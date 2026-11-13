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

// A7-52: does a printed line read as a statement that does something (an assignment, a call, a value returned) rather than a declaration,
// a brace, a label, or a bare `break;`? Such a line with no instruction is a click that silently does nothing.
inline bool MappingIsStatement(const std::string& text)
{
    size_t b = 0, e = text.size();
    while (b < e && std::isspace(static_cast<unsigned char>(text[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1]))) --e;
    if (e - b < 2 || text[e - 1] != ';' || text[b] == '}') return false;
    if (text[b] == '(' && text.find(" = ", b) == std::string::npos) return false;   // (the argument list of a call printed over two lines)   // (`} while( true );` closes a loop: no op of its own)
    const std::string t = text.substr(b, e - b);
    return t.find(" = ") != std::string::npos || t.find('(') != std::string::npos || t.compare(0, 7, "return ") == 0;
}

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
                               // is a line with another first address, or (A7-25) a line that draws it only as a copy materialised there while
                               // another line draws it as its own statement
    // What the corpus looks like (counted).
    int functions = 0, lines = 0, linesWithAddr = 0, tokens = 0, tokensWithAddr = 0;
    int single = 0, multi = 0, multiNonAdjacent = 0, widest = 0;
    int nonCodeLines = 0;      // a line with no instruction (declarations, braces, case labels)
    int noOriginLines = 0;     // ...of which statements the product says it names no instruction for (Line::noOrigin, A7-52; counted)
    int noInstruction = 0;     // ...a statement (an assignment, a call, a value returned) with no instruction and no word saying so, or a line
                               // that says it has none and has one, or says it without a hover text that does (A7-52, a violation)
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
    // Area 7 fixer (A7-21): a register the engine merged under a memory variable's name (`uStack_8 = uVar6;`), naming no store of it.
    int copyMergeAtBranch = 0; // ...drawn only at a branch that ends its block, which makes nothing (a violation: the line names its makers or nothing)
    int copyRegisterMade = 0;  // ...naming instructions that each write a register and are no load, store, call or branch: the makers (counted)
    int copyNoAddress = 0;     // ...naming no instruction at all: no maker was found (counted)
    int copyNumberMade = 0;    // A7-45: a number line drawn only at instructions that make the number, no store of the variable storing it as
                               // an instruction makes it (a register merged under the variable's name; counted)
    int copyInputMerged = 0;   // A7-55: a copy of the function's input drawn only at the moves that put it in a register, or naming nothing and
                               // saying so, no store of the variable storing the input (a register merged under the variable's name; counted)
    int constLines = 0;        // lines `V = <number>;` drawn at one instruction (counted)
    int numberStoreOther = 0;  // ...of a memory variable, drawn at a number instruction beside the stores it names, whose value none of those
                               // stores stores (a violation: A7-28, fixed)
    int caseOtherEntry = 0;    // case labels naming another entry of their own switch's table, or no destination of it (a violation: A7-29, fixed)
    int caseJudged = 0;        // ...the labels judged: a switch of the common `mova ; mov.w/mov.b @(r0,Rm),Rn ; add ; jmp @r0` form
    int storeAfterUse = 0;     // area 7 round 7: a copy drawn only at a call or a return naming a store that cannot run before it (A7-32, a violation)
    int constAtJoin = 0;       // ...drawn at a join's first instruction (a target of the function's own branches) that makes no number, or another
                               // one, or makes it into another register than every path into the join sets it in: the engine's collapsed
                               // MULTIEQUAL, at its block's start (A7-22, A7-26, a violation)
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
        nonCodeLines += o.nonCodeLines; noOriginLines += o.noOriginLines; noInstruction += o.noInstruction; bodyInsns += o.bodyInsns; bodyMapped += o.bodyMapped; unmappedSlots += o.unmappedSlots;
        slotTokens += o.slotTokens; badDataTokens += o.badDataTokens; unmappedMemory += o.unmappedMemory; unmappedCall += o.unmappedCall;
        unmappedBranch += o.unmappedBranch;
        slotMadeLines += o.slotMadeLines;
        copyLines += o.copyLines; copyStoreMissing += o.copyStoreMissing; copyFrameMissing += o.copyFrameMissing; copyGlobalMissing += o.copyGlobalMissing;
        copyAtBranch += o.copyAtBranch; copyAtCall += o.copyAtCall; copyAtReturn += o.copyAtReturn; copyAtLoad += o.copyAtLoad; copyAtOther += o.copyAtOther;
        copyUnconnected += o.copyUnconnected;
        storeValueOther += o.storeValueOther; copyOtherValue += o.copyOtherValue; constLines += o.constLines; constAtJoin += o.constAtJoin;
        copyMergeAtBranch += o.copyMergeAtBranch; copyRegisterMade += o.copyRegisterMade; copyNoAddress += o.copyNoAddress; copyNumberMade += o.copyNumberMade; copyInputMerged += o.copyInputMerged;
        numberStoreOther += o.numberStoreOther; caseOtherEntry += o.caseOtherEntry; caseJudged += o.caseJudged; storeAfterUse += o.storeAfterUse;
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
               accessAtBranch + frameStoreMissing + backWrong + copyAtCall + copyAtReturn + copyAtBranch + copyAtOther + storeValueOther + constAtJoin +
               copyMergeAtBranch + numberStoreOther + caseOtherEntry + storeAfterUse + noInstruction;
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
//   toksilent   (A7-52) every statement line that names an instruction loses them all and is not marked: a click that silently does nothing
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
    if (kind == "toksilent")
    {
        for (Line& l : r.lines)
        {
            if (!MappingIsStatement(l.text) || l.firstAddr == 0) continue;
            for (Token& t : l.tokens) t.addr = 0;
            l.firstAddr = 0;
            l.noOrigin = false;
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

// A7-34 (the area 7 fixer): how many bytes a variable of the text has, by its declaration (`uint2 uVar4;`, or a parameter's in the
// signature, `int2 param_3)`): a type ending in its size (int1, uint2, xunknown4, undefined8), or char, bool, short; 4 for any other or
// when the text declares it nowhere (a global).
inline int DeclaredWidth(const sfe::decomp::DecompileResult& r, const std::string& name)
{
    if (name.empty()) return 4;
    const auto widthOf = [](const std::string& type) {
        const char last = type.back();
        if (last == '1' || last == '2' || last == '4' || last == '8') return last - '0';
        if (type == "char" || type == "uchar" || type == "bool" || type == "byte") return 1;
        if (type == "short" || type == "ushort") return 2;
        return 4;
    };
    for (const sfe::decomp::Line& l : r.lines)
    {
        const std::string& t = l.text;
        // (a parameter: `<type> name,` or `<type> name)` in the signature)
        for (size_t at = t.find(" " + name); at != std::string::npos; at = t.find(" " + name, at + 1))
        {
            const size_t end = at + 1 + name.size();
            if (end >= t.size() || (t[end] != ',' && t[end] != ')')) continue;
            size_t b = at;
            while (b > 0 && (std::isalnum(static_cast<unsigned char>(t[b - 1])) || t[b - 1] == '_')) --b;
            if (b < at && (b == 0 || t[b - 1] == '(' || t[b - 1] == ',')) return widthOf(t.substr(b, at - b));
        }
        const size_t a = t.find_first_not_of(' ');
        if (a == std::string::npos || t.size() < name.size() + 2 || t.compare(t.size() - name.size() - 1, std::string::npos, name + ";") != 0) continue;
        const size_t gap = t.size() - name.size() - 1;
        if (gap == 0 || t[gap - 1] != ' ') continue;
        const std::string type = t.substr(a, gap - 1 - a);
        if (type.empty() || type.find_first_of(" =(*[") != std::string::npos) continue;
        return widthOf(type);
    }
    return 4;
}

// The offset of address `a` from `b` for a global: through any mirror of work RAM's in the cached and cache-through areas (A24-A26
// partly decoded), exact for the other areas (A7-34's triage: 0x66001034, the cache's address array, is no mirror of 0x26001034).
inline uint32_t GlobalDelta(uint32_t a, uint32_t b)
{
    return (a >> 29) <= 1 && (b >> 29) <= 1 ? (a - b) & 0x07FFFFFFu : a - b;
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
    bool byCondition = false;   // (A7-34: a number a branch's condition gives on the path, which a parameter may equal there)
    bool operator==(const WordValue& o) const { return kind == o.kind && value == o.value; }
};
// Area 7 round 14: a rule of this round's oracle fixes, switched off by SE_A7_RULE_OFF=<row numbers, comma separated> -- for the mutation
// runs that show each fixture judges its rule (the product never reads it, and an unset variable leaves every rule on).
inline bool A7RuleOff(int row)
{
    const char* v = std::getenv("SE_A7_RULE_OFF");
    if (!v) return false;
    const std::string list = std::string(",") + v + ",";
    return list.find("," + std::to_string(row) + ",") != std::string::npos;
}
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
// A7-132: the instructions of the blocks the engine says it removed as unreachable ("Removing unreachable block (ram,0x...)": a block
// that begins there and runs to the first branch, jump or return, its slot with it). The code of such a block is run by no path, so the
// edges it makes carry no value: a jump table's cases whose `jmp` the engine found dead (the comparison before it always branches
// past it, a register a call's result or a literal fixes) give a register at the join they run into no number of their own.
inline std::set<uint32_t> RemovedBlockInsns(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word)
{
    std::set<uint32_t> gone;
    if (A7RuleOff(132)) return gone;
    // (a block ends where the next instruction is another branch's target, a join the removed block does not extend into)
    std::set<uint32_t> targets;
    for (const auto& range : r.body)
        for (uint32_t p = range.first; p < range.second; p += 2)
        {
            uint16_t w = 0;
            if (!word(p, w)) continue;
            const sfe::DisassembledInstruction d = sfe::Sh2Decode(p, w);
            if (d.IsValid && d.HasBranchTarget && !d.IsCall) targets.insert(d.BranchTarget);
        }
    for (const std::string& warning : r.warnings)
    {
        const size_t at = warning.find("Removing unreachable block (ram,0x");
        if (at == std::string::npos) continue;
        uint32_t p = static_cast<uint32_t>(std::strtoul(warning.c_str() + at + 34, nullptr, 16));
        const uint32_t start = p;
        for (int steps = 0; steps < 64; ++steps, p += 2)
        {
            uint16_t w = 0;
            if (!word(p, w) || (p != start && targets.count(p))) break;
            const sfe::DisassembledInstruction d = sfe::Sh2Decode(p, w);
            gone.insert(p);
            if (!d.IsValid) break;
            if (d.IsBranch || d.Mnemonic == "rts" || d.Mnemonic == "rte")
            {
                if (d.HasDelaySlot) gone.insert(p + 2);
                break;
            }
        }
    }
    return gone;
}
inline std::map<uint32_t, std::vector<uint32_t>> BranchSources(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word)
{
    std::map<uint32_t, std::vector<uint32_t>> into;
    const std::set<uint32_t> gone = RemovedBlockInsns(r, word);
    for (const auto& range : r.body)
        for (uint32_t p = range.first; p < range.second; p += 2)
        {
            uint16_t w = 0;
            if (!word(p, w)) continue;
            const sfe::DisassembledInstruction d = sfe::Sh2Decode(p, w);
            if (d.IsValid && d.HasBranchTarget && !d.IsCall && !gone.count(p)) into[d.BranchTarget].push_back(d.HasDelaySlot ? p + 2 : p);
        }
    // A7-34 (the area 7 fixer): the edges no word names, a jump table's (`jmp @r1` to its cases), from the flow's blocks: the last
    // instruction of a block into the first of each block after it that does not follow it in memory
    for (size_t k = 0; k < r.flowSuccessors.size() && k < r.flowBlockRanges.size(); ++k)
    {
        // (a block that ends in a `jmp`'s slot)
        uint16_t pw = 0;
        const uint32_t jump = r.flowBlockRanges[k].second;
        if (!(word(jump - 2, pw) && sfe::Sh2Decode(jump - 2, pw).Mnemonic == "jmp") || gone.count(jump - 2)) continue;
        for (int n : r.flowSuccessors[k])
        {
            if (n < 0 || static_cast<size_t>(n) >= r.flowBlockRanges.size()) continue;
            const uint32_t first = r.flowBlockRanges[static_cast<size_t>(n)].first;
            std::vector<uint32_t>& from = into[first];
            if (first != jump + 2 && std::find(from.begin(), from.end(), jump) == from.end()) from.push_back(jump);
        }
    }
    return into;
}
// A7-69: a register a condition gives on the only edge into an instruction (`tst r4,r4 ; bt L`: r4 is 0 at L), for the instructions
// before the branch on the straight line to it that do not write the register. Set only by the const-join check, for the instruction
// it asks about; RegisterBefore reads it.
struct EdgeFact
{
    int reg = -1;
    int64_t value = 0;
    uint32_t edge = 0;   // the branch's address
};
inline std::vector<EdgeFact>& EdgeFacts()
{
    static thread_local std::vector<EdgeFact> facts;
    return facts;
}
// A7-131: the T bit on one edge into an instruction -- `tst r0,r0 ; bt L`'s edge to L has T set, its fall-through T clear -- for the
// instruction at `at`, set only by the const-join check while it asks what the instruction computes on that edge; TBefore reads it.
struct EdgeTruth
{
    int t = -1;
    uint32_t at = 0;
};
inline EdgeTruth& EdgeT()
{
    static thread_local EdgeTruth e;
    return e;
}
inline WordValue RegisterBefore(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                                const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t at, int reg, int depth = 0, uint32_t ignoreWriter = 0);
inline WordValue WrittenValue(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                              const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t p, int reg, int depth);
inline int TBefore(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                   const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t p, int depth);
inline bool NeverRun(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                     const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t p, int depth);
inline bool RegisterMakers(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                           const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t at, int reg, std::set<uint32_t>& makers,
                           bool throughUpdate, std::set<int>* entryRegs);
inline WordValue NumberValue(uint64_t v)
{
    return WordValue{ WordValue::Number, static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(v))) };
}
// The address a store or a load at `p` reads or writes, and its size: false when the instruction is neither or its address is not
// known (RegisterBefore of its registers; GBR is not followed).
inline bool AccessAddress(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                          const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t p, uint16_t w, bool store, uint32_t& addr, int& size, int depth)
{
    const int hi = w >> 12, n = (w >> 8) & 0xF, m = (w >> 4) & 0xF, lo = w & 0xF;
    int base = -1, index = -1;
    int64_t offset = 0;
    if (store)
    {
        if (hi == 0x2 && lo <= 2) base = n, size = 1 << lo;
        else if (hi == 0x2 && lo >= 4 && lo <= 6) base = n, size = 1 << (lo - 4), offset = -size;
        else if (hi == 0x1) base = n, size = 4, offset = (w & 0xF) * 4;
        else if ((w >> 8) == 0x80 || (w >> 8) == 0x81) base = m, size = (w >> 8) == 0x80 ? 1 : 2, offset = (w & 0xF) * size;
        else if (hi == 0x0 && lo >= 4 && lo <= 6) base = n, index = 0, size = 1 << (lo - 4);
        else return false;
    }
    else
    {
        if (hi == 0x6 && lo <= 2) base = m, size = 1 << lo;
        else if (hi == 0x6 && lo >= 4 && lo <= 6) base = m, size = 1 << (lo - 4);
        else if (hi == 0x5) base = m, size = 4, offset = (w & 0xF) * 4;
        else if ((w >> 8) == 0x84 || (w >> 8) == 0x85) base = m, size = (w >> 8) == 0x84 ? 1 : 2, offset = (w & 0xF) * size;
        else if (hi == 0x0 && lo >= 0xC) base = m, index = 0, size = 1 << (lo - 0xC);
        else return false;
    }
    const WordValue b = RegisterBefore(r, word, into, p, base, depth);
    if (b.kind != WordValue::Number) return false;
    int64_t a = b.value + offset;
    if (index >= 0)
    {
        const WordValue i = RegisterBefore(r, word, into, p, index, depth);
        if (i.kind != WordValue::Number) return false;
        a += i.value;
    }
    addr = static_cast<uint32_t>(a);
    return true;
}
// A7-69: is the edge from instruction `p` (a branch, a delayed branch's slot, or the one before `a`) to `a` never taken, by the T the
// words give (`tst #4,r0 ; bt L` with r0 2 always branches) or because `p` is never run (RegisterBefore's pruning, for the walks over
// memory too)?
inline bool EdgeImpossible(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                           const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t p, uint32_t a, int depth)
{
    if (depth >= 6) return false;
    if (p + 2 == a && NeverRun(r, word, into, p, depth + 1)) return true;
    uint16_t w = 0, bw = 0;
    if (!r.InBody(p) || !word(p, w)) return false;
    const sfe::DisassembledInstruction d = sfe::Sh2Decode(p, w);
    if (d.IsValid && ((w >> 8) == 0x89 || (w >> 8) == 0x8B) && d.BranchTarget != p + 2)
    {
        const bool taken = p + 2 != a;
        const int t = TBefore(r, word, into, p, depth + 1);
        if (t >= 0 && taken != (((w >> 8) == 0x89) == (t == 1))) return true;
    }
    if (r.InBody(p - 2) && word(p - 2, bw) && ((bw >> 8) == 0x8D || (bw >> 8) == 0x8F))
    {
        const sfe::DisassembledInstruction br = sfe::Sh2Decode(p - 2, bw);
        // (A7-118: the code after the slot is entered by another way when a branch of its own enters the slot -- `bra L` to the slot of
        // a `bf/s`, which runs on past it whatever the `bf/s`'s T says; grammar 1 seed 6601665)
        const auto slotEntered = into.find(p);
        const bool enteredElsewhere = !A7RuleOff(118) && p + 2 == a && slotEntered != into.end() && !slotEntered->second.empty();
        if (br.IsValid && br.BranchTarget != p + 2 && (p + 2 == a || br.BranchTarget == a) && !enteredElsewhere)
        {
            const int t = TBefore(r, word, into, p - 2, depth + 1);
            const bool taken = p + 2 != a;
            if (t >= 0 && taken != (((bw >> 8) == 0x8D) == (t == 1))) return true;
        }
    }
    return false;
}
// A7-34 (the area 7 fixer): what memory holds before `at`, by the stores of the function's own words on every path into it: a store of
// the same address and size whose register's value is known and that the engine folded (an access fate FoldedOrdinary: not hardware, not
// memory the request marks volatile). Any other store that may write it (its address not known, or a part of it), a call, the entry, or
// paths that disagree: unknown. The engine forwards such a store to a later read (a poll's first read of what the function stored) and
// prints the number at the read.
inline bool FoldedAccess(const sfe::decomp::DecompileResult& r, uint32_t insn, bool write)
{
    for (const sfe::decomp::AccessFate& f : r.accessFates)
        if (f.insn == insn && f.write == write && f.fate == sfe::decomp::AccessFate::FoldedOrdinary) return true;
    return false;
}
inline WordValue MemoryBefore(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                              const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t at, uint32_t addr, int size, int depth)
{
    if (depth > 6) return WordValue();
    const auto decode = [&](uint32_t p, sfe::DisassembledInstruction& d) {
        uint16_t w = 0;
        if (!r.InBody(p) || !word(p, w)) return false;
        d = sfe::Sh2Decode(p, w);
        return d.IsValid;
    };
    WordValue result;
    bool any = false;
    std::set<uint32_t> seen;
    std::vector<uint32_t> todo = { at };
    int steps = 0;
    while (!todo.empty())
    {
        const uint32_t a = todo.back();
        todo.pop_back();
        if (!seen.insert(a).second) continue;
        if (++steps > 2000 || a == r.entry) return WordValue();
        std::vector<uint32_t> preds;
        sfe::DisassembledInstruction prev, prev2;
        if (decode(a - 2, prev))
        {
            const bool afterJump = decode(a - 4, prev2) && prev2.HasDelaySlot && !prev2.IsConditional && !prev2.IsCall;
            if (!afterJump) preds.push_back(a - 2);
        }
        const auto in = into.find(a);
        if (in != into.end()) preds.insert(preds.end(), in->second.begin(), in->second.end());
        if (preds.empty()) return WordValue();
        for (uint32_t p : preds)
        {
            sfe::DisassembledInstruction d;
            if (!decode(p, d) || d.IsCall || d.Mnemonic == "rte" || d.Mnemonic == "trapa") return WordValue();
            if (EdgeImpossible(r, word, into, p, a, depth)) continue;
            uint16_t w = 0;
            word(p, w);
            int ssize = 0;
            const int reg = StoredRegister(w, ssize);
            // (A7-69: `tas.b @Rn` writes the byte it read with bit 7 set; grammar 1 seeds 2702841, 3005842)
            if ((w & 0xF0FF) == 0x401B)
            {
                const WordValue tb = RegisterBefore(r, word, into, p, (w >> 8) & 0xF, depth + 1);
                if (tb.kind != WordValue::Number) return WordValue();
                const uint32_t tx = static_cast<uint32_t>(tb.value) & 0x07FFFFFFu, ty = addr & 0x07FFFFFFu;
                if (tx + 1 <= ty || ty + static_cast<uint32_t>(size) <= tx)
                {
                    todo.push_back(p);
                    continue;
                }
                if (tx != ty || size != 1 || !FoldedAccess(r, p, true)) return WordValue();
                const WordValue old = MemoryBefore(r, word, into, p, addr, 1, depth + 1);
                if (old.kind != WordValue::Number) return WordValue();
                const WordValue set{ WordValue::Number, (old.value & 0xFF) | 0x80 };
                if (any && !(set == result)) return WordValue();
                result = set;
                any = true;
                continue;
            }
            if (reg < 0 && (w >> 8) != 0xC0 && (w >> 8) != 0xC1 && (w >> 8) != 0xC2)
            {
                todo.push_back(p);
                continue;
            }
            uint32_t sa = 0;
            if (reg < 0 || !AccessAddress(r, word, into, p, w, true, sa, ssize, depth + 1)) return WordValue();
            const uint32_t mask = 0x07FFFFFFu;
            const uint32_t x = sa & mask, y = addr & mask;
            if (x + static_cast<uint32_t>(ssize) <= y || y + static_cast<uint32_t>(size) <= x)
            {
                todo.push_back(p);
                continue;
            }
            // (A7-69: a read of the first bytes of a wider store -- `mov.w @r7,r2` after `mov.l r0,@r7` -- is the stored value's piece, the
            // SH-2 being big-endian; grammar 1 seed 3216185)
            if (x > y || y + static_cast<uint32_t>(size) > x + static_cast<uint32_t>(ssize) || !FoldedAccess(r, p, true)) return WordValue();
            const WordValue v = RegisterBefore(r, word, into, p, reg, depth + 1);
            if (v.kind != WordValue::Number) return WordValue();
            const uint64_t bytes = size >= 4 ? 0xFFFFFFFFull : (1ull << (8 * size)) - 1;
            const uint32_t drop = 8 * (static_cast<uint32_t>(ssize) - (y - x) - static_cast<uint32_t>(size));
            const WordValue masked{ WordValue::Number, static_cast<int64_t>((static_cast<uint64_t>(v.value) & 0xFFFFFFFFull) >> drop & bytes) };
            if (any && !(masked == result)) return WordValue();
            result = masked;
            any = true;
        }
    }
    return any ? result : WordValue();
}
// A7-69: a load or store through a base register with a displacement (`mov.l @(0,r14),r2`, `mov.l r0,@(0,r14)`): the base, the offset
// and the size. False for any other instruction.
inline bool BasedAccess(uint16_t w, bool store, int& base, int64_t& offset, int& size)
{
    const int hi = w >> 12, n = (w >> 8) & 0xF, m = (w >> 4) & 0xF, lo = w & 0xF;
    if (store)
    {
        if (hi == 0x2 && lo <= 2) return base = n, offset = 0, size = 1 << lo, true;
        if (hi == 0x1) return base = n, offset = (w & 0xF) * 4, size = 4, true;
        if ((w >> 8) == 0x80 || (w >> 8) == 0x81) return base = m, size = (w >> 8) == 0x80 ? 1 : 2, offset = (w & 0xF) * size, true;
        return false;
    }
    if (hi == 0x6 && lo <= 2) return base = m, offset = 0, size = 1 << lo, true;
    if (hi == 0x5) return base = m, offset = (w & 0xF) * 4, size = 4, true;
    if ((w >> 8) == 0x84 || (w >> 8) == 0x85) return base = m, size = (w >> 8) == 0x84 ? 1 : 2, offset = (w & 0xF) * size, true;
    return false;
}
// A7-69: what a based slot holds before `at` when the address is not a number (the frame, through r14): the store before it, on every
// path, through the same base register with the same offset and size, none of them writing the base register, and no other store or call
// in between that may write it. Unknown otherwise. Grammar 2 seed 3218780: `mov.l r0,@(0,r14)` ... `mov.l @(0,r14),r2`.
inline WordValue SlotBefore(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                            const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t at, int base, int64_t offset, int size, int depth)
{
    if (depth > 6) return WordValue();
    const auto decode = [&](uint32_t p, sfe::DisassembledInstruction& d) {
        uint16_t w = 0;
        if (!r.InBody(p) || !word(p, w)) return false;
        d = sfe::Sh2Decode(p, w);
        return d.IsValid;
    };
    WordValue result;
    bool any = false;
    std::set<uint32_t> seen;
    std::vector<uint32_t> todo = { at };
    int steps = 0;
    while (!todo.empty())
    {
        const uint32_t a = todo.back();
        todo.pop_back();
        if (!seen.insert(a).second) continue;
        if (++steps > 2000 || a == r.entry) return WordValue();
        std::vector<uint32_t> preds;
        sfe::DisassembledInstruction prev, prev2;
        if (decode(a - 2, prev))
        {
            const bool afterJump = decode(a - 4, prev2) && prev2.HasDelaySlot && !prev2.IsConditional && !prev2.IsCall;
            if (!afterJump) preds.push_back(a - 2);
        }
        const auto in = into.find(a);
        if (in != into.end()) preds.insert(preds.end(), in->second.begin(), in->second.end());
        if (preds.empty()) return WordValue();
        for (uint32_t p : preds)
        {
            sfe::DisassembledInstruction d;
            if (!decode(p, d) || d.IsCall || d.Mnemonic == "rte" || d.Mnemonic == "trapa") return WordValue();
            if (EdgeImpossible(r, word, into, p, a, depth)) continue;
            uint16_t w = 0;
            word(p, w);
            if (WrittenRegisters(d).count(base)) return WordValue();
            int ssize = 0, sbase = -1;
            int64_t soff = 0;
            const int reg = StoredRegister(w, ssize);
            if (reg < 0 && (w & 0xF0FF) != 0x401B && (w >> 8) != 0xC0 && (w >> 8) != 0xC1 && (w >> 8) != 0xC2 && !(d.Mnemonic.compare(0, 4, "mac.") == 0) &&
                !(d.Mnemonic.compare(0, 4, "movca") == 0))
            {
                todo.push_back(p);
                continue;
            }
            if (reg < 0 || !BasedAccess(w, true, sbase, soff, ssize) || sbase != base) return WordValue();
            if (soff + ssize <= offset || offset + size <= soff)
            {
                todo.push_back(p);
                continue;
            }
            if (soff != offset || ssize != size || !FoldedAccess(r, p, true)) return WordValue();
            const WordValue v = RegisterBefore(r, word, into, p, reg, depth + 1);
            if (v.kind != WordValue::Number) return WordValue();
            const uint64_t bytes = size >= 4 ? 0xFFFFFFFFull : (1ull << (8 * size)) - 1;
            const WordValue masked{ WordValue::Number, static_cast<int64_t>(static_cast<uint64_t>(v.value) & bytes) };
            if (any && !(masked == result)) return WordValue();
            result = masked;
            any = true;
        }
    }
    return any ? result : WordValue();
}
inline WordValue RegisterBefore(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                                const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t at, int reg, int depth, uint32_t ignoreWriter)
{
    const auto decode = [&](uint32_t p, sfe::DisassembledInstruction& d) {
        uint16_t w = 0;
        if (!r.InBody(p) || !word(p, w)) return false;
        d = sfe::Sh2Decode(p, w);
        return d.IsValid;
    };
    for (const EdgeFact& f : EdgeFacts())
    {
        if (f.reg != reg || at > f.edge || f.edge - at > 32) continue;
        bool straight = true;
        for (uint32_t q = at; q < f.edge && straight; q += 2)
        {
            sfe::DisassembledInstruction qd;
            if (!decode(q, qd) || qd.IsBranch || qd.IsCall || qd.HasDelaySlot || WrittenRegisters(qd).count(reg) || into.find(q + 2) != into.end()) straight = false;
        }
        if (straight) return WordValue{ WordValue::Number, f.value, true };
    }
    WordValue result;
    bool any = false;
    std::set<std::pair<uint32_t, int>> seen;
    std::vector<std::pair<uint32_t, int>> todo = { { at, reg } };
    int steps = 0;
    const auto give = [&](const WordValue& v) {
        if (v.kind == WordValue::Unknown) return false;
        if (any && !(v == result))
        {
            // (A7-122: a number a branch's condition gives is what a parameter holds there too, as the consumers of `byCondition` take it:
            // `tst r3,r3 ; bf L` over a `tas.b`, r3 the copy of r5 -- 0 on the fall-through, r5 on the branch; grammar 2 seed 6605587)
            if (A7RuleOff(122)) return false;
            if (v.kind == WordValue::Number && v.byCondition && result.kind == WordValue::Param) return true;
            if (result.kind == WordValue::Number && result.byCondition && v.kind == WordValue::Param)
            {
                result = v;
                return true;
            }
            return false;
        }
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
            // A7-34 (the area 7 fixer): an edge never taken, by a T the words give (`cmp/eq r1,r2` of two numbers), or an instruction
            // never run after one: no path
            if (p + 2 == a && depth < 6 && NeverRun(r, word, into, p, depth + 1)) continue;
            // A7-69: the edge of a delayed `bt/s` or `bf/s` (reached from its slot) that T says is never taken, or the fall-through of one
            // that always branches; grammar 2 seed 2702788, grammar 1 seed 3214852
            {
                uint16_t bw = 0;
                sfe::DisassembledInstruction br;
                if (depth < 6 && r.InBody(p - 2) && word(p - 2, bw) && ((bw >> 8) == 0x8D || (bw >> 8) == 0x8F) && decode(p - 2, br) &&
                    br.BranchTarget != p + 2 && (p + 2 == a || br.BranchTarget == a) &&
                    // (A7-118: not the fall-through past a slot a branch of its own enters too)
                    !(!A7RuleOff(118) && p + 2 == a && into.find(p) != into.end() && !into.find(p)->second.empty()))
                {
                    const int t = TBefore(r, word, into, p - 2, depth + 1);
                    const bool taken = p + 2 != a;
                    if (t >= 0 && taken != (((bw >> 8) == 0x8D) == (t == 1))) continue;
                }
            }
            if (((w >> 8) == 0x89 || (w >> 8) == 0x8B) && depth < 6 && d.BranchTarget != p + 2)
            {
                const bool taken = p + 2 != a;
                const int t = TBefore(r, word, into, p, depth + 1);
                if (t >= 0 && taken != (((w >> 8) == 0x89) == (t == 1))) continue;
            }
            // A7-34 (the area 7 fixer): an edge a condition says the register's value on: `dt rN ; bf L` falls through, and `dt rN ; bt
            // L` (or `tst rN,rN`) branches, only when rN is 0; `cmp/eq #imm,r0` likewise with the number (as the engine's conditional
            // constants know)
            {
                // (A7-122: not a `bf` to the next instruction, which goes there on either T; grammar 2 seed 6605587)
                const bool fallsThrough = (w >> 8) == 0x8B && p + 2 == a && (A7RuleOff(122) || d.BranchTarget != p + 2), branches = (w >> 8) == 0x89 && p + 2 != a;
                uint16_t cw = 0;
                if ((fallsThrough || branches) && r.InBody(p - 2) && word(p - 2, cw))
                {
                    const int cn = (cw >> 8) & 0xF;
                    if (((cw & 0xF0FF) == 0x4010 && cn == rg) || ((cw & 0xF00F) == 0x2008 && cn == rg && ((cw >> 4) & 0xF) == rg))
                    {
                        if (!give(WordValue{ WordValue::Number, 0, true })) return WordValue();
                        continue;
                    }
                    if ((cw >> 8) == 0x88 && rg == 0)
                    {
                        if (!give(WordValue{ WordValue::Number, static_cast<int8_t>(cw & 0xFF), true })) return WordValue();
                        continue;
                    }
                    // (A7-91: `cmp/eq Rm,Rn ; bf L` falls through, and `bt L` branches, with Rm equal to Rn: a register the words give a number
                    // for is that number in the other too -- `mov #0,r2 ; cmp/eq r2,r0 ; bf L ; add r0,r2` adds 0 and 0; grammar 2 seed 4103690)
                    if ((cw & 0xF00F) == 0x3000 && into.find(p) == into.end() && depth < 6)
                    {
                        const int cm = (cw >> 4) & 0xF;
                        const int other = cn == rg ? cm : cm == rg ? cn : -1;
                        if (other >= 0 && other != rg)
                        {
                            const WordValue v = RegisterBefore(r, word, into, p - 2, other, depth + 1);
                            if (v.kind == WordValue::Number)
                            {
                                if (!give(WordValue{ WordValue::Number, v.value, true })) return WordValue();
                                continue;
                            }
                        }
                    }
                }
            }
            // (A7-64: `extu.w Rm,Rn ; cmp/pl Rn ; bf L` -- on the edge to L, with T 0, Rn is not above 0 and an extension is not below
            // it: 0; likewise the fall-through of a `bt`; grammar 2 seed 2608245)
            {
                const bool tClear = ((w >> 8) == 0x8B && p + 2 != a) || ((w >> 8) == 0x89 && p + 2 == a);
                uint16_t cw = 0;
                if (tClear && word(p - 2, cw) && cw == (0x4015u | (static_cast<unsigned>(rg) << 8)) && into.find(p) == into.end())
                {
                    // (the extension is up to five instructions before the compare, none of them a branch's target, a branch or a write of Rn)
                    bool extended = false;
                    for (uint32_t q = p - 4, k = 0; k < 5 && !extended && r.InBody(q); q -= 2, ++k)
                    {
                        uint16_t xw = 0;
                        sfe::DisassembledInstruction xd;
                        if (!decode(q, xd) || !word(q, xw)) break;
                        if ((xw & 0xF00F) == 0x600C || (xw & 0xF00F) == 0x600D)
                            if (((xw >> 8) & 0xF) == static_cast<unsigned>(rg)) extended = true;
                        if (extended) break;
                        if (xd.IsBranch || xd.IsCall || xd.HasDelaySlot || WrittenRegisters(xd).count(rg) || into.find(q + 2) != into.end()) break;
                    }
                    // (A7-125: or an extension further back, past a loop -- `extu.w r4,r1` inside a `dt` loop the compare follows: every
                    // instruction that made Rn before the compare, on every path, is an `extu.b` or `extu.w`; grammar 2 seed 6804434)
                    if (!extended && !A7RuleOff(125))
                    {
                        std::set<uint32_t> makers;
                        if (RegisterMakers(r, word, into, p - 2, rg, makers, false, nullptr) && !makers.empty())
                        {
                            bool all = true;
                            for (uint32_t m : makers)
                            {
                                uint16_t mw = 0;
                                all = all && m != 0 && word(m, mw) && ((mw & 0xF00F) == 0x600C || (mw & 0xF00F) == 0x600D);
                            }
                            extended = all;
                        }
                    }
                    if (extended)
                    {
                        if (!give(WordValue{ WordValue::Number, 0, true })) return WordValue();
                        continue;
                    }
                }
            }
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
            // A7-34 (the area 7 fixer): an instruction that computes the register from values the walk knows (`add #imm`, `xor r1,r1`,
            // `extu.w`, `shll2`, `and r1,r1`, a read of memory a store before it wrote), as the engine folds it
            if (WrittenRegisters(d).count(rg) || ((w >> 8) == 0xC7 && rg == 0))
            {
                // (A7-64: the writer a loop's extension asks the value before, for the entries into the loop: its own result is the
                // extension of what it extended, itself)
                if (p == ignoreWriter && rg == st.second && ignoreWriter != 0) continue;
                if (!give(depth < 6 ? WrittenValue(r, word, into, p, rg, depth + 1) : WordValue())) return WordValue();
                continue;
            }
            todo.emplace_back(p, rg);
        }
    }
    return any ? result : WordValue();
}
// A7-34 (the area 7 fixer): the value instruction `p` writes into register `reg`, from what the registers hold before it
// (RegisterBefore) and what memory holds (MemoryBefore): moves, numbers, literals, `mova`, the ALU ops with known inputs, the
// extensions, swaps and shifts, `dt`, and loads. `and Rn,Rn` and `or Rn,Rn` pass Rn on whatever it is (a parameter too); `xor Rn,Rn`
// and `sub Rn,Rn` are 0 whatever it is. Unknown for anything else.
// A7-34 (the area 7 fixer): the T bit before `p` (0 or 1), when every path into `p` gives the same one: the instruction that last set
// it, from values the words give (`cmp/eq`, `cmp/hs` and the other compares, `tst`, `dt`, `clrt`, `sett`, `div0u`); a `bt` or `bf`
// passes T on, and a path through one's edge that T says is never taken gives nothing. -1 when that is not known (another
// instruction that sets T, a call, the entry, paths that disagree, past a bound).
inline int TSetBy(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                  const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t q, uint16_t w, int depth)
{
    const int hi = w >> 12, n = (w >> 8) & 0xF, m = (w >> 4) & 0xF, lo = w & 0xF;
    const auto value = [&](int reg, uint32_t& x) {
        const WordValue v = RegisterBefore(r, word, into, q, reg, depth + 1);
        if (v.kind != WordValue::Number) return false;
        x = static_cast<uint32_t>(v.value);
        return true;
    };
    uint32_t a = 0, b = 0;
    if (w == 0x0008 || w == 0x0019) return 0;
    if (w == 0x0018) return 1;
    if (hi == 0x3 && (lo == 0x0 || lo == 0x2 || lo == 0x3 || lo == 0x6 || lo == 0x7))
    {
        if (lo == 0x0 && n == m) return 1;
        // (A7-123: a compare of a register with itself: equal, so `hs` and `ge` are 1 and `hi` and `gt` 0)
        if (n == m) return lo == 0x2 || lo == 0x3 ? 1 : 0;
        if (!value(n, a) || !value(m, b)) return -1;
        switch (lo)
        {
        case 0x0: return a == b;
        case 0x2: return a >= b;
        case 0x3: return static_cast<int32_t>(a) >= static_cast<int32_t>(b);
        case 0x6: return a > b;
        default: return static_cast<int32_t>(a) > static_cast<int32_t>(b);
        }
    }
    if ((w >> 8) == 0x88) return value(0, a) ? a == static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(w & 0xFF))) : -1;
    if ((w >> 8) == 0xC8) return value(0, a) ? (a & (w & 0xFFu)) == 0 : -1;
    if (hi == 0x2 && lo == 0x8)
    {
        if ((value(n, a) && a == 0) || (value(m, b) && b == 0)) return 1;
        if (!value(n, a) || !value(m, b)) return -1;
        return (a & b) == 0;
    }
    if ((w & 0xF0FF) == 0x4010) return value(n, a) ? a == 1 : -1;
    if ((w & 0xF0FF) == 0x4011) return value(n, a) ? static_cast<int32_t>(a) >= 0 : -1;
    if ((w & 0xF0FF) == 0x4015) return value(n, a) ? static_cast<int32_t>(a) > 0 : -1;
    return -2;   // (does not set T from what the words give: -1 if it sets T at all, as the caller asks)
}
// A7-34 (the area 7 fixer): is the instruction at `p` never run: the straight line it is on is entered only by the fall-through of a
// `bt` or `bf` that T says always branches (`cmp/eq r1,r2` of 0x18 and -0x5e ; `bf L`)?
inline bool NeverRun(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                     const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t p, int depth)
{
    if (depth > 6) return false;
    for (uint32_t x = p, steps = 0; steps < 32; x -= 2, ++steps)
    {
        const auto in = into.find(x);
        if ((in != into.end() && !in->second.empty()) || x == r.entry || !r.InBody(x - 2)) return false;
        uint16_t w = 0, sw = 0;
        if (!word(x - 2, w)) return false;
        // (A7-69: the code after a call and its slot is entered by the call's return: go on to what runs before the call; grammar 2 seed
        // 3218780, `tst #4,r0 ; bt L ; bsr f ; nop ; mov r8,r4` with r0 2)
        {
            uint16_t cw = 0;
            if (r.InBody(x - 4) && word(x - 4, cw))
            {
                const sfe::DisassembledInstruction cd = sfe::Sh2Decode(x - 4, cw);
                if (cd.IsValid && cd.IsCall && cd.HasDelaySlot)
                {
                    x -= 2;
                    continue;
                }
            }
        }
        // (the instruction after a `bt/s` or `bf/s` and its slot: the slot runs either way)
        if (r.InBody(x - 4) && word(x - 4, sw) && ((sw >> 8) == 0x8D || (sw >> 8) == 0x8F))
        {
            const sfe::DisassembledInstruction b = sfe::Sh2Decode(x - 4, sw);
            if (b.BranchTarget == x) return false;
            // (A7-91: the slot is also entered by a branch of its own -- `bf L` to the slot's address, which runs on: the code after it
            // is run by that way in; grammar 1 seed 4108075)
            const auto slotIn = into.find(x - 2);
            if (slotIn != into.end() && !slotIn->second.empty()) return false;
            const int t = TBefore(r, word, into, x - 4, depth + 1);
            return t >= 0 && (((sw >> 8) == 0x8D) == (t == 1));
        }
        const sfe::DisassembledInstruction d = sfe::Sh2Decode(x - 2, w);
        if (!d.IsValid) return false;
        if ((w >> 8) == 0x89 || (w >> 8) == 0x8B)
        {
            if (d.BranchTarget == x) return false;
            const int t = TBefore(r, word, into, x - 2, depth + 1);
            return t >= 0 && (((w >> 8) == 0x89) == (t == 1));   // (the branch is always taken)
        }
        if (d.IsBranch || d.HasDelaySlot) return false;
    }
    return false;
}
// A7-134: can the instruction at `at` be run, by the words and the T they give: some way into it (the instruction before it, or a
// branch's source) is the entry or is itself reachable, and the edge is not one T says is never taken (EdgeImpossible). `cmp/eq`,
// `tst` of numbers a store wrote -- a poll of a word the code itself set to a value the poll waits for, an endless loop -- make the
// code after it unreachable by these words, though the engine prints it.
inline bool OracleReachable(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                            const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t at)
{
    std::set<uint32_t> seen;
    std::vector<uint32_t> todo = { at };
    int steps = 0;
    while (!todo.empty())
    {
        const uint32_t a = todo.back();
        todo.pop_back();
        if (!seen.insert(a).second) continue;
        if (a == r.entry || ++steps > 2000) return true;
        std::vector<uint32_t> preds;
        uint16_t w = 0, w2 = 0;
        if (r.InBody(a - 2) && word(a - 2, w))
        {
            const sfe::DisassembledInstruction prev = sfe::Sh2Decode(a - 2, w);
            const sfe::DisassembledInstruction prev2 = r.InBody(a - 4) && word(a - 4, w2) ? sfe::Sh2Decode(a - 4, w2) : sfe::DisassembledInstruction();
            if (prev.IsValid && !(prev2.IsValid && prev2.HasDelaySlot && !prev2.IsConditional && !prev2.IsCall)) preds.push_back(a - 2);
        }
        const auto in = into.find(a);
        if (in != into.end()) preds.insert(preds.end(), in->second.begin(), in->second.end());
        for (uint32_t p : preds)
            if (!EdgeImpossible(r, word, into, p, a, 0)) todo.push_back(p);
    }
    return false;
}
inline bool SetsT(uint16_t w)
{
    const int hi = w >> 12, lo = w & 0xF;
    return (hi == 0x4 && ((w & 0xFF) == 0x00 || (w & 0xFF) == 0x01 || (w & 0xFF) == 0x04 || (w & 0xFF) == 0x05 || (w & 0xFF) == 0x20 ||
                          (w & 0xFF) == 0x21 || (w & 0xFF) == 0x24 || (w & 0xFF) == 0x25 || (w & 0xFF) == 0x1B || (w & 0xFF) == 0x0E || (w & 0xFF) == 0x07)) ||
           (hi == 0x3 && (lo == 0x4 || lo == 0xA || lo == 0xB || lo == 0xE || lo == 0xF)) || (hi == 0x2 && (lo == 0x7 || lo == 0xC)) || (hi == 0x6 && lo == 0xA) ||
           w == 0x002B;
}
inline int TBefore(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                   const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t at, int depth)
{
    if (depth > 6) return -1;
    if (EdgeT().t >= 0 && at == EdgeT().at) return EdgeT().t;
    const auto decode = [&](uint32_t p, sfe::DisassembledInstruction& d) {
        uint16_t w = 0;
        if (!r.InBody(p) || !word(p, w)) return false;
        d = sfe::Sh2Decode(p, w);
        return d.IsValid;
    };
    int result = -1;
    bool any = false;
    std::set<uint32_t> seen;
    std::vector<uint32_t> todo = { at };
    int steps = 0;
    const auto give = [&](int t) {
        if (t < 0 || (any && t != result)) return false;
        result = t;
        any = true;
        return true;
    };
    while (!todo.empty())
    {
        const uint32_t a = todo.back();
        todo.pop_back();
        if (!seen.insert(a).second) continue;
        if (++steps > 200 || a == r.entry) return -1;
        std::vector<uint32_t> preds;
        sfe::DisassembledInstruction prev, prev2;
        if (decode(a - 2, prev))
        {
            const bool afterJump = decode(a - 4, prev2) && prev2.HasDelaySlot && !prev2.IsConditional && !prev2.IsCall;
            if (!afterJump) preds.push_back(a - 2);
        }
        const auto in = into.find(a);
        if (in != into.end()) preds.insert(preds.end(), in->second.begin(), in->second.end());
        if (preds.empty()) return -1;
        for (uint32_t p : preds)
        {
            sfe::DisassembledInstruction d;
            uint16_t w = 0;
            // (A7-115: an edge that is never taken, the fall-through of a `bf` that T says always branches, gives T nothing: `cmp/pl r3 ;
            // bf L ; cmp/hi r2,r1 ; L: rotcl r3` with r3 negative is run with T 0 only; grammar 1 seed 6201923)
            if (!A7RuleOff(115) && depth < 6 && EdgeImpossible(r, word, into, p, a, depth + 1)) continue;
            if (!decode(p, d) || !word(p, w) || d.IsCall || d.Mnemonic == "rte") return -1;
            if ((w >> 8) == 0x89 || (w >> 8) == 0x8B)
            {
                // (a `bt` or `bf` passes T on, and its edge says it: `bt` is taken with T 1 and falls through with T 0, `bf` the other
                // way; an edge the T before the branch never takes gives nothing)
                const int t = TBefore(r, word, into, p, depth + 1);
                if (d.BranchTarget == p + 2)
                {
                    if (!give(t)) return -1;
                    continue;
                }
                const bool taken = p + 2 != a;
                const int edge = taken == ((w >> 8) == 0x89) ? 1 : 0;
                if (t >= 0 && t != edge) continue;
                if (!give(edge)) return -1;
                continue;
            }
            // (the slot of a `bt/s` or `bf/s` that does not set T: the edge from the slot says T as the branch's own edge does, A7-34's
            // triage of grammar 2 seed 1804151: `tst r1,r1 ; bt/s L ; mov.l r1,@(0x18,r6) ; sett ; L: movt r2` puts 1 in r2 either way)
            uint16_t bw = 0;
            sfe::DisassembledInstruction b;
            // (A7-123: a slot that sets T from what the words give -- `cmp/hi r1,r1`, a compare, a `tst` -- is not one that leaves it; SetsT
            // lists only the instructions TSetBy does not evaluate; grammar 1 seed 6609800)
            if (!SetsT(w) && (A7RuleOff(123) || TSetBy(r, word, into, p, w, depth + 1) == -2) && decode(p - 2, b) && word(p - 2, bw) &&
                ((bw >> 8) == 0x8D || (bw >> 8) == 0x8F) && b.BranchTarget != p + 2)
            {
                const int t = TBefore(r, word, into, p - 2, depth + 1);
                const bool taken = p + 2 != a;
                const int edge = taken == ((bw >> 8) == 0x8D) ? 1 : 0;
                if (t >= 0 && t != edge) continue;
                if (!give(edge)) return -1;
                continue;
            }
            const int t = TSetBy(r, word, into, p, w, depth);
            if (t == -1 || (t == -2 && SetsT(w))) return -1;
            if (t >= 0)
            {
                if (!give(t)) return -1;
                continue;
            }
            todo.push_back(p);
        }
    }
    return any ? result : -1;
}
inline bool RegisterMakers(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                           const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t at, int reg, std::set<uint32_t>& makers,
                           bool throughUpdate = false, std::set<int>* entryRegs = nullptr);
