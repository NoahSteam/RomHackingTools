#!/usr/bin/env python3
"""Component tests that run the REAL patched Mednafen code, with no BIOS and no game.

F4: vdp1.cpp (VDP1::Update / DoDrawing) driven with a tiny VRAM command list (component/perf_vdp1_harness.cpp).
F5: hw_cpu/m68k and sound.cpp (SOUND_Update, its longjmp paths) (component/perf_m68k_harness.cpp).
F2/F9: SH7095::SetActive and SH7095::Reset (inside the patched ss.cpp) calling the real glue (component/perf_sh2_harness.cpp),
and where the frame-close, state-load and exception-entry hooks sit in the patched text (f2_hook_checks).
F8: bus-request counts of SH-2 cache fills and the on-chip DMAC, through the real SH7095 core and the real
BSC_BusRead / BSC_BusWrite hook (component/perf_bus_harness.cpp; the expected numbers are component/BUS_EXPECTED.md).

The harnesses #include the patched .cpp files themselves, so nothing under test is a copy or a mock; only
the devices around them are stubbed. The Mednafen source is not in this repository, so this is a script
for a machine that has the pinned checkout (the same tree apply.py patches). It takes the pristine bytes
from the checkout's repository HEAD, patches a scratch copy with the CURRENT apply.py, compiles and runs.

Usage: check_component.py <mednafen root> <scratch dir> [c++ compiler] [--drop KEY ...] [--mutate NAME ...]
                          [--only HARNESS ...] [--skip-if-missing]
Exit status 0 = all checks passed.
--only f4_vdp1|f5_m68k|f2_sh2|f8_bus|hwev_vdp1|hwev_sh2 runs just those harnesses (and no text checks).
--skip-if-missing exits 77 (CTest's SKIP_RETURN_CODE) when there is no Mednafen checkout at <root>, so the
caller can be a ctest that is skipped on machines without one.

--drop KEY removes every injected block containing KEY from the scratch copy after patching, and the
run must then FAIL (exit 0 means "the mutation was detected"; 1 means the tests did not notice the hook
was gone). Keys: "SeSsVdp1Debit" (command work), "SE_PERF_VDP1_DMA_DISCARDED", "IdleCycleCount += 4;"
(the idle hook in the 68000 core), "SE_PERF_M68K_IDLE_CYCLES" (the SOUND_Update exit), "SE_SLAVE_ON_EDGE" / "SE_SLAVE_OFF_EDGE" (the slave's
power edges in SetActive), "SE_CPU_RESET_HOOK" (the top of Reset).

--mutate NAME edits the bus hooks in the SCRATCH copy (never the checkout) and the f8_bus harness must then
fail (exit 0 = detected): "bus-burst" (burst reads are not counted: the three burst reads of a cache fill and
the 16-byte DMA's reads 1..3 vanish), "bus-dma-read" (a read made by the DMAC, SH2DMAHax set, is not counted),
"bus-dma-write" (same for writes), "bus-all-reads" / "bus-all-writes" (the hook is gone).
The device-sync guard in DBG_CPUHandler (debug.inc, SeSsHookNeedsSync) is checked by the f2_sh2 harness, and
these mutations must make it fail: "sync-guard-removed" (ForceEventUpdates is unconditional again, so a
profile-only instruction syncs), "sync-never" (it never runs), "sync-no-breakpoint" (a breakpoint found on the
instruction no longer syncs), "sync-flag-only" (the guard keys on our flag alone: a foreign callback is skipped),
"sync-flag-profile" (the flag is also set for profiling alone).
The native debugger's callback (SeSsNativeSetCPUCallback, the DebuggerInfo entry) is checked by the same harness, and
these must make it fail: "native-table-upstream" (the table entry is DBG_SetCPUCallback again: the debugger's NULL drops
ours for good), "native-null-not-reapplied" (a NULL request is not answered with our mode), "native-not-installed"
(what the debugger asks for is not installed), "native-stolen-by-edit" (an SE install edit takes the callback while
the debugger holds it), "native-both-cpus" (the both-CPU handler stays on under the debugger's callback),
"native-not-noted" (the glue is never told that the code ran unobserved: no stack restart), "native-in-handler"
(the callback changes inside a handler call), "native-null-always-drops" (a NULL request removes ours, then we sync).
A takeover released inside the handler call it was made in (the deferred sync finds the debugger's callback already
gone) must still be a gap, and these must make it fail: "native-transient-lost" (the takeover is not recorded),
"native-transient-undelivered" (the sync does not tell the glue), "native-transient-unconditional" (it is told even
when we want no callback), "native-transient-kept" (the record is never cleared: every later sync repeats it).

Hardware event log (component/hwev_harness.cpp: hwev_vdp1 on the patched vdp1.cpp, hwev_sh2 on the patched ss.cpp with
the real glue). --drop takes every key apply.py writes for it (apply.HWEV_KEYS: SE_HWEV_FWD, SE_HWEV_HOOK_*,
SE_HWEV_CPU_PC); dropping any of them must make the run fail -- by the harnesses where they reach the site, and by
hwev_text_checks (present exactly once, behind a SeSsHwEvMask test, in the right file) where they cannot (CD block,
SMPC, VDP2 display). Mutations: "hwev-mask-ignored" (the sites fire with the mask clear: VDP1, an exception, the DMAC,
the SCU) and "hwev-wrong-kind" (a site reports another kind); every harness they touch must fail."""
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
    # the cache mode label travels with the sample: assigned right after the emulator picks NeedEmuICache
    if not re.search(r"NeedEmuICache = \(cpucache_emumode == CPUCACHE_EMUMODE_FULL\);\s*\n(?:/\*[^\n]*\*/\s*\n)?[^\n]*SeSsCacheMode = NeedEmuICache \? SE_PERF_CACHE_ON : SE_PERF_CACHE_OFF;", ss):
        print("FAIL: SeSsCacheMode is not set right after NeedEmuICache is chosen")
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


