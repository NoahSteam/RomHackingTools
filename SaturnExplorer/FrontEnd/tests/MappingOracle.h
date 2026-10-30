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
        for (const std::string& p : o.problems) Keep(p);
    }
    // Three examples of each kind of problem are kept; the prefix up to the first ':' is the kind.
    void Keep(const std::string& p)
    {
        const std::string kind = p.substr(0, p.find(':'));
        int same = 0;
        for (const std::string& q : problems)
            if (q.compare(0, kind.size() + 1, kind + ":") == 0) ++same;
        if (same < 3) problems.push_back(p);
    }
    int Violations() const
    {
        return odd + outside + notInsn + firstAddrWrong + badRange + callWithoutCall + unmarked + misnamed + accessUnmapped + callUnmapped + callGoneUnsaid + roundTrip +
               accessAtBranch + frameStoreMissing + backWrong;
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
//   tokslot     (A7-10) a delay slot's instruction is addressed to its branch again: the tokens at a slot, and the ledger's accesses, move
//               back by one instruction. Needs 'word' (the instruction words), as the audit does.
inline bool MutateMapping(sfe::decomp::DecompileResult& r, const std::string& kind,
                          const std::function<bool(uint32_t, uint16_t&)>& word = std::function<bool(uint32_t, uint16_t&)>())
{
    using namespace sfe::decomp;
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
    // A7-11: a line that assigns a frame local (`xStack_18 = ...;`, `xStack_18._3_1_ = ...;`) at a load or a call, and not at a store, is not
    // the code writing the local there: the value was stored earlier and the engine shows it again (a call's effect on the local; a piece of
    // it read back). The store the engine folded into the local is among the line's instructions, whichever it was.
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
                const uint32_t at = 0u - hexv + piece;
                std::set<uint32_t> candidates;
                for (const AccessFate* f : folded)
                    if (at - f->address < f->size) candidates.insert(f->insn);
                if (candidates.empty()) continue;
                bool atLoadOrCall = false, atStore = false, named = false;
                for (const Token& t : r.lines[li].tokens)
                {
                    uint16_t w = 0;
                    if (!t.addr || !word(t.addr, w)) continue;
                    // (what the line draws says what it is made at; the empty tokens a store was named by do not)
                    if (t.begin < t.end)
                    {
                        const MemoryShape m = ShapeOf(sfe::Sh2Decode(t.addr, w));
                        atLoadOrCall = atLoadOrCall || m.load || m.call;
                        atStore = atStore || m.store;
                    }
                    named = named || candidates.count(t.addr) != 0;
                }
                if (!atLoadOrCall || atStore) continue;
                ++a.frameAssigns;
                if (named) continue;
                ++a.frameStoreMissing;
                problem("frame-store", "line " + std::to_string(li) + " '" + text + "' assigns a frame local at a load or a call and names none of the " + std::to_string(candidates.size()) +
                                           " store(s) the engine folded into it (first " + MappingHex(*candidates.begin()) + ")");
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
    for (const std::string& p : a.problems) std::printf("    %s\n", p.c_str());
}

}  // namespace dts
