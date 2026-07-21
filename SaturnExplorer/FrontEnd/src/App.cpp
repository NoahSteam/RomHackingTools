#include "App.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"  // DockBuilder + BeginViewportSideBar for the default layout

#include "Platform/IPlatform.h"
#include "SaturnRegions.h"
#include "Theme.h"
#include "Debug/FormatString.h"   // tracepoint output mini-syntax
#include "SavestateDriver.h"
#ifdef SE_ENABLE_LIVE
#include "LiveDriver.h"      // native builds only (threads/sockets)
#include "SeLiveProtocol.h"  // SE_LIVE_VERSION (client protocol version)
#endif

namespace sfe
{

namespace
{

#ifdef SE_ENABLE_LIVE
// Saturn runs at ~60 fps; the recorder's window is expressed in frames, so the
// UI converts its seconds knob through this. Pre-capture memory estimate uses a
// typical compressed frame size until a real average is available.
constexpr int    kFramesPerSecond   = 60;
constexpr double kEstBytesPerFrame  = 1.3 * 1024.0 * 1024.0;
#endif

// Swaps a context pointer for the lifetime of the scope and restores it on exit,
// so pointing the panels at a scrub context mid-frame can't leak past this draw
// (even if an early return is added between the swap and the end of the frame).
struct ScopedContextSwap
{
    se_context** slot;
    se_context*  saved;
    ScopedContextSwap(se_context** s, se_context* now) : slot(s), saved(*s) { *s = now; }
    ~ScopedContextSwap() { *slot = saved; }
    ScopedContextSwap(const ScopedContextSwap&) = delete;
    ScopedContextSwap& operator=(const ScopedContextSwap&) = delete;
};

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

// --- Small hand-drawn vector icons (no icon font). Each draws centered at 'c'
// with half-extent ~'r' onto 'dl'. Colors come from the theme so they track it. ---
void IconPlay(ImDrawList* dl, ImVec2 c, float r, ImU32 col)
{
    dl->AddTriangleFilled(ImVec2(c.x - r * 0.6f, c.y - r), ImVec2(c.x - r * 0.6f, c.y + r),
                          ImVec2(c.x + r, c.y), col);
}
void IconPause(ImDrawList* dl, ImVec2 c, float r, ImU32 col)
{
    const float w = r * 0.38f;
    dl->AddRectFilled(ImVec2(c.x - r * 0.55f, c.y - r), ImVec2(c.x - r * 0.55f + w, c.y + r), col, 1.0f);
    dl->AddRectFilled(ImVec2(c.x + r * 0.17f, c.y - r), ImVec2(c.x + r * 0.17f + w, c.y + r), col, 1.0f);
}
void IconStep(ImDrawList* dl, ImVec2 c, float r, ImU32 col)   // play + bar (step forward)
{
    dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x - r, c.y + r),
                          ImVec2(c.x + r * 0.35f, c.y), col);
    dl->AddRectFilled(ImVec2(c.x + r * 0.55f, c.y - r), ImVec2(c.x + r, c.y + r), col, 1.0f);
}

void IconTri(ImDrawList* dl, ImVec2 c, float r, ImU32 col, bool left)
{
    const float s = left ? -1.0f : 1.0f;
    dl->AddTriangleFilled(ImVec2(c.x - s * r, c.y - r), ImVec2(c.x - s * r, c.y + r),
                          ImVec2(c.x + s * r, c.y), col);
}
enum class Ico { Play, Pause, Step, First, Prev, Next, Last };

