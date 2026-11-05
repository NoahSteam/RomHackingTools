// Area 8 of the review program (Docs/Decompiler/REVIEW_PROGRAM.md): the trust indicators -- what the panel tells the
// reader about whether the text on screen can be trusted. The stale badge and its tooltip, "decompiling...", the dimming
// of old output, and the amber and red lines: unsupported, truncated, unimplemented, unavailable, too large, failed,
// the engine's warnings, the disabled session.
//
// The oracle is a state model (DecompilerTrustModel.h): from the TRUE state -- the inputs on screen, the inputs the shown
// result was made from, whether a request is in flight or stopped, the result's status and what it says about itself, the
// session-disabled state -- it says what each indicator must show. It was written from the guide's table, not from the
// panel's code; this test reads the guide and fails if a phrase the model relies on is not in it.
//
// The driver is area 1's rig (DecompilerModelTests.cpp, included here with its main left out): the real panel, runner and
// engine under random sequences of edit, scrub, Refresh, reconnect, source replacement, BIOS, volatile list, names and
// function starts, Stop, hide and show, and the emulator running -- extended with the outcomes a decompilation can have:
//
//   Ok, Truncated (function T: its flow leaves captured memory), Unsupported (U: a PC-relative load in a delay slot other
//   code jumps to), several amber lines (W: five stores into the BIOS ROM), TooLarge (the budget, as in area 1),
//   Unavailable (the language files cannot be put back: 't-breakcfg'), a session disabled (a cancel the engine does not
//   acknowledge, 'wedge'; a failure inside the worker, 't-engfail'), Stopped, and the results no engine run reaches here
//   but the panel must take (Failed out of memory, NoCode, an engine damaged earlier, Unimplemented, a result with
//   more amber lines than the panel shows, 't-inject <kind>': offered to DecompilerPanel::Accept for the request the
//   engine is holding), and the targets that find no function ('t-badentry', 't-containbad').
//
// At every settled step (the engine held, or idle) the model's Expected is compared with the panel two ways: its state
// functions (ShownStale, GetStaleReason, StaleTooltip) and what it DREW, read back from ImGui's draw lists glyph by glyph
// (DrawnText.h): the amber and red runs of the window in order, the brightness of the code lines, the badge's rectangle,
// and the tooltip window that opens when the badge is hovered. Invariants held at every step:
//
//   I1  output never looks current (undimmed, no badge) while its inputs differ from the screen's;
//   I2  a truncated, unsupported, unimplemented or too-large result never presents as complete;
//   I3  a message never names the wrong reason (a tooltip has its reason's phrase and no other's);
//   I4  every indicator the guide documents is reached (the coverage list at the end of the run).
//
// usage: SaturnExplorerDecompilerTrustTests <scratch dir>
//   SE_TRUST_STEPS=<n>   run at least n operations per outcome mix (default: a short run)
//   SE_TRUST_MIX=<k>     only mix k (see kMixNames); SE_TRUST_FROM=<seed> starts it there, to run a long mix in pieces
//   SE_TRUST_SEED=<k>    (with SE_TRUST_MIX) one sequence of that mix, every step printed; SE_TRUST_SKIP=3,5 and SE_TRUST_LIMIT=n shrink it
//   SE_TRUST_FOLLOW_SEED=<k>  one sequence of the Follow PC rig (mix 8), every step printed; with SE_TRUST_FOLLOW_AUTO=1 one of mix 9
//   SE_TRUST_FOLLOW_SCRIPT="cpu; regsoff; cpu"   one Follow PC sequence written out (kFollowWords); SE_TRUST_FOLLOW_DUMP=1 prints the window
//   SE_TRUST_SCRIPT="select A; t-inject failed" SE_MODEL_WORLD=C,A   one sequence written out (area 1's words, and t-...)
//   SE_TRUST_NOHOVER=1   do not hover the badge (faster)
//   SE_TRUST_HIDDEN_RENAME=1  half the renames made and undone while the panel is hidden (A8-25), with the mixes
//   SE_TRUST_FOLLOW_FOCUS=1   mixes 8 and 9 also click into the entry box without an edit (Enter on it across an install, A8-42)
//   SE_TRUST_PROBE8=1    (area 8, round 8) hand-built sequences against the A8-42 and A8-43 fixes, printed; not in the default run
//   SE_TRUST_FOLLOW_CPU=1     mixes 8 and 9 with the CPU switch, a CPU without registers, the PC in the BIOS and Stop four times as often
#define SE_MODEL_NO_MAIN
#include "DecompilerModelTests.cpp"

#include "AllocFailure.h"
#include "DecompilerTrustModel.h"
#include "DrawnText.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <new>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{

// ---- What the run covered ------------------------------------------------------------------------------------------

std::map<std::string, uint64_t> gCov;   // indicator -> times it was drawn and held to the model
void Cov(const std::string& id) { ++gCov[id]; }

struct TrustStats
{
    uint64_t checks = 0, held = 0, idle = 0, drawn = 0, hovers = 0, programs = 0, steps = 0, injected = 0;
} gT;

bool gHover = true;

// ---- The result as the model sees it -------------------------------------------------------------------------------

trust::Status MapStatus(DecompileStatus s)
{
    switch (s)
    {
    case DecompileStatus::Ok: return trust::Status::Ok;
    case DecompileStatus::NoCode: return trust::Status::NoCode;
    case DecompileStatus::Cancelled: return trust::Status::Cancelled;
    case DecompileStatus::Failed: return trust::Status::Failed;
    case DecompileStatus::Unavailable: return trust::Status::Unavailable;
    case DecompileStatus::TooLarge: return trust::Status::TooLarge;
    case DecompileStatus::Disabled: return trust::Status::Disabled;
    }
    return trust::Status::Failed;
}

trust::ShownFacts FactsOf(const DecompileResult& r)
{
    trust::ShownFacts f;
    f.present = true;
    f.status = MapStatus(r.status);
    f.error = r.error;
    f.hasLines = !r.lines.empty();
    f.frame = r.key.frameNumber;
    f.unsupported = r.unsupported;
    f.unsupportedAt = r.unsupportedAt;
    f.truncated = r.truncated;
    f.truncatedAt = r.truncatedAt;
    f.truncatedWhy = r.truncatedWhy;
    f.truncatedCount = r.truncatedCount;
    f.badDataAt = r.badDataAt;
    f.unimplemented = r.unimplemented;
    // The cut places and the addressed bad-data places are said by their own lines ("Flows into ... (and at N more places)");
    // every other warning is a line of its own, and so is each hardware caution (the explanations are shown on their lines).
    for (const FlowWarning& w : r.flowWarnings)
    {
        if (w.kind == FlowWarning::Cut || (w.kind == FlowWarning::BadData && w.address != 0))
        {
            f.summarised.push_back(trust::Hex8(w.address) + ": " + w.text);   // said by the "Flows into ..." and "Flow runs into bytes ..." lines
            continue;
        }
        f.others.push_back({ w.address, w.text });
    }
    for (const HardwareNote& n : r.hardwareNotes)
        if (n.caution) f.others.push_back({ n.address, n.text });
    f.engineWarnings = r.warnings.size();
    f.notes = r.warnings;
    f.body = r.body;
    f.literalPools = r.literalPools;
    f.flowWarnings = r.flowWarnings.size();
    f.entry = r.entry;
    return f;
}

trust::NoEntry NoEntryOf(int code)
{
    switch (code)
    {
    case 1: return trust::NoEntry::EntryRefused;
    case 2: return trust::NoEntry::EntryNotCode;
    case 3: return trust::NoEntry::ContainingNotFound;
    case 4: return trust::NoEntry::NoRegisters;
    case 5: return trust::NoEntry::FollowNotFound;
    case 6: return trust::NoEntry::NothingPinned;
    default: return trust::NoEntry::None;
    }
}

// A hook for an instrument that knows a fact of the true state the rig does not keep (area 8, round 2: the target is Decompile, with its
// anchor); empty in the default run.
std::function<void(Rig&, trust::TrueState&)> gStateHook;

// The width of the panel's window in the area 1 rig, in pixels: 1250 wide enough for every line whole; SE_TRUST_WIDTH=537 is the default
// layout's panel on a 1280 px window (A8-17).
float gRigWinW = 1250.0f;

// The Containing target is the last thing that chose the question when the last operation with an effect was t-contain: the anchor is the
// function's entry, and the finder's own words about it (area 4's) are taken as the panel gives them, so that what is held is their
// presence and place, not their text.
void ContainState(Rig& r, trust::TrueState& t)
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

// The true state, from the rig's own bookkeeping of what it did to the world (never from the panel's view of it, but for
// the two facts only the panel has: that a request is outstanding, and which one).
trust::TrueState StateOf(Rig& r)
{
    trust::TrueState t;
    t.visible = r.visible;
    const Rig::ViewState vs = r.ViewNow();
    t.haveSource = vs.kind != DecompViewKind::None;
    t.liveEmulator = vs.kind == DecompViewKind::LivePaused || vs.kind == DecompViewKind::LiveRunning;
    t.running = vs.kind == DecompViewKind::LiveRunning;
    if (r.haveShown)
    {
        const Sig now = r.ViewSig();
        // A rename undone is a rename: the output stays marked until it is replaced (guide, "stale").
        t.inputsDiffer = !r.shown.Same(now) || r.shownNamesGen != r.namesGeneration;
        t.frameMoved = t.liveEmulator && r.shown.source == now.source && r.shown.frame != now.frame;
    }
    // A request still running for a question that has since found no function is not the replacement of anything on screen.
    const bool pending = r.panel.Pending() && r.noEntry == 0;
    t.stopping = pending && r.panel.PendingGeneration() == r.stopGen;
    t.outstanding = pending && !t.stopping;
    t.stopped = r.haveStopped && r.noEntry == 0 && r.SigOf(r.held).Same(r.stopped);
    t.sessionDisabled = r.runner.Disabled();
    t.disabledReason = r.runner.DisabledReason();
    t.noEntry = NoEntryOf(r.noEntry);
    t.askedAddress = 0x00001FF0u;
    ContainState(r, t);
    if (gStateHook) gStateHook(r, t);
    return t;
}

// ---- Reading the window --------------------------------------------------------------------------------------------

// Spaces and line breaks do not count, and a character outside ASCII (the ellipsis) is one wildcard: the glyph the
// atlas gives it is the font's business, not the panel's.
std::string Norm(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size();)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == ' ' || c == '\n' || c == '\t' || c == 1) { ++i; continue; }
        if (c < 0x80) { out += static_cast<char>(c); ++i; continue; }
        out += '?';
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    }
    return out;
}

// 'parts' occur in the drawn text in order. A line wider than the window is cut where the window ends (ImGui emits no
// glyph past the clip rectangle): then the drawn text may end inside a part, or before it.
bool HasParts(const std::string& drawnText, const std::vector<std::string>& parts, bool clipped = false)
{
    const std::string d = Norm(drawnText);
    size_t pos = 0;
    for (const std::string& p : parts)
    {
        const std::string q = Norm(p);
        const size_t f = d.find(q, pos);
        if (f != std::string::npos)
        {
            pos = f + q.size();
            continue;
        }
        // Not found whole: acceptable only if the text was cut and what is left of it is the start of this part.
        return clipped && q.compare(0, d.size() - pos, d, pos, std::string::npos) == 0;
    }
    return true;
}

// 1 amber, 2 red, 0 anything else (the body text, the greys).
int InkOf(ImU32 col)
{
    const ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    auto is = [&](float r, float g, float b) { return std::fabs(c.x - r) < 0.03f && std::fabs(c.y - g) < 0.03f && std::fabs(c.z - b) < 0.03f; };
    if (is(0.95f, 0.75f, 0.30f)) return 1;
    if (is(0.95f, 0.42f, 0.42f)) return 2;
    return 0;
}

// ---- Wrapped lines ------------------------------------------------------------------------------------------------------
//
// The panel wraps its amber and red sentences and the pieces of its status row at the window's edge (A8-17), so one sentence can be
// drawn as several runs, one a row. A run continues the one before it when it is on a row below, starts where that line started,
// has the same ink, and its first word would not have fitted after the end of the run before it: the rule text wrapping follows.
bool ContinuesWrap(const drawn::Run& prev, const drawn::Run& cur, float lineX0, float workRight)
{
    if (InkOf(prev.col) != InkOf(cur.col) || cur.y0 < prev.y0 + 4.0f || std::fabs(cur.x0 - lineX0) > 3.0f) return false;
    const size_t sp = cur.text.find(' ');
    const std::string word = cur.text.substr(0, sp);
    const float need = ImGui::CalcTextSize(word.c_str()).x + ImGui::CalcTextSize(" ").x;
    return prev.x1 + need > workRight - 3.0f;
}

struct Exp
{
    int ink;
    std::vector<std::string> parts;
    std::string what;
};

std::string Dump(const std::vector<Exp>& v)
{
    std::string o;
    for (const Exp& e : v)
    {
        o += std::string("    ") + (e.ink == 1 ? "amber " : "red   ");
        for (const std::string& p : e.parts) o += "[" + p + "]";
        o += "  (" + e.what + ")\n";
    }
    return o.empty() ? "    (none)\n" : o;
}

// Some line of the (multi-line) tooltip has the parts, in order. A tooltip line wider than the display is cut there, as any text is.
bool AnyLine(const std::string& tip, const std::vector<std::string>& parts)
{
    size_t from = 0;
    while (from <= tip.size())
    {
        const size_t nl = tip.find('\n', from);
        const std::string line = tip.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        if (!line.empty() && HasParts(line, parts, true)) return true;
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    return false;
}

// The engine's notes that no amber line carries (A8-15 (f)): the grey "N warnings" count, and its tooltip, is all the panel says of them. Each
// is listed here and in the guide ("The engine's notes"); a note the engine makes that is neither carried by an amber line nor listed fails the
// run, so that a new note which should be amber cannot reach only the grey count unseen. (The first four are conditions of the request that
// do not make the output wrong; the last accompanies the amber "Unimplemented instruction" line.)
const char* const kGreyOnlyNotes[] = {
    "code was removed as unreachable and could not be checked for hardware accesses",
    "is inverted: ignored",
    "could not register",
    "not found: access widths and counts may differ from the code",
    "the SH-2 spec has no semantics for an instruction here",
};

bool GreyWarningText(const std::string& text, size_t n)
{
    const std::string s = Norm(text);
    return s == std::to_string(n) + "warnings" || (n == 1 && s == "1warning");
}

bool ReasonMatches(DecompilerPanel::StaleReason pr, trust::Reason mr)
{
    using R = DecompilerPanel::StaleReason;
    switch (mr)
    {
    case trust::Reason::None: return pr == R::None;
    case trust::Reason::Replacement: return pr == R::Pending;
    case trust::Reason::Stopped: return pr == R::Stopped;
    case trust::Reason::Disabled: return pr == R::Disabled;
    default: return pr == R::NeedsRefresh;
    }
}

// Every phrase of a tooltip that names a reason: a tooltip has its own and no other.
std::string TooltipProblem(const std::string& tip, trust::Reason want, const char* how)
{
    if (want == trust::Reason::None) return tip.empty() ? "" : std::string(how) + " is shown for output that is not stale: " + tip;
    if (!HasParts(tip, { trust::TooltipPhrase(want) }))
        return std::string(how) + " does not give the reason " + trust::ReasonName(want) + " (\"" + trust::TooltipPhrase(want) + "\"):\n    " + tip;
    for (int k = 1; k <= static_cast<int>(trust::Reason::NothingAsked); ++k)
    {
        const trust::Reason other = static_cast<trust::Reason>(k);
        if (other == want) continue;
        if (HasParts(tip, { trust::TooltipPhrase(other) }))
            return std::string(how) + " names the wrong reason: it should give " + trust::ReasonName(want) + " and it says " +
                   trust::ReasonName(other) + ":\n    " + tip;
    }
    return "";
}

// One comparison of the model with the panel. 'frame' draws one more frame with the mouse where it is told (to hover the
// badge). Returns the problems, "" when the panel agrees with the model.
std::string Verify(DecompilerPanel& panel, const trust::TrueState& t, const std::function<void(ImVec2)>& frame, bool hover)
{
    const DecompileResult* sh = panel.Shown();
    const trust::ShownFacts facts = sh ? FactsOf(*sh) : trust::ShownFacts();
    const trust::Expected e = trust::Expect(t, facts);
    std::string err;
    auto bad = [&](const std::string& m) { err += m + "\n"; };

    // ---- the state functions
    // A hidden panel tells the reader nothing, and keeps what it knew when it was hidden (it resolves nothing, asks nothing): what it
    // says is judged once it is drawn.
    if (t.visible && panel.ShownStale() != e.stale)
        bad(std::string(e.stale ? "I1 STALE NOT MARKED: the inputs differ (or the target finds no function) and ShownStale() is false"
                                : "STALE MARKED FOR CURRENT OUTPUT: ShownStale() is true and the output answers the inputs on screen"));
    // A hidden panel tells the reader nothing, and keeps what it knew when it was hidden: its reason is judged once it is drawn.
    if (t.visible)
    {
        if (!ReasonMatches(panel.GetStaleReason(), e.reason))
            bad(std::string("REASON: the model says ") + trust::ReasonName(e.reason) + ", GetStaleReason() is " +
                std::to_string(static_cast<int>(panel.GetStaleReason())) + " (0 None, 1 Pending, 2 Stopped, 3 Disabled, 4 NeedsRefresh)");
        const std::string p = TooltipProblem(panel.StaleTooltip(), e.reason, "I3 StaleTooltip()");
        if (!p.empty()) bad(p);
    }
    if (!t.visible) return err;

    // ---- what was drawn
    ++gT.drawn;
    const std::vector<drawn::Run> runs = drawn::WindowRuns(DecompilerPanel::kTitle);
    std::vector<const drawn::Run*> inks;
    for (const drawn::Run& r : runs)
        if (InkOf(r.col)) inks.push_back(&r);

    std::vector<Exp> exp;
    if (e.stoppingNote) exp.push_back({ 1, { "stopping\xe2\x80\xa6" }, "stopping" });
    if (e.decompiling) exp.push_back({ 1, { "decompiling\xe2\x80\xa6" }, "decompiling" });
    if (e.stale)
    {
        std::vector<std::string> badge;
        if (e.badgeFrame) badge = { "[stale: frame " + std::to_string(e.badgeFrameN) + "]" };
        else badge = { "[stale]" };
        exp.push_back({ 1, badge, e.badgeFrame ? "the frame badge" : "the plain badge" });
    }
    for (const trust::Line& l : e.lines) exp.push_back({ l.ink == trust::Ink::Amber ? 1 : 2, l.parts, l.what });

    ImGuiWindow* win = ImGui::FindWindowByName(DecompilerPanel::kTitle);
    const float right = win ? win->Pos.x + win->Size.x : 1e9f;
    const float workRight = win ? win->WorkRect.Max.x : 1e9f;
    // What was drawn, a sentence wrapped over several rows put back together. No allowance for text cut by the window's edge (A8-15 (d),
    // A8-17): the panel wraps, so every line is whole, and a glyph past the edge is a failure.
    // The runs are matched against the lines the model expects, in order: a sentence takes the next run and, while it is not whole, the rows
    // that continue it; a continuation that could as well be the start of the next expected line is that line's.
    std::vector<Exp> got;
    {
        auto startsLike = [](const std::string& drawnText, const Exp& e) {
            const std::string q = Norm(e.parts.empty() ? std::string() : e.parts[0]), d = Norm(drawnText);
            const size_t n = std::min<size_t>(std::min(q.size(), d.size()), 12);
            return n >= 4 && q.compare(0, n, d, 0, n) == 0;
        };
        size_t j = 0;
        for (size_t i = 0; j < inks.size(); ++i)
        {
            const drawn::Run* first = inks[j];
            Exp g{ InkOf(first->col), { first->text }, "drawn" };
            const float lineX0 = first->x0;
            size_t k = j + 1;
            while (k < inks.size() && ContinuesWrap(*inks[k - 1], *inks[k], lineX0, workRight))
            {
                const bool whole = i < exp.size() && g.ink == exp[i].ink && HasParts(g.parts[0], exp[i].parts);
                if (whole && i + 1 < exp.size() && startsLike(inks[k]->text, exp[i + 1])) break;
                g.parts[0] += " " + inks[k]->text;
                ++k;
            }
            for (size_t m = j; m < k; ++m)
                if (inks[m]->x1 > right + 0.5f)
                    bad("CLIPPED: '" + inks[m]->text + "' is drawn past the window's right edge (" + std::to_string(static_cast<int>(inks[m]->x1)) +
                        " px, the window ends at " + std::to_string(static_cast<int>(right)) + ")");
            got.push_back(g);
            j = k;
        }
    }
    bool same = got.size() == exp.size();
    for (size_t i = 0; same && i < got.size(); ++i) same = got[i].ink == exp[i].ink && HasParts(got[i].parts[0], exp[i].parts);
    if (!same)
    {
        // Which invariant: a missing line of the kinds that must never be missing says so.
        std::string which = "AMBER AND RED LINES";
        for (const Exp& x : exp)
        {
            bool found = false;
            for (size_t gi = 0; gi < got.size(); ++gi) found |= got[gi].ink == x.ink && HasParts(got[gi].parts[0], x.parts);
            if (found) continue;
            if (x.what == "truncated") which = "I2 TRUNCATED RESULT PRESENTED AS COMPLETE";
            else if (x.what == "too large") which = "I2 TOO-LARGE RESULT PRESENTED AS COMPLETE";
            else if (x.what == "unsupported") which = "I2 UNSUPPORTED RESULT PRESENTED AS TRUSTWORTHY";
            else if (x.what == "the plain badge" || x.what == "the frame badge")
            {
                bool someBadge = false;
                for (const Exp& g : got) someBadge |= g.ink == 1 && HasParts(g.parts[0], { "[stale" });
                which = someBadge ? "BADGE FORM (" + x.what + " expected)" : "I1 OUTPUT LOOKS CURRENT (no badge)";
            }
            else if (which == "AMBER AND RED LINES") which = "LINE MISSING (" + x.what + ")";
        }
        bad(which + ": the model expects these amber and red lines in this order\n" + Dump(exp) + "  and the panel drew\n" + Dump(got));
    }
    // (area 8 fixer, A8-36, A8-37) The status row's "(entry from ...)" is the verdict on the entry of the output on screen, when the driver
    // knows what confirms it: a pin is not confirmed by being kept, a typed entry is the user's, a symbol erased is no source.
    if (e.labelChecked)
    {
        std::string all;
        for (const drawn::Run& r : runs) all += r.text + " ";
        const std::string n = Norm(all);
        const size_t at = n.find("(entryfrom");
        const std::string label = at == std::string::npos ? std::string() : n.substr(at, n.find(')', at) == std::string::npos ? std::string::npos : n.find(')', at) - at + 1);
        bool ok = false;
        std::string want;
        for (const std::string& l : e.labels)
        {
            ok |= Norm(l) == label;
            want += "'" + (l.empty() ? std::string("no label") : l) + "' ";
        }
        if (!ok) bad("ENTRY LABEL: the status row says '" + (label.empty() ? std::string("no label") : label) + "' and what confirms the entry now gives " + want);
        else Cov("label: the entry's source held to what confirms it");
    }

    // I2, directly: an unimplemented result has a line that says so.
    if (facts.present && facts.status == trust::Status::Ok && facts.unimplemented)
    {
        bool says = false;
        for (const Exp& g : got) says |= g.ink == 1 && (HasParts(g.parts[0], { "nimplemented" }) || HasParts(g.parts[0], { "nosemantics" }));
        if (!says) bad("I2 UNIMPLEMENTED RESULT PRESENTED AS COMPLETE: the result says the spec has no semantics for an instruction and no amber line does");
    }

    // The code lines: drawn when the result has lines, at the model's brightness.
    {
        ImGuiWindow* child = drawn::ChildOf(DecompilerPanel::kTitle);
        float maxAlpha = 0.0f;
        int glyphRuns = 0;
        if (child)
            for (const drawn::Run& r : drawn::Runs(child->DrawList))
            {
                ++glyphRuns;
                maxAlpha = std::max(maxAlpha, r.Alpha());
            }
        if (e.outputShown)
        {
            if (glyphRuns == 0) bad("OUTPUT NOT DRAWN: the result has lines and the code list drew none");
            else if (std::fabs(maxAlpha - e.alpha) > 0.03f)
            {
                char b[200];
                std::snprintf(b, sizeof b, "%s: the code is drawn at brightness %.2f and the model says %.2f",
                              e.stale && maxAlpha > e.alpha ? "I1 STALE OUTPUT NOT DIMMED" : "DIMMING", maxAlpha, e.alpha);
                bad(b);
            }
        }
        else if (glyphRuns > 0)
            bad("OUTPUT DRAWN: code is drawn for a result with no lines or a status that is not Ok");
    }

    // The badge is a widget of its own, and the grey count of warnings is exactly the result's.
    {
        ImVec2 a, b;
        const bool have = panel.ItemRect("stale", a, b);
        if (have != e.stale) bad(std::string("BADGE WIDGET: the badge was ") + (have ? "drawn" : "not drawn") + " and the model says " + (e.stale ? "drawn" : "not drawn"));
    }
    {
        int grey = 0;
        for (const drawn::Run& r : runs)
            if (!InkOf(r.col) && GreyWarningText(r.text, e.greyCount)) ++grey;
        if (e.greyWarnings && grey != 1) bad("GREY WARNINGS: the result has " + std::to_string(e.greyCount) + " engine notes and the panel drew the count " + std::to_string(grey) + " times");
        if (!e.greyWarnings)
            for (const drawn::Run& r : runs)
                if (!InkOf(r.col))
                {
                    const std::string n = Norm(r.text);
                    if (n.size() > 7 && n.compare(n.size() - 7, 7, "warning") == 0 && std::isdigit(static_cast<unsigned char>(n[0])))
                        bad("GREY WARNINGS: " + n + " drawn for a result with no engine notes");
                    if (n.size() > 8 && n.compare(n.size() - 8, 8, "warnings") == 0 && std::isdigit(static_cast<unsigned char>(n[0])))
                        bad("GREY WARNINGS: " + n + " drawn for a result with no engine notes");
                }
    }

    // Every engine note is carried by an amber line (drawn, or behind "N more warnings") or listed as grey only (A8-15 (f)).
    if (facts.present && facts.status == trust::Status::Ok)
    {
        std::string amberText;
        for (const Exp& g : got) amberText += g.parts[0] + " ";
        for (const trust::Line& l : e.overflow)
            for (const std::string& part : l.parts) amberText += part;
        const std::string amberNorm = Norm(amberText);
        for (const std::string& n : facts.notes)
        {
            if (std::find(facts.summarised.begin(), facts.summarised.end(), n) != facts.summarised.end()) continue;
            const size_t colon = n.size() > 12 ? n.find(": ") : std::string::npos;
            const std::string tail = colon == std::string::npos ? n : n.substr(colon + 2);
            if (amberNorm.find(Norm(tail.substr(0, 40))) != std::string::npos) continue;
            bool listed = false;
            for (const char* g : kGreyOnlyNotes)
                if (n.find(g) != std::string::npos)
                {
                    listed = true;
                    Cov(std::string("grey-only note: ") + g);
                }
            if (!listed)
                bad("GREY-ONLY NOTE NOT CLASSIFIED: the engine's note '" + n + "' is on no amber line and is not in the list of notes that are grey only "
                    "(kGreyOnlyNotes, and the guide's \"The engine's notes\"): a note that should be amber would reach the reader as a count");
        }
    }

    // The grey messages: with no source, nothing yet, after the user's Stop.
    {
        for (const std::string& m : e.greyMust)
        {
            bool found = false;
            for (const drawn::Run& r : runs) found |= !InkOf(r.col) && HasParts(r.text, { m });
            if (!found) bad("GREY MESSAGE MISSING: '" + m + "' is expected (no source, nothing decompiled yet, or the user's Stop) and was not drawn");
        }
        for (const std::string& m : e.greyMustNot)
            for (const drawn::Run& r : runs)
                if (!InkOf(r.col) && HasParts(r.text, { m })) bad("GREY MESSAGE WRONG: '" + m + "' is drawn and the true state has no such thing: " + r.text);
    }

    // Copy Line and Copy Function put the marks in front of the text, as C comments (A8-21): a stale mark when the badge is up, one WARNING
    // comment for each amber uncertainty line, nothing for output that is current and certain, and no comment that ends early.
    {
        const std::string marks = panel.CopyMarks();
        const bool ok = facts.present && facts.status == trust::Status::Ok;
        size_t warnings = 0;
        for (size_t at = 0; (at = marks.find("/* WARNING: ", at)) != std::string::npos; ++at) ++warnings;
        const bool staleMark = marks.find("/* [stale") != std::string::npos;
        if (staleMark != (ok && e.stale))
            bad(std::string("COPY MARKS: ") + (staleMark ? "a stale mark is put before the copied text of output that is current" : "no stale mark is put before the copied text of stale output"));
        if (warnings != (ok ? static_cast<size_t>(e.uncertain) : 0))
            bad("COPY MARKS: " + std::to_string(warnings) + " warning comments before the copied text, and the screen carries " + std::to_string(ok ? e.uncertain : 0) +
                " amber lines");
        for (size_t at = 0; at < marks.size();)
        {
            const size_t nl = marks.find('\n', at);
            const std::string line = marks.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
            if (nl == std::string::npos || line.compare(0, 3, "/* ") != 0 || line.size() < 6 || line.compare(line.size() - 3, 3, " */") != 0 ||
                line.find("*/") != line.size() - 2)
            {
                bad("COPY MARKS: not a one-line C comment: " + line);
                break;
            }
            at = nl + 1;
        }
        if (ok && !marks.empty()) Cov("copy marks drawn into the clipboard text");
    }

    // ---- the tooltips, as they open over the widgets
    if (hover && err.empty())
    {
        // Hold the mouse over a rectangle and read the tooltip window back.
        auto tipOver = [&](ImVec2 a, ImVec2 b, bool* found) {
            const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            for (int i = 0; i < 45; ++i) frame(c);
            const std::string tip = drawn::TooltipText(found);
            for (int i = 0; i < 3; ++i) frame(ImVec2(1270, 710));
            ++gT.hovers;
            return tip;
        };
        ImVec2 a, b;
        if (e.stale && panel.ItemRect("stale", a, b))
        {
            bool found = false;
            const std::string tip = tipOver(a, b, &found);
            if (!found) bad("TOOLTIP NOT DRAWN: hovering the badge opened no tooltip window");
            else
            {
                const std::string p = TooltipProblem(tip, e.reason, "I3 the tooltip DRAWN over the badge");
                if (!p.empty()) bad(p);
                else Cov(std::string("tooltip drawn: ") + trust::ReasonName(e.reason));
            }
        }
        // "N more warnings": hover for the rest.
        if (err.empty() && !e.overflow.empty())
            for (const drawn::Run* r : inks)
                if (HasParts(r->text, { "morewarnings" }))
                {
                    bool found = false;
                    const std::string tip = tipOver(ImVec2(r->x0, r->y0), ImVec2(r->x1, r->y1), &found);
                    if (!found) bad("TOOLTIP NOT DRAWN: hovering 'N more warnings' opened no tooltip window");
                    else
                    {
                        for (const trust::Line& l : e.overflow)
                            if (!AnyLine(tip, l.parts)) bad("THE REST OF THE WARNINGS: the tooltip of 'N more warnings' does not list the " + l.what + ":\n    " + tip);
                        if (err.empty()) Cov("tooltip drawn: N more warnings");
                    }
                    break;
                }
        // The grey "N warnings": hover for the engine's notes.
        if (err.empty() && e.greyWarnings)
            for (const drawn::Run& r : runs)
                if (!InkOf(r.col) && GreyWarningText(r.text, e.greyCount))
                {
                    bool found = false;
                    const std::string tip = tipOver(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), &found);
                    if (!found) bad("TOOLTIP NOT DRAWN: hovering the grey 'N warnings' opened no tooltip window");
                    else
                    {
                        for (const std::string& n : e.greyTooltip)
                            if (!AnyLine(tip, { n })) bad("THE ENGINE'S NOTES: the tooltip of the grey 'N warnings' does not list: " + n + "\n    " + tip);
                        if (err.empty()) Cov("tooltip drawn: N warnings");
                    }
                    break;
                }
    }

    if (err.empty())
    {
        // Coverage: what was on screen and held to the model.
        if (e.stale) Cov(e.badgeFrame ? "badge: stale: frame N" : "badge: stale (plain)");
        if (e.stale) Cov(std::string("reason: ") + trust::ReasonName(e.reason));
        if (e.decompiling) Cov("decompiling");
        if (e.stoppingNote) Cov("stopping");
        if (e.outputShown && e.stale && e.alpha < 0.99f) Cov(facts.unsupported ? "dim: stale and unsupported" : "dim: stale output");
        if (e.outputShown && facts.unsupported && !e.stale) Cov("dim: unsupported output");
        if (e.outputShown && !e.stale) Cov("output undimmed");
        for (const trust::Line& l : e.lines) Cov("line: " + l.what);
        if (facts.present && facts.status == trust::Status::Ok && facts.unimplemented) Cov("line: unimplemented");
        if (facts.present && facts.status == trust::Status::Ok && facts.truncated && facts.truncatedCount > 1) Cov("line: truncated at several places");
        if (e.greyWarnings) Cov("grey: N warnings");
        if (t.sessionDisabled) Cov("red: session disabled");
        if (facts.present && facts.status == trust::Status::Unavailable) Cov("red: unavailable");
        if (e.uncertain > 4) Cov("amber: N more warnings (" + std::to_string(e.uncertain) + " lines)");
    }
    return err;
}

