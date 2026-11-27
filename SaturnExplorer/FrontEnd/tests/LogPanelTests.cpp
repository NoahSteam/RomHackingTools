// Log panel, driven headlessly (ImGuiHarness.h).
//
// 1. Selecting a row opens the detail pane (A12-26). Windows verify4 left-clicked a row and saw it
//    highlighted but no detail pane below the table. This drives the real LogPanel::Draw the way the
//    app does (one window, the panel inside it), clicks a row, and asks whether the pane's child window
//    exists and has room, at the window sizes the app starts with and at a short one.
// 2. The hardware event log: the capture chips, the rows a gap and a per-frame cap produce, and the
//    scaling (50000 rows submitted through a clipper, auto-scroll, unchanged row height).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuiHarness.h"
#include "HwEventFormat.h"
#include "LogPanel.h"
#include "Settings.h"

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

// ---------------------------------------------------------------------------------------------
// Hardware event log

SeHwEvent HwEv(unsigned kind, unsigned a0 = 0, unsigned a1 = 0, unsigned flags = 0, unsigned pc = 0, unsigned source = 0)
{
    SeHwEvent e = SeHwEvent();
    e.kind = static_cast<unsigned char>(kind);
    e.source = static_cast<unsigned char>(source);
    e.cpu = flags ? 0 : SE_HWEV_CPU_NONE;
    e.flags = static_cast<unsigned char>(flags);
    e.frame = 10;
    e.pc = pc;
    e.arg[0] = a0; e.arg[1] = a1;
    return e;
}

// Arms every hardware source on `p` (as a saved choice would): PushBatch drops sources this panel did not ask for.
void ArmAll(LogPanel& p)
{
    Settings s;
    for (const char* k : { "cpuexceptions", "scuinterrupts", "scudma", "dmac", "vdp1", "vdp2", "cd", "smpc" })
        s.SetBool("Log", k, true);
    p.Load(s);
}

struct HwRig
{
    LogPanel panel;
    LogPanel::Request req;
    void Draw()
    {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(900.0f, 500.0f));
        ImGui::Begin("Log", nullptr, ImGuiWindowFlags_NoSavedSettings);
        panel.Draw(req);
        ImGui::End();
    }
    ImVec2 ChipCentre(LogCategory c)
    {
        float a, b, x, y;
        if (!panel.ChipRect(c, &a, &b, &x, &y)) return ImVec2(-1, -1);
        return ImVec2((a + x) * 0.5f, (b + y) * 0.5f);
    }
    float ChipWidth(LogCategory c)
    {
        float a, b, x, y;
        return panel.ChipRect(c, &a, &b, &x, &y) ? x - a : -1.0f;
    }
};

HwChipStatus Capable()
{
    HwChipStatus s;
    s.live = true; s.capable = true; s.statusValid = true; s.applied = 0xFF; s.supported = 0xFF;
    return s;
}

void ClickChipArmsAndPersists()
{
    HwRig rig;
    rig.panel.SetHwStatus(Capable());
    ImGuiHarness h([&] { rig.Draw(); });
    h.Settle();
    CHECK(rig.panel.HwRequestMask() == 0);                          // default off
    CHECK(!rig.panel.CategoryVisible(LogCategory::CPU));
    const ImVec2 p = rig.ChipCentre(LogCategory::CPU);
    CHECK(p.x > 0);
    h.Hover(p); h.Click(p); h.Frame(p, false);
    CHECK(rig.panel.HwRequestMask() == (SE_HWEV_EN_CPU_EXC | SE_HWEV_EN_SCU_INT));
    CHECK(rig.panel.CategoryVisible(LogCategory::CPU));
    CHECK(rig.panel.ConsumeSettingsDirty());

    Settings s;
    rig.panel.Store(s);
    CHECK(s.GetBool("Log", "cpuexceptions", false) && s.GetBool("Log", "scuinterrupts", false));
    CHECK(!s.GetBool("Log", "vdp1", true));
    LogPanel again;
    again.Load(s);
    CHECK(again.HwRequestMask() == (SE_HWEV_EN_CPU_EXC | SE_HWEV_EN_SCU_INT));
    CHECK(again.CategoryVisible(LogCategory::CPU));                 // the chip comes back lit
    CHECK(!again.CategoryVisible(LogCategory::VDP));

    // Click again: capture stops and the chip hides.
    const ImVec2 q = rig.ChipCentre(LogCategory::CPU);
    h.Hover(q); h.Click(q); h.Frame(q, false);
    CHECK(rig.panel.HwRequestMask() == 0);
    CHECK(!rig.panel.CategoryVisible(LogCategory::CPU));
}

