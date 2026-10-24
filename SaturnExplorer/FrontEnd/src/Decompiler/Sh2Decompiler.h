// Sh2Decompiler — decompile one SH-2 function from a MemorySnapshot into lines and tokens.
//
// The engine wrapper the worker thread calls (DESIGN.md 4, PLAN.md Step 2). Per request it
// builds a fresh SaturnArchitecture over the snapshot and runs two passes:
//
//   1. Follow flow from the entry. Walk the raw p-code: every LOAD through an instruction-local
//      constant pointer is a literal-pool word, marked read-only so it folds to its value (only
//      those words: RAM globals stay globals). The same walk decodes each instruction with
//      Sh2Decode and flags a PC-relative load or mova in a delay slot (A3, below). Decompile, and
//      collect the constant call targets the folding exposed (jsr @rN through a literal pool).
//   2. On a fresh Architecture, register those callees as functions -- so calls print as
//      func_0x06005000(...) rather than (*(code *)0x6005000)(...) -- apply the same read-only
//      words, decompile again, and print with markup. Each token carries the address of the
//      SH-2 instruction its p-code came from; that is what clicking a line navigates to.
//
// A3, Step 2 policy: on SH-2 a PC-relative load or mova in the delay slot of a taken branch
// computes its address from the branch target, while the SLEIGH spec uses the slot's own
// address. The function is marked unsupported with the slot's address, and that load's literal
// is never marked read-only, so no possibly-wrong constant is folded into the output.
//
// Never throws: every engine error becomes a warning or a Failed status. Each call builds a fresh
// Architecture and destroys it before returning, so a failed or cancelled decompilation leaves
// no engine state behind for the next one: the "rebuild after a hard failure" of PLAN.md Step 2
// happens on every request. The exception is running out of memory inside the engine, which is
// not exception-safe there and can damage the process-global translator; after that, every
// request is answered Unavailable for the life of the process. No Ghidra type appears in this header. Engine thread only.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Decompiler/MemorySnapshot.h"

