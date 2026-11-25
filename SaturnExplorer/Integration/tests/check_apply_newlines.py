"""apply.py must leave every line ending alone: apply and --revert of a patched tree are byte-exact, an LF
tree never gains a CR, and a CRLF tree (git autocrlf) stays pure CRLF -- inserted fences included.

The bug this guards (found on Windows): apply.py read and wrote source files in text mode, so Python
translated every "\\n" it wrote to "\\r\\n" (os.linesep) and an LF checkout came out CRLF, with --revert not
byte-exact either. Linux has no such translation, so the test simulates it: builtins.open is wrapped so a
text-mode open with newline=None writes "\\r\\n" for "\\n", which is what Windows does. Run against a
text-mode apply.py the cases fail (CRs appear); run against this one they pass, because it does binary I/O.

The tree is synthetic (the repository has no Mednafen source) but carries real anchors, so fences are
inserted mid-file, appended, and into a build list. Usage: check_apply_newlines.py <apply.py> <scratch dir>"""
import builtins
import contextlib
import importlib.util
import io
import os
import shutil
import sys

apply_py, work = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
spec = importlib.util.spec_from_file_location("se_apply_nl", apply_py)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

TREE = {
    "src/ss/ss.cpp": ("static void Emulate(EmulateSpecStruct* espec)\n{\n espec->MasterCycles = 0;\n x();\n}\n\n"
                      "static void OutputMIDI(uint8 v)\n{\n}\n"),
    "src/ss/vdp1.cpp": "namespace VDP1\n{\n//puts(\"Kludge\");\nint a;\n}\n",
    "src/ss/scu.inc": "static void f(void)\n{\n d->ReadFunc = rftab[rb];\n}\n",
    "src/ss/sound.cpp": "int sound;\n",
    "src/ss/Makefile.am": "libss_a_SOURCES = \\\n\tss/ss.cpp \\\n\tss/vdp1.cpp \\\n\tss/vdp2.cpp\n",
}
bad = 0


def fail(msg):
    global bad
    print("FAIL: " + msg)
    bad += 1


@contextlib.contextmanager
def windows_text_mode():
    """Text-mode open() with newline=None writes os.linesep == "\\r\\n", as on Windows."""
    real = builtins.open

    def fake(file, mode="r", buffering=-1, encoding=None, errors=None, newline=None, *a, **kw):
        if "b" not in mode and ("w" in mode or "a" in mode or "x" in mode) and newline is None:
            newline = "\r\n"
        return real(file, mode, buffering, encoding, errors, newline, *a, **kw)
    builtins.open = fake
    try:
        yield
    finally:
        builtins.open = real


def run(root, *flags):
    argv, sys.argv = sys.argv, ["apply.py", root] + list(flags)
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out), windows_text_mode():
            try:
                mod.main()
            except SystemExit as e:   # 3 = some anchors of the synthetic tree are absent: not what is tested
                if e.code not in (None, 0, 3):
                    raise
    finally:
        sys.argv = argv
    return out.getvalue()


def snapshot(root):
    snap = {}
    for dp, _, names in os.walk(root):
        for n in names:
            p = os.path.join(dp, n)
            with open(p, "rb") as f:
                snap[os.path.relpath(p, root)] = f.read()
    return snap


def make(root, eol):
    shutil.rmtree(root, ignore_errors=True)
    for rel, text in TREE.items():
        p = os.path.join(root, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(text.replace("\n", eol).encode())


for label, eol in (("LF", "\n"), ("CRLF", "\r\n")):
    root = os.path.join(work, label)
    make(root, eol)
    pristine = snapshot(root)
    out = run(root)
    patched = snapshot(root)
    if "+ applied" not in out or patched == pristine:
        fail("%s: apply changed nothing (the synthetic tree no longer exercises apply.py)" % label)
    for rel, data in patched.items():
        if rel in pristine:
            crlf, lf = data.count(b"\r\n"), data.count(b"\n")
            if eol == "\n" and b"\r" in data:
                fail("%s: apply put a CR into %s" % (label, rel))
            if eol == "\r\n" and (crlf != lf or data.count(b"\r") != crlf):
                fail("%s: %s is no longer pure CRLF after apply (%d CRLF, %d LF)" % (label, rel, crlf, lf))
    run(root)
    if snapshot(root) != patched:
        fail("%s: a second apply changed the tree" % label)
    run(root, "--revert")
    after = snapshot(root)
    for rel in sorted(set(pristine) | set(after)):
        if pristine.get(rel) != after.get(rel):
            fail("%s: --revert is not byte-exact for %s" % (label, rel))

print("apply.py newline handling: %s" % ("FAILED (%d)" % bad if bad else "ok"))
sys.exit(1 if bad else 0)
