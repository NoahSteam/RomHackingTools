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

    std::string  format;             // Log: output template (FormatString mini-syntax)
    std::string  condition;          // optional guard, e.g. "r0 == 5" (stored; Phase 3 evals)
    RepeatMode   repeat = RepeatMode::EveryTime;
    int          repeatN = 100;      // used when repeat == EveryN
    ActionEffects effects;

    uint64_t     hits = 0;           // times it has fired (updated as events arrive)
};

class ExecutionActions
{
public:
    // Log tracepoint at (cpu,addr): add a default one if none, else remove it.
    // Returns true if a tracepoint now exists there.
    bool ToggleLog(int cpu, uint32_t addr);
    bool HasLogAt(int cpu, uint32_t addr) const;
    const ExecutionAction* LogAt(int cpu, uint32_t addr) const;

    // Add an action and return its id (used by "Create Tracepoint…" from the editor).
    uint64_t Add(const ExecutionAction& a);
    // Replace the mutable fields of an existing action (from the editor's OK).
    void Update(const ExecutionAction& a);

    ExecutionAction*       Get(uint64_t id);
    const ExecutionAction* Get(uint64_t id) const;

    void SetEnabled(uint64_t id, bool enabled);
    void Remove(uint64_t id);
    void Clear();
    void RecordHit(uint64_t id);     // ++hits (and disable if repeat==Once)

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
