// Fault injection against the decompiler as a matrix (REVIEW_PROGRAM.md, area 6: worker lifecycle,
// cancellation and resource exhaustion). Three families, every cell run in a child process so that a
// crash, a hang, a sanitizer report or an exit through static destruction is a finding and not the
// end of the test:
//
//   alloc     allocation k of one Decompile fails, for every k (all of them in a dense prefix, then a
//             geometric stride, with every phase boundary and the run's tail covered densely). The
//             result must be Failed("out of memory") -- or Ok if the failure was swallowed -- no
//             exception may escape, the next request must be Unavailable when the failure was inside
//             the engine and Ok when it was before it, the snapshot's reference count must come back,
//             and the process must leave through _Exit with no sanitizer report and no leak.
//   cancel    the cancel flag goes up at every phase entry of the run (the phase hook), at every
//             allocation (a deterministic stand-in for "every thousand ops": the allocator raises the
//             flag), and from a second thread every millisecond across the run. The outcome must be
//             the one the design gives for where the flag went up (see Expected; a flag from another thread is read against a
//             counter that runs a check-point behind, ExpectedThread, and the cancel-enter cells reach that window
//             deterministically: A6-35), and cancel-bound raises it exactly at the host's own check-points, in the hook the
//             host calls just before each (and at the last allocation before it), so that the check there, which the
//             engine's check-points cannot stand in for, is pinned by a cell of its own: A6-41), a cancelled result
//             carries no lines, and the same Sh2Decompiler then produces exactly the uncancelled output.
//   disasm    allocation k of a Disassemble fails: it never throws, a listing cut short says so (the `stopped` flag
//             it sets needs no allocation; the marker line is a move into room made first), the engine is then
//             refused (A6-16, A6-23); a failure making the marker or its room, before the engine is touched, stops
//             the listing before it began and leaves the engine undamaged (A6-30)
//   cold      allocation k of the FIRST request of a process fails (a Decompile; cold-disasm: a Disassemble), in a
//             fork of a parent that never ran the engine, with the spec bundle already extracted and with an empty
//             config directory: the spec materialisation, the library's own start-up, the first translator. Before
//             any engine global was written the failure is transient (the next request is correct); inside
//             startDecompilerLibrary the engine is refused for good, with the same restart reason from the first
//             request on, for Decompile and Disassemble alike (A6-30).
//   sticky    "disasm-sticky", "alloc-sticky", "cold-sticky" and "cold-disasm-sticky": the same cells with every allocation
//             from k on failing, not only k, as a full heap stays full (A6-23; SE_FAULT_STICKY=1 does it to any family,
//             the runner's allocation-failure children included: A6-36)
//   pin-selftest  pins written wrong, and the verdict each gets: a MOVED pin fails the run (A6-34)
//   runner    DecompilerRunner lifecycle: Stop (Cancel), Submit while busy (superseding),
//             InvalidateSession, budget expiry, DisableLocked recovery and destruction, each with the
//             worker held at every phase entry of the request. No result for a stale generation or
//             epoch, the worker joins, the too-large memo is keyed as designed, and after recovery
//             the next request is correct.
//
// A "cell" of the first two families is a fork() of a driver process that has already warmed the
// engine (building the SLEIGH translator takes most of a run), so a few thousand cells cost minutes.
// The driver is itself a child of this binary (the --engine-oom pattern of Sh2DecompilerTests): the
// parent spawns itself with arguments and reads the exit status and the output.
//
// usage: SaturnExplorerDecompilerFaultTests <scratch dir>
//          the default subset (deterministic, under a minute); SE_FAULT_FULL=1 runs the whole matrix
//        SaturnExplorerDecompilerFaultTests <scratch dir> --cell <family> <fixture|kind> <k>
//          one cell, with the failing allocation's stack trace; family is alloc, cancel-enter,
//          cancel-alloc, cancel-bound (<k> 2j: the hook before the host's check-point j; 2j+1: the allocation before it), cancel-us, cancel-oom, disasm, cold, cold-disasm (<fixture> is mailbox; <k> is allocation * 2, plus 1 for an empty config directory) or runner (then <kind> is stop, supersede, invalidate,
//          destroy, budget, disable or memo and <k> a phase-entry ordinal, or "all")
//        SaturnExplorerDecompilerFaultTests <scratch dir> --sites <fixture> <phase> <ord> <size> [site]
//          the allocations of that size in that phase entry, each with its rank and the functions on its stack:
//          how a pin's coordinates and site are written (SE_FAULT_DEPTH=<n> frames, 5). With a site, only the
//          allocations made there, each with its ordinal among them (the '#n' of the site)
//        SaturnExplorerDecompilerFaultTests <scratch dir> --refused-retry
//          the refused-retry cell by hand (A6-40; it is a job of the default subset and of the full run as "refused-retry"):
//          a request refused because the configuration directory cannot be made is answered again, and correctly, once the
//          cause is gone
//        SaturnExplorerDecompilerFaultTests <scratch dir> --diffruns <fixture> [runs]
//          runs of the warmed engine after the baseline, each compared with it, allocation by allocation (the
//          first must match; the markup's pair ids make a later one differ when they pass a varint boundary)
//        environment: SE_FAULT_FULL, SE_FAULT_DENSE (alloc: size of the dense prefix, 3000),
//          SE_FAULT_JOBS (parallel drivers, 1), SE_FAULT_TIMEOUT (seconds per cell, 120),
//          SE_FAULT_PHASE=actions,... / SE_FAULT_RANGE=<lo>-<hi> (alloc: every k of those phase entries, or of the range),
//          SE_FAULT_US_STEP=<us> (cancel-us: the step between raises),
//          SE_FAULT_KEEP=1 (keep the drivers' logs in <scratch>/logs), SE_FAULT_ONLY=alloc,cancel-enter,...
//          (only the jobs whose label starts with one of these), SE_FAULT_EXIT=static (a damaged engine
//          leaves through SleighArchitecture::shutdown() and exit() instead of _Exit), SE_FAULT_LEAKS=0 (no
//          per-cell leak check), SE_FAULT_LEAKLIST=1 (a sanitizer build prints every leak's innermost frames
//          as LEAK-SIG and fails none: how the accepted-leak table is written)
#include "DecompilerTestSupport.h"

#if defined(__unix__) || defined(__APPLE__)
#define SE_FAULT_POSIX 1
#endif

#ifndef SE_FAULT_POSIX

int main()
{
    // 77 is ctest's SKIP_RETURN_CODE for this test: reported as skipped, not as a pass (A6-19).
    std::printf("DecompilerFaultTests: SKIPPED (the matrix forks a process per cell; POSIX only)\n");
    return 77;
}

#else

#include "Decompiler/DecompilerRunner.h"
#include "Decompiler/SaturnArchitecture.h"
#include "Decompiler/SpecBundle.h"
#include "Decompiler/Sh2Decompiler.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

// A feature test, not a libc name: glibc and macOS both have backtrace (<execinfo.h>), dladdr (<dlfcn.h>) and
// abi::__cxa_demangle (<cxxabi.h>), and the pins' identity check (A6-12) needs all three. It was `__GLIBC__`, which left
// every pin "identity not checked" on macOS (A6-19). A platform without them, musl say, is told so loudly below.
#if defined(__has_include)
#if __has_include(<execinfo.h>) && __has_include(<dlfcn.h>) && __has_include(<cxxabi.h>)
#define SE_FAULT_BACKTRACE 1
#endif
#elif defined(__GLIBC__)
#define SE_FAULT_BACKTRACE 1
#endif
#ifdef SE_FAULT_BACKTRACE
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#endif

#if defined(__SANITIZE_ADDRESS__)
#define SE_FAULT_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SE_FAULT_ASAN 1
#endif
#endif
#ifdef SE_FAULT_ASAN
#include <sanitizer/common_interface_defs.h>
#include <sanitizer/lsan_interface.h>
#endif

using namespace sfe::decomp;
using Clock = std::chrono::steady_clock;

namespace {

// ================================================================================================
// What the engine did, as the phase hook sees it
// ================================================================================================

// The phases one request enters, in order, with the allocation count at each entry and whether it
// belongs to the reference run. A run's log is the oracle for where a fault or a cancel landed.
struct Log
{
    static const int kMax = 64;
    int n = 0;
    DecompilePhase phase[kMax];
    long allocAt[kMax];
    bool inRef[kMax];
    // The host's own check-points (Sh2Decompiler::SetBoundaryHookForTests), in the order they are made: the number of phase
    // entries before each, and the allocation count at it (A6-41).
    static const int kMaxBound = 32;
    int nb = 0;
    int boundEnters[kMaxBound];
    long boundAlloc[kMaxBound];
};

struct Probe
{
    std::atomic<int> enters{ 0 };
    DecompilePhase phase[Log::kMax];
    long allocAt[Log::kMax];
    bool inRef[Log::kMax];
    std::atomic<bool> inRefNow{ false };
    std::atomic<bool> engineEntered{ false };       // a phase that does work began: Sh2Decompiler::mWorkPhase != Idle
    std::atomic<int> cancelAtEnter{ -2 };           // raise the cancel flag at the start of this phase entry
    std::atomic<std::atomic<bool>*> cancelFlag{ nullptr };
    std::atomic<int> setOrdinal{ -2 };              // the phase entry the flag went up in (-1: before the first)
    std::atomic<int> bounds{ 0 };                   // host check-points reached
    int boundEnters[Log::kMaxBound];
    long boundAlloc[Log::kMaxBound];
    std::atomic<int> cancelAtBoundary{ -2 };        // raise the cancel flag at the start of this host check-point, before the check

    void Reset()
    {
        enters = 0;
        inRefNow = false;
        engineEntered = false;
        cancelAtEnter = -2;
        cancelAtBoundary = -2;
        bounds = 0;
        cancelFlag = nullptr;
        setOrdinal = -2;
    }
    Log Snapshot() const
    {
        Log l;
        l.n = std::min(enters.load(), int(Log::kMax));
        for (int i = 0; i < l.n; ++i)
        {
            l.phase[i] = phase[i];
            l.allocAt[i] = allocAt[i];
            l.inRef[i] = inRef[i];
        }
        l.nb = std::min(bounds.load(), int(Log::kMaxBound));
        for (int i = 0; i < l.nb; ++i)
        {
            l.boundEnters[i] = boundEnters[i];
            l.boundAlloc[i] = boundAlloc[i];
        }
        return l;
    }
};
Probe gProbe;

// ================================================================================================
// The allocator: count, fail one, raise the cancel flag at one
// ================================================================================================

namespace fa {

std::atomic<long> gCount(0);          // allocations since Arm()
std::atomic<long> gFailAt(-1);        // the 0-based allocation that throws std::bad_alloc, once
std::atomic<long> gCancelAt(-1);      // the allocation at which the cancel flag goes up
std::atomic<bool> gFired(false);
std::atomic<bool> gFiredNothrow(false);
std::atomic<long> gFiredSize(0);
// Sustained exhaustion (A6-23): every allocation from gFailAt on fails, not only that one, as a heap that is full stays
// full. SE_FAULT_STICKY=1, or a family named "...-sticky". Disarm() ends it.
std::atomic<bool> gSticky(false);
std::atomic<bool> gTrace(false);

#ifdef SE_FAULT_BACKTRACE
// ---- where an allocation was made (the identity of a pin, below) ------------------------------------
//
// The test executable is linked with -rdynamic (CMakeLists.txt), so dladdr names the engine's functions
// in a plain build as well as in a sanitizer one. A stack is kept as raw return addresses while the run
// is going and named afterwards, only for the few allocations a pin asks about.
const int kFrames = 30;
struct Stack
{
    int n = 0;
    void* f[kFrames];
};

// The function names of a stack, innermost first, demangled. A frame with no symbol is left out.
std::vector<std::string> Names(const Stack& s)
{
    std::vector<std::string> v;
    for (int i = 0; i < s.n; ++i)
    {
        Dl_info info;
        if (!dladdr(static_cast<char*>(s.f[i]) - 1, &info) || !info.dli_sname) continue;   // - 1: the call, not the instruction after it
        int st = 0;
        char* d = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &st);
        v.push_back(d && st == 0 ? std::string(d) : std::string(info.dli_sname));
        std::free(d);
    }
    return v;
}

// A stack captured by the allocator for the allocations a census asked for (never freed: a cell's process is short).
std::atomic<bool> gCensus(false);
const uint8_t* gCensusWant = nullptr;      // [k] != 0: keep the stack of allocation k
long gCensusWantN = 0;
Stack* gCensusStack = nullptr;
long* gCensusK = nullptr;
uint32_t* gCensusSize = nullptr;            // the size the run actually asked for (A6-24: a pin names a size, the census checks it)
long gCensusN = 0;
const long kCensusCap = 60000;

__attribute__((noinline)) void CensusRecord(long i, std::size_t size)
{
    if (gCensusN >= kCensusCap) return;
    Stack& s = gCensusStack[gCensusN];
    s.n = backtrace(s.f, kFrames);
    gCensusSize[gCensusN] = uint32_t(size);
    gCensusK[gCensusN++] = i;
}
#endif

// The stack of the allocation that was failed (raw addresses, named afterwards with Names), for the cells that
// classify a failure by where it fell.
Stack gFiredStack;

void Trace()
{
    // A short trace of the allocation that was failed, for the findings. Printed only when asked for
    // (--cell, or the driver's second run of a failing cell), so a sweep's logs stay short.
#ifdef SE_FAULT_ASAN
    std::fprintf(stderr, "TRACE of the failed allocation (index %ld, %ld bytes, %s):\n", gFailAt.load(), gFiredSize.load(),
                 gFiredNothrow ? "nothrow" : "throwing");
    __sanitizer_print_stack_trace();
#elif defined(SE_FAULT_BACKTRACE)
    std::fprintf(stderr, "TRACE of the failed allocation (index %ld, %ld bytes, %s):\n", gFailAt.load(), gFiredSize.load(),
                 gFiredNothrow ? "nothrow" : "throwing");
    Stack st;
    st.n = backtrace(st.f, kFrames);
    for (const std::string& n : Names(st)) std::fprintf(stderr, "    %s\n", n.c_str());
#endif
}

void Arm(long failAt, long cancelAt = -1)
{
    gFired = false;
    gFiredNothrow = false;
    gFiredSize = 0;
    gCancelAt = cancelAt;
    gFailAt = failAt;
    gCount = 0;
}

void Disarm()
{
    gFailAt = -1;
    gCancelAt = -1;
}

// The runner's child injects on the worker thread only: the test thread polling beside it must not
// shift the count or take the failure.
thread_local bool tCounted = true;

// The size of each allocation of a run (while gLogSizes), so a site found once -- "the 48-byte
// allocation in Print" -- can be found again by the next build, whose absolute indices may differ.
const long kSizeLogMax = 1L << 20;
uint32_t* gSizeLog = nullptr;       // allocated before the first logged run, never freed
std::atomic<bool> gLogSizes(false);

void* Allocate(std::size_t size, bool nothrow)
{
    if (!tCounted)
    {
        void* q = std::malloc(size ? size : 1);
        if (!q) throw std::bad_alloc();
        return q;
    }
    const long i = gCount.fetch_add(1, std::memory_order_relaxed);
    if (gLogSizes.load(std::memory_order_relaxed) && i < kSizeLogMax && gSizeLog) gSizeLog[i] = uint32_t(size);
#ifdef SE_FAULT_BACKTRACE
    if (gCensus.load(std::memory_order_relaxed) && i < gCensusWantN && gCensusWant[i]) CensusRecord(i, size);
#endif
    if (i == gCancelAt.load(std::memory_order_relaxed))
    {
        gProbe.setOrdinal = gProbe.enters.load() - 1;
        if (std::atomic<bool>* f = gProbe.cancelFlag.load()) f->store(true);
    }
    const long failAt = gFailAt.load(std::memory_order_relaxed);
    if (i == failAt || (failAt >= 0 && i > failAt && gSticky.load(std::memory_order_relaxed)))
    {
        if (i == failAt)
        {
            gFired = true;
            gFiredNothrow = nothrow;
            gFiredSize = long(size);
#ifdef SE_FAULT_BACKTRACE
            gFiredStack.n = backtrace(gFiredStack.f, kFrames);
#endif
            if (gTrace) Trace();
        }
        throw std::bad_alloc();
    }
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

}  // namespace fa

}  // namespace

