// Area 1 of the review program (Docs/Decompiler/REVIEW_PROGRAM.md): snapshot, cache and
// engine-input invalidation, as a model test.
//
// A seeded RNG drives a sequence of 20 to 60 user and source operations against the real
// DecompilerPanel, the real DecompilerRunner and the real engine, headless (the panel's Refresh and
// Stop buttons are clicked through ImGuiHarness, exactly as DecompilerPanelTests does):
//
//   edit a byte in a function body or in a callee (changing its signature, or the callee a caller
//   calls), scrub to another recorded revision and back, Refresh, reconnect (same content, new
//   source, session ended), replace the source (another image), BIOS present / absent / another /
//   reloaded, the volatile list, a function's name or start, select another function, Stop, hide
//   and show the panel, let the emulator run and pause. A step may hold the engine so that several
//   changes land while a request is in flight and the answers of superseded requests come back, or
//   let it finish and change an input before the panel collects the answer. In some seeds a request
//   is held past a short time budget, so that the runner answers "too large".
//
// The model is a from-scratch Sh2Decompiler::Decompile of the current inputs, on this thread while
// the runner is idle: the snapshot (bytes, BIOS, the key the app would give it), the names, the
// user's function starts, the volatile list, and the callees' prototypes as they would be lent under
// those inputs (a callee's prototype is the one its own from-scratch decompilation yields, itself
// lending its callees'). The model never reads the panel's caches; it reads the panel only through
// its public interface (Shown, ShownStale, HeldKey, Pending).
//
// Checked after every step once the runner is idle, and for every result as it is installed:
//   - the shown result's key is the key of the inputs it was asked under, and those are the inputs
//     of the moment it was installed (so a result for a superseded request is never installed);
//   - its entry, status and text equal the model's for those inputs, lending a subset of the
//     current callee prototypes (the panel lends what it has learned; a prototype from a previous
//     revision of a callee matches no subset);
//   - the panel says "stale" exactly when the inputs shown differ from the inputs now (a result
//     shown as current but outdated is the P1 case; a result the panel never refreshes is a miss);
//   - the snapshot the panel holds is the one the policy of DecompilerPanel.h says;
//   - a result made from other inputs than the ones now says stale, even while the engine is held;
//   - Refresh asks the runner again; a request Stop cancelled in the engine installs nothing;
//   - "too large" is accepted for the request held past the budget, and for the runner's memo of it
//     while the bytes it runs are unchanged and nothing has decompiled it since (A1-1, A1-2 are the
//     seeds and shrunk sequences where it is not);
//   - at the end of a sequence a callee-first walk through every function leaves each one showing
//     exactly the from-scratch answer, with every callee prototype lent.
//
// usage: SaturnExplorerDecompilerModelTests <scratch dir>
//   SE_MODEL_SEEDS=<n>   run seeds 1..n (default: a fixed set, see kDefaultSeeds)
//   SE_MODEL_SEED=<k>    run seed k alone, every step printed
//   SE_MODEL_SKIP=3,5    (with SE_MODEL_SEED) drop those operations of the sequence, to shrink by hand
//   SE_MODEL_STEPS=<n>   (with SE_MODEL_SEED) run only the first n operations
//   SE_MODEL_TEXT=1      (with SE_MODEL_SEED) also print the shown text after every step
//   SE_MODEL_SCRIPT="select D; starve; edit C 2 1" SE_MODEL_WORLD=D,C,A SE_MODEL_IMAGE=0
//                        one written-out sequence, see ParseScript for the words
//   SE_MODEL_LIST=1      print the generated sequence only, run nothing
//   SE_MODEL_NOSTARVE=1  leave out the operations that stall a request past the time budget
#include "DecompilerTestSupport.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"

#include "DecompilerPanel.h"
#include "Debug/MemoryBackend.h"
#include "ImGuiHarness.h"
#include "Theme.h"

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>

using namespace sfe;
using namespace sfe::decomp;

namespace
{

std::string gConfig;

// ---- Small things -----------------------------------------------------------------------------

struct Rng
{
    uint64_t s;
    // The seed goes through the mixer first: splitmix64's state steps by a constant, so consecutive raw
    // seeds would give one stream shifted by a place each.
    explicit Rng(uint64_t seed) : s(0) { s = seed + 0x1234567ull; s = Next() ^ 0xA5A5A5A55A5A5A5Aull; for (int i = 0; i < 4; ++i) Next(); }
    uint64_t Next()
    {   // splitmix64: the same stream on every platform (std distributions are not)
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int Below(int n) { return n <= 0 ? 0 : static_cast<int>(Next() % static_cast<uint64_t>(n)); }
    bool Chance(int percent) { return Below(100) < percent; }
};

uint64_t Fnv(const void* data, size_t n, uint64_t h = 1469598103934665603ull)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
uint64_t FnvU64(uint64_t v, uint64_t h) { return Fnv(&v, sizeof v, h); }

std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

bool Has(const std::string& text, const char* what) { return text.find(what) != std::string::npos; }

const uint32_t kLw = 0x00200000u, kHw = 0x06000000u, kSize = 0x00100000u;

// ---- The world: a few functions in work RAM ----------------------------------------------------
//
// Every function is preceded by `rts ; nop` (so Containing-the-entry resolves to the entry) and
// built from instructions Capstone-checked in Fixture 1 (callers) or plain loads, stores and a poll.
// 'sites' are the bytes an edit may rewrite, each with the values it may take; every combination is
// still a valid function. A: reads *r4, returns it + 2. B: stores r4 through r5, returns nothing.
// P: writes 3 to a mailbox word and polls it until bit 0 clears (a poll the engine can only keep if the
// mailbox is volatile: otherwise it reads back its own 3, the loop is endless as analysed and the read
// of *r5 after it is gone), then returns *r5. Q: stores r4 in
// a mailbox word and returns what it reads back (the stored value, unless the mailbox is volatile).
// C, D, E call A/B/P/Q, C, and a BIOS function through a literal pool word.

enum Fn { FnA, FnB, FnP, FnC, FnD, FnE, FnQ, kFns };

struct Site { uint32_t addr; std::vector<uint8_t> vals; };
struct FnDef
{
    const char* label;
    uint32_t entry;
    std::vector<uint16_t> words;
    uint32_t poolAt, poolVal;   // 0: none
    std::vector<Site> sites;
};

const uint32_t kBiosFn = 0x00000700u;   // the function E calls, when a BIOS image is mapped

const std::vector<uint16_t> kCaller = { 0x2FE6, 0x4F22, 0x6E43, 0xE000, 0x4415, 0x8B03, 0xD003,
                                        0x400B, 0x64E3, 0x7001, 0x4F26, 0x000B, 0x6EF6, 0x0009 };

const FnDef* Defs()
{
    static const FnDef defs[kFns] = {
        { "A", 0x06005000, { 0x6042, 0x000B, 0x7002 }, 0, 0,
          { { 0x06005005, { 0x02, 0x03, 0x01 } }, { 0x06005001, { 0x42, 0x52, 0x62 } } } },
        { "B", 0x06005100, { 0x2542, 0x000B, 0x0009 }, 0, 0,
          { { 0x06005101, { 0x42, 0x62, 0x52 } }, { 0x06005100, { 0x25, 0x26 } } } },
        { "P", 0x06005200, { 0xD104, 0xE003, 0x2102, 0x6012, 0xC801, 0x8BFC, 0x6052, 0x000B, 0x0009, 0x0009 }, 0x06005214, 0x06003000,
          { { 0x06005216, { 0x30, 0x31, 0x32 } }, { 0x06005207, { 0x12, 0x22 } }, { 0x0600520D, { 0x52, 0x62 } },
            { 0x06005209, { 0x01, 0x04 } } } },
        { "C", 0x06004000, kCaller, 0x0600401C, 0x06005000,
          { { 0x0600401E, { 0x50, 0x51, 0x52, 0x53 } }, { 0x06004007, { 0x00, 0x05 } }, { 0x06004013, { 0x01, 0x02 } } } },
        { "D", 0x06004200, kCaller, 0x0600421C, 0x06004000,
          { { 0x0600421E, { 0x40, 0x50, 0x51 } }, { 0x06004207, { 0x00, 0x05 } } } },
        { "E", 0x06004400, kCaller, 0x0600441C, 0x00000700,
          { { 0x0600441E, { 0x07, 0x06 } }, { 0x06004407, { 0x00, 0x05 } } } },
        { "Q", 0x06005300, { 0xD103, 0x2142, 0x6012, 0x000B, 0x0009, 0x0009, 0x0009, 0x0009 }, 0x06005310, 0x06003000,
          { { 0x06005312, { 0x30, 0x31 } }, { 0x06005303, { 0x42, 0x52 } } } },
    };
    return defs;
}

// Source images: game 0 is the defaults above; games 1 and 2 are other builds of the same code.
struct Preset { uint32_t addr; uint8_t val; };
const std::vector<Preset>& GamePreset(int game)
{
    static const std::vector<Preset> none;
    static const std::vector<Preset> g1 = { { 0x06005005, 0x03 }, { 0x06005001, 0x52 }, { 0x0600401E, 0x51 },
                                            { 0x06005212, 0x31 }, { 0x0600421E, 0x50 } };
    static const std::vector<Preset> g2 = { { 0x06005101, 0x62 }, { 0x0600401E, 0x52 }, { 0x0600421E, 0x51 },
                                            { 0x06005001, 0x62 }, { 0x06004407, 0x05 } };
    return game == 1 ? g1 : game == 2 ? g2 : none;
}

// The BIOS images: 4 KiB each, a function at 0x700 (A's shape, other constants).
std::vector<uint8_t> BiosBytes(int id)
{
    std::vector<uint8_t> b(0x1000, 0);
    dts::Put16(b, 0x6FC, 0x000B);
    dts::Put16(b, 0x6FE, 0x0009);
    const uint16_t w[3] = { id == 1 ? uint16_t(0x6042) : uint16_t(0x6052), 0x000B, id == 1 ? uint16_t(0x7003) : uint16_t(0x7005) };
    for (int i = 0; i < 3; ++i) dts::Put16(b, 0x700 + 2 * i, w[i]);
    return b;
}

struct Content
{
    std::vector<uint8_t> bytes;   // HWRAM, 1 MiB
    uint64_t hash = 0;
};
using ContentP = std::shared_ptr<const Content>;

ContentP MakeContent(std::vector<uint8_t>&& bytes)
{
    auto c = std::make_shared<Content>();
    c->hash = Fnv(bytes.data(), bytes.size());
    c->bytes = std::move(bytes);
    return c;
}

ContentP BuildImage(const std::vector<int>& placed, int game, const std::vector<Preset>& overlay = {})
{
    std::vector<uint8_t> hw(kSize, 0);
    for (int f : placed)
    {
        const FnDef& d = Defs()[f];
        dts::Put16(hw, d.entry - 4 - kHw, 0x000B);
        dts::Put16(hw, d.entry - 2 - kHw, 0x0009);
        size_t off = d.entry - kHw;
        for (uint16_t w : d.words) { dts::Put16(hw, off, w); off += 2; }
        if (d.poolAt) dts::Put32(hw, d.poolAt - kHw, d.poolVal);
    }
    auto inWorld = [&](uint32_t a) {
        for (int f : placed)
            if (a >= Defs()[f].entry - 4 && a < Defs()[f].entry + 0x40) return true;
        return false;
    };
    for (const Preset& p : GamePreset(game))
        if (inWorld(p.addr)) hw[p.addr - kHw] = p.val;
    for (const Preset& p : overlay)
        if (inWorld(p.addr)) hw[p.addr - kHw] = p.val;
    return MakeContent(std::move(hw));
}

// ---- Backend, gate -----------------------------------------------------------------------------

class ContentBackend : public IMemoryBackend
{
public:
    ContentP content;
    uint64_t source = 1;
    std::vector<uint8_t> lw = std::vector<uint8_t>(kSize, 0);

