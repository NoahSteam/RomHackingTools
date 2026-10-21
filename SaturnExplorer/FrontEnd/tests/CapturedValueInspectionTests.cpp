#include "Debug/CapturedValueInspection.h"
#include "Decompiler/MemorySnapshot.h"
#include <cstdio>
#include <limits>

using namespace sfe::decomp;
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
MemorySnapshot Snapshot()
{
    MemorySnapshot s;
    MemoryRegion hw;
    hw.name = "HWRAM"; hw.base = MemorySnapshot::kHwramBase;
    hw.bytes.resize(MemorySnapshot::kWorkRamSize);
    for (size_t i = 0; i < hw.bytes.size(); ++i) hw.bytes[i] = uint8_t(i);
    s.regions.push_back(std::move(hw));
    MemoryRegion lw;
    lw.name = "LWRAM"; lw.base = MemorySnapshot::kLwramBase; lw.bytes = {0x80, 0x01, 0xFE, 0xDC};
    s.regions.push_back(std::move(lw));
    MemoryRegion bios;
    bios.name = "BIOS"; bios.base = 0; bios.bytes = {0x12, 0x34, 0x56, 0x78};
    s.regions.push_back(std::move(bios));
    return s;
}
InspectionTarget Object(uint32_t address, uint32_t width, InspectionTarget::Display display)
{
    InspectionTarget t;
    t.address = address; t.byteSize = width; t.display = display;
    return t;
}
void TestDecode()
{
    auto s = Snapshot();
    using D = InspectionTarget::Display;
    CHECK(InspectCapturedValue(s, Object(0x06000011, 1, D::UnsignedInteger)).bits == 0x11);
    CHECK(InspectCapturedValue(s, Object(0x06000011, 2, D::UnsignedInteger)).bits == 0x1112);
    CHECK(InspectCapturedValue(s, Object(0x06000011, 4, D::UnsignedInteger)).bits == 0x11121314);
    struct SignedCase { uint32_t address, width; const char* value; };
    for (const auto& c : {SignedCase{0x06000080, 1, "Signed: -128"},
                          SignedCase{0x00200000, 2, "Signed: -32767"},
                          SignedCase{0x00200000, 4, "Signed: -2147352868"},
                          SignedCase{0x0600007F, 1, "Signed: 127"}})
    {
        auto r = InspectCapturedValue(s, Object(c.address, c.width, D::SignedInteger));
        CHECK(r.available); CHECK(r.scalarText.find(c.value) != std::string::npos);
    }
    s.regions[0].bytes[0x100] = 0x80;
    s.regions[0].bytes[0x101] = s.regions[0].bytes[0x102] = s.regions[0].bytes[0x103] = 0;
    CHECK(InspectCapturedValue(s, Object(0x06000100, 2, D::SignedInteger)).scalarText.find("Signed: -32768") != std::string::npos);
    CHECK(InspectCapturedValue(s, Object(0x06000100, 4, D::SignedInteger)).scalarText.find("Signed: -2147483648") != std::string::npos);
    s.regions[0].bytes[0x100] = 0x7F;
    s.regions[0].bytes[0x101] = s.regions[0].bytes[0x102] = s.regions[0].bytes[0x103] = 0xFF;
    CHECK(InspectCapturedValue(s, Object(0x06000100, 4, D::SignedInteger)).scalarText.find("Signed: 2147483647") != std::string::npos);
    CHECK(InspectCapturedValue(s, Object(0x06000000, 1, D::Bool)).scalarText.find("false") != std::string::npos);
    CHECK(InspectCapturedValue(s, Object(0x06000001, 1, D::Bool)).scalarText.find("true") != std::string::npos);
    auto raw = InspectCapturedValue(s, Object(0x06000000, 4, D::RawBytes));
    CHECK(raw.available && raw.bytes.size() == 4);
    CHECK(raw.scalarText == "raw captured bytes");
    CHECK(raw.scalarText.find("Unsigned") == std::string::npos);
    raw = InspectCapturedValue(s, Object(0x06000000, 1000, D::RawBytes));
    CHECK(raw.available && raw.bytes.size() == kCapturedPreviewLimit && raw.preview.truncated);
    CHECK(!InspectCapturedValue(s, Object(0x06000000, 0, D::UnsignedInteger)).available);
    // Unsupported widths/types remain raw; neither float/aggregate nor a malformed pointer
    // descriptor gains guessed integer or pointer semantics.
    raw = InspectCapturedValue(s, Object(0x06000000, 3, D::UnsignedInteger));
    CHECK(raw.available && raw.scalarText == "raw captured bytes" && !raw.hasPointer);
    raw = InspectCapturedValue(s, Object(0x06000000, 2, D::Pointer));
    CHECK(raw.available && !raw.hasPointer);
}
void TestPolicyAndBounds()
{
    auto s = Snapshot();
    using D = InspectionTarget::Display;
    for (uint32_t a : {0x06000020u, 0x26000020u, 0x06100020u, 0x27F00020u})
    {
        auto r = InspectCapturedValue(s, Object(a, 4, D::UnsignedInteger));
        CHECK(r.available && r.bits == 0x20212223);
        uint32_t c = 0;
        CHECK(CanonicalCapturedWram(s, a, 4, c) && c == 0x06000020);
    }
    for (uint32_t a : {0x46000020u, 0x66000020u, 0x86000020u, 0xA6000020u,
                       0xC6000020u, 0xFFFFFE00u, 0x05F80000u, 0x05D00000u,
                       0x05B00000u, 0x00300000u, 0x08000000u})
    {
        auto r = InspectCapturedValue(s, Object(a, 4, D::UnsignedInteger));
        CHECK(!r.available && !r.reason.empty() && r.bytes.empty());
        CHECK(PreviewCapturedMemory(s, a).bytes.empty());
        uint32_t c = 0;
        CHECK(!CanonicalCapturedWram(s, a, 4, c));
    }
    // Even an injected hardware image cannot bypass the inspection region policy.
    MemoryRegion device; device.name = "VDP2"; device.base = 0x05F80000; device.bytes.resize(64, 0xAA);
    s.regions.push_back(std::move(device));
    CHECK(!InspectCapturedValue(s, Object(0x05F80000, 4, D::UnsignedInteger)).available);
    auto r = InspectCapturedValue(s, Object(0, 4, D::UnsignedInteger));
    CHECK(r.available && r.loadedBios && r.bits == 0x12345678);
    uint32_t c = 0;
    CHECK(!CanonicalCapturedWram(s, 0, 4, c));
    r = InspectCapturedValue(s, Object(0x20000000, 4, D::UnsignedInteger));
    CHECK(r.available && r.loadedBios && r.bits == 0x12345678);
    CHECK(!InspectCapturedValue(s, Object(2, 4, D::UnsignedInteger)).available);
    auto biosPrefix = PreviewCapturedMemory(s, 2, false);
    CHECK(biosPrefix.loadedBios && biosPrefix.bytes.size() == 2 && biosPrefix.truncated);
    // A captured region crossing out of BIOS still cannot expand the permitted area.
    MemoryRegion pastBios; pastBios.name = "oversized BIOS"; pastBios.base = 0x0007FFFF; pastBios.bytes = {0xAA, 0xBB};
    s.regions.push_back(std::move(pastBios));
    CHECK(InspectCapturedValue(s, Object(0x0007FFFF, 1, D::UnsignedInteger)).available);
    CHECK(!InspectCapturedValue(s, Object(0x0007FFFF, 2, D::UnsignedInteger)).available);
    CHECK(!CanonicalCapturedWram(s, 0x06000000, 0, c));
    CHECK(!CanonicalCapturedWram(s, 0x06000000, 16, c));
    // Absent bytes must not become zero-filled scalar success.
    r = InspectCapturedValue(s, Object(0x00200002, 4, D::UnsignedInteger));
    CHECK(!r.available && r.bytes.empty() && !r.reason.empty());
    CHECK(!CanonicalCapturedWram(s, 0x00200002, 4, c));
    auto prefix = PreviewCapturedMemory(s, 0x00200002);
    CHECK(prefix.bytes.size() == 2 && prefix.truncated && !prefix.reason.empty());
    r = InspectCapturedValue(s, Object(0x002FFFFE, 4, D::UnsignedInteger));
    CHECK(!r.available);
    s.regions.erase(s.regions.begin());
    CHECK(!InspectCapturedValue(s, Object(0x06000020, 4, D::UnsignedInteger)).available);
    // The helper's input has no engine volatility or transport policy: ordinary captured
    // WRAM bytes remain readable independent of volatile declarations in a request.
}
void TestRepeatedBoundaries()
{
    auto s = Snapshot();
    using D = InspectionTarget::Display;
    auto r = InspectCapturedValue(s, Object(0x060FFFFE, 4, D::UnsignedInteger));
    CHECK(r.available && r.bits == 0xFEFF0001);
    uint32_t c = 0;
    CHECK(!CanonicalCapturedWram(s, 0x060FFFFE, 4, c));
    CHECK(CanonicalCapturedWram(s, 0x060FFFFE, 2, c) && c == 0x060FFFFE);
    // A range leaving the final HWRAM repeat does not wrap into invented capture bytes.
    r = InspectCapturedValue(s, Object(0x27FFFFFE, 4, D::UnsignedInteger));
    CHECK(!r.available);
    auto prefix = PreviewCapturedMemory(s, 0x27FFFFFE);
    CHECK(prefix.bytes.size() == 2 && prefix.truncated);
    r = InspectCapturedValue(s, Object(std::numeric_limits<uint32_t>::max(), 4, D::UnsignedInteger));
    CHECK(!r.available && !r.reason.empty());
    CHECK(!CanonicalCapturedWram(s, std::numeric_limits<uint32_t>::max(), 4, c));
}
void TestPointerAndAscii()
{
    auto s = Snapshot();
    using D = InspectionTarget::Display;
    auto& bytes = s.regions[0].bytes;
    bytes[0] = 0x06; bytes[1] = 0x10; bytes[2] = 0; bytes[3] = 0x20;
    bytes[0x20] = 'A'; bytes[0x21] = '\\'; bytes[0x22] = '"'; bytes[0x23] = 0;
    auto r = InspectCapturedValue(s, Object(0x06000000, 4, D::Pointer));
    CHECK(r.available && r.hasPointer && r.pointer == 0x06100020);
    CHECK(r.bytes.size() == 4 && r.preview.bytes.size() == kCapturedPreviewLimit);
    CHECK(r.preview.ascii.substr(0, 6) == "A\\\\\\\".");
    // Changing target bytes never changes the pointer storage observation.
    bytes[0x20] = 'Z';
    auto changed = InspectCapturedValue(s, Object(0x06000000, 4, D::Pointer));
    CHECK(changed.pointer == r.pointer && changed.preview.ascii[0] == 'Z');
    InspectionTarget address;
    address.kind = InspectionTarget::Kind::ConstantAddress; address.address = 0x06000000;
    address.byteSize = 4;
    r = InspectCapturedValue(s, address);
    CHECK(r.available && r.hasPointer && r.pointer == address.address);
    CHECK(r.preview.bytes.size() == kCapturedPreviewLimit && r.preview.bytes[0] == 0x06);
    // Address form previews the object directly, never the pointer stored in that object.
    CHECK(r.preview.bytes[0] != bytes[0x20]);
    address.address = 0;
    r = InspectCapturedValue(s, address);
    CHECK(r.available && r.hasPointer && r.pointer == 0 && r.preview.bytes.empty());
    CHECK(r.preview.reason == "null pointer");
    CHECK(PreviewCapturedMemory(s, 0, false).loadedBios);
    CHECK(PreviewCapturedMemory(s, 0, false).bytes.size() == 4);
    bytes[0] = bytes[1] = bytes[2] = bytes[3] = 0;
    r = InspectCapturedValue(s, Object(0x06000000, 4, D::Pointer));
    CHECK(r.available && r.hasPointer && r.pointer == 0 && r.preview.bytes.empty());
    bytes[0] = 0x05; bytes[1] = 0xF8; bytes[2] = bytes[3] = 0;
    r = InspectCapturedValue(s, Object(0x06000000, 4, D::Pointer));
    CHECK(r.available && r.pointer == 0x05F80000 && r.preview.bytes.empty() && !r.preview.reason.empty());
    // A pointer target is never recursively interpreted as another pointer.
    bytes[0] = 0x06; bytes[1] = 0; bytes[2] = 0; bytes[3] = 0x40;
    bytes[0x40] = 0x06; bytes[0x41] = 0; bytes[0x42] = 0; bytes[0x43] = 0x80;
    r = InspectCapturedValue(s, Object(0x06000000, 4, D::Pointer));
    CHECK(r.pointer == 0x06000040 && r.preview.bytes[0] == 0x06);
}
}
int main()
{
    TestDecode(); TestPolicyAndBounds(); TestRepeatedBoundaries(); TestPointerAndAscii();
    if (failures) { std::fprintf(stderr, "%d failures\n", failures); return 1; }
    std::puts("CapturedValueInspectionTests: all passed");
    return 0;
}