// ---- The status row ------------------------------------------------------------------------------------------------------
//
// "<source label> . frame N" is the VIEW's; "<name> @ 0x<entry>" and "(run at 0x...)" are the SHOWN result's (A8-15 (a), closed in the fixer's
// round: the first version of this test passed a row that showed the shown output's frame for the view's). Read from the glyphs of the
// row, which may be wrapped over several rows (A8-17), not asked of the panel. The finder's own texts, "(entry from ...)" and the doubt,
// are area 4's: their place is held, not their words.
uint64_t gStatusChecks = 0;
std::string StatusProblem(Rig& r)
{
    if (!r.visible) return "";
    const std::vector<drawn::Run> runs = drawn::WindowRuns(DecompilerPanel::kTitle);
    const Rig::ViewState vs = r.ViewNow();
    const std::string label = vs.kind == DecompViewKind::None ? "No source" : "model";
    // the row: from the run that names the source up to the first line of another kind (an amber or red sentence that is not one of the
    // row's own pieces, or the grey notes that follow the row)
    size_t first = runs.size();
    for (size_t i = 0; i < runs.size(); ++i)
        if (!InkOf(runs[i].col) && HasParts(runs[i].text, { label })) { first = i; break; }
    if (first == runs.size()) return "no row names the source";
    std::string row;
    for (size_t i = first; i < runs.size(); ++i)
    {
        const std::string n = Norm(runs[i].text);
        const int ink = InkOf(runs[i].col);
        const bool piece = ink == 0 || n.compare(0, 11, "decompiling") == 0 || n.compare(0, 8, "stopping") == 0 || n.compare(0, 6, "[stale") == 0;
        if (!piece) break;
        row += runs[i].text + " ";
    }
    std::vector<std::string> parts;
    if (vs.kind == DecompViewKind::None) parts = { "No source" };
    else parts = { "model", "frame" + std::to_string(vs.frame) };
    const DecompileResult* s = r.panel.Shown();
    std::string why;
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
        if (mirror)
        {
            std::snprintf(b, sizeof b, "(runat0x%08X)", s->requestedEntry);
            parts.push_back(b);
        }
        else if (Norm(row).find("(runat") != std::string::npos)
            why = "'(run at ...)' is drawn for a function run at its own address";
    }
    ++gStatusChecks;
    if (why.empty() && !HasParts(row, parts))
    {
        std::string want;
        for (const std::string& p : parts) want += "[" + p + "]";
        why = "the status row is '" + row + "' and should hold " + want;
    }
    return why;
}

// ---- The rig's check -----------------------------------------------------------------------------------------------

void CheckTrust(Rig& r, const char* where)
{
    ++gT.checks;
    if (std::string(where) == "held") ++gT.held; else ++gT.idle;
    const trust::TrueState t = StateOf(r);
    std::string err = Verify(r.panel, t, [&](ImVec2 m) { r.Frame(m, false); }, gHover);
    if (err.empty())
    {
        const std::string why = StatusProblem(r);
        if (!why.empty()) err = "STATUS LINE: " + why + "\n";
    }
    if (err.empty()) return;
    char b[160];
    std::snprintf(b, sizeof b, "TRUST (%s): the panel disagrees with the model\n", where);
    std::string state = std::string("  true state: visible ") + std::to_string(t.visible) + ", inputsDiffer " + std::to_string(t.inputsDiffer) +
                        ", frameMoved " + std::to_string(t.frameMoved) + ", outstanding " + std::to_string(t.outstanding) + ", stopping " +
                        std::to_string(t.stopping) + ", stopped " + std::to_string(t.stopped) + ", sessionDisabled " + std::to_string(t.sessionDisabled) +
                        ", noEntry " + std::to_string(static_cast<int>(t.noEntry)) + ", live " + std::to_string(t.liveEmulator) +
                        ", running " + std::to_string(t.running);
    const DecompileResult* s = r.panel.Shown();
    if (s) state += "\n  shown: status " + std::to_string(static_cast<int>(s->status)) + ", entry " + Hex(s->requestedEntry) + ", lines " +
                    std::to_string(s->lines.size()) + (s->truncated ? ", truncated" : "") + (s->unsupported ? ", unsupported" : "") +
                    (s->unimplemented ? ", unimplemented" : "") + ", warnings " + std::to_string(s->warnings.size()) +
                    (s->error.empty() ? "" : ", error '" + s->error + "'");
    r.Fail(std::string(b) + err + state);
}

// ---- The outcomes the area 1 sequences do not have ----------------------------------------------------------------------

std::string ConfigRoot() { return dts::Join(gConfig, "decompiler"); }

void PutFile(const std::string& path)
{
    if (FILE* f = std::fopen(path.c_str(), "wb"))
    {
        std::fputs("x", f);
        std::fclose(f);
    }
}

// The language files cannot be put back: a regular file where their directory goes. Every request is answered
// Unavailable, naming the path, until the file is gone and the next request puts the files back.
void BreakConfig() { dts::RemoveTree(ConfigRoot()); PutFile(ConfigRoot()); }
void HealConfig() { dts::RemoveTree(ConfigRoot()); }

Rig* gRig = nullptr;

void ThrowOnce();
std::atomic<bool> gArmed(false);
void ThrowOnce()
{
    if (gArmed.exchange(false)) throw std::bad_alloc();
}

const char* const kDamaged =
    "the decompiler ran out of memory earlier and its engine may be damaged; restart Saturn Explorer to decompile again";

// A result as the engine would make it for the request the panel has outstanding, with the status and the notes of 'kind'.
DecompileResult Synthesize(const std::string& kind, Rig& r)
{
    DecompileResult res;
    res.key = *r.panel.HeldKey();
    res.generation = r.panel.PendingGeneration();
    res.slot = DecompilerPanel::kSlot;
    res.requestedEntry = r.panel.WantedEntry();
    uint32_t canonical = res.requestedEntry;
    CanonicalAddress(res.requestedEntry, canonical);
    res.entry = canonical;
    res.name = "func_" + Hex(canonical);
    auto lines = [&] {
        for (const char* text : { "int4 func(void)", "{", "  return 0;", "}" })
        {
            Line l;
            l.text = text;
            res.lines.push_back(l);
        }
    };
    auto warn = [&](uint32_t at, const std::string& text) {
        FlowWarning w;
        w.kind = FlowWarning::Engine;
        w.address = at;
        w.text = text;
        res.flowWarnings.push_back(w);
        res.warnings.push_back(trust::Hex8(at) + ": " + text);
    };
    res.status = DecompileStatus::Ok;
    if (kind == "failed")
    {
        res.status = DecompileStatus::Failed;
        res.error = "out of memory";
    }
    else if (kind == "nocode")
    {
        res.status = DecompileStatus::NoCode;
        res.error = "no code at " + trust::Hex8(res.requestedEntry) + ": not captured memory";
    }
    else if (kind == "damaged")
    {
        res.status = DecompileStatus::Unavailable;
        res.error = kDamaged;
    }
    else if (kind == "plain")
        lines();
    else if (kind == "unimpl")
    {
        lines();
        res.unimplemented = true;
        warn(canonical + 2, "Unimplemented instruction - Truncating control flow here");
        res.warnings.push_back("the SH-2 spec has no semantics for an instruction here");
    }
    else if (kind == "many")
    {
        lines();
        for (uint32_t i = 0; i < 6; ++i) warn(canonical + 2 * i, "flow runs into func_" + Hex(0x06009000u + i) + ", another function's entry");
    }
    else if (kind == "combo")
    {
        lines();
        res.unsupported = true;
        res.unsupportedAt = canonical + 6;
        res.warnings.push_back("PC-relative load in a delay slot at " + trust::Hex8(canonical + 6) + " that other code also jumps to");
        res.truncated = true;
        res.truncatedAt = 0x05FFFFF6u;
        res.truncatedWhy = "the cartridge area";
        res.truncatedCount = 3;
        res.badDataAt = { canonical + 8, canonical + 12 };
        warn(canonical + 4, "jmp is taken for a tail call to 0x06009000");
        warn(canonical + 10, "Treating indirect jump as call");
        warn(0, "Exceeded maximum restarts");
    }
    return res;
}

// Refresh with the engine held, then answer the request that is outstanding with a result of 'kind' (made up: the
// engine never ran it), and let the engine's own answer come and be thrown away.
std::string Inject(Rig& r, const std::string& kind)
{
    if (r.selected == -2 || !r.visible || r.holding) return "(nothing to answer)";
    if (r.runner.Disabled()) return "(decompilation is disabled)";
    r.gate.Close();
    r.holding = true;
    if (!r.Click("refresh"))
    {
        r.gate.Open();
        r.holding = false;
        return "(no Refresh button)";
    }
    r.explicitPending = true;
    r.haveStopped = false;
    r.Idle(2);
    const auto t0 = std::chrono::steady_clock::now();
    while (r.runner.Busy() && r.gate.Held() == 0 && dts::MsSince(t0) < 30000.0) std::this_thread::yield();
    if (!r.panel.Pending() || !r.panel.HeldKey())
    {
        r.gate.Open();
        r.holding = false;
        r.Wait();
        return "(Refresh asked for nothing)";
    }
    DecompileResult res = Synthesize(kind, r);
    r.injectedGens.insert(res.generation);
    const bool taken = r.panel.Accept(res, r.runner.Epoch());
    r.gate.Open();
    r.holding = false;
    r.Wait();
    if (!taken) return "(the panel did not take it)";
    ++gT.injected;
    return "";
}

