// Unit tests for FrameRecorder's savestate-rewind machinery (v16): attaching per-frame
// keyframe/delta blocks, reconstructing a full savestate from keyframe + delta, refusing
// to reconstruct when a block is missing or its keyframe was evicted, and TruncateAfter /
// Evict byte accounting. The savestate blocks are synthetic (the codec is exercised
// directly in SeStateCodecTests); here we only care that the recorder stores, matches by
// frame number, reconstructs, and accounts for them correctly.
//
// Frames enter the ring only through Capture() + the background compression worker, so the
// tests drive Capture against a trivial se_context (an empty data source — region contents
// don't matter to this machinery) and wait for the worker to publish each frame.
#include "FrameRecorder.h"

#include "FrameLz.h"
#include "ScrubState.h"      // PlanScrub + StagedEdits, driven here against the real recorder

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "FakeVdpSource.h"    // se_test::State / CreateContext (shared Seam A mock)
#include "SaturnRegions.h"    // kVdp1VramSize
#include "SeLiveProtocol.h"   // SE_LIVE_STATE_KIND_*
#include "SeStateCodec.h"     // to build synthetic RLE payloads

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

// A minimal data source: valid ABI, no capabilities. Capture() reads empty regions, which
// is fine — this suite exercises the state-block path, not frame contents.
se_context* MakeContext()
{
    se_data_source ds{};
    ds.abi_version = SE_ABI_VERSION;
    ds.capabilities = 0;
    ds.user = nullptr;
    se_config cfg{};
    cfg.abi_version = SE_ABI_VERSION;
    return se_create(&ds, &cfg);
}

// Capture one frame and block until the worker publishes it — detected by the newest
// resident frame number reaching 'frameNo' (robust to eviction, where Count() may not grow).
// Capturing one-at-a-time keeps the bounded raw queue from dropping frames.
bool CaptureFrame(FrameRecorder& r, se_context* ctx, uint64_t frameNo)
{
    r.Capture(ctx, frameNo);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;)
    {
        const size_t c = r.Count();
        if (c > 0 && r.FrameNumber(c - 1) == frameNo) return true;
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// The scrub view as App drives it, against the real recorder and a real scrub context. Mirrors
// App::RefreshScrubContext and App::DiscardPendingEdits: the point of the tests that use it is the
// invariant they share -- the context shows edits if and only if they are staged for replay.
struct ScrubSim
{
    FrameRecorder& rec;
    se_context*    ctx = nullptr;
    int            index = -1, shownIndex = -1;
    uint64_t       shownFrame = 0, target = 0;
    bool           edited = false;
    int            reloads = 0;
    StagedEdits    staged;

    explicit ScrubSim(FrameRecorder& r) : rec(r)
    {
        rec.SetEditSink(this, [](void* u, int isSound, uint32_t addr, const uint8_t* b, size_t n)
        {
            ScrubSim* self = static_cast<ScrubSim*>(u);
            self->staged.Record(self->shownFrame, isSound != 0, addr, b, n);
        });
    }
    ~ScrubSim() { if (ctx) se_destroy(ctx); }

    bool Refresh()
    {
        const ScrubPlan plan = PlanScrub(rec, ctx != nullptr, index, shownIndex, shownFrame, target, edited);
        target = 0;
        if (plan.kind == ScrubPlan::Nothing) return false;
        if (plan.kind == ScrubPlan::Keep) { index = shownIndex = plan.index; return true; }
        se_data_source ds{};
        bool selected = false;
        if (plan.kind == ScrubPlan::SelectFrame)
        {
            size_t found = 0;
            selected = rec.SelectFrame(plan.frame, &found, &ds);
            if (!selected) { shownIndex = -1; shownFrame = 0; return false; }
            index = static_cast<int>(found);
        }
        else
        {
            index = plan.index;
        }
        if (!selected && !rec.Select(static_cast<size_t>(index), &ds)) return false;
        staged.Clear();
        edited = false;
        if (!ctx)
        {
            se_config cfg;
            cfg.abi_version = SE_ABI_VERSION;
            cfg.reserved = 0;
            ctx = se_create(&ds, &cfg);
        }
        se_begin_frame(ctx);
        shownIndex = index;
        shownFrame = rec.SelectedFrameNumber();
        ++reloads;
        return true;
    }

    void Discard()   // App::DiscardPendingEdits
    {
        if (!staged.Empty()) edited = true;
        staged.Clear();
    }

    uint8_t Displayed(uint32_t offset) const
    {
        uint8_t b = 0;
        se_read_vram(ctx, SE_VRAM_KIND_VDP1_VRAM, offset, &b, 1);
        return b;
    }
};

// A pattern the LZ codec can actually compress, so a captured region has a non-trivial blob.
uint8_t PatternByte(size_t i) { return (uint8_t)((i / 7) * 3 + (i & 0x0F)); }

// RLE-encode 'full' into a wire payload the recorder can store + later decode.
std::vector<uint8_t> Encode(const std::vector<uint8_t>& full)
{
    std::vector<uint8_t> enc(full.size() * 2 + 16);
    const size_t n = se_state_rle_encode(enc.data(), enc.size(), full.data(), full.size());
    enc.resize(n);
    return enc;
}
}  // namespace

