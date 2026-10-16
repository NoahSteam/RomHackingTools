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

std::unique_ptr<SaturnArchitecture> BuildArchitecture(const std::shared_ptr<const MemorySnapshot>& snapshot,
                                                      uint32_t maxInstructions, std::atomic<bool>* cancel,
                                                      std::ostream& errors)
{
    std::unique_ptr<SaturnArchitecture> arch(new SaturnArchitecture(snapshot, &errors));
    arch->Initialise(maxInstructions, cancel);
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

    // After Run(): the registers outside r4-r7 (r1-r3, r8-r14, GBR, VBR, MACH, MACL: bit n is register n of
    // origin::) whose entry value reaches r0 at a return, through instructions of this function (not through a call).
    uint32_t InputsInReturn() const { return mInputsInReturn; }

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
        mInputsInReturn = 0;
        mStoresThrough = 0;
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
            if (++steps > kMaxSteps) return "a return not proven to go to its caller (too many paths)";
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
        effects.outOrigins = mOut;
        effects.leaks = mLeaks;
        effects.frameValues = mFrameValues;
        // |delta| below the engine's own 'unknown' extrapop (0x8000).
        effects.spDelta = mSpKnown && mSpDelta > -0x8000 && mSpDelta < 0x8000 ? static_cast<int32_t>(mSpDelta) : CallEffects::kSpUnknown;
        return std::string();
    }