void* operator new(std::size_t size) { return fa::Allocate(size, false); }
void* operator new[](std::size_t size) { return fa::Allocate(size, false); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
// The nothrow forms (libstdc++'s stable_sort buffer, which Ghidra's switch printing uses) report a
// failure by returning null; an injected failure there is the library's own fallback.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return fa::Allocate(size, true);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept { return operator new(size, tag); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace {

using dts::MsSince;

// ================================================================================================
// Fixtures
// ================================================================================================

struct Fix
{
    const char* name;
    uint32_t entry;
    bool lean;   // one pass, no callee registration: for the larger shapes
    // The request lends the callee's prototype and effects (the panel lends every function the user decompiled, A6-36):
    // 1 a pointer parameter and every register but r0 kept; 2 the same at the callee's cache-through mirror too; 3 a second
    // (uint2) parameter, r4, r5 and r8 kept and a stack delta of 4 (setExtraPop, registers killed); 4 a callee that returns a
    // uint8, which the cspec gives in the join of r0 and r1 (r1 is then not kept).
    unsigned lend = 0;
};

const uint32_t kMailboxBase = 0x0600A480u;
const uint32_t kShapesBase = 0x0600B000u;
const uint32_t kStackBase = 0x0600B100u;
const uint32_t kLoopBreakBase = 0x0600B300u;
const uint32_t kDagBase = 0x0600B400u;
const uint32_t kLendMirrorBase = 0x0600B500u;
const uint32_t kHwRegBase = 0x0600B600u;
const uint32_t kWideBase = 0x0600B700u;
const uint32_t kDivBase = 0x0600B800u;
const uint32_t kExtBase = 0x0600B820u;
const uint32_t kStk6Base = 0x0600B840u;
const uint32_t kNestBase = 0x0600BA00u;
const int kNestDepth = 6;
const uint32_t kAliasBase = 0x0600BB00u;
const Fix kFixtures[] = {
    { "mailbox", kMailboxBase, false },        // TestHardwareVolatility's mailboxFold: code is removed, the reference run happens
    { "jumptable", dts::kJumpTableBase, false },   // a 4-way switch recovered through its table
    { "fixture1", dts::kFixture1Caller, false },   // a call through a literal pool: the second pass names the callee
    { "diamonds", dts::kDiamondsBase, true },      // 150 branches: a long flow and a long action tree
    { "shapes", kShapesBase, false },              // loops, &&, ||, a do-while, an if/else: variables that merge (A6-11)
    { "stack", kStackBase, false },                // a stack array: local variables, restructuring, naming (A6-14, A6-15)
    { "loopbrk", kLoopBreakBase, false },          // a counted loop that returns from inside: the structurer picks a goto with TraceDAG (A6-22)
    { "dag", kDagBase, false },                    // two conditionals that share both targets, chosen by a third: TraceDAG's branch points (A6-22)
    { "lend", dts::kFixture1Caller, false, 1 },    // fixture1 with the callee's signature and effects lent: ApplyPrototype, LendEffects (A6-36)
    { "lendm", kLendMirrorBase, false, 2 },        // the same caller calling through the mirror 0x26005000, lent at both addresses: the mirror's registration
    { "lendx", dts::kFixture1Caller, false, 3 },   // lent with a stack delta and kept registers killed: setExtraPop
    { "hwreg", kHwRegBase, false },                // a VDP2 register read directly, and a store through a pointer that is one of two: HardwareNotes, PointerResolver
    { "wide", kWideBase, false },                  // dmulu.l: a 64-bit result returned in the join of r0 and r1 (area 6 round 5: the joined variable's pieces)
    { "lend8", dts::kFixture1Caller, false, 4 },   // fixture1 with its callee lent as returning a uint8: the caller reads the join of r0 and r1 (area 6 round 5)
    { "div3", kDivBase, false },                   // x / 3 as the compiler writes it (dmulu.l by 0xAAAAAAAB, the high half shifted): RuleDivOpt (area 6 round 5)
    { "ext", kExtBase, false },                    // exts.w, extu.b: sub-word values through the subvariable and extension rules (area 6 round 5)
    { "stk6", kStk6Base, false, 5 },               // a call lent six parameters, two of them on the stack (area 6 round 5)
    { "nest", kNestBase, false },                  // six ifs nested, each storing: a deep structure, not one condition (area 6 round 5)
    { "alias", kAliasBase, true },                 // two names of one work-RAM word (A5-52), one pass: the printing pass finds it, gives way and runs again (A6-43)
};

const Fix* FindFix(const std::string& name)
{
    for (const Fix& f : kFixtures)
        if (name == f.name) return &f;
    return nullptr;
}

std::shared_ptr<const MemorySnapshot> BuildSnapshot()
{
    std::vector<uint8_t> hw(0x100000u);
    // mailboxFold, as Sh2DecompilerTests defines it.
    const uint16_t code[] = { 0xD104, 0xE001, 0x2102, 0x6012, 0x2008, 0x8BFC, 0xD202, 0x2200, 0x000B, 0x0009,
                              0x2600, 0x1000, 0x2010, 0x0063 };
    size_t off = kMailboxBase - 0x06000000u;
    for (uint16_t w : code)
    {
        dts::Put16(hw, off, w);
        off += 2;
    }
    // `shapes`: if (r4>0 && r5>0), if (r6>=0 || r4>=0), a while loop on r5, a do-while on `dt r6`, an
    // if/else on r4==r5 and a guarded *r4 = *r4. Joins of distinct variables, so the variable merge
    // (HighVariable::mergeInternal) runs on a loop and on a join.
    const uint16_t shapes[] = { 0xE000, 0x4415, 0x8B03, 0x4515, 0x8B01, 0x7001, 0x0009, 0x4611, 0x8901, 0x4411, 0x8B01,
                                0x7002, 0x0009, 0x4515, 0x8B03, 0x75FF, 0x7003, 0xAFFA, 0x0009, 0x7004, 0x4610, 0x8BFC,
                                0x3450, 0x8B02, 0x7005, 0xA001, 0x0009, 0x7006, 0x2448, 0x8902, 0x6142, 0x2412, 0x0009,
                                0x000B, 0x0009 };
    off = kShapesBase - 0x06000000u;
    for (uint16_t w : shapes)
    {
        dts::Put16(hw, off, w);
        off += 2;
    }
    // `stack`: add #-16,r15 ; mov.l r4,@r15 ; mov.l r5,@(4,r15) ; mov r6,r0 ; mov.l @(r0,r15),r1 ; mov r1,r0 ;
    // rts ; add #16,r15. A frame with an indexed local: ScopeLocal::restructureVarnode, ActionNameVars.
    const uint16_t stack[] = { 0x7FF0, 0x2F42, 0x1F51, 0x6063, 0x01FE, 0x6013, 0x000B, 0x7F10 };
    off = kStackBase - 0x06000000u;
    for (uint16_t w : stack)
    {
        dts::Put16(hw, off, w);
        off += 2;
    }
    // `loopbrk`: mov #0,r0 ; L: add #1,r0 ; cmp/pl r4 ; bt OUT ; dt r5 ; bf L ; add #5,r0 ; OUT: rts ; nop. The counted
    // loop returns from inside it, which CollapseStructure::selectGoto gives to TraceDAG (A6-22).
    const uint16_t loopbrk[] = { 0xE000, 0x7001, 0x4415, 0x8902, 0x4510, 0x8BFA, 0x7005, 0x000B, 0x0009 };
    off = kLoopBreakBase - 0x06000000u;
    for (uint16_t w : loopbrk)
    {
        dts::Put16(hw, off, w);
        off += 2;
    }
    // `dag`: cmp/pl r4 ; bt C ; add #3,r0 ; cmp/pl r5 ; bt E ; bra D ; nop ; C: add #4,r0 ; cmp/pl r6 ; bt E ; nop ;
    // D: add #1,r0 ; bra F ; nop ; E: add #2,r0 ; nop ; F: rts ; nop. Two conditionals that share both targets and are
    // chosen by a third, with different work in each, so the conditional merge cannot fold them (A6-22).
    const uint16_t dag[] = { 0x4415, 0x8904, 0x7003, 0x4515, 0x8908, 0xA004, 0x0009, 0x7004, 0x4615, 0x8903, 0x0009,
                             0x7001, 0xA002, 0x0009, 0x7002, 0x0009, 0x000B, 0x0009 };
    off = kDagBase - 0x06000000u;
    for (uint16_t w : dag)
    {
        dts::Put16(hw, off, w);
        off += 2;
    }
    dts::PlaceFixture1(hw);
    // `lendm`: fixture1's caller again, at kLendMirrorBase, whose literal names the callee through its cache-through mirror.
    for (uint32_t i = 0; i < 0x1C; i += 2)
        dts::Put16(hw, kLendMirrorBase - 0x06000000u + i, uint16_t((hw[dts::kFixture1Caller - 0x06000000u + i] << 8) | hw[dts::kFixture1Caller - 0x06000000u + i + 1]));
    dts::Put32(hw, kLendMirrorBase - 0x06000000u + 0x1Cu, 0x26005000u);
    // `hwreg`: mov.l @(0x14),r1 (VDP2 TVMD) ; mov.w @r1,r0 ; cmp/pl r4 ; bt L ; mov.l @(0x18),r1 (VDP2 EXTEN) ; nop ;
    // L: mov.w r0,@r1 ; rts ; nop. HardwareNotes names the registers, and PointerResolver resolves the store's pointer,
    // which is one of two (A6-36).
    const uint16_t hwreg[] = { 0xD104, 0x6011, 0x4415, 0x8901, 0xD103, 0x0009, 0x2101, 0x000B, 0x0009, 0x0009 };
    off = kHwRegBase - 0x06000000u;
    for (uint16_t w : hwreg)
    {
        dts::Put16(hw, off, w);
        off += 2;
    }
    dts::Put32(hw, kHwRegBase - 0x06000000u + 0x14u, 0x25F80000u);
    dts::Put32(hw, kHwRegBase - 0x06000000u + 0x18u, 0x25F80002u);
    // `wide`: dmulu.l r4,r5 ; sts mach,r0 ; sts macl,r1 ; rts ; nop. The product is 64 bits, and the cspec returns a 64-bit value in the
    // join of r0 and r1 (the high half in r0, patch 0007): the engine builds a joined variable and its pieces (VariableGroup,
    // VariablePiece), which no other fixture reaches.
    const uint16_t wide[] = { 0x3545, 0x000A, 0x011A, 0x000B, 0x0009 };
    off = kWideBase - 0x06000000u;
    for (uint16_t w : wide)
    {
        dts::Put16(hw, off, w);
        off += 2;
    }
    // `div3`: mov.l @(2,pc),r1 ; dmulu.l r1,r4 ; sts mach,r0 ; shlr r0 ; rts ; nop ; .long 0xAAAAAAAB. r4 / 3 the way a compiler writes it.
    const uint16_t div3[] = { 0xD102, 0x3415, 0x000A, 0x4001, 0x000B, 0x0009, 0xAAAB, 0xAAAA };
    off = kDivBase - 0x06000000u;
    for (size_t i = 0; i < sizeof div3 / sizeof div3[0]; ++i)
    {
        dts::Put16(hw, off, i < 6 ? div3[i] : uint16_t(0));
        off += 2;
    }
    dts::Put32(hw, kDivBase - 0x06000000u + 12u, 0xAAAAAAABu);
    // `ext`: exts.w r4,r0 ; add r5,r0 ; extu.b r0,r0 ; rts ; nop. Values of two and one byte, widened and narrowed.
    const uint16_t ext[] = { 0x604F, 0x305C, 0x600C, 0x000B, 0x0009 };
    off = kExtBase - 0x06000000u;
    for (uint16_t w : ext)
    {
        dts::Put16(hw, off, w);
        off += 2;
    }
    // `stk6`: add #-8,r15 ; mov.l r4,@r15 ; mov.l r5,@(4,r15) ; mov.l @(3,pc),r1 ; jsr @r1 ; nop ; add #8,r15 ; rts ; nop ; nop ;
    // .long 0x06005000. The callee is lent six parameters; the fifth and sixth are the two stored words.
    const uint16_t stk6[] = { 0x7FF8, 0x2F42, 0x1F51, 0xD103, 0x410B, 0x0009, 0x7F08, 0x000B, 0x0009, 0x0009 };
    off = kStk6Base - 0x06000000u;
    for (uint16_t w : stk6)
    {
        dts::Put16(hw, off, w);
        off += 2;
    }
    dts::Put32(hw, kStk6Base - 0x06000000u + 20u, dts::kFixture1Callee);
    // `nest`: kNestDepth times (mov.l @r4+,r1 ; cmp/pl r1 ; bt .+2 ; bra END_i ; nop ; mov.l r1,@-r6), then add #3,r3 and one
    // `add #1,r2` for each level (innermost first), then rts. Nested ifs, each with a store of its own, so that the conditions do not
    // merge into one && chain (a first version without the stores did) and the structurer nests the blocks.
    {
        const uint32_t endsAt = kNestBase + 12u * unsigned(kNestDepth) + 2u;
        off = kNestBase - 0x06000000u;
        auto put = [&](uint16_t w) {
            dts::Put16(hw, off, w);
            off += 2;
        };
        for (int i = 0; i < kNestDepth; ++i)
        {
            const uint32_t braAt = kNestBase + 12u * unsigned(i) + 6u;
            const int disp = int((int64_t(endsAt + 2u * unsigned(kNestDepth - 1 - i)) - int64_t(braAt + 4)) / 2);
            for (uint16_t w : { uint16_t(0x6146), uint16_t(0x4115), uint16_t(0x8901), uint16_t(0xA000 | (disp & 0xFFF)), uint16_t(0x0009), uint16_t(0x2616) })
                put(w);
        }
        put(0x7303);
        for (int i = 0; i < kNestDepth; ++i) put(0x7201);
        put(0x000B);
        put(0x0009);
    }
    // `alias`: mov.l @(0x14),r2 ; mov.l @(0x18),r1 ; mov #5,r0 ; mov.w r0,@r2 ; mov #7,r0 ; mov.w r0,@r1 ; mov.w @r2,r0 ; rts ; nop.
    // The two literals are 0x26001000 and 0x06001000, two names of one word of work RAM, which the engine keys apart. The host finds it in
    // the printing pass's actions, makes the word volatile and runs that pass again (A5-52): a third pass, and a Teardown/Init
    // boundary after an Actions that is not the first pass's (A6-43).
    const uint16_t alias[] = { 0xD204, 0xD105, 0xE005, 0x2201, 0xE007, 0x2101, 0x6021, 0x000B, 0x0009, 0x0009 };
    off = kAliasBase - 0x06000000u;
    for (uint16_t w : alias)
    {
        dts::Put16(hw, off, w);
        off += 2;
    }
    dts::Put32(hw, kAliasBase - 0x06000000u + 0x14u, 0x26001000u);
    dts::Put32(hw, kAliasBase - 0x06000000u + 0x18u, 0x06001000u);
    dts::PlaceDiamonds(hw, 150);
    dts::PlaceJumpTable(hw, 4, 2);
    return dts::MakeSnapshot(hw);
}

DecompileRequest MakeRequest(const Fix& f)
{
    DecompileRequest q;
    q.entry = f.entry;
    // Work the wrapper does before the engine's first phase (the name map, the known entries, the
    // volatile aliases), so that an allocation failure there is a failure outside the engine.
    q.names.emplace_back(f.entry, std::string("Fn_") + f.name);
    q.knownFunctions.push_back(0x06005000u);
    q.volatileMemory.emplace_back(0x26002000u, 0x26002003u);   // not the mailbox
    if (f.lend)
    {
        Prototype p;
        p.known = true;
        p.ret.kind = TypeKind::Int;
        p.ret.size = f.lend == 4 ? 8 : 4;
        if (f.lend == 4) p.ret.kind = TypeKind::Uint;
        TypeShape ptr;
        ptr.kind = TypeKind::Pointer;
        ptr.size = 4;
        ptr.pointee = TypeKind::Int;
        ptr.pointeeSize = 4;
        p.params.push_back(ptr);
        p.effects.preserved = ((1u << 22) - 1) & ~(f.lend == 4 ? 3u : 1u);   // all but r0 (and r1, for a 64-bit return)
        p.effects.spDelta = 0;
        if (f.lend == 3)
        {
            p.effects.preserved = (1u << 4) | (1u << 5) | (1u << 16) | (1u << 8);   // r4, r5, r8 and SR kept, the rest killed
            p.effects.spDelta = 4;
            TypeShape u;
            u.kind = TypeKind::Uint;
            u.size = 2;
            p.params.push_back(u);
        }
        if (f.lend == 5)
        {
            // stk6: five more parameters after the pointer, four in r4-r7 and the fifth and sixth on the stack
            TypeShape word;
            word.kind = TypeKind::Int;
            word.size = 4;
            for (int i = 0; i < 5; ++i) p.params.push_back(word);
        }
        q.prototypes.emplace_back(dts::kFixture1Callee, p);
        if (f.lend == 2) q.prototypes.emplace_back(0x26005000u, p);
    }
    if (f.lean)
    {
        q.registerCallees = false;
        q.maxInstructions = 100000;
    }
    return q;
}

// Everything a run is compared by (not its timing).
std::string Signature(const DecompileResult& r)
{
    std::ostringstream s;
    s << int(r.status) << '|' << r.error << '|' << int(r.cancelledIn) << '\n' << r.Text() << '\n';
    for (const std::string& w : r.warnings) s << "W " << w << '\n';
    for (uint32_t c : r.callees) s << "C " << c << '\n';
    for (uint32_t c : r.calledAt) s << "A " << c << '\n';
    for (uint32_t c : r.literalPools) s << "L " << c << '\n';
    for (const auto& b : r.body) s << "B " << b.first << ' ' << b.second << '\n';
    s << "acc " << r.accesses.size() << " fates " << r.accessFates.size() << " calls " << r.calls.size() << '\n';
    s << "proto " << r.prototype.known << ' ' << r.prototypeWithheld << '\n';
    return s.str();
}

bool Has(const std::string& text, const char* what) { return text.find(what) != std::string::npos; }

// The first line where two signatures differ, for a report.
std::string FirstDiff(const std::string& want, const std::string& got)
{
    std::istringstream a(want), b(got);
    std::string x, y;
    for (int line = 1;; ++line)
    {
        const bool ha = bool(std::getline(a, x)), hb = bool(std::getline(b, y));
        if (!ha && !hb) return "(identical)";
        if (ha != hb || x != y)
            return "line " + std::to_string(line) + ": want '" + (ha ? x.substr(0, 120) : "(end)") + "' got '" + (hb ? y.substr(0, 120) : "(end)") + "'";
    }
}

void Expect(bool ok, const char* what, const std::string& detail = std::string())
{
    if (ok) return;
    std::printf("CHECK failed: %s%s%s\n", what, detail.empty() ? "" : " -- ", detail.c_str());
    ++dts::Failures();
}

// ================================================================================================
// The oracle for a cancel: where the design says a flag raised at a point is honoured
// ================================================================================================

enum Outcome : unsigned
{
    kOk = 1,             // completed; no check-point was left
    kOkRefCut = 2,       // completed, the reference run cut short: Ok with the "could not be checked" warning
    kCancelFlow = 4,     // Cancelled, cancelledIn Flow
    kCancelActions = 8,  // Cancelled, cancelledIn Actions
};

// A flag raised at the start of phase entry 's' of the baseline log (-1: before the first). The
// check-points are in Flow (the engine's, and ours before Actions) and in Actions (the engine's,
// and ours before Print); Init, Print and Teardown have none (Sh2Decompiler.h). A cancel in the
// reference run is the check being cut short, not the request: the result is Ok. The comment at
// Sh2Decompiler.cpp's checkCancel says a cancel in a phase is honoured in that phase. The Actions
// of the first pass, which does not print, were the one exception (the cancel landed in the second
// pass's Flow, after its uncheckable Init: A6-6); runPass now checks at the pass boundary.
unsigned Expected(const Log& L, int s)
{
    if (s < 0) return kCancelFlow;   // up before the run: the first Flow
    const DecompilePhase p = L.phase[s];
    if (p == DecompilePhase::Flow) return L.inRef[s] ? kOkRefCut : kCancelFlow;
    if (p == DecompilePhase::Actions)
    {
        const bool prints = s + 1 < L.n && L.phase[s + 1] == DecompilePhase::Print;
        if (prints) return L.inRef[s] ? kOkRefCut : kCancelActions;
        // The first pass has no Print after it, so its check-point is at the pass boundary
        // (Sh2Decompiler.cpp runPass): a flag raised in the tail of its actions is honoured there, in
        // Actions, and not carried through the next pass's Init into its Flow (A6-6).
        return kCancelActions;
    }
    // The Teardown that ends the first pass (its Actions before it, the second pass's Init after it): the pass boundary's
    // check-point comes after it, with the Architecture gone, and reports the phase the pass was in (A6-35). Before that
    // check was moved a raise here was carried through the second pass's Init into its Flow.
    if (p == DecompilePhase::Teardown && s > 0 && L.phase[s - 1] == DecompilePhase::Actions && s + 1 < L.n &&
        L.phase[s + 1] == DecompilePhase::Init && !L.inRef[s])
        return kCancelActions;
    for (int j = s + 1; j < L.n; ++j)
        if (L.phase[j] == DecompilePhase::Flow) return L.inRef[j] ? kOkRefCut : kCancelFlow;
    return kOk;
}

unsigned ExpectedRange(const Log& L, int s0, int s1)
{
    unsigned m = 0;
    for (int s = s0; s <= s1; ++s) m |= Expected(L, s);
    return m;
}

// A flag raised by another thread, read against the phase-entry counter: s0 and s1 are the counter (less one) before and
// after the raise. The counter is incremented by the hook, which the host calls AFTER the check-point that precedes the
// entry (`checkCancel(); Enter(next)`), so a raise that comes after that check and before the hook is read as "in" the
// entry before, and is honoured as a raise at the entry after it (A6-35): Actions' end carried into Print's Ok, the
// Flow-to-Actions boundary honoured in Actions, and the first pass's end carried into the second pass's Flow. The
// honoured outcome of a raise read as in entries s0..s1 is therefore that of s0..s1 + 1. The window is the few
// instructions between a check and the next hook (the first pass's was the destruction of the pass's locals and its
// Architecture, wide under a sanitizer, until the check was moved after them); the cancel-enter cells reach each of
// them deterministically (WindowCell), and fail if this widening is removed.
unsigned ExpectedThread(const Log& L, int s0, int s1)
{
    unsigned m = ExpectedRange(L, std::max(s0, -1), std::min(s1 + 1, L.n - 1));
    if (s1 + 1 >= L.n) m |= kOk;
    return m;
}

unsigned Observed(const DecompileResult& r)
{
    if (r.status == DecompileStatus::Cancelled)
    {
        if (r.cancelledIn == DecompilePhase::Flow) return kCancelFlow;
        if (r.cancelledIn == DecompilePhase::Actions) return kCancelActions;
        return 0;
    }
    if (r.status != DecompileStatus::Ok) return 0;
    for (const std::string& w : r.warnings)
        if (Has(w, "could not be checked for hardware accesses (cancelled)")) return kOkRefCut;
    return kOk;
}

std::string MaskTag(unsigned m)
{
    std::string s;
    if (m & kOk) s += "Ok";
    if (m & kOkRefCut) s += (s.empty() ? "" : "+") + std::string("OkRefCut");
    if (m & kCancelFlow) s += (s.empty() ? "" : "+") + std::string("CancelFlow");
    if (m & kCancelActions) s += (s.empty() ? "" : "+") + std::string("CancelActions");
    return s.empty() ? "none" : s;
}

std::string MaskText(unsigned m)
{
    std::string s;
    if (m & kOk) s += "Ok ";
    if (m & kOkRefCut) s += "Ok-with-reference-cut ";
    if (m & kCancelFlow) s += "Cancelled-in-Flow ";
    if (m & kCancelActions) s += "Cancelled-in-Actions ";
    return s.empty() ? "(none)" : s;
}

// ================================================================================================
// A driver: an engine warmed once, a baseline, and cells forked from it
// ================================================================================================

struct Baseline
{
    std::string sig;
    long allocs = 0;
    Log log;
    double ms = 0;
    std::vector<uint32_t> sizes;   // the size of each allocation of the counted run
};

struct Ctx
{
    std::string config;
    const Fix* fix = nullptr;
    std::shared_ptr<const MemorySnapshot> snap;
    DecompileRequest req;
    std::unique_ptr<Sh2Decompiler> engine;
    Baseline base;
};

// One counted run: the same statement is used for every cell, so the counts agree.
DecompileResult RunOnce(Ctx& c, std::atomic<bool>* cancel = nullptr)
{
    return c.engine->Decompile(c.snap, c.req, cancel);
}

void InstallHooks()
{
    Sh2Decompiler::SetPhaseHookForTests([](DecompilePhase p) {
        const int o = gProbe.enters.fetch_add(1);
        if (o < Log::kMax)
        {
            gProbe.phase[o] = p;
            gProbe.allocAt[o] = fa::gCount.load();
            gProbe.inRef[o] = gProbe.inRefNow.load();
        }
        if (p != DecompilePhase::Teardown) gProbe.engineEntered = true;
        if (o == gProbe.cancelAtEnter.load())
        {
            gProbe.setOrdinal = o;
            if (std::atomic<bool>* f = gProbe.cancelFlag.load()) f->store(true);
        }
    });
    Sh2Decompiler::SetBeforeReferenceHookForTests([] { gProbe.inRefNow = true; });
    // Called by the host immediately before each of its own cancel check-points (A6-41): the flag goes up here, in the phase
    // that is ending, and only the check that follows can honour it in that phase.
    Sh2Decompiler::SetBoundaryHookForTests([] {
        const int b = gProbe.bounds.fetch_add(1);
        if (b < Log::kMaxBound)
        {
            gProbe.boundEnters[b] = gProbe.enters.load();
            gProbe.boundAlloc[b] = fa::gCount.load();
        }
        if (b == gProbe.cancelAtBoundary.load())
        {
            gProbe.setOrdinal = gProbe.enters.load() - 1;
            if (std::atomic<bool>* f = gProbe.cancelFlag.load()) f->store(true);
        }
    });
}

// ---- process plumbing ----------------------------------------------------------------------

std::string gSelf;

std::string ReadFile(const std::string& path)
{
    std::string s;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return s;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

double EnvDouble(const char* name, double dflt)
{
    const char* v = std::getenv(name);
    return v && *v ? std::atof(v) : dflt;
}

bool Full()
{
    const char* v = std::getenv("SE_FAULT_FULL");
    return v && *v && std::string(v) != "0";
}

// The default allowance for one cell or one driver: generous, because a sanitizer build is slow;
// a cell that exceeds it is a hang.
double CellTimeout() { return EnvDouble("SE_FAULT_TIMEOUT", 120.0); }

struct ProcResult
{
    bool exited = false;
    int code = 0;
    int signal = 0;
    bool timedOut = false;
};

ProcResult WaitFor(pid_t pid, double timeoutSec)
{
    ProcResult r;
    const auto t0 = Clock::now();
    for (;;)
    {
        int st = 0;
        const pid_t w = waitpid(pid, &st, WNOHANG);
        if (w == pid)
        {
            if (WIFEXITED(st))
            {
                r.exited = true;
                r.code = WEXITSTATUS(st);
            }
            else if (WIFSIGNALED(st))
            {
                r.signal = WTERMSIG(st);
            }
            return r;
        }
        if (w < 0) return r;
        if (MsSince(t0) > timeoutSec * 1000.0)
        {
            kill(pid, SIGKILL);
            waitpid(pid, &st, 0);
            r.timedOut = true;
            return r;
        }
        usleep(1500);
    }
}

void RedirectOutput(const std::string& file)
{
    const int fd = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
}

pid_t SpawnSelf(const std::vector<std::string>& args, const std::string& outFile)
{
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0)
    {
        RedirectOutput(outFile);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(gSelf.c_str()));
        for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(gSelf.c_str(), argv.data());
        _exit(127);
    }
    return pid;
}

// ---- classifying a cell's end ------------------------------------------------------------------

const int kExitInvariant = 10;
const int kExitLeak = 11;
const int kExitStaticDestruction = 97;

struct Verdict
{
    std::string kind;   // "" when clean
    std::string sig;    // what two cells with the same defect share
};

std::vector<std::string> Lines(const std::string& text)
{
    std::vector<std::string> v;
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) v.push_back(l);
    return v;
}

// The first few frames of a sanitizer report that belong to the code under test, so two reports of
// one defect agree and a different one does not.
std::string FramesAfter(const std::vector<std::string>& lines, size_t from)
{
    std::string s;
    int taken = 0;
    for (size_t i = from; i < lines.size() && taken < 4; ++i)
    {
        const std::string& l = lines[i];
        const size_t hash = l.find("#");
        if (hash == std::string::npos || hash > 6) continue;
        if (l.find("operator new") != std::string::npos || l.find("libasan") != std::string::npos ||
            l.find("libc.so") != std::string::npos || l.find(" in Allocate ") != std::string::npos ||
            l.find("DecompilerFaultTests.cpp") != std::string::npos ||
            l.find("in malloc") != std::string::npos)
            continue;
        const size_t in = l.find(" in ");
        std::string f = in == std::string::npos ? l.substr(hash) : l.substr(in + 4);
        const size_t sp = f.find(' ');
        if (sp != std::string::npos && f.find('(') == std::string::npos) f = f.substr(0, sp);
        s += f + " < ";
        ++taken;
    }
    return s;
}

bool AllowedRuntimeError(const std::string& l)
{
    // The two accepted upstream SLEIGH shifts (check-sanitizer-logs.sh).
    return (l.find("address.hh:605:") != std::string::npos && l.find("left shift of") != std::string::npos) ||
           (l.find("slghpatexpress.cc:979:") != std::string::npos && l.find("left shift of negative value") != std::string::npos);
}

Verdict Classify(const ProcResult& pr, const std::string& out)
{
    Verdict v;
    const std::vector<std::string> lines = Lines(out);
    std::string summary, firstCheck, ubsan, leakFrames;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string& l = lines[i];
        if (summary.empty() && l.find("SUMMARY: AddressSanitizer") != std::string::npos && l.find("leaked") == std::string::npos)
            summary = l.substr(l.find("SUMMARY:"));
        if (firstCheck.empty() && l.compare(0, 13, "CHECK failed:") == 0) firstCheck = l.substr(14);
        if (ubsan.empty() && l.find("runtime error:") != std::string::npos && !AllowedRuntimeError(l))
        {
            const size_t slash = l.rfind('/');
            ubsan = slash == std::string::npos ? l : l.substr(slash + 1);
        }
        if (leakFrames.empty() && (l.find("Direct leak") != std::string::npos || l.find("Indirect leak") != std::string::npos))
            leakFrames = FramesAfter(lines, i + 1);
    }
    const bool asan = out.find("ERROR: AddressSanitizer") != std::string::npos;
    const bool leak = out.find("ERROR: LeakSanitizer") != std::string::npos;
    if (pr.timedOut)
    {
        v.kind = "hang";
        v.sig = "hang";
    }
    else if (pr.signal)
    {
        v.kind = "crash";
        v.sig = std::string("signal ") + std::to_string(pr.signal) + (summary.empty() ? std::string() : " " + summary);
    }
    else if (asan)
    {
        v.kind = "asan";
        v.sig = summary.empty() ? "AddressSanitizer" : summary;
    }
    else if (!ubsan.empty())
    {
        v.kind = "ubsan";
        v.sig = ubsan.substr(0, 160);
    }
    else if (pr.code == kExitStaticDestruction)
    {
        v.kind = "static-destruction";
        v.sig = "left through exit() with a damaged engine";
    }
    else if (leak || pr.code == kExitLeak)
    {
        v.kind = "leak";
        v.sig = "leak: " + leakFrames;
    }
    else if (pr.code != 0)
    {
        v.kind = "invariant";
        v.sig = firstCheck.empty() ? "exit " + std::to_string(pr.code) : firstCheck.substr(0, 200);
        // The cell's own numbers differ cell to cell; the check's text does not.
        for (char& ch : v.sig)
            if (ch >= '0' && ch <= '9') ch = '#';
    }
    return v;
}

// ---- a cell = a fork of the warmed driver --------------------------------------------------------

struct CellRun
{
    ProcResult proc;
    std::string out;
};

// 'body' runs in the forked child and returns the exit code, or leaves through _Exit itself.
CellRun ForkCell(const std::string& scratch, const std::function<int()>& body, double timeoutSec)
{
    const std::string file = dts::Join(scratch, "cell.out");
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0)
    {
        RedirectOutput(file);
        const int rc = body();
        std::fflush(nullptr);
        std::exit(rc);   // the sanitizers' exit-time leak check runs; a damaged engine never gets here
    }
    CellRun c;
    c.proc = WaitFor(pid, timeoutSec);
    c.out = ReadFile(file);
    return c;
}

// The baseline is run N+1 of the series, measured in a forked child: the run every cell makes (a cell is a fork of
// this process at run N). Taking it from run N, as Warm did, named the wrong allocation across a boundary of the markup
// (A6-24): the markup of an unchanged listing is not the same size from one run to the next. The engine numbers the
// begin/end pairs of the printer's groups with a process-wide counter (`TokenSplit::countbase`, prettyprint.hh) that is
// never reset and grows by the groups of every print; these ids are written into the markup (`open` and `close` on
// every parenthesis) as varints, so the markup is a byte longer whenever the counter passes 127 or 16,383 (stack: 1,114,
// 1,124 and 1,128 bytes over the first runs, mailbox 840, 842 and 844). The text and the tokens do not use them (the host
// reads `color`, `indent`, `content` and `opref`); `--diffruns` shows it. Two consecutive runs that agree do not make the
// next one agree, so the next one is measured.
bool MeasureBaseline(Ctx& c, const std::string& scratch)
{
    const std::string sizesFile = dts::Join(scratch, "baseline.sizes");
    const std::string sigFile = dts::Join(scratch, "baseline.sig");
    auto body = [&]() -> int {
        gProbe.Reset();
        fa::Arm(-1);
        fa::gLogSizes = true;
        const auto t0 = Clock::now();
        const DecompileResult w = RunOnce(c);
        const double ms = MsSince(t0);
        fa::gLogSizes = false;
        const long n = fa::gCount.load();
        fa::Disarm();
        const Log log = gProbe.Snapshot();
        const std::string sig = Signature(w);
        std::printf("BASE %d %ld %.3f %d\n", int(w.status), n, ms, log.n);
        for (int i = 0; i < log.n; ++i) std::printf("BASELOG %d %ld %d\n", int(log.phase[i]), log.allocAt[i], log.inRef[i] ? 1 : 0);
        for (int i = 0; i < log.nb; ++i) std::printf("BASEBOUND %d %ld\n", log.boundEnters[i], log.boundAlloc[i]);
        std::fflush(stdout);
        FILE* f = std::fopen(sizesFile.c_str(), "wb");
        if (!f) return 3;
        std::fwrite(fa::gSizeLog, sizeof(uint32_t), size_t(std::min(n, fa::kSizeLogMax)), f);
        std::fclose(f);
        f = std::fopen(sigFile.c_str(), "wb");
        if (!f) return 3;
        std::fwrite(sig.data(), 1, sig.size(), f);
        std::fclose(f);
        std::fflush(nullptr);
        std::_Exit(0);
    };
    const CellRun run = ForkCell(scratch, body, CellTimeout());
    long n = -1, entries = 0;
    int status = -1;
    double ms = 0;
    Log log;
    for (const std::string& l : Lines(run.out))
    {
        if (l.compare(0, 5, "BASE ") == 0)
            std::sscanf(l.c_str() + 5, "%d %ld %lf %ld", &status, &n, &ms, &entries);
        else if (l.compare(0, 8, "BASELOG ") == 0 && log.n < Log::kMax)
        {
            int p = 0, ref = 0;
            long at = 0;
            std::sscanf(l.c_str() + 8, "%d %ld %d", &p, &at, &ref);
            log.phase[log.n] = static_cast<DecompilePhase>(p);
            log.allocAt[log.n] = at;
            log.inRef[log.n] = ref != 0;
            ++log.n;
        }
        else if (l.compare(0, 10, "BASEBOUND ") == 0 && log.nb < Log::kMaxBound)
        {
            int at = 0;
            long al = 0;
            std::sscanf(l.c_str() + 10, "%d %ld", &at, &al);
            log.boundEnters[log.nb] = at;
            log.boundAlloc[log.nb] = al;
            ++log.nb;
        }
    }
    const std::string sizes = ReadFile(sizesFile), sig = ReadFile(sigFile);
    if (n < 0 || status != int(DecompileStatus::Ok) || entries != log.n || sizes.size() != size_t(std::min(n, fa::kSizeLogMax)) * sizeof(uint32_t) ||
        sig.empty())
    {
        std::printf("the baseline run (N+1, in a child) of '%s' did not report (status %d, %ld allocations)\n", c.fix->name, status, n);
        return false;
    }
    c.base.allocs = n;
    c.base.ms = ms;
    c.base.log = log;
    c.base.sig = sig;
    c.base.sizes.resize(sizes.size() / sizeof(uint32_t));
    std::memcpy(c.base.sizes.data(), sizes.data(), sizes.size());
    return true;
}

bool Warm(Ctx& c, const std::string& scratch, const std::string& fixture)
{
    c.fix = FindFix(fixture);
    if (!c.fix)
    {
        std::printf("unknown fixture '%s'\n", fixture.c_str());
        return false;
    }
    c.config = dts::Join(scratch, "config");
    sfe::MakeDirectory(scratch);
    sfe::MakeDirectory(c.config);
    c.snap = BuildSnapshot();
    c.req = MakeRequest(*c.fix);
    fa::gSizeLog = static_cast<uint32_t*>(std::malloc(sizeof(uint32_t) * size_t(fa::kSizeLogMax)));
    c.engine.reset(new Sh2Decompiler(c.config));
    InstallHooks();
    // Warm runs until two in a row agree: the first builds the translator and the library state, and
    // the translator's caches (parse-tree ring, patch 0020) settle over the next one or two. A cell
    // forks from this state, so its run is the next in the series, N+1. That run, and not run N, is the
    // baseline: it is measured in a forked child below (A6-24).
    long prevCount = -1;
    std::string prevSig;
    Log prevLog{};
    std::vector<uint32_t> prevSizes;
    bool settled = false;
    std::string series;
    for (int i = 0; i < 16 && !settled; ++i)
    {
        gProbe.Reset();
        fa::Arm(-1);
        fa::gLogSizes = true;
        const auto t0 = Clock::now();
        const DecompileResult w = RunOnce(c);
        const double ms = MsSince(t0);
        fa::gLogSizes = false;
        fa::Disarm();
        if (w.status != DecompileStatus::Ok)
        {
            std::printf("the warm run of '%s' is %d (%s)\n", fixture.c_str(), int(w.status), w.error.c_str());
            return false;
        }
        const long n = fa::gCount.load();
        const Log log = gProbe.Snapshot();
        const std::string sig = Signature(w);
        series += std::to_string(n) + " ";
        const std::vector<uint32_t> sizes(fa::gSizeLog, fa::gSizeLog + std::min(n, fa::kSizeLogMax));
        bool same = i > 0 && n == prevCount && sig == prevSig && log.n == prevLog.n && sizes == prevSizes;
        for (int k = 0; same && k < log.n; ++k) same = log.phase[k] == prevLog.phase[k] && log.allocAt[k] == prevLog.allocAt[k];
        prevCount = n;
        prevSig = sig;
        prevLog = log;
        prevSizes = sizes;
        if (same)
        {
            settled = true;
            c.base.ms = ms;
        }
    }
    if (!settled)
    {
        std::printf("the allocation count of '%s' does not settle (%s): the matrix would be meaningless\n", fixture.c_str(), series.c_str());
        return false;
    }
    if (!MeasureBaseline(c, scratch)) return false;
    std::string phases;
    for (int i = 0; i < c.base.log.n; ++i)
    {
        phases += PhaseName(c.base.log.phase[i]);
        phases += c.base.log.inRef[i] ? "(ref)@" : "@";
        phases += std::to_string(c.base.log.allocAt[i]);
        phases += ' ';
    }
    std::printf("fixture %-9s: %ld allocations, %.1f ms warm, %d phase entries: %s\n", fixture.c_str(), c.base.allocs, c.base.ms,
                c.base.log.n, phases.c_str());
    return true;
}


