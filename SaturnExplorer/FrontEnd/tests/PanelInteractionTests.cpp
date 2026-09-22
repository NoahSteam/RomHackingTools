// Panel interaction tests — ImGui hit testing, driven headlessly (see ImGuiHarness.h).
//
// These pin behaviour that is invisible to a normal unit test and easy to break by
// accident: whether a click on a cell actually reaches the widget in it. The bug that
// prompted them made the VDP1 Command List's size/position boxes impossible to click --
// the row Selectable swallowed every click, and the resulting double-click jumped the Hex
// Editor and took keyboard focus, so typing edited the Memory window instead.
//
// The flag decision under test is the real one: RowSelectableFlags is the function the
// panel itself calls. The surrounding row is a stand-in for the panel's table, since the
// panels are not separable from App, so this covers "the row lets its cells be clicked",
// not "the Command List renders the right columns".

#include <cstdio>
#include <cstdlib>
#include <unistd.h>       // getpid, for the per-process temp config dir
#include <iostream>
#include <string>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"   // ImGuiTable::RowPosY1/2 -- the row rect being asserted on
#include "ImGuiHarness.h"
#include "PanelWidgets.h"
#include "Settings.h"         // the split height's persistence, round-tripped through an INI

using namespace sfe;

namespace
{
int gFailures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
    ++gFailures;
}
}  // namespace

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

namespace
{

// A table row shaped like the VDP1 Command List's: a row-selecting Selectable spanning all
// columns, submitted first, with an editable cell in a later column.
struct Row
{
    bool interactiveCells = true;   // what the panel passes to RowSelectableFlags

    // Recorded each frame.
    bool  rowClicked = false;
    bool  cellHovered = false;
    bool  cellActive = false;
    ImVec2 cellCenter {};
    ImVec2 labelPoint {};

    void Draw()
    {
        rowClicked = cellHovered = cellActive = false;
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
        ImGui::Begin("Row", nullptr, ImGuiWindowFlags_NoSavedSettings);
        if (ImGui::BeginTable("t", 3, ImGuiTableFlags_Borders))
        {
            ImGui::TableSetupColumn("#");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Size");
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            rowClicked = ImGui::Selectable("0", false, RowSelectableFlags(interactiveCells),
                                           ImVec2(0.0f, ImGui::GetFrameHeight()));
            const ImVec2 rowMin = ImGui::GetItemRectMin();
            labelPoint = ImVec2(rowMin.x + 6.0f, rowMin.y + ImGui::GetFrameHeight() * 0.5f);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Scaled");

            ImGui::TableNextColumn();
            int value = 32;
            ImGui::SetNextItemWidth(40.0f);
            ImGui::InputInt("##w", &value, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
            cellHovered = ImGui::IsItemHovered();
            cellActive = ImGui::IsItemActive();
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            cellCenter = ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            ImGui::EndTable();
        }
        ImGui::End();
    }
};

// Click the editable cell; report whether it took the click and whether the row stole it.
void ProbeCell(bool interactiveCells, bool& cellTookClick, bool& rowStoleClick,
               bool& cellHovered)
{
    Row row;
    row.interactiveCells = interactiveCells;
    ImGuiHarness harness([&] { row.Draw(); });
    harness.Settle();
    const ImVec2 target = row.cellCenter;
    harness.Hover(target);
    cellHovered = row.cellHovered;
    harness.Press(target);
    cellTookClick = row.cellActive;
    harness.Frame(target, false);       // release
    rowStoleClick = row.rowClicked;
}

void TestRowSwallowsCellClickWithoutAllowOverlap()
{
    // The failure mode itself: with the row treated as having no interactive cells, the
    // span-all-columns Selectable owns the whole row and the edit box never activates.
    bool cellTookClick = false, rowStoleClick = false, cellHovered = false;
    ProbeCell(false, cellTookClick, rowStoleClick, cellHovered);
    CHECK(!cellTookClick);
    CHECK(rowStoleClick);
    // Why this is invisible to a screenshot, or to "does the widget light up?": the cell
    // still reports as hovered. Only activation differs.
    CHECK(cellHovered);
}

void TestInteractiveCellTakesItsOwnClick()
{
    // With RowSelectableFlags told the row has interactive cells, the click lands on the
    // edit box and the row does not also fire.
    bool cellTookClick = false, rowStoleClick = false, cellHovered = false;
    ProbeCell(true, cellTookClick, rowStoleClick, cellHovered);
    CHECK(cellTookClick);
    CHECK(!rowStoleClick);
    CHECK(cellHovered);
}

void TestRowStillSelectableBesideItsCells()
{
    // The other half: allowing overlap must not cost the row its own click target, or
    // selecting a command by clicking its number would stop working.
    Row row;
    row.interactiveCells = true;
    ImGuiHarness harness([&] { row.Draw(); });
    harness.Settle();
    const ImVec2 label = row.labelPoint;
    harness.Hover(label);
    harness.Click(label);
    CHECK(row.rowClicked);
}

// A row shaped like the Registers panel's SH-2 tab: plain Text cells, with a context menu
// hung off the value cell. No Selectable, so no row item can own HoveredId -- but
// BeginPopupContextItem on a Text item still depends on IsItemHovered, and Text submits
// with id 0, so "does the menu actually open" is worth pinning rather than assuming.
struct ContextRow
{
    bool  menuOpen = false;     // the popup was visible this frame
    bool  itemPicked = false;   // an entry inside it was chosen
    ImVec2 valueCenter {};
    ImVec2 nameCenter {};

