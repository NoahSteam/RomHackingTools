// A1-7: a paused emulator that App captures every UI frame must not make the SH-2 Pseudocode panel
// ask again every frame.
//
// App captures a paused emulator each frame while a state load is unconfirmed (and while a step
// settles or the session is behind). Every capture re-derives the context, so se_derive_serial moves
// over bytes that did not; the panel's LivePaused view follows every move at once, so it captured,
// submitted and superseded its own run before the engine finished it. A function longer than a frame
// was never shown, for as long as the server withheld the confirmation.
//
// The whole chain, minus App's own glue: the real exporter (se_export.c) withholds the load's
// confirmation, the real live driver and context are captured through CaptureLiveFrame (the call App
// makes), the real panel, runner and engine read the memory through ContextBackend. "Takes longer
// than a frame" is the runner's phase observer holding the engine, not a timer.
//
// Part 1 is the tracker alone (CaptureRepeatTracker): what counts as a repeat and what does not.
// Part 2 is the reproduction: the same frames with the raw serial (what App gave the panel) and with
// the tracker's (what it gives now).
//
// usage: SaturnExplorerLiveDecompilerCaptureTests <scratch dir>
#include "DecompilerTestSupport.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

extern "C" {
#include "se_export.h"
}
#include "CaptureRepeatTracker.h"
#include "DecompilerPanel.h"
#include "Debug/MemoryBackend.h"
#include "ImGuiHarness.h"
#include "LiveCapture.h"
#include "LiveDriver.h"
#include "SeLiveProtocol.h"
#include "Theme.h"

using namespace sfe;
using namespace sfe::decomp;

