// Unit tests for MemorySearch (Cheat-Engine-style value scanner). Uses a mock backend
// over a single mutable byte region so scans can be run, memory changed, and narrowed.
#include "Debug/MemorySearch.h"

#include <cstdio>
#include <cstdint>
#include <vector>

using namespace sfe;

namespace
{
constexpr uint32_t kBase = 0x00200000u;

// A backend over one contiguous big-endian region starting at kBase.
class MockBackend : public IMemoryBackend
{
public:
    explicit MockBackend(std::size_t bytes) : mMem(bytes, 0) {}

    bool Connected() const override { return true; }

    std::vector<MemoryReadResult> ReadMemoryBatch(
        const std::vector<MemoryReadRequest>& reqs) override
    {
        std::vector<MemoryReadResult> out;
        for (const MemoryReadRequest& q : reqs)
        {
            MemoryReadResult r;
            const uint64_t off = uint64_t(q.address) - kBase;
            if (q.address >= kBase && off + q.size <= mMem.size())
            {
                r.success = true;
                r.bytes.assign(mMem.begin() + off, mMem.begin() + off + q.size);
            }
            else
            {
                r.error = "oob";
            }
            out.push_back(std::move(r));
        }
        return out;
    }

    // Write a big-endian value of 'width' bytes at CPU address 'addr'.
    void PutBE(uint32_t addr, int width, uint32_t value)
    {
        const uint32_t off = addr - kBase;
        for (int i = 0; i < width; ++i)
            mMem[off + i] = uint8_t(value >> (8 * (width - 1 - i)));
    }

    std::vector<uint8_t> mMem;
};

int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

bool HasAddr(const MemorySearch& s, uint32_t addr)
{
    for (const SearchHit& h : s.Hits()) if (h.addr == addr) return true;
    return false;
}
}  // namespace

