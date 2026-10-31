#include "Theme.h"
// DecompilerPanel — see the header for the refresh policy and the install rule.
#include "DecompilerPanel.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <map>
#include <set>

#include "Debug/MemoryBackend.h"
#include "Debug/CapturedValueInspection.h"
#include "PanelWidgets.h"
#include "Sh2Address.h"

namespace sfe
{

using namespace decomp;

const char* const DecompilerPanel::kTitle = "SH-2 Pseudocode";

namespace
{

ImVec4 TokenColour(TokenKind kind)
{
    switch (kind)
    {
    case TokenKind::Keyword:  return ui::Color(ImVec4(0.42f, 0.62f, 0.95f, 1.0f), ImVec4(0.12f,0.3f,0.68f, 1.0f));
    case TokenKind::Type:     return ui::Color(ImVec4(0.36f, 0.80f, 0.75f, 1.0f), ImVec4(0.02f,0.39f,0.39f, 1.0f));
    case TokenKind::FuncName: return ui::Color(ImVec4(0.93f, 0.84f, 0.45f, 1.0f), ImVec4(0.5f,0.34f,0.0f, 1.0f));
    case TokenKind::Variable: return ui::Color(ImVec4(0.75f, 0.85f, 0.98f, 1.0f), ImVec4(0.14f,0.22f,0.33f, 1.0f));
    case TokenKind::Param:    return ui::Color(ImVec4(0.96f, 0.68f, 0.45f, 1.0f), ImVec4(0.58f,0.28f,0.08f, 1.0f));
    case TokenKind::Const:    return ui::Color(ImVec4(0.60f, 0.85f, 0.52f, 1.0f), ImVec4(0.22f,0.42f,0.16f, 1.0f));
    case TokenKind::Comment:  return ui::Color(ImVec4(0.50f, 0.56f, 0.50f, 1.0f), ImVec4(0.31f,0.38f,0.31f, 1.0f));
    case TokenKind::Global:   return ui::Color(ImVec4(0.80f, 0.60f, 0.92f, 1.0f), ImVec4(0.42f,0.22f,0.6f, 1.0f));
    case TokenKind::Label:    return ui::Color(ImVec4(0.85f, 0.70f, 0.85f, 1.0f), ImVec4(0.5f,0.28f,0.5f, 1.0f));
    case TokenKind::Error:    return ui::Color(ImVec4(0.95f, 0.40f, 0.40f, 1.0f), ImVec4(0.65f,0.11f,0.14f, 1.0f));
    case TokenKind::Plain:
    default:                  return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}

}  // namespace

int TokenAtX(const Line& line, float x,
             const std::function<float(const char*, const char*)>& measure)
{
    if (!(x >= 0.0f) || !measure) return -1;
    const char* text = line.text.c_str();
    size_t previous = 0;
    float cursor = 0.0f;
    for (size_t i = 0; i < line.tokens.size(); ++i)
    {
        const Token& t = line.tokens[i];
        const size_t begin = t.begin, end = t.end;
        if (end <= begin || end > line.text.size() || begin < previous) continue;
        // The markup exporter saturates uint16_t text offsets. A saturated end on a longer
        // line does not prove that the printed object ends here.
        if (end == std::numeric_limits<uint16_t>::max() && line.text.size() > end) continue;
        // A clamped span or half a UTF-8 codepoint is not a printed token.
        if ((static_cast<unsigned char>(text[begin]) & 0xc0) == 0x80 ||
            (end < line.text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)) continue;
        cursor += measure(text + previous, text + begin);
        const float width = measure(text + begin, text + end);
        bool whitespace = true;
        for (size_t k = begin; k < end; ++k)
            if (text[k] != ' ' && text[k] != '\t' && text[k] != '\r' && text[k] != '\n') whitespace = false;
        if (!whitespace && width > 0.0f && x >= cursor && x < cursor + width) return static_cast<int>(i);
        cursor += width;
        previous = end;
    }
    return -1;
}

bool DecompilerPanel::ParseHexAddress(const char* text, uint32_t& out)
{
    // Strictly an address: optional 0x, then one to eight hex digits, nothing else. strtoul would
    // accept a sign and wrap it, and on LP64 take a ninth digit, so -F9FFC000 and 106004000 would
    // both silently become 06004000 -- a valid RAM address the user never typed.
    const char* p = text;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    uint32_t v = 0;
    int digits = 0;
    for (; *p; ++p, ++digits)
    {
        const char c = *p;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        if (digits == 8) return false;   // does not fit 32 bits
        v = (v << 4) | static_cast<uint32_t>(d);
    }
    if (digits == 0) return false;
    out = v;
    return true;
}

namespace
{

ImVec4 kAmber() { return ui::Color(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), ImVec4(0.50f,0.36f,0.0f,1.0f)); }
ImVec4 kRed() { return ui::Color(ImVec4(0.95f, 0.42f, 0.42f, 1.0f), ImVec4(0.65f,0.11f,0.14f,1.0f)); }

// Text that wraps at the window's edge instead of running past it: the window has no horizontal scrollbar and ImGui draws no glyph
// beyond its clip rectangle, so a long line of the status area is cut off at the edge with nothing to say so (A8-17).
void WrappedColored(const ImVec4& colour, const std::string& text)
{
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(colour, "%s", text.c_str());
    ImGui::PopTextWrapPos();
}

void WrappedDisabled(const std::string& text)
{
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text.c_str());
    ImGui::PopTextWrapPos();
}

// A control goes beside the one before it when it fits in the window and on the next row when it does not: on a narrow panel the toolbar's
// end -- Stop, which "decompiling..." offers, then Auto-refresh, BIOS and Volatile -- was off the window (A8-24).
void BesideIfFits(float width)
{
    const float startX = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
    const float rightX = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (startX + width <= rightX) ImGui::SameLine();
}
float ButtonWidth(const char* label) { return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f; }
float CheckboxWidth(const char* label) { return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label).x; }

// The status row is words laid out like text: a piece goes beside the one before it when it fits in the window and starts the next
// line when it does not (and wraps, when it is wider than a line by itself). The pieces that matter most -- "decompiling...", the stale
// badge -- come last, so on a narrow panel (537 px at the default layout on a 1280 px window; the row is 760 to 790 px) they used to be
// the ones cut off (A8-17).
class StatusRow
{
public:
    void Piece(const std::string& text, const ImVec4* colour = nullptr, bool grey = false)
    {
        if (!mFirst)
        {
            const float startX = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
            const float rightX = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            if (startX + ImGui::CalcTextSize(text.c_str()).x <= rightX) ImGui::SameLine();
        }
        mFirst = false;
        ImGui::PushTextWrapPos(0.0f);
        if (colour) ImGui::TextColored(*colour, "%s", text.c_str());
        else if (grey) ImGui::TextDisabled("%s", text.c_str());
        else ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
    }

private:
    bool mFirst = true;
};

std::string Fmt(const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
std::string Fmt(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list again;
    va_copy(again, args);
    const int n = std::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    std::string out(n > 0 ? static_cast<size_t>(n) : 0, '\0');
    if (n > 0) std::vsnprintf(&out[0], out.size() + 1, fmt, again);
    va_end(again);
    return out;
}
// The rows of "Show Other Instructions" in view at once (the tooltip lists eight); a line from more scrolls the menu.
const size_t kMenuInstructions = 24;

// FNV-1a, a 64-bit value at a time: the fingerprints of engine and finder inputs.
const uint64_t kFnvBasis = 1469598103934665603ull;
void Mix(uint64_t& h, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
    {
        h ^= (v >> (8 * i)) & 0xFF;
        h *= 1099511628211ull;
    }
}

}  // namespace

DecompilerPanel::ViewId DecompilerPanel::IdOf(const DecompilerView& view)
{
    ViewId id;
    if (view.kind == DecompViewKind::None || !view.backend) return id;
    id.valid = true;
    id.sourceId = view.sourceId;
    id.deriveSerial = view.deriveSerial;
    id.frameNumber = view.frameNumber;
    id.biosHash = view.bios && !view.bios->bytes.empty() ? view.bios->contentHash : 0;
    return id;
}

void DecompilerPanel::SetTarget(const Target& target)
{
    mTarget = target;
    mWantInsn = false;   // (ShowInstruction asks for it again after) any other navigation drops a request for an instruction
    // Another question: a refusal belonged to the one before it, and so did a Stop. The refused text goes from the box with the
    // refusal: when the new target is the function already shown nothing installs, so nothing else would reset it (A8-20).
    if (mEntryRefused) mSyncEntryBox = true;
    mEntryRefused = false;
    ForgetStop();
    mResolved = false;
    mExplicit = true;
}

void DecompilerPanel::ShowFunctionContaining(int cpu, uint32_t addr)
{
    mCpu = cpu & 1;
    Target t;
    t.kind = TargetKind::Containing;
    t.address = addr;
    SetTarget(t);
    // "Decompile" is a request, like Enter in the entry box: it is answered even when the same question was asked before, so a Stop
    // or a failure does not leave the old output on screen with nothing coming (A8-18).
    mAskAgain = true;
    mFocus = true;
}

void DecompilerPanel::ShowInstruction(int cpu, uint32_t addr)
{
    ShowFunctionContaining(cpu, addr);
    mWantInsn = true;
    mWantInsnAddr = addr;
    mInsnNote.clear();
}

