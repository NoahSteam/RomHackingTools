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
    bool operator==(const CallEffects& o) const
    {
        return preserved == o.preserved && storesThrough == o.storesThrough && outOrigins == o.outOrigins &&
               leaks == o.leaks && frameValues == o.frameValues;
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

// Where the engine is within one decompilation. Two passes repeat Init..Actions; only the last
// prints. The cancel check-points are in Flow and Actions; Init, Print and Teardown have none
// (PLAN.md A4), so a cancel requested there takes effect at the next check-point, if any.
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
};

// How one of the engine's warning comments is reported: kept or not (warnings from jump-table
// recovery's scratch copy are dropped, unless they say cases were dropped), its text, and whether
// it means a jump table lost cases or the analysis hit its restart limit (either withholds the
// signature). Exposed for tests: a failed second-stage table recovery has no SH-2 fixture that
// reaches it, since both stages analyse the same scratch copy.
struct EngineWarning
{
    bool keep = false;
    bool casesMissing = false;
    bool restartLimit = false;
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
    bool unsupported = false;      // A3: a PC-relative load or mova in a delay slot
    uint32_t unsupportedAt = 0;    // the slot instruction's address
    bool unimplemented = false;    // the spec has no semantics for an instruction
    DecompilePhase cancelledIn = DecompilePhase::Idle;   // when Cancelled: the phase it stopped in
    std::vector<std::string> warnings;
    double elapsedMs = 0.0;

    std::string Text() const;      // the lines joined with '\n'
};

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
    std::vector<std::string> Disassemble(const std::shared_ptr<const MemorySnapshot>& snapshot,
                                         uint32_t address, int count);

    // Called on the engine thread as each phase begins (and with Teardown as each pass's
    // Architecture is destroyed). Tests use it to prove which phase a cancel landed in; it may
    // block, which holds the engine at the start of that phase.
    void SetPhaseObserver(std::function<void(DecompilePhase)> observer) { mObserver = std::move(observer); }

    // Tests only: the engine treats these callees as never returning. Nothing in SE marks one (the
    // spec's only user op has no output, and lent signatures carry no such flag), so this is how
    // that path -- flow ending at the call, ReturnCheck ending the path there -- is exercised.
    // Process-wide; set it before Decompile and clear it after.
    static void SetNoReturnForTests(const std::set<uint32_t>& entries);

private:
    void DecompileInto(const std::shared_ptr<const MemorySnapshot>& snapshot, const DecompileRequest& request,
                       std::atomic<bool>* cancel, DecompileResult& r);
    void Enter(DecompilePhase phase);

    std::string mConfigDir;
    std::function<void(DecompilePhase)> mObserver;
    DecompilePhase mWorkPhase = DecompilePhase::Idle;   // the last phase that does work
};

}  // namespace decomp
}  // namespace sfe
