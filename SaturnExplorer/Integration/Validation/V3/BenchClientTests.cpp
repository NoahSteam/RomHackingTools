// V3 harness: the benchmark client against the REAL exporter (se_export.c) and the real driver,
// with a stand-in emulator thread that publishes frames the way the Mednafen glue does. No BIOS.
//
// What is asserted: the CSV rows (sequence, frame number, timeline, mode, loss counts, arrival
// time), that the attribution on/off request reaches the emulate side and is gone when the client
// leaves, that the bracket mode holds no connection inside its window, that a lost range makes
// the run invalid with the exact count, that a slot load reaches the emulator, and the same through
// the real executable (argv[1]) with its exit codes. It says NOTHING about emulator overhead.
//
// Nothing here depends on how many frames per second the host manages. Windows are counted in frames
// (the client's --frames / --warmup-frames), progress is awaited by published-frame count, and every
// assertion is relative to what the stand-in actually published; the only time values are timeouts that
// turn a hang into a failure, and lower bounds on sleeps. V3_TEST_FRAME_MS=40 (25 fps) must pass too.
//
// Socket and port of their own: 6953 and /tmp/saturn_explorer_v3_bench_test.sock.
#include <atomic>
#include <deque>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
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
#include "BenchClient.h"
#include "LiveDriver.h"
#include "SeLiveProtocol.h"
#include "SePerfCounters.h"

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

std::atomic<unsigned> gHookFlags{ 0 };
std::atomic<int> gHookCalls{ 0 };
extern "C" void FakeSetProfiler(unsigned int flags) { gHookFlags = flags; ++gHookCalls; }
std::atomic<int> gSlotLoads{ 0 };
std::atomic<int> gSlotLoaded{ -1 };
// The savestate the exporter takes per frame while a client wants rewind capture (REW 1, the default
// of every new connection); a client that sent REW 0 stops it. Counting these is how the stand-in
// sees whether rewind capture is on.
std::atomic<int> gSaves{ 0 };
extern "C" size_t FakeSave(unsigned char* buf, size_t cap)
{
    ++gSaves;
    if (!buf) return 64;
    if (cap < 64) return 0;
    std::memset(buf, 0xA5, 64);
    return 64;
}
extern "C" int FakeLoad(const unsigned char*, size_t) { return 0; }
extern "C" int FakeSlotInfo(unsigned int, unsigned long long* mtime) { *mtime = 1; return 1; }
extern "C" int FakeSlotLoad(unsigned int slot)
{
    gSlotLoaded = static_cast<int>(slot);
    ++gSlotLoads;
    SeExportNotifyStateLoaded();
    return 0;
}

// The stand-in emulator: one frame every gFrameMs, each stamped with the producer's identity and the
// attribution mode in force. gPublished counts them; tests wait on that count, never on the clock.
// A burst is published at a chosen frame count and, to take microseconds whatever the host, carries no
// snapshot (the ring overrun it provokes is in the perf records, not the VRAM copy).
std::atomic<bool> gStop{ false };
std::atomic<unsigned long long> gPublished{ 0 };
std::atomic<unsigned long long> gBurstAt{ 0 };   // publish a burst once gPublished reaches this (0 = never)
std::atomic<int> gBurstSize{ 0 };
int gFrameMs = 4;
// A real emulator applies a profiler request at a frame boundary some frames after it arrives. gModeLag models
// that: each frame is stamped with the mode that was in force gModeLag frames earlier. gMismatchAt/Count stamp
// the wrong mode (0) on that many consecutive frames starting at that published count, once.
std::atomic<int> gModeLag{ 0 };
std::atomic<unsigned long long> gMismatchAt{ 0 };
std::atomic<int> gMismatchCount{ 0 };
void PublishOne(bool snapshot)
{
    static std::deque<uint32_t> hist;   // the emulate thread only
    SeExportGateFrame();
    SeExportPerfInfo info;
    std::memset(&info, 0, sizeof info);
    SeExportFrameIdentity(&info.timelineId, &info.frameNo);
    info.valid = SE_PERF_VALID_IDENTITY | SE_PERF_VALID_MODE;
    hist.push_back(SeExportProfilerFlags());
    while (hist.size() > static_cast<size_t>(gModeLag.load()) + 1) hist.pop_front();
    info.mode = hist.front();
    const unsigned long long mm = gMismatchAt.load();
    if (mm != 0 && gPublished.load() >= mm)
    {
        if (gMismatchCount.fetch_sub(1) > 0) info.mode = 0; else gMismatchAt = 0;
    }
    unsigned long long c[SE_PERF_COUNT] = {};
    SeExportPublishPerf(c, SE_PERF_COUNT, nullptr, nullptr, 0, 0, &info);
    if (snapshot)
        SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr, nullptr, nullptr, nullptr, nullptr);
    SeExportEndFrame();
    ++gPublished;
}
void Emulator()
{
    while (!gStop.load())
    {
        const unsigned long long at = gBurstAt.load();
        if (at != 0 && gPublished.load() >= at)
        {
            gBurstAt = 0;
            for (int n = gBurstSize.load(); n > 0; --n) PublishOne(false);
        }
        PublishOne(true);
        SleepMs(gFrameMs);
    }
}
// Blocks until the stand-in has published 'n' more frames. The bound only turns a hang into a failure.
bool WaitFrames(unsigned long long n, int boundSeconds = 120)
{
    const unsigned long long target = gPublished.load() + n;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(boundSeconds);
    while (gPublished.load() < target)
    {
        if (std::chrono::steady_clock::now() > deadline) return false;
        SleepMs(1);
    }
    return true;
}

