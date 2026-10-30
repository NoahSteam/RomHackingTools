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
    // r.haveStopped is left alone: by the guide a Stop belongs to the question stopped, and this is the same question (A8-17: the
    // panel forgets it). SE_PROBE_CONTAIN_FORGETS_STOP=1 models what the panel does today, to look for what lies behind it.
    if (std::getenv("SE_PROBE_CONTAIN_FORGETS_STOP")) r.haveStopped = false;
    r.Frame();
    return "";
}


// The Containing target (t-contain) is the last thing that chose the question: the anchor is the function's entry, and the finder's own
// words about it (area 4's) are taken as the panel gives them, so that what is held is their presence and place, not their text.
void ContainHook(Rig& r, trust::TrueState& t)
{
    bool containing = false;
    for (size_t i = r.done.size(); i-- > 0;)
    {
        const std::string& d = r.done[i];
        if (d.find("  (") != std::string::npos) continue;   // it had no effect (the panel was hidden, the engine held)
        if (d.find("area 8: contain ") != std::string::npos) { containing = true; break; }
        if (d.find("] select ") != std::string::npos || d.find("initial selection") != std::string::npos ||
            d.find("badentry") != std::string::npos || d.find("containbad") != std::string::npos)
            break;
    }
    if (!containing || r.selected < 0) return;
    // The lines belong to the answer to the question on screen: output for another function carries neither.
    const uint32_t entry = Defs()[r.placed[static_cast<size_t>(r.selected)]].entry;
    if (!r.haveShown || r.shown.entry != entry) return;
    t.anchorKnown = true;
    t.anchor = entry;
    for (const std::string& u : r.panel.Uncertainties())
        if (u.compare(0, 7, "Entry: ") == 0 && u.size() > 8) t.entryDoubt = u.substr(7, u.size() - 8);
}

// The status line (A8-15 (a), closed here): "<source label> . frame N" is the VIEW's, "<name> @ 0x<entry>" and "(run at 0x...)" are the
// SHOWN result's. Read from the glyphs of the row, not asked of the panel.
uint64_t gStatusChecks = 0;
void StatusCheck(Rig& r, const char* where)
{
    if (r.failed || !r.visible) return;
    const std::vector<drawn::Run> runs = drawn::WindowRuns(DecompilerPanel::kTitle);
    const Rig::ViewState vs = r.ViewNow();
    // the row of the source label
    float y = -1;
    for (const drawn::Run& d : runs)
        if (HasParts(d.text, { vs.kind == DecompViewKind::None ? "No source" : "model" })) { y = d.y0; break; }
    if (y < 0) { r.Fail(std::string("STATUS LINE (") + where + "): no row names the source"); return; }
    std::string row;
    for (const drawn::Run& d : runs)
        if (std::fabs(d.y0 - y) < 1.0f && !InkOf(d.col)) row += d.text + " ";
    std::vector<std::string> parts;
    if (vs.kind == DecompViewKind::None) parts = { "No source" };
    else parts = { "model", "frame" + std::to_string(vs.frame) };
    std::string why;
    const DecompileResult* s = r.panel.Shown();
    if (s)
    {
        const uint32_t canon = s->entry;
        const bool mirror = s->requestedEntry != s->entry;
        const auto it = r.shown.names.find(canon);
        // the name: the user's, as it was when the output was made (a rename since marks it stale, it does not rewrite it), else the engine's
        // (only for the answers an engine made: an injected result carries a name of its own)
        const bool real = s->status == DecompileStatus::Ok && r.injectedGens.count(s->generation) == 0;
        if (real && it != r.shown.names.end()) parts.push_back(it->second);
        else if (real && s->name.compare(0, 5, "func_") != 0 && s->name.compare(0, 4, "FUN_") != 0 && s->name.compare(0, 5, "bios_") != 0 && !s->name.empty())
            why = "the name '" + s->name + "' is neither the user's name then nor the engine's";
        char b[32];
        std::snprintf(b, sizeof b, "@0x%08X", canon);
        parts.push_back(b);
        if (mirror) { std::snprintf(b, sizeof b, "(runat0x%08X)", s->requestedEntry); parts.push_back(b); }
        else if (Norm(row).find("(runat") != std::string::npos) why = "'(run at ...)' is drawn for a function run at its own address";
    }
    ++gStatusChecks;
    if (why.empty() && !HasParts(row, parts))
    {
        std::string want;
        for (const std::string& p : parts) want += "[" + p + "]";
        why = "the status row is '" + row + "' and should hold " + want;
    }
    if (!why.empty()) r.Fail(std::string("STATUS LINE (") + where + "): " + why);
}

