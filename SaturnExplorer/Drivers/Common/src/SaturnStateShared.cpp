// See SaturnStateShared.h. These were originally file-static in the savestate
// driver; hoisted here verbatim so the live driver reuses the exact same logic.

#include "SaturnStateShared.h"

namespace sedrv
{

namespace
{
constexpr uint32_t kVdp2RegMax = 0x11E;   // highest VDP2 register (COBB)

// Exact hardware-offset -> Yabause 0.9.15 Vdp2 struct byte-offset map, indexed
// by (hw_reg >> 1). 0xFFFF marks a reserved/unmapped hardware slot. Generated
// from the struct in vdp2.h and validated against a real 0.9.15 savestate
// (RAMCTL, priorities, scroll, color offsets all read correctly). The three
// regions: hw < 0x0C maps 1:1; 0x0E..0x76 is hw-2 (the struct omits the
// reserved word at 0x0C); 0x78..0x11E maps 1:1 again (the u32 zoom/address
// unions insert padding that re-absorbs the shift), but each u32's two 16-bit
// halves are byte-swapped for the little-endian host that wrote the state.
const uint16_t kVdp2RegStructOffset[144] =
{
    0x000, 0x002, 0x004, 0x006, 0x008, 0x00A, 0xFFFF, 0x00C,   // hw 0x000
    0x00E, 0x010, 0x012, 0x014, 0x016, 0x018, 0x01A, 0x01C,   // hw 0x010
    0x01E, 0x020, 0x022, 0x024, 0x026, 0x028, 0x02A, 0x02C,   // hw 0x020
    0x02E, 0x030, 0x032, 0x034, 0x036, 0x038, 0x03A, 0x03C,   // hw 0x030
    0x03E, 0x040, 0x042, 0x044, 0x046, 0x048, 0x04A, 0x04C,   // hw 0x040
    0x04E, 0x050, 0x052, 0x054, 0x056, 0x058, 0x05A, 0x05C,   // hw 0x050
    0x05E, 0x060, 0x062, 0x064, 0x066, 0x068, 0x06A, 0x06C,   // hw 0x060
    0x06E, 0x070, 0x072, 0x074, 0x07A, 0x078, 0x07E, 0x07C,   // hw 0x070
    0x080, 0x082, 0x084, 0x086, 0x08A, 0x088, 0x08E, 0x08C,   // hw 0x080
    0x090, 0x092, 0x094, 0x096, 0x098, 0x09A, 0x09E, 0x09C,   // hw 0x090
    0x0A2, 0x0A0, 0x0A6, 0x0A4, 0x0AA, 0x0A8, 0x0AC, 0x0AE,   // hw 0x0A0
    0x0B0, 0x0B2, 0x0B4, 0x0B6, 0x0B8, 0x0BA, 0x0BE, 0x0BC,   // hw 0x0B0
    0x0C0, 0x0C2, 0x0C4, 0x0C6, 0x0C8, 0x0CA, 0x0CC, 0x0CE,   // hw 0x0C0
    0x0D0, 0x0D2, 0x0D4, 0x0D6, 0x0DA, 0x0D8, 0x0DE, 0x0DC,   // hw 0x0D0
    0x0E0, 0x0E2, 0x0E4, 0x0E6, 0x0E8, 0x0EA, 0x0EC, 0x0EE,   // hw 0x0E0
    0x0F0, 0x0F2, 0x0F4, 0x0F6, 0x0F8, 0x0FA, 0x0FC, 0xFFFF,   // hw 0x0F0
    0x0FE, 0x100, 0x102, 0x104, 0x106, 0x108, 0x10A, 0x10C,   // hw 0x100
    0x10E, 0x110, 0x112, 0x114, 0x116, 0x118, 0x11A, 0x11C,   // hw 0x110
};
}  // namespace

uint16_t ReadReg16(const std::vector<uint8_t>& regs, uint32_t reg)
{
    if (reg + 1 >= regs.size())
    {
        return 0;
    }
    return static_cast<uint16_t>((regs[reg] << 8) | regs[reg + 1]);
}

void BuildVdp2RegImage(const std::vector<uint8_t>& src, size_t structBase,
                       std::vector<uint8_t>& out)
{
    out.assign(kVdp2RegMax + 2, 0);
    for (uint32_t hw = 0; hw <= kVdp2RegMax; hw += 2)
    {
        const uint16_t so = kVdp2RegStructOffset[hw >> 1];
        if (so == 0xFFFF || structBase + so + 1 >= src.size())
        {
            continue;  // reserved slot, or beyond the section
        }
        const uint16_t val = static_cast<uint16_t>(src[structBase + so] |
                                                   (src[structBase + so + 1] << 8));
        out[hw]     = static_cast<uint8_t>(val >> 8);
        out[hw + 1] = static_cast<uint8_t>(val & 0xFF);
    }
}

void NormalizeCramToBigEndian(std::vector<uint8_t>& cram, unsigned crmd)
{
    const size_t step = (crmd == 2) ? 4 : 2;   // mode 2 = RGB888 (32-bit entries)
    for (size_t i = 0; i + step <= cram.size(); i += step)
    {
        for (size_t a = 0, b = step - 1; a < b; ++a, --b)
        {
            const uint8_t t = cram[i + a];
            cram[i + a] = cram[i + b];
            cram[i + b] = t;
        }
    }
}

}  // namespace sedrv
