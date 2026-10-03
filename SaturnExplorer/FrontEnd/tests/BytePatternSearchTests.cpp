// Unit tests for FindBytePattern -- the "where else do these bytes appear?" scan behind the
// Memory panel's right-click menu. A mock backend over two regions, so the cases that actually
// bite can be exercised: a match straddling the 64 KiB read chunk, overlapping occurrences, a
// region that fails to read, and the hit cap.
#include "Debug/BytePatternSearch.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace sfe;

namespace
{
int gFailures;

#define CHECK(cond) do {                                                      \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } \
} while (0)

constexpr uint32_t kA = 0x00200000u;   // "LWRAM"
constexpr uint32_t kB = 0x06000000u;   // "HWRAM"

// Two independent regions. 'failB' makes every read of the second region fail, which is how a
// live source that drops a read (or a savestate missing a region) looks from here.
class MockBackend : public IMemoryBackend
{
public:
    MockBackend(std::size_t aSize, std::size_t bSize) : a(aSize, 0), b(bSize, 0) {}

    bool Connected() const override { return true; }

    std::vector<MemoryReadResult> ReadMemoryBatch(
        const std::vector<MemoryReadRequest>& reqs) override
    {
        std::vector<MemoryReadResult> out;
        for (const MemoryReadRequest& q : reqs)
        {
            MemoryReadResult r;
            const std::vector<uint8_t>* src = nullptr;
            uint64_t off = 0;
            if (q.address >= kA && q.address < kA + a.size()) { src = &a; off = q.address - kA; }
            else if (q.address >= kB && q.address < kB + b.size())
            {
                if (!failB) { src = &b; off = q.address - kB; }
            }
            if (src && off + q.size <= src->size())
            {
                r.success = true;
                r.bytes.assign(src->begin() + (std::ptrdiff_t)off,
                               src->begin() + (std::ptrdiff_t)(off + q.size));
            }
            else
            {
                r.error = "oob";
            }
            out.push_back(std::move(r));
        }
        return out;
    }

    std::vector<uint8_t> a, b;
    bool failB = false;
};

void Put(std::vector<uint8_t>& mem, std::size_t off, const std::vector<uint8_t>& bytes)
{
    for (std::size_t i = 0; i < bytes.size(); ++i) mem[off + i] = bytes[i];
}

const std::vector<SearchRegion>& BothRegions()
{
    static const std::vector<SearchRegion> r = { { kA, 0x30000 }, { kB, 0x1000 } };
    return r;
}

void TestFindsMatchesInRegionOrder()
{
    MockBackend be(0x30000, 0x1000);
    const std::vector<uint8_t> pat = { 0xDE, 0xAD, 0xBE, 0xEF };
    Put(be.a, 0x100, pat);
    Put(be.a, 0x2000, pat);
    Put(be.b, 0x40, pat);

    const BytePatternSearchResult res = FindBytePattern(be, BothRegions(), pat, 0);
    CHECK(res.addresses.size() == 3);
    CHECK(res.unread.empty());
    CHECK(!res.truncated);
    if (res.addresses.size() == 3)
    {
        CHECK(res.addresses[0] == kA + 0x100);
        CHECK(res.addresses[1] == kA + 0x2000);
        CHECK(res.addresses[2] == kB + 0x40);   // second region comes after the first
    }
}

// The reason the scan buffers a whole region instead of searching each read in turn: a match
// that spans the boundary between two 64 KiB reads is still a match.
void TestFindsMatchStraddlingAReadChunk()
{
    MockBackend be(0x30000, 0x1000);
    const std::vector<uint8_t> pat = { 0x11, 0x22, 0x33, 0x44 };
    Put(be.a, 0x10000 - 2, pat);   // two bytes either side of the first chunk boundary

    const BytePatternSearchResult res = FindBytePattern(be, BothRegions(), pat, 0);
    CHECK(res.addresses.size() == 1);
    if (!res.addresses.empty()) CHECK(res.addresses[0] == kA + 0x10000 - 2);
}

// AA AA across AA AA AA is two occurrences, not one: advancing past a whole match would hide
// the second, and for "where else do these bytes appear" that is a wrong answer.
void TestOverlappingMatchesBothCount()
{
    MockBackend be(0x30000, 0x1000);
    Put(be.a, 0x80, { 0xAA, 0xAA, 0xAA });
    const std::vector<uint8_t> pat = { 0xAA, 0xAA };

    const BytePatternSearchResult res = FindBytePattern(be, BothRegions(), pat, 0);
    CHECK(res.addresses.size() == 2);
    if (res.addresses.size() == 2)
    {
        CHECK(res.addresses[0] == kA + 0x80);
        CHECK(res.addresses[1] == kA + 0x81);
    }
}

// An unreadable region is reported, not silently treated as "nothing here" -- otherwise a
// scan that looked at nothing is indistinguishable from one that found nothing.
void TestUnreadableRegionIsReported()
{
    MockBackend be(0x30000, 0x1000);
    be.failB = true;
    const std::vector<uint8_t> pat = { 0x5A };
    Put(be.a, 0x10, pat);

    const BytePatternSearchResult res = FindBytePattern(be, BothRegions(), pat, 0);
    CHECK(res.addresses.size() == 1);
    CHECK(res.unread.size() == 1);
    if (res.unread.size() == 1) CHECK(res.unread[0].base == kB);
}

void TestHitCapTruncates()
{
    MockBackend be(0x30000, 0x1000);
    for (std::size_t i = 0; i < 50; ++i) be.a[0x200 + i * 4] = 0x7E;
    const std::vector<uint8_t> pat = { 0x7E };

    const BytePatternSearchResult res = FindBytePattern(be, BothRegions(), pat, 10);
    CHECK(res.addresses.size() == 10);
    CHECK(res.truncated);

    const BytePatternSearchResult all = FindBytePattern(be, BothRegions(), pat, 0);
    CHECK(all.addresses.size() == 50);
    CHECK(!all.truncated);
}

// An empty pattern matches nothing. Matching at every address would be the literal reading and
// is useless; worse, it would flood the caller's result list.
void TestEmptyPatternFindsNothing()
{
    MockBackend be(0x30000, 0x1000);
    const BytePatternSearchResult res = FindBytePattern(be, BothRegions(), {}, 0);
    CHECK(res.addresses.empty());
    CHECK(res.unread.empty());
}

// A pattern longer than the region is simply absent -- it must not be reported as an unread
// region, because the read was never the problem.
void TestPatternLongerThanRegion()
{
    MockBackend be(0x30000, 0x1000);
    const std::vector<uint8_t> pat(0x2000, 0xCC);   // bigger than region B
    const BytePatternSearchResult res = FindBytePattern(be, { { kB, 0x1000 } }, pat, 0);
    CHECK(res.addresses.empty());
    CHECK(res.unread.empty());
}
}  // namespace

int main()
{
    TestFindsMatchesInRegionOrder();
    TestFindsMatchStraddlingAReadChunk();
    TestOverlappingMatchesBothCount();
    TestUnreadableRegionIsReported();
    TestHitCapTruncates();
    TestEmptyPatternFindsNothing();
    TestPatternLongerThanRegion();
    if (gFailures) { std::printf("FAILURES: %d\n", gFailures); return 1; }
    std::printf("all cases passed\n");
    return 0;
}
