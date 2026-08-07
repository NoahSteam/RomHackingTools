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

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

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

    // --- Evicted keyframe: a delta whose base keyframe is gone can't be reconstructed ---
    {
        FrameRecorder r2;
        r2.Configure(2);   // tiny ring: only the 2 newest frames survive
        // Frame 1 is the keyframe; frames 2 and 3 are deltas onto it. With maxFrames=2, adding
        // frame 3 evicts frame 1 (the keyframe), orphaning frame 3's delta.
        Check(CaptureFrame(r2, ctx, 1), "r2 frame 1");
        r2.AttachStateBlock(1, SE_LIVE_STATE_KIND_KEYFRAME, 1, (uint32_t)N, encKf.data(), encKf.size());
        Check(CaptureFrame(r2, ctx, 2), "r2 frame 2");
        r2.AttachStateBlock(2, SE_LIVE_STATE_KIND_DELTA, 1, (uint32_t)N, encD2.data(), encD2.size());
        // A 3rd frame pushes the ring past its cap; the front (frame 1, the keyframe) is evicted.
        Check(CaptureFrame(r2, ctx, 3), "r2 frame 3 evicts frame 1");
        r2.AttachStateBlock(3, SE_LIVE_STATE_KIND_DELTA, 1, (uint32_t)N, encD3.data(), encD3.size());
        Check(r2.Count() == 2 && r2.FrameNumber(0) == 2, "r2 ring holds frames 2,3");
        // Frame 3's delta (index 1) references the evicted keyframe (frame 1) -> not reconstructable.
        std::vector<uint8_t> out;
        Check(!r2.CanReconstruct(1) && !r2.ReconstructState(1, out),
              "delta with evicted keyframe is not reconstructable");
    }

    se_destroy(ctx);
    if (gFail == 0) std::printf("All FrameRecorder tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
