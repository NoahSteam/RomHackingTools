#!/usr/bin/env python3
"""Component tests that run the REAL patched Mednafen code, with no BIOS and no game.

F4: vdp1.cpp (VDP1::Update / DoDrawing) driven with a tiny VRAM command list (component/perf_vdp1_harness.cpp).
F5: hw_cpu/m68k and sound.cpp (SOUND_Update, its longjmp paths) (component/perf_m68k_harness.cpp).

The harnesses #include the patched .cpp files themselves, so nothing under test is a copy or a mock; only
the devices around them are stubbed. The Mednafen source is not in this repository, so this is a script
for a machine that has the pinned checkout (the same tree apply.py patches). It takes the pristine bytes
from the checkout's repository HEAD, patches a scratch copy with the CURRENT apply.py, compiles and runs.

Usage: check_component.py <mednafen root> <scratch dir> [c++ compiler] [--drop KEY ...]
Exit status 0 = all checks passed.

--drop KEY removes every injected block containing KEY from the scratch copy after patching, and the
run must then FAIL (exit 0 means "the mutation was detected"; 1 means the tests did not notice the hook
was gone). Keys: "SeSsVdp1Debit" (command work), "SE_PERF_VDP1_DMA_DISCARDED", "IdleCycleCount += 4;"
(the idle hook in the 68000 core), "SE_PERF_M68K_IDLE_CYCLES" (the SOUND_Update exit)."""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_migration as cm  # noqa: E402


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    args = sys.argv[1:]
    drops = []
    while "--drop" in args:
        i = args.index("--drop")
        drops.append(args[i + 1])
        del args[i:i + 2]
    root, scratch = os.path.abspath(args[0]), os.path.abspath(args[1])
    cxx = args[2] if len(args) > 2 else os.environ.get("CXX", "g++")
    os.makedirs(scratch, exist_ok=True)
    work = cm.make_scratch(root, scratch)
    rc, out = cm.run_apply(cm.NEW_APPLY, work)
    if rc != 0 or "ANCHOR MISS" in out:
        print("apply.py failed:\n" + out[-1000:])
        return 1
    apply_mod = cm.load_new()
    for key in drops:
        for rel in ("src/ss/vdp1.cpp", "src/ss/sound.cpp", "src/hw_cpu/m68k/m68k.cpp"):
            path = os.path.join(work, rel)
            text = open(path).read()
            new, n = apply_mod.retire_fences_text(text, key)
            if n:
                open(path, "w").write(new)
                print("mutation: dropped %d block(s) containing %r from %s" % (n, key, rel))
    ss = os.path.join(work, "src", "ss")
    flags = ["-std=gnu++11", "-O1", "-g", "-fsigned-char", "-fwrapv", "-fexceptions", "-DHAVE_CONFIG_H",
             "-I" + os.path.join(work, "include"), "-I" + os.path.join(work, "intl"), "-I" + ss,
             "-I" + os.path.join(HERE, "..", "Common"), "-w"]
    link = ["-Wl,--unresolved-symbols=ignore-in-object-files"]
    # The patched 68000 core, linked into the sound harness (the VDP1 one does not use it).
    m68k_o = os.path.join(scratch, "m68k.o")
    r = subprocess.run([cxx] + flags + ["-c", os.path.join(work, "src/hw_cpu/m68k/m68k.cpp"), "-o", m68k_o],
                       capture_output=True, text=True)
    if r.returncode:
        print("m68k.cpp does not compile:\n" + r.stderr[-1500:])
        return 1
    bad = 0
    for name, define, src, extra in (
            ("f4_vdp1", "SE_VDP1_CPP", "perf_vdp1_harness.cpp", []),
            ("f5_m68k", "SE_SOUND_CPP", "perf_m68k_harness.cpp", [m68k_o])):
        if not os.path.isfile(os.path.join(HERE, "component", src)):
            continue
        exe = os.path.join(scratch, name)
        target = "vdp1.cpp" if define == "SE_VDP1_CPP" else "sound.cpp"
        cmd = [cxx] + flags + ['-D%s="%s"' % (define, os.path.join(ss, target)),
                               os.path.join(HERE, "component", src)] + extra + ["-o", exe] + link
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=ss)
        if r.returncode:
            print("== %s: harness does not build:\n%s" % (name, r.stderr[-3000:]))
            bad += 1
            continue
        print("== " + name)
        r = subprocess.run([exe], capture_output=True, text=True)
        print(r.stdout, end="")
        if r.returncode:
            print(r.stderr[-500:])
            bad += 1
    if drops:
        print("mutation %s" % ("detected" if bad else "NOT DETECTED"))
        return 0 if bad else 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