// A7-5. Once the function that holds the instruction is the one shown (not stale), its line is selected and scrolled to. Called by DrawLines
// every frame; does nothing without a request.
void DecompilerPanel::ApplyWantedInstruction()
{
    if (!mWantInsn) return;
    // The question found no function: the request cannot be answered, and the entry error says why.
    if (mResolved && !mEntryOk)
    {
        mWantInsn = false;
        return;
    }
    if (!mShownValid || ShownStale() || mShown.status != DecompileStatus::Ok) return;
    mWantInsn = false;
    uint32_t insn = mWantInsnAddr;
    CanonicalAddress(mWantInsnAddr, insn);
    insn &= ~1u;   // a row address is even; an odd PC is the instruction it is in
    char b[160];
    const LineLookup k = LineForInstruction(mShown, insn);
    if (k.lines.empty())
    {
        if (k.kind == LineLookup::Outside)
            std::snprintf(b, sizeof b, "0x%08X is not in the function shown (outside the instructions flow reached from 0x%08X).", insn, mShown.entry);
        else
            std::snprintf(b, sizeof b, "0x%08X is in the function, but no line shows it or a neighbour of it.", insn);
        mInsnNote = b;
        return;
    }
    // A second request for the same instruction on the same output offers the next line that holds it.
    size_t pick = 0;
    if (mLastInsn == insn && mLastInsnRevision == mResultRevision && k.lines.size() > 1 &&
        mSelectedLine == static_cast<int>(k.lines[std::min(mLastInsnIndex, k.lines.size() - 1)]))
        pick = (mLastInsnIndex + 1) % k.lines.size();
    mLastInsn = insn;
    mLastInsnIndex = pick;
    mLastInsnRevision = mResultRevision;
    mSelectedLine = static_cast<int>(k.lines[pick]);
    mScrollToLine = mSelectedLine;
    if (k.kind == LineLookup::Folded)
    {
        std::snprintf(b, sizeof b, "0x%08X was folded into the expression of another instruction; the line shown is 0x%08X's.", insn, k.via);
        mInsnNote = b;
    }
    else if (k.lines.size() > 1)
    {
        std::snprintf(b, sizeof b, "0x%08X is on %zu lines (this is %zu of them; ask again for the next).", insn, k.lines.size(), pick + 1);
        mInsnNote = b;
    }
    else
        mInsnNote.clear();
}

void DecompilerPanel::SetCpu(int cpu)
{
    cpu &= 1;
    if (cpu == mCpu) return;
    mCpu = cpu;
    mInspectionMenu.valid = false;
    // The output on screen stays: it is a function of the shared RAM, not of a CPU. Only Follow PC
    // reads the CPU, so only it asks again.
    if (mTarget.kind == TargetKind::FollowPc)
    {
        mResolved = false;
        mExplicit = true;
    }
}

void DecompilerPanel::ResetSession()
{
    mInspectionMenu.valid = false;
    mPending = Outstanding();
    mAsked = Question();
    mStopped = false;
    // The PC the last auto-refresh was for belongs to the session that ended: kept, it would
    // suppress the new session's one refresh when its PC lands on the same value.
    mAutoPcValid = false;
    mSnap.reset();
    mSnapId = ViewId();
    mResolved = false;
    mCache.clear();
    mCallees.clear();
    ++mCalleesGeneration;
    mPrototypes.clear();
    mNote.clear();
}

uint64_t DecompilerPanel::ShapeOf(const Prototype& proto)
{
    uint64_t h = kFnvBasis;
    auto shape = [&h](const TypeShape& t) {
        Mix(h, (uint64_t(t.kind) << 24) | (uint64_t(t.size) << 16) | (uint64_t(t.pointee) << 8) | t.pointeeSize);
    };
    shape(proto.ret);
    Mix(h, proto.params.size());
    for (const TypeShape& t : proto.params) shape(t);
    // The call effects too: a caller's own signature can depend on them (its return proof).
    Mix(h, proto.effects.preserved);
    Mix(h, proto.effects.storesThrough);
    Mix(h, proto.effects.outOrigins);
    Mix(h, proto.effects.leaks);
    Mix(h, proto.effects.frameValues);
    Mix(h, proto.effects.frameWords);
    Mix(h, proto.effects.storesBelow);
    Mix(h, static_cast<uint32_t>(proto.effects.spDelta));
    return h ? h : 1;   // 0 is "none lent"
}

uint64_t DecompilerPanel::LentShape(uint32_t callee, const SnapshotKey& key, uint64_t functions) const
{
    auto it = mPrototypes.find(callee);
    if (it == mPrototypes.end() || it->second.key != key || it->second.functions != functions) return 0;
    return it->second.shape;
}

uint64_t DecompilerPanel::PrototypeHash(uint32_t entry) const
{
    // Over the lent prototypes, in address order.
    uint64_t h = kFnvBasis;
    for (const auto& p : mPrototypes)
    {
        if (!Lendable(p.second, entry, p.first)) continue;
        Mix(h, p.first);
        Mix(h, p.second.shape);
    }
    return h;
}

void DecompilerPanel::Learn(const DecompileResult& r)
{
    if (r.status == DecompileStatus::Ok && r.prototype.known)
    {
        LearnedPrototype p;
        p.key = r.key;
        p.functions = LendingId();
        p.proto = r.prototype;
        p.shape = ShapeOf(r.prototype);
        // What it was lent for each function it calls. The result answers the question being asked
        // now (Accept and the cache both check that), so that is what is lendable now.
        // By the addresses they run at, as lent: a mirror can change a signature.
        for (uint32_t c : r.calledAt)
            if (c != r.requestedEntry) p.deps.emplace_back(c, LentShape(c, r.key, LendingId()));
        mPrototypes[r.requestedEntry] = std::move(p);
    }
    else
        mPrototypes.erase(r.requestedEntry);

    // Drop every signature recovered from a callee signature that is no longer the one known, to a
    // fixed point: dropping one changes what its own callers were lent. Each pass drops at least
    // one or ends, so mutual recursion settles too (both go, and are learned again when shown).
    for (bool dropped = true; dropped;)
    {
        dropped = false;
        for (auto it = mPrototypes.begin(); it != mPrototypes.end();)
        {
            bool obsolete = false;
            for (const auto& d : it->second.deps)
                if (LentShape(d.first, it->second.key, it->second.functions) != d.second)
                {
                    obsolete = true;
                    break;
                }
            if (obsolete)
            {
                it = mPrototypes.erase(it);
                dropped = true;
            }
            else
                ++it;
        }
    }
}

void DecompilerPanel::UpdateFunctionSet(const DecompilerInputs& inputs)
{
    // The names can be a whole imported symbol file: hashed again only when they change. The user's
    // function starts are a handful, set by hand.
    if (mSymbolsHashGeneration != inputs.namesGeneration)
    {
        mSymbolsHashGeneration = inputs.namesGeneration;
        mSymbolsHash = kFnvBasis;
        if (inputs.names)
            for (const auto& n : *inputs.names) Mix(mSymbolsHash, n.first);
    }
    uint64_t h = mSymbolsHash;
    Mix(h, ~0ull);   // the two sets are not one list
    if (inputs.bounds)
        for (uint32_t e : inputs.bounds->Entries()) Mix(h, e);
    mFunctions = h;
}

uint64_t DecompilerPanel::CalleeHash() const
{
    uint64_t h = kFnvBasis;
    if (!mSnap) return h;
    for (const auto& c : mCallees)
        if (c.second == mSnap->key) Mix(h, c.first);
    return h;
}

bool DecompilerPanel::ShownOutdatedByView() const
{
    return mShownValid && mShownSnapId != mViewId;
}

bool DecompilerPanel::EmulatorMovedOn() const
{
    return mShownValid && mShownSnapId.valid && mViewId.valid &&
           (mViewKind == DecompViewKind::LiveRunning || mViewKind == DecompViewKind::LivePaused) &&
           mShownSnapId.sourceId == mViewId.sourceId && mShownSnapId.frameNumber != mViewId.frameNumber;
}

bool DecompilerPanel::ShownStale() const
{
    if (!mShownValid) return false;
    // Stale is a property of the output against the inputs on screen. A request outstanding for the
    // very question shown (Refresh on current output) does not make it so: "decompiling..." says
    // that, and GetStaleReason tells a coming replacement from one nobody is asking for (A8-3).
    return ShownOutdatedByView() || mShownNamesGeneration != mNamesGeneration ||
           mShownVolatileHash != mVolatileHash ||   // decompiled under another volatile list
           mShownFunctions != mFunctions ||         // or another function set: after a Stop nothing re-asks (review of 0e3cd12)
           (mResolved && (!mEntryOk || mShown.requestedEntry != mEntry));
}

DecompilerPanel::StaleReason DecompilerPanel::GetStaleReason() const
{
    if (!ShownStale()) return StaleReason::None;
    // A request the user has just Stopped is still outstanding until the runner acknowledges it,
    // but no replacement is coming from it. Nor from one for a question that has found no function since (A8-9).
    const bool wanted = RequestWanted();
    if (wanted && !mPending.cancelled) return StaleReason::Pending;
    if (mRunnerDisabled) return StaleReason::Disabled;
    if (mStopped || (wanted && mPending.cancelled)) return StaleReason::Stopped;
    return StaleReason::NeedsRefresh;
}

std::string DecompilerPanel::StaleTooltip() const
{
    switch (GetStaleReason())
    {
    case StaleReason::None:
        return std::string();
    case StaleReason::Pending:
        return "This output is from an earlier state; its replacement is shown when it is ready.";
    case StaleReason::Stopped:
        // Enter in the entry box asks for the function the box holds, as an Entry target: under Follow PC or Decompile that is not
        // the question that was stopped (A8-19).
        if (mTarget.kind == TargetKind::Entry)
            return "This output is from an earlier state. You stopped its replacement; press Enter in the\n"
                   "entry box or Refresh to ask for it again.";
        return "This output is from an earlier state. You stopped its replacement; press Refresh to ask\n"
               "for it again.";
    case StaleReason::Disabled:
        return "This output is from an earlier state. Decompiling is disabled for this session (see\n"
               "the message below), so no replacement will come.";
    case StaleReason::NeedsRefresh:
        break;
    }
    // No source on screen (it was closed, or the emulator went away): Enter and Refresh ask about nothing (A8-8).
    if (mViewKind == DecompViewKind::None)
        return "This output is from an earlier state. No source is open, so there is nothing to replace it with until one is\n"
               "connected or opened.";
    // A target that finds no function says what the user can change (A8-2): the followed PC, the
    // registers, the address asked about -- the entry box only when it is what was refused. Ahead of
    // the running emulator's lag: the failure is why a refresh alone would not help.
    if (mResolved && !mEntryOk)
    {
        char b[320];
        switch (mEntryFailure)
        {
        case EntryFailure::NoRegisters:
            return "This output is from an earlier state. This source has no registers for Follow PC to read\n"
                   "(see the message below). Turn Follow PC off and press Enter on an entry, or press Refresh\n"
                   "once it has registers.";
        case EntryFailure::NotFound:
            if (mTarget.kind == TargetKind::FollowPc)
                return "This output is from an earlier state. Follow PC finds no function at the PC (see the\n"
                       "message below). Step or run the emulator to move the PC and press Refresh, or turn Follow\n"
                       "PC off to keep a function.";
            std::snprintf(b, sizeof b,
                          "This output is from an earlier state. No function was found containing 0x%08X, the\n"
                          "address you asked about (see the message below). Ask about another address, or press\n"
                          "Enter in the entry box to decompile a function by its entry.",
                          mTarget.address);
            return b;
        case EntryFailure::Refused:
        case EntryFailure::NotCode:
        case EntryFailure::None:
            break;
        }
        return "This output is from an earlier state, and the entry box names no function to replace it\n"
               "(see the message below). Correct the entry and press Enter.";
    }
    if (mViewKind == DecompViewKind::LiveRunning && EmulatorMovedOn())
        return "The emulator has moved on since this was decompiled. Press Refresh.";
    if (mTarget.kind != TargetKind::Entry)
        return "This output is from an earlier state and nothing is being decompiled. Press Refresh to\n"
               "replace it.";
    return "This output is from an earlier state and nothing is being decompiled. Press Enter in the\n"
           "entry box or Refresh to replace it.";
}

