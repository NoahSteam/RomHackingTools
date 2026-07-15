#include "App.h"

#include <cstdarg>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"  // DockBuilder + BeginViewportSideBar for the default layout

#include "Platform/IPlatform.h"
#include "SavestateDriver.h"

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
    // Generic entry: dispatches by file magic to the right emulator's parser.
    if (se_savestate_open(path, &dataSource) != 0)
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

    // Top toolbar + bottom status bar reserve space from the viewport; the
    // dockspace fills what's left.
    DrawToolbar(platform);
    DrawStatusBar();

    const ImGuiID dockId = ImGui::DockSpaceOverViewport(
        0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    // Arrange the panels into the concept's three-column layout the first time
    // (unless the user already has a saved layout in imgui.ini).
    if (!mbLayoutBuilt)
    {
        mbLayoutBuilt = true;
        const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockId);
        if (node == nullptr || node->IsLeafNode())
        {
            BuildDefaultLayout(dockId);
        }
    }

    // Left column.
    DrawLayerControls();
    DrawVramMap();
    DrawPlaceholder("Archive Explorer", "Disc filesystem tree — arrives in M6 (needs disc access).");
    DrawPlaceholder("Search ROM / Files", "ROM & archive search — arrives in M6 (needs disc access).");

    // Center: VDP Output and its sibling tabs, then the command list, then the
    // texture/palette/reference row.
    DrawVdpOutput(platform);
    DrawWorldView(platform);
    DrawVdp1Table();
    DrawVdp2Table();
    DrawColorRam();
    DrawPlaceholder("Palette RAM", "VDP1 CLUT-area view — planned (see Color RAM for CRAM).");
    DrawRegisters();
    DrawCommandList();
    DrawTextureViewer(platform);
    DrawPaletteViewer();
    DrawReferences();

    // Right column.
    DrawSelectedObject();
    DrawPlaceholder("Texture Preview", "Preview of the selected sprite's texture — see the Texture Viewer panel.");
    DrawPlaceholder("Palette (CLUT)", "Palette of the selected sprite — see the Palette Viewer panel.");
    DrawPlaceholder("Memory History", "Load chain (File → CD → DMA → Write) — arrives in M7.");
}

// Programmatic default dock layout matching the concept: a narrow left column,
// a wide center, and a right inspector column. Runs once on first launch (or
// after the layout is reset); the user's own rearrangements are saved by ImGui
// to imgui.ini and take precedence on later runs.
void App::BuildDefaultLayout(unsigned int dockspaceId)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID centerRight = dockspaceId;
    const ImGuiID left = ImGui::DockBuilderSplitNode(centerRight, ImGuiDir_Left, 0.20f, nullptr, &centerRight);
    ImGuiID center = centerRight;
    const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);

    // Left column, top to bottom.
    ImGuiID leftRest = left;
    const ImGuiID leftTop  = ImGui::DockBuilderSplitNode(leftRest, ImGuiDir_Up, 0.30f, nullptr, &leftRest);
    const ImGuiID leftVram = ImGui::DockBuilderSplitNode(leftRest, ImGuiDir_Up, 0.35f, nullptr, &leftRest);
    const ImGuiID leftArch = ImGui::DockBuilderSplitNode(leftRest, ImGuiDir_Up, 0.50f, nullptr, &leftRest);
    const ImGuiID leftSearch = leftRest;

    // Center column: output (top), command list (middle), tex/pal/refs (bottom).
    ImGuiID centerRest = center;
    const ImGuiID centerTop = ImGui::DockBuilderSplitNode(centerRest, ImGuiDir_Up, 0.55f, nullptr, &centerRest);
    const ImGuiID centerMid = ImGui::DockBuilderSplitNode(centerRest, ImGuiDir_Up, 0.45f, nullptr, &centerRest);
    ImGuiID bottomRest = centerRest;
    const ImGuiID cbTex = ImGui::DockBuilderSplitNode(bottomRest, ImGuiDir_Left, 0.34f, nullptr, &bottomRest);
    const ImGuiID cbPal = ImGui::DockBuilderSplitNode(bottomRest, ImGuiDir_Left, 0.50f, nullptr, &bottomRest);
    const ImGuiID cbRefs = bottomRest;

    // Right inspector column, top to bottom.
    ImGuiID rightRest = right;
    const ImGuiID rObj  = ImGui::DockBuilderSplitNode(rightRest, ImGuiDir_Up, 0.45f, nullptr, &rightRest);
    const ImGuiID rTex  = ImGui::DockBuilderSplitNode(rightRest, ImGuiDir_Up, 0.25f, nullptr, &rightRest);
    const ImGuiID rPal  = ImGui::DockBuilderSplitNode(rightRest, ImGuiDir_Up, 0.40f, nullptr, &rightRest);
    const ImGuiID rMem  = rightRest;

    ImGui::DockBuilderDockWindow("Layer Controls", leftTop);
    ImGui::DockBuilderDockWindow("VRAM Map (VDP1)", leftVram);
    ImGui::DockBuilderDockWindow("Archive Explorer", leftArch);
    ImGui::DockBuilderDockWindow("Search ROM / Files", leftSearch);

    // These share centerTop, so they appear as tabs (VDP Output | VDP1 Table | ...).
    ImGui::DockBuilderDockWindow("VDP Output", centerTop);
    ImGui::DockBuilderDockWindow("VDP1 Table", centerTop);
    ImGui::DockBuilderDockWindow("VDP2 Table", centerTop);
    ImGui::DockBuilderDockWindow("Color RAM", centerTop);
    ImGui::DockBuilderDockWindow("Palette RAM", centerTop);
    ImGui::DockBuilderDockWindow("Registers", centerTop);
    ImGui::DockBuilderDockWindow("3D View", centerTop);
    ImGui::DockBuilderDockWindow("VDP1 Command List", centerMid);
    ImGui::DockBuilderDockWindow("Texture Viewer", cbTex);
    ImGui::DockBuilderDockWindow("Palette Viewer", cbPal);
    ImGui::DockBuilderDockWindow("References", cbRefs);

    ImGui::DockBuilderDockWindow("Selected Object", rObj);
    ImGui::DockBuilderDockWindow("Texture Preview", rTex);
    ImGui::DockBuilderDockWindow("Palette (CLUT)", rPal);
    ImGui::DockBuilderDockWindow("Memory History", rMem);

    ImGui::DockBuilderFinish(dockspaceId);
}

