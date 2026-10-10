#include "HexEditorPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "imgui.h"

#include <string>

#include "Debug/BytePatternSearch.h"
#include "Debug/ShiftJis.h"
#include "SaturnRegions.h"

namespace sfe
{

namespace
{
const ImU32 kColAddr    = IM_COL32(150, 150, 160, 255);
const ImU32 kColByte    = IM_COL32(210, 210, 215, 255);
const ImU32 kColZero    = IM_COL32(110, 110, 120, 255);   // dim 00 bytes
const ImU32 kColUnmapped = IM_COL32(80, 80, 90, 255);     // "--" outside any region
const ImU32 kColChanged = IM_COL32(235, 170, 70, 255);    // amber (Highlight Changes)
const ImU32 kColText    = IM_COL32(180, 195, 170, 255);
const ImU32 kColJp      = IM_COL32(130, 190, 210, 255);   // double-byte (Shift-JIS) marker
const ImU32 kColSelBg   = IM_COL32(70, 110, 90, 150);     // selection tint
const ImU32 kColHoverBg = IM_COL32(80, 90, 110, 90);      // hovered-cell tint (no per-byte widget)
const ImU32 kColEditBg  = IM_COL32(120, 95, 40, 170);     // byte with a typed first digit pending

// Grid geometry. The byte column is sized from the edit box so the box always fits it:
// column content = edit box + a small gap, edit box = two digits + its own padding + room for
// the caret. (TableSetupColumn's width is the content width; the cell padding is added outside.)
constexpr float kCellPadX  = 2.0f;
constexpr float kEditPadX  = 1.0f;
constexpr float kCaretRoom = 2.0f;
constexpr float kByteGap   = 2.0f;

int64_t SelLo(int64_t a, int64_t b) { return a < b ? a : b; }
int64_t SelHi(int64_t a, int64_t b) { return a > b ? a : b; }
}  // namespace

const std::vector<HexEditorPanel::Region>& HexEditorPanel::Regions()
{
    // The captured regions (SaturnRegions.h), behind an "All" tab covering the whole 27-bit
    // canonical CPU space. "All" is a tab-strip idea rather than a memory-map one, which is
    // why it is added here and not in the shared table. Sound RAM is the SCSP's 512 KiB and
    // is only populated when the source supplies it (live v13+); it is empty otherwise.
    static const std::vector<Region> kRegions = [] {
        std::vector<Region> v{ { "All", 0x00000000u, 0x08000000u } };
        size_t count = 0;
        const SaturnRegion* regions = SaturnRegions(count);
        for (size_t i = 0; i < count; ++i)
            v.push_back({ regions[i].name, regions[i].base, regions[i].size });
        return v;
    }();
    return kRegions;
}

int HexEditorPanel::RegionForAddr(uint32_t addr)
{
    const int i = SaturnRegionIndex(addr);
    return i < 0 ? 0 : i + 1;   // 0 is the "All" tab; the shared table starts at 1 here
}

HexEditorPanel::GridMetrics HexEditorPanel::Metrics()
{
    // The widest hex digit sets the byte width, not "F": proportional digits A-D are wider than
    // F and 0-9, and a column sized from F clips the edit box on exactly those values.
    float digitW = 0.0f;
    for (const char* d = "0123456789ABCDEF"; *d; ++d)
        digitW = std::max(digitW, ImGui::CalcTextSize(d, d + 1).x);
    GridMetrics m;
    m.editPadX = kEditPadX;
    m.editW = digitW * 2.0f + kEditPadX * 2.0f + kCaretRoom;
    m.byteW = m.editW + kByteGap;
    // One row height for the clipper, the scroll-to-address maths and the edit box: text plus
    // the table's own vertical padding. The edit box must not make its row taller than this.
    m.rowH = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
    return m;
}

void HexEditorPanel::GoTo(uint32_t address)
{
    // Fold the cache/through mirrors first: the grid draws and edits the canonical address, so a
    // selection left un-folded matches no cell while typing would still write the folded byte.
    address &= 0x07FFFFFFu;
    mSelectTab = RegionForAddr(address);
    mScrollTab = mSelectTab;   // the scroll must wait until this region tab is actually active
    mScrollPending = true;
    mScrollAddr = address;
    mSelStart = mSelEnd = (int64_t)address;
    std::snprintf(mAddrBuf, sizeof(mAddrBuf), "%08X", address);
    mFocusRequested = true;
}

bool HexEditorPanel::TakeSearchRequest(std::vector<uint8_t>& outBytes, std::string& outLabel)
{
    if (!mSearchRequested) return false;
    mSearchRequested = false;
    outBytes = std::move(mSearchBytes);
    outLabel = std::move(mSearchLabel);
    return true;
}

bool HexEditorPanel::TakeBreakpointRequest(BreakpointRequest& out)
{
    if (!mBpRequested) return false;
    mBpRequested = false;
    out = mBpRequest;
    return true;
}

bool HexEditorPanel::TakeLocateRequest(LocateRequest& out)
{
    if (!mLocateRequested) return false;
    mLocateRequested = false;
    out = mLocateRequest;
    return true;
}

const char* HexEditorPanel::RegionName(uint32_t addr)
{
    const int i = RegionForAddr(addr);
    // 0 is the "All" tab, which here means the address fell outside every captured region.
    return (i > 0) ? Regions()[(size_t)i].name : "?";
}

bool HexEditorPanel::CopySelection(IMemoryBackend& backend, int64_t lo, int64_t hi)
{
    if (lo < 0 || hi < lo) return false;
    const uint32_t len = (uint32_t)(hi - lo + 1);
    std::vector<uint8_t> bytes;
    if (!ReadRegionBytes(backend, (uint32_t)lo, len, bytes)) return false;

    std::string text;
    text.reserve(bytes.size() * 3);
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        char hex[4];
        std::snprintf(hex, sizeof(hex), "%02X", bytes[i]);
        if (i) text.push_back(' ');
        text += hex;
    }
    ImGui::SetClipboardText(text.c_str());
    return true;
}