// Icon-only button (fixed square-ish size). 'id' must be unique (kept invisible
// with "##"); the glyph is drawn over the button rect. Returns true when pressed.
bool IconButton(const char* id, Ico ico, const char* tip, bool disabled = false)
{
    const float h = ImGui::GetFrameHeight();
    if (disabled) ImGui::BeginDisabled();
    const bool pressed = ImGui::Button(id, ImVec2(h * 1.5f, h));
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    const float r = h * 0.22f;
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    switch (ico)
    {
    case Ico::Play:  IconPlay(dl, c, r, col); break;
    case Ico::Pause: IconPause(dl, c, r, col); break;
    case Ico::Step:  IconStep(dl, c, r, col); break;
    case Ico::Prev:  IconTri(dl, c, r, col, true); break;
    case Ico::Next:  IconTri(dl, c, r, col, false); break;
    case Ico::First:
        dl->AddRectFilled(ImVec2(c.x - r * 1.3f, c.y - r), ImVec2(c.x - r * 0.95f, c.y + r), col, 1.0f);
        IconTri(dl, ImVec2(c.x + r * 0.15f, c.y), r, col, true); break;
    case Ico::Last:
        IconTri(dl, ImVec2(c.x - r * 0.15f, c.y), r, col, false);
        dl->AddRectFilled(ImVec2(c.x + r * 0.95f, c.y - r), ImVec2(c.x + r * 1.3f, c.y + r), col, 1.0f); break;
    }
    if (disabled) ImGui::EndDisabled();
    if (tip && !disabled) ImGui::SetItemTooltip("%s", tip);
    return pressed;
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

// Bytes this command's texture occupies in VDP1 VRAM. Mirrors the core's
// TextureByteSize (Core/src/Context.h) — bpp keyed by colour mode. Non-textured
// commands (clips, coords) have zero width/height and so return 0.
uint32_t TextureVramBytes(const se_command& c)
{
    const uint32_t pixels = static_cast<uint32_t>(c.width) * c.height;
    switch (c.color_mode)
    {
    case SE_COLOR_BANK_16:
    case SE_COLOR_LUT_16:   return pixels / 2;   // 4 bpp
    case SE_COLOR_RGB555:   return pixels * 2;   // 16 bpp
    default:                return pixels;       // 8 bpp bank modes
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

#ifdef SE_ENABLE_LIVE
    mRecorder.Configure(mRecordSeconds * kFramesPerSecond);
#endif

    // Relocate ImGui's layout file into the per-user config dir so the dock layout
    // the user arranges is saved to a stable local setting, independent of the
    // working directory. The ImGui context already exists (the platform created it
    // before App::Initialize) and settings load lazily on the first NewFrame, so
    // pointing IniFilename here takes effect before anything is loaded or saved.
    // mIniPath must outlive the context — ImGui stores the pointer, not a copy.
    if (!Settings::EnsureConfigDir().empty())
    {
        mIniPath = Settings::LayoutFilePath();   // config-dir path assembly lives in Settings
        if (!mIniPath.empty()) ImGui::GetIO().IniFilename = mIniPath.c_str();
    }
    LoadSettings();                // panel visibility, data dir, emulator paths

    mWatchPanel.LoadSession();     // restore the session's watch list, if any
    mAssemblyPanel.LoadComments(); // restore the session's assembly comments
    mLog.Info("Saturn Explorer started");
}

void App::Shutdown()
{
    SaveSettings();
    mWatchPanel.SaveSession();
    mAssemblyPanel.SaveComments();
    CloseData();
}

// Adding a panel here makes it save/restore AND appear in the Windows menu with no
// other edits. Order is the Windows-menu display order (settings keys are order-free).
const std::vector<App::PanelInfo>& App::PanelList()
{
    static const std::vector<PanelInfo> kList = {
        {"layerControls",   "Layer Controls",     &Panels::layerControls},
        {"vramMap",         "VRAM Map",           &Panels::vramMap},
        {"archiveExplorer", "Archive Explorer",   &Panels::archiveExplorer},
        {"searchRom",       "Search ROM / Files", &Panels::searchRom},
        {"vdpOutput",       "VDP Output",         &Panels::vdpOutput},
        {"vdp1Framebuffer", "VDP1 Framebuffer",   &Panels::vdp1Framebuffer},
        {"worldView",       "3D View",            &Panels::worldView},
        {"commandList",     "VDP1 Command List",  &Panels::commandList},
        {"vdp1Table",       "VDP1 Table",         &Panels::vdp1Table},
        {"vdp2Table",       "VDP2 Table",         &Panels::vdp2Table},
        {"registers",       "Registers",          &Panels::registers},
        {"colorRam",        "Color RAM",          &Panels::colorRam},
        {"workRam",         "Work RAM",           &Panels::workRam},
        {"paletteRam",      "Palette RAM",        &Panels::paletteRam},
        {"textureViewer",   "Texture Viewer",     &Panels::textureViewer},
        {"paletteViewer",   "Palette Viewer",     &Panels::paletteViewer},
        {"references",      "References",         &Panels::references},
        {"selectedObject",  "Selected Object",    &Panels::selectedObject},
        {"watch",           "Watch",              &Panels::watch},
        {"assembly",        "SH-2 Assembly",      &Panels::assembly},
        {"hexEditor",       "Hex Editor",         &Panels::hexEditor},
        {"controller",      "Controller",         &Panels::controller},
        {"log",             "Log",                &Panels::log},
    };
    return kList;
}

void App::LoadSettings()
{
    mSettings.Load();
    for (const PanelInfo& p : PanelList())
        mPanels.*(p.flag) = mSettings.GetBool("panels", p.key, mPanels.*(p.flag));
    mDataDir      = mSettings.Get("data", "dir", mDataDir);
    mMednafenPath = mSettings.Get("emulators", "mednafen", mMednafenPath);
    mYabausePath  = mSettings.Get("emulators", "yabause", mYabausePath);
}

void App::SaveSettings()
{
    for (const PanelInfo& p : PanelList())
        mSettings.SetBool("panels", p.key, mPanels.*(p.flag));
    mSettings.Set("data", "dir", mDataDir);
    // Emulator paths are owned by the installer. Whatever LoadSettings read stays
    // in mSettings and round-trips through Save, so we don't clobber them here.
    mSettings.Save();
    mSettingsDirty = false;
}

void App::CloseData()
{
#ifdef SE_ENABLE_LIVE
    if (mScrubContext)
    {
        se_destroy(mScrubContext);
        mScrubContext = nullptr;
    }
    mbScrubbing = false;
    mScrubIndex = -1;
    mScrubShownIndex = -1;
    mRecorder.Clear();
#endif
    // Destroying the context closes the data source. For a live source that also
    // releases any pause the debugger applied — the LiveDriver resumes the emulator
    // on close (race-free, from its own poll thread) so Yabause is never left paused
    // after a disconnect, a load of another source, or app shutdown.
    if (mContext)
    {
        se_destroy(mContext);
        mContext = nullptr;
    }
    mbHasData = false;
    mbLiveSource = false;
    mbPaused = false;
    mSelectedCommand = -1;
    mSelection.clear();
    // The frame texture is freed lazily (on next size change) or with the
    // platform's device at shutdown; mark it stale so a new dump recreates it.
    mFrameWidth = 0;
    mFrameHeight = 0;
}

namespace
{
// Append a little-endian uint16/uint32 to a byte vector.
void PushU16(std::vector<uint8_t>& v, uint16_t x)
{
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}
void PushU32(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

// Fit a w×h image into 'avail' preserving aspect (scale by the tighter axis, with a
// >0 guard) and center it in both axes; returns the top-left draw origin (relative to
// the current cursor) and writes the chosen scale. Shared by the image panels that
// letterbox — VDP Output and the Texture Viewer.
ImVec2 AspectFit(const ImVec2& avail, int w, int h, float& outScale)
{
    float scale = 1.0f;
    if (w > 0 && h > 0)
    {
        const float sx = avail.x / static_cast<float>(w);
        const float sy = avail.y / static_cast<float>(h);
        scale = sx < sy ? sx : sy;
        if (scale <= 0.0f) scale = 1.0f;
    }
    outScale = scale;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    return ImVec2(origin.x + (avail.x - w * scale) * 0.5f,
                  origin.y + (avail.y - h * scale) * 0.5f);
}
}  // namespace

// Snapshot every region the current source exposes and pack it into one
// self-describing ".sedump": an 8-byte magic, a section table, then the raw bytes
// (VRAM/CRAM/work-RAM big-endian as the Saturn stores them; registers as a
// hardware-offset big-endian image). The section table names each region and gives
// its Saturn bus address + size + file offset, so a region is trivial to carve out.
void App::DumpMemory(IPlatform& platform)
{
    if (!mbHasData || !mContext)
    {
        return;
    }

    struct Section { char name[16]; uint32_t address; std::vector<uint8_t> bytes; };
    std::vector<Section> sections;
    auto push = [&](const char* name, uint32_t addr, std::vector<uint8_t> bytes) {
        Section s{};
        std::strncpy(s.name, name, sizeof(s.name) - 1);
        s.address = addr;
        s.bytes = std::move(bytes);
        sections.push_back(std::move(s));
    };

    // Bulk memory regions, in canonical order. Bus addresses are informational
    // (for the section table); sizes come from the shared SaturnRegions constants.
    struct RegionDesc { se_vram_kind kind; const char* name; uint32_t addr; uint32_t size; };
    static const RegionDesc kRegions[] = {
        { SE_VRAM_KIND_VDP1_VRAM, "VDP1_VRAM", 0x25C00000u, kVdp1VramSize },
        { SE_VRAM_KIND_VDP2_VRAM, "VDP2_VRAM", 0x25E00000u, kVdp2VramSize },
        { SE_VRAM_KIND_CRAM,      "CRAM",      0x25F00000u, kCramSize },
        { SE_VRAM_KIND_WRAM_LOW,  "WRAM_LOW",  0x00200000u, kWramSize },
        { SE_VRAM_KIND_WRAM_HIGH, "WRAM_HIGH", 0x06000000u, kWramSize },
        { SE_VRAM_KIND_VDP1_FB,   "VDP1_FB",   0x25C80000u, kVdp1FbSize },
    };
    for (const RegionDesc& d : kRegions)
    {
        std::vector<uint8_t> b(d.size);
        const size_t got = se_read_vram(mContext, d.kind, 0, b.data(), d.size);
        if (got == 0) continue;   // region not provided by this source
        b.resize(got);
        push(d.name, d.addr, std::move(b));
    }

    // Register images, packed big-endian like VRAM.
    auto readRegs = [&](uint32_t byteLen, uint16_t (*get)(se_context*, uint32_t)) {
        std::vector<uint8_t> b(byteLen);
        for (uint32_t o = 0; o < byteLen; o += 2)
        {
            const uint16_t v = get(mContext, o);
            b[o]     = static_cast<uint8_t>(v >> 8);
            b[o + 1] = static_cast<uint8_t>(v & 0xFF);
        }
        return b;
    };
    if (se_has_vdp1_registers(mContext)) push("VDP1_REGS", 0x25D00000u, readRegs(kVdp1RegBytes, se_get_vdp1_register));
    if (se_has_vdp2_registers(mContext)) push("VDP2_REGS", 0x25F80000u, readRegs(kVdp2RegBytes, se_get_vdp2_register));

    if (sections.empty())
    {
        return;
    }

    // Header: magic(8) + version(u32) + count(u32); then per section a 32-byte
    // entry: name[16] + address(u32) + size(u32) + offset(u32) + pad(u32).
    const uint32_t count = static_cast<uint32_t>(sections.size());
    const uint32_t headerBytes = 8 + 4 + 4 + count * 32u;
    uint32_t total = headerBytes;
    for (const Section& s : sections) total += static_cast<uint32_t>(s.bytes.size());

    std::vector<uint8_t> out;
    out.reserve(total);
    const char magic[8] = { 'S', 'E', 'M', 'D', 'U', 'M', 'P', '1' };
    out.insert(out.end(), magic, magic + 8);
    PushU32(out, 1u);       // version
    PushU32(out, count);

    uint32_t offset = headerBytes;
    for (const Section& s : sections)
    {
        out.insert(out.end(), s.name, s.name + 16);
        PushU32(out, s.address);
        PushU32(out, static_cast<uint32_t>(s.bytes.size()));
        PushU32(out, offset);
        PushU32(out, 0u);   // pad
        offset += static_cast<uint32_t>(s.bytes.size());
    }
    for (const Section& s : sections)
    {
        out.insert(out.end(), s.bytes.begin(), s.bytes.end());
    }

    char name[64];
    std::snprintf(name, sizeof(name), "saturn_frame_%llu.sedump",
                  static_cast<unsigned long long>(se_frame_number(mContext)));
    platform.SaveFile(name, out.data(), out.size());
}

void App::SelectCommand(int command, bool additive)
{
    if (command < 0)
    {
        return;
    }
    if (additive)
    {
        // Toggle this command in/out of the multi-selection.
        auto it = std::find(mSelection.begin(), mSelection.end(), command);
        if (it != mSelection.end())
        {
            mSelection.erase(it);
            if (mSelectedCommand == command)
            {
                mSelectedCommand = mSelection.empty() ? -1 : mSelection.back();
            }
        }
        else
        {
            mSelection.push_back(command);
            mSelectedCommand = command;   // newest becomes primary
        }
    }
    else
    {
        mSelection.assign(1, command);
        mSelectedCommand = command;
    }
}

bool App::IsSelected(int command) const
{
    return command >= 0 &&
           std::find(mSelection.begin(), mSelection.end(), command) != mSelection.end();
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
    mLog.Info(std::string("Loaded savestate: ") + (path ? path : ""));
    return true;
}

bool App::OpenLive(const char* endpoint)
{
#ifdef SE_ENABLE_LIVE
    CloseData();
    se_data_source dataSource;
    if (se_live_open(endpoint, &dataSource) != SE_OK)
    {
        return false;
    }
    if (!CreateContextFromSource(dataSource, &mContext))
    {
        return false;
    }
    mDataSource = dataSource;
    mbHasData = true;
    mbLiveSource = true;   // re-snapshot every frame (see BuildUI)
    mbPaused = false;      // a fresh connection is free-running
    mLog.Info(std::string("Connected to emulator (live): ") + (endpoint ? endpoint : "default"));
    // Force a breakpoint re-sync so any set from before connecting (or a prior
    // session) installs into this emulator instance.
    mLastBpGeneration = mBreakpoints.Generation() - 1;
    return true;
#else
    (void)endpoint;
    return false;
#endif
}

void App::EnableLiveAutoConnect(const char* endpoint)
{
#ifdef SE_ENABLE_LIVE
    mbAutoConnectLive = true;
    mLiveEndpoint = endpoint ? endpoint : "";
    mLiveRetrySeconds = 1.0f;   // attempt on the very first frame
#else
    (void)endpoint;
#endif
}

bool App::OpenSavestateBuffer(const uint8_t* data, size_t size)
{
    CloseData();
    se_data_source dataSource;
    // Same magic-dispatch as OpenSavestate, but from bytes the host already holds
    // (e.g. a browser reading a File into WASM memory) rather than a file path.
    if (se_savestate_open_buffer(data, size, &dataSource) != 0)
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
#ifdef SE_ENABLE_LIVE
    // Auto-connect: while nothing is loaded, retry the live endpoint about once a
    // second so the app latches onto a Yabause as soon as one starts. se_live_open
    // fails fast when no server is listening, so a failed poll is cheap.
    if (mbAutoConnectLive && !mbHasData)
    {
        mLiveRetrySeconds += ImGui::GetIO().DeltaTime;
        if (mLiveRetrySeconds >= 1.0f)
        {
            mLiveRetrySeconds = 0.0f;
            OpenLive(mLiveEndpoint.empty() ? nullptr : mLiveEndpoint.c_str());
        }
    }
#endif

    // A live source changes every frame: re-snapshot before anything reads it, so
    // all panels reflect the running emulator's current VDP state. (Static sources
    // snapshot once at load, in CreateContextFromSource.)
    if (mbLiveSource && mContext)
    {
        se_begin_frame(mContext);
        // Propagate any breakpoint changes (Assembly gutter, Watch "Break on...")
        // to the emulator, then reflect a breakpoint halt in the UI run state.
        SyncBreakpointsToLive();
        SyncTracepointsToLive();   // push tracepoint set (v8)
        DrainTraceEvents();        // pull fired tracepoints into the Log
#ifdef SE_ENABLE_LIVE
        uint32_t stopReason = 0, stopCpu = 0, stopPc = 0;
        if (se_live_get_stop(&mDataSource, &stopReason, &stopCpu, &stopPc) && !mbPaused)
        {
            mbPaused = true;   // halted on a breakpoint; panel follows the halted PC
        }
#endif
    }

#ifdef SE_ENABLE_LIVE
    // Record each live frame into the rolling ring buffer while the game is
    // running. Gated on the run state (not the emulator's frame counter) so the
    // ring fills regardless of how — or whether — the counter advances; paused and
    // history-scrubbing states never capture, so the buffer holds only real play.
    if (mbLiveSource && mContext && !mbPaused && !mbScrubbing)
    {
        mRecorder.Capture(mContext, se_frame_number(mContext));
    }
#endif

    // Top toolbar + bottom bars (status + timeline) reserve space from the
    // viewport; the dockspace fills what's left. They always operate on the live
    // context, so draw them before any scrub swap below.
    DrawToolbar(platform);
    DrawStatusBar();
    DrawTimeline();

    // If the timeline selected a past frame, point the data panels at a context
    // rebuilt over that recorded frame for the rest of this draw. The guard restores
    // the live context when BuildUI returns; frame control / capture above stay live.
    se_context* view = mContext;
#ifdef SE_ENABLE_LIVE
    if (mbScrubbing && RefreshScrubContext())
    {
        view = mScrubContext;
    }
    else
    {
        mbScrubbing = false;
    }
#endif
    ScopedContextSwap contextSwap(&mContext, view);

    if (mbHasData)
    {
        RenderFrameToTexture(platform);
    }

    const ImGuiID dockId = ImGui::DockSpaceOverViewport(
        0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    // Arrange the panels into the default layout the first time (unless the user
    // already has a saved layout in imgui.ini), or whenever "Reset Layout" is picked.
    if (!mbLayoutBuilt)
    {
        mbLayoutBuilt = true;
        const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockId);
        if (node == nullptr || node->IsLeafNode())
        {
            BuildDefaultLayout(dockId);
        }
    }
    if (mForceRebuildLayout)
    {
        mForceRebuildLayout = false;
        BuildDefaultLayout(dockId);   // snap back to the default arrangement
    }

    // Left column. Each panel is gated by its visibility flag (toolbar "Windows"
    // menu); a hidden panel is simply not drawn, so its dock tab disappears.
    if (mPanels.layerControls)   DrawLayerControls();
    if (mPanels.vramMap)         DrawVramMap();
    if (mPanels.archiveExplorer) DrawPlaceholder("Archive Explorer", "Disc filesystem tree — arrives in M6 (needs disc access).");
    if (mPanels.searchRom)       DrawPlaceholder("Search ROM / Files", "ROM & archive search — arrives in M6 (needs disc access).");

    // Center: VDP Output and its sibling tabs, then the command list, then the
    // texture/palette/reference row.
    if (mPanels.vdpOutput)       DrawVdpOutput(platform);
    if (mPanels.vdp1Framebuffer) DrawVdp1Framebuffer(platform);
    if (mPanels.worldView)       DrawWorldView(platform);
    if (mPanels.vdp1Table)       DrawVdp1Table();
    if (mPanels.vdp2Table)       DrawVdp2Table();
    if (mPanels.colorRam)        DrawColorRam();
    if (mPanels.workRam)         DrawWorkRam();
    if (mPanels.paletteRam)      DrawPlaceholder("Palette RAM", "VDP1 CLUT-area view — planned (see Color RAM for CRAM).");
    if (mPanels.registers)       DrawRegisters();
    if (mPanels.commandList)     DrawCommandList();
    if (mPanels.textureViewer)   DrawTextureViewer(platform);
    if (mPanels.paletteViewer)   DrawPaletteViewer();
    if (mPanels.references)      DrawReferences();

    // Right column. (The old Texture Preview / Palette (CLUT) panels duplicated the
    // center Texture/Palette viewers; the debugger Watch + Assembly panels live here
    // now.)
    if (mPanels.selectedObject)  DrawSelectedObject();
    if (mPanels.watch)           DrawWatch(platform);
    if (mPanels.assembly)        DrawAssembly();
    if (mPanels.hexEditor)       DrawHexEditor();
    if (mPanels.controller)      DrawController();
    else                         SendInput(0);   // hidden panel releases any held input
    if (mPanels.log)             DrawLog();

    // Game-data-directory modal + texture search results (both floating, drawn last
    // so they overlay the docked panels).
    DrawDataDirModal(platform);
    DrawDataSearchResults(platform);
    DrawTracepointEditor();   // modal; no-op until OpenTracepointEditor requests it
    // contextSwap restores the live context here as it goes out of scope.

    // Persist any preference the user changed this frame (panel visibility, data
    // dir). Dock-layout changes are saved separately by ImGui into imgui.ini.
    if (mSettingsDirty) SaveSettings();
}

// Programmatic default dock layout: a left inspector column (Texture/Palette live
// here now), a wide center (views + command list), a slim right column (Selected
// Object + Hex Editor), and a full-width debugger strip along the bottom (Watch +
// SH-2 Assembly). Runs once on first launch or after "Reset Layout"; the user's own
// rearrangements are saved by ImGui to imgui.ini and take precedence on later runs.
//
// NB: each docked title string must exactly match the panel's ImGui::Begin(...)
// title or the window silently floats. Hidden-by-default panels (Archive Explorer,
// Search ROM / Files, References) are docked next to a visible neighbour so they tab
// in when re-enabled from the Windows menu.
void App::BuildDefaultLayout(unsigned int dockspaceId)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    // Carve a full-width debugger strip off the bottom first, then split the top.
    ImGuiID top = dockspaceId;
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(top, ImGuiDir_Down, 0.24f, nullptr, &top);

    ImGuiID center = top;
    const ImGuiID left  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.24f, nullptr, &center);
    const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);

    // Left inspector, top to bottom: layers, VRAM map, then the texture/palette pair.
    ImGuiID leftRest = left;
    const ImGuiID lLayers = ImGui::DockBuilderSplitNode(leftRest, ImGuiDir_Up, 0.20f, nullptr, &leftRest);
    const ImGuiID lVram   = ImGui::DockBuilderSplitNode(leftRest, ImGuiDir_Up, 0.28f, nullptr, &leftRest);
    const ImGuiID lTex    = ImGui::DockBuilderSplitNode(leftRest, ImGuiDir_Up, 0.50f, nullptr, &leftRest);
    const ImGuiID lPal    = leftRest;

    // Center: view tabs (top) + command list (bottom).
    ImGuiID centerRest = center;
    const ImGuiID cViews = ImGui::DockBuilderSplitNode(centerRest, ImGuiDir_Up, 0.62f, nullptr, &centerRest);
    const ImGuiID cList  = centerRest;

    // Right: Selected Object (top) + Hex Editor (below).
    ImGuiID rightRest = right;
    const ImGuiID rObj = ImGui::DockBuilderSplitNode(rightRest, ImGuiDir_Up, 0.40f, nullptr, &rightRest);
    const ImGuiID rHex = rightRest;

    // Bottom strip: Watch | SH-2 Assembly, side by side.
    ImGuiID bottomRest = bottom;
    const ImGuiID bWatch = ImGui::DockBuilderSplitNode(bottomRest, ImGuiDir_Left, 0.5f, nullptr, &bottomRest);
    const ImGuiID bAsm   = bottomRest;

    // Left inspector.
    ImGui::DockBuilderDockWindow("Layer Controls", lLayers);
    ImGui::DockBuilderDockWindow("VRAM Map (VDP1)", lVram);
    ImGui::DockBuilderDockWindow("Texture Viewer", lTex);
    ImGui::DockBuilderDockWindow("Archive Explorer", lTex);      // hidden default; tabs w/ Texture
    ImGui::DockBuilderDockWindow("Palette Viewer", lPal);
    ImGui::DockBuilderDockWindow("Search ROM / Files", lPal);    // hidden default; tabs w/ Palette
    ImGui::DockBuilderDockWindow("References", lPal);            // hidden default; tabs w/ Palette

    // Center: the view group shares cViews, so they appear as tabs.
    ImGui::DockBuilderDockWindow("VDP Output", cViews);
    ImGui::DockBuilderDockWindow("VDP1 Framebuffer", cViews);
    ImGui::DockBuilderDockWindow("3D View", cViews);
    ImGui::DockBuilderDockWindow("VDP1 Table", cViews);
    ImGui::DockBuilderDockWindow("VDP2 Table", cViews);
    ImGui::DockBuilderDockWindow("Color RAM", cViews);
    ImGui::DockBuilderDockWindow("Work RAM", cViews);
    ImGui::DockBuilderDockWindow("Palette RAM", cViews);
    ImGui::DockBuilderDockWindow("Registers", cViews);
    ImGui::DockBuilderDockWindow("VDP1 Command List", cList);

    // Right inspector.
    ImGui::DockBuilderDockWindow("Selected Object", rObj);
    ImGui::DockBuilderDockWindow("Hex Editor", rHex);

    // Bottom debugger strip. Watch / Controller / Log tab together on the left; the
    // SH-2 Assembly gets the right half.
    ImGui::DockBuilderDockWindow("Watch", bWatch);
    ImGui::DockBuilderDockWindow("Controller", bWatch);
    ImGui::DockBuilderDockWindow("Log", bWatch);
    ImGui::DockBuilderDockWindow("SH-2 Assembly", bAsm);

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
            if (ImGui::BeginMenu("Load Dump"))
            {
                // Both save-state entries take a savestate file; OpenSavestate
                // auto-detects Yabause (.yss) vs Mednafen (MDFNSVST) by magic, so the
                // two labels just point users at the right file for their emulator.
                if (ImGui::MenuItem("Yabause Save State..."))
                {
                    std::string path;
                    if (platform.OpenFileDialog(path)) OpenSavestate(path.c_str());
                }
                if (ImGui::MenuItem("Mednafen Save State..."))
                {
                    std::string path;
                    if (platform.OpenFileDialog(path)) OpenSavestate(path.c_str());
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Raw Memory Dump..."))
                {
                    std::string path;
                    if (platform.OpenFileDialog(path)) OpenFullDump(path.c_str(), 0x00000000u);
                }
                ImGui::EndMenu();
            }
#ifdef SE_ENABLE_LIVE
            ImGui::Separator();
            if (ImGui::MenuItem("Connect to emulator (live)", nullptr, false, !mbLiveSource))
            {
                OpenLive(nullptr);   // default endpoint: local socket / named pipe,
                                     // or the TCP bridge on web (see se_live_open)
            }
            if (ImGui::MenuItem("Disconnect (live)", nullptr, false, mbLiveSource)) CloseData();

            // Recording window: 5 s default, up to 30 s. The estimate uses the
            // measured average compressed size per captured frame once recording
            // is under way, else a typical figure, so the memory cost is visible.
            ImGui::Separator();
            ImGui::TextDisabled("Recording");
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::SliderInt("Length##rec", &mRecordSeconds, 5, 30, "%d s"))
            {
                mRecorder.Configure(mRecordSeconds * kFramesPerSecond);
            }
            const size_t frames = mRecorder.Count();
            const double perFrame = frames ? static_cast<double>(mRecorder.BytesUsed()) / frames
                                           : kEstBytesPerFrame;
            const double estMB = perFrame * mRecordSeconds * kFramesPerSecond / (1024.0 * 1024.0);
            ImGui::Text("~%.0f MB (%s)", estMB,
                        frames ? "measured" : "estimated");
#endif
            if (ImGui::MenuItem("Close", nullptr, false, mbHasData)) CloseData();
            ImGui::EndPopup();
        }

        // Playback / stepping — live when the source supports frame control
        // (a running, patched Yabause); otherwise disabled.
        const bool canStep = mbHasData && se_supports_frame_control(mContext);
        ImGui::SameLine();
        if (mbPaused)
        {
            if (IconButton("##resume", Ico::Play, "Resume", !canStep))
            {
                se_frame_resume(mContext);
                mbPaused = false;
            }
        }
        else
        {
            if (IconButton("##pause", Ico::Pause, "Pause", !canStep))
            {
                se_frame_pause(mContext);
                mbPaused = true;
            }
        }
        ImGui::SameLine();
        if (IconButton("##step", Ico::Step, "Step one frame", !canStep))
        {
            se_frame_step(mContext, 1);
            mbPaused = true;
        }
        // Timeline scrubbing lives in the bottom bar (DrawTimeline), shown while
        // paused so the user can drag back through the recorded ring buffer.

        // Dump the current memory state to a file (native) / download (web).
        ImGui::SameLine();
        ImGui::BeginDisabled(!mbHasData);
        if (ImGui::Button("Dump Memory"))
        {
            DumpMemory(platform);
        }
        ImGui::SetItemTooltip("Save VDP1/VDP2 VRAM, CRAM, work RAM and registers to a .sedump file");
        ImGui::EndDisabled();

        // Game data directory (an ISO, or a folder of the game's extracted files).
        // Used by the Texture Viewer right-click "Find in game data" search.
        ImGui::SameLine();
        if (ImGui::Button("Set Data Directory"))
        {
            mOpenDataDirModal = true;
        }
        ImGui::SetItemTooltip("Set the game's data directory — an ISO/disc image or a folder of the\n"
                              "game's files — that the texture 'Find in game data' search scans.");

        // Launch the patched emulator recorded by the installer. Mednafen is always
        // shown (disabled with a hint if no path is recorded yet); Yabause appears
        // only when a path exists. Once running, the app auto-connects live.
        auto launchButton = [&](const char* label, const std::string& path) {
            ImGui::SameLine();
            ImGui::BeginDisabled(path.empty());
            if (ImGui::Button(label))
            {
                // NULL working dir: the platform runs it from the executable's own
                // folder (see IPlatform::LaunchProcess) so the emulator finds its
                // config/saves; the app's live auto-connect latches on once it's up.
                platform.LaunchProcess(path.c_str(), nullptr);
            }
            ImGui::EndDisabled();
            if (path.empty())
            {
                ImGui::SetItemTooltip("No emulator path recorded yet — run the installer\n"
                                      "(Integration/install.bat), which records it for you.");
            }
            else
            {
                ImGui::SetItemTooltip("Launch %s and auto-connect (live):\n%s", label, path.c_str());
            }
        };
        launchButton("Launch Mednafen", mMednafenPath);
        if (!mYabausePath.empty()) launchButton("Launch Yabause", mYabausePath);

        // "Windows" dropdown: show/hide individual panels.
        ImGui::SameLine();
        DrawWindowsMenu();

        // Not-yet-implemented tools — visible but disabled.
        ImGui::SameLine();
        ImGui::BeginDisabled(true);
        ImGui::Button("Bookmarks"); ImGui::SameLine();
        ImGui::Button("Compare");   ImGui::SameLine();
        ImGui::Button("Screenshot"); ImGui::SameLine();
        ImGui::Button("Settings");  ImGui::SameLine();
        ImGui::Button("Help");
        ImGui::EndDisabled();

        // Right-aligned live counters. When connected live, show the client vs
        // server live-protocol version and flag a mismatch (which breaks capture).
        char live[72] = "";
        bool mismatch = false;
