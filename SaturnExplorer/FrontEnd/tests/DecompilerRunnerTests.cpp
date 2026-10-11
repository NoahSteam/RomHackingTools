// DecompilerRunner on the real engine (PLAN.md A4), with the cancellation spike's three large
// fixtures (diamonds: 6k instructions of branches; straight: 15k with none; jumptable: a 16-way
// switch over 14.5k):
//
//   - a cancel requested while the worker is provably in flow following is caught there, and the
//     same for actions -- both at the start of the phase and halfway through it;
//   - a cancel requested during print (no check-point) completes normally with unchanged output;
//   - a 100 ms wall-clock budget cancels the 6k-instruction fixture, which is then not retried;
//   - an epoch bump while a request runs: Poll returns at once and the late result is dropped;
//   - coalescing: a queued request replaced by a newer one from the same requester never runs;
//   - a cancel that is not acknowledged disables decompilation, with a reason, and no new worker;
//   - the destructor with a request in flight returns after the join and shutdown();
//   - after each of these, a following request completes with correct output.
//
// "Provably in phase X": the engine reports each phase as it begins. Either the observer holds the
// worker at the start of X while the cancel is requested (deterministic), or the test waits
// until X began, sleeps half its baseline length, and reads the phase before and after setting the
// flag; only when both reads say X does the attempt count, else it is retried.
//
// usage: SaturnExplorerDecompilerRunnerTests <scratch dir>
#include "DecompilerTestSupport.h"

#include "AllocFailure.h"
#include "Decompiler/DecompilerRunner.h"
#include "Decompiler/SaturnArchitecture.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace sfe::decomp;
using dts::MsSince;
using Clock = std::chrono::steady_clock;

namespace {

std::string gConfig;
std::shared_ptr<const MemorySnapshot> gSnap;

struct Fixture { const char* name; uint32_t entry; };
const Fixture kFixtures[] = {
    { "diamonds", dts::kDiamondsBase },
    { "straight", dts::kStraightBase },
    { "jumptable", dts::kJumpTableBase },
};

// The spike's sizes, except in an MSVC Debug build. There the Debug CRT's checked iterators run
// inside every STL call the engine inlines -- the engine is built /O2, but _ITERATOR_DEBUG_LEVEL
// must match across the whole executable -- and the 6k-instruction fixture takes over 20 s
// instead of about one. A tenth of each fixture keeps every phase long enough to cancel inside
// (followFlow tens of ms, actions over a second there) within the ctest timeout.
#if defined(_MSC_VER) && defined(_DEBUG)
const int kFixtureDivisor = 10;
#else
const int kFixtureDivisor = 1;
#endif

std::shared_ptr<const MemorySnapshot> BuildSnapshot()
{
    std::vector<uint8_t> hw;
    dts::PlaceFixture1(hw);
    dts::PlaceDiamonds(hw, 1000 / kFixtureDivisor);
    dts::PlaceStraight(hw, 5000 / kFixtureDivisor);
    dts::PlaceJumpTable(hw, 16, 300 / kFixtureDivisor);
    return dts::MakeSnapshot(hw);
}

// Every runner here except the budget test's: the default 10 s budget is for users, and a slow
// runner (Rosetta, a sanitizer build) must not turn a timing-insensitive case into a budget case.
RunnerOptions Generous()
{
    RunnerOptions o;
    o.budgetSeconds = 120;
    o.cancelAckSeconds = 60;   // one action's body alone can outlast 5 s in a slow build
    return o;
}

// One pass, so each phase happens once per request and "the phase" is unambiguous.
DecompileRequest Big(uint32_t entry)
{
    DecompileRequest q;
    q.entry = entry;
    q.registerCallees = false;
    q.maxInstructions = 100000;   // the fixtures are larger than the 20 000 default cap
    return q;
}

DecompileRequest Small()
{
    DecompileRequest q;
    q.entry = dts::kFixture1Caller;
    return q;
}

// The engine's phase as the observer last saw it, and a way to hold the worker at the start of a
// chosen phase.
class PhaseGate
{
public:
    void Observe(DecompilePhase p)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mCurrent = p;
        ++mEntered[int(p)];
        mEnteredAt[int(p)] = Clock::now();
        mChanged.notify_all();
        if (p == mHoldAt && !mReleased)
        {
            mHolding = true;
            mChanged.notify_all();
            mChanged.wait(lock, [&] { return mReleased; });
            mHolding = false;
        }
    }
    std::function<void(DecompilePhase)> Observer()
    {
        return [this](DecompilePhase p) { Observe(p); };
    }
    void Reset(DecompilePhase holdAt = DecompilePhase::Idle)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mHoldAt = holdAt;
        mReleased = false;
        mCurrent = DecompilePhase::Idle;
        for (int& e : mEntered) e = 0;
    }
    bool WaitHolding(double seconds)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        return mChanged.wait_for(lock, std::chrono::duration<double>(seconds), [&] { return mHolding; });
    }
    bool WaitEntered(DecompilePhase p, double seconds)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        return mChanged.wait_for(lock, std::chrono::duration<double>(seconds), [&] { return mEntered[int(p)] > 0; });
    }
    void Release()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mReleased = true;
        mChanged.notify_all();
    }
    DecompilePhase Current()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mCurrent;
    }
    Clock::time_point EnteredAt(DecompilePhase p)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mEnteredAt[int(p)];
    }

