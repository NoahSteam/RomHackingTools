#pragma once

// The decompiler's record of the flow as first followed (before any action) and the two decisions of the click/token mapping that are
// made from it alone, so that a test can build a small flow and ask them directly (area 7, external review rounds 1 to 3: A7-340,
// A7-341). Nothing here is part of the product's interface: the records are filled by Sh2Decompiler.cpp's RecordFlow.

#include "funcdata.hh"

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace sfe
{
namespace decomp
{

// A7-22, A7-23: one varnode of the flow's p-code, by its storage (the flow's ops are gone by the time the text is printed).
struct FlowVn
{
    const ghidra::AddrSpace* space = nullptr;
    ghidra::uintb offset = 0;
    ghidra::int4 size = 0;
    bool Constant() const { return space && space->getType() == ghidra::IPTR_CONSTANT; }
};
// ...and one op: what it writes, from what, at which instruction (InstructionOf), and the word a load of a constant address reads.
struct FlowOp
{
    uint32_t insn = 0;
    ghidra::OpCode code = ghidra::CPUI_COPY;
    bool hasOut = false;
    FlowVn out;
    FlowVn in[3];
    int numIn = 0;
    bool slot = false;      // built by the instruction in a delayed branch's slot (PcodeOp::isFromDelaySlot)
    bool literal = false;   // a LOAD of a constant address whose word the image has: `value`
    uint64_t value = 0;
};

struct FlowRecord
{
    std::map<ghidra::uintm, std::pair<int, uint32_t>> ops;   // op time -> (block, position)
    // A volatile register access is a new op the engine makes where the LOAD or STORE was: found
    // again by its instruction and kind.
    std::map<std::pair<uint32_t, bool>, std::vector<std::pair<int, uint32_t>>> accessesByInsn;
    std::vector<std::vector<int>> successors;
    int entryBlock = 0;
    // A7-11: the flow's loads and calls by instruction (the first one's time), and (A7-15) the instructions that call. The instructions
    // are as the engine has them (InstructionOf, not canonical).
    std::map<uint32_t, ghidra::uintm> loadCallTime;
    std::set<uint32_t> callInsns;
    std::map<uint32_t, std::pair<int, uint32_t>> insnBlock;   // A7-19: the block of each instruction and the position of its last op there
    std::vector<std::pair<uint32_t, uint32_t>> ranges;        // ...and each block's lowest and highest instruction (canonical)
    // A7-22, A7-23: every op of the flow by block, in order (the positions of `ops`), each block's predecessors, the position of each
    // instruction's first op, and the first time no flow op has (an op with a later time was made by the actions: a MULTIEQUAL, a copy).
    std::vector<std::vector<FlowOp>> blockOps;
    std::vector<std::vector<int>> predecessors;
    std::map<uint32_t, std::pair<int, uint32_t>> insnFirst;
    ghidra::uintm opsEnd = 0;
    // A7-38: no address of the frame leaves the stack pointer and the temporaries an access computes from it (FrameKept), so a store
    // through a pointer the walk cannot place does not write a frame word, as the engine takes it when it forwards one
    bool frameKept = false;
};

// Tests only: the guards of the two decisions below, each switchable off so a test can show that it alone separates a case. 0 = all on.
enum DecisionGuard : int
{
    kGuardNone = 0,
    // A7-340 (PriorWriteIsZero)
    kGuardPriorUnplacedStore,   // a store through a pointer the walk cannot place ends the walk: unknown
    kGuardPriorCall,            // a call ends the walk: unknown
    kGuardPriorCallOther,       // so does an engine operation (CALLOTHER)
    kGuardPriorEntry,           // a path that reaches the function's entry without a write has no prior zero
    kGuardPriorOverlap,         // only a store that writes the byte is the path's write
    kGuardPriorFolded,          // ...and it is one of the folded ordinary stores
    kGuardPriorZero,            // ...and it writes the number 0
    // A7-341 (OperandMovesToStore)
    kGuardMoveFlowOp,           // both stores are in the recorded flow
    kGuardMoveSameBytes,        // the same start address
    kGuardMoveSameWidth,        // ...and the same width
    kGuardMoveOperandRegister,  // the operand is the register the name's store stores
    kGuardMoveNameInput,        // ...which no instruction wrote since the entry
    kGuardMoveOtherResolved,    // the other store's source is resolved
    kGuardMoveOtherNotInput,    // ...and on no path is the operand's own input
    kGuardCount
};
void SetDecisionGuardOffForTests(int guard);

// A7-340: is the byte `byte` (a place in memory), before op `pos` of flow block `blk`, on EVERY path into the op, last written by a store
// of the number 0? The walk goes back along the recorded flow; the write of a path is the nearest op that may write the byte: a store
// the walk places at the byte (its width covering it), whatever it stores; a store through a pointer it cannot place, a call or an engine
// operation, which may write it, and then nothing is known. A path that reaches the function's entry first holds the memory's own
// value: no prior zero. `counted` are the (block, position) of the stores that count (the ones the engine folded into a statement).
// False unless every path ends at a counted store of 0; bounded, and false past the bound.
bool PriorWriteIsZero(const FlowRecord& flow, const ghidra::Funcdata& fd, int blk, int pos, const FlowVn& byte,
                      const std::set<std::pair<int, uint32_t>>& counted);

// A7-341: one store as the decision sees it: the bytes it writes and its op in the flow (`blk` -1: not found).
struct StoreSite
{
    const ghidra::AddrSpace* space = nullptr;
    ghidra::uintb offset = 0;
    ghidra::int4 size = 0;
    int blk = -1;
    uint32_t pos = 0;
};

// A7-341: may the token that names the input register `operand` (`param_1`, `in_r4`: the varnode the printer gave, the register's whole
// width) and is drawn at the store `other` be drawn at the store `name` instead? Only when `name` stores that register, which no instruction
// has written since the entry, `other` writes the same bytes with the same width, and `other`'s source is resolved and is shown, on every
// path, not to come from the operand's own input: not read from the register or through register moves, and not made from it by any op
// whose stored bits change with it (an extension, a mask, `or r4,r4`, a sum; a load, a call or an engine operation is never shown). 'May be
// the same input' is unknown, not another value. The token must stay at `other` otherwise.
bool OperandMovesToStore(const FlowRecord& flow, const ghidra::Funcdata& fd, const FlowVn& operand, const StoreSite& name, const StoreSite& other);

}  // namespace decomp
}  // namespace sfe