#ifdef SE_ENABLE_LIVE
        if (mbLiveSource)
        {
            const uint32_t sv = se_live_server_version(&mDataSource);
            if (sv != 0 && sv != SE_LIVE_VERSION)
            {
                mismatch = true;
                std::snprintf(live, sizeof(live),
                              "   |   LIVE MISMATCH: client v%u / Yabause v%u",
                              static_cast<unsigned>(SE_LIVE_VERSION), static_cast<unsigned>(sv));
            }
            else
            {
                std::snprintf(live, sizeof(live), "   |   live proto v%u",
                              static_cast<unsigned>(SE_LIVE_VERSION));
            }
        }
#endif
        char info[160];
        std::snprintf(info, sizeof(info), "FPS %.1f   |   VDP1: %zu objs   |   VDP2 regs: %s%s",
                      ImGui::GetIO().Framerate,
                      mbHasData ? se_sprite_count(mContext) : 0,
                      mbHasData ? "loaded" : "-", live);
        const float w = ImGui::CalcTextSize(info).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - w - 12.0f);
        if (mismatch)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f), "%s", info);
        }
        else
        {
            ImGui::TextUnformatted(info);
        }
    }
    ImGui::End();
}

// Toolbar "Windows" dropdown: a checkbox per panel, plus Show All / Hide All.
void App::DrawWindowsMenu()
{
    if (ImGui::Button("Windows"))
    {
        ImGui::OpenPopup("WindowsMenu");
    }
    ImGui::SetItemTooltip("Show or hide panels");

    if (ImGui::BeginPopup("WindowsMenu"))
    {
        if (ImGui::MenuItem("Reset Layout"))
        {
            mForceRebuildLayout = true;   // rebuild the default dock arrangement next frame
        }
        ImGui::SetItemTooltip("Restore the default panel arrangement");
        ImGui::Separator();
        if (ImGui::MenuItem("Show All"))
        {
            for (const PanelInfo& p : PanelList()) mPanels.*(p.flag) = true;
            mSettingsDirty = true;
        }
        if (ImGui::MenuItem("Hide All"))
        {
            for (const PanelInfo& p : PanelList()) mPanels.*(p.flag) = false;
            mSettingsDirty = true;
        }
        ImGui::Separator();
        for (const PanelInfo& p : PanelList())
        {
            // MenuItem returns true on the frame it's toggled; persist visibility then.
            if (ImGui::MenuItem(p.label, nullptr, &(mPanels.*(p.flag)))) mSettingsDirty = true;
        }
        ImGui::EndPopup();
    }
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
            // Game data directory indicator (for the texture "Find in game data" search).
            ImGui::Separator();
            if (mDataDir.empty())
            {
                ImGui::TextDisabled("Data dir: (not set)");
            }
            else
            {
                ImGui::Text("Data dir: %s", mDataDir.c_str());
            }
            if (ImGui::IsItemClicked())
            {
                mOpenDataDirModal = true;   // click the path to change it
            }
            ImGui::SetItemTooltip("%s\n(click to change)",
                                  mDataDir.empty() ? "No game data directory set" : mDataDir.c_str());
            // Frame counter + run state come from a live source (frame control);
            // the remaining fields (scanline / SH-2 PC / busy) await a fuller
            // live status feed and stay placeholders for now.
            char right[160];
            if (mbHasData && se_supports_frame_control(mContext))
            {
                std::snprintf(right, sizeof(right),
                              "Frame: %llu (%s)   |   Line: -   |   SH-2 PC: -   |   VDP1: -   VDP2: -",
                              static_cast<unsigned long long>(se_frame_number(mContext)),
                              mbPaused ? "paused" : "running");
            }
            else
            {
                std::snprintf(right, sizeof(right),
                              "Frame: -   |   Line: -   |   SH-2 PC: -   |   VDP1: -   VDP2: -");
            }
            const float w = ImGui::CalcTextSize(right).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - w - 12.0f);
            ImGui::TextDisabled("%s", right);
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