// A damaged engine must not run its static destruction (the translator may be half reset); an
// exit() that gets there is caught by this handler, registered after the translator's static was
// constructed so that it runs first.
std::atomic<bool> gMustNotExit(false);
void OnExit()
{
    if (gMustNotExit.load())
    {
        static const char msg[] = "FATAL: left through exit() (static destruction) with a damaged engine\n";
        if (write(2, msg, sizeof msg - 1) < 0) {}
        _Exit(kExitStaticDestruction);
    }
}

// SE_FAULT_EXIT=static: a damaged engine leaves through shutdown() and exit() (static destruction)
// instead of _Exit, to prove the application can still quit after an engine-side failure.
bool ExitStatic()
{
    const char* v = std::getenv("SE_FAULT_EXIT");
    return v && std::string(v) == "static";
}

// ---- the leaks that are accepted (REVIEW_LEDGER.md, rows A6-7 .. A6-9) ----------------------------
//
// An allocation failure inside the engine leaves the process refusing the engine for good (the host
// marks it damaged), so a leak the failure causes is bounded: once per process, a few hundred bytes
// to about 30 KB. The engine owns what it builds through raw pointers that the unwinding does not
// reach, in places (the XML reader's parse stack, the action tree, the rule pool) where making them
// exception-safe is a rewrite, not a patch. Each such site is named below by frames of the leaking
// allocation's stack: a leak is accepted only if every frame of one entry occurs in its stack, in
// the order written, innermost first. A leak that matches none fails the cell, so a new one -- or one
// of the sites below in a place it was not seen -- is found. (Not LeakSanitizer's own suppressions:
// they match one frame, which would excuse every leak that passes through it.)
#ifdef SE_FAULT_ASAN
struct AcceptedLeak
{
    const char* row;
    std::vector<const char*> frames;
    bool initOnly;   // accepted only for an allocation failure inside the Init phase (A6-15)
};
// The phase the failed allocation fell in, for the entries above that are bounded by it (set by the cell
// before the leak check). A6-7's sites are in Architecture construction and in the host's register loop, both
// of which run in Init; the engine calls some of the same functions in the Actions (Scope::addSymbol from
// ScopeLocal::createEntry, ProtoStoreSymbol::setInput, Funcdata::linkSymbol, ScopeInternal::renameSymbol), and
// a leak there is a different defect that a frame match alone would excuse as the register loop (A6-15).
bool gFailedInInit = false;
const std::vector<AcceptedLeak>& AcceptedLeaks()
{
    // Round 2 (A6-15) took out what patch 0032 fixed: the host's register loop and the engine's
    // Scope::addSymbol / addMapPoint (a Symbol or map entry the failed insertion leaves unowned is freed now),
    // the conflict symbol of ActionNameVars::linkSymbols, and the copy of the action tree (ActionDatabase::
    // deriveAction: ActionGroup, ActionRestartGroup and ActionPool::clone free what they built). The sweeps of
    // Init and Teardown (every eighth k of three fixtures) use only the three rows below.
    static const std::vector<AcceptedLeak> v = {
        // A6-7: the Architecture is built, and its spec read, from raw pointers
        { "A6-7 spec XML", { "ghidra::xml_parse(" }, true },                          // the XML reader's parse stack (bison values)
        { "A6-7 spec XML", { "ghidra::xml_tree(" }, true },
        { "A6-7 action tree", { "ghidra::ActionDatabase::universalAction(" }, true },   // the default action tree: raw Action and Rule pointers
        { "A6-7 compiler spec", { "ghidra::Architecture::parseCompilerConfig(" }, true },   // prototype models decoded from the cspec
        { "A6-7 compiler spec", { "ghidra::AddrSpaceManager::findAddJoin(" }, true },
        { "A6-7 context", { "ghidra::ContextInternal::FreeArray::reset(" }, true },
        // A6-30: the first translator of a process decodes the .sla (a cold failure only; a warm Init resets the translator).
        { "A6-30 first translator", { "ghidra::SleighBase::decode(" }, true },
    };
    return v;
}

struct LeakStack
{
    bool direct = false;
    std::vector<std::string> frames;   // "#3 0x... in ghidra::Foo::bar(...) /path/file.cc:12", innermost first
};

std::vector<LeakStack> ParseLeaks(const std::string& report)
{
    std::vector<LeakStack> leaks;
    bool in = false;
    for (const std::string& l : Lines(report))
    {
        if (l.compare(0, 11, "Direct leak") == 0 || l.compare(0, 13, "Indirect leak") == 0)
        {
            LeakStack s;
            s.direct = l[0] == 'D';
            leaks.push_back(s);
            in = true;
        }
        else if (in && l.find("#") != std::string::npos && l.find("#") < 8)
            leaks.back().frames.push_back(l);
        else if (in && l.find_first_not_of(" \t") == std::string::npos)
            in = false;
    }
    return leaks;
}

// The row of the first entry whose frames all occur, in order, in the stack; null if none.
const char* AcceptedRow(const LeakStack& s)
{
    for (const AcceptedLeak& a : AcceptedLeaks())
    {
        if (a.initOnly && !gFailedInInit) continue;
        size_t at = 0;
        bool all = true;
        for (const char* f : a.frames)
        {
            while (at < s.frames.size() && s.frames[at].find(f) == std::string::npos) ++at;
            if (at == s.frames.size())
            {
                all = false;
                break;
            }
            ++at;
        }
        if (all) return a.row;
    }
    return nullptr;
}

// Runs LeakSanitizer's check with its report taken into a file: leaks that are all accepted print one
// "LEAK-ACCEPTED" line per row, and any other leak puts the whole report on stderr as LeakSanitizer
// would, for the parent to classify.
int CheckedLeaks()
{
    std::fflush(nullptr);
    FILE* tmp = std::tmpfile();
    const int saved = dup(2);
    if (!tmp || saved < 0)
        return __lsan_do_recoverable_leak_check() ? kExitLeak : 0;
    dup2(fileno(tmp), 2);
    const int found = __lsan_do_recoverable_leak_check();
    dup2(saved, 2);
    close(saved);
    std::string report;
    if (found)
    {
        std::fflush(tmp);
        std::rewind(tmp);
        char buf[8192];
        for (size_t n; (n = std::fread(buf, 1, sizeof buf, tmp)) > 0;) report.append(buf, n);
    }
    std::fclose(tmp);
    if (!found) return 0;
    std::map<std::string, long> rows;
    long other = 0;
    const char* list = std::getenv("SE_FAULT_LEAKLIST");
    const bool survey = list && list[0] && list[0] != '0';
    for (const LeakStack& s : ParseLeaks(report))
    {
        if (survey)
        {
            // A survey of what leaks, for writing the accepted list: one line per leak, its innermost
            // engine and host frames (no allocator, no standard library), no verdict.
            std::string sig = s.direct ? "D" : "I";
            int taken = 0;
            for (const std::string& f : s.frames)
            {
                size_t b = f.find(" in ");
                if (b == std::string::npos) continue;
                std::string fn = f.substr(b + 4);
                const size_t paren = fn.find('(');
                if (paren != std::string::npos) fn.erase(paren);
                if (fn.compare(0, 5, "std::") == 0 || fn.find("std::") != std::string::npos || fn.compare(0, 8, "__gnu_cxx") == 0 ||
                    fn.compare(0, 8, "Allocate") == 0 || fn.compare(0, 12, "operator new") == 0 || fn.compare(0, 6, "malloc") == 0 ||
                    fn.find("DecompilerFaultTests") != std::string::npos)
                    continue;
                if (fn.find("RunOnce") != std::string::npos || fn.find("RunAllocCell") != std::string::npos || fn.find("ForkCell") != std::string::npos)
                    break;
                sig += " < " + fn;
                if (++taken == 5) break;
            }
            std::printf("LEAK-SIG %s\n", sig.c_str());
            continue;
        }
        const char* row = AcceptedRow(s);
        if (row) ++rows[row];
        else ++other;
    }
    if (survey)
    {
        std::fflush(stdout);
        return 0;
    }
    if (other || rows.empty())
    {
        if (write(2, report.data(), report.size()) < 0) {}
        return kExitLeak;
    }
    for (const auto& r : rows) std::printf("LEAK-ACCEPTED %s x%ld\n", r.first.c_str(), r.second);
    std::fflush(stdout);
    return 0;
}
#endif

int LeakCheck()
{
#ifdef SE_FAULT_ASAN
    // SE_FAULT_LEAKS=0 skips the per-cell leak check (it dominates a sanitizer sweep's time, and a
    // sweep for memory errors does not need it; run with ASAN_OPTIONS=detect_leaks=0 too).
    const char* v = std::getenv("SE_FAULT_LEAKS");
    if (v && v[0] == '0') return 0;
    // _Exit skips the sanitizer's exit-time check, so make it now.
    return CheckedLeaks();
#endif
    return 0;
}

[[noreturn]] void LeaveDamaged()
{
    std::fflush(nullptr);
    const int leak = LeakCheck();
    std::fflush(nullptr);
    std::_Exit(dts::Failures() ? kExitInvariant : leak);
}

// The entry of the baseline log that allocation k falls in (-1: before the first), its ordinal among
// the entries of the same phase name, and the allocation's rank among those of its size in the entry.
struct PinCoords
{
    int entry = -1;
    int ord = 0;
    int nth = 0;
    long lo = 0;
    long hi = 0;
};

PinCoords CoordsOf(const Ctx& c, long k)
{
    PinCoords p;
    const Log& L = c.base.log;
    for (int e = 0; e < L.n; ++e)
        if (L.allocAt[e] <= k) p.entry = e;
    if (p.entry < 0) return p;
    for (int e = 0; e < p.entry; ++e)
        if (L.phase[e] == L.phase[p.entry]) ++p.ord;
    p.lo = L.allocAt[p.entry];
    p.hi = p.entry + 1 < L.n ? L.allocAt[p.entry + 1] : c.base.allocs;
    if (k < long(c.base.sizes.size()))
        for (long i = p.lo; i < k; ++i)
            if (c.base.sizes[size_t(i)] == c.base.sizes[size_t(k)]) ++p.nth;
    return p;
}

// ================================================================================================
// Family 1: allocation k fails
// ================================================================================================

// What a failed allocation at 'k' was: its class for the summary.
// 'cancelAt' >= 0 adds a cancel raised at that allocation, so the failure may fall in the unwinding
// of a CancelError (the cancel-oom family).
int RunAllocCell(Ctx& c, long k, long cancelAt = -1)
{
    dts::Failures() = 0;
    gMustNotExit = true;   // until the next request shows the engine is fine
    std::atexit(OnExit);
    gProbe.Reset();
    std::atomic<bool> flag(false);
    if (cancelAt >= 0) gProbe.cancelFlag = &flag;
    const long refsBefore = c.snap.use_count();
    bool escaped = false;
    DecompileResult r;
    if (fa::gTrace && cancelAt < 0 && k < long(c.base.sizes.size()))
    {
        // How a pin for this allocation is written (kPins).
        const PinCoords pc = CoordsOf(c, k);
        std::printf("PIN-AT %s k=%ld: { \"%s\", \"%s\", %d, %u, %d, ... }\n", c.fix->name, k, c.fix->name,
                    pc.entry < 0 ? "?" : PhaseName(c.base.log.phase[pc.entry]), pc.ord, c.base.sizes[size_t(k)], pc.nth);
        std::fflush(stdout);
    }
    fa::Arm(k, cancelAt);
    try
    {
        r = RunOnce(c, cancelAt >= 0 ? &flag : nullptr);
    }
    catch (...)
    {
        escaped = true;
    }
    fa::Disarm();
    const bool fired = fa::gFired.load();
    const bool nothrow = fa::gFiredNothrow.load();
    const long size = fa::gFiredSize.load();
    const bool inEngine = gProbe.engineEntered.load();
    const long refsAfter = c.snap.use_count();
    const Log log = gProbe.Snapshot();
    // Where the failure fell, from the baseline log.
    const char* where = "before the engine";
    for (int i = 0; i < c.base.log.n; ++i)
        if (c.base.log.allocAt[i] <= k) where = PhaseName(c.base.log.phase[i]);
    const bool inRefRun = [&] {
        int last = -1;
        for (int i = 0; i < c.base.log.n; ++i)
            if (c.base.log.allocAt[i] <= k) last = i;
        return last >= 0 && c.base.log.inRef[last];
    }();

#ifdef SE_FAULT_ASAN
    // Before the first Flow entry of a pass is Architecture construction and the register loop (A6-15).
    gFailedInInit = std::string(where) == "before the engine" || std::string(where) == "init";
#endif
    std::string cls;
    Expect(!escaped, "no exception escapes Decompile");
    // Allocation k of this run is allocation k of the baseline, at the size the baseline recorded (A6-24): the cells, the
    // pins' census and `--sites` all index the baseline. Before a cancel the run is the same up to k; after one it need not be.
    if (fired && cancelAt < 0 && k < long(c.base.sizes.size()))
        Expect(size == long(c.base.sizes[size_t(k)]), "the failed allocation is the baseline's: its size is the one the baseline recorded",
               "allocation " + std::to_string(k) + " asked for " + std::to_string(size) + " bytes, the baseline " +
                   std::to_string(c.base.sizes[size_t(k)]));
    if (cancelAt >= 0) gProbe.cancelFlag = nullptr;
    if (cancelAt >= 0 && r.status == DecompileStatus::Cancelled)
    {
        // The run was stopped before the failure was reached, or the failure was absorbed on the
        // way out; either way a stop is a stop.
        cls = fired ? "cancelled-despite-failure" : "cancelled-before-failure";
        Expect(r.lines.empty(), "a cancelled result has no lines");
    }
    else if (!fired)
    {
        cls = "no-failure";
        Expect(cancelAt >= 0 || k >= c.base.allocs, "the allocation count is deterministic: failure k < N always fires",
               "k=" + std::to_string(k) + " N=" + std::to_string(c.base.allocs));
        if (cancelAt >= 0 && flag.load())
            Expect(r.status == DecompileStatus::Ok, "a cancel raised too late to be honoured, with no failure, completes");   // the reference run's cut is a note, not the baseline's text
        else
            Expect(r.status == DecompileStatus::Ok && Signature(r) == c.base.sig, "a run with no failure injected equals the baseline");
    }
    else if (r.status == DecompileStatus::Failed && Has(r.error, "out of memory"))
    {
        cls = inEngine ? (inRefRun ? "failed-in-reference-run" : "failed-in-engine") : "failed-before-engine";
        Expect(r.lines.empty(), "a failed result has no lines");
    }
    else if (r.status == DecompileStatus::Ok)
    {
        // The failure was absorbed. A nothrow allocation reports by returning null and the library
        // copes; a throwing one that ends as Ok was swallowed by a catch block.
        const bool same = Signature(r) == c.base.sig;
        cls = nothrow ? "ok-nothrow-fallback" : "ok-swallowed-bad_alloc";
        Expect(same, "an absorbed failure leaves the output unchanged", cls + "; " + FirstDiff(c.base.sig, Signature(r)));
        Expect(nothrow, "a throwing allocation failure is reported, not swallowed", std::string("allocation ") + std::to_string(k) + " (" +
                                                                                      std::to_string(size) + " bytes) in " + where);
    }
    else
    {
        cls = "wrong-status";
        Expect(false, "an allocation failure ends Failed(out of memory) or Ok",
               "status " + std::to_string(int(r.status)) + " '" + r.error + "' in " + where);
    }
    Expect(refsAfter == refsBefore, "the snapshot's reference count returns to its value before the run",
           std::to_string(refsBefore) + " -> " + std::to_string(refsAfter));

    // The next request.
    const DecompileResult after = RunOnce(c);
    const bool damagedExpected = fired && r.status == DecompileStatus::Failed && inEngine;
    if (damagedExpected)
    {
        Expect(after.status == DecompileStatus::Unavailable && Has(after.error, "out of memory"),
               "after a failure inside the engine, the next request is Unavailable (out of memory)",
               "status " + std::to_string(int(after.status)) + " '" + after.error + "'");
        const std::vector<std::string> listing = c.engine->Disassemble(c.snap, dts::kFixture1Caller, 2);
        Expect(listing.size() == 1 && Has(listing[0], "unavailable"), "Disassemble refuses on a damaged engine");
    }
    else
    {
        Expect(after.status == DecompileStatus::Ok && Signature(after) == c.base.sig,
               "after a failure outside the engine (or none), the next request is correct",
               "status " + std::to_string(int(after.status)) + " '" + after.error + "'");
    }
    const bool damaged = after.status == DecompileStatus::Unavailable;
    gMustNotExit = damaged;
    std::printf("CELL %s %s k=%ld cancelAt=%ld in=%s class=%s inEngine=%d size=%ld %s next=%d refs=%ld->%ld\n",
                cancelAt >= 0 ? "cancel-oom" : "alloc", c.fix->name, k, cancelAt, where, cls.c_str(), inEngine ? 1 : 0, size,
                nothrow ? "nothrow" : "throwing", int(after.status), refsBefore, refsAfter);
    (void)log;
    if (!damaged)
    {
        // An undamaged engine leaves the ordinary way: static destruction of the translator included.
        std::fflush(nullptr);
        return dts::Failures() ? kExitInvariant : 0;
    }
    if (ExitStatic())
    {
        // The application's way out after an engine failure: the runner's destructor shuts the
        // translator registry down and the process then runs its static destruction. Whatever the
        // failure left behind must not crash that (patches 0002 and 0003 are the two known cases).
        const int leak = LeakCheck();
        if (leak && !dts::Failures()) ++dts::Failures();
        gMustNotExit = false;
        ghidra::SleighArchitecture::shutdown();
        std::fflush(nullptr);
        return dts::Failures() ? kExitInvariant : 0;
    }
    LeaveDamaged();
}

// ================================================================================================
// Family 2: the cancel flag goes up
// ================================================================================================

enum class CancelHow { AtEnter, AtAlloc, AtUs, AtBoundary };

int RunCancelCell(Ctx& c, CancelHow how, long param)
{
    dts::Failures() = 0;
    std::atexit(OnExit);
    gProbe.Reset();
    std::atomic<bool> flag(false);
    const long refsBefore = c.snap.use_count();
    unsigned allowed = 0;
    std::atomic<int> s0(-2), s1(-2);
    std::atomic<bool> done(false), stopThread(false);
    std::thread timer;
    gProbe.cancelFlag = &flag;
    if (how == CancelHow::AtEnter)
    {
        if (param < 0) flag = true;   // before the run
        gProbe.cancelAtEnter = int(param);
    }
    // AtBoundary: the flag goes up inside the hook the host calls immediately before its own check-point number 'param'
    // (A6-41), after the engine's last check of the phase that ends there and before the host's.
    if (how == CancelHow::AtBoundary) gProbe.cancelAtBoundary = int(param);
    fa::Arm(-1, how == CancelHow::AtAlloc ? param : -1);
    if (how == CancelHow::AtUs)
    {
        timer = std::thread([&] {
            const auto t0 = Clock::now();
            while (MsSince(t0) * 1000.0 < double(param) && !stopThread.load()) std::this_thread::sleep_for(std::chrono::microseconds(50));
            if (stopThread.load()) return;
            s0 = gProbe.enters.load() - 1;
            flag = true;
            s1 = done.load() ? c.base.log.n : gProbe.enters.load() - 1;
        });
    }
    bool escaped = false;
    DecompileResult r;
    try
    {
        r = RunOnce(c, &flag);
    }
    catch (...)
    {
        escaped = true;
    }
    done = true;
    const long endCount = fa::gCount.load();   // allocations made by the run, for the cancel's latency
    fa::Disarm();
    stopThread = true;
    if (timer.joinable()) timer.join();
    gProbe.cancelFlag = nullptr;
    gProbe.cancelAtEnter = -2;
    gProbe.cancelAtBoundary = -2;
    const Log log = gProbe.Snapshot();
    const bool raised = flag.load();

    int a = -2, b = -2;
    if (how == CancelHow::AtEnter)
        a = b = int(param);
    else if (how == CancelHow::AtAlloc || how == CancelHow::AtBoundary)
        a = b = gProbe.setOrdinal.load();
    else
    {
        a = s0.load();
        b = std::max(s1.load(), a);
    }
    std::string flagged;
    if (!raised || a == -2)
    {
        allowed = kOk;   // the run ended before the flag went up (a late timer, or an index past the run's end)
        flagged = "never raised";
    }
    else
    {
        // The oracle reads the baseline log (the cancelled run's is a prefix of it). A raise from another thread is read
        // against the counter, which runs behind the check-points (ExpectedThread); the other two families raise inside the
        // hook or the allocator, where the entry is exact.
        if (how == CancelHow::AtUs) allowed = ExpectedThread(c.base.log, a, b);
        else
        {
            allowed = a >= c.base.log.n ? kOk : ExpectedRange(c.base.log, std::max(a, -1), std::min(b, c.base.log.n - 1));
            if (b >= c.base.log.n) allowed |= kOk;
        }
        flagged = a < 0 ? "before the run" : std::string("in ") + PhaseName(c.base.log.phase[std::min(a, c.base.log.n - 1)]) +
                                                 (c.base.log.inRef[std::min(a, c.base.log.n - 1)] ? "(reference)" : "") +
                                                 "#" + std::to_string(a) + (b != a ? "..#" + std::to_string(b) : std::string());
    }
    const unsigned got = Observed(r);
    // The baseline has this check-point, so the run must reach it and raise there: a hook that is no longer called would
    // otherwise read as "never raised" and pass.
    if (how == CancelHow::AtBoundary && param < c.base.log.nb)
        Expect(raised, "the host's check-point the baseline made is reached", "check-point #" + std::to_string(param) + " of " + std::to_string(c.base.log.nb));
    // Where the flag went up, as a class name: the phase (the reference run's apart; the first pass's
    // Actions apart from the printing pass's, which have different check-points).
    std::string label = !raised || a == -2 ? "never" : a < 0 ? "before-run" : "";
    if (label.empty())
    {
        const int i = std::min(a, c.base.log.n - 1);
        label = PhaseName(c.base.log.phase[i]);
        if (c.base.log.inRef[i]) label += "/ref";
        else if (c.base.log.phase[i] == DecompilePhase::Actions)
            label += i + 1 < c.base.log.n && c.base.log.phase[i + 1] == DecompilePhase::Print ? "/print-pass" : "/first-pass";
    }
    // Allocations between the flag going up and the engine stopping (the thread family has no index).
    const long latency = how == CancelHow::AtAlloc && r.status == DecompileStatus::Cancelled ? endCount - param : -1;
    Expect(!escaped, "no exception escapes Decompile");
    Expect(got != 0 && (got & allowed), "the cancel is honoured where the design says",
           "raised " + flagged + ": expected " + MaskText(allowed) + "but got status " + std::to_string(int(r.status)) +
               " cancelledIn " + PhaseName(r.cancelledIn) + " '" + r.error + "'");
    // The window, deterministically (A6-35): a raise in the hook of entry p is what a thread sees as a raise in entry p - 1
    // when it lands after the check-point before p and before p's hook. Attribute this one so, and require the outcome to be
    // in what the thread family allows for that attribution; "need" marks the entries where the outcome is not what a raise
    // in p - 1 gets without the widening, i.e. where the window exists (the driver requires that some do).
    const char* window = "-";
    if (how == CancelHow::AtEnter && param >= 0 && param < c.base.log.n && raised)
    {
        const int before = int(param) - 1;
        const unsigned plain = ExpectedRange(c.base.log, before, before);
        const unsigned seen = ExpectedThread(c.base.log, before, before);
        Expect(got != 0 && (got & seen), "a raise the thread family would attribute to the entry before is honoured where it allows",
               "raised in the hook of #" + std::to_string(param) + ", read as #" + std::to_string(before) + ": expected " + MaskText(seen) +
                   "but got status " + std::to_string(int(r.status)) + " cancelledIn " + PhaseName(r.cancelledIn));
        window = (got & plain) ? "plain" : "need";
    }
    if (r.status == DecompileStatus::Cancelled)
    {
        Expect(r.lines.empty(), "a cancelled result has no lines");
        Expect(r.error == "cancelled", "a cancelled result says cancelled", r.error);
    }
    Expect(c.snap.use_count() == refsBefore, "the snapshot's reference count returns to its value before the run");

    // The same engine, uncancelled: exactly the baseline.
    gProbe.Reset();
    const DecompileResult after = RunOnce(c);
    Expect(after.status == DecompileStatus::Ok && Signature(after) == c.base.sig,
           "the same Sh2Decompiler then decompiles correctly",
           "status " + std::to_string(int(after.status)) + " '" + after.error + "'");
    // And with a flag that is down but passed in.
    std::atomic<bool> down(false);
    const DecompileResult after2 = RunOnce(c, &down);
    Expect(after2.status == DecompileStatus::Ok && Signature(after2) == c.base.sig, "a lowered flag does not cancel");

    const char* hows[] = { "enter", "alloc", "us", "bound" };
    std::printf("CELL cancel-%s %s p=%ld raised=%s got=%s allowed=%s class=%s>%s lat=%ld window=%s\n", hows[int(how)], c.fix->name, param,
                flagged.c_str(), MaskText(got).c_str(), MaskText(allowed).c_str(), label.c_str(), MaskTag(got).c_str(), latency, window);
    (void)log;
    if (after.status == DecompileStatus::Unavailable)
    {
        ++dts::Failures();   // a cancel never damages the engine
        gMustNotExit = true;
        LeaveDamaged();
    }
    std::fflush(nullptr);
    return dts::Failures() ? kExitInvariant : 0;
}

// ================================================================================================
// Family 3: allocation k of a Disassemble fails
// ================================================================================================

// A listing of three instructions of fixture1's caller: it builds an Architecture like a decompilation does.
const int kDisasmCount = 3;

struct DisasmBase
{
    std::vector<std::string> lines;
    long allocs = 0;
};

std::vector<std::string> DisasmOnce(Ctx& c, bool* stopped = nullptr)
{
    return c.engine->Disassemble(c.snap, dts::kFixture1Caller, kDisasmCount, stopped);
}

// Disassemble makes these allocations before anything else (the marker, then the room for the listing and the marker),
// so that a failure after them cannot take the marker line away: it is a move into room already made. A failure in
// them leaves no line, and the `stopped` flag is then the whole report.
const long kDisasmMarkerAllocs = 2;

bool MeasureDisasm(Ctx& c, DisasmBase& b)
{
    long prev = -1;
    for (int i = 0; i < 6; ++i)
    {
        gProbe.Reset();
        fa::Arm(-1);
        const std::vector<std::string> lines = DisasmOnce(c);
        const long n = fa::gCount.load();
        fa::Disarm();
        if (lines.size() != size_t(kDisasmCount))
        {
            std::printf("the listing of %d instructions has %zu lines\n", kDisasmCount, lines.size());
            return false;
        }
        if (n == prev)
        {
            b.lines = lines;
            b.allocs = n;
            std::printf("disassembly: %ld allocations, %zu lines\n", n, lines.size());
            return true;
        }
        prev = n;
    }
    std::printf("the allocation count of a disassembly does not settle\n");
    return false;
}

