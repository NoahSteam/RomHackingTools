// Hardware event log transport (HWE verb, trailing section, enable mask), exporter and driver.
//
//  1. Real exporter + real driver on a socket and port of their own, in lockstep: the test advances
//     the "emulate thread" itself (SeExportApplyInstalls / SeExportGateFrame / SeExportQueueHwEvent)
//     and waits on the driver's reply counter, never on a clock, for the server to have handled
//     something. The clocks that remain are safety bounds that turn a hang into a failure.
//  2. A raw socket for what the driver cannot show: the byte length of a reply, the capability word.
//  3. The driver against a scripted server (FakeLiveServer.h): what it sends, to whom, and how it
//     reads a section it should tolerate or refuse.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

extern "C" {
#include "se_export.h"
void SeExportTestFillProfSlots(int fill);   // se_export.c, SE_EXPORT_TEST_HOOKS builds only
}
#include "FakeLiveServer.h"
#include "LiveDriver.h"
#include "SeHwEvents.h"
#include "SeLiveProtocol.h"
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

const char* const kLocal = SE_LIVE_DEFAULT_SOCK_PATH;
const char* const kTcp = "tcp:127.0.0.1:6957";

// What the glue's hook was told, in order (the test thread is the "emulate thread").
std::vector<unsigned> gCalls;
extern "C" void FakeSetHw(unsigned int mask) { gCalls.push_back(mask); }

template <class F>
bool Until(F pred, int budgetMs = 10000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (pred()) return true;
        SleepMs(1);
    }
    return pred();
}

bool OpenRetry(const char* endpoint, se_data_source* ds)
{
    for (int i = 0; i < 1000; ++i)
    {
        if (se_live_open(endpoint, ds) == SE_OK) return true;
        SleepMs(5);
    }
    return false;
}

void Close(se_data_source& ds)
{
    if (ds.close) ds.close(ds.user);
    ds = {};
}

uint64_t Replies(const se_data_source& ds)
{
    se_live_perf_status st = {};
    se_live_perf_status_get(&ds, &st);
    return st.replies;
}

// The server has answered 'n' more requests on each connection, so whatever it had been sent
// before this call has been handled.
void Settled(const std::vector<const se_data_source*>& clients, uint64_t n = 4)
{
    std::vector<uint64_t> target;
    for (const se_data_source* c : clients) target.push_back(Replies(*c) + n);
    CHECK(Until([&] {
        for (size_t i = 0; i < clients.size(); ++i)
            if (Replies(*clients[i]) < target[i]) return false;
        return true;
    }));
}

// The application boundary on the emulate thread.
bool ApplyUntil(unsigned want)
{
    return Until([&] { SeExportApplyInstalls(); return SeExportHwEventMask() == want; });
}

uint32_t CapsOf(const se_data_source& ds)
{
    uint32_t caps = 0;
    se_live_poke_info(&ds, nullptr, nullptr, nullptr, nullptr, &caps);
    return caps;
}

bool HasSection(const se_data_source& ds)
{
    se_live_hw_status st = {};
    return se_live_hw_status_get(&ds, &st) == 1;
}

void Queue(uint32_t tag)
{
    SeHwEvent e;
    std::memset(&e, 0, sizeof e);
    e.kind = SE_HWEV_K_VDP2_VBLANK;
    e.source = 5;
    e.cpu = SE_HWEV_CPU_NONE;
    e.flags = SE_HWEV_F_HAS_PC;
    e.frame = 0xDEADBEEFu;   // the exporter overwrites it
    e.ts = tag * 3u;
    e.pc = 0x06000000u + tag;
    e.arg[0] = tag; e.arg[1] = 0xA1u; e.arg[2] = 0xA2u; e.arg[3] = 0xA3u;
    SeExportQueueHwEvent(&e);
}

std::vector<se_live_hw_event> DrainAll(const se_data_source& ds)
{
    std::vector<se_live_hw_event> out;
    se_live_hw_event b[64];
    uint32_t n;
    while ((n = se_live_drain_hw_events(&ds, b, 64)) != 0) out.insert(out.end(), b, b + n);
    return out;
}

// Wait until 'total' events have been drained in all (returns them), or the budget runs out.
std::vector<se_live_hw_event> DrainUntil(const se_data_source& ds, size_t total, int budgetMs = 15000)
{
    std::vector<se_live_hw_event> out;
    Until([&] {
        const auto more = DrainAll(ds);
        out.insert(out.end(), more.begin(), more.end());
        return out.size() >= total;
    }, budgetMs);
    return out;
}

// ---- raw wire access ----
uint32_t Le32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint64_t Le64(const uint8_t* p) { return Le32(p) | (uint64_t(Le32(p + 4)) << 32); }

int RawConnect()
{
    for (int i = 0; i < 1000; ++i)
    {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        sockaddr_un a = {};
        a.sun_family = AF_UNIX;
        std::snprintf(a.sun_path, sizeof(a.sun_path), "%s", kLocal);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0)
        {
            timeval tv = {}; tv.tv_sec = 20;   // safety only: a hang becomes a failure
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            return fd;
        }
        ::close(fd);
        SleepMs(5);
    }
    return -1;
}

void RawSend(int fd, const char* verb, uint32_t arg)
{
    uint8_t req[SE_LIVE_REQUEST_LEN];
    std::memcpy(req, verb, 4);
    for (int i = 0; i < 4; ++i) req[4 + i] = static_cast<uint8_t>((arg >> (8 * i)) & 0xFF);
    CHECK(::send(fd, req, sizeof req, MSG_NOSIGNAL) == static_cast<ssize_t>(sizeof req));
}

