// AsmSpanFor — the span of memory the Assembly panel scrolls through.
//
// The panel is a virtual list over the whole captured region the view is framed in. The span
// must be exactly that region (so every row is reachable), must never leave it (a read that does
// is rejected whole and every row becomes "????"), and must keep the anchor's mirror.
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

constexpr uint32_t kHwram = 0x06000000u;
constexpr uint32_t kHwramSize = 0x100000u;

// Wherever in HWRAM the view is framed, the span is all of HWRAM -- first and last instruction
// included, so the user can scroll to either end.
void TestSpanIsTheWholeRegion()
{
    const uint32_t anchors[] = { kHwram, kHwram + 0x8000u, kHwram + kHwramSize - 2u };
    for (uint32_t a : anchors)
    {
        const AsmSpan s = AsmSpanFor(a);
        CHECK(s.base == kHwram);
        CHECK(s.size == kHwramSize);
    }
}

// A region smaller than a screenful (the VDP1 register image is 0x18 bytes) is its own span.
void TestTinyRegion()
{
    const AsmSpan s = AsmSpanFor(0x05D00008u);
    CHECK(s.base == 0x05D00000u);
    CHECK(s.size == 0x18u);
}

// An address in a mirror must stay in that mirror: the region table normalizes to compare, and
// returning the canonical base would silently show different addresses than were navigated to.
void TestMirroredAnchorKeepsItsMirror()
{
    const uint32_t mirrored = 0x26004000u;     // HWRAM through the uncached mirror
    const AsmSpan s = AsmSpanFor(mirrored);
    CHECK(s.base == 0x26000000u);
    CHECK(s.size == kHwramSize);
    CHECK(mirrored >= s.base && mirrored < s.base + s.size);
}

// Nothing in BIOS or the cartridge is captured: a bounded block around the anchor, not a
// span reaching into some unrelated region or wrapping.
void TestUnmappedAnchorGetsABoundedBlock()
{
    const uint32_t anchor = 0x00010004u;   // below LWRAM, not a captured region
    const AsmSpan s = AsmSpanFor(anchor);
    CHECK(anchor >= s.base && anchor < s.base + s.size);
    CHECK(s.size == kAsmUnmappedSpan);
    CHECK(s.base + s.size > s.base);
}

// Odd anchors are instruction-aligned, and an anchor at zero must not wrap.
void TestAlignmentAndZero()
{
    CHECK(AsmSpanFor(kHwram + 0x8001u).base == kHwram);
    const AsmSpan z = AsmSpanFor(0u);
    CHECK(z.base == 0u && z.size == kAsmUnmappedSpan);
}

// The invariant over every captured region: the span is exactly the region, whatever the
// anchor, and has an even size.
void TestSpanMatchesEveryRegion()
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
            const AsmSpan s = AsmSpanFor(r.base + off);
            CHECK(s.base == r.base);
            CHECK(s.size == (r.size & ~1u));
            CHECK((s.size & 1u) == 0u);
        }
    }
}
}  // namespace

int main()
{
    TestSpanIsTheWholeRegion();
    TestTinyRegion();
    TestMirroredAnchorKeepsItsMirror();
    TestUnmappedAnchorGetsABoundedBlock();
    TestAlignmentAndZero();
    TestSpanMatchesEveryRegion();
    if (gFailures) { std::printf("FAILURES: %d\n", gFailures); return 1; }
    std::printf("all cases passed\n");
    return 0;
}