// Disassemble never throws and never returns a listing that stops short without saying so (A6-16, A6-23): a failed
// allocation sets `stopped`, which is a bool the caller owns (nothing to allocate, so it holds under sustained
// exhaustion: "disasm-sticky", every allocation from k on failing), and the listing ends with a "<stopped: ..." line
// whenever the room for it was made; the engine is then refused (Unavailable, and Disassemble refuses), and a
// failure the library absorbed leaves the listing as it was.
int RunDisasmCell(Ctx& c, const DisasmBase& b, long k)
{
    dts::Failures() = 0;
    gMustNotExit = true;
    std::atexit(OnExit);
    gProbe.Reset();
    bool escaped = false;
    std::vector<std::string> lines;
    bool stopped = false;
    fa::Arm(k);
    try
    {
        lines = DisasmOnce(c, &stopped);
    }
    catch (...)
    {
        escaped = true;
    }
    fa::Disarm();
#ifdef SE_FAULT_ASAN
    gFailedInInit = true;   // a listing only builds the Architecture: all of it is what Init is
#endif
    const bool fired = fa::gFired.load();
    Expect(!escaped, "no exception escapes Disassemble");
    const bool same = lines == b.lines;
    const bool said = stopped;
    const bool line = !lines.empty() && lines.back().compare(0, 9, "<stopped:") == 0;
    std::string cls;
    if (!fired)
    {
        cls = "no-failure";
        Expect(same && !stopped, "a listing with no failure injected equals the baseline");
    }
    else if (said && k < kDisasmMarkerAllocs)
    {
        // The marker or its room could not be made, before the engine was touched: the listing is cut short before it
        // began, says so when a line can be made (not under sustained exhaustion), and the engine is not damaged (A6-30).
        cls = "stopped-before-engine";
        Expect(lines.empty() || (lines.size() == 1 && lines[0].compare(0, 16, "<out of memory w") == 0),
               "a listing stopped before the engine is empty or one line saying so", lines.empty() ? std::string("(empty)") : lines.back());
    }
    else if (said)
    {
        cls = "stopped";
        // The marker needs no allocation once its room was made; the flag needs none at all.
        Expect(line, "a stopped listing ends with the marker line once its room was made",
               lines.empty() ? std::string("(empty)") : lines.back());
    }
    else
    {
        cls = "absorbed";
        Expect(same && !line, "a failure that does not end the listing leaves it unchanged, and `stopped` clear",
               lines.empty() ? std::string("(empty)") : lines.back());
    }
    const DecompileResult after = RunOnce(c);
    const std::vector<std::string> again = DisasmOnce(c);
    const bool damaged = after.status == DecompileStatus::Unavailable;
    if (said && k < kDisasmMarkerAllocs)
    {
        Expect(after.status == DecompileStatus::Ok && Signature(after) == c.base.sig, "after a listing stopped before the engine the next decompilation is correct");
        Expect(again == b.lines, "after a listing stopped before the engine the next listing is correct");
    }
    else if (said)
    {
        Expect(damaged && Has(after.error, "out of memory"), "after a stopped listing the next decompilation is Unavailable (out of memory)",
               "status " + std::to_string(int(after.status)) + " '" + after.error + "'");
        Expect(again.size() == 1 && Has(again[0], "unavailable"), "Disassemble refuses on a damaged engine");
    }
    else
    {
        Expect(after.status == DecompileStatus::Ok && Signature(after) == c.base.sig, "after no failure the next decompilation is correct");
        Expect(again == b.lines, "after no failure the next listing is correct");
    }
    std::printf("CELL disasm %s k=%ld class=%s next=%d\n", c.fix->name, k, cls.c_str(), int(after.status));
    gMustNotExit = damaged;
    std::fflush(nullptr);
    if (!damaged) return dts::Failures() ? kExitInvariant : 0;
    LeaveDamaged();
}

std::vector<long> DisasmCells(const DisasmBase& b, bool full)
{
    std::set<long> s;
    const long N = b.allocs;
    if (full)
    {
        for (long k = 0; k < N; ++k) s.insert(k);
    }
    else
    {
        for (long k = 0; k < 12; ++k) s.insert(k);
        for (double k = 12; k < double(N); k *= 1.35) s.insert(long(k));
        for (long d = 0; d < 6; ++d) s.insert(N - 1 - d);
    }
    s.insert(N);
    s.insert(N + 5);
    return std::vector<long>(s.begin(), s.end());
}

// ================================================================================================
// Family 4: cold start -- allocation k fails in the first request of a process (A6-30)
// ================================================================================================
//
// Every cell above forks from an engine that has already decompiled twice, so the process-global start-up
// (the spec bundle, the library's registries and spec path, the first translator) is never inside a failure.
// A cold cell forks from a parent that has not touched the engine, and fails allocation k of the *first*
// request: a Decompile ("cold") or a Disassemble ("cold-disasm"). The allocation falls in one of three places,
// and each has its own contract:
//
//   before any engine global    (request preparation, spec materialisation: filesystem work). The request
//                               is Failed("out of memory") -- or, for a listing, says it stopped -- and the engine is
//                               NOT damaged: the next request starts again and is correct.
//   inside startDecompilerLibrary   (the attribute and element ids, the capability lists, the spec path: appended
//                               to, never undone). The library cannot be started twice and cannot be finished, so the
//                               process refuses the engine, and the first request already says why and that a restart
//                               is needed; so does every later one, Decompile and Disassemble alike. It is never
//                               Failed("out of memory") followed by "No print languages registered", and never
//                               Unavailable with an empty explanation.
//   inside the engine           (the first translator, the Architecture): as in the warm families, Failed and then
//                               Unavailable (out of memory).
//
// Run twice per k: with the spec bundle already extracted in the config directory (the verify-only path) and with
// an empty one (extraction), because the two have different allocations before the library.
struct ColdMode
{
    long allocs = 0;        // allocations of the first request, nothing failing
    long libraryEnd = 0;    // allocation count when the first translator is about to be built (the library is up)
    // Where the first request's allocations fall, from the stacks of a census run: the spec materialisation and
    // startDecompilerLibrary, as [lo, hi] allocation indices (-1: not found, then every allocation up to libraryEnd is a cell).
    long matLo = -1, matHi = -1, libLo = -1, libHi = -1;
    std::string sig;        // the first request's result (Decompile) ...
    std::vector<std::string> listing;   // ... or its listing (Disassemble)
};

struct ColdCtx
{
    std::string scratch;
    std::string cachedConfig;
    const Fix* fix = nullptr;
    std::shared_ptr<const MemorySnapshot> snap;
    DecompileRequest req;
    ColdMode mode[2];   // 0: bundle already extracted, 1: empty config directory
    bool disasmFirst = false;
};

long gLibraryEndCount = -1;
const long kColdCensus = 8192;   // the first allocations whose stacks the baseline keeps

// The first request of the process: decompile, or list kDisasmCount instructions.
const char* const kColdRestartText = "restart Saturn Explorer";

std::string ColdConfig(const ColdCtx& c, int mode)
{
    return mode == 0 ? c.cachedConfig : dts::Join(c.scratch, "cfg" + std::to_string(long(getpid())));
}

#ifdef SE_FAULT_BACKTRACE
bool FiredIn(const char* frame)
{
    for (const std::string& n : fa::Names(fa::gFiredStack))
        if (n.find(frame) != std::string::npos) return true;
    return false;
}
#endif

// The steps of startDecompilerLibrary a failure is reported by (the sweep must reach each: a step that allocates
// and is not covered is a step whose failure nobody has seen).
const char* const kLibrarySteps[] = { "AttributeId::initialize", "ElementId::initialize", "CapabilityPoint::initializeAll" };

bool WriteBaseFile(const std::string& file, const ColdMode& m)
{
    FILE* f = std::fopen(file.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "%ld %ld %ld %ld %ld %ld\n", m.allocs, m.libraryEnd, m.matLo, m.matHi, m.libLo, m.libHi);
    std::fprintf(f, "%zu\n", m.listing.size());
    for (const std::string& l : m.listing) std::fprintf(f, "%s\n", l.c_str());
    std::fputs(m.sig.c_str(), f);
    std::fclose(f);
    return true;
}

bool ReadBaseFile(const std::string& file, ColdMode& m)
{
    std::istringstream in(ReadFile(file));
    size_t n = 0;
    if (!(in >> m.allocs >> m.libraryEnd >> m.matLo >> m.matHi >> m.libLo >> m.libHi >> n)) return false;
    std::string line;
    std::getline(in, line);
    for (size_t i = 0; i < n; ++i)
    {
        std::getline(in, line);
        m.listing.push_back(line);
    }
    std::ostringstream rest;
    rest << in.rdbuf();
    m.sig = rest.str();
    return true;
}

// The two requests a cell makes, by family.
DecompileResult ColdDecompile(Sh2Decompiler& e, const ColdCtx& c) { return e.Decompile(c.snap, c.req, nullptr); }
// ... a listing's `stopped` flag is the allocation-free report that it is not the whole listing (A6-23), so every request
// passes one and the cell reads it (A6-31): this family used to pass none, and could not see a listing returned empty with
// the flag clear.
std::vector<std::string> ColdDisasm(Sh2Decompiler& e, const ColdCtx& c, bool* stopped = nullptr)
{
    return e.Disassemble(c.snap, dts::kFixture1Caller, kDisasmCount, stopped);
}

bool PrepareCold(ColdCtx& c, const std::string& scratch, const std::string& fixture, bool disasmFirst)
{
    c.fix = FindFix(fixture);
    if (!c.fix)
    {
        std::printf("unknown fixture '%s'\n", fixture.c_str());
        return false;
    }
    c.scratch = scratch;
    c.disasmFirst = disasmFirst;
    sfe::MakeDirectory(scratch);
    c.cachedConfig = dts::Join(scratch, "config");
    sfe::MakeDirectory(c.cachedConfig);
    c.snap = BuildSnapshot();
    c.req = MakeRequest(*c.fix);
    // The extracted bundle for mode 0: filesystem work only, no engine global is written.
    const sfe::decomp::SpecBundleResult b = sfe::decomp::MaterialiseSh2SpecBundle(c.cachedConfig);
    if (!b.ok)
    {
        std::printf("cannot extract the spec bundle: %s\n", b.error.c_str());
        return false;
    }
    InstallHooks();
    SaturnArchitecture::SetBuildTranslatorHookForTests([] {
        if (gLibraryEndCount < 0) gLibraryEndCount = fa::gCount.load();
    });
    // The baselines: each in a process of its own, because the first request is what is measured.
    for (int mode = 0; mode < 2; ++mode)
    {
        const std::string file = dts::Join(scratch, "base" + std::to_string(mode) + ".txt");
        auto body = [&]() -> int {
            const std::string cfg = ColdConfig(c, mode);
            sfe::MakeDirectory(cfg);
            std::unique_ptr<Sh2Decompiler> e(new Sh2Decompiler(cfg));
            ColdMode m;
            gProbe.Reset();
            gLibraryEndCount = -1;
#ifdef SE_FAULT_BACKTRACE
            // The stack of each of the first allocations, to find where the materialisation and the library start-up are.
            static std::vector<uint8_t> want(kColdCensus, 1);
            fa::gCensusWant = want.data();
            fa::gCensusWantN = long(want.size());
            fa::gCensusStack = static_cast<fa::Stack*>(std::malloc(sizeof(fa::Stack) * size_t(fa::kCensusCap)));
            fa::gCensusK = static_cast<long*>(std::malloc(sizeof(long) * size_t(fa::kCensusCap)));
            fa::gCensusSize = static_cast<uint32_t*>(std::malloc(sizeof(uint32_t) * size_t(fa::kCensusCap)));
            fa::gCensusN = 0;
            fa::gCensus = true;
#endif
            fa::Arm(-1);
            if (disasmFirst)
            {
                bool stopped = true;
                m.listing = ColdDisasm(*e, c, &stopped);
                m.allocs = fa::gCount.load();
                if (stopped) return 7;   // nothing failing: the listing is whole
                fa::Disarm();
                m.libraryEnd = gLibraryEndCount;
                const DecompileResult r = ColdDecompile(*e, c);
                m.sig = Signature(r);
                if (r.status != DecompileStatus::Ok) return 3;
                if (m.listing.size() != size_t(kDisasmCount)) return 4;
            }
            else
            {
                const DecompileResult r = ColdDecompile(*e, c);
                m.allocs = fa::gCount.load();
                fa::Disarm();
                m.libraryEnd = gLibraryEndCount;
                m.sig = Signature(r);
                if (r.status != DecompileStatus::Ok) return 3;
            }
            if (m.libraryEnd < 0) return 5;
#ifdef SE_FAULT_BACKTRACE
            fa::gCensus = false;
            for (long j = 0; j < fa::gCensusN; ++j)
            {
                const long k = fa::gCensusK[j];
                if (k >= m.libraryEnd) continue;
                for (const std::string& n : fa::Names(fa::gCensusStack[j]))
                {
                    if (n.find("startDecompilerLibrary") != std::string::npos)
                    {
                        m.libLo = m.libLo < 0 ? k : std::min(m.libLo, k);
                        m.libHi = std::max(m.libHi, k);
                    }
                    if (n.find("MaterialiseSh2SpecBundle") != std::string::npos)
                    {
                        m.matLo = m.matLo < 0 ? k : std::min(m.matLo, k);
                        m.matHi = std::max(m.matHi, k);
                    }
                }
            }
#endif
            if (mode == 1) dts::RemoveTree(cfg);
            return WriteBaseFile(file, m) ? 0 : 6;
        };
        const CellRun run = ForkCell(scratch, body, CellTimeout());
        if (!run.proc.exited || run.proc.code != 0 || !ReadBaseFile(file, c.mode[mode]))
        {
            std::printf("the cold baseline (%s config) failed: exit %d%s\n", mode ? "empty" : "extracted", run.proc.code,
                        run.proc.timedOut ? " (timed out)" : "");
            return false;
        }
        std::printf("cold %s, %s config: %ld allocations to the end of the first request, the library is up at %ld; "
                    "spec materialisation at %ld-%ld, startDecompilerLibrary at %ld-%ld\n",
                    disasmFirst ? "disasm" : "decompile", mode ? "empty" : "extracted", c.mode[mode].allocs, c.mode[mode].libraryEnd,
                    c.mode[mode].matLo, c.mode[mode].matHi, c.mode[mode].libLo, c.mode[mode].libHi);
    }
    return true;
}

// Cells are k * 2 + mode.
std::vector<long> ColdCells(const ColdCtx& c, bool full)
{
    std::set<long> s;
    for (int mode = 0; mode < 2; ++mode)
    {
        const ColdMode& m = c.mode[mode];
        const long N = m.allocs;
        auto add = [&](long k) {
            if (k >= 0) s.insert(k * 2 + mode);
        };
        // The spec materialisation and the library's start-up are the point: every allocation in them, and a margin
        // around. (The full run, or a build that cannot say where they are: every allocation up to the first translator.)
        const bool located = m.libLo >= 0 && m.matLo >= 0;
        if (full || !located)
            for (long k = 0; k < m.libraryEnd + 8; ++k) add(k);
        else
        {
            // The materialisation is at the front, the library follows it. Under sustained exhaustion (the default subset's
            // sticky families) the materialisation's allocations are every third one and the library's every twelfth: each
            // step of the library is hundreds long, and the dense run is the plain family's (A6-19: the cold families are the
            // longest jobs of the sanitizer run, and a cell costs the same however early it fails: the second request runs
            // the engine whole).
            const char* stickyEnv = std::getenv("SE_FAULT_STICKY");   // asked for by environment: every allocation, as the plain family
            const bool thin = fa::gSticky.load() && !(stickyEnv && *stickyEnv && std::string(stickyEnv) != "0");
            for (long k = 0; k <= m.libHi + 8; ++k)
                if (!thin || k < 12 || k > m.libHi - 16 || (k <= m.matHi + 8 ? k % 3 == 0 : (k - m.libLo) % 12 == 0)) add(k);
            // ...and between the library and the first translator (the Architecture's construction): spread out.
            for (double k = double(m.libHi) + 8; k < double(m.libraryEnd) + 8; k = std::max(k + 1, k * 1.25)) add(long(k));
        }
        // The first translator and the rest of the run.
        const double ratio = full ? 1.05 : 1.7;
        for (double k = double(m.libraryEnd) + 8; k < double(N); k = std::max(k + 1, k * ratio)) add(long(k));
        for (long d = 1; d < 4; ++d) add(N - d);
        add(N);       // nothing fails: the run must equal the baseline
        add(N + 5);
    }
    return std::vector<long>(s.begin(), s.end());
}

// One cell: the first request of this process fails its allocation k.
int RunColdCell(ColdCtx& c, long cell)
{
    const long k = cell / 2;
    const int mode = int(cell % 2);
    const ColdMode& base = c.mode[mode];
    dts::Failures() = 0;
    gMustNotExit = true;
    std::atexit(OnExit);
    const std::string cfg = ColdConfig(c, mode);
    sfe::MakeDirectory(cfg);
    std::unique_ptr<Sh2Decompiler> engine(new Sh2Decompiler(cfg));
    const std::string want = c.disasmFirst ? std::string() : base.sig;

    gProbe.Reset();
    gLibraryEndCount = -1;
    bool escaped = false;
    bool stoppedFlag = false;
    DecompileResult r;
    std::vector<std::string> lines;
    fa::Arm(k);
    try
    {
        if (c.disasmFirst) lines = ColdDisasm(*engine, c, &stoppedFlag);
        else r = ColdDecompile(*engine, c);
    }
    catch (...)
    {
        escaped = true;
    }
    fa::Disarm();
    const bool fired = fa::gFired.load();
    const bool nothrow = fa::gFiredNothrow.load();
    const bool inEngine = gProbe.engineEntered.load();
    bool inLibrary = false;
    bool inMaterialise = false;
    bool siteKnown = false;
    const char* step = "-";
#ifdef SE_FAULT_BACKTRACE
    if (fired)
    {
        siteKnown = true;
        inLibrary = FiredIn("startDecompilerLibrary");
        inMaterialise = FiredIn("MaterialiseSh2SpecBundle");
        for (const char* st : kLibrarySteps)
            if (step[0] == '-' && FiredIn(st)) step = st;
    }
#endif
#ifdef SE_FAULT_ASAN
    {
        // Init's leaks (the Architecture's spec XML, the action tree, the first translator's decode) are the accepted ones
        // (A6-7, A6-30); anything the failure leaves in another phase is not. The phase is the one the failing allocation
        // fell in, by this run's own log (the second pass's Init follows a first pass's Flow).
        const Log log = gProbe.Snapshot();
        gFailedInInit = true;
        if (fired)
            for (int i = 0; i < log.n; ++i)
                if (log.allocAt[i] <= k) gFailedInInit = log.phase[i] == DecompilePhase::Init;
    }
#endif

    Expect(!escaped, "no exception escapes the first request");
    // What the first request was, in the vocabulary of the contract above.
    enum Kind { kNone, kOk, kFailedBefore, kFailedEngine, kRefusedLibrary, kOther } kind = kOther;
    std::string cls, detail;
    if (c.disasmFirst)
    {
        const bool same = lines == base.listing;
        const std::string head = lines.empty() ? std::string() : lines[0];
        if (same) kind = kOk;
        else if (lines.size() == 1 && head.compare(0, 24, "<Decompiler unavailable:") == 0) kind = kRefusedLibrary;
        else if (lines.size() == 1 && head.compare(0, 16, "<out of memory w") == 0) kind = kFailedBefore;
        else if (!lines.empty() && lines.back().compare(0, 9, "<stopped:") == 0) kind = kFailedEngine;
        else if (lines.empty() && stoppedFlag && siteKnown) kind = inLibrary ? kRefusedLibrary : kFailedBefore;   // sustained exhaustion: no line could be made
        detail = head.empty() ? std::string("(empty listing, stopped=") + (stoppedFlag ? "true" : "false") + ")" : head;
        // A listing that is not the whole listing says so in the flag, whether or not a line could be made (A6-31). A refusal
        // line is its own report.
        Expect(same ? !stoppedFlag : (stoppedFlag || kind == kRefusedLibrary),
               "a listing that is not the whole one has `stopped` set (or is the engine's refusal line)", detail);
        Expect(!(fired && lines.empty()) || (stoppedFlag && fa::gSticky.load()),
               "an empty listing is only returned for sustained exhaustion, with `stopped` set", detail);
    }
    else
    {
        if (r.status == DecompileStatus::Ok) kind = Signature(r) == want ? kOk : kOther;
        else if (r.status == DecompileStatus::Unavailable) kind = kRefusedLibrary;
        else if (r.status == DecompileStatus::Failed && Has(r.error, "out of memory")) kind = inEngine ? kFailedEngine : kFailedBefore;
        detail = std::string("status ") + std::to_string(int(r.status)) + " '" + r.error + "'";
        Expect(r.status == DecompileStatus::Ok || r.lines.empty(), "a request that did not complete has no lines");
    }
    switch (kind)
    {
    case kOk:
        cls = fired ? (nothrow ? "ok-nothrow-fallback" : "ok-swallowed-bad_alloc") : "no-failure";
        Expect(!fired || nothrow, "a throwing allocation failure is reported, not swallowed", "allocation " + std::to_string(k));
        Expect(fired || k >= base.allocs, "the allocation count is deterministic: failure k < N always fires",
               "k=" + std::to_string(k) + " N=" + std::to_string(base.allocs));
        break;
    case kFailedBefore: cls = "failed-before-engine"; break;
    case kFailedEngine: cls = "failed-in-engine"; break;
    case kRefusedLibrary: cls = "library-interrupted"; break;
    default:
        cls = "wrong-status";
        Expect(false, "the first request ends Ok, out of memory, or refused with a restart reason", detail);
    }
    if (fired && kind != kOk && kind != kOther)
    {
        Expect(!(kind == kFailedBefore && inLibrary), "a failure inside the library's start-up is not reported as a plain out-of-memory (the next request would meet half-built global tables)", detail);
        Expect(!(kind != kRefusedLibrary && inLibrary), "a failure inside startDecompilerLibrary refuses the engine", detail);
        Expect(!(kind == kRefusedLibrary && siteKnown && !inLibrary), "only a failure inside startDecompilerLibrary refuses the engine for good", detail);
        // The spec materialisation is filesystem work that touches no engine global: its failure costs this request and nothing
        // after it -- for a listing as for a decompilation (the listing used to mark the engine damaged for it).
        Expect(!(inMaterialise && kind != kFailedBefore), "a failure while the language files are prepared is transient: the engine is neither damaged nor refused", detail);
    }

    // A failure while the spec bundle was staged leaves nothing behind (A6-33): the config directory, listed before the second
    // request (which would clear a same-pid leftover itself), has no '<hash>.tmp-<pid>' in it. Under sustained exhaustion too
    // (A6-37): the guard's removal needs no allocation (the staged paths are made before the directory is).
    std::string stray;
    if (mode == 1 && fired)
    {
        std::vector<std::string> names;
        sfe::ListDirectory(dts::Join(cfg, "decompiler"), names);
        for (const std::string& n : names)
            if (n.find(".tmp-") != std::string::npos) stray += (stray.empty() ? "" : ", ") + n;
        Expect(stray.empty(), "a failed first request leaves no staging directory in the config directory", stray);
    }

    // The second request, and the other kind of request after it.
    gProbe.Reset();
    const DecompileResult dec = ColdDecompile(*engine, c);
    const int entersAfter = gProbe.enters.load();
    const std::vector<std::string> list = ColdDisasm(*engine, c);
    bool damaged = false;
    if (kind == kRefusedLibrary)
    {
        const std::string firstReason = c.disasmFirst ? (lines.empty() ? std::string() : lines[0]) : r.error;
        // A listing that could not even make its refusal line (sustained exhaustion: the line is an allocation) has the flag
        // as its whole report; a Decompile has the text from the first request on, made before the library's first global
        // write and moved into place (A6-32), so it needs no allocation either.
        const bool bareListing = c.disasmFirst && lines.empty();
        Expect(bareListing || !fired || !firstReason.empty(), "a refusal carries an explanation");
        Expect(bareListing || (Has(firstReason, "restart Saturn Explorer") && Has(firstReason, "did not finish starting")),
               "the refusal says the library did not finish starting and that a restart is needed", firstReason);
        Expect(dec.status == DecompileStatus::Unavailable && dec.lines.empty(), "the next Decompile is Unavailable", "status " + std::to_string(int(dec.status)) + " '" + dec.error + "'");
        Expect(Has(dec.error, "restart Saturn Explorer") && Has(dec.error, "did not finish starting"), "the next Decompile says why and what to do", dec.error);
        Expect(entersAfter == 0, "the refused request does not enter the engine", std::to_string(entersAfter) + " phase entries");
        Expect(list.size() == 1 && Has(list[0], "restart Saturn Explorer") && Has(list[0], "did not finish starting"),
               "Disassemble refuses with the same reason", list.empty() ? std::string("(empty)") : list[0]);
        Expect(c.disasmFirst || dec.error == r.error, "Decompile says the same thing the first request said", dec.error + " / " + r.error);
        damaged = true;
    }
    else if (kind == kFailedEngine)
    {
        Expect(dec.status == DecompileStatus::Unavailable && Has(dec.error, "out of memory"),
               "after a failure inside the engine, the next Decompile is Unavailable (out of memory)", "status " + std::to_string(int(dec.status)) + " '" + dec.error + "'");
        Expect(list.size() == 1 && Has(list[0], "unavailable"), "after a failure inside the engine, Disassemble refuses");
        damaged = true;
    }
    else
    {
        // Ok, or a failure before anything global was written: both requests are correct, the first of
        // them (after a failure) being the one that starts the library.
        Expect(dec.status == DecompileStatus::Ok && Signature(dec) == base.sig, "the next Decompile is correct",
               "status " + std::to_string(int(dec.status)) + " '" + dec.error + "'");
        Expect(c.disasmFirst ? list == base.listing : list.size() == size_t(kDisasmCount) && list[0].compare(0, 1, "<") != 0,
               "the next listing is correct", list.empty() ? std::string("(empty)") : list[0]);
    }
    std::printf("CELL %s %s k=%ld mode=%s class=%s inEngine=%d inLibrary=%d step=%s next=%d staging=%s\n", c.disasmFirst ? "cold-disasm" : "cold", c.fix->name, k,
                mode ? "empty-config" : "extracted-config", cls.c_str(), inEngine ? 1 : 0, siteKnown ? int(inLibrary) : -1, step, int(dec.status),
                stray.empty() ? "none" : "left");
    if (mode == 1) dts::RemoveTree(cfg);
    gMustNotExit = damaged;
    std::fflush(nullptr);
    if (!damaged) return dts::Failures() ? kExitInvariant : 0;
    if (ExitStatic())
    {
        // The application's way out after the library did not start: the runner's destructor shuts the translator
        // registry down and the process runs its static destruction over tables that were half filled.
        const int leak = LeakCheck();
        if (leak && !dts::Failures()) ++dts::Failures();
        gMustNotExit = false;
        ghidra::SleighArchitecture::shutdown();
        std::fflush(nullptr);
        return dts::Failures() ? kExitInvariant : 0;
    }
    LeaveDamaged();
}

// ================================================================================================
// The driver: cells in a list, forked from the warmed engine
// ================================================================================================

struct SigStat
{
    long count = 0;
    long first = 0, last = 0;            // the lowest and highest parameter (allocation index, phase entry, us)
    std::set<std::string> phases;        // where the failing allocation fell, for the alloc family
};

struct DriverSummary
{
    long cells = 0, failed = 0;
    std::map<std::string, long> classes;                 // cell class -> count
    std::map<std::string, long> maxLatency;              // cancel class -> most allocations between the flag and the stop
    std::map<std::string, SigStat> sigs;                 // failure signature -> where
};

// The report of a failing cell, cut down to what identifies it: the check texts, the cell's own
// line, and the first frames of each stack, with the checkout's path taken off.
std::string Condense(const std::string& detail)
{
    static const char* const kRoot = "SaturnExplorer/";
    std::string out;
    int frames = 0, stacks = 0;
    for (std::string l : Lines(detail))
    {
        const size_t r = l.find(kRoot);
        while (r != std::string::npos && (l.find(kRoot) != std::string::npos))
        {
            // "/x/y/SaturnExplorer/FrontEnd/..." -> "FrontEnd/..."
            const size_t q = l.find(kRoot);
            const size_t start = l.rfind(' ', q);
            l.erase(start == std::string::npos ? 0 : start + 1, q + std::strlen(kRoot) - (start == std::string::npos ? 0 : start + 1));
            if (l.find(kRoot) == std::string::npos) break;
        }
        const bool frame = l.find("    #") == 0 || l.find("#") == 0;
        if (frame)
        {
            if (l.find(" in Allocate ") != std::string::npos || l.find(" in operator new") != std::string::npos ||
                l.find("DecompilerFaultTests.cpp") != std::string::npos || l.find("libasan") != std::string::npos ||
                l.find("__sanitizer_print") != std::string::npos || l.find("in malloc") != std::string::npos ||
                l.find("std::__new_allocator") != std::string::npos || l.find("std::allocator_traits") != std::string::npos ||
                l.find("std::_Vector_base") != std::string::npos)
                continue;
            if (++frames > 7) continue;
        }
        else
        {
            frames = 0;
            if (l.find("TRACE") == 0 || l.find("leak of") != std::string::npos) ++stacks;
            if (l.find("leak of") != std::string::npos && stacks > 2) continue;
        }
        if (l.size() > 230) l = l.substr(0, 227) + "...";
        out += l + "\n";
    }
    return out;
}

