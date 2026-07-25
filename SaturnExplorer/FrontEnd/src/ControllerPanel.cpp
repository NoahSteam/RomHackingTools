#include "ControllerPanel.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

#include "imgui.h"
#include "SeLiveProtocol.h"
#include "Platform/IPlatform.h"
#include "Settings.h"

namespace sfe
{
namespace
{

struct Button
{
    unsigned int bit;
    const char* name;
    const char* label;
    float cx, cy, hx, hy;
    bool round;
    ImGuiKey key;
};

constexpr float kAspect = 0.52f;
const Button kButtons[] = {
    {SE_PAD_UP,    "Up",    "",      0.230f, 0.340f, 0.047f, 0.105f, false, ImGuiKey_UpArrow},
    {SE_PAD_DOWN,  "Down",  "",      0.230f, 0.560f, 0.047f, 0.105f, false, ImGuiKey_DownArrow},
    {SE_PAD_LEFT,  "Left",  "",      0.140f, 0.450f, 0.090f, 0.055f, false, ImGuiKey_LeftArrow},
    {SE_PAD_RIGHT, "Right", "",      0.320f, 0.450f, 0.090f, 0.055f, false, ImGuiKey_RightArrow},
    {SE_PAD_X,     "X",     "X",     0.640f, 0.410f, 0.040f, 0.060f, true,  ImGuiKey_A},
    {SE_PAD_Y,     "Y",     "Y",     0.720f, 0.335f, 0.040f, 0.060f, true,  ImGuiKey_S},
    {SE_PAD_Z,     "Z",     "Z",     0.800f, 0.285f, 0.040f, 0.060f, true,  ImGuiKey_D},
    {SE_PAD_A,     "A",     "A",     0.665f, 0.580f, 0.043f, 0.064f, true,  ImGuiKey_Z},
    {SE_PAD_B,     "B",     "B",     0.755f, 0.515f, 0.043f, 0.064f, true,  ImGuiKey_X},
    {SE_PAD_C,     "C",     "C",     0.835f, 0.455f, 0.043f, 0.064f, true,  ImGuiKey_C},
    {SE_PAD_START, "Start", "START", 0.485f, 0.665f, 0.062f, 0.034f, false, ImGuiKey_Enter},
    {SE_PAD_L,     "L",     "L",     0.220f, 0.135f, 0.095f, 0.034f, false, ImGuiKey_Q},
    {SE_PAD_R,     "R",     "R",     0.780f, 0.135f, 0.095f, 0.034f, false, ImGuiKey_E},
};

ImU32 Col(float r, float g, float b, float a = 1.0f)
{
    return ImGui::GetColorU32(ImVec4(r, g, b, a));
}

std::string StateText(unsigned int mask)
{
    std::string out;
    for (const Button& b : kButtons)
    {
        if (!(mask & b.bit)) continue;
        if (!out.empty()) out += ' ';
        out += b.name;
    }
    return out.empty() ? "(empty)" : out;
}

// USB-HID scancode -> ImGuiKey. Mednafen, SDL, and ImGui all use USB-HID scancodes, so
// a value parsed from mednafen.cfg maps straight across. ImGuiKey_A..Z / _0..9 / _F1..F12
// are contiguous. Returns ImGuiKey_None for keys we don't surface.
ImGuiKey HidToImGuiKey(int sc)
{
    if (sc >= 4  && sc <= 29) return (ImGuiKey)(ImGuiKey_A  + (sc - 4));    // A..Z
    if (sc >= 30 && sc <= 38) return (ImGuiKey)(ImGuiKey_1  + (sc - 30));   // 1..9
    if (sc == 39)             return ImGuiKey_0;
    if (sc >= 58 && sc <= 69) return (ImGuiKey)(ImGuiKey_F1 + (sc - 58));   // F1..F12
    switch (sc)
    {
    case 40: return ImGuiKey_Enter;       case 41: return ImGuiKey_Escape;
    case 42: return ImGuiKey_Backspace;   case 43: return ImGuiKey_Tab;
    case 44: return ImGuiKey_Space;       case 45: return ImGuiKey_Minus;
    case 46: return ImGuiKey_Equal;       case 76: return ImGuiKey_Delete;
    case 74: return ImGuiKey_Home;        case 77: return ImGuiKey_End;
    case 75: return ImGuiKey_PageUp;      case 78: return ImGuiKey_PageDown;
    case 79: return ImGuiKey_RightArrow;  case 80: return ImGuiKey_LeftArrow;
    case 81: return ImGuiKey_DownArrow;   case 82: return ImGuiKey_UpArrow;
    case 224: return ImGuiKey_LeftCtrl;   case 225: return ImGuiKey_LeftShift;
    case 226: return ImGuiKey_LeftAlt;    case 228: return ImGuiKey_RightCtrl;
    case 229: return ImGuiKey_RightShift; case 230: return ImGuiKey_RightAlt;
    default:  return ImGuiKey_None;
    }
}

// SE_PAD_* bits in the wire order used by se_live_poll_keymap (ascending SE_PAD_*): the
// keymap array's slot i carries the scancode for kKeyMapBits[i]. Maps a live keymap entry
// back to its kButtons slot via ButtonIndex.
const unsigned int kKeyMapBits[] = {
    SE_PAD_UP, SE_PAD_DOWN, SE_PAD_LEFT, SE_PAD_RIGHT,
    SE_PAD_A,  SE_PAD_B,    SE_PAD_C,    SE_PAD_X,
    SE_PAD_Y,  SE_PAD_Z,    SE_PAD_L,    SE_PAD_R,    SE_PAD_START,
};

int ButtonIndex(unsigned int bit)
{
    for (int i = 0; i < (int)(sizeof(kButtons) / sizeof(kButtons[0])); ++i)
        if (kButtons[i].bit == bit) return i;
    return -1;
}

}  // namespace

void ControllerPanel::EnsureBindings()
{
    static_assert(sizeof(kButtons) / sizeof(kButtons[0]) == kNumButtons,
                  "mKeyBind must have one slot per pad button");
    if (mBindingsInit) return;
    for (int i = 0; i < kNumButtons; ++i) mKeyBind[i] = (int)kButtons[i].key;
    mBindingsInit = true;
}

void ControllerPanel::ApplyLiveKeyMap(const int32_t* scancodes, int count)
{
    if (!scancodes || count <= 0) return;
    if (count > kNumButtons) count = kNumButtons;
    // Re-adopt only when the emulator's reported mapping actually changed since the last
    // adopt, so a manual rebind isn't clobbered on every frame while the config is stable.
    if (mLiveKeyMapAdopted &&
        std::equal(scancodes, scancodes + count, mAdoptedScancodes)) return;
    EnsureBindings();
    int matched = 0;
    for (int i = 0; i < count; ++i)
    {
        mAdoptedScancodes[i] = scancodes[i];
        if (scancodes[i] < 0) continue;                    // unbound / non-keyboard
        const ImGuiKey k = HidToImGuiKey(scancodes[i]);
        if (k == ImGuiKey_None) continue;
        const int idx = ButtonIndex(kKeyMapBits[i]);
        if (idx >= 0) { mKeyBind[idx] = (int)k; ++matched; }
    }
    mLiveKeyMapAdopted = true;
    mSettingsDirty = true;
    if (matched) std::snprintf(mBindMsg, sizeof(mBindMsg),
                               "Matched Mednafen's keys for Port %d (%d button%s).",
                               mPort + 1, matched, matched == 1 ? "" : "s");
}

void ControllerPanel::DrawKeyBindings()
{
    EnsureBindings();
    if (ImGui::CollapsingHeader("Key Bindings"))
    {
        ImGui::TextWrapped("Keys that drive the pad while this panel is focused. On a live "
                           "connection these match your Mednafen bindings automatically "
                           "(e.g. WASD); rebind below to override.");
        if (ImGui::Button("Reset to defaults"))
        {
            mBindingsInit = false;   // reseed defaults through the single EnsureBindings path
            EnsureBindings();
            mLiveKeyMapAdopted = false;   // let a live map re-adopt on the next poll
            mRebindIndex = -1;
            mSettingsDirty = true;
            std::snprintf(mBindMsg, sizeof(mBindMsg), "Reset to default bindings.");
        }
        if (mBindMsg[0]) ImGui::TextDisabled("%s", mBindMsg);

        if (ImGui::BeginTable("kbbind", 2,
                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerH))
        {
            for (int i = 0; i < kNumButtons; ++i)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(kButtons[i].name);
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                const char* label = (mRebindIndex == i) ? "press a key... (Esc cancels)"
                                                        : ImGui::GetKeyName((ImGuiKey)mKeyBind[i]);
                if (ImGui::Button(label, ImVec2(190, 0)))
                    mRebindIndex = (mRebindIndex == i) ? -1 : i;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    // Capture a rebind: assign the first key pressed (ignoring mouse); Esc cancels.
    if (mRebindIndex >= 0)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            mRebindIndex = -1;
        }
        else
        {
            for (ImGuiKey k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END;
                 k = (ImGuiKey)(k + 1))
            {
                if (k >= ImGuiKey_MouseLeft && k <= ImGuiKey_MouseWheelY) continue;
                if (ImGui::IsKeyPressed(k, false))
                {
                    mKeyBind[mRebindIndex] = (int)k;
                    mRebindIndex = -1;
                    mSettingsDirty = true;
                    break;
                }
            }
        }
    }
}

void ControllerPanel::Load(const Settings& s)
{
    mPort = std::atoi(s.Get("controller", "port", "0").c_str()) == 1 ? 1 : 0;
    mInputSource = std::atoi(s.Get("controller", "source", "0").c_str()) == 1 ? 1 : 0;
    mInputEnabled = s.GetBool("controller", "enabled", true);
    mAutoHold = s.GetBool("controller", "autohold", false);
    mShowCurrentInput = s.GetBool("controller.tools", "current", false);
    mShowTimeline = s.GetBool("controller.tools", "timeline", false);
    mShowQueue = s.GetBool("controller.tools", "queue", false);
    mShowRecording = s.GetBool("controller.tools", "recording", false);
    mShowMacros = s.GetBool("controller.tools", "macros", false);
    mShowStatistics = s.GetBool("controller.tools", "statistics", false);

    EnsureBindings();   // defaults, then overlay any saved per-button keys
    for (int i = 0; i < kNumButtons; ++i)
    {
        const std::string v = s.Get("controller.keys", kButtons[i].name, "");
        if (!v.empty()) mKeyBind[i] = std::atoi(v.c_str());
    }
}

void ControllerPanel::Save(Settings& s) const
{
    s.Set("controller", "port", std::to_string(mPort));
    s.Set("controller", "source", std::to_string(mInputSource));
    s.SetBool("controller", "enabled", mInputEnabled);
    s.SetBool("controller", "autohold", mAutoHold);
    s.SetBool("controller.tools", "current", mShowCurrentInput);
    s.SetBool("controller.tools", "timeline", mShowTimeline);
    s.SetBool("controller.tools", "queue", mShowQueue);
    s.SetBool("controller.tools", "recording", mShowRecording);
    s.SetBool("controller.tools", "macros", mShowMacros);
    s.SetBool("controller.tools", "statistics", mShowStatistics);

    for (int i = 0; i < kNumButtons; ++i)
        if (mBindingsInit) s.Set("controller.keys", kButtons[i].name, std::to_string(mKeyBind[i]));
}

bool ControllerPanel::ConsumeSettingsDirty()
{
    const bool dirty = mSettingsDirty;
    mSettingsDirty = false;
    return dirty;
}

bool ControllerPanel::ConsumeResetLayoutRequest()
{
    const bool requested = mResetLayoutRequested;
    mResetLayoutRequested = false;
    return requested;
}

void ControllerPanel::StopPlayback()
{
    mPlaybackActive = false;
    mPlaybackIndex = 0;
    mSources.playback = 0;
    RecomputeFinal();
}

void ControllerPanel::ReleaseManualInput()
{
    mSources.mouseMomentary = 0;
    mSources.keyboardMomentary = 0;
    mSources.latched = 0;
    RecomputeFinal();
}

void ControllerPanel::ClearAll()
{
    mSources = Sources{};
    StopPlayback();
    mMacroIndex = -1;
    mFinalState = 0;
}

void ControllerPanel::RecomputeFinal()
{
    const unsigned int live = mSources.mouseMomentary | mSources.keyboardMomentary |
                              mSources.latched | mSources.macro;
    if (!mInputEnabled || !mWasConnected)
        mFinalState = 0;
    else if (mPlaybackActive && mPlaybackPolicy != PlaybackPolicy::Overlay)
        mFinalState = mSources.playback;
    else
        mFinalState = live | mSources.playback;
    mFinalState &= SE_PAD_ALL;
}

void ControllerPanel::Update(bool connected, uint64_t frame)
{
    if (!connected)
    {
        if (mWasConnected) ClearAll();
        mWasConnected = false;
        RecomputeFinal();
        return;
    }
    if (!mWasConnected)
    {
        ClearAll();
        mWasConnected = true;
    }
    if (frame == mLastUpdateFrame) { RecomputeFinal(); return; }
    mLastUpdateFrame = frame;

    if (mPlaybackActive && !mRecording.empty())
    {
        const uint64_t rel = frame >= mPlaybackStartFrame ? frame - mPlaybackStartFrame : 0;
        const uint64_t base = mRecording.front().frame;
        while (mPlaybackIndex + 1 < mRecording.size() &&
               mRecording[mPlaybackIndex + 1].frame - base <= rel)
            ++mPlaybackIndex;
        mSources.playback = mRecording[mPlaybackIndex].buttons;
        const uint64_t end = mRecording.back().frame - base;
        if (rel > end)
        {
            if (mPlaybackLoop)
            {
                mPlaybackIndex = 0;
                mPlaybackStartFrame = frame;
                mSources.playback = mRecording.front().buttons;
            }
            else
            {
                mPlaybackActive = false;
                mSources.playback = 0;
            }
        }
    }

    if (mMacroIndex >= 0 && mMacroIndex < static_cast<int>(mMacros.size()))
    {
        Macro& macro = mMacros[static_cast<size_t>(mMacroIndex)];
        const uint64_t step = frame >= mMacroStartFrame ? frame - mMacroStartFrame : 0;
        if (step < macro.states.size())
        {
            mMacroStep = static_cast<size_t>(step);
            mSources.macro = macro.states[mMacroStep];
        }
        else
        {
            mMacroIndex = -1;
            mSources.macro = 0;
        }
    }
    RecomputeFinal();
}

void ControllerPanel::ObserveFrame(uint64_t frame)
{
    if (frame == mLastObservedFrame) return;
    mLastObservedFrame = frame;
    mLastObservedState = mFinalState;
    mHistory.push_back({frame, mFinalState});
    if (mHistory.size() > 600) mHistory.erase(mHistory.begin(), mHistory.begin() + 100);
    if (mRecordingActive) mRecording.push_back({frame, mFinalState});
}

void ControllerPanel::NotifyStateSent(uint64_t frame, unsigned int state)
{
    if (mPacketsSent == 0) mFirstSendFrame = frame;
    ++mPacketsSent;
    if (state != mLastSentState) ++mChangesSent;
    mLastSentState = state;
    mLastSendTime = ImGui::GetTime();
}

void ControllerPanel::DrawViewMenu()
{
    if (ImGui::BeginMenu("View"))
    {
        mSettingsDirty |= ImGui::MenuItem("Current Input", nullptr, &mShowCurrentInput);
        mSettingsDirty |= ImGui::MenuItem("Input Timeline", nullptr, &mShowTimeline);
        mSettingsDirty |= ImGui::MenuItem("Input Queue", nullptr, &mShowQueue);
        mSettingsDirty |= ImGui::MenuItem("Input Recording", nullptr, &mShowRecording);
        mSettingsDirty |= ImGui::MenuItem("Macros", nullptr, &mShowMacros);
        mSettingsDirty |= ImGui::MenuItem("Statistics", nullptr, &mShowStatistics);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Controller Layout"))
        {
            mResetLayoutRequested = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Options"))
    {
        const char* ports[] = {"Controller 1", "Controller 2"};
        for (int p = 0; p < 2; ++p)
            if (ImGui::MenuItem(ports[p], nullptr, mPort == p))
            { mPort = p; ClearAll(); mSettingsDirty = true; }
        ImGui::EndMenu();
    }
}

void ControllerPanel::DrawToolbar(bool connected)
{
    if (ImGui::BeginMenuBar())
    {
        DrawViewMenu();
        ImGui::EndMenuBar();
    }

    ImGui::TextUnformatted("Input Source");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    const char* sources[] = {"Keyboard + Mouse", "Mouse only"};
    if (ImGui::Combo("##inputsource", &mInputSource, sources, 2))
    { ClearAll(); mWasConnected = connected; mSettingsDirty = true; }

    if (ImGui::GetContentRegionAvail().x < 140.0f) ImGui::NewLine();
    else ImGui::SameLine();
    bool enabled = mInputEnabled;
    if (ImGui::Checkbox("Input Enabled", &enabled))
    {
        mInputEnabled = enabled;
        if (!enabled) ClearAll();
        mWasConnected = connected;
        mSettingsDirty = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!mInputEnabled);
    if (ImGui::Checkbox("Auto Hold (Latch)", &mAutoHold)) mSettingsDirty = true;
    ImGui::SameLine();
    const bool nothingHeld = (mSources.mouseMomentary | mSources.keyboardMomentary |
                              mSources.latched | mSources.macro | mSources.playback) == 0;
    ImGui::BeginDisabled(nothingHeld);
    if (ImGui::Button("Clear All")) ClearAll();
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextColored(connected ? ImVec4(0.35f, 0.86f, 0.46f, 1.0f)
                                 : ImVec4(0.92f, 0.38f, 0.34f, 1.0f),
                       "%s", connected ? "Connected" : "Disconnected");
    ImGui::Separator();
}

void ControllerPanel::DrawControllerCanvas(bool connected)
{
    const bool interactive = connected && mInputEnabled && !mPlaybackActive;
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool keyboard = interactive && mInputSource == 0 && focused && !ImGui::GetIO().WantTextInput;
    EnsureBindings();
    mSources.keyboardMomentary = 0;
    // Use the user-configurable bindings; don't drive the pad while capturing a rebind.
    if (keyboard && mRebindIndex < 0)
        for (int i = 0; i < kNumButtons; ++i)
            if (ImGui::IsKeyDown((ImGuiKey)mKeyBind[i])) mSources.keyboardMomentary |= kButtons[i].bit;
    mSources.mouseMomentary = 0;

    const float availW = ImGui::GetContentRegionAvail().x;
    const float availH = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing();
    float width = availW;
    if (availH > 0.0f && width * kAspect > availH) width = availH / kAspect;
    if (width < 1.0f) width = availW;
    const float height = width * kAspect;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    origin.x += (availW - width) * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto px = [&](float nx, float ny) { return ImVec2(origin.x + nx * width, origin.y + ny * height); };
    auto rad = [&](float f) { return f * width; };

    const float dim = interactive ? 1.0f : 0.66f;
    const ImU32 shadow = Col(0.01f, 0.015f, 0.020f, 0.88f);
    const ImU32 shell = Col(0.115f * dim, 0.125f * dim, 0.140f * dim);
    const ImU32 shellTop = Col(0.155f * dim, 0.165f * dim, 0.180f * dim);
    const ImU32 shellEdge = Col(0.30f * dim, 0.32f * dim, 0.35f * dim);
    const ImU32 recess = Col(0.045f * dim, 0.052f * dim, 0.060f * dim);
    const ImU32 windowBg = ImGui::GetColorU32(ImGuiCol_WindowBg);

    // Layered graphite body. The two lower lobes form the grips; masking the center
    // with the window color creates the Saturn pad's characteristic concave lower edge.
    dl->AddCircleFilled(ImVec2(px(0.205f, 0.625f).x + 3.0f, px(0.205f, 0.625f).y + 5.0f),
                        rad(0.165f), shadow, 64);
    dl->AddCircleFilled(ImVec2(px(0.795f, 0.625f).x + 3.0f, px(0.795f, 0.625f).y + 5.0f),
                        rad(0.165f), shadow, 64);
    dl->AddRectFilled(ImVec2(px(0.125f, 0.115f).x + 3.0f, px(0.125f, 0.115f).y + 5.0f),
                      ImVec2(px(0.875f, 0.690f).x + 3.0f, px(0.875f, 0.690f).y + 5.0f),
                      shadow, height * 0.20f);
    dl->AddCircleFilled(px(0.205f, 0.625f), rad(0.165f), shell, 64);
    dl->AddCircleFilled(px(0.795f, 0.625f), rad(0.165f), shell, 64);
    dl->AddRectFilled(px(0.125f, 0.115f), px(0.875f, 0.690f), shell, height * 0.20f);
    dl->AddRectFilled(px(0.160f, 0.115f), px(0.840f, 0.300f), shellTop, height * 0.14f);
    dl->AddEllipseFilled(px(0.500f, 0.920f), ImVec2(rad(0.275f), height * 0.205f),
                         windowBg, 0.0f, 72);
    dl->AddCircle(px(0.205f, 0.625f), rad(0.165f), shellEdge, 64, 1.2f);
    dl->AddCircle(px(0.795f, 0.625f), rad(0.165f), shellEdge, 64, 1.2f);
    dl->AddLine(px(0.245f, 0.125f), px(0.755f, 0.125f), shellEdge, 1.2f);

    // Recessed control wells.
    dl->AddCircleFilled(px(0.230f, 0.450f), rad(0.145f), recess, 56);
    dl->AddCircle(px(0.230f, 0.450f), rad(0.145f), Col(0.19f, 0.21f, 0.23f), 56, 1.2f);
    dl->AddCircleFilled(px(0.750f, 0.445f), rad(0.170f), recess, 56);
    dl->AddCircle(px(0.750f, 0.445f), rad(0.170f), Col(0.18f, 0.20f, 0.22f), 56, 1.2f);

    // A single, joined D-pad replaces the four disconnected rectangular buttons.
    const ImVec2 dc = px(0.230f, 0.450f);
    const float dax = rad(0.048f), day = height * 0.088f;
    const float drx = rad(0.132f), dry = height * 0.255f;
    const ImVec2 dpad[12] = {
        {dc.x - dax, dc.y - dry}, {dc.x + dax, dc.y - dry},
        {dc.x + dax, dc.y - day}, {dc.x + drx, dc.y - day},
        {dc.x + drx, dc.y + day}, {dc.x + dax, dc.y + day},
        {dc.x + dax, dc.y + dry}, {dc.x - dax, dc.y + dry},
        {dc.x - dax, dc.y + day}, {dc.x - drx, dc.y + day},
        {dc.x - drx, dc.y - day}, {dc.x - dax, dc.y - day},
    };
    dl->AddConcavePolyFilled(dpad, 12, Col(0.095f, 0.105f, 0.118f));
    dl->AddPolyline(dpad, 12, Col(0.015f, 0.020f, 0.027f), ImDrawFlags_Closed, 2.2f);
    dl->AddCircleFilled(dc, std::min(dax, day) * 0.72f, Col(0.075f, 0.083f, 0.094f), 24);

    // Subtle Saturn-style center branding.
    const ImVec2 logo = px(0.485f, 0.405f);
    dl->AddEllipse(logo, ImVec2(rad(0.034f), height * 0.028f), Col(0.52f, 0.55f, 0.59f), 0.30f, 24, 1.3f);
    dl->AddEllipse(logo, ImVec2(rad(0.018f), height * 0.055f), Col(0.52f, 0.55f, 0.59f), 0.75f, 24, 1.3f);
    const char* brand = "SEGA SATURN";
    const ImVec2 brandSize = ImGui::CalcTextSize(brand);
    dl->AddText(ImVec2(px(0.485f, 0.500f).x - brandSize.x * 0.5f,
                       px(0.485f, 0.500f).y - brandSize.y * 0.5f),
                Col(0.57f, 0.59f, 0.63f), brand);

    for (const Button& b : kButtons)
    {
        const ImVec2 c = px(b.cx, b.cy);
        const float wx = b.hx * width, wy = b.hy * height;
        const ImVec2 lo(c.x - wx, c.y - wy), hi(c.x + wx, c.y + wy);
        ImGui::SetCursorScreenPos(lo);
        ImGui::PushID(static_cast<int>(b.bit));
        ImGui::InvisibleButton("##button", ImVec2(wx * 2.0f, wy * 2.0f));
        const bool hovered = ImGui::IsItemHovered();
        if (interactive)
        {
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) mSources.latched ^= b.bit;
            if (mAutoHold)
            {
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) mSources.latched ^= b.bit;
            }
            else if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                mSources.mouseMomentary |= b.bit;
        }
        ImGui::SetItemTooltip("%s  (keyboard: %s)%s", b.name,
                              ImGui::GetKeyName((ImGuiKey)mKeyBind[&b - kButtons]),
                              (mSources.latched & b.bit) ? "  [held]" : "");
        ImGui::PopID();

        RecomputeFinal();
        const bool pressed = (mFinalState & b.bit) != 0;
        const bool held = (mSources.latched & b.bit) != 0;
        const bool isDpad = (b.bit & (SE_PAD_UP | SE_PAD_DOWN | SE_PAD_LEFT | SE_PAD_RIGHT)) != 0;
        const bool isStart = b.bit == SE_PAD_START;
        const bool isShoulder = b.bit == SE_PAD_L || b.bit == SE_PAD_R;
        ImU32 fill = isStart ? Col(0.09f, 0.20f, 0.48f) : Col(0.105f, 0.115f, 0.130f);
        if (interactive && hovered && !pressed) fill = Col(0.16f, 0.19f, 0.23f);
        if (pressed) fill = Col(0.10f, 0.38f, 0.82f);
        const ImU32 edge = (pressed || held) ? Col(0.16f, 0.58f, 1.0f)
                                             : Col(0.015f, 0.020f, 0.028f);
        if (isDpad)
        {
            if (pressed || hovered)
            {
                const float rounding = std::min(wx, wy) * 0.32f;
                dl->AddRectFilled(lo, hi, fill, rounding);
                if (pressed) dl->AddRect(lo, hi, edge, rounding, ImDrawFlags_None, 2.0f);
            }
            const float ar = std::min(wx, wy) * 0.36f;
            ImVec2 arrow[3];
            if (b.bit == SE_PAD_UP)
            {
                arrow[0] = {c.x, c.y - ar}; arrow[1] = {c.x - ar, c.y + ar * 0.55f};
                arrow[2] = {c.x + ar, c.y + ar * 0.55f};
            }
            else if (b.bit == SE_PAD_DOWN)
            {
                arrow[0] = {c.x, c.y + ar}; arrow[1] = {c.x - ar, c.y - ar * 0.55f};
                arrow[2] = {c.x + ar, c.y - ar * 0.55f};
            }
            else if (b.bit == SE_PAD_LEFT)
            {
                arrow[0] = {c.x - ar, c.y}; arrow[1] = {c.x + ar * 0.55f, c.y - ar};
                arrow[2] = {c.x + ar * 0.55f, c.y + ar};
            }
            else
            {
                arrow[0] = {c.x + ar, c.y}; arrow[1] = {c.x - ar * 0.55f, c.y - ar};
                arrow[2] = {c.x - ar * 0.55f, c.y + ar};
            }
            dl->AddTriangleFilled(arrow[0], arrow[1], arrow[2],
                                  pressed ? Col(0.68f, 0.83f, 1.0f) : Col(0.29f, 0.32f, 0.36f));
        }
        else if (b.round)
        {
            const float r = std::min(wx, wy) * 1.08f;
            dl->AddCircleFilled(ImVec2(c.x + 1.5f, c.y + 2.5f), r + 2.0f, shadow, 36);
            dl->AddCircleFilled(c, r, fill, 28);
            dl->AddCircle(c, r, edge, 32, (pressed || held) ? 2.6f : 2.0f);
            dl->PathArcTo(c, r - 3.0f, 3.55f, 5.95f, 18);
            dl->PathStroke(Col(0.30f, 0.32f, 0.35f), 1.0f);
        }
        else
        {
            const float rounding = isShoulder ? wy * 0.85f : std::min(wx, wy) * 0.75f;
            dl->AddRectFilled(ImVec2(lo.x + 1.5f, lo.y + 2.5f), ImVec2(hi.x + 1.5f, hi.y + 2.5f),
                              shadow, rounding);
            dl->AddRectFilled(lo, hi, fill, rounding);
            dl->AddRect(lo, hi, edge, rounding, ImDrawFlags_None,
                        (pressed || held) ? 2.6f : 1.8f);
            dl->AddLine(ImVec2(lo.x + rounding, lo.y + 3.0f),
                        ImVec2(hi.x - rounding, lo.y + 3.0f), Col(0.32f, 0.34f, 0.38f), 1.0f);
        }
        if (b.label[0])
        {
            const ImVec2 ts = ImGui::CalcTextSize(b.label);
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f),
                        pressed ? Col(0.92f, 0.97f, 1.0f) : Col(0.76f, 0.78f, 0.82f), b.label);
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
    ImGui::Dummy(ImVec2(width, 0.0f));
    RecomputeFinal();
}

void ControllerPanel::DrawFooter(bool connected) const
{
    ImGui::Separator();
    if (!connected) ImGui::TextDisabled("Disconnected. All controller input has been released.");
    else if (!mInputEnabled) ImGui::TextDisabled("Input disabled. Enable input to control the emulator.");
    else if (mPlaybackActive) ImGui::TextDisabled("Playback has exclusive control. Stop playback to use live input.");
    else ImGui::TextDisabled("Press a button or mapped key to send input. Right-click toggles hold.");
}

unsigned int ControllerPanel::Draw(bool connected, uint64_t frame, IPlatform& platform)
{
    (void)platform;   // key bindings now mirror the emulator live; no file dialog needed
    Update(connected, frame);
    DrawToolbar(connected);
    DrawControllerCanvas(connected);
    ObserveFrame(frame);
    DrawFooter(connected);
    DrawKeyBindings();
    return mFinalState;
}

void ControllerPanel::DrawCurrentInput(uint64_t frame)
{
    if (!ImGui::Begin("Current Input", &mShowCurrentInput)) { ImGui::End(); return; }
    ImGui::Text("Frame: %llu", static_cast<unsigned long long>(frame));
    ImGui::Separator();
    bool any = false;
    for (const Button& b : kButtons) if (mFinalState & b.bit)
    {
        if (any) ImGui::SameLine();
        ImGui::SmallButton(b.name);
        any = true;
    }
    if (!any) ImGui::TextDisabled("(empty)");
    if (mSources.latched)
        ImGui::TextDisabled("Held: %s", StateText(mSources.latched).c_str());
    ImGui::End();
}

void ControllerPanel::DrawQueue()
{
    if (!ImGui::Begin("Input Queue", &mShowQueue)) { ImGui::End(); return; }
    if (ImGui::BeginTable("##queue", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Input");
        ImGui::TableHeadersRow();
        const size_t count = std::min<size_t>(10, mHistory.size());
        for (size_t n = 0; n < count; ++n)
        {
            const FrameState& s = mHistory[mHistory.size() - 1 - n];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(s.frame));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(StateText(s.buttons).c_str());
        }
        if (!count) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextDisabled("(empty)"); }
        ImGui::EndTable();
    }
    ImGui::End();
}

