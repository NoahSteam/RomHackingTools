#include "WatchPanel.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace sfe
{

namespace
{
const char* kSessionFile = "saturn_explorer_watches.json";

// Change-highlight base colours (alpha applied from the fade timer).
constexpr ImU32 kUpCol      = IM_COL32(40, 150, 70, 255);    // value increased
constexpr ImU32 kDownCol    = IM_COL32(175, 55, 55, 255);    // value decreased
constexpr ImU32 kNeutralCol = IM_COL32(170, 150, 45, 255);   // changed, no direction

void CopyStr(char* dst, size_t cap, const std::string& s)
{
    const size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
    std::memcpy(dst, s.data(), n);
    dst[n] = '\0';
}
}  // namespace

WatchPanel::WatchPanel() = default;

WatchPanel::Row& WatchPanel::RowFor(const WatchEntry& e)
{
    Row& row = mRows[e.id];
    if (!row.seeded)
    {
        CopyStr(row.nameBuf, sizeof(row.nameBuf), e.name);
        CopyStr(row.exprBuf, sizeof(row.exprBuf), e.expression);
        row.seeded = true;
    }
    return row;
}

void WatchPanel::Refresh(IMemoryBackend& backend, IExpressionResolver& resolver)
{
    std::vector<MemoryReadRequest> reqs;
    std::vector<uint64_t>          ids;
    std::vector<WatchType>         types;

    for (WatchEntry& e : mList.Entries())
    {
        Row& row = RowFor(e);
        // Expressions only change on edit/import, so resolve once per new text and
        // reuse the cached address on subsequent refreshes (no string churn at 10Hz).
        if (row.resolvedExpr != row.exprBuf)
        {
            row.resolvedExpr = row.exprBuf;
            uint32_t addr = 0;
            std::string err;
            row.resolved = resolver.Resolve(row.exprBuf, addr, err);
            row.address = addr;
            row.error = row.resolved ? std::string() : err;
        }
        if (!row.resolved)
        {
            row.hasValue = false;
            row.value = WatchValue{};
            continue;
        }
        if (!e.enabled) { continue; }
        reqs.push_back({ row.address, WatchTypeSize(e.type) });
        ids.push_back(e.id);
        types.push_back(e.type);
    }

    // One batched request set per refresh (not one call per watch). The backend is
    // synchronous here — it copies from the already-captured snapshot — so there is
    // no async response to race; an async remote backend would add a generation
    // guard at this seam.
    const std::vector<MemoryReadResult> results = backend.ReadMemoryBatch(reqs);
    for (size_t k = 0; k < results.size() && k < ids.size(); ++k)
    {
        Row& row = mRows[ids[k]];
        WatchValue v = FormatWatchValue(types[k], results[k]);
        if (v.valid)
        {
            if (row.hasLast && v.numeric != row.lastNumeric)
            {
                row.highlightCol = !v.numericMeaningful ? kNeutralCol
                                 : (v.numeric > row.lastNumeric ? kUpCol : kDownCol);
                row.highlight = 1.0f;
            }
            row.lastNumeric = v.numeric;
            row.hasLast = true;
        }
        row.value = std::move(v);
        row.hasValue = true;
    }
}

void WatchPanel::Draw(IMemoryBackend& backend, IExpressionResolver& resolver,
                      IPlatform& platform, float dt)
{
    if (!ImGui::Begin("Watch"))
    {
        ImGui::End();
        return;
    }

    // Refresh values at ~mRefreshHz; fade highlights every frame.
    mRefreshAccum += dt;
    const float period = mRefreshHz > 0.0f ? 1.0f / mRefreshHz : 0.1f;
    if (mRefreshAccum >= period)
    {
        mRefreshAccum = 0.0f;
        Refresh(backend, resolver);
    }
    for (auto& kv : mRows)
    {
        if (kv.second.highlight > 0.0f) kv.second.highlight -= dt;
    }

    // --- Toolbar ---
    if (ImGui::Button("+ Add Watch"))
    {
        mList.Add("", "0x06000000", WatchType::U16, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Import")) DoImport(platform);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Load a watch list (JSON).");
    ImGui::SameLine();
    if (ImGui::Button("Export")) DoExport(platform);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save the watch list (JSON).");
    ImGui::SameLine();
    if (ImGui::Button("Clear All")) { mList.Clear(); mRows.clear(); }
    ImGui::SameLine();
    if (!backend.Connected()) { ImGui::TextDisabled("(disconnected)"); }
    else                      { ImGui::TextDisabled("%d watches", (int)mList.Entries().size()); }

    if (!mNotice.empty())
    {
        mNoticeTime -= dt;
        if (mNoticeTime <= 0.0f) mNotice.clear();
        else ImGui::TextWrapped("%s", mNotice.c_str());
    }

    // --- Table ---
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
    if (!ImGui::BeginTable("watches", 6, flags))
    {
        ImGui::End();
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##en", ImGuiTableColumnFlags_WidthFixed, 22.0f);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.25f);
    ImGui::TableSetupColumn("Address / Expression", ImGuiTableColumnFlags_WidthStretch, 0.35f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.15f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.25f);
    ImGui::TableSetupColumn("##opt", ImGuiTableColumnFlags_WidthFixed, 26.0f);
    ImGui::TableHeadersRow();

    int deleteIndex = -1;
    auto& entries = mList.Entries();
    for (size_t i = 0; i < entries.size(); ++i)
    {
        WatchEntry& e = entries[i];
        Row& row = RowFor(e);
        ImGui::PushID((int)e.id);
        ImGui::TableNextRow();

        // Enabled.
        ImGui::TableSetColumnIndex(0);
        ImGui::Checkbox("##en", &e.enabled);

        // Name (borderless inline edit).
        ImGui::TableSetColumnIndex(1);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (mFocusNameRow == (int)i) { ImGui::SetKeyboardFocusHere(); mFocusNameRow = -1; }
        ImGui::InputText("##name", row.nameBuf, sizeof(row.nameBuf));   // commits below
        if (ImGui::IsItemDeactivatedAfterEdit()) e.name = row.nameBuf;

        // Address / Expression (borderless inline edit; normalize + resolve on commit).
        ImGui::TableSetColumnIndex(2);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##expr", row.exprBuf, sizeof(row.exprBuf),
                         ImGuiInputTextFlags_CharsNoBlank);
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            std::string norm = NormalizeExpression(row.exprBuf);
            CopyStr(row.exprBuf, sizeof(row.exprBuf), norm);
            e.expression = norm;
            Refresh(backend, resolver);   // reflect the change immediately
        }
        ImGui::PopStyleColor();

        // Type (dropdown; commits on selection).
        ImGui::TableSetColumnIndex(3);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##type", WatchTypeName(e.type)))
        {
            for (WatchType t : kAllWatchTypes)
            {
                const bool sel = (t == e.type);
                if (ImGui::Selectable(WatchTypeName(t), sel))
                {
                    e.type = t;
                    Refresh(backend, resolver);
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // Value (+ swatch, change highlight, error tooltip).
        ImGui::TableSetColumnIndex(4);
        if (row.highlight > 0.0f)
        {
            const int alpha = (int)(row.highlight * 140.0f);
            const ImU32 col = (row.highlightCol & 0x00FFFFFFu) |
                              ((alpha < 0 ? 0 : alpha) << 24);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, col);
        }
        if (!e.enabled)
        {
            ImGui::TextDisabled("(disabled)");
        }
        else if (!row.resolved)
        {
            ImGui::TextColored(ImVec4(0.85f, 0.4f, 0.4f, 1.0f), "Invalid address");
            if (ImGui::IsItemHovered() && !row.error.empty())
                ImGui::SetTooltip("%s", row.error.c_str());
        }
        else if (!row.hasValue || !row.value.valid)
        {
            ImGui::TextColored(ImVec4(0.8f, 0.55f, 0.35f, 1.0f), "%s",
                               row.hasValue ? row.value.text.c_str() : "Unavailable");
        }
        else
        {
            const WatchValue& v = row.value;
            if (v.hasSwatch)
            {
                ImVec2 p = ImGui::GetCursorScreenPos();
                const float h = ImGui::GetTextLineHeight();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    p, ImVec2(p.x + h, p.y + h), IM_COL32(v.r, v.g, v.b, 255));
                ImGui::GetWindowDrawList()->AddRect(
                    p, ImVec2(p.x + h, p.y + h), IM_COL32(20, 20, 24, 255));
                ImGui::Dummy(ImVec2(h + 4.0f, h));
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(v.text.c_str());
            if (v.isPointer && v.pointerSuspicious && ImGui::IsItemHovered())
                ImGui::SetTooltip("Target 0x%08X is outside known Saturn RAM/VRAM ranges.",
                                  v.pointerTarget);
        }

        // Options (overflow menu).
        ImGui::TableSetColumnIndex(5);
        if (ImGui::SmallButton("...")) ImGui::OpenPopup("rowmenu");
        if (ImGui::BeginPopup("rowmenu"))
        {
            if (ImGui::MenuItem("Edit"))            mFocusNameRow = (int)i;
            if (ImGui::MenuItem("Delete"))          deleteIndex = (int)i;
            if (ImGui::MenuItem(e.enabled ? "Disable" : "Enable")) e.enabled = !e.enabled;
            ImGui::Separator();
            if (ImGui::MenuItem("Copy Address"))
            {
                char b[16]; std::snprintf(b, sizeof(b), "0x%08X", row.address);
                ImGui::SetClipboardText(row.resolved ? b : e.expression.c_str());
            }
            if (ImGui::MenuItem("Copy Value", nullptr, false, row.hasValue))
                ImGui::SetClipboardText(row.value.text.c_str());
            ImGui::Separator();
            ImGui::BeginDisabled();
            ImGui::MenuItem("View in Hex Editor");
            ImGui::MenuItem("Break on Read");
            ImGui::MenuItem("Break on Write");
            ImGui::MenuItem("Break on Read or Write");
            ImGui::EndDisabled();
            ImGui::TextDisabled("  (hex editor + breakpoints: later phase)");
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }
    ImGui::EndTable();

    if (deleteIndex >= 0)
    {
        const uint64_t id = entries[(size_t)deleteIndex].id;
        mRows.erase(id);
        mList.RemoveAt((size_t)deleteIndex);
    }

    ImGui::End();
}

void WatchPanel::DoImport(IPlatform& platform)
{
    std::string path;
    if (!platform.OpenFileDialog(path)) return;
    std::ifstream f(path, std::ios::binary);
    if (!f) { mNotice = "Import failed: cannot open file."; mNoticeTime = 6.0f; return; }
    std::stringstream ss; ss << f.rdbuf();
    std::vector<std::string> errors;
    const int n = mList.FromJson(ss.str(), errors);
    mRows.clear();   // reseed row buffers from the new model
    if (n < 0)
    {
        mNotice = "Import failed: " + (errors.empty() ? std::string("invalid file.") : errors[0]);
    }
    else
    {
        mNotice = "Imported " + std::to_string(n) + " watches";
        if (!errors.empty())
            mNotice += " (" + std::to_string(errors.size()) + " skipped: " + errors[0] + ")";
    }
    mNoticeTime = 6.0f;
}

void WatchPanel::DoExport(IPlatform& platform)
{
    const std::string json = mList.ToJson();
    if (platform.SaveFile("watches.json", json.data(), json.size()))
    {
        mNotice = "Exported " + std::to_string(mList.Entries().size()) + " watches";
        mNoticeTime = 4.0f;
    }
}

// Best-effort session store: a fixed file in the working directory. This is a
// native-desktop stopgap that deliberately bypasses IPlatform — on the web build
// it lands in the ephemeral MEMFS and is effectively a no-op. When a proper
// per-user/app-state seam is added to IPlatform, this should move onto it. Import
// and Export (the durable, user-driven paths) already go through IPlatform.
void WatchPanel::LoadSession()
{
    std::ifstream f(kSessionFile, std::ios::binary);
    if (!f) return;
    std::stringstream ss; ss << f.rdbuf();
    std::vector<std::string> errors;
    mList.FromJson(ss.str(), errors);
    mRows.clear();
}

void WatchPanel::SaveSession() const
{
    std::ofstream f(kSessionFile, std::ios::binary);
    if (f) f << mList.ToJson();
}

}  // namespace sfe