inline WordValue WrittenValue(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                              const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t p, int reg, int depth)
{
    uint16_t w = 0;
    if (depth > 6 || !word(p, w)) return WordValue();
    const int hi = w >> 12, n = (w >> 8) & 0xF, m = (w >> 4) & 0xF, lo = w & 0xF;
    const auto before = [&](int x) { return RegisterBefore(r, word, into, p, x, depth); };
    const auto number = [](const WordValue& v, uint32_t& x) {
        if (v.kind != WordValue::Number) return false;
        x = static_cast<uint32_t>(v.value);
        return true;
    };
    sfe::DisassembledInstruction prev;
    uint16_t pw = 0;
    const bool inSlot = word(p - 2, pw) && r.InBody(p - 2) && (prev = sfe::Sh2Decode(p - 2, pw)).IsValid && prev.HasDelaySlot;
    uint32_t a = 0, b = 0;
    if (hi == 0xE && n == reg) return NumberValue(static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(w & 0xFF))));
    if (hi == 0x6 && lo == 0x3 && n == reg) return before(m);
    if ((w >> 8) == 0xC7 && reg == 0)
        return inSlot ? WordValue() : NumberValue((p & ~3u) + 4 + (w & 0xFFu) * 4);
    if ((hi == 0x9 || hi == 0xD) && n == reg && !inSlot)
    {
        uint16_t h = 0, l = 0;
        const uint32_t q = hi == 0x9 ? p + 4 + (w & 0xFFu) * 2 : (p & ~3u) + 4 + (w & 0xFFu) * 4;
        if (hi == 0x9 ? !word(q, l) : !(word(q, h) && word(q + 2, l))) return WordValue();
        return NumberValue(hi == 0x9 ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(l))) : (static_cast<uint32_t>(h) << 16 | l));
    }
    if (hi == 0x7 && n == reg) return number(before(n), a) ? NumberValue(a + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(w & 0xFF)))) : WordValue();
    // (A7-69: `subv`/`addv` give the value `sub`/`add` do -- `subv r1,r1` is 0 -- and `addc`/`subc` add or take the carry, T before;
    // grammar 2 seeds 3106681, 3210493, 3007721)
    if (hi == 0x3 && n == reg && (lo == 0xC || lo == 0x8 || lo == 0xB || lo == 0xF || lo == 0xE || lo == 0xA))
    {
        const bool subtract = lo == 0x8 || lo == 0xB || lo == 0xA, carry = lo == 0xE || lo == 0xA;
        if (!carry && subtract && m == n) return NumberValue(0);
        // (A7-64: `sub r4,r0` with r0 a copy of the input r4 is 0; grammar 2 seed 2602445)
        if (!carry && subtract)
        {
            const WordValue pn = before(n), pm = before(m);
            if (pn.kind == WordValue::Param && pm.kind == WordValue::Param && pn.value == pm.value) return NumberValue(0);
        }
        // (A7-78: `add r4,r3` with r3 0 is a copy of the input in r4; grammar 2 seed 3629843)
        if (!carry && !subtract)
        {
            const WordValue pn = before(n), pm = before(m);
            if (pn.kind == WordValue::Param && number(pm, b) && b == 0) return pn;
            if (pm.kind == WordValue::Param && number(pn, a) && a == 0) return pm;
        }
        if (!number(before(n), a) || !number(before(m), b)) return WordValue();
        uint32_t t = 0;
        if (carry)
        {
            const int tb = TBefore(r, word, into, p, depth);
            if (tb < 0) return WordValue();
            t = static_cast<uint32_t>(tb);
        }
        return NumberValue(subtract ? a - b - t : a + b + t);
    }
    if (hi == 0x2 && n == reg && (lo == 0x9 || lo == 0xA || lo == 0xB))
    {
        if (m == n) return lo == 0xA ? NumberValue(0) : before(n);
        if (lo == 0xA)   // (A7-64: `xor` of two copies of one input)
        {
            const WordValue pn = before(n), pm = before(m);
            if (pn.kind == WordValue::Param && pm.kind == WordValue::Param && pn.value == pm.value) return NumberValue(0);
            // (A7-121: `xor Rm,Rn` of a register and its complement -- every maker of Rn a `not Rm,Rn` and Rm made by the same
            // instructions at both -- is all ones; grammar 2 seed 6604995)
            const auto complementOf = [&](int holder, int other) {
                std::set<uint32_t> makers, there;
                if (!RegisterMakers(r, word, into, p, holder, makers) || makers.empty() || makers.count(0) || !RegisterMakers(r, word, into, p, other, there)) return false;
                for (uint32_t mk : makers)
                {
                    uint16_t mw = 0;
                    std::set<uint32_t> atMaker;
                    if (!word(mk, mw) || (mw & 0xF00F) != 0x6007 || static_cast<int>((mw >> 8) & 0xF) != holder || static_cast<int>((mw >> 4) & 0xF) != other ||
                        !RegisterMakers(r, word, into, mk, other, atMaker) || atMaker != there)
                        return false;
                }
                return true;
            };
            if (!A7RuleOff(121) && (complementOf(n, m) || complementOf(m, n))) return NumberValue(0xFFFFFFFFu);
        }
        // (an AND with a known 0 is 0 whatever the other is)
        if (lo == 0x9 && ((number(before(n), a) && a == 0) || (number(before(m), b) && b == 0))) return NumberValue(0);
        if (!number(before(n), a) || !number(before(m), b)) return WordValue();
        return NumberValue(lo == 0x9 ? a & b : lo == 0xA ? a ^ b : a | b);
    }
    if ((w & 0xF0FF) == 0x0029 && n == reg)   // movt
    {
        const int t = TBefore(r, word, into, p, depth);
        return t < 0 ? WordValue() : NumberValue(static_cast<uint32_t>(t));
    }
    // (A7-68: `negc Rm,Rn` is 0 minus Rm minus T; a `bt`'s target after `tst` has T set; grammar 2 seed 2807206)
    if (hi == 0x6 && lo == 0xA && n == reg)
    {
        const int t = TBefore(r, word, into, p, depth);
        return t >= 0 && number(before(m), a) ? NumberValue(0u - a - static_cast<uint32_t>(t)) : WordValue();
    }
    if (reg == 0 && (w >> 8) == 0xC9 && (w & 0xFF) == 0) return NumberValue(0);
    if (reg == 0 && ((w >> 8) == 0xC9 || (w >> 8) == 0xCA || (w >> 8) == 0xCB))
    {
        if (!number(before(0), a)) return WordValue();
        b = w & 0xFFu;
        return NumberValue((w >> 8) == 0xC9 ? a & b : (w >> 8) == 0xCA ? a ^ b : a | b);
    }
    if (hi == 0x6 && n == reg && (lo == 0x7 || lo == 0x8 || lo == 0x9 || lo >= 0xB))
    {
        // (an extension of bits a shift just before it cleared is 0 whatever the register held: `shll16 r1 ; extu.b r1,r1`, grammar 2
        // seed 1872967)
        {
            uint16_t sw = 0;
            if ((lo == 0xC || lo == 0xD) && into.find(p) == into.end() && !inSlot && r.InBody(p - 2) && word(p - 2, sw) && (sw >> 12) == 0x4 &&
                ((sw >> 8) & 0xF) == m && ((sw & 0xFF) == 0x28 || (lo == 0xC && (sw & 0xFF) == 0x18)))
                return NumberValue(0);
        }
        bool lowKnown = false;
        if (!number(before(m), a))
        {
            // (A7-127: an extension needs only the low bytes: when the paths into it give other numbers that agree there -- `extu.w r3,r1`
            // of the literal 0xA0000000 on one path and of `rotcr r3`'s 0xD0000000 on the other, both 0 -- the number each instruction
            // that made Rm writes, extended, is the same; grammar 1 seed 7001742)
            if (lo >= 0xC && !A7RuleOff(127) && depth < 6)
            {
                std::set<uint32_t> makers;
                bool agree = RegisterMakers(r, word, into, p, m, makers, false, nullptr) && !makers.empty() && !makers.count(0) && !makers.count(p);
                bool first = true;
                uint32_t low = 0;
                for (uint32_t mk : makers)
                {
                    if (!agree) break;
                    uint16_t mw = 0;
                    std::set<int> wrote;
                    if (word(mk, mw)) wrote = WrittenRegisters(sfe::Sh2Decode(mk, mw));
                    uint32_t v = 0;
                    agree = wrote.size() == 1 && number(WrittenValue(r, word, into, mk, *wrote.begin(), depth + 1), v);
                    v = lo == 0xC || lo == 0xE ? v & 0xFFu : v & 0xFFFFu;
                    agree = agree && (first || v == low);
                    low = v;
                    first = false;
                }
                if (agree) a = low;
                lowKnown = agree;
            }
            // (A7-64: an extension that reads its own result round a loop, `extu.w r1,r1` at the head: what the entries into the loop
            // give, extended, and the extension of that is itself; grammar 1 seed 2607749, 0x60000000 and then 0)
            if (!lowKnown && (m != n || lo < 0xC || !number(RegisterBefore(r, word, into, p, m, depth, p), a))) return WordValue();
        }
        switch (lo)
        {
        case 0x7: return NumberValue(static_cast<uint32_t>(~a));   // not.l: a 32-bit not, widened only after (MSVC C4319)
        case 0x8: return NumberValue((a & 0xFFFF0000u) | (a & 0xFF) << 8 | (a >> 8 & 0xFF));
        case 0x9: return NumberValue(a << 16 | a >> 16);
        case 0xB: return NumberValue(0u - a);
        case 0xC: return NumberValue(a & 0xFF);
        case 0xD: return NumberValue(a & 0xFFFF);
        case 0xE: return NumberValue(static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(a & 0xFF))));
        default: return NumberValue(static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(a & 0xFFFF))));
        }
    }
    // (`mac.w @Rm+,@Rn+` is 0100nnnnmmmm1111: Rn steps by 2 below, it is no shift; grammar 1 seed 2601280, A7-64)
    if (hi == 0x4 && n == reg && lo != 0xF)
    {
        const int op = w & 0xFF;
        if (!number(before(n), a)) return WordValue();
        switch (op)
        {
        case 0x00: case 0x20: return NumberValue(a << 1);
        case 0x01: return NumberValue(a >> 1);
        case 0x21: return NumberValue(static_cast<uint32_t>(static_cast<int32_t>(a) >> 1));
        case 0x08: return NumberValue(a << 2);
        case 0x09: return NumberValue(a >> 2);
        case 0x18: return NumberValue(a << 8);
        case 0x19: return NumberValue(a >> 8);
        case 0x28: return NumberValue(a << 16);
        case 0x29: return NumberValue(a >> 16);
        case 0x10: return NumberValue(a - 1);
        // (A7-42: `rotl`, `rotr`, whose T the value does not need; grammar 2 seed 1853425, `xVar1 = 0xda;` at `rotl r1` of 0x6d)
        case 0x04: return NumberValue(a << 1 | a >> 31);
        case 0x05: return NumberValue(a >> 1 | a << 31);
        // (`rotcl`, `rotcr`: through T, when T is known before it; grammar 2 seed 1873745, `uVar6 = 1;` at `rotcl r3`)
        case 0x24: case 0x25:
        {
            const int t = TBefore(r, word, into, p, depth);
            if (t < 0) return WordValue();
            return NumberValue(op == 0x24 ? a << 1 | static_cast<uint32_t>(t) : a >> 1 | static_cast<uint32_t>(t) << 31);
        }
        default: return WordValue();
        }
    }
    // a register an access steps (`mac.w @r7+,@r5+`, `mov.l r1,@-r2`), not the one a load writes: by the access's size each time
    {
        const sfe::DisassembledInstruction sd = sfe::Sh2Decode(p, w);
        const std::string rn = "r" + std::to_string(reg);
        const std::string& ops = sd.Operands;
        int steps = 0;
        for (size_t q = ops.find("@" + rn + "+"); q != std::string::npos; q = ops.find("@" + rn + "+", q + 1))
            if (q + 2 + rn.size() > ops.size() || ops[q + 1 + rn.size()] == '+') ++steps;
        for (size_t q = ops.find("@-" + rn); q != std::string::npos; q = ops.find("@-" + rn, q + 1))
        {
            const size_t e = q + 2 + rn.size();
            if (e >= ops.size() || !std::isdigit(static_cast<unsigned char>(ops[e]))) --steps;
        }
        const bool loadsIt = hi == 0x6 && lo <= 6 && n == reg;   // (`mov.l @r4+,r4`: the load's value)
        if (steps != 0 && !loadsIt)
        {
            const std::string& mn = sd.Mnemonic;
            const int size = mn.size() > 2 && mn[mn.size() - 2] == '.' ? (mn.back() == 'b' ? 1 : mn.back() == 'w' ? 2 : 4) : 4;
            if (!number(before(reg), a)) return WordValue();
            return NumberValue(a + static_cast<uint32_t>(steps * size));
        }
    }
    // a pop of what a push before it pushed (`mov.l r3,@-r15 ; mov.l @r15+,r3`, none of them a branch's target): the pushed register.
    // A7-40: up to six instructions between them, when none names the stack register, writes memory, branches or calls, and none is a
    // branch's target (`mov.l r3,@-r15 ; extu.w r4,r3 ; mov.l @r15+,r3`)
    {
        uint16_t pushw = 0;
        const bool pop = hi == 0x6 && lo >= 4 && lo <= 6 && n == reg && n != m;
        uint32_t push = p - 2;
        const auto between = [&](uint32_t q) {
            uint16_t iw = 0;
            if (!r.InBody(q) || !word(q, iw) || into.find(q + 2) != into.end()) return false;
            const sfe::DisassembledInstruction di = sfe::Sh2Decode(q, iw);
            if (!di.IsValid || di.IsBranch || di.IsCall || di.HasDelaySlot || di.HasBranchTarget) return false;
            const int ih = iw >> 12, in = (iw >> 8) & 0xF, im = (iw >> 4) & 0xF, il = iw & 0xF;
            if (in == m || im == m) return false;   // (any field that may name the stack register)
            const bool store = (ih == 0x2 && il <= 6) || ih == 0x1 || (ih == 0x0 && il >= 4 && il <= 6) || (iw >> 8) == 0x80 || (iw >> 8) == 0x81 ||
                               ((iw >> 8) >= 0xC0 && (iw >> 8) <= 0xC2) || (iw >> 8) >= 0xCC || (ih == 0x4 && (il == 0x2 || il == 0x3 || il == 0xB));
            // (A7-128: a store through a register that holds a parameter or a number -- `mov.l r1,@r6` of param_3 between `mov.l r2,@-r15`
            // and `mov.l @r15+,r2` -- does not write the word just pushed: no caller's pointer reaches below its own stack, as the engine
            // takes the frame; grammar 2 seed 7006020)
            if (store && !A7RuleOff(128) && depth < 6 && ((ih == 0x2 && (il <= 2 || (il >= 4 && il <= 6))) || ih == 0x1 || (iw >> 8) == 0x80 || (iw >> 8) == 0x81))
            {
                const int base = (iw >> 8) == 0x80 || (iw >> 8) == 0x81 ? im : in;
                std::set<uint32_t> makers;
                std::set<int> entryRegs;
                if (base == m || !RegisterMakers(r, word, into, q, base, makers, false, &entryRegs) || makers.empty() || entryRegs.count(m)) return false;
                for (uint32_t mk : makers)
                {
                    uint16_t mw = 0;
                    if (mk == 0) continue;
                    if (!word(mk, mw) || !((mw >> 12) == 0xD || (mw >> 12) == 0x9 || (mw >> 12) == 0xE || (mw >> 8) == 0xC7)) return false;
                }
                return true;
            }
            // (A7-138: nor a store through gbr, `mov.b r0,@(0xa,gbr)` between `mov.l r1,@-r15` and `mov.l @r15+,r1`: the engine keeps gbr's
            // memory apart from the frame; grammar 2 seed 7606857)
            if (store && !A7RuleOff(138) && (iw >> 8) >= 0xC0 && (iw >> 8) <= 0xC2) return true;
            return !store;
        };
        const auto isPush = [&](uint32_t q) {
            return r.InBody(q) && word(q, pushw) && (pushw >> 12) == 0x2 && (pushw & 0xF) == lo && ((pushw >> 8) & 0xF) == m;
        };
        if (pop && !isPush(push))
            for (int k = 0; k < 6 && between(push) && into.find(push) == into.end(); ++k)
                if (isPush(push - 2))
                {
                    push -= 2;
                    break;
                }
                else
                    push -= 2;
        if (pop && into.find(p) == into.end() && isPush(push) && !inSlot)
        {
            const WordValue v = RegisterBefore(r, word, into, push, (pushw >> 4) & 0xF, depth);
            if (!number(v, a)) return v.kind == WordValue::Param && lo == 6 ? v : WordValue();
            if (lo == 4) a = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(a & 0xFF)));
            if (lo == 5) a = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(a & 0xFFFF)));
            return NumberValue(a);
        }
    }
    // a load: what memory holds at its address, extended as the load extends (a byte or a word sign-extended)
    uint32_t addr = 0;
    int size = 0;
    const bool loadInto = (hi == 0x6 && lo <= 2 && n == reg) || (hi == 0x6 && lo >= 4 && lo <= 6 && n == reg && n != m) || (hi == 0x5 && n == reg) ||
                          (((w >> 8) == 0x84 || (w >> 8) == 0x85) && reg == 0) || (hi == 0x0 && lo >= 0xC && n == reg);
    int sbase = -1, ssize2 = 0;
    int64_t soff = 0;
    if (loadInto && FoldedAccess(r, p, false) && !AccessAddress(r, word, into, p, w, false, addr, size, depth) && BasedAccess(w, false, sbase, soff, ssize2))
    {
        const WordValue v = SlotBefore(r, word, into, p, sbase, soff, ssize2, depth + 1);
        if (!number(v, a)) return WordValue();
        if (ssize2 == 1) a = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(a & 0xFF)));
        if (ssize2 == 2) a = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(a & 0xFFFF)));
        return NumberValue(a);
    }
    if (loadInto && FoldedAccess(r, p, false) && AccessAddress(r, word, into, p, w, false, addr, size, depth))
    {
        const WordValue v = MemoryBefore(r, word, into, p, addr, size, depth + 1);
        if (!number(v, a)) return WordValue();
        if (size == 1) a = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(a & 0xFF)));
        if (size == 2) a = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(a & 0xFFFF)));
        return NumberValue(a);
    }
    return WordValue();
}

