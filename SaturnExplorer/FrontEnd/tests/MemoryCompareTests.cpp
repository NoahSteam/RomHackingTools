// Unit tests for MemoryCompare: region traits, snapshot capture, the diff, and the complete outputs
// (rows, CSV) that must stay exact even where the stored range list is capped.
#include "Debug/MemoryCompare.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "SaturnRegions.h"

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

size_t Ix(RegionId id) { return static_cast<size_t>(id); }

// A snapshot whose every region is zero; tests poke bytes into it.
std::shared_ptr<MemSnapshot> Blank(uint64_t frame, uint64_t session = 1, uint64_t epoch = 1)
{
    auto s = std::make_shared<MemSnapshot>();
    s->origin = { session, epoch, frame, false };
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        MemRegionImage img;
        img.id = static_cast<RegionId>(i);
        img.bytes.assign(Traits(img.id).size, 0);
        s->regions.push_back(std::move(img));
    }
    return s;
}

DiffResult MakeDiff(const std::shared_ptr<MemSnapshot>& a, const std::shared_ptr<MemSnapshot>& b,
                    const DiffOptions& o = DiffOptions())
{
    DiffResult d;
    const DiffStatus st = Diff(a, b, o, &d);
    Check(st == DiffStatus::Ok, "Diff of two compatible snapshots is Ok");
    return d;
}

// Every differing byte of a region, computed the slow obvious way.
std::vector<uint32_t> Oracle(const MemSnapshot& a, const MemSnapshot& b, RegionId id)
{
    std::vector<uint32_t> v;
    const auto& x = a.regions[Ix(id)].bytes;
    const auto& y = b.regions[Ix(id)].bytes;
    for (size_t i = 0; i < x.size(); ++i)
        if (x[i] != y[i]) v.push_back(static_cast<uint32_t>(i));
    return v;
}

// StringCsvSink that can be told to stop, to test cancellation.
struct StringSink : StringCsvSink
{
    size_t limit = ~size_t(0);
    bool Write(const char* d, size_t n) override
    {
        StringCsvSink::Write(d, n);
        return text.size() < limit;
    }
};

// Every differing offset of a region, read back from the rows the diff produces.
std::vector<uint32_t> OffsetsFromRows(const DiffResult& d, RegionId id, const DiffOptions& o = DiffOptions())
{
    std::vector<uint32_t> v;
    for (const DiffRow& row : BuildRows(d, id, o))
        for (uint32_t k = 0; k < kDiffRowBytes; ++k)
            if (row.changedMask & (1u << k)) v.push_back(row.ref.offset + k);
    return v;
}

size_t CountLines(const std::string& s, const char* prefix)
{
    size_t n = 0, pos = 0;
    const size_t pl = std::strlen(prefix);
    while (pos < s.size())
    {
        const size_t e = s.find('\n', pos);
        if (e == std::string::npos) break;
        if (s.compare(pos, pl, prefix) == 0) ++n;
        pos = e + 1;
    }
    return n;
}

// A backend serving every captured region from memory.
class MockBackend : public IMemoryBackend
{
public:
    MockBackend()
    {
        for (size_t i = 0; i < kRegionCount; ++i)
            mem.emplace_back(Traits(static_cast<RegionId>(i)).size, uint8_t(i + 1));
    }
    bool Connected() const override { return connected; }
    std::vector<MemoryReadResult> ReadMemoryBatch(const std::vector<MemoryReadRequest>& reqs) override
    {
        std::vector<MemoryReadResult> out;
        for (const MemoryReadRequest& q : reqs)
        {
            MemoryReadResult r;
            RegionRef ref;
            if (Resolve(q.address, &ref) && static_cast<int>(ref.id) != failRegion &&
                ref.offset + q.size <= mem[static_cast<size_t>(ref.id)].size())
            {
                const std::vector<uint8_t>& m = mem[static_cast<size_t>(ref.id)];
                r.success = true;
                r.bytes.assign(m.begin() + ref.offset, m.begin() + ref.offset + q.size);
            }
            out.push_back(std::move(r));
        }
        return out;
    }
    std::vector<std::vector<uint8_t>> mem;
    int  failRegion = -1;
    bool connected = true;
};

// ---- Tests -------------------------------------------------------------------------------------

