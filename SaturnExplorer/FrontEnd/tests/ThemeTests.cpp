#include "Theme.h"
#include <cstdio>
#include <cstring>

int main()
{
    int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
    using namespace sfe;
    ImGui::CreateContext();
    CHECK(ParseThemeMode(nullptr) == ThemeMode::Dark);
    CHECK(ParseThemeMode("") == ThemeMode::Dark);
    CHECK(ParseThemeMode("invalid") == ThemeMode::Dark);
    CHECK(ParseThemeMode("light") == ThemeMode::Light);
    CHECK(ParseThemeMode("dark") == ThemeMode::Dark);
    CHECK(std::strcmp(ThemeModeName(ThemeMode::Light), "light") == 0);
    ImGuiStyle& style = ImGui::GetStyle();
    ApplyTheme(style);
    style.ScaleAllSizes(2.0f);
    style.FontScaleDpi = 2.0f;
    const ImVec2 padding = style.FramePadding;
    const float rounding = style.WindowRounding;
    const ImVec4 darkText = style.Colors[ImGuiCol_Text];
    const ImU32 darkUnused = ui::VramUnused();
    const ImU32 texture = ui::VramTexture();
    const ImU32 selection = ui::SelectionOutline();
    for (int i = 0; i < 8; ++i)
    {
        ApplyThemeColors(style, ThemeMode::Light);
        CHECK(GetThemeMode() == ThemeMode::Light);
        CHECK(style.Colors[ImGuiCol_Text].x < 0.2f);
        CHECK(style.Colors[ImGuiCol_WindowBg].x > 0.9f);
        CHECK(ui::VramUnused() != darkUnused);
        CHECK(ui::VramTexture() == texture);
        CHECK(ui::SelectionOutline() == selection);
        CHECK(style.FramePadding.x == padding.x && style.FramePadding.y == padding.y);
        CHECK(style.WindowRounding == rounding && style.FontScaleDpi == 2.0f);
        // Model the Windows DPI reset: selected palette survives fresh base geometry.
        const ThemeMode mode = GetThemeMode();
        ApplyTheme(style, mode);
        style.ScaleAllSizes(2.0f);
        CHECK(GetThemeMode() == ThemeMode::Light);
        CHECK(style.FramePadding.x == padding.x);
        ApplyThemeColors(style, ThemeMode::Dark);
        CHECK(GetThemeMode() == ThemeMode::Dark);
        CHECK(style.Colors[ImGuiCol_Text].x == darkText.x);
    }
    ImGui::DestroyContext();
    return failures ? 1 : 0;
}
