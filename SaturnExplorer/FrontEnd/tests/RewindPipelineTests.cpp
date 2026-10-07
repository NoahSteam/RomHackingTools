// "Play From Here" needs a savestate for the frame the user scrubs to. This runs the whole path
// that produces one -- the real exporter (se_export.c), the real live driver over a socket, and
// the real FrameRecorder -- with the front end's per-frame order: latch a snapshot, record that
// frame, then hand over whatever savestate blocks have arrived. Only the emulator is faked.
//
// Each piece has its own test and passes; what none of them covers is the pieces together, which
// is where a live session found no recorded frame could ever be resumed. The exporter listens on
// a socket of this test's own (see the definitions in CMake), so it never touches a running
// emulator's. POSIX only, like the other tests that stand up the exporter: CMake builds it
// where the unix-socket transport exists, and the Windows driver's named pipe is not covered.
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
// What a state of the fake machine looks like, built so a state that is wrong can be told from
// one that is right without knowing which frame it came from:
//  - a run of bytes that follows a pattern of its own (n, n+1, n+2, ...), different every save.
//    A delta applied to a keyframe other than the one it was taken against does not yield a
//    state with the pattern intact, so a mismatched pair shows up as corrupt.
//  - the timeline the machine is on. A load starts a new one, so a state from the timeline a
//    rewind abandoned is told apart from one of the new -- even a state that is itself intact,
//    which is the case a frame number cannot settle, since both timelines use the same numbers.
std::atomic<uint32_t> gTimeline{ 1 };
constexpr size_t kPatternBytes = 4096;
constexpr size_t kMarkerOffset = 100000;

extern "C" size_t FakeSave(unsigned char* buf, size_t cap)
{
    if (!buf) return kStateBytes;
    if (cap < kStateBytes) return 0;
    const uint32_t n = ++gSaves;
    std::memset(buf, 0x5A, kStateBytes);
    for (size_t i = 0; i < kPatternBytes; ++i) buf[i] = static_cast<unsigned char>(n + i);
    buf[kMarkerOffset] = static_cast<unsigned char>(gTimeline.load());
    return kStateBytes;
}
extern "C" int FakeLoad(const unsigned char*, size_t) { ++gTimeline; return 0; }

// True if a rebuilt state is one the machine could have saved.
bool Intact(const std::vector<uint8_t>& st)
{
    if (st.size() != kStateBytes) return false;
    for (size_t i = 1; i < kPatternBytes; ++i)
        if (static_cast<uint8_t>(st[i] - st[i - 1]) != 1) return false;
    return true;
}

void Frame()
{
    SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, nullptr);
}

// What the front end does once it has submitted a load: discard blocks from before it.
uint32_t gEpochFloor = 0;
uint32_t gStaleDropped = 0;