void TestTraits()
{
    size_t count = 0;
    const SaturnRegion* regions = SaturnRegions(count);
    Check(count == kRegionCount, "RegionId matches SaturnRegions() in count");
    for (size_t i = 0; i < count && i < kRegionCount; ++i)
    {
        const RegionTraits& t = Traits(static_cast<RegionId>(i));
        Check(t.id == static_cast<RegionId>(i), "traits are indexed by RegionId");
        Check(t.busBase == regions[i].base && t.size == regions[i].size, "traits base/size come from SaturnRegions()");
        Check(std::strcmp(t.name, regions[i].name) == 0, "traits name comes from SaturnRegions()");
        Check((t.caps & kCapNavigate) && (t.caps & kCapWatch), "every region can be navigated to and watched");
    }
    // The extras table is positional, so a reorder of SaturnRegions() must show up here: pin the
    // name each id is expected to carry.
    const char* const expected[kRegionCount] = { "LWRAM", "HWRAM", "Sound RAM", "VDP1 RAM", "VDP1 FB",
                                                 "VDP1 Regs", "VDP2 RAM", "VDP2 CRAM", "VDP2 Regs" };
    for (size_t i = 0; i < kRegionCount; ++i)
        Check(std::strcmp(Traits(static_cast<RegionId>(i)).name, expected[i]) == 0, "each RegionId keeps its region");
    Check(HasBusAddress(Traits(RegionId::Hwram)) && !HasBusAddress(Traits(RegionId::Vdp1Fb)),
          "only the VDP1 FB has no bus address");
    Check(Traits(RegionId::SoundRam).caps & kCapBreakWrite, "Sound RAM: SH-2 and SCU-DMA writes to it can be caught");
    Check(!(Traits(RegionId::SoundRam).caps & kCapSh2Code), "but it is not SH-2 code (the 68K runs from it)");
    Check(!(Traits(RegionId::Vdp1Fb).caps & kCapBreakWrite), "VDP1 FB: not a bus write, no write break");
    Check(Traits(RegionId::Vdp1Fb).space == AddressSpace::DeviceImage, "VDP1 FB is a device image");
    Check(Traits(RegionId::Vdp2Regs).space == AddressSpace::RegisterImage, "VDP2 regs are a register image");
    Check((Traits(RegionId::Hwram).caps & kCapSh2Code) && (Traits(RegionId::Lwram).caps & kCapSh2Code),
          "work RAM can hold SH-2 code");
    Check(!(Traits(RegionId::Vdp2Ram).caps & kCapSh2Code), "VDP2 RAM is not code");

    RegionRef r;
    Check(Resolve(0x06034F20u, &r) && r.id == RegionId::Hwram && r.offset == 0x34F20u, "Resolve HWRAM");
    Check(BusAddress(r) == 0x06034F20u, "BusAddress round-trips");
    Check(Resolve(0x26034F20u, &r) && r.id == RegionId::Hwram && r.offset == 0x34F20u, "Resolve folds the through mirror");
    Check(Resolve(0x05F80004u, &r) && r.id == RegionId::Vdp2Regs && r.offset == 4, "Resolve VDP2 regs");
    Check(!Resolve(0x00000000u, &r), "an unmapped address does not resolve");
}

void TestIdentical()
{
    auto a = Blank(10), b = Blank(20);
    DiffResult d = MakeDiff(a, b);
    Check(d.TotalChangedBytes() == 0, "identical snapshots: no changes");
    for (const RegionDiff& r : d.regions)
        Check(r.changedBytes == 0 && r.rangeCount == 0, "identical: empty region diff");
    Check(BuildRows(d, RegionId::Hwram, DiffOptions()).empty(), "identical: no rows in Changes Only");
}

void TestSingleBytes()
{
    for (RegionId id : { RegionId::Hwram, RegionId::Vdp1Regs, RegionId::Vdp2Regs, RegionId::Vdp1Fb })
    {
        const uint32_t size = Traits(id).size;
        for (uint32_t off : { 0u, size - 1 })
        {
            auto a = Blank(1), b = Blank(2);
            b->regions[Ix(id)].bytes[off] = 0x5A;
            DiffResult d = MakeDiff(a, b);
            const RegionDiff& r = d.regions[Ix(id)];
            Check(r.changedBytes == 1 && r.rangeCount == 1 && OffsetsFromRows(d, id) == std::vector<uint32_t>({ off }),
                  "a single changed byte is one range, at that offset");
            Check(d.TotalChangedBytes() == 1, "and nothing else changed");
        }
    }
}

