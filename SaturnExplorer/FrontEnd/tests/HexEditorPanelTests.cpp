// Memory panel tests -- the real HexEditorPanel, driven headlessly with the app's own theme
// and font against a mock memory backend (see ImGuiHarness.h).
//
// Each case pins a bug that ordinary unit tests and screenshots both miss: typed digits
// writing the wrong byte, an edit box too narrow for its own padding, row heights that
// disagree with the scroll maths, and clicks reaching the grid through a window stacked on it.

#define IMGUI_DEFINE_MATH_OPERATORS
#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

#include "imgui_internal.h"   // ImGuiTable column/row geometry
#include "DrawnText.h"
#include "ImGuiHarness.h"
#include "HexEditorPanel.h"
#include "Theme.h"

using namespace sfe;

namespace
{
int gFailures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
    ++gFailures;
}
}  // namespace

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

namespace
{
constexpr uint32_t kBase = 0x06000000u;
constexpr uint32_t kSize = 0x100000u;

// High work RAM only; everything else is unmapped, like a source that captured one region.
class MockBackend : public IMemoryBackend
{
public:
    std::vector<uint8_t> ram = std::vector<uint8_t>(kSize, 0);

    uint64_t id = 1;
    int64_t  refuseOffset = -1;   // a byte the source takes and then rejects (a full queue, a lost connection)
    bool Connected() const override { return true; }
    uint64_t SourceId() const override { return id; }
    bool CanWrite(uint32_t a) const override { return InRange(a, 1); }
    std::string WriteRefusal(uint32_t) const override { return "the source refused it"; }
    WriteDest WriteDestination(uint32_t) const override { return dest; }

    // A recorded frame on screen, as App says it each frame (see ContextBackend::SetRecordedFrameEdits).
    WriteDest dest = WriteDest::Emulator;
    bool      recorded = false;
    size_t    editedBytes = 0;
    uint64_t  frame = 0;
    int       reverts = 0;
    int       writes = 0;
    FrameEditSummary RecordedFrameEdits() const override { return FrameEditSummary{ recorded, editedBytes, frame }; }
    bool RevertRecordedFrameEdits() override { ++reverts; const bool had = editedBytes != 0; editedBytes = 0; return had; }

    std::vector<MemoryReadResult> ReadMemoryBatch(const std::vector<MemoryReadRequest>& rs) override
    {
        std::vector<MemoryReadResult> out;
        for (const MemoryReadRequest& r : rs)
        {
            MemoryReadResult res;
            if (InRange(r.address, r.size))
            {
                res.success = true;
                const uint32_t off = (r.address & 0x07FFFFFFu) - kBase;
                res.bytes.assign(ram.begin() + off, ram.begin() + off + r.size);
            }
            out.push_back(std::move(res));
        }
        return out;
    }

    size_t WriteMemory(uint32_t a, const uint8_t* bytes, size_t n) override
    {
        if (!InRange(a, (uint32_t)n)) return 0;
        ++writes;
        if (refuseOffset >= 0 && (int64_t)((a & 0x07FFFFFFu) - kBase) == refuseOffset) return 0;
        for (size_t i = 0; i < n; ++i) ram[(a & 0x07FFFFFFu) - kBase + i] = bytes[i];
        return n;
    }

private:
    static bool InRange(uint32_t a, uint32_t n)
    {
        const uint32_t c = a & 0x07FFFFFFu;
        return c >= kBase && c - kBase + n <= kSize;
    }
};

ImGuiTable* FindGrid()
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (int i = 0; i < g.Tables.GetMapSize(); ++i)
        if (ImGuiTable* t = g.Tables.TryGetMapData(i))
            if (t->ColumnsCount == 18) return t;
    return nullptr;
}

// The Memory window, over the first row of HWRAM, with the real theme and font.
struct Fixture
{
    MockBackend   backend;
    HexEditorPanel panel;
    bool          cover = false;    // draw a window stacked over the cell under 'coverAt'
    ImVec2        coverAt;
    ImGuiHarness  h;

    Fixture() : h([this] { Ui(); })
    {
        ApplyTheme(ImGui::GetStyle());
        LoadFonts(ImGui::GetIO());
        panel.GoTo(kBase);
        h.Settle();
    }

