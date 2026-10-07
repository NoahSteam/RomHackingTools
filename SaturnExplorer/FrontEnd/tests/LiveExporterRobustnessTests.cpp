// The emulator-side tap (se_export.c) against hostile or slow peers, and the frame-step
// accounting it shares with the live driver. Real exporter, real driver, the default unix
// socket -- the same arrangement as LiveRestoreSignalTests.
//
//  - A client that vanishes mid-reply must not take the emulator down (SIGPIPE).
//  - A client that connects and says nothing must not pin the exporter's shutdown.
//  - Frame steps granted from the server thread and consumed on the emulate thread must add up.
//  - The driver can tell when a step has been run AND published, rather than counting UI frames.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

extern "C" {
#include "se_export.h"
}
#include "LiveDriver.h"
#include "SeLiveProtocol.h"
#include "saturnexplorer/SaturnExplorer.h"

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

template <typename Fn>
bool WaitFor(Fn pred, int budgetMs = 5000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (pred()) return true;
        Sleep(2);
    }
    return pred();
}

void Frame()
{
    SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, nullptr);
}

// The emulate thread: gate, then (when released) run and publish one frame.
struct Emulator
{
    std::atomic<bool>     stop{false};
    std::atomic<uint32_t> ran{0};     // frames that went through the gate AND were published
    std::atomic<int>      frameMs{0}; // how long a frame takes to "emulate"
    std::thread           thread;

    void Start()
    {
        thread = std::thread([this] {
            while (!stop.load())
            {
                if (!SeExportGateFrame()) continue;   // the gate sleeps ~2 ms itself
                const int ms = frameMs.load();
                if (ms) Sleep(ms);
                Frame();
                ++ran;
            }
        });
    }
    ~Emulator()
    {
        stop = true;
        if (thread.joinable()) thread.join();
    }
};

int ConnectRaw()
{
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, SE_LIVE_DEFAULT_SOCK_PATH, sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        ::close(fd);
        return -1;
    }
    return fd;
}

int ConnectRawRetry()
{
    for (int i = 0; i < 400; ++i)
    {
        const int fd = ConnectRaw();
        if (fd >= 0) return fd;
        Sleep(5);
    }
    return -1;
}

bool SendGet(int fd)
{
    uint8_t req[SE_LIVE_REQUEST_LEN] = { 'G', 'E', 'T', ' ', 0, 0, 0, 0 };
    return ::send(fd, req, sizeof(req), MSG_NOSIGNAL) == static_cast<ssize_t>(sizeof(req));
}

// A client that asks for a snapshot and closes without reading it. The exporter then writes a
// reply of well over a megabyte to a peer that is gone: after the first chunk the peer's kernel
// answers with a reset, and the next send() is the one that raises SIGPIPE.
void TestVanishingClientDoesNotKillTheEmulator()
{
    for (int i = 0; i < 40; ++i)
    {
        const int fd = ConnectRawRetry();
        if (fd < 0) { Check(false, "could not connect a raw client"); return; }
        SendGet(fd);
        ::close(fd);
        Sleep(5);
    }
    // Still here (the default SIGPIPE disposition is untouched), and still serving.
    const int fd = ConnectRawRetry();
    Check(fd >= 0, "the exporter accepts a new client afterwards");
    if (fd < 0) return;
    Check(SendGet(fd), "request sent");
    uint8_t magic[4] = {};
    size_t got = 0;
    while (got < 4)
    {
        const ssize_t n = ::recv(fd, magic + got, 4 - got, 0);
        if (n <= 0) break;
        got += static_cast<size_t>(n);
    }
    Check(got == 4 && magic[0] == SE_LIVE_MAGIC0 && magic[1] == SE_LIVE_MAGIC1 &&
          magic[2] == SE_LIVE_MAGIC2 && magic[3] == SE_LIVE_MAGIC3,
          "and it still answers with a snapshot");
    ::close(fd);
}

