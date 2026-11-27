#include "ProfilerPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>
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
ImU32 ColNoData()  { return ui::Color(IM_COL32(0x8a, 0x8a, 0x92, 255), IM_COL32(0x70, 0x70, 0x78, 255)); }
ImU32 ColTrack()   { return ui::Color(IM_COL32(0x2a, 0x2a, 0x30, 255), IM_COL32(0xe4, 0xe4, 0xe0, 255)); }
ImU32 ColGrid()    { return ui::Color(IM_COL32(255, 255, 255, 28), IM_COL32(0, 0, 0, 28)); }
ImU32 ColCross()   { return ui::Color(IM_COL32(255, 255, 255, 110), IM_COL32(0, 0, 0, 110)); }
ImU32 ColMarker()  { return ui::Color(IM_COL32(0xd0, 0x2f, 0x2f, 255), IM_COL32(0xff, 0x6b, 0x6b, 255)); }
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
// A history strip spans this many frames whatever the model holds: the model keeps as many as the
// scrub history (deeper than ten seconds), and a strip that rescaled with that would change how the
// live view looks.
const size_t kStripFrames = 600;

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

// "(k of n frames)" after a figure that only some of a window's frames carry; empty when all do.
std::string CoverageNote(const Coverage& c)
{
    if (c.Full() || !c.Any()) return std::string();
    char buf[48];
    std::snprintf(buf, sizeof(buf), " (%zu of %zu frame%s)", c.covered, c.frames, c.frames == 1 ? "" : "s");
    return buf;
}

// Long explanatory / detail lines wrap to the panel's width instead of running off its right edge
// (the panel is often docked narrow). Same colours as Text / TextDisabled.
void WrappedText(const char* fmt, ...) IM_FMTARGS(1);
void WrappedText(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextV(fmt, args);
    ImGui::PopTextWrapPos();
    va_end(args);
}
void WrappedDisabled(const char* fmt, ...) IM_FMTARGS(1);
void WrappedDisabled(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabledV(fmt, args);
    ImGui::PopTextWrapPos();
    va_end(args);
}

constexpr double kNoData = std::numeric_limits<double>::quiet_NaN();

// True when 'newer' follows 'older' across a timeline change (a rewind or state load replaced what
// came before): the same notion of "has a timeline" as ProfilerModel::UsableFrames, so the strips and
// the averaging windows agree about where one machine history ends. Two records that both state no
// timeline have no boundary between them.
bool TimelineChangedBetween(const PerfFrame& older, const PerfFrame& newer)
{
    const bool a = !older.LegacyVdp1() && older.Has(SE_PERF_VALID_IDENTITY);
    const bool b = !newer.LegacyVdp1() && newer.Has(SE_PERF_VALID_IDENTITY);
    if (a != b) return true;
    return a && older.timelineId != newer.timelineId;
}