    bool Connected() const override { return true; }
    uint64_t SourceId() const override { return source; }
    std::vector<MemoryReadResult> ReadMemoryBatch(const std::vector<MemoryReadRequest>& rs) override
    {
        std::vector<MemoryReadResult> out;
        for (const MemoryReadRequest& r : rs)
        {
            MemoryReadResult res;
            const uint32_t c = r.address & 0x07FFFFFFu;
            const uint8_t* p = nullptr;
            if (content && c >= kHw && c - kHw + r.size <= kSize) p = content->bytes.data() + (c - kHw);
            else if (c >= kLw && c - kLw + r.size <= kSize) p = lw.data() + (c - kLw);
            if (p)
            {
                res.success = true;
                res.bytes.assign(p, p + r.size);
            }
            out.push_back(std::move(res));
        }
        return out;
    }
};

// Holds the engine at the start of each pass while closed.
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
    bool WaitHeld(double seconds = 30)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        return mCv.wait_for(lock, std::chrono::duration<double>(seconds), [this] { return mHeld > 0; });
    }
    int Held()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mHeld;
    }

private:
    std::mutex mMutex;
    std::condition_variable mCv;
    bool mClosed = false;
    int mHeld = 0;
};

// ---- Inputs: what an answer depends on ---------------------------------------------------------

using BiosP = std::shared_ptr<const BiosImage>;

// The inputs one question is asked under. 'source/serial/frame/biosHash' are the snapshot's identity
// (the key the app gives it); the content hash stands for the bytes.
struct Sig
{
    bool valid = false;
    ContentP content;
    BiosP bios;
    uint64_t source = 0, serial = 0, frame = 0;
    std::map<uint32_t, std::string> names;
    std::set<uint32_t> bounds;
    std::string volatileText;
    uint32_t entry = 0;

    uint64_t BiosHash() const { return bios && !bios->bytes.empty() ? bios->contentHash : 0; }
    bool SameView(const Sig& o) const
    {
        return source == o.source && serial == o.serial && frame == o.frame && BiosHash() == o.BiosHash();
    }
    bool SameOther(const Sig& o) const
    {
        return names == o.names && bounds == o.bounds && volatileText == o.volatileText && entry == o.entry;
    }
    bool Same(const Sig& o) const { return SameView(o) && SameOther(o); }
};

std::string DescribeSig(const Sig& s)
{
    std::ostringstream o;
    o << "snapshot {source " << s.source << ", serial " << s.serial << ", frame " << s.frame << ", bios "
      << Hex(static_cast<uint32_t>(s.BiosHash())) << ", bytes " << Hex(static_cast<uint32_t>(s.content ? s.content->hash : 0))
      << "}, entry " << Hex(s.entry) << ", names {";
    for (const auto& n : s.names) o << Hex(n.first) << "=" << n.second << " ";
    o << "}, starts {";
    for (uint32_t b : s.bounds) o << Hex(b) << " ";
    o << "}, volatile '" << s.volatileText << "'";
    return o.str();
}

// ---- The oracle: a from-scratch decompilation --------------------------------------------------

typedef std::vector<std::pair<uint32_t, Prototype>> Lent;

bool SameAnswer(const DecompileResult& a, const DecompileResult& b, std::string* why = nullptr)
{
    auto diff = [&](const char* what) {
        if (why) *why = what;
        return false;
    };
    if (a.status != b.status) return diff("status");
    if (a.error != b.error) return diff("error");
    if (a.entry != b.entry) return diff("canonical entry");
    if (a.requestedEntry != b.requestedEntry) return diff("requested entry");
    if (a.name != b.name) return diff("name");
    if (a.Text() != b.Text()) return diff("text");
    if (a.warnings != b.warnings) return diff("warnings");
    if (a.calledAt != b.calledAt) return diff("calledAt");
    if (a.prototype != b.prototype) return diff("prototype");
    if (a.prototypeWithheld != b.prototypeWithheld) return diff("prototypeWithheld");
    return true;
}

class Oracle
{
public:
    uint64_t runs = 0, memoHits = 0;

    // The engine, built lazily and used only while the runner is idle (checked by the caller).
    const DecompileResult& Run(const Sig& s, uint32_t entry, const Lent& lent)
    {
        const uint64_t key = MemoKey(s, entry);
        std::list<Memo>& bucket = mMemo[key];
        for (Memo& m : bucket)
        {
            if (m.lent.size() != lent.size()) continue;
            bool same = true;
            for (size_t i = 0; i < lent.size() && same; ++i)
                same = m.lent[i].first == lent[i].first && m.lent[i].second == lent[i].second;
            if (same)
            {
                ++memoHits;
                return m.result;
            }
        }
        if (!mEngine) mEngine.reset(new Sh2Decompiler(gConfig));
        DecompileRequest rq;
        rq.entry = entry;
        for (const auto& n : s.names) rq.names.push_back(n);
        rq.knownFunctions.assign(s.bounds.begin(), s.bounds.end());
        rq.prototypes = lent;
        std::string err;
        ParseAddressRanges(s.volatileText, rq.volatileMemory, err);
        ++runs;
        Memo m;
        m.lent = lent;
        m.result = mEngine->Decompile(Snapshot(s), rq);
        bucket.push_back(std::move(m));
        return bucket.back().result;
    }

    // The prototype 'callee' lends under inputs 's' when the callees it calls are lent theirs
    // (recursively): null when its decompilation does not vouch for one.
    const Prototype* Full(const Sig& s, uint32_t callee, int depth = 0)
    {
        if (depth > 6) return nullptr;
        const DecompileResult& bare = Run(s, callee, Lent());
        Lent lent;
        for (uint32_t c : bare.calledAt)
            if (c != callee)
                if (const Prototype* p = Full(s, c, depth + 1)) lent.emplace_back(c, *p);
        const DecompileResult& r = lent.empty() ? bare : Run(s, callee, lent);
        return r.status == DecompileStatus::Ok && r.prototype.known ? &r.prototype : nullptr;
    }

    // Every answer the panel may legitimately show for s.entry: it lends the prototypes it has
    // learned, which is any subset of the current ones.
    std::vector<const DecompileResult*> Candidates(const Sig& s, Lent* allLendable = nullptr)
    {
        std::vector<const DecompileResult*> out;
        const DecompileResult& bare = Run(s, s.entry, Lent());
        Lent lendable;
        for (uint32_t c : bare.calledAt)
            if (c != s.entry)
                if (const Prototype* p = Full(s, c)) lendable.emplace_back(c, *p);
        if (allLendable) *allLendable = lendable;
        const size_t n = std::min<size_t>(lendable.size(), 4);
        for (size_t mask = 0; mask < (size_t(1) << n); ++mask)
        {
            Lent subset;
            for (size_t i = 0; i < n; ++i)
                if (mask & (size_t(1) << i)) subset.push_back(lendable[i]);
            out.push_back(&Run(s, s.entry, subset));
        }
        return out;
    }

    // The answer with every callee prototype lent (what a callee-first walk converges to).
    const DecompileResult& Converged(const Sig& s)
    {
        Lent lendable;
        Candidates(s, &lendable);
        return Run(s, s.entry, lendable);
    }

    SnapshotKey ExpectedKey(const Sig& s)
    {
        SnapshotKey k;
        k.sourceId = s.source;
        k.deriveSerial = s.serial;
        k.frameNumber = s.frame;
        k.regionSetHash = Snapshot(s)->key.regionSetHash;
        return k;
    }

private:
    struct Memo
    {
        Lent lent;
        DecompileResult result;
    };

    static uint64_t MemoKey(const Sig& s, uint32_t entry)
    {
        uint64_t h = FnvU64(s.content ? s.content->hash : 0, 1469598103934665603ull);
        h = FnvU64(s.BiosHash(), h);
        for (const auto& n : s.names)
        {
            h = FnvU64(n.first, h);
            h = Fnv(n.second.data(), n.second.size(), h);
        }
        h = FnvU64(~0ull, h);
        for (uint32_t b : s.bounds) h = FnvU64(b, h);
        h = Fnv(s.volatileText.data(), s.volatileText.size(), h);
        return FnvU64(entry, h);
    }