void AllDoesNotArm()
{
    HwRig rig;
    rig.panel.SetHwStatus(Capable());
    ImGuiHarness h([&] { rig.Draw(); });
    h.Settle();
    const ImVec2 all(14.0f, 35.0f);                                 // the first button of the first line, under the title bar
    h.Hover(all); h.Click(all); h.Frame(all, false);
    CHECK(rig.panel.CategoryVisible(LogCategory::VDP));             // visibility changed...
    CHECK(rig.panel.CategoryVisible(LogCategory::CD));
    CHECK(rig.panel.HwRequestMask() == 0);                          // ...capture did not
    CHECK(!rig.panel.ConsumeSettingsDirty());
}

void DisabledWithoutCapability()
{
    Settings s;
    s.SetBool("Log", "vdp1", true);
    for (int mode = 0; mode < 2; ++mode)
    {
        HwRig rig;
        rig.panel.Load(s);
        HwChipStatus st;
        st.live = mode == 1;                                        // 0: not live, 1: live but an old server
        st.capable = false;
        rig.panel.SetHwStatus(st);
        ImGuiHarness h([&] { rig.Draw(); });
        h.Settle();
        const ImVec2 p = rig.ChipCentre(LogCategory::CPU);
        h.Hover(p); h.Click(p); h.Frame(p, false);
        CHECK(rig.panel.HwRequestMask() == SE_HWEV_EN_VDP1);        // the click did nothing, the stored choice is intact
        const ImVec2 v = rig.ChipCentre(LogCategory::VDP);
        h.Hover(v); h.Click(v); h.Frame(v, false);
        CHECK(rig.panel.HwRequestMask() == SE_HWEV_EN_VDP1);
        CHECK(!rig.panel.ConsumeSettingsDirty());
        // The connection becomes capable: the same preference is simply live again.
        rig.panel.SetHwStatus(Capable());
        h.Settle();
        CHECK(rig.panel.HwRequestMask() == SE_HWEV_EN_VDP1);
        Settings out;
        rig.panel.Store(out);
        CHECK(out.GetBool("Log", "vdp1", false));
    }
}

void PendingLabel()
{
    HwRig rig;
    rig.panel.SetHwStatus(Capable());
    ImGuiHarness h([&] { rig.Draw(); });
    h.Settle();
    const float idle = rig.ChipWidth(LogCategory::DMA);
    const ImVec2 p = rig.ChipCentre(LogCategory::DMA);
    h.Hover(p); h.Click(p); h.Frame(p, false);
    CHECK(rig.panel.HwRequestMask() == (SE_HWEV_EN_SCU_DMA | SE_HWEV_EN_SH2_DMAC));
    h.Settle();
    CHECK(std::fabs(rig.ChipWidth(LogCategory::DMA) - idle) < 1.0f);   // applied mask matches: no suffix
    HwChipStatus st = Capable();
    st.applied = SE_HWEV_EN_SCU_DMA;                                   // one sub-source not applied yet
    rig.panel.SetHwStatus(st);
    h.Settle();
    CHECK(rig.ChipWidth(LogCategory::DMA) > idle + 20.0f);             // "(pending)" is shown
    st.applied = SE_HWEV_EN_SCU_DMA | SE_HWEV_EN_SH2_DMAC;
    rig.panel.SetHwStatus(st);
    h.Settle();
    CHECK(std::fabs(rig.ChipWidth(LogCategory::DMA) - idle) < 1.0f);
}

