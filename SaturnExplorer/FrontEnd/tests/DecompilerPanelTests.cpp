// SH-2 Pseudocode panel (PLAN.md Step 3) -- the real DecompilerPanel, with the real engine behind
// the real DecompilerRunner, driven headlessly through ImGuiHarness under the app's theme and font.
// Memory is a mock IMemoryBackend holding both work RAMs; the Memory panel used for the A1 edit
// case is the real HexEditorPanel, typing into that backend as a user would.
//
// Asynchrony is pinned by handshakes, not sleeps: the runner's phase observer holds the engine at
// the start of a decompilation until the test releases it, so "while the replacement is pending"
// is a state the test is provably in. Waiting for a result is a bounded loop of UI frames that
// stops on the condition, never on a timer.
//
// usage: SaturnExplorerDecompilerPanelTests <scratch dir>
#include "DecompilerTestSupport.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"

#include "DecompilerPanel.h"
#include "Debug/MemoryBackend.h"
#include "HexEditorPanel.h"
#include "ImGuiHarness.h"
#include "Theme.h"

#include <condition_variable>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <cstdlib>
#include <cstring>

using namespace sfe;
using namespace sfe::decomp;

namespace
{

std::string gConfig;

const uint32_t kLw = 0x00200000u, kHw = 0x06000000u, kSize = 0x00100000u;

// Both work RAMs. Every accepted write bumps the serial, as se_write_vram's RebuildDerived does.
class MockBackend : public IMemoryBackend
{
public:
    std::vector<uint8_t> lw = std::vector<uint8_t>(kSize, 0), hw = std::vector<uint8_t>(kSize, 0);
    uint64_t serial = 100;
    uint64_t reads = 0;

    bool Connected() const override { return true; }
    uint64_t SourceId() const override { return 7; }
    bool CanWrite(uint32_t a) const override { return Where(a, 1) != nullptr; }
    WriteDest WriteDestination(uint32_t) const override { return WriteDest::ViewOnly; }

    std::vector<MemoryReadResult> ReadMemoryBatch(const std::vector<MemoryReadRequest>& rs) override
    {
        std::vector<MemoryReadResult> out;
        for (const MemoryReadRequest& r : rs)
        {
            ++reads;
            MemoryReadResult res;
            if (const uint8_t* p = Where(r.address, r.size))
            {
                res.success = true;
                res.bytes.assign(p, p + r.size);
            }
            out.push_back(std::move(res));
        }
        return out;
    }

    size_t WriteMemory(uint32_t a, const uint8_t* bytes, size_t n) override
    {
        uint8_t* p = const_cast<uint8_t*>(Where(a, static_cast<uint32_t>(n)));
        if (!p) return 0;
        for (size_t i = 0; i < n; ++i) p[i] = bytes[i];
        ++serial;
        return n;
    }

private:
    const uint8_t* Where(uint32_t a, uint32_t n) const
    {
        const uint32_t c = a & 0x07FFFFFFu;
        if (c >= kHw && c - kHw + n <= kSize) return hw.data() + (c - kHw);
        if (c >= kLw && c - kLw + n <= kSize) return lw.data() + (c - kLw);
        return nullptr;
    }
};

// Holds the engine at the start of each decompilation while closed (the runner's phase observer).
class Gate
{
public:
    void Observe(DecompilePhase phase)
    {
        if (phase != DecompilePhase::Init) return;
        std::unique_lock<std::mutex> lock(mMutex);
        if (!mClosed) return;
        ++mHeld;
        mCv.notify_all();
        mCv.wait(lock, [this] { return !mClosed; });
        --mHeld;
    }
    void Close()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mClosed = true;
    }
    void Open()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mClosed = false;
        mCv.notify_all();
    }
    bool WaitHeld()
    {
        std::unique_lock<std::mutex> lock(mMutex);
        return mCv.wait_for(lock, std::chrono::seconds(30), [this] { return mHeld > 0; });
    }

private:
    std::mutex mMutex;
    std::condition_variable mCv;
    bool mClosed = false;
    int mHeld = 0;
};

ImVec2 Centre(ImVec2 a, ImVec2 b) { return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); }

bool Has(const std::string& text, const char* what) { return text.find(what) != std::string::npos; }

struct Fixture
{
    Gate gate;                 // before the runner: it must outlive the worker that waits on it
    MockBackend backend;
    DecompilerRunner runner;
    DecompilerPanel panel;
    HexEditorPanel hex;
    bool showHex = false;
    bool visible = true;       // the SH-2 Pseudocode window is shown (App: mPanels.decompiler)

    DecompViewKind kind = DecompViewKind::Static;
    uint64_t sourceId = 7, frame = 1;
    bool trackSerial = true;   // the view's serial is the backend's (an edit moves it)
    uint64_t fixedSerial = 0;
    se_sh2_regs regs[2] = {};
    bool haveRegs[2] = { true, true };
    std::shared_ptr<const BiosImage> bios;

    std::map<uint32_t, std::string> names;
    uint64_t namesGeneration = 1;
    std::unordered_map<uint32_t, std::string> comments;   // SH-2 Assembly's user notes
    FunctionBounds bounds;
    // The call stack as App holds it: entries, the memory they were read from, and a revision.
    std::set<uint32_t> stackEntries;
    SnapshotKey stackBuiltFor;
    uint64_t stackRevision = 0;

    DecompilerPanel::Request req;   // accumulated across frames until Take()
    ImGuiHarness h;

    explicit Fixture(RunnerOptions options = RunnerOptions())
        : runner(gConfig, options), h([this] { Ui(); })
    {
        ApplyTheme(ImGui::GetStyle());
        LoadFonts(ImGui::GetIO());
        runner.SetPhaseObserverForTests([this](DecompilePhase p) { gate.Observe(p); });
        std::vector<uint8_t> image;
        dts::PlaceFixture1(image);
        std::copy(image.begin(), image.end(), backend.hw.begin());
        // The end of a previous function just before the caller, as in real code, so the rts scan
        // can find the caller's entry from an address inside it.
        PutWord(dts::kFixture1Caller - 4, 0x000B);   // rts
        PutWord(dts::kFixture1Caller - 2, 0x0009);   //   nop
        regs[0].pc = dts::kFixture1AddOne;          // master: in the caller
        regs[1].pc = dts::kFixture1Callee + 2;      // slave: in the callee
        panel.SetCpu(0);
    }
    ~Fixture() { gate.Open(); }

    void PutWord(uint32_t a, uint16_t w)
    {
        backend.hw[a - kHw] = static_cast<uint8_t>(w >> 8);
        backend.hw[a - kHw + 1] = static_cast<uint8_t>(w);
    }   // never leave the worker held while the runner joins it

    DecompilerView View() const
    {
        DecompilerView v;
        v.backend = const_cast<MockBackend*>(&backend);
        v.kind = kind;
        v.sourceLabel = "test";
        v.sourceId = sourceId;
        v.deriveSerial = trackSerial ? backend.serial : fixedSerial;
        v.frameNumber = frame;
        for (int i = 0; i < 2; ++i)
        {
            v.regs[i] = regs[i];
            v.haveRegs[i] = haveRegs[i];
        }
        v.bios = bios;
        return v;
    }

    DecompilerInputs Inputs()
    {
        DecompilerInputs in;
        in.names = &names;
        in.namesGeneration = namesGeneration;
        in.bounds = &bounds;
        in.comments = &comments;
        in.callSites = [this](const SnapshotKey& snap, std::set<uint32_t>& out) {
            if (snap.sourceId == stackBuiltFor.sourceId && snap.deriveSerial == stackBuiltFor.deriveSerial &&
                snap.frameNumber == stackBuiltFor.frameNumber)
                out.insert(stackEntries.begin(), stackEntries.end());
        };
        in.callSitesRevision = stackRevision;
        return in;
    }

    void Ui()
    {
        const DecompilerView v = View();
        const DecompilerInputs in = Inputs();
        panel.Update(v, runner, in, visible);
        DecompilerPanel::Request r;
        if (visible)
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(760, 380), ImGuiCond_Always);
            panel.Draw(nullptr, v, runner, in, r);
        }
        if (r.jumpAssembly) { req.jumpAssembly = true; req.cpu = r.cpu; req.addr = r.addr; }
        if (r.viewHex) { req.viewHex = true; req.hexAddr = r.hexAddr; }
        if (r.rename) { req.rename = true; req.renameAddr = r.renameAddr; }
        if (r.boundsChanged) req.boundsChanged = true;
        if (showHex)
        {
            ImGui::SetNextWindowPos(ImVec2(0, 400), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(900, 310), ImGuiCond_Always);
            hex.Draw(backend, false, 1.0f / 60.0f);
        }
    }

    void Idle(int frames = 1)
    {
        for (int i = 0; i < frames; ++i) h.Frame(ImVec2(1270, 710), false);
    }

    // UI frames until 'done', bounded (the engine takes milliseconds; the bound only stops a hang).
    template <class F>
    bool PumpUntil(F done)
    {
        const auto t0 = std::chrono::steady_clock::now();
        while (!done())
        {
            if (dts::MsSince(t0) > 30000.0) return false;
            Idle();
            std::this_thread::yield();
        }
        return true;
    }

    bool WaitShown(const char* containing = nullptr)
    {
        const bool ok = PumpUntil([&] {
            return !panel.Pending() && panel.Shown() &&
                   (!containing || Has(panel.Shown()->Text(), containing));
        });
        if (!ok) Dump();
        return ok;
    }

    // The shown result, or an empty one: a failed check must not become a crash on the next line.
    const DecompileResult& Shown() const
    {
        static const DecompileResult none;
        return panel.Shown() ? *panel.Shown() : none;
    }

    SnapshotKey Held() const { return panel.HeldKey() ? *panel.HeldKey() : SnapshotKey(); }

    // What the panel holds, for a failed wait.
    void Dump() const
    {
        const DecompileResult* s = panel.Shown();
        std::printf("  panel: pending %d, captures %llu, submissions %llu, discarded %llu, entry 0x%08X, entry error '%s'\n",
                    panel.Pending(), (unsigned long long)panel.Captures(), (unsigned long long)panel.Submissions(),
                    (unsigned long long)panel.Discarded(), panel.WantedEntry(), panel.EntryError().c_str());
        if (s)
            std::printf("  shown: status %d, entry 0x%08X, error '%s'\n%s\n", (int)s->status, s->entry, s->error.c_str(),
                        s->Text().c_str());
        if (runner.Disabled()) std::printf("  runner disabled: %s\n", runner.DisabledReason().c_str());
    }

    int LineContaining(const char* what) const
    {
        const DecompileResult* s = panel.Shown();
        if (!s) return -1;
        for (size_t i = 0; i < s->lines.size(); ++i)
            if (Has(s->lines[i].text, what)) return static_cast<int>(i);
        return -1;
    }

    // Type into a panel control: click it, type, Enter.
    void TypeInto(const char* item, const char* text)
    {
        ImVec2 a, b;
        CHECK(panel.ItemRect(item, a, b));
        h.Click(Centre(a, b));
        Idle(2);
        // Empty the box: caret to the end, then delete what an earlier entry left.
        Key(ImGuiKey_End);
        for (int i = 0; i < 16; ++i) Key(ImGuiKey_Backspace);
        for (const char* c = text; *c; ++c) ImGui::GetIO().AddInputCharacter(static_cast<unsigned>(*c));
        Idle();
        Key(ImGuiKey_Enter);
    }

    void Key(ImGuiKey key)
    {
        ImGui::GetIO().AddKeyEvent(key, true);
        Idle();
        ImGui::GetIO().AddKeyEvent(key, false);
        Idle();
    }

    void ClickItem(const char* item)
    {
        ImVec2 a, b;
        CHECK(panel.ItemRect(item, a, b));
        h.Hover(Centre(a, b));
        h.Click(Centre(a, b));
        Idle();
    }

    // Right-click line 'line' and pick 'menuItem' from its context menu.
    void LineMenu(int line, const char* menuItem)
    {
        ImVec2 a, b;
        CHECK(panel.LineRect(line, a, b));
        h.Hover(Centre(a, b));
        h.RightClick(Centre(a, b));
        ImVec2 ma, mb;
        CHECK(panel.ItemRect(menuItem, ma, mb));
        h.Hover(Centre(ma, mb));
        h.Click(Centre(ma, mb));
        Idle();
    }

    DecompilerPanel::Request Take()
    {
        DecompilerPanel::Request r = req;
        req = DecompilerPanel::Request();
        return r;
    }
};

