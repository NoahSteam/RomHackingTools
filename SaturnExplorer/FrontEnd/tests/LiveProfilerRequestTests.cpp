// F1: the profiler (function attribution) request belongs to a connection.
//
// Real exporter + real driver on a socket and port of their own, with two clients attached at
// once (the local socket and the TCP port). The value under test is SeExportProfilerFlags(), the
// union the emulator runs with, read after a gate -- the point where the emulate thread applies a
// pending request.
//
// A second layer runs the driver against a scripted server to pin the restate on reconnect, which
// a real exporter cannot expose any more (its connections all start with no request).
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

extern "C" {
#include "se_export.h"
}
#include "FakeLiveServer.h"
#include "LiveDriver.h"
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

extern "C" void FakeSetProfiler(unsigned int) {}

const char* const kLocal = SE_LIVE_DEFAULT_SOCK_PATH;
const char* const kTcp = "tcp:127.0.0.1:6952";

bool OpenRetry(const char* endpoint, se_data_source* ds)
{
    for (int i = 0; i < 1000; ++i)
    {
        if (se_live_open(endpoint, ds) == SE_OK) return true;
        SleepMs(5);
    }
    return false;
}

uint64_t Replies(const se_data_source& ds)
{
    se_live_perf_status st = {};
    se_live_perf_status_get(&ds, &st);
    return st.replies;
}

// The application boundary: the emulate thread's gate applies a pending request.
unsigned Applied()
{
    SeExportGateFrame();
    return SeExportProfilerFlags();
}

// Applying "eventually" is bounded by the deadline only.
bool WaitApplied(unsigned want, int budgetMs = 8000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (Applied() == want) return true;
        SleepMs(1);
    }
    return false;
}

// Every listed client has been served 'n' more requests, so whatever it had sent before this call
// was handled by the server; then one more gate. A "stays on" claim is judged after this.
void Settled(const std::vector<const se_data_source*>& clients, uint64_t n = 5)
{
    std::vector<uint64_t> target;
    for (const se_data_source* c : clients) target.push_back(Replies(*c) + n);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8000);
    for (;;)
    {
        bool done = true;
        for (size_t i = 0; i < clients.size(); ++i)
            if (Replies(*clients[i]) < target[i]) done = false;
        if (done || std::chrono::steady_clock::now() > deadline) break;
        SleepMs(1);
    }
    Applied();
}

void Close(se_data_source& ds)
{
    if (ds.close) ds.close(ds.user);
    ds = {};
}

// A client that sends PRF and vanishes with a reset, without reading the reply.
void RawClientRequestsThenVanishes(unsigned flags)
{
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un a = {};
    a.sun_family = AF_UNIX;
    std::snprintf(a.sun_path, sizeof(a.sun_path), "%s", kLocal);
    if (fd < 0 || ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) { CHECK(false); return; }
    unsigned char req[SE_LIVE_REQUEST_LEN] = { 'P', 'R', 'F', '\n', (unsigned char)flags, 0, 0, 0 };
    CHECK(::write(fd, req, sizeof(req)) == (ssize_t)sizeof(req));
    SleepMs(50);
    linger lg = { 1, 0 };   // close with a reset, the way a killed process does
    ::setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
    ::close(fd);
}

