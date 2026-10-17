// Area 7 of the review program: the pseudocode-to-assembly mapping. Shared by the game-code run
// (Sh2DecompilerTests.cpp, TestGameCode) and the generator (Sh2GeneratedTests.cpp).
//
// The oracle: every token address lies in the body; a line several instructions contribute to is
// marked; navigation round-trips. Concretely, for one DecompileResult:
//   1. Token addresses. Every non-zero token address is even, lies in the body (the instructions
//      flow reached, delay slots included) or is named by a flow warning, and is the start of an
//      instruction that decodes. Counted, not failed, because the engine says so or the word is
//      what the line names: an odd address in a function with an "overlaps instruction" warning
//      or entered at an odd address (A7-6), and the word flow halted at as bad data. A line's
//      firstAddr is its lowest token address, 0 with none. A token's range lies in its line's
//      text. A call token (FuncName with a target) came from a call or a jump.
//   2. Marking. A line from several instructions says so in its hover text (LineOriginText), and
//      lists the addresses; a line from one names it; a line from none has no text and no jump.
//   3. Round trip. A click goes to firstAddr; the lines an instruction owns (those with a token of
//      it) hold the line it came from.
//   4. Coverage. Every access the ledger lists has a token of its instruction on some line, and
//      every call in the final p-code has one; a call removed from the p-code is named by a
//      "Removing unreachable block" warning at or before it. The rest of the body (register
//      moves and arithmetic folded into the expression that uses them, a delay slot, which the
//      branch carries) has no token of its own by design (A7-7) and is counted.
#pragma once

#include "Debug/Sh2Disasm.h"
#include "Decompiler/Sh2Decompiler.h"

#include <algorithm>
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
    int callUnmapped = 0;      // a call in the final p-code that no token came from
    int roundTrip = 0;         // a line whose firstAddr does not lead back to it (the lines an instruction owns do not hold it)
    // What the corpus looks like (counted).
    int functions = 0, lines = 0, linesWithAddr = 0, tokens = 0, tokensWithAddr = 0;
    int single = 0, multi = 0, multiNonAdjacent = 0, widest = 0;
    int nonCodeLines = 0;      // a line with no instruction (declarations, braces, case labels)
    int bodyInsns = 0, bodyMapped = 0;
    int unmappedSlots = 0;     // a delay slot instruction no token carries (its p-code carries the branch's address)
    int slotTokens = 0;        // a token at a delay slot's own address
    int oddWarned = 0;         // an odd token address in a function the engine says has overlapping instructions (A7-6: counted, not failed)
    int oddEntry = 0;          // an odd token address in a function whose entry is odd (A7-6: counted, not failed)
    int badDataTokens = 0;     // a token at a word flow halted at as bad data (`halt_baddata();`): the word, by design
    int unmappedMemory = 0;    // an instruction that accesses memory (not a literal load), no token, in the body
    int unmappedCall = 0;      // a call or jump no token carries
    int callGone = 0;          // ...of which no longer in the final p-code (removed with unreachable code or as dead)
    int callGoneUnsaid = 0;    // ...and no warning names a removed block at or before it (counted as a violation)
    int unmappedBranch = 0;    // a conditional branch no token carries
    std::vector<std::string> problems;   // the first few of each kind, "kind: where: what"

    void Add(const MappingAudit& o)
    {
        odd += o.odd; outside += o.outside; notInsn += o.notInsn; firstAddrWrong += o.firstAddrWrong; badRange += o.badRange;
        callWithoutCall += o.callWithoutCall; unmarked += o.unmarked; misnamed += o.misnamed; accessUnmapped += o.accessUnmapped; accessOffLine += o.accessOffLine; callUnmapped += o.callUnmapped; callGone += o.callGone; callGoneUnsaid += o.callGoneUnsaid;
        roundTrip += o.roundTrip;
        functions += o.functions; lines += o.lines; linesWithAddr += o.linesWithAddr; tokens += o.tokens;
        tokensWithAddr += o.tokensWithAddr; single += o.single; multi += o.multi; multiNonAdjacent += o.multiNonAdjacent;
        widest = widest > o.widest ? widest : o.widest;
        nonCodeLines += o.nonCodeLines; bodyInsns += o.bodyInsns; bodyMapped += o.bodyMapped; unmappedSlots += o.unmappedSlots;
        slotTokens += o.slotTokens; oddWarned += o.oddWarned; oddEntry += o.oddEntry; badDataTokens += o.badDataTokens; unmappedMemory += o.unmappedMemory; unmappedCall += o.unmappedCall;
        unmappedBranch += o.unmappedBranch;
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
        return odd + outside + notInsn + firstAddrWrong + badRange + callWithoutCall + unmarked + misnamed + accessUnmapped + callUnmapped + callGoneUnsaid + roundTrip;
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
inline bool MutateMapping(sfe::decomp::DecompileResult& r, const std::string& kind)
{
    using namespace sfe::decomp;
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
    bool overlaps = false;
    for (const std::string& w : r.warnings)
        if (w.find("overlaps instruction") != std::string::npos) overlaps = true;
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
                // Flow decoded an instruction at an odd address (a jump table entry that is not an address): the engine says
                // so ("Instruction at ... overlaps instruction at ...") and the text is what it made of it. Counted when it
                // does; an odd address with no such warning is a violation.
                if (overlaps) ++a.oddWarned;
                else if (r.entry & 1) ++a.oddEntry;   // the function was asked for at an odd address, and every instruction follows from it
                else
                {
                    ++a.odd;
                    problem("odd", "line " + std::to_string(li) + " '" + l.text + "': token address " + MappingHex(t.addr) + " is odd");
                }
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
        const bool saysSeveral = tip.find(" SH-2 instructions") != std::string::npos;
        if ((at.size() > 1) != saysSeveral)
        {
            ++a.unmarked;
            problem("unmarked", "line " + std::to_string(li) + " '" + l.text + "' is from " + std::to_string(at.size()) + " instruction(s) and its hover text says: " + tip);
        }
        bool names = tip.find("0x" + MappingHex(at[0])) != std::string::npos;
        for (size_t i = 0; i < at.size() && i < 8; ++i) names = names && tip.find("0x" + MappingHex(at[i])) != std::string::npos;
        if (at.size() > 8 && tip.find("and " + std::to_string(at.size() - 8) + " more") == std::string::npos) names = false;
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
    // The ledger: each access has a token of its instruction on the line the engine says it first printed on.
    for (const MemoryAccess& m : r.accesses)
    {
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
                if (word(p - 2, pw) && sfe::Sh2Decode(p - 2, pw).HasDelaySlot && r.InBody(p - 2)) ++a.slotTokens;
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
                "%d calls (%d not in the final p-code), %d conditional branches; %d tokens sit at a delay slot's own address; %d odd addresses in functions with "
                "overlapping instructions (said), %d in functions entered at an odd address, %d tokens at the bad data flow halted at (by design)\n",
                label, a.bodyInsns, a.bodyMapped, a.unmappedSlots, a.unmappedMemory, a.unmappedCall, a.callGone, a.unmappedBranch, a.slotTokens,
                a.oddWarned, a.oddEntry, a.badDataTokens);
    for (const std::string& p : a.problems) std::printf("    %s\n", p.c_str());
}

}  // namespace dts
