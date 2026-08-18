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
    // NBG0/1 fractional scroll + zoom live at 0x070 (NBG0) / 0x080 (NBG1) + these deltas.
    kSCRCTL = 0x09A, kLSTA0 = 0x0A0, kLSTA1 = 0x0A4,
    kWPSX0 = 0x0C0, kWPSY0 = 0x0C2, kWPEX0 = 0x0C4, kWPEY0 = 0x0C6,
    kWPSX1 = 0x0C8, kWPSY1 = 0x0CA, kWPEX1 = 0x0CC, kWPEY1 = 0x0CE,
    kWCTLA = 0x0D0, kWCTLB = 0x0D2, kWCTLC = 0x0D4, kWCTLD = 0x0D6,
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
    kPNCR = 0x038, kMPOFR = 0x03E, kBMPNA = 0x02C, kBMPNB = 0x02E,
    kMPABRA = 0x050, kMPABRB = 0x060,
    kRPMD = 0x0B0, kKTCTL = 0x0B4, kKTAOF = 0x0B6, kOVPNRA = 0x0B8, kOVPNRB = 0x0BA,
    kRPTAU = 0x0BC, kRPTAL = 0x0BE,
    kCRAOFB = 0x0E6, kPRIR = 0x0FC, kCCRR = 0x10C,
    kMZCTL = 0x022,
    kCLOFEN = 0x110, kCLOFSL = 0x112,
    kCOAR = 0x114, kCOAG = 0x116, kCOAB = 0x118,
    kCOBR = 0x11A, kCOBG = 0x11C, kCOBB = 0x11E
};

// Sign-extend the low n bits of v to a signed 32-bit value (Mednafen sign_x_to_s32).
inline int32_t SignX(uint32_t v, int n)
{
    const int s = 32 - n;
    return static_cast<int32_t>(v << s) >> s;
}

uint16_t Reg(const HardwareSnapshot& s, uint32_t hw)
{
    return s.Vdp2Reg(hw);
}

uint32_t CellByteSize(uint32_t colorNum);   // bytes per 8x8 cell; defined below

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
    // Bitmap mode (NBG0/1 and RBG0): the screen is a single linear image rather than a
    // tiled plane. When bitmap is set the plane/pattern fields above are unused.
    bool bitmap;
    uint32_t bitmapW, bitmapH;   // pixel dimensions
    uint32_t bitmapBase;         // VRAM word address of the image
    uint32_t bitmapPalette;      // palette base for the 16/256-colour formats
    uint32_t bitmapBpp;          // bits per pixel (precomputed from colorNum)
    // Fractional scroll + zoom + line scroll (NBG0/1 only). When zoomScroll is set the
    // coordinate is stepped in .8 fixed-point from xScroll8/yScroll8 by xInc8/yInc8, and
    // a per-line scroll/zoom table (scrollCtrl selecting X/Y/zoom) overrides it per line.
    bool zoomScroll;
    uint32_t xScroll8, yScroll8; // .8 fixed base scroll
    uint32_t xInc8, yInc8;       // .8 fixed per-dot / per-line coordinate increment (zoom)
    uint8_t scrollCtrl;          // SCRCTL byte for this NBG
    uint32_t lineScrollBase;     // line scroll/zoom table VRAM word address
    uint32_t mosaicH;            // horizontal mosaic block width (1 = no mosaic)
    bool colorOff;               // per-screen colour offset enabled (CLOFEN)
    int coR, coG, coB;           // signed R/G/B offsets (COxR/G/B), applied post-fetch
};

// Resolve the per-screen colour offset (CLOFEN/CLOFSL select set A or B; the COxR/G/B
// registers are signed 9-bit). 'screen' is 0..3 for NBG0-3, 4 for RBG0.
void ReadColorOffset(const HardwareSnapshot& s, int screen, NbgConfig& c)
{
    c.colorOff = (Reg(s, kCLOFEN) & (1u << screen)) != 0;
    if (!c.colorOff) { c.coR = c.coG = c.coB = 0; return; }
    const bool useB = (Reg(s, kCLOFSL) & (1u << screen)) != 0;
    c.coR = SignX(Reg(s, useB ? kCOBR : kCOAR) & 0x1FF, 9);
    c.coG = SignX(Reg(s, useB ? kCOBG : kCOAG) & 0x1FF, 9);
    c.coB = SignX(Reg(s, useB ? kCOBB : kCOAB) & 0x1FF, 9);
}