const char* const DecompilerPanel::kRefusedEntryMessage = "Type the function's entry as a hex address, e.g. 06004000.";

FinderHints DecompilerPanel::FinderHintsFor(const DecompilerInputs& inputs) const
{
    FinderHints hints;
    if (inputs.bounds) hints.userEntries = inputs.bounds->Entries();
    if (inputs.names)
        for (const auto& n : *inputs.names) hints.symbols.insert(n.first);
    if (inputs.callSites) inputs.callSites(mSnap->key, hints.callSiteTargets);
    for (const auto& c : mCallees)
        if (c.second == mSnap->key) hints.callees.insert(c.first);
    return hints;
}

bool DecompilerPanel::PcStillInTooLargeFunction(uint32_t pc, uint32_t canonicalPc, const DecompilerInputs& inputs) const
{
    // A too-large answer has no body to test the PC against, and no extent: the engine was stopped
    // before it knew one. The question to ask instead is the one a refresh would put: which entry
    // Follow PC resolves this PC to. When that is the entry that ran out of budget, asking again
    // is the same question with the same fate (A1-6).
    if (!mSnap) return false;
    const EntryGuess g = FindFunctionContaining(*mSnap, pc, FinderHintsFor(inputs));
    return g.ok && g.entry + (pc - canonicalPc) == mShownAsked;
}

void DecompilerPanel::Resolve(const DecompilerInputs& inputs)
{
    mResolved = true;
    mResolvedFunctions = mFunctions;
    mResolvedCallees = CalleeHash();
    mResolvedCallSites = mCallSitesRevision;
    mEntryOk = false;
    mEntry = 0;
    mEntrySource = EntrySource::None;
    mEntryConfidence = 0;
    mEntryError.clear();
    mEntryDoubt.clear();
    mHaveAnchor = false;
    mEntryFailure = EntryFailure::None;
    char b[200];

    // A refused entry is still the question until the entry is changed (A8-4): whatever else moves
    // -- an edit, a frame, a function start -- re-resolving must not bring the old target back and
    // show it as current while the box still holds the refused text.
    if (mEntryRefused)
    {
        mEntryError = kRefusedEntryMessage;
        mEntryFailure = EntryFailure::Refused;
        ForgetStop();
        return;
    }

    if (mTarget.kind == TargetKind::Entry)
    {
        // A2: classify before folding. A mirror is accepted and shown canonical; an address in
        // a cache array or on-chip space is refused here, with the partition named, and never
        // reaches the engine.
        uint32_t canonical = 0;
        std::string why;
        if (!CanonicalAddress(mTarget.address, canonical, &why))
        {
            std::snprintf(b, sizeof b, "0x%08X is not code memory: %s", mTarget.address, why.c_str());
            mEntryError = b;
            mEntryFailure = EntryFailure::NotCode;
            ForgetStop();
            return;
        }
        mEntryOk = true;
        mEntry = mTarget.address;   // as typed: a mirror is where the code runs, which mova sees
        mEntrySource = mTarget.source;
        mEntryConfidence = 100;
        return;
    }

    uint32_t anchor = mTarget.address;
    if (mTarget.kind == TargetKind::FollowPc)
    {
        // The snapshot's own registers, not the view's: on a scrubbed frame they are that frame's.
        if (!mSnap->haveRegs[mCpu])
        {
            std::snprintf(b, sizeof b, "This source has no %s SH-2 registers to follow.", mCpu ? "slave" : "master");
            mEntryError = b;
            mEntryFailure = EntryFailure::NoRegisters;
            ForgetStop();
            return;
        }
        anchor = mSnap->regs[mCpu].pc;
    }

    const EntryGuess g = FindFunctionContaining(*mSnap, anchor, FinderHintsFor(inputs));
    if (!g.ok)
    {
        std::snprintf(b, sizeof b, "No function found containing 0x%08X: %s", anchor, g.why.c_str());
        mEntryError = b;
        mEntryFailure = EntryFailure::NotFound;
        ForgetStop();
        return;
    }
    // The finder answers in canonical addresses; the entry is asked for in the mirror the anchor
    // runs in (a PC in 0x26xxxxxx executes its function there).
    uint32_t anchorCanonical = anchor;
    CanonicalAddress(anchor, anchorCanonical);
    mEntryOk = true;
    mEntry = g.entry + (anchor - anchorCanonical);
    mEntrySource = g.source;
    mEntryConfidence = g.confidence;
    mEntryDoubt = g.doubt;
    mHaveAnchor = true;
    mAnchor = anchorCanonical;
}

// What the screen says about the output that the text alone would not: the stale badge with its reason, and each amber line (unsupported,
// truncated, bad data, unimplemented, the engine's cautions, an address the flow does not reach, the entry's doubt). They are the marks
// a reader had in front of them when choosing Copy; pasted, the text is read where they are not, so they go first as C comments (A8-21).
// Nothing for output that is current and certain: the copy is then the pseudocode and nothing else.
std::string DecompilerPanel::CopyMarks() const
{
    if (!mShownValid || mShown.status != DecompileStatus::Ok) return std::string();
    auto comment = [](std::string text) {
        for (char& c : text)
            if (c == '\n' || c == '\r') c = ' ';
        for (size_t at; (at = text.find("*/")) != std::string::npos;) text.replace(at, 2, "* /");   // a comment cannot hold its own end
        return "/* " + text + " */\n";
    };
    std::string out;
    if (ShownStale())
        out += comment(std::string(EmulatorMovedOn() ? Fmt("[stale: frame %llu] ", static_cast<unsigned long long>(mShown.key.frameNumber))
                                                       : std::string("[stale] ")) + StaleTooltip());
    bool unimplemented = false;
    for (const std::string& u : Uncertainties())
    {
        out += comment("WARNING: " + u);
        unimplemented |= u.find("nimplemented") != std::string::npos;
    }
    if (mShown.unimplemented && !unimplemented) out += comment("WARNING: the SH-2 spec has no semantics for an instruction here");
    return out;
}

std::vector<std::string> DecompilerPanel::Uncertainties() const
{
    std::vector<std::string> out;
    if (!mShownValid || mShown.status != DecompileStatus::Ok) return out;
    char b[200];
    if (mShown.unsupported)
    {
        std::snprintf(b, sizeof b, "PC-relative load in a delay slot at 0x%08X that other code also jumps to: pseudocode for this "
                                   "function is not trustworthy.", mShown.unsupportedAt);
        out.push_back(b);
    }
    if (mShown.truncated)
    {
        std::snprintf(b, sizeof b, "Flows into %s at 0x%08X; the function is cut off there", 
                      mShown.truncatedWhy.empty() ? "uncaptured memory" : mShown.truncatedWhy.c_str(), mShown.truncatedAt);
        std::string line = b;
        if (mShown.truncatedCount > 1)
        {
            std::snprintf(b, sizeof b, " (and at %d more places)", mShown.truncatedCount - 1);
            line += b;
        }
        out.push_back(line + ".");
    }
    if (!mShown.badDataAt.empty())
    {
        std::snprintf(b, sizeof b, "Flow runs into bytes that do not decode at 0x%08X", mShown.badDataAt.front());
        std::string line = b;
        if (mShown.badDataAt.size() > 1)
        {
            std::snprintf(b, sizeof b, " (and %zu more places)", mShown.badDataAt.size() - 1);
            line += b;
        }
        out.push_back(line + ": embedded data, or the entry is wrong; the function stops there.");
    }
    for (const FlowWarning& w : mShown.flowWarnings)
    {
        if (w.kind == FlowWarning::Cut || (w.kind == FlowWarning::BadData && w.address != 0)) continue;
        if (w.address == 0) out.push_back(w.text);
        else
        {
            std::snprintf(b, sizeof b, "0x%08X: ", w.address);
            out.push_back(b + w.text);
        }
    }
    for (const HardwareNote& n : mShown.hardwareNotes)
    {
        if (!n.caution) continue;   // an explanation, shown on its line
        std::snprintf(b, sizeof b, "0x%08X: ", n.address);
        out.push_back(b + n.text);
    }
    // Only for the answer to the question the anchor belongs to.
    if (mResolved && mEntryOk && mShown.requestedEntry == mEntry)
    {
        // Not reached can mean the entry is wrong, or that flow stopped short of the address (a
        // switch it could not recover, a cut, data) -- then a new start would be the wrong fix. A
        // literal word asked about is never an instruction flow reaches.
        auto pool = [&](uint32_t a) { return std::binary_search(mShown.literalPools.begin(), mShown.literalPools.end(), a); };
        const bool literal = pool(mAnchor) || pool(mAnchor - 2);   // either half of a long
        if (mHaveAnchor && !literal && !mShown.InBody(mAnchor))
        {
            if (mShown.flowWarnings.empty())
                std::snprintf(b, sizeof b, "0x%08X is not reached from this entry: the function containing it starts "
                                           "elsewhere (Set Function Start Here corrects it).", mAnchor);
            else
                std::snprintf(b, sizeof b, "0x%08X is not reached from this entry: flow may stop short of it (see the "
                                           "warnings above), or the function containing it starts elsewhere.", mAnchor);
            out.push_back(b);
        }
        if (!mEntryDoubt.empty()) out.push_back("Entry: " + mEntryDoubt + ".");
    }
    return out;
}

