// Hardware event text (HwEventFormat): golden messages for every kind, the name tables, the
// fallback for a kind this build has never heard of, and how the PC flags reach the Log entry.
// Pure: no ImGui, no driver.
#include <cstdio>
#include <string>

#include "HwEventFormat.h"

using namespace sfe;

namespace
{
int gFailures;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)

void CheckText(const char* what, const std::string& got, const char* want)
{
    if (got != want)
    {
        std::printf("FAIL %s\n   got:  %s\n   want: %s\n", what, got.c_str(), want);
        ++gFailures;
    }
}

SeHwEvent Ev(unsigned kind, unsigned a0 = 0, unsigned a1 = 0, unsigned a2 = 0, unsigned a3 = 0,
             unsigned cpu = SE_HWEV_CPU_NONE, unsigned flags = 0, unsigned pc = 0, unsigned source = 0)
{
    SeHwEvent e = SeHwEvent();
    e.kind = static_cast<unsigned char>(kind);
    e.source = static_cast<unsigned char>(source);
    e.cpu = static_cast<unsigned char>(cpu);
    e.flags = static_cast<unsigned char>(flags);
    e.frame = 77;
    e.ts = 1234;
    e.pc = pc;
    e.arg[0] = a0; e.arg[1] = a1; e.arg[2] = a2; e.arg[3] = a3;
    return e;
}

std::string Msg(const SeHwEvent& e) { return FormatHwEvent(e).message; }

bool DetailHas(const SeHwEvent& e, const char* key, const char* valueSubstr)
{
    for (const auto& kv : HwEventDetail(e))
        if (kv.first == key && kv.second.find(valueSubstr) != std::string::npos) return true;
    return false;
}

