// DecompilerPanel — the "SH-2 Pseudocode" window (Docs/Decompiler/PLAN.md Step 3).
//
// The panel draws what the engine (Step 2) produced; it does no analysis of its own. Each frame
// App hands it the context on screen -- after ScopedContextSwap, so a scrubbed frame is what it
// sees, never the live one behind it -- as a DecompilerView, and the app's one DecompilerRunner.
//
// What it decompiles is an immutable MemorySnapshot it captured from that view. Which snapshot
// that is, and when it is replaced, is the A1 refresh policy:
//
//   * Every frame the view's identity {source, derive serial, frame, BIOS hash} is recomputed. It
//     is cheap -- four integers; the BIOS hash was computed once when the image was loaded. When
//     it differs from the captured snapshot's, the snapshot no longer shows what is on screen.
//   * A dump, savestate, paused emulator or recorded (scrubbed) frame is then captured again at
//     once and the request re-run: those only move when the user did something.
//   * A running emulator moves every frame. Its snapshot is kept, and the output shown with a
//     stale badge, until Refresh -- or, opt in, an auto-refresh at most every kAutoRefreshFrames
//     frames and only once the followed PC has left the function shown. Choosing, reloading or
//     dropping a BIOS image is a user action: it is followed at once on every kind of source.
//
// A result is installed only when it still answers the question being asked: the request the
// panel has outstanding (generation), made in the current session (runner epoch), for the snapshot
// the panel holds now (key), for the entry it now wants, under the names and function set in force
// now, lending only the callee signatures still current. The runner's
// own filtering covers the first two; the panel checks all of them itself, because the runner
// cannot know which snapshot, entry or names the panel has since moved on to -- and, since a
// hidden panel takes no new snapshot, that the snapshot it holds is still what is on screen
// (except on a running emulator, whose held snapshot lags by design -- in frames and writes, not
// in source or BIOS image). Each Update brings
// that state up to date with the view BEFORE it looks at results, so an answer that finished while
// the view moved on is judged against what is on screen now, not against last frame. Anything else is discarded, and whatever was on screen
// stays there, labelled stale, until its replacement arrives -- the panel never blanks while it
// waits, and never shows output for memory other than what the badge says.
//
// A request is submitted once per {snapshot key, entry, names, function set, callee signatures lent}: a failure is shown, not retried
// every frame. Results are kept in a small LRU under that same full key, which is what makes
// scrubbing back and forth through recorded frames cheap (App gives a recorded frame a stable
// identity for as long as the recording it came from is unchanged).
#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "saturnexplorer/SaturnExplorer.h"

#include "Decompiler/DecompilerRunner.h"
#include "Decompiler/MemorySnapshot.h"
#include "Decompiler/RegisterHint.h"
#include "Decompiler/Sh2Decompiler.h"
#include "Decompiler/Sh2FunctionFinder.h"

namespace sfe
{

class IMemoryBackend;

// How the context on screen is refreshed, which decides when the panel re-requests (A1).
enum class DecompViewKind : uint8_t
{
    None,          // no source
    Static,        // a dump or a savestate: changes only on an edit
    LivePaused,    // a live emulator, paused or halted
    LiveRunning,   // a live emulator, running: changes every frame
    Scrub,         // a recorded frame from the rewind history
};

// The context on screen this frame, as App sees it after ScopedContextSwap.
struct DecompilerView
{
    IMemoryBackend* backend = nullptr;
    DecompViewKind kind = DecompViewKind::None;
    const char* sourceLabel = "";    // "savestate", "live, paused", "recorded frame", ...
    uint64_t sourceId = 0;           // what App vouches is the same source (stable per recorded frame)
    uint64_t deriveSerial = 0;
    uint64_t frameNumber = 0;
    se_sh2_regs regs[2] = {};
    bool haveRegs[2] = { false, false };
    std::shared_ptr<const decomp::BiosImage> bios;   // content hashed once, at load
};

// What the panel reads from the rest of the app.
struct DecompilerInputs
{
    const std::map<uint32_t, std::string>* names = nullptr;   // FunctionNames::All
    uint64_t namesGeneration = 0;                               // FunctionNames::Generation
    decomp::FunctionBounds* bounds = nullptr;                   // user function starts
    // The user's notes from SH-2 Assembly, keyed by the address they were written at (any mirror).
    // A display overlay: shown on the lines decompiled from that instruction, never sent to the
    // engine, so editing one re-requests nothing (A1).
    const std::unordered_map<uint32_t, std::string>* comments = nullptr;
    // Entries the call stack knows (the finder's call-site rung): only those of a stack read from
    // the memory 'snap' names, since another frame's stack can name a function that is not there.
    // Called only when an entry is being resolved, never per frame; callSitesRevision, which moves
    // whenever a stack is rebuilt or cleared, is what says the hints changed.
    std::function<void(const decomp::SnapshotKey& snap, std::set<uint32_t>&)> callSites;
    uint64_t callSitesRevision = 0;
};

class DecompilerPanel
{
public:
    static const char* const kTitle;          // "SH-2 Pseudocode"; the dock layout names it
    static const int kSlot = 1;               // this panel's requester id at the runner
    static const int kAutoRefreshFrames = 30; // the auto-refresh debounce
    static const size_t kCacheSize = 8;       // results kept per full key (scrub frames, entries)