const char* PhaseOfAlloc(const Ctx& c, long k)
{
    const char* where = "before the engine";
    for (int i = 0; i < c.base.log.n; ++i)
        if (c.base.log.allocAt[i] <= k) where = PhaseName(c.base.log.phase[i]);
    return where;
}

std::string CellClass(const std::string& out)
{
    // "CELL alloc mailbox k=12 in=init class=failed-in-engine ..." -> failed-in-engine
    const size_t p = out.find(" class=");
    if (p != std::string::npos)
    {
        const size_t e = out.find(' ', p + 7);
        return out.substr(p + 7, e == std::string::npos ? std::string::npos : e - p - 7);
    }
    const size_t g = out.find(" got=");
    if (g != std::string::npos)
    {
        const size_t e = out.find(" allowed=", g);
        return "cancel:" + out.substr(g + 5, e == std::string::npos ? std::string::npos : e - g - 5);
    }
    return "?";
}

// The allocation sites where the exhaustive sweep found a defect (REVIEW_LEDGER.md, rows A6-1 ..
// A6-5, A6-10, A6-11, A6-14, A6-15, A6-21, A6-22 and the leaks that were fixed), in every run of the matrix, the default
// subset included. A site is named twice:
//
//   where -- the 'ord'-th entry of 'phase' in the baseline log (counting the reference run's entries
//            too), and in it the 'nth' allocation (from 0) of 'size' bytes. This survives a change
//            elsewhere in the run, which an allocation index does not (a merge moved every index by
//            more than a thousand), but not a change in the same phase entry: one more allocation of the
//            same size before the site moves the coordinates onto its neighbour.
//   what  -- 'site', the functions on the stack of that allocation, innermost first, separated by ';'
//            (each a substring of a demangled name; every one must occur, in order), and '#<n>': the
//            allocation is the n-th (from 0, in allocation order) of those at the site that ask for the
//            pin's size in the phase entry. The functions alone cannot tell two allocations of one size in
//            one function apart (ProtoStoreSymbol::clone makes two of 72 bytes), so a shift of one between
//            them was "site ok"; with the ordinal it is MOVED, and the cell fails the allocation the pin
//            names. It is how the driver knows the coordinates still lead to the site the row names. An
//            allocation that is not the one named is looked for again among the allocations of the same
//            size in the same phase entry (the pin "moved", and the one with the ordinal, or every match,
//            at most kPinMax of them, becomes a cell); if there is none the pin is STALE, and that fails
//            the run, so that it is re-pinned instead of silently pointing at code it was not written for
//            (A6-12: a pin that had slid onto a neighbour passed with the defect back in). The census also
//            checks the size (A6-24): the run it takes the stacks from must make the allocation at the
//            coordinates at the size the pin names, and must be the baseline's run, size for size.
//
// `--cell alloc <fixture> <k>` prints the coordinates of its allocation ("PIN-AT") and its stack, which
// is how a pin is written. Where there is no backtrace (not glibc) the coordinates alone are used and the
// run says so. The mutation checks in the Step 5 record revert each fix and show these cells fail again.
struct Pin
{
    const char* fixture;
    const char* phase;
    int ord;
    uint32_t size;
    int nth;
    const char* site;
    const char* what;
};
const Pin kPins[] = {
    { "mailbox", "print", 0, 48, 22, "EmitPrettyPrint::setMarkup#0", "A6-1 EmitPrettyPrint::setMarkup(true): new EmitMarkup after the old emitter was freed" },
    { "mailbox", "print", 0, 40, 81, "EmitPrettyPrint::setMarkup#0", "A6-1 EmitPrettyPrint::setMarkup(false): new EmitNoMarkup after the old emitter was freed" },
    { "mailbox", "print", 1, 48, 22, "EmitPrettyPrint::setMarkup#0", "A6-1 the same, in the reference run's print" },
    { "jumptable", "print", 0, 48, 26, "EmitPrettyPrint::setMarkup#0", "A6-1 the same, on the jumptable fixture" },
    { "mailbox", "print", 0, 16, 1, "EmitMarkup::setPackedOutput#0", "A6-2 EmitMarkup::setPackedOutput: new PackedEncode after the old encoder was freed" },
    { "jumptable", "actions", 0, 40, 87, "BlockGraph::identifyInternal;BlockGraph::newBlockSwitch#0", "A6-3 BlockGraph::identifyInternal: the room made in the new switch block before any block moves (first switch)" },
    { "jumptable", "actions", 0, 48, 9, "BlockGraph::identifyInternal;BlockGraph::newBlockSwitch#0", "A6-3 the same, the next switch block" },
    { "jumptable", "actions", 1, 40, 87, "BlockGraph::identifyInternal;BlockGraph::newBlockSwitch#0", "A6-3 the same, second pass" },
    { "jumptable", "actions", 1, 48, 9, "BlockGraph::identifyInternal;BlockGraph::newBlockSwitch#0", "A6-3 the same, second pass, next switch block" },
    { "mailbox", "print", 0, 513, 0, "overflow;PackedEncode::#0", "A6-4 the markup stream swallows the failure, the truncated markup fails to parse: 'unexpected exception'" },
    { "mailbox", "print", 0, 1025, 0, "overflow;PackedEncode::#0", "A6-4 the same, one growth later" },
    { "jumptable", "print", 0, 513, 0, "overflow;PackedEncode::#0", "A6-4 the same, on the jumptable fixture" },
    { "mailbox", "actions", 0, 513, 0, "overflow;Funcdata::removeUnreachableBlocks#0", "A6-5 a warning's stream swallows the failure, status Ok (first pass)" },
    { "mailbox", "actions", 1, 513, 0, "overflow;Funcdata::removeUnreachableBlocks#0", "A6-5 the same in the printing pass: 'WARNING: Removing unreac', status Ok" },
    { "jumptable", "followFlow", 0, 513, 0, "overflow;FlowInfo::recoverJumpTables#0", "A6-5 the jump-table message stream swallows the failure, status Ok" },
    { "jumptable", "followFlow", 1, 513, 0, "overflow;FlowInfo::recoverJumpTables#0", "A6-5 the same in the second pass" },
    // The leaks fixed on the way (patch 0023), seen under LeakSanitizer (a sanitizer build; in a plain one they are cells that pass):
    { "mailbox", "init", 0, 56, 0, "Architecture::Architecture#0", "A6-7 Architecture::Architecture: the print language is freed when the constructor fails" },
    { "mailbox", "init", 0, 20, 0, "OptionDatabase::registerOption;OptionDatabase::OptionDatabase#0", "A6-7 OptionDatabase::registerOption: the option is freed when it cannot be registered" },
    { "mailbox", "init", 0, 17, 0, "OptionDatabase::registerOption;OptionDatabase::OptionDatabase#0", "A6-7 OptionDatabase::OptionDatabase: the options registered so far are freed" },
    { "mailbox", "init", 0, 512, 0, "ParserContext::initialize;DisassemblyCache::initialize#0", "A6-7 DisassemblyCache::initialize: what it made is freed when it fails" },
    { "mailbox", "init", 0, 40, 52, "TypeFactory::insert;TypeFactory::findAdd#10", "A6-7 TypeFactory::insert: the data-type is freed when it cannot be inserted" },
    { "mailbox", "followFlow", 0, 24, 0, "PcodeOpBank::create;PcodeEmitFd::dump#0", "A6-8 PcodeOpBank::create: the op is freed when it cannot be listed" },
    { "mailbox", "followFlow", 0, 24, 72, "PcodeOpBank::markAlive;Funcdata::opInsert#0", "A6-8 PcodeOpBank::markAlive: the op is not lost when the alive list cannot take it" },
    { "jumptable", "followFlow", 0, 24, 60, "PcodeOpBank::create;Funcdata::cloneOp#0", "A6-8 PcodeOpBank::create (by cloneOp), on the jumptable fixture" },
    { "jumptable", "followFlow", 0, 24, 124, "PcodeOpBank::markAlive;Funcdata::opInsert#0", "A6-8 PcodeOpBank::markAlive, on the jumptable fixture" },
    { "mailbox", "followFlow", 0, 40, 0, "VarnodeBank::xref;VarnodeBank::createDef#0", "A6-8 VarnodeBank::createDef (xref): the Varnode is freed when it cannot be inserted" },
    { "mailbox", "followFlow", 0, 40, 2, "VarnodeBank::create(;Funcdata::newVarnode#0", "A6-8 VarnodeBank::create: the same" },
    { "mailbox", "actions", 0, 40, 0, "VarnodeBank::create(;Funcdata::newConstant#0", "A6-9 Funcdata::newConstant (VarnodeBank::create), in ActionPrototypeTypes" },
    { "mailbox", "actions", 0, 24, 169, "BlockGraph::newBlockCopy;BlockGraph::buildCopy#0", "A6-9 BlockGraph::newBlockCopy: the copy is freed when it cannot be added" },
    { "mailbox", "actions", 1, 24, 169, "BlockGraph::newBlockCopy;BlockGraph::buildCopy#0", "A6-9 the same, in the printing pass" },
    { "mailbox", "actions", 0, 40, 214, "CommentDatabaseInternal::addCommentNoDuplicate#0", "A6-9 CommentDatabaseInternal::addCommentNoDuplicate: the comment is freed when it cannot be stored" },
    { "mailbox", "actions", 1, 40, 214, "CommentDatabaseInternal::addCommentNoDuplicate#0", "A6-9 the same, in the printing pass" },
    // Round 2 (patch 0032): the sweeps of the other fixtures. A6-10 and A6-11 crash without the fix; A6-14 and A6-15 leak, seen under LeakSanitizer.
    { "fixture1", "actions", 0, 64, 58, "ProtoStoreSymbol::clone;FuncProto::copy#0", "A6-10 FuncProto::copy: the other prototype's store is cloned before the old one is freed (ProtoStoreSymbol)" },
    { "fixture1", "actions", 0, 72, 26, "ProtoStoreSymbol::clone;FuncProto::copy#0", "A6-10 ProtoStoreSymbol::clone: the constructor's parameter" },
    { "fixture1", "actions", 0, 72, 27, "ProtoStoreSymbol::clone;FuncProto::copy#1", "A6-10 ProtoStoreSymbol::clone: the copy of the parameter, made before the constructor's is freed" },
    { "fixture1", "actions", 1, 64, 58, "ProtoStoreSymbol::clone;FuncProto::copy#0", "A6-10 the same, second pass" },
    { "fixture1", "actions", 1, 72, 27, "ProtoStoreSymbol::clone;FuncProto::copy#1", "A6-10 the same, second pass" },
    { "shapes", "actions", 0, 16, 338, "HighVariable::mergeInternal#0", "A6-11 HighVariable::mergeInternal: the merged list is built before any Varnode changes owner" },
    { "shapes", "actions", 0, 24, 882, "HighVariable::mergeInternal#0", "A6-11 the same, another merge" },
    { "shapes", "actions", 0, 32, 259, "HighVariable::mergeInternal#0", "A6-11 the same, another merge" },
    { "shapes", "actions", 0, 40, 568, "HighVariable::mergeInternal#0", "A6-11 the same, another merge" },
    { "shapes", "actions", 0, 48, 56, "HighVariable::mergeInternal#0", "A6-11 the same, another merge" },
    { "shapes", "actions", 0, 56, 104, "Cover::merge;HighVariable::mergeInternal#0", "A6-11 the cover merge after the move: the emptied HighVariable is freed when it fails" },
    { "shapes", "actions", 1, 24, 882, "HighVariable::mergeInternal#0", "A6-11 the same, second pass" },
    { "shapes", "actions", 1, 32, 259, "HighVariable::mergeInternal#0", "A6-11 the same, second pass" },
    { "stack", "actions", 0, 40, 19, "VarnodeBank::xref;VarnodeBank::setInput#0", "A6-14 VarnodeBank::setInput: a Varnode taken out of the lists is kept if it cannot be put back" },
    { "stack", "actions", 0, 40, 89, "VarnodeBank::xref;VarnodeBank::setDef#0", "A6-14 VarnodeBank::setDef: the same" },
    { "stack", "actions", 0, 40, 37, "VarnodeBank::makeFree;Funcdata::clearDeadVarnodes#0", "A6-14 VarnodeBank::makeFree: the same" },
    { "stack", "actions", 0, 24, 26, "PcodeOpBank::destroy;PcodeOpBank::destroyDead#0", "A6-14 PcodeOpBank::destroy: the op is put on the final list before it leaves the others" },
    { "stack", "actions", 0, 8, 28, "MapState::addRange#0", "A6-14 MapState::addRange: the hint is freed when the list cannot take it" },
    { "stack", "actions", 0, 40, 108, "ScopeInternal::insertNameTree;ScopeInternal::renameSymbol#0", "A6-15 ScopeInternal::renameSymbol: a Symbol that cannot return to the name tree is kept" },
    { "stack", "actions", 0, 40, 42, "ScopeInternal::insertNameTree;ScopeInternal::addSymbolInternal#0", "A6-15 ScopeInternal::addSymbolInternal: a Symbol the name tree cannot take is freed (ScopeLocal::createEntry)" },
    { "stack", "actions", 0, 8, 29, "ScopeInternal::addMapInternal;Scope::addMapPoint#0", "A6-15 Scope::addMapPoint: the map entry is freed when the Symbol cannot list it" },
    // The leaks the round 2 survey found in the Flow and Print phases and in the action tree's clone (patch 0032), seen under LeakSanitizer:
    { "fixture1", "followFlow", 0, 8, 9, "FlowInfo::setupCallindSpecs#0", "A6-8 FlowInfo::setupCallindSpecs: the call site is freed when the function cannot list it" },
    { "mailbox", "followFlow", 0, 8, 8, "BlockGraph::addBlock;BlockGraph::newBlockBasic#0", "A6-8 BlockGraph::newBlockBasic: the block is freed when the graph cannot take it" },
    { "mailbox", "actions", 0, 24, 221, "BlockGraph::createVirtualRoot#0", "A6-9 BlockGraph::createVirtualRoot: the virtual root is freed when an edge cannot be made" },
    { "mailbox", "print", 0, 32, 2, "PackedDecode::ingestStream#0", "A6-9 PackedDecode::allocateNextInputBuffer: the buffer is freed when the stream cannot hold it" },
    { "jumptable", "followFlow", 0, 144, 0, "ActionRestartGroup::clone;ActionDatabase::deriveAction#0", "A6-7 ActionRestartGroup::clone: the copy of the action tree is freed when it cannot be completed" },
    { "jumptable", "followFlow", 0, 8, 31, "ActionRestartGroup::clone;ActionDatabase::deriveAction#0", "A6-7 the same, the member in hand when the group's list cannot grow" },
    { "jumptable", "followFlow", 0, 16, 23, "ActionGroup::clone;ActionRestartGroup::clone#0", "A6-7 ActionGroup::clone: the same, in a nested group" },
    { "jumptable", "followFlow", 0, 8, 33, "ActionPool::addRule;ActionPool::clone#0", "A6-7 ActionPool::clone: the rule in hand when the pool's list cannot grow" },
    { "jumptable", "followFlow", 0, 24, 175, "RuleIdentityEl::getOpList;ActionPool::addRule#0", "A6-7 ActionPool::clone: the rule is the pool's already when its op-code index cannot grow (not freed twice)" },
    { "mailbox", "actions", 0, 48, 6, "BlockGraph::calcForwardDominator;Funcdata::structureReset#1", "A6-9 BlockGraph::calcForwardDominator: the virtual root is freed when it cannot be added to the order" },
    { "jumptable", "init", 1, 48, 71, "Database::attachScope;Funcdata::Funcdata#1", "A6-7 Database::attachScope: the Scope is freed, and its id taken out of the map, when the parent cannot hold it (Funcdata::Funcdata)" },
    { "jumptable", "init", 1, 40, 40, "TypeFactory::getTypeVoid;TypeFactory::setCoreType#0", "A6-7 TypeFactory::getTypeVoid: the \"void\" data-type is freed when the tree cannot take it" },
    // Round 3 (patch 0040): jump-table recovery and the structurer's TraceDAG; leaks, seen under LeakSanitizer.
    { "jumptable", "followFlow", 0, 64, 94, "JumpValuesRange::clone;JumpBasic::clone#0", "A6-21 JumpBasic::clone: the copy of the model is freed when its range cannot be cloned" },
    { "jumptable", "followFlow", 0, 8, 197, "vector<ghidra::JumpTable*;Funcdata::recoverJumpTable#0", "A6-21 Funcdata::recoverJumpTable: the permanent table is freed when the list of tables cannot take it" },
    { "jumptable", "followFlow", 1, 64, 94, "JumpValuesRange::clone;JumpBasic::clone#0", "A6-21 the same, second pass" },
    { "jumptable", "followFlow", 1, 8, 197, "vector<ghidra::JumpTable*;Funcdata::recoverJumpTable#0", "A6-21 the same, second pass" },
    { "dag", "actions", 0, 8, 145, "vector<ghidra::TraceDAG::BranchPoint*;TraceDAG::initialize#0", "A6-22 TraceDAG::initialize: the root BranchPoint is owned by the list before anything else can fail" },
    { "dag", "actions", 0, 8, 146, "vector<ghidra::TraceDAG::BlockTrace*;TraceDAG::initialize#0", "A6-22 TraceDAG::initialize: a root BlockTrace is owned by its BranchPoint before the active list takes it" },
    { "dag", "actions", 0, 8, 147, "vector<ghidra::TraceDAG::BlockTrace*;BranchPoint::createTraces#0", "A6-22 BranchPoint::BranchPoint (createTraces): the first trace is freed when the list of paths cannot take it" },
    { "dag", "actions", 0, 16, 107, "vector<ghidra::TraceDAG::BlockTrace*;BranchPoint::createTraces#0", "A6-22 createTraces: the traces made so far are freed when the constructor fails" },
    { "dag", "actions", 0, 16, 108, "vector<ghidra::TraceDAG::BranchPoint*;TraceDAG::openBranch#0", "A6-22 TraceDAG::openBranch: the new BranchPoint is freed when the list cannot take it, and the parent trace is untouched" },
    { "dag", "actions", 1, 8, 145, "vector<ghidra::TraceDAG::BranchPoint*;TraceDAG::initialize#0", "A6-22 the same, second pass" },
    { "dag", "actions", 1, 16, 108, "vector<ghidra::TraceDAG::BranchPoint*;TraceDAG::openBranch#0", "A6-22 the same, second pass" },
    { "loopbrk", "actions", 0, 8, 136, "vector<ghidra::TraceDAG::BranchPoint*;TraceDAG::initialize#0", "A6-22 TraceDAG::initialize, in a loop with an early return" },
    { "loopbrk", "actions", 0, 8, 137, "vector<ghidra::TraceDAG::BlockTrace*;TraceDAG::initialize#0", "A6-22 TraceDAG::initialize: the root trace, in a loop with an early return" },
    { "loopbrk", "actions", 0, 16, 102, "vector<ghidra::TraceDAG::BranchPoint*;TraceDAG::openBranch#0", "A6-22 TraceDAG::openBranch, in a loop with an early return" },
};

long ResolvePin(const Ctx& c, const Pin& p)
{
    const Log& L = c.base.log;
    int ord = 0;
    for (int e = 0; e < L.n; ++e)
    {
        if (std::string(PhaseName(L.phase[e])) != p.phase || ord++ != p.ord) continue;
        const long hi = e + 1 < L.n ? L.allocAt[e + 1] : c.base.allocs;
        int n = 0;
        for (long k = L.allocAt[e]; k < hi && k < long(c.base.sizes.size()); ++k)
            if (c.base.sizes[size_t(k)] == p.size && n++ == p.nth) return k;
        return -1;
    }
    return -1;
}

// The phase entry a pin names: [lo, hi) of the baseline's allocation indices (empty if it has none).
void PinRange(const Ctx& c, const Pin& p, long& lo, long& hi)
{
    const Log& L = c.base.log;
    int ord = 0;
    lo = hi = 0;
    for (int e = 0; e < L.n; ++e)
    {
        if (std::string(PhaseName(L.phase[e])) != p.phase || ord++ != p.ord) continue;
        lo = L.allocAt[e];
        hi = e + 1 < L.n ? L.allocAt[e + 1] : c.base.allocs;
        return;
    }
}

// A site is the functions on the stack of the allocation, innermost first, each a substring of a demangled name and all in
// order, separated by ';'. It ends in '#<n>': the allocation is the n-th (from 0, in allocation order) of those at the site
// that ask for the pin's size in the pin's phase entry. The functions alone do not tell two such allocations apart
// (ProtoStoreSymbol::clone makes two of 72 bytes), so a shift of one between them used to be "site ok".
int SiteOrdinal(const char* site)
{
    const char* hash = site ? std::strrchr(site, '#') : nullptr;
    return hash ? std::atoi(hash + 1) : -1;
}

#ifdef SE_FAULT_BACKTRACE
bool SiteMatches(const std::vector<std::string>& names, const char* site)
{
    size_t at = 0;
    const char* hash = std::strrchr(site, '#');
    const std::string rest(site, hash ? size_t(hash - site) : std::strlen(site));
    for (size_t pos = 0; pos <= rest.size();)
    {
        size_t semi = rest.find(';', pos);
        if (semi == std::string::npos) semi = rest.size();
        const std::string want = rest.substr(pos, semi - pos);
        pos = semi + 1;
        if (want.empty()) continue;
        while (at < names.size() && names[at].find(want) == std::string::npos) ++at;
        if (at == names.size()) return false;
        ++at;
    }
    return true;
}
#endif

// A pin resolved against this build.
struct PinRes
{
    const Pin* pin = nullptr;
    long k0 = -1;              // the coordinates' allocation (-1: no such allocation)
    long lo = 0, hi = 0;       // the phase entry the pin names
    std::vector<long> ks;      // the allocations to fail: k0 when it is the site, every match when not
    std::string note;          // "site ok", "MOVED ...", "STALE ...", or why it was not checked
    bool stale = false;
};
const size_t kPinMax = 16;

// A MOVED pin fails the run, as a STALE one does (A6-34): the cell still fails the allocation the pin's ordinal names, so
// this run tests something, but a pin whose coordinates and ordinal disagree is wrong in one of the two and nothing
// re-pinned it, so it could stay pointed away from its defect from the commit that wrote it. The note prints both ways out
// (the coordinates that lead to the ordinal's allocation, and the ordinal the coordinates lead to). An ordinal the site
// does not have is STALE: it names nothing, and "MOVED" would have failed every allocation of the site instead.
long NthOfSize(const Ctx& c, long lo, long k, uint32_t size)
{
    long n = 0;
    for (long j = lo; j < k && j < long(c.base.sizes.size()); ++j)
        if (c.base.sizes[size_t(j)] == size) ++n;
    return n;
}

bool PinFails(const PinRes& r) { return r.stale || r.note.compare(0, 5, "MOVED") == 0; }

