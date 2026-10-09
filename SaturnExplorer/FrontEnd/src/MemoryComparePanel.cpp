#include "MemoryComparePanel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imgui.h"
#include "PanelWidgets.h"

namespace sfe
{

namespace
{
const ImU32 kColAddr   = IM_COL32(150, 150, 160, 255);
const ImU32 kColByte   = IM_COL32(210, 210, 215, 255);
const ImU32 kColDim    = IM_COL32(110, 110, 120, 255);   // bytes in rows shown only as context
const ImU32 kColHot    = IM_COL32(255, 255, 255, 255);   // a changed byte
const ImU32 kColSel    = IM_COL32(70, 110, 90, 170);
const ImU32 kColHover  = IM_COL32(80, 90, 110, 110);
// Side colours: A is the baseline (blue), B the comparison (orange). The same two tint the
// changed bytes in their own columns and the card titles, so a glance says which side is which.
const ImVec4 kColA     = ImVec4(0.38f, 0.60f, 0.95f, 1.0f);
const ImVec4 kColB     = ImVec4(0.95f, 0.62f, 0.25f, 1.0f);
const ImU32  kTintA    = IM_COL32(70, 120, 215, 160);
const ImU32  kTintB    = IM_COL32(225, 140, 50, 160);

constexpr float kByteGap = 4.0f;
constexpr float kPadX = 4.0f;

const ImU32 kSwatch[kRegionCount] = {
    IM_COL32(120, 140, 170, 255), IM_COL32(120, 140, 170, 255),   // work RAM
    IM_COL32(210, 190, 70, 255),                                   // sound RAM
    IM_COL32(225, 140, 50, 255), IM_COL32(225, 140, 50, 255),     // VDP1 RAM / FB
    IM_COL32(225, 140, 50, 255),                                   // VDP1 regs
    IM_COL32(110, 190, 110, 255),                                  // VDP2 RAM
    IM_COL32(170, 110, 200, 255),                                  // colour RAM
    IM_COL32(110, 190, 110, 255),                                  // VDP2 regs
};

std::string Group(uint64_t n)
{
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
    return s;
}

std::string FormatSize(uint32_t bytes)
{
    char b[32];
    if (bytes >= 0x100000u && bytes % 0x100000u == 0) std::snprintf(b, sizeof(b), "%u MiB", bytes / 0x100000u);
    else if (bytes >= 0x400u && bytes % 0x400u == 0)  std::snprintf(b, sizeof(b), "%u KiB", bytes / 0x400u);
    else                                              std::snprintf(b, sizeof(b), "%u B", bytes);
    return b;
}

// Frames are 60 Hz here; PAL titles read slightly off, so the frame number stays authoritative.
std::string FormatTime(uint64_t frame)
{
    const uint64_t ms = frame * 1000u / 60u;
    char b[32];
    std::snprintf(b, sizeof(b), "%02u:%02u.%03u", static_cast<unsigned>(ms / 60000u),
                  static_cast<unsigned>((ms / 1000u) % 60u), static_cast<unsigned>(ms % 1000u));
    return b;
}

// Address label for a row. Bus-addressed regions show the address the rest of the app uses; an
// image or register file has no bus address worth showing, so it is region-qualified instead.
std::string RowLabel(const RegionRef& ref)
{
    const RegionTraits& t = Traits(ref.id);
    char b[40];
    if (t.space == AddressSpace::Sh2Bus) std::snprintf(b, sizeof(b), "%08X", t.busBase + ref.offset);
    else                                  std::snprintf(b, sizeof(b), "%s+%X", t.name, ref.offset);
    return b;
}

const char* NoBreakReason(RegionId id)
{
    if (id == RegionId::SoundRam) return "Sound RAM is written by the 68K; the emulator's watchpoints only see SH-2 and SCU-DMA writes.";
    if (id == RegionId::Vdp1Fb)   return "VDP1 FB is an image drawn by VDP1, not written over the bus.";
    return "A write watchpoint cannot observe this region.";
}

const char* NoAsmReason(RegionId id)
{
    if (id == RegionId::SoundRam) return "68K code is shown in the Sound CPU tab, not the SH-2 listing.";
    return "Only work RAM holds SH-2 code.";
}

bool EqualsNoCase(const char* a, size_t alen, const char* b)
{
    size_t bi = 0;
    for (size_t i = 0; i < alen; ++i)
    {
        if (a[i] == ' ') continue;
        while (b[bi] == ' ') ++bi;
        if (!b[bi] || std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[bi]))) return false;
        ++bi;
    }
    while (b[bi] == ' ') ++bi;
    return b[bi] == '\0';
}

