// ImGuiHarness — drive Dear ImGui headlessly so panel *interaction* can be tested.
//
// Layout, hit testing, hover, activation and keyboard routing all live in ImGui's core and
// need no renderer or window: a context with a display size, synthetic input events, and a
// NewFrame/Render pair per frame is enough. That makes it possible to assert on things you
// otherwise have to check by hand in the running app -- whether a click actually reaches a
// widget, whether an overlapping item steals it, whether typing goes where you expect.
//
// Interaction is stateful across frames (ImGui compares against the previous frame's
// hovered/active ids, and a click is a press frame followed by a release frame), so drive
// several frames rather than one: Settle() to establish layout, then Hover()/Click().
#pragma once

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

#include "imgui.h"

namespace sfe
{

class ImGuiHarness
{
public:
    // 'ui' is submitted inside every frame; it should build the widgets under test and
    // record whatever the assertions need.
    explicit ImGuiHarness(std::function<void()> ui) : mUi(std::move(ui))
    {
        IMGUI_CHECKVERSION();
        mContext = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;          // no imgui.ini side effects from a test
        io.LogFilename = nullptr;
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        // Tell ImGui the "backend" handles texture creation; without this the font atlas
        // wants a real upload before it will build.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        // A context with no backend gets ImGui's *default* clipboard handlers, and on Windows those are
        // OpenClipboard/GetClipboardData on the real desktop clipboard (imgui.cpp, "Win32 clipboard
        // implementation"). A test that Copies and reads back then races every clipboard manager and the
        // user's own Ctrl+C, and reads "" whenever it loses. Give the harness a private in-memory clipboard
        // so no test ever reaches the OS one; production never constructs a harness, so the app's
        // clipboard is untouched.
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        pio.Platform_GetClipboardTextFn = &GetClip;
        pio.Platform_SetClipboardTextFn = &SetClip;
        pio.Platform_ClipboardUserData = &mClipboard;
    }

    ~ImGuiHarness()
    {
        // A test (or a backend init pulled into it) that swapped the handlers back would reach the OS
        // clipboard; fail loudly instead of flaking on one platform only.
        if (!ClipboardIsInMemory())
        {
            std::fprintf(stderr, "ImGuiHarness: the in-memory clipboard handlers were replaced during the test; "
                                 "the test may have touched the OS clipboard\n");
            std::abort();
        }
        ImGui::DestroyContext(mContext);
    }

    // True while this harness's in-memory clipboard handlers are the ones installed on its context.
    bool ClipboardIsInMemory() const
    {
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(mContext);
        const ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        const bool ok = pio.Platform_GetClipboardTextFn == &GetClip && pio.Platform_SetClipboardTextFn == &SetClip &&
                        pio.Platform_ClipboardUserData == &mClipboard;
        ImGui::SetCurrentContext(prev);
        return ok;
    }

    ImGuiHarness(const ImGuiHarness&) = delete;
    ImGuiHarness& operator=(const ImGuiHarness&) = delete;

    // One frame with the mouse at 'mouse' and the left button in state 'down'.
    void Frame(ImVec2 mouse, bool down) { FrameButton(mouse, 0, down); }

    // As Frame, but drives an arbitrary button: 0 left, 1 right, 2 middle. Context menus
    // (BeginPopupContextItem) open on the right button, so they cannot be driven by Frame.
    void FrameButton(ImVec2 mouse, int button, bool down)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(button, down);
        ImGui::NewFrame();
        mUi();
        ImGui::Render();
    }

    // Run frames with the pointer parked off the widgets, so sizes and positions settle
    // and nothing is hovered. Item rects captured by 'ui' are valid afterwards.
    void Settle() { for (int i = 0; i < kSettleFrames; ++i) Frame(Away(), false); }

    // Hold the pointer over 'p'. Items using AllowOverlap need the previous frame's
    // hovered id to match before they report hovered, so more than one frame is required.
    void Hover(ImVec2 p) { for (int i = 0; i < kSettleFrames; ++i) Frame(p, false); }

    // A full press + release at 'p'. Assertions about activation want the press frame;
    // Selectable/Button fire on release, so both are driven.
    void Click(ImVec2 p) { Frame(p, true); Frame(p, false); }

    // Press only, leaving the button held — for asserting what a click activated.
    void Press(ImVec2 p) { Frame(p, true); }

    // A full right-button press + release at 'p'. BeginPopupContextItem opens on release,
    // and the popup only becomes visible on the frame after, so one settled frame follows.
    void RightClick(ImVec2 p)
    {
        FrameButton(p, 1, true);
        FrameButton(p, 1, false);
        FrameButton(p, 1, false);
    }

private:
    static const char* GetClip(ImGuiContext*)
    {
        const std::string* c = static_cast<const std::string*>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
        return c ? c->c_str() : nullptr;
    }
    static void SetClip(ImGuiContext*, const char* text)
    {
        if (std::string* c = static_cast<std::string*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)) *c = text ? text : "";
    }

    // A point parked off the widgets under test. A static function rather than a
    // static constexpr member so it needs no out-of-line definition when odr-used
    // (required in C++14, the standard this project builds with).
    static ImVec2 Away() { return ImVec2(1270.0f, 710.0f); }

    // Enough frames for ImGui to settle: AllowOverlap items compare against the previous
    // frame's hovered id, so a single frame is never enough to establish hover.
    static const int kSettleFrames = 3;

    std::function<void()> mUi;
    ImGuiContext* mContext = nullptr;
    std::string mClipboard;
};

}  // namespace sfe
