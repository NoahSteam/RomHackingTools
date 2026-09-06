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

#include <functional>

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
    }

    ~ImGuiHarness() { ImGui::DestroyContext(mContext); }

    ImGuiHarness(const ImGuiHarness&) = delete;
    ImGuiHarness& operator=(const ImGuiHarness&) = delete;

    // One frame with the mouse at 'mouse' and the left button in state 'down'.
    void Frame(ImVec2 mouse, bool down)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(0, down);
        ImGui::NewFrame();
        mUi();
        ImGui::Render();
    }

    // Run frames with the pointer parked off the widgets, so sizes and positions settle
    // and nothing is hovered. Item rects captured by 'ui' are valid afterwards.
    void Settle(int frames = 3) { for (int i = 0; i < frames; ++i) Frame(kAway, false); }

    // Hold the pointer over 'p'. Items using AllowOverlap need the previous frame's
    // hovered id to match before they report hovered, so more than one frame is required.
    void Hover(ImVec2 p, int frames = 3) { for (int i = 0; i < frames; ++i) Frame(p, false); }

    // A full press + release at 'p'. Assertions about activation want the press frame;
    // Selectable/Button fire on release, so both are driven.
    void Click(ImVec2 p) { Frame(p, true); Frame(p, false); }

    // Press only, leaving the button held — for asserting what a click activated.
    void Press(ImVec2 p) { Frame(p, true); }

private:
    static constexpr ImVec2 kAway = ImVec2(1270.0f, 710.0f);

    std::function<void()> mUi;
    ImGuiContext* mContext = nullptr;
};

}  // namespace sfe
