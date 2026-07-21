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
    // D-pad cross (left), four arms meeting at the hub near (0.19, 0.42).
    {SE_PAD_UP,    "",  0.190f, 0.310f, 0.046f, 0.076f, false, ImGuiKey_UpArrow},
    {SE_PAD_DOWN,  "",  0.190f, 0.520f, 0.046f, 0.076f, false, ImGuiKey_DownArrow},
    {SE_PAD_LEFT,  "",  0.102f, 0.415f, 0.076f, 0.046f, false, ImGuiKey_LeftArrow},
    {SE_PAD_RIGHT, "",  0.278f, 0.415f, 0.076f, 0.046f, false, ImGuiKey_RightArrow},
    // Six face buttons in the Saturn arc: bottom row A B C, top row X Y Z, sweeping
    // up to the right. dish centered ~(0.72, 0.41).
    {SE_PAD_X, "X", 0.615f, 0.370f, 0.047f, 0.047f, true, ImGuiKey_A},
    {SE_PAD_Y, "Y", 0.710f, 0.320f, 0.047f, 0.047f, true, ImGuiKey_S},
    {SE_PAD_Z, "Z", 0.805f, 0.285f, 0.047f, 0.047f, true, ImGuiKey_D},
    {SE_PAD_A, "A", 0.640f, 0.535f, 0.047f, 0.047f, true, ImGuiKey_Z},
    {SE_PAD_B, "B", 0.735f, 0.485f, 0.047f, 0.047f, true, ImGuiKey_X},
    {SE_PAD_C, "C", 0.830f, 0.450f, 0.047f, 0.047f, true, ImGuiKey_C},
    // START (blue oval) centered, between the clusters and above the grips.
    {SE_PAD_START, "START", 0.460f, 0.560f, 0.058f, 0.030f, false, ImGuiKey_Enter},
    // Shoulder buttons along the (flatter) top edge.
    {SE_PAD_L, "L", 0.150f, 0.095f, 0.088f, 0.030f, false, ImGuiKey_Q},
    {SE_PAD_R, "R", 0.760f, 0.095f, 0.088f, 0.030f, false, ImGuiKey_E},
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
    // A circle radius given as a fraction of the pad WIDTH (so it's round on screen).
    auto rad = [&](float f) { return f * width; };

    // ---- Shell: a light-grey Saturn pad — a rounded body with the flatter shoulder
    // edge (L/R) along the TOP and two rounded grip lobes at the BOTTOM corners, plus
    // a soft top bevel and bottom shading for depth. ----
    const ImU32 shell   = Col(0.72f, 0.72f, 0.74f);
    const ImU32 shellLo = Col(0.60f, 0.60f, 0.62f);
    dl->AddCircleFilled(px(0.17f, 0.70f), rad(0.125f), shell, 40);   // bottom-left grip
    dl->AddCircleFilled(px(0.83f, 0.70f), rad(0.125f), shell, 40);   // bottom-right grip
    dl->AddRectFilled(px(0.05f, 0.06f), px(0.95f, 0.78f), shell, height * 0.28f);
    dl->AddRectFilled(px(0.05f, 0.56f), px(0.95f, 0.78f), shellLo, height * 0.28f,
                      ImDrawFlags_RoundCornersBottom);           // subtle bottom shading
    dl->AddRectFilled(px(0.08f, 0.10f), px(0.92f, 0.26f), Col(1.0f, 1.0f, 1.0f, 0.06f),
                      height * 0.18f, ImDrawFlags_RoundCornersTop); // top bevel highlight

    // ---- Recessed dishes behind the D-pad and the face buttons (depth). ----
    dl->AddCircleFilled(px(0.19f, 0.415f), rad(0.135f), Col(0.10f, 0.10f, 0.11f, 0.55f), 40);
    dl->AddCircleFilled(px(0.72f, 0.405f), rad(0.165f), Col(0.10f, 0.10f, 0.11f, 0.45f), 40);

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

        // Base colours: START is the Saturn's blue oval; face + D-pad are charcoal.
        // A pressed button lights up green (blue-white for START) — the highlight.
        const bool isStart = b.bit == SE_PAD_START;
        const ImU32 fill = isStart ? (pressed ? Col(0.45f, 0.72f, 1.0f) : Col(0.15f, 0.33f, 0.70f))
                                   : (pressed ? Col(0.36f, 0.86f, 0.46f) : Col(0.24f, 0.25f, 0.27f));
        const ImU32 edge = Col(0.09f, 0.09f, 0.10f);
        if (b.round)
        {
            const float r = (wx < wy ? wx : wy);
            dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.10f), r, Col(0.0f, 0.0f, 0.0f, 0.35f), 28); // drop shadow
            dl->AddCircleFilled(c, r, fill, 28);
            dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.32f), r * 0.62f,
                                Col(1.0f, 1.0f, 1.0f, 0.10f), 28);   // glossy top highlight
            dl->AddCircle(c, r, edge, 28, 2.0f);
        }
        else
        {
            const float r = wy < wx ? wy * 0.5f : wx * 0.5f;
            dl->AddRectFilled(lo, hi, fill, r);
            dl->AddRect(lo, hi, edge, r, 0, 2.0f);
        }
        if (b.label[0])
        {
            const ImVec2 ts = ImGui::CalcTextSize(b.label);
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f),
                        isStart ? Col(0.95f, 0.97f, 1.0f) : Col(0.92f, 0.93f, 0.95f), b.label);
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
