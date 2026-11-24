// Frame identity and lost records of the profiler stream (F3), and the capability bits of the very
// first reply (F14). Three layers, each asserting exact numbers:
//
//  1. The real exporter's reply-section builder, driven without a socket: first replies empty and
//     not, the exact lost range of a ring overrun, a sequence crossing 2^32, deinit/init.
//  2. The real driver against a scripted server: a lost range on a reply with no record, the
//     range kept distinct from what the driver's own bounded queue evicts, 32- and 64-bit
//     sequences across their wrap, duplicates.
//  3. Real exporter + real driver end to end: capabilities of the first reply with and without a
//     debugger, producer-stamped frame identity (batch, frame without a snapshot, native load,
//     slot load, failed load, several halts in one frame), a reconnect.
//
// Layers 1 and 2 need no shared socket; layer 3 listens on its own path and port.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

extern "C" {
#include "se_export.h"
void SeExportTestSetPerfSeq(unsigned long long seq);   // se_export.c, SE_EXPORT_TEST_HOOKS builds only
}
#include "FakeLiveServer.h"
#include "LiveDriver.h"
#include "SeLiveProtocol.h"
#include "SePerfCounters.h"
#include "saturnexplorer/SaturnExplorer.h"

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what, int line)
{
    if (ok) return;
    std::printf("CHECK failed at line %d: %s\n", line, what);
    ++gFailures;
}
#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// ===================================================================================================
// 1. The exporter's section builder
// ===================================================================================================
void Publish(unsigned n)
{
    unsigned long long c[SE_PERF_COUNT] = {};
    for (unsigned i = 0; i < n; ++i) SeExportPublishPerf(c, SE_PERF_COUNT, nullptr, nullptr, 0, 0, nullptr);
}

struct Section
{
    uint32_t count = 0;
    uint64_t newest = 0, lostFirst = 0, lostLast = 0;
    std::vector<uint64_t> seqs;
};
uint32_t Rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint64_t Rd64(const unsigned char* p) { return Rd32(p) | (static_cast<uint64_t>(Rd32(p + 4)) << 32); }

Section Build(SeExportPerfCursor& cur)
{
    static std::vector<unsigned char> buf(SE_EXPORT_PERF_SECTION_MAX);
    const size_t len = SeExportBuildPerfSection(&cur, buf.data());
    Section s;
    CHECK(len >= SE_EXPORT_PERF_HEADER_LEN);
    s.count = Rd32(buf.data());
    s.newest = Rd64(buf.data() + 4);
    s.lostFirst = Rd64(buf.data() + 12);
    s.lostLast = Rd64(buf.data() + 20);
    size_t off = SE_EXPORT_PERF_HEADER_LEN;
    for (uint32_t i = 0; i < s.count && off + 12 <= len; ++i)
    {
        const uint32_t rl = Rd32(buf.data() + off);
        s.seqs.push_back(Rd64(buf.data() + off + 4));
        off += 4 + rl;
    }
    CHECK(off == len);
    return s;
}

bool Seqs(const Section& s, uint64_t first, uint32_t n)
{
    if (s.seqs.size() != n || s.count != n) return false;
    for (uint32_t i = 0; i < n; ++i)
        if (s.seqs[i] != first + i) return false;
    return true;
}

