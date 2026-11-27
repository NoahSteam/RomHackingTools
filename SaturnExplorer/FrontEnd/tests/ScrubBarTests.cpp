// Unit tests for ScrubBar: which entry the scrub slider's label and handle show across the
// transitions that left them on a stale index (Play while scrubbed, Play From Here, a ring that
// evicts or is truncated). A small model mirrors App's use of the helpers on a ring that evicts from
// the front like FrameRecorder's.
#include "ScrubBar.h"

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

struct Model
{
    std::deque<uint64_t> ring;
    size_t cap;
    bool scrubbing = false;
    int sel = -1;
    explicit Model(size_t c, uint64_t first, uint64_t last) : cap(c)
    {
        for (uint64_t f = first; f <= last; ++f) Publish(f);
    }
    void Publish(uint64_t f)
    {
        ring.push_back(f);
        while (ring.size() > cap) ring.pop_front();
    }
    int N() const { return static_cast<int>(ring.size()); }
    // What the slider draws: the index, and the frame its label names.
    int Shown() const { return ScrubBarIndex(scrubbing, sel, N()); }
    uint64_t ShownFrame() const { return ring[static_cast<size_t>(Shown())]; }
    std::string Label() const { return ScrubBarLabel(ShownFrame(), Shown(), N()); }
    void Drag(int i) { scrubbing = true; sel = i; }
    void Play() { ScrubBarLeave(scrubbing, sel); }
    void PlayFromHere()
    {
        const int target = scrubbing ? sel : N() - 1;
        ring.erase(ring.begin() + target + 1, ring.end());   // TruncateAfter
        ScrubBarLeave(scrubbing, sel);
    }
};
}  // namespace

int main()
{
    // Play while scrubbed, record on, pause: the bar is on the newest entry, not the old selection.
    {
        Model m(1000, 1, 900);
        m.Drag(774);
        Check(m.Shown() == 774, "scrubbed shows the selected entry");
        m.Play();
        for (uint64_t f = 901; f <= 1100; ++f) m.Publish(f);
        Check(m.Shown() == m.N() - 1, "after Play + record, paused bar rests on the newest entry");
        Check(m.ShownFrame() == 1100, "...and its label names the newest frame");
        Check(m.Label() == "#1100   1000 / 1000", "label text at capacity");
    }
    // Play From Here: the ring is truncated, then regrows; the label must follow the newest entry.
    {
        Model m(2000, 14000, 14900);
        m.Drag(133);   // frame 14133
        m.PlayFromHere();
        Check(m.N() == 134 && m.ShownFrame() == 14133, "truncated: newest is the Play From Here frame");
        for (uint64_t f = 14134; f <= 14327; ++f) m.Publish(f);
        Check(m.Shown() == m.N() - 1 && m.ShownFrame() == 14327, "regrown ring: bar on newest, not frame 14133");
        Check(m.Label() == "#14327   328 / 328", "regrown label");
    }
    // Same, but the selection sat past what the truncated ring regrew to (handle in the middle).
    {
        Model m(1800, 1, 1800);
        m.Drag(822);
        m.Play();
        for (uint64_t f = 1801; f <= 1900; ++f) m.Publish(f);   // evicts 100
        Check(m.Shown() == 1799 && m.ShownFrame() == 1900, "mid-bar stale index not shown after eviction");
    }
    // Scrubbing with an index the ring has outgrown stays valid.
    {
        Model m(10, 1, 10);
        m.Drag(9);
        Check(m.ShownFrame() == 10, "selected entry shown");
        m.sel = 40;   // out of range (e.g. truncated beneath it)
        Check(m.Shown() == m.N() - 1, "out-of-range selection falls back to newest");
        m.sel = -1;
        Check(m.Shown() == m.N() - 1, "unset selection while scrubbing falls back to newest");
    }
    // Empty ring.
    Check(ScrubBarIndex(false, 3, 0) == -1 && ScrubBarIndex(true, 3, 0) == -1, "empty ring has no index");
    // Step back from a resting bar goes one before the newest, not before an old selection.
    {
        Check(ScrubBarStepBackIndex(false, 5, 100) == 98, "step back from resting bar");
        Check(ScrubBarStepBackIndex(true, 5, 100) == 4, "step back while scrubbing");
        Check(ScrubBarStepBackIndex(true, 0, 100) == 0, "step back stops at the oldest");
        Check(ScrubBarStepBackIndex(false, 0, 0) == -1, "step back on empty");
    }
    // Leave clears both.
    {
        bool s = true; int i = 7;
        ScrubBarLeave(s, i);
        Check(!s && i == -1, "leave clears scrubbing and selection");
    }
    if (gFail == 0) std::printf("ScrubBarTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