namespace
{

std::string gConfig;

// ---- Part 1: the tracker ----------------------------------------------------------------------

void TestTrackerCountsOnlyUntouchedRepeats()
{
    const int ctxA = 0, ctxB = 0;
    const uint64_t rangeA = 1ull << 32, rangeB = 2ull << 32;
    CaptureRepeatTracker t;

    // Nothing noted: the raw serial passes through.
    CHECK(t.Apply(&ctxA, rangeA + 5) == rangeA + 5);

    // The first capture has nothing to repeat. A second with the same revision does.
    t.Note(&ctxA, rangeA + 1, rangeA + 2, 7);
    CHECK(t.Repeats() == 0 && t.Apply(&ctxA, rangeA + 2) == rangeA + 2);
    t.Note(&ctxA, rangeA + 2, rangeA + 3, 7);
    CHECK(t.Repeats() == 1);
    CHECK(t.Apply(&ctxA, rangeA + 3) == rangeA + 2);   // the same value the first capture had
    t.Note(&ctxA, rangeA + 3, rangeA + 4, 7);
    t.Note(&ctxA, rangeA + 4, rangeA + 5, 7);
    CHECK(t.Repeats() == 3 && t.Apply(&ctxA, rangeA + 5) == rangeA + 2);

    // New content moves it, by exactly one, and stays moved when it then repeats.
    t.Note(&ctxA, rangeA + 5, rangeA + 6, 8);
    CHECK(t.Apply(&ctxA, rangeA + 6) == rangeA + 3);
    t.Note(&ctxA, rangeA + 6, rangeA + 7, 8);
    CHECK(t.Apply(&ctxA, rangeA + 7) == rangeA + 3);

    // An edit re-derives without a capture: it is a different view, and counts for itself.
    CHECK(t.Apply(&ctxA, rangeA + 8) == rangeA + 4);
    // ...and a capture of the revision it started from is not a repeat of what was on screen: it
    // replaced the edited bytes. The edit and the capture are two moves.
    t.Note(&ctxA, rangeA + 8, rangeA + 9, 8);
    CHECK(t.Repeats() == 4);   // unchanged by that capture
    CHECK(t.Apply(&ctxA, rangeA + 9) == rangeA + 5);
    t.Note(&ctxA, rangeA + 9, rangeA + 10, 8);   // and the next one is a repeat of it again
    CHECK(t.Apply(&ctxA, rangeA + 10) == rangeA + 5);

    // A source that cannot say (revision 0) never repeats; neither does a failed capture count.
    t.Note(&ctxA, rangeA + 10, rangeA + 11, 0);
    t.Note(&ctxA, rangeA + 11, rangeA + 12, 0);
    CHECK(t.Repeats() == 5);
    t.Note(&ctxA, rangeA + 12, rangeA + 12, 9);   // the capture failed: the serial did not move
    CHECK(t.Repeats() == 5 && t.Apply(&ctxA, rangeA + 12) == rangeA + 7);
    t.Note(&ctxA, rangeA + 12, rangeA + 14, 9);   // re-derived twice: not one capture
    CHECK(t.Repeats() == 5);

    // A context allocated where this one was has serials in a range of its own: a fresh start, and
    // a serial of that range is not discounted by the old one's count.
    CHECK(t.Repeats() == 5);
    CHECK(t.Apply(&ctxA, rangeB + 7) == rangeB + 7);
    t.Note(&ctxA, rangeB + 1, rangeB + 2, 9);   // the same revision number, another context
    CHECK(t.Repeats() == 0 && t.Apply(&ctxA, rangeB + 2) == rangeB + 2);
    t.Note(&ctxA, rangeB + 2, rangeB + 3, 9);
    CHECK(t.Repeats() == 1);
    // Another address is another context as well.
    t.Note(&ctxB, rangeB + 4, rangeB + 5, 9);
    CHECK(t.Repeats() == 0 && t.Apply(&ctxB, rangeB + 5) == rangeB + 5);
    CHECK(t.Apply(&ctxA, rangeB + 3) == rangeB + 3);   // not the context it knows now
}

// The view serial never repeats a value for two states of the context: over a long scripted run of
// captures (new content, repeated, edited), every state the raw serial tells apart that differs in
// content has a view serial of its own, and states that are repeats share one.
void TestTrackerIsInjectiveOnContent()
{
    const int ctx = 0;
    CaptureRepeatTracker t;
    uint64_t raw = 1ull << 32;
    uint64_t revision = 1;
    uint64_t lastView = 0;
    // content id of what is on screen: moves on new revision and on an edit, back on a revert
    uint64_t content = 1, nextContent = 2;
    uint64_t lastCaptureContent = content;
    bool editedSinceCapture = false;
    uint64_t viewOfContent[64] = {};   // content id -> the view serial first seen for it
    auto see = [&](const char* what) {
        const uint64_t v = t.Apply(&ctx, raw);
        if (viewOfContent[content] == 0) viewOfContent[content] = v;
        else if (viewOfContent[content] != v) dts::Check(false, what, __FILE__, __LINE__);
        lastView = v;
    };
    t.Note(&ctx, raw, raw + 1, revision);
    ++raw;
    see("first capture");
    struct Step { char op; };
    // c: capture the same revision, n: capture a new revision, e: edit in place
    const char script[] = "ccncceccnecnecccn";
    for (const char* p = script; *p; ++p)
    {
        const uint64_t before = lastView;
        if (*p == 'e')
        {
            ++raw;
            content = nextContent++;
            editedSinceCapture = true;
            see("an edit");
            CHECK(t.Apply(&ctx, raw) != before);
        }
        else if (*p == 'n')
        {
            ++revision;
            t.Note(&ctx, raw, raw + 1, revision);
            ++raw;
            content = nextContent++;
            lastCaptureContent = content;
            editedSinceCapture = false;
            see("a capture of new content");
            CHECK(lastView != before);
        }
        else
        {
            t.Note(&ctx, raw, raw + 1, revision);
            ++raw;
            // The capture shows what the previous capture showed, whatever an edit did in between.
            const bool reverted = editedSinceCapture;
            content = lastCaptureContent;
            editedSinceCapture = false;
            // A revert is the old content under a new identity: it must not take the old value (the
            // edit in between moved the view on from it) and need not match it.
            if (reverted)
            {
                const uint64_t v = t.Apply(&ctx, raw);
                CHECK(v != before);
                lastView = v;
                viewOfContent[content] = v;
            }
            else
            {
                see("a repeat");
                CHECK(lastView == before);
            }
        }
    }
}

// ---- The server --------------------------------------------------------------------------------

std::atomic<int> gLoadResult{ 0 };

extern "C" size_t FakeSave(unsigned char* buf, size_t cap)
{
    if (!buf) return 64;
    if (cap < 64) return 0;
    std::memset(buf, 0xA5, 64);
    return 64;
}
extern "C" int FakeLoad(const unsigned char*, size_t) { return gLoadResult.load(); }
extern "C" int FakeSlotInfo(unsigned int, unsigned long long* mtime) { *mtime = 1; return 1; }
extern "C" int FakeSlotLoad(unsigned int) { return gLoadResult.load(); }
extern "C" int FakeSlotSave(unsigned int) { return 0; }

const uint32_t kHw = 0x06000000u, kLw = 0x00200000u, kSize = 0x00100000u;

// Big-endian, as the Saturn has it; the exporter is handed work RAM in the emulator's host order (16-bit
// words, byte-swapped on this machine) and the driver swaps it back.
std::vector<uint8_t> gHw(kSize, 0), gLw(kSize, 0);
std::vector<uint8_t> gHwHost, gLwHost;
// Exporter input order is Yabause R[16], SR, GBR, VBR, MACH, MACL, PR, PC.
uint32_t gMasterRegs[23] = {}, gSlaveRegs[23] = {};

std::vector<uint8_t> HostOrder(const std::vector<uint8_t>& be)
{
    std::vector<uint8_t> out(be);
    for (size_t i = 0; i + 1 < out.size(); i += 2) std::swap(out[i], out[i + 1]);
    return out;
}

void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// One emulated frame: the gate (which is where a queued load is applied) then the snapshot.
void EmulatorTick()
{
    SeExportGateFrame();
    SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, gLwHost.data(), gHwHost.data(), nullptr,
                     gMasterRegs, gSlaveRegs, nullptr, nullptr, nullptr);
}

