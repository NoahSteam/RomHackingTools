// The Memory Compare panel under the app's real theme and font, against hand-built snapshots (no
// emulator, no capture path): region selection, byte selection and hit testing, GoTo, address
// parsing, the context-menu capability table, and that a new comparison starts clean.
#include "MemoryComparePanel.h"

#include <cstdio>
#include <memory>

#include "ImGuiHarness.h"
#include "Theme.h"
#include "imgui.h"
#include "imgui_internal.h"

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

size_t Ix(RegionId id) { return static_cast<size_t>(id); }

std::shared_ptr<MemSnapshot> Blank(uint64_t frame)
{
    auto s = std::make_shared<MemSnapshot>();
    s->origin = { 1, 1, frame, false };
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        MemRegionImage img;
        img.id = static_cast<RegionId>(i);
        img.bytes.assign(SnapshotSize(img.id), 0);
        s->regions.push_back(std::move(img));
    }
    return s;
}

// A table drawn in the last frame with this many columns. ImGui keeps tables in a pool after they
// stop being submitted, so "not drawn" has to be told from "still in the pool".
ImGuiTable* FindTable(int columns)
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (int i = 0; i < g.Tables.GetMapSize(); ++i)
        if (ImGuiTable* t = g.Tables.TryGetMapData(i))
            if (t->ColumnsCount == columns && t->LastFrameActive == g.FrameCount) return t;
    return nullptr;
}

// A comparison with HWRAM changed at 0x10 (row 1) and 0x4000, and VDP2 CRAM at 0x20.
struct Fixture
{
    MemoryComparePanel panel;
    std::shared_ptr<MemSnapshot> a = Blank(1800), b = Blank(1884);
    DiffResult diff;
    bool hasDiff = true, aAttached = true, bAttached = true;
    float width = 1100.0f;
    ImGuiHarness h;

    Fixture() : h([this] { Ui(); })
    {
        b->regions[Ix(RegionId::Hwram)].bytes[0x10] = 0x5A;
        b->regions[Ix(RegionId::Hwram)].bytes[0x4000] = 0x01;
        b->regions[Ix(RegionId::Vdp2Cram)].bytes[0x20] = 0x77;
        Diff(a, b, DiffOptions(), &diff);
        ApplyTheme(ImGui::GetStyle());
        LoadFonts(ImGui::GetIO());
        h.Settle();
    }

    void NewPair()
    {
        a = Blank(10);
        b = Blank(20);
        b->regions[Ix(RegionId::Hwram)].bytes[0x10] = 1;
        Diff(a, b, DiffOptions(), &diff);
    }

    void Ui()
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, 650), ImGuiCond_Always);
        panel.Draw(hasDiff ? &diff : nullptr, aAttached, bAttached);
    }

    // Centre of summary row 'i' (0 = All Memory, then one per region). Rows are one text line
    // plus the table's cell padding, so the last row's bottom edge anchors the count back.
    ImVec2 SummaryRow(int i) const
    {
        const int rows = 1 + ComparedRegions();
        const ImGuiTable* t = FindTable(4);
        const float rowH = MemoryComparePanel::Metrics().rowH;
        const ImGuiTableColumn& c = t->Columns[0];
        return ImVec2(c.MinX + 40.0f, t->RowPosY2 - (rows - 1 - i) * rowH - rowH * 0.5f);
    }

    static int ComparedRegions()
    {
        int n = 0;
        for (size_t i = 0; i < kRegionCount; ++i) n += InCompare(static_cast<RegionId>(i));
        return n;
    }
    // The summary row of a region: rows are listed in region order, minus the ones left out of comparisons.
    static int SummaryIndex(int region)
    {
        int row = 1;   // 0 is All Memory
        for (int i = 0; i < region; ++i) row += InCompare(static_cast<RegionId>(i));
        return row;
    }

    // Click a region's summary row (or All Memory for -1) and let the view rebuild.
    void ClickRegion(int region)
    {
        h.Click(SummaryRow(region < 0 ? 0 : SummaryIndex(region)));
        h.Settle();
    }

    // Centre of byte 'k' on side A (0) or B (1) in grid line 'line'.
    ImVec2 Byte(int line, int side, int k) const
    {
        const ImGuiTable* t = FindTable(5);
        const MemoryComparePanel::GridMetrics gm = MemoryComparePanel::Metrics();
        const ImGuiTableColumn& c = t->Columns[1 + side];
        return ImVec2(c.MinX + (k + 0.5f) * gm.byteW,
                      t->InnerRect.Min.y + gm.rowH + (line + 0.5f) * gm.rowH - t->InnerWindow->Scroll.y);
    }
};

