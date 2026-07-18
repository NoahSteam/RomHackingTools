// Theme — the single source of truth for the frontend's look. Replaces the stock
// ImGui::StyleColorsDark() that both backends used to call inline, so the palette,
// rounding, and spacing live in one portable place shared by the Windows, web, and
// desktop builds. Colors + base (unscaled) geometry only; each backend still calls
// style.ScaleAllSizes(dpiScale) afterward for DPI.
#pragma once

#include "imgui.h"

namespace sfe
{

// Apply the Saturn Explorer dark theme (colors + rounding + spacing) to 'style'.
// Call in place of ImGui::StyleColorsDark(), before ScaleAllSizes(dpiScale).
void ApplyTheme(ImGuiStyle& style);

// Load the embedded UI font (a clean proportional sans-serif) and make it the
// default. Call once after ApplyTheme, before the first frame. DPI is handled by
// the backend's io.FontScaleMain, so the size here is the base (1x) pixel size.
void LoadFonts(ImGuiIO& io);

// Theme color tokens for hand-drawn panels (ImDrawList), so custom rendering
// matches the theme instead of using ad-hoc IM_COL32 literals. Returned as packed
// ImU32 (IM_COL32 order) ready for AddRect*/AddText.
namespace ui
{
extern ImFont* gBody;      // default proportional UI font (set by LoadFonts)

ImU32 SelectionOutline();  // bright outline for the selected object/region
ImU32 VramTexture();       // VRAM map region kinds
ImU32 VramClut();
ImU32 VramCmdTable();
ImU32 VramGouraud();
ImU32 VramUnused();
}  // namespace ui

}  // namespace sfe
