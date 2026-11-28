// Unit tests for FrameEdits.h: memory edits kept per recorded frame. Each case is one of the rules the
// owner set: an edit belongs to the frame it was made on, any number of frames hold edits at once, and
// edits go only when their frame leaves the recording (truncation, eviction, clear). A frame number
// reused on a new timeline must never inherit the replaced frame's edits.
#include "FrameEdits.h"

#include <cstdio>
#include <cstring>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

constexpr uint32_t kWram = 0x06000000u;

size_t Put(FrameEditStore& s, uint32_t timeline, uint64_t frame, uint32_t addr, uint8_t value, bool sound = false)
{
    return s.Record(FrameKey{ timeline, frame }, sound, addr, &value, 1);
}

// The poke list of a frame as a flat description, for comparing against the expected edits.
bool Has(const FrameEditStore& s, uint32_t timeline, uint64_t frame, uint32_t addr, uint8_t value)
{
    const FrameEdits* e = s.Find(FrameKey{ timeline, frame });
    if (!e) return false;
    for (const StagedPoke& p : e->Pokes())
        if (!p.isSound && addr >= p.addr && addr - p.addr < p.bytes.size()) return p.bytes[addr - p.addr] == value;
    return false;
}

void TestPerFrame()
{
    FrameEditStore s;
    Check(!s.Any() && s.FrameCount() == 0, "starts with no edits on any frame");
    Check(Put(s, 1, 11, kWram, 0xAA) == 1, "an edit is taken");
    Check(s.Any() && s.Has(FrameKey{ 1, 11 }) && !s.Has(FrameKey{ 1, 12 }), "it belongs to frame 11 only");
    // An edit on frame 12 neither joins nor clears frame 11's (the old batch did one or the other).
    Put(s, 1, 12, kWram + 4, 0xBB);
    Check(s.FrameCount() == 2 && Has(s, 1, 11, kWram, 0xAA) && Has(s, 1, 12, kWram + 4, 0xBB),
          "two frames hold their own edits at the same time");
    Check(!Has(s, 1, 11, kWram + 4, 0xBB) && !Has(s, 1, 12, kWram, 0xAA), "and neither shows the other's");
    // Return to frame 11: its edits are still there, however many frames were visited between.
    Put(s, 1, 13, kWram, 0xCC);
    Check(Has(s, 1, 11, kWram, 0xAA), "returning to frame 11 finds its edit");
    const std::vector<FrameKey> frames = s.Frames();
    Check(frames.size() == 3 && frames[0].frame == 11 && frames[1].frame == 12 && frames[2].frame == 13,
          "the edited frames are listed in ring order");
    Check(Put(s, 1, 0, kWram, 1) == 0 && !s.Has(FrameKey{ 1, 0 }), "frame 0 (nothing recorded) takes no edit");
}

