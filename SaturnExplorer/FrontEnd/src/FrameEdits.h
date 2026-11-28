// FrameEdits -- the memory edits made on recorded ("scrubbed") frames, kept PER FRAME and apart from App
// so the rules can be unit-tested against a ring that really evicts and truncates.
//
// An edit belongs to the recorded frame it was made on, and lives exactly as long as that frame is in
// the recording:
//   * the scrub view of a frame shows the recorded bytes with that frame's own edits laid over them
//     (however the user got there, and again after leaving and coming back);
//   * Play From Here on frame X sends X's edits and nobody else's;
//   * an edit goes away only when its frame does: truncation (Play From Here on an earlier frame),
//     eviction (the ring's front aging out) or the recording being cleared.
//
// A frame is named by FrameKey = (timeline, frame number), never by the number alone. A rewind replays
// frame numbers on a new timeline, so a number that comes back must not inherit the edits of the frame
// it replaced. The drop operations below are what normally prevent that; the key is the second line
// of defence, so a store that missed a drop still cannot hand one timeline's edits to another's frame.
//
// Memory: a frame's edits are a set of disjoint byte runs, so writing the same bytes twice replaces
// rather than accumulates, and the cost is the DISTINCT bytes edited (plus a small per-run overhead).
// The whole store is capped (kMaxBytes); an edit that would pass the cap is refused, whole, and the
// caller says so. The number of frames is bounded by the ring, because the App reconciles the store
// against the recorder every UI frame (App::ReconcileFrameEdits) and a frame that left it loses its
// edits.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "SeLiveProtocol.h"

namespace sfe
{

// A recorded frame's identity. timeline 0 means "not stated" (a server that does not stamp its
// captures, or a capture that lagged its stamp); it only equals another 0.
struct FrameKey
{
    uint32_t timeline = 0;
    uint64_t frame = 0;

    bool operator==(const FrameKey& o) const { return timeline == o.timeline && frame == o.frame; }
    bool operator!=(const FrameKey& o) const { return !(*this == o); }
    // Frame number first: the order the ring holds frames in (strictly ascending).
    bool operator<(const FrameKey& o) const
    {
        return frame != o.frame ? frame < o.frame : timeline < o.timeline;
    }
};

// One run of edited bytes. 'addr' is a bus address, except for sound RAM (isSound) where it is the
// 0-based offset the emulator's poke queue takes.
struct StagedPoke
{
    bool                 isSound = false;
    uint32_t             addr = 0;
    std::vector<uint8_t> bytes;
};

// What one frame's edits are: disjoint runs, sorted by (isSound, addr). Where two writes touch the
// same byte the later one wins, which is also what a replay in write order would have produced.
class FrameEdits
{
public:
    bool Empty() const { return mRuns.empty(); }
    const std::vector<StagedPoke>& Pokes() const { return mRuns; }
    // Distinct bytes edited, and the same plus a fixed per-run cost (what the store's cap counts).
    size_t ByteCount() const
    {
        size_t n = 0;
        for (const StagedPoke& p : mRuns) n += p.bytes.size();
        return n;
    }
    size_t Footprint() const { return ByteCount() + mRuns.size() * kRunOverhead; }

    // Lay 'len' bytes at 'addr' over the edits. Runs that overlap or touch merge into one, except
    // across a 512 KiB boundary: VDP1 VRAM is followed directly by the frame buffer on the bus, and
    // the frame buffer is not applied by every server, so a run must never straddle the two.
    void Add(bool isSound, uint32_t addr, const uint8_t* bytes, size_t len)
    {
        if (!bytes || len == 0) return;
        // A write is clipped at the end of the address space rather than wrapping onto address 0.
        const uint64_t room = (uint64_t(1) << 32) - addr;
        if (len > room) len = static_cast<size_t>(room);
        // ...and at the next block boundary, the rest going in as a run of its own.
        while (len > 0)
        {
            const uint64_t blockEnd = (uint64_t(addr) | (kBlock - 1)) + 1;
            const size_t   part = static_cast<size_t>((std::min)(static_cast<uint64_t>(len), blockEnd - addr));
            AddWithinBlock(isSound, addr, bytes, part);
            addr = static_cast<uint32_t>(uint64_t(addr) + part);
            bytes += part;
            len -= part;
        }
    }