    void Draw()
    {
        menuOpen = false;
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
        ImGui::Begin("Regs", nullptr, ImGuiWindowFlags_NoSavedSettings);
        if (ImGui::BeginTable("t", 3, ImGuiTableFlags_Borders))
        {
            ImGui::TableSetupColumn("Reg");
            ImGui::TableSetupColumn("Value");
            ImGui::TableSetupColumn("Notes");
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::PushID(0);
            ImGui::TextUnformatted("R15");
            const ImVec2 nmin = ImGui::GetItemRectMin(), nmax = ImGui::GetItemRectMax();
            nameCenter = ImVec2((nmin.x + nmax.x) * 0.5f, (nmin.y + nmax.y) * 0.5f);

            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", 0x0603F000u);
            const ImVec2 vmin = ImGui::GetItemRectMin(), vmax = ImGui::GetItemRectMax();
            valueCenter = ImVec2((vmin.x + vmax.x) * 0.5f, (vmin.y + vmax.y) * 0.5f);
            if (ImGui::BeginPopupContextItem("regmenu"))
            {
                menuOpen = true;
                if (ImGui::MenuItem("View in Memory")) itemPicked = true;
                ImGui::EndPopup();
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted("-> HWRAM");
            ImGui::PopID();
            ImGui::EndTable();
        }
        ImGui::End();
    }
};

// Right-click either the value cell or the register-name cell; report whether the context
// menu came up.
bool MenuOpensOnValueCell(bool onValueCell)
{
    ContextRow row;
    ImGuiHarness harness([&] { row.Draw(); });
    harness.Settle();
    const ImVec2 target = onValueCell ? row.valueCenter : row.nameCenter;
    harness.Hover(target);
    harness.RightClick(target);
    return row.menuOpen;
}

void TestRegisterValueContextMenu()
{
    CHECK(MenuOpensOnValueCell(true));
    // And it belongs to the value cell, not the row: right-clicking the register name must
    // not open it, or "View in Memory" would appear on a cell whose value it has nothing to
    // do with. This half also proves the first is not passing trivially.
    CHECK(!MenuOpensOnValueCell(false));
}

// Vertical centring of a Command-List-shaped row. The row Selectable is given an explicit
// frame height so its highlight covers the whole row; the bug was pairing that with
// AlignTextToFramePadding, which offsets the Selectable's box by FramePadding.y so the row
// grows taller than its own contents and everything inside it rides above centre.
struct AlignRow
{
    bool alignTextToFramePadding = false;   // the old, broken pairing