void App::DrawToolbar(IPlatform& platform)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginViewportSideBar("##Toolbar", vp, ImGuiDir_Up, height, flags))
    {
        // Open ROM (savestate) + a dropdown for the other file actions.
        if (ImGui::Button("Open ROM"))
        {
            std::string path;
            if (platform.OpenFileDialog(path))
            {
                OpenSavestate(path.c_str());
            }
        }
        ImGui::SameLine(0.0f, 1.0f);
        if (ImGui::ArrowButton("##openmenu", ImGuiDir_Down))
        {
            ImGui::OpenPopup("OpenMenu");
        }
        if (ImGui::BeginPopup("OpenMenu"))
        {
            if (ImGui::MenuItem("Open Savestate (.yss / Mednafen)..."))
            {
                std::string path;
                if (platform.OpenFileDialog(path)) OpenSavestate(path.c_str());
            }
            if (ImGui::MenuItem("Open Memory Dump..."))
            {
                std::string path;
                if (platform.OpenFileDialog(path)) OpenFullDump(path.c_str(), 0x00000000u);
            }
            if (ImGui::MenuItem("Close", nullptr, false, mbHasData)) CloseData();
            ImGui::EndPopup();
        }

        // Playback / stepping — stubbed until a live driver exists (M7).
        ImGui::SameLine();
        ImGui::BeginDisabled(true);
        ImGui::Button("Pause"); ImGui::SameLine();
        ImGui::Button("Step");  ImGui::SameLine();
        ImGui::Button("Step Frame"); ImGui::SameLine();
        ImGui::Button("|<"); ImGui::SameLine(); ImGui::Button("<"); ImGui::SameLine();
        ImGui::Button(">"); ImGui::SameLine(); ImGui::Button(">|"); ImGui::SameLine();
        ImGui::EndDisabled();

        // Not-yet-implemented tools — visible but disabled.
        ImGui::SameLine();
        ImGui::BeginDisabled(true);
        ImGui::Button("Bookmarks"); ImGui::SameLine();
        ImGui::Button("Compare");   ImGui::SameLine();
        ImGui::Button("Screenshot"); ImGui::SameLine();
        ImGui::Button("Settings");  ImGui::SameLine();
        ImGui::Button("Help");
        ImGui::EndDisabled();

        // Right-aligned live counters.
        char info[96];
        std::snprintf(info, sizeof(info), "FPS %.1f   |   VDP1: %zu objs   |   VDP2 regs: %s",
                      ImGui::GetIO().Framerate,
                      mbHasData ? se_sprite_count(mContext) : 0,
                      mbHasData ? "loaded" : "-");
        const float w = ImGui::CalcTextSize(info).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - w - 12.0f);
        ImGui::TextUnformatted(info);
    }
    ImGui::End();
}