    void Ui()
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(900, 500), ImGuiCond_Always);
        panel.Draw(backend, false, 1.0f / 60.0f);
        if (cover)
        {
            ImGui::SetNextWindowPos(coverAt - ImVec2(40, 40), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(120, 120), ImGuiCond_Always);
            ImGui::Begin("Cover", nullptr, ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextUnformatted("on top");
            ImGui::End();
        }
    }

    // Centre of the byte at 'column' in absolute grid row 'row' (row 0 is the first row of the
    // region, directly under the header while the view is unscrolled).
    ImVec2 Cell(int row, int column) const
    {
        const ImGuiTable* t = FindGrid();
        const float rowH = HexEditorPanel::Metrics().rowH;
        const ImGuiTableColumn& c = t->Columns[1 + column];
        return ImVec2((c.MinX + c.MaxX) * 0.5f,
                      t->InnerRect.Min.y + rowH + (row + 0.5f) * rowH - t->InnerWindow->Scroll.y);
    }

    void Type(const char* chars, bool oneFramePerChar)
    {
        for (const char* c = chars; *c; ++c)
        {
            ImGui::GetIO().AddInputCharacter((unsigned)*c);
            if (oneFramePerChar) h.Frame(ImVec2(1270, 710), false);
        }
        if (!oneFramePerChar) h.Frame(ImVec2(1270, 710), false);
        h.Frame(ImVec2(1270, 710), false);
    }

    // Double-click a byte and let the edit box take focus (it needs a couple of frames to go active).
    void DoubleClick(ImVec2 p)
    {
        h.Click(p);
        h.Click(p);
        h.Settle();
    }

    void Key(ImGuiKey key)
    {
        ImGui::GetIO().AddKeyEvent(key, true);
        h.Frame(ImVec2(1270, 710), false);
        ImGui::GetIO().AddKeyEvent(key, false);
        h.Frame(ImVec2(1270, 710), false);
    }
};

void TypingWritesTheDigitsTyped(bool oneFramePerChar)
{
    Fixture f;
    f.h.Click(f.Cell(0, 0));
    CHECK(f.panel.SelectionStart() == (int64_t)kBase);

    f.Type("ABCD", oneFramePerChar);
    CHECK(f.backend.ram[0] == 0xAB);
    CHECK(f.backend.ram[1] == 0xCD);
    CHECK(f.backend.ram[2] == 0x00);
    CHECK(f.panel.SelectionStart() == (int64_t)kBase + 2);   // advanced past the two bytes
}

void DoubleClickEditsEachByteSeparately()
{
    Fixture f;
    f.h.Click(f.Cell(0, 0));
    f.DoubleClick(f.Cell(0, 0));
    CHECK(f.panel.IsEditing());
    f.Type("12", true);
    f.Key(ImGuiKey_Enter);
    CHECK(f.backend.ram[0] == 0x12);
    CHECK(!f.panel.IsEditing());

    f.DoubleClick(f.Cell(0, 5));
    CHECK(f.panel.IsEditing());
    f.Type("34", true);
    f.Key(ImGuiKey_Enter);
    CHECK(f.backend.ram[5] == 0x34);
    CHECK(f.backend.ram[0] == 0x12);   // the second edit did not rewrite the first byte
    CHECK(f.backend.ram[1] == 0x00);
    CHECK(f.backend.ram[6] == 0x00);
}

void TypingStopsAtTheRegionEnd()
{
    Fixture f;
    const uint32_t last = kBase + kSize - 1;
    f.panel.GoTo(last);
    f.h.Settle();
    f.h.Click(f.Cell((int)((last - kBase) / 16), 15));
    CHECK(f.panel.SelectionStart() == (int64_t)last);
    f.Type("ABCD", false);
    CHECK(f.backend.ram[kSize - 1] == 0xAB);   // not overwritten by the CD that had nowhere to go
    CHECK(f.backend.ram[kSize - 2] == 0x00);
}

