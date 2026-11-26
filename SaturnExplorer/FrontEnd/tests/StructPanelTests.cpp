// Structs panel tests -- the real StructPanel, driven headlessly with the app's theme and font
// against a mock memory backend (see ImGuiHarness.h).
//
// Each case pins behaviour a unit test on the model cannot see: clicks reaching the edit box in
// a tree row and in a table row, the exact bytes of a write, what a refused or short write
// says, the pointer chain limits, clipped reads of a huge array, row height while editing, and
// the rule that a file which failed to load is never overwritten.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "ImGuiHarness.h"
#include "FileWrite.h"
#include "StructPanel.h"
#include "Theme.h"
#include "WorkFiles.h"

#include <cstdlib>

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

class MockBackend : public IMemoryBackend
{
public:
    struct Write { uint32_t addr; std::vector<uint8_t> bytes; };

    std::vector<uint8_t> ram = std::vector<uint8_t>(kSize, 0);
    uint64_t id = 1;
    bool     canWrite = true;
    int      writeResult = -1;            // -1: really write; else the count to claim (RAM untouched)
    std::string refusal = "the source refused it";
    uint32_t badLo = 0, badHi = 0;        // reads overlapping [badLo, badHi) fail
    size_t   requests = 0;                // read requests seen
    size_t   requestedBytes = 0;
    std::vector<Write> writes;

    bool Connected() const override { return true; }
    uint64_t SourceId() const override { return id; }
    bool CanWrite(uint32_t) const override { return canWrite; }
    std::string WriteRefusal(uint32_t) const override { return refusal; }
    WriteDest WriteDestination(uint32_t) const override { return WriteDest::Emulator; }

    std::vector<MemoryReadResult> ReadMemoryBatch(const std::vector<MemoryReadRequest>& rs) override
    {
        std::vector<MemoryReadResult> out;
        for (const MemoryReadRequest& r : rs)
        {
            ++requests;
            requestedBytes += r.size;
            MemoryReadResult res;
            const uint32_t c = r.address & 0x07FFFFFFu;
            const bool overlapsBad = badHi > badLo && c < badHi && c + r.size > badLo;
            if (!overlapsBad && c >= kBase && c - kBase + r.size <= kSize)
            {
                res.success = true;
                res.bytes.assign(ram.begin() + (c - kBase), ram.begin() + (c - kBase) + r.size);
            }
            out.push_back(std::move(res));
        }
        return out;
    }

    size_t WriteMemory(uint32_t a, const uint8_t* bytes, size_t n) override
    {
        Write w;
        w.addr = a;
        w.bytes.assign(bytes, bytes + n);
        writes.push_back(w);
        if (writeResult >= 0) return (size_t)writeResult;
        const uint32_t c = a & 0x07FFFFFFu;
        for (size_t i = 0; i < n; ++i) ram[c - kBase + i] = bytes[i];
        return n;
    }

    void Put32(uint32_t a, uint32_t v)
    {
        const uint32_t o = (a & 0x07FFFFFFu) - kBase;
        ram[o] = (uint8_t)(v >> 24); ram[o + 1] = (uint8_t)(v >> 16); ram[o + 2] = (uint8_t)(v >> 8); ram[o + 3] = (uint8_t)v;
    }
};

class StubPlatform : public IPlatform
{
public:
    std::string openPath;
    bool Initialize(const PlatformConfig&) override { return true; }
    void Shutdown() override {}
    bool PumpEvents() override { return true; }
    void BeginFrame() override {}
    void EndFrame() override {}
    TextureHandle CreateTexture(int, int) override { return 0; }
    void UpdateTexture(TextureHandle, const void*, int, int) override {}
    void DestroyTexture(TextureHandle) override {}
    bool OpenFileDialog(std::string& p) override { p = openPath; return !openPath.empty(); }
    SaveOutcome SaveFile(const char*, const void*, size_t) override { return SaveOutcome::Cancelled; }
};

const char* kItemDefs =
    "struct Vec { s16 x; s16 y; };\n"
    "struct Item { u8 id; Vec pos; u32 flags; };\n";

struct Fixture
{
    MockBackend   backend;
    SimpleExpressionResolver resolver;
    StubPlatform  platform;
    BreakpointManager bps;
    StructPanel   panel;
    StructPanel::WatchRequest watchReq;
    uint32_t      hexJump = 0;
    float         dt = 0.0f;
    ImGuiHarness  h;

