#include "LogPanel.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "imgui.h"

#include "HwEventFormat.h"
#include "PanelWidgets.h"  // RowSelectable: every span-all row goes through it
#include "Settings.h"

namespace sfe
{
namespace
{

struct CatStyle { const char* name; ImU32 color; };

CatStyle Style(LogCategory c)
{
    const CatStyle kStyles[static_cast<int>(LogCategory::Count)] = {
        {"INFO",   ui::Color(IM_COL32(170, 176, 188, 255), IM_COL32(89,101,121,255))},
        {"WARN",   ui::Color(IM_COL32(232, 176,  84, 255), IM_COL32(128,91,0,255))},
        {"ERROR",  ui::Color(IM_COL32(232, 104,  92, 255), IM_COL32(165,29,36,255))},
        {"CPU",    ui::Color(IM_COL32(150, 190, 240, 255), IM_COL32(30,80,150,255))},
        {"DMA",    ui::Color(IM_COL32(120, 200, 170, 255), IM_COL32(20,104,75,255))},
        {"VDP",    ui::Color(IM_COL32(200, 160, 230, 255), IM_COL32(115,55,150,255))},
        {"CD",     ui::Color(IM_COL32(180, 190, 120, 255), IM_COL32(97,101,20,255))},
        {"SMPC",   ui::Color(IM_COL32(120, 190, 200, 255), IM_COL32(15,100,115,255))},
        {"SCRIPT", ui::Color(IM_COL32(150, 200, 150, 255), IM_COL32(35,105,45,255))},
        {"TRACE",  ui::Color(IM_COL32(110, 180, 250, 255), IM_COL32(20,90,160,255))},
        {"INPUT",  ui::Color(IM_COL32(230, 150, 190, 255), IM_COL32(145,45,95,255))},
    };
    return kStyles[static_cast<int>(c)];
}

std::string NowString()
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec, static_cast<int>(ms.count()));
    return buf;
}

// The hardware chips and what each one arms.
struct HwChip
{
    LogCategory cat;
    uint32_t    bits;          // SE_HWEV_EN_* it arms
    struct Sub { uint32_t bit; const char* label; const char* key; } sub[2];
};
const HwChip kHwChips[] = {
    { LogCategory::CPU,  SE_HWEV_EN_CPU_EXC | SE_HWEV_EN_SCU_INT,
      { { SE_HWEV_EN_CPU_EXC, "SH-2 exceptions", "cpuexceptions" }, { SE_HWEV_EN_SCU_INT, "SCU interrupts", "scuinterrupts" } } },
    { LogCategory::DMA,  SE_HWEV_EN_SCU_DMA | SE_HWEV_EN_SH2_DMAC,
      { { SE_HWEV_EN_SCU_DMA, "SCU DMA", "scudma" }, { SE_HWEV_EN_SH2_DMAC, "On-chip DMAC", "dmac" } } },
    { LogCategory::VDP,  SE_HWEV_EN_VDP1 | SE_HWEV_EN_VDP2,
      { { SE_HWEV_EN_VDP1, "VDP1", "vdp1" }, { SE_HWEV_EN_VDP2, "VDP2", "vdp2" } } },
    { LogCategory::CD,   SE_HWEV_EN_CD,   { { SE_HWEV_EN_CD, "CD block", "cd" }, { 0, nullptr, nullptr } } },
    { LogCategory::SMPC, SE_HWEV_EN_SMPC, { { SE_HWEV_EN_SMPC, "SMPC", "smpc" }, { 0, nullptr, nullptr } } },
};

const HwChip* ChipFor(LogCategory c)
{
    for (const HwChip& h : kHwChips) if (h.cat == c) return &h;
    return nullptr;
}

}  // namespace

constexpr size_t LogPanel::kHwCap;
constexpr size_t LogPanel::kOtherCap;

void LogPanel::PushEvent(LogEvent e, const std::string& time)
{
    e.seq = ++mSeq;
    e.time = time;
    std::deque<LogEvent>& d = e.fromHw ? mHwEvents : mEvents;
    const size_t cap = e.fromHw ? kHwCap : kOtherCap;
    d.push_back(std::move(e));
    if (!mDirty && d.size() <= cap)
    {
        // Common case: nothing evicted, so the filtered list only grows. Appending keeps the
        // per-push cost O(1) instead of rebuilding tens of thousands of entries.
        const LogEvent& n = d.back();
        const bool searching = !mIndexSearch.empty();
        if (mIndexShow[static_cast<int>(n.category)] &&
            (!searching || n.message.find(mIndexSearch) != std::string::npos))
            mIndex.push_back(&n);
        return;
    }
    while (d.size() > cap) d.pop_front();
    mDirty = true;   // the evicted row may be in the list
}