// A whole-row click navigates to the line's first instruction, on the panel's CPU -- and the row is
// a RowSelectable, so a click past the end of the text lands too (CLAUDE.md, PanelWidgets.h).
void TestLineClickNavigates()
{
    Fixture f;
    f.panel.ShowFunctionContaining(0, dts::kFixture1AddOne);
    CHECK(f.WaitShown("+ 1"));
    CHECK(f.Shown().entry == dts::kFixture1Caller);
    const int line = f.LineContaining("+ 1");
    CHECK(line >= 0);
    const Line& ln = f.Shown().lines[static_cast<size_t>(line)];
    CHECK(ln.firstAddr != 0);
    f.Idle(2);
    ImVec2 a, b;
    CHECK(f.panel.LineRect(line, a, b));
    f.Take();
    f.h.Hover(Centre(a, b));
    f.h.Click(Centre(a, b));
    DecompilerPanel::Request r = f.Take();
    CHECK(r.jumpAssembly && r.cpu == 0 && r.addr == ln.firstAddr);

    // Past the end of the text, near the row's right edge.
    const ImVec2 farRight(b.x - 6.0f, (a.y + b.y) * 0.5f);
    f.h.Hover(farRight);
    f.h.Click(farRight);
    r = f.Take();
    CHECK(r.jumpAssembly && r.addr == ln.firstAddr);

    // On the slave, the same line navigates the slave's view.
    f.panel.SetCpu(1);   // Follow PC is off: nothing is asked again
    f.Idle(2);
    f.h.Hover(Centre(a, b));
    f.h.Click(Centre(a, b));
    r = f.Take();
    CHECK(r.jumpAssembly && r.cpu == 1 && r.addr == ln.firstAddr);

    // The context menu: View in Memory goes to the same address.
    f.LineMenu(line, "View in Memory");
    r = f.Take();
    CHECK(r.viewHex && r.hexAddr == ln.firstAddr);
}

// Switching CPU keeps the output. With Follow PC it asks for the other CPU's function, and the old
// output stays on screen, labelled stale, until that arrives.
void TestCpuSwitchKeepsOutput()
{
    Fixture f;
    // Follow PC is the default target.
    CHECK(f.panel.FollowPc());
    CHECK(f.WaitShown("+ 1"));
    CHECK(f.Shown().entry == dts::kFixture1Caller);
    const std::string caller = f.Shown().Text();
    const uint64_t submitted = f.panel.Submissions();

    f.gate.Close();
    f.panel.SetCpu(1);
    f.Idle();
    CHECK(f.gate.WaitHeld());
    CHECK(f.panel.Submissions() == submitted + 1);
    CHECK(f.panel.Pending());
    CHECK(f.panel.Shown() && f.Shown().Text() == caller);   // retained
    CHECK(f.panel.ShownStale());                                      // and labelled
    f.Idle(5);
    CHECK(f.Shown().Text() == caller);
    f.gate.Open();
    CHECK(f.PumpUntil([&] { return !f.panel.Pending(); }));
    CHECK(f.Shown().entry == dts::kFixture1Callee);   // the slave's PC is in the callee
    CHECK(!f.panel.ShownStale());

    // Without Follow PC, switching CPU asks nothing and keeps everything.
    f.panel.ShowFunctionContaining(1, dts::kFixture1Callee);
    CHECK(f.WaitShown());
    const uint64_t before = f.panel.Submissions();
    const std::string text = f.Shown().Text();
    f.panel.SetCpu(0);
    f.Idle(10);
    CHECK(f.panel.Submissions() == before);
    CHECK(f.Shown().Text() == text && !f.panel.ShownStale());
}

// A1: a Hex Editor write (the real Memory panel, typing into the mock backend) moves the serial;
// the panel captures once and asks again; the old output stays, stale, until the new one is in.
void TestHexWriteReRequests()
{
    Fixture f;
    f.showHex = true;
    f.panel.ShowFunctionContaining(0, dts::kFixture1Caller);
    CHECK(f.WaitShown("+ 1"));
    const std::string before = f.Shown().Text();
    const uint64_t captures = f.panel.Captures(), submissions = f.panel.Submissions();

    // Idle frames ask nothing (no capture, no submission, no repeated question).
    f.Idle(30);
    CHECK(f.panel.Captures() == captures && f.panel.Submissions() == submissions);

    const uint32_t imm = dts::kFixture1AddOne + 1;   // add #1,r0: the immediate byte
    f.hex.GoTo(imm);
    f.h.Settle();
    ImGuiTable* grid = nullptr;
    {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        for (int i = 0; i < g.Tables.GetMapSize(); ++i)
            if (ImGuiTable* t = g.Tables.TryGetMapData(i))
                if (t->ColumnsCount == 18) grid = t;
    }
    CHECK(grid != nullptr);
    if (!grid) return;
    const float rowH = HexEditorPanel::Metrics().rowH;
    const int row = static_cast<int>((imm - kHw) / 16), col = static_cast<int>(imm & 15);
    const ImGuiTableColumn& c = grid->Columns[1 + col];
    const ImVec2 cell((c.MinX + c.MaxX) * 0.5f,
                      grid->InnerRect.Min.y + rowH + (row + 0.5f) * rowH - grid->InnerWindow->Scroll.y);

    f.gate.Close();
    f.h.Click(cell);
    CHECK(f.hex.SelectionStart() == static_cast<int64_t>(imm));
    const uint64_t serial = f.backend.serial;
    ImGui::GetIO().AddInputCharacter('0');
    ImGui::GetIO().AddInputCharacter('2');
    f.Idle(2);
    CHECK(f.backend.hw[imm - kHw] == 0x02);           // add #2,r0 now
    CHECK(f.backend.serial == serial + 1);
    CHECK(f.gate.WaitHeld());
    CHECK(f.panel.Captures() == captures + 1);        // once, not every frame
    CHECK(f.panel.Submissions() == submissions + 1);
    CHECK(f.panel.Pending() && f.panel.ShownStale() && f.panel.ShownOutdatedByView());
    CHECK(f.Shown().Text() == before);         // retained while the replacement runs
    f.gate.Open();
    CHECK(f.WaitShown("+ 2"));
    CHECK(!f.panel.ShownStale());
    CHECK(f.Shown().key.deriveSerial == f.backend.serial);
}

// Builds a panel holding a shown result and a pending replacement (held at the gate), for the
// install-rule cases below.
struct HeldReplacement
{
    Fixture f;
    std::string shown;
    DecompileResult answer;   // what the pending request's answer looks like, built from its state
    HeldReplacement()
    {
        f.panel.ShowFunctionContaining(0, dts::kFixture1Caller);
        CHECK(f.WaitShown("+ 1"));
        shown = f.Shown().Text();
        f.gate.Close();
        f.backend.hw[dts::kFixture1AddOne + 1 - kHw] = 0x02;
        ++f.backend.serial;   // an in-place edit
        f.Idle();
        CHECK(f.gate.WaitHeld());
        CHECK(f.panel.Pending());
        answer = f.Shown();
        answer.slot = DecompilerPanel::kSlot;
        answer.generation = f.panel.PendingGeneration();
        answer.key = f.Held();
        answer.requestedEntry = f.panel.WantedEntry();
        answer.lines.clear();
        Line l;
        l.text = "INJECTED";
        answer.lines.push_back(l);
    }
};

// The panel's own install rule, each check alone: a result the runner would hand over (or a
// crafted one) that is not the answer to the outstanding question is never installed, and what was
// on screen stays, stale. Each of the four is removed in turn by the mutation check (record).
void TestInstallRule()
{
    {
        HeldReplacement s;   // the control: the right answer is installed
        DecompileResult r = s.answer;
        CHECK(s.f.panel.Accept(r, s.f.runner.Epoch()));
        CHECK(s.f.Shown().lines.size() == 1 && s.f.Shown().lines[0].text == "INJECTED");
    }
    {
        HeldReplacement s;   // an older generation
        DecompileResult r = s.answer;
        r.generation -= 1;
        CHECK(!s.f.panel.Accept(r, s.f.runner.Epoch()));
        CHECK(s.f.Shown().Text() == s.shown && s.f.panel.ShownStale() && s.f.panel.Pending());
    }
    {
        HeldReplacement s;   // an older epoch: the session it was asked in has ended
        DecompileResult r = s.answer;
        CHECK(!s.f.panel.Accept(r, s.f.runner.Epoch() + 1));
        CHECK(s.f.Shown().Text() == s.shown && s.f.panel.ShownStale());
    }
    {
        HeldReplacement s;   // a key that is not the held snapshot's (the one before the edit)
        DecompileResult r = s.answer;
        r.key.deriveSerial -= 1;
        CHECK(!s.f.panel.Accept(r, s.f.runner.Epoch()));
        CHECK(s.f.Shown().Text() == s.shown && s.f.panel.ShownStale());
    }
    {
        HeldReplacement s;   // another entry than the one wanted
        DecompileResult r = s.answer;
        r.requestedEntry = dts::kFixture1Callee;
        CHECK(!s.f.panel.Accept(r, s.f.runner.Epoch()));
        CHECK(s.f.Shown().Text() == s.shown && s.f.panel.ShownStale());
    }
    {
        // The panel moved on to another snapshot without asking again (the new one has nothing to
        // decompile: the PC left captured memory). The outstanding request's answer is for the old
        // snapshot and must not be installed as if it described the new one.
        Fixture f;
        CHECK(f.WaitShown("+ 1"));   // Follow PC
        const std::string shown = f.Shown().Text();
        f.gate.Close();
        f.backend.hw[dts::kFixture1AddOne + 1 - kHw] = 0x02;
        ++f.backend.serial;
        f.Idle();
        CHECK(f.gate.WaitHeld());
        const uint64_t submitted = f.panel.Submissions();
        f.regs[0].pc = 0x00000600u;   // BIOS: not captured
        ++f.backend.serial;
        f.Idle();
        CHECK(f.panel.Submissions() == submitted);    // nothing to ask
        CHECK(!f.panel.EntryError().empty());
        f.gate.Open();
        CHECK(f.PumpUntil([&] { return !f.runner.Busy(); }));
        f.Idle(3);
        CHECK(f.Shown().Text() == shown && f.panel.ShownStale());
        CHECK(f.panel.Discarded() >= 1 && !f.panel.Pending());
    }
}

// A session change bumps the epoch without waiting for the worker; the old session's late answer is
// dropped and the previous output is kept, stale.
void TestSessionChangeDoesNotWait()
{
    HeldReplacement s;
    const auto t0 = std::chrono::steady_clock::now();
    s.f.runner.InvalidateSession();
    s.f.panel.ResetSession();
    s.f.sourceId = 8;              // the next source
    s.f.Idle();
    const double ms = dts::MsSince(t0);
    std::printf("session change with the engine held: %.2f ms\n", ms);
    CHECK(ms < 1000.0);            // the worker is still held: nothing joined it
    CHECK(s.f.runner.Busy());
    s.f.gate.Open();
    CHECK(s.f.PumpUntil([&] { return !s.f.panel.Pending(); }));
    // The new session's own request (sourceId 8) is answered and installed; the old one never is.
    CHECK(s.f.WaitShown("+ 2"));
    CHECK(s.f.Shown().key.sourceId == 8);
}

