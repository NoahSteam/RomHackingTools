// "Play From Here" needs a savestate for the frame the user scrubs to. This runs the whole path
// that produces one -- the real exporter (se_export.c), the real live driver over a socket, and
// the real FrameRecorder -- with the front end's per-frame order: latch a snapshot, record that
// frame, then hand over whatever savestate blocks have arrived. Only the emulator is faked.
//
// Each piece has its own test and passes; what none of them covers is the pieces together, which
// is where a live session found no recorded frame could ever be resumed. The exporter listens on
// a socket of this test's own (see the definitions in CMake), so it never touches a running
// emulator's.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

extern "C" {
#include "se_export.h"
}
#include "FrameRecorder.h"
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

// A stand-in for a Saturn savestate: large, mostly unchanged from one frame to the next, with a
// small part that moves. The size matters -- it is what the exporter's worker has to diff and
// compress between frames.
constexpr size_t kStateBytes = 1u << 20;
std::atomic<uint32_t> gSaves{ 0 };

extern "C" size_t FakeSave(unsigned char* buf, size_t cap)
{
    if (!buf) return kStateBytes;
    if (cap < kStateBytes) return 0;
    const uint32_t n = ++gSaves;
    std::memset(buf, 0x5A, kStateBytes);
    for (size_t i = 0; i < 4096; ++i) buf[i] = static_cast<unsigned char>(n + i);
    return kStateBytes;
}
extern "C" int FakeLoad(const unsigned char*, size_t) { return 0; }

void Frame()
{
    SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, nullptr);
}

void OnBlock(void* user, uint8_t kind, uint32_t frame, uint32_t base, uint32_t fullLen,
             const uint8_t* payload, uint32_t len)
{
    static_cast<sfe::FrameRecorder*>(user)->AttachStateBlock(frame, kind, base, fullLen, payload, len);
}
}  // namespace

int main()
{
    if (SeExportInit() != 0) { std::cerr << "SeExportInit failed\n"; return 1; }
    SeExportSetSaveStateHook(FakeSave);
    SeExportSetLoadStateHook(FakeLoad);

    const char* endpoint = SE_LIVE_DEFAULT_SOCK_PATH;
    se_data_source ds{};
    se_result r = SE_ERR_IO;
    for (int i = 0; i < 400 && r != SE_OK; ++i)
    {
        r = se_live_open(endpoint, &ds);
        if (r != SE_OK) Sleep(5);
    }
    if (r != SE_OK) { std::cerr << "could not open the live source\n"; SeExportDeinit(); return 1; }
    se_config cfg;
    cfg.abi_version = SE_ABI_VERSION;
    cfg.reserved = 0;
    se_context* ctx = se_create(&ds, &cfg);
    if (!ctx) { std::cerr << "se_create failed\n"; SeExportDeinit(); return 1; }

    // The emulate thread: about 60 frames a second, through the gate like the real one.
    std::atomic<bool> stop{ false };
    std::thread emu([&] {
        while (!stop.load())
        {
            if (!SeExportGateFrame()) continue;
            Frame();
            Sleep(16);
        }
    });

    sfe::FrameRecorder rec;
    rec.Configure(300);

    // The front end's loop, ~60 times a second: latch, record, drain.
    const auto begin = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - begin < std::chrono::seconds(6))
    {
        se_begin_frame(ctx);
        rec.Capture(ctx, se_frame_number(ctx));
        se_live_drain_state_blocks(&ds, &OnBlock, &rec);
        Sleep(16);
    }
    // Pause, as the user does, and let what is already in flight land.
    se_frame_pause(ctx);
    for (int i = 0; i < 30; ++i)
    {
        se_begin_frame(ctx);
        se_live_drain_state_blocks(&ds, &OnBlock, &rec);
        Sleep(16);
    }

    const sfe::FrameRecorder::StateStats st = rec.GetStateStats();
    std::printf("frames %zu, with a block %zu, resumable %zu; blocks received %llu "
                "(invalid %llu, no such frame %llu)\n",
                st.frames, st.withState, st.resumable,
                static_cast<unsigned long long>(st.received),
                static_cast<unsigned long long>(st.invalid),
                static_cast<unsigned long long>(st.noFrame));

    Check(st.received > 0, "the emulator's savestate blocks reach the front end");
    Check(st.frames >= 100, "frames were recorded");
    // Most recorded frames must be resumable. Not all: the oldest may hang off a keyframe that
    // has aged out of the ring, and the newest have no block yet.
    Check(st.resumable * 2 >= st.frames, "at least half of the recorded frames can be resumed from");

    stop = true;
    emu.join();
    se_destroy(ctx);
    SeExportDeinit();
    if (gFailures == 0) std::printf("All RewindPipeline tests passed.\n");
    return gFailures == 0 ? 0 : 1;
}
