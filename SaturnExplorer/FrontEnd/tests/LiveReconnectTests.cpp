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
//
// Connection hygiene: work queued for one emulator must not reach its replacement, the new
// generation must arrive with the new data, a wedged emulator must not hold up shutdown, and a
// peer that hangs up mid-send must not take the process with it.
#include "FakeLiveServer.h"
#include "LiveDriver.h"
#include "SeLiveProtocol.h"   // descriptor lengths + the per-verb protocol maxima
#include "saturnexplorer/SaturnExplorer.h"
#include "saturnexplorer/SeHost.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <netdb.h>
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

using fakelive::Reply;
using fakelive::Request;

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

// Answer exactly one request, as a healthy emulator would, then do whatever 'after' says.
void AnswerOnce(int fd, const Reply& reply)
{
    Request r;
    if (!fakelive::ReadRequest(fd, r)) return;
    const std::vector<uint8_t> bytes = fakelive::Build(reply);
    fakelive::WriteExact(fd, bytes.data(), bytes.size());
}

// Keep a connection open without answering: the shape of an emulator that is suspended in a
// debugger or hung. Returns when the peer goes away or the server stops.
void StaySilent(int fd)
{
    char sink[256];
    while (::recv(fd, sink, sizeof(sink), 0) > 0) { }
}

// A server that accepts and immediately hangs up. The driver cannot read a snapshot from it,
// so it closes and reconnects -- which is exactly the shape of an emulator being stopped and
// another one launched on the same endpoint, minus the part where the new one answers.
void HangUp(int, int) {}

// Start a scripted server and attach the live driver to it, closing both however the test
// leaves.
//
// Ok() is false when the listener could not bind or the source would not open, and the test says
// so once. The destructor stops the server FIRST when asked to: a handler parked in recv() on a
// silent client is woken by the shutdown, which is also what lets close() be timed on its own.
class LiveFixture
{
public:
    // se_live_open probes the endpoint with a connect-and-drop before it starts its thread, so the
    // server's first connection is that probe, not the driver. It is hidden here: the handler is
    // called with the index of the DRIVER's connections (0 = its first attach), and Connections()
    // counts only those.
    explicit LiveFixture(fakelive::Server::Handler handler)
        : mServer([h = std::move(handler)](int fd, int index) {
              if (index > 0) h(fd, index - 1);
          })
    {
        if (!mServer.Start()) return;
        mOpened = (se_live_open(mServer.Endpoint().c_str(), &mSource) == SE_OK);
    }

    ~LiveFixture()
    {
        Close();
        mServer.Stop();
    }

    LiveFixture(const LiveFixture&) = delete;
    LiveFixture& operator=(const LiveFixture&) = delete;

    // Close the source; returns how long that took.
    std::chrono::milliseconds Close()
    {
        const auto t0 = std::chrono::steady_clock::now();
        if (mOpened && mSource.close) mSource.close(mSource.user);
        mOpened = false;
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0);
    }

    int                 Connections() { return mServer.Accepted() - 1; }
    bool                Ok() const { return mOpened; }
    se_data_source&     Source()   { return mSource; }
    fakelive::Server&   Server()   { return mServer; }

private:
    fakelive::Server mServer;
    se_data_source   mSource = {};
    bool             mOpened = false;
};

void TestGenerationCountsEveryAttach()
{
    // Each connection gets one real answer and is then dropped, so every attach is a complete
    // one: the generation only advances with a snapshot (see TestGenerationArrivesWithTheSnapshot).
    LiveFixture live([](int fd, int) { AnswerOnce(fd, Reply()); });
    CHECK(live.Ok());
    if (!live.Ok()) return;
    se_data_source& ds = live.Source();

    // First attach reports 1, not 0 -- a client must be able to tell "connected once" from
    // "never connected", because only the latter means there is nothing to drop.
    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) >= 1u; }));
    const uint32_t first = se_live_connection_generation(&ds);
    CHECK(first >= 1u);

    // The server keeps hanging up after one answer, so the driver keeps reattaching and the
    // count keeps climbing. This is the case the whole mechanism exists for.
    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) > first; }));
    CHECK(se_live_connection_generation(&ds) > first);
    CHECK(live.Connections() >= 2);

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
    LiveFixture live([](int fd, int) { AnswerOnce(fd, Reply()); StaySilent(fd); });
    CHECK(live.Ok());
    if (!live.Ok()) return;
    se_data_source& ds = live.Source();
    CHECK(ds.write_main_ram != nullptr);
    // Writes are only taken while an emulator is attached, i.e. once it has answered.
    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) >= 1u; }));

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

}