    std::shared_ptr<const MemorySnapshot> Snapshot(const Sig& s)
    {
        ContentBackend b;
        b.content = s.content;
        b.source = s.source;
        se_sh2_regs regs[2] = {};
        const bool have[2] = { false, false };
        return MemorySnapshot::Capture(b, regs, have, s.source, s.serial, s.frame, s.bios);
    }

    std::unique_ptr<Sh2Decompiler> mEngine;
    std::map<uint64_t, std::list<Memo>> mMemo;
};

// What a run covered, for the line at the end.
struct Stats
{
    uint64_t seeds = 0, ops = 0, noEffect = 0, holds = 0, races = 0, stops = 0, starves = 0;
    uint64_t installs = 0, idleChecks = 0, heldChecks = 0, walks = 0;
    uint64_t submissions = 0, discarded = 0, cacheHits = 0, captures = 0;
} gStats;

// ---- Operations --------------------------------------------------------------------------------

enum class OpKind
{
    Edit, Scrub, Resume, Refresh, Reconnect, Replace, Bios, Volatile, Rename, Bound, Select, Stop, Cpu,
    Run, Pause, Advance, Hide, Show, Hold, Wait, Finish, Starve
};

struct Op
{
    OpKind kind = OpKind::Wait;
    int a = 0, b = 0, c = 0;
    std::string text;
};

const char* const kVolatileOptions[] = { "", "06003000-0600300F", "06003100-0600310F", "26003000-2600300F, 06003200" };
const char* const kNameOptions[] = { "", "alpha", "beta", "gamma" };

std::string Describe(const Op& o, const std::vector<int>& placed)
{
    char b[160];
    auto fnName = [&](int i) { return Defs()[placed[static_cast<size_t>(i) % placed.size()]].label; };
    switch (o.kind)
    {
    case OpKind::Edit:
    {
        const FnDef& d = Defs()[placed[static_cast<size_t>(o.a) % placed.size()]];
        const Site& s = d.sites[static_cast<size_t>(o.b) % d.sites.size()];
        std::snprintf(b, sizeof b, "edit %s byte %08X := %02X", d.label, s.addr, s.vals[static_cast<size_t>(o.c) % s.vals.size()]);
        return b;
    }
    case OpKind::Scrub: std::snprintf(b, sizeof b, "scrub to recorded frame %d", o.a); return b;
    case OpKind::Resume: return "resume (leave the scrubbed frame)";
    case OpKind::Refresh: return "Refresh";
    case OpKind::Reconnect: return o.a ? "reconnect (same content, new source, counters restart)" : "reconnect (same content, new source)";
    case OpKind::Replace:
        std::snprintf(b, sizeof b, "replace the source with image %d (%s)", o.a, o.b == 0 ? "dump" : o.b == 1 ? "paused" : "running");
        return b;
    case OpKind::Bios: std::snprintf(b, sizeof b, "BIOS image: %s", o.a == 0 ? "none" : o.a == 1 ? "A" : "B"); return b;
    case OpKind::Volatile: std::snprintf(b, sizeof b, "volatile list '%s'", kVolatileOptions[o.a]); return b;
    case OpKind::Rename:
        std::snprintf(b, sizeof b, "name %s (%s) := '%s'", fnName(o.a), "entry", kNameOptions[o.b]);
        return b;
    case OpKind::Bound: std::snprintf(b, sizeof b, "toggle function start at %s%s", fnName(o.a), o.b ? "+4" : ""); return b;
    case OpKind::Select: std::snprintf(b, sizeof b, "select %s", o.a < 0 ? "the BIOS function" : fnName(o.a)); return b;
    case OpKind::Stop: return "Stop";
    case OpKind::Cpu: std::snprintf(b, sizeof b, "CPU %d", o.a); return b;
    case OpKind::Run: return "emulator runs";
    case OpKind::Pause: return "emulator pauses";
    case OpKind::Advance: return "emulator advances a frame";
    case OpKind::Hide: return "hide the panel";
    case OpKind::Show: return "show the panel";
    case OpKind::Hold: return "hold the engine";
    case OpKind::Wait: return "release the engine and wait for the results";
    case OpKind::Finish: return "let the engine finish, delivering nothing yet";
    case OpKind::Starve: return "Refresh, and stall the engine past the time budget";
    }
    return "?";
}

struct Program
{
    std::vector<int> placed;
    int game = 0;
    bool starve = false;   // a short time budget, and operations that exceed it
    std::vector<Op> ops;
};

// ---- Scripts: a sequence written out, for a shrunk finding ---------------------------------------
//
// SE_MODEL_SCRIPT="select D; starve; edit C 2 1"  with  SE_MODEL_WORLD=D,C,A  and  SE_MODEL_IMAGE=0
// Words: select <fn|X>, edit <fn> <site> <value>, starve, refresh, stop, scrub <0-2>, resume,
// reconnect [1], replace <image> <0 dump|1 paused|2 running>, bios <0 none|1 A|2 B>, volatile <0-3>,
// name <fn> <0-3>, bound <fn> <0|1>, cpu <0|1>, run, pause, advance, hide, show, hold, wait,
// finish. <fn> is a letter of the world (A B P C D E Q); X is the BIOS function.
bool ParseScript(const std::string& script, const std::string& world, int image, Program& out, std::string& error)
{
    static const char* const letters = "ABPCDEQ";
    out = Program();
    out.game = image;
    for (char ch : world)
    {
        const char* at = ch ? std::strchr(letters, ch) : nullptr;
        if (at) out.placed.push_back(static_cast<int>(at - letters));
    }
    if (out.placed.empty())
    {
        error = "SE_MODEL_WORLD names no function";
        return false;
    }
    auto pos = [&](const std::string& w, int& v) {
        if (w == "X") { v = -1; return true; }
        const char* at = w.size() == 1 ? std::strchr(letters, w[0]) : nullptr;
        if (!at) return false;
        for (size_t i = 0; i < out.placed.size(); ++i)
            if (out.placed[i] == at - letters) { v = static_cast<int>(i); return true; }
        return false;
    };
    std::istringstream all(script);
    std::string stmt;
    while (std::getline(all, stmt, ';'))
    {
        std::istringstream words(stmt);
        std::string verb;
        if (!(words >> verb)) continue;
        std::vector<std::string> arg;
        for (std::string w; words >> w;) arg.push_back(w);
        Op o;
        auto num = [&](size_t i, int& v) { if (i >= arg.size()) return false; v = std::atoi(arg[i].c_str()); return true; };
        bool ok = true;
        if (verb == "select") { o.kind = OpKind::Select; ok = arg.size() == 1 && pos(arg[0], o.a); }
        else if (verb == "edit") { o.kind = OpKind::Edit; ok = arg.size() == 3 && pos(arg[0], o.a) && num(1, o.b) && num(2, o.c); }
        else if (verb == "starve") { o.kind = OpKind::Starve; out.starve = true; }
        else if (verb == "refresh") o.kind = OpKind::Refresh;
        else if (verb == "stop") o.kind = OpKind::Stop;
        else if (verb == "scrub") { o.kind = OpKind::Scrub; ok = num(0, o.a); }
        else if (verb == "resume") o.kind = OpKind::Resume;
        else if (verb == "reconnect") { o.kind = OpKind::Reconnect; num(0, o.a); }
        else if (verb == "replace") { o.kind = OpKind::Replace; ok = num(0, o.a) && num(1, o.b); }
        else if (verb == "bios") { o.kind = OpKind::Bios; ok = num(0, o.a); }
        else if (verb == "volatile") { o.kind = OpKind::Volatile; ok = num(0, o.a); }
        else if (verb == "name") { o.kind = OpKind::Rename; ok = arg.size() == 2 && pos(arg[0], o.a) && num(1, o.b); }
        else if (verb == "bound") { o.kind = OpKind::Bound; ok = arg.size() == 2 && pos(arg[0], o.a) && num(1, o.b); }
        else if (verb == "cpu") { o.kind = OpKind::Cpu; ok = num(0, o.a); }
        else if (verb == "run") o.kind = OpKind::Run;
        else if (verb == "pause") o.kind = OpKind::Pause;
        else if (verb == "advance") o.kind = OpKind::Advance;
        else if (verb == "hide") o.kind = OpKind::Hide;
        else if (verb == "show") o.kind = OpKind::Show;
        else if (verb == "hold") o.kind = OpKind::Hold;
        else if (verb == "wait") o.kind = OpKind::Wait;
        else if (verb == "finish") o.kind = OpKind::Finish;
        else ok = false;
        if (!ok)
        {
            error = "cannot parse '" + stmt + "'";
            return false;
        }
        out.ops.push_back(o);
    }
    return true;
}