void TestSectionBuilder()
{
    // An initially empty first reply, then three publications: all three arrive.
    CHECK(SeExportPrepare() == 0);
    {
        SeExportPerfCursor cur = {};
        const Section first = Build(cur);
        CHECK(first.count == 0 && first.newest == 0 && first.lostFirst == 0 && first.lostLast == 0);
        Publish(3);
        const Section s = Build(cur);
        CHECK(Seqs(s, 1, 3));
        CHECK(s.newest == 3 && s.lostFirst == 0 && s.lostLast == 0);
        const Section none = Build(cur);
        CHECK(none.count == 0 && none.newest == 3 && none.lostLast == 0);
    }
    SeExportDeinit();

    // A first reply made when records exist starts at the newest one, and that is not a loss.
    CHECK(SeExportPrepare() == 0);
    {
        Publish(5);
        SeExportPerfCursor cur = {};
        const Section s = Build(cur);
        CHECK(Seqs(s, 5, 1));
        CHECK(s.newest == 5 && s.lostFirst == 0 && s.lostLast == 0);
        Publish(2);
        CHECK(Seqs(Build(cur), 6, 2));
    }
    SeExportDeinit();

    // A connection that falls more than the ring behind is told exactly what it lost. The ring
    // holds 16 and a reply carries at most 8, so the first reply after the overrun starts at
    // newest - 15 and the range ends just before it.
    CHECK(SeExportPrepare() == 0);
    {
        SeExportPerfCursor cur = {};
        CHECK(Build(cur).count == 0);          // the first reply, empty
        Publish(40);
        const Section a = Build(cur);          // 40 published, 16 retained: 1..24 are gone
        CHECK(Seqs(a, 25, 8));
        CHECK(a.lostFirst == 1 && a.lostLast == 24 && a.newest == 40);
        const Section b = Build(cur);          // the rest of the ring: deferred, not lost
        CHECK(Seqs(b, 33, 8));
        CHECK(b.lostFirst == 0 && b.lostLast == 0);
        Publish(16);                           // exactly the ring's depth behind: nothing lost
        const Section c1 = Build(cur);
        CHECK(Seqs(c1, 41, 8) && c1.lostFirst == 0 && c1.lostLast == 0);
        CHECK(Seqs(Build(cur), 49, 8));
        Publish(17);                           // one more than the ring: exactly one lost
        const Section d = Build(cur);
        CHECK(d.lostFirst == 57 && d.lostLast == 57 && d.seqs.size() == 8 && d.seqs[0] == 58);
        // A second connection is unaffected by the first one's position.
        SeExportPerfCursor other = {};
        const Section o = Build(other);
        CHECK(Seqs(o, 73, 1) && o.lostLast == 0);
    }
    SeExportDeinit();

    // The sequence is 64-bit: crossing 2^32 is not a wrap and not a restart.
    CHECK(SeExportPrepare() == 0);
    {
        SeExportTestSetPerfSeq(0xFFFFFFFDull);
        SeExportPerfCursor cur;
        cur.sent = 0xFFFFFFFDull;
        cur.started = 1;
        Publish(4);
        const Section s = Build(cur);
        CHECK(s.count == 4);
        CHECK(s.seqs.size() == 4 && s.seqs[0] == 0xFFFFFFFEull && s.seqs[1] == 0xFFFFFFFFull &&
              s.seqs[2] == 0x100000000ull && s.seqs[3] == 0x100000001ull);
        CHECK(s.newest == 0x100000001ull && s.lostLast == 0);
        CHECK(SeExportPerfNewestSeq() == 0x100000001ull);
    }
    SeExportDeinit();

    // Deinit then init in one process: nothing of the previous run survives.
    CHECK(SeExportPrepare() == 0);
    Publish(3);
    SeExportSetPerfCapable(1);
    SeExportDeinit();
    CHECK(SeExportPrepare() == 0);
    {
        CHECK(SeExportPerfNewestSeq() == 0);
        SeExportPerfCursor cur = {};
        const Section s = Build(cur);
        CHECK(s.count == 0 && s.newest == 0);
        Publish(1);
        CHECK(Seqs(Build(cur), 1, 1));   // numbering restarted
    }
    SeExportDeinit();
}

// ===================================================================================================
// 2. The driver against a scripted server
// ===================================================================================================
using fakelive::PerfRec;
using fakelive::Reply;

PerfRec Rec(uint64_t seq)
{
    PerfRec p;
    p.sampleSeq = seq;
    p.timelineId = 1;
    p.frameNo = seq;
    p.valid = SE_PERF_VALID_IDENTITY;
    return p;
}

// Serves replies[i] to the i-th request (the last one thereafter) and lets a test wait for the
// driver to have ingested a number of replies, then look at what it queued.
class Scripted
{
public:
    explicit Scripted(std::vector<Reply> replies) : mReplies(std::move(replies)), mServer([this](int fd, int index) { Serve(fd, index); })
    {
        for (const Reply& r : mReplies) mBytes.push_back(fakelive::Build(r));
    }
    ~Scripted()
    {
        if (mOpen) mDs.close(mDs.user);
        mServer.Stop();
    }
    bool Open()
    {
        if (!mServer.Start()) return false;
        if (se_live_open(mServer.Endpoint().c_str(), &mDs) != SE_OK) return false;
        mOpen = true;
        return true;
    }
    // Wait until the driver has fully ingested 'replies' replies on its connection.
    bool WaitReplies(uint64_t replies, int budgetMs = 8000)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            se_live_perf_status st = {};
            if (se_live_perf_status_get(&mDs, &st) && st.replies >= replies) return true;
            SleepMs(1);
        }
        return false;
    }
    std::vector<se_live_perf_frame> Drain()
    {
        std::vector<se_live_perf_frame> out;
        se_live_perf_frame b[16];
        uint32_t n;
        while ((n = se_live_drain_perf(&mDs, b, 16)) != 0) out.insert(out.end(), b, b + n);
        return out;
    }
    se_live_perf_status Status()
    {
        se_live_perf_status st = {};
        se_live_perf_status_get(&mDs, &st);
        return st;
    }