    float rowTop = 0.0f, rowBottom = 0.0f;
    ImVec2 boxMin {}, boxMax {};            // an InputInt cell: the row's tallest content

    void Draw()
    {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(800.0f, 300.0f));
        ImGui::Begin("CL", nullptr, ImGuiWindowFlags_NoSavedSettings);
        if (ImGui::BeginTable("commands", 3, ImGuiTableFlags_Borders))
        {
            ImGui::TableSetupColumn("#");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Size");
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            if (alignTextToFramePadding)
            {
                // The old, broken shape, spelled out so the test can show it really does
                // offset the row. Everything else goes through the shared helper.
                ImGui::AlignTextToFramePadding();
                ImGui::Selectable("0", false, RowSelectableFlags(true),
                                  ImVec2(0.0f, ImGui::GetFrameHeight()));
            }
            else
            {
                RowSelectable("0", false, true);
            }

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("System Clip");

            ImGui::TableNextColumn();
            int w = 0;
            ImGui::SetNextItemWidth(40.0f);
            ImGui::InputInt("##w", &w, 0, 0);
            boxMin = ImGui::GetItemRectMin();
            boxMax = ImGui::GetItemRectMax();

            ImGuiTable* t = ImGui::GetCurrentTable();
            rowTop = t->RowPosY1;
            rowBottom = t->RowPosY2;
            ImGui::EndTable();
        }
        ImGui::End();
    }

    float ContentOffsetFromRowCentre() const
    {
        return (boxMin.y + boxMax.y) * 0.5f - (rowTop + rowBottom) * 0.5f;
    }
};

void TestCommandRowContentIsVerticallyCentred()
{
    AlignRow row;
    ImGuiHarness harness([&] { row.Draw(); });
    harness.Settle();
    const float off = row.ContentOffsetFromRowCentre();
    CHECK(off > -0.01f && off < 0.01f);
    // And the row is no taller than the frame it contains (plus cell padding), which is
    // what going off-centre looked like: a row stretched by the Selectable's offset box.
    const float pad = ImGui::GetStyle().CellPadding.y * 2.0f;
    CHECK(row.rowBottom - row.rowTop <= ImGui::GetFrameHeight() + pad + 0.01f);
}

void TestAlignTextToFramePaddingOnRowSelectableOffsetsTheRow()
{
    // The regression itself: pin that the old pairing really does push content off centre,
    // so this test fails if someone reinstates it.
    AlignRow row;
    row.alignTextToFramePadding = true;
    ImGuiHarness harness([&] { row.Draw(); });
    harness.Settle();
    CHECK(row.ContentOffsetFromRowCentre() < -0.5f);
}

// A panel shaped like the Call Stack's: a scrolling list, a draggable separator, and a
// detail section that scrolls its own contents. Dragging is a real interaction (press,
// move, release), so it is driven through the harness rather than by calling the clamp.
struct SplitPanel
{
    float windowHeight = 400.0f;
    float minTop = 60.0f;
    float minBottom = 40.0f;
    int   detailLines = 3;       // raise to overflow the detail section

    float split = 0.0f;          // the persisted value the splitter drags
    float avail = 0.0f;          // space the two sections and the separator share
    float topHeight = 0.0f;      // what the top child was actually given
    float bottomHeight = 0.0f;
    float detailContentHeight = 0.0f;
    int   commits = 0;           // times the splitter said "persist this" (once per drag)
    ImVec2 splitterPoint {};
    // Extremes of the stored height across every frame drawn. A clamp has to be watched
    // all the way through a drag, not only at the end: the panel re-defaults a
    // non-positive split, so an unclamped drag can wrap around and land back in range.
    float loSplit = 1e9f, hiSplit = -1e9f;

    void ResetExtremes() { loSplit = 1e9f; hiSplit = -1e9f; }