    // Things App acts on after Draw.
    struct Request
    {
        bool jumpAssembly = false;   // show 'addr' on 'cpu' in SH-2 Assembly
        int cpu = 0;
        uint32_t addr = 0;
        bool viewHex = false;        // show 'hexAddr' in Memory
        uint32_t hexAddr = 0;
        bool rename = false;         // open Rename Function for 'renameAddr'
        uint32_t renameAddr = 0;
        bool boundsChanged = false;  // a function start was set in DecompilerInputs::bounds: save it
        bool settingsChanged = false;// auto-refresh toggled: save settings
        bool chooseBios = false;     // pick a BIOS image file
        bool reloadBios = false;     // read the BIOS image again from its path
        bool clearBios = false;      // stop using a BIOS image
    };

    // Every frame, whether or not the window is shown: collects results (the runner's Poll also
    // enforces the budget and the cancel deadline), recomputes the view's identity, and -- when
    // 'visible' -- captures and submits per the policy above. A hidden panel does no work.
    void Update(const DecompilerView& view, decomp::DecompilerRunner& runner, const DecompilerInputs& inputs,
                bool visible);

    // The window. User actions take effect at the next Update.
    void Draw(bool* open, const DecompilerView& view, const decomp::DecompilerRunner& runner,
              const DecompilerInputs& inputs, Request& req);

    // The window when the app has no runner (its worker could not be started): says why, and stays.
    void DrawUnavailable(bool* open, const std::string& why);

    // "Decompile" from Assembly, Call Stack and Registers: the function containing 'addr' on
    // 'cpu'. Turns Follow PC off and brings the window forward.
    void ShowFunctionContaining(int cpu, uint32_t addr);

    // The session ended (source closed or replaced, emulator reconnected). App has already bumped
    // the runner's epoch; this forgets what belonged to the old session. Shown output stays, stale.
    void ResetSession();

    int Cpu() const { return mCpu; }
    void SetCpu(int cpu);
    bool FollowPc() const { return mTarget.kind == TargetKind::FollowPc; }
    bool AutoRefresh() const { return mAutoRefresh; }
    void SetAutoRefresh(bool on) { mAutoRefresh = on; }
    // The user's volatile memory (DecompileRequest::volatileMemory), as typed: settings keep the
    // text. False, keeping the old ranges, when it does not parse.
    const std::string& VolatileMemory() const { return mVolatileText; }
    bool SetVolatileMemory(const std::string& text, std::string* error = nullptr);

    // The install decision for one polled result (called by Update; public so tests can offer a
    // result the runner would never deliver, which is how the panel's own checks are pinned
    // independently of the runner's). True when installed.
    bool Accept(decomp::DecompileResult& result, uint64_t runnerEpoch);

    // The entry box's rule: an optional 0x and one to eight hex digits; no sign, no overflow.
    static bool ParseHexAddress(const char* text, uint32_t& out);

