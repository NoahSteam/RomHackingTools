// Area 8, round 2 (review): instruments on top of the round 1 trust test. Not part of the repository build:
// build_probe.sh compiles it with the flags of the trust test's own target.
//
//   a8r2_probe <scratch dir> long <mix> <first seed> <programs> [concat]   longer sequences: `concat` generated programs run
//                                                                           end to end, with Stop, Refresh, reconnect, rename, undo,
//                                                                           Decompile (t-contain) and hide/show interleaved after each Stop
//   a8r2_probe <scratch dir> follow <first seed> <ops>                       the Follow PC rig, seeds not used before
//   a8r2_probe <scratch dir> directed                                         the directed probes D1..D4 (see the functions below)
//   a8r2_probe <scratch dir> oom [stride]                                     a real allocation failure inside the engine, sweep over k,
//                                                                           each cell in a forked child, held to the trust model
//   a8r2_probe <scratch dir> script "<script>" <world>                        one script (area 1's words plus t-contain <fn>)
#define main trust_original_main
#include "DecompilerTrustTests.cpp"
#undef main

#include "AllocFailure.h"

#include <sys/wait.h>
#include <unistd.h>

namespace
{

// ---- new operations ---------------------------------------------------------------------------------------------------------

// t-contain <fn>: "Decompile" (ShowFunctionContaining) on the entry of a function, as the SH-2 Assembly menu does. The finder is given the
// entry as a user start first, so it answers with full confidence and no doubt (the finder's own texts are area 4's).
std::string ProbeOp(Rig& r, const Op& o)
{
    std::istringstream words(o.text);
    std::string verb;
    int idx = 0;
    words >> verb >> idx;
    if (verb != "contain") return TrustOp(r, o);
    if (!r.visible || r.holding || r.selected == -2) return "(hidden, held, or nothing selected yet)";
    const int want = static_cast<int>(static_cast<size_t>(idx) % r.placed.size());
    const uint32_t entry = Defs()[r.placed[static_cast<size_t>(want)]].entry;
    if (!r.bounds.Entries().count(entry))
    {
        Op b;
        b.kind = OpKind::Bound;
        b.a = want;
        b.b = 0;
        r.Execute(b);
    }
    r.panel.ShowFunctionContaining(0, entry);
    r.selected = want;
    r.mirrorSel = false;
    r.explicitPending = true;
    r.noEntry = 0;
    // r.haveStopped is left alone: by the guide a Stop belongs to the question stopped, and this is the same question.
    r.Frame();
    return "";
}

void SetupProbe(Rig& rig)
{
    Setup(rig);
    rig.custom = ProbeOp;
}

bool RunProbe(Oracle& oracle, const Program& p, uint64_t seed, bool verbose)
{
    HealForRun();
    oracle.beforeRun = [] { if (gRig && gRig->cfgBroken) HealConfig(); };
    oracle.afterRun = [] { if (gRig && gRig->cfgBroken) BreakConfig(); };
    gRigSetup = SetupProbe;
    const uint64_t before = gStats.ops + gStats.noEffect;
    const bool ok = RunProgram(oracle, p, seed, verbose, std::set<int>(), -1);
    gT.steps += gStats.ops + gStats.noEffect - before;
    ++gT.programs;
    gRig = nullptr;
    HealForRun();
    return ok;
}

Op Mk(OpKind k, int a = 0, int b = 0, int c = 0, const char* text = "")
{
    Op o;
    o.kind = k;
    o.a = a;
    o.b = b;
    o.c = c;
    o.text = text;
    return o;
}

// `concat` programs end to end (their functions are the first program's: indexes are taken modulo the world), and after each
// Stop a few of the operations that interleave with it.
Program MakeLong(uint64_t seed, int mix, int concat)
{
    Program p = MakeProgram(seed, mix);
    Rng rng(seed * 7919 + 17);
    std::vector<Op> ops = p.ops;
    for (int k = 1; k < concat; ++k)
    {
        const Program q = MakeProgram(seed * 131 + static_cast<uint64_t>(k) * 977, mix);
        for (const Op& o : q.ops) ops.push_back(o);
    }
    std::vector<Op> out;
    bool held = false;
    std::map<int, int> nameOf;   // function -> the name option it has now: the undo
    for (const Op& o : ops)
    {
        out.push_back(o);
        if (o.kind == OpKind::Hold) held = true;
        if (o.kind == OpKind::Wait || o.kind == OpKind::Finish) held = false;
        const int fn = o.a % static_cast<int>(p.placed.size());
        if (o.kind == OpKind::Rename && !held)
        {
            const int before = nameOf.count(fn) ? nameOf[fn] : 0;
            nameOf[fn] = o.b;
            if (rng.Chance(35))
            {
                // rename, something else, and the rename undone
                out.push_back(Mk(OpKind::Refresh));
                out.push_back(Mk(OpKind::Rename, o.a, before));
                nameOf[fn] = before;
            }
        }
        if (o.kind == OpKind::Stop && !held && rng.Chance(70))
        {
            const int n = 1 + rng.Below(3);
            for (int i = 0; i < n; ++i)
            {
                switch (rng.Below(9))
                {
                case 0: out.push_back(Mk(OpKind::Refresh)); break;
                case 1: out.push_back(Mk(OpKind::Reconnect, rng.Below(2))); break;
                case 2: out.push_back(Mk(OpKind::Rename, rng.Below(1000), rng.Below(4))); break;
                case 3: out.push_back(Mk(OpKind::Edit, rng.Below(1000), rng.Below(1000), rng.Below(1000))); break;
                case 4: out.push_back(Mk(OpKind::Custom, 0, 0, 0, ("contain " + std::to_string(rng.Below(1000))).c_str())); break;
                case 5: out.push_back(Mk(OpKind::Hide)); out.push_back(Mk(OpKind::Show)); break;
                case 6: out.push_back(Mk(OpKind::Select, rng.Below(1000))); break;
                case 7: out.push_back(Mk(OpKind::Volatile, rng.Below(4))); break;
                default: out.push_back(Mk(OpKind::Stop)); break;
                }
            }
        }
        // Decompile on a function now and then, as the SH-2 Assembly menu does.
        if (!held && o.kind == OpKind::Select && rng.Chance(12))
            out.push_back(Mk(OpKind::Custom, 0, 0, 0, ("contain " + std::to_string(rng.Below(1000))).c_str()));
    }
    p.ops = out;
    return p;
}

int RunLong(const char* scratch, int mix, uint64_t first, int programs, int concat)
{
    (void)scratch;
    Oracle oracle;
    int failed = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const uint64_t before = gT.steps;
    for (int i = 0; i < programs; ++i)
    {
        const uint64_t seed = first + static_cast<uint64_t>(i);
        const Program p = MakeLong(seed, mix, concat);
        const bool ok = RunProbe(oracle, p, seed, false);
        if (!ok)
        {
            ++failed;
            std::printf("FAILED: long mix %d seed %llu concat %d (%zu operations)\n", mix, static_cast<unsigned long long>(seed), concat, p.ops.size());
        }
    }
    std::printf("long mix %d: seeds %llu..%llu, concat %d: %d programs, %d failed, %llu operations, %.1f s\n", mix,
                static_cast<unsigned long long>(first), static_cast<unsigned long long>(first + programs - 1), concat, programs, failed,
                static_cast<unsigned long long>(gT.steps - before), dts::MsSince(t0) / 1000.0);
    return failed;
}

int RunLongVerbose(int mix, uint64_t seed, int concat)
{
    Oracle oracle;
    const Program p = MakeLong(seed, mix, concat);
    const bool ok = RunProbe(oracle, p, seed, true);
    std::printf("long mix %d seed %llu: %s\n", mix, static_cast<unsigned long long>(seed), ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}

// ---- directed probes on the small rig -----------------------------------------------------------------------------------------

const std::string kDoubt = "guessed from the previous rts; no symbol, call or user entry confirms it";

bool WaitHeldOrBusy(MiniRig& m)
{
    const auto t0 = std::chrono::steady_clock::now();
    while (m.runner.Busy() && m.gate.Held() == 0 && dts::MsSince(t0) < 30000.0) std::this_thread::yield();
    return m.gate.Held() > 0;
}

bool ClickMini(MiniRig& m, const char* item)
{
    ImVec2 a, b;
    if (!m.panel.ItemRect(item, a, b)) return false;
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    for (int i = 0; i < 3; ++i) m.Frame(c, false);
    m.Frame(c, true);
    m.Frame(c, false);
    m.Frame();
    return true;
}

void Edit(MiniRig& m)
{
    std::vector<uint8_t> hw = m.backend.content->bytes;
    hw[Defs()[FnC].sites[1].addr - kHw] ^= 0x01;
    m.backend.content = MakeContent(std::move(hw));
    ++m.serial;
}

int gDirectedBad = 0;

void Report(const char* what, bool ok, const std::string& detail = "")
{
    std::printf("probe %-100s %s\n", what, ok ? "ok" : "MISMATCH");
    if (!ok)
    {
        ++gDirectedBad;
        if (!detail.empty()) std::printf("%s\n", detail.c_str());
    }
}

// D1. Stop, then "Decompile" on the same function: the question is unchanged and nothing has asked it since, so the guide says the
// Stop stands ("Stopped." and the Stopped tooltip). What does the panel say, and does it ask?
void D1()
{
    const uint32_t cEntry = Defs()[FnC].entry;
    MiniRig m;
    m.panel.ShowFunctionContaining(0, cEntry + 4);
    m.Idle(3);
    CHECK(m.Settle());
    m.gate.Close();
    Edit(m);
    m.Idle(3);
    CHECK(WaitHeldOrBusy(m));
    m.Idle(2);
    CHECK(ClickMini(m, "stop"));
    m.gate.Open();
    CHECK(m.Settle());
    trust::TrueState t;
    t.inputsDiffer = true;
    t.stopped = true;
    t.entryDoubt = kDoubt;
    std::string err = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, true);
    Report("D1a after Stop: the Stopped badge and the grey note", err.empty(), err);
    const uint64_t sub = m.panel.Submissions();
    m.panel.ShowFunctionContaining(0, cEntry + 4);   // Decompile Here, again, on the same function
    m.Idle(4);
    CHECK(m.Settle());
    const bool asked = m.panel.Submissions() != sub;
    std::printf("      after Decompile on the same function: asked %d, stale %d, reason %d (0 None 1 Pending 2 Stopped 3 Disabled 4 NeedsRefresh), tooltip: %s\n",
                asked, m.panel.ShownStale(), static_cast<int>(m.panel.GetStaleReason()), m.panel.StaleTooltip().c_str());
    err = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, true);
    Report("D1b Decompile on the same function after a Stop: the Stop stands (guide: nothing has asked since)", err.empty(), err);
    t.stopped = false;
    t.inputsDiffer = m.panel.ShownStale();
    Report("D1c ...or Decompile asks the question again", asked, "");
}

