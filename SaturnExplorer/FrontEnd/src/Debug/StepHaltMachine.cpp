#include "Debug/StepHaltMachine.h"

namespace sfe
{

namespace
{
// Safety cap on how long the halted presentation is held while a step is in flight. A normal step
// re-halts in a couple of frames; this only bites when a Step Over/Out runs into a long -- or
// non-returning -- routine, so it eventually reveals "running" instead of lying about a halt.
constexpr int kHoldFrames   = 60;
// After a step, keep re-capturing the live source for this many UI frames so the stepped frame
// settles over the socket before the snapshot re-freezes.
constexpr int kSettleFrames = 4;
}  // namespace

bool StepHaltMachine::IsNewStop(const StopReport& r) const
{
    if (!r.stopped) { return false; }
    // Identity, not position: a step that lands on the PC it started from (a branch to itself) is a
    // different halt at the same address, and only the emulator's own count can say so.
    if (r.hasSeq && mHaltHasSeq) { return r.seq != mHaltSeq; }
    // An emulator that numbers nothing: the best available is whether the PC moved. mHaltPc is the PC
    // the step began at -- it is written only under !mAwaitingHalt, so it cannot move while a hold is
    // live.
    return r.pc != mHaltPc;
}

StepOutcome StepHaltMachine::Observe(const StopReport& report)
{
    // A halt we already declined once (a stray target) and that is being re-reported while the
    // resume that follows it crosses the socket. Same number -> same halt; it is neither new nor
    // stray, just an echo to hold through.
    const bool strayEcho = report.stopped && report.hasSeq && mStrayHasSeq && report.seq == mStraySeq;
    const bool newStop   = IsNewStop(report) && !strayEcho;

    // The halt is at the transient's address. It is the step ARRIVING only for the CPU that is
    // stepping (PC breakpoints are shared, so which CPU reported the stop is exactly what separates
    // "my step finished" from "the other core walked over my transient").
    const bool atTargetAddr = report.stopped && mStepBpActive && report.pc == mStepBpAddr;
    const bool atStepTarget = atTargetAddr && report.cpu == static_cast<uint32_t>(mStepBpCpu);
    // The wrong core, with no user breakpoint to explain the stop: not something
    // to present. Independent of the hold -- which gives up after a while on a long step -- because
    // the transient outlives it, and so does the need to ignore the wrong core reaching it.
    const bool strayTarget = atTargetAddr && !atStepTarget && newStop && !report.userBreakpoint;

    // Captured BEFORE the hold is released below, and this order is the point: the release clears
    // mAwaitingHalt on the very frame the new halt lands, so afterwards a completed step and a
    // fresh breakpoint hit are indistinguishable. Stepping runs through the same resume/re-halt
    // path as a hit, so reading this after the release would count every step as a new hit.
    const bool fromStep = mAwaitingHalt || atStepTarget;

    // Release the hold only when the emulator reaches a *new* halt, never on bare "stopped".
    //
    // The stale pre-step echo: the poll thread keeps reporting the PRE-step halt, same number and
    // same PC, for a frame or two before the server processes the step (the export-side stop is
    // cleared only when its interrupt handler runs). Releasing on that stale stop would drop the
    // hold and let the flash back in.
    if (mAwaitingHalt)
    {
        if (newStop && !strayTarget) { mAwaitingHalt = false; }                    // new halt
        else if (--mHoldFrames <= 0) { mAwaitingHalt = false; }                    // ran long
    }

    if (strayTarget)
    {
        mStrayHasSeq = report.hasSeq;
        mStraySeq    = report.seq;
    }

    // A different halt arrived, somewhere other than the target -- a user breakpoint on the way,
    // the other core's. The step that installed the transient did not finish, so the transient is
    // stale: left installed, the next Continue would stop at the return site as a ghost "step".
    if (newStop && !atStepTarget && !strayTarget && mStepBpActive)
    {
        mStepBpActive = false;
        mStepBpDirty  = true;
    }

    // The flash: without the hold, the emulator reports "running" for the few frames a step takes,
    // so the red row, frozen registers and halted Assembly row blink off and back on.
    mHaltActive = report.stopped || mAwaitingHalt;

    // Adopt the reported halt only once it is trusted to be the current one. While holding, keep
    // the last halt on screen -- a stale pre-step echo carries that same identity anyway.
    if (report.stopped && !mAwaitingHalt)
    {
        mHaltCpu     = static_cast<int>(report.cpu);
        mHaltPc      = report.pc;
        mHaltHasSeq  = report.hasSeq;
        mHaltSeq     = report.seq;
        mStrayHasSeq = false;
    }

    StepOutcome out;
    out.fromStep     = fromStep;
    out.atStepTarget = atStepTarget;
    out.strayTarget  = strayTarget;
    return out;
}

void StepHaltMachine::SuppressHalt()
{
    mHaltActive = false;
}

void StepHaltMachine::RetireStepTarget()
{
    mStepBpActive = false;
    mStepBpDirty  = true;
}

void StepHaltMachine::BeginStep()
{
    mAwaitingHalt = true;
    mHoldFrames   = kHoldFrames;
    mStrayHasSeq  = false;
}

void StepHaltMachine::BeginRunTo(uint32_t addr, int cpu)
{
    mStepBpActive = true;
    mStepBpAddr   = addr;
    mStepBpCpu    = (cpu != 0) ? 1 : 0;
    mStepBpDirty  = true;
    BeginStep();
}

bool StepHaltMachine::TakeStepTargetDirty()
{
    const bool was = mStepBpDirty;
    mStepBpDirty = false;
    return was;
}

void StepHaltMachine::BeginSettle()
{
    mSettleFrames = kSettleFrames;
}

void StepHaltMachine::ConsumeSettleFrame()
{
    if (mSettleFrames > 0) { --mSettleFrames; }
}

void StepHaltMachine::ResetHalt()
{
    mHaltActive   = false;
    mAwaitingHalt = false;
    mHoldFrames   = 0;
    mHaltHasSeq   = false;
    mStrayHasSeq  = false;
}

void StepHaltMachine::ResetForNewEmulator()
{
    ResetHalt();
    mStepBpActive = false;
    mStepBpDirty  = true;   // a fresh emulator holds none of our breakpoints
    mSettleFrames = 0;
}

}  // namespace sfe