// Short sequences aimed at what a lent prototype, a cached answer or an in-flight request depends on.
// Each is written for a world that has the functions it names (the others are left out of a world
// that lacks them). X is the BIOS function.
const char* const kMotifs[] = {
    // the volatile list decides what the poll in P is, and so what C is told about it
    "edit C 0 2; edit P 0 1; volatile 2; select P; select C; volatile 0; select C; select P; select C",
    "edit C 0 2; volatile 1; select P; select C; volatile 0; select C; volatile 1; select C; select P; select C",
    // a callee edited between the callee and its caller
    "select A; select C; edit A 1 1; select C; select A; select C",
    "select A; select C; edit C 0 1; select C; select A; select C",
    // down a chain
    "select A; select C; select D; edit A 1 2; select D; select C; select D",
    // a BIOS function, then another image
    "bios 1; select X; select E; bios 2; select E; select X; select E",
    "bios 1; select X; select E; bios 0; select E",
    // the function set
    "select A; select C; bound A 1; select C; select A; select C",
    "select A; select C; name A 1; select C; select A; select C",
    // other frames and sources
    "select A; select C; scrub 1; select A; select C; resume; select C",
    "select A; select C; reconnect; select C; select A; select C",
    "select A; select C; replace 1 1; select C; select A; select C",
    // a running emulator: the held snapshot lags, then catches up
    "run; select A; scrub 1; resume; select A; advance; refresh",
    "run; select C; edit C 1 1; refresh; edit A 0 1; select A; bios 1; select C",
    // the prototype of a function that keeps a mailbox read, lent to its caller, under another volatile list
    "edit C 0 3; select Q; select C; volatile 1; select Q; select C; volatile 0; select C",
    "edit C 0 3; volatile 1; select Q; select C; volatile 0; select C; select Q; select C",
    // an answer finishes, and an input changes before the panel collects it
    "name A 1; select A; hold; refresh; finish; name A 2; wait",
    "name A 1; select C; hold; refresh; finish; name A 2; wait",
    "select P; volatile 0; hold; refresh; finish; volatile 1; wait",
    "select Q; volatile 1; hold; refresh; finish; volatile 0; wait",
    "hold; select A; finish; name A 2; wait",
    "hold; edit A 0 1; finish; volatile 1; wait",
    "hold; select C; finish; bound C 1; wait",
    "hold; refresh; finish; edit C 1 1; wait",
    "hold; select C; finish; edit A 1 2; wait",
    "hold; select C; finish; scrub 2; wait",
    "hold; select C; finish; bios 2; wait",
    "hold; select A; finish; reconnect; wait",
    // the panel hidden while the view moves and an answer lands
    "hold; refresh; hide; edit A 0 1; finish; edit A 0 2; wait; show",
    // Stop with the request held in the engine (it cannot finish first), and with the answer already done
    "hold; select A; stop; wait; select A",
    "hold; refresh; stop; wait",
    "select A; hold; select C; stop; edit A 0 1; wait",
    "hold; select C; finish; stop; wait; select C",
};

Program Generate(uint64_t seed)
{
    Rng rng(seed);
    Program p;
    const int n = 3 + rng.Below(3);   // 3 to 5 functions
    // A is always there (everything calls it); the rest by size.
    static const int extras[] = { FnP, FnB, FnQ, FnP, FnQ };
    const int extra = extras[rng.Below(5)];
    if (n == 3) p.placed = { FnC, FnA, extra };
    else if (n == 4) p.placed = { FnD, FnC, FnA, extra };
    else p.placed = { FnD, FnC, FnA, FnE, extra };
    p.game = rng.Below(3);
    p.starve = seed % 6 == 0;

    struct W { OpKind k; int w; };
    static const W weights[] = {
        { OpKind::Edit, 16 }, { OpKind::Scrub, 8 }, { OpKind::Resume, 5 }, { OpKind::Refresh, 8 }, { OpKind::Reconnect, 4 },
        { OpKind::Replace, 3 }, { OpKind::Bios, 6 }, { OpKind::Volatile, 6 }, { OpKind::Rename, 8 }, { OpKind::Bound, 6 },
        { OpKind::Select, 16 }, { OpKind::Stop, 4 }, { OpKind::Cpu, 1 }, { OpKind::Run, 6 }, { OpKind::Pause, 3 },
        { OpKind::Advance, 4 }, { OpKind::Hide, 2 }, { OpKind::Show, 2 },
    };
    int total = 0;
    for (const W& w : weights) total += w.w;
    auto pick = [&] {
        int r = rng.Below(total);
        for (const W& w : weights)
        {
            if (r < w.w) return w.k;
            r -= w.w;
        }
        return OpKind::Edit;
    };
    const int count = 20 + rng.Below(41);   // 20 to 60 operations
    const bool hasBiosFn = std::find(p.placed.begin(), p.placed.end(), int(FnE)) != p.placed.end();
    auto fill = [&](Op& o) {
        o.a = rng.Below(1000);
        o.b = rng.Below(1000);
        o.c = rng.Below(1000);
        switch (o.kind)
        {
        case OpKind::Scrub: o.a = rng.Below(3); break;
        case OpKind::Reconnect: o.a = rng.Below(2); break;
        case OpKind::Replace: o.a = rng.Below(3); o.b = rng.Below(3); break;
        case OpKind::Bios: o.a = rng.Below(3); break;
        case OpKind::Volatile: o.a = rng.Below(4); break;
        case OpKind::Rename: o.b = rng.Below(4); break;
        case OpKind::Bound: o.b = rng.Below(2); break;
        case OpKind::Select: o.a = hasBiosFn && rng.Chance(12) ? -1 : o.a; break;
        case OpKind::Cpu: o.a = rng.Below(2); break;
        default: break;
        }
    };
    auto make = [&](OpKind k) {
        Op o;
        o.kind = k;
        fill(o);
        return o;
    };
    // Operations that change an input without drawing a frame: the ones that can land between an
    // answer finishing and the panel collecting it.
    static const OpKind racing[] = { OpKind::Edit, OpKind::Edit, OpKind::Volatile, OpKind::Rename, OpKind::Bound,
                                     OpKind::Bios, OpKind::Rename, OpKind::Edit, OpKind::Bound, OpKind::Volatile,
                                     OpKind::Scrub, OpKind::Reconnect, OpKind::Advance, OpKind::Hide };
    // Operations that change what a lent prototype or a cached answer depends on.
    static const OpKind mutators[] = { OpKind::Edit, OpKind::Edit, OpKind::Edit, OpKind::Volatile, OpKind::Rename,
                                       OpKind::Bound, OpKind::Bios, OpKind::Scrub, OpKind::Reconnect, OpKind::Replace,
                                       OpKind::Advance, OpKind::Refresh, OpKind::Resume };
    bool held = false;
    int starves = p.starve ? 1 + rng.Below(2) : 0;
    int emitted = 0, showIn = 0;
    std::string letters;
    for (int f : p.placed) letters += "ABPCDEQ"[f];
    while (emitted < count)
    {
        if (!held && rng.Chance(8))
        {
            Program m;
            std::string error;
            const char* motif = kMotifs[rng.Below(static_cast<int>(sizeof kMotifs / sizeof kMotifs[0]))];
            if (ParseScript(motif, letters, 0, m, error))
            {
                for (const Op& o : m.ops) p.ops.push_back(o);
                emitted += static_cast<int>(m.ops.size());
                continue;
            }
        }
        if (!held && rng.Chance(22))
        {
            p.ops.push_back(make(OpKind::Hold));
            held = true;
        }
        if (rng.Chance(10))
        {
            // The lending motif: a callee, its caller, a change, the caller again, then the callee
            // and the caller once more -- a prototype learned before the change must not outlive it.
            const int callee = rng.Below(1000), caller = rng.Below(1000);
            Op c1 = make(OpKind::Select), c2 = make(OpKind::Select), m = make(mutators[rng.Below(13)]);
            c1.a = callee;
            c2.a = caller;
            p.ops.push_back(c1);
            p.ops.push_back(c2);
            p.ops.push_back(m);
            p.ops.push_back(c2);
            if (rng.Chance(60)) { p.ops.push_back(c1); p.ops.push_back(c2); }
            emitted += 5;
        }
        else
        {
            const Op o = make(pick());
            p.ops.push_back(o);
            ++emitted;
            // A hidden panel is shown again soon: while it is hidden nothing can be selected or refreshed.
            if (o.kind == OpKind::Hide) showIn = 1 + rng.Below(4);
        }
        if (showIn > 0 && --showIn == 0 && !held) p.ops.push_back(make(OpKind::Show));
        if (held && rng.Chance(45))
        {
            if (rng.Chance(60))
            {
                // The engine finishes, and an input changes before the panel looks.
                p.ops.push_back(make(OpKind::Finish));
                p.ops.push_back(make(racing[rng.Below(14)]));
                ++emitted;
                if (rng.Chance(40))
                {
                    p.ops.push_back(make(racing[rng.Below(14)]));
                    ++emitted;
                }
            }
            p.ops.push_back(make(OpKind::Wait));
            held = false;
        }
        else if (!held && starves > 0 && rng.Chance(8))
        {
            p.ops.push_back(make(OpKind::Starve));
            --starves;
        }
    }
    if (held) p.ops.push_back(make(OpKind::Wait));
    return p;
}

// ---- The rig: the real panel and runner, and the model's bookkeeping ----------------------------

enum class LiveKind { Static, Paused, Running };

// What the model believes the runner remembers of a request that ran out of budget (see Starve).
struct TooLargeMemo
{
    std::set<std::pair<uint32_t, std::string>> entries;   // (entry, volatile list) this session
    Sig starved;                                          // the snapshot etc. it was stalled under
    bool have = false;
    bool fitsSince = false;                               // a decompilation of it has succeeded since
};

struct Install
{
    int op = 0;
    uint64_t frame = 0;
    Sig sig;
    DecompileResult result;
    TooLargeMemo memo;   // as it was when this result was installed
    bool onScreen = true;   // the snapshot held was the one on screen (a running emulator's lags by design)
};

struct Rig
{
    Gate gate;   // before the runner: it must outlive the worker that waits on it
    ContentBackend backend;
    DecompilerRunner runner;
    DecompilerPanel panel;
    FunctionBounds bounds;
    std::map<uint32_t, std::string> names;
    uint64_t namesGeneration = 1;
    std::unordered_map<uint32_t, std::string> comments;
    ImGuiHarness h;

    Oracle& oracle;
    Program prog;
    Rng rng;
    bool verbose;

    // The world as the app would present it.
    std::vector<int> placed;
    int game = 0;
    LiveKind liveKind = LiveKind::Static;
    uint64_t liveSource = 1, liveSerial = 1, liveFrame = 1;
    ContentP liveContent;
    bool scrubbing = false;
    int scrubRev = 0;
    ContentP scrubContent;
    uint64_t scrubSerial = 0;
    int recording = 1;
    std::vector<ContentP> rec;
    BiosP bios;
    std::string volatileText;
    bool visible = true;
    bool closed = false;     // no source for a frame (CloseData)
    int selected = -2;       // index into placed, -1: the BIOS function, -2: nothing yet
    int cpu = 0;
    uint64_t counter = 1000;

