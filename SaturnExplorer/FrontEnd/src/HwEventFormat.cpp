#include "HwEventFormat.h"

#include <cstdarg>
#include <cstdio>

namespace sfe
{
namespace
{

std::string Fmt(const char* f, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

const char* CpuName(unsigned cpu)
{
    return cpu == 0 ? "MSH2" : cpu == 1 ? "SSH2" : "";
}

// scu.inc IntNames, spelled out. Bits 14 and 15 do not exist on the SCU.
const char* const kScuInt[14] = {
    "VBlank-IN", "VBlank-OUT", "HBlank-IN", "Timer 0", "Timer 1", "DSP end", "Sound request",
    "System manager", "PAD", "Level 2 DMA end", "Level 1 DMA end", "Level 0 DMA end",
    "DMA illegal", "Sprite draw end"
};

// sf_to_int_tab in scu.inc: factor 0..6 are interrupts; 7 is software setting the enable bit.
const char* const kScuFactor[8] = {
    "VBlank-IN", "VBlank-OUT", "HBlank-IN", "Timer 0", "Timer 1", "Sound request",
    "Sprite draw end", "enable bit (software)"
};

const char* const kScuBus[3] = { "A-bus", "B-bus", "C-bus (work RAM-H)" };

const char* const kSource[8] = {
    "CPU exception", "SCU interrupt", "SCU DMA", "DMAC", "VDP1", "VDP2", "CD block", "SMPC"
};

struct Named { unsigned id; const char* name; };

// cdb.cpp GetCommandDetails.
const Named kCdCommands[] = {
    {0x00, "Get CD Status"}, {0x01, "Get Hardware Info"}, {0x02, "Get TOC"},
    {0x03, "Get Session Info"}, {0x04, "Initialize"}, {0x05, "Open Tray"},
    {0x06, "End Data Transfer"}, {0x10, "Play"}, {0x11, "Seek"}, {0x12, "Scan"},
    {0x20, "Get Subcode"}, {0x30, "Set CD Device Connection"},
    {0x31, "Get CD Device Connection"}, {0x32, "Get Last Buffer Destination"},
    {0x40, "Set Filter Range"}, {0x41, "Get Filter Range"},
    {0x42, "Set Filter Subheader Conditions"}, {0x43, "Get Filter Subheader Conditions"},
    {0x44, "Set Filter Mode"}, {0x45, "Get Filter Mode"}, {0x46, "Set Filter Connection"},
    {0x47, "Get Filter Connection"}, {0x48, "Reset Selector"}, {0x50, "Get Buffer Size"},
    {0x51, "Get Sector Number"}, {0x52, "Calculate Actual Size"}, {0x53, "Get Actual Size"},
    {0x54, "Get Sector Info"}, {0x55, "Execute FAD Search"}, {0x56, "Get FAD Search Results"},
    {0x60, "Set Sector Length"}, {0x61, "Get Sector Data"}, {0x62, "Delete Sector Data"},
    {0x63, "Get and Delete Sector Data"}, {0x64, "Put Sector Data"},
    {0x65, "Copy Sector Data"}, {0x66, "Move Sector Data"}, {0x67, "Get Copy Error"},
    {0x70, "Change Directory"}, {0x71, "Read Directory"}, {0x72, "Get Filesystem Scope"},
    {0x73, "Get File Info"}, {0x74, "Read File"}, {0x75, "Abort File"},
    {0xE0, "Authenticate Device"}, {0xE1, "Get Device Authentication Status"},
};

// cdb.cpp STATUS_*.
const char* const kCdStatus[11] = {
    "BUSY", "PAUSE", "STANDBY", "PLAY", "SEEK", "SCAN", "OPEN", "NODISC", "RETRY", "ERROR", "FATAL"
};

// smpc.cpp CMD_*.
const Named kSmpcCommands[] = {
    {0x00, "MSHON"}, {0x02, "SSHON"}, {0x03, "SSHOFF"}, {0x06, "SNDON"}, {0x07, "SNDOFF"},
    {0x08, "CDON"}, {0x09, "CDOFF"}, {0x0D, "SYSRES"}, {0x0E, "CKCHG352"}, {0x0F, "CKCHG320"},
    {0x10, "INTBACK"}, {0x16, "SETTIME"}, {0x17, "SETSMEM"}, {0x18, "NMIREQ"},
    {0x19, "RESENAB"}, {0x1A, "RESDISA"},
};

// The fixed SH-2 vector numbers (sh7095.h VECNUM_*).
const char* FixedVectorName(unsigned v)
{
    switch (v)
    {
        case 0:  return "power-on reset";
        case 2:  return "manual reset";
        case 4:  return "general illegal instruction";
        case 6:  return "slot illegal instruction";
        case 9:  return "CPU address error";
        case 10: return "DMA address error";
        case 11: return "NMI";
        case 12: return "user break";
        default: return nullptr;
    }
}

std::string Bits(const unsigned* bytes, int n)
{
    std::string s;
    for (int i = 0; i < n; ++i) s += Fmt(i ? " %02X" : "%02X", bytes[i]);
    return s;
}

std::string MaskNames(uint32_t m)
{
    std::string s;
    for (unsigned b = 0; b < 32; ++b)
        if (m & (1u << b))
        {
            const char* n = HwScuInterruptName(b);
            if (!s.empty()) s += ", ";
            s += n ? std::string(n) : Fmt("bit %u", b);
        }
    return s.empty() ? "(none)" : s;
}

const char* AddrMode(unsigned m)
{
    return m == 0 ? "fixed" : m == 1 ? "incrementing" : m == 2 ? "decrementing" : "reserved";
}

// SH7095 CHCR (sh7095.inc: TS = bits 11:10, SM = 13:12, DM = 15:14; 0x200 = auto request).
unsigned DmacUnit(uint32_t chcr)
{
    static const unsigned kUnit[4] = { 1, 2, 4, 16 };
    return kUnit[(chcr >> 10) & 3];
}
const char* DmacUnitName(uint32_t chcr)
{
    static const char* const k[4] = { "byte", "word", "long", "16-byte" };
    return k[(chcr >> 10) & 3];
}

}  // namespace

const char* HwScuInterruptName(unsigned bit)
{
    if (bit < 14) return kScuInt[bit];
    return nullptr;
}

const char* HwScuDmaFactorName(unsigned f) { return f < 8 ? kScuFactor[f] : nullptr; }
const char* HwScuBusName(unsigned b) { return b < 3 ? kScuBus[b] : nullptr; }
const char* HwSourceName(unsigned s) { return s < 8 ? kSource[s] : nullptr; }

std::string HwSh2VectorName(unsigned v)
{
    if (const char* f = FixedVectorName(v)) return f;
    // TRAPA #imm takes vector imm itself (the immediate IS the vector number), so no offset.
    if (v >= 32 && v < 64) return Fmt("TRAPA #%u", v);
    if (v >= 0x40 && v <= 0x4D) return std::string("SCU ") + kScuInt[v - 0x40];
    if (v >= 0x50 && v <= 0x5F) return Fmt("SCU external %u", v - 0x50);
    return std::string();
}

const char* HwCdCommandName(unsigned c)
{
    for (const Named& n : kCdCommands) if (n.id == c) return n.name;
    return nullptr;
}

const char* HwCdStatusName(unsigned code) { return code < 11 ? kCdStatus[code] : nullptr; }

const char* HwSmpcCommandName(unsigned c)
{
    for (const Named& n : kSmpcCommands) if (n.id == c) return n.name;
    return nullptr;
}

LogCategory HwEventCategory(const SeHwEvent& e)
{
    switch (e.kind)
    {
        case SE_HWEV_K_SH2_EXCEPTION: case SE_HWEV_K_SCU_INT_ACCEPT: case SE_HWEV_K_SCU_INT_MASK:
            return LogCategory::CPU;
        case SE_HWEV_K_SCU_DMA_START: case SE_HWEV_K_SCU_DMA_END: case SE_HWEV_K_SCU_DMA_ILLEGAL:
        case SE_HWEV_K_DMAC_START: case SE_HWEV_K_DMAC_END:
            return LogCategory::DMA;
        case SE_HWEV_K_CD_COMMAND: case SE_HWEV_K_CD_RESPONSE:
            return LogCategory::CD;
        case SE_HWEV_K_SMPC_COMMAND:
            return LogCategory::SMPC;
        case SE_HWEV_K_VDP1_DRAW_START: case SE_HWEV_K_VDP1_DRAW_END: case SE_HWEV_K_VDP1_FB_SWAP:
        case SE_HWEV_K_VDP1_FB_ERASE: case SE_HWEV_K_VDP2_VBLANK: case SE_HWEV_K_VDP2_DISPLAY:
            return LogCategory::VDP;
        default: break;
    }
    // Unknown kind or SUPPRESSED: the source bit says which chip the events belonged to.
    switch (e.source)
    {
        case 0: case 1: return LogCategory::CPU;
        case 2: case 3: return LogCategory::DMA;
        case 4: case 5: return LogCategory::VDP;
        case 6: return LogCategory::CD;
        case 7: return LogCategory::SMPC;
        default: return LogCategory::Info;
    }
}

static std::string Message(const SeHwEvent& e)
{
    const uint32_t* a = e.arg;
    switch (e.kind)
    {
        case SE_HWEV_K_SH2_EXCEPTION:
        {
            const char* cpu = CpuName(e.cpu);
            std::string what;
            switch (a[1])
            {
                case SE_HWEV_EXC_INT:
                {
                    const std::string n = HwSh2VectorName(a[0]);
                    what = "interrupt " + (n.empty() ? std::string("on-chip/other") : n) +
                           Fmt(" (vec 0x%02X, level %u)", a[0], a[2]);
                    break;
                }
                case SE_HWEV_EXC_NMI:      what = Fmt("NMI (vec 0x%02X)", a[0]); break;
                case SE_HWEV_EXC_CPUADDR:  what = Fmt("CPU address error (vec 0x%02X)", a[0]); break;
                case SE_HWEV_EXC_DMAADDR:  what = Fmt("DMA address error (vec 0x%02X)", a[0]); break;
                case SE_HWEV_EXC_ILLINSTR: what = Fmt("illegal instruction (vec 0x%02X)", a[0]); break;
                case SE_HWEV_EXC_ILLSLOT:  what = Fmt("slot illegal instruction (vec 0x%02X)", a[0]); break;
                case SE_HWEV_EXC_TRAP:
                    // arg0 is the instruction's immediate, which is also the vector it fetched.
                    what = Fmt("TRAPA #%u (vec 0x%02X)", a[0], a[0]);
                    break;
                default: what = Fmt("exception class %u (vec 0x%02X)", a[1], a[0]); break;
            }
            return std::string(cpu) + (*cpu ? " " : "") + what + Fmt(" -> handler %08X", a[3]);
        }
        case SE_HWEV_K_SCU_INT_ACCEPT:
        {
            const char* n = HwScuInterruptName(a[0]);
            const std::string src = n ? std::string(n)
                                      : (a[0] >= 16 && a[0] < 32) ? Fmt("External %u", a[0] - 16)
                                                                  : Fmt("bit %u", a[0]);
            return "SCU interrupt " + src + Fmt(" delivered (vec 0x%02X, level %u)", a[2], a[1]);
        }
        case SE_HWEV_K_SCU_INT_MASK:
            return Fmt("SCU interrupt mask %08X -> %08X", a[1], a[0]);
        case SE_HWEV_K_SCU_DMA_START:
        {
            const unsigned level = a[0] & 0xFF, indirect = (a[0] >> 8) & 0xFF;
            const unsigned sf = (a[0] >> 16) & 0xFF, rb = (a[0] >> 24) & 0xF, wb = (a[0] >> 28) & 0xF;
            const char* rn = HwScuBusName(rb);
            const char* wn = HwScuBusName(wb);
            const char* fn = HwScuDmaFactorName(sf);
            std::string s = Fmt("SCU DMA L%u start: ", level);
            // An indirect transfer reports one event per table entry, with that entry's own source,
            // destination and count; the table's address is not captured by the hook.
            if (indirect) s += "indirect, ";
            s += Fmt("%s %08X -> %s %08X, 0x%X bytes", rn ? rn : "invalid bus", a[1],
                     wn ? wn : "invalid bus", a[2], a[3]);
            s += std::string(", factor ") + (fn ? fn : "?");
            if (!fn) s += Fmt(" %u", sf);
            return s;
        }
        case SE_HWEV_K_SCU_DMA_END:     return Fmt("SCU DMA L%u end", a[0]);
        case SE_HWEV_K_SCU_DMA_ILLEGAL:
        {
            // arg0 bit 8 says which side the SCU refused; both addresses are always carried, so the refused one
            // is picked by side and the other is shown for context.
            const bool dst = ((a[0] >> 8) & 1) != 0;
            return Fmt("SCU DMA L%u illegal %s address %08X (%s %08X)", a[0] & 0xFF, dst ? "destination" : "source",
                       dst ? a[2] : a[1], dst ? "source" : "destination", dst ? a[1] : a[2]);
        }
        case SE_HWEV_K_DMAC_START:
        {
            const unsigned ch = a[0] & 0xFF;
            const uint32_t chcr = a[0] >> 8;
            return Fmt("%s DMAC ch%u start: %08X -> %08X, %u x %s (%u bytes)", CpuName(e.cpu), ch, a[1], a[2],
                       a[3], DmacUnitName(chcr), a[3] * DmacUnit(chcr));
        }
        case SE_HWEV_K_DMAC_END: return Fmt("%s DMAC ch%u end", CpuName(e.cpu), a[0]);
        case SE_HWEV_K_CD_COMMAND:
        {
            const unsigned cmd = (a[0] >> 8) & 0xFF;
            const char* n = HwCdCommandName(cmd);
            return std::string("CD command ") + (n ? n : "unknown") + Fmt(" (0x%02X)  CR %04X %04X %04X %04X", cmd,
                       a[0] & 0xFFFF, a[1] & 0xFFFF, a[2] & 0xFFFF, a[3] & 0xFFFF);
        }
        case SE_HWEV_K_CD_RESPONSE:
        {
            const unsigned st = (a[0] >> 8) & 0xFF;
            std::string s = "CD response: ";
            if (st == 0xFF) s += "REJECTED";
            else
            {
                const char* n = HwCdStatusName(st & 0x0F);
                s += n ? std::string(n) : Fmt("status 0x%X", st & 0x0F);
                if (st & 0x20) s += " +PERIODIC";
                if (st & 0x40) s += " +DTREQ";
                if (st & 0x80) s += " +WAIT";
                if (st & 0x10) s += " +0x10";
            }
            return s + Fmt("  CR %04X %04X %04X %04X", a[0] & 0xFFFF, a[1] & 0xFFFF, a[2] & 0xFFFF, a[3] & 0xFFFF);
        }
        case SE_HWEV_K_SMPC_COMMAND:
        {
            const char* n = HwSmpcCommandName(a[0]);
            std::string s = std::string("SMPC command ") + (n ? n : "unknown") + Fmt(" (0x%02X)", a[0] & 0xFF);
            // IREG0..3 are packed into arg1 (IREG0 lowest) and IREG4..6 into arg2.
            unsigned ireg[7];
            for (int i = 0; i < 4; ++i) ireg[i] = (a[1] >> (8 * i)) & 0xFF;
            for (int i = 0; i < 3; ++i) ireg[4 + i] = (a[2] >> (8 * i)) & 0xFF;
            int used = 7;
            while (used > 0 && ireg[used - 1] == 0) --used;   // trailing zeros are noise
            if (used) s += "  IREG " + Bits(ireg, used);
            return s;
        }
        case SE_HWEV_K_VDP1_DRAW_START:
            return Fmt("VDP1 draw start (FB %u)", a[0]) + (a[1] ? ", previous draw interrupted" : "");
        case SE_HWEV_K_VDP1_DRAW_END:
            return Fmt("VDP1 draw end at command %05X (%s)", a[0], a[1] ? "aborted by frame-buffer swap" : "END bit");
        case SE_HWEV_K_VDP1_FB_SWAP:
            return Fmt("VDP1 frame-buffer swap: displaying FB %u (FBCR %04X, TVMR %04X)", a[0], a[1] & 0xFFFF, a[2] & 0xFFFF);
        case SE_HWEV_K_VDP1_FB_ERASE:
            return Fmt("VDP1 frame-buffer erase FB %u (EWDR %04X, EWLR %04X, EWRR %04X)", a[0], a[1] & 0xFFFF,
                       a[2] & 0xFFFF, a[3] & 0xFFFF);
        case SE_HWEV_K_VDP2_VBLANK: return a[0] ? "VDP2 VBlank IN" : "VDP2 VBlank OUT";
        case SE_HWEV_K_VDP2_DISPLAY:
            return Fmt("VDP2 display %s (TVMD %04X)", a[0] ? "ON" : "OFF", a[1] & 0xFFFF);
        case SE_HWEV_K_SUPPRESSED:
        {
            const char* s = HwSourceName(e.source);
            return Fmt("... +%u more %s events this frame (cap %u)", a[0], s ? s : "hardware", a[1]);
        }
        default:
            return Fmt("kind 0x%02X %08X %08X %08X %08X", e.kind, a[0], a[1], a[2], a[3]);
    }
}

LogEvent FormatHwEvent(const SeHwEvent& e)
{
    LogEvent le;
    le.category = HwEventCategory(e);
    le.message = Message(e);
    le.frame = e.frame;
    le.fromHw = true;
    le.hasRecord = true;
    le.hw = e;
    if (e.flags & SE_HWEV_F_HAS_PC)
    {
        le.hasSource = true;
        le.cpu = e.cpu == 1 ? 1 : 0;
        le.pc = e.pc;
        le.pcApprox = (e.flags & SE_HWEV_F_PC_APPROX) != 0;
    }
    return le;
}

LogEvent FormatHwLost(uint64_t count, uint32_t frame)
{
    LogEvent le;
    le.category = LogCategory::Warning;
    le.message = Fmt("%llu hardware events lost (emulator ring / client queue)", static_cast<unsigned long long>(count));
    le.frame = frame;
    le.fromHw = true;
    return le;
}

std::vector<std::pair<std::string, std::string>> HwEventDetail(const SeHwEvent& e)
{
    typedef std::vector<std::pair<std::string, std::string>> Rows;
    Rows r;
    const uint32_t* a = e.arg;
    r.emplace_back("Kind", Fmt("%u", e.kind));
    r.emplace_back("Cycle in frame", Fmt("%u (master SH-2 clock)", e.ts));
    if (e.flags & SE_HWEV_F_HAS_PC)
        r.emplace_back("PC", Fmt("%08X%s", e.pc, (e.flags & SE_HWEV_F_PC_APPROX)
                                 ? " (approximate: where the CPU was when the event was noticed)" : ""));
    switch (e.kind)
    {
        case SE_HWEV_K_SH2_EXCEPTION:
        {
            r.emplace_back("Vector", Fmt("0x%02X %s", a[0], HwSh2VectorName(a[0]).c_str()));
            r.emplace_back("Class", Fmt("%u", a[1]));
            if (a[1] == SE_HWEV_EXC_INT) r.emplace_back("Level", Fmt("%u", a[2]));
            r.emplace_back("Handler", Fmt("%08X", a[3]));
            break;
        }
        case SE_HWEV_K_SCU_INT_ACCEPT:
            r.emplace_back("Source bit", Fmt("%u", a[0]));
            r.emplace_back("Level", Fmt("%u", a[1]));
            r.emplace_back("Vector", Fmt("0x%02X", a[2]));
            r.emplace_back("Pending after", Fmt("%08X (%s)", a[3], MaskNames(a[3]).c_str()));
            break;
        case SE_HWEV_K_SCU_INT_MASK:
            r.emplace_back("New mask", Fmt("%08X", a[0]));
            r.emplace_back("Old mask", Fmt("%08X", a[1]));
            r.emplace_back("Masked now", MaskNames(a[0]));
            r.emplace_back("Changed", MaskNames(a[0] ^ a[1]));
            break;
        case SE_HWEV_K_SCU_DMA_START:
        {
            const unsigned sf = (a[0] >> 16) & 0xFF;
            const char* rn = HwScuBusName((a[0] >> 24) & 0xF);
            const char* wn = HwScuBusName((a[0] >> 28) & 0xF);
            r.emplace_back("Level", Fmt("%u", a[0] & 0xFF));
            r.emplace_back("Mode", ((a[0] >> 8) & 0xFF) ? "indirect (one table entry; the table address is not captured)" : "direct");
            r.emplace_back("Start factor", Fmt("%u %s", sf, HwScuDmaFactorName(sf) ? HwScuDmaFactorName(sf) : "?"));
            r.emplace_back("Read bus", rn ? rn : "invalid");
            r.emplace_back("Write bus", wn ? wn : "invalid");
            r.emplace_back("Source", Fmt("%08X", a[1]));
            r.emplace_back("Destination", Fmt("%08X", a[2]));
            r.emplace_back("Bytes", Fmt("0x%X (%u)", a[3], a[3]));
            break;
        }
        case SE_HWEV_K_SCU_DMA_END: r.emplace_back("Level", Fmt("%u", a[0])); break;
        case SE_HWEV_K_SCU_DMA_ILLEGAL:
        {
            const bool dst = ((a[0] >> 8) & 1) != 0;
            r.emplace_back("Level", Fmt("%u", a[0] & 0xFF));
            r.emplace_back("Refused side", dst ? "destination" : "source");
            r.emplace_back("Illegal address", Fmt("%08X", dst ? a[2] : a[1]));
            r.emplace_back(dst ? "Source" : "Destination", Fmt("%08X", dst ? a[1] : a[2]));
            break;
        }
        case SE_HWEV_K_DMAC_START:
        {
            const uint32_t chcr = a[0] >> 8;
            r.emplace_back("Channel", Fmt("%u", a[0] & 0xFF));
            r.emplace_back("CHCR", Fmt("%06X", chcr));
            r.emplace_back("SAR", Fmt("%08X (%s)", a[1], AddrMode((chcr >> 12) & 3)));
            r.emplace_back("DAR", Fmt("%08X (%s)", a[2], AddrMode((chcr >> 14) & 3)));
            r.emplace_back("TCR", Fmt("0x%X x %s = %u bytes", a[3], DmacUnitName(chcr), a[3] * DmacUnit(chcr)));
            r.emplace_back("Request", (chcr & 0x200) ? "auto" : "module");
            r.emplace_back("Bus mode", (chcr & 0x10) ? "burst" : "cycle steal");
            r.emplace_back("Address mode", (chcr & 0x08) ? "single" : "dual");
            r.emplace_back("Interrupt on end", (chcr & 0x04) ? "yes" : "no");
            r.emplace_back("DE", (chcr & 0x01) ? "enabled" : "disabled");
            break;
        }
        case SE_HWEV_K_DMAC_END: r.emplace_back("Channel", Fmt("%u", a[0])); break;
        case SE_HWEV_K_CD_COMMAND:
            for (int i = 0; i < 4; ++i) r.emplace_back(Fmt("CR%d", i + 1), Fmt("%04X", a[i] & 0xFFFF));
            break;
        case SE_HWEV_K_CD_RESPONSE:
            for (int i = 0; i < 4; ++i) r.emplace_back(Fmt("Result%d", i), Fmt("%04X", a[i] & 0xFFFF));
            r.emplace_back("Status byte", Fmt("%02X", (a[0] >> 8) & 0xFF));
            break;
        case SE_HWEV_K_SMPC_COMMAND:
        {
            r.emplace_back("Command", Fmt("0x%02X", a[0] & 0xFF));
            for (int i = 0; i < 4; ++i) r.emplace_back(Fmt("IREG%d", i), Fmt("%02X", (a[1] >> (8 * i)) & 0xFF));
            for (int i = 0; i < 3; ++i) r.emplace_back(Fmt("IREG%d", 4 + i), Fmt("%02X", (a[2] >> (8 * i)) & 0xFF));
            break;
        }
        case SE_HWEV_K_VDP1_DRAW_START:
            r.emplace_back("Draw frame buffer", Fmt("%u", a[0]));
            r.emplace_back("Previous draw interrupted", a[1] ? "yes" : "no");
            break;
        case SE_HWEV_K_VDP1_DRAW_END:
            r.emplace_back("Last command address", Fmt("%05X", a[0]));
            r.emplace_back("Reason", a[1] ? "aborted by swap" : "END bit");
            break;
        case SE_HWEV_K_VDP1_FB_SWAP:
            r.emplace_back("Displayed FB", Fmt("%u", a[0]));
            r.emplace_back("FBCR", Fmt("%04X", a[1] & 0xFFFF));
            r.emplace_back("TVMR", Fmt("%04X", a[2] & 0xFFFF));
            break;
        case SE_HWEV_K_VDP1_FB_ERASE:
            r.emplace_back("FB", Fmt("%u", a[0]));
            r.emplace_back("EWDR", Fmt("%04X", a[1] & 0xFFFF));
            r.emplace_back("EWLR", Fmt("%04X", a[2] & 0xFFFF));
            r.emplace_back("EWRR", Fmt("%04X", a[3] & 0xFFFF));
            break;
        case SE_HWEV_K_VDP2_VBLANK: break;
        case SE_HWEV_K_VDP2_DISPLAY: r.emplace_back("TVMD", Fmt("%04X", a[1] & 0xFFFF)); break;
        case SE_HWEV_K_SUPPRESSED:
            r.emplace_back("Suppressed", Fmt("%u", a[0]));
            r.emplace_back("Per-frame cap", Fmt("%u", a[1]));
            break;
        default:
            for (int i = 0; i < 4; ++i) r.emplace_back(Fmt("arg%d", i), Fmt("%08X", a[i]));
            break;
    }
    return r;
}

}  // namespace sfe
