// ScrubState -- the two rules about frame identity that scrubbing the rewind history depends on, kept
// apart from App so they can be unit-tested against a ring that really evicts.
//
// The recorder's ring evicts from the front, so an INDEX names a different frame as frames are
// published. Everything that has to stay with a frame (the view, a navigation's target, edits staged
// against it) is therefore keyed by frame number:
//   * PlanScrub decides what the next refresh shows.
//   * StagedEdits owns the edits staged on a scrubbed frame and the one frame they belong to.
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace sfe
{

struct ScrubPlan
{
    enum Kind
    {
        Nothing,       // no recorded frames: nothing to show
        Keep,          // the frame on screen is still the one to show; 'index' is where it now sits
        SelectFrame,   // show frame number 'frame' (a navigation), refused if it has left the ring
        SelectIndex    // show whatever 'index' names (the user moved the slider or the step buttons)
    };
    Kind     kind = Nothing;
    int      index = -1;
    uint64_t frame = 0;
};

// What the next refresh of the scrub view should show. 'ring' provides Count() and
// IndexOfFrame(frame) (the FrameRecorder does). Inputs are the state left by the last refresh:
// 'index' is the selected index (the slider and step buttons change it), 'shownIndex' and
// 'shownFrame' what was built, and 'targetFrame' a pending navigation (0 for none).
//
// A view the user has not moved keeps showing the same FRAME: the index is re-pointed at it as the
// ring shifts, instead of the old index silently opening a neighbour. A frame that has left the
// ring falls back to whatever the index now names.
//
// The context on screen may hold edits made to it. Keeping it (no reload) keeps them, so navigating
// to the frame already shown is a Keep, not a reload that would show the recorded bytes while the
// edits were still staged. 'forceReload' says the displayed edits were discarded (staged edits
// dropped without rebuilding the context): the frame is then rebuilt from the recording.
template <class Ring>
ScrubPlan PlanScrub(const Ring& ring, bool haveContext, int index, int shownIndex, uint64_t shownFrame,
                    uint64_t targetFrame, bool forceReload)
{
    ScrubPlan plan;
    const int n = static_cast<int>(ring.Count());
    if (n == 0) return plan;
    const bool showing = haveContext && shownFrame != 0;
    const int shownNow = showing ? ring.IndexOfFrame(shownFrame) : -1;
    if (targetFrame != 0)
    {
        if (showing && targetFrame == shownFrame && shownNow >= 0 && !forceReload)
        {
            plan.kind = ScrubPlan::Keep;
            plan.index = shownNow;
            return plan;
        }
        plan.kind = ScrubPlan::SelectFrame;
        plan.frame = targetFrame;
        return plan;
    }
    if (showing && index == shownIndex && shownNow >= 0)
    {
        if (forceReload)
        {
            plan.kind = ScrubPlan::SelectFrame;
            plan.frame = shownFrame;
        }
        else
        {
            plan.kind = ScrubPlan::Keep;
            plan.index = shownNow;
        }
        return plan;
    }
    plan.kind = ScrubPlan::SelectIndex;
    plan.index = index < 0 ? 0 : index >= n ? n - 1 : index;
    return plan;
}

struct StagedPoke
{
    bool                 isSound = false;
    uint32_t             addr = 0;
    std::vector<uint8_t> bytes;
};

// The edits made against a scrubbed frame, replayed on top of the restored state by Play From Here.
// A batch carries ONE frame tag that covers every poke in it, so an edit made on another frame cannot
// join it: the earlier batch is dropped, never retagged, or Play From Here would replay frame 11's
// edit onto frame 12.
class StagedEdits
{
public:
    bool Empty() const { return mPokes.empty(); }
    const std::vector<StagedPoke>& Pokes() const { return mPokes; }
    bool BelongsTo(uint64_t frame) const { return !mPokes.empty() && mFrame == frame; }

    // Stage 'len' bytes at 'addr' on 'frame'. The hex editor writes one byte at a time, so a write
    // that continues the last poke extends it. A batch staged on another frame is dropped first.
    void Record(uint64_t frame, bool isSound, uint32_t addr, const uint8_t* bytes, size_t len)
    {
        if (mFrame != frame) Clear();
        mFrame = frame;
        for (size_t i = 0; i < len; ++i)
        {
            const uint32_t a = addr + static_cast<uint32_t>(i);
            if (!mPokes.empty())
            {
                StagedPoke& last = mPokes.back();
                if (last.isSound == isSound && a == last.addr + last.bytes.size())
                {
                    last.bytes.push_back(bytes[i]);
                    continue;
                }
            }
            StagedPoke p;
            p.isSound = isSound;
            p.addr = a;
            p.bytes.push_back(bytes[i]);
            mPokes.push_back(std::move(p));
        }
    }

    void Clear()
    {
        mPokes.clear();
        mFrame = 0;
    }

private:
    std::vector<StagedPoke> mPokes;
    uint64_t                mFrame = 0;
};

}  // namespace sfe
