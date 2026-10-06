// Tests for the off-thread RAM scan: the worker must reach exactly the answer the synchronous
// scan does, accept one scan at a time, and a narrowing scan over few hits must read only the
// pages those hits sit in instead of the whole region again.
#include "Debug/MemorySearchRunner.h"

#include <chrono>
#include <cstdio>
#include <thread>

using namespace sfe;

namespace
{
constexpr uint32_t kBase = 0x06000000u;
constexpr uint32_t kSize = 0x100000u;   // one WRAM bank

class CountingBackend : public IMemoryBackend
{
public:
    CountingBackend() : mMem(kSize, 0) {}
    bool Connected() const override { return true; }
    std::vector<MemoryReadResult> ReadMemoryBatch(
        const std::vector<MemoryReadRequest>& reqs) override
    {
        std::vector<MemoryReadResult> out;
        for (const MemoryReadRequest& q : reqs)
        {
            MemoryReadResult r;
            bytesRead += q.size;
            if (q.address >= kBase && uint64_t(q.address - kBase) + q.size <= mMem.size())
            {
                r.success = true;
                r.bytes.assign(mMem.begin() + (q.address - kBase),
                               mMem.begin() + (q.address - kBase) + q.size);
            }
            out.push_back(std::move(r));
        }
        return out;
    }
    std::vector<uint8_t> mMem;
    uint64_t bytesRead = 0;
};

int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

void WaitAndPoll(MemorySearchRunner& run, MemorySearch& s)
{
    for (int i = 0; i < 5000 && !run.Poll(s); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
}  // namespace

int main()
{
    const std::vector<SearchRegion> regions{{kBase, kSize}};

    // Worker result == synchronous result, for a first scan and a narrowing one.
    {
        CountingBackend be;
        for (uint32_t i = 0; i < kSize; i += 4096) be.mMem[i + 3] = uint8_t(i / 4096);
        MemorySearch sync, async;
        MemorySearchRunner run;

        sync.First(be, regions, WatchType::U8, SearchCompare::Unknown, 0);
        Check(run.StartFirst(async, be, regions, WatchType::U8, SearchCompare::Unknown, 0),
              "a first scan starts");
        Check(!run.StartFirst(async, be, regions, WatchType::U8, SearchCompare::Unknown, 0) ||
              !run.Running(), "a second scan is refused while one runs (or it already finished)");
        WaitAndPoll(run, async);
        Check(!run.Running(), "the runner is idle after Poll");
        Check(async.Count() == sync.Count() && async.Count() == kSize, "same candidate count");

        be.mMem[0x1003] = 7;   // one byte changes
        const std::size_t a = sync.Next(be, SearchCompare::Changed, 0);
        Check(run.StartNext(async, be, SearchCompare::Changed, 0), "a next scan starts");
        WaitAndPoll(run, async);
        Check(a == 1 && async.Count() == 1 && async.Hits()[0].addr == kBase + 0x1003,
              "the narrowing scan finds exactly the changed byte");
        Check(!run.Poll(async), "a result is delivered once");
    }

    // A sparse narrowing scan reads pages, not the bank.
    {
        CountingBackend be;
        MemorySearch s;
        s.First(be, regions, WatchType::U8, SearchCompare::Equal, 0);   // everything is 0
        // Pare it down to three hits by hand: change all but three bytes, scan "unchanged".
        for (uint32_t i = 0; i < kSize; ++i) be.mMem[i] = 1;
        be.mMem[0x40] = 0; be.mMem[0x8000] = 0; be.mMem[0xFFFFF] = 0;
        s.Next(be, SearchCompare::Equal, 0);
        Check(s.Count() == 3, "three hits remain");
        be.bytesRead = 0;
        be.mMem[0x8000] = 5;
        const std::size_t n = s.Next(be, SearchCompare::Changed, 0);
        Check(n == 1 && s.Hits()[0].addr == kBase + 0x8000, "the one change is found");
        Check(be.bytesRead <= 3 * SearchSnapshot::kPage,
              "a three-hit narrowing scan reads three pages, not the region");
    }

    // A hit straddling a page boundary still gets both pages (a u16 at offset 255).
    {
        CountingBackend be;
        be.mMem[255] = 0x12; be.mMem[256] = 0x34;
        MemorySearch s;
        s.First(be, {{kBase, 512}}, WatchType::U16, SearchCompare::Equal, 0x1234);
        Check(s.Count() == 0, "aligned scan does not see the straddling value");   // sanity
        s.First(be, {{kBase + 1, 511}}, WatchType::U16, SearchCompare::Equal, 0x1234);
        Check(s.Count() == 1, "an odd-aligned region sees it");
        be.mMem[256] = 0x35;
        s.Next(be, SearchCompare::Changed, 0);
        Check(s.Count() == 1 && s.Hits()[0].value == 0x1235 && s.Hits()[0].verified,
              "the page after the boundary was read too");
    }

    if (gFail == 0) std::printf("All MemorySearchRunner tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
