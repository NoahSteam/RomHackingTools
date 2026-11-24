#include "ProfilerPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include "imgui.h"

#include "PanelWidgets.h"
#include "Theme.h"

namespace sfe
{

namespace
{
// ---- Colour roles. One hue per job, the same wherever that job is drawn, in a light and a dark
// step each. Text never takes these: values and labels stay in the theme's text colours and the
// coloured mark beside them carries the identity.
ImU32 ColBusy()    { return ui::Color(IM_COL32(0x39, 0x87, 0xe5, 255), IM_COL32(0x2a, 0x78, 0xd6, 255)); }
ImU32 ColStall()   { return ui::Color(IM_COL32(0xd9, 0x59, 0x26, 255), IM_COL32(0xeb, 0x68, 0x34, 255)); }
ImU32 ColSleep()   { return ui::Color(IM_COL32(0x6a, 0x6a, 0x70, 255), IM_COL32(0xb4, 0xb4, 0xb0, 255)); }
ImU32 ColOff()     { return ui::Color(IM_COL32(0x3a, 0x3a, 0x40, 255), IM_COL32(0xd6, 0xd6, 0xd2, 255)); }
ImU32 ColVdp1()    { return ui::Color(IM_COL32(0x19, 0x9e, 0x70, 255), IM_COL32(0x1b, 0xaf, 0x7a, 255)); }
ImU32 ColDsp()     { return ui::Color(IM_COL32(0xc9, 0x85, 0x00, 255), IM_COL32(0xed, 0xa1, 0x00, 255)); }
ImU32 ColM68k()    { return ui::Color(IM_COL32(0xd5, 0x51, 0x81, 255), IM_COL32(0xe8, 0x7b, 0xa4, 255)); }
ImU32 ColTrack()   { return ui::Color(IM_COL32(0x2a, 0x2a, 0x30, 255), IM_COL32(0xe4, 0xe4, 0xe0, 255)); }
ImU32 ColGrid()    { return ui::Color(IM_COL32(255, 255, 255, 28), IM_COL32(0, 0, 0, 28)); }
ImU32 ColCross()   { return ui::Color(IM_COL32(255, 255, 255, 110), IM_COL32(0, 0, 0, 110)); }
ImU32 ColData()    { return ui::Color(IM_COL32(0x2b, 0x5a, 0x92, 255), IM_COL32(0x9c, 0xc0, 0xec, 255)); }

ImU32 Lerp(ImU32 a, ImU32 b, float t)
{
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t,
                                                 x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

struct Segment { double fraction; ImU32 color; const char* name; };

const size_t kWindowFrames[] = { 1, 30, 60, 300, 600 };
const char* const kWindowNames[] = { "Last frame", "0.5 s", "1 s", "5 s", "10 s" };
const int kWindowCount = 5;

void Swatch(ImU32 color, const char* label)
{
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + h * 0.2f),
                                              ImVec2(p.x + h * 0.6f, p.y + h * 0.8f), color, 2.0f);
    ImGui::Dummy(ImVec2(h * 0.6f, h));
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextUnformatted(label);
}

std::string Percent(double fraction)
{
    char buf[16];
    const double pct = fraction * 100.0;
    std::snprintf(buf, sizeof(buf), (pct > 0.0 && pct < 9.95) ? "%.1f%%" : "%.0f%%", pct);
    return buf;
}

std::string Count(double v)
{
    char buf[24];
    if (v >= 1e6)       std::snprintf(buf, sizeof(buf), "%.2f M", v / 1e6);
    else if (v >= 1e4)  std::snprintf(buf, sizeof(buf), "%.1f k", v / 1e3);
    else if (v >= 100 || v == std::floor(v)) std::snprintf(buf, sizeof(buf), "%.0f", v);
    else                std::snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

std::string Bytes(double v)
{
    char buf[24];
    if (v >= 1024.0 * 1024.0) std::snprintf(buf, sizeof(buf), "%.2f MiB", v / (1024.0 * 1024.0));
    else if (v >= 1024.0)     std::snprintf(buf, sizeof(buf), "%.1f KiB", v / 1024.0);
    else                      std::snprintf(buf, sizeof(buf), "%.0f B", v);
    return buf;
}

// A horizontal bar across the available width, split into segments with a 2px gap between fills.
// The whole bar is one item; hovering a segment names it. Fractions are of the full width.
void StackedBar(const char* id, const Segment* segments, int count, float height)
{
    const float width = std::max(ImGui::GetContentRegionAvail().x, 40.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + height), ColTrack(), 3.0f);
    const float mouseX = ImGui::GetIO().MousePos.x;
    const bool hovered = ImGui::IsItemHovered();
    float x = p0.x;
    for (int i = 0; i < count; ++i)
    {
        const float w = static_cast<float>(segments[i].fraction) * width;
        if (w <= 0.0f) continue;
        const float x1 = std::min(x + w, p0.x + width);
        if (x1 - x > 2.5f)
            dl->AddRectFilled(ImVec2(x, p0.y), ImVec2(x1 - 2.0f, p0.y + height), segments[i].color, 3.0f);
        else
            dl->AddRectFilled(ImVec2(x, p0.y), ImVec2(std::max(x1, x + 1.0f), p0.y + height), segments[i].color);
        if (hovered && mouseX >= x && mouseX < x1)
            ImGui::SetTooltip("%s: %s", segments[i].name, Percent(segments[i].fraction).c_str());
        x = x1;
    }
}

// One measure over the retained history, 0..100% on a fixed scale, with a crosshair readout.
template <typename ValueAt>
void HistoryStrip(const char* id, const ProfilerModel& model, ImU32 color, float height, ValueAt valueAt)
{
    const float width = std::max(ImGui::GetContentRegionAvail().x, 40.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + height), ColTrack(), 3.0f);
    dl->AddLine(ImVec2(p0.x, p0.y + height * 0.5f), ImVec2(p0.x + width, p0.y + height * 0.5f), ColGrid());
    const size_t n = model.Count();
    if (n < 2) return;
    // The newest frame sits at the right edge; a short history fills from the right.
    const size_t cap = model.Capacity();
    const float step = width / static_cast<float>(cap > 1 ? cap - 1 : 1);
    const float pad = 2.0f;
    std::vector<ImVec2> pts(n);
    for (size_t i = 0; i < n; ++i)
    {
        const double v = std::min(1.0, std::max(0.0, valueAt(model.At(i))));
        pts[i] = ImVec2(p0.x + width - step * static_cast<float>(n - 1 - i),
                        p0.y + height - pad - static_cast<float>(v) * (height - pad * 2.0f));
    }
    // The line breaks wherever records are missing (lost by the server's ring, or evicted from the
    // driver's queue): joining the two sides would draw a trend through frames nobody measured.
    {
        size_t runStart = 0;
        for (size_t i = 1; i <= n; ++i)
        {
            if (i < n && !model.At(i).HasGap()) continue;
            if (i - runStart >= 2)
                dl->AddPolyline(pts.data() + runStart, static_cast<int>(i - runStart), color, ImDrawFlags_None, 2.0f);
            runStart = i;
        }
    }
    if (ImGui::IsItemHovered())
    {
        const float mx = ImGui::GetIO().MousePos.x;
        const float fromRight = (p0.x + width - mx) / step;
        const long back = std::lround(fromRight);
        if (back >= 0 && static_cast<size_t>(back) < n)
        {
            const size_t i = n - 1 - static_cast<size_t>(back);
            dl->AddLine(ImVec2(pts[i].x, p0.y), ImVec2(pts[i].x, p0.y + height), ColCross());
            dl->AddCircleFilled(pts[i], 4.0f, color);
            ImGui::SetTooltip("Frame %u: %s", model.At(i).frame, Percent(valueAt(model.At(i))).c_str());
        }
    }
}

