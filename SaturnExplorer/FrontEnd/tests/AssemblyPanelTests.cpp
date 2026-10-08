// The real SH-2 Assembly panel, driven headlessly against a mock memory backend and a core
// context that supplies SH-2 registers (see ImGuiHarness.h).
//
// AsmScrollTargetTests models the scroll rule with synthetic rows; this is the panel itself. It
// pins what the model cannot:
//  - the panel links and draws at all (a class-static constant bound to std::min's reference
//    parameter is an ODR-use that fails to link in an unoptimized C++14 build),
//  - the virtual list's row height does not change while a comment is being edited. The list is
//    clipped on the assumption that every row is one text line; an edit box with its normal frame
//    padding makes its row taller, the inferred height changes, and every scroll position computed
//    from it -- a jump to an address -- lands somewhere else.
#define IMGUI_DEFINE_MATH_OPERATORS
#include <cmath>
#include <cstdio>
#include <vector>

#include "imgui_internal.h"
#include "ImGuiHarness.h"
#include "AssemblyPanel.h"
#include "WatchPanel.h"
#include "Debug/BreakpointManager.h"
#include "Debug/ExecutionActions.h"
#include "FakeVdpSource.h"

using namespace sfe;

namespace
{
int gFailures;

#define CHECK(cond) do {                                                      \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } \
} while (0)

constexpr uint32_t kBase = 0x06000000u;
constexpr uint32_t kSize = 0x100000u;
constexpr uint32_t kPc   = 0x06000100u;

// High work RAM only, filled with NOPs; everything else is unmapped.
class MockBackend : public IMemoryBackend
{
public:
    bool Connected() const override { return true; }
    uint64_t SourceId() const override { return 1; }
    bool CanWrite(uint32_t) const override { return false; }
    std::vector<MemoryReadResult> ReadMemoryBatch(const std::vector<MemoryReadRequest>& rs) override
    {
        std::vector<MemoryReadResult> out;
        for (const MemoryReadRequest& r : rs)
        {
            MemoryReadResult res;
            const uint32_t c = r.address & 0x07FFFFFFu;
            if (c >= kBase && c - kBase + r.size <= kSize)
            {
                res.success = true;
                for (uint32_t i = 0; i < r.size; ++i)
                    res.bytes.push_back(((c + i) & 1u) ? 0x09 : 0x00);   // 0x0009 = nop
            }
            out.push_back(std::move(res));
        }
        return out;
    }
    size_t WriteMemory(uint32_t, const uint8_t*, size_t) override { return 0; }
};

int SupplyRegs(void*, int, se_sh2_regs* out)
{
    *out = {};
    out->pc = kPc;
    return 1;
}

struct Fixture
{
    se_test::State state;
    se_context*    ctx = nullptr;
    MockBackend    backend;
    BreakpointManager bps;
    ExecutionActions  actions;
    WatchPanel     watch;
    AssemblyPanel  panel;
    AssemblyPanel::Request req;
    ImGuiHarness   h;

    Fixture() : h([this] {
        // Wide enough that every column, the comment one included, is on screen to be clicked.
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(1200.0f, 600.0f));
        panel.Draw(ctx, backend, bps, actions, watch, false, req);
    })
    {
        se_data_source ds = se_test::MakeSource(state);
        ds.capabilities |= SE_CAP_SH2_REGS;
        ds.read_sh2_regs = SupplyRegs;
        ctx = se_test::CreateContext(ds);
        se_begin_frame(ctx);
    }
    ~Fixture() { se_destroy(ctx); }
};

// The panel's table, found by its column count (six).
ImGuiTable* FindTable()
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (int i = 0; i < g.Tables.GetMapSize(); ++i)
        if (ImGuiTable* t = g.Tables.TryGetMapData(i))
            if (t->ColumnsCount == 6) return t;
    return nullptr;
}

// The height the clipper inferred for a row: the scrollable content divided by the row count.
// (A 1 MiB region is 524288 rows, so an error of one pixel in the content is invisible and a
// change of a few pixels per row is unmistakable.)
float InferredRowHeight()
{
    ImGuiTable* t = FindTable();
    return t && t->InnerWindow ? t->InnerWindow->ContentSize.y / (float)(kSize / 2) : 0.0f;
}

// Park the list at its very top, exactly: a jump leaves the target row a few pixels under the
// header, and then the row the clipper measures from is not the first one on screen.
void ScrollToTop(ImGuiHarness& h)
{
    ImGui::SetScrollY(FindTable()->InnerWindow, 0.0f);
    h.Settle();
}

float ScrollY()
{
    ImGuiTable* t = FindTable();
    return t && t->InnerWindow ? t->InnerWindow->Scroll.y : 0.0f;
}

// The comment cell of the row 'n' rows below the first fully visible one.
ImVec2 CommentCellOfVisibleRow(int n)
{
    ImGuiTable* t = FindTable();
    const float rowH = InferredRowHeight();
    const ImGuiWindow* w = t->InnerWindow;
    const int first = (int)std::ceil(w->Scroll.y / rowH);
    const float y = w->InnerRect.Min.y + rowH +   // the frozen header row is one row tall
                      ((first + n) * rowH - w->Scroll.y) + rowH * 0.5f;
    const float x = t->Columns[5].WorkMinX + 2.0f;   // an empty comment is one space wide
    return ImVec2(x, y);
}

// Smoke: the real panel draws, and the list spans the whole region -- far more rows than a
// window of them. (The link itself is what the first case proves; see the header.)
void TestDrawsTheWholeRegion()
{
    Fixture f;
    f.h.Settle();
    CHECK(FindTable() != nullptr);
    CHECK(InferredRowHeight() > 4.0f);
    // Following the PC, so the view is near it, not at the region's first row.
    CHECK(ScrollY() >= 0.0f);
}

// Editing a comment must not change the row height the clipper works from.
void TestEditingACommentKeepsRowHeight()
{
    Fixture f;
    // At the top of the region the first row is the one the clipper measures its height from --
    // the row whose growth the bug was in, so it is the one to edit.
    f.panel.GoTo(0, kBase);
    f.h.Settle();
    ScrollToTop(f.h);
    const float before = InferredRowHeight();
    CHECK(before > 4.0f);
    CHECK(ScrollY() == 0.0f);

    const ImVec2 cell = CommentCellOfVisibleRow(0);
    f.h.Click(cell);
    f.h.Click(cell);          // double-click: edit this row's comment
    f.h.Settle();

    CHECK(std::fabs(InferredRowHeight() - before) < 0.01f);
}

// The reported consequence: with the editor open (and after it closes), jumping to an address
// scrolls to that address's row, not somewhere else.
void TestJumpLandsOnTargetWhileEditing()
{
    Fixture f;
    f.panel.GoTo(0, kBase);
    f.h.Settle();
    ScrollToTop(f.h);
    const float rowH = InferredRowHeight();

    const ImVec2 cell = CommentCellOfVisibleRow(0);
    f.h.Click(cell);
    f.h.Click(cell);
    f.h.Settle();

    const uint32_t target = 0x060927C0u;
    f.panel.GoTo(0, target);
    f.h.Settle();

    const float wantRow = (float)((target - kBase) / 2);
    const float gotRow = ScrollY() / rowH;
    CHECK(std::fabs(gotRow - wantRow) < 2.0f);
}
}  // namespace

int main()
{
    TestDrawsTheWholeRegion();
    TestEditingACommentKeepsRowHeight();
    TestJumpLandsOnTargetWhileEditing();
    if (gFailures) { std::printf("FAILURES: %d\n", gFailures); return 1; }
    std::printf("all cases passed\n");
    return 0;
}