namespace sfe
{
namespace decomp
{

// A data type as plain values, so a prototype learned from one decompilation can be handed to
// another (each builds its own Architecture, whose types are not shared). One level of pointer.
enum class TypeKind : uint8_t { Unknown, Int, Uint, Bool, Pointer, Void };
struct TypeShape
{
    TypeKind kind = TypeKind::Unknown;
    uint8_t size = 0;                        // bytes; 0 for Void
    TypeKind pointee = TypeKind::Unknown;    // Pointer: what it points to
    uint8_t pointeeSize = 1;
    bool operator==(const TypeShape& o) const
    {
        return kind == o.kind && size == o.size && pointee == o.pointee && pointeeSize == o.pointeeSize;
    }
};

// What a call to a function can do to its caller's state, as verified when the function was
// decompiled (Sh2Decompiler.cpp, ReturnCheck), so a caller's own return-address proof can rely on it.
// Bits name where a value can come from, in the callee's terms: origin::*. The defaults assume the
// worst.
// The comment printed at a call that a frame address reaches in a register its arguments do not show (the address of a returned
// structure, in r2: A3-4). The pseudocode alone would read as a call that cannot touch the local.
// (area 3 round 13 fix, A3-105) "may keep it and write any local": the address goes out (the engine's escape, the whole frame), and what a callee
// keeps a later step may walk down from; "from there up" was the alias of A3-48, which reached only the locals at and above it.
constexpr const char* kFrameEscapeNote = "a frame address is in r2 here, in none of the arguments: the call may keep it and write any local";
// ...and the same for r7 (A3-48): the engine fills in at most two unset registers before an argument it sees, so a frame address in r7 with r4-r6 all
// untouched is dropped from the call's arguments.
constexpr const char* kFrameEscapeNoteR7 = "a frame address is in r7 here, in none of the arguments: the call may keep it and write any local";
// ...and for the call's stack argument slots (A3-51): the engine drops an argument that follows three unset registers, the stack word with it.
constexpr const char* kFrameEscapeNoteStack = "a frame address is in a stack argument slot here, in none of the arguments: the call may keep it and write any local";
// ...and for a local the function wrote at or above the stack pointer before a call to a function with no lent signature and reads after it (A3-61):
// the callee's stack arguments are those words, and a hand-written one may hand a value back in its own argument slot.
constexpr const char* kFrameEscapeNoteSlot = "a local at or above the stack pointer is read after this call, which may write it as one of its stack arguments";
// ...(area 3 round 13 fix, A3-108) and the same for a word that holds a saved register's entry value, which is no local: the epilogue's pop reads it.
constexpr const char* kFrameEscapeNoteSaved = "a saved register's word at or above the stack pointer is read after this call, which may write it as one of its stack arguments";
// (A3-108) A note that holds at more calls than the first is said once, at the first, with this added.
constexpr const char* kFrameEscapeNoteLikewise = "; likewise at the calls below";
// ...and for a frame address in another register a callee with no lent signature stores through, lets out or hands back (area 3 round 12 fix:
// r0, r1, r3, r8-r14, GBR, MACH, ..., which the engine never takes for an argument). The register's name goes between the two parts.
constexpr const char* kFrameEscapeNoteRegA = "a frame address is in ";
constexpr const char* kFrameEscapeNoteRegB = " here, in none of the arguments: the call may keep it and write any local";
// (area 3 round 14 fix, A3-120) ...and the same where the host knows only that a frame address may be there (a word a callee handed the frame may
// have written, a pointer loaded once an address of the frame went out): "is" is kept for a frame address this function made from r15 and holds
// there on every path (the probe's Value::Sure).
constexpr const char* kFrameEscapeNoteMay = "a frame address may be in r2 here, in none of the arguments: the call may keep it and write any local";
constexpr const char* kFrameEscapeNoteMayR7 = "a frame address may be in r7 here, in none of the arguments: the call may keep it and write any local";
constexpr const char* kFrameEscapeNoteMayStack = "a frame address may be in a stack argument slot here, in none of the arguments: the call may keep it and write any local";
constexpr const char* kFrameEscapeNoteMayRegA = "a frame address may be in ";

namespace origin
{
// 0-14 r0-r14, 15 r15, 16 sr, 17 gbr, 18 vbr, 19 mach, 20 macl, 21 pr: that register's value on entry.
constexpr uint32_t kRegisters = (1u << 22) - 1;
constexpr uint32_t kStack = 1u << 24;    // an address in the stack (the callee's, or above: the caller's)
constexpr uint32_t kMemory = 1u << 25;   // loaded from memory not tracked
constexpr uint32_t kFrame = 1u << 26;    // (storesThrough) wrote at or above its entry r15
constexpr uint32_t kAll = kRegisters | kStack | kMemory | kFrame;
}
struct CallEffects
{
    uint32_t preserved = 0;               // registers equal to their entry values at every return (r15: bit 15)
    uint32_t storesThrough = origin::kAll;   // where the addresses it stores through can come from
    uint32_t outOrigins = origin::kAll;      // where the registers it does not preserve can come from
    uint32_t leaks = origin::kAll;           // where the values it stores to memory it does not track come from
    uint32_t frameValues = origin::kAll;     // ...and the values it may write at or above its entry r15
    // ...and where: bit k (k < 31) is the word at its entry r15 + 4k, written at that exact offset (a spill of an argument register into the home
    // area its caller reserved); kFrameAny is a write at an offset it cannot place, or past the 124th byte. A caller clobbers the words named, not
    // every slot from the call's r15 up, the saved PR among them (A3-31). Unknown: any.
    static constexpr uint32_t kFrameAny = 1u << 31;
    uint32_t frameWords = 0xFFFFFFFFu;
    // r15 at every return minus r15 on entry, when it is the same constant at all of them: 0 when r15 is
    // preserved, else the bytes the function leaves on the stack (negative) or takes off its caller's
    // (positive). kSpUnknown when it differs between returns or is not a constant: a function that cannot
    // say is not lent a signature (WithholdReason), and a caller's analysis takes r15 to be anything.
    static constexpr int32_t kSpUnknown = INT32_MIN;
    int32_t spDelta = kSpUnknown;
    // The registers (bit r: r0-r14) the function may store through at an offset below the entry value it was handed in that register -- a
    // negative displacement, or a pointer stepped down in a loop (`*--p = v`, A3-71). The engine's reach for a pointer argument is the object
    // at the address and what lies above it, so a caller that hands a lent callee an exact frame address must expose the locals below it as
    // well when this names that register. Unknown (a callee whose signature is not lent): every register, taken conservatively.
    uint32_t storesBelow = origin::kRegisters;
    bool operator==(const CallEffects& o) const
    {
        return preserved == o.preserved && storesThrough == o.storesThrough && outOrigins == o.outOrigins &&
               leaks == o.leaks && frameValues == o.frameValues && frameWords == o.frameWords && spDelta == o.spDelta &&
               storesBelow == o.storesBelow;
    }
};

// A function's signature as the engine recovered it when it decompiled that function: what a
// call to it passes and returns. Given for a callee, it is locked, so the caller's call shows
// that many arguments and that return instead of guessing from the registers it sets up.
struct Prototype
{
    bool known = false;
    TypeShape ret;
    std::vector<TypeShape> params;
    CallEffects effects;
    bool operator==(const Prototype& o) const
    {
        return known == o.known && ret == o.ret && params == o.params && effects == o.effects;
    }
    bool operator!=(const Prototype& o) const { return !(*this == o); }
};

struct DecompileRequest
{
    // Where the function executes: any mirror. It is analysed there, since PC-derived values (mova,
    // PC-relative call targets) follow the address; the result reports the canonical entry.
    uint32_t entry = 0;
    std::vector<std::pair<uint32_t, std::string>> names;      // address -> name (FunctionNames)
    std::vector<uint32_t> knownFunctions;                     // extra entries to register
    // Callee -> its decompiled signature, keyed by the address it runs at (as calledAt reports): a
    // signature learned through one mirror is not applied to calls through another.
    std::vector<std::pair<uint32_t, Prototype>> prototypes;
    bool registerCallees = true;                              // run the second pass
    uint32_t maxInstructions = 20000;                         // A4: bounds the uncheckable segments
    // Memory another processor changes -- a mailbox shared with the slave SH-2 or the sound CPU --
    // as inclusive [first, last] ranges. Every access there is kept, like a hardware register's
    // (the registers and sound RAM always are). A cached or cache-through address covers both.
    std::vector<std::pair<uint32_t, uint32_t>> volatileMemory;
    // Test instrumentation: every address is volatile, so no access folds away and the access
    // ledger lists every memory instruction the flow reaches (literal pools still fold: read-only
    // wins). The ledger of an ordinary run is checked against it: an access missing there must be
    // one of ordinary memory at a constant address, which may fold, and nothing else.
    bool allMemoryVolatile = false;
};

// "0x26001000-0x26001003, 25A01000": inclusive ranges, or single bytes, hex with or without 0x,
// separated by commas, spaces or new lines. False with 'error' naming the first bad entry.
bool ParseAddressRanges(const std::string& text, std::vector<std::pair<uint32_t, uint32_t>>& out, std::string& error);

enum class TokenKind : uint8_t { Plain, Keyword, Type, FuncName, Variable, Param, Const, Comment, Global, Label, Error };

struct Token
{
    uint16_t begin = 0, end = 0;   // byte range in Line::text
    TokenKind kind = TokenKind::Plain;
    uint32_t addr = 0;             // the instruction the token came from; 0 = none
    uint32_t target = 0;           // a called function's entry, for FuncName tokens; 0 = none
    uint32_t calledAt = 0;         // ...and the exact address called (a mirror, if through one); 0 = none
};

struct Line
{
    std::string text;
    std::vector<Token> tokens;
    uint32_t firstAddr = 0;        // the lowest token address on the line; 0 = none
};

// The instructions a line was decompiled from: the distinct non-zero token addresses, ascending
// (canonical). A line is a statement, and a statement holds the p-code of every instruction whose
// result it combines (a pointer built in one place, an index in another, the load in a third), so
// one line is often several instructions, not always next to each other. Navigation (a click, the
// menu) goes to the first only.
std::vector<uint32_t> LineInstructions(const Line& line);

// What hovering a line says (A7-1): that the number is the address of the SH-2 instruction the
// line came from -- not a value in the line -- that a click shows it in SH-2 Assembly, and, when
// the line is several instructions, how many and which. Empty for a line with no address (a
// declaration, a brace), where a click does nothing.
std::string LineOriginText(const Line& line);

// Where the engine is within one decompilation. Two passes repeat Init..Actions; only the last
// prints. The cancel check-points are in Flow and Actions; Init, Print and Teardown have none
// (PLAN.md A4), so a cancel requested there takes effect at the next check-point, if any. The
// first pass's last check-point comes after its Teardown (its Architecture is gone), and is
// reported as Actions, the phase the pass was in: a cancel raised while that pass was torn down is
// honoured at once, not carried through the second pass's Init into its Flow (A6-35).
enum class DecompilePhase : uint8_t { Idle, Init, Flow, Actions, Print, Teardown };
const char* PhaseName(DecompilePhase phase);

// Something flow recovery could not settle: the engine's own warnings (an unrecovered jump table
// treated as a call, bad instruction data, overlapping instructions, unreachable code removed) and
// ours (flow running into another function's entry). The address is the instruction it concerns,
// canonical; 0 when it concerns the function as a whole.
struct FlowWarning
{
    // Cut: flow left captured memory (truncated*); BadData: badDataAt; IntoFunction: flow ran into
    // another known entry; JumpTable: a table recovered incompletely (cases may be missing);
    // TailJump: an indirect jump taken for a tail call; Engine: anything else the engine said.
    enum Kind : uint8_t { Engine, Cut, BadData, IntoFunction, JumpTable, TailJump } kind = Engine;
    uint32_t address = 0;
    std::string text;
};

// An access to a hardware register that the pseudocode, which shows only the register's name,
// does not explain. A caution says the output may mislead: a read of a write-only register (it
// does not return what was written), a write to a read-only one (it does nothing), one access
// covering two registers the manuals do not pair, a watchdog write with the wrong key or width.
// The rest explain: a documented register pair accessed whole, what a keyed write requests, a
// register reached through a mirror (shown by address), what a read does besides returning a value
// (a FIFO pop, a latch, flags cleared), and what a register whose reads and writes reach different
// fields returns when the function also writes it. The address is the
// instruction's, canonical.
struct HardwareNote
{
    uint32_t address = 0;
    std::string text;
    bool caution = true;
};

// One memory access the final analysis keeps -- a volatile read or write of a constant address,
// or a load or store through a pointer -- and how the pseudocode shows it. The access ledger:
// tests compare it with the operations the SH-2 code makes, so an access that disappears, or one
// printed more often than it happens, shows up even where the text reads plausibly. Accesses to
// ordinary memory at constant addresses are values, not operations, and are not listed.
struct MemoryAccess
{
    bool write = false;
    bool direct = false;        // a constant address; else through a pointer
    uint32_t address = 0;       // the constant address (as the code uses it); 0 through a pointer
    uint8_t size = 0;
    uint32_t insn = 0;          // the instruction, canonical
    bool inLoop = false;        // in a block that can reach itself
    bool returned = false;      // a read whose value is the return value, through copies and extensions
    // How many times the pseudocode shows it: a write once, a read once per emission of its value
    // (an implied expression is printed in place at each use). 0 when nothing printed comes from it.
    int printed = 0;
    int printedLines = 0;       // output lines holding a token printed from it (a statement can wrap)
    int printedTokens = 0;      // ...and how many such tokens
    int firstLine = -1;         // the first output line with a token from it; -1 if none
    // The statement it is printed in: its own when explicit (or a write), else the op its implied
    // value is finally used in. Two reads in one statement are in an order C leaves open.
    uint32_t statement = 0;
    uint32_t id = 0;                // this access's op, as numbered in 'through'
    std::vector<uint32_t> through;  // the ops its implied value passes through to that statement
    // Printed in the right operand of && or ||, so shown as made only when the left operand does
    // not decide, or printed in a block other than the one it happens in, so shown as made only
    // on the paths reaching that block -- though the instruction always makes it.
    bool conditionalInPrint = false;
    // Where it happens and where it is printed, as block index and order within the block.
    int block = -1, statementBlock = -1;
    uint32_t order = 0, statementOrder = 0;
    // Another access of the same kind from the same instruction can run after this one on one path
    // (the engine copies a block into each path that reaches it; copies on separate paths are one
    // access each, copies on one path are an access made twice).
    bool repeatedOnPath = false;
    // Where the access was when the flow was first followed, before any action: the block index in
    // DecompileResult::flowSuccessors and its position in that block. -1 when the op is not one of
    // the flow's (made later by a rule, or a register access with several candidates in its
    // instruction). The order the code makes two accesses in is read from here: the final blocks
    // are joined, split and copied, so neither op ids nor addresses say.
    int origBlock = -1;
    uint32_t origOrder = 0;
    bool inSwitchBlock = false;   // the final block ends in the jump of a switch
};

// How one of the flow's LOAD or STORE ops ended, when it did not stay one: the engine's record
// (Funcdata::getAccessFates), by instruction.
struct AccessFate
{
    // FoldedOrdinary: made a direct read or write of ordinary memory, a variable the analysis may
    // forward or drop. Dead: removed as unused (or by a transformation that is none of the others).
    // Unreachable: removed with the unreachable block it was in. SwitchLookup: the read of a
    // recovered switch's table; the switch stands for it. Patch 0068, for an access folded into the
    // function's own frame (recorded FoldedOrdinary first, 'frame' set): FrameUnread, a write went
    // with its value read by nothing but the engine's markers -- it is lost if the code reads one of
    // its bytes later (A3-124); FrameRemoved, the access went with an unreachable block. MalformedPiece: no access, a
    // PIECE or SUBPIECE whose sizes do not add up (its instruction in 'insn'): the values through it are no code's (A3-127).
    enum Kind : uint8_t { FoldedOrdinary, Dead, Unreachable, SwitchLookup, FrameUnread, FrameRemoved, MalformedPiece };
    uint32_t insn = 0;      // the instruction, canonical
    bool write = false;
    uint8_t size = 0;
    Kind fate = Dead;
    uint32_t address = 0;   // the constant address, as the code uses it, when the pointer was one; else 0
    bool constantAddress = false;   // 'address' is a bus address (the engine's default code space)
    // The fold went into the function's own stack frame (a push, a pop, a local): 'address' is then
    // the frame offset, not a bus address -- never a register, never memory another processor
    // reaches -- and constantAddress is false. It accounts for no bus access (A5-5: the offset -4 of
    // a pop was read as a register, and a pop was taken for the read in an rte's delay slot).
    bool frame = false;
    // Where the access was when the flow was first followed (as MemoryAccess::origBlock and
    // origOrder): -1 when the op is not one of the flow's LOADs or STOREs.
    int origBlock = -1;
    uint32_t origOrder = 0;
};

// A call in the final p-code: an access's order relative to it matters as much as to another access.
struct CallSite
{
    uint32_t insn = 0;
    int block = -1;
    uint32_t order = 0;
    uint32_t id = 0;
    int origBlock = -1;
    uint32_t origOrder = 0;
};

// How one of the engine's warning comments is reported: kept or not (warnings from jump-table
// recovery's scratch copy are dropped, unless they say cases were dropped), its text, and whether
// it means a jump table lost cases or the analysis hit its restart limit or its bound on the
// passes of one action (any of them withholds the signature). Exposed for tests: a failed
// second-stage table recovery has no SH-2 fixture that reaches it, since both stages analyse the
// same scratch copy.
struct EngineWarning
{
    bool keep = false;
    bool casesMissing = false;
    bool restartLimit = false;
    bool repeatLimit = false;   // patch 0055: an action of the engine stopped repeating with changes still pending (A2-59)
    std::string text;   // without the "WARNING: " prefix
};
EngineWarning ClassifyEngineWarning(const std::string& comment);

enum class DecompileStatus : uint8_t
{
    Ok,
    NoCode,        // the entry is not in captured memory, or not memory at all
    Cancelled,     // the cancel flag was honoured
    Failed,        // the engine raised an error; see error
    Unavailable,   // the spec bundle could not be materialised; see error
    TooLarge,      // DecompilerRunner: the wall-clock budget expired
    Disabled,      // DecompilerRunner: a cancel was not acknowledged; decompilation is off
};

struct DecompileResult
{
    DecompileStatus status = DecompileStatus::Failed;
    std::string error;