// Area 7 round 6: the instructions that made the value a register holds before `at`, on each path (the walk RegisterBefore takes): an
// instruction that writes it, or, through a register move, what made the moved register; a call for r0 to r7; 0 for the function's input.
// False when the walk is not finished (an undecodable word, past the bound).
// (A7-69: with `throughUpdate`, an instruction that updates the register in place -- `sub r5,r0`, `add #4,r1`, a shift -- is a maker and
// so are the makers of the value it updates: the number a store's value derives from, `iRam80002014 = 1;` at the `mov #1,r0` of a loop
// that stores `r0 - r5`)
inline bool RegisterMakers(const sfe::decomp::DecompileResult& r, const std::function<bool(uint32_t, uint16_t&)>& word,
                           const std::map<uint32_t, std::vector<uint32_t>>& into, uint32_t at, int reg, std::set<uint32_t>& makers,
                           bool throughUpdate, std::set<int>* entryRegs)
{
    const auto decode = [&](uint32_t p, sfe::DisassembledInstruction& d) {
        uint16_t w = 0;
        if (!r.InBody(p) || !word(p, w)) return false;
        d = sfe::Sh2Decode(p, w);
        return d.IsValid;
    };
    std::set<std::pair<uint32_t, int>> seen;
    std::vector<std::pair<uint32_t, int>> todo = { { at, reg } };
    int steps = 0;
    while (!todo.empty())
    {
        const std::pair<uint32_t, int> st = todo.back();
        todo.pop_back();
        if (!seen.insert(st).second) continue;
        if (++steps > 4000) return false;
        const uint32_t a = st.first;
        const int rg = st.second;
        std::vector<uint32_t> preds;
        sfe::DisassembledInstruction prev, prev2;
        if (decode(a - 2, prev))
        {
            const bool afterJump = decode(a - 4, prev2) && prev2.HasDelaySlot && !prev2.IsConditional && !prev2.IsCall;
            if (!afterJump) preds.push_back(a - 2);
        }
        const auto in = into.find(a);
        if (in != into.end()) preds.insert(preds.end(), in->second.begin(), in->second.end());
        if (a == r.entry)
        {
            makers.insert(0);
            if (entryRegs) entryRegs->insert(rg);
            continue;
        }
        if (preds.empty()) return false;
        for (uint32_t p : preds)
        {
            sfe::DisassembledInstruction d;
            if (!decode(p, d)) return false;
            sfe::DisassembledInstruction call;
            if (p + 2 == a && decode(p - 2, call) && call.IsCall && call.HasDelaySlot && rg <= 7)
            {
                makers.insert(p - 2);
                continue;
            }
            if (d.IsCall && !d.HasDelaySlot && rg <= 7)
            {
                makers.insert(p);
                continue;
            }
            uint16_t w = 0;
            word(p, w);
            if ((w >> 12) == 0x6 && (w & 0xF) == 0x3 && ((w >> 8) & 0xF) == rg)
            {
                todo.emplace_back(p, (w >> 4) & 0xF);
                continue;
            }
            // (A7-78: MACH and MACL are followed as registers 16 and 17: `sts mach,Rn` takes the value `lds Rm,mach` put there, so the
            // number a register is stored from after the pair is made where Rm was; grammar 2 seed 3706722)
            if (rg < 16 && ((w & 0xF0FF) == 0x000A || (w & 0xF0FF) == 0x001A) && ((w >> 8) & 0xF) == rg)
            {
                todo.emplace_back(p, (w & 0xF0FF) == 0x000A ? 16 : 17);
                continue;
            }
            if (rg >= 16)
            {
                if ((w & 0xF0FF) == (rg == 16 ? 0x400Au : 0x401Au))
                {
                    todo.emplace_back(p, (w >> 8) & 0xF);
                    continue;
                }
                const std::string& mn = d.Mnemonic;
                if (mn == "clrmac" || mn == "dmuls.l" || mn == "dmulu.l" || mn == "mac.l" || mn == "mac.w" || (rg == 17 && (mn == "mul.l" || mn == "muls.w" || mn == "mulu.w")))
                {
                    makers.insert(p);
                    continue;
                }
                todo.emplace_back(p, rg);
                continue;
            }
            if (WrittenRegisters(d).count(rg))
            {
                makers.insert(p);
                const int uh = w >> 12, un = (w >> 8) & 0xF, ul = w & 0xF, uo = w & 0xFF;
                if (throughUpdate && un == rg &&
                    ((uh == 0x3 && (ul == 0x8 || ul == 0xA || ul == 0xB || ul == 0xC || ul == 0xE || ul == 0xF)) || uh == 0x7 || (uh == 0x2 && (ul == 0x9 || ul == 0xA || ul == 0xB)) ||
                     (uh == 0x4 && (uo == 0x00 || uo == 0x01 || uo == 0x04 || uo == 0x05 || uo == 0x08 || uo == 0x09 || uo == 0x10 || uo == 0x18 || uo == 0x19 || uo == 0x20 ||
                                    uo == 0x21 || uo == 0x24 || uo == 0x25 || uo == 0x28 || uo == 0x29))))
                    todo.emplace_back(p, rg);
                continue;
            }
            todo.emplace_back(p, rg);
        }
    }
    return true;
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
            // A7-52: a statement with no instruction says so (a click goes nowhere; the hover text, and the panel's note, say why); a
            // declaration, a brace or a label has neither. A line the product marks must be a statement, and have the words.
            if (l.noOrigin)
            {
                ++a.noOriginLines;
                if (tip.find("No SH-2 instruction is named for this line") != 0)
                {
                    ++a.noInstruction;
                    problem("no-instruction", "line " + std::to_string(li) + " '" + l.text + "' says no instruction is named for it, and its hover text does not: " + tip);
                }
            }
            else
            {
                if (!tip.empty())
                {
                    ++a.misnamed;
                    problem("hover", "line " + std::to_string(li) + " has no instruction and a hover text");
                }
                if (MappingIsStatement(l.text))
                {
                    ++a.noInstruction;
                    problem("no-instruction", "line " + std::to_string(li) + " '" + l.text + "' is a statement that names no instruction and does not say so (a click does nothing)");
                }
            }
            continue;
        }
        if (l.noOrigin)
        {
            ++a.noInstruction;
            problem("no-instruction", "line " + std::to_string(li) + " '" + l.text + "' says no instruction is named for it and names one");
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
        // A7-25: the lines that draw an instruction as their own statement, and those that draw it only as a copy materialised there;
        // the way back answers an own one first, and a line beginning with the instruction is one only when it is not such a copy
        // A7-30 (a): read from the text and the words, not from the product's flag. A line is a copy materialised at a call or a store
        // when it is a plain assignment (`V = W;`, a variable or a number on the right: `iRam0603ad2c = param_1;`, `piStack_c =
        // param_2;`), names a store of its variable at another instruction (an empty token at a store) and not one at this
        // instruction, and draws this instruction: the value the engine wrote there again. Every other line that draws the instruction
        // is the instruction's own statement (`uVar7 = extraout_sr_01;` after a call names no store: the call's output).
        const auto isStore = [&](uint32_t x) {
            uint16_t w = 0;
            if (!word(x, w)) return false;
            const sfe::DisassembledInstruction d = sfe::Sh2Decode(x, w);
            return d.IsValid && ShapeOf(d).store;
        };
        // (A7-34's triage: at any instruction but a branch -- a load, the first instruction of a loop the engine put a register's copy
        // under a global's name at, `iRam06001038 = iVar2;` at the loop's `mov.w @r4,r2`; an `and` -- naming a store elsewhere)
        const auto isBranch = [&](uint32_t x) {
            uint16_t w = 0;
            if (!word(x, w)) return true;
            const sfe::DisassembledInstruction d = sfe::Sh2Decode(x, w);
            return !d.IsValid || (d.IsBranch && !d.IsCall);
        };
        const auto plainCopy = [](const std::string& text) {
            const size_t b = text.find_first_not_of(' ');
            const size_t eq = text.find(" = ");
            if (b == std::string::npos || eq == std::string::npos || text.empty() || text.back() != ';') return false;
            const auto word = [](const std::string& x) {
                return !x.empty() && x.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-") == std::string::npos;
            };
            // (A7-34's triage: a conversion too, `xRam26002108 = (xunknown2)xVar1;` at the load that made xVar1, naming the store)
            size_t v = eq + 3;
            if (v < text.size() && text[v] == '(')
            {
                const size_t close = text.find(')', v);
                if (close == std::string::npos || !word(text.substr(v + 1, close - v - 1))) return false;
                v = close + 1;
            }
            // (A7-42: a piece the engine prints as `SUB42(in_r0,0)` is a conversion too: grammar 1 seed 1843207, `xRam26001042 =
            // SUB42(in_r0,0);` materialised at the `add` that made in_r0, naming the store)
            if (text.compare(v, 3, "SUB") == 0 && text.size() > v + 6 && std::isdigit(static_cast<unsigned char>(text[v + 3])) &&
                std::isdigit(static_cast<unsigned char>(text[v + 4])) && text[v + 5] == '(')
            {
                const size_t comma = text.find(',', v + 6), close = text.find(')', v + 6);
                if (comma == std::string::npos || close == std::string::npos || comma > close || close + 2 != text.size()) return false;
                const std::string at = text.substr(comma + 1, close - comma - 1);
                return word(text.substr(b, eq - b)) && word(text.substr(v + 6, comma - v - 6)) && !at.empty() &&
                       at.find_first_not_of("0123456789") == std::string::npos;
            }
            return word(text.substr(b, eq - b)) && v < text.size() && word(text.substr(v, text.size() - 1 - v));
        };
        std::map<uint32_t, std::set<size_t>> ownDrawn, copyDrawn;
        for (size_t li = 0; li < r.lines.size(); ++li)
        {
            const bool plain = plainCopy(r.lines[li].text);
            std::set<uint32_t> named;   // (the stores the line names without drawing them)
            for (const Token& t : r.lines[li].tokens)
                if (t.addr && t.begin == t.end && isStore(t.addr)) named.insert(t.addr);
            for (const Token& t : r.lines[li].tokens)
                // (A7-101: the dot of a piece's name, `xRam0600228c._2_2_`, is the name's: the operand is judged by the token before it)
                if (t.addr && t.begin < t.end && !(t.end == t.begin + 1 && t.end <= r.lines[li].text.size() && r.lines[li].text[t.begin] == '.'))
                {
                    // (A7-34's triage: at any instruction but a branch, `xRam26002208 = (xunknown2)uVar1;` at the `and` making uVar1)
                    // (A7-57: the operand of the line's cast drawn at an extension, `uRam2600219c = (uint4)param_2;` with param_2 at the
                    // `extu.w r5,r2` both stores of r2 store, is that extension's own work, the conversion the line prints (as A7-16's cast
                    // at an `exts`): not a copy of a value materialised there. A value drawn at a store that may alias it is)
                    bool castAtExtension = false;
                    {
                        const std::string& text = r.lines[li].text;
                        const size_t eq = text.find(" = ");
                        const size_t close = eq != std::string::npos && eq + 3 < text.size() && text[eq + 3] == '(' ? text.find(')', eq + 3) : std::string::npos;
                        uint16_t ew = 0;
                        if (close != std::string::npos && t.begin > close && word(t.addr, ew))
                        {
                            const std::string mnem = sfe::Sh2Decode(t.addr, ew).Mnemonic;
                            // (and `movt`, which extends the T bit into a register: `uRam06001030 = (uint4)bVar1;` with bVar1 at the
                            // `movt r2` both the store and the return use, grammar 1 seed 2400221)
                            castAtExtension = mnem.compare(0, 4, "exts") == 0 || mnem.compare(0, 4, "extu") == 0 || mnem == "movt";
                        }
                    }
                    // (A7-57: or a value drawn at a store, on a line drawn at another store -- `iRam060a51e4 = iRam060a5068;` with its
                    // name at `mov.l r1,@(0x4,r2)` and iRam060a5068 at `mov.l r1,@(0x8,r2)`, which may alias it: a store makes no value
                    // it reads, so the global's value is kept there, as a copy materialised at it. hwram008 0x0601B0F0)
                    bool drawnAtOtherStore = false;
                    const size_t assign = r.lines[li].text.find(" = ");
                    // (a bare value only, `V = W;`: a cast's operand drawn at a store is the conversion the line prints, `Ram26001030 =
                    // (uint2)uVar1;` at the next store, which the product does not take for a copy)
                    if (assign != std::string::npos && t.begin > assign && assign + 3 < r.lines[li].text.size() && r.lines[li].text[assign + 3] != '(' &&
                        isStore(t.addr))
                        for (const Token& u : r.lines[li].tokens)
                            drawnAtOtherStore = drawnAtOtherStore || (u.addr && u.addr != t.addr && u.begin < u.end && u.end <= assign && isStore(u.addr));
                    // (A7-69: the memory a line reads, `in_r0 = (int4)iRam2600100e;`, drawn at a store that is not a store of it -- one through a
                    // pointer that may alias it -- is the copy the product marks materialised there, a cast or not; grammar 1 seed 3004177)
                    bool readAtStore = false;
                    if (plain && assign != std::string::npos && t.begin > assign && t.end <= r.lines[li].text.size() && isStore(t.addr))
                    {
                        std::string operand = r.lines[li].text.substr(t.begin, t.end - t.begin);
                        // (A7-101: a piece of the global, `xRam0600228c._2_2_`, is the global's copy as the product marks it; grammar 2 seed 4506207)
                        // (Core and the tests build as C++14, so no `if (init; cond)` here -- MSVC rejects it with C2429.)
                        const size_t piece = operand.find("._");
                        if (piece != std::string::npos) operand.resize(piece);
                        if (operand.size() >= 11 && operand.compare(operand.size() - 11, 3, "Ram") == 0 &&
                            operand.find_first_not_of("0123456789abcdef", operand.size() - 8) == std::string::npos)
                        {
                            const uint32_t va = static_cast<uint32_t>(std::strtoul(operand.c_str() + operand.size() - 8, nullptr, 16));
                            bool own = false;
                            for (const AccessFate& f : r.accessFates)
                                own = own || (f.insn == t.addr && f.write && !f.frame && GlobalDelta(va, f.address) < f.size);
                            readAtStore = !own;
                        }
                    }
                    // (A7-69: a line drawn at a store of its own variable -- `uRam06001034 = 2;` at the word store of 2 to it, naming the byte
                    // store beside it -- is that store's statement, not a copy materialised there; grammar 1 seed 3204967)
                    bool ownStoreHere = false;
                    if (plain && assign != std::string::npos && isStore(t.addr))
                    {
                        const std::string& lt = r.lines[li].text;
                        const size_t nb = lt.find_first_not_of(' ');
                        if (nb != std::string::npos && assign >= nb + 11)
                        {
                            const std::string name = lt.substr(nb, assign - nb);
                            if (name.compare(name.size() - 11, 3, "Ram") == 0 && name.find_first_not_of("0123456789abcdef", name.size() - 8) == std::string::npos)
                            {
                                const uint32_t va = static_cast<uint32_t>(std::strtoul(name.c_str() + name.size() - 8, nullptr, 16));
                                for (const AccessFate& f : r.accessFates)
                                    ownStoreHere = ownStoreHere || (f.insn == t.addr && f.write && !f.frame && GlobalDelta(va, f.address) < f.size);
                                // (A7-129: not when another line assigns the variable with its name drawn at that store -- `cRam06001008 =
                                // (char)in_r3 + (char)param_2;` at `mov.b r3,@r6` -- which is the store's statement: this line's operand
                                // drawn there, `cRam06001008 = (char)param_2;` with the piece `(char)param_2` the engine made at the first
                                // store, is a copy materialised there (A7-116's mark); grammar 1 seed 7102553 (specials))
                                for (size_t lj = 0; lj < r.lines.size() && ownStoreHere && !A7RuleOff(129); ++lj)
                                {
                                    if (lj == li) continue;
                                    const std::string& ot = r.lines[lj].text;
                                    const size_t ob = ot.find_first_not_of(' '), oa = ot.find(" = ");
                                    if (ob == std::string::npos || oa == std::string::npos || oa <= ob || ot.compare(ob, oa - ob, name) != 0) continue;
                                    for (const Token& u : r.lines[lj].tokens)
                                        if (u.addr == t.addr && u.begin == ob && u.end == oa) ownStoreHere = false;
                                }
                            }
                        }
                    }
                    // (A7-78: a global the line reads, drawn at the instruction that reads it -- `iRam2600211c = (int4)iRam2600103a;` with
                    // `iRam2600103a` at the `mov.w @(r0,r6),r3` that loads it, naming the store of r3 -- is that load's own statement;
                    // grammar 1 seed 3300988)
                    bool readHere = false;
                    if (plain && t.end <= r.lines[li].text.size())
                    {
                        const std::string operand = r.lines[li].text.substr(t.begin, t.end - t.begin);
                        if (operand.size() >= 11 && operand.compare(operand.size() - 11, 3, "Ram") == 0 &&
                            operand.find_first_not_of("0123456789abcdef", operand.size() - 8) == std::string::npos)
                        {
                            const uint32_t va = static_cast<uint32_t>(std::strtoul(operand.c_str() + operand.size() - 8, nullptr, 16));
                            for (const AccessFate& f : r.accessFates)
                                readHere = readHere || (f.insn == t.addr && !f.write && !f.frame && GlobalDelta(va, f.address) < f.size);
                        }
                        // (A7-119: a frame local the line reads, `(int4)iStack_14` at the `mov.w @r14,r3` that loads it, is that load's own
                        // statement as a global is: the copy of what the load read into the store after it; grammar 2 seed 6205642)
                        const size_t sp = operand.find("Stack_");
                        if (!A7RuleOff(119) && sp != std::string::npos && operand.find_first_not_of("abcdefghijklmnopqrstuvwxyz", 0) == sp)
                        {
                            uint32_t hexv = 0;
                            size_t q = sp + 6, digits = 0;
                            while (q < operand.size() && std::isxdigit(static_cast<unsigned char>(operand[q])) && digits < 8)
                            {
                                const char c = operand[q];
                                hexv = hexv * 16 + static_cast<uint32_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
                                ++q;
                                ++digits;
                            }
                            uint32_t piece = 0;
                            if (digits > 0 && operand.compare(q, 2, "._") == 0) piece = static_cast<uint32_t>(std::strtoul(operand.c_str() + q + 2, nullptr, 10));
                            if (digits > 0)
                                for (const AccessFate& f : r.accessFates)
                                    readHere = readHere || (f.insn == t.addr && !f.write && f.frame && (0u - hexv + piece) - f.address < f.size);
                        }
                    }
                    // (A7-78: the function's input drawn at its first instruction -- `in_r3` at the entry's pool load -- is where the engine
                    // keeps every input; it is no value materialised there; grammar 1 seed 3804173)
                    bool inputAtEntry = false;
                    if (t.addr == r.entry && t.end <= r.lines[li].text.size())
                    {
                        const std::string operand = r.lines[li].text.substr(t.begin, t.end - t.begin);
                        inputAtEntry = operand.compare(0, 3, "in_") == 0 || operand.compare(0, 6, "param_") == 0 || operand.compare(0, 6, "unaff_") == 0;
                    }
                    const bool copy = plain && !isBranch(t.addr) && ((!named.empty() && !named.count(t.addr)) || drawnAtOtherStore || readAtStore) && !castAtExtension &&
                                      !ownStoreHere && !readHere && !inputAtEntry;
                    (copy ? copyDrawn : ownDrawn)[t.addr].insert(li);
                }
        }
        const auto copyOnly = [&](uint32_t at, size_t li) {
            const auto c = copyDrawn.find(at);
            const auto o = ownDrawn.find(at);
            return c != copyDrawn.end() && c->second.count(li) && (o == ownDrawn.end() || !o->second.count(li));
        };
        for (size_t li = 0; li < r.lines.size(); ++li)
        {
            for (uint32_t at : LineInstructions(r.lines[li])) owns[at].push_back(li);
            if (r.lines[li].firstAddr && !copyOnly(r.lines[li].firstAddr, li)) firsts.insert(r.lines[li].firstAddr);
        }
        for (const auto& kv : owns)
        {
            const LineLookup k = index.Find(kv.first);
            const bool owner = k.kind == LineLookup::Exact && !k.lines.empty() && std::find(kv.second.begin(), kv.second.end(), k.lines[0]) != kv.second.end();
            const bool copyFirst = owner && copyOnly(kv.first, k.lines[0]) && ownDrawn.count(kv.first);
            // (A7-64: a branch's own statement, `if (...) {` at the `bt`, is the line a click on it goes to when the only lines that begin
            // with the branch are plain copies drawn there, `cRam0600102f = (char)param_2;` at the `bt`: the product orders a copy it
            // materialised at the instruction after the statement, and the lines are the ones a click should reach second; grammar 1 seed
            // 2608672. Not for a line with an operation of its own, which begins with the branch and is answered first.)
            bool statementFirst = false;
            if (owner && !copyFirst && firsts.count(kv.first) && r.lines[k.lines[0]].firstAddr != kv.first && isBranch(kv.first) && !plainCopy(r.lines[k.lines[0]].text))
            {
                bool drawn = false;
                for (const Token& t : r.lines[k.lines[0]].tokens) drawn = drawn || (t.addr == kv.first && t.begin < t.end);
                statementFirst = drawn;
                for (size_t li = 0; li < r.lines.size() && statementFirst; ++li)
                    if (r.lines[li].firstAddr == kv.first && !plainCopy(r.lines[li].text))
                    {
                        // (A7-78: a statement of the slot whose assigned name and `=` only are drawn at the branch -- `xRamc0001024 =
                        // (xunknown1)(in_r2 ^ in_r0);` with its operation at the slot `xor r0,r2` -- does not begin with the branch
                        // for a click; grammar 1 seed 4002874)
                        const std::string& lt = r.lines[li].text;
                        const size_t eq = lt.find(" = ");
                        bool onlyName = eq != std::string::npos;
                        for (const Token& t : r.lines[li].tokens)
                            if (t.addr == kv.first && t.begin < t.end && (eq == std::string::npos || t.end > eq + 3)) onlyName = false;
                        if (!onlyName) statementFirst = false;
                    }
            }
            if (!owner || (firsts.count(kv.first) && r.lines[k.lines[0]].firstAddr != kv.first && !statementFirst) || copyFirst)
            {
                ++a.backWrong;
                problem("back", "the instruction " + MappingHex(kv.first) + " is answered " +
                                    (!owner ? "with no line that holds it" : copyFirst ? "with a copy materialised there before its own statement (A7-25)"
                                                                                       : "with a line that does not begin with it"));
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
                // (A7-51 (the area 7 fixer): a dereference of a plain pointer too, `xStack_8._0_2_ = *param_2;` -- a register the engine
                // merged under the piece's name, assigned at the load `mov.w @r5,r3`; a product has spaces, which the last test takes)
                callsHere = callsHere || text.find("*(", q) != std::string::npos || text.find('[', q) != std::string::npos ||
                            text.find(" = *", q) == q || text.find(' ', q + 3) != std::string::npos;
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
                // (A7-78: a call's output the engine merged under the frame word's name, on a path where the word's store does not store
                // it -- `xStack_38 = (xunknown4)xVar3;` at the second `bsr` of two, the push of r1 before it storing the first call's output --
                // is no copy of what the store wrote: when no store of the word stores a value the call made, the line names none (RegisterMakers
                // of each store's register known, none a call the line is drawn at); grammar 2 seed 3601551)
                if (atCall)
                {
                    std::set<uint32_t> drawnCalls;
                    for (const Token& t : r.lines[li].tokens)
                    {
                        uint16_t cw = 0;
                        if (t.addr && t.begin < t.end && word(t.addr, cw) && ShapeOf(sfe::Sh2Decode(t.addr, cw)).call) drawnCalls.insert(t.addr);
                    }
                    const std::map<uint32_t, std::vector<uint32_t>> into = BranchSources(r, word);
                    bool known = true, related = false;
                    for (uint32_t st : candidates)
                    {
                        uint16_t sw = 0;
                        int size = 0;
                        const int reg = word(st, sw) ? StoredRegister(sw, size) : -1;
                        std::set<uint32_t> makers;
                        if (reg < 0 || !RegisterMakers(r, word, into, st, reg, makers))
                        {
                            known = false;
                            break;
                        }
                        for (uint32_t c : drawnCalls) related = related || makers.count(c) != 0;
                    }
                    if (known && !related) continue;
                }
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
                int pieceSize = 0;   // (`._0_2_`: the piece's offset and size)
                if (text.compare(q, 2, "._") == 0)
                {
                    q += 2;
                    piece = static_cast<uint32_t>(std::strtoul(text.c_str() + q, nullptr, 10));
                    while (q < text.size() && text[q] != '_') ++q;
                    ++q;
                    pieceSize = std::atoi(text.c_str() + q);
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
                    // (a global by any mirror of its address)
                    if ((frame ? at - f->address : GlobalDelta(at, f->address)) < f->size)
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
                // (A7-34's triage: a register the engine names after a call's output, `extraout_r8`, merged under the variable's name at a
                // loop's start, is counted as a register variable's merge is)
                const bool mergedRegister = registerVariable || shownText.compare(0, 10, "extraout_r") == 0;
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
                std::map<uint32_t, int> storeSize, storeShift;   // ...and where in the store the variable begins (a piece of a wider store)
                for (const AccessFate* f : folded)
                    if (candidates.count(f->insn))
                    {
                        storeSize[f->insn] = std::max(storeSize[f->insn], static_cast<int>(f->size));
                        storeShift[f->insn] = static_cast<int>(frame ? at - f->address : GlobalDelta(at, f->address));
                    }
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
                    if (shownValue.kind == WordValue::Param)
                    {
                        if (got == shownValue || (got.kind == WordValue::Number && got.byCondition)) return false;
                        // (A7-120: or the store writes only bytes the extension of a narrow unsigned parameter makes 0 -- `(uint4)param_2` of a
                        // `uint2`, the byte store of 0 at the word's high byte -- and its bytes there are 0; grammar 1 seed 6603025)
                        if (got.kind == WordValue::Number && cast && !A7RuleOff(120))
                        {
                            const int pw = DeclaredWidth(r, shownText);
                            bool unsignedParam = false;
                            for (const auto& sig : r.lines)
                            {
                                const size_t nm = sig.text.find(" " + shownText);
                                if (nm == std::string::npos || nm + 1 + shownText.size() >= sig.text.size()) continue;
                                const char after = sig.text[nm + 1 + shownText.size()];
                                if (after != ',' && after != ')') continue;
                                size_t b = nm;
                                while (b > 0 && (std::isalnum(static_cast<unsigned char>(sig.text[b - 1])) || sig.text[b - 1] == '_')) --b;
                                unsignedParam = b < nm && sig.text[b] == 'u';
                                break;
                            }
                            const size_t open = text.find('(', text.find(" = "));
                            const int vw = open != std::string::npos && text.find(')', open) == open + 6 && std::isdigit(static_cast<unsigned char>(text[open + 5])) ? text[open + 5] - '0' : 4;
                            const int sh = std::min(std::max(storeShift[store], 0), size - 1);
                            const int covered = std::min(size - sh, vw);
                            if (unsignedParam && pw < vw && covered > 0 && covered <= vw - pw)
                            {
                                bool zero = true;
                                for (int j = 0; j < covered; ++j) zero = zero && ((static_cast<uint64_t>(got.value) >> (8 * (size - 1 - (sh + j)))) & 0xFF) == 0;
                                if (zero) return false;
                            }
                        }
                        return true;
                    }
                    if (shownValue.kind == WordValue::Number)
                    {
                        // (a parameter may be the number on the line's path: `param_4 == 0` tested before the store)
                        if (got.kind == WordValue::Param) return false;
                        if (composed) return false;   // (a number made of a word and a narrower store: each writes some of its bytes)
                        // (the bytes of the store from the variable's on, the SH-2 being big-endian: the rest of the store, or -- A7-36 --
                        // as many as an access the function makes at the variable's address has, so a byte at the start of a word store
                        // is the word's high byte. The variable's size is not in the text for a global: another value only when no such
                        // width gives the number)
                        const int shift = std::min(std::max(storeShift[store], 0), size - 1);
                        std::set<int> widths = { size - shift };
                        const auto delta = [frame](uint32_t a, uint32_t b) { return frame ? a - b : GlobalDelta(a, b); };
                        for (const AccessFate& f : r.accessFates)
                            if (f.size > 0 && delta(f.address, at) == 0 && static_cast<int>(f.size) < size - shift) widths.insert(static_cast<int>(f.size));
                        // (a piece the text names, `xRam3600228c._0_2_`, has its own size)
                        if (pieceSize > 0 && pieceSize < size - shift) widths = { pieceSize };
                        // (A7-69: the variable ends where the next global the text names begins: `xRam2600200e = 0xffff;` beside
                        // `xRam26002010 = 0xff9d;` is the high half of a word store; grammar 1 seed 3000756)
                        std::set<int> neighbourWidths;   // (these must have a byte to compare)
                        if (!frame)
                            for (const auto& other : r.lines)
                                for (size_t g = other.text.find("Ram"); g != std::string::npos; g = other.text.find("Ram", g + 3))
                                {
                                    if (g + 11 > other.text.size() || other.text.find_first_not_of("0123456789abcdef", g + 3) != g + 11) continue;
                                    const uint32_t ga = static_cast<uint32_t>(std::strtoul(other.text.substr(g + 3, 8).c_str(), nullptr, 16));
                                    const uint32_t gd = GlobalDelta(ga, at);
                                    if (gd > 0 && gd < static_cast<uint32_t>(size - shift) && widths.insert(static_cast<int>(gd)).second) neighbourWidths.insert(static_cast<int>(gd));
                                }
                        // (the store's bytes another store of the variable writes too may be that store's in the value shown: compared are
                        // the ones no other writes, when there are such, as the product compares them -- `xRam26001014 = 0;` for a word
                        // store of 1 whose low byte a byte store writes after it)
                        uint32_t storeAt = 0;
                        for (const AccessFate* f : folded)
                            if (f->insn == store) storeAt = f->address;
                        std::vector<bool> alone(static_cast<size_t>(size), true);
                        bool anyAlone = false;
                        for (int k = 0; k < size; ++k)
                        {
                            for (const AccessFate* f : folded)
                                if (f->insn != store && f->frame == frame && delta(storeAt + static_cast<uint32_t>(k), f->address) < f->size)
                                    alone[static_cast<size_t>(k)] = false;
                            anyAlone = anyAlone || alone[static_cast<size_t>(k)];
                        }
                        for (int bytes : widths)
                        {
                            bool same = true;
                            int compared = 0;
                            for (int j = 0; j < bytes && same; ++j)
                            {
                                const int k = shift + j;   // (the store's byte, from its address)
                                if (k >= size || (anyAlone && !alone[static_cast<size_t>(k)] && !neighbourWidths.count(bytes))) continue;
                                ++compared;
                                same = (static_cast<uint64_t>(got.value) >> (8 * (size - 1 - k)) & 0xFF) ==
                                       (static_cast<uint64_t>(shownValue.value) >> (8 * (bytes - 1 - j)) & 0xFF);
                            }
                            if (same && (compared > 0 || !neighbourWidths.count(bytes))) return false;
                        }
                        // (A7-64: the variable is wider than the store, an access the function makes at its address says so: a half-word store
                        // writes the variable's high half, `xRam06001024 = 0x10000;` for the store of 1 beside a word store of the address)
                        for (const AccessFate& f : r.accessFates)
                        {
                            const int wide = static_cast<int>(f.size);
                            if (wide <= size - shift || wide > 4 || delta(f.address, at) != 0) continue;
                            bool same = true;
                            for (int j = 0; j < size - shift && same; ++j)
                            {
                                const int k = shift + j;
                                if (k >= size || (anyAlone && !alone[static_cast<size_t>(k)])) continue;
                                same = (static_cast<uint64_t>(got.value) >> (8 * (size - 1 - k)) & 0xFF) ==
                                       (static_cast<uint64_t>(shownValue.value) >> (8 * (wide - 1 - j)) & 0xFF);
                            }
                            if (same) return false;
                        }
                        return true;
                    }
                    if (!registerVariable || got.kind != WordValue::Param) return false;
                    // (A7-42: a register variable the text assigns from that parameter, whole or through a cast, holds it -- a byte of
                    // param_1 is `xVar1 = (char)param_1;`, named after no parameter; grammar 1 seed 1843373)
                    // (A7-63: or through the variables the text assigns it from in turn, `xVar1 = xRam26001000;` after `xRam26001000 =
                    // param_1;`, grammar 1 seed 2404131; up to three steps)
                    const std::string from = "param_" + std::to_string(got.value) + ";";
                    std::vector<std::string> names = { shownText };
                    for (size_t k = 0; k < names.size() && k < 4; ++k)
                        for (const auto& other : r.lines)
                        {
                            const size_t s = other.text.find_first_not_of(' ');
                            if (s == std::string::npos || other.text.compare(s, names[k].size() + 3, names[k] + " = ") != 0) continue;
                            std::string rhs = other.text.substr(s + names[k].size() + 3);
                            while (!rhs.empty() && rhs[0] == '(')
                            {
                                const size_t close = rhs.find(')');
                                if (close == std::string::npos || rhs.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_ *", 1) < close) break;
                                rhs = rhs.substr(close + 1);
                            }
                            if (rhs == from) return false;
                            // (A7-102: the low part of the parameter, `SUB41(param_2,0)`, is the register the narrow store stores; grammar 2 seed 4508555.
                            // A5-118's merge rule, patch 0090, makes the same shape: a byte or half of a pointer parameter held in a variable across a
                            // register's access, `xVar2 = SUB41(param_2,0);`, which a `mov.b` stores; grammar 1 seed 3800027)
                            if (rhs.compare(0, 3, "SUB") == 0 && rhs.find('(') != std::string::npos &&
                                rhs.compare(rhs.find('(') + 1, std::string::npos, from.substr(0, from.size() - 1) + ",0);") == 0)
                                return false;
                            const std::string name = rhs.empty() ? rhs : rhs.substr(0, rhs.size() - 1);
                            if (!name.empty() && rhs.back() == ';' && name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos &&
                                std::find(names.begin(), names.end(), name) == names.end())
                                names.push_back(name);
                        }
                    // (A7-142: a register the engine names after a call's output, `extraout_r9`, is the value that register has when the call
                    // returns; for r8 to r14, which a callee keeps, the value it had at the call. The variable holds the parameter when it
                    // is the parameter's after every call of the function -- `pxVar1 = extraout_r9;` for r9 = r5 on entry, stored by
                    // `mov.b r5,@r6` after `mov r9,r5`; grammar 2 seed 7607636)
                    if (!A7RuleOff(142))
                        for (const std::string& name : names)
                        {
                            if (name.compare(0, 10, "extraout_r") != 0 || name.size() < 11 || !std::isdigit(static_cast<unsigned char>(name[10]))) continue;
                            const size_t digits = name.find_first_not_of("0123456789", 10);
                            if (digits != std::string::npos && name[digits] != '_') continue;
                            const int kept = std::atoi(name.c_str() + 10);
                            if (kept < 8 || kept > 14) continue;
                            int calls = 0;
                            bool all = true;
                            for (const auto& range : r.body)
                                for (uint32_t p = range.first; p < range.second && all; p += 2)
                                {
                                    uint16_t cw = 0;
                                    if (!word(p, cw)) continue;
                                    const sfe::DisassembledInstruction cd = sfe::Sh2Decode(p, cw);
                                    if (!cd.IsValid || !cd.IsCall) continue;
                                    ++calls;
                                    all = RegisterBefore(r, word, sources, p + (cd.HasDelaySlot ? 4u : 2u), kept) == got;
                                }
                            if (calls > 0 && all) return false;
                        }
                    return true;
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
                // Area 7 round 7 (A7-32): a copy drawn only at a call or a return is the value the engine shows there (a call's effect on
                // a frame word, A7-11; a held value at a return, A7-15), and the stores it names wrote that value before it. A named store
                // that cannot run before the instruction -- after it in the same flow block (not its delay slot), or in a block from which the
                // flow does not reach the instruction's block -- wrote nothing the call or the return sees: the line names a store of a
                // later value. The flow blocks are the flow's (as copyUnconnected reads them), the instructions the words.
                {
                    std::set<uint32_t> uses;
                    bool useOnly = true;
                    for (const Token& t : r.lines[li].tokens)
                    {
                        if (!t.addr || t.begin == t.end) continue;
                        uint16_t uw = 0;
                        if (!word(t.addr, uw))
                        {
                            useOnly = false;
                            break;
                        }
                        const sfe::DisassembledInstruction ud = sfe::Sh2Decode(t.addr, uw);
                        if (!(ud.IsCall || ud.Mnemonic == "rts" || ud.Mnemonic == "rte"))
                        {
                            useOnly = false;
                            break;
                        }
                        uses.insert(t.addr);
                    }
                    const auto blockOf = [&r](uint32_t x) {
                        for (size_t k = 0; k < r.flowBlockRanges.size(); ++k)
                            if (r.flowBlockRanges[k].first <= x && x <= r.flowBlockRanges[k].second) return static_cast<int>(k);
                        return -1;
                    };
                    // (may the store at `s` run before the instruction at `u` on some path?)
                    const auto before = [&](uint32_t s, uint32_t u) {
                        const int bs = blockOf(s), bu = blockOf(u);
                        if (bs < 0 || bu < 0) return true;   // (not known)
                        if (bs == bu && (s < u || s == u + 2)) return true;
                        std::set<int> seen;
                        std::vector<int> todo;
                        if (static_cast<size_t>(bs) < r.flowSuccessors.size())
                            for (int n : r.flowSuccessors[static_cast<size_t>(bs)])
                                if (seen.insert(n).second) todo.push_back(n);
                        while (!todo.empty())
                        {
                            const int b = todo.back();
                            todo.pop_back();
                            if (b == bu) return true;
                            if (b < 0 || static_cast<size_t>(b) >= r.flowSuccessors.size()) continue;
                            for (int n : r.flowSuccessors[static_cast<size_t>(b)])
                                if (seen.insert(n).second) todo.push_back(n);
                        }
                        return false;
                    };
                    if (useOnly && !uses.empty() && !r.flowBlockRanges.empty())
                        for (const Token& t : r.lines[li].tokens)
                        {
                            if (!t.addr || !candidates.count(t.addr)) continue;
                            bool any = false;
                            for (uint32_t u : uses) any = any || t.addr == u || before(t.addr, u);
                            if (any) continue;
                            // (a store of the call's own output -- `xRam0603b480 = (uint4)uVar2;`, r0 after the call stored -- is the line's
                            // value written after the call, as it should be: the call is one of what made the stored register. Not for a line
                            // that shows a parameter or a number, which no call returns)
                            if (shownValue.kind != WordValue::Param && shownValue.kind != WordValue::Number)
                            {
                                uint16_t sw = 0;
                                int size = 0;
                                const int reg = word(t.addr, sw) ? StoredRegister(sw, size) : -1;
                                if (reg < 0) continue;
                                if (!haveSources)
                                {
                                    sources = BranchSources(r, word);
                                    haveSources = true;
                                }
                                std::set<uint32_t> makers;
                                if (!RegisterMakers(r, word, sources, t.addr, reg, makers)) continue;
                                bool output = false;
                                for (uint32_t u : uses) output = output || makers.count(u) != 0;
                                if (output) continue;
                            }
                            ++a.storeAfterUse;
                            problem("store-after", "line " + std::to_string(li) + " '" + text + "' is drawn at " + MappingHex(*uses.begin()) +
                                                       " (a call or a return) and names the store at " + MappingHex(t.addr) +
                                                       ", which cannot run before it (A7-32)");
                            break;
                        }
                }
                bool named = false, atBranch = false, atCall = false, atReturn = false, atLoad = false;
                // (A7-21: a register merge's line names the instructions that set the register, or nothing; never only the branch that ends
                // its block)
                bool anyAddress = false, branchOnly = true, drawnAny = false, makersOnly = true;
                for (const Token& t : r.lines[li].tokens)
                {
                    if (!t.addr) continue;
                    anyAddress = true;
                    {
                        uint16_t mw = 0;
                        if (!word(t.addr, mw)) makersOnly = false;
                        else
                        {
                            const sfe::DisassembledInstruction md = sfe::Sh2Decode(t.addr, mw);
                            const MemoryShape mm = ShapeOf(md);
                            if (mm.load || mm.store || mm.call || md.IsBranch || md.HasDelaySlot || md.Mnemonic == "rts" || md.Mnemonic == "rte" ||
                                WrittenRegisters(md).empty())
                                makersOnly = false;
                            if (t.begin < t.end)
                            {
                                drawnAny = true;
                                // (A7-30: any instruction that makes nothing -- a branch, a `tst`, a compare, a nop -- not only a branch)
                                // (A7-78: a store too: the value a store of a volatile word sends is materialised at it, `cRam26001045 = (char)pxVar2;`
                                // at `mov.b r0,@r7` beside `Ram26001038 = cRam26001045;`; grammar 1 seed 3800581)
                                if (mm.call || mm.load || mm.store || md.Mnemonic == "rts" || md.Mnemonic == "rte" || !WrittenRegisters(md).empty()) branchOnly = false;
                            }
                        }
                    }
                    named = named || candidates.count(t.addr) != 0;
                    // (A7-34's triage: a store of the variable the engine did not fold, a write to memory the request marks volatile, drawn
                    // at its own instruction)
                    if (!frame && t.begin < t.end)
                        for (const MemoryAccess& m : r.accesses)
                            // (or inside it: `mov.w r1,@(r0,r7)` writing 0x26002306, the low half of `uRam26002304 = (uint4)in_r1;`,
                            // grammar 1 seed 2509053 routed from the area 2 round 20 reviewer)
                            if (m.write && m.direct && m.insn == t.addr && GlobalDelta(at, m.address) < m.size) named = true;
                    // (A7-56: or a folded store drawn there that writes inside the variable, after its first byte: `mov.w r1,@(r0,r7)`
                    // writing 0x26002306, the low half of `uRam26002304 = (uint4)in_r1;`, grammar 1 seed 2509053 routed from the area 2
                    // round 20 reviewer)
                    if (t.begin < t.end)
                        for (const AccessFate* f : folded)
                            if (f->insn == t.addr && f->frame == frame && (frame ? f->address - at : GlobalDelta(f->address, at)) < 4) named = true;
                    uint16_t w = 0;
                    if (t.begin == t.end || !word(t.addr, w)) continue;
                    const sfe::DisassembledInstruction d = sfe::Sh2Decode(t.addr, w);
                    const MemoryShape m = ShapeOf(d);
                    atCall = atCall || m.call;
                    atReturn = atReturn || d.Mnemonic == "rts" || d.Mnemonic == "rte";
                    atBranch = atBranch || (!m.call && (d.IsBranch || d.HasDelaySlot));
                    atLoad = atLoad || m.load;
                    // (a number the line names the `mov #i` or the literal load of is made there: the statement is that instruction's --
                    // A7-33 (the area 7 fixer): when it makes the number into the register a store of the variable stores, or RegisterMakers
                    // from such a store finds it (or that is not known); a `mov #0x4,r3` in a slot whose r3 no store of the variable stores
                    // is not the line's. A loop's `mov #1,r3` whose r3 a `dt` counts down before the store is: the engine names the loop's
                    // counter after the global)
                    if (number && (d.PcRel != sfe::Sh2PcRel::None || (d.Mnemonic == "mov" && d.Operands.compare(0, 1, "#") == 0)))
                    {
                        if (!haveSources)
                        {
                            sources = BranchSources(r, word);
                            haveSources = true;
                        }
                        bool known = true, maker = false;
                        const int into = (w >> 12) == 0xE || (w >> 12) == 0x9 || (w >> 12) == 0xD ? (w >> 8) & 0xF : 0;
                        for (uint32_t st : candidates)
                        {
                            uint16_t sw = 0;
                            int size = 0;
                            const int reg = word(st, sw) ? StoredRegister(sw, size) : -1;
                            std::set<uint32_t> makers;
                            if (reg == into) maker = true;
                            else if (reg < 0 || !RegisterMakers(r, word, sources, st, reg, makers)) known = false;
                            else maker = maker || makers.count(t.addr) != 0;
                        }
                        if (maker || !known) named = true;
                    }
                    // (and a cast the line names an `exts` or `extu` of is that extension, a computation: A7-16's shape)
                    if (cast && (d.Mnemonic.compare(0, 4, "exts") == 0 || d.Mnemonic.compare(0, 4, "extu") == 0)) named = true;
                }
                if (named) continue;
                // (A7-78: as the frame-store check: a call's output merged under the variable's name where no store of the variable stores
                // a value that call made; grammar 2 seed 3601551)
                if (atCall && frame)
                {
                    std::set<uint32_t> drawnCalls;
                    bool callsOnly = true;
                    for (const Token& t : r.lines[li].tokens)
                    {
                        uint16_t cw = 0;
                        if (!t.addr || t.begin >= t.end) continue;
                        if (word(t.addr, cw) && ShapeOf(sfe::Sh2Decode(t.addr, cw)).call) drawnCalls.insert(t.addr);
                        else callsOnly = false;
                    }
                    if (callsOnly && !drawnCalls.empty())
                    {
                        if (!haveSources)
                        {
                            sources = BranchSources(r, word);
                            haveSources = true;
                        }
                        bool known = true, related = false;
                        for (uint32_t st : candidates)
                        {
                            uint16_t sw = 0;
                            int size = 0;
                            const int reg = word(st, sw) ? StoredRegister(sw, size) : -1;
                            std::set<uint32_t> makers;
                            if (reg < 0 || !RegisterMakers(r, word, sources, st, reg, makers))
                            {
                                known = false;
                                break;
                            }
                            for (uint32_t c : drawnCalls) related = related || makers.count(c) != 0;
                        }
                        if (known && !related)
                        {
                            ++a.copyOtherValue;
                            continue;
                        }
                    }
                }
                // (A7-45 (the area 7 fixer): a number line drawn only at instructions that make the number (`mov #imm`, a literal), when
                // no store of the variable stores that number as an instruction makes it -- the makers of each store's register known
                // (RegisterMakers), none a `mov #imm` or a literal of the line's number (an input, another number may be); a store of no
                // register, `tas.b`, makes its own -- is a register the
                // engine merged under the variable's name, as a register variable's merge is: `iRam0600218c = 0xb;` at `mov #0xb,r0`, r0
                // the global's load, whose stores write a loaded byte and a loaded word. Naming one of those stores would name another
                // value (numberStoreOther, A7-28, at the maker; storeValueOther at the store). Counted apart.)
                if (number && anyAddress)
                {
                    const auto numberOf = [&](uint32_t p, uint16_t w, uint32_t& value) {
                        uint16_t hi = 0, lo = 0;
                        if ((w >> 12) == 0xE) return value = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(w & 0xFF))), true;
                        if ((w >> 12) == 0x9 && word(p + 4 + (w & 0xFFu) * 2, lo))
                            return value = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(lo))), true;
                        const uint32_t q = (p & ~3u) + 4 + (w & 0xFFu) * 4;
                        if ((w >> 12) == 0xD && word(q, hi) && word(q + 2, lo)) return value = static_cast<uint32_t>(hi) << 16 | lo, true;
                        return false;
                    };
                    const uint32_t shown = static_cast<uint32_t>(shownValue.value);
                    const auto sameNumber = [shown](uint32_t v) {
                        for (int n = 1; n <= 4; n *= 2)
                        {
                            const uint32_t mask = n == 4 ? ~0u : (1u << (8 * n)) - 1;
                            if ((n == 4 || shown <= mask) && ((v ^ shown) & mask) == 0) return true;
                        }
                        return false;
                    };
                    // (a compare in the body: `cmp/eq #imm,r0` of the number, or any compare of two registers, which may hold it)
                    // (A7-56: `cmp/eq r1,r1` compares a register with itself, which ties it to no number; A7-61 (the area 5
                    // fixer's seed 2017373): a compare of two registers ties the stored input only when one of them is the stored register
                    // or a register moved into it -- `cmp/eq r3,r2` with r3 a global's address does not tie the r6 a store stores)
                    const auto inputTied = [&](int reg) {
                        std::set<int> chain = { reg };
                        for (int round = 0; round < 3; ++round)
                            for (const auto& range : r.flowBlockRanges)
                                for (uint32_t p = range.first; p <= range.second; p += 2)
                                {
                                    uint16_t mw = 0;
                                    if (word(p, mw) && (mw >> 12) == 0x6 && (mw & 0xF) == 0x3 && chain.count((mw >> 8) & 0xF)) chain.insert((mw >> 4) & 0xF);
                                }
                        for (const auto& range : r.flowBlockRanges)
                            for (uint32_t p = range.first; p <= range.second; p += 2)
                            {
                                uint16_t cw = 0;
                                if (!word(p, cw)) continue;
                                if ((cw >> 8) == 0x88 && sameNumber(static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(cw & 0xFF))))) return true;
                                const int n = (cw >> 8) & 0xF, m = (cw >> 4) & 0xF;
                                if ((cw >> 12) == 0x3 && (cw & 0xF) == 0x0 && n != m && (chain.count(n) || chain.count(m)))
                                {
                                    // (...against a register that is only the function's input, which no number made: `cmp/eq r1,r3` with
                                    // r1 in_r1 ties r3 to nothing the engine knows, grammar 1 seed 2500514 routed from the area 2 round 20
                                    // reviewer)
                                    const int other = chain.count(n) ? m : n;
                                    std::set<uint32_t> makers;
                                    if (!chain.count(other) && RegisterMakers(r, word, sources, p, other, makers) && makers.size() == 1 && makers.count(0))
                                        continue;
                                    return true;
                                }
                            }
                        return false;
                    };
                    bool apart = true;
                    for (const Token& t : r.lines[li].tokens)
                    {
                        if (!t.addr) continue;
                        uint16_t mw = 0;
                        uint32_t got = 0;
                        if (t.begin == t.end || !word(t.addr, mw) || !numberOf(t.addr, mw, got) || !sameNumber(got))
                        {
                            apart = false;
                            break;
                        }
                    }
                    if (apart && !haveSources)
                    {
                        sources = BranchSources(r, word);
                        haveSources = true;
                    }
                    for (uint32_t st : candidates)
                    {
                        if (!apart) break;
                        // (A5-118's rule, patch 0090: a store of the number that is the statement of another line, its own, is not this line's: a
                        // loop counter started at 2 and merged under the global's name beside a store of 2 the barrier keeps, grammar 1 seed 15125)
                        bool ownLine = false;
                        for (size_t lj = 0; lj < r.lines.size() && !ownLine; ++lj)
                        {
                            if (lj == li || r.lines[lj].text.find(" = ") == std::string::npos) continue;
                            for (const Token& t : r.lines[lj].tokens) ownLine = ownLine || (t.addr == st && t.begin < t.end);
                        }
                        if (ownLine) continue;
                        uint16_t sw = 0;
                        int size = 0;
                        const int reg = word(st, sw) ? StoredRegister(sw, size) : -1;
                        if (reg < 0) continue;
                        std::set<uint32_t> makers;
                        if (!RegisterMakers(r, word, sources, st, reg, makers))
                        {
                            apart = false;
                            break;
                        }
                        for (uint32_t m : makers)
                        {
                            uint16_t mw = 0;
                            uint32_t got = 0;
                            // (the line's number, made by some instruction; another number, `mov #0x3,r1` stored where the line
                            // shows 0x18, is another value. The function's input is the number only where a compare of the body
                            // can say so, as the product's A7-41 rule has it: `cmp/eq #4,r0` of A7-28's fixture, r0 the input
                            // stored; a store of `in_r3` with no compare is no store of the line's number)
                            if (m == 0 ? inputTied(reg) : word(m, mw) && numberOf(m, mw, got) && sameNumber(got)) apart = false;
                        }
                    }
                    if (apart)
                    {
                        ++a.copyNumberMade;
                        continue;
                    }
                }
                if (mergedRegister && drawnAny && branchOnly)
                {
                    ++a.copyMergeAtBranch;
                    problem("copy-merge", "line " + std::to_string(li) + " '" + text + "' names no store of the variable and is drawn only at instructions that make nothing, " +
                                              MappingHex(r.lines[li].firstAddr) + " first (A7-21, A7-30)");
                    continue;
                }
                if (mergedRegister && anyAddress && makersOnly)
                {
                    ++a.copyRegisterMade;
                    continue;
                }
                if (mergedRegister && !anyAddress)
                {
                    ++a.copyNoAddress;
                    continue;
                }
                // (A7-55: a copy of the function's input -- a parameter, `in_r4`, `unaff_gbr` -- the engine put in a register merged under the
                // variable's name, drawn only at the moves that put the input in that register (`iRam26002130 = param_1;` at `mov r4,r1`, r1
                // then reduced in a loop and stored), or naming nothing and saying so (`pxStack_20 = param_1;`, r4 itself the merged pointer,
                // pushed after the loop advanced it): when no store of the variable stores the input -- the makers of each store's register
                // known (RegisterMakers, through moves), none the entry -- naming a store would name another value. Counted apart.)
                {
                    const bool inputShown = shownValue.kind == WordValue::Param || shownText.compare(0, 3, "in_") == 0 || shownText.compare(0, 6, "unaff_") == 0;
                    // (A7-78: a load into the register may read the input back only when the body stores the input -- a store whose register's
                    // makers include the entry or are not known -- somewhere; a body that stores nothing of it holds the input nowhere in
                    // memory, and the load is the memory's own value, `mov.w @r6,r0` of a register: grammar 1 seed 3706930)
                    const auto bodyStores = [&](bool loadKnown, uint32_t loadAt, int loadSize, int inputReg) {
                        std::set<uint32_t> pool;   // (the literal pool among the body's words is data: the words a pc-relative load reads)
                        for (const auto& range : r.body)
                            for (uint32_t q = range.first; q < range.second; q += 2)
                            {
                                uint16_t qw = 0;
                                if (!word(q, qw)) continue;
                                if ((qw >> 12) == 0x9) pool.insert(q + 4 + (qw & 0xFFu) * 2);
                                if ((qw >> 12) == 0xD || (qw >> 8) == 0xC7)
                                {
                                    pool.insert((q & ~3u) + 4 + (qw & 0xFFu) * 4);
                                    pool.insert((q & ~3u) + 4 + (qw & 0xFFu) * 4 + 2);
                                }
                            }
                        if (!haveSources)
                        {
                            sources = BranchSources(r, word);
                            haveSources = true;
                        }
                        for (const auto& range : r.body)
                            for (uint32_t q = range.first; q < range.second; q += 2)
                            {
                                uint16_t qw = 0;
                                int qsize = 0;
                                if (pool.count(q) || !word(q, qw)) continue;
                                const sfe::DisassembledInstruction qd = sfe::Sh2Decode(q, qw);
                                const int qreg = qd.IsValid && ShapeOf(qd).store ? StoredRegister(qw, qsize) : -1;
                                std::set<uint32_t> qm;
                                std::set<int> entered;
                                if (qreg < 0) continue;
                                const bool walked = RegisterMakers(r, word, sources, q, qreg, qm, false, &entered);
                                if (walked && !qm.count(0)) continue;
                                // (the input the line shows: a store of another input's register holds another value)
                                if (walked && inputReg >= 0 && !entered.empty() && !entered.count(inputReg)) continue;
                                // (the load's address and the store's both known, and apart: the load reads another word)
                                uint32_t sa = 0;
                                int ss = 0;
                                if (loadKnown && AccessAddress(r, word, sources, q, qw, true, sa, ss, 0) && (sa + static_cast<uint32_t>(ss) <= loadAt || loadAt + static_cast<uint32_t>(loadSize) <= sa))
                                    continue;
                                return true;
                            }
                        return false;
                    };
                    bool movesOnly = anyAddress;
                    // (A7-69: or at a store of the same value -- the register's makers the variable's folded store's register has -- through
                    // another address: `xRam26002108 = (xunknown1)param_2;` at the pointer store `mov.b r1,@-r5` of the byte `mov.b r1,@r6` stores
                    // to the variable, the engine having made the byte once; grammar 1 seed 2704995)
                    const auto sameValueStore = [&](uint32_t at, uint16_t aw) {
                        int asize = 0;
                        const int areg = StoredRegister(aw, asize);
                        if (areg < 0) return false;
                        if (!haveSources)
                        {
                            sources = BranchSources(r, word);
                            haveSources = true;
                        }
                        std::set<uint32_t> mine;
                        if (!RegisterMakers(r, word, sources, at, areg, mine) || mine.empty() || mine.count(0)) return false;
                        for (uint32_t st : candidates)
                        {
                            uint16_t sw = 0;
                            int ssz = 0;
                            const int sreg = word(st, sw) ? StoredRegister(sw, ssz) : -1;
                            std::set<uint32_t> theirs;
                            if (sreg >= 0 && ssz == asize && RegisterMakers(r, word, sources, st, sreg, theirs) && theirs == mine) return true;
                        }
                        return false;
                    };
                    // (A7-69: or at the instruction that writes the input's own register next -- `xRam06002080 = in_r2;` at `xtrct r2,r2`, the
                    // engine keeping the input ahead of the write that destroys it; grammar 1 seed 3208727)
                    int inputRegister = -1;
                    if (shownText.compare(0, 4, "in_r") == 0 && shownText.size() > 4 && shownText.find_first_not_of("0123456789", 4) == std::string::npos)
                        inputRegister = std::atoi(shownText.c_str() + 4);
                    else if (shownValue.kind == WordValue::Param)
                        inputRegister = 3 + static_cast<int>(shownValue.value);
                    for (const Token& t : r.lines[li].tokens)
                    {
                        uint16_t mw = 0;
                        if (t.addr && (!word(t.addr, mw) ||
                                       (((mw >> 12) != 0x6 || (mw & 0xF) != 0x3) && !(t.begin < t.end && sameValueStore(t.addr, mw)) &&
                                        !(inputRegister >= 0 && WrittenRegisters(sfe::Sh2Decode(t.addr, mw)).count(inputRegister)))))
                            movesOnly = false;
                    }
                    if (inputShown && (anyAddress ? movesOnly : r.lines[li].noOrigin))
                    {
                        if (!haveSources)
                        {
                            sources = BranchSources(r, word);
                            haveSources = true;
                        }
                        bool storesInput = false;
                        for (uint32_t st : candidates)
                        {
                            uint16_t sw = 0;
                            int size = 0;
                            const int reg = word(st, sw) ? StoredRegister(sw, size) : -1;
                            if (reg < 0) continue;   // (`tas.b`'s byte: made there)
                            // (A7-64: a store narrower than the cast the line prints writes less than it shows: `xRamc0002024 =
                            // (xunknown2)in_r1;` is the word store of r1 xored with a number whose low half is 0, not the byte store of
                            // in_r1 beside it; grammar 1 seed 2601590)
                            {
                                const size_t cast = text.find(" = (");
                                const size_t close = cast == std::string::npos ? cast : text.find(')', cast + 4);
                                if (close != std::string::npos && close > cast + 4 && std::isdigit(static_cast<unsigned char>(text[close - 1])) &&
                                    text[close - 1] - '0' > storeSize[st])
                                    continue;
                            }
                            std::set<uint32_t> makers;
                            if (!RegisterMakers(r, word, sources, st, reg, makers) || makers.count(0)) storesInput = true;
                            // (a load into the register may read the input back from where it was put -- `mov.l r3,@r15 ; mov.w
                            // @(2,r15),r0` -- and is not known to be another value; a post-increment's step of its address register is)
                            for (uint32_t m : makers)
                            {
                                uint16_t mw = 0;
                                if (!m || !word(m, mw)) continue;
                                const bool step = (mw >> 12) == 0x6 && (mw & 0xF) >= 0x4 && (mw & 0xF) <= 0x6 && ((mw >> 4) & 0xF) == reg &&
                                                  ((mw >> 8) & 0xF) != reg;
                                // (A7-78: only in a body that stores something; one without a store holds the input nowhere in memory, and the load
                                // is the memory's own value, `mov.w @r6,r0` of a register: grammar 1 seed 3706930)
                                if (ShapeOf(sfe::Sh2Decode(m, mw)).load && !step)
                                {
                                    uint32_t la = 0;
                                    int ls = 0;
                                    if (!haveSources)
                                    {
                                        sources = BranchSources(r, word);
                                        haveSources = true;
                                    }
                                    const bool known = AccessAddress(r, word, sources, m, mw, false, la, ls, 0);
                                    if (bodyStores(known, la, ls, inputRegister)) storesInput = true;
                                }
                            }
                        }
                        if (!storesInput)
                        {
                            ++a.copyInputMerged;
                            continue;
                        }
                    }
                }
                // (A7-78: a copy drawn only at stores of the value the variable's own store stores -- `cRam26001045 = (char)pxVar2;` at the
                // volatile `mov.b r0,@r7` of the r0 that `mov.b r0,@(0xd,r7)` stores too: the same makers -- is the value materialised at
                // them, which names a store of it already; grammar 1 seed 3800581, the volatile request. Counted apart.)
                {
                    if (!haveSources)
                    {
                        sources = BranchSources(r, word);
                        haveSources = true;
                    }
                    bool any = false, all = true;
                    for (const Token& t : r.lines[li].tokens)
                    {
                        if (!t.addr || t.begin >= t.end) continue;
                        any = true;
                        uint16_t aw = 0;
                        int asize = 0;
                        const int areg = word(t.addr, aw) ? StoredRegister(aw, asize) : -1;
                        std::set<uint32_t> mine;
                        bool same = false;
                        if (areg >= 0 && RegisterMakers(r, word, sources, t.addr, areg, mine) && !mine.empty())
                            for (uint32_t st : candidates)
                            {
                                uint16_t sw = 0;
                                int ssz = 0;
                                const int sreg = word(st, sw) ? StoredRegister(sw, ssz) : -1;
                                std::set<uint32_t> theirs;
                                // (through the updates: the stored r0 is the drawn r0 less r5)
                                if (sreg >= 0 && RegisterMakers(r, word, sources, st, sreg, theirs, true) &&
                                    std::includes(theirs.begin(), theirs.end(), mine.begin(), mine.end()))
                                    same = true;
                            }
                        all = all && same;
                    }
                    if (any && all)
                    {
                        ++a.copyRegisterMade;
                        continue;
                    }
                }
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
                // (A7-78: a statement the product says names no instruction -- `iRamc0001004 = iRamc000103c;`, the engine's copy of a folded
                // read into the variable a later store names -- has no click to be wrong; A7-52 judges that it says so; grammar 2 seed 3622362)
                if (r.lines[li].noOrigin && !anyAddress)
                {
                    // (the value is a global the code reads, a read the engine folded)
                    const size_t eq = text.find(" = ");
                    size_t v = eq == std::string::npos ? eq : eq + 3;
                    if (v != std::string::npos && v < text.size() && text[v] == '(')
                    {
                        const size_t close = text.find(')', v);
                        v = close == std::string::npos ? close : close + 1;
                    }
                    if (v != std::string::npos && text.size() > v + 12 && text.back() == ';')
                    {
                        const std::string operand = text.substr(v, text.size() - 1 - v);
                        if (operand.size() >= 11 && operand.compare(operand.size() - 11, 3, "Ram") == 0 &&
                            operand.find_first_not_of("0123456789abcdef", operand.size() - 8) == std::string::npos)
                        {
                            const uint32_t va = static_cast<uint32_t>(std::strtoul(operand.c_str() + operand.size() - 8, nullptr, 16));
                            bool folded = false;
                            for (const AccessFate& f : r.accessFates)
                                folded = folded || (!f.write && !f.frame && f.fate == AccessFate::FoldedOrdinary && GlobalDelta(va, f.address) < f.size);
                            if (folded)
                            {
                                ++a.copyNoAddress;
                                continue;
                            }
                        }
                    }
                }
                // (A7-99: a number the engine trimmed to the bits the code uses -- `uRam06002380 = 0;` for the byte store of 2, the loop testing
                // `& 0x1000000`; `xStack_c = 0;` for the half-word store of 8 read back by its high byte -- is no store's value, and the line
                // says it names no instruction; grammar 1 seed 4502735, grammar 2 seed 4506709)
                // (A7-108: not when a store of the variable is known to store the number the line shows -- RegisterBefore gives it a number
                // whose bytes are the line's number's at the bytes the store writes, the variable as wide as the store reaches or a word,
                // big-endian: the line then has a store to name, and saying it names none hides it. The trimmed number of A7-99 is no
                // store's: the byte store of 2 is 0x2000000 in the word, not 0)
                bool storesShown = false;
                if (r.lines[li].noOrigin && !anyAddress && number)
                    for (uint32_t st : candidates)
                    {
                        uint16_t sw = 0;
                        int size = 0;
                        const int reg = word(st, sw) ? StoredRegister(sw, size) : -1;
                        const int shift = storeShift.count(st) ? storeShift[st] : -1;
                        // (a store that begins at the variable: `storeShift` is where in the store the variable begins)
                        if (reg < 0 || size <= 0 || size > 4 || shift != 0) continue;
                        if (!haveSources)
                        {
                            sources = BranchSources(r, word);
                            haveSources = true;
                        }
                        const WordValue got = RegisterBefore(r, word, sources, st, reg);
                        if (got.kind != WordValue::Number) continue;
                        // (a store another store of the variable overwrites in its block before anything reads the variable is no value the
                        // text can show: the 0 of `mov.b r3,@r6` before the poll's own store of 2, `uRam26001018 = 0;` the trimmed 2;
                        // grammar 1 seed 5204339)
                        bool overwritten = false;
                        for (const auto& range : r.flowBlockRanges)
                        {
                            if (st < range.first || st > range.second) continue;
                            for (uint32_t p = st + 2; p <= range.second && !overwritten; p += 2)
                            {
                                bool reads = false;
                                for (const AccessFate& f : r.accessFates)
                                    reads = reads || (f.insn == p && !f.write && f.frame == frame &&
                                                      (frame ? at - f.address : GlobalDelta(at, f.address)) < f.size);
                                if (reads) break;
                                if (candidates.count(p) && storeShift.count(p) && storeSize.count(p) && storeShift[p] >= 0 &&
                                    storeSize[p] - storeShift[p] >= size)
                                    overwritten = true;
                            }
                        }
                        if (overwritten) continue;
                        const uint64_t shownNumber = static_cast<uint32_t>(shownValue.value);
                        const uint64_t storedBytes = static_cast<uint64_t>(got.value) & ((1ull << (8 * size)) - 1);
                        for (int width : { shift + size, 4 })
                        {
                            if (width < 4 && (shownNumber >> (8 * width)) != 0) continue;   // (the number does not fit that width)
                            const uint64_t part = (shownNumber >> (8 * (width - shift - size))) & ((1ull << (8 * size)) - 1);
                            if (part == storedBytes) storesShown = true;
                        }
                    }
                if (r.lines[li].noOrigin && !anyAddress && number && !storesShown)
                {
                    ++a.copyNoAddress;
                    continue;
                }
                ++a.copyStoreMissing;
                ++(frame ? a.copyFrameMissing : a.copyGlobalMissing);
                // (A5-124: not a register's read the engine stores as read -- `xRam26001028 = SH2_RTCSR;`, 0090's merge of `mov.l @(0,r7),r2 ;
                // mov.l r2,@r6` -- whose store stores the register the read wrote: the line is that store's statement as much as the read's,
                // and a click on the store's instruction must reach it (grammar 1 seeds 5207177, 5207886). Switched off by
                // SE_A7_RULE_OFF=5124)
                bool readStored = false;
                if (atLoad && !atCall && !atReturn && !atBranch && !frame && !A7RuleOff(5124))
                    for (const Token& t : r.lines[li].tokens)
                    {
                        uint16_t lw = 0;
                        if (readStored || !t.addr || t.begin == t.end || !word(t.addr, lw)) continue;
                        bool registerRead = false;
                        for (const MemoryAccess& m : r.accesses) registerRead = registerRead || (m.insn == t.addr && !m.write && m.direct);
                        if (!registerRead) continue;
                        const std::set<int> loaded = WrittenRegisters(sfe::Sh2Decode(t.addr, lw));
                        for (uint32_t st : candidates)
                        {
                            uint16_t sw = 0;
                            int size = 0;
                            const int reg = word(st, sw) ? StoredRegister(sw, size) : -1;
                            if (reg < 0 || !loaded.count(reg)) continue;
                            if (!haveSources)
                            {
                                sources = BranchSources(r, word);
                                haveSources = true;
                            }
                            std::set<uint32_t> makers;
                            if (RegisterMakers(r, word, sources, st, reg, makers) && makers.count(t.addr)) readStored = true;
                        }
                    }
                ++(atCall ? a.copyAtCall : atReturn ? a.copyAtReturn : atBranch ? a.copyAtBranch : atLoad && !readStored ? a.copyAtLoad : a.copyAtOther);
                // (at a load it is counted: A7-16's shape, a global named after a hardware register is its load)
                const std::string what = "line " + std::to_string(li) + " '" + text + "' names none of the " + std::to_string(candidates.size()) +
                                         " store(s) folded into the variable (first " + MappingHex(*candidates.begin()) + "); a click goes to " + MappingHex(r.lines[li].firstAddr);
                if (atLoad && !atCall && !atReturn && !atBranch && !readStored) a.Keep("copy-store (counted): " + where + ": " + what);
                else problem("copy-store", what);
            }
    }
    // A7-22 (the area 7 fixer): a number line drawn at the first instruction of a join that does not make the number. When every path into
    // a block sets a register to one number, the engine makes the MULTIEQUAL a COPY with the block's address; a line that names that
    // instruction names one that neither makes nor computes its value. As tools/a7_round4/const_at_other.py --join counts them: a line
    // `V = <number>;` with one drawn instruction, a target of the function's own bt/bf/bt/s/bf/s/bra/bsr (the words from the entry to its
    // highest token), that is no `mov #imm`, literal load, store, call or branch, or is a `mov #imm` or literal load of another number; and
    // (unlike the script) no register move, which copies the value into the line's register.
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
            // Area 7 round 6 (A7-28): a number line of a memory variable, drawn at an instruction that makes the number (`mov #imm`, a
            // literal) beside the stores it names, at any instruction (not only a join's first): the instruction is the line's only when
            // it made the value one of those stores stores (RegisterMakers from the store, through register moves). `xRam0603ac00 = 4;`
            // drawn at the slot `mov #0x4,r3` of the `bra` while the store stores r0, which the engine knew was 4 by a compare; and the
            // join's `mov #0x0,r1` after the pushes of r1 that wrote the frame words (the rule A7-26 gave AddFrameStoreTokens asks only
            // which register, not whether its value reaches the store). A violation (A7-28, fixed by the area 7 fixer).
            if (word(at, w) && ((w >> 12) == 0xE || (w >> 12) == 0x9 || (w >> 12) == 0xD))
            {
                uint32_t got = 0;
                std::vector<std::pair<uint32_t, int>> stores;
                for (const Token& t : r.lines[li].tokens)
                {
                    uint16_t sw = 0;
                    int size = 0;
                    if (t.addr && t.begin == t.end && word(t.addr, sw) && StoredRegister(sw, size) >= 0) stores.emplace_back(t.addr, StoredRegister(sw, size));
                }
                if (!stores.empty() && made(at, w, got) && got == want)
                {
                    if (!haveSources)
                    {
                        sources = BranchSources(r, word);
                        haveSources = true;
                    }
                    bool known = true, maker = false;
                    for (const auto& st : stores)
                    {
                        std::set<uint32_t> makers;
                        if (!RegisterMakers(r, word, sources, st.first, st.second, makers, true)) known = false;
                        maker = maker || makers.count(at) != 0;
                    }
                    if (known && !maker)
                    {
                        ++a.numberStoreOther;
                        problem("number-store", "line " + std::to_string(li) + " '" + text + "' is drawn at " + MappingHex(at) +
                                                    ", which makes the number, but not the value any store the line names stores (A7-28)");
                    }
                }
            }
            if (!targets.count(at) || !word(at, w)) continue;
            const sfe::DisassembledInstruction d = sfe::Sh2Decode(at, w);
            const MemoryShape m = ShapeOf(d);
            const bool control = d.IsBranch || d.IsCall || d.Mnemonic == "rts" || d.Mnemonic == "rte";
            const bool numberInsn = (w >> 12) == 0xE || (w >> 12) == 0x9 || (w >> 12) == 0xD;
            // (a register move puts the value in the line's register: `bVar1 = true;` at `mov r10,r9`, r10 holding it, is that move's)
            const bool move = (w >> 12) == 0x6 && (w & 0xF) == 0x3;
            // (A7-34's triage: a read-modify-write of memory, `tas.b @r7` or `or.b #imm,@(r0,gbr)`, stores the variable as a `mov` does)
            const bool modifies = (w & 0xF0FF) == 0x401B || (w >> 8) == 0xCD || (w >> 8) == 0xCE || (w >> 8) == 0xCF;
            // (A7-106: `mac.l` and `mac.w` clamp the sum when the S bit is set: a bound of the clamp, `uVar3 = 0xffff800000000000;` at a
            // `mac.l` a `bt` joins, is the instruction's own number, made by its saturation; grammar 1 seed 5002388, specials)
            bool saturates = false;
            if ((w & 0xF00F) == 0x000F || (w & 0xF00F) == 0x400F)
            {
                const bool neg = value[0] == '-';
                uint64_t full = std::strtoull(value.c_str() + (neg ? 1 : 0), nullptr, 0);
                if (neg) full = 0 - full;
                saturates = (w & 0xF00F) == 0x000F ? full == 0x00007FFFFFFFFFFFull || full == 0xFFFF800000000000ull
                                                   : full == 0x7FFFFFFFull || full == 0x80000000ull || full == 0xFFFFFFFF80000000ull;
            }
            const bool makes = numberInsn || (d.Mnemonic.compare(0, 3, "mov") == 0 && m.store) || modifies || control || move || saturates;
            // A7-34 (the area 7 fixer): the number at the variable's width, its declaration's (`uVar4 = 0xffab;` at `mov #0xAB,r2`: the
            // sign-extended 0xffffffab of a two-byte variable)
            const size_t nameAt = text.find_first_not_of(' ');
            int width = DeclaredWidth(r, text.substr(nameAt, text.find(" = ", nameAt) - nameAt));
            // (A7-69: a global the text declares nowhere is as wide as the stores the line names, when every one is narrower than a word:
            // `uRam06002106 = 0xff9b;` for the half-word store of `mov #-0x65,r1`; grammar 2 seed 3218040)
            if (width >= 4)
            {
                int named = 0;
                for (const Token& t : r.lines[li].tokens)
                {
                    uint16_t sw = 0;
                    int size = 0;
                    if (t.addr && t.begin == t.end && word(t.addr, sw) && StoredRegister(sw, size) >= 0) named = std::max(named, size);
                }
                if (named > 0 && named < 4) width = named;
            }
            const uint32_t widthMask = width >= 4 ? 0xFFFFFFFFu : (1u << (8 * width)) - 1;
            const auto sameNumber = [&](uint32_t x) { return ((x ^ want) & widthMask) == 0; };
            // (A7-134: code the words' own T makes unreachable is judged by nothing: no path runs the instruction)
            const auto unreachable = [&]() {
                if (A7RuleOff(134)) return false;
                if (!haveSources)
                {
                    sources = BranchSources(r, word);
                    haveSources = true;
                }
                return !OracleReachable(r, word, sources, at);
            };
            // A7-34 (the area 7 fixer): an instruction that computes the number into the register it writes, from values the words give
            // (WrittenValue: `extu.w r3,r1` of 0xffe4, `shll2`, `add #imm`, `xor r1,r1`, a read of what a store before it wrote, as the
            // engine folds them), or into a control register (`ldc r1,gbr` for `unaff_gbr = 4`), or a status bit it sets (`div0u` for
            // `in_sr = 0`), makes it
            if (!makes)
            {
                if (!haveSources)
                {
                    sources = BranchSources(r, word);
                    haveSources = true;
                }
                bool computes = false;
                // A7-69: the only way into the instruction is a branch whose condition gives a register's value (`tst r4,r4 ; bt L`: r4 is 0
                // at L); grammar 2 seed 3217274
                EdgeFacts().clear();
                {
                    std::vector<uint32_t> preds;
                    sfe::DisassembledInstruction pv, pv2;
                    uint16_t pw = 0, p2w = 0;
                    if (r.InBody(at - 2) && word(at - 2, pw) && (pv = sfe::Sh2Decode(at - 2, pw)).IsValid &&
                        !(r.InBody(at - 4) && word(at - 4, p2w) && (pv2 = sfe::Sh2Decode(at - 4, p2w)).IsValid && pv2.HasDelaySlot && !pv2.IsConditional && !pv2.IsCall))
                        preds.push_back(at - 2);
                    const auto in = sources.find(at);
                    if (in != sources.end()) preds.insert(preds.end(), in->second.begin(), in->second.end());
                    if (preds.size() == 1 && !pv.HasDelaySlot)
                    {
                        const uint32_t pb = preds[0];
                        uint16_t bw = 0, cw = 0;
                        if (r.InBody(pb) && word(pb, bw) && r.InBody(pb - 2) && word(pb - 2, cw))
                        {
                            const bool fallsThrough = (bw >> 8) == 0x8B && pb + 2 == at, branches = (bw >> 8) == 0x89 && pb + 2 != at;
                            if ((fallsThrough || branches) && ((cw & 0xF0FF) == 0x4010 || ((cw & 0xF00F) == 0x2008 && ((cw >> 4) & 0xF) == ((cw >> 8) & 0xF))))
                                EdgeFacts().push_back(EdgeFact{ static_cast<int>((cw >> 8) & 0xF), 0, pb });
                            // (A7-78: `tst r4,r3 ; bt L` with r3 a copy of the input r4: both are 0 at L; grammar 2 seed 3629843)
                            else if ((fallsThrough || branches) && (cw & 0xF00F) == 0x2008)
                            {
                                const WordValue pn = RegisterBefore(r, word, sources, pb - 2, (cw >> 8) & 0xF), pm = RegisterBefore(r, word, sources, pb - 2, (cw >> 4) & 0xF);
                                if (pn.kind == WordValue::Param && pn == pm)
                                {
                                    EdgeFacts().push_back(EdgeFact{ static_cast<int>((cw >> 8) & 0xF), 0, pb });
                                    EdgeFacts().push_back(EdgeFact{ static_cast<int>((cw >> 4) & 0xF), 0, pb });
                                }
                            }
                        }
                    }
                }
                for (int reg : WrittenRegisters(d))
                {
                    const WordValue v = WrittenValue(r, word, sources, at, reg, 0);
                    computes = computes || (v.kind == WordValue::Number && sameNumber(static_cast<uint32_t>(v.value)));
                }
                EdgeFacts().clear();
                // (A7-131: on one edge into the join the instruction computes the number from the T the edge gives -- `tst r0,r0 ; bt L ;
                // ... ; L: rotcl r3`, the bt's edge with T set: 0x33 rotated left with T is 0x67 -- when the instruction reads T and a
                // `bt` or `bf` of its own is the edge; grammar 1 seeds 7202663 and 7205591)
                if (!computes && !A7RuleOff(131))
                {
                    std::vector<std::pair<uint32_t, int>> edges;
                    uint16_t pw = 0;
                    const auto in = sources.find(at);
                    if (in != sources.end())
                        for (uint32_t pb : in->second)
                            if (r.InBody(pb) && word(pb, pw) && ((pw >> 8) == 0x89 || (pw >> 8) == 0x8B) && sfe::Sh2Decode(pb, pw).BranchTarget == at)
                                edges.emplace_back(pb, (pw >> 8) == 0x89 ? 1 : 0);
                    // (A7-143: a `bt` or `bf` to the next instruction is both edges, the taken one and the fall-through: `cmp/pl r6 ; bt L ; L:
                    // movt r2` makes 0 on the fall-through; grammar 2 seed 7402123)
                    if (r.InBody(at - 2) && word(at - 2, pw) && ((pw >> 8) == 0x89 || (pw >> 8) == 0x8B) &&
                        (sfe::Sh2Decode(at - 2, pw).BranchTarget != at || !A7RuleOff(143)))
                        edges.emplace_back(at - 2, (pw >> 8) == 0x89 ? 0 : 1);
                    // (A7-143: the fall-through from straight code that leaves T as it was, `cmp/pl r6 ; bt L ; mov.l r2,@r5 ; extu.b r2,r2 ;
                    // L: movt r2`: T there is the T before that instruction, 0 on the `bt`'s fall-through)
                    else if (!A7RuleOff(143) && r.InBody(at - 2) && word(at - 2, pw) && !edges.empty())
                    {
                        const sfe::DisassembledInstruction pd = sfe::Sh2Decode(at - 2, pw);
                        uint16_t ppw = 0;
                        const bool inSlot = r.InBody(at - 4) && word(at - 4, ppw) && sfe::Sh2Decode(at - 4, ppw).HasDelaySlot;
                        if (pd.IsValid && !pd.IsBranch && !pd.IsCall && !pd.HasDelaySlot && !inSlot && pd.Mnemonic != "rte" && !SetsT(pw) &&
                            TSetBy(r, word, sources, at - 2, pw, 1) == -2)
                        {
                            const int t = TBefore(r, word, sources, at - 2, 1);
                            if (t >= 0) edges.emplace_back(at - 2, t);
                        }
                    }
                    for (const auto& edge : edges)
                    {
                        EdgeT().t = edge.second;
                        EdgeT().at = at;
                        for (int reg : WrittenRegisters(d))
                        {
                            const WordValue v = WrittenValue(r, word, sources, at, reg, 0);
                            computes = computes || (v.kind == WordValue::Number && sameNumber(static_cast<uint32_t>(v.value)));
                        }
                        EdgeT() = EdgeTruth();
                    }
                }
                if ((w >> 12) == 0x4 && ((w & 0xFF) == 0x0E || (w & 0xFF) == 0x1E || (w & 0xFF) == 0x2E || (w & 0xFF) == 0x0A || (w & 0xFF) == 0x1A ||
                                         (w & 0xFF) == 0x2A))
                {
                    const WordValue v = RegisterBefore(r, word, sources, at, (w >> 8) & 0xF);
                    computes = computes || (v.kind == WordValue::Number && sameNumber(static_cast<uint32_t>(v.value)));
                }
                if ((w == 0x0019 || w == 0x0008 || w == 0x0028) && want == 0) computes = true;
                // (an AND with a number whose bytes the variable keeps are 0: `and r3,r2` with r3 0x60000000 for a two-byte variable's 0)
                if (!computes && (want & widthMask) == 0 && (((w >> 12) == 0x2 && (w & 0xF) == 0x9) || (w >> 8) == 0xC9))
                {
                    const bool imm = (w >> 8) == 0xC9;
                    // (A7-78: the 0 of a four-byte variable the text uses only narrowed -- `(int2)iVar1`, `(char)iVar1` -- is the AND's with a
                    // number whose low half is 0, `and r3,r2` with r3 0x60000000; the bits above are not the text's. Grammar 1 seed 17737)
                    const uint32_t kept = widthMask == 0xFFFFFFFFu && want == 0 ? 0xFFFFu : widthMask;
                    for (int reg : imm ? std::vector<int>{} : std::vector<int>{ (w >> 8) & 0xF, (w >> 4) & 0xF })
                    {
                        const WordValue v = RegisterBefore(r, word, sources, at, reg);
                        computes = computes || (v.kind == WordValue::Number && (static_cast<uint32_t>(v.value) & kept) == 0);
                    }
                    if (imm && ((w & 0xFFu) & kept) == 0) computes = true;
                }
                // (A7-109: `or #imm,r0` and `and #imm,r0` at a join whose paths bring r0 different numbers that give one result: `or
                // #0xBD,r0` of 8 on one path and 5 on the other is 0xBD on both; every maker of r0 an instruction that makes a number, and
                // each gives the line's; grammar 1 seed 5208620)
                if (!computes && ((w >> 8) == 0xCB || (w >> 8) == 0xC9))
                {
                    std::set<uint32_t> makers;
                    bool all = RegisterMakers(r, word, sources, at, 0, makers) && !makers.empty() && !makers.count(0);
                    for (uint32_t m : makers)
                    {
                        uint16_t mw = 0;
                        uint32_t v = 0;
                        if (!all || !word(m, mw) || !made(m, mw, v)) { all = false; break; }
                        all = sameNumber((w >> 8) == 0xCB ? (v | (w & 0xFFu)) : (v & (w & 0xFFu)));
                    }
                    if (all) computes = true;
                }
                // (A7-139: `mov.b @Rk,Rn` or `mov.w @Rk,Rn` of the first bytes of the word `mov.l Rs,@Rk` stored before it, when every maker of Rs is
                // a `movt` (0 or 1): the high bytes are 0, whatever the T. Up to 10 instructions back, the conditional branches among them forward
                // to the load or short of it (code skipped, which is scanned as well), none writing Rk or Rs or storing; grammar 2 seed 7608136, `movt r3 ; mov.l r3,@r15 ; tst r3,r4 ; bt L ;
                // L: mov.b @r15,r1`)
                if (!computes && want == 0 && (w >> 12) == 0x6 && ((w & 0xF) == 0x0 || (w & 0xF) == 0x1) && !A7RuleOff(139))
                {
                    const int rk = static_cast<int>((w >> 4) & 0xF);
                    for (uint32_t q = at - 2, steps = 0; steps < 10 && r.InBody(q) && !computes; q -= 2, ++steps)
                    {
                        uint16_t qw = 0;
                        if (!word(q, qw)) break;
                        const sfe::DisassembledInstruction qd = sfe::Sh2Decode(q, qw);
                        if (!qd.IsValid || qd.IsCall || qd.HasDelaySlot) break;
                        const bool noopBranch = ((qw >> 8) == 0x89 || (qw >> 8) == 0x8B) && qd.BranchTarget > q && qd.BranchTarget <= at;
                        if (qd.IsBranch && !noopBranch) break;
                        if ((qw >> 12) == 0x2 && (qw & 0xF) == 0x2 && static_cast<int>((qw >> 8) & 0xF) == rk)
                        {
                            std::set<uint32_t> makersS;
                            const int rs = static_cast<int>((qw >> 4) & 0xF);
                            bool all = RegisterMakers(r, word, sources, q, rs, makersS) && !makersS.empty() && !makersS.count(0);
                            for (uint32_t mk : makersS)
                            {
                                uint16_t mw = 0;
                                all = all && word(mk, mw) && (mw & 0xF0FF) == 0x0029;
                            }
                            computes = all;
                            break;
                        }
                        int sz = 0;
                        if (StoredRegister(qw, sz) >= 0 || WrittenRegisters(qd).count(rk)) break;
                    }
                }
                // (A7-137: `sub Rm,Rn` of a register and the word a store of it wrote just before: `mov.l r0,@r6 ; mov.l @r6,r2 ; ... ; sub
                // r2,r0` is 0 whatever r0 is, the engine forwarding the stored word to the read. Rm's one maker is the `mov.l @Rk,Rm`, the
                // instruction before it on the straight line (up to 8) that stores is `mov.l Rn,@Rk`, none of them writes Rk or Rn, and Rn has
                // the same makers at the `sub` as at the store; grammar 1 seed 7600135)
                if (!computes && want == 0 && (w >> 12) == 0x3 && (w & 0xF) == 0x8 && !A7RuleOff(137))
                {
                    const int dn = static_cast<int>((w >> 8) & 0xF), sm = static_cast<int>((w >> 4) & 0xF);
                    std::set<uint32_t> makersM;
                    uint16_t lw = 0;
                    if (RegisterMakers(r, word, sources, at, sm, makersM) && makersM.size() == 1 && !makersM.count(0) && word(*makersM.begin(), lw) &&
                        (lw >> 12) == 0x6 && (lw & 0xF) == 0x2 && static_cast<int>((lw >> 8) & 0xF) == sm)
                    {
                        const uint32_t load = *makersM.begin();
                        const int k = static_cast<int>((lw >> 4) & 0xF);
                        for (uint32_t q = load - 2, steps = 0; steps < 8 && r.InBody(q) && !computes; q -= 2, ++steps)
                        {
                            uint16_t qw = 0;
                            if (!word(q, qw) || targets.count(q + 2) || sources.count(q + 2)) break;
                            const sfe::DisassembledInstruction qd = sfe::Sh2Decode(q, qw);
                            if (!qd.IsValid || qd.IsBranch || qd.IsCall || qd.HasDelaySlot) break;
                            if (qw == static_cast<uint16_t>(0x2002u | (static_cast<unsigned>(k) << 8) | (static_cast<unsigned>(dn) << 4)))
                            {
                                std::set<uint32_t> before, now;
                                computes = RegisterMakers(r, word, sources, q, dn, before) && RegisterMakers(r, word, sources, at, dn, now) && before == now;
                                break;
                            }
                            int sz = 0;
                            if (StoredRegister(qw, sz) >= 0 || WrittenRegisters(qd).count(k) || WrittenRegisters(qd).count(dn)) break;
                        }
                    }
                }
                // (A7-130: `and Rm,Rn` in a loop whose Rn is the mask a literal gave, `and`ed again each pass: `mov.l @(..),r1` of
                // 0x20000000, then `and r5,r1` at the loop's head, r1 the literal's on the way in and the `and`'s own on the way round: every
                // maker of a register is a number with the kept bits 0, or an `and` of such a register (a cycle is the loop's own `and`);
                // the engine prints the 0 where the text uses the low bits only. Grammar 1 seed 6900150)
                if (!computes && want == 0 && (w >> 12) == 0x2 && (w & 0xF) == 0x9 && !A7RuleOff(130))
                {
                    const uint32_t kept = widthMask == 0xFFFFFFFFu ? 0xFFFFu : widthMask;
                    std::set<std::pair<uint32_t, int>> active;
                    std::function<bool(uint32_t, int, int)> zeroKept = [&](uint32_t before, int reg, int depth) -> bool {
                        std::set<uint32_t> makers;
                        if (depth > 6 || !RegisterMakers(r, word, sources, before, reg, makers) || makers.empty() || makers.count(0)) return false;
                        if (!active.insert(std::make_pair(before, reg)).second) return true;
                        bool all = true;
                        for (uint32_t m : makers)
                        {
                            uint16_t mw = 0;
                            uint32_t v = 0;
                            if (!word(m, mw)) { all = false; break; }
                            if (made(m, mw, v)) all = (v & kept) == 0;
                            else if ((mw >> 12) == 0x2 && (mw & 0xF) == 0x9 && static_cast<int>((mw >> 8) & 0xF) == reg)
                                all = zeroKept(m, static_cast<int>((mw >> 8) & 0xF), depth + 1) || zeroKept(m, static_cast<int>((mw >> 4) & 0xF), depth + 1);
                            else all = false;
                            if (!all) break;
                        }
                        active.erase(std::make_pair(before, reg));
                        return all;
                    };
                    if (zeroKept(at, static_cast<int>((w >> 8) & 0xF), 0) || zeroKept(at, static_cast<int>((w >> 4) & 0xF), 0)) computes = true;
                }
                // (A7-110: `and Rm,Rn` of a number and a register whose low bits left shifts made 0, the number inside those bits: `and
                // r2,r0` with r0 the 2 a poll reads back and r2 shifted by `shll2` is 0; grammar 1 seed 5207679)
                if (!computes && want == 0 && (w >> 12) == 0x2 && (w & 0xF) == 0x9)
                {
                    std::function<int(uint32_t, int, int)> lowZeros = [&](uint32_t before, int reg, int depth) -> int {
                        std::set<uint32_t> makers;
                        if (depth > 4 || !RegisterMakers(r, word, sources, before, reg, makers) || makers.empty() || makers.count(0)) return 0;
                        int least = 32;
                        for (uint32_t m : makers)
                        {
                            uint16_t mw = 0;
                            uint32_t v = 0;
                            int k = 0;
                            if (!word(m, mw)) return 0;
                            if ((mw >> 12) == 0x4 && ((mw >> 8) & 0xF) == static_cast<uint16_t>(reg) &&
                                ((mw & 0xFF) == 0x00 || (mw & 0xFF) == 0x20 || (mw & 0xFF) == 0x08 || (mw & 0xFF) == 0x18 || (mw & 0xFF) == 0x28))
                            {
                                k = (mw & 0xFF) == 0x28 ? 16 : (mw & 0xFF) == 0x18 ? 8 : (mw & 0xFF) == 0x08 ? 2 : 1;
                                k = std::min(32, k + lowZeros(m, reg, depth + 1));
                            }
                            else if (made(m, mw, v))
                                for (k = 0; k < 32 && !(v >> k & 1); ++k) {}
                            least = std::min(least, k);
                        }
                        return least;
                    };
                    const int rn = (w >> 8) & 0xF, rm = (w >> 4) & 0xF;
                    for (int pass = 0; pass < 2 && !computes; ++pass)
                    {
                        const int numbered = pass ? rm : rn, shifted = pass ? rn : rm;
                        const WordValue x = RegisterBefore(r, word, sources, at, numbered);
                        if (x.kind != WordValue::Number) continue;
                        const int z = lowZeros(at, shifted, 0);
                        if (z > 0 && (z >= 32 || (static_cast<uint32_t>(x.value) >> z) == 0)) computes = true;
                    }
                }
                if (w == 0x0018 && want == 1) computes = true;
                // (A7-69: `xtrct Rm,Rn` is Rm's low half over Rn's high half; the engine prints the part that is a number -- Rm shifted
                // left by 16 -- as a variable's value and the other part as another; grammar 2 seed 3204314, `in_r2 = 0x40000;` for r1 = 4)
                if (!computes && (w & 0xF00F) == 0x200D)
                {
                    const WordValue v = RegisterBefore(r, word, sources, at, (w >> 4) & 0xF);
                    computes = v.kind == WordValue::Number && sameNumber(static_cast<uint32_t>(v.value) << 16);
                    // (A7-140: both registers numbers, the whole of the result: r0 = 3 and r2 = 0x20000000 give `iVar1 = 0x32000;`; grammar 1
                    // seed 7702852, specials)
                    const WordValue rn = RegisterBefore(r, word, sources, at, (w >> 8) & 0xF);
                    if (!computes && !A7RuleOff(140) && v.kind == WordValue::Number && rn.kind == WordValue::Number)
                        computes = sameNumber(static_cast<uint32_t>(v.value) << 16 | static_cast<uint32_t>(rn.value) >> 16);
                }
                // (A7-78: `xtrct r1,r1` of a register that is 0 -- a `dt` loop's counter, then kept 0 by the loop's own `extu.w r1,r1` and the
                // `xtrct` itself, a cycle RegisterBefore cannot close: every maker a `dt`, a `mov #0` or an extension, a
                // rotation or a shift of the register; grammar 2 seed 4000243)
                if (!computes && want == 0 && (w & 0xF00F) == 0x200D && ((w >> 8) & 0xF) == ((w >> 4) & 0xF))
                {
                    std::set<uint32_t> makers;
                    bool zero = RegisterMakers(r, word, sources, at, (w >> 8) & 0xF, makers) && !makers.empty() && !makers.count(0);
                    for (uint32_t m : makers)
                    {
                        uint16_t mw = 0;
                        if (!zero || !word(m, mw)) { zero = false; break; }
                        const bool dt = (mw & 0xF0FF) == 0x4010, zeroMove = (mw >> 12) == 0xE && (mw & 0xFF) == 0, ext = (mw & 0xF00F) == 0x600C || (mw & 0xF00F) == 0x600D ||
                                        (mw & 0xF00F) == 0x600E || (mw & 0xF00F) == 0x600F, rot = (mw & 0xF00F) == 0x200D && ((mw >> 8) & 0xF) == ((mw >> 4) & 0xF),
                                        shift = (mw >> 12) == 0x4 && ((mw & 0xFF) == 0x00 || (mw & 0xFF) == 0x08 || (mw & 0xFF) == 0x18 || (mw & 0xFF) == 0x28 || (mw & 0xFF) == 0x01 ||
                                                                      (mw & 0xFF) == 0x09 || (mw & 0xFF) == 0x19 || (mw & 0xFF) == 0x29);
                        zero = dt || zeroMove || ext || rot || shift;
                    }
                    if (zero) computes = true;
                }
                // (A7-69: the T a compare of registers the words give sets, `cmp/pl r3` of a literal: `bVar1 = true;`; grammar 2 seed 2707916)
                if (!computes)
                {
                    const int ts = TSetBy(r, word, sources, at, w, 0);
                    if (ts >= 0 && sameNumber(static_cast<uint32_t>(ts))) computes = true;
                }
                // (an extension of a register only shifts left made: `extu.b r1,r2` after `shll16 r1` is 0, the A7-43 pair apart, grammar
                // 1 seed 2406142, this round's fresh run)
                // (the low bits the left shifts that made it, on every path, leave 0: `shll16` then, on one path, `shll2`, 16 or 18)
                if (!computes && want == 0 && ((w & 0xF00F) == 0x600C || (w & 0xF00F) == 0x600D))
                {
                    std::function<int(uint32_t, int, int)> zeroBits = [&](uint32_t before, int reg, int depth) -> int {
                        std::set<uint32_t> makers;
                        if (depth > 4 || !RegisterMakers(r, word, sources, before, reg, makers) || makers.empty() || makers.count(0)) return 0;
                        int least = 64;
                        for (uint32_t m : makers)
                        {
                            uint16_t mw = 0;
                            int k = 0;
                            if (word(m, mw) && (mw >> 12) == 0x4 && ((mw >> 8) & 0xF) == reg)
                                k = (mw & 0xFF) == 0x28 ? 16 : (mw & 0xFF) == 0x18 ? 8 : (mw & 0xFF) == 0x08 ? 2 : ((mw & 0xFF) == 0x00 || (mw & 0xFF) == 0x20) ? 1 : 0;
                            least = std::min(least, k ? k + zeroBits(m, reg, depth + 1) : 0);
                        }
                        return least;
                    };
                    if (zeroBits(at, (w >> 4) & 0xF, 0) >= ((w & 0xF) == 0xC ? 8 : 16)) computes = true;
                }
                // (A7-58: a test or a compare sets the T bit from registers whose values the words give -- `tst r4,r3` with r3 0 after a
                // `dt` loop is 1, `uVar2 = 1;` the bit `negc` reads -- as `sett` does)
                if (!computes)
                {
                    const int rn = (w >> 8) & 0xF, rm = (w >> 4) & 0xF;
                    const auto known = [&](int reg, uint32_t& v) {
                        const WordValue x = RegisterBefore(r, word, sources, at, reg);
                        if (x.kind != WordValue::Number) return false;
                        v = static_cast<uint32_t>(x.value);
                        return true;
                    };
                    uint32_t a1 = 0, a2 = 0;
                    int t = -1;
                    if ((w >> 12) == 0x2 && (w & 0xF) == 0x8)   // tst Rm,Rn
                    {
                        const bool k1 = known(rn, a1), k2 = known(rm, a2);
                        if ((k1 && a1 == 0) || (k2 && a2 == 0)) t = 1;
                        else if (k1 && k2) t = (a1 & a2) == 0 ? 1 : 0;
                    }
                    else if ((w >> 8) == 0xC8 && known(0, a1)) t = (a1 & (w & 0xFFu)) == 0 ? 1 : 0;   // tst #imm,r0
                    else if ((w >> 12) == 0x3 && (w & 0xF) == 0x0 && known(rn, a1) && known(rm, a2)) t = a1 == a2 ? 1 : 0;   // cmp/eq Rm,Rn
                    else if ((w >> 8) == 0x88 && known(0, a1))   // cmp/eq #imm,r0
                        t = a1 == static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(w & 0xFF))) ? 1 : 0;
                    // (a shift or a rotate puts the bit it shifts out in T: `rotcl r3` with r3 0 after a `dt` loop is 0, grammar 2 seed
                    // 2502184 routed from the area 2 round 20 reviewer)
                    else if ((w >> 12) == 0x4 && ((w & 0xFF) == 0x00 || (w & 0xFF) == 0x04 || (w & 0xFF) == 0x20 || (w & 0xFF) == 0x24) && known(rn, a1))
                        t = static_cast<int>(a1 >> 31);   // shll, rotl, shal, rotcl
                    else if ((w >> 12) == 0x4 && ((w & 0xFF) == 0x01 || (w & 0xFF) == 0x05 || (w & 0xFF) == 0x21 || (w & 0xFF) == 0x25) && known(rn, a1))
                        t = static_cast<int>(a1 & 1);     // shlr, rotr, shar, rotcr
                    if (t >= 0 && sameNumber(static_cast<uint32_t>(t))) computes = true;
                }
                // (A7-58: `shll8 Rn` leaves Rn's low byte 0, `shll16` its low half: the engine prints the shifted register as 0 where it uses
                // only those bits -- `iVar1 = 0;` stored by `mov.b r0,@(0xd,r4)` -- when the line's variable is Rn where the text uses it.
                // `shlr8`/`shlr16` shift a narrow value out: `uVar1 = 0;` at the `shlr8 r0` after `extu.b r5,r0`, grammar 1 seed 2402555)
                // (A7-78: `shll`, `shal` and `shll2` leave the low bit or two 0: `in_r2 = 0;` at the `shll2 r0` whose r0 the text tests with
                // `tst #2,r0`, printed `(in_r2 & 2) != 0`; grammar 2 seed 3628408 with the specials)
                if (!computes && want == 0 && ((w & 0xF0FF) == 0x4018 || (w & 0xF0FF) == 0x4028 || (w & 0xF0FF) == 0x4019 || (w & 0xF0FF) == 0x4029 ||
                                               (w & 0xF0FF) == 0x4008 || (w & 0xF0FF) == 0x4000 || (w & 0xF0FF) == 0x4020))
                {
                    const size_t nameBegin = text.find_first_not_of(' ');
                    const std::string name = text.substr(nameBegin, text.find(" = ", nameBegin) - nameBegin);
                    const int rn = (w >> 8) & 0xF;
                    for (const Line& l : r.lines)
                        for (const Token& t : l.tokens)
                        {
                            uint16_t tw = 0;
                            if (computes || !t.addr || t.addr == at || t.begin >= t.end || t.end > l.text.size() ||
                                l.text.compare(t.begin, t.end - t.begin, name) != 0 || !word(t.addr, tw))
                                continue;
                            const std::string ops = sfe::Sh2Decode(t.addr, tw).Operands;
                            for (size_t k = 0; k + 1 < ops.size(); ++k)
                                if (ops[k] == 'r' && std::isdigit(static_cast<unsigned char>(ops[k + 1])) &&
                                    (k == 0 || !std::isalnum(static_cast<unsigned char>(ops[k - 1]))) && std::atoi(ops.c_str() + k + 1) == rn)
                                    computes = true;
                        }
                }
                // (A7-64: an extension whose register the text uses only shifted right past the extended bits -- `uVar1 = 0;` at `extu.b
                // r3,r1`, then `shlr16 r1` -- is the 0 the engine prints: the line's variable is the extended register where the
                // shift's instruction names it. Grammar 2 seed 2604457)
                if (!computes && want == 0 && ((w & 0xF00F) == 0x600C || (w & 0xF00F) == 0x600D))
                {
                    const size_t nameBegin = text.find_first_not_of(' ');
                    const std::string name = text.substr(nameBegin, text.find(" = ", nameBegin) - nameBegin);
                    const unsigned rn = (w >> 8) & 0xF;
                    for (const Line& l : r.lines)
                        for (const Token& t : l.tokens)
                        {
                            uint16_t tw = 0;
                            if (computes || !t.addr || t.addr == at || t.begin >= t.end || t.end > l.text.size() ||
                                l.text.compare(t.begin, t.end - t.begin, name) != 0 || !word(t.addr, tw))
                                continue;
                            if (tw == (0x4029u | rn << 8) || ((w & 0xF) == 0xC && tw == (0x4019u | rn << 8))) computes = true;
                            // (A7-111: `xtrct Rm,Rn` keeps Rn's high half only, which the extension left 0: `in_r1 = 0;` at `extu.w r5,r1`
                            // with `xtrct r5,r1` after it, printed `in_r1 >> 0x10`; grammar 2 seed 5204684)
                            if ((tw & 0xFF0Fu) == (0x200Du | rn << 8) && ((tw >> 4) & 0xF) != rn) computes = true;
                        }
                }
                if (computes) continue;
            }
            uint32_t got = 0;
            if (makes && !(numberInsn && made(at, w, got) && !sameNumber(got)))
            {
                // A7-26 (the area 7 fixer): an instruction that makes the number makes it into its register, Rm. When the engine merged
                // the paths' values, every path into the join sets the line's register to the number (RegisterBefore, the walk
                // storeValueOther uses). When Rm is not such a register and another, Rx, is, and the line's variable is Rx's where the
                // text uses it elsewhere (its tokens' instructions name Rx and never Rm), the instruction made another register's number
                // (`mov #0,r12` at the join of the paths that set r8 to 0). An `else { iVar4 = 0; }` whose `mov #0,r7` is the line's own,
                // while r6 happens to be 0 on the paths in, uses r7 elsewhere: not judged. Nor when nothing is known.
                if (!numberInsn) continue;
                if (!haveSources)
                {
                    sources = BranchSources(r, word);
                    haveSources = true;
                }
                const int rm = (w >> 8) & 0xF;
                // (a memory variable's line that names its stores: the number is the stores' when one stores Rm; when each stores another
                // register, the instruction made another value, `mov #0x0,r5` for an argument beside the pushes of r1 that write
                // `xStack_8 = 0;`)
                {
                    std::set<int> storeRegs;
                    for (const Token& t : r.lines[li].tokens)
                    {
                        uint16_t sw = 0;
                        int size = 0;
                        if (t.addr && t.begin == t.end && word(t.addr, sw) && StoredRegister(sw, size) >= 0) storeRegs.insert(StoredRegister(sw, size));
                    }
                    if (!storeRegs.empty())
                    {
                        if (storeRegs.count(rm)) continue;
                        if (unreachable()) continue;
                        ++a.constAtJoin;
                        problem("const-join", "line " + std::to_string(li) + " '" + text + "' is drawn at " + MappingHex(at) + " (" + d.Mnemonic + " " +
                                                  d.Operands + "), the first instruction of a join, which makes the number into r" + std::to_string(rm) +
                                                  "; the stores the line names store r" + std::to_string(*storeRegs.begin()));
                        continue;
                    }
                }
                const auto holds = [&](int reg) {
                    const WordValue v = RegisterBefore(r, word, sources, at, reg);
                    return v.kind == WordValue::Number && static_cast<uint32_t>(v.value) == want;
                };
                if (holds(rm)) continue;
                // the registers the instructions name where the text uses the line's variable elsewhere
                const size_t nameBegin = text.find_first_not_of(' ');
                const size_t nameEnd = text.find(" = ", nameBegin);
                const std::string name = text.substr(nameBegin, nameEnd - nameBegin);
                std::set<int> used;
                // (A7-91: only a line that is a bare copy, `uVar4 = (uint2)uVar3;` -- `iVar1 = iVar1 + -1;` at the `dt r3` that begins
                // a loop's tail is the instruction's own; grammar 1 seed 4106897)
                const auto bareCopy = [](const std::string& line) {
                    const size_t eq = line.find(" = ");
                    if (eq == std::string::npos || line.empty() || line.back() != ';') return false;
                    size_t q = eq + 3;
                    while (q < line.size() && line[q] == '(')
                    {
                        const size_t close = line.find(')', q);
                        if (close == std::string::npos) return false;
                        q = close + 1;
                    }
                    if (q + 1 >= line.size()) return false;
                    for (size_t k = q; k + 1 < line.size(); ++k)
                        if (!std::isalnum(static_cast<unsigned char>(line[k])) && line[k] != '_') return false;
                    return true;
                };
                for (const Line& l : r.lines)
                    for (const Token& t : l.tokens)
                    {
                        uint16_t tw = 0;
                        // (A7-78: a token at another join's first instruction names no register: the engine keeps a merge's copy,
                        // `uVar4 = uVar3;`, at the address of the block's first instruction, a `mov.b @(0xe,r5),r0` that neither reads nor
                        // writes r3; grammar 1 seed 3302704)
                        if (!t.addr || t.addr == at || (targets.count(t.addr) && bareCopy(l.text)) || t.begin >= t.end || t.end > l.text.size() ||
                            l.text.compare(t.begin, t.end - t.begin, name) != 0 || !word(t.addr, tw))
                            continue;
                        const std::string ops = sfe::Sh2Decode(t.addr, tw).Operands;
                        for (size_t k = 0; k + 1 < ops.size(); ++k)
                            if (ops[k] == 'r' && std::isdigit(static_cast<unsigned char>(ops[k + 1])) &&
                                (k == 0 || !std::isalnum(static_cast<unsigned char>(ops[k - 1]))))
                                used.insert(std::atoi(ops.c_str() + k + 1));
                    }
                if (used.count(rm)) continue;
                int other = -1;
                for (int reg = 0; reg < 15 && other < 0; ++reg)
                    if (reg != rm && used.count(reg) && holds(reg)) other = reg;
                if (other < 0) continue;
                if (unreachable()) continue;
                ++a.constAtJoin;
                problem("const-join", "line " + std::to_string(li) + " '" + text + "' is drawn at " + MappingHex(at) + " (" + d.Mnemonic + " " + d.Operands +
                                          "), the first instruction of a join, which makes the number into r" + std::to_string(rm) +
                                          "; the paths into the join set it in r" + std::to_string(other));
                continue;
            }
            // (A7-58: a `mov #imm,r0` of another number where the code then uses only bits of r0 on which the two agree -- `mov #0x1c,r0`
            // and `tst #0x40,r0`, shown as `uStack_8 = 0;` -- when the line's variable is r0 where the text uses it: grammar 2 seed 2500642
            // routed from the area 2 round 20 reviewer)
            if (numberInsn && (w >> 12) == 0xE && ((w >> 8) & 0xF) == 0 && made(at, w, got))
            {
                bool agree = false;
                for (uint32_t p = r.entry; p <= highest && highest - r.entry < 0x100000u && !agree; p += 2)
                {
                    uint16_t tw = 0;
                    if (word(p, tw) && ((tw >> 8) == 0xC8 || (tw >> 8) == 0xC9) && ((got ^ want) & (tw & 0xFFu)) == 0) agree = true;
                }
                const size_t nameBegin = text.find_first_not_of(' ');
                const std::string name = text.substr(nameBegin, text.find(" = ", nameBegin) - nameBegin);
                bool usesR0 = false;
                for (const Line& l : r.lines)
                    for (const Token& t : l.tokens)
                    {
                        uint16_t tw = 0;
                        if (usesR0 || !agree || !t.addr || t.addr == at || t.begin >= t.end || t.end > l.text.size() ||
                            l.text.compare(t.begin, t.end - t.begin, name) != 0 || !word(t.addr, tw))
                            continue;
                        const std::string ops = sfe::Sh2Decode(t.addr, tw).Operands;
                        for (size_t k = 0; k + 1 < ops.size(); ++k)
                            if (ops[k] == 'r' && ops[k + 1] == '0' && (k + 2 >= ops.size() || !std::isdigit(static_cast<unsigned char>(ops[k + 2]))) &&
                                (k == 0 || !std::isalnum(static_cast<unsigned char>(ops[k - 1]))))
                                usesR0 = true;
                    }
                if (agree && usesR0) continue;
            }
            if (unreachable()) continue;
            ++a.constAtJoin;
            problem("const-join", "line " + std::to_string(li) + " '" + text + "' is drawn at " + MappingHex(at) + " (" + d.Mnemonic + " " + d.Operands +
                                      "), the first instruction of a join, which does not make the number");
        }
    }
    // Area 7 round 6 (A7-29): a case label against its own switch's table. The label's switch is the `switch` line at the label's indent
    // before it (the text's nesting; A7-27's caseDestination takes the switch printed last, which a nested switch is). For a table of the
    // common form (`mova @(T),r0 ; mov.w/mov.b @(r0,Rm),Rn ; add Rn,r0 ; jmp @r0`, entries relative to T), the label's value maps to an
    // entry with an offset (a bias before the bound check), the one most of the switch's labels agree with; a label naming another entry,
    // or no destination of the table, is a violation (A7-29, fixed by the area 7 fixer). tools/a7_round6/case_switch.py is the same over a token dump.
    {
        const auto indentOf = [](const std::string& t) { return t.find_first_not_of(' '); };
        const auto parseCase = [](const std::string& t, int64_t& v) {
            const size_t q = t.find_first_not_of(' ');
            if (q == std::string::npos || t.compare(q, 5, "case ") != 0 || t.empty() || t.back() != ':') return false;
            const std::string num = t.substr(q + 5, t.size() - 1 - (q + 5));
            if (num.empty() || num.find_first_not_of("-0123456789abcdefx") != std::string::npos) return false;
            char* end = nullptr;
            const bool neg = num[0] == '-';
            const unsigned long long u = std::strtoull(num.c_str() + (neg ? 1 : 0), &end, 0);
            if (!end || *end) return false;
            v = neg ? -static_cast<int64_t>(u) : static_cast<int64_t>(u);
            if (!neg && v >= (int64_t(1) << 31) && v < (int64_t(1) << 32)) v -= int64_t(1) << 32;   // (an unsigned label of a negative case)
            return true;
        };
        const auto tableOf = [&](uint32_t jmp, uint32_t& T, int& size) {
            uint16_t w = 0;
            if (!word(jmp, w) || w != 0x402B) return false;
            size = 0;
            for (uint32_t k = 1; k < 8; ++k)
            {
                uint16_t x = 0;
                if (!word(jmp - 2 * k, x)) return false;
                if ((x & 0xF00F) == 0x000D) size = 2;
                if ((x & 0xF00F) == 0x000C) size = 1;
                if ((x >> 8) == 0xC7)
                {
                    T = ((jmp - 2 * k) & ~3u) + 4 + (x & 0xFFu) * 4;
                    return size != 0;
                }
            }
            return false;
        };
        const auto entryOf = [&](uint32_t T, int size, int64_t i, uint32_t& dest) {
            if (i < 0 || i >= 256) return false;
            uint16_t x = 0;
            if (size == 2)
            {
                if (!word(T + 2 * static_cast<uint32_t>(i), x)) return false;
                dest = T + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(x)));
                return true;
            }
            const uint32_t b = T + static_cast<uint32_t>(i);
            if (!word(b & ~1u, x)) return false;
            const uint8_t byte = (b & 1) ? static_cast<uint8_t>(x & 0xFF) : static_cast<uint8_t>(x >> 8);
            dest = T + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(byte)));
            return true;
        };
        std::map<size_t, std::vector<std::pair<int64_t, uint32_t>>> bySwitch;   // switch line -> (value, the label's address)
        for (size_t li = 0; li < r.lines.size(); ++li)
        {
            int64_t v = 0;
            if (!parseCase(r.lines[li].text, v)) continue;
            const size_t ind = indentOf(r.lines[li].text);
            size_t sw = r.lines.size();
            for (size_t lj = li; lj-- > 0;)
            {
                const std::string& t = r.lines[lj].text;
                if (indentOf(t) == ind && t.compare(ind, 7, "switch(") == 0)
                {
                    sw = lj;
                    break;
                }
            }
            std::set<uint32_t> at;
            for (const Token& t : r.lines[li].tokens)
                if (t.addr) at.insert(t.addr);
            if (sw == r.lines.size() || at.size() != 1) continue;
            bySwitch[sw].emplace_back(v, *at.begin());
        }
        for (const auto& kv : bySwitch)
        {
            std::set<uint32_t> jmps;
            const Line& sl = r.lines[kv.first];
            for (const Token& t : sl.tokens)
                if (t.addr && t.end <= sl.text.size() && sl.text.compare(t.begin, t.end - t.begin, "switch") == 0) jmps.insert(t.addr);
            uint32_t T = 0;
            int size = 0;
            if (jmps.size() != 1 || !tableOf(*jmps.begin(), T, size)) continue;
            // (the value a label prints, read at the switch's width: `case 0xfffa:` of a 16-bit switch is -6)
            const auto names = [&](int64_t v, int64_t k, uint32_t at) {
                uint32_t dest = 0;
                if (entryOf(T, size, v - k, dest) && dest == at) return true;
                if (v >= 0x8000 && v < 0x10000 && entryOf(T, size, v - 0x10000 - k, dest) && dest == at) return true;
                return v >= 0x80 && v < 0x100 && entryOf(T, size, v - 0x100 - k, dest) && dest == at;
            };
            std::map<int64_t, int> votes;
            for (const auto& label : kv.second)
                for (int64_t k = -64; k <= 64; ++k)
                    if (names(label.first, k, label.second)) ++votes[k];
            bool haveK = false;
            int64_t k = 0;
            int best = 0;
            for (const auto& vk : votes)
                if (vk.second > best) best = vk.second, k = vk.first, haveK = true;
            for (const auto& label : kv.second)
            {
                ++a.caseJudged;
                uint32_t dest = 0;
                if (haveK && names(label.first, k, label.second)) continue;
                ++a.caseOtherEntry;
                problem("case-entry", "the label 'case " + std::to_string(label.first) + ":' of the switch on line " + std::to_string(kv.first) + " names " +
                                          MappingHex(label.second) + (haveK && entryOf(T, size, label.first - k, dest) ? ", its table's entry is " + MappingHex(dest)
                                                                                                                      : std::string(", no entry of its table")) +
                                          " (A7-29)");
            }
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
                        // (or the engine says it made the call a branch: `Possible PIC construction at 0x...: Changing call to branch`, and
                        // the text has no call there to name)
                        const size_t pic = w.find("Possible PIC construction at 0x");
                        if (pic != std::string::npos && w.find("Changing call to branch") != std::string::npos &&
                            static_cast<uint32_t>(std::strtoul(w.c_str() + pic + 31, nullptr, 16)) == p)
                            said = true;
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
// an address (line, begin, end, kind, address, in body, previous instruction has a delay slot, the instruction, the token's text, and
// (the area 7 fixer, A7-25) whether it is a copy materialised there).
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
            std::fprintf(out, "T\t%zu\t%zu\t%u\t%u\t%d\t%08X\t%d\t%d\t%s\t%s\t%d\n", li, ti, unsigned(t.begin), unsigned(t.end), int(t.kind), t.addr,
                         r.InBody(t.addr) ? 1 : 0, slot ? 1 : 0, insn.c_str(), text.c_str(), t.materialised ? 1 : 0);
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
    std::printf("  mapping (%s): register merges under a memory variable's name naming no store (A7-21): %d drawn only at a branch ending the block (a "
                "violation), %d naming the instructions that set the register (counted), %d naming nothing (counted); %d number lines drawn at "
                "their makers that no store's value is made by (A7-45, counted)\n",
                label, a.copyMergeAtBranch, a.copyRegisterMade, a.copyNoAddress, a.copyNumberMade);
    std::printf("  mapping (%s): area 7 round 6: %d number lines of a memory variable drawn at a number instruction whose value no store they name "
                "stores (A7-28, a violation); %d of %d case labels judged against their own switch's table name another entry or none (A7-29, a violation)\n",
                label, a.numberStoreOther, a.caseOtherEntry, a.caseJudged);
    std::printf("  mapping (%s): area 7 round 7: %d copies drawn at a call or a return name a store that cannot run before it (A7-32, a violation)\n",
                label, a.storeAfterUse);
    std::printf("  mapping (%s): area 7 round 8 (A7-52): %d of the lines with no instruction are statements the product says so of (counted); %d statements with none that do not say so, or say so with one (a violation)\n",
                label, a.noOriginLines, a.noInstruction);
    std::printf("  mapping (%s): area 7 round 8 (A7-55): %d copies of the function's input drawn only at the moves that put it in a register, or "
                "naming none and saying so, that no store of the variable stores (counted)\n",
                label, a.copyInputMerged);
    for (const std::string& p : a.problems) std::printf("    %s\n", p.c_str());
}

}  // namespace dts
