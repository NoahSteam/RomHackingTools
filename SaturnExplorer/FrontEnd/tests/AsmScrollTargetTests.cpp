// The Assembly panel's scroll rule, driven headlessly.
//
// The panel decodes a fixed window of instructions and asks ImGui to scroll one row into view.
// It used to key that request off "is this row the PC", which meant a jump to any address that
// is NOT the PC -- a Call Stack frame's entry point, a branch target, the Back button -- set the
// request and then found no row to consume it, so the view never moved. The rule is now "scroll
// the row whose address matches the request", at a requested alignment, and a request naming an
// address outside the window is dropped rather than left to fire later.
//
// Tested here as the mechanism rather than through AssemblyPanel, which is how the other
// headless panel tests in this directory work: real ImGui scrolling, synthetic rows.
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

constexpr uint32_t kBase  = 0x06000000u;   // window's first decoded instruction
constexpr int      kRows  = 256;           // kWinInstr
constexpr uint32_t kStep  = 2;             // SH-2 instructions are 2 bytes

constexpr int kNavLead = 96;   // kWinLead: instructions decoded before the anchor

// The panel's state, reduced to what the scroll rule reads and writes.
struct View
{
    uint32_t base = kBase;     // first decoded address; moves when a navigation rebuilds
    bool     pending = false;
    uint32_t addr = 0;
    float    align = 0.0f;
    unsigned seq = 0;          // mScrollSeq

    // A navigation raised from INSIDE the row loop, the way clicking a branch operand or
    // Follow Branch does. The window it targets is only decoded on the following frame.
    bool     navOnRow = false;
    int      navAtRow = 0;
    uint32_t navTarget = 0;
    bool     navPendingRebuild = false;

    // Recorded while drawing, for the assertions.
    bool     targetDrawn = false;
    float    scrollY = 0.0f;
    float    rowH = 0.0f;
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
    // The panel recomputes its window at the top of the frame after a navigation, framing it a
    // lead before the target -- so the target is NOT the first row, and a scroll to it is
    // visible as a nonzero scroll position.
    if (v.navPendingRebuild)
    {
        v.base = v.navTarget - (uint32_t)kNavLead * kStep;
        v.navPendingRebuild = false;
    }
    ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
    ImGui::Begin("asm");
    if (ImGui::BeginTable("rows", 1, ImGuiTableFlags_ScrollY))
    {
        const unsigned seqAtRowStart = v.seq;
        for (int i = 0; i < kRows; ++i)
        {
            const uint32_t addr = v.base + (uint32_t)i * kStep;
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
            if (i == 1) v.rowH = ImGui::GetItemRectSize().y + ImGui::GetStyle().CellPadding.y * 2.0f;
            if (v.pending && v.seq == seqAtRowStart && addr == v.addr)
            {
                v.targetDrawn = true;
                ImGui::SetScrollHereY(v.align);
                v.pending = false;
            }
            ImGui::PopID();
        }
        v.scrollY = ImGui::GetScrollY();
        // A request no row matched is dropped -- but only if it was already pending when the
        // rows went in. One raised during the loop targets a window not decoded yet.
        if (v.pending && v.seq == seqAtRowStart) v.pending = false;
        ImGui::EndTable();
    }
    ImGui::End();
}

// A Call Stack double-click lands on a function's entry point, which is almost never the PC.
// Before the fix this produced no scroll at all; the behaviour asked for is that the target
// becomes the first visible line.
void TestNonPcTargetScrollsToTop()
{
    View v;
    ImGuiHarness h([&] { DrawRows(v); });
    h.Settle();
    CHECK(v.scrollY == 0.0f);   // starts at the top of the window

    const uint32_t target = kBase + 80u * kStep;   // well below the first screenful
    Request(v, target, 0.0f);
    h.Settle();

    CHECK(v.targetDrawn);
    CHECK(v.scrollY > 0.0f);    // it moved -- the bug was that it did not
    // At the TOP of the view, not merely somewhere in it: align 0 scrolls to the row's own
    // offset in the content, so the two agree to within a row.
    const float want = ContentOffsetOf(80, v.rowH);
    CHECK(v.scrollY > want - v.rowH);
    CHECK(v.scrollY < want + v.rowH);
}