// D2. Follow PC, a Stop, and the badge says "press Enter in the entry box": the box holds the function SHOWN, not the one followed.
void D2()
{
    const uint32_t cEntry = Defs()[FnC].entry, aEntry = Defs()[FnA].entry;
    MiniRig m;
    m.kind = DecompViewKind::LivePaused;
    m.regs[0].pc = cEntry + 4;
    m.haveRegs[0] = true;
    m.Idle(3);
    CHECK(m.Settle());
    const std::string shownEntry = m.panel.EntryText();
    m.gate.Close();
    m.regs[0].pc = aEntry + 4;
    ++m.frame;
    ++m.serial;
    m.Idle(3);
    CHECK(WaitHeldOrBusy(m));
    m.Idle(2);
    CHECK(ClickMini(m, "stop"));
    m.gate.Open();
    CHECK(m.Settle());
    std::printf("      Follow PC %d, PC in A (%08X), shown C (%08X), entry box '%s', badge reason %d, tooltip: %s\n", m.panel.FollowPc(), aEntry + 4, cEntry,
                m.panel.EntryText(), static_cast<int>(m.panel.GetStaleReason()), m.panel.StaleTooltip().c_str());
    // Do what the tooltip says: press Enter in the entry box.
    TypeEntry(m, m.panel.EntryText());
    m.Idle(3);
    CHECK(m.Settle());
    const DecompileResult* s = m.panel.Shown();
    std::printf("      after Enter in the box: Follow PC %d, shown entry %08X, stale %d\n", m.panel.FollowPc(), s ? s->requestedEntry : 0, m.panel.ShownStale());
    Report("D2 Enter in the box after a Stop under Follow PC asks for the function followed (it asks for the one shown and turns Follow PC off)",
           m.panel.FollowPc() && s && s->requestedEntry == aEntry, "");
}

