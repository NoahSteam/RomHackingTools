// DebuggerControl -- which context a debugger / transport command acts on while the user is scrubbed
// back in the frame history, and whether it ends the scrub, kept apart from App so the rule is one
// table that the tests can walk and no call site can drift from.
//
// While paused, the user can scrub the recorded frames. The panels then draw from a read-only
// context rebuilt from the recording (the "scrub view"); the real emulator is the live context. A
// command sent to the scrub view reaches nobody: Continue from the Call Stack and Assembly panels
// resumed the scrub view, so the emulator never moved, while the transport (which already used the
// live context) worked.
//
// The rule:
//  - Every command goes to the LIVE emulator when there is one.
//  - A command that makes the emulator RUN or STEP also ends the scrub, as the transport's Play does:
//    the user is asking the game to move on from where it really is (App::LeaveScrubForRun).
//  - A command that only CONFIGURES the emulator (breakpoints, tracepoints, watchpoints, the access
//    log, pokes, input) leaves the scrub view up: the user is still inspecting a recorded frame.
//  - A read that describes the HALT (why it stopped, the live PC, whether it can step) comes from the
//    live emulator; a read that describes the DISPLAYED frame (registers, memory, the call stack
//    shown) stays on the scrub view.
#pragma once

namespace sfe
{

enum class DebugCommand
{
    // Run or step: moves the emulator.
    Continue,
    StepInto,
    StepOver,
    StepOut,
    RunToHere,
    StepFrame,
    Resume,                 // the transport's Play / the Pause toggle's resume half
    // Does not move the emulator.
    Pause,
    ConfigureBreakpoints,   // breakpoints, watchpoints, the transient step breakpoint
    ConfigureTracepoints,
    AccessLog,              // "find what accesses this" watchpoint requests
    Poke,                   // memory / register writes
    Input,                  // controller state
};

enum class DebugRead
{
    HaltState,       // halt reason, the halt PC, "can step"
    DisplayedFrame,  // registers, memory, call stack as shown
};

struct CommandPlan
{
    bool useLive;        // act on the live emulator, not the scrub view
    bool leaveScrub;     // end the scrub, as Play does
};

inline bool MovesEmulator(DebugCommand c)
{
    switch (c)
    {
        case DebugCommand::Continue:
        case DebugCommand::StepInto:
        case DebugCommand::StepOver:
        case DebugCommand::StepOut:
        case DebugCommand::RunToHere:
        case DebugCommand::StepFrame:
        case DebugCommand::Resume:
            return true;
        default:
            return false;
    }
}

// 'hasLive': a live emulator context exists apart from the scrub view. 'scrubbing': the user has a
// recorded frame selected (the scrub view is, or is about to be, up).
inline CommandPlan PlanCommand(DebugCommand c, bool hasLive, bool scrubbing)
{
    CommandPlan p;
    p.useLive = hasLive;
    p.leaveScrub = MovesEmulator(c) && scrubbing;
    return p;
}

// Whether a read comes from the live emulator (true) or from the context the panels display.
inline bool ReadFromLive(DebugRead r, bool hasLive)
{
    return hasLive && r == DebugRead::HaltState;
}

// The recorded frame a panel should say it is showing instead of the halt: 0 when the panel shows the
// live state. 'viewUp' is the scrub view being the displayed context, 'shownFrame' its frame number.
inline unsigned long long RecordedFrameShown(bool viewUp, unsigned long long shownFrame)
{
    return viewUp ? shownFrame : 0ull;
}

// Which PC the Call Stack banner and the Assembly header show for a CPU. Halted on that CPU: the
// halt PC (the live one, which is what "BREAKPOINT HIT at" means) -- but only while a recorded frame
// is shown; not scrubbed, the banner keeps reading the captured registers as it always did. Anything
// else: the displayed registers.
inline unsigned HeaderPc(bool recordedShown, bool haltedOnCpu, unsigned haltPc, unsigned displayedPc)
{
    return (recordedShown && haltedOnCpu) ? haltPc : displayedPc;
}

}  // namespace sfe
