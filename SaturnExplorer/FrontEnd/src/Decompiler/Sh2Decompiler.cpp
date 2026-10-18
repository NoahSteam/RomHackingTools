#include "Decompiler/Sh2Decompiler.h"

#include "Decompiler/SaturnArchitecture.h"   // first among the engine headers (Win32 LoadImage)
#include "Decompiler/SaturnHardwareMap.h"

#include "Debug/Sh2Disasm.h"

#include "funcdata.hh"
#include "jumptable.hh"
#include "printlanguage.hh"
#include "marshal.hh"
#include "prettyprint.hh"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <tuple>

namespace sfe
{
namespace decomp
{
namespace
{

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

std::string Hex8(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

// Tests only: the bound on the passes of one repeating action of the engine (patch 0055), 0 for the engine's own.
int gActionRepeatLimitForTests = 0;

std::unique_ptr<SaturnArchitecture> BuildArchitecture(const std::shared_ptr<const MemorySnapshot>& snapshot,
                                                      uint32_t maxInstructions, std::atomic<bool>* cancel,
                                                      std::ostream& errors)
{
    std::unique_ptr<SaturnArchitecture> arch(new SaturnArchitecture(snapshot, &errors));
    arch->Initialise(maxInstructions, cancel);
    if (gActionRepeatLimitForTests > 0) arch->maxActionRepeats = gActionRepeatLimitForTests;
    return arch;
}

// Set once an allocation failure (or any non-engine exception) has escaped from inside the
// engine. Ghidra is not exception-safe there: the process-global SLEIGH translator can be left
// half-reset, and the next Architecture built on it crashes. It cannot be rebuilt either (the
// translator map is private to SleighArchitecture), so from then on the decompiler reports
// itself unavailable for the life of the process instead of touching the engine again.
std::atomic<bool> gEngineDamaged(false);

// Canonical call target -> the addresses the code actually calls it through (itself, or mirrors).
using CalleeAliases = std::map<uint32_t, std::set<uint32_t>>;
}  // namespace

// The literals marked read-only and the callees registered before a printed pass followed its flow
// (the first pass's findings): a pass given them follows the same flow in one pass.
struct PassSeed
{
    std::vector<std::pair<uint32_t, int>> literals;
    CalleeAliases callees;
};

namespace
{

// Fills the error text of a failed result. Never throws: the failure being reported may be an
// allocation failure, and the result must still leave the boundary with its status set. The
// const char* overload matters -- converting e.what() to a std::string argument would allocate
// before any guard could catch it.
void SetError(DecompileResult& r, const char* text) noexcept
{
    try
    {
        r.error = text;
    }
    catch (...)
    {
        r.error.clear();
    }
}

void SetError(DecompileResult& r, const std::string& text) noexcept
{
    try
    {
        r.error = text;
    }
    catch (...)
    {
        r.error.clear();
    }
}

const char* const kEngineDamaged =
    "the decompiler ran out of memory earlier and its engine may be damaged; restart Saturn Explorer to decompile again";

// A function in the user's BIOS image (DESIGN.md 6.1) is named for where it lives, so a call into
// the BIOS reads as one at a glance: bios_0x00000600, beside the engine's func_0x06005000.
bool InBios(const MemorySnapshot& snap, uint32_t canonical)
{
    const MemoryRegion* region = snap.RegionAt(canonical);
    return region != nullptr && region->base == MemorySnapshot::kBiosBase && std::string(region->name) == "BIOS";
}

std::string BiosFunctionName(uint32_t canonical)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "bios_0x%08x", canonical);
    return buf;
}

TypeShape ShapeOf(const ghidra::Datatype* t)
{
    TypeShape shape;
    if (t == nullptr) return shape;
    shape.size = static_cast<uint8_t>(std::min<ghidra::int4>(t->getSize(), 255));
    switch (t->getMetatype())
    {
    case ghidra::TYPE_VOID: shape.kind = TypeKind::Void; shape.size = 0; break;
    case ghidra::TYPE_INT: shape.kind = TypeKind::Int; break;
    case ghidra::TYPE_UINT: shape.kind = TypeKind::Uint; break;
    case ghidra::TYPE_BOOL: shape.kind = TypeKind::Bool; break;
    case ghidra::TYPE_PTR:
    {
        shape.kind = TypeKind::Pointer;
        const ghidra::Datatype* to = static_cast<const ghidra::TypePointer*>(t)->getPtrTo();
        if (to != nullptr)
        {
            switch (to->getMetatype())
            {
            case ghidra::TYPE_INT: shape.pointee = TypeKind::Int; break;
            case ghidra::TYPE_UINT: shape.pointee = TypeKind::Uint; break;
            case ghidra::TYPE_BOOL: shape.pointee = TypeKind::Bool; break;
            default: shape.pointee = TypeKind::Unknown; break;
            }
            const ghidra::int4 n = to->getSize();
            shape.pointeeSize = static_cast<uint8_t>(n == 1 || n == 2 || n == 4 || n == 8 ? n : 1);
        }
        break;
    }
    default: shape.kind = TypeKind::Unknown; break;   // floats, structures: an unknown of that size
    }
    return shape;
}

ghidra::Datatype* TypeOf(ghidra::TypeFactory& types, const TypeShape& shape)
{
    auto base = [&](TypeKind kind, int size) -> ghidra::Datatype* {
        if (size != 1 && size != 2 && size != 4 && size != 8) size = 4;
        switch (kind)
        {
        case TypeKind::Int: return types.getBase(size, ghidra::TYPE_INT);
        case TypeKind::Uint: return types.getBase(size, ghidra::TYPE_UINT);
        case TypeKind::Bool: return types.getBase(1, ghidra::TYPE_BOOL);
        default: return types.getBase(size, ghidra::TYPE_UNKNOWN);
        }
    };
    switch (shape.kind)
    {
    case TypeKind::Void: return types.getTypeVoid();
    case TypeKind::Pointer: return types.getTypePointer(4, base(shape.pointee, shape.pointeeSize), 1);
    default: return base(shape.kind, shape.size);
    }
}

// Whether a lent prototype also lends the effects its analysis proved (the registers it keeps).
// Tests only: off, a lent callee leaves r0-r7, MACH, MACL and SR killed and r8-r15, GBR and VBR kept, as
// the default model does.
bool gLendEffectsForTests = true;

// The registers the default model's killedbycall list (patch 0026) names that a callee can be proved
// to keep, as CallEffects::preserved bits: r2-r7, SR, MACH, MACL. (r8-r14, GBR, VBR and r15 are
// unaffected for every callee already, and are killed the other way round: kKeptByCall, below. PR
// is the return address.) Not r0 and r1: they are the
// return registers, and what the engine's return recovery takes for a value depends on what a call
// leaves in them -- a caller whose r0 and r1 pass through callees that keep them has a "real" value
// in both at its own return, so a function that only leaves temporaries there was given a return
// (2 signatures lost in the corpus, none gained; the generator's lending is the same without them).
struct KilledRegister
{
    int bit;
    const char* name;
};
constexpr KilledRegister kKilledByCall[] = { { 2, "r2" }, { 3, "r3" }, { 4, "r4" }, { 5, "r5" }, { 6, "r6" }, { 7, "r7" },
                                             { 16, "sr" }, { 19, "mach" }, { 20, "macl" } };
// The registers the model keeps across every call (the cspec's <unaffected>, bar r15, which is the stack
// pointer and is settled in WithholdReason).
constexpr KilledRegister kKeptByCall[] = { { 8, "r8" }, { 9, "r9" }, { 10, "r10" }, { 11, "r11" }, { 12, "r12" }, { 13, "r13" },
                                           { 14, "r14" }, { 17, "gbr" }, { 18, "vbr" } };

// Lend 'proto''s proved effects with its signature: the registers ReturnCheck found equal to their
// entry values at every return are unaffected by a call to this function, where the model would
// have them killed and the caller would read extraout_rN and lose the parameter it passed through.
// Everything not proved keeps the model's effect.
void LendEffects(ghidra::Architecture& arch, ghidra::FuncProto& fp, const Prototype& proto)
{
    if (!gLendEffectsForTests || !proto.known) return;
    std::vector<ghidra::EffectRecord> effects;
    for (const KilledRegister& k : kKilledByCall)
        if ((proto.effects.preserved >> k.bit) & 1u)
            effects.emplace_back(arch.translate->getRegister(k.name), ghidra::EffectRecord::unaffected);
    // ...and the other way: a register the model keeps for every callee (cspec <unaffected>) that this
    // one was proved to change on some path is killed, as the model's kill of r0-r7 is. Without it the
    // caller reads the value from before the call and is lent a signature on it (A3-3).
    for (const KilledRegister& k : kKeptByCall)
        if (!((proto.effects.preserved >> k.bit) & 1u))
            effects.emplace_back(arch.translate->getRegister(k.name), ghidra::EffectRecord::killedbycall);
    if (!effects.empty()) fp.setEffects(effects);
    // r15: the model keeps it (extrapop 0). A callee proved to leave it a constant distance from where it
    // found it -- a push it does not undo, a pop of its caller's frame -- has that as its extrapop, which
    // the engine applies to the stack pointer after the call. (A callee that cannot say is not lent at
    // all: WithholdReason.)
    if (!((proto.effects.preserved >> 15) & 1u) && proto.effects.spDelta != CallEffects::kSpUnknown)
        fp.setExtraPop(proto.effects.spDelta);
}

// Lock 'fd' to 'proto': the caller's call site then copies it instead of guessing.
void ApplyPrototype(ghidra::Architecture& arch, ghidra::Funcdata& fd, const Prototype& proto)
{
    ghidra::PrototypePieces pieces;
    pieces.model = arch.defaultfp;
    pieces.name = fd.getName();
    pieces.outtype = TypeOf(*arch.types, proto.ret);
    for (size_t i = 0; i < proto.params.size(); ++i)
    {
        pieces.intypes.push_back(TypeOf(*arch.types, proto.params[i]));
        pieces.innames.push_back("param_" + std::to_string(i + 1));
    }
    pieces.firstVarArgSlot = -1;
    fd.getFuncProto().setPieces(pieces);
    LendEffects(arch, fd.getFuncProto(), proto);
}

Prototype PrototypeOf(const ghidra::Funcdata& fd)
{
    Prototype p;
    const ghidra::FuncProto& fp = fd.getFuncProto();
    if (fp.isDotdotdot()) return p;
    p.known = true;
    p.ret = ShapeOf(fp.getOutputType());
    for (ghidra::int4 i = 0; i < fp.numParams(); ++i) p.params.push_back(ShapeOf(fp.getParam(i)->getType()));
    return p;
}

// Whether every return in the function goes to the caller's return address -- PR as it was on
// entry, kept in a register or saved on the stack and reloaded -- and what a call to the function
// can do to its caller (CallEffects). The engine cannot say: its actions replace a RETURN's address
// with 0 once it is dead. So each instruction's p-code is interpreted here.
//
// A value is exact (a register's entry value, a stack address as an offset from r15 on entry, a
// constant) or else only the set of things it can come from (origin::*). Losing precision keeps
// the origins, so an address computed from a stack address still may point into the stack. A
// store that may hit a stack slot leaves it holding what it held or the stored value; a load that
// may read one gets any of them. Slots below r15 hold anything (pushes, interrupts). The state is
// 'escaped' once a stack address is let out to untracked memory, after which anything loaded from
// memory may be one too; a store that can only land in the stack lets nothing out. An absolute address is taken never to be in the stack, as the engine's own
// stack analysis takes it.
//
// A call applies the callee's verified CallEffects: the registers it preserves keep their values,
// the others take what it can return, its stores through what it was handed hit what they may, and
// what it lets out escapes. All of it joins this function's own summary. A callee whose effects
// are unknown is taken to do anything.
// Each indirect jump's recovered destinations, by the jump's address: as the first flow recovered
// them (AddJumpTables, called twice), as the last flow did (AddFlowTables), and as the function
// holds them at the end. All of them, because the actions can remove a table they have resolved --
// one destination becomes a plain goto (RuleSwitchSingle) -- while the jump itself is still in the
// instruction it came from.
using JumpDests = std::map<uint32_t, std::set<uint32_t>>;

void AddJumpTables(ghidra::Funcdata& fd, JumpDests& dests)
{
    for (ghidra::int4 t = 0; t < fd.numJumpTables(); ++t)
    {
        const ghidra::JumpTable* table = fd.getJumpTable(t);
        std::set<uint32_t>& d = dests[static_cast<uint32_t>(table->getOpAddress().getOffset())];
        for (ghidra::int4 j = 0; j < table->numEntries(); ++j)
            d.insert(static_cast<uint32_t>(table->getAddressByIndex(j).getOffset()));
    }
}

// ...and the ones the last flow recovered (patch 0021). A restart in the actions follows the flow
// again, and the tables it recovers reach further than the first flow's; one the actions resolve to
// a goto is gone by the end, and is in neither of the other two.
void AddFlowTables(const ghidra::Funcdata& fd, JumpDests& dests)
{
    for (const ghidra::FlowTable& t : fd.getFlowTables())
    {
        std::set<uint32_t>& d = dests[static_cast<uint32_t>(t.op.getOffset())];
        for (const ghidra::Address& a : t.dests) d.insert(static_cast<uint32_t>(a.getOffset()));
    }
}

class ReturnCheck
{
public:
    ReturnCheck(ghidra::Funcdata& fd, uint32_t exec, const std::map<uint32_t, Prototype>& lent, const JumpDests& tables)
        : mFd(fd), mExec(exec), mLent(lent), mTables(tables)
    {
        const ghidra::Architecture* arch = fd.getArch();
        mRam = arch->getDefaultCodeSpace();
        const char* names[kTracked] = { "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
                                        "r11", "r12", "r13", "r14", "r15", "sr", "gbr", "vbr", "mach", "macl", "pr" };
        for (int i = 0; i < kTracked; ++i) mReg[i] = arch->translate->getRegister(names[i]);
        // Where each call goes, by the instruction that makes it (the engine's call specs know a
        // jsr @rN through a literal; the p-code alone does not).
        for (ghidra::int4 i = 0; i < fd.numCalls(); ++i)
        {
            const ghidra::FuncCallSpecs* cs = fd.getCallSpecs(i);
            const ghidra::Address at = cs->getEntryAddress();
            if (cs->getOp() == nullptr) continue;
            const uint32_t site = static_cast<uint32_t>(cs->getOp()->getAddr().getOffset());
            if (!at.isInvalid() && at.getSpace() == mRam) mCallAt[site] = static_cast<uint32_t>(at.getOffset());
            // Flow stopped after it, so what follows is not this function's (often data).
            if (cs->isNoReturn()) mNoReturn.insert(site);
        }
    }

    // Explore every path whatever happens on it: an instruction that does not decode, a return not to
    // the caller or an unresolved jump ends that path instead of the run. For FrameEscape().
    void SetTolerant() { mTolerant = true; }
    // For the probe, which runs on the first flow, before the actions fold a literal-pool load into the target of `jsr @rN`: the words the
    // raw scan marked as literals, and the memory to read them from. A call through a register that holds one is a call to its value
    // (A3-36); without it the call is a CALLIND that mCallAt knows nothing of and its callee is taken to do anything.
    void SetLiterals(const std::set<uint32_t>& words, const MemorySnapshot& snapshot)
    {
        mLiterals = &words;
        mLiteralMemory = &snapshot;
    }
    // A cancel flag the walk checks periodically (A3-74): a long walk of an adversarial callee is interruptible.
    void SetCancel(std::atomic<bool>* cancel) { mCancel = cancel; }

    // The calls (by the instruction that makes them) a frame address escapes at in a register the call is shown without, and which (bit 2: r2, bit 7: r7).
    const std::map<uint32_t, uint32_t>& EscapeSites() const { return mEscapeSites; }
    // After Run(): whether r2 held the address of a local at a call whose signature is not lent (A3-4),
    // and the lowest such offset from r15 on entry as the engine's stack offset (unsigned, 32-bit).
    bool FrameEscape(uint32_t& offset) const
    {
        if (!mEscaped) return false;
        // An address made from a base that was given up (a loop that moves it, an index that may be negative)
        // points anywhere in the frame: from its lowest slot, the lowest offset r15 or a stack address held.
        offset = static_cast<uint32_t>(mEscapedLow ? std::min(mEscapeLow, mLowestSp) : mEscapeLow);
        return true;
    }

    // After Run(): what the value r0 holds at the returns depends on, through this function's own instructions (not through a call), as inputs
    // (kInputs): the return rule of a function the engine made `void` (WithholdReason). The value is the one value of every return, their r0s
    // joined as their paths would be at a common exit, and two values are one only when one op made them (Value::def).
    //   'data': the inputs it was computed from, by data, the address of a load included (`mov.b @(r0,r4),r0`).
    //   'chosen': when it is not one value (r0 differs by path), the inputs that decided the path: the conditions of the branches and the jump
    //   tables' indices on the way to the returns (A3-55 to A3-58, A3-62), r0's entry value (`tst r0,r0`), the entry T flag and the stack
    //   arguments included.
    //   'joinsR0': of 'data', what was in a join with r0's entry value itself (A3-20's text, `if (b) return a;`).
    // The exceptions, each said where it is made: r0's entry value where no op read it (ReturnInput: a path that leaves r0 alone); a value
    // nothing is known of (ReturnInput, ChooserInput); and SR's bits other than T, with the T in them, in a value that is neither SR itself nor
    // joined (the interrupt-mask helper's `stc sr,r0 ; or #0xF0,r0 ; ldc r0,sr`, which compiled code leaves in r0 as scratch: 06010054).
    struct ReturnInputs
    {
        uint32_t data = 0, chosen = 0, joinsR0 = 0;
    };
    ReturnInputs Inputs() const
    {
        ReturnInputs in;
        if (!mExitSeen) return in;
        const bool one = !mExit.Joined();
        in.data = mReturnData;
        // The interrupt-mask idiom (06010054: `stc sr,r0 ; or #0xF0,r0 ; ldc r0,sr`): SR's bits, T among them, are excused only when r0 at every
        // return is the very value the function wrote back to SR with `ldc` (mSrWritten, by the op that made it). The engine never removes that
        // Write_SR, so every read the value was computed from stays in the text, and how SR's entry value is used there is EntryStateUse's to
        // settle. Any other value computed from SR -- `and #1,r0`, `add #1,r0`, the idiom with no write-back -- keeps T and SR as inputs (A3-69).
        if ((in.data & (1u << kSr)) && one && mExit.kind == Value::Fuzzy && mExit.def != 0 && mSrWritten.count(mExit.def) != 0)
            in.data &= ~((1u << kSr) | kDepT);
        in.joinsR0 = mReturnJoinsR0 & in.data;
        in.chosen = one ? 0u : mReturnCtl;
        return in;
    }
    // After Run(): whether r0 holds, at some return, a value this function put there and held across a call to a callee that
    // keeps r0 (A3-13): the engine kills r0 at every call, so it does not see the value at the return.
    bool CarriedValueInReturn() const { return mCarriedInReturn; }
    // After Run(): the origins of what the function writes at an exact offset at or above its entry r15, a word of its caller's frame
    // (A3-21); a constant counts as origin::kFrame, which no signature holds. 0 when it writes none.
    uint32_t CallerFrameStores() const { return mCallerFrameStores; }

    // Empty when every return reached goes to the caller, else why not. 'effects' is then this
    // function's own summary.
    std::string Run(CallEffects& effects)
    {
        State start;
        start.reached = true;
        for (int i = 0; i < kTracked; ++i) Set(start, mReg[i], i == kR15 ? Value::Stack(0) : Value::Entry(i));
        mPreserved = origin::kRegisters;
        mSpSeen = false;
        mSpKnown = true;
        mSpDelta = 0;
        mCarriedInReturn = false;
        mExit = Value();
        mExitSeen = false;
        mReturnData = mReturnCtl = mReturnJoinsR0 = 0;
        mDef = 0;
        mCallerFrameStores = 0;
        mOwnFrameWords = 0;
        mFrameWords = 0;
        mStoresThrough = 0;
        mStoresBelow = 0;
        mSrWritten.clear();
        mOut = 0;
        mLeaks = 0;
        mFrameValues = 0;
        mLowestSp = 0;
        std::map<uint32_t, State> in;
        std::map<uint32_t, int> changes;   // how often the state at an address grew
        std::set<uint32_t> work;
        in[mExec] = start;
        work.insert(mExec);
        size_t steps = 0;
        while (!work.empty())
        {
            const uint32_t a = *work.begin();
            work.erase(work.begin());
            if (++steps > mMaxSteps) return "a return not proven to go to its caller (too many paths)";
            // A cancel requested anywhere reaches the probe's walk too (A3-74: the outer phases check it, this did not).
            if (mCancel != nullptr && (steps & 0x3FFu) == 0 && mCancel->load()) throw ghidra::CancelError("cancelled");
            // The walk of an unlent callee (CalleeFrameWrites) is quadratic in the callee's stores: each state copies its slots map, and a
            // callee of thousands of pushes cost seconds and gigabytes (A3-74). Bound it by the slots a state holds, as kNestedMaxSteps bounds
            // the instructions; past the bound, fail closed -- the callee may write any word of its caller's frame.
            if (mNested && in[a].slots.size() > kNestedMaxSlots)
            {
                mOwnFrameWords |= CallEffects::kFrameAny;
                mCallerFrameStores |= origin::kFrame;
                break;
            }
            std::vector<std::pair<uint32_t, State>> out;
            const std::string why = Step(a, in[a], out);
            if (!why.empty()) return why;
            for (auto& o : out)
            {
                auto f = in.find(o.first);
                if (f == in.end()) in.emplace(o.first, std::move(o.second));
                else if (!Join(f->second, o.second)) continue;
                // A loop that moves a stack address keeps lowering its base: each pass is a new, lower one, and
                // the walk would run to its step limit (A3-16). After a few, the bases at that address are
                // given up for "the frame's lowest slot".
                else if (++changes[o.first] > kWidenAfter) Widen(f->second);
                work.insert(o.first);
            }
        }
        effects.preserved = mPreserved;
        effects.storesThrough = mStoresThrough;
        effects.storesBelow = mStoresBelow;
        effects.outOrigins = mOut;
        effects.leaks = mLeaks;
        effects.frameValues = mFrameValues;
        effects.frameWords = mFrameWords;
        // |delta| below the engine's own 'unknown' extrapop (0x8000).
        effects.spDelta = mSpKnown && mSpDelta > -0x8000 && mSpDelta < 0x8000 ? static_cast<int32_t>(mSpDelta) : CallEffects::kSpUnknown;
        return std::string();
    }

private:
    static constexpr size_t kMaxSteps = 200000;
    static constexpr size_t kNestedMaxSteps = 20000;   // a callee walked for CalleeFrameWrites: one it cannot follow in that writes nothing
    static constexpr size_t kNestedMaxSlots = 256;     // ...and the stack words it stores to: past this the walk is bounded (A3-74)
    static constexpr int kWidenAfter = 6;
    static constexpr int kTracked = 22;   // the registers origin:: names, in its order
    static constexpr int kR15 = 15, kSr = 16, kPr = 21;

public:
    // The inputs of the return rule (Return, ReturnInputs), as bits: the registers' entry values as origin:: has them (bit 16 is SR's bits
    // other than T), and two of the rule's own, which are no origin (CallEffects never holds them): the entry T flag, and the words of the
    // caller's frame the function reads before writing (its stack arguments: the word at the entry r15 + 4k is bit 27 + k for k < 4, bit 31
    // any above those). Not r15 or PR (the stack and the return address), nor memory, a stack address or what a call returned.
    static constexpr uint32_t kDepT = 1u << 22;
    static constexpr uint32_t kDepArg0 = 1u << 27;
    static constexpr uint32_t kDepArgs = 0x1Fu << 27;
    static constexpr uint32_t kInputs = 0x7FFFu | (1u << 16) | (0xFu << 17) | kDepT | kDepArgs;
    static uint32_t ArgDep(int64_t offset)
    {
        return offset < 0 ? 0u : offset < 16 ? kDepArg0 << (offset / 4) : 1u << 31;
    }

private:
    // A dependence set by bit position: what bit 0, bit 1, bit 8 and bit 9 of a value (T, S, Q and M when the value is SR) and the rest of its
    // bits were computed from. SLEIGH writes a flag as a masked update of SR (`sr = (sr & ~1) | t`): a set kept for the whole value gives every
    // flag a compare wrote the entry SR among its inputs, and a T that `cmp/eq` wrote cannot be told from the T the caller left (A3-57); a Q
    // and an M that `div0u` cleared, from the ones it found.
    struct Deps
    {
        uint32_t b[5] = { 0, 0, 0, 0, 0 };   // bits 0, 1, 8, 9; the rest
        static int64_t Pos(int k) { return k == 0 ? 0 : k == 1 ? 1 : k == 2 ? 8 : 9; }
        static int Slot(int64_t bit) { return bit == 0 ? 0 : bit == 1 ? 1 : bit == 8 ? 2 : bit == 9 ? 3 : 4; }
        static Deps All(uint32_t o)
        {
            Deps d;
            for (uint32_t& x : d.b) x = o;
            return d;
        }
        uint32_t Union() const { return b[0] | b[1] | b[2] | b[3] | b[4]; }
        uint32_t At(int64_t bit) const { return bit < 0 || bit >= 64 ? 0u : b[Slot(bit)]; }
        Deps& operator|=(const Deps& o)
        {
            for (int k = 0; k < 5; ++k) b[k] |= o.b[k];
            return *this;
        }
        bool operator==(const Deps& o) const { return std::equal(b, b + 5, o.b); }
        // x >> c and x << c for a constant c; x & c.
        Deps Right(int64_t c) const
        {
            if (c <= 0) return *this;
            Deps d;
            for (int k = 0; k < 4; ++k) d.b[k] = At(Pos(k) + c);
            d.b[4] = c < 64 ? b[4] : 0u;
            for (int k = 0; k < 4; ++k)
                if (Pos(k) >= c && Slot(Pos(k) - c) == 4) d.b[4] |= b[k];
            return d;
        }
        Deps Left(int64_t c) const
        {
            if (c <= 0) return *this;
            Deps d;
            for (int k = 0; k < 4; ++k) d.b[k] = At(Pos(k) - c);
            d.b[4] = c < 64 ? b[4] : 0u;
            for (int k = 0; k < 4; ++k)
                if (Pos(k) + c < 64 && Slot(Pos(k) + c) == 4) d.b[4] |= b[k];
            return d;
        }
        Deps Masked(uint64_t c) const
        {
            Deps d;
            for (int k = 0; k < 4; ++k) d.b[k] = (c >> Pos(k)) & 1u ? b[k] : 0u;
            d.b[4] = (c & ~uint64_t(0x303)) != 0 ? b[4] : 0u;
            return d;
        }
        // A truth value (0 or 1) computed from all of these.
        Deps Truth() const
        {
            Deps d;
            d.b[0] = Union();
            return d;
        }
        // A sum, a difference, a product: bit 0 from the bits 0 alone, the carry into every other.
        static Deps Sum(const Deps& x, const Deps& y)
        {
            Deps d = All(x.Union() | y.Union());
            d.b[0] = x.b[0] | y.b[0];
            return d;
        }
    };