template <class R>
void TypeEntry(R& r, const std::string& text)
{
    ImVec2 a, b;
    if (!r.panel.ItemRect("entry", a, b)) return;
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    for (int i = 0; i < 3; ++i) r.Frame(c, false);
    r.Frame(c, true);
    r.Frame(c, false);
    r.Idle(2);
    r.Key(ImGuiKey_End);
    for (int i = 0; i < 16; ++i) r.Key(ImGuiKey_Backspace);
    for (char ch : text) ImGui::GetIO().AddInputCharacter(static_cast<unsigned>(ch));
    r.Idle();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    r.Frame(ImVec2(1270, 710), false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    r.Frame(ImVec2(1270, 710), false);
}

// t-contain <fn>: "Decompile" (ShowFunctionContaining) on the entry of a function, as the SH-2 Assembly menu does. The finder is given the
// entry as a user start first, so it answers with full confidence and no doubt (the finder's own texts are area 4's).
std::string Contain(Rig& r, int idx)
{
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
    // Decompile is a request like Enter: it asks again, so the Stop on this question is over (A8-18).
    r.haveStopped = false;
    r.noEntry = 0;
    r.Frame();
    return "";
}

std::string TrustOp(Rig& r, const Op& o)
{
    std::istringstream words(o.text);
    std::string verb, arg;
    words >> verb >> arg;
    if (verb == "inject") return Inject(r, arg);
    if (verb == "contain") return Contain(r, std::atoi(arg.c_str()));
    if (verb == "breakcfg")
    {
        if (r.holding || r.cfgBroken) return "(held, or already broken)";
        BreakConfig();
        r.cfgBroken = true;
        return "";
    }
    if (verb == "fixcfg")
    {
        if (r.holding || !r.cfgBroken) return "(held, or not broken)";
        HealConfig();
        r.cfgBroken = false;
        return "";
    }
    if (verb == "engfail")
    {
        if (r.holding || r.selected == -2 || !r.visible || r.runner.Disabled()) return "(nothing to fail)";
        gArmed = true;
        DecompilerRunner::SetRecordHookForTests(&ThrowOnce);
        if (!r.Click("refresh"))
        {
            gArmed = false;
            DecompilerRunner::SetRecordHookForTests(nullptr);
            return "(no Refresh button)";
        }
        r.explicitPending = true;
        r.haveStopped = false;
        const auto t0 = std::chrono::steady_clock::now();
        while (!r.runner.Disabled() && dts::MsSince(t0) < 20000.0)
        {
            r.Frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        gArmed = false;
        DecompilerRunner::SetRecordHookForTests(nullptr);
        return r.runner.Disabled() ? "" : "(the worker did not fail)";
    }
    if (verb == "badentry")
    {
        if (!r.visible) return "(panel hidden)";
        TypeEntry(r, arg == "F" ? "FFFFFFF0" : "zz");
        r.explicitPending = arg == "F";   // an entry that parses is a request (a capture even of a running emulator); a refused one is not
        r.haveStopped = false;   // a Stop belongs to the question it stopped (A8-10)
        r.noEntry = arg == "F" ? 2 : 1;
        return "";
    }
    if (verb == "containbad")
    {
        if (!r.visible) return "(panel hidden)";
        r.panel.ShowFunctionContaining(0, 0x00300004u);   // past LWRAM: not captured whatever BIOS image is mapped
        r.explicitPending = true;
        r.haveStopped = false;
        r.noEntry = 3;
        return "";
    }
    return "(unknown area 8 operation '" + o.text + "')";
}

// ---- Programs: area 1's sequences, extended ------------------------------------------------------------------------------

enum Mix { MixBase, MixWarn, MixBudget, MixConfig, MixInject, MixDisable, MixEntry, MixAll, kMixes };
const char* const kMixNames[kMixes] = {
    "ok, truncated, unsupported, stopped",
    "... and several amber lines",
    "... and too large (the budget)",
    "... and unavailable (the language files)",
    "... and the injected results (failed, no code, damaged engine, unimplemented, many warnings)",
    "... and the session disabled (a cancel not acknowledged, a worker that failed)",
    "... and targets that find no function (a refused entry, a Containing nobody holds)",
    "everything",
};

Op Custom(const std::string& text)
{
    Op o;
    o.kind = OpKind::Custom;
    o.text = text;
    return o;
}

Program MakeBaseProgram(uint64_t seed, int mix)
{
    Program p = Generate(seed);
    Rng rng(seed * 2654435761ull + static_cast<uint64_t>(mix) * 40503ull + 7);
    const bool all = mix == MixAll;
    p.placed.push_back(FnT);
    p.placed.push_back(FnU);
    if (mix >= MixWarn || all) p.placed.push_back(FnW);
    p.starve = mix == MixBudget || all || (mix > MixBudget && rng.Chance(25));
    p.wedge = mix == MixDisable || all;
    auto idxOf = [&](int f) {
        for (size_t i = 0; i < p.placed.size(); ++i)
            if (p.placed[i] == f) return static_cast<int>(i);
        return 0;
    };

    struct W { std::string word; int weight; bool native; };
    std::vector<W> extra;
    if (mix == MixBudget || all || mix > MixBudget) extra.push_back({ "starve", 5, true });
    if (mix == MixConfig || all) { extra.push_back({ "t-breakcfg", 4, false }); extra.push_back({ "t-fixcfg", 4, false }); }
    if (mix == MixInject || all)
        for (const char* k : { "failed", "nocode", "damaged", "plain", "unimpl", "many", "combo" })
            extra.push_back({ std::string("t-inject ") + k, 3, false });
    if (mix == MixDisable || all) { extra.push_back({ "wedge", 2, true }); extra.push_back({ "t-engfail", 2, false }); }
    if (mix == MixEntry || all)
    {
        extra.push_back({ "t-badentry zz", 3, false });
        extra.push_back({ "t-badentry F", 2, false });
        extra.push_back({ "t-containbad", 3, false });
    }
    int totalW = 0;
    for (const W& w : extra) totalW += w.weight;

    std::vector<Op> out;
    bool held = false;
    for (const Op& o : p.ops)
    {
        out.push_back(o);
        if (o.kind == OpKind::Hold) held = true;
        if (o.kind == OpKind::Wait) held = false;
        // The three new functions are chosen and edited as often as the old ones.
        if (rng.Chance(14))
        {
            Op s;
            s.kind = rng.Chance(60) ? OpKind::Select : OpKind::Edit;
            s.a = idxOf(rng.Chance(40) ? FnT : rng.Chance(50) ? FnU : (p.placed.back() == FnW ? FnW : FnT));
            s.b = rng.Below(1000);
            s.c = rng.Below(1000);
            out.push_back(s);
        }
        if (!held && totalW > 0 && rng.Chance(all ? 14 : 18))
        {
            int pick = rng.Below(totalW);
            for (const W& w : extra)
            {
                if (pick < w.weight)
                {
                    if (w.native)
                    {
                        Op n;
                        n.kind = w.word == "starve" ? OpKind::Starve : OpKind::Wedge;
                        out.push_back(n);
                    }
                    else
                        out.push_back(Custom(w.word.substr(2)));
                    break;
                }
                pick -= w.weight;
            }
        }
    }
    p.ops = out;
    return p;
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

bool gHiddenRename = false;   // SE_TRUST_HIDDEN_RENAME (area 8, round 4)
uint64_t gHiddenRenames = 0;  // ...how many renames it made and undid while the panel was hidden

// The sequence a run uses: `concat` generated programs end to end (their functions are the first program's: indexes are taken modulo the
// world; round 1's ran 20 to 60 operations, a long run 300 to 450), with, after each Stop, some of the operations that interleave with
// it (a Refresh, a reconnect, a rename, an edit, Decompile, hide and show, a Select, the volatile list, a second Stop), every rename
// followed by a Refresh and the rename undone, and Decompile on a function now and then (A8-18, A8-23: the generated sequences typed
// every entry and never reached it).
Program MakeProgram(uint64_t seed, int mix, int concat = 1)
{
    Program p = MakeBaseProgram(seed, mix);
    Rng rng(seed * 7919 + 17);
    std::vector<Op> ops = p.ops;
    for (int k = 1; k < concat; ++k)
    {
        const Program q = MakeBaseProgram(seed * 131 + static_cast<uint64_t>(k) * 977, mix);
        for (const Op& o : q.ops) ops.push_back(o);
    }
    const int n = static_cast<int>(p.placed.size());
    std::vector<Op> out;
    bool held = false;
    std::map<int, int> nameOf;   // function -> the name option it has now: the undo
    // The functions a Decompile was ever asked about: their start is not toggled off (the finder would answer another entry: area 4's). Every
    // one counts, not the latest: a Decompile that had no effect (the panel hidden or the engine held) must not stand for the target.
    std::set<int> containFns;
    auto emitContain = [&](int k) {
        out.push_back(Custom("contain " + std::to_string(k)));
        containFns.insert(k % n);
    };
    for (const Op& o0 : ops)
    {
        Op o = o0;
        if (o.kind == OpKind::Bound && containFns.count(o.a % n)) o = Mk(OpKind::Refresh);
        out.push_back(o);
        if (o.kind == OpKind::Hold) held = true;
        if (o.kind == OpKind::Wait || o.kind == OpKind::Finish) held = false;
        const int fn = o.a % n;
        // (area 8, round 4) SE_TRUST_HIDDEN_RENAME=1: half the renames are made and undone while the panel is hidden (A8-25's case, which
        // the default mixes reach only by chance), with now and then an edit, a hold or a reconnect while hidden, and the panel shown after
        // (or left hidden for a later Show of the sequence). Not in the default run: its sequences stay as they were.
        if (gHiddenRename && o.kind == OpKind::Rename && !held && rng.Chance(50))
        {
            out.pop_back();
            const int before = nameOf.count(fn) ? nameOf[fn] : 0;
            out.push_back(Mk(OpKind::Hide));
            out.push_back(o);
            switch (rng.Below(5))
            {
            case 0: out.push_back(Mk(OpKind::Edit, rng.Below(1000), rng.Below(1000), rng.Below(1000))); break;
            case 1: out.push_back(Mk(OpKind::Reconnect, rng.Below(2))); break;
            case 2: out.push_back(Mk(OpKind::Volatile, rng.Below(4))); break;
            default: break;
            }
            out.push_back(Mk(OpKind::Rename, o.a, before));
            nameOf[fn] = before;
            ++gHiddenRenames;
            if (rng.Chance(75)) out.push_back(Mk(OpKind::Show));
            continue;
        }
        if (o.kind == OpKind::Rename && !held)
        {
            const int before = nameOf.count(fn) ? nameOf[fn] : 0;
            nameOf[fn] = o.b;
            if (rng.Chance(35))
            {
                out.push_back(Mk(OpKind::Refresh));
                out.push_back(Mk(OpKind::Rename, o.a, before));
                nameOf[fn] = before;
            }
        }
        if (o.kind == OpKind::Stop && !held && rng.Chance(70))
        {
            const int count = 1 + rng.Below(3);
            for (int i = 0; i < count; ++i)
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
        if (!held && o.kind == OpKind::Select && rng.Chance(12)) emitContain(rng.Below(1000));
    }
    p.ops = out;
    return p;
}

// ---- Runs --------------------------------------------------------------------------------------------------------------

void HealForRun()
{
    HealConfig();
    gArmed = false;
    DecompilerRunner::SetRecordHookForTests(nullptr);
}

void Setup(Rig& rig)
{
    gRig = &rig;
    rig.winW = gRigWinW;
    rig.winH = 700;
    rig.custom = TrustOp;
    rig.trustCheck = CheckTrust;
    rig.atEnd = [](Rig& r) {
        if (r.cfgBroken)
        {
            HealConfig();
            r.cfgBroken = false;
        }
    };
}

bool RunTrust(Oracle& oracle, const Program& p, uint64_t seed, bool verbose, const std::set<int>& skip = std::set<int>(), int limit = -1)
{
    HealForRun();
    oracle.beforeRun = [] {
        if (gRig && gRig->cfgBroken) HealConfig();
    };
    oracle.afterRun = [] {
        if (gRig && gRig->cfgBroken) BreakConfig();
    };
    gRigSetup = Setup;
    const uint64_t before = gStats.ops + gStats.noEffect;
    const bool ok = RunProgram(oracle, p, seed, verbose, skip, limit);
    gT.steps += gStats.ops + gStats.noEffect - before;
    ++gT.programs;
    gRig = nullptr;
    HealForRun();
    return ok;
}


// ---- Targets the rig cannot reach: Follow PC and the registers -----------------------------------------------------------------
//
// The area 1 rig types its entries and has no registers. These small panels have both; the true state is written out
// by hand for each, and held to the model by the same Verify.
float gMiniWinW = 1250.0f;
const char* gMiniLabel = "mini";   // ...and its source label ("live, running" in the app)   // the mini panel's window width (area 8 round 2: the status row on a narrow panel)

struct MiniRig
{
    Gate gate;   // before the runner: it must outlive the worker that waits on it
    ContentBackend backend;
    DecompilerRunner runner;
    DecompilerPanel panel;
    FunctionBounds bounds;
    std::map<uint32_t, std::string> names;
    std::unordered_map<uint32_t, std::string> comments;
    ImGuiHarness h;
    DecompViewKind kind = DecompViewKind::Static;
    uint64_t source = 1, serial = 1, frame = 1;
    se_sh2_regs regs[2] = {};
    bool haveRegs[2] = { false, false };
    std::vector<int> placed;
    std::set<uint32_t> stackEntries;   // round 5 probe: the call stack's targets, and its revision
    uint64_t stackRevision = 0;
    uint64_t namesGen = 0;             // round 6 probe: the names' generation (0, as before, unless a probe renames)

    MiniRig() : runner(gConfig), h([this] { Ui(); })
    {
        ApplyTheme(ImGui::GetStyle());
        LoadFonts(ImGui::GetIO());
        runner.SetPhaseObserverForTests([this](DecompilePhase ph) { gate.Observe(ph); });
        placed = { FnC, FnA };
        backend.content = BuildImage(placed, 0);
        backend.source = source;
    }
    DecompilerView View() const
    {
        DecompilerView d;
        d.backend = const_cast<ContentBackend*>(&backend);
        d.kind = kind;
        d.sourceLabel = gMiniLabel;
        d.sourceId = source;
        d.deriveSerial = serial;
        d.frameNumber = frame;
        for (int i = 0; i < 2; ++i)
        {
            d.regs[i] = regs[i];
            d.haveRegs[i] = haveRegs[i];
        }
        return d;
    }
    void Ui()
    {
        const DecompilerView v = View();
        DecompilerInputs in;
        in.names = &names;
        in.namesGeneration = namesGen;
        in.bounds = &bounds;
        in.comments = &comments;
        in.callSites = [this](const SnapshotKey&, std::set<uint32_t>& out) { out.insert(stackEntries.begin(), stackEntries.end()); };
        in.callSitesRevision = stackRevision;
        panel.Update(v, runner, in, true);
        DecompilerPanel::Request r;
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(gMiniWinW, 700), ImGuiCond_Always);
        panel.Draw(nullptr, v, runner, in, r);
    }
    void Frame(ImVec2 mouse = ImVec2(1270, 710), bool down = false) { h.Frame(mouse, down); }
    void Idle(int n = 1)
    {
        for (int i = 0; i < n; ++i) Frame();
    }
    void Key(ImGuiKey key)
    {
        ImGui::GetIO().AddKeyEvent(key, true);
        Frame();
        ImGui::GetIO().AddKeyEvent(key, false);
        Frame();
    }
    bool Settle()
    {
        const auto t0 = std::chrono::steady_clock::now();
        int calm = 0;
        while (calm < 3)
        {
            if (dts::MsSince(t0) > dts::kWaitMs) return false;
            Idle();
            if (!runner.Busy() && !panel.Pending()) ++calm; else { calm = 0; std::this_thread::yield(); }
        }
        return true;
    }
};

int gMiniFailures = 0;

void MiniVerify(MiniRig& m, const char* what, const trust::TrueState& t, const char* wantInShown)
{
    const DecompileResult* s = m.panel.Shown();
    std::string err;
    if (wantInShown && !s) err = "nothing is shown";
    else if (wantInShown && s->Text().find(wantInShown) == std::string::npos) err = std::string("the output on screen is not the function expected: ") + wantInShown;
    if (err.empty()) err = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, gHover);
    ++gT.checks;
    std::printf("pin %-90s %s\n", what, err.empty() ? "ok" : "FAILED");
    if (!err.empty())
    {
        std::printf("%s", err.c_str());
        ++gMiniFailures;
    }
}

void RunMiniPins()
{
    const uint32_t cEntry = Defs()[FnC].entry;
    // What the function finder says of a function it was asked to find by an address with nothing else to go on (area 4's
    // text, quoted here as the true state: the panel shows it as the amber line "Entry: ...").
    const std::string doubt = "guessed from the previous rts; no symbol, call or user entry confirms it";
    {
        // Nothing decompiled yet: Follow PC, and this source has no registers.
        MiniRig m;
        m.Idle(3);
        CHECK(m.Settle());
        trust::TrueState t;
        t.noEntry = trust::NoEntry::NoRegisters;
        MiniVerify(m, "nothing decompiled yet: Follow PC with no registers", t, nullptr);
    }
    auto inside = [&](MiniRig& m) {
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
    };
    {
        // Follow PC, the PC where no function is (the BIOS, no image): nothing will be asked; the tooltip names the PC.
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        inside(m);
        m.Idle(3);
        CHECK(m.Settle());
        trust::TrueState t;
        t.liveEmulator = true;
        t.entryDoubt = doubt;
        MiniVerify(m, "Follow PC finds the function, current", t, "func_");
        m.regs[0].pc = 0x00001FF0u;
        ++m.frame;
        m.Idle(3);
        t.inputsDiffer = true;
        t.frameMoved = true;
        t.noEntry = trust::NoEntry::FollowNotFound;
        MiniVerify(m, "Follow PC with the PC where no function is", t, "func_");
        // The PC comes back: answered, current.
        inside(m);
        ++m.frame;
        m.Idle(2);
        CHECK(m.Settle());
        t = trust::TrueState();
        t.liveEmulator = true;
        t.entryDoubt = doubt;
        MiniVerify(m, "...and the PC comes back to a function", t, "func_");
    }
    {
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        inside(m);
        m.Idle(3);
        CHECK(m.Settle());
        m.haveRegs[0] = false;
        ++m.frame;
        m.Idle(3);
        trust::TrueState t;
        t.liveEmulator = true;
        t.inputsDiffer = true;
        t.frameMoved = true;
        t.noEntry = trust::NoEntry::NoRegisters;
        t.entryDoubt = doubt;   // (area 8 fixer, A8-39) the output's own doubt, drawn while the question has no function
        MiniVerify(m, "Follow PC with no registers to read", t, "func_");
    }
    {
        MiniRig m;
        m.panel.ShowFunctionContaining(0, cEntry + 4);
        m.Idle(3);
        CHECK(m.Settle());
        trust::TrueState t;
        t.entryDoubt = doubt;
        MiniVerify(m, "Decompile (Containing) on a function, current", t, "func_");
        // The source goes away (closed, or the emulator disconnected): the output stays, stale, and nothing can replace it.
        m.kind = DecompViewKind::None;
        m.Idle(3);
        trust::TrueState gone;
        gone.haveSource = false;
        gone.inputsDiffer = true;
        gone.entryDoubt = doubt;
        MiniVerify(m, "no source on screen", gone, "func_");
        m.kind = DecompViewKind::Static;
        m.Idle(2);
        CHECK(m.Settle());
        t.noEntry = trust::NoEntry::None;
        m.panel.ShowFunctionContaining(0, 0x00001FF0u);
        m.Idle(3);
        t.noEntry = trust::NoEntry::ContainingNotFound;
        MiniVerify(m, "Decompile (Containing) on an address no function holds", t, "func_");
        TypeEntry(m, "zz");
        m.Idle(2);
        t.noEntry = trust::NoEntry::EntryRefused;
        MiniVerify(m, "...then an entry the box refuses", t, "func_");
        TypeEntry(m, "FFFFFFF0");
        m.Idle(2);
        t.noEntry = trust::NoEntry::EntryNotCode;
        MiniVerify(m, "...then an entry that is no code", t, "func_");
    }
    {
        // Decompile on an address inside a function that its flow never reaches (two nops the bra jumps over): said.
        MiniRig m;
        m.placed = { FnC, FnU };
        {
            // U with its bt moved to the rts: the two nops after the bra are dead code.
            std::vector<uint8_t> hw = BuildImage(m.placed, 0)->bytes;
            hw[Defs()[FnU].sites[0].addr - kHw] = 0x03;
            m.backend.content = MakeContent(std::move(hw));
        }
        const uint32_t unreached = Defs()[FnU].entry + 8;
        m.panel.ShowFunctionContaining(0, unreached);
        m.Idle(3);
        CHECK(m.Settle());
        trust::TrueState t;
        t.entryDoubt = "the code from " + trust::Hex8(Defs()[FnU].entry) + " (after the rts at " + trust::Hex8(Defs()[FnU].entry - 4) +
                       ") never reaches " + trust::Hex8(unreached);
        t.anchorKnown = true;
        t.anchor = unreached;
        MiniVerify(m, "Decompile on an address the function found does not reach", t, nullptr);
    }
    {
        // A running emulator that has moved on: the frame badge, and the tooltip that says to press Refresh.
        MiniRig m;
        m.kind = DecompViewKind::LiveRunning;
        inside(m);
        m.Idle(3);
        CHECK(m.Settle());
        ++m.frame;
        ++m.serial;
        m.Idle(3);
        trust::TrueState t;
        t.liveEmulator = true;
        t.running = true;
        t.inputsDiffer = true;
        t.frameMoved = true;
        t.entryDoubt = doubt;
        MiniVerify(m, "a running emulator has moved on", t, "func_");
    }
    {
        // The register hint at a halt: on current output of a stopped CPU, and gone when the output goes stale or the emulator runs.
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        std::vector<uint8_t> hw = m.backend.content->bytes;
        dts::Put16(hw, dts::kFixture1Jsr - 2 - kHw, 0x6042);   // mov.l @r4,r0 replaces the literal load: the call is computed
        m.backend.content = MakeContent(std::move(hw));
        ++m.serial;
        m.regs[0].pc = dts::kFixture1Jsr;
        m.regs[0].r[0] = dts::kFixture1Callee;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        trust::TrueState t;
        t.liveEmulator = true;
        t.entryDoubt = doubt;
        t.hintMnemonic = "jsr";
        t.hintInsn = dts::kFixture1Jsr;
        t.hintText = "r0 = 0x06005000 at the halt";
        MiniVerify(m, "a register call at the halt: the hint, current output", t, "(*(code *)");
        m.kind = DecompViewKind::LiveRunning;
        m.Idle(3);
        t.running = true;
        MiniVerify(m, "...the emulator runs: no hint (the registers are old), the output not yet stale", t, "(*(code *)");
        ++m.frame;
        m.Idle(3);
        t.inputsDiffer = true;
        t.frameMoved = true;
        MiniVerify(m, "...and a frame later: stale, still no hint", t, "(*(code *)");
        m.kind = DecompViewKind::LivePaused;
        m.Idle(3);
        CHECK(m.Settle());
        t.running = false;
        t.inputsDiffer = false;
        t.frameMoved = false;
        MiniVerify(m, "...paused again and asked: the hint is back", t, "(*(code *)");
        // A write while the engine holds the replacement: the output is stale and has no hint (the registers belong to the old memory).
        m.gate.Close();
        std::vector<uint8_t> hw2 = m.backend.content->bytes;
        hw2[Defs()[FnC].sites[1].addr - kHw] ^= 0x01;
        m.backend.content = MakeContent(std::move(hw2));
        ++m.serial;
        m.Idle(3);
        const auto t0 = std::chrono::steady_clock::now();
        while (m.runner.Busy() && m.gate.Held() == 0 && dts::MsSince(t0) < 30000.0) std::this_thread::yield();
        m.Idle(2);
        t.inputsDiffer = true;
        t.outstanding = true;
        MiniVerify(m, "...a write with the replacement held: stale, no hint", t, "(*(code *)");
        m.gate.Open();
        CHECK(m.Settle());
        t.inputsDiffer = false;
        t.outstanding = false;
        MiniVerify(m, "...the replacement shown: the hint again", t, "(*(code *)");
    }
    {
        // The window when the app has no runner: red, and it says it stays.
        DecompilerPanel panel;
        ImGuiHarness h([&] {
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(1250, 700), ImGuiCond_Always);
            panel.DrawUnavailable(nullptr, "its worker could not be started: no thread");
        });
        ApplyTheme(ImGui::GetStyle());
        LoadFonts(ImGui::GetIO());
        for (int i = 0; i < 4; ++i) h.Frame(ImVec2(1270, 710), false);
        const std::vector<drawn::Run> runs = drawn::WindowRuns(DecompilerPanel::kTitle);
        bool red = false, note = false;
        for (const drawn::Run& r : runs)
        {
            if (InkOf(r.col) == 2 && HasParts(r.text, { "Decompiler unavailable: ", "its worker could not be started: no thread" })) red = true;
            if (HasParts(r.text, { "Restart to try again" })) note = true;
        }
        std::printf("pin %-90s %s\n", "no runner: the window says why and that it stays", red && note ? "ok" : "FAILED");
        CHECK(red && note);
        if (red && note) Cov("line: no runner");
    }
    CHECK(gMiniFailures == 0);
}


// ---- The fixer's pins on the small rig (A8-17 to A8-21) ----------------------------------------------------------------------

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

// A write to the function C: its content changes (the view's serial moves).
void EditMini(MiniRig& m)
{
    std::vector<uint8_t> hw = m.backend.content->bytes;
    hw[Defs()[FnC].sites[1].addr - kHw] ^= 0x01;
    m.backend.content = MakeContent(std::move(hw));
    ++m.serial;
}

// A check of the small rig that is not a comparison with the model.
void MiniCheck(const char* what, bool ok, const std::string& detail = "")
{
    ++gT.checks;
    std::printf("pin %-90s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok)
    {
        if (!detail.empty()) std::printf("%s\n", detail.c_str());
        ++gMiniFailures;
    }
}

// Pushes the item "name" of the last draw (a context-menu item), and returns the clipboard text it put there.
bool CopyFrom(MiniRig& m, int line, const char* item, std::string& text)
{
    ImVec2 a, b;
    if (!m.panel.LineRect(line, a, b)) return false;
    const ImVec2 point((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    m.h.Hover(point);
    m.h.RightClick(point);
    m.Idle(2);
    ImVec2 ma, mb;
    if (!m.panel.ItemRect(item, ma, mb)) return false;
    const ImVec2 c((ma.x + mb.x) * 0.5f, (ma.y + mb.y) * 0.5f);
    ImGui::SetClipboardText("");
    m.h.Hover(c);
    m.h.Click(c);
    m.Idle(2);
    const char* clip = ImGui::GetClipboardText();
    text = clip ? clip : "";
    return true;
}

// A8-17: the stale badge and "decompiling..." are on screen at the default layout's panel widths (42% of the work area: 537 px on a 1280 px
// window, 672 on 1600, 806 on 1920), with a four-digit and a six-digit frame number, and so are the whole amber sentences. Verify reads every
// run of the window back, wrapped sentences put together, with no allowance for a line the window cut off; and it hovers the badge, which
// opens a tooltip only if the badge is on screen.
void RunWidthPins()
{
    const std::string doubt = "guessed from the previous rts; no symbol, call or user entry confirms it";
    const float savedW = gMiniWinW;
    const char* savedLabel = gMiniLabel;
    gMiniLabel = "live, running";
    for (float w : { 537.0f, 672.0f, 806.0f })
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
            ++m.frame;
            ++m.serial;
            CHECK(ClickMini(m, "refresh"));
            CHECK(WaitHeldOrBusy(m));
            ++m.frame;
            ++m.serial;
            m.Idle(3);
            trust::TrueState t;
            t.liveEmulator = true;
            t.running = true;
            t.inputsDiffer = true;
            t.frameMoved = true;
            t.outstanding = true;
            t.entryDoubt = doubt;
            char what[160];
            std::snprintf(what, sizeof what, "panel %.0f px, frame %d: the frame badge and decompiling... are on screen", w, frames);
            MiniVerify(m, what, t, "func_");
            ImVec2 a, b;
            ImGuiWindow* win = ImGui::FindWindowByName(DecompilerPanel::kTitle);
            const bool inside = m.panel.ItemRect("stale", a, b) && win && b.x <= win->Pos.x + win->Size.x;
            std::snprintf(what, sizeof what, "panel %.0f px, frame %d: the badge's rectangle is inside the window", w, frames);
            MiniCheck(what, inside);
            // ...and so is Stop, the control "decompiling..." offers (A8-24: the toolbar ran off the window)
            std::snprintf(what, sizeof what, "panel %.0f px, frame %d: Stop is inside the window", w, frames);
            MiniCheck(what, m.panel.ItemRect("stop", a, b) && win && b.x <= win->Pos.x + win->Size.x);
            m.gate.Open();
            CHECK(m.Settle());
        }
    // The amber sentences: the unsupported one is the longest the panel draws, the truncated one the next.
    for (float w : { 537.0f, 672.0f, 806.0f })
        for (int fn : { FnU, FnT })
        {
            gMiniWinW = w;
            MiniRig m;
            m.placed = { FnC, FnU, FnT };
            m.backend.content = BuildImage(m.placed, 0);
            m.bounds.SetEntry(Defs()[fn].entry);   // the finder then answers with no doubt of its own
            m.panel.ShowFunctionContaining(0, Defs()[fn].entry);
            m.Idle(3);
            CHECK(m.Settle());
            trust::TrueState t;
            t.anchorKnown = true;
            t.anchor = Defs()[fn].entry;
            char what[160];
            std::snprintf(what, sizeof what, "panel %.0f px: the %s sentence is whole", w, fn == FnU ? "unsupported" : "truncated");
            MiniVerify(m, what, t, nullptr);
            // ...and stale at once (a write the engine holds): the badge, the dimmed code and the sentence together
            m.gate.Close();
            EditMini(m);
            m.Idle(3);
            CHECK(WaitHeldOrBusy(m));
            m.Idle(2);
            t.inputsDiffer = true;
            t.outstanding = true;
            std::snprintf(what, sizeof what, "panel %.0f px: the %s sentence, the badge and decompiling... together", w, fn == FnU ? "unsupported" : "truncated");
            MiniVerify(m, what, t, nullptr);
            m.gate.Open();
            CHECK(m.Settle());
        }
    gMiniWinW = savedW;
    gMiniLabel = savedLabel;
}

// A8-19: the tooltips that send the reader to "Enter in the entry box" say Refresh only under Follow PC and Decompile, where the box holds the
// function shown, not the one the PC (or the address asked) is in.
void RunTooltipPins()
{
    const uint32_t cEntry = Defs()[FnC].entry, aEntry = Defs()[FnA].entry;
    auto noEnter = [](const std::string& tip) { return tip.find("Enter") == std::string::npos && tip.find("Refresh") != std::string::npos; };
    {
        // Follow PC, the PC moves to A with the engine holding the replacement, Stop.
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
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
        const std::string tip = m.panel.StaleTooltip();
        MiniCheck("Follow PC, Stopped: the tooltip names Refresh and not the entry box", m.panel.GetStaleReason() == DecompilerPanel::StaleReason::Stopped && noEnter(tip), tip);
        // ...and Refresh does what it says: the followed function replaces the output
        CHECK(ClickMini(m, "refresh"));
        m.Idle(3);
        CHECK(m.Settle());
        const DecompileResult* s = m.panel.Shown();
        MiniCheck("...and Refresh then shows the function followed, with Follow PC still on", s && s->requestedEntry == aEntry && m.panel.FollowPc() && !m.panel.ShownStale());
    }
    {
        // Decompile on a function, a write the engine holds, Stop.
        MiniRig m;
        m.panel.ShowFunctionContaining(0, cEntry + 4);
        m.Idle(3);
        CHECK(m.Settle());
        m.gate.Close();
        EditMini(m);
        m.Idle(3);
        CHECK(WaitHeldOrBusy(m));
        m.Idle(2);
        CHECK(ClickMini(m, "stop"));
        m.gate.Open();
        CHECK(m.Settle());
        const std::string tip = m.panel.StaleTooltip();
        MiniCheck("Decompile, Stopped: the tooltip names Refresh and not the entry box", m.panel.GetStaleReason() == DecompilerPanel::StaleReason::Stopped && noEnter(tip), tip);
    }
    {
        // An entry typed: Enter is the right word, and stays.
        MiniRig m;
        m.Idle(3);
        TypeEntry(m, Hex(cEntry));
        m.Idle(3);
        CHECK(m.Settle());
        m.gate.Close();
        EditMini(m);
        m.Idle(3);
        CHECK(WaitHeldOrBusy(m));
        m.Idle(2);
        CHECK(ClickMini(m, "stop"));
        m.gate.Open();
        CHECK(m.Settle());
        const std::string tip = m.panel.StaleTooltip();
        MiniCheck("an Entry target, Stopped: the tooltip still names Enter in the entry box", m.panel.GetStaleReason() == DecompilerPanel::StaleReason::Stopped &&
                  tip.find("Enter in the") != std::string::npos, tip);
    }
    {
        // Nothing asked: a write to a running emulator at the same frame (no capture is taken for a running emulator by itself).
        MiniRig m;
        m.kind = DecompViewKind::LiveRunning;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        EditMini(m);
        m.Idle(3);
        const std::string tip = m.panel.StaleTooltip();
        MiniCheck("Follow PC on a running emulator, a write at the same frame: 'nothing is being decompiled' names Refresh only",
                  m.panel.GetStaleReason() == DecompilerPanel::StaleReason::NeedsRefresh && tip.find("nothing is being decompiled") != std::string::npos && noEnter(tip), tip);
        trust::TrueState t;
        t.liveEmulator = true;
        t.running = true;
        t.inputsDiffer = true;
        t.entryDoubt = "guessed from the previous rts; no symbol, call or user entry confirms it";
        MiniVerify(m, "...held to the model, the tooltip drawn over the badge", t, "func_");
    }
    {
        MiniRig m;
        m.kind = DecompViewKind::LiveRunning;
        m.Idle(3);
        TypeEntry(m, Hex(cEntry));
        m.Idle(3);
        CHECK(m.Settle());
        EditMini(m);
        m.Idle(3);
        const std::string tip = m.panel.StaleTooltip();
        MiniCheck("an Entry target on a running emulator, a write at the same frame: 'nothing is being decompiled' still names Enter",
                  m.panel.GetStaleReason() == DecompilerPanel::StaleReason::NeedsRefresh && tip.find("Enter in the") != std::string::npos, tip);
    }
}

// A8-20: the text of a refused entry goes from the box when the refusal is dropped.
void RunRefusedTextPins()
{
    const uint32_t cEntry = Defs()[FnC].entry;
    {
        MiniRig m;
        m.panel.ShowFunctionContaining(0, cEntry + 4);
        m.Idle(3);
        CHECK(m.Settle());
        TypeEntry(m, "zz");
        m.Idle(2);
        MiniCheck("a refused entry: the red line, the output stale, the text still in the box", !m.panel.EntryError().empty() && m.panel.ShownStale() &&
                  std::string(m.panel.EntryText()) == "zz");
        m.panel.ShowFunctionContaining(0, cEntry + 4);   // Decompile on the function already shown
        m.Idle(4);
        CHECK(m.Settle());
        MiniCheck("...Decompile on the function shown: no error, current, and the box holds the entry shown, not 'zz'",
                  m.panel.EntryError().empty() && !m.panel.ShownStale() && m.panel.Shown() && std::string(m.panel.EntryText()) == Hex(m.panel.Shown()->requestedEntry),
                  std::string("box '") + m.panel.EntryText() + "'");
    }
    {
        // Follow PC turned on after a refusal: the followed function is the one shown, and the box says it
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        TypeEntry(m, "zz");
        m.Idle(2);
        CHECK(ClickMini(m, "follow"));
        m.Idle(3);
        CHECK(m.Settle());
        MiniCheck("a refusal, then the Follow PC box: the refused text is gone", std::string(m.panel.EntryText()) != "zz", std::string("box '") + m.panel.EntryText() + "'");
    }
    {
        // A refusal followed by a valid entry typed: the box keeps what was typed while the answer comes.
        MiniRig m;
        m.Idle(3);
        TypeEntry(m, Hex(cEntry));
        m.Idle(3);
        CHECK(m.Settle());
        TypeEntry(m, "zz");
        m.Idle(2);
        m.gate.Close();
        TypeEntry(m, Hex(Defs()[FnA].entry));
        m.Idle(3);
        CHECK(WaitHeldOrBusy(m));
        m.Idle(2);
        MiniCheck("a refusal, then an entry typed: the box holds the typed entry while it is decompiled", std::string(m.panel.EntryText()) == Hex(Defs()[FnA].entry),
                  std::string("box '") + m.panel.EntryText() + "'");
        m.gate.Open();
        CHECK(m.Settle());
    }
}

// A8-21: Copy Function and Copy Line carry the panel's marks as C comments, and nothing else for output that is current and certain.
void RunCopyPins()
{
    for (int fn : { FnC, FnU, FnT })
        for (int stale = 0; stale < 2; ++stale)
        {
            MiniRig m;
            m.placed = { FnC, FnU, FnT };
            m.backend.content = BuildImage(m.placed, 0);
            m.bounds.SetEntry(Defs()[fn].entry);
            m.panel.ShowFunctionContaining(0, Defs()[fn].entry);
            m.Idle(3);
            CHECK(m.Settle());
            if (stale)
            {
                m.gate.Close();
                EditMini(m);
                m.Idle(3);
                CHECK(WaitHeldOrBusy(m));
                m.Idle(2);
            }
            const DecompileResult* r = m.panel.Shown();
            CHECK(r);
            const std::string body = r->Text();
            for (const char* item : { "Copy Function", "Copy Line" })
            {
                std::string text;
                const bool copied = CopyFrom(m, 1, item, text);
                const std::string want = std::string(item) == "Copy Function" ? body : r->lines[1].text;
                const bool whole = text.size() >= want.size() && text.compare(text.size() - want.size(), want.size(), want) == 0;
                const std::string header = copied && whole ? text.substr(0, text.size() - want.size()) : std::string();
                const bool hasStale = header.find("/* [stale") != std::string::npos;
                const bool hasUnsupported = header.find("not trustworthy") != std::string::npos;
                const bool hasTruncated = header.find("the function is cut off there") != std::string::npos;
                const bool wantMarks = stale || fn != FnC;
                bool ok = copied && whole && hasStale == (stale != 0) && hasUnsupported == (fn == FnU) && hasTruncated == (fn == FnT) && (header.empty() == !wantMarks);
                // a comment per mark, each whole on its own line
                for (size_t at = 0; ok && at < header.size();)
                {
                    const size_t nl = header.find('\n', at);
                    const std::string line = header.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
                    ok = nl != std::string::npos && line.compare(0, 3, "/* ") == 0 && line.size() > 6 && line.compare(line.size() - 3, 3, " */") == 0 &&
                         line.find("*/") == line.size() - 2;
                    at = nl == std::string::npos ? header.size() : nl + 1;
                }
                char what[200];
                std::snprintf(what, sizeof what, "%s of %s%s output: %s", item, fn == FnC ? "plain" : fn == FnU ? "unsupported" : "truncated", stale ? " stale" : "",
                              wantMarks ? "C comments with the marks, then the text" : "the text and nothing else");
                MiniCheck(what, ok, "clipboard:\n" + text);
                if (ok) Cov(std::string("copy: ") + (stale ? "stale" : fn == FnC ? "plain" : fn == FnU ? "unsupported" : "truncated"));
            }
            if (stale)
            {
                m.gate.Open();
                CHECK(m.Settle());
            }
        }
}

// Auto-refresh on a running emulator (A8-15 (e)): the PC leaves the function, the debounce passes, the panel asks by itself. Held to the model
// with the engine holding that request, and after it.
void RunAutoRefreshPin()
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
    // The panel's part is frame-bound: it asks within 120 frames. The engine's part is not: the worker has to reach the held phase, which takes
    // real time and, on a loaded machine, more than the 120 frames take (A8-35: this pin failed once in a full ctest run, alone it passed).
    // So the frames are counted until the panel has asked, and the engine is then waited for by the clock.
    const uint64_t asked = m.panel.Submissions();
    bool held = false;
    for (int i = 0; i < 120 && m.panel.Submissions() == asked; ++i)
    {
        ++m.frame;
        ++m.serial;
        m.Idle(1);
    }
    const bool asksByItself = m.panel.Submissions() > asked;
    if (asksByItself) held = WaitHeldOrBusy(m);
    held = held && asksByItself;
    MiniCheck("auto-refresh asks by itself within 120 frames once the PC has left the function", held);
    if (!held)
    {
        m.gate.Open();
        return;
    }
    ++m.frame;
    ++m.serial;
    m.Idle(2);
    trust::TrueState t;
    t.liveEmulator = true;
    t.running = true;
    t.inputsDiffer = true;
    t.frameMoved = true;
    t.outstanding = true;
    // The shown output is still C's: the question is A now, and C keeps the doubt it was installed with (area 8 fixer, A8-39). Before, no
    // doubt line was drawn for it.
    t.entryDoubt = trust::RtsGuess();
    MiniVerify(m, "auto-refresh in flight: [stale: frame N] and decompiling...", t, "func_");
    m.gate.Open();
    // By the clock, as the wait above (A8-35): 400 frames pass in less time than a loaded machine needs for the answer.
    for (const auto t0 = std::chrono::steady_clock::now(); (m.runner.Busy() || m.panel.Pending()) && dts::MsSince(t0) < dts::kWaitMs;)
    {
        ++m.frame;
        ++m.serial;
        m.Idle(1);
        std::this_thread::yield();
    }
    ++m.frame;
    ++m.serial;
    m.Idle(2);
    const DecompileResult* s = m.panel.Shown();
    t.outstanding = false;
    for (const std::string& u : m.panel.Uncertainties())
        if (u.compare(0, 7, "Entry: ") == 0 && u.size() > 8) t.entryDoubt = u.substr(7, u.size() - 8);
    MiniVerify(m, "auto-refresh answered, the emulator running on: [stale: frame N], press Refresh", t, "func_");
    MiniCheck("...and the answer is for the function the PC is in", s && s->requestedEntry == aEntry);
}

// A8-30 (area 8 fixer): Follow PC turned off pins the entry the finder found, and that entry is still the finder's guess. The label keeps
// its figure, "(entry from rts scan, 60%)", and the amber "Entry: ..." line stays, on screen and in the Copy marks, as the guide says of an
// entry found only by the rts scan. Both ways in: the entry Follow PC found, and the entry of the output shown when the PC is where no
// function is. Only an entry the user types drops them. The finder's words are area 4's; here they are what the screen showed before the pin.
void RunPinnedEntryPins()
{
    const uint32_t cEntry = Defs()[FnC].entry;
    const std::string doubt = "guessed from the previous rts; no symbol, call or user entry confirms it";
    auto drawnText = [] {
        std::string all;
        for (const drawn::Run& run : drawn::WindowRuns(DecompilerPanel::kTitle)) all += run.text + " ";
        return Norm(all);
    };
    auto doubted = [&](MiniRig& m, const char* what) {
        m.Frame();
        const std::vector<std::string> u = m.panel.Uncertainties();
        const bool line = std::find(u.begin(), u.end(), "Entry: " + doubt + ".") != u.end();
        const bool label = drawnText().find(Norm("(entry from rts scan, 60%)")) != std::string::npos;
        const bool drawnLine = drawnText().find(Norm("Entry: " + doubt + ".")) != std::string::npos;
        const bool copied = m.panel.CopyMarks().find(doubt) != std::string::npos;
        MiniCheck(what, line && label && drawnLine && copied,
                  std::string("label ") + (label ? "kept" : "lost") + ", Entry line " + (line && drawnLine ? "drawn" : "missing") + ", Copy marks " +
                      (copied ? "carry it" : "do not carry it") + "; window: " + drawnText());
    };
    auto model = [&](MiniRig& m, const char* what) {
        trust::TrueState t;
        t.liveEmulator = true;
        t.entryDoubt = doubt;
        MiniVerify(m, what, t, "func_");
    };
    {
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        doubted(m, "A8-30: Follow PC on C+4, found by the rts scan: the figure and the Entry line");
        CHECK(ClickMini(m, "follow"));
        m.Idle(3);
        CHECK(m.Settle());
        MiniCheck("A8-30: ...Follow PC off pins C", !m.panel.FollowPc() && m.panel.WantedEntry() == cEntry);
        doubted(m, "A8-30: ...pinned, the entry is still the finder's guess: the figure and the Entry line stay");
        model(m, "A8-30: ...and the model holds the pin to the finder's doubt");
        // Pinned, a new snapshot asks the same entry again: the doubt goes with the target, not with the snapshot it was found on.
        EditMini(m);
        m.Idle(3);
        CHECK(m.Settle());
        doubted(m, "A8-30: ...an edit, answered again for the pinned entry: still the finder's guess");
        // Only the user's own entry drops it.
        TypeEntry(m, Hex(cEntry));
        m.Idle(3);
        CHECK(m.Settle());
        const std::vector<std::string> u = m.panel.Uncertainties();
        MiniCheck("A8-30: ...the same entry typed: the user's entry, no figure and no Entry line",
                  drawnText().find(Norm("(entry from user-set entry)")) != std::string::npos && u.empty() &&
                      m.panel.CopyMarks().find(doubt) == std::string::npos, "window: " + drawnText());
    }
    {
        // The other way in: the PC where no function is, so the box pins the entry of the output shown.
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        m.regs[0].pc = 0x00001FF0u;
        ++m.frame;
        m.Idle(3);
        CHECK(m.Settle());
        CHECK(m.panel.WantedEntry() == 0);
        CHECK(ClickMini(m, "follow"));
        m.Idle(3);
        CHECK(m.Settle());
        MiniCheck("A8-30: Follow PC off with the PC where no function is pins the output shown, C", !m.panel.FollowPc() && m.panel.WantedEntry() == cEntry);
        doubted(m, "A8-30: ...that entry was the finder's guess too: the figure and the Entry line stay");
        model(m, "A8-30: ...and the model holds it to the finder's doubt");
    }
}

// Enter on the entry box as the panel left it: a click into it, no edit, Enter (what the tooltips tell the reader to do).
// Click into the entry box without editing it (the box keeps the keyboard focus), and press Enter in it.
bool FocusEntryBox(MiniRig& m)
{
    ImVec2 a, b;
    if (!m.panel.ItemRect("entry", a, b)) return false;
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    for (int i = 0; i < 3; ++i) m.Frame(c, false);
    m.Frame(c, true);
    m.Frame(c, false);
    m.Idle(2);
    return true;
}

void PressEnterInBox(MiniRig& m)
{
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    m.Frame(ImVec2(1270, 710), false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    m.Frame(ImVec2(1270, 710), false);
}

bool EnterUntouched(MiniRig& m)
{
    if (!FocusEntryBox(m)) return false;
    PressEnterInBox(m);
    return true;
}

// (area 8 fixer) A8-36 to A8-39, A8-33: round 6's probe cases, made pins. Each states the verdict the label, the Entry line and the Copy marks
// must carry, and holds the window to the model with the label checked against what confirms the entry.
void RunEntryOriginPins()
{
    const uint32_t cEntry = Defs()[FnC].entry;
    const std::string guess = trust::RtsGuess();
    auto drawnText = [] {
        std::string all;
        for (const drawn::Run& run : drawn::WindowRuns(DecompilerPanel::kTitle)) all += run.text + " ";
        return Norm(all);
    };
    // The window says: this label, this Entry line (or none), in Uncertainties, drawn, and in the Copy marks.
    auto says = [&](MiniRig& m, const char* what, const std::string& label, const std::string& doubt) {
        m.Frame();
        const std::vector<std::string> u = m.panel.Uncertainties();
        const std::string w = drawnText();
        const bool labelOk = label.empty() ? w.find(Norm("(entry from")) == std::string::npos : w.find(Norm(label)) != std::string::npos;
        bool lineOk = true;
        if (doubt.empty())
        {
            for (const std::string& x : u) lineOk &= x.compare(0, 7, "Entry: ") != 0;
            lineOk &= m.panel.CopyMarks().find("Entry: ") == std::string::npos;
        }
        else
            lineOk = std::find(u.begin(), u.end(), "Entry: " + doubt + ".") != u.end() && w.find(Norm("Entry: " + doubt + ".")) != std::string::npos &&
                     m.panel.CopyMarks().find(doubt) != std::string::npos;
        MiniCheck(what, labelOk && lineOk, "want label '" + label + "' and Entry line '" + doubt + "'; window: " + w);
    };
    auto model = [&](MiniRig& m, const char* what, trust::TrueState t) {
        t.liveEmulator = m.kind == DecompViewKind::LivePaused || m.kind == DecompViewKind::LiveRunning;
        MiniVerify(m, what, t, nullptr);
    };
    auto follow = [&](MiniRig& m) {
        CHECK(ClickMini(m, "follow"));
        m.Idle(3);
        CHECK(m.Settle());
    };
    auto paused = [&](MiniRig& m, uint32_t pc) {
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = pc;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
    };
    trust::TrueState rts;
    rts.entryDoubt = guess;
    rts.evidenceKnown = true;
    {
        // A8-36, probe case A: the pin, an edit whose request is Stopped, the tooltip that says Enter, and Enter on the untouched box.
        MiniRig m;
        paused(m, cEntry + 4);
        follow(m);
        m.gate.Close();
        EditMini(m);
        m.Idle(3);
        CHECK(WaitHeldOrBusy(m));
        CHECK(ClickMini(m, "stop"));
        m.gate.Open();
        m.Idle(3);
        CHECK(m.Settle());
        MiniCheck("A8-36: a pin Stopped: the tooltip names Enter, which asks the pin again",
                  m.panel.StaleTooltip().find("press Enter in the") != std::string::npos, m.panel.StaleTooltip());
        CHECK(EnterUntouched(m));
        m.Idle(3);
        CHECK(m.Settle());
        says(m, "A8-36: ...Enter on the untouched box asks the pin again: still the finder's guess", "(entry from rts scan, 60%)", guess);
        model(m, "A8-36: ...and the model holds the label to what confirms the entry", rts);
    }
    {
        // A8-36, probe case B (A8-34 (3)): Enter on the box Follow PC filled.
        MiniRig m;
        paused(m, cEntry + 4);
        CHECK(EnterUntouched(m));
        m.Idle(3);
        CHECK(m.Settle());
        MiniCheck("A8-36: Enter on the box Follow PC filled pins the function found", !m.panel.FollowPc() && m.panel.WantedEntry() == cEntry);
        says(m, "A8-36: ...the finder's guess, not a user-set entry", "(entry from rts scan, 60%)", guess);
        model(m, "A8-36: ...held to the model", rts);
    }
    {
        // A8-42: the box has the focus (clicked, not edited) while the output moves on; Enter asks the output shown, as A8-36 does for an
        // untouched box, and not the panel's stale text as a user-set entry.
        MiniRig m;
        const uint32_t aEntry = Defs()[FnA].entry;
        paused(m, cEntry + 4);
        CHECK(FocusEntryBox(m));
        m.regs[0].pc = aEntry + 4;
        ++m.frame;
        m.Idle(6);
        CHECK(m.Settle());
        MiniCheck("A8-42: the focused box kept C while A is shown", m.panel.Shown() && m.panel.Shown()->requestedEntry == aEntry &&
                  std::string(m.panel.EntryText()) == Hex(cEntry), std::string(m.panel.EntryText()));
        PressEnterInBox(m);
        m.Idle(3);
        CHECK(m.Settle());
        MiniCheck("A8-42: Enter in the focused, unedited box asks the output shown (A), not C as typed",
                  m.panel.Shown() && m.panel.Shown()->requestedEntry == aEntry && m.panel.WantedEntry() == aEntry && std::string(m.panel.EntryText()) == Hex(aEntry),
                  std::string(m.panel.EntryText()));
        MiniCheck("A8-42: ...no 'user-set entry' label", drawnText().find(Norm("user-set entry")) == std::string::npos, drawnText());
    }
    {
        // A8-36, probe case I: the NoRegisters tooltip ("Turn Follow PC off and press Enter on an entry"), done as it says.
        MiniRig m;
        paused(m, cEntry + 4);
        m.haveRegs[0] = false;
        ++m.frame;
        m.Idle(3);
        CHECK(m.Settle());
        follow(m);
        CHECK(EnterUntouched(m));
        m.Idle(3);
        CHECK(m.Settle());
        says(m, "A8-36: no registers, Follow PC off and Enter on the entry, as the tooltip says: still the finder's guess", "(entry from rts scan, 60%)", guess);
        model(m, "A8-36: ...held to the model", rts);
    }
    {
        // A8-36, the converse: the box holds the entry the user typed, and the target is the pin of the same entry. Enter there asks the
        // user's entry, which is another question: the tooltip must not say it asks the pin again. Enter is then the user's entry.
        MiniRig m;
        paused(m, cEntry + 4);
        TypeEntry(m, Hex(cEntry));
        m.Idle(3);
        CHECK(m.Settle());
        follow(m);   // Follow PC on: the same function, so the box keeps the typed text
        follow(m);   // ...and off: the pin
        m.gate.Close();
        EditMini(m);
        m.Idle(3);
        CHECK(WaitHeldOrBusy(m));
        CHECK(ClickMini(m, "stop"));
        m.gate.Open();
        m.Idle(3);
        CHECK(m.Settle());
        MiniCheck("A8-36: a pin Stopped with the user's typed text in the box: the tooltip names Refresh only",
                  m.panel.StaleTooltip().find("You stopped its replacement; press Refresh") != std::string::npos, m.panel.StaleTooltip());
        CHECK(EnterUntouched(m));
        m.Idle(3);
        CHECK(m.Settle());
        says(m, "A8-36: ...and Enter there is the user's entry", "(entry from user-set entry)", "");
    }
    {
        // A8-37, probe case F: a symbol confirms the entry, Follow PC off pins it, the symbol is erased.
        MiniRig m;
        m.names[cEntry] = "foo";
        ++m.namesGen;
        paused(m, cEntry + 4);
        follow(m);
        says(m, "A8-37: a pin the symbol confirms", "(entry from symbol, 90%)", "");
        m.names.erase(cEntry);
        ++m.namesGen;
        m.Idle(3);
        CHECK(m.Settle());
        says(m, "A8-37: ...the symbol erased: the pin is the rts scan's guess again", "(entry from rts scan, 60%)", guess);
        model(m, "A8-37: ...held to the model", rts);
    }
    {
        // A8-37, probe case G: the call stack confirms it, then the stack goes and C is edited.
        MiniRig m;
        m.stackEntries.insert(cEntry);
        ++m.stackRevision;
        paused(m, cEntry + 4);
        follow(m);
        says(m, "A8-37: a pin the call stack confirms", "(entry from call-site target, 80%)", "");
        m.stackEntries.clear();
        ++m.stackRevision;
        EditMini(m);
        m.Idle(3);
        CHECK(m.Settle());
        says(m, "A8-37: ...the stack gone and C edited: the rts scan's guess again", "(entry from rts scan, 60%)", guess);
        model(m, "A8-37: ...held to the model", rts);
    }
    {
        // A8-33, round 5's probe case C: a function start set at the pinned entry from another panel confirms it.
        MiniRig m;
        paused(m, cEntry + 4);
        follow(m);
        m.bounds.SetEntry(cEntry);
        m.Idle(3);
        CHECK(m.Settle());
        says(m, "A8-33: a start set at the pinned entry: the user's entry, no doubt", "(entry from user-set entry)", "");
        trust::TrueState t;
        t.evidenceKnown = true;
        t.userEntry = true;
        model(m, "A8-33: ...held to the model", t);
        // ...and cleared again: the finder's guess again.
        m.bounds.ClearEntry(cEntry);
        m.Idle(3);
        CHECK(m.Settle());
        says(m, "A8-33: ...the start cleared: the rts scan's guess again", "(entry from rts scan, 60%)", guess);
    }
    {
        // A pin no source names any more: a start set below it, which the finder never overrides, takes the address in. No label, and the
        // panel's own line says so.
        MiniRig m;
        paused(m, cEntry + 4);
        follow(m);
        m.bounds.SetEntry(cEntry - 0x100);
        m.Idle(3);
        CHECK(m.Settle());
        const std::string unconfirmed = "kept by you from the finder, and no source names " + trust::Hex8(cEntry) +
                                        " as an entry now: the finder puts it in the function at " + trust::Hex8(cEntry - 0x100);
        says(m, "a pin no source names now: no label, and a line that says so", "", unconfirmed);
        trust::TrueState t;
        t.entryDoubt = unconfirmed;
        t.evidenceKnown = true;
        t.unconfirmedPin = true;
        model(m, "...held to the model", t);
    }
    for (int k = 0; k < 2; ++k)
    {
        // A8-38, probe cases C and D: Follow PC off with no function found and nothing shown pins nothing and asks nothing.
        MiniRig m;
        if (k == 1)
        {
            m.kind = DecompViewKind::LivePaused;
            m.regs[0].pc = 0x00001FF0u;
            m.haveRegs[0] = true;
        }
        m.Idle(3);
        CHECK(m.Settle());
        const uint64_t asked = m.panel.Submissions();
        follow(m);
        MiniCheck(k ? "A8-38: the PC in the BIOS, nothing shown, Follow PC off: nothing asked, nothing shown, the box empty"
                    : "A8-38: no registers, nothing shown, Follow PC off: nothing asked, nothing shown, the box empty",
                  m.panel.Submissions() == asked && !m.panel.Shown() && std::string(m.panel.EntryText()).empty() && !m.panel.FollowPc(),
                  std::string("box '") + m.panel.EntryText() + "', window: " + drawnText());
        trust::TrueState t;
        t.liveEmulator = k == 1;
        t.noEntry = trust::NoEntry::NothingPinned;
        MiniVerify(m, k ? "A8-38: ...held to the model (the PC in the BIOS)" : "A8-38: ...held to the model (no registers)", t, nullptr);
    }
    {
        // A8-39, probe case H: the PC moves where no function is; the output on screen keeps its own Entry line and Copy mark.
        MiniRig m;
        paused(m, cEntry + 4);
        m.regs[0].pc = 0x00001FF0u;
        ++m.frame;
        m.Idle(3);
        CHECK(m.Settle());
        says(m, "A8-39: the PC in the BIOS, C stale on screen: its label and its Entry line, drawn and copied", "(entry from rts scan, 60%)", guess);
    }
}

// Round 5 probe (SE_TRUST_PROBE5): the call stack changes under an answer that is not asked again.
void RunProbe5()
{
    const uint32_t cEntry = Defs()[FnC].entry;
    auto drawnText = [] {
        std::string all;
        for (const drawn::Run& run : drawn::WindowRuns(DecompilerPanel::kTitle)) all += run.text + " ";
        return Norm(all);
    };
    auto show = [&](MiniRig& m, const char* what) {
        m.Frame();
        std::string u;
        for (const std::string& x : m.panel.Uncertainties()) u += "[" + x + "]";
        const std::string w = drawnText();
        const size_t at = w.find(Norm("(entry from"));
        std::printf("PROBE5 %s\n   label: %s\n   uncertainties: %s\n   copy: %s\n", what,
                    at == std::string::npos ? "(none)" : w.substr(at, 40).c_str(), u.c_str(), m.panel.CopyMarks().c_str());
    };
    {
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "A: Follow PC C+4, rts scan");
        m.stackEntries.insert(cEntry);
        ++m.stackRevision;
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "A: the call stack now confirms C (revision bumped)");
        m.stackEntries.clear();
        ++m.stackRevision;
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "A: the call stack gone again");
    }
    {
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.stackEntries.insert(cEntry);
        ++m.stackRevision;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "B: stack confirms C first");
        m.stackEntries.clear();
        ++m.stackRevision;
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "B: stack gone: C is a guess again, the label?");
    }
    {
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        CHECK(ClickMini(m, "follow"));
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "C: pinned (Follow PC off), the finder's guess");
        m.bounds.SetEntry(cEntry);   // a function start recorded at the pinned entry (another panel's Set Function Start Here)
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "C: ...a user function start is now recorded at the pinned entry");
    }
}

