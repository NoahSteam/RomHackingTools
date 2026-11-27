// A layout saved before a window existed must dock that window beside its neighbour, not open it floating.
//
// Builds an "old" layout (Watch below, Memory above -- no Structs, no Memory Compare), saves it as imgui.ini
// text, then loads that text into a fresh context the way a user's old imgui.ini is, and runs the same
// adoption App::AdoptNewPanels runs. Only one ImGuiHarness is alive at a time (each owns the ImGui context).

#include <cstdio>
#include <string>

#include "ImGuiHarness.h"
#include "LayoutAdoption.h"

using namespace sfe;

namespace
{
int gFailures;

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)

void EnableDocking() { ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable; }

// The saved text of a layout that predates Structs and Memory Compare.
std::string OldLayoutIni()
{
    bool built = false;
    std::string ini;
    {
        ImGuiHarness h([&] {
            const ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
            if (!built)
            {
                built = true;
                ImGui::DockBuilderRemoveNode(dock);
                ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->Size);
                ImGuiID top = 0;
                const ImGuiID bottom = ImGui::DockBuilderSplitNode(dock, ImGuiDir_Down, 0.3f, nullptr, &top);
                ImGui::DockBuilderDockWindow("Watch", bottom);
                ImGui::DockBuilderDockWindow("Log", bottom);
                ImGui::DockBuilderDockWindow("Memory", top);
                ImGui::DockBuilderFinish(dock);
            }
            for (const char* t : { "Watch", "Log", "Memory" }) { ImGui::Begin(t); ImGui::End(); }
        });
        EnableDocking();
        h.Settle();
        h.Settle();
        ini = ImGui::SaveIniSettingsToMemory();
    }
    return ini;
}

struct Result
{
    bool oldLayoutLoaded = false;
    ImGuiID watchDock = 0, structsDock = 0, memoryDock = 0, compareDock = 0;
    bool structsDocked = false, compareDocked = false;
    float structsW = 0.0f;
};

Result RunWithOldLayout(const std::string& ini, bool adopt)
{
    Result r;
    bool first = true;
    ImGuiHarness h([&] {
        const ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
        if (first)
        {
            first = false;
            // The same test App makes: a saved layout is a non-leaf dock space.
            const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dock);
            r.oldLayoutLoaded = node != nullptr && !node->IsLeafNode();
            const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dock);
            if (adopt && r.oldLayoutLoaded && central) AdoptPanelsBeside(central->ID);
        }
        for (const char* t : { "Watch", "Log", "Memory", "Structs", "Memory Compare" }) { ImGui::Begin(t); ImGui::End(); }
    });
    EnableDocking();
    ImGui::LoadIniSettingsFromMemory(ini.c_str(), ini.size());
    h.Settle();
    h.Settle();
    const ImGuiWindow* watch = ImGui::FindWindowByName("Watch");
    const ImGuiWindow* structs = ImGui::FindWindowByName("Structs");
    const ImGuiWindow* memory = ImGui::FindWindowByName("Memory");
    const ImGuiWindow* compare = ImGui::FindWindowByName("Memory Compare");
    if (watch && structs && memory && compare)
    {
        r.watchDock = watch->DockId;
        r.structsDock = structs->DockId;
        r.memoryDock = memory->DockId;
        r.compareDock = compare->DockId;
        r.structsDocked = structs->DockNode != nullptr || structs->DockIsActive;
        r.compareDocked = compare->DockNode != nullptr || compare->DockIsActive;
        r.structsW = structs->Size.x;
    }
    return r;
}

// The saved layout knows Watch and Memory only. Structs must join Watch's dock node (the default layout puts
// it there), Memory Compare Memory's.
void StructsDocksBesideWatchInAnOldLayout()
{
    const std::string ini = OldLayoutIni();
    CHECK(ini.find("[Window][Watch]") != std::string::npos);
    CHECK(ini.find("[Window][Structs]") == std::string::npos);

    // Without adoption the window has nothing to say where it goes: ImGui opens it floating. This is what a
    // user with a saved layout saw, and it keeps the test honest about what adoption changes.
    const Result before = RunWithOldLayout(ini, false);
    CHECK(before.oldLayoutLoaded);
    CHECK(before.watchDock != 0);
    CHECK(before.structsDock == 0);

    const Result after = RunWithOldLayout(ini, true);
    CHECK(after.oldLayoutLoaded);
    CHECK(after.watchDock != 0);
    CHECK(after.structsDock == after.watchDock);
    CHECK(after.structsDocked);
    CHECK(after.compareDock == after.memoryDock && after.compareDock != 0);
    CHECK(after.compareDocked);
    // Docked windows take their node's size, not the 310 x 70 a floating one opens at.
    CHECK(after.structsW > 400.0f);
}

// A window the user already placed has settings of its own and is never moved by adoption.
void AdoptionLeavesKnownWindowsAlone()
{
    std::string ini = OldLayoutIni();
    ini += "\n[Window][Structs]\nPos=10,10\nSize=300,200\nCollapsed=0\n";
    const Result r = RunWithOldLayout(ini, true);
    CHECK(r.structsDock == 0);
}
}  // namespace

int main()
{
    StructsDocksBesideWatchInAnOldLayout();
    AdoptionLeavesKnownWindowsAlone();
    if (gFailures) { std::printf("FAILURES: %d\n", gFailures); return 1; }
    std::printf("all cases passed\n");
    return 0;
}