// Bottom timeline: while the live emulator is paused, show a horizontal scrubber
// spanning the window so the user can drag back through the recorded ring buffer
// and inspect any captured frame. Setting mbScrubbing / mScrubIndex here drives the
// scrub-context swap in BuildUI. Native (SE_ENABLE_LIVE) only.
void App::DrawTimeline()
{
#ifdef SE_ENABLE_LIVE
    // Shown while a live source is paused. (Off-pause the panels follow the live
    // emulator, so there's nothing to scrub.)
    if (!mbLiveSource || !mbPaused)
    {
        mbScrubbing = false;      // resume the live view
        mScrubShownIndex = -1;    // force a rebuild next time we scrub
        return;
    }

    const int n = static_cast<int>(mRecorder.Count());
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeightWithSpacing() + 6.0f;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginViewportSideBar("##Timeline", vp, ImGuiDir_Down, height, flags))
    {
        // No recording yet: show state instead of dead controls (tells the user the
        // ring is empty rather than leaving them wondering why nothing scrubs).
        if (n == 0)
        {
            mbScrubbing = false;
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Timeline: no frames recorded yet — play, then pause to scrub.");
            ImGui::End();
            return;
        }

        // Default to the newest captured frame when first paused or out of range;
        // RefreshScrubContext re-clamps defensively before rendering.
        if (!mbScrubbing || mScrubIndex < 0 || mScrubIndex >= n)
        {
            mScrubIndex = n - 1;
        }
        mbScrubbing = true;

        const ImGuiStyle& style = ImGui::GetStyle();

        // Left: frame readout (selected frame + buffer footprint / span).
        char head[96];
        const double mb = static_cast<double>(mRecorder.BytesUsed()) / (1024.0 * 1024.0);
        std::snprintf(head, sizeof(head), "Frame #%llu   %d / %d   (%.1f MB / %.1fs)",
                      static_cast<unsigned long long>(mRecorder.FrameNumber((size_t)mScrubIndex)),
                      mScrubIndex + 1, n, mb, n / static_cast<double>(kFramesPerSecond));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(head);
        ImGui::SameLine();

        // Reserve the right-hand step-button cluster so the scrubber can fill the
        // gap between the readout and the buttons.
        auto btnW = [&](const char* l) {
            return ImGui::CalcTextSize(l).x + style.FramePadding.x * 2.0f;
        };
        const float rightW = btnW("|<") + btnW("<") + btnW(">") + btnW(">|") +
                             style.ItemSpacing.x * 3.0f;

        // Middle: the scrubber spans the bottom of the window.
        const float sliderW = ImGui::GetContentRegionAvail().x - rightW - style.ItemSpacing.x;
        ImGui::SetNextItemWidth(sliderW > 80.0f ? sliderW : 80.0f);
        int idx = mScrubIndex;
        if (ImGui::SliderInt("##scrub", &idx, 0, n - 1, ""))
        {
            mScrubIndex = idx;
        }

        // Right: step backward / forward (with jump-to-ends), matching the concept
        // transport bar — icon buttons with tooltips.
        ImGui::SameLine();
        if (IconButton("##first", Ico::First, "First frame")) mScrubIndex = 0;
        ImGui::SameLine();
        if (IconButton("##prev", Ico::Prev, "Step back one frame") && mScrubIndex > 0) --mScrubIndex;
        ImGui::SameLine();
        if (IconButton("##next", Ico::Next, "Step forward one frame") && mScrubIndex < n - 1) ++mScrubIndex;
        ImGui::SameLine();
        if (IconButton("##last", Ico::Last, "Latest frame")) mScrubIndex = n - 1;
    }
    ImGui::End();
#endif
}

