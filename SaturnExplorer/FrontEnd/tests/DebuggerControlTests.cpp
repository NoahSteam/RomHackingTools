// Unit tests for DebuggerControl: which context a debugger / transport command acts on while the
// user is scrubbed back in the frame history, and whether it ends the scrub. Every command kind is
// walked in every state, so a new kind that is not classified fails here rather than in the field
// (Continue from the Call Stack / Assembly panels did nothing while scrubbed).
#include "DebuggerControl.h"

#include <cstdio>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

const DebugCommand kRun[] = {
    DebugCommand::Continue, DebugCommand::StepInto, DebugCommand::StepOver, DebugCommand::StepOut,
    DebugCommand::RunToHere, DebugCommand::StepFrame, DebugCommand::Resume,
};
const DebugCommand kConfigure[] = {
    DebugCommand::Pause, DebugCommand::ConfigureBreakpoints, DebugCommand::ConfigureTracepoints,
    DebugCommand::AccessLog, DebugCommand::Poke, DebugCommand::Input,
};
}  // namespace

int main()
{
    for (DebugCommand c : kRun)
    {
        Check(MovesEmulator(c), "run command moves the emulator");
        // Scrubbed with a live emulator: the live one.
        CommandPlan s = PlanCommand(c, true, true);
        Check(s.useLive, "run while scrubbed acts on the live emulator");
        Check(s.leaveScrub, "run while scrubbed leaves the scrub view");
        // Not scrubbed: nothing to leave.
        CommandPlan n = PlanCommand(c, true, false);
        Check(n.useLive && !n.leaveScrub, "run while live: no scrub to leave");
        // No live emulator (a loaded dump): never claims one.
        Check(!PlanCommand(c, false, false).useLive, "no live emulator -> not live");
    }
    for (DebugCommand c : kConfigure)
    {
        Check(!MovesEmulator(c), "configure command does not move the emulator");
        CommandPlan s = PlanCommand(c, true, true);
        Check(s.useLive, "configure while scrubbed reaches the live emulator");
        Check(!s.leaveScrub, "configure while scrubbed keeps the scrub view");
        CommandPlan n = PlanCommand(c, true, false);
        Check(n.useLive && !n.leaveScrub, "configure while live changes nothing");
    }

    // Reads: the halt comes from the live emulator, the displayed frame from the view.
    Check(ReadFromLive(DebugRead::HaltState, true), "halt state is read live");
    Check(!ReadFromLive(DebugRead::DisplayedFrame, true), "displayed frame stays on the view");
    Check(!ReadFromLive(DebugRead::HaltState, false), "no live emulator: nothing to read live");

    // Banner / header PC: the scrubbed bug showed 0601002A (recorded) beside the live 0601002E.
    Check(HeaderPc(true, true, 0x0601002E, 0x0601002A) == 0x0601002E, "scrubbed + halted shows the halt PC");
    Check(HeaderPc(true, false, 0x0601002E, 0x0601002A) == 0x0601002A, "scrubbed, other CPU halted: displayed PC");
    Check(HeaderPc(false, true, 0x0601002E, 0x0601002A) == 0x0601002A, "not scrubbed: banner unchanged");
    Check(RecordedFrameShown(true, 42) == 42 && RecordedFrameShown(false, 42) == 0, "recorded frame label");

    if (gFail) { std::printf("%d failure(s)\n", gFail); return 1; }
    std::printf("DebuggerControl: all passed\n");
    return 0;
}