private:
    void Serve(int fd, int index)
    {
        if (index == 0) return;   // the open-time probe
        fakelive::Request r;
        size_t i = 0;
        while (fakelive::ReadRequest(fd, r))
        {
            const std::vector<uint8_t>& b = mBytes[i < mBytes.size() ? i : mBytes.size() - 1];
            ++i;
            if (!fakelive::WriteExact(fd, b.data(), b.size())) return;
        }
    }
    std::vector<Reply> mReplies;
    std::vector<std::vector<uint8_t>> mBytes;
    fakelive::Server mServer;
    se_data_source mDs = {};
    bool mOpen = false;
};

Reply Idle(uint64_t newest)
{
    Reply r;
    r.perfNewest = newest;
    return r;
}

void TestDriverGaps()
{
    // A lost range on a reply with no record is not dropped: it reaches the consumer, in order,
    // as a gap-only entry, and the record that follows does not carry it a second time.
    {
        Reply r1; r1.perf = { Rec(1) }; r1.perfNewest = 1;
        Reply r2 = Idle(8); r2.perfLostFirst = 2; r2.perfLostLast = 5;     // header only
        Reply r3; r3.perf = { Rec(6) }; r3.perfNewest = 8;
        Scripted s({ r1, r2, r3, Idle(8) });
        CHECK(s.Open());
        CHECK(s.WaitReplies(5));
        const auto got = s.Drain();
        CHECK(got.size() == 3);
        if (got.size() == 3)
        {
            CHECK(!got[0].gap_only && got[0].sample_seq == 1 && got[0].gap_last == 0);
            CHECK(got[1].gap_only == 1 && got[1].gap_first == 2 && got[1].gap_last == 5 && got[1].newest_seq == 8);
            CHECK(got[1].evict_last == 0);
            CHECK(!got[2].gap_only && got[2].sample_seq == 6 && got[2].gap_first == 0 && got[2].gap_last == 0);
        }
        const se_live_perf_status st = s.Status();
        CHECK(st.server_lost == 4 && st.driver_evicted == 0 && st.queued_seq == 6 && st.newest_seq == 8);
    }
    // A newer newest_seq with nothing to carry it is reported once, not on every reply.
    {
        Reply r1 = Idle(0);
        Reply r2 = Idle(9);     // the server has published, this reply carries nothing
        Scripted s({ r1, r2, r2, r2 });
        CHECK(s.Open());
        CHECK(s.WaitReplies(5));
        const auto got = s.Drain();
        CHECK(got.size() == 1 && got[0].gap_only == 1 && got[0].newest_seq == 9 && got[0].gap_last == 0);
    }
    // A range on a reply that also carries records rides on the first of them.
    {
        Reply r1; r1.perf = { Rec(1), Rec(2) }; r1.perfNewest = 2;
        Reply r2; r2.perf = { Rec(13), Rec(14) }; r2.perfNewest = 14; r2.perfLostFirst = 3; r2.perfLostLast = 12;
        Scripted s({ r1, r2, Idle(14) });
        CHECK(s.Open());
        CHECK(s.WaitReplies(4));
        const auto got = s.Drain();
        CHECK(got.size() == 4);
        if (got.size() == 4)
        {
            CHECK(got[2].sample_seq == 13 && got[2].gap_first == 3 && got[2].gap_last == 12);
            CHECK(got[3].sample_seq == 14 && got[3].gap_last == 0);
        }
    }
    // Duplicates and older records are ignored.
    {
        Reply r1; r1.perf = { Rec(5), Rec(6) }; r1.perfNewest = 6;
        Reply r2; r2.perf = { Rec(6), Rec(5), Rec(7) }; r2.perfNewest = 7;
        Scripted s({ r1, r2, Idle(7) });
        CHECK(s.Open());
        CHECK(s.WaitReplies(4));
        const auto got = s.Drain();
        CHECK(got.size() == 3 && got[0].sample_seq == 5 && got[1].sample_seq == 6 && got[2].sample_seq == 7);
        CHECK(s.Status().server_lost == 0);
    }
    // The driver's own bound: 700 records, 8 a reply, nobody drains. The first 100 are evicted and
    // the oldest survivor says exactly which -- in the eviction range, not the server's.
    {
        std::vector<Reply> script;
        for (uint64_t base = 1; base <= 700; base += 8)
        {
            Reply r;
            for (uint64_t k = base; k < base + 8 && k <= 700; ++k) r.perf.push_back(Rec(k));
            r.perfNewest = r.perf.back().sampleSeq;
            script.push_back(r);
        }
        script.push_back(Idle(700));
        const size_t n = script.size();
        Scripted s(script);
        CHECK(s.Open());
        CHECK(s.WaitReplies(n));
        const se_live_perf_status st = s.Status();
        CHECK(st.queued_seq == 700 && st.driver_evicted == 100 && st.server_lost == 0);
        const auto got = s.Drain();
        CHECK(got.size() == 600);
        if (got.size() == 600)
        {
            CHECK(got[0].sample_seq == 101 && got[0].evict_first == 1 && got[0].evict_last == 100);
            CHECK(got[0].gap_first == 0 && got[0].gap_last == 0);
            CHECK(got[1].sample_seq == 102 && got[1].evict_last == 0);
            CHECK(got[599].sample_seq == 700);
        }
    }
    // Both causes at once stay apart: the server dropped 310..319 (reported on record 320, which
    // survives), and the queue then evicted the oldest records, none of them in that range.
    {
        std::vector<uint64_t> seqs;
        for (uint64_t k = 1; k <= 650; ++k)
            if (k < 310 || k > 319) seqs.push_back(k);
        std::vector<Reply> script;
        Reply cur;
        for (uint64_t k : seqs)
        {
            if (k == 320 || cur.perf.size() == 8)
            {
                if (!cur.perf.empty()) { cur.perfNewest = cur.perf.back().sampleSeq; script.push_back(cur); }
                cur = Reply();
                if (k == 320) { cur.perfLostFirst = 310; cur.perfLostLast = 319; }
            }
            cur.perf.push_back(Rec(k));
        }
        cur.perfNewest = cur.perf.back().sampleSeq;
        script.push_back(cur);
        script.push_back(Idle(650));
        const size_t n = script.size();
        Scripted s(script);
        CHECK(s.Open());
        CHECK(s.WaitReplies(n));
        const se_live_perf_status st = s.Status();
        // 640 records reached the queue and 40 were evicted; 10 were never sent.
        CHECK(st.server_lost == 10 && st.driver_evicted == 40 && st.queued_seq == 650);
        const auto got = s.Drain();
        CHECK(got.size() == 600);
        if (got.size() == 600)
        {
            CHECK(got[0].sample_seq == 41 && got[0].evict_first == 1 && got[0].evict_last == 40 && got[0].gap_last == 0);
            size_t at = 0;
            for (size_t i = 0; i < got.size(); ++i) if (got[i].sample_seq == 320) at = i;
            CHECK(at > 1 && got[at].gap_first == 310 && got[at].gap_last == 319 && got[at].evict_last == 0);
            CHECK(at > 1 && got[at - 1].sample_seq == 309);
        }
    }
    // Counts are exact and never derived from a span. Records 1 and 2 arrive, the server reports 3..4
    // lost, then 5..605 arrive undrained: the queue (600) evicts 1, 2 and 5 -- the record that
    // carried the server's gap among them. The survivor 6 spans 1..5 and 3..4, which overlap, and
    // must still say 3 evicted + 2 lost.
    {
        std::vector<Reply> script;
        { Reply r1; r1.perf = { Rec(1), Rec(2) }; r1.perfNewest = 2; script.push_back(r1); }
        for (uint64_t base = 5; base <= 605; base += 8)   // replies carry at most 8 records
        {
            Reply r;
            for (uint64_t k = base; k < base + 8 && k <= 605; ++k) r.perf.push_back(Rec(k));
            r.perfNewest = r.perf.back().sampleSeq;
            if (base == 5) { r.perfLostFirst = 3; r.perfLostLast = 4; }
            script.push_back(r);
        }
        script.push_back(Idle(605));
        const size_t n = script.size();
        Scripted s(script);
        CHECK(s.Open());
        CHECK(s.WaitReplies(n));
        const se_live_perf_status st = s.Status();
        CHECK(st.server_lost == 2 && st.driver_evicted == 3);
        const auto got = s.Drain();
        CHECK(got.size() == 600);
        if (got.size() == 600)
        {
            CHECK(got[0].sample_seq == 6);
            CHECK(got[0].gap_count == 2 && got[0].evict_count == 3);
            CHECK(got[0].evict_first == 1 && got[0].evict_last == 5 && got[0].gap_first == 3 && got[0].gap_last == 4);
            CHECK(got[1].gap_count == 0 && got[1].evict_count == 0);
        }
    }
    // Two separate server gaps (2..3 and 10..11) with delivered records (4..9) between them, both
    // carried by records that the queue then evicts: the survivor spans 2..11 but lost exactly 4
    // to the server (the 6 delivered records inside the span are not lost), and 8 were evicted
    // (1, 4..9 and 12).
    {
        std::vector<Reply> script;
        { Reply r1; r1.perf = { Rec(1) }; r1.perfNewest = 1; script.push_back(r1); }
        { Reply r2; r2.perf = { Rec(4), Rec(5), Rec(6), Rec(7), Rec(8), Rec(9) }; r2.perfNewest = 9;
          r2.perfLostFirst = 2; r2.perfLostLast = 3; script.push_back(r2); }
        for (uint64_t base = 12; base <= 612; base += 8)
        {
            Reply r;
            for (uint64_t k = base; k < base + 8 && k <= 612; ++k) r.perf.push_back(Rec(k));
            r.perfNewest = r.perf.back().sampleSeq;
            if (base == 12) { r.perfLostFirst = 10; r.perfLostLast = 11; }
            script.push_back(r);
        }
        script.push_back(Idle(612));
        const size_t n = script.size();
        Scripted s(script);
        CHECK(s.Open());
        CHECK(s.WaitReplies(n));
        const se_live_perf_status st = s.Status();
        CHECK(st.server_lost == 4 && st.driver_evicted == 8);
        const auto got = s.Drain();
        CHECK(got.size() == 600);
        if (got.size() == 600)
        {
            CHECK(got[0].sample_seq == 13);
            CHECK(got[0].gap_first == 2 && got[0].gap_last == 11 && got[0].gap_count == 4);
            CHECK(got[0].evict_first == 1 && got[0].evict_last == 12 && got[0].evict_count == 8);
        }
    }
    // A v27 sequence crossing 2^32 is delivered in order, with no gap and no restart.
    {
        Reply r1; r1.perf = { Rec(0xFFFFFFFEull), Rec(0xFFFFFFFFull), Rec(0x100000000ull), Rec(0x100000001ull) };
        r1.perfNewest = 0x100000001ull;
        Scripted s({ r1, Idle(0x100000001ull) });
        CHECK(s.Open());
        CHECK(s.WaitReplies(3));
        const auto got = s.Drain();
        CHECK(got.size() == 4);
        for (size_t i = 0; i < got.size() && got.size() == 4; ++i)
            CHECK(got[i].sample_seq == 0xFFFFFFFEull + i && got[i].gap_last == 0 && got[i].evict_last == 0);
    }
    // A v26 server has 32 bits: its wrap is extended, not read as a restart, and the records it
    // never sent show up as a gap -- the only way a v26 stream can say so.
    {
        Reply r1; r1.version = 26;
        PerfRec a; a.sampleSeq = 0xFFFFFFFEull; r1.perf.push_back(a);
        PerfRec b; b.sampleSeq = 0xFFFFFFFFull; r1.perf.push_back(b);
        PerfRec c; c.sampleSeq = 1; r1.perf.push_back(c);          // wrapped past 0 (0 is never sent)
        PerfRec d; d.sampleSeq = 2; r1.perf.push_back(d);
        PerfRec stale; stale.sampleSeq = 0xFFFFFFFFull; r1.perf.push_back(stale);   // a duplicate of an old one
        Reply idle; idle.version = 26;
        Scripted s({ r1, idle });
        CHECK(s.Open());
        CHECK(s.WaitReplies(3));
        const auto got = s.Drain();
        CHECK(got.size() == 4);
        if (got.size() == 4)
        {
            CHECK(got[0].wire_version == 26 && got[0].sample_seq == 0xFFFFFFFEull);
            CHECK(got[1].sample_seq == 0xFFFFFFFFull && got[1].gap_last == 0);
            CHECK(got[2].sample_seq == 0x100000001ull && got[2].seq == 1);
            CHECK(got[2].gap_first == 0x100000000ull && got[2].gap_last == 0x100000000ull);   // seq 0 was never sent
            CHECK(got[3].sample_seq == 0x100000002ull && got[3].gap_last == 0);
        }
    }
}

