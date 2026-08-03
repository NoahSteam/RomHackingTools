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
        Check(MemorySearch::DecodeBigEndian(p, SearchType::U16) == 0x1234, "decode u16 BE");
        Check(MemorySearch::DecodeBigEndian(p, SearchType::U32) == 0x12345678, "decode u32 BE");
        const uint8_t n[1] = {0xFF};
        Check(MemorySearch::DecodeBigEndian(n, SearchType::S8) == -1, "decode s8 negative");
        Check(MemorySearch::DecodeBigEndian(n, SearchType::U8) == 255, "decode u8");
    }

    // --- First scan: u16 == 100 finds exactly the seeded addresses ---
    be.PutBE(kBase + 0, 2, 100);
    be.PutBE(kBase + 2, 2, 100);
    be.PutBE(kBase + 4, 2, 42);
    be.PutBE(kBase + 6, 2, 100);
    {
        MemorySearch s;
        std::size_t n = s.First(be, regions, SearchType::U16, SearchCompare::Equal, 100);
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
        std::size_t n = s.First(be, regions, SearchType::U32, SearchCompare::Unknown, 0);
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
        std::size_t n = s.First(s8be, r8, SearchType::S8, SearchCompare::Less, 0);
        Check(n == 2 && HasAddr(s, kBase + 1) && HasAddr(s, kBase + 2),
              "signed S8 < 0 finds negatives only");
    }

    if (gFail == 0) std::printf("All MemorySearch tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