// The engine's notes that no amber line carries (A8-15 (f)): tallied by shape (digits and hex addresses folded), over every settled step.
std::map<std::string, uint64_t> gGreyOnly;
std::string Shape(std::string w)
{
    std::string o;
    for (size_t i = 0; i < w.size(); ++i)
    {
        if (w.compare(i, 2, "0x") == 0) { o += "0x#"; i += 2; while (i < w.size() && std::isxdigit(static_cast<unsigned char>(w[i]))) ++i; --i; }
        else if (std::isdigit(static_cast<unsigned char>(w[i]))) { o += '#'; while (i + 1 < w.size() && std::isdigit(static_cast<unsigned char>(w[i + 1]))) ++i; }
        else o += w[i];
    }
    return o;
}
void TallyGreyOnly(Rig& r)
{
    const DecompileResult* s = r.panel.Shown();
    if (!s || s->status != DecompileStatus::Ok || r.injectedGens.count(s->generation)) return;
    const std::vector<std::string> amber = r.panel.Uncertainties();
    for (const std::string& w : s->warnings)
    {
        bool carried = false;
        const std::string tail = w.size() > 12 ? w.substr(w.find(": ") == std::string::npos ? 0 : w.find(": ") + 2) : w;
        for (const std::string& a : amber) carried |= a.find(tail.substr(0, std::min<size_t>(tail.size(), 40))) != std::string::npos;
        if (!carried) ++gGreyOnly[Shape(w).substr(0, 110)];
    }
}

void ProbeCheck(Rig& r, const char* where)
{
    CheckTrust(r, where);
    StatusCheck(r, where);
    TallyGreyOnly(r);
    if (std::getenv("SE_PROBE_REASONS") && r.visible && r.panel.ShownStale() && r.panel.GetStaleReason() == DecompilerPanel::StaleReason::NeedsRefresh)
    {
        std::printf("REASON NeedsRefresh at %s: view kind %d, last operations:", where, static_cast<int>(r.ViewNow().kind));
        for (size_t i = r.done.size() > 3 ? r.done.size() - 3 : 0; i < r.done.size(); ++i) std::printf(" | %s", r.done[i].c_str());
        std::printf("\n");
    }
}

void SetupProbe(Rig& rig)
{
    Setup(rig);
    rig.custom = ProbeOp;
    rig.trustCheck = ProbeCheck;
    gStateHook = ContainHook;
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
    bool held = false, hidden = false;
    std::map<int, int> nameOf;   // function -> the name option it has now: the undo
    int containFn = -1;
    auto emitContain = [&](int k) {
        out.push_back(Mk(OpKind::Custom, 0, 0, 0, ("contain " + std::to_string(k)).c_str()));
        containFn = k % static_cast<int>(p.placed.size());
    };   // the function the Containing target is on: its start is not toggled off (the finder would answer another entry: area 4's)
    for (const Op& o0 : ops)
    {
        Op o = o0;
        if (o.kind == OpKind::Bound && o.b == 0 && containFn >= 0 && o.a % static_cast<int>(p.placed.size()) == containFn) o = Mk(OpKind::Refresh);
        if (o.kind == OpKind::Custom && o.text.compare(0, 8, "contain ") == 0) containFn = std::atoi(o.text.c_str() + 8) % static_cast<int>(p.placed.size());
        if (o.kind == OpKind::Custom && (o.text.compare(0, 8, "badentry") == 0 || o.text.compare(0, 11, "containbad") == 0)) {}
        out.push_back(o);
        if (o.kind == OpKind::Hold) held = true;
        if (o.kind == OpKind::Wait || o.kind == OpKind::Finish) held = false;
        if (o.kind == OpKind::Hide) hidden = true;
        if (o.kind == OpKind::Show) hidden = false;
        const int fn = o.a % static_cast<int>(p.placed.size());
        if (o.kind == OpKind::Rename && !held)
        {
            const int before = nameOf.count(fn) ? nameOf[fn] : 0;
            nameOf[fn] = o.b;
            // (not while the panel is hidden: it asks nothing then, and area 1's STALE FOREVER check does not excuse the names generation of a
            // rename undone -- A8-14 -- for a panel that is not asked; R2-I1 in the record)
            if (!hidden && rng.Chance(35))
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
                case 4: emitContain(rng.Below(1000)); break;
                case 5: out.push_back(Mk(OpKind::Hide)); out.push_back(Mk(OpKind::Show)); break;
                case 6: out.push_back(Mk(OpKind::Select, rng.Below(1000))); break;
                case 7: out.push_back(Mk(OpKind::Volatile, rng.Below(4))); break;
                default: out.push_back(Mk(OpKind::Stop)); break;
                }
            }
        }
        // Decompile on a function now and then, as the SH-2 Assembly menu does.
        if (!held && o.kind == OpKind::Select && rng.Chance(12))
            emitContain(rng.Below(1000));
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
    std::printf("status line checks: %llu\n", static_cast<unsigned long long>(gStatusChecks));
    for (const auto& g : gGreyOnly) std::printf("grey-only engine note: %8llu  %s\n", static_cast<unsigned long long>(g.second), g.first.c_str());
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


