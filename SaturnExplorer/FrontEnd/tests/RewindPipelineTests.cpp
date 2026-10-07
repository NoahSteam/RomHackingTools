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
    SeExportEndFrame();   // the glue's last call of the frame: where the savestate is taken
}

// What the front end does once it has submitted a load: discard blocks from before it. Zero (no
// load submitted) accepts everything.
uint32_t gEpochFloor = 0;
uint32_t gStaleDropped = 0;

void OnBlock(void* user, uint8_t kind, uint32_t frame, uint32_t base, uint32_t fullLen,
             uint32_t epoch, const uint8_t* payload, uint32_t len)
{
    if (epoch < gEpochFloor) { ++gStaleDropped; return; }
    static_cast<sfe::FrameRecorder*>(user)->AttachStateBlock(frame, kind, base, fullLen, payload, len);
}

// One front end: a live connection and the recorder it feeds.
//
// How fast any of this runs depends on the machine -- a shared CI runner manages a few frames a
// second where a laptop does sixty -- so each phase runs until what it is for has happened, with a
// ceiling, and the checks are about proportions rather than absolute counts.
struct Client
{
    se_data_source        ds{};
    se_context*           ctx = nullptr;
    sfe::FrameRecorder    rec;

    ~Client() { Close(); }
    void Close() { if (ctx) { se_destroy(ctx); ctx = nullptr; } }

    bool Open(const char* endpoint)
    {
        se_result r = SE_ERR_IO;
        for (int i = 0; i < 400 && r != SE_OK; ++i)
        {
            r = se_live_open(endpoint, &ds);
            if (r != SE_OK) Sleep(5);
        }
        if (r != SE_OK) return false;
        se_config cfg;
        cfg.abi_version = SE_ABI_VERSION;
        cfg.reserved = 0;
        ctx = se_create(&ds, &cfg);
        rec.Configure(300);
        return ctx != nullptr;
    }

    // One pass of the front end's per-frame loop, ~60 times a second: read the load counters,
    // latch a snapshot, record it, drain the savestate blocks.
    void Tick()
    {
        // The counters are read BEFORE the snapshot is latched, as the front end does: the driver
        // only moves forward, so what is latched is at least as new as what was read. The other way
        // round can see "landed" while still holding the frame that was left, record it, and have
        // the recorder refuse everything up to its number as already seen.
        uint32_t d = 0, f = 0;
        const bool signal = se_live_restore_state(&ds, &d, &f) != 0;
        se_begin_frame(ctx);
        // Not while a load is outstanding: the display still shows the frame that was left.
        if (gEpochFloor == 0 || (signal && d + f >= gEpochFloor)) rec.Capture(ctx, se_frame_number(ctx));
        se_live_drain_state_blocks(&ds, &OnBlock, &rec);
        Sleep(16);
    }

    template <typename Done>
    bool RunUntil(Done done, int maxSeconds)
    {
        const auto start = std::chrono::steady_clock::now();
        while (!done() && std::chrono::steady_clock::now() - start < std::chrono::seconds(maxSeconds)) Tick();
        return done();
    }

    size_t FramesAfter(uint64_t frame) const
    {
        size_t n = 0;
        for (size_t i = 0; i < rec.Count(); ++i) if (rec.FrameNumber(i) > frame) ++n;
        return n;
    }
    size_t ResumableAfter(uint64_t frame) const
    {
        size_t n = 0;
        for (size_t i = 0; i < rec.Count(); ++i) if (rec.FrameNumber(i) > frame && rec.CanReconstruct(i)) ++n;
        return n;
    }
    bool MostlyResumable() const
    {
        const sfe::FrameRecorder::StateStats s = rec.GetStateStats();
        return s.resumable * 2 >= s.frames;
    }
};
}  // namespace