void TestWordFastPath()
{
    // A difference at each byte of a word, in a region large enough to take the word path.
    for (uint32_t k = 0; k < 8; ++k)
    {
        auto a = Blank(1), b = Blank(2);
        b->regions[Ix(RegionId::Lwram)].bytes[64 + k] = 1;
        DiffResult d = MakeDiff(a, b);
        const RegionDiff& r = d.regions[Ix(RegionId::Lwram)];
        Check(r.changedBytes == 1 && OffsetsFromRows(d, RegionId::Lwram) == std::vector<uint32_t>({ 64 + k }),
              "a change at every offset of a word is found exactly");
    }
    auto a = Blank(1), b = Blank(2);
    for (int i = 0; i < 8; ++i) b->regions[Ix(RegionId::Lwram)].bytes[64 + i] = 1;
    DiffResult d = MakeDiff(a, b);
    Check(d.regions[Ix(RegionId::Lwram)].changedBytes == 8 && d.regions[Ix(RegionId::Lwram)].rangeCount == 1,
          "a fully changed word is eight bytes in one range");
}

void TestMergeGap()
{
    DiffOptions o;   // mergeGap 4: runs separated by fewer than 4 identical bytes merge
    auto edit = [&](uint32_t x, uint32_t y)
    {
        auto a = Blank(1), b = Blank(2);
        b->regions[Ix(RegionId::Hwram)].bytes[x] = 1;
        b->regions[Ix(RegionId::Hwram)].bytes[y] = 1;
        return MakeDiff(a, b, o).regions[Ix(RegionId::Hwram)];
    };
    RegionDiff r = edit(10, 11);
    Check(r.rangeCount == 1 && r.changedBytes == 2, "adjacent bytes always merge");
    r = edit(10, 13);   // gap of 2
    Check(r.rangeCount == 1 && r.changedBytes == 2, "a gap below mergeGap merges, and changedBytes stays exact");
    r = edit(10, 14);   // gap of 3
    Check(r.rangeCount == 1, "a gap of mergeGap-1 still merges");
    r = edit(10, 15);   // gap of 4
    Check(r.rangeCount == 2, "a gap of mergeGap does not merge");
    o.mergeGap = 0;
    r = edit(10, 11);
    Check(r.rangeCount == 1, "mergeGap 0 still merges contiguous bytes");
    r = edit(10, 12);
    Check(r.rangeCount == 2, "mergeGap 0 keeps separated bytes apart");
}

void TestRowsAndContext()
{
    DiffOptions o;
    o.contextRows = 1;
    auto a = Blank(1), b = Blank(2);
    auto& bytes = b->regions[Ix(RegionId::Hwram)].bytes;
    bytes[10 * 16 + 3] = 1;    // row 10
    bytes[12 * 16 + 15] = 1;   // row 12: its context window meets row 10's
    bytes[30 * 16] = 1;        // row 30: far away
    DiffResult d = MakeDiff(a, b, o);
    std::vector<DiffRow> rows = BuildRows(d, RegionId::Hwram, o);
    // rows 9..13 and 29..31
    Check(rows.size() == 8, "windows around changes, merged where they touch");
    Check(rows[0].ref.offset == 9 * 16 && rows[0].gapBefore && rows[0].isContext, "first window starts with an elided gap");
    Check(rows[1].ref.offset == 10 * 16 && rows[1].changedMask == (1u << 3) && !rows[1].isContext, "changed row mask");
    Check(!rows[1].gapBefore && !rows[2].gapBefore && !rows[3].gapBefore && !rows[4].gapBefore,
          "no gap inside a merged window");
    Check(rows[3].ref.offset == 12 * 16 && rows[3].changedMask == 0x8000u, "mask bit 15");
    Check(rows[5].ref.offset == 29 * 16 && rows[5].gapBefore, "gapBefore marks the elided run between windows");
    Check(rows[7].ref.offset == 31 * 16 && !rows[7].gapBefore, "no gap within the second window");

    // Window clipped at the start of the region.
    auto a2 = Blank(1), b2 = Blank(2);
    b2->regions[Ix(RegionId::Hwram)].bytes[0] = 1;
    o.contextRows = 2;
    rows = BuildRows(MakeDiff(a2, b2, o), RegionId::Hwram, o);
    Check(rows.size() == 3 && rows[0].ref.offset == 0 && !rows[0].gapBefore, "window clips at the region start");

    // ... and at the end.
    auto a3 = Blank(1), b3 = Blank(2);
    const uint32_t size = Traits(RegionId::Hwram).size;
    b3->regions[Ix(RegionId::Hwram)].bytes[size - 1] = 1;
    rows = BuildRows(MakeDiff(a3, b3, o), RegionId::Hwram, o);
    Check(rows.size() == 3 && rows.back().ref.offset == size - 16, "window clips at the region end");

    // Changes Only off: every row, no gaps.
    o.changesOnly = false;
    rows = BuildRows(MakeDiff(a3, b3, o), RegionId::Hwram, o);
    Check(rows.size() == size / 16, "Changes Only off lists every row");
    bool anyGap = false;
    for (const DiffRow& r : rows) anyGap |= r.gapBefore;
    Check(!anyGap, "and elides nothing");
}