// Round 6 probe (SE_TRUST_PROBE6): Enter on the untouched entry box after a pin, as the stale tooltip says; a pin with nothing found and
// nothing shown; the call stack under a Decompile target whose entry is also a callee.
void RunProbe6()
{
    const uint32_t cEntry = Defs()[FnC].entry, aEntry = Defs()[FnA].entry;
    auto drawnText = [] {
        std::string all;
        for (const drawn::Run& run : drawn::WindowRuns(DecompilerPanel::kTitle)) all += run.text + " ";
        return Norm(all);
    };
    auto show = [&](MiniRig& m, const char* what) {
        m.Frame();
        std::string u;
        for (const std::string& x : m.panel.Uncertainties()) u += "[" + x + "]";
        const std::string w = drawnText();
        const size_t at = w.find(Norm("(entry from"));
        const DecompileResult* s = m.panel.Shown();
        std::printf("PROBE6 %s\n   label: %s\n   uncertainties: %s\n   box: %s, follow %d, wanted %08X, shown %s, stale %d, pending %d\n"
                    "   tooltip: %s\n   error: %s\n   copy: %s\n",
                    what, at == std::string::npos ? "(none)" : w.substr(at, 40).c_str(), u.c_str(), m.panel.EntryText(), m.panel.FollowPc() ? 1 : 0,
                    m.panel.WantedEntry(), s ? Hex(s->requestedEntry).c_str() : "nothing", m.panel.ShownStale() ? 1 : 0, m.panel.Pending() ? 1 : 0,
                    m.panel.StaleTooltip().c_str(), m.panel.EntryError().c_str(), m.panel.CopyMarks().c_str());
    };
    auto enterUntouched = [](MiniRig& m) {
        ImVec2 a, b;
        if (!m.panel.ItemRect("entry", a, b)) return false;
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        for (int i = 0; i < 3; ++i) m.Frame(c, false);
        m.Frame(c, true);
        m.Frame(c, false);
        m.Idle(2);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
        m.Frame(ImVec2(1270, 710), false);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
        m.Frame(ImVec2(1270, 710), false);
        return true;
    };
    {
        // A: the pin, an edit whose request is Stopped, then Enter in the box as the Stopped tooltip says.
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "A: Follow PC on C+4, rts scan");
        CHECK(ClickMini(m, "follow"));
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "A: pinned (Follow PC off)");
        m.gate.Close();
        EditMini(m);
        m.Idle(3);
        CHECK(WaitHeldOrBusy(m));
        CHECK(ClickMini(m, "stop"));
        m.gate.Open();
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "A: an edit, its request Stopped");
        CHECK(enterUntouched(m));
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "A: Enter in the untouched box, as the tooltip says");
    }
    {
        // B: the same without the Stop: Enter on the box the panel filled, under Follow PC (A8-34 (3)).
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        CHECK(enterUntouched(m));
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "B: Follow PC on C+4, Enter in the untouched box");
    }
    {
        // C: nothing found and nothing shown (a dump without registers), Follow PC off.
        MiniRig m;
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "C: Follow PC, no registers, nothing shown");
        CHECK(ClickMini(m, "follow"));
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "C: Follow PC off");
    }
    {
        // D: the PC where no function is, nothing shown, Follow PC off.
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = 0x00001FF0u;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "D: Follow PC, PC in the BIOS, nothing shown");
        CHECK(ClickMini(m, "follow"));
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "D: Follow PC off");
    }
    {
        // E: Decompile at A+2 on the snapshot C was shown on (A is C's callee) with the call stack holding A; then the stack goes.
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        m.stackEntries.insert(aEntry);
        ++m.stackRevision;
        m.panel.ShowFunctionContaining(0, aEntry + 2);
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "E: Decompile at A+2, the stack holds A, A is C's callee");
        m.stackEntries.clear();
        ++m.stackRevision;
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "E: the stack gone: the finder now says callee, 70%");
        CHECK(ClickMini(m, "refresh"));
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "E: ...Refresh: the verdict now");
    }
    {
        // F: a symbol at C; Follow PC on C+4 finds it by the symbol; Follow PC off pins it; the symbol is then removed (Rename to empty).
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.names[cEntry] = "foo";
        ++m.namesGen;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "F: Follow PC on C+4, C named foo");
        CHECK(ClickMini(m, "follow"));
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "F: pinned (Follow PC off)");
        m.names.erase(cEntry);
        ++m.namesGen;
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "F: the name removed: answered again for the pinned entry");
        CHECK(ClickMini(m, "follow"));
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "F: ...Follow PC on again: the finder's verdict now");
    }
    {
        // G: the same with a call-stack entry at C instead of a symbol, the stack then gone and C edited.
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.stackEntries.insert(cEntry);
        ++m.stackRevision;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        CHECK(ClickMini(m, "follow"));
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "G: Follow PC on C+4 with the stack holding C, pinned");
        m.stackEntries.clear();
        ++m.stackRevision;
        EditMini(m);
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "G: the stack gone and C edited: answered again for the pinned entry");
    }
    {
        // H: the output's own doubt when the question moves on: the PC steps into the BIOS (no function), C stays on screen, stale.
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        m.regs[0].pc = 0x00001FF0u;
        ++m.frame;
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "H: the PC in the BIOS, C stale on screen");
        std::printf("   window: %s\n", drawnText().c_str());
    }
    {
        // I: the registers go; the NoRegisters tooltip says "Turn Follow PC off and press Enter on an entry". Done as it says.
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = cEntry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        m.haveRegs[0] = false;
        ++m.frame;
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "I: no registers now, C stale on screen");
        CHECK(ClickMini(m, "follow"));
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "I: Follow PC off (pins the output shown, with its doubt)");
        CHECK(enterUntouched(m));
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "I: ...and Enter on the entry in the box, as the tooltip says");
    }
    {
        // C again, the whole window.
        MiniRig m;
        m.Idle(3);
        CHECK(m.Settle());
        CHECK(ClickMini(m, "follow"));
        m.Idle(3);
        CHECK(m.Settle());
        show(m, "C': Follow PC off with nothing found or shown");
        std::printf("   window: %s\n", drawnText().c_str());
        const DecompileResult* s = m.panel.Shown();
        if (s) std::printf("   status %d, error: %s\n", static_cast<int>(s->status), s->error.c_str());
    }
}