    struct Value
    {
        enum Kind : uint8_t { Fuzzy, Reg, Sp, Imm } kind = Fuzzy;
        int64_t v = 0;               // Reg: the register's index; Sp: the offset; Imm: the constant
        uint32_t from = origin::kAll;   // Fuzzy: what it can come from
        // Fuzzy: ...of that, what it was computed from here, not what a call returned (A3-7), by bit position, and the entry T and the stack
        // arguments among it (kDepT, kDepArg0); through a load's address as well as from what was loaded (`mov.b @(r0,r4),r0` reads r0 and r4).
        Deps dep = Deps::All(origin::kAll);
        // Fuzzy: the op that made it (Step's mDef), or kJoin for a join of two different ones. Values are told apart by what made them, not by
        // what they are made of: two loads, or r4 + 1 and r4 + 2, have the same origins and are two values (A3-58). 0: made by no op of this
        // function (memory not written, a callee's write over a slot).
        static constexpr uint64_t kJoin = ~0ull;
        uint64_t def = 0;
        // Fuzzy, a stack slot only: written on some path to here and not on another, or only in part (a narrower store). The engine takes no
        // such word for a written stack argument (A3-60).
        bool hole = false;
        // A stack slot only: a word the function wrote at or above the stack pointer of a call whose callee is not lent (the call made at this
        // address), and has not written since. The callee may write it through its own r15, as a word of its incoming arguments (A3-61).
        uint32_t exposedAt = 0;
        // Fuzzy: a stack address plus an index, with the offset the address had before it (from r15 on entry): the value
        // points at or above it, taking the index as non-negative as the engine's own pointer arithmetic does (A3-9).
        static constexpr int64_t kNoBase = INT64_MIN;
        // ...or "from the frame's lowest slot": a base a loop kept lowering (A3-16) or an index that may be negative
        // (A3-17) leaves no lower bound but the frame's own. The least base there is, so a join with it is it.
        static constexpr int64_t kLowBase = INT64_MIN + 1;
        static int64_t MoveBase(int64_t base, int64_t by) { return base == kLowBase ? kLowBase : Wrap(base + by); }
        int64_t stackBase = kNoBase;
        int64_t Base() const { return kind == Sp ? v : kind == Fuzzy ? stackBase : kNoBase; }
        // A3-71: an offset from a parameter register's entry value (r0-r14, not r15), for recognising a store below the address a callee was
        // handed in that register (`*--p = v`, `mov.l r1,@(-4,r4)`). 'baseReg' names the register, or -1. 'regOff' is the constant offset, or
        // kLowBase (a variable was subtracted, or a loop lowered it: possibly below) or kHighBase (a non-negative variable index was added: at
        // or above the entry value, exact unknown, as the engine takes an index). A store is below when regOff is kLowBase or a negative constant.
        static constexpr int64_t kHighBase = INT64_MIN + 2;
        int8_t baseReg = -1;
        int64_t regOff = 0;
        // Move a register offset by a constant, or by a non-negative variable (index added, or value subtracted).
        static int64_t MoveRegConst(int64_t off, int64_t by)
        {
            by = Wrap(by);   // the immediate is a 32-bit value (`add #-4` is 0xFFFFFFFC): read it as signed, or a decrement looks like a huge add
            return off == kLowBase ? kLowBase : off == kHighBase ? (by < 0 ? kLowBase : kHighBase) : Wrap(off + by);
        }
        static int64_t MoveRegVar(int64_t off, bool add)
        {
            if (!add) return kLowBase;   // a variable subtracted: possibly below
            return off == kLowBase ? kLowBase : off == kHighBase ? kHighBase : off >= 0 ? kHighBase : kLowBase;
        }
        bool RegBase(int& reg, int64_t& off) const
        {
            if (kind == Reg && v >= 0 && v < 15 && v != 15) { reg = static_cast<int>(v); off = 0; return true; }
            if (kind == Fuzzy && baseReg >= 0) { reg = baseReg; off = regOff; return true; }
            return false;
        }
        bool StoresBelow() const
        {
            return kind == Fuzzy && baseReg >= 0 && (regOff == kLowBase || (regOff != kHighBase && regOff < 0));
        }
        static Value Entry(int r) { Value x; x.kind = Reg; x.v = r; return x; }
        static Value Stack(int64_t off) { Value x; x.kind = Sp; x.v = off; return x; }
        static Value Const(int64_t c) { Value x; x.kind = Imm; x.v = c; return x; }
        // Fuzzy: computed, at least in part, by this function's own instructions or loaded by them (A3-13, A3-14); false
        // for what only a call returned. An exact value (a register's entry value, a stack address, a constant) is
        // always the function's own: put there by it, or, for r0's own entry value, left alone (see OwnInR0).
        bool own = true;
        // The value was held in r0 across a call to a callee that keeps r0: the engine kills r0 at every call, so it
        // does not see this value at a later return (A3-13).
        bool carried = false;
        // The value was the target of a `jsr @r0`: compiled code leaves the callee's address in r0 and the callee that keeps r0
        // leaves it there, which is no value anyone returns (10 of the corpus's void functions do). Never carried.
        bool callTarget = false;
        // A value a call returned (not the function's own) that some instruction has read since: a result the function used, which compiled
        // code leaves in r0 as scratch once it has. Held across a callee that keeps r0 and never read, it is a return (A3-19).
        bool read = false;
        // Fuzzy: an operation of this function had r0's entry value as an operand (not merely a path that leaves r0 alone).
        bool usesR0 = false;
        // Fuzzy: made, wholly or in part, by a join of two different values (the engine's MULTIEQUAL).
        bool joined = false;
        // Fuzzy: a join one of whose alternatives is r0's entry value itself, a path that leaves r0 alone (A3-45). Not kept by an operation: a value
        // computed from such a join is r0's entry value transformed on every path, not a path that leaves it alone.
        bool keepsR0 = false;
        // Fuzzy: a stack address built from one at or above the entry r15 (the exact r15, a word of the caller's frame) and an
        // index that may be anything. The base is given up for the frame's lowest slot (kLowBase), which a local array's address
        // with a variable index also is, so this says which of the two a store through it may be: the caller's word (A3-39).
        bool callerHigh = false;
        // Fuzzy: a low part of this register's entry value, and nothing done to it (the operand of mov.w / mov.b of a parameter,
        // which SLEIGH takes as a smaller varnode of the register): the register's number, else -1. A spill of a held parameter
        // is told from a value computed from it by this (A3-40).
        int8_t part = -1;
        static Value From(uint32_t o) { Value x; x.from = o; x.dep = Deps::All(o); return x; }
        static Value From(uint32_t o, uint32_t direct) { Value x; x.from = o; x.dep = Deps::All(direct); return x; }
        static Value From(uint32_t o, const Deps& d) { Value x; x.from = o; x.dep = d; return x; }
        // What a callee returned: not computed from anything here.
        static Value Returned(uint32_t o) { Value x; x.from = o; x.dep = Deps(); x.own = false; return x; }
        bool Own() const { return kind != Fuzzy || own; }
        // As an operand: r0's entry value itself, or something already computed from it.
        bool Joined() const { return kind == Fuzzy && joined; }
        bool KeepsR0() const { return (kind == Reg && v == 0) || (kind == Fuzzy && keepsR0); }
        bool UsesR0() const { return (kind == Reg && v == 0) || (kind == Fuzzy && usesR0); }
        // A stack address that is, or may be, at or above the entry r15: a store through it can be a word of the caller's frame.
        // (A local array below r15 indexed by a variable is not: its base is negative. Nor is an address inside the last word below
        // r15, a saved register's: an aligned access of any width ends before r15.)
        bool AtOrAboveEntry() const
        {
            if (kind == Sp) return v >= 0;
            return kind == Fuzzy && (callerHigh || (stackBase != kNoBase && stackBase >= 0));
        }
        // A value in r0 that this function put there: not r0's entry value left alone, not what a call returned.
        bool OwnInR0() const { return !(kind == Reg && v == 0) && Own(); }
        uint32_t Origins() const
        {
            switch (kind)
            {
            case Reg: return 1u << v;
            case Sp: return origin::kStack;
            case Imm: return 0;
            default: return from;
            }
        }
        // What the value was computed from by this function's own instructions, as origins: a call's outputs
        // share one set of origins (everything any of the callee's registers can come from), so a value that
        // only a call made says nothing about the registers it was passed.
        Deps DepsOf() const
        {
            switch (kind)
            {
            case Reg:
            {
                if (v != kSr) return Deps::All(1u << v);
                Deps d = Deps::All(1u << kSr);
                d.b[0] = kDepT;
                return d;
            }
            case Sp: return Deps::All(origin::kStack);
            case Imm: return Deps();
            default: return dep;
            }
        }
        uint32_t Direct() const { return DepsOf().Union(); }
        bool operator==(const Value& o) const
        {
            return kind == o.kind && v == o.v && carried == o.carried && callTarget == o.callTarget && read == o.read &&
                   exposedAt == o.exposedAt &&
                   (kind != Fuzzy || (from == o.from && dep == o.dep && def == o.def && hole == o.hole && stackBase == o.stackBase && own == o.own &&
                                     usesR0 == o.usesR0 && joined == o.joined && keepsR0 == o.keepsR0 && callerHigh == o.callerHigh && part == o.part &&
                                     baseReg == o.baseReg && regOff == o.regOff));
        }
    };
    using Key = std::tuple<ghidra::AddrSpace*, ghidra::uintb, ghidra::uint4>;
    struct State
    {
        bool reached = false;
        bool escaped = false;
        // The inputs (kInputs) a branch taken on the way here was decided by: a condition, or a jump table's index, computed from them (A3-55).
        // The value r0 has at a return is chosen by them when it differs by path, whatever the alternatives are made of.
        uint32_t ctl = 0;
        std::map<Key, Value> vars;         // registers and temporaries; absent = anything
        std::map<int64_t, Value> slots;    // 4-byte stack words by offset; absent = loaded from memory
    };
    struct Op
    {
        ghidra::OpCode code;
        bool hasOut = false;
        ghidra::VarnodeData out;
        std::vector<ghidra::VarnodeData> in;
    };
    struct Collect : public ghidra::PcodeEmit
    {
        std::vector<Op> ops;
        void dump(const ghidra::Address&, ghidra::OpCode opc, ghidra::VarnodeData* outvar, ghidra::VarnodeData* vars,
                  ghidra::int4 isize) override
        {
            Op op;
            op.code = opc;
            if (outvar != nullptr)
            {
                op.hasOut = true;
                op.out = *outvar;
            }
            op.in.assign(vars, vars + isize);
            ops.push_back(std::move(op));
        }
    };

    static Key KeyOf(const ghidra::VarnodeData& vn) { return Key(vn.space, vn.offset, vn.size); }

    static Value Get(const State& s, const ghidra::VarnodeData& vn)
    {
        if (vn.space->getType() == ghidra::IPTR_CONSTANT) return Value::Const(static_cast<int64_t>(vn.offset));
        auto f = s.vars.find(KeyOf(vn));
        if (f != s.vars.end()) return f->second;
        // Part of a tracked register (mov.w r1,@r2 stores r1's low word): a constant's bytes, else
        // what the whole can come from. Not found as a whole either: anything (review of 0e3cd12;
        // taken for anything, a word store of a register made the signature withheld).
        for (const auto& v : s.vars)
        {
            if (std::get<0>(v.first) != vn.space || std::get<1>(v.first) > vn.offset ||
                vn.offset + vn.size > std::get<1>(v.first) + std::get<2>(v.first))
                continue;
            if (v.second.kind != Value::Imm)
            {
                // (big-endian: the part's bit 0 is the whole's bit 8 * the bytes below it)
                const uint64_t below = std::get<2>(v.first) - (vn.offset - std::get<1>(v.first)) - vn.size;
                Value p = Value::From(v.second.Origins(), v.second.DepsOf().Right(static_cast<int64_t>(8 * below)));
                if (v.second.kind == Value::Reg) p.part = static_cast<int8_t>(v.second.v);
                // A part of a register is rebuilt as a Fuzzy value, so the flags the return rule reads would be lost -- but a part of r0's entry
                // value is a use of it, a part of a carried or joined value is that, a part of what a call returned is not the function's own
                // (A3-68). Keep them.
                p.usesR0 = v.second.UsesR0();
                p.carried = v.second.carried;
                p.joined = v.second.Joined();
                p.keepsR0 = v.second.KeepsR0();
                p.read = v.second.read;
                p.own = v.second.Own();
                return p;
            }
            const uint64_t whole = std::get<2>(v.first), below = whole - (vn.offset - std::get<1>(v.first)) - vn.size;
            const uint64_t mask = vn.size >= 8 ? ~0ull : (1ull << (8 * vn.size)) - 1;   // big-endian: the last bytes are the low ones
            Value p = Value::Const(static_cast<int64_t>((static_cast<uint64_t>(v.second.v) >> (8 * below)) & mask));
            p.carried = v.second.carried;   // a part of a value held in r0 across a call that keeps r0 is still that (A3-68)
            return p;
        }
        return Value();
    }

    void Set(State& s, const ghidra::VarnodeData& vn, Value v)
    {
        // A value an op of this function made carries the op (Value::def); a copy keeps the one it had.
        if (v.kind == Value::Fuzzy && v.def == 0) v.def = mDef;
        v.hole = false;
        v.exposedAt = 0;
        for (auto it = s.vars.begin(); it != s.vars.end();)
        {
            const bool overlap = std::get<0>(it->first) == vn.space && std::get<1>(it->first) < vn.offset + vn.size &&
                                 vn.offset < std::get<1>(it->first) + std::get<2>(it->first);
            it = overlap ? s.vars.erase(it) : std::next(it);
        }
        s.vars[KeyOf(vn)] = v;
        if (v.kind == Value::Sp && v.v < mLowestSp) mLowestSp = v.v;
        // Below r15 is free stack: a callee's pushes and any interrupt write there. A slot the
        // stack pointer rises past holds anything from then on.
        if (KeyOf(vn) == KeyOf(mReg[kR15]))
            for (auto& slot : s.slots)
                if (v.kind != Value::Sp || slot.first < v.v) slot.second = FreeStack();
    }

    static constexpr uint32_t kAnything = origin::kRegisters | origin::kStack | origin::kMemory;

    // What a load from untracked memory can give.
    static Value Loaded(const State& s) { return Value::From(origin::kMemory | (s.escaped ? origin::kStack : 0)); }
    // A value read from free stack or through a stack pointer the interpreter cannot place (below r15, or a pointer with no bound): its
    // origins are anything (it may be a spilled register), but as a dependence for the return rule it is a read of memory and the stack, not of
    // every register -- a word read from the stack is no register a caller passes. Without this the union of its dependences with a real input
    // (`nothing-known + r3`) hid that input (A3-66, A3-67); the input is now kept, and the pure memory read is still no input (06038F64/008).
    static Value FreeStack() { return Value::From(kAnything, Deps::All(origin::kMemory | origin::kStack)); }
    // ...and a slot of the frame at 'offset' that nothing wrote on some path: at or above the entry r15, a word of the caller's frame, which is a
    // stack argument (kDepArg0).
    static Value Unwritten(const State& s, int64_t offset)
    {
        Value u = Loaded(s);
        u.dep = Deps::All(u.from | ArgDep(offset));
        u.hole = true;
        return u;
    }

    // A store this function (or a callee) may have made over slots at offsets 'from' and up, of a
    // value from 'stored': each holds what it held or that.
    // A register the callee-saved convention keeps (r8-r14, GBR, VBR) or the return address, held in a slot as its entry value: what a
    // prologue saved there.
    static bool SavedRegister(const Value& v)
    {
        return v.kind == Value::Reg && ((v.v >= 8 && v.v <= 14) || v.v == 17 || v.v == 18 || v.v == kPr);
    }

    // 'spare': the store is a lent callee's through a pointer it was handed. The engine assumes such a callee does not write its caller's
    // saved registers (a pointer to a local reaches the object, not the prologue's pushes), so the slots that hold one are left, but for the
    // offsets in 'exact' (a register it stores through holds that frame address itself): A3-24. A store made through an address
    // reaches the bytes from it up, as many as the access is wide (1, 2 or 4, aligned: the SH-2 takes an address error otherwise), so a
    // slot is not spared when the address lies anywhere in its word: `exact` holds addresses, and the width is the address's own alignment
    // (A3-42: the address of a saved register's word plus 2, and a halfword store).
    static bool Reaches(int64_t address, int64_t slot)
    {
        const int64_t width = (address & -address) == 0 || (address & -address) > 4 ? 4 : (address & -address);
        return address < slot + 4 && slot < address + width;
    }
    static void Clobber(State& s, uint32_t stored, int64_t from = INT64_MIN, int64_t to = INT64_MAX, bool spare = false,
                        const std::vector<int64_t>* exact = nullptr)
    {
        auto handed = [&](int64_t slot) {
            if (exact != nullptr)
                for (int64_t address : *exact)
                    if (Reaches(address, slot)) return true;
            return false;
        };
        for (auto& slot : s.slots)
            if (slot.first + 4 > from && slot.first < to && !(spare && SavedRegister(slot.second) && !handed(slot.first)))
            {
                Deps d = slot.second.DepsOf();
                d |= Deps::All(stored);
                const bool hole = slot.second.kind == Value::Fuzzy && slot.second.hole;
                const uint32_t exposedAt = slot.second.exposedAt;
                const Value was = slot.second;
                slot.second = Value::From(slot.second.Origins() | stored, d);
                // (a word that may now hold what it held is still a use of r0, carried or joined if that was: A3-68's flags, fail-closed)
                slot.second.usesR0 = was.UsesR0() && !(was.kind == Value::Reg && was.v == 0);
                slot.second.carried = was.carried;
                slot.second.joined = was.Joined();
                slot.second.keepsR0 = was.KeepsR0();
                slot.second.hole = hole;
                slot.second.exposedAt = exposedAt;
            }
    }

    static uint32_t AllSlots(const State& s)
    {
        uint32_t o = 0;
        for (const auto& slot : s.slots) o |= slot.second.Origins();
        return o;
    }

    // An address that may point into this function's stack (or above it, its caller's).
    static bool MayBeStack(const State& s, const Value& addr)
    {
        const uint32_t o = addr.Origins();
        return (o & origin::kStack) || (s.escaped && (o & origin::kMemory));
    }

    // The stack-address bases of every value in 'st' given up for the lowest slot (see Run).
    static void Widen(State& st)
    {
        auto widen = [](Value& v) {
            if (v.kind == Value::Fuzzy && v.stackBase != Value::kNoBase) v.stackBase = Value::kLowBase;
            if (v.kind == Value::Fuzzy && v.baseReg >= 0) v.regOff = Value::kLowBase;   // A3-71: a loop that moves a register pointer
        };
        for (auto& v : st.vars) widen(v.second);
        for (auto& v : st.slots) widen(v.second);
    }

    // Keeps in 'into' what both states agree on, the rest as its origins; true when 'into' changed.
    // 'b' joined into 'a' (the engine's MULTIEQUAL of two paths, or the returns' values at a common exit): what both agree on, the rest as its
    // origins and dependences, made by a join (Value::kJoin) when the two were made by different ops; true when 'a' changed.
    static bool Merge(Value& a, const Value& b)
    {
        if (a == b) return false;
        Deps dep = a.DepsOf();
        dep |= b.DepsOf();
        Value m = Value::From(a.Origins() | b.Origins(), dep);
        m.def = Value::kJoin;
        m.hole = (a.kind == Value::Fuzzy && a.hole) || (b.kind == Value::Fuzzy && b.hole);
        m.exposedAt = a.exposedAt ? a.exposedAt : b.exposedAt;
        m.part = a.kind == Value::Fuzzy && b.kind == Value::Fuzzy && a.part == b.part ? a.part : int8_t(-1);
        m.own = a.Own() || b.Own();
        m.carried = a.carried || b.carried;
        m.callTarget = a.callTarget && b.callTarget;
        m.read = a.read && b.read;
        m.usesR0 = a.usesR0 || b.usesR0;
        m.joined = true;
        m.keepsR0 = a.KeepsR0() || b.KeepsR0();
        // ...at or above r15 on every path, or flagged: an exact address at or above r15 joined with a lower one is a walk (a loop
        // that steps a pointer down from r15, `mov.l r0,@-r1`, A3-16) as often as a choice of two addresses, and is not followed.
        m.callerHigh = (a.kind == Value::Fuzzy && a.callerHigh) || (b.kind == Value::Fuzzy && b.callerHigh) ||
                       (a.AtOrAboveEntry() && b.AtOrAboveEntry());
        // A frame address joined with a value that is no stack address at all (a null pointer, a loaded pointer: `g(c ? &s : 0)`)
        // is still that address on the path that has one (A3-26): its base stands. A side that may be a stack address
        // whose base is not known leaves the join without one.
        {
            const bool ab = a.Base() != Value::kNoBase, bb = b.Base() != Value::kNoBase;
            const bool aStack = (a.Origins() & origin::kStack) != 0, bStack = (b.Origins() & origin::kStack) != 0;
            if (ab && bb) m.stackBase = std::min(a.Base(), b.Base());
            else if (ab && !bStack) m.stackBase = a.Base();
            else if (bb && !aStack) m.stackBase = b.Base();
        }
        // A3-71: a register-relative pointer kept where both sides name the same register; two offsets of it (a loop decrement) is lowered to
        // "possibly below".
        {
            int ar = -1, br = -1;
            int64_t ao = 0, bo = 0;
            if (a.RegBase(ar, ao) && b.RegBase(br, bo) && ar == br)
            {
                m.baseReg = static_cast<int8_t>(ar);
                m.regOff = ao == bo ? ao : Value::kLowBase;
            }
        }
        if (m == a) return false;
        a = m;
        return true;
    }

    static bool Join(State& into, const State& from)
    {
        bool changed = false;
        if (from.escaped && !into.escaped) into.escaped = changed = true;
        if ((from.ctl & ~into.ctl) != 0)
        {
            into.ctl |= from.ctl;
            changed = true;
        }
        auto merge = [&changed](Value& a, const Value& b) {
            if (Merge(a, b)) changed = true;
        };
        // A register or temporary absent from either is anything.
        for (auto it = into.vars.begin(); it != into.vars.end();)
        {
            auto f = from.vars.find(it->first);
            if (f == from.vars.end())
            {
                it = into.vars.erase(it);
                changed = true;
                continue;
            }
            merge(it->second, f->second);
            ++it;
        }
        // A slot absent from either holds what a load from memory gives there, and was not written on that path.
        for (auto& slot : into.slots)
        {
            auto f = from.slots.find(slot.first);
            merge(slot.second, f == from.slots.end() ? Unwritten(from, slot.first) : f->second);
        }
        for (const auto& slot : from.slots)
            if (into.slots.count(slot.first) == 0)
            {
                Value v = Unwritten(into, slot.first);
                merge(v, slot.second);
                if (!(v == Unwritten(into, slot.first))) into.slots[slot.first] = v;
            }
        return changed;
    }

    static int64_t Wrap(int64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(v)); }

    void Call(State& s, uint32_t at, bool targetIsR0 = false, bool haveTarget = false, uint32_t target = 0)
    {
        CallEffects e;   // unknown: anything
        bool signatureLent = false;
        auto t = mCallAt.find(at);
        if (t != mCallAt.end())
        {
            haveTarget = true;
            target = t->second;
        }
        if (haveTarget)
        {
            auto p = mLent.find(target);
            if (p != mLent.end())
            {
                e = p->second.effects;
                signatureLent = p->second.known;
            }
        }
        if (!signatureLent)
        {
            NoteFrameEscape(s, 2, true, at);
            if (mTolerant)
            {
                // The probe's callee is any callee the first flow cannot name, and the engine reads it by the default model: it takes
                // r4-r7 as inputs it may write through (A3-37), and keeps r8-r14, GBR, VBR and r15 (kKeptByCall; extrapop 0) -- with
                // the registers the model keeps lost, the frame pointer and the stack pointer after one such call had no base, and
                // no frame address made from them after it was seen (A3-36, A3-37).
                for (int r = 4; r < 8; ++r) NoteFrameEscape(s, r, false, at);
                // ...which is so for the arguments the engine sees. It reads r4-r7 in order and fills in at most two unset registers before one that is
                // set (ParamListStandard::fillinMap, forceInactiveChain with a chain of 2): a frame address in r7 with r4-r6 all untouched is no argument
                // of the call, and the locals it reaches would be folded across it (A3-48). Said like the address in r2.
                // ...and the stack argument is the same: the word at the call's r15 comes after r7, and the engine drops an argument that follows
                // a run of three unset ones, so with r4-r6 or r5-r7 all untouched a frame address stored there is in no argument either (A3-51).
                {
                    bool unset[4];
                    for (int r = 4; r < 8; ++r)
                    {
                        const Value x = Get(s, mReg[r]);
                        // (a register an earlier call killed is as unset to the engine as one nothing wrote)
                        unset[r - 4] = (x.kind == Value::Reg && x.v == r) || (x.kind == Value::Fuzzy && !x.own);
                    }
                    if (unset[0] && unset[1] && unset[2]) NoteFrameEscape(s, 7, true, at);
                    NoteStackArguments(s, at, (unset[0] && unset[1] && unset[2]) || (unset[1] && unset[2] && unset[3]));
                }
                if (haveTarget) ExposeStackArguments(s, at, target);
                for (const KilledRegister& k : kKeptByCall) e.preserved |= 1u << k.bit;
                e.preserved |= 1u << kR15;
                e.spDelta = 0;
            }
            // A3-70: a callee walked for CalleeFrameWrites that itself hands its incoming frame onward -- r15 still at or above its entry (its
            // stack-argument area is this function's frame), or a frame address in an argument register -- may write any word of its caller's
            // frame through a call the nested walk cannot resolve. Fail closed: treat it as writing the whole frame.
            if (mNested)
            {
                const Value sp = Get(s, mReg[kR15]);
                bool handsFrame = sp.kind == Value::Sp && sp.v >= 0;
                for (int r = 0; r < 8 && !handsFrame; ++r) handsFrame = Get(s, mReg[r]).AtOrAboveEntry();
                if (handsFrame)
                {
                    mOwnFrameWords |= CallEffects::kFrameAny;
                    mCallerFrameStores |= origin::kFrame;
                }
            }
        }
        else
        {
            // A lent callee gets its pointer in a register of its signature, which the engine sees, but only as far as its own
            // reading of the address goes: an index added to it or a loop that moved it leaves the locals under the address it
            // was made from out of the alias (A3-22). Declared for the registers the callee stores through, when the address is
            // not exact (an exact one is the engine's, and was never a problem).
            for (int r = 0; r < 15; ++r)
                if ((e.storesThrough >> r) & 1u) NoteFrameEscape(s, r, false, at);
            // ...and one that writes below the exact frame address it was handed (a lent `fill backwards`, `*--p = v`): the engine reaches only
            // the object at the address and above, so the locals below it would be folded across the call (A3-71). The amount is unknown, so the
            // whole frame below is exposed. NoteFrameEscape's skip of an exact address stands for a callee that only writes at or above it.
            for (int r = 0; r < 15; ++r)
                if ((e.storesBelow >> r) & 1u)
                {
                    const Value h = Get(s, mReg[r]);
                    if (h.kind == Value::Sp || (h.kind == Value::Fuzzy && h.Base() != Value::kNoBase))
                    {
                        mEscaped = true;
                        mEscapedLow = true;   // from the frame's lowest slot (FrameEscape)
                    }
                }
            // ...and one handed an address at or above this function's entry r15 (`mov r15,r4 ; add #4,r4 ; bsr h`, h stores through
            // r4) writes a word of this function's caller's frame, as a store of its own there would (A3-39). What h stores is not in the
            // effects as a value, so the hand-back is withheld whatever it is.
            for (int r = 0; r < 15; ++r)
                if (((e.storesThrough >> r) & 1u) && Get(s, mReg[r]).AtOrAboveEntry()) mCallerFrameStores |= origin::kFrame;
        }
        Apply(s, e, targetIsR0);
    }

    // A call whose signature is not lent may read r2: the SH ABI passes the address of a returned
    // structure there (SH_STRUCT_VALUE_REGNUM 2 in gcc, and Renesas' compilers), and the engine's
    // calling convention has no input in r2. So the copy of r15 that made the address is dead code
    // before the engine looks for pointers into the frame, and the callee's writes through it are
    // taken for none (A3-4). A callee whose signature is lent has been through WithholdReason, which
    // lends none that reads r2. Only r2, not the other registers a callee could read: r0, r1 and r3
    // are scratch the compiler leaves a frame address in, and r8-r14 hold the frame pointer, so a
    // rule over them would put every local of every function in an alias.
    void NoteFrameEscape(const State& s, int reg, bool exactToo, uint32_t at)
    {
        NoteFrameEscapeOf(Get(s, mReg[reg]), reg, exactToo, at);
    }

    // The words from the call's r15 up, where a callee reads its stack arguments (the SH calling convention puts the fifth at its entry r15): a
    // frame address stored in one the engine drops is no argument the call shows, as an address in r7 alone is not (A3-48). The engine drops
    // every stack word when r4-r6 or r5-r7 are all unset (A3-51: 'all'), and otherwise the words from the first one not written on every path
    // up, whatever r4-r7 hold (A3-60: held to the engine by TestArea3Round9Fix's two tables). Bit 15 of the site's mask.
    void NoteStackArguments(const State& s, uint32_t at, bool all)
    {
        const Value sp = Get(s, mReg[kR15]);
        if (sp.kind != Value::Sp) return;
        int64_t first = sp.v;
        if (!all)
            for (;; first += 4)
            {
                const auto f = s.slots.find(first);
                if (f == s.slots.end() || (f->second.kind == Value::Fuzzy && f->second.hole)) break;
            }
        for (const auto& slot : s.slots)
        {
            if (slot.first < first) continue;
            // The same exact address in r4-r7 is an argument the call shows (a pointer to a local kept in a local, passed too): the engine
            // declares the alias from it, and the note would say it is in none of the arguments.
            bool shown = false;
            if (slot.second.kind == Value::Sp)
                for (int r = 4; r < 8; ++r)
                {
                    const Value x = Get(s, mReg[r]);
                    shown = shown || (x.kind == Value::Sp && x.v == slot.second.v);
                }
            if (!shown) NoteFrameEscapeOf(slot.second, kR15, true, at);
        }
    }

    // A callee that is not lent may write the words of this function's frame from the call's r15 up through its own r15: they are its incoming
    // stack arguments, and a hand-written one hands a value back in its argument slot (A3-21's channel, which withholds the callee). The engine
    // takes the call to write none of them and folds this function's read of the word after the call to what it stored before (A3-61). Each word
    // this function wrote there, and that the callee writes (CalleeFrameWrites: bit k the word at the call's r15 + 4k), is marked with the call
    // (Value::exposedAt); a read of it after the call declares the alias and says the note (ReadExposed). A word a later call leaves alone keeps
    // the mark of the call that wrote it, so a word the first of two calls writes is not folded away because the second does not (A3-75). Not
    // every word at or above the call's r15: compiled code keeps its locals there across calls, and a note at each (815 of 839) says nothing.
    void ExposeStackArguments(State& s, uint32_t at, uint32_t target)
    {
        const Value sp = Get(s, mReg[kR15]);
        if (sp.kind != Value::Sp) return;
        const uint32_t words = CalleeFrameWrites(target);
        if (words == 0) return;   // the callee writes none of its caller's frame
        for (auto& slot : s.slots)
            if (slot.first >= sp.v && slot.second.exposedAt == 0 && !(slot.second.kind == Value::Fuzzy && slot.second.hole))
            {
                const int64_t k = (slot.first - sp.v) / 4;
                if ((words & CallEffects::kFrameAny) || (k >= 0 && k < 31 && ((words >> k) & 1u))) slot.second.exposedAt = at;
            }
    }
    // The words of its caller's frame that the function at 'target' writes itself, as CallEffects::frameWords has them (A3-21's stores, an
    // argument register spilled into the home area included), read from its code by the same walk; 0 when it writes none, or when the walk
    // cannot follow it (memory not captured, too many paths). Asked once per target, by the probe only.
    uint32_t CalleeFrameWrites(uint32_t target)
    {
        if (mNested) return 0;
        const auto known = mCalleeFrameWrites.find(target);
        if (known != mCalleeFrameWrites.end()) return known->second;
        static const JumpDests kNoTables;
        ReturnCheck callee(mFd, target, mLent, kNoTables);
        callee.mNested = true;
        callee.mMaxSteps = kNestedMaxSteps;
        callee.SetTolerant();
        CallEffects unused;
        uint32_t words = 0;
        if (callee.Run(unused).empty() && callee.CallerFrameStores() != 0) words = callee.mOwnFrameWords;
        mCalleeFrameWrites[target] = words;
        return words;
    }
    // ...the read of such a word, after the call that exposed it: the alias from the word up, and the note at that call (bit 16 of its mask).
    // Whether the callee writes it was settled when the word was marked (ExposeStackArguments), so each word points at the call that wrote it.
    void ReadExposed(const Value& slot, int64_t offset)
    {
        if (!mTolerant || slot.exposedAt == 0) return;
        mEscapeSites[slot.exposedAt] |= 1u << 16;
        if (!mEscaped || offset < mEscapeLow) mEscapeLow = offset;
        mEscaped = true;
    }

    void NoteFrameEscapeOf(const Value& v, int reg, bool exactToo, uint32_t at)
    {
        if (!exactToo && v.kind == Value::Sp) return;
        // An address with an index added to it (a returned structure that is an array element) points at or above
        // the address it was made from: that offset is the alias's lower bound, and every local from it up is aliased
        // whatever the index (A3-9). An address made from nothing known has none to declare: the whole frame would
        // make an array of every local.
        const int64_t base = v.Base();
        if (base == Value::kNoBase) return;
        if (exactToo) mEscapeSites[at] |= 1u << reg;   // the address is in no argument of the call: the text says so (A3-28)
        if (base == Value::kLowBase) mEscapedLow = true;
        else if (!mEscaped || base < mEscapeLow) mEscapeLow = base;
        mEscaped = true;
    }