bool ParseHex(const char* s, uint32_t* out)
{
    while (*s == ' ') ++s;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (!*s) return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(s, &end, 16);
    while (end && *end == ' ') ++end;
    if (!end || *end != '\0') return false;
    *out = static_cast<uint32_t>(v);
    return true;
}

}  // namespace

bool MemoryComparePanel::ParseLocation(const char* text, RegionRef* out)
{
    const char* plus = std::strchr(text, '+');
    uint32_t v = 0;
    if (!plus)
        return ParseHex(text, &v) && Resolve(v, out);
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        const RegionTraits& t = Traits(static_cast<RegionId>(i));
        if (!EqualsNoCase(text, static_cast<size_t>(plus - text), t.name)) continue;
        if (!ParseHex(plus + 1, &v) || v >= t.size) return false;
        *out = { t.id, v };
        return true;
    }
    return false;
}

bool MemoryComparePanel::ActionEnabled(Action action, RegionId region)
{
    const uint8_t caps = Traits(region).caps;
    switch (action)
    {
    case Action::GoToMemory:     return (caps & kCapNavigate) != 0;
    case Action::AddWatch:       return (caps & kCapWatch) != 0;
    case Action::BreakOnWrite:   return (caps & kCapBreakWrite) != 0;
    case Action::ViewInAssembly: return (caps & kCapSh2Code) != 0;
    case Action::ExportDiff:
    case Action::GoToFrame:      return true;
    }
    return false;
}

MemoryComparePanel::GridMetrics MemoryComparePanel::Metrics()
{
    // The widest hex digit sets the byte width (proportional digits A-D are wider than F).
    float digitW = 0.0f;
    for (const char* d = "0123456789ABCDEF"; *d; ++d)
        digitW = std::max(digitW, ImGui::CalcTextSize(d, d + 1).x);
    GridMetrics m;
    m.byteW = digitW * 2.0f + kByteGap;
    m.asciiW = ImGui::CalcTextSize("W").x;
    m.rowH = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
    return m;
}

bool MemoryComparePanel::TakeRequest(Request& out)
{
    if (!mRequested) return false;
    out = mRequest;
    mRequested = false;
    return true;
}

void MemoryComparePanel::Raise(Action action, Side side, bool allRegions)
{
    mRequest = Request();
    mRequest.action = action;
    mRequest.side = side;
    mRequest.allRegions = allRegions;
    if (mSelValid)
    {
        mRequest.ref = { mSelRegion, mSelLo };
        const uint32_t len = mSelHi - mSelLo + 1;
        mRequest.size = len >= 4 ? 4u : len >= 2 ? 2u : 1u;
    }
    else if (mRegionSel >= 0)
    {
        mRequest.ref = { static_cast<RegionId>(mRegionSel), 0 };
    }
    mRequested = true;
}

int MemoryComparePanel::FindLine(const RegionRef& ref) const
{
    const uint32_t rowStart = ref.offset & ~15u;
    for (size_t i = 0; i < mLines.size(); ++i)
        if (mLines[i].kind == Line::Row && mLines[i].region == ref.id && mLines[i].row.ref.offset == rowStart)
            return static_cast<int>(i);
    return -1;
}