// A paused emulator publishes nothing: no tick. The driver's poll thread keeps fetching the newest
// frame and so republishing the same content, which is what a paused or halted game looks like.

// Holds the engine at the start of each decompilation while closed.
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
    void Close() { std::lock_guard<std::mutex> lock(mMutex); mClosed = true; }
    void Open() { std::lock_guard<std::mutex> lock(mMutex); mClosed = false; mCv.notify_all(); }
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

// What App has: the source, its context, the tracker, the backend over the context.
struct Live
{
    se_data_source ds{};
    se_context* ctx = nullptr;
    CaptureRepeatTracker captures;
    ContextBackend backend{ &ctx };

    uint64_t Raw() const { return se_derive_serial(ctx); }
    uint64_t View() const { return captures.Apply(ctx, Raw()); }
    // What App does once per UI frame while the emulator is paused and a load is unconfirmed.
    void Capture() { CaptureLiveFrame(captures, ctx, &ds); }
    uint64_t Revision() const { return se_live_captured_revision(&ds); }
    bool Restore(uint32_t* done, uint32_t* failed) const { return se_live_restore_state(&ds, done, failed) != 0; }
};

// The panel, its runner and the harness over a Live, with the view App builds for it.
struct Rig
{
    Gate gate;   // before the runner: it must outlive the worker that waits on it
    Live& live;
    DecompilerRunner runner;
    DecompilerPanel panel;
    ImGuiHarness h;
    std::map<uint32_t, std::string> names;
    FunctionBounds bounds;
    std::unordered_map<uint32_t, std::string> comments;
    bool useTracker;   // the serial the panel is given: the tracker's (now) or the raw one (before)

    // The engine is held on purpose for many frames: neither the time budget nor the cancel
    // deadline (wall clock, a few seconds by default) may end that on a slow build.
    static RunnerOptions Options()
    {
        RunnerOptions o;
        o.budgetSeconds = 600;
        o.cancelAckSeconds = 600;
        return o;
    }

    Rig(Live& l, bool tracker) : live(l), runner(gConfig, Options()), h([this] { Ui(); }), useTracker(tracker)
    {
        ApplyTheme(ImGui::GetStyle());
        LoadFonts(ImGui::GetIO());
        runner.SetPhaseObserverForTests([this](DecompilePhase p) { gate.Observe(p); });
        panel.SetCpu(0);
    }
    ~Rig() { gate.Open(); }

