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
    mRenderOpts.show_bounding_boxes = 0;
    mRenderOpts.show_object_numbers = 0;
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
    // The frame texture is freed lazily (on next size change) or with the
    // platform's device at shutdown; mark it stale so a new dump recreates it.
    mFrameWidth = 0;
    mFrameHeight = 0;
}

// Recreate 'tex' when the target size changes; updates cached w/h. Returns the
// (possibly new) handle. Shared by the 2D and 3D render panels.
static TextureHandle EnsureTexture(IPlatform& platform, TextureHandle tex,
                                   int& curW, int& curH, int w, int h)
{
    if (w == curW && h == curH && tex != 0)
    {
        return tex;
    }
    if (tex != 0)
    {
        platform.DestroyTexture(tex);
    }
    curW = w;
    curH = h;
    return platform.CreateTexture(w, h);
}

void App::RenderFrameToTexture(IPlatform& platform)
{
    if (!mbHasData)
    {
        return;
    }

    se_image img = {};
    size_t needed = 0;
    if (se_render_frame(mContext, &mRenderOpts, &img, &needed) != SE_OK)
    {
        return;
    }
    const int w = static_cast<int>(img.width);
    const int h = static_cast<int>(img.height);
    if (w <= 0 || h <= 0)
    {
        return;
    }

    mFrameTexture = EnsureTexture(platform, mFrameTexture, mFrameWidth, mFrameHeight, w, h);
    mFrameBuffer.resize(needed);
    img.pixels = mFrameBuffer.data();
    img.capacity = mFrameBuffer.size();
    if (se_render_frame(mContext, &mRenderOpts, &img, &needed) == SE_OK && mFrameTexture != 0)
    {
        platform.UpdateTexture(mFrameTexture, mFrameBuffer.data(), w, h);
    }
}

// Finish opening: create the context around an already-opened data source.
static bool CreateContextFromSource(se_data_source& dataSource, se_context** ctxOut)
{
    se_config config;
    config.abi_version = SE_ABI_VERSION;
    config.reserved = 0;

    se_context* ctx = se_create(&dataSource, &config);
    if (!ctx)
    {
        if (dataSource.close)
        {
            dataSource.close(dataSource.user);
        }
        return false;
    }
    // Snapshot + parse + build geometry once, here. The current drivers are
    // static (savestate / dump), so there's no need to re-run it every frame.
    // A future live driver would re-call se_begin_frame per frame instead.
    se_begin_frame(ctx);
    *ctxOut = ctx;
    return true;
}

bool App::OpenFullDump(const char* path, uint32_t baseAddress)
{
    CloseData();
    se_data_source dataSource;
    if (se_savestate_open_full_dump(path, baseAddress, &dataSource) != 0)
    {
        return false;
    }
    if (!CreateContextFromSource(dataSource, &mContext))
    {
        return false;
    }
    mDataSource = dataSource;
    mbHasData = true;
    return true;
}

bool App::OpenSavestate(const char* path)
{
    CloseData();
    se_data_source dataSource;
    if (se_savestate_open_yss(path, &dataSource) != 0)
    {
        return false;
    }
    if (!CreateContextFromSource(dataSource, &mContext))
    {
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
        RenderFrameToTexture(platform);
    }

    DrawMenuBar(platform);

    // Full-viewport dockspace so every panel below is dockable.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    DrawLayerControls();
    DrawVdpOutput(platform);
    DrawWorldView(platform);
    DrawCommandList();
    DrawSelectedObject();
    DrawPlaceholder("VRAM Map (VDP1)", "VRAM usage map — arrives in M5.");
    DrawPlaceholder("Archive Explorer", "Disc filesystem — arrives in M6.");
    DrawPlaceholder("Texture Viewer", "Texture inspection — arrives in M5.");
    DrawPlaceholder("Palette Viewer", "Palette inspection — arrives in M5.");
    DrawPlaceholder("References", "What uses this texture — arrives in M6.");
    DrawPlaceholder("Memory History", "Load chain — arrives in M7.");
    DrawPlaceholder("Search ROM / Files", "ROM & archive search — arrives in M6.");
}

void App::DrawMenuBar(IPlatform& platform)
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Open Savestate (.yss)..."))
        {
            std::string path;
            if (platform.OpenFileDialog(path))
            {
                OpenSavestate(path.c_str());
            }
        }
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

