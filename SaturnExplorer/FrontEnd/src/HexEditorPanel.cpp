#include "HexEditorPanel.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "imgui.h"

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

int64_t SelLo(int64_t a, int64_t b) { return a < b ? a : b; }
int64_t SelHi(int64_t a, int64_t b) { return a > b ? a : b; }
}  // namespace

const std::vector<HexEditorPanel::Region>& HexEditorPanel::Regions()
{
    // First entry is "All" (the whole 27-bit canonical CPU space); the rest are the
    // regions the snapshot captures, addressed at their CPU-visible (cached-mirror) bases.
    // BIOS and the cartridge (CS0-2) are not captured, so they have no tab. Sound RAM is
    // the SCSP's 512 KiB (0x05A00000 cached mirror of 0x25A00000) when the source supplies
    // it (live v13+); it is empty otherwise.
    static const std::vector<Region> kRegions = {
        { "All",       0x00000000u, 0x08000000u },
        { "LWRAM",     0x00200000u, kWramSize     },
        { "HWRAM",     0x06000000u, kWramSize     },
        { "Sound RAM", 0x05A00000u, kSoundRamSize },
        { "VDP1 RAM",  0x05C00000u, kVdp1VramSize },
        { "VDP1 FB",   0x05C80000u, kVdp1FbSize   },
        { "VDP1 Regs", 0x05D00000u, kVdp1RegBytes },
        { "VDP2 RAM",  0x05E00000u, kVdp2VramSize },
        { "VDP2 CRAM", 0x05F00000u, kCramSize     },
        { "VDP2 Regs", 0x05F80000u, kVdp2RegBytes },
    };
    return kRegions;
}

int HexEditorPanel::RegionForAddr(uint32_t addr)
{
    const std::vector<Region>& regs = Regions();
    const uint32_t a = addr & 0x07FFFFFFu;   // fold cache/through mirrors like the backend
    for (int i = 1; i < (int)regs.size(); ++i)
        if (a >= regs[i].base && a < regs[i].base + regs[i].size)
            return i;
    return 0;   // "All"
}

void HexEditorPanel::GoTo(uint32_t address)
{
    mSelectTab = RegionForAddr(address);
    mScrollPending = true;
    mScrollAddr = address;
    mSelStart = mSelEnd = (int64_t)address;
    std::snprintf(mAddrBuf, sizeof(mAddrBuf), "%08X", address);
    mFocusRequested = true;
}

void HexEditorPanel::Select(uint32_t address, uint32_t length)
{
    if (length == 0) length = 1;
    GoTo(address);
    mSelEnd = (int64_t)address + (int64_t)length - 1;
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
    // Keep the change history bounded when scrolling across large regions.
    if (mPrevByte.size() > 200000) mPrevByte.clear();

    const int64_t selLo = (mSelStart >= 0 && mSelEnd >= 0) ? SelLo(mSelStart, mSelEnd) : -1;
    const int64_t selHi = (mSelStart >= 0 && mSelEnd >= 0) ? SelHi(mSelStart, mSelEnd) : -1;

    // --- Grid: a frozen-header table, virtually scrolled over the whole region. ---
    const float ch = ImGui::CalcTextSize("F").x;
    // Compact byte columns: 2 hex glyphs + a little slack. Combined with the tightened cell
    // padding below this gives ~one-space gap between bytes (like the old Work RAM view),
    // instead of the wide default-padded columns.
    const float byteW = ch * 2.0f + 4.0f;
    const float rowH = ImGui::GetTextLineHeightWithSpacing();
    const uint32_t totalRows = (reg.size + 15u) / 16u;

    ImGuiTableFlags tflags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
                             ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit;
    const ImVec2 outer(0.0f, -ImGui::GetFrameHeightWithSpacing());
    // Read the visible rows fresh when Auto Refresh is on (or the cache is empty); when
    // off, freeze the view by rendering the last-seen bytes from mPrevByte.
    const bool doRead = mAutoRefresh || mPrevByte.empty();
    // Halve the horizontal cell padding so the hex grid packs tightly.
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0f, ImGui::GetStyle().CellPadding.y));
    const bool memTableOpen = ImGui::BeginTable("mem", 18, tflags, outer);
    if (memTableOpen)
    {
        ImGui::TableSetupScrollFreeze(1, 1);   // freeze the address column + header row
        ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, ch * 8.0f + 4.0f);
        for (int c = 0; c < 16; ++c)
        {
            char h[3]; std::snprintf(h, sizeof(h), "%02X", c);
            ImGui::TableSetupColumn(h, ImGuiTableColumnFlags_WidthFixed, byteW);
        }
        ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthFixed, ch * 16.0f + 4.0f);
        ImGui::TableHeadersRow();

        // Scroll a pending target address into view (1-frame latency is fine).
        if (mScrollPending)
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
                    const bool cellHovered = ImGui::IsMouseHoveringRect(
                        cur, ImVec2(cur.x + byteW, cur.y + ImGui::GetTextLineHeight()));
                    if (cellHovered && !selected)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, kColHoverBg);

                    if (mEditAddr == (int64_t)addr)
                    {
                        ImGui::SetNextItemWidth(byteW);
                        if (mEditFocus) { ImGui::SetKeyboardFocusHere(); mEditFocus = false; }
                        const bool enter = ImGui::InputText("##edit", mEditBuf, sizeof(mEditBuf),
                            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue |
                            ImGuiInputTextFlags_AutoSelectAll);
                        if (enter || ImGui::IsItemDeactivated())
                        {
                            unsigned val = 0;
                            if (mEditBuf[0] && std::sscanf(mEditBuf, "%x", &val) == 1)
                            {
                                const uint8_t byte = (uint8_t)(val & 0xFF);
                                if (backend.WriteMemory(addr, &byte, 1) == 1)
                                {
                                    mPrevByte[addr] = byte;
                                    mChangeAge[addr] = 1.0f;
                                    mModifiedFlash = 1.5f;
                                }
                            }
                            mEditAddr = -1;
                        }
                        continue;
                    }

                    const ImU32 col = (mChangeAge.count(addr)) ? kColChanged
                                     : (v == 0 ? kColZero : kColByte);
                    char b[3]; std::snprintf(b, sizeof(b), "%02X", v);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::TextUnformatted(b);
                    ImGui::PopStyleColor();

                    if (cellHovered)
                    {
                        if (backend.CanWrite(addr) && ImGui::IsMouseDoubleClicked(0))
                        {
                            mEditAddr = (int64_t)addr; mEditFocus = true;
                            std::snprintf(mEditBuf, sizeof(mEditBuf), "%02X", v);
                        }
                        else if (ImGui::IsMouseClicked(0))
                        {
                            if (ImGui::GetIO().KeyShift && mSelStart >= 0)
                            {
                                // Extend the range from the existing anchor (mSelStart) to
                                // the clicked byte; selLo/selHi take the min/max, so clicking
                                // before the anchor shrinks the tail back to the clicked byte.
                                mSelEnd = (int64_t)addr; mSelecting = false;
                            }
                            else
                            {
                                mSelStart = mSelEnd = (int64_t)addr; mSelecting = true;
                            }
                        }
                    }
                    if (mSelecting && ImGui::IsMouseDown(0) && cellHovered) mSelEnd = (int64_t)addr;
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
            ImGui::TextColored(ImVec4(0.92f, 0.35f, 0.30f, 1.0f), "Modified");
        }
    }
    else
    {
        ImGui::TextDisabled("Click a byte to select; drag to extend; double-click a writable byte to edit.");
    }

    ImGui::End();
}

}  // namespace sfe
