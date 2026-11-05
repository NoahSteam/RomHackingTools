// Area 7 of the review program: the pseudocode-to-assembly mapping. Shared by the game-code run
// (Sh2DecompilerTests.cpp, TestGameCode) and the generator (Sh2GeneratedTests.cpp).
//
// The oracle: every token address lies in the body; a line several instructions contribute to is
// marked; navigation round-trips. Concretely, for one DecompileResult:
//   1. Token addresses. Every non-zero token address is even, lies in the body (the instructions
//      flow reached, delay slots included) or is named by a flow warning, and is the start of an
//      instruction that decodes. Counted, not failed, because the word is what the line names: the
//      word flow halted at as bad data (it may be odd: a table entry that is no address, A7-6, patch
//      0052; an odd token address anywhere else, or in a function entered at an odd address, fails). A line's
//      firstAddr is its lowest token address, 0 with none. A token's range lies in its line's
//      text. A call token (FuncName with a target) came from a call or a jump.
//   2. Marking. A line from several instructions says so in its hover text (LineOriginText), and
//      lists the addresses; a line from one names it; a line from none has no text and no jump.
//   3. Round trip. A click goes to firstAddr; the lines an instruction owns (those with a token of
//      it) hold the line it came from. A delay slot's instruction is its own (A7-10, patch 0075): the
//      line it made carries its address, not its branch's, and so does the ledger's `insn` of an
//      access it makes (a ledger access at a delayed branch is a violation, `rte`'s own pops apart).
//   4. Coverage. Every access the ledger lists has a token of its instruction on some line, and
//      every call in the final p-code has one; a call removed from the p-code is named by a
//      "Removing unreachable block" warning at or before it. The rest of the body (register
//      moves and arithmetic folded into the expression that uses them) has no token of its own by
//      design (A7-7) and is counted.
#pragma once

