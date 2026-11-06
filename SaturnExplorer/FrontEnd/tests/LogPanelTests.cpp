// Log panel: selecting a row opens the detail pane (A12-26), driven headlessly (ImGuiHarness.h).
//
// Windows verify4 left-clicked a row and saw it highlighted but no detail pane below the table. This drives the real LogPanel::Draw
// the way the app does (one window, the panel inside it), clicks a row, and asks whether the pane's child window exists and has
// room, at the window sizes the app starts with and at a short one.
#include <cstdio>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuiHarness.h"
#include "LogPanel.h"

using namespace sfe;

namespace
{
int gFailures;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)

struct Rig
{
    LogPanel panel;
    float windowH;
    LogPanel::Request req;
    ImVec2 firstRow {};
    explicit Rig(float h) : windowH(h)
    {
        panel.Info("Saturn Explorer started");
        panel.Error("The function names: not saved, cannot create or lock C:\\x\\saturn_functions.txt.lock: Access is denied");
        panel.Warn("second warning");
    }
    void Draw()
    {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(900.0f, windowH));
        ImGui::Begin("Log", nullptr, ImGuiWindowFlags_NoSavedSettings);
        panel.Draw(req);
        ImGui::End();
    }
    // The detail pane's child window, if it was submitted: its height, or -1.
    static float DetailHeight()
    {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        for (ImGuiWindow* w : g.Windows)
            if (w && w->Active && std::string(w->Name).find("##logdetail") != std::string::npos) return w->Size.y;
        return -1.0f;
    }
};


void ClickRowAndLook(float windowH, const char* label)
{
    Rig rig(windowH);
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    CHECK(Rig::DetailHeight() < 0.0f);   // nothing selected: no pane
    // Try each text line under the header: one of them is a row. Any click on a row (its Time cell or its Message cell,
    // the Selectable spans all columns) must select it.
    float found = -1.0f;
    for (int line = 0; line < 8 && found < 0.0f; ++line)
    {
        for (float x : { 40.0f, 400.0f })
        {
            const ImVec2 p(x, ImGui::GetFontSize() * 0.0f + 52.0f + 20.0f * line);
            harness.Hover(p);
            harness.Click(p);
            harness.Frame(p, false);
            harness.Frame(p, false);
            const float h = Rig::DetailHeight();
            if (h >= 0.0f) { found = h; break; }
        }
    }
    std::printf("%s: detail pane height %.1f (window %.0f)\n", label, found, windowH);
    // More than a sliver: before the fix a 170-high Log window left the pane 15 px (A12-26).
    CHECK(found >= 25.0f);
}
}  // namespace

int main()
{
    ClickRowAndLook(600.0f, "tall window");
    ClickRowAndLook(300.0f, "short window");
    for (float h : { 240.0f, 200.0f, 170.0f }) ClickRowAndLook(h, "small window");
    if (gFailures == 0) std::printf("LogPanel: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
