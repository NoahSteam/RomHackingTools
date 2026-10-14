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
// Each indirect jump's recovered destinations, by the jump's address: as flow recovered them, and
// as the function holds them at the end. Both, because the actions can remove a table they have
// resolved -- one destination becomes a plain goto (RuleSwitchSingle) -- while the jump itself is
// still in the instruction it came from.
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

    // Empty when every return reached goes to the caller, else why not. 'effects' is then this
    // function's own summary.
    std::string Run(CallEffects& effects)
    {
        State start;
        start.reached = true;
        for (int i = 0; i < kTracked; ++i) Set(start, mReg[i], i == kR15 ? Value::Stack(0) : Value::Entry(i));
        mPreserved = origin::kRegisters;
        mStoresThrough = 0;
        mOut = 0;
        mLeaks = 0;
        mFrameValues = 0;
        std::map<uint32_t, State> in;
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
                work.insert(o.first);
            }
        }
        effects.preserved = mPreserved;
        effects.storesThrough = mStoresThrough;
        effects.outOrigins = mOut;
        effects.leaks = mLeaks;
        effects.frameValues = mFrameValues;
        return std::string();
    }

private:
    static constexpr size_t kMaxSteps = 200000;
    static constexpr int kTracked = 22;   // the registers origin:: names, in its order
    static constexpr int kR15 = 15, kPr = 21;

    struct Value
    {
        enum Kind : uint8_t { Fuzzy, Reg, Sp, Imm } kind = Fuzzy;
        int64_t v = 0;               // Reg: the register's index; Sp: the offset; Imm: the constant
        uint32_t from = origin::kAll;   // Fuzzy: what it can come from
        static Value Entry(int r) { Value x; x.kind = Reg; x.v = r; return x; }
        static Value Stack(int64_t off) { Value x; x.kind = Sp; x.v = off; return x; }
        static Value Const(int64_t c) { Value x; x.kind = Imm; x.v = c; return x; }
        static Value From(uint32_t o) { Value x; x.from = o; return x; }
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
        bool operator==(const Value& o) const { return kind == o.kind && v == o.v && (kind != Fuzzy || from == o.from); }
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
        return f == s.vars.end() ? Value() : f->second;
    }

    void Set(State& s, const ghidra::VarnodeData& vn, const Value& v) const
    {
        for (auto it = s.vars.begin(); it != s.vars.end();)
        {
            const bool overlap = std::get<0>(it->first) == vn.space && std::get<1>(it->first) < vn.offset + vn.size &&
                                 vn.offset < std::get<1>(it->first) + std::get<2>(it->first);
            it = overlap ? s.vars.erase(it) : std::next(it);
        }
        s.vars[KeyOf(vn)] = v;
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
            if (slot.first + 4 > from && slot.first < to) slot.second = Value::From(slot.second.Origins() | stored);
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

    // Keeps in 'into' what both states agree on, the rest as its origins; true when 'into' changed.
    static bool Join(State& into, const State& from)
    {
        bool changed = false;
        if (from.escaped && !into.escaped) into.escaped = changed = true;
        auto merge = [&changed](Value& a, const Value& b) {
            if (a == b) return;
            const Value m = Value::From(a.Origins() | b.Origins());
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
        auto t = mCallAt.find(at);
        if (t != mCallAt.end())
        {
            auto p = mLent.find(t->second);
            if (p != mLent.end()) e = p->second.effects;
        }
        Apply(s, e);
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
            if (!(e.preserved & (1u << r))) Set(s, mReg[r], Value::From(out));
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
        for (int r = 0; r < kTracked; ++r)
        {
            const Value v = Get(s, mReg[r]);
            const bool same = r == kR15 ? v.kind == Value::Sp && v.v == 0 : v.kind == Value::Reg && v.v == r;
            if (same) continue;
            mPreserved &= ~(1u << r);
            mOut |= v.Origins();
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
            return "an instruction that does not decode";
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
                Value r = Value::From(x.Origins() | y.Origins());
                if (x.kind == Value::Sp && y.kind == Value::Imm) r = Value::Stack(Wrap(sub ? x.v - y.v : x.v + y.v));
                else if (!sub && x.kind == Value::Imm && y.kind == Value::Sp) r = Value::Stack(Wrap(x.v + y.v));
                else if (x.kind == Value::Imm && y.kind == Value::Imm) r = Value::Const(sub ? x.v - y.v : x.v + y.v);
                // An index added to a stack address stays in the stack (the engine's own stack analysis
                // takes pointer arithmetic so too): somewhere in it, not the index's memory.
                else if (x.kind == Value::Sp || (!sub && y.kind == Value::Sp)) r = Value::From(origin::kStack);
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
                // sleep (Sleep_Standby) changes no register or memory; any other user op, anything.
                if (mFd.getArch()->userops.getOp(static_cast<ghidra::uint4>(op.in[0].offset))->getName() != "Sleep_Standby")
                    Apply(s, CallEffects());
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
                if (!found && !Return(s, op.in[0])) return "an indirect jump it does not resolve";
                falls = false;
                break;
            }
            case ghidra::CPUI_RETURN:
                if (!Return(s, op.in[0])) return "a return not to its caller";
                falls = false;
                break;
            default:
                if (op.hasOut)
                {
                    uint32_t o = 0;
                    for (const ghidra::VarnodeData& vn : op.in) o |= Get(s, vn).Origins();
                    Set(s, op.out, Value::From(o));
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
};

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
    // A return that does not go back to the caller (PR replaced, or saved after a call changed it)
    // is a jump somewhere else, not a return to the call site the signature describes.
    return ReturnCheck(fd, exec, lent, tables).Run(effects);
}

// What one pass learned from a function's raw p-code, before any action has run.
struct RawScan
{
    std::set<uint32_t> literalWords;   // start of each literal-pool load marked read-only
    uint32_t literalLoads = 0;
    std::vector<std::pair<uint32_t, int>> readonly;   // (address, size) ranges to mark
    bool unsupported = false;
    uint32_t unsupportedAt = 0;
    std::set<uint32_t> insns;   // every instruction with p-code, before any action removes some
    std::set<uint32_t> halted;  // ...of which these failed to decode: flow stopped at them
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
        if (it->second->getHaltType() & (ghidra::PcodeOp::badinstruction | ghidra::PcodeOp::unimplemented))
            scan.halted.insert(a);
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
std::vector<Line> LinesFromMarkup(const std::string& packed, const ghidra::Funcdata& fd, const ghidra::AddrSpaceManager* spaces)
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

void Sh2Decompiler::Enter(DecompilePhase phase)
{
    if (phase != DecompilePhase::Teardown) mWorkPhase = phase;
    if (mObserver) mObserver(phase);
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

namespace
{
std::set<uint32_t> gNoReturnForTests;
}  // namespace

void Sh2Decompiler::SetNoReturnForTests(const std::set<uint32_t>& entries)
{
    gNoReturnForTests = entries;
}

namespace
{

// What the pseudocode does not say about the hardware accesses it shows. Every access to a
// register area is a volatile read or write by now (the symbols and ranges are volatile), so
// those ops are the accesses, at the address and width the code used. A read through a pointer
// that never became a constant stays a plain load (patches/0009) and says nothing here: which
// register it reaches depends on run time.
std::vector<HardwareNote> HardwareNotes(const ghidra::Funcdata& fd, const std::function<uint32_t(uint32_t)>& canonicalOf)
{
    std::set<std::pair<uint32_t, std::string>> seen;
    std::vector<HardwareNote> out;
    auto note = [&](uint32_t at, std::string text, bool caution) {
        if (seen.insert(std::make_pair(at, text)).second) out.push_back(HardwareNote{ at, std::move(text), caution });
    };
    std::set<const HardwareRegister*> written;
    std::vector<std::pair<uint32_t, const HardwareRegister*>> splitReads;
    for (auto it = fd.beginOpAlive(); it != fd.endOpAlive(); ++it)
    {
        const ghidra::PcodeOp* o = *it;
        if (o->code() != ghidra::CPUI_CALLOTHER || o->numInput() < 2 || !o->getIn(0)->isConstant()) continue;
        const ghidra::UserPcodeOp* u = fd.getArch()->userops.getOp(static_cast<ghidra::uint4>(o->getIn(0)->getOffset()));
        const bool read = u && u->getType() == ghidra::UserPcodeOp::volatile_read;
        const bool write = u && u->getType() == ghidra::UserPcodeOp::volatile_write;
        if (!read && !write) continue;
        const ghidra::Varnode* value = read ? o->getOut() : (o->numInput() > 2 ? o->getIn(2) : nullptr);
        if (!value) continue;
        const uint32_t at = static_cast<uint32_t>(o->getIn(1)->getOffset());
        const uint32_t size = static_cast<uint32_t>(value->getSize());
        const uint32_t insn = canonicalOf(static_cast<uint32_t>(o->getAddr().getOffset()));
        const std::vector<const HardwareRegister*> regs = HardwareRegistersCovering(at, size);
        std::string text;
        bool ignored = false;
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
                if (write && reg->access == RegisterAccess::KeyedWrite)
                    note(insn, std::string(reg->name) + " takes no " + std::to_string(size) +
                                   "-byte write: it is written by a keyed word at " +
                                   (std::string(reg->name) == "SH2_RSTCSR" ? "0xFFFFFE82" : "0xFFFFFE80") +
                                   "; this write does nothing", true);
                if (read && reg->onRead) note(insn, std::string(reg->name) + ": " + reg->onRead, false);
                if (write) written.insert(reg);
                if (read && reg->split) splitReads.emplace_back(insn, reg);
            }
            if (regs.size() > 1)
            {
                std::string names = regs[0]->name;
                for (size_t i = 1; i < regs.size(); ++i) names += (i + 1 == regs.size() ? " and " : ", ") + std::string(regs[i]->name);
                const char* whole = DocumentedWideAccess(at, size);
                if (whole)
                    note(insn, std::string(whole) + ": one " + std::to_string(size) + "-byte " + (read ? "read" : "write") +
                                   " of " + names + ", as documented (shown under the first)", false);
                else
                    note(insn, "one " + std::to_string(size) + "-byte " + (read ? "read" : "write") + " covers " + names +
                                   " (shown under the first)", true);
            }
        }
        if (regs.empty()) continue;   // an unnamed register: nothing to add to the address shown
        // The engine names a register only at the address its symbol is at: the cached and
        // cache-through partitions for the bus, the one on-chip address. Any other alias is
        // shown as an address.
        uint32_t canonical = 0;
        HardwareAlias(at, canonical);
        const bool named = ClassifyPartition(canonical) == Sh2Partition::OnChip
                               ? at == canonical
                               : (at >> 29) <= 1 && (at & 0x1FFFFFFFu) == canonical;
        if (!named) note(insn, Hex8(at) + " is " + regs[0]->name + " through a mirror (shown by address)", false);
    }
    // A register read back in a function that also writes it, when the read returns something
    // else: say what.
    for (const auto& r : splitReads)
        if (written.count(r.second)) note(r.first, std::string(r.second->name) + ": " + r.second->split, false);
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

// The instructions flow reached, as merged [lo, hi) ranges. 'insns' holds every instruction that
// had p-code at some point; nop has none, and a delay slot's p-code carries its branch's address,
// so both are added here. The caller leaves out the instructions flow halted at (memory not there,
// or not decoding), and a delay slot is added only when it decodes.
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
        DecompileInto(snapshot, request, cancel, r);
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
    r.elapsedMs = MsSince(t0);
    return r;
}

void Sh2Decompiler::DecompileInto(const std::shared_ptr<const MemorySnapshot>& snapshot,
                                  const DecompileRequest& request, std::atomic<bool>* cancel, DecompileResult& r)
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
        for (const HardwareRange& h : VolatileAliases(v.first, v.second)) userVolatile.push_back(h);
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
        // ...and everything else in a register area, every alias included: an access wider than
        // one register (no symbol contains it, so the range decides), the unnamed ones (SCSP
        // slots, the FIFOs), and the mirrors code can reach a register through.
        for (const HardwareRange& h : HardwareRanges())
            arch->symboltab->setPropertyRange(ghidra::Varnode::volatil, ghidra::Range(ram, h.first, h.last));
        // The user's shared memory, through every alias that reaches it (VolatileAliases).
        for (const HardwareRange& v : userVolatile)
            arch->symboltab->setPropertyRange(ghidra::Varnode::volatil, ghidra::Range(ram, v.first, v.last));

        for (const auto& ro : knownLiterals)
            if (!sharedLiteral(ro.first, ro.second))
                arch->symboltab->setPropertyRange(ghidra::Varnode::readonly,
                                                  ghidra::Range(ram, ro.first, ro.first + ro.second - 1));
        Enter(DecompilePhase::Flow);
        fd->followFlow(ghidra::Address(ram, 0), ghidra::Address(ram, ram->getHighest()));
        if (fd->hasNoCode()) return std::string();

        const RawScan scan = ScanRawPcode(*fd, *snapshot);
        knownLiterals = scan.readonly;
        JumpDests flowTables;
        AddJumpTables(*fd, flowTables);
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
        if (!print) return std::string();

        checkCancel();
        Enter(DecompilePhase::Print);
        r.name = fd->getName();
        r.bodyBytes = static_cast<uint32_t>(fd->getSize());
        r.unimplemented = fd->hasUnimplemented();
        r.unsupported = scan.unsupported;
        r.unsupportedAt = canonicalOf(scan.unsupportedAt);
        // The body: what flow reached, now (a restart in the actions can flow further) and before
        // the actions removed what they found dead.
        std::set<uint32_t> insns = scan.insns;
        for (auto it = fd->beginOpMain(); it != fd->endOpMain(); ++it)
            insns.insert(static_cast<uint32_t>(it->second->getAddr().getOffset()));
        for (uint32_t h : scan.halted) insns.erase(h);
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
        // indirect jump ends up (a table, a call, a return) is recorded at its address.
        for (uint32_t a : scan.insns)
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
                                                      ", assumed to return to this function's caller" });
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
            r.prototypeWithheld = WithholdReason(*fd, prototypes, exec, entry, flowTables, effects);
        }
        if (r.prototypeWithheld.empty())
        {
            r.prototype = PrototypeOf(*fd);
            r.prototype.effects = effects;
        }

        std::ostringstream markup;
        arch->print->setOutputStream(&markup);
        arch->print->setMarkup(true);
        arch->print->setPackedOutput(true);
        arch->print->docFunction(fd);
        arch->print->setMarkup(false);
        r.lines = LinesFromMarkup(markup.str(), *fd, arch.get());
        const std::string e = errors.str();
        if (!e.empty()) r.warnings.push_back(e);
        return std::string("ok");
    };

    CalleeAliases callees;
    std::string printed;
    if (request.registerCallees)
    {
        // The callees reported are the printed pass's own: the first pass can take a jump for a call
        // that the second, with the literals folded, follows as a goto.
        CalleeAliases first;
        runPass(CalleeAliases(), false, &first);
        printed = runPass(first, true, &callees);
    }
    else
    {
        printed = runPass(CalleeAliases(), true, &callees);
    }
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
    // Every way into the engine checks this: disassembly builds an Architecture too, which would
    // reset the same possibly half-reset translator.
    if (gEngineDamaged) return std::vector<std::string>{ std::string("<unavailable: ") + kEngineDamaged + ">" };
    std::string error;
    if (!SaturnArchitecture::StartLibrary(mConfigDir, error)) return std::vector<std::string>{ "<" + error + ">" };
    AsmLines em;
    try
    {
        std::ostringstream errors;
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
        // treats it -- the engine may be damaged, so nothing touches it again.
        gEngineDamaged = true;
    }
    return em.lines;
}

}  // namespace decomp
}  // namespace sfe