// Colour calculation (CCCTL enable bit `screen`, additive via CCMD, and the given 5-bit
// ratio) resolved into a config. Shared by ReadNbgConfig and ReadRbg0Config.
void SetColorCalc(const HardwareSnapshot& s, uint32_t screen, uint32_t ratio, NbgConfig& c)
{
    const uint16_t ccctl = Reg(s, kCCCTL);
    c.colorCalc = (ccctl & (1u << screen)) != 0;
    c.colorCalcRatio = ratio & 0x1F;
    c.colorCalcAdd = (ccctl & 0x0100) != 0;
}

// Horizontal mosaic block width for a screen (MZCTL enable bit `screen`); 1 = no mosaic.
uint32_t ReadMosaicH(const HardwareSnapshot& s, uint32_t screen)
{
    const uint16_t m = Reg(s, kMZCTL);
    return ((m >> screen) & 1) ? (((m >> 8) & 0xF) + 1) : 1;
}

// Apply a screen's colour offset to a fetched texel (clamped to 0..255). No-op when the
// screen has no offset enabled. Runs before color calculation, per hardware.
inline void ApplyColorOffset(Rgba& col, const NbgConfig& c)
{
    if (!c.colorOff) return;
    auto clamp8 = [](int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); };
    col.r = clamp8(col.r + c.coR);
    col.g = clamp8(col.g + c.coG);
    col.b = clamp8(col.b + c.coB);
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

    // Color calculation: the 5-bit ratio lives in CCRNA (N0 low / N1 high) and CCRNB
    // (N2 low / N3 high); CCCTL bit n enables it and CCMD selects additive blending.
    const uint16_t ccrn = (n < 2) ? Reg(s, kCCRNA) : Reg(s, kCCRNB);
    SetColorCalc(s, n, (n & 1) ? (ccrn >> 8) : ccrn, c);

    // Bitmap mode is available on NBG0/1 only (CHCTLA N0/N1BMEN). The bitmap base comes
    // from the map-offset nibble, its size from CHCTLA, and its palette from BMPNA.
    if (n < 2)
    {
        c.bitmap = (cha >> (1 + n * 8)) & 1;
        const uint32_t bmsz = (cha >> (2 + n * 8)) & 0x3;
        c.bitmapW = (bmsz & 0x2) ? 1024 : 512;
        c.bitmapH = (bmsz & 0x1) ? 512 : 256;
        c.bitmapBpp = CellByteSize(c.colorNum) >> 3;
        c.bitmapBase = static_cast<uint32_t>((mpofn >> (n * 4)) & 0x7) << 16;
        c.bitmapPalette = static_cast<uint32_t>((Reg(s, kBMPNA) >> (n * 8)) & 0x7) << 4;

        // Fractional scroll + zoom + line scroll (NBG0/1). Integer + fractional parts form
        // an .8 fixed-point base scroll; the zoom register is the .8 per-dot coordinate
        // increment (0x100 = 1:1). SCRCTL selects per-line X/Y/zoom from the LSTA table.
        const uint32_t base = kSCXIN0 + n * 0x10;
        const uint32_t xi = Reg(s, base + 0x0) & 0x7FF, xf = Reg(s, base + 0x2) >> 8;
        const uint32_t yi = Reg(s, base + 0x4) & 0x7FF, yf = Reg(s, base + 0x6) >> 8;
        const uint32_t zxi = Reg(s, base + 0x8) & 0x7, zxf = Reg(s, base + 0xA) >> 8;
        const uint32_t zyi = Reg(s, base + 0xC) & 0x7, zyf = Reg(s, base + 0xE) >> 8;
        c.xScroll8 = (xi << 8) | xf;
        c.yScroll8 = (yi << 8) | yf;
        c.xInc8 = ((zxi << 8) | zxf); if (c.xInc8 == 0) c.xInc8 = 0x100;
        c.yInc8 = ((zyi << 8) | zyf); if (c.yInc8 == 0) c.yInc8 = 0x100;
        c.scrollCtrl = static_cast<uint8_t>(Reg(s, kSCRCTL) >> (n * 8));
        const uint32_t lsta = kLSTA0 + n * 4;
        c.lineScrollBase = (static_cast<uint32_t>(Reg(s, lsta) & 0x7) << 16) |
                           (Reg(s, lsta + 2) & 0xFFFE);
        c.zoomScroll = true;
    }

    // Mosaic (horizontal) + per-screen colour offset.
    c.mosaicH = ReadMosaicH(s, n);
    ReadColorOffset(s, n, c);
    return c;
}