void Messages()
{
    // 1 SH2_EXCEPTION
    CheckText("int vblank-in", Msg(Ev(1, 0x40, SE_HWEV_EXC_INT, 15, 0x06004A20, 0)),
              "MSH2 interrupt SCU VBlank-IN (vec 0x40, level 15) -> handler 06004A20");
    CheckText("int on-chip", Msg(Ev(1, 0x6E, SE_HWEV_EXC_INT, 6, 0x06000100, 1)),
              "SSH2 interrupt on-chip/other (vec 0x6E, level 6) -> handler 06000100");
    CheckText("int external", Msg(Ev(1, 0x53, SE_HWEV_EXC_INT, 4, 0x06000200, 0)),
              "MSH2 interrupt SCU external 3 (vec 0x53, level 4) -> handler 06000200");
    CheckText("nmi", Msg(Ev(1, 11, SE_HWEV_EXC_NMI, 0, 0x06000300, 0)), "MSH2 NMI (vec 0x0B) -> handler 06000300");
    CheckText("cpu addr", Msg(Ev(1, 9, SE_HWEV_EXC_CPUADDR, 0, 0x06000400, 1)),
              "SSH2 CPU address error (vec 0x09) -> handler 06000400");
    CheckText("dma addr", Msg(Ev(1, 10, SE_HWEV_EXC_DMAADDR, 0, 0x06000500, 0)),
              "MSH2 DMA address error (vec 0x0A) -> handler 06000500");
    CheckText("illegal", Msg(Ev(1, 4, SE_HWEV_EXC_ILLINSTR, 0, 0x06000600, 0)),
              "MSH2 illegal instruction (vec 0x04) -> handler 06000600");
    CheckText("slot illegal", Msg(Ev(1, 6, SE_HWEV_EXC_ILLSLOT, 0, 0x06000700, 0)),
              "MSH2 slot illegal instruction (vec 0x06) -> handler 06000700");
    CheckText("trapa", Msg(Ev(1, 35, SE_HWEV_EXC_TRAP, 0, 0x06001000, 0)),
              "MSH2 TRAPA #35 (vec 0x23) -> handler 06001000");
    CheckText("trapa odd vector", Msg(Ev(1, 5, SE_HWEV_EXC_TRAP, 0, 0x06001000, 0)),
              "MSH2 TRAPA #5 (vec 0x05) -> handler 06001000");
    CheckText("unknown class", Msg(Ev(1, 1, 99, 0, 0x10, 0)), "MSH2 exception class 99 (vec 0x01) -> handler 00000010");
    // 2 SCU_INT_ACCEPT
    CheckText("accept", Msg(Ev(2, 0, 15, 0x40, 0)), "SCU interrupt VBlank-IN delivered (vec 0x40, level 15)");
    CheckText("accept ext", Msg(Ev(2, 17, 4, 0x51, 0)), "SCU interrupt External 1 delivered (vec 0x51, level 4)");
    CheckText("accept odd bit", Msg(Ev(2, 14, 1, 0x4E, 0)), "SCU interrupt bit 14 delivered (vec 0x4E, level 1)");
    // 3 SCU_INT_MASK
    CheckText("mask", Msg(Ev(3, 0xFFFF, 0xBFFF)), "SCU interrupt mask 0000BFFF -> 0000FFFF");
    // 4 SCU_DMA_START: level | indirect<<8 | sf<<16 | rb<<24 | wb<<28
    CheckText("dma start", Msg(Ev(4, 0x12010000u, 0x06000000, 0x05C00000, 0x1000)),
              "SCU DMA L0 start: C-bus (work RAM-H) 06000000 -> B-bus 05C00000, 0x1000 bytes, factor VBlank-OUT");
    CheckText("dma start a-bus", Msg(Ev(4, 0x10000001u, 0x02000000, 0x05A00000, 0x20)),
              "SCU DMA L1 start: A-bus 02000000 -> B-bus 05A00000, 0x20 bytes, factor VBlank-IN");
    CheckText("dma indirect", Msg(Ev(4, 0x12070101u, 0x06004000, 0x05A00000, 0x40)),
              "SCU DMA L1 start: indirect, C-bus (work RAM-H) 06004000 -> B-bus 05A00000, 0x40 bytes, factor enable bit (software)");
    CheckText("dma bad bus", Msg(Ev(4, 0xFF060000u, 0x00001000, 0x00002000, 4)),
              "SCU DMA L0 start: invalid bus 00001000 -> invalid bus 00002000, 0x4 bytes, factor Sprite draw end");
    // 5, 6
    CheckText("dma end", Msg(Ev(5, 1)), "SCU DMA L1 end");
    CheckText("dma illegal source", Msg(Ev(6, 0, 0x12345678, 0x06000000)),
              "SCU DMA L0 illegal source address 12345678 (destination 06000000)");
    // Side 1 (arg0 bit 8): the destination was refused; the text must show arg2, not arg1.
    CheckText("dma illegal destination", Msg(Ev(6, 0x102, 0x06000000, 0x0BADBAD0)),
              "SCU DMA L2 illegal destination address 0BADBAD0 (source 06000000)");
    // 7 DMAC_START: ch | CHCR<<8. CHCR 0x5A01 = long, SAR+, DAR+, auto request, DE.
    CheckText("dmac", Msg(Ev(7, 0x5A0100, 0x06000000, 0x25C00000, 0x100, 0, SE_HWEV_F_HAS_PC | SE_HWEV_F_PC_APPROX, 0x6004000)),
              "MSH2 DMAC ch0 start: 06000000 -> 25C00000, 256 x long (1024 bytes)");
    CheckText("dmac 16-byte", Msg(Ev(7, (0x0C00u << 8) | 1u, 0x1000, 0x2000, 4, 1)),
              "SSH2 DMAC ch1 start: 00001000 -> 00002000, 4 x 16-byte (64 bytes)");
    // 8
    CheckText("dmac end", Msg(Ev(8, 1, 0, 0, 0, 0)), "MSH2 DMAC ch1 end");
    // 9 CD command
    CheckText("cd play", Msg(Ev(9, 0x1080, 0x0001, 0x0F80, 0x0100)), "CD command Play (0x10)  CR 1080 0001 0F80 0100");
    CheckText("cd unknown", Msg(Ev(9, 0xAB00, 1, 2, 3)), "CD command unknown (0xAB)  CR AB00 0001 0002 0003");
    // 10 CD response
    CheckText("cd resp", Msg(Ev(10, 0x4300, 0x0101, 0x0002, 0)), "CD response: PLAY +DTREQ  CR 4300 0101 0002 0000");
    CheckText("cd resp periodic wait", Msg(Ev(10, 0xA102, 0, 0, 0)), "CD response: PAUSE +PERIODIC +WAIT  CR A102 0000 0000 0000");
    CheckText("cd rejected", Msg(Ev(10, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF)), "CD response: REJECTED  CR FFFF FFFF FFFF FFFF");
    CheckText("cd resp bad code", Msg(Ev(10, 0x0D00, 0, 0, 0)), "CD response: status 0xD  CR 0D00 0000 0000 0000");
    // 11 SMPC
    CheckText("smpc", Msg(Ev(11, 0x02)), "SMPC command SSHON (0x02)");
    CheckText("smpc intback", Msg(Ev(11, 0x10, 0x0801)), "SMPC command INTBACK (0x10)  IREG 01 08");
    CheckText("smpc high ireg", Msg(Ev(11, 0x16, 0, 0xAA00)), "SMPC command SETTIME (0x16)  IREG 00 00 00 00 00 AA");
    CheckText("smpc unknown", Msg(Ev(11, 0x55)), "SMPC command unknown (0x55)");
    // 12-17 VDP
    CheckText("vdp1 start", Msg(Ev(12, 1, 0)), "VDP1 draw start (FB 1)");
    CheckText("vdp1 start interrupted", Msg(Ev(12, 0, 1)), "VDP1 draw start (FB 0), previous draw interrupted");
    CheckText("vdp1 end", Msg(Ev(13, 0x120, 0)), "VDP1 draw end at command 00120 (END bit)");
    CheckText("vdp1 end aborted", Msg(Ev(13, 0x40, 1)), "VDP1 draw end at command 00040 (aborted by frame-buffer swap)");
    CheckText("swap", Msg(Ev(14, 1, 3, 0)), "VDP1 frame-buffer swap: displaying FB 1 (FBCR 0003, TVMR 0000)");
    CheckText("erase", Msg(Ev(15, 0, 0x8000, 0, 0x50DF)), "VDP1 frame-buffer erase FB 0 (EWDR 8000, EWLR 0000, EWRR 50DF)");
    CheckText("vblank in", Msg(Ev(16, 1)), "VDP2 VBlank IN");
    CheckText("vblank out", Msg(Ev(16, 0)), "VDP2 VBlank OUT");
    CheckText("display on", Msg(Ev(17, 1, 0x8000)), "VDP2 display ON (TVMD 8000)");
    CheckText("display off", Msg(Ev(17, 0, 0)), "VDP2 display OFF (TVMD 0000)");
    // 255 SUPPRESSED
    CheckText("suppressed", Msg(Ev(255, 137, 64, 0, 0, SE_HWEV_CPU_NONE, 0, 0, 4)),
              "... +137 more VDP1 events this frame (cap 64)");
    CheckText("suppressed source unknown", Msg(Ev(255, 1, 64, 0, 0, SE_HWEV_CPU_NONE, 0, 0, 200)),
              "... +1 more hardware events this frame (cap 64)");
    // Unknown kind: a newer emulator must still render.
    CheckText("unknown", Msg(Ev(99, 1, 2, 3, 4, SE_HWEV_CPU_NONE, 0, 0, 2)),
              "kind 0x63 00000001 00000002 00000003 00000004");
    // Lost row
    LogEvent lost = FormatHwLost(1234, 9);
    CheckText("lost", lost.message, "1234 hardware events lost (emulator ring / client queue)");
    CHECK(lost.category == LogCategory::Warning && lost.fromHw && lost.frame == 9 && !lost.hasSource);
}