// The breakpoint/tracepoint entry points take a (pointer, count) pair straight across the C
// ABI, so neither part can be assumed: a null pointer with a nonzero count is undefined to
// build a range from, a count near UINT32_MAX overflows the size computation on a 32-bit
// size_t, and the allocation is otherwise sized by whatever the caller passed. None of these
// may crash, and a rejected call must leave the previously installed set alone.
void TestBreakpointApiRejectsBadPairs()
{
    LiveFixture live([](int fd, int) { AnswerOnce(fd, Reply()); StaySilent(fd); });
    CHECK(live.Ok());
    if (!live.Ok()) return;
    se_data_source& ds = live.Source();

    // A well-formed install first, so there is a set to protect.
    std::vector<uint8_t> good(4u * SE_LIVE_BKPT_DESC_LEN, 0x11);
    se_live_set_breakpoints(&ds, good.data(), 4);

    // Null with a nonzero count, and a count past the protocol maximum: both refused without
    // reading the pointer or sizing anything from the count.
    se_live_set_breakpoints(&ds, nullptr, 8);
    se_live_set_breakpoints(&ds, good.data(), SE_LIVE_MAX_BKPT_DESCS + 1);
    se_live_set_breakpoints(&ds, good.data(), 0xFFFFFFFFu);
    se_live_set_tracepoints(&ds, nullptr, 8);
    se_live_set_tracepoints(&ds, good.data(), SE_LIVE_MAX_TRACE_DESCS + 1);
    se_live_set_tracepoints(&ds, good.data(), 0xFFFFFFFFu);

    // Zero descriptors with a null pointer is the legitimate "clear everything" call, not a
    // malformed pair -- it must still be accepted.
    se_live_set_breakpoints(&ds, nullptr, 0);
    se_live_set_tracepoints(&ds, nullptr, 0);

    // Still alive and still usable: the driver did not crash, and a later well-formed install
    // is still accepted.
    se_live_set_breakpoints(&ds, good.data(), 2);
    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) >= 1u; }));
}

// LST to a server that does not know the verb desyncs the connection: the server ignores the
// verb and never consumes the attached payload, so its next reply is read from the middle of
// our bytes. The hang-up server never completes an exchange, so the negotiated version stays 0
// -- unknown, which must count as too old. The ABI entry point has to refuse on its own, because
// the UI's guard is not the only route to it.
void TestLoadStateRefusedWithoutANegotiatedVersion()
{
    LiveFixture live(HangUp);
    CHECK(live.Ok());
    if (!live.Ok()) return;
    se_data_source& ds = live.Source();
    CHECK(WaitFor([&] { return live.Connections() >= 1; }));
    CHECK(ds.load_state != nullptr);

    const std::vector<uint8_t> state(1024, 0x7E);
    CHECK(ds.load_state(ds.user, 1234, state.data(), state.size(), nullptr, 0) != 0);

    // Still usable afterwards -- a refusal is not a broken source.
    CHECK(ds.write_main_ram != nullptr);
}


// With no emulator attached there is nothing to apply a write, load or step to, and queueing
// them would apply them to whichever emulator answers next. They are refused instead.
void TestMutationsRefusedWhileNothingIsAttached()
{
    LiveFixture live(HangUp);
    CHECK(live.Ok());
    if (!live.Ok()) return;
    se_data_source& ds = live.Source();
    CHECK(WaitFor([&] { return live.Connections() >= 1; }));

    const uint8_t byte = 0x42;
    CHECK(ds.write_main_ram(ds.user, 0x06000000u, &byte, 1) == 0);
    CHECK(ds.write_sound_ram(ds.user, 0, &byte, 1) == 0);
    CHECK(ds.write_vram(ds.user, SE_VRAM_KIND_VDP1_VRAM, 0, &byte, 1) == 0);
    CHECK(ds.frame_pause(ds.user) != 0);
    CHECK(ds.frame_step(ds.user, 2) != 0);
    CHECK(se_live_connection_generation(&ds) == 0u);
}

