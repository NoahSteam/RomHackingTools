// The Assembly panel's scroll rule, driven headlessly.
//
// The panel is a virtual list over a whole memory region (hundreds of thousands of rows), and
// ImGui scrolls one row into view with SetScrollHereY -- which needs that row actually submitted.
// A row far from the viewport is clipped away, so a pending scroll request forces its row into
// the ListClipper's range (IncludeItemByIndex). The rule under test: scroll the row whose
// address matches the request, at the requested alignment, from anywhere in the span; a request
// naming an address outside the span is dropped; one raised DURING rendering belongs to the span
// the next frame lists and must survive the end-of-frame drop.
//
// Tested here as the mechanism rather than through AssemblyPanel, which is how the other
// headless panel tests in this directory work: real ImGui scrolling and clipping, synthetic rows.
#include "ImGuiHarness.h"

#include <cstdio>
#include <cstdint>

using namespace sfe;

namespace
{
int gFailures;

#define CHECK(cond) do {                                                      \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } \
} while (0)

constexpr uint32_t kBase  = 0x06000000u;   // the span's first instruction (HWRAM)
constexpr int      kRows  = 0x80000;       // HWRAM: 1 MiB of 2-byte instructions
constexpr uint32_t kStep  = 2;             // SH-2 instructions are 2 bytes

// The panel's state, reduced to what the scroll rule reads and writes.
struct View
{
    uint32_t base = kBase;     // the span being listed; moves when a navigation lands elsewhere
    bool     pending = false;
    uint32_t addr = 0;
    float    align = 0.0f;
    unsigned seq = 0;          // mScrollSeq

    // A navigation raised from INSIDE the row loop, the way clicking a branch operand or
    // Follow Branch does. The span it targets is only listed on the following frame.
    bool     navOnRow = false;
    int      navAtRow = 0;
    uint32_t navTarget = 0;
    uint32_t navNewBase = 0;
    bool     navPendingRebuild = false;

    // Recorded while drawing, for the assertions.
    bool     targetDrawn = false;
    float    scrollY = 0.0f;
    float    rowH = 0.0f;
    int      rowsDrawn = 0;
};

// Navigate()'s request, with the sequence bump that marks it as raised this frame.
void Request(View& v, uint32_t addr, float align)
{
    v.pending = true;
    v.addr = addr;
    v.align = align;
    ++v.seq;
}

// Where a row sits in the table's scrollable content. Rows here are uniform, so this is exact
// and lets the assertions work in scroll space rather than in screen coordinates -- which move
// with the scroll they are trying to measure.
float ContentOffsetOf(int rowIndex, float rowH) { return (float)rowIndex * rowH; }

// One frame of the row list, applying the panel's rule verbatim.
void DrawRows(View& v)
{
    if (v.navPendingRebuild)
    {
        v.base = v.navNewBase;
        v.navPendingRebuild = false;
    }
    v.rowsDrawn = 0;
    ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
    ImGui::Begin("asm");
    if (ImGui::BeginTable("rows", 1, ImGuiTableFlags_ScrollY))
    {
        const unsigned seqAtRowStart = v.seq;
        const uint32_t base = v.base;   // a local: the loop must stay in the span being drawn

        ImGuiListClipper clipper;
        clipper.Begin(kRows);
        if (v.pending && v.addr >= base && v.addr - base < (uint32_t)kRows * kStep)
            clipper.IncludeItemByIndex((int)((v.addr - base) / kStep));
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const uint32_t addr = base + (uint32_t)i * kStep;
                ++v.rowsDrawn;
                if (v.navOnRow && i == v.navAtRow)
                {
                    v.navOnRow = false;
                    v.navPendingRebuild = true;
                    Request(v, v.navTarget, 0.0f);
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i);
                ImGui::Text("%08X", addr);
                v.rowH = ImGui::GetItemRectSize().y + ImGui::GetStyle().CellPadding.y * 2.0f;
                if (v.pending && v.seq == seqAtRowStart && addr == v.addr)
                {
                    v.targetDrawn = true;
                    ImGui::SetScrollHereY(v.align);
                    v.pending = false;
                }
                ImGui::PopID();
            }
        }
        v.scrollY = ImGui::GetScrollY();
        // A request no row matched is dropped -- but only if it was already pending when the
        // rows went in. One raised during the loop targets a span not listed yet.
        if (v.pending && v.seq == seqAtRowStart) v.pending = false;
        ImGui::EndTable();
    }
    ImGui::End();
}

// Only the rows on screen are submitted -- the point of the virtual list. Without it a region
// is half a million table rows a frame.
void TestOnlyVisibleRowsAreSubmitted()
{
    View v;
    ImGuiHarness h([&] { DrawRows(v); });
    h.Settle();
    CHECK(v.rowsDrawn > 0);
    CHECK(v.rowsDrawn < 100);
}

// Jumping far down the region -- the whole reason for the list -- puts the target on the first
// visible line even though its row was clipped away until the request forced it in.
void TestFarTargetScrollsToTop()
{
    View v;
    ImGuiHarness h([&] { DrawRows(v); });
    h.Settle();
    CHECK(v.scrollY == 0.0f);

    const int row = 300000;
    Request(v, kBase + (uint32_t)row * kStep, 0.0f);
    h.Settle();

    CHECK(v.targetDrawn);
    CHECK(!v.pending);
    const float want = ContentOffsetOf(row, v.rowH);
    CHECK(v.scrollY > want - 2.0f * v.rowH);
    CHECK(v.scrollY < want + 2.0f * v.rowH);
    CHECK(v.rowsDrawn < 100);   // and it settled back to just the screenful, not the whole way
}