void TestReplaceAndMerge()
{
    FrameEditStore s;
    Put(s, 1, 5, kWram, 0x11);
    Put(s, 1, 5, kWram, 0x22);
    const FrameEdits* e = s.Find(FrameKey{ 1, 5 });
    Check(e && e->Pokes().size() == 1 && e->Pokes()[0].bytes.size() == 1 && e->Pokes()[0].bytes[0] == 0x22,
          "a second edit of the same byte replaces the first");
    Put(s, 1, 5, kWram + 1, 0x33);   // touches: one run
    Put(s, 1, 5, kWram + 3, 0x44);   // gap: another
    e = s.Find(FrameKey{ 1, 5 });
    Check(e->Pokes().size() == 2 && e->Pokes()[0].bytes.size() == 2, "contiguous writes extend a run, a gap starts one");
    Put(s, 1, 5, kWram + 2, 0x55);   // fills the gap: runs merge
    e = s.Find(FrameKey{ 1, 5 });
    Check(e->Pokes().size() == 1 && e->Pokes()[0].bytes.size() == 4 && e->Pokes()[0].bytes[2] == 0x55 &&
              e->Pokes()[0].bytes[3] == 0x44 && e->Pokes()[0].bytes[0] == 0x22,
          "a write that bridges two runs merges them and keeps every byte");
    const uint8_t wide[3] = { 1, 2, 3 };
    s.Record(FrameKey{ 1, 5 }, false, kWram + 1, wide, 3);
    e = s.Find(FrameKey{ 1, 5 });
    Check(e->Pokes().size() == 1 && e->Pokes()[0].bytes == std::vector<uint8_t>({ 0x22, 1, 2, 3 }),
          "a wider write overwrites what it covers, later wins");
    Put(s, 1, 5, 0x10, 0x66, true);
    Check(s.Find(FrameKey{ 1, 5 })->Pokes().size() == 2, "sound RAM offsets are runs of their own, never merged with bus addresses");
    // VDP1 VRAM ends where the frame buffer begins: a run must not straddle the two.
    FrameEditStore v;
    Put(v, 1, 5, 0x05C7FFFFu, 0x01);
    Put(v, 1, 5, 0x05C80000u, 0x02);
    Check(v.Find(FrameKey{ 1, 5 })->Pokes().size() == 2, "VDP1 VRAM and frame-buffer edits stay separate runs");
    const uint8_t cross[2] = { 7, 8 };
    FrameEditStore w;
    w.Record(FrameKey{ 1, 5 }, false, 0x05C7FFFFu, cross, 2);
    Check(w.Find(FrameKey{ 1, 5 })->Pokes().size() == 2, "and one write over the boundary is split at it");
}

void TestTruncation()
{
    FrameEditStore s;
    for (uint64_t f = 10; f <= 14; ++f) Put(s, 1, f, kWram, uint8_t(f));
    Check(s.DropAfter(12) == 2, "Play From Here on 12 drops the two later frames");
    Check(s.Has(FrameKey{ 1, 10 }) && s.Has(FrameKey{ 1, 11 }) && s.Has(FrameKey{ 1, 12 }) &&
              !s.Has(FrameKey{ 1, 13 }) && !s.Has(FrameKey{ 1, 14 }),
          "only the frames after 12 lost their edits; 12 and the frames before it keep theirs");
    Check(s.DropAfter(99) == 0 && s.FrameCount() == 3, "truncating past the newest drops nothing");
}

void TestEviction()
{
    FrameEditStore s;
    for (uint64_t f = 10; f <= 14; ++f) Put(s, 1, f, kWram, uint8_t(f));
    Check(s.DropBefore(12) == 2, "the ring's front moved to 12: frames 10 and 11 aged out");
    Check(!s.Has(FrameKey{ 1, 10 }) && !s.Has(FrameKey{ 1, 11 }) && s.Has(FrameKey{ 1, 12 }) && s.FrameCount() == 3,
          "only the aged-out frames lost their edits");
    // Retain: the same through whatever predicate the ring answers.
    Check(s.Retain([](const FrameKey& k) { return k.frame != 13; }) == 1 && !s.Has(FrameKey{ 1, 13 }) && s.Has(FrameKey{ 1, 14 }),
          "Retain drops the frames the recording no longer holds");
}

void TestTimelineReuse()
{
    FrameEditStore s;
    Put(s, 1, 20, kWram, 0xAA);
    Put(s, 1, 21, kWram, 0xBB);
    // Rewind to 20: frame 21 is discarded and the number comes back on timeline 2.
    s.DropAfter(20);
    Check(!s.Has(FrameKey{ 2, 21 }) && !s.Has(FrameKey{ 1, 21 }), "the number reused on the new timeline holds no edits");
    // A store that was NOT told still must not hand them over: the key differs.
    FrameEditStore t;
    Put(t, 1, 21, kWram, 0xBB);
    Check(!t.Has(FrameKey{ 2, 21 }) && t.Find(FrameKey{ 2, 21 }) == nullptr,
          "even without a drop, frame 21 of timeline 2 does not inherit frame 21 of timeline 1");
    Check(t.Retain([](const FrameKey& k) { return k.timeline == 2; }) == 1 && !t.Any(),
          "and reconciling against a ring that only holds timeline 2 drops the stale entry");
    // Unknown timeline (0) only equals another 0.
    FrameEditStore u;
    Put(u, 0, 5, kWram, 1);
    Check(u.Has(FrameKey{ 0, 5 }) && !u.Has(FrameKey{ 3, 5 }), "timeline 0 is its own identity");
}