// Round 7 probe (SE_TRUST_PROBE7): hand-built sequences against A8-33, A8-36 to A8-39: pins across snapshots, starts and names appearing and
// going, Follow PC on and off, Enter on an edited and on an untouched box, Stop and ask again, a typed entry the finder then finds as well.
void RunProbe7()
{
    const uint32_t cEntry = Defs()[FnC].entry, aEntry = Defs()[FnA].entry;
    auto drawnText = [] {
        std::string all;
        for (const drawn::Run& run : drawn::WindowRuns(DecompilerPanel::kTitle)) all += run.text + " ";
        return Norm(all);
    };
    auto show = [&](MiniRig& m, const char* what) {
        m.Frame();
        std::string u;
        for (const std::string& x : m.panel.Uncertainties()) u += "[" + x + "]";
        const std::string w = drawnText();
        const size_t at = w.find(Norm("(entry from"));
        const DecompileResult* s = m.panel.Shown();
        std::printf("PROBE7 %s\n   label: %s\n   uncertainties: %s\n   box: %s, follow %d, wanted %08X, shown %s, stale %d, pending %d\n"
                    "   tooltip: %s\n   error: %s\n   copy: %s\n",
                    what, at == std::string::npos ? "(none)" : w.substr(at, 40).c_str(), u.c_str(), m.panel.EntryText(), m.panel.FollowPc() ? 1 : 0,
                    m.panel.WantedEntry(), s ? Hex(s->requestedEntry).c_str() : "nothing", m.panel.ShownStale() ? 1 : 0, m.panel.Pending() ? 1 : 0,
                    m.panel.StaleTooltip().c_str(), m.panel.EntryError().c_str(), m.panel.CopyMarks().c_str());
    };
    auto enterUntouched = [](MiniRig& m) {
        ImVec2 a, b;
        if (!m.panel.ItemRect("entry", a, b)) return false;
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        for (int i = 0; i < 3; ++i) m.Frame(c, false);
        m.Frame(c, true);
        m.Frame(c, false);
        m.Idle(2);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
        m.Frame(ImVec2(1270, 710), false);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
        m.Frame(ImVec2(1270, 710), false);
        return true;
    };
    auto settle = [&](MiniRig& m) {
        m.Idle(3);
        CHECK(m.Settle());
    };
    auto live = [&](MiniRig& m, uint32_t pc) {
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = pc;
        m.haveRegs[0] = true;
    };
    {
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        CHECK(ClickMini(m, "follow"));
        settle(m);
        show(m, "1: C pinned");
        m.regs[0].pc = aEntry + 4;
        ++m.frame;
        settle(m);
        show(m, "1: the PC moved into A, next frame: the pin still C");
        m.bounds.SetEntry(cEntry);
        settle(m);
        show(m, "1: a start set at C");
        CHECK(enterUntouched(m));
        settle(m);
        show(m, "1: Enter on the untouched box (the start set)");
        CHECK(ClickMini(m, "follow"));
        settle(m);
        show(m, "1: Follow PC on (PC in A)");
    }
    {
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        CHECK(ClickMini(m, "follow"));
        settle(m);
        m.bounds.SetEntry(cEntry + 2);
        settle(m);
        show(m, "2: pinned C, a start set at C+2 (inside C, below the PC)");
        m.bounds.SetEntry(cEntry);
        settle(m);
        show(m, "2: ...and one at C as well");
    }
    {
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        TypeEntry(m, Hex(cEntry));
        settle(m);
        show(m, "3: C typed with Follow PC on, the finder finds the same C");
        CHECK(ClickMini(m, "follow"));
        settle(m);
        show(m, "3: Follow PC (it was turned off by the typed entry) on again");
        CHECK(ClickMini(m, "follow"));
        settle(m);
        show(m, "3: off again, a pin");
        ++m.frame;
        settle(m);
        show(m, "3: next frame");
        CHECK(enterUntouched(m));
        settle(m);
        show(m, "3: Enter on the untouched box");
    }
    {
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        m.gate.Close();
        ++m.frame;
        m.regs[0].pc = aEntry + 4;
        m.Idle(3);
        CHECK(WaitHeldOrBusy(m));
        CHECK(ClickMini(m, "stop"));
        m.gate.Open();
        settle(m);
        show(m, "4: Follow PC, PC moved to A, the request stopped (C on screen)");
        CHECK(enterUntouched(m));
        settle(m);
        show(m, "4: Enter on the untouched box (shows C)");
    }
    {
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        CHECK(ClickMini(m, "follow"));
        settle(m);
        TypeEntry(m, Hex(cEntry));
        settle(m);
        show(m, "5: pinned C, then C typed and Entered");
        m.names[cEntry] = "foo";
        ++m.namesGen;
        settle(m);
        show(m, "5: C named");
        m.names.erase(cEntry);
        ++m.namesGen;
        settle(m);
        show(m, "5: name removed");
    }
    {
        MiniRig m;
        m.kind = DecompViewKind::LivePaused;
        m.haveRegs[0] = false;
        m.panel.ShowFunctionContaining(0, aEntry + 2);
        settle(m);
        show(m, "6: Decompile A+2");
        CHECK(enterUntouched(m));
        settle(m);
        show(m, "6: Enter on the untouched box");
        m.names[aEntry] = "bar";
        ++m.namesGen;
        settle(m);
        show(m, "6: A named");
        m.names.erase(aEntry);
        ++m.namesGen;
        m.bounds.SetEntry(aEntry);
        settle(m);
        show(m, "6: name gone, start at A");
    }
    {
        MiniRig m;
        live(m, 0x00001FF0u);
        settle(m);
        show(m, "7: PC in the BIOS, nothing shown");
        CHECK(ClickMini(m, "follow"));
        settle(m);
        show(m, "7: Follow PC off (nothing to pin)");
        m.regs[0].pc = cEntry + 4;
        ++m.frame;
        settle(m);
        show(m, "7: PC in C now, frame 2");
        CHECK(enterUntouched(m));
        settle(m);
        show(m, "7: Enter on the empty box");
        CHECK(ClickMini(m, "follow"));
        settle(m);
        show(m, "7: Follow PC on");
    }
    {
        MiniRig m;
        live(m, cEntry + 4);
        m.regs[1].pc = aEntry + 4;
        m.haveRegs[1] = true;
        settle(m);
        CHECK(ClickMini(m, "follow"));
        settle(m);
        m.panel.SetCpu(1);
        settle(m);
        show(m, "8: pinned C, CPU 1 chosen (PC in A)");
        CHECK(ClickMini(m, "follow"));
        settle(m);
        show(m, "8: Follow PC on, CPU 1");
        CHECK(ClickMini(m, "follow"));
        settle(m);
        show(m, "8: Follow PC off, CPU 1: pin A");
    }
    {
        // 9: the box has the focus (clicked, not edited) while the output moves on; then Enter.
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        show(m, "9: Follow PC on C+4");
        ImVec2 a, b;
        CHECK(m.panel.ItemRect("entry", a, b));
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        for (int i = 0; i < 3; ++i) m.Frame(c, false);
        m.Frame(c, true);
        m.Frame(c, false);
        m.Idle(2);
        m.regs[0].pc = aEntry + 4;
        ++m.frame;
        m.Idle(6);
        CHECK(m.Settle());
        show(m, "9: the box focused, the PC moved to A, A shown, the box still C");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
        m.Frame(ImVec2(1270, 710), false);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
        m.Frame(ImVec2(1270, 710), false);
        settle(m);
        show(m, "9: Enter in the box (text the panel wrote): fixed (A8-42), A is asked with its origin, box A");
        MiniCheck("9: A8-42 fixed", m.panel.WantedEntry() == aEntry && std::string(m.panel.EntryText()) == Hex(aEntry));
    }
    {
        // 10: the long saved volatile list the popup cannot hold: what Apply on its text does.
        MiniRig m;
        std::string list = "06002000, ";   // 10 + 107 * 19 + 4 = 2047: the buffer ends after "0600" of the 108th range
        while (list.size() < 2300) list += "06001000-06001003, ";
        list += "06002000";
        m.panel.KeepUnparsedVolatileMemory(list);
        std::printf("PROBE7 10: saved list of %zu chars, not in force %d\n   problem: %s\n", list.size(), m.panel.VolatileNotInForce() ? 1 : 0,
                    m.panel.UnparsedVolatileProblem().c_str());
        m.panel.BeginVolatileEdit();   // what the popup does on opening
        const std::string edit = m.panel.VolatileEditText();
        const bool ok = m.panel.ApplyVolatileEdit();
        std::printf("   Apply on the buffer (%zu chars, ends '%s'): %d, in force %d, saved list kept whole %d\n   said: %s\n", edit.size(),
                    edit.size() > 12 ? edit.c_str() + edit.size() - 12 : edit.c_str(), ok ? 1 : 0, m.panel.VolatileNotInForce() ? 0 : 1,
                    m.panel.VolatileMemory() == list ? 1 : 0, m.panel.VolatileEditError().c_str());
        CHECK(!ok && m.panel.VolatileNotInForce() && m.panel.VolatileMemory() == list);   // A8-43 fixed: expected behaviour
    }
}

// Round 8 probe (SE_TRUST_PROBE8): the A8-42 and A8-43 fixes attacked. The entry box edited and brought back to the panel's text (Escape,
// Backspace, Ctrl+Z) before Enter; the box focused across an install, a Stop, a CPU switch, a rename, a PC with no function; the Volatile
// popup drawn and driven with the mouse and the keyboard, with saved lists at, just under and just over its box, an edit made and undone,
// and a multibyte character cut in two.
void RunProbe8()
{
    const uint32_t cEntry = Defs()[FnC].entry, aEntry = Defs()[FnA].entry;
    auto drawnText = [] {
        std::string all;
        for (const drawn::Run& run : drawn::WindowRuns(DecompilerPanel::kTitle)) all += run.text + " ";
        return Norm(all);
    };
    auto show = [&](MiniRig& m, const char* what) {
        m.Frame();
        std::string u;
        for (const std::string& x : m.panel.Uncertainties()) u += "[" + x + "]";
        const std::string w = drawnText();
        const size_t at = w.find(Norm("(entry from"));
        const DecompileResult* s = m.panel.Shown();
        std::printf("PROBE8 %s\n   label: %s\n   uncertainties: %s\n   box: %s, active %d, follow %d, wanted %08X, shown %s, stale %d, pending %d\n"
                    "   tooltip: %s\n   error: %s\n   copy: %s\n",
                    what, at == std::string::npos ? "(none)" : w.substr(at, 40).c_str(), u.c_str(), m.panel.EntryText(), m.panel.EntryBoxActive() ? 1 : 0,
                    m.panel.FollowPc() ? 1 : 0, m.panel.WantedEntry(), s ? Hex(s->requestedEntry).c_str() : "nothing", m.panel.ShownStale() ? 1 : 0,
                    m.panel.Pending() ? 1 : 0, m.panel.StaleTooltip().c_str(), m.panel.EntryError().c_str(), m.panel.CopyMarks().c_str());
    };
    auto settle = [&](MiniRig& m) {
        m.Idle(3);
        CHECK(m.Settle());
    };
    auto live = [&](MiniRig& m, uint32_t pc) {
        m.kind = DecompViewKind::LivePaused;
        m.regs[0].pc = pc;
        m.haveRegs[0] = true;
    };
    auto focusBox = [](MiniRig& m) {
        ImVec2 a, b;
        if (!m.panel.ItemRect("entry", a, b)) return false;
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        for (int i = 0; i < 3; ++i) m.Frame(c, false);
        m.Frame(c, true);
        m.Frame(c, false);
        m.Idle(2);
        return m.panel.EntryBoxActive();
    };
    auto enter = [](MiniRig& m) {
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
        m.Frame(ImVec2(1270, 710), false);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
        m.Frame(ImVec2(1270, 710), false);
    };
    auto typeChar = [](MiniRig& m, char ch) {
        ImGui::GetIO().AddInputCharacter(static_cast<unsigned>(ch));
        m.Idle(2);
    };
    auto ctrl = [](MiniRig& m, ImGuiKey key) {
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
        ImGui::GetIO().AddKeyEvent(key, true);
        m.Frame();
        ImGui::GetIO().AddKeyEvent(key, false);
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
        m.Frame();
    };
    // E1 to E3: Follow PC on C (rts scan, 60%, the Entry line). The box is focused, a character typed, and the edit taken back three ways; then
    // Enter on text that is the panel's again.
    for (int way = 0; way < 3; ++way)
    {
        static const char* const kWay[] = { "Escape (ImGui reverts the edit and drops the focus), then the box clicked again",
                                            "Backspace", "Ctrl+Z" };
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        std::string title = std::string("E") + std::to_string(way + 1) + ": Follow PC on C+4";
        show(m, title.c_str());
        CHECK(focusBox(m));
        typeChar(m, '1');
        std::printf("PROBE8 E%d: typed '1': box %s\n", way + 1, m.panel.EntryText());
        if (way == 0)
        {
            m.Key(ImGuiKey_Escape);
            std::printf("PROBE8 E1: Escape: box %s, active %d\n", m.panel.EntryText(), m.panel.EntryBoxActive() ? 1 : 0);
            CHECK(focusBox(m));
        }
        else if (way == 1)
            m.Key(ImGuiKey_Backspace);
        else
            ctrl(m, ImGuiKey_Z);
        title = std::string("E") + std::to_string(way + 1) + ": the edit taken back by " + kWay[way] + "; box before Enter";
        show(m, title.c_str());
        enter(m);
        settle(m);
        title = std::string("E") + std::to_string(way + 1) + ": Enter on the box (text the panel wrote, brought back)";
        show(m, title.c_str());
    }
    {
        // E4: the box focused while C is shown; the PC moves to A and A installs (box still C); a character typed and deleted (the text is C,
        // the panel's old text); Enter.
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        CHECK(focusBox(m));
        m.regs[0].pc = aEntry + 4;
        ++m.frame;
        m.Idle(6);
        CHECK(m.Settle());
        show(m, "E4: focused, the PC moved to A, A shown, box C");
        typeChar(m, '1');
        m.Key(ImGuiKey_Backspace);
        enter(m);
        settle(m);
        show(m, "E4: '1' typed and deleted, Enter (box C, the user's keys)");
    }
    {
        // E5: the box focused with C shown; the PC moves to A with the engine held; the request is stopped (the click takes the focus); then the
        // box is clicked and Enter pressed on its untouched text.
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        CHECK(focusBox(m));
        m.gate.Close();
        m.regs[0].pc = aEntry + 4;
        ++m.frame;
        m.Idle(3);
        CHECK(WaitHeldOrBusy(m));
        show(m, "E5: focused, PC to A, A held in the engine");
        CHECK(ClickMini(m, "stop"));
        m.gate.Open();
        settle(m);
        show(m, "E5: Stop clicked (focus gone)");
        CHECK(focusBox(m));
        enter(m);
        settle(m);
        show(m, "E5: Enter on the untouched box");
    }
    {
        // E6: the box focused on CPU 0's C; the CPU switched to 1 (PC in A) without a click (SetCpu), A installs while the box is focused; Enter.
        MiniRig m;
        live(m, cEntry + 4);
        m.regs[1].pc = aEntry + 4;
        m.haveRegs[1] = true;
        settle(m);
        CHECK(focusBox(m));
        m.panel.SetCpu(1);
        m.Idle(4);
        CHECK(m.Settle());
        show(m, "E6: focused, CPU 1 (PC in A), A shown, box C");
        enter(m);
        settle(m);
        show(m, "E6: Enter (untouched)");
        m.panel.SetCpu(0);
        settle(m);
        show(m, "E6: CPU 0 again (a pin: nothing moves)");
    }
    {
        // E7: the box focused on C; C named while focused (the same function installed again, no sync); Enter. Then the name erased.
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        CHECK(focusBox(m));
        m.names[cEntry] = "foo";
        ++m.namesGen;
        m.Idle(4);
        CHECK(m.Settle());
        show(m, "E7: focused, C named");
        enter(m);
        settle(m);
        show(m, "E7: Enter (untouched)");
        m.names.erase(cEntry);
        ++m.namesGen;
        settle(m);
        show(m, "E7: the name erased");
    }
    {
        // E8: the box focused on C; the PC moves into the BIOS (no function): C stays, stale; Enter.
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        CHECK(focusBox(m));
        m.regs[0].pc = 0x00001FF0u;
        ++m.frame;
        m.Idle(4);
        CHECK(m.Settle());
        show(m, "E8: focused, PC in the BIOS");
        enter(m);
        settle(m);
        show(m, "E8: Enter (untouched)");
    }
    {
        // E9: an entry typed (A) and asked; the box focused; the PC moves (nothing: an Entry target); Follow PC turned on by a click (focus
        // gone) and C installs, then the box focused again and the PC moved to A, then Enter.
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        TypeEntry(m, Hex(aEntry));
        settle(m);
        show(m, "E9: A typed");
        CHECK(ClickMini(m, "follow"));
        settle(m);
        show(m, "E9: Follow PC on (C)");
        CHECK(focusBox(m));
        m.regs[0].pc = aEntry + 4;
        ++m.frame;
        m.Idle(4);
        CHECK(m.Settle());
        show(m, "E9: focused, PC to A (A shown, box C)");
        enter(m);
        settle(m);
        show(m, "E9: Enter (untouched)");
    }
    {
        // E10: the box's own tooltip ("Decompile the function starting at this address ... Enter to go.") while it holds the focus and an older
        // address than the output (A8-42's state): what Enter asks then is the output shown, not the address in the box.
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        CHECK(focusBox(m));
        m.regs[0].pc = aEntry + 4;
        ++m.frame;
        m.Idle(6);
        CHECK(m.Settle());
        ImVec2 a, b;
        CHECK(m.panel.ItemRect("entry", a, b));
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        bool found = false;
        std::string tip;
        for (int i = 0; i < 90 && !found; ++i)
        {
            m.Frame(c, false);
            tip = drawn::TooltipText(&found);
        }
        std::printf("PROBE8 E10: focused, A shown, box %s, active %d; the box hovered: tooltip %s\n   %s\n", m.panel.EntryText(),
                    m.panel.EntryBoxActive() ? 1 : 0, found ? "shown" : "not shown", Norm(tip).c_str());
        enter(m);
        settle(m);
        show(m, "E10: Enter");
    }

    // The Volatile popup, drawn. The button and the popup's items are found by their drawn text.
    auto popupWindow = []() -> ImGuiWindow* {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        for (ImGuiWindow* w : g.Windows)
            if ((w->Flags & ImGuiWindowFlags_Popup) && w->Active && !(w->Flags & ImGuiWindowFlags_Tooltip)) return w;
        return nullptr;
    };
    auto clickAt = [](MiniRig& m, ImVec2 c) {
        for (int i = 0; i < 3; ++i) m.Frame(c, false);
        m.Frame(c, true);
        m.Frame(c, false);
        m.Frame(c, false);
    };
    auto runCentre = [](const std::vector<drawn::Run>& runs, const std::string& text, ImVec2& c) {
        for (const drawn::Run& r : runs)
            if (r.text == text)
            {
                c = ImVec2((r.x0 + r.x1) * 0.5f, (r.y0 + r.y1) * 0.5f);
                return true;
            }
        return false;
    };
    auto openPopup = [&](MiniRig& m) {
        m.Idle(2);
        ImVec2 c;
        if (!runCentre(drawn::WindowRuns(DecompilerPanel::kTitle), "Volatile", c)) return false;
        clickAt(m, c);
        return popupWindow() != nullptr;
    };
    auto popupText = [&]() {
        ImGuiWindow* w = popupWindow();
        std::string all;
        if (w)
            for (const drawn::Run& r : drawn::Runs(w->DrawList)) all += r.text + " | ";
        return all;
    };
    auto clickApply = [&](MiniRig& m) {
        ImGuiWindow* w = popupWindow();
        ImVec2 c;
        if (!w || !runCentre(drawn::Runs(w->DrawList), "Apply", c)) return false;
        clickAt(m, c);
        return true;
    };
    auto clickBox = [&](MiniRig& m) {
        // The edit box is the frame under the first line of text: click just below the popup's first run.
        ImGuiWindow* w = popupWindow();
        if (!w) return false;
        std::vector<drawn::Run> rs = drawn::Runs(w->DrawList);
        if (rs.empty()) return false;
        const float y = rs[0].y1 + ImGui::GetFrameHeight() * 0.5f + ImGui::GetStyle().ItemSpacing.y;
        clickAt(m, ImVec2(rs[0].x0 + 40.0f, y));
        return true;
    };
    auto tail = [](const std::string& s) { return s.size() > 14 ? "..." + s.substr(s.size() - 14) : s; };
    auto report = [&](MiniRig& m, const char* what, const std::string& saved) {
        const std::string t = popupText();
        std::printf("PROBE8 %s\n   popup open %d: %s\n   saved list %zu chars (%s), whole %d, not in force %d, edit %zu chars (%s)\n", what,
                    popupWindow() ? 1 : 0, t.size() > 400 ? (t.substr(0, 200) + " ... " + t.substr(t.size() - 200)).c_str() : t.c_str(),
                    m.panel.VolatileMemory().size(), tail(m.panel.VolatileMemory()).c_str(), m.panel.VolatileMemory() == saved ? 1 : 0,
                    m.panel.VolatileNotInForce() ? 1 : 0, m.panel.VolatileEditText().size(), tail(m.panel.VolatileEditText()).c_str());
    };
    auto listOf = [](size_t n) {
        // A list that parses, exactly n characters: "06002000, " then ranges, padded with spaces before the last address.
        std::string list = "06002000";
        while (list.size() + 19 + 10 <= n) list += ", 06001000-06001003";
        list += ",";
        while (list.size() + 9 < n) list += " ";
        list += " 06003000";
        return list;
    };
    for (size_t n : { static_cast<size_t>(2046), static_cast<size_t>(2047), static_cast<size_t>(2048), static_cast<size_t>(2049) })
    {
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        const std::string list = listOf(n);
        CHECK(list.size() == n);
        std::string err;
        if (!m.panel.SetVolatileMemory(list, &err)) m.panel.KeepUnparsedVolatileMemory(list);   // what App does at start
        char what[160];
        std::snprintf(what, sizeof what, "V%zu: a saved list of %zu characters that parses (set: %s)", n, n, err.empty() ? "in force" : err.c_str());
        CHECK(openPopup(m));
        report(m, what, list);
        CHECK(clickApply(m));
        report(m, "   ...Apply clicked on the text unchanged", list);
        if (!popupWindow()) CHECK(openPopup(m));
        CHECK(clickBox(m));
        m.Key(ImGuiKey_End);
        typeChar(m, '7');
        report(m, "   ...a '7' typed at the end", list);
        m.Key(ImGuiKey_Backspace);
        report(m, "   ...and deleted when it went in (edit-then-revert)", list);
        CHECK(clickApply(m));
        report(m, "   ...Apply", list);
        if (!popupWindow()) CHECK(openPopup(m));
        CHECK(clickBox(m));
        typeChar(m, 'x');
        m.Key(ImGuiKey_Escape);
        report(m, "   ...'x' typed and Escape pressed in the box", list);
        if (!popupWindow()) CHECK(openPopup(m));
        if (popupWindow())
        {
            CHECK(clickApply(m));
            report(m, "   ...Apply after the Escape", list);
        }
    }
    {
        // V-mb: a saved list cut in the middle of a two-byte character (it does not parse: the character is no address).
        MiniRig m;
        live(m, cEntry + 4);
        settle(m);
        std::string list = listOf(2046);
        list.erase(list.size() - 9);
        while (list.size() < 2046) list += " ";
        list += "\xC3\xA9 06003000";   // 'é': its first byte is the 2047th
        m.panel.KeepUnparsedVolatileMemory(list);
        CHECK(openPopup(m));
        report(m, "Vmb: a saved list cut in the middle of 'é' (the box holds its first byte)", list);
        CHECK(clickBox(m));
        report(m, "   ...the box clicked", list);
        CHECK(clickApply(m));
        report(m, "   ...Apply on it unchanged", list);
        if (!popupWindow()) CHECK(openPopup(m));
        CHECK(clickBox(m));
        m.Key(ImGuiKey_Home);
        typeChar(m, '1');
        m.Key(ImGuiKey_Backspace);
        report(m, "   ...a '1' typed at the start and deleted", list);
        CHECK(clickApply(m));
        report(m, "   ...Apply", list);
        if (!popupWindow()) CHECK(openPopup(m));
        CHECK(clickBox(m));
        m.Key(ImGuiKey_End);
        m.Key(ImGuiKey_Backspace);
        report(m, "   ...Backspace at the end (the cut byte)", list);
        CHECK(clickApply(m));
        report(m, "   ...Apply", list);
    }
}

void RunFixPins()
{
    RunPinnedEntryPins();
    RunEntryOriginPins();
    RunWidthPins();
    RunTooltipPins();
    RunRefusedTextPins();
    RunCopyPins();
    RunAutoRefreshPin();
    CHECK(gMiniFailures == 0);
}


// ---- A real allocation failure inside the engine (A8-15 (b), A8-23) -------------------------------------------------------------
//
// The outcomes Failed ("out of memory") and the damaged engine are the ones the panel must show when the machine runs out of memory in
// the middle of a decompilation. Round 1 offered made-up results to the panel; here allocation k of one request of C throws, for every
// stride'th k, each in a child process (a failed engine is not meant to be reused), and the window the panel draws is held to the model:
// the red line, no output, no badge on a current panel -- and after a Refresh the engine says it may be damaged. A cell past the last
// allocation of the request is simply an answer. Unimplemented stays made up (the spec has no unimplemented instruction), and the
// cold start is area 6's (the language files are filled before the first request here).
#ifndef _WIN32
int OomCell(long k)
{
    MiniRig m;
    const uint32_t cEntry = Defs()[FnC].entry;
    m.gate.Close();
    m.panel.ShowFunctionContaining(0, cEntry + 4);
    m.Idle(3);
    if (!WaitHeldOrBusy(m))
    {
        std::printf("oom cell %ld: the engine never held\n", k);
        return 3;
    }
    dts::AllocCount() = 0;
    dts::FailAfter() = k;
    m.gate.Open();
    const auto t0 = std::chrono::steady_clock::now();
    while (m.runner.Busy() && dts::MsSince(t0) < 60000.0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const long used = dts::AllocCount().load();
    const bool fired = dts::FailAfter().load() == -1;
    dts::FailAfter() = -1;
    m.Idle(4);
    if (!m.Settle()) return 4;
    const DecompileResult* s = m.panel.Shown();
    trust::TrueState t;
    t.entryDoubt = "guessed from the previous rts; no symbol, call or user entry confirms it";
    t.sessionDisabled = m.runner.Disabled();
    t.disabledReason = m.runner.DisabledReason();
    std::string err = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, true);
    const bool failed = s && s->status == DecompileStatus::Failed;
    const bool answered = s && s->status == DecompileStatus::Ok;
    if (err.empty() && fired && !failed && !answered) err = "oom: status " + std::to_string(s ? static_cast<int>(s->status) : -1) + " after a failed allocation\n";
    // Decompile on the same function after the failed answer asks again (A8-18: it asked nothing, and the red line stayed with no sign that the
    // request had been taken): the panel submits, and the engine's reply is held to the model.
    if (err.empty() && failed)
    {
        const uint64_t sub = m.panel.Submissions();
        m.panel.ShowFunctionContaining(0, cEntry + 4);
        m.Idle(4);
        if (!m.Settle()) return 4;
        s = m.panel.Shown();
        if (m.panel.Submissions() == sub) err = "oom: Decompile on the function after a failed answer asked nothing\n";
        else err = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, true);
    }
    // Refresh once more: an engine damaged by the failure refuses, and the panel says so, or the answer is right.
    std::string err2;
    if (err.empty() && ClickMini(m, "refresh"))
    {
        m.Idle(3);
        if (!m.Settle()) return 4;
        s = m.panel.Shown();
        err2 = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, true);
        if (err2.empty() && fired && failed && (!s || s->status != DecompileStatus::Unavailable || s->error.find("ran out of memory earlier") == std::string::npos))
            err2 = "oom: after the failure and a Refresh the panel does not say the engine may be damaged\n";
    }
    if (!err.empty() || !err2.empty())
    {
        std::printf("oom cell %ld (%s, %ld allocations): %s%s\n", k, fired ? "fired" : "not reached", used, err.c_str(), err2.c_str());
        return 1;
    }
    return failed ? 10 : 0;   // 10: a failure, shown and held to the model
}

