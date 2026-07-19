#include "HexEditorPanel.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "imgui.h"

namespace sfe
{

namespace
{
const ImU32 kColAddr    = IM_COL32(150, 150, 160, 255);
const ImU32 kColByte    = IM_COL32(210, 210, 215, 255);
const ImU32 kColZero    = IM_COL32(110, 110, 120, 255);   // dim 00 bytes
const ImU32 kColChanged = IM_COL32(235, 170, 70, 255);    // amber (Highlight Changes)
const ImU32 kColText    = IM_COL32(180, 195, 170, 255);
const ImU32 kColJp      = IM_COL32(130, 190, 210, 255);   // double-byte (Shift-JIS) marker
const ImU32 kColSelBg   = IM_COL32(70, 110, 90, 150);     // selection tint

int SelLo(int a, int b) { return a < b ? a : b; }
int SelHi(int a, int b) { return a > b ? a : b; }

// True if 'b' can start a Shift-JIS double-byte sequence.
bool SjisLead(uint8_t b) { return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xEF); }
bool SjisTrail(uint8_t b) { return (b >= 0x40 && b <= 0x7E) || (b >= 0x80 && b <= 0xFC); }
}  // namespace

void HexEditorPanel::GoTo(uint32_t address)
{
    mBase = address & ~0xFu;   // align to the row so the byte is visible
    std::snprintf(mAddrBuf, sizeof(mAddrBuf), "%08X", mBase);
    mHavePrev = false;         // don't flash "changed" just from jumping
    mSelStart = mSelEnd = (int)(address - mBase);
    mFocusRequested = true;
}

void HexEditorPanel::Refresh(IMemoryBackend& backend)
{
    mConnected = backend.Connected();
    if (!mConnected) { mBytes.clear(); return; }

    auto res = backend.ReadMemoryBatch({ { mBase, (uint32_t)mSize } })[0];
    std::vector<uint8_t> fresh = res.success ? res.bytes : std::vector<uint8_t>((size_t)mSize, 0);
    fresh.resize((size_t)mSize, 0);

    // Change detection: bump each differing byte's highlight timer.
    if (mChangeAge.size() != fresh.size()) { mChangeAge.assign(fresh.size(), 0.0f); mHavePrev = false; }
    if (mHighlightChanges && mHavePrev && mPrev.size() == fresh.size())
        for (size_t i = 0; i < fresh.size(); ++i)
            if (fresh[i] != mPrev[i]) mChangeAge[i] = 1.0f;   // seconds

    mPrev = fresh;
    mHavePrev = true;
    mBytes = std::move(fresh);
}