// Sends the requests, then one more GET, and reads until that last reply has begun: the distance
// between the starts of consecutive replies is each reply's exact length.
std::vector<size_t> ReplyLengths(int fd, const std::vector<std::pair<const char*, uint32_t>>& reqs)
{
    for (const auto& r : reqs) RawSend(fd, r.first, r.second);
    RawSend(fd, SE_LIVE_VERB_GET, 0);
    uint8_t pat[8] = { SE_LIVE_MAGIC0, SE_LIVE_MAGIC1, SE_LIVE_MAGIC2, SE_LIVE_MAGIC3,
                       static_cast<uint8_t>(SE_LIVE_VERSION), 0, 0, 0 };
    std::vector<uint8_t> buf;
    std::vector<size_t> starts;
    size_t scanned = 0;
    uint8_t chunk[65536];
    while (starts.size() < reqs.size() + 1)
    {
        const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
        if (n <= 0) break;
        buf.insert(buf.end(), chunk, chunk + n);
        for (; scanned + 8 <= buf.size(); ++scanned)
            if (std::memcmp(buf.data() + scanned, pat, 8) == 0) starts.push_back(scanned);
    }
    std::vector<size_t> lens;
    for (size_t i = 0; i + 1 < starts.size(); ++i) lens.push_back(starts[i + 1] - starts[i]);
    CHECK(lens.size() == reqs.size());
    return lens;
}

// The capability word of a raw GET's reply (control block +52).
uint32_t RawCaps()
{
    const int fd = RawConnect();
    if (fd < 0) { CHECK(false); return 0xDEADu; }
    RawSend(fd, SE_LIVE_VERB_GET, 0);
    uint32_t caps = 0xDEADu;
    uint8_t hdr[SE_LIVE_HEADER_LEN];
    std::vector<uint8_t> sink;
    auto rd = [&](uint8_t* d, size_t n) {
        while (n) { const ssize_t r = ::recv(fd, d, n, 0); if (r <= 0) return false; d += r; n -= static_cast<size_t>(r); }
        return true;
    };
    if (rd(hdr, sizeof hdr))
    {
        size_t before = 0;   // v1 v2 cram vdp2struct vdp1regs wramLow wramHigh fb
        for (int i = 0; i < 8; ++i) before += Le32(hdr + 8 + 4 * i);
        const uint32_t ct = Le32(hdr + 40);
        std::vector<uint8_t> ctl(ct);
        sink.resize(before);
        if (ct >= 56 && rd(sink.data(), before) && rd(ctl.data(), ct)) caps = Le32(ctl.data() + 52);
    }
    ::close(fd);
    return caps;
}

// One frame as the glue plays it, without a snapshot: the gate, then the end of the frame.
void Frame()
{
    SeExportGateFrame();
    SeExportEndFrame();
}

struct Harness
{
    explicit Harness(bool hook, unsigned supported = 0xFFu)
    {
        gCalls.clear();
        CHECK(SeExportPrepare() == 0);
        if (hook) SeExportSetHwEventHook(FakeSetHw, supported);
        CHECK(SeExportStart() == 0);
    }
    ~Harness() { SeExportDeinit(); }
};

// ===================================================================================================
// 1. Replies of a connection that never sends HWE are what they were
// ===================================================================================================
void TestNotOptedIn()
{
    size_t baselineSteady = 0, baselineFirst = 0;
    {
        Harness h(false);
        const int fd = RawConnect();
        const auto lens = ReplyLengths(fd, { { SE_LIVE_VERB_GET, 0 }, { SE_LIVE_VERB_GET, 0 }, { SE_LIVE_VERB_GET, 0 } });
        ::close(fd);
        CHECK(lens.size() == 3);
        if (lens.size() == 3) { baselineFirst = lens[0]; baselineSteady = lens[2]; CHECK(lens[1] == lens[2]); }
    }
    Harness h(true);
    {
        // With the hook registered, a connection that does not send HWE gets the same bytes.
        const int fd = RawConnect();
        const auto lens = ReplyLengths(fd, { { SE_LIVE_VERB_GET, 0 }, { SE_LIVE_VERB_GET, 0 }, { SE_LIVE_VERB_GET, 0 } });
        ::close(fd);
        CHECK(lens.size() == 3);
        if (lens.size() == 3) CHECK(lens[0] == baselineFirst && lens[1] == baselineSteady && lens[2] == baselineSteady);
    }
    {
        // The reply to the first HWE already carries the section, and so does every later one,
        // whatever the verb, even when the mask is 0.
        const int fd = RawConnect();
        const auto lens = ReplyLengths(fd, { { SE_LIVE_VERB_HWEVENTS, 0 }, { SE_LIVE_VERB_GET, 0 },
                                             { SE_LIVE_VERB_PAUSE, 0 }, { SE_LIVE_VERB_RESUME, 0 } });
        ::close(fd);
        CHECK(lens.size() == 4);
        if (lens.size() == 4)
        {
            CHECK(lens[0] == baselineFirst + SE_LIVE_HWEV_HDR_LEN);
            CHECK(lens[1] == baselineSteady + SE_LIVE_HWEV_HDR_LEN);
            CHECK(lens[3] == baselineSteady + SE_LIVE_HWEV_HDR_LEN);
        }
    }
    // Opting in on one connection changes nothing for another.
    {
        const int fd = RawConnect();
        const auto lens = ReplyLengths(fd, { { SE_LIVE_VERB_GET, 0 }, { SE_LIVE_VERB_GET, 0 } });
        ::close(fd);
        CHECK(lens.size() == 2 && lens[1] == baselineSteady);
    }
}

