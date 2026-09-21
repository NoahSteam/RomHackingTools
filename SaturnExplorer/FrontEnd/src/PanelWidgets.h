// PanelWidgets — small shared helpers for the ImGui panels, kept in a header of their own
// so the interaction tests can exercise the same code the panels run (see
// FrontEnd/tests/PanelInteractionTests.cpp). Only depends on imgui.h.
#pragma once

#include <cstdarg>
#include <cstdio>

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

// The whole-row Selectable itself: submit this as the row's first cell and it both selects
// the row and lets the cells beside it be clicked. Returns true on click.
//
// 'framed' means the row carries widgets that draw a frame (edit boxes), which makes it a
// full frame tall. Two things then have to happen together, which is why they live here
// rather than at the call site:
//
//   * the Selectable is given that frame height, or its highlight covers one line of text
//     instead of the row;
//   * its label is centred inside that box. NOT AlignTextToFramePadding -- Selectable
//     derives its box from CursorPos + CurrLineTextBaseOffset (imgui_widgets.cpp), so the
//     frame-padding offset pushes the whole box down and the row grows to fit it: the row
//     ends up taller than its own contents and everything in it rides above centre.
//     Centring the label in the box puts it on exactly the baseline the framed widgets and
//     the AlignTextToFramePadding'd text cells use.
//
// Text cells elsewhere in a framed row still want AlignTextToFramePadding; it is only the
// row Selectable that must not have it.
inline bool RowSelectable(const char* label, bool selected, bool framed)
{
    const float height = framed ? ImGui::GetFrameHeight() : 0.0f;
    if (framed)
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    const bool pressed = ImGui::Selectable(label, selected, RowSelectableFlags(framed),
                                           ImVec2(0.0f, height));
    if (framed) ImGui::PopStyleVar();
    return pressed;
}

// Horizontally centre 'width' worth of content in the current table cell, for a column
// holding a small fixed-width group (two edit boxes, or "W x H") that the user can widen.
// Only ever shifts right, so it is a no-op when the column is too narrow to centre in.
inline void CenterInCell(float width)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > width)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - width) * 0.5f);
}

// printf-style text, centred in the current table cell.
inline void TextCenteredInCell(const char* fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    CenterInCell(ImGui::CalcTextSize(buf).x);
    ImGui::TextUnformatted(buf);
}

}  // namespace sfe
