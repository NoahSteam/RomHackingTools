// Unit tests for FrameEdits.h: memory edits kept per recorded frame. Each case is one of the rules the
// owner set: an edit belongs to the frame it was made on, any number of frames hold edits at once, and
// edits go only when their frame leaves the recording (truncation, eviction, clear). A frame number
// reused on a new timeline must never inherit the replaced frame's edits.
#include "FrameEdits.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

constexpr uint32_t kWram = 0x06000000u;

// The byte every recorded frame holds at every address in these tests. An edit is a difference from it, so a
// value other than this is a real edit and this value is a write-back.
constexpr uint8_t kRecorded = 0xE7;

size_t Rec(FrameEditStore& s, const FrameKey& k, bool sound, uint32_t addr, const uint8_t* bytes, size_t len)
{
    const std::vector<uint8_t> was(len, kRecorded);
    return s.Record(k, sound, addr, bytes, len, was.data());
}

size_t Put(FrameEditStore& s, uint32_t timeline, uint64_t frame, uint32_t addr, uint8_t value, bool sound = false)
{
    return Rec(s, FrameKey{ timeline, frame }, sound, addr, &value, 1);
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
    Rec(s, FrameKey{ 1, 5 }, false, kWram + 1, wide, 3);
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
    Rec(w, FrameKey{ 1, 5 }, false, 0x05C7FFFFu, cross, 2);
    Check(w.Find(FrameKey{ 1, 5 })->Pokes().size() == 2, "and one write over the boundary is split at it");
}

// Runs stay sorted and disjoint across a block boundary, so the last write of a byte is the one that
// replays. (Found in review: a run touching the boundary from below was treated as mergeable and then
// not merged, so the new run went in FRONT of it; rewriting the lower byte then left the old value in a
// stale duplicate that was applied last.)
bool SortedDisjoint(const FrameEdits& e)
{
    const std::vector<StagedPoke>& r = e.Pokes();
    for (size_t i = 1; i < r.size(); ++i)
    {
        if (r[i - 1].isSound != r[i].isSound) { if (r[i - 1].isSound) return false; continue; }
        if (uint64_t(r[i - 1].addr) + r[i - 1].bytes.size() > r[i].addr) return false;
    }
    return true;
}

// What a replay in list order leaves at 'addr' (-1: untouched).
int Replayed(const FrameEdits& e, uint32_t addr)
{
    int v = -1;
    for (const StagedPoke& p : e.Pokes())
        if (!p.isSound && addr >= p.addr && addr - p.addr < p.bytes.size()) v = p.bytes[addr - p.addr];
    return v;
}