BUS_MUTATIONS = {
    "bus-burst": (" SeSsPerfBus(A, 0);   /* SE_PERF_BUS_READ */", " if(!BurstHax) SeSsPerfBus(A, 0);   /* SE_PERF_BUS_READ */"),
    "bus-dma-read": (" SeSsPerfBus(A, 0);   /* SE_PERF_BUS_READ */", " if(!SH2DMAHax) SeSsPerfBus(A, 0);   /* SE_PERF_BUS_READ */"),
    "bus-dma-write": (" SeSsPerfBus(A, 1);   /* SE_PERF_BUS_WRITE */", " if(!SH2DMAHax) SeSsPerfBus(A, 1);   /* SE_PERF_BUS_WRITE */"),
    "bus-all-reads": (" SeSsPerfBus(A, 0);   /* SE_PERF_BUS_READ */", " /* mutated away */"),
    "bus-all-writes": (" SeSsPerfBus(A, 1);   /* SE_PERF_BUS_WRITE */", " /* mutated away */"),
}


# The device-sync guard (apply.py, DEBUG_SYNC_EDIT / SeSyncCpuHook): (harness, file, patched text, mutated text).
SYNC_MUTATIONS = {
    "sync-guard-removed": ("f2_sh2", "src/ss/debug.inc", "if(DBG.FoundBPoint || SeSsHookNeedsSync || DBG.CPUHook != SeSsOwnCpuHook)", "if(1)"),
    "sync-never": ("f2_sh2", "src/ss/debug.inc", "if(DBG.FoundBPoint || SeSsHookNeedsSync || DBG.CPUHook != SeSsOwnCpuHook)", "if(0)"),
    "sync-no-breakpoint": ("f2_sh2", "src/ss/debug.inc", "if(DBG.FoundBPoint || SeSsHookNeedsSync || DBG.CPUHook != SeSsOwnCpuHook)", "if(SeSsHookNeedsSync || DBG.CPUHook != SeSsOwnCpuHook)"),
    "sync-flag-only": ("f2_sh2", "src/ss/debug.inc", "if(DBG.FoundBPoint || SeSsHookNeedsSync || DBG.CPUHook != SeSsOwnCpuHook)", "if(DBG.FoundBPoint || SeSsHookNeedsSync)"),
    "sync-flag-profile": ("f2_sh2", "src/ss/ss.cpp", "SeSsHookNeedsSync = (sSeTraceActive || sSeStepActive) ? 1 : 0;",
                          "SeSsHookNeedsSync = (sSeTraceActive || sSeStepActive || sSeProfActive) ? 1 : 0;"),
    # The native debugger's callback (apply.py, SeSsNativeSetCPUCallback). Each of these must fail the f2_sh2 harness.
    "native-table-upstream": ("f2_sh2", "src/ss/debug.inc", " SeSsNativeSetCPUCallback,   /* Saturn Explorer: the native debugger's callback request (ss.cpp) */",
                              " DBG_SetCPUCallback,"),
    "native-null-not-reapplied": ("f2_sh2", "src/ss/ss.cpp", "else if (wasHeld)\n      SeSyncCpuHook();", "else if (0)\n      SeSyncCpuHook();"),
    "native-not-installed": ("f2_sh2", "src/ss/ss.cpp", "DBG_SetCPUCallback(callb, continuous);\n      SeSyncCpuHook();", "SeSyncCpuHook();"),
    "native-stolen-by-edit": ("f2_sh2", "src/ss/ss.cpp", "if (sSeNativeCb != 0)\n      return;", "if (0)\n      return;"),
    "native-both-cpus": ("f2_sh2", "src/ss/ss.cpp", "SeSsBothCpus = on && !foreign;", "SeSsBothCpus = on;"),
    "native-not-noted": ("f2_sh2", "src/ss/ss.cpp", "   SeMednafenForeignHook(foreign);\n", ""),
    "native-in-handler": ("f2_sh2", "src/ss/ss.cpp", "if (SeSsInHandler > 0) { sSeSyncPending = 1; return; }", "if (0) { sSeSyncPending = 1; return; }"),
    "native-null-always-drops": ("f2_sh2", "src/ss/ss.cpp", "else if (wasHeld)\n      SeSyncCpuHook();", "else { DBG_SetCPUCallback(0, false); SeSyncCpuHook(); }"),
    "native-transient-lost": ("f2_sh2", "src/ss/ss.cpp", "if (sSeOursInstalled) { sSeNativeLost = 1; sSeOursInstalled = 0; }", "if (sSeOursInstalled) { sSeOursInstalled = 0; }"),
    "native-transient-undelivered": ("f2_sh2", "src/ss/ss.cpp", "if (on) SeMednafenForeignHook(1);", ""),
    "native-transient-unconditional": ("f2_sh2", "src/ss/ss.cpp", "if (on) SeMednafenForeignHook(1);", "SeMednafenForeignHook(1);"),
    "native-transient-kept": ("f2_sh2", "src/ss/ss.cpp", "      sSeNativeLost = 0;\n      if (on)", "      if (on)"),
}



