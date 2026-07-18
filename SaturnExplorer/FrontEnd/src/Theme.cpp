#include "Theme.h"

#include "../third_party/fonts/LiberationSans.h"

namespace sfe
{

namespace
{
// Build an ImVec4 color from 8-bit sRGB components (as read off a design palette).
inline ImVec4 Rgb(int r, int g, int b, float a = 1.0f)
{
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

// --- Palette (deep charcoal/navy with a cyan accent + warm selection). ---
const ImVec4 kAppBg      = Rgb(0x0F, 0x11, 0x16);   // darkest — behind panels / dock gaps
const ImVec4 kPanelBg    = Rgb(0x16, 0x19, 0x22);   // panel (window) body
const ImVec4 kRaisedBg   = Rgb(0x1E, 0x22, 0x2C);   // title bars, tabs, headers
const ImVec4 kInputBg    = Rgb(0x0F, 0x11, 0x16);   // frame backgrounds (inputs, troughs)
const ImVec4 kHoverBg    = Rgb(0x26, 0x2C, 0x39);   // hovered raised surfaces
const ImVec4 kBorder     = Rgb(0x2A, 0x2F, 0x3A);   // borders / separators
const ImVec4 kText       = Rgb(0xE4, 0xE7, 0xED);
const ImVec4 kTextDim    = Rgb(0x6B, 0x72, 0x80);
const ImVec4 kAccent     = Rgb(0x35, 0xC6, 0xD6);   // cyan/teal — active / selected
const ImVec4 kAccentDim  = Rgb(0x24, 0x86, 0x92);   // accent, muted (inactive/hover base)
const ImVec4 kSelection  = Rgb(0xF2, 0xC1, 0x4E);   // warm amber — selection outline

inline ImVec4 WithAlpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }
}  // namespace

void ApplyTheme(ImGuiStyle& style)
{
    // Start from the stock dark theme, then override — so any ImGuiCol we don't
    // name explicitly still has a sensible value.
    ImGui::StyleColorsDark(&style);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                 = kText;
    c[ImGuiCol_TextDisabled]         = kTextDim;
    c[ImGuiCol_WindowBg]             = kPanelBg;
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = kRaisedBg;
    c[ImGuiCol_Border]               = kBorder;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = kInputBg;
    c[ImGuiCol_FrameBgHovered]       = kHoverBg;
    c[ImGuiCol_FrameBgActive]        = WithAlpha(kAccent, 0.24f);
    c[ImGuiCol_TitleBg]              = kRaisedBg;
    c[ImGuiCol_TitleBgActive]        = kRaisedBg;
    c[ImGuiCol_TitleBgCollapsed]     = kRaisedBg;
    c[ImGuiCol_MenuBarBg]            = kRaisedBg;
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = kBorder;
    c[ImGuiCol_ScrollbarGrabHovered] = kHoverBg;
    c[ImGuiCol_ScrollbarGrabActive]  = kAccentDim;
    c[ImGuiCol_CheckMark]            = kAccent;
    c[ImGuiCol_SliderGrab]           = kAccent;
    c[ImGuiCol_SliderGrabActive]     = kAccent;
    c[ImGuiCol_Button]               = kRaisedBg;
    c[ImGuiCol_ButtonHovered]        = kHoverBg;
    c[ImGuiCol_ButtonActive]         = WithAlpha(kAccent, 0.30f);
    c[ImGuiCol_Header]               = WithAlpha(kAccent, 0.18f);   // selectables / tree
    c[ImGuiCol_HeaderHovered]        = WithAlpha(kAccent, 0.28f);
    c[ImGuiCol_HeaderActive]         = WithAlpha(kAccent, 0.38f);
    c[ImGuiCol_Separator]            = kBorder;
    c[ImGuiCol_SeparatorHovered]     = kAccentDim;
    c[ImGuiCol_SeparatorActive]      = kAccent;
    c[ImGuiCol_ResizeGrip]           = WithAlpha(kAccent, 0.20f);
    c[ImGuiCol_ResizeGripHovered]    = WithAlpha(kAccent, 0.40f);
    c[ImGuiCol_ResizeGripActive]     = WithAlpha(kAccent, 0.60f);
    c[ImGuiCol_Tab]                  = kRaisedBg;
    c[ImGuiCol_TabHovered]           = kHoverBg;
    c[ImGuiCol_TabSelected]          = kPanelBg;
    c[ImGuiCol_TabSelectedOverline]  = kAccent;   // accent underline on the active tab
    c[ImGuiCol_TabDimmed]            = kRaisedBg;
    c[ImGuiCol_TabDimmedSelected]    = kPanelBg;
    c[ImGuiCol_DockingPreview]       = WithAlpha(kAccent, 0.35f);
    c[ImGuiCol_DockingEmptyBg]       = kAppBg;
    c[ImGuiCol_TableHeaderBg]        = kRaisedBg;
    c[ImGuiCol_TableBorderStrong]    = kBorder;
    c[ImGuiCol_TableBorderLight]     = WithAlpha(kBorder, 0.5f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = WithAlpha(ImVec4(1, 1, 1, 1), 0.02f);
    c[ImGuiCol_TextSelectedBg]       = WithAlpha(kAccent, 0.35f);
    c[ImGuiCol_NavCursor]            = kAccent;

    // Geometry — rounded "cards" + airier spacing. Backends call ScaleAllSizes()
    // after this, so these are the base (1x) values.
    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 6.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(9.0f, 5.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.CellPadding       = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 10.0f;
    style.WindowTitleAlign  = ImVec2(0.02f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;   // drop the collapse arrow, cleaner headers
    style.SeparatorTextBorderSize  = 2.0f;
}

void LoadFonts(ImGuiIO& io)
{
    io.Fonts->Clear();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    // One embedded source at the body size; DPI comes from io.FontScaleMain.
    ui::gBody = io.Fonts->AddFontFromMemoryCompressedBase85TTF(
        LiberationSansRegular_compressed_data_base85, 15.0f, &cfg);
    io.FontDefault = ui::gBody;
}

namespace ui
{
ImFont* gBody = nullptr;

ImU32 SelectionOutline() { return ImGui::GetColorU32(kSelection); }
ImU32 VramTexture()      { return IM_COL32(0x5A, 0xBE, 0x78, 0xFF); }
ImU32 VramClut()         { return IM_COL32(0xDC, 0xC8, 0x5A, 0xFF); }
ImU32 VramCmdTable()     { return IM_COL32(0x35, 0xC6, 0xD6, 0xFF); }
ImU32 VramGouraud()      { return IM_COL32(0xC8, 0x78, 0xD2, 0xFF); }
ImU32 VramUnused()       { return IM_COL32(0x3A, 0x40, 0x4C, 0xFF); }
}  // namespace ui

}  // namespace sfe
