#!/usr/bin/env python3
"""
apply.py — wire the Saturn Explorer live-tap into a Yabause checkout.

Saturn Explorer keeps the export module (se_export.{c,h}) and the wire protocol
(SeLiveProtocol.h) as the single source of truth in this repo. This script drops
them into a Yabause tree and inserts the four small hook calls, so your fork
stays a clean vanilla Yabause + a handful of clearly-marked edits.

Usage:
    python3 apply.py /path/to/yabause            # apply (or re-apply)
    python3 apply.py /path/to/yabause --check     # report status, change nothing
    python3 apply.py /path/to/yabause --revert    # remove the edits + copied files

It is idempotent: every edit is fenced with `SE_EXPORT` markers, so running it
again is a no-op, and --revert cleanly removes them. If an anchor can't be found
(a Yabause fork moved the code), the script tells you exactly which hook to add
by hand — see README.md for the manual diffs.

Target layout assumed: <yabause>/yabause/src/{yabause.c,vdp2.c,CMakeLists.txt}.
Pass the repo root; the script also accepts the src dir directly.
"""

import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# SeLiveProtocol.h lives with the drivers; keep one copy, copy it in on apply.
PROTOCOL_SRC = os.path.normpath(os.path.join(HERE, "..", "..", "Drivers", "Common", "src", "SeLiveProtocol.h"))
COPY_FILES = ["se_export.c", "se_export.h"]

BEGIN = "/* --- SE_EXPORT (Saturn Explorer live tap) --- */"
END = "/* --- end SE_EXPORT --- */"


def fence(code):
    return f"{BEGIN}\n{code}\n{END}"


# Each edit: (filename, anchor regex, where to put the block relative to the
# anchor ('after'|'before'), the C code to insert). Anchors are chosen to be
# stable across Yabause versions; if one drifts, we report it precisely.
EDITS = [
    # --- yabause.c ---
    ("yabause.c",
     r'(\n)(int YabauseInit\s*\(yabauseinit_struct)',
     "before_group2",
     '#include "se_export.h"\n'),

    ("yabause.c",
     r'(scsp_set_use_new\(init->use_new_scsp\);\s*\n)(\s*return 0;)',
     "between",
     "   SeExportInit();   /* start the live-tap server */\n"),

    ("yabause.c",
     r'(void YabauseDeInit\(void\)\s*\{\s*\n)',
     "after_group1",
     "   SeExportDeinit();\n"),

    ("yabause.c",
     r'(int YabauseEmulate\(void\)\s*\{\s*\n\s*int oneframeexec = 0;\s*\n)',
     "after_group1",
     "   /* Frame gate: hold here while the debugger has us paused. The gate\n"
     "    * sleeps internally and the export server thread keeps running, so a\n"
     "    * resume/step from Saturn Explorer releases us. */\n"
     "   while (!SeExportGateFrame()) { }\n"),

    # --- vdp2.c ---
    ("vdp2.c",
     r'(#include "vdp2\.h"\n)',
     "after_group1",
     '#include "se_export.h"\n#include "memory.h"\n'),

    ("vdp2.c",
     r'(void Vdp2VBlankOUT\(void\)\s*\{[\s\S]*?static VideoInterface_struct \* saved = NULL;\s*\n)',
     "after_group1",
     "   SeExportSnapshot(Vdp1Ram, Vdp2Ram, Vdp2ColorRam, Vdp2Regs,\n"
     "                    Vdp1Regs, LowWram, HighWram, Vdp1FrameBuffer[0]);\n"),
]

# CMakeLists is handled specially (add se_export.c to the source list).
CMAKE_ANCHOR = re.compile(r'(\n\s*)(vdp1\.c vdp2\.c)')


def find_src(root):
    for cand in (os.path.join(root, "yabause", "src"), os.path.join(root, "src"), root):
        if os.path.isfile(os.path.join(cand, "yabause.c")):
            return cand
    return None


def already(text):
    return BEGIN in text


def apply_edit(text, anchor, mode, code):
    """Insert fenced `code` at `anchor`. Returns (new_text, applied, found)."""
    m = re.search(anchor, text)
    if not m:
        return text, False, False
    block = fence(code.rstrip("\n")) + "\n"
    if mode == "before_group2":
        # keep group1 (a newline), insert before group2
        ins = m.start(2)
        return text[:ins] + block + text[ins:], True, True
    if mode == "after_group1":
        ins = m.end(1)
        return text[:ins] + block + text[ins:], True, True
    if mode == "between":
        ins = m.end(1)
        return text[:ins] + block + text[ins:], True, True
    raise ValueError(mode)