    void Apply(State& s, const CallEffects& e, bool targetIsR0 = false)
    {
        // The callee's bits as origins here. Its entry registers are ours at the call. What it loads
        // from memory can be anything in memory, our slots included: it may read our frame through
        // its stack pointer or a pointer it was given. Its stack is ours above its r15.
        uint32_t reg[kTracked];
        for (int r = 0; r < kTracked; ++r) reg[r] = Get(s, mReg[r]).Origins();
        const uint32_t memory = Loaded(s).from | AllSlots(s);
        auto map = [&](uint32_t bits) {
            uint32_t o = bits & origin::kStack;
            if (bits & origin::kMemory) o |= memory;
            for (int r = 0; r < kTracked; ++r)
                if (bits & (1u << r)) o |= reg[r];
            return o;
        };
        const uint32_t stores = map(e.storesThrough & ~origin::kFrame);
        const uint32_t leaked = map(e.leaks);
        const uint32_t framed = map(e.frameValues);
        const uint32_t out = map(e.outOrigins);
        const Value sp = Get(s, mReg[kR15]);
        // Its pushes (and an interrupt's) land below the call's r15.
        if (sp.kind == Value::Sp) Clobber(s, kAnything, INT64_MIN, sp.v);
        else Clobber(s, kAnything);
        // Its writes and what it lets out are this function's too.
        Leak(s, leaked);
        if (e.storesThrough & origin::kFrame)
        {
            // Above its r15: our frame from the call's r15 up, and our caller's.
            mStoresThrough |= origin::kFrame;
            mFrameValues |= framed;
            if (sp.kind == Value::Sp && !(e.frameWords & CallEffects::kFrameAny))
            {
                // The words it wrote, at the offsets it wrote them (a spill of its arguments into the home area): not the saved PR above.
                for (int k = 0; k < 31; ++k)
                    if ((e.frameWords >> k) & 1u)
                    {
                        const int64_t at = Wrap(sp.v + 4 * k);
                        Clobber(s, framed, at, at + 4);
                        if (at + 4 > 0) AddFrameWords(at, 4);
                    }
            }
            else
            {
                Clobber(s, framed, sp.kind == Value::Sp ? sp.v : INT64_MIN);
                mFrameWords |= CallEffects::kFrameAny;
            }
        }
        if (e.storesThrough & ~origin::kFrame)
        {
            std::vector<int64_t> exact;   // frame addresses the callee was handed in the registers it stores through
            for (int r = 0; r < 15; ++r)
                if (((e.storesThrough >> r) & 1u) && reg[r] == origin::kStack)
                {
                    const Value h = Get(s, mReg[r]);
                    if (h.kind == Value::Sp) exact.push_back(h.v);
                }
            StoreThrough(s, Value::From(stores), leaked | framed, true, &exact);
        }
        // A value this function put in r0 and a callee that keeps r0 leaves there: the engine's call killed it (A3-13). Not
        // the callee's own address in `jsr @r0`, which compiled code leaves there (10 of the corpus's void functions).
        if (e.preserved & 1u)
        {
            Value r0 = Get(s, mReg[0]);
            if (targetIsR0 || r0.callTarget)
                r0.callTarget = true;
            else if (r0.OwnInR0() || (!(r0.kind == Value::Reg && r0.v == 0) && !r0.read))
                r0.carried = true;   // a call's return included, while nothing has used it (A3-19); not r0's entry value left alone
            Set(s, mReg[0], r0);
        }
        for (int r = 0; r < kTracked; ++r)
        {
            if (e.preserved & (1u << r)) continue;
            // r15 moved by a constant at every return of the callee: where it ends is known.
            if (r == kR15 && e.spDelta != CallEffects::kSpUnknown && sp.kind == Value::Sp)
                Set(s, mReg[r], Value::Stack(Wrap(sp.v + e.spDelta)));
            else
                Set(s, mReg[r], Value::Returned(out));   // what the callee returns: not computed from anything here
        }
    }

    // Values let out to memory this function does not track: its callers learn of them through
    // 'leaks', and a stack address among them may come back from any load.
    void Leak(State& s, uint32_t origins)
    {
        mLeaks |= origins;
        if (origins & origin::kStack) s.escaped = true;
    }

    // The words of [off, off + n) that lie at or above the entry r15, as CallEffects::frameWords has them.
    void AddFrameWords(int64_t off, int64_t n)
    {
        for (int64_t w = std::max<int64_t>(off, 0) & ~int64_t(3); w < off + n; w += 4)
            mFrameWords |= w / 4 >= 31 ? CallEffects::kFrameAny : 1u << (w / 4);
    }

    void Store(State& s, const Value& addr, const ghidra::VarnodeData& valueVn, const Value& value)
    {
        const int64_t n = valueVn.size;
        const uint32_t v = value.Origins();
        // A3-71: a store below the frame address a parameter register held on entry (`*--p = v`, a negative displacement). A caller that hands
        // a lent callee an exact frame address in that register must expose the locals below it (Call reads CallEffects::storesBelow).
        if (addr.StoresBelow()) mStoresBelow |= 1u << addr.baseReg;
        if (addr.kind == Value::Sp)
        {
            if (addr.v + n > 0)   // at or above r15 on entry: the caller's frame
            {
                // What is written decides whether it is a hand-back: a parameter register's own value (any width) is the spill of an
                // argument into the home area, a value computed from one is not (A3-40; a constant has no origin and counts as kFrame).
                const bool spill = value.kind == Value::Reg || (value.kind == Value::Fuzzy && value.part >= 0);
                mCallerFrameStores |= spill && v != 0 ? v : (v | origin::kFrame);
                for (int64_t w = std::max<int64_t>(addr.v, 0) & ~int64_t(3); w < addr.v + n; w += 4)
                    mOwnFrameWords |= w / 4 >= 31 ? CallEffects::kFrameAny : 1u << (w / 4);
                mStoresThrough |= origin::kFrame;
                mFrameValues |= v;
                AddFrameWords(addr.v, n);
            }
            if (n != 4)
            {
                // Part of a word: it holds what it held and part of this.
                for (int64_t w = addr.v & ~int64_t(3); w < addr.v + n; w += 4)
                    if (s.slots.count(w) == 0) s.slots[w] = Unwritten(s, w);
            }
            Clobber(s, v, addr.v, addr.v + n);
            if (n == 4)
            {
                Value w = value;
                w.hole = false;   // (and no exposure: a register's value never carries one, Set)
                s.slots[addr.v] = w;
            }
            else
                // A partial store leaves the word a Fuzzy mix (Clobber), which drops the flags; a byte of r0, or of a carried value, spilled
                // and reloaded is still a use of r0, or carried (A3-68). Keep them on the word it touched.
                for (auto& slot : s.slots)
                    if (slot.first + 4 > addr.v && slot.first < addr.v + n)
                    {
                        slot.second.usesR0 = slot.second.UsesR0() || value.UsesR0();
                        slot.second.carried = slot.second.carried || value.carried;
                        slot.second.joined = slot.second.Joined() || value.Joined();
                        slot.second.keepsR0 = slot.second.KeepsR0() || value.KeepsR0();
                    }
            return;
        }
        // An address made of the entry r15 (or a word above it) and an index may be a word of the caller's frame: the same hand-back
        // as the exact store, whatever the register stored (the signature can hold it: `mov r15,r1 ; add r6,r1 ; mov.l r5,@r1`, A3-39).
        if (addr.AtOrAboveEntry())
        {
            mCallerFrameStores |= v | origin::kFrame;
            mOwnFrameWords |= CallEffects::kFrameAny;
        }
        StoreThrough(s, addr, v);
    }

    // A store through an address not known exactly, made here or by a callee, of a value from
    // 'stored': recorded in this function's summary, and over every slot it may hit. An absolute
    // address is taken never to be in the stack, as the engine's own stack analysis takes it.
    void StoreThrough(State& s, const Value& addr, uint32_t stored, bool spare = false, const std::vector<int64_t>* exact = nullptr)
    {
        const uint32_t o = addr.Origins();
        // Whether the address may be in the stack is asked before the store lets a stack address out: the store that publishes a frame address
        // (`mov.l r1,@r6` with r1 = r15 and r6 a pointer loaded from the literal pool) is not made through that address (A3-47).
        const bool mayBeStack = MayBeStack(s, addr);
        if (o == 0 || (o & ~origin::kStack)) Leak(s, stored);   // memory this function does not track
        if (o == 0) return;
        mStoresThrough |= o & ~origin::kStack;
        if (o & origin::kStack)   // a stack address it cannot place: its own frame or its caller's
        {
            mStoresThrough |= origin::kFrame;
            mFrameValues |= stored;
            mFrameWords |= CallEffects::kFrameAny;
        }
        if (mayBeStack) Clobber(s, stored, INT64_MIN, INT64_MAX, spare, exact);
        else Clobber(s, stored, 0);   // through an entry value: the caller's frame, at most
    }

    Value Load(const State& s, const Value& addr, ghidra::uint4 n)
    {
        if (addr.kind == Value::Sp)
        {
            // Below r15 an interrupt may have written anything.
            const Value sp = Get(s, mReg[kR15]);
            if (sp.kind != Value::Sp || addr.v < sp.v) return FreeStack();
            if (n == 4)
            {
                auto f = s.slots.find(addr.v);
                if (f != s.slots.end())
                {
                    ReadExposed(f->second, f->first);
                    return f->second;
                }
            }
            uint32_t o = Loaded(s).from;
            Deps d = Deps::All(o);
            Value r;
            for (const auto& slot : s.slots)
                if (slot.first < addr.v + n && addr.v < slot.first + 4)
                {
                    ReadExposed(slot.second, slot.first);
                    o |= slot.second.Origins();
                    d |= Deps::All(slot.second.Direct());
                    // A part of a word a byte spill of r0 (or a carried value) put there is read back: keep the flags (A3-68).
                    r.usesR0 = r.usesR0 || slot.second.UsesR0();
                    r.carried = r.carried || slot.second.carried;
                    r.joined = r.joined || slot.second.Joined();
                    r.keepsR0 = r.keepsR0 || slot.second.KeepsR0();
                }
            // A word nothing wrote: at or above the entry r15, the caller's (a stack argument).
            for (int64_t w = addr.v & ~int64_t(3); w < addr.v + n; w += 4)
                if (s.slots.count(w) == 0) d |= Deps::All(ArgDep(w));
            r.from = o;
            r.dep = d;
            return r;
        }
        // Through an address that may be in the stack but not exactly: nothing bounds it above r15,
        // so it may read free stack, which holds anything.
        return MayBeStack(s, addr) ? FreeStack() : Loaded(s);
    }

    // At a return: PR must be the caller's; and what the caller sees afterwards is recorded.
    bool Return(const State& s, const ghidra::VarnodeData& target)
    {
        const Value t = Get(s, target);
        if (!(t.kind == Value::Reg && t.v == kPr)) return false;
        const Value sp = Get(s, mReg[kR15]);
        if (!mSpSeen)
        {
            mSpSeen = true;
            if (sp.kind == Value::Sp) mSpDelta = sp.v;
            else mSpKnown = false;
        }
        else if (!(sp.kind == Value::Sp && sp.v == mSpDelta))
            mSpKnown = false;
        for (int r = 0; r < kTracked; ++r)
        {
            const Value v = Get(s, mReg[r]);
            if (r == 0) ReturnValue(s, v);
            const bool same = r == kR15 ? v.kind == Value::Sp && v.v == 0 : v.kind == Value::Reg && v.v == r;
            if (same) continue;
            mPreserved &= ~(1u << r);
            mOut |= v.Origins();
        }
        return true;
    }

    // The return register at a return (A3-5 to A3-62): what r0 holds is joined into one value for all the returns (mExit), as their paths would
    // meet at a common exit, with what the function computed it from (ReturnInput) and what decided the path here (s.ctl). See ReturnInputs.
    void ReturnValue(const State& s, const Value& v)
    {
        // A value this function put in r0 and held across a call that keeps it (A3-13): the engine's call killed it, so the return is dropped,
        // whatever the value is (a parameter, a constant, a sum of them). Compiled code leaves such a value only as scratch, which is withheld as
        // well (10 of image 007's and 22 of image 008's void functions, measured); the alternative is a signature that loses a return.
        if (v.carried) mCarriedInReturn = true;
        if (!mExitSeen) mExit = v;
        else Merge(mExit, v);
        mExitSeen = true;
        mReturnCtl |= s.ctl;
        const uint32_t d = ReturnInput(v);
        mReturnData |= d;
        if (v.Joined() && v.KeepsR0()) mReturnJoinsR0 |= d;
    }

    // What a value in r0 at a return was computed from by this function's own instructions, as inputs (kInputs). One thing is no input of it:
    // r0's entry value where no op read it, a path that leaves r0 alone (A3-14's scratch r0 of compiled code, `phi(in_r0, 5)`); an op that
    // computes from it is a read (`add #1,r0`, `rotcr r0`, `mov.b @(r0,r4),r0`). Nothing else is excused: a value the interpreter cannot model
    // carries its inputs' dependences and fails closed (A3-66 to A3-69, A3-76). A read of memory or the stack is modelled -- its dependence is
    // kMemory/kStack, which `kInputs` does not name -- so a word loaded through a pointer anywhere in the stack (06038F64/008) is still no
    // input, but r3 joined or summed with it is (its register bit survives the union).
    static uint32_t ReturnInput(const Value& v)
    {
        if (v.kind == Value::Reg && v.v == 0) return 0;
        uint32_t d = v.Direct();
        if (!v.UsesR0()) d &= ~1u;
        return d & kInputs;
    }
    // What an op's output depends on, by bit position, from its inputs' (Deps). Exact for what SLEIGH makes of a flag (`sr & ~1`, `sr & 1`, a shift
    // of SR to a flag's place, a truth value), and for the shifts and masks of other values; anything else depends on everything it reads.
    static Deps OpDeps(const Op& op, const std::vector<Value>& xs)
    {
        Deps all;
        for (const Value& x : xs) all |= x.DepsOf();
        auto constant = [&xs](size_t k, int64_t& c) {
            if (k >= xs.size() || xs[k].kind != Value::Imm) return false;
            c = xs[k].v;
            return true;
        };
        int64_t c = 0;
        switch (op.code)
        {
        case ghidra::CPUI_COPY:
        case ghidra::CPUI_INT_ZEXT:
        case ghidra::CPUI_INT_NEGATE:
        case ghidra::CPUI_INT_OR:
        case ghidra::CPUI_INT_XOR: return all;
        case ghidra::CPUI_INT_AND:
            if (xs.size() == 2 && constant(1, c)) return xs[0].DepsOf().Masked(static_cast<uint64_t>(c));
            if (xs.size() == 2 && constant(0, c)) return xs[1].DepsOf().Masked(static_cast<uint64_t>(c));
            return all;
        case ghidra::CPUI_INT_LEFT:
            if (xs.size() == 2 && constant(1, c)) return xs[0].DepsOf().Left(c);
            break;
        case ghidra::CPUI_INT_RIGHT:
            if (xs.size() == 2 && constant(1, c)) return xs[0].DepsOf().Right(c);
            break;
        case ghidra::CPUI_INT_SRIGHT:
            if (xs.size() == 2 && constant(1, c))
            {
                Deps d = xs[0].DepsOf().Right(c);
                const uint32_t sign = xs[0].DepsOf().At(8 * static_cast<int64_t>(op.in[0].size) - 1);
                for (uint32_t& x : d.b) x |= sign;
                return d;
            }
            break;
        case ghidra::CPUI_SUBPIECE:
            if (xs.size() == 2 && constant(1, c)) return xs[0].DepsOf().Right(8 * c);
            break;
        case ghidra::CPUI_INT_SEXT:
            if (!xs.empty())
            {
                Deps d = xs[0].DepsOf();
                const int64_t top = 8 * static_cast<int64_t>(op.in[0].size) - 1;
                const uint32_t sign = d.At(top);
                for (int k = 0; k < 4; ++k)
                    if (Deps::Pos(k) > top) d.b[k] = sign;
                d.b[4] |= sign;
                return d;
            }
            break;
        case ghidra::CPUI_INT_EQUAL:
        case ghidra::CPUI_INT_NOTEQUAL:
        case ghidra::CPUI_INT_LESS:
        case ghidra::CPUI_INT_SLESS:
        case ghidra::CPUI_INT_LESSEQUAL:
        case ghidra::CPUI_INT_SLESSEQUAL:
        case ghidra::CPUI_INT_CARRY:
        case ghidra::CPUI_INT_SCARRY:
        case ghidra::CPUI_INT_SBORROW:
        case ghidra::CPUI_BOOL_NEGATE:
        case ghidra::CPUI_BOOL_XOR:
        case ghidra::CPUI_BOOL_AND:
        case ghidra::CPUI_BOOL_OR: return all.Truth();
        case ghidra::CPUI_INT_MULT:
        case ghidra::CPUI_INT_2COMP: return Deps::Sum(all, Deps());
        default: break;
        }
        return Deps::All(all.Union());
    }

    // ...and of a condition (a branch's, or a jump table's index): the same, r0's entry value included (a test reads it), and failing closed in
    // the same way -- a hidden register compared with a word read from the stack still chooses (A3-67).
    static uint32_t ChooserInput(const Value& c)
    {
        return c.Direct() & kInputs;
    }

    // One instruction (with its delay slot, which the engine's p-code for a delayed branch holds):
    // its own p-code is a small graph of its own when it has internal branches.
    std::string Step(uint32_t a, const State& entry, std::vector<std::pair<uint32_t, State>>& succ)
    {
        Collect c;
        ghidra::int4 length = 0;
        try
        {
            length = mFd.getArch()->translate->oneInstruction(c, ghidra::Address(mRam, a));
        }
        catch (ghidra::LowlevelError&)   // bad data, memory not captured
        {
            return mTolerant ? std::string() : std::string("an instruction that does not decode");
        }
        const uint32_t next = a + static_cast<uint32_t>(length);
        const int n = static_cast<int>(c.ops.size());
        std::vector<State> at(n + 1);   // at[n]: falls off the end
        at[0] = entry;
        auto flow = [&](int to, const State& s) {
            if (to < 0 || to > n) return false;
            if (!at[to].reached) at[to] = s;
            else return Join(at[to], s);
            return true;
        };
        std::set<int> work{ 0 };
        size_t steps = 0;
        while (!work.empty())
        {
            const int i = *work.begin();
            work.erase(work.begin());
            if (i == n) continue;
            if (++steps > 4096) return "a return not proven to go to its caller (too many paths)";
            State s = at[i];
            const Op& op = c.ops[i];
            mDef = (static_cast<uint64_t>(a) << 12) | static_cast<uint64_t>(i + 1);   // what this op makes (Value::def)
            // r0 read by this op: a call's result in it is used (see Value::read). Not by a copy of r0 onto itself, which the spec makes of
            // r0 around a call (`bsr`'s delayed branch).
            if (!(op.code == ghidra::CPUI_COPY && op.hasOut && KeyOf(op.out) == KeyOf(op.in[0])))
            {
                const ghidra::VarnodeData& r0 = mReg[0];
                auto f = s.vars.find(KeyOf(r0));
                if (f != s.vars.end() && f->second.kind == Value::Fuzzy && !f->second.own && !f->second.read)
                    for (const ghidra::VarnodeData& vn : op.in)
                        if (vn.space == r0.space && vn.offset < r0.offset + r0.size && r0.offset < vn.offset + vn.size)
                        {
                            f->second.read = true;
                            break;
                        }
            }
            bool falls = true;
            switch (op.code)
            {
            case ghidra::CPUI_COPY: Set(s, op.out, Get(s, op.in[0])); break;
            // The zero- or sign-extension of a constant is a constant. SLEIGH writes `and #imm,r0` and `xor #imm,r0` as `r0 op zext(imm:1)`;
            // without this fold the ZEXT is a Fuzzy value, so OpDeps saw no mask on the `and` (the T flag the caller left was dropped with SR's
            // other bits, A3-69) and `xor #0,r0` was not the identity the table means it to be (A3-77).
            case ghidra::CPUI_INT_ZEXT:
            case ghidra::CPUI_INT_SEXT:
                if (op.hasOut && !op.in.empty())
                {
                    const Value x = Get(s, op.in[0]);
                    if (x.kind == Value::Imm)
                    {
                        const int inBits = 8 * static_cast<int>(op.in[0].size);
                        const uint64_t mask = inBits >= 64 ? ~0ull : ((1ull << inBits) - 1);
                        uint64_t val = static_cast<uint64_t>(x.v) & mask;
                        if (op.code == ghidra::CPUI_INT_SEXT && inBits < 64 && ((val >> (inBits - 1)) & 1u)) val |= ~mask;
                        Value r = Value::Const(op.out.size >= 8 ? static_cast<int64_t>(val) : Wrap(static_cast<int64_t>(val)));
                        r.carried = x.carried;   // a carried value extended is still carried (A3-68)
                        Set(s, op.out, r);
                    }
                    else
                    {
                        std::vector<Value> xs{ x };
                        Value r = Value::From(x.Origins(), OpDeps(op, xs));
                        r.carried = x.carried;
                        r.usesR0 = x.UsesR0();
                        r.joined = x.Joined();
                        Set(s, op.out, r);
                    }
                }
                break;
            case ghidra::CPUI_INT_ADD:
            case ghidra::CPUI_INT_SUB:
            {
                const Value x = Get(s, op.in[0]), y = Get(s, op.in[1]);
                const bool sub = op.code == ghidra::CPUI_INT_SUB;
                Value r = Value::From(x.Origins() | y.Origins(), Deps::Sum(x.DepsOf(), y.DepsOf()));
                r.carried = x.carried || y.carried;
                r.usesR0 = x.UsesR0() || y.UsesR0();
                r.joined = x.Joined() || y.Joined();
                if (x.kind == Value::Sp && y.kind == Value::Imm) r = Value::Stack(Wrap(sub ? x.v - y.v : x.v + y.v));
                else if (!sub && x.kind == Value::Imm && y.kind == Value::Sp) r = Value::Stack(Wrap(x.v + y.v));
                else if (x.kind == Value::Imm && y.kind == Value::Imm) r = Value::Const(sub ? x.v - y.v : x.v + y.v);
                else if (sub && KeyOf(op.in[0]) == KeyOf(op.in[1])) r = Value::Const(0);   // x - x
                // x + 0 and x - 0 are x (`add #0,r0` is a nop: A2-43)
                else if (y.kind == Value::Imm && y.v == 0) r = x;
                else if (!sub && x.kind == Value::Imm && x.v == 0) r = y;
                // An index added to a stack address stays in the stack (the engine's own stack analysis
                // takes pointer arithmetic so too): somewhere in it, not the index's memory. Where from is
                // kept (A3-9: `a[i] = f()` passes the address of an element in r2): the base address, with
                // a constant added to it later added to the base.
                else if (x.Base() != Value::kNoBase || (!sub && y.Base() != Value::kNoBase))
                {
                    // The sum is a stack address, but it still depends on the index added to it: a frame address plus a hidden register, or
                    // plus a loaded word, is a real input (A3-76). Keep the sum's dependences and flags; only the origin narrows to the stack.
                    r.from = origin::kStack;
                    r.stackBase = Value::kNoBase;
                    r.callerHigh = x.AtOrAboveEntry() || (!sub && y.AtOrAboveEntry());
                    const bool xb = x.Base() != Value::kNoBase, yb = y.Base() != Value::kNoBase;
                    // A constant moves the base; any other index may be negative (`p - i`, `p + i` for an i below zero reach the
                    // locals under the address it was made from: A3-17), so the base is given up for the frame's lowest slot.
                    if (xb && !yb)
                        r.stackBase = y.kind == Value::Imm ? Value::MoveBase(x.Base(), sub ? -y.v : y.v) : Value::kLowBase;
                    else if (!sub && yb && !xb)
                        r.stackBase = x.kind == Value::Imm ? Value::MoveBase(y.Base(), x.v) : Value::kLowBase;
                }
                // ...and, for a pointer that is a parameter register's entry value give or take an offset, track that offset (A3-71), unless the
                // value is a stack address (handled above).
                if (r.kind == Value::Fuzzy && r.stackBase == Value::kNoBase)
                {
                    int xr = -1, yr = -1;
                    int64_t xo = 0, yo = 0;
                    const bool xg = x.RegBase(xr, xo), yg = y.RegBase(yr, yo);
                    if (xg && !yg)
                    {
                        r.baseReg = static_cast<int8_t>(xr);
                        r.regOff = y.kind == Value::Imm ? Value::MoveRegConst(xo, sub ? -y.v : y.v) : Value::MoveRegVar(xo, !sub);
                    }
                    else if (!sub && yg && !xg)
                    {
                        r.baseReg = static_cast<int8_t>(yr);
                        r.regOff = x.kind == Value::Imm ? Value::MoveRegConst(yo, x.v) : Value::MoveRegVar(yo, true);
                    }
                }
                Set(s, op.out, r);
                break;
            }
            case ghidra::CPUI_LOAD:
            {
                const Value from = Get(s, op.in[1]);
                uint32_t word = 0;
                if (mLiterals != nullptr && from.kind == Value::Imm && op.out.size == 4 && mLiterals->count(static_cast<uint32_t>(from.v)) != 0 &&
                    mLiteralMemory->ReadU32(static_cast<uint32_t>(from.v), word))
                    Set(s, op.out, Value::Const(word));
                else
                {
                    Value l = Load(s, from, op.out.size);
                    // The address is read too: what `mov.b @(r0,r4),r0` loads depends on r0 and r4 (an index, a table's lookup).
                    if (l.kind == Value::Fuzzy && (from.kind == Value::Fuzzy || from.kind == Value::Reg))
                    {
                        l.dep |= Deps::All(from.Direct());
                        l.usesR0 = l.usesR0 || from.UsesR0();
                        l.joined = l.joined || from.Joined();
                    }
                    Set(s, op.out, l);
                }
                break;
            }
            case ghidra::CPUI_STORE: Store(s, Get(s, op.in[1]), op.in[2], Get(s, op.in[2])); break;
            case ghidra::CPUI_CALL:
            case ghidra::CPUI_CALLIND:
                {
                    // `jsr @r0`: the target is a copy of r0 made at the start of the instruction (the delay slot may change r0).
                    // ...and only while r0 still holds what was copied: a value the delay slot puts in r0 is no call target (A3-18).
                    // Identity, not equality of values: two loads are equal Values (`jsr @r0 ; mov.l @r5,r0` put another load in r0 and
                    // compared equal to the target: A3-38). The target is the copy while no op of the instruction has written r0 since.
                    bool viaR0 = false;
                    if (op.code == ghidra::CPUI_CALLIND)
                        for (int k = 0; k < i; ++k)
                            if (c.ops[k].code == ghidra::CPUI_COPY && c.ops[k].hasOut && KeyOf(c.ops[k].out) == KeyOf(op.in[0]) &&
                                KeyOf(c.ops[k].in[0]) == KeyOf(mReg[0]))
                            {
                                bool rewritten = false;
                                for (int j = k + 1; j < i && !rewritten; ++j)
                                {
                                    const ghidra::VarnodeData& w = c.ops[j].out;
                                    rewritten = c.ops[j].hasOut && w.space == mReg[0].space && w.offset < mReg[0].offset + mReg[0].size &&
                                                mReg[0].offset < w.offset + w.size;
                                }
                                viaR0 = !rewritten;
                            }
                    // A target the code loaded from a literal word (the probe): the callee the engine will find once it folds the load.
                    bool literal = false;
                    uint32_t target = 0;
                    if (op.code == ghidra::CPUI_CALLIND && mLiterals != nullptr)
                    {
                        const Value t = Get(s, op.in[0]);
                        if (t.kind == Value::Imm)
                        {
                            literal = true;
                            target = static_cast<uint32_t>(t.v);
                        }
                    }
                    Call(s, a, viaR0, literal, target);
                }
                if (mNoReturn.count(a)) continue;   // the path ends in the callee
                break;
            case ghidra::CPUI_CALLOTHER:
                // sleep (Sleep_Standby) changes no register or memory, nor do the control register writes (Write_SR,
                // Write_GBR, Write_VBR: the register itself is written by the instruction's own p-code, the user
                // op only keeps the write in the code); any other user op, anything.
                {
                    const std::string& userOp = mFd.getArch()->userops.getOp(static_cast<ghidra::uint4>(op.in[0].offset))->getName();
                    // `ldc Rm,sr` is `value = Rm & 0x3F3 ; sr = value ; Write_SR(value)`: remember the op that made Rm's value (the idiom's
                    // exception above is for r0 holding exactly that value at the returns).
                    if (userOp == "Write_SR" && op.in.size() > 1)
                        for (int k = 0; k < i; ++k)
                            if (c.ops[k].hasOut && KeyOf(c.ops[k].out) == KeyOf(op.in[1]) && c.ops[k].code == ghidra::CPUI_INT_AND && !c.ops[k].in.empty())
                            {
                                const Value rm = Get(s, c.ops[k].in[0]);
                                if (rm.kind == Value::Fuzzy && rm.def != 0 && rm.def != Value::kJoin) mSrWritten.insert(rm.def);
                            }
                    if (userOp != "Sleep_Standby" && userOp != "Write_SR" && userOp != "Write_GBR" && userOp != "Write_VBR")
                        Apply(s, CallEffects());
                }
                if (op.hasOut) Set(s, op.out, Value());
                break;
            case ghidra::CPUI_BRANCH:
            case ghidra::CPUI_CBRANCH:
                // The condition of a branch is made of what this function computed it from (Direct leaves out what a call returned: its outputs
                // share every register's origin): the inputs among them decide which way the code goes (A3-55), whatever they are (ChooserInput).
                if (op.code == ghidra::CPUI_CBRANCH && op.in.size() > 1) s.ctl |= ChooserInput(Get(s, op.in[1]));
                if (op.in[0].space->getType() == ghidra::IPTR_CONSTANT)
                {
                    const int to = i + static_cast<int>(static_cast<int64_t>(op.in[0].offset));
                    if (flow(to, s)) work.insert(to);
                }
                else
                    succ.emplace_back(static_cast<uint32_t>(op.in[0].offset), s);
                falls = op.code == ghidra::CPUI_CBRANCH;
                break;
            case ghidra::CPUI_BRANCHIND:
            {
                auto table = mTables.find(a);
                const bool found = table != mTables.end() && !table->second.empty();
                // ...and a jump table's index: the target is loaded through it.
                if (found) s.ctl |= ChooserInput(Get(s, op.in[0]));
                if (found)
                    for (uint32_t to : table->second) succ.emplace_back(to, s);
                // A tail call: the engine made the jump a call and a return (its flow says so, and the call specs have it). The
                // callee's effects apply as at any call -- a value in r0 it keeps is carried across, A3-27 -- and its rts goes
                // to our caller, so the path ends here with PR as it was, if it was the caller's.
                if (!found && mCallAt.count(a) != 0)
                {
                    const Value pr = Get(s, mReg[kPr]);
                    if (!(pr.kind == Value::Reg && pr.v == kPr))
                    {
                        if (!mTolerant) return "a return not to its caller";
                        falls = false;
                        break;
                    }
                    // (The engine used to print the jump as `f(); return;`, a call whose value nothing returned, so a caller of a callee that
                    // returns one was withheld; since patch 0044 a RETURN of a locked returning call's output is a return, the text states the
                    // callee's value and the caller's signature returns it: A3-29. A callee lent `void` has no value to lose: r0 after it is
                    // the scratch of a void function, and a reader of this function's r0 is withheld by "uses a register after calling".)
                    Call(s, a);
                    if (mNoReturn.count(a)) continue;
                    Set(s, mReg[kPr], pr);
                    Return(s, mReg[kPr]);
                    falls = false;
                    break;
                }
                // jmp @rN with rN holding the caller's PR is a return like rts (the engine takes a jump
                // table it cannot recover that way too, when the target is the return address).
                if (!found)
                {
                    const bool isReturn = Return(s, op.in[0]);
                    if (!isReturn && !mTolerant) return "an indirect jump it does not resolve";
                    // A3-70: in a nested callee walk a tail jump the walk cannot resolve (a thunk `jmp @r1` to the function that actually
                    // writes the frame) may reach any writer of this function's caller's frame. Fail closed.
                    if (!isReturn && mNested)
                    {
                        mOwnFrameWords |= CallEffects::kFrameAny;
                        mCallerFrameStores |= origin::kFrame;
                    }
                }
                falls = false;
                break;
            }
            case ghidra::CPUI_RETURN:
                if (!Return(s, op.in[0]) && !mTolerant) return "a return not to its caller";
                falls = false;
                break;
            default:
                if (op.hasOut)
                {
                    uint32_t o = 0;
                    bool carried = false, usesR0 = false, joined = false;
                    std::vector<Value> xs;
                    for (const ghidra::VarnodeData& vn : op.in)
                    {
                        const Value x = Get(s, vn);
                        o |= x.Origins();
                        carried = carried || x.carried;
                        usesR0 = usesR0 || x.UsesR0();
                        joined = joined || x.Joined();
                        xs.push_back(x);
                    }
                    Value r = Value::From(o, OpDeps(op, xs));
                    r.carried = carried;
                    r.usesR0 = usesR0;
                    r.joined = joined;
                    // A bitwise op of two constants is a constant, so SLEIGH's `imm_00_07 & 0x000000FF` (the masked immediate of `xor #imm,r0`,
                    // whose `#0` the identity table means to be a nop) folds and the identity check below sees a zero (A3-77).
                    if (xs.size() == 2 && xs[0].kind == Value::Imm && xs[1].kind == Value::Imm &&
                        (op.code == ghidra::CPUI_INT_AND || op.code == ghidra::CPUI_INT_OR || op.code == ghidra::CPUI_INT_XOR))
                    {
                        const uint64_t a0 = static_cast<uint64_t>(xs[0].v), a1 = static_cast<uint64_t>(xs[1].v);
                        const uint64_t rv = op.code == ghidra::CPUI_INT_AND ? a0 & a1 : op.code == ghidra::CPUI_INT_OR ? a0 | a1 : a0 ^ a1;
                        r = Value::Const(op.out.size >= 8 ? static_cast<int64_t>(rv) : Wrap(static_cast<int64_t>(rv)));
                    }
                    // `xor r0,r0` is how compiled code zeroes a register: 0, computed from nothing (the engine folds it so). Taken for a value
                    // computed from r0's entry value it would withhold the function for a return that reads no input (A2-31: 06073C7C).
                    else if (op.code == ghidra::CPUI_INT_XOR && op.in.size() == 2 && KeyOf(op.in[0]) == KeyOf(op.in[1])) r = Value::Const(0);
                    // ...and `and r0,r0`, `or r0,r0`, `or #0,r0`, `xor #0,r0` leave it as it was: nops, as the engine takes them (A2-43, patch 0048).
                    else if ((op.code == ghidra::CPUI_INT_AND || op.code == ghidra::CPUI_INT_OR) && xs.size() == 2 && KeyOf(op.in[0]) == KeyOf(op.in[1]))
                        r = xs[0];
                    else if ((op.code == ghidra::CPUI_INT_OR || op.code == ghidra::CPUI_INT_XOR) && xs.size() == 2 && xs[1].kind == Value::Imm && xs[1].v == 0)
                        r = xs[0];
                    Set(s, op.out, r);
                }
                break;
            }
            if (falls && flow(i + 1, s)) work.insert(i + 1);
        }
        if (at[n].reached) succ.emplace_back(next, at[n]);
        return std::string();
    }