    SnapshotKey key;
    uint64_t generation = 0;       // DecompilerRunner's request stamp
    int slot = 0;                  // DecompilerRunner's requester id
    uint32_t requestedEntry = 0;
    uint32_t entry = 0;            // canonical
    std::string name;

    std::vector<Line> lines;
    std::vector<CallSite> calls;                      // the calls, for the ledger's order checks
    std::vector<std::vector<int>> flowSuccessors;     // the flow's basic blocks before any action: each one's successors
    // The calls (their place in the flow as first followed, as MemoryAccess::origBlock and origOrder) the engine guarded a local
    // of the frame at once a frame address escaped, with the local (its offset in the stack space, as AccessFate::address, and
    // size): the call's effect on it is unknown and it may reach the frame, so it may read it (patch 0068). For the frame ledger.
    struct FrameCall
    {
        int origBlock = -1;
        uint32_t origOrder = 0;
        uint32_t offset = 0;
        uint32_t size = 0;
    };
    std::vector<FrameCall> frameCalls;
    // How each memory access the flow made that is no longer one in the p-code ended (the engine
    // records it, patch 0017). With the printed accesses above, they account for every LOAD and
    // STORE of the flow: an access of the all-volatile reference run (DecompileRequest::
    // allMemoryVolatile) is conserved when it is printed here, or ended folded into a variable of
    // ordinary memory, or as the read of a switch's table, or was removed with unreachable code --
    // which the removal caution says. One that ended dead, or has no record, was dropped.
    std::vector<AccessFate> accessFates;
    std::vector<uint32_t> callees;        // constant call targets, canonical, sorted
    // ...and the addresses they are called at (a mirror when the call goes through one), sorted:
    // what DecompileRequest::prototypes is keyed by, since a callee's signature can depend on the
    // mirror it runs in.
    std::vector<uint32_t> calledAt;
    // This function's own signature, for callers to use (DecompileRequest::prototypes). Not known
    // unless the analysis that recovered it was complete: the output is not truncated, unsupported
    // or unimplemented, there is no bad instruction, every jump table was recovered whole and the
    // analysis settled within its restarts, every call resolves to a callee whose
    // signature was lent (none to itself), every incoming argument read is in the signature, and
    // every return goes to the caller's PR. A caller decompiled first is lent nothing; it can be
    // once its callees' signatures are known.
    Prototype prototype;
    std::string prototypeWithheld;        // why it is not known; empty when it is
    std::vector<uint32_t> literalPools;   // literal-pool words marked read-only, sorted, unique
    uint32_t literalLoads = 0;            // PC-relative loads through them (a word may be read twice)