// The scenario: the emulator exits with writes, a savestate load and step commands still queued,
// and another game is launched on the same endpoint. The poll thread reconnects on its own. None
// of the old session's work may run against the new machine -- and the first thing the new one is
// sent must be a plain GET, because nothing is known about it yet.
void TestQueuedWorkDoesNotReachTheReplacement()
{
    std::mutex logMtx;
    std::vector<std::string> verbsOnSecond;          // every verb the replacement received
    std::atomic<bool> secondRequestSeen{false};      // conn 0: the driver is now waiting on us
    std::atomic<bool> dropFirst{false};              // test -> server: hang up on conn 0
    std::atomic<bool> releaseSecond{false};          // test -> server: let conn 1 answer
    std::atomic<bool> secondAnswered{false};

    LiveFixture live([&](int fd, int index)
    {
        Request r;
        if (index == 0)
        {
            AnswerOnce(fd, Reply());                 // handshake
            if (!fakelive::ReadRequest(fd, r)) return;
            secondRequestSeen = true;                // ...and now say nothing, holding the driver
            while (!dropFirst.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return;                                  // hang up
        }
        // The replacement: slow to answer, so there is a window where the driver is connected
        // but has not heard from it.
        while (fakelive::ReadRequest(fd, r))
        {
            {
                std::lock_guard<std::mutex> lk(logMtx);
                verbsOnSecond.push_back(r.verb);
            }
            while (!releaseSecond.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            Reply rep; rep.frame = 100;
            const std::vector<uint8_t> bytes = fakelive::Build(rep);
            if (!fakelive::WriteExact(fd, bytes.data(), bytes.size())) return;
            secondAnswered = true;
        }
    });
    CHECK(live.Ok());
    if (!live.Ok()) return;
    se_data_source& ds = live.Source();

    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) >= 1u; }));
    CHECK(WaitFor([&] { return secondRequestSeen.load(); }));

    // The driver is parked waiting for a reply that will never come. Queue everything.
    const std::vector<uint8_t> big(64, 0x11);
    CHECK(ds.write_main_ram(ds.user, 0x06000000u, big.data(), big.size()) == big.size());
    CHECK(ds.write_sound_ram(ds.user, 0, big.data(), big.size()) == big.size());
    CHECK(ds.frame_step(ds.user, 3) == 0);
    const std::vector<uint8_t> state(256, 0x7E);
    CHECK(ds.load_state(ds.user, 77, state.data(), state.size(), nullptr, 0) == 0);
    se_live_emu_load_slot(&ds, 2);

    dropFirst = true;   // the old emulator goes away with all of that still queued
    CHECK(WaitFor([&] { return live.Connections() >= 2; }));

    // Connected to the replacement, which has not answered yet: nothing is accepted, and the
    // generation has not moved -- the client is still looking at the old emulator's data.
    CHECK(WaitFor([&] { std::lock_guard<std::mutex> lk(logMtx); return !verbsOnSecond.empty(); }));
    CHECK(ds.write_main_ram(ds.user, 0x06000000u, big.data(), big.size()) == 0);
    CHECK(ds.frame_step(ds.user, 1) != 0);
    CHECK(ds.load_state(ds.user, 5, state.data(), state.size(), nullptr, 0) != 0);
    CHECK(se_live_connection_generation(&ds) == 1u);

    releaseSecond = true;   // the replacement answers its first request
    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) == 2u; }));
    // Give the poll thread several cycles in which any carried-over work would be shipped.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    {
        std::lock_guard<std::mutex> lk(logMtx);
        CHECK(!verbsOnSecond.empty());
        // The first request is a plain GET, and what follows is only what this connection itself
        // needs (REW restates the rewind setting to a server that starts with capture on). Nothing
        // from the previous session: no stale WRM/WRS/STP/PAU/LST/ELS.
        CHECK(verbsOnSecond.front() == SE_LIVE_VERB_GET);
        for (const std::string& v : verbsOnSecond)
        {
            CHECK(v == SE_LIVE_VERB_GET || v == SE_LIVE_VERB_REWIND);
        }
    }
    // And a fresh edit, made now, is accepted and goes to this emulator.
    CHECK(ds.write_main_ram(ds.user, 0x06000000u, big.data(), big.size()) == big.size());
    CHECK(WaitFor([&] {
        std::lock_guard<std::mutex> lk(logMtx);
        for (const std::string& v : verbsOnSecond) if (v == SE_LIVE_VERB_WRITE) return true;
        return false;
    }));
}

