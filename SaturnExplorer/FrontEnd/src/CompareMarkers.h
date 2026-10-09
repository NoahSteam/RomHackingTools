// CompareMarkers -- the two Frame Memory Compare markers (A and B) and the identity rules that keep
// them honest. Pure logic with no ImGui, no App and no recorder, so the rules are unit-tested.
// Design: Docs/MemoryCompare/TimelineMarkers.md.
//
// A marker owns an immutable snapshot, so the rewind ring evicting its frame cannot change what it
// compares. What can go wrong instead is a marker that outlives the thing it was taken from, so two
// counters say what "the same" means:
//   * the SESSION: one emulator run or loaded source. A new session drops both markers; a snapshot
//     of a different run is not comparable at all.
//   * the TIMELINE EPOCH: one rewind history within a session. When history is replaced (a load
//     state jumps, Play From Here discards the future) the snapshot is still real memory of this run
//     and still compares, but its frame number may be reused by the new timeline with different
//     content, so a marker of an older epoch must never be located by number. It is "detached".
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "Debug/MemoryCompare.h"

namespace sfe
{

// Index of the entry whose frame number is exactly 'frameNo', or -1. 'frameAt(i)' is the frame
// number of entry i; entries ascend (the rewind ring is strictly monotonic). Exact, never "nearest":
// the nearest frame is a different frame.
template <class F>
int FindFrameIndex(size_t count, F frameAt, uint64_t frameNo)
{
    size_t lo = 0, hi = count;
    while (lo < hi)
    {
        const size_t mid = lo + (hi - lo) / 2;
        const uint64_t f = frameAt(mid);
        if (f == frameNo) return static_cast<int>(mid);
        if (f < frameNo) lo = mid + 1; else hi = mid;
    }
    return -1;
}

// Where ImGui draws the centre of the grab of an integer SliderInt over [0, count-1], so a marker can
// sit exactly where the slider puts that frame. 'minX'/'maxX' are the slider's frame rect (an item
// with an empty label has the same rect as its frame). This mirrors ImGui's SliderBehaviorT (the 2.0
// grab padding, a grab one unit wide, GrabMinSize), which is not exposed; CompareMarkersTests
// (TestGeometry) compares it with ImGui's own result so an ImGui update that moves the grab fails.
inline float SliderGrabCenterX(float minX, float maxX, float grabMinSize, int count, int index)
{
    const float pad = 2.0f;
    const float sliderSz = (maxX - minX) - pad * 2.0f;
    float grab = count > 1 ? sliderSz / static_cast<float>(count) : sliderSz;   // v_range + 1 == count
    if (grab < grabMinSize) grab = grabMinSize;
    if (grab > sliderSz) grab = sliderSz;
    const float lo = minX + pad + grab * 0.5f;
    const float hi = maxX - pad - grab * 0.5f;
    const float t = count > 1 ? static_cast<float>(index) / static_cast<float>(count - 1) : 0.0f;
    return lo + (hi - lo) * t;
}

class CompareMarkers
{
public:
    enum Slot { A = 0, B = 1 };

    uint64_t Session() const { return mSession; }

    bool Has(Slot s) const { return mMarker[s].snap != nullptr; }
    bool HasBoth() const { return Has(A) && Has(B); }
    bool Any() const { return Has(A) || Has(B); }
    const std::shared_ptr<const MemSnapshot>& Snapshot(Slot s) const { return mMarker[s].snap; }

    // The stamp a capture should carry so Set() can accept it: the current session and epoch.
    SnapshotOrigin Origin(uint64_t frameNo, bool liveHead) const
    {
        SnapshotOrigin o;
        o.sessionId = mSession;
        o.timelineEpoch = mEpoch;
        o.frameNo = frameNo;
        o.liveHead = liveHead;
        return o;
    }

    // Adopt 'snap' as marker 's'. Refused (false) when it was not captured in this session and epoch:
    // a snapshot taken before a reset must not become a marker after it.
    bool Set(Slot s, std::shared_ptr<const MemSnapshot> snap)
    {
        if (!snap || snap->origin.sessionId != mSession || snap->origin.timelineEpoch != mEpoch) return false;
        mMarker[s] = { std::move(snap), true };
        return true;
    }

    void Clear() { mMarker[A] = Marker(); mMarker[B] = Marker(); }

    // A different emulator run or loaded source: nothing taken from the old one can be compared, or
    // kept, so both markers go.
    void NewSession() { ++mSession; ReplaceTimeline(); Clear(); }

    // The recorded history was replaced wholesale (a load-state jump, a fresh recording). Snapshots
    // stay comparable but none can be found on the new timeline.
    void ReplaceTimeline()
    {
        ++mEpoch;
        for (Marker& m : mMarker) m.attached = false;
    }

    // Play From Here resumed at 'keptFrame': frames after it are discarded and the numbers will be
    // reused. A marker at or before it still names the same memory, so it stays as attached as it
    // was (one an earlier reset detached stays detached); one after it describes a future that no
    // longer exists and is detached.
    void TruncateAfter(uint64_t keptFrame)
    {
        ++mEpoch;
        for (Marker& m : mMarker)
            m.attached = m.attached && m.snap && m.snap->origin.frameNo <= keptFrame;
    }

    // Index of marker 's' on the timeline, or -1 when it is detached or its frame has left the ring.
    // Never a nearest match.
    template <class F>
    int IndexOf(Slot s, size_t count, F frameAt) const
    {
        const Marker& m = mMarker[s];
        if (!m.snap || !m.attached) return -1;
        return FindFrameIndex(count, frameAt, m.snap->origin.frameNo);
    }

private:
    struct Marker
    {
        std::shared_ptr<const MemSnapshot> snap;
        bool attached = false;   // still on the current timeline, so its frame number means this frame
    };
    Marker   mMarker[2];
    uint64_t mSession = 1;
    uint64_t mEpoch = 1;
};

}  // namespace sfe
