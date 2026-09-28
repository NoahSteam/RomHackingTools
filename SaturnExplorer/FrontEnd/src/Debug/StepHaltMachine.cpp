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

StepOutcome StepHaltMachine::Observe(const StopReport& report)
{
    // Recognised by address alone: PC breakpoints are shared across both SH-2s, so which CPU
    // reported the stop says nothing about whose breakpoint it is.
    const bool atStepTarget = mStepBpActive && report.pc == mStepBpAddr;
    // Captured BEFORE the hold is released below, and this order is the point: the release clears
    // mAwaitingHalt on the very frame the new halt lands, so afterwards a completed step and a
    // fresh breakpoint hit are indistinguishable. Stepping runs through the same resume/re-halt
    // path as a hit, so reading this after the release would count every step as a new hit.
    const bool fromStep = mAwaitingHalt || atStepTarget;

    // Release the hold only when the emulator reaches a *new* halt -- a PC other than where the
    // step began -- never on bare "stopped".
    //
    // The stale pre-step echo: the poll thread keeps reporting the PRE-step halt, at the same PC,
    // for a frame or two before the server processes the step (the export-side stop is cleared
    // only when its interrupt handler runs). Releasing on that stale stop would drop the hold and
    // let the flash back in.
    if (mAwaitingHalt)
    {
        // mHaltPc is the PC the step began at: it is written only below, under !mAwaitingHalt, so
        // it cannot move while a hold is live.
        if (report.stopped && report.pc != mHaltPc) { mAwaitingHalt = false; }  // new halt
        else if (--mHoldFrames <= 0)                { mAwaitingHalt = false; }  // ran long
    }

    // The flash: without the hold, the emulator reports "running" for the few frames a step takes,
    // so the red row, frozen registers and halted Assembly row blink off and back on.
    mHaltActive = report.stopped || mAwaitingHalt;

    // Adopt the reported PC only once it is trusted to be the current halt. While holding, keep
    // the last halt PC on screen -- a stale pre-step echo carries that same PC anyway.
    if (report.stopped && !mAwaitingHalt)
    {
        mHaltCpu = static_cast<int>(report.cpu);
        mHaltPc  = report.pc;
    }

    StepOutcome out;
    out.fromStep     = fromStep;
    out.atStepTarget = atStepTarget;
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
}

void StepHaltMachine::BeginRunTo(uint32_t addr)
{
    mStepBpActive = true;
    mStepBpAddr   = addr;
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
}

void StepHaltMachine::ResetForNewEmulator()
{
    ResetHalt();
    mStepBpActive = false;
    mStepBpDirty  = true;   // a fresh emulator holds none of our breakpoints
    mSettleFrames = 0;
}

}  // namespace sfe