    // The instructions flow reached, as [lo, hi) canonical ranges, sorted and merged. A function is
    // not one range from its entry: a shared epilogue can sit below the entry, a tail far above it.
    // It is what the last flow translated (patch 0021), whether or not the actions kept the p-code of
    // an instruction: an entry of another function whose first instruction was dead is in it, and
    // every known entry in it has a FlowWarning::IntoFunction, those reached by a second jump too.
    std::vector<std::pair<uint32_t, uint32_t>> body;
    bool InBody(uint32_t canonical) const;
    uint32_t bodyBytes = 0;        // the span from the lowest instruction to the end of the highest
    bool truncated = false;        // flow reached memory that is not captured / not memory
    uint32_t truncatedAt = 0;      // the lowest such address (every one is in flowWarnings)
    std::string truncatedWhy;
    int truncatedCount = 0;        // how many places flow was cut off
    // Words in captured memory flow tried to run that do not decode as instructions: embedded data,
    // or a wrong entry. Flow stops at each.
    std::vector<uint32_t> badDataAt;
    std::vector<FlowWarning> flowWarnings;   // every one is in warnings too
    std::vector<HardwareNote> hardwareNotes; // the cautions are in warnings too
    std::vector<MemoryAccess> accesses;      // in p-code order
    // Ordinary memory the host made volatile for this function (A5-50, A5-52, A5-53): a word it reaches under two addresses of the bus
    // (the cached and cache-through partitions, bits 28-27, the repeats of work RAM and VDP memory) is two memories to the engine's data
    // flow, which would forward a store past a write to the other name; and a byte store to colour RAM writes the whole entry. Each range
    // is a constant address the function accesses, outward to whole longwords. Accesses in them are printed as they are, in the ledger,
    // as the user's volatile ranges are.
    std::vector<std::pair<uint32_t, uint32_t>> aliasedMemory;
    bool unsupported = false;      // A3: a PC-relative load or mova in a delay slot
    uint32_t unsupportedAt = 0;    // the slot instruction's address
    bool unimplemented = false;    // the spec has no semantics for an instruction
    DecompilePhase cancelledIn = DecompilePhase::Idle;   // when Cancelled: the phase it stopped in
    std::vector<std::string> warnings;
    double elapsedMs = 0.0;