void HexEditorPanel::FindSelectionInRam(IMemoryBackend& backend, int64_t lo, int64_t hi)
{
    mFindHits.clear();
    mFindUnreadBase.clear();
    mFindMore = 0;
    mFindError.clear();
    mFindOrigin = (uint32_t)lo;
    mFindLength = (uint32_t)(hi - lo + 1);
    mFindOpen = true;   // raised even when nothing matches: that is the answer

    std::vector<uint8_t> pattern;
    if (!ReadRegionBytes(backend, (uint32_t)lo, mFindLength, pattern))
    {
        // The selection itself could not be read -- e.g. an address reached through Go that
        // lands outside every captured region. There is no pattern, so nothing was searched,
        // and saying "no matches" here would answer a question that was never asked.
        char err[128];
        std::snprintf(err, sizeof(err),
                      "Could not read the selected bytes at 0x%08X, so nothing was searched.",
                      mFindOrigin);
        mFindError = err;
        return;
    }

    // The RAM regions only. The register files are in the tab strip because they are worth
    // looking at, but they are not memory a byte sequence meaningfully "appears in", and
    // including them would put noise at the top of the list.
    std::vector<SearchRegion> regions;
    size_t count = 0;
    const SaturnRegion* all = SaturnRegions(count);
    for (size_t i = 0; i < count; ++i)
    {
        if (std::strstr(all[i].name, "Regs")) continue;
        regions.push_back({ all[i].base, all[i].size });
    }

    // One over the cap, so dropping the selection itself below still leaves a full list.
    const BytePatternSearchResult res =
        FindBytePattern(backend, regions, pattern, kMaxFindHits + 1);
    for (const SearchRegion& r : res.unread) mFindUnreadBase.push_back(r.base);
    // Drop the selection itself: the question is where ELSE these bytes are. An overlapping
    // match a byte or two away is a different occurrence and stays.
    bool selfCounted = false;
    for (uint32_t addr : res.addresses)
    {
        if (addr == mFindOrigin) selfCounted = true;
        else mFindHits.push_back(addr);
    }
    // The selection may also sit past the listed matches, among the ones only counted. It
    // matches wherever it was read from, so it is among them if its region was searched.
    if (!selfCounted && res.truncated)
    {
        for (const SearchRegion& r : regions)
        {
            const bool unread = std::find_if(res.unread.begin(), res.unread.end(),
                [&](const SearchRegion& u) { return u.base == r.base; }) != res.unread.end();
            if (!unread && mFindOrigin >= r.base &&
                uint64_t(mFindOrigin) + mFindLength <= uint64_t(r.base) + r.size)
                selfCounted = true;
        }
    }
    const std::size_t others = res.total - (selfCounted ? 1 : 0);
    if (mFindHits.size() > kMaxFindHits) mFindHits.resize(kMaxFindHits);
    mFindMore = others - mFindHits.size();
}

void HexEditorPanel::CancelEdit()
{
    mEditAddr = -1;
    mEditFlow = false;
    mEditFocus = false;
    mEditBuf[0] = mEditBuf[1] = '\0';
    mSelecting = false;
    mPrevByte.clear();
    mChangeAge.clear();
}