bool DecompilerPanel::SetVolatileMemory(const std::string& text, std::string* error)
{
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    std::string why;
    // The popup edits a fixed buffer: a longer list could not be shown whole, and an edit would
    // cut it mid-address.
    if (text.size() >= sizeof mVolatileEdit)
    {
        if (error) *error = "the list is longer than " + std::to_string(sizeof mVolatileEdit - 1) + " characters";
        return false;
    }
    if (!ParseAddressRanges(text, ranges, why))
    {
        if (error) *error = why;
        return false;
    }
    mVolatileText = text;
    mVolatileUnparsed.clear();
    mVolatile = std::move(ranges);
    // Engine input, so part of the question: changing it asks again, and the cache keeps answers
    // for each set.
    uint64_t h = 1469598103934665603ull;
    for (const auto& r : mVolatile)
        for (uint32_t v : { r.first, r.second })
            for (int b = 0; b < 4; ++b) h = (h ^ ((v >> (8 * b)) & 0xFFu)) * 1099511628211ull;
    mVolatileHash = mVolatile.empty() ? 0 : h;
    return true;
}

const DecompilerPanel::CacheEntry* DecompilerPanel::CacheFind(const Question& q)
{
    for (auto it = mCache.begin(); it != mCache.end(); ++it)
        if (it->question == q)
        {
            mCache.splice(mCache.begin(), mCache, it);
            return &mCache.front();
        }
    return nullptr;
}

void DecompilerPanel::CachePut(const Question& q, const DecompileResult& r, const ViewId& snapId)
{
    for (auto it = mCache.begin(); it != mCache.end();)
        it = it->question == q ? mCache.erase(it) : std::next(it);
    CacheEntry e;
    e.question = q;
    e.result = r;
    e.snapId = snapId;
    mCache.push_front(std::move(e));
    while (mCache.size() > kCacheSize) mCache.pop_back();
}

void DecompilerPanel::Install(DecompileResult& result, const ViewId& snapId, uint64_t namesGeneration)
{
    ++mResultRevision;

    // The same function run through another mirror is another answer: the entry box follows it.
    const bool sameFunction = mShownValid && mShown.requestedEntry == result.requestedEntry;
    mShown = std::move(result);
    mShownValid = true;
    mShownSnapId = snapId;
    mShownAsked = mEntry;   // both callers install the answer for the entry wanted now
    mShownNamesGeneration = namesGeneration;
    mShownVolatileHash = mVolatileHash;   // both callers install only answers asked under it
    mShownFunctions = mFunctions;         // ...and under the function set in force
    char b[64];
    if (mEntrySource == EntrySource::None)
        mShownEntrySource.clear();
    else if (mEntryConfidence >= 100)
        mShownEntrySource = EntrySourceName(mEntrySource);
    else
    {
        std::snprintf(b, sizeof b, "%s, %d%%", EntrySourceName(mEntrySource), mEntryConfidence);
        mShownEntrySource = b;
    }
    for (uint32_t c : mShown.callees)
    {
        auto it = mCallees.find(c);
        if (it != mCallees.end() && it->second == mShown.key) continue;
        mCallees[c] = mShown.key;
        ++mCalleesGeneration;
    }
    Learn(mShown);
    mStopped = false;
    mInsnNote.clear();
    // Follow PC at a stop (A7-5): the line the PC is at is the one to look at.
    if (mTarget.kind == TargetKind::FollowPc && mViewHavePc[mCpu & 1] &&
        (mViewKind == DecompViewKind::LivePaused || mViewKind == DecompViewKind::Static || mViewKind == DecompViewKind::Scrub))
    {
        mWantInsn = true;
        mWantInsnAddr = mViewPc[mCpu & 1];
    }
    mShownWidth = -1.0f;   // measured at the next Draw, which has a font
    if (!sameFunction)
    {
        mSelectedLine = -1;
        mSyncEntryBox = true;
    }
    mNote.clear();
}

bool DecompilerPanel::HeldIsOnScreen() const
{
    // The held snapshot is replaced as soon as the view moves -- but only while the panel is shown,
    // since a hidden panel copies no memory. So the view's identity is checked here as well: a
    // hidden panel must not install an answer for memory that is no longer on screen. A running
    // emulator is the exception by design: its held snapshot lags the view until Refresh, and the
    // stale badge says so. The lag is only the emulator moving on, though -- frames and writes. A
    // different source or BIOS image changes the region set itself, which no badge excuses.
    if (!mSnap) return false;
    if (mViewKind == DecompViewKind::LiveRunning)
        return mSnapId.valid == mViewId.valid && mSnapId.sourceId == mViewId.sourceId &&
               mSnapId.biosHash == mViewId.biosHash;
    return mSnapId == mViewId;
}

bool DecompilerPanel::Accept(DecompileResult& r, uint64_t runnerEpoch)
{
    // The checks of the install rule (header). The runner has already dropped results from
    // an older epoch or an older generation, but it cannot know that this panel has since taken
    // another snapshot or wants another entry -- and a result for memory other than the snapshot
    // held would show code that is not what the badge says it is.
    const bool current = mPending.active && r.slot == kSlot &&
                         r.generation == mPending.generation &&       // the outstanding request
                         mPending.epoch == runnerEpoch &&              // of this session
                         mSnap && r.key == mPending.key && r.key == mSnap->key &&   // for the snapshot held now
                         mResolved && mEntryOk && r.requestedEntry == mPending.entry &&
                         mPending.entry == mEntry &&                   // and the entry wanted now,
                         mPending.namesGeneration == mNamesGeneration && // under the names in force now,
                         mPending.functions == mFunctions &&           // registering the functions known now,
                         mPending.prototypes == PrototypeHash(mEntry) &&   // lending the signatures known now,
                         mPending.volatileMemory == mVolatileHash &&   // with the volatile memory set now,
                         HeldIsOnScreen();                             // while that snapshot is what is shown
    if (!current)
    {
        ++mDiscarded;
        // The answer to the outstanding request came back and is not wanted: nothing else will. And
        // the question it answered is not settled: an answer the panel threw away (it was closed
        // while the view moved) must not leave the question recorded as asked, or the inputs coming
        // back to it ask nothing and the old output stays (A1-4). A question the user Stopped stays
        // asked: the Stop is the user's answer to it.
        if (mPending.active && r.slot == kSlot && r.generation == mPending.generation)
        {
            if (!mPending.cancelled) mAsked = Question();
            mPending.active = false;
        }
        return false;
    }
    mPending.active = false;
    if (r.status == DecompileStatus::Cancelled)
    {
        // The user's Stop. What was shown stays; the same question is not asked again until Refresh.
        mNote = "Stopped.";
        mStopped = true;
        return false;
    }
    if (r.status == DecompileStatus::Ok)
    {
        Question q;
        q.valid = true;
        q.key = mPending.key;
        q.entry = mPending.entry;
        q.namesGeneration = mPending.namesGeneration;
        q.functions = mPending.functions;
        q.prototypes = mPending.prototypes;
        q.volatileMemory = mPending.volatileMemory;
        CachePut(q, r, mPending.snapId);
    }
    Install(r, mPending.snapId, mPending.namesGeneration);
    return true;
}

