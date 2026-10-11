// DecompilerPanel — see the header for the refresh policy and the install rule.
#include "DecompilerPanel.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>

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
    mNote.clear();
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
    mShownWidth = -1.0f;   // measured at the next Draw, which has a font
    if (!sameFunction) mSelectedLine = -1;
    mNote.clear();
}

bool DecompilerPanel::Accept(DecompileResult& r, uint64_t runnerEpoch)
{
    // The four checks of the install rule (header). The runner has already dropped results from
    // an older epoch or an older generation, but it cannot know that this panel has since taken
    // another snapshot or wants another entry -- and a result for memory other than the snapshot
    // held would show code that is not what the badge says it is.
    const bool current = mPending.active && r.slot == kSlot &&
                         r.generation == mPending.generation &&       // the outstanding request
                         mPending.epoch == runnerEpoch &&              // of this session
                         mSnap && r.key == mPending.key && r.key == mSnap->key &&   // for the snapshot held now
                         mResolved && mEntryOk && r.requestedEntry == mPending.entry &&
                         mPending.entry == mEntry;                     // and the entry wanted now
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

    // Results first, against the state the requests were made in.
    const uint64_t epoch = runner.Epoch();
    DecompileResult r;
    while (runner.Poll(r))
        if (r.slot == kSlot) Accept(r, epoch);
    // Neither a disabled runner nor an ended session will answer what is outstanding.
    if (mPending.active && (runner.Disabled() || mPending.epoch != epoch)) mPending.active = false;

    mViewId = IdOf(view);
    for (int cpu = 0; cpu < 2; ++cpu)
    {
        mViewHavePc[cpu] = view.haveRegs[cpu];
        mViewPc[cpu] = view.regs[cpu].pc;
    }
    mNamesGeneration = inputs.namesGeneration;
    if (!visible || !mViewId.valid) return;

    // Which snapshot to hold (A1). Recomputed every frame; capturing copies 2 MiB, so it happens
    // only when the view's identity moved AND the policy says to follow it.
    const bool snapCurrent = mSnap && mSnapId == mViewId;
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
            // A running emulator moves every frame: only an explicit action, the first look, or the
            // opt-in auto-refresh -- debounced, never over a request still running, and only once the
            // followed PC has left the function.
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
            capture = mExplicit || !mSnap || autoDue;
            break;
        }
        case DecompViewKind::None:
            break;
        }
    }
    mExplicit = false;
    if (capture)
    {
        mSnap = MemorySnapshot::Capture(*view.backend, view.regs, view.haveRegs, view.sourceId, view.deriveSerial,
                                        view.frameNumber, view.bios);
        mSnapId = mViewId;
        mCaptureTick = mTick;
        ++mCaptures;
        mResolved = false;
    }
    if (!mSnap) return;
    if (!mResolved) Resolve(inputs);
    const bool force = mForceSubmit, retry = mRetryTooLarge;
    mForceSubmit = mRetryTooLarge = false;
    if (!mEntryOk) return;

    Question q;
    q.valid = true;
    q.key = mSnap->key;
    q.entry = mEntry;
    q.namesGeneration = mNamesGeneration;
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
    DrawHeader(view, runner, req);
    DrawStatus(view, runner);
    DrawLines(inputs, req);
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
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("0x00000000").x + ImGui::GetStyle().FramePadding.x * 2.0f);
    if (ImGui::InputTextWithHint("##entry", "entry", mEntryBuf, sizeof mEntryBuf,
                                 ImGuiInputTextFlags_EnterReturnsTrue))
    {
        char* end = nullptr;
        const unsigned long v = std::strtoul(mEntryBuf, &end, 16);
        if (end != mEntryBuf && end && *end == '\0')
        {
            Target t;
            t.kind = TargetKind::Entry;
            t.address = static_cast<uint32_t>(v);
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

    ImGui::SetNextWindowContentSize(ImVec2(mShownWidth + ImGui::GetStyle().WindowPadding.x * 2.0f, 0.0f));
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
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

}  // namespace sfe
