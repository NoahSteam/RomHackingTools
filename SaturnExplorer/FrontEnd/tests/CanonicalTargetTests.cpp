// CanonicalTarget tests (PLAN.md §5.3, M0 "Done when": CanonicalTarget agrees with the decompiler's A2
// rule). Mirror folding, region/offset <-> bus agreement, rejection of mismatches and of unmapped
// addresses, and the encoding of the JSON form.
#include "AIInvestigation/CanonicalTarget.h"

#include <cstdio>
#include <string>

#include "AIInvestigation/Encoding.h"
#include "SaturnRegions.h"
#include "Sh2Address.h"

#ifdef SE_TEST_WITH_DECOMPILER
#include "Decompiler/MemorySnapshot.h"
#endif

using namespace sfe;
using namespace sfe::ai;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

bool Is(const TargetResult& r, RegionId region, uint32_t offset, uint32_t bus)
{
    return r.ok && r.target.region == region && r.target.offset == offset && r.target.hasBus && r.target.bus == bus;
}

TargetResult Json(const char* text)
{
    JsonValue v;
    JsonError e;
    if (!ParseJson(text, JsonLimits(), &v, &e)) { std::printf("test JSON did not parse: %s\n", text); ++gFail; }
    return ResolveTargetJson(v);
}

// ---- The wire names ---------------------------------------------------------------------------

void TestRegionNames()
{
    const char* expected[] = { "lwram", "hwram", "soundram", "vdp1ram", "vdp1fb", "vdp1regs", "vdp2ram", "vdp2cram", "vdp2regs" };
    Check(kRegionCount == 9, "nine regions");
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        Check(std::string(RegionApiName(static_cast<RegionId>(i))) == expected[i], "the wire name matches PLAN.md §5.3");
        RegionId back = RegionId::Count;
        Check(RegionFromApiName(expected[i], &back) && back == static_cast<RegionId>(i), "and maps back");
    }
    RegionId r;
    Check(!RegionFromApiName("HWRAM", &r) && !RegionFromApiName("Sound RAM", &r) && !RegionFromApiName("", &r) &&
              !RegionFromApiName("sound_ram", &r) && !RegionFromApiName("bios", &r),
          "only the nine lower-case names are regions");
}

// ---- A2 rule: the cases Docs/Decompiler/PLAN.md A2 lists -------------------------------------

