#include "LogPanel.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "imgui.h"

namespace sfe
{
namespace
{

struct CatStyle { const char* name; ImU32 color; };

const CatStyle& Style(LogCategory c)
{
    static const CatStyle kStyles[static_cast<int>(LogCategory::Count)] = {
        {"INFO",   IM_COL32(170, 176, 188, 255)},
        {"WARN",   IM_COL32(232, 176,  84, 255)},
        {"ERROR",  IM_COL32(232, 104,  92, 255)},
        {"CPU",    IM_COL32(150, 190, 240, 255)},
        {"DMA",    IM_COL32(120, 200, 170, 255)},
        {"VDP",    IM_COL32(200, 160, 230, 255)},
        {"CD",     IM_COL32(180, 190, 120, 255)},
        {"SCRIPT", IM_COL32(150, 200, 150, 255)},
        {"TRACE",  IM_COL32(110, 180, 250, 255)},
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

}  // namespace

void LogPanel::PushEvent(LogEvent e)
{
    e.seq = ++mSeq;
    e.time = NowString();
    mEvents.push_back(std::move(e));
    while (mEvents.size() > mCap) mEvents.pop_front();
}

void LogPanel::Push(LogCategory cat, const std::string& message, uint32_t frame)
{
    LogEvent e;
    e.category = cat;
    e.message = message;
    e.frame = frame;
    PushEvent(std::move(e));
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
    PushEvent(std::move(e));
}

void LogPanel::Clear()
{
    mEvents.clear();
    mSelected = 0;
}

void LogPanel::Draw(Request& req)
{
    if (!mFilterInit)
    {
        for (bool& s : mShow) s = true;
        mFilterInit = true;
    }

    // --- filter chips: "All" + a toggle per category. ---
    if (ImGui::SmallButton("All")) { for (bool& s : mShow) s = true; }
    for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i)
    {
        ImGui::SameLine();
        const CatStyle& st = Style(static_cast<LogCategory>(i));
        const bool on = mShow[i];
        if (!on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? st.color : IM_COL32(120, 120, 120, 255));
        if (ImGui::SmallButton(st.name)) mShow[i] = !mShow[i];
        ImGui::PopStyleColor(on ? 1 : 2);
    }
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

    // --- the event table + a detail pane for the selected row. ---
    const float detailH = mSelected ? ImGui::GetTextLineHeightWithSpacing() * 6.0f : 0.0f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
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

        const bool searching = mSearch[0] != '\0';
        for (const LogEvent& e : mEvents)
        {
            if (!mShow[static_cast<int>(e.category)]) continue;
            if (searching && e.message.find(mSearch) == std::string::npos) continue;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(e.seq));
            const bool sel = e.seq == mSelected;
            if (ImGui::Selectable(e.time.c_str(), sel,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
            {
                mSelected = e.seq;
                if (e.hasSource && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    req.jumpAssembly = true; req.jumpCpu = e.cpu; req.jumpAddr = e.pc;
                }
            }
            ImGui::TableSetColumnIndex(1);
            if (e.frame) ImGui::Text("%u", e.frame); else ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(2);
            const CatStyle& st = Style(e.category);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(st.color), "%s", st.name);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                e.category == LogCategory::Tracepoint ? st.color : IM_COL32(215, 217, 222, 255)),
                "%s", e.message.c_str());
            ImGui::PopID();
        }
        if (mAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndTable();
    }

    // --- detail pane for the selected entry. ---
    if (mSelected)
    {
        const LogEvent* sel = nullptr;
        for (const LogEvent& e : mEvents) if (e.seq == mSelected) { sel = &e; break; }
        if (!sel) { mSelected = 0; return; }

        ImGui::Separator();
        if (ImGui::BeginChild("##logdetail", ImVec2(0, 0), false))
        {
            ImGui::Text("%s  [%s]  frame %u", sel->time.c_str(), Style(sel->category).name, sel->frame);
            if (sel->hasSource)
            {
                ImGui::Text("Source: %s SH-2  PC=%08X", sel->cpu ? "Slave" : "Master", sel->pc);
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
            for (const auto& kv : sel->detail)
                ImGui::BulletText("%s = %s", kv.first.c_str(), kv.second.c_str());
        }
        ImGui::EndChild();
    }
}

}  // namespace sfe