// D3. Stop on a request whose answer is already waiting in the runner (the cache-hit and memo paths), and Stop pressed twice.
void D3()
{
    MiniRig m;
    m.panel.ShowFunctionContaining(0, Defs()[FnC].entry + 4);
    m.Idle(3);
    CHECK(m.Settle());
    // Refresh on current output, Stop at once: the answer may land first. Whatever happens, the state must be one the model accepts.
    for (int round = 0; round < 6; ++round)
    {
        m.gate.Close();
        CHECK(ClickMini(m, "refresh"));
        WaitHeldOrBusy(m);
        m.Idle(2);
        const bool stopped = ClickMini(m, "stop");
        ClickMini(m, "stop");
        m.gate.Open();
        CHECK(m.Settle());
        trust::TrueState t;
        t.entryDoubt = kDoubt;
        (void)stopped;
        const std::string err = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, true);
        // The grey note is the panel's own record of the Stop: tolerate it either way, but then the model must be told.
        if (!err.empty())
        {
            trust::TrueState t2 = t;
            t2.stopped = true;
            const std::string err2 = Verify(m.panel, t2, [&](ImVec2 p) { m.Frame(p, false); }, true);
            Report("D3 Refresh on current output, Stop twice", err2.empty(), err2);
        }
        else
            Report("D3 Refresh on current output, Stop twice (the answer landed first)", true);
    }
}


