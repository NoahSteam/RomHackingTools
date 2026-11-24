// V3 harness: the benchmark client against the REAL exporter (se_export.c) and the real driver,
// with a stand-in emulator thread that publishes frames the way the Mednafen glue does. No BIOS.
//
// What is asserted: the CSV rows (sequence, frame number, timeline, mode, loss counts, arrival
// time), that the attribution on/off request reaches the emulate side and is gone when the client
// leaves, that the bracket mode holds no connection inside its window, that a lost range makes
// the run invalid with the exact count, that a slot load reaches the emulator, and the same through
// the real executable (argv[1]) with its exit codes. It says NOTHING about emulator overhead.
//
// Socket and port of their own: 6953 and /tmp/saturn_explorer_v3_bench_test.sock.
#include <atomic>
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

// The stand-in emulator: one frame every 'periodMs', each stamped with the producer's identity and
// the attribution mode in force. 'burst' makes it publish that many frames back to back once.
std::atomic<bool> gStop{ false };
std::atomic<int> gBurst{ 0 };
void Publish1()
{
    SeExportGateFrame();
    SeExportPerfInfo info;
    std::memset(&info, 0, sizeof info);
    SeExportFrameIdentity(&info.timelineId, &info.frameNo);
    info.valid = SE_PERF_VALID_IDENTITY | SE_PERF_VALID_MODE;
    info.mode = SeExportProfilerFlags();
    unsigned long long c[SE_PERF_COUNT] = {};
    SeExportPublishPerf(c, SE_PERF_COUNT, nullptr, nullptr, 0, 0, &info);
    SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, nullptr);
    SeExportEndFrame();
}
void Emulator()
{
    while (!gStop.load())
    {
        int n = gBurst.exchange(0);
        if (n > 0) { while (n-- > 0) Publish1(); }
        Publish1();
        SleepMs(4);
    }
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

v3::BenchOptions Base(const std::string& name)
{
    v3::BenchOptions o;
    o.endpoint = kSock;
    o.warmupS = 0.4;
    o.durationS = 1.2;
    o.drainMs = 8;
    o.connectTimeoutS = 20;
    o.csvPath = gTmp + name + ".csv";
    o.summaryPath = gTmp + name + ".summary";
    o.tag = name;
    return o;
}

// Rows of kind "frame" at or after the window start, from the summary's own window.
void CheckFrames(const Csv& c, const std::map<std::string, std::string>& s, uint32_t wantMode)
{
    const int64_t ws = std::atoll(s.at("window_start_ns").c_str());
    uint64_t prevSeq = 0;
    int64_t prevFrame = -1, prevArrival = -1;
    uint32_t timeline = 0;
    size_t inWin = 0;
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
        if (at >= ws) { ++inWin; CHECK(c.U(i, "mode") == wantMode); }
        prevSeq = seq; prevFrame = fr; prevArrival = at;
    }
    // 1.2 s at one frame per 4 ms: far more than a handful, and not more than a frame per ms.
    CHECK(inWin > 60);
    CHECK(inWin < 1300);
    CHECK(static_cast<uint64_t>(std::atoll(s.at("window_rows").c_str())) == inWin);
}

void TestStreamAttributionOff()
{
    v3::BenchOptions o = Base("off");
    const int rc = v3::RunBench(o, stdout);
    CHECK(rc == v3::kOk);
    const Csv c = ReadCsv(o.csvPath);
    const auto s = ReadSummary(o.summaryPath);
    CHECK(s.at("valid") == "1");
    CHECK(s.at("attribution_requested") == "0");
    CHECK(s.at("lost_server_ring_total") == "0" && s.at("lost_client_queue_total") == "0");
    CHECK(c.rows.size() > 100);
    if (!c.rows.empty()) CheckFrames(c, s, 0u);
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
    const int rc = v3::RunBench(o, stdout);
    SleepMs(300);   // the emulate side applies the disconnect at its next frame
    done = true;
    watcher.join();
    CHECK(rc == v3::kOk);
    CHECK(sawOn.load());
    CHECK(gHookFlags.load() == 0u);   // and it is gone once the client left
    const Csv c = ReadCsv(o.csvPath);
    const auto s = ReadSummary(o.summaryPath);
    CHECK(s.at("valid") == "1");
    CHECK(s.at("attribution_requested") == "1");
    if (!c.rows.empty()) CheckFrames(c, s, SE_LIVE_PROF_FUNCTIONS);
    // Warm-up rows exist and precede the window (the first ones ran before the request landed or after).
    CHECK(std::atoll(s.at("window_start_ns").c_str()) >= 400000000ll);
}

void TestBracket()
{
    v3::BenchOptions o = Base("bracket");
    o.mode = v3::Mode::Bracket;
    o.warmupS = 0.3;
    o.durationS = 1.0;
    const int rc = v3::RunBench(o, stdout);
    CHECK(rc == v3::kOk);
    const Csv c = ReadCsv(o.csvPath);
    const auto s = ReadSummary(o.summaryPath);
    CHECK(s.at("mode") == "bracket" && s.at("valid") == "1");
    CHECK(c.rows.size() == 2);
    if (c.rows.size() == 2)
    {
        CHECK(c.rows[0].at("kind") == "probe" && c.rows[1].at("kind") == "probe");
        const int64_t dt = c.I(1, "arrival_ns") - c.I(0, "arrival_ns");
        CHECK(dt >= 1000000000ll && dt < 4000000000ll);
        const int64_t df = c.I(1, "frame_no") - c.I(0, "frame_no");
        // ~250 frames/s over ~1 s; a stalled or double-counting clock is far outside this.
        CHECK(df > 100 && df < 2000);
        CHECK(c.I(0, "arrival_ns") >= 300000000ll);   // nothing connected during the warm-up
        CHECK(c.U(0, "timeline_id") == c.U(1, "timeline_id"));
    }
}