void TestBlockBoundaryOrder()
{
    const uint32_t lo = 0x05C7FFFFu, hi = 0x05C80000u;
    const FrameKey k{ 1, 5 };
    {   // lower byte, upper byte, lower byte again
        FrameEditStore s;
        Put(s, 1, 5, lo, 0x11);
        Put(s, 1, 5, hi, 0x22);
        Put(s, 1, 5, lo, 0x33);
        const FrameEdits* e = s.Find(k);
        Check(e && e->Pokes().size() == 2 && SortedDisjoint(*e), "boundary: two runs, sorted, no duplicate (lo, hi, lo)");
        Check(e && e->Pokes()[0].addr == lo && e->Pokes()[1].addr == hi, "boundary: the lower run comes first");
        Check(e && Replayed(*e, lo) == 0x33 && Replayed(*e, hi) == 0x22, "boundary: the last write of each byte replays");
        Check(e && e->ByteCount() == 2, "boundary: two distinct bytes are counted, not three");
    }
    {   // upper first, then lower, then both rewritten
        FrameEditStore s;
        Put(s, 1, 5, hi, 0x22);
        Put(s, 1, 5, lo, 0x11);
        Put(s, 1, 5, hi, 0x44);
        Put(s, 1, 5, lo, 0x55);
        const FrameEdits* e = s.Find(k);
        Check(e && e->Pokes().size() == 2 && SortedDisjoint(*e), "boundary: two runs, sorted (hi, lo, hi, lo)");
        Check(e && Replayed(*e, lo) == 0x55 && Replayed(*e, hi) == 0x44, "boundary: rewriting both sides keeps the latest of each");
    }
    {   // a run ending at the boundary, extended from below and rewritten across it
        FrameEditStore s;
        const uint8_t three[3] = { 1, 2, 3 };
        Rec(s, k, false, lo - 2, three, 3);          // ..FFFD, ..FFFE, ..FFFF
        Put(s, 1, 5, hi, 0x09);
        const uint8_t cross[4] = { 0xA, 0xB, 0xC, 0xD };   // ..FFFE, ..FFFF | hi, hi+1
        Rec(s, k, false, lo - 1, cross, 4);
        const FrameEdits* e = s.Find(k);
        Check(e && e->Pokes().size() == 2 && SortedDisjoint(*e), "boundary: a write across it updates both runs in place");
        Check(e && Replayed(*e, lo - 2) == 1 && Replayed(*e, lo - 1) == 0xA && Replayed(*e, lo) == 0xB &&
                  Replayed(*e, hi) == 0xC && Replayed(*e, hi + 1) == 0xD,
              "boundary: every byte holds its last write");
    }
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
        const size_t n = Rec(s, FrameKey{ 1, f }, false, kWram, big.data(), big.size());
        if (n == big.size()) ++taken; else if (n == 0) ++refused; else Check(false, "an edit is taken whole or not at all");
    }
    Check(taken > 0 && refused > 0 && s.Footprint() <= FrameEditStore::kMaxBytes,
          "the store refuses an edit that would pass its cap, and stays under it");
    // Rewriting bytes already held costs nothing, so it is still accepted at the cap.
    Check(Rec(s, FrameKey{ 1, 1 }, false, kWram, big.data(), 16) == 16, "replacing held bytes is accepted at the cap");
    s.Clear();
    Check(Rec(s, FrameKey{ 1, 1 }, false, kWram, big.data(), 16) == 16, "and a cleared store takes edits again");
}

// ---- Write-backs: an edit that equals the recorded byte is not an edit ----------------------------------

// Every invariant a frame's runs must keep, however writes and write-backs came: sorted by (isSound, addr),
// disjoint, none straddling a 512 KiB block, none touching its neighbour inside a block (touching runs would
// be one run), every byte different from its original, 'originals' as long as 'bytes'.
bool WellFormed(const FrameEdits& e)
{
    const std::vector<StagedPoke>& r = e.Pokes();
    for (size_t i = 0; i < r.size(); ++i)
    {
        if (r[i].bytes.empty() || r[i].originals.size() != r[i].bytes.size()) return false;
        for (size_t j = 0; j < r[i].bytes.size(); ++j)
            if (r[i].bytes[j] == r[i].originals[j]) return false;
        if (uint64_t(r[i].addr) / FrameEdits::kBlock !=
            (uint64_t(r[i].addr) + r[i].bytes.size() - 1) / FrameEdits::kBlock) return false;
        if (i == 0) continue;
        if (r[i - 1].isSound != r[i].isSound) { if (r[i - 1].isSound) return false; continue; }
        const uint64_t end = uint64_t(r[i - 1].addr) + r[i - 1].bytes.size();
        if (end > r[i].addr) return false;
        if (end == r[i].addr && r[i - 1].addr / FrameEdits::kBlock == r[i].addr / FrameEdits::kBlock) return false;
    }
    return true;
}

// The frame as RECORDED, at any address.
uint8_t RecordedAt(bool sound, uint32_t addr) { return uint8_t((addr * 37u + (sound ? 91u : 5u)) >> 1); }
uint8_t Rb(uint32_t addr) { return RecordedAt(false, addr); }

