// AsmCodeWindow — the span of memory the Assembly panel lets you scroll through.
//
// The panel is a virtual list over the WHOLE captured region the view is framed in: every
// 2-byte instruction of it is a row, and only the rows on screen are read and decoded. SH-2
// instructions are fixed-width, so any even address decodes on its own and there is no
// "window" to rebuild as the view moves.
//
// The span has to stay inside one captured region: ContextBackend rejects a request that
// leaves the region rather than serving the part of it that is mapped, so a read starting even
// two bytes early turns every line in it into "????". Regions are not contiguous in the Saturn
// address space (LWRAM and HWRAM are 0x5E00000 apart), so scrolling stops at a region's edge;
// moving to another region is a jump (goto, branch, call stack).
//
// Header-only, and depending on nothing but the region table, so it is unit-testable without
// linking the panel (and everything the panel pulls in).
#pragma once

#include <cstddef>
#include <cstdint>

#include "SaturnRegions.h"

namespace sfe
{

struct AsmSpan
{
    uint32_t base = 0;   // address of the first instruction row
    uint32_t size = 0;   // bytes the span covers (a multiple of 2)
};

// Size of the span used for an address in no captured region (BIOS, cartridge): nothing there
// reads either way, so it only has to keep row arithmetic bounded.
constexpr uint32_t kAsmUnmappedSpan = 0x10000u;

// The span a view framed on 'anchor' scrolls through: the anchor's whole region. The span keeps
// the anchor's mirror (cached 0x0600.., uncached 0x2600..) rather than being moved into the
// canonical one, so the addresses the rows show match the ones the user navigated with.
inline AsmSpan AsmSpanFor(uint32_t anchor)
{
    anchor &= ~1u;
    AsmSpan s;
    const int ri = SaturnRegionIndex(anchor);
    if (ri >= 0)
    {
        size_t count = 0;
        const SaturnRegion* regions = SaturnRegions(count);
        const SaturnRegion& r = regions[static_cast<size_t>(ri)];
        const uint32_t into = (anchor & 0x07FFFFFFu) - r.base;   // bytes between region start and anchor
        s.base = anchor - into;
        s.size = r.size & ~1u;
    }
    else
    {
        s.base = anchor & ~(kAsmUnmappedSpan - 1u);
        s.size = kAsmUnmappedSpan;
    }
    return s;
}

}  // namespace sfe