def process_file(src_dir, fname, do_write):
    path = os.path.join(src_dir, fname)
    if not os.path.isfile(path):
        return [f"MISSING  {fname} (not found in {src_dir})"]
    text = original = open(path, encoding="utf-8", errors="surrogateescape").read()
    notes = []
    for f, anchor, mode, code in EDITS:
        if f != fname:
            continue
        tag = code.strip().splitlines()[0]
        # Idempotency: skip if this exact code already fenced in.
        if fence(code.rstrip("\n")) in text:
            notes.append(f"  ok (already)  {tag}")
            continue
        text, applied, found = apply_edit(text, anchor, mode, code)
        if applied:
            notes.append(f"  + inserted    {tag}")
        elif found:
            notes.append(f"  ?             {tag}")
        else:
            notes.append(f"  ANCHOR MISS   {tag}  <-- add by hand (see README)")
    if do_write and text != original:
        open(path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    return notes


def process_cmake(src_dir, do_write):
    path = os.path.join(src_dir, "CMakeLists.txt")
    if not os.path.isfile(path):
        return ["MISSING  CMakeLists.txt"]
    text = open(path, encoding="utf-8", errors="surrogateescape").read()
    if "se_export.c" in text:
        return ["  ok (already)  se_export.c in yabause_SOURCES"]
    new, n = CMAKE_ANCHOR.subn(r"\1se_export.c \2", text, count=1)
    if n == 0:
        return ["  ANCHOR MISS   add se_export.c to yabause_SOURCES by hand"]
    if do_write:
        open(path, "w", encoding="utf-8", errors="surrogateescape").write(new)
    return ["  + inserted    se_export.c into yabause_SOURCES"]


def copy_sources(src_dir, do_write):
    notes = []
    files = [(os.path.join(HERE, f), f) for f in COPY_FILES] + [(PROTOCOL_SRC, "SeLiveProtocol.h")]
    for srcpath, name in files:
        if not os.path.isfile(srcpath):
            notes.append(f"  MISSING SOURCE {srcpath}")
            continue
        dst = os.path.join(src_dir, name)
        if do_write:
            shutil.copyfile(srcpath, dst)
        notes.append(f"  copy          {name}")
    return notes


def revert(src_dir):
    fence_re = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END) + r"\n?", re.DOTALL)
    for fname in ("yabause.c", "vdp2.c"):
        path = os.path.join(src_dir, fname)
        if os.path.isfile(path):
            t = open(path, encoding="utf-8", errors="surrogateescape").read()
            open(path, "w", encoding="utf-8", errors="surrogateescape").write(fence_re.sub("", t))
    cpath = os.path.join(src_dir, "CMakeLists.txt")
    if os.path.isfile(cpath):
        t = open(cpath, encoding="utf-8", errors="surrogateescape").read()
        open(cpath, "w", encoding="utf-8", errors="surrogateescape").write(t.replace("se_export.c vdp1.c", "vdp1.c"))
    for f in COPY_FILES + ["SeLiveProtocol.h"]:
        p = os.path.join(src_dir, f)
        if os.path.isfile(p):
            os.remove(p)
    print("Reverted: removed SE_EXPORT edits and copied files.")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    if not args:
        print(__doc__)
        sys.exit(1)
    src_dir = find_src(args[0])
    if not src_dir:
        print(f"error: no yabause.c under {args[0]} (expected <root>/yabause/src).")
        sys.exit(2)
    print(f"Yabause src: {src_dir}")

    if "--revert" in flags:
        revert(src_dir)
        return

    do_write = "--check" not in flags
    if not do_write:
        print("(--check: reporting only, no files changed)\n")

    notes = []
    if do_write:
        notes += copy_sources(src_dir, do_write)
    for fname in ("yabause.c", "vdp2.c"):
        notes.append(fname + ":")
        notes += process_file(src_dir, fname, do_write)
    notes.append("CMakeLists.txt:")
    notes += process_cmake(src_dir, do_write)

    print("\n".join(notes))
    misses = [n for n in notes if "ANCHOR MISS" in n or "MISSING" in n]
    if misses:
        print("\nSome hooks need manual placement — see Integration/Yabause/README.md.")
        sys.exit(3)
    print("\nDone. Build Yabause as usual; Saturn Explorer can now connect (--live).")


if __name__ == "__main__":
    main()