// ===================================================================================================
// 3. The real exporter and the real driver
// ===================================================================================================
const char* const kSock = SE_LIVE_DEFAULT_SOCK_PATH;

int gSlotResult = 0;
int gSlotLoads = 0;
extern "C" int FakeSlotInfo(unsigned int, unsigned long long* mtime) { *mtime = 1; return 1; }
// As the emulator's own PostStateLoad would: a native load notification from inside the load.
extern "C" int FakeSlotLoad(unsigned int)
{
    ++gSlotLoads;
    if (gSlotResult == 0) SeExportNotifyStateLoaded();
    return gSlotResult;
}
extern "C" void FakeSetProfiler(unsigned int) {}

bool OpenRetry(se_data_source* ds)
{
    for (int i = 0; i < 1000; ++i)
    {
        if (se_live_open(kSock, ds) == SE_OK) return true;
        SleepMs(5);
    }
    return false;
}

uint32_t CapsOf(const se_data_source& ds)
{
    uint32_t caps = 0;
    se_live_poke_info(&ds, nullptr, nullptr, nullptr, nullptr, &caps);
    return caps;
}

// Starts the exporter the way a glue should: prepare, register what the build has, admit
// listeners. 'racer' asks for capabilities at the first instant a listener exists.
uint32_t FirstReplyCaps(bool counters, bool debugger)
{
    // Every sample taken from the first reply on, ANDed and ORed: they must all be the same.
    std::atomic<uint32_t> andMask{ 0xFFFFFFFFu }, orMask{ 0 };
    std::atomic<int> samples{ 0 };
    std::atomic<bool> stop{ false };
    std::thread racer([&] {
        se_data_source ds = {};
        while (!stop.load() && se_live_open(kSock, &ds) != SE_OK) std::this_thread::yield();
        if (stop.load()) return;
        // Bounded by time, not by a count of yields: a slow transport (TCP) can need longer than any
        // fixed number of spins before its first reply exists, and a run with no first-reply sample
        // would assert nothing. The first sample is still taken the instant the version is known.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!stop.load() && samples.load() < 100 && std::chrono::steady_clock::now() < deadline)
        {
            if (se_live_server_version(&ds) != 0)
            {
                const uint32_t c = CapsOf(ds) & (SE_LIVE_CAP_PROFILER | SE_LIVE_CAP_PROF_FUNCTIONS);
                andMask.fetch_and(c);
                orMask.fetch_or(c);
                samples.fetch_add(1);
            }
            std::this_thread::yield();
        }
        ds.close(ds.user);
    });
    CHECK(SeExportPrepare() == 0);
    if (debugger) SeExportSetProfilerHook(FakeSetProfiler);
    if (counters) SeExportSetPerfCapable(1);
    CHECK(SeExportStart() == 0);
    for (int i = 0; i < 10000 && samples.load() < 100; ++i) SleepMs(2);   // up to 20 s, as the racer
    stop = true;
    racer.join();
    SeExportDeinit();
    CHECK(samples.load() > 0);
    return andMask.load() == orMask.load() ? andMask.load() : 0xDEADu;
}