// ===================================================================================================
// 2. Capability word
// ===================================================================================================
void TestCapability()
{
    {
        Harness h(true);
        CHECK((RawCaps() & SE_LIVE_CAP_HW_EVENTS) != 0);
    }
    {
        Harness h(false);
        CHECK((RawCaps() & SE_LIVE_CAP_HW_EVENTS) == 0);
        // The verb still opts the connection in, and a hook-less build applies nothing and
        // supports nothing.
        const int fd = RawConnect();
        RawSend(fd, SE_LIVE_VERB_HWEVENTS, 0xFF);
        ::close(fd);
        for (int i = 0; i < 20; ++i) SeExportApplyInstalls();
        CHECK(SeExportHwEventMask() == 0);
    }
    // And a restart forgets the hook of the run before.
    {
        Harness h(true);
    }
    {
        Harness h(false);
        CHECK((RawCaps() & SE_LIVE_CAP_HW_EVENTS) == 0);
    }
}

// ===================================================================================================
// 3. End to end
// ===================================================================================================
void TestMaskAndStream()
{
    Harness h(true, 0x7Fu);
    se_data_source a = {};
    CHECK(OpenRetry(kLocal, &a));
    CHECK(Until([&] { return se_live_server_version(&a) != 0; }));
    CHECK((CapsOf(a) & SE_LIVE_CAP_HW_EVENTS) != 0);

    // Nothing is opted in until something is wanted.
    Settled({ &a });
    CHECK(!HasSection(a));
    { se_live_hw_status st; st.newest_seq = 9; CHECK(se_live_hw_status_get(&a, &st) == 0 && st.newest_seq == 0); }

    // The request arrives (the section is there) but is applied only at a frame boundary.
    se_live_set_hw_events(&a, 0x13u | 0x80u);   // 0x80 is not in this build's supported set
    CHECK(Until([&] { return HasSection(a); }));
    Settled({ &a });
    for (int i = 0; i < 5; ++i) SeExportApplyInstallsMidFrame();
    CHECK(SeExportHwEventMask() == 0u);
    CHECK(gCalls.empty());
    { se_live_hw_status st = {}; CHECK(se_live_hw_status_get(&a, &st) == 1 && st.applied_mask == 0u && st.supported_mask == 0x7Fu); }
    SeExportApplyInstalls();
    CHECK(SeExportHwEventMask() == 0x13u);
    CHECK(gCalls.size() == 1 && gCalls[0] == 0x13u);
    SeExportApplyInstalls();   // nothing changed: the glue is not told again
    CHECK(gCalls.size() == 1);
    CHECK(Until([&] { se_live_hw_status st = {}; se_live_hw_status_get(&a, &st); return st.applied_mask == 0x13u; }));

    // The frame gate is a boundary too.
    se_live_set_hw_events(&a, 0x01u);
    Settled({ &a });
    CHECK(SeExportHwEventMask() == 0x13u);
    SeExportGateFrame();
    CHECK(SeExportHwEventMask() == 0x01u && gCalls.size() == 2 && gCalls[1] == 0x01u);

    // Events: stamped with the committed frame number as the trace path stamps it, in order, with
    // every field intact.
    Frame(); Frame();
    unsigned tl = 0; unsigned long long next = 0;
    SeExportFrameIdentity(&tl, &next);
    const uint32_t committed = static_cast<uint32_t>(next - 1);
    for (uint32_t i = 1; i <= 5; ++i) Queue(i);
    auto got = DrainUntil(a, 5);
    CHECK(got.size() == 5);
    for (size_t i = 0; i < got.size(); ++i)
    {
        const se_live_hw_event& e = got[i];
        CHECK(e.seq == i + 1 && e.lost_before == 0);
        CHECK(e.kind == SE_HWEV_K_VDP2_VBLANK && e.source == 5 && e.cpu == SE_HWEV_CPU_NONE && e.flags == SE_HWEV_F_HAS_PC);
        CHECK(e.frame == committed && e.ts == (i + 1) * 3 && e.pc == 0x06000000u + i + 1);
        CHECK(e.arg[0] == i + 1 && e.arg[1] == 0xA1u && e.arg[2] == 0xA2u && e.arg[3] == 0xA3u);
    }
    {
        se_live_hw_status st = {};
        CHECK(se_live_hw_status_get(&a, &st) == 1);
        CHECK(st.newest_seq == 5 && st.server_lost == 0 && st.driver_evicted == 0);
    }

    // Turning everything off stops the producer at once, before the boundary applies it.
    se_live_set_hw_events(&a, 0);
    Settled({ &a });
    Queue(99);
    SeExportGateFrame();
    CHECK(SeExportHwEventMask() == 0u && gCalls.back() == 0u);
    Settled({ &a });
    CHECK(DrainAll(a).empty());

    Close(a);
}

void TestUnionAndRelease()
{
    Harness h(true);
    se_data_source a = {}, b = {};
    CHECK(OpenRetry(kLocal, &a));
    CHECK(OpenRetry(kTcp, &b));
    se_live_set_hw_events(&a, 0x03u);
    se_live_set_hw_events(&b, 0x04u);
    CHECK(Until([&] { return HasSection(a) && HasSection(b); }));
    Settled({ &a, &b });
    CHECK(ApplyUntil(0x07u));

    // One leaves: its bits go with it.
    Close(a);
    CHECK(ApplyUntil(0x04u));
    // It comes back, asks again, and an abrupt (reset) disconnect releases it as well.
    {
        const int fd = RawConnect();
        RawSend(fd, SE_LIVE_VERB_HWEVENTS, 0x10u);
        CHECK(ApplyUntil(0x14u));
        linger lg = { 1, 0 };   // close with a reset, the way a killed process does
        ::setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
        ::close(fd);
        CHECK(ApplyUntil(0x04u));
    }
    // The TCP connection is a connection like the other.
    se_live_set_hw_events(&b, 0);
    Settled({ &b });
    CHECK(ApplyUntil(0u));
    // And the last one leaving with something on leaves nothing on.
    se_live_set_hw_events(&b, 0x20u);
    Settled({ &b });
    CHECK(ApplyUntil(0x20u));
    Close(b);
    CHECK(ApplyUntil(0u));
    CHECK(!gCalls.empty() && gCalls.back() == 0u);
}