void App::DrawStatusBar()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##StatusBar", vp, ImGuiDir_Down, height, flags))
    {
        if (ImGui::BeginMenuBar())
        {
            if (mbHasData)
            {
                ImGui::Text("Loaded");
                ImGui::Separator();
                ImGui::Text("Commands: %zu", se_command_count(mContext));
                ImGui::Separator();
                ImGui::Text("Sprites: %zu", se_sprite_count(mContext));
                ImGui::Separator();
                ImGui::Text("VRAM regions: %zu", se_vram_region_count(mContext));
            }
            else
            {
                ImGui::TextDisabled("No data loaded — Open ROM to begin.");
            }
            // Live-only fields (frame / blanks / SH-2 PC / busy) arrive with the
            // M7 live driver; shown here as placeholders to mirror the concept.
            char right[128];
            std::snprintf(right, sizeof(right), "Frame: -   |   Line: -   |   SH-2 PC: -   |   VDP1: -   VDP2: -");
            const float w = ImGui::CalcTextSize(right).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - w - 12.0f);
            ImGui::TextDisabled("%s", right);
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
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

// Build a texture reference straight from a parsed command's texture fields.
static se_texture_ref TextureRefOf(const se_command& cmd)
{
    se_texture_ref ref = {};
    ref.vram_address = cmd.texture_address;
    ref.width = cmd.width;
    ref.height = cmd.height;
    ref.color_mode = cmd.color_mode;
    ref.clut_address = cmd.clut_address;
    ref.palette_bank = cmd.palette_bank;
    return ref;
}

// Draw a checkerboard behind an image rect so transparent texels read clearly.
static void Checkerboard(ImVec2 topLeft, ImVec2 size, float cell)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int cols = static_cast<int>(size.x / cell) + 1;
    const int rows = static_cast<int>(size.y / cell) + 1;
    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            const ImU32 tone = ((r ^ c) & 1) ? IM_COL32(70, 70, 78, 255)
                                             : IM_COL32(48, 48, 54, 255);
            const ImVec2 a(topLeft.x + c * cell, topLeft.y + r * cell);
            ImVec2 b(a.x + cell, a.y + cell);
            if (b.x > topLeft.x + size.x) b.x = topLeft.x + size.x;
            if (b.y > topLeft.y + size.y) b.y = topLeft.y + size.y;
            dl->AddRectFilled(a, b, tone);
        }
    }
}

void App::DrawTextureViewer(IPlatform& platform)
{
    if (ImGui::Begin("Texture Viewer"))
    {
        se_command cmd;
        const bool haveCmd = mbHasData && mSelectedCommand >= 0 &&
            se_get_command(mContext, static_cast<size_t>(mSelectedCommand), &cmd) == SE_OK;
        const bool textured = haveCmd &&
            (cmd.type == SE_CMD_NORMAL_SPRITE || cmd.type == SE_CMD_SCALED_SPRITE ||
             cmd.type == SE_CMD_DISTORTED_SPRITE) && cmd.width > 0 && cmd.height > 0;

        if (!haveCmd)
        {
            ImGui::TextDisabled("Select a sprite (Command List) to view its texture.");
        }
        else if (!textured)
        {
            ImGui::TextDisabled("Command #%u has no texture.", cmd.index);
        }
        else
        {
            const se_texture_ref ref = TextureRefOf(cmd);
            se_image img = {};
            size_t needed = 0;
            se_result r = se_decode_texture(mContext, &ref, &img, &needed);
            if (r == SE_OK)
            {
                const int w = static_cast<int>(img.width);
                const int h = static_cast<int>(img.height);
                mTexTexture = EnsureTexture(platform, mTexTexture, mTexWidth, mTexHeight, w, h);
                mTexBuffer.resize(needed);
                img.pixels = mTexBuffer.data();
                img.capacity = mTexBuffer.size();
                if (se_decode_texture(mContext, &ref, &img, &needed) == SE_OK && mTexTexture != 0)
                {
                    platform.UpdateTexture(mTexTexture, mTexBuffer.data(), w, h);
                }

                ImGui::Text("%d x %d   %s   @0x%06X", w, h,
                            ColorModeName(cmd.color_mode), cmd.texture_address);

                // Integer zoom to roughly fill the panel width (crisp-ish, 1..16x).
                int zoom = (w > 0) ? static_cast<int>(ImGui::GetContentRegionAvail().x) / w : 1;
                if (zoom < 1) zoom = 1;
                if (zoom > 16) zoom = 16;
                const ImVec2 dispSize(static_cast<float>(w * zoom), static_cast<float>(h * zoom));
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                Checkerboard(pos, dispSize, 8.0f);
                ImGui::Image(mTexTexture, dispSize);
            }
            else
            {
                ImGui::TextDisabled("Texture decode failed (%d).", r);
            }
        }
    }
    ImGui::End();
}