void MemoryComparePanel::GoTo(const RegionRef& ref)
{
    mStatus.clear();
    if (mRegionSel >= 0 && mRegionSel != static_cast<int>(ref.id)) mRegionSel = static_cast<int>(ref.id);
    mSelValid = true;
    mSelRegion = ref.id;
    mSelLo = mSelHi = mSelAnchor = ref.offset;
    mScrollLine = -2;   // resolved in Draw, after the line list has been rebuilt for the new region
    mPendingJump = ref;
    mHavePendingJump = true;
}

void MemoryComparePanel::SelectByte(const RegionRef& ref, bool extend)
{
    if (extend && mSelValid && mSelRegion == ref.id)
    {
        mSelLo = std::min(mSelAnchor, ref.offset);
        mSelHi = std::max(mSelAnchor, ref.offset);
        return;
    }
    mSelValid = true;
    mSelRegion = ref.id;
    mSelLo = mSelHi = mSelAnchor = ref.offset;
}

void MemoryComparePanel::Rebuild(const DiffResult& diff)
{
    mLines.clear();
    DiffOptions o;
    o.changesOnly = mChangesOnly;
    o.contextRows = mShowContext ? 2u : 0u;
    const bool all = mRegionSel < 0;
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        if (!all && static_cast<int>(i) != mRegionSel) continue;
        const RegionId id = static_cast<RegionId>(i);
        const std::vector<DiffRow> rows = BuildRows(diff, id, o);
        if (rows.empty()) continue;
        if (all)
        {
            Line h;
            h.kind = Line::Header;
            h.region = id;
            h.changed = diff.regions[i].changedBytes;
            mLines.push_back(h);
        }
        uint32_t nextRow = 0;   // the row index just past the previous kept row
        for (const DiffRow& r : rows)
        {
            const uint32_t idx = r.ref.offset / 16u;
            if (r.gapBefore)
            {
                Line g;
                g.kind = Line::Gap;
                g.region = id;
                g.skipped = idx - nextRow;
                mLines.push_back(g);
            }
            Line l;
            l.kind = Line::Row;
            l.region = id;
            l.row = r;
            mLines.push_back(l);
            nextRow = idx + 1;
        }
    }
    mBuiltA = diff.a.get();
    mBuiltB = diff.b.get();
    mBuiltRegion = mRegionSel;
    mBuiltChangesOnly = mChangesOnly;
    mBuiltShowContext = mShowContext;
}

