// StepHaltMachine — the run-control state machine behind Step Into / Over / Out and a
// breakpoint halt.
//
// This was ten members of `App` (UI-01). It is extracted here not to make App.cpp shorter but
// because it is the one part of that file holding invariants that nothing could exercise: its
// behaviour is only visible against a running emulator, so every rule in it was arrived at by
// fighting a race by hand. Both races are named in the rules below, and both are now tests.
//
// What it does NOT do is decide anything the emulator can see. It resumes nothing, evaluates no
// breakpoint condition, logs no access and opens no panel; it reports what the halt *is* and the
// caller stays in charge of what to do about it. That is what lets it be tested without a live
// emulator, and it is why the conditional-breakpoint and access-log policy stayed in App.
#pragma once

#include <cstdint>

namespace sfe
{

// What the poll loop observed from the emulator this frame. A direct copy of se_live_get_stop's
// out-parameters, so the caller does no interpretation before handing it over.
struct StopReport
{
    bool     stopped = false;   // the emulator reports itself halted
    uint32_t reason  = 0;       // SE_LIVE_STOP_* (the machine only passes this through)
    uint32_t cpu     = 0;       // which SH-2 latched the stop
    uint32_t pc      = 0;       // the halt PC
    // Stop identity (v21+): the emulator numbers every halt it publishes. Two reports with the same
    // number are the same halt, different numbers are different halts -- even at the same PC, which
    // comparing PCs cannot say (a step over a branch to itself lands where it started). Absent
    // (hasSeq false) from an older emulator, and then the machine falls back to comparing PCs.
    bool     hasSeq  = false;
    uint32_t seq     = 0;
    // The halt address carries a user execution breakpoint. PC breakpoints are shared across both
    // SH-2s, so the transient step target (installed as one) stops whichever CPU reaches it first;
    // a stop that the user's own breakpoint explains is a real halt whoever reached it.
    bool     userBreakpoint = false;
};

// What just happened this frame, as opposed to what is now true.
//
// The halt itself is level-triggered and is read through HaltActive()/HaltCpu()/HaltPc(), by
// callers spread across the whole frame. Only these two facts are edges -- they are meaningful for
// the frame Observe() returns and nowhere else -- so only these two are returned. Keeping the
// presentation out of here means there is one view of it rather than two contracts to hold in step.
struct StepOutcome
{
    // This halt ended a step rather than being a fresh breakpoint hit. Callers use it to decide
    // whether to surface the Assembly panel: the user asked for a breakpoint halt, but yanking
    // focus back on every step would take it away from whatever they clicked between steps.
    bool     fromStep = false;
    // The halt is at the transient Step Over / Step Out target. Separate from fromStep because
    // the caller both tests this (to skip the condition guard and the access log, which the
    // transient is not subject to) and acts on it (retiring the transient).
    bool     atStepTarget = false;
    // The halt is at the transient's ADDRESS but is not the step arriving: the other SH-2 reached it
    // (the transient is a PC breakpoint, and those are shared). Nothing the user asked for has
    // happened, so the caller resumes without presenting it; the step stays in flight. One report per
    // such halt.
    bool     strayTarget = false;
};

class StepHaltMachine
{
public:
    // --- Per-frame ---

    // Fold this frame's stop report in and report the halt. Call once per poll, before acting on
    // the result: it advances the hold's frame cap.
    StepOutcome Observe(const StopReport& report);

    // The caller resumed instead of surfacing this halt -- a condition guard that did not hold, or
    // an access-log watchpoint. The halt is not presented, so drop it.
    void SuppressHalt();

    // The current halt, for the callers that read it outside a poll: the capture gate (which runs
    // before Observe, so it sees last frame's answer, as it always did), the Assembly panel mirror
    // and the run-control strips.
    bool     HaltActive() const { return mHaltActive; }
    int      HaltCpu()    const { return mHaltCpu; }
    uint32_t HaltPc()     const { return mHaltPc; }

    // The caller has surfaced a halt at the transient target, so the transient has done its job.
    // Marks the breakpoint set dirty so the next sync drops it from the emulator.
    void RetireStepTarget();

    // --- Starting a step ---

