// AsmWindowFor — the Assembly panel's decode window, including its lead-in.
//
// The panel reads its whole window in ONE request and ContextBackend rejects a request that
// leaves the region rather than serving the mapped part of it. So a window that starts even two
// bytes before its region turns every line into "????" -- which is what an unclamped 192-byte
// lead does at 0x06000000, the first address of HWRAM and a perfectly ordinary place to stop.
// These cases pin the clamp at both ends, and that a mirrored address keeps its mirror.
#include "AsmCodeWindow.h"

#include <cstdio>
#include <cstdint>

using namespace sfe;

namespace
{
int gFailures;

#define CHECK(cond) do {                                                      \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } \
} while (0)

constexpr int kInstr = 256;   // the panel's kWinInstr
constexpr int kLead  = 96;    // the panel's kWinLead
constexpr uint32_t kHwram = 0x06000000u;
constexpr uint32_t kHwramSize = 0x100000u;

AsmCodeWindow Win(uint32_t anchor) { return AsmWindowFor(anchor, kInstr, kLead); }

// The reported bug: the first instruction of HWRAM. The lead must collapse rather than start
// below the region, or the single window read fails and the whole view shows "????".
void TestRegionStartKeepsTheWindowInsideTheRegion()
{
    const AsmCodeWindow w = Win(kHwram);
    CHECK(w.base == kHwram);                       // not kHwram - 192
    CHECK(w.instructions == kInstr);               // and nothing lost below the anchor
    CHECK(w.base + (uint32_t)w.instructions * 2u <= kHwram + kHwramSize);
}

// Well inside a region the lead applies in full -- this is what gives the scroll-back room.
void TestInteriorAnchorGetsTheFullLead()
{
    const uint32_t anchor = kHwram + 0x8000u;
    const AsmCodeWindow w = Win(anchor);
    CHECK(w.base == anchor - (uint32_t)kLead * 2u);
    CHECK(w.instructions == kInstr);
    CHECK(w.base < anchor);                        // there IS code above the anchor
}

// The tail needs the same treatment: a window running past the end of the region is rejected
// just as readily as one starting before it.
void TestNearRegionEndTrimsTheLength()
{
    const uint32_t anchor = kHwram + kHwramSize - 0x10u;   // 16 bytes left in the region
    const AsmCodeWindow w = Win(anchor);
    CHECK(w.instructions < kInstr);                        // trimmed
    CHECK(w.instructions > 0);
    CHECK(w.base + (uint32_t)w.instructions * 2u == kHwram + kHwramSize);
    CHECK(w.base == anchor - (uint32_t)kLead * 2u);         // the lead still fits behind it
}

// A region smaller than the window at all (the VDP1 register image is 0x18 bytes) must not
// produce a window reaching outside it in either direction.
void TestTinyRegionIsFullyContained()
{
    const uint32_t base = 0x05D00000u, size = 0x18u;
    const AsmCodeWindow w = Win(base + 8u);
    CHECK(w.base >= base);
    CHECK(w.base + (uint32_t)w.instructions * 2u <= base + size);
    CHECK(w.instructions > 0);
}

// An address in a mirror must stay in that mirror: the region table normalizes to compare, and
// clamping against its canonical base would silently move the view somewhere else.
void TestMirroredAnchorKeepsItsMirror()
{
    const uint32_t mirrored = 0x26000000u;     // HWRAM through a different mirror
    const AsmCodeWindow w = Win(mirrored);
    CHECK(w.base == mirrored);
    CHECK((w.base & 0x07FFFFFFu) == kHwram);
    CHECK(w.instructions == kInstr);
}

// Nothing in BIOS or the cartridge is captured, so there is no better window to pick; it is
// left alone rather than snapped to some unrelated region.
void TestUnmappedAnchorIsLeftAlone()
{
    const uint32_t anchor = 0x00010000u;   // below LWRAM, not a captured region
    const AsmCodeWindow w = Win(anchor);
    CHECK(w.base == anchor - (uint32_t)kLead * 2u);
    CHECK(w.instructions == kInstr);
}

// Odd anchors are instruction-aligned, and an anchor near zero must not wrap underneath it.
void TestAlignmentAndNoWrapAtZero()
{
    CHECK((Win(kHwram + 0x8001u).base & 1u) == 0u);
    const AsmCodeWindow w = Win(0x10u);
    CHECK(w.base == 0u);                 // clamped, not 0x10 - 192 wrapping to ~4 GiB
    CHECK(w.instructions > 0);
}

// The invariant behind all of the above, over every captured region: whatever the anchor, the
// window never leaves the region it is in.
void TestWindowNeverLeavesItsRegion()
{
    size_t count = 0;
    const SaturnRegion* regions = SaturnRegions(count);
    for (size_t i = 0; i < count; ++i)
    {
        const SaturnRegion& r = regions[i];
        const uint32_t probes[] = { 0u, 2u, r.size / 2u, r.size - 2u };
        for (uint32_t off : probes)
        {
            if (off >= r.size) continue;
            const AsmCodeWindow w = Win(r.base + off);
            const uint32_t lo = w.base & 0x07FFFFFFu;
            const uint32_t hi = lo + (uint32_t)w.instructions * 2u;
            CHECK(lo >= r.base);
            CHECK(hi <= r.base + r.size);
            CHECK(w.instructions > 0);
        }
    }
}
}  // namespace

int main()
{
    TestRegionStartKeepsTheWindowInsideTheRegion();
    TestInteriorAnchorGetsTheFullLead();
    TestNearRegionEndTrimsTheLength();
    TestTinyRegionIsFullyContained();
    TestMirroredAnchorKeepsItsMirror();
    TestUnmappedAnchorIsLeftAlone();
    TestAlignmentAndNoWrapAtZero();
    TestWindowNeverLeavesItsRegion();
    if (gFailures) { std::printf("FAILURES: %d\n", gFailures); return 1; }
    std::printf("all cases passed\n");
    return 0;
}