// Frame steps arrive on the server thread and are consumed on the emulate thread. Every
// requested frame must run exactly once: the old unlocked decrement could overwrite a concurrent
// grant, silently dropping a step.
void TestEveryRequestedStepRuns(se_data_source& ds)
{
    Emulator emu;
    emu.Start();
    Check(ds.frame_pause(ds.user) == 0, "pause posted");
    Sleep(150);
    const uint32_t before = emu.ran.load();
    const int kSteps = 300;
    for (int i = 0; i < kSteps; ++i)
    {
        ds.frame_step(ds.user, 1);
        if (i % 3 == 0) Sleep(1);
    }
    Check(WaitFor([&] { return emu.ran.load() >= before + kSteps; }, 8000),
          "every requested step was run");
    Sleep(200);   // and none beyond what was asked
    Check(emu.ran.load() == before + kSteps, "no step ran twice");
    ds.frame_step(ds.user, 0);   // resume
    Sleep(100);
}

// A paused display follows a step to its end. The emulator is slow (frames take 30 ms), so the
// step outlasts any fixed number of UI frames; the driver has to report "pending" until the
// frames have been run and published and the display has caught up -- and then stop.
void TestStepCompletionIsObserved(se_data_source& ds, se_context* ctx)
{
    Emulator emu;
    emu.frameMs = 30;
    emu.Start();
    Check(ds.frame_pause(ds.user) == 0, "pause posted");
    Sleep(300);
    // Settle: capture until the display agrees with the paused emulator.
    for (int i = 0; i < 200 && se_live_capture_pending(&ds) == 1; ++i) { se_begin_frame(ctx); Sleep(5); }
    Check(se_live_capture_pending(&ds) == 0, "a paused, caught-up display has nothing pending");
    const uint64_t shownBefore = se_frame_number(ctx);
    const uint32_t ranBefore = emu.ran.load();
    // ...and it STAYS settled: the pending check must not keep a paused display capturing
    // forever (that would overwrite in-place edits).
    Sleep(300);
    Check(se_live_capture_pending(&ds) == 0, "and stays settled while nothing happens");

    const int kSteps = 4;
    // Capturing a frame is slow next to the gaps this has to catch (the span between the
    // emulator being told about the step and publishing its first frame is a fraction of one
    // frame), so a separate thread samples the pending state every millisecond for the whole
    // step. It does not capture, so it checks the driver/server half of the answer: 'pending' may
    // not drop to 0 until every step frame has been run.
    std::atomic<bool> posted{false}, monitorStop{false};
    std::atomic<int>  prematureZeros{0};
    std::thread monitor([&] {
        while (!monitorStop.load())
        {
            if (posted.load() && se_live_capture_pending(&ds) == 0 &&
                emu.ran.load() < ranBefore + kSteps)
            {
                ++prematureZeros;
            }
            Sleep(1);
        }
    });
    posted = true;
    Check(ds.frame_step(ds.user, kSteps) == 0, "step posted");
    bool zeroTooEarly = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;)
    {
        const int pending = se_live_capture_pending(&ds);
        if (pending == -1) { Check(false, "a v20 server reports step state"); break; }
        if (pending == 0)
        {
            // Stopped capturing: only legitimate once every step frame has been run...
            if (emu.ran.load() != ranBefore + kSteps) zeroTooEarly = true;
            break;
        }
        se_begin_frame(ctx);
        if (std::chrono::steady_clock::now() > deadline) { Check(false, "step completion never observed"); break; }
        Sleep(3);
    }
    monitorStop = true;
    monitor.join();
    Check(prematureZeros.load() == 0, "pending never dropped to 0 while step frames were outstanding");
    Check(!zeroTooEarly, "capture did not stop before the step finished");
    Check(emu.ran.load() == ranBefore + kSteps, "all step frames ran");
    // ...and the display shows the final frame, not an intermediate one.
    Check(se_frame_number(ctx) == shownBefore + kSteps, "the display ends on the stepped frame");
    ds.frame_step(ds.user, 0);
    Sleep(100);
}