    explicit Fixture(const std::string& text) : h([this] { Ui(); })
    {
        ApplyTheme(ImGui::GetStyle());
        LoadFonts(ImGui::GetIO());
        std::string msg;
        if (!text.empty()) CHECK(panel.ImportText(text, &msg));
        panel.SetRecordRows(true);
        panel.SetExpandAll(true);
        h.Settle();
    }

    void Ui()
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(1270, 700), ImGuiCond_Always);
        panel.Draw(backend, resolver, platform, bps, dt, &hexJump, &watchReq);
    }

    const StructPanel::RowRecord* Row(const std::string& key) const
    {
        for (const StructPanel::RowRecord& r : panel.Rows())
            if (r.key == key) return &r;
        return nullptr;
    }
    const StructPanel::RowRecord* RowByLabel(const std::string& label) const
    {
        for (const StructPanel::RowRecord& r : panel.Rows())
            if (r.label == label) return &r;
        return nullptr;
    }
    void Dump() const
    {
        for (const StructPanel::RowRecord& r : panel.Rows())
            std::fprintf(stderr, "  [%s] label=%s addr=%s type=%s value=%s edit=%d\n", r.key.c_str(), r.label.c_str(),
                         r.address.c_str(), r.type.c_str(), r.value.c_str(), (int)r.editing);
    }
    size_t CountLabel(const std::string& label) const
    {
        size_t n = 0;
        for (const StructPanel::RowRecord& r : panel.Rows()) if (r.label == label) ++n;
        return n;
    }
    static ImVec2 Mid(ImVec2 a, ImVec2 b) { return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); }

    void DoubleClickValue(const std::string& key)
    {
        const StructPanel::RowRecord* r = Row(key);
        CHECK(r != nullptr);
        if (!r) return;
        const ImVec2 p = Mid(r->valueMin, r->valueMax);
        h.Hover(p);
        h.Click(p);
        h.Click(p);
        h.Settle();
    }
    void Type(const char* chars)
    {
        for (const char* c = chars; *c; ++c)
        {
            ImGui::GetIO().AddInputCharacter((unsigned)*c);
            h.Frame(ImVec2(1275, 715), false);
        }
        h.Frame(ImVec2(1275, 715), false);
    }
    void SelectAll()
    {
        // ImGui swaps Cmd and Ctrl when ConfigMacOSXBehaviors is set (the default on macOS): the shortcut key there
        // is Cmd, and a Ctrl event selects nothing, so the typed text would be inserted instead of replacing.
        const ImGuiKey mod = ImGui::GetIO().ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
        ImGui::GetIO().AddKeyEvent(mod, true);
        Key(ImGuiKey_A);
        ImGui::GetIO().AddKeyEvent(mod, false);
        h.Frame(ImVec2(1275, 715), false);
    }
    void Key(ImGuiKey key)
    {
        ImGui::GetIO().AddKeyEvent(key, true);
        h.Frame(ImVec2(1275, 715), false);
        ImGui::GetIO().AddKeyEvent(key, false);
        h.Frame(ImVec2(1275, 715), false);
        h.Frame(ImVec2(1275, 715), false);
    }
    // The first frame carries a refresh tick, so memory changed by the test is picked up.
    void Frames(int n)
    {
        for (int i = 0; i < n; ++i)
        {
            dt = i == 0 ? 0.11f : 0.0f;
            h.Frame(ImVec2(1275, 715), false);
        }
        dt = 0.0f;
    }
};

std::string ItemView(int count) { return std::string(kItemDefs) + "view a = Item[" + std::to_string(count) + "] @ 0x06000100;\n"; }

// ---------------------------------------------------------------- editing