// Direct section builder: exact sequences across the per-reply cap, exact lost count on overrun.
struct HwSection
{
    uint32_t count = 0, recLen = 0, applied = 0, supported = 0;
    uint64_t first = 0, newest = 0, lost = 0;
    std::vector<uint32_t> tags;   // arg[0] of each record
    size_t length = 0;
};

HwSection Build(SeExportHwCursor& cur)
{
    static std::vector<unsigned char> buf(SE_EXPORT_HW_SECTION_MAX);
    HwSection s;
    s.length = SeExportBuildHwSection(&cur, buf.data());
    CHECK(s.length >= SE_LIVE_HWEV_HDR_LEN);
    s.count = Le32(buf.data());
    s.recLen = Le32(buf.data() + 4);
    s.first = Le64(buf.data() + 8);
    s.newest = Le64(buf.data() + 16);
    s.lost = Le64(buf.data() + 24);
    s.applied = Le32(buf.data() + 32);
    s.supported = Le32(buf.data() + 36);
    CHECK(s.length == SE_LIVE_HWEV_HDR_LEN + static_cast<size_t>(s.count) * s.recLen);
    for (uint32_t i = 0; i < s.count; ++i) s.tags.push_back(Le32(buf.data() + SE_LIVE_HWEV_HDR_LEN + i * s.recLen + 16));
    return s;
}

bool Consecutive(const HwSection& s, uint32_t firstTag, uint32_t n)
{
    if (s.count != n || s.tags.size() != n) return false;
    for (uint32_t i = 0; i < n; ++i)
        if (s.tags[i] != firstTag + i) return false;
    return true;
}

void TestBuilder()
{
    Harness h(true, 0x3Fu);
    se_data_source a = {};
    CHECK(OpenRetry(kLocal, &a));
    se_live_set_hw_events(&a, 0x01u);
    CHECK(Until([&] { return HasSection(a); }));
    Settled({ &a });
    CHECK(ApplyUntil(0x01u));

    Queue(1); Queue(2);   // before this cursor existed: no backlog
    SeExportHwCursor cur = {};
    HwSection s = Build(cur);
    CHECK(s.count == 0 && s.recLen == SE_LIVE_HWEV_REC_LEN && s.lost == 0);
    CHECK(s.first == 3 && s.newest == 2);   // 'first' with nothing to send is the next sequence owed
    CHECK(s.applied == 0x01u && s.supported == 0x3Fu);

    // Across the per-reply cap: 1300 events arrive as 512, 512, 276, exactly consecutive, with the
    // rest deferred and never lost.
    for (uint32_t i = 3; i < 3 + 1300; ++i) Queue(i);
    s = Build(cur);
    CHECK(Consecutive(s, 3, 512) && s.first == 3 && s.lost == 0 && s.newest == 1302);
    CHECK(s.length == SE_EXPORT_HW_SECTION_MAX);
    s = Build(cur);
    CHECK(Consecutive(s, 515, 512) && s.first == 515 && s.lost == 0);
    s = Build(cur);
    CHECK(Consecutive(s, 1027, 276) && s.first == 1027 && s.lost == 0);
    s = Build(cur);
    CHECK(s.count == 0 && s.first == 1303 && s.newest == 1302 && s.lost == 0);

    // A cursor exactly the ring's depth behind loses nothing; one more loses exactly one.
    for (uint32_t i = 0; i < SE_HWEV_RING; ++i) Queue(2000 + i);
    s = Build(cur);
    CHECK(s.lost == 0 && s.first == 1303 && s.count == 512);
    for (uint32_t i = 0; i < 100; ++i) Queue(20000 + i);
    // The cursor stands at 1814 (1302 + 512). 9594 are produced, so the ring holds 1403..9594 and
    // 1815 is still in it: nothing lost.
    s = Build(cur);
    CHECK(s.lost == 0 && s.first == 1815);
    // A second connection was never behind: it starts at the newest and says nothing is lost.
    SeExportHwCursor fresh = {};
    s = Build(fresh);
    CHECK(s.count == 0 && s.lost == 0 && s.first == 9595);

    // Now overrun by exactly 7: 7 more than the ring can hold while 'fresh' is parked.
    for (uint32_t i = 0; i < SE_HWEV_RING + 7; ++i) Queue(40000 + i);
    s = Build(fresh);
    CHECK(s.lost == 7 && s.first == 9595 + 7 && s.count == 512);
    CHECK(s.tags.size() == 512 && s.tags[0] == 40007u);
    // A cursor that fell a long way behind is told how far, exactly.
    s = Build(cur);
    const uint64_t newest = s.newest;
    CHECK(newest == 9594 + SE_HWEV_RING + 7);
    CHECK(s.first == newest - SE_HWEV_RING + 1 && s.lost == (newest - SE_HWEV_RING) - 2326);   // the cursor stood at 1814 + 512
    Close(a);
}

// A producer far ahead of the reader: whatever is delivered plus whatever is reported lost is exactly
// what was produced, in order, and the loss lands on the record after the gap.
void TestOverrunEndToEnd()
{
    Harness h(true);
    se_data_source a = {};
    CHECK(OpenRetry(kLocal, &a));
    se_live_set_hw_events(&a, 0x01u);
    CHECK(Until([&] { return HasSection(a); }));
    Settled({ &a });
    CHECK(ApplyUntil(0x01u));
    const uint32_t total = 60000;
    for (uint32_t i = 1; i <= total; ++i) Queue(i);
    std::vector<se_live_hw_event> got;
    se_live_hw_status st = {};
    // Everything produced is accounted for once the stream has caught up with the newest sequence.
    Until([&] {
        const auto more = DrainAll(a);
        got.insert(got.end(), more.begin(), more.end());
        se_live_hw_status_get(&a, &st);
        return !got.empty() && got.back().seq == total;
    }, 30000);
    CHECK(!got.empty() && got.back().seq == total);
    uint64_t lostBefore = 0;
    bool ordered = true;
    for (size_t i = 0; i < got.size(); ++i)
    {
        lostBefore += got[i].lost_before;
        if (i && got[i].seq != got[i - 1].seq + 1 + got[i].lost_before) ordered = false;
        if (got[i].arg[0] != got[i].seq) ordered = false;   // the tag is the sequence here
    }
    CHECK(ordered);
    CHECK(st.server_lost > 0);                                 // 60000 into a ring of 8192
    CHECK(got.size() + st.server_lost == total);               // nothing unaccounted for
    CHECK(lostBefore == st.server_lost && st.driver_evicted == 0);
    Close(a);
}