// A connection the server had no request slot for is told supported 0: the chip says "unavailable", not
// "pending" (which would wait for an applied mask that never comes).
void UnavailableLabel()
{
    HwRig rig;
    rig.panel.SetHwStatus(Capable());
    ImGuiHarness h([&] { rig.Draw(); });
    h.Settle();
    const float idle = rig.ChipWidth(LogCategory::DMA);
    const ImVec2 p = rig.ChipCentre(LogCategory::DMA);
    h.Hover(p); h.Click(p); h.Frame(p, false);
    HwChipStatus st = Capable();
    st.applied = 0;
    rig.panel.SetHwStatus(st);
    h.Settle();
    const float pending = rig.ChipWidth(LogCategory::DMA);
    CHECK(pending > idle + 20.0f);
    st.supported = 0;
    rig.panel.SetHwStatus(st);
    h.Settle();
    CHECK(rig.ChipWidth(LogCategory::DMA) > pending + 10.0f);   // "(unavailable)" is longer than "(pending)"
}

ImGuiWindow* PopupWindow()
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (ImGuiWindow* w : g.Windows)
        if (w && w->Active && (w->Flags & ImGuiWindowFlags_Popup)) return w;
    return nullptr;
}

void RightClickMenuChangesMask()
{
    HwRig rig;
    rig.panel.SetHwStatus(Capable());
    ImGuiHarness h([&] { rig.Draw(); });
    h.Settle();
    const ImVec2 p = rig.ChipCentre(LogCategory::VDP);
    h.Hover(p);
    h.RightClick(p);
    ImGuiWindow* pop = PopupWindow();
    CHECK(pop != nullptr);
    if (!pop) return;
    // Second entry (VDP2): entries are one text line plus spacing apart.
    const float lineH = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 item(pop->Pos.x + 40.0f, pop->Pos.y + ImGui::GetStyle().WindowPadding.y + lineH * 1.5f);
    h.Hover(item); h.Click(item); h.Frame(item, false);
    CHECK(rig.panel.HwRequestMask() == SE_HWEV_EN_VDP2);               // one sub-source, not the whole chip
    CHECK(rig.panel.CategoryVisible(LogCategory::VDP));
    CHECK(rig.panel.ConsumeSettingsDirty());
}

void LostAndSuppressedRowsRender()
{
    HwRig rig;
    ArmAll(rig.panel);
    rig.panel.CategoryVisible(LogCategory::VDP) = true;
    std::vector<LogEvent> batch;
    batch.push_back(FormatHwEvent(HwEv(12, 1, 0)));
    batch.push_back(FormatHwLost(1234, 10));
    batch.push_back(FormatHwEvent(HwEv(255, 137, 64, 0, 0, 4)));
    rig.panel.PushBatch(batch);
    CHECK(batch.empty());
    ImGuiHarness h([&] { rig.Draw(); });
    h.Settle();
    bool lost = false, more = false;
    for (const LogEvent* e : rig.panel.LastSubmitted())
    {
        if (e->message == "1234 hardware events lost (emulator ring / client queue)") lost = true;
        if (e->message == "... +137 more VDP1 events this frame (cap 64)") more = true;
    }
    CHECK(lost && more);
    CHECK(rig.panel.LastSubmitted().size() == 3);
    // One timestamp for the whole batch.
    CHECK(rig.panel.LastSubmitted()[0]->time == rig.panel.LastSubmitted()[2]->time);
}