#include "Debug/Sh2Disasm.h"
#include "Decompiler/Sh2Decompiler.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace dts
{

struct MappingAudit
{
    // Violations (each must be 0).
    int odd = 0;               // a token address that is odd
    int outside = 0;           // ...not in the body and not named by a flow warning
    int notInsn = 0;           // ...where the word does not decode as an instruction
    int firstAddrWrong = 0;    // a line whose firstAddr is not its lowest token address
    int badRange = 0;          // a token whose range is not inside its line's text
    int callWithoutCall = 0;   // a FuncName token with a target whose instruction is no call or jump
    int unmarked = 0;          // a line from several instructions whose hover text does not say so
    int misnamed = 0;          // a hover text that does not hold its line's addresses
    int accessUnmapped = 0;    // a ledger access printed from, with no token of its instruction anywhere
    int accessOffLine = 0;     // (counted, not failed) the token is on a line other than MemoryAccess::firstLine names: the ledger names the
                               // line of the pointer expression when the access is printed through another op's tokens
    int accessAtBranch = 0;    // a ledger access whose instruction is a delayed branch that makes no access of its own (A7-10: it is the slot's)
    int foldedInsns = 0;       // (counted) body instructions the way back answers Folded
    int accessAtRte = 0;       // ...and of an rte, whose own pops of SR and PC they are (counted; before patch 0075 accessAtBranch held these too)
    int frameStoreMissing = 0; // a line that assigns a frame local at a load or a call and names none of the stores that wrote it (A7-11)
    int callUnmapped = 0;      // a call in the final p-code that no token came from
    int roundTrip = 0;         // a line whose firstAddr does not lead back to it (the lines an instruction owns do not hold it)
    int backWrong = 0;         // the way back (LineForInstruction, A7-5): an instruction a token names whose lookup is not an owner, or a first address whose lookup
                               // is a line with another first address
    // What the corpus looks like (counted).
    int functions = 0, lines = 0, linesWithAddr = 0, tokens = 0, tokensWithAddr = 0;
    int single = 0, multi = 0, multiNonAdjacent = 0, widest = 0;
    int nonCodeLines = 0;      // a line with no instruction (declarations, braces, case labels)
    int bodyInsns = 0, bodyMapped = 0;
    int unmappedSlots = 0;     // a delay slot instruction no token carries (folded into the expression that uses it, or removed)
    int slotTokens = 0;        // a token at a delay slot's own address (A7-10: the line the slot made; before patch 0075 there were none, A7-9)
    int frameAssigns = 0;      // lines that assign a frame local at a load or a call, with a folded store to the word (A7-11), counted
    int slotCaseTokens = 0;    // ...of which on a case label's line (A7-9: a jump table destination that is a slot, decoded a second time)
    int badDataTokens = 0;     // a token at a word flow halted at as bad data (`halt_baddata();`): the word, by design
    int unmappedMemory = 0;    // an instruction that accesses memory (not a literal load), no token, in the body
    int unmappedCall = 0;      // a call or jump no token carries
    int callGone = 0;          // ...of which no longer in the final p-code (removed with unreachable code or as dead)
    int callGoneUnsaid = 0;    // ...and no warning names a removed block at or before it (counted as a violation)
    int unmappedBranch = 0;    // a conditional branch no token carries
    // Area 7 round 2 (A7-10), counted: a line whose every address is a delayed branch and that holds no keyword or call name of its own.
    // Before the slot's ops were marked (patch 0075) these were the lines the slot instruction made, addressed to the branch, and a click on
    // them showed the row before the instruction that made them (1,397 and 3,357 in the two images). What is left is a copy a merge puts at
    // the end of a block, which sits on the block's branch by design; the mutation `tokslot` brings the others back.
    int slotMadeLines = 0;
    // Area 7 round 3 (A7-19), counted: a line that assigns a frame local or a global a bare value (a name or a number, under a cast or
    // not: no operator, no load, no call; not a local's address, which the add the line names computes), with a store the engine folded
    // into that variable, and that names none of those stores (a number the line names the `mov #i` or literal load of is that
    // instruction's). The statement is a copy the engine placed elsewhere: a merge copy or a call's copy of a frame word at the end of a
    // block (it carries the block's last instruction, a branch or any other), a held value at a return or a call (A7-15). The store that
    // wrote the variable is then on no line of its own, and a click on the line goes to an instruction that neither writes nor computes
    // it. The line's drawn tokens say at what: a call or a return (A7-15), another branch, a load (A7-16's shape: a global named after a
    // hardware register is its load), or another instruction (A7-19 is the branches and the others). A copy that names
    // a store of the variable, but the wrong one (A7-20), is not seen here.
    int copyLines = 0, copyStoreMissing = 0, copyFrameMissing = 0, copyGlobalMissing = 0;
    int copyAtBranch = 0, copyAtCall = 0, copyAtReturn = 0, copyAtLoad = 0, copyAtOther = 0;
    int copyUnconnected = 0;   // ...not counted in copyStoreMissing: no store of the variable on a path through the line (a register's merge)
    // Area 7 fixer (A7-22, A7-23). What a store wrote and what made a number, read from the instruction words (RegisterBefore).
    int storeValueOther = 0;   // a line `V = X;` that names a store of V whose register holds, on every path into it, a value the line does not show
                               // (another parameter or number; a parameter where the line shows a register variable): a violation (A7-23)
    int copyOtherValue = 0;    // ...not counted in copyStoreMissing: every store of the variable writes a value the line does not show (counted)
    int constLines = 0;        // lines `V = <number>;` drawn at one instruction (counted)
    int constAtJoin = 0;       // ...drawn at a join's first instruction (a target of the function's own branches) that makes no number, or another
                               // one: the engine's collapsed MULTIEQUAL, at its block's start (A7-22, a violation)
    std::vector<std::string> problems;   // the first few of each kind, "kind: where: what"

    void Add(const MappingAudit& o)
    {
        odd += o.odd; outside += o.outside; notInsn += o.notInsn; firstAddrWrong += o.firstAddrWrong; badRange += o.badRange;
        callWithoutCall += o.callWithoutCall; unmarked += o.unmarked; misnamed += o.misnamed; accessUnmapped += o.accessUnmapped; accessOffLine += o.accessOffLine; callUnmapped += o.callUnmapped; callGone += o.callGone; callGoneUnsaid += o.callGoneUnsaid;
        accessAtBranch += o.accessAtBranch; accessAtRte += o.accessAtRte; slotCaseTokens += o.slotCaseTokens; frameStoreMissing += o.frameStoreMissing; backWrong += o.backWrong; foldedInsns += o.foldedInsns; frameAssigns += o.frameAssigns;
        roundTrip += o.roundTrip;
        functions += o.functions; lines += o.lines; linesWithAddr += o.linesWithAddr; tokens += o.tokens;
        tokensWithAddr += o.tokensWithAddr; single += o.single; multi += o.multi; multiNonAdjacent += o.multiNonAdjacent;
        widest = widest > o.widest ? widest : o.widest;
        nonCodeLines += o.nonCodeLines; bodyInsns += o.bodyInsns; bodyMapped += o.bodyMapped; unmappedSlots += o.unmappedSlots;
        slotTokens += o.slotTokens; badDataTokens += o.badDataTokens; unmappedMemory += o.unmappedMemory; unmappedCall += o.unmappedCall;
        unmappedBranch += o.unmappedBranch;
        slotMadeLines += o.slotMadeLines;
        copyLines += o.copyLines; copyStoreMissing += o.copyStoreMissing; copyFrameMissing += o.copyFrameMissing; copyGlobalMissing += o.copyGlobalMissing;
        copyAtBranch += o.copyAtBranch; copyAtCall += o.copyAtCall; copyAtReturn += o.copyAtReturn; copyAtLoad += o.copyAtLoad; copyAtOther += o.copyAtOther;
        copyUnconnected += o.copyUnconnected;
        storeValueOther += o.storeValueOther; copyOtherValue += o.copyOtherValue; constLines += o.constLines; constAtJoin += o.constAtJoin;
        for (const std::string& p : o.problems) Keep(p);
    }
    // Three examples of each kind of problem are kept; the prefix up to the first ':' is the kind.
    void Keep(const std::string& p)
    {
        const std::string kind = p.substr(0, p.find(':'));
        int same = 0;
        for (const std::string& q : problems)
            if (q.compare(0, kind.size() + 1, kind + ":") == 0) ++same;
        if (same < (std::getenv("SE_A7F_DEBUG") ? 400 : 3)) problems.push_back(p);
    }
    int Violations() const
    {
        return odd + outside + notInsn + firstAddrWrong + badRange + callWithoutCall + unmarked + misnamed + accessUnmapped + callUnmapped + callGoneUnsaid + roundTrip +
               accessAtBranch + frameStoreMissing + backWrong + copyAtCall + copyAtReturn + copyAtBranch + copyAtOther + storeValueOther + constAtJoin;
    }
};

// The checks checked (SE_GENERATED_MUTATE, SE_GAME_MAPPING_MUTATE): damage a result as one product defect would, and the audit must
// then report it. False for a kind that is not a mapping mutation.
//   tokodd      the first token with an address carries one in the middle of an instruction
//   tokoutside  ...one far outside the body
//   tokfirst    firstAddr is the highest token address, not the lowest
//   tokdrop     the instruction of the first printed access has no token anywhere
//   tokcall     the call tokens lose their instruction
//   tokunmark   (hover text) a line from several instructions is described as one; tokhover: its text lists all but the last
//   tokframe    (A7-11) the tokens that name the stores a frame local was assigned from are taken away again (the empty tokens at a folded
//               frame store's instruction)
//   tokcopy     (A7-19) a line that assigns a frame local or a global a bare value loses the tokens at the stores folded into it, as before
//               the fix: those a copy carries (empty, or the copy's own taken over) are gone and the line names no instruction
//   tokslot     (A7-10) a delay slot's instruction is addressed to its branch again: the tokens at a slot, and the ledger's accesses, move
//               back by one instruction. Needs 'word' (the instruction words), as the audit does.
inline bool MutateMapping(sfe::decomp::DecompileResult& r, const std::string& kind,
                          const std::function<bool(uint32_t, uint16_t&)>& word = std::function<bool(uint32_t, uint16_t&)>())
{
    using namespace sfe::decomp;
    if (kind == "tokcopy")
    {
        std::set<uint32_t> folded;
        for (const AccessFate& f : r.accessFates)
            if (f.fate == AccessFate::FoldedOrdinary && f.write && (f.frame || f.constantAddress)) folded.insert(f.insn);
        for (Line& l : r.lines)
        {
            const size_t eq = l.text.find(" = ");
            if (eq == std::string::npos || l.text.find_first_of("(*+[", eq) != std::string::npos) continue;
            std::vector<Token> keep;
            for (const Token& t : l.tokens)
                if (!(t.addr && folded.count(t.addr))) keep.push_back(t);
            l.tokens = keep;
            l.firstAddr = 0;
            for (const Token& t : l.tokens)
                if (t.addr && (l.firstAddr == 0 || t.addr < l.firstAddr)) l.firstAddr = t.addr;
        }
        return true;
    }
    if (kind == "tokframe")
    {
        std::set<uint32_t> folded;
        for (const AccessFate& f : r.accessFates)
            if (f.fate == AccessFate::FoldedOrdinary && f.write && f.frame) folded.insert(f.insn);
        for (Line& l : r.lines)
        {
            std::vector<Token> keep;
            for (const Token& t : l.tokens)
                if (!(t.begin == t.end && t.addr && folded.count(t.addr))) keep.push_back(t);
            l.tokens = keep;
            l.firstAddr = 0;
            for (const Token& t : l.tokens)
                if (t.addr && (l.firstAddr == 0 || t.addr < l.firstAddr)) l.firstAddr = t.addr;
        }
        return true;
    }
    if (kind == "tokslot")
    {
        auto slotOf = [&](uint32_t a) {
            uint16_t w = 0;
            if (!word || !r.InBody(a - 2) || !word(a - 2, w)) return false;
            return sfe::Sh2Decode(a - 2, w).HasDelaySlot;
        };
        for (Line& l : r.lines)
        {
            l.firstAddr = 0;
            for (Token& t : l.tokens)
            {
                if (t.addr && slotOf(t.addr)) t.addr -= 2;
                if (t.addr && (l.firstAddr == 0 || t.addr < l.firstAddr)) l.firstAddr = t.addr;
            }
        }
        for (MemoryAccess& m : r.accesses)
            if (slotOf(m.insn)) m.insn -= 2;
        return true;
    }
    if (kind == "tokodd" || kind == "tokoutside")
    {
        for (Line& l : r.lines)
            for (Token& t : l.tokens)
                if (t.addr)
                {
                    t.addr = kind == "tokodd" ? t.addr + 1 : t.addr + 0x00100000u;
                    return true;
                }
        return true;
    }
    if (kind == "tokfirst")
    {
        for (Line& l : r.lines)
            for (const Token& t : l.tokens)
                if (t.addr > l.firstAddr) l.firstAddr = t.addr;
        return true;
    }
    if (kind == "tokdrop")
    {
        if (!r.accesses.empty())
            for (Line& l : r.lines)
                for (Token& t : l.tokens)
                    if (t.addr == r.accesses[0].insn) t.addr = 0;
        return true;
    }
    if (kind == "tokcall")
    {
        for (const CallSite& c : r.calls)
            for (Line& l : r.lines)
                for (Token& t : l.tokens)
                    if (t.addr == c.insn) t.addr = 0;
        return true;
    }
    return kind == "tokunmark" || kind == "tokhover";
}

inline std::string MutatedOriginText(const std::string& kind, const sfe::decomp::Line& l)
{
    using namespace sfe::decomp;
    if (kind != "tokunmark" && kind != "tokhover") return LineOriginText(l);
    const std::vector<uint32_t> at = LineInstructions(l);
    if (at.size() < 2) return LineOriginText(l);
    Line damaged = l;
    damaged.tokens.clear();
    for (const Token& t : l.tokens)
        if (t.addr && (kind == "tokunmark" ? t.addr == at[0] : t.addr != at.back())) damaged.tokens.push_back(t);
    return LineOriginText(damaged);
}

// How an instruction touches memory, from its text: a load (the first of two operands is a memory operand), a store (the last is), a call.
struct MemoryShape
{
    bool load = false, store = false, call = false;
};
inline MemoryShape ShapeOf(const sfe::DisassembledInstruction& d)
{
    MemoryShape m;
    m.call = d.IsCall;
    if (d.IsCall || d.IsBranch) return m;
    std::vector<std::string> operands;
    int depth = 0;
    std::string cur;
    for (char c : d.Operands)
    {
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (c == ',' && depth == 0)
        {
            operands.push_back(cur);
            cur.clear();
        }
        else if (c != ' ') cur += c;
    }
    if (!cur.empty()) operands.push_back(cur);
    if (operands.size() >= 2)
    {
        m.load = operands.front().find('@') != std::string::npos;
        m.store = operands.back().find('@') != std::string::npos;
    }
    return m;
}

inline std::string MappingHex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

// Area 7 fixer (A7-22, A7-23): what a register holds before an instruction, read from the instruction words alone (the oracle does not
// ask the product). Every path into the instruction is followed back -- by fall-through, from the function's own branches to it (from a
// delayed branch's slot), across a delayed branch to its slot -- through `mov Rm,Rn` to a `mov #imm,Rn` (a number) or to the entry,
// where r4 to r7 are the parameters. Any other write of the register, a call returning between (r0 to r7), a path out of the body or
// past the bound, or paths that disagree: unknown.
struct WordValue
{
    enum Kind { Unknown, Number, Param } kind = Unknown;
    int64_t value = 0;   // the number, or the parameter's index (param_1 is r4)
    bool operator==(const WordValue& o) const { return kind == o.kind && value == o.value; }
};
// The register an instruction stores, and how many bytes; -1 when it is no store of a register.
inline int StoredRegister(uint16_t w, int& size)
{
    const int hi = w >> 12, lo = w & 0xF, m = (w >> 4) & 0xF;
    if (hi == 0x2 && lo <= 2) return size = 1 << lo, m;
    if (hi == 0x2 && lo >= 4 && lo <= 6) return size = 1 << (lo - 4), m;
    if (hi == 0x1) return size = 4, m;
    if (hi == 0x0 && lo >= 4 && lo <= 6) return size = 1 << (lo - 4), m;
    if ((w >> 8) == 0x80 || (w >> 8) == 0xC0) return size = 1, 0;
    if ((w >> 8) == 0x81 || (w >> 8) == 0xC1) return size = 2, 0;
    if ((w >> 8) == 0xC2) return size = 4, 0;
    return -1;
}
// The general registers an instruction writes (its last operand when that is a register and the instruction is no compare or test, a
// post-increment's or pre-decrement's register).
inline std::set<int> WrittenRegisters(const sfe::DisassembledInstruction& d)
{
    std::set<int> out;
    const std::string& ops = d.Operands;
    for (size_t p = ops.find('@'); p != std::string::npos; p = ops.find('@', p + 1))
    {
        const bool pre = p + 1 < ops.size() && ops[p + 1] == '-';
        const size_t r = p + (pre ? 2 : 1);
        if (r < ops.size() && ops[r] == 'r' && r + 1 < ops.size() && std::isdigit(static_cast<unsigned char>(ops[r + 1])))
        {
            size_t e = r + 1;
            while (e < ops.size() && std::isdigit(static_cast<unsigned char>(ops[e]))) ++e;
            if (pre || (e < ops.size() && ops[e] == '+')) out.insert(std::atoi(ops.c_str() + r + 1));
        }
    }
    const size_t comma = ops.rfind(',');
    const std::string last = comma == std::string::npos ? ops : ops.substr(comma + 1);
    const std::string& mn = d.Mnemonic;
    const bool reads = mn.compare(0, 4, "cmp/") == 0 || mn == "tst" || mn == "div0s" || mn.compare(0, 3, "mul") == 0 || mn.compare(0, 4, "dmul") == 0 ||
                       mn.compare(0, 3, "ldc") == 0 || mn.compare(0, 3, "lds") == 0 || d.IsBranch;
    if (!reads && last.size() >= 2 && last[0] == 'r' && last.find_first_not_of("0123456789", 1) == std::string::npos)
        out.insert(std::atoi(last.c_str() + 1));
    return out;
}
// The function's own branches, by target (not its calls): the instruction each comes from (a delayed branch's slot).
inline std::map<uint32_t, std::vector<uint32_t>> BranchSources(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word)
{
    std::map<uint32_t, std::vector<uint32_t>> into;
    for (const auto& range : r.body)
        for (uint32_t p = range.first; p < range.second; p += 2)
        {
            uint16_t w = 0;
            if (!word(p, w)) continue;
            const sfe::DisassembledInstruction d = sfe::Sh2Decode(p, w);
            if (d.IsValid && d.HasBranchTarget && !d.IsCall) into[d.BranchTarget].push_back(d.HasDelaySlot ? p + 2 : p);
        }
    return into;
}
inline WordValue RegisterBefore(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                                const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t at, int reg)
{
    const auto decode = [&](uint32_t p, sfe::DisassembledInstruction& d) {
        uint16_t w = 0;
        if (!r.InBody(p) || !word(p, w)) return false;
        d = sfe::Sh2Decode(p, w);
        return d.IsValid;
    };
    WordValue result;
    bool any = false;
    std::set<std::pair<uint32_t, int>> seen;
    std::vector<std::pair<uint32_t, int>> todo = { { at, reg } };
    int steps = 0;
    const auto give = [&](const WordValue& v) {
        if (v.kind == WordValue::Unknown || (any && !(v == result))) return false;
        result = v;
        any = true;
        return true;
    };
    while (!todo.empty())
    {
        const std::pair<uint32_t, int> st = todo.back();
        todo.pop_back();
        if (!seen.insert(st).second) continue;
        if (++steps > 4000) return WordValue();
        const uint32_t a = st.first;
        const int rg = st.second;
        std::vector<uint32_t> preds;
        sfe::DisassembledInstruction prev, prev2;
        if (decode(a - 2, prev))
        {
            // (after the slot of an unconditional delayed branch, only a branch reaches `a`)
            const bool afterJump = decode(a - 4, prev2) && prev2.HasDelaySlot && !prev2.IsConditional && !prev2.IsCall;
            if (!afterJump) preds.push_back(a - 2);
        }
        const auto in = into.find(a);
        if (in != into.end()) preds.insert(preds.end(), in->second.begin(), in->second.end());
        if (a == r.entry)
        {
            if (!(rg >= 4 && rg <= 7 && give(WordValue{ WordValue::Param, rg - 3 }))) return WordValue();
            continue;
        }
        if (preds.empty()) return WordValue();
        for (uint32_t p : preds)
        {
            sfe::DisassembledInstruction d;
            if (!decode(p, d)) return WordValue();
            // a call returned between its slot and `a`: r0 to r7 are the callee's
            sfe::DisassembledInstruction call;
            if (p + 2 == a && decode(p - 2, call) && call.IsCall && call.HasDelaySlot && rg <= 7) return WordValue();
            if (d.IsCall && !d.HasDelaySlot && rg <= 7) return WordValue();
            uint16_t w = 0;
            word(p, w);
            if ((w >> 12) == 0xE && ((w >> 8) & 0xF) == rg)
            {
                if (!give(WordValue{ WordValue::Number, static_cast<int8_t>(w & 0xFF) })) return WordValue();
                continue;
            }
            if ((w >> 12) == 0x6 && (w & 0xF) == 0x3 && ((w >> 8) & 0xF) == rg)
            {
                todo.emplace_back(p, (w >> 4) & 0xF);
                continue;
            }
            // a literal (`mov.w @(disp,pc),Rn`, `mov.l @(disp,pc),Rn`), when no delayed branch is before it (in a slot the pc is the target's)
            sfe::DisassembledInstruction before;
            if (((w >> 12) == 0x9 || (w >> 12) == 0xD) && ((w >> 8) & 0xF) == rg && !(decode(p - 2, before) && before.HasDelaySlot))
            {
                uint16_t hi = 0, lo = 0;
                const uint32_t q = (w >> 12) == 0x9 ? p + 4 + (w & 0xFFu) * 2 : (p & ~3u) + 4 + (w & 0xFFu) * 4;
                if ((w >> 12) == 0x9 ? !word(q, lo) : !(word(q, hi) && word(q + 2, lo))) return WordValue();
                const int64_t v = (w >> 12) == 0x9 ? static_cast<int16_t>(lo) : static_cast<int32_t>(static_cast<uint32_t>(hi) << 16 | lo);
                if (!give(WordValue{ WordValue::Number, v })) return WordValue();
                continue;
            }
            if (WrittenRegisters(d).count(rg)) return WordValue();
            todo.emplace_back(p, rg);
        }
    }
    return any ? result : WordValue();
}

// 'word' reads the 16-bit instruction word at a canonical address; false when not there.
// 'originText' is the hover text under test: LineOriginText unless a mutation run damages it.
inline MappingAudit AuditMapping(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                                 const std::string& where,
                                 const std::function<std::string(const sfe::decomp::Line&)>& originText = sfe::decomp::LineOriginText)
{
    using namespace sfe::decomp;
    MappingAudit a;
    a.functions = 1;
    auto problem = [&](const char* kind, const std::string& what) { a.Keep(std::string(kind) + ": " + where + ": " + what); };
    // The addresses a flow warning names: the instruction, and the slot that runs with it.
    std::set<uint32_t> named;
    for (const FlowWarning& w : r.flowWarnings)
        if (w.address)
        {
            named.insert(w.address);
            named.insert(w.address + 2);
        }
    std::set<uint32_t> mapped;   // every token address of the result
    std::set<uint32_t> insnsWithCallTokens;
    for (size_t li = 0; li < r.lines.size(); ++li)
    {
        const Line& l = r.lines[li];
        ++a.lines;
        uint32_t lowest = 0;
        for (const Token& t : l.tokens)
        {
            ++a.tokens;
            if (t.begin > t.end || t.end > l.text.size())
            {
                ++a.badRange;
                problem("range", "line " + std::to_string(li) + " has a token range [" + std::to_string(t.begin) + "," + std::to_string(t.end) + ") outside its text");
            }
            if (!t.addr) continue;
            ++a.tokensWithAddr;
            mapped.insert(t.addr);
            if (!lowest || t.addr < lowest) lowest = t.addr;
            // The word flow halted at, which did not decode (`halt_baddata();` and the case label of a table entry that led to it): the line
            // names that word, and Assembly shows it as data. It may be odd too (a table entry that is no address).
            if (std::find(r.badDataAt.begin(), r.badDataAt.end(), t.addr) != r.badDataAt.end())
            {
                ++a.badDataTokens;
                continue;
            }
            if (t.addr & 1)
            {
                // An SH-2 instruction starts at an even address. Flow stops at an odd one as at bad data (patch 0052; before it, a table entry
                // that was no address was decoded in the middle of the instruction before it, A7-6) and a function is not entered at one.
                ++a.odd;
                problem("odd", "line " + std::to_string(li) + " '" + l.text + "': token address " + MappingHex(t.addr) + " is odd");
                continue;
            }
            if (!r.InBody(t.addr) && !named.count(t.addr))
            {
                ++a.outside;
                problem("outside", "line " + std::to_string(li) + " '" + l.text + "': token address " + MappingHex(t.addr) + " is outside the body and no warning names it");
            }
            uint16_t w = 0;
            if (!word(t.addr, w) || !sfe::Sh2Decode(t.addr, w).IsValid)
            {
                ++a.notInsn;
                problem("not-instruction", "line " + std::to_string(li) + " '" + l.text + "': token address " + MappingHex(t.addr) + " is not an instruction");
                continue;
            }
            if (t.kind == TokenKind::FuncName && t.target)
            {
                const sfe::DisassembledInstruction d = sfe::Sh2Decode(t.addr, w);
                if (!d.IsCall && !d.IsBranch)
                {
                    ++a.callWithoutCall;
                    problem("call-token", "line " + std::to_string(li) + " '" + l.text + "': a call token came from " + MappingHex(t.addr) + ", which is no call or jump");
                }
            }
        }
        if (l.firstAddr != lowest)
        {
            ++a.firstAddrWrong;
            problem("first-addr", "line " + std::to_string(li) + " '" + l.text + "': firstAddr " + MappingHex(l.firstAddr) + ", lowest token address " + MappingHex(lowest));
        }
        const std::vector<uint32_t> at = LineInstructions(l);
        const std::string tip = originText(l);
        if (at.empty())
        {
            ++a.nonCodeLines;
            if (!tip.empty())
            {
                ++a.misnamed;
                problem("hover", "line " + std::to_string(li) + " has no instruction and a hover text");
            }
            continue;
        }
        ++a.linesWithAddr;
        {
            bool onlyBranches = true, control = false;
            for (const Token& t : l.tokens)
            {
                if (!t.addr) continue;
                uint16_t bw = 0;
                if (!word(t.addr, bw) || !sfe::Sh2Decode(t.addr, bw).HasDelaySlot) onlyBranches = false;
                if (t.kind == TokenKind::Keyword || t.kind == TokenKind::FuncName) control = true;
            }
            if (onlyBranches && !control) ++a.slotMadeLines;
        }
        if (at.size() == 1) ++a.single;
        else
        {
            ++a.multi;
            bool adjacent = true;
            for (size_t i = 1; i < at.size(); ++i)
                if (at[i] != at[i - 1] + 2) adjacent = false;
            if (!adjacent) ++a.multiNonAdjacent;
            if (static_cast<int>(at.size()) > a.widest) a.widest = static_cast<int>(at.size());
        }
        // The hover text: names the first address; says "N SH-2 instructions" when it is several, and then lists each
        // address (up to the eight it lists, "and n more" after); a line from one says "the SH-2 instruction at".
        // A word flow halted at is no instruction (A7-12): it is named as bytes that do not decode, and is not counted among the instructions.
        std::vector<uint32_t> insns;
        size_t halts = 0;
        for (uint32_t x : at)
        {
            if (std::find(r.badDataAt.begin(), r.badDataAt.end(), x) != r.badDataAt.end()) ++halts;
            else insns.push_back(x);
        }
        const bool saysSeveral = tip.find(" SH-2 instructions") != std::string::npos;
        if ((insns.size() > 1) != saysSeveral || (halts != 0) != (tip.find("do not decode") != std::string::npos))
        {
            ++a.unmarked;
            problem("unmarked", "line " + std::to_string(li) + " '" + l.text + "' is from " + std::to_string(insns.size()) + " instruction(s) and " + std::to_string(halts) +
                                    " halt word(s) and its hover text says: " + tip);
        }
        bool names = tip.find("0x" + MappingHex(at[0])) != std::string::npos;
        for (size_t i = 0; i < insns.size() && i < 8; ++i) names = names && tip.find("0x" + MappingHex(insns[i])) != std::string::npos;
        for (uint32_t x : at)
            if (std::find(r.badDataAt.begin(), r.badDataAt.end(), x) != r.badDataAt.end()) names = names && tip.find("0x" + MappingHex(x)) != std::string::npos;
        if (insns.size() > 8 && tip.find("and " + std::to_string(insns.size() - 8) + " more") == std::string::npos) names = false;
        if (!names)
        {
            ++a.misnamed;
            problem("hover", "line " + std::to_string(li) + " '" + l.text + "': the hover text does not hold its addresses: " + tip);
        }
    }
    // Navigation round trip: a click on a line goes to its firstAddr; the lines an instruction owns (those with a token of it)
    // are the way back, and must hold the line. Every instruction a token names owns at least one line by construction.
    {
        std::map<uint32_t, std::vector<size_t>> owns;
        for (size_t li = 0; li < r.lines.size(); ++li)
            for (uint32_t at : LineInstructions(r.lines[li])) owns[at].push_back(li);
        for (size_t li = 0; li < r.lines.size(); ++li)
        {
            const uint32_t first = r.lines[li].firstAddr;
            if (!first) continue;
            const std::vector<size_t>& back = owns[first];
            if (std::find(back.begin(), back.end(), li) == back.end())
            {
                ++a.roundTrip;
                problem("round-trip", "line " + std::to_string(li) + " '" + r.lines[li].text + "': a click goes to " + MappingHex(first) + ", which does not lead back to it");
            }
        }
    }
    // The way back from SH-2 Assembly (A7-5): the lookup finds, for every instruction a token names, one of the lines that hold it, and the one
    // whose first address it is when there is one (the line a click on that row's own line would have come from); every instruction of the body
    // that no token names is answered Folded, with the nearest instruction of its run that has a line, and counted.
    {
        const InstructionLineIndex index(r);
        std::map<uint32_t, std::vector<size_t>> owns;
        std::set<uint32_t> firsts;
        for (size_t li = 0; li < r.lines.size(); ++li)
        {
            for (uint32_t at : LineInstructions(r.lines[li])) owns[at].push_back(li);
            if (r.lines[li].firstAddr) firsts.insert(r.lines[li].firstAddr);
        }
        for (const auto& kv : owns)
        {
            const LineLookup k = index.Find(kv.first);
            const bool owner = k.kind == LineLookup::Exact && !k.lines.empty() && std::find(kv.second.begin(), kv.second.end(), k.lines[0]) != kv.second.end();
            if (!owner || (firsts.count(kv.first) && r.lines[k.lines[0]].firstAddr != kv.first))
            {
                ++a.backWrong;
                problem("back", "the instruction " + MappingHex(kv.first) + " is answered " + (owner ? "with a line that does not begin with it" : "with no line that holds it"));
            }
        }
        for (const auto& range : r.body)
            for (uint32_t p = range.first; p < range.second; p += 2)
            {
                if (owns.count(p)) continue;
                const LineLookup k = index.Find(p);
                if (k.kind != LineLookup::Folded)
                {
                    ++a.backWrong;
                    problem("back", "the body instruction " + MappingHex(p) + " that no token names is not answered Folded");
                }
                else ++a.foldedInsns;
            }
    }
    // The ledger: each access has a token of its instruction on the line the engine says it first printed on.
    for (const MemoryAccess& m : r.accesses)
    {
        {
            // (an rte pops SR and PC itself: those are the branch's own accesses)
            uint16_t bw = 0;
            if (word(m.insn, bw))
            {
                const sfe::DisassembledInstruction b = sfe::Sh2Decode(m.insn, bw);
                if (b.HasDelaySlot && b.Mnemonic == "rte") ++a.accessAtRte;
                else if (b.HasDelaySlot)
                {
                    ++a.accessAtBranch;
                    problem("access-at-branch", std::string("the ") + (m.write ? "write" : "read") + " of the instruction " + MappingHex(m.insn) + " is at a delayed branch (A7-10: it is the slot's)");
                }
            }
        }
        if (m.printed == 0) continue;   // nothing printed comes from it (A5's oracles say whether that is right)
        bool ok = false;
        if (m.firstLine >= 0 && static_cast<size_t>(m.firstLine) < r.lines.size())
            for (const Token& t : r.lines[static_cast<size_t>(m.firstLine)].tokens)
                if (t.addr == m.insn) ok = true;
        if (ok) continue;
        const std::string what = std::string(m.write ? "write" : "read") + " of the instruction " + MappingHex(m.insn);
        if (mapped.count(m.insn)) ++a.accessOffLine;
        else
        {
            ++a.accessUnmapped;
            const bool haveLine = m.firstLine >= 0 && static_cast<size_t>(m.firstLine) < r.lines.size();
            problem("access", "the " + what + " (first line " + std::to_string(m.firstLine) + (haveLine ? " '" + r.lines[static_cast<size_t>(m.firstLine)].text + "'" : std::string()) +
                                  ") has no token anywhere");
        }
    }
    // A7-11: a line that assigns a frame local at a call (`xStack_18 = ...;`) or a piece of one at a load or a call (`xStack_18._3_1_ = ...;`),
    // and not at a store, is not the code writing the local there: the value was stored earlier and the engine shows it again (a call's effect on
    // the local; a piece of it read back). The store the engine folded into the local is among the line's instructions, whichever it was.
    {
        std::vector<const AccessFate*> folded;
        for (const AccessFate& f : r.accessFates)
            if (f.fate == AccessFate::FoldedOrdinary && f.write && f.frame && f.size > 0) folded.push_back(&f);
        if (!folded.empty())
            for (size_t li = 0; li < r.lines.size(); ++li)
            {
                const std::string& text = r.lines[li].text;
                size_t p = text.find_first_not_of(' ');
                if (p == std::string::npos) continue;
                size_t q = p;
                while (q < text.size() && text[q] >= 'a' && text[q] <= 'z') ++q;
                if (text.compare(q, 6, "Stack_") != 0) continue;
                q += 6;
                uint32_t hexv = 0;
                size_t digits = 0;
                while (q < text.size() && std::isxdigit(static_cast<unsigned char>(text[q])) && digits < 8)
                {
                    const char c = text[q];
                    hexv = hexv * 16 + static_cast<uint32_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
                    ++q;
                    ++digits;
                }
                if (digits == 0) continue;
                uint32_t piece = 0;
                bool isPiece = false;
                if (text.compare(q, 2, "._") == 0)
                {
                    isPiece = true;
                    q += 2;
                    piece = static_cast<uint32_t>(std::strtoul(text.c_str() + q, nullptr, 10));
                    while (q < text.size() && text[q] != '_') ++q;
                    ++q;
                    while (q < text.size() && text[q] != '_') ++q;
                    ++q;
                }
                if (text.compare(q, 3, " = ") != 0) continue;
                const uint32_t at = 0u - hexv + piece;
                std::set<uint32_t> candidates;
                for (const AccessFate* f : folded)
                    if (at - f->address < f->size) candidates.insert(f->insn);
                if (candidates.empty()) continue;
                bool atLoadOrCall = false, atCall = false, atStore = false, named = false, callsHere = text.find("(*") != std::string::npos;
                for (const Token& t : r.lines[li].tokens) callsHere = callsHere || t.kind == TokenKind::FuncName;
                // ...and a statement that shows the load itself (`xStack_20._2_2_ = *(uint2 *)(p + 0x30) & 0x7ff;`) is the load's: the store is what the
                // code does with the value, as for a whole local assigned from a load
                // ...nor a statement that computes (`iStack_2e = (int2)xStack_10 + extraout_r0 + -0x5e;`): the copies A7-11 names are bare values
                callsHere = callsHere || text.find("*(", q) != std::string::npos || text.find('[', q) != std::string::npos ||
                            text.find(' ', q + 3) != std::string::npos;
                if (callsHere) continue;   // `xStack_18 = f(x);` is the call's result stored in the local: the call's own statement
                for (const Token& t : r.lines[li].tokens)
                {
                    uint16_t w = 0;
                    if (!t.addr || !word(t.addr, w)) continue;
                    // (what the line draws says what it is made at; the empty tokens a store was named by do not)
                    if (t.begin < t.end)
                    {
                        const MemoryShape m = ShapeOf(sfe::Sh2Decode(t.addr, w));
                        atLoadOrCall = atLoadOrCall || m.load || m.call;
                        atCall = atCall || m.call;
                        atStore = atStore || m.store;
                    }
                    named = named || candidates.count(t.addr) != 0;
                }
                // (a whole local assigned from a load is the load's value stored: the statement is the load's, and the store the code makes of it
                // is what the line is about; only a call's copy of a local, and a piece of one read back, are the shapes A7-11 names)
                if (!(isPiece ? atLoadOrCall : atCall) || atStore) continue;
                ++a.frameAssigns;
                if (named) continue;
                ++a.frameStoreMissing;
                problem("frame-store", "line " + std::to_string(li) + " '" + text + "' assigns a frame local at a load or a call and names none of the " + std::to_string(candidates.size()) +
                                           " store(s) the engine folded into it (first " + MappingHex(*candidates.begin()) + ")");
            }
    }
    // A7-19 (area 7 round 3), counted: a bare value assigned to a frame local or a global, at an instruction that does not write it.
    bool haveSources = false;
    std::map<uint32_t, std::vector<uint32_t>> sources;
    {
        std::vector<const AccessFate*> folded;
        for (const AccessFate& f : r.accessFates)
            if (f.fate == AccessFate::FoldedOrdinary && f.write && f.size > 0 && (f.frame || f.constantAddress)) folded.push_back(&f);
        if (!folded.empty())
            for (size_t li = 0; li < r.lines.size(); ++li)
            {
                const std::string& text = r.lines[li].text;
                const size_t p = text.find_first_not_of(' ');
                if (p == std::string::npos) continue;
                size_t q = p;
                while (q < text.size() && text[q] >= 'a' && text[q] <= 'z') ++q;
                const bool frame = text.compare(q, 6, "Stack_") == 0, global = text.compare(q, 3, "Ram") == 0;
                if (!frame && !global) continue;
                q += frame ? 6 : 3;
                uint32_t hexv = 0;
                size_t digits = 0;
                while (q < text.size() && std::isxdigit(static_cast<unsigned char>(text[q])) && digits < 8)
                {
                    const char c = text[q];
                    hexv = hexv * 16 + static_cast<uint32_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
                    ++q;
                    ++digits;
                }
                if (digits == 0 || (global && digits != 8)) continue;
                uint32_t piece = 0;
                if (text.compare(q, 2, "._") == 0)
                {
                    q += 2;
                    piece = static_cast<uint32_t>(std::strtoul(text.c_str() + q, nullptr, 10));
                    while (q < text.size() && text[q] != '_') ++q;
                    ++q;
                    while (q < text.size() && text[q] != '_') ++q;
                    ++q;
                }
                if (text.compare(q, 3, " = ") != 0) continue;
                // the value: a cast, then '&' or '-', then one name or number, then ';' and the end of the line
                size_t v = q + 3;
                const bool cast = v < text.size() && text[v] == '(';
                if (cast)
                {
                    const size_t close = text.find(')', v);
                    if (close == std::string::npos) continue;
                    v = close + 1;
                }
                // (the address of a local is computed by the add the line names: that is the statement's own instruction, as A7-16's; a
                // '-' is a negative number's sign, not a negation, which the `neg` the line names computes)
                if (v + 1 < text.size() && text[v] == '-' && std::isdigit(static_cast<unsigned char>(text[v + 1]))) ++v;
                const size_t valueBegin = v;
                while (v < text.size() && (std::isalnum(static_cast<unsigned char>(text[v])) || text[v] == '_')) ++v;
                if (v == valueBegin || text.compare(v, std::string::npos, ";") != 0) continue;
                if (text.find("Stack_", valueBegin) != std::string::npos) continue;
                const bool number = std::isdigit(static_cast<unsigned char>(text[valueBegin])) != 0;
                const uint32_t at = frame ? 0u - hexv + piece : hexv + piece;
                std::set<uint32_t> candidates;
                std::set<int> storeBlocks;
                for (const AccessFate* f : folded)
                {
                    if (f->frame != frame) continue;
                    const uint32_t mask = frame ? 0xFFFFFFFFu : 0x07FFFFFFu;   // a global by any mirror of its address
                    if (((at - f->address) & mask) < f->size)
                    {
                        candidates.insert(f->insn);
                        storeBlocks.insert(f->origBlock);
                    }
                }
                if (candidates.empty()) continue;
                ++a.copyLines;
                // A7-23: what the line shows, against what the stores it names wrote. A number or a parameter is a value the words can
                // give; a register variable is no parameter (a variable that holds a parameter's input is named after it).
                WordValue shownValue;
                const std::string shownText = text.substr(valueBegin, v - valueBegin);
                const bool registerVariable = !number && shownText.find("Var") != std::string::npos &&
                                              shownText.find_first_not_of("abcdefghijklmnopqrstuvwxyz") == shownText.find("Var") &&
                                              shownText.find("Var") > 0 && shownText.size() > shownText.find("Var") + 3 &&
                                              shownText.find_first_not_of("0123456789", shownText.find("Var") + 3) == std::string::npos;
                if (number)
                {
                    shownValue.kind = WordValue::Number;
                    shownValue.value = static_cast<int64_t>(std::strtoull(shownText.c_str(), nullptr, 0));
                    if (valueBegin > 0 && text[valueBegin - 1] == '-') shownValue.value = -shownValue.value;
                }
                else if (shownText.compare(0, 6, "param_") == 0 && shownText.size() > 6 && shownText.find_first_not_of("0123456789", 6) == std::string::npos)
                {
                    shownValue.kind = WordValue::Param;
                    shownValue.value = std::atoi(shownText.c_str() + 6);
                }
                std::map<uint32_t, int> storeSize;
                for (const AccessFate* f : folded)
                    if (candidates.count(f->insn)) storeSize[f->insn] = std::max(storeSize[f->insn], static_cast<int>(f->size));
                const auto writesOther = [&](uint32_t store, bool composed) {
                    uint16_t w = 0;
                    int size = 0;
                    if (!word(store, w)) return false;
                    const int reg = StoredRegister(w, size);
                    if (reg < 0) return false;
                    if (!haveSources)
                    {
                        sources = BranchSources(r, word);
                        haveSources = true;
                    }
                    const WordValue got = RegisterBefore(r, word, sources, store, reg);
                    if (got.kind == WordValue::Unknown) return false;
                    if (shownValue.kind == WordValue::Param) return !(got == shownValue);
                    if (shownValue.kind == WordValue::Number)
                    {
                        // (a parameter may be the number on the line's path: `param_4 == 0` tested before the store)
                        if (got.kind == WordValue::Param) return false;
                        if (composed) return false;   // (a number made of a word and a narrower store: each writes some of its bytes)
                        const uint64_t mask = size >= 8 ? ~0ull : (1ull << (8 * size)) - 1;
                        return ((static_cast<uint64_t>(got.value) ^ static_cast<uint64_t>(shownValue.value)) & mask) != 0;
                    }
                    return registerVariable && got.kind == WordValue::Param;
                };
                {
                    std::set<uint32_t> namedStores;
                    bool narrow = false;
                    for (const Token& t : r.lines[li].tokens)
                        if (t.addr && candidates.count(t.addr) && namedStores.insert(t.addr).second) narrow = narrow || storeSize[t.addr] < 4;
                    for (uint32_t st : namedStores)
                        if (writesOther(st, narrow && namedStores.size() > 1))
                        {
                            ++a.storeValueOther;
                            problem("store-value", "line " + std::to_string(li) + " '" + text + "' names the store at " + MappingHex(st) +
                                                       ", whose register holds another value than the line shows");
                            break;
                        }
                }
                bool named = false, atBranch = false, atCall = false, atReturn = false, atLoad = false;
                for (const Token& t : r.lines[li].tokens)
                {
                    if (!t.addr) continue;
                    named = named || candidates.count(t.addr) != 0;
                    uint16_t w = 0;
                    if (t.begin == t.end || !word(t.addr, w)) continue;
                    const sfe::DisassembledInstruction d = sfe::Sh2Decode(t.addr, w);
                    const MemoryShape m = ShapeOf(d);
                    atCall = atCall || m.call;
                    atReturn = atReturn || d.Mnemonic == "rts" || d.Mnemonic == "rte";
                    atBranch = atBranch || (!m.call && (d.IsBranch || d.HasDelaySlot));
                    atLoad = atLoad || m.load;
                    // (a number the line names the `mov #i` or the literal load of is made there: the statement is that instruction's)
                    if (number && (d.PcRel != sfe::Sh2PcRel::None || (d.Mnemonic == "mov" && d.Operands.compare(0, 1, "#") == 0))) named = true;
                    // (and a cast the line names an `exts` or `extu` of is that extension, a computation: A7-16's shape)
                    if (cast && (d.Mnemonic.compare(0, 4, "exts") == 0 || d.Mnemonic.compare(0, 4, "extu") == 0)) named = true;
                }
                if (named) continue;
                // (every store of the variable writes a value the line does not show: a register the engine merged under the variable's
                // name, which no store makes, A7-21 and A7-23. Counted apart.)
                {
                    std::set<int> kept;
                    bool any = false;
                    for (const AccessFate* f : folded)
                        if (candidates.count(f->insn) && f->frame == frame && !writesOther(f->insn, false))
                        {
                            any = true;
                            kept.insert(f->origBlock);
                        }
                    if (!any)
                    {
                        ++a.copyOtherValue;
                        continue;
                    }
                    storeBlocks = kept;
                }
                // (no store of the variable is on a path through the line, in the flow as first followed: the variable is a register the
                // engine merged under the variable's name, as the value a return reads -- a merge copy of a register variable, which has
                // no store to name. Counted apart.)
                {
                    std::set<int> lineBlocks;
                    for (const Token& t : r.lines[li].tokens)
                        if (t.addr && t.begin < t.end)
                            for (size_t k = 0; k < r.flowBlockRanges.size(); ++k)
                                if (r.flowBlockRanges[k].first <= t.addr && t.addr <= r.flowBlockRanges[k].second) lineBlocks.insert(static_cast<int>(k));
                    const auto reaches = [&r](const std::set<int>& from, const std::set<int>& to) {
                        std::set<int> seen(from.begin(), from.end());
                        std::vector<int> todo(from.begin(), from.end());
                        while (!todo.empty())
                        {
                            const int b = todo.back();
                            todo.pop_back();
                            if (to.count(b)) return true;
                            if (b < 0 || static_cast<size_t>(b) >= r.flowSuccessors.size()) continue;
                            for (int n : r.flowSuccessors[static_cast<size_t>(b)])
                                if (seen.insert(n).second) todo.push_back(n);
                        }
                        return false;
                    };
                    if (!lineBlocks.empty() && !storeBlocks.count(-1) && !reaches(storeBlocks, lineBlocks) && !reaches(lineBlocks, storeBlocks))
                    {
                        ++a.copyUnconnected;
                        continue;
                    }
                }
                ++a.copyStoreMissing;
                ++(frame ? a.copyFrameMissing : a.copyGlobalMissing);
                ++(atCall ? a.copyAtCall : atReturn ? a.copyAtReturn : atBranch ? a.copyAtBranch : atLoad ? a.copyAtLoad : a.copyAtOther);
                // (at a load it is counted: A7-16's shape, a global named after a hardware register is its load)
                const std::string what = "line " + std::to_string(li) + " '" + text + "' names none of the " + std::to_string(candidates.size()) +
                                         " store(s) folded into the variable (first " + MappingHex(*candidates.begin()) + "); a click goes to " + MappingHex(r.lines[li].firstAddr);
                if (atLoad && !atCall && !atReturn && !atBranch) a.Keep("copy-store (counted): " + where + ": " + what);
                else problem("copy-store", what);
            }
    }
    // A7-22 (the area 7 fixer): a number line drawn at the first instruction of a join that does not make the number. When every path into
    // a block sets a register to one number, the engine makes the MULTIEQUAL a COPY with the block's address; a line that names that
    // instruction names one that neither makes nor computes its value. As tools/a7_round4/const_at_other.py --join counts them: a line
    // `V = <number>;` with one drawn instruction, a target of the function's own bt/bf/bt/s/bf/s/bra/bsr (the words from the entry to its
    // highest token), that is no `mov #imm`, literal load, store, call or branch, or is a `mov #imm` or literal load of another number.
    {
        uint32_t highest = 0;
        for (const Line& l : r.lines)
            for (const Token& t : l.tokens) highest = std::max(highest, t.addr);
        std::set<uint32_t> targets;
        for (uint32_t p = r.entry; p <= highest && highest - r.entry < 0x100000u; p += 2)
        {
            uint16_t w = 0;
            if (!word(p, w)) continue;
            if ((w >> 8) == 0x89 || (w >> 8) == 0x8B || (w >> 8) == 0x8D || (w >> 8) == 0x8F)
                targets.insert(p + 4 + 2 * static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(w & 0xFF))));
            else if ((w >> 12) == 0xA || (w >> 12) == 0xB)
            {
                const int32_t d = (w & 0x800) ? static_cast<int32_t>(w & 0xFFF) - 0x1000 : static_cast<int32_t>(w & 0xFFF);
                targets.insert(p + 4 + 2 * static_cast<uint32_t>(d));
            }
        }
        // the value a `mov #imm`, `mov.w @(disp,pc)` or `mov.l @(disp,pc)` gives; false when there is none
        const auto made = [&](uint32_t p, uint16_t w, uint32_t& value) {
            uint16_t hi = 0, lo = 0;
            if ((w >> 12) == 0xE) return value = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(w & 0xFF))), true;
            if ((w >> 12) == 0x9 && word(p + 4 + (w & 0xFFu) * 2, lo)) return value = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(lo))), true;
            const uint32_t q = (p & ~3u) + 4 + (w & 0xFFu) * 4;
            if ((w >> 12) == 0xD && word(q, hi) && word(q + 2, lo)) return value = static_cast<uint32_t>(hi) << 16 | lo, true;
            return false;
        };
        for (size_t li = 0; li < r.lines.size(); ++li)
        {
            const std::string& text = r.lines[li].text;
            size_t q = text.find_first_not_of(' ');
            if (q == std::string::npos || !(std::isalpha(static_cast<unsigned char>(text[q])) || text[q] == '_')) continue;
            while (q < text.size() && (std::isalnum(static_cast<unsigned char>(text[q])) || text[q] == '_' || text[q] == '.')) ++q;
            if (text.compare(q, 3, " = ") != 0) continue;
            q += 3;
            if (q < text.size() && text[q] == '(')
            {
                const size_t close = text.find(')', q);
                if (close == std::string::npos || text.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789 ", q + 1) != close) continue;
                q = close + 1;
            }
            const size_t end = text.size() - 1;
            if (text.empty() || text[end] != ';' || end <= q) continue;
            const std::string value = text.substr(q, end - q);
            uint32_t want = 0;
            if (value == "true" || value == "false") want = value == "true" ? 1u : 0u;
            else if (value.size() == 6 && value.compare(0, 3, "'\\x") == 0 && value[5] == '\'') want = static_cast<uint32_t>(std::strtoul(value.substr(3, 2).c_str(), nullptr, 16));
            else if (value.size() == 3 && value[0] == '\'' && value[2] == '\'') want = static_cast<unsigned char>(value[1]);
            else
            {
                const size_t digits = value[0] == '-' ? 1 : 0;
                if (digits >= value.size() || !std::isdigit(static_cast<unsigned char>(value[digits]))) continue;
                const bool hex = value.compare(digits, 2, "0x") == 0;
                if (value.find_first_not_of(hex ? "0123456789abcdef" : "0123456789", digits + (hex ? 2 : 0)) != std::string::npos) continue;
                want = static_cast<uint32_t>(std::strtoull(value.c_str() + digits, nullptr, 0));
                if (digits) want = 0u - want;
            }
            std::set<uint32_t> drawn;
            for (const Token& t : r.lines[li].tokens)
                if (t.addr && t.begin < t.end) drawn.insert(t.addr);
            if (drawn.size() != 1) continue;
            ++a.constLines;
            const uint32_t at = *drawn.begin();
            uint16_t w = 0;
            if (!targets.count(at) || !word(at, w)) continue;
            const sfe::DisassembledInstruction d = sfe::Sh2Decode(at, w);
            const MemoryShape m = ShapeOf(d);
            const bool control = d.IsBranch || d.IsCall || d.Mnemonic == "rts" || d.Mnemonic == "rte";
            const bool numberInsn = (w >> 12) == 0xE || (w >> 12) == 0x9 || (w >> 12) == 0xD;
            const bool makes = numberInsn || (d.Mnemonic.compare(0, 3, "mov") == 0 && m.store) || control;
            uint32_t got = 0;
            if (makes && !(numberInsn && made(at, w, got) && got != want)) continue;
            ++a.constAtJoin;
            problem("const-join", "line " + std::to_string(li) + " '" + text + "' is drawn at " + MappingHex(at) + " (" + d.Mnemonic + " " + d.Operands +
                                      "), the first instruction of a join, which does not make the number");
        }
    }
    // Coverage of the body: which instructions no token carries, by kind.
    for (const auto& range : r.body)
        for (uint32_t p = range.first; p < range.second; p += 2)
        {
            uint16_t w = 0;
            if (!word(p, w)) continue;
            const sfe::DisassembledInstruction d = sfe::Sh2Decode(p, w);
            if (!d.IsValid) continue;
            ++a.bodyInsns;
            if (mapped.count(p))
            {
                ++a.bodyMapped;
                uint16_t pw = 0;
                if (word(p - 2, pw) && sfe::Sh2Decode(p - 2, pw).HasDelaySlot && r.InBody(p - 2))
                {
                    ++a.slotTokens;
                    // ...on a case label's line (A7-9): the destination of a table entry that is a slot, decoded a second time
                    for (const Line& l : r.lines)
                    {
                        bool here = false;
                        for (const Token& t : l.tokens) here = here || t.addr == p;
                        const size_t b = l.text.find_first_not_of(' ');
                        if (here && b != std::string::npos && (l.text.compare(b, 5, "case ") == 0 || l.text.compare(b, 8, "default:") == 0))
                        {
                            ++a.slotCaseTokens;
                            break;
                        }
                    }
                }
                continue;
            }
            uint16_t pw = 0;
            const bool slot = word(p - 2, pw) && sfe::Sh2Decode(p - 2, pw).HasDelaySlot && r.InBody(p - 2);
            if (slot) ++a.unmappedSlots;
            if (d.IsCall)
            {
                ++a.unmappedCall;
                bool inFinal = false;
                for (const CallSite& c : r.calls) inFinal = inFinal || c.insn == p;
                if (inFinal)
                {
                    ++a.callUnmapped;
                    problem("call-unmapped", MappingHex(p) + " is a call the text makes and no token came from it");
                }
                else
                {
                    // Removed with unreachable code ("Removing unreachable block (ram,0x...)": a block that begins at or before the call)
                    // the output says so; or the call is in code the actions found dead, which they do not say.
                    ++a.callGone;
                    bool said = false;
                    for (const std::string& w : r.warnings)
                    {
                        const size_t at = w.find("Removing unreachable block (ram,0x");
                        if (at == std::string::npos) continue;
                        const uint32_t block = static_cast<uint32_t>(std::strtoul(w.c_str() + at + 34, nullptr, 16));
                        if (block <= p) said = true;
                    }
                    if (!said)
                    {
                        ++a.callGoneUnsaid;
                        problem("call-gone", MappingHex(p) + " is a call that is not in the final p-code and no warning names a removed block at or before it");
                    }
                }
            }
            else if (d.IsConditional) ++a.unmappedBranch;
            // an access of memory that is not a PC-relative load (a literal pool word folds by design)
            const bool mem = d.Operands.find('@') != std::string::npos && d.PcRel == sfe::Sh2PcRel::None && !d.IsBranch;
            if (mem && !slot) ++a.unmappedMemory;
        }
    return a;
}

