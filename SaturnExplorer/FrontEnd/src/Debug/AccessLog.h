// AccessLog — the record behind "find out what accesses this address" (Cheat Engine's
// "find out what writes/reads to this address"). When a logging data watchpoint fires, the
// accessing instruction's PC and its reconstructed call stack are recorded here; repeat hits
// from the same instruction collapse into one row with a hit count, so a busy address yields
// a short list of accessors instead of an endless stream. Emulator-agnostic and ImGui-free
// (it only holds CallStackFrame values), so it is unit-testable.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Debug/CallStack.h"   // CallStackFrame

namespace sfe
{

struct AccessRecord
{
    uint32_t                    pc = 0;         // the instruction that made the access
    int                         cpu = 0;        // 0 master, 1 slave
    uint64_t                    count = 0;      // how many times it has hit
    uint32_t                    lastFrame = 0;  // emulated frame of the most recent hit
    std::string                 insn;           // disassembly of the instruction (decoded once)
    std::vector<CallStackFrame> stack;          // call stack reconstructed at the latest hit
};

class AccessLog
{
public:
    // Record one hit from instruction 'pc' on 'cpu' ('insn' = its disassembly, decoded by the
    // caller). Merges into the existing row for that (cpu, pc) — bumping its count and
    // replacing its stack with the latest — or appends a new row (kept in first-seen order).
    void Record(uint32_t pc, int cpu, uint32_t frame, std::string insn,
                std::vector<CallStackFrame> stack);

    void        Clear();
    bool        Empty() const { return mRecords.empty(); }
    std::size_t Size()  const { return mRecords.size(); }
    const std::vector<AccessRecord>& Records() const { return mRecords; }

private:
    static uint64_t Key(uint32_t pc, int cpu) { return (uint64_t(cpu ? 1u : 0u) << 32) | pc; }

    std::vector<AccessRecord>            mRecords;   // first-seen order
    std::unordered_map<uint64_t, std::size_t> mIndex;  // (cpu,pc) -> index into mRecords
};

}  // namespace sfe