// Resolves the pins of the fixture and checks their identity: one extra run of the warmed engine, in a
// child, takes the stack of every allocation that could be a pin (the pin's size, in its phase entry), and
// names the stacks of the ones asked about. That run is the one the cells perturb (run N+1 of the series
// Warm settled), so it is also checked against the baseline: every allocation's size must be the baseline's,
// and the size the run made at a pin's coordinates must be the size the pin names (A6-24).
std::vector<PinRes> ResolvePins(Ctx& c, const std::string& scratch, const Pin* pinList = kPins, size_t pinCount = sizeof kPins / sizeof kPins[0])
{
    std::vector<PinRes> out;
    for (size_t pi = 0; pi < pinCount; ++pi)
    {
        const Pin& p = pinList[pi];
        if (c.fix->name != std::string(p.fixture)) continue;
        PinRes r;
        r.pin = &p;
        r.k0 = ResolvePin(c, p);
        PinRange(c, p, r.lo, r.hi);
        if (r.k0 < 0)
        {
            r.stale = true;
            r.note = "STALE: no such allocation (the phase entry or the size is gone)";
        }
        else if (!p.site || !*p.site)
        {
            r.stale = true;
            r.note = "STALE: the pin has no site";
        }
        else
            r.ks.push_back(r.k0);
        out.push_back(r);
    }
#ifdef SE_FAULT_BACKTRACE
    if (out.empty()) return out;
    // Which allocations to take a stack of.
    std::vector<uint8_t> want(c.base.sizes.size(), 0);
    for (const PinRes& r : out)
    {
        if (r.stale) continue;
        for (long k = r.lo; k < r.hi && k < long(want.size()); ++k)
            if (c.base.sizes[size_t(k)] == r.pin->size) want[size_t(k)] = 1;
    }
    fa::gCensusWant = want.data();
    fa::gCensusWantN = long(want.size());
    fa::gCensusStack = static_cast<fa::Stack*>(std::malloc(sizeof(fa::Stack) * size_t(fa::kCensusCap)));
    fa::gCensusK = static_cast<long*>(std::malloc(sizeof(long) * size_t(fa::kCensusCap)));
    fa::gCensusSize = static_cast<uint32_t*>(std::malloc(sizeof(uint32_t) * size_t(fa::kCensusCap)));
    auto body = [&]() -> int {
        dts::Failures() = 0;
        gProbe.Reset();
        fa::gCensusN = 0;
        fa::Arm(-1);
        fa::gCensus = true;
        fa::gLogSizes = true;
        RunOnce(c);
        fa::gLogSizes = false;
        fa::gCensus = false;
        const long n = fa::gCount.load();
        fa::Disarm();
        // Is this the baseline's run? Its allocations, one by one.
        long firstDiff = -1, diffs = 0;
        for (long k = 0; k < std::min(n, long(c.base.sizes.size())); ++k)
            if (fa::gSizeLog[k] != c.base.sizes[size_t(k)])
            {
                if (firstDiff < 0) firstDiff = k;
                ++diffs;
            }
        if (n != c.base.allocs || diffs)
            std::printf("PINSEQ %ld %ld %ld %u %u\n", n, diffs, firstDiff, firstDiff >= 0 ? fa::gSizeLog[firstDiff] : 0u,
                        firstDiff >= 0 ? c.base.sizes[size_t(firstDiff)] : 0u);
        // Name the stacks here, where the addresses mean what they meant while the run was going. A stack is named once.
        std::map<long, std::vector<std::string>> named;
        auto namesOf = [&](long j) -> const std::vector<std::string>& {
            auto it = named.find(j);
            if (it == named.end()) it = named.emplace(j, fa::Names(fa::gCensusStack[j])).first;
            return it->second;
        };
        for (size_t i = 0; i < out.size(); ++i)
        {
            const PinRes& r = out[i];
            if (r.stale) continue;
            // The coordinates' own allocation first.
            long j0 = -1;
            for (long j = 0; j < fa::gCensusN && j0 < 0; ++j)
                if (fa::gCensusK[j] == r.k0) j0 = j;
            if (j0 < 0)
            {
                std::printf("PINSITE %zu 0 -1\n", i);
                continue;
            }
            std::string top;
            int taken = 0;
            for (const std::string& nm : namesOf(j0))
            {
                if (nm.find("operator new") != std::string::npos || nm.find("Allocate") != std::string::npos ||
                    nm.find("CensusRecord") != std::string::npos)
                    continue;
                top += (taken ? " < " : "") + nm.substr(0, nm.find('('));
                if (++taken == 6) break;
            }
            std::printf("PINAT %zu %s\n", i, top.c_str());
            // Every allocation of the pin's size (as the run made it) in the entry that is made at the site, in order: the
            // coordinates' allocation is one of them, and its place among them is the ordinal a pin names.
            std::string matches;
            long rank0 = -1, nmatch = 0;
            for (long j = 0; j < fa::gCensusN; ++j)
            {
                const long k = fa::gCensusK[j];
                if (k < r.lo || k >= r.hi || fa::gCensusSize[j] != r.pin->size) continue;
                if (!SiteMatches(namesOf(j), r.pin->site)) continue;
                if (j == j0) rank0 = nmatch;
                ++nmatch;
                matches += " " + std::to_string(k);
            }
            std::printf("PINSITE %zu %u %ld%s\n", i, fa::gCensusSize[j0], rank0, matches.c_str());
        }
        if (fa::gCensusN >= fa::kCensusCap) std::printf("PINCAP the census is full: raise kCensusCap\n");
        std::fflush(stdout);
        std::_Exit(0);
    };
    CellRun run = ForkCell(scratch, body, CellTimeout());
    std::vector<std::string> top(out.size());
    std::vector<std::vector<long>> matches(out.size());
    std::vector<long> rank0(out.size(), -1);
    std::vector<long> sizeAt(out.size(), 0);
    std::vector<bool> seen(out.size(), false);
    std::string seqNote;
    for (const std::string& l : Lines(run.out))
    {
        if (l.compare(0, 7, "PINSEQ ") == 0)
        {
            long n = 0, diffs = 0, first = 0;
            unsigned got = 0, want = 0;
            std::istringstream in(l.substr(7));
            in >> n >> diffs >> first >> got >> want;
            seqNote = "the identity run made " + std::to_string(n) + " allocations (the baseline " + std::to_string(c.base.allocs) + ") and " +
                      std::to_string(diffs) + " of them at another size, the first at " + std::to_string(first) + " (" + std::to_string(got) +
                      " bytes, the baseline " + std::to_string(want) + ")";
            continue;
        }
        const bool at = l.compare(0, 6, "PINAT ") == 0;
        if (!at && l.compare(0, 8, "PINSITE ") != 0) continue;
        std::istringstream in(l.substr(at ? 6 : 8));
        size_t i = 0;
        in >> i;
        if (i >= out.size()) continue;
        if (at)
        {
            std::string rest;
            std::getline(in, rest);
            top[i] = rest.empty() ? rest : rest.substr(1);
        }
        else
        {
            in >> sizeAt[i] >> rank0[i];
            for (long k; in >> k;) matches[i].push_back(k);
            seen[i] = true;
        }
    }
    for (size_t i = 0; i < out.size(); ++i)
    {
        PinRes& r = out[i];
        if (r.stale) continue;
        const int ord = SiteOrdinal(r.pin->site);
        std::vector<long>& m = matches[i];
        if (!seqNote.empty())
        {
            r.stale = true;
            r.ks.clear();
            r.note = "STALE: " + seqNote + ": the baseline is not the run the cells perturb";
        }
        else if (!seen[i] || sizeAt[i] == 0)
        {
            r.stale = true;
            r.ks.clear();
            r.note = std::string("STALE: the identity run did not report (") + (run.proc.timedOut ? "timed out" : "failed") + ")";
        }
        else if (sizeAt[i] != long(r.pin->size))
        {
            r.stale = true;
            r.ks.clear();
            r.note = "STALE: the identity run asked for " + std::to_string(sizeAt[i]) + " bytes at allocation " + std::to_string(r.k0) +
                     ", the pin names " + std::to_string(r.pin->size);
        }
        else if (ord < 0)
        {
            // A pin names which of the site's allocations of its size it is: two in one function cannot be told apart by the
            // function alone (the second of ProtoStoreSymbol::clone's 72-byte allocations is not the first).
            r.stale = true;
            r.ks.clear();
            r.note = rank0[i] >= 0 ? "STALE: the site has no ordinal; the coordinates' allocation is #" + std::to_string(rank0[i]) + " of the " +
                                         std::to_string(m.size()) + " at [" + r.pin->site + "]: write \"" + r.pin->site + "#" + std::to_string(rank0[i]) + "\""
                                   : "STALE: the site has no ordinal, and the coordinates lead to [" + top[i] + "], not to [" + r.pin->site + "]";
        }
        else if (rank0[i] >= 0 && rank0[i] == ord)
        {
            r.ks.assign(1, r.k0);
            r.note = "site ok (#" + std::to_string(ord) + " of " + std::to_string(m.size()) + ")";
        }
        else if (ord >= 0 && ord < long(m.size()))
        {
            // The site's allocation the pin names is not at the coordinates: fail that one.
            // This run tests the allocation the pin's ordinal names, and the pin fails the run (PinFails) until it is
            // re-pinned: to follow the ordinal, the coordinates below; to keep the coordinates, the ordinal they lead to.
            r.ks.assign(1, m[size_t(ord)]);
            r.note = "MOVED: the coordinates lead to [" + top[i] + "]" +
                     (rank0[i] >= 0 ? " (the site's #" + std::to_string(rank0[i]) + ")" : std::string(", not the site")) + ", the pin names #" +
                     std::to_string(ord) + " of [" + r.pin->site + "], which is allocation " + std::to_string(m[size_t(ord)]) +
                     ". Re-pin: to follow the ordinal, write nth = " + std::to_string(NthOfSize(c, r.lo, m[size_t(ord)], r.pin->size)) +
                     " (the " + std::to_string(r.pin->size) + "-byte allocations of " + r.pin->phase + " #" + std::to_string(r.pin->ord) +
                     " are counted from 0)" +
                     (rank0[i] >= 0 ? "; to keep the coordinates, write \"" + std::string(r.pin->site).substr(0, std::string(r.pin->site).rfind('#')) + "#" +
                                          std::to_string(rank0[i]) + "\""
                                    : std::string());
        }
        else if (!m.empty())
        {
            // An ordinal the site does not have names no allocation: the honest verdict is STALE, not a sweep of the whole
            // site (A6-34), which would test every allocation there but the one the row is about.
            r.stale = true;
            r.ks.clear();
            r.note = "STALE: the site [" + std::string(r.pin->site) + "] has no #" + std::to_string(ord) + " here, only " + std::to_string(m.size()) +
                     " allocation(s) of " + std::to_string(r.pin->size) + " bytes in the entry are made there (the coordinates lead to [" + top[i] + "])";
        }
        else
        {
            r.stale = true;
            r.ks.clear();
            r.note = "STALE: the coordinates lead to [" + top[i] + "], and no allocation of " + std::to_string(r.pin->size) +
                     " bytes in this phase entry is made at [" + r.pin->site + "]";
        }
    }
#else
    for (PinRes& r : out)
        if (!r.stale) r.note = "identity not checked (no backtrace on this platform)";
#endif
    return out;
}

// `--sites <fixture> <phase> <ord> <size> [site]`: for each allocation of that size in that phase entry, its rank
// among them and the innermost functions on its stack. How the 'site' of a pin is found. With a site, only the
// allocations made there, and each one's ordinal among them (the '#n' of the site). The run is checked as the pins'
// census is (A6-24): an allocation that asks for another size than the baseline's is marked, and so is a run that is
// not the baseline's.
int SitesTool(const std::string& fixture, const std::string& scratch, const std::string& phase, int ord, uint32_t size,
              const char* site = nullptr)
{
#ifdef SE_FAULT_BACKTRACE
    Ctx c;
    if (!Warm(c, scratch, fixture)) return 2;
    Pin p = { fixture.c_str(), phase.c_str(), ord, size, 0, "", "" };
    long lo = 0, hi = 0;
    PinRange(c, p, lo, hi);
    std::vector<uint8_t> want(c.base.sizes.size(), 0);
    for (long k = lo; k < hi && k < long(want.size()); ++k)
        if (c.base.sizes[size_t(k)] == size) want[size_t(k)] = 1;
    fa::gCensusWant = want.data();
    fa::gCensusWantN = long(want.size());
    fa::gCensusStack = static_cast<fa::Stack*>(std::malloc(sizeof(fa::Stack) * size_t(fa::kCensusCap)));
    fa::gCensusK = static_cast<long*>(std::malloc(sizeof(long) * size_t(fa::kCensusCap)));
    fa::gCensusSize = static_cast<uint32_t*>(std::malloc(sizeof(uint32_t) * size_t(fa::kCensusCap)));
    gProbe.Reset();
    fa::gCensusN = 0;
    fa::Arm(-1);
    fa::gCensus = true;
    fa::gLogSizes = true;
    RunOnce(c);
    fa::gLogSizes = false;
    fa::gCensus = false;
    const long n = fa::gCount.load();
    fa::Disarm();
    long firstDiff = -1, diffs = 0;
    for (long k = 0; k < std::min(n, long(c.base.sizes.size())); ++k)
        if (fa::gSizeLog[k] != c.base.sizes[size_t(k)])
        {
            if (firstDiff < 0) firstDiff = k;
            ++diffs;
        }
    if (n != c.base.allocs || diffs)
        std::printf("WARNING: this run is not the baseline's: %ld allocations (baseline %ld), %ld at another size, the first at %ld; the listing may name other allocations\n",
                    n, c.base.allocs, diffs, firstDiff);
    const char* depthEnv = std::getenv("SE_FAULT_DEPTH");
    const int depth = depthEnv ? std::atoi(depthEnv) : 5;
    long rank = 0;
    for (long j = 0; j < fa::gCensusN; ++j)
    {
        const std::vector<std::string> names = fa::Names(fa::gCensusStack[j]);
        if (site && !SiteMatches(names, site)) continue;
        std::string top;
        int taken = 0;
        for (const std::string& nm : names)
        {
            if (nm.find("operator new") != std::string::npos || nm.find("Allocate") != std::string::npos || nm.find("CensusRecord") != std::string::npos)
                continue;
            top += (taken ? " < " : "") + nm.substr(0, 110);
            if (++taken == depth) break;
        }
        char tag[48] = "";
        if (fa::gCensusSize[j] != size) std::snprintf(tag, sizeof tag, "  ASKED FOR %u BYTES", fa::gCensusSize[j]);
        if (site) std::printf("#%ld  { \"%s\", \"%s\", %d, %u, %ld }  k=%ld%s  %s\n", rank++, fixture.c_str(), phase.c_str(), ord, size, j, fa::gCensusK[j], tag, top.c_str());
        else std::printf("{ \"%s\", \"%s\", %d, %u, %ld }  k=%ld%s  %s\n", fixture.c_str(), phase.c_str(), ord, size, j, fa::gCensusK[j], tag, top.c_str());
    }
    return 0;
#else
    (void)fixture; (void)scratch; (void)phase; (void)ord; (void)size;
    std::printf("--sites needs a backtrace (glibc)\n");
    return 2;
#endif
}

// `--diffruns <fixture> [runs]`: after Warm, 'runs' more runs of the same engine (50), each compared with the baseline: the
// number of allocations, the size of each and the result. The first, N+1, is the run every cell makes and the baseline was
// measured from, so it must be identical (the exit status). The later ones are not required to be: the markup's group ids
// (`TokenSplit::countbase`, see MeasureBaseline) keep growing, and a run in which they pass 127 or 16,383 has a longer
// markup; that is what this tool is for.
int DiffRunsTool(const std::string& fixture, const std::string& scratch, int runs)
{
    Ctx c;
    if (!Warm(c, scratch, fixture)) return 2;
    int bad = 0;
    for (int i = 1; i <= runs; ++i)
    {
        gProbe.Reset();
        fa::Arm(-1);
        fa::gLogSizes = true;
        const DecompileResult r = RunOnce(c);
        fa::gLogSizes = false;
        const long n = fa::gCount.load();
        fa::Disarm();
        long first = -1, diffs = 0;
        for (long k = 0; k < std::min(n, long(c.base.sizes.size())); ++k)
            if (fa::gSizeLog[k] != c.base.sizes[size_t(k)])
            {
                if (first < 0) first = k;
                ++diffs;
            }
        const bool same = n == c.base.allocs && diffs == 0 && Signature(r) == c.base.sig;
        if (!same)
        {
            if (i == 1) bad = 1000;
            ++bad;
            std::printf("run N+%d: %ld allocations (baseline %ld), %ld sizes differ, first at %ld (%u vs baseline %u)%s\n", i, n, c.base.allocs, diffs,
                        first, first >= 0 ? fa::gSizeLog[first] : 0u, first >= 0 ? c.base.sizes[size_t(first)] : 0u,
                        Signature(r) == c.base.sig ? "" : "; the result differs");
        }
    }
    std::printf("diffruns %s: %d of %d runs after the baseline differ from it%s\n", fixture.c_str(), bad % 1000, runs,
                bad >= 1000 ? "; THE FIRST DOES: the baseline is not the run the cells make" : "");
    return bad >= 1000 ? 1 : 0;
}

// What a fixture added for coverage must keep reaching (A6-36). Each one is there to put a failed allocation inside a host path
// no other fixture runs: the lent prototype and effects, the mirror's entry, the hardware notes. A refactor that stopped
// lending, or stopped noting a register, would leave the fixture decompiling and the sweep green over nothing; the baseline
// result (Signature: the text, the warnings) must show each path was taken, and this turns its absence into a failure.
// (The census cannot say it from stacks: the host's own functions are not in the dynamic symbol table.) `--show <fixture>`
// prints the baseline signature these are written from.
struct Reach
{
    const char* fixture;
    const char* evidence;   // a substring of the baseline's signature
    const char* what;
};
const Reach kReach[] = {
    { "lend", "Fn_lend(int4 *param_1)", "the lent parameter type reaches the caller's own parameter (ApplyPrototype)" },
    { "lend", "func_0x06005000(param_1)", "the call to the lent callee passes the lent parameter" },
    { "lendm", "Fn_lendm(int4 *param_1)", "the lent parameter type reaches the caller's own parameter" },
    { "lendm", "A 637554688", "the call goes through the mirror 0x26005000 (the callee lent at both addresses)" },
    { "lendm", "func_0x06005000(param_1)", "the mirrored call resolves to the callee and passes the lent parameter" },
    { "lendx", "Fn_lendx(int4 *param_1,uint2 param_2)", "the lent second parameter reaches the caller" },
    { "lendx", "func_0x06005000(param_1,param_2)", "the call passes both lent parameters" },
    { "lendx", "axStack_88 + 0x84", "the lent stack delta (setExtraPop) moves the frame after the call" },
    { "div3", "return param_1 / 3;", "the division by a constant is recovered from the multiply and the shift (RuleDivOpt)" },
    { "ext", "char Fn_ext(char param_1,char param_2)", "the sub-word values are typed as the extensions narrow them" },
    { "stk6", "func_0x06005000(param_1,param_2,param_3,param_4,(int4)param_1,param_2)", "the fifth and sixth lent parameters are the two words stored on the stack" },
    { "nest", "*(int4 *)(param_3 + -0x18) = iVar1;", "the sixth nested store is printed: six levels of structure, not one merged condition" },
    { "wide", "return (uint8)param_2 * (uint8)param_1;", "the 64-bit product is returned in the join of r0 and r1" },
    { "lend8", "uVar2 = func_0x06005000(param_1);", "the lent 64-bit return is read by the caller as one uint8 (the join of r0 and r1)" },
    { "lend8", "(uVar2 >> 0x20)", "the high half of the join is r0's (patch 0007)" },
    { "hwreg", "puVar2 = &VDP2_TVMD;", "the register is named (HardwareNotes)" },
    { "hwreg", "puVar2 = &VDP2_EXTEN;", "the store's pointer resolves to the second register (PointerResolver)" },
};

// The requirements of kReach for the fixture; the number of those not met.
int ReachCheck(const Ctx& c)
{
    int bad = 0;
    for (const Reach& r : kReach)
    {
        if (c.fix->name != std::string(r.fixture)) continue;
        const bool ok = Has(c.base.sig, r.evidence);
        std::printf("REACHED %s '%s': %s (%s)\n", r.fixture, r.evidence, ok ? "yes" : "NO", r.what);
        if (!ok)
        {
            std::printf("FAIL reach %s: the baseline result has no '%s' -- %s: the fixture no longer covers what it was added for\n", r.fixture,
                        r.evidence, r.what);
            ++bad;
        }
    }
    return bad;
}

// The pins' own check (A6-34): pins deliberately written wrong, on the dag fixture, and the verdict each must get. A pin whose
// coordinates and ordinal disagree (MOVED) must fail the run, with the coordinates that put it right in the note; an ordinal
// the site does not have, or a site the coordinates cannot reach, is STALE. Without this a change to the census that let a
// MOVED pin pass again would break nothing: every real pin is "site ok".
int PinSelfTest(Ctx& c, const std::string& scratch)
{
#ifdef SE_FAULT_BACKTRACE
    static const char* const kCreate = "vector<ghidra::TraceDAG::BlockTrace*;BranchPoint::createTraces";
    static const std::string good = std::string(kCreate) + "#0", wrongOrd = std::string(kCreate) + "#1", noOrd = std::string(kCreate) + "#99",
                             noSite = "TraceDAG::initialize#0", noHash = kCreate;
    // The good pin's coordinates are found at run time, by the census every real pin goes through: ordinal #0 of the site is some
    // allocation of the entry, and its place among the entry's 16-byte allocations is the nth that leads to it. Nothing here
    // names an allocation count, so an engine patch that moves the count (patch 0044 moved it from 108 to 107) cannot make this
    // test stale: it never needed to be re-pinned by hand, only the real pins do (A6-42). The probe's own coordinates are any
    // 16-byte allocation of the entry (the first); whatever it leads to, the verdict names the allocation of the site's #0.
    const Pin probe[] = { { "dag", "actions", 0, 16, 0, good.c_str(), "probe" } };
    const std::vector<PinRes> found = ResolvePins(c, scratch, probe, 1);
    if (found.size() != 1 || found[0].stale || found[0].ks.size() != 1)
    {
        std::printf("FAIL pin-selftest: the site [%s] has no #0 in dag's first Actions entry (%s): the fixture or the engine changed so that the "
                    "test's allocation is gone\n",
                    good.c_str(), found.empty() ? "no result" : found[0].note.c_str());
        return 1;
    }
    const int nth = int(NthOfSize(c, found[0].lo, found[0].ks[0], 16));
    // A neighbour for the pin one allocation off: the next 16-byte allocation of the entry, or the one before when there is none.
    const Pin next = { "dag", "actions", 0, 16, nth + 1, good.c_str(), "" };
    const int off = ResolvePin(c, next) >= 0 || nth == 0 ? nth + 1 : nth - 1;
    std::printf("pin-selftest: the site's #0 is the %dth 16-byte allocation of the entry (allocation %ld); the one-off pin is %d\n", nth, found[0].ks[0], off);
    const Pin pins[] = {
        { "dag", "actions", 0, 16, nth, good.c_str(), "right coordinates, right ordinal" },
        { "dag", "actions", 0, 16, nth, wrongOrd.c_str(), "right coordinates, the next allocation's ordinal" },
        { "dag", "actions", 0, 16, nth, noOrd.c_str(), "an ordinal the site does not have" },
        { "dag", "actions", 0, 16, off, good.c_str(), "coordinates one allocation off, right ordinal" },
        { "dag", "actions", 0, 16, nth, noSite.c_str(), "a site no allocation of the entry is made at" },
        { "dag", "actions", 0, 16, nth, noHash.c_str(), "no ordinal at all" },
    };
    const std::vector<PinRes> res = ResolvePins(c, scratch, pins, sizeof pins / sizeof pins[0]);
    int bad = 0;
    auto check = [&](size_t i, bool ok, const char* what) {
        if (ok) return;
        std::printf("FAIL pin-selftest: %s: %s -- %s\n", pins[i].what, what, i < res.size() ? res[i].note.c_str() : "(no result)");
        ++bad;
    };
    if (res.size() != sizeof pins / sizeof pins[0])
    {
        std::printf("FAIL pin-selftest: %zu pins resolved, %zu written\n", res.size(), sizeof pins / sizeof pins[0]);
        return 1;
    }
    for (size_t i = 0; i < res.size(); ++i) std::printf("PINTEST %zu %s: %s\n", i, pins[i].what, res[i].note.c_str());
    auto starts = [&](size_t i, const char* w) { return res[i].note.compare(0, std::strlen(w), w) == 0; };
    check(0, starts(0, "site ok") && !PinFails(res[0]) && res[0].ks.size() == 1, "a pin that leads to its site passes");
    check(1, starts(1, "MOVED") && PinFails(res[1]) && res[1].ks.size() == 1 && res[1].ks[0] != res[1].k0 && res[1].note.find("Re-pin") != std::string::npos,
          "a wrong ordinal is MOVED, fails the run, tests the ordinal's allocation, and says how to re-pin");
    check(2, starts(2, "STALE") && PinFails(res[2]) && res[2].ks.empty(), "an ordinal the site does not have is STALE");
    // The re-pin the note offers is the good pin's nth, whatever the engine's allocation count is this round.
    const std::string repin = "nth = " + std::to_string(nth);
    check(3, starts(3, "MOVED") && PinFails(res[3]) && res[3].ks.size() == 1 && res[3].note.find(repin) != std::string::npos,
          "coordinates one off are MOVED, fail the run, and the note gives the nth that re-pins them");
    check(4, starts(4, "STALE") && PinFails(res[4]), "a site nothing in the entry is made at is STALE");
    check(5, starts(5, "STALE") && PinFails(res[5]), "a pin without an ordinal is STALE");
    std::printf("SUMMARY pin-selftest dag shard 0/1: %zu cells, %d failed, 0.0 s\n", res.size(), bad);
    return bad ? 1 : 0;
#else
    (void)c; (void)scratch;
    std::printf("pin-selftest: needs a backtrace (glibc); not run\n");
    return 0;
#endif
}

std::vector<long> AllocCells(const Ctx& c, bool full, const std::vector<PinRes>& pins)
{
    std::set<long> s;
    const long N = c.base.allocs;
    auto add = [&](long k) {
        if (k >= 0) s.insert(k);
    };
    const Log& L = c.base.log;
    if (full)
    {
        const long dense = long(EnvDouble("SE_FAULT_DENSE", 3000));
        for (long k = 0; k < std::min(dense, N); ++k) add(k);
        for (double k = double(dense); k < double(N); k = std::max(k + 1, k * 1.02)) add(long(k));
        for (int e = 0; e < L.n; ++e)
            for (long d = -32; d <= 32; ++d) add(L.allocAt[e] + d);
        for (long d = 0; d < 64; ++d) add(N - 1 - d);
    }
    else
    {
        // A deterministic subset: the first dozen, a geometric spread, three allocations around
        // every phase entry, the tail.
        for (long k = 0; k < 12; ++k) add(k);
        for (double k = 12; k < double(N); k *= 1.6) add(long(k));
        for (int e = 0; e < L.n; ++e)
            for (long d = -1; d <= 1; ++d) add(L.allocAt[e] + d);
        add(N - 1);
        add(N - 2);
    }
    for (const PinRes& r : pins)
        for (long k : r.ks) add(k);   // a stale pin has none; the driver reports it and fails
    add(N);       // one past the last: nothing fails, the run must equal the baseline
    add(N + 5);
    // SE_FAULT_PHASE=<name>[,<name>...]: instead, every k of every phase entry with one of these names (every k of
    // the Actions of a fixture under a sanitizer, where the whole run would take hours); SE_FAULT_RANGE=<lo>-<hi>:
    // every k in the range.
    if (const char* phases = std::getenv("SE_FAULT_PHASE"))
    {
        std::set<std::string> names;
        std::istringstream in(phases);
        for (std::string w; std::getline(in, w, ',');) names.insert(w);
        s.clear();
        for (int e = 0; e < L.n; ++e)
        {
            if (!names.count(PhaseName(L.phase[e]))) continue;
            const long hi = e + 1 < L.n ? L.allocAt[e + 1] : N;
            for (long k = L.allocAt[e]; k < hi; ++k) s.insert(k);
        }
    }
    if (const char* range = std::getenv("SE_FAULT_RANGE"))
    {
        const char* dash = std::strchr(range, '-');
        if (dash)
        {
            s.clear();
            for (long k = std::atol(range); k < std::atol(dash + 1); ++k) s.insert(k);
        }
    }
    return std::vector<long>(s.begin(), s.end());
}

// cancel-oom: the cancel goes up at allocation k and an allocation 'j' later fails (j from this table),
// so the failure can come while the engine is unwinding a CancelError. Encoded as k * 8 + index of j.
const long kFailAfterCancel[8] = { 0, 1, 2, 4, 8, 64, 512, 4096 };
std::vector<long> CancelOomCells(const Ctx& c, bool full)
{
    std::set<long> ks;
    const long N = c.base.allocs;
    const Log& L = c.base.log;
    if (full)
    {
        for (long k = 0; k < std::min(3000L, N); k += 7) ks.insert(k);
        for (double k = 3000; k < double(N); k = std::max(k + 1, k * 1.015)) ks.insert(long(k));
        for (int e = 0; e < L.n; ++e)
            for (long d = -16; d <= 16; ++d)
                if (L.allocAt[e] + d >= 0) ks.insert(L.allocAt[e] + d);
    }
    else
    {
        for (double k = 3; k < double(N); k *= 1.9) ks.insert(long(k));
        for (int e = 0; e < L.n; ++e)
            if (L.allocAt[e] >= 0) ks.insert(L.allocAt[e]);
    }
    std::vector<long> v;
    for (long k : ks)
        for (long j = 0; j < 8; ++j)
            if (full || j == 0 || j == 3 || j == 5) v.push_back(k * 8 + j);
    return v;
}

std::vector<long> EnterCells(const Ctx& c, bool)
{
    std::vector<long> v;
    for (long e = -1; e < c.base.log.n; ++e) v.push_back(e);
    v.push_back(c.base.log.n);   // past the end: never raised
    return v;
}

// The host's own check-points (A6-41): one cell with the flag raised in the hook before each, one with it raised at the last
// allocation made before each, and one past the last (never raised). Every one must be honoured by the check, in the phase that
// ends there, which only that check can do: the engine has no check-point between its last one and ours.
std::vector<long> BoundCells(const Ctx& c)
{
    std::vector<long> v;
    for (int j = 0; j < c.base.log.nb; ++j)
    {
        v.push_back(2L * j);
        if (c.base.log.boundAlloc[j] > 0) v.push_back(2L * j + 1);
    }
    v.push_back(2L * c.base.log.nb);   // past the last check-point: never raised
    return v;
}

// The check-points the phase log says there must be: one at the end of every Flow that goes on to Actions, of every Actions
// that goes on to Print, and of every Teardown that follows an Actions and goes on to an Init (the first pass's end, whose
// Architecture is gone by then). Their number and place in the run are the baseline's; a check-point the host no longer makes
// has no cell and no hook call, which would pass for want of a raise.
int BoundaryCheck(const Ctx& c)
{
    const Log& L = c.base.log;
    std::vector<int> want;
    for (int s = 0; s < L.n; ++s)
    {
        const bool next = s + 1 < L.n;
        const DecompilePhase p = L.phase[s];
        if (p == DecompilePhase::Flow && next && L.phase[s + 1] == DecompilePhase::Actions) want.push_back(s + 1);
        else if (p == DecompilePhase::Actions && next && L.phase[s + 1] == DecompilePhase::Print) want.push_back(s + 1);
        else if (p == DecompilePhase::Teardown && s > 0 && L.phase[s - 1] == DecompilePhase::Actions && next && L.phase[s + 1] == DecompilePhase::Init)
            want.push_back(s + 1);
    }
    std::string wantText, gotText;
    for (int e : want) wantText += std::to_string(e) + " ";
    for (int j = 0; j < L.nb; ++j) gotText += std::to_string(L.boundEnters[j]) + "@" + std::to_string(L.boundAlloc[j]) + " ";
    bool same = int(want.size()) == L.nb;
    for (int j = 0; same && j < L.nb; ++j) same = want[size_t(j)] == L.boundEnters[j];
    std::printf("BOUNDARIES %s: %d host check-points (phase entries before each @ allocations): %s\n", c.fix->name, L.nb, gotText.c_str());
    if (!same)
        std::printf("FAIL boundaries %s: the host made check-points after phase entries [%s], the phase log says [%s]: a check-point is gone or new\n",
                    c.fix->name, gotText.c_str(), wantText.c_str());
    return same && L.nb > 0 ? 0 : 1;
}

std::vector<long> CancelAllocCells(const Ctx& c, bool full)
{
    std::set<long> s;
    const long N = c.base.allocs;
    auto add = [&](long k) {
        if (k >= 0) s.insert(k);
    };
    const Log& L = c.base.log;
    if (full)
    {
        const long dense = std::min(long(EnvDouble("SE_FAULT_DENSE", 3000)), 3000L);   // a cancel is cheap to land, dear to repeat
        for (long k = 0; k < std::min(dense, N); k += 7) add(k);
        for (double k = double(dense); k < double(N); k = std::max(k + 1, k * 1.01)) add(long(k));
        for (int e = 0; e < L.n; ++e)
            for (long d = -24; d <= 24; ++d) add(L.allocAt[e] + d);
        for (long d = 0; d < 48; ++d) add(N - 1 - d);
    }
    else
    {
        for (double k = 1; k < double(N); k *= 1.45) add(long(k));
        for (int e = 0; e < L.n; ++e)
            for (long d = -2; d <= 2; ++d) add(L.allocAt[e] + d);
        add(N - 1);
    }
    add(N + 3);
    return std::vector<long>(s.begin(), s.end());
}

