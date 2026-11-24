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

print("OK" if not bad else "%d check(s) failed" % bad)
sys.exit(1 if bad else 0)