// A byte the source refuses must not look written: the caret stays on it, the digits queued for
// the next byte are dropped instead of landing one byte along, and the panel says why.
void RefusedByteKeepsTheCaretAndSaysWhy(bool oneFramePerChar)
{
    Fixture f;
    f.backend.refuseOffset = 0;
    f.h.Click(f.Cell(0, 0));
    f.Type("ABCD", oneFramePerChar);
    CHECK(f.backend.ram[0] == 0x00);
    CHECK(f.backend.ram[1] == 0x00);                          // CD did not slide onto the next byte
    CHECK(f.panel.SelectionStart() == (int64_t)kBase);        // the caret did not move past it
    CHECK(!f.panel.IsEditing());
    CHECK(f.panel.WriteError() == "the source refused it");

    f.backend.refuseOffset = -1;                              // the source recovers: the same byte takes it
    f.Type("EF", false);
    CHECK(f.backend.ram[0] == 0xEF);
    CHECK(f.panel.WriteError().empty());
    CHECK(f.panel.SelectionStart() == (int64_t)kBase + 1);
}

void RefusedEditBoxKeepsTheCaret()
{
    Fixture f;
    f.backend.refuseOffset = 0;
    f.h.Click(f.Cell(0, 0));
    f.DoubleClick(f.Cell(0, 0));
    f.Type("12", true);
    f.Key(ImGuiKey_Enter);
    CHECK(f.backend.ram[0] == 0x00);
    CHECK(f.panel.SelectionStart() == (int64_t)kBase);        // Enter does not walk on past a refused byte
    CHECK(!f.panel.WriteError().empty());
}

// The grid draws and edits the canonical address, so a cache-through address (0x26000000) must be
// folded when it is navigated to, or the selection matches no cell while typing still writes the
// folded byte.
void GoToFoldsMirrorAddresses()
{
    Fixture f;
    f.panel.GoTo(0x26000000u + 0x10);
    f.h.Settle();
    CHECK(f.panel.SelectionStart() == (int64_t)kBase + 0x10);
    f.Type("5A", false);
    CHECK(f.backend.ram[0x10] == 0x5A);
    CHECK(f.panel.SelectionStart() == (int64_t)kBase + 0x11);
}

void DoubleClickOnAPendingDigitOpensTheEditor()
{
    Fixture f;
    f.h.Click(f.Cell(0, 0));
    f.Type("A", true);
    f.DoubleClick(f.Cell(0, 0));
    CHECK(f.panel.IsEditing());
    f.Type("BC", true);
    f.Key(ImGuiKey_Enter);
    CHECK(f.backend.ram[0] == 0xBC);   // replaced, not "AB" then "0C" across two bytes
    CHECK(f.backend.ram[1] == 0x00);
}

void EscapeAbandonsAPendingDigit()
{
    Fixture f;
    f.h.Click(f.Cell(0, 0));
    f.Type("A", true);
    f.Key(ImGuiKey_Escape);
    f.Type("B", true);
    f.Type("C", true);
    CHECK(f.backend.ram[0] == 0xBC);
}

// A digit typed against one source must not complete a byte in its replacement.
void ReplacingTheSourceAbandonsAPendingDigit()
{
    Fixture f;
    f.h.Click(f.Cell(0, 0));
    f.Type("A", true);
    f.backend.id = 2;                 // the data behind the panel was swapped
    f.h.Settle();
    f.Type("B", true);
    CHECK(f.backend.ram[0] == 0x00);  // not "AB": the A belonged to the old source
    f.Type("C", true);
    CHECK(f.backend.ram[0] == 0xBC);  // typing carries on normally in the new one
}

// Same for an open edit box.
void ReplacingTheSourceClosesTheEditor()
{
    Fixture f;
    f.h.Click(f.Cell(0, 0));
    f.DoubleClick(f.Cell(0, 0));
    CHECK(f.panel.IsEditing());
    f.backend.id = 2;
    f.h.Settle();
    CHECK(!f.panel.IsEditing());
}

void GridGeometryMatchesTheRules()
{
    Fixture f;
    const HexEditorPanel::GridMetrics m = HexEditorPanel::Metrics();
    const ImGuiTable* t = FindGrid();
    CHECK(t != nullptr);
    if (!t) return;

    // The widest pair of hex digits, plus the edit box's own padding, fits inside the box ...
    float widest = 0.0f;
    for (const char* a = "0123456789ABCDEF"; *a; ++a)
        for (const char* b = "0123456789ABCDEF"; *b; ++b)
        {
            const char pair[3] = { *a, *b, '\0' };
            widest = std::max(widest, ImGui::CalcTextSize(pair).x);
        }
    CHECK(widest + 2.0f * m.editPadX <= m.editW);

    // ... and the box fits inside the byte column's content area, which is what it is placed in.
    for (int c = 1; c <= 16; ++c)
    {
        const ImGuiTableColumn& col = t->Columns[c];
        CHECK(m.editW <= (col.WorkMaxX - col.WorkMinX) + 0.01f);
    }
}