// Enter writes the exact big-endian bytes in ONE call at the right address: a scalar in a nested
// struct inside an array element, edited from a tree row.
void TreeEditWritesExactBytes()
{
    Fixture f(ItemView(2));
    const std::string key = "v1[1].pos.y";
    f.backend.Put32(0x0600010C + 4, 0x00050000);   // pos.y reads 5 initially
    f.Frames(8);
    const auto* r = f.Row(key);
    CHECK(r != nullptr);
    CHECK(r && r->address == "0x06000110");
    CHECK(r && r->value == "5");

    f.DoubleClickValue(key);
    CHECK(f.panel.IsEditing());
    CHECK(f.Row(key) && f.Row(key)->editing);
    // A click on the edit box itself reaches it (the box stays active and takes text).
    const ImVec2 p = Fixture::Mid(f.Row(key)->valueMin, f.Row(key)->valueMax);
    f.h.Click(p);
    f.Frames(2);
    CHECK(f.panel.IsEditing());
    CHECK(ImGui::GetIO().WantTextInput);

    f.SelectAll();
    f.Type("-2");
    f.Key(ImGuiKey_Enter);
    CHECK(f.backend.writes.size() == 1);
    if (!f.backend.writes.empty())
    {
        CHECK(f.backend.writes[0].addr == 0x06000110);
        CHECK(f.backend.writes[0].bytes.size() == 2);
        CHECK(f.backend.writes[0].bytes.size() == 2 && f.backend.writes[0].bytes[0] == 0xFF && f.backend.writes[0].bytes[1] == 0xFE);
    }
    CHECK(!f.panel.IsEditing());
    CHECK(f.panel.Notice().find("Queued for emulator") != std::string::npos);
    f.Frames(3);
    CHECK(f.Row(key) && f.Row(key)->value == "-2");
}

// The same click-through in a table row (array view over 16 elements defaults to the table).
void TableEditWritesExactBytes()
{
    Fixture f(ItemView(20));
    const std::string key = "v1[3].pos.y";
    f.Frames(4);
    CHECK(f.Row(key) != nullptr);
    f.DoubleClickValue(key);
    CHECK(f.panel.IsEditing());
    const auto* r = f.Row(key);
    if (r) f.h.Click(Fixture::Mid(r->valueMin, r->valueMax));
    f.Frames(2);
    CHECK(f.panel.IsEditing());
    CHECK(ImGui::GetIO().WantTextInput);
    f.SelectAll();
    f.Type("300");
    f.Key(ImGuiKey_Enter);
    CHECK(f.backend.writes.size() == 1);
    if (!f.backend.writes.empty())
    {
        CHECK(f.backend.writes[0].addr == 0x06000100 + 3 * 12 + 4);
        CHECK(f.backend.writes[0].bytes.size() == 2 && f.backend.writes[0].bytes[0] == 0x01 && f.backend.writes[0].bytes[1] == 0x2C);
    }
}

void RefusedWriteKeepsOldValueAndSaysWhy()
{
    Fixture f(ItemView(1));
    f.backend.Put32(0x06000100, 0x07000000);   // id = 7
    f.Frames(8);
    const std::string key = "v1.id";
    CHECK(f.Row(key) && f.Row(key)->value == "07");

    // CanWrite false: edit mode is not entered and the refusal is shown.
    f.backend.canWrite = false;
    f.DoubleClickValue(key);
    CHECK(!f.panel.IsEditing());
    CHECK(f.panel.Notice().find("the source refused it") != std::string::npos);
    CHECK(f.backend.writes.empty());

    // CanWrite true but the write is refused at commit: the old value stays.
    f.backend.canWrite = true;
    f.backend.writeResult = 0;
    f.DoubleClickValue(key);
    CHECK(f.panel.IsEditing());
    f.Type("0x55");
    f.Key(ImGuiKey_Enter);
    CHECK(f.backend.writes.size() == 1);
    CHECK(f.panel.Notice().find("the source refused it") != std::string::npos);
    CHECK(f.panel.NoticeIsError());
    f.Frames(10);
    CHECK(f.Row(key) && f.Row(key)->value == "07");
}

void ShortWriteIsReported()
{
    Fixture f(ItemView(1));
    f.Frames(6);
    f.backend.writeResult = 1;                 // s16 needs 2
    f.DoubleClickValue("v1.pos.x");
    CHECK(f.panel.IsEditing());
    f.Type("9");
    f.Key(ImGuiKey_Enter);
    CHECK(f.panel.Notice().find("Partially written 1/2") != std::string::npos);
}