void MemoryComparePanel::Draw(const DiffResult* diff, bool aAttached, bool bAttached)
{
    if (mFocusRequested) { ImGui::SetNextWindowFocus(); mFocusRequested = false; }
    if (!ImGui::Begin("Memory Compare"))
    {
        ImGui::End();
        return;
    }
    if (!diff || !diff->a || !diff->b)
    {
        ImGui::TextDisabled("Right-click the rewind timeline to set Compare Frame A and Compare Frame B,\n"
                            "then click Compare Memory...");
        ImGui::End();
        return;
    }

    // A new comparison starts from a clean view: nothing selected carries over between two
    // different pairs of snapshots.
    if (diff->a.get() != mBuiltA || diff->b.get() != mBuiltB)
    {
        mBuiltA = diff->a.get();
        mBuiltB = diff->b.get();
        mSelValid = false;
        mRegionSel = -1;
        mStatus.clear();
        mBuiltRegion = -2;   // force a rebuild below
    }

    DrawCards(*diff, aAttached, bAttached);
    DrawToolbar(*diff, aAttached, bAttached);

    if (mBuiltRegion != mRegionSel || mBuiltChangesOnly != mChangesOnly || mBuiltShowContext != mShowContext)
        Rebuild(*diff);

    // A GoTo waits here for the lines of its region to exist.
    if (mHavePendingJump)
    {
        mHavePendingJump = false;
        Rebuild(*diff);
        int line = FindLine(mPendingJump);
        if (line < 0 && mChangesOnly)
        {
            mChangesOnly = false;
            mStatus = "Changes Only turned off to show that row.";
            Rebuild(*diff);
            line = FindLine(mPendingJump);
        }
        mScrollLine = line;
    }

    if (diff->TotalChangedBytes() == 0)
    {
        ImGui::Separator();
        ImGui::TextDisabled("No differences between frame %llu and frame %llu.",
                            static_cast<unsigned long long>(diff->a->origin.frameNo),
                            static_cast<unsigned long long>(diff->b->origin.frameNo));
        ImGui::End();
        return;
    }

    const float lh = ImGui::GetTextLineHeightWithSpacing();
    const float avail = ImGui::GetContentRegionAvail().y;
    const float minTop = lh * 3.0f, minBottom = lh * 4.0f;
    const bool roomy = avail - SplitterHeight() >= minTop + minBottom;
    if (mSplitTop <= 0.0f && roomy) mSplitTop = lh * 7.0f;
    const float topH = SplitTopHeight(mSplitTop, avail, minTop, minBottom);
    if (roomy) mSplitTop = topH;
    if (topH > 0.0f)
    {
        DrawSummary(*diff, topH);
        HorizontalSplitter("##cmpsplit", mSplitTop, avail, minTop, minBottom);
    }
    DrawGrid(*diff, aAttached, bAttached);
    ImGui::End();
}

void MemoryComparePanel::DrawCards(const DiffResult& diff, bool aAttached, bool bAttached)
{
    if (!ImGui::BeginTable("cmpcards", 2, ImGuiTableFlags_SizingStretchSame)) return;
    for (int s = 0; s < 2; ++s)
    {
        const MemSnapshot& snap = s == 0 ? *diff.a : *diff.b;
        const bool attached = s == 0 ? aAttached : bAttached;
        ImGui::TableNextColumn();
        ImGui::TextColored(s == 0 ? kColA : kColB, s == 0 ? "Frame A (baseline)" : "Frame B (compare)");
        ImGui::Text("%llu", static_cast<unsigned long long>(snap.origin.frameNo));
        ImGui::SameLine();
        ImGui::TextDisabled("%s  -  %s", FormatTime(snap.origin.frameNo).c_str(),
                            snap.origin.liveHead ? "live head" : "rewind frame");
        if (!attached)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.3f, 1.0f), "[detached]");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("This frame is no longer in the rewind history. The snapshot is kept\n"
                                  "and still compares, but the timeline cannot go to it.");
        }
    }
    ImGui::EndTable();
}