// Window control is packed two layers per register, in this order. The NBG indices line
// up with the screen numbers, so the NBG callers pass their own index unchanged.
enum : int
{
    kWinLayerNbg0 = 0, kWinLayerNbg1 = 1, kWinLayerNbg2 = 2, kWinLayerNbg3 = 3,
    kWinLayerRbg0 = 4, kWinLayerSprite = 5, kWinLayerRotParam = 6, kWinLayerColorCalc = 7
};

WindowConfig ReadWindowConfig(const HardwareSnapshot& s, int n)
{
    static const uint32_t kWctl[4] = { kWCTLA, kWCTLB, kWCTLC, kWCTLD };
    const uint16_t wctl = Reg(s, kWctl[(n >> 1) & 0x3]);
    // The rotation parameter window has no sprite-window input, so its byte is narrower.
    const uint8_t mask = (n == kWinLayerRotParam) ? 0x8F : 0xBF;
    const uint8_t control = static_cast<uint8_t>((wctl >> ((n & 1) * 8)) & mask);
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
        // The 32-bit RGB word is stored big-endian as [code][B][G][R]: the two VDP2
        // half-words are code|B and G|R, so R is the last byte and B the first (after the
        // code byte). Reading them as R,G,B swapped red and blue (Mednafen mixit reads the
        // dword's low 24 bits as 0xBBGGRR).
        return { vram[off + 3], vram[off + 2], vram[off + 1], 255 };
    }
    }
}

// Sample one bitmap-mode texel at plane coordinate (ix, iy). Reuses FetchCellTexel by
// treating the horizontal 8-pixel group as a one-row "cell": the group's byte address is
// cellBase and the pixel within it is (ix & 7). Wraps to the bitmap dimensions.
Rgba FetchBitmapTexel(const std::vector<uint8_t>& vram, const std::vector<uint8_t>& cram,
                      se_cram_mode cramMode, const NbgConfig& c, int ix, int iy)
{
    const uint32_t xw = static_cast<uint32_t>(ix) & (c.bitmapW - 1);
    const uint32_t yw = static_cast<uint32_t>(iy) & (c.bitmapH - 1);
    const uint32_t cellBase =
        (c.bitmapBase * 2 + (((yw * c.bitmapW + (xw & ~7u)) * c.bitmapBpp) >> 3)) & 0x7FFFFu;
    PatternName pn {};
    pn.palette = c.bitmapPalette;
    return FetchCellTexel(vram, cram, cramMode, c, pn, cellBase,
                          static_cast<int>(xw & 7u), 0);
}

// Emit one resolved texel into its column as a descriptor at the layer's priority,
// carrying the layer's colour-calculation parameters. The mixer applies colour
// calculation at resolve time (only ever blending the top pixel with the one below),
// so the per-layer blend that CompositeTexel used to do inline now happens once, later.
inline void EmitTexel(PixColumn& col, const Rgba& c, const NbgConfig& cfg)
{
    EmitPix(col, c.r, c.g, c.b, static_cast<uint8_t>(cfg.priority),
            cfg.colorCalc, static_cast<uint8_t>(cfg.colorCalcRatio), cfg.colorCalcAdd);
}

// VRAM word address of one plane's pattern-name table, from the combined map-offset +
// page register and the plane geometry. Shared by the NBG (4-plane) and RBG0 (16-plane)
// map builds.
uint32_t PlaneBaseFor(uint32_t tmp, bool oneWord, uint32_t patternWH,
                      uint32_t deca, uint32_t multi)
{
    if (oneWord)
        return (patternWH == 1) ? (((tmp & 0x3F) >> deca) * (multi * 0x2000))
                                : ((tmp >> deca) * (multi * 0x800));
    return (patternWH == 1) ? (((tmp & 0x1F) >> deca) * (multi * 0x4000))
                            : (((tmp & 0x7F) >> deca) * (multi * 0x1000));
}

// Geometry of a tiled screen's plane->page->pattern->cell layout, resolved once per layer.
struct PlaneGeom
{
    const uint32_t* planeBase;   // per-plane pattern-name table base (word address)
    uint32_t planesPerRow;       // 2 for the NBG 2x2 grid, 4 for the RBG0 4x4 grid
    uint32_t planeW;             // pages per plane along X (1 or 2)
    uint32_t planePixW, planePixH;
    uint32_t cellWH, pageCells, pnBytes, cellBytes;
};