    std::string Text() const;      // the lines joined with '\n'
};

struct PassSeed;   // what a printed pass followed its flow with: the reference run follows the same

class Sh2Decompiler
{
public:
    // 'configDir' is where SpecBundle materialises the language files (Settings::EnsureConfigDir).
    explicit Sh2Decompiler(std::string configDir);

    // Decompile per the request. 'cancel' may be null; when set during the run the result is
    // Cancelled at the engine's next check-point.
    DecompileResult Decompile(const std::shared_ptr<const MemorySnapshot>& snapshot,
                              const DecompileRequest& request, std::atomic<bool>* cancel = nullptr);

    // SLEIGH disassembly of 'count' instructions from 'address', one "ADDR  mnemonic operands"
    // line each, in the spec's own spelling (tests and diagnostics: the listing is what the
    // fixture test compares with the Capstone-verified one). Stops at the first undecodable one.
    // Never throws. When memory runs out (or anything else the engine does not report itself goes wrong) the
    // listing is cut short and the engine is marked damaged; that is reported in two ways: '*stopped' (when
    // given) is set, which needs no allocation and so holds however many allocations fail from then on, and the
    // listing ends with a "<stopped: ..." line whenever the room for it could be made first (it is, but for a
    // failure in the first two allocations, or a 'count' past 4096).
    std::vector<std::string> Disassemble(const std::shared_ptr<const MemorySnapshot>& snapshot,
                                         uint32_t address, int count, bool* stopped = nullptr);

