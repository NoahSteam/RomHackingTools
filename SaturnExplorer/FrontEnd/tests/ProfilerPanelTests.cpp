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
#include "SeLiveProtocol.h"

using namespace sfe;

namespace
{
int gFailures;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)

// Every text run of the Profiler window and of its child windows (the function table scrolls).
std::vector<drawn::Run> AllRuns(bool tooltips = false)
{
    std::vector<drawn::Run> all;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (ImGuiWindow* w : g.Windows)
    {
        if (!w || !w->Active) continue;
        // The Profiler window and its children; or, when asked, only the tooltip windows.
        const bool tip = std::string(w->Name).find("##Tooltip") == 0;
        if (tooltips ? !tip : std::string(w->Name).find("Profiler") != 0) continue;
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

// The same, over the tooltip windows only: what a hover says, apart from what the panel always shows.
bool Tip(const std::string& text)
{
    std::string all;
    for (const drawn::Run& r : AllRuns(true)) all += drawn::Squash(r.text);
    return all.find(drawn::Squash(text)) != std::string::npos;
}
std::string TipDump()
{
    std::string s;
    for (const drawn::Run& r : AllRuns(true)) s += "[" + r.text + "] ";
    return s;
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
            f.wireVersion = 26;   // the old elapsed VDP1 figure, under its old label
            f.c[SE_PERF_FRAME_CYCLES] = 400000;
            f.c[SE_PERF_SH2_SLEEP] = 160000;                 // master 60% busy
            f.c[SE_PERF_SSH2_ON_CYCLES] = 400000;
            f.c[SE_PERF_SH2_SLEEP + 1] = 300000;             // slave 25% busy
            f.c[SE_PERF_V26_VDP1_ELAPSED] = 120000;      // 30%
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

// What the user sees for a v26 server (old figure, old label) against a v27 one whose producer has
// not filled the VDP1 command-work counter yet (a label of its own, and "not available" rather
// than a measured 0%), and then once the producer sets its bit.
void TestVdp1Presentation()
{
    {
        Rig rig;   // its frames are v26
        ImGuiHarness harness([&] { rig.Draw(); });
        harness.Settle();
        CHECK(Drawn("VDP1 drawing"));
        CHECK(!Drawn("VDP1 command work"));
        CHECK(!Drawn("not available"));
    }
    {
        Rig rig;
        rig.model.Clear();
        for (uint32_t i = 1; i <= 3; ++i)
        {
            PerfFrame f;
            f.seq = i; f.sampleSeq = i; f.wireVersion = 27; f.valid = 0;
            f.c[SE_PERF_FRAME_CYCLES] = 400000;
            f.c[SE_PERF_SSH2_ON_CYCLES] = 400000;
            rig.model.Push(f);
        }
        ImGuiHarness harness([&] { rig.Draw(); });
        harness.Settle();
        CHECK(Drawn("VDP1 command work"));
        CHECK(Drawn("not available"));
        CHECK(!Drawn("VDP1 drawing"));
    }
    {
        Rig rig;
        rig.model.Clear();
        for (uint32_t i = 1; i <= 3; ++i)
        {
            PerfFrame f;
            f.seq = i; f.sampleSeq = i; f.wireVersion = 27; f.valid = SE_PERF_VALID_ALL;   // every producer bit: nothing else is "not available"
            f.c[SE_PERF_FRAME_CYCLES] = 400000;
            f.c[SE_PERF_SSH2_ON_CYCLES] = 400000;
            f.c[SE_PERF_VDP1_WORK_CYCLES] = 100000;   // 25%
            rig.model.Push(f);
        }
        ImGuiHarness harness([&] { rig.Draw(); });
        harness.Settle();
        CHECK(Drawn("VDP1 command work"));
        CHECK(Drawn("25%"));
        CHECK(!Drawn("not available"));
    }
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
    CHECK(Drawn("Uniform") && Drawn("Non-uniform") && Drawn("Changed since sample"));
    CHECK(Drawn("non-uniform, 0% changed since sample"));
    CHECK(!Drawn("Holds data") && !Drawn("rewritten"));
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

// ---- window validity, labels, history, functions (review finding P2, F7, F10, Labels) ----

PerfFrame V27(uint64_t seq, uint32_t valid)
{
    PerfFrame f;
    f.seq = static_cast<uint32_t>(seq);
    f.sampleSeq = seq;
    f.wireVersion = 27;
    f.valid = valid;
    f.c[SE_PERF_FRAME_CYCLES] = 400000;
    f.c[SE_PERF_SSH2_ON_CYCLES] = 400000;
    return f;
}

// Four equal 400,000-cycle frames; only one carries the VDP1 work counter (120,000 = 30%). The old
// panel summed all four against the whole window and drew 7.5% with no sign that three frames had
// not measured it.
void CheckWindowCoverage(bool validFirst)
{
    Rig rig;
    rig.model.Clear();
    for (uint64_t i = 1; i <= 4; ++i)
    {
        const bool v = validFirst ? i == 1 : i == 4;
        PerfFrame f = V27(i, SE_PERF_VALID_SSH2_INT | (v ? SE_PERF_VALID_VDP1_WORK : 0u));
        if (v) f.c[SE_PERF_VDP1_WORK_CYCLES] = 120000;
        rig.model.Push(f);
    }
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    if (!Drawn("30% (1 of 4 frames)")) std::printf("drawn: %s\n", Dump().c_str());
    CHECK(Drawn("30% (1 of 4 frames)"));
    CHECK(!Drawn("7.5%"));
}

void TestWindowCoverageBothOrders()
{
    CheckWindowCoverage(false);   // the valid frame is the newest (the review's repro)
    CheckWindowCoverage(true);    // the valid frame is the oldest
    {
        Rig rig;   // none valid: "not available", and no figure at all
        rig.model.Clear();
        for (uint64_t i = 1; i <= 4; ++i) rig.model.Push(V27(i, SE_PERF_VALID_SSH2_INT));
        ImGuiHarness harness([&] { rig.Draw(); });
        harness.Settle();
        CHECK(Drawn("VDP1 command work"));
        CHECK(Drawn("not available"));
        CHECK(!Drawn("of 4 frames"));
    }
}

// The slave's integrated on-time is gated the same way: frames without SE_PERF_VALID_SSH2_INT claim
// the slave was on all frame, and averaging them in would read 81% instead of the one measurement.
void CheckSlaveCoverage(bool validFirst)
{
    Rig rig;
    rig.model.Clear();
    for (uint64_t i = 1; i <= 4; ++i)
    {
        const bool v = validFirst ? i == 1 : i == 4;
        PerfFrame f = V27(i, v ? SE_PERF_VALID_SSH2_INT : 0u);
        if (v) f.c[SE_PERF_SH2_SLEEP + 1] = 300000;   // 25% busy
        rig.model.Push(f);
    }
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    CHECK(Drawn("25% busy (1 of 4 frames)"));
    CHECK(!Drawn("81%"));
}

void TestSlaveCoverageBothOrders()
{
    CheckSlaveCoverage(false);
    CheckSlaveCoverage(true);
}

// A window holding a v26 server's frames and a v27 server's shows both VDP1 figures under their own
// labels, each over its own frames; it never adds one to the other.
void CheckMixedWire(bool v26First)
{
    Rig rig;
    rig.model.Clear();
    for (uint64_t i = 1; i <= 4; ++i)
    {
        const bool legacy = v26First ? i <= 2 : i > 2;
        PerfFrame f = V27(i, legacy ? 0u : (SE_PERF_VALID_VDP1_WORK | SE_PERF_VALID_SSH2_INT));
        if (legacy) { f.wireVersion = 26; f.c[SE_PERF_V26_VDP1_ELAPSED] = 100000; }       // 25%
        else f.c[SE_PERF_VDP1_WORK_CYCLES] = 200000;                                       // 50%
        rig.model.Push(f);
    }
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    CHECK(Drawn("VDP1 drawing"));
    CHECK(Drawn("VDP1 command work"));
    CHECK(Drawn("25% (2 of 4 frames)"));
    CHECK(Drawn("50% (2 of 4 frames)"));
    CHECK(!Drawn("37%") && !Drawn("38%"));     // the blend of the two
}

void TestMixedWireBothOrders()
{
    CheckMixedWire(true);
    CheckMixedWire(false);
}

// What the v27 server adds, and the labels.
void TestFrameTabDetailsAndLabels()
{
    Rig rig;
    rig.model.Clear();
    for (uint64_t i = 1; i <= 4; ++i)
    {
        PerfFrame f = V27(i, SE_PERF_VALID_ALL);
        f.c[SE_PERF_VDP1_WORK_CYCLES] = 600000;          // 150%: instant draw
        f.c[SE_PERF_VDP1_DMA_DISCARDED] = 40000;         // 10%
        f.c[SE_PERF_M68K_TOTAL_CYCLES] = 1000;
        f.c[SE_PERF_M68K_IDLE_CYCLES] = 700;
        f.c[SE_PERF_M68K_CYCLES] = 300;
        f.mode = SE_LIVE_PROF_FUNCTIONS;
        f.cacheMode = SE_PERF_CACHE_ON;
        if (i == 4) { f.gapCount = 3; f.gapFirst = 10; f.gapLast = 12; }
        rig.model.Push(f);
    }
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    if (!Drawn("150%")) std::printf("drawn: %s\n", Dump().c_str());
    CHECK(Drawn("Busy (includes bus waits)"));
    CHECK(Drawn("150%"));                                         // above 100%, not clamped
    CHECK(Drawn("VDP1 command work exceeds the frame in 4 frames"));
    CHECK(Drawn("discarded by DMA: 10% of the frame"));
    CHECK(Drawn("70% idle (stopped)"));
    CHECK(Drawn("Function attribution: on in 4 of 4 frames"));
    CHECK(Drawn("Lost records: 3 dropped by the emulator's ring"));
    CHECK(!Drawn("not available"));
}

// A v26 window has none of the v27 detail lines, and says nothing is available that v26 never had.
// The paired 68000 detail line (idle and executing) comes from ONE frame subset: the frames that
// carry SE_PERF_VALID_M68K_IDLE. Three frames without the bit (executed 100 of 100, idle unavailable)
// and one with it (executed 0, idle 100 of 100): the measured subset is 100% idle and 0% executing;
// the old line mixed that idle with the all-frame executing share and read "75% executing".
void CheckIdleLine(bool validFirst)
{
    Rig rig;
    rig.model.Clear();
    for (uint64_t i = 1; i <= 4; ++i)
    {
        const bool v = validFirst ? i == 1 : i == 4;
        PerfFrame f = V27(i, SE_PERF_VALID_SSH2_INT | (v ? SE_PERF_VALID_M68K_IDLE : 0u));
        f.c[SE_PERF_M68K_TOTAL_CYCLES] = 100;
        f.c[SE_PERF_M68K_CYCLES] = v ? 0 : 100;
        f.c[SE_PERF_M68K_IDLE_CYCLES] = v ? 100 : 0;
        rig.model.Push(f);
    }
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    if (!Drawn("68000: 100% idle (stopped), 0% executing, of its own clock (1 of 4 frames)"))
        std::printf("drawn: %s\n", Dump().c_str());
    CHECK(Drawn("68000: 100% idle (stopped), 0% executing, of its own clock (1 of 4 frames)"));
    CHECK(!Drawn("75% executing"));
    // The bar row averages executing over every frame and says so, so it cannot contradict the line.
    CHECK(Drawn("75% (all 4 frames)"));
}

void TestIdleLineMixedValidity()
{
    CheckIdleLine(false);
    CheckIdleLine(true);
}

void TestFrameTabV26HasNoDetails()
{
    Rig rig;   // v26 frames
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    CHECK(!Drawn("discarded by DMA"));
    CHECK(!Drawn("Function attribution:"));
    CHECK(!Drawn("Lost records"));
    CHECK(Drawn("Busy (includes bus waits)"));
}

void TestBusTabUnitCacheAndBuckets()
{
    Rig rig;
    rig.model.Clear();
    for (uint64_t i = 1; i <= 2; ++i)
    {
        PerfFrame f = V27(i, SE_PERF_VALID_ALL);
        f.cacheMode = SE_PERF_CACHE_ON;
        f.c[SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_ABUS_DUMMY * 2] = 12;
        f.c[SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_UNMAPPED * 2 + 1] = 5;
        rig.model.Push(f);
    }
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    ClickAt(harness, Where("Buses"));
    harness.Settle();
    CHECK(Drawn("SH-2 bus requests per frame"));
    CHECK(Drawn("Cache mode: instruction cache emulated"));
    CHECK(Drawn("A-bus dummy area 12 reads"));
    CHECK(Drawn("Unmapped 0 reads, 5 writes"));
}

// The history strip: a frame that did not measure the value is "no data", not 0%; the tooltip names
// the frame by its real identity, or says it has none.
ImVec2 StripNewest(const std::string& rowLabel)
{
    const ImVec2 label = Where(rowLabel);
    return ImVec2(885.0f, label.y + ImGui::GetTextLineHeight() * 0.9f);
}

void TestHistoryStripNoDataAndIdentity()
{
    Rig rig;
    rig.model.Clear();
    for (uint64_t i = 1; i <= 6; ++i)
    {
        // Slave integrated on-time only in the first five; the newest frame did not measure it.
        PerfFrame f = V27(i, SE_PERF_VALID_IDENTITY | (i < 6 ? SE_PERF_VALID_SSH2_INT : 0u));
        f.timelineId = 2;
        f.frameNo = 5000 + i;
        f.frame = 77;   // the snapshot number of the batch: never shown
        rig.model.Push(f);
    }
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    harness.Hover(StripNewest("Slave SH-2 busy"));
    const bool shown = Tip("Frame 5006 (timeline 2)");
    if (!shown) std::printf("tooltip: %s\n", TipDump().c_str());
    CHECK(shown);
    CHECK(Tip("no data"));
    CHECK(!Tip("Frame 77"));
    CHECK(!Tip("0%"));                 // not drawn as a measured zero
    // The previous frame did measure it (all on, nothing asleep: 100%).
    harness.Hover(ImVec2(885.0f - 58.0f, StripNewest("Slave SH-2 busy").y));
    CHECK(Tip("Frame 5005 (timeline 2)"));
    CHECK(Tip("100%"));
    CHECK(!Tip("no data"));
}

void TestHistoryStripV26HasNoIdentity()
{
    Rig rig;   // v26 frames: frame = 1000 + i is the snapshot number they arrived with
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    harness.Hover(StripNewest("Master SH-2 busy"));
    CHECK(Tip("Frame: not available"));
    CHECK(!Tip("1004"));
    CHECK(Tip("60%"));
}

// F7 in the panel.
void TestFunctionsOtherAndDenominators()
{
    {
        Rig rig;
        rig.model.Clear();
        for (uint64_t i = 1; i <= 2; ++i)
        {
            PerfFrame f = V27(i, SE_PERF_VALID_OTHER | SE_PERF_VALID_SSH2_INT);
            f.functions = true;
            f.fns = { { 0x06004000u, 30000 }, { 0x06005000u, 30000 } };
            f.otherNotAdmitted[0] = 25000; f.otherEvicted[0] = 15000;
            f.fnOther = 40000;
            f.satFlags = SE_PERF_SAT_TABLE_FULL | SE_PERF_SAT_TRUNCATED;
            rig.model.Push(f);
        }
        ImGuiHarness harness([&] { rig.Draw(); });
        harness.Settle();
        ClickAt(harness, Where("Functions"));
        harness.Settle();
        if (!Drawn("Other (not listed)")) std::printf("drawn: %s\n", Dump().c_str());
        CHECK(Drawn("Other (not listed)"));
        CHECK(Drawn("30%"));                  // 30,000 of 100,000 attributed, not of the 60,000 listed
        CHECK(Drawn("40%"));                  // Other
        CHECK(!Drawn("50%"));
        CHECK(Drawn("Listed rows are the retained keys"));
        CHECK(Drawn("The table filled in 2 of 2 frames"));
        CHECK(Drawn("More than 64 functions ran in 2 of 2 frames"));
        CHECK(Drawn("Not observed (estimate)"));   // busy time nobody was charged for, apart from Other; labelled as an estimate
    }
    // Master and slave filters, each against that CPU's own attributed total.
    for (int filter = 0; filter < 3; ++filter)
    {
        Rig rig;
        rig.model.Clear();
        PerfFrame f = V27(1, SE_PERF_VALID_OTHER | SE_PERF_VALID_SSH2_INT);
        f.functions = true;
        f.fns = { { 0x06004000u, 30000 }, { 0x06010000u | SE_PERF_FN_SLAVE, 10000 } };
        f.otherNotAdmitted[0] = 10000;     // master attributed 40,000
        f.otherEvicted[1] = 5000;          // slave attributed 15,000
        f.fnOther = 15000;
        rig.model.Push(f);
        rig.panel.SetFnCpuFilter(filter);
        ImGuiHarness harness([&] { rig.Draw(); });
        harness.Settle();
        ClickAt(harness, Where("Functions"));
        harness.Settle();
        if (filter == 0) { CHECK(Drawn("55%")); CHECK(Drawn("18%")); CHECK(Drawn("27%")); }   // of 55,000: 30k, 10k, Other 15k
        if (filter == 1) { CHECK(Drawn("75%")); CHECK(Drawn("25%")); CHECK(!Drawn("sub_06010000")); }   // of 40,000
        if (filter == 2) { CHECK(Drawn("67%")); CHECK(Drawn("33%")); CHECK(!Drawn("sub_06004000")); }   // of 15,000
    }
    // A CPU filter over frames with no split says so rather than inventing a split.
    Rig old;   // v26 frames: functions on, no per-CPU Other
    old.panel.SetFnCpuFilter(1);
    ImGuiHarness h2([&] { old.Draw(); });
    h2.Settle();
    ClickAt(h2, Where("Functions"));
    h2.Settle();
    CHECK(Drawn("per-CPU split"));
}
}  // namespace

// ---- A scrubbed frame: the panel shows that frame's record, found by identity ----

// Frames 100..110 on timeline 2. Frames up to 105 kept the master 80% asleep (20% busy), the later
// ones 20% asleep (80% busy), so which frames a window held is visible in the figure; a different
// slave on-time per half lets the Buses / Functions tabs be told apart too.
void FillScrubModel(Rig& rig, uint32_t timeline = 2)
{
    rig.model.Clear();
    for (uint64_t n = 100; n <= 110; ++n)
    {
        PerfFrame f = V27(n, SE_PERF_VALID_SSH2_INT | SE_PERF_VALID_IDENTITY);
        f.timelineId = timeline;
        f.frameNo = n;
        f.frame = 7;   // the snapshot number of the batch: never the key
        f.c[SE_PERF_SH2_SLEEP] = n <= 105 ? 320000 : 80000;
        f.c[SE_PERF_BUS_ACCESS + SE_PERF_DEV_WRAM_H * 2] = n <= 105 ? 1000 : 9000;
        f.functions = true;
        f.fns = { { n <= 105 ? 0x06004000u : 0x06008000u, 1000 } };
        rig.model.Push(f);
    }
}

void Scrub(Rig& rig, uint32_t timeline, uint64_t frame, bool known = true)
{
    rig.host.scrubbed = true;
    rig.host.scrubIdentityKnown = known;
    rig.host.scrubTimeline = timeline;
    rig.host.scrubFrame = frame;
}

void TestScrubbedAnchorsFigures()
{
    Rig rig;
    FillScrubModel(rig);
    {   // Live: all eleven frames, no "showing" line -- as before this feature.
        ImGuiHarness harness([&] { rig.Draw(); });
        harness.Settle();
        CHECK(!Drawn("(scrubbed)"));
        CHECK(!Drawn("20% busy") && !Drawn("80% busy"));   // 6 of 11 frames at 20%, 5 at 80%: 47%
        CHECK(Drawn("47% busy"));
    }
    Scrub(rig, 2, 105);
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    // The window of 1 s (60 frames) ends at frame 105: only the six 20% frames.
    if (!Drawn("20% busy")) std::printf("drawn: %s\n", Dump().c_str());
    CHECK(Drawn("20% busy"));
    CHECK(!Drawn("47% busy") && !Drawn("80% busy"));
    CHECK(Drawn("Showing frame 105 (scrubbed), newest is 110"));
    // The Buses and Functions tabs follow the same anchor.
    ClickAt(harness, Where("Buses"));
    harness.Settle();
    CHECK(Drawn("1000"));                        // frame <=105's work-RAM count, not the newer 9000
    CHECK(!Drawn("9000") && !Drawn("5000"));
    ClickAt(harness, Where("Functions"));
    harness.Settle();
    CHECK(Drawn("UpdatePlayer"));                // the function of frames up to 105
    CHECK(!Drawn("sub_06008000"));
}

void TestScrubbedNewestHasNoNote()
{
    Rig rig;
    FillScrubModel(rig);
    Scrub(rig, 2, 110);   // scrubbed, but to the newest recorded frame
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    CHECK(!Drawn("Showing frame"));
    CHECK(Drawn("47% busy"));   // the window ends at the newest frame: all eleven
}

void TestScrubbedMissingRecord()
{
    // Each of these must say so and show no figure from any other frame.
    struct Case { uint32_t timeline; uint64_t frame; bool known; };
    const Case cases[] = {
        { 2, 99, true },      // evicted / before the profiler was on
        { 2, 111, true },     // newer than anything stored
        { 3, 105, true },     // the frame number exists, on another timeline
        { 2, 105, false },    // the capture's timeline is not known
    };
    for (const Case& c : cases)
    {
        Rig rig;
        FillScrubModel(rig);
        Scrub(rig, c.timeline, c.frame, c.known);
        ImGuiHarness harness([&] { rig.Draw(); });
        harness.Settle();
        char want[64];
        std::snprintf(want, sizeof(want), "No profiler data for frame %llu", static_cast<unsigned long long>(c.frame));
        if (!Drawn(want)) std::printf("drawn: %s\n", Dump().c_str());
        CHECK(Drawn(want));
        CHECK(!Drawn("% busy") && !Drawn("Frame length"));       // nothing from another frame
        CHECK(!Drawn("Showing frame"));
        ClickAt(harness, Where("Buses"));
        harness.Settle();
        CHECK(Drawn(want) && !Drawn("1000") && !Drawn("9000"));
        ClickAt(harness, Where("Functions"));
        harness.Settle();
        CHECK(Drawn(want) && !Drawn("UpdatePlayer") && !Drawn("sub_06008000"));
    }
    // The message names the limit it can be (the ring's capacity).
    Rig rig;
    FillScrubModel(rig);
    Scrub(rig, 2, 99);
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    CHECK(Drawn("older than the last 16 kept"));
}

void TestScrubbedRecordWithoutIdentity()
{
    Rig rig;
    rig.model.Clear();
    for (uint64_t n = 100; n <= 103; ++n)
    {
        PerfFrame f = V27(n, SE_PERF_VALID_SSH2_INT);   // no identity bit
        f.timelineId = 2;
        f.frameNo = n;
        rig.model.Push(f);
    }
    Scrub(rig, 2, 102);
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    CHECK(Drawn("No profiler data for frame 102"));
}

// The status line has a fixed place: a row below it sits at the same y whether the line is empty,
// a "showing" note, or a "no data" message.
void TestScrubNoteDoesNotMoveRows()
{
    float yLive = 0, yNote = 0, yMissing = 0;
    {
        Rig rig;
        FillScrubModel(rig);
        ImGuiHarness harness([&] { rig.Draw(); });
        harness.Settle();
        yLive = Where("Frame length:").y;
    }
    {
        Rig rig;
        FillScrubModel(rig);
        Scrub(rig, 2, 105);
        ImGuiHarness harness([&] { rig.Draw(); });
        harness.Settle();
        CHECK(Drawn("Showing frame 105"));
        yNote = Where("Frame length:").y;
    }
    {
        Rig rig;
        FillScrubModel(rig);
        ImGuiHarness harness([&] { rig.Draw(); });
        harness.Settle();
        rig.host.scrubbed = true; rig.host.scrubIdentityKnown = true; rig.host.scrubTimeline = 2; rig.host.scrubFrame = 105;
        harness.Settle();
        CHECK(Drawn("Showing frame 105"));
        // And it appearing / disappearing in one panel: the row stays where it was.
        const float before = Where("Frame length:").y;
        rig.host.scrubbed = false;
        harness.Settle();
        CHECK(!Drawn("Showing frame"));
        CHECK(Where("Frame length:").y == before);
        yMissing = before;
    }
    CHECK(yLive > 0 && yLive == yNote && yNote == yMissing);

    // The missing-data message is in the same place as the note (its tab body then replaces the
    // figures, so the check is on the Window row above the tabs).
    Rig rig;
    FillScrubModel(rig);
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    const float tabsLive = Where("Frame").y;
    Scrub(rig, 2, 999);
    harness.Settle();
    CHECK(Drawn("No profiler data for frame 999"));
    CHECK(Where("Frame").y == tabsLive);
}

// Hover on a strip still names the frame under the pointer, scrubbed or not.
void TestScrubbedStripHoverUnchanged()
{
    Rig rig;
    FillScrubModel(rig);
    Scrub(rig, 2, 105);
    ImGuiHarness harness([&] { rig.Draw(); });
    harness.Settle();
    harness.Hover(StripNewest("Master SH-2 busy"));
    CHECK(Tip("Frame 110 (timeline 2)"));   // the newest is still at the right edge: the strip is not re-anchored
}

int main()
{
    TestScrubbedAnchorsFigures();
    TestScrubbedNewestHasNoNote();
    TestScrubbedMissingRecord();
    TestScrubbedRecordWithoutIdentity();
    TestScrubNoteDoesNotMoveRows();
    TestScrubbedStripHoverUnchanged();
    TestFrameTab();
    TestVdp1Presentation();
    TestBusTab();
    TestMemoryTab();
    TestFunctionsTab();
    TestWindowCoverageBothOrders();
    TestSlaveCoverageBothOrders();
    TestMixedWireBothOrders();
    TestFrameTabDetailsAndLabels();
    TestFrameTabV26HasNoDetails();
    TestIdleLineMixedValidity();
    TestBusTabUnitCacheAndBuckets();
    TestHistoryStripNoDataAndIdentity();
    TestHistoryStripV26HasNoIdentity();
    TestFunctionsOtherAndDenominators();
    if (gFailures) { std::printf("%d check(s) failed\n", gFailures); return 1; }
    std::printf("ProfilerPanelTests passed\n");
    return 0;
}