// D5. What reaches the reader only through the grey "N warnings" tooltip: a rename the engine could not apply (two functions given one
// name), shown as an engine note; is anything amber, or marked?
void D5()
{
    const uint32_t cEntry = Defs()[FnC].entry, aEntry = Defs()[FnA].entry;
    MiniRig m;
    m.names[aEntry] = "alpha";
    m.names[cEntry] = "alpha";
    ++m.serial;
    m.panel.ShowFunctionContaining(0, cEntry);
    m.Idle(3);
    CHECK(m.Settle());
    const DecompileResult* s = m.panel.Shown();
    std::printf("      two functions named alpha, C asked: name on screen '%s', %zu engine notes:\n", s ? s->name.c_str() : "-", s ? s->warnings.size() : 0);
    if (s)
        for (const std::string& w : s->warnings) std::printf("        - %s\n", w.c_str());
    const std::vector<std::string> unc = m.panel.Uncertainties();
    std::printf("      amber lines: %zu\n", unc.size());
    for (const std::string& u : unc) std::printf("        * %s\n", u.c_str());
    Report("D5 a rename the engine could not apply is amber", s && !s->warnings.empty() && !unc.empty() ? true : (s && s->warnings.empty()), "");
}


// D6. A refused entry ("zz") leaves its text in the box. Decompile on the function already shown (same function: the box is not
// synced) clears the refusal and the error: the box still reads "zz" beside the function that is current.
void D6()
{
    const uint32_t cEntry = Defs()[FnC].entry;
    MiniRig m;
    m.panel.ShowFunctionContaining(0, cEntry + 4);
    m.Idle(3);
    CHECK(m.Settle());
    TypeEntry(m, "zz");
    m.Idle(2);
    std::printf("      after typing zz: box '%s', error '%s', stale %d\n", m.panel.EntryText(), m.panel.EntryError().c_str(), m.panel.ShownStale());
    m.panel.ShowFunctionContaining(0, cEntry + 4);
    m.Idle(4);
    CHECK(m.Settle());
    std::printf("      after Decompile on the same function: box '%s', error '%s', stale %d\n", m.panel.EntryText(), m.panel.EntryError().c_str(), m.panel.ShownStale());
    Report("D6 the entry box does not keep a refused draft beside current output", std::string(m.panel.EntryText()) != "zz", "");
}

// ---- D4: a real allocation failure inside the engine ------------------------------------------------------------------------------

// One cell: allocation k (counted from the moment the engine is released) fails. The result is held to the model, then Refresh is
// pressed once more (an engine damaged by the failure refuses, and says so).
int OomCell(long k)
{
    MiniRig m;
    const uint32_t cEntry = Defs()[FnC].entry;
    m.gate.Close();
    m.panel.ShowFunctionContaining(0, cEntry + 4);
    m.Idle(3);
    if (!WaitHeldOrBusy(m)) { std::printf("cell %ld: engine never held\n", k); return 3; }
    dts::AllocCount() = 0;
    dts::FailAfter() = k;
    m.gate.Open();
    const auto t0 = std::chrono::steady_clock::now();
    while (m.runner.Busy() && dts::MsSince(t0) < 60000.0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const long used = dts::AllocCount().load();
    const bool fired = dts::FailAfter().load() == -1;
    dts::FailAfter() = -1;
    m.Idle(4);
    CHECK(m.Settle());
    const DecompileResult* s = m.panel.Shown();
    std::string line = "cell " + std::to_string(k) + " (" + (fired ? "fired" : "not reached") + ", " + std::to_string(used) + " allocations): ";
    if (!s) line += "nothing shown";
    else line += "status " + std::to_string(static_cast<int>(s->status)) + " '" + s->error + "'";
    trust::TrueState t;
    t.entryDoubt = kDoubt;
    t.sessionDisabled = m.runner.Disabled();
    t.disabledReason = m.runner.DisabledReason();
    std::string err = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, true);
    // Press Refresh: the damaged engine refuses; the panel says so, or the answer is right.
    ClickMini(m, "refresh");
    m.Idle(3);
    CHECK(m.Settle());
    s = m.panel.Shown();
    line += s ? " | after Refresh: status " + std::to_string(static_cast<int>(s->status)) + " '" + s->error + "'" : " | after Refresh: nothing shown";
    std::string err2 = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, true);
    std::printf("%s | model: %s\n", line.c_str(), err.empty() && err2.empty() ? "ok" : "MISMATCH");
    if (!err.empty()) std::printf("%s", err.c_str());
    if (!err2.empty()) std::printf("after Refresh:\n%s", err2.c_str());
    return err.empty() && err2.empty() ? 0 : 1;
}