void TestPerConnection()
{
    CHECK(SeExportInit() == 0);
    SeExportSetProfilerHook(FakeSetProfiler);
    se_data_source a = {}, b = {};
    CHECK(OpenRetry(kLocal, &a));
    CHECK(OpenRetry(kTcp, &b));
    CHECK(Applied() == 0u);

    // A asks, B does not: on.
    se_live_set_profiler(&a, SE_LIVE_PROF_FUNCTIONS);
    CHECK(WaitApplied(SE_LIVE_PROF_FUNCTIONS));
    Settled({ &a, &b });
    CHECK(Applied() == SE_LIVE_PROF_FUNCTIONS);

    // A goes away; B is still attached and never asked: off. (Old code: stays on, because the
    // last-client cleanup does not run while B is connected.)
    const uint64_t bBefore = Replies(b);
    Close(a);
    CHECK(WaitApplied(0u));
    Settled({ &b });
    CHECK(Applied() == 0u);
    CHECK(Replies(b) > bBefore);   // B was connected throughout

    // Both ask; one leaves: still on. The other withdraws: off, with the client still attached.
    CHECK(OpenRetry(kLocal, &a));
    se_live_set_profiler(&a, SE_LIVE_PROF_FUNCTIONS);
    se_live_set_profiler(&b, SE_LIVE_PROF_FUNCTIONS);
    CHECK(WaitApplied(SE_LIVE_PROF_FUNCTIONS));
    Settled({ &a, &b });
    Close(a);
    Settled({ &b });
    CHECK(Applied() == SE_LIVE_PROF_FUNCTIONS);
    se_live_set_profiler(&b, 0);
    CHECK(WaitApplied(0u));

    // Connect, enable, leave, repeatedly, with the passive peer attached throughout: each
    // connection's end releases exactly its own request, and nothing it does clobbers the next.
    for (int round = 0; round < 20; ++round)
    {
        se_data_source x = {};
        CHECK(OpenRetry(kLocal, &x));
        se_live_set_profiler(&x, SE_LIVE_PROF_FUNCTIONS);
        CHECK(WaitApplied(SE_LIVE_PROF_FUNCTIONS));
        Close(x);
        CHECK(WaitApplied(0u));
    }

    // An abrupt (reset) disconnect releases its request while B stays attached.
    RawClientRequestsThenVanishes(SE_LIVE_PROF_FUNCTIONS);
    Settled({ &b });
    CHECK(WaitApplied(0u));
    CHECK(Replies(b) > 0);

    // The TCP (web bridge) connection has its own lifetime: its request dies with it while a
    // local client that asked for nothing stays attached.
    CHECK(OpenRetry(kLocal, &a));
    Close(b);
    se_data_source t = {};
    CHECK(OpenRetry(kTcp, &t));
    se_live_set_profiler(&t, SE_LIVE_PROF_FUNCTIONS);
    CHECK(WaitApplied(SE_LIVE_PROF_FUNCTIONS));
    Close(t);
    CHECK(WaitApplied(0u));
    Settled({ &a });
    CHECK(Applied() == 0u);

    Close(a);
    SeExportDeinit();
}

// The driver's side: a fresh connection restates the setting whatever it is, "off" included.
void TestRestate()
{
    std::mutex mu;
    std::vector<std::vector<uint32_t>> prf;   // per connection index: PRF args in arrival order
    std::atomic<int> hangupOn{ -1 };          // hang up connection N once the test says so
    fakelive::Reply reply;
    const std::vector<uint8_t> bytes = fakelive::Build(reply);

    fakelive::Server server([&](int fd, int index) {
        if (index == 0) return;   // the open-time probe
        {
            std::lock_guard<std::mutex> lk(mu);
            if (prf.size() <= static_cast<size_t>(index)) prf.resize(index + 1);
        }
        fakelive::Request r;
        while (hangupOn.load() != index && fakelive::ReadRequest(fd, r))
        {
            if (r.Is(SE_LIVE_VERB_PROFILE))
            {
                std::lock_guard<std::mutex> lk(mu);
                prf[index].push_back(r.arg);
            }
            if (!fakelive::WriteExact(fd, bytes.data(), bytes.size())) return;
        }
    });
    CHECK(server.Start());
    se_data_source ds = {};
    CHECK(se_live_open(server.Endpoint().c_str(), &ds) == SE_OK);

    auto seen = [&](int index, size_t n) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8000);
        while (std::chrono::steady_clock::now() < deadline)
        {
            {
                std::lock_guard<std::mutex> lk(mu);
                if (prf.size() > static_cast<size_t>(index) && prf[index].size() >= n) return true;
            }
            SleepMs(1);
        }
        return false;
    };

    se_live_set_profiler(&ds, SE_LIVE_PROF_FUNCTIONS);
    CHECK(seen(1, 1));
    se_live_set_profiler(&ds, 0);
    CHECK(seen(1, 2));

    // Reconnect with the setting off: the new connection is told "off". The server has no memory
    // of this client and the driver must not assume its default. (Old code restated only a
    // non-zero setting, so this connection heard nothing.)
    hangupOn.store(1);
    CHECK(seen(2, 1));
    {
        std::lock_guard<std::mutex> lk(mu);
        CHECK(prf.size() > 2 && !prf[2].empty() && prf[2][0] == 0u);
    }

    // And with it on.
    se_live_set_profiler(&ds, SE_LIVE_PROF_FUNCTIONS);
    CHECK(seen(2, 2));
    hangupOn.store(2);
    CHECK(seen(3, 1));
    {
        std::lock_guard<std::mutex> lk(mu);
        CHECK(prf.size() > 3 && !prf[3].empty() && prf[3][0] == SE_LIVE_PROF_FUNCTIONS);
    }
    Close(ds);
    server.Stop();
}
}  // namespace

int main()
{
    TestRestate();
    TestPerConnection();
    if (gFailures) { std::printf("LiveProfilerRequestTests: %d failure(s)\n", gFailures); return 1; }
    std::printf("LiveProfilerRequestTests passed\n");
    return 0;
}