    // The model's belief about the panel.
    Sig held;                // the snapshot the policy of DecompilerPanel.h has it hold
    bool explicitPending = false;
    uint64_t lastGen = 0;
    uint64_t frames = 0;
    std::vector<Install> installs;
    Sig shown;               // the inputs the shown result was installed under
    bool haveShown = false;
    Sig stopped;
    bool haveStopped = false;
    bool stopCertain = false;   // the request stopped was held in the engine: it cannot have finished first
    uint64_t stoppedNamesGen = 0;

    // Reporting.
    uint64_t seed = 0;
    std::vector<std::string> done;   // the operations executed so far
    int opIndex = 0;
    bool failed = false;
    std::string failure;

    bool holding = false;

    // The model of the budget: what the runner may legitimately answer "too large" for.
    TooLargeMemo memo;
    bool shownExcused = false;   // the shown "too large" result was excused when it was installed

    Rig(Oracle& o, const Program& p, uint64_t sd, bool verb)
        : runner(gConfig, Options(p.starve)), h([this] { Ui(); }), oracle(o), prog(p), rng(sd * 77 + 5), verbose(verb), seed(sd)
    {
        ApplyTheme(ImGui::GetStyle());
        LoadFonts(ImGui::GetIO());
        runner.SetPhaseObserverForTests([this](DecompilePhase ph) { gate.Observe(ph); });
        placed = p.placed;
        game = p.game;
        liveContent = BuildImage(placed, game);
        MakeRecording();
        panel.SetCpu(0);
        SyncBackend();
    }
    ~Rig()
    {
        gate.Open();
        gStats.submissions += panel.Submissions();
        gStats.discarded += panel.Discarded();
        gStats.cacheHits += panel.CacheHits();
        gStats.captures += panel.Captures();
    }

    // The budget is wall-clock, so it is long enough that nothing here comes near it -- except in the
    // runs that stall a request on purpose (Starve), where it is the shortest that still leaves
    // an ordinary decompilation (milliseconds) two orders of magnitude of room.
    static const double kShortBudget;
    static RunnerOptions Options(bool starve)
    {
        RunnerOptions o;
        o.budgetSeconds = starve ? kShortBudget : 120;
        o.cancelAckSeconds = 60;
        return o;
    }

    // ---- the view the app would build -------------------------------------------------------
    struct ViewState
    {
        DecompViewKind kind = DecompViewKind::None;
        uint64_t source = 0, serial = 0, frame = 0;
        ContentP content;
    };
    ViewState ViewNow() const
    {
        ViewState v;
        if (closed) return v;
        if (scrubbing)
        {
            v.kind = DecompViewKind::Scrub;
            v.source = 0x5343525542000000ull ^ static_cast<uint64_t>(recording);
            v.serial = scrubSerial;
            v.frame = static_cast<uint64_t>(scrubRev) + 1;
            v.content = scrubContent;
            return v;
        }
        v.kind = liveKind == LiveKind::Static ? DecompViewKind::Static
                 : liveKind == LiveKind::Paused ? DecompViewKind::LivePaused : DecompViewKind::LiveRunning;
        v.source = liveSource;
        v.serial = liveSerial;
        v.frame = liveFrame;
        v.content = liveContent;
        return v;
    }

    void SyncBackend()
    {
        const ViewState v = ViewNow();
        backend.content = v.content;
        backend.source = v.source;
    }

    DecompilerView View() const
    {
        const ViewState v = ViewNow();
        DecompilerView d;
        d.backend = const_cast<ContentBackend*>(&backend);
        d.kind = v.kind;
        d.sourceLabel = "model";
        d.sourceId = v.source;
        d.deriveSerial = v.serial;
        d.frameNumber = v.frame;
        d.bios = bios;
        return d;
    }

    DecompilerInputs Inputs()
    {
        DecompilerInputs in;
        in.names = &names;
        in.namesGeneration = namesGeneration;
        in.bounds = &bounds;
        in.comments = &comments;
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
    }

    uint32_t SelectedEntry() const
    {
        if (selected == -1) return kBiosFn;
        if (selected < 0) return 0;
        return Defs()[placed[static_cast<size_t>(selected)]].entry;
    }

    Sig SigOf(const Sig& snapshot) const
    {
        Sig s = snapshot;
        s.names = names;
        s.bounds = bounds.Entries();
        s.volatileText = volatileText;
        s.entry = SelectedEntry();
        return s;
    }
    Sig ViewSig() const
    {
        const ViewState v = ViewNow();
        Sig s;
        s.valid = v.kind != DecompViewKind::None;
        s.content = v.content;
        s.bios = bios;
        s.source = v.source;
        s.serial = v.serial;
        s.frame = v.frame;
        return SigOf(s);
    }

    // ---- frames ----------------------------------------------------------------------------

    // The panel's capture policy (DecompilerPanel.h), as the model believes it: what snapshot is held
    // after this frame's Update. Mirrors only the cases the operations can reach (no auto-refresh, no
    // registers).
    void PolicyStep()
    {
        const Sig now = ViewSig();
        if (!visible || !now.valid) return;
        const bool current = held.valid && held.SameView(now);
        bool capture = false;
        if (!current)
        {
            if (ViewNow().kind == DecompViewKind::LiveRunning)
            {
                const bool biosChanged = held.valid && held.BiosHash() != now.BiosHash();
                const bool sourceChanged = held.valid && held.source != now.source;
                capture = explicitPending || !held.valid || biosChanged || sourceChanged;
            }
            else
                capture = true;
        }
        explicitPending = false;
        if (capture)
        {
            held = now;
            held.valid = true;
        }
    }

    void Observe()
    {
        const DecompileResult* s = panel.Shown();
        if (!s || s->generation == lastGen) return;
        lastGen = s->generation;
        if (haveStopped && stopCertain && namesGeneration == stoppedNamesGen && SigOf(held).Same(stopped))
            Fail("STOP: the request was cancelled while the engine held it, yet a result for the question stopped was installed");
        Install in;
        in.op = opIndex;
        in.frame = frames;
        in.sig = SigOf(held);
        in.result = *s;
        in.memo = memo;
        {
            const Sig now = ViewSig();
            in.onScreen = held.valid && now.valid &&
                          (ViewNow().kind == DecompViewKind::LiveRunning
                               ? held.source == now.source && held.BiosHash() == now.BiosHash()
                               : held.SameView(now));
        }
        installs.push_back(std::move(in));
        {
            // A decompilation of the stalled function that succeeds shows that it fits.
            const Install& last = installs.back();
            if (memo.have && last.result.status == DecompileStatus::Ok && last.sig.entry == memo.starved.entry &&
                last.sig.volatileText == memo.starved.volatileText)
                memo.fitsSince = true;
        }
        shown = installs.back().sig;
        haveShown = true;
        haveStopped = false;
    }

    void Frame(ImVec2 mouse = ImVec2(1270, 710), bool down = false)
    {
        PolicyStep();
        ++frames;
        h.Frame(mouse, down);
        Observe();
    }

    void Idle(int n = 1)
    {
        for (int i = 0; i < n; ++i) Frame();
    }