// Draw a grid of palette swatches with per-swatch hover (index / raw / RGB).
// Swatch size adapts so a 256-colour bank still fits.
void App::DrawPaletteSwatches(const se_palette& pal)
{
    if (pal.count == 0)
    {
        ImGui::TextDisabled("  (empty)");
        return;
    }
    const int cols = (pal.count <= 16) ? 8 : 16;
    const float sw = (pal.count <= 16) ? 26.0f : (pal.count <= 64 ? 18.0f : 12.0f);
    const int rows = (pal.count + cols - 1) / cols;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    for (uint16_t i = 0; i < pal.count; ++i)
    {
        const int cx = i % cols;
        const int cy = i / cols;
        const ImVec2 a(origin.x + cx * sw, origin.y + cy * sw);
        const ImVec2 b(a.x + sw - 2.0f, a.y + sw - 2.0f);
        const se_palette_entry& e = pal.entries[i];
        dl->AddRectFilled(a, b, IM_COL32(e.r, e.g, e.b, 255));
        dl->AddRect(a, b, IM_COL32(0, 0, 0, 110));
    }

    // Reserve the grid area so a hovered swatch can show its details.
    ImGui::InvisibleButton("swatches", ImVec2(cols * sw, rows * sw));
    if (ImGui::IsItemHovered())
    {
        const ImVec2 m = ImGui::GetMousePos();
        const int cx = static_cast<int>((m.x - origin.x) / sw);
        const int cy = static_cast<int>((m.y - origin.y) / sw);
        const int idx = cy * cols + cx;
        if (cx >= 0 && cx < cols && idx >= 0 && idx < pal.count)
        {
            const se_palette_entry& e = pal.entries[idx];
            ImGui::BeginTooltip();
            ImGui::Text("#%d   raw 0x%04X", idx, e.raw);
            ImGui::Text("RGB  %d, %d, %d", e.r, e.g, e.b);
            ImGui::EndTooltip();
        }
    }
}

void App::DrawPaletteViewer()
{
    if (ImGui::Begin("Palette Viewer"))
    {
        se_command cmd;
        const bool haveCmd = mbHasData && mSelectedCommand >= 0 &&
            se_get_command(mContext, static_cast<size_t>(mSelectedCommand), &cmd) == SE_OK;

        if (!haveCmd)
        {
            ImGui::TextDisabled("Select a sprite to view its palette.");
        }
        else if (cmd.color_mode == SE_COLOR_RGB555)
        {
            ImGui::TextDisabled("Command #%u is direct RGB555 — no palette.", cmd.index);
        }
        else
        {
            se_palette pal = {};
            se_result r;
            if (cmd.color_mode == SE_COLOR_LUT_16)
            {
                r = se_decode_palette(mContext, cmd.clut_address, &pal);
                if (r == SE_OK)
                {
                    ImGui::Text("CLUT @0x%06X  —  %u entries", pal.clut_address, pal.count);
                }
            }
            else
            {
                // Color-bank modes read a sub-palette of CRAM (16/64/128/256 entries).
                r = se_decode_bank_palette(mContext, cmd.palette_bank, cmd.color_mode, &pal);
                if (r == SE_OK)
                {
                    ImGui::Text("CRAM bank 0x%X  —  %s  —  %u entries",
                                cmd.palette_bank, ColorModeName(cmd.color_mode), pal.count);
                }
            }

            if (r == SE_OK)
            {
                DrawPaletteSwatches(pal);
            }
            else
            {
                ImGui::TextDisabled("Palette decode failed (%d).", r);
            }
        }
    }
    ImGui::End();
}

static const char* VramKindName(se_vram_region_kind k)
{
    switch (k)
    {
    case SE_VRAM_TEXTURE:   return "Texture";
    case SE_VRAM_CLUT:      return "CLUT";
    case SE_VRAM_CMD_TABLE: return "Command Table";
    case SE_VRAM_GOURAUD:   return "Gouraud Table";
    default:                return "Other";
    }
}