void TestParseLocation()
{
    RegionRef r;
    Check(MemoryComparePanel::ParseLocation("06034F20", &r) && r.id == RegionId::Hwram && r.offset == 0x34F20u,
          "a bus address resolves");
    Check(MemoryComparePanel::ParseLocation("0x06034F20", &r) && r.offset == 0x34F20u, "0x prefix accepted");
    Check(MemoryComparePanel::ParseLocation("HWRAM+34F20", &r) && r.id == RegionId::Hwram && r.offset == 0x34F20u,
          "region+offset resolves");
    Check(MemoryComparePanel::ParseLocation("vdp1 fb+0x1230", &r) && r.id == RegionId::Vdp1Fb && r.offset == 0x1230u,
          "region names ignore case and spaces");
    Check(MemoryComparePanel::ParseLocation("VDP2 Regs+11F", &r) && r.id == RegionId::Vdp2Regs, "last byte of a register file");
    Check(!MemoryComparePanel::ParseLocation("VDP2 Regs+120", &r), "an offset past the region is refused");
    Check(!MemoryComparePanel::ParseLocation("00000000", &r), "an unmapped address is refused");
    Check(!MemoryComparePanel::ParseLocation("nonsense", &r), "text is refused");
    Check(!MemoryComparePanel::ParseLocation("NOPE+10", &r), "an unknown region is refused");
    Check(!MemoryComparePanel::ParseLocation("", &r), "empty is refused");
    // Sound RAM is left out of comparisons, so there is no row to jump to.
    Check(!MemoryComparePanel::ParseLocation("05A00010", &r), "a Sound RAM address is refused");
    Check(!MemoryComparePanel::ParseLocation("Sound RAM+10", &r), "and so is Sound RAM+offset");

    // Nothing wider than 32 bits may wrap into a valid location (strtoul on a 64-bit long would).
    Check(!MemoryComparePanel::ParseLocation("106034F20", &r), "a 33-bit address is refused, not wrapped to 06034F20");
    Check(!MemoryComparePanel::ParseLocation("100000000", &r), "2^32 is refused, not wrapped to 0");
    Check(!MemoryComparePanel::ParseLocation("FFFFFFFFFFFFFFFF", &r), "64 bits of F is refused");
    Check(!MemoryComparePanel::ParseLocation("0x106034F20", &r), "with a 0x prefix too");
    Check(!MemoryComparePanel::ParseLocation("HWRAM+100000000", &r), "an offset of 2^32 is refused, not wrapped to 0");
    Check(!MemoryComparePanel::ParseLocation("HWRAM+100034F20", &r), "an oversized offset is refused, not wrapped to 34F20");
    Check(!MemoryComparePanel::ParseLocation("HWRAM+FFFFFFFFFFFFFFFFF", &r), "a very long offset is refused");
    Check(!MemoryComparePanel::ParseLocation("-1", &r) && !MemoryComparePanel::ParseLocation("+10", &r), "signs are refused");
    Check(!MemoryComparePanel::ParseLocation("HWRAM+-1", &r), "a negative offset is refused");
    Check(!MemoryComparePanel::ParseLocation("0x", &r) && !MemoryComparePanel::ParseLocation("HWRAM+", &r), "no digits is refused");
    Check(!MemoryComparePanel::ParseLocation("06034F2G", &r), "a non-hex digit is refused");
    Check(MemoryComparePanel::ParseLocation("00000000006034F20", &r) && r.id == RegionId::Hwram && r.offset == 0x34F20u,
          "leading zeros are fine however many there are");
    Check(MemoryComparePanel::ParseLocation("  06034F20  ", &r), "surrounding spaces are fine");
}