    static constexpr size_t kRunOverhead = 32;          // vector + header, charged per run
    static constexpr uint64_t kBlock = 0x80000u;        // runs never merge across this

private:
    static bool Before(const StagedPoke& r, bool isSound, uint64_t addr)
    {
        // r ends strictly before 'addr' (a touching run is not "before": it merges).
        if (r.isSound != isSound) return !r.isSound && isSound;
        return uint64_t(r.addr) + r.bytes.size() < addr;
    }

    void AddWithinBlock(bool isSound, uint32_t addr, const uint8_t* bytes, size_t len)
    {
        const uint64_t lo = addr, hi = uint64_t(addr) + len;
        size_t first = 0;
        while (first < mRuns.size() && Before(mRuns[first], isSound, lo)) ++first;
        size_t last = first;   // one past the last run that overlaps or touches [lo, hi)
        uint64_t mergedLo = lo, mergedHi = hi;
        while (last < mRuns.size() && mRuns[last].isSound == isSound && mRuns[last].addr <= hi &&
               SameBlock(mRuns[last].addr, addr))
        {
            mergedLo = (std::min)(mergedLo, uint64_t(mRuns[last].addr));
            mergedHi = (std::max)(mergedHi, uint64_t(mRuns[last].addr) + mRuns[last].bytes.size());
            ++last;
        }
        StagedPoke merged;
        merged.isSound = isSound;
        merged.addr = static_cast<uint32_t>(mergedLo);
        merged.bytes.assign(static_cast<size_t>(mergedHi - mergedLo), 0);
        for (size_t i = first; i < last; ++i)
            std::copy(mRuns[i].bytes.begin(), mRuns[i].bytes.end(),
                      merged.bytes.begin() + static_cast<std::ptrdiff_t>(mRuns[i].addr - mergedLo));
        std::copy(bytes, bytes + len, merged.bytes.begin() + static_cast<std::ptrdiff_t>(lo - mergedLo));
        mRuns.erase(mRuns.begin() + static_cast<std::ptrdiff_t>(first), mRuns.begin() + static_cast<std::ptrdiff_t>(last));
        mRuns.insert(mRuns.begin() + static_cast<std::ptrdiff_t>(first), std::move(merged));
    }
    static bool SameBlock(uint32_t a, uint32_t b) { return (a / kBlock) == (b / kBlock); }

    std::vector<StagedPoke> mRuns;
};

// Every frame's edits, by frame. UI thread only.
class FrameEditStore
{
public:
    // Distinct bytes (plus run overhead) the whole store may hold. Whole work RAM is 2 MiB, so this is
    // room for several frames each rewritten end to end; a hand-typed session is orders below it.
    static constexpr size_t kMaxBytes = 16u * 1024u * 1024u;

    bool Any() const { return !mFrames.empty(); }
    size_t FrameCount() const { return mFrames.size(); }
    size_t Footprint() const { return mFootprint; }

    bool Has(const FrameKey& key) const { return Find(key) != nullptr; }
    // This frame's edits, or null when it has none. Valid until the store is next changed.
    const FrameEdits* Find(const FrameKey& key) const
    {
        const auto it = mFrames.find(key);
        return it == mFrames.end() ? nullptr : &it->second;
    }
    // Frames holding edits, ascending by frame number (for marking them on the scrub bar).
    std::vector<FrameKey> Frames() const
    {
        std::vector<FrameKey> out;
        out.reserve(mFrames.size());
        for (const auto& kv : mFrames) out.push_back(kv.first);
        return out;
    }