    // BuildDecompilerView, for a live paused source.
    DecompilerView View() const
    {
        DecompilerView v;
        v.backend = &live.backend;
        v.kind = DecompViewKind::LivePaused;
        v.sourceLabel = "live, paused";
        v.sourceId = live.backend.SourceId();
        v.deriveSerial = useTracker ? live.View() : live.Raw();
        v.frameNumber = se_frame_number(live.ctx);
        for (int cpu = 0; cpu < 2; ++cpu) v.haveRegs[cpu] = se_get_sh2_regs(live.ctx, cpu, &v.regs[cpu]) == SE_OK;
        return v;
    }
    void Ui()
    {
        const DecompilerView v = View();
        DecompilerInputs in;
        in.names = &names;
        in.namesGeneration = 1;
        in.bounds = &bounds;
        in.comments = &comments;
        panel.Update(v, runner, in, true);
        DecompilerPanel::Request r;
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(760, 380), ImGuiCond_Always);
        panel.Draw(nullptr, v, runner, in, r);
    }
    void Frame() { h.Frame(ImVec2(1270, 710), false); }

    // One UI frame as App runs it: capture the paused emulator, then draw.
    void AppFrame()
    {
        live.Capture();
        Frame();
    }
    bool ShownHas(const char* what) const
    {
        const DecompileResult* s = panel.Shown();
        return s && s->Text().find(what) != std::string::npos;
    }
    // Frames until 'done'; with 'capture' each is a UI frame of App's (a capture, then the panel),
    // without it the context is left as it is (an edit in it stays until the next capture).
    template <class F>
    bool PumpUntil(F done, bool capture = true)
    {
        const auto t0 = std::chrono::steady_clock::now();
        while (!done())
        {
            if (dts::MsSince(t0) > 30000.0) return false;
            if (capture) AppFrame(); else Frame();
            Sleep(1);
        }
        return true;
    }
};

// The emulator has been told to load a state and has not answered: it will, at its next gate, which
// this test does not run until it means to. Waits until the driver has sent it, so the exporter has
// it in its mailbox, and the display has settled on one revision.
void SettleOnRevision(Live& live)
{
    int stable = 0;
    uint64_t last = 0;
    for (int i = 0; i < 2000 && stable < 20; ++i)
    {
        live.Capture();
        const uint64_t r = live.Revision();
        stable = (r != 0 && r == last) ? stable + 1 : 0;
        last = r;
        Sleep(3);
    }
    CHECK(stable >= 20);
}

// ---- Part 2: the reproduction and the fix -----------------------------------------------------

struct Phase
{
    uint64_t submissions = 0, captures = 0, rawMoves = 0, viewMoves = 0;
    bool shownWhileHeld = false, shownAfterOpen = false, shownAtEnd = false;
};

// 'frames' UI frames with the engine held (a function that takes longer than they do), then the engine
// let go and 'frames' more, then the identity left alone until the answer is shown.
Phase RunWithdrawnConfirmation(Live& live, bool useTracker, int frames)
{
    Phase out;
    Rig rig(live, useTracker);
    rig.gate.Close();
    rig.panel.ShowFunctionContaining(0, dts::kFixture1Caller);
    const uint64_t subs0 = rig.panel.Submissions(), caps0 = rig.panel.Captures();
    uint64_t lastRaw = live.Raw(), lastView = rig.View().deriveSerial;
    auto count = [&] {
        if (live.Raw() != lastRaw) ++out.rawMoves;
        lastRaw = live.Raw();
        const uint64_t v = rig.View().deriveSerial;
        if (v != lastView) ++out.viewMoves;
        lastView = v;
    };
    for (int i = 0; i < frames; ++i)
    {
        rig.AppFrame();
        count();
        if (i == 0) CHECK(rig.gate.WaitHeld());   // the engine has the first question
        Sleep(1);
    }
    out.submissions = rig.panel.Submissions() - subs0;
    out.captures = rig.panel.Captures() - caps0;
    out.shownWhileHeld = rig.panel.Shown() != nullptr;

    // The engine is released and now finishes anything it is given in a few milliseconds -- far less
    // than a frame. The identity keeps doing what it did.
    rig.gate.Open();
    for (int i = 0; i < frames; ++i)
    {
        rig.AppFrame();
        count();
        Sleep(1);
    }
    out.shownAfterOpen = rig.panel.Shown() != nullptr;

    // Nothing else changes; only the identity stops moving (no more captures: the serial holds
    // still). The answer was there to be had all along.
    for (int i = 0; i < 4000 && !(rig.panel.Shown() && !rig.panel.Pending()); ++i)
    {
        rig.Frame();
        Sleep(1);
    }
    out.shownAtEnd = rig.ShownHas("+ 1");
    return out;
}