// Returns the number of cells that are not clean; `failedCells` counts the cells in which the allocation failed.
int RunOomSweep(long stride, int& cells, int& failedCells)
{
    // Size the request first: its allocations counted from the held engine, in a child.
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
    int bad = 0;
    cells = failedCells = 0;
    for (long k = 0; k <= total + 5; k += stride)
    {
        std::fflush(stdout);
        const pid_t pid = fork();
        if (pid == 0) _exit(OomCell(k));
        int st = 0;
        waitpid(pid, &st, 0);
        ++cells;
        if (WIFEXITED(st) && WEXITSTATUS(st) == 10) ++failedCells;
        else if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
        {
            ++bad;
            std::printf("oom cell %ld: %s %d\n", k, WIFEXITED(st) ? "exit status" : "signal", WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
        }
    }
    std::printf("oom sweep: one request of C allocates %ld times; every %ldth allocation failing: %d cells, %d with the failure shown and held to the model, %d not clean\n",
                total, stride, cells, failedCells, bad);
    return bad;
}
#endif


// ---- Follow PC, the default target, under random sequences ----------------------------------------------------------------------
//
// The area 1 rig types its entries; a user mostly follows the PC. This rig has registers: the PC of each CPU, whether it has
// them, and a view that moves with the emulator (a live source's frame and serial move whenever its registers or memory do).
// The true state is not read from the panel's policy but from history: which registers the snapshot the panel holds was taken
// with (HeldKey names its frame; the rig kept the registers of every frame it showed), which function that PC is in (the
// world's own layout), and the key of the result on screen (the memory it was made from). The sequences also hold the panel to
// the guide's policy for a source that does not run: once it has caught up it is not stale, unless nothing can be asked.
struct FRig
{
    Gate gate;   // before the runner: it must outlive the worker that waits on it
    ContentBackend backend;
    DecompilerRunner runner;
    DecompilerPanel panel;
    FunctionBounds bounds;
    std::map<uint32_t, std::string> names;
    std::unordered_map<uint32_t, std::string> comments;
    ImGuiHarness h;

    DecompViewKind kind = DecompViewKind::LivePaused;
    uint64_t source = 1, serial = 1, frame = 1;
    se_sh2_regs regs[2] = {};
    bool haveRegs[2] = { true, true };
    bool visible = true;
    bool holding = false;
    int cpu = 0;
    std::vector<int> placed;

    struct Regs { se_sh2_regs r[2]; bool have[2]; };
    std::map<std::pair<uint64_t, uint64_t>, Regs> history;   // (serial, frame) -> the registers the view had

    // What the user has asked for. A refusal leaves the target as it was (the box was refused, nothing was chosen): Follow PC is still
    // ticked, and it is the target again once the refusal is gone.
    enum class Tgt { Follow, Entry, Contain };
    Tgt tgt = Tgt::Follow;
    uint32_t tgtAddr = 0;
    bool refused = false;

    // (area 8 fixer, A8-37) The names and the function starts are part of the question, as they are of the panel's: a Stop is of the question
    // under the names then, and a pin's verdict is the finder's under the evidence then.
    struct Q
    {
        uint64_t source = 0, serial = 0, frame = 0;
        uint32_t wanted = 0;
        uint64_t namesGen = 0;
        std::set<uint32_t> functions;
        bool operator==(const Q& o) const
        {
            return source == o.source && serial == o.serial && frame == o.frame && wanted == o.wanted && namesGen == o.namesGen && functions == o.functions;
        }
    };
    uint64_t stopGen = 0, lastGen = 0, installRev = 0;
    bool haveStopped = false;
    Q stoppedQ;
    uint64_t counter = 100;

    // (area 8 fixer, A8-30) Follow PC turned off pins the entry the finder found (or else the one shown): the Entry target is still the
    // finder's guess, not the user's, and keeps its doubt. The doubt is the one the model accepted at the check before the click, for the
    // same question; when no check saw it (the output on screen was another function, or the click's frames captured), any of the finder's.
    bool pinGuess = false;
    bool pinDoubtKnown = false;
    std::string pinDoubt;
    uint32_t pinAnchor = 0;   // the PC that was followed, which the finder's "never reaches" names
    bool pinAnchorKnown = false;   // (area 8 fixer, A8-36) ...when the rig knows it (a pin of the output shown takes the output's)
    bool provUnknown = false;      // ...and a pin of the output shown whose origin the rig did not record: typed or the finder's

    // (area 8 fixer, A8-36 to A8-39) The evidence the finder reads besides the bytes: names and function starts (SE_TRUST_FOLLOW_EVIDENCE, and
    // in the generated mixes 8 and 9). The panel takes the names' generation from the app; the function set is the starts and the named addresses.
    uint64_t namesGen = 0;
    uint64_t shownNamesGen = 0;          // ...as they were when the output on screen was installed: a change marks it stale until it is replaced
    std::set<uint32_t> shownFunctions;
    // The starts (the named addresses move the names' generation): a start at a named address is engine input of its own.
    std::set<uint32_t> Functions() const { return bounds.Entries(); }
    // What the verdict on an entry depends on, besides the snapshot: the target and the evidence. Taken at each install, so that the doubt
    // the model accepts for current output is known to be the one the output was installed with only while this is unchanged.
    struct State
    {
        Tgt tgt = Tgt::Follow;
        uint32_t addr = 0;
        bool pinGuess = false, refused = false;
        uint32_t pinAnchor = 0;
        uint64_t namesGen = 0;
        std::set<uint32_t> functions;
        int cpu = 0;
        bool operator==(const State& o) const
        {
            return tgt == o.tgt && addr == o.addr && pinGuess == o.pinGuess && refused == o.refused && pinAnchor == o.pinAnchor &&
                   namesGen == o.namesGen && functions == o.functions && cpu == o.cpu;
        }
    };
    State StateNow() const { return State{ tgt, tgtAddr, pinGuess, refused, pinAnchor, namesGen, Functions(), cpu }; }
    State installState;
    // Where the output on screen came from, recorded at the check after it was installed (ResultRevision): what Follow PC turned off and
    // Enter on the box the panel filled ask for it again with. typed: the user's own entry; else the finder's, found for 'anchor'.
    struct Prov
    {
        bool known = false;
        bool typed = false;
        uint32_t entry = 0;
        bool anchorKnown = false;
        uint32_t anchor = 0;
        bool operator==(const Prov& o) const
        {
            return known == o.known && typed == o.typed && entry == o.entry && anchorKnown == o.anchorKnown && (!anchorKnown || anchor == o.anchor);
        }
    };
    Prov shownProv;
    uint64_t shownProvRev = ~0ull;
    uint32_t lastShownEntry = 0;
    // ...and the doubt the output carried while it was current: drawn for it once the question has moved on (A8-39).
    std::string shownDoubt;
    uint64_t shownDoubtRev = ~0ull;
    // The entry box: the panel's text (the output shown's entry, asking it as it was asked), or the target Enter last asked.
    bool boxShowsShown = true;
    Prov boxTarget;
    // The target as a Prov, for the comparison with what Enter would ask.
    Prov TargetProv() const
    {
        Prov p;
        p.known = tgt == Tgt::Entry && !refused;
        p.typed = !pinGuess;
        p.entry = tgtAddr;
        p.anchorKnown = pinGuess && pinAnchorKnown;
        p.anchor = pinAnchor;
        return p;
    }
    // What Enter on the untouched box asks, as the rig knows it; known false when it does not.
    Prov EnterProv() const
    {
        Prov none;
        uint32_t v = 0;
        if (!DecompilerPanel::ParseHexAddress(panel.EntryText(), v)) return none;
        // The box kept the focus, unedited, while the output moved on: its text is stale and Enter asks the output shown (A8-42).
        if (boxShowsShown && panel.EntryBoxActive())
        {
            const DecompileResult* s = panel.Shown();
            if (!s || shownProvRev != panel.ResultRevision() || !shownProv.known) return none;
            return shownProv;
        }
        const bool panelText = boxShowsShown || Hex(boxTarget.entry) != panel.EntryText();
        if (!panelText) return boxTarget.entry == v ? boxTarget : none;
        const DecompileResult* s = panel.Shown();
        if (!s || s->requestedEntry != v || shownProvRev != panel.ResultRevision() || !shownProv.known) return none;
        return shownProv;
    }
    bool followDoubtKnown = false;
    Q followDoubtQ;
    std::string followDoubt;

    FRig() : runner(gConfig, RunnerOptions()), h([this] { Ui(); })
    {
        ApplyTheme(ImGui::GetStyle());
        LoadFonts(ImGui::GetIO());
        runner.SetPhaseObserverForTests([this](DecompilePhase ph) { gate.Observe(ph); });
        placed = { FnC, FnA, FnT, FnU };
        backend.content = BuildImage(placed, 0);
        backend.source = source;
        regs[0].pc = Defs()[FnC].entry + 4;
        regs[1].pc = Defs()[FnA].entry;
        Note();
    }
    ~FRig() { gate.Open(); }

    void Note() { history[{ serial, frame }] = Regs{ { regs[0], regs[1] }, { haveRegs[0], haveRegs[1] } }; }
    // The live emulator moved (its registers or its memory): a new frame, a new serial. A dump with other registers is another source.
    void Moved()
    {
        if (kind == DecompViewKind::Static)
        {
            source = ++counter;
            backend.source = source;
            frame = 1;
        }
        else
            ++frame;
        ++serial;
        Note();
    }

    DecompilerView View() const
    {
        DecompilerView d;
        d.backend = const_cast<ContentBackend*>(&backend);
        d.kind = kind;
        d.sourceLabel = "follow";
        d.sourceId = source;
        d.deriveSerial = serial;
        d.frameNumber = frame;
        for (int i = 0; i < 2; ++i)
        {
            d.regs[i] = regs[i];
            d.haveRegs[i] = haveRegs[i];
        }
        return d;
    }
    void Ui()
    {
        const DecompilerView v = View();
        DecompilerInputs in;
        in.names = &names;
        in.namesGeneration = namesGen;
        in.bounds = &bounds;
        in.comments = &comments;
        panel.Update(v, runner, in, visible);
        DecompilerPanel::Request r;
        if (visible)
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(gRigWinW, 700), ImGuiCond_Always);
            panel.Draw(nullptr, v, runner, in, r);
        }
    }
    void Frame(ImVec2 mouse = ImVec2(1270, 710), bool down = false)
    {
        h.Frame(mouse, down);
        const DecompileResult* s = panel.Shown();
        if (s && s->generation != lastGen)
        {
            lastGen = s->generation;
            haveStopped = false;   // an answer was installed: the question is answered
        }
        // Installed under the names and starts of this frame (the panel installs only answers to the question now, cache hits included).
        if (s && panel.ResultRevision() != installRev)
        {
            installRev = panel.ResultRevision();
            shownNamesGen = namesGen;
            shownFunctions = Functions();
            installState = StateNow();
        }
    }
    void Idle(int n = 1)
    {
        for (int i = 0; i < n; ++i) Frame();
    }
    void Key(ImGuiKey key)
    {
        ImGui::GetIO().AddKeyEvent(key, true);
        Frame();
        ImGui::GetIO().AddKeyEvent(key, false);
        Frame();
    }
    bool Click(const char* item)
    {
        ImVec2 a, b;
        if (!visible || !panel.ItemRect(item, a, b)) return false;
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        for (int i = 0; i < 3; ++i) Frame(c, false);
        Frame(c, true);
        Frame(c, false);
        return true;
    }
    bool Settle()
    {
        const auto t0 = std::chrono::steady_clock::now();
        int calm = 0;
        uint64_t subs = panel.Submissions();
        while (calm < 3)
        {
            if (dts::MsSince(t0) > dts::kWaitMs) return false;
            Idle();
            if (panel.Submissions() != subs) calm = 0;
            else if (!runner.Busy() && !panel.Pending()) ++calm;
            else calm = 0;
            subs = panel.Submissions();
            if (calm == 0) std::this_thread::yield();
        }
        return true;
    }
    void PumpHeld()
    {
        Idle(3);
        const auto t0 = std::chrono::steady_clock::now();
        while (runner.Busy() && gate.Held() == 0 && dts::MsSince(t0) < 30000.0) std::this_thread::yield();
        Idle(2);
    }

    // The world's layout: which function a PC is in (0: none). The finder answers the same from the `rts ; nop` before each entry.
    static uint32_t FunctionAt(uint32_t pc)
    {
        for (int f : { FnC, FnA, FnT, FnU })
        {
            const FnDef& d = Defs()[f];
            if (pc >= d.entry && pc < d.entry + 2 * d.words.size()) return d.entry;
        }
        return 0;
    }
    bool HeldRegs(Regs& out) const
    {
        const SnapshotKey* k = panel.HeldKey();
        if (!k) return false;
        auto it = history.find({ k->deriveSerial, k->frameNumber });
        if (it == history.end()) return false;
        out = it->second;
        return true;
    }

    // The question as the user has put it, in the terms of the world.
    struct Want
    {
        bool known = true;   // false: the panel holds no snapshot yet
        uint32_t entry = 0;  // the function asked for (0: none found)
        trust::NoEntry noEntry = trust::NoEntry::None;
        bool anchor = false;
        uint32_t anchorAt = 0;
    };
    // The question: the memory the panel holds (what it asks about, whatever the view has moved on to) and the function wanted.
    Q QNow(uint32_t wanted) const
    {
        Q q;
        q.source = 0;
        if (const SnapshotKey* k = panel.HeldKey())
        {
            q.source = k->sourceId;
            q.serial = k->deriveSerial;
            q.frame = k->frameNumber;
        }
        q.wanted = wanted;
        q.namesGen = namesGen;
        q.functions = Functions();
        return q;
    }
    Want WantNow() const
    {
        Want w;
        Regs hr;
        if (refused)
        {
            w.noEntry = trust::NoEntry::EntryRefused;
            return w;
        }
        switch (tgt)
        {
        case Tgt::Follow:
            if (!HeldRegs(hr)) { w.known = false; break; }
            if (!hr.have[cpu]) w.noEntry = trust::NoEntry::NoRegisters;
            else if (!(w.entry = FunctionAt(hr.r[cpu].pc))) w.noEntry = trust::NoEntry::FollowNotFound;
            else { w.anchor = true; w.anchorAt = hr.r[cpu].pc; }
            break;
        case Tgt::Entry:
            if (tgtAddr == 0xFFFFFFF0u) w.noEntry = trust::NoEntry::EntryNotCode;
            else w.entry = tgtAddr;
            break;
        case Tgt::Contain:
            if (!(w.entry = FunctionAt(tgtAddr))) w.noEntry = trust::NoEntry::ContainingNotFound;
            else { w.anchor = true; w.anchorAt = tgtAddr; }
            break;
        }
        return w;
    }
};

// The model's true state for the rig, then the model against the panel, then the guide's policy.
std::string FollowCheck(FRig& r, bool hover)
{
    // (area 8, round 3) The check draws frames (the read-back, the hover): Auto-refresh is off while it runs, or it would capture and ask in
    // the middle of the check, against a true state taken before (mix 9). It acts in the operations' frames and the settling.
    struct AutoOff
    {
        DecompilerPanel& p;
        bool was;
        explicit AutoOff(DecompilerPanel& panel) : p(panel), was(panel.AutoRefresh()) { p.SetAutoRefresh(false); }
        ~AutoOff() { p.SetAutoRefresh(was); }
    } autoOff(r.panel);
    const DecompileResult* sh = r.panel.Shown();
    const FRig::Want w = r.WantNow();
    if (!w.known) return r.visible ? "FOLLOW: the panel holds no snapshot whose registers the rig kept" : "";
    trust::TrueState t;
    t.visible = r.visible;
    t.haveSource = true;
    t.liveEmulator = r.kind != DecompViewKind::Static;
    t.running = r.kind == DecompViewKind::LiveRunning;
    t.noEntry = w.noEntry;
    bool doubtful = false, pinned = false;
    const bool forQuestion = sh && w.entry && sh->requestedEntry == w.entry;
    r.followDoubtKnown = false;
    if (sh)
    {
        const bool viewMoved = sh->key.sourceId != r.source || sh->key.deriveSerial != r.serial || sh->key.frameNumber != r.frame;
        // (area 8 fixer) A rename or a function start since the output was installed marks it stale until it is replaced, as on the area 1 rig.
        t.inputsDiffer = viewMoved || (w.entry && sh->requestedEntry != w.entry) || r.shownNamesGen != r.namesGen || r.shownFunctions != r.Functions();
        t.frameMoved = t.liveEmulator && sh->key.sourceId == r.source && sh->key.frameNumber != r.frame;
        if (forQuestion && (r.tgt == FRig::Tgt::Follow || r.tgt == FRig::Tgt::Contain))
        {
            doubtful = true;
            t.anchorKnown = w.anchor;
            t.anchor = w.anchorAt;
        }
        // (area 8 fixer, A8-30) An Entry target that came from the finder is doubted as the finder doubted it. Before, the model expected
        // no doubt for every Entry target, and so accepted the pin dropping it. The anchor line is not drawn for it: no address was asked.
        if (forQuestion && r.tgt == FRig::Tgt::Entry && r.pinGuess)
            doubtful = pinned = true;
    }
    // A request still running for a question that has since found no function is not the replacement of anything on screen.
    const bool pending = r.panel.Pending() && w.noEntry == trust::NoEntry::None;
    t.stopping = pending && r.panel.PendingGeneration() == r.stopGen;
    t.outstanding = pending && !t.stopping;
    const FRig::Q now = r.QNow(w.entry);
    t.stopped = r.haveStopped && now == r.stoppedQ;
    t.sessionDisabled = r.runner.Disabled();
    t.disabledReason = r.runner.DisabledReason();

    // (area 8 fixer) Where the output on screen came from, recorded once per install while the question is still the one it answered.
    if (sh && r.panel.ResultRevision() != r.shownProvRev)
    {
        r.shownProvRev = r.panel.ResultRevision();
        FRig::Prov p;
        FRig::Regs hr;
        const SnapshotKey* held = r.panel.HeldKey();
        if (forQuestion && held && *held == sh->key)
        {
            p.known = true;
            p.entry = sh->requestedEntry;
            switch (r.tgt)
            {
            case FRig::Tgt::Follow:
                p.anchorKnown = r.HeldRegs(hr) && hr.have[r.cpu];
                p.anchor = p.anchorKnown ? hr.r[r.cpu].pc : 0;
                p.known = p.anchorKnown;
                break;
            case FRig::Tgt::Contain:
                p.anchorKnown = true;
                p.anchor = r.tgtAddr;
                break;
            case FRig::Tgt::Entry:
                p.typed = !r.pinGuess;
                p.anchorKnown = r.pinGuess && r.pinAnchorKnown;
                p.anchor = r.pinAnchor;
                p.known = p.typed || r.pinAnchorKnown;
                break;
            }
        }
        r.shownProv = p;
    }
    // The panel writes the entry of a new function into the box (the user's draft aside, which the rig never leaves).
    // ...but not over the user's own text while the box holds the focus (A8-42): the sync waits for the focus to go.
    if (sh && sh->requestedEntry != r.lastShownEntry && !(r.panel.EntryBoxActive() && !r.boxShowsShown))
    {
        r.lastShownEntry = sh->requestedEntry;
        r.boxShowsShown = true;
    }

    // (area 8 fixer, A8-36, A8-37) What confirms the entry of current output, held against the label: the user's own entry (typed, or a
    // function start at it), else a symbol, else the finder alone. A pin is the finder's: keeping it confirms nothing. Only while the target
    // and the evidence are those the output was installed under: the label is the verdict at the install, and a target that changes with
    // the question unchanged (an entry typed, then Follow PC finding the same entry) asks nothing, which is A8-32's (CPU-weighted mix 8
    // seeds 420068 and 420664).
    if (sh && forQuestion && !t.inputsDiffer && w.noEntry == trust::NoEntry::None && !(r.tgt == FRig::Tgt::Entry && r.provUnknown) &&
        r.installRev == r.panel.ResultRevision() && r.installState == r.StateNow() && r.panel.HeldKey() && *r.panel.HeldKey() == sh->key)
    {
        uint32_t canon = sh->requestedEntry;
        CanonicalAddress(sh->requestedEntry, canon);
        t.evidenceKnown = true;
        t.userEntry = (r.tgt == FRig::Tgt::Entry && !r.pinGuess) || r.bounds.Entries().count(canon) != 0;
        t.symbolEntry = r.names.count(canon) != 0;
    }

    auto frame = [&](ImVec2 m) { r.Frame(m, false); };
    std::string err;
    std::vector<std::string> variants;
    const uint32_t shownEntry = sh ? sh->requestedEntry : 0;
    auto finderVariants = [&](uint32_t entry, uint32_t anchorAt, bool anyPc) {
        std::vector<std::string> v = { trust::RtsGuess(), "", "the code from " + trust::Hex8(entry) + " (after the rts at " + trust::Hex8(entry - 4) +
                                                                  ") never reaches " + trust::Hex8(anchorAt) };
        if (anyPc)
            for (uint32_t pc : { Defs()[FnC].entry + 4, Defs()[FnA].entry, Defs()[FnT].entry + 4, Defs()[FnU].entry + 4, Defs()[FnU].entry + 8 })
                if (pc != anchorAt)
                    v.push_back("the code from " + trust::Hex8(entry) + " (after the rts at " + trust::Hex8(entry - 4) + ") never reaches " + trust::Hex8(pc));
        return v;
    };
    if (doubtful)
    {
        // The finder's doubt about an entry it found by the previous `rts` goes away once the entry is a known callee (area 4's
        // order of rungs): the line is the finder's to give, so it may or may not be there. The pins hold it exactly.
        const uint32_t anchorAt = pinned ? r.pinAnchor : w.anchorAt;
        // (area 8 fixer) A pin's verdict is asked of the finder again: exactly what the check before the click accepted while the question,
        // the names and the starts are the same, else the finder's, for the anchor the pin carries (any PC the rig sets when it does not know it).
        variants = finderVariants(w.entry, anchorAt, pinned && !r.pinAnchorKnown);
        if (pinned && r.pinDoubtKnown && now == r.followDoubtQ) variants = { r.pinDoubt };
        if (pinned && r.provUnknown) variants.push_back("");
        if (t.userEntry) variants = { "" };   // the user's own start: no doubt (the guide)
    }
    else if (sh && !forQuestion)
    {
        // (area 8 fixer, A8-39) The question is about something else: the output keeps the doubt it was installed with. Exactly what the
        // last check of it while current saw, else what the finder can say of it (nothing, for an entry the user gave).
        if (r.shownDoubtRev == r.panel.ResultRevision()) variants = { r.shownDoubt };
        else if (r.shownProvRev == r.panel.ResultRevision() && r.shownProv.known && r.shownProv.typed) variants = { "" };
        else variants = finderVariants(shownEntry, shownEntry + 4, true);
    }
    else
        variants = { "" };
    size_t good = variants.size();
    if (variants.size() == 1)
    {
        t.entryDoubt = variants[0];
        err = Verify(r.panel, t, frame, hover);
        if (err.empty()) good = 0;
    }
    else
        for (size_t i = 0; i < variants.size() && good == variants.size(); ++i)
        {
            t.entryDoubt = variants[i];
            err = Verify(r.panel, t, frame, false);
            if (err.empty()) good = i;
        }
    if (good < variants.size() && hover && variants.size() > 1)
    {
        t.entryDoubt = variants[good];
        err = Verify(r.panel, t, frame, true);
    }
    if (err.empty() && r.visible && doubtful && r.tgt == FRig::Tgt::Follow)
    {
        r.followDoubtKnown = true;
        r.followDoubtQ = r.QNow(w.entry);
        r.followDoubt = variants[good];
    }
    // The doubt it was installed with: what this check accepted, the first time it is drawn after the install, while the target, the evidence
    // and the snapshot are still those of the install (a hidden panel draws nothing, so its check accepts any).
    if (err.empty() && r.visible && forQuestion && r.shownDoubtRev != r.panel.ResultRevision() && r.installRev == r.panel.ResultRevision())
    {
        const SnapshotKey* held = r.panel.HeldKey();
        if (held && *held == sh->key && r.installState == r.StateNow())
        {
            r.shownDoubt = t.entryDoubt;
            r.shownDoubtRev = r.panel.ResultRevision();
        }
    }
    // (area 8 fixer, A8-36) A tooltip that sends the reader to Enter in the entry box promises that Enter asks the question on screen again:
    // the same entry, from the same origin (A8-19). Where the rig knows what Enter on the untouched box asks, it must be the target.
    if (err.empty() && r.visible)
    {
        const std::string tip = Norm(r.panel.StaleTooltip());
        const FRig::Prov enter = r.EnterProv();
        if (tip.find("EnterintheentryboxorRefresh") != std::string::npos && enter.known && !r.provUnknown && !(enter == r.TargetProv()))
            err = "TOOLTIP SENDS TO ENTER: the tooltip says to press Enter in the entry box to ask again, and Enter on the box asks " +
                  std::string(enter.typed ? "the typed entry " : "a pin of ") + Hex(enter.entry) + ", not the target\n";
    }
    if (!err.empty())
    {
        char b[260];
        std::snprintf(b, sizeof b, "  rig: source %llu serial %llu frame %llu, wanted entry %08X (target %d%s%s); shown: %s", (unsigned long long)r.source,
                      (unsigned long long)r.serial, (unsigned long long)r.frame, w.entry, static_cast<int>(r.tgt), r.refused ? ", refused" : "",
                      r.pinGuess ? ", pinned" : "", sh ? Rig::KeyText(&sh->key).c_str() : "nothing");
        return err + b + (sh ? " entry " + Hex(sh->requestedEntry) : std::string()) + "\n";
    }
    // A source that does not run is followed at once: a panel that has caught up shows no stale mark, unless nothing can be asked
    // (no function for the question, the session disabled, the user's Stop, the panel hidden).
    if (r.visible && r.kind != DecompViewKind::LiveRunning && !r.runner.Disabled() && t.noEntry == trust::NoEntry::None && !t.stopped &&
        sh && r.panel.ShownStale() && !r.panel.Pending())
        return "NOT REFRESHED: the source does not run, the panel is idle, the question has a function, and the output on screen is stale";
    return "";
}

