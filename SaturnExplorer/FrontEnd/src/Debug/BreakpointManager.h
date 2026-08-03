// BreakpointManager — the one place the app tracks breakpoints, shared by the
// Assembly gutter, the Watch "Break on..." menu, and (later) a Breakpoints panel.
// Emulator-agnostic: it just holds the set + a generation counter; a driver syncs
// the live emulator when the generation changes (Phase 4). No ImGui, no Yabause.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sfe
{

enum class BpKind
{
    Execution,       // SH-2 PC == address
    MemRead,         // read of [address, address+size)
    MemWrite,        // write of that range
    MemReadWrite     // either
};

struct Breakpoint
{
    uint64_t id = 0;
    BpKind   kind = BpKind::Execution;
    int      cpu = 0;            // 0 master, 1 slave (execution BPs)
    uint32_t address = 0;
    uint32_t size = 2;           // memory BPs: 1/2/4; execution: instruction (2)
    bool     enabled = true;
    // Optional guard (ConditionEval syntax, e.g. "r4 == 0x1234"). Evaluated client-side
    // when the emulator halts here: if it's false the client resumes without stopping, so
    // the break only "sticks" when the condition holds. Empty = unconditional. Execution
    // BPs only for now (a memory BP's halt PC is the accessor, not the watched address).
    std::string condition;
};

class BreakpointManager
{
public:
    // Execution BP at 'addr' for 'cpu': add if none present, remove if present.
    // Returns true if a breakpoint now exists there.
    bool ToggleExecution(int cpu, uint32_t addr);
    bool HasExecutionAt(int cpu, uint32_t addr) const;
    const Breakpoint* ExecutionAt(int cpu, uint32_t addr) const;

    // First enabled execution BP at 'addr' that carries a guard, ignoring cpu (a PC
    // breakpoint halts whichever SH-2 reaches the address). nullptr if none — the stop
    // handler uses this to decide whether a halt needs a client-side guard check.
    const Breakpoint* ConditionalExecutionAt(uint32_t addr) const;

    // Add a memory breakpoint (dedup by address+kind+size). Returns its id.
    uint64_t AddMemory(uint32_t addr, uint32_t size, BpKind rw);

    void SetEnabled(uint64_t id, bool enabled);
    void SetCondition(uint64_t id, const std::string& cond);   // client-side guard; no re-sync
    void Remove(uint64_t id);
    void Clear();

    const std::vector<Breakpoint>& All() const { return mBps; }

    // Bumped on every mutation; a live driver re-sends the set when it changes.
    uint64_t Generation() const { return mGeneration; }

private:
    Breakpoint* Find(int cpu, uint32_t addr, BpKind kind, uint32_t size);
    std::vector<Breakpoint> mBps;
    uint64_t mNextId = 1;
    uint64_t mGeneration = 0;
};

}  // namespace sfe
