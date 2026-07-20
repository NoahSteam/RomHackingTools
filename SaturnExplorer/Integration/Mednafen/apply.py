#!/usr/bin/env python3
"""
apply.py — wire the Saturn Explorer live-tap into a Mednafen (Beetle Saturn) tree.

Mednafen keeps the memory Saturn Explorer wants as *file-scope static* (VDP2
VRAM/CRAM/RawRegs, WorkRAML/H, the VDP1 registers), unreachable by `extern` from a
separate file. So this patcher injects tiny C-linkage **accessors** at the end of
vdp1.cpp / vdp2.cpp / ss.cpp (where those statics are visible — a re-opened namespace
block), copies the portable server (../Common/se_export.{c,h} + SeLiveProtocol.h) plus
the Mednafen glue (se_mednafen_glue.c) into the tree, and injects ONE per-frame call
into ss.cpp's Emulate(). That call lazy-starts the server on the first frame, so no
Load()/CloseGame() anchoring is needed.

Usage:
    python3 apply.py /path/to/mednafen                 # apply (or re-apply)
    python3 apply.py /path/to/mednafen --check          # report status, change nothing
    python3 apply.py /path/to/mednafen --revert         # remove edits + copied files
    python3 apply.py /path/to/mednafen --with-pause     # also inject the pause/step gate

It is idempotent: accessor/hook blocks are fenced with `SE_EXPORT` markers (updated in
place, never duplicated) and --revert removes them. If an anchor can't be found (a
Mednafen version moved the code), the script says exactly what to add by hand — see
README.md. Symbol names were mapped against libretro-mirrors/mednafen-git src/ss.

The one thing this can't finish blind is the **Tier-3 execution-breakpoint** wiring
(Mednafen's debug API); those helpers stay TODO(mednafen) in the copied glue. Live
view, Watch/Hex (incl. work-RAM pokes), SH-2 registers, and pause/step all wire up.

Target layout assumed: <mednafen>/src/ss/{vdp1.cpp,vdp2.cpp,ss.cpp}. Pass the repo
root; the script also accepts the src/ss dir directly.
"""

import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHARED_DIR = os.path.normpath(os.path.join(HERE, "..", "Common"))
# Copied verbatim from Common; the glue is copied from HERE with SE_MEDNAFEN_WIRED
# prepended so no per-file build flag is needed.
COMMON_FILES = ["se_export.c", "se_export.h", "SeLiveProtocol.h"]
GLUE_FILE = "se_mednafen_glue.c"

BEGIN = "/* --- SE_EXPORT (Saturn Explorer live tap) --- */"
END = "/* --- end SE_EXPORT --- */"


def fence(code):
    return f"{BEGIN}\n{code}\n{END}"


# ---- Accessor blocks appended at end-of-file (file-scope statics are visible to a
#      re-opened namespace block later in the same translation unit). Keyed by a
#      stable accessor name so re-runs update in place. ----
VDP1_ACCESSORS = """\
/* Expose VDP1's file-scope statics to the glue (C linkage). FB[!FBDrawWhich] is the
   displayed (front) bank; confirm against your build if the framebuffer looks stale. */
namespace MDFN_IEN_SS { namespace VDP1 {
extern "C" const unsigned short* SsDbgVdp1Vram(void) { return (const unsigned short*)VRAM; }
extern "C" const unsigned short* SsDbgVdp1Fb(void)   { return (const unsigned short*)FB[!FBDrawWhich]; }
extern "C" void SsDbgVdp1Regs(unsigned short o[11]) {
   o[0]=TVMR; o[1]=FBCR; o[2]=PTMR; o[3]=EWDR; o[4]=EWLR; o[5]=EWRR;
   o[6]=0;    o[7]=EDSR; o[8]=LOPR; o[9]=0;    o[10]=0;   /* ENDR/COPR/MODR write-only/computed */
}
}}"""

VDP2_ACCESSORS = """\
/* Expose VDP2's file-scope statics to the glue (C linkage). */
namespace MDFN_IEN_SS { namespace VDP2 {
extern "C" const unsigned short* SsDbgVdp2Vram(void) { return (const unsigned short*)VRAM; }
extern "C" const unsigned short* SsDbgCram(void)     { return (const unsigned short*)CRAM; }
extern "C" const unsigned short* SsDbgRawRegs(void)  { return (const unsigned short*)RawRegs; }
}}"""

