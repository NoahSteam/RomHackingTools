// Emulator pokes, end to end: the real exporter (se_export.c) on one side of a unix socket, the
// real live driver on the other, the emulator's memory writers faked. What this pins:
//  - a poke is applied by the emulate thread at the frame gate, never by the server thread while
//    the cores run (the hooks record which thread called them);
//  - a CRAM / VDP1 frame-buffer poke -- which the emulator's ordinary bus writer silently drops --
//    is refused until the server says it has a VDP writer, then applied and visible on the next
//    capture;
//  - the server counts what it applied and what it could not, and the driver counts what it
//    accepted and never delivered;
//  - a state load discards the pokes still queued against the state it replaces, instead of
//    shipping them after it.
#if defined(_WIN32)
#error "LivePokeTests is POSIX-only: it listens on a unix socket. CMake builds it only where that exists."
#endif
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

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

// ---- the fake emulator -------------------------------------------------------------------------
struct Event { char kind; unsigned addr; unsigned char val; std::thread::id tid; };
std::mutex gMtx;
std::vector<Event> gEvents;
uint16_t gCram[2048];          // host-order words, like Mednafen's CRAM[]
const std::thread::id kMain = std::this_thread::get_id();

void Log(char kind, unsigned addr, unsigned char val)
{
    std::lock_guard<std::mutex> lk(gMtx);
    gEvents.push_back({ kind, addr, val, std::this_thread::get_id() });
}
extern "C" void HookMain(unsigned a, unsigned char v) { Log('P', a, v); }
extern "C" void HookSound(unsigned o, unsigned char v) { Log('S', o, v); }
extern "C" void HookVdp(unsigned a, unsigned char v)
{
    Log('V', a, v);
    if (a >= 0x05F00000u && a < 0x05F80000u)
    {
        const unsigned off = a - 0x05F00000u, w = (off >> 1) & 0x7FF;
        gCram[w] = (off & 1) ? uint16_t((gCram[w] & 0xFF00) | v) : uint16_t((gCram[w] & 0x00FF) | (v << 8));
    }
}
extern "C" size_t FakeSave(unsigned char* buf, size_t cap)
{
    if (!buf) return 64;
    if (cap < 64) return 0;
    std::memset(buf, 0xA5, 64);
    return 64;
}
extern "C" int FakeLoad(const unsigned char*, size_t) { Log('L', 0, 0); return 0; }

size_t Count(char kind)
{
    std::lock_guard<std::mutex> lk(gMtx);
    size_t n = 0;
    for (const Event& e : gEvents) n += e.kind == kind;
    return n;
}

// What the app's per-frame loop does on the emulator side: gate (which applies pokes), then a frame.
void EmulatorTick()
{
    SeExportGateFrame();
    SeExportSnapshot(nullptr, nullptr, gCram, nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, nullptr);
}

struct Info { uint32_t applied = 0, dropped = 0, lost = 0, caps = 0; bool known = false; };
Info Poll(const se_data_source& ds)
{
    Info i;
    i.known = se_live_poke_info(&ds, &i.applied, &i.dropped, &i.lost, &i.caps) != 0;
    return i;
}

template <typename Pred>
bool Until(Pred pred, int ticks = 600)
{
    for (int i = 0; i < ticks; ++i)
    {
        EmulatorTick();
        if (pred()) return true;
        Sleep(5);
    }
    return false;
}
}  // namespace