void LogPanel::Push(LogCategory cat, const std::string& message, uint32_t frame)
{
    LogEvent e;
    e.category = cat;
    e.message = message;
    e.frame = frame;
    PushEvent(std::move(e), NowString());
}

void LogPanel::PushBatch(std::vector<LogEvent>& events)
{
    if (events.empty()) return;
    const std::string t = NowString();
    for (LogEvent& e : events) PushEvent(std::move(e), t);
    events.clear();
}

void LogPanel::Tracepoint(uint32_t frame, int cpu, uint32_t pc, const std::string& message,
                          std::vector<std::pair<std::string, std::string>> detail)
{
    LogEvent e;
    e.category = LogCategory::Tracepoint;
    e.message = message;
    e.frame = frame;
    e.hasSource = true;
    e.cpu = cpu;
    e.pc = pc;
    e.detail = std::move(detail);
    PushEvent(std::move(e), NowString());
}

void LogPanel::Clear()
{
    mEvents.clear();
    mHwEvents.clear();
    mIndex.clear();
    mLastSubmitted.clear();
    mDirty = true;
    mSelected = 0;
}

void LogPanel::EnsureFilterInit()
{
    if (mFilterInit) return;
    for (bool& s : mShow) s = true;
    // Input is a high-volume per-press trace (SE's transmitted mask plus the emulator's
    // receive/translate/merge lines and its own host keypresses). Useful when chasing a
    // controller problem, noise the rest of the time — off until the INPUT chip is on.
    mShow[static_cast<int>(LogCategory::Input)] = false;
    // The hardware chips are capture toggles: they start hidden and are armed by a click (or by
    // the saved choice), so a session that never uses the feature never asks the emulator for anything.
    for (const HwChip& h : kHwChips) mShow[static_cast<int>(h.cat)] = (mHwArmed & h.bits) != 0;
    mFilterInit = true;
}

bool& LogPanel::CategoryVisible(LogCategory c)
{
    EnsureFilterInit();   // may be called before the first Draw()
    return mShow[static_cast<int>(c)];
}

void LogPanel::Load(const Settings& s)
{
    mHwArmed = 0;
    for (const HwChip& h : kHwChips)
        for (const HwChip::Sub& sub : h.sub)
            if (sub.bit && s.GetBool("Log", sub.key, false)) mHwArmed |= sub.bit;
    mFilterInit = false;   // visibility follows what is armed
}

void LogPanel::Store(Settings& s) const
{
    for (const HwChip& h : kHwChips)
        for (const HwChip::Sub& sub : h.sub)
            if (sub.bit) s.SetBool("Log", sub.key, (mHwArmed & sub.bit) != 0);
}

bool LogPanel::ChipRect(LogCategory c, float* x0, float* y0, float* x1, float* y1) const
{
    const int i = static_cast<int>(c);
    if (!mChipDrawn[i]) return false;
    *x0 = mChipRect[i][0]; *y0 = mChipRect[i][1]; *x1 = mChipRect[i][2]; *y1 = mChipRect[i][3];
    return true;
}

const LogEvent* LogPanel::Find(uint64_t seq) const
{
    const std::deque<LogEvent>* streams[2] = { &mEvents, &mHwEvents };
    for (const std::deque<LogEvent>* d : streams)
    {
        // Each deque is ordered by seq, so the selected row is found without walking 50000 entries.
        size_t lo = 0, hi = d->size();
        while (lo < hi)
        {
            const size_t mid = (lo + hi) / 2;
            if ((*d)[mid].seq < seq) lo = mid + 1; else hi = mid;
        }
        if (lo < d->size() && (*d)[lo].seq == seq) return &(*d)[lo];
    }
    return nullptr;
}

void LogPanel::RebuildIfDirty()
{
    EnsureFilterInit();
    bool same = !mDirty && mIndexSearch == mSearch;
    for (int i = 0; same && i < static_cast<int>(LogCategory::Count); ++i)
        if (mIndexShow[i] != mShow[i]) same = false;
    if (same) return;

    mIndexSearch = mSearch;
    for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i) mIndexShow[i] = mShow[i];
    mIndex.clear();
    mIndex.reserve(mEvents.size() + mHwEvents.size());
    const bool searching = !mIndexSearch.empty();
    // Merge the two seq-ordered streams back into one timeline.
    size_t a = 0, b = 0;
    while (a < mEvents.size() || b < mHwEvents.size())
    {
        const bool takeA = b >= mHwEvents.size() || (a < mEvents.size() && mEvents[a].seq < mHwEvents[b].seq);
        const LogEvent& e = takeA ? mEvents[a++] : mHwEvents[b++];
        if (mShow[static_cast<int>(e.category)] &&
            (!searching || e.message.find(mIndexSearch) != std::string::npos))
            mIndex.push_back(&e);
    }
    mDirty = false;
}