# The hardware event log hooks: (file, key) for the text checks, from apply.py's own table.
def hwev_file_of(key):
    if key == "SE_HWEV_FWD":
        return None   # one per translation unit, checked separately
    if key == "SE_HWEV_CPU_PC":
        return "ss.cpp"
    if key.startswith("SE_HWEV_HOOK_EXC_"):
        return "sh7095_ops.inc"
    if key == "SE_HWEV_HOOK_DMAC":
        return "sh7095.inc"
    for prefix, f in (("SE_HWEV_HOOK_SCU_", "scu.inc"), ("SE_HWEV_HOOK_VBLANK", "scu.inc"), ("SE_HWEV_HOOK_CD_", "cdb.cpp"),
                      ("SE_HWEV_HOOK_SMPC_", "smpc.cpp"), ("SE_HWEV_HOOK_VDP1_", "vdp1.cpp"), ("SE_HWEV_HOOK_VDP2_", "vdp2.cpp")):
        if key.startswith(prefix):
            return f
    return None


def hwev_text_checks(work, apply_mod):
    """Every hardware-event fence is in the patched tree exactly once, in the file it belongs in, and (a hook) tests
    SeSsHwEvMask. This is the only check the sites no harness reaches (CD block, SMPC, VDP2 display) get at run time;
    for the others it backs the harness up when the harness cannot tell a dropped hook (a source with no events
    expected in the scenario)."""
    import re
    bad = 0
    texts = {}
    for key in apply_mod.HWEV_KEYS:
        if key == "SE_HWEV_FWD":
            continue
        f = hwev_file_of(key)
        if f not in texts:
            texts[f] = open(os.path.join(work, "src/ss", f), errors="replace").read()
        fences = [m.group(0) for m in re.finditer(re.escape(apply_mod.BEGIN) + r".*?" + re.escape(apply_mod.END), texts[f], re.S)
                  if key in m.group(0)]
        if len(fences) != 1:
            print("FAIL: %d fence(s) with %s in %s, expected 1" % (len(fences), key, f))
            bad += 1
        elif key.startswith("SE_HWEV_HOOK_") and "SeSsHwEvMask &" not in fences[0]:
            print("FAIL: the %s hook does not test SeSsHwEvMask" % key)
            bad += 1
    for f in ("ss.cpp", "cdb.cpp", "smpc.cpp", "vdp1.cpp", "vdp2.cpp"):
        t = open(os.path.join(work, "src/ss", f), errors="replace").read()
        n = t.count("/* SE_HWEV_FWD */")
        if n != 1:
            print("FAIL: %s has %d hardware-event declaration fence(s), expected 1" % (f, n))
            bad += 1
    return bad


