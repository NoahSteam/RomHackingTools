// The Profiler's Memory tab and the machine-state timeline (plan item F10, the follow-up to its
// "a capture can trail the profiler's newest frame" limitation).
//
// The tracker is fed bytes from the snapshot a live client captured, and must be told which timeline
// those bytes belong to. The perf stream names a timeline too, but it is delivered by another route:
// the server drains perf records from their own queue into every reply, while the snapshot in that
// reply is the OLDEST ring slot this connection has not been served. After a state load a lagging
// client therefore holds, for a few replies, old-timeline bytes next to new-timeline perf records,
// and a native load (SeExportNotifyStateLoaded) even leaves the old timeline's slots in the ring.
// A tracker told the stream's timeline files old bytes under the new timeline; the next capture is
// then compared with them and the whole RAM reads as changed.
//
// Here the real exporter (se_export.c) and the real live driver run over a socket, with the test
// standing in for the emulate thread (memory contents that differ per timeline) and for App (the
// same capture -> LivePoolSource -> BeginSample -> Sample sequence App::UpdateProfiler runs), and
// every ingested sample is judged against the ground truth: the timeline its bytes really are of.
//
// The straddle is CONSTRUCTED, not hoped for: the driver talks to the exporter through a relay owned
// by the test (GatedRelay) that holds each request of the driver until the test lets it through. While
// a request is held the driver is idle, so the emulator burst "frames of the old timeline, native load,
// frames of the new one" is fully published before the server can answer, and the reply that follows
// is, by the exporter's own oldest-unseen rule, an old-timeline slot beside new-timeline perf records.
// Nothing here depends on how the scheduler interleaves the threads; timeouts only turn a hang into a
// failure.
//
//   ./SaturnExplorerMemoryTimelineTests           the fixed wiring (must pass)
//   SE_TEST_LEGACY_WIRING=1 ./...                 the wiring before the fix (the tracker is told the
//                                                 perf stream's timeline); it must FAIL, which shows
//                                                 the race is real and the test can see it
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

extern "C" {
#include "se_export.h"
}
#include "Debug/MemoryBackend.h"
#include "Debug/ProfilerModel.h"
#include "LiveCapture.h"
#include "LiveDriver.h"
#include "SeLiveProtocol.h"
#include "SePerfCounters.h"
#include "saturnexplorer/SaturnExplorer.h"

using namespace sfe;

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what)
{
    if (ok) return;
    std::cerr << "FAIL: " << what << '\n';
    ++gFailures;
}
void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

const uint32_t kHw = 0x06000000u, kSize = 0x00100000u;

// What high work RAM holds on timeline 't' (big-endian, as the Saturn has it): every 1 KiB block is
// non-uniform and differs from the other timeline's, and byte 0 is the timeline's tag, which is how a
// sample's bytes say which timeline they really are of -- the ground truth the tracker is judged against.
std::vector<uint8_t> Contents(uint32_t t)
{
    std::vector<uint8_t> be(kSize);
    for (uint32_t i = 0; i < kSize; ++i) be[i] = static_cast<uint8_t>(t * 37u + i * 5u + (i >> 10) * 11u + 1u);
    be[0] = static_cast<uint8_t>(t);
    return be;
}
// The exporter is handed work RAM in the emulator's host order; the driver swaps it back.
std::vector<uint8_t> HostOrder(std::vector<uint8_t> be)
{
    for (size_t i = 0; i + 1 < be.size(); i += 2) std::swap(be[i], be[i + 1]);
    return be;
}

// ---- The emulate thread's side ----------------------------------------------------------------
uint32_t gTimeline = 1;                    // what the exporter's timeline is (SeExportFrameIdentity), mirrored
std::vector<uint8_t> gHostRam;             // contents of the current timeline, host order

void SetTimeline(uint32_t t) { gTimeline = t; gHostRam = HostOrder(Contents(t)); }

