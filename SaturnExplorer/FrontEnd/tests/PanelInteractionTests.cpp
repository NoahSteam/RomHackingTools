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

#include <iostream>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"   // ImGuiTable::RowPosY1/2 -- the row rect being asserted on
#include "ImGuiHarness.h"
#include "PanelWidgets.h"

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

}  // namespace

int main()
{
    TestRowSwallowsCellClickWithoutAllowOverlap();
    TestInteractiveCellTakesItsOwnClick();
    TestRowStillSelectableBesideItsCells();
    TestRegisterValueContextMenu();
    TestCommandRowContentIsVerticallyCentred();
    TestAlignTextToFramePaddingOnRowSelectableOffsetsTheRow();
    if (gFailures != 0)
    {
        std::cerr << gFailures << " panel interaction check(s) failed\n";
        return 1;
    }
    std::cout << "Panel interaction tests passed\n";
    return 0;
}