struct Csv
{
    std::vector<std::map<std::string, std::string>> rows;
    uint64_t U(size_t i, const char* col) const { return std::strtoull(rows[i].at(col).c_str(), nullptr, 10); }
    int64_t I(size_t i, const char* col) const { return std::strtoll(rows[i].at(col).c_str(), nullptr, 10); }
};
Csv ReadCsv(const std::string& path)
{
    Csv c;
    std::ifstream f(path);
    std::string line;
    std::vector<std::string> head;
    bool first = true;
    while (std::getline(f, line))
    {
        std::vector<std::string> cells;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) cells.push_back(cell);
        if (first) { head = cells; first = false; continue; }
        std::map<std::string, std::string> row;
        for (size_t i = 0; i < head.size() && i < cells.size(); ++i) row[head[i]] = cells[i];
        c.rows.push_back(row);
    }
    return c;
}
std::map<std::string, std::string> ReadSummary(const std::string& path)
{
    std::map<std::string, std::string> m;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
    {
        const size_t eq = line.find('=');
        if (eq != std::string::npos) m[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return m;
}

const char* const kSock = SE_LIVE_DEFAULT_SOCK_PATH;
std::string gTmp;

const int64_t kWarmFrames = 10;
const int64_t kWindowFrames = 80;

// Count-driven options: warm up 'kWarmFrames' frames, measure 'frames' frames. The timeout is a safety bound.
v3::BenchOptions Base(const std::string& name, int64_t frames = kWindowFrames)
{
    v3::BenchOptions o;
    o.endpoint = kSock;
    o.frames = frames;
    o.warmupFrames = kWarmFrames;
    o.warmupS = 0.0;
    o.durationS = 0.3;   // bracket only: the minimum gap between its two probes
    o.timeoutS = 30.0;
    o.drainMs = 8;
    o.connectTimeoutS = 30;
    o.csvPath = gTmp + name + ".csv";
    o.summaryPath = gTmp + name + ".summary";
    o.tag = name;
    return o;
}

// Newest frame number among rows that arrived at or before 't' (the analyzer's f(t)).
int64_t FrameAt(const Csv& c, int64_t t)
{
    int64_t f = -1;
    for (size_t i = 0; i < c.rows.size(); ++i)
        if (c.I(i, "arrival_ns") <= t && c.I(i, "frame_no") >= 0) f = c.I(i, "frame_no");
    return f;
}

// A clean stream run, judged against what was asked and what the stand-in published, never against a rate:
// every row is a frame, consecutive in sequence and frame number, one timeline, nothing lost; the rows are
// exactly the frames from the first to the last one seen; the window holds at least the requested frames and
// the CSV agrees with the summary about how many; the warm-up was at least the requested frames; every
// frame from the window start on ran in the requested attribution mode.
void CheckFrames(const Csv& c, const std::map<std::string, std::string>& s, uint32_t wantMode,
                 unsigned long long publishedDuringRun, int64_t wantFrames = kWindowFrames)
{
    const int64_t ws = std::atoll(s.at("window_start_ns").c_str());
    const int64_t we = std::atoll(s.at("window_end_ns").c_str());
    uint64_t prevSeq = 0;
    int64_t prevFrame = -1, prevArrival = -1;
    uint32_t timeline = 0;
    for (size_t i = 0; i < c.rows.size(); ++i)
    {
        CHECK(c.rows[i].at("kind") == "frame");
        const uint64_t seq = c.U(i, "sample_seq");
        const int64_t fr = c.I(i, "frame_no");
        const int64_t at = c.I(i, "arrival_ns");
        CHECK(at >= prevArrival);
        if (prevSeq) { CHECK(seq == prevSeq + 1); CHECK(fr == prevFrame + 1); CHECK(c.U(i, "timeline_id") == timeline); }
        timeline = static_cast<uint32_t>(c.U(i, "timeline_id"));
        CHECK(c.U(i, "gap_count") == 0 && c.U(i, "evict_count") == 0);
        if (at >= ws) CHECK(c.U(i, "mode") == wantMode);
        prevSeq = seq; prevFrame = fr; prevArrival = at;
    }
    CHECK(!c.rows.empty());
    if (c.rows.empty()) return;
    const int64_t first = c.I(0, "frame_no"), last = c.I(c.rows.size() - 1, "frame_no");
    CHECK(static_cast<int64_t>(c.rows.size()) == last - first + 1);   // rows == the frames in between: none missing
    // and none invented. Two of slack: a connection starts at the newest record, which can be the one published
    // just before the count was read (the stand-in counts a frame after publishing it).
    CHECK(c.rows.size() <= publishedDuringRun + 2);
    const int64_t fws = FrameAt(c, ws), fwe = FrameAt(c, we);
    CHECK(fwe - fws >= wantFrames);                                // the window holds what was asked
    CHECK(std::atoll(s.at("window_frames").c_str()) == fwe - fws);
    CHECK(fws - first >= kWarmFrames);                                // and the warm-up was at least that long
}

void TestStreamAttributionOff()
{
    v3::BenchOptions o = Base("off");
    const unsigned long long p0 = gPublished.load();
    const int rc = v3::RunBench(o, stdout);
    const unsigned long long ran = gPublished.load() - p0;
    CHECK(rc == v3::kOk);
    const Csv c = ReadCsv(o.csvPath);
    const auto s = ReadSummary(o.summaryPath);
    CHECK(s.at("valid") == "1");
    CHECK(s.at("attribution_requested") == "0");
    CHECK(s.at("lost_server_ring_total") == "0" && s.at("lost_client_queue_total") == "0");
    CheckFrames(c, s, 0u, ran);
    CHECK(gHookFlags.load() == 0u);
}

void TestStreamAttributionOn()
{
    v3::BenchOptions o = Base("on");
    o.functions = true;
    gHookFlags = 0;
    // The request must reach the emulate side while the client is attached.
    std::atomic<bool> sawOn{ false };
    std::atomic<bool> done{ false };
    std::thread watcher([&] {
        while (!done.load()) { if (gHookFlags.load() == SE_LIVE_PROF_FUNCTIONS) sawOn = true; SleepMs(1); }
    });
    const unsigned long long p0 = gPublished.load();
    const int rc = v3::RunBench(o, stdout);
    const unsigned long long ran = gPublished.load() - p0;
    CHECK(WaitFrames(5));   // the emulate side applies the disconnect at a frame boundary
    done = true;
    watcher.join();
    CHECK(rc == v3::kOk);
    CHECK(sawOn.load());
    CHECK(gHookFlags.load() == 0u);   // and it is gone once the client left
    const Csv c = ReadCsv(o.csvPath);
    const auto s = ReadSummary(o.summaryPath);
    CHECK(s.at("valid") == "1");
    CHECK(s.at("attribution_requested") == "1");
    CheckFrames(c, s, SE_LIVE_PROF_FUNCTIONS, ran);
}

// The request is applied some frames after it is sent (modelled by gModeLag, longer than the 10-frame warm-up):
// a window that opened on the warm-up count alone would hold frames of the old mode. The client must wait for
// the requested mode, and every frame in the window must carry it, whatever the lag.
void TestWindowWaitsForRequestedMode()
{
    v3::BenchOptions o = Base("lag", 40);
    o.functions = true;
    gHookFlags = 0;
    gModeLag = 30;
    const unsigned long long p0 = gPublished.load();
    const int rc = v3::RunBench(o, stdout);
    const unsigned long long ran = gPublished.load() - p0;
    gModeLag = 0;
    CHECK(rc == v3::kOk);
    const Csv c = ReadCsv(o.csvPath);
    const auto s = ReadSummary(o.summaryPath);
    CHECK(s.at("valid") == "1" && s.at("mode_mismatch_window") == "0");
    CheckFrames(c, s, SE_LIVE_PROF_FUNCTIONS, ran, 40);
    // Frames of the old mode were delivered, and the window opened after the last of them.
    size_t old = 0;
    for (size_t i = 0; i < c.rows.size(); ++i) if (c.U(i, "mode") == 0u) ++old;
    CHECK(old > 0);
    CHECK(WaitFrames(40));   // the lag drains: the disconnect is applied and the old mode comes back
}

// A frame in the window that ran in another mode than requested makes the run invalid, counted exactly.
void TestModeMismatchIsInvalid()
{
    v3::BenchOptions o = Base("mismatch", 150);
    o.functions = true;
    gHookFlags = 0;
    gMismatchCount = 3;
    gMismatchAt = gPublished.load() + 70;   // inside the window (opens ~10 frames in, 150 long)
    const int rc = v3::RunBench(o, stdout);
    gMismatchAt = 0;
    CHECK(rc == v3::kInvalid);
    const auto s = ReadSummary(o.summaryPath);
    CHECK(s.at("valid") == "0");
    CHECK(s.at("mode_mismatch_window") == "3");
    CHECK(WaitFrames(5));
}

void TestBracket()
{
    v3::BenchOptions o = Base("bracket", 150);   // more than the 0.3 s minimum gap holds at 4 ms a frame: forces a second look
    o.mode = v3::Mode::Bracket;
    o.warmupS = 0.15;
    o.durationS = 0.3;
    const unsigned long long p0 = gPublished.load();
    const int rc = v3::RunBench(o, stdout);
    const unsigned long long ran = gPublished.load() - p0;
    CHECK(rc == v3::kOk);
    const Csv c = ReadCsv(o.csvPath);
    const auto s = ReadSummary(o.summaryPath);
    CHECK(s.at("mode") == "bracket" && s.at("valid") == "1");
    // Two probes, plus one more for every time the end probe found fewer than the requested frames.
    const size_t extra = static_cast<size_t>(std::atoll(s.at("bracket_extra_probes").c_str()));
    CHECK(c.rows.size() == 2 + extra);
    if (c.rows.size() >= 2)
    {
        for (size_t i = 0; i < c.rows.size(); ++i) CHECK(c.rows[i].at("kind") == "probe");
        const size_t n = c.rows.size();
        const int64_t dt = c.I(n - 1, "arrival_ns") - c.I(0, "arrival_ns");
        CHECK(dt >= 300000000ll);                     // a sleep is never shorter than asked
        CHECK(c.I(0, "arrival_ns") >= 150000000ll);   // nothing connected during the warm-up
        const int64_t df = c.I(n - 1, "frame_no") - c.I(0, "frame_no");
        CHECK(df >= 150);                             // the count that was asked for
        CHECK(static_cast<unsigned long long>(df) <= ran + 2);   // and not more than the stand-in published (2 slack, as above)
        CHECK(std::atoll(s.at("window_frames").c_str()) == df);
        for (size_t i = 1; i < n; ++i) CHECK(c.U(i, "timeline_id") == c.U(0, "timeline_id"));
    }
}

// A burst published far faster than the ring holds, at a chosen frame count inside the window. Judged by the
// exact accounting every run must satisfy: sequence numbers from the first to the last delivered record are
// the records delivered plus the records the CSV says were lost, and the lost count is large (the ring keeps
// 16, a reply carries 8).
void TestLossIsCountedAndInvalid()
{
    v3::BenchOptions o = Base("loss", 150);
    const unsigned long long burstAt = gPublished.load() + 70;   // well after the 10-frame warm-up, well before 150
    gBurstSize = 200;
    gBurstAt = burstAt;
    const int rc = v3::RunBench(o, stdout);
    gBurstAt = 0;
    CHECK(rc == v3::kInvalid);
    const auto s = ReadSummary(o.summaryPath);
    CHECK(s.at("valid") == "0");
    {
        std::ifstream sf(o.summaryPath);
        std::stringstream all;
        all << sf.rdbuf();
        CHECK(all.str().find("invalid_reason=samples lost in the measured window") != std::string::npos);
    }
    const uint64_t lostWin = std::strtoull(s.at("lost_server_ring_window").c_str(), nullptr, 10);
    const uint64_t lostAll = std::strtoull(s.at("lost_server_ring_total").c_str(), nullptr, 10);
    CHECK(lostWin >= 150);   // 200 records at once into a 16-deep ring
    CHECK(lostAll >= lostWin);
    CHECK(std::atoll(s.at("lost_client_queue_window").c_str()) == 0);
    const Csv c = ReadCsv(o.csvPath);
    uint64_t sum = 0, evict = 0, frames = 0, firstSeq = 0, lastSeq = 0;
    for (size_t i = 0; i < c.rows.size(); ++i)
    {
        sum += c.U(i, "gap_count");
        evict += c.U(i, "evict_count");
        if (c.rows[i].at("kind") == "frame")
        {
            ++frames;
            if (!firstSeq) firstSeq = c.U(i, "sample_seq");
            lastSeq = c.U(i, "sample_seq");
        }
    }
    CHECK(sum == lostAll);                                      // the CSV carries the same exact count
    CHECK(lastSeq - firstSeq + 1 == frames + sum + evict);      // every record is delivered or counted lost
}

void TestLoadSlot()
{
    v3::BenchOptions o = Base("slot");
    o.mode = v3::Mode::LoadSlot;
    o.loadSlot = 3;
    gSlotLoads = 0;
    const int rc = v3::RunBench(o, stdout);
    CHECK(rc == v3::kOk);
    CHECK(gSlotLoads.load() == 1);
    CHECK(gSlotLoaded.load() == 3);
}


// A byte-for-byte TCP forwarder in front of the exporter's TCP port that records every request header
// the client sends ("REW:0", "PRF:1", ...), per connection. bench_client sends no request with a
// payload, so the stream splits into 8-byte headers.
class Sniffer
{
public:
    bool Start(uint16_t upstreamPort)
    {
        mUp = upstreamPort;
        mL = ::socket(AF_INET, SOCK_STREAM, 0);
        if (mL < 0) return false;
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (::bind(mL, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(mL, 8) != 0) return false;
        socklen_t len = sizeof a;
        ::getsockname(mL, reinterpret_cast<sockaddr*>(&a), &len);
        mPort = ntohs(a.sin_port);
        mRun = true;
        mAcceptor = std::thread([this] {
            while (mRun.load())
            {
                const int c = ::accept(mL, nullptr, nullptr);
                if (c < 0 || !mRun.load()) { if (c >= 0) ::close(c); break; }
                const int u = ::socket(AF_INET, SOCK_STREAM, 0);
                sockaddr_in ua = {};
                ua.sin_family = AF_INET;
                ua.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                ua.sin_port = htons(mUp);
                if (u < 0 || ::connect(u, reinterpret_cast<sockaddr*>(&ua), sizeof ua) != 0) { ::close(c); if (u >= 0) ::close(u); continue; }
                size_t idx;
                { std::lock_guard<std::mutex> lk(mMu); idx = mConns.size(); mConns.emplace_back(); }
                std::lock_guard<std::mutex> lk(mMu);
                mThreads.emplace_back([this, c, u, idx] { Relay(c, u, idx, true); });
                mThreads.emplace_back([this, c, u, idx] { Relay(u, c, idx, false); });
            }
        });
        return true;
    }
    void Stop()
    {
        if (!mRun.exchange(false)) return;
        const int w = ::socket(AF_INET, SOCK_STREAM, 0);   // wake accept()
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(mPort);
        if (w >= 0) { (void)::connect(w, reinterpret_cast<sockaddr*>(&a), sizeof a); ::close(w); }
        if (mAcceptor.joinable()) mAcceptor.join();
        ::close(mL);
        for (std::thread& t : mThreads) if (t.joinable()) t.join();
    }
    ~Sniffer() { Stop(); }
    std::string Endpoint() const { return "tcp:127.0.0.1:" + std::to_string(mPort); }
    // Connections that carried at least one request, each as its list of "VERB:arg" tokens.
    std::vector<std::vector<std::string>> Used()
    {
        std::lock_guard<std::mutex> lk(mMu);
        std::vector<std::vector<std::string>> out;
        for (const auto& c : mConns) if (!c.empty()) out.push_back(c);
        return out;
    }
private:
    void Relay(int from, int to, size_t idx, bool parse)
    {
        std::vector<unsigned char> acc;
        unsigned char buf[65536];
        for (;;)
        {
            const ssize_t n = ::recv(from, buf, sizeof buf, 0);
            if (n <= 0) break;
            if (parse)
            {
                acc.insert(acc.end(), buf, buf + n);
                while (acc.size() >= 8)
                {
                    std::string v(reinterpret_cast<const char*>(acc.data()), 3);
                    const uint32_t arg = acc[4] | (acc[5] << 8) | (acc[6] << 16) | (static_cast<uint32_t>(acc[7]) << 24);
                    { std::lock_guard<std::mutex> lk(mMu); mConns[idx].push_back(v + ":" + std::to_string(arg)); }
                    acc.erase(acc.begin(), acc.begin() + 8);
                }
            }
            ssize_t off = 0;
            while (off < n) { const ssize_t w = ::send(to, buf + off, static_cast<size_t>(n - off), MSG_NOSIGNAL); if (w <= 0) { off = n; break; } off += w; }
        }
        ::shutdown(from, SHUT_RDWR);
        ::shutdown(to, SHUT_RDWR);
    }
    uint16_t mUp = 0, mPort = 0;
    int mL = -1;
    std::atomic<bool> mRun{ false };
    std::thread mAcceptor;
    std::vector<std::thread> mThreads;
    std::mutex mMu;
    std::vector<std::vector<std::string>> mConns;
};

bool Has(const std::vector<std::string>& v, const std::string& t)
{
    for (const std::string& x : v) if (x == t) return true;
    return false;
}

// Rewind capture is held off in every mode: on the wire (REW:0 on every connection that asks for
// anything) and at the server (no per-frame savestates while the client is attached).
void TestRewindOffEveryMode()
{
    // Control: a client that does not send REW 0 has the stand-in take a savestate for (nearly) every frame
    // it publishes while the client is attached.
    {
        se_data_source ds{};
        for (int i = 0; i < 2000 && se_live_open(kSock, &ds) != SE_OK; ++i) SleepMs(5);
        CHECK(ds.close != nullptr);
        for (int i = 0; i < 6000 && se_live_server_version(&ds) == 0; ++i) SleepMs(2);
        CHECK(WaitFrames(3));   // attached and flowing before counting
        const int s0 = gSaves.load();
        const unsigned long long p0 = gPublished.load();
        CHECK(WaitFrames(60));
        const int saves = gSaves.load() - s0;
        const long long frames = static_cast<long long>(gPublished.load() - p0);
        std::printf("control (no REW 0): %d savestates over %lld frames\n", saves, frames);
        CHECK(frames >= 60);
        CHECK(saves >= frames - 8);
        if (ds.close) ds.close(ds.user);
        CHECK(WaitFrames(5));
    }
    // Server side, stream mode over the local socket (no proxy in the way): the same client turned off
    // REW, so only the frames before its request landed may have been saved.
    {
        v3::BenchOptions o = Base("rew_srv", 120);
        const int s0 = gSaves.load();
        const unsigned long long p0 = gPublished.load();
        CHECK(v3::RunBench(o, stdout) == v3::kOk);
        const int taken = gSaves.load() - s0;
        const long long frames = static_cast<long long>(gPublished.load() - p0);
        std::printf("REW 0 stream run: %d savestates over %lld frames\n", taken, frames);
        CHECK(frames >= 130);
        CHECK(taken * 4 < frames);   // a quarter at most; the control above saves all but a handful
    }
    // Wire side, all three modes, through the sniffer.
    Sniffer sn;
    CHECK(sn.Start(6953));
    v3::BenchOptions st = Base("rew_stream", 40);
    st.endpoint = sn.Endpoint();
    st.functions = true;
    (void)v3::RunBench(st, stdout);   // validity is not the point here, only what the client sent
    v3::BenchOptions br = Base("rew_bracket", 30);
    br.endpoint = sn.Endpoint();
    br.mode = v3::Mode::Bracket;
    br.warmupS = 0.1;
    br.durationS = 0.2;
    CHECK(v3::RunBench(br, stdout) == v3::kOk);
    const size_t extraProbes = static_cast<size_t>(std::atoll(ReadSummary(br.summaryPath).at("bracket_extra_probes").c_str()));
    v3::BenchOptions ls = Base("rew_slot");
    ls.endpoint = sn.Endpoint();
    ls.mode = v3::Mode::LoadSlot;
    ls.loadSlot = 2;
    CHECK(v3::RunBench(ls, stdout) == v3::kOk);
    CHECK(WaitFrames(5));
    sn.Stop();
    const auto used = sn.Used();
    // stream: 1 connection, bracket: 2 plus one per extra look, load-slot: 1
    CHECK(used.size() == 4 + extraProbes);
    size_t withRew = 0, withPrf1 = 0, withEls = 0;
    for (const auto& c : used)
    {
        if (Has(c, "REW:0")) ++withRew;
        if (Has(c, "PRF:1")) ++withPrf1;
        if (Has(c, "ELS:2")) ++withEls;
        CHECK(!Has(c, "REW:1"));
    }
    CHECK(withRew == used.size());   // every connection that sent anything turned rewind capture off
    CHECK(withPrf1 == 1);            // attribution on reached the wire once (the stream)
    CHECK(withEls == 1);             // and the slot request reached it too
}

int Exec(const std::string& cmd)
{
    const int st = std::system(cmd.c_str());
#if defined(_WIN32)
    return st;
#else
    return (st >= 0 && (st & 0x7f) == 0) ? ((st >> 8) & 0xff) : -1;
#endif
}

void TestExecutable(const char* exe)
{
    // Attribution on through the real command line.
    const std::string base = std::string("\"") + exe + "\" --endpoint " + std::string(kSock) + " --frames 60 --warmup-frames 10 --drain-ms 8 ";
    gHookFlags = 0;
    int rc = Exec(base + "--attribution on --tag exe --csv " + gTmp + "exe.csv --summary " + gTmp + "exe.summary > " + gTmp + "exe.out");
    CHECK(rc == v3::kOk);
    const auto s = ReadSummary(gTmp + "exe.summary");
    CHECK(s.count("valid") && s.at("valid") == "1");
    CHECK(s.count("attribution_requested") && s.at("attribution_requested") == "1");
    const Csv c = ReadCsv(gTmp + "exe.csv");
    CHECK(c.rows.size() >= 60 + 10);   // the warm-up frames and the window's, both delivered
    if (!c.rows.empty()) CHECK(c.U(c.rows.size() - 1, "mode") == SE_LIVE_PROF_FUNCTIONS);
    CHECK(s.count("window_frames") && std::atoll(s.at("window_frames").c_str()) >= 60);
    CHECK(Exec(std::string("\"") + exe + "\" --mode nonsense > " + gTmp + "bad.out 2>&1") == v3::kError);
    // Nothing listening: an error exit, not a hang.
    CHECK(Exec(std::string("\"") + exe + "\" --endpoint tcp:127.0.0.1:6959 --connect-timeout 1 > " + gTmp + "none.out") == v3::kError);
}
}  // namespace

int main(int argc, char** argv)
{
    if (const char* e = std::getenv("V3_TEST_FRAME_MS")) { if (std::atoi(e) > 0) gFrameMs = std::atoi(e); }
    gTmp = "/tmp/se_v3_bench_test_" + std::to_string(static_cast<long>(std::time(nullptr))) + "_";
    if (SeExportPrepare() != 0) { std::printf("SeExportPrepare failed\n"); return 1; }
    SeExportSetProfilerHook(FakeSetProfiler);
    SeExportSetPerfCapable(1);
    SeExportSetEmuSlotHooks(FakeSlotInfo, FakeSlotLoad);
    SeExportSetSaveStateHook(FakeSave);
    SeExportSetLoadStateHook(FakeLoad);
    if (SeExportStart() != 0) { std::printf("SeExportStart failed\n"); return 1; }
    std::thread emu(Emulator);

    TestStreamAttributionOff();
    TestStreamAttributionOn();
    TestWindowWaitsForRequestedMode();
    TestModeMismatchIsInvalid();
    TestBracket();
    TestLossIsCountedAndInvalid();
    TestLoadSlot();
    TestRewindOffEveryMode();
    if (argc > 1) TestExecutable(argv[1]);

    gStop = true;
    emu.join();
    SeExportDeinit();
    std::printf(gFailures ? "FAILED: %d check(s)\n" : "ok\n", gFailures);
    return gFailures ? 1 : 0;
}
