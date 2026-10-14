#include "Decompiler/Sh2FunctionFinder.h"

#include "Debug/Sh2Disasm.h"
#include "Disc/PathUtil.h"   // ReadWholeFile
#include "FileWrite.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace sfe
{
namespace decomp
{
namespace
{

const uint16_t kRts = 0x000B;
const uint16_t kRte = 0x002B;
const uint16_t kNop = 0x0009;

// How far a mov.w/mov.l @(disp,PC) can reach (disp8 x 4, plus the PC offset): the previous
// function's literals can only be this far past its last load.
const uint32_t kPcRelReach = 0x400;

// How many instructions after the scan's entry must decode before it is believed.
const int kValidateInstructions = 3;

std::string Hex8(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

// The literal address a PC-relative load at 'site' reads, as the instruction stream shows it
// (its own PC: not in a delay slot).
bool PcRelLiteral(uint32_t site, uint16_t op, uint32_t& address, uint32_t& size)
{
    const DisassembledInstruction ins = Sh2Decode(site, op);
    if (ins.PcRel == Sh2PcRel::Word)
    {
        address = site + 4 + ins.PcRelDisp;
        size = 2;
        return true;
    }
    if (ins.PcRel == Sh2PcRel::Long)
    {
        address = ((site + 4) & ~3u) + ins.PcRelDisp;
        size = 4;
        return true;
    }
    return false;
}

// The value a mov.l @(disp,PC),rN loaded into rN in the instructions just before 'site', believed
// only when every instruction in between provably leaves rN alone. SH-2 encodes its register
// operands in bits 8-11 and 4-7, so one naming rN in either may change it (a destination, or @rN+ /
// @-rN); r0 is also written implicitly by mova, the GBR-relative and #imm-to-r0 forms (0xCxxx) and
// mov.b/mov.w @(disp,rM),r0 (0x8xxx); and a branch or call in between means the load may not even
// have run. Anything of that kind ends the search with no answer.
bool LiteralInRegister(const MemorySnapshot& snap, uint32_t site, unsigned n, uint32_t& value)
{
    for (int back = 1; back <= 8; ++back)
    {
        const uint32_t q = site - 2 * back;
        uint16_t prev = 0;
        if (!snap.ReadU16(q, prev)) return false;
        uint32_t lit = 0, size = 0;
        if ((prev & 0xF000u) == 0xD000u && ((prev >> 8) & 0xF) == n && PcRelLiteral(q, prev, lit, size))
            return snap.ReadU32(lit, value);
        const DisassembledInstruction ins = Sh2Decode(q, prev);
        if (!ins.IsValid || ins.IsBranch || ins.HasDelaySlot) return false;
        if (((prev >> 8) & 0xF) == n || ((prev >> 4) & 0xF) == n) return false;
        const unsigned hi = prev >> 12;
        if (n == 0 && (hi == 0x8 || hi == 0xC)) return false;
    }
    return false;
}

// How many instructions a reachability walk visits before giving up with no answer.
const size_t kMaxReachInstructions = 4096;

enum class Reach { Yes, No, Unknown };

// Whether flow from 'from' gets to the instruction at 'to', followed the way the decompiler's flow
// is: both ways of a conditional branch, over calls (taken to return), never past rts/rte. A jump
// the code does not resolve (braf, or jmp @rN not loaded from a literal: a switch, perhaps) or
// bytes that do not decode leave it Unknown unless 'to' is reached some other way. A jmp @rN to a
// literal address between 'from' and 'to' is a far branch within the function (past bra's reach);
// anywhere else it is a tail call, which ends the path. Flow is not followed through 'avoid'.
Reach Reaches(const MemorySnapshot& snap, uint32_t from, uint32_t to, uint32_t avoid = 1)
{
    std::set<uint32_t> seen;
    std::vector<uint32_t> work{ from };
    bool unknown = false;
    while (!work.empty())
    {
        const uint32_t p = work.back();
        work.pop_back();
        if (!seen.insert(p).second) continue;
        if (seen.size() > kMaxReachInstructions) return Reach::Unknown;
        if (p == avoid && p != from) continue;
        if (p == to) return Reach::Yes;
        uint16_t op = 0;
        if (!snap.ReadU16(p, op))
        {
            unknown = true;
            continue;
        }
        const DisassembledInstruction ins = Sh2Decode(p, op);
        if (!ins.IsValid)
        {
            unknown = true;
            continue;
        }
        uint32_t next = p + 2;
        if (ins.HasDelaySlot)
        {
            if (p + 2 == to) return Reach::Yes;
            next = p + 4;
        }
        if (ins.IsReturn) continue;
        if (ins.IsCall)
        {
            work.push_back(next);
            continue;
        }
        if (ins.IsBranch)
        {
            uint32_t t = 0, c = 0;
            if (ins.HasBranchTarget) work.push_back(ins.BranchTarget);
            else if ((op & 0xF0FFu) == 0x402Bu && LiteralInRegister(snap, p, (op >> 8) & 0xF, t))
            {
                if (CanonicalAddress(t, c) && c >= std::min(from, to) && c <= std::max(from, to)) work.push_back(c);
            }
            else
                unknown = true;
            if (ins.IsConditional) work.push_back(next);
            continue;
        }
        work.push_back(next);
    }
    return unknown ? Reach::Unknown : Reach::No;
}

// Whether the halfword at q is data: a word some PC-relative load before it reads (the constant
// 11 in a literal pool reads as rts), or an rts whose delay slot could not be one.
bool DataNotReturn(const MemorySnapshot& snap, uint32_t q, uint32_t floor)
{
    const uint32_t from = q - floor > kPcRelReach ? q - kPcRelReach : floor;
    for (uint32_t x = from; x < q; x += 2)
    {
        uint16_t op = 0;
        uint32_t lit = 0, size = 0;
        if (snap.ReadU16(x, op) && PcRelLiteral(x, op, lit, size) && q >= lit && q < lit + size) return true;
    }
    uint16_t slot = 0;
    if (!snap.ReadU16(q + 2, slot)) return false;   // the region's last word: nothing says otherwise
    const DisassembledInstruction s = Sh2Decode(q + 2, slot);
    return !s.IsValid || s.IsBranch || s.HasDelaySlot;
}

// How many returns (rts/rte that are not data) lie in [lo, hi).
int ReturnsBetween(const MemorySnapshot& snap, uint32_t lo, uint32_t hi)
{
    int n = 0;
    for (uint32_t q = lo; q + 2 <= hi; q += 2)
    {
        uint16_t w = 0;
        if (snap.ReadU16(q, w) && (w == kRts || w == kRte) && !DataNotReturn(snap, q, lo)) ++n;
    }
    return n;
}

// A word a jump table may hold: a 16-bit offset to a case (braf, or added to the table's base), so
// small either way. Function prologues (mov.l rN,@-r15, sts.l pr,@-r15, add #-n,r15) are not.
bool TableOffset(uint16_t w)
{
    return w < 0x1000u || w >= 0xF000u;
}

int Confidence(EntrySource s)
{
    switch (s)
    {
    case EntrySource::User: return 100;
    case EntrySource::Symbol: return 90;
    case EntrySource::CallSite: return 80;
    case EntrySource::RtsScan: return 60;
    case EntrySource::Callee: return 70;
    default: return 0;
    }
}

}  // namespace

const char* EntrySourceName(EntrySource source)
{
    switch (source)
    {
    case EntrySource::User: return "user-set entry";
    case EntrySource::Symbol: return "symbol";
    case EntrySource::CallSite: return "call-site target";
    case EntrySource::RtsScan: return "rts scan";
    case EntrySource::Callee: return "callee";
    default: return "none";
    }
}

namespace
{

// The first instruction after the unconditional transfer at p (rts, or a jump) and its delay slot:
// past the previous function's literal pool, a jump table it addresses with mova, and nop/zero
// padding. False when that runs past 'a'. A mova table has no stated length, so its end is a guess
// ('guessed'): words read as table entries for as long as they look like them.
bool SkipToCode(const MemorySnapshot& snap, uint32_t p, uint32_t floor, uint32_t a, uint32_t& c, bool& guessed)
{
    std::vector<std::pair<uint32_t, uint32_t>> literals;   // [lo, hi)
    std::set<uint32_t> tables;
    const uint32_t from = p - floor > kPcRelReach ? p - kPcRelReach : floor;
    for (uint32_t q = from; q <= p + 2; q += 2)
    {
        uint16_t op = 0;
        if (!snap.ReadU16(q, op)) continue;
        uint32_t lit = 0, size = 0;
        if (PcRelLiteral(q, op, lit, size) && lit >= p + 4) literals.emplace_back(lit, lit + size);
        const DisassembledInstruction ins = Sh2Decode(q, op);
        if (ins.PcRel == Sh2PcRel::Mova)
        {
            const uint32_t t = ((q + 4) & ~3u) + ins.PcRelDisp;
            if (t >= p + 4) tables.insert(t);
        }
    }
    c = p + 4;
    bool inTable = false;
    for (;;)
    {
        uint16_t w = 0;
        if (c > a || !snap.ReadU16(c, w)) break;
        bool literal = false;
        for (const auto& l : literals)
            if (c >= l.first && c < l.second) literal = true;
        if (literal || w == kNop || w == 0x0000)
        {
            c += 2;
            continue;
        }
        inTable = inTable || tables.count(c) != 0;
        if (inTable)
        {
            // Entries are case addresses (32-bit, into captured memory) or offsets (16-bit, small).
            uint32_t v = 0, canonical = 0;
            if ((c & 3) == 0 && snap.ReadU32(c, v) && (v & 1) == 0 && CanonicalAddress(v, canonical) &&
                snap.RegionAt(canonical))
            {
                c += 4;
                guessed = true;
                continue;
            }
            if (TableOffset(w))
            {
                c += 2;
                guessed = true;
                continue;
            }
        }
        break;
    }
    return c <= a;
}

bool DecodesAsCode(const MemorySnapshot& snap, uint32_t c)
{
    for (int i = 0; i < kValidateInstructions; ++i)
    {
        uint16_t op = 0;
        if (!snap.ReadU16(c + 2 * i, op) || !Sh2Decode(c + 2 * i, op).IsValid) return false;
    }
    return true;
}

}  // namespace

bool RtsScanEntry(const MemorySnapshot& snap, uint32_t address, uint32_t& entry, std::string* why, std::string* doubt)
{
    uint32_t a = 0;
    if (!CanonicalAddress(address, a, why)) return false;
    a &= ~1u;
    const MemoryRegion* region = snap.RegionAt(a);
    if (!region)
    {
        if (why) *why = Hex8(a) + " is not in captured memory";
        return false;
    }
    const uint32_t floor = a - region->base > kMaxFunctionSpan ? a - kMaxFunctionSpan : region->base;

    // The previous return: rts/rte at p, its delay slot at p+2, so the next function can start
    // at p+4. An rts whose slot is the address itself still belongs to the address's function.
    // Candidates p run from a - 4 (so the slot at p + 2 is still before the address) down to the
    // floor itself, inclusive. A word that only reads as rts -- a literal, or one with no possible
    // delay slot after it -- is passed over.
    uint32_t p = 0;
    bool found = false;
    if (a - floor >= 4)
    {
        for (uint32_t q = a - 4;; q -= 2)
        {
            uint16_t w = 0;
            if (!snap.ReadU16(q, w)) break;
            if ((w == kRts || w == kRte) && !DataNotReturn(snap, q, floor))
            {
                p = q;
                found = true;
                break;
            }
            if (q < floor + 2) break;
        }
    }
    if (!found)
    {
        if (why) *why = "no rts within " + Hex8(kMaxFunctionSpan) + " bytes before " + Hex8(a);
        return false;
    }

    uint32_t c = 0;
    bool guessed = false;
    if (!SkipToCode(snap, p, floor, a, c, guessed))
    {
        if (why) *why = Hex8(a) + " is padding or a literal pool after the function ending at " + Hex8(p + 2);
        return false;
    }
    if (!DecodesAsCode(snap, c))
    {
        if (why) *why = "what follows the rts at " + Hex8(p) + " does not decode as code";
        return false;
    }
    std::string d;
    if (guessed) d = "the jump table after the rts at " + Hex8(p) + " has no stated end; the entry is a guess";

    // The previous function need not end in rts: it can end in a tail jump (jmp @rN, bra, braf), so
    // the code after the rts can be a whole function before the address's own. If it provably never
    // gets to the address, the entry is after the first such jump whose code does.
    if (Reaches(snap, c, a) == Reach::No)
    {
        bool moved = false;
        for (uint32_t q = c; q + 4 <= a && !moved; q += 2)
        {
            uint16_t op = 0;
            if (!snap.ReadU16(q, op)) break;
            const DisassembledInstruction ins = Sh2Decode(q, op);
            if (!ins.IsValid || !ins.IsBranch || ins.IsCall || ins.IsConditional || !ins.HasDelaySlot) continue;
            uint32_t c2 = 0;
            bool guessed2 = false;
            if (!SkipToCode(snap, q, floor, a, c2, guessed2) || !DecodesAsCode(snap, c2)) continue;
            if (Reaches(snap, c2, a) != Reach::Yes) continue;
            d = "the code after the rts at " + Hex8(p) + " never reaches " + Hex8(a) + "; the entry is after the jump at " + Hex8(q);
            c = c2;
            moved = true;
        }
        if (!moved) d = "the code from " + Hex8(c) + " (after the rts at " + Hex8(p) + ") never reaches " + Hex8(a);
    }
    // Code before the rts that branches past it, into what the scan takes for the next function:
    // the rts is then as likely an early return inside one function. (A bra reaches 4 KiB back.)
    if (d.empty())
    {
        const uint32_t lo = p - floor > 0x1000u ? p - 0x1000u : floor;
        for (uint32_t q = lo; q < p && d.empty(); q += 2)
        {
            uint16_t op = 0;
            if (!snap.ReadU16(q, op)) continue;
            const DisassembledInstruction ins = Sh2Decode(q, op);
            if (!ins.IsValid || !ins.HasBranchTarget || ins.IsCall) continue;
            if (ins.BranchTarget > p + 2 && ins.BranchTarget <= a)
                d = "the branch at " + Hex8(q) + " jumps past the rts at " + Hex8(p) + " to " + Hex8(ins.BranchTarget) +
                    ": the rts may be an early return in a function that starts before it";
        }
    }
    entry = c;
    if (doubt) *doubt = d;
    return true;
}

bool CallTargetAt(const MemorySnapshot& snap, uint32_t site, uint32_t& target)
{
    uint16_t op = 0;
    if (!snap.ReadU16(site, op)) return false;
    const DisassembledInstruction call = Sh2Decode(site, op);
    if (!call.IsCall) return false;
    uint32_t t = 0;
    if (call.HasBranchTarget)   // bsr
        t = call.BranchTarget;
    else if ((op & 0xF0FFu) == 0x400Bu)   // jsr @rN
    {
        // A missed name costs nothing, a wrong one sends the user to the wrong function.
        if (!LiteralInRegister(snap, site, (op >> 8) & 0xF, t)) return false;
    }
    else
        return false;
    return CanonicalAddress(t, target);
}

std::vector<uint32_t> CallTargetsFromListing(const MemorySnapshot& snap, uint32_t entry, uint32_t end)
{
    std::set<uint32_t> targets;
    for (uint32_t a = entry & ~1u; a < end; a += 2)
    {
        uint32_t t = 0;
        if (CallTargetAt(snap, a, t)) targets.insert(t);
    }
    return std::vector<uint32_t>(targets.begin(), targets.end());
}

EntryGuess FindFunctionContaining(const MemorySnapshot& snap, uint32_t address, const FinderHints& hints)
{
    EntryGuess g;
    uint32_t a = 0;
    if (!CanonicalAddress(address, a, &g.why)) return g;
    a &= ~1u;

    // The nearest known entry at or below the address, within the span. On a tie the more
    // trusted source names it.
    bool haveKnown = false;
    uint32_t known = 0;
    EntrySource knownSource = EntrySource::None;
    auto consider = [&](const std::set<uint32_t>& set, EntrySource source) {
        for (uint32_t e : set)
        {
            uint32_t c = 0;
            if (!CanonicalAddress(e, c) || c > a || a - c > kMaxFunctionSpan) continue;
            if (!haveKnown || c > known)
            {
                haveKnown = true;
                known = c;
                knownSource = source;
            }
        }
    };
    // Least trusted first, so that a more trusted source with the same entry replaces it below.
    consider(hints.callees, EntrySource::Callee);
    consider(hints.callSiteTargets, EntrySource::CallSite);
    consider(hints.symbols, EntrySource::Symbol);
    consider(hints.userEntries, EntrySource::User);
    auto rename = [&](const std::set<uint32_t>& set, EntrySource source) {
        for (uint32_t e : set)
        {
            uint32_t c = 0;
            if (haveKnown && CanonicalAddress(e, c) && c == known) knownSource = source;
        }
    };
    rename(hints.callSiteTargets, EntrySource::CallSite);
    rename(hints.symbols, EntrySource::Symbol);
    rename(hints.userEntries, EntrySource::User);

    uint32_t scanned = 0;
    std::string scanWhy, scanDoubt;
    const bool haveScan = RtsScanEntry(snap, a, scanned, &scanWhy, &scanDoubt);

    // An rts between a known entry and the address is either that function's end (the address is
    // in a later one) or an early return inside it. Flow from the entry says which: if it provably
    // never gets to the address, a later function. If it does, the known entry is kept -- but a
    // path that only gets there through the scan's entry, just after the rts, is a conditional
    // tail call into a later function as much as an early return, and that is said. When flow
    // cannot tell (a switch it cannot follow), the known entry -- a real function start -- is kept
    // only if that rts is the one return in between; past several, the scan answers.
    bool useKnown = haveKnown && (knownSource == EntrySource::User || !haveScan || known >= scanned);
    if (haveKnown && !useKnown)
    {
        const Reach r = Reaches(snap, known, a);
        if (r == Reach::Yes)
        {
            useKnown = true;
            if (Reaches(snap, known, a, scanned) != Reach::Yes)
                g.doubt = "flow from " + Hex8(known) + " gets to " + Hex8(a) + " only through " + Hex8(scanned) +
                          ", just after an rts: an early return, or a branch into a function starting there";
        }
        else if (r == Reach::Unknown && ReturnsBetween(snap, known, scanned) <= 1)
        {
            useKnown = true;
            g.doubt = "an rts before " + Hex8(a) + " may end this function; if so, " + Hex8(a) +
                      " is in one starting at " + Hex8(scanned);
        }
    }
    if (useKnown)
    {
        g.ok = true;
        g.entry = known;
        g.source = knownSource;
    }
    else if (haveScan)
    {
        g.ok = true;
        g.entry = scanned;
        g.source = EntrySource::RtsScan;
        g.doubt = scanDoubt;
    }
    else
    {
        g.why = scanWhy;
        return g;
    }
    g.confidence = Confidence(g.source);
    if (!g.doubt.empty()) g.confidence -= 20;
    // A boundary found only by scanning is a guess however well it decodes: nothing proves the rts
    // ends a function rather than returning early from one. Said, so it is never shown as settled.
    if (g.source == EntrySource::RtsScan && g.doubt.empty())
        g.doubt = "guessed from the previous rts; no symbol, call or user entry confirms it";
    return g;
}

const char* const FunctionBounds::kDefaultFile = "saturn_function_bounds.txt";

void FunctionBounds::SetEntry(uint32_t address)
{
    uint32_t c = 0;
    if (CanonicalAddress(address, c)) mEntries.insert(c);
}

void FunctionBounds::ClearEntry(uint32_t address)
{
    uint32_t c = 0;
    if (CanonicalAddress(address, c)) mEntries.erase(c);
}

bool FunctionBounds::Load(const std::string& path)
{
    mEntries.clear();
    std::string text;
    if (!ReadWholeFile(path, text)) return !PathEntryExists(path);
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
    {
        size_t b = 0;
        while (b < line.size() && std::isspace(static_cast<unsigned char>(line[b]))) ++b;
        char* end = nullptr;
        const unsigned long v = std::strtoul(line.c_str() + b, &end, 16);
        if (end == line.c_str() + b) continue;
        SetEntry(static_cast<uint32_t>(v));
    }
    return true;
}

bool FunctionBounds::Save(const std::string& path) const
{
    std::string text = "; Function entries set by hand (Saturn Explorer, SH-2 Pseudocode)\n";
    char b[16];
    for (uint32_t e : mEntries)
    {
        std::snprintf(b, sizeof b, "%08X\n", e);
        text += b;
    }
    std::string error;
    return WriteFileAtomically(path, text.data(), text.size(), error);
}

}  // namespace decomp
}  // namespace sfe