// The time family: the flag goes up 'param' microseconds into the run, from a second thread. The
// full matrix steps by 100 us (a superset of the 1 ms steps across the run) and runs past its end.
std::vector<long> UsCells(const Ctx& c, bool full)
{
    std::vector<long> v;
    const long lenUs = long(std::ceil(c.base.ms * 1000.0)) + 2000;
    long step = full ? 100 : std::max(1000L, lenUs / 24 / 1000 * 1000);
    // SE_FAULT_US_STEP=<us>: a denser (or sparser) sweep, for a sanitizer build, where the full run's 100 us steps are hours.
    if (const char* st = std::getenv("SE_FAULT_US_STEP"))
        if (std::atol(st) > 0) step = std::atol(st);
    for (long t = 0; t <= lenUs; t += step) v.push_back(t);
    return v;
}

int Driver(const std::string& familyArg, const std::string& fixture, const std::string& scratch, bool full, int shard, int shards,
           long only)
{
    // "<family>-sticky": the same cells with sustained exhaustion, every allocation from k on failing (A6-23).
    std::string family = familyArg;
    const bool sticky = family.size() > 7 && family.compare(family.size() - 7, 7, "-sticky") == 0;
    if (sticky) family.resize(family.size() - 7);
    const char* stickyEnv = std::getenv("SE_FAULT_STICKY");
    fa::gSticky = sticky || (stickyEnv && *stickyEnv && std::string(stickyEnv) != "0");
    Ctx c;
    ColdCtx cold;
    const bool isCold = family == "cold" || family == "cold-disasm";
    if (isCold)
    {
        // No warm-up: the first request of each cell is the thing under test.
        if (!PrepareCold(cold, scratch, fixture, family == "cold-disasm")) return 2;
    }
    else if (!Warm(c, scratch, fixture))
        return 2;
    if (family == "pin-selftest") return PinSelfTest(c, scratch);
    std::vector<long> cells;
    std::vector<PinRes> pins;
    DisasmBase disasm;
    if (family == "alloc" && !sticky && only == -999999) pins = ResolvePins(c, scratch);
    if (isCold) cells = ColdCells(cold, full);
    else if (family == "alloc") cells = AllocCells(c, full, pins);
    else if (family == "disasm")
    {
        if (!MeasureDisasm(c, disasm)) return 2;
        cells = DisasmCells(disasm, full);
    }
    else if (family == "cancel-enter") cells = EnterCells(c, full);
    else if (family == "cancel-bound") cells = BoundCells(c);
    else if (family == "cancel-alloc") cells = CancelAllocCells(c, full);
    else if (family == "cancel-us") cells = UsCells(c, full);
    else if (family == "cancel-oom") cells = CancelOomCells(c, full);
    else
    {
        std::printf("unknown family '%s'\n", familyArg.c_str());
        return 2;
    }
    if (only != -999999) cells.assign(1, only);
    const auto t0 = Clock::now();
    DriverSummary sum;
    if (family == "alloc" && !sticky && only == -999999 && shard == 0)
    {
        // The fixtures added for coverage still reach what they were added for (A6-36).
        const int bad = ReachCheck(c);
        if (bad)
        {
            sum.failed += bad;
            sum.classes["FAIL:reach"] += bad;
        }
    }
    if (family == "alloc" && !sticky && only == -999999)
    {
        // Every shard resolves the pins (the cells are dealt out among them); shard 0 reports them, and every
        // shard counts a stale one as its own failure, so that no shard of a sweep passes over it.
        for (const PinRes& r : pins)
        {
            const Pin& p = *r.pin;
            if (shard == 0 || PinFails(r))
                std::printf("PIN %s %s: %s #%d, %u bytes #%d -> allocation %ld: %s\n", p.fixture, p.what, p.phase, p.ord, p.size, p.nth,
                            r.k0, r.note.c_str());
            if (PinFails(r))
            {
                // STALE (no such allocation, an ordinal the site lacks, a site the coordinates cannot reach) and MOVED (the
                // coordinates and the ordinal disagree) both fail the run until the pin is re-pinned, the note saying how: a
                // MOVED pin's cell still tests the allocation of its ordinal, but nothing else would ever re-point the pin from
                // a defect it was written for (A6-34). PinSelfTest holds PinFails to this.
                ++sum.failed;
                ++sum.classes[r.stale ? "FAIL:stale-pin" : "FAIL:pin-moved"];
            }
            else if (r.note.compare(0, 8, "identity") == 0 && !std::getenv("SE_FAULT_NO_IDENTITY"))
            {
                // Without a backtrace a pin that slides onto a neighbour passes (A6-12's oracle switched off): say so as a
                // failure, once per pin, rather than a note in a log nobody reads. SE_FAULT_NO_IDENTITY=1 accepts it.
                std::printf("FAIL pin %s %s: its site cannot be checked on this platform (set SE_FAULT_NO_IDENTITY=1 to accept)\n", p.fixture, p.what);
                ++sum.failed;
                ++sum.classes["FAIL:pin-identity-unchecked"];
            }
        }
    }
    const double timeout = CellTimeout();
    long shownFailures = 0;
    for (size_t i = 0; i < cells.size(); ++i)
    {
        if (only == -999999 && long(i % size_t(shards)) != shard) continue;
        const long p = cells[i];
        auto body = [&]() -> int {
            if (isCold) return RunColdCell(cold, p);
            if (family == "alloc") return RunAllocCell(c, p);
            if (family == "disasm") return RunDisasmCell(c, disasm, p);
            if (family == "cancel-oom") return RunAllocCell(c, p / 8 + kFailAfterCancel[p % 8], p / 8);
            if (family == "cancel-enter") return RunCancelCell(c, CancelHow::AtEnter, p);
            // Even p: the flag goes up in the hook before host check-point p/2; odd p: at the last allocation made before it.
            if (family == "cancel-bound")
                return p % 2 == 0 ? RunCancelCell(c, CancelHow::AtBoundary, p / 2) : RunCancelCell(c, CancelHow::AtAlloc, c.base.log.boundAlloc[p / 2] - 1);
            if (family == "cancel-alloc") return RunCancelCell(c, CancelHow::AtAlloc, p);
            return RunCancelCell(c, CancelHow::AtUs, p);
        };
        if (only != -999999) fa::gTrace = true;
        CellRun run = ForkCell(scratch, body, timeout);
        ++sum.cells;
        Verdict v = Classify(run.proc, run.out);
        if (v.kind.empty())
        {
            const std::string cls = CellClass(run.out);
            ++sum.classes[cls];
            // The accepted leaks of this cell (LeakCheck): kept visible, one count per row.
            for (const std::string& ln : Lines(run.out))
            {
                if (ln.compare(0, 14, "LEAK-ACCEPTED ") == 0)
                    ++sum.classes["leak-accepted:" + ln.substr(14, ln.rfind(" x") - 14)];
                if (ln.compare(0, 9, "LEAK-SIG ") == 0)
                    ++sum.classes["leak-sig " + ln.substr(9)];
            }
            if (run.out.find(" window=need") != std::string::npos) ++sum.classes["window:need"];
            if (run.out.find(" staging=left") != std::string::npos) ++sum.classes["staging-left"];   // 0 since A6-37; the cell itself fails when it is not
            if (isCold)
                for (const char* st : kLibrarySteps)
                    if (run.out.find(std::string(" step=") + st + " ") != std::string::npos) ++sum.classes[std::string("step:") + st];
            const size_t lp = run.out.find(" lat=");
            if (lp != std::string::npos)
            {
                const long lat = std::atol(run.out.c_str() + lp + 5);
                if (lat >= 0 && lat > sum.maxLatency[cls]) sum.maxLatency[cls] = lat;
            }
            if (only != -999999) std::printf("%s", run.out.c_str());
            continue;
        }
        ++sum.failed;
        ++sum.classes["FAIL:" + v.kind];
        SigStat& sig = sum.sigs[v.kind + ": " + v.sig];
        const bool firstOfSignature = ++sig.count == 1;
        if (firstOfSignature) sig.first = p;
        sig.last = p;
        if (family == "alloc") sig.phases.insert(PhaseOfAlloc(c, p));
        if (firstOfSignature || only != -999999)
        {
            // Run the cell again with the failing allocation's stack trace printed.
            std::string detail = run.out;
            if (family == "alloc" && only == -999999)
            {
                fa::gTrace = true;
                CellRun again = ForkCell(scratch, body, timeout);
                fa::gTrace = false;
                detail = again.out;
            }
            if (shownFailures++ < 40)
            {
                std::printf("FAIL %s %s %ld: %s\n", familyArg.c_str(), fixture.c_str(), p, (v.kind + ": " + v.sig).c_str());
                std::string cond = only == -999999 ? Condense(detail) : detail;
                for (const std::string& l : Lines(cond)) std::printf("    | %s\n", l.c_str());
            }
        }
        else if (shownFailures < 400)
        {
            std::printf("FAIL %s %s %ld: %s\n", familyArg.c_str(), fixture.c_str(), p, (v.kind + ": " + v.sig).c_str());
        }
    }
    if (family == "cancel-bound" && only == -999999 && shard == 0)
    {
        const int bad = BoundaryCheck(c);
        if (bad)
        {
            sum.failed += bad;
            sum.classes["FAIL:boundaries"] += bad;
        }
    }
    if (family == "cancel-enter" && only == -999999 && !sticky)
    {
        // The window is part of the design the oracle describes (A6-35): the host checks, and only then does the hook count the
        // entry. If no cell needs the widened oracle, either the family no longer reaches the window or the window is gone, and
        // ExpectedThread's extra entry is dead weight that hides a real misplaced cancel: the oracle must be tightened, not left.
        if (sum.classes["window:need"] == 0)
        {
            std::printf("FAIL %s %s: no cell raised in an entry's hook was honoured where only the widened (thread) oracle allows (the check-point window is not reached, or is gone)\n",
                        familyArg.c_str(), fixture.c_str());
            ++sum.failed;
            ++sum.classes["FAIL:window-coverage"];
        }
    }
    if (isCold && only == -999999)
    {
        // The sweep must have reached the class it exists for: a cell that failed inside startDecompilerLibrary and was
        // refused. (Without a backtrace it cannot say where a failure fell, and the class is then not demanded.)
#ifdef SE_FAULT_BACKTRACE
        if (shards == 1)
        {
            std::vector<std::string> missing;
            if (sum.classes["library-interrupted"] == 0) missing.push_back("any step");
            for (const char* st : kLibrarySteps)
                if (sum.classes[std::string("step:") + st] == 0) missing.push_back(st);
            if (!missing.empty())
            {
                std::string list;
                for (const std::string& m : missing) list += (list.empty() ? "" : ", ") + m;
                std::printf("FAIL %s %s: no cell failed inside startDecompilerLibrary at: %s (the library start-up is not covered)\n", family.c_str(),
                            fixture.c_str(), list.c_str());
                ++sum.failed;
                ++sum.classes["FAIL:cold-coverage"];
            }
        }
#endif
    }
    std::printf("SUMMARY %s %s shard %d/%d: %ld cells, %ld failed, %.1f s\n", familyArg.c_str(), fixture.c_str(), shard, shards, sum.cells,
                sum.failed, MsSince(t0) / 1000.0);
    for (const auto& kv : sum.classes) std::printf("CLASS %s %s %ld\n", familyArg.c_str(), kv.first.c_str(), kv.second);
    for (const auto& kv : sum.maxLatency)
        std::printf("LATENCY %s %s max %ld allocations of %ld\n", familyArg.c_str(), kv.first.c_str(), kv.second, isCold ? 0L : c.base.allocs);
    for (const auto& kv : sum.sigs)
    {
        std::string ph;
        for (const std::string& x : kv.second.phases) ph += (ph.empty() ? "" : ",") + x;
        std::printf("SIGNATURE %s | x%ld, %s %s k=%ld..%ld%s%s\n", kv.first.c_str(), kv.second.count, familyArg.c_str(), fixture.c_str(),
                    kv.second.first, kv.second.last, ph.empty() ? "" : " in ", ph.c_str());
    }
    return sum.failed ? 1 : 0;
}

// ================================================================================================
// Family 3: DecompilerRunner lifecycle
// ================================================================================================

// Holds the worker at the start of a chosen phase entry of a request (any phase, the reference
// run's included: the static hook sees them all), so the test acts while it is provably there.
class Gate
{
public:
    void Arm(long holdAt)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mHoldAt = holdAt;
        mReleased = false;
        mHolding = false;
        mOrdinal = 0;
        mLog.n = 0;
        mInRef = false;
    }
    void OnPhase(DecompilePhase p)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        const long o = mOrdinal++;
        if (o < Log::kMax)
        {
            mLog.phase[o] = p;
            mLog.allocAt[o] = 0;
            mLog.inRef[o] = mInRef;
            mLog.n = int(o) + 1;
        }
        if (o == mHoldAt && !mReleased)
        {
            mHolding = true;
            mChanged.notify_all();
            mChanged.wait(lock, [&] { return mReleased; });
            mHolding = false;
        }
    }
    void OnBeforeReference()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mInRef = true;
    }
    bool WaitHolding(double seconds)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        return mChanged.wait_for(lock, std::chrono::duration<double>(seconds), [&] { return mHolding; });
    }
    void Release()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mReleased = true;
        mChanged.notify_all();
    }
    Log LogCopy()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mLog;
    }

private:
    std::mutex mMutex;
    std::condition_variable mChanged;
    long mHoldAt = -1, mOrdinal = 0;
    bool mReleased = false, mHolding = false, mInRef = false;
    Log mLog{};
};
Gate gGate;

struct RunnerCtx
{
    std::string config;
    std::shared_ptr<const MemorySnapshot> snap;
    const Fix* fix = nullptr;
    DecompileRequest req;
    std::string baseSig;
    Log log;
    double requestMs = 0;   // a warm request through the runner, wall clock: budgets are set against it
};

RunnerOptions Generous()
{
    RunnerOptions o;
    o.budgetSeconds = 600;
    o.cancelAckSeconds = 600;
    return o;
}

