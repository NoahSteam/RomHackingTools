// Layer panel tests — the per-layer viewer tabs driven headlessly (see ImGuiHarness.h).
//
// The export artifacts themselves are covered by LayerExportTests; what this pins is the
// panel wiring that no data test can see: that each layer really submits a window under
// the title App's dock layout references, that the toolbar's Tile Grid toggle is reachable
// by a click and persists — on the VDP2 tabs only, since the sprite layer has no tile grid
// to outline — and that a Data > Dump Textures request exports the frame on screen.

#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"   // FindWindowByName -- asserting a window was submitted
#include "ImGuiHarness.h"

#include "saturnexplorer/SeHost.h"

#include "FakeVdpSource.h"

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

// The smallest state that renders: a 32x8 VDP1 system clip sets the composited frame size,
// and a draw-end terminator keeps the command walk honest. No sprites and no scroll screens,
// which is enough -- the composed layer image alone makes the export produce a file, and this
// test is about WHICH FRAME was exported, not what the pixels are.
se_context* OpenMinimalContext(se_test::State& state)
{
    se_test::WriteSystemClip(state, 32, 8);
    se_test::PutBE16(state.vdp1, 0x20, 0x8000);   // draw-end
    se_context* ctx = se_test::CreateContext(state);
    CHECK(ctx != nullptr);
    if (ctx) CHECK(se_begin_frame(ctx) == SE_OK);
    return ctx;
}

// Delete the folder an export wrote, so a run leaves nothing behind.
void RemoveExportedFiles(const std::string& message)
{
    // The message ends with the directory. Files are named from the same stem, so removing
    // the directory's contents means removing what BuildLayerExport named -- the composed
    // layer image is the only file a sprite-less VDP1 export writes.
    const std::string marker = " to ";
    const size_t at = message.rfind(marker);
    if (at == std::string::npos) return;
    std::string dir = message.substr(at + marker.size());
    const size_t note = dir.find("  (");
    if (note != std::string::npos) dir.erase(note);
    const size_t slash = dir.find_last_of("/\\");
    const std::string stem = (slash == std::string::npos) ? dir : dir.substr(slash + 1);
    std::remove((dir + "/" + stem + "_layer.bmp").c_str());
}

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

// The P2 this replaced: Data > Dump Textures ran where the menu command was handled, which is
// before the app picks the context the panels will show -- so while scrubbing it wrote the LIVE
// frame while every panel showed the scrubbed one. The request is now queued and serviced by
// Draw, which is handed the displayed frame. Both halves of that are pinned here: nothing runs
// until a Draw, and what runs carries that Draw's frame, not the one in force when the menu was
// used.
void TestMenuRequestExportsTheFrameOnScreen()
{
    se_test::State state(0x200);
    se_context* ctx = OpenMinimalContext(state);
    if (ctx == nullptr) return;

    Settings settings;
    settings.Set("export", "dir", "se_layer_panel_test");
    LayerPanels panels;
    panels.Load(settings);
    StubPlatform platform;

    LayerPanelFrame frame;
    frame.context = ctx;
    static const se_render_opts kOpts = {};
    frame.opts = &kOpts;
    const bool visible[kLayerCount] = {};   // no panel open: the export reads the core, not the view

    std::string message;
    bool error = true;

    // Stand in for "paused at frame 3000, then scrubbed back to 2800": the request is made at a
    // point where 3000 is what an immediate export would have seen, and the Draw that services
    // it carries 2800, the frame the panels are drawing.
    frame.frame = 3000;
    panels.RequestExport(kLayerVdp1);
    CHECK(!panels.ConsumeExportResult(message, error));   // nothing has run yet

    frame.frame = 2800;
    panels.Draw(frame, visible, platform);
    CHECK(panels.ConsumeExportResult(message, error));
    CHECK(!error);
    CHECK(message.find("vdp1_frame02800") != std::string::npos);
    CHECK(message.find("3000") == std::string::npos);
    CHECK(!panels.ConsumeExportResult(message, error));   // the result is one-shot
    RemoveExportedFiles(message);

    // A Draw with no request pending exports nothing, so the frame number alone cannot
    // trigger one.
    frame.frame = 2801;
    panels.Draw(frame, visible, platform);
    std::string unexpected;
    CHECK(!panels.ConsumeExportResult(unexpected, error));

    // And the frame really is read per-Draw rather than captured once: the same request made
    // again against a later frame writes that one.
    panels.RequestExport(kLayerVdp1);
    frame.frame = 2900;
    panels.Draw(frame, visible, platform);
    CHECK(panels.ConsumeExportResult(message, error));
    CHECK(!error);
    CHECK(message.find("vdp1_frame02900") != std::string::npos);
    RemoveExportedFiles(message);

    se_destroy(ctx);
}

// Several requests queued before a Draw all run, and each reports its own result -- App shows
// them one at a time, so a dropped one would be a silent no-op for the user.
void TestEveryQueuedRequestIsReported()
{
    se_test::State state(0x200);
    se_context* ctx = OpenMinimalContext(state);
    if (ctx == nullptr) return;

    Settings settings;
    settings.Set("export", "dir", "se_layer_panel_test");
    LayerPanels panels;
    panels.Load(settings);
    StubPlatform platform;

    LayerPanelFrame frame;
    frame.context = ctx;
    static const se_render_opts kOpts = {};
    frame.opts = &kOpts;
    frame.frame = 12;
    const bool visible[kLayerCount] = {};

    panels.RequestExport(kLayerVdp1);
    panels.RequestExport(kLayerVdp1);
    panels.Draw(frame, visible, platform);

    std::string message;
    bool error = true;
    int reported = 0;
    while (panels.ConsumeExportResult(message, error))
    {
        ++reported;
        CHECK(!error);
        CHECK(message.find("vdp1_frame00012") != std::string::npos);
    }
    CHECK(reported == 2);
    RemoveExportedFiles(message);

    se_destroy(ctx);
}

// An out-of-range layer -- a menu id from a stale menu -- is dropped rather than indexing the
// view array out of bounds.
void TestOutOfRangeRequestIsIgnored()
{
    LayerPanels panels;
    StubPlatform platform;
    panels.RequestExport(static_cast<LayerId>(kLayerCount));
    panels.RequestExport(static_cast<LayerId>(-1));

    LayerPanelFrame frame;   // no context: an export that did run would report a failure
    static const se_render_opts kOpts = {};
    frame.opts = &kOpts;
    const bool visible[kLayerCount] = {};
    panels.Draw(frame, visible, platform);

    std::string message;
    bool error = false;
    CHECK(!panels.ConsumeExportResult(message, error));
}

}  // namespace

int main()
{
    TestPanelListIsWellFormed();
    TestVisiblePanelsAreSubmitted();
    TestHiddenPanelsAreNotSubmitted();
    TestTileGridToggleIsClickable();
    TestSpriteLayerHasNoTileGrid();
    TestMenuRequestExportsTheFrameOnScreen();
    TestEveryQueuedRequestIsReported();
    TestOutOfRangeRequestIsIgnored();

    if (gFailures)
    {
        std::cerr << gFailures << " check(s) failed\n";
        return 1;
    }
    std::cout << "LayerPanelTests: all checks passed\n";
    return 0;
}
