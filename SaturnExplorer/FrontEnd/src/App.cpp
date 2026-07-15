#include "App.h"

#include <cstdarg>
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

const char* CommandTypeName(se_command_type type)
{
    switch (type)
    {
    case SE_CMD_NORMAL_SPRITE:    return "Normal";
    case SE_CMD_SCALED_SPRITE:    return "Scaled";
    case SE_CMD_DISTORTED_SPRITE: return "Distorted";
    case SE_CMD_POLYGON:          return "Polygon";
    case SE_CMD_POLYLINE:         return "Polyline";
    case SE_CMD_LINE:             return "Line";
    case SE_CMD_USER_CLIP:        return "User Clip";
    case SE_CMD_SYSTEM_CLIP:      return "System Clip";
    case SE_CMD_LOCAL_COORD:      return "Local Coord";
    default:                      return "Unknown";
    }
}

const char* ColorModeName(se_color_mode mode)
{
    switch (mode)
    {
    case SE_COLOR_BANK_16:  return "16 (bank)";
    case SE_COLOR_LUT_16:   return "16 (LUT)";
    case SE_COLOR_BANK_64:  return "64 (bank)";
    case SE_COLOR_BANK_128: return "128 (bank)";
    case SE_COLOR_BANK_256: return "256 (bank)";
    case SE_COLOR_RGB555:   return "RGB555";
    default:                return "?";
    }
}

const char* DrawModeName(se_draw_mode mode)
{
    switch (mode)
    {
    case SE_DRAW_NORMAL:     return "Normal";
    case SE_DRAW_MESH:       return "Mesh";
    case SE_DRAW_SHADOW:     return "Shadow";
    case SE_DRAW_HALF_LUM:   return "Half Luminance";
    case SE_DRAW_HALF_TRANS: return "Half Transparent";
    default:                 return "?";
    }
}

// One "Label: value" row in the inspector.
void InspectorRow(const char* label, const char* fmt, ...)
{
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(160.0f);
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
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
        if (!mbHasData)
        {
            ImGui::TextDisabled("No data loaded. File > Open Memory Dump...");
        }
        else
        {
            const size_t count = se_command_count(mContext);
            ImGui::Text("%zu commands", count);

            const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
            if (ImGui::BeginTable("commands", 6, flags))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("#");
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Size");
                ImGui::TableSetupColumn("Position");
                ImGui::TableSetupColumn("Color");
                ImGui::TableSetupColumn("Tex Addr");
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(count));
                while (clipper.Step())
                {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                    {
                        se_command cmd;
                        if (se_get_command(mContext, static_cast<size_t>(row), &cmd) != SE_OK)
                        {
                            continue;
                        }

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        char label[16];
                        std::snprintf(label, sizeof(label), "%d", row);
                        if (ImGui::Selectable(label, mSelectedCommand == row,
                                              ImGuiSelectableFlags_SpanAllColumns))
                        {
                            mSelectedCommand = row;
                        }
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(CommandTypeName(cmd.type));
                        ImGui::TableNextColumn();
                        ImGui::Text("%ux%u", cmd.width, cmd.height);
                        ImGui::TableNextColumn();
                        ImGui::Text("(%d, %d)", cmd.x, cmd.y);
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(ColorModeName(cmd.color_mode));
                        ImGui::TableNextColumn();
                        ImGui::Text("%06X", cmd.texture_address);
                    }
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
}

void App::DrawSelectedObject()
{
    if (ImGui::Begin("Selected Object"))
    {
        se_command cmd;
        if (!mbHasData || mSelectedCommand < 0 ||
            se_get_command(mContext, static_cast<size_t>(mSelectedCommand), &cmd) != SE_OK)
        {
            ImGui::TextDisabled("Select a command to inspect it.");
        }
        else
        {
            ImGui::Text("Command #%u  (%s)", cmd.index, CommandTypeName(cmd.type));
            ImGui::Separator();
            InspectorRow("Table Address", "0x%06X", cmd.table_address);
            InspectorRow("Link Address", "0x%06X", cmd.link_address);
            InspectorRow("Texture Address", "0x%06X", cmd.texture_address);
            if (cmd.color_mode == SE_COLOR_LUT_16)
            {
                InspectorRow("CLUT Address", "0x%06X", cmd.clut_address);
            }
            else
            {
                InspectorRow("Palette Bank", "%u", cmd.palette_bank);
            }
            InspectorRow("Size", "%u x %u", cmd.width, cmd.height);
            InspectorRow("Position", "(%d, %d)", cmd.x, cmd.y);
            InspectorRow("Color Mode", "%s", ColorModeName(cmd.color_mode));
            InspectorRow("Draw Mode", "%s", DrawModeName(cmd.draw_mode));
            InspectorRow("Transparency", "%s",
                         cmd.transparency == SE_TRANSP_PER_PIXEL ? "Per Pixel" : "None");
            InspectorRow("Gouraud", "%s", cmd.gouraud ? "On" : "Off");
            InspectorRow("Color Calc", "%s", cmd.color_calc ? "On" : "Off");
            InspectorRow("Flip", "%s%s%s", cmd.flip_x ? "H " : "", cmd.flip_y ? "V" : "",
                         (!cmd.flip_x && !cmd.flip_y) ? "None" : "");
            InspectorRow("CMDCTRL", "0x%04X", cmd.raw_cmdctrl);
            InspectorRow("CMDPMOD", "0x%04X", cmd.raw_cmdpmod);
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