void App::DrawVramMap()
{
    if (ImGui::Begin("VRAM Map (VDP1)"))
    {
        if (!mbHasData)
        {
            ImGui::TextDisabled("No data loaded.");
        }
        else
        {
            const size_t count = se_vram_region_count(mContext);
            ImGui::Text("%zu regions in 512 KiB VDP1 VRAM", count);

            // Colour by region kind; legend mirrors these.
            auto kindColor = [](se_vram_region_kind k) -> ImU32
            {
                switch (k)
                {
                case SE_VRAM_TEXTURE:   return IM_COL32(90, 190, 120, 255);
                case SE_VRAM_CLUT:      return IM_COL32(220, 200, 90, 255);
                case SE_VRAM_CMD_TABLE: return IM_COL32(100, 150, 230, 255);
                case SE_VRAM_GOURAUD:   return IM_COL32(200, 120, 210, 255);
                default:                return IM_COL32(150, 150, 150, 255);
                }
            };

            const uint32_t kVramSize = 0x80000;   // 512 KiB
            const int rows = 16;                    // each row = 32 KiB
            const uint32_t perRow = kVramSize / rows;
            const float width = ImGui::GetContentRegionAvail().x;
            const float rowH = 12.0f;
            const ImVec2 origin = ImGui::GetCursorScreenPos();

            // Reserve the map area up front so it can be hovered/clicked; the bars
            // are painted into it with the draw list.
            ImGui::InvisibleButton("vrammap", ImVec2(width, rows * rowH));
            const bool mapHovered = ImGui::IsItemHovered();
            const bool mapClicked = ImGui::IsItemClicked();

            // Byte address under the cursor, for the hover hit-test below.
            uint32_t hoverAddr = 0;
            bool hoverValid = false;
            if (mapHovered)
            {
                const ImVec2 m = ImGui::GetMousePos();
                const int row = static_cast<int>((m.y - origin.y) / rowH);
                if (row >= 0 && row < rows)
                {
                    float fx = (m.x - origin.x) / width;
                    if (fx < 0.0f) fx = 0.0f;
                    if (fx > 0.999f) fx = 0.999f;
                    hoverAddr = static_cast<uint32_t>(row * perRow + fx * perRow);
                    hoverValid = true;
                }
            }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + rows * rowH),
                              IM_COL32(24, 24, 30, 255));

            se_vram_region hoveredReg = {};
            bool haveHover = false;
            for (size_t i = 0; i < count; ++i)
            {
                se_vram_region reg;
                if (se_get_vram_region(mContext, i, &reg) != SE_OK)
                {
                    continue;
                }
                const uint32_t end = reg.address + (reg.size ? reg.size : 1);
                const ImU32 col = kindColor(reg.kind);
                // A region can straddle rows; paint it row by row.
                uint32_t a = reg.address;
                while (a < end && a < kVramSize)
                {
                    const int row = static_cast<int>(a / perRow);
                    const uint32_t rowEnd = (row + 1) * perRow;
                    const uint32_t b = end < rowEnd ? end : rowEnd;
                    const float x0 = origin.x + (a % perRow) / static_cast<float>(perRow) * width;
                    const float x1 = origin.x + ((b - 1) % perRow + 1) / static_cast<float>(perRow) * width;
                    const float y0 = origin.y + row * rowH;
                    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + rowH - 1.0f), col);
                    a = b;
                }
                if (hoverValid && !haveHover && hoverAddr >= reg.address && hoverAddr < end)
                {
                    hoveredReg = reg;
                    haveHover = true;
                }
            }

            if (haveHover)
            {
                ImGui::BeginTooltip();
                ImGui::Text("0x%05X   (%u bytes)", hoveredReg.address, hoveredReg.size);
                ImGui::TextUnformatted(VramKindName(hoveredReg.kind));
                if (hoveredReg.ref_index != 0xFFFFFFFFu)
                {
                    ImGui::Text("Command #%u", hoveredReg.ref_index);
                }
                ImGui::EndTooltip();
                if (mapClicked && hoveredReg.ref_index != 0xFFFFFFFFu)
                {
                    mSelectedCommand = static_cast<int>(hoveredReg.ref_index);
                }
            }
            ImGui::Spacing();

            // Legend.
            auto swatch = [&](const char* name, se_vram_region_kind k)
            {
                ImDrawList* d = ImGui::GetWindowDrawList();
                const ImVec2 p = ImGui::GetCursorScreenPos();
                d->AddRectFilled(p, ImVec2(p.x + 12, p.y + 12), kindColor(k));
                ImGui::Dummy(ImVec2(16, 12));
                ImGui::SameLine();
                ImGui::TextUnformatted(name);
            };
            swatch("Texture", SE_VRAM_TEXTURE); ImGui::SameLine(0.0f, 16.0f);
            swatch("CLUT", SE_VRAM_CLUT);       ImGui::SameLine(0.0f, 16.0f);
            swatch("Cmd Table", SE_VRAM_CMD_TABLE); ImGui::SameLine(0.0f, 16.0f);
            swatch("Gouraud", SE_VRAM_GOURAUD);
        }
    }
    ImGui::End();
}