// Sound RAM is rewritten constantly by the 68K and the SCSP, so it is not part of a comparison: no row
// in the summary, and All Memory counts and sizes without it.
void TestSoundRamIsNotListed()
{
    Fixture f;
    const ImGuiTable* t = FindTable(4);
    Check(t != nullptr, "the summary is drawn");
    if (!t) return;
    // One row per compared region plus All Memory, by the height of the table's rows.
    const float rowH = MemoryComparePanel::Metrics().rowH;
    const int rows = static_cast<int>((t->RowPosY2 - t->InnerRect.Min.y) / rowH + 0.5f) - 1;   // minus the header
    Check(rows == 1 + Fixture::ComparedRegions(), "the summary lists All Memory and the compared regions only");
    Check(Fixture::ComparedRegions() == static_cast<int>(kRegionCount) - 1, "exactly one region is left out");
    Check(!InCompare(RegionId::SoundRam), "and it is Sound RAM");
}

void TestActionTable()
{
    using A = MemoryComparePanel::Action;
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        const RegionId id = static_cast<RegionId>(i);
        Check(MemoryComparePanel::ActionEnabled(A::GoToMemory, id), "Go to Memory is offered everywhere");
        Check(MemoryComparePanel::ActionEnabled(A::AddWatch, id), "Add to Watch is offered everywhere");
    }
    Check(MemoryComparePanel::ActionEnabled(A::BreakOnWrite, RegionId::Hwram), "HWRAM can break on write");
    Check(MemoryComparePanel::ActionEnabled(A::BreakOnWrite, RegionId::Vdp2Ram), "VDP2 RAM can break on write");
    Check(!MemoryComparePanel::ActionEnabled(A::BreakOnWrite, RegionId::Vdp1Fb), "VDP1 FB cannot (not a bus write)");
    Check(MemoryComparePanel::ActionEnabled(A::ViewInAssembly, RegionId::Lwram), "LWRAM can show SH-2 code");
    Check(!MemoryComparePanel::ActionEnabled(A::ViewInAssembly, RegionId::Vdp1Ram), "VDP1 RAM is not SH-2 code");
}

void TestStates()
{
    Fixture f;
    // No comparison yet: the window shows a hint and no tables.
    f.hasDiff = false;
    f.h.Settle();
    Check(FindTable(4) == nullptr && FindTable(5) == nullptr, "no comparison, no tables");
    f.hasDiff = true;
    f.h.Settle();
    Check(FindTable(4) != nullptr && FindTable(5) != nullptr, "a comparison shows the summary and the diff grid");
    Check(f.panel.SelectedRegion() == -1, "it opens on All Memory");
    Check(f.panel.ChangesOnly(), "Changes Only starts on");
    Check(f.panel.LineCount() > 0, "there are lines to show");
    Check(!f.panel.HasSelection(), "nothing is selected");
    MemoryComparePanel::Request req;
    Check(!f.panel.TakeRequest(req), "no request is pending");
}

void TestRegionSelection()
{
    Fixture f;
    const size_t allLines = f.panel.LineCount();
    f.ClickRegion(static_cast<int>(RegionId::Hwram));
    Check(f.panel.SelectedRegion() == static_cast<int>(RegionId::Hwram), "clicking HWRAM selects it");
    Check(f.panel.LineCount() > 0 && f.panel.LineCount() != allLines, "its lines replace the All Memory sections");
    f.ClickRegion(-1);
    Check(f.panel.SelectedRegion() == -1 && f.panel.LineCount() == allLines, "clicking All Memory restores the sections");
}

void TestByteSelection()
{
    Fixture f;
    f.ClickRegion(static_cast<int>(RegionId::Hwram));   // HWRAM alone: line 0 is its first kept row
    // Changes Only with 2 context rows around row 1 keeps rows 0..3, so line 1 is the changed row.
    f.h.Click(f.Byte(1, 1, 0));
    f.h.Settle();
    Check(f.panel.HasSelection(), "clicking a byte selects it");
    Check(f.panel.SelectionStart() == (RegionRef{ RegionId::Hwram, 0x10 }), "on the byte that was clicked");
    Check(f.panel.SelectionLength() == 1, "one byte");

    // The same offset is selected whichever side is clicked.
    f.h.Click(f.Byte(1, 0, 3));
    f.h.Settle();
    Check(f.panel.SelectionStart() == (RegionRef{ RegionId::Hwram, 0x13 }), "side A maps to the same offsets");

    // A right-click opens the context menu and selects the byte under it.
    f.h.RightClick(f.Byte(1, 1, 0));
    f.h.Settle();
    Check(f.panel.SelectionStart().offset == 0x10, "right-click selects the byte under the pointer");
    ImGuiContext& g = *ImGui::GetCurrentContext();
    Check(g.OpenPopupStack.Size == 1, "and opens the context menu");
}

