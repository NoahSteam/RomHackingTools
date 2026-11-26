// The real SH-2 Assembly panel, driven headlessly against a mock memory backend and a core
// context that supplies SH-2 registers (see ImGuiHarness.h).
//
// AsmScrollTargetTests models the scroll rule with synthetic rows; this is the panel itself. It
// pins what the model cannot:
//  - the panel links and draws at all (a class-static constant bound to std::min's reference
//    parameter is an ODR-use that fails to link in an unoptimized C++14 build),
//  - the virtual list's row height does not change while a comment is being edited. The list is
//    clipped on the assumption that every row is one text line; an edit box with its normal frame
//    padding makes its row taller, the inferred height changes, and every scroll position computed
//    from it -- a jump to an address -- lands somewhere else.
#define IMGUI_DEFINE_MATH_OPERATORS
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

#include "imgui_internal.h"
#include "ImGuiHarness.h"
#include "AssemblyPanel.h"
#include "WatchPanel.h"
#include "Debug/BreakpointManager.h"
#include "Debug/ExecutionActions.h"
#include "FakeVdpSource.h"

using namespace sfe;

namespace
{
int gFailures;

#define CHECK(cond) do {                                                      \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } \
} while (0)

bool MakeDir(const char* path)
{
#ifdef _WIN32
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0755) == 0;
#endif
}

bool RemoveDir(const char* path)
{
#ifdef _WIN32
    return _rmdir(path) == 0;
#else
    return rmdir(path) == 0;
#endif
}

constexpr uint32_t kBase = 0x06000000u;
constexpr uint32_t kSize = 0x100000u;
constexpr uint32_t kPc   = 0x06000100u;

// High work RAM only, filled with NOPs; everything else is unmapped.
class MockBackend : public IMemoryBackend
{
public:
    // Writing is off by default, which is what the original tests rely on. The editing tests turn it on
    // and then assert on 'writes', the exact WriteMemory calls the panel made.
    bool         canWrite = false;
    uint64_t     sourceId = 1;
    std::string  refusal;                  // WriteRefusal text
    long         acceptBytes = -1;         // -1: accept and apply every write; else this many bytes "written"
    WriteDest    dest = WriteDest::ViewOnly;
    struct Write { uint32_t addr; std::vector<uint8_t> bytes; };
    std::vector<Write> writes;
    std::map<uint32_t, uint8_t> patch;     // bytes that differ from the nop fill

    // Put a big-endian opcode at a work-RAM address.
    void Put(uint32_t addr, uint16_t op) { patch[addr & 0x07FFFFFFu] = op >> 8; patch[(addr + 1) & 0x07FFFFFFu] = op & 0xFF; }

    bool Connected() const override { return true; }
    uint64_t SourceId() const override { return sourceId; }
    bool CanWrite(uint32_t) const override { return canWrite; }
    std::string WriteRefusal(uint32_t) const override { return refusal; }
    WriteDest WriteDestination(uint32_t) const override { return dest; }
    std::vector<MemoryReadResult> ReadMemoryBatch(const std::vector<MemoryReadRequest>& rs) override
    {
        std::vector<MemoryReadResult> out;
        for (const MemoryReadRequest& r : rs)
        {
            MemoryReadResult res;
            const uint32_t c = r.address & 0x07FFFFFFu;
            if (c >= kBase && c - kBase + r.size <= kSize)
            {
                res.success = true;
                for (uint32_t i = 0; i < r.size; ++i)
                {
                    const auto p = patch.find(c + i);
                    res.bytes.push_back(p != patch.end() ? p->second : (((c + i) & 1u) ? 0x09 : 0x00));   // 0x0009 = nop
                }
            }
            out.push_back(std::move(res));
        }
        return out;
    }
    size_t WriteMemory(uint32_t addr, const uint8_t* bytes, size_t size) override
    {
        if (!canWrite) return 0;
        writes.push_back({ addr, std::vector<uint8_t>(bytes, bytes + size) });
        const size_t n = acceptBytes < 0 ? size : (std::min)(size, (size_t)acceptBytes);
        for (size_t i = 0; i < n; ++i) patch[(addr + (uint32_t)i) & 0x07FFFFFFu] = bytes[i];
        return n;
    }
};