// A halted emulator republishes the same machine state every poll. The driver reports whether the
// newest publish is content the display already shows, so a client can skip re-copying several MB
// to draw nothing new -- and must stop saying so the moment the content moves.
void TestUnchangedDisplayIsReported(se_data_source& ds, se_context* ctx)
{
    Emulator emu;
    emu.frameMs = 5;
    emu.Start();
    Check(ds.frame_pause(ds.user) == 0, "pause posted");
    Sleep(300);
    for (int i = 0; i < 200 && se_live_capture_pending(&ds) == 1; ++i) { se_begin_frame(ctx); Sleep(5); }
    se_begin_frame(ctx);

    Check(WaitFor([&] { return se_live_capture_unchanged(&ds) == 1; }),
          "a caught-up, paused display is reported unchanged");
    // The poll thread keeps publishing while paused; none of those publishes may read as new.
    bool flickered = false;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
    while (std::chrono::steady_clock::now() < until)
    {
        if (se_live_capture_unchanged(&ds) != 1) flickered = true;
        Sleep(2);
    }
    Check(!flickered, "republishing identical state does not look like a change");

    // A step moves the machine: the display is no longer current until it is captured again.
    Check(ds.frame_step(ds.user, 1) == 0, "step posted");
    Check(WaitFor([&] { return se_live_capture_unchanged(&ds) == 0; }),
          "a new frame is reported as a change");
    for (int i = 0; i < 400 && se_live_capture_pending(&ds) == 1; ++i) { se_begin_frame(ctx); Sleep(3); }
    se_begin_frame(ctx);
    Check(WaitFor([&] { return se_live_capture_unchanged(&ds) == 1; }),
          "and unchanged again once the display has caught up");
    ds.frame_step(ds.user, 0);   // resume
    Sleep(100);
}

// Shutdown with a client attached that never says anything: the server thread is parked in
// recv() on it. Closing the listening socket only wakes accept(), so the join used to wait
// until that client left. Run with a watchdog so a regression fails instead of hanging the suite.
void TestShutdownDoesNotWaitForASilentClient()
{
    const int fd = ConnectRawRetry();
    Check(fd >= 0, "silent client connected");
    if (fd < 0) return;
    Sleep(150);   // let the server accept it and park in recv()

    std::atomic<bool> done{false};
    std::thread watchdog([&] {
        for (int i = 0; i < 1000 && !done.load(); ++i) Sleep(10);
        if (!done.load())
        {
            std::cerr << "FAIL: SeExportDeinit is stuck behind a silent client\n";
            std::_Exit(2);
        }
    });
    const auto t0 = std::chrono::steady_clock::now();
    SeExportDeinit();
    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    done = true;
    watchdog.join();
    Check(took < std::chrono::milliseconds(3000), "deinit returns promptly with a silent client attached");
    ::close(fd);
}
}  // namespace

int main()
{
    if (SeExportInit() != 0) { std::cerr << "SeExportInit failed\n"; return 1; }

    TestVanishingClientDoesNotKillTheEmulator();

    se_data_source ds{};
    se_result r = SE_ERR_IO;
    for (int i = 0; i < 400 && r != SE_OK; ++i)
    {
        r = se_live_open(nullptr, &ds);
        if (r != SE_OK) Sleep(5);
    }
    if (r != SE_OK) { std::cerr << "could not open the live source\n"; SeExportDeinit(); return 1; }
    se_config cfg;
    cfg.abi_version = SE_ABI_VERSION;
    cfg.reserved = 0;
    se_context* ctx = se_create(&ds, &cfg);
    if (!ctx) { std::cerr << "se_create failed\n"; SeExportDeinit(); return 1; }

    {
        // Frames only get published while a client is attached; give the emulate thread a
        // moment of free running so there is a first frame to capture.
        Emulator warm;
        Check(WaitFor([&] { return se_live_connection_generation(&ds) >= 1u; }), "driver attached");
        warm.Start();
        Check(WaitFor([&] { return warm.ran.load() > 3; }), "frames flow");
    }
    se_begin_frame(ctx);

    TestEveryRequestedStepRuns(ds);
    TestStepCompletionIsObserved(ds, ctx);
    TestUnchangedDisplayIsReported(ds, ctx);

    se_destroy(ctx);   // the driver detaches; the exporter's client slot frees up
    Sleep(100);
    TestShutdownDoesNotWaitForASilentClient();

    if (gFailures) { std::cerr << gFailures << " check(s) failed\n"; return 1; }
    std::cout << "LiveExporterRobustnessTests passed\n";
    return 0;
}