// One emulated frame: the gate, the end-of-frame snapshot, the record (identity stamped by the producer),
// the end-of-frame bookkeeping.
void EmulatedFrame()
{
    while (!SeExportGateFrame()) { }
    SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, gHostRam.data(), nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr);
    unsigned long long c[SE_PERF_COUNT] = {};
    SeExportPerfInfo info;
    std::memset(&info, 0, sizeof info);
    SeExportFrameIdentity(&info.timelineId, &info.frameNo);
    info.valid = SE_PERF_VALID_IDENTITY;
    SeExportPublishPerf(c, SE_PERF_COUNT, nullptr, nullptr, 0, 0, &info);
    SeExportEndFrame();
}
// A native state load (the emulator's own hotkey): the timeline changes, the ring is NOT emptied.
void NativeLoad(uint32_t newTimeline)
{
    SeExportNotifyStateLoaded();
    SetTimeline(newTimeline);
}


// ---- A relay between the driver and the exporter that lets requests through one at a time ----------
// Bytes flow unchanged in both directions (the driver parses the real exporter's real replies); only
// the driver's REQUESTS wait at the gate. A payload verb would be forwarded with its payload.
class GatedRelay
{
public:
    ~GatedRelay() { Stop(); }
    bool Start()
    {
        mListen = ::socket(AF_INET, SOCK_STREAM, 0);
        if (mListen < 0) return false;
        int on = 1;
        ::setsockopt(mListen, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (::bind(mListen, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || ::listen(mListen, 8) != 0) return false;
        socklen_t len = sizeof(a);
        ::getsockname(mListen, reinterpret_cast<sockaddr*>(&a), &len);
        mPort = ntohs(a.sin_port);
        mRunning = true;
        mAcceptThread = std::thread([this] {
            while (mRunning)
            {
                const int client = ::accept(mListen, nullptr, nullptr);
                if (client < 0) continue;
                if (!mRunning) { ::close(client); break; }
                const int server = ::socket(AF_INET, SOCK_STREAM, 0);
                sockaddr_in s = {};
                s.sin_family = AF_INET;
                s.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                s.sin_port = htons(SE_LIVE_DEFAULT_TCP_PORT);
                if (server < 0 || ::connect(server, reinterpret_cast<sockaddr*>(&s), sizeof(s)) != 0)
                {
                    if (server >= 0) ::close(server);
                    ::close(client);
                    continue;
                }
                std::lock_guard<std::mutex> lk(mMtx);
                mFds.push_back(client);
                mFds.push_back(server);
                mThreads.emplace_back([this, client, server] { Requests(client, server); });
                mThreads.emplace_back([this, client, server] { Replies(client, server); });
            }
        });
        return true;
    }
    void Stop()
    {
        if (!mRunning.exchange(false)) return;
        {
            std::lock_guard<std::mutex> lk(mMtx);
            mOpen = true;
            for (int fd : mFds) ::shutdown(fd, SHUT_RDWR);
        }
        mCv.notify_all();
        const int wake = ::socket(AF_INET, SOCK_STREAM, 0);
        if (wake >= 0)
        {
            sockaddr_in a = {};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = htons(static_cast<uint16_t>(mPort));
            (void)::connect(wake, reinterpret_cast<sockaddr*>(&a), sizeof(a));
            ::close(wake);
        }
        if (mAcceptThread.joinable()) mAcceptThread.join();
        for (std::thread& t : mThreads) if (t.joinable()) t.join();
        for (int fd : mFds) ::close(fd);
        mFds.clear();
        ::close(mListen);
    }
    std::string Endpoint() const { return "tcp:127.0.0.1:" + std::to_string(mPort); }

    // Close the gate: from now on a request waits for a permit.
    void Hold() { std::lock_guard<std::mutex> lk(mMtx); mOpen = false; }
    void Open() { { std::lock_guard<std::mutex> lk(mMtx); mOpen = true; } mCv.notify_all(); }
    // Let exactly one waiting (or the next) request through.
    void Permit() { { std::lock_guard<std::mutex> lk(mMtx); ++mPermits; } mCv.notify_all(); }
    // Block until a request is waiting at the closed gate: the driver has finished its previous reply
    // and is idle, and the server will not answer anything until a permit is given.
    bool WaitHeld(int ms = 15000)
    {
        std::unique_lock<std::mutex> lk(mMtx);
        return mCv.wait_for(lk, std::chrono::milliseconds(ms), [this] { return mWaiting > 0 && mPermits == 0; });
    }

private:
    static bool Full(int fd, void* d, size_t n)
    {
        uint8_t* p = static_cast<uint8_t*>(d);
        while (n)
        {
            const ssize_t r = ::recv(fd, p, n, 0);
            if (r <= 0) return false;
            p += r; n -= static_cast<size_t>(r);
        }
        return true;
    }
    static bool Send(int fd, const void* d, size_t n)
    {
        const uint8_t* p = static_cast<const uint8_t*>(d);
        while (n)
        {
            const ssize_t w = ::send(fd, p, n, MSG_NOSIGNAL);
            if (w <= 0) return false;
            p += w; n -= static_cast<size_t>(w);
        }
        return true;
    }
    void Requests(int client, int server)
    {
        uint8_t h[SE_LIVE_REQUEST_LEN];
        while (Full(client, h, sizeof(h)))
        {
            const uint32_t arg = h[4] | (h[5] << 8) | (h[6] << 16) | (static_cast<uint32_t>(h[7]) << 24);
            size_t payload = 0;
            if (!std::memcmp(h, SE_LIVE_VERB_BKPTS, 4)) payload = size_t(arg) * SE_LIVE_BKPT_DESC_LEN;
            else if (!std::memcmp(h, SE_LIVE_VERB_TRACE, 4)) payload = size_t(arg) * SE_LIVE_TRACE_DESC_LEN;
            else if (!std::memcmp(h, SE_LIVE_VERB_WRITE, 4) || !std::memcmp(h, SE_LIVE_VERB_WRITESND, 4) ||
                     !std::memcmp(h, SE_LIVE_VERB_LOADSTATE, 4)) payload = arg;
            std::vector<uint8_t> extra(payload);
            if (payload && !Full(client, extra.data(), payload)) break;
            {
                std::unique_lock<std::mutex> lk(mMtx);
                ++mWaiting;
                mCv.notify_all();
                mCv.wait(lk, [this] { return mOpen || mPermits > 0 || !mRunning; });
                --mWaiting;
                if (!mOpen && mPermits > 0) --mPermits;
            }
            if (!Send(server, h, sizeof(h)) || (payload && !Send(server, extra.data(), payload))) break;
        }
        ::shutdown(server, SHUT_RDWR);
    }
    void Replies(int client, int server)
    {
        uint8_t buf[65536];
        for (;;)
        {
            const ssize_t r = ::recv(server, buf, sizeof(buf), 0);
            if (r <= 0 || !Send(client, buf, static_cast<size_t>(r))) break;
        }
        ::shutdown(client, SHUT_RDWR);
    }

    int mListen = -1;
    uint16_t mPort = 0;
    std::atomic<bool> mRunning{ false };
    std::thread mAcceptThread;
    std::vector<std::thread> mThreads;
    std::vector<int> mFds;
    std::mutex mMtx;
    std::condition_variable mCv;
    bool mOpen = true;
    int mPermits = 0;
    int mWaiting = 0;
};

// ---- App's side ---------------------------------------------------------------------------------
bool gLegacy = false;

struct Rig
{
    se_data_source ds{};
    se_context* ctx = nullptr;
    CaptureRepeatTracker captures;
    ContextBackend backend{ &ctx };
    ProfilerModel model;
    MemoryPoolTracker pools;

    struct Sample
    {
        uint32_t trueTimeline;     // what the bytes are of
        uint32_t trackerTimeline;  // what the tracker filed them under
        uint32_t changed;          // changed blocks it reported
        uint32_t streamTimeline;   // what the perf stream said at that moment
    };
    std::vector<Sample> samples;
    int unstamped = 0;             // captures the server did not stamp with a timeline
    int straddles = 0;             // steps whose capture and perf stream were on different timelines
    uint32_t lastCapturedTag = 0;

    bool Open(const std::string& endpoint)
    {
        se_result r = SE_ERR_IO;
        for (int i = 0; i < 400 && r != SE_OK; ++i)
        {
            r = se_live_open(endpoint.c_str(), &ds);
            if (r != SE_OK) Sleep(5);
        }
        if (r != SE_OK) return false;
        se_config cfg;
        cfg.abi_version = SE_ABI_VERSION;
        cfg.reserved = 0;
        ctx = se_create(&ds, &cfg);
        if (!ctx) return false;
        for (int i = 0; i < 2000 && se_live_server_version(&ds) == 0; ++i) Sleep(2);
        return true;
    }
    ~Rig() { if (ctx) se_destroy(ctx); }

    // App::UpdateProfiler's perf drain, as far as identity goes.
    void DrainPerf()
    {
        se_live_perf_frame batch[32];
        uint32_t n;
        while ((n = se_live_drain_perf(&ds, batch, 32)) != 0)
            for (uint32_t i = 0; i < n; ++i)
            {
                const se_live_perf_frame& in = batch[i];
                PerfFrame f;
                f.seq = in.seq; f.frame = in.frame; f.wireVersion = in.wire_version; f.valid = in.valid;
                f.sampleSeq = in.sample_seq; f.timelineId = in.timeline_id; f.frameNo = in.frame_no;
                f.gapOnly = in.gap_only != 0; f.newestSeq = in.newest_seq;
                model.Push(std::move(f));
            }
    }

    // One UI frame: drain, capture, and the Memory tab's sample (every call samples; App's every-tenth
    // countdown only thins them out).
    void Step()
    {
        DrainPerf();
        CaptureLiveFrame(captures, ctx, &ds);
        uint32_t streamTimeline = 0;
        uint64_t streamFrame = 0;
        model.LatestIdentity(&streamTimeline, &streamFrame);
        bool accept;
        PoolCaptureSource captureSrc;
        if (gLegacy)
        {
            // The wiring before the fix: the tracker is told the perf stream's timeline.
            pools.SetSource(backend.SourceId(), 1, streamTimeline, 0);
            accept = true;
        }
        else
        {
            const PoolCaptureSource src = LivePoolSource(&ds, backend.SourceId(), 1, streamTimeline);
            captureSrc = src;
            accept = pools.BeginSample(src);
        }
        std::vector<uint8_t> bytes;
        if (!ReadRegionBytes(backend, kHw, kSize, bytes)) return;
        lastCapturedTag = bytes[0];
        if (!gLegacy && bytes[0] != 0 && captureSrc.captureTimeline == 0) ++unstamped;   // a real capture the server did not stamp
        if (streamTimeline != 0 && bytes[0] != streamTimeline) ++straddles;
        if (!accept) return;
        pools.Sample(0, "High work RAM", kHw, bytes.data(), kSize);
        samples.push_back({ bytes[0], pools.Timeline(), pools.Pool(0).changedBlocks, streamTimeline });
    }
};

// Step until the display holds timeline 't's bytes at the newest frame, or time runs out.
bool CatchUp(Rig& rig, uint32_t t)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(15000);
    int quiet = 0;
    while (std::chrono::steady_clock::now() < deadline)
    {
        rig.Step();
        if (rig.lastCapturedTag == t && se_live_capture_pending(&rig.ds) == 0 && ++quiet >= 3) return true;
        if (rig.lastCapturedTag != t) quiet = 0;
        Sleep(2);
    }
    return false;
}
}  // namespace