void TestGoTo()
{
    Fixture f;
    f.ClickRegion(static_cast<int>(RegionId::Hwram));
    // A byte whose row is shown: Changes Only stays on.
    f.panel.GoTo({ RegionId::Hwram, 0x11 });
    f.h.Settle();
    Check(f.panel.ChangesOnly(), "a visible row does not change the mode");
    Check(f.panel.HasSelection() && f.panel.SelectionStart() == (RegionRef{ RegionId::Hwram, 0x11 }), "GoTo selects the byte");

    // A byte whose row Changes Only elides: the mode switches off so the row exists.
    const size_t before = f.panel.LineCount();
    f.panel.GoTo({ RegionId::Hwram, 0x8000 });
    f.h.Settle();
    Check(!f.panel.ChangesOnly(), "an elided row turns Changes Only off");
    Check(f.panel.LineCount() > before, "and lists the whole region");

    // GoTo into another region while one region is selected follows it there.
    f.panel.GoTo({ RegionId::Vdp2Cram, 0x20 });
    f.h.Settle();
    Check(f.panel.SelectedRegion() == static_cast<int>(RegionId::Vdp2Cram), "GoTo switches to the byte's region");
}

void TestNewComparisonStartsClean()
{
    Fixture f;
    f.ClickRegion(static_cast<int>(RegionId::Hwram));
    f.h.Click(f.Byte(1, 1, 0));
    f.h.Settle();
    Check(f.panel.HasSelection(), "a byte is selected");
    f.NewPair();
    f.h.Settle();
    Check(!f.panel.HasSelection(), "a new pair of snapshots clears the selection");
    Check(f.panel.SelectedRegion() == -1, "and goes back to All Memory");
}

void TestPendingJumpDoesNotCrossComparisons()
{
    Fixture f;
    f.ClickRegion(static_cast<int>(RegionId::Hwram));
    // A jump to a row Changes Only hides, then the pair changes before the panel draws again: the
    // jump belonged to the old bytes and must not flip the mode on the new ones.
    f.panel.GoTo({ RegionId::Hwram, 0x8000 });
    f.NewPair();
    f.h.Settle();
    Check(f.panel.ChangesOnly(), "a jump waiting when the pair changes is dropped");
    Check(!f.panel.HasSelection(), "and so is the selection it made");
}

// A control placed past the window's right edge cannot be reached: the toolbar does not scroll, and
// the grid's horizontal scrollbar belongs to the grid. Content wider than the window is the symptom.
void TestToolbarWrapsInNarrowWindows()
{
    Fixture f;
    for (float w : { 1100.0f, 900.0f, 600.0f, 420.0f, 320.0f })
    {
        f.width = w;
        f.h.Settle();
        ImGuiWindow* win = ImGui::FindWindowByName("Memory Compare");
        char msg[96];
        std::snprintf(msg, sizeof(msg), "every control fits inside a %.0f px window", w);
        Check(win && win->ContentSize.x <= win->InnerRect.GetWidth() + 1.0f, msg);
    }
}

void TestNoDifferences()
{
    Fixture f;
    f.a = Blank(5);
    f.b = Blank(6);
    Diff(f.a, f.b, DiffOptions(), &f.diff);
    f.h.Settle();
    Check(FindTable(5) == nullptr, "identical snapshots show no diff grid");
}
}  // namespace

int main()
{
    TestParseLocation();
    TestActionTable();
    TestSoundRamIsNotListed();
    TestStates();
    TestRegionSelection();
    TestByteSelection();
    TestGoTo();
    TestNewComparisonStartsClean();
    TestPendingJumpDoesNotCrossComparisons();
    TestToolbarWrapsInNarrowWindows();
    TestNoDifferences();
    if (gFail == 0) std::printf("MemoryComparePanelTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