    // ---- State, for App's status and for tests -------------------------------------------
    const decomp::DecompileResult* Shown() const { return mShownValid ? &mShown : nullptr; }
    bool ShownStale() const;                  // shown output is not the answer for the view now
    bool ShownOutdatedByView() const;         // ...because the memory on screen moved on
    bool Pending() const { return mPending.active; }
    uint64_t PendingGeneration() const { return mPending.generation; }
    const decomp::SnapshotKey* HeldKey() const { return mSnap ? &mSnap->key : nullptr; }
    uint32_t WantedEntry() const { return mEntryOk ? mEntry : 0; }   // as run: a mirror stays one
    const std::string& EntryError() const { return mEntryError; }
    // What the panel warns about the output on screen, one amber line each: flow cut off or into
    // data, the engine's flow warnings, flow into another function, an address the entry never
    // reaches, an entry the finder doubts. Empty when nothing is uncertain.
    std::vector<std::string> Uncertainties() const;
    const char* EntryText() const { return mEntryBuf; }
    // The register hint the last Draw showed for a register-target call at the halt, or "".
    const std::string& HintText() const { return mHintText; }
    // The user notes the last Draw attached to each shown line ("" for none), by line index.
    const std::vector<std::string>& LineNotes() const { return mLineNotes; }
    // ...and the notes inside the function's body whose instruction no line came from (a folded
    // literal load, a delay slot), listed under the code so they are not lost: "0x06008218: note".
    const std::vector<std::string>& UnplacedNotes() const { return mUnplacedNotes; }
    uint64_t Captures() const { return mCaptures; }
    uint64_t Submissions() const { return mSubmissions; }
    uint64_t Discarded() const { return mDiscarded; }
    uint64_t CacheHits() const { return mCacheHits; }
    // Screen rectangle of displayed line 'index' in the last Draw (tests click it). False when the
    // line was not drawn (clipped, or no output).
    bool LineRect(int index, ImVec2& min, ImVec2& max) const;
    // ...and of a named control in the last Draw: "cpu", "entry", "refresh", "stop", or a line
    // context-menu item by its label ("Set Function Start Here", "View in Memory", ...).
    bool ItemRect(const char* name, ImVec2& min, ImVec2& max) const;

private:
    enum class TargetKind : uint8_t { FollowPc, Containing, Entry };
    struct Target
    {
        TargetKind kind = TargetKind::FollowPc;
        uint32_t address = 0;   // Containing: the address asked about; Entry: the entry itself
        decomp::EntrySource source = decomp::EntrySource::User;   // Entry: why it is an entry
    };

    // The view's identity: what a snapshot must match to show what is on screen.
    struct ViewId
    {
        bool valid = false;
        uint64_t sourceId = 0, deriveSerial = 0, frameNumber = 0, biosHash = 0;
        bool operator==(const ViewId& o) const
        {
            return valid == o.valid && sourceId == o.sourceId && deriveSerial == o.deriveSerial &&
                   frameNumber == o.frameNumber && biosHash == o.biosHash;
        }
        bool operator!=(const ViewId& o) const { return !(*this == o); }
    };
    static ViewId IdOf(const DecompilerView& view);

    struct Outstanding
    {
        bool active = false;
        uint64_t generation = 0;
        uint64_t epoch = 0;
        decomp::SnapshotKey key;
        uint32_t entry = 0;
        ViewId snapId;
        uint64_t namesGeneration = 0;
        uint64_t functions = 0;      // the function set when asked
        uint64_t prototypes = 0;     // PrototypeHash for the entry when asked
        uint64_t volatileMemory = 0; // VolatileHash when asked
    };

    // What the panel last submitted (or served from the cache): the same question is never asked
    // twice in a row, so a failure is shown rather than retried every frame.
    struct Question
    {
        bool valid = false;
        decomp::SnapshotKey key;
        uint32_t entry = 0;
        uint64_t namesGeneration = 0;
        uint64_t functions = 0;      // the function set registered (engine input, like the names)
        uint64_t prototypes = 0;     // the callee signatures lent (engine input, like the names)
        uint64_t volatileMemory = 0; // the user's volatile ranges (engine input)
        bool operator==(const Question& o) const
        {
            return valid == o.valid && key == o.key && entry == o.entry && namesGeneration == o.namesGeneration &&
                   functions == o.functions && prototypes == o.prototypes && volatileMemory == o.volatileMemory;
        }
    };