void HexEditorPanel::Draw(IMemoryBackend& backend, bool live, float dt)
{
    (void)live;
    if (mFocusRequested) { ImGui::SetNextWindowFocus(); mFocusRequested = false; }
    if (!ImGui::Begin("Memory"))
    {
        ImGui::End();
        return;
    }

    // --- Toolbar: Address + Go, Encoding, Auto Refresh, Highlight Changes (no Size:
    //     the whole region scrolls). ---
    ImGui::TextUnformatted("Address:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    bool go = ImGui::InputText("##addr", mAddrBuf, sizeof(mAddrBuf),
                               ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    go |= ImGui::Button("Go");
    if (go)
    {
        unsigned a = 0;
        if (std::sscanf(mAddrBuf, "%x", &a) == 1) GoTo(a);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Encoding:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    const char* kEnc[] = { "ASCII", "Shift-JIS" };
    ImGui::Combo("##enc", &mEncoding, kEnc, 2);
    ImGui::SameLine();
    ImGui::Checkbox("Auto Refresh", &mAutoRefresh);
    ImGui::SameLine();
    ImGui::Checkbox("Highlight Changes", &mHighlightChanges);

    // An edit begun against one source must not carry on against its replacement: the digit
    // typed so far (or the open box) belongs to an address in the old data, and the byte cache
    // would paint the old values as "changes" in the new.
    const uint64_t sourceId = backend.Connected() ? backend.SourceId() : 0;
    if (sourceId != mSourceId)
    {
        CancelEdit();
        mSourceId = sourceId;
        mWriteError.clear();
        mModifiedFlash = 0.0f;
    }

    mConnected = backend.Connected();
    if (!mConnected)
    {
        ImGui::Separator();
        ImGui::TextDisabled("(no source loaded)");
        ImGui::End();
        return;
    }

    // --- Region tabs (first is "All"). ---
    const std::vector<Region>& regs = Regions();
    if (ImGui::BeginTabBar("regions", ImGuiTabBarFlags_FittingPolicyScroll))
    {
        for (int i = 0; i < (int)regs.size(); ++i)
        {
            ImGuiTabItemFlags f = (mSelectTab == i) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(regs[i].name, nullptr, f))
            {
                if (mTab != i) { mPrevByte.clear(); mChangeAge.clear(); }
                mTab = i;
                ImGui::EndTabItem();
            }
        }
        mSelectTab = -1;
        ImGui::EndTabBar();
    }
    const Region& reg = regs[mTab];

    // Fade change highlights.
    for (auto it = mChangeAge.begin(); it != mChangeAge.end(); )
    {
        it->second -= dt;
        if (it->second <= 0.0f) it = mChangeAge.erase(it); else ++it;
    }
    if (mModifiedFlash > 0.0f) mModifiedFlash = std::max(0.0f, mModifiedFlash - dt);
    if (mWriteErrorAge > 0.0f && (mWriteErrorAge -= dt) <= 0.0f) mWriteError.clear();
    // Keep the change history bounded when scrolling across large regions.
    if (mPrevByte.size() > 200000) mPrevByte.clear();

    const int64_t selLo = (mSelStart >= 0 && mSelEnd >= 0) ? SelLo(mSelStart, mSelEnd) : -1;
    const int64_t selHi = (mSelStart >= 0 && mSelEnd >= 0) ? SelHi(mSelStart, mSelEnd) : -1;

    // Ctrl+C copies the selection. Gated on this window having focus so it does not steal the
    // shortcut app-wide, and skipped while a byte is being typed over -- the edit box owns
    // Ctrl+C then, for the text in it.
    if (selLo >= 0 && mEditAddr < 0 &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C))
    {
        CopySelection(backend, selLo, selHi);
    }

    // Say why an edit did not happen. Silence here is what let a refused byte look written: the
    // caret moved on and the next digits landed one byte further along.
    auto refuse = [&](uint32_t addr)
    {
        mWriteError = backend.WriteRefusal(addr);
        if (mWriteError.empty()) mWriteError = "The edit was not accepted.";
        mWriteErrorAge = 8.0f;
        mModifiedFlash = 0.0f;
    };
    // Commit one byte through the backend and record it for the change highlight. False means it was
    // refused: the caller leaves the caret where it is, so the user sees the byte they tried to edit.
    auto writeByte = [&](uint32_t addr, unsigned value) -> bool
    {
        const uint8_t byte = (uint8_t)(value & 0xFF);
        if (backend.WriteMemory(addr, &byte, 1) != 1) { refuse(addr); return false; }
        mPrevByte[addr] = byte;
        mChangeAge[addr] = 1.0f;
        mModifiedFlash = 1.5f;
        mModifiedDest = backend.WriteDestination(addr);
        mWriteError.clear();
        return true;
    };
    auto hexVal = [](unsigned c) -> unsigned
    {
        return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
    };
    auto isHex = [](unsigned c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                                         (c >= 'A' && c <= 'F'); };
    const uint32_t regionEnd = reg.base + reg.size;

    // Type-to-edit: with a single writable byte selected and the Memory window focused (and no
    // text field capturing input), hex digits are entered straight into the byte, like a
    // standard hex editor -- the first digit is held pending, the second completes the byte and
    // moves to the next. This deliberately does not go through an InputText: seeding one with
    // the typed digit raced its own select-all-on-focus and re-read the queued character, which
    // dropped a digit and repeated the previous byte.
    if (mEditFlow && !(selLo >= 0 && selLo == selHi && selLo == mEditAddr))
    {
        mEditFlow = false;   // the selection moved off the byte being typed: drop the pending digit
        mEditAddr = -1;
    }
    if (selLo >= 0 && selLo == selHi && (mEditFlow || mEditAddr < 0) &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive())
    {
        int64_t sel = selLo;
        if (mEditFlow)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Backspace))
            {
                mEditFlow = false; mEditAddr = -1;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
            {
                const bool wrote = writeByte((uint32_t)mEditAddr, hexVal((unsigned char)mEditBuf[0]));   // lone digit
                mEditFlow = false; mEditAddr = -1;
                if (wrote && sel + 1 < (int64_t)regionEnd) mSelStart = mSelEnd = sel + 1;
            }
        }
        for (ImWchar ch : ImGui::GetIO().InputQueueCharacters)
        {
            if (!isHex(ch)) continue;
            if (!mEditFlow)
            {
                if (!backend.CanWrite((uint32_t)sel)) { refuse((uint32_t)sel); break; }
                mEditAddr = sel; mEditFlow = true;
                mEditBuf[0] = (char)ch; mEditBuf[1] = '\0';
            }
            else
            {
                const bool wrote = writeByte((uint32_t)mEditAddr, (hexVal((unsigned char)mEditBuf[0]) << 4) | hexVal(ch));
                mEditFlow = false; mEditAddr = -1;
                // Refused: stay on this byte and drop the digits still queued, which were meant for
                // the byte after it and would otherwise start over on this one.
                if (!wrote) break;
                // Nowhere to advance to: stop here rather than let the rest of the queue
                // start over on (and overwrite) this same last byte.
                if (sel + 1 >= (int64_t)regionEnd) break;
                sel += 1; mSelStart = mSelEnd = sel;
            }
        }
    }
    // Click / double-click / drag on a byte cell. Shared by the plain and the pending-digit
    // renderings, so a double-click on a cell holding a typed digit still opens the editor.
    auto cellMouse = [&](uint32_t addr, uint8_t v, bool cellHovered)
    {
        if (cellHovered)
        {
            if (backend.CanWrite(addr) && ImGui::IsMouseDoubleClicked(0))
            {
                mEditAddr = (int64_t)addr; mEditFocus = true;
                mEditFlow = false;   // seed the current value; AutoSelectAll replaces it
                std::snprintf(mEditBuf, sizeof(mEditBuf), "%02X", v);
            }
            else if (ImGui::IsMouseClicked(0))
            {
                if (mEditFlow) { mEditFlow = false; mEditAddr = -1; }   // abandon a pending digit
                mWriteError.clear();   // about the byte they were on
                if (ImGui::GetIO().KeyShift && mSelStart >= 0)
                {
                    // Extend the range from the existing anchor (mSelStart) to the clicked
                    // byte; selLo/selHi take the min/max, so clicking before the anchor
                    // shrinks the tail back to the clicked byte.
                    mSelEnd = (int64_t)addr; mSelecting = false;
                }
                else
                {
                    mSelStart = mSelEnd = (int64_t)addr; mSelecting = true;
                }
            }
        }
        if (mSelecting && ImGui::IsMouseDown(0) && cellHovered) mSelEnd = (int64_t)addr;
    };

    bool editRendered = false;   // did the active edit box get drawn? (else it's scrolled off)

    // --- Grid: a frozen-header table, virtually scrolled over the whole region. ---
    const float ch = ImGui::CalcTextSize("F").x;
    const GridMetrics gm = Metrics();
    const float editW = gm.editW, byteW = gm.byteW, rowH = gm.rowH;
    const float cellPadY = ImGui::GetStyle().CellPadding.y;
    const float lineH = ImGui::GetTextLineHeight();
    const uint32_t totalRows = (reg.size + 15u) / 16u;

    ImGuiTableFlags tflags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
                             ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit;
    const ImVec2 outer(0.0f, -ImGui::GetFrameHeightWithSpacing());
    // Read the visible rows fresh when Auto Refresh is on (or the cache is empty); when
    // off, freeze the view by rendering the last-seen bytes from mPrevByte.
    const bool doRead = mAutoRefresh || mPrevByte.empty();
    // Tight horizontal cell padding so the hex grid packs closely.
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(kCellPadX, cellPadY));
    if (ImGui::BeginTable("mem", 18, tflags, outer))
    {
        ImGui::TableSetupScrollFreeze(1, 1);   // freeze the address column + header row
        // Cells are hit-tested geometrically, so also require the grid itself to be the hovered
        // window: a window stacked over this one must not receive the click underneath it.
        const bool gridHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, ch * 8.0f + 4.0f);
        for (int c = 0; c < 16; ++c)
        {
            char h[3]; std::snprintf(h, sizeof(h), "%02X", c);
            ImGui::TableSetupColumn(h, ImGuiTableColumnFlags_WidthFixed, byteW);
        }
        ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthFixed, ch * 16.0f + 4.0f);
        ImGui::TableHeadersRow();

        // Scroll a pending target address into view — but only once the region tab GoTo asked
        // for (mScrollTab) is actually active. GoTo requests the switch via SetSelected, which
        // doesn't reach mTab until the next frame; scrolling before then would run against the
        // OLD tab. Gating on the *target* tab (not merely "the current tab contains the folded
        // address") matters because the default "All" tab — and any region that also spans the
        // address — would otherwise match and consume the scroll a frame early, leaving the
        // real target tab scrolled to the top. mScrollTab always resolves to a tab that
        // contains the address ("All" as the fallback), so this can't stay pending forever.
        if (mScrollPending && mTab == mScrollTab)
        {
            const uint32_t a = mScrollAddr & 0x07FFFFFFu;
            if (a >= reg.base && a < reg.base + reg.size)
            {
                const uint32_t row = (a - reg.base) / 16u;
                ImGui::SetScrollY((float)row * rowH);
            }
            mScrollPending = false;
        }

        ImGuiListClipper clip;
        clip.Begin((int)totalRows, rowH);
        while (clip.Step())
        {
            // Batch-read the visible rows in one call (one 16-byte request per row so an
            // unmapped gap in "All" only blanks that row, not the whole span).
            const int first = clip.DisplayStart, last = clip.DisplayEnd;
            std::vector<MemoryReadResult> res;
            if (doRead)
            {
                std::vector<MemoryReadRequest> reqs;
                reqs.reserve((size_t)std::max(0, last - first));
                for (int r = first; r < last; ++r)
                    reqs.push_back({ reg.base + (uint32_t)r * 16u, 16 });
                res = backend.ReadMemoryBatch(reqs);
            }

            for (int r = first; r < last; ++r)
            {
                const uint32_t rowAddr = reg.base + (uint32_t)r * 16u;
                // Resolve this row's 16 bytes: fresh from the read, or the frozen cache.
                uint8_t rowBytes[16] = {};
                bool rowMapped[16] = {};
                for (int c = 0; c < 16; ++c)
                {
                    const uint32_t addr = rowAddr + (uint32_t)c;
                    if (doRead)
                    {
                        const MemoryReadResult& rr = res[(size_t)(r - first)];
                        if (rr.success && rr.bytes.size() == 16)
                        {
                            const uint8_t v = rr.bytes[(size_t)c];
                            auto pit = mPrevByte.find(addr);
                            if (mHighlightChanges && pit != mPrevByte.end() && pit->second != v)
                                mChangeAge[addr] = 1.0f;
                            mPrevByte[addr] = v;
                            rowBytes[c] = v; rowMapped[c] = true;
                        }
                    }
                    else
                    {
                        auto pit = mPrevByte.find(addr);
                        if (pit != mPrevByte.end()) { rowBytes[c] = pit->second; rowMapped[c] = true; }
                    }
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushStyleColor(ImGuiCol_Text, kColAddr);
                ImGui::Text("%08X", rowAddr);
                ImGui::PopStyleColor();

                for (int c = 0; c < 16; ++c)
                {
                    ImGui::TableSetColumnIndex(1 + c);
                    const uint32_t addr = rowAddr + (uint32_t)c;
                    const uint8_t v = rowBytes[c];

                    const bool selected = selLo >= 0 && (int64_t)addr >= selLo && (int64_t)addr <= selHi;
                    if (selected)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, kColSelBg);

                    if (!rowMapped[c])
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, kColUnmapped);
                        ImGui::TextUnformatted("--");
                        ImGui::PopStyleColor();
                        continue;
                    }

                    // Geometric hit-test of this fixed-width cell. Bytes are drawn as plain
                    // text (below), NOT as a per-byte Selectable: an interactive widget per
                    // cell would emit hundreds of ImGui IDs (the "conflicting ID" surface)
                    // and, worse, become the active/nav item on click — which makes a
                    // ScrollX/ScrollY table with a frozen column scroll-jitter ("vibrate")
                    // while dragging. A pure rect test needs none of that.
                    const ImVec2 cur = ImGui::GetCursorScreenPos();
                    const bool cellHovered = gridHovered && ImGui::IsMouseHoveringRect(
                        ImVec2(cur.x - kCellPadX, cur.y - cellPadY),
                        ImVec2(cur.x + editW + kCellPadX, cur.y + lineH + cellPadY));
                    if (cellHovered && !selected)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, kColHoverBg);

                    if (mEditAddr == (int64_t)addr)
                    {
                        editRendered = true;
                        if (mEditFlow)
                        {
                            // First digit typed, second pending: shown in the cell itself.
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, kColEditBg);
                            char pending[3] = { (char)std::toupper((unsigned char)mEditBuf[0]), '_', '\0' };
                            ImGui::PushStyleColor(ImGuiCol_Text, kColChanged);
                            ImGui::TextUnformatted(pending);
                            ImGui::PopStyleColor();
                            cellMouse(addr, v, cellHovered);
                            continue;
                        }
                        // Double-click edit. A distinct ID per address, so moving the edit from
                        // one byte to another starts a fresh InputText state instead of inheriting
                        // the last byte's buffer; the zero vertical padding keeps the row at rowH.
                        ImGui::PushID((int)addr);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kEditPadX, 0.0f));
                        ImGui::SetNextItemWidth(editW);
                        if (mEditFocus) { ImGui::SetKeyboardFocusHere(); mEditFocus = false; }
                        const bool enter = ImGui::InputText("##edit", mEditBuf, sizeof(mEditBuf),
                                                            ImGuiInputTextFlags_CharsHexadecimal |
                                                            ImGuiInputTextFlags_EnterReturnsTrue |
                                                            ImGuiInputTextFlags_AutoSelectAll);
                        const bool deactivated = ImGui::IsItemDeactivated();
                        ImGui::PopStyleVar();
                        ImGui::PopID();
                        if (enter || deactivated)
                        {
                            unsigned val = 0;
                            bool wrote = true;   // nothing typed is not a refusal
                            if (mEditBuf[0] && std::sscanf(mEditBuf, "%x", &val) == 1)
                                wrote = writeByte(addr, val);
                            mEditAddr = -1;
                            // Enter moves to the next byte so you can keep going down the row; a
                            // click-away just stops. A refused byte keeps the caret.
                            const int64_t next = (int64_t)addr + 1;
                            if (enter && wrote && next < (int64_t)regionEnd) mSelStart = mSelEnd = next;
                        }
                        continue;
                    }

                    const ImU32 col = (mChangeAge.count(addr)) ? kColChanged
                                     : (v == 0 ? kColZero : kColByte);
                    char b[3]; std::snprintf(b, sizeof(b), "%02X", v);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::TextUnformatted(b);
                    ImGui::PopStyleColor();

                    cellMouse(addr, v, cellHovered);
                }

                // Text pane (ASCII, or Shift-JIS: double-byte kanji/kana + half katakana).
                ImGui::TableSetColumnIndex(17);
                int c = 0;
                while (c < 16)
                {
                    const uint8_t v = rowBytes[c];
                    const bool mp = rowMapped[c];
                    if (c) ImGui::SameLine(0.0f, 0.0f);
                    if (mp && mEncoding == 1 && c < 15 && rowMapped[c + 1] &&
                        SjisIsLead(v) && SjisIsTrail(rowBytes[c + 1]))
                    {
                        const uint32_t cp = SjisDecode(v, rowBytes[c + 1]);
                        char u[5]; if (cp) Utf8Encode(cp, u); else { u[0] = u[1] = '.'; u[2] = '\0'; }
                        ImGui::PushStyleColor(ImGuiCol_Text, kColJp);
                        ImGui::TextUnformatted(u);
                        ImGui::PopStyleColor();
                        c += 2;
                        continue;
                    }
                    if (mp && mEncoding == 1 && SjisIsHalfKana(v))
                    {
                        char u[5]; Utf8Encode(SjisHalfKana(v), u);
                        ImGui::PushStyleColor(ImGuiCol_Text, kColJp);
                        ImGui::TextUnformatted(u);
                        ImGui::PopStyleColor();
                        c += 1;
                        continue;
                    }
                    const char disp = (mp && v >= 0x20 && v <= 0x7E) ? (char)v : '.';
                    ImGui::PushStyleColor(ImGuiCol_Text, mp ? kColText : kColUnmapped);
                    ImGui::Text("%c", disp);
                    ImGui::PopStyleColor();
                    c += 1;
                }
            }
        }
        if (ImGui::IsMouseReleased(0)) mSelecting = false;

        // Right-click → add a memory breakpoint at the selection, or search the current
        // byte selection in the game data directory.
        if (ImGui::BeginPopupContextWindow("hexctx", ImGuiPopupFlags_MouseButtonRight))
        {
            // Add breakpoint: read/write/either at the first selected byte, for a
            // byte/short/long access (the Saturn has 8/16/32-bit memory operations).
            if (selLo >= 0)
            {
                if (ImGui::BeginMenu("Add breakpoint"))
                {
                    ImGui::TextDisabled("At 0x%08X", (uint32_t)selLo);
                    ImGui::Separator();
                    static const struct { const char* label; int kind; } kKinds[] = {
                        { "Break on Read",         1 },
                        { "Break on Write",        2 },
                        { "Break on Read or Write", 3 },
                    };
                    static const struct { const char* label; uint32_t size; } kSizes[] = {
                        { "Byte (8-bit)",   1 },
                        { "Short (16-bit)", 2 },
                        { "Long (32-bit)",  4 },
                    };
                    for (const auto& k : kKinds)
                    {
                        if (ImGui::BeginMenu(k.label))
                        {
                            for (const auto& s : kSizes)
                            {
                                if (ImGui::MenuItem(s.label))
                                {
                                    mBpRequest.address = (uint32_t)selLo;
                                    mBpRequest.size = s.size;
                                    mBpRequest.kind = k.kind;
                                    mBpRequested = true;
                                }
                            }
                            ImGui::EndMenu();
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
            }

            const int64_t cnt = (selLo >= 0) ? (selHi - selLo + 1) : 0;
            if (cnt > 0 && cnt <= 0x1000)
            {
                {
                    char copyItem[64];
                    std::snprintf(copyItem, sizeof(copyItem), "Copy %lld byte%s",
                                  (long long)cnt, cnt == 1 ? "" : "s");
                    if (ImGui::MenuItem(copyItem, "Ctrl+C")) CopySelection(backend, selLo, selHi);

                    char findItem[80];
                    std::snprintf(findItem, sizeof(findItem), "Find these %lld byte%s in RAM...",
                                  (long long)cnt, cnt == 1 ? "" : "s");
                    if (ImGui::MenuItem(findItem)) FindSelectionInRam(backend, selLo, selHi);
                    ImGui::Separator();
                }
                char item[80];
                std::snprintf(item, sizeof(item), "Find %lld selected byte%s in data directory",
                              (long long)cnt, cnt == 1 ? "" : "s");
                if (ImGui::MenuItem(item))
                {
                    auto res = backend.ReadMemoryBatch({ { (uint32_t)selLo, (uint32_t)cnt } })[0];
                    if (res.success)
                    {
                        mSearchBytes = std::move(res.bytes);
                        char lbl[80];
                        std::snprintf(lbl, sizeof(lbl), "Hex bytes @0x%08X (%lld byte%s)",
                                      (uint32_t)selLo, (long long)cnt, cnt == 1 ? "" : "s");
                        mSearchLabel = lbl;
                        mSearchRequested = true;
                    }
                }
#ifdef SE_ENABLE_LIVE
                // Patch feature: locate the selection in the game files (with surrounding
                // context) and record the mapping for later "Apply changes to disc".
                if (ImGui::MenuItem("Find in game files (for patching)..."))
                {
                    mLocateRequest.address = (uint32_t)selLo;
                    mLocateRequest.length  = (uint32_t)cnt;
                    mLocateRequested = true;
                }
#endif
            }
            else
            {
                ImGui::TextDisabled("Select bytes first (click, then drag to extend).");
            }
            ImGui::EndPopup();
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();   // CellPadding (pushed before BeginTable, balanced regardless of open)

    // If an edit was started on a byte that's scrolled out of view (e.g. flow-typing advanced
    // past the visible rows), bring it into view so its edit box renders next frame.
    if (mEditAddr >= 0 && !editRendered)
    {
        mScrollAddr = (uint32_t)mEditAddr;
        mScrollPending = true;
    }

    // --- Selection / value readout ---
    ImGui::Separator();
    if (selLo >= 0)
    {
        const int64_t count = selHi - selLo + 1;
        ImGui::Text("Selection: 0x%08X - 0x%08X (%lld byte%s)",
                    (uint32_t)selLo, (uint32_t)selHi, (long long)count, count == 1 ? "" : "s");
        // Value (hex, and U16/U32 big-endian) from the first bytes of the selection.
        auto res = backend.ReadMemoryBatch({ { (uint32_t)selLo, (uint32_t)std::min<int64_t>(count, 8) } })[0];
        if (res.success && !res.bytes.empty())
        {
            char hex[64] = {}; int hp = 0;
            for (size_t i = 0; i < res.bytes.size() && hp < 56; ++i)
                hp += std::snprintf(hex + hp, sizeof(hex) - hp, "%02X ", res.bytes[i]);
            ImGui::SameLine(); ImGui::Text("| Hex: %s", hex);
            if (res.bytes.size() >= 2)
            {
                const uint16_t u16 = (uint16_t)((res.bytes[0] << 8) | res.bytes[1]);
                ImGui::SameLine(); ImGui::Text("| U16: %04X", u16);
            }
            if (res.bytes.size() >= 4)
            {
                uint32_t u32 = 0;
                for (int i = 0; i < 4; ++i) u32 = (u32 << 8) | res.bytes[(size_t)i];
                ImGui::SameLine(); ImGui::Text("| U32: %08X", u32);
            }
        }
        if (mModifiedFlash > 0.0f)
        {
            ImGui::SameLine();
            // "Queued" is as far as the panel can know for a live source: the emulator applies the poke
            // at its next frame gate and does not confirm it here. A scrubbed frame's edit is held for
            // Play From Here and no emulator has it yet; one the source never sees changes this view
            // only. Each says which.
            using Dest = IMemoryBackend::WriteDest;
            ImGui::TextColored(ImVec4(0.92f, 0.35f, 0.30f, 1.0f),
                               mModifiedDest == Dest::Emulator ? "Queued for emulator"
                             : mModifiedDest == Dest::Staged   ? "Staged for Play From Here"
                                                               : "Modified (this view only)");
        }
        if (!mWriteError.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "Not written: %s", mWriteError.c_str());
        else if (selLo == selHi && !backend.CanWrite((uint32_t)selLo))
        {
            // Read-only here, and why -- the registers on a live emulator, a recorded frame the
            // server cannot resume from -- instead of a grid that silently ignores typing.
            const std::string why = backend.WriteRefusal((uint32_t)selLo);
            if (!why.empty()) ImGui::TextDisabled("Read-only: %s", why.c_str());
        }
    }
    else
    {
        ImGui::TextDisabled("Click a byte to select; drag to extend; double-click a writable byte to edit.");
    }

    DrawFindResultsPopup();

    ImGui::End();
}

// The "find these bytes in RAM" results. Opened from the grid's right-click menu, including
// when there were no matches: the question was asked, so it gets an answer rather than a UI
// that appears to have done nothing.
void HexEditorPanel::DrawFindResultsPopup()
{
    static const char* kTitle = "Find in RAM";
    if (mFindOpen)
    {
        ImGui::OpenPopup(kTitle);
        mFindOpen = false;
    }
    ImGui::SetNextWindowSize(ImVec2(320.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_None)) return;

    ImGui::Text("%u byte%s from 0x%08X", mFindLength, mFindLength == 1 ? "" : "s", mFindOrigin);
    ImGui::Separator();

    if (!mFindError.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.55f, 0.45f, 1.0f));
        ImGui::TextWrapped("%s", mFindError.c_str());
        ImGui::PopStyleColor();
    }
    else if (mFindHits.empty())
    {
        ImGui::TextUnformatted("No matches found");
    }
    else
    {
        const std::size_t others = mFindHits.size() + mFindMore;
        ImGui::Text("%zu other location%s -- double-click to go there:",
                    others, others == 1 ? "" : "s");
        // Leave room for the notes + button below, whatever the list length.
        const float footer = ImGui::GetFrameHeightWithSpacing() * 3.0f;
        if (ImGui::BeginChild("hits", ImVec2(0.0f, -footer), ImGuiChildFlags_None))
        {
            for (uint32_t addr : mFindHits)
            {
                char label[64];
                std::snprintf(label, sizeof(label), "0x%08X  (%s)", addr,
                              RegionName(addr));
                ImGui::Selectable(label);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                {
                    GoTo(addr);
                    ImGui::CloseCurrentPopup();
                }
            }
            if (mFindMore)
                ImGui::TextDisabled("plus %zu more", mFindMore);
        }
        ImGui::EndChild();
    }

    // Would otherwise make the list above look like the whole truth.
    if (!mFindUnreadBase.empty())
        ImGui::TextDisabled("%zu region(s) could not be read and were not searched.",
                            mFindUnreadBase.size());

    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

}  // namespace sfe
