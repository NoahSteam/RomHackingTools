#include "Decompiler/SaturnHardwareMap.h"

#include <algorithm>
#include <vector>

namespace sfe
{
namespace decomp
{

namespace
{
struct Reg
{
    uint16_t offset;
    const char* name;
};

struct Block
{
    const char* chip;
    uint32_t base;
    uint8_t size;
    const Reg* regs;
    size_t count;
};

// SMPC: byte registers at odd addresses (Sega "SMPC User's Manual").
const Reg kSmpc[] = {
    { 0x001, "SMPC_IREG0" }, { 0x003, "SMPC_IREG1" }, { 0x005, "SMPC_IREG2" },
    { 0x007, "SMPC_IREG3" }, { 0x009, "SMPC_IREG4" }, { 0x00B, "SMPC_IREG5" },
    { 0x00D, "SMPC_IREG6" }, { 0x01F, "SMPC_COMREG" }, { 0x021, "SMPC_OREG0" },
    { 0x023, "SMPC_OREG1" }, { 0x025, "SMPC_OREG2" }, { 0x027, "SMPC_OREG3" },
    { 0x029, "SMPC_OREG4" }, { 0x02B, "SMPC_OREG5" }, { 0x02D, "SMPC_OREG6" },
    { 0x02F, "SMPC_OREG7" }, { 0x031, "SMPC_OREG8" }, { 0x033, "SMPC_OREG9" },
    { 0x035, "SMPC_OREG10" }, { 0x037, "SMPC_OREG11" }, { 0x039, "SMPC_OREG12" },
    { 0x03B, "SMPC_OREG13" }, { 0x03D, "SMPC_OREG14" }, { 0x03F, "SMPC_OREG15" },
    { 0x041, "SMPC_OREG16" }, { 0x043, "SMPC_OREG17" }, { 0x045, "SMPC_OREG18" },
    { 0x047, "SMPC_OREG19" }, { 0x049, "SMPC_OREG20" }, { 0x04B, "SMPC_OREG21" },
    { 0x04D, "SMPC_OREG22" }, { 0x04F, "SMPC_OREG23" }, { 0x051, "SMPC_OREG24" },
    { 0x053, "SMPC_OREG25" }, { 0x055, "SMPC_OREG26" }, { 0x057, "SMPC_OREG27" },
    { 0x059, "SMPC_OREG28" }, { 0x05B, "SMPC_OREG29" }, { 0x05D, "SMPC_OREG30" },
    { 0x05F, "SMPC_OREG31" }, { 0x061, "SMPC_SR" }, { 0x063, "SMPC_SF" }, { 0x075, "SMPC_PDR1" },
    { 0x077, "SMPC_PDR2" }, { 0x079, "SMPC_DDR1" }, { 0x07B, "SMPC_DDR2" }, { 0x07D, "SMPC_IOSEL" },
    { 0x07F, "SMPC_EXLE" },
};

// SCSP common control registers, named after the fields they hold ("SCSP User's Manual"). The
// per-slot block below 0x400 is left unnamed: 32 copies of one layout read better as offsets.
const Reg kScsp[] = {
    { 0x400, "SCSP_MVOL" }, { 0x402, "SCSP_RBL_RBP" }, { 0x404, "SCSP_MIBUF" },
    { 0x406, "SCSP_MOBUF" }, { 0x408, "SCSP_MSLC_CA" }, { 0x412, "SCSP_DMEAL" },
    { 0x414, "SCSP_DMEAH_DRGA" }, { 0x416, "SCSP_DTLG" }, { 0x418, "SCSP_TIMA" },
    { 0x41A, "SCSP_TIMB" }, { 0x41C, "SCSP_TIMC" }, { 0x41E, "SCSP_SCIEB" },
    { 0x420, "SCSP_SCIPD" }, { 0x422, "SCSP_SCIRE" }, { 0x424, "SCSP_SCILV0" },
    { 0x426, "SCSP_SCILV1" }, { 0x428, "SCSP_SCILV2" }, { 0x42A, "SCSP_MCIEB" },
    { 0x42C, "SCSP_MCIPD" }, { 0x42E, "SCSP_MCIRE" },
};

// VDP1 ("VDP1 User's Manual" ch. 4).
const Reg kVdp1[] = {
    { 0x000, "VDP1_TVMR" }, { 0x002, "VDP1_FBCR" }, { 0x004, "VDP1_PTMR" }, { 0x006, "VDP1_EWDR" },
    { 0x008, "VDP1_EWLR" }, { 0x00A, "VDP1_EWRR" }, { 0x00C, "VDP1_ENDR" }, { 0x010, "VDP1_EDSR" },
    { 0x012, "VDP1_LOPR" }, { 0x014, "VDP1_COPR" }, { 0x016, "VDP1_MODR" },
};

// VDP2 ("VDP2 User's Manual"); 0x00C and 0x0FE are reserved.
const Reg kVdp2[] = {
    { 0x000, "VDP2_TVMD" }, { 0x002, "VDP2_EXTEN" }, { 0x004, "VDP2_TVSTAT" },
    { 0x006, "VDP2_VRSIZE" }, { 0x008, "VDP2_HCNT" }, { 0x00A, "VDP2_VCNT" },
    { 0x00E, "VDP2_RAMCTL" }, { 0x010, "VDP2_CYCA0L" }, { 0x012, "VDP2_CYCA0U" },
    { 0x014, "VDP2_CYCA1L" }, { 0x016, "VDP2_CYCA1U" }, { 0x018, "VDP2_CYCB0L" },
    { 0x01A, "VDP2_CYCB0U" }, { 0x01C, "VDP2_CYCB1L" }, { 0x01E, "VDP2_CYCB1U" },
    { 0x020, "VDP2_BGON" }, { 0x022, "VDP2_MZCTL" }, { 0x024, "VDP2_SFSEL" },
    { 0x026, "VDP2_SFCODE" }, { 0x028, "VDP2_CHCTLA" }, { 0x02A, "VDP2_CHCTLB" },
    { 0x02C, "VDP2_BMPNA" }, { 0x02E, "VDP2_BMPNB" }, { 0x030, "VDP2_PNCN0" },
    { 0x032, "VDP2_PNCN1" }, { 0x034, "VDP2_PNCN2" }, { 0x036, "VDP2_PNCN3" },
    { 0x038, "VDP2_PNCR" }, { 0x03A, "VDP2_PLSZ" }, { 0x03C, "VDP2_MPOFN" },
    { 0x03E, "VDP2_MPOFR" }, { 0x040, "VDP2_MPABN0" }, { 0x042, "VDP2_MPCDN0" },
    { 0x044, "VDP2_MPABN1" }, { 0x046, "VDP2_MPCDN1" }, { 0x048, "VDP2_MPABN2" },
    { 0x04A, "VDP2_MPCDN2" }, { 0x04C, "VDP2_MPABN3" }, { 0x04E, "VDP2_MPCDN3" },
    { 0x050, "VDP2_MPABRA" }, { 0x052, "VDP2_MPCDRA" }, { 0x054, "VDP2_MPEFRA" },
    { 0x056, "VDP2_MPGHRA" }, { 0x058, "VDP2_MPIJRA" }, { 0x05A, "VDP2_MPKLRA" },
    { 0x05C, "VDP2_MPMNRA" }, { 0x05E, "VDP2_MPOPRA" }, { 0x060, "VDP2_MPABRB" },
    { 0x062, "VDP2_MPCDRB" }, { 0x064, "VDP2_MPEFRB" }, { 0x066, "VDP2_MPGHRB" },
    { 0x068, "VDP2_MPIJRB" }, { 0x06A, "VDP2_MPKLRB" }, { 0x06C, "VDP2_MPMNRB" },
    { 0x06E, "VDP2_MPOPRB" }, { 0x070, "VDP2_SCXIN0" }, { 0x072, "VDP2_SCXDN0" },
    { 0x074, "VDP2_SCYIN0" }, { 0x076, "VDP2_SCYDN0" }, { 0x078, "VDP2_ZMXIN0" },
    { 0x07A, "VDP2_ZMXDN0" }, { 0x07C, "VDP2_ZMYIN0" }, { 0x07E, "VDP2_ZMYDN0" },
    { 0x080, "VDP2_SCXIN1" }, { 0x082, "VDP2_SCXDN1" }, { 0x084, "VDP2_SCYIN1" },
    { 0x086, "VDP2_SCYDN1" }, { 0x088, "VDP2_ZMXIN1" }, { 0x08A, "VDP2_ZMXDN1" },
    { 0x08C, "VDP2_ZMYIN1" }, { 0x08E, "VDP2_ZMYDN1" }, { 0x090, "VDP2_SCXN2" },
    { 0x092, "VDP2_SCYN2" }, { 0x094, "VDP2_SCXN3" }, { 0x096, "VDP2_SCYN3" },
    { 0x098, "VDP2_ZMCTL" }, { 0x09A, "VDP2_SCRCTL" }, { 0x09C, "VDP2_VCSTAU" },
    { 0x09E, "VDP2_VCSTAL" }, { 0x0A0, "VDP2_LSTA0U" }, { 0x0A2, "VDP2_LSTA0L" },
    { 0x0A4, "VDP2_LSTA1U" }, { 0x0A6, "VDP2_LSTA1L" }, { 0x0A8, "VDP2_LCTAU" },
    { 0x0AA, "VDP2_LCTAL" }, { 0x0AC, "VDP2_BKTAU" }, { 0x0AE, "VDP2_BKTAL" },
    { 0x0B0, "VDP2_RPMD" }, { 0x0B2, "VDP2_RPRCTL" }, { 0x0B4, "VDP2_KTCTL" },
    { 0x0B6, "VDP2_KTAOF" }, { 0x0B8, "VDP2_OVPNRA" }, { 0x0BA, "VDP2_OVPNRB" },
    { 0x0BC, "VDP2_RPTAU" }, { 0x0BE, "VDP2_RPTAL" }, { 0x0C0, "VDP2_WPSX0" },
    { 0x0C2, "VDP2_WPSY0" }, { 0x0C4, "VDP2_WPEX0" }, { 0x0C6, "VDP2_WPEY0" },
    { 0x0C8, "VDP2_WPSX1" }, { 0x0CA, "VDP2_WPSY1" }, { 0x0CC, "VDP2_WPEX1" },
    { 0x0CE, "VDP2_WPEY1" }, { 0x0D0, "VDP2_WCTLA" }, { 0x0D2, "VDP2_WCTLB" },
    { 0x0D4, "VDP2_WCTLC" }, { 0x0D6, "VDP2_WCTLD" }, { 0x0D8, "VDP2_LWTA0U" },
    { 0x0DA, "VDP2_LWTA0L" }, { 0x0DC, "VDP2_LWTA1U" }, { 0x0DE, "VDP2_LWTA1L" },
    { 0x0E0, "VDP2_SPCTL" }, { 0x0E2, "VDP2_SDCTL" }, { 0x0E4, "VDP2_CRAOFA" },
    { 0x0E6, "VDP2_CRAOFB" }, { 0x0E8, "VDP2_LNCLEN" }, { 0x0EA, "VDP2_SFPRMD" },
    { 0x0EC, "VDP2_CCCTL" }, { 0x0EE, "VDP2_SFCCMD" }, { 0x0F0, "VDP2_PRISA" },
    { 0x0F2, "VDP2_PRISB" }, { 0x0F4, "VDP2_PRISC" }, { 0x0F6, "VDP2_PRISD" },
    { 0x0F8, "VDP2_PRINA" }, { 0x0FA, "VDP2_PRINB" }, { 0x0FC, "VDP2_PRIR" },
    { 0x100, "VDP2_CCRSA" }, { 0x102, "VDP2_CCRSB" }, { 0x104, "VDP2_CCRSC" },
    { 0x106, "VDP2_CCRSD" }, { 0x108, "VDP2_CCRNA" }, { 0x10A, "VDP2_CCRNB" },
    { 0x10C, "VDP2_CCRR" }, { 0x10E, "VDP2_CCRLB" }, { 0x110, "VDP2_CLOFEN" },
    { 0x112, "VDP2_CLOFSL" }, { 0x114, "VDP2_COAR" }, { 0x116, "VDP2_COAG" },
    { 0x118, "VDP2_COAB" }, { 0x11A, "VDP2_COBR" }, { 0x11C, "VDP2_COBG" }, { 0x11E, "VDP2_COBB" },
};

// SCU: DMA levels 0-2, DSP, timers, interrupts, A-bus (Sega "SCU User's Manual").
const Reg kScu[] = {
    { 0x000, "SCU_D0R" }, { 0x004, "SCU_D0W" }, { 0x008, "SCU_D0C" }, { 0x00C, "SCU_D0AD" },
    { 0x010, "SCU_D0EN" }, { 0x014, "SCU_D0MD" }, { 0x020, "SCU_D1R" }, { 0x024, "SCU_D1W" },
    { 0x028, "SCU_D1C" }, { 0x02C, "SCU_D1AD" }, { 0x030, "SCU_D1EN" }, { 0x034, "SCU_D1MD" },
    { 0x040, "SCU_D2R" }, { 0x044, "SCU_D2W" }, { 0x048, "SCU_D2C" }, { 0x04C, "SCU_D2AD" },
    { 0x050, "SCU_D2EN" }, { 0x054, "SCU_D2MD" }, { 0x060, "SCU_DSTP" }, { 0x07C, "SCU_DSTA" },
    { 0x080, "SCU_PPAF" }, { 0x084, "SCU_PPD" }, { 0x088, "SCU_PDA" }, { 0x08C, "SCU_PDD" },
    { 0x090, "SCU_T0C" }, { 0x094, "SCU_T1S" }, { 0x098, "SCU_T1MD" }, { 0x0A0, "SCU_IMS" },
    { 0x0A4, "SCU_IST" }, { 0x0A8, "SCU_AIACK" }, { 0x0B0, "SCU_ASR0" }, { 0x0B4, "SCU_ASR1" },
    { 0x0B8, "SCU_AREF" }, { 0x0C4, "SCU_RSEL" }, { 0x0C8, "SCU_VER" },
};

#define SE_BLOCK(chip, base, size, regs) { chip, base, size, regs, sizeof(regs) / sizeof(regs[0]) }
const Block kBlocks[] = {
    SE_BLOCK("SMPC", 0x00100000u, 1, kSmpc),
    SE_BLOCK("SCSP", 0x05B00000u, 2, kScsp),
    SE_BLOCK("VDP1", 0x05D00000u, 2, kVdp1),
    SE_BLOCK("VDP2", 0x05F80000u, 2, kVdp2),
    SE_BLOCK("SCU", 0x05FE0000u, 4, kScu),
};
#undef SE_BLOCK

std::vector<HardwareRegister> Build()
{
    std::vector<HardwareRegister> all;
    for (const Block& b : kBlocks)
        for (size_t i = 0; i < b.count; ++i)
            all.push_back(HardwareRegister{ b.base + b.regs[i].offset, b.size, b.regs[i].name });
    std::sort(all.begin(), all.end(),
              [](const HardwareRegister& x, const HardwareRegister& y) { return x.address < y.address; });
    return all;
}

const std::vector<HardwareRegister>& Table()
{
    static const std::vector<HardwareRegister> table = Build();
    return table;
}
}  // namespace

const HardwareRegister* SaturnHardwareRegisters(size_t& count)
{
    count = Table().size();
    return Table().data();
}

const HardwareRegister* FindHardwareRegister(uint32_t address)
{
    // Only the cached and cache-through partitions reach the bus (A2); the rest are not these
    // registers, whatever their low bits.
    const uint32_t partition = address >> 29;
    if (partition != 0 && partition != 1) return nullptr;
    const uint32_t a = address & 0x1FFFFFFFu;
    const std::vector<HardwareRegister>& t = Table();
    auto it = std::upper_bound(t.begin(), t.end(), a,
                               [](uint32_t v, const HardwareRegister& r) { return v < r.address; });
    if (it == t.begin()) return nullptr;
    --it;
    return a < it->address + it->size ? &*it : nullptr;
}

}  // namespace decomp
}  // namespace sfe