// The generation must announce an emulator together with its data. Advancing it at connect time,
// before the first snapshot, lets the client reset its history and then capture the previous
// emulator's snapshot -- and drain its queued events -- straight back into it.
void TestGenerationArrivesWithTheSnapshot()
{
    std::atomic<bool> release{false};
    LiveFixture live([&](int fd, int index)
    {
        if (index == 0)
        {
            Reply r; r.events = 3; r.fill = 0xAA; r.frame = 10;
            AnswerOnce(fd, r);
            return;                                   // hang up; the events are still queued
        }
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        Reply r; r.fill = 0xBB; r.frame = 20;
        AnswerOnce(fd, r);
        StaySilent(fd);
    });
    CHECK(live.Ok());
    if (!live.Ok()) return;
    se_data_source& ds = live.Source();

    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) == 1u; }));
    CHECK(WaitFor([&] { return live.Connections() >= 2; }));   // attached, not yet answered
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    // Attached to the replacement but it has said nothing: still the old generation, still the
    // old data, so a client that has not reset yet is consistent with itself.
    CHECK(se_live_connection_generation(&ds) == 1u);
    uint8_t b = 0;
    CHECK(ds.read_vdp1_vram(ds.user, 0, &b, 1) == 1 && b == 0xAA);

    release = true;
    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) == 2u; }));
    // The generation and the snapshot arrived together.
    CHECK(ds.read_vdp1_vram(ds.user, 0, &b, 1) == 1 && b == 0xBB);
    // The previous emulator's undrained events are gone, not delivered into the new run.
    se_live_event ev[8];
    CHECK(se_live_poll_events(&ds, ev, 8) == 0u);
}

// One capture is one frame. The server stamps each reply with a frame number and fills VRAM with
// the matching byte, so a capture that mixes frames shows up as VDP1 and VDP2 disagreeing -- or
// as bytes that do not belong to the reported frame number.
void TestCaptureIsPinnedToOneSnapshot()
{
    LiveFixture live([](int fd, int)
    {
        uint64_t frame = 1;
        Request r;
        while (fakelive::ReadRequest(fd, r))
        {
            Reply rep; rep.frame = frame; rep.fill = static_cast<uint8_t>(frame);
            ++frame;
            const std::vector<uint8_t> bytes = fakelive::Build(rep);
            if (!fakelive::WriteExact(fd, bytes.data(), bytes.size())) return;
        }
    });
    CHECK(live.Ok());
    if (!live.Ok()) return;
    se_data_source& ds = live.Source();
    CHECK(ds.begin_capture != nullptr && ds.end_capture != nullptr);
    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) >= 1u; }));

    for (int i = 0; i < 20; ++i)
    {
        ds.begin_capture(ds.user);
        const uint64_t pinned = ds.frame_number(ds.user);
        uint8_t a = 0, b = 0;
        CHECK(ds.read_vdp1_vram(ds.user, 0, &a, 1) == 1);

        // Let the emulator move on while this capture is half read: another thread (which is not
        // pinned) waits until the newest frame is past the one we hold.
        bool moved = false;
        std::thread other([&] {
            moved = WaitFor([&] { return ds.frame_number(ds.user) > pinned; }, 3000);
        });
        other.join();
        CHECK(moved);

        CHECK(ds.read_vdp2_vram(ds.user, 0, &b, 1) == 1);
        CHECK(ds.frame_number(ds.user) == pinned);    // still the pinned frame, not the newest
        ds.end_capture(ds.user);
        CHECK(a == b);                                // same instant in both regions
        CHECK(a == static_cast<uint8_t>(pinned));     // and it is the frame the number names
    }
}