// Sample one texel from a tiled screen at plane-space (x, y), already wrapped to the
// screen's total dimensions. This is the shared plane/page/pattern/cell walk used by both
// the NBG and RBG0 renderers (they differ only in plane-grid width and coordinate source).
Rgba FetchPlaneTexel(const std::vector<uint8_t>& vram, const std::vector<uint8_t>& cram,
                     se_cram_mode cramMode, const NbgConfig& c, uint16_t vrsize,
                     const PlaneGeom& g, uint32_t x, uint32_t y)
{
    const uint32_t plane = (y / g.planePixH) * g.planesPerRow + (x / g.planePixW);
    const uint32_t px = x % g.planePixW;
    const uint32_t py = y % g.planePixH;

    // Pattern-name tables are page-major: address the enclosing page, then the pattern
    // within it (a plain patY*pageCells+patX stride collides across page columns).
    const uint32_t patX = px / g.cellWH;
    const uint32_t patY = py / g.cellWH;
    const uint32_t pageIndex = (patY / g.pageCells) * g.planeW + (patX / g.pageCells);
    const uint32_t patIndex = pageIndex * (g.pageCells * g.pageCells) +
                              (patY % g.pageCells) * g.pageCells + (patX % g.pageCells);
    const PatternName pn = DecodePatternName(
        vram, g.planeBase[plane] + patIndex * g.pnBytes, c, vrsize);

    // Pixel within the pattern, then within its 8x8 sub-cell.
    uint32_t inX = px % g.cellWH;
    uint32_t inY = py % g.cellWH;
    if (pn.flip & 1) inX = g.cellWH - 1 - inX;
    if (pn.flip & 2) inY = g.cellWH - 1 - inY;
    const uint32_t subCell = (inY / 8) * c.patternWH + (inX / 8);
    const uint32_t cellBase = (pn.charBase + subCell * g.cellBytes) & 0x7FFFFu;
    return FetchCellTexel(vram, cram, cramMode, c, pn, cellBase,
                          static_cast<int>(inX % 8), static_cast<int>(inY % 8));
}

// Emit one NBG layer into 'cols' (width*height PixColumns): each opaque cell texel emits
// a descriptor at the layer's priority, transparent texels emit nothing. Faithful port of
// the validated plane/page/cell walk.
void RenderLayer(const HardwareSnapshot& snap, const NbgConfig& c, int layerIndex,
                 bool applyWindows, int width, int height,
                 std::vector<PixColumn>& cols)
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
        planeBase[i] = PlaneBaseFor(c.mapOffset | pageRegs[i], oneWord, c.patternWH, deca, multi);

    const uint32_t planePixW = planeW * 512;
    const uint32_t planePixH = planeH * 512;
    const uint32_t xMask = 2 * planePixW - 1;
    const uint32_t yMask = 2 * planePixH - 1;
    const PlaneGeom geom {
        planeBase.data(), 2, planeW, planePixW, planePixH,
        8 * c.patternWH, 64u >> (c.patternWH - 1), oneWord ? 2u : 4u, CellByteSize(c.colorNum) };

    for (int sy = 0; sy < height; ++sy)
    {
        const WindowLine windowLines[2] = {
            ResolveWindowLine(windowConfig, 0, vram, sy),
            ResolveWindowLine(windowConfig, 1, vram, sy)
        };
        // Per-line horizontal start (.8), per-dot increment (.8), and the plane-space Y.
        // NBG2/3 use plain integer scroll; NBG0/1 add fractional scroll, zoom, and an
        // optional per-line scroll/zoom table (SCRCTL/LSTA).
        uint32_t xcStart = c.scrollX << 8, xcinc = 0x100;
        int yCoord = c.scrollY + sy;
        if (c.zoomScroll)
        {
            const uint8_t sc = c.scrollCtrl;
            const uint32_t lss = (sc >> 4) & 0x3;
            uint32_t lineX8 = c.xScroll8, lineY8 = c.yScroll8, inc = c.xInc8;
            bool haveLineY = false;
            if (sc & 0x0E)   // any per-line table field enabled
            {
                const uint32_t fields = ((sc >> 1) & 1) + ((sc >> 2) & 1) + ((sc >> 3) & 1);
                uint32_t addr = c.lineScrollBase +
                                (static_cast<uint32_t>(sy) >> lss) * fields * 2;
                if (sc & 0x2)   // per-line X scroll
                {
                    lineX8 = (((ReadVdp2Word(vram, addr) & 0x7FF) << 8) |
                              (ReadVdp2Word(vram, addr + 1) >> 8)) + c.xScroll8;
                    addr += 2;
                }
                if (sc & 0x4)   // per-line Y scroll
                {
                    lineY8 = (((ReadVdp2Word(vram, addr) & 0x7FF) << 8) |
                              (ReadVdp2Word(vram, addr + 1) >> 8)) + c.yScroll8;
                    haveLineY = true;
                    addr += 2;
                }
                if (sc & 0x8)   // per-line X zoom
                {
                    inc = ((ReadVdp2Word(vram, addr) & 0x7) << 8) |
                          (ReadVdp2Word(vram, addr + 1) >> 8);
                    if (inc == 0) inc = 0x100;
                }
            }
            xcStart = lineX8;
            xcinc = inc;
            yCoord = haveLineY ? static_cast<int>(lineY8 >> 8)
                               : static_cast<int>((c.yScroll8 + c.yInc8 * sy) >> 8);
        }
        for (int sx = 0; sx < width; ++sx)
        {
            if (applyWindows && WindowMasksPixel(windowConfig.control, windowLines, sx))
            {
                continue;
            }
            // Horizontal mosaic replicates each block's leftmost dot across the block.
            const int msx = (c.mosaicH > 1) ? sx - (sx % static_cast<int>(c.mosaicH)) : sx;
            const int sampleX = static_cast<int>((xcStart + xcinc * msx) >> 8);
            // Bitmap mode indexes the linear image directly; cell mode walks the plane.
            Rgba col = c.bitmap
                ? FetchBitmapTexel(vram, cram, cramMode, c, sampleX, yCoord)
                : FetchPlaneTexel(vram, cram, cramMode, c, vrsize, geom,
                                  static_cast<uint32_t>(sampleX) & xMask,
                                  static_cast<uint32_t>(yCoord) & yMask);
            if (col.a == 0)
            {
                continue;
            }
            ApplyColorOffset(col, c);
            EmitTexel(cols[static_cast<size_t>(sy) * width + sx], col, c);
        }
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
    SetColorCalc(s, 4, Reg(s, kCCRR), c);

    // RBG0 bitmap mode (CHCTLB R0BMEN): 512-wide, 256 or 512 tall.
    c.bitmap = (chb >> 9) & 1;
    c.bitmapW = 512;
    c.bitmapH = ((chb >> 10) & 1) ? 512 : 256;
    c.bitmapBpp = CellByteSize(c.colorNum) >> 3;
    c.bitmapBase = static_cast<uint32_t>((mpofr >> (paramB ? 4 : 0)) & 0x7) << 16;
    c.bitmapPalette = static_cast<uint32_t>(Reg(s, kBMPNB) & 0x7) << 4;

    c.mosaicH = ReadMosaicH(s, 4);
    ReadColorOffset(s, 4, c);
    return c;
}

