// Unit tests for CompareMarkers: the session / timeline-epoch rules, exact frame lookup, and the
// slider geometry a marker is drawn with. The geometry is also checked against ImGui itself, so an
// ImGui update that moves the grab shows up here instead of as markers drifting off their frames.
#include "CompareMarkers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuiHarness.h"

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

std::shared_ptr<const MemSnapshot> Snap(const CompareMarkers& m, uint64_t frame)
{
    auto s = std::make_shared<MemSnapshot>();
    s->origin = m.Origin(frame, false);
    return s;
}

// Index of marker 's' in a ring holding exactly these frame numbers.
int Find(const CompareMarkers& m, CompareMarkers::Slot s, const std::vector<uint64_t>& frames)
{
    return m.IndexOf(s, frames.size(), [&frames](size_t i) { return frames[i]; });
}

void TestFindFrameIndex()
{
    const std::vector<uint64_t> f = { 10, 11, 13, 20, 21 };   // sparse: the recorder drops frames
    auto at = [&](size_t i) { return f[i]; };
    Check(FindFrameIndex(f.size(), at, 10) == 0, "first");
    Check(FindFrameIndex(f.size(), at, 21) == 4, "last");
    Check(FindFrameIndex(f.size(), at, 13) == 2, "middle");
    Check(FindFrameIndex(f.size(), at, 12) == -1, "a missing frame is not matched to its neighbour");
    Check(FindFrameIndex(f.size(), at, 9) == -1 && FindFrameIndex(f.size(), at, 22) == -1, "outside the ring");
    Check(FindFrameIndex(0, at, 10) == -1, "empty ring");
    Check(FindFrameIndex(1, at, 10) == 0, "single frame");
}

void TestLineFlow()
{
    {
        LineFlow f(400.0f, 8.0f);
        Check(!f.BreaksBefore(100.0f) && !f.BreaksBefore(100.0f) && !f.BreaksBefore(150.0f) && f.Lines() == 1,
              "100 + 8 + 100 + 8 + 150 = 366 fits on a 400 line");
    }
    {
        LineFlow f(320.0f, 8.0f);
        f.BreaksBefore(100.0f);
        f.BreaksBefore(100.0f);
        Check(f.BreaksBefore(150.0f) && f.Lines() == 2, "the third item (366 > 320) goes to a second line");
    }
    {
        LineFlow f(120.0f, 8.0f);
        Check(!f.BreaksBefore(100.0f) && f.BreaksBefore(100.0f) && f.BreaksBefore(100.0f) && f.Lines() == 3,
              "when nothing fits beside another, each item has its own line");
    }
    {
        LineFlow f(50.0f, 8.0f);
        Check(!f.BreaksBefore(200.0f) && f.Lines() == 1, "an item wider than the line does not make an empty line before it");
        Check(f.BreaksBefore(10.0f) && f.Lines() == 2, "but nothing shares its line");
    }
    {
        LineFlow f(100.0f, 10.0f);
        Check(!f.BreaksBefore(45.0f) && !f.BreaksBefore(45.0f) && f.Lines() == 1, "45 + 10 + 45 = 100 fits exactly");
        Check(f.BreaksBefore(1.0f), "and one more pixel does not");
    }

    // The model against ImGui's own layout: place real items by LineFlow and check where they land.
    float width = 600.0f;
    int lines = 0;
    float maxRight = 0.0f, windowRight = 0.0f;
    int distinctLines = 0;
    ImGuiHarness h([&]
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, 300), ImGuiCond_Always);
        ImGui::Begin("flow", nullptr, ImGuiWindowFlags_NoSavedSettings);
        const char* labels[3] = { "Frame A: 1800 (detached)", "Frame B: 1884 (detached)", "Compare Memory..." };
        float w[3];
        for (int i = 0; i < 3; ++i)
            w[i] = ImGui::CalcTextSize(labels[i]).x + (i == 2 ? ImGui::GetStyle().FramePadding.x * 2.0f : 0.0f);
        LineFlow flow(ImGui::GetContentRegionAvail().x, ImGui::GetStyle().ItemSpacing.x);
        float lastY = -1.0f;
        maxRight = 0.0f;
        distinctLines = 0;
        for (int i = 0; i < 3; ++i)
        {
            if (!flow.BreaksBefore(w[i]) && i > 0) ImGui::SameLine();
            if (i == 2) ImGui::Button(labels[i]); else ImGui::TextUnformatted(labels[i]);
            maxRight = std::max(maxRight, ImGui::GetItemRectMax().x);
            if (ImGui::GetItemRectMin().y != lastY) { ++distinctLines; lastY = ImGui::GetItemRectMin().y; }
        }
        lines = flow.Lines();
        windowRight = ImGui::GetCurrentWindow()->WorkRect.Max.x;
        ImGui::End();
    });
    for (float ww : { 900.0f, 600.0f, 420.0f, 320.0f, 260.0f })
    {
        width = ww;
        h.Frame(ImVec2(1270.0f, 710.0f), false);
        h.Frame(ImVec2(1270.0f, 710.0f), false);
        char msg[96];
        std::snprintf(msg, sizeof(msg), "the row's items stay inside a %.0f px window", ww);
        Check(maxRight <= windowRight + 0.5f, msg);
        std::snprintf(msg, sizeof(msg), "LineFlow predicts the %d line(s) ImGui lays out at %.0f px", lines, ww);
        Check(lines == distinctLines, msg);
    }
}