// Write 'bytes' the way the app does: originals are what the frame shows now (recorded, or the earlier edit).
size_t Write(FrameEditStore& s, const FrameKey& k, bool sound, uint32_t addr, const std::vector<uint8_t>& bytes)
{
    std::vector<uint8_t> shown(bytes.size());
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        shown[i] = RecordedAt(sound, addr + uint32_t(i));
        if (const FrameEdits* e = s.Find(k))
            for (const StagedPoke& p : e->Pokes())
                if (p.isSound == sound && addr + i >= p.addr && addr + i - p.addr < p.bytes.size())
                    shown[i] = p.bytes[addr + i - p.addr];
    }
    return s.Record(k, sound, addr, bytes.data(), bytes.size(), shown.data());
}

void TestWriteBack()
{
    const FrameKey k{ 1, 7 };
    const uint32_t a = kWram + 0x100;
    {   // the only byte: writing the recorded value back leaves the frame unedited
        FrameEditStore s;
        Write(s, k, false, a, { uint8_t(Rb(a) ^ 1) });
        Check(s.Has(k) && s.Footprint() > 0, "write-back: an edit is held");
        Check(Write(s, k, false, a, { Rb(a) }) == 1, "write-back: the write itself is accepted");
        Check(!s.Has(k) && !s.Any() && s.Footprint() == 0, "write-back: the last edited byte went, so the frame is not edited and holds nothing");
    }
    {   // a write of the recorded value onto a frame that has no edit at all is no edit either
        FrameEditStore s;
        Check(Write(s, k, false, a, { Rb(a) }) == 1 && !s.Has(k) && s.Footprint() == 0,
              "no-change: writing the recorded value over an unedited byte keeps nothing");
    }
    {   // first / middle / last byte of a run of five
        for (int which = 0; which < 3; ++which)
        {
            FrameEditStore s;
            std::vector<uint8_t> five;
            for (uint32_t i = 0; i < 5; ++i) five.push_back(uint8_t(Rb(a + i) ^ 0x80));
            Write(s, k, false, a, five);
            const uint32_t at = a + (which == 0 ? 0u : which == 1 ? 2u : 4u);
            Write(s, k, false, at, { Rb(at) });
            const FrameEdits* e = s.Find(k);
            const size_t runs = which == 1 ? 2 : 1;
            Check(e && WellFormed(*e) && e->Pokes().size() == runs && e->ByteCount() == 4,
                  which == 0 ? "write-back of a run's first byte shortens the run"
                  : which == 1 ? "write-back of a run's middle byte splits it in two"
                               : "write-back of a run's last byte shortens the run");
            Check(e && Replayed(*e, at) == -1, "and that byte is no longer replayed");
            Check(e && Replayed(*e, a + (which == 0 ? 1u : 0u)) == five[which == 0 ? 1 : 0], "the others still are");
            // re-edit after removal: the byte comes back as part of one run again
            Write(s, k, false, at, { uint8_t(Rb(at) ^ 0x21) });
            e = s.Find(k);
            Check(e && WellFormed(*e) && e->Pokes().size() == 1 && e->ByteCount() == 5 &&
                      Replayed(*e, at) == (Rb(at) ^ 0x21),
                  "re-editing a removed byte rejoins its neighbours");
            Check(e && e->Pokes()[0].originals[at - a] == Rb(at), "and its original is still the RECORDED byte");
        }
    }
    {   // an edit over an edit keeps the first original: writing the recorded value after two edits removes it
        FrameEditStore s;
        Write(s, k, false, a, { uint8_t(Rb(a) ^ 1) });
        Write(s, k, false, a, { uint8_t(Rb(a) ^ 2) });
        Check(s.Find(k) && s.Find(k)->Pokes()[0].originals[0] == Rb(a), "a second edit keeps the recorded byte as the original");
        Write(s, k, false, a, { Rb(a) });
        Check(!s.Has(k), "and the recorded value written after two edits removes the byte");
    }
    {   // across a block boundary: both runs shrink, none straddles
        const uint32_t lo = 0x05C7FFFEu, hi = 0x05C80000u;
        FrameEditStore s;
        std::vector<uint8_t> four;
        for (uint32_t i = 0; i < 4; ++i) four.push_back(uint8_t(Rb(lo + i) ^ 0x40));   // FFFE FFFF | 0000 0001
        Write(s, k, false, lo, four);
        Check(s.Find(k) && s.Find(k)->Pokes().size() == 2 && WellFormed(*s.Find(k)), "boundary: a write across it is two runs");
        Write(s, k, false, lo + 1, { Rb(lo + 1), Rb(hi) });   // write both middle bytes back, across the boundary
        const FrameEdits* e = s.Find(k);
        Check(e && WellFormed(*e) && e->Pokes().size() == 2 && e->ByteCount() == 2 &&
                  Replayed(*e, lo) == four[0] && Replayed(*e, hi + 1) == four[3] &&
                  Replayed(*e, lo + 1) == -1 && Replayed(*e, hi) == -1,
              "boundary: a write-back across it removes from both sides");
        Write(s, k, false, lo, { Rb(lo) });
        Write(s, k, false, hi + 1, { Rb(hi + 1) });
        Check(!s.Has(k), "boundary: and removing the rest leaves the frame unedited");
    }
    {   // other frames are untouched by one frame's write-back
        FrameEditStore s;
        Write(s, FrameKey{ 1, 8 }, false, a, { uint8_t(Rb(a) ^ 1) });
        Write(s, k, false, a, { uint8_t(Rb(a) ^ 1) });
        Write(s, k, false, a, { Rb(a) });
        Check(!s.Has(k) && s.Has(FrameKey{ 1, 8 }), "a write-back unedits only its own frame");
    }
}

