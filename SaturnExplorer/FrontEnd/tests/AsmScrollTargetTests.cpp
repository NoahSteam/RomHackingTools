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

// The panel's state, reduced to what the scroll rule reads and writes.
struct View
{
    bool     pending = false;
    uint32_t addr = 0;
    float    align = 0.0f;

    // Recorded while drawing, for the assertions.
    bool     targetDrawn = false;
    float    scrollY = 0.0f;
    float    rowH = 0.0f;
};

// Where a row sits in the table's scrollable content. Rows here are uniform, so this is exact
// and lets the assertions work in scroll space rather than in screen coordinates -- which move
// with the scroll they are trying to measure.
float ContentOffsetOf(int rowIndex, float rowH) { return (float)rowIndex * rowH; }

// One frame of the row list, applying the panel's rule verbatim.
void DrawRows(View& v)
{
    ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
    ImGui::Begin("asm");
    if (ImGui::BeginTable("rows", 1, ImGuiTableFlags_ScrollY))
    {
        for (int i = 0; i < kRows; ++i)
        {
            const uint32_t addr = kBase + (uint32_t)i * kStep;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            ImGui::Text("%08X", addr);
            if (i == 1) v.rowH = ImGui::GetItemRectSize().y + ImGui::GetStyle().CellPadding.y * 2.0f;
            if (v.pending && addr == v.addr)
            {
                v.targetDrawn = true;
                ImGui::SetScrollHereY(v.align);
                v.pending = false;
            }
            ImGui::PopID();
        }
        v.scrollY = ImGui::GetScrollY();
        // A request no row matched can never be consumed, so it is dropped here.
        v.pending = false;
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
    v.pending = true; v.addr = target; v.align = 0.0f;
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
    v.pending = true; v.addr = target; v.align = 0.35f;
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

    v.pending = true; v.addr = kBase - 0x1000u; v.align = 0.0f;
    h.Settle();

    CHECK(!v.targetDrawn);
    CHECK(!v.pending);           // dropped, not left armed
    CHECK(v.scrollY == 0.0f);    // and the view did not move
}
}  // namespace

int main()
{
    TestNonPcTargetScrollsToTop();
    TestMidAlignmentLeavesRoomBelow();
    TestRequestOutsideTheWindowIsDropped();
    if (gFailures) { std::printf("FAILURES: %d\n", gFailures); return 1; }
    std::printf("all cases passed\n");
    return 0;
}