void TestWithheldConfirmationDoesNotReaskEveryFrame(Live& live)
{
    const int kFrames = 30;

    // The reproduction: the serial the panel was given before this fix. The load is outstanding and
    // the emulator paused, so App captures every frame, and every capture is a new identity.
    const Phase before = RunWithdrawnConfirmation(live, /*useTracker=*/false, kFrames);
    CHECK(before.rawMoves >= static_cast<uint64_t>(2 * kFrames) - 2);   // the premise: it moved each frame
    CHECK(before.submissions >= static_cast<uint64_t>(kFrames) - 2);    // the panel asked again each frame
    CHECK(before.captures >= static_cast<uint64_t>(kFrames) - 2);       // and copied 2 MiB each time
    CHECK(!before.shownWhileHeld);
    CHECK(!before.shownAfterOpen);   // finished runs are for the identity of a frame ago: dropped
    CHECK(before.shownAtEnd);        // the answer was there to be had: only the identity kept it away

    std::printf("  raw serial, %d held + %d free frames: raw moved %llu times, view moved %llu; panel captured %llu, asked %llu; "
                "shown while held %d, after release %d, once the identity stood still %d\n",
                kFrames, kFrames, (unsigned long long)before.rawMoves, (unsigned long long)before.viewMoves,
                (unsigned long long)before.captures, (unsigned long long)before.submissions, before.shownWhileHeld,
                before.shownAfterOpen, before.shownAtEnd);

    // The fix: the serial App gives it now. The same frames, the same captures, the same server.
    const Phase after = RunWithdrawnConfirmation(live, /*useTracker=*/true, kFrames);
    CHECK(after.rawMoves >= static_cast<uint64_t>(2 * kFrames) - 2);   // the raw serial still moved
    CHECK(after.viewMoves == 0);                                        // the identity did not
    CHECK(after.submissions == 1);                                      // asked once
    CHECK(after.captures == 1);                                         // one copy of memory
    std::printf("  tracker's serial, the same frames: raw moved %llu times, view moved %llu; panel captured %llu, asked %llu; "
                "shown after release %d\n",
                (unsigned long long)after.rawMoves, (unsigned long long)after.viewMoves,
                (unsigned long long)after.captures, (unsigned long long)after.submissions, after.shownAfterOpen);
    CHECK(after.shownAfterOpen);                                        // and the answer shows
    CHECK(after.shownAtEnd);
}