// The host tags what it captured with se_frame_number. That must be the frame the capture came
// from, not whatever the emulator has reached by the time the host asks.
void TestFrameNumberIsTheCapturedFrame()
{
    LiveFixture live([](int fd, int)
    {
        uint64_t frame = 1;
        Request r;
        while (fakelive::ReadRequest(fd, r))
        {
            Reply rep; rep.frame = frame++; rep.fill = 1;
            const std::vector<uint8_t> bytes = fakelive::Build(rep);
            if (!fakelive::WriteExact(fd, bytes.data(), bytes.size())) return;
        }
    });
    CHECK(live.Ok());
    if (!live.Ok()) return;
    se_data_source& ds = live.Source();
    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) >= 1u; }));

    se_config cfg;
    cfg.abi_version = SE_ABI_VERSION;
    cfg.reserved = 0;
    se_context* ctx = se_create(&ds, &cfg);
    CHECK(ctx != nullptr);
    if (!ctx) return;
    CHECK(se_begin_frame(ctx) == SE_OK);
    const uint64_t shown = se_frame_number(ctx);
    CHECK(shown > 0);
    CHECK(WaitFor([&] { return ds.frame_number(ds.user) > shown + 2; }, 3000));   // the emulator ran on
    CHECK(se_frame_number(ctx) == shown);                                         // the capture did not
    CHECK(se_begin_frame(ctx) == SE_OK);
    CHECK(se_frame_number(ctx) > shown);
    se_destroy(ctx);   // closes the data source
    // 'live' will not close it a second time: it only closes sources it still believes open.
    live.Source() = se_data_source{};
}

// The stop's sequence number rides with the stop, from the same displayed frame, so a client can
// tell a NEW halt from a re-report of the one it has -- including a second halt at the same PC. A
// server older than v21 numbers nothing, and the driver says so rather than inventing a zero.
void TestStopCarriesItsSequenceNumber()
{
    for (const uint32_t version : { SE_LIVE_VERSION, 20u })
    {
        LiveFixture live([version](int fd, int)
        {
            Request r;
            while (fakelive::ReadRequest(fd, r))
            {
                Reply rep;
                rep.version    = version;
                rep.frame      = 10;
                rep.paused     = 1;
                rep.stopReason = SE_LIVE_STOP_EXEC_BP;
                rep.stopCpu    = 1;
                rep.stopPc     = 0x06001234;
                rep.stopSeq    = 7;
                const std::vector<uint8_t> bytes = fakelive::Build(rep);
                if (!fakelive::WriteExact(fd, bytes.data(), bytes.size())) return;
            }
        });
        CHECK(live.Ok());
        if (!live.Ok()) return;
        se_data_source& ds = live.Source();
        CHECK(WaitFor([&] { return se_live_connection_generation(&ds) >= 1u; }));
        se_config cfg;
        cfg.abi_version = SE_ABI_VERSION;
        cfg.reserved = 0;
        se_context* ctx = se_create(&ds, &cfg);
        CHECK(ctx != nullptr);
        if (!ctx) return;
        CHECK(se_begin_frame(ctx) == SE_OK);

        uint32_t reason = 0, cpu = 0, pc = 0, seq = 99;
        CHECK(se_live_get_stop(&ds, &reason, &cpu, &pc) == 1);
        CHECK(reason == SE_LIVE_STOP_EXEC_BP && cpu == 1 && pc == 0x06001234);
        const int hasSeq = se_live_get_stop_seq(&ds, &seq);
        if (version >= 21u) { CHECK(hasSeq == 1); CHECK(seq == 7); }
        else                { CHECK(hasSeq == 0); }
        se_destroy(ctx);
        live.Source() = se_data_source{};
    }
}