// Area 7 round 2 instrument (SE_GAME_TOKEN_DUMP=<file prefix>): every token of every text, with the instruction its address names, as
// tab separated rows for tools/a7_round2/*.py to read. F = a function (label, entry, status of its body), L = a line, T = a token with
// an address (line, begin, end, kind, address, in body, previous instruction has a delay slot, the instruction, the token's text).
inline void DumpMappingTokens(std::FILE* out, const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                              const char* label)
{
    using namespace sfe::decomp;
    if (!out) return;
    std::fprintf(out, "F\t%s\t%08X\t%zu\t%zu\n", label, r.entry, r.lines.size(), r.body.size());
    for (size_t li = 0; li < r.lines.size(); ++li)
    {
        const Line& l = r.lines[li];
        std::fprintf(out, "L\t%zu\t%08X\t%s\n", li, l.firstAddr, l.text.c_str());
        for (size_t ti = 0; ti < l.tokens.size(); ++ti)
        {
            const Token& t = l.tokens[ti];
            if (!t.addr) continue;
            uint16_t w = 0, pw = 0;
            std::string insn = "?";
            if (word(t.addr, w))
            {
                const sfe::DisassembledInstruction d = sfe::Sh2Decode(t.addr, w);
                insn = d.IsValid ? d.Mnemonic + " " + d.Operands : "(bad) " + MappingHex(w);
            }
            const bool slot = word(t.addr - 2, pw) && sfe::Sh2Decode(t.addr - 2, pw).HasDelaySlot && r.InBody(t.addr - 2);
            const std::string text = t.begin <= t.end && t.end <= l.text.size() ? l.text.substr(t.begin, t.end - t.begin) : std::string("?");
            std::fprintf(out, "T\t%zu\t%zu\t%u\t%u\t%d\t%08X\t%d\t%d\t%s\t%s\n", li, ti, unsigned(t.begin), unsigned(t.end), int(t.kind), t.addr,
                         r.InBody(t.addr) ? 1 : 0, slot ? 1 : 0, insn.c_str(), text.c_str());
        }
    }
}