void Categories()
{
    struct { unsigned kind; LogCategory cat; } k[] = {
        {1, LogCategory::CPU}, {2, LogCategory::CPU}, {3, LogCategory::CPU},
        {4, LogCategory::DMA}, {5, LogCategory::DMA}, {6, LogCategory::DMA}, {7, LogCategory::DMA}, {8, LogCategory::DMA},
        {9, LogCategory::CD}, {10, LogCategory::CD}, {11, LogCategory::SMPC},
        {12, LogCategory::VDP}, {13, LogCategory::VDP}, {14, LogCategory::VDP}, {15, LogCategory::VDP},
        {16, LogCategory::VDP}, {17, LogCategory::VDP},
    };
    for (const auto& c : k) CHECK(FormatHwEvent(Ev(c.kind)).category == c.cat);
    // SUPPRESSED and unknown kinds follow the source bit.
    const LogCategory bySource[8] = { LogCategory::CPU, LogCategory::CPU, LogCategory::DMA, LogCategory::DMA,
                                      LogCategory::VDP, LogCategory::VDP, LogCategory::CD, LogCategory::SMPC };
    for (unsigned s = 0; s < 8; ++s)
    {
        CHECK(FormatHwEvent(Ev(255, 1, 64, 0, 0, SE_HWEV_CPU_NONE, 0, 0, s)).category == bySource[s]);
        CHECK(FormatHwEvent(Ev(200, 0, 0, 0, 0, SE_HWEV_CPU_NONE, 0, 0, s)).category == bySource[s]);
    }
    CHECK(FormatHwEvent(Ev(200, 0, 0, 0, 0, SE_HWEV_CPU_NONE, 0, 0, 9)).category == LogCategory::Info);
}

