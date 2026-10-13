// DecompilerPanel — see the header for the refresh policy and the install rule.
#include "DecompilerPanel.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>

#include "Debug/MemoryBackend.h"
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
    case TokenKind::Keyword:  return ImVec4(0.42f, 0.62f, 0.95f, 1.0f);
    case TokenKind::Type:     return ImVec4(0.36f, 0.80f, 0.75f, 1.0f);
    case TokenKind::FuncName: return ImVec4(0.93f, 0.84f, 0.45f, 1.0f);
    case TokenKind::Variable: return ImVec4(0.75f, 0.85f, 0.98f, 1.0f);
    case TokenKind::Param:    return ImVec4(0.96f, 0.68f, 0.45f, 1.0f);
    case TokenKind::Const:    return ImVec4(0.60f, 0.85f, 0.52f, 1.0f);
    case TokenKind::Comment:  return ImVec4(0.50f, 0.56f, 0.50f, 1.0f);
    case TokenKind::Global:   return ImVec4(0.80f, 0.60f, 0.92f, 1.0f);
    case TokenKind::Label:    return ImVec4(0.85f, 0.70f, 0.85f, 1.0f);
    case TokenKind::Error:    return ImVec4(0.95f, 0.40f, 0.40f, 1.0f);
    case TokenKind::Plain:
    default:                  return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}

}  // namespace

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

const ImVec4 kAmber(0.95f, 0.75f, 0.30f, 1.0f);
const ImVec4 kRed(0.95f, 0.42f, 0.42f, 1.0f);

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
    mFocus = true;
}

void DecompilerPanel::SetCpu(int cpu)
{
    cpu &= 1;
    if (cpu == mCpu) return;
    mCpu = cpu;
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
    mPending = Outstanding();
    mAsked = Question();
    mSnap.reset();
    mSnapId = ViewId();
    mResolved = false;
    mCache.clear();
    mCallees.clear();
    mPrototypes.clear();
    mNote.clear();
}

uint64_t DecompilerPanel::PrototypeHash(uint32_t entry) const
{
    // FNV-1a over the lent prototypes, in address order.
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i)
        {
            h ^= (v >> (8 * i)) & 0xFF;
            h *= 1099511628211ull;
        }
    };
    auto shape = [&mix](const TypeShape& t) {
        mix((uint64_t(t.kind) << 24) | (uint64_t(t.size) << 16) | (uint64_t(t.pointee) << 8) | t.pointeeSize);
    };
    for (const auto& p : mPrototypes)
    {
        if (!Lendable(p.second, entry, p.first)) continue;
        mix(p.first);
        shape(p.second.proto.ret);
        mix(p.second.proto.params.size());
        for (const TypeShape& t : p.second.proto.params) shape(t);
    }
    return h;
}

bool DecompilerPanel::ShownOutdatedByView() const
{
    return mShownValid && mShownSnapId != mViewId;
}

bool DecompilerPanel::ShownStale() const
{
    if (!mShownValid) return false;
    return ShownOutdatedByView() || mPending.active || mShownNamesGeneration != mNamesGeneration ||
           (mResolved && (!mEntryOk || mShown.entry != mEntry));
}

void DecompilerPanel::Resolve(const DecompilerInputs& inputs)
{
    mResolved = true;
    mEntryOk = false;
    mEntry = 0;
    mEntrySource = EntrySource::None;
    mEntryConfidence = 0;
    mEntryError.clear();
    char b[200];

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
            return;
        }
        mEntryOk = true;
        mEntry = canonical;
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
            return;
        }
        anchor = mSnap->regs[mCpu].pc;
    }

    FinderHints hints;
    if (inputs.bounds) hints.userEntries = inputs.bounds->Entries();
    if (inputs.names)
        for (const auto& n : *inputs.names) hints.symbols.insert(n.first);
    if (inputs.callSites) inputs.callSites(hints.callSiteTargets);
    hints.callees = mCallees;
    const EntryGuess g = FindFunctionContaining(*mSnap, anchor, hints);
    if (!g.ok)
    {
        std::snprintf(b, sizeof b, "No function found containing 0x%08X: %s", anchor, g.why.c_str());
        mEntryError = b;
        return;
    }
    mEntryOk = true;
    mEntry = g.entry;
    mEntrySource = g.source;
    mEntryConfidence = g.confidence;
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

