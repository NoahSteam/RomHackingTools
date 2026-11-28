// ScrubState -- the two rules about frame identity that scrubbing the rewind history depends on, kept
// apart from App so they can be unit-tested against a ring that really evicts.
//
// The recorder's ring evicts from the front, so an INDEX names a different frame as frames are
// published. Everything that has to stay with a frame (the view, a navigation's target, edits staged
// against it) is therefore keyed by frame number:
//   * PlanScrub decides what the next refresh shows.
//   * Edits made on a scrubbed frame are per-frame too: see FrameEdits.h (FrameEditStore).
#pragma once

#include <cstddef>
#include <cstdint>

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
// to the frame already shown is a Keep. A rebuild lays the frame's stored edits back over the
// recording (FrameRecorder::OverlaySelected), so a reload shows them too. 'forceReload' says the
// displayed edits were dropped from the store (their frame left the recording, or it was cleared)
// while the context still shows them: the frame is then rebuilt without them.
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

}  // namespace sfe
