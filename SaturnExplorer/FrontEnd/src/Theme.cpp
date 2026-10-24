#include "Theme.h"

#include <cstring>
#include <initializer_list>

#include "../third_party/fonts/LiberationSans.h"
#include "../third_party/fonts/IpaGothicJP.h"

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

ThemeMode ParseThemeMode(const char* value)
{
    return value && std::strcmp(value, "light") == 0 ? ThemeMode::Light : ThemeMode::Dark;
}
const char* ThemeModeName(ThemeMode mode) { return mode == ThemeMode::Light ? "light" : "dark"; }
ThemeMode GetThemeMode() { return ui::IsLight() ? ThemeMode::Light : ThemeMode::Dark; }

void ApplyThemeColors(ImGuiStyle& style, ThemeMode mode)
{
    // Build in a temporary style: stock palette functions must never reset live geometry.
    ImGuiStyle palette;
    ApplyTheme(palette, mode);
    for (int i = 0; i < ImGuiCol_COUNT; ++i) style.Colors[i] = palette.Colors[i];
}

void ApplyTheme(ImGuiStyle& style) { ApplyTheme(style, ThemeMode::Dark); }
void ApplyTheme(ImGuiStyle& style, ThemeMode mode)
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

    if (mode == ThemeMode::Light)
    {
        ImGui::StyleColorsLight(&style);
        const ImVec4 panel = Rgb(247,248,250), raised = Rgb(233,237,242);
        const ImVec4 input = Rgb(255,255,255), hover = Rgb(223,231,238);
        const ImVec4 border = Rgb(205,211,220), accent = Rgb(8,127,140);
        const ImVec4 selection = Rgb(209,238,240);
        c[ImGuiCol_Text] = Rgb(36,43,54);
        c[ImGuiCol_TextDisabled] = Rgb(104,115,134);
        c[ImGuiCol_WindowBg] = panel;
        c[ImGuiCol_ChildBg] = ImVec4(0,0,0,0);
        c[ImGuiCol_PopupBg] = panel;
        c[ImGuiCol_Border] = border;
        c[ImGuiCol_BorderShadow] = ImVec4(0,0,0,0);
        c[ImGuiCol_FrameBg] = input;
        c[ImGuiCol_FrameBgHovered] = hover;
        c[ImGuiCol_FrameBgActive] = selection;
        for (int slot : { ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed,
                          ImGuiCol_MenuBarBg, ImGuiCol_TableHeaderBg, ImGuiCol_Tab, ImGuiCol_TabDimmed,
                          ImGuiCol_Button }) c[slot] = raised;
        c[ImGuiCol_ButtonHovered] = hover;
        c[ImGuiCol_ButtonActive] = selection;
        c[ImGuiCol_Header] = selection;
        c[ImGuiCol_HeaderHovered] = Rgb(185,226,230);
        c[ImGuiCol_HeaderActive] = Rgb(163,215,221);
        for (int slot : { ImGuiCol_CheckMark, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive,
                          ImGuiCol_NavCursor, ImGuiCol_TabSelectedOverline,
                          ImGuiCol_SeparatorActive, ImGuiCol_SeparatorHovered }) c[slot] = accent;
        c[ImGuiCol_ScrollbarBg] = ImVec4(0,0,0,0);
        c[ImGuiCol_ScrollbarGrab] = border;
        c[ImGuiCol_ScrollbarGrabHovered] = Rgb(104,115,134);
        c[ImGuiCol_ScrollbarGrabActive] = accent;
        c[ImGuiCol_TabHovered] = hover;
        c[ImGuiCol_TabSelected] = c[ImGuiCol_TabDimmedSelected] = panel;
        c[ImGuiCol_TabDimmedSelectedOverline] = WithAlpha(accent, 0.5f);
        c[ImGuiCol_Separator] = border;
        c[ImGuiCol_DockingEmptyBg] = raised;
        c[ImGuiCol_DockingPreview] = WithAlpha(accent, 0.35f);
        c[ImGuiCol_TableBorderStrong] = border;
        c[ImGuiCol_TableBorderLight] = WithAlpha(border, 0.5f);
        c[ImGuiCol_TableRowBg] = ImVec4(0,0,0,0);
        c[ImGuiCol_TableRowBgAlt] = Rgb(238,242,246);
        c[ImGuiCol_TextSelectedBg] = WithAlpha(accent, 0.25f);
        c[ImGuiCol_ResizeGrip] = WithAlpha(accent, 0.2f);
        c[ImGuiCol_ResizeGripHovered] = WithAlpha(accent, 0.4f);
        c[ImGuiCol_ResizeGripActive] = WithAlpha(accent, 0.6f);
        c[ImGuiCol_PlotLines] = Rgb(8,100,115);
        c[ImGuiCol_PlotHistogram] = Rgb(128,91,0);
        c[ImGuiCol_DragDropTarget] = Rgb(128,91,0);
    }

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

    // Merge Japanese glyphs (IPAGothic, subset to ImGui's Japanese ranges) into the
    // body font so JP text renders — the Hex Editor's Shift-JIS pane and any game
    // strings. Embedded + compressed, so the web single-file build needs no fetch.
    ImFontConfig jp;
    jp.MergeMode = true;
    jp.OversampleH = 2;
    jp.OversampleV = 2;
    io.Fonts->AddFontFromMemoryCompressedBase85TTF(
        IpaGothicJP_compressed_data_base85, 16.0f, &jp, io.Fonts->GetGlyphRangesJapanese());

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
ImU32 VramUnused()       { return Color(IM_COL32(0x3A, 0x40, 0x4C, 0xFF), IM_COL32(217,222,229,255)); }
}  // namespace ui

}  // namespace sfe
