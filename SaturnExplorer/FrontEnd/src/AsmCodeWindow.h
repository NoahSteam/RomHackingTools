// AsmCodeWindow — which bytes the Assembly panel should decode for a view framed on one address.
//
// The panel keeps a lead of instructions BEFORE its anchor so there is code above it to scroll
// back into. That lead has to respect the captured regions: the panel reads its whole window in
// ONE request, and ContextBackend rejects a request that leaves the region rather than serving
// the part of it that is mapped. So a window starting even two bytes early turns every line in
// it into "????" -- which is exactly what an unclamped lead does at 0x06000000, where
// anchor - 192 lands below HWRAM.
//
// Header-only, and depending on nothing but the region table, so it is unit-testable without
// linking the panel (and everything the panel pulls in).
#pragma once

#include <cstddef>
#include <cstdint>

#include "SaturnRegions.h"

namespace sfe
{

struct AsmCodeWindow
{
    uint32_t base = 0;          // address of the first instruction to decode
    int      instructions = 0;  // instructions the window covers (2 bytes each)
};

// The window for a view framed on 'anchor': 'leadInstr' instructions before it, 'instrCount'
// in total, both trimmed to what the anchor's region can actually serve. Clamping is done as
// distances from the anchor rather than against the region's own base, so an address given in a
// mirror keeps that mirror instead of being silently moved into the canonical one.
//
// An anchor in no captured region (BIOS, cartridge) is left unclamped: nothing there reads
// either way, so there is no better window to choose.
inline AsmCodeWindow AsmWindowFor(uint32_t anchor, int instrCount, int leadInstr)
{
    anchor &= ~1u;
    uint32_t lead = static_cast<uint32_t>(leadInstr) * 2u;
    uint32_t len  = static_cast<uint32_t>(instrCount) * 2u;
    if (lead > len)    lead = len;      // a lead past the window's own size means nothing
    if (lead > anchor) lead = anchor;   // and it must not run below address 0

    const int ri = SaturnRegionIndex(anchor);
    if (ri >= 0)
    {
        size_t count = 0;
        const SaturnRegion* regions = SaturnRegions(count);
        const SaturnRegion& r = regions[static_cast<size_t>(ri)];
        const uint32_t a = anchor & 0x07FFFFFFu;
        const uint32_t before = a - r.base;                 // readable bytes behind the anchor
        const uint32_t after  = (r.base + r.size) - a;      // readable bytes from it onward
        if (lead > before)      lead = before & ~1u;
        if (len > lead + after) len = lead + after;
    }

    AsmCodeWindow w;
    w.base = anchor - lead;
    w.instructions = static_cast<int>(len / 2u);
    return w;
}

}  // namespace sfe