private:
    static constexpr size_t kMaxSteps = 200000;
    static constexpr int kWidenAfter = 6;
    static constexpr int kTracked = 22;   // the registers origin:: names, in its order
    static constexpr int kR15 = 15, kSr = 16, kPr = 21;
    // r0-r3, r8-r14, GBR, VBR, MACH, MACL: the registers whose entry value no signature passes (r4-r7 are
    // parameters; r15 and PR are the stack and the return address; SR is left out, see Return()).
    static constexpr uint32_t kNotParameters = 0xFu | (0x7Fu << 8) | (1u << 17) | (1u << 18) | (1u << 19) | (1u << 20);

    struct Value
    {
        enum Kind : uint8_t { Fuzzy, Reg, Sp, Imm } kind = Fuzzy;
        int64_t v = 0;               // Reg: the register's index; Sp: the offset; Imm: the constant
        uint32_t from = origin::kAll;   // Fuzzy: what it can come from
        uint32_t direct = origin::kAll;   // Fuzzy: ...of that, what it was computed from here, not what a call returned (A3-7)
        // Fuzzy: a stack address plus an index, with the offset the address had before it (from r15 on entry): the value
        // points at or above it, taking the index as non-negative as the engine's own pointer arithmetic does (A3-9).
        static constexpr int64_t kNoBase = INT64_MIN;
        // ...or "from the frame's lowest slot": a base a loop kept lowering (A3-16) or an index that may be negative
        // (A3-17) leaves no lower bound but the frame's own. The least base there is, so a join with it is it.
        static constexpr int64_t kLowBase = INT64_MIN + 1;
        static int64_t MoveBase(int64_t base, int64_t by) { return base == kLowBase ? kLowBase : Wrap(base + by); }
        int64_t stackBase = kNoBase;
        int64_t Base() const { return kind == Sp ? v : kind == Fuzzy ? stackBase : kNoBase; }
        static Value Entry(int r) { Value x; x.kind = Reg; x.v = r; return x; }
        static Value Stack(int64_t off) { Value x; x.kind = Sp; x.v = off; return x; }
        static Value Const(int64_t c) { Value x; x.kind = Imm; x.v = c; return x; }
        static Value From(uint32_t o) { Value x; x.from = o; x.direct = o; return x; }
        static Value From(uint32_t o, uint32_t direct) { Value x; x.from = o; x.direct = direct; return x; }
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
        uint32_t Direct() const { return kind == Fuzzy ? direct : Origins(); }
        bool operator==(const Value& o) const { return kind == o.kind && v == o.v && (kind != Fuzzy || (from == o.from && direct == o.direct && stackBase == o.stackBase)); }
    };
    using Key = std::tuple<ghidra::AddrSpace*, ghidra::uintb, ghidra::uint4>;
    struct State
    {
        bool reached = false;
        bool escaped = false;
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
            if (v.second.kind != Value::Imm) return Value::From(v.second.Origins(), v.second.Direct());
            const uint64_t whole = std::get<2>(v.first), below = whole - (vn.offset - std::get<1>(v.first)) - vn.size;
            const uint64_t mask = vn.size >= 8 ? ~0ull : (1ull << (8 * vn.size)) - 1;   // big-endian: the last bytes are the low ones
            return Value::Const(static_cast<int64_t>((static_cast<uint64_t>(v.second.v) >> (8 * below)) & mask));
        }
        return Value();
    }

    void Set(State& s, const ghidra::VarnodeData& vn, const Value& v)
    {
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
                if (v.kind != Value::Sp || slot.first < v.v) slot.second = Value::From(kAnything);
    }

    static constexpr uint32_t kAnything = origin::kRegisters | origin::kStack | origin::kMemory;

    // What a load from untracked memory can give.
    static Value Loaded(const State& s) { return Value::From(origin::kMemory | (s.escaped ? origin::kStack : 0)); }

    // A store this function (or a callee) may have made over slots at offsets 'from' and up, of a
    // value from 'stored': each holds what it held or that.
    static void Clobber(State& s, uint32_t stored, int64_t from = INT64_MIN, int64_t to = INT64_MAX)
    {
        for (auto& slot : s.slots)
            if (slot.first + 4 > from && slot.first < to) slot.second = Value::From(slot.second.Origins() | stored, slot.second.Direct() | stored);
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
        };
        for (auto& v : st.vars) widen(v.second);
        for (auto& v : st.slots) widen(v.second);
    }

    // Keeps in 'into' what both states agree on, the rest as its origins; true when 'into' changed.
    static bool Join(State& into, const State& from)
    {
        bool changed = false;
        if (from.escaped && !into.escaped) into.escaped = changed = true;
        auto merge = [&changed](Value& a, const Value& b) {
            if (a == b) return;
            Value m = Value::From(a.Origins() | b.Origins(), a.Direct() | b.Direct());
            if (a.Base() != Value::kNoBase && b.Base() != Value::kNoBase) m.stackBase = std::min(a.Base(), b.Base());
            if (m == a) return;
            a = m;
            changed = true;
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
        // A slot absent from either holds what a load from memory gives there.
        for (auto& slot : into.slots)
        {
            auto f = from.slots.find(slot.first);
            merge(slot.second, f == from.slots.end() ? Loaded(from) : f->second);
        }
        for (const auto& slot : from.slots)
            if (into.slots.count(slot.first) == 0)
            {
                Value v = Loaded(into);
                merge(v, slot.second);
                if (!(v == Loaded(into))) into.slots[slot.first] = v;
            }
        return changed;
    }

    static int64_t Wrap(int64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(v)); }

    void Call(State& s, uint32_t at)
    {
        CallEffects e;   // unknown: anything
        bool signatureLent = false;
        auto t = mCallAt.find(at);
        if (t != mCallAt.end())
        {
            auto p = mLent.find(t->second);
            if (p != mLent.end())
            {
                e = p->second.effects;
                signatureLent = p->second.known;
            }
        }
        if (!signatureLent) NoteFrameEscape(s);
        Apply(s, e);
    }

    // A call whose signature is not lent may read r2: the SH ABI passes the address of a returned
    // structure there (SH_STRUCT_VALUE_REGNUM 2 in gcc, and Renesas' compilers), and the engine's
    // calling convention has no input in r2. So the copy of r15 that made the address is dead code
    // before the engine looks for pointers into the frame, and the callee's writes through it are
    // taken for none (A3-4). A callee whose signature is lent has been through WithholdReason, which
    // lends none that reads r2. Only r2, not the other registers a callee could read: r0, r1 and r3
    // are scratch the compiler leaves a frame address in, and r8-r14 hold the frame pointer, so a
    // rule over them would put every local of every function in an alias.
    void NoteFrameEscape(const State& s)
    {
        const Value v = Get(s, mReg[2]);
        // An address with an index added to it (a returned structure that is an array element) points at or above
        // the address it was made from: that offset is the alias's lower bound, and every local from it up is aliased
        // whatever the index (A3-9). An address made from nothing known has none to declare: the whole frame would
        // make an array of every local.
        const int64_t base = v.Base();
        if (base == Value::kNoBase) return;
        if (base == Value::kLowBase) mEscapedLow = true;
        else if (!mEscaped || base < mEscapeLow) mEscapeLow = base;
        mEscaped = true;
    }

    void Apply(State& s, const CallEffects& e)
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
            Clobber(s, framed, sp.kind == Value::Sp ? sp.v : INT64_MIN);
        }
        if (e.storesThrough & ~origin::kFrame) StoreThrough(s, Value::From(stores), leaked | framed);
        for (int r = 0; r < kTracked; ++r)
        {
            if (e.preserved & (1u << r)) continue;
            // r15 moved by a constant at every return of the callee: where it ends is known.
            if (r == kR15 && e.spDelta != CallEffects::kSpUnknown && sp.kind == Value::Sp)
                Set(s, mReg[r], Value::Stack(Wrap(sp.v + e.spDelta)));
            else
                Set(s, mReg[r], Value::From(out, 0));   // what the callee returns: not computed from anything here
        }
    }

    // Values let out to memory this function does not track: its callers learn of them through
    // 'leaks', and a stack address among them may come back from any load.
    void Leak(State& s, uint32_t origins)
    {
        mLeaks |= origins;
        if (origins & origin::kStack) s.escaped = true;
    }

    void Store(State& s, const Value& addr, const ghidra::VarnodeData& valueVn, const Value& value)
    {
        const int64_t n = valueVn.size;
        const uint32_t v = value.Origins();
        if (addr.kind == Value::Sp)
        {
            if (addr.v + n > 0)   // at or above r15 on entry: the caller's frame
            {
                mStoresThrough |= origin::kFrame;
                mFrameValues |= v;
            }
            if (n != 4)
            {
                // Part of a word: it holds what it held and part of this.
                for (int64_t w = addr.v & ~int64_t(3); w < addr.v + n; w += 4)
                    if (s.slots.count(w) == 0) s.slots[w] = Loaded(s);
            }
            Clobber(s, v, addr.v, addr.v + n);
            if (n == 4) s.slots[addr.v] = value;
            return;
        }
        StoreThrough(s, addr, v);
    }

    // A store through an address not known exactly, made here or by a callee, of a value from
    // 'stored': recorded in this function's summary, and over every slot it may hit. An absolute
    // address is taken never to be in the stack, as the engine's own stack analysis takes it.
    void StoreThrough(State& s, const Value& addr, uint32_t stored)
    {
        const uint32_t o = addr.Origins();
        if (o == 0 || (o & ~origin::kStack)) Leak(s, stored);   // memory this function does not track
        if (o == 0) return;
        mStoresThrough |= o & ~origin::kStack;
        if (o & origin::kStack)   // a stack address it cannot place: its own frame or its caller's
        {
            mStoresThrough |= origin::kFrame;
            mFrameValues |= stored;
        }
        if (MayBeStack(s, addr)) Clobber(s, stored);
        else Clobber(s, stored, 0);   // through an entry value: the caller's frame, at most
    }

    Value Load(const State& s, const Value& addr, ghidra::uint4 n) const
    {
        if (addr.kind == Value::Sp)
        {
            // Below r15 an interrupt may have written anything.
            const Value sp = Get(s, mReg[kR15]);
            if (sp.kind != Value::Sp || addr.v < sp.v) return Value::From(kAnything);
            if (n == 4)
            {
                auto f = s.slots.find(addr.v);
                if (f != s.slots.end()) return f->second;
            }
            uint32_t o = Loaded(s).from;
            for (const auto& slot : s.slots)
                if (slot.first < addr.v + n && addr.v < slot.first + 4) o |= slot.second.Origins();
            return Value::From(o);
        }
        // Through an address that may be in the stack but not exactly: nothing bounds it above r15,
        // so it may read free stack, which holds anything.
        return MayBeStack(s, addr) ? Value::From(kAnything) : Loaded(s);
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
            const bool same = r == kR15 ? v.kind == Value::Sp && v.v == 0 : v.kind == Value::Reg && v.v == r;
            if (same) continue;
            mPreserved &= ~(1u << r);
            mOut |= v.Origins();
            // The return register: a value computed from a register no signature passes is a return the
            // engine does not see (it takes a return made only of such inputs for no return at all: A3-5).
            // Not r0's own entry value: a path that leaves r0 alone and one that does not meet in a value
            // that has it among its origins, which is no read. Not r1, the second half of a 64-bit
            // return, which is scratch for every other (a loop that may not run leaves its r1 as it was).
            // Not SR among the origins of a computed value: a flag written here carries the entry SR's origin
            // with it (the write is a masked update), so every movt after a compare would be one. SR itself,
            // copied whole to r0 (`stc sr,r0`), is. Nor a value of unknown origin.
            // Calls do not excuse it (A3-7: `bsr g ; ... ; rts ; mov r8,r0` returns the caller's r8 across a
            // call that keeps it, and the engine drops that return as it drops the leaf's). What a call
            // returned is no read of an input, though: its outputs share one set of origins, everything any
            // of the callee's registers can come from, so after one r0 has the origins of the callee's other
            // registers (it is how a function that saves r8 in its prologue and calls would look to have
            // returned it). Value::Direct() leaves those out and keeps what this function's own instructions
            // computed r0 from, on every path that reaches the return.
            if (r == 0 && (v.Direct() & origin::kRegisters) != origin::kRegisters)
            {
                mInputsInReturn |= v.Direct() & kNotParameters & ~1u;
                if (v.kind == Value::Reg && v.v == kSr) mInputsInReturn |= 1u << kSr;   // a plain copy of it, not a masked update
            }
        }
        return true;
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
            bool falls = true;
            switch (op.code)
            {
            case ghidra::CPUI_COPY: Set(s, op.out, Get(s, op.in[0])); break;
            case ghidra::CPUI_INT_ADD:
            case ghidra::CPUI_INT_SUB:
            {
                const Value x = Get(s, op.in[0]), y = Get(s, op.in[1]);
                const bool sub = op.code == ghidra::CPUI_INT_SUB;
                Value r = Value::From(x.Origins() | y.Origins(), x.Direct() | y.Direct());
                if (x.kind == Value::Sp && y.kind == Value::Imm) r = Value::Stack(Wrap(sub ? x.v - y.v : x.v + y.v));
                else if (!sub && x.kind == Value::Imm && y.kind == Value::Sp) r = Value::Stack(Wrap(x.v + y.v));
                else if (x.kind == Value::Imm && y.kind == Value::Imm) r = Value::Const(sub ? x.v - y.v : x.v + y.v);
                // An index added to a stack address stays in the stack (the engine's own stack analysis
                // takes pointer arithmetic so too): somewhere in it, not the index's memory. Where from is
                // kept (A3-9: `a[i] = f()` passes the address of an element in r2): the base address, with
                // a constant added to it later added to the base, and the index taken as not negative.
                else if (x.Base() != Value::kNoBase || (!sub && y.Base() != Value::kNoBase))
                {
                    r = Value::From(origin::kStack);
                    if (!sub && x.Base() != Value::kNoBase && y.Base() == Value::kNoBase)
                        r.stackBase = Value::MoveBase(x.Base(), y.kind == Value::Imm ? y.v : 0);
                    else if (!sub && y.Base() != Value::kNoBase && x.Base() == Value::kNoBase)
                        r.stackBase = Value::MoveBase(y.Base(), x.kind == Value::Imm ? x.v : 0);
                    else if (sub && y.kind == Value::Imm)
                        r.stackBase = Value::MoveBase(x.Base(), -y.v);
                }
                Set(s, op.out, r);
                break;
            }
            case ghidra::CPUI_LOAD: Set(s, op.out, Load(s, Get(s, op.in[1]), op.out.size)); break;
            case ghidra::CPUI_STORE: Store(s, Get(s, op.in[1]), op.in[2], Get(s, op.in[2])); break;
            case ghidra::CPUI_CALL:
            case ghidra::CPUI_CALLIND:
                Call(s, a);
                if (mNoReturn.count(a)) continue;   // the path ends in the callee
                break;
            case ghidra::CPUI_CALLOTHER:
                // sleep (Sleep_Standby) changes no register or memory, nor do the control register writes (Write_SR,
                // Write_GBR, Write_VBR: the register itself is written by the instruction's own p-code, the user
                // op only keeps the write in the code); any other user op, anything.
                {
                    const std::string& userOp = mFd.getArch()->userops.getOp(static_cast<ghidra::uint4>(op.in[0].offset))->getName();
                    if (userOp != "Sleep_Standby" && userOp != "Write_SR" && userOp != "Write_GBR" && userOp != "Write_VBR")
                        Apply(s, CallEffects());
                }
                if (op.hasOut) Set(s, op.out, Value());
                break;
            case ghidra::CPUI_BRANCH:
            case ghidra::CPUI_CBRANCH:
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
                if (found)
                    for (uint32_t to : table->second) succ.emplace_back(to, s);
                // jmp @rN with rN holding the caller's PR is a return like rts (the engine takes a jump
                // table it cannot recover that way too, when the target is the return address).
                if (!found && !Return(s, op.in[0]) && !mTolerant) return "an indirect jump it does not resolve";
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
                    uint32_t o = 0, d = 0;
                    for (const ghidra::VarnodeData& vn : op.in)
                    {
                        const Value x = Get(s, vn);
                        o |= x.Origins();
                        d |= x.Direct();
                    }
                    Set(s, op.out, Value::From(o, d));
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
    uint32_t mPreserved = 0, mStoresThrough = 0, mOut = 0, mLeaks = 0, mFrameValues = 0;
    bool mSpSeen = false, mSpKnown = true;   // r15 at the returns reached: one constant offset from its entry value, or not
    int64_t mSpDelta = 0;
    uint32_t mInputsInReturn = 0;   // registers outside r4-r7 whose entry value reaches r0 at a return, not through a call
    bool mTolerant = false;
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
//   `ok`: every path the value takes ends in the write back, or is a whole copy (SR only: returned, stored, or with bits
//   set by an or with a constant). `transformed`: it went through an operation, so it is not a plain copy. `written`: it
//   reached a write back. A transformed value that reaches no write back is somebody's flags, and is an input.
struct StateUse
{
    bool ok = true, transformed = false, written = false;
};

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
            case ghidra::CPUI_STORE: r.ok = wholeOk && !part && op->getIn(2) == vn; break;
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

// Why the signature recovered for 'fd' cannot be lent to its callers, or empty when it can. A lent
// signature is locked: the caller's call shows exactly those arguments and that return, and
// nothing later revisits it. So it is lent only when the recovery saw everything that decides it.
std::string WithholdReason(ghidra::Funcdata& fd, const std::map<uint32_t, Prototype>& lent, uint32_t exec,
                           uint32_t entry, const JumpDests& tables, CallEffects& effects)
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
    const std::string why = check.Run(effects);
    if (!why.empty()) return why;
    // A return the engine dropped because its value comes from such a register alone (`rts ; sts macl,r0`
    // prints as returning nothing): the input is no longer in the text to be found above. Only when the
    // engine did drop it; one it kept has its input in the text, and was looked at there.
    if (const uint32_t inputs = fp.getOutputType()->getMetatype() == ghidra::TYPE_VOID ? check.InputsInReturn() : 0u)
    {
        static const char* const kNames[] = { "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
                                              "r11", "r12", "r13", "r14", "r15", "sr", "gbr", "vbr", "mach", "macl", "pr" };
        for (int bit = 0; bit < 22; ++bit)
            if ((inputs >> bit) & 1u) return std::string("its return is computed from ") + kNames[bit] + " on entry, which no signature passes";
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

    // A3: every instruction the flow reached, decoded on our side. A delayed branch followed by
    // a PC-relative load or mova reads relative to the branch target when the branch is taken.
    // The spec (patch 0005) gets that right for bra and bsr, whose target is in the instruction.
    // Every other delayed branch -- conditional, through a register, rts -- leaves the spec
    // computing the slot's own address, which is wrong whenever the branch is taken; that
    // literal is excluded from folding and the function flagged.
    std::set<uint32_t>& insns = scan.insns;
    for (auto it = fd.beginOpMain(); it != fd.endOpMain(); ++it)
    {
        const uint32_t a = static_cast<uint32_t>(it->second->getAddr().getOffset());
        insns.insert(a);
    }
    // The patch hands the slot its PC by address, so a slot that anything in the function also
    // jumps straight to would get the slot's PC on that path too. Such code is flagged rather than
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
        const bool specFixed = branch.HasBranchTarget && !branch.IsConditional;   // bra, bsr
        if (specFixed && targets.count(a + 2) == 0) continue;
        if (!scan.unsupported || a + 2 < scan.unsupportedAt)
        {
            scan.unsupported = true;
            scan.unsupportedAt = a + 2;
        }
        // Neither candidate folds: the spec computes the slot-relative address after a bra or bsr
        // and the slot's own otherwise, and either is wrong on one of the paths.
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

    std::vector<Line> out;
    for (Line& l : lines)
    {
        const bool blank = l.text.find_first_not_of(' ') == std::string::npos;
        if (blank && out.empty()) continue;   // the document's leading break
        if (blank) l.text.clear();
        for (const Token& t : l.tokens)
            if (t.addr && (l.firstAddr == 0 || t.addr < l.firstAddr)) l.firstAddr = t.addr;
        out.push_back(std::move(l));
    }
    while (!out.empty() && out.back().text.empty()) out.pop_back();
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
    w.keep = !scratch || w.casesMissing;
    w.text = text;
    return w;
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

// The constants a pointer varnode can hold when every path to it is a copy or a merge: the values the printed code names as
// `&REG` (a pointer merged from register literals, `puVar2 = &VDP2_TVSTAT; if (c) puVar2 = &VDP2_VCNT;`). 'others' is set when
// some path is not a constant (a parameter, a value loaded or computed): the pointer may hold something else as well.
// 'bases' are the constants an indexed pointer starts from (`(&SCU_D0EN)[param_1 * 8]`, a PTRADD or an INT_ADD of an address and
// something the analysis does not know): the access is at the base plus an amount, not at the base, so it is said only for the
// index 0.
struct PointerValues
{
    std::set<uint32_t> constants;
    std::set<uint32_t> bases;
    bool others = false;
};

PointerValues ConstantsOf(const ghidra::Varnode* start)
{
    PointerValues out;
    std::set<std::pair<const ghidra::Varnode*, bool>> seen;   // (varnode, reached through an index)
    std::vector<std::pair<const ghidra::Varnode*, bool>> todo{ std::make_pair(start, false) };
    while (!todo.empty() && seen.size() < 4096)
    {
        const std::pair<const ghidra::Varnode*, bool> item = todo.back();
        todo.pop_back();
        if (!seen.insert(item).second) continue;
        const ghidra::Varnode* vn = item.first;
        const bool indexed = item.second;
        if (vn->isConstant())
        {
            // A bare number, which the engine prints as a number even when it is a register's address (`param_1 + -0xc` is 0xFFFFFFF4,
            // SH2_RTCNT's): the pointer holds something else as well. A register address the engine means as a pointer is a PTRSUB (below).
            if (!indexed) out.others = true;
            continue;
        }
        const ghidra::PcodeOp* d = vn->isWritten() ? vn->getDef() : nullptr;
        if (!d)
        {
            if (!indexed) out.others = true;   // (an addend nothing is known about is the index: it makes the pointer indexed, not unknown)
            continue;
        }
        switch (d->code())
        {
        case ghidra::CPUI_COPY:
        case ghidra::CPUI_CAST:
            todo.push_back(std::make_pair(d->getIn(0), indexed));
            break;
        case ghidra::CPUI_MULTIEQUAL:
            for (int i = 0; i < d->numInput(); ++i) todo.push_back(std::make_pair(d->getIn(i), indexed));
            break;
        case ghidra::CPUI_PTRSUB:   // the address of a global: PTRSUB(0, address)
            if (d->getIn(0)->isConstant() && d->getIn(1)->isConstant())
            {
                const uint32_t v = static_cast<uint32_t>(d->getIn(0)->getOffset() + d->getIn(1)->getOffset());
                if (indexed) out.bases.insert(v);
                else out.constants.insert(v);
            }
            else if (!indexed) out.others = true;
            break;
        case ghidra::CPUI_PTRADD:   // base + index * size: the base is where the pointer starts
            todo.push_back(std::make_pair(d->getIn(0), true));
            break;
        case ghidra::CPUI_INT_ADD:   // a sum: either addend may be the base (a constant that is a number, `param_1 - 1`, is dropped by PrintsAsRegister)
            todo.push_back(std::make_pair(d->getIn(0), true));
            todo.push_back(std::make_pair(d->getIn(1), true));
            break;
        default:
            if (!indexed) out.others = true;
            break;
        }
    }
    if (!todo.empty()) out.others = true;
    return out;
}

// Whether the printed code names a register at this address (`&VDP2_TVSTAT`): the engine names one only at the address its symbol is
// at -- the cached and cache-through partitions for the bus, the one on-chip address -- and prints any other as a number (a sentinel
// -1, an offset, an address in an unnamed part of a register area), which is not a claim that the pointer reaches a register.
bool PrintsAsRegister(uint32_t address)
{
    const HardwareRegister* r = FindHardwareRegister(address);
    uint32_t canonical = 0;
    if (!r || !HardwareAlias(address, canonical) || canonical != r->address) return false;
    return ClassifyPartition(canonical) == Sh2Partition::OnChip ? address == canonical
                                                                : (address >> 29) <= 1 && (address & 0x1FFFFFFFu) == canonical;
}

// What the pseudocode does not say about the hardware accesses it shows. Every access to a
// register area at a constant address is a volatile read or write by now (the symbols and ranges
// are volatile), so those ops are the accesses, at the address and width the code used. A read or
// write through a pointer that did not become one constant stays a plain load or store (patches/0009);
// when the engine proved the pointer to be one of several constants, and some are register addresses
// (the output prints `&REG`), each of those is said as the direct access would be, under "if the pointer holds ..."
// (A5-28): the cache caution, the read effects, the width cautions, the write effects.
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
    auto account = [&](uint32_t insn, uint32_t at, uint32_t size, bool write, const ghidra::Varnode* value, const std::string& prefix) {
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
                if (read && reg->access == RegisterAccess::WriteOnly)
                    note(insn, std::string("reads ") + reg->name + ", which is write-only: the value is not what was written", true);
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
        if (!named && reach == at) note(insn, Hex8(at) + " is " + regs[0]->name + " through a mirror (shown by address)", false);
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
            const PointerValues pv = ConstantsOf(ptr);
            const uint32_t size = static_cast<uint32_t>(value->getSize());
            // (a pointer that starts at a register address and is indexed is said for the register itself, as "if the index is 0")
            std::vector<std::pair<uint32_t, bool>> registers;   // (address, reached through an index)
            for (uint32_t c : pv.constants)
                if (PrintsAsRegister(c)) registers.emplace_back(c, false);
            for (uint32_t c : pv.bases)
                if (PrintsAsRegister(c) && !pv.constants.count(c)) registers.emplace_back(c, true);
            if (registers.empty()) continue;
            const uint32_t insn = canonicalOf(static_cast<uint32_t>(o->getAddr().getOffset()));
            const bool certain = pv.constants.size() == 1 && pv.bases.empty() && !pv.others;
            for (const std::pair<uint32_t, bool>& rc : registers)
            {
                const HardwareRegister* r = FindHardwareRegister(AccessAddress(rc.first, size));
                const std::string name = r ? std::string(" (") + r->name + ")" : std::string();
                std::string prefix;
                if (rc.second)
                    prefix = "if the index is 0 (the pointer is based at " + Hex8(rc.first) + name + "): ";
                else if (!certain)
                    prefix = "if the pointer holds " + Hex8(rc.first) + name + (pv.others ? " (it may hold other values too)" : "") + ": ";
                account(insn, rc.first, size, write, value, prefix);
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
                static_cast<uint32_t>(value->getSize()), write, value, std::string());
    }
    // A store to the BIOS ROM (0x00000000-0x000FFFFF through either bus partition) that the engine folded into ordinary memory: the
    // ROM drops the write (ss.c BusRW_DB_CS0_u8/u16/u32_W1: the ROM branch stores nothing), but a later read of the address in this
    // function is shown as the value written (A5-33). The store is no volatile op to hang a note on; the engine's record of how it
    // ended (patch 0017) is the only trace, so the caution is made from it.
    for (const ghidra::AccessRecord& rec : fd.getAccessFates())
    {
        if (!rec.store || rec.fate != ghidra::fate_folded_ordinary || rec.addr.isInvalid() || rec.addr.getSpace() != fd.getArch()->getDefaultCodeSpace()) continue;
        const uint32_t a = static_cast<uint32_t>(rec.addr.getOffset());
        if ((a >> 29) > 1 || (a & 0x07FFFFFFu) > 0x000FFFFFu) continue;
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

void Sh2Decompiler::DecompileInto(const std::shared_ptr<const MemorySnapshot>& snapshot,
                                  const DecompileRequest& request, std::atomic<bool>* cancel, DecompileResult& r,
                                  const PassSeed* seedIn, PassSeed* seedOut)
{
    std::string why;
    uint32_t entry = 0;
    uint8_t probe[2];
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

    // One pass: a fresh Architecture, every function registered, flow followed, read-only
    // literals applied, actions run. 'print' adds the markup output.
    auto runPass = [&](const CalleeAliases& callees, bool print, CalleeAliases* targets) {
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
        if (print && fd->numCalls() > 0)
        {
            ReturnCheck probe(*fd, exec, prototypes, flowTables);
            probe.SetTolerant();
            CallEffects unused;
            probe.Run(unused);
            uint32_t from = 0;
            if (probe.FrameEscape(from)) fd->getScopeLocal()->declareHostAlias(from);
        }
        for (const auto& ro : scan.readonly)
            if (!sharedLiteral(ro.first, ro.second))
                arch->symboltab->setPropertyRange(ghidra::Varnode::readonly,
                                              ghidra::Range(ram, ro.first, ro.first + ro.second - 1));

        // A check-point of ours at each phase boundary: the engine's last one in a phase can come
        // well before the phase ends (block generation after flow, the tail of the action tree),
        // and our own raw-p-code scan has none. A cancel requested anywhere in a phase is then
        // honoured in that phase, never carried into the next one's work.
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
                // can show.
                uint32_t c = 0;
                uint8_t code[2];
                if (CanonicalAddress(static_cast<uint32_t>(t), c) && snapshot->Read(c, code, 2))
                    (*targets)[c].insert(static_cast<uint32_t>(t));
            }
        }
        if (!print)
        {
            // The pass boundary: a cancel raised in the tail of these actions is honoured here, in
            // Actions, not carried through the next pass's Init (Architecture construction has no
            // check-point of its own) into its Flow.
            checkCancel();
            return std::string();
        }

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
                                 ": pseudocode for this function is not trustworthy");
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
        bool restartsExhausted = false, tableIncomplete = false;
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
                if (cutHere)
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
                    w.text = "flow reaches bytes that do not decode as an instruction and stops there (embedded "
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
        else if (r.unsupported) r.prototypeWithheld = "unsupported";
        else if (r.unimplemented) r.prototypeWithheld = "unimplemented";
        CallEffects effects;
        if (r.prototypeWithheld.empty()) 
        {
            AddJumpTables(*fd, flowTables);
            AddFlowTables(*fd, flowTables);
            r.prototypeWithheld = WithholdReason(*fd, prototypes, exec, entry, flowTables, effects);
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
    if (seedIn)
    {
        knownLiterals = seedIn->literals;
        first = seedIn->callees;
    }
    else if (request.registerCallees)
    {
        runPass(CalleeAliases(), false, &first);
    }
    if (seedOut)
    {
        seedOut->literals = knownLiterals;
        seedOut->callees = first;
    }
    printed = runPass(first, true, &callees);
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
                                                    uint32_t address, int count)
{
    // Never throws, whatever the heap does (A6-16): an allocation failure ends the listing with a line that
    // says so, and the engine is not touched again. Everything that can allocate is inside the try.
    AsmLines em;
    try
    {
        // Every way into the engine checks this: disassembly builds an Architecture too, which would
        // reset the same possibly half-reset translator.
        if (gEngineDamaged) return std::vector<std::string>{ std::string("<unavailable: ") + kEngineDamaged + ">" };
        std::string error;
        if (!SaturnArchitecture::StartLibrary(mConfigDir, error)) return std::vector<std::string>{ "<" + error + ">" };
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
        }
    }
    catch (...)
    {
        // Out of memory inside the engine (or anything else not its own): treated as Decompile
        // treats it -- the engine may be damaged, so nothing touches it again. The listing so far is
        // cut short, and says so: a caller reading it would otherwise take it for the whole.
        gEngineDamaged = true;
        try
        {
            em.lines.push_back("<stopped: out of memory or an unexpected exception>");
        }
        catch (...)
        {
        }
    }
    return std::move(em.lines);   // a move: a copy would allocate outside the try
}

}  // namespace decomp
}  // namespace sfe