enum class FOp { PcC, PcA, PcT, PcU, PcUDead, PcBios, RegsOff, RegsOn, Static, Paused, Running, Advance, Edit, Refresh, Stop, Hold,
                 Release, Hide, Show, FollowToggle, TypeC, TypeA, TypeT, BadEntry, NotCode, ContainC, ContainBad, ContainDead, Cpu, AutoToggle,
                 ShowInsn, NameAll, Unname, StartAll, ClearStarts, EnterBox, FocusBox, kCount };

const char* FOpName(FOp o)
{
    static const char* const names[] = { "PC in C", "PC in A", "PC in T", "PC in U", "PC in the dead code of U", "PC in the BIOS (no function)",
        "the CPU has no registers", "the CPU has registers", "a dump (static source)", "the emulator is paused", "the emulator runs",
        "the emulator advances a frame", "edit a byte of C or U", "Refresh", "Stop", "hold the engine", "release the engine", "hide the panel",
        "show the panel", "click Follow PC", "type the entry of C", "type the entry of A", "type the entry of T", "type 'zz'",
        "type FFFFFFF0", "Decompile at C+4", "Decompile at 0x00001FF0", "Decompile at the dead code of U", "the other CPU",
        "toggle Auto-refresh", "Show in Pseudocode at C+4", "name every function", "erase every name", "set a function start at every entry",
        "clear every function start", "Enter on the untouched entry box", "click into the entry box (no edit)" };
    return names[static_cast<int>(o)];
}

std::string FollowExecute(FRig& r, FOp o, Rng& rng)
{
    const FnDef* d = Defs();
    auto setPc = [&](uint32_t pc) {
        r.regs[r.cpu].pc = pc;
        r.haveRegs[r.cpu] = true;
        r.Moved();
        return std::string();
    };
    auto typed = [&](uint32_t entry) {
        if (!r.visible) return std::string("(panel hidden: no entry box)");
        // The target first: an answer the keys' own frames install (a cache hit on Enter) is installed for it.
        r.tgt = FRig::Tgt::Entry;
        r.tgtAddr = entry;
        r.pinGuess = false;   // the user's own entry: full confidence, no doubt
        r.provUnknown = false;
        r.refused = false;
        TypeEntry(r, Hex(entry));
        r.haveStopped = false;
        r.boxShowsShown = false;
        r.boxTarget = r.TargetProv();
        return std::string();
    };
    // (area 8 fixer) The evidence the finder reads: a change of the names or of the function set asks again, and a pin is asked of the finder again.
    auto evidence = [&](const std::function<bool()>& change) {
        if (!change()) return std::string("(no change)");
        return std::string();
    };
    switch (o)
    {
    case FOp::PcC: return setPc(d[FnC].entry + 4);
    case FOp::PcA: return setPc(d[FnA].entry);
    case FOp::PcT: return setPc(d[FnT].entry + 4);
    case FOp::PcU: return setPc(d[FnU].entry + 4);
    case FOp::PcUDead: return setPc(d[FnU].entry + 8);
    case FOp::PcBios: return setPc(0x00001FF0u);
    case FOp::RegsOff: r.haveRegs[r.cpu] = false; r.Moved(); return "";
    case FOp::RegsOn: r.haveRegs[r.cpu] = true; r.Moved(); return "";
    case FOp::Static: r.kind = DecompViewKind::Static; return "";
    case FOp::Paused: r.kind = DecompViewKind::LivePaused; return "";
    case FOp::Running: r.kind = DecompViewKind::LiveRunning; return "";
    case FOp::Advance:
        if (r.kind == DecompViewKind::Static) return "(a dump does not advance)";
        r.Moved();
        return "";
    case FOp::Edit:
    {
        // Not the literal C calls through: another callee is a function the finder learns, and the entries it gives are area 4's.
        // C's, and U's: one of its sites moves the bt to the rts (value 3), which leaves the two nops after the bra dead code.
        const bool inU = rng.Chance(40);
        const FnDef& c = inU ? d[FnU] : d[FnC];
        const Site& s = inU ? c.sites[static_cast<size_t>(rng.Below(1000)) % c.sites.size()]
                            : c.sites[1 + static_cast<size_t>(rng.Below(1000)) % (c.sites.size() - 1)];
        std::vector<uint8_t> b = r.backend.content->bytes;
        b[s.addr - kHw] = s.vals[static_cast<size_t>(rng.Below(1000)) % s.vals.size()];
        r.backend.content = MakeContent(std::move(b));
        ++r.serial;   // an in-place edit: the serial moves, the frame does not
        r.Note();
        return "";
    }
    case FOp::Refresh:
        if (!r.Click("refresh")) return "(no Refresh button: panel hidden)";
        r.haveStopped = false;
        return "";
    case FOp::Stop:
    {
        if (!r.panel.Pending()) return "(nothing outstanding)";
        const FRig::Want w = r.WantNow();
        r.stoppedQ = r.QNow(w.entry);
        const uint64_t pressedOn = r.panel.PendingGeneration();
        if (!r.Click("stop")) return "(no Stop button: panel hidden)";
        r.stopGen = pressedOn;
        // Stopping a request for a question that has found no function stops nothing the user is waiting for.
        r.haveStopped = w.noEntry == trust::NoEntry::None;
        if (const DecompileResult* landed = r.panel.Shown())   // the answer may have been collected before the press
            if (landed->generation == r.stopGen) r.haveStopped = false;
        return "";
    }
    case FOp::Hold:
        if (r.holding) return "(held already)";
        r.gate.Close();
        r.holding = true;
        return "";
    case FOp::Release:
        if (!r.holding) return "(not held)";
        r.gate.Open();
        r.holding = false;
        r.Settle();
        return "";
    case FOp::Hide: r.visible = false; return "";
    case FOp::Show: r.visible = true; return "";
    case FOp::FollowToggle:
    {
        if (!r.visible) return "(panel hidden)";
        if (r.tgt == FRig::Tgt::Follow)
        {
            // Follow PC off pins the function the panel found, or else the one it shows.
            uint32_t pinned = r.panel.WantedEntry();
            if (!pinned)
                if (const DecompileResult* shown = r.panel.Shown()) pinned = shown->requestedEntry;
            if (!pinned) return "(nothing to pin)";
            if (!r.Click("follow")) return "(no Follow PC box)";
            // (area 8, round 4) What the box pinned is what the panel had found when the click landed, not before the click's own frames: on a
            // running emulator the retry of a PC that found no function (on the auto-refresh cadence) can capture and find one in them (seed
            // 40109 of mix 8 with SE_TRUST_NOHOVER=1). Nothing has resolved the new target yet, so the entry the panel wants is still the one
            // it pinned.
            pinned = r.panel.WantedEntry();
            const bool found = pinned != 0;
            if (!pinned)
                if (const DecompileResult* shown = r.panel.Shown()) pinned = shown->requestedEntry;
            // (area 8 fixer, A8-30) The pin is the finder's guess, with the doubt the last check accepted for this very question. The entry
            // pinned from the output shown (the question had no function) keeps the doubt it was installed with, which no check here recorded.
            FRig::Regs hr;
            r.pinGuess = true;
            r.provUnknown = false;
            r.pinDoubtKnown = found && r.followDoubtKnown && r.QNow(pinned) == r.followDoubtQ;
            r.pinDoubt = r.pinDoubtKnown ? r.followDoubt : std::string();
            r.pinAnchorKnown = found && r.HeldRegs(hr) && hr.have[r.cpu];
            r.pinAnchor = r.pinAnchorKnown ? hr.r[r.cpu].pc : 0;
            // (area 8 fixer) Pinned from the output shown, the pin is that output's target, with its origin: the user's entry stays theirs,
            // the finder's keeps the address it was found for.
            if (!found)
            {
                const DecompileResult* shown = r.panel.Shown();
                if (r.shownProvRev == r.panel.ResultRevision() && r.shownProv.known && shown && r.shownProv.entry == shown->requestedEntry)
                {
                    r.pinGuess = !r.shownProv.typed;
                    r.pinAnchorKnown = r.shownProv.anchorKnown;
                    r.pinAnchor = r.shownProv.anchor;
                }
                else
                    r.provUnknown = true;
            }
            r.tgt = FRig::Tgt::Entry;
            r.tgtAddr = pinned;
        }
        else
        {
            if (!r.Click("follow")) return "(no Follow PC box)";
            r.tgt = FRig::Tgt::Follow;
            r.pinGuess = false;
            r.provUnknown = false;
        }
        r.refused = false;
        r.haveStopped = false;
        return "";
    }
    case FOp::TypeC: return typed(d[FnC].entry);
    case FOp::TypeA: return typed(d[FnA].entry);
    case FOp::TypeT: return typed(d[FnT].entry);
    case FOp::BadEntry:
        if (!r.visible) return "(panel hidden: no entry box)";
        TypeEntry(r, "zz");
        r.refused = true;
        r.haveStopped = false;
        r.boxShowsShown = true;   // "zz" is no entry; the panel writes the shown entry back once a target replaces the refusal
        return "";
    case FOp::NotCode:
        if (!r.visible) return "(panel hidden: no entry box)";
        TypeEntry(r, "FFFFFFF0");
        r.tgt = FRig::Tgt::Entry;
        r.tgtAddr = 0xFFFFFFF0u;
        r.pinGuess = false;
        r.provUnknown = false;
        r.refused = false;
        r.haveStopped = false;
        r.boxShowsShown = false;
        r.boxTarget = r.TargetProv();
        return "";
    case FOp::ContainC:
    case FOp::ContainBad:
    case FOp::ContainDead:
        r.tgt = FRig::Tgt::Contain;
        r.tgtAddr = o == FOp::ContainC ? d[FnC].entry + 4 : o == FOp::ContainBad ? 0x00001FF0u : d[FnU].entry + 8;
        r.pinGuess = r.provUnknown = false;
        r.panel.ShowFunctionContaining(r.cpu, r.tgtAddr);
        r.refused = false;
        r.haveStopped = false;
        return "";
    case FOp::Cpu:
        r.cpu ^= 1;
        r.panel.SetCpu(r.cpu);
        return "";
    case FOp::AutoToggle:
        // (area 8, round 3) Auto-refresh in the generated sequences: on a running emulator under Follow PC it captures and asks by itself
        // once the PC has left the function shown (A1-6), over Stops, holds, hides and the other targets. Round 2 held it in one directed pin.
        if (!r.visible) return "(panel hidden: no checkbox)";
        r.panel.SetAutoRefresh(!r.panel.AutoRefresh());
        return r.panel.AutoRefresh() ? "now on" : "now off";
    case FOp::ShowInsn:
        // (area 8, round 3) "Show in Pseudocode" from SH-2 Assembly (A7-5): Decompile at the instruction, then its line selected. A target of
        // the same kind as Decompile; what it adds is the grey note and the selection, neither of which may change an indicator.
        r.tgt = FRig::Tgt::Contain;
        r.tgtAddr = d[FnC].entry + 4;
        r.pinGuess = r.provUnknown = false;
        r.panel.ShowInstruction(r.cpu, r.tgtAddr);
        r.refused = false;
        r.haveStopped = false;
        return "";
    // A name or a start at every entry, not at one: the finder never lets a user start be overridden, and the nearest known entry at or below
    // an address wins, so a start at A alone would put U in A (area 4's rule). At every entry, each PC stays in the function the world has it
    // in, and the evidence for it is the name or the start.
    case FOp::NameAll:
        return evidence([&] {
            if (!r.names.empty()) return false;
            for (int f : { FnC, FnA, FnT, FnU }) r.names[d[f].entry] = std::string("fn_") + static_cast<char>('a' + f);
            ++r.namesGen;
            return true;
        });
    case FOp::Unname:
        // Rename Function with an empty name erases (FunctionNames::Rename); here every name at once.
        return evidence([&] {
            if (r.names.empty()) return false;
            r.names.clear();
            ++r.namesGen;
            return true;
        });
    case FOp::StartAll:
        return evidence([&] {
            if (!r.bounds.Entries().empty()) return false;
            for (int f : { FnC, FnA, FnT, FnU }) r.bounds.SetEntry(d[f].entry);   // another panel's Set Function Start Here: the target is not changed
            return true;
        });
    case FOp::ClearStarts:
        return evidence([&] {
            if (r.bounds.Entries().empty()) return false;
            const std::set<uint32_t> all = r.bounds.Entries();
            for (uint32_t e : all) r.bounds.ClearEntry(e);
            return true;
        });
    case FOp::EnterBox:
    {
        // (area 8 fixer, A8-36) Enter on the entry box as the panel left it: what the tooltips tell the reader to do. It asks again what the text
        // stands for, with its origin; the rig presses it only where it knows what that is.
        if (!r.visible) return "(panel hidden: no entry box)";
        const FRig::Prov enter = r.EnterProv();
        if (!enter.known) return "(what Enter on the box asks is not known to the rig)";
        ImVec2 a, b;
        if (!r.panel.ItemRect("entry", a, b)) return "(no entry box)";
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        for (int i = 0; i < 3; ++i) r.Frame(c, false);
        r.Frame(c, true);
        r.Frame(c, false);
        r.Idle(2);
        r.tgt = FRig::Tgt::Entry;
        r.tgtAddr = enter.entry;
        r.pinGuess = !enter.typed;
        r.pinAnchorKnown = enter.anchorKnown;
        r.pinAnchor = enter.anchor;
        r.pinDoubtKnown = false;
        r.provUnknown = false;
        r.refused = false;
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
        r.Frame(ImVec2(1270, 710), false);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
        r.Frame(ImVec2(1270, 710), false);
        r.haveStopped = false;
        r.boxShowsShown = false;
        r.boxTarget = enter;
        return enter.typed ? "asks the typed entry " + Hex(enter.entry) : "asks the pin of " + Hex(enter.entry);
    }
    case FOp::FocusBox:
    {
        // (area 8 fixer, A8-42) The box keeps the keyboard focus (clicked, not edited) while the output moves on; the panel does not rewrite
        // it then, and Enter on it asks the output shown (EnterProv).
        if (!r.visible) return "(panel hidden: no entry box)";
        ImVec2 a, b;
        if (!r.panel.ItemRect("entry", a, b)) return "(no entry box)";
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        for (int i = 0; i < 3; ++i) r.Frame(c, false);
        r.Frame(c, true);
        r.Frame(c, false);
        r.Idle(2);
        return "the box has the focus";
    }
    case FOp::kCount: break;
    }
    return "";
}

// (area 8, round 3) The Follow PC rig with Auto-refresh toggled and "Show in Pseudocode" among its operations (mix 9).
bool gFollowAuto = false;

// (area 8, round 4) A Follow PC sequence written out: SE_TRUST_FOLLOW_SCRIPT="cpu; regsoff; cpu; hold; edit; stop; release; cpu; cpu", one word
// per operation (kFollowWords, in the order of FOp). Each step is held to the model as a generated one is; SE_TRUST_FOLLOW_DUMP=1 also prints
// the window's text after each step.
const char* const kFollowWords[] = { "pcc", "pca", "pct", "pcu", "pcudead", "pcbios", "regsoff", "regson", "static", "paused", "running",
                                     "advance", "edit", "refresh", "stop", "hold", "release", "hide", "show", "follow", "typec", "typea",
                                     "typet", "badentry", "notcode", "containc", "containbad", "containdead", "cpu", "auto", "showinsn", "nameall",
                                     "unname", "startall", "clearstarts", "enter", "focus" };
static_assert(sizeof kFollowWords / sizeof kFollowWords[0] == static_cast<size_t>(FOp::kCount), "a word per operation");
std::vector<FOp> gFollowScript;
bool gFollowDump = false;
bool gFollowCpu = false;   // SE_TRUST_FOLLOW_CPU (area 8, round 4)
// (area 8 fixer) The evidence operations and Enter on the untouched box in the generated Follow PC sequences: on in mixes 8 and 9 and for
// SE_TRUST_FOLLOW_SEED, off (SE_TRUST_FOLLOW_EVIDENCE=0) for the seeds pinned before it, which keep their sequences.
bool gFollowEvidence = true;
// (area 8 fixer, A8-42) SE_TRUST_FOLLOW_FOCUS=1: the box is clicked into without an edit while the sequence goes on (off by default: the seeds
// pinned before keep their sequences).
bool gFollowFocus = false;

bool ParseFollowScript(const std::string& text, std::vector<FOp>& out, std::string& error)
{
    out.clear();
    std::istringstream all(text);
    std::string stmt;
    while (std::getline(all, stmt, ';'))
    {
        std::istringstream words(stmt);
        std::string w;
        if (!(words >> w)) continue;
        size_t k = 0;
        while (k < static_cast<size_t>(FOp::kCount) && w != kFollowWords[k]) ++k;
        if (k == static_cast<size_t>(FOp::kCount))
        {
            error = "unknown operation '" + w + "'";
            return false;
        }
        out.push_back(static_cast<FOp>(k));
    }
    return !out.empty();
}

void DumpFollowWindow()
{
    std::string line;
    float y = -1.0f;
    for (const drawn::Run& run : drawn::WindowRuns(DecompilerPanel::kTitle))
    {
        if (run.y0 != y && !line.empty())
        {
            std::printf("      | %s\n", line.c_str());
            line.clear();
        }
        y = run.y0;
        line += (line.empty() ? "" : " ") + run.text;
    }
    if (!line.empty()) std::printf("      | %s\n", line.c_str());
}

// One random sequence of the Follow PC rig. False, with the sequence printed, when the panel and the model disagree.
bool RunFollow(uint64_t seed, bool verbose, uint64_t& opsDone)
{
    HealForRun();
    Rng rng(seed * 7919 + 13);
    static const struct { FOp op; int weight; } kW[] = {
        { FOp::PcC, 8 }, { FOp::PcA, 6 }, { FOp::PcT, 5 }, { FOp::PcU, 4 }, { FOp::PcUDead, 3 }, { FOp::PcBios, 5 }, { FOp::RegsOff, 3 },
        { FOp::RegsOn, 3 }, { FOp::Static, 2 }, { FOp::Paused, 5 }, { FOp::Running, 5 }, { FOp::Advance, 6 }, { FOp::Edit, 6 },
        { FOp::Refresh, 6 }, { FOp::Stop, 4 }, { FOp::Hold, 7 }, { FOp::Release, 6 }, { FOp::Hide, 3 }, { FOp::Show, 3 },
        { FOp::FollowToggle, 5 }, { FOp::TypeC, 3 }, { FOp::TypeA, 3 }, { FOp::TypeT, 3 }, { FOp::BadEntry, 2 }, { FOp::NotCode, 2 },
        { FOp::ContainC, 3 }, { FOp::ContainBad, 3 }, { FOp::ContainDead, 3 }, { FOp::Cpu, 3 },
        // (area 8, round 3) only with gFollowAuto, so that the seeds pinned in kFollowPins stay the sequences they were
        { FOp::AutoToggle, 4 }, { FOp::ShowInsn, 2 },
        // (area 8 fixer, A8-36 to A8-39) only with gFollowEvidence: names and starts that come and go under a pin, and Enter on the box the
        // panel filled. The seeds pinned before keep their sequences (they run without it).
        { FOp::NameAll, 2 }, { FOp::Unname, 2 }, { FOp::StartAll, 2 }, { FOp::ClearStarts, 2 }, { FOp::EnterBox, 5 },
        { FOp::FocusBox, 4 },
    };
    const size_t kinds = sizeof kW / sizeof kW[0];
    // (area 8, round 4) SE_TRUST_FOLLOW_CPU=1: the CPU switch, a CPU without registers and Stop four times as often (A8-29's ground). Not in the
    // default run: the pinned seeds keep their sequences.
    auto weightOf = [](size_t k) {
        const FOp o = kW[k].op;
        if ((o == FOp::AutoToggle || o == FOp::ShowInsn) && !gFollowAuto) return 0;
        if (o >= FOp::NameAll && !gFollowEvidence) return 0;
        if (o == FOp::FocusBox && !gFollowFocus) return 0;
        return gFollowCpu && (o == FOp::Cpu || o == FOp::RegsOff || o == FOp::Stop || o == FOp::PcBios) ? 4 * kW[k].weight : kW[k].weight;
    };
    int total = 0;
    for (size_t k = 0; k < kinds; ++k) total += weightOf(k);
    FRig r;
    std::vector<std::string> done;
    auto fail = [&](const std::string& what) {
        std::printf("\nFOLLOW FAILURE, seed %llu, after operation %zu\n  operations so far:\n", static_cast<unsigned long long>(seed), done.size());
        for (const std::string& d : done) std::printf("    %s\n", d.c_str());
        std::printf("  %s\n  panel: pending %d, stale %d, held key %s, kind %d, tgt %d, cpu %d, regs have {%d, %d}, pc {%08X, %08X}\n", what.c_str(),
                    r.panel.Pending(), r.panel.ShownStale(), Rig::KeyText(r.panel.HeldKey()).c_str(), static_cast<int>(r.kind),
                    static_cast<int>(r.tgt), r.cpu, r.haveRegs[0], r.haveRegs[1], r.regs[0].pc, r.regs[1].pc);
        return false;
    };
    r.Idle(3);
    if (!r.Settle()) return fail("HANG at the first look");
    std::string err = FollowCheck(r, gHover);
    if (!err.empty()) return fail("TRUST (first look): " + err);
    const int n = gFollowScript.empty() ? 25 + rng.Below(40) : static_cast<int>(gFollowScript.size());
    for (int i = 0; i < n; ++i)
    {
        FOp op = FOp::PcC;
        if (!gFollowScript.empty())
            op = gFollowScript[static_cast<size_t>(i)];
        else
        {
            int pick = rng.Below(total);
            for (size_t k = 0; k < kinds; ++k)
            {
                if (pick < weightOf(k)) { op = kW[k].op; break; }
                pick -= weightOf(k);
            }
        }
        const std::string note = FollowExecute(r, op, rng);
        done.push_back("[" + std::to_string(i + 1) + "] " + FOpName(op) + (note.empty() ? "" : "  " + note));
        if (verbose) std::printf("%s\n", done.back().c_str());
        ++opsDone;
        if (op == FOp::Release) { /* settled */ }
        else if (r.holding) r.PumpHeld();
        else
        {
            r.Idle(2);
            if (!r.Settle()) return fail("HANG: the panel did not settle");
        }
        // (area 8, round 4) A Stop belongs to the question it stopped: once the question on screen has found no function it is forgotten
        // (A8-10, the guide's grey-messages row), however the question got there. The panel resolves only while it is drawn. Before round 4
        // the rig forgot it only for the operations that choose a target, so a CPU switch onto a CPU with no registers and back kept it.
        if (r.visible && r.WantNow().noEntry != trust::NoEntry::None) r.haveStopped = false;
        ++gT.checks;
        ++(r.holding ? gT.held : gT.idle);
        if (gFollowDump && r.visible)
        {
            r.Frame();
            DumpFollowWindow();
        }
        err = FollowCheck(r, gHover);
        if (!err.empty()) return fail("TRUST (" + std::string(r.holding ? "held" : "idle") + "): " + err);
    }
    if (r.holding)
    {
        r.gate.Open();
        r.holding = false;
        if (!r.Settle()) return fail("HANG at the end");
        err = FollowCheck(r, gHover);
        if (!err.empty()) return fail("TRUST (end): " + err);
    }
    return true;
}

struct Pin
{
    const char* id;
    const char* script;
    const char* world;
};

// Sequences written out, one per indicator the guide documents: each is held to the model at every step like the
// generated ones, and the run fails if a documented indicator was never drawn (the coverage list).
const Pin kPins[] = {
    { "truncated, then whole, then cut at another place", "select T; edit T 0 1; edit T 0 2; wait", "T,A" },
    { "unsupported, then supported", "select U; edit U 0 1; edit U 0 0", "U,A" },
    { "unsupported and stale at once (dimmed to the lesser)", "select U; hold; edit A 0 1; edit U 1 1; wait", "U,A" },
    { "five cautions", "select W; edit W 0 1; edit W 0 0", "W,A" },
    { "stale: a write, a scrub, a rename, a start, the volatile list, a BIOS image", "select A; hold; edit A 0 1; wait; hold; scrub 1; wait; hold; name A 1; wait; hold; bound A 1; wait; hold; volatile 1; wait; hold; bios 1; wait", "C,A" },
    { "stale by a rename of a function already named (only the names generation says so)", "select A; name A 1; wait; hold; name A 2; wait; hold; name A 1; wait", "C,A" },
    { "stale: frame N on a running emulator", "select A; run; advance; advance; refresh; advance; pause", "C,A" },
    { "a request outstanding, then stopped", "select A; hold; edit A 0 1; stop; wait; edit A 0 2; wait", "C,A" },
    { "a request after a Stop is not stopped (its tooltip says a replacement is coming)", "select A; hold; edit A 0 1; stop; wait; hold; edit A 0 2; wait; hold; edit A 0 3; stop; wait", "C,A" },
    { "stopped and the session disabled", "select A; wedge; edit A 0 1; wait", "C,A" },
    { "hidden while the view moves", "select A; hide; edit A 0 1; wait; show; wait", "C,A" },
    { "too large, then an edit", "select A; starve; edit A 0 1; refresh", "C,A" },
    { "unavailable, then fixed and refreshed", "select A; t-breakcfg; refresh; edit A 0 1; t-fixcfg; refresh; edit A 0 2", "C,A" },
    { "failed (out of memory)", "select A; t-inject failed; edit A 0 1; refresh", "C,A" },
    { "no code", "select A; t-inject nocode; refresh", "C,A" },
    { "the engine damaged earlier", "select A; t-inject damaged; refresh", "C,A" },
    { "unimplemented", "select A; t-inject unimpl; edit A 0 1; refresh", "C,A" },
    { "unsupported, truncated, bad data and engine warnings at once", "select A; t-inject combo; edit A 0 1; refresh", "C,A" },
    { "more amber lines than the panel shows", "select A; t-inject many; refresh", "C,A" },
    { "a result of the engine, injected, then replaced by the engine's", "select A; t-inject plain; edit A 0 1; wait", "C,A" },
    { "the session disabled by a worker that failed, then recovered", "select A; t-engfail; edit A 0 1; reconnect; wait", "C,A" },
    { "a refused entry, an entry that is no code, a Containing nobody holds", "select A; t-badentry zz; edit A 0 1; t-badentry F; t-containbad; select A", "C,A" },
    { "a request outstanding when the target finds no function (no replacement is coming)", "select A; hold; edit A 0 1; t-containbad; wait; hold; edit A 0 2; t-badentry zz; wait", "C,A" },
    { "a Stop is forgotten when the question has found no function", "select A; hold; edit A 0 1; stop; wait; t-badentry zz; wait; t-containbad", "C,A" },
    // A8-18: Decompile on the function already asked asks again, as Enter does: the Stop is over and the output is replaced.
    { "Decompile after a Stop on the very question asks it again (A8-18)", "bound A 0; select A; hold; edit A 0 1; stop; wait; t-contain 1", "C,A" },
    { "Decompile twice after a Stop, the second while the first runs", "bound A 0; select A; hold; edit A 0 1; stop; wait; t-contain 1; t-contain 1; edit A 0 2; t-contain 1", "C,A" },
    // A8-25: a rename undone while the panel is hidden leaves the output marked with the names generation moved; a hidden panel asks nothing.
    { "a rename undone while the panel is hidden (A8-25)", "select A; wait; hide; name A 1; name A 0; wait", "C,A" },
};

