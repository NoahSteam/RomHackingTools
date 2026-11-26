"""apply.py's profiler hooks that can be checked without a Mednafen source tree (the repository has none).

  * migration: a fence an OLDER apply.py wrote and this one retires (the v26 elapsed-VDP1 hook, the v26
    68000 slice hooks) is deleted by process_retired(), a current fence is left alone, and --revert still
    removes everything (byte-exact for files that were appended to);
  * the F4 debit macro: the injected re-definition of VDP1_EAT_CLOCKS, compiled around a copy of Mednafen's
    original macro, evaluates the debit expression exactly once and does not charge a resumed phase twice;
  * the F8 bus buckets: the injected address table, compiled, puts both ends of every decode range
    (Integration/Mednafen/bus_decode.py, written from the pinned SCU/CS0 decoders) in the expected counter;
  * the glue sets the valid bits for the counters the hooks produce.

The same hooks run against the real Mednafen files in Integration/Mednafen/check_migration.py and
check_component.py, which need the checkout. Usage: check_apply_hooks.py <apply.py> <scratch dir> [c++]"""
import importlib.util
import os
import re
import subprocess
import sys
import tempfile

apply_py, work = sys.argv[1], sys.argv[2]
cxx = sys.argv[3] if len(sys.argv) > 3 else "g++"
HERE = os.path.dirname(os.path.abspath(__file__))
MED = os.path.join(HERE, "..", "Mednafen")
COMMON = os.path.join(HERE, "..", "Common")
sys.path.insert(0, MED)
spec = importlib.util.spec_from_file_location("se_apply", apply_py)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
HAS_F5 = hasattr(mod, "M68K_IDLE_STEP")        # the 68000 idle hooks have landed
HAS_F8 = hasattr(mod, "SS_CACHE_MODE_HOOK")    # the bus buckets / cache mode have landed
if HAS_F8:
    import bus_decode  # noqa: E402

os.makedirs(work, exist_ok=True)
bad = 0


def fail(msg):
    global bad
    print("FAIL: " + msg)
    bad += 1


# ---- migration -----------------------------------------------------------------------------------
V26_VDP1 = mod.fence(" if(DrawingActive) SeSsPerfC[SE_PERF_VDP1_DRAW_CYCLES] += cycles;")
V26_SOUND_PRE = mod.fence("   const int32 se_perf_t0 = SoundCPU.timestamp;\n   const bool se_perf_idle = 0;")
V26_SOUND_POST = mod.fence("   SeSsPerfC[SE_PERF_M68K_TOTAL_CYCLES] += SoundCPU.timestamp - se_perf_t0;")
with tempfile.TemporaryDirectory(dir=work) as d:
    vdp1 = "int a;\n int32 cycles = timestamp - lastts;\n" + V26_VDP1 + "\n lastts = timestamp;\nint b;\n"
    sound = "x;\n" + V26_SOUND_PRE + "\n y;\n" + V26_SOUND_POST + "\n z;\n" + mod.fence(mod.SOUND_PERF_PRE_HOOK) + "\n"
    open(os.path.join(d, "vdp1.cpp"), "w").write(vdp1)
    open(os.path.join(d, "sound.cpp"), "w").write(sound)
    notes = mod.process_retired(d, True)
    v = open(os.path.join(d, "vdp1.cpp")).read()
    s = open(os.path.join(d, "sound.cpp")).read()
    if "SE_PERF_VDP1_DRAW_CYCLES" in v or v != "int a;\n int32 cycles = timestamp - lastts;\n lastts = timestamp;\nint b;\n":
        fail("the v26 elapsed-VDP1 fence was not removed cleanly:\n" + v)
    if HAS_F5 and ("se_perf_t0" in s or "x;\n y;\n z;\n" not in s):
        fail("the v26 sound fences were not removed cleanly:\n" + s)
    if HAS_F5 and mod.SOUND_PERF_PRE_HOOK.strip() not in s:
        fail("a CURRENT sound fence was removed by the retirement step (its key must not contain a retired one)")
    if not notes:
        fail("retirement reported nothing")
    if mod.process_retired(d, True):
        fail("retirement is not idempotent")
hooks = [mod.VDP1_EAT_HOOK, mod.VDP1_PERF_DMA_HOOK]
if HAS_F5:
    hooks += [mod.SOUND_PERF_PRE_HOOK, mod.SOUND_PERF_POST_HOOK, mod.M68K_IDLE_MEMBER, mod.M68K_IDLE_INIT, mod.M68K_IDLE_STEP]
if HAS_F8:
    hooks += [mod.SS_CACHE_MODE_HOOK]
