// Unit tests for ScrubState: what the scrub view shows and what staged edits belong to, against a ring
// that evicts from the front the way FrameRecorder's does. Each case is a sequence that was seen to go
// wrong when the view or the edits were tracked by index.
#include "ScrubState.h"

#include <cstdio>
#include <deque>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

// A ring of frame numbers capped at 'cap': publishing evicts the oldest, shifting every index down.
struct Ring
{
    std::deque<uint64_t> frames;
    size_t cap;
    explicit Ring(size_t c, uint64_t first, uint64_t last) : cap(c)
    {
        for (uint64_t f = first; f <= last; ++f) Publish(f);
    }
    void Publish(uint64_t f)
    {
        frames.push_back(f);
        while (frames.size() > cap) frames.pop_front();
    }
    size_t Count() const { return frames.size(); }
    int IndexOfFrame(uint64_t f) const
    {
        for (size_t i = 0; i < frames.size(); ++i)
            if (frames[i] == f) return static_cast<int>(i);
        return -1;
    }
};

// The view as App holds it between refreshes.
struct View
{
    int index = -1, shownIndex = -1;
    uint64_t shownFrame = 0, target = 0;
    bool haveContext = false, edited = false;
    int reloads = 0;   // how many times the context was rebuilt from the recording

    // One refresh against 'ring', applying the plan the way App does. Returns the frame now shown.
    ScrubPlan Refresh(const Ring& ring)
    {
        const ScrubPlan plan = PlanScrub(ring, haveContext, index, shownIndex, shownFrame, target, edited);
        target = 0;
        switch (plan.kind)
        {
        case ScrubPlan::Nothing: break;
        case ScrubPlan::Keep: index = shownIndex = plan.index; break;   // no reload: the context stays as it is
        case ScrubPlan::SelectFrame:
            index = ring.IndexOfFrame(plan.frame);
            if (index < 0) { shownIndex = -1; shownFrame = 0; break; }
            shownIndex = index; shownFrame = plan.frame; haveContext = true;
            ++reloads; edited = false;
            break;
        case ScrubPlan::SelectIndex:
            index = shownIndex = plan.index;
            shownFrame = ring.frames[static_cast<size_t>(plan.index)];
            haveContext = true;
            ++reloads; edited = false;
            break;
        }
        return plan;
    }
};

void TestPlanBasics()
{
    Ring empty(3, 1, 0);
    View v;
    Check(v.Refresh(empty).kind == ScrubPlan::Nothing, "an empty ring shows nothing");

    Ring ring(3, 10, 12);
    View first;
    first.index = 1;
    Check(first.Refresh(ring).kind == ScrubPlan::SelectIndex && first.shownFrame == 11, "no view yet: show what the index names");

    View clamp;
    clamp.index = -5;
    clamp.Refresh(ring);
    Check(clamp.shownFrame == 10, "a negative index clamps to the oldest");
    View high;
    high.index = 99;
    high.Refresh(ring);
    Check(high.shownFrame == 12, "an index past the end clamps to the newest");
}

void TestUnmovedViewFollowsItsFrame()
{
    Ring ring(3, 10, 12);
    View v;
    v.index = 2;
    v.Refresh(ring);   // showing frame 12 at index 2
    Check(v.shownFrame == 12, "showing frame 12");

    ring.Publish(13);   // ring {11,12,13}: frame 12 is now at index 1
    ScrubPlan p = v.Refresh(ring);
    Check(p.kind == ScrubPlan::Keep && v.index == 1 && v.shownFrame == 12, "the unmoved view follows frame 12 to index 1");

    ring.Publish(14);   // ring {12,13,14}: frame 12 at index 0
    p = v.Refresh(ring);
    Check(p.kind == ScrubPlan::Keep && v.index == 0 && v.shownFrame == 12,
          "and on the next publication to index 0, never drifting to 13");

    ring.Publish(15);   // frame 12 has left
    p = v.Refresh(ring);
    Check(p.kind == ScrubPlan::SelectIndex && v.shownFrame == 13, "once its frame has left the ring it shows what the index names");
}

void TestNavigationPersists()
{
    // Go to frame 12, then more frames are published: the view must not slide off it (the second
    // refresh used to select by index again).
    Ring ring(3, 10, 12);
    View v;
    v.index = 0;
    v.Refresh(ring);
    ring.Publish(13);                     // {11,12,13}
    v.target = 12;
    ScrubPlan p = v.Refresh(ring);
    Check(p.kind == ScrubPlan::SelectFrame && v.index == 1 && v.shownFrame == 12, "Go to 12 opens frame 12 at index 1");
    ring.Publish(14);                     // {12,13,14}
    v.Refresh(ring);
    Check(v.shownFrame == 12 && v.index == 0, "the next publication leaves it on 12 (now index 0), not 13");
    ring.Publish(15);                     // 12 evicted
    v.Refresh(ring);
    Check(v.shownFrame == 13, "only when 12 is actually gone does the view move");
}

void TestNavigationToEvictedFrame()
{
    Ring ring(3, 11, 13);
    View v;
    v.index = 0;
    v.Refresh(ring);
    v.target = 10;   // evicted before the refresh
    v.Refresh(ring);
    Check(v.shownFrame == 0 && v.shownIndex == -1, "navigating to an evicted frame is refused, not shown as a neighbour");
}