    // Add (or replace, byte for byte) an edit on 'key'. Returns the bytes taken: 'len', or 0 when it
    // was refused -- no such frame (0), nothing to write, or the store would pass kMaxBytes. Never
    // partial, so the view is never shown an edit the replay does not have.
    size_t Record(const FrameKey& key, bool isSound, uint32_t addr, const uint8_t* bytes, size_t len)
    {
        if (key.frame == 0 || !bytes || len == 0) return 0;
        auto it = mFrames.find(key);
        const size_t before = it == mFrames.end() ? 0 : it->second.Footprint();
        // Growth is at most the bytes plus one new run per block touched; only when that could pass
        // the cap is the merge tried on a copy first.
        const size_t blocks = len / FrameEdits::kBlock + 2;
        const bool mayExceed = mFootprint + len + blocks * FrameEdits::kRunOverhead > kMaxBytes;
        if (!mayExceed)
        {
            FrameEdits& e = it == mFrames.end() ? mFrames[key] : it->second;
            e.Add(isSound, addr, bytes, len);
            mFootprint = mFootprint - before + e.Footprint();
            return len;
        }
        FrameEdits trial = it == mFrames.end() ? FrameEdits() : it->second;
        trial.Add(isSound, addr, bytes, len);
        const size_t after = trial.Footprint();
        if (mFootprint - before + after > kMaxBytes) return 0;
        mFrames[key] = std::move(trial);
        mFootprint = mFootprint - before + after;
        return len;
    }

    // Play From Here on frame 'frame' discards every recorded frame after it: those frames' edits go.
    // Matches by number alone, whatever the timeline: it is the cut in the ring that decides.
    size_t DropAfter(uint64_t frame)
    {
        return DropIf([&](const FrameKey& k) { return k.frame > frame; });
    }
    // The ring aged out every frame before 'oldest' (its current front): their edits go.
    size_t DropBefore(uint64_t oldest)
    {
        return DropIf([&](const FrameKey& k) { return k.frame < oldest; });
    }
    // Keep only the frames 'present(key)' accepts; the rest were discarded from the recording by some
    // path nobody told the store about. Returns how many frames were dropped.
    template <class Pred>
    size_t Retain(Pred present)
    {
        return DropIf([&](const FrameKey& k) { return !present(k); });
    }
    void Clear()
    {
        mFrames.clear();
        mFootprint = 0;
    }

private:
    template <class Pred>
    size_t DropIf(Pred drop)
    {
        size_t n = 0;
        for (auto it = mFrames.begin(); it != mFrames.end();)
        {
            if (drop(it->first))
            {
                mFootprint -= it->second.Footprint();
                it = mFrames.erase(it);
                ++n;
            }
            else ++it;
        }
        return n;
    }

    std::map<FrameKey, FrameEdits> mFrames;
    size_t                         mFootprint = 0;
};

// The LST edit blob (SE_LIVE_EDIT_* layout) Play From Here sends: u32 count, then per poke
// type(1) + pad(3) + addr(4) + len(4) + bytes, all little-endian. The emulator replays them in
// this order on top of the restored state. 'edits' is ONE frame's: nothing else rides along.
inline std::vector<uint8_t> EncodeEditBlob(const FrameEdits& edits)
{
    std::vector<uint8_t> blob;
    auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) blob.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    put32(static_cast<uint32_t>(edits.Pokes().size()));
    for (const StagedPoke& p : edits.Pokes())
    {
        blob.push_back(p.isSound ? static_cast<uint8_t>(SE_LIVE_EDIT_TYPE_SOUND)
                                 : static_cast<uint8_t>(SE_LIVE_EDIT_TYPE_WRAM));
        blob.push_back(0); blob.push_back(0); blob.push_back(0);
        put32(p.addr);
        put32(static_cast<uint32_t>(p.bytes.size()));
        blob.insert(blob.end(), p.bytes.begin(), p.bytes.end());
    }
    return blob;
}

}  // namespace sfe