inline void PrintMappingAudit(const MappingAudit& a, const char* label)
{
    std::printf("  mapping (%s): %d functions, %d lines (%d from one instruction, %d from several [%d not adjacent, widest %d], "
                "%d with none), %d tokens (%d with an address)\n",
                label, a.functions, a.lines, a.single, a.multi, a.multiNonAdjacent, a.widest, a.nonCodeLines, a.tokens, a.tokensWithAddr);
    std::printf("  mapping (%s): odd %d, outside the body %d, not an instruction %d, firstAddr wrong %d, bad ranges %d, call tokens "
                "from a non-call %d, several-instruction lines unmarked %d, hover text without its addresses %d, ledger accesses "
                "unmapped %d, on another line than firstLine says %d, calls with no token %d, calls removed unsaid %d, click does not lead back %d\n", label, a.odd, a.outside, a.notInsn,
                a.firstAddrWrong, a.badRange, a.callWithoutCall, a.unmarked, a.misnamed, a.accessUnmapped, a.accessOffLine, a.callUnmapped, a.callGoneUnsaid, a.roundTrip);
    std::printf("  mapping (%s): body instructions %d, %d carry a token; without one: %d delay slots, %d memory accesses outside slots, "
                "%d calls (%d not in the final p-code), %d conditional branches; %d instructions with a token are delay slots (%d of them on a case label's line); "
                "%d tokens at the bad data flow halted at (by design)\n",
                label, a.bodyInsns, a.bodyMapped, a.unmappedSlots, a.unmappedMemory, a.unmappedCall, a.callGone, a.unmappedBranch, a.slotTokens,
                a.slotCaseTokens, a.badDataTokens);
    std::printf("  mapping (%s): %d lines assign a frame local at a load or a call and have a folded store to it; %d of them name none of the stores (A7-11, a violation)\n",
                label, a.frameAssigns, a.frameStoreMissing);
    std::printf("  mapping (%s): %d lines have only a delayed branch's address and no keyword or call name (a merge copy at the end of a block; before patch 0075 also every line a "
                "delay slot made); %d ledger accesses are at a delayed branch (a violation), %d at an rte (its own pops)\n",
                label, a.slotMadeLines, a.accessAtBranch, a.accessAtRte);
    std::printf("  mapping (%s): the way back from SH-2 Assembly: %d instructions of the body are folded into a line of another instruction (answered with the nearest that has one); "
                "%d answered wrongly (a violation)\n", label, a.foldedInsns, a.backWrong);
    std::printf("  mapping (%s): %d lines assign a frame local or a global a bare value and have a folded store to it; %d of them name none of the stores (%d frame, "
                "%d global; at a call %d and a return %d (A7-15), at another branch %d and another instruction %d (A7-19): violations; at a load %d (A7-16's, a "
                "register's name is a load): counted); %d more have no store of the variable on a path through them (a register merged under its name, counted)\n",
                label, a.copyLines, a.copyStoreMissing, a.copyFrameMissing, a.copyGlobalMissing, a.copyAtCall, a.copyAtReturn, a.copyAtBranch, a.copyAtOther, a.copyAtLoad,
                a.copyUnconnected);
    std::printf("  mapping (%s): %d lines name a store whose register holds another value than they show (A7-23, a violation); %d lines assign a bare value "
                "every store of the variable writes another value of (a register merged under its name, counted); %d number lines drawn at one "
                "instruction, %d of them at a join's first instruction that does not make the number (A7-22, a violation)\n",
                label, a.storeValueOther, a.copyOtherValue, a.constLines, a.constAtJoin);
    for (const std::string& p : a.problems) std::printf("    %s\n", p.c_str());
}

}  // namespace dts