void MemoryComparePanel::DrawToolbar(const DiffResult& diff, bool aAttached, bool bAttached)
{
    (void)diff;
    ImGui::Checkbox("Changes Only", &mChangesOnly);
    ImGui::SameLine();
    ImGui::Checkbox("Show Context (2 lines)", &mShowContext);
    ImGui::SameLine();
    if (ImGui::Button("Export..."))
        Raise(Action::ExportDiff, Side::A, mRegionSel < 0);
    ImGui::SameLine();
    ImGui::BeginDisabled(!aAttached);
    if (ImGui::Button("Go to A")) Raise(Action::GoToFrame, Side::A, false);
    ImGui::EndDisabled();
    if (!aAttached && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Frame A is no longer in the rewind history.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!bAttached);
    if (ImGui::Button("Go to B")) Raise(Action::GoToFrame, Side::B, false);
    ImGui::EndDisabled();
    if (!bAttached && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Frame B is no longer in the rewind history.");

    ImGui::SameLine();
    ImGui::TextUnformatted("Jump to:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    bool go = ImGui::InputText("##cmpjump", mJumpBuf, sizeof(mJumpBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    go |= ImGui::Button("Go");
    if (go)
    {
        RegionRef ref;
        if (ParseLocation(mJumpBuf, &ref)) GoTo(ref);
        else mStatus = "Not a captured address. Use a bus address (06034F20) or Region+offset (HWRAM+34F20).";
    }
    if (!mStatus.empty()) ImGui::TextDisabled("%s", mStatus.c_str());
}

void MemoryComparePanel::DrawSummary(const DiffResult& diff, float height)
{
    const ImGuiTableFlags tf = ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter |
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("cmpsummary", 4, tf, ImVec2(0.0f, height))) return;
    ImGui::TableSetupColumn("Region", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Changed Bytes", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableSetupColumn("Changed Ranges", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    auto row = [&](int sel, const char* name, const std::string& size, uint64_t bytes, uint64_t ranges, bool showRanges)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(sel);
        const bool none = bytes == 0;
        if (none) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float indent = ImGui::GetTextLineHeight();
        if (sel >= 0)
        {
            const float sz = indent * 0.6f;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + (indent - sz) * 0.5f),
                                                      ImVec2(p.x + sz, p.y + (indent + sz) * 0.5f), kSwatch[sel]);
        }
        // The Selectable spans the whole row; its label starts at the cursor, so indenting
        // leaves room for the swatch.
        ImGui::Indent(indent);
        if (RowSelectable(name, mRegionSel == sel, false)) mRegionSel = sel;
        ImGui::Unindent(indent);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(size.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(Group(bytes).c_str());
        ImGui::TableSetColumnIndex(3);
        if (showRanges) ImGui::TextUnformatted(ranges ? Group(ranges).c_str() : "-");
        if (none) ImGui::PopStyleColor();
        ImGui::PopID();
    };

    uint64_t total = 0, totalRanges = 0, totalSize = 0;
    for (const RegionDiff& r : diff.regions) { total += r.changedBytes; totalRanges += r.rangeCount; }
    for (size_t i = 0; i < kRegionCount; ++i) totalSize += Traits(static_cast<RegionId>(i)).size;
    row(-1, "All Memory", FormatSize(static_cast<uint32_t>(std::min<uint64_t>(totalSize, 0xFFFFFFFFu))),
        total, totalRanges, true);
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        const RegionTraits& t = Traits(static_cast<RegionId>(i));
        row(static_cast<int>(i), t.name, FormatSize(t.size), diff.regions[i].changedBytes,
            diff.regions[i].rangeCount, true);
    }
    ImGui::EndTable();
}