int main()
{
    if (SeExportInit() != 0) { std::cerr << "SeExportInit failed\n"; return 1; }
    SeExportSetSaveStateHook(FakeSave);
    SeExportSetLoadStateHook(FakeLoad);

    const char* endpoint = SE_LIVE_DEFAULT_SOCK_PATH;
    Client first;
    if (!first.Open(endpoint)) { std::cerr << "could not open the live source\n"; SeExportDeinit(); return 1; }
    sfe::FrameRecorder& rec = first.rec;

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

    Check(first.RunUntil([&] { return rec.Count() >= 30; }, 90), "frames were recorded");

    // Pause, as the user does, and let what is already in flight land.
    se_frame_pause(first.ctx);
    first.RunUntil([&] { return first.MostlyResumable(); }, 30);

    const sfe::FrameRecorder::StateStats before = rec.GetStateStats();
    Check(before.received > 0, "the emulator's savestate blocks reach the front end");
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
    se_frame_resume(first.ctx);
    Sleep(400);
    size_t index = rec.Count() / 2;
    while (index + 1 < rec.Count() && !rec.CanReconstruct(index)) ++index;
    std::vector<uint8_t> state;
    Check(rec.CanReconstruct(index) && rec.ReconstructState(index, state), "a frame to rewind to");
    const uint64_t resumeFrame = rec.FrameNumber(index);
    uint32_t done = 0, failed = 0;
    Check(se_live_restore_state(&first.ds, &done, &failed) != 0, "the server reports restore counters");
    Check(se_load_state(first.ctx, resumeFrame, state.data(), state.size(), nullptr, 0) == SE_OK,
          "the load is accepted");
    rec.TruncateAfter(index);
    gEpochFloor = done + failed + 1;   // the front end's BeginRestoreWait
    const size_t keptFrames = rec.Count();

    first.RunUntil([&] { return first.FramesAfter(resumeFrame) >= 12; }, 90);
    se_frame_pause(first.ctx);
    first.RunUntil([&] { return first.ResumableAfter(resumeFrame) + 8 >= first.FramesAfter(resumeFrame); }, 30);

    // Every resumable frame must rebuild to a state the machine could have saved, on the timeline
    // its frame number belongs to: the old one up to the resume point, the new one after it. A
    // state from the abandoned timeline filed under a reused frame number would resume the game
    // into a future that already did not happen.
    const size_t recordedAfter = first.FramesAfter(resumeFrame);
    size_t after = 0, corrupt = 0, wrongTimeline = 0;
    for (size_t i = 0; i < rec.Count(); ++i)
    {
        const bool isAfter = rec.FrameNumber(i) > resumeFrame;
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
    Check(recordedAfter >= 12, "enough frames were recorded after the rewind to judge it");
    Check(after > 0, "frames recorded after the rewind can be resumed from");
    // All but the newest few, whose blocks are still on their way when the test stops.
    Check(after + 8 >= recordedAfter, "nearly every frame recorded after the rewind can be resumed from");
    Check(corrupt == 0, "no frame rebuilds to a state the machine could not have saved");
    Check(wrongTimeline == 0, "no frame rebuilds to a state of the other timeline");
    // Only a fast enough machine has blocks of the old timeline still in flight when the load goes
    // in; on a slow one there is nothing to drop and the checks above are the whole test.
    if (gStaleDropped == 0) std::printf("note: no blocks of the abandoned timeline were in flight this run\n");

    // --- A client that joins an emulator that has been running a while ---
    // The exporter's deltas are measured against a keyframe sent some time ago. A newcomer never
    // saw it, so unless the exporter starts it a keyframe of its own, nothing it records can be
    // rebuilt until the next one comes round (hundreds of frames away).
    first.Close();   // the first client leaves; the emulator carries on
    gEpochFloor = 0; // a new client has submitted no load
    Client late;
    const bool attached = late.Open(endpoint);
    Check(attached, "a second client attaches mid-run");
    if (attached)
    {
        late.RunUntil([&] { return late.rec.Count() >= 16; }, 90);
        late.RunUntil([&] { return late.MostlyResumable(); }, 30);   // let the newest blocks arrive
        const sfe::FrameRecorder::StateStats s = late.rec.GetStateStats();
        std::printf("late joiner: frames %zu, resumable %zu\n", s.frames, s.resumable);
        Check(s.frames >= 8, "the late joiner recorded frames");
        Check(s.resumable * 2 >= s.frames, "a client that joins mid-run can resume from most of what it records");
    }
    late.Close();

    stop = true;
    emu.join();
    SeExportDeinit();
    if (gFailures == 0) std::printf("All RewindPipeline tests passed.\n");
    return gFailures == 0 ? 0 : 1;
}
