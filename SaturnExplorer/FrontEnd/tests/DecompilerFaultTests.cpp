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
//             the one the design gives for where the flag went up (see Expected), a cancelled result
//             carries no lines, and the same Sh2Decompiler then produces exactly the uncancelled output.
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
//          cancel-alloc, cancel-us, cancel-oom or runner (then <kind> is stop, supersede, invalidate, destroy,
//          budget, disable or memo and <k> a phase-entry ordinal, or "all")
//        environment: SE_FAULT_FULL, SE_FAULT_DENSE (alloc: size of the dense prefix, 3000),
//          SE_FAULT_JOBS (parallel drivers, 1), SE_FAULT_TIMEOUT (seconds per cell, 120),
//          SE_FAULT_KEEP=1 (keep the drivers' logs in <scratch>/logs), SE_FAULT_ONLY=alloc,cancel-enter,...
//          (only the jobs whose label starts with one of these), SE_FAULT_EXIT=static (a damaged engine
//          leaves through SleighArchitecture::shutdown() and exit() instead of _Exit)
#include "DecompilerTestSupport.h"

#if defined(__unix__) || defined(__APPLE__)
#define SE_FAULT_POSIX 1
#endif

#ifndef SE_FAULT_POSIX

int main()
{
    std::printf("DecompilerFaultTests: SKIPPED (the matrix forks a process per cell; POSIX only)\n");
    return 0;
}

#else

#include "Decompiler/DecompilerRunner.h"
#include "Decompiler/SaturnArchitecture.h"
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