for hook in hooks:
    for fname, key in mod.RETIRED_FENCES:
        if key in hook:
            fail("a current hook contains the retired key %r (the retirement step would delete it)" % key)
if "SE_PERF_VDP1_DRAW_CYCLES" in mod.VDP1_EAT_HOOK + mod.VDP1_PERF_DMA_HOOK or "SE_PERF_V26_VDP1_ELAPSED" in mod.VDP1_EAT_HOOK:
    fail("a hook increments the retired index 6")

# ---- appended blocks revert exactly -------------------------------------------------------------------
original = "int main() {}\n"
t, _ = mod.apply_append(original, "void f() {}\n", "f()")
strip = re.compile(re.escape(mod.BEGIN) + r".*?" + re.escape(mod.END) + r"\n?", re.DOTALL)
if strip.sub("", t) != original:
    fail("revert of an appended block leaves residue: %r" % strip.sub("", t))
t2, n = mod.apply_append(t, "void f() {}\n", "f()")
if t2 != t or "already" not in n:
    fail("appending twice is not a no-op")
old_format = original + "\n" + mod.fence("void f() {}") + "\n"          # an older apply.py: separator outside
t3, _ = mod.apply_append(old_format, "void f() {}\n", "f()")
if strip.sub("", t3) != original:
    fail("a block appended by an older apply.py does not revert exactly after migration: %r" % strip.sub("", t3))
t4, _ = mod.apply_append(original, "void g() {}\n", "g()")
t4, _ = mod.apply_append(t4, "void h() {}\n", "h()")
if strip.sub("", t4) != original:
    fail("two appended blocks do not revert exactly")

# ---- a fence that REPLACES upstream text reverts exactly (smpc.cpp's UpdateInput call) ---------------
smpc = "void SMPC_UpdateInput(const int32 time_elapsed)\n{\n for(unsigned vp = 0; vp < 12; vp++)\n {\n" \
       "  VirtualPorts[vp]->UpdateInput(VirtualPortsDPtr[vp], time_elapsed);\n }\n}\n"
strip_or_restore = re.compile(re.escape(mod.BEGIN) + r".*?" + re.escape(mod.END) + r"\n?", re.DOTALL)
KEY = "SeSMPCUpdateInput(vp, time_elapsed)"
r1, n = mod.apply_replace(smpc, mod.SMPC_UPDATE_ANCHOR, mod.SMPC_INPUT_UPDATE, KEY)
if "applied" not in n or "SeSMPCUpdateInput(vp, time_elapsed);" not in r1:
    fail("the smpc.cpp replacement did not apply: %s" % n)
if strip_or_restore.sub(mod.revert_fence, r1) != smpc:
    fail("--revert does not restore the replaced smpc.cpp line byte for byte")
r2, n = mod.apply_replace(r1, mod.SMPC_UPDATE_ANCHOR, mod.SMPC_INPUT_UPDATE, KEY)
if r2 != r1 or "already" not in n:
    fail("applying the smpc.cpp replacement twice is not a no-op")
# a block an older apply.py wrote (the replaced line is gone from the tree): recognised, the line is
# put back inside the new-style fence on re-apply, and put back in place by --revert
legacy = smpc.replace("  VirtualPorts[vp]->UpdateInput(VirtualPortsDPtr[vp], time_elapsed);",
                      mod.fence("  SeSMPCUpdateInput(vp, time_elapsed);"))
if strip_or_restore.sub(mod.revert_fence, legacy) != smpc:
    fail("--revert does not restore the line an older apply.py replaced in smpc.cpp")
r3, n = mod.apply_replace(legacy, mod.SMPC_UPDATE_ANCHOR, mod.SMPC_INPUT_UPDATE, KEY)
if r3 != r1 or "updated" not in n:
    fail("an older apply.py's smpc.cpp block is not migrated to the revertible form: %s" % n)
if strip_or_restore.sub(mod.revert_fence, r3) != smpc:
    fail("a migrated smpc.cpp block does not revert exactly")

if HAS_F5:
    # --revert reaches the shared 68000 core
    with tempfile.TemporaryDirectory(dir=work) as root:
        os.makedirs(os.path.join(root, "src", "ss"))
        m = os.path.join(root, mod.M68K_DIR)
        os.makedirs(m)
        h = "class M68K {\n uint32 XPending;\n};\n"
        open(os.path.join(m, "m68k.h"), "w").write(h)
        patched, _ = mod.apply_anchored(h, mod.M68K_IDLE_MEMBER_ANCHOR, mod.M68K_IDLE_MEMBER, "IdleCycleCount;")
        if "IdleCycleCount" not in patched:
            fail("the m68k.h member hook did not apply to a class body")
        open(os.path.join(m, "m68k.h"), "w").write(patched)
        mod.revert(os.path.join(root, "src", "ss"), root)
        if open(os.path.join(m, "m68k.h")).read() != h:
            fail("--revert does not restore m68k.h")