void TestPartialRow()
{
    // VDP1 regs are 0x18 bytes: the second row has only 8 valid bytes.
    auto a = Blank(1), b = Blank(2);
    b->regions[Ix(RegionId::Vdp1Regs)].bytes[0x17] = 1;
    DiffOptions o;
    o.changesOnly = false;
    std::vector<DiffRow> rows = BuildRows(MakeDiff(a, b, o), RegionId::Vdp1Regs, o);
    Check(rows.size() == 2, "0x18 bytes is two rows");
    Check(rows[0].validMask == 0xFFFFu && rows[1].validMask == 0x00FFu, "the short row marks its valid bytes");
    Check(rows[1].changedMask == (1u << 7), "and the change lands inside them");
}

void TestLargeDiffsStayExact()
{
    // Every second byte of a 1 MiB region differs and mergeGap 1 keeps them apart: half a million
    // ranges. Counts, rows and CSV must all cover every one.
    DiffOptions o;
    o.mergeGap = 1;
    auto a = Blank(1), b = Blank(2);
    auto& bytes = b->regions[Ix(RegionId::Hwram)].bytes;
    for (size_t i = 0; i < bytes.size(); i += 2) bytes[i] = 0xEE;
    DiffResult d = MakeDiff(a, b, o);
    const RegionDiff& r = d.regions[Ix(RegionId::Hwram)];
    const uint32_t expect = static_cast<uint32_t>(bytes.size() / 2);
    Check(r.changedBytes == expect && r.rangeCount == expect, "counts are exact however many ranges there are");

    const std::vector<uint32_t> truth = Oracle(*a, *b, RegionId::Hwram);
    Check(OffsetsFromRows(d, RegionId::Hwram, o) == truth, "rows cover every changed byte");

    StringSink sink;
    RegionId only = RegionId::Hwram;
    Check(WriteCsv(d, &only, sink) == CsvResult::Ok, "CSV of a large diff succeeds");
    Check(CountLines(sink.text, "HWRAM,") == truth.size(), "CSV has a line for every changed byte");
}

void TestIdentityRules()
{
    DiffResult d;
    auto a = Blank(1, /*session*/ 1, /*epoch*/ 1);
    Check(Diff(a, Blank(2, 2, 1), DiffOptions(), &d) == DiffStatus::SessionMismatch, "different sessions are refused");
    Check(Diff(a, Blank(2, 1, 9), DiffOptions(), &d) == DiffStatus::Ok, "different timelines of one session compare");
    Check(Diff(nullptr, a, DiffOptions(), &d) == DiffStatus::NullSnapshot, "a null snapshot is refused");
    auto bad = Blank(2);
    bad->regions[Ix(RegionId::Hwram)].bytes.pop_back();
    Check(Diff(a, bad, DiffOptions(), &d) == DiffStatus::RegionMismatch, "a wrong-sized region is refused");
    auto few = Blank(2);
    few->regions.pop_back();
    Check(Diff(a, few, DiffOptions(), &d) == DiffStatus::RegionMismatch, "a missing region is refused");
}

void TestSwapMirrors()
{
    auto a = Blank(1), b = Blank(2);
    b->regions[Ix(RegionId::Lwram)].bytes[5] = 9;
    b->regions[Ix(RegionId::Vdp2Ram)].bytes[7] = 3;
    DiffResult ab = MakeDiff(a, b), ba = MakeDiff(b, a);
    Check(ab.TotalChangedBytes() == ba.TotalChangedBytes() && ab.TotalChangedBytes() == 2, "swapping A and B keeps the counts");
    StringSink s1, s2;
    WriteCsv(ab, nullptr, s1);
    WriteCsv(ba, nullptr, s2);
    Check(s1.text.find("LWRAM,0x00000005,0x00200005,0x00,0x09") != std::string::npos, "A to B: old then new");
    Check(s2.text.find("LWRAM,0x00000005,0x00200005,0x09,0x00") != std::string::npos, "B to A: swapped");
}

