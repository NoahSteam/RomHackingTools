#!/usr/bin/env python3
"""Component tests that run the REAL patched Mednafen code, with no BIOS and no game.

F4: vdp1.cpp (VDP1::Update / DoDrawing) driven with a tiny VRAM command list (component/perf_vdp1_harness.cpp).
F5: hw_cpu/m68k and sound.cpp (SOUND_Update, its longjmp paths) (component/perf_m68k_harness.cpp).
F2/F9: SH7095::SetActive and SH7095::Reset (inside the patched ss.cpp) calling the real glue (component/perf_sh2_harness.cpp),
and where the frame-close, state-load and exception-entry hooks sit in the patched text (f2_hook_checks).

The harnesses #include the patched .cpp files themselves, so nothing under test is a copy or a mock; only
the devices around them are stubbed. The Mednafen source is not in this repository, so this is a script
for a machine that has the pinned checkout (the same tree apply.py patches). It takes the pristine bytes
from the checkout's repository HEAD, patches a scratch copy with the CURRENT apply.py, compiles and runs.

Usage: check_component.py <mednafen root> <scratch dir> [c++ compiler] [--drop KEY ...]
Exit status 0 = all checks passed.

--drop KEY removes every injected block containing KEY from the scratch copy after patching, and the
run must then FAIL (exit 0 means "the mutation was detected"; 1 means the tests did not notice the hook
was gone). Keys: "SeSsVdp1Debit" (command work), "SE_PERF_VDP1_DMA_DISCARDED", "IdleCycleCount += 4;"
(the idle hook in the 68000 core), "SE_PERF_M68K_IDLE_CYCLES" (the SOUND_Update exit), "SE_SLAVE_ON_EDGE" / "SE_SLAVE_OFF_EDGE" (the slave's
power edges in SetActive), "SE_CPU_RESET_HOOK" (the top of Reset)."""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_migration as cm  # noqa: E402
import bus_decode  # noqa: E402  (F8)


def f8_decoder_checks(work):
    """The windows the bus buckets rest on are still where bus_decode.py says (scu.inc, ss.cpp)."""
    import re
    bad = 0
    scu = open(os.path.join(work, "src/ss/scu.inc"), errors="replace").read()
    ss = open(os.path.join(work, "src/ss/ss.cpp"), errors="replace").read()
    for lo, hi in bus_decode.SCU_DECODER_WINDOWS:
        if not re.search(r"A >= 0x%08x && A <= 0x%08x" % (lo, hi), scu, re.I):
            print("FAIL: scu.inc no longer decodes 0x%08x-0x%08x as the bus map assumes" % (lo, hi))
            bad += 1
    for lo, hi in ((0x00200000, 0x003FFFFF), (0x00000000, 0x000FFFFF), (0x00100000, 0x0017FFFF),
                   (0x00180000, 0x001FFFFF), (0x01000000, 0x01FFFFFF)):
        if not re.search(r"A >= 0x%08x && A <= 0x%08x" % (lo, hi), ss, re.I):
            print("FAIL: ss.cpp no longer decodes CS0 0x%08x-0x%08x as the bus map assumes" % (lo, hi))
            bad += 1
    return bad


def f11_checks(work):
    """The DMA / DSP counters sit on the definitions documented in Docs/Codebase/Systems/Profiler.md."""
    import re
    bad = 0
    scu = open(os.path.join(work, "src/ss/scu.inc"), errors="replace").read()
    # F11. DMA: counted once per transfer that STARTED -- after StartDMATransfer's three refusals
    # (illegal source, illegal destination, same-bus) -- with the byte count it was started with; an
    # indirect chain reaches it once per table entry (NextIndirect calls StartDMATransfer).
    a = scu.index("static bool StartDMATransfer")
    b = scu.index("static bool NextIndirect")
    body = scu[a:b]
    hook = body.find("SE_PERF_DMA_TRANSFERS")
    refusals = [m.start() for m in re.finditer(r"return false;", body)]
    if body.count("SE_PERF_DMA_TRANSFERS") != 1 or len(refusals) != 3 or hook < max(refusals):
        print("FAIL: the DMA counters are not after StartDMATransfer's three refusals")
        bad += 1
    if "SE_PERF_DMA_LEVEL_BYTES" not in body or "+= bc" not in body:
        print("FAIL: DMA bytes are not the requested byte count")
        bad += 1
    # DSP: two SH-2 clocks per dispatch of the scheduled loop in SCU_UpdateDSP; the control-register
    # Step bypasses it and is not counted.
    u = scu.index("sscpu_timestamp_t SCU_UpdateDSP")
    upd = scu[u:scu.index("static void DSP_Reset", u)]
    loop = upd.find("while(MDFN_LIKELY(DSP.CycleCounter > 0))")
    if (upd.count("SE_PERF_DSP_CYCLES") != 1 or loop < 0 or not
            loop < upd.find("DSP.CycleCounter -= 2;") < upd.find("SE_PERF_DSP_CYCLES] += 2")):
        print("FAIL: the DSP counter is not on the scheduled dispatch loop")
        bad += 1
    s0 = scu.index("(1U << 17)")
    step = scu[s0:s0 + 700]
    if "SeSsPerfC" in step:
        print("FAIL: the manual DSP step is counted (the documented definition excludes it)")
        bad += 1
    return bad


