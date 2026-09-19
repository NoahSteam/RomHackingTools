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

}  // namespace

int main()
{
    TestRowSwallowsCellClickWithoutAllowOverlap();
    TestInteractiveCellTakesItsOwnClick();
    TestRowStillSelectableBesideItsCells();
    if (gFailures != 0)
    {
        std::cerr << gFailures << " panel interaction check(s) failed\n";
        return 1;
    }
    std::cout << "Panel interaction tests passed\n";
    return 0;
}