#if defined(__GLIBC__)
#include <execinfo.h>
#define SE_FAULT_BACKTRACE 1
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

    void Reset()
    {
        enters = 0;
        inRefNow = false;
        engineEntered = false;
        cancelAtEnter = -2;
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
std::atomic<bool> gTrace(false);

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
    void* frames[40];
    const int n = backtrace(frames, 40);
    backtrace_symbols_fd(frames, n, 2);
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
    if (i == gCancelAt.load(std::memory_order_relaxed))
    {
        gProbe.setOrdinal = gProbe.enters.load() - 1;
        if (std::atomic<bool>* f = gProbe.cancelFlag.load()) f->store(true);
    }
    if (i == gFailAt.load(std::memory_order_relaxed))
    {
        gFired = true;
        gFiredNothrow = nothrow;
        gFiredSize = long(size);
        if (gTrace) Trace();
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
};

const uint32_t kMailboxBase = 0x0600A480u;
const Fix kFixtures[] = {
    { "mailbox", kMailboxBase, false },        // TestHardwareVolatility's mailboxFold: code is removed, the reference run happens
    { "jumptable", dts::kJumpTableBase, false },   // a 4-way switch recovered through its table
    { "fixture1", dts::kFixture1Caller, false },   // a call through a literal pool: the second pass names the callee
    { "diamonds", dts::kDiamondsBase, true },      // 150 branches: a long flow and a long action tree
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
    dts::PlaceFixture1(hw);
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
// Sh2Decompiler.cpp's checkCancel says a cancel in a phase is honoured in that phase; the one
// exception is the Actions of the first pass, which does not print and has no check-point after
// its action tree (the cancel lands in the second pass's Flow).
unsigned Expected(const Log& L, int s)
{
    if (s < 0) return kCancelFlow;   // up before the run: the first Flow
    const DecompilePhase p = L.phase[s];
    if (p == DecompilePhase::Flow) return L.inRef[s] ? kOkRefCut : kCancelFlow;
    if (p == DecompilePhase::Actions)
    {
        const bool prints = s + 1 < L.n && L.phase[s + 1] == DecompilePhase::Print;
        if (prints) return L.inRef[s] ? kOkRefCut : kCancelActions;
        return kCancelActions | kCancelFlow;
    }
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
    // forks from this state, so its run is the next in the series.
    long prevCount = -1;
    std::string prevSig;
    Log prevLog{};
    bool settled = false;
    std::string series;
    for (int i = 0; i < 12 && !settled; ++i)
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
        bool same = i > 0 && n == prevCount && sig == prevSig && log.n == prevLog.n;
        for (int k = 0; same && k < log.n; ++k) same = log.phase[k] == prevLog.phase[k] && log.allocAt[k] == prevLog.allocAt[k];
        prevCount = n;
        prevSig = sig;
        prevLog = log;
        if (same)
        {
            settled = true;
            c.base.sig = sig;
            c.base.allocs = n;
            c.base.log = log;
            c.base.ms = ms;
            c.base.sizes.assign(fa::gSizeLog, fa::gSizeLog + std::min(n, fa::kSizeLogMax));
        }
    }
    if (!settled)
    {
        std::printf("the allocation count of '%s' does not settle (%s): the matrix would be meaningless\n", fixture.c_str(), series.c_str());
        return false;
    }
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

int LeakCheck()
{
#ifdef SE_FAULT_ASAN
    // SE_FAULT_LEAKS=0 skips the per-cell leak check (it dominates a sanitizer sweep's time, and a
    // sweep for memory errors does not need it; run with ASAN_OPTIONS=detect_leaks=0 too).
    const char* v = std::getenv("SE_FAULT_LEAKS");
    if (v && v[0] == '0') return 0;
    // _Exit skips the sanitizer's exit-time check, so make it now.
    if (__lsan_do_recoverable_leak_check()) return kExitLeak;
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

    std::string cls;
    Expect(!escaped, "no exception escapes Decompile");
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

enum class CancelHow { AtEnter, AtAlloc, AtUs };

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
    const Log log = gProbe.Snapshot();
    const bool raised = flag.load();

    int a = -2, b = -2;
    if (how == CancelHow::AtEnter)
        a = b = int(param);
    else if (how == CancelHow::AtAlloc)
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
        // The oracle reads the baseline log (the cancelled run's is a prefix of it).
        allowed = a >= c.base.log.n ? kOk : ExpectedRange(c.base.log, std::max(a, -1), std::min(b, c.base.log.n - 1));
        if (b >= c.base.log.n) allowed |= kOk;
        flagged = a < 0 ? "before the run" : std::string("in ") + PhaseName(c.base.log.phase[std::min(a, c.base.log.n - 1)]) +
                                                 (c.base.log.inRef[std::min(a, c.base.log.n - 1)] ? "(reference)" : "") +
                                                 "#" + std::to_string(a) + (b != a ? "..#" + std::to_string(b) : std::string());
    }
    const unsigned got = Observed(r);
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

    const char* hows[] = { "enter", "alloc", "us" };
    std::printf("CELL cancel-%s %s p=%ld raised=%s got=%s allowed=%s class=%s>%s lat=%ld\n", hows[int(how)], c.fix->name, param,
                flagged.c_str(), MaskText(got).c_str(), MaskText(allowed).c_str(), label.c_str(), MaskTag(got).c_str(), latency);
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
// A6-5), in every run of the matrix, the default subset included: the allocation of 'size' bytes,
// 'k' in the build that found it, in 'phase'. A different build or engine may number allocations
// differently, so a pin is found again by (size, phase) near its old index; one that no longer
// resolves is reported, so that it is re-pinned rather than silently dropped.
struct Pin
{
    const char* fixture;
    long k;
    uint32_t size;
    const char* phase;
    const char* what;
};
const Pin kPins[] = {
    { "mailbox", 21058, 48, "print", "A6-1 EmitPrettyPrint::setMarkup(true): new EmitMarkup after the old emitter was freed" },
    { "mailbox", 21074, 40, "print", "A6-1 EmitPrettyPrint::setMarkup(false): new EmitNoMarkup after the old emitter was freed" },
    { "mailbox", 21060, 16, "print", "A6-2 EmitMarkup::setPackedOutput: new PackedEncode after the old encoder was freed" },
    { "jumptable", 11342, 16, "actions", "A6-3 BlockGraph::identifyInternal: list growth fails with blocks half moved (newBlockSwitch)" },
    { "jumptable", 23628, 16, "actions", "A6-3 BlockGraph::identifyInternal, second pass" },
    { "mailbox", 21067, 513, "print", "A6-4 the markup stream swallows the failure, the truncated markup fails to parse: 'unexpected exception'" },
    { "mailbox", 18426, 513, "actions", "A6-5 the warning text stream swallows the failure: a truncated warning, status Ok" },
    { "jumptable", 8305, 513, "followFlow", "A6-5 the jump-table message stream swallows the failure, status Ok" },
};

const char* PhaseOfK(const Ctx& c, long k)
{
    const char* where = "before the engine";
    for (int i = 0; i < c.base.log.n; ++i)
        if (c.base.log.allocAt[i] <= k) where = PhaseName(c.base.log.phase[i]);
    return where;
}

long ResolvePin(const Ctx& c, const Pin& p)
{
    auto matches = [&](long k) {
        return k >= 0 && k < long(c.base.sizes.size()) && c.base.sizes[size_t(k)] == p.size && std::string(PhaseOfK(c, k)) == p.phase;
    };
    if (matches(p.k)) return p.k;
    for (long d = 1; d <= 400; ++d)
    {
        if (matches(p.k - d)) return p.k - d;
        if (matches(p.k + d)) return p.k + d;
    }
    return -1;
}

std::vector<long> AllocCells(const Ctx& c, bool full)
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
    for (const Pin& p : kPins)
        if (c.fix->name == std::string(p.fixture)) add(ResolvePin(c, p));   // -1 (stale) is skipped; the driver reports it
    add(N);       // one past the last: nothing fails, the run must equal the baseline
    add(N + 5);
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
    const long step = full ? 100 : std::max(1000L, lenUs / 24 / 1000 * 1000);
    for (long t = 0; t <= lenUs; t += step) v.push_back(t);
    return v;
}

int Driver(const std::string& family, const std::string& fixture, const std::string& scratch, bool full, int shard, int shards,
           long only)
{
    Ctx c;
    if (!Warm(c, scratch, fixture)) return 2;
    std::vector<long> cells;
    if (family == "alloc") cells = AllocCells(c, full);
    else if (family == "cancel-enter") cells = EnterCells(c, full);
    else if (family == "cancel-alloc") cells = CancelAllocCells(c, full);
    else if (family == "cancel-us") cells = UsCells(c, full);
    else if (family == "cancel-oom") cells = CancelOomCells(c, full);
    else
    {
        std::printf("unknown family '%s'\n", family.c_str());
        return 2;
    }
    if (only != -999999) cells.assign(1, only);
    const auto t0 = Clock::now();
    DriverSummary sum;
    if (family == "alloc" && only == -999999 && shard == 0)
    {
        for (const Pin& p : kPins)
        {
            if (fixture != p.fixture) continue;
            const long k = ResolvePin(c, p);
            std::printf("PIN %s %s: allocation %ld (%u bytes, %s) -> %s\n", p.fixture, p.what, k, p.size, p.phase,
                        k == p.k ? "as found" : k < 0 ? "STALE" : "moved");
            if (k < 0)
            {
                ++sum.failed;
                ++sum.classes["FAIL:stale-pin"];
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
            if (family == "alloc") return RunAllocCell(c, p);
            if (family == "cancel-oom") return RunAllocCell(c, p / 8 + kFailAfterCancel[p % 8], p / 8);
            if (family == "cancel-enter") return RunCancelCell(c, CancelHow::AtEnter, p);
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
                std::printf("FAIL %s %s %ld: %s\n", family.c_str(), fixture.c_str(), p, (v.kind + ": " + v.sig).c_str());
                std::string cond = only == -999999 ? Condense(detail) : detail;
                for (const std::string& l : Lines(cond)) std::printf("    | %s\n", l.c_str());
            }
        }
        else if (shownFailures < 400)
        {
            std::printf("FAIL %s %s %ld: %s\n", family.c_str(), fixture.c_str(), p, (v.kind + ": " + v.sig).c_str());
        }
    }
    std::printf("SUMMARY %s %s shard %d/%d: %ld cells, %ld failed, %.1f s\n", family.c_str(), fixture.c_str(), shard, shards, sum.cells,
                sum.failed, MsSince(t0) / 1000.0);
    for (const auto& kv : sum.classes) std::printf("CLASS %s %s %ld\n", family.c_str(), kv.first.c_str(), kv.second);
    for (const auto& kv : sum.maxLatency)
        std::printf("LATENCY %s %s max %ld allocations of %ld\n", family.c_str(), kv.first.c_str(), kv.second, c.base.allocs);
    for (const auto& kv : sum.sigs)
    {
        std::string ph;
        for (const std::string& x : kv.second.phases) ph += (ph.empty() ? "" : ",") + x;
        std::printf("SIGNATURE %s | x%ld, %s %s k=%ld..%ld%s%s\n", kv.first.c_str(), kv.second.count, family.c_str(), fixture.c_str(),
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
           std::string(after) + ": gen " + std::to_string(g) + " got " + std::to_string(got) + " status " + std::to_string(int(r.status)));
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
        runner.Submit(0, rc.snap, rc.req);
        DecompileResult r;
        if (!WaitResult(runner, r) || r.status != DecompileStatus::Ok)
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
    gen = runner.Submit(slot, rc.snap, rc.req);
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
    o.budgetSeconds = 0.05;
    o.cancelAckSeconds = 600;
    DecompilerRunner runner(config, o);
    gGate.Arm(-1);
    // The budget runs from the start of the request, a cold translator build included: warm the
    // worker first with a budget it cannot expire in (a fresh request has no hold).
    uint64_t g = 0;
    if (!HoldAt(runner, rc, e, 0, g)) return;
    const auto t0 = Clock::now();
    DecompileResult r;
    while (MsSince(t0) < 200.0) runner.Poll(r);   // Poll enforces the budget: 50 ms in, the cancel goes up
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
        while (MsSince(t1) < 200.0) runner.Poll(r);
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
    o.budgetSeconds = 0.05;
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
    for (const char* f : { "mailbox", "jumptable" }) addDriver("alloc", f, shards);
    for (const Fix& f : kFixtures) addDriver("cancel-enter", f.name, 1);
    for (const Fix& f : kFixtures)
    {
        if (!full && std::string(f.name) == "diamonds") continue;
        addDriver("cancel-alloc", f.name, full ? shards : 1);
    }
    for (const Fix& f : kFixtures) addDriver("cancel-us", f.name, 1);
    for (const char* f : { "mailbox", "jumptable" }) addDriver("cancel-oom", f, full ? shards : 1);
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