// What must still move the identity: the confirmation arriving (new content), an edit, and the
// capture that replaces an edit the emulator never took.
void TestRealChangesStillMoveTheIdentity(Live& live)
{
    Rig rig(live, true);
    rig.panel.ShowFunctionContaining(0, dts::kFixture1Caller);
    CHECK(rig.PumpUntil([&] { return !rig.panel.Pending() && rig.ShownHas("+ 1"); }));
    SettleOnRevision(live);
    for (int i = 0; i < 5; ++i) rig.AppFrame();
    const uint64_t v0 = live.View();
    const uint64_t subs0 = rig.panel.Submissions();
    for (int i = 0; i < 5; ++i) rig.AppFrame();
    CHECK(live.View() == v0 && rig.panel.Submissions() == subs0);   // still: nothing asked

    // The emulator takes the load at its next frame: the counters move, so does the content.
    uint32_t done0 = 0, failed0 = 0;
    CHECK(live.Restore(&done0, &failed0));
    gLoadResult = 0;
    EmulatorTick();
    bool resolved = false;
    for (int i = 0; i < 2000 && !resolved; ++i)
    {
        uint32_t d = 0, f = 0;
        const bool known = live.Restore(&d, &f);   // read before the capture, as App does
        rig.AppFrame();
        resolved = known && d == done0 + 1;
        Sleep(2);
    }
    CHECK(resolved);
    CHECK(live.View() != v0);                                  // the new frame is a new identity
    CHECK(rig.PumpUntil([&] { return !rig.panel.Pending() && rig.panel.Submissions() > subs0; }));
    CHECK(rig.ShownHas("+ 1"));

    // ...and it settles again once nothing new is published.
    SettleOnRevision(live);
    const uint64_t v1 = live.View();
    const uint64_t subs1 = rig.panel.Submissions();
    for (int i = 0; i < 10; ++i) rig.AppFrame();
    CHECK(live.View() == v1 && rig.panel.Submissions() == subs1);

    // An edit through the Memory panel's backend re-derives the context: a different view.
    const uint32_t editAt = dts::kFixture1AddOne + 1;   // add #1,r0
    const uint8_t five = 0x05;
    CHECK(live.backend.WriteMemory(editAt, &five, 1) == 1);
    const uint64_t vEdit = live.View();
    CHECK(vEdit != v1);
    CHECK(rig.PumpUntil([&] { return !rig.panel.Pending() && rig.ShownHas("+ 5"); }, /*capture=*/false));

    // The emulator has not taken the poke (it takes it at its next frame, which nobody runs), so the
    // next capture shows the old byte again. That is a change of the view, not a repeat of the edited
    // one: the edit in between keeps it from being counted.
    live.Capture();
    const uint64_t vBack = live.View();
    CHECK(vBack != vEdit && vBack != v1);
    CHECK(rig.PumpUntil([&] { return !rig.panel.Pending() && rig.ShownHas("+ 1"); }, /*capture=*/false));
    // ...and from then on captures of it are repeats again.
    live.Capture();
    CHECK(live.View() == vBack);
}