// The driver's own queue (16384) overflows when nobody drains: the evictions are counted, and the
// record that follows the gap says how many.
void TestDriverEviction()
{
    Harness h(true);
    se_data_source a = {};
    CHECK(OpenRetry(kLocal, &a));
    se_live_set_hw_events(&a, 0x01u);
    CHECK(Until([&] { return HasSection(a); }));
    Settled({ &a });
    CHECK(ApplyUntil(0x01u));
    const uint32_t rounds = 6, per = 4096;   // 24576 delivered, 16384 kept
    for (uint32_t r = 0; r < rounds; ++r)
    {
        for (uint32_t i = 0; i < per; ++i) Queue(r * per + i + 1);
        Settled({ &a }, per / SE_LIVE_HWEV_MAX_PER_REPLY + 3);   // the server has sent all of it
    }
    se_live_hw_status st = {};
    CHECK(se_live_hw_status_get(&a, &st) == 1);
    CHECK(st.newest_seq == rounds * per && st.driver_evicted == 8192u && st.server_lost == 0);
    const auto got = DrainAll(a);
    CHECK(got.size() == 16384u);
    if (got.size() == 16384u)
    {
        CHECK(got.front().seq == 8193u && got.front().lost_before == 8192u);
        CHECK(got.back().seq == 24576u);
        bool clean = true;
        for (size_t i = 1; i < got.size(); ++i)
            if (got[i].lost_before != 0 || got[i].seq != got[i - 1].seq + 1) clean = false;
        CHECK(clean);
    }
    Close(a);
}

// A connection that opts in later gets only what happens after, never the ring's history.
void TestFreshConnectionNoBacklog()
{
    Harness h(true);
    se_data_source a = {}, b = {};
    CHECK(OpenRetry(kLocal, &a));
    se_live_set_hw_events(&a, 0x02u);
    CHECK(Until([&] { return HasSection(a); }));
    Settled({ &a });
    CHECK(ApplyUntil(0x02u));
    for (uint32_t i = 1; i <= 40; ++i) Queue(i);
    CHECK(DrainUntil(a, 40).size() == 40);

    CHECK(OpenRetry(kTcp, &b));
    se_live_set_hw_events(&b, 0x02u);
    CHECK(Until([&] { return HasSection(b); }));
    Settled({ &b });
    CHECK(DrainAll(b).empty());
    for (uint32_t i = 41; i <= 43; ++i) Queue(i);
    const auto got = DrainUntil(b, 3);
    CHECK(got.size() == 3);
    if (got.size() == 3) CHECK(got[0].seq == 41 && got[2].seq == 43 && got[0].lost_before == 0);
    { se_live_hw_status st = {}; CHECK(se_live_hw_status_get(&b, &st) == 1 && st.server_lost == 0); }
    // The first connection saw them too: two cursors, one ring.
    CHECK(DrainUntil(a, 3).size() == 3);

    // A reconnect starts the stream over: the sequence is the server's, and a second session on
    // the same server does not inherit the queue.
    Close(b);
    CHECK(OpenRetry(kTcp, &b));
    se_live_set_hw_events(&b, 0x02u);
    CHECK(Until([&] { return HasSection(b); }));
    Settled({ &b });
    CHECK(DrainAll(b).empty());
    Close(a);
    Close(b);
}

// ===================================================================================================
// 4. The driver against a scripted server
// ===================================================================================================
using fakelive::HwRec;
using fakelive::Reply;

struct Verbs
{
    std::mutex mu;
    std::vector<std::vector<std::pair<std::string, uint32_t>>> byConn;
    void Add(int conn, const char* verb, uint32_t arg)
    {
        std::lock_guard<std::mutex> lk(mu);
        if (byConn.size() <= static_cast<size_t>(conn)) byConn.resize(conn + 1);
        byConn[conn].push_back({ std::string(verb, 3), arg });
    }
    size_t Count(int conn, const char* verb)
    {
        std::lock_guard<std::mutex> lk(mu);
        size_t n = 0;
        if (byConn.size() > static_cast<size_t>(conn))
            for (const auto& v : byConn[conn]) if (v.first == std::string(verb, 3)) ++n;
        return n;
    }
    size_t Total(int conn)
    {
        std::lock_guard<std::mutex> lk(mu);
        return byConn.size() > static_cast<size_t>(conn) ? byConn[conn].size() : 0;
    }
    uint32_t ArgOf(int conn, const char* verb, size_t nth)
    {
        std::lock_guard<std::mutex> lk(mu);
        size_t n = 0;
        if (byConn.size() > static_cast<size_t>(conn))
            for (const auto& v : byConn[conn])
                if (v.first == std::string(verb, 3) && n++ == nth) return v.second;
        return 0xFFFFFFFFu;
    }
};

// A scripted server: connection 0 is the open-time probe; the handler decides each reply.
using Script = std::function<Reply(int conn, int nth, bool opted, const fakelive::Request& req)>;