void EncodeErrorWritesNothing()
{
    Fixture f(ItemView(1));
    f.Frames(6);
    f.DoubleClickValue("v1.pos.x");
    CHECK(f.panel.IsEditing());
    f.Type("zzz");
    f.Key(ImGuiKey_Enter);
    CHECK(f.backend.writes.empty());
    CHECK(f.panel.IsEditing());                // stays open so the text can be fixed
    CHECK(!f.panel.EditError().empty());
}

void SourceChangeDropsTheEdit()
{
    Fixture f(ItemView(1));
    f.Frames(6);
    f.DoubleClickValue("v1.pos.x");
    CHECK(f.panel.IsEditing());
    f.backend.id = 2;
    f.Frames(2);
    CHECK(!f.panel.IsEditing());
    CHECK(f.backend.writes.empty());
}

void EscapeCancels()
{
    Fixture f(ItemView(1));
    f.Frames(6);
    f.DoubleClickValue("v1.pos.x");
    CHECK(f.panel.IsEditing());
    f.Type("9");
    f.Key(ImGuiKey_Escape);
    CHECK(!f.panel.IsEditing());
    CHECK(f.backend.writes.empty());
}

void RowHeightUnchangedWhileEditing()
{
    Fixture f(ItemView(2));
    f.Frames(6);
    std::vector<float> before;
    for (const auto& r : f.panel.Rows()) before.push_back(r.rowY);
    f.DoubleClickValue("v1[1].pos.y");
    CHECK(f.panel.IsEditing());
    std::vector<float> during;
    for (const auto& r : f.panel.Rows()) during.push_back(r.rowY);
    CHECK(before.size() == during.size());
    bool same = before.size() == during.size();
    for (size_t i = 0; same && i < before.size(); ++i) same = before[i] == during[i];
    CHECK(same);
}

// ---------------------------------------------------------------- pointers

void PointerFollowNullCycleAndDepth()
{
    Fixture f("struct Node { u32 hp; Node* next; };\nview n = Node[1] @ 0x06000200;\n");
    // A -> B -> null
    f.backend.Put32(0x06000200, 5);
    f.backend.Put32(0x06000204, 0x06000300);
    f.backend.Put32(0x06000300, 7);
    f.backend.Put32(0x06000304, 0);
    f.Frames(8);
    const auto* hp = f.Row("v1.next->.hp");
    CHECK(hp != nullptr);
    CHECK(hp && hp->value == "00000007");
    const auto* nx = f.Row("v1.next->.next");
    CHECK(nx && nx->value == "null");

    // B -> A: the chain ends with "(shown above)".
    f.backend.Put32(0x06000304, 0x06000200);
    f.Frames(10);
    CHECK(f.CountLabel("(shown above)") == 1);

    // A long list: followed at most 8 hops, then the limit row.
    for (int i = 0; i < 20; ++i)
    {
        f.backend.Put32(0x06000200 + 0x100 * i, (uint32_t)i);
        f.backend.Put32(0x06000204 + 0x100 * i, 0x06000200 + 0x100 * (i + 1));
    }
    f.Frames(12);
    CHECK(f.CountLabel("hp") == 9);            // the view's own struct plus 8 hops
    CHECK(f.CountLabel("(depth limit 8)") == 1);
    CHECK(f.CountLabel("(shown above)") == 0);
}

// ---------------------------------------------------------------- reads

void HugeArrayReadsOnlyVisibleRows()
{
    Fixture f("struct Cell { u32 a; u32 b; };\nview big = Cell[10000] @ 0x06000000;\n");
    f.backend.requests = 0;
    f.backend.requestedBytes = 0;
    f.Frames(6);
    CHECK(f.backend.requests > 0);
    CHECK(f.backend.requests < 40);
    CHECK(f.backend.requestedBytes < 40 * 8);
    size_t cells = 0;
    for (const auto& r : f.panel.Rows()) if (r.label == "a") ++cells;
    CHECK(cells > 0 && cells < 40);
}

void FallsBackToPerFieldReads()
{
    Fixture f(ItemView(1));
    f.backend.badLo = 0x100 + 8;               // flags (+8..+12) unreadable, rest fine
    f.backend.badHi = 0x100 + 12;
    f.backend.badLo += 0x06000000 - 0x06000000;
    // the mock compares masked addresses, which include the base
    f.backend.badLo += kBase;
    f.backend.badHi += kBase;
    f.backend.Put32(0x06000100, 0x09000000);
    f.Frames(8);
    CHECK(f.Row("v1.flags") && f.Row("v1.flags")->value == "Unavailable");
    CHECK(f.Row("v1.id") && f.Row("v1.id")->value == "09");
    CHECK(f.Row("v1.pos.x") && f.Row("v1.pos.x")->value != "Unavailable");
}