int main()
{
    gLegacy = std::getenv("SE_TEST_LEGACY_WIRING") != nullptr;
    if (SeExportInit() != 0) { std::cerr << "SeExportInit failed\n"; return 1; }
    SetTimeline(1);
    GatedRelay relay;
    if (!relay.Start()) { std::cerr << "relay failed\n"; SeExportDeinit(); return 1; }
    Rig rig;
    if (!rig.Open(relay.Endpoint())) { std::cerr << "could not open the live source\n"; SeExportDeinit(); return 1; }

    for (int i = 0; i < 3; ++i) EmulatedFrame();
    Check(CatchUp(rig, 1), "the client catches up on timeline 1");
    Check(!rig.samples.empty() && rig.samples.back().trueTimeline == 1 && rig.samples.back().trackerTimeline == 1,
          "a sample of timeline 1 is filed under timeline 1");
    Check(rig.samples.back().changed == 0, "nothing changes while the machine is on one timeline");

    // Repeated native loads. Each: hold the driver's next request at the relay (the driver is then idle and the
    // server cannot answer), publish the burst -- two more frames of the old timeline, the load, two frames of
    // the new one -- and only then let requests through one reply at a time, stepping the client after each.
    // The first reply is the oldest unserved slot (old timeline) with the new timeline's perf records.
    const int kTrials = 12;
    int straddlingTrials = 0;
    for (int trial = 0; trial < kTrials; ++trial)
    {
        const uint32_t to = gTimeline + 1;
        relay.Hold();
        Check(relay.WaitHeld(), "the driver's next request is waiting at the relay");
        EmulatedFrame();
        EmulatedFrame();
        NativeLoad(to);
        EmulatedFrame();
        EmulatedFrame();
        const int before = rig.straddles;
        for (int reply = 0; reply < 3; ++reply)
        {
            se_live_perf_status st = {};
            se_live_perf_status_get(&rig.ds, &st);
            const uint64_t target = st.replies + 1;
            relay.Permit();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(15000);
            while (std::chrono::steady_clock::now() < deadline &&
                   !(se_live_perf_status_get(&rig.ds, &st) && st.replies >= target && st.queued_seq >= SeExportPerfNewestSeq()))
                Sleep(1);
            Check(st.replies >= target, "the released request was answered");
            relay.WaitHeld();   // the driver sent its next request: this reply is fully processed
            rig.Step();
        }
        if (rig.straddles > before) ++straddlingTrials;
        relay.Open();
        Check(CatchUp(rig, to), "the client catches up on the new timeline");
    }
    Check(straddlingTrials == kTrials, "every trial held an old-timeline capture beside a new-timeline perf stream");

    // ---- the verdict --------------------------------------------------------------------------------
    int wrongTimeline = 0, spikes = 0;
    for (const Rig::Sample& s : rig.samples)
    {
        if (s.trueTimeline != s.trackerTimeline) ++wrongTimeline;
        if (s.changed != 0) ++spikes;
    }
    std::printf("samples %zu, straddling steps %d, filed under the wrong timeline %d, false change spikes %d\n",
                rig.samples.size(), rig.straddles, wrongTimeline, spikes);
    Check(gLegacy || rig.unstamped == 0, "the exporter stamps every snapshot with its timeline (an unstamped capture would only be waited out, not sampled)");
    Check(rig.straddles > 0,
          "the scenario produced a capture and a perf stream on different timelines (else this test proves nothing)");
    Check(wrongTimeline == 0, "no sample is filed under a timeline its bytes are not of");
    Check(spikes == 0, "no sample reports changed blocks: the memory is constant within a timeline, so a change is a cross-timeline delta");
    Check(rig.pools.Run() >= 2, "a timeline change starts a new run");

    relay.Stop();
    SeExportDeinit();
    if (gFailures) { std::cerr << gFailures << " failure(s)\n"; return 1; }
    std::cout << "MemoryTimelineTests: all passed\n";
    return 0;
}