// Requests are asked once: a failure is shown, not retried every frame; A2 refusals never reach
// the engine; a hand-typed mirror is accepted and shown canonical.
void TestEntriesAndNoRetryLoops()
{
    Fixture f;
    f.Idle(3);
    f.TypeInto("entry", "00000600");   // BIOS, not captured
    CHECK(f.PumpUntil([&] { return f.panel.Shown() && !f.panel.Pending(); }));
    CHECK(f.Shown().status != DecompileStatus::Ok);
    const uint64_t n = f.panel.Submissions();
    f.Idle(60);
    CHECK(f.panel.Submissions() == n);   // no retry loop

    f.TypeInto("entry", "C6004000");     // cache data array
    f.Idle(3);
    CHECK(Has(f.panel.EntryError(), "cache data array"));
    CHECK(f.panel.Submissions() == n);   // refused before the engine
    CHECK(f.panel.ShownStale());

    f.TypeInto("entry", "26004000");     // cache-through mirror of the caller: run there, named canonical
    CHECK(f.WaitShown("+ 1"));
    CHECK(f.Shown().entry == dts::kFixture1Caller);
    CHECK(f.panel.WantedEntry() == 0x26004000u);
}

// A3: a delay-slot construct the spec cannot get right (bf/s) is flagged, with its address, and
// still shown.
void TestDelaySlotFlagged()
{
    Fixture f;
    std::vector<uint8_t> image(f.backend.hw.begin(), f.backend.hw.end());
    dts::PlaceFixture3(image);
    std::copy(image.begin(), image.end(), f.backend.hw.begin());
    ++f.backend.serial;
    f.Idle(3);
    f.TypeInto("entry", "06006100");
    CHECK(f.WaitShown());
    CHECK(f.Shown().unsupported && f.Shown().unsupportedAt == dts::kFixture3BfsSlot);
    CHECK(!f.Shown().lines.empty());
}

// Step 4: stopped on a register-target call, the panel says where it goes this time -- the
// register's value, and its name when it has one -- and only while that is true of the output
// on screen.
void TestRegisterHintAtHalt()
{
    Fixture f;
    f.kind = DecompViewKind::LivePaused;
    f.PutWord(dts::kFixture1Jsr - 2, 0x6042);   // mov.l @r4,r0 replaces the literal load: computed
    ++f.backend.serial;
    f.regs[0].pc = dts::kFixture1Jsr;
    f.regs[0].r[0] = dts::kFixture1Callee;
    f.names[dts::kFixture1Callee] = "read_word";
    ++f.namesGeneration;
    CHECK(f.WaitShown("(*(code *)"));                   // the output does not name the target
    f.Idle(2);
    std::printf("hint: %s\n", f.panel.HintText().c_str());
    CHECK(f.panel.HintText() == "r0 = 0x06005000 at the halt (read_word)");
    // Review of Step 4, finding 4: the same halt at a mirror of the call -- cache-through, or a
    // repeat of HWRAM -- is the same instruction, and gets the same hint.
    for (uint32_t pc : { dts::kFixture1Jsr | 0x20000000u, dts::kFixture1Jsr + 0x00100000u })
    {
        f.regs[0].pc = pc;
        ++f.frame;
        f.Idle();   // the panel sees the new frame
        CHECK(f.PumpUntil([&] { return !f.panel.ShownStale() && !f.panel.Pending(); }));
        f.Idle(2);
        CHECK(f.panel.HintText() == "r0 = 0x06005000 at the halt (read_word)");
    }
    f.regs[0].pc = dts::kFixture1Jsr;

    // The CPU steps on: no longer stopped on the call. (The panel is let see the new frame first:
    // until it has, the old output is shown, not pending, and WaitShown would return at once.)
    auto settled = [&] {
        f.Idle();
        return f.PumpUntil([&] { return !f.panel.ShownStale() && !f.panel.Pending(); });
    };
    f.regs[0].pc = dts::kFixture1AddOne;
    ++f.frame;
    CHECK(settled());
    CHECK(f.WaitShown("(*(code *)"));
    f.Idle(2);
    CHECK(f.panel.HintText().empty());

    // Running: the CPU is no longer where the registers say, even before the frame moves on and
    // the output goes stale.
    f.regs[0].pc = dts::kFixture1Jsr;
    ++f.frame;
    CHECK(settled());
    CHECK(f.WaitShown("(*(code *)"));
    f.Idle(2);
    CHECK(!f.panel.HintText().empty());
    f.kind = DecompViewKind::LiveRunning;
    f.Idle(2);
    CHECK(!f.panel.ShownStale());
    CHECK(f.panel.HintText().empty());

    // A call the output resolved statically (jsr through a literal) needs no hint.
    f.kind = DecompViewKind::LivePaused;
    f.PutWord(dts::kFixture1Jsr - 2, 0xD003);
    ++f.backend.serial;
    CHECK(f.WaitShown("read_word("));
    f.Idle(2);
    CHECK(f.panel.HintText().empty());
}

// Step 4: the user's Assembly notes appear on the lines decompiled from their instructions --
// once each, whichever mirror they were written at -- and editing one asks the engine nothing.
void TestUserCommentsOnLines()
{
    Fixture f;
    f.panel.ShowFunctionContaining(0, dts::kFixture1Caller);
    CHECK(f.WaitShown("+ 1"));
    const uint64_t n = f.panel.Submissions();
    f.comments[dts::kFixture1AddOne] = "count the hit";
    f.comments[dts::kFixture1MovZero | 0x20000000u] = "default";   // written at the cache-through mirror
    f.comments[0x06008000u] = "elsewhere";                          // not in this function
    f.Idle(2);
    const DecompileResult& r = f.Shown();
    const std::vector<std::string>& notes = f.panel.LineNotes();
    CHECK(notes.size() == r.lines.size());
    int hit = 0, dflt = 0;
    for (size_t i = 0; i < notes.size() && i < r.lines.size(); ++i)
    {
        if (notes[i].empty()) continue;
        std::printf("  %-40s // %s\n", r.lines[i].text.c_str(), notes[i].c_str());
        if (notes[i] == "count the hit") { ++hit; CHECK(Has(r.lines[i].text, "+ 1")); }
        if (notes[i] == "default") { ++dflt; CHECK(Has(r.lines[i].text, "= 0;")); }
        CHECK(!Has(notes[i], "elsewhere"));
    }
    CHECK(hit == 1 && dflt == 1);
    CHECK(f.panel.Submissions() == n);   // a display overlay, not engine input

    // macOS run: a note on the literal load (folded into the call, so no line comes from it) was
    // shown nowhere. It is listed under the code instead; one outside the function is not.
    f.comments[dts::kFixture1Jsr - 2] = "the literal";
    f.Idle(2);
    const std::vector<std::string>& unplaced = f.panel.UnplacedNotes();
    for (const std::string& u : unplaced) std::printf("  unplaced: %s\n", u.c_str());
    CHECK(unplaced.size() == 1 && unplaced[0] == "0x0600400C: the literal");

    // Review of Step 4, finding 5: a note wider than the window can be scrolled to.
    auto codeScrollMaxX = [] {
        for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
            if (std::strstr(w->Name, "##code")) return w->ScrollMax.x;
        return -1.0f;
    };
    f.comments.erase(dts::kFixture1Jsr - 2);
    f.Idle(2);
    const float before = codeScrollMaxX();
    f.comments[dts::kFixture1AddOne] = std::string(400, 'n');
    f.Idle(3);
    const float after = codeScrollMaxX();
    std::printf("  code scroll max x: %.0f before, %.0f with a 400-character note\n", before, after);
    CHECK(after > before + 1000.0f);

    f.comments.clear();
    f.Idle(2);
    for (const std::string& note : f.panel.LineNotes()) CHECK(note.empty());
    CHECK(f.panel.UnplacedNotes().empty());
}

// Step 4: a callee's signature, learned when the panel showed it, is lent to its caller. The
// caller shown first guesses from the call site; shown again after its callee, it is asked again
// -- the question has changed -- and the call shows what the callee really takes.
void TestCalleePrototypesLent()
{
    Fixture f;
    const uint32_t caller = 0x06007300u, callee = 0x06007340u;
    // As in Sh2DecompilerTests' TestCalleePrototypes (Capstone-checked): the caller sets r4-r6 and
    // bsr's to a callee that stores r4 and returns nothing.
    uint32_t a = caller;
    for (uint16_t w : { 0xE401, 0xE502, 0xE603, 0x4F22, 0xB01A, 0x0009, 0x4F26, 0x000B, 0x0009 })
    { f.PutWord(a, w); a += 2; }
    a = callee;
    for (uint16_t w : { 0xD101, 0x2142, 0x000B, 0x0009, 0x0600, 0x8000 }) { f.PutWord(a, w); a += 2; }
    ++f.backend.serial;
    f.Idle();

    f.TypeInto("entry", "06007300");
    CHECK(f.WaitShown("(1,2,3)"));
    f.TypeInto("entry", "06007340");
    CHECK(f.WaitShown("void func_0x06007340("));
    const uint64_t n = f.panel.Submissions();
    f.TypeInto("entry", "06007300");
    CHECK(f.WaitShown("func_0x06007340(1);"));
    CHECK(f.panel.Submissions() == n + 1);   // asked again, not served from the cache
    CHECK(f.panel.CacheHits() == 0);

    // Review of Step 4, finding 1: a signature belongs to the memory it was recovered from. Edit the
    // callee to take r5 as well, and the caller -- on screen, so asked again at once -- must not be
    // given the old one-argument signature. Shown the edited callee, the caller learns the new one.
    auto shownCurrent = [&](const char* text) {
        const bool ok = f.PumpUntil([&] {
            return !f.panel.Pending() && f.panel.Shown() && f.panel.HeldKey() &&
                   f.panel.Shown()->key == *f.panel.HeldKey() && Has(f.panel.Shown()->Text(), text);
        });
        if (!ok) f.Dump();
        return ok;
    };
    f.PutWord(callee + 6, 0x1151);   // the rts slot: mov.l r5,@(4,r1)
    ++f.backend.serial;
    CHECK(shownCurrent("(1,2,3)"));
    CHECK(!Has(f.Shown().Text(), "func_0x06007340(1);"));
    f.TypeInto("entry", "06007340");
    CHECK(shownCurrent("param_2"));
    f.TypeInto("entry", "06007300");
    CHECK(shownCurrent("func_0x06007340(1,2);"));

    // Likewise another BIOS image and another frame: a new snapshot, so nothing learned before it
    // is lent until it is learned again.
    f.bios = BiosImage::Make(std::vector<uint8_t>(0x1000, 0x5A));
    CHECK(shownCurrent("(1,2,3)"));
    f.TypeInto("entry", "06007340");
    CHECK(shownCurrent("param_2"));
    f.TypeInto("entry", "06007300");
    CHECK(shownCurrent("func_0x06007340(1,2);"));
    ++f.frame;
    CHECK(shownCurrent("(1,2,3)"));

    // A new session forgets them: what was learned belonged to the old source.
    f.panel.ResetSession();
    f.runner.InvalidateSession();
    ++f.sourceId;
    f.TypeInto("entry", "06007300");
    CHECK(f.WaitShown("(1,2,3)"));
}

// Review of Step 4 (second pass), finding 1: a signature is only as current as the signatures it was
// recovered with. A calls B calls C. B decompiled before C's signature is known says B returns what C
// left in r0; once C is known to be void, that is obsolete, and so is anything learned from it.
void TestPrototypeProvenance()
{
    Fixture f;
    const uint32_t A = 0x06007300u, B = 0x06007380u, C = 0x06007340u;
    auto place = [&](uint32_t at, std::initializer_list<uint16_t> words) {
        for (uint16_t w : words) { f.PutWord(at, w); at += 2; }
    };
    place(A, { 0xE401, 0xE502, 0xE603, 0x4F22, 0xB03A, 0x0009, 0x4F26, 0x000B, 0x0009 });   // r4-r6; bsr B
    place(B, { 0x4F22, 0xE401, 0xBFDC, 0x0009, 0x4F26, 0x000B, 0x0009 });   // mov #1,r4; bsr C
    place(C, { 0xD101, 0x2142, 0x000B, 0x0009, 0x0600, 0x8000 });           // stores r4, returns nothing
    ++f.backend.serial;
    f.Idle();
    auto show = [&](const char* entry) {
        f.TypeInto("entry", entry);
        const uint32_t want = static_cast<uint32_t>(std::strtoul(entry, nullptr, 16));
        const bool ok = f.PumpUntil([&] {
            return !f.panel.Pending() && f.panel.Shown() && f.panel.Shown()->entry == want && !f.panel.ShownStale();
        });
        if (!ok) f.Dump();
        return f.Shown().Text();
    };

    // A with nothing learned passes what it set up; lent B's signature, it knows B takes nothing.
    const std::string bare = show("06007300");
    CHECK(Has(bare, "(1,2,3)"));
    show("06007380");                       // B, before C is known: a guess, so nothing is lent
    const std::string withOldB = show("06007300");
    CHECK(withOldB == bare);                // (review of 8639b80; it used to be lent and dropped later)
    show("06007340");                       // C: void, one argument -- B's signature is now obsolete
    CHECK(Has(f.Shown().Text(), "void func_0x06007340("));
    const std::string afterC = show("06007300");
    CHECK(afterC == bare);                  // B's old signature is not lent
    show("06007380");                       // B again, lent C's
    const std::string withNewB = show("06007300");
    CHECK(Has(withNewB, "func_0x06007380();"));
    CHECK(!Has(withNewB, "= func_0x06007380("));
}