    struct CacheEntry
    {
        Question question;
        decomp::DecompileResult result;
        ViewId snapId;
    };

    void SetTarget(const Target& target);
    void Resolve(const DecompilerInputs& inputs);
    void Install(decomp::DecompileResult& result, const ViewId& snapId, uint64_t namesGeneration);
    const CacheEntry* CacheFind(const Question& q);
    void CachePut(const Question& q, const decomp::DecompileResult& r, const ViewId& snapId);
    void DrawHeader(const DecompilerView& view, const decomp::DecompilerRunner& runner, Request& req);
    void DrawStatus(const DecompilerView& view, const decomp::DecompilerRunner& runner);
    void DrawLines(const DecompilerInputs& inputs, Request& req);
    // The hint for the output on screen: the followed CPU stopped on a register-target call or
    // jump in it whose target the output does not already name. Invalid otherwise.
    decomp::RegisterHint HaltHint() const;

    int mCpu = 0;
    Target mTarget;
    bool mAutoRefresh = false;
    bool mFocus = false;

    // Set by a user action (a new target, Refresh): capture now even on a running emulator.
    bool mExplicit = false;
    bool mForceSubmit = false;       // Refresh: ask again even if the question is unchanged
    bool mRetryTooLarge = false;     // Refresh: including an entry that ran out of budget
    bool mCancelRequested = false;   // Stop

    ViewId mViewId;                  // the view this frame
    DecompViewKind mViewKind = DecompViewKind::None;
    bool HeldIsOnScreen() const;
    uint32_t mViewPc[2] = { 0, 0 };
    bool mViewHavePc[2] = { false, false };
    std::shared_ptr<const decomp::MemorySnapshot> mSnap;   // the snapshot the panel holds
    ViewId mSnapId;
    uint64_t mTick = 0, mCaptureTick = 0;
    bool mAutoPcValid = false;       // the PC the last auto-refresh was for: not refreshed for again
    uint32_t mAutoPc = 0;

    // The entry wanted for mSnap, resolved once per snapshot, target and set of finder hints: a
    // function start set, a symbol added or a callee discovered can move the containing entry.
    bool mResolved = false;
    uint64_t mResolvedFunctions = 0, mResolvedCallees = 0, mResolvedCallSites = 0;
    uint64_t mCallSitesRevision = 0;
    bool mEntryOk = false;
    uint32_t mEntry = 0;             // where it runs (a mirror, if the code runs there): what is asked
    decomp::EntrySource mEntrySource = decomp::EntrySource::None;
    int mEntryConfidence = 0;
    std::string mEntryError;
    std::string mEntryDoubt;         // the finder's doubt about mEntry
    bool mHaveAnchor = false;        // the entry was found for an address (a PC, a line asked about)...
    uint32_t mAnchor = 0;            // ...this one, which the function found must reach