private:
    std::mutex mMutex;
    std::condition_variable mChanged;
    DecompilePhase mCurrent = DecompilePhase::Idle;
    DecompilePhase mHoldAt = DecompilePhase::Idle;
    bool mReleased = false;
    bool mHolding = false;
    int mEntered[8] = {};
    Clock::time_point mEnteredAt[8];
};

// Poll as the UI would, once a millisecond, until a result comes back.
bool WaitResult(DecompilerRunner& runner, DecompileResult& out, double seconds = 60.0)
{
    const auto t0 = Clock::now();
    while (dts::MsSince(t0) < seconds * 1000.0)
    {
        if (runner.Poll(out)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool WaitIdle(DecompilerRunner& runner, double seconds = 60.0)
{
    const auto t0 = Clock::now();
    while (runner.Busy() && dts::MsSince(t0) < seconds * 1000.0)
    {
        DecompileResult ignored;
        runner.Poll(ignored);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return !runner.Busy();
}

// The following request after anything else: fixture 1, correct.
void CheckFollowingRequest(DecompilerRunner& runner, const char* after)
{
    const auto t0 = Clock::now();
    runner.Submit(9, gSnap, Small());
    DecompileResult r;
    const bool got = WaitResult(runner, r);
    const bool ok = got && r.status == DecompileStatus::Ok &&
                    r.Text().find("func_0x06005000(param_1)") != std::string::npos;
    std::printf("  following request after %s: %s in %.1f ms\n", after, ok ? "correct" : "WRONG", MsSince(t0));
    CHECK(ok);
}

struct Baseline { std::string text; double phaseMs[8] = {}; double totalMs = 0; };
Baseline gBaseline[3];

void TestBaselines()
{
    DecompilerRunner runner(gConfig, Generous());
    // Phase lengths, from the observer's timestamps. Declared outside the loop: the engine keeps
    // the observer it was given until its next request starts.
    std::vector<std::pair<DecompilePhase, Clock::time_point>> seen;
    std::mutex m;
    runner.SetPhaseObserverForTests([&](DecompilePhase p) {
        std::lock_guard<std::mutex> lock(m);
        seen.emplace_back(p, Clock::now());
    });
    // Each fixture twice: the first, cold run is the slow one, and the midway cancels below time
    // themselves against a warm run like theirs. Both runs must give the same text.
    for (int i = 0; i < 3; ++i)
    {
        std::string coldText;
        for (int run = 0; run < 2; ++run)
        {
            {
                std::lock_guard<std::mutex> lock(m);
                seen.clear();
            }
            const auto t0 = Clock::now();
            runner.Submit(0, gSnap, Big(kFixtures[i].entry));
            DecompileResult r;
            CHECK(WaitResult(runner, r));
            CHECK(r.status == DecompileStatus::Ok);
            if (run == 0)
            {
                coldText = r.Text();
                continue;
            }
            gBaseline[i] = Baseline();
            gBaseline[i].totalMs = MsSince(t0);
            gBaseline[i].text = r.Text();
            CHECK(gBaseline[i].text == coldText);
            {
                std::lock_guard<std::mutex> lock(m);
                for (size_t k = 0; k + 1 < seen.size(); ++k)
                    gBaseline[i].phaseMs[int(seen[k].first)] +=
                        std::chrono::duration<double, std::milli>(seen[k + 1].second - seen[k].second).count();
            }
        }
        const double* p = gBaseline[i].phaseMs;
        std::printf("  %-9s baseline %7.1f ms: init %.1f  followFlow %.1f  actions %.1f  print %.1f ms, %zu bytes of C\n",
                    kFixtures[i].name, gBaseline[i].totalMs, p[int(DecompilePhase::Init)], p[int(DecompilePhase::Flow)],
                    p[int(DecompilePhase::Actions)], p[int(DecompilePhase::Print)], gBaseline[i].text.size());
        CHECK(gBaseline[i].text.size() > 100);
    }
    CHECK(gBaseline[2].text.find("switch") != std::string::npos);   // the jump table was recovered
    CheckFollowingRequest(runner, "the baselines");
    runner.SetPhaseObserverForTests(nullptr);
}

// Cancel with the worker held at the very start of 'phase'.
void CancelHeldAt(DecompilerRunner& runner, PhaseGate& gate, int fixture, DecompilePhase phase, DecompileResult& r)
{
    gate.Reset(phase);
    runner.Submit(0, gSnap, Big(kFixtures[fixture].entry));
    CHECK(gate.WaitHolding(60));
    const auto t0 = Clock::now();
    runner.Cancel(0);   // the worker is inside 'phase': it has begun and cannot have left
    gate.Release();
    CHECK(WaitResult(runner, r));
    std::printf("  %-9s cancel at start of %-10s -> %-9s in %-10s flag->result %7.1f ms\n", kFixtures[fixture].name,
                PhaseName(phase), r.status == DecompileStatus::Cancelled ? "cancelled" : "completed",
                r.status == DecompileStatus::Cancelled ? PhaseName(r.cancelledIn) : "-", MsSince(t0));
}

// Cancel halfway through 'phase' (by its baseline length); counts only when the phase read
// before and after setting the flag is 'phase'. Returns false when no attempt was conclusive.
bool CancelMidway(DecompilerRunner& runner, PhaseGate& gate, int fixture, DecompilePhase phase, DecompileResult& r)
{
    for (int attempt = 1; attempt <= 10; ++attempt)
    {
        gate.Reset();
        runner.Submit(0, gSnap, Big(kFixtures[fixture].entry));
        CHECK(gate.WaitEntered(phase, 60));
        const double half = gBaseline[fixture].phaseMs[int(phase)] * 0.5;
        // Spun, not slept: followFlow lasts ~60 ms, and a sleep on a loaded CI VM (macOS under
        // Rosetta) overshoots by more than half of that, landing every attempt in the next phase.
        const auto until = gate.EnteredAt(phase) + std::chrono::duration_cast<Clock::duration>(
                                                       std::chrono::duration<double, std::milli>(half));
        while (Clock::now() < until) {}
        const DecompilePhase before = gate.Current();
        const auto t0 = Clock::now();
        runner.Cancel(0);
        const DecompilePhase after = gate.Current();
        CHECK(WaitResult(runner, r));
        const bool conclusive = before == phase && after == phase;
        std::printf("  %-9s cancel at 50%% of %-10s -> %-9s in %-10s flag->result %7.1f ms%s\n", kFixtures[fixture].name,
                    PhaseName(phase), r.status == DecompileStatus::Cancelled ? "cancelled" : "completed",
                    r.status == DecompileStatus::Cancelled ? PhaseName(r.cancelledIn) : "-", MsSince(t0),
                    conclusive ? "" : "  (inconclusive: the phase changed around the flag)");
        if (conclusive) return true;
    }
    return false;
}

void TestCancelInFlowAndActions()
{
    DecompilerRunner runner(gConfig, Generous());
    PhaseGate gate;
    runner.SetPhaseObserverForTests(gate.Observer());
    for (int i = 0; i < 3; ++i)
    {
        for (DecompilePhase phase : { DecompilePhase::Flow, DecompilePhase::Actions })
        {
            DecompileResult r;
            CancelHeldAt(runner, gate, i, phase, r);
            CHECK(r.status == DecompileStatus::Cancelled);
            CHECK(r.cancelledIn == phase);
            CHECK(r.lines.empty());

            DecompileResult m;
            const bool conclusive = CancelMidway(runner, gate, i, phase, m);
            CHECK(conclusive);
            if (conclusive)
            {
                CHECK(m.status == DecompileStatus::Cancelled);
                CHECK(m.cancelledIn == phase);
            }
        }
        // The same fixture again, uncancelled, on the same translator: identical output.
        gate.Reset();
        runner.Submit(0, gSnap, Big(kFixtures[i].entry));
        DecompileResult again;
        CHECK(WaitResult(runner, again));
        CHECK(again.status == DecompileStatus::Ok && again.Text() == gBaseline[i].text);
        std::printf("  %-9s rerun after cancellations: %s (status %d, %.1f ms)\n", kFixtures[i].name,
                    again.Text() == gBaseline[i].text ? "identical" : "DIFFERENT", int(again.status), again.elapsedMs);
    }
    CheckFollowingRequest(runner, "cancels in flow and actions");
}

// Printing has no check-point: a cancel requested while it runs is too late, and the request
// completes with the output it would have had.
void TestCancelDuringPrint()
{
    DecompilerRunner runner(gConfig, Generous());
    PhaseGate gate;
    runner.SetPhaseObserverForTests(gate.Observer());
    for (int i = 0; i < 3; ++i)
    {
        DecompileResult r;
        CancelHeldAt(runner, gate, i, DecompilePhase::Print, r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(r.Text() == gBaseline[i].text);
    }
    CheckFollowingRequest(runner, "cancels during print");
}

void TestBudget()
{
    // 100 ms against the 6k-instruction fixture (PLAN.md A4); a scaled-down fixture gets a tenth of
    // its own measured baseline instead (at least 50 ms, so fixture 1 afterwards fits), so the
    // budget still expires mid-decompilation.
    RunnerOptions options;
    options.budgetSeconds = kFixtureDivisor == 1 ? 0.1 : std::max(0.05, gBaseline[0].totalMs / 10000.0);
    DecompilerRunner runner(gConfig, options);
    const auto t0 = Clock::now();
    runner.Submit(0, gSnap, Big(dts::kDiamondsBase));
    DecompileResult r;
    CHECK(WaitResult(runner, r));
    std::printf("  diamonds under a %.0f ms budget: %s after %.1f ms (%s)\n", options.budgetSeconds * 1000,
                r.status == DecompileStatus::TooLarge ? "too large" : "NOT CANCELLED", MsSince(t0), r.error.c_str());
    CHECK(r.status == DecompileStatus::TooLarge);
    char expected[80];
    std::snprintf(expected, sizeof expected, "too large to decompile within %g s", options.budgetSeconds);
    CHECK(r.error.find(expected) != std::string::npos);

    // Not retried automatically this session...
    const uint64_t runs = runner.RunsStarted();
    runner.Submit(0, gSnap, Big(dts::kDiamondsBase));
    DecompileResult again;
    CHECK(WaitResult(runner, again, 5));
    CHECK(again.status == DecompileStatus::TooLarge);
    CHECK(runner.RunsStarted() == runs);
    // ...but an explicit Refresh runs it again (and the budget stops it again).
    runner.Submit(0, gSnap, Big(dts::kDiamondsBase), true);
    CHECK(WaitResult(runner, again));
    CHECK(again.status == DecompileStatus::TooLarge);
    CHECK(runner.RunsStarted() == runs + 1);

    // The immediate TooLarge answer still supersedes what the same requester had queued.
    PhaseGate gate;
    runner.SetPhaseObserverForTests(gate.Observer());
    gate.Reset(DecompilePhase::Flow);
    const uint64_t before = runner.RunsStarted();
    runner.Submit(0, gSnap, Small());          // holds the worker
    CHECK(gate.WaitHolding(60));
    runner.Submit(1, gSnap, Small());          // queued for requester 1...
    const uint64_t tooLarge = runner.Submit(1, gSnap, Big(dts::kDiamondsBase));   // ...then replaced
    gate.Release();
    std::vector<DecompileResult> got;   // collected by polling, as the UI would (WaitIdle discards)
    const auto t2 = Clock::now();
    while ((runner.Busy() || got.size() < 2) && MsSince(t2) < 60000)
    {
        if (runner.Poll(again)) got.push_back(again);
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::printf("  queued request replaced by an immediate TooLarge: %llu run(s), %zu result(s)\n",
                static_cast<unsigned long long>(runner.RunsStarted() - before), got.size());
    CHECK(runner.RunsStarted() == before + 1);   // only the held request ran
    bool sawTooLarge = false;
    int slot1 = 0;
    for (const DecompileResult& g : got)
        if (g.slot == 1)
        {
            ++slot1;
            sawTooLarge = g.generation == tooLarge && g.status == DecompileStatus::TooLarge;
        }
    CHECK(slot1 == 1);
    CHECK(sawTooLarge);
    runner.SetPhaseObserverForTests(nullptr);
    CheckFollowingRequest(runner, "the budget");
}

void TestEpochBump()
{
    DecompilerRunner runner(gConfig, Generous());
    PhaseGate gate;
    runner.SetPhaseObserverForTests(gate.Observer());
    gate.Reset();
    const uint64_t epoch = runner.Epoch();
    runner.Submit(0, gSnap, Big(dts::kStraightBase));
    CHECK(gate.WaitEntered(DecompilePhase::Actions, 60));
    auto t0 = Clock::now();
    runner.InvalidateSession();
    DecompileResult r;
    const bool got = runner.Poll(r);
    const double pollMs = MsSince(t0);
    std::printf("  epoch bump during actions: InvalidateSession + Poll returned in %.3f ms\n", pollMs);
    CHECK(!got);
    // Within a frame: nothing waits on the worker, so this is two short lock holds. The bound is
    // loose (50 ms, not one 60 Hz frame) because a loaded CI runner can deschedule this thread;
    // blocking on the worker would take the rest of a ~500 ms action instead.
    CHECK(pollMs < 50.0);
    CHECK(runner.Epoch() == epoch + 1);
    // The abandoned request ends at its next check-point; its result never comes back.
    t0 = Clock::now();
    bool late = false;
    while (runner.Busy() && MsSince(t0) < 60000)
    {
        late |= runner.Poll(r);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    late |= runner.Poll(r);
    std::printf("  abandoned request finished %.1f ms after the bump; result %s\n", MsSince(t0), late ? "INSTALLED" : "dropped");
    CHECK(!late);
    CheckFollowingRequest(runner, "an epoch bump");
}

void TestCoalescing()
{
    DecompilerRunner runner(gConfig, Generous());
    PhaseGate gate;
    runner.SetPhaseObserverForTests(gate.Observer());
    gate.Reset(DecompilePhase::Flow);
    const uint64_t a = runner.Submit(0, gSnap, Small());
    CHECK(gate.WaitHolding(60));   // the worker is busy with slot 0's request
    const uint64_t x = runner.Submit(1, gSnap, Big(dts::kDiamondsBase));
    const uint64_t y = runner.Submit(1, gSnap, Small());
    CHECK(a < x && x < y);
    CHECK(runner.RunsStarted() == 1);
    gate.Release();
    std::vector<DecompileResult> got;
    DecompileResult r;
    while (got.size() < 2 && WaitResult(runner, r, 60)) got.push_back(r);
    CHECK(WaitIdle(runner));
    CHECK(!runner.Poll(r));
    std::printf("  three submissions, two from one requester: %llu runs, %zu results\n",
                static_cast<unsigned long long>(runner.RunsStarted()), got.size());
    CHECK(runner.RunsStarted() == 2);   // x was replaced before it ran
    CHECK(got.size() == 2);
    for (const DecompileResult& g : got)
    {
        CHECK(g.status == DecompileStatus::Ok);
        CHECK(g.generation == (g.slot == 0 ? a : y));
    }

    // A newer request from the same requester cancels the one running; only the newer result
    // comes back.
    gate.Reset();
    const uint64_t first = runner.Submit(0, gSnap, Big(dts::kStraightBase));
    CHECK(gate.WaitEntered(DecompilePhase::Actions, 60));
    const uint64_t second = runner.Submit(0, gSnap, Small());
    CHECK(WaitResult(runner, r));
    CHECK(r.generation == second && r.status == DecompileStatus::Ok);
    CHECK(WaitIdle(runner));
    CHECK(!runner.Poll(r));
    CHECK(first < second);
}

// The engine stuck in an uncheckable segment: the observer holds it at the start of flow, where
// the cancel flag is not looked at. The budget asks it to stop; when it has not within the limit,
// decompilation is disabled, with a reason, and nothing new is started.
void TestDisableOnUnacknowledgedCancel()
{
    RunnerOptions options;
    options.budgetSeconds = 0.05;
    options.cancelAckSeconds = 0.2;
    DecompilerRunner runner(gConfig, options);
    PhaseGate gate;
    runner.SetPhaseObserverForTests(gate.Observer());
    gate.Reset(DecompilePhase::Flow);
    runner.Submit(0, gSnap, Big(dts::kDiamondsBase));
    CHECK(gate.WaitHolding(60));
    const auto t0 = Clock::now();
    DecompileResult r;
    while (!runner.Disabled() && MsSince(t0) < 10000)
    {
        CHECK(!runner.Poll(r));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::printf("  disabled after %.1f ms: %s\n", MsSince(t0), runner.DisabledReason().c_str());
    CHECK(runner.Disabled());
    CHECK(runner.DisabledReason().find("0x06010000") != std::string::npos);
    CHECK(runner.DisabledReason().find("disabled") != std::string::npos);
    CHECK(runner.Submit(0, gSnap, Small()) == 0);
    // A new session while the stuck request still runs: still disabled -- a request now would
    // only queue behind it, and no second worker is ever started.
    runner.InvalidateSession();
    CHECK(runner.Disabled());
    gate.Release();   // the stuck request ends at its next check-point...
    CHECK(WaitIdle(runner));
    CHECK(!runner.Poll(r));   // ...and nothing it produced is installed
    CHECK(runner.RunsStarted() == 1);   // no replacement worker, no further run
    // Disabled for the session, not for good: with the stuck request gone, the new session
    // decompiles on the same worker.
    std::printf("  after the stuck request ended in a later session: %s\n", runner.Disabled() ? "STILL DISABLED" : "enabled again");
    CHECK(!runner.Disabled());
    runner.SetPhaseObserverForTests(nullptr);
    CheckFollowingRequest(runner, "re-enabling in a new session");

    // Disabled and idle, then a new session: enabled at once.
    gate.Reset(DecompilePhase::Flow);
    runner.SetPhaseObserverForTests(gate.Observer());
    runner.Submit(0, gSnap, Big(dts::kDiamondsBase));
    CHECK(gate.WaitHolding(60));
    const auto t1 = Clock::now();
    while (!runner.Disabled() && MsSince(t1) < 10000)
    {
        runner.Poll(r);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(runner.Disabled());
    gate.Release();
    CHECK(WaitIdle(runner));
    CHECK(runner.Disabled());   // same session: stays disabled
    runner.InvalidateSession();
    CHECK(!runner.Disabled());
    runner.SetPhaseObserverForTests(nullptr);
    CheckFollowingRequest(runner, "a new session after the stuck request ended");
}

// Out of memory on the worker inside the engine: the request is answered Failed, the worker
// survives and answers the next one (Unavailable: the engine may be damaged, see Sh2Decompiler.h).
// The result-recording path is DisableOomChild's. Run in a child process: an injected failure
// inside the engine marks it damaged for the rest of that process.
int WorkerOomChild()
{
    DecompilerRunner runner(gConfig, Generous());
    runner.Submit(0, gSnap, Small());   // the worker is up and the engine warm before injecting
    DecompileResult r;
    CHECK(WaitResult(runner, r) && r.status == DecompileStatus::Ok);
    // Held at the start of the engine's work, so the injection lands there -- on the worker,
    // deterministically -- and not wherever the scheduler happens to be.
    PhaseGate gate;
    runner.SetPhaseObserverForTests(gate.Observer());
    gate.Reset(DecompilePhase::Init);
    runner.Submit(0, gSnap, Small());
    CHECK(gate.WaitHolding(60));
    dts::FailAfter() = 0;   // the worker's next allocation: building the Architecture
    gate.Release();
    CHECK(WaitResult(runner, r));
    dts::FailAfter() = -1;
    runner.SetPhaseObserverForTests(nullptr);
    std::printf("  child: out of memory building the Architecture -> status %d (%s)\n", int(r.status), r.error.c_str());
    CHECK(r.status == DecompileStatus::Failed);
    // The worker survived and still answers; the engine is treated as damaged from here on.
    runner.Submit(0, gSnap, Small());
    DecompileResult after;
    CHECK(WaitResult(runner, after));
    std::printf("  child: next request -> status %d\n", int(after.status));
    CHECK(after.status == DecompileStatus::Unavailable);
    CHECK(!runner.Disabled());
    std::fflush(stdout);
    std::_Exit(dts::Failures() ? 1 : 0);   // skip static teardown of a possibly damaged engine
}

void TestWorkerAllocationFailure(const char* self, const std::string& scratch)
{
    if (!dts::kInjectionUsable)
    {
        std::printf("  SKIPPED in an MSVC Debug build: its STL allocates inside noexcept moves, so an\n"
                    "  injected failure there terminates by design (the Windows Release run covers this)\n");
        return;
    }
    std::string cmd = std::string("\"") + self + "\" --worker-oom \"" + dts::Join(scratch, "oom") + "\"";
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";   // cmd.exe strips the outer pair
#endif
    const int rc = std::system(cmd.c_str());
    std::printf("  child exit status %d\n", rc);
    CHECK(rc == 0);
}

// Persistent allocation failure while a request finishes, with another request queued: recording
// the result fails, so the worker's own boundary must disable the session -- without allocating,
// since nothing can be -- and the queued request must never start; its snapshot is released.
int DisableOomChild()
{
    DecompilerRunner runner(gConfig, Generous());
    DecompileResult r;
    runner.Submit(0, gSnap, Small());
    CHECK(WaitResult(runner, r) && r.status == DecompileStatus::Ok);   // worker up, engine warm
    // A is held as its Architecture is torn down, its engine work done (one pass, so there is one
    // teardown): the failures then land in finishing and recording the result, which is the path
    // under test. (Persistent failure inside Ghidra's own action pipeline is not survivable: its
    // objects are not exception-safe, and their teardown crashes -- which is why an engine-side
    // failure marks the engine damaged, see Sh2Decompiler.h.)
    PhaseGate gate;
    runner.SetPhaseObserverForTests(gate.Observer());
    gate.Reset(DecompilePhase::Teardown);
    DecompileRequest a = Small();
    a.registerCallees = false;
    runner.Submit(0, gSnap, a);              // A
    CHECK(gate.WaitHolding(60));
    std::vector<uint8_t> hw;
    dts::PlaceFixture1(hw);
    std::shared_ptr<const MemorySnapshot> queued = dts::MakeSnapshot(hw, 9);
    runner.Submit(1, queued, Small());       // B: queued behind A
    const long queuedRefs = queued.use_count();
    dts::FailAlways() = true;                // from here every allocation fails...
    gate.Release();
    const auto t0 = Clock::now();
    while (!runner.Disabled() && MsSince(t0) < 60000)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));   // (polling Poll would allocate nothing either)
    CHECK(WaitIdle(runner));
    dts::FailAlways() = false;               // ...until here
    std::printf("  child (disable): disabled %d, runs started %llu, queued snapshot references %ld -> %ld\n",
                int(runner.Disabled()), static_cast<unsigned long long>(runner.RunsStarted()), queuedRefs,
                long(queued.use_count()));
    CHECK(runner.Disabled());                // the disable branch ran
    CHECK(runner.RunsStarted() == 2);        // the warm-up and A; B never started
    CHECK(queued.use_count() == 1);          // B dropped from the queue, its snapshot released
    std::fflush(stdout);
    std::_Exit(dts::Failures() ? 1 : 0);     // the engine ran out of memory too: skip its teardown
}

// Out of memory inside Sleigh::reset, on the runner: the request fails, the next is refused, and
// then the runner is destroyed (join + shutdown) and the process exits normally -- through the
// static destruction of the translator the failure left behind (patch 0002).
std::atomic<bool> gFailInReset(false);
void FailNextAllocationInReset()
{
    if (gFailInReset.exchange(false)) dts::FailAfter() = 0;
}

int ResetOomChild()
{
    {
        DecompilerRunner runner(gConfig, Generous());
        DecompileResult r;
        runner.Submit(0, gSnap, Small());
        CHECK(WaitResult(runner, r) && r.status == DecompileStatus::Ok);
        SaturnArchitecture::SetBuildTranslatorHookForTests(FailNextAllocationInReset);
        gFailInReset = true;
        runner.Submit(0, gSnap, Small());
        CHECK(WaitResult(runner, r));
        SaturnArchitecture::SetBuildTranslatorHookForTests(nullptr);
        dts::FailAfter() = -1;
        CHECK(r.status == DecompileStatus::Failed);
        runner.Submit(0, gSnap, Small());
        DecompileResult after;
        CHECK(WaitResult(runner, after));
        CHECK(after.status == DecompileStatus::Unavailable);
        std::printf("  child (reset): failed in Sleigh::reset -> status %d; next -> status %d; destroying the runner\n",
                    int(r.status), int(after.status));
    }
    std::printf("  child (reset): runner joined; returning normally, through static destruction\n");
    std::fflush(stdout);
    return dts::Failures() ? 1 : 0;
}

void RunChild(const char* self, const char* mode, const std::string& scratch)
{
    std::string cmd = std::string("\"") + self + "\" " + mode + " \"" + dts::Join(scratch, "oom") + "\"";
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";   // cmd.exe strips the outer pair
#endif
    const int rc = std::system(cmd.c_str());
    std::printf("  child %s exit status %d\n", mode, rc);
    CHECK(rc == 0);
}

void TestOutOfMemoryEdges(const char* self, const std::string& scratch)
{
    if (!dts::kInjectionUsable)
    {
        std::printf("  SKIPPED in an MSVC Debug build: its STL allocates inside noexcept moves, so an\n"
                    "  injected failure there terminates by design (the Windows Release run covers this)\n");
        return;
    }
    RunChild(self, "--disable-oom", scratch);
    RunChild(self, "--reset-oom", scratch);
}

// The engine-thread claim is released if the runner cannot be constructed, so the next one is
// not born disabled with no worker anywhere.
void TestConstructionFailureReleasesEngine()
{
    if (!dts::kInjectionUsable)
    {
        std::printf("  SKIPPED in an MSVC Debug build: its STL allocates inside noexcept moves, so an\n"
                    "  injected failure there terminates by design (the Windows Release run covers this)\n");
        return;
    }
    int threw = 0;
    for (long k = 0; k < 50; ++k)
    {
        bool failed = false;
        dts::FailAfter() = k;
        try
        {
            DecompilerRunner r(gConfig, Generous());
            dts::FailAfter() = -1;
        }
        catch (std::bad_alloc&)
        {
            failed = true;
        }
        dts::FailAfter() = -1;
        // Constructed: every allocation of the constructor has been failed in turn. (The pending
        // injection may have landed on the new worker instead, which then disables that runner:
        // the case the worker guards against.)
        if (!failed) break;
        ++threw;
        DecompilerRunner next(gConfig, Generous());
        CHECK(!next.Disabled());
    }
    std::printf("  runner construction failed at %d allocation point(s); the engine claim was released each time\n", threw);
    CHECK(threw > 0);
}

// A second runner while one is alive would be a second engine thread on the shared translator.
void TestOneEngineThread()
{
    DecompilerRunner first(gConfig);
    DecompilerRunner second(gConfig);
    CHECK(!first.Disabled());
    CHECK(second.Disabled());
    CHECK(second.Submit(0, gSnap, Small()) == 0);
    std::printf("  second runner: %s\n", second.DisabledReason().c_str());
}

void TestDestructorJoins()
{
    std::unique_ptr<DecompilerRunner> runner(new DecompilerRunner(gConfig, Generous()));
    PhaseGate gate;
    runner->SetPhaseObserverForTests(gate.Observer());
    gate.Reset();
    runner->Submit(0, gSnap, Big(dts::kJumpTableBase));
    CHECK(gate.WaitEntered(DecompilePhase::Actions, 60));
    const auto t0 = Clock::now();
    runner.reset();
    const double destroyMs = MsSince(t0);
    std::printf("  destructor with a request in flight (actions): returned after join + shutdown in %.1f ms\n", destroyMs);
    CHECK(destroyMs < 30000);
    // The engine thread is gone and its slot free: a new runner works.
    DecompilerRunner next(gConfig, Generous());
    CHECK(!next.Disabled());
    CheckFollowingRequest(next, "the destructor joined a request in flight");
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--worker-oom")
    {
        dts::PrepareProcess();
        const std::string dir = argv[2];
        sfe::MakeDirectory(dir);
        gConfig = dts::Join(dir, "config");
        sfe::MakeDirectory(gConfig);
        gSnap = BuildSnapshot();
        return WorkerOomChild();
    }
    if (argc == 3 && (std::string(argv[1]) == "--disable-oom" || std::string(argv[1]) == "--reset-oom"))
    {
        dts::PrepareProcess();
        const std::string dir = argv[2];
        sfe::MakeDirectory(dir);
        gConfig = dts::Join(dir, "config");
        sfe::MakeDirectory(gConfig);
        gSnap = BuildSnapshot();
        return std::string(argv[1]) == "--disable-oom" ? DisableOomChild() : ResetOomChild();
    }
    if (argc != 2)
    {
        std::printf("usage: %s <scratch dir>\n", argv[0]);
        return 2;
    }
    dts::PrepareProcess();
    const auto start = std::chrono::steady_clock::now();
    const std::string scratch = argv[1];
    dts::RemoveTree(scratch);
    CHECK(sfe::MakeDirectory(scratch));
    gConfig = dts::Join(scratch, "config");
    CHECK(sfe::MakeDirectory(gConfig));
    gSnap = BuildSnapshot();
    std::printf("fixtures: diamonds %d, straight %d, jumptable 16 x %d%s\n", 1000 / kFixtureDivisor,
                5000 / kFixtureDivisor, 300 / kFixtureDivisor,
                kFixtureDivisor > 1 ? " (a tenth: MSVC Debug CRT)" : " (the cancellation spike's sizes)");
    try
    {
        RUN(TestBaselines());
        RUN(TestCancelInFlowAndActions());
        RUN(TestCancelDuringPrint());
        RUN(TestBudget());
        RUN(TestEpochBump());
        RUN(TestCoalescing());
        RUN(TestDisableOnUnacknowledgedCancel());
        RUN(TestOneEngineThread());
        RUN(TestWorkerAllocationFailure(argv[0], scratch));
        RUN(TestConstructionFailureReleasesEngine());
        RUN(TestOutOfMemoryEdges(argv[0], scratch));
        RUN(TestDestructorJoins());
    }
    catch (std::exception& e)
    {
        std::printf("FATAL: uncaught std::exception: %s\n", e.what());
        return 3;
    }
    gSnap.reset();
    dts::RemoveTree(scratch);
    return dts::Finish("DecompilerRunnerTests", start);
}
