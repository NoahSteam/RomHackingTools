// The reference model of area 8 (Docs/Decompiler/REVIEW_PROGRAM.md): what the decompiler panel must tell
// the reader about whether the text on screen can be trusted.
//
// It is a function from the TRUE state -- the inputs on screen, the inputs the shown result was made from,
// whether a request is in flight or was stopped, the result's status and what it says about itself, the
// session-disabled state -- to the indicators the reader must see: the stale badge and its form, the reason
// its tooltip may give, "decompiling...", the dimming of the output, and the amber and red lines in the
// order they are drawn. It includes nothing of the panel: no header of DecompilerPanel, no ImGui. It was
// written from the table "When the output is stale or missing" of DECOMPILER_GUIDE.md, not from
// DecompilerPanel.cpp, and where that table was silent or ambiguous the decision made is written into the
// guide in the same words (the rows A8-5 and later of REVIEW_LEDGER.md), so the guide and this file say the
// same thing: DecompilerTrustTests.cpp reads the guide and fails if a phrase the model relies on is not in it.
//
// The driver (DecompilerTrustTests.cpp) fills a TrueState from its own bookkeeping, never from the panel's
// accessors alone, and compares Expected with what the panel DREW (the glyphs in ImGui's draw lists,
// DrawnText.h) as well as with what its state functions return.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace trust
{

// The result's status. The same set as the decompiler's, restated so this file includes nothing of it.
enum class Status { Ok, NoCode, Cancelled, Failed, Unavailable, TooLarge, Disabled };

// One thing the result on screen says about itself that the panel must turn into an amber line (the
// engine's own warnings and the hardware cautions; the cut and the bad-data places are said by their own
// lines and are counted in 'truncated' and 'badDataAt').
struct Warning
{
    uint32_t address = 0;   // 0: none
    std::string text;
};

// What the result on screen says. Filled from a DecompileResult by the driver.
struct ShownFacts
{
    bool present = false;
    Status status = Status::Ok;
    std::string error;
    bool hasLines = false;
    uint64_t frame = 0;                 // the frame of the memory it was made from

    bool unsupported = false;           // a PC-relative load in a delay slot other code also jumps to
    uint32_t unsupportedAt = 0;
    bool truncated = false;             // flow ran into memory that is not captured
    uint32_t truncatedAt = 0;
    std::string truncatedWhy;
    int truncatedCount = 0;
    std::vector<uint32_t> badDataAt;    // flow ran into bytes that do not decode
    bool unimplemented = false;         // the spec has no semantics for an instruction
    std::vector<Warning> others;        // every other amber line, in order
    size_t engineWarnings = 0;          // all the notes the engine kept (the grey "N warnings")
    std::vector<std::string> notes;     // ...their text (the tooltip of that line)
    std::vector<std::pair<uint32_t, uint32_t>> body;   // the instructions flow reached, [lo, hi), canonical
    std::vector<uint32_t> literalPools; // literal-pool words (data, never reached as code)
    size_t flowWarnings = 0;            // how many flow warnings the result has
    uint32_t entry = 0;                 // canonical
};

// Why no function was found for the question, when none was.
enum class NoEntry { None, NoRegisters, FollowNotFound, ContainingNotFound, EntryRefused, EntryNotCode };

// The true state.
struct TrueState
{
    bool visible = true;                // the window is drawn this frame
    bool haveSource = true;             // a source is on screen
    bool liveEmulator = false;          // ...a live emulator (running or paused)
    bool running = false;               // ...and it is running
    bool inputsDiffer = false;          // the inputs the shown result was made from are not the inputs on screen
    bool frameMoved = false;            // ...and the live emulator on screen is at another frame than the result's
    bool outstanding = false;           // a request for the question on screen is in flight and wanted
    bool stopping = false;              // Stop was pressed and the engine has not acknowledged it
    bool stopped = false;               // the user stopped this question and nothing has asked it since
    bool sessionDisabled = false;
    std::string disabledReason;
    NoEntry noEntry = NoEntry::None;
    uint32_t askedAddress = 0;          // Containing: the address asked about
    std::string entryDoubt;             // the finder's doubt about the entry on screen ("guessed from the previous rts; ..."), if it has one
    bool anchorKnown = false;           // the function was found for an address (the PC, or the one asked about) ...
    uint32_t anchor = 0;                // ... this one, canonical, which the function found should reach
    // The CPU is stopped on a call through a register that the output does not resolve, and this is where it goes this time (empty: not).
    std::string hintMnemonic, hintText;
    uint32_t hintInsn = 0;
};

// Why the badge is up, and so what its tooltip may say. The wording of each is in the guide.
enum class Reason
{
    None,                 // not stale
    Replacement,          // "its replacement is shown when it is ready"
    Stopped,              // "You stopped its replacement"
    Disabled,             // "Decompiling is disabled for this session"
    EmulatorMoved,        // "The emulator has moved on"
    NoRegisters,          // "has no registers for Follow PC to read"
    FollowNotFound,       // "Follow PC finds no function at the PC"
    ContainingNotFound,   // "No function was found containing"
    EntryBox,             // "the entry box names no function"
    NoSource,             // "No source is open"
    NothingAsked,         // "nothing is being decompiled"
};

enum class Ink { Amber, Red };

// One drawn line: its colour and the pieces that must appear in it, in this order.
struct Line
{
    Ink ink = Ink::Amber;
    std::vector<std::string> parts;
    std::string what;     // for a failure message
};

struct Expected
{
    bool stale = false;
    bool badgeFrame = false;         // "[stale: frame N]" rather than "[stale]"
    uint64_t badgeFrameN = 0;
    Reason reason = Reason::None;
    bool decompiling = false;        // the amber "decompiling..."
    bool stoppingNote = false;       // the amber "stopping..."
    float alpha = 1.0f;              // of the output lines
    bool outputShown = false;        // the lines of the result are on screen
    std::vector<Line> lines;         // the amber and red lines in the order they are drawn
    bool greyWarnings = false;       // the grey "N warning(s)" line
    size_t greyCount = 0;
    int uncertain = 0;               // how many amber uncertainty lines the result has (the first four are drawn)
    std::vector<Line> overflow;      // the rest, shown in the tooltip of "N more warnings"
    std::vector<std::string> greyTooltip;   // the tooltip of the grey "N warnings": each note
    std::vector<std::string> greyMust;      // grey messages that are drawn
    std::vector<std::string> greyMustNot;   // ...and the ones that are not
};

// The key phrase of each reason's tooltip. Each is in the guide; a tooltip names its reason and only its reason.
inline const char* TooltipPhrase(Reason r)
{
    switch (r)
    {
    case Reason::Replacement: return "its replacement is shown when it is ready";
    case Reason::Stopped: return "You stopped its replacement";
    case Reason::Disabled: return "Decompiling is disabled for this session";
    case Reason::EmulatorMoved: return "The emulator has moved on";
    case Reason::NoRegisters: return "has no registers for Follow PC to read";
    case Reason::FollowNotFound: return "Follow PC finds no function at the PC";
    case Reason::ContainingNotFound: return "No function was found containing";
    case Reason::EntryBox: return "the entry box names no function";
    case Reason::NoSource: return "No source is open";
    case Reason::NothingAsked: return "nothing is being decompiled";
    case Reason::None: break;
    }
    return "";
}

inline const char* ReasonName(Reason r)
{
    static const char* const names[] = { "None", "Replacement", "Stopped", "Disabled", "EmulatorMoved", "NoRegisters",
                                         "FollowNotFound", "ContainingNotFound", "EntryBox", "NoSource", "NothingAsked" };
    return names[static_cast<int>(r)];
}

inline std::string Hex8(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

// The reason, by the guide's order of precedence: a replacement on its way; the session disabled (nothing
// can be asked, whatever was stopped); the user's Stop; then the causes nothing is being asked for.
inline Reason ReasonFor(const TrueState& t)
{
    if (!t.inputsDiffer && t.noEntry == NoEntry::None && t.haveSource) return Reason::None;
    if (t.outstanding) return Reason::Replacement;
    if (t.sessionDisabled) return Reason::Disabled;
    if (t.stopping || t.stopped) return Reason::Stopped;
    if (!t.haveSource) return Reason::NoSource;
    switch (t.noEntry)
    {
    case NoEntry::NoRegisters: return Reason::NoRegisters;
    case NoEntry::FollowNotFound: return Reason::FollowNotFound;
    case NoEntry::ContainingNotFound: return Reason::ContainingNotFound;
    case NoEntry::EntryRefused:
    case NoEntry::EntryNotCode: return Reason::EntryBox;
    case NoEntry::None: break;
    }
    if (t.running && t.frameMoved) return Reason::EmulatorMoved;
    return Reason::NothingAsked;
}

inline Expected Expect(const TrueState& t, const ShownFacts& s)
{
    Expected e;
    // Stale is a property of the output against the inputs on screen: the inputs differ, or the question
    // on screen (the entry) has no function at all. With no output there is nothing to mark.
    e.stale = s.present && (t.inputsDiffer || t.noEntry != NoEntry::None || !t.haveSource);
    if (e.stale)
    {
        e.reason = ReasonFor(t);
        // The frame form says the live emulator moved on; every other change (a write, a scrub, a BIOS image,
        // a rename, a function start, the volatile list) is the plain badge.
        e.badgeFrame = t.liveEmulator && t.frameMoved;
        e.badgeFrameN = s.frame;
    }
    e.decompiling = t.outstanding && !t.stopping;
    e.stoppingNote = t.stopping;

    // The output: stale output is dimmed, unsupported output is dimmed further; both is the further.
    e.outputShown = s.present && s.status == Status::Ok && s.hasLines;
    e.alpha = 1.0f;
    if (e.stale) e.alpha = 0.7f;
    if (s.present && s.unsupported && s.status == Status::Ok) e.alpha = 0.5f;

    // The grey messages: with no source, with nothing decompiled yet, and after the user's Stop (until something is asked again).
    const bool stoppedNote = t.stopped && !t.stopping && !t.outstanding && !t.sessionDisabled;
    for (const char* m : { "No source", "Nothing decompiled yet", "Stopped." })
    {
        const std::string n = m;
        const bool on = (n == "No source" && !t.haveSource) || (n == "Nothing decompiled yet" && !s.present) || (n == "Stopped." && stoppedNote);
        (on ? e.greyMust : e.greyMustNot).push_back(n);
    }

    // The amber and red lines, in the order they are drawn.
    if (t.sessionDisabled)
        e.lines.push_back({ Ink::Red, { "Decompiler disabled for this session: ", t.disabledReason }, "the disabled line" });
    switch (t.noEntry)
    {
    case NoEntry::EntryRefused:
        e.lines.push_back({ Ink::Red, { "Type the function's entry as a hex address" }, "the refused entry" });
        break;
    case NoEntry::EntryNotCode:
        e.lines.push_back({ Ink::Red, { "is not code memory" }, "the entry that is not code" });
        break;
    case NoEntry::NoRegisters:
        e.lines.push_back({ Ink::Red, { "This source has no", "SH-2 registers to follow." }, "no registers" });
        break;
    case NoEntry::FollowNotFound:
    case NoEntry::ContainingNotFound:
        e.lines.push_back({ Ink::Red, { "No function found containing 0x" }, "no function found" });
        break;
    case NoEntry::None:
        break;
    }
    if (!s.present) return e;

    switch (s.status)
    {
    case Status::Ok:
        break;
    case Status::TooLarge:
        e.lines.push_back({ Ink::Amber, { Hex8(s.entry) + ": ", "too large to decompile within" }, "too large" });
        break;
    case Status::Disabled:
        e.lines.push_back({ Ink::Red, { "Decompiler disabled for this session: ", s.error }, "a disabled result" });
        break;
    case Status::NoCode:
    case Status::Cancelled:
    case Status::Failed:
    case Status::Unavailable:
        e.lines.push_back({ Ink::Red, { s.error.empty() ? std::string("Decompilation failed.") : s.error },
                            s.status == Status::Unavailable ? "unavailable" : s.status == Status::NoCode ? "no code" : "failed" });
        break;
    }

    // Uncertainty is said, never left to look like a complete function (guide: the amber rows). Only an answer
    // that decompiled has any; the first four are drawn, the rest are counted.
    std::vector<Line> unc;
    if (s.status == Status::Ok)
    {
        if (s.unsupported)
            unc.push_back({ Ink::Amber, { "PC-relative load in a delay slot at " + Hex8(s.unsupportedAt), "not trustworthy" }, "unsupported" });
        if (s.truncated)
        {
            Line l{ Ink::Amber, { "Flows into ", s.truncatedWhy.empty() ? std::string("uncaptured memory") : s.truncatedWhy,
                                  " at " + Hex8(s.truncatedAt), "the function is cut off there" }, "truncated" };
            if (s.truncatedCount > 1) l.parts.push_back("(and at " + std::to_string(s.truncatedCount - 1) + " more places)");
            unc.push_back(l);
        }
        if (!s.badDataAt.empty())
        {
            Line l{ Ink::Amber, { "Flow runs into bytes that do not decode at " + Hex8(s.badDataAt.front()) }, "bad data" };
            if (s.badDataAt.size() > 1) l.parts.push_back("(and " + std::to_string(s.badDataAt.size() - 1) + " more places)");
            unc.push_back(l);
        }
        for (const Warning& w : s.others)
        {
            Line l{ Ink::Amber, {}, "a warning" };
            if (w.address) l.parts.push_back(Hex8(w.address) + ": ");
            l.parts.push_back(w.text);
            unc.push_back(l);
        }
        // The address the function was found for is not in the function (guide: "... is not reached from this entry"). A literal
        // word is data, never an instruction flow reaches, so asking about one never says this.
        if (t.anchorKnown && t.noEntry == NoEntry::None)
        {
            auto inList = [&](uint32_t a) {
                for (const auto& r : s.body)
                    if (a >= r.first && a < r.second) return true;
                return false;
            };
            auto pool = [&](uint32_t a) {
                for (uint32_t w : s.literalPools)
                    if (w == a) return true;
                return false;
            };
            const bool literal = pool(t.anchor) || pool(t.anchor - 2);
            if (!literal && !inList(t.anchor))
            {
                if (s.flowWarnings == 0)
                    unc.push_back({ Ink::Amber, { Hex8(t.anchor) + " is not reached from this entry", "the function containing it starts elsewhere" },
                                    "the anchor is not reached" });
                else
                    unc.push_back({ Ink::Amber, { Hex8(t.anchor) + " is not reached from this entry", "flow may stop short of it" },
                                    "the anchor is not reached" });
            }
        }
        // The finder's doubt about the entry on screen comes last (guide: "Entry: ...").
        if (!t.entryDoubt.empty() && t.noEntry == NoEntry::None) unc.push_back({ Ink::Amber, { "Entry: " + t.entryDoubt + "." }, "the entry doubt" });
    }
    e.uncertain = static_cast<int>(unc.size());
    for (size_t i = 0; i < unc.size() && i < 4; ++i) e.lines.push_back(unc[i]);
    if (unc.size() > 4)
    {
        e.lines.push_back({ Ink::Amber, { std::to_string(unc.size() - 4) + " more warnings" }, "the count of the rest" });
        for (size_t i = 4; i < unc.size(); ++i) e.overflow.push_back(unc[i]);
    }
    // The halt hint belongs to output that is current and to a CPU that is stopped: a running emulator has moved on from its registers.
    if (s.present && s.status == Status::Ok && !t.hintText.empty() && !e.stale && !t.running)
        e.lines.push_back({ Ink::Amber, { t.hintMnemonic, Hex8(t.hintInsn), t.hintText }, "the halt hint" });
    e.greyWarnings = s.engineWarnings > 0;
    e.greyCount = s.engineWarnings;
    e.greyTooltip = s.notes;
    return e;
}

}  // namespace trust