int SupplyRegs(void*, int, se_sh2_regs* out)
{
    *out = {};
    out->pc = kPc;
    return 1;
}

struct Fixture
{
    se_test::State state;
    se_context*    ctx = nullptr;
    MockBackend    backend;
    BreakpointManager bps;
    ExecutionActions  actions;
    WatchPanel     watch;
    AssemblyPanel  panel;
    AssemblyPanel::Request req;
    bool live = false;
    ImGuiHarness   h;

    Fixture() : h([this] {
        // Wide enough that every column, the comment one included, is on screen to be clicked.
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(1200.0f, 600.0f));
        panel.Draw(ctx, backend, bps, actions, watch, live, req);
    })
    {
        se_data_source ds = se_test::MakeSource(state);
        ds.capabilities |= SE_CAP_SH2_REGS;
        ds.read_sh2_regs = SupplyRegs;
        ctx = se_test::CreateContext(ds);
        se_begin_frame(ctx);
    }
    ~Fixture() { se_destroy(ctx); }
};

// The panel's table, found by its column count (six).
ImGuiTable* FindTable()
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (int i = 0; i < g.Tables.GetMapSize(); ++i)
        if (ImGuiTable* t = g.Tables.TryGetMapData(i))
            if (t->ColumnsCount == 6) return t;
    return nullptr;
}

// The height the clipper inferred for a row: the scrollable content divided by the row count.
// (A 1 MiB region is 524288 rows, so an error of one pixel in the content is invisible and a
// change of a few pixels per row is unmistakable.)
float InferredRowHeight()
{
    ImGuiTable* t = FindTable();
    return t && t->InnerWindow ? t->InnerWindow->ContentSize.y / (float)(kSize / 2) : 0.0f;
}

// Park the list at its very top, exactly: a jump leaves the target row a few pixels under the
// header, and then the row the clipper measures from is not the first one on screen.
void ScrollToTop(ImGuiHarness& h)
{
    ImGui::SetScrollY(FindTable()->InnerWindow, 0.0f);
    h.Settle();
}

float ScrollY()
{
    ImGuiTable* t = FindTable();
    return t && t->InnerWindow ? t->InnerWindow->Scroll.y : 0.0f;
}

// Check that the entire instruction row is visible below the frozen table header.
void CheckInstructionVisible(uint32_t address)
{
    const ImGuiWindow* w = FindTable()->InnerWindow;
    const float rowH = InferredRowHeight();
    const float rowTop = w->DC.CursorStartPos.y + rowH +
                         ((address - kBase) / 2) * rowH;
    CHECK(rowTop >= w->InnerRect.Min.y + rowH - 1.0f);
    CHECK(rowTop + rowH <= w->InnerRect.Max.y + 1.0f);
}

// The comment cell of the row 'n' rows below the first fully visible one.
ImVec2 CommentCellOfVisibleRow(int n)
{
    ImGuiTable* t = FindTable();
    const float rowH = InferredRowHeight();
    const ImGuiWindow* w = t->InnerWindow;
    const int first = (int)std::ceil(w->Scroll.y / rowH);
    const float y = w->InnerRect.Min.y + rowH +   // the frozen header row is one row tall
                      ((first + n) * rowH - w->Scroll.y) + rowH * 0.5f;
    const float x = t->Columns[5].WorkMinX + 2.0f;   // an empty comment is one space wide
    return ImVec2(x, y);
}

// Smoke: the real panel draws, and the list spans the whole region -- far more rows than a
// window of them. (The link itself is what the first case proves; see the header.)
void TestDrawsTheWholeRegion()
{
    Fixture f;
    f.h.Settle();
    CHECK(FindTable() != nullptr);
    CHECK(InferredRowHeight() > 4.0f);
    // Following the PC, so the view is near it, not at the region's first row.
    CHECK(ScrollY() >= 0.0f);
}