void TestClear()
{
    FrameEditStore s;
    Put(s, 1, 1, kWram, 1);
    Put(s, 2, 2, kWram, 2);
    s.Clear();
    Check(!s.Any() && s.FrameCount() == 0 && s.Footprint() == 0, "Clear drops every frame's edits and the footprint");
}

// What Play From Here sends for frame A: A's edits only.
void TestPlayFromHereAppliesOnlyThatFrame()
{
    FrameEditStore s;
    Put(s, 1, 10, kWram + 0x10, 0x10);   // frame before A
    Put(s, 1, 11, kWram + 0x20, 0x20);   // A
    Put(s, 1, 11, kWram + 0x21, 0x21);
    Put(s, 1, 12, kWram + 0x30, 0x30);   // frame after A
    const FrameEdits* a = s.Find(FrameKey{ 1, 11 });
    const std::vector<uint8_t> blob = EncodeEditBlob(*a);
    auto get32 = [&](size_t off) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t(blob[off + i]) << (8 * i); return v; };
    Check(blob.size() == 4 + 12 + 2 && get32(0) == 1, "the blob carries one poke: A's two bytes");
    Check(get32(4 + 4) == kWram + 0x20 && get32(4 + 8) == 2 && blob[16] == 0x20 && blob[17] == 0x21,
          "at A's address with A's bytes");
    bool leaked = false;
    for (size_t i = 16; i < blob.size(); ++i) leaked = leaked || blob[i] == 0x10 || blob[i] == 0x30;
    Check(!leaked, "nothing of frame 10's or frame 12's is in it");
    // Truncating at A leaves the earlier frame's edits attached to it and drops the later frame's.
    s.DropAfter(11);
    Check(s.Has(FrameKey{ 1, 10 }) && s.Has(FrameKey{ 1, 11 }) && !s.Has(FrameKey{ 1, 12 }),
          "after Play From Here on A: earlier frames keep their edits, A keeps its own, later frames lose theirs");
    const std::vector<uint8_t> none = EncodeEditBlob(FrameEdits());
    Check(none.size() == 4 && none[0] == 0, "a frame with no edits sends an empty blob");
}

void TestBound()
{
    FrameEditStore s;
    std::vector<uint8_t> big(1u << 20, 0x5A);
    size_t taken = 0, refused = 0;
    // Distinct bytes on many frames, each 1 MiB: the store stops at its cap, and never takes a write
    // partially.
    for (uint64_t f = 1; f <= 40; ++f)
    {
        const size_t n = s.Record(FrameKey{ 1, f }, false, kWram, big.data(), big.size());
        if (n == big.size()) ++taken; else if (n == 0) ++refused; else Check(false, "an edit is taken whole or not at all");
    }
    Check(taken > 0 && refused > 0 && s.Footprint() <= FrameEditStore::kMaxBytes,
          "the store refuses an edit that would pass its cap, and stays under it");
    // Rewriting bytes already held costs nothing, so it is still accepted at the cap.
    Check(s.Record(FrameKey{ 1, 1 }, false, kWram, big.data(), 16) == 16, "replacing held bytes is accepted at the cap");
    s.Clear();
    Check(s.Record(FrameKey{ 1, 1 }, false, kWram, big.data(), 16) == 16, "and a cleared store takes edits again");
}
}  // namespace

int main()
{
    TestPerFrame();
    TestReplaceAndMerge();
    TestTruncation();
    TestEviction();
    TestTimelineReuse();
    TestClear();
    TestPlayFromHereAppliesOnlyThatFrame();
    TestBound();
    if (gFail == 0) std::printf("FrameEditsTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