// One measure over the retained history, 0..100% on a fixed scale, with a crosshair readout.
// valueAt returns kNoData (NaN) for a frame that did not measure it: that frame is a gap in the
// line and a "no data" mark, never a point at 0%.
template <typename ValueAt>
void HistoryStrip(const char* id, const ProfilerModel& model, size_t end, long anchor, ImU32 color,
                  float height, ValueAt valueAt)
{
    const float width = std::max(ImGui::GetContentRegionAvail().x, 40.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + height), ColTrack(), 3.0f);
    dl->AddLine(ImVec2(p0.x, p0.y + height * 0.5f), ImVec2(p0.x + width, p0.y + height * 0.5f), ColGrid());
    // The frames [begin, end) of the model; 'end' is the newest one unless the view is scrolled to keep
    // a scrubbed frame in sight.
    const size_t cap = (std::min)(model.Capacity(), kStripFrames);
    const size_t begin = end > cap ? end - cap : 0;
    const size_t n = end - begin;
    if (n < 2) return;
    // The newest frame sits at the right edge; a short history fills from the right.
    const float step = width / static_cast<float>(cap > 1 ? cap - 1 : 1);
    const float pad = 2.0f;
    std::vector<ImVec2> pts(n);
    std::vector<HistoryPoint> hp(n);
    std::vector<size_t> dividers;   // points whose record starts a new timeline
    for (size_t i = 0; i < n; ++i)
    {
        const PerfFrame& pf = model.At(begin + i);
        const double raw = valueAt(pf);
        hp[i].available = !std::isnan(raw);
        hp[i].value = hp[i].available ? raw : 0.0;
        // A timeline change breaks the line like missing records do: the replaced timeline's tail and
        // the new frames are not one trend.
        const bool tlChange = i > 0 && TimelineChangedBetween(model.At(begin + i - 1), pf);
        hp[i].breakBefore = pf.HasGap() || tlChange;
        if (tlChange) dividers.push_back(i);
        const double v = std::min(1.0, std::max(0.0, hp[i].value));
        pts[i] = ImVec2(p0.x + width - step * static_cast<float>(n - 1 - i),
                        p0.y + height - pad - static_cast<float>(v) * (height - pad * 2.0f));
    }
    // A faint vertical divider half a step before the first frame of a new timeline.
    for (size_t i : dividers)
    {
        const float dx = pts[i].x - step * 0.5f;
        dl->AddLine(ImVec2(dx, p0.y), ImVec2(dx, p0.y + height), ColGrid(), 1.0f);
    }
    // The line breaks wherever records are missing (lost by the server's ring, or evicted from the
    // driver's queue) and wherever a frame did not measure the quantity: joining the two sides would
    // draw a trend through frames nobody measured.
    for (const HistoryRun& run : SplitHistoryRuns(hp))
    {
        if (run.count >= 2)
            dl->AddPolyline(pts.data() + run.first, static_cast<int>(run.count), color, ImDrawFlags_None, 2.0f);
        else
            dl->AddCircleFilled(pts[run.first], 1.5f, color);   // a lone point between gaps
    }
    for (size_t i = 0; i < n; ++i)
        if (!hp[i].available)   // distinct from every value, 0% included: a short tick mid-strip
            dl->AddLine(ImVec2(pts[i].x, p0.y + height * 0.35f), ImVec2(pts[i].x, p0.y + height * 0.65f),
                        ColNoData(), 1.0f);
    // The frame the panel is anchored at (scrubbed): a mark across the strip, drawn before the hover
    // crosshair so hovering still reads as before.
    if (anchor >= 0 && static_cast<size_t>(anchor) >= begin && static_cast<size_t>(anchor) < end)
    {
        const float ax = pts[static_cast<size_t>(anchor) - begin].x;
        dl->AddLine(ImVec2(ax, p0.y), ImVec2(ax, p0.y + height), ColMarker(), 2.0f);
        dl->AddTriangleFilled(ImVec2(ax - 4.0f, p0.y), ImVec2(ax + 4.0f, p0.y), ImVec2(ax, p0.y + 5.0f), ColMarker());
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
            if (hp[i].available) dl->AddCircleFilled(pts[i], 4.0f, color);
            // The frame's own identity (F3), not the snapshot number its batch happened to arrive with.
            const std::string label = PerfFrameLabel(model.At(begin + i));
            ImGui::SetTooltip("%s\n%s", label.c_str(),
                              hp[i].available ? Percent(hp[i].value).c_str() : "no data (not measured in this frame)");
        }
    }
}

void EmptyState(const ProfilerPanel::Host& host, bool modelEmpty)
{
    if (!host.live)
        ImGui::TextWrapped("The profiler measures a running game. Connect to a live emulator "
                           "(Session > Source > Connect to Emulator) to see where each frame's time goes.");
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

// When a timeline boundary shortened the averaging window, say so in the status line (already
// reserved, so no row moves): a "10 s" span that covers 24 frames would otherwise be a surprise.
// Appended to the scrub note when there is one. Needs mViewEnd and mViewNote to be set.
void ProfilerPanel::NoteWindowCut(const ProfilerModel& model)
{
    bool cut = false;
    const size_t used = model.UsableFrames(WindowFrames(), mViewEnd, &cut);
    if (!cut) return;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Averaging %zu of %zu frames (the rest are from before a rewind or state load)",
                  used, (std::min)(WindowFrames(), mViewEnd));
    if (!mViewNote.empty()) mViewNote += "; ";
    mViewNote += buf;
}