// ---------------------------------------------------------------- menu

void WatchpointMenuUsesLeafAddressAndSize()
{
    Fixture f(ItemView(2));
    f.Frames(6);
    const auto* r = f.Row("v1[1].flags");
    CHECK(r != nullptr);
    if (!r) return;
    f.h.Hover(Fixture::Mid(r->menuMin, r->menuMax));
    f.h.Click(Fixture::Mid(r->menuMin, r->menuMax));
    f.Frames(3);
    const StructPanel::MenuItemRecord* item = nullptr;
    for (const auto& m : f.panel.MenuItems()) if (m.label == "Break on Write") item = &m;
    CHECK(item != nullptr);
    if (!item) return;
    const ImVec2 p = Fixture::Mid(item->min, item->max);
    f.h.Hover(p);
    f.h.Click(p);
    f.Frames(2);
    CHECK(f.bps.All().size() == 1);
    if (!f.bps.All().empty())
    {
        CHECK(f.bps.All()[0].address == 0x0600010C + 8);
        CHECK(f.bps.All()[0].size == 4);
        CHECK(f.bps.All()[0].kind == BpKind::MemWrite);
    }
}

// The two hand-offs the App turns into actions: "View in Memory" fills the hex-jump out-param with the
// leaf's address, and "Add to Watch" fills the request with a name, an address expression and the
// leaf's type (App forwards it to WatchPanel::AddWatch).
bool ClickLeafMenuItem(Fixture& f, const char* rowKey, const char* label)
{
    const auto* r = f.Row(rowKey);
    CHECK(r != nullptr);
    if (!r) return false;
    f.h.Hover(Fixture::Mid(r->menuMin, r->menuMax));
    f.h.Click(Fixture::Mid(r->menuMin, r->menuMax));
    f.Frames(3);
    for (const auto& m : f.panel.MenuItems())
        if (m.label == label)
        {
            const ImVec2 p = Fixture::Mid(m.min, m.max);
            f.h.Hover(p);
            f.h.Click(p);
            f.Frames(2);
            return true;
        }
    return false;
}

void ViewInMemoryAndAddToWatchHandOff()
{
    {   // one harness at a time: each owns the ImGui context and its clipboard handlers
        Fixture f(ItemView(2));
        f.Frames(6);
        CHECK(ClickLeafMenuItem(f, "v1[1].flags", "View in Memory"));
        CHECK(f.hexJump == 0x0600010C + 8);
    }
    Fixture g(ItemView(2));
    g.Frames(6);
    CHECK(ClickLeafMenuItem(g, "v1[1].flags", "Add to Watch"));
    CHECK(g.watchReq.pending);
    CHECK(g.watchReq.type == WatchType::U32);
    CHECK(!g.watchReq.name.empty());
    CHECK(!g.watchReq.expression.empty());
}

// ---------------------------------------------------------------- definitions and persistence

void ApplyWithParseErrorLeavesLibraryUntouched()
{
    Fixture f(ItemView(1));
    const std::string before = f.panel.Library().Emit();
    CHECK(!f.panel.ApplyDefinitions("struct A {\n  u8 x;\n  u8 y\n};\n"));
    CHECK(f.panel.Library().Emit() == before);
    CHECK(!f.panel.DefinitionErrors().empty());
    bool lined = false;
    for (const ParseError& e : f.panel.DefinitionErrors()) if (e.line >= 3) lined = true;
    CHECK(lined);
}

void RemovedStructLeavesBrokenRow()
{
    Fixture f("struct A { u8 x; };\nstruct B { u8 y; };\nview vb = B[1] @ 0x06000100;\nview va = A[1] @ 0x06000110;\n");
    f.Frames(4);
    CHECK(f.panel.Library().Views().size() == 2);
    CHECK(f.panel.ApplyDefinitions("struct A { u8 x; };\n"));
    CHECK(f.panel.Library().Views().size() == 1);
    CHECK(f.panel.BrokenViewCount() == 1);
    f.Frames(4);
    bool broken = false;
    for (const auto& r : f.panel.Rows())
        if (r.broken && r.label == "vb" && r.value.find("not defined") != std::string::npos) broken = true;
    CHECK(broken);
    CHECK(f.panel.FullText().find("//!broken view vb = B[1] @ 0x06000100;") != std::string::npos);

    // Defining B again heals the view.
    CHECK(f.panel.ApplyDefinitions("struct A { u8 x; };\nstruct B { u8 y; };\n"));
    CHECK(f.panel.BrokenViewCount() == 0);
    CHECK(f.panel.Library().Views().size() == 2);
}