// Follow PC sequences (mix 8) that found A8-18, kept as pins: seed 5051 (Stop, the PC moves, Stop, Decompile at C+4: not refreshed) and seed 5080
// (the timing variant: Decompile while the engine has not yet acknowledged the cancel, "Stopped." drawn after it).
const uint64_t kFollowPins[] = { 5051, 5080 };
// A8-27: Stop, then a click on the Follow PC box onto the question that was stopped (the box asks again, as Enter and Decompile do): the seeds
// of mix 8 (20149, 20442, 21097) and of mix 9, with Auto-refresh and Show in Pseudocode (20297, 20729, 20793, 21058, 21275).
const uint64_t kFollowPinsClick[] = { 20149, 20442, 21097 };
const uint64_t kFollowPinsClickAuto[] = { 20297, 20729, 20793, 21058, 21275 };
// (area 8 fixer) A8-30: Follow PC turned off drops the finder's doubt about the entry it pins: mix 8 seed 12 and mix 9 seed 1 (the first seeds
// of each that fail once the model expects the doubt for a pinned entry and the panel drops it).
const uint64_t kFollowPinsPinned[] = { 12 };
const uint64_t kFollowPinsPinnedAuto[] = { 1 };
// A8-29: the CPU switch under Follow PC onto a question whose Stop is over, with the CPU switch, a CPU without registers, the PC in the BIOS
// and Stop four times as often (SE_TRUST_FOLLOW_CPU) and hover off, as round 4 found them: mix 8 seed 50534 (way (b)), mix 9 seed 50814 ((c)).
// Seed 95174 of the same mix 8 is the rig's own: Follow PC off with the PC in the BIOS pins the output shown, whose doubt names the PC it was
// installed for (the dead code of U), which the rig did not know (found by this round's fresh run, after the fix).
const uint64_t kFollowPinsCpu[] = { 50534, 95174 };
const uint64_t kFollowPinsCpuAuto[] = { 50814 };
// ...and written out (round 4's tools/a8_round4/a8_29.sh and a8_30.sh): each ended NOT REFRESHED, or (A8-30) with the doubt gone.
const char* const kFollowScriptPins[] = {
    "cpu; regsoff; cpu; hold; edit; stop; release; cpu; cpu",              // A8-29 (a): a CPU without registers
    "cpu; pcbios; cpu; hold; edit; stop; release; cpu; cpu",               // A8-29 (a): the PC in the BIOS
    "hold; containc; pcc; cpu; regsoff; stop; follow; cpu",                // A8-29 (b): the click's request used up where nothing is found
    "pct; cpu; regsoff; hold; typet; cpu; cpu; refresh; follow; stop; cpu", // A8-29 (c): Stop on a request for no question on screen
    "refresh; follow; refresh; follow",                                    // A8-30: the pin keeps the finder's doubt
    "hold; edit; follow; release; refresh",                                // A8-30: pinned while the answer is held, then answered
    "pcc; focus; pca; enter",                                              // A8-42: Enter in the box that kept the focus while the output moved on
    "pcc; focus; pca; follow; focus; pcc; enter",                          // A8-42 (control: also passes with the fix reverted)
};

// (area 8 fixer, A8-36 to A8-39) Generated sequences with the evidence operations and Enter on the untouched box that failed with the fix
// reverted: the seed, mix 9 (Auto-refresh and Show in Pseudocode), and CPU-weighted without hover.
struct FollowPinE { uint64_t seed; bool autoMix, cpu; };
// Mix 8 seed 2 fails with the pin's verdict frozen (A8-37, A8-33: names given to every entry under a pin of A, still "rts scan"); mix 9 seed 3 with Enter on
// the box always the typed entry (A8-36: Enter on the box Follow PC filled shows "user-set entry").
const std::vector<FollowPinE> kFollowPinsEvidence = { { 2, false, false }, { 3, true, false } };

// (area 8 fixer) A8-41: sequences of SE_TRUST_HIDDEN_RENAME=1, kept in the default run (mix, seed).
struct HiddenRenamePin { int mix; uint64_t seed; };
const HiddenRenamePin kHiddenRenamePins[] = { { 7, 210063 } };

// The stride of the allocation-failure sweep in the default run: every 50th allocation of one request of C (about 32,200 of them), about 644 cells.
const long kOomStride = 50;

struct CoverageNeed
{
    const char* id;
    const char* why;
};
// Every indicator the guide documents must have been drawn and held to the model in the run (I4).
const CoverageNeed kNeed[] = {
    { "badge: stale (plain)", "the plain badge" },
    { "badge: stale: frame N", "the frame badge" },
    { "reason: Replacement", "tooltip: its replacement is coming" },
    { "reason: Stopped", "tooltip: you stopped it" },
    { "reason: Disabled", "tooltip: decompiling is disabled" },
    { "reason: EmulatorMoved", "tooltip: the emulator moved on" },
    { "reason: EntryBox", "tooltip: the entry box names no function" },
    { "reason: NoRegisters", "tooltip: no registers for Follow PC" },
    { "reason: FollowNotFound", "tooltip: Follow PC finds no function" },
    { "reason: ContainingNotFound", "tooltip: no function containing the address" },
    { "reason: NoSource", "tooltip: no source is open" },
    { "tooltip drawn: N more warnings", "the tooltip of N more warnings" },
    { "tooltip drawn: N warnings", "the tooltip of the grey count" },
    { "line: no runner", "the window when the app has no runner" },
    { "line: the halt hint", "the register hint at a halt" },
    { "line: the anchor is not reached", "the address asked about is not reached from the entry" },
    { "reason: NothingAsked", "tooltip: nothing is being decompiled" },
    { "tooltip drawn: Replacement", "the tooltip window, replacement" },
    { "tooltip drawn: Stopped", "the tooltip window, stopped" },
    { "tooltip drawn: Disabled", "the tooltip window, disabled" },
    { "decompiling", "decompiling..." },
    { "dim: stale output", "stale output dimmed" },
    { "dim: unsupported output", "unsupported output greyed" },
    { "output undimmed", "current output at full brightness" },
    { "line: unsupported", "the unsupported line" },
    { "line: truncated", "the truncated line" },
    { "line: bad data", "the bad-data line" },
    { "line: unimplemented", "the unimplemented line" },
    { "line: a warning", "an engine warning line" },
    { "line: too large", "the too-large line" },
    { "line: failed", "the failure line" },
    { "line: no code", "the no-code line" },
    { "line: unavailable", "the unavailable line" },
    { "line: the disabled line", "the red disabled line" },
    { "line: the refused entry", "the refused-entry line" },
    { "line: no function found", "the no-function line" },
    { "grey: N warnings", "the grey count of notes" },
    { "red: session disabled", "the red line" },
    { "amber: N more warnings (6 lines)", "N more warnings" },
};

// The guide holds the phrases the model relies on.
void CheckGuide()
{
#ifdef SE_DECOMPILER_GUIDE
    std::ifstream in(SE_DECOMPILER_GUIDE, std::ios::binary);
    CHECK(in.good());
    std::string guide((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (char& c : guide)
        if (c == '\n' || c == '\r') c = ' ';
    auto has = [&](const std::string& phrase) {
        std::string flat;
        bool space = false;
        for (char c : guide)
        {
            if (c == ' ') { space = true; continue; }
            if (space && !flat.empty()) flat += ' ';
            space = false;
            flat += c;
        }
        if (flat.find(phrase) != std::string::npos) return true;
        std::printf("the guide does not contain: %s\n", phrase.c_str());
        return false;
    };
    for (int k = 1; k <= static_cast<int>(trust::Reason::NothingAsked); ++k) CHECK(has(trust::TooltipPhrase(static_cast<trust::Reason>(k))));
    for (const char* g : kGreyOnlyNotes) CHECK(has(g));
    CHECK(has("The engine's notes"));
    for (const char* phrase : { "decompiling\xe2\x80\xa6", "stopping\xe2\x80\xa6", "[stale]", "[stale: frame N]", "too large to decompile within",
                                "Flows into", "the function is cut off there", "Flow runs into bytes that do not decode",
                                "PC-relative load in a delay slot", "not trustworthy", "more warnings", "Decompiler disabled for this session",
                                "Decompiler unavailable", "Unimplemented instruction - Truncating control flow here", "Decompilation failed.",
                                "no code at", "dimmed to 70% brightness", "dimmed further, to 50%", "at the halt",
                                "Type the function's entry as a hex address", "is not code memory", "No function found containing",
                                "registers to follow", "Nothing decompiled yet.", "No source", "Stopped.", "N warnings",
                                "You stopped its replacement; press Refresh to ask for it again.", "nothing is being decompiled. Press Refresh to replace it.",
                                "/* WARNING: <the amber line> */", "/* [stale] This output is from an earlier state.", "wrap at the panel's edge",
                                // (area 8 fixer) the label's figures (trust::EntryLabels), the pin asked again, Enter on the box, nothing to keep
                                "user-set entry (no figure), symbol 90%, call-site target 80%, callee 70%, rts scan 60%, less 20 for a doubt",
                                "is asked of the finder again whenever what it knows changes", "only text you type is your entry",
                                "Follow PC is off, and there was no function to keep", "kept by you from the finder, and no source names",
                                "The line belongs to the output on screen" })
        CHECK(has(phrase));
#endif
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
    gHover = std::getenv("SE_TRUST_NOHOVER") == nullptr;
    gHiddenRename = std::getenv("SE_TRUST_HIDDEN_RENAME") != nullptr;
    gFollowCpu = std::getenv("SE_TRUST_FOLLOW_CPU") != nullptr;
    gFollowFocus = std::getenv("SE_TRUST_FOLLOW_FOCUS") != nullptr;
    if (const char* ev = std::getenv("SE_TRUST_FOLLOW_EVIDENCE")) gFollowEvidence = std::strcmp(ev, "0") != 0;

    CheckGuide();

    const char* stepsEnv = std::getenv("SE_TRUST_STEPS");
    const char* mixEnv = std::getenv("SE_TRUST_MIX");
    const char* seedEnv = std::getenv("SE_TRUST_SEED");
    const bool long_ = stepsEnv && *stepsEnv;
    const uint64_t perMix = long_ ? std::strtoull(stepsEnv, nullptr, 10) : 120;
    const char* fromEnv = std::getenv("SE_TRUST_FROM");   // the first seed of each mix: a long run in pieces
    const uint64_t fromSeed = fromEnv && *fromEnv ? std::strtoull(fromEnv, nullptr, 10) : 1;
    int failedPrograms = 0;
    std::vector<std::string> failedList;
    if (const char* w = std::getenv("SE_TRUST_WIDTH"))
        if (*w) gRigWinW = static_cast<float>(std::atof(w));
    const bool defaultRun = !mixEnv && !seedEnv && !std::getenv("SE_TRUST_SCRIPT") && !std::getenv("SE_TRUST_FOLLOW_SEED") &&
                            !std::getenv("SE_TRUST_FOLLOW_SCRIPT");
#ifndef _WIN32
    // The real allocation failure first: it runs in a process that has not decompiled anything yet, so every child counts the same allocations
    // (SE_TRUST_OOM_STRIDE=0 leaves it out, =1 tries every allocation of the request).
    {
        const char* st = std::getenv("SE_TRUST_OOM_STRIDE");
        const long stride = st && *st ? std::atol(st) : kOomStride;
        if (defaultRun && stride > 0 && dts::kInjectionUsable)
        {
            const auto t0 = std::chrono::steady_clock::now();
            int cells = 0, failedCells = 0;
            const int bad = RunOomSweep(stride, cells, failedCells);
            std::printf("oom sweep took %.1f s\n", dts::MsSince(t0) / 1000.0);
            CHECK(bad == 0);
            CHECK(failedCells > 0);   // the sweep must have reached the failure
            Cov("a real allocation failure (the engine throws bad_alloc)");
        }
    }
#endif
    try
    {
        Oracle oracle;
        if (std::getenv("SE_TRUST_PROBE5"))
        {
            RunProbe5();
            return 0;
        }
        if (std::getenv("SE_TRUST_PROBE6"))
        {
            RunProbe6();
            return 0;
        }
        if (std::getenv("SE_TRUST_PROBE7"))
        {
            RunProbe7();
            return 0;
        }
        if (std::getenv("SE_TRUST_PROBE8"))
        {
            RunProbe8();
            return 0;
        }
        if (seedEnv && *seedEnv)
        {
            const int mix = mixEnv && *mixEnv ? std::atoi(mixEnv) : 0;
            const uint64_t seed = std::strtoull(seedEnv, nullptr, 10);
            // SE_TRUST_SKIP=3,5 drops those operations and SE_TRUST_LIMIT=n stops after n, to shrink a failure by hand.
            const char* lim = std::getenv("SE_TRUST_LIMIT");
            const bool ok = RunTrust(oracle, MakeProgram(seed, mix), seed, true, ParseList(std::getenv("SE_TRUST_SKIP")), lim && *lim ? std::atoi(lim) : -1);
            std::printf("mix %d seed %llu: %s\n", mix, static_cast<unsigned long long>(seed), ok ? "ok" : "FAILED");
            dts::RemoveTree(scratch);
            return ok ? 0 : 1;
        }
        if (const char* script = std::getenv("SE_TRUST_SCRIPT"))
        {
            const char* world = std::getenv("SE_MODEL_WORLD");
            Program p;
            std::string error;
            if (!ParseScript(script, world ? world : "C,A", 0, p, error))
            {
                std::printf("SE_TRUST_SCRIPT: %s\n", error.c_str());
                return 2;
            }
            const bool ok = RunTrust(oracle, p, 0, true);
            std::printf("script: %s\n", ok ? "ok" : "FAILED");
            dts::RemoveTree(scratch);
            return ok ? 0 : 1;
        }

        // The pins.
        if (!mixEnv && !std::getenv("SE_TRUST_FOLLOW_SEED") && !std::getenv("SE_TRUST_FOLLOW_SCRIPT"))
            for (const Pin& pin : kPins)
            {
                Program p;
                std::string error;
                if (!ParseScript(pin.script, pin.world, 0, p, error))
                {
                    std::printf("pin '%s': %s\n", pin.id, error.c_str());
                    return 2;
                }
                const auto t0 = std::chrono::steady_clock::now();
                const bool ok = RunTrust(oracle, p, 0, false);
                std::printf("pin %-90s %s, %.0f ms\n", pin.id, ok ? "ok" : "FAILED", dts::MsSince(t0));
                if (!ok)
                {
                    ++failedPrograms;
                    failedList.push_back(std::string("pin: ") + pin.id);
                }
            }

        // (area 8 fixer) A8-41: mix 7 seed 210063 with the renames made and undone while hidden (SE_TRUST_HIDDEN_RENAME), the sequence that found
        // area 1's oracle erasing the runner's too-large memo on a panel cache hit.
        if (!mixEnv && !std::getenv("SE_TRUST_FOLLOW_SEED") && !std::getenv("SE_TRUST_FOLLOW_SCRIPT"))
            for (const auto& hp : kHiddenRenamePins)
            {
                const bool was = gHiddenRename;
                gHiddenRename = true;
                const Program p = MakeProgram(hp.seed, hp.mix);
                gHiddenRename = was;
                const bool ok = RunTrust(oracle, p, hp.seed, false);
                std::printf("pin mix %d seed %llu with the renames undone while hidden %s\n", hp.mix, static_cast<unsigned long long>(hp.seed), ok ? "ok" : "FAILED");
                if (!ok)
                {
                    ++failedPrograms;
                    failedList.push_back("pin: hidden rename mix " + std::to_string(hp.mix) + " seed " + std::to_string(hp.seed));
                }
            }

        if (!mixEnv && !std::getenv("SE_TRUST_FOLLOW_SEED") && !std::getenv("SE_TRUST_FOLLOW_SCRIPT"))
        {
            RunMiniPins();
            RunFixPins();
            // (seed, mix 9, CPU-weighted without hover, with the evidence operations)
            struct FollowPin { uint64_t seed; bool autoMix, cpu, evidence; };
            std::vector<FollowPin> followPins;
            for (uint64_t seed : kFollowPins) followPins.push_back({ seed, false, false, false });
            for (uint64_t seed : kFollowPinsClick) followPins.push_back({ seed, false, false, false });
            for (uint64_t seed : kFollowPinsClickAuto) followPins.push_back({ seed, true, false, false });
            for (uint64_t seed : kFollowPinsPinned) followPins.push_back({ seed, false, false, false });
            for (uint64_t seed : kFollowPinsPinnedAuto) followPins.push_back({ seed, true, false, false });
            for (uint64_t seed : kFollowPinsCpu) followPins.push_back({ seed, false, true, false });
            for (uint64_t seed : kFollowPinsCpuAuto) followPins.push_back({ seed, true, true, false });
            for (const FollowPinE& p : kFollowPinsEvidence) followPins.push_back({ p.seed, p.autoMix, p.cpu, true });
            for (size_t k = 0; k < followPins.size(); ++k)
            {
                const uint64_t seed = followPins[k].seed;
                gFollowAuto = followPins[k].autoMix;
                // (area 8 fixer) the A8-29 seeds are sequences of the CPU-weighted mixes, run without hover as they were found
                const bool cpuWeighted = followPins[k].cpu, hoverWas = gHover, cpuWas = gFollowCpu, evidenceWas = gFollowEvidence;
                if (cpuWeighted)
                {
                    gFollowCpu = true;
                    gHover = false;
                }
                // (area 8 fixer) the seeds found before the evidence operations run as they were found
                gFollowEvidence = followPins[k].evidence;
                uint64_t ops = 0;
                const bool ok = RunFollow(seed, false, ops);
                gFollowAuto = false;
                gFollowCpu = cpuWas;
                gHover = hoverWas;
                gFollowEvidence = evidenceWas;
                std::printf("pin follow seed %llu%s (%llu operations) %s\n", static_cast<unsigned long long>(seed), cpuWeighted ? " (CPU-weighted)" : "",
                            static_cast<unsigned long long>(ops), ok ? "ok" : "FAILED");
                if (!ok)
                {
                    ++failedPrograms;
                    failedList.push_back("pin: follow seed " + std::to_string(seed));
                }
            }
            for (const char* script : kFollowScriptPins)
            {
                std::string error;
                CHECK(ParseFollowScript(script, gFollowScript, error));
                uint64_t ops = 0;
                const bool ok = RunFollow(0, false, ops);
                gFollowScript.clear();
                std::printf("pin follow script \"%s\" %s\n", script, ok ? "ok" : "FAILED");
                if (!ok)
                {
                    ++failedPrograms;
                    failedList.push_back(std::string("pin: follow script ") + script);
                }
            }
        }

        if (const char* fsc = std::getenv("SE_TRUST_FOLLOW_SCRIPT"))
        {
            std::string error;
            if (!ParseFollowScript(fsc, gFollowScript, error))
            {
                std::printf("SE_TRUST_FOLLOW_SCRIPT: %s\n", error.empty() ? "no operation" : error.c_str());
                return 2;
            }
            gFollowDump = std::getenv("SE_TRUST_FOLLOW_DUMP") != nullptr;
            uint64_t ops = 0;
            const bool ok = RunFollow(0, true, ops);
            gFollowScript.clear();
            std::printf("follow script: %s\n", ok ? "ok" : "FAILED");
            dts::RemoveTree(scratch);
            return ok ? 0 : 1;
        }
        if (const char* fs = std::getenv("SE_TRUST_FOLLOW_SEED"))
        {
            gFollowAuto = std::getenv("SE_TRUST_FOLLOW_AUTO") != nullptr;   // a mix 9 sequence
            uint64_t ops = 0;
            const uint64_t seed = std::strtoull(fs, nullptr, 10);
            const bool ok = RunFollow(seed, true, ops);
            std::printf("follow seed %llu: %s\n", static_cast<unsigned long long>(seed), ok ? "ok" : "FAILED");
            dts::RemoveTree(scratch);
            return ok ? 0 : 1;
        }

        // The mixes.
        for (int mix = 0; mix < kMixes; ++mix)
        {
            if (mixEnv && *mixEnv && std::atoi(mixEnv) != mix) continue;
            const uint64_t before = gT.steps;
            const auto t0 = std::chrono::steady_clock::now();
            uint64_t seeds = 0;
            for (uint64_t seed = fromSeed; gT.steps - before < perMix; ++seed)
            {
                const bool ok = RunTrust(oracle, MakeProgram(seed, mix), seed, false);
                ++seeds;
                if (!ok)
                {
                    ++failedPrograms;
                    failedList.push_back("mix " + std::to_string(mix) + " seed " + std::to_string(seed));
                }
            }
            std::printf("mix %d (%s): %llu sequences (seeds %llu to %llu), %llu operations, %.1f s\n", mix, kMixNames[mix],
                        static_cast<unsigned long long>(seeds), static_cast<unsigned long long>(fromSeed),
                        static_cast<unsigned long long>(fromSeed + seeds - 1), static_cast<unsigned long long>(gT.steps - before),
                        dts::MsSince(t0) / 1000.0);
        }

        // The panel at the default layout's widths (A8-17): 42% of the work area is 537 px on a 1280 px window, 672 on 1600, 806 on 1920. Every
        // line is whole at each of them, wrapped sentences read back as one, and the badge is on screen (no allowance for a line the window cut).
        if (defaultRun && gRigWinW == 1250.0f)
        {
            static const struct { float width; int mix; } kNarrow[] = { { 537.0f, MixAll }, { 672.0f, MixWarn }, { 806.0f, MixInject }, { 537.0f, MixEntry } };
            const uint64_t each = long_ ? perMix : 80;
            for (const auto& n : kNarrow)
            {
                gRigWinW = n.width;
                const uint64_t before = gT.steps;
                const auto t0 = std::chrono::steady_clock::now();
                uint64_t seeds = 0;
                for (uint64_t seed = fromSeed + 1000; gT.steps - before < each; ++seed)
                {
                    const bool ok = RunTrust(oracle, MakeProgram(seed, n.mix), seed, false);
                    ++seeds;
                    if (!ok)
                    {
                        ++failedPrograms;
                        failedList.push_back("width " + std::to_string(static_cast<int>(n.width)) + " mix " + std::to_string(n.mix) + " seed " + std::to_string(seed));
                    }
                }
                std::printf("width %.0f px, mix %d (%s): %llu sequences, %llu operations, %.1f s\n", n.width, n.mix, kMixNames[n.mix],
                            static_cast<unsigned long long>(seeds), static_cast<unsigned long long>(gT.steps - before), dts::MsSince(t0) / 1000.0);
            }
            gRigWinW = 1250.0f;
        }

        // Follow PC, with registers (mix 8).
        if (!mixEnv || std::atoi(mixEnv) == kMixes)
        {
            const auto t0 = std::chrono::steady_clock::now();
            uint64_t ops = 0, seeds = 0;
            for (uint64_t seed = fromSeed; ops < perMix; ++seed)
            {
                const bool ok = RunFollow(seed, false, ops);
                ++seeds;
                ++gT.programs;
                if (!ok)
                {
                    ++failedPrograms;
                    failedList.push_back("follow seed " + std::to_string(seed));
                }
            }
            gT.steps += ops;
            std::printf("mix %d (Follow PC, registers, the CPU switched, the entry typed, Decompile): %llu sequences (seeds %llu to %llu), %llu operations, %.1f s\n", kMixes,
                        static_cast<unsigned long long>(seeds), static_cast<unsigned long long>(fromSeed),
                        static_cast<unsigned long long>(fromSeed + seeds - 1), static_cast<unsigned long long>(ops), dts::MsSince(t0) / 1000.0);
        }

        // (area 8, round 3) Follow PC with Auto-refresh toggled and "Show in Pseudocode" (mix 9): the policy of A1-6 driven by the generator, not
        // only by the directed pin, and A7-5's navigation, both held to the model at every step.
        if (!mixEnv || std::atoi(mixEnv) == kMixes + 1)
        {
            gFollowAuto = true;
            const auto t0 = std::chrono::steady_clock::now();
            uint64_t ops = 0, seeds = 0;
            for (uint64_t seed = fromSeed; ops < perMix; ++seed)
            {
                const bool ok = RunFollow(seed, false, ops);
                ++seeds;
                ++gT.programs;
                if (!ok)
                {
                    ++failedPrograms;
                    failedList.push_back("follow (auto-refresh) seed " + std::to_string(seed));
                }
            }
            gT.steps += ops;
            gFollowAuto = false;
            std::printf("mix %d (Follow PC, as mix %d, with Auto-refresh toggled and Show in Pseudocode): %llu sequences (seeds %llu to %llu), %llu operations, %.1f s\n",
                        kMixes + 1, kMixes, static_cast<unsigned long long>(seeds), static_cast<unsigned long long>(fromSeed),
                        static_cast<unsigned long long>(fromSeed + seeds - 1), static_cast<unsigned long long>(ops), dts::MsSince(t0) / 1000.0);
        }

        // ...and the same at 537 px.
        if (defaultRun && gRigWinW == 1250.0f)
        {
            gRigWinW = 537.0f;
            const auto t0 = std::chrono::steady_clock::now();
            uint64_t ops = 0, seeds = 0;
            for (uint64_t seed = fromSeed + 1000; ops < (long_ ? perMix : 80); ++seed)
            {
                const bool ok = RunFollow(seed, false, ops);
                ++seeds;
                ++gT.programs;
                if (!ok)
                {
                    ++failedPrograms;
                    failedList.push_back("follow seed " + std::to_string(seed) + " at 537 px");
                }
            }
            gT.steps += ops;
            gRigWinW = 1250.0f;
            std::printf("width 537 px, mix %d (Follow PC): %llu sequences, %llu operations, %.1f s\n", kMixes, static_cast<unsigned long long>(seeds),
                        static_cast<unsigned long long>(ops), dts::MsSince(t0) / 1000.0);
        }
    }
    catch (std::exception& e)
    {
        std::printf("FATAL: uncaught std::exception: %s\n", e.what());
        return 3;
    }

    std::printf("%llu sequences, %d failed; %llu operations; %llu settled checks (%llu with the engine holding a request, %llu idle), %llu drawn "
                "windows read back, %llu badge tooltips hovered, %llu made-up results offered to the panel\n",
                static_cast<unsigned long long>(gT.programs), failedPrograms, static_cast<unsigned long long>(gT.steps),
                static_cast<unsigned long long>(gT.checks), static_cast<unsigned long long>(gT.held), static_cast<unsigned long long>(gT.idle),
                static_cast<unsigned long long>(gT.drawn), static_cast<unsigned long long>(gT.hovers), static_cast<unsigned long long>(gT.injected));
    if (gHiddenRename) std::printf("renames made and undone while the panel was hidden: %llu\n", static_cast<unsigned long long>(gHiddenRenames));
    std::printf("indicators drawn and held to the model:\n");
    for (const auto& c : gCov) std::printf("  %-52s %llu\n", c.first.c_str(), static_cast<unsigned long long>(c.second));
    if (!mixEnv)
    {
        int missing = 0;
        for (const CoverageNeed& n : kNeed)
            if (gCov.find(n.id) == gCov.end())
            {
                std::printf("NOT REACHED: %s (%s)\n", n.id, n.why);
                ++missing;
            }
        CHECK(missing == 0);
    }
    for (const std::string& f : failedList) std::printf("failed: %s\n", f.c_str());
    dts::RemoveTree(scratch);
    CHECK(failedPrograms == 0);
    return dts::Finish("DecompilerTrustTests", start);
}
