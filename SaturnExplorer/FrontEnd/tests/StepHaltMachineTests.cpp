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

// A halt from an emulator that numbers its halts (v21+).
StopReport Numbered(uint32_t seq, uint32_t pc, uint32_t cpu = 0)
{
    StopReport r = Halted(pc, cpu);
    r.hasSeq = true;
    r.seq    = seq;
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

// Step Over / Step Out install a transient breakpoint. It completes the step when the STEPPING CPU
// reaches it -- PC breakpoints are shared across both SH-2s, so the emulator stops whichever core
// gets there first and only the CPU recorded with the step can say it was the step arriving. It is
// reported so the caller can skip the condition guard, and retired once the caller surfaces the halt.
void TestRunToTargetIsRecognisedAndRetired()
{
    StepHaltMachine m = HaltedAt(0x06001000, 1);
    m.BeginRunTo(0x06002000, 1);
    CHECK(m.StepTargetActive());
    CHECK(m.StepTargetAddr() == 0x06002000);
    CHECK(m.StepTargetCpu() == 1);
    CHECK(m.TakeStepTargetDirty());    // installing it needs a breakpoint re-sync
    CHECK(!m.TakeStepTargetDirty());   // and the flag is one-shot

    const StepOutcome s = m.Observe(Halted(0x06002000, 1));
    CHECK(m.HaltActive());
    CHECK(s.atStepTarget);
    CHECK(s.fromStep);
    CHECK(!s.strayTarget);

    m.RetireStepTarget();
    CHECK(!m.StepTargetActive());
    CHECK(m.TakeStepTargetDirty());    // removing it needs one too

    // Once retired, a later halt at that same address is an ordinary halt.
    const StepOutcome later = m.Observe(Halted(0x06002000));
    CHECK(!later.atStepTarget);
    CHECK(!later.strayTarget);
}

// The other core walking over the transient is NOT the step arriving. It is reported as a stray
// target for the caller to resume from, the step stays in flight, and nothing is presented as the
// new halt. Reproduces the failure: matched by address alone, a slave halt at the address completed
// a master Step Over.
void TestTheOtherCpuReachingTheTargetIsNotTheStep()
{
    StepHaltMachine m = HaltedAt(0x06001000, 0);
    m.BeginRunTo(0x06002000, 0);
    (void)m.TakeStepTargetDirty();

    const StepOutcome stray = m.Observe(Numbered(7, 0x06002000, 1));
    CHECK(!stray.atStepTarget);
    CHECK(stray.strayTarget);
    CHECK(m.StepInFlight());            // the step is still waiting for ITS CPU
    CHECK(m.StepTargetActive());        // and its transient is still installed
    CHECK(m.HaltCpu() == 0);            // the slave's halt was not adopted
    CHECK(m.HaltPc() == 0x06001000);

    // The caller resumes and declines the halt. The same halt re-reported while that resume crosses
    // the socket is an echo: not a second stray to resume from, and above all not a halt to present --
    // the caller (App) treats any report that is not an echo as a stop and would pause on it.
    m.SuppressHalt();
    for (int frame = 0; frame < 3; ++frame)
    {
        const StepOutcome echo = m.Observe(Numbered(7, 0x06002000, 1));
        CHECK(echo.declinedEcho);
        CHECK(!echo.strayTarget && !echo.atStepTarget);
        CHECK(m.StepInFlight());
        CHECK(m.HaltCpu() == 0 && m.HaltPc() == 0x06001000);   // still not adopted
    }

    // The master arrives: that is the step.
    const StepOutcome done = m.Observe(Numbered(8, 0x06002000, 0));
    CHECK(done.atStepTarget);
    CHECK(done.fromStep);
    CHECK(!done.strayTarget);
    CHECK(!m.StepInFlight());
    CHECK(m.HaltCpu() == 0 && m.HaltPc() == 0x06002000);
}

// An SCU-DMA watchpoint stops between instructions, so its PC equalling the transient proves
// nothing: on either CPU it is not the step arriving, and it is not a stray to resume from.
void TestADmaHaltAtTheTargetIsNeitherTheStepNorAStray()
{
    for (uint32_t cpu = 0; cpu < 2; ++cpu)
    {
        StepHaltMachine m = HaltedAt(0x06001000, 0);
        m.BeginRunTo(0x06002000, 1);                 // stepping the slave
        (void)m.TakeStepTargetDirty();

        StopReport dma = Numbered(9, 0x06002000, cpu);
        dma.dmaWatch = true;
        const StepOutcome o = m.Observe(dma);
        CHECK(!o.atStepTarget);                      // not the step finishing
        CHECK(!o.strayTarget);                       // and not discarded before the DMA policy
        CHECK(m.StepTargetActive());                 // the transient is untouched
    }
}

// A user breakpoint at the same address explains the other core's halt: it is a real hit, so it is
// presented, and the step it interrupted is over -- its transient goes with it.
void TestAUserBreakpointAtTheTargetIsARealHaltOnEitherCpu()
{
    StepHaltMachine m = HaltedAt(0x06001000, 0);
    m.BeginRunTo(0x06002000, 0);
    (void)m.TakeStepTargetDirty();

    StopReport r = Numbered(5, 0x06002000, 1);
    r.userBreakpoint = true;
    const StepOutcome s = m.Observe(r);
    CHECK(!s.strayTarget);
    CHECK(!s.atStepTarget);
    CHECK(m.HaltActive() && m.HaltCpu() == 1 && m.HaltPc() == 0x06002000);
    CHECK(!m.StepInFlight());
    CHECK(m.StepTargetActive());        // Observe alone does not cancel the step: the caller decides
    m.HaltPresented(s.atStepTarget);    // ...and it has now decided to show this halt
    CHECK(!m.StepTargetActive());       // the step that wanted it did not finish
    CHECK(m.TakeStepTargetDirty());     // so the emulator is told to drop it
}

// A different halt before the target -- a breakpoint inside the call being stepped over -- ends the
// step, and the transient it installed must not survive to stop the next Continue at the return
// site as a ghost step.
void TestAHaltElsewhereRetiresTheTransient()
{
    StepHaltMachine m = HaltedAt(0x06001000, 0);
    m.BeginRunTo(0x06002000, 0);
    (void)m.TakeStepTargetDirty();

    const StepOutcome s = m.Observe(Numbered(9, 0x06003000, 0));   // a user breakpoint in the callee
    CHECK(!s.atStepTarget && !s.strayTarget);
    CHECK(!m.StepInFlight());
    CHECK(m.HaltPc() == 0x06003000);
    m.HaltPresented(s.atStepTarget);    // the caller shows it
    CHECK(!m.StepTargetActive());
    CHECK(m.TakeStepTargetDirty());

    // The same halt reported again does nothing further.
    (void)m.Observe(Numbered(9, 0x06003000, 0));
    CHECK(!m.TakeStepTargetDirty());
}

// A halt the caller RESUMES from -- a conditional breakpoint whose guard does not hold, a logging
// watchpoint -- must not cost the step its destination. Reproduces the failure: the transient was
// retired when the halt was observed, before the guard was evaluated, so the step ran past the
// address it was going to.
void TestAResumedHaltDoesNotCancelTheStep()
{
    StepHaltMachine m = HaltedAt(0x06001000, 0);
    m.BeginRunTo(0x06002000, 0);
    (void)m.TakeStepTargetDirty();

    const StepOutcome s = m.Observe(Numbered(9, 0x06003000, 0));   // a conditional BP in the callee
    CHECK(!s.atStepTarget);
    m.SuppressHalt();                  // its guard did not hold: the caller resumes
    CHECK(m.StepTargetActive());       // the destination is still installed
    CHECK(!m.TakeStepTargetDirty());   // and the emulator is not told otherwise

    // The echo of that halt is dropped, and the step carries on to its target.
    CHECK(m.Observe(Numbered(9, 0x06003000, 0)).declinedEcho);
    (void)m.Observe(StopReport{});
    const StepOutcome arrived = m.Observe(Numbered(10, 0x06002000, 0));
    CHECK(arrived.atStepTarget && arrived.fromStep);
}

// A declined halt is dropped once, whichever way it was declined, and a LATER halt -- even at the same
// PC -- is a different halt that is presented.
void TestADeclinedHaltIsDroppedOnceAndALaterOneIsNot()
{
    StepHaltMachine m;
    m.Observe(Numbered(5, 0x06004000, 0));
    CHECK(m.HaltActive());
    m.SuppressHalt();                                   // guard false: resumed
    CHECK(!m.HaltActive());

    const StepOutcome echo = m.Observe(Numbered(5, 0x06004000, 0));
    CHECK(echo.declinedEcho);
    CHECK(!m.HaltActive());                             // nothing to present

    const StepOutcome again = m.Observe(Numbered(6, 0x06004000, 0));   // the loop hits it again
    CHECK(!again.declinedEcho);
    CHECK(m.HaltActive() && m.HaltPc() == 0x06004000);
}

// Stop identity. A step that lands on the PC it started from (a taken branch to itself) is a
// different halt at the same address; only the emulator's sequence number can say so. Reproduces the
// failure: comparing PCs treated the landing as the pre-step echo, held the presentation for the
// whole cap, and revealed "running" over a halted emulator.
void TestAStepThatLandsOnTheSamePcIsANewHaltBySequence()
{
    StepHaltMachine m;
    m.Observe(Numbered(4, 0x06001000));
    m.BeginStep();

    const StepOutcome echo = m.Observe(Numbered(4, 0x06001000));   // the pre-step stop, again
    CHECK(m.StepInFlight());
    CHECK(echo.fromStep);

    const StepOutcome landed = m.Observe(Numbered(5, 0x06001000));   // the step: same address
    CHECK(!m.StepInFlight());
    CHECK(landed.fromStep);
    CHECK(m.HaltActive() && m.HaltPc() == 0x06001000);

    // Without numbers it cannot be told, and the PC is all there is to compare (the old rule).
    StepHaltMachine old;
    old.Observe(Halted(0x06001000));
    old.BeginStep();
    old.Observe(Halted(0x06001000));
    CHECK(old.StepInFlight());
}

// A halt at the transient's address counts as the target even before the hold releases, which is
// how a Step Over that arrives instantly is still recognised as a step rather than a hit.
void TestReachingTheTargetImmediatelyIsStillAStep()
{
    StepHaltMachine m = HaltedAt(0x06001000);
    m.BeginRunTo(0x06002000, 0);
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
    m.BeginRunTo(0x06002000, 0);
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
    m.BeginRunTo(0x06002000, 0);
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
    TestADmaHaltAtTheTargetIsNeitherTheStepNorAStray();
    TestTheOtherCpuReachingTheTargetIsNotTheStep();
    TestAUserBreakpointAtTheTargetIsARealHaltOnEitherCpu();
    TestAHaltElsewhereRetiresTheTransient();
    TestAResumedHaltDoesNotCancelTheStep();
    TestADeclinedHaltIsDroppedOnceAndALaterOneIsNot();
    TestAStepThatLandsOnTheSamePcIsANewHaltBySequence();
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