void PcFlags()
{
    LogEvent none = FormatHwEvent(Ev(1, 0x40, 0, 15, 0x100, 0));
    CHECK(!none.hasSource && none.fromHw && none.hasRecord && none.frame == 77);

    LogEvent exact = FormatHwEvent(Ev(1, 0x40, 0, 15, 0x100, 1, SE_HWEV_F_HAS_PC, 0x06001234));
    CHECK(exact.hasSource && exact.cpu == 1 && exact.pc == 0x06001234 && !exact.pcApprox);
    CHECK(DetailHas(exact.hw, "PC", "06001234"));
    CHECK(!DetailHas(exact.hw, "PC", "approximate"));

    LogEvent approx = FormatHwEvent(Ev(7, 0x5A0100, 1, 2, 3, 0, SE_HWEV_F_HAS_PC | SE_HWEV_F_PC_APPROX, 0x06005678));
    CHECK(approx.hasSource && approx.cpu == 0 && approx.pc == 0x06005678 && approx.pcApprox);
    CHECK(DetailHas(approx.hw, "PC", "approximate"));
}

void Details()
{
    CHECK(DetailHas(Ev(7, 0x5A0100, 0x06000000, 0x25C00000, 0x100, 0), "SAR", "incrementing"));
    CHECK(DetailHas(Ev(7, 0x5A0100, 0, 0, 0x100, 0), "Request", "auto"));
    CHECK(DetailHas(Ev(7, 0x5A0100, 0, 0, 0x100, 0), "TCR", "1024 bytes"));
    CHECK(DetailHas(Ev(4, 0x12010000u, 1, 2, 3), "Start factor", "VBlank-OUT"));
    CHECK(DetailHas(Ev(3, 0x0001, 0x0003), "Changed", "VBlank-OUT"));
    CHECK(DetailHas(Ev(10, 0x4300), "Status byte", "43"));
    CHECK(DetailHas(Ev(11, 0x10, 0x0801), "IREG1", "08"));
    CHECK(DetailHas(Ev(99, 0xDEADBEEF), "arg0", "DEADBEEF"));
    CHECK(DetailHas(Ev(255, 5, 64), "Suppressed", "5"));
    // Every kind decodes to at least the kind and the cycle row.
    for (unsigned k = 1; k <= 17; ++k) CHECK(HwEventDetail(Ev(k)).size() >= 2);
}