# ---- F4: the debit macro -------------------------------------------------------------------------------
ORIGINAL_MACRO = """\
enum : int { CommandPhaseBias = __COUNTER__ + 1 };
#define VDP1_EAT_CLOCKS(n)\t\t\t\t\t\t\t\t\t\\
\t\t{\t\t\t\t\t\t\t\t\t\t\\
\t\t CycleCounter -= (n);\t\t\t\t\t\t\t\t\\
\t\t case __COUNTER__:\t\t\t\t\t\t\t\t\\
\t\t if(CycleCounter <= 0)\t\t\t\t\t\t\t\t\\
\t\t {\t\t\t\t\t\t\t\t\t\t\\
\t\t  CommandPhase = __COUNTER__ - CommandPhaseBias - 1;\t\t\t\t\\
\t\t  goto Breakout;\t\t\t\t\t\t\t\t\\
\t\t }\t\t\t\t\t\t\t\t\t\t\\
\t\t}\t\t\t\t\t\t\t\t\t\t\\

"""
text, note = mod.apply_anchored(ORIGINAL_MACRO, mod.VDP1_EAT_ANCHOR, mod.VDP1_EAT_HOOK, "SeSsVdp1Debit")
if "applied" not in note:
    fail("the debit-macro anchor does not match the shape of Mednafen's macro: " + note)
F4 = r'''
#include <cstdio>
typedef int int32;
#define INLINE inline
#include "SePerfCounters.h"
extern "C" unsigned long long SeSsPerfC[SE_PERF_COUNT];
unsigned long long SeSsPerfC[SE_PERF_COUNT];
static int32 CycleCounter, CommandPhase;
static int evals;
static int32 cost(int32 n) { ++evals; return n; }
''' + text + r'''
// One resumable command loop shaped like DoDrawing's: a fetch, a command whose cost is a call, a resume phase.
static bool Run()
{
 switch(CommandPhase + CommandPhaseBias)
 {
  for(;;)
  {
   default:
   VDP1_EAT_CLOCKS(0);
   VDP1_EAT_CLOCKS(cost(16));
   VDP1_EAT_CLOCKS(cost(300));
   VDP1_EAT_CLOCKS(cost(8));
   return true;
  }
 }
 Breakout:;
 return false;
}
int main()
{
 int bad = 0;
 CycleCounter = 100;
 if(Run()) { std::puts("FAIL: the first call should yield"); ++bad; }
 if(SeSsPerfC[SE_PERF_VDP1_WORK_CYCLES] != 316) { std::printf("FAIL: work after the yield %llu, want 316\n", SeSsPerfC[SE_PERF_VDP1_WORK_CYCLES]); ++bad; }
 if(evals != 2) { std::printf("FAIL: debit expressions evaluated %d times, want 2\n", evals); ++bad; }
 CycleCounter += 300;   // -216 -> 84: the yielded phase's check passes on resume
 if(!Run()) { std::puts("FAIL: the resumed call should finish"); ++bad; }
 if(SeSsPerfC[SE_PERF_VDP1_WORK_CYCLES] != 324) { std::printf("FAIL: work after the resume %llu, want 324 (the 300 is not charged again)\n", SeSsPerfC[SE_PERF_VDP1_WORK_CYCLES]); ++bad; }
 if(evals != 3) { std::printf("FAIL: debit expressions evaluated %d times in all, want 3\n", evals); ++bad; }
 return bad;
}
'''
src = os.path.join(work, "f4_macro.cpp")
open(src, "w").write(F4)
exe = os.path.join(work, "f4_macro")
cc = subprocess.run([cxx, "-std=c++14", "-I", COMMON, src, "-o", exe], capture_output=True, text=True)
if cc.returncode:
    fail("the F4 macro harness does not compile:\n" + cc.stderr[-1500:])