void TestFirstReplyCapabilities()
{
    const uint32_t both = SE_LIVE_CAP_PROFILER | SE_LIVE_CAP_PROF_FUNCTIONS;
    // A debugger build advertises both, before any sample exists.
    CHECK(FirstReplyCaps(true, true) == both);
    // A counter-capable build without a debugger advertises only the counters.
    CHECK(FirstReplyCaps(true, false) == SE_LIVE_CAP_PROFILER);
    // A build with neither says neither; and nothing of the previous run survived its deinit.
    CHECK(FirstReplyCaps(false, false) == 0u);
}

// ---- end to end ----
// One frame as the glue plays it: the gate (which reserves the frame's identity), the counters
// handed over with that identity, the snapshot -- when the frame took one -- and the end of the
// frame (which commits the identity).
void Frame(bool snapshot = true)
{
    SeExportGateFrame();
    SeExportPerfInfo info;
    std::memset(&info, 0, sizeof info);
    SeExportFrameIdentity(&info.timelineId, &info.frameNo);
    info.valid = SE_PERF_VALID_IDENTITY;
    unsigned long long c[SE_PERF_COUNT] = {};
    SeExportPublishPerf(c, SE_PERF_COUNT, nullptr, nullptr, 0, 0, &info);
    if (snapshot)
        SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr, nullptr, nullptr, nullptr, nullptr);
    SeExportEndFrame();
}