void TestCapture()
{
    MockBackend be;
    SnapshotOrigin origin{ 7, 3, 1800, false };
    std::string err;
    auto snap = CaptureSnapshot(be, origin, nullptr, &err);
    Check(snap != nullptr, "capture succeeds");
    if (!snap) return;
    Check(snap->origin.sessionId == 7 && snap->origin.timelineEpoch == 3 && snap->origin.frameNo == 1800,
          "the snapshot carries its origin");
    Check(snap->regions.size() == kRegionCount, "every region is captured");
    for (size_t i = 0; i < kRegionCount; ++i)
        Check(snap->regions[i].bytes == be.mem[i], "region bytes match the source");

    // Immutability: changing the source afterwards does not touch the snapshot.
    const uint8_t before = snap->regions[Ix(RegionId::Hwram)].bytes[100];
    be.mem[Ix(RegionId::Hwram)][100] ^= 0xFF;
    Check(snap->regions[Ix(RegionId::Hwram)].bytes[100] == before, "a snapshot is a copy, not a view");

    for (size_t fail = 0; fail < kRegionCount; ++fail)
    {
        be.failRegion = static_cast<int>(fail);
        Check(CaptureSnapshot(be, origin, nullptr, &err) == nullptr && !err.empty(),
              "a region that cannot be read fails the capture, never leaves it partial");
    }
    be.failRegion = -1;

    SnapshotOrigin zero = origin;
    zero.frameNo = 0;
    Check(CaptureSnapshot(be, zero, nullptr, &err) == nullptr, "frame number 0 is rejected");

    MockBackend gone;
    gone.connected = false;
    Check(CaptureSnapshot(gone, origin, nullptr, &err) == nullptr, "a disconnected source is rejected");

    int calls = 0;
    auto steady = [&]() { ++calls; return CaptureGuard{ 5, 9 }; };
    Check(CaptureSnapshot(be, origin, steady, &err) != nullptr && calls == 2, "an unchanged guard accepts, checked before and after");
    auto moving = [&]() { return CaptureGuard{ static_cast<uint64_t>(calls++), 9 }; };
    calls = 0;
    Check(CaptureSnapshot(be, origin, moving, &err) == nullptr && err.find("changed") != std::string::npos,
          "a context that re-derived mid-capture discards the snapshot");
    int n = 0;
    auto sourceMoves = [&]() { return CaptureGuard{ 5, static_cast<uint64_t>(n++) }; };
    Check(CaptureSnapshot(be, origin, sourceMoves, &err) == nullptr, "a changed source id discards the snapshot");
}

void TestCsv()
{
    auto a = Blank(100, 4, 2), b = Blank(200, 4, 2);
    b->origin.liveHead = true;
    b->regions[Ix(RegionId::Hwram)].bytes[0x10] = 0xAB;
    b->regions[Ix(RegionId::Vdp1Fb)].bytes[3] = 0x7F;
    DiffResult d = MakeDiff(a, b);

    StringSink all;
    Check(WriteCsv(d, nullptr, all) == CsvResult::Ok, "CSV of all regions");
    const char* golden =
        "# Saturn Explorer memory diff\n"
        "# A: frame 100, session 4, timeline 2, rewind frame\n"
        "# B: frame 200, session 4, timeline 2, live head\n"
        "region,offset,bus_address,old,new\n"
        "HWRAM,0x00000010,0x06000010,0x00,0xAB\n"
        "VDP1 FB,0x00000003,,0x00,0x7F\n";
    Check(all.text == golden, "CSV matches the golden text (no bus address for the device image)");

    StringSink one;
    RegionId only = RegionId::Hwram;
    WriteCsv(d, &only, one);
    Check(one.text.find("HWRAM,") != std::string::npos && one.text.find("VDP1 FB,") == std::string::npos,
          "a single-region export holds only that region");

    Check(CsvFileName(d, nullptr) == "saturn_memory_diff_100_200.csv", "the export file name carries both frames");
    Check(CsvFileName(d, &only) == "saturn_memory_diff_100_200_hwram.csv", "and the region, lower case");
    const RegionId fb = RegionId::Vdp1Fb;
    Check(CsvFileName(d, &fb) == "saturn_memory_diff_100_200_vdp1_fb.csv", "with spaces as underscores");

    StringSink cancel;
    cancel.limit = 1;   // refuse the first chunk
    Check(WriteCsv(d, nullptr, cancel) == CsvResult::Cancelled, "a sink that stops the write cancels cleanly");

    DiffResult tampered = d;
    tampered.regions[Ix(RegionId::Hwram)].changedBytes += 1;
    StringSink bad;
    Check(WriteCsv(tampered, nullptr, bad) == CsvResult::IntegrityError, "a count mismatch is an error, not a short file");
}

