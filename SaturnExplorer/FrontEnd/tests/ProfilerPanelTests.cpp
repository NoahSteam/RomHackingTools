// The Profiler panel, driven headlessly (ImGuiHarness.h) with a hand-filled model: that the
// figures it draws are the model's, that its tabs tell the owner what to gather (the Memory tab
// asks for pool samples, the Functions checkbox for attribution -- and only where the emulator
// can do it), and that a function row's double-click reaches the host through a row that also
// holds a hoverable bar (the RowSelectable overlap trap in CLAUDE.md).
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "DrawnText.h"
#include "ImGuiHarness.h"
#include "ProfilerPanel.h"

using namespace sfe;

namespace
{
int gFailures;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)

// Every text run of the Profiler window and of its child windows (the function table scrolls).
std::vector<drawn::Run> AllRuns()
{
    std::vector<drawn::Run> all;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (ImGuiWindow* w : g.Windows)
    {
        if (!w || !w->Active) continue;
        if (std::string(w->Name).find("Profiler") != 0) continue;
        for (const drawn::Run& r : drawn::Runs(w->DrawList)) all.push_back(r);
    }
    return all;
}

// The harness font spaces glyphs widely enough that a run is a word (or part of one) with gaps
// inside it, so text is compared with every space removed, across consecutive runs.
bool Drawn(const std::string& text)
{
    std::string all;
    for (const drawn::Run& r : AllRuns()) all += drawn::Squash(r.text);
    return all.find(drawn::Squash(text)) != std::string::npos;
}

// Centre of the first run of the first place the consecutive runs spell 'text', or (-1,-1).
ImVec2 Where(const std::string& text)
{
    const std::string want = drawn::Squash(text);
    const std::vector<drawn::Run> runs = AllRuns();
    for (size_t i = 0; i < runs.size(); ++i)
    {
        std::string got;
        for (size_t j = i; j < runs.size() && got.size() < want.size(); ++j)
        {
            got += drawn::Squash(runs[j].text);
            if (got == want)
                return ImVec2((runs[i].x0 + runs[i].x1) * 0.5f, (runs[i].y0 + runs[i].y1) * 0.5f);
        }
    }
    return ImVec2(-1.0f, -1.0f);
}

std::string Dump()
{
    std::string s;
    for (const drawn::Run& r : AllRuns()) s += "[" + r.text + "] ";
    return s;
}

// A tab (like any item that tests the previous frame's hovered id) only takes a press once the
// pointer has rested on it, so every click is a settled hover first.
void ClickAt(ImGuiHarness& h, ImVec2 p)
{
    h.Hover(p);
    h.Click(p);
}

struct Rig
{
    ProfilerModel model{ 16 };
    MemoryPoolTracker pools;
    ProfilerPanel panel;
    ProfilerPanel::Host host;
    int goCpu = -1;
    uint32_t goAddr = 0;
    int clears = 0;

    Rig()
    {
        host.live = host.hasCounters = host.canFunctions = true;
        host.functionName = [](uint32_t a)
        {
            if (a == 0x06004000u) return std::string("UpdatePlayer");
            char b[24];
            std::snprintf(b, sizeof(b), "sub_%08X", a);
            return std::string(b);
        };
        host.goToCode = [this](int cpu, uint32_t a) { goCpu = cpu; goAddr = a; };

        for (uint32_t i = 1; i <= 4; ++i)
        {
            PerfFrame f;
            f.seq = i;
            f.frame = 1000 + i;
            f.c[SE_PERF_FRAME_CYCLES] = 400000;
            f.c[SE_PERF_SH2_SLEEP] = 160000;                 // master 60% busy
            f.c[SE_PERF_SSH2_ON_CYCLES] = 400000;
            f.c[SE_PERF_SH2_SLEEP + 1] = 300000;             // slave 25% busy
            f.c[SE_PERF_VDP1_DRAW_CYCLES] = 120000;          // 30%
            f.c[SE_PERF_VDP1_COMMANDS] = 250;
            f.c[SE_PERF_VDP1_DRAW_STARTS] = 1;
            f.c[SE_PERF_M68K_TOTAL_CYCLES] = 1000;
            f.c[SE_PERF_M68K_CYCLES] = 80;                   // 8.0%
            f.c[SE_PERF_BUS_ACCESS + SE_PERF_DEV_WRAM_H * 2] = 5000;
            f.c[SE_PERF_BUS_ACCESS + SE_PERF_DEV_VDP1 * 2 + 1] = 640;
            f.c[SE_PERF_DMA_TRANSFERS] = 2;
            f.c[SE_PERF_DMA_LEVEL_BYTES] = 8192;
            f.c[SE_PERF_DMA_ROUTE_BYTES + SE_PERF_BUS_CPU * 3 + SE_PERF_BUS_B] = 8192;
            f.functions = true;
            f.fns = { { 0x06004000u, 90000 }, { 0x06010000u | SE_PERF_FN_SLAVE, 30000 },
                      { SE_PERF_FN_RAW_PC | 0x06020040u, 12000 } };
            model.Push(f);
        }
        std::vector<uint8_t> ram(4096, 0);
        ram[100] = 7;
        pools.Sample(0, "Colour RAM", 0x05F00000u, ram.data(), 4096);
        pools.MarkMissing(1, "Sound RAM", 0x05A00000u, 0x80000);
    }