    ghidra::Funcdata& mFd;
    uint32_t mExec;
    const std::map<uint32_t, Prototype>& mLent;
    const JumpDests& mTables;
    ghidra::AddrSpace* mRam = nullptr;
    ghidra::VarnodeData mReg[kTracked];
    std::map<uint32_t, uint32_t> mCallAt;   // call instruction -> the address it calls
    std::set<uint32_t> mNoReturn;            // call instructions the engine knows do not return
    const std::set<uint32_t>* mLiterals = nullptr;   // the probe's literal-pool words, and where to read them (SetLiterals)
    const MemorySnapshot* mLiteralMemory = nullptr;
    uint32_t mPreserved = 0, mStoresThrough = 0, mStoresBelow = 0, mOut = 0, mLeaks = 0, mFrameValues = 0;
    std::atomic<bool>* mCancel = nullptr;   // A3-74: checked in Run
    std::set<uint64_t> mSrWritten;          // the ops whose value `ldc Rm,sr` wrote back (Inputs: the interrupt-mask idiom)
    bool mSpSeen = false, mSpKnown = true;   // r15 at the returns reached: one constant offset from its entry value, or not
    int64_t mSpDelta = 0;
    bool mCarriedInReturn = false;
    Value mExit;                  // r0 at every return reached, joined (ReturnValue)
    bool mExitSeen = false;
    uint32_t mReturnData = 0, mReturnCtl = 0, mReturnJoinsR0 = 0;   // ...what it was computed from, what chose the paths, of which joined with r0
    uint64_t mDef = 0;            // the op being interpreted (Value::def)
    uint32_t mCallerFrameStores = 0;   // what was stored at an exact offset at or above the entry r15 (A3-21)
    uint32_t mFrameWords = 0;          // ...and which words of it (CallEffects::frameWords)
    bool mTolerant = false;
    bool mNested = false;              // a walk of a callee's code for CalleeFrameWrites, which asks no further
    size_t mMaxSteps = kMaxSteps;
    uint32_t mOwnFrameWords = 0;       // the words of the caller's frame this function's own stores write (CallEffects::frameWords' bits)
    std::map<uint32_t, uint32_t> mCalleeFrameWrites;   // CalleeFrameWrites, by target
    std::map<uint32_t, uint32_t> mEscapeSites;   // the calls that escape was declared at because of r2 or r7, and which
    bool mEscaped = false;   // r2 held a frame address at a call that may read it
    bool mEscapedLow = false;   // ...of which one with no lower bound than the frame's lowest slot
    int64_t mEscapeLow = 0;     // ...and the lowest exact base (when mEscapedLow, with mLowestSp)
    int64_t mLowestSp = 0;      // the lowest stack offset any register or slot held

};

// How the entry value of SR, GBR or VBR is used, for WithholdReason. Since patch 0034 an `ldc` into one of them stays in the
// code as a call of a user op (Write_SR, Write_GBR, Write_VBR), and with it the read of whatever the register was written
// from. A function that saves the register and gives it back (`stc gbr,r1 ; ldc r4,gbr ; ... ; ldc r1,gbr`), or changes some of
// its bits and keeps the rest (the interrupt mask: `stc sr,r1 ; ... ; or #0xF0,r0 ; ldc r0,sr ; ... ; ldc r1,sr`) reads the
// entry value only to write it back: it is the machine's state, which no caller passes, and not an input. One whose behaviour
// depends on it is: a branch, a compare, a load or a call argument computed from it; a value of it returned or stored (but
// the whole of SR, or the whole with bits set, `stc sr,r0 ; or #0xF0,r0`, is the helper's idiom of A3-5 and stays lent).
//   `ok`: every path the value takes ends in the write back, or is a whole copy (SR only: returned, or with bits
//   set by an or with a constant; a store is an input, see below). `transformed`: it went through an operation, so it is not a plain copy. `written`: it
//   reached a write back. A transformed value that reaches no write back is somebody's flags, and is an input.
struct StateUse
{
    bool ok = true, transformed = false, written = false;
};

// A whole SR stored through a pointer is an input (A2-23, A2-35): the memory outlives the function. The engine does not forward a store
// through a pointer to a load that follows it in the same function, so the reload is no descendant of the entry value (a function that
// stores SR, loads it back and branches on T was lent without SR); and a caller reads the word back after the call (`stc sr,r1 ;
// mov.l r1,@r4` called under a compare, then `mov.l @r4,r0 ; and #1,r0`), so its compare is the callee's input and was dropped.
StateUse EntryStateUse(const ghidra::Architecture& arch, const ghidra::Varnode* entry, const char* writeOp, bool wholeOk)
{
    StateUse r;
    std::set<std::pair<const ghidra::Varnode*, bool>> seen;   // (value, whether it is no longer the whole register)
    std::vector<std::pair<const ghidra::Varnode*, bool>> work;
    work.emplace_back(entry, false);
    while (!work.empty() && r.ok)
    {
        const ghidra::Varnode* vn = work.back().first;
        const bool part = work.back().second;
        work.pop_back();
        if (!seen.insert(std::make_pair(vn, part)).second) continue;
        if (seen.size() > 512)   // a value that spreads this far is computed from
        {
            r.ok = false;
            break;
        }
        for (auto use = vn->beginDescend(); use != vn->endDescend() && r.ok; ++use)
        {
            const ghidra::PcodeOp* op = *use;
            const ghidra::Varnode* out = op->getOut();
            switch (op->code())
            {
            case ghidra::CPUI_COPY:
            case ghidra::CPUI_MULTIEQUAL:
                if (out != nullptr) work.emplace_back(out, part);
                break;
            case ghidra::CPUI_RETURN: r.ok = wholeOk && !part; break;
            case ghidra::CPUI_STORE: r.ok = false; break;
            case ghidra::CPUI_CALLOTHER:
                if (arch.userops.getOp(static_cast<ghidra::uint4>(op->getIn(0)->getOffset()))->getName() == writeOp) r.written = true;
                else r.ok = false;
                break;
            case ghidra::CPUI_CBRANCH:
            case ghidra::CPUI_BRANCHIND:
            case ghidra::CPUI_CALL:
            case ghidra::CPUI_CALLIND:
            case ghidra::CPUI_LOAD:
            case ghidra::CPUI_INDIRECT: r.ok = false; break;
            default:
                if (out == nullptr)
                {
                    r.ok = false;
                    break;
                }
                // Bits set in the whole value are still the whole value, and may be returned; anything else is a field of it.
                r.transformed = true;
                if (op->code() == ghidra::CPUI_INT_OR && op->getIn(1 - op->getSlot(vn))->isConstant()) work.emplace_back(out, part);
                else work.emplace_back(out, true);
                break;
            }
        }
    }
    return r;
}

// The user op that writes the control register 'reg' (patch 0034), or null for any other register.
const char* StateWriteOp(const std::string& reg)
{
    return reg == "sr" ? "Write_SR" : reg == "gbr" ? "Write_GBR" : reg == "vbr" ? "Write_VBR" : nullptr;
}

// The inputs the signature of 'fd' holds, as ReturnCheck's bits: r4-r7 (bit n: Rn) and the stack arguments (ReturnCheck::ArgDep of each word).
uint32_t HeldInputs(const ghidra::Funcdata& fd)
{
    const ghidra::FuncProto& fp = fd.getFuncProto();
    const ghidra::Translate* translate = fd.getArch()->translate;
    uint32_t held = 0;
    for (ghidra::int4 p = 0; p < fp.numParams(); ++p)
    {
        const ghidra::ProtoParameter* param = fp.getParam(p);
        const ghidra::Address a = param->getAddress();
        const int64_t size = param->getSize();
        if (a.getSpace()->getType() == ghidra::IPTR_SPACEBASE)
        {
            const int64_t off = static_cast<int32_t>(static_cast<uint32_t>(a.getOffset()));
            for (int64_t w = off & ~int64_t(3); w < off + size; w += 4) held |= ReturnCheck::ArgDep(w);
            continue;
        }
        for (int r = 4; r < 8; ++r)
        {
            const ghidra::VarnodeData reg = translate->getRegister("r" + std::to_string(r));
            if (a.getSpace() == reg.space && a.getOffset() < reg.offset + reg.size && reg.offset < a.getOffset() + static_cast<ghidra::uintb>(size))
                held |= 1u << r;
        }
    }
    return held;
}

// Why the signature recovered for 'fd' cannot be lent to its callers, or empty when it can. A lent
// signature is locked: the caller's call shows exactly those arguments and that return, and
// nothing later revisits it. So it is lent only when the recovery saw everything that decides it.
std::string WithholdReason(ghidra::Funcdata& fd, const std::map<uint32_t, Prototype>& lent, uint32_t exec,
                           uint32_t entry, const JumpDests& tables, CallEffects& effects,
                           const std::set<uint32_t>* literals = nullptr, const MemorySnapshot* literalMemory = nullptr)
{
    if (fd.hasBadData()) return "bad instruction data";
    const ghidra::FuncProto& fp = fd.getFuncProto();
    if (fp.isDotdotdot()) return "variadic";
    ghidra::AddrSpace* ram = fd.getArch()->getDefaultCodeSpace();
    // A call whose callee's signature is unknown leaves the engine guessing what it consumes: an
    // argument forwarded to it unchanged can be taken for unused. Recursion is the same guess.
    // (An unrecovered jump is turned into an indirect call, so it lands here too.)
    for (ghidra::int4 i = 0; i < fd.numCalls(); ++i)
    {
        const ghidra::FuncCallSpecs* cs = fd.getCallSpecs(i);
        const ghidra::Address at = cs->getEntryAddress();
        if (at.isInvalid() || at.getSpace() != ram) return "an indirect call it does not resolve";
        const uint32_t t = static_cast<uint32_t>(at.getOffset());
        uint32_t c = 0;
        if (t == entry || (CanonicalAddress(t, c) && c == entry)) return "it calls itself";   // through any mirror
        if (lent.count(t) == 0) return "it calls " + Hex8(t) + ", whose signature is not known";
    }
    // A register read after a call that the callee's signature does not return (the engine prints
    // it as extraout_rN): the callee's signature is its own analysis -- void when it found no value
    // returned, which may be a return it could not see -- so what this function does with that
    // register is not described by anything, and its own signature is not lent on it (review of
    // 0e3cd12: a caller returning P() + 1 under a void P was lent as known).
    for (auto it = fd.beginLoc(); it != fd.endLoc(); ++it)
    {
        const ghidra::Varnode* vn = *it;
        if (!vn->isExtraOut() || vn->hasNoDescend() || !vn->isWritten()) continue;
        const ghidra::PcodeOp* ind = vn->getDef();
        const ghidra::PcodeOp* call = ind->code() == ghidra::CPUI_INDIRECT && ind->getIn(1)->getSpace()->getType() == ghidra::IPTR_IOP
                                          ? ghidra::PcodeOp::getOpFromConst(ind->getIn(1)->getAddr())
                                          : nullptr;
        if (!call || (call->code() != ghidra::CPUI_CALL && call->code() != ghidra::CPUI_CALLIND)) continue;   // a user op's effect (sleep) is not a call's
        // What a call left in SR, GBR or VBR that goes on only into a write of the same register (the interrupt mask raised
        // around a call and put back: `Write_SR(extraout_sr & 0xf0 | ...)`) is the machine's state again, not a value the
        // callee returns: see EntryStateUse.
        {
            const char* writeOp = vn->getSize() == 4 ? StateWriteOp(fd.getArch()->translate->getRegisterName(vn->getSpace(), vn->getOffset(), 4)) : nullptr;
            if (writeOp != nullptr)
            {
                const StateUse use = EntryStateUse(*fd.getArch(), vn, writeOp, false);
                if (use.ok && use.written) continue;
            }
        }
        std::string callee = "a call";
        {
            const ghidra::FuncCallSpecs* cs = fd.getCallSpecs(const_cast<ghidra::PcodeOp*>(call));
            if (cs && !cs->getEntryAddress().isInvalid()) callee = Hex8(static_cast<uint32_t>(cs->getEntryAddress().getOffset()));
        }
        const std::string reg = fd.getArch()->translate->getRegisterName(vn->getSpace(), vn->getOffset(), vn->getSize());
        return "it uses " + (reg.empty() ? std::string("a register") : reg) + " after calling " + callee +
               ", which is not known to return it";
    }
    // An incoming argument the signature does not hold: the fifth one read with r4-r7 unused is
    // left a stack local, and a signature without it loses it at every call.
    for (auto it = fd.beginDef(ghidra::Varnode::input); it != fd.endDef(ghidra::Varnode::input); ++it)
    {
        const ghidra::Varnode* vn = *it;
        if (!fp.possibleInputParam(vn->getAddr(), vn->getSize())) continue;
        bool held = false;
        for (ghidra::int4 p = 0; p < fp.numParams() && !held; ++p)
        {
            const ghidra::ProtoParameter* param = fp.getParam(p);
            const ghidra::Address a = param->getAddress();
            held = a.getSpace() == vn->getSpace() && vn->getOffset() >= a.getOffset() &&
                   vn->getOffset() + vn->getSize() <= a.getOffset() + param->getSize();
        }
        if (!held) return "an incoming argument outside its signature";
    }
    // ...and past the model's parameter window. The default prototype's stack entry is 500 bytes (superh.cspec), so a read of the caller's
    // frame at or above r15 + 500 is no possibleInputParam and the loop above misses it; a non-void signature then returns it as in_stack_NNN
    // with no parameter for it (A3-73). Any input in the stack space at or above the entry r15 that no parameter holds is an argument the
    // signature leaves out, whatever the window.
    for (auto it = fd.beginDef(ghidra::Varnode::input); it != fd.endDef(ghidra::Varnode::input); ++it)
    {
        const ghidra::Varnode* vn = *it;
        if (vn->getSpace()->getType() != ghidra::IPTR_SPACEBASE) continue;
        if (static_cast<int32_t>(static_cast<uint32_t>(vn->getOffset())) < 0) continue;   // a local, not an incoming argument
        bool held = false;
        for (ghidra::int4 p = 0; p < fp.numParams() && !held; ++p)
        {
            const ghidra::ProtoParameter* param = fp.getParam(p);
            const ghidra::Address a = param->getAddress();
            held = a.getSpace() == vn->getSpace() && vn->getOffset() >= a.getOffset() &&
                   vn->getOffset() + vn->getSize() <= a.getOffset() + param->getSize();
        }
        if (!held) return "an incoming argument outside its signature";
    }
    // A register outside r4-r7 and the stack that the function reads before writing (r0-r3, r8-r14, GBR,
    // MACH, MACL, T): its value is an input no signature can pass, so a caller's setup of it is dead in
    // the caller's text and the call shows without it (A3-5). The calling convention of hand-written
    // code, not of the compiler's: compiled code reads such a register only to save it, and that read
    // is gone by now. r15 and PR are the stack and the return address.
    {
        const ghidra::Translate* translate = fd.getArch()->translate;
        for (auto it = fd.beginDef(ghidra::Varnode::input); it != fd.endDef(ghidra::Varnode::input); ++it)
        {
            const ghidra::Varnode* vn = *it;
            if (fp.possibleInputParam(vn->getAddr(), vn->getSize()) || vn->getSpace()->getName() != "register") continue;
            std::string reg = translate->getRegisterName(vn->getSpace(), vn->getOffset(), vn->getSize());
            if (reg == "r15" || reg == "pr") continue;
            // The status register as it is -- returned or stored whole (`stc sr,r0 ; rts`, the idiom of an
            // interrupt-mask helper) -- is the machine's state, which nothing passes and nothing here
            // describes wrongly. Anything computed from it (the T flag: movt, bt, addc, ...) is an input.
            if (vn->getSize() == 4 && StateWriteOp(reg) != nullptr)
            {
                const StateUse use = EntryStateUse(*fd.getArch(), vn, StateWriteOp(reg), reg == "sr");
                if (use.ok && (reg == "sr" ? !use.transformed || use.written : use.written)) continue;
            }
            return "it reads " + (reg.empty() ? std::string("a register") : reg) + " on entry, which no signature passes";
        }
    }
    // A return that does not go back to the caller (PR replaced, or saved after a call changed it)
    // is a jump somewhere else, not a return to the call site the signature describes.
    ReturnCheck check(fd, exec, lent, tables);
    // Fold the literal-pool loads the probe folds, so an ordinary stack access whose offset is a pool word (`mov.w =20,r2 ; add r15,r2 ;
    // mov.l @r2,r0`) is seen as the stack argument it is, not a "nothing-known" address (A3-66 c, A3-67 b).
    if (literals != nullptr && literalMemory != nullptr) check.SetLiterals(*literals, *literalMemory);
    const std::string why = check.Run(effects);
    if (!why.empty()) return why;
    // The return rule (A3-5, A3-13, A3-14, A3-20, A3-45, A3-49, A3-50, A3-55 to A3-59, A3-62). A function the engine made `void` is lent only
    // when no input the signature does not hold can change what it leaves in r0: neither by data (what r0 at the returns was computed from)
    // nor by choice (what decided which value it holds, when it differs by path). The engine drops a return it takes for none (A3-5: a value
    // with another use than the return, or made of inputs no signature passes), and every read whose only use was that value goes with it: the
    // input leaves the text and the signature, and a caller's setup of it is dead in the caller's text. A value made only of what the signature
    // holds, of memory, of constants and of what calls returned loses nothing a caller can see: a caller that reads r0 afterwards is withheld
    // ("uses r0 after calling"). The inputs are ReturnCheck::Inputs's, with the exceptions it lists.
    if (fp.getOutputType()->getMetatype() == ghidra::TYPE_VOID)
    {
        static const char* const kNames[] = { "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
                                              "r11", "r12", "r13", "r14", "r15", "sr", "gbr", "vbr", "mach", "macl", "pr", "the T flag" };
        auto name = [](int bit) {
            if (bit < 23) return std::string(kNames[bit]) + " on entry";
            const int word = bit - 27;
            return word < 4 ? "a stack argument (the word at r15 + " + std::to_string(4 * word) + " on entry)"
                            : std::string("a stack argument (a word at r15 + 16 or above on entry)");
        };
        const uint32_t held = HeldInputs(fd);
        const ReturnCheck::ReturnInputs in = check.Inputs();
        const uint32_t notPassed = ReturnCheck::kInputs & ~(0xF0u | ReturnCheck::kDepArgs);   // by any signature
        // r0's entry value joined with a parameter the signature does not hold (A3-20, A3-49): `if (b) return a;`.
        if ((in.joinsR0 & 0xF0u & ~held) != 0)
            return "its return joins r0's entry value with a parameter it does not hold, which the engine takes for no return";
        for (int bit = 0; bit < 32; ++bit)
            if (((in.data & notPassed) >> bit) & 1u) return "its return is computed from " + name(bit) + ", which no signature passes";
        if (check.CarriedValueInReturn()) return "it holds a value in r0 across a call that keeps r0, which the engine takes for no return";
        for (int bit = 0; bit < 32; ++bit)
            if (((in.chosen & notPassed) >> bit) & 1u) return "its return is chosen by " + name(bit) + ", which no signature passes";
        for (int bit = 0; bit < 32; ++bit)
            if (((in.data & ~notPassed & ~held) >> bit) & 1u) return "its return is computed from " + name(bit) + ", which the signature does not hold";
        for (int bit = 0; bit < 32; ++bit)
            if (((in.chosen & ~notPassed & ~held) >> bit) & 1u) return "its return is chosen by " + name(bit) + ", which the signature does not hold";
    }
    // A word written at or above the entry r15 is a value handed to the caller through its own frame (`rts ; mov.l r5,@r15`, a
    // pop and a push of another word, `mov.l r5,@(4,r15)`): the signature has registers only, and the engine takes the store for
    // a write into the callee's dead frame, so the register stored is no input and the caller that reads the word back is
    // shown what it wrote before the call (A3-21; A3-15 is the same through a lowered r15).
    // ...unless what it writes is a parameter the signature holds, which is how a callee spills its argument registers into the
    // home area the caller reserved (one function of image 007 does): no caller reads that back, and a store of a register
    // the signature does not hold (the dead store of the engine's) is the value handed back.
    if (const uint32_t stored = check.CallerFrameStores())
    {
        uint32_t held = 0;
        for (ghidra::int4 p = 0; p < fp.numParams() && p < 4; ++p) held |= 1u << (4 + p);
        if ((stored & ~held) != 0) return "it writes a word of its caller's frame (a value handed back through the stack)";
    }
    // r15 is not where it was on entry: the engine applies a constant difference as the call's extrapop
    // (LendEffects); one that varies between returns, or is not a constant, cannot be lent.
    if (!((effects.preserved >> 15) & 1u) && effects.spDelta == CallEffects::kSpUnknown)
        return "it leaves r15 changed by an amount that is not the same at every return";
    // ...and a constant difference below where it began is words the callee pushed and left (`rts ; mov.l r5,@-r15`): a
    // value handed to its caller through the stack. The signature has registers only, and the engine takes the callee's
    // store for a write into its own frame, so the register stored is no input and the caller, which pops the word,
    // reads a local nothing wrote (A3-15). No compiler leaves a push, and neither corpus image lends one: withheld, and
    // so is the caller. (A callee that pops its caller's words, a positive difference, reads them: that is an incoming
    // stack argument, which is withheld above.)
    if (!((effects.preserved >> 15) & 1u) && effects.spDelta < 0)
        return "it leaves r15 lower than it found it (words pushed for its caller)";
    return std::string();
}

// What one pass learned from a function's raw p-code, before any action has run.
struct RawScan
{
    std::set<uint32_t> literalWords;   // start of each literal-pool load marked read-only
    uint32_t literalLoads = 0;
    std::vector<std::pair<uint32_t, int>> readonly;   // (address, size) ranges to mark
    bool unsupported = false;
    uint32_t unsupportedAt = 0;
    std::set<uint32_t> insns;   // every instruction with p-code after the first flow, before any action removes some
};

// Literal pools and A3. SLEIGH emits "tmp = #addr; Rn = *(ram, tmp)" for mov.w/mov.l
// @(disp,PC), and raw-stage varnodes have no def links yet, so constants are tracked per
// instruction: fold any op whose inputs are known constants, and a LOAD through a known
// constant pointer is reading a literal-pool word. (Only the PC-relative forms produce a
// constant pointer inside one instruction; a load through a register never does.)
RawScan ScanRawPcode(ghidra::Funcdata& fd, const MemorySnapshot& snap)
{
    RawScan scan;

    // A3: every instruction the flow reached, decoded on our side. A delayed branch followed by a PC-relative load or mova hands the
    // load the PC of the branch's target when the branch is taken. The spec gives the slot that PC for every delayed branch (patch
    // 0005 for bra and bsr, whose target is in the instruction; patch 0050 for the rest: bt/s and bf/s by T, jmp, jsr and braf and bsrf
    // by their register, rts by PR, rte by the stacked PC), through the context of the slot's address.
    std::set<uint32_t>& insns = scan.insns;
    for (auto it = fd.beginOpMain(); it != fd.endOpMain(); ++it)
    {
        const uint32_t a = static_cast<uint32_t>(it->second->getAddr().getOffset());
        insns.insert(a);
    }
    // The context reaches the slot by its address, so a slot that anything in the function also
    // jumps straight to would get the branch's PC on that path too. Such code is flagged rather than
    // trusted. Jumps come from immediate branches and from the jump tables flow recovered (a
    // switch case may enter a slot as well as an ordinary branch can).
    std::set<uint32_t> targets;
    for (uint32_t a : insns)
    {
        uint16_t op = 0;
        if (!snap.ReadU16(a, op)) continue;
        const DisassembledInstruction ins = Sh2Decode(a, op);
        if (ins.HasBranchTarget) targets.insert(ins.BranchTarget);
    }
    for (ghidra::int4 i = 0; i < fd.numJumpTables(); ++i)
    {
        const ghidra::JumpTable* table = fd.getJumpTable(i);
        for (ghidra::int4 j = 0; j < table->numEntries(); ++j)
            targets.insert(static_cast<uint32_t>(table->getAddressByIndex(j).getOffset()));
    }
    std::vector<std::pair<uint32_t, uint32_t>> excluded;   // [lo, hi) literal addresses not to fold
    for (uint32_t a : insns)
    {
        uint16_t op = 0;
        if (!snap.ReadU16(a, op)) continue;
        const DisassembledInstruction branch = Sh2Decode(a, op);
        if (!branch.HasDelaySlot) continue;
        uint16_t slotOp = 0;
        if (!snap.ReadU16(a + 2, slotOp)) continue;
        const DisassembledInstruction slot = Sh2DecodeAfterBranch(a + 2, slotOp, branch);
        if (slot.PcRel == Sh2PcRel::None) continue;
        if (targets.count(a + 2) == 0) continue;
        if (!scan.unsupported || a + 2 < scan.unsupportedAt)
        {
            scan.unsupported = true;
            scan.unsupportedAt = a + 2;
        }
        // Neither candidate folds: the spec computes the address the branch's PC gives the slot, and the slot's own otherwise,
        // and either is wrong on one of the paths.
        const uint32_t size = slot.PcRel == Sh2PcRel::Word ? 2 : 4;
        excluded.emplace_back(slot.PcRelDirectAddress, slot.PcRelDirectAddress + size);
        if (slot.PcRelHasSlotAddress)
            excluded.emplace_back(slot.PcRelSlotAddress, slot.PcRelSlotAddress + size);
    }
    auto isExcluded = [&](uint32_t lo, uint32_t hi) {
        for (const auto& e : excluded)
            if (lo < e.second && e.first < hi) return true;
        return false;
    };

    std::map<std::pair<ghidra::uintb, ghidra::int4>, ghidra::uintb> consts;
    ghidra::Address cur;
    for (auto it = fd.beginOpMain(); it != fd.endOpMain(); ++it)
    {
        const ghidra::PcodeOp* op = it->second;
        if (op->getAddr() != cur)
        {
            cur = op->getAddr();
            consts.clear();
        }
        auto valueOf = [&](const ghidra::Varnode* vn, ghidra::uintb& v) {
            if (vn->isConstant())
            {
                v = vn->getOffset();
                return true;
            }
            if (vn->getSpace()->getType() != ghidra::IPTR_INTERNAL) return false;
            auto f = consts.find(std::make_pair(vn->getOffset(), vn->getSize()));
            if (f == consts.end()) return false;
            v = f->second;
            return true;
        };
        ghidra::uintb a = 0, b = 0;
        const ghidra::OpCode code = op->code();
        if (code == ghidra::CPUI_LOAD)
        {
            if (valueOf(op->getIn(1), a))
            {
                const uint32_t lo = static_cast<uint32_t>(a);
                const ghidra::int4 n = op->getOut()->getSize();
                if (!isExcluded(lo, lo + n))
                {
                    ++scan.literalLoads;
                    if (scan.literalWords.insert(lo).second) scan.readonly.emplace_back(lo, n);
                }
            }
            continue;
        }
        const ghidra::Varnode* out = op->getOut();
        if (out == nullptr || out->getSpace()->getType() != ghidra::IPTR_INTERNAL) continue;
        try
        {
            if (op->numInput() == 1 && valueOf(op->getIn(0), a))
                consts[std::make_pair(out->getOffset(), out->getSize())] =
                    op->getOpcode()->evaluateUnary(out->getSize(), op->getIn(0)->getSize(), a);
            else if (op->numInput() == 2 && valueOf(op->getIn(0), a) && valueOf(op->getIn(1), b))
                consts[std::make_pair(out->getOffset(), out->getSize())] =
                    op->getOpcode()->evaluateBinary(out->getSize(), op->getIn(0)->getSize(), a, b);
        }
        catch (ghidra::LowlevelError&)
        {
            // An op with no constant semantics (or a division by zero): just not a constant.
        }
    }
    return scan;
}

TokenKind KindForColor(int color)
{
    switch (color)
    {
    case 0: return TokenKind::Keyword;
    case 1: return TokenKind::Comment;
    case 2: return TokenKind::Type;
    case 3: return TokenKind::FuncName;
    case 4: return TokenKind::Variable;
    case 5: return TokenKind::Const;
    case 6: return TokenKind::Param;
    case 7: return TokenKind::Global;
    case 9: return TokenKind::Error;
    default: return TokenKind::Plain;
    }
}

// Turns the emitter's markup into lines of tokens. Each token's opref names the p-code op it
// was printed from; that op's address is the SH-2 instruction it came from. The markup is read
// in Ghidra's packed encoding, not XML: its XML parser drops whitespace-only text, which is every
// space between tokens.
std::vector<Line> LinesFromMarkup(const std::string& packed, const ghidra::Funcdata& fd, const ghidra::AddrSpaceManager* spaces,
                                  std::map<const ghidra::PcodeOp*, std::pair<std::set<size_t>, int>>* printed = nullptr)
{
    std::map<ghidra::uintm, const ghidra::PcodeOp*> ops;
    for (auto it = fd.beginOpAlive(); it != fd.endOpAlive(); ++it) ops[(*it)->getTime()] = *it;
    for (auto it = fd.beginOpDead(); it != fd.endOpDead(); ++it) ops[(*it)->getTime()] = *it;

    std::vector<Line> lines(1);
    ghidra::PackedDecode decoder(spaces);
    std::istringstream in(packed);
    decoder.ingestStream(in);
    const ghidra::uint4 kBreak = ghidra::ELEM_BREAK.getId();
    const ghidra::uint4 kLabel = ghidra::ELEM_LABEL.getId();
    const ghidra::uint4 kFuncName = ghidra::ELEM_FUNCNAME.getId();
    const ghidra::uint4 kCaseLabel = ghidra::ELEM_VALUE.getId();   // EmitMarkup::tagCaseLabel
    std::function<void()> walk = [&]() {
        const ghidra::uint4 elem = decoder.openElement();
        int color = 8, indent = 0;
        bool haveContent = false;
        std::string content;
        const ghidra::PcodeOp* op = nullptr;
        for (;;)
        {
            const ghidra::uint4 attr = decoder.getNextAttributeId();
            if (attr == 0) break;
            if (attr == ghidra::ATTRIB_COLOR.getId()) color = static_cast<int>(decoder.readUnsignedInteger());
            else if (attr == ghidra::ATTRIB_INDENT.getId()) indent = static_cast<int>(decoder.readSignedInteger());
            else if (attr == ghidra::ATTRIB_CONTENT.getId())
            {
                content = decoder.readString();
                haveContent = true;
            }
            else if (attr == ghidra::ATTRIB_OPREF.getId())
            {
                auto f = ops.find(static_cast<ghidra::uintm>(decoder.readUnsignedInteger()));
                if (f != ops.end()) op = f->second;
            }
        }
        if (elem == kBreak)
        {
            lines.emplace_back();
            lines.back().text.assign(static_cast<size_t>(std::max(indent, 0)), ' ');
        }
        if (haveContent && !content.empty())
        {
            Line& line = lines.back();
            Token t;
            t.begin = static_cast<uint16_t>(std::min<size_t>(line.text.size(), 0xFFFF));
            line.text += content;
            t.end = static_cast<uint16_t>(std::min<size_t>(line.text.size(), 0xFFFF));
            t.kind = elem == kLabel ? TokenKind::Label : elem == kFuncName ? TokenKind::FuncName : KindForColor(color);
            if (op)
            {
                // The instruction's canonical address: the function may be analysed at a mirror,
                // but navigation and notes are keyed by the canonical one.
                t.addr = static_cast<uint32_t>(op->getAddr().getOffset());
                uint32_t canonical = 0;
                if (CanonicalAddress(t.addr, canonical)) t.addr = canonical;
                if (t.kind == TokenKind::FuncName)
                {
                    const ghidra::FuncCallSpecs* cs = fd.getCallSpecs(op);
                    uint32_t c = 0;
                    if (cs && !cs->getEntryAddress().isInvalid() &&
                        CanonicalAddress(static_cast<uint32_t>(cs->getEntryAddress().getOffset()), c))
                    {
                        t.target = c;   // navigation goes to the canonical entry, whichever mirror was called
                        t.calledAt = static_cast<uint32_t>(cs->getEntryAddress().getOffset());
                    }
                }
            }
            line.tokens.push_back(t);
            // A case label names the first op of its case: it is a label, not that op printed again.
            if (op && printed && elem != kCaseLabel)
            {
                auto& p = (*printed)[op];
                p.first.insert(lines.size() - 1);
                ++p.second;
            }
        }
        while (decoder.peekElement() != 0) walk();
        decoder.closeElement(elem);
    };
    while (decoder.peekElement() != 0) walk();

    // A read printed inside another op's expression has no token of its own: an array form (p[i]) is printed by the pointer
    // expression's op, which holds the brackets; a read the statement uses only as an operand is printed by the op that uses it.
    // The load's instruction would then be on no line and a reader could not get from it to the text that shows its read
    // (A7-3). Each line that prints that expression gets an empty token at its end that carries the load's address: it draws
    // nothing, and the line belongs to that instruction too (navigation, the user's notes, the halt hint).
    if (printed)
        for (auto it = fd.beginOpAlive(); it != fd.endOpAlive(); ++it)
        {
            const ghidra::PcodeOp* o = *it;
            if (o->code() != ghidra::CPUI_LOAD || printed->count(o) != 0) continue;
            const std::set<size_t>* where = nullptr;
            if (o->getIn(1)->isWritten() && o->getIn(1)->isImplied())
            {
                const auto p = printed->find(o->getIn(1)->getDef());
                if (p != printed->end()) where = &p->second.first;
            }
            // ...else the op that finally uses its value, through the implied values between
            for (const ghidra::Varnode* v = o->getOut(); !where && v != nullptr && v->isImplied();)
            {
                const ghidra::PcodeOp* use = v->loneDescend();
                if (!use) break;
                const auto p = printed->find(use);
                if (p != printed->end()) where = &p->second.first;
                v = use->getOut();
            }
            if (!where) continue;
            uint32_t insn = static_cast<uint32_t>(o->getAddr().getOffset()), canonical = 0;
            if (CanonicalAddress(insn, canonical)) insn = canonical;
            for (size_t at : *where)
            {
                if (at >= lines.size()) continue;
                Line& line = lines[at];
                bool carried = false;
                for (const Token& t : line.tokens) carried = carried || t.addr == insn;
                if (carried) continue;
                Token t;
                t.begin = t.end = static_cast<uint16_t>(std::min<size_t>(line.text.size(), 0xFFFF));
                t.kind = TokenKind::Plain;
                t.addr = insn;
                line.tokens.push_back(t);
            }
        }

    std::vector<Line> out;
    size_t leading = 0;
    for (Line& l : lines)
    {
        const bool blank = l.text.find_first_not_of(' ') == std::string::npos;
        if (blank && out.empty())
        {
            ++leading;   // the document's leading break
            continue;
        }
        if (blank) l.text.clear();
        for (const Token& t : l.tokens)
            if (t.addr && (l.firstAddr == 0 || t.addr < l.firstAddr)) l.firstAddr = t.addr;
        out.push_back(std::move(l));
    }
    while (!out.empty() && out.back().text.empty()) out.pop_back();
    // 'printed' names lines by their index among all the document's lines; the lines returned begin after the leading
    // breaks, so the indices move up with them (A7-4: MemoryAccess::firstLine named the line after the one it printed on).
    if (printed && leading)
        for (auto& kv : *printed)
        {
            std::set<size_t> moved;
            for (size_t at : kv.second.first)
                if (at >= leading) moved.insert(at - leading);
            kv.second.first = std::move(moved);
        }
    return out;
}

struct AsmLines : public ghidra::AssemblyEmit
{
    std::vector<std::string> lines;
    void dump(const ghidra::Address& addr, const std::string& mnem, const std::string& body) override
    {
        char b[16];
        std::snprintf(b, sizeof b, "%X", static_cast<unsigned>(addr.getOffset()));
        lines.push_back(std::string(b) + "  " + mnem + " " + body);
    }
};

}  // namespace