void TestSetAndClear()
{
    CompareMarkers m;
    Check(!m.Any() && !m.HasBoth(), "starts empty");
    Check(m.Set(CompareMarkers::A, Snap(m, 100)), "a current snapshot is accepted");
    Check(m.Has(CompareMarkers::A) && !m.Has(CompareMarkers::B) && !m.HasBoth(), "A set, B not");
    Check(m.Set(CompareMarkers::B, Snap(m, 200)) && m.HasBoth(), "both set");
    Check(!m.Set(CompareMarkers::A, nullptr), "null is refused");

    auto stale = Snap(m, 300);
    m.ReplaceTimeline();
    Check(!m.Set(CompareMarkers::A, stale), "a snapshot from before a timeline reset is refused");
    auto stale2 = Snap(m, 300);
    m.NewSession();
    Check(!m.Set(CompareMarkers::A, stale2), "a snapshot from before a new session is refused");
    m.Set(CompareMarkers::A, Snap(m, 1));
    m.Clear();
    Check(!m.Any(), "Clear drops both");
}

void TestNewSession()
{
    CompareMarkers m;
    const uint64_t s = m.Session();
    m.Set(CompareMarkers::A, Snap(m, 100));
    m.Set(CompareMarkers::B, Snap(m, 200));
    m.NewSession();
    Check(m.Session() != s, "the session changes");
    Check(!m.Any(), "and both markers are dropped");
}

void TestReplaceTimeline()
{
    CompareMarkers m;
    const std::vector<uint64_t> ring = { 100, 101, 102 };
    m.Set(CompareMarkers::A, Snap(m, 101));
    Check(Find(m, CompareMarkers::A, ring) == 1, "attached while on its timeline");
    m.ReplaceTimeline();
    Check(m.Has(CompareMarkers::A), "the snapshot is kept");
    Check(Find(m, CompareMarkers::A, ring) == -1,
          "but is not found by number: the new timeline may reuse frame 101 with other content");
}