bool App::RefreshScrubContext()
{
#ifdef SE_ENABLE_LIVE
    const int n = static_cast<int>(mRecorder.Count());
    if (n == 0)
    {
        return false;
    }
    if (mScrubIndex < 0)  mScrubIndex = 0;
    if (mScrubIndex >= n) mScrubIndex = n - 1;

    // Already showing this frame: nothing to rebuild.
    if (mScrubContext && mScrubIndex == mScrubShownIndex)
    {
        return true;
    }

    // Select decompresses the frame into the recorder's scratch. The scrub context
    // is created once; its copied data source keeps the recorder's callbacks + user
    // pointer, so a later Select + se_begin_frame re-renders any frame exactly.
    se_data_source ds;
    if (!mRecorder.Select(static_cast<size_t>(mScrubIndex), &ds))
    {
        return false;
    }
    if (!mScrubContext)
    {
        se_config cfg;
        cfg.abi_version = SE_ABI_VERSION;
        cfg.reserved = 0;
        mScrubContext = se_create(&ds, &cfg);
        if (!mScrubContext)
        {
            return false;
        }
    }
    se_begin_frame(mScrubContext);
    mScrubShownIndex = mScrubIndex;
    return true;
#else
    return false;
#endif
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

// Debugger Watch Window — the panel owns its list/refresh; App just supplies the
// memory backend (served from the current context) and the expression resolver.
void App::DrawWatch(IPlatform& platform)
{
    uint32_t hexJump = 0;
    mWatchPanel.Draw(mMemBackend, mExprResolver, platform, mBreakpoints,
                     ImGui::GetIO().DeltaTime, &hexJump);
    if (hexJump != 0) mHexEditor.GoTo(hexJump);   // "View in Hex Editor"
}

// SH-2 Assembly — live disassembly around the master/slave PC. Reads CPU state +
// code from the current context (savestate or live) and renders via AssemblyPanel.
void App::DrawAssembly()
{
    AssemblyPanel::Request req;
    mAssemblyPanel.Draw(mContext, mMemBackend, mBreakpoints, mActions, mWatchPanel, mbLiveSource, req);

    if (req.editTracepoint) OpenTracepointEditor(req.tpCpu, req.tpAddr);

    // "Run to Here" sets an execution breakpoint at the target and resumes; the
    // emulator halts there via the stop event. No-op without frame control.
    if (req.runTo && mbHasData && se_supports_frame_control(mContext))
    {
        if (!mBreakpoints.HasExecutionAt(mAssemblyPanel.Cpu(), req.runToAddr))
            mBreakpoints.ToggleExecution(mAssemblyPanel.Cpu(), req.runToAddr);
        SyncBreakpointsToLive();
        se_frame_resume(mContext);
        mbPaused = false;
    }
    if (req.viewHex) mHexEditor.GoTo(req.hexAddr);   // "View Address in Hex Editor"
}

// --- Tracepoints / structured Log (Execution Actions, Phase 1) -------------------

namespace
{
// IFormatContext over the current core context: registers via se_get_sh2_regs, memory
// via the debugger backend (big-endian). Used for the editor's live preview + Test Fire
// (and, later, for formatting values the emulator captures at a tracepoint hit).
struct ContextFormat : sfe::IFormatContext
{
    se_context*         ctx = nullptr;
    sfe::IMemoryBackend* backend = nullptr;
    int                 cpu = 0;
    uint32_t            frame = 0;
    se_sh2_regs         regs{};
    bool                haveRegs = false;

    bool GetValue(const std::string& n, uint32_t& o) const override
    {
        if (n == "frame") { o = frame; return true; }
        if (n == "cycle") { o = 0; return true; }         // not exposed yet
        if (!haveRegs) return false;
        if (n == "pc")   { o = regs.pc;   return true; }
        if (n == "pr")   { o = regs.pr;   return true; }
        if (n == "sr")   { o = regs.sr;   return true; }
        if (n == "gbr")  { o = regs.gbr;  return true; }
        if (n == "vbr")  { o = regs.vbr;  return true; }
        if (n == "mach") { o = regs.mach; return true; }
        if (n == "macl") { o = regs.macl; return true; }
        if (n.size() >= 2 && n[0] == 'r')
        {
            const int i = std::atoi(n.c_str() + 1);
            if (i >= 0 && i < 16) { o = regs.r[i]; return true; }
        }
        return false;
    }
    bool ReadMem(uint32_t a, uint32_t sz, uint32_t& o) const override
    {
        if (!backend) return false;
        std::vector<sfe::MemoryReadRequest> reqs{{a, sz}};
        auto res = backend->ReadMemoryBatch(reqs);
        if (res.empty() || !res[0].success || res[0].bytes.size() < sz) return false;
        uint32_t v = 0;
        for (uint32_t i = 0; i < sz; ++i) v = (v << 8) | res[0].bytes[i];   // big-endian
        o = v;
        return true;
    }
    bool ReadString(uint32_t a, std::string& o, size_t maxLen) const override
    {
        for (size_t i = 0; i < maxLen; ++i)
        {
            uint32_t b = 0;
            if (!ReadMem(a + static_cast<uint32_t>(i), 1, b) || b == 0) break;
            o.push_back((b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.');
        }
        return !o.empty();
    }
};
}  // namespace

std::string App::FormatAgainstContext(const std::string& tmpl, int cpu)
{
    if (!mbHasData || !mContext) return std::string();
    ContextFormat fc;
    fc.ctx = mContext;
    fc.backend = &mMemBackend;
    fc.cpu = cpu;
    fc.frame = static_cast<uint32_t>(se_frame_number(mContext));
    fc.haveRegs = se_get_sh2_regs(mContext, cpu, &fc.regs) == SE_OK;
    return FormatEvaluate(tmpl, fc);
}

// Push the Log-type tracepoints to the emulator when the set changes (v8). Serializes
// each as a 16-byte descriptor {id,cpu,address,flags}; the emulator traps those PCs.
void App::SyncTracepointsToLive()
{
#ifdef SE_ENABLE_LIVE
    if (!mbLiveSource) { return; }
    if (mActions.Generation() == mLastTpGeneration) { return; }
    mLastTpGeneration = mActions.Generation();
    std::vector<uint8_t> descs;
    auto w32 = [&](uint32_t v) {
        descs.push_back(v & 0xFF); descs.push_back((v >> 8) & 0xFF);
        descs.push_back((v >> 16) & 0xFF); descs.push_back((v >> 24) & 0xFF);
    };
    uint32_t count = 0;
    for (const ExecutionAction& a : mActions.All())
    {
        if (a.type != ActionType::Log) continue;
        w32(static_cast<uint32_t>(a.id));
        w32(static_cast<uint32_t>(a.cpu));
        w32(a.address);
        w32(a.enabled ? SE_LIVE_TP_ENABLED : 0u);
        ++count;
    }
    se_live_set_tracepoints(&mDataSource, descs.data(), count);
#endif
}

// Pull fired tracepoint events from the driver and format each into a Log entry. The
// message is formatted here from the event's CAPTURED registers (memory derefs read
// the latest snapshot — up to ~1 frame stale, fine for RE logging).
void App::DrainTraceEvents()
{
#ifdef SE_ENABLE_LIVE
    if (!mbLiveSource) { return; }
    se_live_event evs[64];
    for (;;)
    {
        const uint32_t n = se_live_poll_events(&mDataSource, evs, 64);
        for (uint32_t i = 0; i < n; ++i)
        {
            const se_live_event& e = evs[i];
            const ExecutionAction* a = mActions.Get(e.id);
            if (!a) continue;
            mActions.RecordHit(e.id);
            if (!a->effects.writeToLog) continue;

            ContextFormat fc;
            fc.ctx = mContext;
            fc.backend = &mMemBackend;
            fc.cpu = static_cast<int>(e.cpu);
            fc.frame = e.frame;
            fc.haveRegs = true;
            for (int j = 0; j < 16; ++j) fc.regs.r[j] = e.regs[j];
            fc.regs.pc = e.regs[16]; fc.regs.pr = e.regs[17]; fc.regs.sr = e.regs[18];
            fc.regs.gbr = e.regs[19]; fc.regs.vbr = e.regs[20];
            fc.regs.mach = e.regs[21]; fc.regs.macl = e.regs[22];

            const std::string msg = FormatEvaluate(a->format, fc);
            std::vector<std::pair<std::string, std::string>> detail;
            char b[16];
            for (int j = 0; j < 16; ++j)
            { std::snprintf(b, sizeof(b), "%08X", e.regs[j]); detail.emplace_back("r" + std::to_string(j), b); }
            mLog.Tracepoint(e.frame, static_cast<int>(e.cpu), fc.regs.pc, msg, std::move(detail));
        }
        if (n < 64) break;   // drained
    }
#endif
}

void App::OpenTracepointEditor(int cpu, uint32_t addr)
{
    if (const ExecutionAction* existing = mActions.LogAt(cpu, addr))
    {
        mTpEdit = *existing;
        mTpEditNew = false;
    }
    else
    {
        mTpEdit = ExecutionAction{};
        mTpEdit.type = ActionType::Log;
        mTpEdit.cpu = cpu;
        mTpEdit.address = addr;
        mTpEdit.format = "PC={pc}";
    }
    mTpEditorOpen = true;   // one-shot: DrawTracepointEditor opens the popup next frame
}

void App::DrawTracepointEditor()
{
    if (mTpEditorOpen) { ImGui::OpenPopup("Tracepoint"); mTpEditorOpen = false; }
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
    bool keepOpen = true;
    if (!ImGui::BeginPopupModal("Tracepoint", &keepOpen, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("Instruction:  %s SH-2   %08X", mTpEdit.cpu ? "Slave" : "Master", mTpEdit.address);
    ImGui::Checkbox("Enabled", &mTpEdit.enabled);

    ImGui::SeparatorText("Output");
    char fbuf[256];
    std::snprintf(fbuf, sizeof(fbuf), "%s", mTpEdit.format.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##fmt", "e.g. Dialogue ID = {r4}", fbuf, sizeof(fbuf)))
        mTpEdit.format = fbuf;
    const std::string err = FormatValidate(mTpEdit.format);
    if (!err.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f), "! %s", err.c_str());
    else
    {
        const std::string prev = FormatAgainstContext(mTpEdit.format, mTpEdit.cpu);
        ImGui::TextDisabled("Preview: %s", (mbHasData ? prev : std::string("(no data loaded)")).c_str());
    }
    ImGui::TextDisabled("{r0-r15,pc,frame,...}  {r5:X8}  {*0x6034F20:u16}  {*r4:string}");

    ImGui::SeparatorText("Repeat");
    int rm = static_cast<int>(mTpEdit.repeat);
    ImGui::RadioButton("Every time", &rm, 0); ImGui::SameLine();
    ImGui::RadioButton("Once", &rm, 1);       ImGui::SameLine();
    ImGui::RadioButton("Every N", &rm, 2);
    mTpEdit.repeat = static_cast<RepeatMode>(rm);
    if (mTpEdit.repeat == RepeatMode::EveryN)
    {
        ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f);
        ImGui::InputInt("N", &mTpEdit.repeatN);
        if (mTpEdit.repeatN < 1) mTpEdit.repeatN = 1;
    }

    ImGui::SeparatorText("Condition");
    char cbuf[128];
    std::snprintf(cbuf, sizeof(cbuf), "%s", mTpEdit.condition.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##cond", "optional, e.g. r0 == 5", cbuf, sizeof(cbuf)))
        mTpEdit.condition = cbuf;

    ImGui::SeparatorText("Actions");
    ImGui::Checkbox("Write to Log", &mTpEdit.effects.writeToLog);
    ImGui::Checkbox("Pause Emulator", &mTpEdit.effects.pauseEmulator);
    ImGui::Checkbox("Capture Screenshot", &mTpEdit.effects.screenshot);
    ImGui::Checkbox("Save Memory Snapshot", &mTpEdit.effects.memSnapshot);
    ImGui::Checkbox("Play Sound", &mTpEdit.effects.playSound);
    ImGui::Checkbox("Run Script", &mTpEdit.effects.runScript);

    ImGui::Separator();
    // Test Fire: format against the current state and push a Log entry now, so the whole
    // tracepoint->format->log->jump path is exercisable without the emulator firing it.
    ImGui::BeginDisabled(!mbHasData || !err.empty());
    if (ImGui::Button("Test Fire"))
    {
        const std::string msg = FormatAgainstContext(mTpEdit.format, mTpEdit.cpu);
        std::vector<std::pair<std::string, std::string>> detail;
        se_sh2_regs r{};
        if (se_get_sh2_regs(mContext, mTpEdit.cpu, &r) == SE_OK)
        {
            char b[16];
            for (int i = 0; i < 16; ++i)
            { std::snprintf(b, sizeof(b), "%08X", r.r[i]); detail.emplace_back("r" + std::to_string(i), b); }
        }
        mLog.Tracepoint(static_cast<uint32_t>(se_frame_number(mContext)), mTpEdit.cpu,
                        mTpEdit.address, msg, std::move(detail));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("OK", ImVec2(90, 0)))
    {
        if (mTpEditNew) mActions.Add(mTpEdit); else mActions.Update(mTpEdit);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::DrawLog()
{
    if (ImGui::Begin("Log"))
    {
        LogPanel::Request req;
        mLog.Draw(req);
        if (req.jumpAssembly) { mAssemblyPanel.GoTo(req.jumpCpu, req.jumpAddr); mPanels.assembly = true; }
        if (req.jumpHex)      { mHexEditor.GoTo(req.hexAddr); mPanels.hexEditor = true; }
    }
    ImGui::End();
}

// Hex Editor — raw memory view/edit over the shared backend (same source as Watch).
void App::DrawHexEditor()
{
    mHexEditor.Draw(mMemBackend, mbLiveSource, ImGui::GetIO().DeltaTime);
}

// Saturn control pad: draw it, and forward the pressed-button mask to the live
// emulator whenever it changes (the driver drives the emulated pad directly).
void App::DrawController()
{
    unsigned int mask = 0;
    if (ImGui::Begin("Controller"))
    {
        mask = mController.Draw(mbLiveSource);
    }
    ImGui::End();
    SendInput(mask);
}

void App::SendInput(unsigned int mask)
{
    if (mask == mInputMask) { return; }   // only send on change (the glue latches)
    mInputMask = mask;
#ifdef SE_ENABLE_LIVE
    if (mbLiveSource)
    {
        se_live_send_input(&mDataSource, static_cast<uint32_t>(mController.Port()), mask);
    }
#endif
}

// Push the current breakpoint set to the live emulator when it changes. Serializes
// every breakpoint into the wire descriptor (address + size + flags) the LiveDriver
// forwards with a BKP command. No-op off a live source or when nothing changed.
void App::SyncBreakpointsToLive()
{
#ifdef SE_ENABLE_LIVE
    if (!mbLiveSource) { return; }
    if (mBreakpoints.Generation() == mLastBpGeneration) { return; }
    mLastBpGeneration = mBreakpoints.Generation();

    const std::vector<Breakpoint>& all = mBreakpoints.All();
    std::vector<uint8_t> descs;
    descs.reserve(all.size() * SE_LIVE_BKPT_DESC_LEN);
    auto put32 = [&descs](uint32_t w) {
        for (int i = 0; i < 4; ++i) descs.push_back(static_cast<uint8_t>(w >> (8 * i)));
    };
    for (const Breakpoint& b : all)
    {
        uint32_t kind = 0;   // 0 exec, 1 read, 2 write, 3 read/write
        switch (b.kind)
        {
            case BpKind::Execution:    kind = 0; break;
            case BpKind::MemRead:      kind = 1; break;
            case BpKind::MemWrite:     kind = 2; break;
            case BpKind::MemReadWrite: kind = 3; break;
        }
        uint32_t flags = kind & SE_LIVE_BP_KIND_MASK;
        if (b.cpu != 0) { flags |= SE_LIVE_BP_CPU_SLAVE; }
        if (b.enabled)  { flags |= SE_LIVE_BP_ENABLED; }
        put32(b.address);
        put32(b.size);
        put32(flags);
    }
    se_live_set_breakpoints(&mDataSource, descs.data(),
                            static_cast<uint32_t>(all.size()));
#endif
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
            // Fit the frame to the panel, preserving aspect ratio and centering it —
            // letterbox/pillarbox, like a 3D editor viewport.
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            float scale = 1.0f;
            const ImVec2 imgPos = AspectFit(avail, mFrameWidth, mFrameHeight, scale);
            const ImVec2 dispSize(mFrameWidth * scale, mFrameHeight * scale);
            ImGui::SetCursorScreenPos(imgPos);
            ImGui::Image(mFrameTexture, dispSize);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            auto toScreen = [&](const se_vec2& c)
            {
                return ImVec2(imgPos.x + c.x * scale, imgPos.y + c.y * scale);
            };

            // Only walk the sprite list when an overlay actually needs it.
            const bool wantOverlays = mRenderOpts.show_bounding_boxes ||
                                      mRenderOpts.show_object_numbers || !mSelection.empty();
            const size_t spriteCount = wantOverlays ? se_sprite_count(mContext) : 0;
            for (size_t i = 0; i < spriteCount; ++i)
            {
                se_sprite_2d sprite;
                if (se_get_sprite_2d(mContext, i, &sprite) != SE_OK)
                {
                    continue;
                }
                const bool selected = IsSelected(static_cast<int>(sprite.command_index));
                if (mRenderOpts.show_bounding_boxes || selected)
                {
                    const ImVec2 c0 = toScreen(sprite.corners[0]);
                    const ImVec2 c1 = toScreen(sprite.corners[1]);
                    const ImVec2 c2 = toScreen(sprite.corners[2]);
                    const ImVec2 c3 = toScreen(sprite.corners[3]);
                    if (selected)
                    {
                        // Tint the fill and draw a thick outline in the theme's
                        // selection color so a selection made elsewhere (e.g. the
                        // VRAM Map) is obvious and consistent across panels.
                        const ImU32 sel = ui::SelectionOutline();
                        dl->AddQuadFilled(c0, c1, c2, c3, (sel & 0x00FFFFFFu) | (60u << 24));
                        dl->AddQuad(c0, c1, c2, c3, sel, 3.0f);
                    }
                    else
                    {
                        dl->AddQuad(c0, c1, c2, c3, IM_COL32(230, 210, 60, 130), 1.0f);
                    }
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
                    SelectCommand(static_cast<int>(hitCommand), ImGui::GetIO().KeyShift);
                    mScrollCommandListToSelection = true;
                }
            }
        }
    }
    ImGui::End();
}

static void Checkerboard(ImVec2 topLeft, ImVec2 size, float cell);

namespace
{
// SPCTL sprite-type decode. Types 0x0-0x7 carry 16-bit framebuffer data, 0x8-0xF
// 8-bit. kSprColorMask is the colour-data field (the CRAM index bits) per type;
// SprPriorityNumber returns the 0..7 index into PRISA..PRISD. Both mirror the
// Saturn VDP2 manual's sprite-data table — the priority half is kept in sync with
// Core Context.h SpritePriorityNumber (validated against Yabause).
constexpr uint16_t kSprColorMask[16] = {
    0x07FF, 0x07FF, 0x07FF, 0x07FF, 0x03FF, 0x07FF, 0x03FF, 0x01FF,  // 16-bit types
    0x007F, 0x007F, 0x003F, 0x003F, 0x007F, 0x007F, 0x003F, 0x003F,  // 8-bit types
};

int SprPriorityNumber(uint16_t px, int type, bool spclmd)
{
    if (spclmd && (px & 0x8000)) return 0;   // direct-RGB pixel carries no number
    switch (type)
    {
    case 0x0: return (px >> 14) & 0x3;
    case 0x1: return (px >> 13) & 0x7;
    case 0x2: return (px >> 14) & 0x1;
    case 0x3: return (px >> 13) & 0x3;
    case 0x4: return (px >> 13) & 0x3;
    case 0x5: case 0x6: case 0x7: return (px >> 12) & 0x7;
    case 0x8: case 0x9: return (px >> 7) & 0x1;
    case 0xA:           return (px >> 6) & 0x3;
    case 0xC: case 0xD: return (px >> 7) & 0x1;
    case 0xE:           return (px >> 6) & 0x3;
    default:            return 0;   // types B, F: no priority bits
    }
}

// Priority heatmap ramp (0 = dark, 7 = hot), for the Priority display mode.
constexpr uint8_t kPrioHeat[8][3] = {
    { 45, 45, 52 }, { 40, 70, 150 }, { 40, 130, 190 }, { 40, 175, 120 },
    { 130, 195, 55 }, { 235, 205, 45 }, { 240, 140, 35 }, { 235, 60, 45 },
};
}  // namespace

// VDP1 Framebuffer viewer. VDP1 renders every sprite/polygon command for a frame
// into an off-screen 512x256 x 16bpp buffer (256 KiB), which VDP2 then scans out
// as the sprite layer. We capture the displayed (front) bank; this panel shows the
// emulator's real rendered pixels next to our command-list render in VDP Output.
//
// Three modes:
//  - Resolved: interpret each word by the VDP2 SPCTL sprite type. Direct-RGB
//    pixels (mixed mode + bit 15) are RGB555; colour-bank pixels take their
//    colour-data bits as a CRAM index and resolve through the palette (honouring
//    the CRAM colour mode). This is what the game actually displays.
//  - Raw RGB555: every word shown as RGB555 (exact for RGB sprites; colour-bank
//    pixels look wrong — useful to inspect the raw bits).
//  - Priority: per-pixel priority (SPCTL type -> PRISA..PRISD) as a heatmap.
// Word 0 (erased) is transparent. The frame buffer is host-endian (software
// renderer writes native u16, so little-endian by default); "Byte-swap" flips to
// big-endian for a big-endian source.
void App::DrawVdp1Framebuffer(IPlatform& platform)
{
    constexpr int kFbW = 512;
    constexpr int kFbH = 256;

    if (ImGui::Begin("VDP1 Framebuffer"))
    {
        mFbRaw.resize(kVdp1FbSize);
        const size_t got = mbHasData
            ? se_read_vram(mContext, SE_VRAM_KIND_VDP1_FB, 0, mFbRaw.data(), kVdp1FbSize)
            : 0;

        if (got < static_cast<size_t>(kFbW) * kFbH * 2)
        {
            ImGui::TextDisabled("No VDP1 frame buffer in this source.");
            ImGui::TextDisabled("Captured only from a live Yabause (software renderer).");
            ImGui::End();
            return;
        }

        // VDP2 state driving the resolve: sprite type + mixed-colour flag, the
        // eight priority slots, and a CRAM lookup rebuilt each frame.
        const bool haveVdp2 = se_has_vdp2_registers(mContext) != 0;
        const uint16_t spctl = se_get_vdp2_register(mContext, 0x0E0);
        const int  sprType = spctl & 0xF;
        const bool spclmd  = (spctl & 0x20) != 0;
        const uint16_t pris[4] = {
            se_get_vdp2_register(mContext, 0x0F0), se_get_vdp2_register(mContext, 0x0F2),
            se_get_vdp2_register(mContext, 0x0F4), se_get_vdp2_register(mContext, 0x0F6) };
        const uint8_t prioSlot[8] = {
            static_cast<uint8_t>(pris[0] & 7), static_cast<uint8_t>((pris[0] >> 8) & 7),
            static_cast<uint8_t>(pris[1] & 7), static_cast<uint8_t>((pris[1] >> 8) & 7),
            static_cast<uint8_t>(pris[2] & 7), static_cast<uint8_t>((pris[2] >> 8) & 7),
            static_cast<uint8_t>(pris[3] & 7), static_cast<uint8_t>((pris[3] >> 8) & 7) };
        mFbCram.resize(2048);
        const size_t nCram = se_read_cram_colors(mContext, 0, 2048, mFbCram.data());

        // Controls: display mode + endianness. Resolved/Priority need VDP2 regs.
        const char* kModes[] = { "Resolved (SPCTL+CRAM)", "Raw RGB555", "Priority" };
        ImGui::SetNextItemWidth(190.0f);
        ImGui::Combo("##fbmode", &mFbMode, kModes, IM_ARRAYSIZE(kModes));
        ImGui::SameLine();
        ImGui::Checkbox("Byte-swap", &mFbByteSwap);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Frame buffer is host-endian (little-endian) by default.\n"
                              "Enable only if colours look wrong (big-endian source).");
        }
        ImGui::SameLine();
        if (haveVdp2)
            ImGui::TextDisabled("type %X  %s  512x256", sprType, spclmd ? "mixed" : "palette");
        else
            ImGui::TextDisabled("no VDP2 regs - raw only");
        if (!haveVdp2 && mFbMode != 1)
        {
            mFbMode = 1;   // can't resolve without SPCTL/CRAM/PRIS
        }

        auto wordAt = [&](int i) -> uint16_t
        {
            // The VDP1 frame buffer is host-endian: the software renderer (VIDSoft)
            // writes native u16 pixels, so on the usual little-endian host the low
            // byte comes first — unlike VDP1/VDP2 VRAM, which is big-endian. Default
            // to little-endian (correct for real captures); "Byte-swap" flips to
            // big-endian for a big-endian source.
            const uint8_t b0 = mFbRaw[i * 2], b1 = mFbRaw[i * 2 + 1];
            return mFbByteSwap
                ? static_cast<uint16_t>((static_cast<uint16_t>(b0) << 8) | b1)
                : static_cast<uint16_t>((static_cast<uint16_t>(b1) << 8) | b0);
        };

        // Decode a word to RGBA per the active mode. A == 0 means transparent.
        auto decode = [&](uint16_t w, uint8_t out[4])
        {
            out[0] = out[1] = out[2] = out[3] = 0;
            if (w == 0) return;                           // erased -> transparent
            if (mFbMode == 1)                              // Raw RGB555
            {
                DecodeRgb555(w, out[0], out[1], out[2]); out[3] = 255;
                return;
            }
            if (mFbMode == 2)                              // Priority heatmap
            {
                const uint8_t p = prioSlot[SprPriorityNumber(w, sprType, spclmd) & 7];
                out[0] = kPrioHeat[p][0]; out[1] = kPrioHeat[p][1];
                out[2] = kPrioHeat[p][2]; out[3] = 255;
                return;
            }
            // Resolved: direct RGB, else colour-bank index through CRAM.
            if (spclmd && (w & 0x8000))
            {
                DecodeRgb555(w, out[0], out[1], out[2]); out[3] = 255;
                return;
            }
            const uint16_t idx = w & kSprColorMask[sprType];
            if (idx == 0 || idx >= nCram) return;          // index 0 = transparent
            const se_palette_entry& e = mFbCram[idx];
            out[0] = e.r; out[1] = e.g; out[2] = e.b; out[3] = 255;
        };

        mFbRgba.resize(static_cast<size_t>(kFbW) * kFbH * 4);
        for (int i = 0; i < kFbW * kFbH; ++i)
        {
            decode(wordAt(i), &mFbRgba[static_cast<size_t>(i) * 4]);
        }

        mFbTexture = EnsureTexture(platform, mFbTexture, mFbTexW, mFbTexH, kFbW, kFbH);
        if (mFbTexture != 0)
        {
            platform.UpdateTexture(mFbTexture, mFbRgba.data(), kFbW, kFbH);
        }

        // Aspect-fit into the panel over a checkerboard (matches VDP Output / textures).
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float readoutH = ImGui::GetTextLineHeightWithSpacing();
        const float boxH = avail.y > readoutH ? avail.y - readoutH : avail.y;
        const float sx = avail.x / static_cast<float>(kFbW);
        const float sy = boxH / static_cast<float>(kFbH);
        float scale = sx < sy ? sx : sy;
        if (scale <= 0.0f) scale = 1.0f;
        const ImVec2 dispSize(kFbW * scale, kFbH * scale);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 imgPos(origin.x + (avail.x - dispSize.x) * 0.5f, origin.y);
        Checkerboard(imgPos, dispSize, 8.0f);
        ImGui::SetCursorScreenPos(imgPos);
        ImGui::Image(mFbTexture, dispSize);

        // Hover readout: pixel coords, raw word, and what it resolved to.
        if (ImGui::IsItemHovered())
        {
            const ImVec2 mouse = ImGui::GetMousePos();
            int px = static_cast<int>((mouse.x - imgPos.x) / scale);
            int py = static_cast<int>((mouse.y - imgPos.y) / scale);
            if (px < 0) px = 0; else if (px >= kFbW) px = kFbW - 1;
            if (py < 0) py = 0; else if (py >= kFbH) py = kFbH - 1;
            const int i = py * kFbW + px;
            const uint16_t w = wordAt(i);
            const uint8_t* c = &mFbRgba[static_cast<size_t>(i) * 4];
            if (w == 0)
            {
                ImGui::Text("(%3d,%3d)  0x0000  transparent", px, py);
            }
            else if (mFbMode == 2)
            {
                ImGui::Text("(%3d,%3d)  0x%04X  priority %u", px, py, w,
                            prioSlot[SprPriorityNumber(w, sprType, spclmd) & 7]);
            }
            else if (mFbMode == 0 && !(spclmd && (w & 0x8000)))
            {
                ImGui::Text("(%3d,%3d)  0x%04X  bank idx %u  #%02X%02X%02X", px, py, w,
                            w & kSprColorMask[sprType], c[0], c[1], c[2]);
            }
            else
            {
                ImGui::Text("(%3d,%3d)  0x%04X  RGB  #%02X%02X%02X", px, py, w, c[0], c[1], c[2]);
            }
        }
        else if (mFbMode == 2)
        {
            ImGui::TextDisabled("Priority 0 (back) -> 7 (front).  Hover a pixel for its value.");
        }
        else
        {
            ImGui::TextDisabled("Hover a pixel for its raw word and colour.");
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
                const ImVec2 imgMin = ImGui::GetItemRectMin();
                const bool hovered = ImGui::IsItemHovered();

                // Record where a left-press started, to tell a click from an orbit.
                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    m3dPressPos = ImGui::GetMousePos();
                }
                if (hovered)
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
                // A left-release that barely moved is a click: pick the sprite under
                // it (same camera the frame was rendered with).
                if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                {
                    const ImVec2 up = ImGui::GetMousePos();
                    const float dx = up.x - m3dPressPos.x;
                    const float dy = up.y - m3dPressPos.y;
                    if (dx * dx + dy * dy < 16.0f)
                    {
                        size_t hit = 0;
                        if (se_hit_test_3d(mContext, &cam,
                                           static_cast<int>(up.x - imgMin.x),
                                           static_cast<int>(up.y - imgMin.y), &hit) == SE_OK)
                        {
                            SelectCommand(static_cast<int>(hit), ImGui::GetIO().KeyShift);
                            mScrollCommandListToSelection = true;
                        }
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

                // If the selection was changed by another panel, scroll its row into
                // view once. IncludeItemByIndex keeps that row laid out even when the
                // clipper would otherwise skip it, so SetScrollHereY can act on it.
                const bool doScroll = mScrollCommandListToSelection;
                mScrollCommandListToSelection = false;

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(count));
                if (doScroll && mSelectedCommand >= 0 &&
                    mSelectedCommand < static_cast<int>(count))
                {
                    clipper.IncludeItemByIndex(mSelectedCommand);
                }
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
                        if (ImGui::Selectable(label, IsSelected(row),
                                              ImGuiSelectableFlags_SpanAllColumns))
                        {
                            SelectCommand(row, ImGui::GetIO().KeyShift);
                        }
                        // Double-click: jump the Hex Editor to this command's table in
                        // VDP1 VRAM and surface the same row in the VDP1 Table.
                        if (ImGui::IsItemHovered() &&
                            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            SelectCommand(row, false);
                            mHexEditor.GoTo(kVdp1VramBase + cmd.table_address);
                            mScrollVdp1TableToSelection = true;
                        }
                        if (doScroll && row == mSelectedCommand)
                        {
                            ImGui::SetScrollHereY(0.5f);
                        }
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(CommandTypeName(cmd.type));
                        ImGui::TableNextColumn();
                        const uint32_t texBytes = TextureVramBytes(cmd);
                        if (texBytes > 0)
                        {
                            ImGui::Text("%ux%u (%u B)", cmd.width, cmd.height, texBytes);
                        }
                        else
                        {
                            ImGui::Text("%ux%u", cmd.width, cmd.height);
                        }
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

namespace
{
// Assemble a Windows BMP. If paletteCount > 0: an 8-bpp indexed BMP with color table
// 'palBGRA' (paletteCount*4 bytes, B,G,R,0) and 'pixels' = row-major top-down indices.
// Otherwise: a 24-bpp BMP with 'pixels' = row-major top-down RGBA (4 bytes/pixel).
// Rows are written bottom-up (BMP convention) and padded to a 4-byte boundary.
std::vector<uint8_t> BuildBmp(int w, int h, const std::vector<uint8_t>& pixels,
                              int paletteCount, const uint8_t* palBGRA)
{
    const bool indexed = paletteCount > 0;
    const int bpp = indexed ? 8 : 24;
    const int rowBytes = indexed ? w : w * 3;
    const int pad = (4 - (rowBytes & 3)) & 3;
    const int tableBytes = indexed ? paletteCount * 4 : 0;
    const uint32_t dataOff = 14 + 40 + static_cast<uint32_t>(tableBytes);
    const uint32_t imageSize = static_cast<uint32_t>((rowBytes + pad) * h);

    std::vector<uint8_t> bmp;
    bmp.reserve(dataOff + imageSize);
    bmp.push_back('B'); bmp.push_back('M');                       // BITMAPFILEHEADER
    PushU32(bmp, dataOff + imageSize);
    PushU32(bmp, 0);
    PushU32(bmp, dataOff);
    PushU32(bmp, 40);                                            // BITMAPINFOHEADER
    PushU32(bmp, static_cast<uint32_t>(w));
    PushU32(bmp, static_cast<uint32_t>(h));
    PushU16(bmp, 1);
    PushU16(bmp, static_cast<uint16_t>(bpp));
    PushU32(bmp, 0);
    PushU32(bmp, imageSize);
    PushU32(bmp, 0); PushU32(bmp, 0);
    PushU32(bmp, indexed ? static_cast<uint32_t>(paletteCount) : 0);
    PushU32(bmp, 0);
    if (indexed) bmp.insert(bmp.end(), palBGRA, palBGRA + tableBytes);

    for (int y = h - 1; y >= 0; --y)                             // bottom-up rows
    {
        if (indexed)
        {
            for (int x = 0; x < w; ++x)
                bmp.push_back(pixels[static_cast<size_t>(y) * w + x]);
        }
        else
        {
            for (int x = 0; x < w; ++x)
            {
                const uint8_t* p = &pixels[(static_cast<size_t>(y) * w + x) * 4];
                bmp.push_back(p[2]); bmp.push_back(p[1]); bmp.push_back(p[0]);   // BGR
            }
        }
        for (int i = 0; i < pad; ++i) bmp.push_back(0);
    }
    return bmp;
}
}  // namespace

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
                ImGui::SameLine();
                if (ImGui::Button("Export..."))
                {
                    ExportTexture(platform, cmd, w, h);
                }

                // Aspect-fit the texture into the remaining region, centered over a
                // checkerboard — same letterbox behavior as the VDP Output panel.
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                float scale = 1.0f;
                const ImVec2 pos = AspectFit(avail, w, h, scale);
                const ImVec2 dispSize(w * scale, h * scale);
                ImGui::SetCursorScreenPos(pos);
                Checkerboard(pos, dispSize, 8.0f);
                ImGui::Image(mTexTexture, dispSize);

                // Right-click the texture -> search the game data directory for its
                // raw VRAM bytes. (Explicit id: Image() is not an interactive item.)
                if (ImGui::BeginPopupContextItem("##texture_ctx"))
                {
                    if (ImGui::MenuItem("Find in game data directory"))
                    {
                        BeginTextureSearch(platform, cmd);
                    }
                    ImGui::EndPopup();
                }
            }
            else
            {
                ImGui::TextDisabled("Texture decode failed (%d).", r);
            }
        }
    }
    ImGui::End();
}

void App::ExportTexture(IPlatform& platform, const se_command& cmd, int w, int h)
{
    if (w <= 0 || h <= 0 || mTexBuffer.size() < static_cast<size_t>(w) * h * 4)
    {
        return;   // nothing decoded to export
    }

    char name[80];
    std::vector<uint8_t> bmp;

    se_palette pal = {};
    if (PaletteOf(cmd, &pal) == SE_OK && pal.count > 0)
    {
        // Keep the game's own palette: BMP color table (BGRA) + a reverse map from the
        // decoded RGBA back to its palette index. The core only decodes to RGBA today
        // (no indexed output), so we reconstruct indices by exact RGBA match. Keying on
        // RGBA (not just RGB) keeps transparent/opaque duplicates distinct; if a palette
        // still repeats a colour, the first index wins and a miss falls back to 0.
        // TODO: a core indexed-decode entry point would make this exact and lossless.
        std::vector<uint8_t> table(static_cast<size_t>(pal.count) * 4);
        std::unordered_map<uint32_t, uint8_t> toIndex;
        toIndex.reserve(pal.count * 2);
        for (int i = 0; i < pal.count; ++i)
        {
            const se_palette_entry& e = pal.entries[i];
            table[i * 4 + 0] = e.b; table[i * 4 + 1] = e.g;
            table[i * 4 + 2] = e.r; table[i * 4 + 3] = 0;
            const uint32_t key = (static_cast<uint32_t>(e.r) << 24) |
                                 (static_cast<uint32_t>(e.g) << 16) |
                                 (static_cast<uint32_t>(e.b) << 8) | e.a;
            toIndex.emplace(key, static_cast<uint8_t>(i));
        }
        std::vector<uint8_t> idx(static_cast<size_t>(w) * h);
        for (size_t i = 0; i < idx.size(); ++i)
        {
            const uint8_t* p = &mTexBuffer[i * 4];
            const uint32_t key = (static_cast<uint32_t>(p[0]) << 24) |
                                 (static_cast<uint32_t>(p[1]) << 16) |
                                 (static_cast<uint32_t>(p[2]) << 8) | p[3];
            const auto it = toIndex.find(key);
            idx[i] = (it != toIndex.end()) ? it->second : 0;
        }
        std::snprintf(name, sizeof(name), "texture_%06X_%dx%d_pal%u.bmp",
                      cmd.texture_address, w, h, pal.count);
        bmp = BuildBmp(w, h, idx, pal.count, table.data());
    }
    else
    {
        std::snprintf(name, sizeof(name), "texture_%06X_%dx%d.bmp",
                      cmd.texture_address, w, h);
        bmp = BuildBmp(w, h, mTexBuffer, 0, nullptr);
    }

    platform.SaveFile(name, bmp.data(), bmp.size());
}

// Kick a "find this texture in the game data" search. The needle is the texture's
// raw packed VRAM bytes (Saturn big-endian — the same form they'd take in the game's
// files). If no data directory is set yet, stash the needle and pop the set-dir modal,
// which runs the pending search once a directory is chosen.
void App::BeginTextureSearch(IPlatform& platform, const se_command& cmd)
{
    (void)platform;
    const uint32_t n = TextureVramBytes(cmd);
    if (!mbHasData || n == 0)
    {
        return;
    }
    mPendingNeedle.resize(n);
    const size_t got = se_read_vram(mContext, SE_VRAM_KIND_VDP1_VRAM,
                                    cmd.texture_address, mPendingNeedle.data(), n);
    mPendingNeedle.resize(got);

    char label[96];
    std::snprintf(label, sizeof(label), "Texture @0x%06X (%ux%u, %u bytes)",
                  cmd.texture_address, cmd.width, cmd.height, static_cast<unsigned>(got));
    mPendingSearchLabel = label;

    if (mDataDir.empty())
    {
        mSearchAfterSetDir = true;     // run once the user picks a directory
        mOpenDataDirModal = true;
    }
    else
    {
        RunPendingSearch();
    }
}

void App::RunPendingSearch()
{
    mSearchResults.clear();
    if (mPendingNeedle.empty() || mDataDir.empty())
    {
        mSearchSummary = "Nothing to search.";
        mShowSearchResults = true;
        return;
    }
    const size_t files = SearchDataDir(mDataDir, mPendingNeedle.data(),
                                       mPendingNeedle.size(), mSearchResults);
    size_t total = 0;
    for (const DataSearchHit& h : mSearchResults)
    {
        total += h.offsets.size();
    }
    char sum[224];
    std::snprintf(sum, sizeof(sum),
                  "%s\n%zu match(es) in %zu file(s)  —  scanned %zu file%s in the data directory.",
                  mPendingSearchLabel.c_str(), total, mSearchResults.size(),
                  files, files == 1 ? "" : "s");
    mSearchSummary = sum;
    mShowSearchResults = true;
}

// The "Set Game Data Directory" modal. Opened from the toolbar button, the status-bar
// path, or automatically when a search is requested with no directory set.
void App::DrawDataDirModal(IPlatform& platform)
{
    static char buf[1024] = {};
    if (mOpenDataDirModal)
    {
        std::snprintf(buf, sizeof(buf), "%s", mDataDir.c_str());
        ImGui::OpenPopup("Set Game Data Directory");
        mOpenDataDirModal = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Set Game Data Directory", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("Point Saturn Explorer at the game's original data: either a "
                           "folder containing the game's files, or an ISO / disc image.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(520.0f);
        ImGui::InputText("##datadir", buf, sizeof(buf));
        ImGui::SameLine();
        if (ImGui::Button("Folder..."))
        {
            std::string p;
            if (platform.PickDirectory(p))
            {
                std::snprintf(buf, sizeof(buf), "%s", p.c_str());
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("ISO / File..."))
        {
            std::string p;
            if (platform.OpenFileDialog(p))
            {
                std::snprintf(buf, sizeof(buf), "%s", p.c_str());
            }
        }

        ImGui::Separator();
        const bool valid = buf[0] != '\0';
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("OK", ImVec2(90, 0)))
        {
            mDataDir = buf;
            mSettingsDirty = true;   // remember the data dir across runs
            ImGui::CloseCurrentPopup();
            if (mSearchAfterSetDir)
            {
                mSearchAfterSetDir = false;
                RunPendingSearch();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 0)))
        {
            mSearchAfterSetDir = false;   // abandon any pending search
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Results of a texture search: which file(s) it was found in and where. Each file is
// a clickable link that reveals it in the OS file manager (Explorer/Finder/etc.).
void App::DrawDataSearchResults(IPlatform& platform)
{
    if (!mShowSearchResults)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(620, 320), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Data Search Results", &mShowSearchResults))
    {
        ImGui::TextWrapped("%s", mSearchSummary.c_str());
        ImGui::Separator();
        if (mSearchResults.empty())
        {
            ImGui::TextDisabled("No matches. The texture may be stored compressed or in a "
                                "different form in the game data, or the data directory is wrong.");
        }
        for (const DataSearchHit& hit : mSearchResults)
        {
            if (ImGui::Selectable(hit.path.c_str()))
            {
                platform.RevealPath(hit.path.c_str());
            }
            ImGui::SetItemTooltip("Click to reveal this file in the file manager");

            ImGui::Indent();
            std::string offs;
            for (size_t i = 0; i < hit.offsets.size(); ++i)
            {
                if (i >= 16)
                {
                    offs += "  (+" + std::to_string(hit.offsets.size() - i) + " more)";
                    break;
                }
                char o[24];
                std::snprintf(o, sizeof(o), "0x%llX",
                              static_cast<unsigned long long>(hit.offsets[i]));
                if (!offs.empty()) offs += ", ";
                offs += o;
            }
            ImGui::TextDisabled("offset(s): %s", offs.c_str());
            ImGui::Unindent();
        }
    }
    ImGui::End();
}

// Resolve a command's palette (CLUT for LUT-16, else the CRAM sub-palette for bank
// modes). Returns SE_ERR_UNSUPPORTED for direct RGB555 (no palette). Shared by the
// Palette Viewer and texture export.
se_result App::PaletteOf(const se_command& cmd, se_palette* pal)
{
    if (cmd.color_mode == SE_COLOR_RGB555)
        return SE_ERR_UNSUPPORTED;
    if (cmd.color_mode == SE_COLOR_LUT_16)
        return se_decode_palette(mContext, cmd.clut_address, pal);
    return se_decode_bank_palette(mContext, cmd.palette_bank, cmd.color_mode, pal);
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
            const se_result r = PaletteOf(cmd, &pal);
            if (r == SE_OK)
            {
                if (cmd.color_mode == SE_COLOR_LUT_16)
                    ImGui::Text("CLUT @0x%06X  —  %u entries", pal.clut_address, pal.count);
                else
                    ImGui::Text("CRAM bank 0x%X  —  %s  —  %u entries",
                                cmd.palette_bank, ColorModeName(cmd.color_mode), pal.count);
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
            const uint32_t kVramSize = 0x80000;   // 512 KiB
            const size_t count = se_vram_region_count(mContext);

            // Colour by region kind; legend mirrors these. Each block also gets a
            // dark outline (below) so abutting same-kind regions read as separate
            // items rather than one merged run.
            const ImU32 kOtherCol = IM_COL32(0x8A, 0x84, 0x50, 0xFF);  // olive
            auto kindColor = [&](se_vram_region_kind k) -> ImU32
            {
                switch (k)
                {
                case SE_VRAM_TEXTURE:   return ui::VramTexture();
                case SE_VRAM_CLUT:      return ui::VramClut();
                case SE_VRAM_CMD_TABLE: return ui::VramCmdTable();
                case SE_VRAM_GOURAUD:   return ui::VramGouraud();
                case SE_VRAM_OTHER:     return kOtherCol;
                default:                return ui::VramUnused();
                }
            };
            const ImU32 kBorderCol = IM_COL32(15, 15, 18, 255);

            // Pre-pass: bytes referenced by active commands, as the union of all
            // classified regions. Regions arrive sorted by address, so a running
            // "covered up to" watermark counts overlapping blocks (shared textures,
            // abutting tables) once instead of double-counting their sizes.
            uint32_t usedBytes = 0, coverEnd = 0;
            for (size_t i = 0; i < count; ++i)
            {
                se_vram_region reg;
                if (se_get_vram_region(mContext, i, &reg) != SE_OK) continue;
                const uint32_t rStart = reg.address < kVramSize ? reg.address : kVramSize;
                const uint32_t rEnd = (reg.address + reg.size < kVramSize)
                                    ? reg.address + reg.size : kVramSize;
                if (rEnd > coverEnd)
                {
                    usedBytes += rEnd - (rStart > coverEnd ? rStart : coverEnd);
                    coverEnd = rEnd;
                }
            }
            const float usedPct = 100.0f * static_cast<float>(usedBytes) / kVramSize;
            ImGui::TextDisabled("%zu regions   -   %u bytes used  (%.1f%% of 512 KiB)",
                                count, usedBytes, usedPct);

            // --- Layout: the map fills all remaining space above a wrapping legend,
            // so it scales with the panel. A left gutter holds address labels. ---
            struct LegendItem { const char* name; se_vram_region_kind kind; };
            const LegendItem legend[] = {
                { "Texture", SE_VRAM_TEXTURE }, { "CLUT", SE_VRAM_CLUT },
                { "Cmd Table", SE_VRAM_CMD_TABLE }, { "Gouraud", SE_VRAM_GOURAUD },
                { "Unused", SE_VRAM_UNUSED }, { "Other", SE_VRAM_OTHER },
            };
            const float lineH   = ImGui::GetTextLineHeightWithSpacing();
            const float textH   = ImGui::GetTextLineHeight();
            const float swSize  = textH;               // swatch is one glyph tall
            const float itemGap = 14.0f;
            auto itemWidth = [&](const char* n)
            { return swSize + 6.0f + ImGui::CalcTextSize(n).x + itemGap; };

            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float fullW = avail.x;

            // How many rows the legend needs at this width (greedy pack).
            int legendRows = 1;
            {
                float lx = 0.0f;
                for (const LegendItem& it : legend)
                {
                    const float w = itemWidth(it.name);
                    if (lx > 0.0f && lx + w > fullW) { ++legendRows; lx = 0.0f; }
                    lx += w;
                }
            }
            const float legendH = legendRows * lineH;

            const float gutterW = ImGui::CalcTextSize("80000").x + 8.0f;
            const int   rows = 16;                       // each row = 32 KiB
            const uint32_t perRow = kVramSize / rows;
            const float mapW = fullW - gutterW;
            float mapH = avail.y - legendH - ImGui::GetStyle().ItemSpacing.y;
            if (mapH < 48.0f) mapH = 48.0f;              // stay usable in a tiny panel
            const float rowH = mapH / rows;

            const ImVec2 gut = ImGui::GetCursorScreenPos();     // gutter top-left
            const ImVec2 origin(gut.x + gutterW, gut.y);        // map top-left

            // One interactive item spanning the whole map+gutter block.
            ImGui::InvisibleButton("vrammap", ImVec2(fullW, mapH));
            const bool mapHovered = ImGui::IsItemHovered();
            const bool mapClicked = ImGui::IsItemClicked();

            // Byte address under the cursor (only when over the map, not the gutter).
            uint32_t hoverAddr = 0;
            bool hoverValid = false;
            if (mapHovered)
            {
                const ImVec2 m = ImGui::GetMousePos();
                const int row = static_cast<int>((m.y - origin.y) / rowH);
                if (m.x >= origin.x && row >= 0 && row < rows)
                {
                    float fx = (m.x - origin.x) / mapW;
                    if (fx < 0.0f) fx = 0.0f;
                    if (fx > 0.999f) fx = 0.999f;
                    hoverAddr = static_cast<uint32_t>(row * perRow + fx * perRow);
                    hoverValid = true;
                }
            }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            // Background = the Unused colour, so gaps read as the "Unused" swatch.
            dl->AddRectFilled(origin, ImVec2(origin.x + mapW, origin.y + mapH),
                              kindColor(SE_VRAM_UNUSED));

            se_vram_region hoveredReg = {};
            bool haveHover = false;
            // Nearest command-owning region to the cursor, for a forgiving click:
            // texture blocks can be sub-pixel, so an exact hit is often impossible.
            uint32_t nearestRef = 0xFFFFFFFFu;
            uint32_t nearestDist = 0xFFFFFFFFu;
            // Row-slice rects (x0,y0,x1,y1) of the currently-selected command's
            // region(s), drawn on top after the fill pass so the selection outline
            // isn't overpainted.
            std::vector<ImVec4> selRects;
            for (size_t i = 0; i < count; ++i)
            {
                se_vram_region reg;
                if (se_get_vram_region(mContext, i, &reg) != SE_OK)
                {
                    continue;
                }
                const uint32_t end = reg.address + (reg.size ? reg.size : 1);
                const ImU32 col = kindColor(reg.kind);
                const bool isSelected = IsSelected(static_cast<int>(reg.ref_index));
                // A region can straddle rows; paint it row by row, outlining each
                // slice so neighbouring same-kind blocks read as distinct items.
                uint32_t a = reg.address;
                while (a < end && a < kVramSize)
                {
                    const int row = static_cast<int>(a / perRow);
                    const uint32_t rowEnd = (row + 1) * perRow;
                    const uint32_t b = end < rowEnd ? end : rowEnd;
                    const float x0 = origin.x + (a % perRow) / static_cast<float>(perRow) * mapW;
                    const float x1 = origin.x + ((b - 1) % perRow + 1) / static_cast<float>(perRow) * mapW;
                    const float y0 = origin.y + row * rowH;
                    const ImVec2 p0(x0, y0);
                    const ImVec2 p1(x1, y0 + rowH - 1.0f);
                    dl->AddRectFilled(p0, p1, col);
                    dl->AddRect(p0, p1, kBorderCol, 0.0f, 0, 1.0f);
                    if (isSelected)
                    {
                        selRects.push_back(ImVec4(p0.x, p0.y, p1.x, p1.y));
                    }
                    a = b;
                }
                if (hoverValid && !haveHover && hoverAddr >= reg.address && hoverAddr < end)
                {
                    hoveredReg = reg;
                    haveHover = true;
                }
                if (hoverValid && reg.ref_index != 0xFFFFFFFFu)
                {
                    const uint32_t dist = (hoverAddr < reg.address) ? reg.address - hoverAddr
                                        : (hoverAddr >= end)        ? hoverAddr - (end - 1)
                                                                    : 0u;
                    if (dist < nearestDist)
                    {
                        nearestDist = dist;
                        nearestRef = reg.ref_index;
                    }
                }
            }

            // Address labels + faint boundary lines every 128 KiB (0..0x80000).
            const ImU32 kLabelCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImU32 kGridCol  = IM_COL32(255, 255, 255, 18);
            for (uint32_t addr = 0; addr <= kVramSize; addr += 0x20000)
            {
                const float y = origin.y + (addr / static_cast<float>(kVramSize)) * mapH;
                dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + mapW, y), kGridCol, 1.0f);
                float ty = y - textH * 0.5f;
                if (addr == 0)         ty = y;
                if (addr == kVramSize) ty = y - textH;
                char lbl[12];
                std::snprintf(lbl, sizeof(lbl), "%05X", addr);
                dl->AddText(ImVec2(gut.x, ty), kLabelCol, lbl);
            }

            // Selection highlight: a bright, slightly-inflated outline so the block
            // for the sprite selected in VDP Output / the command list clearly pops.
            for (const ImVec4& r : selRects)
            {
                dl->AddRect(ImVec2(r.x - 1.0f, r.y - 1.0f),
                            ImVec2(r.z + 1.0f, r.w + 1.0f),
                            ui::SelectionOutline(), 0.0f, 0, 2.0f);
            }

            if (haveHover)
            {
                ImGui::BeginTooltip();
                ImGui::Text("0x%05X   (%u bytes)", hoveredReg.address, hoveredReg.size);
                ImGui::TextUnformatted(VramKindName(hoveredReg.kind));
                if (hoveredReg.ref_index != 0xFFFFFFFFu)
                {
                    ImGui::Text("Command #%u  (click to select in VDP Output)",
                                hoveredReg.ref_index);
                }
                ImGui::EndTooltip();
            }
            // Clicking anywhere in the map selects the nearest texture/command block,
            // which the VDP Output panel then highlights (via mSelectedCommand).
            if (mapClicked && nearestRef != 0xFFFFFFFFu)
            {
                SelectCommand(static_cast<int>(nearestRef), ImGui::GetIO().KeyShift);
                mScrollCommandListToSelection = true;
            }

            // Legend, wrapping to fit the panel width.
            auto swatch = [&](const char* name, se_vram_region_kind k)
            {
                ImDrawList* d = ImGui::GetWindowDrawList();
                const ImVec2 p = ImGui::GetCursorScreenPos();
                d->AddRectFilled(p, ImVec2(p.x + swSize, p.y + swSize), kindColor(k));
                d->AddRect(p, ImVec2(p.x + swSize, p.y + swSize), kBorderCol, 0.0f, 0, 1.0f);
                ImGui::Dummy(ImVec2(swSize + 4.0f, swSize));
                ImGui::SameLine();
                ImGui::TextUnformatted(name);
            };
            float lx = 0.0f;
            for (size_t i = 0; i < IM_ARRAYSIZE(legend); ++i)
            {
                const float w = itemWidth(legend[i].name);
                if (i > 0)
                {
                    if (lx + w <= fullW) ImGui::SameLine(0.0f, itemGap);
                    else                 lx = 0.0f;   // wrap to a new row
                }
                swatch(legend[i].name, legend[i].kind);
                lx += w;
            }
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
            const bool selected = IsSelected(static_cast<int>(r.command_index));
            if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns))
            {
                SelectCommand(static_cast<int>(r.command_index), ImGui::GetIO().KeyShift);
                mScrollCommandListToSelection = true;
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

                // Scroll+surface the selected row once when another panel asks (e.g.
                // a double-click in the Command List).
                const bool doScroll = mScrollVdp1TableToSelection;
                mScrollVdp1TableToSelection = false;

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(count));
                if (doScroll && mSelectedCommand >= 0 &&
                    mSelectedCommand < static_cast<int>(count))
                {
                    clipper.IncludeItemByIndex(mSelectedCommand);
                }
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
                        char rowlabel[32];
                        std::snprintf(rowlabel, sizeof(rowlabel), "%d##vt%d", row, row);
                        if (ImGui::Selectable(rowlabel, IsSelected(row),
                                              ImGuiSelectableFlags_SpanAllColumns))
                        {
                            SelectCommand(row, ImGui::GetIO().KeyShift);
                        }
                        // Double-click: jump the Hex Editor to this table entry and
                        // select+surface the same command in the Command List.
                        if (ImGui::IsItemHovered() &&
                            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            SelectCommand(row, false);
                            mHexEditor.GoTo(kVdp1VramBase + cmd.table_address);
                            mScrollCommandListToSelection = true;
                        }
                        if (doScroll && row == mSelectedCommand)
                        {
                            ImGui::SetScrollHereY(0.5f);
                        }
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

void App::DrawWorkRam()
{
    if (ImGui::Begin("Work RAM"))
    {
        if (!mbHasData)
        {
            ImGui::TextDisabled("No data loaded.");
        }
        else if (ImGui::BeginTabBar("wramtabs"))
        {
            struct Bank { const char* name; se_vram_kind kind; uint32_t base; };
            const Bank banks[2] = {
                {"Low (0x00200000)",  SE_VRAM_KIND_WRAM_LOW,  0x00200000u},
                {"High (0x06000000)", SE_VRAM_KIND_WRAM_HIGH, 0x06000000u},
            };
            for (const Bank& b : banks)
            {
                if (!ImGui::BeginTabItem(b.name))
                {
                    continue;
                }
                uint8_t probe[16];
                if (se_read_vram(mContext, b.kind, 0, probe, sizeof(probe)) == 0)
                {
                    ImGui::TextDisabled("Not available for this source "
                                        "(connect to a live emulator, or load a full dump).");
                }
                else
                {
                    ImGui::BeginChild("wramhex");   // always paired with EndChild
                    const int cols = 16;
                    const int rows = 0x100000 / cols;   // 1 MiB / 16
                    ImGuiListClipper clip;
                    clip.Begin(rows);
                    while (clip.Step())
                    {
                        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r)
                        {
                            uint8_t row[16] = {};
                            se_read_vram(mContext, b.kind,
                                         static_cast<uint32_t>(r * cols), row, cols);
                            char line[128];
                            int p = std::snprintf(line, sizeof(line), "%08X  ",
                                                  b.base + static_cast<uint32_t>(r * cols));
                            for (int i = 0; i < cols; ++i)
                            {
                                p += std::snprintf(line + p, sizeof(line) - p, "%02X ", row[i]);
                            }
                            if (p < static_cast<int>(sizeof(line)) - 1) line[p++] = ' ';
                            for (int i = 0; i < cols && p < static_cast<int>(sizeof(line)) - 1; ++i)
                            {
                                const uint8_t c = row[i];
                                line[p++] = (c >= 32 && c < 127) ? static_cast<char>(c) : '.';
                            }
                            line[p] = '\0';
                            // Clickable row: double-click jumps the Hex Editor to this
                            // address. PushID keeps each row's ID unique with no copy.
                            ImGui::PushID(r);
                            ImGui::Selectable(line);
                            if (ImGui::IsItemHovered() &&
                                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            {
                                mHexEditor.GoTo(b.base + static_cast<uint32_t>(r * cols));
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
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