void App::DrawVdpOutput(IPlatform& platform)
{
    (void)platform;
    if (ImGui::Begin("VDP Output"))
    {
        if (!mbHasData || mFrameTexture == 0)
        {
            ImGui::TextDisabled("No data loaded. File > Open Memory Dump...");
        }
        else
        {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            float scale = (mFrameWidth > 0) ? avail.x / static_cast<float>(mFrameWidth) : 1.0f;
            if (scale <= 0.0f)
            {
                scale = 1.0f;
            }
            const ImVec2 imgPos = ImGui::GetCursorScreenPos();
            const ImVec2 dispSize(mFrameWidth * scale, mFrameHeight * scale);
            ImGui::Image(mFrameTexture, dispSize);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            auto toScreen = [&](const se_vec2& c)
            {
                return ImVec2(imgPos.x + c.x * scale, imgPos.y + c.y * scale);
            };

            // Only walk the sprite list when an overlay actually needs it.
            const bool wantOverlays = mRenderOpts.show_bounding_boxes ||
                                      mRenderOpts.show_object_numbers || mSelectedCommand >= 0;
            const size_t spriteCount = wantOverlays ? se_sprite_count(mContext) : 0;
            for (size_t i = 0; i < spriteCount; ++i)
            {
                se_sprite_2d sprite;
                if (se_get_sprite_2d(mContext, i, &sprite) != SE_OK)
                {
                    continue;
                }
                const bool selected = (static_cast<int>(sprite.command_index) == mSelectedCommand);
                if (mRenderOpts.show_bounding_boxes || selected)
                {
                    const ImU32 col = selected ? IM_COL32(90, 225, 130, 255)
                                               : IM_COL32(230, 210, 60, 130);
                    dl->AddQuad(toScreen(sprite.corners[0]), toScreen(sprite.corners[1]),
                                toScreen(sprite.corners[2]), toScreen(sprite.corners[3]),
                                col, selected ? 2.0f : 1.0f);
                }
                if (mRenderOpts.show_object_numbers)
                {
                    char num[8];
                    std::snprintf(num, sizeof(num), "%u", sprite.object_number);
                    dl->AddText(toScreen(sprite.corners[0]), IM_COL32(255, 240, 80, 220), num);
                }
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                const int vx = static_cast<int>((mouse.x - imgPos.x) / scale);
                const int vy = static_cast<int>((mouse.y - imgPos.y) / scale);
                size_t hitCommand = 0;
                if (se_hit_test(mContext, vx, vy, &hitCommand) == SE_OK)
                {
                    mSelectedCommand = static_cast<int>(hitCommand);
                }
            }
        }
    }
    ImGui::End();
}

void App::DrawWorldView(IPlatform& platform)
{
    if (ImGui::Begin("3D View"))
    {
        if (!mbHasData)
        {
            ImGui::TextDisabled("No data loaded. Drag to orbit, wheel to zoom.");
        }
        else
        {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const int vw = static_cast<int>(avail.x);
            const int vh = static_cast<int>(avail.y);
            if (vw > 16 && vh > 16)
            {
                m3dTexture = EnsureTexture(platform, m3dTexture, m3dWidth, m3dHeight, vw, vh);

                se_camera3d cam = {};
                cam.yaw = mYaw;
                cam.pitch = mPitch;
                cam.distance = mDistance;
                cam.fov = vh * 1.1f;
                cam.viewport_width = static_cast<uint32_t>(vw);
                cam.viewport_height = static_cast<uint32_t>(vh);

                se_image img = {};
                size_t needed = 0;
                se_render_3d(mContext, &cam, &mRenderOpts, &img, &needed);
                m3dBuffer.resize(needed);
                img.pixels = m3dBuffer.data();
                img.capacity = m3dBuffer.size();
                if (se_render_3d(mContext, &cam, &mRenderOpts, &img, &needed) == SE_OK &&
                    m3dTexture != 0)
                {
                    platform.UpdateTexture(m3dTexture, m3dBuffer.data(), vw, vh);
                }

                ImGui::Image(m3dTexture, ImVec2(static_cast<float>(vw), static_cast<float>(vh)));

                if (ImGui::IsItemHovered())
                {
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                    {
                        const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                        mYaw += drag.x * 0.01f;
                        mPitch += drag.y * 0.01f;
                        if (mPitch > 1.5f) mPitch = 1.5f;
                        if (mPitch < -1.5f) mPitch = -1.5f;
                        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                    }
                    const float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0f)
                    {
                        mDistance *= (1.0f - wheel * 0.1f);
                        if (mDistance < 50.0f) mDistance = 50.0f;
                    }
                }
            }
        }
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

// Panels whose content lands in a later milestone: an empty dock slot with a
// note. One helper instead of seven near-identical functions.
void App::DrawPlaceholder(const char* title, const char* note)
{
    if (ImGui::Begin(title))
    {
        ImGui::TextDisabled("%s", note);
    }
    ImGui::End();
}

}  // namespace sfe