void Tables()
{
    for (unsigned b = 0; b < 14; ++b) CHECK(HwScuInterruptName(b) != nullptr);
    CHECK(HwScuInterruptName(14) == nullptr && HwScuInterruptName(15) == nullptr && HwScuInterruptName(40) == nullptr);
    CHECK(std::string(HwScuInterruptName(0)) == "VBlank-IN");
    CHECK(std::string(HwScuInterruptName(13)) == "Sprite draw end");
    for (unsigned f = 0; f < 8; ++f) CHECK(HwScuDmaFactorName(f) != nullptr);
    CHECK(HwScuDmaFactorName(8) == nullptr);
    for (unsigned b = 0; b < 3; ++b) CHECK(HwScuBusName(b) != nullptr);
    CHECK(HwScuBusName(3) == nullptr && HwScuBusName(0xF) == nullptr);
    for (unsigned s = 0; s < 8; ++s) CHECK(HwSourceName(s) != nullptr);
    CHECK(HwSourceName(8) == nullptr);
    CheckText("vec 0", HwSh2VectorName(0), "power-on reset");
    CheckText("vec 2", HwSh2VectorName(2), "manual reset");
    CheckText("vec 4", HwSh2VectorName(4), "general illegal instruction");
    CheckText("vec 6", HwSh2VectorName(6), "slot illegal instruction");
    CheckText("vec 9", HwSh2VectorName(9), "CPU address error");
    CheckText("vec 10", HwSh2VectorName(10), "DMA address error");
    CheckText("vec 11", HwSh2VectorName(11), "NMI");
    CheckText("vec 12", HwSh2VectorName(12), "user break");
    CheckText("vec 32", HwSh2VectorName(32), "TRAPA #32");
    CheckText("vec 63", HwSh2VectorName(63), "TRAPA #63");
    CheckText("vec 0x41", HwSh2VectorName(0x41), "SCU VBlank-OUT");
    CheckText("vec 0x4B", HwSh2VectorName(0x4B), "SCU Level 0 DMA end");
    CheckText("vec 0x5F", HwSh2VectorName(0x5F), "SCU external 15");
    CHECK(HwSh2VectorName(0x4E).empty() && HwSh2VectorName(0x60).empty() && HwSh2VectorName(1).empty());
    for (unsigned v = 0x40; v <= 0x4D; ++v) CHECK(!HwSh2VectorName(v).empty());

    // Every CD command the emulator decodes, and nothing else.
    const unsigned cd[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x10, 0x11, 0x12, 0x20, 0x30, 0x31, 0x32,
                            0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x50, 0x51, 0x52, 0x53, 0x54,
                            0x55, 0x56, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x70, 0x71, 0x72, 0x73,
                            0x74, 0x75, 0xE0, 0xE1 };
    unsigned known = 0;
    for (unsigned c = 0; c < 256; ++c) if (HwCdCommandName(c)) ++known;
    for (unsigned c : cd) CHECK(HwCdCommandName(c) != nullptr);
    CHECK(known == sizeof(cd) / sizeof(cd[0]));
    CHECK(std::string(HwCdCommandName(0x63)) == "Get and Delete Sector Data");
    for (unsigned s = 0; s <= 0x0A; ++s) CHECK(HwCdStatusName(s) != nullptr);
    CHECK(HwCdStatusName(0x0B) == nullptr);
    CHECK(std::string(HwCdStatusName(0x0A)) == "FATAL");

    const unsigned smpc[] = { 0x00, 0x02, 0x03, 0x06, 0x07, 0x08, 0x09, 0x0D, 0x0E, 0x0F, 0x10, 0x16, 0x17, 0x18, 0x19, 0x1A };
    known = 0;
    for (unsigned c = 0; c < 256; ++c) if (HwSmpcCommandName(c)) ++known;
    for (unsigned c : smpc) CHECK(HwSmpcCommandName(c) != nullptr);
    CHECK(known == sizeof(smpc) / sizeof(smpc[0]));
    CHECK(std::string(HwSmpcCommandName(0x03)) == "SSHOFF");
}
}  // namespace

int main()
{
    Messages();
    Categories();
    PcFlags();
    Details();
    Tables();
    if (gFailures == 0) std::printf("HwEventFormat: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