# (harness, file, patched text, mutated text) lists. Each mutation must make EVERY harness it touches fail.
HWEV_MUTATIONS = {
    "hwev-mask-ignored": [
        ("hwev_vdp1", "src/ss/vdp1.cpp", "if(MDFN_UNLIKELY(SeSsHwEvMask & SE_HWEV_EN_VDP1)) SeMednafenHwEvent(SE_HWEV_K_VDP1_DRAW_START",
         "if(1) SeMednafenHwEvent(SE_HWEV_K_VDP1_DRAW_START"),
        ("hwev_sh2", "src/ss/sh7095_ops.inc", "if(MDFN_UNLIKELY(SeSsHwEvMask & SE_HWEV_EN_CPU_EXC)) SeMednafenHwEvent(SE_HWEV_K_SH2_EXCEPTION, (int)(this - CPU), PC, (unsigned)(VECNUM_ILLINSTR)",
         "if(1) SeMednafenHwEvent(SE_HWEV_K_SH2_EXCEPTION, (int)(this - CPU), PC, (unsigned)(VECNUM_ILLINSTR)"),
        ("hwev_sh2", "src/ss/sh7095.inc", "if(MDFN_UNLIKELY(SeSsHwEvMask & SE_HWEV_EN_SH2_DMAC)) SeMednafenDmacRecalc",
         "if(1) SeMednafenDmacRecalc"),
        ("hwev_sh2", "src/ss/scu.inc", "if(MDFN_UNLIKELY(SeSsHwEvMask & SE_HWEV_EN_SCU_DMA)) SeMednafenHwEvent(SE_HWEV_K_SCU_DMA_START",
         "if(1) SeMednafenHwEvent(SE_HWEV_K_SCU_DMA_START"),
    ],
    "hwev-wrong-kind": [
        ("hwev_vdp1", "src/ss/vdp1.cpp", "SeMednafenHwEvent(SE_HWEV_K_VDP1_FB_SWAP", "SeMednafenHwEvent(SE_HWEV_K_VDP1_FB_ERASE"),
        ("hwev_sh2", "src/ss/sh7095_ops.inc", "SeMednafenHwEvent(SE_HWEV_K_SH2_EXCEPTION, (int)(this - CPU), PC, (unsigned)(VECNUM_ILLINSTR), SE_HWEV_EXC_ILLINSTR",
         "SeMednafenHwEvent(SE_HWEV_K_SH2_EXCEPTION, (int)(this - CPU), PC, (unsigned)(VECNUM_ILLINSTR), SE_HWEV_EXC_NMI"),
        ("hwev_sh2", "src/ss/scu.inc", "SeMednafenHwEvent(SE_HWEV_K_SCU_DMA_END", "SeMednafenHwEvent(SE_HWEV_K_SCU_DMA_START"),
    ],
}