void TestUserSeekWins()
{
    Ring ring(5, 10, 14);
    View v;
    v.index = 4;
    v.Refresh(ring);   // frame 14
    v.index = 1;       // the slider moved
    ScrubPlan p = v.Refresh(ring);
    Check(p.kind == ScrubPlan::SelectIndex && v.shownFrame == 11, "an explicit seek shows the frame at the new index");
    v.index = 2;
    v.target = 13;   // a navigation overrides a pending index change
    p = v.Refresh(ring);
    Check(p.kind == ScrubPlan::SelectFrame && v.shownFrame == 13, "a navigation names its own frame");
}

void TestSameFrameNavigationKeepsTheEditedContext()
{
    Ring ring(5, 10, 12);
    View v;
    v.index = 2;
    v.Refresh(ring);   // showing frame 12, one reload
    const int before = v.reloads;
    v.target = 12;     // Go to the frame that is already shown
    const ScrubPlan p = v.Refresh(ring);
    Check(p.kind == ScrubPlan::Keep && v.reloads == before,
          "navigating to the shown frame keeps the context (and so its edits) instead of reloading it");
    ring.Publish(13);
    ring.Publish(14);
    v.target = 12;
    const ScrubPlan q = v.Refresh(ring);
    Check(q.kind == ScrubPlan::Keep && v.shownFrame == 12 && v.index == ring.IndexOfFrame(12) && v.reloads == before,
          "also after the ring shifted: still no reload, index re-pointed");
    v.target = 13;
    v.Refresh(ring);
    Check(v.shownFrame == 13 && v.reloads == before + 1, "a different frame does reload");
}

void TestForceReload()
{
    // Staged edits were discarded while the context still shows them (Play, an abandoned frame): the
    // frame must be rebuilt from the recording before it is shown again, not kept as it is.
    Ring ring(5, 10, 12);
    View v;
    v.index = 2;
    v.Refresh(ring);
    const int before = v.reloads;
    v.edited = true;
    ScrubPlan p = v.Refresh(ring);   // the user has not moved
    Check(p.kind == ScrubPlan::SelectFrame && p.frame == 12 && v.reloads == before + 1 && !v.edited,
          "an unmoved view with discarded edits is rebuilt, by frame number");
    v.edited = true;
    v.target = 12;
    p = v.Refresh(ring);
    Check(p.kind == ScrubPlan::SelectFrame && v.reloads == before + 2,
          "so is a navigation to the same frame");
    v.edited = true;
    ring.Publish(13);
    ring.Publish(14);
    ring.Publish(15);
    ring.Publish(16);
    ring.Publish(17);   // frame 12 is gone (the ring holds 13..17)
    p = v.Refresh(ring);
    Check(p.kind == ScrubPlan::SelectIndex, "if the frame has left the ring it falls back to the index");
}

void TestStagedEdits()
{
    const uint8_t a[] = { 1, 2, 3 };
    StagedEdits e;
    Check(e.Empty() && !e.BelongsTo(11), "starts empty");
    e.Record(11, false, 0x06000000u, a, 1);
    e.Record(11, false, 0x06000001u, a + 1, 1);   // continues the run
    e.Record(11, false, 0x06000010u, a + 2, 1);   // a new run
    e.Record(11, true, 0x00000011u, a, 1);        // sound RAM is its own run
    Check(e.Pokes().size() == 3 && e.Pokes()[0].bytes.size() == 2, "contiguous writes extend a poke");
    Check(e.BelongsTo(11) && !e.BelongsTo(12), "the batch belongs to the frame it was staged on");
}

void TestStagedEditsDoNotCrossFrames()
{
    // Edit frame 11 (index 1 of {10,11,12}); eviction moves frame 12 to index 1; navigate to 12.
    // A reload clears the batch (App does that when the shown frame is rebuilt)...
    const uint8_t b[] = { 0xAA };
    StagedEdits e;
    e.Record(11, false, 0x06000000u, b, 1);
    e.Clear();
    Check(e.Empty() && !e.BelongsTo(11) && !e.BelongsTo(12), "a reload drops the other frame's edit");
    e.Record(12, false, 0x06000100u, b, 1);
    Check(e.Pokes().size() == 1 && e.Pokes()[0].addr == 0x06000100u && e.BelongsTo(12),
          "so a later edit tags only itself as frame 12");

    // ...and even if it did not run first, recording on another frame must not retag the batch.
    StagedEdits late;
    late.Record(11, false, 0x06000000u, b, 1);
    late.Record(12, false, 0x06000100u, b, 1);
    Check(late.Pokes().size() == 1 && late.Pokes()[0].addr == 0x06000100u && late.BelongsTo(12) && !late.BelongsTo(11),
          "an edit on another frame drops the old batch instead of joining it under the new tag");

    StagedEdits same;
    same.Record(11, false, 0x06000000u, b, 1);
    same.Record(11, false, 0x06000010u, b, 1);
    Check(same.Pokes().size() == 2 && same.BelongsTo(11), "more edits on the same frame join its batch");
    same.Clear();
    Check(same.Empty() && !same.BelongsTo(11), "Clear empties the batch and its tag");
}
}  // namespace

int main()
{
    TestPlanBasics();
    TestUnmovedViewFollowsItsFrame();
    TestNavigationPersists();
    TestNavigationToEvictedFrame();
    TestUserSeekWins();
    TestSameFrameNavigationKeepsTheEditedContext();
    TestForceReload();
    TestStagedEdits();
    TestStagedEditsDoNotCrossFrames();
    if (gFail == 0) std::printf("ScrubStateTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
