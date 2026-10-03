// Tests for the step/halt state machine (UI-01).
//
// These are the point of the extraction. The machine's rules were each arrived at by fighting a
// race against a live emulator, and until now nothing could exercise them -- the code review left
// it alone for exactly that reason. Both races the comments in App.cpp described are pinned here:
// the presentation "flash" and the stale pre-step stop echo.

#include "Debug/StepHaltMachine.h"

#include <cstdio>
#include <iostream>

namespace
{

int gFailures = 0;

void CheckImpl(bool ok, const char* expression, int line)
{
    if (!ok)
    {
        std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
        ++gFailures;
    }
}

#define CHECK(expr) CheckImpl((expr), #expr, __LINE__)

using sfe::StepOutcome;
using sfe::StepHaltMachine;
using sfe::StopReport;

StopReport Halted(uint32_t pc, uint32_t cpu = 0, uint32_t reason = 0)
{
    StopReport r;
    r.stopped = true;
    r.pc      = pc;
    r.cpu     = cpu;
    r.reason  = reason;
    return r;
}

StopReport Running()
{
    return StopReport{};
}

// Reach a halt at 'pc' the way the app does: the emulator reports stopped and nothing is in flight.
StepHaltMachine HaltedAt(uint32_t pc, uint32_t cpu = 0)
{
    StepHaltMachine m;
    const StepOutcome s = m.Observe(Halted(pc, cpu));
    CHECK(m.HaltActive());
    CHECK(m.HaltPc() == pc);
    CHECK(!s.fromStep);
    return m;
}

// A plain breakpoint hit is presented, is not a step, and adopts the reported CPU and PC.
void TestBreakpointHaltIsPresented()
{
    StepHaltMachine m;
    const StepOutcome s = m.Observe(Halted(0x06001000, 1));
    CHECK(m.HaltActive());
    CHECK(m.HaltCpu() == 1);
    CHECK(m.HaltPc() == 0x06001000);
    CHECK(!s.fromStep);
    CHECK(!s.atStepTarget);
}

// The flash: while a step is in flight the emulator reports "running" for a frame or two. The
// halted presentation has to survive that, or the red row and frozen registers blink off and back
// on. This is the test the original code had no way to write.
void TestHaltIsHeldWhileAStepIsInFlight()
{
    StepHaltMachine m = HaltedAt(0x06001000);
    m.BeginStep();
    for (int frame = 0; frame < 5; ++frame)
    {
        const StepOutcome s = m.Observe(Running());
        CHECK(m.HaltActive());                 // held: no flash
        CHECK(m.HaltPc() == 0x06001000);       // and still showing the PC it halted at
        CHECK(s.fromStep);
    }
}

// The stale pre-step echo: the poll thread keeps reporting the PRE-step halt, at the same PC, until
// the server processes the step. Releasing the hold on that stale stop would drop the hold and let
// the flash back in, so a stop at the PC the step began at must NOT release it.
void TestStaleStopAtTheSamePcDoesNotReleaseTheHold()
{
    StepHaltMachine m = HaltedAt(0x06001000);
    m.BeginStep();
    for (int frame = 0; frame < 3; ++frame)
    {
        const StepOutcome s = m.Observe(Halted(0x06001000));   // the stale echo
        CHECK(m.HaltActive());
        CHECK(s.fromStep);         // still in flight, so still a step
        CHECK(m.StepInFlight());   // the hold survived
    }
    // The real re-halt, at a new PC, does release it.
    m.Observe(Halted(0x06001002));
    CHECK(m.HaltActive());
    CHECK(m.HaltPc() == 0x06001002);
    CHECK(!m.StepInFlight());
}

// fromStep must be read before the hold is released, or the frame the step lands on looks exactly
// like a fresh breakpoint hit -- and the caller would re-surface the Assembly panel on every step.
void TestTheLandingFrameOfAStepStillReportsFromStep()
{
    StepHaltMachine m = HaltedAt(0x06001000);
    m.BeginStep();
    const StepOutcome s = m.Observe(Halted(0x06001002));   // the step lands
    CHECK(m.HaltActive());
    CHECK(s.fromStep);            // the whole point: not a new hit
    CHECK(!m.StepInFlight());     // and the hold is released on the same frame
    // The frame after is an ordinary halt again.
    const StepOutcome next = m.Observe(Halted(0x06001002));
    CHECK(m.HaltActive());
    CHECK(!next.fromStep);
}

// A step that runs into a long -- or non-returning -- routine must eventually reveal "running"
// rather than hold a halt that is never coming.
void TestALongRunningStepEventuallyRevealsRunning()
{
    StepHaltMachine m = HaltedAt(0x06001000);
    m.BeginStep();
    int frames = 0;
    while (m.StepInFlight() && frames < 1000)
    {
        m.Observe(Running());
        ++frames;
    }
    CHECK(!m.StepInFlight());
    CHECK(frames > 1);       // it did hold for a while first
    CHECK(frames < 1000);    // and the cap did fire
    m.Observe(Running());
    CHECK(!m.HaltActive());   // now it admits the emulator is running
}

// While holding, the reported PC is not adopted: the panels keep showing where the step began.
void TestHoldingDoesNotAdoptAReportedPc()
{
    StepHaltMachine m = HaltedAt(0x06001000, 0);
    m.BeginStep();
    // A stale echo carrying a different CPU must not move the presentation either.
    m.Observe(Halted(0x06001000, 1));
    CHECK(m.HaltActive());
    CHECK(m.HaltCpu() == 0);
    CHECK(m.HaltPc() == 0x06001000);
}

// Step Over / Step Out install a transient breakpoint. It is recognised by address alone (PC
// breakpoints are shared across both SH-2s), reported so the caller can skip the condition guard,
// and retired once the caller surfaces the halt.
void TestRunToTargetIsRecognisedAndRetired()
{
    StepHaltMachine m = HaltedAt(0x06001000);
    m.BeginRunTo(0x06002000);
    CHECK(m.StepTargetActive());
    CHECK(m.StepTargetAddr() == 0x06002000);
    CHECK(m.TakeStepTargetDirty());    // installing it needs a breakpoint re-sync
    CHECK(!m.TakeStepTargetDirty());   // and the flag is one-shot

    // Reported by the other CPU: still our target, because the address is what identifies it.
    const StepOutcome s = m.Observe(Halted(0x06002000, 1));
    CHECK(m.HaltActive());
    CHECK(s.atStepTarget);
    CHECK(s.fromStep);

    m.RetireStepTarget();
    CHECK(!m.StepTargetActive());
    CHECK(m.TakeStepTargetDirty());    // removing it needs one too

    // Once retired, a later halt at that same address is an ordinary halt.
    const StepOutcome later = m.Observe(Halted(0x06002000));
    CHECK(!later.atStepTarget);
}

// A halt at the transient's address counts as the target even before the hold releases, which is
// how a Step Over that arrives instantly is still recognised as a step rather than a hit.
void TestReachingTheTargetImmediatelyIsStillAStep()
{
    StepHaltMachine m = HaltedAt(0x06001000);
    m.BeginRunTo(0x06002000);
    const StepOutcome s = m.Observe(Halted(0x06002000));
    CHECK(s.atStepTarget);
    CHECK(s.fromStep);
    CHECK(m.HaltPc() == 0x06002000);
}

// Every step entry point refuses while one is in flight: a second resume loses the first step's
// re-halt. The machine only reports it; App's step methods return early on it.
void TestStepInFlightBlocksAnotherStep()
{
    StepHaltMachine m = HaltedAt(0x06001000);
    CHECK(!m.StepInFlight());
    m.BeginStep();
    CHECK(m.StepInFlight());
    m.Observe(Running());
    CHECK(m.StepInFlight());   // still in flight across the resume
}

// A halt the caller chose not to surface -- a condition guard that did not hold, or an access-log
// watchpoint -- is resumed instead, so it must not be presented.
void TestSuppressedHaltIsNotPresented()
{
    StepHaltMachine m;
    m.Observe(Halted(0x06001000, 0, 1 /* exec BP */));
    CHECK(m.HaltActive());
    m.SuppressHalt();
    CHECK(!m.HaltActive());   // retracted at once, before any reader sees it
    // The next frame reports running (the caller resumed), and nothing is held.
    m.Observe(Running());
    CHECK(!m.HaltActive());
}

// A source that cannot halt drops the halt and any in-flight step, but leaves the transient alone:
// it lives in the emulator, and the sync path is what removes it.
void TestResetHaltDropsTheStepButKeepsTheTransient()
{
    StepHaltMachine m = HaltedAt(0x06001000);
    m.BeginRunTo(0x06002000);
    (void)m.TakeStepTargetDirty();

    m.ResetHalt();
    CHECK(!m.StepInFlight());
    CHECK(!m.HaltActive());
    CHECK(m.StepTargetActive());        // still installed
    CHECK(!m.TakeStepTargetDirty());    // and no re-sync was invented
}

// A freshly started emulator holds none of our breakpoints, so the whole machine resets and the
// dirty flag is left set to force a full re-sync.
void TestResetForNewEmulatorClearsEverythingAndForcesASync()
{
    StepHaltMachine m = HaltedAt(0x06001000);
    m.BeginRunTo(0x06002000);
    m.BeginSettle();
    (void)m.TakeStepTargetDirty();

    m.ResetForNewEmulator();
    CHECK(!m.StepInFlight());
    CHECK(!m.StepTargetActive());
    CHECK(!m.Settling());
    CHECK(!m.HaltActive());
    CHECK(m.TakeStepTargetDirty());   // forced, so the user's set is re-armed
}

// The settle window keeps capture on for a few frames after a step so the stepped frame crosses
// the socket before the snapshot re-freezes.
void TestSettleWindowCountsDownAndStops()
{
    StepHaltMachine m;
    CHECK(!m.Settling());
    m.BeginSettle();
    CHECK(m.Settling());
    int frames = 0;
    while (m.Settling() && frames < 100)
    {
        m.ConsumeSettleFrame();
        ++frames;
    }
    CHECK(frames > 1);
    CHECK(frames < 100);
    CHECK(!m.Settling());
    // Consuming past the end is a no-op. Note what this does NOT pin: removing the guard in
    // ConsumeSettleFrame changes nothing a caller can observe, because Settling() tests > 0 and
    // BeginSettle assigns rather than adds. The guard is there so the counter cannot drift
    // negative for the whole life of the process -- the capture gate consumes a frame every frame
    // -- which is signed overflow eventually and not something a test can reach. Left as a
    // deliberate no-test case rather than a contrived one.
    m.ConsumeSettleFrame();
    CHECK(!m.Settling());
}

}  // namespace

int main()
{
    TestBreakpointHaltIsPresented();
    TestHaltIsHeldWhileAStepIsInFlight();
    TestStaleStopAtTheSamePcDoesNotReleaseTheHold();
    TestTheLandingFrameOfAStepStillReportsFromStep();
    TestALongRunningStepEventuallyRevealsRunning();
    TestHoldingDoesNotAdoptAReportedPc();
    TestRunToTargetIsRecognisedAndRetired();
    TestReachingTheTargetImmediatelyIsStillAStep();
    TestStepInFlightBlocksAnotherStep();
    TestSuppressedHaltIsNotPresented();
    TestResetHaltDropsTheStepButKeepsTheTransient();
    TestResetForNewEmulatorClearsEverythingAndForcesASync();
    TestSettleWindowCountsDownAndStops();
    if (gFailures != 0)
    {
        std::cerr << gFailures << " step/halt check(s) failed\n";
        return 1;
    }
    std::printf("StepHaltMachineTests: all checks passed\n");
    return 0;
}