void TestMirrorFolding()
{
    // 0x26004000 -> the same bytes as 0x06004000; the HWRAM 1 MiB repeat folds to the first copy.
    for (uint32_t a : { 0x06004000u, 0x26004000u, 0x06104000u, 0x07F04000u, 0x27F04000u, 0x06204000u })
        Check(Is(CanonicalizeBusAddress(a), RegionId::Hwram, 0x4000, 0x06004000u), "HWRAM aliases fold to 0x06004000");

    // Low work RAM is one copy; the cache-through alias folds, a "repeat" does not exist.
    Check(Is(CanonicalizeBusAddress(0x00204000u), RegionId::Lwram, 0x4000, 0x00204000u), "LWRAM");
    Check(Is(CanonicalizeBusAddress(0x20204000u), RegionId::Lwram, 0x4000, 0x00204000u), "LWRAM through the cache-through partition");
    {
        const TargetResult r = CanonicalizeBusAddress(0x00300000u);
        Check(!r.ok && r.error == TargetError::Unmapped, "0x00300000 is not an LWRAM repeat");
    }
    Check(CanonicalizeBusAddress(0x002FFFFFu).ok && !CanonicalizeBusAddress(0x00300000u).ok, "LWRAM ends at 0x002FFFFF");

    // The first and last byte of HWRAM's window, and the byte after it.
    Check(Is(CanonicalizeBusAddress(0x06000000u), RegionId::Hwram, 0, 0x06000000u), "HWRAM first byte");
    Check(Is(CanonicalizeBusAddress(0x060FFFFFu), RegionId::Hwram, 0xFFFFF, 0x060FFFFFu), "HWRAM last byte of the first copy");
    Check(Is(CanonicalizeBusAddress(0x06100000u), RegionId::Hwram, 0, 0x06000000u), "the first byte of the second copy folds to offset 0");
    Check(Is(CanonicalizeBusAddress(0x07FFFFFFu), RegionId::Hwram, 0xFFFFF, 0x060FFFFFu), "the last byte of the window");
    {
        const TargetResult r = CanonicalizeBusAddress(0x08000000u);
        Check(!r.ok && r.error == TargetError::Unmapped, "past the HWRAM window is unmapped, not a further repeat");
        Check(!CanonicalizeBusAddress(0x28000000u).ok, "and through the cache-through partition too");
    }

    // Each captured region's base and last byte, in both bus partitions.
    size_t n = 0;
    const SaturnRegion* regions = SaturnRegions(n);
    Check(n == kRegionCount, "the table and the enum agree");
    for (size_t i = 0; i < n; ++i)
    {
        const RegionTraits& t = Traits(static_cast<RegionId>(i));
        Check(t.busBase == regions[i].base && t.size == regions[i].size, "traits come from the shared region table");
        if (!HasBusAddress(t)) continue;
        for (uint32_t partition : { 0x00000000u, 0x20000000u })
        {
            Check(Is(CanonicalizeBusAddress(partition | t.busBase), t.id, 0, t.busBase), "region base");
            Check(Is(CanonicalizeBusAddress(partition | (t.busBase + t.size - 1)), t.id, t.size - 1, t.busBase + t.size - 1),
                  "region last byte");
        }
    }
    // One byte past a register image is unmapped.
    Check(CanonicalizeBusAddress(0x05D00017u).ok && !CanonicalizeBusAddress(0x05D00018u).ok, "VDP1 registers are 0x18 bytes");
    Check(CanonicalizeBusAddress(0x05F8011Fu).ok && !CanonicalizeBusAddress(0x05F80120u).ok, "VDP2 registers are 0x120 bytes");
    Check(CanonicalizeBusAddress(0x05F00FFFu).ok && !CanonicalizeBusAddress(0x05F01000u).ok, "VDP2 CRAM is 4 KiB and is not assumed to repeat");
    // Sound RAM is a captured region even though Memory Compare leaves it out.
    Check(Is(CanonicalizeBusAddress(0x25A00010u), RegionId::SoundRam, 0x10, 0x05A00010u), "sound RAM through the cache-through partition");
}

void TestPartitionsAndUnmapped()
{
    // Not memory at all: the partition is named.
    for (uint32_t a : { 0x46004000u, 0x66004000u, 0x86004000u, 0xA6004000u, 0xC6004000u, 0xFFFFFE00u })
    {
        const TargetResult r = CanonicalizeBusAddress(a);
        Check(!r.ok && r.error == TargetError::NotMemory, "a non-bus partition is refused");
        Check(r.message.find("not memory") != std::string::npos, "with the reason");
    }
    Check(CanonicalizeBusAddress(0x66004000u).message.find("cache address array") != std::string::npos, "0x66004000 names the cache address array");
    Check(CanonicalizeBusAddress(0xFFFFFE00u).message.find("on-chip") != std::string::npos, "0xFFFFFE00 names on-chip space");

    // On the bus but nothing captured: BIOS, SMPC, backup RAM, A-bus, SCU, the gaps.
    for (uint32_t a : { 0x00000000u, 0x00100000u, 0x00180000u, 0x02000000u, 0x04000000u, 0x05800000u, 0x05B00000u,
                        0x05FE0000u, 0x05FF0000u, 0x05D80000u })
    {
        const TargetResult r = CanonicalizeBusAddress(a);
        Check(!r.ok && r.error == TargetError::Unmapped, "an uncaptured bus address is unmapped");
    }
    Check(CanonicalizeBusAddress(0x05CC0000u).error == TargetError::Unmapped, "the frame buffer's hardware mirror is not captured");
}

