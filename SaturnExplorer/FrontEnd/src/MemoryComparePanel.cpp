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

ImU32 ByteColor(const DiffRow& r, int k)
{
    return (r.changedMask & (1u << k)) ? kColHot : (r.isContext ? kColDim : kColByte);
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
    if (id == RegionId::Vdp1Fb) return "VDP1 FB is an image drawn by VDP1, not written over the bus.";
    return "A write watchpoint cannot observe this region.";
}

// The hint on an enabled Break on Write. Sound RAM is also written by the 68K sound CPU, which the
// emulator's watchpoint cannot see, so what it catches is spelled out.
const char* BreakNote(RegionId id)
{
    if (id == RegionId::SoundRam)
        return "Catches SH-2 and SCU-DMA writes to Sound RAM, not the 68K sound CPU's own writes.\n"
               "Breaks in the running game, not the snapshot.";
    return "Breaks in the running game, not the snapshot.";
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

// Hex digits only, optionally after 0x and surrounded by spaces. No sign, and nothing wider than 32
// bits: strtoul would wrap "106034F20" into a valid address on a 64-bit unsigned long, and accepts a
// leading '-'.
bool ParseHex(const char* s, uint32_t* out)
{
    while (*s == ' ') ++s;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    uint64_t v = 0;
    size_t digits = 0;
    for (; std::isxdigit(static_cast<unsigned char>(*s)); ++s, ++digits)
    {
        const char c = *s;
        v = v * 16 + static_cast<uint64_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
        if (v > 0xFFFFFFFFull) return false;
    }
    while (*s == ' ') ++s;
    if (digits == 0 || *s != '\0') return false;
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

void MemoryComparePanel::Raise(Action action, Side side)
{
    mRequest = Request();
    mRequest.action = action;
    mRequest.side = side;
    if (mSelValid)
    {
        mRequest.ref = { mSelRegion, mSelLo };
        const uint32_t len = mSelHi - mSelLo + 1;
        mRequest.size = len >= 4 ? 4u : len >= 2 ? 2u : 1u;
    }
    mRequested = true;
}

void MemoryComparePanel::RaiseExport(bool allRegions, RegionId region)
{
    Raise(Action::ExportDiff, Side::A);
    mRequest.allRegions = allRegions;
    mRequest.exportRegion = region;
}

bool MemoryComparePanel::InSelection(RegionId region, uint32_t offset) const
{
    return mSelValid && mSelRegion == region && offset >= mSelLo && offset <= mSelHi;
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
    if (mRegionSel >= 0) mRegionSel = static_cast<int>(ref.id);   // follow into the region, but leave All alone
    SelectByte(ref, false);
    // Resolved in Draw: the lines of the byte's region may not exist until then.
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

void MemoryComparePanel::EnsureLines(const DiffResult& diff)
{
    const BuildKey key = Key();
    if (mLinesValid && key == mBuilt) return;

    mLines.clear();
    DiffOptions o;
    o.changesOnly = mChangesOnly;
    if (!mShowContext) o.contextRows = 0;
    const bool all = mRegionSel < 0;
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        if (!all && static_cast<int>(i) != mRegionSel) continue;
        const RegionId id = static_cast<RegionId>(i);
        const std::vector<DiffRow> rows = BuildRows(diff, id, o);
        if (rows.empty()) continue;
        mLines.reserve(mLines.size() + rows.size() + 1);
        if (all)
        {
            Line h;
            h.kind = Line::Header;
            h.region = id;
            h.count = diff.regions[i].changedBytes;
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
                g.count = idx - nextRow;
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
    mBuilt = key;
    mLinesValid = true;
}

void MemoryComparePanel::Draw(const DiffResult* diff, bool aAttached, bool bAttached)
{
    if (mFocusRequested) { ImGui::SetNextWindowFocus(); mFocusRequested = false; }
    const bool open = ImGui::Begin("Memory Compare");
    if (open) DrawBody(diff, aAttached, bAttached);
    ImGui::End();
}

void MemoryComparePanel::DrawBody(const DiffResult* diff, bool aAttached, bool bAttached)
{
    if (!diff || !diff->a || !diff->b)
    {
        ImGui::TextDisabled("Right-click the rewind timeline to set Compare Frame A and Compare Frame B,\n"
                            "then click Compare Memory...");
        return;
    }

    // A new comparison starts from a clean view: neither the selection nor a jump still waiting
    // for its rows carries over onto different bytes. Identity is the shared_ptr control block
    // (weak_ptr::owner_before), not an address, which a freed snapshot's replacement can reuse.
    if (mPairA.owner_before(diff->a) || diff->a.owner_before(mPairA) ||
        mPairB.owner_before(diff->b) || diff->b.owner_before(mPairB))
    {
        mPairA = diff->a;
        mPairB = diff->b;
        mSelValid = false;
        mRegionSel = -1;
        mHavePendingJump = false;
        mScrollLine = -1;
        mStatus.clear();
        mLinesValid = false;
    }

    DrawCards(*diff, aAttached, bAttached);
    DrawToolbar(aAttached, bAttached);

    if (diff->TotalChangedBytes() == 0)
    {
        ImGui::Separator();
        ImGui::TextDisabled("No differences between frame %llu and frame %llu.",
                            static_cast<unsigned long long>(diff->a->origin.frameNo),
                            static_cast<unsigned long long>(diff->b->origin.frameNo));
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

    // After the summary, so a click there shows its region's lines this frame, not the next.
    EnsureLines(*diff);
    if (mHavePendingJump)
    {
        mHavePendingJump = false;
        int line = FindLine(mPendingJump);
        if (line < 0 && mChangesOnly)
        {
            mChangesOnly = false;
            mStatus = "Changes Only turned off to show that row.";
            EnsureLines(*diff);
            line = FindLine(mPendingJump);
        }
        mScrollLine = line;
    }
    DrawGrid(*diff, aAttached, bAttached);
}

void MemoryComparePanel::DrawCards(const DiffResult& diff, bool aAttached, bool bAttached)
{
    if (!ImGui::BeginTable("cmpcards", 2, ImGuiTableFlags_SizingStretchSame)) return;
    for (int s = 0; s < 2; ++s)
    {
        const MemSnapshot& snap = s == 0 ? *diff.a : *diff.b;
        const bool attached = s == 0 ? aAttached : bAttached;
        ImGui::TableNextColumn();
        const bool isA = s == 0;
        ImGui::TextColored(isA ? kColA : kColB, isA ? "Frame A (baseline)" : "Frame B (compare)");
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

void MemoryComparePanel::DrawToolbar(bool aAttached, bool bAttached)
{
    // The controls wrap: each stays on the line only if it fits in the window, so a narrow dock
    // never pushes one out of reach (the grid's own scrollbar does not scroll the toolbar).
    const ImGuiStyle& style = ImGui::GetStyle();
    const float right = ImGui::GetCurrentWindow()->WorkRect.Max.x;
    auto buttonW = [&](const char* label) { return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f; };
    auto checkW = [&](const char* label) { return ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x; };
    auto place = [&](float width)
    {
        if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right) ImGui::SameLine();
    };

    ImGui::Checkbox("Changes Only", &mChangesOnly);
    char ctx[40];
    std::snprintf(ctx, sizeof(ctx), "Show Context (%u lines)", DiffOptions().contextRows);
    place(checkW(ctx));
    ImGui::Checkbox(ctx, &mShowContext);
    // The region the summary has selected, or every region when it is on All Memory.
    place(buttonW("Export..."));
    if (ImGui::Button("Export..."))
        RaiseExport(mRegionSel < 0, mRegionSel < 0 ? RegionId::Lwram : static_cast<RegionId>(mRegionSel));
    for (int s = 0; s < 2; ++s)
    {
        const bool attached = s == 0 ? aAttached : bAttached;
        const char* label = s == 0 ? "Go to A" : "Go to B";
        place(buttonW(label));
        ImGui::BeginDisabled(!attached);
        if (ImGui::Button(label)) Raise(Action::GoToFrame, s == 0 ? Side::A : Side::B);
        ImGui::EndDisabled();
        if (!attached && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("That frame is no longer in the rewind history.");
    }

    // The jump controls move together: label, box and Go start a new line as a group, and the box
    // shrinks to what is left rather than running past the edge.
    const float labelW = ImGui::CalcTextSize("Jump to:").x, goW = buttonW("Go");
    const float gap = style.ItemSpacing.x;
    place(labelW + gap + 60.0f + gap + goW);
    ImGui::TextUnformatted("Jump to:");
    ImGui::SameLine();
    const float room = right - ImGui::GetCursorScreenPos().x - gap - goW;
    ImGui::SetNextItemWidth(std::max(60.0f, std::min(150.0f, room)));
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

    auto row = [&](int sel, const char* name, const std::string& size, uint64_t bytes, uint64_t ranges)
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
        ImGui::TextUnformatted(ranges ? Group(ranges).c_str() : "-");
        if (none) ImGui::PopStyleColor();
        ImGui::PopID();
    };

    uint64_t total = 0, totalRanges = 0;
    uint32_t totalSize = 0;
    for (const RegionDiff& r : diff.regions) { total += r.changedBytes; totalRanges += r.rangeCount; }
    for (size_t i = 0; i < kRegionCount; ++i) totalSize += Traits(static_cast<RegionId>(i)).size;
    row(-1, "All Memory", FormatSize(totalSize), total, totalRanges);
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        const RegionTraits& t = Traits(static_cast<RegionId>(i));
        row(static_cast<int>(i), t.name, FormatSize(t.size), diff.regions[i].changedBytes,
            diff.regions[i].rangeCount);
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
        static const char kHex[] = "0123456789ABCDEF";

        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(mLines.size()), gm.rowH);
        while (clip.Step())
        {
            for (int li = clip.DisplayStart; li < clip.DisplayEnd; ++li)
            {
                const Line& line = mLines[static_cast<size_t>(li)];
                ImGui::TableNextRow(0, gm.rowH);

                if (line.kind == Line::Header)
                {
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(0.8f, 0.85f, 0.95f, 1.0f), "%s", Traits(line.region).name);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled("%s changed bytes", Group(line.count).c_str());
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(60, 70, 90, 90));
                    continue;
                }
                if (line.kind == Line::Gap)
                {
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled("... %s identical rows", Group(line.count).c_str());
                    continue;
                }

                const DiffRow& r = line.row;
                const size_t ri = static_cast<size_t>(r.ref.id);
                ImGui::PushID(li);

                ImGui::TableSetColumnIndex(0);
                ImGui::PushStyleColor(ImGuiCol_Text, kColAddr);
                ImGui::TextUnformatted(RowLabel(r.ref).c_str());
                ImGui::PopStyleColor();

                for (int side = 0; side < 2; ++side)
                {
                    const Side thisSide = side == 0 ? Side::A : Side::B;
                    const std::vector<uint8_t>& bytes = (side == 0 ? diff.a : diff.b)->regions[ri].bytes;

                    // Hex cell: one invisible button over the 16 bytes, so one click target per
                    // side, with the byte under the pointer worked out from its x.
                    ImGui::TableSetColumnIndex(1 + side);
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushID(side);
                    ImGui::InvisibleButton("##hex", ImVec2(gm.byteW * 16.0f, lineH));
                    int hovByte = -1;
                    if (ImGui::IsItemHovered())
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
                            mSelSide = thisSide;
                        }
                        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        {
                            if (!InSelection(clicked.id, clicked.offset)) SelectByte(clicked, false);
                            mSelSide = thisSide;
                            openPopup = true;
                        }
                    }
                    for (int k = 0; k < 16; ++k)
                    {
                        if (!(r.validMask & (1u << k))) continue;
                        const uint32_t off = r.ref.offset + static_cast<uint32_t>(k);
                        const ImVec2 bp(p.x + k * gm.byteW, p.y);
                        const ImVec2 be(bp.x + gm.byteW - 1.0f, bp.y + lineH);
                        if (r.changedMask & (1u << k)) dl->AddRectFilled(bp, be, side == 0 ? kTintA : kTintB);
                        if (InSelection(r.ref.id, off)) dl->AddRectFilled(bp, be, kColSel);
                        else if (k == hovByte)          dl->AddRectFilled(bp, be, kColHover);
                        const char digits[2] = { kHex[bytes[off] >> 4], kHex[bytes[off] & 15] };
                        dl->AddText(ImVec2(bp.x + kByteGap * 0.5f, bp.y), ByteColor(r, k), digits, digits + 2);
                    }
                    ImGui::PopID();
                }

                for (int side = 0; side < 2; ++side)
                {
                    const std::vector<uint8_t>& bytes = (side == 0 ? diff.a : diff.b)->regions[ri].bytes;
                    ImGui::TableSetColumnIndex(3 + side);
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(gm.asciiW * 16.0f, lineH));
                    for (int k = 0; k < 16; ++k)
                    {
                        if (!(r.validMask & (1u << k))) continue;
                        const ImVec2 cp(p.x + k * gm.asciiW, p.y);
                        if (r.changedMask & (1u << k))
                            dl->AddRectFilled(cp, ImVec2(cp.x + gm.asciiW, cp.y + lineH), side == 0 ? kTintA : kTintB);
                        const uint8_t c = bytes[r.ref.offset + static_cast<uint32_t>(k)];
                        const char ch = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.';
                        const float w = ImGui::CalcTextSize(&ch, &ch + 1).x;
                        dl->AddText(ImVec2(cp.x + (gm.asciiW - w) * 0.5f, cp.y), ByteColor(r, k), &ch, &ch + 1);
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
    const bool isA = mSelSide == Side::A;
    const bool attached = isA ? aAttached : bAttached;
    ImGui::TextDisabled("%s  %s  (frame %c)", Traits(mSelRegion).name,
                        RowLabel({ mSelRegion, mSelLo }).c_str(), isA ? 'A' : 'B');
    ImGui::Separator();

    // 'why' explains a disabled item; 'note' is a hint shown while it is enabled.
    auto item = [&](const char* label, Action a, const char* why, const char* note)
    {
        const bool enabled = ActionEnabled(a, mSelRegion);
        if (ImGui::MenuItem(label, nullptr, false, enabled)) Raise(a, mSelSide);
        const char* tip = enabled ? note : why;
        if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tip);
    };
    item(attached ? "Go to Memory" : "Go to Memory (current view)", Action::GoToMemory, nullptr,
         attached ? nullptr : "That frame is no longer in the rewind history, so the Memory tab\n"
                              "shows the current frame, not the snapshot.");
    item("Add to Watch", Action::AddWatch, nullptr, "Watches the running game, not the snapshot.");
    item("Break on Write", Action::BreakOnWrite, NoBreakReason(mSelRegion), BreakNote(mSelRegion));
    item("View in Assembly", Action::ViewInAssembly, NoAsmReason(mSelRegion), nullptr);
    ImGui::Separator();
    if (ImGui::MenuItem("Export Diff (this region)")) RaiseExport(false, mSelRegion);
    ImGui::EndPopup();
}

}  // namespace sfe