// Finding 2: the function set (the user's starts and the named addresses) is engine input -- it
// decides whether a branch is a call -- so it keys requests, acceptance and the cache, for every
// A saved volatile list that does not parse is kept, for the editor and for saving back, until a
// list that parses replaces it (review of 0e3cd12: it was saved back as nothing).
void TestUnparsedVolatileKept()
{
    Fixture f;
    std::string error;
    CHECK(!f.panel.SetVolatileMemory("26001000-zz", &error) && !error.empty());
    f.panel.KeepUnparsedVolatileMemory("26001000-zz");
    CHECK(f.panel.VolatileMemory() == "26001000-zz");
    CHECK(f.panel.SetVolatileMemory("26001000-26001003"));
    CHECK(f.panel.VolatileMemory() == "26001000-26001003");
    CHECK(f.panel.SetVolatileMemory(""));
    CHECK(f.panel.VolatileMemory().empty());   // the unparsed text is gone for good
}

// A function start set after the answer arrived makes the shown output stale even when nothing is
// pending any more (review of 0e3cd12: after a Stop, or with the runner disabled, nothing re-asks).
void TestFunctionSetChangeIsStale()
{
    Fixture f;
    f.Idle();
    f.TypeInto("entry", "06004000");
    CHECK(f.WaitShown("func_0x06005000"));
    CHECK(!f.panel.ShownStale());
    f.runner.InvalidateSession();   // as a session's end does: nothing outstanding will be answered
    f.bounds.SetEntry(0x06006000u);
    f.Idle();
    CHECK(f.panel.ShownStale());
}

// function, not only the one whose start was corrected.
void TestFunctionSetKeysQuestions()
{
    Fixture f;
    f.Idle();
    f.TypeInto("entry", "06004000");
    CHECK(f.WaitShown("func_0x06005000"));
    uint64_t n = f.panel.Submissions();

    f.bounds.SetEntry(0x06006000u);         // a start elsewhere: asked again
    f.Idle();
    CHECK(f.panel.Submissions() == n + 1);
    CHECK(f.WaitShown());
    n = f.panel.Submissions();
    const uint64_t hits = f.panel.CacheHits();
    f.bounds.ClearEntry(0x06006000u);       // and undone: the answer for that set again
    f.Idle();
    CHECK(f.panel.Submissions() == n && f.panel.CacheHits() == hits + 1);

    // An answer that finished while the set changed is not installed: asked again under the new set.
    f.gate.Close();
    f.backend.hw[dts::kFixture1AddOne + 1 - kHw] = 0x02;
    ++f.backend.serial;
    f.Idle();
    CHECK(f.gate.WaitHeld());
    n = f.panel.Submissions();
    f.bounds.SetEntry(0x06006000u);
    f.Idle();
    f.gate.Open();
    CHECK(f.PumpUntil([&] { return !f.runner.Busy() && !f.panel.Pending(); }));
    CHECK(f.panel.Submissions() == n + 1);

    // A caller cached before a correction elsewhere is not served from the cache after it.
    f.bounds.ClearEntry(0x06006000u);
    f.TypeInto("entry", "06005000");
    CHECK(f.WaitShown());
    f.TypeInto("entry", "06004000");
    CHECK(f.WaitShown());
    f.TypeInto("entry", "06005000");
    CHECK(f.WaitShown());
    f.bounds.SetEntry(0x06005002u);         // as Set Function Start Here would
    f.Idle();
    n = f.panel.Submissions();
    const uint64_t h2 = f.panel.CacheHits();
    f.TypeInto("entry", "06004000");
    CHECK(f.WaitShown());
    CHECK(f.panel.CacheHits() == h2 && f.panel.Submissions() >= n + 1);
}

// Finding 3: an entry discovered as a callee is a hint only for the memory that called it.
void TestCalleeHintsBelongToTheirSnapshot()
{
    Fixture f;
    f.Idle();
    f.TypeInto("entry", "06004000");
    CHECK(f.WaitShown("func_0x06005000"));   // 06005000 is now a known callee
    // Replace the code: a function now starts at 06004FF0 and runs through 06005000.
    for (uint32_t a = 0x06004FF0u; a < 0x06005010u; a += 2) f.PutWord(a, 0x7001);   // add #1,r0
    f.PutWord(0x06004FECu, 0x000B);
    f.PutWord(0x06004FEEu, 0x0009);
    f.PutWord(0x06005010u, 0x000B);
    f.PutWord(0x06005012u, 0x0009);
    ++f.backend.serial;
    f.panel.ShowFunctionContaining(0, 0x06005004u);
    f.Idle(2);
    CHECK(f.panel.WantedEntry() == 0x06004FF0u);
}

// Finding 4: back from a recorded frame to a running emulator, the scrubbed snapshot is not this
// emulator's memory at any lag: captured at once, auto-refresh or not.
void TestRunningAfterScrubCaptures()
{
    Fixture f;
    f.kind = DecompViewKind::Scrub;
    f.trackSerial = false;
    f.sourceId = 1000;
    CHECK(f.WaitShown("+ 1"));
    const uint64_t captures = f.panel.Captures();
    f.kind = DecompViewKind::LiveRunning;
    f.sourceId = 7;
    for (int i = 0; i < 5; ++i)
    {
        ++f.frame;
        ++f.fixedSerial;
        f.Idle();
    }
    CHECK(f.panel.Captures() == captures + 1);
    CHECK(f.Held().sourceId == 7);
    CHECK(f.WaitShown("+ 1"));
    CHECK(f.Shown().key.sourceId == 7);
}

// Finding 5: a containing function is found from the finder's hints too, so a symbol or a start
// added inside it moves it.
void TestHintsReResolve()
{
    Fixture f;
    f.panel.ShowFunctionContaining(0, dts::kFixture1AddOne);
    CHECK(f.WaitShown());
    CHECK(f.panel.WantedEntry() == dts::kFixture1Caller);
    f.names[dts::kFixture1AddOne] = "inner";
    ++f.namesGeneration;
    f.Idle(2);
    CHECK(f.panel.WantedEntry() == dts::kFixture1AddOne);
    f.names.erase(dts::kFixture1AddOne);
    ++f.namesGeneration;
    f.Idle(2);
    CHECK(f.panel.WantedEntry() == dts::kFixture1Caller);
    f.bounds.SetEntry(dts::kFixture1Jsr);
    f.Idle(2);
    CHECK(f.panel.WantedEntry() == dts::kFixture1Jsr);
}

// Review of 4bd1c99: the call stack's entries are finder hints too. A rebuilt stack finds the
// containing function again; a stack read from another frame is no hint for this one.
void TestCallStackHints()
{
    Fixture f;
    f.panel.ShowFunctionContaining(0, dts::kFixture1AddOne);
    CHECK(f.WaitShown());
    CHECK(f.panel.WantedEntry() == dts::kFixture1Caller);
    // A stack rebuilt (after the panel's Update, as App draws it) naming an entry inside it.
    f.stackEntries = { dts::kFixture1AddOne };
    f.stackBuiltFor = f.Held();
    ++f.stackRevision;
    f.Idle(2);
    CHECK(f.panel.WantedEntry() == dts::kFixture1AddOne);
    f.stackEntries.clear();
    ++f.stackRevision;
    f.Idle(2);
    CHECK(f.panel.WantedEntry() == dts::kFixture1Caller);

    // Scrubbing: frame 50's stack names 06005000. Frame 51 has a function at 06004FF0 running
    // through it; until the stack is rebuilt for frame 51, frame 50's entry must not pick it.
    f.kind = DecompViewKind::Scrub;
    f.trackSerial = false;
    f.sourceId = 1000;
    f.frame = 50;
    f.Idle();
    f.stackEntries = { 0x06005000u };
    f.stackBuiltFor = f.Held();
    ++f.stackRevision;
    f.frame = 51;
    for (uint32_t a = 0x06004FF0u; a < 0x06005010u; a += 2) f.PutWord(a, 0x7001);   // add #1,r0
    f.PutWord(0x06004FECu, 0x000B);
    f.PutWord(0x06004FEEu, 0x0009);
    f.PutWord(0x06005010u, 0x000B);
    f.PutWord(0x06005012u, 0x0009);
    f.panel.ShowFunctionContaining(0, 0x06005004u);
    f.Idle(2);
    CHECK(f.Held().frameNumber == 51);
    CHECK(f.panel.WantedEntry() == 0x06004FF0u);
    f.stackEntries.clear();                  // rebuilt for frame 51: nothing to add
    f.stackBuiltFor = f.Held();
    ++f.stackRevision;
    f.Idle(2);
    CHECK(f.panel.WantedEntry() == 0x06004FF0u);
    // ...and a stack for frame 51 that does name 06005000 is a hint for it.
    f.stackEntries = { 0x06005000u };
    ++f.stackRevision;
    f.Idle(2);
    CHECK(f.panel.WantedEntry() == 0x06005000u);
}

// Review at 6eaa133, finding 1: the entry is asked for where the code runs. A PC in the
// cache-through mirror decompiles its function there (mova sees the mirror), under the canonical
// identity; the two are different questions, never served from each other's cache.
void TestMirrorEntryIsKept()
{
    Fixture f;
    f.panel.ShowFunctionContaining(0, 0x20000000u | dts::kFixture1AddOne);
    CHECK(f.WaitShown("func_0x06005000"));
    CHECK(f.panel.WantedEntry() == (0x20000000u | dts::kFixture1Caller));
    CHECK(f.Shown().requestedEntry == (0x20000000u | dts::kFixture1Caller));
    CHECK(f.Shown().entry == dts::kFixture1Caller);
    CHECK(std::string(f.panel.EntryText()) == "26004000");
    const uint64_t n = f.panel.Submissions(), hits = f.panel.CacheHits();
    f.panel.ShowFunctionContaining(0, dts::kFixture1AddOne);
    f.Idle();
    CHECK(f.WaitShown());
    CHECK(f.panel.WantedEntry() == dts::kFixture1Caller && f.Shown().requestedEntry == dts::kFixture1Caller);
    CHECK(f.panel.Submissions() == n + 1 && f.panel.CacheHits() == hits);
    f.Idle(2);
    CHECK(std::string(f.panel.EntryText()) == "06004000");   // review of 37a0535: the box follows the mirror
    f.Idle();
    f.TypeInto("entry", "26004000");          // typed: kept as typed
    CHECK(f.PumpUntil([&] { return !f.panel.Pending() && f.Shown().requestedEntry == 0x26004000u; }));
    CHECK(f.panel.WantedEntry() == 0x26004000u);
    // Not from the cache: the canonical run's signature is now lent to it, a function of its own.
    f.Idle(2);
    CHECK(std::string(f.panel.EntryText()) == "26004000");
}