struct Scripted
{
    Verbs verbs;
    std::atomic<int> hangupOn{ -1 };
    fakelive::Server server;
    explicit Scripted(Script script)
        : server([this, script](int fd, int index) {
              if (index == 0) return;
              bool opted = false;
              int nth = 0;
              fakelive::Request r;
              while (hangupOn.load() != index && fakelive::ReadRequest(fd, r))
              {
                  verbs.Add(index, r.verb, r.arg);
                  if (r.Is(SE_LIVE_VERB_HWEVENTS)) opted = true;
                  Reply rep = script(index, nth++, opted, r);
                  rep.hwSection = opted;
                  const std::vector<uint8_t> bytes = fakelive::Build(rep);
                  if (!fakelive::WriteExact(fd, bytes.data(), bytes.size())) return;
              }
          })
    {
    }
};

Reply Plain(uint32_t version, uint32_t caps)
{
    Reply r;
    r.version = version;
    r.caps = caps;
    return r;
}

HwRec Ev(uint32_t tag)
{
    HwRec h;
    h.kind = SE_HWEV_K_SCU_INT_MASK;
    h.arg[0] = tag;
    h.frame = tag + 100;
    return h;
}

bool WaitVerbs(const std::function<bool()>& pred) { return Until(pred); }

void TestScriptedGate()
{
    // A server without the capability never receives HWE, however long the client wants it. A GET
    // answers in its place, so the poll cadence continues and a reply with no section is parsed as one.
    {
        Scripted s([](int, int, bool, const fakelive::Request&) { return Plain(SE_LIVE_VERSION, 0); });
        CHECK(s.server.Start());
        se_data_source ds = {};
        CHECK(se_live_open(s.server.Endpoint().c_str(), &ds) == SE_OK);
        CHECK(WaitVerbs([&] { return s.verbs.Total(1) >= 2; }));
        se_live_set_hw_events(&ds, 0x0Fu);
        const size_t base = s.verbs.Total(1);
        CHECK(WaitVerbs([&] { return s.verbs.Total(1) >= base + 10; }));   // it kept polling: not blocked
        CHECK(s.verbs.Count(1, SE_LIVE_VERB_HWEVENTS) == 0);
        CHECK(!HasSection(ds) && DrainAll(ds).empty());
        Close(ds);
        s.server.Stop();
    }
    // An old protocol never receives it, even if (wrongly) it advertised the bit.
    {
        Scripted s([](int, int, bool, const fakelive::Request&) { return Plain(26, SE_LIVE_CAP_HW_EVENTS); });
        CHECK(s.server.Start());
        se_data_source ds = {};
        CHECK(se_live_open(s.server.Endpoint().c_str(), &ds) == SE_OK);
        se_live_set_hw_events(&ds, 0x0Fu);
        CHECK(WaitVerbs([&] { return s.verbs.Total(1) >= 12; }));
        CHECK(s.verbs.Count(1, SE_LIVE_VERB_HWEVENTS) == 0);
        CHECK(!HasSection(ds));
        Close(ds);
        s.server.Stop();
    }
    {
        Scripted s([](int, int, bool, const fakelive::Request&) { return Plain(20, 0); });
        CHECK(s.server.Start());
        se_data_source ds = {};
        CHECK(se_live_open(s.server.Endpoint().c_str(), &ds) == SE_OK);
        se_live_set_hw_events(&ds, 0x0Fu);
        CHECK(WaitVerbs([&] { return s.verbs.Total(1) >= 12; }));
        CHECK(s.verbs.Count(1, SE_LIVE_VERB_HWEVENTS) == 0);
        Close(ds);
        s.server.Stop();
    }
    // With the capability and the version, it is sent -- once per change, not per poll.
    {
        Scripted s([](int, int, bool, const fakelive::Request&) { return Plain(SE_LIVE_VERSION, SE_LIVE_CAP_HW_EVENTS); });
        CHECK(s.server.Start());
        se_data_source ds = {};
        CHECK(se_live_open(s.server.Endpoint().c_str(), &ds) == SE_OK);
        CHECK(WaitVerbs([&] { return s.verbs.Total(1) >= 2; }));
        CHECK(s.verbs.Count(1, SE_LIVE_VERB_HWEVENTS) == 0);   // wanting nothing, a connection never opts in
        se_live_set_hw_events(&ds, 0x21u);
        se_live_set_hw_events(&ds, 0x21u);   // restated every frame: not a change
        CHECK(WaitVerbs([&] { return s.verbs.Count(1, SE_LIVE_VERB_HWEVENTS) >= 1; }));
        CHECK(WaitVerbs([&] { return HasSection(ds); }));
        const size_t base = s.verbs.Total(1);
        se_live_set_hw_events(&ds, 0x21u);
        CHECK(WaitVerbs([&] { return s.verbs.Total(1) >= base + 6; }));
        CHECK(s.verbs.Count(1, SE_LIVE_VERB_HWEVENTS) == 1 && s.verbs.ArgOf(1, SE_LIVE_VERB_HWEVENTS, 0) == 0x21u);
        se_live_set_hw_events(&ds, 0);
        CHECK(WaitVerbs([&] { return s.verbs.Count(1, SE_LIVE_VERB_HWEVENTS) >= 2; }));
        CHECK(s.verbs.ArgOf(1, SE_LIVE_VERB_HWEVENTS, 1) == 0u);
        Close(ds);
        s.server.Stop();
    }
}