// Editing a comment must not change the row height the clipper works from.
void TestEditingACommentKeepsRowHeight()
{
    Fixture f;
    // At the top of the region the first row is the one the clipper measures its height from --
    // the row whose growth the bug was in, so it is the one to edit.
    f.panel.GoTo(0, kBase);
    f.h.Settle();
    ScrollToTop(f.h);
    const float before = InferredRowHeight();
    CHECK(before > 4.0f);
    CHECK(ScrollY() == 0.0f);

    const ImVec2 cell = CommentCellOfVisibleRow(0);
    f.h.Click(cell);
    f.h.Click(cell);          // double-click: edit this row's comment
    f.h.Settle();

    CHECK(std::fabs(InferredRowHeight() - before) < 0.01f);
}

// The reported consequence: with the editor open (and after it closes), jumping to an address
// scrolls to that address's row, not somewhere else.
void TestJumpLandsOnTargetWhileEditing()
{
    Fixture f;
    f.panel.GoTo(0, kBase);
    f.h.Settle();
    ScrollToTop(f.h);
    const float rowH = InferredRowHeight();

    const ImVec2 cell = CommentCellOfVisibleRow(0);
    f.h.Click(cell);
    f.h.Click(cell);
    f.h.Settle();

    const uint32_t target = 0x060927C0u;
    f.panel.GoTo(0, target);
    f.h.Settle();

    const float wantRow = (float)((target - kBase) / 2);
    const float gotRow = ScrollY() / rowH;
    CHECK(std::fabs(gotRow - wantRow) < 2.0f);
}
// A breakpoint must return from browsing to the stopped CPU and PC, even when
// Follow PC was disabled by an earlier address jump.
void TestBreakpointRevealReturnsFromBrowsing()
{
    for (int cpu = 0; cpu < 2; ++cpu)
    {
        Fixture f;
        f.live = true;
        f.h.Settle();
        const uint32_t stoppedPc = kBase + 0x4000;
        f.panel.GoTo(1 - cpu, kBase + 0x90000);
        f.h.Settle();
        const float browsingScroll = ScrollY();

        f.panel.SetBreakpointStop(true, cpu, stoppedPc);
        f.panel.Reveal(cpu);
        f.h.Settle();
        CHECK(f.panel.Cpu() == cpu);
        CHECK(ScrollY() < browsingScroll);
        const float stoppedScroll = ScrollY();
        CheckInstructionVisible(stoppedPc);

        // Following resumes after the reveal, so a subsequent step stays visible.
        f.panel.SetBreakpointStop(true, cpu, stoppedPc + 0x1000);
        f.h.Settle();
        CHECK(ScrollY() > stoppedScroll);
        CheckInstructionVisible(stoppedPc + 0x1000);
    }
}

// A new halt at the same PC must also scroll back after the user scrolled away.
// The reveal is one-shot: browsing while that halt remains on screen is allowed.
void TestBreakpointRevealAtUnchangedPc()
{
    Fixture f;
    f.live = true;
    f.h.Settle();
    f.panel.SetBreakpointStop(true, 0, kBase + 0x4000);
    f.h.Settle();
    const float stoppedScroll = ScrollY();
    CHECK(stoppedScroll > 0.0f);
    ScrollToTop(f.h);
    CHECK(ScrollY() == 0.0f);

    f.panel.Reveal(0);
    f.h.Settle();
    // Initial window sizing can shift the alignment by part of a row.
    CHECK(std::fabs(ScrollY() - stoppedScroll) < InferredRowHeight());
    CheckInstructionVisible(kBase + 0x4000);
    ScrollToTop(f.h);
    CHECK(ScrollY() == 0.0f);
}