// Every row must be rowH tall, with or without an edit box in it: the clipper and the
// scroll-to-address maths assume one height, so a row that disagrees makes a click land on the
// wrong byte further down the grid.
void RowsAreOneHeight(bool editing)
{
    Fixture f;
    if (editing)
    {
        f.h.Click(f.Cell(0, 0));
        f.DoubleClick(f.Cell(0, 0));
        CHECK(f.panel.IsEditing());
    }
    // Aim near the top edge of the row, so even a few pixels of drift above it picks row 11.
    f.h.Click(f.Cell(12, 3) - ImVec2(0.0f, HexEditorPanel::Metrics().rowH * 0.4f));
    CHECK(f.panel.SelectionStart() == (int64_t)(kBase + 12 * 16 + 3));
}

void JumpLandsOnTheTargetRow()
{
    Fixture f;
    const float rowH = HexEditorPanel::Metrics().rowH;
    f.panel.GoTo(kBase + 0x1000);   // row 256
    f.h.Settle();
    const ImGuiTable* t = FindGrid();
    CHECK(std::fabs(t->InnerWindow->Scroll.y - 256.0f * rowH) < 0.5f);
    // The target row sits at the top of the view: a click on its fourth byte selects that byte.
    f.h.Click(f.Cell(256, 3));
    CHECK(f.panel.SelectionStart() == (int64_t)(kBase + 0x1003));
}

void CoveredCellsIgnoreClicks()
{
    Fixture f;
    const int64_t before = f.panel.SelectionStart();   // GoTo selected the first byte
    f.cover = true;
    f.coverAt = f.Cell(0, 5);
    f.h.Settle();
    f.h.Click(f.coverAt);
    CHECK(f.panel.SelectionStart() == before);          // the click went to the window on top
    f.DoubleClick(f.coverAt);
    CHECK(!f.panel.IsEditing());

    f.cover = false;
    f.h.Settle();
    f.h.Click(f.Cell(0, 5));
    CHECK(f.panel.SelectionStart() == (int64_t)kBase + 5);   // and it still works uncovered
}

// ---- Edits on a recorded frame: the status line, Revert, the notice, a no-change commit -------------------------

std::string MemoryWindowText()
{
    std::string all;
    for (const drawn::Run& r : drawn::WindowRuns("Memory")) all += drawn::Squash(r.text);
    return all;
}
bool Says(const std::string& text) { return MemoryWindowText().find(drawn::Squash(text)) != std::string::npos; }

// Where the Revert button is, found by hovering: an item's rectangle is not exposed, its id is.
bool FindRevert(Fixture& f, ImVec2* at)
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImGuiID id = ImGui::FindWindowByName("Memory")->GetID("Revert##frameedits");
    for (float y = 20.0f; y < 90.0f; y += 3.0f)
        for (float x = 700.0f; x < 900.0f; x += 4.0f)
        {
            f.h.Hover(ImVec2(x, y));
            if (g.HoveredId == id) { *at = ImVec2(x, y); return true; }
        }
    return false;
}

float GridTop() { return FindGrid()->OuterRect.Min.y; }

void RecordedFrameLineAndRevert()
{
    Fixture f;
    f.backend.recorded = true;
    f.backend.frame = 42;
    f.backend.dest = IMemoryBackend::WriteDest::Staged;
    f.h.Settle();
    const float topNone = GridTop();   // the line is reserved even with nothing edited
    ImVec2 at;
    CHECK(!Says("edited byte"));
    CHECK(!FindRevert(f, &at));        // no edits: nothing to revert

    f.backend.editedBytes = 1;
    f.h.Settle();
    CHECK(Says("This frame has 1 edited byte. Play From Here applies it."));
    CHECK(GridTop() == topNone);       // the first edit does not move the grid
    f.backend.editedBytes = 3;
    f.h.Settle();
    CHECK(Says("This frame has 3 edited bytes. Play From Here applies them."));
    CHECK(GridTop() == topNone);

    // The hover on the line says why, not what.
    {
        bool saw = false;
        for (float y = 20.0f; y < 70.0f && !saw; y += 3.0f)
        {
            f.h.Hover(ImVec2(40.0f, y));
            const std::string tip = drawn::Squash(drawn::TooltipText());
            saw = tip.find(drawn::Squash("Edits stay on this frame however you scrub. Only Play From Here on this frame sends "
                                         "them to the game. They are discarded when this frame leaves the recording.")) != std::string::npos;
        }
        CHECK(saw);
    }

    // The Revert control receives clicks and asks the BACKEND (the panel never reaches the application).
    CHECK(FindRevert(f, &at));
    f.h.Click(at);
    CHECK(f.backend.reverts == 1);
    f.h.Settle();
    CHECK(f.backend.editedBytes == 0 && !Says("edited byte"));
    CHECK(GridTop() == topNone);       // and taking the edits away does not move the grid either
    CHECK(!FindRevert(f, &at));
}