    void Draw()
    {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, windowHeight));
        ImGui::Begin("Split", nullptr, ImGuiWindowFlags_NoSavedSettings);
        avail = ImGui::GetContentRegionAvail().y;
        // The panel's own sequence: adopt a height only while both minimums fit, clamp for
        // this frame, and store the clamp back so a drag starts where the boundary is.
        const bool roomy = avail - SplitterHeight() >= minTop + minBottom;
        if (split <= 0.0f && roomy) split = avail - 100.0f;
        topHeight = SplitTopHeight(split, avail, minTop, minBottom);
        if (roomy) split = topHeight;
        loSplit = ImMin(loSplit, split);
        hiSplit = ImMax(hiSplit, split);

        // A height of 0 fills the window, which is what the panel's frame table does when
        // there is no room to split at all.
        ImGui::BeginChild("top", ImVec2(0.0f, topHeight));
        for (int i = 0; i < 20; ++i) ImGui::Text("frame %d", i);
        ImGui::EndChild();

        if (topHeight > 0.0f)
        {
            if (HorizontalSplitter("##sp", split, avail, minTop, minBottom)) ++commits;
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            splitterPoint = ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);

            ImGui::BeginChild("bottom");
            bottomHeight = ImGui::GetWindowHeight();
            for (int i = 0; i < detailLines; ++i) ImGui::Text("detail %d", i);
            detailContentHeight = ImGui::GetCurrentWindow()->DC.CursorMaxPos.y -
                                  ImGui::GetCurrentWindow()->Pos.y;
            ImGui::EndChild();
        }
        ImGui::End();
    }
};

// Hover the separator, press it, move the pointer 'dy' pixels a pixel per frame (so a
// clamp is met on the way rather than jumped over), and release.
//
// The hover is not padding: ImGui's SplitterBehavior submits its item with AllowOverlap,
// and such an item only reports hovered once the *previous* frame's hovered id was already
// it -- so a press that arrives on the first frame the pointer is there does nothing.
void DragSplitter(ImGuiHarness& harness, SplitPanel& panel, float dy)
{
    const ImVec2 start = panel.splitterPoint;
    const float step = dy < 0.0f ? -1.0f : 1.0f;
    harness.Hover(start);
    harness.Press(start);
    for (int i = 1; i <= static_cast<int>(dy * step); ++i)
        harness.Frame(ImVec2(start.x, start.y + i * step), true);
    harness.Frame(ImVec2(start.x, start.y + dy), false);
}

void TestSplitterDragMovesTheBoundary()
{
    SplitPanel panel;
    ImGuiHarness harness([&] { panel.Draw(); });
    harness.Settle();
    const float before = panel.topHeight;

    // A press that moves nothing must not commit: the caller writes its settings file on
    // a commit, and a click on the separator is not a resize.
    harness.Hover(panel.splitterPoint);
    harness.Press(panel.splitterPoint);
    CHECK(panel.commits == 0);
    harness.Frame(panel.splitterPoint, false);

    DragSplitter(harness, panel, -40.0f);
    harness.Settle();
    CHECK(panel.topHeight < before - 39.0f);
    CHECK(panel.topHeight > before - 41.0f);
    // One commit for the whole drag, on release -- not one per moved frame.
    CHECK(panel.commits == 1);
    // The detail section gets exactly what the table gave up.
    CHECK(panel.topHeight + panel.bottomHeight + SplitterHeight() > panel.avail - 1.0f);
    CHECK(panel.topHeight + panel.bottomHeight + SplitterHeight() < panel.avail + 1.0f);
}