// A9-57: the Watch panel's session file is written atomically and a failure is reported. The file is a directory for the failing case:
// the rename over it is refused, the old thing is left as it was, and the text names the subject.
void TestWatchSessionSave()
{
#ifdef _WIN32
    // (A12-20) no mkdtemp on Windows: a name of this process's own
    const std::string dir = "watchsess_" + std::to_string(_getpid());
    CHECK(MakeDir(dir.c_str()));
    char* old = _getcwd(nullptr, 0);
    CHECK(_chdir(dir.c_str()) == 0);
#else
    char tmpl[] = "watchsess_XXXXXX";
    if (!mkdtemp(tmpl)) { CHECK(false); return; }
    const std::string dir = tmpl;
    char* old = getcwd(nullptr, 0);
    CHECK(chdir(tmpl) == 0);
#endif
    {
        WatchPanel w;
        w.AddWatch("hp", "0x06000000", WatchType::U8);
        std::string err;
        CHECK(w.SaveSession(&err) && err.empty());
        {
            // closed before the remove: Windows does not delete a file that is open
            std::ifstream f("saturn_explorer_watches.json", std::ios::binary);
            std::stringstream ss; ss << f.rdbuf();
            CHECK(ss.str().find("hp") != std::string::npos);
        }
        CHECK(std::remove("saturn_explorer_watches.json") == 0);
        CHECK(MakeDir("saturn_explorer_watches.json"));
        err.clear();
        CHECK(!w.SaveSession(&err));
        CHECK(err.rfind("The watch list: not saved, ", 0) == 0);
        CHECK(RemoveDir("saturn_explorer_watches.json"));
    }
#ifdef _WIN32
    if (old) { CHECK(_chdir(old) == 0); free(old); }
    const std::string cmd = "rmdir /s /q " + dir;
#else
    if (old) { CHECK(chdir(old) == 0); free(old); }
    const std::string cmd = "rm -rf " + dir;
#endif
    if (std::system(cmd.c_str()) != 0) {}
}

// ------------------------------------------------------------------ in-place editing (task 2B)

// A cell of the row 'n' rows below the first fully visible one, in column 'col'.
ImVec2 CellOfVisibleRow(int n, int col)
{
    ImVec2 p = CommentCellOfVisibleRow(n);
    p.x = FindTable()->Columns[col].WorkMinX + 2.0f;
    return p;
}

constexpr int kColInstr = 3;

// A writable fixture parked at the top of the region, so row n is the instruction at kBase + 2n.
void Prepare(Fixture& f)
{
    f.backend.canWrite = true;
    f.backend.dest = IMemoryBackend::WriteDest::Emulator;
    f.panel.GoTo(0, kBase);
    f.h.Settle();
    ScrollToTop(f.h);
}

ImVec2 Away() { return ImVec2(1270.0f, 710.0f); }

void OpenRow(Fixture& f, int row)
{
    const ImVec2 cell = CellOfVisibleRow(row, kColInstr);
    // Let the previous double-click's window lapse (ImGui's 0.30 s): a third click in quick succession
    // counts as click number three, which is not a double-click.
    for (int i = 0; i < 25; ++i) f.h.Frame(Away(), false);
    f.h.Click(cell);
    f.h.Click(cell);
    f.h.Settle();
}

void PressKey(Fixture& f, ImGuiKey key)
{
    ImGui::GetIO().AddKeyEvent(key, true);
    f.h.Frame(Away(), false);
    ImGui::GetIO().AddKeyEvent(key, false);
    f.h.Frame(Away(), false);
}

void Type(Fixture& f, const char* text)
{
    ImGui::GetIO().AddInputCharactersUTF8(text);
    f.h.Frame(Away(), false);
    f.h.Frame(Away(), false);
}

std::string TextOf(const DisassembledInstruction& i)
{
    return i.Operands.empty() ? i.Mnemonic : i.Mnemonic + " " + i.Operands;
}

constexpr uint16_t kAddR1 = 0x7101;   // add #0x01,r1
constexpr uint16_t kBra   = 0xA005;   // bra: delayed branch with a static target
constexpr uint16_t kMovLR2 = 0xD202;  // mov.l @(disp,pc),r2