def unsupported_linker(cxx):
    """The harnesses link real emulator translation units whose references into the rest of Mednafen are left
    unresolved on purpose (-Wl,--unresolved-symbols=ignore-in-object-files, below). That GNU ld option exists for
    ELF only: the PE linkers (MinGW ld, MSVC/lld-link) and Apple's ld64 do not honour it, and there is no
    equivalent that works on them (--warn-unresolved-symbols is ELF-only too; the alternative is a stub for every
    missing symbol, which would hide link errors the harness is meant to expose). So detect such a toolchain up
    front. Returns a one-line reason, or None for an ELF target (the Linux behaviour, unchanged)."""
    try:
        machine = subprocess.run([cxx, "-dumpmachine"], capture_output=True, text=True, timeout=30).stdout.strip().lower()
    except (OSError, subprocess.SubprocessError):
        machine = ""
    if any(t in machine for t in ("mingw", "windows", "msys", "cygwin", "darwin", "apple")) or \
            (not machine and sys.platform in ("win32", "cygwin", "darwin")):
        return ("target %s is not ELF: its linker ignores --unresolved-symbols=ignore-in-object-files, "
                "so the component harnesses cannot link (Linux/ELF only)" % (machine or sys.platform))
    return None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    args = sys.argv[1:]
    drops, mutates, only = [], [], []
    skip_missing = "--skip-if-missing" in args
    if skip_missing:
        args.remove("--skip-if-missing")
    for flag, dest in (("--drop", drops), ("--mutate", mutates), ("--only", only)):
        while flag in args:
            i = args.index(flag)
            dest.append(args[i + 1])
            del args[i:i + 2]
    root, scratch = os.path.abspath(args[0]), os.path.abspath(args[1])
    if skip_missing and not os.path.isfile(os.path.join(root, "src", "ss", "sh7095.inc")):
        print("SKIP: no Mednafen checkout at %s" % root)
        return 77
    cxx = args[2] if len(args) > 2 else os.environ.get("CXX", "g++")
    reason = unsupported_linker(cxx)
    if reason:
        print("SKIP: " + reason)
        return 77
    os.makedirs(scratch, exist_ok=True)
    work = cm.make_scratch(root, scratch)
    rc, out = cm.run_apply(cm.NEW_APPLY, work)
    if rc != 0 or "ANCHOR MISS" in out:
        print("apply.py failed:\n" + out[-1000:])
        return 1
    if not drops and not mutates and not only:
        if f8_decoder_checks(work) or f11_checks(work) or f2_hook_checks(work):
            return 1
        print("decoder and hook-placement checks: ok")
        if hwev_text_checks(work, cm.load_new()):
            return 1
        print("hardware event hook checks: ok")
    apply_mod = cm.load_new()
    mutated_harnesses = set()
    hwev_expected = {}
    for key in mutates:
        if key in HWEV_MUTATIONS:
            for harness, rel, old, new in HWEV_MUTATIONS[key]:
                path = os.path.join(work, rel)
                text = open(path, newline="").read()
                if old not in text:
                    print("mutation %s: the hook text is not in the patched %s" % (key, rel))
                    return 1
                open(path, "w", newline="").write(text.replace(old, new, 1))
                hwev_expected.setdefault(harness, set()).add(key)
                mutated_harnesses.add(harness)
            print("mutation: %s" % key)
            continue
        if key in SYNC_MUTATIONS:
            harness, rel, old, new = SYNC_MUTATIONS[key]
        else:
            harness, rel = "f8_bus", "src/ss/sh7095.inc"
            old, new = BUS_MUTATIONS[key]
        mutated_harnesses.add(harness)
        path = os.path.join(work, rel)
        text = open(path, newline="").read()
        if old not in text:
            print("mutation %s: the hook text is not in the patched %s" % (key, rel))
            return 1
        open(path, "w", newline="").write(text.replace(old, new))
        print("mutation: %s" % key)
    for key in drops:
        for rel in ("src/ss/vdp1.cpp", "src/ss/sound.cpp", "src/hw_cpu/m68k/m68k.cpp", "src/ss/sh7095.inc", "src/ss/sh7095_ops.inc",
                    "src/ss/scu.inc", "src/ss/ss.cpp", "src/ss/cdb.cpp", "src/ss/smpc.cpp", "src/ss/vdp2.cpp"):
            path = os.path.join(work, rel)
            text = open(path, newline="").read()
            new, n = apply_mod.retire_fences_text(text, key)
            if n:
                open(path, "w", newline="").write(new)
                print("mutation: dropped %d block(s) containing %r from %s" % (n, key, rel))
    ss = os.path.join(work, "src", "ss")
    flags = ["-std=gnu++11", "-O1", "-g", "-fsigned-char", "-fwrapv", "-fexceptions", "-DHAVE_CONFIG_H"] + cm.include_flags(work) + [
             "-I" + ss, "-I" + os.path.join(HERE, "..", "Common"), "-w"]
    link = ["-Wl,--unresolved-symbols=ignore-in-object-files"]
    # The patched 68000 core, linked into the sound harness (the VDP1 one does not use it).
    m68k_o = os.path.join(scratch, "m68k.o")
    need_m68k = not mutates and (not only or "f5_m68k" in only)
    r = subprocess.run([cxx] + flags + ["-c", os.path.join(work, "src/hw_cpu/m68k/m68k.cpp"), "-o", m68k_o],
                       capture_output=True, text=True) if need_m68k else None
    if r is not None and r.returncode:
        print("m68k.cpp does not compile:\n" + r.stderr[-1500:])
        return 1
    bad = 0
    failed = set()
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
    # The hardware event harness links the glue WIRED (its clock and PC then come from the emulator's own accessors in
    # the patched ss.cpp) with the sink replaced by the harness's capture.
    hw_o = os.path.join(scratch, "se_mednafen_glue.hwev.o")
    r = subprocess.run(["gcc", "-std=gnu99", "-O1", "-g", "-w", "-DSE_MEDNAFEN_WIRED=1", "-DSE_MDFN_HWEV_SINK=SeHarnessHwSink",
                        "-I" + os.path.join(HERE, "..", "Common"), "-I" + HERE, "-c", os.path.join(HERE, "se_mednafen_glue.c"), "-o", hw_o],
                       capture_output=True, text=True)
    if r.returncode:
        print("the wired glue does not compile:\n" + r.stderr[-1500:])
        return 1
    hw_objs = [hw_o, glue_objs[1]]
    for name, define, src, extra in (
            ("f4_vdp1", "SE_VDP1_CPP", "perf_vdp1_harness.cpp", []),
            ("f5_m68k", "SE_SOUND_CPP", "perf_m68k_harness.cpp", [m68k_o]),
            ("f2_sh2", "SE_SS_CPP", "perf_sh2_harness.cpp", glue_objs + ["-lpthread"]),
            ("f8_bus", "SE_SS_CPP", "perf_bus_harness.cpp", glue_objs + ["-lpthread"]),
            ("hwev_vdp1", "SE_VDP1_CPP", "hwev_harness.cpp", []),
            ("hwev_sh2", "SE_SS_CPP", "hwev_harness.cpp", hw_objs + ["-lpthread"])):
        if only and name not in only:
            continue
        if mutates and name not in mutated_harnesses:
            continue
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
            failed.add(name)
            continue
        print("== " + name)
        r = subprocess.run([exe], capture_output=True, text=True)
        print(r.stdout, end="")
        if r.returncode:
            print(r.stderr[-500:])
            bad += 1
            failed.add(name)
    if drops and not mutates and not only:
        bad += hwev_text_checks(work, apply_mod)
    if drops or mutates:
        if hwev_expected:
            missed = [h for h in hwev_expected if h not in failed]
            if missed:
                print("mutation NOT DETECTED by: %s" % ", ".join(missed))
                return 1
        print("mutation %s" % ("detected" if bad else "NOT DETECTED"))
        return 0 if bad else 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