const char* PhaseName(DecompilePhase phase)
{
    switch (phase)
    {
    case DecompilePhase::Idle: return "idle";
    case DecompilePhase::Init: return "init";
    case DecompilePhase::Flow: return "followFlow";
    case DecompilePhase::Actions: return "actions";
    case DecompilePhase::Print: return "print";
    case DecompilePhase::Teardown: return "teardown";
    }
    return "?";
}

namespace
{
std::set<uint32_t> gNoReturnForTests;
std::function<void(DecompilePhase)> gPhaseHookForTests;
std::function<void()> gBeforeReferenceHookForTests;
std::function<void()> gBoundaryHookForTests;
std::function<void()> gEngineDoneHookForTests;
}  // namespace

void Sh2Decompiler::Enter(DecompilePhase phase)
{
    if (phase != DecompilePhase::Teardown) mWorkPhase = phase;
    if (gPhaseHookForTests) gPhaseHookForTests(phase);
    if (mObserver && !mQuietPhases) mObserver(phase);
}

EngineWarning ClassifyEngineWarning(const std::string& comment)
{
    EngineWarning w;
    std::string text = comment;
    // Jump-table recovery works in a scratch copy of the function. What that copy's own flow and
    // simplification ran into is not this function's; what recovery concluded about the table is.
    const bool scratch = text.compare(0, 20, "WARNING (jumptable):") == 0;
    if (scratch) text.erase(0, 21);
    else if (text.compare(0, 9, "WARNING: ") == 0) text.erase(0, 9);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    // Destinations dropped (recovery in the scratch copy) mean cases are missing; a label the switch
    // normalisation could not work out (on the function proper, every destination recovered) is
    // only said.
    static const char* const kCasesMissing[] = { "Sanity check requires truncation of jumptable",
                                                 "Second-stage recovery error" };
    for (const char* o : kCasesMissing) w.casesMissing |= text.compare(0, std::strlen(o), o) == 0;
    static const char kRestarts[] = "Exceeded maximum restarts";
    w.restartLimit = text.compare(0, sizeof kRestarts - 1, kRestarts) == 0;
    static const char kRepeats[] = "Exceeded maximum repeats of ";
    w.repeatLimit = text.compare(0, sizeof kRepeats - 1, kRepeats) == 0;
    w.keep = !scratch || w.casesMissing;
    w.text = text;
    return w;
}

void Sh2Decompiler::SetActionRepeatLimitForTests(int limit)
{
    gActionRepeatLimitForTests = limit;
}

void Sh2Decompiler::SetLendEffectsForTests(bool lend)
{
    gLendEffectsForTests = lend;
}

void Sh2Decompiler::SetNoReturnForTests(const std::set<uint32_t>& entries)
{
    gNoReturnForTests = entries;
}

void Sh2Decompiler::SetPhaseHookForTests(std::function<void(DecompilePhase)> hook)
{
    gPhaseHookForTests = std::move(hook);
}

void Sh2Decompiler::SetBoundaryHookForTests(std::function<void()> hook)
{
    gBoundaryHookForTests = std::move(hook);
}

void Sh2Decompiler::SetEngineDoneHookForTests(std::function<void()> hook)
{
    gEngineDoneHookForTests = std::move(hook);
}

void Sh2Decompiler::SetBeforeReferenceHookForTests(std::function<void()> hook)
{
    gBeforeReferenceHookForTests = std::move(hook);
}

namespace
{

// "A", "A and B", "A, B and C": every register an access covers, in order.
std::string RegisterNames(const std::vector<const HardwareRegister*>& regs)
{
    std::string names;
    for (size_t i = 0; i < regs.size(); ++i)
        names += (i == 0 ? "" : i + 1 == regs.size() ? " and " : ", ") + std::string(regs[i]->name);
    return names;
}

// An address the bus decodes to a register area (named or not).
bool IsRegisterArea(uint32_t address)
{
    uint32_t canonical = 0;
    return HardwareAlias(address, canonical);
}

// The addresses a pointer varnode can hold, as far as the analysis proves them: the values the printed code shows as `&REG` (`puVar2 =
// &VDP2_TVSTAT; if (c) puVar2 = &VDP2_VCNT;`), as numbers (`pxVar1 = (xunknown4 *)0x25fe0110;`: a register's alias, A5-38) or as such a
// base plus a constant offset (`puVar1[4]`, `add #-4,Rn`; A5-37: the access is at base + offset, a different register). Every path to the
// pointer is a copy, a merge, or an add; 'others' is set when some path is not a known address (a parameter, a value loaded or computed, a
// callee's output): the pointer may hold something else as well. 'steps' is set when a path runs through a cycle, a pointer stepped in a loop
// (A5-39): it holds the first value and then others. 'bases' are the addresses an indexed pointer starts from (`(&SCU_D0EN)[param_1 * 8]`, a
// base plus an index the analysis does not know): the access is at the base plus an amount, said only for the index 0.

// One step a loop applies to a pointer's own value on a turn (A5-60, A5-70): a bitwise operation with a set of known numbers (the set is the
// values the other operand can take), or an add of a known number (the pointer stepped).
struct SelfStep
{
    ghidra::OpCode code = ghidra::CPUI_COPY;
    std::set<uint32_t> operands;
    bool operator==(const SelfStep& o) const { return code == o.code && operands == o.operands; }
    bool Adds() const { return code == ghidra::CPUI_INT_ADD; }
};

// The way from the value of a node still being walked, round the loop, back to the node: the steps one turn applies to it, in order (none: the
// value comes back unchanged). Every path of a loop body is one of these, however the body branches and joins, so a conditional operation, two
// operations in one turn and an operation on one path and a step on the other are each their own path; the node closes them over the values it
// has (CloseBack) and nothing is lost on the way.
struct SelfPath
{
    const ghidra::Varnode* origin = nullptr;
    std::vector<SelfStep> steps;
    bool operator==(const SelfPath& o) const { return origin == o.origin && steps == o.steps; }
    bool Adds() const
    {
        for (const SelfStep& s : steps)
            if (s.Adds()) return true;
        return false;
    }
};

struct PointerValues
{
    std::map<uint32_t, bool> constants;   // address -> reached through a constant offset (printed as base[k], not as the address)
    std::set<uint32_t> bases;
    bool others = false;
    bool steps = false;
    // (while walking) the value includes the value of a node still being walked, taken through each of these paths. Closed where that node ends.
    std::vector<SelfPath> back;
    // A pointer stepped in a loop, whose first values are no cached-partition register but whose later ones are (`(p + 4) and 0x1FFFFFFF` from
    // 0x25F80004 reads 0x05F80008 next): one such address, for the cache caution the stepped note then gives (A5-70).
    bool steppedCached = false;
    uint32_t steppedCachedAt = 0;
};

class PointerResolver
{
public:
    PointerValues Resolve(const ghidra::Varnode* start)
    {
        PointerValues v = Walk(start, 0);
        if (budget_ <= 0 || !v.back.empty()) v.others = true;   // (a path round a loop no node closed)
        for (const SelfPath& p : v.back) v.steps |= p.Adds();
        v.back.clear();
        return v;
    }

private:
    PointerValues Walk(const ghidra::Varnode* vn, int depth)
    {
        PointerValues out;
        if (--budget_ <= 0 || depth > 96) { out.others = true; return out; }
        // A number the pointer holds on this path: a known address. (As the addend of an unknown it is an offset, below: the engine prints
        // `param_1 + -0xc`, 0xFFFFFFF4, as a number, and that is not a pointer to SH2_RTCNT, A5-28's rawconst.)
        if (vn->isConstant()) { out.constants[static_cast<uint32_t>(vn->getOffset())] = false; return out; }
        const auto memo = done_.find(vn);
        if (memo != done_.end()) return memo->second;
        // A path back to a node being walked: the value comes back unchanged. What the loop does to it on the way is added by the operations
        // that path runs through (Shift, Bitwise, Indexed), and the node closes the paths where it ends.
        if (active_.count(vn)) { out.back.push_back(SelfPath{ vn, {} }); return out; }
        const ghidra::PcodeOp* d = vn->isWritten() ? vn->getDef() : nullptr;
        if (!d) { out.others = true; return out; }
        active_.insert(vn);
        switch (d->code())
        {
        case ghidra::CPUI_COPY:
        case ghidra::CPUI_CAST:
            out = Walk(d->getIn(0), depth + 1);
            break;
        case ghidra::CPUI_MULTIEQUAL:
            for (int i = 0; i < d->numInput(); ++i) Merge(out, Walk(d->getIn(i), depth + 1));
            break;
        case ghidra::CPUI_PTRSUB:   // the address of a global: PTRSUB(0, address); or a base and a field offset
            if (d->getIn(0)->isConstant() && d->getIn(1)->isConstant())
                out.constants[static_cast<uint32_t>(d->getIn(0)->getOffset() + d->getIn(1)->getOffset())] = false;
            else if (d->getIn(1)->isConstant())
                out = Shift(Walk(d->getIn(0), depth + 1), static_cast<uint32_t>(d->getIn(1)->getOffset()));
            else
                out.others = true;
            break;
        case ghidra::CPUI_PTRADD:   // base + index * size
        {
            PointerValues base = Walk(d->getIn(0), depth + 1);
            if (d->getIn(1)->isConstant())
                out = Shift(base, static_cast<uint32_t>(d->getIn(1)->getOffset() * d->getIn(2)->getOffset()));
            else
                out = Indexed(base);
            break;
        }
        case ghidra::CPUI_INT_ADD:
        case ghidra::CPUI_INT_SUB:
        {
            const bool sub = d->code() == ghidra::CPUI_INT_SUB;
            const ghidra::Varnode* a = d->getIn(0);
            const ghidra::Varnode* b = d->getIn(1);
            if (b->isConstant() || (!sub && a->isConstant()))
            {
                // A constant addend is an offset from what the other operand holds (a base shifted, an unknown stays unknown).
                const ghidra::Varnode* base = b->isConstant() ? a : b;
                const uint32_t k = static_cast<uint32_t>((b->isConstant() ? b : a)->getOffset());
                out = Shift(Walk(base, depth + 1), sub ? 0u - k : k);
                // An unknown plus a number that is an address of colour RAM or of a register area: the number is the base and the unknown the
                // index (`param + 0x25F00000`), not a small offset of an unknown. The engine prints a symbol's address as the symbol (and the
                // pointer is based at it already); colour RAM has none, nor has an unnamed register (A5-78). Said for the index 0, as an indexed
                // pointer is. (Not the on-chip registers' partition: `param_1 + 0xFFFFFFF4` is `param_1 - 12`, A5-28's rawconst.)
                if (!sub && out.others && out.constants.empty() && out.bases.empty() && out.back.empty() && !out.steps && k < 0xE0000000u && (InColorRam(k) || IsRegisterArea(k)))
                    out.bases.insert(k);
            }
            else
            {
                // Neither is a number: an address plus an index nothing is known about (either addend may be the address).
                PointerValues x = Walk(a, depth + 1), y = Walk(b, depth + 1);
                const bool anyBase = !x.constants.empty() || !x.bases.empty() || !y.constants.empty() || !y.bases.empty();
                if (!anyBase)
                {
                    // An unknown added to the pointer's own value, round a loop, steps it by an amount nothing is known about (A5-54: the
                    // qualifier "it may hold other values too" said so, the stepped note did not).
                    const bool loops = !x.back.empty() || !y.back.empty();
                    Merge(out, x);
                    Merge(out, y);
                    out.others = true;
                    if (loops) out.steps = true;
                }
                else
                {
                    out = Indexed(x);
                    if (!sub) Merge(out, Indexed(y));
                    out.others |= x.steps || y.steps;   // (an unknown addend is the index: it makes the pointer indexed, not unknown)
                    out.steps |= x.steps || y.steps;
                }
            }
            break;
        }
        case ghidra::CPUI_INT_AND:   // a pointer moved between bus partitions (`& 0x1fffffff`, `| 0x20000000`: the cached / cache-through idiom, A5-51)
        case ghidra::CPUI_INT_OR:
        case ghidra::CPUI_INT_XOR:
            out = Bitwise(d->code(), Walk(d->getIn(0), depth + 1), Walk(d->getIn(1), depth + 1));
            break;
        default:
            out.others = true;
            break;
        }
        active_.erase(vn);
        CloseBack(out, vn);
        // What was worked out while this node was being walked, for the paths round it, is no longer the answer for them: it was closed here.
        for (auto it = done_.begin(); it != done_.end();)
        {
            bool mine = false;
            for (const SelfPath& p : it->second.back) mine |= p.origin == vn;
            it = mine ? done_.erase(it) : std::next(it);
        }
        done_[vn] = out;
        return out;
    }

    static void Merge(PointerValues& into, const PointerValues& from)
    {
        constexpr size_t kMaxPaths = 32;   // (a loop body with more paths than this is not worked out)
        for (const auto& c : from.constants)
        {
            auto at = into.constants.find(c.first);
            if (at == into.constants.end()) into.constants[c.first] = c.second;
            else at->second = at->second && c.second;
        }
        into.bases.insert(from.bases.begin(), from.bases.end());
        into.others |= from.others;
        into.steps |= from.steps;
        for (const SelfPath& p : from.back)
            if (std::find(into.back.begin(), into.back.end(), p) == into.back.end()) into.back.push_back(p);
        if (into.back.size() > kMaxPaths)
        {
            into.back.clear();
            into.others = into.steps = true;
        }
        if (!into.steppedCached && from.steppedCached)
        {
            into.steppedCached = true;
            into.steppedCachedAt = from.steppedCachedAt;
        }
    }

    // The values a pointer takes round a loop: the closure of the values it holds at 'node' under the paths from the node back to itself. A path
    // of bitwise operations with known numbers gives values a finite closure holds exactly (`0x25F80004 and 0x1FFFFFFF` is 0x05F80004, a fixed point
    // after that; `xor 0x20000000` alternates between two; `(p and 0xFFFFFFF0) xor 4` gives two); more than kMaxValues values is left unknown. A path
    // that adds a number steps the pointer by it every turn, which no closure ends: the pointer is then stepped (`steps`, `others`), its first
    // values are kept, and the later ones are not worked out; but whether they come to read through the cached partition is looked for in a bounded
    // walk, because the cache caution is about that partition, not the register (A5-70).
    static void CloseBack(PointerValues& v, const ghidra::Varnode* node)
    {
        std::vector<SelfPath> rest;
        std::vector<const SelfPath*> exact, stepped;
        std::vector<SelfPath> mine;
        for (const SelfPath& p : v.back)
            (p.origin == node ? mine : rest).push_back(p);
        if (mine.empty()) return;
        v.back = rest;
        for (const SelfPath& p : mine) (p.Adds() ? stepped : exact).push_back(&p);
        constexpr size_t kMaxValues = 64;
        std::set<uint32_t> all;
        for (const auto& c : v.constants) all.insert(c.first);
        std::vector<uint32_t> work(all.begin(), all.end());
        bool overflow = false;
        while (!work.empty() && !overflow)
        {
            const uint32_t a = work.back();
            work.pop_back();
            for (const SelfPath* p : exact)
            {
                for (uint32_t r : Eval(*p, a))
                {
                    if (!all.insert(r).second) continue;
                    v.constants[r] = true;
                    work.push_back(r);
                    if (all.size() > kMaxValues) { v.others = true; overflow = true; break; }
                }
                if (overflow) break;
            }
        }
        if (stepped.empty()) return;
        v.steps = v.others = true;
        if (overflow || v.steppedCached) return;
        bool cachedKnown = false;
        for (uint32_t a : all) cachedKnown |= (a >> 29) == 0 && IsRegisterArea(a);
        if (cachedKnown) return;   // (the caution is made for that address already)
        std::set<uint32_t> seen = all;
        std::vector<uint32_t> queue(all.begin(), all.end());
        size_t derived = 0;
        for (size_t i = 0; i < queue.size() && derived < kMaxValues && !v.steppedCached; ++i)
            for (const SelfPath& p : mine)
                for (uint32_t r : Eval(p, queue[i]))
                {
                    if (!seen.insert(r).second) continue;
                    ++derived;
                    queue.push_back(r);
                    if (!v.steppedCached && (r >> 29) == 0 && IsRegisterArea(r))
                    {
                        v.steppedCached = true;
                        v.steppedCachedAt = r;
                    }
                }
    }

    // The values 'a' becomes after one turn along 'path'.
    static std::set<uint32_t> Eval(const SelfPath& path, uint32_t a)
    {
        std::set<uint32_t> cur{ a };
        for (const SelfStep& s : path.steps)
        {
            std::set<uint32_t> next;
            for (uint32_t x : cur)
                for (uint32_t m : s.operands) next.insert(Apply(s.code, x, m));
            cur.swap(next);
        }
        return cur;
    }

    // v shifted by k. A path round a loop that is shifted is a step.
    static PointerValues Shift(const PointerValues& v, uint32_t k)
    {
        PointerValues out;
        out.others = v.others;
        out.steps = v.steps;
        out.steppedCached = v.steppedCached;
        out.steppedCachedAt = v.steppedCachedAt + k;
        for (SelfPath p : v.back)
        {
            if (k != 0) p.steps.push_back(SelfStep{ ghidra::CPUI_INT_ADD, { k } });
            out.back.push_back(std::move(p));
        }
        for (const auto& c : v.constants) out.constants[c.first + k] = c.second || k != 0;
        for (uint32_t b : v.bases) out.bases.insert(b + k);
        return out;
    }

    // x op y for a bitwise op. The values are the address sets of the operands: with one side a set of known numbers (the mask) and the
    // other a set of addresses, the op gives each pair's result (`{0x25F80004, 0x25F8000A} & {0x1FFFFFFF}` is the same two registers
    // through the cached partition). Every result is an address the pointer holds through an operation, never one printed as itself. The
    // other side may hold more besides (`others`). An operand nothing is known about (a parameter, a loaded value, a computed mask) makes
    // the result unknown. An indexed pointer keeps its bases, moved (A5-61). A side that is only the pointer's own value
    // round a loop (no address, no unknown) is a path round the loop that applies the operation (A5-60): the node closes it with the others
    // (A5-70), so `p = p and mask` is a fixed point only when the first value already is one.
    static PointerValues Bitwise(ghidra::OpCode code, const PointerValues& x, const PointerValues& y)
    {
        PointerValues out;
        constexpr size_t kMaxPairs = 64;   // (a pointer merged from more addresses than this is not worked out)
        auto known = [](const PointerValues& v) { return !v.constants.empty() && v.bases.empty() && !v.others && v.back.empty(); };
        auto onlyBack = [](const PointerValues& v) { return v.constants.empty() && v.bases.empty() && !v.others && !v.back.empty(); };
        out.steps = x.steps || y.steps;
        if (onlyBack(x) && onlyBack(y))
        {
            Merge(out, x);   // (p and p)
            Merge(out, y);
            return out;
        }
        if (onlyBack(x) || onlyBack(y))
        {
            const PointerValues& other = onlyBack(x) ? y : x;
            const PointerValues& own = onlyBack(x) ? x : y;
            if (known(other))
            {
                std::set<uint32_t> masks;
                for (const auto& c : other.constants) masks.insert(c.first);
                for (SelfPath p : own.back)
                {
                    p.steps.push_back(SelfStep{ code, masks });
                    out.back.push_back(std::move(p));
                }
            }
            else
                out.others = out.steps = true;
            return out;
        }
        const PointerValues* mask = known(y) ? &y : known(x) ? &x : nullptr;
        const PointerValues* set = mask == &y ? &x : &y;
        if (!mask || (set->constants.empty() && set->bases.empty()) ||
            mask->constants.size() * (set->constants.size() + set->bases.size()) > kMaxPairs)
        {
            out.others = true;
            return out;
        }
        for (const auto& s : set->constants)
            for (const auto& m : mask->constants)
                out.constants[Apply(code, s.first, m.first)] = true;
        // An indexed pointer (bases) is moved by the operation too: the note it allows is for the index 0, where the access is at the base
        // moved (`(base + index) and mask` is `base and mask` at the index 0, A5-61); the operation on the index is nothing it says.
        for (uint32_t base : set->bases)
            for (const auto& m : mask->constants)
                out.bases.insert(Apply(code, base, m.first));
        out.others = set->others;
        // The pointer's own value round a loop, moved by the operation: a path that the node it comes back to closes (A5-60, A5-70).
        if (!set->back.empty())
        {
            std::set<uint32_t> masks;
            for (const auto& m : mask->constants) masks.insert(m.first);
            for (SelfPath p : set->back)
            {
                p.steps.push_back(SelfStep{ code, masks });
                out.back.push_back(std::move(p));
            }
        }
        return out;
    }

    static uint32_t Apply(ghidra::OpCode code, uint32_t a, uint32_t b)
    {
        return code == ghidra::CPUI_INT_AND ? (a & b) : code == ghidra::CPUI_INT_OR ? (a | b) : code == ghidra::CPUI_INT_ADD ? a + b : (a ^ b);
    }

    // Every address held becomes the base of an indexed pointer.
    static PointerValues Indexed(const PointerValues& v)
    {
        PointerValues out;
        out.others = v.others;
        out.steps = v.steps;
        if (!v.back.empty()) out.steps = out.others = true;
        for (const auto& c : v.constants) out.bases.insert(c.first);
        out.bases.insert(v.bases.begin(), v.bases.end());
        return out;
    }

