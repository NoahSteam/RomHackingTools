// HardwareHandlerCheck.h -- the width table checked against Mednafen's handlers instead of against
// itself (Area 5, round 4; REVIEW_LEDGER.md A5-18).
//
// HardwareWidthTable.golden.txt is printed from WidthCellFor, so it can only say the table did not
// change; a rule that disagrees with Mednafen is a wrong golden line (A5-18: eleven DMAC cells said a
// word read at +2 reaches the register, sh7095.inc has no case there). This file reads
// MednafenHandlerCases.txt instead -- the switch labels each handler decodes at each width, derived
// from Mednafen's sources by Docs/Decompiler/tools/derive_handler_cases.py without looking at the
// table -- and compares it, cell by cell, with the table:
//   * on-chip registers (0xFFFFFE00 and up), every cell: the rule's claim that the access reaches the
//     register (or does not, or is an address error) against the handler's case labels for that
//     width, the half of the area (A & 0x100) and the mask the handler applies, and the register's
//     own read or write effect (a flag armed, a latch, a division) against the case body's;
//   * SCU registers, every cell: decoded and doing something at that width and direction, or not;
//   * the other register blocks (VDP1, VDP2, SMPC, CD block, SCSP), every register and direction;
//   * what the CPU's bus does before any register is reached: a misaligned access, a read through
//     the cached partition, the whole 0xE0000000 partition decoding as on-chip registers;
//   * every aligned address of the on-chip area, named or not, at every width: an address error iff
//     the handler raises one.
// What it cannot check is listed in 15_DECOMPILER_STEP5.md ("Area 5, round 4", "What the independent
// check covers"): which bits of the value reach the register, the lane rules of the 16-bit buses
// (SMPC, SCSP, CD, MINIT/SINIT), the keyed writes' keys, the FIFO pops and the text of any caution.
#pragma once
#include "Decompiler/SaturnHardwareMap.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace hhc
{
using namespace sfe::decomp;

struct Group
{
    std::vector<unsigned> labels;
    std::string flags;   // M E D S C, or "-"
    bool Has(char c) const { return flags.find(c) != std::string::npos; }
};

struct Handler
{
    bool pex = false, sw = false;
    long mask = -1;   // -1: `am`, the low byte as it is (or none)
    std::vector<Group> groups;
    std::map<unsigned, int> byLabel;
    const Group* At(unsigned label) const
    {
        auto it = byLabel.find(label);
        return !sw || it == byLabel.end() ? nullptr : &groups[size_t(it->second)];
    }
};

struct Cases
{
    std::map<std::string, Handler> handlers;
    std::map<std::string, std::map<std::string, int>> bus;
    bool Load(const std::string& path, std::string& error)
    {
        std::ifstream f(path);
        if (!f) { error = "cannot read " + path; return false; }
        std::string ln;
        while (std::getline(f, ln))
        {
            if (ln.empty() || ln[0] == '#') continue;
            const size_t colon = ln.find(" : ");
            std::istringstream head(colon == std::string::npos ? ln : ln.substr(0, colon));
            std::string name, kv;
            head >> name;
            Handler h;
            std::map<std::string, int> facts;
            while (head >> kv)
            {
                const size_t eq = kv.find('=');
                const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
                if (k == "pex") h.pex = v == "1";
                else if (k == "sw") h.sw = v == "1";
                else if (k == "mask") h.mask = (v == "am" || v == "-") ? -1 : std::strtol(v.c_str(), nullptr, 16);
                else facts[k] = std::atoi(v.c_str());
            }
            if (name.compare(0, 4, "bus.") == 0) { bus[name] = facts; continue; }
            if (colon != std::string::npos)
            {
                const std::string body = ln.substr(colon + 3);
                size_t at = 0;
                while (at < body.size())
                {
                    size_t end = body.find(" ; ", at);
                    if (end == std::string::npos) end = body.size();
                    const std::string g = body.substr(at, end - at);
                    at = end + 3;
                    const size_t eq = g.find('=');
                    if (eq == std::string::npos) continue;
                    Group grp;
                    grp.flags = g.substr(eq + 1);
                    std::stringstream ls(g.substr(0, eq));
                    std::string tok;
                    while (std::getline(ls, tok, ',')) grp.labels.push_back(unsigned(std::strtoul(tok.c_str(), nullptr, 16)));
                    for (unsigned l : grp.labels) h.byLabel[l] = int(h.groups.size());
                    h.groups.push_back(grp);
                }
            }
            handlers[name] = h;
        }
        if (handlers.size() < 20) { error = "the cases file has too few handlers"; return false; }
        return true;
    }
    const Handler* Get(const std::string& key) const
    {
        auto it = handlers.find(key);
        return it == handlers.end() ? nullptr : &it->second;
    }
};

// Cells and registers the table claims from the manual, where Mednafen has no handler at all.
inline bool NotDecodedFamily(NarrowAccess f) { return f == NarrowAccess::OnChipSci || f == NarrowAccess::OnChipWord; }

struct Stats
{
    size_t onchipCells = 0, scuCells = 0, blockRegs = 0, unnamedAddresses = 0, busFacts = 0, reached = 0, unreached = 0, errors = 0,
           effects = 0, undecodedRegs = 0;
};

struct Problems
{
    std::vector<std::string> list;
    void Add(const std::string& s) { list.push_back(s); }
};

inline std::string Hex(uint32_t v, int digits = 8)
{
    char b[24];
    std::snprintf(b, sizeof b, "%0*X", digits, v);
    return b;
}

inline std::string CellName(const WidthProbe& p)
{
    return std::string(p.reg->name) + " " + (p.write ? "w" : "r") + std::to_string(p.width) + "@" + std::to_string(p.off) + " (" +
           Hex(p.access) + ") " + WidthRuleName(p.cell.rule);
}

inline bool Allows(const HardwareRegister& r, bool write)
{
    return write ? (r.access == RegisterAccess::ReadWrite || r.access == RegisterAccess::WriteOnly)
                 : (r.access != RegisterAccess::WriteOnly);
}

// Read effects the table does not document (no onRead) on registers where the handler does something
// besides return the value, and write effects it does not document: the map's rule is that a note says
// them, and these are the ones decided not to (each with the reason). Empty entries are a failure.
struct Excused
{
    const char* reg;
    const char* what;   // "read" or "write:<D|S|C>"
    const char* why;
};

inline void CheckOnChip(const Cases& c, const std::vector<WidthProbe>& table, const std::vector<Excused>& excused, Stats& st, Problems& pr)
{
    for (const WidthProbe& p : table)
    {
        const HardwareRegister& r = *p.reg;
        if (r.address < 0xFFFFFE00u || NotDecodedFamily(r.narrow)) continue;
        if (p.cell.rule == WidthRule::AccessKind || p.cell.rule == WidthRule::Undecoded) continue;
        const uint32_t a = p.access;
        const bool hi = (a & 0x100u) != 0;
        const std::string key = std::string("onchip.") + (p.write ? "wr" : "rd") + std::to_string(p.width * 8) + (hi ? ".hi" : ".lo");
        const Handler* h = c.Get(key);
        if (!h) { pr.Add("no handler " + key + " in the cases file"); continue; }
        ++st.onchipCells;
        const unsigned label = hi ? (unsigned(a) & unsigned(h->mask)) : (a & 0xFFu);
        const unsigned own = hi ? (r.address & unsigned(h->mask)) : (r.address & 0xFFu);
        const Group* g = h->At(label);
        const Group* go = h->At(own);
        // A keyed word write (the watchdog) reaches whichever register its key picks: the port's case is the check.
        const bool port = p.cell.rule == WidthRule::KeyedWrite && p.width == 2;
        const bool reached = g && (port || (go && g == go));
        const bool meaningful = reached && g->Has('M');
        const std::string where = "  [" + key + ", label 0x" + Hex(label, 2) + " vs the register's 0x" + Hex(own, 2) +
                                  (g ? std::string(", group flags ") + g->flags : std::string(", no case")) + "]";
        meaningful ? ++st.reached : ++st.unreached;
        switch (p.cell.rule)
        {
        case WidthRule::Plain:
        case WidthRule::WordWritesLong:
        case WidthRule::EvenByteWrite:
        case WidthRule::EvenByteRead:
        case WidthRule::LowHalfRead:
        case WidthRule::KeyedWrite:
            if (!meaningful) pr.Add(CellName(p) + ": the table says the access reaches the register, the handler has no case that does" + where);
            break;
        case WidthRule::OddByteWrite:
        case WidthRule::OddByteRead:
        case WidthRule::LongHalfWrite:
        case WidthRule::LongHalfRead:
        case WidthRule::KeyedWordWrite:
        case WidthRule::KeyedByteWrite:
        case WidthRule::MirrorWrite:
            if (meaningful) pr.Add(CellName(p) + ": the table says nothing reaches the register, the handler decodes it" + where);
            break;
        case WidthRule::AddressError:
            ++st.errors;
            if (!h->pex) pr.Add(CellName(p) + ": the table says address error, the handler raises none" + where);
            break;
        default:
            break;
        }
        // The register's own effect: does the access do it? An address error is not a stop: the access is made after it.
        const bool flag = (p.cell.flags & kWidthEffect) != 0;
        if (!p.write)
        {
            const bool effect = reached && g->Has('E');
            if (r.onRead)
            {
                ++st.effects;
                if (flag != effect)
                    pr.Add(CellName(p) + ": the table says the read effect " + (flag ? "happens" : "does not happen") + ", the handler " + (effect ? "does it" : "does not") + where);
            }
            else if (effect)
            {
                bool ok = false;
                for (const Excused& e : excused) ok |= std::string(e.reg) == r.name && std::string(e.what) == "read";
                if (!ok) pr.Add(CellName(p) + ": the handler has a read effect (a flag armed, a latch) and the register has no onRead" + where);
            }
        }
        else
        {
            const bool division = reached && g->Has('D');
            if (r.onWrite)
            {
                ++st.effects;
                if (flag != division)
                    pr.Add(CellName(p) + ": the table says the write effect " + (flag ? "happens" : "does not happen") + ", the handler " + (division ? "divides" : "does not") + where);
            }
            else if (division)
                pr.Add(CellName(p) + ": the handler starts a division and the register has no onWrite" + where);
            if (reached && g->Has('C') && WriteEffects(r.address, p.width, false, 0).empty())
                pr.Add(CellName(p) + ": the handler sets CCR (a cache purge) and WriteEffects says nothing" + where);
            if (!r.onWrite && reached && g->Has('S'))
            {
                bool ok = false;
                for (const Excused& e : excused) ok |= std::string(e.reg) == r.name && std::string(e.what) == "write:S";
                if (!ok) pr.Add(CellName(p) + ": the handler starts a DMA transfer and nothing says so" + where);
            }
        }
    }
}

// Families Mednafen has no handler for: the table claims them from the manual (SCI, UBC); the check is
// that Mednafen still has no case, so the day it grows one this fails and the claim gets reviewed.
inline void CheckUndecoded(const Cases& c, Stats& st, Problems& pr)
{
    size_t n = 0;
    const HardwareRegister* regs = SaturnHardwareRegisters(n);
    for (size_t i = 0; i < n; ++i)
    {
        const HardwareRegister& r = regs[i];
        if (r.address < 0xFFFFFE00u || !NotDecodedFamily(r.narrow)) continue;
        ++st.undecodedRegs;
        for (const char* d : { "rd", "wr" })
            for (int w : { 8, 16, 32 })
            {
                const bool hi = (r.address & 0x100u) != 0;
                const Handler* h = c.Get(std::string("onchip.") + d + std::to_string(w) + (hi ? ".hi" : ".lo"));
                if (!h) continue;
                const unsigned label = hi ? (r.address & unsigned(h->mask)) : (r.address & 0xFFu);
                if (h->At(label)) pr.Add(std::string(r.name) + ": claimed from the manual as undecoded, but " + (hi ? "onchip." : "onchip.") + d + std::to_string(w) + " has a case for it now");
            }
    }
}

inline void CheckScu(const Cases& c, const std::vector<WidthProbe>& table, Stats& st, Problems& pr)
{
    for (const WidthProbe& p : table)
    {
        const HardwareRegister& r = *p.reg;
        if (r.address < 0x05FE0000u || r.address > 0x05FEFFFFu) continue;
        const std::string key = std::string("scu.") + (p.write ? "wr" : "rd") + std::to_string(p.width * 8);
        const Handler* h = c.Get(key);
        if (!h) { pr.Add("no handler " + key); continue; }
        ++st.scuCells;
        const unsigned label = (r.address - 0x05FE0000u) & unsigned(h->mask);
        const Group* g = h->At(label);
        const bool meaningful = g && g->Has('M');
        const bool kind = p.cell.rule == WidthRule::AccessKind;
        if (kind == meaningful)
            pr.Add(CellName(p) + ": the table says the register is " + (kind ? "not accessible this way" : "accessible this way") + ", " + key + " " +
                   (meaningful ? "decodes it" : g ? "decodes it but does nothing" : "has no case for it"));
        if (!p.write && !kind && ((p.cell.flags & kWidthEffect) != 0) && r.onRead && !(g && g->Has('E')))
            pr.Add(CellName(p) + ": onRead says a read does more than return a value, " + key + " does not");
        if (!p.write && !kind && !r.onRead && g && g->Has('E'))
            pr.Add(CellName(p) + ": " + key + " has a read effect and the register has no onRead");
    }
}

// The blocks whose handlers are one switch for every width: per register and direction.
inline void CheckBlocks(const Cases& c, Stats& st, Problems& pr)
{
    size_t n = 0;
    const HardwareRegister* regs = SaturnHardwareRegisters(n);
    for (size_t i = 0; i < n; ++i)
    {
        const HardwareRegister& r = regs[i];
        const uint32_t a = r.address;
        std::string rdKey, wrKey, wrKey2;
        long label = -1;
        if (a >= 0x05D00000u && a < 0x05D80000u) { rdKey = "vdp1.rd"; wrKey = "vdp1.wr"; label = (a - 0x05D00000u) >> 1; }
        else if (a >= 0x05F80000u && a < 0x05FC0000u) { rdKey = "vdp2.rd"; wrKey = "vdp2.wr"; wrKey2 = "vdp2r.wr"; label = a - 0x05F80000u; }
        else if (a >= 0x00100000u && a < 0x00180000u) { rdKey = "smpc.rd"; wrKey = "smpc.wr"; label = (a & 0x7Fu) >> 1; }
        else if (a == 0x05818000u) { rdKey = "cd.rd"; wrKey = "cd.wr"; label = 0; }
        else if (a >= 0x05890000u && a < 0x058A0000u) { rdKey = "cd.rd"; wrKey = "cd.wr"; label = (a - 0x05890000u) >> 2; }
        else if (a >= 0x05B00400u && a < 0x05B00430u) { rdKey = "scsp.rd"; wrKey = "scsp.wr"; label = ((a - 0x05B00000u) >> 1) & 0x1F; }
        else continue;   // MINIT / SINIT and the SCU (per cell) are not a case switch
        ++st.blockRegs;
        for (int dir = 0; dir < 2; ++dir)
        {
            const bool write = dir == 1;
            bool meaningful = false, effect = false;
            for (const std::string& key : { write ? wrKey : rdKey, write ? wrKey2 : std::string() })
            {
                if (key.empty()) continue;
                const Handler* h = c.Get(key);
                if (!h) { pr.Add("no handler " + key); continue; }
                const Group* g = h->At(unsigned(label));
                meaningful |= g && g->Has('M');
                effect |= g && g->Has('E');
            }
            const bool allowed = Allows(r, write);
            // The watchdog-style keyed registers do not exist here; every block register is plain.
            if (allowed != meaningful)
                pr.Add(std::string(r.name) + ": the table says a " + (write ? "write" : "read") + (allowed ? " works" : " does nothing") + ", " +
                       (write ? wrKey : rdKey) + " label 0x" + Hex(unsigned(label), 1) + (meaningful ? " does something" : " does not"));
            if (write && allowed)
            {
                bool drawing = false, constant = false;
                for (const std::string& key : { wrKey, wrKey2 })
                {
                    const Handler* h = key.empty() ? nullptr : c.Get(key);
                    const Group* g = h ? h->At(unsigned(label)) : nullptr;
                    drawing |= g && g->Has('G');
                    constant |= g && g->Has('F') && key == "smpc.wr";   // the others (VDP1 ENDR, SCU AIACK, SCSP) are listed in the cases file, not checked
                }
                if (drawing && WriteEffects(r.address, 2, false, 0).empty())
                    pr.Add(std::string(r.name) + ": the handler starts drawing on a write and WriteEffects says nothing");
                if (constant && !r.onWrite)
                    pr.Add(std::string(r.name) + ": the handler stores a constant whatever is written and the register has no onWrite");
                if (constant && r.onWrite && !r.noteWrite)
                    pr.Add(std::string(r.name) + ": the handler stores a constant whatever is written, onWrite says so but no note is made of it");
            }
            if (!write && allowed && bool(r.onRead) != effect)
                pr.Add(std::string(r.name) + ": " + (r.onRead ? "onRead says a read has an effect" : "the handler has a read effect and the register has no onRead") + " (" + rdKey + " label 0x" + Hex(unsigned(label), 1) + ")");
        }
    }
}

// What the CPU's bus does before a register is reached.
inline void CheckBus(const Cases& c, const std::vector<WidthProbe>& table, Stats& st, Problems& pr)
{
    auto fact = [&](const char* group, const char* name) {
        auto g = c.bus.find(group);
        if (g == c.bus.end() || !g->second.count(name)) { pr.Add(std::string("no fact ") + group + " " + name); return 0; }
        return g->second.at(name);
    };
    auto hasWord = [](const std::vector<NarrowEffect>& v, const char* word, bool cautionOnly) {
        for (const NarrowEffect& e : v)
            if ((!cautionOnly || e.caution) && e.text.find(word) != std::string::npos) return true;
        return false;
    };
    // A misaligned access is an address error, made at the aligned address (MemRead / MemWrite).
    const int mis = fact("bus.misaligned", "rd_pex") && fact("bus.misaligned", "wr_pex");
    size_t n = 0;
    const HardwareRegister* regs = SaturnHardwareRegisters(n);
    for (size_t i = 0; i < n; ++i)
    {
        const HardwareRegister& r = regs[i];
        if (r.address >= 0xFFFFFE00u) continue;   // the on-chip area gets every address below
        for (uint32_t w : { 2u, 4u })
            for (uint32_t k = 1; k < w; ++k)
            {
                const uint32_t addr = r.address + k;   // inside or beside the register, never aligned to w
                if (!(addr & (w - 1))) continue;
                for (int wr = 0; wr < 2; ++wr)
                {
                    ++st.busFacts;
                    const uint32_t bus = r.address | (r.address < 0x20000000u ? 0x20000000u : 0u);   // cache-through: no line fill in the way
                    const bool said = hasWord(NarrowAccessEffects(bus + k, w, wr != 0), "address error", true);
                    if (bool(mis) != said)
                        pr.Add(std::string(r.name) + ": a misaligned " + std::to_string(w) + "-byte " + (wr ? "write" : "read") + " at " + Hex(bus + k) +
                               (said ? " is called an address error, the bus does not raise one" : " is not called an address error, the bus raises one"));
                }
            }
        // A read through the cached partition is a cache read: a hit makes no bus access, a miss reads the
        // line. A write is written through. The cache-through partition is the plain bus.
        const bool cacheRead = fact("bus.cache", "rd_cached_partition") && fact("bus.cache", "rd_hit_no_bus") && fact("bus.cache", "rd_miss_fills_line");
        const bool writeThrough = fact("bus.cache", "wr_through") != 0;
        const uint32_t cached = r.address & 0x07FFFFFFu, through = cached | 0x20000000u;
        ++st.busFacts;
        const bool rdCached = hasWord(NarrowAccessEffects(cached, r.size, false), "cache", true);
        const bool rdThrough = hasWord(NarrowAccessEffects(through, r.size, false), "cache", true);
        const bool wrCached = hasWord(NarrowAccessEffects(cached, r.size, true), "cache", true);
        const bool readable = r.access != RegisterAccess::WriteOnly;
        if (readable && bool(cacheRead) != rdCached)
            pr.Add(std::string(r.name) + ": a read at " + Hex(cached) + " (cached partition) " + (rdCached ? "is called a cache read, the bus does not cache" : "is not called a cache read, the bus caches it"));
        if (rdThrough) pr.Add(std::string(r.name) + ": a read at " + Hex(through) + " (cache-through) is called a cache read");
        if (writeThrough && wrCached) pr.Add(std::string(r.name) + ": a write at " + Hex(cached) + " is called a cache effect, the cache writes through");
    }
    // The whole 0xE0000000-0xFFFFFFFF partition is the on-chip registers, decoded by the low 9 bits.
    const bool p7 = fact("bus.partition7", "onchip_rd") && fact("bus.partition7", "onchip_wr");
    const uint32_t prefixes[] = { 0xE0000000u, 0xE0000200u, 0xE0004000u, 0xF0001000u, 0xFFFF8E00u, 0xFFFFC000u, 0xFFFFFE00u, 0xFFFF0000u };
    for (size_t i = 0; i < n; ++i)
    {
        if (regs[i].address < 0xFFFFFE00u) continue;
        for (uint32_t pre : prefixes)
        {
            ++st.busFacts;
            const HardwareRegister* got = FindHardwareRegister((pre & ~0x1FFu) | (regs[i].address & 0x1FFu));
            const bool same = got == &regs[i];
            if (bool(p7) != same)
                pr.Add(std::string(regs[i].name) + ": " + Hex((pre & ~0x1FFu) | (regs[i].address & 0x1FFu)) +
                       (same ? " is named as the register, the bus does not decode it" : " is not the register, the bus decodes the whole partition by A & 0x1FF"));
        }
    }
    (void)table;
}

// Every aligned address of the on-chip area, named or not, at every width and direction: the caution
// "address error" exactly when the handler raises one.
inline void CheckOnChipAddresses(const Cases& c, Stats& st, Problems& pr)
{
    for (uint64_t a = 0xFFFFFE00ull; a <= 0xFFFFFFFFull; ++a)
        for (uint32_t w : { 1u, 2u, 4u })
        {
            if (a & (w - 1)) continue;
            for (int wr = 0; wr < 2; ++wr)
            {
                const bool hi = (a & 0x100u) != 0;
                const Handler* h = c.Get(std::string("onchip.") + (wr ? "wr" : "rd") + std::to_string(w * 8) + (hi ? ".hi" : ".lo"));
                if (!h) continue;
                ++st.unnamedAddresses;
                bool said = false;
                for (const NarrowEffect& e : NarrowAccessEffects(uint32_t(a), w, wr != 0))
                    said |= e.caution && e.text.find("address error") != std::string::npos;
                if (said != h->pex)
                    pr.Add("on-chip " + std::string(wr ? "write" : "read") + " of " + std::to_string(w) + " bytes at " + Hex(uint32_t(a)) +
                           (said ? " is called an address error, the handler raises none" : " is not called an address error, the handler raises one"));
            }
        }
}
}  // namespace hhc