int main()
{
    // Region big enough for a handful of aligned values.
    MockBackend be(64);
    const std::vector<SearchRegion> regions{{kBase, 64}};

    // --- Big-endian decode sanity ---
    {
        const uint8_t p[4] = {0x12, 0x34, 0x56, 0x78};
        Check(MemorySearch::DecodeBigEndian(p, WatchType::U16) == 0x1234, "decode u16 BE");
        Check(MemorySearch::DecodeBigEndian(p, WatchType::U32) == 0x12345678, "decode u32 BE");
        const uint8_t n[1] = {0xFF};
        Check(MemorySearch::DecodeBigEndian(n, WatchType::S8) == -1, "decode s8 negative");
        Check(MemorySearch::DecodeBigEndian(n, WatchType::U8) == 255, "decode u8");
    }

    // --- First scan: u16 == 100 finds exactly the seeded addresses ---
    be.PutBE(kBase + 0, 2, 100);
    be.PutBE(kBase + 2, 2, 100);
    be.PutBE(kBase + 4, 2, 42);
    be.PutBE(kBase + 6, 2, 100);
    {
        MemorySearch s;
        std::size_t n = s.First(be, regions, WatchType::U16, SearchCompare::Equal, 100);
        Check(n == 3, "first scan u16==100 count");
        Check(HasAddr(s, kBase + 0) && HasAddr(s, kBase + 2) && HasAddr(s, kBase + 6),
              "first scan hit addresses");
        Check(!HasAddr(s, kBase + 4), "first scan excludes non-match");

        // Change one of the three to a different value, then narrow by Changed.
        be.PutBE(kBase + 2, 2, 99);
        std::size_t n2 = s.Next(be, SearchCompare::Changed, 0);
        Check(n2 == 1 && HasAddr(s, kBase + 2), "next Changed keeps only the changed addr");

        // Narrow again by Decreased (99 < 100): still the same addr.
        be.PutBE(kBase + 2, 2, 50);
        std::size_t n3 = s.Next(be, SearchCompare::Decreased, 0);
        Check(n3 == 1 && HasAddr(s, kBase + 2), "next Decreased");

        // And Equal to the new absolute value.
        std::size_t n4 = s.Next(be, SearchCompare::Equal, 50);
        Check(n4 == 1 && HasAddr(s, kBase + 2), "next Equal absolute");

        s.Reset();
        Check(!s.Active() && s.Count() == 0, "reset clears");
    }

    // --- Unknown-initial-value workflow: baseline everything, then narrow by Unchanged ---
    {
        for (uint32_t off = 0; off < 64; off += 4) be.PutBE(kBase + off, 4, off);
        MemorySearch s;
        std::size_t n = s.First(be, regions, WatchType::U32, SearchCompare::Unknown, 0);
        Check(n == 16, "unknown first scan baselines all aligned u32");

        // Change exactly one dword; Unchanged should drop just that one.
        be.PutBE(kBase + 20, 4, 0xDEADBEEF);
        std::size_t n2 = s.Next(be, SearchCompare::Unchanged, 0);
        Check(n2 == 15 && !HasAddr(s, kBase + 20), "unchanged drops the mutated dword");

        // Increased narrows to values that went up since last scan (none did here).
        std::size_t n3 = s.Next(be, SearchCompare::Increased, 0);
        Check(n3 == 0, "increased narrows to none when nothing rose");
    }

    // --- Signed compare: S8 Less than 0 finds negative bytes ---
    {
        MockBackend s8be(8);
        const std::vector<SearchRegion> r8{{kBase, 8}};
        s8be.PutBE(kBase + 0, 1, 0x10);   // +16
        s8be.PutBE(kBase + 1, 1, 0xFF);   // -1
        s8be.PutBE(kBase + 2, 1, 0x80);   // -128
        s8be.PutBE(kBase + 3, 1, 0x7F);   // +127
        MemorySearch s;
        std::size_t n = s.First(s8be, r8, WatchType::S8, SearchCompare::Less, 0);
        Check(n == 2 && HasAddr(s, kBase + 1) && HasAddr(s, kBase + 2),
              "signed S8 < 0 finds negatives only");
    }

    // --- MEM-02: a region that cannot be read is not a region that matched ---
    //
    // The mock refuses reads while 'offline' is set, which is how a live emulator dropping a
    // read or a savestate missing a region looks from here.
    {
        // A first scan over two regions where only one reads: the count must not be presented as
        // if the whole range had been searched.
        class HalfBackend : public IMemoryBackend
        {
        public:
            bool Connected() const override { return true; }
            std::vector<MemoryReadResult> ReadMemoryBatch(
                const std::vector<MemoryReadRequest>& reqs) override
            {
                std::vector<MemoryReadResult> out;
                for (const MemoryReadRequest& q : reqs)
                {
                    MemoryReadResult r;
                    if (q.address >= kBase && q.address + q.size <= kBase + 16)
                    {
                        r.success = true;
                        r.bytes.assign(q.size, 0);   // every dword reads as 0
                    }
                    else
                    {
                        r.error = "not captured";
                    }
                    out.push_back(std::move(r));
                }
                return out;
            }
        };
        HalfBackend be;
        const std::vector<SearchRegion> regions{{kBase, 16}, {0x06000000u, 16}};
        MemorySearch s;
        const std::size_t n = s.First(be, regions, WatchType::U32, SearchCompare::Equal, 0);
        Check(n == 4, "first scan finds the four dwords in the readable region");
        Check(s.LastScanPartial(), "first scan reports itself partial");
        Check(s.UnreadRegions().size() == 1 && s.UnreadRegions()[0].base == 0x06000000u,
              "the unreadable region is named");
        Check(s.UnverifiedCount() == 0, "no hits came from the unread region, so none are stale");
    }
    {
        // A next scan over a region that has stopped reading. The hits must survive -- dropping
        // them would discard a narrowing the user built up -- but they must not count as tested.
        // "Unchanged" is the compare that makes this visible: every untested hit would otherwise
        // be reported as having been confirmed unchanged.
        class FlakyBackend : public IMemoryBackend
        {
        public:
            bool offline = false;
            bool Connected() const override { return true; }
            std::vector<MemoryReadResult> ReadMemoryBatch(
                const std::vector<MemoryReadRequest>& reqs) override
            {
                std::vector<MemoryReadResult> out;
                for (const MemoryReadRequest& q : reqs)
                {
                    MemoryReadResult r;
                    if (!offline && q.address >= kBase && q.address + q.size <= kBase + 16)
                    {
                        r.success = true;
                        r.bytes.assign(q.size, 0);
                    }
                    else
                    {
                        r.error = "dropped";
                    }
                    out.push_back(std::move(r));
                }
                return out;
            }
        };
        FlakyBackend be;
        MemorySearch s;
        const std::size_t n = s.First(be, {{kBase, 16}}, WatchType::U32, SearchCompare::Equal, 0);
        Check(n == 4 && !s.LastScanPartial(), "baseline scan is complete");
        for (const SearchHit& h : s.Hits()) Check(h.verified, "baseline hits are verified");

        be.offline = true;
        const std::size_t n2 = s.Next(be, SearchCompare::Unchanged, 0);
        Check(n2 == 4, "hits are carried over, not discarded, when the region cannot be read");
        Check(s.LastScanPartial(), "the next scan reports itself partial");
        Check(s.UnverifiedCount() == 4, "every carried-over hit is marked unverified");

        // And they clear again once the region comes back, so the mark tracks the last scan
        // rather than sticking to a hit forever.
        be.offline = false;
        const std::size_t n3 = s.Next(be, SearchCompare::Unchanged, 0);
        Check(n3 == 4, "all four are still unchanged once the region reads again");
        Check(!s.LastScanPartial() && s.UnverifiedCount() == 0,
              "the unverified mark clears on a scan that could read");
    }

    if (gFail == 0) std::printf("All MemorySearch tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