// Word address of one coefficient-table entry. The running offset is .10 fixed point;
// 32-bit entries occupy two words, so the integer part is doubled for them. CRKTE moves
// the table out of VDP2 VRAM and into the upper half of colour RAM, which is far smaller.
uint32_t CoeffAddr(uint32_t offset, bool coeffWord, bool crkte)
{
    offset >>= 10;
    if (!coeffWord) offset <<= 1;
    return offset & (crkte ? 0x3FFu : 0x3FFFFu);
}

// One coefficient, normalised to the 32-bit layout: bit 31 = transparent, bits 23-0 = the
// signed value. The 16-bit format carries the same fields packed into one word.
uint32_t ReadCoeff(const std::vector<uint8_t>& vram, const std::vector<uint8_t>& cram,
                   bool crkte, bool coeffWord, uint32_t addr)
{
    auto word = [&](uint32_t w) -> uint16_t
    {
        if (!crkte) return ReadVdp2Word(vram, w);
        // Colour RAM holds the table at word 0x400 and up.
        const size_t byte = (0x400u + (w & 0x3FFu)) * 2;
        return (byte + 1 < cram.size())
            ? static_cast<uint16_t>((cram[byte] << 8) | cram[byte + 1]) : uint16_t(0);
    };
    if (coeffWord)
    {
        const uint16_t t = word(addr);
        return (static_cast<uint32_t>(SignX(t << 6, 21)) & 0x00FFFFFFu) |
               (static_cast<uint32_t>(t & 0x8000) << 16);
    }
    return (static_cast<uint32_t>(word(addr)) << 16) | word(addr + 1);
}

// One rotation parameter set (A or B) resolved in full: the parameter table, the RBG0
// cell/plane configuration it selects (PLSZ, MPOFR and the map registers are per-set),
// and its coefficient-table settings. RPMD can choose between the two sets per dot, so
// both are always built.
struct RotSet
{
    RotParam                 rp {};
    NbgConfig                cfg {};
    std::array<uint32_t, 16> planeBase {};
    PlaneGeom                geom {};
    uint32_t totalW = 0, totalH = 0;
    uint32_t screenOver = 0;
    bool     useCoeff = false;
    bool     coeffWord = false;
    uint32_t coeffMode = 0;