// Follow-PC keeps the old framing: the PC a third down, so the instructions about to run are
// on screen below it. Same mechanism, different alignment.
void TestMidAlignmentLeavesRoomBelow()
{
    View v;
    ImGuiHarness h([&] { DrawRows(v); });
    h.Settle();

    const uint32_t target = kBase + 120u * kStep;
    Request(v, target, 0.35f);
    h.Settle();

    CHECK(v.targetDrawn);
    CHECK(v.scrollY > 0.0f);
    // Short of the row's own offset, which is what leaves it sitting below the top of the view
    // with the following instructions visible underneath.
    CHECK(v.scrollY < ContentOffsetOf(120, v.rowH) - v.rowH);
}

// An address outside the decoded window matches no row. The request must not survive the frame,
// or it would fire later against an unrelated view once that address happened to be decoded.
void TestRequestOutsideTheWindowIsDropped()
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
// submitted -- against a window decoded before the jump. Its target is normally not in those
// rows, so no row consumes the request, and dropping every unconsumed request at the end of the
// frame threw it away: the new window then drew with no framing at all. The request has to
// survive into the frame that decodes the window it names.
void TestRequestRaisedDuringRenderingSurvives()
{
    View v;
    ImGuiHarness h([&] { DrawRows(v); });
    h.Settle();
    CHECK(v.scrollY == 0.0f);

    // Raised at row 10, targeting an address well outside the window on screen.
    v.navOnRow = true;
    v.navAtRow = 10;
    v.navTarget = kBase + 0x4000u;
    h.Frame(ImVec2(1270.0f, 710.0f), false);   // exactly one frame

    CHECK(v.pending);         // survived the end-of-frame drop -- the regression
    CHECK(!v.targetDrawn);    // and could not have been consumed: not in that window

    // The next frames decode the window around the target, which now honours the request.
    h.Settle();
    CHECK(v.targetDrawn);
    CHECK(!v.pending);
    const float want = ContentOffsetOf(kNavLead, v.rowH);
    CHECK(v.scrollY > want - v.rowH);
    CHECK(v.scrollY < want + v.rowH);
}
// The case the sequence guard on cleanup alone did not cover: a FORWARD branch whose target is
// inside the window already on screen. Matching on address alone, the old row carrying that
// address consumed the brand-new request and scrolled the window being replaced -- so the
// rebuilt window, where the target sits a lead down rather than wherever it happened to be,
// had no request left to frame it.
void TestForwardTargetInsideOldWindowIsNotConsumedEarly()
{
    View v;
    ImGuiHarness h([&] { DrawRows(v); });
    h.Settle();

    const int targetRow = 150;                               // inside the window on screen
    v.navOnRow = true;
    v.navAtRow = 10;                                         // the branch instruction clicked
    v.navTarget = kBase + (uint32_t)targetRow * kStep;
    h.Frame(ImVec2(1270.0f, 710.0f), false);                 // exactly one frame

    // Old row 150 must NOT have taken it, even though its address matches.
    CHECK(v.pending);

    h.Settle();
    CHECK(!v.pending);
    CHECK(v.targetDrawn);
    // Framed against the REBUILT window, where the target is kNavLead rows down -- not against
    // the old one, where it was row 150.
    const float want = ContentOffsetOf(kNavLead, v.rowH);
    CHECK(v.scrollY > want - v.rowH);
    CHECK(v.scrollY < want + v.rowH);
    // And demonstrably not the old framing, which these two bracket out.
    const float wrong = ContentOffsetOf(targetRow, v.rowH);
    CHECK(wrong > want + v.rowH);          // the two are far enough apart to tell apart
    CHECK(v.scrollY < wrong - v.rowH);
}
}  // namespace

int main()
{
    TestNonPcTargetScrollsToTop();
    TestMidAlignmentLeavesRoomBelow();
    TestRequestOutsideTheWindowIsDropped();
    TestRequestRaisedDuringRenderingSurvives();
    TestForwardTargetInsideOldWindowIsNotConsumedEarly();
    if (gFailures) { std::printf("FAILURES: %d\n", gFailures); return 1; }
    std::printf("all cases passed\n");
    return 0;
}