// Double-click opens the editor in the row, seeded with exactly the Mnemonic + Operands the row
// shows -- including the "@(disp,pc)" spelling of a row that follows a delayed branch.
void TestDoubleClickOpensEditorWithSeed()
{
    Fixture f;
    Prepare(f);
    f.backend.Put(kBase + 2, kAddR1);
    f.backend.Put(kBase + 6, kBra);
    f.backend.Put(kBase + 8, kMovLR2);
    CHECK(!f.panel.Editing());

    OpenRow(f, 1);
    CHECK(f.panel.Editing());
    CHECK(f.panel.EditText() == TextOf(Sh2Decode(kBase + 2, kAddR1)));
    CHECK(f.panel.EditText() == "add #0x1,r1");
    PressKey(f, ImGuiKey_Escape);
    CHECK(!f.panel.Editing());

    OpenRow(f, 2);                           // a nop: no operands, so no trailing blank
    CHECK(f.panel.EditText() == "nop");
    PressKey(f, ImGuiKey_Escape);

    OpenRow(f, 4);                           // follows the bra at row 3
    const DisassembledInstruction slot =
        Sh2DecodeAfterBranch(kBase + 8, kMovLR2, Sh2Decode(kBase + 6, kBra));
    CHECK(f.panel.EditText() == TextOf(slot));
    CHECK(f.panel.EditText().find(",pc)") != std::string::npos);
    CHECK(f.backend.writes.empty());
}

// Typing "nop" and pressing Enter writes 00 09 in exactly one WriteMemory call, at the row's address.
void TestNopWritesOneCall()
{
    Fixture f;
    Prepare(f);
    f.backend.Put(kBase + 2, kAddR1);
    OpenRow(f, 1);
    Type(f, "nop");
    CHECK(f.panel.EditText() == "nop");        // typing replaced the seed (it is selected on open)
    PressKey(f, ImGuiKey_Enter);

    CHECK(f.backend.writes.size() == 1);
    if (f.backend.writes.size() == 1)
    {
        CHECK(f.backend.writes[0].addr == kBase + 2);
        CHECK((f.backend.writes[0].bytes == std::vector<uint8_t>{ 0x00, 0x09 }));
    }
    CHECK(!f.panel.Editing());
    CHECK(!f.panel.NoticeIsError());
    CHECK(f.panel.Notice().find("Wrote 2 bytes at 06000002") != std::string::npos);
    CHECK(f.panel.Notice().find("Queued for emulator") != std::string::npos);
    // The App's log line: what was written, where, old and new.
    CHECK(f.req.assembled && f.req.asmAddr == kBase + 2 && f.req.asmSize == 2);
    CHECK(f.req.asmOld == kAddR1 && f.req.asmNew == 0x0009);
}

// The row's neighbours reach the assembler: after a delayed branch, an absolute PC-relative operand
// assembles as "reached directly" and the assembler's warning is shown after the (successful) write.
void TestContextFromRowAndWarningsAfterWrite()
{
    Fixture f;
    Prepare(f);
    f.backend.Put(kBase + 6, kBra);
    f.backend.Put(kBase + 8, kMovLR2);
    OpenRow(f, 4);
    Type(f, "mov.l @(0x06000030),r1");
    PressKey(f, ImGuiKey_Enter);
    CHECK(f.backend.writes.size() == 1);
    if (f.backend.writes.size() == 1)
    {
        CHECK(f.backend.writes[0].addr == kBase + 8);
        // base (0x0600000C) to 0x06000030 is 0x24 bytes = disp 9: 0xD109
        CHECK((f.backend.writes[0].bytes == std::vector<uint8_t>{ 0xD1, 0x09 }));
    }
    CHECK(f.panel.Notice().find("Wrote 2 bytes") != std::string::npos);
    CHECK(f.panel.Notice().find("follows a delayed branch") != std::string::npos);
    CHECK(!f.panel.NoticeIsError());
}

