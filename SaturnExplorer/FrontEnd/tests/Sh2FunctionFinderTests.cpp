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
    // Jsrs whose register the literal load no longer holds by the call (the old literal still
    // points at f2): mova overwrites r0 implicitly; a postincrement through r1 changes r1; a
    // branch in between means the load may not have run.
    put(0x0600B000u, { 0xD007, 0xC711, 0x400B, 0x0009 });   // mov.l @(28,pc),r0 ; mova @(68,pc),r0 ; jsr @r0
    // (mova's operand fields hold 1 and 1: only its implicit write of r0 says it changes r0)
    put(0x0600B008u, { 0xD106, 0x6216, 0x410B, 0x0009 });   // mov.l @(24,pc),r1 ; mov.l @r1+,r2 ; jsr @r1
    put(0x0600B010u, { 0xD105, 0x8B00, 0x410B, 0x0009 });   // mov.l @(20,pc),r1 ; bf .+4 ; jsr @r1
    dts::Put32(hw, 0x0600B020u - 0x06000000u, kF2);   // their literals: 0x0600B020, 24, 28
    dts::Put32(hw, 0x0600B024u - 0x06000000u, kF2);
    dts::Put32(hw, 0x0600B028u - 0x06000000u, kF2);
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

    // A known entry is a function start: a nearer symbol is a later function, which an older
    // user entry further back does not claim (the ladder is per entry, not "user beats all").
    FinderHints mixed;
    mixed.userEntries.insert(kF1);
    mixed.symbols.insert(kF2);
    g = FindFunctionContaining(*s, kF2 + 4, mixed);
    CHECK(g.ok && g.entry == kF2 && g.source == EntrySource::Symbol);
    mixed.symbols.clear();
    mixed.callees.insert(kF3);
    g = FindFunctionContaining(*s, kF3 + 2, mixed);
    CHECK(g.ok && g.entry == kF3 && g.source == EntrySource::Callee);

    // A user-set entry wins across an rts: an early return looks just like an end, and the
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
    FinderHints distant;   // not 'far': <windows.h> defines it away
    distant.symbols.insert(0x06040000u);
    g = FindFunctionContaining(*s, 0x06040010u, distant);
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
    CHECK(!CallTargetAt(*s, 0x0600B004u, t));           // mova overwrote r0 after the load
    CHECK(!CallTargetAt(*s, 0x0600B00Cu, t));           // @r1+ changed r1 after the load
    CHECK(!CallTargetAt(*s, 0x0600B014u, t));           // a branch between load and call
    CHECK(CallTargetsFromListing(*s, kF1, kF3 + 4) == (std::vector<uint32_t>{ kF2, kF3 }));
}