void DecompilerPanel::CacheDropEntry(uint32_t entry)
{
    for (auto it = mCache.begin(); it != mCache.end();)
        it = it->question.entry == entry ? mCache.erase(it) : std::next(it);
}

void DecompilerPanel::Install(DecompileResult& result, const ViewId& snapId, uint64_t namesGeneration)
{
    const bool sameFunction = mShownValid && mShown.entry == result.entry;
    mShown = std::move(result);
    mShownValid = true;
    mShownSnapId = snapId;
    mShownNamesGeneration = namesGeneration;
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
    for (uint32_t c : mShown.callees) mCallees.insert(c);
    if (mShown.status == DecompileStatus::Ok && mShown.prototype.known)
        mPrototypes[mShown.entry] = LearnedPrototype{ mShown.key, mShown.prototype };
    else
        mPrototypes.erase(mShown.entry);
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
                         mPending.prototypes == PrototypeHash(mEntry) &&   // lending the signatures known now,
                         HeldIsOnScreen();                             // while that snapshot is what is shown
    if (!current)
    {
        ++mDiscarded;
        // The answer to the outstanding request came back and is not wanted: nothing else will.
        if (mPending.active && r.slot == kSlot && r.generation == mPending.generation) mPending.active = false;
        return false;
    }
    mPending.active = false;
    if (r.status == DecompileStatus::Cancelled)
    {
        // The user's Stop. What was shown stays; the same question is not asked again until Refresh.
        mNote = "Stopped.";
        return false;
    }
    if (r.status == DecompileStatus::Ok)
    {
        Question q;
        q.valid = true;
        q.key = mPending.key;
        q.entry = mPending.entry;
        q.namesGeneration = mPending.namesGeneration;
        q.prototypes = mPending.prototypes;
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
        if (mPending.active) runner.Cancel(kSlot);
    }

    // First bring the panel's own state up to date with what is on screen -- the view's identity,
    // the names, the snapshot it holds and the entry it wants -- and only then look at results.
    // The other order would judge a finished answer against last frame's state: an answer for B,
    // completed while the view moved on to C, would be installed before the panel noticed C.
    mViewId = IdOf(view);
    mViewKind = view.kind;
    for (int cpu = 0; cpu < 2; ++cpu)
    {
        mViewHavePc[cpu] = view.haveRegs[cpu];
        mViewPc[cpu] = view.regs[cpu].pc;
    }
    mNamesGeneration = inputs.namesGeneration;
    const bool active = visible && mViewId.valid;
    if (active)
    {
        // Which snapshot to hold (A1). Recomputed every frame; capturing copies 2 MiB, so it happens
        // only when the view's identity moved AND the policy says to follow it.
        const bool snapCurrent = mSnap && mSnapId == mViewId;
        // A BIOS image chosen, reloaded or dropped is the user's doing, not an advancing frame: the
        // held snapshot's region set is wrong from then on, whatever kind of source is shown.
        const bool biosChanged = mSnap && mSnapId.biosHash != mViewId.biosHash;
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
                    const uint32_t pc = mViewPc[mCpu];
                    uint32_t canonical = 0;
                    const bool known = mViewHavePc[mCpu] && CanonicalAddress(pc, canonical);
                    const uint32_t span = mShownValid ? std::max<uint32_t>(mShown.bodyBytes, 2u) : 0u;
                    autoDue = known && (!mShownValid || canonical < mShown.entry || canonical - mShown.entry >= span);
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
                if (mTarget.kind == TargetKind::FollowPc && mSnap && mResolved && !mEntryOk &&
                    !mPending.active && mSnap->haveRegs[mCpu] && view.haveRegs[mCpu] &&
                    view.regs[mCpu].pc != mSnap->regs[mCpu].pc && mTick - mCaptureTick >= kAutoRefreshFrames)
                    unanswered = true;
                capture = mExplicit || !mSnap || biosChanged || autoDue || regsArrived || unanswered;
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
        if (mSnap && !mResolved) Resolve(inputs);
    }

    // Results, against the state just brought up to date.
    const uint64_t epoch = runner.Epoch();
    DecompileResult r;
    while (runner.Poll(r))
        if (r.slot == kSlot) Accept(r, epoch);
    // Neither a disabled runner nor an ended session will answer what is outstanding.
    if (mPending.active && (runner.Disabled() || mPending.epoch != epoch)) mPending.active = false;

    if (!active || !mSnap) return;
    const bool force = mForceSubmit, retry = mRetryTooLarge;
    mForceSubmit = mRetryTooLarge = false;
    if (!mEntryOk) return;

    Question q;
    q.valid = true;
    q.key = mSnap->key;
    q.entry = mEntry;
    q.namesGeneration = mNamesGeneration;
    q.prototypes = PrototypeHash(mEntry);
    if (q == mAsked && !force) return;   // asked already: its answer, or its failure, is shown
    mAsked = q;
    mNote.clear();

    if (!force)
        if (const CacheEntry* hit = CacheFind(q))
        {
            DecompileResult copy = hit->result;
            const ViewId snapId = hit->snapId;
            mPending.active = false;
            Install(copy, snapId, q.namesGeneration);
            ++mCacheHits;
            return;
        }

    if (runner.Disabled())
    {
        mPending.active = false;
        return;
    }
    DecompileRequest rq;
    rq.entry = mEntry;
    if (inputs.names)
        for (const auto& n : *inputs.names) rq.names.push_back(n);
    if (inputs.bounds)
        rq.knownFunctions.assign(inputs.bounds->Entries().begin(), inputs.bounds->Entries().end());
    for (const auto& p : mPrototypes)
        if (Lendable(p.second, mEntry, p.first)) rq.prototypes.emplace_back(p.first, p.second.proto);
    const uint64_t generation = runner.Submit(kSlot, mSnap, std::move(rq), retry);
    ++mSubmissions;
    if (generation == 0)
    {
        mPending.active = false;
        return;
    }
    mPending.active = true;
    mPending.generation = generation;
    mPending.epoch = runner.Epoch();
    mPending.key = mSnap->key;
    mPending.entry = mEntry;
    mPending.snapId = mSnapId;
    mPending.namesGeneration = mNamesGeneration;
    mPending.prototypes = q.prototypes;
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
        ImGui::TextColored(kRed, "Decompiler unavailable: %s", why.c_str());
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

    ImGui::SameLine();
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
            t.address = mEntryOk ? mEntry : (mShownValid ? mShown.entry : 0);
            t.source = mEntryOk ? mEntrySource : EntrySource::User;
        }
        SetTarget(t);
    }

    ImGui::SameLine();
    // Not while the box is being edited: ImGui owns its text then, and would also lose the draft.
    if (mSyncEntryBox && !mEntryActive && mShownValid)
    {
        std::snprintf(mEntryBuf, sizeof mEntryBuf, "%08X", mShown.entry);
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
        }
        else
        {
            mResolved = true;
            mEntryOk = false;
            mEntryError = "Type the function's entry as a hex address, e.g. 06004000.";
        }
    }
    mEntryActive = ImGui::IsItemActive();
    Remember("entry");   // the input box: nothing in the branch above submits an item
    ImGui::SetItemTooltip("Decompile the function starting at this address (hex; any mirror). Enter to go.");

    ImGui::SameLine();
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
        ImGui::SameLine();
        if (ImGui::Button("Stop")) mCancelRequested = true;
        Remember("stop");
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("Auto-refresh", &mAutoRefresh)) req.settingsChanged = true;
    ImGui::SetItemTooltip("While the emulator runs and Follow PC is on: refresh at most every %d frames, and\n"
                          "only once the PC has left the function shown. Off: the output is marked stale\n"
                          "until you press Refresh.", kAutoRefreshFrames);

    ImGui::SameLine();
    if (ImGui::Button("BIOS")) ImGui::OpenPopup("bios");
    ImGui::SetItemTooltip(view.bios ? "A BIOS image is mapped at 0x00000000." : "No BIOS image: calls into the BIOS stop there.");
    if (ImGui::BeginPopup("bios"))
    {
        if (ImGui::MenuItem("Choose BIOS Image...")) req.chooseBios = true;
        if (ImGui::MenuItem("Reload BIOS Image", nullptr, false, view.bios != nullptr)) req.reloadBios = true;
        if (ImGui::MenuItem("Stop Using BIOS Image", nullptr, false, view.bios != nullptr)) req.clearBios = true;
        ImGui::EndPopup();
    }
}