// An emulator that keeps the connection open but stops answering must not hold up disconnect,
// and the final best-effort "resume" must not wait for a reply that will never come.
void TestCloseDoesNotWaitForASilentEmulator()
{
    std::atomic<bool> silent{false};
    LiveFixture live([&](int fd, int)
    {
        AnswerOnce(fd, Reply());
        Request r;
        if (!fakelive::ReadRequest(fd, r)) return;   // the next request: never answered
        silent = true;
        StaySilent(fd);
    });
    CHECK(live.Ok());
    if (!live.Ok()) return;
    se_data_source& ds = live.Source();
    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) >= 1u; }));
    CHECK(WaitFor([&] { return silent.load(); }));
    CHECK(ds.frame_pause(ds.user) == 0);   // we now "hold" the emulator, so close tries to resume it

    const auto took = live.Close();
    CHECK(took < std::chrono::milliseconds(3000));
}

// A peer that has closed makes a later send() raise SIGPIPE, whose default action ends the
// process. The shape that does it on Linux: the peer closes cleanly while the driver is idle
// (so the driver is in CLOSE_WAIT and its next send is accepted), the peer's kernel answers that
// with a reset, and the NEXT send -- the payload half of a two-part request -- fails with EPIPE.
// So: hold a reply back, queue a poke (header + payload = two sends), then let the server answer
// and close in one motion. The default signal disposition is left untouched on purpose; if
// SIGPIPE is not suppressed, this test never reports.
void TestPeerHangUpDuringSendDoesNotKillTheProcess()
{
    std::atomic<bool> held{false}, go{false};
    LiveFixture live([&](int fd, int index)
    {
        if (index > 0) { AnswerOnce(fd, Reply()); StaySilent(fd); return; }
        AnswerOnce(fd, Reply());
        Request r;
        if (!fakelive::ReadRequest(fd, r)) return;   // the next GET: held back
        held = true;
        while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const std::vector<uint8_t> bytes = fakelive::Build(Reply());
        fakelive::WriteExact(fd, bytes.data(), bytes.size());
        // returning closes the socket cleanly: nothing is left unread, so it is a FIN, not a reset
    });
    CHECK(live.Ok());
    if (!live.Ok()) return;
    se_data_source& ds = live.Source();
    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) >= 1u; }));
    CHECK(WaitFor([&] { return held.load(); }));

    const std::vector<uint8_t> poke(4096, 0x33);
    CHECK(ds.write_main_ram(ds.user, 0x06000000u, poke.data(), poke.size()) == poke.size());
    go = true;

    // If SIGPIPE killed the process we never get here. The driver should notice the dead
    // connection, drop it and attach to the next one.
    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) >= 2u; }, 8000));
}


