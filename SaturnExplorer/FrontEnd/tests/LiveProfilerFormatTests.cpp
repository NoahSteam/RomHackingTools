// The profiler's wire record, both forms, against a scripted server (FakeLiveServer.h):
//
//  * a v26 server's section still parses in a v27 client, and what it carries is presented as
//    such: the old elapsed VDP1 figure kept, every v27-only field "not available";
//  * a v27 record round-trips EVERY field with distinct non-zero values, including 64-bit values
//    above 2^32 and the section header's newest / lost range;
//  * the 'valid' word is honoured exactly: bits a producer did not set stay clear, and a counter
//    the record is too short to carry is never reported as available;
//  * a server newer than this client is refused at the version check.
//
// The real exporter's end of the same record is covered in LiveProfilerTests. This one listens on
// an ephemeral loopback port, so it does not share the default socket with the other live tests.
#include "FakeLiveServer.h"
#include "LiveDriver.h"
#include "SeLiveProtocol.h"
#include "SePerfCounters.h"
#include "saturnexplorer/SaturnExplorer.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

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

using fakelive::PerfRec;
using fakelive::Reply;

// Serves 'reply' to every request of the driver's connection (the first connection is the
// driver's open-time probe and is dropped), and returns the perf records the driver queued.
std::vector<se_live_perf_frame> Collect(const Reply& reply, size_t wantAtLeast, uint32_t* serverVersion = nullptr,
                                        int budgetMs = 5000)
{
    std::vector<se_live_perf_frame> got;
    const std::vector<uint8_t> bytes = fakelive::Build(reply);
    fakelive::Server server([&bytes](int fd, int index) {
        if (index == 0) return;
        fakelive::Request r;
        while (fakelive::ReadRequest(fd, r))
            if (!fakelive::WriteExact(fd, bytes.data(), bytes.size())) return;
    });
    if (!server.Start()) { CHECK(false && "listener"); return got; }
    se_data_source ds = {};
    if (se_live_open(server.Endpoint().c_str(), &ds) != SE_OK) { CHECK(false && "open"); return got; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
    while (std::chrono::steady_clock::now() < deadline && got.size() < wantAtLeast)
    {
        se_live_perf_frame batch[8];
        const uint32_t n = se_live_drain_perf(&ds, batch, 8);
        for (uint32_t i = 0; i < n; ++i) got.push_back(batch[i]);
        if (!n) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (serverVersion) *serverVersion = se_live_server_version(&ds);
    ds.close(ds.user);
    server.Stop();
    return got;
}

PerfRec V26Record()
{
    PerfRec p;
    p.sampleSeq = 5;
    p.flags = SE_LIVE_PROF_FUNCTIONS;
    for (unsigned i = 0; i < SE_PERF_V26_COUNT; ++i) p.counters.push_back(5000000000ull + i * 1000u + 1u);
    p.counters[SE_PERF_V26_VDP1_ELAPSED] = 777777;
    p.fns = { { 0x06004000u, 4242u }, { 0x06010000u | SE_PERF_FN_SLAVE, 99u } };
    p.fnOther = 9;
    return p;
}

PerfRec V27Record()
{
    PerfRec p;
    p.sampleSeq = 0x100000005ull;           // above 2^32
    p.flags = SE_LIVE_PROF_FUNCTIONS;       // the latched mode
    p.timelineId = 0x00A1B2C3u;
    p.frameNo = 0x200000033ull;
    p.cacheMode = SE_PERF_CACHE_ON;
    p.valid = SE_PERF_VALID_ALL;
    p.satFlags = SE_PERF_SAT_TABLE_FULL | SE_PERF_SAT_TRUNCATED;
    p.otherNotAdmitted[0] = 0x100000001ull; p.otherNotAdmitted[1] = 2222;
    p.otherEvicted[0] = 3333;               p.otherEvicted[1] = 0x300000004ull;
    for (unsigned i = 0; i < SE_PERF_COUNT; ++i) p.counters.push_back(0x800000000ull + i * 1000003u + 1u);
    p.counters[SE_PERF_RETIRED_6] = 0;      // retired in v27: always 0
    p.fns = { { 0x06004000u, 4242u }, { SE_PERF_FN_RAW_PC | 0x06020040u, 77u } };
    p.fnOther = 5000u;
    return p;
}
}  // namespace

int main()
{
    // ---- a v26 server, read by this (v27) client ----
    {
        Reply r;
        r.version = 26;
        r.perf.push_back(V26Record());
        uint32_t sv = 0;
        const auto got = Collect(r, 1, &sv);
        CHECK(sv == 26);
        CHECK(got.size() >= 1);
        if (!got.empty())
        {
            const se_live_perf_frame& f = got[0];
            CHECK(f.wire_version == 26);
            CHECK(f.valid == 0);   // no v27 bit; wire_version says counters[6] is the old VDP1 figure
            CHECK(f.seq == 5 && f.sample_seq == 5);
            CHECK(f.flags == SE_LIVE_PROF_FUNCTIONS && f.mode == SE_LIVE_PROF_FUNCTIONS);
            CHECK(f.counter_count == SE_PERF_V26_COUNT);
            for (unsigned i = 0; i < SE_PERF_V26_COUNT; ++i)
                CHECK(f.counters[i] == (i == SE_PERF_V26_VDP1_ELAPSED ? 777777ull : 5000000000ull + i * 1000u + 1u));
            for (unsigned i = SE_PERF_V26_COUNT; i < SE_LIVE_PERF_COUNTERS; ++i) CHECK(f.counters[i] == 0);
            CHECK(f.fn_count == 2 && f.fn_key[0] == 0x06004000u && f.fn_cycles[0] == 4242u);
            CHECK(f.fn_key[1] == (0x06010000u | SE_PERF_FN_SLAVE) && f.fn_cycles[1] == 99u);
            CHECK(f.fn_other_cycles == 9);
            // Not available, and nothing pretends otherwise.
            CHECK(f.timeline_id == 0 && f.frame_no == 0 && f.cache_mode == 0 && f.sat_flags == 0);
            CHECK(f.other_not_admitted[0] == 0 && f.other_evicted[1] == 0);
            CHECK(f.newest_seq == 0 && f.gap_first == 0 && f.gap_last == 0);
        }
    }

    // ---- a v27 record: every new field, distinct non-zero values ----
    {
        Reply r;
        r.version = 27;
        r.perf.push_back(V27Record());
        r.perfNewest = 0x100000009ull;
        r.perfLostFirst = 0x100000002ull;
        r.perfLostLast = 0x100000004ull;     // lostLast + 1 == the first record's sampleSeq
        uint32_t sv = 0;
        const auto got = Collect(r, 1, &sv);
        CHECK(sv == 27);
        CHECK(got.size() >= 1);
        if (!got.empty())
        {
            const se_live_perf_frame& f = got[0];
            CHECK(f.wire_version == 27);
            CHECK(f.valid == SE_PERF_VALID_ALL);
            CHECK(f.sample_seq == 0x100000005ull && f.seq == 5u);
            CHECK(f.timeline_id == 0x00A1B2C3u);
            CHECK(f.frame_no == 0x200000033ull);
            CHECK(f.mode == SE_LIVE_PROF_FUNCTIONS && f.flags == SE_LIVE_PROF_FUNCTIONS);
            CHECK(f.cache_mode == SE_PERF_CACHE_ON);
            CHECK(f.sat_flags == (SE_PERF_SAT_TABLE_FULL | SE_PERF_SAT_TRUNCATED));
            CHECK(f.other_not_admitted[0] == 0x100000001ull && f.other_not_admitted[1] == 2222);
            CHECK(f.other_evicted[0] == 3333 && f.other_evicted[1] == 0x300000004ull);
            CHECK(f.newest_seq == 0x100000009ull);
            CHECK(f.gap_first == 0x100000002ull && f.gap_last == 0x100000004ull);
            CHECK(f.counter_count == SE_PERF_COUNT);
            for (unsigned i = 0; i < SE_PERF_COUNT; ++i)
                CHECK(f.counters[i] == (i == SE_PERF_RETIRED_6 ? 0ull : 0x800000000ull + i * 1000003u + 1u));
            CHECK(f.counters[SE_PERF_SSH2_ON_INTEGRATED] != 0 && f.counters[SE_PERF_VDP1_WORK_CYCLES] != 0 &&
                  f.counters[SE_PERF_VDP1_DMA_DISCARDED] != 0 && f.counters[SE_PERF_M68K_IDLE_CYCLES] != 0 &&
                  f.counters[SE_PERF_BUS_ACCESS_X + 3] != 0);
            CHECK(f.fn_count == 2 && f.fn_key[1] == (SE_PERF_FN_RAW_PC | 0x06020040u) && f.fn_cycles[1] == 77u);
            CHECK(f.fn_other_cycles == 5000u);
        }
    }

    // ---- v27: bits a producer left clear stay clear; a short counter array cannot claim more ----
    {
        Reply r;
        r.version = 27;
        PerfRec lazy = V27Record();
        lazy.valid = 0;                                   // a Step-0 style producer: nothing filled
        lazy.timelineId = 0; lazy.frameNo = 0; lazy.cacheMode = 0; lazy.satFlags = 0;
        lazy.otherNotAdmitted[0] = lazy.otherNotAdmitted[1] = lazy.otherEvicted[0] = lazy.otherEvicted[1] = 0;
        r.perf.push_back(lazy);
        const auto got = Collect(r, 1);
        CHECK(got.size() >= 1);
        if (!got.empty()) CHECK(got[0].valid == 0 && got[0].wire_version == 27 && got[0].timeline_id == 0);

        Reply s;
        s.version = 27;
        PerfRec shortRec = V27Record();                   // claims everything, sends only the v26 counters
        shortRec.counters.resize(SE_PERF_V26_COUNT);
        s.perf.push_back(shortRec);
        const auto gs = Collect(s, 1);
        CHECK(gs.size() >= 1);
        if (!gs.empty())
        {
            CHECK(gs[0].counter_count == SE_PERF_V26_COUNT);
            CHECK(gs[0].valid == (SE_PERF_VALID_ALL & ~(SE_PERF_VALID_SSH2_INT | SE_PERF_VALID_VDP1_WORK |
                                                        SE_PERF_VALID_M68K_IDLE | SE_PERF_VALID_BUS_X)));
        }
    }

    // ---- a server newer than this client: refused at the version check, no records ----
    {
        Reply r;
        r.version = SE_LIVE_VERSION + 1;
        r.perf.push_back(V27Record());
        uint32_t sv = 99;
        const auto got = Collect(r, 1, &sv, 600);
        CHECK(got.empty());
        CHECK(sv == 0);
    }

    // ---- the version constant is the frozen one ----
    CHECK(SE_LIVE_VERSION == 27u);
    CHECK(SE_PERF_COUNT == 62);
    CHECK(SE_LIVE_PERF_MAX_LEN == 80u + 256u * 8u + 64u * 8u);

    if (gFailures) { std::printf("%d check(s) failed\n", gFailures); return 1; }
    std::printf("LiveProfilerFormatTests passed\n");
    return 0;
}