def f2_hook_checks(work):
    """F2 + F6 + F9: where the lifecycle hooks sit in the patched source (the places the glue's contract assumes)."""
    import re
    bad = 0
    ss = open(os.path.join(work, "src/ss/ss.cpp"), errors="replace").read()
    inc = open(os.path.join(work, "src/ss/sh7095.inc"), errors="replace").read()
    ops = open(os.path.join(work, "src/ss/sh7095_ops.inc"), errors="replace").read()
    # the frame is closed on the PRE-rebase clock: after SMPC_EndFrame (which can still change the slave's
    # power), before RebaseTS and the AdjustTS calls that move the timestamps
    e = ss.index("static void Emulate(")
    body = ss[e:ss.index("static void OutputMIDI", e)]
    order = [body.find(k) for k in ("SMPC_EndFrame(espec, end_ts);", "SeMednafenFrameClose(end_ts", "RebaseTS(end_ts);",
                                    "CPU[c].AdjustTS(-end_ts);", "SeMednafenEndFrameHook();")]
    if min(order) < 0 or order != sorted(order):
        print("FAIL: SeMednafenFrameClose is not between SMPC_EndFrame and the rebase (positions %r)" % order)
        bad += 1
    if "SE_PERF_SSH2_ON_CYCLES" in body:
        print("FAIL: Emulate still counts the slave on-time by the all-or-nothing rule")
        bad += 1
    # a load never calls SetActive: the state-load hook follows the last PostStateLoad
    p = ss.find("CPU[1].PostStateLoad(")
    if p < 0 or not (p < ss.find("SeMednafenStateLoaded(", p) < ss.find("}", ss.find("SeMednafenStateLoaded(", p) + 80)):
        print("FAIL: SeMednafenStateLoaded does not follow CPU[1].PostStateLoad")
        bad += 1
    # SetActive: the on edge after the timestamp is given, the off edge before it is thrown away
    a = inc.index("void SH7095::SetActive(bool active)")
    sa = inc[a:inc.index("void SH7095::AdjustTS", a)]
    on, off = sa.find("SE_SLAVE_ON_EDGE"), sa.find("SE_SLAVE_OFF_EDGE")
    if not (0 < sa.find("timestamp = SH7095_mem_timestamp;") < on < sa.find("Reset(true);")) or not \
            (sa.find("else if(!active") < off < sa.find("timestamp = SS_EVENT_DISABLED_TS;")):
        print("FAIL: the SetActive hooks are not on the right sides of the transitions")
        bad += 1
    # Reset: on a powered CPU only (after the disabled early return)
    r = inc.index("void NO_INLINE SH7095::Reset(")
    rb = inc[r:r + 600]
    if not (0 <= rb.find("return;") < rb.find("SE_CPU_RESET_HOOK")):
        print("FAIL: the Reset hook is not after the disabled early return")
        bad += 1
    # every exception the core takes except TRAPA is hooked, right after its Exception() statement
    for kind in ("INT", "NMI", "CPUADDR", "DMAADDR", "ILLINSTR", "ILLSLOT"):
        m = re.search(r"Exception\(EXCEPTION_%s, [^;\n]*;[^\n]*\n(?:/\*[^\n]*\n)?[^\n]*SE_OWN_EXC_%s" % (kind, kind), ops)
        if not m:
            print("FAIL: no ownership hook after Exception(EXCEPTION_%s, ...)" % kind)
            bad += 1
    if re.search(r"Exception\(EXCEPTION_TRAP[^\n]*\n[^\n]*SE_OWN_EXC", ops):
        print("FAIL: TRAPA is hooked, but the glue observes it as a call")
        bad += 1
    return bad


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
    if not drops:
        if f8_decoder_checks(work) or f11_checks(work) or f2_hook_checks(work):
            return 1
        print("decoder and hook-placement checks: ok")
    apply_mod = cm.load_new()
    for key in drops:
        for rel in ("src/ss/vdp1.cpp", "src/ss/sound.cpp", "src/hw_cpu/m68k/m68k.cpp", "src/ss/sh7095.inc"):
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
    # The real glue (unwired: no emulator symbols) and the real shadow call stack, for the SH-2 harness.
    glue_objs = []
    for c_src in (os.path.join(HERE, "se_mednafen_glue.c"), os.path.join(HERE, "..", "Common", "se_export.c")):
        o = os.path.join(scratch, os.path.basename(c_src) + ".o")
        r = subprocess.run(["gcc", "-std=gnu99", "-O1", "-g", "-w", "-I" + os.path.join(HERE, "..", "Common"),
                            "-I" + HERE, "-c", c_src, "-o", o], capture_output=True, text=True)
        if r.returncode:
            print("%s does not compile:\n%s" % (c_src, r.stderr[-1500:]))
            return 1
        glue_objs.append(o)
    for name, define, src, extra in (
            ("f4_vdp1", "SE_VDP1_CPP", "perf_vdp1_harness.cpp", []),
            ("f5_m68k", "SE_SOUND_CPP", "perf_m68k_harness.cpp", [m68k_o]),
            ("f2_sh2", "SE_SS_CPP", "perf_sh2_harness.cpp", glue_objs + ["-lpthread"])):
        if not os.path.isfile(os.path.join(HERE, "component", src)):
            continue
        exe = os.path.join(scratch, name)
        target = {"SE_VDP1_CPP": "vdp1.cpp", "SE_SOUND_CPP": "sound.cpp", "SE_SS_CPP": "ss.cpp"}[define]
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