    // Per-line state, recomputed by BeginRotLine.
    int64_t  Xsp = 0, Ysp = 0;
    int32_t  XpBase = 0, Yp = 0, dX = 0, dY = 0;
    uint32_t KAstLine = 0;
};

void BuildRotSet(const HardwareSnapshot& snap, bool paramB, RotSet& s)
{
    const std::vector<uint8_t>& vram = snap.Vdp2Vram();

    const uint32_t rpta = ((static_cast<uint32_t>(Reg(snap, kRPTAU) & 0x0007) << 16) |
                           Reg(snap, kRPTAL)) & 0x7FFBE;
    s.rp = FetchRotParam(vram, rpta + (paramB ? 0x40 : 0));
    // KTAOF supplies the coefficient table's high address bits (A18-A16). KAst is .10
    // fixed point, so the register's 3-bit field lands at bit 26. Without this the two
    // parameter sets read the same table, which is exactly what a split-screen rotation
    // (a horizon: one set above, the other below) relies on being different.
    s.rp.KAst += static_cast<uint32_t>((Reg(snap, kKTAOF) >> (paramB ? 8 : 0)) & 0x7) << 26;

    const uint16_t ktctl = Reg(snap, kKTCTL) >> (paramB ? 8 : 0);
    s.useCoeff = (ktctl & 0x1) != 0;
    s.coeffWord = (ktctl & 0x2) != 0;   // 1 = 16-bit coeff, 0 = 32-bit
    s.coeffMode = (ktctl >> 2) & 0x3;
    s.screenOver = (Reg(snap, kPLSZ) >> (paramB ? 14 : 10)) & 0x3;

    s.cfg = ReadRbg0Config(snap, paramB);

    // Plane geometry: RBG0 tiles a 4x4 grid of 16 planes.
    const NbgConfig& c = s.cfg;
    const uint32_t planeW = (c.planeSize & 0x1) ? 2 : 1;
    const uint32_t planeH = (c.planeSize & 0x2) ? 2 : 1;
    const uint32_t deca = planeH + planeW - 2;
    const uint32_t multi = planeH * planeW;
    const bool oneWord = (c.patternCtrl & 0x8000) != 0;
    const uint32_t mapBase = paramB ? kMPABRB : kMPABRA;
    for (int i = 0; i < 16; ++i)
    {
        const uint16_t w = Reg(snap, mapBase + (i / 2) * 2);
        const uint32_t pageReg = (i & 1) ? ((w >> 8) & 0x3F) : (w & 0x3F);
        s.planeBase[i] = PlaneBaseFor(c.mapOffset | pageReg, oneWord, c.patternWH, deca, multi);
    }

    const uint32_t planePixW = planeW * 512;
    const uint32_t planePixH = planeH * 512;
    s.totalW = 4 * planePixW;   // power of two — screen-over "repeat" masks
    s.totalH = 4 * planePixH;
    s.geom = PlaneGeom {
        s.planeBase.data(), 4, planeW, planePixW, planePixH,
        8 * c.patternWH, 64u >> (c.patternWH - 1), oneWord ? 2u : 4u, CellByteSize(c.colorNum) };
}

// Per-line rotation setup (Xst/Yst accumulate DXst/DYst down the screen).
void BeginRotLine(RotSet& s, int sy)
{
    const RotParam& rp = s.rp;
    const int32_t XstA = rp.Xst + rp.DXst * sy;
    const int32_t YstA = rp.Yst + rp.DYst * sy;
    s.Xsp = ((int64_t)rp.M[0] * (XstA - rp.Px * 1024) +
             (int64_t)rp.M[1] * (YstA - rp.Py * 1024) +
             (int64_t)rp.M[2] * (rp.Zst - rp.Pz * 1024)) >> 10;
    s.Ysp = ((int64_t)rp.M[3] * (XstA - rp.Px * 1024) +
             (int64_t)rp.M[4] * (YstA - rp.Py * 1024) +
             (int64_t)rp.M[5] * (rp.Zst - rp.Pz * 1024)) >> 10;
    s.XpBase = rp.M[0] * (rp.Px - rp.Cx) + rp.M[1] * (rp.Py - rp.Cy) +
               rp.M[2] * (rp.Pz - rp.Cz) + rp.Cx * 1024 + rp.Mx;
    s.Yp = rp.M[3] * (rp.Px - rp.Cx) + rp.M[4] * (rp.Py - rp.Cy) +
           rp.M[5] * (rp.Pz - rp.Cz) + rp.Cy * 1024 + rp.My;
    s.dX = (rp.M[0] * rp.DX + rp.M[1] * rp.DY) >> 10;
    s.dY = (rp.M[3] * rp.DX + rp.M[4] * rp.DY) >> 10;
    s.KAstLine = rp.KAst + static_cast<uint32_t>(rp.DKAst) * static_cast<uint32_t>(sy);
}

