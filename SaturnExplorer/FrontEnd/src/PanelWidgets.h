// PanelWidgets — small shared helpers for the ImGui panels, kept in a header of their own
// so the interaction tests can exercise the same code the panels run (see
// FrontEnd/tests/PanelInteractionTests.cpp). Depends only on Dear ImGui — including
// imgui_internal.h, for the splitter behaviour ImGui keeps there.
#pragma once

#include <cstdarg>
#include <cstdio>

#include "imgui.h"
// Deliberately without IMGUI_DEFINE_MATH_OPERATORS: imgui_internal.h rejects that define
// arriving after imgui.h, which is the order every panel .cpp already includes them in.
#include "imgui_internal.h"

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

// Thickness of the draggable band between two stacked sections: wide enough to grab.
constexpr float kSplitterThickness = 6.0f;

// What the separator costs the two sections: its own band plus the item spacing above and
// below it. Leaving the spacing out oversizes the sections by that much, and the bottom
// one — which fills whatever is left — is the one that loses it.
inline float SplitterHeight()
{
    return kSplitterThickness + ImGui::GetStyle().ItemSpacing.y * 2.0f;
}

// Height for the top section of a vertical split, clamped so neither section can be
// dragged away entirely. 'avail' is the space the two sections and the separator share.
//
// When the panel is too short to honour both minimums the space is divided in their
// proportion instead of clamping one away: both sections scroll their own contents, so a
// squeezed section is still usable, whereas a zero-height one would vanish. Returns 0 when
// there is no room at all for a split, which the caller reads as "skip the split".
inline float SplitTopHeight(float stored, float avail, float minTop, float minBottom)
{
    const float room = avail - SplitterHeight();
    // room > 0 and room < minTop + minBottom together mean the sum is positive, so the
    // proportional branch cannot divide by zero.
    if (room <= 0.0f) return 0.0f;
    if (room < minTop + minBottom) return room * (minTop / (minTop + minBottom));
    if (stored < minTop) return minTop;
    if (stored > room - minBottom) return room - minBottom;
    return stored;
}

// The draggable separator itself, submitted between the two sections. 'height' is the top
// section's height and follows the pointer every frame of a drag; the return is the
// *commit* — true once, when a drag that moved something is released. Persisting on that
// rather than on every moved frame is what keeps a one-second drag from rewriting the
// settings file sixty times.
//
// ImGui's own SplitterBehavior does the dragging: it tracks the grab against the offset
// the press was made at (not the frame's mouse delta, which would jump the split by the
// whole travel when a click lands straight on the band), clamps both sections to their
// minimums, and renders the band in the Separator colours with the resize cursor. It only
// registers the item, so a Dummy advances the layout past the band.
inline bool HorizontalSplitter(const char* id, float& height, float avail,
                               float minTop, float minBottom)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + ImGui::GetContentRegionAvail().x,
                                pos.y + kSplitterThickness));
    float top = height;
    float bottom = avail - SplitterHeight() - height;
    ImGui::SplitterBehavior(bb, window->GetID(id), ImGuiAxis_Y, &top, &bottom,
                            minTop, minBottom, 0.0f);
    height = top;
    // Read before the Dummy: that submits an item of its own, and IsItemDeactivated...
    // reports on whichever item was submitted last.
    const bool committed = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::Dummy(ImVec2(0.0f, kSplitterThickness));
    return committed;
}

// Width for a combo box holding 'items': its widest entry, the frame padding on both
// sides, and the arrow button ImGui draws inside the frame (a square of the frame height).
// A hand-picked number instead clips the last character of the longest entry -- and the
// widest entry is measured rather than named, so adding one cannot quietly reintroduce
// that. Pass the same array the Combo gets.
inline float ComboWidth(const char* const items[], int count)
{
    float widest = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        const float w = ImGui::CalcTextSize(items[i]).x;
        if (w > widest) widest = w;
    }
    return widest + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight();
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
