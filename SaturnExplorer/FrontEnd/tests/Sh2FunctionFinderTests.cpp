// Sh2FunctionFinder over hand-built byte vectors: the rts back-scan with literal pools and nop
// padding between functions, the entry ladder, call-site extraction for bsr and for jsr through
// a literal, and the persisted user corrections.
//
// usage: SaturnExplorerSh2FunctionFinderTests <scratch dir>
#include "DecompilerTestSupport.h"

#include "Decompiler/Sh2FunctionFinder.h"

using namespace sfe::decomp;

namespace {

const uint32_t kF1 = 0x06008000u;
const uint32_t kF2 = 0x06008020u;
const uint32_t kF3 = 0x0600802Eu;
const uint32_t kF1Jsr = 0x06008004u;
const uint32_t kF2Bsr = 0x06008022u;

// Three functions, back to back, the way a compiler lays them out:
//
//   06007FFC  rts ; nop                 (the end of whatever precedes f1)
//   06008000  f1: sts.l pr,@-r15 ; mov.l @(12,pc),r1 ; jsr @r1 ; nop ; lds.l @r15+,pr ; rts ; nop
//   0600800E  nop                       (aligns the literal)
//   06008010  .long 0x06008020          (f1's literal pool: the address of f2)
//   06008014  nop nop, then zeros       (padding)
//   06008020  f2: mov r4,r0 ; bsr f3 ; add #1,r0 ; rts ; nop
//   0600802A  nop nop                   (padding)
//   0600802E  f3: rts ; add #2,r0 ; nop...
std::shared_ptr<MemorySnapshot> Functions()
{
    std::vector<uint8_t> hw;
    auto put = [&](uint32_t a, std::initializer_list<uint16_t> words) {
        size_t off = a - 0x06000000u;
        for (uint16_t w : words) { dts::Put16(hw, off, w); off += 2; }
    };
    put(0x06007FFCu, { 0x000B, 0x0009 });
    put(kF1, { 0x4F22, 0xD103, 0x410B, 0x0009, 0x4F26, 0x000B, 0x0009, 0x0009 });
    dts::Put32(hw, 0x06008010u - 0x06000000u, kF2);
    put(0x06008014u, { 0x0009, 0x0009 });
    put(kF2, { 0x6043, 0xB004, 0x7001, 0x000B, 0x0009, 0x0009, 0x0009 });
    put(kF3, { 0x000B, 0x7002, 0x0009, 0x0009, 0x0009 });
    // A jsr whose register was changed after the literal load, and one never loaded.
    put(0x06009000u, { 0xD102, 0x7104, 0x410B, 0x0009, 0x420B, 0x0009 });
    // After an rts, words that are not SH-2 instructions.
    put(0x0600A000u, { 0x000B, 0x0009, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF });
    return dts::MakeSnapshot(hw);
}

void TestRtsScan()
{
    auto s = Functions();
    uint32_t e = 0;
    std::string why;
    CHECK(RtsScanEntry(*s, kF1 + 4, e) && e == kF1);
    CHECK(RtsScanEntry(*s, kF2 + 4, e) && e == kF2);     // past f1's literal pool and padding
    CHECK(RtsScanEntry(*s, kF2, e) && e == kF2);
    CHECK(RtsScanEntry(*s, kF2 + 8, e) && e == kF2);     // f2's own rts slot is still f2
    CHECK(RtsScanEntry(*s, kF3 + 2, e) && e == kF3);
    CHECK(RtsScanEntry(*s, 0x26008024u, e) && e == kF2); // a mirror answers canonically

    CHECK(!RtsScanEntry(*s, 0x06008012u, e, &why));     // inside the literal pool: not code
    std::printf("  literal pool -> %s\n", why.c_str());
    CHECK(why.find("literal pool") != std::string::npos);
    CHECK(!RtsScanEntry(*s, 0x0600A006u, e, &why));     // what follows the rts is not code
    std::printf("  undecodable -> %s\n", why.c_str());
    CHECK(!RtsScanEntry(*s, 0x06040000u, e, &why));     // no rts within the span
    std::printf("  no rts -> %s\n", why.c_str());
    CHECK(!RtsScanEntry(*s, 0x66008024u, e, &why));     // not memory
}

void TestLadder()
{
    auto s = Functions();
    FinderHints none;
    EntryGuess g = FindFunctionContaining(*s, kF2 + 4, none);
    CHECK(g.ok && g.entry == kF2 && g.source == EntrySource::RtsScan);
    std::printf("  no hints: %08X from %s (confidence %d)\n", g.entry, EntrySourceName(g.source), g.confidence);

    // A symbol at f1 does not claim an address past f1's rts...
    FinderHints sym;
    sym.symbols.insert(kF1);
    g = FindFunctionContaining(*s, kF2 + 4, sym);
    CHECK(g.ok && g.entry == kF2 && g.source == EntrySource::RtsScan);
    // ...but does claim its own body.
    g = FindFunctionContaining(*s, kF1 + 6, sym);
    CHECK(g.ok && g.entry == kF1 && g.source == EntrySource::Symbol);

    // A user-set entry wins even across an rts: an early return looks just like an end, and the
    // user's correction is for exactly that.
    FinderHints user;
    user.userEntries.insert(kF1);
    g = FindFunctionContaining(*s, kF2 + 4, user);
    CHECK(g.ok && g.entry == kF1 && g.source == EntrySource::User && g.confidence == 100);

    // The same entry known two ways is named by the more trusted.
    FinderHints both;
    both.callSiteTargets.insert(kF2);
    both.symbols.insert(0x26008020u);   // a mirror of the same entry
    g = FindFunctionContaining(*s, kF2 + 4, both);
    CHECK(g.ok && g.entry == kF2 && g.source == EntrySource::Symbol);

    FinderHints site;
    site.callSiteTargets.insert(kF2);
    g = FindFunctionContaining(*s, kF2 + 4, site);
    CHECK(g.ok && g.entry == kF2 && g.source == EntrySource::CallSite);
    FinderHints callee;
    callee.callees.insert(kF3);
    g = FindFunctionContaining(*s, kF3 + 2, callee);
    CHECK(g.ok && g.entry == kF3 && g.source == EntrySource::Callee);

    // No rts to scan back to: a known entry still answers; nothing at all is an honest failure.
    FinderHints far;
    far.symbols.insert(0x06040000u);
    g = FindFunctionContaining(*s, 0x06040010u, far);
    CHECK(g.ok && g.entry == 0x06040000u && g.source == EntrySource::Symbol);
    g = FindFunctionContaining(*s, 0x06040010u, none);
    CHECK(!g.ok && !g.why.empty());
    g = FindFunctionContaining(*s, 0xC6008024u, sym);
    CHECK(!g.ok && g.why.find("cache data array") != std::string::npos);
}

void TestCallTargets()
{
    auto s = Functions();
    uint32_t t = 0;
    CHECK(CallTargetAt(*s, kF1Jsr, t) && t == kF2);     // jsr @r1, r1 from the literal pool
    CHECK(CallTargetAt(*s, kF2Bsr, t) && t == kF3);     // bsr encodes it
    CHECK(!CallTargetAt(*s, 0x06009004u, t));           // r1 changed after the load
    CHECK(!CallTargetAt(*s, 0x06009008u, t));           // r2 never loaded
    CHECK(!CallTargetAt(*s, kF1, t));                   // not a call
    CHECK(CallTargetsFromListing(*s, kF1, kF3 + 4) == (std::vector<uint32_t>{ kF2, kF3 }));
}

void TestBoundsPersistence(const std::string& scratch)
{
    const std::string path = dts::Join(scratch, "saturn_function_bounds.txt");
    FunctionBounds b;
    CHECK(b.Load(path));   // missing: empty, not an error
    CHECK(b.Entries().empty());
    b.SetEntry(0x26008020u);   // stored canonically
    b.SetEntry(kF1);
    b.SetEntry(0x66000000u);   // not memory: ignored
    CHECK(b.Save(path));
    FunctionBounds c;
    CHECK(c.Load(path));
    CHECK(c.Entries() == (std::set<uint32_t>{ kF1, kF2 }));
    c.ClearEntry(kF1);
    CHECK(c.Entries() == (std::set<uint32_t>{ kF2 }));
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
    RUN(TestRtsScan());
    RUN(TestLadder());
    RUN(TestCallTargets());
    RUN(TestBoundsPersistence(scratch));
    dts::RemoveTree(scratch);
    return dts::Finish("Sh2FunctionFinderTests", start);
}