void DecompilerPanel::Update(const DecompilerView& view, DecompilerRunner& runner, const DecompilerInputs& inputs,
                             bool visible)
{
    ++mTick;
    if (mCancelRequested)
    {
        mCancelRequested = false;
        if (mPending.active)
        {
            runner.Cancel(kSlot);
            mPending.cancelled = true;
        }
    }

    // First bring the panel's own state up to date with what is on screen -- the view's identity,
    // the names, the snapshot it holds and the entry it wants -- and only then look at results.
    // The other order would judge a finished answer against last frame's state: an answer for B,
    // completed while the view moved on to C, would be installed before the panel noticed C.
    mViewId = IdOf(view);
    mViewKind = view.kind;
    mInspectionReady = view.inspectionReady;
    mInspectionReason = view.inspectionReason;
    mInspectionSource = view.sourceLabel ? view.sourceLabel : "capture";
    for (int cpu = 0; cpu < 2; ++cpu)
    {
        mViewHavePc[cpu] = view.haveRegs[cpu];
        mViewPc[cpu] = view.regs[cpu].pc;
    }
    mNamesGeneration = inputs.namesGeneration;
    mCallSitesRevision = inputs.callSitesRevision;
    UpdateFunctionSet(inputs);
    const bool active = visible && mViewId.valid;
    if (active)
    {
        // Which snapshot to hold (A1). Recomputed every frame; capturing copies 2 MiB, so it happens
        // only when the view's identity moved AND the policy says to follow it.
        const bool snapCurrent = mSnap && mSnapId == mViewId;
        // A BIOS image chosen, reloaded or dropped is the user's doing, not an advancing frame: the
        // held snapshot's region set is wrong from then on, whatever kind of source is shown.
        const bool biosChanged = mSnap && mSnapId.biosHash != mViewId.biosHash;
        // So is another source -- back from a recorded frame, or a state restored under a new
        // identity: what is held is not this emulator's memory at all, at any lag.
        const bool sourceChanged = mSnap && mSnapId.sourceId != mViewId.sourceId;
        bool capture = false;
        if (!snapCurrent)
        {
            switch (view.kind)
            {
            case DecompViewKind::Static:
            case DecompViewKind::LivePaused:
            case DecompViewKind::Scrub:
                capture = true;   // moves only on a user action: follow it at once
                break;
            case DecompViewKind::LiveRunning:
            {
                // A running emulator moves every frame: only an explicit action, the first look, a
                // BIOS change, or the opt-in auto-refresh -- debounced, never over a request still
                // running, and only once the followed PC has left the function.
                bool autoDue = false;
                if (mAutoRefresh && mTarget.kind == TargetKind::FollowPc && !mPending.active &&
                    mTick - mCaptureTick >= kAutoRefreshFrames)
                {
                    // Validated canonically, but tested against where the shown function ran: the PC
                    // moving into another mirror of it has left it (its mova values differ).
                    const uint32_t pc = mViewPc[mCpu];
                    uint32_t canonical = 0;
                    const bool known = mViewHavePc[mCpu] && CanonicalAddress(pc, canonical);
                    // The body is canonical; the PC is inside only in the mirror the function ran in.
                    const uint32_t delta = mShownValid ? mShown.requestedEntry - mShown.entry : 0u;
                    if (mShownValid && mShown.status == DecompileStatus::TooLarge)
                    {
                        // No body to be inside of: the PC is inside while Follow PC would resolve it to
                        // the entry that ran out of budget. That takes a finder pass, so it is asked
                        // once per debounce interval, not every frame the PC sits there (A1-6).
                        if (mTick - mTooLargeCheckTick >= kAutoRefreshFrames)
                        {
                            mTooLargeCheckTick = mTick;
                            autoDue = known && !PcStillInTooLargeFunction(pc, canonical, inputs);
                        }
                    }
                    else
                        autoDue = known && (!mShownValid || pc - delta != canonical || !mShown.InBody(canonical));
                    // A PC no answer holds (a hole flow never reached: an unrecovered switch's case,
                    // or an answer that failed) is refreshed for once, not every interval while it
                    // sits there.
                    if (autoDue && mAutoPcValid && pc == mAutoPc) autoDue = false;
                    if (autoDue)
                    {
                        mAutoPc = pc;
                        mAutoPcValid = true;
                    }
                }
                // Following a PC the held snapshot has no registers for -- the first look after
                // connecting can land before the emulator's first register frame -- is no answer at
                // all, not a stale one: capture again once the view has them.
                const bool regsArrived = mTarget.kind == TargetKind::FollowPc && mSnap &&
                                         !mSnap->haveRegs[mCpu] && view.haveRegs[mCpu];
                // Likewise a followed PC the held snapshot found no function for (at boot it sits in a
                // BIOS that is not mapped): retried once the PC has moved, on the auto-refresh cadence,
                // whether or not auto-refresh is on -- there is no shown answer to keep stable.
                bool unanswered = false;
                if (mTarget.kind == TargetKind::FollowPc && mSnap && mResolved && !mEntryOk && !mEntryRefused &&
                    !mPending.active && mSnap->haveRegs[mCpu] && view.haveRegs[mCpu] &&
                    view.regs[mCpu].pc != mSnap->regs[mCpu].pc && mTick - mCaptureTick >= kAutoRefreshFrames)
                    unanswered = true;
                capture = mExplicit || !mSnap || biosChanged || sourceChanged || autoDue || regsArrived || unanswered;
                break;
            }
            case DecompViewKind::None:
                break;
            }
        }
        mExplicit = false;
        if (capture)
        {
            mSnap = MemorySnapshot::Capture(*view.backend, view.regs, view.haveRegs, view.sourceId,
                                            view.deriveSerial, view.frameNumber, view.bios);
            mSnapId = mViewId;
            mCaptureTick = mTick;
            ++mCaptures;
            mResolved = false;
        }
        // A containing function is found from the finder's hints as well as the snapshot: a start set
        // or a symbol added inside it, an entry discovered since, or a call stack rebuilt can move it.
        if (mSnap && mResolved && mTarget.kind != TargetKind::Entry &&
            (mResolvedFunctions != mFunctions || mResolvedCallees != CalleeHash() ||
             mResolvedCallSites != mCallSitesRevision))
            mResolved = false;
        if (mSnap && !mResolved) Resolve(inputs);
    }

    // Results, against the state just brought up to date.
    const uint64_t epoch = runner.Epoch();
    DecompileResult r;
    while (runner.Poll(r))
        if (r.slot == kSlot) Accept(r, epoch);
    // Neither a disabled runner nor an ended session will answer what is outstanding. The question
    // stays recorded: the runner re-enables only in a new session (epoch), and App ends a session
    // with ResetSession, which forgets what was asked. (The model cannot tell a clear here from
    // none: no sequence reaches it without that ResetSession.)
    mRunnerDisabled = runner.Disabled();
    if (mPending.active && (mRunnerDisabled || mPending.epoch != epoch)) mPending.active = false;

    if (!active || !mSnap) return;
    const bool force = mForceSubmit, retry = mRetryTooLarge;
    // An explicit Enter re-asks a question that has been asked, but not over a request still
    // outstanding for it and wanted (a second Enter while it runs would only cancel and restart it);
    // one the user has Stopped is not wanted, so Stop and Enter at once ask it afresh. The rule
    // against asking the same question every frame holds for everything but the user's own request,
    // which is taken once.
    const bool askAgain = mAskAgain && (!mPending.active || mPending.cancelled);
    mForceSubmit = mRetryTooLarge = mAskAgain = false;
    if (!mEntryOk) return;

    Question q;
    q.valid = true;
    q.key = mSnap->key;
    q.entry = mEntry;
    q.namesGeneration = mNamesGeneration;
    q.functions = mFunctions;
    q.prototypes = PrototypeHash(mEntry);
    q.volatileMemory = mVolatileHash;
    if (q == mAsked && !force && !askAgain) return;   // asked already: its answer, or its failure, is shown

    if (!force)
        if (const CacheEntry* hit = CacheFind(q))
        {
            DecompileResult copy = hit->result;
            const ViewId snapId = hit->snapId;
            mAsked = q;
            mNote.clear();
            mPending.active = false;
            Install(copy, snapId, q.namesGeneration);
            ++mCacheHits;
            return;
        }

    // Nothing is sent to a disabled runner, so nothing is recorded as asked: the question is put
    // when the runner takes requests again (A1-5), not lost with the session that disabled it.
    if (runner.Disabled())
    {
        mPending.active = false;
        return;
    }
    mAsked = q;
    mNote.clear();
    mStopped = false;
    DecompileRequest rq;
    rq.entry = mEntry;
    if (inputs.names)
        for (const auto& n : *inputs.names) rq.names.push_back(n);
    if (inputs.bounds)
        rq.knownFunctions.assign(inputs.bounds->Entries().begin(), inputs.bounds->Entries().end());
    for (const auto& p : mPrototypes)
        if (Lendable(p.second, mEntry, p.first)) rq.prototypes.emplace_back(p.first, p.second.proto);
    rq.volatileMemory = mVolatile;
    const uint64_t generation = runner.Submit(kSlot, mSnap, std::move(rq), retry);
    ++mSubmissions;
    if (generation == 0)
    {
        mPending.active = false;
        return;
    }
    mPending.active = true;
    mPending.cancelled = false;   // a Stop was for the request before this one (A8-7)
    mPending.generation = generation;
    mPending.epoch = runner.Epoch();
    mPending.key = mSnap->key;
    mPending.entry = mEntry;
    mPending.snapId = mSnapId;
    mPending.namesGeneration = mNamesGeneration;
    mPending.functions = mFunctions;
    mPending.prototypes = q.prototypes;
    mPending.volatileMemory = q.volatileMemory;
}

bool DecompilerPanel::LineRect(int index, ImVec2& min, ImVec2& max) const
{
    for (const DrawnLine& d : mDrawn)
        if (d.index == index)
        {
            min = d.min;
            max = d.max;
            return true;
        }
    return false;
}

bool DecompilerPanel::ItemRect(const char* name, ImVec2& min, ImVec2& max) const
{
    for (const DrawnItem& d : mItems)
        if (d.name == name)
        {
            min = d.min;
            max = d.max;
            return true;
        }
    return false;
}

void DecompilerPanel::Remember(const char* name)
{
    mItems.push_back(DrawnItem{ name, ImGui::GetItemRectMin(), ImGui::GetItemRectMax() });
}

void DecompilerPanel::Draw(bool* open, const DecompilerView& view, const DecompilerRunner& runner,
                           const DecompilerInputs& inputs, Request& req)
{
    if (mFocus)
    {
        ImGui::SetNextWindowFocus();
        mFocus = false;
    }
    mDrawn.clear();
    mItems.clear();
    mLineTooltip.clear();
    if (!ImGui::Begin(kTitle, open))
    {
        ImGui::End();
        return;
    }
    mHint = HaltHint();
    mHintText.clear();
    if (mHint.valid)
    {
        char b[160];
        uint32_t canonical = 0;
        const char* name = nullptr;
        if (inputs.names && CanonicalAddress(mHint.target, canonical))
        {
            auto it = inputs.names->find(canonical);
            if (it != inputs.names->end()) name = it->second.c_str();
        }
        int len = std::snprintf(b, sizeof b, "r%d = 0x%08X at the halt", mHint.reg, mHint.value);
        if (mHint.target != mHint.value && len > 0 && len < static_cast<int>(sizeof b))
            len += std::snprintf(b + len, sizeof b - len, ": %s goes to 0x%08X", mHint.mnemonic, mHint.target);
        if (name && len > 0 && len < static_cast<int>(sizeof b))
            std::snprintf(b + len, sizeof b - len, " (%s)", name);
        mHintText = b;
    }
    DrawHeader(view, runner, req);
    DrawStatus(view, runner);
    DrawLines(inputs, req);
    ImGui::End();
}

void DecompilerPanel::DrawUnavailable(bool* open, const std::string& why)
{
    if (mFocus)
    {
        ImGui::SetNextWindowFocus();
        mFocus = false;
    }
    mDrawn.clear();
    mItems.clear();
    if (ImGui::Begin(kTitle, open))
    {
        WrappedColored(kRed(), "Decompiler unavailable: " + why);
        Remember("unavailable");
        ImGui::TextDisabled("The rest of Saturn Explorer is unaffected. Restart to try again.");
    }
    ImGui::End();
}