void EmptyState(const ProfilerPanel::Host& host, bool modelEmpty)
{
    if (!host.live)
        ImGui::TextWrapped("The profiler measures a running game. Connect to a live emulator "
                           "(Emulator menu) to see where each frame's time goes.");
    else if (!host.hasCounters)
        ImGui::TextWrapped("This emulator build does not report profiler counters. Rebuild the "
                           "patched Mednafen with the current Saturn Explorer (update.sh / update.bat).");
    else if (modelEmpty)
        ImGui::TextDisabled("Waiting for the emulator to finish a frame...");
}
}  // namespace

size_t ProfilerPanel::WindowFrames() const
{
    return kWindowFrames[std::min(std::max(mWindow, 0), kWindowCount - 1)];
}

bool ProfilerPanel::Draw(const ProfilerModel& model, const MemoryPoolTracker& pools, const Host& host)
{
    bool clear = false;
    mPoolsVisible = false;
    if (!ImGui::Begin("Profiler")) { ImGui::End(); return false; }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Average over");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ComboWidth(kWindowNames, kWindowCount));
    ImGui::Combo("##window", &mWindow, kWindowNames, kWindowCount);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) clear = true;
    if (!model.Empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%zu frame%s recorded", model.Count(), model.Count() == 1 ? "" : "s");
    }

    if (ImGui::BeginTabBar("##profiler-tabs"))
    {
        if (ImGui::BeginTabItem("Frame"))
        {
            if (model.Empty() || !host.hasCounters) EmptyState(host, model.Empty());
            if (!model.Empty()) DrawFrameTab(model);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Buses"))
        {
            if (model.Empty() || !host.hasCounters) EmptyState(host, model.Empty());
            if (!model.Empty()) DrawBusTab(model);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Memory"))
        {
            mPoolsVisible = true;
            DrawMemoryTab(pools, host);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Functions"))
        {
            DrawFunctionsTab(model, host);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    return clear;
}

void ProfilerPanel::DrawFrameTab(const ProfilerModel& model)
{
    uint64_t sum[SE_PERF_COUNT];
    const size_t frames = model.Sum(WindowFrames(), sum);
    const PerfShares s = ComputePerfShares(sum, static_cast<double>(frames));
    const float barH = ImGui::GetTextLineHeight();

    ImGui::Text("Frame: %s SH-2 cycles", Count(s.frameCycles).c_str());
    ImGui::SameLine(0.0f, 24.0f);
    Swatch(ColBusy(), "Busy");
    ImGui::SameLine(0.0f, 12.0f);
    Swatch(ColStall(), "Bus stall");
    ImGui::SameLine(0.0f, 12.0f);
    Swatch(ColSleep(), "Asleep");
    ImGui::SameLine(0.0f, 12.0f);
    Swatch(ColOff(), "Off");

    if (ImGui::BeginTable("##budget", 3, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("bar", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed);

        static const char* const kCpuName[2] = { "Master SH-2", "Slave SH-2" };
        for (int c = 0; c < 2; ++c)
        {
            const CpuShare& cs = s.cpu[c];
            const Segment seg[4] = {
                { cs.busy, ColBusy(), "Busy" }, { cs.stall, ColStall(), "Stalled on the bus (DMA)" },
                { cs.sleep, ColSleep(), "Asleep (waiting for an interrupt)" }, { cs.off, ColOff(), "Switched off" } };
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(kCpuName[c]);
            ImGui::TableNextColumn(); StackedBar(c ? "##ssh2" : "##msh2", seg, 4, barH);
            ImGui::TableNextColumn();
            if (cs.off >= 0.999) ImGui::TextDisabled("off");
            else ImGui::Text("%s busy", Percent(cs.busy).c_str());
        }
        // v26 server: the old elapsed figure under its old label. v27: command work (F4 fills the
        // producer; until its bit is set the value is "not available", never a measured 0%).
        const PerfFrame& newest = model.Latest();
        const bool legacyVdp1 = newest.LegacyVdp1();
        const bool vdp1Known = legacyVdp1 || newest.Has(SE_PERF_VALID_VDP1_WORK);
        struct Single { const char* name; const char* id; double v; ImU32 col; const char* tip; bool known; };
        const Single singles[3] = {
            legacyVdp1
                ? Single{ "VDP1 drawing", "##vdp1", s.vdp1Draw, ColVdp1(), "VDP1 drawing its command table", true }
                : Single{ "VDP1 command work", "##vdp1", s.vdp1Work, ColVdp1(), "VDP1 command-work cycles", vdp1Known },
            { "SCU DSP", "##dsp", s.dsp, ColDsp(), "SCU DSP executing", true },
            { "68000 (sound)", "##m68k", s.m68k, ColM68k(), "68000 executing (share of its own clock)", true } };
        for (const Single& one : singles)
        {
            const Segment seg[1] = { { one.known ? one.v : 0.0, one.col, one.tip } };
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(one.name);
            ImGui::TableNextColumn(); StackedBar(one.id, seg, 1, barH);
            ImGui::TableNextColumn();
            if (one.known) ImGui::TextUnformatted(Percent(one.v).c_str());
            else ImGui::TextDisabled("not available");
        }
        ImGui::EndTable();
    }

    ImGui::Text("VDP1: %s commands, %s draw%s per frame", Count(s.vdp1Commands).c_str(),
                Count(s.vdp1DrawsPerFrame).c_str(), s.vdp1DrawsPerFrame == 1.0 ? "" : "s");
    if (s.vdp1Overruns > 0.0)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ColStall()), "  %s unfinished",
                           Count(s.vdp1Overruns).c_str());
        ImGui::SetItemTooltip("Draws cut short per frame: the frame buffer was swapped, or drawing was "
                              "restarted, before VDP1 reached the end of its command table.");
    }

    ImGui::SeparatorText("History (0-100% of the frame, newest on the right)");
    const float stripH = ImGui::GetTextLineHeight() * 2.6f;
    if (ImGui::BeginTable("##history", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("plot", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted("Master SH-2 busy");
        ImGui::TableNextColumn();
        HistoryStrip("##h-msh2", model, ColBusy(), stripH,
                     [](const PerfFrame& f) { return ComputePerfShares(f.c).cpu[0].busy; });
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted("Slave SH-2 busy");
        ImGui::TableNextColumn();
        HistoryStrip("##h-ssh2", model, ColBusy(), stripH,
                     [](const PerfFrame& f) { return ComputePerfShares(f.c).cpu[1].busy; });
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(model.Latest().LegacyVdp1() ? "VDP1 drawing" : "VDP1 command work");
        ImGui::TableNextColumn();
        HistoryStrip("##h-vdp1", model, ColVdp1(), stripH,
                     [](const PerfFrame& f) {
                         const PerfShares sh = ComputePerfShares(f.c);
                         return f.LegacyVdp1() ? sh.vdp1Draw : (f.Has(SE_PERF_VALID_VDP1_WORK) ? sh.vdp1Work : 0.0);
                     });
        ImGui::EndTable();
    }
}

void ProfilerPanel::DrawBusTab(const ProfilerModel& model)
{
    uint64_t sum[SE_PERF_COUNT];
    const double frames = static_cast<double>(std::max<size_t>(model.Sum(WindowFrames(), sum), 1));
    const BusTotals totals = ComputeBusTotals(sum);
    const float barH = ImGui::GetTextLineHeight();

    uint64_t maxDev = 1;
    for (int d = 0; d < SE_PERF_DEV_COUNT; ++d)
        maxDev = std::max(maxDev, sum[SE_PERF_BUS_ACCESS + d * 2] + sum[SE_PERF_BUS_ACCESS + d * 2 + 1]);

    ImGui::TextUnformatted("SH-2 bus accesses per frame");
    ImGui::SameLine(0.0f, 24.0f);
    Swatch(ColBusy(), "Reads");
    ImGui::SameLine(0.0f, 12.0f);
    Swatch(ColStall(), "Writes");
    ImGui::SetItemTooltip("Accesses that reached the external bus. A hit in the SH-2's cache is not one,\n"
                          "and neither is an instruction fetch the emulator serves from its fast map.");

    if (ImGui::BeginTable("##buses", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Reads", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Writes", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Share of the busiest device", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        static const PerfBus kOrder[3] = { PerfBus::Cpu, PerfBus::A, PerfBus::B };
        for (PerfBus bus : kOrder)
        {
            const int b = static_cast<int>(bus);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", PerfBusName(bus));
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", Count(totals.reads[b] / frames).c_str());
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", Count(totals.writes[b] / frames).c_str());
            ImGui::TableNextColumn();
            for (int d = 0; d < SE_PERF_DEV_COUNT; ++d)
            {
                if (PerfDeviceBus(d) != bus) continue;
                const uint64_t r = sum[SE_PERF_BUS_ACCESS + d * 2], w = sum[SE_PERF_BUS_ACCESS + d * 2 + 1];
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("   %s", PerfDeviceName(d));
                ImGui::TableNextColumn(); ImGui::TextUnformatted(Count(r / frames).c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(Count(w / frames).c_str());
                ImGui::TableNextColumn();
                const Segment seg[2] = { { static_cast<double>(r) / maxDev, ColBusy(), "Reads" },
                                         { static_cast<double>(w) / maxDev, ColStall(), "Writes" } };
                ImGui::PushID(d);
                StackedBar("##dev", seg, 2, barH);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("SCU DMA per frame");
    if (ImGui::BeginTable("##dma", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Level");
        ImGui::TableSetupColumn("Transfers");
        ImGui::TableSetupColumn("Bytes");
        ImGui::TableHeadersRow();
        for (int level = 0; level < 3; ++level)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("Level %d", level);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(Count(sum[SE_PERF_DMA_TRANSFERS + level] / frames).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(Bytes(sum[SE_PERF_DMA_LEVEL_BYTES + level] / frames).c_str());
        }
        ImGui::EndTable();
    }
    bool anyRoute = false;
    for (int src = 0; src < SE_PERF_BUS_COUNT; ++src)
        for (int dst = 0; dst < SE_PERF_BUS_COUNT; ++dst)
        {
            const uint64_t bytes = sum[SE_PERF_DMA_ROUTE_BYTES + src * 3 + dst];
            if (!bytes) continue;
            anyRoute = true;
            ImGui::BulletText("%s to %s: %s", PerfDmaBusName(src), PerfDmaBusName(dst),
                              Bytes(bytes / frames).c_str());
        }
    if (!anyRoute) ImGui::TextDisabled("No SCU DMA in this window.");
}

void ProfilerPanel::DrawMemoryTab(const MemoryPoolTracker& pools, const Host& host)
{
    if (pools.Count() == 0)
    {
        ImGui::TextDisabled("No data loaded.");
        return;
    }
    Swatch(ColTrack(), "Empty");
    ImGui::SameLine(0.0f, 12.0f);
    Swatch(ColData(), "Holds data");
    ImGui::SameLine(0.0f, 12.0f);
    Swatch(ColStall(), "Just rewritten");
    ImGui::SetItemTooltip("Each cell is one block of the RAM. A block that is a single byte value repeated\n"
                          "(cleared, or never used) counts as empty. A block is \"rewritten\" when its\n"
                          "contents differ from the previous sample; the colour fades as it stays put.");

    const float cell = std::max(4.0f, std::floor(ImGui::GetTextLineHeight() * 0.42f));
    const float pitch = cell + 1.0f;
    for (size_t i = 0; i < pools.Count(); ++i)
    {
        const PoolUsage& u = pools.Pool(i);
        if (u.name.empty()) continue;
        ImGui::PushID(static_cast<int>(i));
        ImGui::Spacing();
        if (!u.valid)
        {
            ImGui::Text("%s", u.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%s) not in this capture", Bytes(u.size).c_str());
            ImGui::PopID();
            continue;
        }
        ImGui::Text("%s", u.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s at %08X  -  %s holds data, %s rewritten", Bytes(u.size).c_str(), u.base,
                            Percent(u.UsedFraction()).c_str(), Percent(u.ChangedFraction()).c_str());

        const float width = std::max(ImGui::GetContentRegionAvail().x, pitch * 8.0f);
        const int perRow = std::max(8, static_cast<int>(width / pitch));
        const int count = static_cast<int>(u.cells.size());
        const int rows = (count + perRow - 1) / perRow;
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##map", ImVec2(perRow * pitch, rows * pitch));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 empty = ColTrack(), data = ColData(), hot = ColStall();
        for (int b = 0; b < count; ++b)
        {
            const uint8_t v = u.cells[b];
            const ImU32 col = v == 0 ? empty : (v == 1 ? data : Lerp(data, hot, (v - 1) / 254.0f));
            const float x = p0.x + (b % perRow) * pitch, y = p0.y + (b / perRow) * pitch;
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cell, y + cell), col);
        }
        if (ImGui::IsItemHovered())
        {
            const ImVec2 m = ImGui::GetIO().MousePos;
            const int col = static_cast<int>((m.x - p0.x) / pitch), row = static_cast<int>((m.y - p0.y) / pitch);
            const int b = row * perRow + col;
            if (col >= 0 && col < perRow && b >= 0 && b < count)
            {
                const uint32_t a0 = u.base + static_cast<uint32_t>(b) * u.blockSize;
                const uint32_t a1 = std::min(a0 + u.blockSize, u.base + u.size) - 1;
                const uint8_t v = u.cells[b];
                ImGui::SetTooltip("%08X - %08X\n%s%s", a0, a1,
                                  v == 0 ? "Empty" : (v == 255 ? "Rewritten since the last sample"
                                                               : (v > 1 ? "Rewritten recently" : "Holds data")),
                                  host.goToMemory ? "\nClick to open in Memory" : "");
                if (ImGui::IsItemClicked() && host.goToMemory) host.goToMemory(a0);
            }
        }
        ImGui::PopID();
    }
}

void ProfilerPanel::DrawFunctionsTab(const ProfilerModel& model, const Host& host)
{
    ImGui::BeginDisabled(!host.canFunctions);
    ImGui::Checkbox("Attribute SH-2 time to functions", &mFunctions);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (!host.live)
        ImGui::TextDisabled("(needs a live emulator)");
    else if (!host.canFunctions)
        ImGui::TextDisabled("(needs a Mednafen built with --enable-debugger and the current patch)");
    else
        ImGui::TextDisabled("(the emulator runs several times slower while this is on)");

    static const char* const kCpus[] = { "Both CPUs", "Master SH-2", "Slave SH-2" };
    ImGui::SetNextItemWidth(ComboWidth(kCpus, 3));
    ImGui::Combo("##fncpu", &mFnCpu, kCpus, 3);

    uint64_t total = 0;
    size_t frames = 0;
    std::vector<PerfFunction> raw = model.HotFunctions(WindowFrames(), &total, &frames);
    if (!frames)
    {
        ImGui::TextDisabled(mFunctions ? "Waiting for attributed frames..."
                                       : "No attributed frames in this window.");
        return;
    }

    // Fold raw-PC buckets into the function that contains them, when the host can find it, so a
    // function is one row however its time was recorded.
    struct Row { uint32_t address; bool slave; bool known; uint64_t cycles; };
    std::vector<Row> rows;
    std::unordered_map<uint64_t, size_t> index;
    uint64_t shown = 0;
    for (const PerfFunction& f : raw)
    {
        if ((mFnCpu == 1 && f.Slave()) || (mFnCpu == 2 && !f.Slave())) continue;
        uint32_t address = f.Address();
        bool known = !f.RawPc();
        if (!known && host.functionContaining)
        {
            uint32_t entry = 0;
            // Work RAM code is addressed through the cached mirror everywhere else in the tool.
            if (host.functionContaining(address, &entry)) { address = entry & SE_PERF_FN_ADDR_MASK; known = true; }
        }
        const uint64_t k = (static_cast<uint64_t>(address) << 2) | (f.Slave() ? 2u : 0u) | (known ? 1u : 0u);
        auto it = index.find(k);
        if (it == index.end()) { index[k] = rows.size(); rows.push_back({ address, f.Slave(), known, f.cycles }); }
        else rows[it->second].cycles += f.cycles;
        shown += f.cycles;
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.cycles > b.cycles; });

    ImGui::SameLine();
    ImGui::TextDisabled("self time, callees excluded - %s cycles per frame attributed",
                        Count(static_cast<double>(shown) / frames).c_str());

    const float barH = ImGui::GetTextLineHeight();
    if (ImGui::BeginTable("##functions", 5, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg |
                                                ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Cycles/frame", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("##bar", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        const double top = rows.empty() ? 1.0 : static_cast<double>(std::max<uint64_t>(rows[0].cycles, 1));
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const Row& r = rows[i];
            std::string name;
            if (r.known && host.functionName) name = host.functionName(r.address);
            if (name.empty())
            {
                char buf[32];
                std::snprintf(buf, sizeof(buf), r.known ? "sub_%08X" : "at %08X", r.address);
                name = buf;
            }
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // The bar cell is hoverable, so the row must let it through (see RowSelectable).
            RowSelectable(name.c_str(), false, false, ImGuiSelectableFlags_AllowDoubleClick |
                                                           ImGuiSelectableFlags_AllowOverlap);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && host.goToCode)
                host.goToCode(r.slave ? 1 : 0, r.address);
            if (!r.known)
                ImGui::SetItemTooltip("No recorded call names this code (it was already running when\n"
                                      "profiling began), so it is listed by a 16-byte range of its PC.");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.slave ? "Slave" : "Master");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(Count(static_cast<double>(r.cycles) / frames).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Percent(shown ? static_cast<double>(r.cycles) / shown : 0.0).c_str());
            ImGui::TableNextColumn();
            const Segment seg[1] = { { static_cast<double>(r.cycles) / top, ColBusy(), "Relative to the hottest function" } };
            StackedBar("##fnbar", seg, 1, barH);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

}  // namespace sfe
