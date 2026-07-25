#include "Vdp2Compositor.h"

#include <algorithm>
#include <array>

#include "ByteOrder.h"
#include "Vdp1Color.h"

namespace se
{

namespace
{

// VDP2 hardware register offsets used by the NBG cell walk.
enum : uint32_t
{
    kTVMD = 0x000, kVRSIZE = 0x006, kRAMCTL = 0x00E, kBGON = 0x020,
    kCHCTLA = 0x028, kCHCTLB = 0x02A,
    kPNCN0 = 0x030, kPNCN1 = 0x032, kPNCN2 = 0x034, kPNCN3 = 0x036,
    kPLSZ = 0x03A, kMPOFN = 0x03C,
    kMPABN0 = 0x040, kMPCDN0 = 0x042, kMPABN1 = 0x044, kMPCDN1 = 0x046,
    kMPABN2 = 0x048, kMPCDN2 = 0x04A, kMPABN3 = 0x04C, kMPCDN3 = 0x04E,
    kSCXIN0 = 0x070, kSCYIN0 = 0x074, kSCXIN1 = 0x080, kSCYIN1 = 0x084,
    kSCXN2 = 0x090, kSCYN2 = 0x092, kSCXN3 = 0x094, kSCYN3 = 0x096,
    kWPSX0 = 0x0C0, kWPSY0 = 0x0C2, kWPEX0 = 0x0C4, kWPEY0 = 0x0C6,
    kWPSX1 = 0x0C8, kWPSY1 = 0x0CA, kWPEX1 = 0x0CC, kWPEY1 = 0x0CE,
    kWCTLA = 0x0D0, kWCTLB = 0x0D2,
    kLWTA0U = 0x0D8, kLWTA0L = 0x0DA, kLWTA1U = 0x0DC, kLWTA1L = 0x0DE,
    kSPCTL = 0x0E0,
    kBKTAU = 0x0AC, kBKTAL = 0x0AE,
    kCCCTL = 0x0EC,
    kPRISA = 0x0F0, kPRISB = 0x0F2, kPRISC = 0x0F4, kPRISD = 0x0F6,
    kCRAOFA = 0x0E4, kPRINA = 0x0F8, kPRINB = 0x0FA,
    kCCRNA = 0x108, kCCRNB = 0x10A,
    // Rotation (RBG0): pattern name, map offset, char/bitmap control, plane sizes,
    // the rotation parameter tables, coefficient control, and RBG0's own priority /
    // colour-offset / colour-calc-ratio registers.
    kPNCR = 0x038, kMPOFR = 0x03E, kBMPNB = 0x02C,
    kMPABRA = 0x050, kMPABRB = 0x060,
    kRPMD = 0x0B0, kKTCTL = 0x0B4, kKTAOF = 0x0B6, kOVPNRA = 0x0B8, kOVPNRB = 0x0BA,
    kRPTAU = 0x0BC, kRPTAL = 0x0BE,
    kCRAOFB = 0x0E6, kPRIR = 0x0FC, kCCRR = 0x10C
};

// Sign-extend the low n bits of v to a signed 32-bit value (Mednafen sign_x_to_s32).
inline int32_t SignX(uint32_t v, int n)
{
    const int s = 32 - n;
    return static_cast<int32_t>(v << s) >> s;
}

struct Window
{
    uint16_t xStart;
    uint16_t yStart;
    uint16_t xEnd;
    uint16_t yEnd;
    bool lineEnabled;
    uint32_t lineAddress;  // VDP2 VRAM word address
};

struct WindowConfig
{
    uint8_t control;       // one WCTL byte for this NBG
    bool highResolution;
    Window windows[2];
};

struct WindowLine
{
    int xStart;
    int xEnd;
    bool yInside;
};

// Everything needed to render one NBG, resolved from the VDP2 registers.
struct NbgConfig
{
    uint16_t patternCtrl;   // PNCNx
    uint16_t mapAB;         // MPABNx
    uint16_t mapCD;         // MPCDNx
    uint16_t scrollX;       // 11-bit
    uint16_t scrollY;
    uint32_t colorNum;      // 0=16, 1=256, 2=2048, 3=RGB555, 4=RGB888
    uint32_t patternWH;     // 1 (8x8) or 2 (16x16 cells per pattern)
    uint32_t planeSize;     // PLSZ 2-bit field
    uint32_t mapOffset;     // MPOFNx nibble, pre-shifted to the plane base
    uint32_t colorOffset;   // CRAOFx nibble, pre-shifted to the CRAM base
    uint32_t priority;      // 0..7 (0 = not displayed)
    bool transparentPixelDisable; // BGON NnTPON: color code zero is opaque
    bool colorCalc;         // CCCTL: this screen participates in color calculation
    uint32_t colorCalcRatio;// CCRNx 5-bit: 0 = mostly this screen, 31 = mostly below
    bool colorCalcAdd;      // CCCTL CCMD: additive blend (ratio ignored)
};

// Blend one source texel over the destination pixel already in the buffer using VDP2
// color-calculation rules (matches Mednafen's mixit): additive saturates per channel;
// ratio mode weights the top screen by (31 - ratio) and the screen below by (ratio + 1)
// out of 32. The result is opaque (color calc never changes coverage). 'dst' is RGBA.
inline void BlendColorCalc(uint8_t* dst, const Rgba& src, uint32_t ratio, bool add)
{
    if (add)
    {
        dst[0] = static_cast<uint8_t>(std::min(255, src.r + dst[0]));
        dst[1] = static_cast<uint8_t>(std::min(255, src.g + dst[1]));
        dst[2] = static_cast<uint8_t>(std::min(255, src.b + dst[2]));
    }
    else
    {
        const uint32_t fore = 31u - (ratio & 0x1F);   // top-screen weight
        const uint32_t sec = 32u - fore;              // = ratio + 1, screen-below weight
        dst[0] = static_cast<uint8_t>((src.r * fore + dst[0] * sec) >> 5);
        dst[1] = static_cast<uint8_t>((src.g * fore + dst[1] * sec) >> 5);
        dst[2] = static_cast<uint8_t>((src.b * fore + dst[2] * sec) >> 5);
    }
    dst[3] = 255;
}

uint16_t Reg(const HardwareSnapshot& s, uint32_t hw)
{
    return s.Vdp2Reg(hw);
}

// Resolve the per-layer configuration for NBG 'n' (0..3).
NbgConfig ReadNbgConfig(const HardwareSnapshot& s, int n)
{
    const uint16_t cha = Reg(s, kCHCTLA);
    const uint16_t chb = Reg(s, kCHCTLB);
    const uint16_t plsz = Reg(s, kPLSZ);
    const uint16_t mpofn = Reg(s, kMPOFN);
    const uint16_t craofa = Reg(s, kCRAOFA);
    const uint16_t prina = Reg(s, kPRINA);
    const uint16_t prinb = Reg(s, kPRINB);

    NbgConfig c {};
    switch (n)
    {
    case 0:
        c.patternCtrl = Reg(s, kPNCN0); c.mapAB = Reg(s, kMPABN0); c.mapCD = Reg(s, kMPCDN0);
        c.scrollX = Reg(s, kSCXIN0) & 0x7FF; c.scrollY = Reg(s, kSCYIN0) & 0x7FF;
        c.colorNum = (cha & 0x0070) >> 4; c.patternWH = (cha & 0x0001) ? 2 : 1;
        c.planeSize = plsz & 0x3;
        c.mapOffset = static_cast<uint32_t>(mpofn & 0x0007) << 6;
        c.colorOffset = static_cast<uint32_t>(craofa & 0x0007) << 8;
        c.priority = prina & 0x7;
        break;
    case 1:
        c.patternCtrl = Reg(s, kPNCN1); c.mapAB = Reg(s, kMPABN1); c.mapCD = Reg(s, kMPCDN1);
        c.scrollX = Reg(s, kSCXIN1) & 0x7FF; c.scrollY = Reg(s, kSCYIN1) & 0x7FF;
        c.colorNum = (cha & 0x3000) >> 12; c.patternWH = (cha & 0x0100) ? 2 : 1;
        c.planeSize = (plsz >> 2) & 0x3;
        c.mapOffset = static_cast<uint32_t>(mpofn & 0x0070) << 2;
        c.colorOffset = static_cast<uint32_t>(craofa & 0x0070) << 4;
        c.priority = (prina >> 8) & 0x7;
        break;
    case 2:
        c.patternCtrl = Reg(s, kPNCN2); c.mapAB = Reg(s, kMPABN2); c.mapCD = Reg(s, kMPCDN2);
        c.scrollX = Reg(s, kSCXN2) & 0x7FF; c.scrollY = Reg(s, kSCYN2) & 0x7FF;
        c.colorNum = (chb & 0x0002) >> 1; c.patternWH = (chb & 0x0001) ? 2 : 1;
        c.planeSize = (plsz >> 4) & 0x3;
        c.mapOffset = static_cast<uint32_t>(mpofn & 0x0700) >> 2;
        c.colorOffset = static_cast<uint32_t>(craofa & 0x0700);
        c.priority = prinb & 0x7;
        break;
    default:  // 3
        c.patternCtrl = Reg(s, kPNCN3); c.mapAB = Reg(s, kMPABN3); c.mapCD = Reg(s, kMPCDN3);
        c.scrollX = Reg(s, kSCXN3) & 0x7FF; c.scrollY = Reg(s, kSCYN3) & 0x7FF;
        c.colorNum = (chb & 0x0020) >> 5; c.patternWH = (chb & 0x0010) ? 2 : 1;
        c.planeSize = (plsz >> 6) & 0x3;
        c.mapOffset = static_cast<uint32_t>(mpofn & 0x7000) >> 6;
        c.colorOffset = static_cast<uint32_t>(craofa & 0x7000) >> 4;
        c.priority = (prinb >> 8) & 0x7;
        break;
    }
    c.transparentPixelDisable = (Reg(s, kBGON) & (1u << (n + 8))) != 0;

    // Color calculation: CCCTL bit n enables it for NBG n; CCMD (bit 8) selects
    // additive blending; the 5-bit ratio lives in CCRNA (N0 low / N1 high) and
    // CCRNB (N2 low / N3 high).
    const uint16_t ccctl = Reg(s, kCCCTL);
    const uint16_t ccrn = (n < 2) ? Reg(s, kCCRNA) : Reg(s, kCCRNB);
    c.colorCalc = (ccctl & (1u << n)) != 0;
    c.colorCalcRatio = ((n & 1) ? (ccrn >> 8) : ccrn) & 0x1F;
    c.colorCalcAdd = (ccctl & 0x0100) != 0;
    return c;
}

WindowConfig ReadWindowConfig(const HardwareSnapshot& s, int n)
{
    const uint16_t wctl = Reg(s, n < 2 ? kWCTLA : kWCTLB);
    const uint8_t control = static_cast<uint8_t>((wctl >> ((n & 1) * 8)) & 0xBF);
    WindowConfig c {
        control,
        (Reg(s, kTVMD) & 0x0002) != 0,
        {
            { static_cast<uint16_t>(Reg(s, kWPSX0) & 0x3FF),
              static_cast<uint16_t>(Reg(s, kWPSY0) & 0x1FF),
              static_cast<uint16_t>(Reg(s, kWPEX0) & 0x3FF),
              static_cast<uint16_t>(Reg(s, kWPEY0) & 0x1FF),
              (Reg(s, kLWTA0U) & 0x8000) != 0,
              (static_cast<uint32_t>(Reg(s, kLWTA0U) & 0x7) << 16) |
                  (Reg(s, kLWTA0L) & 0xFFFE) },
            { static_cast<uint16_t>(Reg(s, kWPSX1) & 0x3FF),
              static_cast<uint16_t>(Reg(s, kWPSY1) & 0x1FF),
              static_cast<uint16_t>(Reg(s, kWPEX1) & 0x3FF),
              static_cast<uint16_t>(Reg(s, kWPEY1) & 0x1FF),
              (Reg(s, kLWTA1U) & 0x8000) != 0,
              (static_cast<uint32_t>(Reg(s, kLWTA1U) & 0x7) << 16) |
                  (Reg(s, kLWTA1L) & 0xFFFE) }
        }
    };
    return c;
}

uint16_t ReadVdp2Word(const std::vector<uint8_t>& vram, uint32_t wordAddress)
{
    return ReadBE16(vram, (wordAddress & 0x3FFFFu) * 2);
}

// A 32-bit big-endian value stored as two consecutive VDP2 VRAM words (rotation
// parameter tables and coefficient tables store their fixed-point values this way).
uint32_t ReadVdp2Dword(const std::vector<uint8_t>& vram, uint32_t wordAddress)
{
    return (static_cast<uint32_t>(ReadVdp2Word(vram, wordAddress)) << 16) |
           ReadVdp2Word(vram, wordAddress + 1);
}

bool CoordinateInside(uint32_t coordinate, uint32_t start, uint32_t end)
{
    // Vertical windows can cross the counter wrap at the end of a field.
    return start <= end ? coordinate >= start && coordinate <= end
                        : coordinate >= start || coordinate <= end;
}

WindowLine ResolveWindowLine(const WindowConfig& config, int index,
                             const std::vector<uint8_t>& vram, int y)
{
    const Window& window = config.windows[index];
    int xStart = window.xStart;
    int xEnd = window.xEnd;
    if (window.lineEnabled)
    {
        // Each displayed line contributes two words: horizontal start and end.
        // In double-density interlace the two fields use alternating pairs; a
        // full-height SE image naturally visits those pairs in display-line order.
        const uint32_t lineAddress = window.lineAddress + static_cast<uint32_t>(y) * 2;
        xStart = ReadVdp2Word(vram, lineAddress + 0) & 0x3FF;
        xEnd = ReadVdp2Word(vram, lineAddress + 1) & 0x3FF;
    }

    // Match the Saturn coordinate conversion and Mednafen's handling of the
    // hardware's out-of-range line-window sentinels.
    if (xStart >= 0x380) xStart = 0;
    if (xEnd >= 0x380)
    {
        xStart = 2;
        xEnd = 0;
    }
    if (!config.highResolution)
    {
        xStart >>= 1;
        xEnd >>= 1;
    }

    return { xStart, xEnd,
             CoordinateInside(static_cast<uint32_t>(y), window.yStart, window.yEnd) };
}

bool WindowMasksPixel(uint8_t control, const WindowLine (&windows)[2], int x)
{
    // WCTL describes a *transparent-processing* window: true means the layer
    // pixel is suppressed. Disabled inputs take the identity value for the
    // selected OR/AND operation, exactly as on VDP2.
    const bool useAnd = (control & 0x80) != 0;
    bool values[2];
    for (int i = 0; i < 2; ++i)
    {
        const uint8_t enableBit = static_cast<uint8_t>(0x02u << (i * 2));
        const uint8_t areaBit = static_cast<uint8_t>(0x01u << (i * 2));
        if (control & enableBit)
        {
            const bool inside = windows[i].yInside &&
                                x >= windows[i].xStart && x <= windows[i].xEnd;
            values[i] = inside ^ ((control & areaBit) != 0);
        }
        else
        {
            values[i] = useAnd;
        }
    }

    // Sprite-window pixels are not available to the command-list compositor yet.
    // Ignore that input by supplying the selected operation's identity value;
    // otherwise an outside-area sprite window would incorrectly hide the whole NBG.
    const bool spriteValue = useAnd;
    return useAnd ? (values[0] && values[1] && spriteValue)
                  : (values[0] || values[1] || spriteValue);
}

// Bytes one 8x8 cell occupies, by color-number field.
uint32_t CellByteSize(uint32_t colorNum)
{
    switch (colorNum)
    {
    case 0:  return 0x20;   // 4 bpp
    case 1:  return 0x40;   // 8 bpp
    case 2:                 // 16 bpp palette
    case 3:  return 0x80;   // 16 bpp RGB
    default: return 0x100;  // 32 bpp RGB
    }
}

// One decoded pattern-name entry: character byte base (already * 0x20), the
// palette base for palette color modes, and the H/V flip flags.
struct PatternName
{
    uint32_t charBase;
    uint32_t palette;
    uint32_t flip;   // bit0 = H, bit1 = V
};

PatternName DecodePatternName(const std::vector<uint8_t>& vram, uint32_t addr,
                              const NbgConfig& c, uint16_t vrsize)
{
    const uint32_t supp = c.patternCtrl & 0x3FF;
    const bool oneWord = (c.patternCtrl & 0x8000) != 0;
    const uint32_t auxMode = (c.patternCtrl >> 14) & 1;

    PatternName p {};
    uint32_t ch;
    if (oneWord)
    {
        const uint16_t tmp = ReadBE16(vram, addr);
        p.palette = (c.colorNum == 0) ? (((tmp & 0xF000u) >> 8) | ((supp & 0xE0u) << 3))
                                      : ((tmp & 0x7000u) >> 4);
        if (auxMode == 0)
        {
            p.flip = (tmp & 0x0C00u) >> 10;
            ch = (c.patternWH == 1) ? ((tmp & 0x3FFu) | ((supp & 0x1Fu) << 10))
                                    : (((tmp & 0x3FFu) << 2) | (supp & 0x3u) | ((supp & 0x1Cu) << 10));
        }
        else
        {
            p.flip = 0;
            ch = (c.patternWH == 1) ? ((tmp & 0xFFFu) | ((supp & 0x1Cu) << 10))
                                    : (((tmp & 0xFFFu) << 2) | (supp & 0x3u) | ((supp & 0x10u) << 10));
        }
    }
    else
    {
        const uint16_t t1 = ReadBE16(vram, addr);
        const uint16_t t2 = ReadBE16(vram, addr + 2);
        ch = t2 & 0x7FFFu;
        p.flip = (t1 & 0xC000u) >> 14;
        p.palette = (c.colorNum == 0) ? ((t1 & 0x7Fu) << 4) : ((t1 & 0x70u) << 4);
    }

    if (!(vrsize & 0x8000))
    {
        ch &= 0x3FFF;
    }
    p.charBase = ch * 0x20;
    return p;
}

// Sample one 8x8-cell texel (ix,iy in 0..7) at 'cellBase'. Returns a == 0 when
// the pixel is transparent (palette index 0, or MSB clear in RGB modes).
Rgba FetchCellTexel(const std::vector<uint8_t>& vram, const std::vector<uint8_t>& cram,
                    se_cram_mode cramMode, const NbgConfig& c, const PatternName& p,
                    uint32_t cellBase, int ix, int iy)
{
    const uint32_t pix = static_cast<uint32_t>(iy) * 8 + ix;
    switch (c.colorNum)
    {
    case 0:   // 16-color (4 bpp)
    {
        const uint32_t off = cellBase + (pix >> 1);
        const uint8_t byte = (off < vram.size()) ? vram[off] : 0;
        const uint8_t dot = (ix & 1) ? (byte & 0x0F) : (byte >> 4);
        if (dot == 0 && !c.transparentPixelDisable) return { 0, 0, 0, 0 };
        return CramColor(cram, cramMode, c.colorOffset + (p.palette | dot));
    }
    case 1:   // 256-color (8 bpp)
    {
        const uint32_t off = cellBase + pix;
        const uint8_t dot = (off < vram.size()) ? vram[off] : 0;
        if (dot == 0 && !c.transparentPixelDisable) return { 0, 0, 0, 0 };
        return CramColor(cram, cramMode, c.colorOffset + (p.palette | dot));
    }
    case 2:   // 2048-color (16 bpp palette)
    {
        const uint16_t dot = ReadBE16(vram, cellBase + pix * 2) & 0x7FF;
        if (dot == 0 && !c.transparentPixelDisable) return { 0, 0, 0, 0 };
        return CramColor(cram, cramMode, c.colorOffset + dot);
    }
    case 3:   // 32K-color (16 bpp RGB555)
    {
        const uint16_t dot = ReadBE16(vram, cellBase + pix * 2);
        if (!(dot & 0x8000) && !c.transparentPixelDisable) return { 0, 0, 0, 0 };
        return Rgb555ToRgba(dot);
    }
    default:  // 16M-color (32 bpp RGB888)
    {
        const uint32_t off = cellBase + pix * 4;
        if (off + 3 >= vram.size()) return { 0, 0, 0, 0 };
        if (!(vram[off] & 0x80) && !c.transparentPixelDisable)
            return { 0, 0, 0, 0 };   // MSB = transparency
        return { vram[off + 1], vram[off + 2], vram[off + 3], 255 };
    }
    }
}

// Composite one NBG layer into 'out' (width*height*4): each opaque cell texel
// overwrites the pixel, transparent texels leave whatever a farther layer drew.
// 'out' must already be sized and cleared/painted; layers are drawn back-to-front
// so this needs no per-layer scratch buffer. Faithful port of the validated
// plane/page/cell walk.
void RenderLayer(const HardwareSnapshot& snap, const NbgConfig& c, int layerIndex,
                 bool applyWindows, bool colorCalc, int width, int height,
                 std::vector<uint8_t>& out)
{
    const std::vector<uint8_t>& vram = snap.Vdp2Vram();
    const std::vector<uint8_t>& cram = snap.Cram();
    const se_cram_mode cramMode = snap.CramMode();
    const uint16_t vrsize = Reg(snap, kVRSIZE);
    const WindowConfig windowConfig = ReadWindowConfig(snap, layerIndex);

    // Plane arrangement from the 2-bit plane-size field: 1x1, 2x1, or 2x2 pages.
    uint32_t planeW = (c.planeSize & 0x1) ? 2 : 1;
    uint32_t planeH = (c.planeSize & 0x2) ? 2 : 1;
    const uint32_t deca = planeH + planeW - 2;
    const uint32_t multi = planeH * planeW;

    // The four planes (A,B,C,D) that tile the screen as a 2x2 grid of planes.
    const uint32_t pageRegs[4] = {
        static_cast<uint32_t>(c.mapAB & 0xFF), static_cast<uint32_t>(c.mapAB >> 8),
        static_cast<uint32_t>(c.mapCD & 0xFF), static_cast<uint32_t>(c.mapCD >> 8) };

    const bool oneWord = (c.patternCtrl & 0x8000) != 0;
    std::array<uint32_t, 4> planeBase {};
    for (int i = 0; i < 4; ++i)
    {
        const uint32_t tmp = c.mapOffset | pageRegs[i];
        if (oneWord)
        {
            planeBase[i] = (c.patternWH == 1) ? (((tmp & 0x3F) >> deca) * (multi * 0x2000))
                                              : ((tmp >> deca) * (multi * 0x800));
        }
        else
        {
            planeBase[i] = (c.patternWH == 1) ? (((tmp & 0x1F) >> deca) * (multi * 0x4000))
                                              : (((tmp & 0x7F) >> deca) * (multi * 0x1000));
        }
    }

    const uint32_t cellWH = 8 * c.patternWH;                 // pattern size in pixels
    const uint32_t pnBytes = oneWord ? 2 : 4;                // pattern-name entry size
    const uint32_t pageCells = 64 >> (c.patternWH - 1);      // patterns per page dim
    const uint32_t planePixW = planeW * 512;
    const uint32_t planePixH = planeH * 512;
    const uint32_t xMask = 2 * planePixW - 1;
    const uint32_t yMask = 2 * planePixH - 1;
    const uint32_t cellBytes = CellByteSize(c.colorNum);

    for (int sy = 0; sy < height; ++sy)
    {
        const WindowLine windowLines[2] = {
            ResolveWindowLine(windowConfig, 0, vram, sy),
            ResolveWindowLine(windowConfig, 1, vram, sy)
        };
        for (int sx = 0; sx < width; ++sx)
        {
            if (applyWindows && WindowMasksPixel(windowConfig.control, windowLines, sx))
            {
                continue;
            }
            const uint32_t x = (c.scrollX + sx) & xMask;
            const uint32_t y = (c.scrollY + sy) & yMask;
            const uint32_t plane = (y / planePixH) * 2 + (x / planePixW);
            const uint32_t px = x % planePixW;
            const uint32_t py = y % planePixH;

            // Pattern-name tables are laid out page-major: a plane holds
            // planeW*planeH pages, each a contiguous pageCells*pageCells block of
            // entries. Address the enclosing page first, then the pattern within
            // it — a plain patY*pageCells+patX stride is only correct for a 1x1
            // plane and collides across page columns otherwise.
            const uint32_t patX = px / cellWH;
            const uint32_t patY = py / cellWH;
            const uint32_t pageIndex = (patY / pageCells) * planeW + (patX / pageCells);
            const uint32_t patIndex = pageIndex * (pageCells * pageCells) +
                                      (patY % pageCells) * pageCells + (patX % pageCells);
            const PatternName pn = DecodePatternName(
                vram, planeBase[plane] + patIndex * pnBytes, c, vrsize);

            // Pixel within the pattern, then within its 8x8 sub-cell.
            uint32_t inX = px % cellWH;
            uint32_t inY = py % cellWH;
            if (pn.flip & 1) inX = cellWH - 1 - inX;
            if (pn.flip & 2) inY = cellWH - 1 - inY;
            const uint32_t subCell = (inY / 8) * c.patternWH + (inX / 8);
            const uint32_t cellBase = (pn.charBase + subCell * cellBytes) & 0x7FFFFu;

            const Rgba col = FetchCellTexel(vram, cram, cramMode, c, pn, cellBase,
                                            static_cast<int>(inX % 8), static_cast<int>(inY % 8));
            if (col.a == 0)
            {
                continue;
            }
            const size_t o = (static_cast<size_t>(sy) * width + sx) * 4;
            // Color calculation blends this screen with whatever is already below it
            // (a lower-priority layer or the back screen); otherwise it overwrites.
            // Blending only makes sense over an opaque pixel — a transparent
            // destination (no back screen painted) falls back to an opaque write.
            if (colorCalc && c.colorCalc && out[o + 3] != 0)
            {
                BlendColorCalc(&out[o], col, c.colorCalcRatio, c.colorCalcAdd);
            }
            else
            {
                out[o + 0] = col.r;
                out[o + 1] = col.g;
                out[o + 2] = col.b;
                out[o + 3] = 255;
            }
        }
    }
}

// Composite one resolved texel into the buffer, honoring color calculation exactly like
// RenderLayer's inner write (blend over an opaque pixel below, else overwrite).
inline void CompositeTexel(std::vector<uint8_t>& out, size_t o, const Rgba& col,
                           bool colorCalc, const NbgConfig& c)
{
    if (colorCalc && c.colorCalc && out[o + 3] != 0)
    {
        BlendColorCalc(&out[o], col, c.colorCalcRatio, c.colorCalcAdd);
    }
    else
    {
        out[o + 0] = col.r;
        out[o + 1] = col.g;
        out[o + 2] = col.b;
        out[o + 3] = 255;
    }
}

// One rotation parameter set (table A or B), as fixed-point integers read straight from
// VDP2 VRAM. Formats follow the VDP2 manual / Mednafen: the ".10" values keep 10
// fractional bits, kx/ky keep 16. See FetchRotParam for the exact per-field extraction.
struct RotParam
{
    int32_t Xst, Yst, Zst, DXst, DYst, DX, DY;
    int32_t M[6];                       // rotation matrix, row-major (A,B,C / D,E,F)
    int32_t Px, Py, Pz, Cx, Cy, Cz, Mx, My, kx, ky;
    uint32_t KAst;                      // coefficient table start (.10)
    int32_t DKAst, DKAx;               // coefficient step per line / per dot
};

RotParam FetchRotParam(const std::vector<uint8_t>& v, uint32_t a)
{
    RotParam r {};
    r.Xst = SignX(ReadVdp2Dword(v, a + 0x00) >> 6, 23);
    r.Yst = SignX(ReadVdp2Dword(v, a + 0x02) >> 6, 23);
    r.Zst = SignX(ReadVdp2Dword(v, a + 0x04) >> 6, 23);
    r.DXst = SignX(ReadVdp2Dword(v, a + 0x06) >> 6, 13);
    r.DYst = SignX(ReadVdp2Dword(v, a + 0x08) >> 6, 13);
    r.DX = SignX(ReadVdp2Dword(v, a + 0x0A) >> 6, 13);
    r.DY = SignX(ReadVdp2Dword(v, a + 0x0C) >> 6, 13);
    for (int m = 0; m < 6; ++m)
        r.M[m] = SignX(ReadVdp2Dword(v, a + 0x0E + m * 2) >> 6, 14);
    r.Px = SignX(ReadVdp2Word(v, a + 0x1A), 14);
    r.Py = SignX(ReadVdp2Word(v, a + 0x1B), 14);
    r.Pz = SignX(ReadVdp2Word(v, a + 0x1C), 14);
    r.Cx = SignX(ReadVdp2Word(v, a + 0x1E), 14);
    r.Cy = SignX(ReadVdp2Word(v, a + 0x1F), 14);
    r.Cz = SignX(ReadVdp2Word(v, a + 0x20), 14);
    r.Mx = SignX(ReadVdp2Dword(v, a + 0x22) >> 6, 24);
    r.My = SignX(ReadVdp2Dword(v, a + 0x24) >> 6, 24);
    r.kx = SignX(ReadVdp2Dword(v, a + 0x26), 24);
    r.ky = SignX(ReadVdp2Dword(v, a + 0x28), 24);
    r.KAst = ReadVdp2Dword(v, a + 0x2A) >> 6;
    r.DKAst = SignX(ReadVdp2Dword(v, a + 0x2C) >> 6, 20);
    r.DKAx = SignX(ReadVdp2Dword(v, a + 0x2E) >> 6, 20);
    return r;
}

// Resolve the RBG0 cell configuration (shares the cell/pattern layout with the NBGs;
// the rotation-specific plane map + coordinates are handled in RenderRbg0). 'paramB'
// selects which rotation parameter set supplies plane size / map offset.
NbgConfig ReadRbg0Config(const HardwareSnapshot& s, bool paramB)
{
    const uint16_t chb = Reg(s, kCHCTLB);
    const uint16_t plsz = Reg(s, kPLSZ);
    const uint16_t mpofr = Reg(s, kMPOFR);
    NbgConfig c {};
    c.patternCtrl = Reg(s, kPNCR);
    c.colorNum = std::min<uint32_t>(4u, (chb >> 12) & 0x7);
    c.patternWH = (chb & 0x0100) ? 2 : 1;
    c.planeSize = (plsz >> (paramB ? 12 : 8)) & 0x3;
    c.mapOffset = static_cast<uint32_t>((mpofr >> (paramB ? 4 : 0)) & 0x7) << 6;
    c.colorOffset = static_cast<uint32_t>(Reg(s, kCRAOFB) & 0x0007) << 8;
    c.priority = Reg(s, kPRIR) & 0x7;
    c.transparentPixelDisable = (Reg(s, kBGON) & (1u << 12)) != 0;
    const uint16_t ccctl = Reg(s, kCCCTL);
    c.colorCalc = (ccctl & (1u << 4)) != 0;
    c.colorCalcRatio = Reg(s, kCCRR) & 0x1F;
    c.colorCalcAdd = (ccctl & 0x0100) != 0;
    return c;
}

// Composite the RBG0 rotation screen. For each screen dot the rotation parameter set is
// evaluated (matrix + view/centre/move + per-line accumulation + optional per-dot
// coefficient table) to a plane-space coordinate, which indexes RBG0's 4x4 grid of 16
// planes via the same page->pattern->cell walk the NBGs use. Cell mode only for now
// (RBG0 bitmap arrives with the NBG bitmap path); RPMD 0/1 (single parameter set) with
// coefficient tables; screen-over "repeat" wraps, other modes read transparent outside.
void RenderRbg0(const HardwareSnapshot& snap, const NbgConfig& c, bool paramB,
                bool applyWindows, bool colorCalc, int width, int height,
                std::vector<uint8_t>& out)
{
    const std::vector<uint8_t>& vram = snap.Vdp2Vram();
    const std::vector<uint8_t>& cram = snap.Cram();
    const se_cram_mode cramMode = snap.CramMode();
    const uint16_t vrsize = Reg(snap, kVRSIZE);
    const WindowConfig windowConfig = ReadWindowConfig(snap, 0);  // RBG0 uses WCTLC bits;
    // approximate with the NBG0 window byte (WCTLA low) — the common single-window case.

    // Rotation parameter table + coefficient control.
    const uint32_t rpta = ((static_cast<uint32_t>(Reg(snap, kRPTAU) & 0x0007) << 16) |
                           Reg(snap, kRPTAL)) & 0x7FFBE;
    const RotParam rp = FetchRotParam(vram, rpta + (paramB ? 0x40 : 0));
    const uint16_t ktctl = Reg(snap, kKTCTL) >> (paramB ? 8 : 0);
    const bool useCoeff = (ktctl & 0x1) != 0;
    const bool coeffWord = (ktctl & 0x2) != 0;   // 1 = 16-bit coeff, 0 = 32-bit
    const uint32_t coeffMode = (ktctl >> 2) & 0x3;
    const uint32_t screenOver = (Reg(snap, kPLSZ) >> (paramB ? 14 : 10)) & 0x3;

    // Plane geometry: RBG0 tiles a 4x4 grid of 16 planes.
    const uint32_t planeW = (c.planeSize & 0x1) ? 2 : 1;
    const uint32_t planeH = (c.planeSize & 0x2) ? 2 : 1;
    const uint32_t deca = planeH + planeW - 2;
    const uint32_t multi = planeH * planeW;
    const bool oneWord = (c.patternCtrl & 0x8000) != 0;
    const uint32_t mapBase = paramB ? kMPABRB : kMPABRA;
    std::array<uint32_t, 16> planeBase {};
    for (int i = 0; i < 16; ++i)
    {
        const uint16_t w = Reg(snap, mapBase + (i / 2) * 2);
        const uint32_t pageReg = (i & 1) ? ((w >> 8) & 0x3F) : (w & 0x3F);
        const uint32_t tmp = c.mapOffset | pageReg;
        if (oneWord)
            planeBase[i] = (c.patternWH == 1) ? (((tmp & 0x3F) >> deca) * (multi * 0x2000))
                                              : ((tmp >> deca) * (multi * 0x800));
        else
            planeBase[i] = (c.patternWH == 1) ? (((tmp & 0x1F) >> deca) * (multi * 0x4000))
                                              : (((tmp & 0x7F) >> deca) * (multi * 0x1000));
    }

    const uint32_t cellWH = 8 * c.patternWH;
    const uint32_t pnBytes = oneWord ? 2 : 4;
    const uint32_t pageCells = 64 >> (c.patternWH - 1);
    const uint32_t planePixW = planeW * 512;
    const uint32_t planePixH = planeH * 512;
    const uint32_t totalW = 4 * planePixW;   // power of two — screen-over "repeat" masks
    const uint32_t totalH = 4 * planePixH;
    const uint32_t cellBytes = CellByteSize(c.colorNum);

    for (int sy = 0; sy < height; ++sy)
    {
        const WindowLine windowLines[2] = {
            ResolveWindowLine(windowConfig, 0, vram, sy),
            ResolveWindowLine(windowConfig, 1, vram, sy)
        };
        // Per-line rotation setup (Xst/Yst accumulate DXst/DYst down the screen).
        const int32_t XstA = rp.Xst + rp.DXst * sy;
        const int32_t YstA = rp.Yst + rp.DYst * sy;
        const int64_t Xsp = ((int64_t)rp.M[0] * (XstA - rp.Px * 1024) +
                             (int64_t)rp.M[1] * (YstA - rp.Py * 1024) +
                             (int64_t)rp.M[2] * (rp.Zst - rp.Pz * 1024)) >> 10;
        const int64_t Ysp = ((int64_t)rp.M[3] * (XstA - rp.Px * 1024) +
                             (int64_t)rp.M[4] * (YstA - rp.Py * 1024) +
                             (int64_t)rp.M[5] * (rp.Zst - rp.Pz * 1024)) >> 10;
        const int32_t XpBase = rp.M[0] * (rp.Px - rp.Cx) + rp.M[1] * (rp.Py - rp.Cy) +
                               rp.M[2] * (rp.Pz - rp.Cz) + rp.Cx * 1024 + rp.Mx;
        const int32_t Yp = rp.M[3] * (rp.Px - rp.Cx) + rp.M[4] * (rp.Py - rp.Cy) +
                           rp.M[5] * (rp.Pz - rp.Cz) + rp.Cy * 1024 + rp.My;
        const int32_t dX = (rp.M[0] * rp.DX + rp.M[1] * rp.DY) >> 10;
        const int32_t dY = (rp.M[3] * rp.DX + rp.M[4] * rp.DY) >> 10;
        const uint32_t KAstLine = rp.KAst + static_cast<uint32_t>(rp.DKAst) * sy;

        for (int sx = 0; sx < width; ++sx)
        {
            if (applyWindows && WindowMasksPixel(windowConfig.control, windowLines, sx))
            {
                continue;
            }
            int32_t kx = rp.kx, ky = rp.ky, Xp = XpBase;
            if (useCoeff)
            {
                // One coefficient per dot along the line; it can override kx/ky or Xp.
                uint32_t coeffOff = (KAstLine + static_cast<uint32_t>(rp.DKAx) * sx) >> 10;
                if (!coeffWord) coeffOff <<= 1;   // 32-bit entries are two words
                uint32_t coeff;
                if (coeffWord)
                {
                    const uint16_t t = ReadVdp2Word(vram, coeffOff);
                    coeff = (static_cast<uint32_t>(SignX(t << 6, 21)) & 0x00FFFFFFu) |
                            (static_cast<uint32_t>(t & 0x8000) << 16);
                }
                else
                {
                    coeff = ReadVdp2Dword(vram, coeffOff);
                }
                if (static_cast<int32_t>(coeff) < 0) continue;   // coefficient = transparent
                const int32_t sext = SignX(coeff, 24);
                switch (coeffMode)
                {
                case 0: kx = ky = sext; break;
                case 1: kx = sext; break;
                case 2: ky = sext; break;
                default: Xp = sext << 2; break;
                }
            }

            const int32_t ixs = static_cast<int32_t>(
                (Xp + static_cast<int32_t>(((int64_t)kx * (int32_t)(Xsp + dX * sx)) >> 16)) >> 10);
            const int32_t iys = static_cast<int32_t>(
                (Yp + static_cast<int32_t>(((int64_t)ky * (int32_t)(Ysp + dY * sx)) >> 16)) >> 10);

            // Screen-over: mode 0 repeats (wrap); other modes read transparent outside.
            if (screenOver != 0 &&
                (ixs < 0 || iys < 0 || static_cast<uint32_t>(ixs) >= totalW ||
                 static_cast<uint32_t>(iys) >= totalH))
            {
                continue;
            }
            const uint32_t x = static_cast<uint32_t>(ixs) & (totalW - 1);
            const uint32_t y = static_cast<uint32_t>(iys) & (totalH - 1);

            const uint32_t plane = (y / planePixH) * 4 + (x / planePixW);
            const uint32_t px = x % planePixW;
            const uint32_t py = y % planePixH;
            const uint32_t patX = px / cellWH;
            const uint32_t patY = py / cellWH;
            const uint32_t pageIndex = (patY / pageCells) * planeW + (patX / pageCells);
            const uint32_t patIndex = pageIndex * (pageCells * pageCells) +
                                      (patY % pageCells) * pageCells + (patX % pageCells);
            const PatternName pn = DecodePatternName(
                vram, planeBase[plane] + patIndex * pnBytes, c, vrsize);

            uint32_t inX = px % cellWH;
            uint32_t inY = py % cellWH;
            if (pn.flip & 1) inX = cellWH - 1 - inX;
            if (pn.flip & 2) inY = cellWH - 1 - inY;
            const uint32_t subCell = (inY / 8) * c.patternWH + (inX / 8);
            const uint32_t cellBase = (pn.charBase + subCell * cellBytes) & 0x7FFFFu;

            const Rgba col = FetchCellTexel(vram, cram, cramMode, c, pn, cellBase,
                                            static_cast<int>(inX % 8), static_cast<int>(inY % 8));
            if (col.a == 0)
            {
                continue;
            }
            CompositeTexel(out, (static_cast<size_t>(sy) * width + sx) * 4, col, colorCalc, c);
        }
    }
}

}  // namespace

int Vdp2Compositor::SpritePriority(const HardwareSnapshot& snapshot)
{
    if (!snapshot.HasVdp2Regs())
    {
        return 0;
    }
    // The sprite pixel's priority *number* selects one of eight 3-bit priority
    // values in PRISA..PRISD. Modeling that per pixel needs the framebuffer;
    // we render from the command list, so we use priority number 0 (PRISA low
    // 3 bits), which is what the overwhelming majority of scenes use. This is
    // enough to place the whole sprite plane correctly relative to the NBGs.
    return Reg(snapshot, kPRISA) & 0x7;
}

void Vdp2Compositor::Render(const HardwareSnapshot& snapshot, const se_render_opts& opts,
                            int width, int height, std::vector<uint8_t>& outRgba,
                            int minPriority, int maxPriority, bool clear)
{
    if (clear)
    {
        outRgba.assign(static_cast<size_t>(width) * height * 4, 0);
    }
    if (width <= 0 || height <= 0 || !snapshot.HasVdp2Regs() || snapshot.Vdp2Vram().empty())
    {
        return;
    }

    const uint16_t bgon = Reg(snapshot, kBGON);

    // Resolve the enabled NBGs first (no rendering yet). A layer is drawn only if
    // BGON enables it, the host toggle is on, and its priority is non-zero
    // (priority 0 = not displayed on hardware).
    struct Layer
    {
        int index;      // NBG number, or 4 for RBG0
        NbgConfig config;
        bool rbg0 = false;
        bool paramB = false;
    };
    std::vector<Layer> layers;
    auto consider = [&](const Layer& layer)
    {
        if (layer.config.priority == 0) return;    // priority 0 = not displayed
        if (static_cast<int>(layer.config.priority) < minPriority ||
            static_cast<int>(layer.config.priority) > maxPriority)
            return;                                 // outside the requested band
        layers.push_back(layer);
    };
    for (int n = 0; n < 4; ++n)
    {
        if (!(bgon & (1u << n)) || !opts.show_layer[n])
        {
            continue;
        }
        consider({ n, ReadNbgConfig(snapshot, n), false, false });
    }
    // RBG0 (rotation) occupies BGON bit 4; RPMD picks its parameter set (0 = A, 1 = B;
    // per-dot/window selection isn't modeled yet, so those fall back to A).
    if ((bgon & (1u << 4)) && opts.show_layer[SE_LAYER_RBG0])
    {
        const bool paramB = (Reg(snapshot, kRPMD) & 0x3) == 1;
        consider({ 4, ReadRbg0Config(snapshot, paramB), true, paramB });
    }

    // Paint order = back to front: ascending priority; for equal priority the
    // higher-numbered NBG is further back (drawn first), so NBG0 wins ties. Then
    // composite each layer straight into outRgba — opaque texels overwrite,
    // transparent ones leave the farther layer, so no per-layer scratch buffer.
    std::stable_sort(layers.begin(), layers.end(), [](const Layer& a, const Layer& b)
    {
        if (a.config.priority != b.config.priority) return a.config.priority < b.config.priority;
        return a.index > b.index;
    });

    for (const Layer& layer : layers)
    {
        if (layer.rbg0)
        {
            RenderRbg0(snapshot, layer.config, layer.paramB, opts.show_window != 0,
                       opts.show_color_calculation != 0, width, height, outRgba);
        }
        else
        {
            RenderLayer(snapshot, layer.config, layer.index, opts.show_window != 0,
                        opts.show_color_calculation != 0, width, height, outRgba);
        }
    }
}

void Vdp2Compositor::RenderBackScreen(const HardwareSnapshot& snapshot, int width,
                                      int height, std::vector<uint8_t>& outRgba)
{
    if (width <= 0 || height <= 0 || !snapshot.HasVdp2Regs() || snapshot.Vdp2Vram().empty())
    {
        return;
    }
    const std::vector<uint8_t>& vram = snapshot.Vdp2Vram();
    // BKTA table address: BKTAU bits 2-0 are the high word bits, BKTAL the low 16;
    // BKTAU bit 15 selects a per-display-line colour (one RGB555 word per line)
    // rather than a single colour for the whole screen.
    const uint16_t bktau = Reg(snapshot, kBKTAU);
    const uint16_t bktal = Reg(snapshot, kBKTAL);
    const uint32_t base = (static_cast<uint32_t>(bktau & 0x0007) << 16) | bktal;
    const bool perLine = (bktau & 0x8000) != 0;

    const size_t need = static_cast<size_t>(width) * height * 4;
    if (outRgba.size() != need)
    {
        outRgba.assign(need, 0);
    }
    for (int y = 0; y < height; ++y)
    {
        const uint32_t wordAddr = base + (perLine ? static_cast<uint32_t>(y) : 0u);
        const Rgba col = Rgb555ToRgba(ReadVdp2Word(vram, wordAddr));
        uint8_t* row = &outRgba[static_cast<size_t>(y) * width * 4];
        for (int x = 0; x < width; ++x)
        {
            row[x * 4 + 0] = col.r;
            row[x * 4 + 1] = col.g;
            row[x * 4 + 2] = col.b;
            row[x * 4 + 3] = 255;
        }
    }
}

}  // namespace se