void DecompilerPanel::DrawHeader(const DecompilerView& view, const DecompilerRunner& runner, Request& req)
{
    (void)runner;
    static const char* const kCpus[] = { "Master SH-2", "Slave SH-2" };
    int cpu = mCpu;
    ImGui::SetNextItemWidth(ComboWidth(kCpus, 2));
    if (ImGui::Combo("##cpu", &cpu, kCpus, 2)) SetCpu(cpu);
    Remember("cpu");
    ImGui::SetItemTooltip("Whose PC Follow PC reads. The output is the shared RAM's and stays when you switch.");
    BesideIfFits(ButtonWidth("Open Registers"));
    if (ImGui::SmallButton("Open Registers"))
    {
        req.showRegisters = true;
        req.cpu = mCpu;
    }
    Remember("Open Registers");

    BesideIfFits(CheckboxWidth("Follow PC"));
    bool follow = FollowPc();
    if (ImGui::Checkbox("Follow PC", &follow))
    {
        Target t;
        if (follow)
            t.kind = TargetKind::FollowPc;
        else
        {
            // Pin the function shown, rather than forgetting what was asked.
            t.kind = TargetKind::Entry;
            t.address = mEntryOk ? mEntry : (mShownValid ? mShown.requestedEntry : 0);
            t.source = mEntryOk ? mEntrySource : EntrySource::User;
        }
        SetTarget(t);
    }
    Remember("follow");

    BesideIfFits(ImGui::CalcTextSize("0x00000000").x + ImGui::GetStyle().FramePadding.x * 2.0f);
    // Not while the box is being edited: ImGui owns its text then, and would also lose the draft.
    if (mSyncEntryBox && !mEntryActive && mShownValid)
    {
        std::snprintf(mEntryBuf, sizeof mEntryBuf, "%08X", mShown.requestedEntry);
        mSyncEntryBox = false;
    }
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("0x00000000").x + ImGui::GetStyle().FramePadding.x * 2.0f);
    if (ImGui::InputTextWithHint("##entry", "entry", mEntryBuf, sizeof mEntryBuf,
                                 ImGuiInputTextFlags_EnterReturnsTrue))
    {
        uint32_t v = 0;
        if (ParseHexAddress(mEntryBuf, v))
        {
            Target t;
            t.kind = TargetKind::Entry;
            t.address = v;
            t.source = EntrySource::User;
            SetTarget(t);
            // The box holds what was typed: the reset of a refusal's text (SetTarget) is for the targets that did not come from it.
            mSyncEntryBox = false;
            // Typing an entry and pressing Enter is a request, not a state the panel drifted into: it
            // is answered even when the same question was asked before (after Stop, or after a
            // failure, the old function would otherwise stay on screen with nothing coming).
            mAskAgain = true;
        }
        else
        {
            // The refusal is the question until the entry changes (A8-4); the target is not touched,
            // so a later Follow PC or Containing target is still the one asked for after it.
            mEntryRefused = true;
            mResolved = true;
            mEntryOk = false;
            mEntryFailure = EntryFailure::Refused;
            mEntryError = kRefusedEntryMessage;
            ForgetStop();
        }
    }
    mEntryActive = ImGui::IsItemActive();
    Remember("entry");   // the input box: nothing in the branch above submits an item
    ImGui::SetItemTooltip("Decompile the function starting at this address (hex; any mirror). Enter to go.");

    BesideIfFits(ButtonWidth("Refresh"));
    const bool refresh = ImGui::Button("Refresh");
    Remember("refresh");
    if (refresh)
    {
        mExplicit = true;
        mForceSubmit = true;
        mRetryTooLarge = true;
        mResolved = false;
    }
    ImGui::SetItemTooltip(view.kind == DecompViewKind::LiveRunning
                              ? "Capture the running emulator now and decompile again."
                              : "Decompile again (also retries a function that ran out of time).");
    if (mPending.active)
    {
        BesideIfFits(ButtonWidth("Stop"));
        if (ImGui::Button("Stop")) mCancelRequested = true;
        Remember("stop");
    }

    BesideIfFits(CheckboxWidth("Auto-refresh"));
    if (ImGui::Checkbox("Auto-refresh", &mAutoRefresh)) req.settingsChanged = true;
    ImGui::SetItemTooltip("While the emulator runs and Follow PC is on: refresh at most every %d frames, and\n"
                          "only once the PC has left the function shown. Off: the output is marked stale\n"
                          "until you press Refresh.", kAutoRefreshFrames);

    BesideIfFits(ButtonWidth("BIOS"));
    if (ImGui::Button("BIOS")) ImGui::OpenPopup("bios");
    ImGui::SetItemTooltip(view.bios ? "A BIOS image is mapped at 0x00000000." : "No BIOS image: calls into the BIOS stop there.");
    if (ImGui::BeginPopup("bios"))
    {
        if (ImGui::MenuItem("Choose BIOS Image...")) req.chooseBios = true;
        if (ImGui::MenuItem("Reload BIOS Image", nullptr, false, view.bios != nullptr)) req.reloadBios = true;
        if (ImGui::MenuItem("Stop Using BIOS Image", nullptr, false, view.bios != nullptr)) req.clearBios = true;
        ImGui::EndPopup();
    }

    BesideIfFits(ButtonWidth("Volatile"));
    if (ImGui::Button("Volatile"))
    {
        std::snprintf(mVolatileEdit, sizeof mVolatileEdit, "%s", VolatileMemory().c_str());
        mVolatileError.clear();
        ImGui::OpenPopup("volatile");
    }
    ImGui::SetItemTooltip("Memory another processor changes, such as a mailbox shared with the slave SH-2:\n"
                          "every access there is kept. Hardware registers and sound RAM always are.");
    if (ImGui::BeginPopup("volatile"))
    {
        ImGui::TextUnformatted("Volatile memory: first-last or single addresses, hex");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24);
        const bool enter = ImGui::InputText("##volatile", mVolatileEdit, sizeof mVolatileEdit, ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Apply") || enter)
        {
            if (SetVolatileMemory(mVolatileEdit, &mVolatileError))
            {
                req.settingsChanged = true;
                ImGui::CloseCurrentPopup();
            }
        }
        if (!mVolatileError.empty()) ImGui::TextColored(kAmber(), "%s", mVolatileError.c_str());
        ImGui::EndPopup();
    }
}

void DecompilerPanel::DrawStatus(const DecompilerView& view, const DecompilerRunner& runner)
{
    // One row of pieces that wrap as text does (StatusRow): the badge and "decompiling..." are on screen at any width (A8-17).
    StatusRow row;
    const ImVec4 amber = kAmber();
    if (view.kind == DecompViewKind::None)
        row.Piece("No source", nullptr, true);
    else
        row.Piece(Fmt("%s \xc2\xb7 frame %llu", view.sourceLabel, static_cast<unsigned long long>(view.frameNumber)), nullptr, true);
    if (mShownValid)
    {
        row.Piece(Fmt("\xc2\xb7 %s @ 0x%08X", mShown.name.empty() ? "?" : mShown.name.c_str(), mShown.entry));
        if (mShown.requestedEntry != mShown.entry) row.Piece(Fmt("(run at 0x%08X)", mShown.requestedEntry), nullptr, true);
        if (!mShownEntrySource.empty()) row.Piece(Fmt("(entry from %s)", mShownEntrySource.c_str()), nullptr, true);
    }
    if (RequestWanted())
        // Stop was pressed: the request is outstanding until the runner acknowledges the cancel (up to its deadline, when it
        // does not), but no replacement is coming from it, and "decompiling" would say one is (A8-6).
        row.Piece(mPending.cancelled ? "stopping\xe2\x80\xa6" : "decompiling\xe2\x80\xa6", &amber);
    if (ShownStale())
    {
        if (EmulatorMovedOn())
            row.Piece(Fmt("[stale: frame %llu]", static_cast<unsigned long long>(mShown.key.frameNumber)), &amber);
        else
            row.Piece("[stale]", &amber);
        Remember("stale");   // the badge's rectangle, for the tests that hover it
        ImGui::SetItemTooltip("%s", StaleTooltip().c_str());
    }

    if (runner.Disabled())
        WrappedColored(kRed(), "Decompiler disabled for this session: " + runner.DisabledReason());
    if (mResolved && !mEntryOk && !mEntryError.empty()) WrappedColored(kRed(), mEntryError);
    if (!mNote.empty()) WrappedDisabled(mNote);
    if (!mInsnNote.empty()) WrappedDisabled(mInsnNote);
    if (!mShownValid) return;

    switch (mShown.status)
    {
    case DecompileStatus::Ok:
        break;
    case DecompileStatus::TooLarge:
        WrappedColored(amber, Fmt("0x%08X: ", mShown.entry) + mShown.error);
        break;
    case DecompileStatus::Disabled:
        WrappedColored(kRed(), "Decompiler disabled for this session: " + mShown.error);
        break;
    default:
        WrappedColored(kRed(), mShown.error.empty() ? "Decompilation failed." : mShown.error);
        break;
    }
    // Uncertainty is said, never left to look like a complete function: the first few in full, the
    // rest behind a count.
    const std::vector<std::string> uncertain = Uncertainties();
    const size_t kShownUncertainties = 4;
    for (size_t i = 0; i < uncertain.size() && i < kShownUncertainties; ++i)
        WrappedColored(amber, uncertain[i]);
    if (uncertain.size() > kShownUncertainties)
    {
        ImGui::TextColored(amber, "%zu more warnings", uncertain.size() - kShownUncertainties);
        if (ImGui::BeginItemTooltip())
        {
            for (size_t i = kShownUncertainties; i < uncertain.size(); ++i) ImGui::TextUnformatted(uncertain[i].c_str());
            ImGui::EndTooltip();
        }
    }
    if (!mHintText.empty())
    {
        WrappedColored(amber, Fmt("%s 0x%08X: ", mHint.mnemonic, mHint.insn) + mHintText);
        ImGui::SetItemTooltip("Where this call goes this time, read from the register file at the halt. "
                              "The target is computed, so another call may go elsewhere.");
    }
    if (!mShown.warnings.empty())
    {
        ImGui::TextDisabled("%zu warning%s", mShown.warnings.size(), mShown.warnings.size() == 1 ? "" : "s");
        if (ImGui::BeginItemTooltip())
        {
            for (const std::string& w : mShown.warnings) ImGui::TextUnformatted(w.c_str());
            ImGui::EndTooltip();
        }
    }
}

