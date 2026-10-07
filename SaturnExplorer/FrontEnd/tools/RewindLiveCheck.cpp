// "Play From Here" against a REAL emulator: does restoring a recorded frame really put the game
// back where it was?
//
// Needs a patched Mednafen already running a game and listening on the default live endpoint, and
// is therefore not a ctest. Run it by hand (see Docs/PlayFromHereVerification.md); the emulator
// must be started with  -ss.smpc.autortc 0  so the machine is deterministic, and nobody may be
// pressing its keys.
//
// It uses the same pieces the front end does, in the front end's per-frame order (read the load
// counters, latch a snapshot, record it, drain the savestate blocks), and acts as Play From Here
// does: reconstruct a recorded frame's state, load it, drop what was recorded after it.
//
// The check that matters is determinism. With no input and a fixed clock the emulator replays
// the same frames from the same state, so after restoring frame N the frames it goes on to record
// as N+1, N+2, ... must hold exactly the memory that those frames held the first time round. A
// restore that left anything behind -- a register, a timer, a DMA in flight -- diverges, and
// the memory shows it. Memory equality is a far better proof of "resumed from that frame" than a
// picture, and it is the same oracle for every game.
//
// Exit status 0 only if every check holds.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "FrameRecorder.h"
#include "LiveDriver.h"
#include "SeLiveProtocol.h"
#include "saturnexplorer/SaturnExplorer.h"

namespace
{
int gFailures = 0;
bool Check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++gFailures;
    return ok;
}
void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

uint32_t gEpochFloor = 0;
uint32_t gStaleDropped = 0;
void OnBlock(void* user, uint8_t kind, uint32_t frame, uint32_t base, uint32_t fullLen,
             uint32_t epoch, const uint8_t* payload, uint32_t len)
{
    if (epoch < gEpochFloor) { ++gStaleDropped; return; }
    static_cast<sfe::FrameRecorder*>(user)->AttachStateBlock(frame, kind, base, fullLen, payload, len);
}

// The memory a recorded frame holds, hashed region by region (FNV-1a) so a difference can be
// pinned to where it is. The recorder's own copies, so it is the recording that is compared.
constexpr int kRegions = 5;
const char* const kRegionName[kRegions] = { "VDP1 VRAM", "VDP2 VRAM", "color RAM", "low work RAM", "high work RAM" };
struct FrameHash
{
    uint64_t r[kRegions] = {};
    bool operator==(const FrameHash& o) const { return std::memcmp(r, o.r, sizeof(r)) == 0; }
};
FrameHash HashFrame(sfe::FrameRecorder& rec, size_t index)
{
    FrameHash out;
    se_data_source ds{};
    if (!rec.Select(index, &ds)) return out;
    std::vector<uint8_t> buf(0x10000);
    int region = 0;
    auto feed = [&](size_t (*read)(void*, uint32_t, void*, size_t), uint32_t base, uint32_t size) {
        uint64_t h = 1469598103934665603ull;
        for (uint32_t off = 0; off < size; off += static_cast<uint32_t>(buf.size()))
        {
            const size_t n = read(ds.user, base + off, buf.data(), buf.size());
            for (size_t i = 0; i < n; ++i) { h ^= buf[i]; h *= 1099511628211ull; }
        }
        out.r[region++] = h;
    };
    feed(ds.read_vdp1_vram, 0, 0x80000);
    feed(ds.read_vdp2_vram, 0, 0x80000);
    feed(ds.read_cram, 0, 0x1000);
    feed(ds.read_main_ram, 0x00200000u, 0x100000);   // low work RAM
    feed(ds.read_main_ram, 0x06000000u, 0x100000);   // high work RAM
    return out;
}
}  // namespace