void TestFrameBufferHasNoBusAddress()
{
    // The VDP1 frame buffer is an app-derived image: addressable only as region + offset.
    const TargetResult bus = CanonicalizeBusAddress(0x05C80010u);
    Check(!bus.ok && bus.error == TargetError::NoBusAddress, "a bus address in the frame buffer window is refused");
    Check(bus.message.find("vdp1fb") != std::string::npos, "and the message says how to address it");

    const TargetResult img = CanonicalizeRegionOffset(RegionId::Vdp1Fb, 0x10);
    Check(img.ok && !img.target.hasBus && img.target.offset == 0x10, "{vdp1fb, offset} resolves without a bus address");
    Check(!CanonicalizeRegionOffset(RegionId::Vdp1Fb, 0x40000).ok, "the frame buffer is 0x40000 bytes");

    // The last byte of VDP1 RAM resolves; the next byte is the frame buffer's, which has no bus form.
    Check(CanonicalizeBusAddress(0x05C7FFFFu).ok, "last byte of VDP1 RAM");
    Check(CanonicalizeBusAddress(0x05C80000u).error == TargetError::NoBusAddress, "the byte after it belongs to the frame buffer");
}

// ---- region/offset <-> bus agreement ----------------------------------------------------------

void TestAgreement()
{
    TargetInput in;
    in.hasBus = true;
    in.bus = 0x06104000u;       // the second copy of HWRAM...
    in.hasRegion = true;
    in.region = RegionId::Hwram;
    in.hasOffset = true;
    in.offset = 0x4000;         // ...is offset 0x4000 of the first
    Check(Is(ResolveTarget(in), RegionId::Hwram, 0x4000, 0x06004000u), "a mirror and its canonical offset agree");

    in.bus = 0x26004000u;
    Check(Is(ResolveTarget(in), RegionId::Hwram, 0x4000, 0x06004000u), "a cache-through address and its offset agree");

    in.bus = 0x06005000u;
    TargetResult r = ResolveTarget(in);
    Check(!r.ok && r.error == TargetError::Mismatch, "a different offset in the same region is a mismatch");
    Check(r.message.find("0x06005000") != std::string::npos && r.message.find("hwram+0x4000") != std::string::npos,
          "and the message names both places");

    in.bus = 0x00204000u;       // LWRAM +0x4000 against hwram +0x4000
    r = ResolveTarget(in);
    Check(!r.ok && r.error == TargetError::Mismatch, "the same offset in another region is a mismatch");

    in.bus = 0x06004000u;
    in.offset = 0x104000u;      // past HWRAM: not folded, an error even though bus mirrors wrap
    r = ResolveTarget(in);
    Check(!r.ok && r.error == TargetError::OffsetOutOfRange, "an offset is never wrapped like a bus mirror");

    // Either spelling alone.
    TargetInput only;
    only.hasRegion = only.hasOffset = true;
    only.region = RegionId::SoundRam;
    only.offset = 0x7FFFF;
    Check(Is(ResolveTarget(only), RegionId::SoundRam, 0x7FFFF, 0x05A7FFFFu), "region/offset alone resolves to its bus address");
    only.offset = 0x80000;
    Check(ResolveTarget(only).error == TargetError::OffsetOutOfRange, "one past the end");

    // A bus address no region holds is refused even when a region is also given.
    TargetInput both;
    both.hasBus = both.hasRegion = both.hasOffset = true;
    both.bus = 0x00300000u;
    both.region = RegionId::Lwram;
    both.offset = 0x100000;
    Check(ResolveTarget(both).error == TargetError::Unmapped, "the bus half is checked first and refused");

    TargetInput none;
    Check(ResolveTarget(none).error == TargetError::Missing, "no location at all");
    TargetInput half;
    half.hasRegion = true;
    Check(ResolveTarget(half).error == TargetError::Malformed, "a region without an offset");
    TargetInput half2;
    half2.hasBus = true;
    half2.bus = 0x06004000u;
    half2.hasOffset = true;
    Check(ResolveTarget(half2).error == TargetError::Malformed, "an offset without a region");
}