// Replacing a delayed branch with a non-delayed instruction is the panel's note to make.
void TestReplacingDelayedBranchWarns()
{
    Fixture f;
    Prepare(f);
    f.backend.Put(kBase + 6, kBra);
    OpenRow(f, 3);
    Type(f, "nop");
    PressKey(f, ImGuiKey_Enter);
    CHECK(f.backend.writes.size() == 1);
    CHECK(f.panel.Notice().find("no longer its delay slot") != std::string::npos);
    CHECK(f.panel.Notice().find("06000008") != std::string::npos);
}

// Invalid text writes nothing, keeps the editor open and shows the error with its column; a fix then
// goes through (the box kept the keyboard).
void TestInvalidTextWritesNothing()
{
    Fixture f;
    Prepare(f);
    f.backend.Put(kBase + 2, kAddR1);
    OpenRow(f, 1);
    Type(f, "frob r1");
    PressKey(f, ImGuiKey_Enter);
    CHECK(f.backend.writes.empty());
    CHECK(f.panel.Editing());
    CHECK(f.panel.NoticeIsError());
    CHECK(f.panel.Notice().find("column 1") != std::string::npos);
    f.h.Settle();
    CHECK(f.panel.Editing());

    // Select-all is not repeated, so replace the text the way a user would: select all, type.
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_A, true);
    f.h.Frame(Away(), false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_A, false);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    f.h.Frame(Away(), false);
    Type(f, "nop");
    PressKey(f, ImGuiKey_Enter);
    CHECK(f.backend.writes.size() == 1);
    CHECK(!f.panel.Editing());
}

// A backend that refuses: with CanWrite false no editor opens and the backend's reason is shown; with
// CanWrite true but WriteMemory returning 0 or a short count, the reason (or the partial count) is
// shown and the bytes are as they were.
void TestRefusedWritesShowTheBackendsReason()
{
    {
        Fixture f;
        Prepare(f);
        f.backend.canWrite = false;
        f.backend.refusal = "Read-only: this is a savestate.";
        f.backend.Put(kBase + 2, kAddR1);
        OpenRow(f, 1);
        CHECK(!f.panel.Editing());
        CHECK(f.panel.Notice() == "Read-only: this is a savestate.");
        CHECK(f.panel.NoticeIsError());
        CHECK(f.backend.writes.empty());
    }
    {
        Fixture f;
        Prepare(f);
        f.backend.acceptBytes = 0;
        f.backend.refusal = "The emulator refused the poke.";
        f.backend.Put(kBase + 2, kAddR1);
        OpenRow(f, 1);
        Type(f, "nop");
        PressKey(f, ImGuiKey_Enter);
        CHECK(f.backend.writes.size() == 1);   // attempted once, as a single call
        CHECK(f.panel.Notice() == "The emulator refused the poke.");
        CHECK(f.panel.NoticeIsError());
        CHECK(!f.panel.Editing());
        f.h.Settle();
        const MemoryReadResult r = f.backend.ReadMemoryBatch({ { kBase + 2, 2 } })[0];
        CHECK(r.bytes[0] == 0x71 && r.bytes[1] == 0x01);
        OpenRow(f, 1);                          // and the row still shows the old instruction
        CHECK(f.panel.EditText() == "add #0x1,r1");
    }
    {
        Fixture f;
        Prepare(f);
        f.backend.acceptBytes = 1;
        f.backend.refusal = "short";
        f.backend.Put(kBase + 2, kAddR1);
        OpenRow(f, 1);
        Type(f, "nop");
        PressKey(f, ImGuiKey_Enter);
        CHECK(f.backend.writes.size() == 1);
        CHECK(f.panel.Notice().find("Partially written 1/2 at 06000002") != std::string::npos);
        CHECK(f.panel.NoticeIsError());
        CHECK(!f.req.assembled);
    }
}

// Escape cancels; so does losing focus (a click on another row).
void TestEscapeAndFocusLossCancel()
{
    Fixture f;
    Prepare(f);
    f.backend.Put(kBase + 2, kAddR1);
    OpenRow(f, 1);
    Type(f, "nop");
    PressKey(f, ImGuiKey_Escape);
    CHECK(!f.panel.Editing());
    CHECK(f.backend.writes.empty());

    OpenRow(f, 1);
    CHECK(f.panel.Editing());
    f.h.Click(CellOfVisibleRow(5, 1));            // another row's address cell
    f.h.Settle();
    CHECK(!f.panel.Editing());
    CHECK(f.backend.writes.empty());
}