// Composite the RBG0 rotation screen. For each screen dot a rotation parameter set is
// evaluated (matrix + view/centre/move + per-line accumulation + optional per-dot
// coefficient table) to a plane-space coordinate, which indexes RBG0's 4x4 grid of 16
// planes via the same page->pattern->cell walk the NBGs use (or the bitmap image in
// bitmap mode); screen-over "repeat" wraps, other modes read transparent outside.
//
// RPMD picks the set: 0/1 use A/B for the whole screen, 2 switches per dot on the sign
// bit of A's coefficient, and 3 switches per dot on the rotation parameter window. Modes
// 2 and 3 are how a game draws a horizon — one set for the sky, the other for the ground
// — so treating them as "always A" leaves half the screen sampling the wrong table.
void RenderRbg0(const HardwareSnapshot& snap, uint32_t rpmd, bool applyWindows,
                int width, int height, std::vector<PixColumn>& cols)
{
    const std::vector<uint8_t>& vram = snap.Vdp2Vram();
    const std::vector<uint8_t>& cram = snap.Cram();
    const se_cram_mode cramMode = snap.CramMode();
    const uint16_t vrsize = Reg(snap, kVRSIZE);
    const bool crkte = (Reg(snap, kRAMCTL) & 0x8000) != 0;

    RotSet sets[2];
    BuildRotSet(snap, false, sets[0]);
    BuildRotSet(snap, true, sets[1]);

    // RBG0's own transparent-processing window (WCTLC low byte), and separately the
    // rotation parameter window (WCTLD low byte) that mode 3 selects the set with. The
    // latter is geometry, not masking, so it applies even when window display is off.
    const WindowConfig windowConfig = ReadWindowConfig(snap, kWinLayerRbg0);
    const WindowConfig rotWindowConfig = ReadWindowConfig(snap, kWinLayerRotParam);

    auto coeffAt = [&](const RotSet& s, int msx) -> uint32_t
    {
        const uint32_t off = s.KAstLine +
                             static_cast<uint32_t>(s.rp.DKAx) * static_cast<uint32_t>(msx);
        return ReadCoeff(vram, cram, crkte, s.coeffWord, CoeffAddr(off, s.coeffWord, crkte));
    };

    for (int sy = 0; sy < height; ++sy)
    {
        const WindowLine windowLines[2] = {
            ResolveWindowLine(windowConfig, 0, vram, sy),
            ResolveWindowLine(windowConfig, 1, vram, sy)
        };
        const WindowLine rotWindowLines[2] = {
            ResolveWindowLine(rotWindowConfig, 0, vram, sy),
            ResolveWindowLine(rotWindowConfig, 1, vram, sy)
        };
        BeginRotLine(sets[0], sy);
        BeginRotLine(sets[1], sy);
        // In mode 2 set B contributes only the coefficient sampled at the start of the
        // line; the per-dot walk is done in set A's table alone.
        const uint32_t baseCoeffB = sets[1].useCoeff ? coeffAt(sets[1], 0) : 0u;

        for (int sx = 0; sx < width; ++sx)
        {
            if (applyWindows && WindowMasksPixel(windowConfig.control, windowLines, sx))
            {
                continue;
            }
            // Horizontal mosaic snaps the sampled dot to its block's left edge.
            const int msx = (sets[0].cfg.mosaicH > 1)
                ? sx - (sx % static_cast<int>(sets[0].cfg.mosaicH)) : sx;

            // Choose the parameter set for this dot, and with it the coefficient.
            int ab = (rpmd == 1) ? 1 : 0;
            uint32_t coeff = 0;
            bool haveCoeff = false;
            if (rpmd == 3)
            {
                ab = WindowMasksPixel(rotWindowConfig.control, rotWindowLines, sx) ? 1 : 0;
            }
            else if (rpmd == 2 && sets[0].useCoeff)
            {
                const uint32_t ca = coeffAt(sets[0], msx);
                ab = static_cast<int>(ca >> 31);
                coeff = (static_cast<int32_t>(ca) < 0) ? baseCoeffB : ca;
                haveCoeff = true;
            }
            const RotSet& s = sets[ab];
            const NbgConfig& c = s.cfg;

            int32_t kx = s.rp.kx, ky = s.rp.ky, Xp = s.XpBase;
            if (s.useCoeff)
            {
                // One coefficient per dot along the line; it can override kx/ky or Xp.
                const uint32_t k = haveCoeff ? coeff : coeffAt(s, msx);
                if (static_cast<int32_t>(k) < 0) continue;   // coefficient = transparent
                const int32_t sext = SignX(k, 24);
                switch (s.coeffMode)
                {
                case 0: kx = ky = sext; break;
                case 1: kx = sext; break;
                case 2: ky = sext; break;
                default: Xp = sext << 2; break;
                }
            }

            const int32_t ixs = static_cast<int32_t>(
                (Xp + static_cast<int32_t>(((int64_t)kx * (int32_t)(s.Xsp + s.dX * msx)) >> 16)) >> 10);
            const int32_t iys = static_cast<int32_t>(
                (s.Yp + static_cast<int32_t>(((int64_t)ky * (int32_t)(s.Ysp + s.dY * msx)) >> 16)) >> 10);

            // Screen-over: mode 0 repeats (wrap); other modes read transparent outside.
            if (s.screenOver != 0 &&
                (ixs < 0 || iys < 0 || static_cast<uint32_t>(ixs) >= s.totalW ||
                 static_cast<uint32_t>(iys) >= s.totalH))
            {
                continue;
            }
            Rgba col = c.bitmap
                ? FetchBitmapTexel(vram, cram, cramMode, c, ixs, iys)
                : FetchPlaneTexel(vram, cram, cramMode, c, vrsize, s.geom,
                                  static_cast<uint32_t>(ixs) & (s.totalW - 1),
                                  static_cast<uint32_t>(iys) & (s.totalH - 1));
            if (col.a == 0)
            {
                continue;
            }
            ApplyColorOffset(col, c);
            EmitTexel(cols[static_cast<size_t>(sy) * width + sx], col, c);
        }
    }
}

}  // namespace