// One scrollable, click-to-select table of sprite references (used for both the
// texture and palette reference lists). Clicking a row selects that command.
void App::DrawReferenceList(const char* id, const std::vector<se_reference>& refs)
{
    if (refs.empty())
    {
        ImGui::TextDisabled("  (none)");
        return;
    }
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY;
    const size_t visRows = refs.size() < 8 ? refs.size() + 1 : 8;
    const ImVec2 size(0.0f, ImGui::GetTextLineHeightWithSpacing() * visRows);
    if (ImGui::BeginTable(id, 4, flags, size))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Cmd");
        ImGui::TableSetupColumn("Obj");
        ImGui::TableSetupColumn("Pos");
        ImGui::TableSetupColumn("Size");
        ImGui::TableHeadersRow();

        for (const se_reference& r : refs)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char label[24];
            std::snprintf(label, sizeof(label), "%u", r.command_index);
            const bool selected = (mSelectedCommand == static_cast<int>(r.command_index));
            if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns))
            {
                mSelectedCommand = static_cast<int>(r.command_index);
            }
            ImGui::TableNextColumn();
            ImGui::Text("%u", r.object_number);
            ImGui::TableNextColumn();
            ImGui::Text("(%d, %d)", r.x, r.y);
            ImGui::TableNextColumn();
            ImGui::Text("%u x %u", r.width, r.height);
        }
        ImGui::EndTable();
    }
}

void App::DrawReferences()
{
    if (ImGui::Begin("References"))
    {
        se_command cmd;
        if (!mbHasData || mSelectedCommand < 0 ||
            se_get_command(mContext, static_cast<size_t>(mSelectedCommand), &cmd) != SE_OK)
        {
            ImGui::TextDisabled("Select a sprite to see what shares its texture or palette.");
        }
        else
        {
            // Sprites sharing this texture.
            const se_texture_ref ref = TextureRefOf(cmd);
            const size_t texTotal = se_references_of_texture(mContext, &ref, nullptr, 0);
            std::vector<se_reference> texRefs(texTotal);
            if (texTotal > 0)
            {
                se_references_of_texture(mContext, &ref, texRefs.data(), texRefs.size());
            }
            ImGui::SeparatorText("Shares this texture");
            ImGui::Text("Texture @0x%06X  —  %zu sprite(s)", cmd.texture_address, texTotal);
            DrawReferenceList("texrefs", texRefs);

            // Sprites sharing this palette (LUT mode only; bank palettes have no CLUT).
            if (cmd.color_mode == SE_COLOR_LUT_16)
            {
                const size_t palTotal = se_references_of_palette(mContext, cmd.clut_address, nullptr, 0);
                std::vector<se_reference> palRefs(palTotal);
                if (palTotal > 0)
                {
                    se_references_of_palette(mContext, cmd.clut_address, palRefs.data(), palRefs.size());
                }
                ImGui::SeparatorText("Shares this palette");
                ImGui::Text("CLUT @0x%06X  —  %zu sprite(s)", cmd.clut_address, palTotal);
                DrawReferenceList("palrefs", palRefs);
            }
        }
    }
    ImGui::End();
}

