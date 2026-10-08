// Sh2Dump — render the SH-2 disassembly the Assembly panel shows as plain text, for the
// Data > Dump SH-2 command.
//
// The panel is a virtual list over a whole memory region; a dump is the same listing written
// out, row for row, for the code regions (low and high work RAM -- the only captured memory the
// SH-2 runs from). It decodes exactly as the panel does (blocks carrying the preceding opcode, so
// a PC-relative load in a delay slot reads the same) and resolves comments the same way: a user
// note replaces the generated one.
//
// Written as a job stepped a slice per frame rather than one call: a region is half a million
// instructions, and the front end must keep drawing (and show progress) while it runs. Free of
// ImGui and of the core context -- the caller snapshots what it needs into Sh2DumpInput -- so the
// whole thing is unit-testable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"

#include "Debug/Sh2Disasm.h"

namespace sfe
{

// The regions SH-2 code lives in. (Sound RAM holds 68K code; VDP memory holds no code; the BIOS
// and cartridge are not captured.)
struct Sh2DumpRegion { const char* name; uint32_t base; uint32_t size; };
constexpr size_t kSh2DumpRegionCount = 2;
const Sh2DumpRegion& Sh2DumpRegionAt(size_t index);

// Columns and scope. At least one column must be on for a dump to mean anything (see Valid()).
struct Sh2DumpOptions
{
    bool addresses = true;      // "06005210"
    bool bytes = true;          // the raw 16-bit word, "D108"
    bool opcodes = true;        // the decoded instruction: mnemonic and operands
    bool comments = true;       // the Comment column
    bool autoComments = true;   // ...including the generated ones (needs registers); user notes always
    bool regions[kSh2DumpRegionCount] = { true, true };

    bool Valid() const
    {
        return (addresses || bytes || opcodes || comments) && (regions[0] || regions[1]);
    }
};

struct Sh2DumpInput
{
    Sh2DumpOptions opt;
    int            cpu = 0;                  // which SH-2's registers the generated comments resolve against
    bool           haveRegs = false;
    se_sh2_regs    regs = {};
    std::unordered_map<uint32_t, std::string> userComments;   // address -> note (the panel's store)
    // The bytes of each code region, as read from the source; empty when it cannot provide it.
    // Every region is wanted even when not dumped: a generated comment reads literal pools.
    std::vector<uint8_t> memory[kSh2DumpRegionCount];
};

class Sh2DumpJob
{
public:
    explicit Sh2DumpJob(Sh2DumpInput input);

    // Render up to 'maxInstructions' more rows. Returns true once everything is written.
    bool Step(uint32_t maxInstructions);

    bool   Done() const { return mDone; }
    float  Progress() const { return mTotal ? static_cast<float>(mEmitted) / static_cast<float>(mTotal) : 1.0f; }
    // Rows the dump will hold in all (the selected, available regions).
    uint64_t TotalInstructions() const { return mTotal; }
    // The text so far; complete once Done().
    const std::string& Text() const { return mText; }
    std::string        TakeText() { return std::move(mText); }

    // Rows a dump of 'opt' would hold given which regions have bytes, and a rough size of its
    // text -- for the options dialog to say what it is about to write.
    static uint64_t EstimateInstructions(const Sh2DumpOptions& opt, const bool available[kSh2DumpRegionCount]);
    static uint64_t EstimateBytes(const Sh2DumpOptions& opt, uint64_t instructions);

private:
    void BeginRegion();
    void EmitRow(uint32_t addr, uint16_t op, bool readable, const DisassembledInstruction& ins);

    Sh2DumpInput mIn;
    std::string  mText;
    size_t       mRegion = 0;       // the region being written
    uint32_t     mRow = 0;          // next row within it
    bool         mRegionOpen = false;
    bool         mHeaderDone = false;
    bool         mDone = false;
    uint64_t     mTotal = 0;
    uint64_t     mEmitted = 0;
};

}  // namespace sfe