void DecompilerPanel::DrawStatus(const DecompilerView& view, const DecompilerRunner& runner)
{
    if (view.kind == DecompViewKind::None)
        ImGui::TextDisabled("No source");
    else
        ImGui::TextDisabled("%s \xc2\xb7 frame %llu", view.sourceLabel,
                            static_cast<unsigned long long>(view.frameNumber));
    if (mShownValid)
    {
        ImGui::SameLine();
        ImGui::Text("\xc2\xb7 %s @ 0x%08X", mShown.name.empty() ? "?" : mShown.name.c_str(), mShown.entry);
        if (!mShownEntrySource.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(entry from %s)", mShownEntrySource.c_str());
        }
    }
    if (mPending.active)
    {
        ImGui::SameLine();
        ImGui::TextColored(kAmber, "decompiling\xe2\x80\xa6");
    }
    if (ShownStale())
    {
        ImGui::SameLine();
        if (ShownOutdatedByView())
            ImGui::TextColored(kAmber, "[stale: frame %llu]", static_cast<unsigned long long>(mShown.key.frameNumber));
        else
            ImGui::TextColored(kAmber, "[stale]");
        if (view.kind == DecompViewKind::LiveRunning && !mPending.active && ShownOutdatedByView())
            ImGui::SetItemTooltip("The emulator has moved on since this was decompiled. Press Refresh.");
        else
            ImGui::SetItemTooltip("This output is from an earlier state; its replacement is shown when it is ready.");
    }

    if (runner.Disabled())
        ImGui::TextColored(kRed, "Decompiler disabled for this session: %s", runner.DisabledReason().c_str());
    if (mResolved && !mEntryOk && !mEntryError.empty()) ImGui::TextColored(kRed, "%s", mEntryError.c_str());
    if (!mNote.empty()) ImGui::TextDisabled("%s", mNote.c_str());
    if (!mShownValid) return;

    switch (mShown.status)
    {
    case DecompileStatus::Ok:
        break;
    case DecompileStatus::TooLarge:
        ImGui::TextColored(kAmber, "0x%08X: %s", mShown.entry, mShown.error.c_str());
        break;
    case DecompileStatus::Disabled:
        ImGui::TextColored(kRed, "Decompiler disabled for this session: %s", mShown.error.c_str());
        break;
    default:
        ImGui::TextColored(kRed, "%s", mShown.error.empty() ? "Decompilation failed." : mShown.error.c_str());
        break;
    }
    if (mShown.unsupported)
        ImGui::TextColored(kAmber, "PC-relative load in a delay slot at 0x%08X: pseudocode for this function is not "
                                   "trustworthy.", mShown.unsupportedAt);
    if (mShown.truncated)
        ImGui::TextColored(kAmber, "Flows into %s at 0x%08X; the function is cut off there.",
                           mShown.truncatedWhy.empty() ? "uncaptured memory" : mShown.truncatedWhy.c_str(),
                           mShown.truncatedAt);
    if (!mHintText.empty())
    {
        ImGui::TextColored(kAmber, "%s 0x%08X: %s", mHint.mnemonic, mHint.insn, mHintText.c_str());
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
    h.insn = insn;
    uint32_t target = 0;
    const bool canonical = CanonicalAddress(h.target, target);
    bool inFunction = false;
    for (const Line& l : mShown.lines)
    {
        bool here = false;
        for (const Token& t : l.tokens) here |= t.addr == h.insn;
        if (!here) continue;
        inFunction = true;
        // A call the output already resolved (jsr through a literal) needs no hint.
        for (const Token& t : l.tokens)
            if (t.kind == TokenKind::FuncName && canonical && t.target == target) return RegisterHint();
    }
    return inFunction ? h : RegisterHint();
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

    // User notes: each on the first line decompiled from its instruction, so a note shows once
    // even when the instruction feeds several lines. Notes are written at whatever mirror the
    // Assembly panel showed; the output's addresses are canonical.
    mLineNotes.assign(mShown.lines.size(), std::string());
    mUnplacedNotes.clear();
    if (inputs.comments && !inputs.comments->empty())
    {
        std::map<uint32_t, const std::string*> notes;
        for (const auto& c : *inputs.comments)
        {
            uint32_t canonical = 0;
            if (!c.second.empty() && CanonicalAddress(c.first, canonical)) notes[canonical] = &c.second;
        }
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
                mLineNotes[i] += *it->second;
            }
        }
        // The rest of the body's notes: their instruction feeds a line (a literal load folded into
        // a constant, a delay slot) without being one, so they have no line of their own.
        const uint32_t end = mShown.entry + mShown.bodyBytes;
        for (auto it = notes.lower_bound(mShown.entry); it != notes.end() && it->first < end; ++it)
            if (placed.count(it->first) == 0)
            {
                char b[16];
                std::snprintf(b, sizeof b, "0x%08X: ", it->first);
                mUnplacedNotes.push_back(b + *it->second);
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
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            const Line& ln = mShown.lines[static_cast<size_t>(i)];
            ImGui::PushID(i);
            if (RowSelectable("##ln", i == mSelectedLine, false))
            {
                mSelectedLine = i;
                // A line navigates to the first SH-2 instruction it was decompiled from.
                if (ln.firstAddr)
                {
                    req.jumpAssembly = true;
                    req.cpu = mCpu;
                    req.addr = ln.firstAddr;
                }
            }
            const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            mDrawn.push_back(DrawnLine{ i, min, max });
            if (ln.firstAddr) ImGui::SetItemTooltip("0x%08X", ln.firstAddr);

            if (ImGui::BeginPopupContextItem("lnctx"))
            {
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
                if (item("Set Function Start Here",
                         inputs.bounds && ln.firstAddr != 0 && ln.firstAddr != mShown.entry))
                {
                    // A boundary correction: the old entry's results go, the new entry is asked for.
                    if (inputs.bounds) inputs.bounds->SetEntry(ln.firstAddr);
                    req.boundsChanged = true;
                    CacheDropEntry(mShown.entry);
                    Target t;
                    t.kind = TargetKind::Entry;
                    t.address = ln.firstAddr;
                    t.source = EntrySource::User;
                    SetTarget(t);
                }
                if (item("View in Memory", ln.firstAddr != 0))
                {
                    req.viewHex = true;
                    req.hexAddr = ln.firstAddr;
                }
                ImGui::Separator();
                if (item("Copy Line", true)) ImGui::SetClipboardText(ln.text.c_str());
                if (item("Copy Function", true)) ImGui::SetClipboardText(mShown.Text().c_str());
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
                        trailing(mHintText, kAmber);
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