void TestScriptedRestate()
{
    Scripted s([](int, int, bool, const fakelive::Request&) { return Plain(SE_LIVE_VERSION, SE_LIVE_CAP_HW_EVENTS); });
    CHECK(s.server.Start());
    se_data_source ds = {};
    CHECK(se_live_open(s.server.Endpoint().c_str(), &ds) == SE_OK);
    se_live_set_hw_events(&ds, 0x05u);
    CHECK(WaitVerbs([&] { return s.verbs.Count(1, SE_LIVE_VERB_HWEVENTS) >= 1; }));
    CHECK(s.verbs.ArgOf(1, SE_LIVE_VERB_HWEVENTS, 0) == 0x05u);

    // The new connection has not been told: it is, once it has answered a GET.
    s.hangupOn.store(1);
    CHECK(WaitVerbs([&] { return s.verbs.Count(2, SE_LIVE_VERB_HWEVENTS) >= 1; }));
    CHECK(s.verbs.ArgOf(2, SE_LIVE_VERB_HWEVENTS, 0) == 0x05u);
    {
        std::lock_guard<std::mutex> lk(s.verbs.mu);
        CHECK(!s.verbs.byConn[2].empty() && s.verbs.byConn[2][0].first == "GET");   // handshake first
    }

    // Wanting nothing at the moment of a reconnect: the new connection never opts in.
    se_live_set_hw_events(&ds, 0);
    CHECK(WaitVerbs([&] { return s.verbs.Count(2, SE_LIVE_VERB_HWEVENTS) >= 2; }));
    s.hangupOn.store(2);
    CHECK(WaitVerbs([&] { return s.verbs.Total(3) >= 8; }));
    CHECK(s.verbs.Count(3, SE_LIVE_VERB_HWEVENTS) == 0);
    CHECK(!HasSection(ds));
    // And asking again on that connection opts it in.
    se_live_set_hw_events(&ds, 0x01u);
    CHECK(WaitVerbs([&] { return s.verbs.Count(3, SE_LIVE_VERB_HWEVENTS) >= 1; }));
    CHECK(WaitVerbs([&] { return HasSection(ds); }));
    Close(ds);
    s.server.Stop();
}

// Replies: the HWE reply carries 'first' records; later ones carry what the test assigns.
void TestScriptedParsing()
{
    std::atomic<int> phase{ 0 };
    Scripted s([&](int conn, int, bool opted, const fakelive::Request&) {
        Reply r = Plain(SE_LIVE_VERSION, SE_LIVE_CAP_HW_EVENTS);
        if (!opted || conn != 1) return r;
        r.hwApplied = 0x05u; r.hwSupported = 0xFFu;
        // Each phase is delivered once (the poll then sees idle replies).
        static std::atomic<int> sent{ -1 };
        const int p = phase.load();
        if (p != sent.load())
        {
            sent.store(p);
            switch (p)
            {
                case 1:   // a longer record than we know: read in full, tail ignored, stream stays aligned
                    r.hwRecLen = 40; r.hw = { Ev(1), Ev(2), Ev(3) };
                    r.hwFirstSeq = 1; r.hwNewest = 3;
                    break;
                case 2:   // the next reply, at the old length again, continues the sequence
                    r.hw = { Ev(4), Ev(5) };
                    r.hwFirstSeq = 4; r.hwNewest = 5;
                    break;
                case 3:   // the server dropped 14 records (6..19) before 20
                    r.hw = { Ev(20), Ev(21) };
                    r.hwFirstSeq = 20; r.hwNewest = 21; r.hwLost = 14;
                    break;
                case 4:   // a jump the server did not report is a loss all the same (a record this client could not read)
                    r.hw = { Ev(25) };
                    r.hwFirstSeq = 25; r.hwNewest = 25;
                    break;
                case 5:   // a duplicate is ignored
                    r.hw = { Ev(25), Ev(26) };
                    r.hwFirstSeq = 25; r.hwNewest = 26;
                    break;
                default: break;
            }
        }
        else
        {
            r.hwFirstSeq = r.hwNewest + 1;
        }
        return r;
    });
    CHECK(s.server.Start());
    se_data_source ds = {};
    CHECK(se_live_open(s.server.Endpoint().c_str(), &ds) == SE_OK);
    se_live_set_hw_events(&ds, 0x05u);
    CHECK(WaitVerbs([&] { return HasSection(ds); }));
    { se_live_hw_status st = {}; CHECK(se_live_hw_status_get(&ds, &st) == 1 && st.applied_mask == 0x05u && st.supported_mask == 0xFFu); }

    phase = 1;
    auto got = DrainUntil(ds, 3);
    CHECK(got.size() == 3);
    if (got.size() == 3)
    {
        CHECK(got[0].seq == 1 && got[1].seq == 2 && got[2].seq == 3 && got[0].lost_before == 0);
        CHECK(got[1].arg[0] == 2 && got[2].frame == 103 && got[2].kind == SE_HWEV_K_SCU_INT_MASK);
    }
    phase = 2;
    got = DrainUntil(ds, 2);
    CHECK(got.size() == 2);
    if (got.size() == 2) CHECK(got[0].seq == 4 && got[1].seq == 5 && got[0].arg[0] == 4 && got[1].lost_before == 0);
    CHECK(s.server.Accepted() == 2);   // the probe and one connection: nothing dropped it

    phase = 3;
    got = DrainUntil(ds, 2);
    CHECK(got.size() == 2);
    if (got.size() == 2) CHECK(got[0].seq == 20 && got[0].lost_before == 14 && got[1].lost_before == 0);
    { se_live_hw_status st = {}; se_live_hw_status_get(&ds, &st); CHECK(st.server_lost == 14 && st.newest_seq == 21); }

    phase = 4;
    got = DrainUntil(ds, 1);
    CHECK(got.size() == 1);
    if (got.size() == 1) CHECK(got[0].seq == 25 && got[0].lost_before == 3);   // 22, 23, 24
    { se_live_hw_status st = {}; se_live_hw_status_get(&ds, &st); CHECK(st.server_lost == 17); }

    phase = 5;
    got = DrainUntil(ds, 1);
    CHECK(got.size() == 1);
    if (got.size() == 1) CHECK(got[0].seq == 26 && got[0].lost_before == 0);
    CHECK(s.server.Accepted() == 2);
    Close(ds);
    s.server.Stop();
}