else:
    r = subprocess.run([exe], capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode:
        bad += 1

if HAS_F8:
    # ---- F8: the bus table ----------------------------------------------------------------------------------
    checks = []
    for lo, hi, bucket in bus_decode.BUS_RANGES:
        base = "SE_PERF_BUS_ACCESS_X + %s * 2" % bucket[2:] if bucket.startswith("X:") else "SE_PERF_BUS_ACCESS + %s * 2" % bucket
        for a in sorted({lo, hi, (lo + hi) // 2 & ~3}):
            for w in (0, 1):
                checks.append("Check(0x%08xu, %d, %s + %d);" % (a, w, base, w))
    F8 = '#include "SePerfCounters.h"\n#include <cstdio>\nextern "C" { unsigned long long SeSsPerfC[SE_PERF_COUNT]; unsigned int SeSsCacheMode; }\n' \
         + mod.PERF_BUS_HELPER + r'''
    static int bad;
    static void Check(unsigned a, int w, int idx)
    {
     for(unsigned i = 0; i < SE_PERF_COUNT; ++i) SeSsPerfC[i] = 0;
     SeSsPerfBus(a, w);
     unsigned n = 0; int where = -1;
     for(unsigned i = 0; i < SE_PERF_COUNT; ++i) if(SeSsPerfC[i]) { ++n; where = (int)i; }
     if(n != 1 || where != idx) { std::printf("FAIL: 0x%08x %s landed in counter %d (%u hit), want %d\n", a, w ? "write" : "read", where, n, idx); ++bad; }
    }
    int main() {
    ''' + "\n".join(checks) + "\n return bad; }\n"
    src = os.path.join(work, "f8_bus.cpp")
    open(src, "w").write(F8)
    exe = os.path.join(work, "f8_bus")
    cc = subprocess.run([cxx, "-std=c++14", "-I", COMMON, src, "-o", exe], capture_output=True, text=True)
    if cc.returncode:
        fail("the F8 bus-table harness does not compile:\n" + cc.stderr[-1500:])
    else:
        r = subprocess.run([exe], capture_output=True, text=True)
        sys.stdout.write(r.stdout)
        if r.returncode:
            bad += 1
    # the ranges tile the 27-bit space with no gap or overlap
    prev = -1
    for lo, hi, _ in bus_decode.BUS_RANGES:
        if lo != prev + 1:
            fail("BUS_RANGES has a gap or overlap at 0x%08x" % lo)
        prev = hi
    if prev != 0x07FFFFFF:
        fail("BUS_RANGES does not end at 0x07FFFFFF")


# ---- the glue sets the bits for what the hooks produce -----------------------------------------------------
glue = open(os.path.join(MED, "se_mednafen_glue.c")).read()
bits = ["SE_PERF_VALID_VDP1_WORK"] + (["SE_PERF_VALID_M68K_IDLE"] if HAS_F5 else []) + \
       (["SE_PERF_VALID_BUS_X", "SE_PERF_VALID_CACHE"] if HAS_F8 else [])
for bit in bits:
    if not re.search(r"info\.valid\s*\|=[^;]*\b" + bit + r"\b", glue):
        fail("the glue never sets " + bit)
if HAS_F8 and not re.search(r"info\.cacheMode\s*=\s*SeSsCacheMode", glue):
    fail("the glue does not publish the cache mode")

# ---- F2 + F6 + F9 -----------------------------------------------------------------------------------------
HAS_LIFECYCLE = hasattr(mod, "FRAME_CLOSE_HOOK")
if HAS_LIFECYCLE:
    for bit in ("SE_PERF_VALID_SSH2_INT", "SE_PERF_VALID_MODE"):
        if not re.search(r"info\.valid\s*\|=[^;]*\b" + bit + r"\b", glue):
            fail("the glue never sets " + bit)
    if not re.search(r"info\.mode\s*=\s*gap\s*\?\s*\(sFrameMode\s*&\s*~SE_LIVE_PROF_FUNCTIONS\)\s*:\s*sFrameMode", glue):
        fail("the glue does not publish the latched mode (or a frame with a gap as a non-attributed one)")
    if "SE_PERF_RESERVED_54" in glue or any("SE_PERF_RESERVED_54" in h for h in (mod.END_FRAME_HOOK, mod.FRAME_CLOSE_HOOK)):
        fail("index 54 is reserved and must never be produced")
    # the old end-of-frame fence (3269140 and before) counted the slave all-or-nothing after the rebase; the
    # retirement step must remove exactly that fence, and the current end-of-frame fence must not be touched
    old_end = mod.fence(" /* Saturn Explorer profiler: this frame's length, and whether the slave SH-2 was on for it */\n"
                        " SeSsPerfC[SE_PERF_FRAME_CYCLES] += end_ts;\n"
                        " if(CPU[1].timestamp != SS_EVENT_DISABLED_TS) SeSsPerfC[SE_PERF_SSH2_ON_CYCLES] += end_ts;\n"
                        " SeMednafenEndFrameHook();\n")
    with tempfile.TemporaryDirectory(dir=work) as d:
        open(os.path.join(d, "ss.cpp"), "w").write("a;\n" + old_end + "\nb;\n" + mod.fence(mod.END_FRAME_HOOK) + "\nc;\n")
        mod.process_retired(d, True)
        t = open(os.path.join(d, "ss.cpp")).read()
        if "SE_PERF_SSH2_ON_CYCLES" in t or t.count("SeMednafenEndFrameHook();") != 1:
            fail("the all-or-nothing end-of-frame fence was not retired cleanly:\n" + t)
    for hook in (mod.END_FRAME_HOOK, mod.FRAME_CLOSE_HOOK, mod.STATE_LOADED_HOOK, mod.SH2_SETACTIVE_ON_HOOK,
                 mod.SH2_SETACTIVE_OFF_HOOK, mod.SH2_RESET_HOOK):
        for fname, key in mod.RETIRED_FENCES:
            if key in hook:
                fail("a current F2 hook contains the retired key %r" % key)
    # keys must not collide with another fence's text (a key found in the wrong fence is "updated" forever)
    keys = ["SE_FRAME_CLOSE_HOOK", "SE_SLAVE_ON_EDGE", "SE_SLAVE_OFF_EDGE", "SE_CPU_RESET_HOOK"] + \
           ["SE_OWN_EXC_" + k for k in mod.SH2_EXCEPTIONS]
    fence_texts = [mod.FWD_DECLS, mod.END_FRAME_HOOK, mod.FRAME_CLOSE_HOOK, mod.STATE_LOADED_HOOK, mod.SH2_SETACTIVE_ON_HOOK,
                   mod.SH2_SETACTIVE_OFF_HOOK, mod.SH2_RESET_HOOK] + [mod.sh2_exception_edit(k)[1] for k in mod.SH2_EXCEPTIONS]
    for key in keys:
        if sum(key in t for t in fence_texts) != 1:
            fail("key %r is not unique to its own fence" % key)
    # every hook the glue defines is declared where the injected code calls it
    for fn in ("SeMednafenSlaveActive", "SeMednafenCpuReset", "SeMednafenExceptionEntry", "SeMednafenStateLoaded",
               "SeMednafenFrameClose"):
        if "void " + fn + "(" not in mod.FWD_DECLS:
            fail(fn + " is not forward-declared for the injected code")
        if not re.search(r"^void " + fn + r"\(", glue, re.M):
            fail(fn + " is not defined by the glue")
    # the SetActive hooks are guarded to the slave; TRAPA is not hooked (its flow is observed); each anchor matches one place
    if "CPU[1]" not in mod.SH2_SETACTIVE_ON_HOOK + mod.SH2_SETACTIVE_OFF_HOOK:
        fail("the SetActive hooks are not restricted to the slave")
    if "TRAP" in "".join(mod.SH2_EXCEPTIONS):
        fail("TRAPA must not withdraw ownership: its flow is observed")
    sample = "void SH7095::SetActive(bool active)\n{\n if(active && timestamp == SS_EVENT_DISABLED_TS)\n {\n  timestamp = SH7095_mem_timestamp;\n  FRT.lastts = timestamp;\n }\n else if(!active && timestamp != SS_EVENT_DISABLED_TS)\n {\n  Reset(true);\n  timestamp = SS_EVENT_DISABLED_TS;\n }\n}\n"
    t, _ = mod.apply_anchored(sample, mod.SH2_SETACTIVE_ON_ANCHOR, mod.SH2_SETACTIVE_ON_HOOK, "SE_SLAVE_ON_EDGE")
    t, _ = mod.apply_anchored(t, mod.SH2_SETACTIVE_OFF_ANCHOR, mod.SH2_SETACTIVE_OFF_HOOK, "SE_SLAVE_OFF_EDGE")
    on_at, off_at = t.index("SE_SLAVE_ON_EDGE"), t.index("SE_SLAVE_OFF_EDGE")
    if not (t.index("timestamp = SH7095_mem_timestamp;") < on_at < t.index("FRT.lastts")) or not \
            (t.index("else if(!active") < off_at < t.index("Reset(true);")):
        fail("the SetActive hooks are not on the right sides of the power transition:\n" + t)

# ---- the device-sync guard in DBG_CPUHandler (profile-only instructions skip ForceEventUpdates) -----------------
if hasattr(mod, "DEBUG_SYNC_EDIT"):
    mock = ("template<unsigned which>\nstatic MDFN_COLD void DBG_CPUHandler(void)\n{\n const uint32 PC = 0;\n"
            " DBG.CPUHookContinuous |= DBG.FoundBPoint;\n\n if(DBG.CPUHookContinuous && DBG.CPUHook)\n {\n"
            "  DBG.inss = which;\n  ForceEventUpdates(DBG.eff_ts);\n  DBG.CPUHook(PC, DBG.FoundBPoint);\n"
            "  DBG.inss = false;\n }\n}\n"
            "\nstatic DebuggerInfoStruct DBGInfo\n{\n false,\n\n DBG_FlushBreakPoints,\n DBG_AddBreakPoint,\n"
            " DBG_SetCPUCallback,\n\n DBG_EnableBranchTrace,\n};\n")
    with tempfile.TemporaryDirectory(dir=work) as d:
        path = os.path.join(d, mod.DEBUG_FILE)
        open(path, "w", newline="").write(mock)
        notes = "\n".join(mod.process_debug(d, True))
        t = open(path, newline="").read()
        if "ANCHOR MISS" in notes:
            fail("process_debug missed an anchor on the mock handler:\n" + notes)
        # the sync is conditional on exactly: a breakpoint found now, or the flag the glue maintains
        if "if(DBG.FoundBPoint || SeSsHookNeedsSync || DBG.CPUHook != SeSsOwnCpuHook)\n   ForceEventUpdates(DBG.eff_ts);" not in t:
            fail("the guarded ForceEventUpdates is not in the patched handler:\n" + t)
        if t.count("ForceEventUpdates(DBG.eff_ts);") != 2 or "#if 0 /* SE_ORIGINAL */\n  ForceEventUpdates(DBG.eff_ts);" not in t:
            fail("the upstream ForceEventUpdates line is not kept inside its fence")
        if "extern \"C\" int SeSsHookNeedsSync;" not in t:
            fail("SeSsHookNeedsSync is not declared before the handler")
        if t.index("SeSsHookNeedsSync;") > t.index("MDFN_COLD void DBG_CPUHandler"):
            fail("SeSsHookNeedsSync is declared after the handler uses it")
        # the native debugger's callback request goes through the SE wrapper; the upstream entry stays under the fence
        if "\n SeSsNativeSetCPUCallback,   /*" not in t or "#if 0 /* SE_ORIGINAL */\n DBG_SetCPUCallback,\n#endif" not in t:
            fail("the DebuggerInfo.SetCPUCallback entry is not routed through SeSsNativeSetCPUCallback:\n" + t)
        if t.count("DBG_SetCPUCallback,") != 1:
            fail("the upstream table entry must survive exactly once, inside its fence")
        if "extern \"C\" void SeSsNativeSetCPUCallback(" not in t or t.index("SeSsNativeSetCPUCallback(") > t.index("DBGInfo"):
            fail("SeSsNativeSetCPUCallback is not declared before the table that names it")
        mod.process_debug(d, True)
        if open(path, newline="").read() != t:
            fail("applying the debug.inc patch twice changed the file")
        # a tree patched by the previous apply.py (no native-callback fence, no extern in the helper) is upgraded in place
        old = t
        fm = [m for m in mod.FENCE_RE.finditer(old) if "SeSsNativeSetCPUCallback," in m.group(0) and "SE_ORIGINAL" in m.group(0)]
        if len(fm) != 1:
            fail("expected exactly one native-callback fence")
        else:
            old = old[:fm[0].start()] + mod.fence_original(fm[0].group(0)).rstrip("\n") + old[fm[0].end():]
            old = old.replace("/* The native debugger's SetCPUCallback request (ss.cpp, SeSsNativeSetCPUCallback): the DebuggerInfo table entry. */\n"
                              "extern \"C\" void SeSsNativeSetCPUCallback(void (*callb)(uint32 PC, bool bpoint), bool continuous);\n", "")
            if old == t or "SeSsNativeSetCPUCallback" in old:
                fail("could not build the previous-version tree for the upgrade check")
            open(path, "w", newline="").write(old)
            mod.process_debug(d, True)
            if open(path, newline="").read() != t:
                fail("a tree patched by the previous apply.py was not upgraded to the current text")
        mod.revert(d, d)
        if open(path, newline="").read() != mock:
            fail("--revert did not restore debug.inc byte for byte")
    # the flag is defined and maintained next to the callback it describes, and profiling alone leaves it clear
    if "int SeSsHookNeedsSync = 1;" not in open(apply_py).read():
        fail("SeSsHookNeedsSync is never defined")
    if "SeSsHookNeedsSync = (sSeTraceActive || sSeStepActive) ? 1 : 0;" not in open(apply_py).read():
        fail("SeSyncCpuHook does not maintain SeSsHookNeedsSync from the trace and step state alone")
    src = open(apply_py).read()
    # the wrapper: what the debugger asks for is installed as upstream; NULL re-applies our mode; an SE edit leaves a held callback alone
    for need, why in (("extern \"C\" void SeSsNativeSetCPUCallback(void (*callb)(uint32 PC, bool bpoint), bool continuous) {", "defined with C linkage"),
                      ("DBG_SetCPUCallback(callb, continuous);\n      SeSyncCpuHook();", "installs a non-NULL request as upstream does"),
                      ("else if (wasHeld)\n      SeSyncCpuHook();", "re-applies our mode when the debugger releases the callback"),
                      ("if (sSeNativeCb != 0)\n      return;", "SeSyncCpuHook leaves a callback the debugger holds alone"),
                      ("SeSsBothCpus = on && !foreign;", "the both-CPU handler is off while the debugger holds the callback"),
                      ("SeMednafenForeignHook(foreign);", "the glue is told when ours is not the installed callback"),
                      ("if (sSeOursInstalled) { sSeNativeLost = 1; sSeOursInstalled = 0; }", "a takeover from our installed callback is recorded when it happens"),
                      ("sSeNativeLost = 0;\n      if (on) SeMednafenForeignHook(1);", "the recorded takeover is delivered by the next sync that runs, only when we want a callback")):
        if need not in src:
            fail("the native-callback wrapper does not hold: " + why)
    if "void SeMednafenForeignHook(int foreign);" not in mod.FWD_DECLS or not re.search(r"^void SeMednafenForeignHook\(", glue, re.M):
        fail("SeMednafenForeignHook is not forward-declared and defined")
    if open(apply_py).read().count("int SeSsHookNeedsSync = 1;") != 2:
        fail("SeSsHookNeedsSync needs a definition in both the debugger and the non-debugger branch")

# ---- hardware event log hooks (apply.HWEV_*): each key present exactly once and guarded by the mask --------------
if hasattr(mod, "HWEV_KEYS"):
    keys = list(mod.HWEV_KEYS)
    if len(set(keys)) != len(keys):
        fail("a hardware event key is used twice")
    for a in keys:
        for b in keys:
            if a != b and a in b:
                fail("hardware event key %r is a substring of %r: upsert() would conflate their fences" % (a, b))
    sites = ([(h, k) for _a, h, k in mod.HWEV_SCU + mod.HWEV_CD + mod.HWEV_SMPC + mod.HWEV_VDP1 + mod.HWEV_VDP2]
             + [(mod.HWEV_DMAC_HOOK, mod.HWEV_DMAC_KEY)]
             + [(h, k) for _a, h, k in [mod.hwev_exception_edit(*e) for e in mod.HWEV_EXC]])
    if len(sites) + 2 != len(keys):     # + the declarations and the SsDbgCpuPc accessor
        fail("HWEV_KEYS (%d) does not list every hook (%d + 2)" % (len(keys), len(sites)))
    for hook, key in sites:
        if hook.count(key) != 1:
            fail("hook %s: its key appears %d times in its own text" % (key, hook.count(key)))
        for other in keys:
            if other != key and other in hook:
                fail("hook %s contains the key of another hook (%s)" % (key, other))
        m = re.search(r"if\(MDFN_UNLIKELY\(SeSsHwEvMask & SE_HWEV_EN_[A-Z0-9_]+\)", hook)
        if not m or hook.lstrip().find("if(") != 0:
            fail("hook %s is not guarded by a SeSsHwEvMask test at the start of its statement" % key)
        # a coroutine body (cdb / smpc / vdp1 command loop) cannot take a declaration: the hook is one statement
        if re.search(r"\b(const|int|unsigned|bool|auto|uint\d+)\s+\w+\s*=", hook.split("SeMednafenHwEvent(")[0]):
            fail("hook %s declares a local" % key)
        # the mask is tested before anything that costs: no call sits ahead of it on the line
        if "(" in hook[:hook.index("SeSsHwEvMask")].replace("if(MDFN_UNLIKELY(", ""):
            fail("hook %s evaluates something before the mask" % key)
        for retired_file, retired_key in mod.RETIRED_FENCES:
            if retired_key in hook:
                fail("hook %s contains the retired key %r" % (key, retired_key))
    # an exception hook lands AFTER the profiler's ownership fence (f2_hook_checks pins that one to the statement),
    # an old tree gets it in place, a second apply changes nothing, and --revert is exact
    exc_src = ("void f() {\n\tPC -= 4;\n\tException(EXCEPTION_INT, opexec_vecnum, opexec_new_PC);\n\tSR &= 1;\n"
               "\tException(EXCEPTION_TRAP, (uint8)instr, opexec_new_PC)\n\tBranch(false, opexec_new_PC);\n}\n")
    with tempfile.TemporaryDirectory(dir=work) as d:
        path = os.path.join(d, "sh7095_ops.inc")
        open(path, "w", newline="").write(exc_src)
        mod.process_anchored(d, "sh7095_ops.inc", "t", [mod.sh2_exception_edit("INT")], True)
        with_own = open(path, newline="").read()
        edits = [mod.hwev_exception_edit(*e) for e in mod.HWEV_EXC]
        mod.process_anchored(d, "sh7095_ops.inc", "t", edits, True)
        t = open(path, newline="").read()
        if not (t.index("SE_OWN_EXC_INT") < t.index("SE_HWEV_HOOK_EXC_INT") < t.index("SR &= 1;")):
            fail("the INT event hook is not directly after the ownership fence:\n" + t)
        if not (t.index("Exception(EXCEPTION_TRAP") < t.index("SE_HWEV_HOOK_EXC_TRAP") < t.index("Branch(false")):
            fail("the TRAPA event hook is not between the Exception statement and the branch:\n" + t)
        for k in ("SE_HWEV_HOOK_EXC_INT", "SE_HWEV_HOOK_EXC_TRAP"):
            if t.count(k) != 1:
                fail("%s appears %d times after one apply" % (k, t.count(k)))
        mod.process_anchored(d, "sh7095_ops.inc", "t", edits, True)
        if open(path, newline="").read() != t:
            fail("a second apply of the exception event hooks changed the file")
        # applied BEFORE the profiler's fence exists (a tree an older apply.py patched, then this one): same result
        open(path, "w", newline="").write(exc_src)
        mod.process_anchored(d, "sh7095_ops.inc", "t", edits, True)
        mod.process_anchored(d, "sh7095_ops.inc", "t", [mod.sh2_exception_edit("INT")], True)
        if open(path, newline="").read().count("SE_HWEV_HOOK_EXC_INT") != 1:
            fail("the INT event hook is duplicated when the ownership fence arrives later")
        mod.revert(d, d)
        if open(path, newline="").read() != exc_src:
            fail("--revert does not restore the exception sites byte for byte")
    # a prepended declaration fence reverts exactly and is applied once
    with tempfile.TemporaryDirectory(dir=work) as d:
        path = os.path.join(d, "vdp2.cpp")
        src = "#include \"vdp2.h\"\nint x;\n"
        open(path, "w", newline="").write(src)
        for _ in range(2):
            mod.process_anchored(d, "vdp2.cpp", "t", [], True, prepend=(mod.hwev_fwd(mod.HWEV_DECL), mod.HWEV_FWD_KEY))
        t = open(path, newline="").read()
        if t.count("SE_HWEV_FWD") != 1 or "SeSsHwEvMask" not in t or "SeMednafenHwEvent" not in t:
            fail("the declaration fence is not applied exactly once:\n" + t)
        mod.revert(d, d)
        if open(path, newline="").read() != src:
            fail("--revert does not restore a file with a declaration fence")
    # the declarations compile in C++ and agree with the glue's definitions
    with tempfile.TemporaryDirectory(dir=work) as d:
        tu = os.path.join(d, "decl.cpp")
        open(tu, "w").write(mod.HWEV_DECL_SS + "void f(){ if(SeSsHwEvMask & SE_HWEV_EN_VDP1) SeMednafenHwEvent(SE_HWEV_K_VDP1_FB_SWAP, SE_HWEV_CPU_NONE, 0, 1u, 2u, 3u, 4u);\n"
                                                "  SeMednafenDmacRecalc(0, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u); }\n")
        r = subprocess.run([cxx, "-std=gnu++11", "-fsyntax-only", "-I" + COMMON, tu], capture_output=True, text=True)
        if r.returncode:
            fail("the hardware event declarations do not compile:\n" + r.stderr[-800:])
        glue = open(os.path.join(MED, "se_mednafen_glue.c")).read()
        for sym in ("unsigned int SeSsHwEvMask;", "void SeMednafenHwEvent(unsigned int kind, int cpu, unsigned int pc,",
                    "void SeMednafenDmacRecalc(int cpu, unsigned int runMask,"):
            if sym not in glue:
                fail("the glue does not define " + sym)

print("OK" if not bad else "%d check(s) failed" % bad)
sys.exit(1 if bad else 0)