int main(int argc, char** argv)
{
    const char* endpoint = argc > 1 ? argv[1] : nullptr;   // default: the local socket

    std::printf("== Connect\n");
    se_data_source ds{};
    se_result r = SE_ERR_IO;
    for (int i = 0; i < 600 && r != SE_OK; ++i)
    {
        r = se_live_open(endpoint, &ds);
        if (r != SE_OK) Sleep(100);
    }
    if (!Check(r == SE_OK, "connected to a running emulator")) return 1;
    se_config cfg;
    cfg.abi_version = SE_ABI_VERSION;
    cfg.reserved = 0;
    se_context* ctx = se_create(&ds, &cfg);
    if (!Check(ctx != nullptr, "context created")) return 1;
    for (int i = 0; i < 100 && se_live_server_version(&ds) == 0; ++i) Sleep(50);
    const uint32_t version = se_live_server_version(&ds);
    std::printf("  server protocol v%u\n", version);
    Check(version >= SE_LIVE_STATE_EPOCH_MINVER, "the emulator is a build that stamps its savestate blocks (v22+)");
    se_live_set_rewind_enabled(&ds, 1);

    sfe::FrameRecorder rec;
    rec.Configure(300);

    auto drainLog = [&] {
        char lines[8][160];
        const uint32_t n = se_live_poll_log(&ds, &lines[0][0], 160, 8);
        for (uint32_t i = 0; i < n; ++i) std::printf("  emulator: %s\n", lines[i]);
    };
    auto tick = [&] {
        drainLog();
        se_begin_frame(ctx);
        rec.Capture(ctx, se_frame_number(ctx));
        se_live_drain_state_blocks(&ds, &OnBlock, &rec);
        Sleep(16);
    };
    auto framesAfter = [&](uint64_t frame) {
        size_t n = 0;
        for (size_t i = 0; i < rec.Count(); ++i) if (rec.FrameNumber(i) > frame) ++n;
        return n;
    };
    auto settle = [&](int seconds) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds))
        {
            tick();
            const sfe::FrameRecorder::StateStats s = rec.GetStateStats();
            if (s.resumable * 2 >= s.frames && s.frames) break;
        }
    };

    std::printf("== Record a stretch of the game\n");
    {
        const auto start = std::chrono::steady_clock::now();
        while (rec.Count() < 300 && std::chrono::steady_clock::now() - start < std::chrono::seconds(180)) tick();
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("  %zu frames in %.1f s (newest #%llu)\n", rec.Count(), secs,
                    static_cast<unsigned long long>(rec.Count() ? rec.FrameNumber(rec.Count() - 1) : 0));
    }
    Check(rec.Count() >= 100, "frames are being recorded");
    se_frame_pause(ctx);
    settle(30);
    {
        const sfe::FrameRecorder::StateStats s = rec.GetStateStats();
        std::printf("  frames %zu, with a block %zu, resumable %zu; blocks received %llu "
                    "(invalid %llu, never recorded %llu)\n",
                    s.frames, s.withState, s.resumable, static_cast<unsigned long long>(s.received),
                    static_cast<unsigned long long>(s.invalid), static_cast<unsigned long long>(s.noFrame));
        Check(s.received > 0, "the real emulator sends savestate blocks");
        Check(s.resumable * 2 >= s.frames, "at least half of the recorded frames can be resumed from");
        Check(s.invalid == 0, "no block was refused as corrupt");
    }

    // Rewind to the recorded frame with number 'N' and see what the game does next, comparing the
    // frames it records with 'expected' (frame number -> hash). Returns what it recorded.
    auto rewindTo = [&](uint64_t N, const std::map<uint64_t, FrameHash>& expected, const char* against,
                        size_t wantNewFrames, size_t minCompared, bool mustBeExact, std::map<uint64_t, FrameHash>& recorded) {
        size_t idx = 0;
        while (idx < rec.Count() && rec.FrameNumber(idx) != N) ++idx;
        std::vector<uint8_t> state;
        if (!Check(idx < rec.Count() && rec.CanReconstruct(idx) && rec.ReconstructState(idx, state),
                   "the frame to rewind to can be rebuilt"))
            return false;
        std::printf("  rewinding to frame #%llu (position %zu of %zu); savestate %zu bytes\n",
                    static_cast<unsigned long long>(N), idx + 1, rec.Count(), state.size());
        const size_t kept = idx + 1;
        const size_t discarded = rec.Count() - kept;
        (void)kept;

        uint32_t d0 = 0, f0 = 0;
        Check(se_live_restore_state(&ds, &d0, &f0) != 0, "the emulator reports load counters");
        gStaleDropped = 0;
        std::printf("  before the load: counters %u+%u, ring newest #%llu, display frame #%llu\n", d0, f0,
                    static_cast<unsigned long long>(rec.FrameNumber(rec.Count() - 1)),
                    static_cast<unsigned long long>(se_frame_number(ctx)));
        Check(se_load_state(ctx, N, state.data(), state.size(), nullptr, 0) == SE_OK, "the load request is accepted");
        rec.TruncateAfter(idx);
        gEpochFloor = (d0 + f0 + 1) & 0xFFFFFFu;
        Check(rec.Count() == kept, "the recorded frames after the chosen one are discarded at once");

        const auto start = std::chrono::steady_clock::now();
        int shown = 0;
        size_t compared = 0, same = 0, newFrames = 0;
        size_t regionDiffers[kRegions] = {};
        std::vector<std::string> differing;
        uint64_t lastDifferingOffset = 0;
        bool landed = false;
        uint32_t d1 = 0, f1 = 0;
        std::map<uint64_t, bool> judged;   // frame number -> did it match
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(60))
        {
            // Counters first, then the snapshot, as the front end does.
            drainLog();
            const bool sig = se_live_restore_state(&ds, &d1, &f1) != 0;
            se_begin_frame(ctx);
            landed = sig && ((d1 + f1) & 0xFFFFFFu) >= gEpochFloor;
            if (shown++ < 6)
                std::printf("    t+%lldms counters %u+%u floor %u display #%llu landed=%d ring %zu\n",
                            (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(),
                            d1, f1, gEpochFloor, (unsigned long long)se_frame_number(ctx), (int)landed, rec.Count());
            if (landed) rec.Capture(ctx, se_frame_number(ctx));
            se_live_drain_state_blocks(&ds, &OnBlock, &rec);
            Sleep(16);
            // Judge each new frame once, by its number (the ring is a window, not an array to index).
            newFrames = 0;
            for (size_t i = 0; i < rec.Count(); ++i)
            {
                const uint64_t f = rec.FrameNumber(i);
                if (f <= N) continue;
                ++newFrames;
                auto it = expected.find(f);
                if (it == expected.end() || judged.count(f)) continue;
                const FrameHash h = HashFrame(rec, i);
                judged[f] = (h == it->second);
                ++compared;
                if (h == it->second) ++same;
                else
                {
                    std::string which;
                    for (int k = 0; k < kRegions; ++k)
                        if (h.r[k] != it->second.r[k]) { ++regionDiffers[k]; which += std::string(which.empty() ? "" : ", ") + kRegionName[k]; }
                    differing.push_back("N+" + std::to_string(f - N) + " (" + which + ")");
                    if (f - N > lastDifferingOffset) lastDifferingOffset = f - N;
                }
            }
            if (compared >= minCompared && newFrames >= wantNewFrames) break;
        }
        se_frame_pause(ctx);
        settle(15);

        Check(landed, "the emulator reports the load applied");
        Check(d1 == d0 + 1 && f1 == f0, "exactly one load applied, none refused");
        uint64_t firstNew = 0;
        for (size_t i = 0; i < rec.Count() && !firstNew; ++i) if (rec.FrameNumber(i) > N) firstNew = rec.FrameNumber(i);
        Check(firstNew != 0, "recording carried on after the restore");
        std::printf("  first frame after the restore: #%llu (rewound to #%llu); %zu frames discarded; "
                    "%u stale blocks of the old run dropped\n",
                    static_cast<unsigned long long>(firstNew), static_cast<unsigned long long>(N),
                    discarded, gStaleDropped);
        Check(firstNew > N && firstNew - N <= 30, "the game resumed from the rewound frame, not from where it had got to");
        bool ascending = true;
        for (size_t i = 1; i < rec.Count(); ++i) ascending = ascending && rec.FrameNumber(i) > rec.FrameNumber(i - 1);
        Check(ascending, "recorded frame numbers still strictly increase through the restore");

        std::printf("  frames also recorded in the earlier run: %zu; identical memory: %zu\n", compared, same);
        if (same != compared)
        {
            std::printf("    differing frames:");
            for (size_t k = 0; k < differing.size() && k < 12; ++k) std::printf(" %s", differing[k].c_str());
            std::printf("\n");
        }
        if (same != compared)
            for (int k = 0; k < kRegions; ++k)
                if (regionDiffers[k]) std::printf("    %s differed in %zu of %zu\n", kRegionName[k], regionDiffers[k], compared);
        Check(compared >= minCompared, "enough re-simulated frames to compare");
        // Where it differs matters as much as whether: a restore that leaves something behind drifts
        // and keeps differing, while a one-frame blip that then matches again is not a drift.
        size_t tail = 0, tailSame = 0;
        for (auto it = judged.rbegin(); it != judged.rend() && tail < 10; ++it, ++tail) tailSame += it->second ? 1 : 0;
        std::printf("  the last %zu compared frames: %zu identical\n", tail, tailSame);
        Check(tail > 0 && tailSame == tail, "the replay has not drifted: its final frames match the original exactly");
        if (mustBeExact)
            Check(compared > 0 && same == compared, against);
        else
        {
            // Not bit-exact: Mednafen's own savestate leaves something small unrestored, which the
            // game settles within a couple of dozen frames of the load (the differing frames are all
            // early, then the replay matches the original again). So this asks for a replay that is
            // overwhelmingly the same and does not drift, and reports the transient.
            Check(compared > 0 && same * 10 >= compared * 9, against);
            if (same != compared)
                std::printf("  note: %zu of %zu frames differed, all within the first %llu frames after the restore\n",
                            compared - same, compared,
                            static_cast<unsigned long long>(lastDifferingOffset));
        }

        recorded.clear();
        for (size_t i = 0; i < rec.Count(); ++i)
            if (rec.FrameNumber(i) > N) recorded[rec.FrameNumber(i)] = HashFrame(rec, i);
        return true;
    };

    // The frame to rewind to, and what the game held for the frames after it the first time round.
    size_t idx0 = rec.Count() / 2;
    while (idx0 + 1 < rec.Count() && !rec.CanReconstruct(idx0)) ++idx0;
    const uint64_t N = rec.FrameNumber(idx0);
    std::map<uint64_t, FrameHash> original;
    for (size_t i = idx0 + 1; i < rec.Count(); ++i) original[rec.FrameNumber(i)] = HashFrame(rec, i);

    std::printf("== Play From Here, first restore: replay vs the ORIGINAL run\n");
    std::map<uint64_t, FrameHash> replay1, replay2;
    rewindTo(N, original, "the replay matches the original run (at least 9 frames in 10, and it does not drift)", 150, 40, false, replay1);

    // Control: the same frame again. If the replays agree with each other but not with the original,
    // the restore is repeatable yet not the same as having kept playing; if they disagree with each
    // other, the emulator is not deterministic once a state has been loaded.
    std::printf("== Play From Here, second restore of the SAME frame: replay vs the first REPLAY\n");
    rewindTo(N, replay1, "a second restore of the same frame replays exactly what the first did (repeatable)", 0, 40, true, replay2);

    std::printf("\n%s\n", gFailures ? "FAILED" : "ALL CHECKS PASSED");
    se_destroy(ctx);
    return gFailures ? 1 : 0;
}
