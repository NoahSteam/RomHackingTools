// Live-driver behaviors that need a real socket on the other end, driven against a loopback
// listener that never speaks the protocol -- which is also the shape of an emulator that has
// stopped answering.
//
// Reconnection: the driver reconnects on its own, so a client can find itself talking to a
// different emulator process without ever re-opening the source. se_live_connection_-
// generation is how it notices; these tests pin that it counts attachments rather than,
// say, connection attempts, because everything SE drops on a replacement hangs off it.
//
// Poke budget: the poll thread ships one queued memory write per cycle, so a producer that
// outruns it must be refused rather than growing the queue without limit.
#include "LiveDriver.h"
#include "saturnexplorer/SeHost.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what, int line)
{
    if (ok) return;
    std::printf("CHECK failed at line %d: %s\n", line, what);
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

// A listener that accepts and immediately hangs up. The driver cannot read a snapshot from
// it, so it closes and reconnects -- which is exactly the shape of an emulator being
// stopped and another one launched on the same endpoint.
class HangUpServer
{
public:
    bool Start()
    {
        mFd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (mFd < 0) return false;
        int on = 1;
        ::setsockopt(mFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;   // let the kernel pick, so a busy port cannot wedge the test
        if (::bind(mFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
        if (::listen(mFd, 8) != 0) return false;
        socklen_t len = sizeof(addr);
        if (::getsockname(mFd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
        mPort = ntohs(addr.sin_port);
        mRunning.store(true);
        mThread = std::thread([this] {
            while (mRunning.load())
            {
                const int client = ::accept(mFd, nullptr, nullptr);
                if (client < 0) continue;
                ++mAccepted;
                ::close(client);   // hang up; the driver will come back
            }
        });
        return true;
    }

    void Stop()
    {
        mRunning.store(false);
        if (mFd >= 0) { ::shutdown(mFd, SHUT_RDWR); ::close(mFd); mFd = -1; }
        if (mThread.joinable()) mThread.join();
    }

    uint16_t Port() const { return mPort; }
    int Accepted() const { return mAccepted.load(); }

private:
    int               mFd = -1;
    uint16_t          mPort = 0;
    std::atomic<bool> mRunning{false};
    std::atomic<int>  mAccepted{0};
    std::thread       mThread;
};

// Poll until 'predicate' holds or the budget runs out. Returns whether it held; the driver
// paces its retries (100-250 ms), so this waits on the condition rather than on a guess.
template <typename Fn>
bool WaitFor(Fn predicate, int budgetMs = 5000)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(budgetMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

void TestGenerationCountsEveryAttach()
{
    HangUpServer server;
    if (!server.Start())
    {
        CHECK(false && "could not bind a loopback listener");
        return;
    }

    const std::string endpoint = "tcp:127.0.0.1:" + std::to_string(server.Port());
    se_data_source ds = {};
    CHECK(se_live_open(endpoint.c_str(), &ds) == SE_OK);

    // First attach reports 1, not 0 -- a client must be able to tell "connected once" from
    // "never connected", because only the latter means there is nothing to drop.
    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) >= 1u; }));
    const uint32_t first = se_live_connection_generation(&ds);
    CHECK(first >= 1u);

    // The server keeps hanging up, so the driver keeps reattaching and the count keeps
    // climbing. This is the case the whole mechanism exists for.
    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) > first; }));
    CHECK(se_live_connection_generation(&ds) > first);
    CHECK(server.Accepted() >= 2);

    if (ds.close) ds.close(ds.user);
    server.Stop();
}

void TestGenerationIsZeroForANonLiveSource()
{
    // A savestate/dump source never reconnects, and asking must be harmless rather than
    // reaching into a foreign user pointer.
    se_data_source ds = {};
    CHECK(se_live_connection_generation(&ds) == 0u);
    CHECK(se_live_connection_generation(nullptr) == 0u);
}

// A server that never answers ships nothing, so every poke stays queued -- the
// producer-outruns-consumer case, with the consumer stopped dead.
void TestPokeQueueAppliesBackpressure()
{
    HangUpServer server;
    if (!server.Start())
    {
        CHECK(false && "could not bind a loopback listener");
        return;
    }
    const std::string endpoint = "tcp:127.0.0.1:" + std::to_string(server.Port());
    se_data_source ds = {};
    CHECK(se_live_open(endpoint.c_str(), &ds) == SE_OK);
    CHECK(ds.write_main_ram != nullptr);

    const std::vector<uint8_t> chunk(64u * 1024u, 0x5A);
    size_t accepted = 0, refused = 0;
    // 64 MiB of attempts against an 8 MiB budget: if the queue were unbounded every one
    // would be accepted and this loop would simply allocate it all.
    for (int i = 0; i < 1024; ++i)
    {
        const size_t wrote = ds.write_main_ram(ds.user, 0x06000000u, chunk.data(), chunk.size());
        if (wrote == chunk.size()) { ++accepted; }
        else if (wrote == 0)       { ++refused; break; }
        else                       { CHECK(false && "a poke was partially accepted"); break; }
    }
    CHECK(accepted > 0);            // a fresh queue takes work
    CHECK(refused == 1);            // and stops taking it well before 64 MiB
    CHECK(accepted < 1024);

    // Sound-RAM pokes are budgeted separately, so a full work-RAM queue does not block them.
    if (ds.write_sound_ram)
    {
        CHECK(ds.write_sound_ram(ds.user, 0, chunk.data(), chunk.size()) == chunk.size());
    }

    if (ds.close) ds.close(ds.user);
    server.Stop();
}

}  // namespace

int main()
{
    TestGenerationCountsEveryAttach();
    TestGenerationIsZeroForANonLiveSource();
    TestPokeQueueAppliesBackpressure();
    if (gFailures)
    {
        std::printf("LiveReconnectTests: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("LiveReconnectTests: all checks passed\n");
    return 0;
}