std::string TempDir(const char* name)
{
    // Under the platform's temp folder (TEMP on Windows, TMPDIR elsewhere): "/tmp" does not exist on Windows.
#ifdef _WIN32
    const char* base = std::getenv("TEMP");
#else
    const char* base = std::getenv("TMPDIR");
#endif
    std::string root = base && *base ? base : "/tmp";
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    const std::string d = root + "/se_structpanel_" + name;
    MakeDirectory(d);
    return d;
}
void WriteText(const std::string& path, const std::string& text)
{
    std::ofstream o(path, std::ios::binary);
    o << text;
}
std::string ReadText(const std::string& path)
{
    std::ifstream i(path, std::ios::binary);
    std::stringstream s;
    s << i.rdbuf();
    return s.str();
}

void FailedLoadIsNeverOverwritten()
{
    const std::string dir = TempDir("failed");
    SetWorkFolder(dir);
    const std::string file = WorkFilePath("saturn_structs.txt");
    const std::string garbage = "struct Broken {{{ nonsense\n";
    WriteText(file, garbage);

    StructPanel p;
    std::string err;
    CHECK(!p.LoadSession(&err));
    CHECK(p.LoadFailed());
    CHECK(!err.empty());
    std::string saveErr;
    CHECK(!p.SaveSession(&saveErr));
    CHECK(!saveErr.empty());
    CHECK(ReadText(file) == garbage);

    // Text that parses lifts the hold; only then does a save replace the file.
    CHECK(p.ApplyDefinitions("struct A { u8 x; };\n"));
    CHECK(!p.LoadFailed());
    CHECK(p.SaveSession(&saveErr));
    CHECK(ReadText(file).find("struct A") != std::string::npos);

    // Round trip, including a broken view.
    StructPanel q;
    CHECK(q.LoadSession());
    CHECK(q.Library().Structs().size() == 1);
    SetWorkFolder("");
}

void NeverLoadedDoesNotClobberExistingFile()
{
    const std::string dir = TempDir("neverloaded");
    SetWorkFolder(dir);
    const std::string file = WorkFilePath("saturn_structs.txt");
    const std::string good = "struct A size(0x1) { u8 x @0x0; };\n";
    WriteText(file, good);

    StructPanel p;                              // LoadSession never called
    std::string err;
    CHECK(!p.SaveSession(&err));
    CHECK(ReadText(file) == good);

    // And a loaded-empty-because-absent panel never creates an empty file.
    RemoveFile(file);
    StructPanel q;
    CHECK(q.LoadSession());
    CHECK(q.SaveSession(&err));
    CHECK(!FileOrDirectoryExists(file));
    StructPanel r;                              // never loaded, no file, nothing to save
    CHECK(r.SaveSession(&err));
    CHECK(!FileOrDirectoryExists(file));
    SetWorkFolder("");
}

void BrokenViewsSurviveARestart()
{
    const std::string dir = TempDir("broken");
    SetWorkFolder(dir);
    RemoveFile(WorkFilePath("saturn_structs.txt"));
    StructPanel p;
    CHECK(p.LoadSession());
    CHECK(p.ImportText("struct A { u8 x; };\nstruct B { u8 y; };\nview vb = B[1] @ 0x06000100;\n"));
    CHECK(p.ApplyDefinitions("struct A { u8 x; };\n"));
    CHECK(p.BrokenViewCount() == 1);
    std::string err;
    CHECK(p.SaveSession(&err));
    StructPanel q;
    CHECK(q.LoadSession());
    CHECK(q.BrokenViewCount() == 1);
    SetWorkFolder("");
}