void TestJsonForm()
{
    Check(Is(Json("{\"bus_address\":\"0x06012340\"}"), RegionId::Hwram, 0x12340, 0x06012340u), "bus_address");
    Check(Is(Json("{\"region\":\"hwram\",\"offset\":\"0x12340\"}"), RegionId::Hwram, 0x12340, 0x06012340u), "region and offset");
    Check(Is(Json("{\"bus_address\":\"0x26112340\",\"region\":\"hwram\",\"offset\":\"0x12340\"}"), RegionId::Hwram, 0x12340, 0x06012340u),
          "both spellings, in agreement, with a mirror");
    Check(Json("{\"bus_address\":\"0x06012340\",\"region\":\"lwram\",\"offset\":\"0x12340\"}").error == TargetError::Mismatch,
          "both spellings, disagreeing");

    // The encoding is the plan's: 0x-prefixed upper-case hex.
    Check(Json("{\"bus_address\":\"0x0601234a\"}").error == TargetError::BadEncoding, "lower-case hex digit");
    Check(Json("{\"bus_address\":\"0X06012340\"}").error == TargetError::BadEncoding, "upper-case prefix");
    Check(Json("{\"bus_address\":\"06012340\"}").error == TargetError::BadEncoding, "no prefix");
    Check(Json("{\"bus_address\":\"0x106012340\"}").error == TargetError::BadEncoding, "nine digits");
    Check(Json("{\"region\":\"hwram\",\"offset\":\"0x4000\"}").ok, "a minimal-digit offset is fine");
    Check(Json("{\"bus_address\":100663296}").error == TargetError::Malformed, "a number is not a hex string");
    Check(Json("{\"region\":\"HWRAM\",\"offset\":\"0x0\"}").error == TargetError::UnknownRegion, "region names are lower case");
    Check(Json("{\"region\":\"hwram\"}").error == TargetError::Malformed, "region alone");
    Check(Json("{\"offset\":\"0x4000\"}").error == TargetError::Malformed, "offset alone");
    Check(Json("{}").error == TargetError::Missing, "empty target");
    Check(Json("{\"bus_address\":\"0x06004000\",\"cpu\":0}").error == TargetError::Malformed, "an unknown target field");
    Check(Json("[]").error == TargetError::Malformed, "not an object");
    Check(Json("{\"bus_address\":\"0x66004000\"}").error == TargetError::NotMemory, "a non-memory partition");
    Check(Json("{\"bus_address\":\"0x05C80000\"}").error == TargetError::NoBusAddress, "a frame buffer address");
    Check(Json("{\"region\":\"vdp1fb\",\"offset\":\"0x10\"}").ok, "a frame buffer region/offset");

    // The written form is the canonical spelling and reads back to the same target.
    const TargetResult t = CanonicalizeBusAddress(0x27F04000u);
    Check(WriteJson(TargetToJson(t.target), JsonWriteOptions()) ==
              "{\"region\":\"hwram\",\"offset\":\"0x4000\",\"bus_address\":\"0x06004000\"}", "TargetToJson");
    Check(ResolveTargetJson(TargetToJson(t.target)).target == t.target, "and it round-trips");
    const TargetResult fb = CanonicalizeRegionOffset(RegionId::Vdp1Fb, 0x20);
    Check(WriteJson(TargetToJson(fb.target), JsonWriteOptions()) == "{\"region\":\"vdp1fb\",\"offset\":\"0x20\"}", "an image has no bus_address");
    Check(TargetToString(t.target) == "hwram+0x4000 (0x06004000)", "TargetToString");
}