// The window the queue-clearing does not cover: the UI captures the OLD emulator's frame, the
// connection is replaced, and only then does the UI commit an edit made against what it was
// showing. At that point the new session is attached and 'connected' is true again, so the edit
// would be accepted and applied to a machine it was never about. Edits are checked against the
// session of the display the editing thread last captured.
void TestEditFromTheOldDisplayIsRefused()
{
    std::atomic<bool> release{false};
    LiveFixture live([&](int fd, int index)
    {
        if (index == 0) { Reply r; r.fill = 0xAA; AnswerOnce(fd, r); return; }   // answer once, hang up
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        Reply r; r.fill = 0xBB; r.frame = 50;
        AnswerOnce(fd, r);
        // The replacement keeps answering so it stays attached.
        Request req;
        while (fakelive::ReadRequest(fd, req))
        {
            const std::vector<uint8_t> bytes = fakelive::Build(r);
            if (!fakelive::WriteExact(fd, bytes.data(), bytes.size())) return;
        }
    });
    CHECK(live.Ok());
    if (!live.Ok()) return;
    se_data_source& ds = live.Source();

    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) == 1u; }));
    // The UI captures the old emulator's frame.
    ds.begin_capture(ds.user);
    uint8_t shown = 0;
    CHECK(ds.read_vdp1_vram(ds.user, 0, &shown, 1) == 1 && shown == 0xAA);
    ds.end_capture(ds.user);

    CHECK(se_live_captured_generation(&ds) == 1u);   // the display is of session 1

    // The old emulator goes away and the replacement answers -- all before the UI recaptures.
    release = true;
    CHECK(WaitFor([&] { return se_live_connection_generation(&ds) == 2u; }));
    // The display is still of session 1 until the thread captures again: this is the number a
    // client must reconcile its per-session state against, not the connection's.
    CHECK(se_live_captured_generation(&ds) == 1u);
    CHECK(WaitFor([&] { return live.Connections() >= 2; }));

    // An edit made against the old display, committed now: refused, in every form.
    const uint8_t byte = 0x42;
    const std::vector<uint8_t> state(256, 0x7E);
    CHECK(ds.write_main_ram(ds.user, 0x06000000u, &byte, 1) == 0);
    CHECK(ds.write_sound_ram(ds.user, 0, &byte, 1) == 0);
    CHECK(ds.write_vram(ds.user, SE_VRAM_KIND_VDP1_VRAM, 0, &byte, 1) == 0);
    CHECK(ds.frame_pause(ds.user) != 0);
    CHECK(ds.frame_step(ds.user, 1) != 0);
    CHECK(ds.load_state(ds.user, 1, state.data(), state.size(), nullptr, 0) != 0);
    CHECK(se_live_emu_load_slot(&ds, 1) != 0);

    // Once the UI has captured the new session's first frame, edits are accepted again.
    ds.begin_capture(ds.user);
    CHECK(ds.read_vdp1_vram(ds.user, 0, &shown, 1) == 1 && shown == 0xBB);
    CHECK(se_live_captured_generation(&ds) == 2u);   // pinned: already the new session inside the capture
    ds.end_capture(ds.user);
    CHECK(se_live_captured_generation(&ds) == 2u);
    CHECK(ds.write_main_ram(ds.user, 0x06000000u, &byte, 1) == 1);
    CHECK(se_live_emu_load_slot(&ds, 1) == 0);
    CHECK(se_live_emu_load_slot(&ds, 2) != 0);   // one load at a time: a second would replace the first unsent
}

