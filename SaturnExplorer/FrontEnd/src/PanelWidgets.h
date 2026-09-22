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

// Draw a checkerboard behind an image rect so transparent texels read clearly. Shared by
// every panel that shows an image with alpha (Texture Viewer, the per-layer viewers).
inline void Checkerboard(ImVec2 topLeft, ImVec2 size, float cell)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int cols = static_cast<int>(size.x / cell) + 1;
    const int rows = static_cast<int>(size.y / cell) + 1;
    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            const ImU32 tone = ((r ^ c) & 1) ? IM_COL32(70, 70, 78, 255)
                                             : IM_COL32(48, 48, 54, 255);
            const ImVec2 a(topLeft.x + c * cell, topLeft.y + r * cell);
            ImVec2 b(a.x + cell, a.y + cell);
            if (b.x > topLeft.x + size.x) b.x = topLeft.x + size.x;
            if (b.y > topLeft.y + size.y) b.y = topLeft.y + size.y;
            dl->AddRectFilled(a, b, tone);
        }
    }
}

// Place a w*h core-rendered image inside 'avail' at the Saturn's 4:3 output aspect, letter-
// or pillarboxed. The framebuffer's pixel count (320/352/640 wide, 224-480 tall) is
// stretched to fill 4:3 exactly as the hardware does — drawing the pixels square (320x224
// = 1.43:1) makes everything look too wide — so the horizontal and vertical scales differ.
// Returns the on-screen size; 'outPos' gets the top-left corner.
inline ImVec2 FitToDisplayAspect(const ImVec2& avail, ImVec2& outPos)
{
    const float dar = 4.0f / 3.0f;
    float fitW = avail.x;
    float fitH = avail.y;
    if (avail.y > 0.0f && avail.x / avail.y > dar) fitW = avail.y * dar;   // pillarbox
    else if (avail.y > 0.0f)                       fitH = avail.x / dar;   // letterbox
    const ImVec2 cur = ImGui::GetCursorScreenPos();
    outPos = ImVec2(cur.x + (avail.x - fitW) * 0.5f, cur.y + (avail.y - fitH) * 0.5f);
    return ImVec2(fitW, fitH);
}

// A small caption in the top-left corner of an image, drawn with a one-pixel drop shadow so
// it stays legible over whatever the image happens to show there.
inline void ImageCornerLabel(ImVec2 imagePos, const char* text)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 at(imagePos.x + 4.0f, imagePos.y + 3.0f);
    dl->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 200), text);
    dl->AddText(at, IM_COL32(255, 240, 120, 230), text);
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