void TestRangeAndCaps()
{
    const CanonicalTarget last = CanonicalizeBusAddress(0x060FFFFFu).target;
    Check(RangeFits(last, 1) && !RangeFits(last, 2) && !RangeFits(last, 0), "a range must fit inside the region");
    const CanonicalTarget start = CanonicalizeBusAddress(0x06000000u).target;
    Check(RangeFits(start, 0x100000) && !RangeFits(start, 0x100001), "the whole region fits, one more does not");
    Check(!RangeFits(start, 0xFFFFFFFFu), "no overflow");

    Check(HasCaps(start, kCapWatch | kCapBreakWrite | kCapSh2Code), "HWRAM can hold code and take write watchpoints");
    Check(!HasCaps(CanonicalizeBusAddress(0x05E00000u).target, kCapSh2Code), "VDP2 RAM does not hold SH-2 code");
    Check(!HasCaps(CanonicalizeRegionOffset(RegionId::Vdp1Fb, 0).target, kCapBreakWrite), "the frame buffer has no bus writer to watch");
}

// ---- Agreement with the decompiler -------------------------------------------------------------

// CanonicalTarget must agree with the A2 rule on every address, not just the documented cases: a
// strided sweep over all 4 GiB plus every boundary, comparing the bus address it produces with
// CanonicalAddress (which MemorySnapshot uses).
void TestAgreesWithDecompilerRule()
{
    auto agree = [](uint32_t a) {
        uint32_t folded = 0;
        const bool bus = CanonicalAddress(a, folded);
        const TargetResult r = CanonicalizeBusAddress(a);
        if (!bus)
            return !r.ok && r.error == TargetError::NotMemory;
        if (r.ok)
            return r.target.hasBus && r.target.bus == folded;
        // On the bus, not resolved: unmapped, or the frame buffer's window; never "not memory".
        return r.error == TargetError::Unmapped || r.error == TargetError::NoBusAddress;
    };
    bool all = true;
    for (uint64_t a = 0; a <= 0xFFFFFFFFull; a += 0x1003) all = agree(static_cast<uint32_t>(a)) && all;
    for (uint32_t base : { 0x00000000u, 0x00100000u, 0x00200000u, 0x00300000u, 0x05A00000u, 0x05C00000u, 0x05C80000u, 0x05D00000u,
                           0x05E00000u, 0x05F00000u, 0x05F80000u, 0x06000000u, 0x06100000u, 0x07F00000u, 0x08000000u })
        for (uint32_t part : { 0x00000000u, 0x20000000u, 0x40000000u, 0x60000000u, 0x80000000u, 0xA0000000u, 0xC0000000u, 0xE0000000u })
            for (int d = -2; d <= 2; ++d)
                all = agree((part | base) + static_cast<uint32_t>(d)) && all;
    Check(all, "CanonicalTarget and CanonicalAddress agree across the address space");

#ifdef SE_TEST_WITH_DECOMPILER
    // The decompiler's own spelling of the rule is the same function.
    uint32_t viaDecomp = 0, viaShared = 0;
    Check(decomp::CanonicalAddress(0x27F04000u, viaDecomp) && CanonicalAddress(0x27F04000u, viaShared) && viaDecomp == viaShared,
          "decomp::CanonicalAddress is the shared function");
    Check(decomp::ClassifyPartition(0x66004000u) == Sh2Partition::CacheAddressArray, "and so is its classifier");
#endif
}
}  // namespace

int main()
{
    TestRegionNames();
    TestMirrorFolding();
    TestPartitionsAndUnmapped();
    TestFrameBufferHasNoBusAddress();
    TestAgreement();
    TestJsonForm();
    TestRangeAndCaps();
    TestAgreesWithDecompilerRule();
    if (gFail == 0) std::printf("CanonicalTargetTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