    // Called on the engine thread as each phase begins (and with Teardown as each pass's
    // Architecture is destroyed). Tests use it to prove which phase a cancel landed in; it may
    // block, which holds the engine at the start of that phase.
    void SetPhaseObserver(std::function<void(DecompilePhase)> observer) { mObserver = std::move(observer); }

    // Tests only: the engine treats these callees as never returning. Nothing in SE marks one (the
    // spec's only user op has no output, and lent signatures carry no such flag), so this is how
    // that path -- flow ending at the call, ReturnCheck ending the path there -- is exercised.
    // Process-wide; set it before Decompile and clear it after.
    static void SetNoReturnForTests(const std::set<uint32_t>& entries);
    // Tests only: whether a lent signature also lends the effects its analysis proved (the registers
    // it keeps, so a caller reads them as they were instead of extraout_rN). On unless cleared: the
    // lending measurements run both ways. Process-wide.
    static void SetLendEffectsForTests(bool lend);
    // Tests only: the most passes one repeating action of the engine makes before it stops with "Exceeded maximum repeats of"
    // and the signature is withheld (patch 0055; 0 restores the engine's own bound). Process-wide.
    static void SetActionRepeatLimitForTests(int limit);
    // Tests only: called as each phase begins, in every pass, including the reference run's (which
    // the observer is not told of); and just before the reference run starts. Process-wide.
    static void SetPhaseHookForTests(std::function<void(DecompilePhase)> hook);
    static void SetBeforeReferenceHookForTests(std::function<void()> hook);
    // Tests only: called at each of the host's own cancel check-points (Flow to Actions, Actions to Print, and the end of the first
    // pass), immediately BEFORE the check, so that a test can raise the cancel flag exactly there and require the check to honour
    // it in the phase that is ending (A6-41). The engine's own check-points are not told of. Process-wide.
    static void SetBoundaryHookForTests(std::function<void()> hook);
    // Tests only: called where the host declares the engine finished with a request -- the last pass's Architecture is gone,
    // the work phase back to Idle -- so that a failure after it is host-only (A6-45) and the fault matrix can tell it from one
    // inside the engine. It runs again after the reference run. Process-wide.
    static void SetEngineDoneHookForTests(std::function<void()> hook);

private:
    void NoteRemovedAccesses(const std::shared_ptr<const MemorySnapshot>& snapshot, const DecompileRequest& request,
                             std::atomic<bool>* cancel, DecompileResult& r, const PassSeed& seed);
    // 'seedIn' makes the printed pass follow its flow with another pass's literals and callees (one
    // pass, the same flow); 'seedOut' receives the printed pass's own.
    void DecompileInto(const std::shared_ptr<const MemorySnapshot>& snapshot, const DecompileRequest& request,
                       std::atomic<bool>* cancel, DecompileResult& r, const PassSeed* seedIn = nullptr,
                       PassSeed* seedOut = nullptr);
    void Enter(DecompilePhase phase);

    std::string mConfigDir;
    std::function<void(DecompilePhase)> mObserver;
    DecompilePhase mWorkPhase = DecompilePhase::Idle;   // the last phase that does work; Idle once the last pass's Architecture is gone
    // The reference run's phases are not the request's progress: the observer is not told of them.
    bool mQuietPhases = false;
};

}  // namespace decomp
}  // namespace sfe