void TestLossIsCountedAndInvalid()
{
    v3::BenchOptions o = Base("loss");
    std::thread burster([] { SleepMs(900); gBurst = 200; });   // inside the window
    const int rc = v3::RunBench(o, stdout);
    burster.join();
    CHECK(rc == v3::kInvalid);
    const auto s = ReadSummary(o.summaryPath);
    CHECK(s.at("valid") == "0");
    {
        std::ifstream sf(o.summaryPath);
        std::stringstream all;
        all << sf.rdbuf();
        CHECK(all.str().find("invalid_reason=samples lost in the measured window") != std::string::npos);
    }
    const uint64_t lost = std::strtoull(s.at("lost_server_ring_window").c_str(), nullptr, 10);
    CHECK(lost > 150);   // 200 frames at once into a 16-deep ring
    CHECK(std::atoll(s.at("lost_client_queue_window").c_str()) == 0);
    // The CSV carries the same exact count, row by row.
    const Csv c = ReadCsv(o.csvPath);
    uint64_t sum = 0;
    for (size_t i = 0; i < c.rows.size(); ++i) sum += c.U(i, "gap_count");
    CHECK(sum == std::strtoull(s.at("lost_server_ring_total").c_str(), nullptr, 10));
    CHECK(sum >= lost);
    CHECK(std::atoll(s.at("seq_missing_window").c_str()) > 0 || lost > 0);
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
    // Positive control: a client that does not send REW 0 makes the stand-in take savestates.
    {
        se_data_source ds{};
        for (int i = 0; i < 2000 && se_live_open(kSock, &ds) != SE_OK; ++i) SleepMs(5);
        CHECK(ds.close != nullptr);
        SleepMs(150);
        const int before = gSaves.load();
        SleepMs(600);
        CHECK(gSaves.load() - before > 40);
        if (ds.close) ds.close(ds.user);
        SleepMs(200);
    }
    // Server side, stream mode over the local socket (no proxy in the way).
    {
        v3::BenchOptions o = Base("rew_srv");
        o.warmupS = 0.3;
        o.durationS = 1.2;
        const int before = gSaves.load();
        CHECK(v3::RunBench(o, stdout) == v3::kOk);
        const int taken = gSaves.load() - before;
        std::printf("savestates taken during a 1.5 s stream run: %d\n", taken);
        CHECK(taken < 30);   // ~375 frames ran; only those before REW 0 landed may be saved
    }
    // Wire side, all three modes, through the sniffer.
    Sniffer sn;
    CHECK(sn.Start(6953));
    v3::BenchOptions st = Base("rew_stream");
    st.endpoint = sn.Endpoint();
    st.functions = true;
    st.warmupS = 0.3;
    st.durationS = 0.6;
    (void)v3::RunBench(st, stdout);   // over TCP the stand-in may overrun the ring: validity is not the point here
    v3::BenchOptions br = Base("rew_bracket");
    br.endpoint = sn.Endpoint();
    br.mode = v3::Mode::Bracket;
    br.warmupS = 0.2;
    br.durationS = 0.5;
    CHECK(v3::RunBench(br, stdout) == v3::kOk);
    v3::BenchOptions ls = Base("rew_slot");
    ls.endpoint = sn.Endpoint();
    ls.mode = v3::Mode::LoadSlot;
    ls.loadSlot = 2;
    CHECK(v3::RunBench(ls, stdout) == v3::kOk);
    SleepMs(200);
    sn.Stop();
    const auto used = sn.Used();
    // stream: 1 connection, bracket: 2, load-slot: 1
    CHECK(used.size() == 4);
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
    const std::string base = std::string("\"") + exe + "\" --endpoint " + std::string(kSock) + " --warmup 0.3 --duration 1 --drain-ms 8 ";
    gHookFlags = 0;
    int rc = Exec(base + "--attribution on --tag exe --csv " + gTmp + "exe.csv --summary " + gTmp + "exe.summary > " + gTmp + "exe.out");
    CHECK(rc == v3::kOk);
    const auto s = ReadSummary(gTmp + "exe.summary");
    CHECK(s.count("valid") && s.at("valid") == "1");
    CHECK(s.count("attribution_requested") && s.at("attribution_requested") == "1");
    const Csv c = ReadCsv(gTmp + "exe.csv");
    CHECK(c.rows.size() > 100);
    if (!c.rows.empty()) CHECK(c.U(c.rows.size() - 1, "mode") == SE_LIVE_PROF_FUNCTIONS);
    CHECK(Exec(std::string("\"") + exe + "\" --mode nonsense > " + gTmp + "bad.out 2>&1") == v3::kError);
    // Nothing listening: an error exit, not a hang.
    CHECK(Exec(std::string("\"") + exe + "\" --endpoint tcp:127.0.0.1:6959 --connect-timeout 1 > " + gTmp + "none.out") == v3::kError);
}
}  // namespace

int main(int argc, char** argv)
{
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