void PcRowOffersJump()
{
    HwRig rig;
    ArmAll(rig.panel);
    rig.panel.CategoryVisible(LogCategory::CPU) = true;
    std::vector<LogEvent> batch;
    SeHwEvent e = HwEv(1, 0x40, SE_HWEV_EXC_INT, SE_HWEV_F_HAS_PC, 0x06004A20);
    e.arg[2] = 15; e.arg[3] = 0x06005000;
    batch.push_back(FormatHwEvent(e));
    rig.panel.PushBatch(batch);
    ImGuiHarness h([&] { rig.Draw(); });
    h.Settle();
    CHECK(rig.panel.LastSubmitted().size() == 1);
    bool selected = false;
    for (float y = 40.0f; y < 120.0f && !selected; y += 4.0f)
    {
        const ImVec2 p(40.0f, y);
        h.Hover(p); h.Click(p); h.Frame(p, false); h.Frame(p, false);
        selected = rig.panel.Selected(nullptr);
    }
    CHECK(selected);
    h.Settle();
    CHECK(Rig::DetailHeight() > 0.0f);
    // The Jump to Assembly button sits on the "Source:" line of the pane; scan along it.
    bool jumped = false;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    float lineY = -1.0f;
    for (ImGuiWindow* w : g.Windows)
        if (w && w->Active && std::string(w->Name).find("##logdetail") != std::string::npos)
            lineY = w->Pos.y + ImGui::GetTextLineHeightWithSpacing() * 1.5f;   // a borderless child has no padding
    for (float x = 200.0f; x < 700.0f && !jumped && lineY > 0; x += 10.0f)
    {
        const ImVec2 p(x, lineY);
        h.Hover(p); h.Click(p); h.Frame(p, false);
        jumped = rig.req.jumpAssembly;
    }
    CHECK(jumped);
    CHECK(rig.req.jumpAddr == 0x06004A20 && rig.req.jumpCpu == 0);
}

void ManyRowsAreClipped()
{
    HwRig rig;
    ArmAll(rig.panel);
    rig.panel.CategoryVisible(LogCategory::VDP) = true;
    for (int i = 0; i < 10000; ++i) rig.panel.Info("info row " + std::to_string(i), 1);
    std::vector<LogEvent> batch;
    for (int i = 0; i < 60000; ++i)
    {
        batch.push_back(FormatHwEvent(HwEv(16, i & 1)));
        if (batch.size() == 1000) rig.panel.PushBatch(batch);
    }
    rig.panel.PushBatch(batch);
    // 60000 hardware rows pushed: the hardware budget holds 40000, the 10000 Info rows are untouched.
    CHECK(rig.panel.Size() == LogPanel::kHwCap + 10000);
    CHECK(rig.panel.FilteredCount() == LogPanel::kHwCap + 10000);
    rig.panel.CategoryVisible(LogCategory::VDP) = false;
    CHECK(rig.panel.FilteredCount() == 10000);
    rig.panel.CategoryVisible(LogCategory::VDP) = true;

    ImGuiHarness h([&] { rig.Draw(); });
    h.Settle();
    CHECK(rig.panel.FilteredCount() == 50000);
    const size_t drawn = rig.panel.LastSubmitted().size();
    std::printf("50000 rows: %zu submitted\n", drawn);
    CHECK(drawn > 3 && drawn < 80);

    // Click a row: the pane opens (which shrinks the table and re-pins it to the bottom), then, with the
    // layout settled, click a row and the one below it: each selects exactly the row under the pointer.
    uint64_t first = 0, second = 0;
    const float x = 40.0f;
    {
        const ImVec2 p(x, 200.0f);
        h.Hover(p); h.Click(p); h.Frame(p, false);
    }
    CHECK(rig.panel.Selected(&first));
    bool inSubmitted = false;
    for (const LogEvent* e : rig.panel.LastSubmitted()) if (e->seq == first) inSubmitted = true;
    CHECK(inSubmitted);
    h.Settle();
    CHECK(Rig::DetailHeight() > 20.0f);
    const float rowH = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
    const ImVec2 pa(x, 200.0f);
    h.Hover(pa); h.Click(pa); h.Frame(pa, false);
    CHECK(rig.panel.Selected(&first));
    const ImVec2 pb(x, 200.0f + rowH);
    h.Hover(pb); h.Click(pb); h.Frame(pb, false);
    CHECK(rig.panel.Selected(&second));
    CHECK(second == first + 1);
    // And the row is the one that was drawn there, not an index into the unfiltered log.
    bool drawnSecond = false;
    for (const LogEvent* e : rig.panel.LastSubmitted()) if (e->seq == second) drawnSecond = true;
    CHECK(drawnSecond);
}