void TestRandomAgainstOracle()
{
    // A byte map as the oracle: (sound, addr) -> edited value, present only while it differs from the recording.
    struct Rng
    {
        uint64_t x = 0x9E3779B97F4A7C15ull;
        uint32_t Next() { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return uint32_t(x >> 16); }
    } rng;
    const FrameKey k{ 3, 99 };
    FrameEditStore s;
    std::map<std::pair<bool, uint32_t>, uint8_t> oracle;
    // The bus window crosses the 512 KiB block boundary; sound offsets start at 0.
    const uint32_t busBase = 0x05C7FF00u;
    size_t removals = 0, splits = 0;
    for (int op = 0; op < 6000; ++op)
    {
        const bool sound = rng.Next() % 3 == 0;
        const uint32_t addr = (sound ? 0u : busBase) + rng.Next() % 0x200u;
        const size_t len = 1 + rng.Next() % 6;
        std::vector<uint8_t> bytes(len);
        for (size_t i = 0; i < len; ++i)
        {
            const uint8_t rec = RecordedAt(sound, addr + uint32_t(i));
            bytes[i] = rng.Next() % 100 < 45 ? rec : uint8_t(rng.Next());   // write-backs are common: runs split and heal often
        }
        const size_t runsBefore = s.Find(k) ? s.Find(k)->Pokes().size() : 0;
        Check(Write(s, k, sound, addr, bytes) == len, "oracle: every write is accepted");
        for (size_t i = 0; i < len; ++i)
        {
            const auto key = std::make_pair(sound, addr + uint32_t(i));
            if (bytes[i] == RecordedAt(sound, addr + uint32_t(i))) removals += oracle.erase(key);
            else oracle[key] = bytes[i];
        }
        const FrameEdits* e = s.Find(k);
        if (e && e->Pokes().size() > runsBefore + 1) ++splits;
        if (oracle.empty()) { Check(!e && !s.Any() && s.Footprint() == 0, "oracle: no edited bytes, no frame"); continue; }
        if (!e) { Check(false, "oracle: edited bytes but no frame"); break; }
        if (!WellFormed(*e) || e->ByteCount() != oracle.size())
        { Check(false, "oracle: runs stay sorted, disjoint, unstraddled, maximal and counted"); break; }
        bool same = true;
        size_t seen = 0;
        for (const StagedPoke& p : e->Pokes())
            for (size_t i = 0; i < p.bytes.size(); ++i, ++seen)
            {
                const auto it = oracle.find(std::make_pair(p.isSound, p.addr + uint32_t(i)));
                same = same && it != oracle.end() && it->second == p.bytes[i] &&
                       p.originals[i] == RecordedAt(p.isSound, p.addr + uint32_t(i));
            }
        if (!same || seen != oracle.size()) { Check(false, "oracle: every byte, value and original matches the byte map"); break; }
        if (s.Footprint() != 2 * oracle.size() + e->Pokes().size() * FrameEdits::kRunOverhead)
        { Check(false, "oracle: the footprint is twice the bytes plus the runs"); break; }
    }
    Check(removals > 200 && splits > 20, "oracle: the run exercised plenty of removals and splits");
}