void ImportRejectsBadTextWithoutChange()
{
    Fixture f(ItemView(1));
    const std::string before = f.panel.Library().Emit();
    std::string msg;
    CHECK(!f.panel.ImportText("struct X { bogus y; };\n", &msg));
    CHECK(f.panel.Library().Emit() == before);
    CHECK(!msg.empty());
}

void RequestViewAtAddsAView()
{
    Fixture f(kItemDefs);
    f.panel.RequestViewAt(0x06001234, "from watch");
    CHECK(f.panel.Library().Views().size() == 1);
    CHECK(f.panel.Library().Views()[0].expression == "0x06001234");
}
// ---------------------------------------------------------------- root-row editing

// Click a root-row box, replace its text and press Enter.
void TypeIntoBox(Fixture& f, ImVec2 at, const char* text)
{
    f.h.Hover(at);
    f.h.Click(at);
    f.Frames(2);
    f.SelectAll();
    f.Type(text);
    f.Key(ImGuiKey_Enter);
    f.Frames(2);
}

// Saving clears the dirty flag, so a later edit can be seen to set it.
void MakeClean(Fixture& f, const char* name)
{
    SetWorkFolder(TempDir(name));
    std::string err;
    CHECK(f.panel.SaveSession(&err));
    CHECK(!f.panel.NeedsSave());
}

void TypedCountTakesEffect()
{
    Fixture f(ItemView(1));
    f.Frames(4);
    MakeClean(f, "count");
    const auto* root = f.Row("v1");
    CHECK(root != nullptr);
    if (!root) return;
    TypeIntoBox(f, root->countPos, "5");
    CHECK(f.panel.Library().Views()[0].count == 5);
    CHECK(f.panel.NeedsSave());
    CHECK(f.Row("v1") && f.Row("v1")->value == "5");

    // Zero and negatives clamp to 1, and the box shows the stored value afterwards.
    TypeIntoBox(f, f.Row("v1")->countPos, "0");
    CHECK(f.panel.Library().Views()[0].count == 1);
    CHECK(f.Row("v1") && f.Row("v1")->value == "1");
    TypeIntoBox(f, f.Row("v1")->countPos, "-3");
    CHECK(f.panel.Library().Views()[0].count == 1);
    SetWorkFolder("");
}

void TypedLabelAndAddressTakeEffect()
{
    Fixture f(ItemView(1));
    f.Frames(4);
    MakeClean(f, "labeladdr");
    const auto* root = f.Row("v1");
    CHECK(root != nullptr);
    if (!root) return;
    TypeIntoBox(f, root->labelPos, "renamed");
    CHECK(f.panel.Library().Views()[0].label == "renamed");
    CHECK(f.panel.NeedsSave());

    std::string err;
    CHECK(f.panel.SaveSession(&err));
    CHECK(!f.panel.NeedsSave());
    TypeIntoBox(f, f.Row("v1")->exprPos, "0x06000200");
    CHECK(f.panel.Library().Views()[0].expression == "0x06000200");
    CHECK(f.panel.NeedsSave());
    f.Frames(3);
    CHECK(f.Row("v1.id") && f.Row("v1.id")->address == "0x06000200");
    SetWorkFolder("");
}
}  // namespace

int main()
{
    TypedCountTakesEffect();
    TypedLabelAndAddressTakeEffect();
    TreeEditWritesExactBytes();
    TableEditWritesExactBytes();
    RefusedWriteKeepsOldValueAndSaysWhy();
    ShortWriteIsReported();
    EncodeErrorWritesNothing();
    SourceChangeDropsTheEdit();
    EscapeCancels();
    RowHeightUnchangedWhileEditing();
    PointerFollowNullCycleAndDepth();
    HugeArrayReadsOnlyVisibleRows();
    FallsBackToPerFieldReads();
    WatchpointMenuUsesLeafAddressAndSize();
    ViewInMemoryAndAddToWatchHandOff();
    ApplyWithParseErrorLeavesLibraryUntouched();
    RemovedStructLeavesBrokenRow();
    FailedLoadIsNeverOverwritten();
    NeverLoadedDoesNotClobberExistingFile();
    BrokenViewsSurviveARestart();
    ImportRejectsBadTextWithoutChange();
    RequestViewAtAddsAView();
    if (gFailures == 0) std::printf("StructPanelTests: all passed\n");
    return gFailures == 0 ? 0 : 1;
}