// Review of 37a0535, finding 1: a signature belongs to the address its function ran at. This
// callee's mova sees whether it runs through the cache-through mirror: canonically it stores its
// argument, through 26004800 it skips the store and takes none. Learning the mirror's signature
// must not change what a canonical caller is lent.
void TestPrototypesPerMirror()
{
    Fixture f;
    auto place = [&](uint32_t at, std::initializer_list<uint16_t> words) {
        for (uint16_t w : words) { f.PutWord(at, w); at += 2; }
    };
    const uint32_t C = 0x06004800u, A = 0x06004900u;
    place(C, { 0xC703,    // mova @(12,pc),r0   -> C+0x10, in the partition run from
               0xD103,    // mov.l @(12,pc),r1  -> the literal at C+0x10
               0x3100,    // cmp/eq r0,r1
               0x8901,    // bt C+0xC
               0x2142,    // mov.l r4,@r1
               0x0009,    // nop
               0x000B,    // rts
               0x0009 }); //  nop
    f.PutWord(C + 0x10, 0x2600);
    f.PutWord(C + 0x12, 0x4810);   // 0x26004810
    place(A, { 0xE407, 0x4F22, 0xBF7C, 0x0009, 0x4F26, 0x000B, 0x0009 });   // mov #7,r4; bsr C
    ++f.backend.serial;
    f.Idle();
    auto show = [&](const char* entry) {
        f.TypeInto("entry", entry);
        const uint32_t want = static_cast<uint32_t>(std::strtoul(entry, nullptr, 16));
        const bool ok = f.PumpUntil([&] {
            return !f.panel.Pending() && f.panel.Shown() && f.panel.Shown()->requestedEntry == want &&
                   !f.panel.ShownStale();
        });
        if (!ok) f.Dump();
        return f.Shown().Text();
    };
    CHECK(Has(show("06004800"), "param_1"));      // canonical: takes the argument
    CHECK(Has(show("06004900"), "func_0x06004800(7);"));
    show("26004800");                              // through the mirror: takes none
    CHECK(!Has(f.Shown().Text(), "param_1"));
    CHECK(Has(show("06004900"), "func_0x06004800(7);"));
}

// Review of 37a0535, finding 2: Set Function Start Here records the start canonical and asks for
// the new function in the mirror the shown one runs in.
void TestFunctionStartKeepsMirror()
{
    Fixture f;
    f.Idle();
    f.TypeInto("entry", "26004000");
    CHECK(f.PumpUntil([&] { return !f.panel.Pending() && f.Shown().requestedEntry == 0x26004000u; }));
    const int line = f.LineContaining("func_0x06005000");
    CHECK(line >= 0);
    const uint32_t at = f.Shown().lines[static_cast<size_t>(line)].firstAddr;
    CHECK(at > dts::kFixture1Caller && at < 0x06005000u);   // canonical
    f.LineMenu(line, "Set Function Start Here");
    CHECK(f.bounds.Entries().count(at) == 1);
    f.Idle();
    CHECK(f.panel.WantedEntry() == (at | 0x20000000u));
}

// Review of the control-flow criterion: uncertainty is shown, never left to look like a complete
// function. An entry whose flow never reaches the address it was found for (the finder kept a
// symbol whose switch it cannot follow), and the engine's unresolved jump, are warned about. A
// shared epilogue below the entry is part of the body: its notes are listed and a PC in it has not
// left the function; a PC in the gap between has.
void TestFlowUncertaintyShown()
{
    const uint32_t sw = 0x06009800u, epi = 0x06009900u, shared = 0x06009910u;
    auto place = [&](Fixture& f) {
        f.PutWord(sw - 4, 0x000B);   // rts: the end of whatever precedes
        f.PutWord(sw - 2, 0x0009);
        const uint16_t swWords[] = { 0x452B, 0x0009, 0x000B, 0x0009, 0x7001, 0x7001, 0x7001, 0x7001, 0x000B, 0x0009 };
        for (size_t i = 0; i < sizeof swWords / sizeof swWords[0]; ++i) f.PutWord(sw + 2 * uint32_t(i), swWords[i]);
        const uint16_t epiWords[] = { 0x4F26, 0x000B, 0x0009 };   // lds.l @r15+,pr ; rts ; nop
        for (size_t i = 0; i < 3; ++i) f.PutWord(epi + 2 * uint32_t(i), epiWords[i]);
        const uint16_t sharedWords[] = { 0x4F22, 0xAFF5, 0x0009 };   // sts.l pr,@-r15 ; bra epi ; nop
        for (size_t i = 0; i < 3; ++i) f.PutWord(shared + 2 * uint32_t(i), sharedWords[i]);
        f.names[sw] = "dispatch";
        f.names[shared] = "shared";
        ++f.namesGeneration;
        ++f.backend.serial;
    };
    auto any = [](const std::vector<std::string>& lines, const char* what) {
        for (const std::string& l : lines)
            if (Has(l, what)) return true;
        return false;
    };
    {
        Fixture f;
        place(f);
        f.panel.ShowFunctionContaining(0, sw + 0x0C);
        CHECK(f.WaitShown("dispatch"));
        f.Idle(2);
        const std::vector<std::string> u = f.panel.Uncertainties();
        for (const std::string& l : u) std::printf("  amber: %s\n", l.c_str());
        CHECK(f.Shown().entry == sw);
        CHECK(any(u, "0x0600980C is not reached from this entry: flow may stop short of it"));
        CHECK(any(u, "Entry: an rts before 0x0600980C"));
        CHECK(any(u, "0x06009800: Treating indirect jump as call"));

        f.TypeInto("entry", "06009910");
        CHECK(f.WaitShown("shared"));
        CHECK(f.panel.Uncertainties().empty());   // a typed entry is no guess
        f.comments[epi + 4] = "slot";     // the epilogue's delay slot: no line of its own
        f.comments[0x06009908u] = "gap";  // between the epilogue and the entry: not the function's
        f.Idle(2);
        const std::vector<std::string>& unplaced = f.panel.UnplacedNotes();
        for (const std::string& n : unplaced) std::printf("  unplaced: %s\n", n.c_str());
        CHECK(unplaced == std::vector<std::string>{ "0x06009904: slot" });
        CHECK(f.panel.Uncertainties().empty());   // a typed entry has no address to reach
    }
    {
        // A hardware note is an amber line too: reading a write-only register back shows a value
        // the hardware never returns.
        Fixture f;
        const uint32_t fn = 0x06009A00u;
        const uint16_t words[] = { 0x000B, 0x0009,   // rts ; nop: the end of whatever precedes
                                   0xD102,           // mov.l @(8,pc),r1  -> VDP1_FBCR
                                   0x2141,           // mov.w r4,@r1
                                   0x6011,           // mov.w @r1,r0      reads it back
                                   0x000B, 0x0009, 0x0009,
                                   0x25D0, 0x0002 }; // the literal
        for (size_t i = 0; i < sizeof words / sizeof words[0]; ++i) f.PutWord(fn - 4 + 2 * uint32_t(i), words[i]);
        f.names[fn] = "fbcr";
        ++f.namesGeneration;
        ++f.backend.serial;
        f.panel.ShowFunctionContaining(0, fn);
        CHECK(f.WaitShown("fbcr"));
        f.Idle(2);
        const std::vector<std::string> u = f.panel.Uncertainties();
        for (const std::string& l : u) std::printf("  amber: %s\n", l.c_str());
        CHECK(any(u, "0x06009A04: reads VDP1_FBCR, which is write-only"));

        // An explanation is not amber: it goes on its line, like a user's note.
        const uint32_t mir = 0x06009A40u;
        const uint16_t mirWords[] = { 0x000B, 0x0009,
                                      0xD101,              // mov.l @(4,pc),r1  -> SMPC_SF, 128 bytes up
                                      0x6010,              // mov.b @r1,r0
                                      0x000B, 0x0009,
                                      0x2010, 0x00E3 };    // the literal
        for (size_t i = 0; i < sizeof mirWords / sizeof mirWords[0]; ++i) f.PutWord(mir - 4 + 2 * uint32_t(i), mirWords[i]);
        f.names[mir] = "mirrored";
        ++f.namesGeneration;
        ++f.backend.serial;
        f.panel.ShowFunctionContaining(0, mir);
        CHECK(f.WaitShown("mirrored"));
        f.Idle(2);
        for (const std::string& l : f.panel.Uncertainties()) CHECK(!Has(l, "through a mirror"));
        CHECK(any(f.panel.LineNotes(), "0x201000E3 is SMPC_SF through a mirror (shown by address)"));

        // The user's volatile memory is engine input: a change asks again, a bad list changes nothing.
        const uint64_t before = f.panel.Submissions();
        CHECK(f.panel.SetVolatileMemory("0x26001000-0x26001003"));
        f.Idle(2);
        CHECK(f.WaitShown("mirrored"));
        CHECK(f.panel.Submissions() > before);
        std::string error;
        CHECK(!f.panel.SetVolatileMemory("0x10-0x08", &error) && !error.empty());
        CHECK(f.panel.VolatileMemory() == "0x26001000-0x26001003");
        CHECK(!f.panel.SetVolatileMemory(std::string(4000, '1'), &error) && Has(error, "longer"));
        CHECK(f.panel.VolatileMemory() == "0x26001000-0x26001003");
    }
    {
        // A signature recovered under one volatile list is not lent under another (review of
        // 960051e). The callee writes 0 to a mailbox and returns its argument unless the mailbox
        // still reads 0: with the mailbox ordinary memory that folds away and so does the argument.
        Fixture f;
        const uint32_t callee = 0x06009B00u, caller = 0x06009B40u;
        const uint16_t calleeWords[] = { 0x000B, 0x0009,
                                         0xD104,           // mov.l @(16,pc),r1  -> 0x26001000
                                         0xE000,           // mov #0,r0
                                         0x2102,           // mov.l r0,@r1
                                         0x6012,           // mov.l @r1,r0
                                         0x2008,           // tst r0,r0
                                         0x8900,           // bt done
                                         0x6043,           // mov r4,r0
                                         0x000B, 0x0009,   // done: rts ; nop
                                         0x0009,
                                         0x2600, 0x1000 }; // the literal
        for (size_t i = 0; i < sizeof calleeWords / sizeof calleeWords[0]; ++i) f.PutWord(callee - 4 + 2 * uint32_t(i), calleeWords[i]);
        const uint16_t callerWords[] = { 0x000B, 0x0009,
                                         0x4F22,           // sts.l pr,@-r15
                                         0xE401,           // mov #1,r4
                                         0xBFDC,           // bsr callee
                                         0x0009,
                                         0x4F26,           // lds.l @r15+,pr
                                         0x000B, 0x0009 };
        for (size_t i = 0; i < sizeof callerWords / sizeof callerWords[0]; ++i) f.PutWord(caller - 4 + 2 * uint32_t(i), callerWords[i]);
        f.names[callee] = "review_callee";
        f.names[caller] = "review_caller";
        ++f.namesGeneration;
        ++f.backend.serial;

        f.panel.ShowFunctionContaining(0, callee);
        CHECK(f.WaitShown("4 review_callee("));   // its own signature line
        f.Idle(2);
        std::printf("%s", f.Shown().Text().c_str());
        CHECK(Has(f.Shown().Text(), "review_callee(void)"));   // the argument folded away
        CHECK(!f.panel.ShownStale());

        // The shown answer is stale as soon as the list changes, whether or not anything is asked.
        CHECK(f.panel.SetVolatileMemory("26001000-26001003"));
        CHECK(f.panel.ShownStale());

        f.panel.ShowFunctionContaining(0, caller);
        CHECK(f.WaitShown("4 review_caller("));
        f.Idle(2);
        std::printf("%s", f.Shown().Text().c_str());
        CHECK(!Has(f.Shown().Text(), "review_callee();"));     // the old signature is not lent

        // Recovered again under the new list, the callee takes its argument, and the caller passes it.
        f.panel.ShowFunctionContaining(0, callee);
        CHECK(f.WaitShown("4 review_callee("));   // its own signature line
        f.Idle(2);
        std::printf("%s", f.Shown().Text().c_str());
        CHECK(Has(f.Shown().Text(), "param_1"));
        f.panel.ShowFunctionContaining(0, caller);
        CHECK(f.WaitShown("4 review_caller("));
        f.Idle(2);
        std::printf("%s", f.Shown().Text().c_str());
        CHECK(Has(f.Shown().Text(), "review_callee(1)"));
    }
    {
        // State and caching across volatile lists (consolidated review). The callee alone: its
        // mailbox write and read fold away while the mailbox is ordinary memory, and stay when it
        // is volatile.
        Fixture f;
        const uint32_t callee = 0x06009B00u;
        const uint16_t calleeWords[] = { 0x000B, 0x0009, 0xD104, 0xE000, 0x2102, 0x6012, 0x2008, 0x8900, 0x6043,
                                         0x000B, 0x0009, 0x0009, 0x2600, 0x1000 };
        for (size_t i = 0; i < sizeof calleeWords / sizeof calleeWords[0]; ++i) f.PutWord(callee - 4 + 2 * uint32_t(i), calleeWords[i]);
        f.names[callee] = "mailbox_fn";
        ++f.namesGeneration;
        ++f.backend.serial;
        auto keeps = [&] { return Has(f.Shown().Text(), "iVar1 = Ram26001000;"); };

        // The list changes while the first request is outstanding: whatever comes back for the
        // old list is not installed as the answer, and the one shown is the new list's.
        f.panel.ShowFunctionContaining(0, callee);
        CHECK(f.panel.SetVolatileMemory("26001000-26001003"));
        CHECK(f.WaitShown("mailbox_fn"));
        f.Idle(4);
        CHECK(f.WaitShown("mailbox_fn"));
        CHECK(keeps());
        CHECK(!f.panel.ShownStale());

        // Back to no list: asked again, the mailbox folds.
        CHECK(f.panel.SetVolatileMemory(""));
        f.Idle(4);
        CHECK(f.WaitShown("mailbox_fn"));
        CHECK(!keeps());
        // ...and to the list again: the earlier answer comes from the cache, not the engine.
        const uint64_t submissions = f.panel.Submissions(), hits = f.panel.CacheHits();
        CHECK(f.panel.SetVolatileMemory("26001000-26001003"));
        f.Idle(4);
        CHECK(f.WaitShown("mailbox_fn"));
        CHECK(keeps());
        CHECK(f.panel.Submissions() == submissions && f.panel.CacheHits() == hits + 1);
        CHECK(!f.panel.ShownStale());
    }
    {
        // An entry the rts scan found is a guess, and the panel says so (review of 7eaa935).
        Fixture f;
        CHECK(f.WaitShown("+ 1"));
        f.Idle(2);
        const std::vector<std::string> u = f.panel.Uncertainties();
        CHECK(any(u, "Entry: guessed from the previous rts"));
    }
    {
        Fixture f;
        place(f);
        f.kind = DecompViewKind::LiveRunning;
        f.trackSerial = false;
        f.regs[0].pc = shared + 2;
        auto tick = [&](int frames) {
            for (int i = 0; i < frames; ++i)
            {
                ++f.frame;
                ++f.fixedSerial;
                f.Idle();
            }
        };
        CHECK(f.WaitShown("shared"));
        f.panel.SetAutoRefresh(true);
        const uint64_t c = f.panel.Captures();
        f.regs[0].pc = epi + 2;   // in the epilogue, below the entry: still inside
        tick(DecompilerPanel::kAutoRefreshFrames + 1);
        CHECK(f.panel.Captures() == c);
        // Between: outside, refreshed for once. What is found for it does not hold it either, and
        // the PC parked there is not refreshed for again every interval.
        f.regs[0].pc = 0x06009908u;
        tick(DecompilerPanel::kAutoRefreshFrames + 1);
        CHECK(f.panel.Captures() == c + 1);
        while (f.panel.Pending()) tick(1);
        tick(3 * DecompilerPanel::kAutoRefreshFrames);
        CHECK(f.panel.Captures() == c + 1);
        f.regs[0].pc = shared + 2;   // moving on is refreshed for, back to the shared function
        tick(DecompilerPanel::kAutoRefreshFrames + 1);
        CHECK(f.panel.Captures() == c + 2);
        CHECK(f.WaitShown("shared"));
    }
}