void TestSlicedCsv()
{
    // The export runs a slice at a time so the UI stays responsive: any slice size must produce
    // exactly the bytes a single pass does, and the size it announces up front must be the real size.
    auto a = Blank(1), b = Blank(2);
    auto mutate = [&](RegionId id, size_t step)
    {
        auto& bytes = b->regions[Ix(id)].bytes;
        for (size_t i = 0; i < bytes.size(); i += step) bytes[i] = 0xEE;
    };
    mutate(RegionId::Lwram, 5);
    mutate(RegionId::Hwram, 3);
    mutate(RegionId::Vdp2Regs, 1);   // every byte, ending mid-row
    b->regions[Ix(RegionId::Vdp1Regs)].bytes.back() = 0x11;   // the last byte of a region
    DiffResult d = MakeDiff(a, b);

    StringSink whole;
    Check(WriteCsv(d, nullptr, whole) == CsvResult::Ok, "one pass writes the export");
    for (uint32_t budget : { 1u, 7u, 4096u, 1000000u })
    {
        CsvExport job(d, nullptr);
        Check(job.TotalBytes() == whole.text.size(), "the announced size is the exact size");
        Check(job.TotalLines() == d.TotalChangedBytes(), "the announced line count is the diff's");
        StringSink sink;
        uint64_t steps = 1, before = 0;   // the last, finishing call is a step too
        bool sliceTooBig = false;
        while (!job.Step(sink, budget))
        {
            ++steps;
            sliceTooBig |= job.LinesWritten() - before > budget;
            before = job.LinesWritten();
        }
        Check(job.Result() == CsvResult::Ok && sink.text == whole.text, "sliced output equals the single pass");
        Check(job.LinesWritten() == job.TotalLines() && job.Progress() == 1.0f, "progress ends complete");
        Check(!sliceTooBig && steps >= job.TotalLines() / budget, "a step writes no more than its budget");
    }

    RegionId only = RegionId::Vdp1Regs;
    CsvExport last(d, &only);
    StringSink lastOut;
    while (!last.Step(lastOut, 1)) {}
    Check(last.Result() == CsvResult::Ok && lastOut.text.size() == last.TotalBytes() && last.TotalLines() == 1,
          "pausing on the last changed byte of a region still finishes cleanly");

    CsvExport cancelled(d, nullptr);
    StringSink stops;
    stops.limit = 1;
    Check(cancelled.Step(stops, 0) && cancelled.Result() == CsvResult::Cancelled, "a sink that stops it cancels the job");
    Check(cancelled.Step(stops, 0), "and a finished job stays finished");

    DiffResult tampered = d;
    tampered.regions[Ix(RegionId::Hwram)].changedBytes += 1;
    CsvExport bad(tampered, nullptr);
    StringSink badOut;
    while (!bad.Step(badOut, 100)) {}
    Check(bad.Result() == CsvResult::IntegrityError, "a count mismatch found mid-way is still an error");

    CsvExport empty(DiffResult(), nullptr);
    StringSink none;
    Check(empty.Step(none, 10) && empty.Result() == CsvResult::IntegrityError && none.text.empty(),
          "a diff that cannot be checked writes nothing");
}
}  // namespace

int main()
{
    TestTraits();
    TestIdentical();
    TestSingleBytes();
    TestWordFastPath();
    TestMergeGap();
    TestRowsAndContext();
    TestPartialRow();
    TestLargeDiffsStayExact();
    TestIdentityRules();
    TestSwapMirrors();
    TestCapture();
    TestCsv();
    TestSlicedCsv();
    if (gFail == 0) std::printf("MemoryCompareTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