void ProfilerPanel::ResolveView(const ProfilerModel& model, const Host& host)
{
    mViewEnd = model.Count();
    mStripEnd = model.Count();
    mAnchor = -1;
    mViewMissing = false;
    mViewNote.clear();
    mMissingText.clear();
    if (!host.scrubbed)   // live: the newest frame, exactly as before scrubbing existed
    {
        NoteWindowCut(model);
        return;
    }

    size_t at = 0;
    if (host.scrubIdentityKnown && model.FindIdentity(host.scrubTimeline, host.scrubFrame, &at))
    {
        mAnchor = static_cast<long>(at);
        mViewEnd = at + 1;
        // Keep the mark inside the strip's fixed span: a frame older than it moves the span back
        // (half a span of newer frames stay in view) instead of drawing no mark at all.
        if (model.Count() - at > kStripFrames)
            mStripEnd = (std::min)(model.Count(), at + 1 + kStripFrames / 2);
        if (at + 1 < model.Count())
        {
            uint32_t tl = 0; uint64_t newest = 0;
            char buf[128];
            if (model.LatestIdentity(&tl, &newest))
                std::snprintf(buf, sizeof(buf), "Showing frame %llu (scrubbed), newest is %llu",
                              static_cast<unsigned long long>(host.scrubFrame), static_cast<unsigned long long>(newest));
            else
                std::snprintf(buf, sizeof(buf), "Showing frame %llu (scrubbed)", static_cast<unsigned long long>(host.scrubFrame));
            mViewNote = buf;
        }
        NoteWindowCut(model);
        return;
    }

    // No record for the displayed frame. Say so and show nothing per-frame: the newest frame's
    // figures under a scrubbed frame's number would be a plausible, wrong answer.
    mViewMissing = true;
    mViewEnd = 0;
    char buf[320];
    if (host.scrubIdentityKnown)
        std::snprintf(buf, sizeof(buf),
                      "No profiler data for frame %llu (the Profiler was not capturing then, or the frame is older "
                      "than the last %zu kept)",
                      static_cast<unsigned long long>(host.scrubFrame), model.Capacity());
    else
        std::snprintf(buf, sizeof(buf),
                      "No profiler data for frame %llu (its timeline is not known, so no record can be matched to "
                      "it: an emulator that does not state its timeline, or a frame recorded without one)",
                      static_cast<unsigned long long>(host.scrubFrame));
    mMissingText = buf;
    mViewNote = buf;
}