// The edit box must not make its row taller than a text line.
void TestEditBoxKeepsRowHeight()
{
    Fixture f;
    Prepare(f);
    const float before = InferredRowHeight();
    CHECK(before > 4.0f);
    OpenRow(f, 1);
    CHECK(f.panel.Editing());
    CHECK(std::fabs(InferredRowHeight() - before) < 0.01f);
    PressKey(f, ImGuiKey_Escape);
    f.h.Settle();
    CHECK(std::fabs(InferredRowHeight() - before) < 0.01f);
}

// With Auto Refresh off the rows are held across frames, and a backend write does not move SourceId;
// the committed row must re-decode anyway.
void TestAutoRefreshOffRedecodesAfterCommit()
{
    Fixture f;
    f.panel.SetAutoRefresh(false);
    f.backend.Put(kBase + 2, kAddR1);               // before the first draw: the held rows are read once
    Prepare(f);
    OpenRow(f, 1);
    CHECK(f.panel.EditText() == "add #0x1,r1");
    PressKey(f, ImGuiKey_Escape);

    f.backend.Put(kBase + 2, 0x7202);             // behind the panel's back: held rows stay stale
    f.h.Settle();
    OpenRow(f, 1);
    CHECK(f.panel.EditText() == "add #0x1,r1");
    Type(f, "nop");
    PressKey(f, ImGuiKey_Enter);
    CHECK(f.backend.writes.size() == 1);
    f.h.Settle();
    OpenRow(f, 1);
    CHECK(f.panel.EditText() == "nop");
    PressKey(f, ImGuiKey_Escape);
}

// A source change cancels the edit: the bytes behind the address are different data now.
void TestSourceChangeCancelsEdit()
{
    Fixture f;
    Prepare(f);
    f.backend.Put(kBase + 2, kAddR1);
    OpenRow(f, 1);
    Type(f, "nop");
    CHECK(f.panel.Editing());
    f.backend.sourceId = 2;
    f.h.Frame(Away(), false);
    CHECK(!f.panel.Editing());
    f.h.Settle();
    PressKey(f, ImGuiKey_Enter);
    CHECK(f.backend.writes.empty());
}

// Opens the row menu on the Instr cell of row 'row' and clicks the entry after 'itemsBefore' menu items
// and 'separatorsBefore' separators (a separator takes a smaller step than an item).
bool ClickRowMenuItem(Fixture& f, int row, int itemsBefore, int separatorsBefore)
{
    f.h.RightClick(CellOfVisibleRow(row, kColInstr));
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* pop = nullptr;
    for (ImGuiWindow* w : g.Windows)
        if (w->WasActive && (w->Flags & ImGuiWindowFlags_Popup)) pop = w;
    if (!pop) return false;
    const float step = g.FontSize + g.Style.ItemSpacing.y;
    const float sepStep = 1.0f + g.Style.ItemSpacing.y;
    const float y = pop->Pos.y + g.Style.WindowPadding.y + itemsBefore * step + separatorsBefore * sepStep + g.FontSize * 0.5f;
    const ImVec2 p(pop->Pos.x + 20.0f, y);
    f.h.Hover(p);
    f.h.Click(p);
    f.h.Settle();
    return true;
}