    // True when a step is already resuming the CPU. Every step entry point refuses in that case:
    // issuing a second resume while one is in flight loses the first step's re-halt.
    bool StepInFlight() const { return mAwaitingHalt; }

    // A single-instruction step (Step Into): no transient breakpoint, just a resume and a hold.
    // The hold releases on a NEW halt -- a different sequence number from the one showing now (or,
    // from an emulator that numbers nothing, a different PC) -- which is where the step begins; the
    // machine already knows it, so a caller has no chance to pass it wrong.
    void BeginStep();

    // A run-to-address step (Step Over across a call, Step Out, Run to Here): installs the
    // transient breakpoint as well as holding. PC breakpoints are shared across both SH-2s, so the
    // EMULATOR cannot scope the transient to a CPU -- the machine does: the step completes only when
    // 'cpu' reaches 'addr', and the other CPU reaching it is reported as a stray target to resume
    // from. (There is deliberately no check of the stack depth on arrival: a call's delay slot may
    // push or pop, so R15 after the return is not reliably R15 before the call, and a depth test
    // that rejects the real return would leave the step running for ever.)
    void BeginRunTo(uint32_t addr, int cpu);

    // --- Breakpoint sync ---

    bool     StepTargetActive() const { return mStepBpActive; }
    uint32_t StepTargetAddr()   const { return mStepBpAddr; }
    int      StepTargetCpu()    const { return mStepBpCpu; }
    // True when the transient changed since the last sync, so the caller must re-ship the set
    // even if the user's breakpoints did not change. Clears the flag.
    bool     TakeStepTargetDirty();

    // --- Settling after a step ---

    // A stepped frame has to cross the socket before the snapshot re-freezes, so capture stays on
    // for a few frames after a step. Frame control is core ABI, not the live driver, so this half
    // exists in every build -- including the web one, which leaves SE_ENABLE_LIVE undefined.
    void BeginSettle();
    bool Settling() const { return mSettleFrames > 0; }
    // Consume one settle frame. Call only on a frame that actually captured.
    void ConsumeSettleFrame();

    // --- Clearing ---
    //
    // These two look alike and are not interchangeable. The fact that separates them is whether the
    // peer on the other end still holds our transient breakpoint, and both cases are pinned by a
    // test. A single Reset(bool) would hide exactly the thing a reader needs to see.

    // Drop the halt and any in-flight step, and nothing else. Called EVERY FRAME that the source
    // cannot halt -- a static dump, or a live connection that went away mid-step -- so it is an
    // idempotent clamp rather than a lifecycle event. The transient breakpoint is deliberately left
    // installed: it lives in the emulator, which this call does not speak to, and the sync path is
    // what removes it.
    void ResetHalt();

    // The whole machine, for an emulator process we have not talked to before. Nothing is in flight
    // on something that just started, and it holds none of our breakpoints -- so unlike ResetHalt
    // this one drops the transient and leaves the dirty flag SET, to force a full re-sync.
    void ResetForNewEmulator();

private:
    // Is this report a halt the machine has not shown yet? By sequence number when the emulator
    // supplies one; by PC against the halt being shown otherwise.
    bool IsNewStop(const StopReport& r) const;

    int      mSettleFrames   = 0;       // frames to keep capturing after a step
    bool     mHaltActive     = false;   // presentation state, mirrored to the Assembly panel
    int      mHaltCpu        = 0;
    uint32_t mHaltPc         = 0;
    bool     mHaltHasSeq     = false;   // the halt showing was numbered by the emulator...
    uint32_t mHaltSeq        = 0;       // ...with this number
    bool     mStrayHasSeq    = false;   // the last stray target was numbered...
    uint32_t mStraySeq       = 0;       // ...with this one (so its echo is not reported again)
    bool     mStepBpActive   = false;   // transient Step Over / Out breakpoint installed
    uint32_t mStepBpAddr     = 0;
    int      mStepBpCpu      = 0;       // the CPU whose arrival at mStepBpAddr completes the step
    bool     mStepBpDirty    = false;
    bool     mAwaitingHalt   = false;   // a step is in flight; hold the halted presentation
    int      mHoldFrames     = 0;       // safety cap on that hold
};

}  // namespace sfe