// Review of c426c2e, finding 1: with Follow PC and auto-refresh, the PC moving into another mirror
// of the shown function has left it -- the mirror is another question.
void TestAutoRefreshFollowsMirror()
{
    Fixture f;
    f.kind = DecompViewKind::LiveRunning;
    f.trackSerial = false;
    auto tick = [&](int frames) {
        for (int i = 0; i < frames; ++i)
        {
            ++f.frame;
            ++f.fixedSerial;
            f.Idle();
        }
    };
    CHECK(f.WaitShown("+ 1"));
    CHECK(f.Shown().requestedEntry == dts::kFixture1Caller);
    f.panel.SetAutoRefresh(true);
    const uint64_t c = f.panel.Captures();
    f.regs[0].pc = dts::kFixture1AddOne | 0x20000000u;   // the same instruction, cache-through
    tick(DecompilerPanel::kAutoRefreshFrames + 1);
    CHECK(f.panel.Captures() == c + 1);
    while (f.panel.Pending()) tick(1);
    CHECK(f.panel.WantedEntry() == (dts::kFixture1Caller | 0x20000000u));
    CHECK(f.Shown().requestedEntry == (dts::kFixture1Caller | 0x20000000u));
    tick(90);                                              // and stays put there
    CHECK(f.panel.Captures() == c + 1);
}

// Review of c426c2e, finding 2: a halt hint belongs to the mirror the output was decompiled at.
// Output pinned at the canonical entry gets no hint for a halt at the cache-through mirror.
void TestHaltHintMatchesMirror()
{
    Fixture f;
    f.kind = DecompViewKind::LivePaused;
    f.PutWord(dts::kFixture1Jsr - 2, 0x6042);   // mov.l @r4,r0: the jsr target is computed
    ++f.backend.serial;
    f.regs[0].pc = dts::kFixture1Jsr;
    f.regs[0].r[0] = dts::kFixture1Callee;
    CHECK(f.WaitShown("(*(code *)"));
    f.Idle(2);
    CHECK(!f.panel.HintText().empty());
    f.TypeInto("entry", "06004000");             // pinned canonical
    CHECK(f.PumpUntil([&] { return !f.panel.Pending() && !f.panel.ShownStale(); }));
    f.regs[0].pc = dts::kFixture1Jsr | 0x20000000u;
    ++f.frame;
    f.Idle();
    CHECK(f.PumpUntil([&] { return !f.panel.Pending() && !f.panel.ShownStale(); }));
    f.Idle(2);
    CHECK(f.Shown().requestedEntry == dts::kFixture1Caller);
    CHECK(f.panel.HintText().empty());
    // Review of a92a300: a call the output resolved suppresses the hint only for the exact address
    // called. The literal names 06005000; the CPU about to call 26005000 is not that call.
    f.PutWord(dts::kFixture1Jsr - 2, 0xD003);   // the literal load again: the call prints as resolved
    ++f.backend.serial;
    f.regs[0].pc = dts::kFixture1Jsr;
    f.regs[0].r[0] = dts::kFixture1Callee;
    ++f.frame;
    CHECK(f.WaitShown("func_0x06005000("));
    f.Idle(2);
    CHECK(f.panel.HintText().empty());           // exactly the resolved call
    f.regs[0].r[0] = dts::kFixture1Callee | 0x20000000u;
    ++f.frame;
    f.Idle();
    CHECK(f.PumpUntil([&] { return !f.panel.Pending() && !f.panel.ShownStale(); }));
    f.Idle(2);
    CHECK(!f.panel.HintText().empty());
    f.PutWord(dts::kFixture1Jsr - 2, 0x6042);
    ++f.backend.serial;
    f.regs[0].r[0] = dts::kFixture1Callee;
    f.regs[0].pc = dts::kFixture1Jsr | 0x20000000u;
    ++f.frame;
    CHECK(f.PumpUntil([&] { return !f.panel.Pending() && !f.panel.ShownStale(); }));
    f.TypeInto("entry", "26004000");             // the output for that mirror: the hint is its own
    CHECK(f.PumpUntil([&] { return !f.panel.Pending() && f.Shown().requestedEntry == 0x26004000u; }));
    f.Idle(2);
    CHECK(!f.panel.HintText().empty());
}

// A1 non-memory invalidations: a rename re-runs the request (names are engine input), a BIOS image
// with other bytes is a new key, a function-start correction asks for the new entry.
void TestRenameBiosAndBounds()
{
    Fixture f;
    f.panel.ShowFunctionContaining(0, dts::kFixture1Caller);
    CHECK(f.WaitShown("func_0x06005000"));
    uint64_t n = f.panel.Submissions();

    f.names[dts::kFixture1Callee] = "read_word";
    ++f.namesGeneration;
    CHECK(f.WaitShown("read_word("));
    CHECK(f.panel.Submissions() == n + 1);

    n = f.panel.Submissions();
    const SnapshotKey k0 = f.Held();
    f.bios = BiosImage::Make(std::vector<uint8_t>(0x1000, 0x11));
    f.Idle();
    const SnapshotKey k1 = f.Held();
    CHECK(k1.regionSetHash != k0.regionSetHash);
    f.bios = BiosImage::Make(std::vector<uint8_t>(0x1000, 0x22));   // same size, other bytes
    f.Idle();
    CHECK(f.Held().regionSetHash != k1.regionSetHash);
    CHECK(f.WaitShown());
    CHECK(f.panel.Submissions() >= n + 1);

    // Set Function Start Here, from the context menu, on the line that calls.
    const int line = f.LineContaining("read_word(");
    CHECK(line >= 0);
    const uint32_t at = f.Shown().lines[static_cast<size_t>(line)].firstAddr;
    CHECK(at != 0 && at != dts::kFixture1Caller);
    f.Take();
    f.LineMenu(line, "Set Function Start Here");
    CHECK(f.Take().boundsChanged);
    CHECK(f.bounds.Entries().count(at) == 1);
    CHECK(f.WaitShown());
    CHECK(f.Shown().entry == at && f.panel.WantedEntry() == at);
}

// A1, running emulator: a key that moves every frame never triggers a capture by itself; the
// output is kept with the stale badge. Refresh captures now; the opt-in auto-refresh is debounced
// and waits for the followed PC to leave the function. Pausing re-requests at once.
void TestRunningLive()
{
    Fixture f;
    f.kind = DecompViewKind::LiveRunning;
    f.trackSerial = false;
    auto tick = [&](int frames) {
        for (int i = 0; i < frames; ++i)
        {
            ++f.frame;
            ++f.fixedSerial;
            f.Idle();
        }
    };
    CHECK(f.WaitShown("+ 1"));   // the first look: one capture
    CHECK(f.panel.Captures() == 1);
    tick(90);
    CHECK(f.panel.Captures() == 1 && f.panel.Submissions() == 1);
    CHECK(f.panel.ShownStale() && f.panel.ShownOutdatedByView());   // "stale: frame N"

    f.ClickItem("refresh");
    const uint64_t asked = f.frame;
    CHECK(f.panel.Captures() == 2);
    // The answer is for the frame captured; it installs although the emulator has moved on since,
    // and is shown as what it is: frame 'asked', stale.
    while (f.panel.Pending())
    {
        tick(1);
        std::this_thread::yield();
    }
    CHECK(f.Shown().key.frameNumber == asked);
    tick(2);
    CHECK(f.panel.ShownOutdatedByView());

    // Auto-refresh: the PC stays in the function shown -- no capture however long it runs.
    f.panel.SetAutoRefresh(true);
    tick(120);
    CHECK(f.panel.Captures() == 2);
    // The PC leaves: one capture after the debounce, then the new function.
    f.regs[0].pc = dts::kFixture1Callee + 2;
    tick(DecompilerPanel::kAutoRefreshFrames + 1);
    CHECK(f.panel.Captures() == 3);
    while (f.panel.Pending()) tick(1);
    CHECK(f.Shown().entry == dts::kFixture1Callee);
    // Leaving again and again is still at most one capture per debounce period. Each frame waits
    // for the engine to finish (without drawing a frame), so every frame could have refreshed.
    const uint64_t c = f.panel.Captures();
    for (int i = 0; i < 90; ++i)
    {
        f.regs[0].pc = (i & 1) ? dts::kFixture1AddOne : dts::kFixture1Callee + 2;
        tick(1);
        const auto t0 = std::chrono::steady_clock::now();
        while (f.runner.Busy() && dts::MsSince(t0) < 30000.0) std::this_thread::yield();
    }
    std::printf("auto-refresh over 90 frames of a PC leaving every other frame: %llu captures\n",
                static_cast<unsigned long long>(f.panel.Captures() - c));
    CHECK(f.panel.Captures() - c >= 2);   // it does refresh
    CHECK(f.panel.Captures() - c <= 90 / DecompilerPanel::kAutoRefreshFrames + 1);

    // Paused: the view is followed at once.
    f.panel.SetAutoRefresh(false);
    while (f.panel.Pending()) tick(1);
    const uint64_t c2 = f.panel.Captures();
    f.kind = DecompViewKind::LivePaused;
    ++f.frame;
    f.Idle();
    CHECK(f.panel.Captures() == c2 + 1);
    f.Idle(10);
    CHECK(f.panel.Captures() == c2 + 1);   // and only while it moves
}