    uint64_t mNamesGeneration = 0;
    // The addresses the engine registers as functions: the user's function starts and the named
    // addresses. A fingerprint, not a generation, so undoing a correction finds the old answers
    // again. Both change how the engine reads a call (a function, or a branch into code), so this is
    // engine input like the names, and a finder input besides.
    uint64_t mFunctions = 0;
    uint64_t mSymbolsHash = 0, mSymbolsHashGeneration = ~0ull;   // the named addresses' part, per names generation
    void UpdateFunctionSet(const DecompilerInputs& inputs);
    // Entries found by results this session (the finder's last rung), with the snapshot whose code
    // called them: an edit, another frame or another BIOS image may have moved them, so only those
    // found in the snapshot held are hints.
    std::map<uint32_t, decomp::SnapshotKey> mCallees;
    uint64_t mCalleesGeneration = 0;   // moves when an entry is found for a snapshot
    uint64_t CalleeHash() const;       // over the callees that are hints for mSnap
    // Each function's signature from its last decompilation this session, keyed by the address it
    // ran at (a mirror's mova can change what it takes), lent to its callers' requests. Engine input: the question carries a hash of what is lent, so a caller decompiled
    // before its callee is asked again, with the callee's signature, when it is next shown.
    // A signature is a function of everything its decompilation read, so it records all of it: the
    // snapshot, the function set, and the signature it was lent for each function it calls ('deps',
    // 0 for none). Only one recovered from the snapshot held, under the function set in force, is
    // lent. And when a function's signature changes, every signature recovered from the old one is
    // dropped, transitively -- B learned before C knew C's signature is obsolete once C's is known,
    // and so is A learned from B (A1). A later decompilation that cannot vouch for a signature
    // removes the old one rather than keeping it.
    struct LearnedPrototype
    {
        decomp::SnapshotKey key;
        uint64_t functions = 0;                                // LendingId() when learned
        decomp::Prototype proto;
        uint64_t shape = 0;                                    // ShapeOf(proto), never 0
        std::vector<std::pair<uint32_t, uint64_t>> deps;       // callee -> shape lent, or 0
    };
    std::map<uint32_t, LearnedPrototype> mPrototypes;
    static uint64_t ShapeOf(const decomp::Prototype& proto);
    // The shape of the signature 'callee' would be lent under {key, functions}, or 0.
    uint64_t LentShape(uint32_t callee, const decomp::SnapshotKey& key, uint64_t functions) const;
    bool Lendable(const LearnedPrototype& p, uint32_t entry, uint32_t callee) const
    {
        return callee != entry && mSnap && p.key == mSnap->key && p.functions == LendingId();
    }
    // The engine inputs a signature was recovered under besides the snapshot: the function set and
    // the volatile memory list. A signature recovered with a mailbox folded away is not the one
    // recovered with it kept, so a change to either makes every learned one unlendable (and, through
    // 'deps', every one recovered from them).
    uint64_t LendingId() const { return mVolatileHash ? (mFunctions ^ (mVolatileHash * 0x9E3779B97F4A7C15ull)) : mFunctions; }
    void Learn(const decomp::DecompileResult& result);
    uint64_t PrototypeHash(uint32_t entry) const;   // over the prototypes lent to 'entry' now

    std::string mVolatileText;                                   // as typed and saved
    std::vector<std::pair<uint32_t, uint32_t>> mVolatile;        // ...parsed
    uint64_t mVolatileHash = 0;
    char mVolatileEdit[2048] = {};                               // the popup's edit box
    std::string mVolatileError;
    Question mAsked;
    Outstanding mPending;
    std::string mNote;               // "cancelled", ... (the last request's fate when not installed)

    decomp::DecompileResult mShown;
    bool mShownValid = false;
    ViewId mShownSnapId;
    uint64_t mShownNamesGeneration = 0;
    uint64_t mShownVolatileHash = 0;   // the volatile list the shown answer was asked under
    std::string mShownEntrySource;
    float mShownWidth = 0.0f;        // widest line, for the horizontal scroll range
    int mSelectedLine = -1;

    std::list<CacheEntry> mCache;    // most recent first

    char mEntryBuf[16] = {};
    // A new function was installed: the entry box shows its entry once it is not being edited, so
    // it never names a function other than the one on screen (Follow PC, Call Stack, Registers).
    bool mSyncEntryBox = false;
    bool mEntryActive = false;       // the box had keyboard focus last frame
    int mContextLine = -1;           // the line the context menu was opened on
    decomp::RegisterHint mHint;      // this Draw's HaltHint, and its text
    std::string mHintText;
    std::vector<std::string> mLineNotes;   // this Draw's user notes per shown line
    std::vector<std::string> mUnplacedNotes;

    struct DrawnLine { int index; ImVec2 min, max; };
    std::vector<DrawnLine> mDrawn;
    struct DrawnItem { std::string name; ImVec2 min, max; };
    std::vector<DrawnItem> mItems;
    void Remember(const char* name);

    uint64_t mCaptures = 0, mSubmissions = 0, mDiscarded = 0, mCacheHits = 0;
};

}  // namespace sfe
