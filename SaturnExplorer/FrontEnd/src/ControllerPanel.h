// ControllerPanel — deterministic Sega Saturn virtual input and its optional tools.
// Input producers own separate masks; the final transmitted mask is an arbitration
// result, never widget-local state. Optional tools are independent dockable windows.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sfe
{

class IPlatform;
class Settings;

class ControllerPanel
{
public:
    // Draw the primary Controller window contents and return the state to transmit.
    unsigned int Draw(bool liveConnected, uint64_t frame, IPlatform& platform);
    // Draw optional dockable tools even when the main Controller window is hidden.
    void DrawAuxiliary(bool liveConnected, uint64_t liveFrame, uint64_t viewedFrame,
                       IPlatform& platform);

    void Load(const Settings& settings);
    void Save(Settings& settings) const;
    bool ConsumeSettingsDirty();

    // Called by App after a state is handed to the live connection.
    void NotifyStateSent(uint64_t frame, unsigned int state);
    void ClearAll();
    // Release user-held input without interrupting deterministic playback or macros.
    void ReleaseManualInput();
    bool ConsumeResetLayoutRequest();

    int Port() const { return mPort; }
    unsigned int FinalState() const { return mFinalState; }

private:
    struct Sources
    {
        unsigned int mouseMomentary = 0;
        unsigned int keyboardMomentary = 0;
        unsigned int latched = 0;
        unsigned int macro = 0;
        unsigned int playback = 0;
    };
    struct FrameState { uint64_t frame = 0; unsigned int buttons = 0; };
    struct Macro
    {
        std::string name;
        std::vector<unsigned int> states;
    };

    enum class PlaybackPolicy { Overlay = 0, Exclusive = 1, Override = 2 };

    void Update(bool liveConnected, uint64_t frame);
    void StopPlayback();
    void RecomputeFinal();
    void ObserveFrame(uint64_t frame);
    void DrawToolbar(bool liveConnected);
    void DrawControllerCanvas(bool liveConnected);
    void DrawFooter(bool liveConnected) const;
    void DrawViewMenu();
    // Configurable keyboard bindings: initialise from the default layout and draw the
    // rebind UI. The live path (ApplyLiveKeyMap) mirrors Mednafen's own bindings.
    void EnsureBindings();
    void DrawKeyBindings();
public:
    // Adopt the emulator's live host keyboard bindings so the panel's keys match the
    // user's Mednafen config automatically (e.g. WASD) — no config-file upload. 'scancodes'
    // holds USB-HID scancodes in ascending SE_PAD_* order (see se_live_poll_keymap), -1
    // where unbound; 'count' is how many entries are valid. Re-adopts only when the
    // reported mapping actually changes, so a manual rebind isn't clobbered every frame.
    void ApplyLiveKeyMap(const int32_t* scancodes, int count);
private:

    void DrawCurrentInput(uint64_t frame);
    void DrawTimeline(uint64_t frame);
    void DrawQueue();
    void DrawRecording(IPlatform& platform);
    void DrawMacros();
    void DrawStatistics(bool liveConnected);
    bool SaveRecording(IPlatform& platform) const;
    bool LoadRecording(IPlatform& platform);

    Sources      mSources;
    unsigned int mFinalState = 0;
    unsigned int mLastObservedState = 0;
    uint64_t     mLastObservedFrame = ~uint64_t(0);
    uint64_t     mLastUpdateFrame = ~uint64_t(0);
    bool         mWasConnected = false;

    // Per-button keyboard bindings (ImGuiKey stored as int to keep imgui.h out of this
    // header), parallel to the button table in the .cpp. Defaults set by EnsureBindings;
    // user-editable via DrawKeyBindings; persisted. mRebindIndex >= 0 = capturing a key.
    static constexpr int kNumButtons = 13;
    int          mKeyBind[kNumButtons] = {0};
    bool         mBindingsInit = false;
    int          mRebindIndex = -1;
    char         mBindMsg[96] = {};      // transient status after a live adopt / reset
    // Last live keyboard map adopted from the emulator (USB-HID scancodes in wire order),
    // so ApplyLiveKeyMap re-adopts only when Mednafen's config actually changes and a
    // manual rebind survives between changes. mLiveKeyMapAdopted gates the first compare.
    int32_t      mAdoptedScancodes[kNumButtons] = {0};
    bool         mLiveKeyMapAdopted = false;

    int          mPort = 0;
    int          mInputSource = 0;       // 0 keyboard + mouse, 1 mouse only
    bool         mInputEnabled = true;
    bool         mAutoHold = false;
    bool         mSettingsDirty = false;
    bool         mResetLayoutRequested = false;

    // Optional tool visibility: all hidden by default (progressive disclosure).
    bool mShowCurrentInput = false;
    bool mShowTimeline = false;
    bool mShowQueue = false;
    bool mShowRecording = false;
    bool mShowMacros = false;
    bool mShowStatistics = false;

    std::vector<FrameState> mHistory;
    std::vector<FrameState> mRecording;
    bool                    mRecordingActive = false;
    bool                    mPlaybackActive = false;
    bool                    mPlaybackLoop = false;
    PlaybackPolicy          mPlaybackPolicy = PlaybackPolicy::Exclusive;
    size_t                  mPlaybackIndex = 0;
    uint64_t                mPlaybackStartFrame = 0;

    std::vector<Macro> mMacros;
    int                mMacroIndex = -1;
    size_t             mMacroStep = 0;
    uint64_t           mMacroStartFrame = 0;

    uint64_t mPacketsSent = 0;
    uint64_t mChangesSent = 0;
    uint64_t mFirstSendFrame = 0;
    unsigned int mLastSentState = 0;
    double   mLastSendTime = -1.0;
    char     mMacroName[64] = "New Macro";
};

}  // namespace sfe