// Review of the control-flow criterion: the scan and the ladder against layouts that fool an
// rts-only boundary. An early return inside a named function; a function whose flow the finder
// cannot follow (a switch); the constant 11 in a literal pool, which reads as rts; a function that
// ends in a tail jump instead of rts; a jump table addressed with mova after an rts.
void TestBoundaries()
{
    std::vector<uint8_t> hw;
    auto put = [&](uint32_t a, std::initializer_list<uint16_t> words) {
        size_t off = a - 0x06000000u;
        for (uint16_t w : words) { dts::Put16(hw, off, w); off += 2; }
    };
    // An early return: the rts at +8 is not the function's end, since bt jumps past it.
    const uint32_t early = 0x06030004u;
    put(early - 4, { 0x000B, 0x0009 });
    put(early, { 0x4415,      // cmp/pl r4
                 0x8901,      // bt early+0x08
                 0x000B,      // rts
                 0xE000,      //   mov #0,r0
                 0x7401,      // add #1,r4
                 0x6043,      // mov r4,r0
                 0x000B,      // rts
                 0x0009 });   //   nop
    // A switch the finder cannot follow (jmp @r5), then an rts, then more code.
    const uint32_t sw = 0x06030100u;
    put(sw, { 0x452B, 0x0009, 0x000B, 0x0009, 0x7001, 0x7001, 0x7001, 0x7001, 0x000B, 0x0009 });
    // .long 11 in a literal pool, after a bra over it.
    const uint32_t lit = 0x06030204u;
    put(lit - 4, { 0x000B, 0x0009 });
    put(lit, { 0xD001,      // mov.l @(4,pc),r0  (the literal at lit+8)
               0xA003,      // bra lit+0x0C
               0x0009,      //   nop
               0x0009 });   // (aligns the literal)
    dts::Put32(hw, lit + 8 - 0x06000000u, 11);
    put(lit + 0x0C, { 0x7001, 0x7001, 0x7001, 0x000B, 0x0009 });
    // f2 ends in a tail jump through a literal; f3 follows its pool.
    const uint32_t f2 = 0x06030304u, f3 = 0x06030310u;
    put(f2 - 4, { 0x000B, 0x0009 });
    put(f2, { 0xD101,      // mov.l @(4,pc),r1
              0x412B,      // jmp @r1
              0x0009,      //   nop
              0x0009 });
    dts::Put32(hw, f2 + 8 - 0x06000000u, 0x06030400u);
    put(f3, { 0x4F22, 0x7401, 0x7401, 0x4F26, 0x000B, 0x0009 });
    // mova of a table placed after the rts; the table's offsets decode as instructions.
    const uint32_t mv = 0x06030500u, afterTable = 0x0603050Cu;
    put(mv, { 0xC701,      // mova @(4,pc),r0  (the table at mv+8)
              0x000B,      // rts
              0x0009,      //   nop
              0x0009,
              0x0014,      // table: two 16-bit offsets
              0x0024 });
    put(afterTable, { 0x4F22, 0x7401, 0x7401, 0x4F26, 0x000B, 0x0009 });
    auto s = dts::MakeSnapshot(hw);

    // Flow from the symbol gets past its own early return: the symbol answers, not the scan.
    FinderHints sym;
    sym.symbols.insert(early);
    EntryGuess g = FindFunctionContaining(*s, early + 0x0A, sym);
    std::printf("  early return: %08X from %s%s%s\n", g.entry, EntrySourceName(g.source), g.doubt.empty() ? "" : ", ",
                g.doubt.c_str());
    CHECK(g.ok && g.entry == early && g.source == EntrySource::Symbol && g.doubt.empty());

    // Flow the finder cannot follow: the symbol is kept, the alternative said.
    FinderHints swSym;
    swSym.symbols.insert(sw);
    g = FindFunctionContaining(*s, sw + 0x0C, swSym);
    std::printf("  unfollowable: %08X from %s, %s\n", g.entry, EntrySourceName(g.source), g.doubt.c_str());
    CHECK(g.ok && g.entry == sw && g.source == EntrySource::Symbol);
    CHECK(g.doubt.find("0x06030108") != std::string::npos && g.confidence < 90);

    // The literal 11's low half is not an rts.
    uint32_t e = 0;
    std::string why, doubt;
    CHECK(RtsScanEntry(*s, lit + 0x10, e, &why, &doubt));
    std::printf("  .long 11: %08X\n", e);
    CHECK(e == lit && doubt.empty());

    // f2 never reaches f3: the entry is after its tail jump and pool.
    CHECK(RtsScanEntry(*s, f3 + 4, e, &why, &doubt));
    std::printf("  tail jump: %08X, %s\n", e, doubt.c_str());
    CHECK(e == f3 && doubt.find("never reaches") != std::string::npos);
    g = FindFunctionContaining(*s, f3 + 4, FinderHints());
    CHECK(g.ok && g.entry == f3 && g.source == EntrySource::RtsScan && !g.doubt.empty() && g.confidence < 60);

    // The mova table is skipped, and its guessed end said.
    CHECK(RtsScanEntry(*s, afterTable + 4, e, &why, &doubt));
    std::printf("  mova table: %08X, %s\n", e, doubt.c_str());
    CHECK(e == afterTable && doubt.find("jump table") != std::string::npos);
}

// The returns at the bottom of the scan window are candidates too: at the region's base, and at
// a span-limited floor.
void TestScanFloor()
{
    std::vector<uint8_t> hw;
    auto put = [&](uint32_t a, std::initializer_list<uint16_t> words) {
        size_t off = a - 0x06000000u;
        for (uint16_t w : words) { dts::Put16(hw, off, w); off += 2; }
    };
    put(0x06000000u, { 0x000B, 0x0009, 0x6043, 0x7001, 0x000B, 0x0009 });   // rts at the base
    auto s = dts::MakeSnapshot(hw);
    uint32_t e = 0;
    CHECK(RtsScanEntry(*s, 0x06000006u, e) && e == 0x06000004u);
    CHECK(RtsScanEntry(*s, 0x06000004u, e) && e == 0x06000004u);

    // An rts exactly kMaxFunctionSpan below the address, and one at floor + 2.
    std::vector<uint8_t> hw2;
    const uint32_t a = 0x06020000u;
    for (uint32_t rts : { a - kMaxFunctionSpan, a - kMaxFunctionSpan + 2 })
    {
        hw2.assign(0x30000, 0);
        for (uint32_t x = rts + 4; x < a + 8; x += 2) dts::Put16(hw2, x - 0x06000000u, 0x7001);   // add #1,r0
        dts::Put16(hw2, rts - 0x06000000u, 0x000B);
        dts::Put16(hw2, rts + 2 - 0x06000000u, 0x0009);
        auto s2 = dts::MakeSnapshot(hw2);
        std::string why;
        const bool ok = RtsScanEntry(*s2, a, e, &why);
        std::printf("  rts at floor%s: %s\n", rts == a - kMaxFunctionSpan ? "" : " + 2", ok ? "found" : why.c_str());
        CHECK(ok && e == rts + 4);
    }
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
    RUN(TestScanFloor());
    RUN(TestBoundaries());
    RUN(TestBoundsPersistence(scratch));
    dts::RemoveTree(scratch);
    return dts::Finish("Sh2FunctionFinderTests", start);
}
