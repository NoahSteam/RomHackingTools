// Layer panel tests — the per-layer viewer tabs driven headlessly (see ImGuiHarness.h).
//
// The export artifacts themselves are covered by LayerExportTests; what this pins is the
// panel wiring that no data test can see: that each layer really submits a window under
// the title App's dock layout references, and that the toolbar's Tile Grid toggle is
// reachable by a click and persists — on the VDP2 tabs only, since the sprite layer has no
// tile grid to outline.

#include <iostream>
#include <set>
#include <string>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"   // FindWindowByName -- asserting a window was submitted
#include "ImGuiHarness.h"

#include "LayerPanels.h"
#include "Settings.h"

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

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

// A platform that does nothing: the panels only need it for texture upload (never reached
// without data) and for the folder picker, which declines here.
class StubPlatform : public IPlatform
{
public:
    bool Initialize(const PlatformConfig&) override { return true; }
    void Shutdown() override {}
    bool PumpEvents() override { return true; }
    void BeginFrame() override {}
    void EndFrame() override {}
    TextureHandle CreateTexture(int, int) override { return 0; }
    void UpdateTexture(TextureHandle, const void*, int, int) override {}
    void DestroyTexture(TextureHandle) override {}
    bool OpenFileDialog(std::string&) override { return false; }
    bool SaveFile(const char*, const void*, size_t) override { return false; }
    // A desktop-shaped platform, so the toolbar lays out its export controls in full.
    bool HasHostFilesystem() override { return true; }
};

// Drives one layer panel, parked at a known position so the toolbar's widget positions can
// be computed rather than guessed.
struct Harness
{
    LayerPanels  panels;
    StubPlatform platform;
    bool         visible[kLayerCount] = {};
    ImVec2       gridCheckbox {};   // centre of the Tile Grid checkbox, recorded each frame

    Harness()
    {
        // Give the panels an export folder up front, so drawing never resolves (and
        // creates) the default one in the user's config directory.
        Settings settings;
        settings.Set("export", "dir", "se_layer_panel_test");
        panels.Load(settings);
    }

    void ShowOnly(LayerId layer)
    {
        for (int i = 0; i < kLayerCount; ++i) visible[i] = (i == static_cast<int>(layer));
    }

    void Ui()
    {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(760.0f, 420.0f));

        // Where the toolbar puts the Tile Grid checkbox: window padding, then the Export
        // button, then one item spacing. The checkbox's hit box is its square frame.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float frameHeight = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;
        const float exportWidth = ImGui::CalcTextSize("Export").x + style.FramePadding.x * 2.0f;
        gridCheckbox = ImVec2(style.WindowPadding.x + exportWidth + style.ItemSpacing.x +
                                  frameHeight * 0.5f,
                              frameHeight + style.WindowPadding.y + frameHeight * 0.5f);

        LayerPanelFrame frame;
        // frame.context stays null: the toolbar draws either way, no core needed.
        static const se_render_opts kOpts = {};
        frame.opts = &kOpts;
        panels.Draw(frame, visible, platform);
    }
};

// --- Tests ---

void TestPanelListIsWellFormed()
{
    const std::vector<LayerPanelDesc>& list = LayerPanelList();
    CHECK(list.size() == kLayerCount);

    std::set<std::string> titles;
    std::set<std::string> keys;
    std::set<int> ids;
    for (size_t i = 0; i < list.size(); ++i)
    {
        titles.insert(list[i].title);
        keys.insert(list[i].key);
        ids.insert(static_cast<int>(list[i].id));
    }
    // Titles double as ImGui window ids and as the strings App's dock layout references,
    // so a duplicate would silently merge two panels.
    CHECK(titles.size() == list.size());
    CHECK(keys.size() == list.size());
    CHECK(ids.size() == kLayerCount);   // every layer has exactly one tab
}

void TestVisiblePanelsAreSubmitted()
{
    Harness h;
    for (int i = 0; i < kLayerCount; ++i) h.visible[i] = true;
    ImGuiHarness imgui([&] { h.Ui(); });
    imgui.Settle();
    for (const LayerPanelDesc& desc : LayerPanelList())
        CHECK(ImGui::FindWindowByName(desc.title) != nullptr);
}

void TestHiddenPanelsAreNotSubmitted()
{
    Harness h;
    h.ShowOnly(kLayerNbg2);
    ImGuiHarness imgui([&] { h.Ui(); });
    imgui.Settle();
    CHECK(ImGui::FindWindowByName("NBG2") != nullptr);
    CHECK(ImGui::FindWindowByName("NBG0") == nullptr);
    CHECK(ImGui::FindWindowByName("VDP1 Sprites") == nullptr);
}

void TestTileGridToggleIsClickable()
{
    Harness h;
    h.ShowOnly(kLayerNbg1);
    ImGuiHarness imgui([&] { h.Ui(); });
    imgui.Settle();
    h.panels.ConsumeSettingsDirty();   // ignore anything the first frames reported

    imgui.Click(h.gridCheckbox);
    imgui.Settle();
    CHECK(h.panels.ConsumeSettingsDirty());

    // The toggle is what gets persisted, so read it back the way App saves it.
    Settings settings;
    h.panels.Save(settings);
    CHECK(settings.GetBool("layergrid", "layerNbg1", false));
    CHECK(!settings.GetBool("layergrid", "layerNbg0", false));   // only the clicked panel

    imgui.Click(h.gridCheckbox);
    imgui.Settle();
    CHECK(h.panels.ConsumeSettingsDirty());
    Settings off;
    h.panels.Save(off);
    CHECK(!off.GetBool("layergrid", "layerNbg1", true));
}

void TestSpriteLayerHasNoTileGrid()
{
    Harness h;
    h.ShowOnly(kLayerVdp1);
    ImGuiHarness imgui([&] { h.Ui(); });
    imgui.Settle();
    h.panels.ConsumeSettingsDirty();

    // Same point, but the sprite layer's toolbar has no checkbox there — whatever sits at
    // that spot must not flip a persisted setting.
    imgui.Click(h.gridCheckbox);
    imgui.Settle();
    CHECK(!h.panels.ConsumeSettingsDirty());
}

}  // namespace

int main()
{
    TestPanelListIsWellFormed();
    TestVisiblePanelsAreSubmitted();
    TestHiddenPanelsAreNotSubmitted();
    TestTileGridToggleIsClickable();
    TestSpriteLayerHasNoTileGrid();

    if (gFailures)
    {
        std::cerr << gFailures << " check(s) failed\n";
        return 1;
    }
    std::cout << "LayerPanelTests: all checks passed\n";
    return 0;
}