SS_ACCESSORS = """\
/* Expose ss.cpp's file-scope statics + SH-2 cores to the glue (C linkage). */
namespace MDFN_IEN_SS {
extern "C" const unsigned short* SsDbgWramL(void) { return (const unsigned short*)WorkRAML; }
extern "C" const unsigned short* SsDbgWramH(void) { return (const unsigned short*)WorkRAMH; }
extern "C" void SsDbgSh2Regs(int cpu, unsigned int o[23]) {
   SH7095& c = CPU[cpu ? 1 : 0];
   for (int i = 0; i < 16; ++i) o[i] = c.GetRegister(SH7095::GSREG_R0 + i, 0, 0);
   o[16]=c.GetRegister(SH7095::GSREG_SR,0,0);   o[17]=c.GetRegister(SH7095::GSREG_GBR,0,0);
   o[18]=c.GetRegister(SH7095::GSREG_VBR,0,0);  o[19]=c.GetRegister(SH7095::GSREG_MACH,0,0);
   o[20]=c.GetRegister(SH7095::GSREG_MACL,0,0); o[21]=c.GetRegister(SH7095::GSREG_PR,0,0);
   /* TODO(mednafen): confirm the PC enumerator the disassembler should track. The
      SH7095 pipeline exposes GSREG_PC_IF (fetch stage), GSREG_PC_ID (decode), and
      GSREG_RPC; PC_IF is the usual "next instruction" for a running core. */
   o[22]=c.GetRegister(SH7095::GSREG_PC_IF,0,0);
}
extern "C" void SsDbgPokeByte(unsigned int addr, unsigned char val) {
   /* Poke work RAM directly (bypasses the SH-2 cache — the conventional debugger
      write), preserving big-endian order: WorkRAM is host-order uint16, so a Saturn
      (big-endian) byte at an even address is the word's high byte. Covers the Hex
      Editor's work-RAM edits; other regions would need the ss bus poke (TODO). */
   unsigned short* w = 0; unsigned int off = 0;
   if      (addr >= 0x06000000u && addr < 0x06100000u) { w = (unsigned short*)WorkRAMH; off = addr - 0x06000000u; }
   else if (addr >= 0x00200000u && addr < 0x00300000u) { w = (unsigned short*)WorkRAML; off = addr - 0x00200000u; }
   if (!w) return;
   unsigned short v = w[off >> 1];
   if (off & 1u) v = (unsigned short)((v & 0xFF00u) | val);         /* BE low byte  */
   else          v = (unsigned short)((v & 0x00FFu) | (val << 8));  /* BE high byte */
   w[off >> 1] = v;
}
}"""

# EOF-appended accessor edits: (filename, block, key).
APPEND_EDITS = [
    ("vdp1.cpp", VDP1_ACCESSORS, "SsDbgVdp1Vram"),
    ("vdp2.cpp", VDP2_ACCESSORS, "SsDbgVdp2Vram"),
    ("ss.cpp",   SS_ACCESSORS,   "SsDbgWramL"),
]

# File-scope forward declarations (extern "C" is illegal inside a function body, so
# the call sites below are plain calls that resolve to these). Prepended at BOF.
FWD_DECLS = (
    "extern \"C\" void SeMednafenFrameHook(void);\n"
    "extern \"C\" int  SeExportGateFrame(void);\n"
)

# Per-frame snapshot call, injected after the frame's cycle count is finalized.
FRAME_HOOK = (
    "   /* Saturn Explorer live tap: publish this frame (lazy-starts the server). */\n"
    "   SeMednafenFrameHook();\n"
)
FRAME_ANCHOR = r'(espec->MasterCycles\s*=\s*[^;]*;\s*\n)'

# Optional pause/step gate at the top of Emulate() (opt-in: can disturb audio timing).
GATE_HOOK = (
    "   /* Saturn Explorer pause/step gate (remove if it disturbs audio timing). */\n"
    "   while (!SeExportGateFrame()) { }\n"
)
GATE_ANCHOR = r'(\bEmulate\s*\(\s*EmulateSpecStruct\s*\*\s*\w+\s*\)\s*\{\s*\n)'

FENCE_RE = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END), re.DOTALL)


def find_src(root):
    for cand in (os.path.join(root, "src", "ss"), os.path.join(root, "ss"), root):
        if os.path.isfile(os.path.join(cand, "ss.cpp")):
            return cand
    return None


