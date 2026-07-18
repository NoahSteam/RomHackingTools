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
    kVRSIZE = 0x006, kRAMCTL = 0x00E, kBGON = 0x020,
    kCHCTLA = 0x028, kCHCTLB = 0x02A,
    kPNCN0 = 0x030, kPNCN1 = 0x032, kPNCN2 = 0x034, kPNCN3 = 0x036,
    kPLSZ = 0x03A, kMPOFN = 0x03C,
    kMPABN0 = 0x040, kMPCDN0 = 0x042, kMPABN1 = 0x044, kMPCDN1 = 0x046,
    kMPABN2 = 0x048, kMPCDN2 = 0x04A, kMPABN3 = 0x04C, kMPCDN3 = 0x04E,
    kSCXIN0 = 0x070, kSCYIN0 = 0x074, kSCXIN1 = 0x080, kSCYIN1 = 0x084,
    kSCXN2 = 0x090, kSCYN2 = 0x092, kSCXN3 = 0x094, kSCYN3 = 0x096,
    kSPCTL = 0x0E0,
    kPRISA = 0x0F0, kPRISB = 0x0F2, kPRISC = 0x0F4, kPRISD = 0x0F6,
    kCRAOFA = 0x0E4, kPRINA = 0x0F8, kPRINB = 0x0FA
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
};

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
    return c;
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
        if (dot == 0) return { 0, 0, 0, 0 };
        return CramColor(cram, cramMode, c.colorOffset + (p.palette | dot));
    }
    case 1:   // 256-color (8 bpp)
    {
        const uint32_t off = cellBase + pix;
        const uint8_t dot = (off < vram.size()) ? vram[off] : 0;
        if (dot == 0) return { 0, 0, 0, 0 };
        return CramColor(cram, cramMode, c.colorOffset + (p.palette | dot));
    }
    case 2:   // 2048-color (16 bpp palette)
    {
        const uint16_t dot = ReadBE16(vram, cellBase + pix * 2) & 0x7FF;
        if (dot == 0) return { 0, 0, 0, 0 };
        return CramColor(cram, cramMode, c.colorOffset + dot);
    }
    case 3:   // 32K-color (16 bpp RGB555)
    {
        const uint16_t dot = ReadBE16(vram, cellBase + pix * 2);
        if (!(dot & 0x8000)) return { 0, 0, 0, 0 };
        return Rgb555ToRgba(dot);
    }
    default:  // 16M-color (32 bpp RGB888)
    {
        const uint32_t off = cellBase + pix * 4;
        if (off + 3 >= vram.size()) return { 0, 0, 0, 0 };
        if (!(vram[off] & 0x80)) return { 0, 0, 0, 0 };   // MSB = transparency
        return { vram[off + 1], vram[off + 2], vram[off + 3], 255 };
    }
    }
}

// Composite one NBG layer into 'out' (width*height*4): each opaque cell texel
// overwrites the pixel, transparent texels leave whatever a farther layer drew.
// 'out' must already be sized and cleared/painted; layers are drawn back-to-front
// so this needs no per-layer scratch buffer. Faithful port of the validated
// plane/page/cell walk.
void RenderLayer(const HardwareSnapshot& snap, const NbgConfig& c, int width, int height,
                 std::vector<uint8_t>& out)
{
    const std::vector<uint8_t>& vram = snap.Vdp2Vram();
    const std::vector<uint8_t>& cram = snap.Cram();
    const se_cram_mode cramMode = snap.CramMode();
    const uint16_t vrsize = Reg(snap, kVRSIZE);

    // Plane arrangement from the 2-bit plane-size field: 1x1, 2x1, or 2x2 pages.
    uint32_t planeW = 1, planeH = 1;
    if (c.planeSize == 1) { planeW = 2; }
    else if (c.planeSize == 3) { planeW = 2; planeH = 2; }
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
        for (int sx = 0; sx < width; ++sx)
        {
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
            const uint32_t cellBase = pn.charBase + subCell * cellBytes;

            const Rgba col = FetchCellTexel(vram, cram, cramMode, c, pn, cellBase,
                                            static_cast<int>(inX % 8), static_cast<int>(inY % 8));
            if (col.a == 0)
            {
                continue;
            }
            const size_t o = (static_cast<size_t>(sy) * width + sx) * 4;
            out[o + 0] = col.r;
            out[o + 1] = col.g;
            out[o + 2] = col.b;
            out[o + 3] = 255;
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
        int index;
        NbgConfig config;
    };
    std::vector<Layer> layers;
    for (int n = 0; n < 4; ++n)
    {
        if (!(bgon & (1u << n)) || !opts.show_layer[n])
        {
            continue;
        }
        const NbgConfig c = ReadNbgConfig(snapshot, n);
        if (c.priority == 0)
        {
            continue;   // priority 0 = not displayed on hardware
        }
        if (static_cast<int>(c.priority) < minPriority ||
            static_cast<int>(c.priority) > maxPriority)
        {
            continue;   // outside the requested band (behind/in-front of sprites)
        }
        layers.push_back({ n, c });
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
        RenderLayer(snapshot, layer.config, width, height, outRgba);
    }
}

}  // namespace se
