// The repeat policy of a tracepoint (Once / Every N) and what the editor may claim.
//
// Reproduces the failures: RecordHit disabled a fire-once tracepoint locally without changing the
// generation, so the emulator was never told and carried on reporting it; the next event for the
// disabled action was still accepted; and "Every N" counted nothing at all.
#include "Debug/ExecutionActions.h"

#include <cstdio>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

uint64_t AddLog(ExecutionActions& a, RepeatMode mode, int n = 1, const char* cond = "")
{
    ExecutionAction x;
    x.type = ActionType::Log;
    x.address = 0x06001000;
    x.format = "PC={pc}";
    x.repeat = mode;
    x.repeatN = n;
    x.condition = cond;
    return a.Add(x);
}

// How many of 'n' qualifying executions the policy lets act, counting them as App does.
int Acts(ExecutionActions& a, uint64_t id, int n, bool emulatorApplies = false)
{
    int acted = 0;
    for (int i = 0; i < n; ++i) acted += a.AcceptHit(id, emulatorApplies) ? 1 : 0;
    return acted;
}
}  // namespace

int main()
{
    // Every time.
    {
        ExecutionActions a;
        const uint64_t id = AddLog(a, RepeatMode::EveryTime);
        Check(Acts(a, id, 7) == 7, "every time acts on every hit");
        Check(a.Get(id)->hits == 7, "and counts them");
    }

    // Once: acts on the first, disables itself, and the emulator is TOLD (generation moves).
    {
        ExecutionActions a;
        const uint64_t id = AddLog(a, RepeatMode::Once);
        const uint64_t before = a.Generation();
        Check(a.AcceptHit(id, false), "once acts on the first hit");
        Check(!a.Get(id)->enabled, "then disables itself");
        Check(a.Generation() != before, "and the generation moves, so the set is re-sent with it off");
        Check(!a.AcceptHit(id, false), "an event already in flight for the disabled action is refused");
        Check(!a.AcceptHit(id, true), "whoever applies the policy");
        Check(a.Get(id)->hits == 1, "it fired once");

        // Switched back on, it is armed again.
        a.SetEnabled(id, true);
        Check(a.AcceptHit(id, false), "re-enabled, it fires once more");
        Check(!a.Get(id)->enabled, "and spends itself again");

        // Each change of the enabled state advances the re-arm token sent to the emulator, so a
        // disable+enable that is collapsed into one update still differs from what it installed.
        const uint8_t t0 = a.Get(id)->rearm;
        a.SetEnabled(id, true);
        Check(a.Get(id)->rearm != t0, "re-enabling advances the re-arm token");
    }

    // Every N, counted here: the Nth, 2Nth... qualifying execution.
    {
        ExecutionActions a;
        const uint64_t id = AddLog(a, RepeatMode::EveryN, 3);
        int pattern = 0;
        for (int i = 1; i <= 9; ++i) pattern = pattern * 2 + (a.AcceptHit(id, false) ? 1 : 0);
        Check(pattern == 0b001001001, "every 3rd of 9 acts: 3, 6, 9");
        Check(a.Get(id)->hits == 3, "three fired");
        Check(a.Get(id)->enabled, "and it stays on");
    }
    // N of 1 is every time.
    {
        ExecutionActions a;
        const uint64_t id = AddLog(a, RepeatMode::EveryN, 1);
        Check(Acts(a, id, 5) == 5, "every 1st is every time");
    }

    // Where the emulator applies the policy, it is not applied twice: events delivered ARE the
    // firings, so counting them again would turn "every 3rd" into "every 9th".
    {
        ExecutionActions a;
        const uint64_t id = AddLog(a, RepeatMode::EveryN, 3);
        Check(Acts(a, id, 6, /*emulatorApplies=*/true) == 6, "an emulator-filtered every-3rd acts on what arrives");
        const uint64_t once = AddLog(a, RepeatMode::Once);
        Check(a.AcceptHit(once, true), "an emulator-filtered once acts on its one event");
        Check(!a.Get(once)->enabled, "and is disabled here too");
    }

    // Who applies it.
    {
        ExecutionActions a;
        ExecutionAction plain;  plain.repeat = RepeatMode::EveryN; plain.repeatN = 5;
        ExecutionAction guarded = plain; guarded.condition = "r0 == 5";
        Check(EmulatorAppliesRepeat(plain, 21), "an unconditional tracepoint on a v21 server");
        Check(!EmulatorAppliesRepeat(guarded, 21), "not one with a condition: the emulator cannot evaluate it");
        Check(!EmulatorAppliesRepeat(plain, 20), "not on a server that predates the policy");
        Check(!EmulatorAppliesRepeat(plain, 0), "nor one whose version is not known");
    }

    // Counting is over the executions that qualified, which is what the caller passes in: a
    // condition that did not hold never reaches AcceptHit. A fire-once one is therefore not spent
    // by an execution its condition rejected.
    {
        ExecutionActions a;
        const uint64_t id = AddLog(a, RepeatMode::Once, 1, "r0 == 5");
        // (the caller skips the executions where r0 != 5 without calling AcceptHit)
        Check(a.Get(id)->enabled, "a fire-once guarded tracepoint is still armed");
        Check(a.AcceptHit(id, false), "and fires on the first execution that qualifies");
    }

    // A count restart: editing the policy counts afresh, and a different emulator starts from nothing.
    {
        ExecutionActions a;
        const uint64_t id = AddLog(a, RepeatMode::EveryN, 3);
        Acts(a, id, 2);
        ExecutionAction e = *a.Get(id);
        a.Update(e);
        Check(Acts(a, id, 1) == 0, "after an edit the count restarts (the next is the 1st, not the 3rd)");
        Acts(a, id, 1);
        a.ResetCounts();
        Check(Acts(a, id, 2) == 0, "a new emulator restarts it");
        Check(Acts(a, id, 1) == 1, "so the 3rd after the reset acts");
    }

    // What the editor offers is what happens.
    {
        ExecutionActions a;
        ExecutionAction x;
        x.effects.pauseEmulator = true;
        x.effects.screenshot = true;
        x.effects.memSnapshot = true;
        x.effects.playSound = true;
        x.effects.runScript = true;
        x.effects.writeToLog = true;
        x.repeat = RepeatMode::EveryN;
        x.repeatN = 0x7FFFFFFF;
        const uint64_t id = a.Add(x);
        const ExecutionAction* got = a.Get(id);
        Check(!got->effects.pauseEmulator && !got->effects.screenshot && !got->effects.memSnapshot &&
              !got->effects.playSound && !got->effects.runScript,
              "effects nothing implements cannot be claimed");
        Check(got->effects.writeToLog, "logging is kept");
        Check(got->repeatN == 0xFFFFFF, "the repeat count is clamped to what the wire carries");
        ExecutionAction y = *a.Get(id);
        y.repeatN = -4;
        y.effects.pauseEmulator = true;
        a.Update(y);
        Check(a.Get(id)->repeatN == 1 && !a.Get(id)->effects.pauseEmulator, "an edit is normalised the same way");
    }

    if (gFail) { std::printf("%d check(s) failed\n", gFail); return 1; }
    std::printf("ExecutionActionsTests: all checks passed\n");
    return 0;
}