void AutoScrollFollowsAndRowHeightIsStable()
{
    HwRig rig;
    ArmAll(rig.panel);
    rig.panel.CategoryVisible(LogCategory::VDP) = true;
    auto push = [&](int n)
    {
        std::vector<LogEvent> b;
        for (int i = 0; i < n; ++i) b.push_back(FormatHwEvent(HwEv(16, i & 1)));
        rig.panel.PushBatch(b);
    };
    push(300);
    ImGuiHarness h([&] { rig.Draw(); });
    h.Settle();
    auto tableWindow = []() -> ImGuiWindow*
    {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        for (ImGuiWindow* w : g.Windows)
            if (w && w->Active && std::string(w->Name).find("/##log_") != std::string::npos) return w;
        return nullptr;
    };
    ImGuiWindow* t = tableWindow();
    CHECK(t != nullptr);
    if (!t) return;
    CHECK(t->Scroll.y >= t->ScrollMax.y - 2.0f && t->ScrollMax.y > 0.0f);
    const float max300 = t->ScrollMax.y;
    const uint64_t newestBefore = rig.panel.LastSubmitted().back()->seq;
    push(100);
    h.Settle();
    t = tableWindow();
    CHECK(t->Scroll.y >= t->ScrollMax.y - 2.0f);                     // still pinned to the bottom
    CHECK(rig.panel.LastSubmitted().back()->seq == newestBefore + 100);
    // 100 more rows grow the content by exactly 100 rows of the table's own row height.
    const float rowH = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
    CHECK(std::fabs((t->ScrollMax.y - max300) - 100.0f * rowH) < 1.0f);
}
}  // namespace

// The server streams the UNION of every connection's mask, so a panel can be handed sources it did not ask for.
// They must be dropped before they count against the hardware budget.
void UnrequestedSourcesAreDropped()
{
    HwRig rig;
    Settings s;
    s.SetBool("Log", "vdp1", true);          // this panel asked for VDP1 (source bit index 4) only
    rig.panel.Load(s);
    CHECK(rig.panel.HwRequestMask() == SE_HWEV_EN_VDP1);
    std::vector<LogEvent> batch;
    batch.push_back(FormatHwEvent(HwEv(12, 1, 0, 0, 0, 4)));     // VDP1: kept
    batch.push_back(FormatHwEvent(HwEv(16, 1, 0, 0, 0, 5)));     // VDP2: another connection's source
    batch.push_back(FormatHwEvent(HwEv(11, 0, 0, 0, 0, 7)));     // SMPC: same
    batch.push_back(FormatHwLost(5, 10));                        // a gap marker has no source and stays
    rig.panel.PushBatch(batch);
    CHECK(rig.panel.Size() == 2);
    // And they do not consume the budget: far more than kHwCap foreign rows leave the wanted ones in place.
    for (int i = 0; i < 3; ++i)
    {
        for (size_t n = 0; n < LogPanel::kHwCap + 100; ++n) batch.push_back(FormatHwEvent(HwEv(16, 1, 0, 0, 0, 5)));
        rig.panel.PushBatch(batch);
    }
    CHECK(rig.panel.Size() == 2);
}

int main()
{
    ClickRowAndLook(600.0f, "tall window");
    ClickRowAndLook(300.0f, "short window");
    for (float h : { 240.0f, 200.0f, 170.0f }) ClickRowAndLook(h, "small window");
    ClickChipArmsAndPersists();
    AllDoesNotArm();
    DisabledWithoutCapability();
    PendingLabel();
    UnavailableLabel();
    RightClickMenuChangesMask();
    LostAndSuppressedRowsRender();
    PcRowOffersJump();
    ManyRowsAreClipped();
    UnrequestedSourcesAreDropped();
    AutoScrollFollowsAndRowHeightIsStable();
    if (gFailures == 0) std::printf("LogPanel: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