// A host-name lookup has no timeout and cannot be interrupted, so it must never be what a thread
// of ours is parked in: opening gives up after a bound, and closing does not wait for a lookup
// the reconnect thread is in the middle of.
std::atomic<int>  gResolveCalls{0};
std::atomic<int>  gResolveAllowed{0};      // how many calls resolve normally before the rest stall
std::atomic<bool> gResolveRelease{false};
extern "C" int TestResolver(const char* host, const char* port, const addrinfo* hints, addrinfo** res)
{
    const int n = gResolveCalls.fetch_add(1);
    if (n < gResolveAllowed.load()) return ::getaddrinfo("127.0.0.1", port, hints, res);
    while (!gResolveRelease.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    (void)host;
    return EAI_FAIL;
}

void TestOpenDoesNotWaitOnAStalledLookup()
{
    gResolveCalls = 0; gResolveAllowed = 0; gResolveRelease = false;
    se_live_test_set_resolver(TestResolver);
    se_data_source ds = {};
    const auto t0 = std::chrono::steady_clock::now();
    const se_result r = se_live_open("tcp:stalls.invalid:6845", &ds);
    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    CHECK(r != SE_OK);
    CHECK(took < std::chrono::milliseconds(6000));   // bounded (the lookup itself never returns)
    CHECK(gResolveCalls.load() == 1);
    gResolveRelease = true;                          // let the abandoned lookup finish
    se_live_test_set_resolver(nullptr);
    if (r == SE_OK && ds.close) ds.close(ds.user);
}

// A client that retries while the lookup is stalled must not leave a resolver thread behind per
// attempt: attempts at the same host share the one in flight, and a different host past the
// concurrency cap is refused instead of starting yet another.
void TestStalledLookupsDoNotAccumulateThreads()
{
    // Let the previous test's abandoned lookup finish before the release flag is reused.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    gResolveCalls = 0; gResolveAllowed = 0; gResolveRelease = false;
    se_live_test_set_resolver(TestResolver);
    // Attempts run together (each waits out the lookup bound), as a retrying client's would
    // overlap an earlier attempt's abandoned lookup.
    auto attempt = [](const char* endpoint) {
        se_data_source ds = {};
        CHECK(se_live_open(endpoint, &ds) != SE_OK);
    };
    {
        std::thread t1(attempt, "tcp:stalls-shared.invalid:6845"), t2(attempt, "tcp:stalls-shared.invalid:6845"),
                    t3(attempt, "tcp:stalls-shared.invalid:6845");
        t1.join(); t2.join(); t3.join();
    }
    CHECK(gResolveCalls.load() == 1);   // three attempts, one lookup

    // Other hosts: each is its own lookup, up to the cap and no further. The first host's
    // lookup is still stalled and holds a slot.
    {
        std::thread t1(attempt, "tcp:other-a.invalid:6845"), t2(attempt, "tcp:other-b.invalid:6845"),
                    t3(attempt, "tcp:other-c.invalid:6845");
        t1.join(); t2.join(); t3.join();
    }
    CHECK(gResolveCalls.load() <= 2);   // the stalled host holds one slot; one more fits

    gResolveRelease = true;             // let them finish, and free their slots
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    gResolveCalls = 0; gResolveAllowed = 0; gResolveRelease = true;
    se_data_source d = {};
    se_live_open("tcp:after.invalid:6845", &d);
    CHECK(gResolveCalls.load() == 1);   // slots were released, so a new lookup starts again
    se_live_test_set_resolver(nullptr);
}

void TestCloseDoesNotWaitOnAStalledLookup()
{
    gResolveCalls = 0; gResolveAllowed = 2; gResolveRelease = false;   // probe + first attach resolve
    se_live_test_set_resolver(TestResolver);
    fakelive::Server server([](int fd, int index) { if (index > 0) AnswerOnce(fd, Reply()); });
    CHECK(server.Start());
    const std::string endpoint = "tcp:stalls.invalid:" + server.Endpoint().substr(server.Endpoint().rfind(':') + 1);
    se_data_source ds = {};
    CHECK(se_live_open(endpoint.c_str(), &ds) == SE_OK);
    // The first connection answers once and hangs up; the reconnect then stalls in the lookup.
    CHECK(WaitFor([&] { return gResolveCalls.load() >= 3; }));
    const auto t0 = std::chrono::steady_clock::now();
    if (ds.close) ds.close(ds.user);
    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    CHECK(took < std::chrono::milliseconds(2000));
    gResolveRelease = true;
    se_live_test_set_resolver(nullptr);
    server.Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));   // the abandoned lookup finishes
}

}  // namespace

int main()
{
    TestGenerationCountsEveryAttach();
    TestGenerationIsZeroForANonLiveSource();
    TestPokeQueueAppliesBackpressure();
    TestBreakpointApiRejectsBadPairs();
    TestLoadStateRefusedWithoutANegotiatedVersion();
    TestMutationsRefusedWhileNothingIsAttached();
    TestQueuedWorkDoesNotReachTheReplacement();
    TestGenerationArrivesWithTheSnapshot();
    TestCaptureIsPinnedToOneSnapshot();
    TestFrameNumberIsTheCapturedFrame();
    TestStopCarriesItsSequenceNumber();
    TestCloseDoesNotWaitForASilentEmulator();
    TestPeerHangUpDuringSendDoesNotKillTheProcess();
    TestEditFromTheOldDisplayIsRefused();
    TestOpenDoesNotWaitOnAStalledLookup();
    TestStalledLookupsDoNotAccumulateThreads();
    TestCloseDoesNotWaitOnAStalledLookup();
    if (gFailures)
    {
        std::printf("LiveReconnectTests: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("LiveReconnectTests: all checks passed\n");
    return 0;
}