void TestTruncateAfter()
{
    CompareMarkers m;
    m.Set(CompareMarkers::A, Snap(m, 12));   // at the resume point
    m.Set(CompareMarkers::B, Snap(m, 14));   // after it
    m.TruncateAfter(12);
    Check(Find(m, CompareMarkers::A, { 10, 11, 12 }) == 2, "a marker at the resume frame stays attached");
    // The new timeline reaches 14 with other content: the old marker must not be found there.
    Check(Find(m, CompareMarkers::B, { 10, 11, 12, 13, 14, 15 }) == -1,
          "a marker past it is detached, even if its number comes back");
    Check(m.HasBoth(), "both snapshots are kept");

    CompareMarkers early;
    early.Set(CompareMarkers::A, Snap(early, 5));
    early.TruncateAfter(12);
    Check(Find(early, CompareMarkers::A, { 5 }) == 0, "a marker well before it stays attached");

    // Truncating again must not resurrect a marker an earlier reset detached, even one that is at
    // or before the new resume frame.
    CompareMarkers twice;
    twice.Set(CompareMarkers::B, Snap(twice, 14));
    twice.TruncateAfter(12);
    twice.TruncateAfter(20);
    Check(Find(twice, CompareMarkers::B, { 14 }) == -1, "a truncated-away marker stays detached");
    CompareMarkers replaced;
    replaced.Set(CompareMarkers::A, Snap(replaced, 5));
    replaced.ReplaceTimeline();
    replaced.TruncateAfter(12);
    Check(Find(replaced, CompareMarkers::A, { 5 }) == -1, "a marker detached by a reset stays detached");
}

void TestEvictedFrame()
{
    CompareMarkers m;
    m.Set(CompareMarkers::A, Snap(m, 100));
    Check(Find(m, CompareMarkers::A, { 101, 102 }) == -1, "an evicted frame is not found");   // 100 left the ring
    Check(m.Has(CompareMarkers::A), "its snapshot is kept");
}

void TestGeometry()
{
    // Endpoints and monotonic steps.
    const float lo = SliderGrabCenterX(0.0f, 400.0f, 12.0f, 100, 0);
    const float hi = SliderGrabCenterX(0.0f, 400.0f, 12.0f, 100, 99);
    Check(lo > 0.0f && hi < 400.0f && lo < hi, "ends sit inside the frame");
    Check(SliderGrabCenterX(0.0f, 400.0f, 12.0f, 1, 0) == 200.0f, "a single frame is centred");
    float prev = lo;
    bool mono = true;
    for (int i = 1; i < 100; ++i)
    {
        const float x = SliderGrabCenterX(0.0f, 400.0f, 12.0f, 100, i);
        mono = mono && x > prev;
        prev = x;
    }
    Check(mono, "x grows with the index");

    // Against ImGui itself: the grab ImGui's own slider code computes, at several lengths, counts
    // and positions. A drift in ImGui's geometry fails here rather than as markers off their frames.
    int value = 0, count = 2;
    float width = 600.0f, grabCenter = 0.0f, minX = 0.0f, maxX = 0.0f;
    ImGuiHarness h([&]
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(1000, 200), ImGuiCond_Always);
        ImGui::Begin("g", nullptr, ImGuiWindowFlags_NoSavedSettings);
        const ImRect bb(ImVec2(20.0f, 20.0f), ImVec2(20.0f + width, 40.0f));
        int v = value, vMin = 0, vMax = count - 1;
        ImRect grab;
        ImGui::SliderBehavior(bb, ImGui::GetID("s"), ImGuiDataType_S32, &v, &vMin, &vMax, "%d", 0, &grab);
        grabCenter = (grab.Min.x + grab.Max.x) * 0.5f;
        minX = bb.Min.x;
        maxX = bb.Max.x;
        ImGui::End();
    });
    for (float w : { 10.0f, 300.0f, 600.0f, 900.0f })
    {
        width = w;
        for (int n : { 1, 2, 5, 60, 300, 2000 })
        {
            count = n;
            for (int idx : { 0, n / 3, n - 1 })
            {
                value = idx;
                h.Frame(ImVec2(1270.0f, 710.0f), false);   // no input: one frame computes the grab
                const float mine = SliderGrabCenterX(minX, maxX, ImGui::GetStyle().GrabMinSize, n, idx);
                char msg[96];
                std::snprintf(msg, sizeof(msg), "matches ImGui's grab (width %.0f, count %d, index %d)", w, n, idx);
                Check(std::fabs(mine - grabCenter) < 0.01f, msg);
            }
        }
    }
}
}  // namespace

int main()
{
    TestFindFrameIndex();
    TestLineFlow();
    TestSetAndClear();
    TestNewSession();
    TestReplaceTimeline();
    TestTruncateAfter();
    TestEvictedFrame();
    TestGeometry();
    if (gFail == 0) std::printf("CompareMarkersTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