bool WaitResult(DecompilerRunner& runner, DecompileResult& out, double seconds = 60.0)
{
    const auto t0 = Clock::now();
    while (MsSince(t0) < seconds * 1000.0)
    {
        if (runner.Poll(out)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// Polls as the UI would until the runner is idle; every result that comes back is returned.
std::vector<DecompileResult> DrainUntilIdle(DecompilerRunner& runner, double seconds = 60.0)
{
    std::vector<DecompileResult> got;
    const auto t0 = Clock::now();
    DecompileResult r;
    while (MsSince(t0) < seconds * 1000.0)
    {
        if (runner.Poll(r))
        {
            got.push_back(std::move(r));
            continue;
        }
        if (!runner.Busy()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    while (runner.Poll(r)) got.push_back(std::move(r));
    return got;
}

bool CheckNext(DecompilerRunner& runner, RunnerCtx& rc, const char* after)
{
    const uint64_t g = runner.Submit(5, rc.snap, rc.req);
    DecompileResult r;
    const bool got = WaitResult(runner, r);
    const bool ok = g != 0 && got && r.status == DecompileStatus::Ok && r.generation == g && Signature(r) == rc.baseSig;
    Expect(ok, "the next request after recovery is correct",
           std::string(after) + ": gen " + std::to_string(g) + " got " + std::to_string(got) + " status " + std::to_string(int(r.status)) +
               (got && r.status == DecompileStatus::Ok ? "; " + FirstDiff(rc.baseSig, Signature(r)) : std::string()));
    return ok;
}

void InstallGateHooks()
{
    Sh2Decompiler::SetPhaseHookForTests([](DecompilePhase p) {
        fa::tCounted = true;   // this is the worker thread
        gGate.OnPhase(p);
    });
    Sh2Decompiler::SetBeforeReferenceHookForTests([] { gGate.OnBeforeReference(); });
}

// The first requests of a runner child: the baseline text and the phase-entry log, through the
// runner itself.
bool RunnerBaseline(DecompilerRunner& runner, RunnerCtx& rc)
{
    std::string sig[2];
    for (int i = 0; i < 2; ++i)
    {
        gGate.Arm(-1);
        const auto tb = Clock::now();
        runner.Submit(0, rc.snap, rc.req);
        DecompileResult r;
        const bool done = WaitResult(runner, r);
        rc.requestMs = MsSince(tb);
        if (!done || r.status != DecompileStatus::Ok)
        {
            std::printf("the baseline request of '%s' did not complete Ok\n", rc.fix->name);
            return false;
        }
        sig[i] = Signature(r);
        rc.log = gGate.LogCopy();
    }
    rc.baseSig = sig[1];
    Expect(sig[0] == sig[1], "the first (cold) and second request give the same output");
    return true;
}

// The cancel oracle for an action taken while the worker is held at the start of entry 'e'.
void CheckOutcome(const DecompileResult& r, const RunnerCtx& rc, long e, const char* what, bool budget = false)
{
    const unsigned allowed = Expected(rc.log, int(e));
    unsigned got = Observed(r);
    if (budget && r.status == DecompileStatus::TooLarge)
    {
        // The budget turns a Cancelled result into TooLarge; the phase is not reported.
        got = allowed & (kCancelFlow | kCancelActions);
        Expect(allowed & (kCancelFlow | kCancelActions), "a budget cancel ends TooLarge only where a cancel would be honoured",
               std::string(what) + " held at entry " + std::to_string(e));
        return;
    }
    Expect(got != 0 && (got & allowed), "the cancel is honoured where the design says",
           std::string(what) + " held at entry " + std::to_string(e) + " (" + PhaseName(rc.log.phase[e]) + (rc.log.inRef[e] ? ", reference" : "") +
               "): expected " + MaskText(allowed) + "but status " + std::to_string(int(r.status)) + " cancelledIn " +
               PhaseName(r.cancelledIn) + " '" + r.error + "'");
    if (r.status == DecompileStatus::Cancelled) Expect(r.lines.empty(), "a cancelled result has no lines");
}

// Runs one action with the worker held at entry 'e'. Returns false when the worker never got there.
bool HoldAt(DecompilerRunner& runner, RunnerCtx& rc, long e, int slot, uint64_t& gen)
{
    gGate.Arm(e);
    gen = runner.Submit(slot, rc.snap, rc.req, true);   // retryTooLarge: a memo from an earlier expiry must not answer instead of the worker
    const bool held = gGate.WaitHolding(60.0);
    Expect(held && gen != 0, "the worker reaches the phase entry", "entry " + std::to_string(e));
    return held && gen != 0;
}

void ScenarioStop(DecompilerRunner& runner, RunnerCtx& rc, long e)
{
    uint64_t g = 0;
    if (!HoldAt(runner, rc, e, 0, g)) return;
    runner.Cancel(0);
    gGate.Release();
    DecompileResult r;
    const bool got = WaitResult(runner, r);
    Expect(got, "Stop: the result of a cancelled request comes back through Poll", "entry " + std::to_string(e));
    if (got)
    {
        Expect(r.generation == g && r.slot == 0, "Stop: the result is the request's own", "gen " + std::to_string(r.generation));
        CheckOutcome(r, rc, e, "Stop");
    }
    const auto more = DrainUntilIdle(runner);
    Expect(more.empty(), "Stop: exactly one result", std::to_string(more.size()) + " more");
    Expect(!runner.Busy() && !runner.Disabled(), "Stop: idle and enabled afterwards");
    CheckNext(runner, rc, "Stop");
}

void ScenarioSupersede(DecompilerRunner& runner, RunnerCtx& rc, long e)
{
    uint64_t g1 = 0;
    if (!HoldAt(runner, rc, e, 0, g1)) return;
    const uint64_t runs = runner.RunsStarted();
    const uint64_t g2 = runner.Submit(0, rc.snap, rc.req);   // while busy: cancels the first, queues the second
    Expect(g2 > g1, "Supersede: a newer generation");
    gGate.Release();
    const auto got = DrainUntilIdle(runner);
    int stale = 0, current = 0;
    for (const DecompileResult& r : got)
    {
        if (r.generation == g1) ++stale;
        if (r.generation == g2) ++current;
    }
    Expect(stale == 0, "Supersede: no result for the superseded generation is installed", "entry " + std::to_string(e));
    Expect(current == 1 && got.size() == 1, "Supersede: exactly the newest result comes back", std::to_string(got.size()) + " result(s)");
    if (current == 1)
        for (const DecompileResult& r : got)
            Expect(r.status == DecompileStatus::Ok && Signature(r) == rc.baseSig, "Supersede: the newest result is correct");
    Expect(runner.RunsStarted() == runs + 1, "Supersede: the second request ran once");
    // A different requester's queued request is cancelled by Cancel at once, without running.
    uint64_t g3 = 0;
    if (HoldAt(runner, rc, e, 0, g3))
    {
        const uint64_t q = runner.Submit(1, rc.snap, rc.req);
        runner.Cancel(1);
        DecompileResult r;
        const bool gotQ = WaitResult(runner, r, 10.0);
        Expect(gotQ && r.generation == q && r.slot == 1 && r.status == DecompileStatus::Cancelled,
               "Stop on a queued request answers Cancelled at once, for its own generation");
        gGate.Release();
        DrainUntilIdle(runner);
    }
    CheckNext(runner, rc, "Supersede");
}

void ScenarioInvalidate(DecompilerRunner& runner, RunnerCtx& rc, long e)
{
    uint64_t g = 0;
    if (!HoldAt(runner, rc, e, 0, g)) return;
    const uint64_t epoch = runner.Epoch();
    const uint64_t queued = runner.Submit(1, rc.snap, rc.req);   // dropped with the session
    runner.InvalidateSession();
    Expect(runner.Epoch() == epoch + 1, "Invalidate: the epoch moves");
    gGate.Release();
    const auto got = DrainUntilIdle(runner);
    Expect(got.empty(), "Invalidate: nothing from the old epoch is installed",
           "entry " + std::to_string(e) + ": generation " + (got.empty() ? std::string("-") : std::to_string(got[0].generation)) + " (queued was " +
               std::to_string(queued) + ")");
    Expect(!runner.Disabled(), "Invalidate: the session change does not disable");
    CheckNext(runner, rc, "Invalidate");
}

void ScenarioDestroy(const std::string& config, RunnerCtx& rc, long e)
{
    std::unique_ptr<DecompilerRunner> runner(new DecompilerRunner(config, Generous()));
    uint64_t g = 0;
    if (!HoldAt(*runner, rc, e, 0, g))
    {
        gGate.Release();
        return;
    }
    // The destructor waits for the worker; something else lets it go, as a user closing the window
    // while the engine is mid-phase would not, but the join must still complete once it moves.
    std::thread releaser([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        gGate.Release();
    });
    const auto t0 = Clock::now();
    runner.reset();
    const double ms = MsSince(t0);
    releaser.join();
    Expect(ms < 30000, "Destroy: the destructor joins the worker", std::to_string(ms) + " ms");
    // The engine claim is free and the engine is whole.
    DecompilerRunner next(config, Generous());
    Expect(!next.Disabled(), "Destroy: a new runner is not born disabled");
    gGate.Arm(-1);
    CheckNext(next, rc, "Destroy");
}

// Budget expiry with the worker held at entry 'e' for longer than the budget; Poll raises it.
void ScenarioBudget(const std::string& config, RunnerCtx& rc, long e)
{
    RunnerOptions o;
    // A request that is merely slow must not expire it: the follow-up request of a cell runs under the
    // same budget (the runner's options are fixed at construction), and under load (a parallel build, ctest -j) a
    // run takes several times its measured length. The DecompilerRunnerTests disable case had the same flake (A12-6).
    o.budgetSeconds = std::max(0.05, 12.0 * rc.requestMs / 1000.0);
    o.cancelAckSeconds = 600;
    const double spinMs = o.budgetSeconds * 1000.0 + 150.0;   // held for longer than the budget
    DecompilerRunner runner(config, o);
    gGate.Arm(-1);
    // The budget runs from the start of the request, a cold translator build included: warm the
    // worker first with a budget it cannot expire in (a fresh request has no hold).
    uint64_t g = 0;
    if (!HoldAt(runner, rc, e, 0, g)) return;
    const auto t0 = Clock::now();
    DecompileResult r;
    while (MsSince(t0) < spinMs) runner.Poll(r);   // Poll enforces the budget: the cancel goes up when it is spent
    gGate.Release();
    const auto got = DrainUntilIdle(runner);
    Expect(got.size() == 1 && got[0].generation == g, "Budget: exactly the request's result comes back",
           "entry " + std::to_string(e) + ": " + std::to_string(got.size()) + " result(s)");
    if (got.size() != 1) return;
    const DecompileResult& res = got[0];
    CheckOutcome(res, rc, e, "Budget", true);
    const uint64_t runs = runner.RunsStarted();
    const bool tooLarge = res.status == DecompileStatus::TooLarge;
    if (tooLarge)
    {
        char expected[80];
        std::snprintf(expected, sizeof expected, "too large to decompile within %g s", o.budgetSeconds);
        Expect(Has(res.error, expected), "Budget: the TooLarge text names the budget", res.error);
    }
    // The memo: the same entry, as asked, under the same volatile list, is answered at once ...
    gGate.Arm(-1);
    runner.Submit(0, rc.snap, rc.req);
    DecompileResult again;
    Expect(WaitResult(runner, again, 30.0), "Budget: the resubmitted request answers");
    if (tooLarge)
    {
        Expect(again.status == DecompileStatus::TooLarge && runner.RunsStarted() == runs,
               "Budget: a too-large entry is answered from the memo without a run", "status " + std::to_string(int(again.status)));
        // ... Refresh runs it again (no hold: it finishes inside the budget only if it can; the
        // budget may stop it again, which is also correct) ...
        runner.Submit(0, rc.snap, rc.req, true);
        Expect(WaitResult(runner, again, 30.0), "Budget: Refresh answers");
        Expect(runner.RunsStarted() == runs + 1, "Budget: Refresh (retryTooLarge) runs the entry again");
        // ... another mirror is another question ...
        DecompileRequest mirror = rc.req;
        mirror.entry |= 0x20000000u;
        runner.Submit(0, rc.snap, mirror);
        Expect(WaitResult(runner, again, 30.0), "Budget: the mirror answers");
        Expect(runner.RunsStarted() == runs + 2, "Budget: the same entry through a mirror is attempted, not answered from the memo");
        // ... and so is another volatile list.
        DecompileRequest vol = rc.req;
        vol.volatileMemory.emplace_back(0x26001000u, 0x26001003u);
        runner.Submit(0, rc.snap, vol);
        Expect(WaitResult(runner, again, 30.0), "Budget: the other volatile list answers");
        Expect(runner.RunsStarted() == runs + 3, "Budget: another volatile list is attempted, not answered from the memo");
        // A new session forgets the memo.
        runner.InvalidateSession();
        const uint64_t before = runner.RunsStarted();
        runner.Submit(0, rc.snap, rc.req);
        WaitResult(runner, again, 30.0);
        Expect(runner.RunsStarted() == before + 1, "Budget: InvalidateSession clears the too-large memo");
    }
    else
    {
        // Ok (the cancel came too late, or only cut the reference run short): not memoised.
        Expect(again.status == DecompileStatus::Ok || again.status == DecompileStatus::TooLarge, "Budget: a later request answers");
        Expect(runner.RunsStarted() == runs + 1, "Budget: an entry whose request finished Ok is not in the too-large memo",
               "entry " + std::to_string(e));
    }
    DrainUntilIdle(runner);
    // The same expiry, but the session is invalidated before the request stops: its result is not
    // installed and nothing is remembered as too large (the memo belongs to the session).
    gGate.Arm(-1);
    uint64_t g2 = 0;
    if (HoldAt(runner, rc, e, 0, g2))
    {
        const auto t1 = Clock::now();
        while (MsSince(t1) < spinMs) runner.Poll(r);
        runner.InvalidateSession();
        gGate.Release();
        const auto late = DrainUntilIdle(runner);
        Expect(late.empty(), "Budget: a request stopped by the budget in an invalidated session installs nothing",
               "entry " + std::to_string(e));
        const uint64_t before = runner.RunsStarted();
        gGate.Arm(-1);
        runner.Submit(0, rc.snap, rc.req);
        DecompileResult x;
        WaitResult(runner, x, 30.0);
        Expect(runner.RunsStarted() == before + 1, "Budget: nothing from the invalidated session is in the too-large memo",
               "entry " + std::to_string(e));
    }
    DrainUntilIdle(runner);
}

// DisableLocked and its recovery. 'invalidateFirst' picks which of the two documented routes back
// to service: a new session while the stuck request still runs, or after it ended.
void ScenarioDisable(const std::string& config, RunnerCtx& rc, long e, bool invalidateFirst)
{
    RunnerOptions o;
    o.budgetSeconds = std::max(0.05, 12.0 * rc.requestMs / 1000.0);   // the follow-up request runs under it too (see ScenarioBudget)
    o.cancelAckSeconds = 0.15;
    DecompilerRunner runner(config, o);
    uint64_t g = 0;
    if (!HoldAt(runner, rc, e, 0, g)) return;
    const uint64_t queued = runner.Submit(1, rc.snap, rc.req);   // dropped by the disable, never run
    (void)queued;
    DecompileResult r;
    const auto t0 = Clock::now();
    while (!runner.Disabled() && MsSince(t0) < 10000.0)
    {
        Expect(!runner.Poll(r), "Disable: nothing is installed while the engine is stuck");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Expect(runner.Disabled(), "Disable: an unacknowledged cancel disables decompilation", "entry " + std::to_string(e));
    Expect(runner.DisabledReason().find("disabled") != std::string::npos, "Disable: with a reason", runner.DisabledReason());
    Expect(runner.Submit(2, rc.snap, rc.req) == 0, "Disable: Submit is refused");
    const uint64_t runs = runner.RunsStarted();
    if (invalidateFirst)
    {
        runner.InvalidateSession();
        Expect(runner.Disabled(), "Disable: a new session does not re-enable while the stuck request still runs");
    }
    gGate.Release();
    const auto late = DrainUntilIdle(runner);
    Expect(late.empty(), "Disable: nothing the stuck request produced is installed", "entry " + std::to_string(e));
    Expect(runner.RunsStarted() == runs, "Disable: no request starts while disabled (the queued one is gone)");
    if (invalidateFirst)
    {
        Expect(!runner.Disabled(), "Disable: the new session decompiles again once the stuck request has ended");
    }
    else
    {
        Expect(runner.Disabled(), "Disable: the same session stays disabled after the stuck request ended");
        runner.InvalidateSession();
        Expect(!runner.Disabled(), "Disable: a new session re-enables at once when nothing is running");
    }
    gGate.Arm(-1);
    CheckNext(runner, rc, "Disable");
}

// The worker fails an allocation 'j' allocations after it is let go at phase entry 'e': the request
// is Failed (out of memory) and the next one Unavailable -- or, when the failure lands in the
// runner's own recording of the result, the session is disabled and no result comes back. The
// runner itself stays up either way. One failure damages the engine for the process, so each cell is
// a process of its own (--runner-oom).
int RunnerOomChild(const std::string& scratch, const std::string& fixture, long e, long j)
{
    dts::PrepareProcess();
    fa::tCounted = false;   // the test thread is not injected into
    std::atexit(OnExit);
    // SE_FAULT_STICKY=1 applies to this family too, as the header says of any family (A6-36): the worker then meets a heap
    // that stays full from allocation j on, not one that fails once. (The runner children never read it before.)
    {
        const char* st = std::getenv("SE_FAULT_STICKY");
        fa::gSticky = st && *st && std::string(st) != "0";
    }
    RunnerCtx rc;
    rc.config = dts::Join(scratch, "config");
    sfe::MakeDirectory(scratch);
    sfe::MakeDirectory(rc.config);
    rc.snap = BuildSnapshot();
    rc.fix = FindFix(fixture);
    if (!rc.fix) return 2;
    rc.req = MakeRequest(*rc.fix);
    InstallGateHooks();
    DecompilerRunner runner(rc.config, Generous());
    if (!RunnerBaseline(runner, rc)) return 2;
    if (e >= rc.log.n) return 0;   // past the request's last phase entry
    uint64_t g = 0;
    if (!HoldAt(runner, rc, e, 0, g)) return 2;
    const long refs = rc.snap.use_count();
    fa::Arm(j);
    gGate.Release();
    DecompileResult r;
    bool got = false;
    const auto t0 = Clock::now();
    while (MsSince(t0) < 60000.0 && !got && !runner.Disabled())
    {
        got = runner.Poll(r);
        if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // A request can also finish before the failure is reached (j beyond what is left of it).
    const bool fired = fa::gFired.load();
    fa::Disarm();
    std::string cls;
    if (got)
    {
        Expect(r.generation == g, "the result is the request's own");
        if (!fired)
        {
            cls = "no-failure";
            Expect(r.status == DecompileStatus::Ok || r.status == DecompileStatus::Cancelled, "a run with no failure injected completes");
        }
        else if (r.status == DecompileStatus::Failed && Has(r.error, "out of memory"))
            cls = "failed-oom";
        else if (r.status == DecompileStatus::Ok && fa::gFiredNothrow.load())
            cls = "ok-nothrow-fallback";
        else
        {
            cls = "wrong";
            Expect(false, "an allocation failure on the worker ends Failed(out of memory)",
                   "status " + std::to_string(int(r.status)) + " '" + r.error + "'");
        }
    }
    else
    {
        cls = "disabled-while-recording";
        Expect(runner.Disabled() && fired, "no result comes back only if the session was disabled by a failure",
               std::string("disabled=") + std::to_string(runner.Disabled()) + " fired=" + std::to_string(fired));
        Expect(!runner.Busy(), "the worker is idle after the failure");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    // The worker is alive and answers the next request, unless the session was disabled.
    bool damaged = false;
    if (!runner.Disabled())
    {
        runner.Submit(0, rc.snap, rc.req);
        DecompileResult after;
        const bool ok = WaitResult(runner, after, 60.0);
        Expect(ok, "the worker answers the next request after a failure");
        if (ok)
        {
            damaged = after.status == DecompileStatus::Unavailable;
            if (cls == "failed-oom") Expect(damaged, "after a failure inside the engine the next request is Unavailable", std::to_string(int(after.status)));
            else Expect(after.status == DecompileStatus::Ok && Signature(after) == rc.baseSig, "after no failure the next request is correct");
        }
    }
    else
        damaged = true;
    Expect(!runner.Disabled() || cls == "disabled-while-recording", "an engine failure does not disable the session by itself");
    Expect(rc.snap.use_count() <= refs, "the snapshot is not held by a dead job", std::to_string(refs) + " -> " + std::to_string(rc.snap.use_count()));
    std::printf("CELL runner-oom %s entry=%ld j=%ld class=%s next=%s\n", fixture.c_str(), e, j, cls.c_str(), damaged ? "Unavailable/disabled" : "ok");
    gMustNotExit = true;
    LeaveDamaged();
}

int RunnerChild(const std::string& scratch, const std::string& kind, const std::string& which, bool full)
{
    dts::PrepareProcess();
    RunnerCtx rc;
    rc.config = dts::Join(scratch, "config");
    sfe::MakeDirectory(scratch);
    sfe::MakeDirectory(rc.config);
    rc.snap = BuildSnapshot();
    InstallGateHooks();
    std::vector<const Fix*> fixtures = { FindFix("mailbox"), FindFix("jumptable") };
    if (full) fixtures.push_back(FindFix("fixture1"));
    const auto t0 = Clock::now();
    long cells = 0;
    const bool ownRunner = kind == "destroy" || kind == "budget" || kind == "disable";   // each cell builds its own
    for (const Fix* fix : fixtures)
    {
        rc.fix = fix;
        rc.req = MakeRequest(*fix);
        std::unique_ptr<DecompilerRunner> runner(new DecompilerRunner(rc.config, Generous()));
        if (!RunnerBaseline(*runner, rc)) return 2;
        const int n = rc.log.n;
        std::printf("runner %s on %s: %d phase entries\n", kind.c_str(), fix->name, n);
        for (long e = 0; e < n; ++e)
        {
            if (which != "all" && e != std::atol(which.c_str())) continue;
            // A scenario that builds its own runner pays a translator build per cell: the default
            // subset takes the first three entries and every second one after.
            if (!full && ownRunner && which == "all" && e >= 3 && (e % 2)) continue;
            std::printf("-- %s %s entry %ld (%s%s)\n", kind.c_str(), fix->name, e, PhaseName(rc.log.phase[e]), rc.log.inRef[e] ? ", reference" : "");
            ++cells;
            if (kind == "stop") ScenarioStop(*runner, rc, e);
            else if (kind == "supersede") ScenarioSupersede(*runner, rc, e);
            else if (kind == "invalidate") ScenarioInvalidate(*runner, rc, e);
            else if (ownRunner)
            {
                runner.reset();   // one runner at a time: the engine thread is per process
                if (kind == "destroy") ScenarioDestroy(rc.config, rc, e);
                else if (kind == "budget") ScenarioBudget(rc.config, rc, e);
                else
                {
                    ScenarioDisable(rc.config, rc, e, true);
                    ScenarioDisable(rc.config, rc, e, false);
                }
                runner.reset(new DecompilerRunner(rc.config, Generous()));
                gGate.Arm(-1);
            }
            else
            {
                std::printf("unknown scenario '%s'\n", kind.c_str());
                return 2;
            }
        }
        runner.reset();
    }
    std::printf("RUNNER %s: %ld cells, %ld failure(s), %.1f s\n", kind.c_str(), cells, long(dts::Failures()), MsSince(t0) / 1000.0);
    std::fflush(nullptr);
    std::_Exit(dts::Failures() ? kExitInvariant : LeakCheck());
}

}  // namespace

// ================================================================================================
// The parent
// ================================================================================================

namespace {

struct Job
{
    std::vector<std::string> args;
    std::string label, log;
    bool quiet = false;   // reported only when it fails (the many small cells)
    pid_t pid = -1;
    Clock::time_point start;
    ProcResult res;
};

int RunJobs(std::vector<Job>& jobs, size_t parallel, double timeoutSec, bool verbose)
{
    int failures = 0;
    size_t next = 0;
    std::vector<size_t> running;
    auto finish = [&](size_t i) {
        Job& j = jobs[i];
        const std::string out = ReadFile(j.log);
        const bool ok = j.res.exited && j.res.code == 0 && !j.res.timedOut;
        if (ok && j.quiet) return;
        std::printf("== %s: %s in %.1f s%s\n", j.label.c_str(), ok ? "ok" : "FAILED", MsSince(j.start) / 1000.0,
                    j.res.timedOut ? " (TIMED OUT)" : j.res.signal ? (" (signal " + std::to_string(j.res.signal) + ")").c_str() : "");
        // The summary lines always; the rest when it failed or when asked, up to a limit (the log
        // file holds all of it).
        int shown = 0, omitted = 0;
        for (const std::string& l : Lines(out))
        {
            const bool summary = l.compare(0, 8, "SUMMARY ") == 0 || l.compare(0, 6, "CLASS ") == 0 || l.compare(0, 10, "SIGNATURE ") == 0 ||
                                 l.compare(0, 8, "fixture ") == 0 || l.compare(0, 7, "RUNNER ") == 0 ||
                                 l.compare(0, 8, "LATENCY ") == 0 || l.compare(0, 4, "PIN ") == 0;
            if (summary || ((verbose || !ok) && shown < 60))
            {
                std::printf("   %s\n", l.c_str());
                if (!summary) ++shown;
            }
            else if (!ok)
                ++omitted;
        }
        if (omitted) std::printf("   ... %d more line(s) in the log\n", omitted);
        if (!ok) ++failures;
    };
    while (next < jobs.size() || !running.empty())
    {
        while (next < jobs.size() && running.size() < parallel)
        {
            Job& j = jobs[next];
            j.start = Clock::now();
            j.pid = SpawnSelf(j.args, j.log);
            running.push_back(next++);
        }
        for (size_t k = 0; k < running.size();)
        {
            Job& j = jobs[running[k]];
            int st = 0;
            const pid_t w = waitpid(j.pid, &st, WNOHANG);
            const bool late = MsSince(j.start) > timeoutSec * 1000.0;
            if (w == j.pid || late)
            {
                if (w != j.pid)
                {
                    kill(j.pid, SIGKILL);
                    waitpid(j.pid, &st, 0);
                    j.res.timedOut = true;
                }
                else if (WIFEXITED(st))
                {
                    j.res.exited = true;
                    j.res.code = WEXITSTATUS(st);
                }
                else if (WIFSIGNALED(st))
                {
                    j.res.signal = WTERMSIG(st);
                }
                finish(running[k]);
                running.erase(running.begin() + long(k));
            }
            else
                ++k;
        }
        usleep(5000);
    }
    return failures;
}


// A6-40: a refusal for a cause that is gone one request later. Failing to prepare the language files (a configuration directory that
// cannot be made, a file where a directory goes, a lock that cannot be taken, a rename that is blocked) writes nothing of the
// engine's, and its cause can be gone one request later (a full disk, a permission the user fixes): such a request is refused, and
// the next one starts again. Only a failure inside the library's own start-up is for the process, and it says that a restart is what
// clears it (the cold family). One process (the library's start-up is once per process): `<scratch> --refused-retry`, or the job
// "refused-retry" of the parent. The causes, in order, in one engine:
//   1. a regular file where the configuration directory goes          -> Unavailable, "cannot create ...cfg/decompiler"
//   2. the same again, and a Disassemble                              -> refused again with the same text (nothing is cached)
//   3. the file replaced by a directory that holds a file where
//      "decompiler" goes                                              -> Unavailable, the other path in the text
//   4. that file removed                                              -> Ok, with the text of a request that was never refused
//   5. a second request, and a Disassemble                            -> Ok (the library was started once, not again)
int RefusedRetryCell(const std::string& scratch)
{
    dts::Failures() = 0;
    sfe::MakeDirectory(scratch);
    const std::string cfg = dts::Join(scratch, "cfg");
    auto put = [](const std::string& path) {
        if (FILE* f = std::fopen(path.c_str(), "wb"))
        {
            std::fputs("x", f);
            std::fclose(f);
        }
    };
    put(cfg);
    const auto snap = BuildSnapshot();
    const Fix* fx = FindFix("mailbox");
    Sh2Decompiler engine(cfg);
    const DecompileRequest rq = MakeRequest(*fx);
    auto ask = [&](const char* what) {
        const DecompileResult r = engine.Decompile(snap, rq, nullptr);
        std::printf("%-8s: status %d '%s'\n", what, int(r.status), r.error.c_str());
        return r;
    };
    auto list = [&](const char* what) {
        bool stopped = false;
        const std::vector<std::string> lines = engine.Disassemble(snap, dts::kFixture1Caller, 3, &stopped);
        std::printf("%-8s: %zu line(s), first '%s'\n", what, lines.size(), lines.empty() ? "" : lines[0].c_str());
        return lines;
    };
    const DecompileResult first = ask("1 file");
    Expect(first.status == DecompileStatus::Unavailable && Has(first.error, "cannot create"),
           "the first request is refused, with the path: its configuration directory cannot be made", first.error);
    const DecompileResult again = ask("2 again");
    Expect(again.status == DecompileStatus::Unavailable && again.error == first.error,
           "the same cause is refused again, with the same text: nothing wedged and nothing cached", again.error);
    const std::vector<std::string> broken = list("2 disasm");
    Expect(broken.size() == 1 && broken[0].compare(0, 24, "<Decompiler unavailable:") == 0, "a listing in the same state is refused with the reason");

    sfe::RemoveFile(cfg);
    sfe::MakeDirectory(cfg);
    put(dts::Join(cfg, "decompiler"));   // another cause: a file where the decompiler's directory goes
    const DecompileResult third = ask("3 file2");
    Expect(third.status == DecompileStatus::Unavailable && third.error != first.error,
           "a different cause is a different refusal, not the first one's text", third.error);

    sfe::RemoveFile(dts::Join(cfg, "decompiler"));   // the cause is gone
    const DecompileResult fourth = ask("4 fixed");
    Expect(fourth.status == DecompileStatus::Ok && !fourth.Text().empty(),
           "a refusal whose cause is gone is not for the process: the next request decompiles (A6-40)",
           "status " + std::to_string(int(fourth.status)) + " '" + fourth.error + "'");
    const DecompileResult fifth = ask("5 again");
    Expect(fifth.status == DecompileStatus::Ok && Signature(fifth) == Signature(fourth), "and the one after it, the same", fifth.error);
    const std::vector<std::string> good = list("5 disasm");
    Expect(good.size() == 3 && !Has(good[0], "unavailable"), "and a listing is made");
    std::printf("RESULT refused-retry: %s\n", dts::Failures() ? "FAIL" : "ok");
    std::fflush(nullptr);
    const int leak = dts::Failures() ? 0 : LeakCheck();
    std::fflush(nullptr);
    std::_Exit(dts::Failures() ? kExitInvariant : leak);
}

}  // namespace

int main(int argc, char** argv)
{
    dts::PrepareProcess();
    {
        char buf[4096];
        const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
        if (n > 0)
        {
            buf[n] = 0;
            gSelf = buf;
        }
        else
            gSelf = argv[0];
    }
    auto arg = [&](int i) -> std::string { return i < argc ? std::string(argv[i]) : std::string(); };

    // ---- spawned by the parent: a driver over one family and fixture ...
    // --driver <family> <fixture> <scratch> <full 0|1> <shard> <shards>
    if (arg(1) == "--driver" && argc >= 8)
        return Driver(arg(2), arg(3), arg(4), arg(5) == "1", std::atoi(argv[6]), std::atoi(argv[7]), -999999);
    // ... or the refused-retry cell (A6-40) in a process of its own: --refused-retry <scratch>
    if (arg(1) == "--refused-retry" && argc >= 3) return RefusedRetryCell(arg(2));
    // ... or one worker allocation-failure cell: --runner-oom <fixture> <scratch> <entry> <j>
    if (arg(1) == "--runner-oom" && argc >= 6) return RunnerOomChild(arg(3), arg(2), std::atol(argv[4]), std::atol(argv[5]));
    // ... or the runner scenarios of one kind: --runner <kind> <scratch> <entry|all> <full 0|1>
    if (arg(1) == "--runner" && argc >= 6) return RunnerChild(arg(3), arg(2), arg(4), arg(5) == "1");

    if (argc < 2)
    {
        std::printf("usage: %s <scratch dir> [--cell <family> <fixture|kind> <k>]\n", argv[0]);
        return 2;
    }
    const std::string scratch = argv[1];
    dts::RemoveTree(scratch);
    sfe::MakeDirectory(scratch);

    // ---- the allocations of one size in one phase entry, with their stacks (how a pin's site is found)
    if (arg(2) == "--sites" && argc >= 7)
        return SitesTool(arg(3), dts::Join(scratch, "cell"), arg(4), std::atoi(argv[5]), uint32_t(std::atol(argv[6])), argc >= 8 ? argv[7] : nullptr);

    if (arg(2) == "--diffruns" && argc >= 4) return DiffRunsTool(arg(3), dts::Join(scratch, "cell"), argc >= 5 ? std::atoi(argv[4]) : 50);

    // ---- the refusal for a cause that is gone (A6-40), a process of its own
    if (arg(2) == "--refused-retry") return RefusedRetryCell(dts::Join(scratch, "cell"));

    // ---- the baseline result of a fixture (what kReach's evidence is written from)
    if (arg(2) == "--show" && argc >= 4)
    {
        Ctx c;
        if (!Warm(c, dts::Join(scratch, "cell"), arg(3))) return 2;
        std::printf("%s\n", c.base.sig.c_str());
        return 0;
    }

    // ---- one cell, in this process's driver, with the failing allocation's trace
    if (arg(2) == "--cell" && argc >= 6)
    {
        const std::string family = arg(3);
        if (family == "runner") return RunnerChild(dts::Join(scratch, "cell"), arg(4), arg(5), true);
        if (family == "runner-oom" && argc >= 7) return RunnerOomChild(dts::Join(scratch, "cell"), arg(4), std::atol(argv[5]), std::atol(argv[6]));
        return Driver(family, arg(4), dts::Join(scratch, "cell"), true, 0, 1, std::atol(argv[5]));
    }

    // ---- the parent: every driver and scenario as a child, in the default subset or in full
    const bool full = Full();
    const size_t jobsParallel = size_t(std::max(1.0, EnvDouble("SE_FAULT_JOBS", full ? 3.0 : 2.0)));
    const double timeout = full ? 4 * 3600.0 : 600.0;
    const std::string logs = dts::Join(scratch, "logs");
    sfe::MakeDirectory(logs);
    std::vector<Job> jobs;
    auto addDriver = [&](const char* family, const char* fixture, int shards) {
        for (int s = 0; s < shards; ++s)
        {
            Job j;
            j.label = std::string(family) + " " + fixture + (shards > 1 ? " shard " + std::to_string(s) + "/" + std::to_string(shards) : "");
            j.log = dts::Join(logs, std::string(family) + "_" + fixture + "_" + std::to_string(s) + ".log");
            j.args = { "--driver", family, fixture, dts::Join(scratch, "d" + std::to_string(jobs.size())), full ? "1" : "0",
                       std::to_string(s), std::to_string(shards) };
            jobs.push_back(j);
        }
    };
    auto addRunner = [&](const char* kind) {
        Job j;
        j.label = std::string("runner ") + kind;
        j.log = dts::Join(logs, std::string("runner_") + kind + ".log");
        j.args = { "--runner", kind, dts::Join(scratch, "r" + std::to_string(jobs.size())), "all", full ? "1" : "0" };
        jobs.push_back(j);
    };
    const int shards = full ? int(std::max(1.0, EnvDouble("SE_FAULT_JOBS", 3.0))) : 1;
    // The fixtures added for coverage (A6-36): the lending paths and the hardware notes, which no other fixture reaches.
    auto addedForCoverage = [](const Fix& f) { return f.lend != 0 || std::string(f.name) == "hwreg" || std::string(f.name) == "wide" || std::string(f.name) == "div3" ||
                                      std::string(f.name) == "ext" || std::string(f.name) == "nest" || std::string(f.name) == "alias"; };
    // The allocation sweeps run on every fixture, so that a resolved call (fixture1), a loop merge (shapes) and a
    // stack local (stack) see a failed allocation as the mailbox and the switch do (A6-13). diamonds, with 411,659
    // allocations, is left to the full run.
    for (const Fix& f : kFixtures)
        if (full || std::string(f.name) != "diamonds") addDriver("alloc", f.name, shards);
    // The cancel families: the default subset keeps the four fixtures it always had; shapes and stack, added for
    // the allocation sweeps, join them in the full run (under a sanitizer they would add about two minutes).
    auto cancelFixture = [&](const Fix& f) {
        return full || (std::string(f.name) != "shapes" && std::string(f.name) != "stack" && std::string(f.name) != "loopbrk" &&
                        std::string(f.name) != "dag" && !addedForCoverage(f));
    };
    // cancel-enter is a handful of cells per fixture, so the fixtures added for coverage (A6-36) join it in the default subset.
    for (const Fix& f : kFixtures)
        if (cancelFixture(f) || addedForCoverage(f)) addDriver("cancel-enter", f.name, 1);
    // The host's own check-points, a flag raised exactly before each and at the last allocation before it (A6-41): a handful of
    // cells per fixture, on the same fixtures.
    for (const Fix& f : kFixtures)
        if (cancelFixture(f) || addedForCoverage(f)) addDriver("cancel-bound", f.name, 1);
    for (const Fix& f : kFixtures)
    {
        if (!full && std::string(f.name) == "diamonds") continue;
        if (cancelFixture(f)) addDriver("cancel-alloc", f.name, full ? shards : 1);
    }
    for (const Fix& f : kFixtures)
        if (cancelFixture(f)) addDriver("cancel-us", f.name, 1);
    for (const Fix& f : kFixtures)
        if (full || (std::string(f.name) != "diamonds" && !addedForCoverage(f))) addDriver("cancel-oom", f.name, full ? shards : 1);
    addDriver("disasm", "fixture1", full ? shards : 1);
    // A request refused for a cause that is gone one request later is answered again (A6-40): a process of its own.
    {
        Job j;
        j.label = "refused-retry";
        j.log = dts::Join(logs, "refused-retry.log");
        j.args = { "--refused-retry", dts::Join(scratch, "rr" + std::to_string(jobs.size())) };
        jobs.push_back(j);
    }
    // The pins' own verdicts, on pins written wrong (A6-34): a MOVED pin fails the run, an ordinal the site lacks is STALE.
    addDriver("pin-selftest", "dag", 1);
    // The first request of a process (A6-30): fresh processes, so no warm-up and no sharing with the families above.
    addDriver("cold", "mailbox", 1);
    addDriver("cold-disasm", "mailbox", 1);
    // ... and under sustained exhaustion (A6-31, A6-32): the first request's refusal text and the listing's flag need no
    // allocation, so they hold when every allocation from k on fails. The library's allocations are thinned (ColdCells).
    addDriver("cold-sticky", "mailbox", 1);
    addDriver("cold-disasm-sticky", "mailbox", 1);
    addDriver("disasm-sticky", "fixture1", full ? shards : 1);   // A6-23: every allocation from k on fails
    // Sustained exhaustion on the decompilation itself: the three fixtures with the longest recovery paths in the default
    // subset, all of them in the full run.
    for (const Fix& f : kFixtures)
        if (full ? std::string(f.name) != "diamonds" : (std::string(f.name) == "mailbox" || std::string(f.name) == "jumptable" || std::string(f.name) == "dag" ||
                                                                           std::string(f.name) == "lendx" || std::string(f.name) == "hwreg"))
            addDriver("alloc-sticky", f.name, full ? shards : 1);
    for (const char* k : { "stop", "supersede", "invalidate", "destroy", "budget", "disable" }) addRunner(k);
    // The worker failing an allocation, a process per cell.
    size_t oomFirst = jobs.size();
    {
        const std::vector<long> offsets = full ? std::vector<long>{ 0, 1, 2, 5, 20, 100, 1000, 5000, 9000 } : std::vector<long>{ 0, 40 };
        for (const char* f : { "mailbox", "jumptable" })
        {
            if (!full && std::string(f) != "mailbox") continue;
            const long entries = std::string(f) == "mailbox" ? 14 : 9;
            for (long e = 0; e < (full ? entries : 2); ++e)
                for (long j : offsets)
                {
                    Job job;
                    job.quiet = true;
                    job.label = std::string("runner-oom ") + f + " entry " + std::to_string(e) + " j " + std::to_string(j);
                    job.log = dts::Join(logs, "runner-oom_" + std::string(f) + "_" + std::to_string(e) + "_" + std::to_string(j) + ".log");
                    job.args = { "--runner-oom", f, dts::Join(scratch, "o" + std::to_string(jobs.size())), std::to_string(e), std::to_string(j) };
                    jobs.push_back(job);
                }
        }
    }
    size_t oomCount = jobs.size() - oomFirst;
    // SE_FAULT_ONLY=alloc,runner-oom ...: only the jobs whose label starts with one of these.
    if (const char* only = std::getenv("SE_FAULT_ONLY"))
    {
        std::vector<std::string> keep;
        std::istringstream in(only);
        for (std::string w; std::getline(in, w, ',');) keep.push_back(w);
        std::vector<Job> kept;
        oomCount = 0;
        for (const Job& j : jobs)
            for (const std::string& k : keep)
                if (!k.empty() && j.label.compare(0, k.size(), k) == 0)
                {
                    kept.push_back(j);
                    if (j.label.compare(0, 10, "runner-oom") == 0) ++oomCount;
                    break;
                }
        jobs.swap(kept);
    }

    const auto t0 = Clock::now();
    std::printf("fault matrix: %zu job(s), %zu in parallel, %s\n", jobs.size(), jobsParallel,
                full ? "FULL (SE_FAULT_FULL=1)" : "default subset");
    const int failures = RunJobs(jobs, jobsParallel, timeout, false);
    std::printf("runner-oom: %zu cells (a process each; only failures are listed above)\n", oomCount);
    std::printf("fault matrix: %d of %zu job(s) failed, %.1f s\n", failures, jobs.size(), MsSince(t0) / 1000.0);
    const char* keep = std::getenv("SE_FAULT_KEEP");
    if (!(keep && *keep && std::string(keep) != "0") && !failures) dts::RemoveTree(scratch);
    else std::printf("logs kept in %s\n", logs.c_str());
    return failures ? 1 : 0;
}

#endif   // SE_FAULT_POSIX