void TestSplitterClampsAtBothExtremes()
{
    SplitPanel panel;
    ImGuiHarness harness([&] { panel.Draw(); });
    harness.Settle();

    // Drag well past the top: the list keeps its minimum throughout, and the stored value
    // stops there too rather than banking travel the user would have to undo coming back.
    panel.ResetExtremes();
    DragSplitter(harness, panel, -300.0f);
    harness.Settle();
    CHECK(panel.loSplit > panel.minTop - 0.01f);
    CHECK(panel.topHeight < panel.minTop + 0.01f);
    // And back down again, which must move the boundary from the very first pixel.
    DragSplitter(harness, panel, 5.0f);
    harness.Settle();
    CHECK(panel.topHeight > panel.minTop + 4.0f);

    // Then well past the bottom: the detail section keeps its own minimum.
    const float maxTop = panel.avail - SplitterHeight() - panel.minBottom;
    panel.ResetExtremes();
    DragSplitter(harness, panel, 300.0f);
    harness.Settle();
    CHECK(panel.hiSplit < maxTop + 0.01f);
    CHECK(panel.topHeight > maxTop - 0.01f);
    CHECK(panel.bottomHeight > panel.minBottom - 0.01f);

    // Dragging while already against a clamp moves nothing, so it must not commit either.
    panel.commits = 0;
    DragSplitter(harness, panel, 20.0f);
    CHECK(panel.commits == 0);
}

void TestStoredSplitIsClampedIntoRange()
{
    // A height restored from settings was chosen against whatever the panel measured last
    // run -- a different dock size, a different font, or a hand-edited INI. Both ends have
    // to be brought back into range before anything is drawn with them.
    SplitPanel panel;
    ImGuiHarness harness([&] { panel.Draw(); });
    harness.Settle();
    const float maxTop = panel.avail - SplitterHeight() - panel.minBottom;

    panel.split = 5.0f;              // taller detail than the panel will allow
    harness.Settle();
    CHECK(panel.topHeight > panel.minTop - 0.01f);
    CHECK(panel.topHeight < panel.minTop + 0.01f);

    panel.split = 10000.0f;          // a height from a much bigger window
    harness.Settle();
    CHECK(panel.topHeight > maxTop - 0.01f);
    CHECK(panel.topHeight < maxTop + 0.01f);
    CHECK(panel.bottomHeight > panel.minBottom - 0.01f);
}

void TestPanelTooShortToSplitDrawsNoSeparator()
{
    // Shorter than the separator itself: SplitTopHeight returns 0, the caller reads that
    // as "no split", and the list simply fills what there is. Nothing may be given a
    // negative height on the way.
    SplitPanel panel;
    panel.windowHeight = 30.0f;
    ImGuiHarness harness([&] { panel.Draw(); });
    harness.Settle();
    CHECK(panel.avail < SplitterHeight());
    CHECK(panel.topHeight == 0.0f);
}

void TestSqueezedPanelDoesNotLatchItsClamp()
{
    // A spell docked too short to honour both minimums is drawn at the clamp, but must not
    // be remembered as the user's choice -- widening the panel again would otherwise leave
    // the frame list stuck at its minimum forever.
    SplitPanel panel;
    ImGuiHarness harness([&] { panel.Draw(); });
    harness.Settle();
    DragSplitter(harness, panel, -40.0f);
    harness.Settle();
    const float chosen = panel.split;

    panel.windowHeight = 90.0f;      // squeezed
    harness.Settle();
    CHECK(panel.topHeight < chosen);

    panel.windowHeight = 400.0f;     // and back
    harness.Settle();
    CHECK(panel.split > chosen - 0.01f);
    CHECK(panel.split < chosen + 0.01f);
}

void TestShortPanelKeepsBothSectionsVisible()
{
    // Too short to honour both minimums: neither section may collapse to nothing, or the
    // Frame Detail (or the frame list) would simply disappear in a squeezed dock.
    SplitPanel panel;
    panel.windowHeight = 90.0f;
    ImGuiHarness harness([&] { panel.Draw(); });
    harness.Settle();
    CHECK(panel.avail < panel.minTop + panel.minBottom);
    CHECK(panel.topHeight > 1.0f);
    CHECK(panel.bottomHeight > 1.0f);
    CHECK(panel.topHeight + panel.bottomHeight + SplitterHeight() < panel.avail + 1.0f);
}

