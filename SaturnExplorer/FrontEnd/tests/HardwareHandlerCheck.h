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
//     the handler raises one;
//   * every MISALIGNED word and long address of the on-chip area (round 5, A5-29): the address the handler is
//     given (the bus passes it unmasked), the register its case labels reach, against the registers the
//     decompiler says the access reaches, with the same cell checks as an aligned probe;
//   * a lane mask that kills the store of one label of a group (IPRB's and VCRD's low byte, A5-30): the cases
//     file lists such a label as its own inert group, so a cell that says it writes there disagrees.
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
           effects = 0, undecodedRegs = 0, misaligned = 0, writeEffectRegs = 0, valueActionRegs = 0;
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

// One cell against the handler: p.access is the address the handler is given (aligned for the table's own probes, as the access
// made for a misaligned one), p.off its distance from the register.
inline void CheckOnChipProbe(const Cases& c, const WidthProbe& p, const std::vector<Excused>& excused, Stats& st, Problems& pr)
{
    {
        const HardwareRegister& r = *p.reg;
        if (r.address < 0xFFFFFE00u || NotDecodedFamily(r.narrow)) return;
        if (p.cell.rule == WidthRule::AccessKind || p.cell.rule == WidthRule::Undecoded) return;
        const uint32_t a = p.access;
        const bool hi = (a & 0x100u) != 0;
        const std::string key = std::string("onchip.") + (p.write ? "wr" : "rd") + std::to_string(p.width * 8) + (hi ? ".hi" : ".lo");
        const Handler* h = c.Get(key);
        if (!h) { pr.Add("no handler " + key + " in the cases file"); return; }
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
        case WidthRule::ReservedByteWrite:
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

inline void CheckOnChip(const Cases& c, const std::vector<WidthProbe>& table, const std::vector<Excused>& excused, Stats& st, Problems& pr)
{
    for (const WidthProbe& p : table) CheckOnChipProbe(c, p, excused, st, pr);
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
        // A write that acts whatever (or nearly whatever) is written -- a DMA level started, the A-bus interrupt acknowledged, every
        // level stopped, timer 0 reset (`F` in the cases file) -- is said by WriteEffects for an unknown value (A5-32). Judged once per
        // register, at the long write.
        // V (A5-64): the SCU's IMS raises the SH-2's interrupt line on the spot from the mask written; judged at the long write too.
        if (p.write && p.width == 4 && !kind && g && g->Has('V'))
        {
            ++st.valueActionRegs;
            if (WriteEffects(r.address, 4, false, 0).empty())
                pr.Add(std::string(r.name) + ": " + key + " acts on the value written (V) and WriteEffects says nothing");
        }
        if (p.write && p.width == 4 && !kind && g && g->Has('F'))
        {
            ++st.writeEffectRegs;
            if (WriteEffects(r.address, 4, false, 0).empty())
                pr.Add(std::string(r.name) + ": " + key + " acts on a write whatever it stores (F), and WriteEffects says nothing");
        }
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
                bool drawing = false, constant = false, ends = false, valueActs = false, lowOnly = false;
                size_t actions = 0;   // how many of G, V, S, D, C the label has: each is something a write does

                for (const std::string& key : { wrKey, wrKey2 })
                {
                    const Handler* h = key.empty() ? nullptr : c.Get(key);
                    const Group* g = h ? h->At(unsigned(label)) : nullptr;
                    drawing |= g && g->Has('G');
                    // V (A5-55): the value written chooses the action -- the SCSP's DMA start (DTLG's EX), its interrupt clears (SCIRE, MCIRE),
                    // the SMPC's command (COMREG), VDP1's manual frame change (FBCR's FCM). No line shows it, so a note must.
                    valueActs |= g && g->Has('V');
                    lowOnly |= g && g->Has('L');
                    for (char flag : { 'G', 'V', 'S', 'D', 'C' })
                        if (g && g->Has(flag)) ++actions;
                    constant |= g && g->Has('F') && key == "smpc.wr";
                    // VDP1's ENDR (A5-32). The SCU is judged in CheckScu; the SCSP common block's F is bookkeeping (MPROG_Dirty, a timer
                    // reload under a condition) and is not an effect a note would help with: decided, not checked.
                    ends |= g && g->Has('F') && (key == "vdp1.wr" || key == "cd.wr");
                }
                if (ends)
                {
                    ++st.writeEffectRegs;
                    if (WriteEffects(r.address, 2, false, 0).empty())
                        pr.Add(std::string(r.name) + ": the handler acts on a write whatever it stores (F) and WriteEffects says nothing");
                }
                if (valueActs)
                {
                    ++st.valueActionRegs;
                    if (WriteEffects(r.address, r.size, false, 0).empty())
                        pr.Add(std::string(r.name) + ": the handler acts on the value written (V: a latched action or cleared pending bits) and WriteEffects says nothing");
                }
                // A register that does two things on a write needs two texts: a text for one of them satisfies the check above for the other
                // (A5-74: PTMR's bit 0 starts drawing now, its bit 1 at the frame change).
                if (actions >= 2)
                {
                    ++st.valueActionRegs;
                    if (WriteEffects(r.address, r.size, false, 0).size() < actions)
                        pr.Add(std::string(r.name) + ": the handler does " + std::to_string(actions) + " things on a write (G, V, S, D, C) and WriteEffects says fewer");
                }
                // L (A5-77): the handler stores nothing of the high byte. A byte write there does not do what a byte write to the low byte does,
                // and its notes must not be the same.
                if (lowOnly && r.size == 2)
                {
                    ++st.valueActionRegs;
                    if (WriteEffects(r.address, 1, false, 0) == WriteEffects(r.address + 1, 1, false, 0))
                        pr.Add(std::string(r.name) + ": the handler stores nothing of the high byte (L) and WriteEffects says the same of both bytes");
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

// The SCSP's slot registers (32 slots of 16 words below 0x400): not in the register table, they are unnamed, but a handler label can act on
// the value there too -- word 0's KYONEX keys every slot (A5-55). Each label with V must have a WriteEffects text at every slot.
inline void CheckScspSlots(const Cases& c, Stats& st, Problems& pr)
{
    const Handler* h = c.Get("scsp.slot.wr");
    if (!h) { pr.Add("no handler scsp.slot.wr"); return; }
    for (unsigned label = 0; label < 16; ++label)
    {
        const Group* g = h->At(label);
        if (!g || !(g->Has('V') || g->Has('F'))) continue;   // F: words 12-15 store 0 whatever is written (A5-64)
        for (unsigned slot = 0; slot < 32; ++slot)
        {
            ++st.valueActionRegs;
            const uint32_t at = 0x05B00000u + slot * 0x20u + label * 2u;
            for (uint32_t width : { 2u, 4u })
                if (WriteEffects(at, width, false, 0).empty())
                    pr.Add("SCSP slot " + std::to_string(slot) + " word " + std::to_string(label) + " (" + Hex(at) + "), a " + std::to_string(width) +
                           "-byte write: the handler acts on the value written (V) and WriteEffects says nothing");
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
                // ...and the aligned long below 0xFFFFFF00 reaches at most the register at its own address, a read none (A5-31).
                if (!hi && w == 4)
                {
                    const size_t reached = CoveredRegisters(uint32_t(a), w, wr != 0).size();
                    if (reached > (wr ? 1u : 0u))
                        pr.Add("on-chip long " + std::string(wr ? "write" : "read") + " at " + Hex(uint32_t(a)) + " is said to reach " + std::to_string(reached) +
                               " registers, the handler (switch on A & 0xFF) reaches " + (wr ? "the one at its own address" : "none"));
                }
            }
        }
}

// Every misaligned word and long address of the on-chip area (A5-29). MemRead / MemWrite raise the address error and hand the
// handler the address unmasked, so the handler decodes what it is given: from 0xFFFFFF00 up it aligns the address itself (the
// mask in the cases file), below it it switches on A & 0xFF as it is. The register the label reaches must be one the decompiler
// says the access reaches, with the cells the table has for it checked as for an aligned probe; a register it says the access
// reaches that the handler does not, is a disagreement; and the address error is always said.
inline void CheckOnChipMisaligned(const Cases& c, const std::vector<Excused>& excused, Stats& st, Problems& pr)
{
    size_t n = 0;
    const HardwareRegister* regs = SaturnHardwareRegisters(n);
    const bool mis = c.bus.count("bus.misaligned") && c.bus.at("bus.misaligned").count("rd_pex") && c.bus.at("bus.misaligned").at("rd_pex") &&
                     c.bus.at("bus.misaligned").at("wr_pex");
    if (!mis) pr.Add("bus.misaligned: the bus does not raise an address error for a misaligned access (the cautions say it does)");
    for (uint64_t a64 = 0xFFFFFE00ull; a64 <= 0xFFFFFFFFull; ++a64)
    {
        const uint32_t a = uint32_t(a64);
        for (uint32_t w : { 2u, 4u })
        {
            if (!(a & (w - 1))) continue;
            for (int wr = 0; wr < 2; ++wr)
            {
                const bool hi = (a & 0x100u) != 0;
                const Handler* h = c.Get(std::string("onchip.") + (wr ? "wr" : "rd") + std::to_string(w * 8) + (hi ? ".hi" : ".lo"));
                if (!h) continue;
                ++st.misaligned;
                bool said = false;
                for (const NarrowEffect& e : NarrowAccessEffects(a, w, wr != 0))
                    said |= e.caution && e.text.find("address error") != std::string::npos;
                if (said != mis)
                    pr.Add("misaligned " + std::string(wr ? "write" : "read") + " of " + std::to_string(w) + " bytes at " + Hex(a) +
                           (said ? " is called an address error, the bus raises none" : " is not called an address error, the bus raises one"));
                // The handler's decoding of the address it is given.
                const unsigned label = hi ? (a & unsigned(h->mask)) : (a & 0xFFu);
                const Group* g = h->At(label);
                const HardwareRegister* target = nullptr;   // the register whose own label is in the label's group, when the case does something
                if (g && g->Has('M'))
                {
                    const HardwareRegister* sameGroup = nullptr;   // (a 16-bit register's second label, a copy of the divider)
                    for (size_t i = 0; i < n && !target; ++i)
                    {
                        const HardwareRegister& r = regs[i];
                        if (r.address < 0xFFFFFE00u || NotDecodedFamily(r.narrow) || !Allows(r, wr != 0) || ((r.address & 0x100u) != 0) != hi) continue;
                        const unsigned own = hi ? (r.address & unsigned(h->mask)) : (r.address & 0xFFu);
                        if (own == label) target = &r;
                        else if (!sameGroup && h->At(own) == g) sameGroup = &r;
                    }
                    if (!target) target = sameGroup;
                }
                // The decompiler: the registers it says the access reaches, each with its cell.
                uint32_t first = 0;
                const uint32_t reach = AccessAddress(a, w);
                HardwareAlias(reach, first);
                const std::vector<CoveredRegister> covered = CoveredRegisters(a, w, wr != 0);
                bool targetSaid = false;
                for (const CoveredRegister& cr : covered)
                {
                    if (cr.reg == target) targetSaid = true;
                    WidthProbe p{ cr.reg, w, wr != 0, int32_t(first) - int32_t(cr.reg->address), reach, cr.cell };
                    CheckOnChipProbe(c, p, excused, st, pr);
                }
                // Below 0xFFFFFF00 a long write is made on the register at the long's own address only and a long read reaches none (the
                // cases file: rd32.lo has no switch, wr32.lo switches on A & 0xFF): the decompiler may not say it reaches the others.
                if (!hi && w == 4 && covered.size() > (wr ? 1u : 0u))
                    pr.Add("misaligned " + std::string(wr ? "write" : "read") + " of 4 bytes at " + Hex(a) + ": the handler reaches " +
                           (wr ? "the register at its own address only" : "no register") + ", the decompiler says it reaches " + std::to_string(covered.size()));
                if (target && !targetSaid)
                    pr.Add("misaligned " + std::string(wr ? "write" : "read") + " of " + std::to_string(w) + " bytes at " + Hex(a) + ": the handler (" +
                           std::string("label 0x") + Hex(label, 2) + ") reaches " + target->name + ", the decompiler says the access reaches " +
                           (covered.empty() ? std::string("no register") : std::string(covered[0].reg->name)));
            }
        }
    }
}

// The values a byte access to a 32-bit register gets after its address error, and the halves' masks, that the cautions' texts state
// (A5-31): a byte write stores V | V << 8, a byte read returns bits 15-8; a long read below 0xFFFFFF00 reaches no register.
inline void CheckOnChipValueRules(const Cases& c, Problems& pr)
{
    auto need = [&](const char* name, const char* fact, int want) {
        auto g = c.bus.find(name);
        if (g == c.bus.end() || !g->second.count(fact)) { pr.Add(std::string("no fact ") + name + " " + fact); return; }
        if (g->second.at(fact) != want) pr.Add(std::string(name) + " " + fact + ": the cautions' text states " + std::to_string(want) + ", Mednafen has " + std::to_string(g->second.at(fact)));
    };
    need("bus.onchipbyte", "wr_dup", 1);
    need("bus.onchipbyte", "rd_shift8", 1);
    auto mask = [&](const char* key, long want) {
        const Handler* h = c.Get(key);
        if (!h) { pr.Add(std::string("no handler ") + key); return; }
        if (h->mask != want) pr.Add(std::string(key) + ": the address mask is " + std::to_string(h->mask) + ", AccessAddress assumes " + std::to_string(want));
    };
    // From 0xFFFFFF00 up the handlers align the address themselves (the same address AccessAddress makes); below it they take it as given.
    mask("onchip.rd16.hi", 0xFE); mask("onchip.wr16.hi", 0xFE); mask("onchip.rd32.hi", 0xFC); mask("onchip.wr32.hi", 0xFC);
    mask("onchip.rd8.hi", 0xFC); mask("onchip.wr8.hi", 0xFC);
    for (const char* k : { "onchip.rd8.lo", "onchip.wr8.lo", "onchip.rd16.lo", "onchip.wr16.lo", "onchip.rd32.lo", "onchip.wr32.lo" }) mask(k, -1);
    const Handler* r32 = c.Get("onchip.rd32.lo");
    if (!r32 || r32->sw || !r32->pex) pr.Add("onchip.rd32.lo: a long read below 0xFFFFFF00 is said to return 0 and read nothing, the handler decodes something");
    const Handler* w32 = c.Get("onchip.wr32.lo");
    if (!w32 || !w32->sw || !w32->pex) pr.Add("onchip.wr32.lo: a long write below 0xFFFFFF00 is said to reach the register at its own address, the handler is not a switch with an address error");
}
}  // namespace hhc