// Lockstep: wait, without ticking, until the driver has the newest sequence the exporter published.
bool Seen(const se_data_source& ds, int budgetMs = 10000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
    const unsigned long long want = SeExportPerfNewestSeq();
    while (std::chrono::steady_clock::now() < deadline)
    {
        se_live_perf_status st = {};
        if (se_live_perf_status_get(&ds, &st) && st.queued_seq >= want) return true;
        SleepMs(1);
    }
    return false;
}

std::vector<se_live_perf_frame> DrainAll(const se_data_source& ds)
{
    std::vector<se_live_perf_frame> out;
    se_live_perf_frame b[16];
    uint32_t n;
    while ((n = se_live_drain_perf(&ds, b, 16)) != 0) out.insert(out.end(), b, b + n);
    return out;
}

void TestIdentityEndToEnd()
{
    CHECK(SeExportPrepare() == 0);
    SeExportSetPerfCapable(1);
    SeExportSetEmuSlotHooks(FakeSlotInfo, FakeSlotLoad);
    CHECK(SeExportStart() == 0);
    se_data_source ds = {};
    CHECK(OpenRetry(&ds));
    se_config cfg;
    cfg.abi_version = SE_ABI_VERSION;
    cfg.reserved = 0;
    se_context* ctx = se_create(&ds, &cfg);
    CHECK(ctx != nullptr);
    // Connected, with a snapshot flowing (a halt snapshot needs a client to be published).
    for (int i = 0; i < 400 && se_live_server_version(&ds) == 0; ++i) SleepMs(5);
    for (int i = 0; i < 400; ++i)
    {
        Frame();
        if (Seen(ds, 50)) break;
    }
    DrainAll(ds);
    const unsigned long long base = SeExportPerfNewestSeq();
    unsigned frameBase = 0;
    {
        // Establish the starting frame number from what the producer stamped.
        Frame();
        CHECK(Seen(ds));
        const auto got = DrainAll(ds);
        CHECK(got.size() == 1 && (got[0].valid & SE_PERF_VALID_IDENTITY));
        if (!got.empty()) frameBase = static_cast<unsigned>(got[0].frame_no);
    }
    const uint32_t timeline = [&] {
        Frame(); CHECK(Seen(ds));
        const auto g = DrainAll(ds);
        CHECK(g.size() == 1);
        if (!g.empty()) { CHECK(g[0].frame_no == frameBase + 1); return g[0].timeline_id; }
        return 0u;
    }();
    CHECK(timeline != 0);
    unsigned next = frameBase + 2;
    (void)base;

    // A batch of eight records: consecutive frames, one timeline, no gap.
    for (int i = 0; i < 8; ++i) Frame();
    CHECK(Seen(ds));
    {
        const auto got = DrainAll(ds);
        CHECK(got.size() == 8);
        for (size_t i = 0; i < got.size(); ++i)
            CHECK(got[i].frame_no == next + i && got[i].timeline_id == timeline && got[i].gap_last == 0);
        next += 8;
    }
    // A frame that took no snapshot (NeedSoundReverse) still has its own number, and does not
    // lend it to the next one.
    Frame(false);
    Frame();
    CHECK(Seen(ds));
    {
        const auto got = DrainAll(ds);
        CHECK(got.size() == 2);
        if (got.size() == 2) CHECK(got[0].frame_no == next && got[1].frame_no == next + 1);
        next += 2;
    }
    // A native load (the emulator replaced its own state): a new timeline, numbering continues.
    SeExportNotifyStateLoaded();
    Frame();
    CHECK(Seen(ds));
    uint32_t timeline2 = 0;
    {
        const auto got = DrainAll(ds);
        CHECK(got.size() == 1);
        if (got.size() == 1)
        {
            CHECK(got[0].timeline_id == timeline + 1 && got[0].frame_no == next);
            timeline2 = got[0].timeline_id;
        }
        ++next;
    }
    // A slot load ordered by the client: exactly one new timeline, though the emulator's own
    // post-load notification also fires inside it.
    CHECK(se_begin_frame(ctx) == SE_OK);
    gSlotResult = 0;
    CHECK(se_live_emu_load_slot(&ds, 0) == 0);
    for (int i = 0; i < 2000 && gSlotLoads == 0; ++i) { Frame(); SleepMs(1); }
    Check(gSlotLoads == 1, "the slot load ran once", __LINE__);
    CHECK(Seen(ds));
    DrainAll(ds);
    Frame();
    CHECK(Seen(ds));
    uint32_t timeline3 = 0;
    {
        const auto got = DrainAll(ds);
        CHECK(got.size() == 1);
        if (got.size() == 1) timeline3 = got[0].timeline_id;
        CHECK(timeline3 == timeline2 + 1);
    }
    // A refused slot load changes nothing.
    gSlotResult = -1;
    const int loadsBefore = gSlotLoads;
    for (int i = 0; i < 400; ++i)
    {
        se_begin_frame(ctx);
        if (se_live_emu_load_slot(&ds, 1) == 0) break;
        Frame();
        SleepMs(2);
    }
    for (int i = 0; i < 2000 && gSlotLoads == loadsBefore; ++i) { Frame(); SleepMs(1); }
    CHECK(gSlotLoads == loadsBefore + 1);
    CHECK(Seen(ds));
    DrainAll(ds);
    Frame();
    CHECK(Seen(ds));
    {
        const auto got = DrainAll(ds);
        CHECK(got.size() == 1 && got[0].timeline_id == timeline3);
    }
    // Reconnect: the new connection starts at the newest record, with no stale queue and no gap.
    se_destroy(ctx);
    ctx = nullptr;
    for (int i = 0; i < 200; ++i) { SeExportApplyInstalls(); SleepMs(2); }
    se_data_source ds2 = {};
    CHECK(OpenRetry(&ds2));
    for (int i = 0; i < 2000; ++i)
    {
        se_live_perf_status st = {};
        if (se_live_perf_status_get(&ds2, &st) && st.queued_seq != 0) break;
        SleepMs(2);
    }
    CHECK(Seen(ds2));
    {
        const auto got = DrainAll(ds2);
        CHECK(got.size() == 1 && got[0].sample_seq == SeExportPerfNewestSeq() && got[0].gap_last == 0);
    }
    // Several halts inside one frame do not advance the number: the frame's record carries it.
    {
        Frame();
        CHECK(Seen(ds2));
        const auto before = DrainAll(ds2);
        CHECK(before.size() == 1);
        const uint64_t k = before.empty() ? 0 : before[0].frame_no;
        SeExportGateFrame();
        for (unsigned h = 0; h < 3; ++h)
        {
            SeExportNotifyStop(0, 0x06001000u + h * 2);
            SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr, nullptr, nullptr, nullptr, nullptr);
        }
        SeExportPerfInfo info;
        std::memset(&info, 0, sizeof info);
        SeExportFrameIdentity(&info.timelineId, &info.frameNo);
        info.valid = SE_PERF_VALID_IDENTITY;
        unsigned long long c[SE_PERF_COUNT] = {};
        SeExportPublishPerf(c, SE_PERF_COUNT, nullptr, nullptr, 0, 0, &info);
        SeExportEndFrame();
        CHECK(Seen(ds2));
        const auto got = DrainAll(ds2);
        CHECK(got.size() == 1);
        if (got.size() == 1) CHECK(got[0].frame_no == k + 1);
        unsigned int tl = 0; unsigned long long nextFrame = 0;
        SeExportFrameIdentity(&tl, &nextFrame);
        CHECK(nextFrame == k + 2);   // the halts took no numbers
    }
    ds2.close(ds2.user);
    for (int i = 0; i < 200; ++i) { SeExportApplyInstalls(); SleepMs(2); }
    SeExportDeinit();
}
// F3 review (B): a halt delivers a snapshot; it never consumes the identity of the frame it is in.
void TestFirstHaltKeepsIdentity()
{
    CHECK(SeExportPrepare() == 0);
    SeExportSetPerfCapable(1);
    CHECK(SeExportStart() == 0);
    se_data_source ds = {};
    CHECK(OpenRetry(&ds));
    for (int i = 0; i < 2000 && se_live_server_version(&ds) == 0; ++i) SleepMs(5);   // a client is attached

    // The very first frame of the run (sFrameNo == 0), halted three times before any normal snapshot.
    SeExportGateFrame();
    unsigned tl0 = 0, tl1 = 0, tl2 = 0;
    unsigned long long before = 0, afterHalt = 0, perHalt[3] = {};
    SeExportFrameIdentity(&tl0, &before);
    CHECK(before == 1);
    for (unsigned h = 0; h < 3; ++h)
    {
        SeExportNotifyStop(0, 0x06001000u + h * 2);
        SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr, nullptr, nullptr, nullptr, nullptr);
        SeExportFrameIdentity(&tl1, &perHalt[h]);
    }
    afterHalt = perHalt[2];
    CHECK(perHalt[0] == 1 && perHalt[1] == 1 && perHalt[2] == 1);   // unchanged by each halt
    CHECK(tl1 == tl0);
    {
        SeExportPerfInfo info;
        std::memset(&info, 0, sizeof info);
        SeExportFrameIdentity(&info.timelineId, &info.frameNo);
        info.valid = SE_PERF_VALID_IDENTITY;
        unsigned long long c[SE_PERF_COUNT] = {};
        SeExportPublishPerf(c, SE_PERF_COUNT, nullptr, nullptr, 0, 0, &info);
        CHECK(info.frameNo == 1);
    }
    SeExportEndFrame();   // commits the frame: 1, once
    unsigned long long next = 0;
    SeExportFrameIdentity(&tl2, &next);
    CHECK(afterHalt == before && next == 2);   // only one frame completed: the next one is 2, not 3
    SeExportGateFrame();
    unsigned long long reserved = 0;
    SeExportFrameIdentity(&tl2, &reserved);
    CHECK(reserved == 2);
    SeExportEndFrame();
    SeExportFrameIdentity(&tl2, &next);
    CHECK(next == 3);

    ds.close(ds.user);
    for (int i = 0; i < 200; ++i) { SeExportApplyInstalls(); SleepMs(2); }
    SeExportDeinit();
}
}  // namespace

int main()
{
    TestSectionBuilder();
    TestDriverGaps();
    TestFirstReplyCapabilities();
    TestIdentityEndToEnd();
    TestFirstHaltKeepsIdentity();
    if (gFailures) { std::printf("%d check(s) failed\n", gFailures); return 1; }
    std::printf("LivePerfLossTests passed\n");
    return 0;
}