// A count or a record length this protocol could never produce is a broken stream: the connection
// is dropped, like any other malformed reply.
void TestScriptedBroken()
{
    struct Case { const char* name; uint32_t count; uint32_t recLen; };
    const Case cases[] = {
        { "count above the cap", SE_LIVE_HWEV_MAX_PER_REPLY + 1, 32 },
        { "record shorter than the known layout", 1, 31 },
        { "record longer than any", 1, 257 },
    };
    for (const Case& c : cases)
    {
        std::atomic<bool> poison{ false };
        Scripted s([&](int conn, int, bool opted, const fakelive::Request&) {
            Reply r = Plain(SE_LIVE_VERSION, SE_LIVE_CAP_HW_EVENTS);
            if (!opted) return r;
            if (conn == 1 && poison.load())
            {
                r.hw = { Ev(1) };
                r.hwCountField = c.count;
                r.hwRecLen = c.recLen;
                r.hwFirstSeq = 1; r.hwNewest = 1;
            }
            else
            {
                r.hwFirstSeq = 1;
            }
            return r;
        });
        CHECK(s.server.Start());
        se_data_source ds = {};
        CHECK(se_live_open(s.server.Endpoint().c_str(), &ds) == SE_OK);
        se_live_set_hw_events(&ds, 0x01u);
        CHECK(WaitVerbs([&] { return HasSection(ds); }));
        CHECK(s.server.Accepted() == 2);
        poison = true;
        const bool dropped = Until([&] { return s.server.Accepted() >= 3; });
        if (!dropped) std::printf("not dropped: %s\n", c.name);
        CHECK(dropped);
        poison = false;
        // The new connection restates the request and works.
        CHECK(WaitVerbs([&] { return s.verbs.Count(2, SE_LIVE_VERB_HWEVENTS) >= 1; }));
        CHECK(WaitVerbs([&] { return HasSection(ds); }));
        CHECK(DrainAll(ds).empty());
        Close(ds);
        s.server.Stop();
    }
}
// Every connection slot taken: the request of the connection that got none is never recorded, so the section
// has to say so (supportedMask 0) instead of leaving the client waiting for an applied mask.
void TestSlotExhaustion()
{
    Harness h(true);
    se_data_source a = {}, b = {};
    CHECK(OpenRetry(kLocal, &a));                 // takes a slot
    CHECK(Until([&] { return se_live_server_version(&a) != 0; }));   // served, so the slot is really taken
    SeExportTestFillProfSlots(1);                 // ...and nothing is left for the next one
    CHECK(OpenRetry(kTcp, &b));
    se_live_set_hw_events(&a, 0x01u);
    se_live_set_hw_events(&b, 0x02u);
    CHECK(Until([&] { return HasSection(a) && HasSection(b); }));
    Settled({ &a, &b });
    CHECK(ApplyUntil(0x01u));                     // only the connection with a slot counts
    CHECK(Until([&] { se_live_hw_status st = {}; return se_live_hw_status_get(&b, &st) == 1 && st.supported_mask == 0u; }));
    { se_live_hw_status st = {}; CHECK(se_live_hw_status_get(&a, &st) == 1 && st.supported_mask == 0xFFu); }
    // The cursor field is the whole mechanism: the builder reports supported 0 for it and nothing else changes.
    SeExportHwCursor cur;
    std::memset(&cur, 0, sizeof cur);
    cur.noSlot = 1;
    CHECK(Build(cur).supported == 0u);
    std::memset(&cur, 0, sizeof cur);
    CHECK(Build(cur).supported == 0xFFu);
    Close(b);
    SeExportTestFillProfSlots(0);
    Close(a);
}

// Preparing the exporter again must leave the glue and the exporter agreeing: the old hook is told "nothing
// wanted" (the emulator's own copy of the mask follows), and a hook registered after the second prepare
// receives requests again.
void TestRePrepare()
{
    {
        Harness h(true);
        se_data_source a = {};
        CHECK(OpenRetry(kLocal, &a));
        se_live_set_hw_events(&a, 0x13u);
        CHECK(Until([&] { return HasSection(a); }));
        Settled({ &a });
        CHECK(ApplyUntil(0x13u));
        Close(a);
        CHECK(ApplyUntil(0u));
        // Leave the glue "on" the way an applied mask would have: the next prepare must switch it off.
        SeExportApplyInstalls();
    }
    gCalls.clear();
    {
        CHECK(SeExportPrepare() == 0);
        CHECK(!gCalls.empty() && gCalls.back() == 0u);   // the previous run's hook heard "0" from Prepare
        SeExportSetHwEventHook(FakeSetHw, 0xFFu);         // the glue registers again
        CHECK(SeExportStart() == 0);
        gCalls.clear();
        se_data_source a = {};
        CHECK(OpenRetry(kLocal, &a));
        se_live_set_hw_events(&a, 0x13u);
        CHECK(Until([&] { return HasSection(a); }));
        Settled({ &a });
        CHECK(ApplyUntil(0x13u));
        CHECK(!gCalls.empty() && gCalls.back() == 0x13u);
        Close(a);
        SeExportDeinit();
    }
}
}  // namespace

int main()
{
    TestSlotExhaustion();
    TestRePrepare();
    TestNotOptedIn();
    TestCapability();
    TestMaskAndStream();
    TestUnionAndRelease();
    TestBuilder();
    TestOverrunEndToEnd();
    TestDriverEviction();
    TestFreshConnectionNoBacklog();
    TestScriptedGate();
    TestScriptedRestate();
    TestScriptedParsing();
    TestScriptedBroken();
    if (gFailures) { std::printf("LiveHwEventTests: %d failure(s)\n", gFailures); return 1; }
    std::printf("LiveHwEventTests passed\n");
    return 0;
}