// D8. Copy Function / Copy Line on output the panel says is unsupported or stale: does the text that leaves the panel say so?
void D8()
{
    for (int stale = 0; stale < 2; ++stale)
    {
        MiniRig m;
        m.placed = { FnC, FnU };
        m.backend.content = BuildImage(m.placed, 0);
        m.panel.ShowFunctionContaining(0, Defs()[FnU].entry + 4);
        m.Idle(3);
        CHECK(m.Settle());
        if (stale)
        {
            m.gate.Close();
            Edit(m);
            m.Idle(3);
            CHECK(WaitHeldOrBusy(m));
            m.Idle(2);
        }
        ImVec2 a, b;
        CHECK(m.panel.LineRect(1, a, b));
        const ImVec2 point((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        m.h.Hover(point);
        m.h.RightClick(point);
        m.Idle(2);
        ImVec2 ma, mb;
        if (!m.panel.ItemRect("Copy Function", ma, mb)) { Report("D8 no Copy Function item", false); m.gate.Open(); continue; }
        const ImVec2 c((ma.x + mb.x) * 0.5f, (ma.y + mb.y) * 0.5f);
        m.h.Hover(c);
        m.h.Click(c);
        m.Idle(2);
        const char* clip = ImGui::GetClipboardText();
        const std::string text = clip ? clip : "";
        if (!stale) std::printf("---- clipboard ----\n%s\n---- end ----\n", text.c_str());
        const bool says = text.find("trustworthy") != std::string::npos || text.find("stale") != std::string::npos || text.find("unsupported") != std::string::npos;
        std::printf("      %s output (stale %d, unsupported %d): Copy Function put %zu bytes on the clipboard, first line '%s'\n", stale ? "stale" : "current",
                    m.panel.ShownStale(), m.panel.Shown() && m.panel.Shown()->unsupported, text.size(), text.substr(0, text.find('\n')).c_str());
        Report(stale ? "D8b Copy Function on stale output says so" : "D8a Copy Function on unsupported output says so", says, "");
        m.gate.Open();
    }
}


std::string DoubtOf(const DecompilerPanel& p)
{
    for (const std::string& u : p.Uncertainties())
        if (u.compare(0, 7, "Entry: ") == 0 && u.size() > 8) return u.substr(7, u.size() - 8);
    return "";
}

// D9. Auto-refresh on a running emulator (A8-15 (e)): the PC leaves the function, the debounce passes, the panel asks by itself. Held to
// the model with the engine holding that request, and after it.
void D9()
{
    const uint32_t cEntry = Defs()[FnC].entry, aEntry = Defs()[FnA].entry;
    MiniRig m;
    m.kind = DecompViewKind::LiveRunning;
    m.regs[0].pc = cEntry + 4;
    m.haveRegs[0] = true;
    m.panel.SetAutoRefresh(true);
    m.Idle(3);
    CHECK(m.Settle());
    m.gate.Close();
    m.regs[0].pc = aEntry + 4;
    bool held = false;
    for (int i = 0; i < 120 && !held; ++i)
    {
        ++m.frame;
        ++m.serial;
        m.Idle(1);
        held = m.runner.Busy() && m.gate.Held() > 0;
        if (!held && m.runner.Busy()) std::this_thread::yield();
    }
    if (!held) { Report("D9 auto-refresh asked nothing within 120 frames", false); m.gate.Open(); return; }
    ++m.frame; ++m.serial; m.Idle(2);
    trust::TrueState t;
    t.liveEmulator = true;
    t.running = true;
    t.inputsDiffer = true;
    t.frameMoved = true;
    t.outstanding = true;
    t.entryDoubt = DoubtOf(m.panel);
    t.entryDoubt.clear();   // the shown output is still C's: its entry is not the one the finder now doubts
    std::string err = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, true);
    Report("D9a auto-refresh in flight: [stale: frame N] and decompiling", err.empty(), err);
    m.gate.Open();
    for (int i = 0; i < 400 && (m.runner.Busy() || m.panel.Pending()); ++i)
    {
        ++m.frame; ++m.serial;
        m.Idle(1);
        std::this_thread::yield();
    }
    ++m.frame; ++m.serial; m.Idle(2);
    const DecompileResult* s = m.panel.Shown();
    std::printf("      after the answer: shown entry %08X, stale %d, reason %d, pending %d\n", s ? s->requestedEntry : 0, m.panel.ShownStale(),
                static_cast<int>(m.panel.GetStaleReason()), m.panel.Pending());
    t.outstanding = false;
    t.entryDoubt = DoubtOf(m.panel);
    err = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, true);
    Report("D9b auto-refresh answered, the emulator running on: [stale: frame N], press Refresh", err.empty() && s && s->requestedEntry == aEntry, err);
}