    static ImVec2 Centre(ImVec2 a, ImVec2 b) { return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); }

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
        const ImVec2 c = Centre(a, b);
        for (int i = 0; i < 3; ++i) Frame(c, false);
        Frame(c, true);
        Frame(c, false);
        return true;
    }

    // Frames until the runner is idle and the panel has nothing outstanding and asks nothing new.
    bool Settle()
    {
        const auto t0 = std::chrono::steady_clock::now();
        int calm = 0;
        uint64_t subs = panel.Submissions();
        while (calm < 3)
        {
            if (dts::MsSince(t0) > 30000.0) return false;
            Idle();
            if (panel.Submissions() != subs) calm = 0;
            else if (!runner.Busy() && !panel.Pending()) ++calm;
            else calm = 0;
            subs = panel.Submissions();
            if (calm == 0) std::this_thread::yield();
        }
        return true;
    }

    // With the engine held: pump frames, then wait until the worker is at the gate (or idle) so the
    // sequence of requests does not depend on timing.
    void PumpHeld()
    {
        Idle(3);
        const auto t0 = std::chrono::steady_clock::now();
        while (runner.Busy() && gate.Held() == 0 && dts::MsSince(t0) < 30000.0) std::this_thread::yield();
        Idle(2);
        CheckStaleMark();
    }

    // At any time, engine busy or not: a result made from other inputs than the ones now must say so.
    void CheckStaleMark()
    {
        if (failed || !haveShown || selected == -2) return;
        ++gStats.heldChecks;
        const Sig now = ViewSig();
        if (shown.Same(now) || panel.ShownStale()) return;
        Fail("STALE SHOWN AS CURRENT (P1, checked with the engine held): the result on screen was made from other inputs "
             "than the ones now, and carries no stale mark\n  shown under " + DescribeSig(shown) + "\n  now          " +
             DescribeSig(now));
    }

    // ---- operations ------------------------------------------------------------------------

    void MakeRecording()
    {
        ++recording;
        rec.clear();
        Rng r(static_cast<uint64_t>(recording) * 31 + static_cast<uint64_t>(game));
        for (int i = 0; i < 3; ++i)
        {
            std::vector<Preset> overlay;
            for (int k = 0; k < 2; ++k)
            {
                const FnDef& d = Defs()[placed[static_cast<size_t>(r.Below(1000)) % placed.size()]];
                const Site& s = d.sites[static_cast<size_t>(r.Below(1000)) % d.sites.size()];
                overlay.push_back({ s.addr, s.vals[static_cast<size_t>(r.Below(1000)) % s.vals.size()] });
            }
            rec.push_back(BuildImage(placed, game, overlay));
        }
    }

    ContentP WithByte(const ContentP& c, uint32_t addr, uint8_t v) const
    {
        std::vector<uint8_t> b = c->bytes;
        b[addr - kHw] = v;
        return MakeContent(std::move(b));
    }

    void EndSession()
    {
        // As App::ResetSessionDebugState: the runner's epoch, then the panel's forgetting.
        runner.InvalidateSession();
        panel.ResetSession();
        held = Sig();
        haveStopped = false;
        memo = TooLargeMemo();   // the runner forgets what ran out of budget with the session
    }

    std::string Execute(const Op& o)
    {
        switch (o.kind)
        {
        case OpKind::Edit:
        {
            const FnDef& d = Defs()[placed[static_cast<size_t>(o.a) % placed.size()]];
            const Site& s = d.sites[static_cast<size_t>(o.b) % d.sites.size()];
            const uint8_t v = s.vals[static_cast<size_t>(o.c) % s.vals.size()];
            if (scrubbing)
            {
                scrubContent = WithByte(scrubContent, s.addr, v);
                scrubSerial = ++counter;
            }
            else
            {
                liveContent = WithByte(liveContent, s.addr, v);
                liveSerial = ++counter;
            }
            SyncBackend();
            return "";
        }
        case OpKind::Scrub:
            scrubbing = true;
            scrubRev = o.a % static_cast<int>(rec.size());
            scrubContent = rec[static_cast<size_t>(scrubRev)];
            scrubSerial = 0;
            SyncBackend();
            return "";
        case OpKind::Resume:
            if (!scrubbing) return "(not scrubbing)";
            scrubbing = false;
            SyncBackend();
            return "";
        case OpKind::Refresh:
        {
            const uint64_t before = panel.Submissions();
            if (!Click("refresh")) return "(no Refresh button: panel hidden)";
            explicitPending = true;
            haveStopped = false;
            Idle(1);
            if (selected != -2 && !runner.Disabled() && panel.Submissions() == before)
                Fail("REFRESH: Refresh was pressed with a function selected and the panel asked the runner for nothing");
            return "";
        }
        case OpKind::Reconnect:
            liveSource = ++counter;
            if (o.a) { liveSerial = 1; liveFrame = 1; }
            if (liveKind == LiveKind::Static) liveKind = LiveKind::Paused;
            scrubbing = false;
            MakeRecording();
            SyncBackend();
            EndSession();
            return "";
        case OpKind::Replace:
        {
            game = o.a % 3;
            liveContent = BuildImage(placed, game);
            liveSource = ++counter;
            liveSerial = 1;
            liveFrame = 1;
            liveKind = o.b == 0 ? LiveKind::Static : o.b == 1 ? LiveKind::Paused : LiveKind::Running;
            scrubbing = false;
            MakeRecording();
            closed = true;   // CloseData: one frame with no source
            SyncBackend();
            EndSession();
            Frame();
            closed = false;
            SyncBackend();
            return "";
        }
        case OpKind::Bios:
            // Always a freshly made object: a reload of the same bytes must not look like a change.
            bios = o.a == 0 ? nullptr : BiosImage::Make(BiosBytes(o.a));
            return "";
        case OpKind::Volatile:
        {
            std::string error;
            if (!panel.SetVolatileMemory(kVolatileOptions[o.a], &error)) return "(volatile list refused: " + error + ")";
            volatileText = kVolatileOptions[o.a];
            return "";
        }
        case OpKind::Rename:
        {
            const uint32_t addr = Defs()[placed[static_cast<size_t>(o.a) % placed.size()]].entry;
            const std::string nm = kNameOptions[o.b];
            const bool had = names.count(addr) != 0;
            if (nm.empty() ? !had : (had && names[addr] == nm)) return "(no change)";
            if (nm.empty()) names.erase(addr); else names[addr] = nm;
            ++namesGeneration;
            return "";
        }
        case OpKind::Bound:
        {
            const uint32_t addr = Defs()[placed[static_cast<size_t>(o.a) % placed.size()]].entry + (o.b ? 4u : 0u);
            if (bounds.Entries().count(addr)) bounds.ClearEntry(addr); else bounds.SetEntry(addr);
            return "";
        }
        case OpKind::Select:
        {
            // Typed into the entry box, as a user does. Typed rather than ShowFunctionContaining: that
            // asks the function finder, which by design never overrides a user-set start, so a start
            // inside another function would move the entry (area 4's subject, not this one's).
            const int want = o.a < 0 ? -1 : static_cast<int>(static_cast<size_t>(o.a) % placed.size());
            const int before = selected;
            ImVec2 a, b;
            if (!visible || !panel.ItemRect("entry", a, b)) return "(panel hidden: no entry box)";
            selected = want;
            const uint32_t entry = SelectedEntry();
            selected = before;   // until the Enter has been handled
            const ImVec2 c = Centre(a, b);
            for (int i = 0; i < 3; ++i) Frame(c, false);
            Frame(c, true);
            Frame(c, false);
            Idle(2);
            Key(ImGuiKey_End);
            for (int i = 0; i < 16; ++i) Key(ImGuiKey_Backspace);
            const std::string text = Hex(entry);
            for (char ch : text) ImGui::GetIO().AddInputCharacter(static_cast<unsigned>(ch));
            Idle();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
            Frame();                    // the box returns true in this frame's Draw: the target is set
            selected = want;            // ...and the next Update is the first to ask for it
            explicitPending = true;
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
            Frame();
            return "";
        }
        case OpKind::Stop:
            if (!panel.Pending()) return "(nothing outstanding)";
            stopped = SigOf(held);
            stopCertain = holding && gate.Held() > 0;
            stoppedNamesGen = namesGeneration;
            if (!Click("stop")) return "(no Stop button: panel hidden)";
            haveStopped = true;
            return "";
        case OpKind::Cpu:
            cpu = o.a & 1;
            panel.SetCpu(cpu);
            return "";
        case OpKind::Run:
            if (scrubbing) return "(scrubbing: the live emulator is behind the recorded frame)";
            liveKind = LiveKind::Running;
            return "";
        case OpKind::Pause:
            if (liveKind != LiveKind::Running) return "(not running)";
            liveKind = LiveKind::Paused;
            return "";
        case OpKind::Advance:
            if (scrubbing) return "(scrubbing)";
            ++liveFrame;
            liveSerial = ++counter;   // the emulator wrote memory somewhere: same code, new key
            return "";
        case OpKind::Hide: visible = false; return "";
        case OpKind::Show: visible = true; return "";
        case OpKind::Finish:
        {
            // The engine runs everything it has and the results wait, undelivered, in the runner.
            gate.Open();
            holding = false;
            const auto t0 = std::chrono::steady_clock::now();
            while (runner.Busy() && dts::MsSince(t0) < 30000.0) std::this_thread::yield();
            return runner.Busy() ? "(the engine did not finish)" : "";
        }
        case OpKind::Starve: return Starve();
        case OpKind::Hold:
        case OpKind::Wait:
            return "";
        }
        return "";
    }

    // Refresh the shown function and keep the engine on it past the time budget, so the runner
    // answers "too large" (and remembers it for the session).
    std::string Starve()
    {
        if (!prog.starve) return "(no short budget in this run)";
        if (std::getenv("SE_MODEL_NOSTARVE")) return "(SE_MODEL_NOSTARVE)";
        if (holding || selected == -2 || !visible) return "(nothing to stall)";
        gate.Close();
        if (!Click("refresh"))
        {
            gate.Open();
            return "(no Refresh button)";
        }
        explicitPending = true;
        haveStopped = false;
        Idle(2);
        const auto t0 = std::chrono::steady_clock::now();
        while (runner.Busy() && gate.Held() == 0 && dts::MsSince(t0) < 30000.0) std::this_thread::yield();
        if (gate.Held() == 0)
        {
            gate.Open();
            Wait();
            return "(Refresh asked for nothing)";
        }
        const auto t1 = std::chrono::steady_clock::now();
        while (dts::MsSince(t1) < kShortBudget * 1000.0 + 300.0)
        {
            Frame();   // the runner enforces the budget when polled
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        memo.entries.insert(std::make_pair(SelectedEntry(), volatileText));
        memo.starved = SigOf(held);
        memo.have = true;
        memo.fitsSince = false;
        Wait();
        return "";
    }

    // ---- checking --------------------------------------------------------------------------

    void Fail(const std::string& what)
    {
        if (failed) return;
        failed = true;
        std::ostringstream o;
        o << what;
        failure = o.str();
        std::printf("\nMODEL FAILURE, seed %llu, after operation %d\n", (unsigned long long)seed, opIndex);
        std::printf("  world: functions");
        for (int f : placed) std::printf(" %s@%08X", Defs()[f].label, Defs()[f].entry);
        std::printf(", image %d\n  operations so far:\n", game);
        for (const std::string& d : done) std::printf("    %s\n", d.c_str());
        std::printf("  %s\n", failure.c_str());
        std::printf("  now: %s\n", DescribeSig(ViewSig()).c_str());
        std::printf("  panel: pending %d, stale %d, held key %s, captures %llu, submissions %llu, discarded %llu, cache hits %llu\n",
                    panel.Pending(), panel.ShownStale(), KeyText(panel.HeldKey()).c_str(),
                    (unsigned long long)panel.Captures(), (unsigned long long)panel.Submissions(),
                    (unsigned long long)panel.Discarded(), (unsigned long long)panel.CacheHits());
    }

    static std::string KeyText(const SnapshotKey* k)
    {
        if (!k) return "none";
        char b[160];
        std::snprintf(b, sizeof b, "{source %llu, serial %llu, frame %llu, regions %08X}", (unsigned long long)k->sourceId,
                      (unsigned long long)k->deriveSerial, (unsigned long long)k->frameNumber,
                      static_cast<uint32_t>(k->regionSetHash));
        return b;
    }

    static std::string ResultText(const DecompileResult& r)
    {
        std::ostringstream o;
        o << "status " << int(r.status) << ", entry " << Hex(r.requestedEntry) << ", key " << KeyText(&r.key);
        if (!r.error.empty()) o << ", error '" << r.error << "'";
        o << "\n" << r.Text();
        return o.str();
    }

    // The functions 'entry' reaches under 's' (itself, its callees, theirs), canonical.
    void Reach(const Sig& s, uint32_t entry, std::set<uint32_t>& out, int depth = 0)
    {
        if (depth > 6 || !out.insert(entry).second) return;
        for (uint32_t c : oracle.Run(s, entry, Lent()).calledAt) Reach(s, c, out, depth + 1);
    }

    // A "too large" answer the model accepts: the request that was stalled on purpose, or the
    // runner's memo of it for the same function (and volatile list) while the bytes it runs are those
    // it was stalled on -- its own and its callees'. Another snapshot
    // key with the same code is the same verdict (a live game's every frame is a new key, and the
    // memo exists so a slow function is not retried on each). Different bytes, or a Refresh that has
    // since shown that the function fits, make it a verdict on something else: the from-scratch
    // answer is a decompilation.
    bool TooLargeExcused(const Sig& s, const TooLargeMemo& m)
    {
        const Sig& starved = m.starved;
        if (!m.have || m.fitsSince || !m.entries.count(std::make_pair(s.entry, s.volatileText)) ||
            s.entry != starved.entry || s.volatileText != starved.volatileText)
            return false;
        std::set<uint32_t> reach;
        Reach(s, s.entry, reach);
        for (uint32_t f : reach)
        {
            if (f < kHw || f + 0x40 > kHw + kSize) continue;
            const uint8_t* a = s.content->bytes.data() + (f - 4 - kHw);
            const uint8_t* b = starved.content->bytes.data() + (f - 4 - kHw);
            if (std::memcmp(a, b, 0x44) != 0) return false;
        }
        return true;
    }

    bool CheckInstalls()
    {
        for (const Install& in : installs)
        {
            if (failed) break;
            const std::string where = "result installed at frame " + std::to_string(in.frame) + " (after operation " +
                                      std::to_string(in.op) + ") under " + DescribeSig(in.sig);
            if (!in.onScreen)
            {
                Fail("ON SCREEN: " + where + "\n  the answer was installed for memory that is no longer what is on screen (the panel "
                     "holds a snapshot its view has moved away from)");
                break;
            }
            const SnapshotKey want = oracle.ExpectedKey(in.sig);
            if (in.result.key != want)
            {
                Fail("KEY: " + where + "\n  expected key " + KeyText(&want) + "\n  actual key   " + KeyText(&in.result.key));
                break;
            }
            if (in.result.requestedEntry != in.sig.entry)
            {
                Fail("ENTRY: " + where + "\n  expected entry " + Hex(in.sig.entry) + ", actual " + Hex(in.result.requestedEntry));
                break;
            }
            if (in.result.status == DecompileStatus::TooLarge)
            {
                shownExcused = TooLargeExcused(in.sig, in.memo);
                if (!shownExcused)
                {
                    const DecompileResult& want = oracle.Run(in.sig, in.sig.entry, Lent());
                    Fail("TOO LARGE: " + where + "\n  the runner answered '" + in.result.error + "', but a from-scratch decompilation of these "
                         "inputs is status " + std::to_string(int(want.status)) + ": the verdict was reached for other bytes, or "
                         "a Refresh has since shown that the function fits");
                    break;
                }
                continue;
            }
            const std::vector<const DecompileResult*> cands = oracle.Candidates(in.sig);
            bool ok = false;
            for (const DecompileResult* c : cands)
                if (SameAnswer(in.result, *c)) ok = true;
            if (!ok)
            {
                std::string why;
                SameAnswer(in.result, *cands.front(), &why);
                std::string msg = "TEXT: " + where + "\n  the installed result matches none of the " + std::to_string(cands.size()) +
                                  " answers a from-scratch decompilation can give (first differs in " + why + ")\n  actual:\n" +
                                  ResultText(in.result);
                for (size_t i = 0; i < cands.size() && i < 4; ++i)
                    msg += "\n  expected (lending " + std::to_string(i == 0 ? 0 : 1) + (i == 0 ? " prototypes" : "+ prototypes") + ", variant " +
                           std::to_string(i) + "):\n" + ResultText(*cands[i]);
                Fail(msg);
            }
        }
        gStats.installs += installs.size();
        installs.clear();
        return !failed;
    }

    // Idle: the runner has nothing, the panel asks nothing.
    void CheckIdle()
    {
        if (failed) return;
        if (runner.Busy())
        {
            Fail("the runner is still busy after the panel settled");
            return;
        }
        if (!CheckInstalls()) return;
        if (panel.Pending())
        {
            Fail("PENDING: the runner is idle but the panel still waits for an answer (a result was lost)");
            return;
        }
        if (visible && ViewNow().kind != DecompViewKind::None)
        {
            const SnapshotKey* hk = panel.HeldKey();
            if (!hk || !held.valid)
            {
                Fail(std::string("HELD: the panel holds no snapshot but the model expects ") + DescribeSig(held));
                return;
            }
            const SnapshotKey want = oracle.ExpectedKey(held);
            if (*hk != want)
            {
                Fail("HELD: the snapshot held is not the one the refresh policy says\n  expected key " + KeyText(&want) +
                     "\n  actual key   " + KeyText(hk));
                return;
            }
        }
        if (selected == -2) return;
        ++gStats.idleChecks;

        const Sig now = ViewSig();
        const DecompileResult* s = panel.Shown();
        if (!s)
        {
            if (visible && now.valid) Fail("NOTHING SHOWN: a function is selected, the runner is idle, and the panel shows no result");
            return;
        }
        if (!haveShown)
        {
            Fail("a result is shown that was never observed being installed");
            return;
        }
        const bool viewDiff = !shown.SameView(now);
        const bool otherDiff = !shown.SameOther(now);
        const bool modelStale = viewDiff || otherDiff;
        const bool panelStale = panel.ShownStale();
        // By design a running emulator's held snapshot lags (frames and writes, not source or BIOS).
        const bool byDesignLag = !otherDiff && ViewNow().kind == DecompViewKind::LiveRunning && shown.source == now.source &&
                                 shown.BiosHash() == now.BiosHash();
        const bool excused = haveStopped && SigOf(held).Same(stopped);   // the user stopped this very question
        std::string state = "\n  shown under " + DescribeSig(shown) + "\n  now          " + DescribeSig(now) +
                            "\n  model stale " + std::to_string(modelStale) + " (view " + std::to_string(viewDiff) +
                            ", other inputs " + std::to_string(otherDiff) + "), panel stale " + std::to_string(panelStale);
        if (modelStale && !panelStale)
        {
            Fail("STALE SHOWN AS CURRENT (P1): the result on screen was made from other inputs than the ones now, and carries no stale mark" + state);
            return;
        }
        if (!modelStale && panelStale && !excused)
        {
            Fail("STALE FOREVER: the result on screen is for the inputs now, but is marked stale with nothing outstanding" + state);
            return;
        }
        if (modelStale && !byDesignLag && !excused && visible)
        {
            Fail("NOT REFRESHED: the panel is idle with a result for other inputs than the ones now, and no Stop explains it" + state);
            return;
        }
        if (!modelStale && s->status == DecompileStatus::TooLarge)
        {
            if (!shownExcused)
                Fail("TOO LARGE: the result on screen says '" + s->error + "' but nothing excuses it" + state);
            return;
        }
        if (!modelStale)
        {
            // The result on screen is the answer for the inputs now.
            const std::vector<const DecompileResult*> cands = oracle.Candidates(now.valid ? SigOf(held) : now);
            bool ok = false;
            for (const DecompileResult* c : cands)
                if (SameAnswer(*s, *c)) ok = true;
            if (!ok)
            {
                Fail("SHOWN TEXT: the result on screen is not a from-scratch answer for the inputs now" + state + "\n  actual:\n" +
                     ResultText(*s) + "\n  expected:\n" + ResultText(*cands.back()));
                return;
            }
        }
    }

    // After the callee-first walk: the function on screen is exactly the from-scratch answer, every
    // callee prototype lent.
    void CheckConverged()
    {
        if (failed || selected == -2) return;
        const Sig now = ViewSig();
        const DecompileResult* s = panel.Shown();
        if (!s || !haveShown || !shown.Same(now) || s->status == DecompileStatus::TooLarge) return;
        const DecompileResult& want = oracle.Converged(SigOf(held));
        std::string why;
        if (!SameAnswer(*s, want, &why))
            Fail("CONVERGED: after a callee-first walk the function does not show the from-scratch answer (differs in " + why +
                 ")\n  shown under " + DescribeSig(shown) + "\n  actual:\n" + ResultText(*s) + "\n  expected:\n" + ResultText(want));
    }

    void Wait()
    {
        gate.Open();
        holding = false;
        if (!Settle())
        {
            Fail("HANG: the panel did not settle within 30 s");
            return;
        }
        CheckIdle();
    }

    void Run()
    {
        // The first look: one frame to lay the window out, then select the first function.
        Idle(3);
        Op first;
        first.kind = OpKind::Select;
        first.a = 0;
        Step(first, "initial selection");
        int n = 0;
        for (const Op& o : prog.ops)
        {
            if (failed) return;
            ++n;
            Step(o, nullptr, n);
        }
        if (failed) return;
        Wait();
        Converge();
    }

    void Step(const Op& o, const char* label, int index = 0)
    {
        opIndex = index;
        std::string d = (label ? std::string(label) + ": " : std::string()) + Describe(o, placed);
        if (o.kind == OpKind::Hold)
        {
            gate.Close();
            holding = true;
        }
        const std::string note = o.kind == OpKind::Wait ? "" : Execute(o);   // Hold: closes the gate above
        if (o.kind == OpKind::Hold) ++gStats.holds;
        else if (o.kind != OpKind::Wait)
        {
            if (!note.empty() && note[0] == '(') ++gStats.noEffect; else ++gStats.ops;
            if (note.empty() && o.kind == OpKind::Finish) ++gStats.races;
            if (note.empty() && o.kind == OpKind::Stop) ++gStats.stops;
            if (note.empty() && o.kind == OpKind::Starve) ++gStats.starves;
        }
        d = "[" + std::to_string(index) + "] " + d + (note.empty() ? "" : "  " + note);
        done.push_back(d);
        if (verbose) std::printf("%s\n", d.c_str());
        if (o.kind == OpKind::Hold || o.kind == OpKind::Finish) return;
        if (o.kind == OpKind::Wait)
        {
            Wait();
            Verbose();
            return;
        }
        if (o.kind == OpKind::Starve)
        {
            Verbose();
            return;
        }
        if (holding) PumpHeld(); else { Idle(2); Wait(); Verbose(); }
    }

    void Verbose()
    {
        if (!verbose || failed) return;
        const DecompileResult* s = panel.Shown();
        std::printf("      -> %s, shown entry %s key %s, stale %d, held %s, submissions %llu, hits %llu, model runs %llu\n",
                    visible ? "visible" : "hidden", s ? Hex(s->requestedEntry).c_str() : "-", KeyText(s ? &s->key : nullptr).c_str(),
                    panel.ShownStale(), KeyText(panel.HeldKey()).c_str(), (unsigned long long)panel.Submissions(),
                    (unsigned long long)panel.CacheHits(), (unsigned long long)oracle.runs);
        if (s && std::getenv("SE_MODEL_TEXT")) std::printf("%s\n", s->Text().c_str());
    }

    // A callee-first walk through every function, then each shows exactly the from-scratch answer.
    void Converge()
    {
        Op show;
        show.kind = OpKind::Show;
        Step(show, "final walk");
        std::vector<int> order;   // callees first: A, B, P, C, D, E (the world's call graph is acyclic that way)
        for (int f : { FnA, FnB, FnP, FnQ, FnC, FnD, FnE })
            for (size_t i = 0; i < placed.size(); ++i)
                if (placed[i] == f) order.push_back(static_cast<int>(i));
        const bool hasE = std::find(placed.begin(), placed.end(), int(FnE)) != placed.end();
        if (hasE) order.insert(order.begin(), -1);
        for (int idx : order)
        {
            if (failed) return;
            Op sel;
            sel.kind = OpKind::Select;
            sel.a = idx;
            opIndex = 1000;
            const std::string d = std::string("[walk] ") + Describe(sel, placed);
            Execute(sel);
            done.push_back(d);
            if (verbose) std::printf("%s\n", d.c_str());
            Idle(2);
            Wait();
            // A question the user stopped is not asked again by choosing the function again, only by
            // Refresh (DecompilerPanel.cpp, Accept): the documented way out, taken here.
            const DecompileResult* now = panel.Shown();
            if (!failed && (!now || now->requestedEntry != SelectedEntry()) && haveStopped)
            {
                Click("refresh");
                explicitPending = true;
                haveStopped = false;
                Idle(2);
                Wait();
            }
            CheckConverged();
            ++gStats.walks;
            Verbose();
        }
    }
};

