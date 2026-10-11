// MemorySnapshot: the A2 address rules (PLAN.md) and the A1 key, over byte vectors.
//
// The SH-2 partition is classified before anything folds: only the cached and cache-through
// partitions reach the bus, and on the bus only HWRAM's documented 1 MiB repeat is folded. The
// cache arrays, purge space and on-chip space are rejected rather than read as RAM, and LWRAM is
// not assumed to repeat.
#include "DecompilerTestSupport.h"

#include "Debug/MemoryBackend.h"

#include <cstring>

using namespace sfe::decomp;

namespace {

// HWRAM filled with a pattern that names each byte's offset, so a read shows which bytes it got.
std::shared_ptr<MemorySnapshot> PatternSnapshot()
{
    std::vector<uint8_t> hw(MemorySnapshot::kWorkRamSize);
    for (size_t i = 0; i < hw.size(); ++i) hw[i] = uint8_t(i * 7 + (i >> 8));
    auto s = dts::MakeSnapshot(hw);
    auto* lw = const_cast<MemoryRegion*>(s->RegionAt(MemorySnapshot::kLwramBase));
    for (size_t i = 0; i < lw->bytes.size(); ++i) lw->bytes[i] = uint8_t(0xA5 ^ i);
    return s;
}

bool Same(const MemorySnapshot& s, uint32_t a, uint32_t b, size_t n)
{
    std::vector<uint8_t> x(n), y(n);
    return s.Read(a, x.data(), n) && s.Read(b, y.data(), n) && x == y;
}

void TestPartitions()
{
    CHECK(ClassifyPartition(0x06004000u) == Sh2Partition::Cached);
    CHECK(ClassifyPartition(0x26004000u) == Sh2Partition::CacheThrough);
    CHECK(ClassifyPartition(0x46004000u) == Sh2Partition::AssociativePurge);
    CHECK(ClassifyPartition(0x66004000u) == Sh2Partition::CacheAddressArray);
    CHECK(ClassifyPartition(0x86004000u) == Sh2Partition::Reserved);
    CHECK(ClassifyPartition(0xA6004000u) == Sh2Partition::Reserved);
    CHECK(ClassifyPartition(0xC6004000u) == Sh2Partition::CacheDataArray);
    CHECK(ClassifyPartition(0xFFFFFE00u) == Sh2Partition::OnChip);
}

void TestFolding()
{
    auto s = PatternSnapshot();
    uint32_t c = 0;
    CHECK(CanonicalAddress(0x26004000u, c) && c == 0x06004000u);
    CHECK(CanonicalAddress(0x06104000u, c) && c == 0x06004000u);
    CHECK(CanonicalAddress(0x07F04000u, c) && c == 0x06004000u);
    CHECK(CanonicalAddress(0x27F04000u, c) && c == 0x06004000u);
    CHECK(CanonicalAddress(0x00204000u, c) && c == 0x00204000u);
    CHECK(Same(*s, 0x26004000u, 0x06004000u, 64));
    CHECK(Same(*s, 0x06104000u, 0x06004000u, 64));
    CHECK(Same(*s, 0x07F04000u, 0x06004000u, 64));

    // Not memory: rejected with the partition named, never read as a RAM mirror.
    struct { uint32_t a; const char* name; } rejected[] = {
        { 0x66004000u, "cache address array" }, { 0x46004000u, "associative purge" },
        { 0xC6004000u, "cache data array" }, { 0xFFFFFE00u, "on-chip" }, { 0x86004000u, "reserved" },
    };
    for (const auto& r : rejected)
    {
        std::string why;
        uint8_t b[4];
        CHECK(!CanonicalAddress(r.a, c, &why));
        CHECK(!s->Read(r.a, b, 4, &why));
        CHECK(why.find(r.name) != std::string::npos);
        std::printf("  %08X -> %s\n", r.a, why.c_str());
    }

    // LWRAM is not assumed to repeat: the next megabyte is not captured.
    std::string why;
    uint8_t b[4];
    CHECK(!s->Read(0x00300000u, b, 4, &why));
    std::printf("  00300000 -> %s\n", why.c_str());
    CHECK(why.find("0x00300000") != std::string::npos);

    // A read straddling the end of one HWRAM repeat continues at the start of the next, which
    // on the bus is the start of the same 1 MiB.
    uint8_t wrap[4], expect[4];
    CHECK(s->Read(0x060FFFFEu, wrap, 4));
    CHECK(s->Read(0x060FFFFEu, expect, 2) && s->Read(0x06000000u, expect + 2, 2));
    CHECK(std::memcmp(wrap, expect, 4) == 0);

    // Straddling the end of LWRAM into unmapped space fails as a whole: no partial bytes.
    std::memset(b, 0xEE, sizeof b);
    CHECK(!s->Read(0x002FFFFEu, b, 4, &why));
    CHECK(why.find("0x00300000") != std::string::npos);

    // The BIOS area is named when it is not captured.
    CHECK(!s->Read(0x00000600u, b, 2, &why));
    CHECK(why.find("BIOS") != std::string::npos);
    uint16_t w = 0;
    uint32_t l = 0;
    CHECK(s->ReadU16(0x06000002u, w) && w == ((uint16_t(s->RegionAt(0x06000000u)->bytes[2]) << 8) | s->RegionAt(0x06000000u)->bytes[3]));
    CHECK(s->ReadU32(0x26000000u, l));
}

// A backend over two byte vectors, counting reads; 'failHwram' makes HWRAM unreadable.
class FakeBackend : public sfe::IMemoryBackend
{
public:
    std::vector<uint8_t> lw = std::vector<uint8_t>(MemorySnapshot::kWorkRamSize, 0x11);
    std::vector<uint8_t> hw = std::vector<uint8_t>(MemorySnapshot::kWorkRamSize, 0x22);
    bool failHwram = false;
    bool Connected() const override { return true; }
    std::vector<sfe::MemoryReadResult> ReadMemoryBatch(const std::vector<sfe::MemoryReadRequest>& reqs) override
    {
        std::vector<sfe::MemoryReadResult> out;
        for (const auto& q : reqs)
        {
            sfe::MemoryReadResult r;
            const std::vector<uint8_t>* src = nullptr;
            uint32_t base = 0;
            if (q.address >= 0x06000000u && !failHwram) { src = &hw; base = 0x06000000u; }
            else if (q.address >= 0x00200000u && q.address < 0x00300000u) { src = &lw; base = 0x00200000u; }
            if (src && q.address - base + q.size <= src->size())
            {
                r.success = true;
                r.bytes.assign(src->begin() + (q.address - base), src->begin() + (q.address - base + q.size));
            }
            out.push_back(r);
        }
        return out;
    }
};

void TestCaptureAndKey()
{
    FakeBackend backend;
    se_sh2_regs regs[2] = {};
    regs[0].pc = 0x06004000u;
    const bool have[2] = { true, false };
    auto t0 = std::chrono::steady_clock::now();
    auto a = MemorySnapshot::Capture(backend, regs, have, 7, 100, 55);
    std::printf("capture of LWRAM + HWRAM: %.2f ms\n", dts::MsSince(t0));
    CHECK(a->regions.size() == 2);
    CHECK(a->haveRegs[0] && !a->haveRegs[1]);
    CHECK(a->regs[0].pc == 0x06004000u);
    CHECK(a->key.sourceId == 7 && a->key.deriveSerial == 100 && a->key.frameNumber == 55);
    uint8_t b = 0;
    CHECK(a->Read(0x06000010u, &b, 1) && b == 0x22);
    CHECK(a->Read(0x00200010u, &b, 1) && b == 0x11);

    // Every component of the key moves it.
    auto same = MemorySnapshot::Capture(backend, regs, have, 7, 100, 55);
    CHECK(same->key == a->key);
    CHECK(MemorySnapshot::Capture(backend, regs, have, 8, 100, 55)->key != a->key);
    CHECK(MemorySnapshot::Capture(backend, regs, have, 7, 101, 55)->key != a->key);   // an in-place edit
    CHECK(MemorySnapshot::Capture(backend, regs, have, 7, 100, 56)->key != a->key);

    // A region the backend cannot provide in full is left out, never half-read, and the region
    // hash says so.
    backend.failHwram = true;
    auto partial = MemorySnapshot::Capture(backend, regs, have, 7, 100, 55);
    CHECK(partial->regions.size() == 1);
    CHECK(!partial->Read(0x06000010u, &b, 1));
    CHECK(partial->key != a->key);
    backend.failHwram = false;

    // The BIOS image's contents are in the key: a same-size replacement is a new snapshot.
    auto biosA = BiosImage::Make(std::vector<uint8_t>(0x80000, 0x33));
    auto biosA2 = BiosImage::Make(std::vector<uint8_t>(0x80000, 0x33));
    std::vector<uint8_t> other(0x80000, 0x33);
    other[0x7FFFF] = 0x34;
    auto biosB = BiosImage::Make(other);
    auto withA = MemorySnapshot::Capture(backend, regs, have, 7, 100, 55, biosA);
    CHECK(withA->key != a->key);
    CHECK(MemorySnapshot::Capture(backend, regs, have, 7, 100, 55, biosA2)->key == withA->key);
    CHECK(MemorySnapshot::Capture(backend, regs, have, 7, 100, 55, biosB)->key != withA->key);
    CHECK(withA->Read(0x00000600u, &b, 1) && b == 0x33);
    CHECK(withA->Read(0x20000600u, &b, 1) && b == 0x33);
}

}  // namespace

int main()
{
    dts::PrepareProcess();
    const auto start = std::chrono::steady_clock::now();
    RUN(TestPartitions());
    RUN(TestFolding());
    RUN(TestCaptureAndKey());
    return dts::Finish("MemorySnapshotTests", start);
}