    void Draw()
    {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f));
        if (panel.Draw(model, pools, host)) ++clears;
    }
};

void TestFrameTab()
{
    Rig rig;
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    if (!Drawn("60% busy")) std::printf("drawn: %s\n", Dump().c_str());
    CHECK(Drawn("60% busy"));            // master
    CHECK(Drawn("25% busy"));            // slave
    CHECK(Drawn("30%"));                 // VDP1 drawing
    CHECK(Drawn("8.0%"));                // the 68000, of its own clock
    CHECK(Drawn("250 commands"));
    CHECK(Drawn("4 frames recorded"));
    CHECK(!rig.panel.WantsPools());      // the Memory tab is not showing: nothing to hash
    CHECK(!rig.panel.WantsFunctions());  // attribution is opt-in

    ClickAt(harness, Where("Clear"));
    CHECK(rig.clears == 1);
}

void TestBusTab()
{
    Rig rig;
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    ClickAt(harness, Where("Buses"));
    harness.Settle();
    if (!Drawn("High work RAM")) std::printf("drawn: %s\n", Dump().c_str());
    CHECK(Drawn("High work RAM"));
    CHECK(Drawn("5000"));                // reads per frame, averaged over identical frames
    CHECK(Drawn("640"));
    CHECK(Drawn("8.0 KiB"));
    CHECK(Drawn("CPU bus to B-bus"));
}

void TestMemoryTab()
{
    {   // one harness (one ImGui context) at a time
    Rig rig;
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    ClickAt(harness, Where("Memory"));
    harness.Settle();
    CHECK(rig.panel.WantsPools());
    CHECK(Drawn("Colour RAM"));
    CHECK(Drawn("not in this capture"));     // a missing region says so; it is not drawn as empty
    rig.panel.NotDrawn();
    CHECK(!rig.panel.WantsPools());
    }

    // Offline (a savestate): the pools still draw; the frame tab explains why it is empty.
    Rig off;
    off.host.live = off.host.hasCounters = off.host.canFunctions = false;
    off.model.Clear();
    ImGuiHarness h2([&] { off.Draw(); });
    h2.Settle();
    CHECK(Drawn("Connect to a live emulator"));
    ClickAt(h2, Where("Memory"));
    h2.Settle();
    CHECK(Drawn("Colour RAM"));
}

void TestFunctionsTab()
{
    {
    Rig rig;
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    ClickAt(harness, Where("Functions"));
    harness.Settle();
    if (!Drawn("UpdatePlayer")) std::printf("drawn: %s\n", Dump().c_str());
    CHECK(Drawn("UpdatePlayer"));            // the host's name for a recorded entry
    CHECK(Drawn("sub_06010000"));
    CHECK(Drawn("at 06020040"));             // a raw-PC bucket is not passed off as a function
    CHECK(Drawn("Slave"));

    // Double-click the hottest row: it navigates, with the CPU the time was measured on.
    const ImVec2 row = Where("UpdatePlayer");
    CHECK(row.x >= 0.0f);
    harness.Hover(row);
    harness.Click(row);
    harness.Click(row);
    CHECK(rig.goCpu == 0 && rig.goAddr == 0x06004000u);
    const ImVec2 slaveRow = Where("sub_06010000");
    harness.Hover(slaveRow);
    harness.Click(slaveRow);
    harness.Click(slaveRow);
    CHECK(rig.goCpu == 1 && rig.goAddr == 0x06010000u);

    // The checkbox turns attribution on...
    const ImVec2 box = Where("Attribute SH-2 time to functions");
    CHECK(box.x >= 0.0f);
    ClickAt(harness, box);
    CHECK(rig.panel.WantsFunctions());
    }

    // ...but not on an emulator that cannot do it: the control is disabled, so the emulator is
    // never asked for (and never slowed by) something it would ignore.
    Rig plain;
    plain.host.canFunctions = false;
    ImGuiHarness h2([&] { plain.Draw(); });
    h2.Settle();
    ClickAt(h2, Where("Functions"));
    h2.Settle();
    ClickAt(h2, Where("Attribute SH-2 time to functions"));
    CHECK(!plain.panel.WantsFunctions());
    CHECK(Drawn("--enable-debugger"));
}
}  // namespace

int main()
{
    TestFrameTab();
    TestBusTab();
    TestMemoryTab();
    TestFunctionsTab();
    if (gFailures) { std::printf("%d check(s) failed\n", gFailures); return 1; }
    std::printf("ProfilerPanelTests passed\n");
    return 0;
}