void TestTallDetailScrollsInsteadOfGrowing()
{
    // Frame #0's detail is taller than a short detail section. It must scroll inside its
    // own child -- the fixed six-and-a-half-line reservation it replaced overflowed and
    // put a scrollbar on the whole panel instead.
    SplitPanel panel;
    panel.detailLines = 40;
    ImGuiHarness harness([&] { panel.Draw(); });
    harness.Settle();
    CHECK(panel.detailContentHeight > panel.bottomHeight);
    CHECK(panel.topHeight + panel.bottomHeight + SplitterHeight() < panel.avail + 1.0f);
}

// Room a combo of width 'w' leaves for its preview text: the frame minus the padding on
// both sides and the dropdown arrow ImGui draws inside it.
float ComboTextRoom(float w) { return w - ImGui::GetStyle().FramePadding.x * 2.0f -
                                      ImGui::GetFrameHeight(); }

void TestComboWidthFitsItsWidestEntry()
{
    // The Access Log's span combo, whose widest entry "2 bytes" lost its "s" behind the
    // dropdown arrow at the 70px the panel used to pass.
    const char* sizes[] = { "1 byte", "2 bytes", "4 bytes" };
    float sized = 0.0f, widest = 0.0f;
    ImGuiHarness harness([&] {
        ImGui::Begin("Combo", nullptr, ImGuiWindowFlags_NoSavedSettings);
        int idx = 1;
        ImGui::SetNextItemWidth(ComboWidth(sizes, 3));
        ImGui::Combo("##accsize", &idx, sizes, 3);
        sized = ImGui::GetItemRectSize().x;
        widest = ImGui::CalcTextSize(sizes[1]).x;
        ImGui::End();
    });
    harness.Settle();
    CHECK(ComboTextRoom(sized) >= widest);
    // And the number it replaced really did clip, so this is not passing trivially.
    CHECK(ComboTextRoom(70.0f) < widest);
}

void TestSplitHeightRoundTripsThroughSettings()
{
    // The dragged split survives a restart: written to the INI and read back as the same
    // pixel height, including the fractional part a drag leaves behind. Points the config
    // dir at a private directory for the rest of the process -- so this test runs last,
    // and its directory carries the pid so concurrent runs cannot collide.
    const std::string dir = "/tmp/__se_split_settings__" + std::to_string(getpid());
#ifdef _WIN32
    _putenv_s("APPDATA", dir.c_str());
#else
    ::setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
#endif
    std::remove(Settings::FilePath().c_str());

    Settings out;
    out.SetFloat("callstack", "split", 237.5f);
    CHECK(out.Save());

    Settings in;
    in.Load();
    CHECK(in.GetFloat("callstack", "split", 0.0f) == 237.5f);
    // An absent key leaves the default, which is what tells the panel to pick its own,
    // and so does a hand-edited line that isn't a number.
    CHECK(in.GetFloat("callstack", "nosuchkey", -1.0f) == -1.0f);
    in.Set("callstack", "split", "wide");
    CHECK(in.GetFloat("callstack", "split", -1.0f) == -1.0f);
    std::remove(Settings::FilePath().c_str());
}

}  // namespace

int main()
{
    TestRowSwallowsCellClickWithoutAllowOverlap();
    TestInteractiveCellTakesItsOwnClick();
    TestRowStillSelectableBesideItsCells();
    TestRegisterValueContextMenu();
    TestCommandRowContentIsVerticallyCentred();
    TestAlignTextToFramePaddingOnRowSelectableOffsetsTheRow();
    TestSplitterDragMovesTheBoundary();
    TestSplitterClampsAtBothExtremes();
    TestShortPanelKeepsBothSectionsVisible();
    TestSqueezedPanelDoesNotLatchItsClamp();
    TestTallDetailScrollsInsteadOfGrowing();
    TestStoredSplitIsClampedIntoRange();
    TestPanelTooShortToSplitDrawsNoSeparator();
    TestComboWidthFitsItsWidestEntry();
    TestSplitHeightRoundTripsThroughSettings();
    if (gFailures != 0)
    {
        std::cerr << gFailures << " panel interaction check(s) failed\n";
        return 1;
    }
    std::cout << "Panel interaction tests passed\n";
    return 0;
}