int RunOom(long stride)
{
    // Size a run first: the allocations of one request, counted in a child.
    long total = 0;
    {
        int fd[2];
        CHECK(pipe(fd) == 0);
        const pid_t pid = fork();
        if (pid == 0)
        {
            close(fd[0]);
            MiniRig m;
            m.gate.Close();
            m.panel.ShowFunctionContaining(0, Defs()[FnC].entry + 4);
            m.Idle(3);
            WaitHeldOrBusy(m);
            dts::AllocCount() = 0;
            m.gate.Open();
            while (m.runner.Busy()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const long n = dts::AllocCount().load();
            (void)!write(fd[1], &n, sizeof n);
            _exit(0);
        }
        close(fd[1]);
        (void)!read(fd[0], &total, sizeof total);
        close(fd[0]);
        int st = 0;
        waitpid(pid, &st, 0);
    }
    std::printf("one request of C allocates %ld times (counted from the held engine)\n", total);
    int bad = 0, cells = 0;
    for (long k = 0; k <= total + 5; k += stride)
    {
        const pid_t pid = fork();
        if (pid == 0) _exit(OomCell(k));
        int st = 0;
        waitpid(pid, &st, 0);
        ++cells;
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
        {
            ++bad;
            std::printf("cell %ld: %s %d\n", k, WIFEXITED(st) ? "exit status" : "signal", WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
        }
    }
    std::printf("oom sweep: %d cells, %d not clean\n", cells, bad);
    return bad;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: %s <scratch dir> long|follow|directed|oom|script ...\n", argv[0]);
        return 2;
    }
    dts::PrepareProcess();
    const auto start = std::chrono::steady_clock::now();
    const std::string scratch = argv[1];
    dts::RemoveTree(scratch);
    CHECK(sfe::MakeDirectory(scratch));
    gConfig = dts::Join(scratch, "config");
    CHECK(sfe::MakeDirectory(gConfig));
    gHover = std::getenv("SE_TRUST_NOHOVER") == nullptr;
    const std::string mode = argv[2];
    int bad = 0;
    try
    {
        if (mode == "long")
        {
            const int mix = argc > 3 ? std::atoi(argv[3]) : 7;
            const uint64_t first = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 1;
            const int programs = argc > 5 ? std::atoi(argv[5]) : 10;
            const int concat = argc > 6 ? std::atoi(argv[6]) : 4;
            if (std::getenv("SE_PROBE_VERBOSE")) bad = RunLongVerbose(mix, first, concat);
            else bad = RunLong(argv[1], mix, first, programs, concat);
        }
        else if (mode == "follow")
        {
            const uint64_t first = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 1;
            const uint64_t want = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 5000;
            uint64_t ops = 0;
            int seeds = 0;
            for (uint64_t seed = first; ops < want; ++seed, ++seeds)
                if (!RunFollow(seed, false, ops))
                {
                    ++bad;
                    std::printf("FAILED: follow seed %llu\n", static_cast<unsigned long long>(seed));
                }
            std::printf("follow: seeds from %llu, %d sequences, %llu operations, %d failed\n", static_cast<unsigned long long>(first), seeds,
                        static_cast<unsigned long long>(ops), bad);
        }
        else if (mode == "directed")
        {
            D1();
            D2();
            D3();
            D5();
            D6();
            bad = gDirectedBad;
        }
        else if (mode == "oom")
            bad = RunOom(argc > 3 ? std::atol(argv[3]) : 200);
        else if (mode == "script")
        {
            Program p;
            std::string error;
            if (argc < 5 || !ParseScript(argv[3], argv[4], 0, p, error))
            {
                std::printf("cannot parse: %s\n", error.c_str());
                return 2;
            }
            Oracle oracle;
            bad = RunProbe(oracle, p, 0, true) ? 0 : 1;
            std::printf("script: %s\n", bad ? "FAILED" : "ok");
        }
    }
    catch (std::exception& e)
    {
        std::printf("FATAL: %s\n", e.what());
        return 3;
    }
    dts::RemoveTree(scratch);
    return bad ? 1 : 0;
}
