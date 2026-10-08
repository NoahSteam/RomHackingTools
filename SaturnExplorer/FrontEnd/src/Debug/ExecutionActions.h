// ExecutionActions — the store of non-halting actions attached to instruction
// addresses (see EXECUTION_ACTIONS.md). Phase 1 implements the Log action type
// (tracepoints); the struct is typed so future action types (screenshot, memory
// snapshot, counter, recording, script) slot in without a UI/model redesign.
//
// Emulator-agnostic and ImGui-free: it holds the set + a generation counter, exactly
// like BreakpointManager. A driver syncs the live emulator when the generation
// changes (Phase 2). Breaks still live in BreakpointManager for now; a later cleanup
// folds Break in here as just another ActionType.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sfe
{

enum class ActionType
{
    Log,          // a tracepoint: format its output and push a Log entry
    // future: Screenshot, MemorySnapshot, Counter, StartRecording, StopRecording, ...
};

enum class RepeatMode
{
    EveryTime,    // act on every execution
    Once,         // act on the first execution, then disable
    EveryN        // act on every Nth execution
};

// The bundle of side effects a single tracepoint performs when it fires (the general
// "Actions" checklist from the editor). Write-to-log is the default and only one
// wired in Phase 1; the rest are stored so the editor + wire format are stable.
struct ActionEffects
{
    bool writeToLog   = true;
    bool pauseEmulator = false;
    bool screenshot   = false;
    bool memSnapshot  = false;
    bool playSound    = false;
    bool runScript    = false;
};

struct ExecutionAction
{
    uint64_t     id = 0;
    ActionType   type = ActionType::Log;
    int          cpu = 0;             // 0 master, 1 slave
    uint32_t     address = 0;
    bool         enabled = true;
    // Advanced each time the enabled state changes; sent to the emulator in the descriptor so a
    // re-armed tracepoint is restarted by an explicit token (see SE_LIVE_TP_REARM_SHIFT).
    uint8_t      rearm = 0;
    void         BumpRearm() { rearm = static_cast<uint8_t>((rearm + 1u) & 0xFu); }

    std::string  format;             // Log: output template (FormatString mini-syntax)
    std::string  condition;          // optional guard, e.g. "r0 == 5" (stored; Phase 3 evals)
    RepeatMode   repeat = RepeatMode::EveryTime;
    int          repeatN = 100;      // used when repeat == EveryN
    ActionEffects effects;

    uint64_t     hits = 0;           // times it has fired (updated as events arrive)
    uint64_t     seen = 0;           // executions whose condition held, counted here (see EmulatorAppliesRepeat)
};

// Who applies a tracepoint's repeat policy (Once / Every N) to its executions.
//
// The emulator can, and should: a tracepoint on hot code with "every 1000th" would otherwise queue a
// thousand events to be thrown away, and the queue drops what it cannot hold. But it can apply the
// policy only to what it can see, which is EXECUTIONS -- it cannot evaluate a condition -- so a
// tracepoint with a condition is counted here, over the executions whose condition held. And an
// emulator older than protocol v21 applies nothing, so everything is counted here for it. Either way
// exactly one side counts, which is what keeps "every 3rd" from becoming "every 9th".
constexpr uint32_t kEmulatorAppliesRepeatSince = 21;
inline bool EmulatorAppliesRepeat(const ExecutionAction& a, uint32_t serverVersion)
{
    return serverVersion >= kEmulatorAppliesRepeatSince && a.condition.empty();
}

class ExecutionActions
{
public:
    // Log tracepoint at (cpu,addr): add a default one if none, else remove it.
    // Returns true if a tracepoint now exists there.
    bool ToggleLog(int cpu, uint32_t addr);
    bool HasLogAt(int cpu, uint32_t addr) const;
    const ExecutionAction* LogAt(int cpu, uint32_t addr) const;

    // Add an action and return its id (used by "Create Tracepoint…" from the editor). The repeat
    // count is clamped to what the wire carries; effects that nothing implements yet are dropped
    // (see ImplementedEffects).
    uint64_t Add(const ExecutionAction& a);
    // Replace the mutable fields of an existing action (from the editor's OK).
    void Update(const ExecutionAction& a);

    ExecutionAction*       Get(uint64_t id);
    const ExecutionAction* Get(uint64_t id) const;

    void SetEnabled(uint64_t id, bool enabled);
    void Remove(uint64_t id);
    void Clear();
    // A tracepoint fired and its condition (if any) held: decide whether the user's repeat policy
    // lets it act. Returns false for an action that is gone or disabled -- events already in flight
    // when a Once fired, or the user switched it off -- which must not be acted on. Counts the hit; a
    // fire-once action disables itself HERE and bumps the generation, so the emulator is sent the
    // disable instead of carrying on firing it. 'emulatorApplies' is EmulatorAppliesRepeat for it.
    bool AcceptHit(uint64_t id, bool emulatorApplies);
    // A different emulator: its counts start from nothing, so ours do too. (A re-sent set to the SAME
    // emulator does not restart a tracepoint that did not change, so nothing here is reset by one.)
    void ResetCounts();

    const std::vector<ExecutionAction>& All() const { return mActions; }
    size_t Count() const { return mActions.size(); }

    // Bumped on every structural mutation (add/remove/enable/format change) so a live
    // driver re-syncs the set; a plain hit-count update does NOT bump it.
    uint64_t Generation() const { return mGeneration; }

private:
    std::vector<ExecutionAction> mActions;
    uint64_t mNextId = 1;
    uint64_t mGeneration = 0;
};

}  // namespace sfe