void MemoryComparePanel::DrawGrid(const DiffResult& diff, bool aAttached, bool bAttached)
{
    const GridMetrics gm = Metrics();
    const float hexW = gm.byteW * 16.0f + kPadX;
    const float asciiW = gm.asciiW * 16.0f + kPadX;
    float addrW = 0.0f;
    for (size_t i = 0; i < kRegionCount; ++i)
        addrW = std::max(addrW, ImGui::CalcTextSize(RowLabel({ static_cast<RegionId>(i), 0xFFFFF }).c_str()).x);
    addrW += kPadX * 2.0f;

    const ImGuiID popupId = ImGui::GetID("cmpctx");
    bool openPopup = false;

    const ImGuiTableFlags tf = ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
                               ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("cmpgrid", 5, tf, ImVec2(0.0f, 0.0f)))
    {
        char hA[48], hB[48];
        std::snprintf(hA, sizeof(hA), "Frame A (%llu)", static_cast<unsigned long long>(diff.a->origin.frameNo));
        std::snprintf(hB, sizeof(hB), "Frame B (%llu)", static_cast<unsigned long long>(diff.b->origin.frameNo));
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, addrW);
        ImGui::TableSetupColumn(hA, ImGuiTableColumnFlags_WidthFixed, hexW);
        ImGui::TableSetupColumn(hB, ImGuiTableColumnFlags_WidthFixed, hexW);
        ImGui::TableSetupColumn("ASCII (A)", ImGuiTableColumnFlags_WidthFixed, asciiW);
        ImGui::TableSetupColumn("ASCII (B)", ImGuiTableColumnFlags_WidthFixed, asciiW);
        ImGui::TableHeadersRow();

        if (mScrollLine >= 0)
        {
            ImGui::SetScrollY(static_cast<float>(mScrollLine) * gm.rowH);
            mScrollLine = -1;
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float lineH = ImGui::GetTextLineHeight();
        const bool gridHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        (void)gridHovered;

        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(mLines.size()), gm.rowH);
        while (clip.Step())
        {
            for (int li = clip.DisplayStart; li < clip.DisplayEnd; ++li)
            {
                const Line& line = mLines[static_cast<size_t>(li)];
                ImGui::TableNextRow(0, gm.rowH);
                ImGui::PushID(li);

                if (line.kind == Line::Header)
                {
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(0.8f, 0.85f, 0.95f, 1.0f), "%s", Traits(line.region).name);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled("%s changed bytes", Group(line.changed).c_str());
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(60, 70, 90, 90));
                    ImGui::PopID();
                    continue;
                }
                if (line.kind == Line::Gap)
                {
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled("... %s identical rows", Group(line.skipped).c_str());
                    ImGui::PopID();
                    continue;
                }

                const DiffRow& r = line.row;
                const size_t ri = static_cast<size_t>(r.ref.id);
                const std::vector<uint8_t>& A = diff.a->regions[ri].bytes;
                const std::vector<uint8_t>& B = diff.b->regions[ri].bytes;

                ImGui::TableSetColumnIndex(0);
                ImGui::PushStyleColor(ImGuiCol_Text, kColAddr);
                ImGui::TextUnformatted(RowLabel(r.ref).c_str());
                ImGui::PopStyleColor();

                for (int side = 0; side < 2; ++side)
                {
                    const std::vector<uint8_t>& bytes = side == 0 ? A : B;
                    const ImU32 tint = side == 0 ? kTintA : kTintB;

                    // Hex cell: one invisible button over the 16 bytes, so one click target per
                    // side and the byte under the pointer is worked out from its x.
                    ImGui::TableSetColumnIndex(1 + side);
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushID(side);
                    ImGui::InvisibleButton("##hex", ImVec2(gm.byteW * 16.0f, lineH));
                    const bool hov = ImGui::IsItemHovered();
                    int hovByte = -1;
                    if (hov)
                    {
                        hovByte = std::min(15, std::max(0, static_cast<int>((ImGui::GetIO().MousePos.x - p.x) / gm.byteW)));
                        if (!(r.validMask & (1u << hovByte))) hovByte = -1;
                    }
                    if (hovByte >= 0)
                    {
                        const RegionRef clicked = { r.ref.id, r.ref.offset + static_cast<uint32_t>(hovByte) };
                        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                        {
                            SelectByte(clicked, ImGui::GetIO().KeyShift);
                            mSelSide = side == 0 ? Side::A : Side::B;
                        }
                        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        {
                            if (!(mSelValid && mSelRegion == clicked.id && clicked.offset >= mSelLo && clicked.offset <= mSelHi))
                                SelectByte(clicked, false);
                            mSelSide = side == 0 ? Side::A : Side::B;
                            openPopup = true;
                        }
                    }
                    for (int k = 0; k < 16; ++k)
                    {
                        if (!(r.validMask & (1u << k))) continue;
                        const uint32_t off = r.ref.offset + static_cast<uint32_t>(k);
                        const ImVec2 bp(p.x + k * gm.byteW, p.y);
                        const bool changed = (r.changedMask & (1u << k)) != 0;
                        const bool selected = mSelValid && mSelRegion == r.ref.id && off >= mSelLo && off <= mSelHi;
                        if (changed)
                            dl->AddRectFilled(bp, ImVec2(bp.x + gm.byteW - 1.0f, bp.y + lineH), tint);
                        if (selected)
                            dl->AddRectFilled(bp, ImVec2(bp.x + gm.byteW - 1.0f, bp.y + lineH), kColSel);
                        else if (k == hovByte)
                            dl->AddRectFilled(bp, ImVec2(bp.x + gm.byteW - 1.0f, bp.y + lineH), kColHover);
                        char t[3];
                        std::snprintf(t, sizeof(t), "%02X", bytes[off]);
                        dl->AddText(ImVec2(bp.x + kByteGap * 0.5f, bp.y),
                                    changed ? kColHot : (r.isContext ? kColDim : kColByte), t);
                    }
                    ImGui::PopID();
                }

                for (int side = 0; side < 2; ++side)
                {
                    const std::vector<uint8_t>& bytes = side == 0 ? A : B;
                    ImGui::TableSetColumnIndex(3 + side);
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(gm.asciiW * 16.0f, lineH));
                    for (int k = 0; k < 16; ++k)
                    {
                        if (!(r.validMask & (1u << k))) continue;
                        const uint32_t off = r.ref.offset + static_cast<uint32_t>(k);
                        const bool changed = (r.changedMask & (1u << k)) != 0;
                        const ImVec2 cp(p.x + k * gm.asciiW, p.y);
                        if (changed)
                            dl->AddRectFilled(cp, ImVec2(cp.x + gm.asciiW, cp.y + lineH), side == 0 ? kTintA : kTintB);
                        const uint8_t c = bytes[off];
                        const char ch[2] = { (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.', 0 };
                        const float w = ImGui::CalcTextSize(ch).x;
                        dl->AddText(ImVec2(cp.x + (gm.asciiW - w) * 0.5f, cp.y),
                                    changed ? kColHot : (r.isContext ? kColDim : kColByte), ch);
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (openPopup) ImGui::OpenPopup(popupId);
    DrawContextMenu(aAttached, bAttached);
}

void MemoryComparePanel::DrawContextMenu(bool aAttached, bool bAttached)
{
    if (!ImGui::BeginPopup("cmpctx")) return;
    if (!mSelValid) { ImGui::EndPopup(); return; }
    const RegionTraits& t = Traits(mSelRegion);
    const bool attached = mSelSide == Side::A ? aAttached : bAttached;
    char head[64];
    const RegionRef ref = { mSelRegion, mSelLo };
    std::snprintf(head, sizeof(head), "%s  %s  (frame %c)", t.name, RowLabel(ref).c_str(), mSelSide == Side::A ? 'A' : 'B');
    ImGui::TextDisabled("%s", head);
    ImGui::Separator();

    auto item = [&](const char* label, bool enabled, const char* why, Action a)
    {
        if (ImGui::MenuItem(label, nullptr, false, enabled)) Raise(a, mSelSide, false);
        if (!enabled && why && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", why);
    };
    item(attached ? "Go to Memory" : "Go to Memory (current view)", ActionEnabled(Action::GoToMemory, mSelRegion), nullptr, Action::GoToMemory);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !attached)
        ImGui::SetTooltip("That frame is no longer in the rewind history, so the Memory tab\nshows the current frame, not the snapshot.");
    item("Add to Watch", ActionEnabled(Action::AddWatch, mSelRegion), nullptr, Action::AddWatch);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Watches the running game, not the snapshot.");
    item("Break on Write", ActionEnabled(Action::BreakOnWrite, mSelRegion), NoBreakReason(mSelRegion), Action::BreakOnWrite);
    item("View in Assembly", ActionEnabled(Action::ViewInAssembly, mSelRegion), NoAsmReason(mSelRegion), Action::ViewInAssembly);
    ImGui::Separator();
    if (ImGui::MenuItem("Export Diff (this region)"))
    {
        Raise(Action::ExportDiff, mSelSide, false);
        mRequest.ref = { mSelRegion, 0 };
    }
    ImGui::EndPopup();
}

}  // namespace sfe