void ControllerPanel::DrawTimeline(uint64_t frame)
{
    if (!ImGui::Begin("Input Timeline", &mShowTimeline)) { ImGui::End(); return; }
    ImGui::Text("Frame: %llu", static_cast<unsigned long long>(frame));
    const size_t first = mHistory.size() > 180 ? mHistory.size() - 180 : 0;
    const float cell = 5.0f;
    size_t playhead = mHistory.empty() ? 0 : mHistory.size() - 1;
    for (size_t n = first; n < mHistory.size(); ++n)
        if (mHistory[n].frame >= frame) { playhead = n; break; }
    ImGui::BeginChild("##timeline_scroll", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (ImGui::BeginTable("##timeline", 2, ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("Frames", ImGuiTableColumnFlags_WidthFixed,
                                std::max(300.0f, (mHistory.size() - first) * cell));
        for (const Button& b : kButtons)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(b.name);
            ImGui::TableNextColumn();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetTextLineHeight();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (size_t n = first; n < mHistory.size(); ++n)
            {
                const float x = p.x + (n - first) * cell;
                if (mHistory[n].buttons & b.bit)
                    dl->AddRectFilled(ImVec2(x, p.y + 2), ImVec2(x + cell, p.y + h - 2),
                                      Col(0.18f, 0.48f, 0.88f));
            }
            if (!mHistory.empty())
            {
                const float x = p.x + (playhead - first) * cell;
                dl->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + h), Col(0.20f, 0.78f, 1.0f), 1.5f);
            }
            ImGui::Dummy(ImVec2(std::max(300.0f, (mHistory.size() - first) * cell), h));
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::End();
}

bool ControllerPanel::SaveRecording(IPlatform& platform) const
{
    std::ostringstream out;
    out << "{\n  \"format\": \"saturn-input-recording\",\n  \"version\": 1,\n"
           "  \"system\": \"sega_saturn\",\n  \"frames\": [\n";
    for (size_t i = 0; i < mRecording.size(); ++i)
    {
        const FrameState& f = mRecording[i];
        out << "    {\"frame\": " << f.frame << ", \"mask\": " << f.buttons
            << ", \"buttons\": [";
        bool comma = false;
        for (const Button& b : kButtons) if (f.buttons & b.bit)
        { if (comma) out << ", "; out << '\"' << b.name << '\"'; comma = true; }
        out << "]}" << (i + 1 == mRecording.size() ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
    const std::string json = out.str();
    return platform.SaveFile("saturn-input.json", json.data(), json.size());
}

bool ControllerPanel::LoadRecording(IPlatform& platform)
{
    std::string path;
    if (!platform.OpenFileDialogFiltered(path, "Saturn input recording", "json")) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (text.find("\"format\": \"saturn-input-recording\"") == std::string::npos) return false;
    std::vector<FrameState> loaded;
    size_t pos = 0;
    while ((pos = text.find("\"frame\"", pos)) != std::string::npos)
    {
        const size_t fc = text.find(':', pos), mp = text.find("\"mask\"", fc);
        const size_t mc = mp == std::string::npos ? mp : text.find(':', mp);
        if (fc == std::string::npos || mp == std::string::npos || mc == std::string::npos) break;
        FrameState s;
        s.frame = std::strtoull(text.c_str() + fc + 1, nullptr, 10);
        s.buttons = static_cast<unsigned int>(std::strtoul(text.c_str() + mc + 1, nullptr, 10)) & SE_PAD_ALL;
        loaded.push_back(s);
        pos = mc + 1;
    }
    if (loaded.empty()) return false;
    StopPlayback();
    mRecording = std::move(loaded);
    return true;
}

void ControllerPanel::DrawRecording(IPlatform& platform)
{
    if (!ImGui::Begin("Input Recording", &mShowRecording)) { ImGui::End(); return; }
    if (ImGui::Button(mRecordingActive ? "Stop Recording" : "Record"))
    {
        if (!mRecordingActive)
        {
            StopPlayback();
            mRecording.clear();
        }
        mRecordingActive = !mRecordingActive;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(mRecording.empty() || mRecordingActive);
    if (ImGui::Button(mPlaybackActive ? "Stop Playback" : "Play"))
    {
        if (mPlaybackActive)
        {
            StopPlayback();
        }
        else
        {
            mPlaybackActive = true;
            mPlaybackIndex = 0;
            mPlaybackStartFrame = mLastObservedFrame == std::numeric_limits<uint64_t>::max()
                                      ? 0 : mLastObservedFrame + 1;
            mSources.mouseMomentary = mSources.keyboardMomentary = mSources.latched = mSources.macro = 0;
            mMacroIndex = -1;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::Checkbox("Loop", &mPlaybackLoop);
    int policy = static_cast<int>(mPlaybackPolicy);
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::Combo("Policy", &policy, "Overlay\0Exclusive\0Override\0"))
        mPlaybackPolicy = static_cast<PlaybackPolicy>(policy);
    if (ImGui::Button("Save...")) SaveRecording(platform);
    ImGui::SameLine();
    if (ImGui::Button("Load...")) LoadRecording(platform);
    ImGui::TextDisabled("%zu frame%s captured", mRecording.size(), mRecording.size() == 1 ? "" : "s");
    ImGui::End();
}

void ControllerPanel::DrawMacros()
{
    if (!ImGui::Begin("Macros", &mShowMacros)) { ImGui::End(); return; }
    ImGui::SetNextItemWidth(-130.0f);
    ImGui::InputText("##macro_name", mMacroName, sizeof(mMacroName));
    ImGui::SameLine();
    if (ImGui::Button("Add Current"))
    {
        Macro m; m.name = mMacroName[0] ? mMacroName : "Macro"; m.states.push_back(mFinalState);
        mMacros.push_back(std::move(m));
    }
    for (size_t i = 0; i < mMacros.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton("Play"))
        {
            mMacroIndex = static_cast<int>(i);
            mMacroStep = 0;
            mMacroStartFrame = mLastObservedFrame == std::numeric_limits<uint64_t>::max()
                                 ? 0 : mLastObservedFrame + 1;
        }
        ImGui::SameLine(); ImGui::TextUnformatted(mMacros[i].name.c_str());
        if (ImGui::BeginPopupContextItem("##macro_menu"))
        {
            if (ImGui::MenuItem("Duplicate")) mMacros.push_back(mMacros[i]);
            if (ImGui::MenuItem("Delete")) { mMacros.erase(mMacros.begin() + i); ImGui::EndPopup(); ImGui::PopID(); break; }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (mMacros.empty()) ImGui::TextDisabled("No macros defined.");
    ImGui::End();
}

void ControllerPanel::DrawStatistics(bool connected)
{
    if (!ImGui::Begin("Statistics", &mShowStatistics)) { ImGui::End(); return; }
    ImGui::Text("Connection: %s", connected ? "Connected" : "Disconnected");
    ImGui::Text("State updates submitted: %llu", static_cast<unsigned long long>(mPacketsSent));
    ImGui::Text("State changes submitted: %llu", static_cast<unsigned long long>(mChangesSent));
    const uint64_t span = mLastObservedFrame > mFirstSendFrame ? mLastObservedFrame - mFirstSendFrame : 0;
    ImGui::Text("Average submission rate: %.1f / second", span ? (mPacketsSent * 60.0 / span) : 0.0);
    ImGui::Text("Dropped/rejected states: not reported");
    if (mLastSendTime < 0.0) ImGui::Text("Last send age: never");
    else ImGui::Text("Last send age: %.0f ms", (ImGui::GetTime() - mLastSendTime) * 1000.0);
    ImGui::End();
}

void ControllerPanel::DrawAuxiliary(bool connected, uint64_t liveFrame, uint64_t viewedFrame,
                                    IPlatform& platform)
{
    Update(connected, liveFrame);
    ObserveFrame(liveFrame);
    const bool before[] = {mShowCurrentInput, mShowTimeline, mShowQueue,
                           mShowRecording, mShowMacros, mShowStatistics};
    if (mShowCurrentInput) DrawCurrentInput(liveFrame);
    if (mShowTimeline) DrawTimeline(viewedFrame);
    if (mShowQueue) DrawQueue();
    if (mShowRecording) DrawRecording(platform);
    if (mShowMacros) DrawMacros();
    if (mShowStatistics) DrawStatistics(connected);
    const bool after[] = {mShowCurrentInput, mShowTimeline, mShowQueue,
                          mShowRecording, mShowMacros, mShowStatistics};
    for (int i = 0; i < 6; ++i) if (before[i] != after[i]) mSettingsDirty = true;
}

}  // namespace sfe
