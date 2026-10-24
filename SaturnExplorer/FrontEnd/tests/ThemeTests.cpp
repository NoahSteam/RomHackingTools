#include "Theme.h"
#include "ThemePreference.h"
#include "LogPanel.h"
#include "ImGuiHarness.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace
{
float Luminance(const ImVec4& c)
{
    auto linear = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * linear(c.x) + 0.7152f * linear(c.y) + 0.0722f * linear(c.z);
}
float Contrast(const ImVec4& a, const ImVec4& b)
{
    float x = Luminance(a), y = Luminance(b);
    return (std::max(x,y) + 0.05f) / (std::min(x,y) + 0.05f);
}
}
int main()
{
    int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
    using namespace sfe;
    ImGui::CreateContext();
    CHECK(ParseThemeMode(nullptr) == ThemeMode::Dark);
    CHECK(ParseThemeMode("") == ThemeMode::Dark);
    CHECK(ParseThemeMode("invalid") == ThemeMode::Dark);
    CHECK(ParseThemeMode("Light") == ThemeMode::Light);
    CHECK(ParseThemeMode("LIGHT") == ThemeMode::Light);
    CHECK(ParseThemeMode("light") == ThemeMode::Light);
    CHECK(ParseThemeMode("dark") == ThemeMode::Dark);
    CHECK(std::strcmp(ThemeModeName(ThemeMode::Light), "light") == 0);
    ImGuiStyle& style = ImGui::GetStyle();
    ApplyTheme(style);
    style.ScaleAllSizes(2.0f);
    style.FontScaleDpi = 2.0f;
    const ImVec2 padding = style.FramePadding;
    const float rounding = style.WindowRounding;
    const float frameBorder = style.FrameBorderSize;
    const ImVec4 darkText = style.Colors[ImGuiCol_Text];
    const ImU32 darkUnused = ui::VramUnused();
    const ImU32 texture = ui::VramTexture();
    const ImU32 selection = ui::SelectionOutline();
    for (int i = 0; i < 8; ++i)
    {
        ApplyThemeColors(style, ThemeMode::Light);
        CHECK(GetThemeMode() == ThemeMode::Light);
        CHECK(style.FrameBorderSize == frameBorder && frameBorder >= 1.0f);
        CHECK(Contrast(style.Colors[ImGuiCol_Border], style.Colors[ImGuiCol_FrameBg]) >= 3.0f);
        CHECK(Contrast(style.Colors[ImGuiCol_Border], style.Colors[ImGuiCol_WindowBg]) >= 3.0f);
        CHECK(Contrast(style.Colors[ImGuiCol_TextDisabled], style.Colors[ImGuiCol_TableHeaderBg]) >= 4.5f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,1));
        CHECK(GetThemeMode() == ThemeMode::Light);
        CHECK(ui::IsLight());
        ImGui::PopStyleColor();
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
    Settings settings;
    settings.Set("unrelated", "keep", "value");
    ThemePreference preference;
    preference.Load(settings);
    CHECK(preference.Mode() == ThemeMode::Dark);
    settings.Set("appearance", "theme", "invalid");
    preference.Load(settings);
    CHECK(preference.Mode() == ThemeMode::Dark);
    settings.Set("appearance", "theme", "Light");
    preference.Load(settings);
    CHECK(preference.Mode() == ThemeMode::Light);
    CHECK(GetThemeMode() == ThemeMode::Dark);  // Load only requests; startup explicitly applies.
    preference.ApplyPending();
    CHECK(GetThemeMode() == ThemeMode::Light);
    CHECK(preference.Request(MenuCommand::ThemeDark));
    CHECK(preference.Mode() == ThemeMode::Dark);
    CHECK(GetThemeMode() == ThemeMode::Light); // Menu dispatch leaves this frame unchanged.
    preference.Store(settings);               // Save the request, not the old applied palette.
    CHECK(settings.Get("appearance", "theme") == "dark");
    CHECK(settings.Get("unrelated", "keep") == "value");
    preference.ApplyPending();                // Next frame boundary.
    CHECK(GetThemeMode() == ThemeMode::Dark);
    CHECK(!preference.Request(MenuCommand::ToggleTooltips));
    CHECK(preference.Mode() == ThemeMode::Dark);
    CHECK(preference.Request(MenuCommand::ThemeLight));
    preference.Store(settings);
    ThemePreference reloaded;
    reloaded.Load(settings);
    CHECK(reloaded.Mode() == ThemeMode::Light);
    CHECK(GetThemeMode() == ThemeMode::Dark);
    reloaded.ApplyPending();
    CHECK(GetThemeMode() == ThemeMode::Light);
    ImGui::DestroyContext();
    // Exercise the real Log panel in Light. Font quads have atlas UVs distinct
    // from the white pixel used by backgrounds/borders; check their actual colors.
    LogPanel log;
    for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i)
        log.Push(static_cast<LogCategory>(i), "Readable event message", 42);
    {
        ImGuiHarness harness([&] {
            ImGui::SetNextWindowPos(ImVec2(0,0));
            ImGui::SetNextWindowSize(ImVec2(1280,720));
            ImGui::Begin("Log contrast");
            LogPanel::Request req;
            log.Draw(req);
            ImGui::End();
        });
        ApplyTheme(ImGui::GetStyle(), ThemeMode::Light);
        harness.Settle();
        const ImVec2 white = ImGui::GetIO().Fonts->TexUvWhitePixel;
        const ImVec4 background = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        auto isGlyph = [&](const ImVec2& uv) {
            if (uv.x == white.x && uv.y == white.y) return false;
            // ImGui also uses atlas UVs for anti-aliased lines, not just text.
            for (const ImVec4& line : ImGui::GetIO().Fonts->TexUvLines)
                if (uv.y == line.y && uv.x >= line.x && uv.x <= line.z) return false;
            return true;
        };
        int textVertices = 0;
        const ImDrawData* data = ImGui::GetDrawData();
        for (int i = 0; i < data->CmdListsCount; ++i)
            for (const ImDrawVert& v : data->CmdLists[i]->VtxBuffer)
                if (isGlyph(v.uv) && (v.col >> 24) == 255)
                {
                    ++textVertices;
                    CHECK(Contrast(ImGui::ColorConvertU32ToFloat4(v.col), background) >= 4.5f);
                }
        CHECK(textVertices > 100);
    }
    return failures ? 1 : 0;
}