// D10. Is the badge on screen on a narrow panel? The badge is the last item of the status row; ImGui clips what does not fit and the window has
// no horizontal scrollbar. Width of the row up to and after the badge, against the window, for a running emulator (function name, entry,
// "(entry from ...)", "decompiling...", "[stale: frame N]").
void D10()
{
    gMiniLabel = "live, running";
    // the default layout's panel: 42% of the work area (bottom strip: Watch 40%, Breakpoints 30% of the rest), at four common widths
    for (float screen : { 1280.0f, 1600.0f, 1920.0f, 2560.0f })
    {
        const float w = std::floor(screen * 0.60f * 0.70f);
        for (int frames : { 1234, 123456 })
        {
            gMiniWinW = w;
            MiniRig m;
            m.kind = DecompViewKind::LiveRunning;
            m.regs[0].pc = Defs()[FnC].entry + 4;
            m.haveRegs[0] = true;
            m.frame = static_cast<uint64_t>(frames);
            m.Idle(3);
            CHECK(m.Settle());
            m.gate.Close();
            ++m.frame; ++m.serial;
            ClickMini(m, "refresh");
            WaitHeldOrBusy(m);
            ++m.frame; ++m.serial;
            m.Idle(3);
            ImVec2 a, b;
            const bool have = m.panel.ItemRect("stale", a, b);
            ImGuiWindow* win = ImGui::FindWindowByName(DecompilerPanel::kTitle);
            const float right = win ? win->Pos.x + win->Size.x : 0;
            bool badgeDrawn = false, badgeCut = false, pendingDrawn = false;
            for (const drawn::Run& r : drawn::WindowRuns(DecompilerPanel::kTitle))
            {
                const std::string n = Norm(r.text);
                if (n.find("[stale") != std::string::npos)
                {
                    if (n.back() == ']') badgeDrawn = true; else badgeCut = true;
                }
                if (n.find("decompiling") != std::string::npos) pendingDrawn = true;
            }
            std::printf("      screen %4.0f px -> panel %4.0f px, frame %6d: badge item ends at %.0f, badge whole %d (cut %d), 'decompiling...' drawn %d, scroll range %.0f\n",
                        screen, w, frames, have ? b.x : 0.0f, badgeDrawn, badgeCut, pendingDrawn, win ? win->ScrollMax.x : -1.0f);
            Report((std::string("D10 stale badge and decompiling... are on screen: panel ") + std::to_string(static_cast<int>(w)) + " px, frame " +
                    std::to_string(frames)).c_str(), badgeDrawn && pendingDrawn, "");
            m.gate.Open();
        }
    }
    gMiniWinW = 1250.0f;
    gMiniLabel = "mini";
}

// D11. The amber lines of a narrow panel: the unsupported line is the longest sentence the panel draws in amber; which of it is on screen?
void D11()
{
    for (float w : { 538.0f, 672.0f, 806.0f })
    {
        gMiniWinW = w;
        MiniRig m;
        m.placed = { FnC, FnU };
        m.backend.content = BuildImage(m.placed, 0);
        m.panel.ShowFunctionContaining(0, Defs()[FnU].entry + 4);
        m.Idle(3);
        CHECK(m.Settle());
        std::string amber;
        for (const drawn::Run& r : drawn::WindowRuns(DecompilerPanel::kTitle))
            if (InkOf(r.col) == 1 && HasParts(r.text, { "PC-relative" })) amber = r.text;
        const bool whole = Norm(amber).find("nottrustworthy") != std::string::npos;
        std::printf("      panel %4.0f px: the unsupported line as drawn: '%s'\n", w, amber.c_str());
        Report((std::string("D11 the whole unsupported sentence is on screen: panel ") + std::to_string(static_cast<int>(w)) + " px").c_str(), whole, "");
    }
    gMiniWinW = 1250.0f;
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
            D8();
            D9();
            D10();
            D11();
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