    int budget_ = 4096;
    std::set<const ghidra::Varnode*> active_;
    std::map<const ghidra::Varnode*, PointerValues> done_;
};

// How far a pointer stepped in a loop is taken to walk: a register within this many bytes of where it starts (either way: a step may be
// negative) is one it may reach. The trip count is not worked out; 256 bytes is 64 longs, more than the loops that walk a register file.
constexpr uint32_t kSteppedReach = 0x100;

// A named register whose address is within 'span' bytes of 'address' (through any alias, in the same register area).
bool NamedRegisterNear(uint32_t address, uint32_t span)
{
    uint32_t canonical = 0;
    if (!HardwareAlias(address, canonical)) return false;
    size_t n = 0;
    const HardwareRegister* regs = SaturnHardwareRegisters(n);
    for (size_t i = 0; i < n; ++i)
    {
        const int64_t lo = regs[i].address, hi = int64_t(regs[i].address) + regs[i].size - 1;
        if (hi + span >= canonical && lo <= int64_t(canonical) + span) return true;
    }
    return false;
}

// True when every use of 'vn' looks only at the bits in 'mask': an AND with a constant inside it, through the copies, extensions and
// truncations that carry the value on. A value nothing uses is seen by no bit.
bool OnlyUsesBits(const ghidra::Varnode* vn, uint64_t mask, std::set<const ghidra::Varnode*>& seen)
{
    if (!seen.insert(vn).second) return true;   // (a loop's merge of its own value: its other uses are judged where they are)
    for (auto it = vn->beginDescend(); it != vn->endDescend(); ++it)
    {
        const ghidra::PcodeOp* use = *it;
        if (use->isDead()) continue;
        switch (use->code())
        {
        case ghidra::CPUI_INT_AND:
        {
            const ghidra::Varnode* other = use->getIn(0) == vn ? use->getIn(1) : use->getIn(0);
            if (other->isConstant() && (other->getOffset() & ~mask) == 0) continue;
            return false;
        }
        case ghidra::CPUI_RETURN:   // (r0 left in the return register by a poll is no use of the value: the decompiler returns it whatever the code meant)
            continue;
        case ghidra::CPUI_COPY:
            // A byte stored to a constant address of ordinary memory: the engine folds the store into a copy to that memory's variable, and
            // the byte is then in memory, whole, whatever the readers of the variable look at (A5-66).
            if (use->getOut() && use->getOut()->isPersist()) return false;
            if (!use->getOut() || !OnlyUsesBits(use->getOut(), mask, seen)) return false;
            continue;
        case ghidra::CPUI_INT_ZEXT:
        case ghidra::CPUI_INT_SEXT:
        case ghidra::CPUI_CAST:
        case ghidra::CPUI_MULTIEQUAL:
            if (!use->getOut() || !OnlyUsesBits(use->getOut(), mask, seen)) return false;
            continue;
        case ghidra::CPUI_SUBPIECE:
            if (use->getIn(1)->isConstant() && use->getIn(1)->getOffset() == 0 && use->getOut() && OnlyUsesBits(use->getOut(), mask, seen)) continue;
            return false;
        default:
            return false;
        }
    }
    return true;
}

bool OnlyUsesBits(const ghidra::Varnode* vn, uint64_t mask)
{
    std::set<const ghidra::Varnode*> seen;
    return OnlyUsesBits(vn, mask, seen);
}

// What the pseudocode does not say about the hardware accesses it shows. Every access to a
// register area at a constant address is a volatile read or write by now (the symbols and ranges
// are volatile), so those ops are the accesses, at the address and width the code used. A read or
// write through a pointer that did not become one constant stays a plain load or store (patches/0009);
// when the engine proved the pointer to be one of several constants, and some are register addresses
// (the output prints `&REG`), each of those is said as the direct access would be, under "if the pointer holds ..."
// (A5-28): the cache caution, the read effects, the width cautions, the write effects.
std::string ColorRamByteNote(uint32_t at)
{
    return "a 1-byte write to colour RAM at " + Hex8(at) + " stores the whole 16-bit entry: the byte not written is the last value on the "
           "data bus, not the entry's old byte (Mednafen vdp2.c RW_W8)";
}

std::vector<HardwareNote> HardwareNotes(const ghidra::Funcdata& fd, const std::function<uint32_t(uint32_t)>& canonicalOf)
{
    std::set<std::pair<uint32_t, std::string>> seen;
    std::vector<HardwareNote> out;
    auto noteRaw = [&](uint32_t at, std::string text, bool caution) {
        if (seen.insert(std::make_pair(at, text)).second) out.push_back(HardwareNote{ at, std::move(text), caution });
    };
    std::set<const HardwareRegister*> written;
    std::vector<std::pair<uint32_t, const HardwareRegister*>> splitReads;
    // One access: of 'size' bytes at the constant address 'at', made by the instruction 'insn'; 'prefix' is what a note starts
    // with when the address is one the pointer may hold (empty for a direct access, whose address is certain).
    auto account = [&](uint32_t insn, uint32_t at, uint32_t size, bool write, const ghidra::Varnode* value, const std::string& prefix, bool shownByAddress) {
        const bool read = !write;
        auto note = [&](uint32_t where, std::string text, bool caution) { noteRaw(where, prefix + text, caution); };
        // A misaligned access is an address error and is made at the aligned address (A5-25), on-chip too from 0xFFFFFF00 up; below
        // it the handlers decode the address as given (A5-29): the registers are that access's.
        const uint32_t reach = AccessAddress(at, size);
        const std::vector<const HardwareRegister*> regs = HardwareRegistersReached(reach, size, write);
        // The width table's cell for each register the access reaches: whether its own read or
        // write effect happens at this width (a word read of the byte beside FTCSR does not read it).
        const std::vector<CoveredRegister> covered = CoveredRegisters(at, size, write);
        auto effectHappens = [&](const HardwareRegister* reg) {
            for (const CoveredRegister& c : covered)
                if (c.reg == reg) return (c.cell.flags & kWidthEffect) != 0;
            return true;
        };
        std::string text;
        bool ignored = false;
        if (write)
            for (const std::string& t : WriteEffects(at, size, value->isConstant(), static_cast<uint32_t>(value->getOffset())))
                note(insn, t, false);   // an effect that depends on the value: the cache purge, the VDP1 start, the SCU's DMA
        if (write && KeyedWrite(at, size, value->isConstant(), static_cast<uint32_t>(value->getOffset()), text, ignored))
            note(insn, text, ignored);   // the watchdog's write ports: the key picks the register
        else if (!regs.empty())
        {
            for (const HardwareRegister* reg : regs)
            {
                const bool smpc = reg->address >= 0x00100000u && reg->address < 0x00180000u;
                if (read && reg->access == RegisterAccess::WriteOnly)
                    note(insn, std::string("reads ") + reg->name + ", which is write-only: " +
                                   (smpc ? "Mednafen returns the last byte written to the SMPC, to any register (its bus buffer), not the register"
                                         : "the value is not what was written"), true);
                // The SMPC's bus buffer is also the high bits of the two status registers it reads (smpc.c SMPC_Read: `ret = BusBuffer; ...
                // ret &= ~0x01; ret |= SF;` and `(ret & 0x80) | IOBusState`). Said only where the code uses them: a read the code masks to
                // bit 0 (`SMPC_SF & 1`, every game poll) does not see them (A5-56).
                if (read && smpc && (reg->address == 0x00100063u || reg->address == 0x00100075u || reg->address == 0x00100077u) && value &&
                    !OnlyUsesBits(value, reg->address == 0x00100063u ? 0x01u : 0x7Fu))
                    note(insn, std::string(reg->name) + ": " + (reg->address == 0x00100063u ? "bits 7-1" : "bit 7") +
                                   " of a read are the last byte written to the SMPC (its bus buffer), not the register's", false);
                if (write && reg->access == RegisterAccess::ReadOnly)
                    note(insn, std::string("writes ") + reg->name + ", which is read-only: the write does nothing", true);
                if (write && reg->access == RegisterAccess::KeyedWrite && size != 1)   // a byte write: the width table says it
                    note(insn, KeyedPortCaution(*reg, size), true);
                if (read && reg->onRead && effectHappens(reg)) note(insn, std::string(reg->name) + ": " + reg->onRead, false);
                if (reg->about) note(insn, std::string(reg->name) + ": " + reg->about, false);
                // (a flag that a write sets to 1 is no surprise when the value written is 1)
                if (write && reg->noteWrite && effectHappens(reg) && !(value->isConstant() && (value->getOffset() & 0xFFu) == 1))
                    note(insn, std::string(reg->name) + ": " + reg->onWrite, false);
                if (prefix.empty())
                {
                    if (write) written.insert(reg);
                    if (read && reg->split && effectHappens(reg)) splitReads.emplace_back(insn, reg);
                }
            }
        }
        // What the access's width does to the registers it reaches that the line does not say: a
        // long over a pair, a byte write to a 16-bit VDP register, a word or byte access to an
        // on-chip one, a word at an SMPC register, the register the 32-bit bus reads as well. The
        // width table decides, per register, and writes the cautions (NarrowAccessEffects).
        for (const NarrowEffect& e : NarrowAccessEffects(at, size, write)) note(insn, e.text, e.caution);
        // VDP2 colour RAM stores the whole 16-bit entry from the data bus on a byte write: the byte not written is the last value on the
        // bus, not the entry's old byte (vdp2.c RW_W8, unlike VRAM's masked store), so no value of the entry is carried past it (A5-50).
        if (write && size == 1 && InColorRam(at))
            note(insn, ColorRamByteNote(at), true);
        if (regs.empty()) return;   // an unnamed register: nothing to add to the address shown
        // The engine names a register only at the address its symbol is at: the cached and
        // cache-through partitions for the bus, the one on-chip address. Any other alias is
        // shown as an address.
        uint32_t canonical = 0;
        HardwareAlias(reach, canonical);
        const bool named = ClassifyPartition(canonical) == Sh2Partition::OnChip
                               ? reach == canonical
                               : (reach >> 29) <= 1 && (reach & 0x1FFFFFFFu) == canonical;
        // (a misaligned access says where it lands in its own caution)
        if (!named && reach == at && shownByAddress) note(insn, Hex8(at) + " is " + regs[0]->name + " through a mirror (shown by address)", false);
    };
    for (auto it = fd.beginOpAlive(); it != fd.endOpAlive(); ++it)
    {
        const ghidra::PcodeOp* o = *it;
        if (o->code() == ghidra::CPUI_LOAD || o->code() == ghidra::CPUI_STORE)
        {
            // A plain load or store through a pointer that is not one constant. If every value the pointer can hold is a constant
            // (or some are, and the rest are not known), the register addresses among them are accesses to those registers.
            const ghidra::Varnode* ptr = o->getIn(1);
            if (ptr->isConstant()) continue;
            const bool write = o->code() == ghidra::CPUI_STORE;
            const ghidra::Varnode* value = write ? o->getIn(2) : o->getOut();
            if (!value) continue;
            const PointerValues pv = PointerResolver().Resolve(ptr);
            const uint32_t size = static_cast<uint32_t>(value->getSize());
            // The addresses the pointer can hold that are a register's, by its symbol or by an alias the engine prints as a number
            // (A5-38); a pointer that starts at one and is indexed is said for the register itself, as "if the index is 0".
            // (an address the bus decodes to a register area, named or not: the cache caution and the width cautions are about the access,
            // and an unnamed register in a SCSP or VDP2 block is read through the cache as a named one is)
            auto isRegister = [&](uint32_t address) {
                uint32_t canonical = 0;
                return HardwareAlias(AccessAddress(address, size), canonical);
            };
            struct Candidate { uint32_t address; bool indexed; bool shifted; };
            std::vector<Candidate> registers;
            for (const auto& c : pv.constants)
                if (isRegister(c.first)) registers.push_back(Candidate{ c.first, false, c.second });
            for (uint32_t c : pv.bases)
                if (isRegister(c) && !pv.constants.count(c)) registers.push_back(Candidate{ c, true, false });
            const uint32_t insn = canonicalOf(static_cast<uint32_t>(o->getAddr().getOffset()));
            const bool certain = pv.constants.size() == 1 && pv.bases.empty() && !pv.others;
            // A byte store through a pointer that holds a colour RAM address stores the whole entry as a store at a constant address does
            // (A5-50): said under the address the pointer holds, as the register notes are (A5-67). Every colour RAM address says the same
            // thing, so a pointer stepped from one need not name the others.
            if (write && size == 1)
                for (const auto& c : pv.constants)
                {
                    if (!InColorRam(c.first)) continue;
                    bool others = pv.others || !pv.bases.empty();
                    for (const auto& d : pv.constants) others |= !InColorRam(d.first);
                    noteRaw(insn, (certain ? std::string() : "if the pointer holds " + Hex8(c.first) + (others ? " (it may hold other values too)" : "") + ": ") +
                                      ColorRamByteNote(c.first), true);
                }
            if (write && size == 1)
                for (uint32_t b : pv.bases)
                    if (!pv.constants.count(b) && InColorRam(b))
                        noteRaw(insn, "if the index is 0 (the pointer is based at " + Hex8(b) + "): " + ColorRamByteNote(b), true);
            if (registers.empty()) continue;
            // (an address the pointer holds that is no register's is another value it may hold, as a parameter's is)
            bool otherValues = pv.others;
            for (const auto& c : pv.constants) otherValues |= !isRegister(c.first);
            for (uint32_t c : pv.bases) otherValues |= !isRegister(c);
            for (const Candidate& rc : registers)
            {
                // (the register the access reaches: the SMPC's word view at an even address is no register's own address)
                const std::vector<const HardwareRegister*> reached = HardwareRegistersReached(AccessAddress(rc.address, size), size, write);
                const HardwareRegister* r = reached.empty() ? FindHardwareRegister(AccessAddress(rc.address, size)) : reached[0];
                const std::string name = r ? std::string(" (") + r->name + ")" : std::string();
                std::string prefix;
                if (rc.indexed)
                    prefix = "if the index is 0 (the pointer is based at " + Hex8(rc.address) + name + "): ";
                else if (!certain)
                    prefix = "if the pointer holds " + Hex8(rc.address) + name + (otherValues ? " (it may hold other values too)" : "") + ": ";
                account(insn, rc.address, size, write, value, prefix, !rc.shifted);
            }
            // (said for a pointer that starts at a register, or in a register area within kSteppedReach bytes of one: A5-54, a walk from the SCU's
            // hole at 0x25FE0018 reaches SCU_D1EN, one from the last SCSP slot reaches MVOL and MOBUF. A walk through the SCSP's DSP program
            // area, with no register near, has nothing to be missed.)
            // ...or, whatever is near, when the later values read through the cached partition: that caution is about the partition, and an
            // unnamed register (an SCSP slot's, 0x05B00000-0x05B003FF) is read through the cache as a named one is (A5-83: `p = (p + 4) ^
            // 0x20000000` from 0x35B000C8 reads 0x15B000CC next, a cached read of a register no name is near).
            if (pv.steps)
                for (const Candidate& rc : registers)
                {
                    const bool cached = pv.steppedCached && !write;
                    if (!cached && HardwareRegistersReached(AccessAddress(rc.address, size), size, write).empty() && !NamedRegisterNear(rc.address, kSteppedReach))
                        continue;
                    // A step and a mask in one turn moves the pointer into the cached partition (A5-70): its addresses are not worked out,
                    // but a read of a register through that partition is a cache read, whichever register it is.
                    noteRaw(insn, "the pointer is stepped in a loop from " + Hex8(rc.address) + ": the iterations after the first reach other addresses, "
                            "other registers perhaps, which are not said" +
                            (cached ? std::string("; they read through the cached partition (" + Hex8(pv.steppedCachedAt) + " is one) too, and a read of a "
                                    "register through the cached partition is a cache read when the cache is on (SH2_CCR.CE): a hit returns the cached bytes "
                                    "without reading the hardware, so a poll of it may never see it change; read it through 0x2xxxxxxx to bypass the cache")
                                    : std::string()),
                            cached);
                    break;
                }
            continue;
        }
        if (o->code() != ghidra::CPUI_CALLOTHER || o->numInput() < 2 || !o->getIn(0)->isConstant()) continue;
        const ghidra::UserPcodeOp* u = fd.getArch()->userops.getOp(static_cast<ghidra::uint4>(o->getIn(0)->getOffset()));
        const bool read = u && u->getType() == ghidra::UserPcodeOp::volatile_read;
        const bool write = u && u->getType() == ghidra::UserPcodeOp::volatile_write;
        if (!read && !write) continue;
        const ghidra::Varnode* value = read ? o->getOut() : (o->numInput() > 2 ? o->getIn(2) : nullptr);
        if (!value) continue;
        account(canonicalOf(static_cast<uint32_t>(o->getAddr().getOffset())), static_cast<uint32_t>(o->getIn(1)->getOffset()),
                static_cast<uint32_t>(value->getSize()), write, value, std::string(), true);
    }
    // A store to the BIOS ROM (0x00000000-0x000FFFFF through either bus partition), or to the cartridge areas, that the engine folded into ordinary memory: the
    // ROM drops the write (ss.c BusRW_DB_CS0_u8/u16/u32_W1: the ROM branch stores nothing), but a later read of the address in this
    // function is shown as the value written (A5-33). The store is no volatile op to hang a note on; the engine's record of how it
    // ended (patch 0017) is the only trace, so the caution is made from it.
    for (const ghidra::AccessRecord& rec : fd.getAccessFates())
    {
        if (!rec.store || rec.fate != ghidra::fate_folded_ordinary || rec.addr.isInvalid() || rec.addr.getSpace() != fd.getArch()->getDefaultCodeSpace()) continue;
        const uint32_t a = static_cast<uint32_t>(rec.addr.getOffset());
        if ((a >> 29) > 1) continue;
        const uint32_t bus = a & 0x07FFFFFFu;
        if (bus > 0x000FFFFFu)
        {
            // The A-bus cartridge areas, 0x02000000-0x04FFFFFF (A5-73): what a write does is the cartridge's, and the decompiler is told none. Mednafen's
            // default is no cartridge (cart.c DummyRead / DummyWrite: a write goes nowhere, a read returns 0xFFFF); a ROM cartridge drops writes; the
            // backup RAM cartridge (cart/backup.c, 0x04000000) keeps a byte at an odd address only (`if(A & 1) *ptr = *DB`, the same function for a word
            // write, so a word write stores nothing); the extended RAM cartridge answers 0x02400000 and up. A cartridge probe -- store a pattern, read it
            // back, compare -- is folded to a match, and the absent-cartridge branch goes.
            if (bus < 0x02000000u || bus > 0x04FFFFFFu) continue;
            noteRaw(canonicalOf(static_cast<uint32_t>(rec.pc.getOffset())),
                    "a write to the cartridge area at " + Hex8(a) + " may be dropped (no cartridge, a ROM cartridge, a backup RAM cartridge for a word or "
                    "an even byte): a later read of it in this function is shown as the value written, which the cartridge may not hold",
                    true);
            continue;
        }
        noteRaw(canonicalOf(static_cast<uint32_t>(rec.pc.getOffset())),
                "a write to the BIOS ROM at " + Hex8(a) + " does nothing (the ROM takes no write), but the decompiler treats the address as ordinary "
                "memory: a later read of it in this function is shown as the value written, which the ROM does not hold",
                true);
    }
    // A register read back in a function that also writes it, when the read returns something
    // else: say what.
    for (const auto& r : splitReads)
        if (written.count(r.second)) noteRaw(r.first, std::string(r.second->name) + ": " + r.second->split, false);
    std::stable_sort(out.begin(), out.end(), [](const HardwareNote& x, const HardwareNote& y) { return x.address < y.address; });
    return out;
}

}  // namespace

bool ParseAddressRanges(const std::string& input, std::vector<std::pair<uint32_t, uint32_t>>& out, std::string& error)
{
    out.clear();
    error.clear();
    // "a - b" is one range: spaces around a dash go before entries are split.
    std::string text;
    for (size_t k = 0; k < input.size(); ++k)
    {
        if (std::isspace(static_cast<unsigned char>(input[k])))
        {
            size_t n = k;
            while (n < input.size() && std::isspace(static_cast<unsigned char>(input[n]))) ++n;
            const bool nextDash = n < input.size() && input[n] == '-';
            const bool afterDash = !text.empty() && text.back() == '-';
            if (nextDash || afterDash) { k = n - 1; continue; }
        }
        text += input[k];
    }
    auto hex = [](const std::string& t, uint32_t& v) {
        std::string d = t;
        if (d.size() > 2 && d[0] == '0' && (d[1] == 'x' || d[1] == 'X')) d = d.substr(2);
        if (d.empty() || d.size() > 8) return false;
        v = 0;
        for (char c : d)
        {
            if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
            v = (v << 4) | static_cast<uint32_t>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (std::tolower(c) - 'a' + 10));
        }
        return true;
    };
    size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && (text[i] == ',' || std::isspace(static_cast<unsigned char>(text[i])))) ++i;
        size_t j = i;
        while (j < text.size() && text[j] != ',' && !std::isspace(static_cast<unsigned char>(text[j]))) ++j;
        if (j == i) break;
        const std::string entry = text.substr(i, j - i);
        const size_t dash = entry.find('-');
        uint32_t lo = 0, hi = 0;
        const bool ok = dash == std::string::npos ? hex(entry, lo) && hex(entry, hi)
                                                  : hex(entry.substr(0, dash), lo) && hex(entry.substr(dash + 1), hi);
        if (!ok || hi < lo)
        {
            error = "\"" + entry + "\" is not an address or a range first-last";
            out.clear();
            return false;
        }
        out.emplace_back(lo, hi);
        i = j;
    }
    return true;
}

namespace
{

// The flow as first followed, before any action: where each memory op and call was, and the
// blocks' edges. The final blocks are joined (a block with one way out into one with one way in),
// split and copied, so the order two accesses are made in is read from here, not from op ids or
// addresses (review of 0e3cd12: both were wrong on joined blocks).
struct FlowRecord
{
    std::map<ghidra::uintm, std::pair<int, uint32_t>> ops;   // op time -> (block, position)
    // A volatile register access is a new op the engine makes where the LOAD or STORE was: found
    // again by its instruction and kind.
    std::map<std::pair<uint32_t, bool>, std::vector<std::pair<int, uint32_t>>> accessesByInsn;
    std::vector<std::vector<int>> successors;
    int entryBlock = 0;
};

FlowRecord RecordFlow(const ghidra::Funcdata& fd, const std::function<uint32_t(uint32_t)>& canonicalOf)
{
    FlowRecord f;
    const ghidra::BlockGraph& graph = fd.getBasicBlocks();
    f.successors.resize(static_cast<size_t>(graph.getSize()));
    // The blocks have no indices of their own yet (the engine numbers them later): by position.
    std::map<const ghidra::FlowBlock*, int> indexOf;
    for (ghidra::int4 i = 0; i < graph.getSize(); ++i) indexOf[graph.getBlock(i)] = i;
    for (ghidra::int4 i = 0; i < graph.getSize(); ++i)
    {
        const ghidra::FlowBlock* b = graph.getBlock(i);
        if (b->isEntryPoint()) f.entryBlock = i;
        for (ghidra::int4 k = 0; k < b->sizeOut(); ++k)
        {
            auto t = indexOf.find(b->getOut(k));
            if (t != indexOf.end()) f.successors[i].push_back(t->second);
        }
        const ghidra::BlockBasic* bb = static_cast<const ghidra::BlockBasic*>(b);
        uint32_t pos = 0;
        for (auto it = bb->beginOp(); it != bb->endOp(); ++it, ++pos)
        {
            const ghidra::PcodeOp* o = *it;
            const ghidra::OpCode c = o->code();
            if (c != ghidra::CPUI_LOAD && c != ghidra::CPUI_STORE && c != ghidra::CPUI_CALL && c != ghidra::CPUI_CALLIND) continue;
            f.ops[o->getTime()] = std::make_pair(i, pos);
            if (c == ghidra::CPUI_LOAD || c == ghidra::CPUI_STORE)
                f.accessesByInsn[std::make_pair(canonicalOf(static_cast<uint32_t>(o->getAddr().getOffset())), c == ghidra::CPUI_STORE)]
                    .emplace_back(i, pos);
        }
    }
    return f;
}

std::vector<CallSite> Calls(const ghidra::Funcdata& fd, const FlowRecord& flow, const std::function<uint32_t(uint32_t)>& canonicalOf)
{
    std::vector<CallSite> out;
    for (auto it = fd.beginOpAlive(); it != fd.endOpAlive(); ++it)
    {
        const ghidra::PcodeOp* o = *it;
        if (o->code() != ghidra::CPUI_CALL && o->code() != ghidra::CPUI_CALLIND) continue;
        CallSite c;
        c.insn = canonicalOf(static_cast<uint32_t>(o->getAddr().getOffset()));
        if (o->getParent()) c.block = o->getParent()->getIndex();
        c.order = static_cast<uint32_t>(o->getSeqNum().getOrder());
        c.id = static_cast<uint32_t>(o->getTime()) + 1;
        auto f = flow.ops.find(o->getTime());
        if (f != flow.ops.end())
        {
            c.origBlock = f->second.first;
            c.origOrder = f->second.second;
        }
        out.push_back(c);
    }
    return out;
}

// The access ledger (MemoryAccess), from the final p-code and what was printed of it.
std::vector<MemoryAccess> Accesses(const ghidra::Funcdata& fd,
                                   const std::map<const ghidra::PcodeOp*, std::pair<std::set<size_t>, int>>& printed,
                                   const std::function<uint32_t(uint32_t)>& canonicalOf, const FlowRecord& flow)
{
    // The values returned, traced back through what passes a value on unchanged.
    std::set<const ghidra::Varnode*> returnedVns;
    std::vector<const ghidra::Varnode*> work;
    for (auto it = fd.beginOp(ghidra::CPUI_RETURN); it != fd.endOp(ghidra::CPUI_RETURN); ++it)
        if (!(*it)->isDead() && (*it)->numInput() > 1) work.push_back((*it)->getIn(1));
    while (!work.empty())
    {
        const ghidra::Varnode* vn = work.back();
        work.pop_back();
        if (!returnedVns.insert(vn).second || !vn->isWritten()) continue;
        const ghidra::PcodeOp* d = vn->getDef();
        switch (d->code())
        {
        case ghidra::CPUI_COPY:
        case ghidra::CPUI_INT_ZEXT:
        case ghidra::CPUI_INT_SEXT:
        case ghidra::CPUI_SUBPIECE:
        case ghidra::CPUI_CAST:
            work.push_back(d->getIn(0));
            break;
        case ghidra::CPUI_MULTIEQUAL:
            for (int i = 0; i < d->numInput(); ++i) work.push_back(d->getIn(i));
            break;
        case ghidra::CPUI_INDIRECT:
            work.push_back(d->getIn(0));
            break;
        default:
            break;
        }
    }
    // How many times the printer emits a value: once where it is explicit (a statement or a
    // variable), at every use where it is implied -- the engine prints an implied expression in
    // place at each of its uses, so a read inside one is printed, and appears to happen, that often.
    std::map<const ghidra::Varnode*, int> memo;
    std::function<int(const ghidra::Varnode*)> emissions = [&](const ghidra::Varnode* vn) -> int {
        if (!vn->isImplied()) return 1;
        auto m = memo.find(vn);
        if (m != memo.end()) return m->second;
        memo[vn] = 1;   // a cycle cannot be implied; this only bounds a malformed graph
        int n = 0;
        for (auto d = vn->beginDescend(); d != vn->endDescend(); ++d)
        {
            const ghidra::PcodeOp* use = *d;
            if (use->isDead()) continue;
            const ghidra::Varnode* out = use->getOut();
            n += out ? emissions(out) : 1;
        }
        memo[vn] = n;
        return n;
    };
    auto inLoop = [](const ghidra::FlowBlock* b) {
        std::set<const ghidra::FlowBlock*> seen;
        std::vector<const ghidra::FlowBlock*> todo;
        for (int i = 0; i < b->sizeOut(); ++i) todo.push_back(b->getOut(i));
        while (!todo.empty())
        {
            const ghidra::FlowBlock* x = todo.back();
            todo.pop_back();
            if (x == b) return true;
            if (!seen.insert(x).second) continue;
            for (int i = 0; i < x->sizeOut(); ++i) todo.push_back(x->getOut(i));
        }
        return false;
    };
    std::vector<MemoryAccess> out;
    std::vector<const ghidra::PcodeOp*> outOps;
    for (auto it = fd.beginOpAlive(); it != fd.endOpAlive(); ++it)
    {
        const ghidra::PcodeOp* o = *it;
        MemoryAccess a;
        const ghidra::Varnode* value = nullptr;
        if (o->code() == ghidra::CPUI_LOAD || o->code() == ghidra::CPUI_STORE)
        {
            a.write = o->code() == ghidra::CPUI_STORE;
            value = a.write ? o->getIn(2) : o->getOut();
            if (o->getIn(1)->isConstant()) a.address = static_cast<uint32_t>(o->getIn(1)->getOffset());
        }
        else if (o->code() == ghidra::CPUI_CALLOTHER && o->numInput() >= 2 && o->getIn(0)->isConstant())
        {
            const ghidra::UserPcodeOp* u = fd.getArch()->userops.getOp(static_cast<ghidra::uint4>(o->getIn(0)->getOffset()));
            if (!u || (u->getType() != ghidra::UserPcodeOp::volatile_read && u->getType() != ghidra::UserPcodeOp::volatile_write))
                continue;
            a.write = u->getType() == ghidra::UserPcodeOp::volatile_write;
            a.direct = true;
            a.address = static_cast<uint32_t>(o->getIn(1)->getOffset());
            value = a.write ? (o->numInput() > 2 ? o->getIn(2) : nullptr) : o->getOut();
        }
        else
            continue;
        if (!value) continue;
        a.size = static_cast<uint8_t>(value->getSize());
        a.insn = canonicalOf(static_cast<uint32_t>(o->getAddr().getOffset()));
        a.inLoop = o->getParent() && inLoop(o->getParent());
        a.returned = !a.write && returnedVns.count(value) != 0;
        auto p = printed.find(o);
        // A read in array form (p[i]) prints no token of its own: the index expression's op prints
        // the brackets. Its lines are then the pointer expression's.
        if (p == printed.end() && !a.write && o->code() == ghidra::CPUI_LOAD && o->getIn(1)->isWritten() &&
            o->getIn(1)->isImplied())
            p = printed.find(o->getIn(1)->getDef());
        if (p != printed.end())
        {
            a.printedLines = static_cast<int>(p->second.first.size());
            if (!p->second.first.empty()) a.firstLine = static_cast<int>(*p->second.first.begin());
            a.printedTokens = p->second.second;
        }
        // A write is its own statement; a read is printed as often as its value is emitted. (Its
        // tokens alone cannot say: p[2] prints the load through the pointer arithmetic's tokens.)
        a.printed = a.write || !o->getOut() ? 1 : emissions(o->getOut());
        {
            const ghidra::PcodeOp* at = o;
            for (const ghidra::Varnode* v = a.write ? nullptr : o->getOut(); v && v->isImplied();)
            {
                const ghidra::PcodeOp* use = v->loneDescend();
                if (!use) break;
                if ((use->code() == ghidra::CPUI_BOOL_AND || use->code() == ghidra::CPUI_BOOL_OR) && use->getSlot(v) == 1)
                    a.conditionalInPrint = true;
                // ...or printed in another block, which runs only on the paths that reach it.
                if (use->getParent() != o->getParent()) a.conditionalInPrint = true;
                at = use;
                a.through.push_back(static_cast<uint32_t>(use->getTime()) + 1);
                // A call's argument is printed in the call: the chain ends there, whatever is done
                // with the call's result.
                if (use->isCall()) break;
                v = use->getOut();
            }
            a.statement = static_cast<uint32_t>(at->getTime()) + 1;
            a.id = static_cast<uint32_t>(o->getTime()) + 1;
            if (o->getParent()) a.block = o->getParent()->getIndex();
            if (at->getParent()) a.statementBlock = at->getParent()->getIndex();
            a.order = static_cast<uint32_t>(o->getSeqNum().getOrder());
            a.statementOrder = static_cast<uint32_t>(at->getSeqNum().getOrder());
        }
        if (o->getParent() && o->getParent()->lastOp() && o->getParent()->lastOp()->code() == ghidra::CPUI_BRANCHIND)
            a.inSwitchBlock = true;
        // Where it was in the flow: a LOAD or STORE is the flow's own op; a register access is a
        // new op, found by its instruction and kind when that names one position.
        {
            auto f = flow.ops.find(o->getTime());
            if (f != flow.ops.end() && !a.direct)
            {
                a.origBlock = f->second.first;
                a.origOrder = f->second.second;
            }
            else if (a.direct)
            {
                auto g = flow.accessesByInsn.find(std::make_pair(a.insn, a.write));
                if (g != flow.accessesByInsn.end() && g->second.size() == 1)
                {
                    a.origBlock = g->second[0].first;
                    a.origOrder = g->second[0].second;
                }
            }
        }
        out.push_back(a);
        outOps.push_back(o);
    }
    // Copies of one instruction's access: a copy that another can reach -- through the blocks, or
    // later in the same block -- would run twice on that path.
    auto reaches = [](const ghidra::PcodeOp* from, const ghidra::PcodeOp* to) {
        const ghidra::FlowBlock* a = from->getParent();
        const ghidra::FlowBlock* b = to->getParent();
        if (a == b && from->getSeqNum().getOrder() < to->getSeqNum().getOrder()) return true;
        std::set<const ghidra::FlowBlock*> seen;
        std::vector<const ghidra::FlowBlock*> todo;
        for (int i = 0; i < a->sizeOut(); ++i) todo.push_back(a->getOut(i));
        while (!todo.empty())
        {
            const ghidra::FlowBlock* x = todo.back();
            todo.pop_back();
            if (x == b) return true;
            if (!seen.insert(x).second) continue;
            for (int i = 0; i < x->sizeOut(); ++i) todo.push_back(x->getOut(i));
        }
        return false;
    };
    // An instruction can make several accesses of one kind -- mac's two operand reads, the two
    // stack pops of an rte beside the read in its slot -- and the flow had a LOAD or STORE for each.
    // One of them reaching another is then no copy: 'made' is how many the flow had, less those that
    // ended some other way (folded into a variable, removed), and a path with that many before
    // an access holds one too many (A5-6; before, any two of a kind flagged mac's operands).
    std::map<std::pair<uint32_t, bool>, int> made;
    for (const auto& k : flow.accessesByInsn) made[k.first] = static_cast<int>(k.second.size());
    for (const ghidra::AccessRecord& rec : fd.getAccessFates())
        --made[std::make_pair(canonicalOf(static_cast<uint32_t>(rec.pc.getOffset())), rec.store)];
    for (size_t j = 0; j < out.size(); ++j)
    {
        if (!outOps[j]->getParent()) continue;
        const int allowed = std::max(1, made[std::make_pair(out[j].insn, out[j].write)]);
        std::vector<size_t> before;
        for (size_t i = 0; i < out.size(); ++i)
            if (i != j && out[i].insn == out[j].insn && out[i].write == out[j].write && outOps[i]->getParent() &&
                reaches(outOps[i], outOps[j]))
                before.push_back(i);
        if (static_cast<int>(before.size()) < allowed) continue;
        out[j].repeatedOnPath = true;
        for (size_t i : before) out[i].repeatedOnPath = true;
    }
    return out;
}

}  // namespace