bool ProfilerPanel::Draw(const ProfilerModel& model, const MemoryPoolTracker& pools, const Host& host)
{
    bool clear = false;
    mPoolsVisible = false;
    if (!ImGui::Begin("Profiler")) { ImGui::End(); return false; }
    ResolveView(model, host);

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
    // Always one reserved line, so the rows under it do not move when it gains or loses text.
    FixedStatusLine(mViewNote, mViewMissing ? ColStall() : 0);

    if (ImGui::BeginTabBar("##profiler-tabs"))
    {
        if (ImGui::BeginTabItem("Frame"))
        {
            if (model.Empty() || !host.hasCounters) EmptyState(host, model.Empty());
            if (!model.Empty()) { if (mViewMissing) WrappedText("%s", mMissingText.c_str()); else DrawFrameTab(model); }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Buses"))
        {
            if (model.Empty() || !host.hasCounters) EmptyState(host, model.Empty());
            if (!model.Empty()) { if (mViewMissing) WrappedText("%s", mMissingText.c_str()); else DrawBusTab(model); }
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
    // Every figure is over the frames of the window that CARRY it (see WindowShares): one frame with a
    // measurement does not turn its three neighbours without one into zeros.
    const WindowShares w = model.Shares(WindowFrames(), mViewEnd);
    const PerfShares& s = w.shares;
    const float barH = ImGui::GetTextLineHeight();

    ImGui::Text("Frame length: %s SH-2 cycles", Count(s.frameCycles).c_str());
    ImGui::SameLine(0.0f, 24.0f);
    Swatch(ColBusy(), "Busy (includes bus waits)");
    ImGui::SetItemTooltip("What is left of the frame after sleep and bus stalls. It includes the cycles an\n"
                          "SH-2 spent waiting on slow bus accesses; those are not separated out.");
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
            const bool known = c == 0 || w.slave.Any();
            const CpuShare& cs = s.cpu[c];
            const Segment seg[4] = {
                { known ? cs.busy : 0.0, ColBusy(), "Busy, including waits on slow bus cycles" },
                { known ? cs.stall : 0.0, ColStall(), "Stalled on the bus (DMA)" },
                { known ? cs.sleep : 0.0, ColSleep(), "Asleep (waiting for an interrupt)" },
                { known ? cs.off : 0.0, ColOff(), "Switched off" } };
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(kCpuName[c]);
            ImGui::TableNextColumn(); StackedBar(c ? "##ssh2" : "##msh2", seg, 4, barH);
            ImGui::TableNextColumn();
            if (!known) ImGui::TextDisabled("not available");
            else if (cs.off >= 0.999) ImGui::TextDisabled("off");
            else ImGui::Text("%s busy%s", Percent(cs.busy).c_str(), c ? CoverageNote(w.slave).c_str() : "");
        }
        // VDP1: a v26 server's elapsed figure and a v27 server's command work are different
        // quantities. A window holding both shows both, each over its own frames; neither is ever
        // added to the other. A v27 frame without the producer's bit is "not available", never 0%.
        struct Single { const char* name; const char* id; double v; ImU32 col; const char* tip; Coverage cov; std::string note; };
        std::vector<Single> singles;
        if (w.vdp1Draw.Any())
            singles.push_back({ "VDP1 drawing", "##vdp1d", s.vdp1Draw, ColVdp1(),
                                "VDP1 drawing its command table (a v26 emulator's elapsed figure)", w.vdp1Draw, CoverageNote(w.vdp1Draw) });
        if (w.v27Frames > 0)
            singles.push_back({ "VDP1 command work", "##vdp1w", s.vdp1Work, ColVdp1(),
                                "VDP1 command-work cycles as a share of the frame; above 100% in instant-draw mode",
                                w.vdp1Work, CoverageNote(w.vdp1Work) });
        singles.push_back({ "SCU DSP", "##dsp", s.dsp, ColDsp(), "SCU DSP executing", Coverage{ w.frames, w.frames }, std::string() });
        singles.push_back({ "68000 (sound)", "##m68k", s.m68k, ColM68k(), "68000 executing (share of its own clock)",
                            Coverage{ w.frames, w.frames },
                            // The detail line below pairs idle with executing over the frames that carry the
                            // idle bit; when that is a subset, this row says its own denominator.
                            (w.m68kIdle.Any() && !w.m68kIdle.Full()) ? " (all " + std::to_string(w.frames) + " frames)" : std::string() });
        for (const Single& one : singles)
        {
            const bool known = one.cov.Any();
            const Segment seg[1] = { { known ? one.v : 0.0, one.col, one.tip } };
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(one.name);
            ImGui::TableNextColumn(); StackedBar(one.id, seg, 1, barH);
            ImGui::TableNextColumn();
            if (known) ImGui::Text("%s%s", Percent(one.v).c_str(), one.note.c_str());
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

    // What a v27 server adds. Each line states its own coverage or says it has none; a window of v26
    // frames shows none of them.
    if (w.v27Frames > 0)
    {
        ImGui::SeparatorText("Details");
        if (w.vdp1Work.Any())
        {
            if (s.vdp1WorkOverFrame)
                WrappedText("VDP1 command work exceeds the frame in %zu frame%s: instant-draw mode does a whole "
                            "table's work in no time, so this is work, not elapsed time.",
                            w.vdp1OverFrameFrames, w.vdp1OverFrameFrames == 1 ? "" : "s");
            WrappedText("VDP1 draw budget discarded by DMA: %s of the frame%s (budget, not elapsed time)",
                        Percent(s.vdp1DmaDiscarded).c_str(), CoverageNote(w.vdp1Work).c_str());
        }
        else
            WrappedDisabled("VDP1 draw budget discarded by DMA: not available");
        if (w.m68kIdle.Any())
            WrappedText("68000: %s idle (stopped), %s executing, of its own clock%s",
                        Percent(w.m68kIdleShare).c_str(), Percent(w.m68kIdleExecShare).c_str(), CoverageNote(w.m68kIdle).c_str());
        else
            WrappedDisabled("68000 idle: not available");
        if (w.mode.Any())
            WrappedText("Function attribution: on in %zu of %zu frame%s (the mode each frame ran in)",
                        w.modeFunctionsOn, w.mode.covered, w.mode.covered == 1 ? "" : "s");
        else
            WrappedDisabled("Function attribution mode per frame: not available");
    }

    // Records the emulator or the driver lost: a window averages the frames it really holds, and the
    // history line breaks where records are missing.
    {
        uint64_t winServer = 0, winDriver = 0;
        model.LostIn(WindowFrames(), mViewEnd, &winServer, &winDriver);
        if (model.TotalServerLost() || model.TotalDriverEvicted())
            WrappedText("Lost records: %llu dropped by the emulator's ring, %llu evicted by the driver's queue "
                        "(in this window: %llu and %llu)",
                        static_cast<unsigned long long>(model.TotalServerLost()),
                        static_cast<unsigned long long>(model.TotalDriverEvicted()),
                        static_cast<unsigned long long>(winServer), static_cast<unsigned long long>(winDriver));
    }

    ImGui::SeparatorText(mStripEnd < model.Count()
                             ? "History (0-100% of the frame, the red mark is the frame shown)"
                             : "History (0-100% of the frame, newest on the right)");
    const float stripH = ImGui::GetTextLineHeight() * 2.6f;
    const WindowShares whole = model.Shares(model.Count());
    const char* vdp1Name = whole.v27Frames == 0 ? "VDP1 drawing"
                         : (whole.vdp1Draw.Any() ? "VDP1 drawing / command work" : "VDP1 command work");
    if (ImGui::BeginTable("##history", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("plot", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted("Master SH-2 busy");
        ImGui::TableNextColumn();
        HistoryStrip("##h-msh2", model, mStripEnd, mAnchor, ColBusy(), stripH,
                     [](const PerfFrame& f) { return ComputePerfShares(f.c).cpu[0].busy; });
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted("Slave SH-2 busy");
        ImGui::TableNextColumn();
        HistoryStrip("##h-ssh2", model, mStripEnd, mAnchor, ColBusy(), stripH,
                     [](const PerfFrame& f) { return f.SlaveOnTimeKnown() ? ComputePerfShares(f.c).cpu[1].busy : kNoData; });
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(vdp1Name);
        ImGui::TableNextColumn();
        HistoryStrip("##h-vdp1", model, mStripEnd, mAnchor, ColVdp1(), stripH,
                     [](const PerfFrame& f) {
                         const PerfShares sh = ComputePerfShares(f.c);
                         if (f.LegacyVdp1()) return sh.vdp1Draw;
                         return f.Has(SE_PERF_VALID_VDP1_WORK) ? sh.vdp1Work : kNoData;
                     });
        ImGui::EndTable();
    }
}

void ProfilerPanel::DrawBusTab(const ProfilerModel& model)
{
    uint64_t sum[SE_PERF_COUNT];
    const double frames = static_cast<double>(std::max<size_t>(model.Sum(WindowFrames(), mViewEnd, sum), 1));
    const BusTotals totals = ComputeBusTotals(sum);
    const float barH = ImGui::GetTextLineHeight();

    uint64_t maxDev = 1;
    for (int d = 0; d < SE_PERF_DEV_COUNT; ++d)
        maxDev = std::max(maxDev, sum[SE_PERF_BUS_ACCESS + d * 2] + sum[SE_PERF_BUS_ACCESS + d * 2 + 1]);

    const WindowShares w = model.Shares(WindowFrames(), mViewEnd);
    WrappedText("SH-2 %s per frame", PerfBusUnitLabel());
    ImGui::SameLine(0.0f, 24.0f);
    Swatch(ColBusy(), "Reads");
    ImGui::SameLine(0.0f, 12.0f);
    Swatch(ColStall(), "Writes");
    ImGui::SetItemTooltip("Requests to the SH-2's bus controller that reached the external bus. A split 32-bit\n"
                          "access is one request and a cache line fill is four; a hit in the SH-2's cache is\n"
                          "none, and neither is an instruction fetch the emulator serves from its fast map.\n"
                          "So the counts depend on whether the instruction cache is emulated.");
    if (w.cache.Any())
    {
        if (w.cacheMixed)
            WrappedDisabled("Cache mode: both emulated and not emulated in this window");
        else
            WrappedDisabled("Cache mode: %s%s", PerfCacheModeName(w.cacheMode), CoverageNote(w.cache).c_str());
    }
    else if (w.v27Frames > 0)
        WrappedDisabled("Cache mode: not available (the game has not set it yet)");

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

    // Requests that belong to no device (F8): kept out of every bus total above. A v26 emulator folded
    // them into the CD block and the VDP2 / SCU windows, so only v27 frames carrying them are summed.
    if (w.v27Frames > 0)
    {
        if (w.busX.Any())
        {
            const double bf = static_cast<double>(w.busX.covered);
            const BusTotals& x = w.busXTotals;
            WrappedText("Not on any bus, per frame: %s %s reads, %s writes;  %s %s reads, %s writes%s",
                        PerfXDeviceName(SE_PERF_XDEV_ABUS_DUMMY), Count(x.dummyReads / bf).c_str(),
                        Count(x.dummyWrites / bf).c_str(), PerfXDeviceName(SE_PERF_XDEV_UNMAPPED),
                        Count(x.unmappedReads / bf).c_str(), Count(x.unmappedWrites / bf).c_str(),
                        CoverageNote(w.busX).c_str());
        }
        else
            WrappedDisabled("A-bus dummy area and unmapped requests: not available");
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
        if (pools.SettlingCaptures() > 0)
            ImGui::TextDisabled("The machine state was replaced. This emulator does not say which timeline each capture is of, "
                                "so sampling resumes after %u more captures.", pools.SettlingCaptures());
        else
            ImGui::TextDisabled("No data loaded.");
        return;
    }
    Swatch(ColTrack(), "Uniform");
    ImGui::SameLine(0.0f, 12.0f);
    Swatch(ColData(), "Non-uniform");
    ImGui::SameLine(0.0f, 12.0f);
    Swatch(ColStall(), "Changed since sample");
    ImGui::SetItemTooltip("Each cell is one block of the RAM, classified from its bytes alone.\n"
                          "Uniform: the block is one byte value repeated (cleared, never used, or a table\n"
                          "of identical entries) -- it says nothing about whether the game uses it.\n"
                          "Non-uniform: the block holds more than one byte value.\n"
                          "Changed since sample: its contents differ from the previous sample of the SAME\n"
                          "capture; the colour fades while it stays put. Sampling starts over (nothing is\n"
                          "reported changed) whenever the source, the emulator, the machine-state timeline\n"
                          "OF THE CAPTURE ITSELF or the displayed frame goes backwards -- scrubbing back is a\n"
                          "jump, not a change, and so is a state load.\n"
                          "VDP1 VRAM is the copy taken when VDP1 last finished drawing (the draw-end latch);\n"
                          "until the first draw-end after boot or a state load it is the live VRAM, so a\n"
                          "change there means a draw finished, not that the game wrote at that instant.");

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
        WrappedDisabled("%s at %08X  -  %s non-uniform, %s changed since sample", Bytes(u.size).c_str(), u.base,
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
                                  v == 0 ? "Uniform" : (v == 255 ? "Changed since the last sample"
                                                               : (v > 1 ? "Changed recently" : "Non-uniform")),
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

    if (mViewMissing)
    {
        WrappedText("%s", mMissingText.c_str());
        return;
    }
    // The model's view for this CPU filter: the retained rows, Other, and the denominator they
    // share. Percentages are of Attributed() = listed + Other, so dropping a row to the bounded
    // table cannot make the rest look bigger.
    const FunctionWindow fw = model.Functions(WindowFrames(), mViewEnd, static_cast<FnCpu>(std::min(std::max(mFnCpu, 0), 2)));
    const size_t frames = fw.used;
    if (!frames)
    {
        if (mFnCpu != 0 && model.Functions(WindowFrames(), mViewEnd, FnCpu::Both).attributed > 0)
            ImGui::TextWrapped("No frame in this window carries the per-CPU split of the unlisted time (an "
                               "older emulator). Choose both CPUs to see its functions.");
        else
            ImGui::TextDisabled(mFunctions ? "Waiting for attributed frames..."
                                           : "No attributed frames in this window.");
        return;
    }

    // Fold raw-PC buckets into the function that contains them, when the host can find it, so a
    // function is one row however its time was recorded.
    struct Row { uint32_t address; bool slave; bool known; uint64_t cycles; };
    std::vector<Row> rows;
    std::unordered_map<uint64_t, size_t> index;
    for (const PerfFunction& f : fw.rows)
    {
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
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.cycles > b.cycles; });

    const double denom = static_cast<double>(std::max<uint64_t>(fw.Attributed(), 1));
    const double perFrame = 1.0 / static_cast<double>(frames);
    ImGui::SameLine();
    WrappedDisabled("self time, callees excluded - %s cycles per frame attributed",
                        Count(static_cast<double>(fw.Attributed()) * perFrame).c_str());
    WrappedDisabled("Listed rows are the retained keys: the emulator keeps the 64 hottest functions of each "
                        "frame out of up to 3072 it tracks. Everything else it attributed is in Other.");
    if (mFnCpu != 0 && frames < fw.attributed)
        WrappedDisabled("%zu of %zu frames in this window carry the per-CPU split and are used.", frames, fw.attributed);
    if (fw.tableFullFrames)
        WrappedDisabled("The table filled in %zu of %zu frames: functions first seen after 3,072 were not "
                            "tracked on their own, and their time is in Other.", fw.tableFullFrames, frames);
    if (fw.truncatedFrames)
        WrappedDisabled("More than 64 functions ran in %zu of %zu frames: the smallest were folded into Other.",
                            fw.truncatedFrames, frames);

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
            ImGui::TableNextColumn(); ImGui::TextUnformatted(Count(static_cast<double>(r.cycles) * perFrame).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Percent(static_cast<double>(r.cycles) / denom).c_str());
            ImGui::TableNextColumn();
            const Segment seg[1] = { { static_cast<double>(r.cycles) / top, ColBusy(), "Relative to the hottest function" } };
            StackedBar("##fnbar", seg, 1, barH);
            ImGui::PopID();
        }

        // Cycles the emulator measured but did not keep a row for.
        {
            ImGui::PushID("other");
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Other (not listed)");
            std::string tip = "Cycles attributed to functions that have no row:\n";
            char buf[160];
            std::snprintf(buf, sizeof(buf), "  not admitted to the per-frame table: %s per frame\n",
                          Count(static_cast<double>(fw.otherNotAdmitted) * perFrame).c_str());
            tip += buf;
            std::snprintf(buf, sizeof(buf), "  below the top 64 of their frame: %s per frame",
                          Count(static_cast<double>(fw.otherEvicted) * perFrame).c_str());
            tip += buf;
            if (fw.otherUnsplit)
            {
                std::snprintf(buf, sizeof(buf), "\n  from an emulator that does not split it: %s per frame",
                              Count(static_cast<double>(fw.otherUnsplit) * perFrame).c_str());
                tip += buf;
            }
            ImGui::SetItemTooltip("%s", tip.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(mFnCpu == 1 ? "Master" : (mFnCpu == 2 ? "Slave" : "Both"));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(Count(static_cast<double>(fw.Other()) * perFrame).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(Percent(static_cast<double>(fw.Other()) / denom).c_str());
            ImGui::TableNextColumn();
            const Segment seg[1] = { { static_cast<double>(fw.Other()) / top, ColSleep(), "Other, relative to the hottest function" } };
            StackedBar("##fnother", seg, 1, barH);
            ImGui::PopID();
        }
        // Busy time nothing was charged for. Not measured cycles and not part of Other, so it has no
        // share of the attributed total: it is the gap between the counters and the sampler.
        if (fw.busyKnown)
        {
            ImGui::PushID("unobserved");
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("Not observed (estimate)");
            ImGui::SetItemTooltip("An ESTIMATE, not a measured counter: busy time (frame cycles minus sleep and stall)\n"
                                  "minus the cycles attributed above, clamped at zero. It stands for time no sampled\n"
                                  "instruction was charged for, for example the stretch after a reset or after\n"
                                  "attribution was switched on. It is not in Other and not in the percentages above.");
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", mFnCpu == 1 ? "Master" : (mFnCpu == 2 ? "Slave" : "Both"));
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", Count(static_cast<double>(fw.Unobserved()) * perFrame).c_str());
            ImGui::TableNextColumn(); ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

}  // namespace sfe