void LogPanel::DrawChips()
{
    if (ImGui::SmallButton("All")) { for (bool& s : mShow) s = true; }   // visibility only: never arms capture
    for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i)
    {
        ImGui::SameLine();
        const LogCategory cat = static_cast<LogCategory>(i);
        const CatStyle& st = Style(cat);
        const HwChip* hw = ChipFor(cat);
        const bool on = mShow[i];
        if (!hw)
        {
            if (!on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            ImGui::PushStyleColor(ImGuiCol_Text, on ? st.color : ImGui::GetColorU32(ImGuiCol_TextDisabled));
            if (ImGui::SmallButton(st.name)) mShow[i] = !mShow[i];
            ImGui::PopStyleColor(on ? 1 : 2);
            continue;
        }

        const bool enabled = mHw.live && mHw.capable;
        const bool armed = (mHwArmed & hw->bits) != 0;
        const bool pending = enabled && (mHwArmed & hw->bits & ~mHw.applied) != 0;
        // Armed = full colour, shown but not capturing = dimmed, hidden = disabled text.
        ImU32 col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        if (on)
        {
            ImVec4 c = ImGui::ColorConvertU32ToFloat4(st.color);
            if (!armed) c.w *= 0.55f;
            col = ImGui::ColorConvertFloat4ToU32(c);
        }
        char label[48];
        std::snprintf(label, sizeof(label), "%s%s###chip%d", st.name, pending ? " (pending)" : "", i);
        if (!on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        if (!enabled) ImGui::BeginDisabled();
        if (ImGui::SmallButton(label))
        {
            if (armed) { mHwArmed &= ~hw->bits; mShow[i] = false; }
            else       { mHwArmed |= hw->bits;  mShow[i] = true; }
            mSettingsDirty = true;
        }
        if (!enabled) ImGui::EndDisabled();
        ImGui::PopStyleColor(on ? 1 : 2);

        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        mChipRect[i][0] = mn.x; mChipRect[i][1] = mn.y; mChipRect[i][2] = mx.x; mChipRect[i][3] = mx.y;
        mChipDrawn[i] = true;

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
            if (!mHw.live)
                ImGui::TextUnformatted("Hardware events need a live emulator connection. Your setting is kept and applies when you connect.");
            else if (!mHw.capable)
                ImGui::TextUnformatted("This emulator build has no hardware event support. Your setting is kept.");
            else
            {
                ImGui::Text("%s: click to capture and show, click again to stop. Right-click to pick sources.", st.name);
                ImGui::Text("Capturing costs a little emulator time, and events are capped at %u per source per frame; the rest is summarised as \"+N more\".",
                            static_cast<unsigned>(SE_HWEV_FRAME_CAP));
                if (pending) ImGui::TextUnformatted("Pending: the emulator applies the change at the next frame boundary.");
            }
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        // Scoped by chip: a popup id is hashed from the ID stack, so without this every chip would open
        // (and the first one would draw) the same menu.
        ImGui::PushID(i);
        if (enabled && ImGui::BeginPopupContextItem("##hwchipmenu"))
        {
            for (const HwChip::Sub& sub : hw->sub)
            {
                if (!sub.bit) continue;
                bool v = (mHwArmed & sub.bit) != 0;
                const bool unsupported = mHw.statusValid && !(mHw.supported & sub.bit);
                if (ImGui::MenuItem(sub.label, unsupported ? "not in this build" : nullptr, &v, !unsupported))
                {
                    if (v) mHwArmed |= sub.bit; else mHwArmed &= ~sub.bit;
                    mShow[i] = (mHwArmed & hw->bits) != 0;   // armed = shown, none armed = hidden
                    mSettingsDirty = true;
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
}

void LogPanel::Draw(Request& req)
{
    EnsureFilterInit();
    DrawChips();

    // Own row for the trailing controls. The category chips grow with the filter set and
    // routinely consume the panel's whole width, which pushed the search box, Auto-Scroll
    // and Clear past the right edge — invisible and unclickable. A measured wrap turned out
    // to be unreliable (the chips' true width is only known after layout), so give them a
    // dedicated line unconditionally: one text row, and Clear is always reachable.
    ImGui::NewLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##logsearch", "search", mSearch, sizeof(mSearch));
    ImGui::SameLine();
    ImGui::Checkbox("Auto-Scroll", &mAutoScroll);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) Clear();

    RebuildIfDirty();
    mLastSubmitted.clear();

    // --- the event table + a detail pane for the selected row. ---
    // A12-26: the pane is six lines, but never more than half of what the panel has left. In the default layout the Log tab is a
    // short strip; with a fixed six lines the table's height went negative (ImGui reads that as "all but this much") and the pane
    // was left a few pixels, so selecting a row seemed to open nothing. The pane scrolls when it is short.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    float detailH = mSelected ? ImGui::GetTextLineHeightWithSpacing() * 6.0f : 0.0f;
    if (detailH > avail.y * 0.5f) detailH = avail.y * 0.5f;
    const float tableH = avail.y - detailH;

    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##log", 4, tf, ImVec2(0, tableH)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 84.0f);
        ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("Cat", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Only the rows on screen are submitted: the list can hold 50000.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(mIndex.size()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const LogEvent& e = *mIndex[static_cast<size_t>(row)];
                mLastSubmitted.push_back(&e);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(e.seq));
                const bool sel = e.seq == mSelected;
                if (RowSelectable(e.time.c_str(), sel, false, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    mSelected = e.seq;
                    if (e.hasSource && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        req.jumpAssembly = true; req.jumpCpu = e.cpu; req.jumpAddr = e.pc;
                    }
                }
                // A12-18: the end of a long message (an OS reason such as "Access is denied.") can lie past
                // the panel edge. Hovering the row shows the whole text wrapped; right-click copies it.
                const bool rowHovered = ImGui::IsItemHovered();
                if (ImGui::BeginPopupContextItem("##logrowmenu"))
                {
                    if (ImGui::MenuItem("Copy message")) ImGui::SetClipboardText(e.message.c_str());
                    ImGui::EndPopup();
                }
                ImGui::TableSetColumnIndex(1);
                if (e.frame) ImGui::Text("%u", e.frame); else ImGui::TextDisabled("-");
                ImGui::TableSetColumnIndex(2);
                const CatStyle& st = Style(e.category);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(st.color), "%s", st.name);
                ImGui::TableSetColumnIndex(3);
                const bool clipped = ImGui::CalcTextSize(e.message.c_str()).x > ImGui::GetContentRegionAvail().x;
                if (rowHovered && clipped && !ImGui::IsPopupOpen("##logrowmenu"))
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
                    ImGui::TextUnformatted(e.message.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                    e.category == LogCategory::Tracepoint ? st.color : ImGui::GetColorU32(ImGuiCol_Text)),
                    "%s", e.message.c_str());
                ImGui::PopID();
            }
        }
        if (mAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndTable();
    }

    // --- detail pane for the selected entry. ---
    if (mSelected)
    {
        const LogEvent* sel = Find(mSelected);
        if (!sel) { mSelected = 0; return; }

        ImGui::Separator();
        if (ImGui::BeginChild("##logdetail", ImVec2(0, 0), false))
        {
            ImGui::Text("%s  [%s]  frame %u", sel->time.c_str(), Style(sel->category).name, sel->frame);
            if (sel->hasSource)
            {
                ImGui::Text("Source: %s SH-2  PC=%08X%s", sel->cpu ? "Slave" : "Master", sel->pc,
                            sel->pcApprox ? " (approximate)" : "");
                ImGui::SameLine();
                if (ImGui::SmallButton("Jump to Assembly"))
                {
                    req.jumpAssembly = true; req.jumpCpu = sel->cpu; req.jumpAddr = sel->pc;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Jump to Memory"))
                {
                    req.jumpHex = true; req.hexAddr = sel->pc;
                }
            }
            ImGui::TextWrapped("%s", sel->message.c_str());
            if (ImGui::SmallButton("Copy message")) ImGui::SetClipboardText(sel->message.c_str());
            if (sel->hasRecord)
                for (const auto& kv : HwEventDetail(sel->hw))
                    ImGui::BulletText("%s = %s", kv.first.c_str(), kv.second.c_str());
            for (const auto& kv : sel->detail)
                ImGui::BulletText("%s = %s", kv.first.c_str(), kv.second.c_str());
        }
        ImGui::EndChild();
    }
}

}  // namespace sfe