std::string DecompileResult::Text() const
{
    std::string s;
    for (const Line& l : lines)
    {
        s += l.text;
        s += '\n';
    }
    return s;
}

std::vector<uint32_t> LineInstructions(const Line& line)
{
    std::vector<uint32_t> at;
    for (const Token& t : line.tokens)
        if (t.addr) at.push_back(t.addr);
    std::sort(at.begin(), at.end());
    at.erase(std::unique(at.begin(), at.end()), at.end());
    return at;
}

std::string LineOriginText(const Line& line)
{
    const std::vector<uint32_t> at = LineInstructions(line);
    if (at.empty()) return std::string();
    // The click and the menu go to the lowest address (Line::firstAddr), the one named first.
    std::string s;
    if (at.size() == 1)
    {
        s = "Decompiled from the SH-2 instruction at " + Hex8(at[0]) + " (an address in the program, not a value in this line).\n"
            "Click to show it in SH-2 Assembly.";
        return s;
    }
    s = "Decompiled from " + std::to_string(at.size()) + " SH-2 instructions (an address is not a value in this line):\n";
    const size_t kListed = 8;
    for (size_t i = 0; i < at.size() && i < kListed; ++i)
    {
        if (i) s += ", ";
        s += Hex8(at[i]);
    }
    if (at.size() > kListed) s += ", and " + std::to_string(at.size() - kListed) + " more";
    s += "\nClick to show the first, " + Hex8(at[0]) + ", in SH-2 Assembly.";
    return s;
}

bool DecompileResult::InBody(uint32_t canonical) const
{
    auto it = std::upper_bound(body.begin(), body.end(), std::make_pair(canonical, ~0u));
    return it != body.begin() && canonical < std::prev(it)->second;
}

namespace
{

// The instructions flow reached, as merged [lo, hi) ranges. 'insns' holds every instruction the
// flow translated (patch 0021); a delay slot is translated with its branch, so it is added here,
// and a run of nops after an instruction that falls through is kept in case a nop was not recorded.
// The caller leaves out the instructions flow halted at (memory not there, or not decoding), and a
// delay slot is added only when it decodes.
std::vector<std::pair<uint32_t, uint32_t>> BodyRanges(const std::set<uint32_t>& insns, const MemorySnapshot& snap)
{
    std::set<uint32_t> all;
    for (uint32_t a : insns)
    {
        uint16_t op = 0;
        if (!snap.ReadU16(a, op)) continue;
        const DisassembledInstruction ins = Sh2Decode(a, op);
        if (!ins.IsValid) continue;
        all.insert(a);
        uint32_t next = a + 2;
        if (ins.HasDelaySlot)
        {
            // The slot ran only if it is there and decodes (the engine halts the pair otherwise).
            uint16_t slot = 0;
            if (!snap.ReadU16(a + 2, slot) || !Sh2Decode(a + 2, slot).IsValid) continue;
            all.insert(a + 2);
            next = a + 4;
        }
        // A fall-through run of nops: flow went through them without p-code.
        if (ins.IsReturn || (ins.IsBranch && !ins.IsConditional && !ins.IsCall)) continue;
        for (uint16_t w = 0; insns.count(next) == 0 && snap.ReadU16(next, w) && w == 0x0009; next += 2)
            all.insert(next);
    }
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    for (uint32_t a : all)
    {
        uint32_t c = 0;
        if (!CanonicalAddress(a, c)) continue;
        if (!ranges.empty() && ranges.back().second == c) ranges.back().second = c + 2;
        else ranges.emplace_back(c, c + 2);
    }
    std::sort(ranges.begin(), ranges.end());
    std::vector<std::pair<uint32_t, uint32_t>> merged;
    for (const auto& r : ranges)
        if (!merged.empty() && r.first <= merged.back().second) merged.back().second = std::max(merged.back().second, r.second);
        else merged.push_back(r);
    return merged;
}

}  // namespace

Sh2Decompiler::Sh2Decompiler(std::string configDir) : mConfigDir(std::move(configDir))
{
}

// The one exception boundary: nothing thrown while decompiling -- an engine error, a cancel, an
// allocation failure anywhere, including preparing the request -- leaves this function.
DecompileResult Sh2Decompiler::Decompile(const std::shared_ptr<const MemorySnapshot>& snapshot,
                                         const DecompileRequest& request, std::atomic<bool>* cancel)
{
    const Clock::time_point t0 = Clock::now();
    DecompileResult r;
    try
    {
        r.key = snapshot->key;
        r.requestedEntry = request.entry;
        PassSeed seed;
        DecompileInto(snapshot, request, cancel, r, nullptr, &seed);
        if (r.status == DecompileStatus::Ok && !request.allMemoryVolatile) NoteRemovedAccesses(snapshot, request, cancel, r, seed);
    }
    catch (ghidra::CancelError&)
    {
        r.status = DecompileStatus::Cancelled;
        r.cancelledIn = mWorkPhase;
        r.lines.clear();
        SetError(r, "cancelled");
    }
    catch (ghidra::LowlevelError& e)
    {
        r.status = DecompileStatus::Failed;
        r.lines.clear();
        SetError(r, e.explain);
    }
    catch (std::bad_alloc&)
    {
        r.status = DecompileStatus::Failed;
        r.lines.clear();
        if (mWorkPhase != DecompilePhase::Idle) gEngineDamaged = true;   // inside the engine
        SetError(r, "out of memory");
    }
    catch (std::exception& e)
    {
        r.status = DecompileStatus::Failed;
        r.lines.clear();
        if (mWorkPhase != DecompilePhase::Idle) gEngineDamaged = true;
        SetError(r, e.what());
    }
    catch (...)
    {
        r.status = DecompileStatus::Failed;
        r.lines.clear();
        if (mWorkPhase != DecompilePhase::Idle) gEngineDamaged = true;
        SetError(r, "unexpected exception");
    }
    mWorkPhase = DecompilePhase::Idle;
    mQuietPhases = false;   // however the reference run ended
    r.elapsedMs = MsSince(t0);
    return r;
}

// Code the analysis removed as unreachable is gone from the output, hardware accesses included.
// The usual cause is a test of memory not marked volatile that the function itself wrote (a
// mailbox for the other SH-2 or the sound CPU): the write is forwarded to the read and the branch
// is decided -- a poll never exits, a check always goes one way. The engine records how each of
// the flow's loads and stores ended (patch 0017); when any of them ended with an unreachable block,
// the function is analysed once more with every address volatile (nothing folds, so every branch
// stays open and every access the flow reaches stays), with the literals and callees the shown pass
// followed its flow with, so that both follow the same flow (a switch bounded by a literal only the
// second pass folds is reached by both). Each access of that run that the shown output lacks, and
// whose instruction's load or store was removed with unreachable code, and that is a register,
// memory the request marks, or through a pointer -- whatever the pointer came from: it is missing
// because its code was removed, not because it folded -- is said, on its instruction and in the
// warnings, with the cause. The signature is withheld: it describes a function that stops at the fold. Should the reference run not finish (an engine error, a cancel,
// the time budget), the removal is said without its contents and the signature is still withheld;
// an allocation failure inside the engine leaves this function like any other, marking the engine
// damaged (review of 0e3cd12, from the ledger's reference run on game code; review of 0c2dd20).
void Sh2Decompiler::NoteRemovedAccesses(const std::shared_ptr<const MemorySnapshot>& snapshot, const DecompileRequest& request,
                                        std::atomic<bool>* cancel, DecompileResult& r, const PassSeed& seed)
{
    bool removedCode = false;
    for (const AccessFate& f : r.accessFates)
        removedCode |= f.fate == AccessFate::Unreachable;
    if (!removedCode) return;
    bool loop = false;
    for (const std::string& w : r.warnings)
        if (w.find("Do nothing block with infinite loop") != std::string::npos) loop = true;
    if (gBeforeReferenceHookForTests) gBeforeReferenceHookForTests();
    DecompileRequest ref = request;
    ref.allMemoryVolatile = true;
    DecompileResult rr;
    std::string notDone;
    mQuietPhases = true;
    try
    {
        DecompileInto(snapshot, ref, cancel, rr, &seed);   // one pass, the shown pass's flow
    }
    catch (ghidra::CancelError&)
    {
        // The shown output is finished; only this check was cut short (the budget, or a stop).
        notDone = "cancelled";
    }
    catch (ghidra::LowlevelError& e)
    {
        notDone = e.explain;
    }
    // Anything else -- an allocation failure, an unexpected exception -- is the engine's state in
    // doubt: it leaves through Decompile's handlers, as from the shown run.
    mQuietPhases = false;
    if (notDone.empty() && rr.status != DecompileStatus::Ok) notDone = rr.error.empty() ? "no reference output" : rr.error;
    const std::string cause = loop ? "the analysis removed the code around it as unreachable, after a loop it takes to run for "
                                     "ever. A poll of memory another processor writes folds so unless that memory is marked volatile"
                                   : "the analysis removed the code around it as unreachable, after a branch it takes always the "
                                     "same way. A test of memory another processor writes is decided from this function's own "
                                     "write unless that memory is marked volatile";
    if (!notDone.empty())
    {
        r.warnings.push_back("code was removed as unreachable and could not be checked for hardware accesses (" + notDone +
                             "): " + cause);
        if (r.prototypeWithheld.empty()) r.prototypeWithheld = "code removed as unreachable was not checked for hardware accesses";
        r.prototype.known = false;
        return;
    }
    std::vector<HardwareRange> marked;
    for (const auto& v : request.volatileMemory)
        for (const HardwareRange& h : VolatileAliases(v.first, v.second)) marked.push_back(h);
    for (const auto& h : r.aliasedMemory) marked.push_back(HardwareRange{ h.first, h.second });
    auto isMarked = [&](uint32_t a) {
        for (const HardwareRange& h : marked)
            if (h.first <= a && a <= h.last) return true;
        return false;
    };
    // Each access of the reference run is matched with one thing the shown pass did with it, one to
    // one: a shown access (printed), or one of the engine's records of an access that went (folded
    // into a variable of ordinary memory, the read of a switch's table, removed with unreachable
    // code). They are found by instruction, direction and width, and by address wherever both sides
    // have a constant one. Two accesses of one instruction -- mac's two operands, an rte's stack
    // pops beside the access in its slot -- would otherwise excuse one another: the access of
    // ordinary memory that folded accounted for the register read that was removed (A5-5). Matches
    // by address come first, so an access through a pointer does not take the record of a constant
    // one. A fold into the function's own frame is a variable, not a bus access: no match.
    enum Outcome : uint8_t { Unmatched, Printed, Conserved, Removed };
    std::vector<Outcome> outcome(rr.accesses.size(), Unmatched);
    std::vector<char> shownUsed(r.accesses.size(), 0), fateUsed(r.accessFates.size(), 0);
    auto fits = [](bool mKnown, uint32_t mAddress, bool known, uint32_t address, int pass) {
        const bool both = mKnown && known;
        return pass == 0 ? both && mAddress == address : !both;
    };
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < rr.accesses.size(); ++i)
        {
            const MemoryAccess& m = rr.accesses[i];
            if (outcome[i] != Unmatched) continue;
            for (size_t k = 0; k < r.accesses.size() && outcome[i] == Unmatched; ++k)
            {
                const MemoryAccess& x = r.accesses[k];
                if (shownUsed[k] || x.insn != m.insn || x.write != m.write || x.size != m.size) continue;
                if (!fits(m.direct, m.address, x.direct, x.address, pass)) continue;
                shownUsed[k] = 1;
                outcome[i] = Printed;
            }
            // Conserved before removed: a record that says the access was only a variable.
            for (int want = 0; want < 2 && outcome[i] == Unmatched; ++want)
                for (size_t k = 0; k < r.accessFates.size() && outcome[i] == Unmatched; ++k)
                {
                    const AccessFate& f = r.accessFates[k];
                    if (fateUsed[k] || f.frame || f.insn != m.insn || f.write != m.write || f.size != m.size) continue;
                    const bool conserves = f.fate == AccessFate::FoldedOrdinary || f.fate == AccessFate::SwitchLookup;
                    if (want == 0 ? !conserves : f.fate != AccessFate::Unreachable) continue;
                    if (!fits(m.direct, m.address, f.constantAddress, f.address, pass)) continue;
                    fateUsed[k] = 1;
                    outcome[i] = want == 0 ? Conserved : Removed;
                }
        }
    bool any = false;
    for (size_t i = 0; i < rr.accesses.size(); ++i)
    {
        const MemoryAccess& m = rr.accesses[i];
        if (m.direct && !InHardwareRange(m.address) && !isMarked(m.address)) continue;   // ordinary memory
        // Removed with unreachable code is what is said here. Printed, folded into a variable or a
        // switch, or ended some other way (no removal, no cause to give) is not.
        if (outcome[i] != Removed) continue;
        // Every register the access covers, and its width: a 4-byte write over two registers is
        // not the write of the first (A5-7).
        std::string what = " through a pointer";
        if (m.direct)
        {
            const std::vector<const HardwareRegister*> regs = HardwareRegistersCovering(m.address, m.size);
            what = " of " + (regs.empty() ? Hex8(m.address) : RegisterNames(regs));
        }
        const std::string text = "the " + std::to_string(m.size) + "-byte " + (m.write ? "write" : "read") + what +
                                 " here is missing: " + cause;
        r.hardwareNotes.push_back(HardwareNote{ m.insn, text, true });
        r.warnings.push_back(Hex8(m.insn) + ": " + text);
        any = true;
    }
    if (!any) return;
    std::stable_sort(r.hardwareNotes.begin(), r.hardwareNotes.end(),
                     [](const HardwareNote& x, const HardwareNote& y) { return x.address < y.address; });
    if (r.prototypeWithheld.empty()) r.prototypeWithheld = "code removed as unreachable holds hardware accesses";
    r.prototype.known = false;
}

namespace
{

// An address in the partitions Mednafen sends to the associative purge (sh7095.inc MemRead / MemWrite `case 2: case 5:`; the SH7604 reserves
// the second, A5-62).
bool IsPurgePartition(uint32_t address)
{
    return (address >> 29) == 2 || (address >> 29) == 5;
}

// One access of ordinary memory at a constant address, as the actions left it. 'bus' is where it lands on the bus (the address, or for an
// associative purge the cached address it purges), 'size' its width.
struct ConstAccess
{
    uint32_t address = 0;
    uint32_t bus = 0;
    uint32_t size = 0;
    // The bytes the access covers on the bus, when they are not its own (MemoryBytesOf of 'bus' and this): an associative purge invalidates the
    // 16-byte line of its address, whatever word it names (sh7095.inc SH7095_Cache_AssocPurge: the tag of the line is compared, A5-72).
    uint32_t busSize = 0;
    bool store = false;
    // A write to a register that changes how a memory is addressed from then on (A5-68): bit 0 VDP1_TVMR (the frame buffer's 8 bpp rotate
    // scramble), bit 1 VDP2_RAMCTL (the colour RAM mode), bit 2 VDP1_FBCR (the frame change: after it the same frame buffer address is the other
    // buffer, vdp1.c `FBDrawWhich = !FBDrawWhich`, A5-79). Not an access of any memory; the evidence that the function re-addresses them.
    uint8_t remaps = 0;
    bool operator<(const ConstAccess& o) const { return std::tie(address, size, store) < std::tie(o.address, o.size, o.store); }
};

// The constant-address accesses of the function that are ordinary memory to the engine (it folded them into variables, patch 0017's
// record) and the associative purges of work RAM, which are volatile accesses of a word the function also treats as memory: a purge says
// another processor is expected to have written it (the slave's idle loop purges its mailbox and polls it).
std::vector<ConstAccess> ConstantAccesses(const ghidra::Funcdata& fd, const ghidra::AddrSpace* ram)
{
    std::vector<ConstAccess> out;
    for (const ghidra::AccessRecord& rec : fd.getAccessFates())
    {
        if (rec.fate != ghidra::fate_folded_ordinary || rec.addr.isInvalid() || rec.addr.getSpace() != ram) continue;
        ConstAccess c;
        c.address = c.bus = static_cast<uint32_t>(rec.addr.getOffset());
        c.size = static_cast<uint32_t>(rec.size);
        c.store = rec.store;
        out.push_back(c);
    }
    for (auto it = fd.beginOpAlive(); it != fd.endOpAlive(); ++it)
    {
        const ghidra::PcodeOp* o = *it;
        if (o->code() != ghidra::CPUI_CALLOTHER || o->numInput() < 2 || !o->getIn(0)->isConstant()) continue;
        const ghidra::UserPcodeOp* u = fd.getArch()->userops.getOp(static_cast<ghidra::uint4>(o->getIn(0)->getOffset()));
        if (!u || (u->getType() != ghidra::UserPcodeOp::volatile_read && u->getType() != ghidra::UserPcodeOp::volatile_write)) continue;
        const uint32_t address = static_cast<uint32_t>(o->getIn(1)->getOffset());
        if (u->getType() == ghidra::UserPcodeOp::volatile_write && o->numInput() > 2)
        {
            const uint32_t width = static_cast<uint32_t>(o->getIn(2)->getSize());
            uint8_t remaps = 0;
            for (const HardwareRegister* reg : HardwareRegistersCovering(address, width))
                remaps |= reg->address == 0x05D00000u ? 1 : reg->address == 0x05F8000Eu ? 2 : reg->address == 0x05D00002u ? 4 : 0;
            if (remaps)
            {
                ConstAccess c;
                c.address = c.bus = address;
                c.size = width;
                c.store = true;
                c.remaps = remaps;
                out.push_back(c);
                continue;
            }
        }
        if (!IsPurgePartition(address)) continue;   // 0x40000000-0x5FFFFFFF and 0xA0000000-0xBFFFFFFF: Mednafen's associative purge (sh7095.inc cases 2 and 5, A5-62)
        const bool store = u->getType() == ghidra::UserPcodeOp::volatile_write;
        const ghidra::Varnode* value = store ? (o->numInput() > 2 ? o->getIn(2) : nullptr) : o->getOut();
        if (!value) continue;
        ConstAccess c;
        c.address = address;
        c.bus = address & 0x07FFFFF0u;   // the line it purges, as the cached partition names it (A5-72: the whole line, not the word named)
        c.busSize = 16;
        c.size = static_cast<uint32_t>(value->getSize());
        c.store = store;
        out.push_back(c);
    }
    return out;
}

// The accesses among 'seen' and 'known' that reach memory another address of the function reaches too, not yet in 'known': two accesses
// that cover a byte of one memory in common (MemoryBytesOf) at addresses whose bytes do not overlap, so that the data flow takes them for
// two memories (A5-52, A5-53); and every byte store to colour RAM, which stores the whole entry (A5-50). They are appended to 'known',
// so a later pass counts them too, and their ranges (outward to whole longwords, as VolatileAliases does) are returned.
std::vector<HardwareRange> NewAliasedRanges(const std::vector<ConstAccess>& seen, std::vector<ConstAccess>& known)
{
    struct Member
    {
        ConstAccess access;
        MemoryBytes bytes;
        bool flagged = false;
        bool isKnown = false;
    };
    std::vector<Member> members;
    std::set<ConstAccess> have;
    // The function writes VDP1_TVMR, VDP2_RAMCTL or VDP1_FBCR: the memory that register re-addresses is not the memory the data flow keys by
    // address after the write. A store before it and a load after it are not the same bytes (the mode 3 colour RAM reads another entry; after a
    // frame change the frame buffer address is the other buffer), so every access the function makes to that memory is volatile (A5-68, A5-79).
    // (Seen as accesses of the register in 'seen' or 'known'.) A frame change with no FBCR write in the function (the automatic one, FCM clear)
    // has no position to hang this on: that is the user's volatile range.
    uint8_t remaps = 0;
    for (const ConstAccess& c : known) remaps |= c.remaps;
    for (const ConstAccess& c : seen) remaps |= c.remaps;
    auto add = [&](const ConstAccess& c, bool isKnown) {
        if (!have.insert(c).second) return;
        Member m;
        m.access = c;
        m.isKnown = isKnown;
        if (!MemoryBytesOf(c.bus, c.busSize ? c.busSize : c.size, m.bytes)) return;
        members.push_back(m);
    };
    for (const ConstAccess& c : known) add(c, true);
    for (const ConstAccess& c : seen) add(c, false);
    std::sort(members.begin(), members.end(), [](const Member& x, const Member& y) {
        return std::tie(x.bytes.kind, x.bytes.first, x.bytes.last) < std::tie(y.bytes.kind, y.bytes.first, y.bytes.last);
    });
    auto overlap = [](uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1) { return a0 <= b1 && b0 <= a1; };
    for (size_t i = 0; i < members.size(); ++i)
    {
        Member& mi = members[i];
        if (mi.bytes.kind == MemoryKind::Vdp2ColorRam && mi.access.store && mi.access.size == 1) mi.flagged = true;
        if ((mi.bytes.kind == MemoryKind::Vdp1FrameBuffer && (remaps & 5)) || (mi.bytes.kind == MemoryKind::Vdp2ColorRam && (remaps & 2)))
            mi.flagged = true;
        for (size_t j = i + 1; j < members.size(); ++j)
        {
            Member& mj = members[j];
            if (mj.bytes.kind != mi.bytes.kind || mj.bytes.first > mi.bytes.last) break;   // sorted by first byte: nothing later overlaps
            if (!overlap(mi.bytes.first, mi.bytes.last, mj.bytes.first, mj.bytes.last)) continue;
            // The same bytes under addresses that do not overlap: the engine keys them apart.
            if (overlap(mi.access.address, mi.access.address + mi.access.size - 1, mj.access.address, mj.access.address + mj.access.size - 1)) continue;
            mi.flagged = mj.flagged = true;
        }
    }
    std::vector<HardwareRange> out;
    for (const Member& m : members)
    {
        if (!m.flagged || m.isKnown) continue;
        known.push_back(m.access);
        // (a purge is volatile already; it is only the evidence)
        if (m.access.address != m.access.bus && IsPurgePartition(m.access.address)) continue;
        out.push_back(HardwareRange{ m.access.address & ~3u, (m.access.address + m.access.size - 1) | 3u });
    }
    return out;
}

}  // namespace