void HexEditorPanel::Draw(IMemoryBackend& backend, bool live, float dt)
{
    if (mFocusRequested) { ImGui::SetNextWindowFocus(); mFocusRequested = false; }
    if (!ImGui::Begin("Hex Editor"))
    {
        ImGui::End();
        return;
    }

    // --- Toolbar: Address + Go, Size, Encoding, Auto Refresh, Highlight Changes ---
    ImGui::TextUnformatted("Address:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputText("##addr", mAddrBuf, sizeof(mAddrBuf),
                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsHexadecimal))
    {
        unsigned a = 0;
        if (std::sscanf(mAddrBuf, "%x", &a) == 1) { mBase = a & ~0xFu; mHavePrev = false; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Go"))
    {
        unsigned a = 0;
        if (std::sscanf(mAddrBuf, "%x", &a) == 1) { mBase = a & ~0xFu; mHavePrev = false; }
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Size:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    const char* kSizes[] = { "0x40", "0x80", "0x100", "0x200", "0x400", "0x1000" };
    const int   kSizeVals[] = { 0x40, 0x80, 0x100, 0x200, 0x400, 0x1000 };
    int sizeIdx = 2;
    for (int i = 0; i < 6; ++i) if (kSizeVals[i] == mSize) sizeIdx = i;
    if (ImGui::Combo("##size", &sizeIdx, kSizes, 6)) { mSize = kSizeVals[sizeIdx]; mHavePrev = false; }
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

    // --- Read the window (auto-refresh, or once when frozen) ---
    if (mAutoRefresh || !mHavePrev) { Refresh(backend); }
    for (float& a : mChangeAge) if (a > 0.0f) a = std::max(0.0f, a - dt);

    if (!mConnected)
    {
        ImGui::Separator();
        ImGui::TextDisabled("(no source loaded)");
        ImGui::End();
        return;
    }

    // --- Grid ---
    ImGui::Separator();
    const float ch = ImGui::CalcTextSize("F").x;
    const float byteW = ch * 2.0f + 6.0f;      // "FF" + padding
    const ImVec2 pad = ImGui::GetStyle().ItemSpacing;

    ImGui::BeginChild("grid", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    // Column header row: address gutter + 00..0F.
    ImGui::PushStyleColor(ImGuiCol_Text, kColAddr);
    ImGui::Text("%-8s", "Addr");
    for (int c = 0; c < 16; ++c)
    {
        ImGui::SameLine(0.0f, (c == 0) ? ch : pad.x);
        ImGui::Text("%02X", c);
    }
    ImGui::PopStyleColor();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int rows = mSize / 16;
    const int selLo = (mSelStart >= 0 && mSelEnd >= 0) ? SelLo(mSelStart, mSelEnd) : -1;
    const int selHi = (mSelStart >= 0 && mSelEnd >= 0) ? SelHi(mSelStart, mSelEnd) : -1;

    for (int r = 0; r < rows; ++r)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kColAddr);
        ImGui::Text("%08X", mBase + (uint32_t)r * 16);
        ImGui::PopStyleColor();

        // Hex cells.
        for (int c = 0; c < 16; ++c)
        {
            const int idx = r * 16 + c;
            const uint8_t v = mBytes[(size_t)idx];
            ImGui::SameLine(0.0f, (c == 0) ? ch : pad.x);

            ImVec2 cur = ImGui::GetCursorScreenPos();
            const bool selected = idx >= selLo && idx <= selHi;
            if (selected)
                dl->AddRectFilled(cur, ImVec2(cur.x + byteW, cur.y + ImGui::GetTextLineHeight()), kColSelBg);

            ImU32 col = (mChangeAge[(size_t)idx] > 0.0f) ? kColChanged
                       : (v == 0 ? kColZero : kColByte);
            char b[3]; std::snprintf(b, sizeof(b), "%02X", v);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::Selectable(b, false, ImGuiSelectableFlags_AllowOverlap, ImVec2(byteW, 0));
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
            {
                if (ImGui::IsMouseClicked(0)) { mSelStart = mSelEnd = idx; mSelecting = true; }
                else if (mSelecting && ImGui::IsMouseDown(0)) { mSelEnd = idx; }
            }
        }

        // Text pane.
        ImGui::SameLine(0.0f, ch);
        for (int c = 0; c < 16; ++c)
        {
            const int idx = r * 16 + c;
            const uint8_t v = mBytes[(size_t)idx];
            // Shift-JIS: mark a double-byte cluster (glyphs need a JP font; the
            // marker at least reveals where JP text lives).
            bool jp = false;
            if (mEncoding == 1 && c < 15 && SjisLead(v) && SjisTrail(mBytes[(size_t)idx + 1]))
                jp = true;
            const char disp = (v >= 0x20 && v <= 0x7E) ? (char)v : '.';
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, jp ? kColJp : kColText);
            ImGui::Text("%c", disp);
            ImGui::PopStyleColor();
        }
    }
    if (ImGui::IsMouseReleased(0)) mSelecting = false;
    ImGui::EndChild();

    // --- Selection / value readout ---
    ImGui::Separator();
    if (selLo >= 0)
    {
        const uint32_t a0 = mBase + (uint32_t)selLo;
        const uint32_t a1 = mBase + (uint32_t)selHi;
        const int count = selHi - selLo + 1;
        ImGui::Text("Selection: 0x%08X - 0x%08X (%d byte%s)", a0, a1, count, count == 1 ? "" : "s");
        ImGui::SameLine();
        // Value (hex), and U16/U32 big-endian from the first bytes of the selection.
        char hex[64] = {}; int hp = 0;
        for (int i = selLo; i <= selHi && hp < 56; ++i)
            hp += std::snprintf(hex + hp, sizeof(hex) - hp, "%02X ", mBytes[(size_t)i]);
        ImGui::Text("| Hex: %s", hex);
        if (count >= 2)
        {
            const uint16_t u16 = (uint16_t)((mBytes[(size_t)selLo] << 8) | mBytes[(size_t)selLo + 1]);
            ImGui::SameLine(); ImGui::Text("| U16: %04X", u16);
        }
        if (count >= 4)
        {
            uint32_t u32 = 0;
            for (int i = 0; i < 4; ++i) u32 = (u32 << 8) | mBytes[(size_t)selLo + i];
            ImGui::SameLine(); ImGui::Text("| U32: %08X", u32);
        }
    }
    else
    {
        ImGui::TextDisabled("Click a byte to select; drag to extend.");
    }
    (void)live;

    ImGui::End();
}

}  // namespace sfe
