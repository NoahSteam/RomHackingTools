#include "Decompiler/Sh2FunctionFinder.h"

#include "Debug/Sh2Disasm.h"
#include "Disc/PathUtil.h"   // ReadWholeFile
#include "FileWrite.h"

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

bool RtsScanEntry(const MemorySnapshot& snap, uint32_t address, uint32_t& entry, std::string* why)
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
    uint32_t p = a;
    bool found = false;
    while (p >= floor + 4)
    {
        p -= 2;
        if (p + 4 > a) continue;
        uint16_t w = 0;
        if (!snap.ReadU16(p, w)) break;
        if (w == kRts || w == kRte)
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        if (why) *why = "no rts within " + Hex8(kMaxFunctionSpan) + " bytes before " + Hex8(a);
        return false;
    }

    // The previous function's literal pool sits between its last instruction and the next
    // function: the words its PC-relative loads read. They are skipped, as is nop/zero padding.
    std::vector<std::pair<uint32_t, uint32_t>> literals;   // [lo, hi)
    const uint32_t from = p - floor > kPcRelReach ? p - kPcRelReach : floor;
    for (uint32_t q = from; q <= p + 2; q += 2)
    {
        uint16_t op = 0;
        uint32_t lit = 0, size = 0;
        if (snap.ReadU16(q, op) && PcRelLiteral(q, op, lit, size) && lit >= p + 4)
            literals.emplace_back(lit, lit + size);
    }
    uint32_t c = p + 4;
    for (;;)
    {
        uint16_t w = 0;
        if (c > a || !snap.ReadU16(c, w)) break;
        bool literal = false;
        for (const auto& l : literals)
            if (c >= l.first && c < l.second) literal = true;
        if (!literal && w != kNop && w != 0x0000) break;
        c += 2;
    }
    if (c > a)
    {
        if (why) *why = Hex8(a) + " is padding or a literal pool after the function ending at " + Hex8(p + 2);
        return false;
    }
    for (int i = 0; i < kValidateInstructions; ++i)
    {
        uint16_t op = 0;
        if (!snap.ReadU16(c + 2 * i, op) || !Sh2Decode(c + 2 * i, op).IsValid)
        {
            if (why) *why = "what follows the rts at " + Hex8(p) + " does not decode as code";
            return false;
        }
    }
    entry = c;
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
        // Walk back to the mov.l @(disp,PC),rN that loaded rN. Every SH-2 form that writes a
        // general register names it in bits 8-11, so an instruction in between with that field
        // equal to N may have changed rN; give up there rather than guess. (Compares and stores
        // through rN, and opcodes whose bits 8-11 are not a register at all, are given up on
        // too: rare in this position, and a missed name costs nothing.)
        const unsigned n = (op >> 8) & 0xF;
        bool found = false;
        for (int back = 1; back <= 8 && !found; ++back)
        {
            const uint32_t q = site - 2 * back;
            uint16_t prev = 0;
            if (!snap.ReadU16(q, prev)) return false;
            if (((prev >> 8) & 0xF) != n) continue;
            uint32_t lit = 0, size = 0;
            if ((prev & 0xF000u) != 0xD000u || !PcRelLiteral(q, prev, lit, size)) return false;
            if (!snap.ReadU32(lit, t)) return false;
            found = true;
        }
        if (!found) return false;
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
    std::string scanWhy;
    const bool haveScan = RtsScanEntry(snap, a, scanned, &scanWhy);

    if (haveKnown && (knownSource == EntrySource::User || !haveScan || known >= scanned))
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
    }
    else
    {
        g.why = scanWhy;
        return g;
    }
    g.confidence = Confidence(g.source);
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