void NoticeNamesTheRecordedFrame()
{
    {
    Fixture f;
    f.backend.recorded = true;
    f.backend.frame = 42;
    f.backend.dest = IMemoryBackend::WriteDest::Staged;
    f.h.Settle();
    f.h.Click(f.Cell(0, 0));
    f.DoubleClick(f.Cell(0, 0));
    f.Type("12", true);
    f.Key(ImGuiKey_Enter);
    CHECK(f.backend.ram[0] == 0x12);
    CHECK(Says("Kept on recorded frame #42"));
    f.backend.frame = 43;              // scrubbed on: the notice still names the frame the edit went to
    f.h.Frame(ImVec2(1270, 710), false);
    CHECK(Says("Kept on recorded frame #42") && !Says("#43"));
    }
    // The other destination keeps its words (one harness at a time).
    Fixture g;
    g.h.Click(g.Cell(0, 0));
    g.DoubleClick(g.Cell(0, 0));
    g.Type("34", true);
    g.Key(ImGuiKey_Enter);
    CHECK(Says("Queued for emulator"));
}

void CommittingAByteUnchangedWritesNothing()
{
    Fixture f;
    f.h.Click(f.Cell(0, 2));
    f.DoubleClick(f.Cell(0, 2));
    CHECK(f.panel.IsEditing());
    f.Key(ImGuiKey_Enter);             // nothing typed: the box still holds the byte it opened on
    CHECK(!f.panel.IsEditing());
    CHECK(f.backend.writes == 0);      // no poke to a live emulator...
    CHECK(!Says("Queued for emulator"));   // ...and no notice claiming one

    f.DoubleClick(f.Cell(0, 5));       // another byte: a double-click on the same one so soon would count as a third click
    CHECK(f.panel.IsEditing());
    f.Type("00", true);                // typing the same value out is the same no-change commit
    f.Key(ImGuiKey_Enter);
    CHECK(f.backend.writes == 0);

    f.DoubleClick(f.Cell(0, 8));
    CHECK(f.panel.IsEditing());
    f.Type("01", true);
    f.Key(ImGuiKey_Enter);             // a real change still writes
    CHECK(f.backend.writes == 1 && f.backend.ram[8] == 0x01);
}
}  // namespace

int main()
{
    TypingWritesTheDigitsTyped(true);
    TypingWritesTheDigitsTyped(false);
    DoubleClickEditsEachByteSeparately();
    EscapeAbandonsAPendingDigit();
    TypingStopsAtTheRegionEnd();
    RefusedByteKeepsTheCaretAndSaysWhy(true);
    RefusedByteKeepsTheCaretAndSaysWhy(false);
    RefusedEditBoxKeepsTheCaret();
    GoToFoldsMirrorAddresses();
    DoubleClickOnAPendingDigitOpensTheEditor();
    ReplacingTheSourceAbandonsAPendingDigit();
    ReplacingTheSourceClosesTheEditor();
    GridGeometryMatchesTheRules();
    RowsAreOneHeight(false);
    RowsAreOneHeight(true);
    JumpLandsOnTheTargetRow();
    CoveredCellsIgnoreClicks();
    RecordedFrameLineAndRevert();
    NoticeNamesTheRecordedFrame();
    CommittingAByteUnchangedWritesNothing();
    if (gFailures)
    {
        std::cerr << gFailures << " check(s) failed\n";
        return 1;
    }
    std::cout << "HexEditorPanelTests passed\n";
    return 0;
}