void TestDropAndCountAfter()
{
    FrameEditStore s;
    for (uint64_t f = 10; f <= 15; ++f) Put(s, 1, f, kWram, uint8_t(f));
    Put(s, 2, 13, kWram, 0x21);   // the same number on another timeline: DropAfter goes by number alone
    const size_t foot = s.Footprint();
    Check(s.Drop(FrameKey{ 1, 12 }) && !s.Has(FrameKey{ 1, 12 }) && s.FrameCount() == 6 && s.Footprint() < foot,
          "Drop discards one frame's edits and its footprint");
    Check(!s.Drop(FrameKey{ 1, 12 }) && !s.Drop(FrameKey{ 9, 10 }), "Drop of a frame with none says so");
    Check(s.Has(FrameKey{ 1, 11 }) && s.Has(FrameKey{ 1, 13 }), "and touches no other frame");
    // CountAfter is DropAfter's own rule: for every frame, what it counts is what the drop then removes.
    bool agree = true;
    for (uint64_t f = 0; f <= 17; ++f)
    {
        FrameEditStore copy = s;
        const size_t counted = copy.CountAfter(f);
        agree = agree && copy.DropAfter(f) == counted && counted == s.CountAfter(f);
    }
    Check(agree, "CountAfter(frame) equals what DropAfter(frame) removes, for every frame");
    Check(s.CountAfter(12) == 4 && s.CountAfter(15) == 0 && s.CountAfter(0) == s.FrameCount(),
          "and counts frames on every timeline after the number");
}

void TestBoundWithOriginals()
{
    FrameEditStore s;
    std::vector<uint8_t> big(1u << 20, 0x5A), was(1u << 20, 0x00);
    Check(s.Record(FrameKey{ 1, 1 }, false, kWram, big.data(), big.size(), was.data()) == big.size(), "a 1 MiB edit is taken");
    Check(s.Footprint() == 2 * big.size() + 2 * FrameEdits::kRunOverhead,
          "its footprint counts the value and the original of each byte, plus a run per 512 KiB block");
    size_t refused = 0;
    for (uint64_t f = 2; f <= 12; ++f)
        if (s.Record(FrameKey{ 1, f }, false, kWram, big.data(), big.size(), was.data()) == 0) ++refused;
    Check(refused > 0 && s.Footprint() <= FrameEditStore::kMaxBytes, "the cap counts originals: fewer 1 MiB frames fit");
    // Over the cap, a write that WRITES BACK still fits: it frees what it removes.
    Check(s.Record(FrameKey{ 1, 1 }, false, kWram, was.data(), big.size(), big.data()) == big.size() && !s.Has(FrameKey{ 1, 1 }),
          "writing a whole frame's edits back is accepted at the cap and empties the frame");
}
}  // namespace

int main()
{
    TestPerFrame();
    TestReplaceAndMerge();
    TestBlockBoundaryOrder();
    TestTruncation();
    TestEviction();
    TestTimelineReuse();
    TestClear();
    TestPlayFromHereAppliesOnlyThatFrame();
    TestBound();
    TestWriteBack();
    TestRandomAgainstOracle();
    TestDropAndCountAfter();
    TestBoundWithOriginals();
    if (gFail == 0) std::printf("FrameEditsTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
