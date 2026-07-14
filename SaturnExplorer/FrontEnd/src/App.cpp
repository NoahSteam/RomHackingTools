#include "App.h"

#include <cstdio>
#include <string>

#include "imgui.h"

#include "Platform/IPlatform.h"
#include "savestate_driver.h"

namespace sfe
{

namespace
{

// ImGui::Checkbox wants a bool*, but se_render_opts stores flags as uint8_t.
// Bridge them safely rather than aliasing a uint8_t* as bool*.
bool CheckboxU8(const char* label, uint8_t* value)
{
    bool checked = (*value != 0);
    bool changed = ImGui::Checkbox(label, &checked);
    if (changed)
    {
        *value = checked ? 1 : 0;
    }
    return changed;
}

}  // namespace

void App::Initialize()
{
    // Default: show everything, nothing highlighted.
    mRenderOpts.show_vdp1_sprites = 1;
    mRenderOpts.show_wireframe = 0;
    mRenderOpts.show_bounding_boxes = 1;
    mRenderOpts.show_object_numbers = 1;
    for (int i = 0; i < SE_LAYER_COUNT; ++i)
    {
        mRenderOpts.show_layer[i] = 1;
    }
    mRenderOpts.show_window = 1;
    mRenderOpts.show_color_calculation = 1;
    mRenderOpts.show_shadow_highlight = 1;
    mRenderOpts.highlight_command = -1;
}

void App::Shutdown()
{
    CloseData();
}

void App::CloseData()
{
    if (mContext)
    {
        se_destroy(mContext);  // also closes the data source
        mContext = nullptr;
    }
    mbHasData = false;
    mSelectedCommand = -1;
}

bool App::OpenFullDump(const char* path, uint32_t baseAddress)
{
    CloseData();

    se_data_source dataSource;
    if (se_savestate_open_full_dump(path, baseAddress, &dataSource) != 0)
    {
        return false;
    }

    se_config config;
    config.abi_version = SE_ABI_VERSION;
    config.reserved = 0;

    mContext = se_create(&dataSource, &config);
    if (!mContext)
    {
        if (dataSource.close)
        {
            dataSource.close(dataSource.user);
        }
        return false;
    }

    mDataSource = dataSource;
    mbHasData = true;
    return true;
}

void App::BuildUI(IPlatform& platform)
{
    if (mbHasData)
    {
        se_begin_frame(mContext);
    }

    DrawMenuBar(platform);

    // Full-viewport dockspace so every panel below is dockable.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    DrawLayerControls();
    DrawVdpOutput();
    DrawCommandList();
    DrawSelectedObject();
    DrawVramMap();
    DrawArchiveExplorer();
    DrawTextureViewer();
    DrawPaletteViewer();
    DrawReferences();
    DrawMemoryHistory();
    DrawSearch();
}

void App::DrawMenuBar(IPlatform& platform)
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Open Memory Dump..."))
        {
            std::string path;
            if (platform.OpenFileDialog(path))
            {
                // A full linear dump is based at the start of the SH-2 bus.
                OpenFullDump(path.c_str(), 0x00000000u);
            }
        }
        if (ImGui::MenuItem("Close", nullptr, false, mbHasData))
        {
            CloseData();
        }
        ImGui::EndMenu();
    }

    // Right-aligned status text.
    char status[128];
    if (mbHasData)
    {
        std::snprintf(status, sizeof(status), "Loaded  |  commands: %zu",
                      se_command_count(mContext));
    }
    else
    {
        std::snprintf(status, sizeof(status), "No data loaded");
    }
    float width = ImGui::CalcTextSize(status).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - width - 16.0f);
    ImGui::TextUnformatted(status);

    ImGui::EndMainMenuBar();
}

void App::DrawLayerControls()
{
    if (ImGui::Begin("Layer Controls"))
    {
        ImGui::SeparatorText("VDP1 (Sprites)");
        CheckboxU8("Sprites", &mRenderOpts.show_vdp1_sprites);
        CheckboxU8("Wireframe", &mRenderOpts.show_wireframe);
        CheckboxU8("Bounding Boxes", &mRenderOpts.show_bounding_boxes);
        CheckboxU8("Object Numbers", &mRenderOpts.show_object_numbers);

        ImGui::SeparatorText("VDP2 (Background)");
        CheckboxU8("NBG0 (Scroll A)", &mRenderOpts.show_layer[SE_LAYER_NBG0]);
        CheckboxU8("NBG1 (Scroll B)", &mRenderOpts.show_layer[SE_LAYER_NBG1]);
        CheckboxU8("NBG2 (Scroll C)", &mRenderOpts.show_layer[SE_LAYER_NBG2]);
        CheckboxU8("NBG3 (Scroll D)", &mRenderOpts.show_layer[SE_LAYER_NBG3]);
        CheckboxU8("RBG0 (Rotation)", &mRenderOpts.show_layer[SE_LAYER_RBG0]);
        CheckboxU8("Window", &mRenderOpts.show_window);
        CheckboxU8("Color Calculation", &mRenderOpts.show_color_calculation);
        CheckboxU8("Shadow / Highlight", &mRenderOpts.show_shadow_highlight);
    }
    ImGui::End();
}

void App::DrawVdpOutput()
{
    if (ImGui::Begin("VDP Output"))
    {
        ImGui::TextDisabled("Rendered frame (software VDP) — arrives in M3.");
    }
    ImGui::End();
}

void App::DrawCommandList()
{
    if (ImGui::Begin("VDP1 Command List"))
    {
        ImGui::TextDisabled("Command table — arrives in M2.");
    }
    ImGui::End();
}

void App::DrawSelectedObject()
{
    if (ImGui::Begin("Selected Object"))
    {
        if (mSelectedCommand < 0)
        {
            ImGui::TextDisabled("Select a sprite to inspect it.");
        }
    }
    ImGui::End();
}

void App::DrawVramMap()
{
    if (ImGui::Begin("VRAM Map (VDP1)"))
    {
        ImGui::TextDisabled("VRAM usage map — arrives in M5.");
    }
    ImGui::End();
}

void App::DrawArchiveExplorer()
{
    if (ImGui::Begin("Archive Explorer"))
    {
        ImGui::TextDisabled("Disc filesystem — arrives in M6.");
    }
    ImGui::End();
}

void App::DrawTextureViewer()
{
    if (ImGui::Begin("Texture Viewer"))
    {
        ImGui::TextDisabled("Texture inspection — arrives in M5.");
    }
    ImGui::End();
}

void App::DrawPaletteViewer()
{
    if (ImGui::Begin("Palette Viewer"))
    {
        ImGui::TextDisabled("Palette inspection — arrives in M5.");
    }
    ImGui::End();
}

void App::DrawReferences()
{
    if (ImGui::Begin("References"))
    {
        ImGui::TextDisabled("What uses this texture — arrives in M6.");
    }
    ImGui::End();
}

void App::DrawMemoryHistory()
{
    if (ImGui::Begin("Memory History"))
    {
        ImGui::TextDisabled("Load chain — arrives in M7.");
    }
    ImGui::End();
}

void App::DrawSearch()
{
    if (ImGui::Begin("Search ROM / Files"))
    {
        ImGui::TextDisabled("ROM & archive search — arrives in M6.");
    }
    ImGui::End();
}

}  // namespace sfe