// SameContent must include BOTH captured SH-2 files even when frame, stop and all RAM are
// unchanged. A second independently sampled register file would hide this identity error.
void TestRegisterOnlyChangesMoveTheIdentity(Live& live)
{
    gMasterRegs[22] = dts::kFixture1Caller;
    gSlaveRegs[22] = dts::kFixture1Caller;
    SeExportNotifyStop(0, dts::kFixture1Caller);
    auto publish = [] {
        SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, gLwHost.data(), gHwHost.data(), nullptr,
                         gMasterRegs, gSlaveRegs, nullptr, nullptr, nullptr);
    };
    publish();
    SettleOnRevision(live);
    Rig rig(live, true);
    rig.panel.ShowFunctionContaining(0, dts::kFixture1Caller);
    CHECK(rig.PumpUntil([&] { return rig.panel.Shown() && !rig.panel.Pending(); }));
    CHECK(rig.panel.HeldKey() != nullptr);
    if (!rig.panel.HeldKey()) return;
    for (int cpu = 0; cpu < 2; ++cpu)
    {
        const uint64_t frame = se_frame_number(live.ctx);
        const uint64_t revision = live.Revision(), view = live.View();
        const auto oldKey = *rig.panel.HeldKey();
        const int reg = cpu == 0 ? 4 : 8;
        const uint32_t expected = cpu == 0 ? 0x12345678u : 0x89ABCDEFu;
        (cpu == 0 ? gMasterRegs : gSlaveRegs)[reg] = expected;
        publish();
        bool observed = false;
        for (int i = 0; i < 2000 && !observed; ++i)
        {
            live.Capture();
            se_sh2_regs regs{};
            observed = se_get_sh2_regs(live.ctx, cpu, &regs) == SE_OK && regs.r[reg] == expected;
            if (!observed) Sleep(2);
        }
        CHECK(observed);
        CHECK(se_frame_number(live.ctx) == frame);
        CHECK(live.Revision() != revision);
        CHECK(live.View() != view);
        rig.Frame();
        CHECK(rig.panel.HeldKey() && *rig.panel.HeldKey() != oldKey);
        CHECK(rig.PumpUntil([&] { return !rig.panel.Pending() && !rig.panel.ShownStale(); }));
        SettleOnRevision(live);
        const uint64_t stable = live.View();
        live.Capture();
        CHECK(live.View() == stable);
    }
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

    RUN(TestTrackerCountsOnlyUntouchedRepeats());
    RUN(TestTrackerIsInjectiveOnContent());

    // The function the panel will be asked about, in high work RAM of the emulator being faked.
    {
        std::vector<uint8_t> image;
        dts::PlaceFixture1(image);
        std::copy(image.begin(), image.end(), gHw.begin());
        dts::Put16(gHw, dts::kFixture1Caller - 4 - kHw, 0x000B);   // rts
        dts::Put16(gHw, dts::kFixture1Caller - 2 - kHw, 0x0009);   //   nop
    }

    gHwHost = HostOrder(gHw);
    gLwHost = HostOrder(gLw);

    if (SeExportInit() != 0) { std::printf("SeExportInit failed\n"); return 1; }
    SeExportSetSaveStateHook(FakeSave);
    SeExportSetLoadStateHook(FakeLoad);
    SeExportSetEmuSlotHooks(FakeSlotInfo, FakeSlotLoad);
    SeExportSetEmuSlotSaveHook(FakeSlotSave);

    Live live;
    se_result r = SE_ERR_IO;
    for (int i = 0; i < 400 && r != SE_OK; ++i)
    {
        r = se_live_open(nullptr, &live.ds);
        if (r != SE_OK) Sleep(5);
    }
    if (r != SE_OK) { std::printf("could not open the live source\n"); SeExportDeinit(); return 1; }
    se_config cfg;
    cfg.abi_version = SE_ABI_VERSION;
    cfg.reserved = 0;
    live.ctx = se_create(&live.ds, &cfg);
    if (!live.ctx) { std::printf("se_create failed\n"); SeExportDeinit(); return 1; }

    try
    {
        // A first frame with the memory in it, captured; the load the emulator will never confirm
        // (until this test runs its gate), and the display settled on the republished frame.
        bool counters = false, memory = false;
        for (int i = 0; i < 400 && !(counters && memory); ++i)
        {
            EmulatorTick();
            live.Capture();
            uint32_t d = 0, f = 0;
            counters = live.Restore(&d, &f);
            MemoryReadRequest rq;
            rq.address = dts::kFixture1Caller;
            rq.size = 2;
            const auto rd = live.backend.ReadMemoryBatch({ rq });
            memory = rd[0].success && (rd[0].bytes[0] != 0 || rd[0].bytes[1] != 0);
            if (!(counters && memory)) Sleep(5);
        }
        CHECK(counters);
        CHECK(memory);   // the function is in the context the panel reads
        const std::vector<uint8_t> state(64, 0x5A);
        CHECK(se_load_state(live.ctx, 7, state.data(), state.size(), nullptr, 0) == SE_OK);
        Sleep(200);   // the poll thread sends it; the exporter holds it for a gate nobody runs
        SettleOnRevision(live);
        uint32_t d = 0, f = 0;
        CHECK(live.Restore(&d, &f));
        const uint32_t done0 = d, failed0 = f;

        RUN(TestWithheldConfirmationDoesNotReaskEveryFrame(live));
        uint32_t d1 = 0, f1 = 0;
        CHECK(live.Restore(&d1, &f1) && d1 == done0 && f1 == failed0);   // still unconfirmed throughout

        RUN(TestRealChangesStillMoveTheIdentity(live));
        RUN(TestRegisterOnlyChangesMoveTheIdentity(live));
    }
    catch (std::exception& e)
    {
        std::printf("FATAL: uncaught std::exception: %s\n", e.what());
        se_destroy(live.ctx);
        SeExportDeinit();
        return 3;
    }
    se_destroy(live.ctx);
    SeExportDeinit();
    dts::RemoveTree(scratch);
    return dts::Finish("LiveDecompilerCaptureTests", start);
}
