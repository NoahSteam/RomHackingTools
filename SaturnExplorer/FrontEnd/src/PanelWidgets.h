// PanelWidgets — small shared helpers for the ImGui panels, kept in a header of their own
// so the interaction tests can exercise the same code the panels run (see
// FrontEnd/tests/PanelInteractionTests.cpp). Only depends on imgui.h.
#pragma once

#include "imgui.h"

namespace sfe
{

// Flags for the Selectable that makes a whole table row clickable.
//
// A row Selectable uses SpanAllColumns, so its hit box covers the entire row, and it is
// submitted before the cells to its right. ImGui's hit test rejects an item once an
// earlier one owns HoveredId and did not opt into overlap (ImGui::ItemHoverable), so
// without AllowOverlap the row swallows every click in it -- any InputText/InputInt/button
// in a later column can be hovered but never activated. The click reads as a row click,
// and a second one as a double-click, so a row that navigates on double-click will run
// off and steal keyboard focus while the user is trying to type in a cell.
//
// Pass hasInteractiveCells = true whenever any column in the row submits a widget that
// takes clicks. AllowOverlap costs one frame of hover before the row itself is clickable,
// which is why it is not simply always on.
inline ImGuiSelectableFlags RowSelectableFlags(bool hasInteractiveCells)
{
    ImGuiSelectableFlags flags = ImGuiSelectableFlags_SpanAllColumns;
    if (hasInteractiveCells) flags |= ImGuiSelectableFlags_AllowOverlap;
    return flags;
}

}  // namespace sfe