// The two ends of the span are reachable: the first instruction, and the last one (where the
// scroll clamps, so the target lands below the top of the view).
void TestBothEndsOfTheSpanAreReachable()
{
    View v;
    ImGuiHarness h([&] { DrawRows(v); });
    h.Settle();

    Request(v, kBase + (uint32_t)(kRows - 1) * kStep, 0.0f);
    h.Settle();
    CHECK(v.targetDrawn);
    CHECK(v.scrollY > ContentOffsetOf(kRows - 40, v.rowH));   // at the very bottom

    v.targetDrawn = false;
    Request(v, kBase, 0.0f);
    h.Settle();
    CHECK(v.targetDrawn);
    CHECK(v.scrollY < v.rowH);                                // back at the very top
}

// Follow-PC keeps the old framing: the PC a third down, so the instructions about to run are
// on screen below it. Same mechanism, different alignment.
void TestMidAlignmentLeavesRoomBelow()
{
    View v;
    ImGuiHarness h([&] { DrawRows(v); });
    h.Settle();

    const int row = 120000;
    Request(v, kBase + (uint32_t)row * kStep, 0.35f);
    h.Settle();

    CHECK(v.targetDrawn);
    CHECK(v.scrollY > 0.0f);
    // Short of the row's own offset, which is what leaves it sitting below the top of the view
    // with the following instructions visible underneath.
    CHECK(v.scrollY < ContentOffsetOf(row, v.rowH) - v.rowH);
}

// An address outside the span matches no row. The request must not survive the frame, or it
// would fire later against an unrelated view once that address happened to be listed.
void TestRequestOutsideTheSpanIsDropped()
{
    View v;
    ImGuiHarness h([&] { DrawRows(v); });
    h.Settle();

    Request(v, kBase - 0x1000u, 0.0f);
    h.Settle();

    CHECK(!v.targetDrawn);
    CHECK(!v.pending);           // dropped, not left armed
    CHECK(v.scrollY == 0.0f);    // and the view did not move
}

// Clicking a branch operand, or Follow Branch, calls Navigate() while the rows are being
// submitted. A target in another region is only listed on the following frame, so no row
// consumes the request in this one, and dropping every unconsumed request at the end of the
// frame threw it away: the new span then drew with no framing at all.
void TestRequestRaisedDuringRenderingSurvives()
{
    View v;
    ImGuiHarness h([&] { DrawRows(v); });
    h.Settle();
    CHECK(v.scrollY == 0.0f);

    // Raised at row 3 (the window shows only a handful of rows), targeting another span.
    const uint32_t newBase = 0x00200000u;                     // LWRAM
    v.navOnRow = true;
    v.navAtRow = 3;
    v.navNewBase = newBase;
    v.navTarget = newBase + 50000u * kStep;
    h.Frame(ImVec2(1270.0f, 710.0f), false);                  // exactly one frame

    CHECK(v.pending);         // survived the end-of-frame drop -- the regression
    CHECK(!v.targetDrawn);    // and could not have been consumed: not in that span

    // The next frames list the new span, which now honours the request.
    h.Settle();
    CHECK(v.targetDrawn);
    CHECK(!v.pending);
    const float want = ContentOffsetOf(50000, v.rowH);
    CHECK(v.scrollY > want - 2.0f * v.rowH);
    CHECK(v.scrollY < want + 2.0f * v.rowH);
}

// A navigation inside the span being drawn (a forward branch in the same region): the view is
// the same one, so the row carrying the target address can honour the request straight away --
// there is no second window to be mistaken for.
void TestNavigationWithinTheSpanLandsOnTheTarget()
{
    View v;
    ImGuiHarness h([&] { DrawRows(v); });
    h.Settle();

    const int targetRow = 150;
    v.navOnRow = true;
    v.navAtRow = 3;                                           // the branch instruction clicked
    v.navNewBase = kBase;                                     // same span
    v.navTarget = kBase + (uint32_t)targetRow * kStep;
    h.Frame(ImVec2(1270.0f, 710.0f), false);
    h.Settle();

    CHECK(!v.pending);
    CHECK(v.targetDrawn);
    const float want = ContentOffsetOf(targetRow, v.rowH);
    CHECK(v.scrollY > want - 2.0f * v.rowH);
    CHECK(v.scrollY < want + 2.0f * v.rowH);
}
}  // namespace

int main()
{
    TestOnlyVisibleRowsAreSubmitted();
    TestFarTargetScrollsToTop();
    TestBothEndsOfTheSpanAreReachable();
    TestMidAlignmentLeavesRoomBelow();
    TestRequestOutsideTheSpanIsDropped();
    TestRequestRaisedDuringRenderingSurvives();
    TestNavigationWithinTheSpanLandsOnTheTarget();
    if (gFailures) { std::printf("FAILURES: %d\n", gFailures); return 1; }
    std::printf("all cases passed\n");
    return 0;
}