void OnBlock(void* user, uint8_t kind, uint32_t frame, uint32_t base, uint32_t fullLen,
             uint32_t epoch, const uint8_t* payload, uint32_t len)
{
    if (epoch < gEpochFloor) { ++gStaleDropped; return; }
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

    const sfe::FrameRecorder::StateStats before = rec.GetStateStats();
    Check(before.received > 0, "the emulator's savestate blocks reach the front end");
    Check(before.frames >= 40, "frames were recorded");
    // Most recorded frames must be resumable. Not all: the oldest may hang off a keyframe that
    // has aged out of the ring, and the newest have no block yet.
    Check(before.resumable * 2 >= before.frames, "at least half of the recorded frames can be resumed from");
    std::printf("before the rewind: frames %zu, with a block %zu, resumable %zu; blocks received %llu "
                "(invalid %llu, never recorded %llu)\n",
                before.frames, before.withState, before.resumable,
                static_cast<unsigned long long>(before.received),
                static_cast<unsigned long long>(before.invalid),
                static_cast<unsigned long long>(before.noFrame));

    // --- Rewind to the middle of what was recorded and carry on, as Play From Here does ---
    // First let the transport fill up with blocks of the timeline about to be abandoned: they are
    // the ones that arrive after the load and reuse the numbers of the frames recorded after it.
    se_frame_resume(ctx);
    Sleep(400);
    const size_t index = rec.Count() / 2;
    std::vector<uint8_t> state;
    Check(rec.CanReconstruct(index) && rec.ReconstructState(index, state), "a frame to rewind to");
    const uint64_t resumeFrame = rec.FrameNumber(index);
    uint32_t done = 0, failed = 0;
    Check(se_live_restore_state(&ds, &done, &failed) != 0, "the server reports restore counters");
    Check(se_load_state(ctx, resumeFrame, state.data(), state.size(), nullptr, 0) == SE_OK,
          "the load is accepted");
    rec.TruncateAfter(index);
    gEpochFloor = done + failed + 1;   // the front end's BeginRestoreWait
    const size_t keptFrames = rec.Count();

    const auto rewound = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - rewound < std::chrono::seconds(4))
    {
        // The counters are read BEFORE the snapshot is latched, as the front end does: the driver
        // only moves forward, so what is latched is at least as new as what was read. The other
        // way round can see "landed" while still holding the frame that was left, record it, and
        // have the recorder refuse everything up to its number as already seen.
        uint32_t d = 0, f = 0;
        const bool signal = se_live_restore_state(&ds, &d, &f) != 0;
        se_begin_frame(ctx);
        const bool landed = signal && d + f >= gEpochFloor;
        // Not while the load is outstanding: the display still shows the frame that was left.
        if (landed) rec.Capture(ctx, se_frame_number(ctx));
        se_live_drain_state_blocks(&ds, &OnBlock, &rec);
        Sleep(16);
    }
    se_frame_pause(ctx);
    for (int i = 0; i < 30; ++i)
    {
        se_begin_frame(ctx);
        se_live_drain_state_blocks(&ds, &OnBlock, &rec);
        Sleep(16);
    }

    // Every resumable frame must rebuild to a state the machine could have saved, on the timeline
    // its frame number belongs to: the old one up to the resume point, the new one after it. A
    // state from the abandoned timeline filed under a reused frame number would resume the game
    // into a future that already did not happen.
    size_t recordedAfter = 0, after = 0, corrupt = 0, wrongTimeline = 0;
    for (size_t i = 0; i < rec.Count(); ++i)
    {
        const bool isAfter = rec.FrameNumber(i) > resumeFrame;
        if (isAfter) ++recordedAfter;
        std::vector<uint8_t> out;
        if (!rec.CanReconstruct(i) || !rec.ReconstructState(i, out)) continue;
        if (!Intact(out)) { ++corrupt; continue; }
        const uint8_t want = isAfter ? gTimeline.load() : 1;
        if (out[kMarkerOffset] != want) { ++wrongTimeline; continue; }
        if (isAfter) ++after;
    }
    std::printf("rewind to #%llu: kept %zu, now %zu frames; resumable after it %zu of %zu, "
                "corrupt %zu, wrong timeline %zu; stale blocks dropped %u\n",
                static_cast<unsigned long long>(resumeFrame), keptFrames, rec.Count(), after,
                recordedAfter, corrupt, wrongTimeline, gStaleDropped);
    Check(rec.Count() > keptFrames, "recording continued after the rewind");
    Check(after > 0, "frames recorded after the rewind can be resumed from");
    // All but the newest few, whose blocks are still on their way when the test stops.
    Check(after + 8 >= recordedAfter, "nearly every frame recorded after the rewind can be resumed from");
    Check(corrupt == 0, "no frame rebuilds to a state the machine could not have saved");
    Check(wrongTimeline == 0, "no frame rebuilds to a state of the other timeline");
    Check(gStaleDropped > 0, "blocks from before the load were in flight, and were dropped");

    stop = true;
    emu.join();
    se_destroy(ctx);
    SeExportDeinit();
    if (gFailures == 0) std::printf("All RewindPipeline tests passed.\n");
    return gFailures == 0 ? 0 : 1;
}