// Recorded frames: a frame shown before is served from the LRU under its full key -- the engine is
// not asked again -- while an edit to it, or another recording, is a different key.
void TestScrubCache()
{
    Fixture f;
    f.kind = DecompViewKind::Scrub;
    f.trackSerial = false;
    f.fixedSerial = 0;     // a recorded frame as recorded
    f.sourceId = 1000;     // the recording (App: stable while the recording is unchanged)
    f.panel.ShowFunctionContaining(0, dts::kFixture1Caller);
    f.frame = 50;
    CHECK(f.WaitShown("+ 1"));
    const uint64_t n = f.panel.Submissions();

    f.frame = 51;          // another recorded frame, other bytes
    f.backend.hw[dts::kFixture1AddOne + 1 - kHw] = 0x03;
    CHECK(f.WaitShown("+ 3"));
    CHECK(f.panel.Submissions() == n + 1);

    f.frame = 50;          // back: served from the cache, nothing submitted
    f.backend.hw[dts::kFixture1AddOne + 1 - kHw] = 0x01;
    f.Idle();
    CHECK(f.panel.CacheHits() == 1 && f.panel.Submissions() == n + 1);
    CHECK(Has(f.Shown().Text(), "+ 1") && !f.panel.ShownStale());

    f.fixedSerial = 5;     // an edit to frame 50: another key, asked afresh
    f.backend.hw[dts::kFixture1AddOne + 1 - kHw] = 0x04;
    CHECK(f.WaitShown("+ 4"));
    CHECK(f.panel.Submissions() == n + 2);

    f.fixedSerial = 0;
    f.sourceId = 1001;     // frame 50 of another recording (after Play From Here): not the cached one
    f.backend.hw[dts::kFixture1AddOne + 1 - kHw] = 0x05;
    CHECK(f.WaitShown("+ 5"));
    CHECK(f.panel.Submissions() == n + 3);

    // Bounded: after kCacheSize other frames, the first is gone.
    for (uint64_t i = 0; i < DecompilerPanel::kCacheSize; ++i)
    {
        f.frame = 200 + i;
        CHECK(f.PumpUntil([&] { return !f.panel.Pending() && f.Shown().key.frameNumber == f.frame; }));
    }
    const uint64_t hits = f.panel.CacheHits(), subs = f.panel.Submissions();
    f.frame = 50;
    f.Idle();
    CHECK(f.panel.CacheHits() == hits && f.panel.Submissions() == subs + 1);
    CHECK(f.WaitShown());
}

// A runner that cannot decompile (a second one is born disabled) is reported, without retrying;
// an entry over budget is reported "too large" and asked again only by Refresh.
void TestDisabledAndBudget()
{
    {
        DecompilerRunner first(gConfig);
        DecompilerRunner second(gConfig);   // born disabled: only one engine thread per process
        CHECK(second.Disabled());
        MockBackend backend;
        std::vector<uint8_t> image;
        dts::PlaceFixture1(image);
        std::copy(image.begin(), image.end(), backend.hw.begin());
        DecompilerPanel panel;
        std::map<uint32_t, std::string> names;
        ImGuiHarness h([&] {
            DecompilerView v;
            v.backend = &backend;
            v.kind = DecompViewKind::Static;
            v.sourceId = 1;
            v.deriveSerial = backend.serial;
            v.haveRegs[0] = true;
            v.regs[0].pc = dts::kFixture1AddOne;
            DecompilerInputs in;
            in.names = &names;
            panel.Update(v, second, in, true);
            DecompilerPanel::Request r;
            panel.Draw(nullptr, v, second, in, r);
        });
        for (int i = 0; i < 30; ++i) h.Frame(ImVec2(1270, 710), false);
        CHECK(!panel.Pending() && panel.Submissions() == 0 && !panel.Shown());
        CHECK(panel.Captures() == 1);   // and the snapshot was taken once, not every frame
    }
    {
        RunnerOptions o;
        o.budgetSeconds = 0.05;
        Fixture f(o);
        std::vector<uint8_t> image(f.backend.hw.begin(), f.backend.hw.end());
        dts::PlaceDiamonds(image, 1000);
        std::copy(image.begin(), image.end(), f.backend.hw.begin());
        ++f.backend.serial;
        f.Idle(3);
        f.TypeInto("entry", "06010000");
        CHECK(f.PumpUntil([&] { return f.panel.Shown() && !f.panel.Pending(); }));
        CHECK(f.Shown().status == DecompileStatus::TooLarge);
        std::printf("over budget: %s\n", f.Shown().error.c_str());
        const uint64_t n = f.panel.Submissions();
        f.Idle(30);
        CHECK(f.panel.Submissions() == n);
        f.ClickItem("refresh");
        CHECK(f.panel.Submissions() == n + 1);
        CHECK(f.PumpUntil([&] { return !f.panel.Pending(); }));
    }
}


// Review round 1, finding 2: an answer that finished while nobody was looking is judged against
// what is on screen NOW. Shown A (+ 1); B (+ 2) is asked and completes without a UI frame; the
// memory moves on to C (+ 3); the next Update must take C first and discard B, keeping A, stale.
void TestCompletedAnswerJudgedAgainstCurrentView()
{
    Fixture f;
    f.panel.ShowFunctionContaining(0, dts::kFixture1Caller);
    CHECK(f.WaitShown("+ 1"));
    f.gate.Close();
    f.backend.hw[dts::kFixture1AddOne + 1 - kHw] = 0x02;   // B
    ++f.backend.serial;
    f.Idle();
    CHECK(f.gate.WaitHeld());
    f.gate.Open();
    const auto t0 = std::chrono::steady_clock::now();
    while (f.runner.Busy() && dts::MsSince(t0) < 30000.0) std::this_thread::yield();   // B done, unpolled
    CHECK(!f.runner.Busy());
    f.gate.Close();
    f.backend.hw[dts::kFixture1AddOne + 1 - kHw] = 0x03;   // C
    ++f.backend.serial;
    const uint64_t discarded = f.panel.Discarded();
    f.Idle();   // one Update
    CHECK(Has(f.Shown().Text(), "+ 1"));                    // A retained, not replaced by B
    CHECK(f.panel.ShownStale());
    CHECK(f.panel.Discarded() == discarded + 1);            // B judged against C, and dropped
    CHECK(f.panel.Pending());
    CHECK(f.gate.WaitHeld());
    f.gate.Open();
    CHECK(f.WaitShown("+ 3"));
    CHECK(!Has(f.Shown().Text(), "+ 2"));

    // The same with a rename instead of an edit: an answer made under the old names, finished
    // unpolled, is not installed in the frame the names change (names are engine input, A1).
    f.gate.Close();
    f.backend.hw[dts::kFixture1AddOne + 1 - kHw] = 0x04;   // D
    ++f.backend.serial;
    f.Idle();
    CHECK(f.gate.WaitHeld());
    f.gate.Open();
    const auto t1 = std::chrono::steady_clock::now();
    while (f.runner.Busy() && dts::MsSince(t1) < 30000.0) std::this_thread::yield();
    f.gate.Close();
    f.names[dts::kFixture1Callee] = "read_word";
    ++f.namesGeneration;
    const uint64_t discarded2 = f.panel.Discarded();
    f.Idle();
    CHECK(Has(f.Shown().Text(), "+ 3"));                     // not D under the old names
    CHECK(f.panel.Discarded() == discarded2 + 1);
    CHECK(f.gate.WaitHeld());
    f.gate.Open();
    CHECK(f.WaitShown("read_word("));
    CHECK(Has(f.Shown().Text(), "+ 4"));

    // And while the window is hidden (review round 2): a hidden panel takes no snapshot, so the
    // view's own identity must stop the stale answer. E (+ 5) completes unpolled, the memory moves
    // to F (+ 6), and a hidden Update discards E -- copying and asking nothing -- so reopening
    // still shows D, stale, and asks for F.
    f.gate.Close();
    f.backend.hw[dts::kFixture1AddOne + 1 - kHw] = 0x05;   // E
    ++f.backend.serial;
    f.Idle();
    CHECK(f.gate.WaitHeld());
    f.gate.Open();
    const auto t2 = std::chrono::steady_clock::now();
    while (f.runner.Busy() && dts::MsSince(t2) < 30000.0) std::this_thread::yield();
    f.gate.Close();
    f.backend.hw[dts::kFixture1AddOne + 1 - kHw] = 0x06;   // F
    ++f.backend.serial;
    f.visible = false;
    const uint64_t discarded3 = f.panel.Discarded(), captures3 = f.panel.Captures(), asked3 = f.panel.Submissions();
    f.Idle();
    CHECK(Has(f.Shown().Text(), "+ 4"));                     // D kept, E not installed
    CHECK(f.panel.Discarded() == discarded3 + 1);
    CHECK(f.panel.Captures() == captures3 && f.panel.Submissions() == asked3);   // hidden: no work
    CHECK(!f.panel.Pending());
    f.visible = true;
    f.Idle();
    CHECK(f.panel.Captures() == captures3 + 1 && f.panel.Submissions() == asked3 + 1);
    CHECK(Has(f.Shown().Text(), "+ 4") && f.panel.ShownStale());
    CHECK(f.gate.WaitHeld());
    f.gate.Open();
    CHECK(f.WaitShown("+ 6"));
    CHECK(!Has(f.Shown().Text(), "+ 5"));

    // A running emulator's held snapshot lags by design: hidden, its answer is still installed
    // (shown with the stale badge), exactly as when visible.
    f.kind = DecompViewKind::LiveRunning;
    f.trackSerial = false;
    f.fixedSerial = 1000;
    f.ClickItem("refresh");
    CHECK(f.WaitShown());
    f.gate.Close();
    f.ClickItem("refresh");
    CHECK(f.gate.WaitHeld());
    const uint64_t g = f.panel.PendingGeneration();
    f.gate.Open();
    while (f.runner.Busy()) std::this_thread::yield();
    ++f.frame;
    ++f.fixedSerial;
    f.visible = false;
    f.Idle();
    CHECK(!f.panel.Pending() && f.Shown().generation == g);
    f.visible = true;

    // Review round 3: the lag excuses frames and writes, not a new BIOS image -- that changes the
    // region set. Adding, replacing and removing the image each discard a hidden finished answer,
    // keep what is shown, do no hidden work, and ask once again on reopening.
    const std::shared_ptr<const BiosImage> images[] = { BiosImage::Make(std::vector<uint8_t>(0x1000, 0x11)),
                                                         BiosImage::Make(std::vector<uint8_t>(0x1000, 0x22)),
                                                         nullptr };
    for (const auto& image : images)
    {
        const uint64_t shownGen = f.Shown().generation;
        f.gate.Close();
        f.ClickItem("refresh");
        CHECK(f.gate.WaitHeld());
        f.gate.Open();
        while (f.runner.Busy()) std::this_thread::yield();
        f.bios = image;
        ++f.frame;
        ++f.fixedSerial;
        f.visible = false;
        const uint64_t discarded4 = f.panel.Discarded(), captures4 = f.panel.Captures(),
                       asked4 = f.panel.Submissions();
        f.Idle();
        CHECK(f.Shown().generation == shownGen);               // the old output stays
        CHECK(f.panel.Discarded() == discarded4 + 1);          // the answer for the old image goes
        CHECK(!f.panel.Pending());
        CHECK(f.panel.Captures() == captures4 && f.panel.Submissions() == asked4);   // hidden: no work
        f.visible = true;
        f.Idle();
        CHECK(f.panel.Captures() == captures4 + 1 && f.panel.Submissions() == asked4 + 1);
        CHECK(f.WaitShown());
        CHECK(f.Shown().generation != shownGen);
        f.Idle(3);
        CHECK(f.panel.Submissions() == asked4 + 1);            // once
    }
}