def apply_anchored(text, anchor, code, key):
    """Insert fenced `code` after `anchor` unless a block with `key` already exists.
    Returns (text, note)."""
    block = fence(code.rstrip("\n"))
    existing = next((m.group(0) for m in FENCE_RE.finditer(text) if key in m.group(0)), None)
    if existing is not None:
        if existing == block:
            return text, f"  ok (already)  {key}"
        return text.replace(existing, block, 1), f"  ~ updated     {key}"
    m = re.search(anchor, text)
    if not m:
        return text, f"  ANCHOR MISS   {key}  <-- add by hand (see README)"
    ins = m.end(1)
    return text[:ins] + block + "\n" + text[ins:], f"  + inserted    {key}"


def apply_append(text, code, key):
    """Append fenced `code` at EOF unless a block with `key` already exists."""
    block = fence(code.rstrip("\n"))
    existing = next((m.group(0) for m in FENCE_RE.finditer(text) if key in m.group(0)), None)
    if existing is not None:
        if existing == block:
            return text, f"  ok (already)  {key} (accessors)"
        return text.replace(existing, block, 1), f"  ~ updated     {key} (accessors)"
    sep = "" if text.endswith("\n") else "\n"
    return text + sep + "\n" + block + "\n", f"  + appended    {key} (accessors)"


def apply_prepend(text, code, key):
    """Prepend fenced `code` at BOF unless a block with `key` already exists."""
    block = fence(code.rstrip("\n"))
    existing = next((m.group(0) for m in FENCE_RE.finditer(text) if key in m.group(0)), None)
    if existing is not None:
        if existing == block:
            return text, f"  ok (already)  {key} (fwd decls)"
        return text.replace(existing, block, 1), f"  ~ updated     {key} (fwd decls)"
    return block + "\n" + text, f"  + prepended   {key} (fwd decls)"