namespace
{
struct RegInfo { uint32_t off; const char* name; };

// Curated VDP2 registers (hardware byte offset -> name). Covers display, layer
// enable, character/pattern control, plane/map, scroll, priorities, and color
// offsets — the fields a background-layer investigation reaches for.
const RegInfo kVdp2Regs[] = {
    {0x000,"TVMD"},{0x002,"EXTEN"},{0x004,"TVSTAT"},{0x006,"VRSIZE"},{0x00E,"RAMCTL"},
    {0x020,"BGON"},{0x028,"CHCTLA"},{0x02A,"CHCTLB"},
    {0x030,"PNCN0"},{0x032,"PNCN1"},{0x034,"PNCN2"},{0x036,"PNCN3"},{0x03A,"PLSZ"},
    {0x03C,"MPOFN"},{0x03E,"MPOFR"},
    {0x040,"MPABN0"},{0x042,"MPCDN0"},{0x044,"MPABN1"},{0x046,"MPCDN1"},
    {0x048,"MPABN2"},{0x04A,"MPCDN2"},{0x04C,"MPABN3"},{0x04E,"MPCDN3"},
    {0x070,"SCXIN0"},{0x074,"SCYIN0"},{0x080,"SCXIN1"},{0x084,"SCYIN1"},
    {0x090,"SCXN2"},{0x092,"SCYN2"},{0x094,"SCXN3"},{0x096,"SCYN3"},
    {0x0E0,"SPCTL"},{0x0E4,"CRAOFA"},{0x0E6,"CRAOFB"},{0x0EC,"CCCTL"},
    {0x0F0,"PRISA"},{0x0F2,"PRISB"},{0x0F4,"PRISC"},{0x0F6,"PRISD"},
    {0x0F8,"PRINA"},{0x0FA,"PRINB"},{0x0FC,"PRIR"},
    {0x100,"CCRSA"},{0x108,"CCRNA"},{0x10A,"CCRNB"},
};

// VDP1 control/status registers.
const RegInfo kVdp1Regs[] = {
    {0x00,"TVMR"},{0x02,"FBCR"},{0x04,"PTMR"},{0x06,"EWDR"},{0x08,"EWLR"},
    {0x0A,"EWRR"},{0x0C,"ENDR"},{0x10,"EDSR"},{0x12,"LOPR"},{0x14,"COPR"},{0x16,"MODR"},
};

void DrawRegTable(const char* id, const RegInfo* regs, size_t count,
                  uint16_t (*read)(se_context*, uint32_t), se_context* ctx)
{
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable(id, 3, flags, ImVec2(0, 0)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Reg");
        ImGui::TableSetupColumn("Addr");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < count; ++i)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(regs[i].name);
            ImGui::TableNextColumn();
            ImGui::Text("0x%03X", regs[i].off);
            ImGui::TableNextColumn();
            ImGui::Text("0x%04X", read(ctx, regs[i].off));
        }
        ImGui::EndTable();
    }
}
}  // namespace