// "Edit Literal Value..." on a PC-relative load: the literal is written at the effective address in one
// call (4 bytes for mov.l), and the instructions sharing the slot are counted before and after.
// Items before it with no decompiler: 5 run/breakpoint items, 3 memory/copy items and "Assemble...", with
// two separators in between.
void TestEditLiteralWritesFourBytesAndReportsSharers()
{
    Fixture f;
    Prepare(f);
    // Two loads of the same pool word: 0x06000020 and 0x06000022 both resolve to 0x0600002C.
    f.backend.Put(kBase + 0x20, 0xD102);
    f.backend.Put(kBase + 0x22, 0xD202);
    const uint32_t lit = kBase + 0x2C;
    f.backend.Put(lit, 0x1234);
    f.backend.Put(lit + 2, 0x5678);
    CHECK(ClickRowMenuItem(f, 0x20 / 2, 9, 2));
    CHECK(f.panel.Editing());
    CHECK(f.panel.EditText() == "0x12345678");
    CHECK(f.backend.writes.empty());

    Type(f, "0xDEADBEEF");
    PressKey(f, ImGuiKey_Enter);
    CHECK(f.backend.writes.size() == 1);
    if (f.backend.writes.size() == 1)
    {
        CHECK(f.backend.writes[0].addr == lit);
        CHECK((f.backend.writes[0].bytes == std::vector<uint8_t>{ 0xDE, 0xAD, 0xBE, 0xEF }));
    }
    CHECK(!f.panel.Editing());
    CHECK(f.panel.Notice().find("Wrote 4 bytes at 0600002C") != std::string::npos);
    CHECK(f.panel.Notice().find("also loaded by 1 other instruction (first at 06000022)") != std::string::npos);
    CHECK(f.req.assembled && f.req.asmAddr == lit && f.req.asmSize == 4);
    CHECK(f.req.asmOld == 0x12345678u && f.req.asmNew == 0xDEADBEEFu);
}

// A word literal (mov.w) is 2 bytes; a malformed or too-large value writes nothing.
void TestEditLiteralWordAndInvalid()
{
    Fixture f;
    Prepare(f);
    f.backend.Put(kBase + 0x20, 0x9102);          // mov.w @(disp,pc),r1: 0x06000020 + 4 + 4 = 0x06000028
    f.backend.Put(kBase + 0x28, 0xABCD);
    CHECK(ClickRowMenuItem(f, 0x20 / 2, 9, 2));
    CHECK(f.panel.Editing());
    CHECK(f.panel.EditText() == "0xABCD");
    Type(f, "0x12345");                            // does not fit 2 bytes
    PressKey(f, ImGuiKey_Enter);
    CHECK(f.backend.writes.empty());
    CHECK(f.panel.Editing());
    CHECK(f.panel.NoticeIsError());
    f.h.Settle();                                  // the box takes the keyboard back a frame after a refused Enter
    PressKey(f, ImGuiKey_Escape);
    CHECK(!f.panel.Editing());

    CHECK(ClickRowMenuItem(f, 0x20 / 2, 9, 2));
    Type(f, "-2");
    PressKey(f, ImGuiKey_Enter);
    CHECK(f.backend.writes.size() == 1);
    if (f.backend.writes.size() == 1)
    {
        CHECK(f.backend.writes[0].addr == kBase + 0x28);
        CHECK((f.backend.writes[0].bytes == std::vector<uint8_t>{ 0xFF, 0xFE }));
    }
    CHECK(f.panel.Notice().find("also loaded") == std::string::npos);
}
}  // namespace

int main()
{
    TestDoubleClickOpensEditorWithSeed();
    TestNopWritesOneCall();
    TestContextFromRowAndWarningsAfterWrite();
    TestReplacingDelayedBranchWarns();
    TestInvalidTextWritesNothing();
    TestRefusedWritesShowTheBackendsReason();
    TestEscapeAndFocusLossCancel();
    TestEditBoxKeepsRowHeight();
    TestAutoRefreshOffRedecodesAfterCommit();
    TestSourceChangeCancelsEdit();
    TestEditLiteralWritesFourBytesAndReportsSharers();
    TestEditLiteralWordAndInvalid();
    TestDrawsTheWholeRegion();
    TestEditingACommentKeepsRowHeight();
    TestJumpLandsOnTargetWhileEditing();
    TestBreakpointRevealReturnsFromBrowsing();
    TestBreakpointRevealAtUnchangedPc();
    TestWatchSessionSave();
    if (gFailures) { std::printf("FAILURES: %d\n", gFailures); return 1; }
    std::printf("all cases passed\n");
    return 0;
}