RegisterHint DecompilerPanel::HaltHint() const
{
    // Only for registers that belong to the output on screen: a running emulator has moved on
    // since its snapshot, and stale output is from another snapshot than the registers.
    if (!mShownValid || !mSnap || ShownStale() || mViewKind == DecompViewKind::LiveRunning ||
        mViewKind == DecompViewKind::None)
        return RegisterHint();
    RegisterHint h = RegisterTargetAtHalt(*mSnap, mCpu);
    if (!h.valid) return h;
    // The output's token addresses are canonical; the PC may be any mirror. The target was already
    // computed from the raw PC (braf/bsrf add to it), so only the match uses the canonical one.
    uint32_t insn = 0;
    if (!CanonicalAddress(h.insn, insn)) return RegisterHint();
    // ...but only when the halt is in the mirror the output was decompiled at: the same
    // instruction run through another mirror computes other values (a bsrf's target).
    if (h.insn - insn != mShown.requestedEntry - mShown.entry) return RegisterHint();
    h.insn = insn;
    bool inFunction = false;
    for (const Line& l : mShown.lines)
    {
        bool here = false;
        for (const Token& t : l.tokens) here |= t.addr == h.insn;
        if (!here) continue;
        inFunction = true;
        // A call the output already resolved (jsr through a literal) needs no hint -- if it is the
        // very address the CPU is about to call. The same function through another mirror is not
        // what the output shows (its signature can differ), so that keeps its hint.
        for (const Token& t : l.tokens)
            if (t.kind == TokenKind::FuncName && t.calledAt != 0 && t.calledAt == h.target) return RegisterHint();
    }
    return inFunction ? h : RegisterHint();
}

bool DecompilerPanel::InspectionAvailable(std::string* reason) const
{
    const char* why = nullptr;
    if (!mInspectionReady) why = mInspectionReason.empty() ? "capture not ready" : mInspectionReason.c_str();
    else if (!mViewId.valid || !mSnap || !mShownValid || mShown.status != DecompileStatus::Ok)
        why = "no matched successful capture";
    else if (mViewKind != DecompViewKind::Static && mViewKind != DecompViewKind::Scrub &&
             mViewKind != DecompViewKind::LivePaused) why = "capture is running or closed";
    else if (ShownStale() || mShownSnapId != mViewId || mSnapId != mViewId || mShown.key != mSnap->key)
        why = "displayed output and capture do not match";
    if (reason) *reason = why ? why : "";
    return why == nullptr;
}

std::string DecompilerPanel::InspectionText(const InspectionTarget& target) const
{
    if (!InspectionAvailable()) return "";
    const CapturedValue value = InspectCapturedValue(*mSnap, target);
    char text[256];
    std::snprintf(text, sizeof text, "%s: 0x%08X%s",
                  target.kind == InspectionTarget::Kind::AbsoluteObject ? "Storage" : "Object address",
                  target.address, value.loadedBios ? " (loaded BIOS image)" : "");
    std::string out = text;
    if (!target.typeLabel.empty()) out += "\nType: " + target.typeLabel;
    if (target.kind == InspectionTarget::Kind::AbsoluteObject)
    {
        std::snprintf(text, sizeof text, "\nSize: %u bytes\nCaptured memory: ", target.byteSize);
        out += text;
        out += value.available ? value.scalarText : value.reason;
    }
    if (value.hasPointer)
    {
        std::snprintf(text, sizeof text, "\nPointer target: 0x%08X", value.pointer);
        out += text;
    }
    if (!value.preview.bytes.empty())
    {
        out += value.preview.loadedBios ? "\nLoaded BIOS image bytes:" : "\nCaptured bytes:";
        for (uint8_t byte : value.preview.bytes)
        {
            std::snprintf(text, sizeof text, " %02X", byte);
            out += text;
        }
        out += "\nASCII: \"" + value.preview.ascii + "\"";
    }
    if (value.preview.truncated) out += "\nPreview truncated";
    if (!value.preview.reason.empty()) out += "\n" + value.preview.reason;
    std::snprintf(text, sizeof text, "\nSource: %s / %s SH-2 / frame %llu\nCaptured observation, not a recovered value of this occurrence.",
                  mInspectionSource.c_str(), mCpu == 0 ? "master" : "slave",
                  static_cast<unsigned long long>(mSnap->key.frameNumber));
    return out + text;
}

bool DecompilerPanel::InspectionMenuCurrent() const
{
    return mInspectionMenu.valid && InspectionAvailable() && mInspectionMenu.cpu == mCpu &&
           mInspectionMenu.resultRevision == mResultRevision && mInspectionMenu.key == mSnap->key;
}

void DecompilerPanel::DrawInspectionMenu(Request& req)
{
    if (!mInspectionMenu.valid) return;
    const InspectionTarget& target = mInspectionMenu.target;
    const bool current = InspectionMenuCurrent();
    const CapturedValue value = current ? InspectCapturedValue(*mSnap, target) : CapturedValue();
    auto item = [&](const char* label, bool enabled) {
        const bool hit = ImGui::MenuItem(label, nullptr, false, current && enabled);
        Remember(label);
        return hit && InspectionMenuCurrent();
    };
    auto copy = [](uint32_t address) {
        char text[16];
        std::snprintf(text, sizeof text, "0x%08X", address);
        ImGui::SetClipboardText(text);
    };
    uint32_t canonical = 0;
    const bool object = target.kind == InspectionTarget::Kind::AbsoluteObject;
    // Memory can show an object of any size; a Watch needs a scalar the existing backend can read.
    const bool storage = current && object && CanonicalCapturedWramRange(*mSnap, target.address,
        target.byteSize, canonical);
    const uint32_t storageAddress = canonical;
    uint32_t watchable = 0;
    const bool storageWatch = current && object && CanonicalCapturedWram(*mSnap, target.address,
        target.byteSize, watchable);
    if (object)
    {
        if (item("View Object Storage in Memory", storage))
        {
            req.viewHex = true;
            req.hexAddr = storageAddress;
        }
        if (item("Copy Storage Address", true)) copy(target.address);
        const bool scalar = target.byteSize == 1 || target.byteSize == 2 || target.byteSize == 4;
        const bool known = target.display != InspectionTarget::Display::RawBytes;
        if (item("Add Object Address to Watch", storageWatch && scalar && known))
        {
            req.addWatch = true;
            req.watchAddress = watchable;
            const bool sign = target.display == InspectionTarget::Display::SignedInteger;
            req.watchType = target.display == InspectionTarget::Display::Pointer ? WatchType::Pointer :
                target.byteSize == 1 ? (sign ? WatchType::S8 : WatchType::U8) :
                target.byteSize == 2 ? (sign ? WatchType::S16 : WatchType::U16) :
                                      (sign ? WatchType::S32 : WatchType::U32);
            char label[64];
            std::snprintf(label, sizeof label, "Captured object storage %08X", watchable);
            req.watchLabel = label;
        }
    }
    const bool pointerForm = !object || target.display == InspectionTarget::Display::Pointer;
    if (pointerForm)
    {
        const bool pointer = value.hasPointer;
        const bool readable = current && pointer && value.pointer != 0 &&
            CanonicalCapturedWram(*mSnap, value.pointer, 1, canonical);
        if (item("View Pointer Target in Memory", readable))
        {
            req.viewHex = true;
            req.hexAddr = canonical;
        }
        if (item("Copy Pointer Value", pointer)) copy(value.pointer);
        if (item("Watch Memory at Pointer Value", readable))
        {
            req.addWatch = true;
            req.watchAddress = canonical;
            req.watchType = WatchType::U8;
            char label[64];
            std::snprintf(label, sizeof label, "Memory at captured pointer value %08X", canonical);
            req.watchLabel = label;
        }
    }
    ImGui::Separator();
}