def process_ss(src_dir, do_write, with_pause):
    path = os.path.join(src_dir, "ss.cpp")
    if not os.path.isfile(path):
        return ["MISSING  ss.cpp"]
    text = original = open(path, encoding="utf-8", errors="surrogateescape").read()
    notes = ["ss.cpp:"]
    text, n = apply_prepend(text, FWD_DECLS, "SeMednafenFrameHook(void)")
    notes.append(n)
    text, n = apply_anchored(text, FRAME_ANCHOR, FRAME_HOOK, "SeMednafenFrameHook();")
    notes.append(n)
    if with_pause:
        text, n = apply_anchored(text, GATE_ANCHOR, GATE_HOOK, "while (!SeExportGateFrame())")
        notes.append(n)
    text, n = apply_append(text, SS_ACCESSORS, "SsDbgWramL")
    notes.append(n)
    if do_write and text != original:
        open(path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    return notes


def process_append_file(src_dir, fname, block, key, do_write):
    path = os.path.join(src_dir, fname)
    if not os.path.isfile(path):
        return [f"MISSING  {fname}"]
    text = original = open(path, encoding="utf-8", errors="surrogateescape").read()
    text, note = apply_append(text, block, key)
    if do_write and text != original:
        open(path, "w", encoding="utf-8", errors="surrogateescape").write(text)
    return [f"{fname}:", note]


def copy_sources(src_dir, do_write):
    notes = []
    for name in COMMON_FILES:
        srcpath = os.path.join(SHARED_DIR, name)
        if not os.path.isfile(srcpath):
            notes.append(f"  MISSING SOURCE {srcpath}")
            continue
        if do_write:
            shutil.copyfile(srcpath, os.path.join(src_dir, name))
        notes.append(f"  copy          {name}")
    # Glue: copy with SE_MEDNAFEN_WIRED defined so the snapshot path compiles.
    glue_src = os.path.join(HERE, GLUE_FILE)
    if os.path.isfile(glue_src):
        if do_write:
            body = open(glue_src, encoding="utf-8", errors="surrogateescape").read()
            open(os.path.join(src_dir, GLUE_FILE), "w", encoding="utf-8",
                 errors="surrogateescape").write("#define SE_MEDNAFEN_WIRED 1\n" + body)
        notes.append(f"  copy          {GLUE_FILE} (+SE_MEDNAFEN_WIRED)")
    else:
        notes.append(f"  MISSING SOURCE {glue_src}")
    return notes


# Build wiring: add the two C sources to whatever lists the ss sources. Mednafen has
# used autotools (Makefile.am) and meson (meson.build) across versions, so try to find
# a file that references ss/vdp1.cpp (or vdp1.cpp) and add ours beside it.
BUILD_CANDIDATES = [
    os.path.join("src", "Makefile.am"),
    os.path.join("src", "meson.build"),
    os.path.join("src", "ss", "Makefile.inc"),
]
NEW_SOURCES = ["se_export.c", "se_mednafen_glue.c"]


def process_build(root, do_write):
    for rel in BUILD_CANDIDATES:
        path = os.path.join(root, rel)
        if not os.path.isfile(path):
            continue
        text = open(path, encoding="utf-8", errors="surrogateescape").read()
        if "se_mednafen_glue.c" in text:
            return [f"{rel}:", "  ok (already)  se_export.c + se_mednafen_glue.c"]
        # Match the vdp1 source entry with whatever prefix/suffix the file uses.
        m = re.search(r'([ \t]*)(["\']?)((?:ss/)?vdp1\.cpp)\2', text)
        if not m:
            continue
        indent, quote, ref = m.group(1), m.group(2), m.group(3)
        prefix = ref[: ref.index("vdp1.cpp")]   # "ss/" or ""
        add = "".join(f"{indent}{quote}{prefix}{s}{quote}{_line_suffix(text, m.end())}"
                      for s in NEW_SOURCES)
        new = text[: m.start()] + add + text[m.start():]
        if do_write:
            open(path, "w", encoding="utf-8", errors="surrogateescape").write(new)
        return [f"{rel}:", f"  + inserted    {', '.join(NEW_SOURCES)}"]
    return ["build file:",
            "  ANCHOR MISS   add se_export.c + se_mednafen_glue.c to the ss build by hand"]


def _line_suffix(text, pos):
    """Mirror how the anchored source line ends (autotools ' \\\n' vs meson ',\n')."""
    eol = text.find("\n", pos)
    line = text[pos:eol if eol >= 0 else len(text)]
    if line.rstrip().endswith("\\"):
        return " \\\n"
    if line.rstrip().endswith(","):
        return ",\n"
    return "\n"


def revert(src_dir, root):
    fence_re = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END) + r"\n?", re.DOTALL)
    for fname in ("vdp1.cpp", "vdp2.cpp", "ss.cpp"):
        path = os.path.join(src_dir, fname)
        if os.path.isfile(path):
            t = open(path, encoding="utf-8", errors="surrogateescape").read()
            open(path, "w", encoding="utf-8", errors="surrogateescape").write(fence_re.sub("", t))
    for name in COMMON_FILES + [GLUE_FILE]:
        p = os.path.join(src_dir, name)
        if os.path.isfile(p):
            os.remove(p)
    for rel in BUILD_CANDIDATES:
        path = os.path.join(root, rel)
        if os.path.isfile(path):
            t = open(path, encoding="utf-8", errors="surrogateescape").read()
            for s in NEW_SOURCES:
                t = re.sub(r'[ \t]*["\']?(?:ss/)?' + re.escape(s) + r'["\']?[ \t]*(?:\\|,)?\n', "", t)
            open(path, "w", encoding="utf-8", errors="surrogateescape").write(t)
    print("Reverted: removed SE_EXPORT edits and copied files.")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    if not args:
        print(__doc__)
        sys.exit(1)
    root = args[0]
    src_dir = find_src(root)
    if not src_dir:
        print(f"error: no ss.cpp under {root} (expected <root>/src/ss).")
        sys.exit(2)
    # If the caller passed src/ss directly, recover the repo root for build files.
    if os.path.basename(src_dir) == "ss" and os.path.basename(os.path.dirname(src_dir)) == "src":
        root = os.path.dirname(os.path.dirname(src_dir))
    print(f"Mednafen ss src: {src_dir}")

    if "--revert" in flags:
        revert(src_dir, root)
        return

    do_write = "--check" not in flags
    with_pause = "--with-pause" in flags
    if not do_write:
        print("(--check: reporting only, no files changed)\n")

    notes = []
    if do_write:
        notes.append("copy:")
        notes += copy_sources(src_dir, do_write)
    notes += process_append_file(src_dir, "vdp1.cpp", VDP1_ACCESSORS, "SsDbgVdp1Vram", do_write)
    notes += process_append_file(src_dir, "vdp2.cpp", VDP2_ACCESSORS, "SsDbgVdp2Vram", do_write)
    notes += process_ss(src_dir, do_write, with_pause)
    notes += process_build(root, do_write)

    print("\n".join(notes))
    misses = [n for n in notes if "ANCHOR MISS" in n or "MISSING" in n]
    if misses:
        print("\nSome hooks need manual placement — see Integration/Mednafen/README.md.")
        sys.exit(3)
    print("\nDone. Build Mednafen as usual; Saturn Explorer can now connect (--live).")
    if not with_pause:
        print("(Pause/step gate not injected — pass --with-pause to add it.)")


if __name__ == "__main__":
    main()
