#include "ControllerPanel.h"

#include "imgui.h"
#include "SeLiveProtocol.h"   // SE_PAD_* button bits

namespace sfe
{
namespace
{

// A pressable region on the pad, positioned in a normalized 0..1 landscape box that
// is scaled to the actual panel width. Round buttons draw as circles; the rest as
// rounded rects. Hit-testing uses the (square-ish) bounding box for all of them.
struct Button
{
    unsigned int bit;
    const char*  label;
    float        cx, cy;    // center, normalized (x in [0,1] of width, y in [0,1] of a
                            // box whose height is kAspect * width)
    float        hx, hy;    // half-extents, same normalized units
    bool         round;
    ImGuiKey     key;       // default keyboard binding (active while window focused)
};

constexpr float kAspect = 0.46f;   // drawing box height / width

// Saturn pad layout. D-pad cross on the left (four arms), START oval centered low,
// six face buttons on the right in the Saturn arc (X Y Z over A B C), L/R along the top.
const Button kButtons[] = {
    // D-pad (bit, label, cx, cy, hx, hy, round, key)
    {SE_PAD_UP,    "",  0.200f, 0.360f, 0.035f, 0.075f, false, ImGuiKey_UpArrow},
    {SE_PAD_DOWN,  "",  0.200f, 0.620f, 0.035f, 0.075f, false, ImGuiKey_DownArrow},
    {SE_PAD_LEFT,  "",  0.120f, 0.490f, 0.045f, 0.055f, false, ImGuiKey_LeftArrow},
    {SE_PAD_RIGHT, "",  0.280f, 0.490f, 0.045f, 0.055f, false, ImGuiKey_RightArrow},
    // Face buttons — top row X Y Z, bottom row A B C, arced upward to the right.
    {SE_PAD_X, "X", 0.640f, 0.470f, 0.045f, 0.045f, true, ImGuiKey_A},
    {SE_PAD_Y, "Y", 0.730f, 0.430f, 0.045f, 0.045f, true, ImGuiKey_S},
    {SE_PAD_Z, "Z", 0.820f, 0.400f, 0.045f, 0.045f, true, ImGuiKey_D},
    {SE_PAD_A, "A", 0.640f, 0.640f, 0.045f, 0.045f, true, ImGuiKey_Z},
    {SE_PAD_B, "B", 0.730f, 0.600f, 0.045f, 0.045f, true, ImGuiKey_X},
    {SE_PAD_C, "C", 0.820f, 0.570f, 0.045f, 0.045f, true, ImGuiKey_C},
    // START (blue oval) centered low.
    {SE_PAD_START, "START", 0.500f, 0.660f, 0.070f, 0.035f, false, ImGuiKey_Enter},
    // Shoulder buttons along the top edge.
    {SE_PAD_L, "L", 0.170f, 0.130f, 0.090f, 0.030f, false, ImGuiKey_Q},
    {SE_PAD_R, "R", 0.720f, 0.130f, 0.090f, 0.030f, false, ImGuiKey_E},
};

ImU32 Col(float r, float g, float b, float a = 1.0f)
{
    return ImGui::GetColorU32(ImVec4(r, g, b, a));
}

}  // namespace

unsigned int ControllerPanel::Draw(bool liveConnected)
{
    // --- controls row ---
    ImGui::Text("Controller");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    const char* ports[] = {"Controller 1", "Controller 2"};
    ImGui::Combo("##port", &mPort, ports, 2);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-hold", &mAutoHold);
    ImGui::SameLine();
    ImGui::Checkbox("Keyboard", &mKeyboard);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) mLatched = 0;

    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // --- pad drawing area ---
    // Fit the pad within BOTH the available width and height (its height is
    // kAspect*width), reserving one text line below for the status, so a short dock
    // (e.g. the bottom strip) shows the whole pad instead of clipping it. Center it.
    const float availW = ImGui::GetContentRegionAvail().x;
    const float availH = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing();
    float width = availW;
    if (availH > 0.0f && width * kAspect > availH) width = availH / kAspect;
    if (width < 1.0f) width = availW;      // degenerate dock; draw at full width
    const float height = width * kAspect;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    origin.x += (availW - width) * 0.5f;   // center horizontally
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto px = [&](float nx, float ny) { return ImVec2(origin.x + nx * width, origin.y + ny * height); };

    // Pad shell (rounded rectangle backdrop).
    dl->AddRectFilled(px(0.02f, 0.02f), px(0.98f, 0.98f), Col(0.62f, 0.63f, 0.65f),
                      height * 0.12f);
    dl->AddRect(px(0.02f, 0.02f), px(0.98f, 0.98f), Col(0.35f, 0.36f, 0.38f),
                height * 0.12f, 0, 2.0f);

    unsigned int mask = 0;
    unsigned int newLatch = mLatched;

    for (const Button& b : kButtons)
    {
        const ImVec2 c = px(b.cx, b.cy);
        const float wx = b.hx * width;
        const float wy = b.hy * height;
        const ImVec2 lo(c.x - wx, c.y - wy);
        const ImVec2 hi(c.x + wx, c.y + wy);

        // Invisible hit region for mouse press/hold (bit is a unique, stable id).
        ImGui::SetCursorScreenPos(lo);
        ImGui::PushID(static_cast<int>(b.bit));
        ImGui::InvisibleButton("##btn", ImVec2(wx * 2.0f, wy * 2.0f));
        ImGui::PopID();
        const bool mouseHeld = ImGui::IsItemActive();
        if (mAutoHold && ImGui::IsItemDeactivated() && ImGui::IsItemHovered())
        {
            newLatch ^= b.bit;   // click toggles the latch in auto-hold mode
        }

        const bool keyHeld = mKeyboard && windowFocused && ImGui::IsKeyDown(b.key);
        const bool latched = (mLatched & b.bit) != 0;
        const bool pressed = mouseHeld || keyHeld || latched;
        if (pressed) mask |= b.bit;

        // Draw the button, highlighted when pressed.
        const bool isStart = b.bit == SE_PAD_START;
        ImU32 fill = isStart ? (pressed ? Col(0.35f, 0.65f, 1.0f) : Col(0.16f, 0.34f, 0.72f))
                             : (pressed ? Col(0.35f, 0.85f, 0.45f) : Col(0.28f, 0.29f, 0.31f));
        if (b.round)
        {
            const float rad = (wx < wy ? wx : wy);
            dl->AddCircleFilled(c, rad, fill, 24);
            dl->AddCircle(c, rad, Col(0.12f, 0.12f, 0.13f), 24, 1.5f);
        }
        else
        {
            dl->AddRectFilled(lo, hi, fill, wy * 0.4f);
            dl->AddRect(lo, hi, Col(0.12f, 0.12f, 0.13f), wy * 0.4f, 0, 1.5f);
        }
        if (b.label[0])
        {
            const ImVec2 ts = ImGui::CalcTextSize(b.label);
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f),
                        Col(0.95f, 0.95f, 0.97f), b.label);
        }
    }

    mLatched = newLatch & SE_PAD_ALL;

    // Reserve the drawing area's height so following widgets don't overlap the pad.
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
    ImGui::Dummy(ImVec2(width, 0.0f));

    // Status line.
    if (!liveConnected)
        ImGui::TextDisabled("Not connected — connect to a running emulator to send input.");
    else if (mask)
        ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.5f, 1.0f), "Sending input to Controller %d", mPort + 1);
    else
        ImGui::TextDisabled("Press a button (mouse or keyboard) to send input.");

    return mask;
}

}  // namespace sfe