int main()
{
    se_context* ctx = MakeContext();
    Check(ctx != nullptr, "context created");
    if (!ctx) return 1;

    // Build the ring: frames 1..5, each captured and confirmed resident.
    FrameRecorder rec;
    rec.Configure(100);   // large cap: no eviction during the fill
    bool filled = true;
    for (uint64_t fn = 1; fn <= 5; ++fn)
        filled = filled && CaptureFrame(rec, ctx, fn);
    Check(filled && rec.Count() == 5, "captured 5 frames");

    // Synthetic keyframe (frame 1) + deltas that XOR onto it (frames 2 and 3).
    const size_t N = 2048;
    std::vector<uint8_t> keyframe(N), full2(N), full3(N);
    for (size_t i = 0; i < N; ++i) keyframe[i] = (uint8_t)(i * 5 + 1);
    full2 = keyframe; full2[7] ^= 0xFF; full2[1000] = 0x22;
    full3 = keyframe; full3[8] = 0x99;  full3[2047] = 0x01;

    std::vector<uint8_t> delta2(N), delta3(N);
    se_state_xor(delta2.data(), full2.data(), keyframe.data(), N);
    se_state_xor(delta3.data(), full3.data(), keyframe.data(), N);

    const std::vector<uint8_t> encKf = Encode(keyframe);
    const std::vector<uint8_t> encD2 = Encode(delta2);
    const std::vector<uint8_t> encD3 = Encode(delta3);

    // Attach: frame 1 keyframe, frames 2/3 deltas based on frame 1. Frames 4/5 stay unattached.
    rec.AttachStateBlock(1, SE_LIVE_STATE_KIND_KEYFRAME, 1, (uint32_t)N, encKf.data(), encKf.size());
    rec.AttachStateBlock(2, SE_LIVE_STATE_KIND_DELTA,    1, (uint32_t)N, encD2.data(), encD2.size());
    rec.AttachStateBlock(3, SE_LIVE_STATE_KIND_DELTA,    1, (uint32_t)N, encD3.data(), encD3.size());

    // --- Reconstruction: keyframe frame ---
    {
        std::vector<uint8_t> out;
        Check(rec.CanReconstruct(0), "frame 1 (keyframe) reconstructable");
        Check(rec.ReconstructState(0, out) && out == keyframe, "frame 1 reconstructs to keyframe");
    }
    // --- Reconstruction: delta frames rebuild the exact full state ---
    {
        std::vector<uint8_t> out;
        Check(rec.CanReconstruct(1) && rec.ReconstructState(1, out) && out == full2,
              "frame 2 delta reconstructs to full2");
        out.clear();
        Check(rec.CanReconstruct(2) && rec.ReconstructState(2, out) && out == full3,
              "frame 3 delta reconstructs to full3");
    }
    // --- A frame with no block yet is not reconstructable ---
    {
        std::vector<uint8_t> out;
        Check(!rec.CanReconstruct(3) && !rec.ReconstructState(3, out),
              "frame 4 (no block) not reconstructable");
    }

    // --- Duplicate attach is ignored (block already present) ---
    {
        const size_t before = rec.BytesUsed();
        rec.AttachStateBlock(1, SE_LIVE_STATE_KIND_KEYFRAME, 1, (uint32_t)N, encKf.data(), encKf.size());
        Check(rec.BytesUsed() == before, "duplicate AttachStateBlock is a no-op");
    }

    // --- Attaching a block adds its bytes to the footprint ---
    {
        std::vector<uint8_t> full4 = keyframe; full4[3] = 0x44;
        std::vector<uint8_t> delta4(N);
        se_state_xor(delta4.data(), full4.data(), keyframe.data(), N);
        const std::vector<uint8_t> encD4 = Encode(delta4);
        const size_t before = rec.BytesUsed();
        rec.AttachStateBlock(4, SE_LIVE_STATE_KIND_DELTA, 1, (uint32_t)N, encD4.data(), encD4.size());
        Check(rec.BytesUsed() == before + encD4.size(), "attach adds payload bytes to footprint");
        std::vector<uint8_t> out;
        Check(rec.ReconstructState(3, out) && out == full4, "frame 4 now reconstructs");
    }

    // --- TruncateAfter drops the tail and its byte accounting ---
    {
        const size_t before = rec.BytesUsed();
        // Sum the footprint of frames we're about to drop is hard to know exactly; instead
        // assert the ring shrinks, bytes strictly decrease, and the survivors still work.
        rec.TruncateAfter(1);   // keep frames at index 0,1 (frame numbers 1,2)
        Check(rec.Count() == 2, "TruncateAfter(1) leaves 2 frames");
        Check(rec.BytesUsed() < before, "TruncateAfter reduces the footprint");
        Check(rec.FrameNumber(0) == 1 && rec.FrameNumber(1) == 2, "surviving frame numbers intact");
        std::vector<uint8_t> out;
        Check(rec.ReconstructState(1, out) && out == full2, "frame 2 still reconstructs after truncate");
    }

    // --- A keyframe outlives its own frame; a delta with no keyframe cannot be rebuilt ---
    {
        FrameRecorder r2;
        r2.Configure(2);   // tiny ring: only the 2 newest frames survive
        // Frame 1 is the keyframe; frames 2 and 3 are deltas onto it. With maxFrames=2, adding
        // frame 3 evicts frame 1 -- but the keyframe block is held apart from its frame, so the
        // deltas against it are still resumable.
        Check(CaptureFrame(r2, ctx, 1), "r2 frame 1");
        r2.AttachStateBlock(1, SE_LIVE_STATE_KIND_KEYFRAME, 1, (uint32_t)N, encKf.data(), encKf.size());
        Check(CaptureFrame(r2, ctx, 2), "r2 frame 2");
        r2.AttachStateBlock(2, SE_LIVE_STATE_KIND_DELTA, 1, (uint32_t)N, encD2.data(), encD2.size());
        Check(CaptureFrame(r2, ctx, 3), "r2 frame 3 evicts frame 1");
        r2.AttachStateBlock(3, SE_LIVE_STATE_KIND_DELTA, 1, (uint32_t)N, encD3.data(), encD3.size());
        Check(r2.Count() == 2 && r2.FrameNumber(0) == 2, "r2 ring holds frames 2,3");
        std::vector<uint8_t> out;
        Check(r2.CanReconstruct(1) && r2.ReconstructState(1, out) && out == full3,
              "a delta still reconstructs after its keyframe's frame was evicted");

        // Whereas a delta whose keyframe block never arrived has nothing to be applied to.
        FrameRecorder r2b;
        r2b.Configure(10);
        Check(CaptureFrame(r2b, ctx, 2), "r2b frame 2");
        r2b.AttachStateBlock(2, SE_LIVE_STATE_KIND_DELTA, 1, (uint32_t)N, encD2.data(), encD2.size());
        Check(!r2b.CanReconstruct(0) && !r2b.ReconstructState(0, out),
              "a delta whose keyframe never arrived is not reconstructable");
    }

    // --- A block can arrive before its frame is published (it does, in practice) ---
    {
        FrameRecorder r8;
        r8.Configure(10);
        // Blocks first, frames after: the emulator's stream runs ahead of what the front end has
        // recorded, so the recorder has to hold what it is given.
        r8.AttachStateBlock(1, SE_LIVE_STATE_KIND_KEYFRAME, 1, (uint32_t)N, encKf.data(), encKf.size());
        r8.AttachStateBlock(2, SE_LIVE_STATE_KIND_DELTA, 1, (uint32_t)N, encD2.data(), encD2.size());
        r8.AttachStateBlock(3, SE_LIVE_STATE_KIND_DELTA, 1, (uint32_t)N, encD3.data(), encD3.size());
        Check(r8.GetStateStats().waiting == 2, "r8 holds the two deltas ahead of their frames");
        Check(CaptureFrame(r8, ctx, 1) && CaptureFrame(r8, ctx, 2), "r8 frames 1,2");
        std::vector<uint8_t> out;
        Check(r8.CanReconstruct(0) && r8.ReconstructState(0, out) && out == keyframe,
              "the keyframe's frame is resumable once recorded");
        Check(r8.CanReconstruct(1) && r8.ReconstructState(1, out) && out == full2,
              "a delta that arrived early attaches when its frame is recorded");

        // The front end sees only some frames, so frame 3 may never be recorded: its block must
        // not wait forever. Recording frame 4 shows the ring has moved past it.
        Check(CaptureFrame(r8, ctx, 4), "r8 frame 4");
        Check(r8.GetStateStats().waiting == 0, "a block for a frame that was skipped is dropped");
        Check(r8.GetStateStats().noFrame >= 1, "and counted as never recorded");
    }

    // --- Deltas rebuild against a keyframe whose own frame was never recorded ---
    {
        FrameRecorder r9;
        r9.Configure(10);
        r9.AttachStateBlock(1, SE_LIVE_STATE_KIND_KEYFRAME, 1, (uint32_t)N, encKf.data(), encKf.size());
        Check(CaptureFrame(r9, ctx, 2), "r9 frame 2");
        r9.AttachStateBlock(2, SE_LIVE_STATE_KIND_DELTA, 1, (uint32_t)N, encD2.data(), encD2.size());
        std::vector<uint8_t> out;
        Check(r9.CanReconstruct(0) && r9.ReconstructState(0, out) && out == full2,
              "frame 2 rebuilds from a keyframe taken at a frame that was not recorded");

        // Rewinding to frame 2 discards a keyframe taken after it: that future did not happen.
        r9.AttachStateBlock(9, SE_LIVE_STATE_KIND_KEYFRAME, 9, (uint32_t)N, encKf.data(), encKf.size());
        Check(r9.GetStateStats().keyframes == 2, "r9 holds both keyframes");
        r9.TruncateAfter(0);
        Check(r9.GetStateStats().keyframes == 1, "a keyframe from after the resume point is dropped");
        Check(r9.CanReconstruct(0), "and the frame resumed from is still resumable");
    }

    // --- Pruning must not delete the keyframe a waiting delta is about to need ---
    {
        FrameRecorder r10;
        r10.Configure(10);
        // Keyframe 1 and a delta against it for frame 2, which is not recorded yet; then two
        // newer keyframes (the exporter promotes one on a scene change) before frame 2 lands.
        r10.AttachStateBlock(1, SE_LIVE_STATE_KIND_KEYFRAME, 1, (uint32_t)N, encKf.data(), encKf.size());
        r10.AttachStateBlock(2, SE_LIVE_STATE_KIND_DELTA, 1, (uint32_t)N, encD2.data(), encD2.size());
        r10.AttachStateBlock(3, SE_LIVE_STATE_KIND_KEYFRAME, 3, (uint32_t)N, encKf.data(), encKf.size());
        r10.AttachStateBlock(4, SE_LIVE_STATE_KIND_KEYFRAME, 4, (uint32_t)N, encKf.data(), encKf.size());
        Check(CaptureFrame(r10, ctx, 2), "r10 frame 2");
        std::vector<uint8_t> out;
        Check(r10.CanReconstruct(0) && r10.ReconstructState(0, out) && out == full2,
              "the delta that was waiting still has its keyframe");
    }

    // --- Blocks waiting for a frame are counted against the byte budget, and bounded by it ---
    {
        // Incompressible payloads, so each is a real few KB. A tiny budget must not be
        // overrun by early blocks that never find a frame.
        std::vector<uint8_t> noisy(4096);
        uint32_t x = 12345;
        for (auto& b : noisy) { x = x * 1664525u + 1013904223u; b = (uint8_t)(x >> 24); }
        const std::vector<uint8_t> encNoisy = Encode(noisy);

        FrameRecorder r11;
        r11.Configure(10, 1024);
        for (uint64_t f = 2; f < 50; ++f)
            r11.AttachStateBlock(f, SE_LIVE_STATE_KIND_DELTA, 1, (uint32_t)noisy.size(),
                                 encNoisy.data(), encNoisy.size());
        Check(r11.BytesUsed() <= 1024, "48 early blocks do not exceed a 1 KiB budget");
        Check(r11.GetStateStats().waiting == 0, "blocks bigger than the allowance are not held");

        FrameRecorder r11b;
        r11b.Configure(10, 4 * encNoisy.size());   // a quarter of this fits one block, not two
        for (uint64_t f = 2; f < 12; ++f)
            r11b.AttachStateBlock(f, SE_LIVE_STATE_KIND_DELTA, 1, (uint32_t)noisy.size(),
                                  encNoisy.data(), encNoisy.size());
        Check(r11b.GetStateStats().waiting == 1, "only what the allowance covers is held");
        Check(r11b.BytesUsed() == encNoisy.size(), "and what is held is counted in the footprint");
        r11b.Clear();
        Check(r11b.BytesUsed() == 0 && r11b.GetStateStats().waiting == 0, "clearing releases them");
    }

    // --- A keyframe replaces a delta the frame already holds ---
    {
        FrameRecorder r12;
        r12.Configure(10);
        Check(CaptureFrame(r12, ctx, 3), "r12 frame 3");
        const size_t frameOnly = r12.BytesUsed();   // the frame's own footprint, before any state
        // A delta for frame 3 against a keyframe that is not there, then frame 3's own keyframe.
        r12.AttachStateBlock(3, SE_LIVE_STATE_KIND_DELTA, 1, (uint32_t)N, encD3.data(), encD3.size());
        const size_t withDelta = r12.BytesUsed() - frameOnly;
        r12.AttachStateBlock(3, SE_LIVE_STATE_KIND_KEYFRAME, 3, (uint32_t)N, encKf.data(), encKf.size());
        std::vector<uint8_t> out;
        Check(r12.CanReconstruct(0) && r12.ReconstructState(0, out) && out == keyframe,
              "the keyframe is the state, not the delta it arrived behind");
        Check(r12.BytesUsed() - frameOnly == encKf.size() && withDelta == encD3.size(),
              "and the replaced delta is no longer counted");
    }

    // --- A frame that does not fully decode is refused, not blanked and served (REW-02) ---
    // Nothing can corrupt a blob once the recorder has stored one, so the frame is posed
    // directly. DecompressFrame is the recorder's only decode entry point, so these cover
    // both the per-region verdict and what the frame does with it.
    {
        std::vector<uint8_t> raw(4096);
        for (size_t i = 0; i < raw.size(); ++i) raw[i] = PatternByte(i);

        FrameRecorder::Region good;
        good.rawSize = raw.size();
        FrameLzCompress(raw.data(), raw.size(), good.lz);

        // Truncating the blob leaves a stream that cannot produce rawSize bytes.
        FrameRecorder::Region corrupt = good;
        corrupt.lz.resize(corrupt.lz.size() / 2);

        FrameRecorder::Frame frame;
        frame.vdp1Vram = good;
        frame.cram = good;
        frame.soundRam = good;   // wramLow/High, vdp2Vram, vdp1Fb stay absent (rawSize 0)
        FrameRecorder::Scratch scratch;
        Check(FrameRecorder::DecompressFrame(frame, scratch),
              "a frame whose regions all decode is accepted");
        Check(scratch.vdp1 == raw && scratch.cram == raw && scratch.soundRam == raw,
              "...and every region lands in the scratch");
        Check(scratch.wramLow.empty() && scratch.vdp2.empty(),
              "an absent region is not a corrupt one");

        // In the middle, so the regions after it prove they are still written.
        frame.cram = corrupt;
        scratch.soundRam.assign(8, 0xCD);
        Check(!FrameRecorder::DecompressFrame(frame, scratch),
              "a frame with one undecodable region is refused");
        Check(scratch.cram.size() == corrupt.rawSize &&
                  std::all_of(scratch.cram.begin(), scratch.cram.end(),
                              [](uint8_t b) { return b == 0; }),
              "the failed region leaves no earlier frame's bytes behind");
        Check(scratch.soundRam == raw,
              "a refused frame still overwrites the regions past the bad one");
    }

    // --- Select over a frame with real contents hands back exactly what was captured ---
    // The trivial source above leaves every region at rawSize 0, which never decompresses, so
    // this one serves real VDP1 VRAM through the shared fixture.
    {
        se_test::State st(kVdp1VramSize);
        for (size_t i = 0; i < st.vdp1.size(); ++i) st.vdp1[i] = PatternByte(i);
        se_context* vctx = se_test::CreateContext(st);
        Check(vctx != nullptr, "vram context created");
        se_begin_frame(vctx);   // Capture() reads through the context's latched snapshot
        FrameRecorder r3;
        r3.Configure(10);
        Check(CaptureFrame(r3, vctx, 1), "captured a frame with real VDP1 VRAM");
        se_data_source ds{};
        Check(r3.Select(0, &ds), "Select accepts a frame that decompresses");
        uint8_t got[64] = {};
        const size_t n = ds.read_vdp1_vram ? ds.read_vdp1_vram(ds.user, 1024, got, sizeof(got)) : 0;
        Check(n == sizeof(got) && std::equal(got, got + n, st.vdp1.begin() + 1024),
              "the selected frame reads back the captured bytes");
        se_destroy(vctx);
    }

    // --- Edits to VDP memory on a scrubbed frame are recorded for replay, not just displayed ---
    // Work and sound RAM were forwarded to the edit sink; VDP1/VDP2 VRAM, CRAM and the
    // framebuffer were not, so such an edit showed on the frame and was gone once Play rewound.
    {
        struct Rec { int isSound; uint32_t addr; std::vector<uint8_t> bytes; };
        static std::vector<Rec> sink;
        sink.clear();
        se_test::State st(kVdp1VramSize);
        se_context* vctx = se_test::CreateContext(st);
        se_begin_frame(vctx);
        FrameRecorder r5;
        r5.Configure(10);
        r5.SetEditSink(nullptr, [](void*, int isSound, uint32_t addr, const uint8_t* b, size_t n)
                       { sink.push_back({isSound, addr, std::vector<uint8_t>(b, b + n)}); });
        Check(CaptureFrame(r5, vctx, 1), "captured a frame to edit");
        se_data_source ds{};
        Check(r5.Select(0, &ds), "selected the frame to edit");
        se_config cfg;
        cfg.abi_version = SE_ABI_VERSION;
        cfg.reserved = 0;
        se_context* scrub = se_create(&ds, &cfg);
        Check(scrub != nullptr, "scrub context created");
        if (scrub)
        {
            se_begin_frame(scrub);
            const uint8_t ab[2] = { 0xAB, 0xCD };
            struct { se_vram_kind kind; uint32_t off; uint32_t bus; } cases[] = {
                { SE_VRAM_KIND_VDP1_VRAM, 0x10, 0x05C00010u },
                { SE_VRAM_KIND_VDP1_FB,   0x20, 0x05C80020u },
                { SE_VRAM_KIND_VDP2_VRAM, 0x30, 0x05E00030u },
                { SE_VRAM_KIND_CRAM,      0x40, 0x05F00040u },
            };
            for (const auto& c : cases)
            {
                sink.clear();
                Check(se_write_vram(scrub, c.kind, c.off, ab, 2) == 2, "the VDP edit is accepted");
                Check(sink.size() == 1 && sink[0].isSound == 0 && sink[0].addr == c.bus &&
                          sink[0].bytes == std::vector<uint8_t>({ 0xAB, 0xCD }),
                      "the VDP edit reaches the replay sink at its bus address");
                uint8_t back[2] = {};
                se_read_vram(scrub, c.kind, c.off, back, 2);
                Check(back[0] == 0xAB && back[1] == 0xCD, "and the displayed frame shows it");
            }
            // Without a VDP writer on the server, Play From Here would drop a CRAM or frame-buffer
            // edit (the emulator's bus writer does not reach them), so the edit is refused instead
            // of staged and shown; VRAM is unaffected.
            r5.SetVdpBusEditsAccepted(false);
            for (const auto& c : cases)
            {
                sink.clear();
                const bool bus = c.kind == SE_VRAM_KIND_CRAM || c.kind == SE_VRAM_KIND_VDP1_FB;
                uint8_t before[2] = {};
                se_read_vram(scrub, c.kind, c.off, before, 2);
                const uint8_t other[2] = { 0x11, 0x22 };
                const size_t n = se_write_vram(scrub, c.kind, c.off, other, 2);
                uint8_t after[2] = {};
                se_read_vram(scrub, c.kind, c.off, after, 2);
                if (bus)
                    Check(n == 0 && sink.empty() && std::memcmp(before, after, 2) == 0 && after[0] != 0x11,
                          "a CRAM / frame-buffer edit is refused, staged nowhere, when the server cannot apply it");
                else
                    Check(n == 2 && sink.size() == 1, "a VRAM edit still stages");
            }
            r5.SetVdpBusEditsAccepted(true);
            se_destroy(scrub);
        }
        se_destroy(vctx);
    }

    // --- A corrupt or mis-declared state block is refused at the door (REW-03) ---
    {
        FrameRecorder r4;
        r4.Configure(10);
        Check(CaptureFrame(r4, ctx, 1), "r4 frame 1");
        const size_t before = r4.BytesUsed();

        // Truncated payload: its tokens no longer decode to N bytes.
        std::vector<uint8_t> truncated = encKf;
        truncated.resize(truncated.size() / 2);
        r4.AttachStateBlock(1, SE_LIVE_STATE_KIND_KEYFRAME, 1, (uint32_t)N,
                            truncated.data(), truncated.size());
        Check(r4.BytesUsed() == before, "a truncated state block is not stored");
        Check(!r4.CanReconstruct(0), "a frame with a rejected block is not reconstructable");

        // Intact payload, but the sender's declared full length disagrees with it.
        r4.AttachStateBlock(1, SE_LIVE_STATE_KIND_KEYFRAME, 1, (uint32_t)(N - 1),
                            encKf.data(), encKf.size());
        Check(r4.BytesUsed() == before, "a block whose decoded size belies fullLen is not stored");
        Check(!r4.CanReconstruct(0), "...and leaves the frame unreconstructable");

        // The same block with the right length still attaches, so the check is not refusing
        // everything.
        r4.AttachStateBlock(1, SE_LIVE_STATE_KIND_KEYFRAME, 1, (uint32_t)N,
                            encKf.data(), encKf.size());
        Check(r4.BytesUsed() == before + encKf.size(), "a well-formed block still attaches");
        Check(r4.CanReconstruct(0), "...and the frame becomes reconstructable");
    }

    // --- A delta can't be XORed onto a keyframe of a different size (REW-03) ---
    {
        FrameRecorder r5;
        r5.Configure(10);
        Check(CaptureFrame(r5, ctx, 1) && CaptureFrame(r5, ctx, 2), "r5 frames 1,2");
        // Frame 1 carries a shorter keyframe than frame 2's delta claims to be against.
        const size_t shortN = N / 2;
        std::vector<uint8_t> shortKf(shortN);
        for (size_t i = 0; i < shortN; ++i) shortKf[i] = (uint8_t)(i + 3);
        const std::vector<uint8_t> encShort = Encode(shortKf);
        r5.AttachStateBlock(1, SE_LIVE_STATE_KIND_KEYFRAME, 1, (uint32_t)shortN,
                            encShort.data(), encShort.size());
        r5.AttachStateBlock(2, SE_LIVE_STATE_KIND_DELTA, 1, (uint32_t)N,
                            encD2.data(), encD2.size());
        std::vector<uint8_t> out;
        Check(!r5.CanReconstruct(1) && !r5.ReconstructState(1, out),
              "a delta whose keyframe has a different decoded size is not reconstructable");
    }

    // --- Attaching a block re-runs eviction, so it can't overshoot the budget (REW-01) ---
    {
        FrameRecorder r6;
        r6.Configure(10);
        Check(CaptureFrame(r6, ctx, 1) && CaptureFrame(r6, ctx, 2) && CaptureFrame(r6, ctx, 3),
              "r6 frames 1,2,3");
        // Set the ceiling to what the three frames already occupy: the ring is at its limit,
        // so the next attachment has to push something out.
        const size_t settled = r6.BytesUsed();
        r6.Configure(10, settled);
        Check(r6.Count() == 3, "all three frames fit at the new ceiling");
        r6.AttachStateBlock(3, SE_LIVE_STATE_KIND_KEYFRAME, 3, (uint32_t)N,
                            encKf.data(), encKf.size());
        Check(r6.Count() < 3, "attaching over the ceiling evicts, rather than waiting for a frame");
        Check(r6.BytesUsed() <= settled || r6.Count() == 1,
              "the footprint is back within the ceiling");
    }

    // --- "Play from here": record 100 frames, resume at the 40th, and 41..100 are gone ---
    {
        FrameRecorder r7;
        r7.Configure(200);
        bool filled = true;
        for (uint64_t fn = 1; fn <= 100 && filled; ++fn) filled = CaptureFrame(r7, ctx, fn);
        Check(filled && r7.Count() == 100, "r7 recorded 100 frames");

        r7.TruncateAfter(39);   // scrub position 40 is index 39 (frame numbers start at 1)
        Check(r7.Count() == 40, "r7 keeps exactly the 40 frames up to the resume point");
        Check(r7.FrameNumber(39) == 40, "r7's newest frame is the one resumed from");

        // Recording must carry on from the resume point, not wait for the game to pass the old
        // frame 100 again. (Keeping a stale 100 out is the caller's job -- see the restore gate
        // on Capture() in App -- because the recorder cannot tell it from a genuine frame 100.)
        Check(CaptureFrame(r7, ctx, 41), "r7 records frame 41 after resuming");
        Check(r7.Count() == 41 && r7.FrameNumber(40) == 41, "the new frame 41 follows frame 40");
    }

    // --- SelectedFrameNumber names the decompressed frame, not an index (eviction shifts indexes) ---
    {
        se_test::State st;   // a small non-empty VDP1 VRAM is enough for a frame to decompress
        for (size_t i = 0; i < st.vdp1.size(); ++i) st.vdp1[i] = PatternByte(i);
        se_context* vctx = se_test::CreateContext(st);
        se_begin_frame(vctx);
        FrameRecorder rSel;
        rSel.Configure(3);
        Check(rSel.SelectedFrameNumber() == 0, "nothing is selected before the first Select");
        for (uint64_t fn = 10; fn <= 12; ++fn) CaptureFrame(rSel, vctx, fn);
        se_data_source ds{};
        Check(rSel.Select(1, &ds) && rSel.SelectedFrameNumber() == 11, "Select(1) selects frame 11");

        CaptureFrame(rSel, vctx, 13);   // cap 3: frame 10 is evicted, every index shifts down
        Check(rSel.FrameNumber(1) == 12, "after eviction, index 1 now names frame 12");
        Check(rSel.SelectedFrameNumber() == 11, "but the scratch still holds, and reports, frame 11");

        Check(!rSel.Select(99, &ds) && rSel.SelectedFrameNumber() == 0, "a refused Select leaves nothing selected");
        rSel.Clear();
        Check(rSel.SelectedFrameNumber() == 0, "Clear drops the selection");
        se_destroy(vctx);
    }

    // --- SelectFrame names a frame, not an index, and refuses one that has been evicted ---
    // Go to A/B resolves a frame number a UI frame after it was looked up; by then the worker may have
    // published a newer frame and evicted the oldest, so the old index names a different frame.
    {
        se_test::State st;
        for (size_t i = 0; i < st.vdp1.size(); ++i) st.vdp1[i] = PatternByte(i);
        se_context* vctx = se_test::CreateContext(st);
        se_begin_frame(vctx);
        FrameRecorder rFind;
        rFind.Configure(3);
        for (uint64_t fn = 10; fn <= 12; ++fn) CaptureFrame(rFind, vctx, fn);
        Check(rFind.IndexOfFrame(11) == 1 && rFind.IndexOfFrame(10) == 0, "frames are found by number");
        Check(rFind.IndexOfFrame(9) == -1 && rFind.IndexOfFrame(99) == -1, "a missing frame is not matched to a neighbour");

        const int staleIndex = rFind.IndexOfFrame(11);   // what a caller would remember
        CaptureFrame(rFind, vctx, 13);                   // evicts frame 10; every index shifts down
        se_data_source ds{};
        Check(rFind.Select(static_cast<size_t>(staleIndex), &ds) && rFind.SelectedFrameNumber() == 12,
              "the remembered index now opens frame 12, which is the bug SelectFrame exists to avoid");

        size_t found = 99;
        Check(rFind.SelectFrame(11, &found, &ds), "frame 11 is still selectable by number");
        Check(found == 0 && rFind.SelectedFrameNumber() == 11, "it is found at its new index and is frame 11, never 12");
        Check(!rFind.SelectFrame(10, &found, &ds), "an evicted frame is refused");
        Check(rFind.SelectedFrameNumber() == 0, "and a refusal leaves nothing selected");
        Check(!rFind.SelectFrame(99, nullptr, &ds), "a frame that never existed is refused");
        Check(rFind.SelectFrame(13, nullptr, &ds) && rFind.SelectedFrameNumber() == 13, "the index out-parameter is optional");
        se_destroy(vctx);
    }

    // --- Navigating to the frame already shown must not split the display from the replay batch ---
    // Reloading the shown frame rebuilds the context from the recording (the displayed edit vanishes)
    // while the staged edit stayed and Play From Here would still replay it.
    {
        se_test::State st(kVdp1VramSize);
        for (size_t i = 0; i < st.vdp1.size(); ++i) st.vdp1[i] = PatternByte(i);
        se_context* vctx = se_test::CreateContext(st);
        se_begin_frame(vctx);
        FrameRecorder rEdit;
        rEdit.Configure(5);
        for (uint64_t fn = 10; fn <= 12; ++fn) CaptureFrame(rEdit, vctx, fn);
        ScrubSim sim(rEdit);
        const uint32_t kOff = 5;
        const uint8_t orig = PatternByte(kOff), kEdit = 0xFF;
        auto consistent = [&](const char* when)
        {
            const bool shows = sim.Displayed(kOff) == kEdit;
            char msg[160];
            std::snprintf(msg, sizeof(msg), "%s: the display shows the edit iff it is staged for replay", when);
            Check(shows == !sim.staged.Empty(), msg);
        };

        sim.index = 2;
        Check(sim.Refresh() && sim.shownFrame == 12 && sim.Displayed(kOff) == orig, "showing frame 12, unedited");
        Check(se_write_vram(sim.ctx, SE_VRAM_KIND_VDP1_VRAM, kOff, &kEdit, 1) == 1, "the edit is accepted");
        Check(sim.Displayed(kOff) == kEdit && sim.staged.BelongsTo(12) && sim.staged.Pokes().size() == 1 &&
                  sim.staged.Pokes()[0].addr == 0x05C00000u + kOff,
              "frame 12 shows the edit and it is staged against frame 12");
        consistent("after the edit");

        const int reloads = sim.reloads;
        sim.target = 12;   // Go to frame 12, which is already shown
        Check(sim.Refresh() && sim.reloads == reloads, "navigating to the shown frame does not reload it");
        Check(sim.Displayed(kOff) == kEdit && sim.staged.BelongsTo(12), "so the edit stays displayed and staged");
        consistent("after navigating to the shown frame");

        // Leaving the frame drops its edits with it, and coming back shows the recording.
        sim.index = 0;
        Check(sim.Refresh() && sim.shownFrame == 10 && sim.staged.Empty() && sim.Displayed(kOff) == orig,
              "seeking to another frame drops the edit and shows that frame's recorded bytes");
        sim.index = 2;
        Check(sim.Refresh() && sim.shownFrame == 12 && sim.staged.Empty() && sim.Displayed(kOff) == orig,
              "and coming back shows the recorded frame, with nothing staged");
        consistent("after leaving and returning");

        // Staged edits discarded while the context still shows them (Play, an abandoned frame): the
        // frame is rebuilt from the recording before it is shown again.
        se_write_vram(sim.ctx, SE_VRAM_KIND_VDP1_VRAM, kOff, &kEdit, 1);
        sim.Discard();
        Check(sim.staged.Empty() && sim.Displayed(kOff) == kEdit, "the context still shows a discarded edit until it is rebuilt");
        Check(sim.Refresh() && sim.Displayed(kOff) == orig && sim.staged.Empty(), "the next refresh rebuilds it from the recording");
        consistent("after discarding the edit");

        // The same with a navigation to the shown frame in between.
        se_write_vram(sim.ctx, SE_VRAM_KIND_VDP1_VRAM, kOff, &kEdit, 1);
        sim.Discard();
        sim.target = 12;
        Check(sim.Refresh() && sim.Displayed(kOff) == orig && sim.staged.Empty(), "discarded edits are not kept by a same-frame navigation either");
        consistent("after discard + same-frame navigation");
        se_destroy(vctx);
    }

    se_destroy(ctx);
    if (gFail == 0) std::printf("All FrameRecorder tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