// Windows manual run: after reconnecting, the first look came before the emulator's first register
// frame, so Follow PC had no PC -- and a running emulator never captured again until Refresh.
void TestRunningLiveRegistersArrive()
{
    Fixture f;
    f.kind = DecompViewKind::LiveRunning;
    f.trackSerial = false;
    f.haveRegs[0] = f.haveRegs[1] = false;
    auto tick = [&](int frames) {
        for (int i = 0; i < frames; ++i)
        {
            ++f.frame;
            ++f.fixedSerial;
            f.Idle();
        }
    };
    tick(3);
    CHECK(f.panel.Captures() == 1 && f.panel.Submissions() == 0);   // the first look: nothing to follow
    tick(10);
    CHECK(f.panel.Captures() == 1);                                   // and no capture every frame
    f.haveRegs[1] = true;                                             // the other CPU's are no use
    tick(10);
    CHECK(f.panel.Captures() == 1);
    f.haveRegs[0] = true;
    CHECK(f.WaitShown("+ 1"));                                        // once, with the registers
    CHECK(f.panel.Captures() == 2 && f.panel.Submissions() == 1);
    tick(60);
    CHECK(f.panel.Captures() == 2);                                   // then running-live as usual
}

// Windows re-test: after Launch the new session's first PC was in the (unmapped) BIOS, so Follow PC
// found no function, and a running emulator never looked again until Refresh.
void TestRunningLiveUnansweredFollowRetries()
{
    Fixture f;
    f.kind = DecompViewKind::LiveRunning;
    f.trackSerial = false;
    f.regs[0].pc = 0x00001FF0u;
    auto tick = [&](int frames) {
        for (int i = 0; i < frames; ++i)
        {
            ++f.frame;
            ++f.fixedSerial;
            f.Idle();
        }
    };
    tick(3);
    CHECK(f.panel.Captures() == 1 && f.panel.Submissions() == 0);
    CHECK(Has(f.panel.EntryError(), "No function found"));
    tick(90);
    CHECK(f.panel.Captures() == 1);                     // the PC has not moved: nothing new to ask
    // The PC keeps moving inside the BIOS, every update: one capture per debounce interval, never
    // one per update (a 2 MiB copy each).
    const uint64_t c0 = f.panel.Captures();
    const int n = 3 * DecompilerPanel::kAutoRefreshFrames;
    for (int i = 0; i < n; ++i)
    {
        f.regs[0].pc = 0x00002000u + 2u * static_cast<uint32_t>(i);
        tick(1);
    }
    const uint64_t moved = f.panel.Captures() - c0;
    CHECK(moved >= 2 && moved <= 3);
    CHECK(f.panel.Submissions() == 0);
    const uint64_t c1 = f.panel.Captures();
    f.regs[0].pc = dts::kFixture1AddOne;
    tick(DecompilerPanel::kAutoRefreshFrames + 1);
    CHECK(f.panel.Captures() == c1 + 1);
    CHECK(f.WaitShown("+ 1"));
    tick(90);
    CHECK(f.panel.Captures() == c1 + 1 && f.panel.Submissions() == 1);   // answered: running-live as usual
}

// Review round 1, finding 3: on a running emulator, choosing, replacing or dropping the BIOS image
// is a user action -- each one captures and asks again, once, with the new region set.
void TestBiosChangeWhileRunning()
{
    Fixture f;
    f.kind = DecompViewKind::LiveRunning;
    f.trackSerial = false;
    auto tick = [&](int frames) {
        for (int i = 0; i < frames; ++i)
        {
            ++f.frame;
            ++f.fixedSerial;
            f.Idle();
        }
    };
    CHECK(f.WaitShown("+ 1"));   // the first look
    const uint64_t h0 = f.Held().regionSetHash;
    tick(10);
    CHECK(f.panel.Captures() == 1 && f.panel.Submissions() == 1);
    uint64_t prev = h0;
    const std::shared_ptr<const BiosImage> images[] = { BiosImage::Make(std::vector<uint8_t>(0x1000, 0x11)),
                                                         BiosImage::Make(std::vector<uint8_t>(0x1000, 0x22)),
                                                         nullptr };
    for (int i = 0; i < 3; ++i)
    {
        f.bios = images[i];
        tick(1);
        CHECK(f.panel.Captures() == static_cast<uint64_t>(2 + i));
        CHECK(f.panel.Submissions() == static_cast<uint64_t>(2 + i));
        CHECK(f.Held().regionSetHash != prev);
        prev = f.Held().regionSetHash;
        while (f.panel.Pending()) tick(1);
        tick(10);   // and nothing more while it keeps running
        CHECK(f.panel.Captures() == static_cast<uint64_t>(2 + i));
    }
    CHECK(prev == h0);   // no image again: the original region set
}

// macOS manual run: the entry box showed the last typed entry while another function -- reached
// from the Call Stack or Registers -- was on screen. It shows the shown function's entry, but
// never overwrites what the user is typing.
void TestEntryBoxFollowsShownFunction()
{
    Fixture f;
    CHECK(f.WaitShown("+ 1"));
    CHECK(std::string(f.panel.EntryText()) == "06004000");
    f.TypeInto("entry", "26005000");                 // a mirror: named canonical, run where typed
    CHECK(f.WaitShown());
    f.Idle(2);
    CHECK(f.Shown().entry == dts::kFixture1Callee);
    CHECK(std::string(f.panel.EntryText()) == "26005000");
    f.panel.ShowFunctionContaining(0, dts::kFixture1AddOne);   // as Call Stack / Registers do
    CHECK(f.WaitShown("+ 1"));
    f.Idle(2);
    CHECK(std::string(f.panel.EntryText()) == "06004000");

    // A draft being typed survives another function arriving; the box catches up once it is left.
    ImVec2 a, b;
    CHECK(f.panel.ItemRect("entry", a, b));
    f.h.Click(Centre(a, b));
    f.Idle(2);
    f.Key(ImGuiKey_End);
    ImGui::GetIO().AddInputCharacter('7');
    f.Idle();
    f.panel.ShowFunctionContaining(0, dts::kFixture1Callee);
    // Not necessarily from the cache: the caller's signature has been learned since, which the
    // callee's question includes.
    CHECK(f.PumpUntil([&] {
        return !f.panel.Pending() && f.panel.Shown() && f.panel.Shown()->entry == dts::kFixture1Callee;
    }));
    f.Idle(2);
    CHECK(f.Shown().entry == dts::kFixture1Callee);
    CHECK(std::string(f.panel.EntryText()) == "060040007");
    f.Key(ImGuiKey_Escape);
    f.Idle(2);
    CHECK(std::string(f.panel.EntryText()) == "06005000");
}

// Review round 1, finding 4: an entry is one to eight hex digits after an optional 0x -- no sign,
// no overflow that wraps into RAM.
void TestEntryParsing()
{
    uint32_t v = 0;
    CHECK(DecompilerPanel::ParseHexAddress("26004000", v) && v == 0x26004000u);
    CHECK(DecompilerPanel::ParseHexAddress("0x06004000", v) && v == 0x06004000u);
    CHECK(DecompilerPanel::ParseHexAddress("FFFFFFFF", v) && v == 0xFFFFFFFFu);
    for (const char* bad : { "106004000", "-F9FFC000", "+6004000", " 6004000", "6004000 ", "", "0x", "6004G00" })
        CHECK(!DecompilerPanel::ParseHexAddress(bad, v));

    Fixture f;
    CHECK(f.WaitShown("+ 1"));
    const uint64_t n = f.panel.Submissions();
    for (const char* bad : { "106004000", "-F9FFC000" })
    {
        f.TypeInto("entry", bad);
        f.Idle(3);
        CHECK(Has(f.panel.EntryError(), "hex address"));
        CHECK(f.panel.Submissions() == n);
    }
    f.TypeInto("entry", "26005000");
    CHECK(f.PumpUntil([&] { return !f.panel.Pending() && f.Shown().entry == dts::kFixture1Callee; }));
}

// Review round 1, finding 1: a runner whose worker cannot start does not take the application
// down: Start returns null with the reason, the engine claim is not left held (the next runner
// starts normally), and the panel shows the reason instead of nothing.
void TestRunnerStartFailure()
{
    std::string why;
    DecompilerRunner::SetThreadStartFailureForTests(true);
    std::unique_ptr<DecompilerRunner> failed = DecompilerRunner::Start(gConfig, why);
    DecompilerRunner::SetThreadStartFailureForTests(false);
    CHECK(!failed);
    CHECK(Has(why, "could not be started"));
    std::printf("start failure reported as: %s\n", why.c_str());
    {
        std::string none;
        std::unique_ptr<DecompilerRunner> next = DecompilerRunner::Start(gConfig, none);
        CHECK(next && !next->Disabled() && none.empty());
    }
    DecompilerPanel panel;
    ImGuiHarness h([&] {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(600, 200), ImGuiCond_Always);
        panel.DrawUnavailable(nullptr, why);
    });
    ApplyTheme(ImGui::GetStyle());
    LoadFonts(ImGui::GetIO());
    h.Settle();
    ImVec2 a, b;
    CHECK(panel.ItemRect("unavailable", a, b) && b.x > a.x);
}
}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::printf("usage: %s <scratch dir>\n", argv[0]);
        return 2;
    }
    dts::PrepareProcess();
    const auto start = std::chrono::steady_clock::now();
    const std::string scratch = argv[1];
    dts::RemoveTree(scratch);
    CHECK(sfe::MakeDirectory(scratch));
    gConfig = dts::Join(scratch, "config");
    CHECK(sfe::MakeDirectory(gConfig));
    try
    {
        RUN(TestLineClickNavigates());
        RUN(TestCpuSwitchKeepsOutput());
        RUN(TestHexWriteReRequests());
        RUN(TestInstallRule());
        RUN(TestSessionChangeDoesNotWait());
        RUN(TestEntriesAndNoRetryLoops());
        RUN(TestDelaySlotFlagged());
        RUN(TestRegisterHintAtHalt());
        RUN(TestUserCommentsOnLines());
        RUN(TestCalleePrototypesLent());
        RUN(TestRenameBiosAndBounds());
        RUN(TestPrototypeProvenance());
        RUN(TestUnparsedVolatileKept());
        RUN(TestFunctionSetChangeIsStale());
        RUN(TestFunctionSetKeysQuestions());
        RUN(TestCalleeHintsBelongToTheirSnapshot());
        RUN(TestRunningAfterScrubCaptures());
        RUN(TestHintsReResolve());
        RUN(TestCallStackHints());
        RUN(TestMirrorEntryIsKept());
        RUN(TestPrototypesPerMirror());
        RUN(TestFunctionStartKeepsMirror());
        RUN(TestAutoRefreshFollowsMirror());
        RUN(TestFlowUncertaintyShown());
        RUN(TestHaltHintMatchesMirror());
        RUN(TestRunningLive());
        RUN(TestScrubCache());
        RUN(TestDisabledAndBudget());
        RUN(TestCompletedAnswerJudgedAgainstCurrentView());
        RUN(TestBiosChangeWhileRunning());
        RUN(TestRunningLiveRegistersArrive());
        RUN(TestRunningLiveUnansweredFollowRetries());
        RUN(TestEntryParsing());
        RUN(TestEntryBoxFollowsShownFunction());
        RUN(TestRunnerStartFailure());
    }
    catch (std::exception& e)
    {
        std::printf("FATAL: uncaught std::exception: %s\n", e.what());
        return 3;
    }
    dts::RemoveTree(scratch);
    return dts::Finish("DecompilerPanelTests", start);
}