void Sh2Decompiler::DecompileInto(const std::shared_ptr<const MemorySnapshot>& snapshot,
                                  const DecompileRequest& request, std::atomic<bool>* cancel, DecompileResult& r,
                                  const PassSeed* seedIn, PassSeed* seedOut)
{
    std::string why;
    uint32_t entry = 0;
    uint8_t probe[2];
    // An SH-2 function starts at an even address: at an odd one the fetch is an address error, and what flow would decode there is the middle of
    // the instruction before it (A7-6: a literal that is no function, a typed entry).
    if ((request.entry & 1u) != 0)
    {
        r.status = DecompileStatus::NoCode;
        r.entry = CanonicalAddress(request.entry, entry, &why) ? entry : request.entry;
        r.error = "no code at " + Hex8(request.entry) + ": an SH-2 instruction starts at an even address";
        return;
    }
    if (!CanonicalAddress(request.entry, entry, &why) || !snapshot->Read(entry, probe, 2, &why))
    {
        r.status = DecompileStatus::NoCode;
        r.entry = entry;
        r.error = "no code at " + Hex8(request.entry) + ": " + why;
        return;
    }
    r.entry = entry;
    // The function is analysed at the address it executes at, not the canonical one: the bytes are
    // the same, but PC-derived values are not -- a mova through the cache-through mirror yields a
    // 0x2xxxxxxx pointer. Its identity (name, prototype, what the result reports) stays canonical.
    const uint32_t exec = request.entry;
    auto canonicalOf = [](uint32_t a) {
        uint32_t c = 0;
        return CanonicalAddress(a, c) ? c : a;
    };

    if (gEngineDamaged)
    {
        r.status = DecompileStatus::Unavailable;
        r.error = kEngineDamaged;
        return;
    }
    if (!SaturnArchitecture::StartLibrary(mConfigDir, r.error))
    {
        r.status = DecompileStatus::Unavailable;
        return;
    }

    // Names first, so a FunctionNames rename shows; then the other known entries.
    std::map<uint32_t, std::string> named;
    for (const auto& n : request.names)
    {
        uint32_t c = 0;
        if (!n.second.empty() && CanonicalAddress(n.first, c)) named[c] = n.second;
    }
    // By the address each callee runs at, exactly: the same bytes through another mirror can take
    // other arguments (a mova-derived pointer), so a signature is not shared across mirrors.
    std::map<uint32_t, Prototype> prototypes;
    for (const auto& p : request.prototypes)
    {
        uint32_t c = 0;
        if (p.second.known && CanonicalAddress(p.first, c)) prototypes[p.first] = p.second;
    }
    std::set<uint32_t> known;
    for (uint32_t k : request.knownFunctions)
    {
        uint32_t c = 0;
        if (CanonicalAddress(k, c)) known.insert(c);
    }

    auto checkCancel = [&] {
        if (cancel && cancel->load()) throw ghidra::CancelError("cancelled");
    };

    // The literal words an earlier pass found. Jump tables are recovered during flow, before this
    // pass's own scan can mark its literals read-only, so a switch bound or a jmp target loaded from
    // a literal pool would not fold; the second pass marks the first pass's literals before flow.
    std::vector<std::pair<uint32_t, int>> knownLiterals;
    // The user's volatile memory, every alias, and the literal words in it: those stay reads.
    // A literal is folded from the captured bytes only when it is read-only, and read-only wins
    // over volatile in the engine (ActionVarnodeProps), so a literal another processor may change
    // must not be marked read-only.
    std::vector<HardwareRange> userVolatile;
    for (const auto& v : request.volatileMemory)
    {
        if (v.second < v.first)
        {
            r.warnings.push_back("volatile range " + Hex8(v.first) + "-" + Hex8(v.second) + " is inverted: ignored");
            continue;
        }
        for (const HardwareRange& h : VolatileAliases(v.first, v.second)) userVolatile.push_back(h);
    }
    auto sharedLiteral = [&](uint32_t at, int size) {
        const uint32_t end = at + static_cast<uint32_t>(size) - 1;
        if (InHardwareRange(at) || InHardwareRange(end)) return true;
        for (const HardwareRange& v : userVolatile)
            if (at <= v.last && end >= v.first) return true;
        return false;
    };

    // Ordinary memory the function reaches under more than one address (A5-52, A5-53), found after a pass's actions from the accesses the
    // engine folded into variables, and made volatile for the passes after it: the engine keys memory by address, and would forward a store
    // past a write to the other name of the same word. Found in a pass that prints, it makes that pass give way to another.
    std::vector<ConstAccess> aliasKnown;
    std::vector<HardwareRange> aliasRanges;
    bool aliasRetry = false;
    constexpr int kAliasRounds = 6;

    // One pass: a fresh Architecture, every function registered, flow followed, read-only
    // literals applied, actions run. 'print' adds the markup output; 'mayRetry' lets a print pass stop, before it prints, when the
    // actions showed memory with two names that is not yet volatile ('aliasRetry').
    auto runPass = [&](const CalleeAliases& callees, bool print, CalleeAliases* targets, bool mayRetry) {
        Enter(DecompilePhase::Init);
        std::ostringstream errors;
        errors.exceptions(std::ios::badbit);   // an allocation failure must not just cut the text short
        std::unique_ptr<SaturnArchitecture> arch = BuildArchitecture(snapshot, request.maxInstructions, cancel, errors);
        // Declared after the Architecture so it is destroyed first: Teardown is reported as the
        // pass's Architecture starts to go, whichever way the pass ends.
        struct TeardownNote
        {
            Sh2Decompiler* self;
            ~TeardownNote()
            {
                // A destructor, run while an exception may be unwinding: an observer that throws
                // here must not terminate the process.
                try
                {
                    self->Enter(DecompilePhase::Teardown);
                }
                catch (...)
                {
                }
            }
        } teardownNote{ this };
        ghidra::AddrSpace* ram = arch->getDefaultCodeSpace();
        ghidra::Scope* global = arch->symboltab->getGlobalScope();

        std::set<uint32_t> all(known);
        for (const auto& c : callees) all.insert(c.first);
        for (const auto& n : named) all.insert(n.first);
        all.insert(exec);
        ghidra::Funcdata* fd = nullptr;
        for (uint32_t a : all)
        {
            const ghidra::Address addr(ram, a);
            const uint32_t key = a == exec ? entry : a;   // every other address here is canonical
            std::string name;
            auto f = named.find(key);
            if (f != named.end()) name = f->second;
            else if (InBios(*snapshot, key)) name = BiosFunctionName(key);
            else arch->nameFunction(ghidra::Address(ram, key), name);
            // A call through a mirror (0x26005000 for 0x06005000) resolves to the address it
            // names, so the function is registered there too, under the canonical name.
            auto aliases = callees.find(a);
            if (aliases != callees.end())
                for (uint32_t alias : aliases->second)
                {
                    if (alias == a || alias == exec) continue;   // registered in its own right
                    try
                    {
                        ghidra::Funcdata* af = global->addFunction(ghidra::Address(ram, alias), name)->getFunction();
                        // The call resolves to the alias's function object, so it needs the lent
                        // signature as much as the canonical one does.
                        auto proto = prototypes.find(alias);
                        if (proto != prototypes.end()) ApplyPrototype(*arch, *af, proto->second);
                    }
                    catch (ghidra::LowlevelError& e)
                    {
                        r.warnings.push_back("could not register the mirror " + Hex8(alias) + " of " + Hex8(a) + ": " + e.explain);
                    }
                }
            try
            {
                ghidra::Funcdata* f2 = global->addFunction(addr, name)->getFunction();
                if (a == exec) fd = f2;
                // A callee decompiled before lends its signature; the function being decompiled
                // keeps the engine's own recovery.
                auto proto = prototypes.find(a);
                if (a != exec && proto != prototypes.end()) ApplyPrototype(*arch, *f2, proto->second);
                if (a != exec && gNoReturnForTests.count(a)) f2->getFuncProto().setNoReturn(true);
            }
            catch (ghidra::LowlevelError& e)
            {
                // A clashing user name, say. The function is still decompiled, under the
                // engine's own name if need be.
                r.warnings.push_back("could not register " + Hex8(a) + " as \"" + name + "\": " + e.explain);
                if (a == exec)
                {
                    arch->nameFunction(addr, name);
                    fd = global->addFunction(addr, name)->getFunction();
                }
            }
        }

        // The hardware registers by name, at both partitions code reaches them through, so a
        // folded pointer prints as VDP2_TVMD rather than *(ushort *)0x25f80000. Each is volatile:
        // to the engine a named global is an ordinary variable, and it would forward a write to the
        // next read, drop a write the next one overwrites and remove a read nobody uses -- a
        // status poll becomes an endless loop. A symbol's own flags win over a property range
        // (Scope::queryProperties), so the symbol needs the flag itself.
        size_t registerCount = 0;
        const HardwareRegister* registers = SaturnHardwareRegisters(registerCount);
        for (size_t i = 0; i < registerCount; ++i)
        {
            ghidra::Datatype* type = arch->types->getBase(registers[i].size, ghidra::TYPE_UINT);
            const uint32_t at = registers[i].address;
            const bool onChip = ClassifyPartition(at) == Sh2Partition::OnChip;
            for (uint32_t partition : { 0x00000000u, 0x20000000u })
            {
                if (onChip && partition) continue;
                ghidra::SymbolEntry* e = global->addSymbol(registers[i].name, type, ghidra::Address(ram, at | partition),
                                                           ghidra::Address());
                global->setAttribute(e->getSymbol(), ghidra::Varnode::volatil);
            }
        }
        // The SMPC's registers again as the 16-bit words they sit in, at the even address: a word
        // access there is a write of the register's low byte (ss.c), so it prints as the register,
        // not as work RAM at an address (A5-4). The byte symbol above is the smaller container, so
        // a byte access still finds that one.
        size_t viewCount = 0;
        const HardwareRegister* views = SaturnHardwareWordViews(viewCount);
        for (size_t i = 0; i < viewCount; ++i)
        {
            ghidra::Datatype* type = arch->types->getBase(views[i].size, ghidra::TYPE_UINT);
            for (uint32_t partition : { 0x00000000u, 0x20000000u })
            {
                ghidra::SymbolEntry* e = global->addSymbol(views[i].name, type, ghidra::Address(ram, views[i].address | partition),
                                                           ghidra::Address());
                global->setAttribute(e->getSymbol(), ghidra::Varnode::volatil);
            }
        }
        // ...and everything else in a register area, every alias included: an access wider than
        // one register (no symbol contains it, so the range decides), the unnamed ones (SCSP
        // slots, the FIFOs), and the mirrors code can reach a register through.
        for (const HardwareRange& h : HardwareRanges())
            arch->symboltab->setPropertyRange(ghidra::Varnode::volatil, ghidra::Range(ram, h.first, h.last));
        // The user's shared memory, through every alias that reaches it (VolatileAliases).
        for (const HardwareRange& v : userVolatile)
            arch->symboltab->setPropertyRange(ghidra::Varnode::volatil, ghidra::Range(ram, v.first, v.last));
        // The ledger's reference run: nothing folds but the literal pools (read-only, set below, wins).
        if (request.allMemoryVolatile)
            arch->symboltab->setPropertyRange(ghidra::Varnode::volatil, ghidra::Range(ram, 0, 0xFFFFFFFFu));
        // ...and none of it, nor any hardware, is a value the analysis may read from the snapshot:
        // jump-table recovery reads its table through the data image, and a table another
        // processor can rewrite is no fixed switch.
        {
            std::vector<std::pair<uint32_t, uint32_t>> noData;
            for (const HardwareRange& h : HardwareRanges()) noData.emplace_back(h.first, h.last);
            for (const HardwareRange& v : userVolatile) noData.emplace_back(v.first, v.last);
            arch->DataImage().SetVolatile(std::move(noData));
        }

        for (const auto& ro : knownLiterals)
            if (!sharedLiteral(ro.first, ro.second))
                arch->symboltab->setPropertyRange(ghidra::Varnode::readonly,
                                                  ghidra::Range(ram, ro.first, ro.first + ro.second - 1));
        Enter(DecompilePhase::Flow);
        fd->followFlow(ghidra::Address(ram, 0), ghidra::Address(ram, ram->getHighest()));
        if (fd->hasNoCode()) return std::string();
        const FlowRecord flow = RecordFlow(*fd, canonicalOf);

        const RawScan scan = ScanRawPcode(*fd, *snapshot);
        knownLiterals = scan.readonly;
        JumpDests flowTables;
        AddJumpTables(*fd, flowTables);
        // A pointer into the frame in r2 at a call the engine cannot give an input in r2 (A3-4): said to
        // the engine now, while the code that made it is still there to be found. (Only the pass that
        // prints: the first one learns who is called, and a function without a call has nothing to say.)
        std::map<uint32_t, uint32_t> escapeSites;   // calls the note is for, once the actions have said who is called
        if (print && fd->numCalls() > 0)
        {
            ReturnCheck probe(*fd, exec, prototypes, flowTables);
            probe.SetTolerant();
            probe.SetLiterals(scan.literalWords, *snapshot);
            probe.SetCancel(cancel);   // the callee walk can be long for an adversarial callee (A3-74)
            CallEffects unused;
            probe.Run(unused);
            uint32_t from = 0;
            if (probe.FrameEscape(from))
            {
                fd->getScopeLocal()->declareHostAlias(from);
                // ...and the text says where: the address is in no argument of the call, so a reader of `f(); return xStack_8;`
                // would take the local for one the callee cannot reach (A3-28). A comment at the call, of a type the function's own
                // flow warnings do not collect; put below, after the actions, when a `jsr @rN` through a literal is known for what it calls.
                escapeSites = probe.EscapeSites();
            }
        }
        for (const auto& ro : scan.readonly)
            if (!sharedLiteral(ro.first, ro.second))
                arch->symboltab->setPropertyRange(ghidra::Varnode::readonly,
                                              ghidra::Range(ram, ro.first, ro.first + ro.second - 1));

        // A check-point of ours at each phase boundary: the engine's last one in a phase can come
        // well before the phase ends (block generation after flow, the tail of the action tree),
        // and our own raw-p-code scan has none. A cancel requested anywhere in a phase is then
        // honoured in that phase, never carried into the next one's work.
        if (gBoundaryHookForTests) gBoundaryHookForTests();
        checkCancel();
        Enter(DecompilePhase::Actions);
        // Every memory access as the code makes it: as many, as wide. These cleanup rules rewrite
        // accesses to fit the data types -- one load or store split into several (a word store to
        // a byte pointer printed as two byte stores), two merged into one, a run of stores
        // collapsed into a string, a load widened to its pointer's type. Harmless for ordinary
        // memory; for a register, a different access (VDP2 refuses byte writes, a FIFO pops per
        // read). The access ledger tests pin this.
        for (const char* rule : { "splitload", "splitstore", "doubleload", "doublestore", "stringstore", "expandload" })
            if (!arch->allacts.getCurrent()->disableRule(rule))
                r.warnings.push_back(std::string("engine rule ") + rule + " not found: access widths and counts may differ from the code");
        arch->allacts.getCurrent()->reset(*fd);
        arch->allacts.getCurrent()->perform(*fd);
        // The note of A3-28, but not at a call to a callee whose signature is lent: its arguments are what it reads, and r2 is no
        // input (the probe, run before the literals fold, took every `jsr @rN` for a callee that may read r2: A3-32).
        for (const auto& siteRegs : escapeSites)
        {
            const uint32_t site = siteRegs.first;
            bool lentCallee = false;
            for (ghidra::int4 i = 0; i < fd->numCalls() && !lentCallee; ++i)
            {
                const ghidra::FuncCallSpecs* cs = fd->getCallSpecs(i);
                if (cs->getOp() == nullptr || static_cast<uint32_t>(cs->getOp()->getAddr().getOffset()) != site) continue;
                const ghidra::Address at = cs->getEntryAddress();
                if (at.isInvalid() || at.getSpace() != ram) continue;
                const auto p = prototypes.find(static_cast<uint32_t>(at.getOffset()));
                lentCallee = p != prototypes.end() && p->second.known;
            }
            if (lentCallee) continue;
            // One note for the register that holds the address (r2, then r7), or for the stack argument slot, or for a local the call may write as
            // one of its stack arguments (A3-61) when that is all.
            const uint32_t m = siteRegs.second;
            arch->commentdb->addComment(ghidra::Comment::user2, fd->getAddress(), ghidra::Address(ram, site),
                                        (m & 4u) ? kFrameEscapeNote : (m & 0x80u) ? kFrameEscapeNoteR7 : (m & 0x8000u) ? kFrameEscapeNoteStack : kFrameEscapeNoteSlot);
        }

        if (!request.allMemoryVolatile)
        {
            const std::vector<HardwareRange> fresh = NewAliasedRanges(ConstantAccesses(*fd, ram), aliasKnown);
            for (const HardwareRange& h : fresh)
            {
                aliasRanges.push_back(h);
                userVolatile.push_back(h);
            }
            if (!fresh.empty() && print && mayRetry)
            {
                aliasRetry = true;
                return std::string();
            }
        }

        if (targets)
        {
            // Every call the function makes. A direct call's target is in its call spec (flow
            // following replaces the CALL's address input with a spec annotation); a jsr @rN
            // through a literal pool is a CALLIND on a constant once the literal is folded -- the
            // engine leaves it indirect, which is why the second pass registers it.
            for (int i = 0; i < fd->numCalls(); ++i)
            {
                const ghidra::FuncCallSpecs* cs = fd->getCallSpecs(i);
                const ghidra::PcodeOp* op = cs->getOp();
                uint64_t t = 0;
                if (!cs->getEntryAddress().isInvalid() && cs->getEntryAddress().getSpace() == ram)
                    t = cs->getEntryAddress().getOffset();
                else if (op && op->code() == ghidra::CPUI_CALLIND && op->getIn(0)->isConstant())
                    t = op->getIn(0)->getOffset();
                else
                    continue;
                // Only targets whose code was captured are registered: a call into the BIOS
                // (never captured) stays an opaque pointer rather than naming a function nothing
                // can show. An odd address is no function either (a literal that is no code address, A7-6).
                uint32_t c = 0;
                uint8_t code[2];
                if ((t & 1u) == 0 && CanonicalAddress(static_cast<uint32_t>(t), c) && snapshot->Read(c, code, 2))
                    (*targets)[c].insert(static_cast<uint32_t>(t));
            }
        }
        // The first pass ends here, and its check-point is the caller's, made after this function has returned
        // (A6-6, A6-35): a cancel raised in the tail of these actions must not be carried through the next pass's
        // Init (Architecture construction has no check-point of its own) into its Flow. A check made here would
        // leave the destruction of the pass's locals and then of its Architecture (Teardown) between the check and
        // the next phase entry, and a raise there was carried: under a sanitizer that window is long enough to hit
        // one run in three. Made after them, only the few instructions to the next pass's Init remain.
        if (!print) return std::string();

        if (gBoundaryHookForTests) gBoundaryHookForTests();
        checkCancel();
        Enter(DecompilePhase::Print);
        r.name = fd->getName();
        r.bodyBytes = static_cast<uint32_t>(fd->getSize());
        r.unimplemented = fd->hasUnimplemented();
        r.unsupported = scan.unsupported;
        r.unsupportedAt = canonicalOf(scan.unsupportedAt);
        // The body: what the function's last flow translated, before any action removed what it found
        // dead (patch 0021). A restart in the actions follows the flow again and can reach further
        // than the scan above did (a jump into a second function is only resolved then), and what
        // that flow reached is the function's whether or not the p-code survived: the entry's
        // 'add #-0x40,r15' of a function jumped into, a test and branch whose arms meet.
        std::set<uint32_t> insns;
        for (const ghidra::FlowInsn& fi : fd->getFlowInsns())
            if (!fi.halted) insns.insert(static_cast<uint32_t>(fi.addr.getOffset()));
        r.body = BodyRanges(insns, *snapshot);
        for (uint32_t w : scan.literalWords) r.literalPools.push_back(canonicalOf(w));
        std::sort(r.literalPools.begin(), r.literalPools.end());
        r.literalPools.erase(std::unique(r.literalPools.begin(), r.literalPools.end()), r.literalPools.end());
        r.literalLoads = scan.literalLoads;
        if (scan.unsupported)
            r.warnings.push_back("PC-relative load in a delay slot at " + Hex8(r.unsupportedAt) +
                                 " that other code also jumps to: pseudocode for this function is not trustworthy");
        if (r.unimplemented) r.warnings.push_back("the SH-2 spec has no semantics for an instruction here");

        // Every uncertainty the engine recorded while following flow, which it keeps as warning
        // comments on the function. The pseudocode shows them inline only as far as the comment
        // options print them; here they become part of the result, so a function cut short never
        // passes for a complete one.
        const ghidra::Address fad = fd->getAddress();
        // What the snapshot says of a halt's address: not there (a cut), or there (it did not
        // decode). Asked of the snapshot itself, so the answer does not depend on how many refusals
        // the fetch image happened to record.
        auto missing = [&](uint32_t a, std::string& why) {
            uint8_t b[2];
            return !snapshot->Read(a, b, 2, &why);
        };
        bool restartsExhausted = false, repeatsExhausted = false, tableIncomplete = false;
        // The blocks the actions removed as unreachable, by the address they start at as the engine wrote it ("Removing unreachable block
        // (ram,0x25fe0008)"). A halt flow made at the start of one is not in the function that is shown (A4-16).
        std::set<uint32_t> removedBlocks;
        for (auto it = arch->commentdb->beginComment(fad); it != arch->commentdb->endComment(fad); ++it)
        {
            static const char kRemoved[] = "Removing unreachable block (ram,0x";
            const std::string& text = (*it)->getText();
            const size_t at = text.find(kRemoved);
            if (at != std::string::npos)
                removedBlocks.insert(static_cast<uint32_t>(std::strtoul(text.c_str() + at + sizeof kRemoved - 1, nullptr, 16)));
        }
        for (auto it = arch->commentdb->beginComment(fad); it != arch->commentdb->endComment(fad); ++it)
        {
            const ghidra::Comment* c = *it;
            if ((c->getType() & (ghidra::Comment::warning | ghidra::Comment::warningheader)) == 0) continue;
            const EngineWarning cls = ClassifyEngineWarning(c->getText());
            if (!cls.keep) continue;
            std::string text = cls.text;
            const bool outcome = cls.casesMissing;
            tableIncomplete |= cls.casesMissing;
            restartsExhausted |= cls.restartLimit;
            repeatsExhausted |= cls.repeatLimit;
            const bool header = (c->getType() & ghidra::Comment::warningheader) != 0;
            // Said once per halt below, where it is.
            if (header && (text == "Control flow encountered bad instruction data" ||
                           text == "Control flow encountered unimplemented instructions"))
                continue;
            const uint32_t at = static_cast<uint32_t>(c->getAddr().getOffset());
            FlowWarning w;
            w.address = header ? 0 : canonicalOf(at);
            if (!header && text == "Bad instruction - Truncating control flow here")
            {
                // Memory that is not there (the instruction, or the delay slot decoded with it), or
                // bytes that are there but do not decode.
                std::string why;
                uint32_t gone = 0;
                bool cutHere = missing(at, why);
                if (cutHere) gone = at;
                else
                {
                    uint16_t op = 0;
                    if (snapshot->ReadU16(at, op) && Sh2Decode(at, op).HasDelaySlot && missing(at + 2, why))
                    {
                        cutHere = true;
                        gone = at + 2;
                    }
                }
                if (cutHere && removedBlocks.count(at) != 0)
                {
                    // Flow went on to the halt through a branch or a table the analysis then proved cannot be taken (a jump table read at a
                    // constant index its bound check excludes, A4-16), and removed the block the halt stands in. The text has no cut and no
                    // halt in it; it is said, as the removal is, and the function is not truncated by it. (Bytes that do not decode in
                    // such a block are still reported as bad data: nothing has shown that they are harmless.)
                    w.kind = FlowWarning::Engine;
                    w.address = canonicalOf(at);
                    w.text = "flow followed a path into " + why + ", which the analysis proved cannot be taken and removed: the function is not cut "
                             "off there";
                }
                else if (cutHere)
                {
                    const uint32_t cut = canonicalOf(gone);
                    if (!r.truncated || cut < r.truncatedAt)
                    {
                        r.truncatedAt = cut;
                        r.truncatedWhy = why;
                    }
                    r.truncated = true;
                    ++r.truncatedCount;
                    w.kind = FlowWarning::Cut;
                    w.address = cut;
                    w.text = "flows into " + why + "; the function is cut off there";
                }
                else
                {
                    r.badDataAt.push_back(w.address);
                    w.kind = FlowWarning::BadData;
                    w.text = (at & 1u) != 0 ? "flow reaches an odd address, where no SH-2 instruction starts (a jump table entry or a pointer that is no "
                                              "code address), and stops there"
                                            : "flow reaches bytes that do not decode as an instruction and stops there (embedded "
                                              "data, or the entry is wrong)";
                }
            }
            else
            {
                if (outcome)
                {
                    w.kind = FlowWarning::JumpTable;
                    text += ": cases may be missing";
                }
                w.text = text;
            }
            r.flowWarnings.push_back(std::move(w));
        }
        // An indirect jump the engine turned into a call without a word: a table of one entry, which
        // it takes for a thunk -- a tail call, with a return assumed after it. Every other way an
        // indirect jump ends up (a table, a call, a return) is recorded at its address. Of every
        // instruction the last flow translated, not just the first flow's: a jump reached after a
        // restart is as much a tail call.
        for (uint32_t a : insns)
        {
            uint16_t op = 0;
            if (!snapshot->ReadU16(a, op)) continue;
            const bool jmp = (op & 0xF0FFu) == 0x402Bu, braf = (op & 0xF0FFu) == 0x0023u;
            if (!jmp && !braf) continue;
            bool settled = false;
            for (ghidra::int4 t = 0; t < fd->numJumpTables() && !settled; ++t)
                settled = fd->getJumpTable(t)->getOpAddress().getOffset() == a;
            for (const FlowWarning& w : r.flowWarnings) settled |= w.address == canonicalOf(a) && w.kind == FlowWarning::Engine;
            if (settled) continue;
            // Only a jump that is now a call: one the engine turned into a goto within the function
            // (a table of one destination) or removed as dead is not a tail call.
            std::string where = "an address the code computes";
            bool call = false;
            for (auto it = fd->beginOpMain(ghidra::Address(ram, a)); it != fd->endOpMain(ghidra::Address(ram, a)); ++it)
            {
                const ghidra::PcodeOp* o = it->second;
                if (o->code() != ghidra::CPUI_CALLIND && o->code() != ghidra::CPUI_CALL) continue;
                call = true;
                const ghidra::FuncCallSpecs* cs = fd->getCallSpecs(o);
                uint64_t t = 0;
                bool known = false;
                if (cs && !cs->getEntryAddress().isInvalid()) { t = cs->getEntryAddress().getOffset(); known = true; }
                else if (o->getIn(0)->isConstant()) { t = o->getIn(0)->getOffset(); known = true; }
                if (!known) continue;
                std::string why;
                where = Hex8(static_cast<uint32_t>(t));
                if (missing(static_cast<uint32_t>(t), why)) where += " (" + why + ": what runs there is not shown)";
            }
            if (!call) continue;
            r.flowWarnings.push_back(FlowWarning{ FlowWarning::TailJump, canonicalOf(a),
                                                  std::string(jmp ? "jmp" : "braf") + " is taken for a tail call to " + where +
                                                      ", assumed to return to this function's caller" +
                                                      // Said whether or not the destination is captured: it is a call here, the code there
                                                      // is a function of its own and not in the body (A4-5; name its entry to see it).
                                                      (where.find('(') == std::string::npos ? "; the code there is not part of this function's body" : "") });
        }
        for (ghidra::int4 t = 0; t < fd->numJumpTables(); ++t) tableIncomplete |= fd->getJumpTable(t)->isPartial();
        std::sort(r.badDataAt.begin(), r.badDataAt.end());
        // Ghidra fails a function on its fifth bad-data halt; ours carries on (SaturnArchitecture:
        // halts on uncaptured memory are expected), so the same judgement is a warning here.
        if (r.badDataAt.size() >= 4)
            r.flowWarnings.push_back(FlowWarning{ FlowWarning::BadData, 0, std::to_string(r.badDataAt.size()) +
                                                         " paths run into bytes that do not decode: the entry is "
                                                         "probably not code" });
        // Flow that runs into another function's entry -- a tail branch, a fall-through into the next
        // function, a shared epilogue -- has taken that function's code in as this one's. The engine
        // follows a branch wherever it goes; it is said here, since we know the entries. A callee
        // counts only if this pass calls it: one the first pass took for a call (a jump whose target
        // only this pass could fold) is no entry.
        std::set<uint32_t> entries;
        for (uint32_t k : known) entries.insert(k);
        for (const auto& n : named) entries.insert(n.first);
        // ...and a function the request lends a signature to is one, whether or not the finder listed it (the panel
        // lends every function the user decompiled, at the address the user chose: A4-8).
        for (const auto& p : prototypes) entries.insert(p.first);
        if (targets)
            for (const auto& t : *targets) entries.insert(t.first);
        for (uint32_t a : entries)
        {
            const uint32_t c = canonicalOf(a);
            if (c == entry || !r.InBody(c)) continue;
            auto f = named.find(c);
            r.flowWarnings.push_back(FlowWarning{ FlowWarning::IntoFunction, c, "flow runs into " + (f != named.end() ? f->second + " at " : std::string()) +
                                                         Hex8(c) + ", another function's entry: that code is shown "
                                                         "as part of this function" });
        }
        std::stable_sort(r.flowWarnings.begin(), r.flowWarnings.end(),
                         [](const FlowWarning& x, const FlowWarning& y) { return x.address < y.address; });
        for (const FlowWarning& w : r.flowWarnings)
            r.warnings.push_back(w.address ? Hex8(w.address) + ": " + w.text : w.text);
        r.hardwareNotes = HardwareNotes(*fd, canonicalOf);
        for (const HardwareNote& n : r.hardwareNotes)
            if (n.caution) r.warnings.push_back(Hex8(n.address) + ": " + n.text);
        if (r.truncated) r.prototypeWithheld = "truncated";
        else if (tableIncomplete) r.prototypeWithheld = "a jump table recovered incompletely";
        else if (restartsExhausted) r.prototypeWithheld = "the analysis did not settle (restart limit)";
        else if (repeatsExhausted) r.prototypeWithheld = "the analysis did not settle (repeat limit)";
        else if (r.unsupported) r.prototypeWithheld = "unsupported";
        else if (r.unimplemented) r.prototypeWithheld = "unimplemented";
        CallEffects effects;
        if (r.prototypeWithheld.empty()) 
        {
            AddJumpTables(*fd, flowTables);
            AddFlowTables(*fd, flowTables);
            r.prototypeWithheld = WithholdReason(*fd, prototypes, exec, entry, flowTables, effects, &scan.literalWords, snapshot.get());
        }
        if (r.prototypeWithheld.empty())
        {
            r.prototype = PrototypeOf(*fd);
            r.prototype.effects = effects;
        }

        std::ostringstream markup;
        markup.exceptions(std::ios::badbit);   // a std::bad_alloc inside an insertion is rethrown, not swallowed
        arch->print->setOutputStream(&markup);
        arch->print->setMarkup(true);
        arch->print->setPackedOutput(true);
        arch->print->docFunction(fd);
        arch->print->setMarkup(false);
        std::map<const ghidra::PcodeOp*, std::pair<std::set<size_t>, int>> printed;
        // Never read the text of a stream that failed: with the exception mask above nothing
        // reaches here with badbit set, and this says so if that ever stops being true.
        if (markup.bad()) throw std::bad_alloc();
        r.lines = LinesFromMarkup(markup.str(), *fd, arch.get(), &printed);
        r.accesses = Accesses(*fd, printed, canonicalOf, flow);
        // How each flow access that is no longer a LOAD or STORE ended (patch 0017). The engine
        // keeps the records of the last generation of the function (a restart clears them with the
        // ops they describe), so they match the final p-code.
        for (const ghidra::AccessRecord& rec : fd->getAccessFates())
        {
            AccessFate f;
            f.insn = canonicalOf(static_cast<uint32_t>(rec.pc.getOffset()));
            f.write = rec.store;
            f.size = static_cast<uint8_t>(rec.size);
            switch (rec.fate)
            {
            case ghidra::fate_folded_ordinary: f.fate = AccessFate::FoldedOrdinary; break;
            case ghidra::fate_unreachable: f.fate = AccessFate::Unreachable; break;
            case ghidra::fate_switch_lookup: f.fate = AccessFate::SwitchLookup; break;
            default: f.fate = AccessFate::Dead; break;
            }
            // A fold into the stack frame records the Varnode's address in the stack space: an
            // offset (-4 for a pop), which no bus address is (A5-5).
            if (!rec.addr.isInvalid())
            {
                f.address = static_cast<uint32_t>(rec.addr.getOffset());
                f.constantAddress = rec.addr.getSpace() == ram;
                f.frame = !f.constantAddress;
            }
            r.accessFates.push_back(f);
        }
        r.calls = Calls(*fd, flow, canonicalOf);
        r.flowSuccessors = flow.successors;
        if (errors.bad()) throw std::bad_alloc();
        const std::string e = errors.str();
        if (!e.empty()) r.warnings.push_back(e);
        return std::string("ok");
    };

    CalleeAliases callees;
    std::string printed;
    // The callees reported are the printed pass's own: the first pass can take a jump for a call
    // that the second, with the literals folded, follows as a goto.
    CalleeAliases first;
    bool firstPassRan = false;
    if (seedIn)
    {
        knownLiterals = seedIn->literals;
        first = seedIn->callees;
    }
    else if (request.registerCallees)
    {
        runPass(CalleeAliases(), false, &first, false);
        firstPassRan = true;
    }
    if (seedOut)
    {
        seedOut->literals = knownLiterals;
        seedOut->callees = first;
    }
    // The first pass's boundary, with its locals and its Architecture gone (Teardown has been entered and left) and
    // the copies above made, so that nothing but the call stands between this check and the next pass's Init: a cancel
    // raised in the tail of the pass's actions, or while it was torn down, is honoured here, in Actions (mWorkPhase is
    // still the pass's last phase), and not carried through the next pass's Init into its Flow (A6-6, A6-35).
    if (firstPassRan)
    {
        if (gBoundaryHookForTests) gBoundaryHookForTests();
        checkCancel();
    }
    for (int round = 0;; ++round)
    {
        aliasRetry = false;
        printed = runPass(first, true, &callees, round < kAliasRounds);
        if (!aliasRetry) break;
        // The same boundary as the first pass's, for a printing pass that gives way to another (aliasRetry): its locals and Architecture
        // are gone, and a cancel raised in its teardown is honoured here, in Actions, not carried into the next pass's Init (A5-63).
        if (gBoundaryHookForTests) gBoundaryHookForTests();
        checkCancel();
    }
    // The last pass's Architecture is gone and no translator, Architecture or engine object is live: what follows is host
    // work on host data (the aliased ranges, the callee lists, and in NoteRemovedAccesses the request's copy and the scan of
    // the warnings). An allocation failure there leaves nothing half-reset, so it must not mark the engine damaged for the
    // process (Decompile marks it for any exception met with a work phase set): it fails this request only (A6-45).
    // mWorkPhase stays the pass's last phase until here, for the boundary check above to name the phase it honours a cancel in.
    mWorkPhase = DecompilePhase::Idle;
    if (gEngineDoneHookForTests) gEngineDoneHookForTests();
    for (const HardwareRange& h : aliasRanges) r.aliasedMemory.emplace_back(h.first, h.last);
    if (printed.empty())
    {
        r.status = DecompileStatus::NoCode;
        r.error = "no code at " + Hex8(entry);
        return;
    }
    r.status = DecompileStatus::Ok;
    for (const auto& c : callees)
    {
        r.callees.push_back(c.first);
        r.calledAt.insert(r.calledAt.end(), c.second.begin(), c.second.end());
    }
    std::sort(r.calledAt.begin(), r.calledAt.end());
}

std::vector<std::string> Sh2Decompiler::Disassemble(const std::shared_ptr<const MemorySnapshot>& snapshot,
                                                    uint32_t address, int count, bool* stopped)
{
    // Never throws, whatever the heap does (A6-16): an allocation failure ends the listing with a line that
    // says so, and the engine is not touched again. Everything that can allocate is inside the try.
    //
    // The line that says so is itself an allocation, and under sustained exhaustion (every allocation from one
    // on fails) it could not be made: the listing came back empty or cut short, with nothing to say it was (A6-23).
    // So the marker string and the room for it in the listing (and for the lines before it) are made first, and a
    // stop is then a move into room that exists; and '*stopped' is a bool the caller owns, which needs nothing.
    if (stopped) *stopped = false;
    AsmLines em;
    std::string marker;   // empty: no allocation until it is assigned, inside a try
    try
    {
        marker.assign("<stopped: out of memory or an unexpected exception>");
        em.lines.reserve(static_cast<std::size_t>(std::max(0, std::min(count, 4096))) + 1);
    }
    catch (...)
    {
        // The marker and its room could not be made: nothing of the engine's was touched, so the engine is not
        // damaged and the next request starts again (A6-30). The listing is cut short before it began, and says
        // so when a line can be made; under sustained exhaustion only the flag, which needs nothing, can say it.
        if (stopped) *stopped = true;
        try
        {
            em.lines.push_back("<out of memory while preparing the decompiler; try again>");
        }
        catch (...)
        {
        }
        return std::move(em.lines);
    }
    try
    {
        // Every way into the engine checks this: disassembly builds an Architecture too, which would
        // reset the same possibly half-reset translator.
        if (gEngineDamaged) return std::vector<std::string>{ std::string("<unavailable: ") + kEngineDamaged + ">" };
        std::string error;
        bool started = false;
        try
        {
            started = SaturnArchitecture::StartLibrary(mConfigDir, error);
        }
        catch (...)
        {
            // Out of memory preparing the language files, before anything of the engine's was touched
            // (StartLibrary writes no global until its last step, and says so itself when that step is
            // the one that failed): this listing is cut short, the engine is not damaged, and the next
            // request starts again -- as for Decompile, whose boundary treats an exception in the Idle
            // phase the same way (A6-30). The listing is cut short before it began, and the flag says so
            // whether or not the line can be made (A6-31): under sustained exhaustion it cannot.
            if (stopped) *stopped = true;
            try
            {
                em.lines.push_back("<out of memory while preparing the decompiler; try again>");
            }
            catch (...)
            {
            }
            return std::move(em.lines);
        }
        if (!started)
        {
            // The refusal as one line. Its text is built here, handled here and not by the damage handler below: the engine was never
            // entered, so a failure making the line is a listing cut short before it began and not a damaged engine
            // (A6-32). The room is already in the vector (count + 1 >= 1), so a move into it cannot fail.
            if (stopped) *stopped = true;
            try
            {
                std::string line = "<" + error + ">";
                em.lines.push_back(std::move(line));
                if (stopped) *stopped = false;   // the line is the report
            }
            catch (...)
            {
            }
            return std::move(em.lines);
        }
        std::ostringstream errors;
        errors.exceptions(std::ios::badbit);
        std::unique_ptr<SaturnArchitecture> arch = BuildArchitecture(snapshot, 20000, nullptr, errors);
        ghidra::Address a(arch->getDefaultCodeSpace(), address);
        for (int i = 0; i < count; ++i)
        {
            const ghidra::int4 len = arch->translate->printAssembly(em, a);
            a = a + len;
        }
    }
    catch (ghidra::LowlevelError& e)
    {
        try
        {
            em.lines.push_back("<" + e.explain + ">");
        }
        catch (...)
        {
            // The engine's own error, and not even its message could be made: the heap is gone. As below.
            gEngineDamaged = true;
            if (stopped) *stopped = true;
            try
            {
                em.lines.push_back(std::move(marker));
            }
            catch (...)
            {
            }
        }
    }
    catch (...)
    {
        // Out of memory inside the engine (or anything else not its own): treated as Decompile
        // treats it -- the engine may be damaged, so nothing touches it again. The listing so far is
        // cut short, and says so: a caller reading it would otherwise take it for the whole.
        gEngineDamaged = true;
        if (stopped) *stopped = true;
        try
        {
            em.lines.push_back(std::move(marker));   // into the room made first: no allocation
        }
        catch (...)
        {
        }
    }
    return std::move(em.lines);   // a move: a copy would allocate outside the try
}

}  // namespace decomp
}  // namespace sfe