int main()
{
    if (SeExportInit() != 0) { std::cerr << "SeExportInit failed\n"; return 1; }
    SeExportSetMemWriteHook(HookMain);
    SeExportSetSoundWriteHook(HookSound);
    SeExportSetSaveStateHook(FakeSave);
    SeExportSetLoadStateHook(FakeLoad);

    se_data_source ds{};
    se_result r = SE_ERR_IO;
    for (int i = 0; i < 400 && r != SE_OK; ++i)
    {
        r = se_live_open(SE_LIVE_DEFAULT_SOCK_PATH, &ds);
        if (r != SE_OK) Sleep(5);
    }
    if (r != SE_OK) { std::cerr << "could not open the live source\n"; SeExportDeinit(); return 1; }
    se_config cfg;
    cfg.abi_version = SE_ABI_VERSION;
    cfg.reserved = 0;
    se_context* ctx = se_create(&ds, &cfg);
    if (!ctx) { std::cerr << "se_create failed\n"; SeExportDeinit(); return 1; }

    Check(Until([&] { return Poll(ds).known; }), "the server reports poke info (v23)");
    Check(se_begin_frame(ctx) == SE_OK, "a first capture");
    Info base = Poll(ds);
    Check(!(base.caps & SE_LIVE_CAP_VDP_POKE), "no VDP writer is wired yet: the capability bit is clear");

    // H1: a CRAM edit is refused outright while the server cannot apply it, not accepted and lost.
    const uint8_t v = 0x42;
    Check(se_write_vram(ctx, SE_VRAM_KIND_CRAM, 0x10, &v, 1) == 0, "a CRAM edit is refused without a VDP writer");
    uint8_t got = 0xEE;
    se_read_vram(ctx, SE_VRAM_KIND_CRAM, 0x10, &got, 1);
    Check(got == 0, "and the view does not show an edit the emulator never got");
    Check(se_write_vram(ctx, SE_VRAM_KIND_VDP1_FB, 0x20, &v, 1) == 0, "nor a frame-buffer edit");

    // The same edit once the server has a VDP writer.
    SeExportSetVdpWriteHook(HookVdp);
    Check(Until([&] { return (Poll(ds).caps & SE_LIVE_CAP_VDP_POKE) != 0; }), "the capability bit appears");
    Check(se_write_vram(ctx, SE_VRAM_KIND_CRAM, 0x10, &v, 1) == 1, "a CRAM edit is accepted with a VDP writer");

    // M2: nothing is written until the emulate thread runs its gate.
    Sleep(300);
    Check(Count('V') == 0, "the server thread did not write the poke while the emulator was not at a gate");
    EmulatorTick();
    Check(Count('V') == 1, "the gate applied it");
    {
        std::lock_guard<std::mutex> lk(gMtx);
        const Event* e = nullptr;
        for (const Event& x : gEvents) if (x.kind == 'V') e = &x;
        Check(e && e->addr == 0x05F00010u && e->val == 0x42, "to the CRAM bus address with the byte");
        Check(e && e->tid == kMain, "on the emulate thread, not the server's");
    }

    // The wired poke reads back on the next capture.
    Check(Until([&] { se_begin_frame(ctx); uint8_t b = 0; se_read_vram(ctx, SE_VRAM_KIND_CRAM, 0x10, &b, 1); return b == 0x42; }),
          "the poke is in the next capture");

    // A work-RAM poke takes the same road.
    const uint8_t w = 0x7E;
    Check(se_write_vram(ctx, SE_VRAM_KIND_WRAM_HIGH, 0x30, &w, 1) == 1, "a work-RAM edit is accepted");
    Sleep(300);
    Check(Count('P') == 0, "work RAM is not written from the server thread either");
    Check(Until([&] { return Count('P') == 1; }), "the gate applies it");
    {
        std::lock_guard<std::mutex> lk(gMtx);
        bool onMain = true;
        for (const Event& x : gEvents) if (x.kind == 'P' && x.tid != kMain) onMain = false;
        Check(onMain, "on the emulate thread");
    }

    // Counters: what the server applied, and a poke it had no writer for.
    Check(Until([&] { return Poll(ds).applied >= base.applied + 2; }), "the server counts applied pokes");
    const Info mid = Poll(ds);
    SeExportSetMemWriteHook(nullptr);
    Check(se_write_vram(ctx, SE_VRAM_KIND_WRAM_HIGH, 0x31, &w, 1) == 1, "the client cannot know the writer is gone");
    Check(Until([&] { return Poll(ds).dropped == mid.dropped + 1; }), "the server counts a poke it could not apply as dropped");
    Check(Poll(ds).applied == mid.applied, "and does not count it as applied");
    SeExportSetMemWriteHook(HookMain);

    // L2: pokes queued when a state load is requested belong to the state being replaced.
    {
        const size_t before = Count('P');
        // Straight to the driver's sink: through se_write_vram each edit also re-derives the
        // picture, slowly enough for the poll thread to ship every poke before the load is asked.
        constexpr unsigned kPokes = 5000;
        for (unsigned i = 0; i < kPokes; ++i)
        {
            const uint8_t b = uint8_t(i);
            if (ds.write_main_ram(ds.user, 0x06000100u + i, &b, 1) != 1) { Check(false, "queueing a poke"); break; }
        }
        const std::vector<uint8_t> state(64, 0x5A);
        Check(se_load_state(ctx, 7, state.data(), state.size(), nullptr, 0) == SE_OK, "the load request is accepted");
        Check(Until([&] { return Count('L') == 1; }), "the load is applied");
        // Give anything still wrongly queued time to ship and be applied after it.
        for (int i = 0; i < 100; ++i) { EmulatorTick(); Sleep(5); }
        size_t loadAt = 0, afterLoad = 0;
        {
            std::lock_guard<std::mutex> lk(gMtx);
            for (size_t i = 0; i < gEvents.size(); ++i) if (gEvents[i].kind == 'L') loadAt = i;
            for (size_t i = loadAt + 1; i < gEvents.size(); ++i) afterLoad += gEvents[i].kind == 'P';
        }
        Check(afterLoad == 0, "no queued poke is applied after the load that replaced its state");
        Check(Count('P') - before < kPokes, "the load discarded the queue instead of draining it first");
    }

    se_destroy(ctx);
    SeExportDeinit();
    if (gFailures) { std::cerr << gFailures << " check(s) failed\n"; return 1; }
    std::cout << "LivePokeTests passed\n";
    return 0;
}