void App::DrawRegisters()
{
    if (ImGui::Begin("Registers"))
    {
        if (!mbHasData)
        {
            ImGui::TextDisabled("No data loaded.");
        }
        else if (ImGui::BeginTabBar("regtabs"))
        {
            if (ImGui::BeginTabItem("VDP2"))
            {
                if (se_has_vdp2_registers(mContext))
                {
                    DrawRegTable("vdp2regs", kVdp2Regs,
                                 sizeof(kVdp2Regs) / sizeof(kVdp2Regs[0]),
                                 se_get_vdp2_register, mContext);
                }
                else
                {
                    ImGui::TextDisabled("This source doesn't provide VDP2 registers.");
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("VDP1"))
            {
                if (se_has_vdp1_registers(mContext))
                {
                    DrawRegTable("vdp1regs", kVdp1Regs,
                                 sizeof(kVdp1Regs) / sizeof(kVdp1Regs[0]),
                                 se_get_vdp1_register, mContext);
                }
                else
                {
                    ImGui::TextDisabled("This source doesn't provide VDP1 registers");
                    ImGui::TextDisabled("(most VDP1 draw state lives in the command table).");
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void App::DrawColorRam()
{
    if (ImGui::Begin("Color RAM"))
    {
        if (!mbHasData)
        {
            ImGui::TextDisabled("No data loaded.");
        }
        else
        {
            mCramColors.resize(2048);
            const size_t n = se_read_cram_colors(mContext, 0,
                                                 static_cast<uint16_t>(mCramColors.size()),
                                                 mCramColors.data());
            const se_cram_mode mode = se_get_cram_mode(mContext);
            const bool rgb888 = (mode == SE_CRAM_RGB888_1024);
            ImGui::Text("%zu CRAM entries  (%s)", n,
                        rgb888 ? "RGB888" : "RGB555");
            const int cols = 32;
            const float sw = 12.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            for (size_t i = 0; i < n; ++i)
            {
                const int cx = static_cast<int>(i) % cols;
                const int cy = static_cast<int>(i) / cols;
                const ImVec2 a(origin.x + cx * sw, origin.y + cy * sw);
                const ImVec2 b(a.x + sw - 1.0f, a.y + sw - 1.0f);
                const se_palette_entry& e = mCramColors[i];
                dl->AddRectFilled(a, b, IM_COL32(e.r, e.g, e.b, 255));
            }
            const int rows = (static_cast<int>(n) + cols - 1) / cols;
            ImGui::InvisibleButton("cram", ImVec2(cols * sw, rows * sw));
            if (ImGui::IsItemHovered())
            {
                const ImVec2 m = ImGui::GetMousePos();
                const int cx = static_cast<int>((m.x - origin.x) / sw);
                const int cy = static_cast<int>((m.y - origin.y) / sw);
                const int idx = cy * cols + cx;
                if (cx >= 0 && cx < cols && idx >= 0 && idx < static_cast<int>(n))
                {
                    const se_palette_entry& e = mCramColors[idx];
                    ImGui::BeginTooltip();
                    // RGB888 packs 24 bits, which won't fit e.raw (16-bit); show the
                    // reconstructed color instead of a misleading 0x0000.
                    if (rgb888)
                    {
                        ImGui::Text("CRAM #%d   RGB888 0x%06X", idx,
                                    (e.r << 16) | (e.g << 8) | e.b);
                    }
                    else
                    {
                        ImGui::Text("CRAM #%d   raw 0x%04X", idx, e.raw);
                    }
                    ImGui::Text("RGB  %d, %d, %d", e.r, e.g, e.b);
                    ImGui::EndTooltip();
                }
            }
        }
    }
    ImGui::End();
}

void App::DrawVdp1Table()
{
    if (ImGui::Begin("VDP1 Table"))
    {
        if (!mbHasData)
        {
            ImGui::TextDisabled("No data loaded.");
        }
        else
        {
            const size_t count = se_command_count(mContext);
            ImGui::Text("%zu command tables (15 words each: CMDCTRL - GRDA)", count);
            const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX;
            if (ImGui::BeginTable("vdp1table", 3, flags))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("CMDCTRL LINK PMOD COLR SRCA SIZE  XA YA XB YB XC YC XD YD  GRDA");
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
                        uint8_t raw[0x20] = {};
                        se_read_vram(mContext, SE_VRAM_KIND_VDP1_VRAM, cmd.table_address,
                                     raw, sizeof(raw));
                        char words[160];
                        int p = 0;
                        for (int w = 0; w < 15 && p < static_cast<int>(sizeof(words)) - 6; ++w)
                        {
                            const uint16_t v = static_cast<uint16_t>((raw[w * 2] << 8) | raw[w * 2 + 1]);
                            p += std::snprintf(words + p, sizeof(words) - p, "%04X ", v);
                        }
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", row);
                        ImGui::TableNextColumn();
                        ImGui::Text("0x%05X", cmd.table_address);
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(words);
                    }
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
}

void App::DrawVdp2Table()
{
    if (ImGui::Begin("VDP2 Table"))
    {
        if (!mbHasData || !se_has_vdp2_registers(mContext))
        {
            ImGui::TextDisabled("No VDP2 register data loaded.");
        }
        else
        {
            auto R = [&](uint32_t hw) { return se_get_vdp2_register(mContext, hw); };
            const uint16_t bgon = R(0x020);
            const uint16_t cha = R(0x028);
            const uint16_t chb = R(0x02A);
            const uint16_t prina = R(0x0F8);
            const uint16_t prinb = R(0x0FA);

            // NBG color counts max out at 32K (N0CHCN=3); higher codes are
            // reserved for NBG, so they read as unknown rather than a real depth.
            const char* colorName[8] = { "16", "256", "2048", "32K", "?", "?", "?", "?" };
            const uint32_t colorNum[4] = {
                (cha & 0x0070u) >> 4, (cha & 0x3000u) >> 12,
                (chb & 0x0002u) >> 1, (chb & 0x0020u) >> 5 };
            const uint32_t priority[4] = {
                prina & 0x7u, (prina >> 8) & 0x7u, prinb & 0x7u, (prinb >> 8) & 0x7u };
            const uint16_t scrollX[4] = {
                uint16_t(R(0x070) & 0x7FF), uint16_t(R(0x080) & 0x7FF),
                uint16_t(R(0x090) & 0x7FF), uint16_t(R(0x094) & 0x7FF) };
            const uint16_t scrollY[4] = {
                uint16_t(R(0x074) & 0x7FF), uint16_t(R(0x084) & 0x7FF),
                uint16_t(R(0x092) & 0x7FF), uint16_t(R(0x096) & 0x7FF) };

            ImGui::TextDisabled("Decoded normal-background (NBG) configuration:");
            const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
            if (ImGui::BeginTable("vdp2table", 5, flags))
            {
                ImGui::TableSetupColumn("Layer");
                ImGui::TableSetupColumn("On");
                ImGui::TableSetupColumn("Colors");
                ImGui::TableSetupColumn("Priority");
                ImGui::TableSetupColumn("Scroll (X, Y)");
                ImGui::TableHeadersRow();
                for (int n = 0; n < 4; ++n)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("NBG%d", n);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted((bgon & (1u << n)) ? "yes" : "no");
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(colorName[colorNum[n] & 7]);
                    ImGui::TableNextColumn(); ImGui::Text("%u", priority[n]);
                    ImGui::TableNextColumn(); ImGui::Text("(%u, %u)", scrollX[n], scrollY[n]);
                }
                ImGui::EndTable();
            }
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