const double Rig::kShortBudget = 1.0;

// ---- Seeds -------------------------------------------------------------------------------------

bool RunProgram(Oracle& oracle, Program p, uint64_t seed, bool verbose, const std::set<int>& skip, int limit)
{
    if (!skip.empty() || limit >= 0)
    {
        std::vector<Op> kept;
        int n = 0;
        for (const Op& o : p.ops)
        {
            ++n;
            if (skip.count(n)) continue;
            if (limit >= 0 && n > limit) continue;
            kept.push_back(o);
        }
        // Holds and waits stay paired by construction of the stream; a dropped Wait is added back at the end.
        p.ops = kept;
    }
    if (verbose)
    {
        std::printf("seed %llu: %zu operations over", (unsigned long long)seed, p.ops.size());
        for (int f : p.placed) std::printf(" %s", Defs()[f].label);
        std::printf(", image %d\n", p.game);
    }
    if (std::getenv("SE_MODEL_LIST"))
    {
        int n = 0;
        for (const Op& o : p.ops) std::printf("[%d] %s\n", ++n, Describe(o, p.placed).c_str());
        return true;
    }
    ++gStats.seeds;
    Rig rig(oracle, p, seed, verbose);
    rig.Run();
    return !rig.failed;
}

bool RunSeed(Oracle& oracle, uint64_t seed, bool verbose, const std::set<int>& skip, int limit)
{
    return RunProgram(oracle, Generate(seed), seed, verbose, skip, limit);
}