void Vdp2Compositor::EmitLayers(const HardwareSnapshot& snapshot, const se_render_opts& opts,
                               int width, int height, std::vector<PixColumn>& cols)
{
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
    };
    std::vector<Layer> layers;
    auto consider = [&](const Layer& layer)
    {
        if (layer.config.priority == 0) return;    // priority 0 = not displayed
        layers.push_back(layer);
    };
    for (int n = 0; n < 4; ++n)
    {
        if (!(bgon & (1u << n)) || !opts.show_layer[n])
        {
            continue;
        }
        consider({ n, ReadNbgConfig(snapshot, n), false });
    }
    // RBG0 (rotation) occupies BGON bit 4. RPMD selects the rotation parameter set, which
    // can vary per dot; RenderRbg0 resolves that itself. Priority and colour calculation
    // come from RBG0's own registers either way, so set A's config orders the layer.
    if ((bgon & (1u << 4)) && opts.show_layer[SE_LAYER_RBG0])
    {
        consider({ 4, ReadRbg0Config(snapshot, false), true });
    }

    // Emit order = back to front: ascending priority; for equal priority the
    // higher-numbered NBG is emitted first, so on ties EmitPix's later-wins rule leaves
    // NBG0 on top. Emitting in this order also means a same-priority sprite (emitted
    // after all layers) wins its tie against the NBGs, matching hardware.
    std::stable_sort(layers.begin(), layers.end(), [](const Layer& a, const Layer& b)
    {
        if (a.config.priority != b.config.priority) return a.config.priority < b.config.priority;
        return a.index > b.index;
    });

    for (const Layer& layer : layers)
    {
        if (layer.rbg0)
        {
            RenderRbg0(snapshot, Reg(snapshot, kRPMD) & 0x3, opts.show_window != 0,
                       width, height, cols);
        }
        else
        {
            RenderLayer(snapshot, layer.config, layer.index, opts.show_window != 0,
                        width, height, cols);
        }
    }
}

void Vdp2Compositor::SeedBackScreen(const HardwareSnapshot& snapshot, int width,
                                    int height, std::vector<PixColumn>& cols)
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

    for (int y = 0; y < height; ++y)
    {
        const uint32_t wordAddr = base + (perLine ? static_cast<uint32_t>(y) : 0u);
        const Rgba col = Rgb555ToRgba(ReadVdp2Word(vram, wordAddr));
        PixColumn* row = &cols[static_cast<size_t>(y) * width];
        for (int x = 0; x < width; ++x)
        {
            // Priority 0: the always-below backdrop. Marks the column valid so the
            // fallback backdrop never shows where VDP2 is present, and gives the lowest
            // colour-calc layer a real surface to blend against.
            EmitPix(row[x], col.r, col.g, col.b, 0, false, 0, false);
        }
    }
}

}  // namespace se