void DecompilerPanel::DrawLines(const DecompilerInputs& inputs, Request& req)
{
    if (!mShownValid || mShown.lines.empty())
    {
        if (!mShownValid) ImGui::TextDisabled("Nothing decompiled yet. Follow PC, type an entry, or use \"Decompile\" "
                                              "in SH-2 Assembly, Call Stack or Registers.");
        return;
    }
    if (mShownWidth < 0.0f)
    {
        mShownWidth = 0.0f;
        for (const Line& l : mShown.lines)
            mShownWidth = std::max(mShownWidth, ImGui::CalcTextSize(l.text.c_str()).x);
    }
    // A3: shown, because the rest of the function is usually fine, but greyed; stale output dimmed.
    const float alpha = mShown.unsupported ? 0.5f : (ShownStale() ? 0.7f : 1.0f);
    ApplyWantedInstruction();

    // User notes: each on the first line decompiled from its instruction, so a note shows once
    // even when the instruction feeds several lines. Notes are written at whatever mirror the
    // Assembly panel showed; the output's addresses are canonical.
    mLineNotes.assign(mShown.lines.size(), std::string());
    mUnplacedNotes.clear();
    // The result's explanations of hardware accesses (a documented register pair, the register a
    // keyed write selects, a mirror) go on their lines like the user's notes, after them.
    std::map<uint32_t, std::string> info;
    for (const HardwareNote& n : mShown.hardwareNotes)
        if (!n.caution) info[n.address] += (info[n.address].empty() ? "" : "; ") + n.text;
    if ((inputs.comments && !inputs.comments->empty()) || !info.empty())
    {
        std::map<uint32_t, std::string> notes;
        if (inputs.comments)
            for (const auto& c : *inputs.comments)
            {
                uint32_t canonical = 0;
                if (!c.second.empty() && CanonicalAddress(c.first, canonical)) notes[canonical] = c.second;
            }
        for (const auto& n : info) notes[n.first] += (notes[n.first].empty() ? "" : "; ") + n.second;
        std::set<uint32_t> placed;
        for (size_t i = 0; i < mShown.lines.size(); ++i)
        {
            std::set<uint32_t> here;
            for (const Token& t : mShown.lines[i].tokens)
                if (t.addr) here.insert(t.addr);
            for (uint32_t a : here)
            {
                auto it = notes.find(a);
                if (it == notes.end() || !placed.insert(a).second) continue;
                if (!mLineNotes[i].empty()) mLineNotes[i] += "; ";
                mLineNotes[i] += it->second;
            }
        }
        // The rest of the body's notes: their instruction feeds a line (a literal load folded into
        // a constant, a nop) without being one, so they have no line of their own.
        for (const auto& range : mShown.body)
            for (auto it = notes.lower_bound(range.first); it != notes.end() && it->first < range.second; ++it)
                if (placed.count(it->first) == 0)
                {
                    char b[16];
                    std::snprintf(b, sizeof b, "0x%08X: ", it->first);
                    mUnplacedNotes.push_back(b + it->second);
                }
    }

    if (!mUnplacedNotes.empty())
    {
        ImGui::TextDisabled("%zu note%s on instructions with no line of their own", mUnplacedNotes.size(),
                            mUnplacedNotes.size() == 1 ? "" : "s");
        if (ImGui::BeginItemTooltip())
        {
            for (const std::string& n : mUnplacedNotes) ImGui::TextUnformatted(n.c_str());
            ImGui::EndTooltip();
        }
    }

    // The scroll range covers the trailing comments too (the halt hint, the notes), which are drawn
    // after a line's text: a long note past the window's edge must be reachable. Recomputed every
    // frame, since notes and the hint change without a new result.
    float width = mShownWidth;
    for (size_t i = 0; i < mShown.lines.size(); ++i)
    {
        const Line& l = mShown.lines[i];
        float trailing = 0.0f;
        if (mHint.valid)
            for (const Token& t : l.tokens)
                if (t.addr == mHint.insn)
                {
                    trailing += ImGui::CalcTextSize(("  // " + mHintText).c_str()).x;
                    break;
                }
        if (!mLineNotes[i].empty()) trailing += ImGui::CalcTextSize(("  // " + mLineNotes[i]).c_str()).x;
        if (trailing > 0.0f) width = std::max(width, ImGui::CalcTextSize(l.text.c_str()).x + trailing);
    }
    ImGui::SetNextWindowContentSize(ImVec2(width + ImGui::GetStyle().WindowPadding.x * 2.0f, 0.0f));
    if (!ImGui::BeginChild("##code", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImGui::EndChild();
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(mShown.lines.size()));
    // The line a "Show in Pseudocode" selected is scrolled to: it must be submitted, clipped or not.
    if (mScrollToLine >= 0 && mScrollToLine < static_cast<int>(mShown.lines.size())) clipper.IncludeItemByIndex(mScrollToLine);
    else mScrollToLine = -1;
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            const Line& ln = mShown.lines[static_cast<size_t>(i)];
            ImGui::PushID(i);
            if (RowSelectable("##ln", i == mSelectedLine, false))
            {
                mSelectedLine = i;
                mInsnNote.clear();
                // A line navigates to the first SH-2 instruction it was decompiled from.
                if (ln.firstAddr)
                {
                    req.jumpAssembly = true;
                    req.cpu = mCpu;
                    req.addr = ln.firstAddr;
                }
            }
            const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            if (i == mScrollToLine)
            {
                ImGui::SetScrollHereY(0.3f);
                mScrollToLine = -1;
            }
            mDrawn.push_back(DrawnLine{ i, min, max });
            const bool rowHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
            const int tokenIndex = rowHovered ? TokenAtX(ln, ImGui::GetMousePos().x - min.x,
                [](const char* a, const char* b) { return ImGui::CalcTextSize(a, b).x; }) : -1;
            const InspectionTarget* target = nullptr;
            if (tokenIndex >= 0)
            {
                const uint32_t binding = ln.tokens[static_cast<size_t>(tokenIndex)].inspectionTarget;
                if (binding && binding <= mShown.inspectionTargets.size())
                    target = &mShown.inspectionTargets[binding - 1];
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                if (target && InspectionAvailable())
                {
                    // The value first, and then where the line came from: a line several instructions contribute to is marked on every token (A7-13).
                    mLineTooltip = InspectionText(*target);
                    if (ln.firstAddr) mLineTooltip += "\n\n" + LineOriginText(ln);
                }
                else if (ln.firstAddr) mLineTooltip = LineOriginText(ln);
                if (!mLineTooltip.empty()) ImGui::SetTooltip("%s", mLineTooltip.c_str());
            }
            // The pointer moves into the menu next frame. Freeze owned metadata now; all actions
            // below revalidate the capture and result rather than reusing the hovered row.
            if (rowHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
                mInspectionMenu = InspectionMenu();
                if (target && InspectionAvailable())
                {
                    mInspectionMenu.valid = true;
                    mInspectionMenu.target = *target;
                    mInspectionMenu.key = mSnap->key;
                    mInspectionMenu.resultRevision = mResultRevision;
                    mInspectionMenu.cpu = mCpu;
                }
            }

            if (ImGui::BeginPopupContextItem("lnctx"))
            {
                DrawInspectionMenu(req);
                auto item = [&](const char* label, bool enabled) {
                    const bool hit = ImGui::MenuItem(label, nullptr, false, enabled);
                    Remember(label);
                    return hit;
                };
                uint32_t callee = 0;
                for (const Token& t : ln.tokens)
                    if (t.kind == TokenKind::FuncName && t.target && t.target != mShown.entry)
                    {
                        callee = t.target;
                        break;
                    }
                if (item("Show in SH-2 Assembly", ln.firstAddr != 0))
                {
                    req.jumpAssembly = true;
                    req.cpu = mCpu;
                    req.addr = ln.firstAddr;
                }
                // A line from several instructions: the click and the item above show the first; the rest are here (A7-2).
                {
                    const std::vector<uint32_t> at = LineInstructions(ln);
                    if (at.size() > 1)
                    {
                        // Every other instruction is reachable (A7-13); the list scrolls past kMenuInstructions rows.
                        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                                            ImVec2(FLT_MAX, ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(kMenuInstructions) +
                                                                                ImGui::GetStyle().WindowPadding.y * 2.0f));
                        const bool open = ImGui::BeginMenu("Show Other Instructions");
                        Remember("Show Other Instructions");
                        if (open)
                        {
                            for (size_t k = 1; k < at.size(); ++k)
                            {
                                char b[24];
                                std::snprintf(b, sizeof b, "0x%08X", at[k]);
                                if (item(b, true))
                                {
                                    req.jumpAssembly = true;
                                    req.cpu = mCpu;
                                    req.addr = at[k];
                                }
                            }
                            ImGui::EndMenu();
                        }
                    }
                }
                if (item("Rename Function...", true))
                {
                    req.rename = true;
                    req.renameAddr = mShown.entry;
                }
                if (callee)
                {
                    char b[48];
                    std::snprintf(b, sizeof b, "Rename Callee 0x%08X...", callee);
                    if (item(b, true))
                    {
                        req.rename = true;
                        req.renameAddr = callee;
                    }
                }
                // ...not at an odd address (the word flow halted at as bad data can be one): an SH-2 function starts at an even address (A7-6).
                if (item("Set Function Start Here",
                         inputs.bounds && ln.firstAddr != 0 && (ln.firstAddr & 1u) == 0 && ln.firstAddr != mShown.entry))
                {
                    // A boundary correction. Every question is keyed by the function set, so the
                    // answers it outdates -- this function's and its callers' -- are not found again.
                    // The start is recorded canonical; the new function is asked for in the
                    // mirror the shown one runs in.
                    if (inputs.bounds) inputs.bounds->SetEntry(ln.firstAddr);
                    req.boundsChanged = true;
                    Target t;
                    t.kind = TargetKind::Entry;
                    t.address = ln.firstAddr + (mShown.requestedEntry - mShown.entry);
                    t.source = EntrySource::User;
                    SetTarget(t);
                }
                if (item("View in Memory", ln.firstAddr != 0))
                {
                    req.viewHex = true;
                    req.hexAddr = ln.firstAddr;
                }
                ImGui::Separator();
                if (item("Copy Line", true)) ImGui::SetClipboardText((CopyMarks() + ln.text).c_str());
                if (item("Copy Function", true)) ImGui::SetClipboardText((CopyMarks() + mShown.Text()).c_str());
                ImGui::EndPopup();
            }

            // The tokens, drawn over the row: one draw call per run of a colour, no items.
            float x = min.x;
            const char* text = ln.text.c_str();
            size_t pos = 0;
            auto run = [&](size_t a, size_t b, TokenKind kind) {
                if (b <= a) return;
                ImVec4 c = TokenColour(kind);
                c.w *= alpha;
                dl->AddText(ImVec2(x, min.y), ImGui::GetColorU32(c), text + a, text + b);
                x += ImGui::CalcTextSize(text + a, text + b).x;
            };
            for (const Token& t : ln.tokens)
            {
                const size_t b = std::min<size_t>(t.begin, ln.text.size());
                const size_t e = std::min<size_t>(t.end, ln.text.size());
                if (b < pos) continue;
                run(pos, b, TokenKind::Plain);
                run(b, e, t.kind);
                pos = e;
            }
            run(pos, ln.text.size(), TokenKind::Plain);
            // Trailing comments: the halt hint on the line the call was decompiled into, then the
            // user's notes for the line.
            auto trailing = [&](const std::string& note, ImVec4 c) {
                const std::string t = "  // " + note;
                c.w *= alpha;
                dl->AddText(ImVec2(x, min.y), ImGui::GetColorU32(c), t.c_str());
                x += ImGui::CalcTextSize(t.c_str()).x;
            };
            if (mHint.valid)
                for (const Token& t : ln.tokens)
                    if (t.addr == mHint.insn)
                    {
                        trailing(mHintText, kAmber());
                        break;
                    }
            if (!mLineNotes[static_cast<size_t>(i)].empty())
                trailing(mLineNotes[static_cast<size_t>(i)], TokenColour(TokenKind::Comment));
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

}  // namespace sfe