// The default run: this many seeds, then the shrunk sequences of the findings that stand open in
// REVIEW_LEDGER.md (each fails until its row is fixed; remove a line when its row closes).
const uint64_t kDefaultSeeds = 18;

struct Pinned
{
    const char* id;
    const char* script;
    const char* world;
};
const Pinned kPinned[] = {
    // A function that ran out of budget is answered "too large" for the bytes edited since.
    { "A1-1", "select A; starve; edit A 0 1", "A" },
    // ...and again after a Refresh has just decompiled it, when anything asks again (a rename).
    { "A1-2", "select A; starve; refresh; name A 1", "A" },
};

std::set<int> ParseList(const char* text)
{
    std::set<int> out;
    if (!text) return out;
    for (const char* p = text; *p;)
    {
        char* end = nullptr;
        const long v = std::strtol(p, &end, 10);
        if (end == p) { ++p; continue; }
        out.insert(static_cast<int>(v));
        p = end;
    }
    return out;
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

    const char* oneSeed = std::getenv("SE_MODEL_SEED");
    const char* manySeeds = std::getenv("SE_MODEL_SEEDS");
    uint64_t first = 1, last = kDefaultSeeds;
    bool verbose = false;
    if (oneSeed && *oneSeed)
    {
        first = last = std::strtoull(oneSeed, nullptr, 10);
        verbose = true;
    }
    else if (manySeeds && *manySeeds)
        last = std::strtoull(manySeeds, nullptr, 10);
    const std::set<int> skip = verbose ? ParseList(std::getenv("SE_MODEL_SKIP")) : std::set<int>();
    const char* steps = std::getenv("SE_MODEL_STEPS");
    const int limit = verbose && steps && *steps ? std::atoi(steps) : -1;

    int failedSeeds = 0;
    std::vector<uint64_t> failedList;
    try
    {
        Oracle oracle;
        if (const char* script = std::getenv("SE_MODEL_SCRIPT"))
        {
            const char* world = std::getenv("SE_MODEL_WORLD");
            const char* image = std::getenv("SE_MODEL_IMAGE");
            Program p;
            std::string error;
            if (!ParseScript(script, world ? world : "C,A", image ? std::atoi(image) : 0, p, error))
            {
                std::printf("SE_MODEL_SCRIPT: %s\n", error.c_str());
                return 2;
            }
            const bool ok = RunProgram(oracle, p, 0, true, std::set<int>(), -1);
            std::printf("script: %s\n", ok ? "ok" : "FAILED");
            dts::RemoveTree(scratch);
            return ok ? 0 : 1;
        }
        for (uint64_t seed = first; seed <= last; ++seed)
        {
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = RunSeed(oracle, seed, verbose, skip, limit);
            if (!ok)
            {
                ++failedSeeds;
                failedList.push_back(seed);
            }
            if (verbose || !ok || last - first < 60)
                std::printf("seed %llu: %s, %.0f ms (model runs so far %llu, memo hits %llu)\n", (unsigned long long)seed,
                            ok ? "ok" : "FAILED", dts::MsSince(t0), (unsigned long long)oracle.runs,
                            (unsigned long long)oracle.memoHits);
        }
        if (first == 1 && !oneSeed && !manySeeds)
        {
            for (const Pinned& pin : kPinned)
            {
                Program p;
                std::string error;
                if (!ParseScript(pin.script, pin.world, 0, p, error))
                {
                    std::printf("pinned %s: %s\n", pin.id, error.c_str());
                    return 2;
                }
                std::printf("pinned finding %s: %s\n", pin.id, pin.script);
                const auto t0 = std::chrono::steady_clock::now();
                const bool ok = RunProgram(oracle, p, 0, false, std::set<int>(), -1);
                std::printf("pinned finding %s: %s, %.0f ms\n", pin.id, ok ? "ok" : "FAILED", dts::MsSince(t0));
                if (!ok) ++failedSeeds;
            }
        }
        std::printf("%llu seed(s), %d failed; model engine runs %llu, memo hits %llu\n", (unsigned long long)(last - first + 1),
                    failedSeeds, (unsigned long long)oracle.runs, (unsigned long long)oracle.memoHits);
        std::printf("covered: %llu operations (%llu without effect), %llu held steps, %llu finish-then-change races, %llu stops, "
                    "%llu budget stalls; %llu results checked at install, %llu idle checks, %llu held-engine stale checks, "
                    "%llu callee-first walks; panel: %llu captures, %llu submissions, %llu discarded, %llu cache hits\n",
                    (unsigned long long)gStats.ops, (unsigned long long)gStats.noEffect, (unsigned long long)gStats.holds,
                    (unsigned long long)gStats.races, (unsigned long long)gStats.stops, (unsigned long long)gStats.starves,
                    (unsigned long long)gStats.installs, (unsigned long long)gStats.idleChecks, (unsigned long long)gStats.heldChecks,
                    (unsigned long long)gStats.walks, (unsigned long long)gStats.captures, (unsigned long long)gStats.submissions,
                    (unsigned long long)gStats.discarded, (unsigned long long)gStats.cacheHits);
        if (!failedList.empty())
        {
            std::printf("failing seeds:");
            for (uint64_t s : failedList) std::printf(" %llu", (unsigned long long)s);
            std::printf("\n");
        }
    }
    catch (std::exception& e)
    {
        std::printf("FATAL: uncaught std::exception: %s\n", e.what());
        return 3;
    }
    dts::RemoveTree(scratch);
    CHECK(failedSeeds == 0);
    return dts::Finish("DecompilerModelTests", start);
}
