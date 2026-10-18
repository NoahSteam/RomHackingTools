// Sh2Decompiler over synthetic snapshots (PLAN.md Step 2): what the engine wrapper produces
// for the hand-assembled, Capstone-verified fixtures, and that a result follows the bytes it
// was made from (A1).
//
//   fixture 1   a caller calling through a literal pool, the argument moved in the delay slot:
//               the callee is found and named, the literal folded, tokens carry addresses
//   fixture 1b  the same call into BIOS, which is never captured: an opaque call, not truncated
//   fixture 3   a PC-relative load in a delay slot (A3): bra/bsr fold the hardware's literal
//               (patch 0005), every other delayed branch is unsupported with neither folded
//   A1          an edited instruction word, an edited literal, a replaced BIOS image: new key,
//               new output
//
// usage: SaturnExplorerSh2DecompilerTests <scratch dir>
#include "DecompilerTestSupport.h"
#include "MappingOracle.h"

#include "AllocFailure.h"
#include "Decompiler/SaturnArchitecture.h"
#include "emulate.hh"    // the p-code emulator, for the call oracle
#include "Sh2EmulatorHooks.h"
#include "funcdata.hh"   // Funcdata, for the 0004 patch test
#include "memstate.hh"
#include "Decompiler/Sh2Decompiler.h"
#include "Decompiler/SaturnHardwareMap.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <regex>
#include <stdexcept>
#include <cstdlib>
#include <sstream>
#include <thread>

using namespace sfe::decomp;
using dts::MakeSnapshot;
using dts::MsSince;

namespace {

std::string gConfig;

bool Has(const std::string& text, const char* what) { return text.find(what) != std::string::npos; }

void Print(const char* label, const DecompileResult& r)
{
    std::printf("%s: status %d, %.1f ms, %u body bytes%s%s\n", label, int(r.status), r.elapsedMs, r.bodyBytes,
                r.truncated ? ", truncated" : "", r.unsupported ? ", unsupported" : "");
    if (!r.error.empty()) std::printf("  error: %s\n", r.error.c_str());
    if (!r.prototypeWithheld.empty()) std::printf("  signature withheld: %s\n", r.prototypeWithheld.c_str());
    for (const std::string& w : r.warnings) std::printf("  warning: %s\n", w.c_str());
    for (const MemoryAccess& m : r.accesses)
        std::printf("  access: %s %s %08X size %u at %08X%s%s printed %d time(s), %d line(s), %d token(s)\n", m.write ? "write" : "read",
                    m.direct ? "direct" : "via pointer", m.address, unsigned(m.size), m.insn, m.inLoop ? " in loop" : "",
                    m.returned ? " returned" : "", m.printed, m.printedLines, m.printedTokens);
    for (const AccessFate& f : r.accessFates)
    {
        static const char* const names[] = { "folded into ordinary memory", "dead", "removed with unreachable code", "the switch's table" };
        std::printf("  ended: %s of %u bytes at %08X (%s%08X): %s\n", f.write ? "write" : "read", unsigned(f.size), f.insn,
                    f.constantAddress ? "" : "through a pointer; ", f.address, names[int(f.fate)]);
    }
    // SE_GAME_TOKENS=1: every instruction a line came from, not just the first (the click goes to the first).
    static const bool allAddresses = std::getenv("SE_GAME_TOKENS") != nullptr;
    for (const Line& l : r.lines)
    {
        if (l.firstAddr && allAddresses)
        {
            std::printf("  %-50s ;", l.text.c_str());
            for (uint32_t a : LineInstructions(l)) std::printf(" %08X", a);
            std::printf("\n");
        }
        else if (l.firstAddr) std::printf("  %-50s ; %08X\n", l.text.c_str(), l.firstAddr);
        else std::printf("  %s\n", l.text.c_str());
    }
}

const Line* LineWith(const DecompileResult& r, const char* what)
{
    for (const Line& l : r.lines)
        if (Has(l.text, what)) return &l;
    return nullptr;
}

std::shared_ptr<MemorySnapshot> Fixture1Snapshot(uint32_t literal = dts::kFixture1Callee, uint64_t serial = 1)
{
    std::vector<uint8_t> hw;
    dts::PlaceFixture1(hw, literal);
    return MakeSnapshot(hw, serial);
}

DecompileRequest Request(uint32_t entry)
{
    DecompileRequest q;
    q.entry = entry;
    return q;
}

// Area 7 (MappingOracle.h) over the engine's fixtures, every run of ctest: the fixtures hold delay slots, a loop, a switch, a call, and
// literal loads, so every token address of their texts is checked against the body, and the lines several instructions made are
// marked. The game code does the same over every function (TestGameCode, TestGameLending). Also pins which instructions the
// fixture 1 lines come from.
void TestMappingOracleOnFixtures()
{
    std::vector<uint8_t> hw(0x100000u, 0);
    dts::PlaceFixture1(hw);
    dts::PlaceFixture3(hw);
    dts::PlaceDiamonds(hw, 3);
    dts::PlaceStraight(hw, 4);
    dts::PlaceJumpTable(hw, 4, 2);
    // A7-3, shrunk from generator seed 62956's first array read: mov.b @r4+,r0 ; mov.b @r4,r0 ; rts ; nop prints
    // `return (int4)(char)param_1[1];`, whose brackets are the pointer expression's op, so the load at 0x06070002 had no token.
    const uint32_t kArrayRead = 0x06070000u;
    for (uint32_t i = 0; i < 4; ++i) dts::Put16(hw, kArrayRead - 0x06000000u + 2 * i, uint16_t(i == 0 ? 0x6044 : i == 1 ? 0x6040 : i == 2 ? 0x000B : 0x0009));
    auto snap = MakeSnapshot(hw);
    Sh2Decompiler d(gConfig);
    const std::function<bool(uint32_t, uint16_t&)> wordAt = [&hw](uint32_t a, uint16_t& w) {
        if (a < 0x06000000u || a - 0x06000000u + 1 >= hw.size()) return false;
        w = static_cast<uint16_t>(hw[a - 0x06000000u] << 8 | hw[a - 0x06000000u + 1]);
        return true;
    };
    std::vector<uint32_t> entries = { dts::kFixture1Caller, dts::kFixture3Bra, dts::kFixture3Bfs, dts::kFixture3BsrMova,
                                      dts::kFixture3BsrLoad, dts::kFixture3BraTarget, dts::kFixture3SlotJumped,
                                      dts::kFixture3SwitchSlot, dts::kDiamondsBase, dts::kStraightBase, dts::kJumpTableBase, kArrayRead };
    for (const dts::Fixture3Case& c : dts::kFixture3Others) entries.push_back(c.entry);
    dts::MappingAudit all;
    for (uint32_t entry : entries)
    {
        const DecompileResult r = d.Decompile(snap, Request(entry));
        CHECK(r.status == DecompileStatus::Ok);
        char where[32];
        std::snprintf(where, sizeof where, "fixture %08X", entry);
        const dts::MappingAudit a = dts::AuditMapping(r, wordAt, where);
        if (a.Violations()) dts::PrintMappingAudit(a, where);
        all.Add(a);
    }
    dts::PrintMappingAudit(all, "fixtures");
    CHECK(all.Violations() == 0);
    // ...and the ledger names the line each access prints on (MemoryAccess::firstLine counted the document's leading break, A7-4)
    CHECK(all.accessOffLine == 0);
    CHECK(all.functions == static_cast<int>(entries.size()));
    CHECK(all.single > 10 && all.multi > 3);   // the fixtures hold both kinds of line, so both are checked

    const DecompileResult ra = d.Decompile(snap, Request(kArrayRead));
    const Line* arr = LineWith(ra, "param_1[1]");
    CHECK(ra.status == DecompileStatus::Ok && arr != nullptr);
    if (arr)
    {
        const std::vector<uint32_t> from = LineInstructions(*arr);
        CHECK(std::find(from.begin(), from.end(), kArrayRead + 2) != from.end());   // the load that reads param_1[1]
        // ...as an empty token: nothing is drawn for it
        bool empty = false;
        for (const Token& t : arr->tokens) empty = empty || (t.addr == kArrayRead + 2 && t.begin == t.end);
        CHECK(empty);
    }

    // Fixture 1: `if (0 < param_1)` is the cmp/pl and the bf it tests; the call is the jsr (its argument, in the delay slot, rides
    // with it); the line after it is the add; no line is from an instruction in a delay slot's own address.
    const DecompileResult r1 = d.Decompile(snap, Request(dts::kFixture1Caller));
    const Line* guard = LineWith(r1, "if (0 < param_1)");
    const Line* call = LineWith(r1, "func_0x06005000(");
    const Line* brace = LineWith(r1, "}");
    CHECK(guard && LineInstructions(*guard) == (std::vector<uint32_t>{ 0x06004008u, 0x0600400Au }));
    CHECK(call && LineInstructions(*call) == (std::vector<uint32_t>{ dts::kFixture1Jsr }));
    CHECK(brace && LineInstructions(*brace).empty() && brace->firstAddr == 0 && LineOriginText(*brace).empty());
    // A statement from two instructions names both, and the first is where a click goes.
    CHECK(guard && guard->firstAddr == 0x06004008u && Has(LineOriginText(*guard), "2 SH-2 instructions") &&
          Has(LineOriginText(*guard), "0x06004008, 0x0600400A"));
}

void TestFixture1()
{
    Sh2Decompiler d(gConfig);
    auto snap = Fixture1Snapshot();
    const DecompileResult r = d.Decompile(snap, Request(dts::kFixture1Caller));
    Print("fixture 1", r);
    const std::string c = r.Text();
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(r.entry == dts::kFixture1Caller);
    CHECK(r.name == "func_0x06004000");
    CHECK(Has(c, "func_0x06005000(param_1)"));   // the call through the literal pool, named
    CHECK(Has(c, "iVar1 + 1"));
    CHECK(Has(c, "0 < param_1"));                // the cmp/pl guard
    CHECK(!r.truncated && !r.unsupported);
    CHECK(r.callees == std::vector<uint32_t>{ dts::kFixture1Callee });
    CHECK(r.literalPools == std::vector<uint32_t>{ dts::kFixture1Literal });
    CHECK(r.key == snap->key);

    // Token addresses: the call line navigates to the jsr, the return to the rts, the
    // initialisation to its mov.
    const Line* call = LineWith(r, "func_0x06005000(");
    const Line* ret = LineWith(r, "return");
    const Line* init = LineWith(r, "iVar1 = 0;");
    CHECK(call && call->firstAddr == dts::kFixture1Jsr);
    CHECK(ret && ret->firstAddr == dts::kFixture1Rts);
    CHECK(init && init->firstAddr == dts::kFixture1MovZero);
    // The callee's name token carries the callee as its target.
    bool targetSeen = false;
    if (call)
        for (const Token& t : call->tokens)
            if (t.kind == TokenKind::FuncName && t.target == dts::kFixture1Callee) targetSeen = true;
    CHECK(targetSeen);
    // Every token's range lies inside its line.
    for (const Line& l : r.lines)
        for (const Token& t : l.tokens) CHECK(t.begin <= t.end && t.end <= l.text.size());

    // Without the second pass the call stays an opaque pointer: the pass is what names it.
    DecompileRequest one = Request(dts::kFixture1Caller);
    one.registerCallees = false;
    const DecompileResult r1 = d.Decompile(snap, one);
    CHECK(r1.status == DecompileStatus::Ok);
    CHECK(!Has(r1.Text(), "func_0x06005000("));
    CHECK(r1.callees == std::vector<uint32_t>{ dts::kFixture1Callee });   // still discovered

    // A FunctionNames rename is engine input.
    DecompileRequest named = Request(dts::kFixture1Caller);
    named.names.emplace_back(dts::kFixture1Callee, "ReadCounter");
    named.names.emplace_back(dts::kFixture1Caller, "Tick");
    const DecompileResult rn = d.Decompile(snap, named);
    CHECK(Has(rn.Text(), "ReadCounter(param_1)"));
    CHECK(rn.name == "Tick");
}

void TestFixture1b()
{
    Sh2Decompiler d(gConfig);
    const DecompileResult r = d.Decompile(Fixture1Snapshot(0x00000600u), Request(dts::kFixture1Caller));
    Print("fixture 1b", r);
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(Has(r.Text(), "(*(code *)0x600)(param_1)"));
    CHECK(!r.truncated);   // a call is not flow: nothing was cut short
}

void TestNoCodeAndMirror()
{
    Sh2Decompiler d(gConfig);
    auto snap = Fixture1Snapshot();
    const DecompileResult none = d.Decompile(snap, Request(0x003C3000u));   // the spike's slave PC
    std::printf("unmapped entry: %s\n", none.error.c_str());
    CHECK(none.status == DecompileStatus::NoCode);
    CHECK(none.lines.empty());
    const DecompileResult array = d.Decompile(snap, Request(0x66004000u));
    std::printf("cache address array entry: %s\n", array.error.c_str());
    CHECK(array.status == DecompileStatus::NoCode);
    CHECK(Has(array.error, "cache address array"));

    // The cache-through mirror and a later HWRAM repeat decompile to the same text, under the
    // canonical entry.
    const std::string base = d.Decompile(snap, Request(dts::kFixture1Caller)).Text();
    for (uint32_t mirror : { 0x26004000u, 0x07F04000u })
    {
        const DecompileResult m = d.Decompile(snap, Request(mirror));
        CHECK(m.status == DecompileStatus::Ok);
        CHECK(m.requestedEntry == mirror);
        CHECK(m.entry == dts::kFixture1Caller);
        CHECK(m.Text() == base);
    }
}

// Review at 6eaa133, finding 1: a mirror is the same bytes, not the same code. mova yields a
// pointer in the partition the code runs from, so a function that compares it with a
// cache-through literal returns 1 when it runs through 0x26xxxxxx and 0 when it runs canonical.
void TestMovaFollowsExecutionAddress()
{
    const uint32_t base = 0x06004800u;
    std::vector<uint8_t> hw;
    const size_t o = base - 0x06000000u;
    dts::Put16(hw, o + 0x0, 0xC702);   // mova @(8,pc),r0     -> base+0xC, in the partition run from
    dts::Put16(hw, o + 0x2, 0xD102);   // mov.l @(8,pc),r1    -> the literal at base+0xC
    dts::Put16(hw, o + 0x4, 0x3100);   // cmp/eq r0,r1
    dts::Put16(hw, o + 0x6, 0x000B);   // rts
    dts::Put16(hw, o + 0x8, 0x0029);   //  movt r0
    dts::Put16(hw, o + 0xA, 0x0009);   // nop
    dts::Put32(hw, o + 0xC, 0x26000000u | (base + 0xC));
    auto snap = MakeSnapshot(hw);
    Sh2Decompiler d(gConfig);
    const DecompileResult through = d.Decompile(snap, Request(0x26004800u));
    Print("mova via cache-through", through);
    CHECK(through.status == DecompileStatus::Ok && !through.unsupported);
    CHECK(Has(through.Text(), "return 1;"));
    CHECK(through.entry == base && through.requestedEntry == 0x26004800u);
    CHECK(Has(through.name, "06004800"));   // named by its canonical entry
    for (const Line& l : through.lines)
        CHECK(l.firstAddr == 0 || (l.firstAddr >= base && l.firstAddr < base + 0x10));   // canonical
    const DecompileResult canonical = d.Decompile(snap, Request(base));
    Print("mova canonical", canonical);
    CHECK(Has(canonical.Text(), "return 0;"));
    // Its canonical entry also known and named, as a user's function start and a rename leave it:
    // the mirror is still what runs, under that name, with nothing registered twice.
    DecompileRequest q = Request(0x26004800u);
    q.knownFunctions.push_back(base);
    q.names.emplace_back(base, "cmp_self");
    const DecompileResult named = d.Decompile(snap, q);
    Print("mova via cache-through, named", named);
    CHECK(Has(named.Text(), "return 1;") && named.name == "cmp_self");
    CHECK(named.warnings.empty());
}

// A3: a PC-relative load in a delay slot. The hardware reads relative to the branch target, which
// the unpatched spec does not; the fixture holds different literals at the two candidate addresses.
// Patch 0005 fixes the spec for bra and bsr, so those fold the hardware's literal; every other
// delayed branch is flagged with neither literal folded.
void TestDelaySlotPcRelative()
{
    Sh2Decompiler d(gConfig);
    std::vector<uint8_t> hw;
    dts::PlaceFixture3(hw);
    auto snap = MakeSnapshot(hw);
    auto hasValue = [](const std::string& text, const char* hex, const char* dec) {
        return Has(text, hex) || Has(text, dec);
    };

    // bra: the slot's load reads the hardware's literal.
    {
        const DecompileResult r = d.Decompile(snap, Request(dts::kFixture3Bra));
        Print("bra", r);
        const std::string text = r.Text();
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!r.unsupported);
        CHECK(hasValue(text, "0x22222222", "572662306"));
        CHECK(!hasValue(text, "0x11111111", "286331153"));
        for (const std::string& w : r.warnings) CHECK(!Has(w, "delay slot"));
    }
    // bsr: the slot's load (the call's argument) reads the hardware's literal, and the load after
    // the bsr + slot pair, reached on return, reads from its own PC: the slot's context does not
    // flow past the slot.
    {
        const DecompileResult r = d.Decompile(snap, Request(dts::kFixture3BsrLoad));
        Print("bsr + mov.l", r);
        const std::string text = r.Text();
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!r.unsupported);
        CHECK(hasValue(text, "0x34343434", "875836468"));
        CHECK(hasValue(text, "0x35353535", "892679477"));
        CHECK(!hasValue(text, "0x33333333", "858993459"));
    }
    // bra again, with a load at the target: 0x01000000 from the slot plus 0x234 from the target's
    // own PC. Context reaching the target would read 0x01000000 twice (0x2000000).
    {
        const DecompileResult r = d.Decompile(snap, Request(dts::kFixture3BraTarget));
        Print("bra + target load", r);
        const std::string text = r.Text();
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!r.unsupported);
        CHECK(hasValue(text, "0x1000234", "16777780"));
        CHECK(!hasValue(text, "0x2000000", "33554432"));
        CHECK(!hasValue(text, "0x33333333", "858993459"));
    }
    // bsr with mova in the slot: no longer flagged.
    {
        const DecompileResult r = d.Decompile(snap, Request(dts::kFixture3BsrMova));
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!r.unsupported);
    }

    // bf/s: whether the slot sees the target depends on the branch being taken, and so does the address it loads from: the output
    // computes it from the condition (patch 0050; before it the function was flagged and neither literal folded). Taken, the
    // hardware's literal; not taken, the slot's own PC.
    {
        const DecompileResult r = d.Decompile(snap, Request(dts::kFixture3Bfs));
        Print("bf/s", r);
        const std::string text = r.Text();
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!r.unsupported && r.unsupportedAt == 0);
        CHECK(Has(text, "0x6006114"));   // the address the taken branch gives the slot's load: the literal 0x22222222
        for (const std::string& w : r.warnings) CHECK(!Has(w, "delay slot"));
    }
    // A bra slot that something else also jumps straight to: the spec gives that address the slot's PC on both paths, so it is
    // flagged.
    struct Case { const char* name; uint32_t entry, slot; } cases[] = {
        { "bra slot jumped to", dts::kFixture3SlotJumped, dts::kFixture3SlotJumpedSlot },
        { "bra slot is a switch case", dts::kFixture3SwitchSlot, dts::kFixture3SwitchSlotSlot },
    };
    for (const Case& c : cases)
    {
        const DecompileResult r = d.Decompile(snap, Request(c.entry));
        Print(c.name, r);
        const std::string text = r.Text();
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(r.unsupported);
        CHECK(r.unsupportedAt == c.slot);
        CHECK(!hasValue(text, "0x11111111", "286331153"));   // the slot's own-PC literal
        CHECK(!hasValue(text, "0x22222222", "572662306"));   // the slot-relative literal
        CHECK(r.literalPools.empty());
        bool warned = false;
        for (const std::string& w : r.warnings) warned |= Has(w, "delay slot");
        CHECK(warned);
        CHECK(!r.prototype.known);   // not lent to callers either
    }
    // Every other delayed-branch kind (rts, jmp, braf, jsr), and mov.w in the slot: no longer flagged (A2-4, patch 0050). The load's
    // address is made from the branch's register, PR or target; the value is not folded to a literal (the decode oracle holds the p-code
    // to the hardware, and TestAreas234Leftovers runs these shapes on the emulator).
    for (const dts::Fixture3Case& c : dts::kFixture3Others)
    {
        const DecompileResult r = d.Decompile(snap, Request(c.entry));
        Print(c.name, r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!r.unsupported && r.unsupportedAt == 0);
        CHECK(!Has(r.Text(), "0x44444444") && !Has(r.Text(), "0x55555555") && !Has(r.Text(), "0x66666666"));
        // The address is the hardware's: rts + mov.w @(1,pc) loads from PR + 2 + 2 * 1, jsr + mov.l @(1,pc) from (r1 + 2 & ~3) + 4 * 1,
        // braf r1 + mov.l @(1,pc) from (r1 + the slot's address + 4 & ~3) + 4 * 1 (the target + 2).
        if (std::string(c.name) == "rts + mov.w") CHECK(Has(r.Text(), "unaff_retaddr + 4"));
        if (std::string(c.name) == "jsr + mov.l") CHECK(Has(r.Text(), "in_r1 + 2") && Has(r.Text(), "0xfffffffc) + 4"));
        if (std::string(c.name) == "braf + mov.l") CHECK(Has(r.Text(), "in_r1 + 0x6006506U") && Has(r.Text(), "0xfffffffc) + 4"));
    }
}

// A1: a result belongs to the bytes it was made from. An edit moves the key, and decompiling the
// edited snapshot shows the edit.
void TestEditsChangeKeyAndOutput()
{
    Sh2Decompiler d(gConfig);
    auto before = Fixture1Snapshot();
    const DecompileResult r0 = d.Decompile(before, Request(dts::kFixture1Caller));
    CHECK(Has(r0.Text(), "iVar1 + 1"));

    // add #1,r0 -> add #2,r0, as a Hex Editor write would leave it: same source, new serial.
    std::vector<uint8_t> hw;
    dts::PlaceFixture1(hw);
    dts::Put16(hw, dts::kFixture1AddOne - 0x06000000u, 0x7002);
    auto edited = MakeSnapshot(hw, 2);
    CHECK(edited->key != before->key);
    const DecompileResult r1 = d.Decompile(edited, Request(dts::kFixture1Caller));
    CHECK(Has(r1.Text(), "iVar1 + 2"));
    CHECK(!Has(r1.Text(), "iVar1 + 1"));
    CHECK(r1.key == edited->key);

    // The literal pointed at another function: the call follows it, and the new callee is the one
    // registered.
    const uint32_t other = 0x06005100u;
    std::vector<uint8_t> hw2;
    dts::PlaceFixture1(hw2, other);
    dts::Put16(hw2, other - 0x06000000u, 0x000B);       // rts
    dts::Put16(hw2, other - 0x06000000u + 2, 0xE007);   //   mov #7,r0
    auto relinked = MakeSnapshot(hw2, 3);
    const DecompileResult r2 = d.Decompile(relinked, Request(dts::kFixture1Caller));
    Print("literal edited", r2);
    CHECK(Has(r2.Text(), "func_0x06005100("));
    CHECK(!Has(r2.Text(), "func_0x06005000("));
    CHECK(r2.callees == std::vector<uint32_t>{ other });

    // A BIOS image replaced by another of the same size (as at the same path): a new key.
    std::vector<uint8_t> biosA(0x80000, 0x11), biosB(0x80000, 0x11);
    biosB[0x1234] = 0x22;
    auto keyWith = [&](const std::vector<uint8_t>& bios) {
        auto s = Fixture1Snapshot();
        auto image = BiosImage::Make(bios);
        MemoryRegion r;
        r.name = "BIOS";
        r.base = MemorySnapshot::kBiosBase;
        r.bytes = image->bytes;
        s->regions.push_back(r);
        s->biosHash = image->contentHash;
        s->key.regionSetHash = s->ComputeRegionSetHash();
        return s->key;
    };
    const SnapshotKey kA = keyWith(biosA), kA2 = keyWith(biosA), kB = keyWith(biosB);
    CHECK(kA == kA2);
    CHECK(kA != kB);
    CHECK(kA != before->key);   // adding the image at all is a new region set
}

// mov.w @(disp,PC): a 16-bit literal is folded like a 32-bit one (the spike's 2a: 7000).
void TestMovWFold()
{
    Sh2Decompiler d(gConfig);
    const uint32_t base = 0x06007000u;
    std::vector<uint8_t> hw;
    size_t off = base - 0x06000000u;
    for (uint16_t w : { 0x9002,     // mov.w @(4,pc),r0  -> the word at 0x06007008
                        0x000B,     // rts
                        0x304C,     //   add r4,r0      (slot)
                        0x0009,     // nop
                        0x1B58 })   // 0x06007008: .word 7000
    { dts::Put16(hw, off, w); off += 2; }
    const DecompileResult r = d.Decompile(MakeSnapshot(hw), Request(base));
    Print("mov.w fold", r);
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(Has(r.Text(), "7000"));
    CHECK(r.literalPools == std::vector<uint32_t>{ base + 8 });
}

// Step 4: a hardware register reached through a folded literal prints under its name, whichever
// partition the code uses and whatever its width. Encodings checked with Capstone (SH2, BE).
void TestHardwareRegisterNames()
{
    Sh2Decompiler d(gConfig);
    std::vector<uint8_t> hw;
    struct Case { uint32_t base; uint16_t op; uint32_t pointer; const char* name; } cases[] = {
        // mov.l @(8,pc),r1 ; mov #1,r0 ; mov.w r0,@r1 ; rts ; nop -- a 16-bit write, cache-through
        { 0x06007000u, 0x2101, 0x25F80000u, "VDP2_TVMD = 1;" },
        // mov.l @(8,pc),r1 ; mov.b @r1,r0 ; rts ; nop -- an SMPC byte register, cache-through
        { 0x06007100u, 0x6010, 0x20100063u, "SMPC_SF" },
        // mov.l @(8,pc),r1 ; mov.l @r1,r0 ; rts ; nop -- a 32-bit SCU register, cached partition
        { 0x06007200u, 0x6012, 0x05FE00A4u, "SCU_IST" },
    };
    for (const Case& c : cases)
    {
        size_t off = c.base - 0x06000000u;
        const bool write = c.op == 0x2101;
        for (uint16_t w : { uint16_t(0xD102), uint16_t(write ? 0xE001 : c.op), uint16_t(write ? c.op : 0x000B),
                            uint16_t(write ? 0x000B : 0x0009), uint16_t(0x0009), uint16_t(0x0009) })
        { dts::Put16(hw, off, w); off += 2; }
        dts::Put32(hw, c.base + 0x0C - 0x06000000u, c.pointer);
    }
    auto snap = MakeSnapshot(hw);
    for (const Case& c : cases)
    {
        const DecompileResult r = d.Decompile(snap, Request(c.base));
        Print(c.name, r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(Has(r.Text(), c.name));
    }

    CHECK(FindHardwareRegister(0x05F80001u) && std::string(FindHardwareRegister(0x05F80001u)->name) == "VDP2_TVMD");
}

// Naming a register must not turn it into memory. Every access the code makes stays in the
// pseudocode, in order: a write before a poll, the poll itself, a write the next one overwrites,
// a read nobody uses, two reads of a counter, the SH-2 divider's quotient, a write wider than one
// register and a register reached through a mirror. Before the registers were volatile the poll
// printed as do {} while (true), reading SCU_IST back as "return param_1" and the counter
// difference as "return 0".
size_t Count(const std::string& text, const char* what)
{
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
    return n;
}

// A note containing 'what', of the given kind: a caution (amber, in warnings) or an explanation.
bool HasNote(const DecompileResult& r, const char* what, bool caution = true)
{
    for (const HardwareNote& n : r.hardwareNotes)
        if (Has(n.text, what) && n.caution == caution) return true;
    return false;
}

// ...and on this instruction.
bool HasNoteAt(const DecompileResult& r, uint32_t insn, const char* what, bool caution = true)
{
    for (const HardwareNote& n : r.hardwareNotes)
        if (n.address == insn && Has(n.text, what) && n.caution == caution) return true;
    return false;
}

bool WarnsAbout(const DecompileResult& r, const char* what)
{
    for (const std::string& w : r.warnings)
        if (Has(w, what)) return true;
    return false;
}

// The output says, on this instruction, that its access is missing because the code around it was
// removed as unreachable: the caution itself, not any warning that happens to print the address
// (the engine prints its own in lowercase, and an address of digits alone matches either).
int SaidCount(const DecompileResult& r, uint32_t insn)
{
    int n = 0;
    for (const HardwareNote& h : r.hardwareNotes)
        n += h.address == insn && h.caution && Has(h.text, "here is missing");
    return n;
}

bool SaidMissing(const DecompileResult& r, uint32_t insn) { return SaidCount(r, insn) > 0; }

// The access ledger (consolidated review): the memory operations the SH-2 code makes, written
// from the instructions, compared with DecompileResult::accesses in instruction order -- kind,
// address (0: through a pointer), width, whether it is in a loop, whether it is the value
// returned -- and each one printed on exactly one line. A substring check passes when an access is
// lost or printed twice; this does not.
// Two reads in one statement are in an order C leaves open -- unless one feeds the other
// (a pointer read and the read through it), which orders them.
bool Unordered(const MemoryAccess& a, const MemoryAccess& b)
{
    if (a.write || b.write || a.statement != b.statement) return false;
    auto feeds = [](const MemoryAccess& x, const MemoryAccess& y) {
        return std::find(x.through.begin(), x.through.end(), y.id) != x.through.end();
    };
    return !feeds(a, b) && !feeds(b, a);
}

// Reachability over the flow's blocks as first followed (DecompileResult::flowSuccessors): the
// reference for the order two accesses can be made in. The final blocks are joined, split and
// copied by the engine, so op ids, addresses and output lines all mislead (review of 0e3cd12).
struct FlowReach
{
    std::vector<std::vector<char>> reach;    // reach[x][y]: y runs after x on some path
    std::vector<std::vector<char>> uncond;   // uncond[x][y]: ...and on every path, through blocks with one way out
    explicit FlowReach(const std::vector<std::vector<int>>& succ)
    {
        const size_t n = succ.size();
        reach.assign(n, std::vector<char>(n, 0));
        uncond.assign(n, std::vector<char>(n, 0));
        for (size_t x = 0; x < n; ++x)
        {
            std::vector<int> todo(succ[x].begin(), succ[x].end());
            while (!todo.empty())
            {
                const int y = todo.back();
                todo.pop_back();
                if (y < 0 || size_t(y) >= n || reach[x][y]) continue;
                reach[x][y] = 1;
                todo.insert(todo.end(), succ[y].begin(), succ[y].end());
            }
            for (size_t y = x; succ[y].size() == 1;)
            {
                const int z = succ[y][0];
                if (z < 0 || size_t(z) >= n || uncond[x][z] || size_t(z) == x) break;
                uncond[x][z] = 1;
                y = size_t(z);
            }
        }
    }
};

// Whether the code can make 'a' before 'b'. Unknown origins (ops the engine made later) are not judged.
bool FlowOrdered(const FlowReach& fr, const MemoryAccess& a, const MemoryAccess& b)
{
    if (a.origBlock < 0 || b.origBlock < 0) return true;
    if (a.origBlock == b.origBlock) return a.origOrder < b.origOrder;
    return fr.reach[a.origBlock][b.origBlock] != 0;
}

// Two accesses in one final block, printed in an order the code cannot make them in.
bool OutOfFlowOrder(const FlowReach& fr, const MemoryAccess& a, const MemoryAccess& b)
{
    if (a.block < 0 || a.block != b.block) return false;
    return a.order < b.order ? !FlowOrdered(fr, a, b) : !FlowOrdered(fr, b, a);
}

// Two accesses from blocks the flow kept apart by a branch, now in one block that ends in a switch:
// a guard folded into the switch over a read, which then appears made for the guarded-out values.
bool FoldedIntoSwitch(const FlowReach& fr, const MemoryAccess& a, const MemoryAccess& b)
{
    if (a.block < 0 || a.block != b.block || !a.inSwitchBlock || a.origBlock < 0 || b.origBlock < 0 || a.origBlock == b.origBlock)
        return false;
    return !fr.uncond[a.origBlock][b.origBlock] && !fr.uncond[b.origBlock][a.origBlock];
}

// A read printed where its value is used, later in its block, with another access or a call made in
// between: it is shown after them.
bool PrintedPast(const MemoryAccess& m, const std::vector<MemoryAccess>& accesses, const std::vector<CallSite>& calls,
                 std::string* what)
{
    if (m.write || m.statement == m.id || m.block < 0 || m.block != m.statementBlock) return false;
    for (const MemoryAccess& o : accesses)
        if (o.id != m.id && o.block == m.block && o.order > m.order && o.order < m.statementOrder &&
            std::find(m.through.begin(), m.through.end(), o.id) == m.through.end())
        {
            if (what) *what = "the access at " + [&] { char b[12]; std::snprintf(b, sizeof b, "%08X", o.insn); return std::string(b); }();
            return true;
        }
    for (const CallSite& c : calls)
        if (c.block == m.block && c.order > m.order && c.order < m.statementOrder)
        {
            if (what) *what = "the call at " + [&] { char b[12]; std::snprintf(b, sizeof b, "%08X", c.insn); return std::string(b); }();
            return true;
        }
    return false;
}

// How many of the engine's records are of this fate on this instruction and direction (insn 0:
// any instruction).
int FateAt(const DecompileResult& r, AccessFate::Kind fate, uint32_t insn, bool write)
{
    int n = 0;
    for (const AccessFate& f : r.accessFates)
        n += f.fate == fate && (insn == 0 || f.insn == insn) && f.write == write;
    return n;
}

// The conservation law (program phase 0): every memory access the flow makes is accounted for
// exactly once. The reference run (every address volatile) lists each one the flow reaches; the
// shown output accounts for an access of it -- found by instruction, direction and width -- as
//   Printed: it is in r.accesses;
//   Folded: the engine made it a direct access of ordinary memory (a variable the analysis may
//     forward or drop), or it was the read of a recovered switch's table (the switch stands for it);
//   Removed: it went with a block the engine removed as unreachable;
//   Dropped: it ended dead, or the engine has no record of it. That is a lost access.
// A Removed access must be said by the removal caution unless it was a direct access of ordinary
// memory, which is no loss. 'marked' says whether an address is memory the request marks volatile.
enum class Account { Printed, Folded, Removed, Dropped };
const char* AccountName(Account a)
{
    switch (a)
    {
    case Account::Printed: return "printed";
    case Account::Folded: return "folded";
    case Account::Removed: return "removed";
    default: return "dropped";
    }
}

// How the shown output accounts for each access of the reference run, one to one (A5-5). An access
// of the shown output, or a record of one that went, pairs with a reference access of the same
// instruction, direction and width -- and, when both have a constant address, the same address --
// and with no other: two accesses of one instruction (mac's operands; an rte's stack pops beside the
// read in its slot) must not excuse each other. Pairs by address are made before the rest, so an
// access through a pointer does not take the record of a constant one. A fold into the function's
// own frame (AccessFate::frame) is a variable, no bus access, and pairs with nothing; its
// 'address' is a frame offset (-4 for a pop), not an address.
std::vector<Account> Accounts(const DecompileResult& r, const std::vector<MemoryAccess>& reference,
                              const std::function<bool(uint32_t)>& marked)
{
    std::vector<Account> out(reference.size(), Account::Dropped);
    std::vector<char> done(reference.size(), 0), shownUsed(r.accesses.size(), 0), fateUsed(r.accessFates.size(), 0);
    for (int byAddress = 1; byAddress >= 0; --byAddress)
        for (size_t i = 0; i < reference.size(); ++i)
        {
            if (done[i]) continue;
            const MemoryAccess& m = reference[i];
            // Whether a candidate with (known, address) pairs with m in this round.
            auto pairs = [&](bool known, uint32_t address) {
                const bool both = m.direct && known;
                return byAddress ? both && address == m.address : !both;
            };
            for (size_t k = 0; k < r.accesses.size() && !done[i]; ++k)
            {
                const MemoryAccess& x = r.accesses[k];
                if (shownUsed[k] || x.insn != m.insn || x.write != m.write || x.size != m.size || !pairs(x.direct, x.address)) continue;
                shownUsed[k] = done[i] = 1;
                out[i] = Account::Printed;
            }
            // A variable or a switch's table (conserved) before code removed as unreachable.
            for (int removed = 0; removed < 2 && !done[i]; ++removed)
                for (size_t k = 0; k < r.accessFates.size() && !done[i]; ++k)
                {
                    const AccessFate& f = r.accessFates[k];
                    if (fateUsed[k] || f.frame || f.insn != m.insn || f.write != m.write || f.size != m.size) continue;
                    if (!pairs(f.constantAddress, f.address)) continue;
                    if (!removed)
                    {
                        // A fold of a register or of marked memory would have been a real access: it is not ordinary.
                        if (f.fate == AccessFate::FoldedOrdinary && f.constantAddress && (InHardwareRange(f.address) || marked(f.address)))
                            continue;
                        if (f.fate != AccessFate::FoldedOrdinary && f.fate != AccessFate::SwitchLookup) continue;
                        out[i] = Account::Folded;
                    }
                    else
                    {
                        if (f.fate != AccessFate::Unreachable) continue;
                        out[i] = Account::Removed;
                    }
                    fateUsed[k] = done[i] = 1;
                }
        }
    return out;
}

// A Removed access that needs no word: a direct access of ordinary memory.
bool OrdinaryDirect(const MemoryAccess& m, const std::function<bool(uint32_t)>& marked)
{
    return m.direct && !InHardwareRange(m.address) && !marked(m.address);
}

// The other half of the conservation law, which does not need the reference run. An access that
// ended dead is a loss unless it is a direct access of ordinary memory (a variable the analysis may
// drop: an unused global, a literal-pool word, a store the function overwrites): a read of a
// register, of marked memory or through a pointer is kept for its effect, because it may be a
// register's. The reference run cannot see such a loss when the engine drops the access in both
// runs alike, as it did for a read whose value only fed an expression that folded (A5-1, A4-1).
bool DeadIsLoss(const AccessFate& f, const std::function<bool(uint32_t)>& marked)
{
    // A record in the function's own frame has an offset for an address (-4 reads as a register).
    return f.fate == AccessFate::Dead && !f.frame &&
           !(f.constantAddress && !InHardwareRange(f.address) && !marked(f.address));
}

// The ledger's reference: the same request with every address volatile, so the output holds every
// memory instruction the flow reaches. Each access there must be accounted for in the ordinary
// output (Accounts); a lost one, or a removal the output does not say, fails. So does any access
// the shown run's own record says ended dead that is not a direct access of ordinary memory
// (DeadIsLoss), the reference run's list or not.
bool ReferenceOk(Sh2Decompiler& d, const std::shared_ptr<MemorySnapshot>& snap, const DecompileRequest& q,
                 const DecompileResult& r)
{
    DecompileRequest ref = q;
    ref.allMemoryVolatile = true;
    const DecompileResult rr = d.Decompile(snap, ref);
    if (rr.status != DecompileStatus::Ok)
    {
        std::printf("  reference run: status %d (%s): nothing to compare with\n", int(rr.status), rr.error.c_str());
        return false;
    }
    std::vector<HardwareRange> user;
    for (const auto& v : q.volatileMemory)
        for (const HardwareRange& h : VolatileAliases(v.first, v.second)) user.push_back(h);
    for (const auto& h : r.aliasedMemory) user.push_back(HardwareRange{ h.first, h.second });   // memory the host made volatile (A5-52)
    auto marked = [&](uint32_t a) {
        for (const HardwareRange& h : user)
            if (h.first <= a && a <= h.last) return true;
        return false;
    };
    bool ok = true;
    const std::vector<Account> accounts = Accounts(r, rr.accesses, marked);
    std::map<uint32_t, int> said;   // the removal cautions on an instruction, spent one per removed access
    for (size_t i = 0; i < rr.accesses.size(); ++i)
    {
        const MemoryAccess& m = rr.accesses[i];
        const Account a = accounts[i];
        if (a == Account::Printed || a == Account::Folded) continue;
        if (a == Account::Removed && (OrdinaryDirect(m, marked) || ++said[m.insn] <= SaidCount(r, m.insn))) continue;
        std::printf("  reference: the %s of %u bytes at %08X (%s%08X) is %s in the output\n", m.write ? "write" : "read",
                    unsigned(m.size), m.insn, m.direct ? "" : "through a pointer; ", m.address,
                    a == Account::Removed ? "removed as unreachable, and its removal is not said" : "dropped");
        ok = false;
    }
    // ...and the shown run's own record: what ended dead, and was not a variable of ordinary memory,
    // is a loss whether or not the reference run lists it (it may have lost the access alike).
    for (const AccessFate& f : r.accessFates)
        if (DeadIsLoss(f, marked))
        {
            std::printf("  conservation: the %s of %u bytes at %08X (%s%08X) ended dead, and is no direct access of ordinary memory\n",
                        f.write ? "write" : "read", unsigned(f.size), f.insn, f.constantAddress ? "" : "through a pointer; ",
                        f.address);
            ok = false;
        }
    return ok;
}

struct Op
{
    bool write;
    uint32_t address;   // 0: through a pointer
    uint8_t size;
    bool loop;
    bool returned;
};
Op Rd(uint32_t a, uint8_t size, bool loop = false, bool returned = false) { return Op{ false, a, size, loop, returned }; }
Op Wr(uint32_t a, uint8_t size, bool loop = false) { return Op{ true, a, size, loop, false }; }

bool Ledger(const DecompileResult& r, const std::vector<Op>& want)
{
    std::vector<MemoryAccess> got = r.accesses;
    // In instruction order; one instruction's own accesses (tas.b reads then writes) in op order.
    std::stable_sort(got.begin(), got.end(), [](const MemoryAccess& x, const MemoryAccess& y) {
        return x.insn != y.insn ? x.insn < y.insn : x.order < y.order;
    });
    bool ok = got.size() == want.size();
    for (size_t i = 0; ok && i < got.size(); ++i)
    {
        const MemoryAccess& g = got[i];
        const Op& w = want[i];
        ok = g.write == w.write && g.direct == (w.address != 0) && g.address == w.address && g.size == w.size &&
             g.inLoop == w.loop && g.returned == w.returned && g.printed == 1 && g.printedLines >= 1 && !g.repeatedOnPath &&
             !g.conditionalInPrint;
    }
    // No two reads in one statement (C leaves their order open); within one final block the
    // accesses must be in an order the code can make them in, read from the flow's own blocks
    // (FlowReach); two blocks the flow kept apart by a branch are not joined into a switch's; and a
    // read printed where its value is used does not pass another access or a call on the way.
    const FlowReach fr(r.flowSuccessors);
    for (size_t i = 0; ok && i < got.size(); ++i)
        for (size_t j = i + 1; ok && j < got.size(); ++j)
        {
            const MemoryAccess& a = got[i];
            const MemoryAccess& b = got[j];
            if (Unordered(a, b))
            {
                std::printf("  ledger: %08X and %08X are read in one statement\n", a.insn, b.insn);
                ok = false;
            }
            else if (OutOfFlowOrder(fr, a, b))
            {
                std::printf("  ledger: %08X and %08X are out of the code's order in block %d\n", a.insn, b.insn, a.block);
                ok = false;
            }
            else if (FoldedIntoSwitch(fr, a, b))
            {
                std::printf("  ledger: %08X and %08X were joined into a switch's block across a branch\n", a.insn, b.insn);
                ok = false;
            }
        }
    for (const MemoryAccess& m : got)
    {
        std::string what;
        if (ok && PrintedPast(m, got, r.calls, &what))
        {
            std::printf("  ledger: the read at %08X is printed past %s\n", m.insn, what.c_str());
            ok = false;
        }
    }
    if (!ok)
    {
        std::printf("  ledger mismatch, expected:\n");
        for (const Op& w : want)
            std::printf("    %s %08X size %u%s%s\n", w.write ? "write" : "read", w.address, unsigned(w.size),
                        w.loop ? " in loop" : "", w.returned ? " returned" : "");
    }
    return ok;
}

// A mailbox: one write, then one read per pass through the poll. The value read is the loop's condition and nothing returns it (a poll
// is void: the nop in rts's slot used to write r0 and make the last read the function's return, patches/0045).
std::vector<Op> Mailbox(uint32_t a) { return { Wr(a, 2), Rd(a, 2, true) }; }

void TestHardwareVolatility()
{
    Sh2Decompiler d(gConfig);
    std::vector<uint8_t> hw;
    // Each fixture: its instructions from 'base', then the register's address as a literal at
    // base + literal (the first instruction is mov.l @(disp,pc),r1 loading it).
    struct Fixture { uint32_t base; std::vector<uint16_t> code; uint32_t literal; uint32_t pointer; };
    const Fixture poll = { 0x06009000u, { 0xD103,     // mov.l @(12,pc),r1   -> SMPC_SF
                                          0xE001,     // mov #1,r0
                                          0x2100,     // mov.b r0,@r1        SMPC_SF = 1
                                          0x6010,     // loop: mov.b @r1,r0
                                          0xC801,     // tst #1,r0
                                          0x8BFC,     // bf loop             while (SMPC_SF & 1)
                                          0x000B, 0x0009 }, 0x10, 0x20100063u };
    const Fixture echo = { 0x06009040u, { 0xD102,     // mov.l @(8,pc),r1    -> SCU_IST
                                          0x2142,     // mov.l r4,@r1        SCU_IST = param_1
                                          0x6012,     // mov.l @r1,r0        return SCU_IST
                                          0x000B, 0x0009, 0x0009 }, 0x0C, 0x25FE00A4u };
    const Fixture twice = { 0x06009080u, { 0xD103,    // mov.l @(12,pc),r1   -> VDP2_TVMD
                                           0xE000,    // mov #0,r0
                                           0x2101,    // mov.w r0,@r1        VDP2_TVMD = 0
                                           0xE001,    // mov #1,r0
                                           0x2101,    // mov.w r0,@r1        VDP2_TVMD = 1
                                           0x000B, 0x0009, 0x0009 }, 0x10, 0x25F80000u };
    const Fixture dummy = { 0x060090C0u, { 0xD102,    // mov.l @(8,pc),r1    -> SCU_IST
                                           0x6212,    // mov.l @r1,r2        a read nobody uses
                                           0xE200,    // mov #0,r2
                                           0x2122,    // mov.l r2,@r1        SCU_IST = 0
                                           0x000B, 0x0009 }, 0x0C, 0x25FE00A4u };
    const Fixture counter = { 0x06009100u, { 0xD102,  // mov.l @(8,pc),r1    -> VDP2_VCNT
                                             0x6211,  // mov.w @r1,r2
                                             0x6011,  // mov.w @r1,r0
                                             0x3028,  // sub r2,r0           return second - first
                                             0x000B, 0x0009 }, 0x0C, 0x25F8000Au };
    const Fixture divide = { 0x06009140u, { 0xD102,   // mov.l @(8,pc),r1    -> SH2_DVSR
                                            0x2152,   // mov.l r5,@r1        DVSR = param_2
                                            0x1141,   // mov.l r4,@(4,r1)    DVDNT = param_1: divides
                                            0x5011,   // mov.l @(4,r1),r0    the quotient
                                            0x000B, 0x0009 }, 0x0C, 0xFFFFFF00u };
    const Fixture wide = { 0x06009180u, { 0xD102,     // mov.l @(8,pc),r1    -> VDP2_SCXIN0
                                          0x2142,     // mov.l r4,@r1        SCXIN0:SCXDN0 = param_1
                                          0x2152,     // mov.l r5,@r1        ... = param_2
                                          0x000B, 0x0009, 0x0009 }, 0x0C, 0x25F80070u };
    const Fixture mirror = { 0x060091C0u, poll.code, 0x10, 0x201000E3u };   // SMPC_SF, 128 bytes up
    const Fixture woRead = { 0x06009200u, { 0xD102,   // mov.l @(8,pc),r1    -> VDP1_FBCR
                                            0x2141,   // mov.w r4,@r1        VDP1_FBCR = param_1
                                            0x6011,   // mov.w @r1,r0        reads a write-only register
                                            0x000B, 0x0009, 0x0009 }, 0x0C, 0x25D00002u };
    const Fixture roWrite = { 0x06009240u, { 0xD101,  // mov.l @(4,pc),r1    -> VDP1_EDSR
                                             0x2141,  // mov.w r4,@r1        writes a read-only one
                                             0x000B, 0x0009 }, 0x08, 0x25D00010u };
    // An unused read whose address only becomes constant late: the engine removed it as dead
    // before the xor folded (review of 20bfde7).
    const Fixture late = { 0x06009280u, { 0xD103,     // mov.l @(12,pc),r1   -> SMPC_SF
                                          0x6243,     // mov r4,r2
                                          0x222A,     // xor r2,r2           r2 = 0, once folded
                                          0x312C,     // add r2,r1
                                          0x6310,     // mov.b @r1,r3        reads SMPC_SF
                                          0xE300,     // mov #0,r3           the value is unused
                                          0x000B, 0x0009 }, 0x10, 0x20100063u };
    // ...and one whose address never becomes constant: kept, since it may be a register.
    const Fixture indexed = { 0x060092C0u, { 0xD102,  // mov.l @(8,pc),r1    -> 0x20100063
                                             0x314C,  // add r4,r1           plus a runtime offset
                                             0x6210,  // mov.b @r1,r2
                                             0xE200,  // mov #0,r2           the value is unused
                                             0x000B, 0x0009 }, 0x0C, 0x20100063u };
    // The policy's other side: once the address folds to ordinary memory the unused read goes.
    const Fixture lateRam = { 0x06009300u, late.code, 0x10, 0x26070000u };
    // mov.l @(disp,pc),r1 -> 'pointer'; 'ops'; rts; nop; the literal after them, word-aligned.
    auto simple = [](uint32_t base, std::vector<uint16_t> ops, uint32_t pointer) {
        std::vector<uint16_t> code = { 0 };
        code.insert(code.end(), ops.begin(), ops.end());
        code.push_back(0x000B);
        code.push_back(0x0009);
        if (code.size() % 2) code.push_back(0x0009);
        const uint32_t literal = uint32_t(code.size()) * 2;
        code[0] = uint16_t(0xD100 | ((literal - 4) / 4));
        return Fixture{ base, code, literal, pointer };
    };
    const Fixture mvolRead = simple(0x06009340u, { 0x6011 }, 0x25B00400u);          // mov.w @r1,r0
    const Fixture mibufWrite = simple(0x06009380u, { 0x2141 }, 0x25B00404u);        // mov.w r4,@r1
    const Fixture wtcnt = simple(0x060093C0u, { 0xE05A, 0x4018, 0x2101 }, 0xFFFFFE80u);   // mov #0x5A,r0; shll8 r0; mov.w r0,@r1
    const Fixture wtcsr = simple(0x06009400u, { 0xE0A5, 0x4018, 0x2101 }, 0xFFFFFE80u);   // key 0xA5
    const Fixture wovf = simple(0x06009440u, { 0xE0A5, 0x4018, 0x2101 }, 0xFFFFFE82u);    // 0xA500 at FE82
    const Fixture badKey = simple(0x06009480u, { 0xE012, 0x4018, 0x2101 }, 0xFFFFFE80u);  // key 0x12
    const Fixture keyVar = simple(0x060094C0u, { 0x2141 }, 0xFFFFFE80u);                  // mov.w r4,@r1
    const Fixture divMirror = simple(0x06009500u, { 0x6012 }, 0xFFFFFF24u);         // mov.l @r1,r0
    const Fixture purge5 = simple(0x06009540u, { 0x6011 }, 0xA5F80000u);            // mov.w @r1,r0
    const Fixture oddPair = simple(0x06009580u, { 0x2142 }, 0x25F80000u);           // mov.l r4,@r1: TVMD:EXTEN
    const Fixture ubcPair = simple(0x060095C0u, { 0x2142 }, 0xFFFFFF40u);           // mov.l r4,@r1: BARAH:BARAL
    // A mailbox: set a flag, wait for the other CPU to clear it. In sound RAM always kept; in work
    // RAM only when the user marks it volatile.
    const std::vector<uint16_t> mailboxOps = { 0xE001,    // mov #1,r0
                                               0x2101,    // mov.w r0,@r1
                                               0x6011,    // loop: mov.w @r1,r0
                                               0xC801,    // tst #1,r0
                                               0x8BFC };  // bf loop
    const Fixture soundMailbox = simple(0x06009600u, mailboxOps, 0x25A01000u);
    const Fixture ramMailbox = simple(0x06009640u, mailboxOps, 0x26001000u);
    // The same mailbox through work RAM's other addresses: its 1 MiB repeat, bit 27.
    const Fixture repeatMailbox = simple(0x06009680u, mailboxOps, 0x26101000u);
    const Fixture farMailbox = simple(0x060096C0u, mailboxOps, 0x26801000u);
    const Fixture bit27Mailbox = simple(0x06009700u, mailboxOps, 0x2E001000u);
    // Ranges crossing a region or bus-period boundary, or spanning whole periods, still reach the
    // mailbox through its other addresses (review of 80620fb).
    const Fixture hwramMirrorMailbox = simple(0x06009880u, mailboxOps, 0x260FFFFCu);
    // A switch's guard and its block (review of 0e3cd12). The engine folds a guard branch into the
    // switch as its default case when the switch block holds no statement; a read in that block
    // then prints before the switch, as if made for the guarded-out values too. Four fixtures: the
    // read in the switch block (unused, so not a statement to the engine), the read in the jump's
    // delay slot, the control with the table load only (still folded), and a switch on a second
    // read of the pointer the guard tested (two reads, not one). Each has a 4-entry table at +0x28
    // and cases returning 0..3 at +0x28, +0x2C, +0x30, +0x34.
    auto switchFixture = [](uint32_t base, std::vector<uint16_t> head) {
        // head: the 10 words from the selector read to the jump's delay slot (0x00-0x12).
        std::vector<uint16_t> code = head;
        code.insert(code.end(), { 0x000B, 0xE0FF });   // 0x14 default: rts; mov #-1,r0
        for (uint32_t i = 0; i < 4; ++i)               // 0x18 the table
        {
            const uint32_t target = base + 0x28 + 4 * i;
            code.push_back(uint16_t(target >> 16));
            code.push_back(uint16_t(target));
        }
        code.insert(code.end(), { 0x000B, 0xE000, 0x000B, 0xE001, 0x000B, 0xE002, 0x000B, 0xE003 });
        return Fixture{ base, code, 0x3C, 0 };   // no literal of its own (the table is in the code)
    };
    const Fixture switchRead = switchFixture(0x0600A300u, { 0x6442,     // mov.l @r4,r4   the selector
                                                            0xE103,     // mov #3,r1
                                                            0x3416,     // cmp/hi r1,r4
                                                            0x8905,     // bt default
                                                            0x6352,     // mov.l @r5,r3   the read, guarded; unused
                                                            0xC703,     // mova table,r0
                                                            0x4408,     // shll2 r4
                                                            0x014E,     // mov.l @(r0,r4),r1
                                                            0x412B,     // jmp @r1
                                                            0x0009 });  //   nop
    const Fixture switchSlotRead = switchFixture(0x0600A340u, { 0x6442, 0xE103, 0x3416, 0x8905, 0xC703, 0x4408, 0x014E,
                                                                0x412B,
                                                                0x6352,     //   mov.l @r5,r3 in the slot
                                                                0x0009 });
    const Fixture switchFold = switchFixture(0x0600A380u, { 0x6442, 0xE103, 0x3416, 0x8905, 0xC703, 0x4408, 0x014E,
                                                            0x412B, 0x0009, 0x0009 });
    const Fixture switchSecondRead = switchFixture(0x0600A3C0u, { 0x6142,     // mov.l @r4,r1   the guard's read
                                                                  0xE203,     // mov #3,r2
                                                                  0x3126,     // cmp/hi r2,r1
                                                                  0x8905,     // bt default
                                                                  0x6142,     // mov.l @r4,r1   read again: the selector
                                                                  0xC703, 0x4108,
                                                                  0x011E,     // mov.l @(r0,r1),r1
                                                                  0x412B, 0x0009 });
    // A switch whose table read is used after it (review of the generator's second grammar, A4-3). The
    // bound check folds into the switch as its default and the table read would stand for the lookup,
    // but the join after the switch uses the register the entry was loaded into (2 from the default
    // path, the entry from the cases), so the read is the code's own and it is made only for an
    // index in range. Folded, it printed ahead of the switch and made for every index. Three cases
    // that do nothing, each a 'bra join'; the join stores the low byte of r1.
    const Fixture switchUsed = { 0x0600B400u, { 0x6341,     // mov.w @r4,r3   unused
                                                0xE102,     // mov #2,r1
                                                0x3716,     // cmp/hi r1,r7
                                                0x8911,     // bt join
                                                0x4708,     // shll2 r7
                                                0xC702,     // mova table,r0
                                                0x017E,     // mov.l @(r0,r7),r1   the table read, in range only
                                                0x412B,     // jmp @r1
                                                0x0009, 0x0009,
                                                0x0600, 0xB420, 0x0600, 0xB424, 0x0600, 0xB428,   // the table
                                                0xA004, 0x0009, 0xA002, 0x0009, 0xA000, 0x0009,   // the cases: bra join
                                                0x621C,     // join: extu.b r1,r2
                                                0xE00E,     // mov #14,r0
                                                0x0525,     // mov.w r2,@(r0,r5)
                                                0x000B,     // rts
                                                0xE056 },   //   mov #0x56,r0
                                  0x38, 0 };
    // A mailbox in work RAM, not marked: the write is forwarded to the poll, the loop never ends as
    // analysed, and the engine removes the hardware write after it as unreachable. Found by the
    // reference run on game code (review of 0e3cd12): the output must say what is missing and why.
    const Fixture mailboxFold = { 0x0600A480u, { 0xD104,     // mov.l @(16,pc),r1   -> 0x26001000
                                                 0xE001,     // mov #1,r0
                                                 0x2102,     // mov.l r0,@r1        mailbox = 1
                                                 0x6012,     // loop: mov.l @r1,r0
                                                 0x2008,     // tst r0,r0
                                                 0x8BFC,     // bf loop             while (mailbox != 0)
                                                 0xD202,     // mov.l @(8,pc),r2    -> SMPC_SF
                                                 0x2200,     // mov.b r0,@r2        SMPC_SF = 0: removed as unreachable
                                                 0x000B, 0x0009,
                                                 0x2600, 0x1000,     // 0x14: the mailbox
                                                 0x2010, 0x0063 },   // 0x18: SMPC_SF
                                  0x1C, 0 };
    // A read whose only use is a store to a work RAM global that the function overwrites before any
    // read: the store is removed as overwritten, late, and the read with it (found by the reference
    // run on game code, review of 0e3cd12). The read stays: it may be of a register.
    const Fixture storeOverwritten = { 0x0600A4C0u, { 0xD203,     // mov.l @(12,pc),r2  -> 0x26001000
                                                      0x6142,     // mov.l @r4,r1       the read
                                                      0x2212,     // mov.l r1,@r2       global = *p
                                                      0xE100,     // mov #0,r1
                                                      0x2212,     // mov.l r1,@r2       global = 0: the first store is dead
                                                      0x000B, 0x0009, 0x0009 }, 0x10, 0x26001000u };
    // Program phase 0 (conservation). The storeOverwritten shape with the pointer read from ordinary
    // work RAM instead of a register. In the reference run (every address volatile) the pointer is a
    // volatile read, so the access stays a read through a pointer; the old oracle excused any such
    // access that was missing from reached code ("the pointer may have folded"), and so would not
    // have seen the read go. Here the pointer does not fold (the function never stores it), the read
    // is held for its effect, and the engine's record says what became of every access.
    const Fixture ptrFromRam = { 0x0600A780u, { 0xD204,     // mov.l @(16,pc),r2  -> 0x26001000, the global
                                                0xD305,     // mov.l @(20,pc),r3  -> 0x26002000, a slot holding a pointer
                                                0x6332,     // mov.l @r3,r3       the pointer, from ordinary memory
                                                0x6132,     // mov.l @r3,r1       the read through it
                                                0x2212,     // mov.l r1,@r2       global = *p
                                                0xE100,     // mov #0,r1
                                                0x2212,     // mov.l r1,@r2       global = 0: the first store is dead
                                                0x000B, 0x0009, 0x0009,
                                                0x2600, 0x1000, 0x2600, 0x2000 }, 0x1C, 0 };
    // Review of 0c2dd20. A switch whose cases all coincide is removed as a switch with one
    // destination; its selector, read from memory, must stay (the lookup flag landed on it).
    const Fixture switchSameDest = { 0x0600A500u, { 0x6442,     // mov.l @r4,r4   the selector
                                                    0xE102,     // mov #2,r1
                                                    0x3416,     // cmp/hi r1,r4
                                                    0x8B01,     // bf in_range
                                                    0xE403,     // mov #3,r4      clamp
                                                    0x0009,
                                                    0xC702,     // in_range: mova table,r0
                                                    0x4408,     // shll2 r4
                                                    0x014E,     // mov.l @(r0,r4),r1
                                                    0x412B,     // jmp @r1
                                                    0x0009, 0x0009,
                                                    0x0600, 0xA528, 0x0600, 0xA528, 0x0600, 0xA528, 0x0600, 0xA528,   // the table
                                                    0x000B, 0xE000 },   // done: rts; mov #0,r0
                                     0x2C, 0 };
    // The mailbox poll, with the hardware pointer saved in work RAM first and read back after the
    // poll: the write through it is in the removed code, and its pointer's origin must not exempt it.
    const Fixture ptrSavedInRam = { 0x0600A540u, { 0xD106,     // mov.l @(24,pc),r1   -> 0x26002000, ordinary RAM
                                                   0xD207,     // mov.l @(28,pc),r2   -> SMPC_SF
                                                   0xD307,     // mov.l @(28,pc),r3   -> 0x26001000, the mailbox
                                                   0x2122,     // mov.l r2,@r1        saved = &SMPC_SF
                                                   0xE001,     // mov #1,r0
                                                   0x2302,     // mov.l r0,@r3        mailbox = 1
                                                   0x6032,     // poll: mov.l @r3,r0
                                                   0x2008,     // tst r0,r0
                                                   0x8BFC,     // bf poll
                                                   0x6212,     // mov.l @r1,r2        the pointer, read back
                                                   0x2200,     // mov.b r0,@r2        SMPC_SF = 0: removed
                                                   0x000B, 0x0009, 0x0009,
                                                   0x2600, 0x2000, 0x2010, 0x0063, 0x2600, 0x1000 },
                                    0x28, 0 };
    // The poll, then a switch bounded by a literal (mov.w @(disp,pc)) with three cases writing
    // SMPC_SF: the bound folds only in a second pass, so the reference run must run both.
    const Fixture litSwitchAfterPoll = { 0x0600A600u, { 0xD114,     // mov.l @(80,pc),r1   -> the mailbox
                                                        0xE001, 0x2102,             // mailbox = 1
                                                        0x6012, 0x2008, 0x8BFC,     // poll
                                                        0xE300,                     // mov #0,r3
                                                        0x6442,                     // mov.l @r4,r4   the selector
                                                        0x911C,                     // mov.w @(56,pc),r1   the bound, 2
                                                        0x3416, 0x8905,             // cmp/hi r1,r4; bt default
                                                        0xC704, 0x4408, 0x014E, 0x412B, 0x0009,   // the jump
                                                        0x0009,
                                                        0x000B, 0x0009, 0x0009,     // default: rts
                                                        0x0600, 0xA634, 0x0600, 0xA63C, 0x0600, 0xA644,   // the table
                                                        0xD206, 0x2230, 0x000B, 0x0009,   // case 0: SMPC_SF = 0
                                                        0xD204, 0x2230, 0x000B, 0x0009,   // case 1
                                                        0xD202, 0x2230, 0x000B, 0x0009,   // case 2
                                                        0x0002, 0x0000,                   // the bound
                                                        0x2010, 0x0063, 0x2600, 0x1000 },
                                         0x58, 0 };
    // Two tests of the mailbox, each always one way, cutting both edges into a region of two blocks
    // where the second has the live second test as a flow predecessor: the engine cuts the edges
    // (ActionDeterminedBranch) and removes the blocks in the next action, before anything splices
    // them, so each is warned by its own start and both writes are said.
    const Fixture twoCuts = { 0x0600A700u, { 0xD10A,     // mov.l @(40,pc),r1   -> the mailbox
                                             0xE001, 0x2102,     // mailbox = 1
                                             0x6012, 0x2008,     // tst mailbox
                                             0x8904,             // bt A
                                             0x6012, 0x2008,     // tst mailbox again
                                             0x8905,             // bt B
                                             0x000B, 0x0009,     // rts
                                             0xD204, 0xE300, 0x2230,     // A: SMPC_SF = 0, falls into B
                                             0x0009,
                                             0xD202, 0xE301, 0x2230,     // B: SMPC_SF = 1
                                             0x000B, 0x0009,
                                             0x2010, 0x0063, 0x2600, 0x1000 },
                              0x30, 0 };
    // A jump through a pointer kept in work RAM. Marked, or in the reference run, that read is a
    // volatile read, and the engine tries its assisted jump model on it first; the failed model was
    // leaked (patch 0016; LeakSanitizer on this suite and on the game code).
    const Fixture jmpPointer = { 0x0600A740u, { 0xD101,     // mov.l @(4,pc),r1   -> 0x26001000
                                                0x6112,     // mov.l @r1,r1       the pointer
                                                0x412B,     // jmp @r1
                                                0x0009 },
                                 0x08, 0x26001000u };
    // Area 5, round 1 (REVIEW_LEDGER.md A5-1, A5-2, A4-1): the dead-read class. A load whose value's
    // only consumers go in the same pass (an expression the engine folds to a constant, a store it
    // removes, a branch it threads) was removed with them: the load was held only if it was unused
    // when the first dead-code pass ran (patches 0009, 0014). The words, literal offset and value are
    // the ledger's, moved from 0x06009000 to the bases here (the code is PC-relative).
    // a: mov.l @(pool),r1 ; mov.b @r1,r0 ; xor r0,r0   (SCU_IST, a register)
    const Fixture deadXor = { 0x0600A800u, { 0xD102, 0x6010, 0x200A, 0x000B, 0x0009, 0x0009, 0x25FE, 0x00A4 }, 0x0C, 0x25FE00A4u };
    // b: mov.l @(pool),r6 ; mov.l @r6,r3 ; cmp/eq r3,r3 ; bt .+0   (the compare folds to true)
    const Fixture deadCmp = { 0x0600A840u, { 0xD602, 0x6362, 0x3330, 0x89FF, 0x000B, 0x0009, 0x05FE, 0x007C }, 0x0C, 0x05FE007Cu };
    // c, with 0x26001030-0x2600103F marked volatile: mov.w @r7,r2 ; xor r2,r2 ; mov.w r2,@r7
    const Fixture deadMarked = { 0x0600A880u, { 0xD702, 0x6271, 0x222A, 0x2721, 0x000B, 0x0009, 0x0600, 0x1034 }, 0x0C, 0x06001034u };
    // d, through a pointer: mov.l @r4,r0 ; xor r0,r0
    const Fixture deadPtr = { 0x0600A8C0u, { 0x6042, 0x200A, 0x000B, 0x0009 }, 0x08, 0 };
    // A5-2: a write, a poll of unmarked work RAM that folds to an endless loop, then a read through a
    // pointer that A5-1 dropped: the caution that the code around it was removed names it.
    const Fixture pollThenRead = { 0x0600A900u, { 0xD704, 0xE003, 0x2702, 0x6072, 0xC801, 0x8BFC, 0x8455, 0x200A, 0x000B, 0x0009,
                                                  0x2600, 0x2180 }, 0x14, 0x26002180u };
    // A4-1: if (c) a; read; if (c) b;  -- the engine threads the second test through the first
    const Fixture twoTests = { 0x0600A940u, { 0xD704, 0x4515, 0x8B00, 0x7301, 0x6271, 0x4515, 0x8B00, 0x7401, 0x000B, 0x0009,
                                              0x05F8, 0x00DE }, 0x14, 0x05F800DEu };
    // ...with a write at the same place (mov.w r2,@r7), which was always kept
    const Fixture twoTestsWrite = { 0x0600A980u, { 0xD704, 0x4515, 0x8B00, 0x7301, 0x2721, 0x4515, 0x8B00, 0x7401, 0x000B, 0x0009,
                                                   0x05F8, 0x00DE }, 0x14, 0x05F800DEu };
    // Two more consumers that die with the read: an AND with 0, and a store to the stack that nothing
    // reads. (deadStack is a control: the read was held before patch 0018, since its store dies at
    // the pass that first sees it. The game code's stack stores die at a later pass, through a stack
    // variable the next write covers, after the pass that saw the read used: deadXor's state.)
    const Fixture deadAnd = { 0x0600A9C0u, { 0xD102, 0x6010, 0xC900, 0x000B, 0x0009, 0x0009, 0x25FE, 0x00A4 }, 0x0C, 0x25FE00A4u };
    const Fixture deadStack = { 0x0600AA00u, { 0x6142,     // mov.l @r4,r1
                                               0x2F16,     // mov.l r1,@-r15     a store nothing reads
                                               0x7F04,     // add #4,r15
                                               0x000B, 0x0009 }, 0x0C, 0 };
    // ...and a read shifted out of its register: both shifts consume no bit of it, so at the first
    // dead-code pass it is reached but consumed by nothing (a mask of 0), a state the unused-load
    // test did not see (this is the shape of the 31 and 34 reads of the game code that ended dead).
    const Fixture deadShift = { 0x0600AA40u, { 0x6042,     // mov.l @r4,r0
                                               0x4028,     // shll16 r0
                                               0x4028,     // shll16 r0          r0 = 0
                                               0x000B, 0x0009 }, 0x0C, 0 };
    // Area 5, round 2 (A5-3): an access narrower than the register it reaches. Each is mov #3,r0 then
    // the access at base+4 on r1 = the literal (the ledger's fixtures a to d, with the effect Mednafen
    // has: sh7095.inc OnChipReg*, vdp1.c VDP1_Write8_DB, vdp2.c RW_W8).
    const std::vector<uint16_t> movb = { 0xE003, 0x2100 }, movw = { 0xE003, 0x2101 };
    const Fixture vdp2Byte1 = simple(0x0600AB00u, movb, 0x25F80001u);       // VDP2_TVMD._1_1_ = 3
    const Fixture vdp2Byte0 = simple(0x0600AB40u, movb, 0x25F80000u);       // ...the other byte of it
    const Fixture vdp1Byte = simple(0x0600AB80u, movb, 0x25D00001u);        // VDP1_TVMR, write-only
    const Fixture vdp2ByteRead = simple(0x0600ABC0u, { 0x6010 }, 0x25F80001u);   // mov.b @r1,r0: a part is read as it is
    const Fixture vdp2Word = simple(0x0600AC00u, movw, 0x25F80000u);        // control: the whole register
    const Fixture divWordLow = simple(0x0600AC40u, movw, 0xFFFFFF06u);      // SH2_DVDNT._2_2_ = 3: nothing
    const Fixture divWordHigh = simple(0x0600AC80u, movw, 0xFFFFFF04u);     // SH2_DVDNT._0_2_ = 3: DVDNT = 3, divides
    const Fixture divByte = simple(0x0600ACC0u, movb, 0xFFFFFF04u);         // an address error
    const Fixture divByteRead = simple(0x0600AD00u, { 0x6010 }, 0xFFFFFF07u);
    const Fixture divWordReadHigh = simple(0x0600AD40u, { 0x6011 }, 0xFFFFFF04u);   // the low half, not the high
    const Fixture divWordReadLow = simple(0x0600AD80u, { 0x6011 }, 0xFFFFFF06u);    // control: the low half
    const Fixture divLong = simple(0x0600ADC0u, { 0xE003, 0x2102 }, 0xFFFFFF04u);   // control: 32 bits
    const Fixture bscWord = simple(0x0600AE00u, movw, 0xFFFFFFE0u);         // SH2_BCR1: keyed long only
    const Fixture ubcByte = simple(0x0600AE40u, movb, 0xFFFFFF40u);         // SH2_BARAH, 16 bits
    // A5-4: the SMPC is a byte device on the low lane of the 16-bit bus. Its registers are at the odd
    // addresses; a word at the even address beside one is a write of its low byte (ss.c).
    const Fixture smpcWord = simple(0x0600AE80u, { 0xE019, 0x2101 }, 0x2010001Eu);      // SMPC_COMREG = 0x19
    const Fixture smpcByteEven = simple(0x0600AEC0u, { 0xE019, 0x2100 }, 0x2010001Eu);   // does nothing
    const Fixture smpcByteOdd = simple(0x0600AF00u, { 0xE019, 0x2100 }, 0x2010001Fu);    // control
    const Fixture smpcLong = simple(0x0600AF40u, { 0xE019, 0x2102 }, 0x2010001Cu);       // two 16-bit writes
    const Fixture smpcLongPair = simple(0x0600AF80u, { 0xE019, 0x2102 }, 0x20100000u);   // IREG0 and IREG1
    const Fixture smpcWordRead = simple(0x0600AFC0u, { 0x6011 }, 0x20100060u);           // 0xFF00 | SMPC_SR
    const Fixture smpcWordMirror = simple(0x0600B000u, { 0xE019, 0x2101 }, 0x2010009Eu); // 128 bytes up
    const Fixture smpcByteEvenRead = simple(0x0600B040u, { 0x6010 }, 0x20100060u);       // 0xFF
    // A5-5 and A5-6: an instruction with two accesses of one kind. mac.l's two operands (mac.w the
    // same, 2 bytes), the second of ordinary memory; the first, SCU_IST, in code the analysis removes
    // (a poll of unmarked RAM that folds to an endless loop). The ledger's fixture A5-5 a, and its
    // control with SCU_IMS in place of 0x26003000's partner.
    const std::vector<uint16_t> macPoll = { 0xD705, 0xE003, 0x2702, 0x6072, 0xC801, 0x8BFC, 0xD103, 0xD204 };
    auto macAfterPoll = [&](uint32_t base, uint16_t mac, uint32_t first, uint32_t second) {
        std::vector<uint16_t> code = macPoll;
        // clrmac, not sts macl,r0: the accumulator is the caller's otherwise, an input no signature passes
        // (A3-5), and these fixtures are about the access removed with the endless loop.
        for (uint16_t w : { mac, uint16_t(0x0028), uint16_t(0x000B), uint16_t(0x0009) }) code.push_back(w);
        for (uint32_t w : { 0x26002180u, first, second })
        {
            code.push_back(uint16_t(w >> 16));
            code.push_back(uint16_t(w));
        }
        return Fixture{ base, code, 0x18, 0x26002180u };   // the poll's literal; the others follow it
    };
    const Fixture macRemoved = macAfterPoll(0x0600B100u, 0x021F, 0x25FE00A4u, 0x26003000u);   // SCU_IST, RAM
    const Fixture macRemovedBoth = macAfterPoll(0x0600B140u, 0x021F, 0x25FE00A4u, 0x25FE00A0u);   // SCU_IST, SCU_IMS
    const Fixture macRemovedSecond = macAfterPoll(0x0600B180u, 0x021F, 0x26003000u, 0x25FE00A4u);   // RAM, SCU_IST
    // ...and the same in the slot of an rte, beside its two stack pops: mov.l @(pool),r1 ; rte ; mov.l @r1,r2
    const Fixture rteSlot = { 0x0600B1C0u, { 0xD705, 0xE003, 0x2702, 0x6072, 0xC801, 0x8BFC, 0xD103, 0x002B, 0x6212, 0x0009,
                                             0x0009, 0x0009, 0x2600, 0x2180, 0x25FE, 0x00A4 }, 0x18, 0x26002180u };
    const Fixture rtsSlot2 = { 0x0600B200u, { 0xD705, 0xE003, 0x2702, 0x6072, 0xC801, 0x8BFC, 0xD103, 0x000B, 0x6212, 0x0009,
                                              0x0009, 0x0009, 0x2600, 0x2180, 0x25FE, 0x00A4 }, 0x18, 0x26002180u };
    // mac.l @r5+,@r4+ and mac.w through pointers; with the same pointer for both operands; with registers
    const Fixture macPtr = { 0x0600B240u, { 0x045F, 0x000B, 0x0009, 0x0009 }, 0x0C, 0 };
    const Fixture macWPtr = { 0x0600B280u, { 0x445F, 0x000B, 0x0009, 0x0009 }, 0x0C, 0 };
    const Fixture macSame = { 0x0600B2C0u, { 0x044F, 0x000B, 0x0009, 0x0009 }, 0x0C, 0 };
    const Fixture macReg = { 0x0600B300u, { 0xD103, 0xD204, 0x021F, 0x001A, 0x000B, 0x0009, 0x0009, 0x0009,
                                            0x25FE, 0x00A4, 0x25FE, 0x00C8 }, 0x10, 0x25FE00A4u };   // SCU_IST, SCU_VER
    // A5-7: the removal caution names every register the removed access covers, and its width. The
    // ledger's fixture (a 4-byte write over VDP2_TVMD and VDP2_EXTEN, after the poll), and a byte write.
    const Fixture removedLong = { 0x0600B340u, { 0xD704, 0xE003, 0x2702, 0x6072, 0xC801, 0x8BFC, 0xD102, 0x2142, 0x000B, 0x0009,
                                                 0x2600, 0x2180, 0x25F8, 0x0000 }, 0x14, 0x26002180u };
    const Fixture removedByte = { 0x0600B380u, { 0xD704, 0xE003, 0x2702, 0x6072, 0xC801, 0x8BFC, 0xD102, 0x2140, 0x000B, 0x0009,
                                                 0x2600, 0x2180, 0x25F8, 0x0001 }, 0x14, 0x26002180u };
    const Fixture removedWord = { 0x0600B3C0u, { 0xD704, 0xE003, 0x2702, 0x6072, 0xC801, 0x8BFC, 0xD102, 0x2141, 0x000B, 0x0009,
                                                 0x2600, 0x2180, 0x2010, 0x001E }, 0x14, 0x26002180u };   // SMPC_COMREG by word
    // A test of the mailbox taken always one way, with no loop: the caution must not claim one.
    const Fixture forwardFold = { 0x0600A680u, { 0xD105,     // mov.l @(20,pc),r1   -> the mailbox
                                                 0xE001, 0x2102,     // mailbox = 1
                                                 0x6012, 0x2008,     // tst mailbox
                                                 0x8B02,             // bf skip
                                                 0xD203,             // mov.l @(12,pc),r2   -> SMPC_SF
                                                 0xE300, 0x2230,     // SMPC_SF = 0: removed
                                                 0x000B, 0x0009,     // skip: rts
                                                 0x0009,
                                                 0x2600, 0x1000, 0x2010, 0x0063 },
                                  0x20, 0 };
    // Two correct outputs the earlier ledger rejected (review of 0e3cd12). A branch on a constant
    // always goes to B, which then runs A: the engine joins entry, B and A into one block, so B's
    // read (made later in the flow, at a higher address) is rightly before A's. And a for loop whose
    // step read is the body's last, with the body's other read under a branch: the step prints in
    // the header, above the body, and runs after it.
    const Fixture backSplice = { 0x0600A400u, { 0xE000,     // mov #0,r0
                                                0x2008,     // tst r0,r0        T = 1
                                                0x8902,     // bt B             always
                                                0x6052,     // A: mov.l @r5,r0
                                                0x000B,     //    rts
                                                0x0009,     //    nop
                                                0x6142,     // B: mov.l @r4,r1
                                                0xAFFA,     //    bra A
                                                0x0009 },   //    nop
                                 0x14, 0 };
    const Fixture forCondBody = { 0x0600A440u, { 0xA004,     // bra test
                                                 0x0009,     //   nop
                                                 0x2668,     // body: tst r6,r6
                                                 0x8900,     //       bt step
                                                 0x6152,     //       mov.l @r5,r1   x = *q, only when r6 == 0
                                                 0x6442,     // step: mov.l @r4,r4   p = *p
                                                 0x2448,     // test: tst r4,r4
                                                 0x8BF9,     //       bf body
                                                 0x000B,     // rts
                                                 0x6013 },   //   mov r1,r0
                                  0x14, 0 };
    // Low work RAM is 1 MiB mirrored twice over 0x00200000-0x003FFFFF (review of 0e3cd12): a
    // mailbox marked at 0x20201000 and polled through 0x20301000.
    const Fixture lowRamMirrorMailbox = simple(0x0600A2C0u, mailboxOps, 0x20301000u);
    // A literal the user marked volatile: mov.l @(4,pc),r0; rts; nop; .long 0x12345678.
    const Fixture sharedLiteral = { 0x06009740u, { 0xD001, 0x000B, 0x0009, 0x0009 }, 0x08, 0x12345678u };
    const Fixture wdtMirror = simple(0x06009780u, { 0xE05A, 0x4018, 0x2101 }, 0xFFFFFE88u);  // keyed, second copy
    const Fixture wdtByte = simple(0x060097C0u, { 0x2140 }, 0xFFFFFE81u);         // mov.b r4,@r1: WTCNT
    const Fixture lcta = simple(0x06009800u, { 0x2142 }, 0x25F800A8u);            // mov.l r4,@r1
    const Fixture ppaf = simple(0x06009840u, { 0x6012 }, 0x25FE0080u);            // mov.l @r1,r0
    const Fixture shadow = simple(0x060098C0u, { 0x2142, 0x6012 }, 0xFFFFFF18u);  // mov.l r4,@r1; mov.l @r1,r0
    const Fixture pdd = simple(0x06009900u, { 0x6012 }, 0x25FE008Cu);             // mov.l @r1,r0
    const Fixture ptrMailbox = simple(0x06009940u, mailboxOps, 0x26001000u);
    // The rest of the access matrix: read-modify-write, a result used twice, and pointers that are
    // the function's arguments (never constant).
    const Fixture rmw = simple(0x06009980u, { 0x6011, 0xCB01, 0x2101 }, 0x25F80000u);   // TVMD |= 1
    const Fixture reuse = simple(0x060099C0u, { 0x6011, 0x6203, 0x302C }, 0x25F8000Au); // v = VCNT; return v + v
    const Fixture argTwice = { 0x06009A00u, { 0x6041, 0x6241, 0x3028, 0x000B, 0x0009, 0x0009 }, 0x0C, 0 };   // *p - *p
    const Fixture argUnused = { 0x06009A40u, { 0x6241, 0x000B, 0x0009, 0x0009 }, 0x08, 0 };                  // (void)*p
    const Fixture argEcho = { 0x06009A80u, { 0x2451, 0x6041, 0x000B, 0x0009 }, 0x08, 0 };                    // *p = q; return *p
    const Fixture argRmw = { 0x06009AC0u, { 0x6041, 0xCB01, 0x2401, 0x000B, 0x0009, 0x0009 }, 0x0C, 0 };     // *p |= 1
    // From the game-code run: one byte read gives an index, the address built from it is used by a
    // load and a store (p[*p].x -= 1). The address was printed at both, and the byte read with it.
    const Fixture indexRmw = { 0x06009D00u, { 0x6240,     // mov.b @r4,r2
                                              0x622C,     // extu.b r2,r2
                                              0x322C,     // add r2,r2
                                              0x324C,     // add r4,r2
                                              0x720E,     // add #14,r2
                                              0x6121,     // mov.w @r2,r1
                                              0x71FF,     // add #-1,r1
                                              0x2211,     // mov.w r1,@r2
                                              0x000B, 0x0009 }, 0x14, 0 };
    // From the game-code run (0607190A): one read of a status register, its value tested on two
    // paths. One machine read; it must be one read in the output too.
    const Fixture twoPaths = { 0x06009D40u, { 0xD105,     // mov.l @(20,pc),r1  -> VDP2_TVSTAT
                                              0x6211,     // mov.w @r1,r2       the one read
                                              0x6023,     // mov r2,r0
                                              0xC901,     // and #1,r0
                                              0x2008,     // tst r0,r0
                                              0x8903,     // bt L
                                              0x6023,     // mov r2,r0
                                              0xC9C0,     // and #0xC0,r0
                                              0x000B, 0x0009,
                                              0x000B,     // L: rts
                                              0xE005 },   //    mov #5,r0
                               0x18, 0x25F80004u };
    // From the game-code run (0603FB3E): a word store through a pointer the code also reads bytes
    // through. The engine split the store to fit the byte type -- two byte writes for one word.
    const Fixture wordToBytes = { 0x06009D80u, { 0x6040,     // mov.b @r4,r0     (r4: a byte pointer)
                                                 0xE100,     // mov #0,r1
                                                 0x2411,     // mov.w r1,@r4     one 16-bit write
                                                 0x000B, 0x0009, 0x0009 }, 0x0C, 0 };
    // Order through pointers (review of d0ce10a): a read through a runtime pointer, then an SMPC
    // command; and two reads of a FIFO through a volatile pointer, subtracted.
    const Fixture readThenCommand = simple(0x06009DC0u, { 0x6042,     // mov.l @r4,r0     the read
                                                          0x2150 },   // mov.b r5,@r1     SMPC_COMREG = param_2
                                           0x2010001Fu);
    const Fixture fifoTwice = simple(0x06009E00u, { 0x6212,           // mov.l @r1,r2     first FIFO read
                                                    0x6012,           // mov.l @r1,r0     second
                                                    0x3028 },         // sub r2,r0        second - first
                                     0x25818000u);
    // Reads the code always makes, combined into a boolean (review of 47ae113): printed in the
    // right operand of && or ||, they would appear skipped whenever the left one decides.
    const Fixture andRead = { 0x06009E40u, { 0x2558,      // tst r5,r5
                                             0x0129,      // movt r1         r1 = param_2 == 0
                                             0x6042,      // mov.l @r4,r0    the read, always made
                                             0x2008,      // tst r0,r0
                                             0x0029,      // movt r0
                                             0x2109,      // and r0,r1
                                             0x6013,      // mov r1,r0
                                             0x000B, 0x0009, 0x0009 }, 0x14, 0 };
    const Fixture orRead = { 0x06009E80u, { 0x2558, 0x0129, 0x6042, 0x2008, 0x0029,
                                            0x210B,       // or r0,r1
                                            0x6013, 0x000B, 0x0009, 0x0009 }, 0x14, 0 };
    const Fixture andByte = { 0x06009EC0u, { 0x2558, 0x0129, 0x6040,   // mov.b @r4,r0
                                             0x2008, 0x0029, 0x2109, 0x6013, 0x000B, 0x0009, 0x0009 }, 0x14, 0 };
    const Fixture twoBools = { 0x06009F00u, { 0x6142,     // mov.l @r4,r1
                                              0x2118,     // tst r1,r1
                                              0x0129,     // movt r1
                                              0x6052,     // mov.l @r5,r0
                                              0x2008,     // tst r0,r0
                                              0x0029,     // movt r0
                                              0x2109,     // and r0,r1
                                              0x6013,     // mov r1,r0
                                              0x000B, 0x0009 }, 0x14, 0 };
    // The control: a read the code makes only when a branch is not taken.
    const Fixture branchRead = { 0x06009F40u, { 0x2558,   // tst r5,r5
                                                0x8B04,   // bf F          param_2 != 0: return 0
                                                0x6042,   // mov.l @r4,r0  read only on this path
                                                0x2008,   // tst r0,r0
                                                0x0029,   // movt r0
                                                0x000B, 0x0009,
                                                0x000B,   // F: rts
                                                0xE000 }, //    mov #0,r0
                                 0x14, 0 };
    // For-loop recovery (review at 5670323): the statement that steps the loop variable, and the one
    // that initialises it, are moved to the end of their blocks so they can print in the for header.
    // A read moved past another read would read in the other order. First the step is not the
    // body's last statement; then it is (a genuine for loop); then the initialiser is not last.
    const Fixture forStep = { 0x06009F80u, { 0xA002,     // bra test
                                             0x0009,     //   nop
                                             0x6442,     // body: mov.l @r4,r4   p = *p (steps the loop)
                                             0x6152,     //       mov.l @r5,r1   x = *q
                                             0x2448,     // test: tst r4,r4
                                             0x8BFB,     //       bf body
                                             0x000B,     // rts
                                             0x6013 },   //   mov r1,r0        return x
                              0x10, 0 };
    const Fixture forStepLast = { 0x06009FC0u, { 0xA002, 0x0009,
                                                 0x6152,     // body: mov.l @r5,r1   x = *q
                                                 0x6442,     //       mov.l @r4,r4   p = *p, last
                                                 0x2448, 0x8BFB, 0x000B, 0x6013 }, 0x10, 0 };
    // A read made before a branch and used on one side of it (review at 5670323): printed on that
    // side, it would appear made only there. Once with the read in the branch's delay slot.
    const Fixture sideRead = { 0x0600A040u, { 0x6142,     // mov.l @r4,r1    the read, always made
                                              0x2558,     // tst r5,r5
                                              0x8902,     // bt L
                                              0xE000,     // mov #0,r0
                                              0x000B, 0x0009,
                                              0x6013,     // L: mov r1,r0    the only use
                                              0x000B, 0x0009, 0x0009 }, 0x14, 0 };
    const Fixture slotRead = { 0x0600A080u, { 0x2558,     // tst r5,r5
                                              0x8D03,     // bt/s L
                                              0x6142,     //   mov.l @r4,r1  in the slot: made either way
                                              0xE000,     // mov #0,r0
                                              0x000B, 0x0009,
                                              0x6013,     // L: mov r1,r0
                                              0x000B, 0x0009, 0x0009 }, 0x14, 0 };
    const Fixture forInit = { 0x0600A000u, { 0x6442,     // mov.l @r4,r4   p = *p (initialises the loop)
                                             0x6152,     // mov.l @r5,r1   x = *q
                                             0xA001,     // bra test
                                             0x0009,     //   nop
                                             0x6442,     // body: mov.l @r4,r4
                                             0x2448,     // test: tst r4,r4
                                             0x8BFC,     //       bf body
                                             0x000B,     // rts
                                             0x6013 },   //   mov r1,r0
                              0x14, 0 };
    // The rest of the instruction forms and expression shapes (review at 5670323): a read-modify-
    // write in one instruction on a register; a GBR-relative read; a read in a return's delay slot;
    // a 4-byte read of which one byte is used, and one stored as a byte; two stores through one
    // pointer; a compound assignment through pointers; two reads as a call's arguments.
    const Fixture tasReg = simple(0x0600A0C0u, { 0x411B }, 0x20100063u);                 // tas.b @r1
    const Fixture gbrRead = { 0x0600A100u, { 0xC400, 0x000B, 0x0009, 0x0009 }, 0x08, 0 };  // mov.b @(0,gbr),r0
    const Fixture rtsSlot = { 0x0600A140u, { 0x000B, 0x6042, 0x0009, 0x0009 }, 0x08, 0 };  // rts; mov.l @r4,r0
    const Fixture byteOfLong = { 0x0600A180u, { 0x6042, 0x600C, 0x000B, 0x0009 }, 0x08, 0 };    // extu.b of a 4-byte read
    const Fixture longToByte = { 0x0600A1C0u, { 0x6042, 0x2500, 0x000B, 0x0009 }, 0x08, 0 };    // mov.b r0,@r5 of a 4-byte read
    const Fixture twoStores = { 0x0600A200u, { 0xE001, 0x2402, 0xE002, 0x2402, 0x000B, 0x0009 }, 0x0C, 0 };   // *p = 1; *p = 2
    const Fixture compound = { 0x0600A240u, { 0x6042, 0x6152, 0x310C, 0x2512, 0x000B, 0x0009 }, 0x0C, 0 };    // *q += *p
    const Fixture callArgs = { 0x0600A280u, { 0x6442,     // mov.l @r4,r4
                                              0x6552,     // mov.l @r5,r5
                                              0xD102,     // mov.l @(8,pc),r1   -> a BIOS entry: opaque
                                              0x410B,     // jsr @r1
                                              0x0009,     //   nop
                                              0x000B, 0x0009, 0x0009 }, 0x10, 0x00000600u };
    // Analysis data reads: a pointer to a string in captured memory, returned.
    const Fixture strPtr = { 0x06009B00u, { 0xD001, 0x000B, 0x0009, 0x0009 }, 0x08, 0x06009B40u };
    // Diagnostics by direction: the same registers written instead of read, and read instead of written.
    const Fixture ppafWrite = simple(0x06009B80u, { 0x2142 }, 0x25FE0080u);   // mov.l r4,@r1: PPAF
    const Fixture istWrite = simple(0x06009BC0u, { 0x2142 }, 0x25FE00A4u);    // IST written only
    const Fixture wtcntRead = simple(0x06009C00u, { 0x6010 }, 0xFFFFFE81u);   // mov.b @r1,r0: WTCNT read
    const Fixture tvstatRead = simple(0x06009C40u, { 0x6011 }, 0x25F80004u);  // TVSTAT (read-only) read
    const Fixture ptmrWrite = simple(0x06009C80u, { 0x2141 }, 0x25D00004u);   // PTMR (write-only) written
    for (const Fixture* f : { &poll, &echo, &twice, &dummy, &counter, &divide, &wide, &mirror, &woRead, &roWrite,
                              &late, &indexed, &lateRam, &mvolRead, &mibufWrite, &wtcnt, &wtcsr, &wovf, &badKey,
                              &keyVar, &divMirror, &purge5, &oddPair, &ubcPair, &soundMailbox, &ramMailbox,
                              &repeatMailbox, &farMailbox, &bit27Mailbox, &sharedLiteral, &wdtMirror, &wdtByte, &lcta, &ppaf,
                              &hwramMirrorMailbox, &shadow, &pdd, &ptrMailbox,
                              &rmw, &reuse, &argTwice, &argUnused, &argEcho, &argRmw,
                              &strPtr, &ppafWrite, &istWrite, &wtcntRead, &tvstatRead, &ptmrWrite, &indexRmw, &twoPaths, &wordToBytes, &readThenCommand, &fifoTwice,
                              &andRead, &orRead, &andByte, &twoBools, &branchRead, &forStep, &forStepLast, &forInit, &sideRead, &slotRead,
                              &tasReg, &gbrRead, &rtsSlot, &byteOfLong, &longToByte, &twoStores, &compound, &callArgs,
                              &lowRamMirrorMailbox, &switchRead, &switchSlotRead, &switchFold, &switchSecondRead, &switchUsed,
                              &backSplice, &forCondBody, &mailboxFold, &storeOverwritten, &ptrFromRam, &switchSameDest, &ptrSavedInRam,
                              &litSwitchAfterPoll, &forwardFold, &twoCuts, &jmpPointer,
                              &deadXor, &deadCmp, &deadMarked, &deadPtr, &pollThenRead, &twoTests, &twoTestsWrite, &deadAnd,
                              &deadStack, &deadShift,
                              &vdp2Byte1, &vdp2Byte0, &vdp1Byte, &vdp2ByteRead, &vdp2Word, &divWordLow, &divWordHigh, &divByte,
                              &divByteRead, &divWordReadHigh, &divWordReadLow, &divLong, &bscWord, &ubcByte,
                              &smpcWord, &smpcByteEven, &smpcByteOdd, &smpcLong, &smpcLongPair, &smpcWordRead, &smpcWordMirror,
                              &smpcByteEvenRead, &macRemoved, &macRemovedBoth, &macRemovedSecond, &rteSlot, &rtsSlot2, &macPtr,
                              &macWPtr, &macSame, &macReg, &removedLong, &removedByte, &removedWord })
    {
        size_t off = f->base - 0x06000000u;
        for (uint16_t w : f->code) { dts::Put16(hw, off, w); off += 2; }
        dts::Put32(hw, f->base + f->literal - 0x06000000u, f->pointer);
    }
    // Area 5, round 3: the width table (SaturnHardwareMap.h "The width table"), one fixture per family
    // and ledger row A5-9 to A5-17. Each is mov #3,r0 (or none) then the access at base+2*ops on r1 = the
    // literal; the table's golden file has every cell, these pin the cautions it generates.
    struct WidthFixture { Fixture f; uint32_t insn; };
    std::map<std::string, WidthFixture> wf;
    auto wadd = [&](const char* name, std::vector<uint16_t> ops, uint32_t pointer) {
        const uint32_t base = 0x0600C000u + uint32_t(wf.size()) * 0x40u;
        wf[name] = WidthFixture{ simple(base, ops, pointer), base + 2u * uint32_t(ops.size()) };
    };
    const std::vector<uint16_t> movl = { 0xE003, 0x2102 }, rdb = { 0x6010 }, rdw = { 0x6011 }, rdl = { 0x6012 };
    wadd("icrLong", movl, 0xFFFFFEE0u);          // A5-9a: SH2_ICR and SH2_IPRA, 16-bit registers
    wadd("iprbLong", movl, 0xFFFFFE60u);         //        SH2_IPRB and SH2_VCRA
    wadd("sbycrLong", movl, 0xFFFFFE90u);        //        SH2_SBYCR and SH2_CCR, 8-bit
    wadd("vcrwdtLongRead", rdl, 0xFFFFFEE4u);    // A5-9b: a long read over a register and a gap
    wadd("tierWord", movw, 0xFFFFFE10u);         // A5-10a: TIER, FTCSR
    wadd("frchWordRead", rdw, 0xFFFFFE12u);      // A5-10b: FRCH, FRCL
    wadd("tierWordRead", rdw, 0xFFFFFE10u);      //         TIER, FTCSR: nothing is armed
    wadd("sbycrWord", movw, 0xFFFFFE90u);        // A5-10c: no register at the even address
    wadd("drcr0Word", movw, 0xFFFFFE70u);
    wadd("ppdByte", movb, 0x25FE0087u);          // A5-11: the SCU's DSP ports are not masked
    wadd("ppafByte", movb, 0x25FE0083u);
    wadd("pdaWord", movw, 0x25FE008Au);
    wadd("ppdLong", movl, 0x25FE0084u);          //        control: the register whole
    wadd("pddWordRead", rdw, 0x25FE008Cu);       //        a narrow read of PDD is the whole read
    wadd("bcrLong", movl, 0xFFFFFFE0u);          // A5-12: no key
    wadd("bcrUnknown", { 0x2142 }, 0xFFFFFFE0u); //        a value the analysis does not know
    wadd("minitByte", movb, 0x21000000u);        // A5-13
    wadd("minitLong", movl, 0x21000000u);
    wadd("minitWord", movw, 0x21000000u);        //        control
    wadd("prirLong", movl, 0x25F800FCu);         // A5-14a: a register and a reserved word
    wadd("ramctlLong", movl, 0x25F8000Cu);       //         b: a reserved word and a register
    wadd("endrLong", movl, 0x25D0000Cu);         //         c: VDP1
    wadd("dmealLong", movl, 0x25B00410u);        //         d: the SCSP
    wadd("hirqLongRead", rdl, 0x25890008u);      //         e: a long read of a CD register
    wadd("hirqLong", movl, 0x25890008u);         //            ...and a long write
    wadd("cr4Byte", movb, 0x25890024u);          //            a byte write to the command register
    wadd("tvmdWordRead", rdw, 0x25F80000u);      // A5-15a: the bus reads EXTEN as well
    wadd("fifoWordRead", rdw, 0x25818000u);      //         b: the CD FIFO pops twice
    wadd("fifoWindowRead", rdw, 0x25898000u);    //            ...once in the register window
    wadd("fifoWindowLong", rdl, 0x25898000u);    //            a long read there: one word, in both halves
    wadd("fifoLong", rdl, 0x25818000u);          //            control: a long read of the FIFO window is two words
    wadd("extenRead", rdw, 0x25F80002u);         // A5-16: the latch
    wadd("aiackRead", rdl, 0x25FE00A8u);         //        reads 0
    wadd("ccrMirrorRead", rdb, 0xFFFFFE93u);     //        the CCR reads back here
    wadd("ccrMirrorWrite", movb, 0xFFFFFE95u);   //        ...but is not written
    wadd("bcrKeyed", { 0 }, 0xFFFFFFE0u);        //        (replaced below: BCR1 = 0xA55A0003)
    {
        WidthFixture& k = wf["bcrKeyed"];
        // mov.l @(pool),r1 ; mov.l @(pool),r2 ; mov.l r2,@r1 ; rts ; nop ; nop ; the two literals
        k.f.code = { 0xD102, 0xD203, 0x2122, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFFE0, 0xA55A, 0x0003 };
        k.f.literal = 0x0C;
        k.insn = k.f.base + 4;
    }

    // ---- Area 5, round 4 (REVIEW_LEDGER.md A5-18 to A5-27): the reviewer's fixtures, their words verbatim; only the base moves (they
    // all sit at 0x06009000 in the ledger, the suite keeps one function per address). The `or` of the ledger is the C bar.
    std::map<std::string, Fixture> r4;
    {
        uint32_t next = 0x0600D000u;
        auto add = [&](const char* name, std::vector<uint16_t> words, uint32_t literal, uint32_t pointer) {
            r4[name] = Fixture{ next, words, literal, pointer };
            next += 0x40u;
        };
        const std::vector<uint16_t> rd1 = { 0xD101, 0x6011, 0x000B, 0x0009 };   // mov.l @(pool),r1 ; mov.w @r1,r0 ; rts ; nop
        auto withLiteral = [&](std::vector<uint16_t> ops, uint32_t pointer) {
            ops.push_back(uint16_t(pointer >> 16));
            ops.push_back(uint16_t(pointer));
            return ops;
        };
        // A5-18: a word read at +2 of a DMAC register reaches nothing.
        add("a5_18a", withLiteral(rd1, 0xFFFFFF8Eu), 0x08, 0xFFFFFF8Eu);   // CHCR0 + 2
        add("a5_18b", withLiteral(rd1, 0xFFFFFFB2u), 0x08, 0xFFFFFFB2u);   // DMAOR + 2
        add("a5_18c", withLiteral(rd1, 0xFFFFFF82u), 0x08, 0xFFFFFF82u);   // SAR0 + 2
        add("a5_18d", withLiteral(rd1, 0xFFFFFF8Cu), 0x08, 0xFFFFFF8Cu);   // CHCR0 + 0
        add("a5_18dvsr", withLiteral(rd1, 0xFFFFFF02u), 0x08, 0xFFFFFF02u);   // control: the divider has the +2 case
        // A5-19: a register read through the cached partition is a cache read.
        add("a5_19a", { 0xD103, 0x6011, 0xC808, 0x89FC, 0x000B, 0x0009, 0x0009, 0x0009, 0x05F8, 0x0004 }, 0x10, 0x05F80004u);
        add("a5_19a_through", { 0xD103, 0x6011, 0xC808, 0x89FC, 0x000B, 0x0009, 0x0009, 0x0009, 0x25F8, 0x0004 }, 0x10, 0x25F80004u);   // control
        add("a5_19b", { 0xD101, 0x6012, 0x000B, 0x0009, 0x0581, 0x8000 }, 0x08, 0x05818000u);
        add("a5_19c", { 0xD101, 0x6010, 0x000B, 0x0009, 0x0010, 0x0063 }, 0x08, 0x00100063u);   // SMPC_SF, the third address of the ledger
        add("a5_19write", { 0xD102, 0xE003, 0x2101, 0x000B, 0x0009, 0x0009, 0x05F8, 0x0000 }, 0x0C, 0x05F80000u);   // control: written through
        // A5-20: the CCR's word accesses, the register beside RSTCSR.
        add("a5_20a", withLiteral(rd1, 0xFFFFFE92u), 0x08, 0xFFFFFE92u);
        add("a5_20b", { 0xD102, 0xE003, 0x2101, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE92 }, 0x0C, 0xFFFFFE92u);
        add("a5_20c", withLiteral(rd1, 0xFFFFFE82u), 0x08, 0xFFFFFE82u);
        // A5-21: the watchdog.
        add("a5_21rstcsr", { 0xD101, 0x6010, 0x000B, 0x0009, 0xFFFF, 0xFE83 }, 0x08, 0xFFFFFE83u);
        add("a5_21wtcsr", { 0xD101, 0x6010, 0x000B, 0x0009, 0xFFFF, 0xFE80 }, 0x08, 0xFFFFFE80u);
        add("a5_21b", { 0xD102, 0xD203, 0x2121, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE80, 0x0000, 0xA51F }, 0x0C, 0xFFFFFE80u);
        add("a5_21b_tme", { 0xD102, 0xD203, 0x2121, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE80, 0x0000, 0xA53F }, 0x0C, 0xFFFFFE80u);   // control: TME set
        add("a5_21c", { 0xD102, 0xE003, 0x2100, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE80 }, 0x0C, 0xFFFFFE80u);
        // A5-22: any write to SMPC_SF sets it.
        add("a5_22", { 0xD102, 0xE000, 0x2100, 0x000B, 0x0009, 0x0009, 0x2010, 0x0063 }, 0x0C, 0x20100063u);
        // A5-23: a byte write to the CD FIFO pushes a word.
        add("a5_23", { 0xD102, 0xE003, 0x2100, 0x000B, 0x0009, 0x0009, 0x2581, 0x8000 }, 0x0C, 0x25818000u);
        // A5-24: the byte access to a 32-bit on-chip register is made after the address error.
        add("a5_24", withLiteral({ 0xD101, 0x6010, 0x000B, 0x0009 }, 0xFFFFFF8Fu), 0x08, 0xFFFFFF8Fu);
        // A5-25: a misaligned access lands on the aligned register.
        add("a5_25long", { 0xD102, 0xE003, 0x2102, 0x000B, 0x0009, 0x0009, 0x25FE, 0x00A2 }, 0x0C, 0x25FE00A2u);
        add("a5_25word", { 0xD102, 0xE003, 0x2101, 0x000B, 0x0009, 0x0009, 0x25F8, 0x0001 }, 0x0C, 0x25F80001u);
        // A5-26: backup RAM is a byte device on the low lane.
        add("a5_26", { 0xD102, 0xE005, 0x2100, 0x6010, 0x000B, 0x0009, 0x2018, 0x0000 }, 0x0C, 0x20180000u);
        // A5-27: a long access to an unnamed on-chip address below 0xFFFFFF00.
        add("a5_27", { 0xD102, 0xE003, 0x2102, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE20 }, 0x0C, 0xFFFFFE20u);
        // The reviewer's two questions: the partition's decode, and the notes for a CCR purge, a line purge and a PTMR start.
        add("q_alias", withLiteral({ 0xD101, 0x6010, 0x000B, 0x0009 }, 0xFFFF8E10u), 0x08, 0xFFFF8E10u);   // TIER through A & 0x1FF
        add("q_ccr", { 0xD102, 0xE011, 0x2100, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE92 }, 0x0C, 0xFFFFFE92u);   // CE and CP
        add("q_ccr_ce", { 0xD102, 0xE001, 0x2100, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE92 }, 0x0C, 0xFFFFFE92u);   // control: CE only
        add("q_purge", withLiteral({ 0xD101, 0x6012, 0x000B, 0x0009 }, 0x46003304u), 0x08, 0x46003304u);
        add("q_ptmr", { 0xD102, 0xE001, 0x2101, 0x000B, 0x0009, 0x0009, 0x25D0, 0x0004 }, 0x0C, 0x25D00004u);   // PTMR = 1
        add("q_ptmr0", { 0xD102, 0xE000, 0x2101, 0x000B, 0x0009, 0x0009, 0x25D0, 0x0004 }, 0x0C, 0x25D00004u);   // control: PTMR = 0
    }
    // ---- Area 5, round 5 (REVIEW_LEDGER.md A5-28 to A5-32): the reviewer's fixtures, their words verbatim (only the base moves), and
    // the controls and neighbours each fix needs. A5-28's through-variant and A5-29..A5-32's literals are as the ledger gives them.
    std::map<std::string, Fixture> r5;
    {
        uint32_t next = 0x0600E000u;
        auto add = [&](const char* name, std::vector<uint16_t> words, uint32_t literal, uint32_t pointer) {
            r5[name] = Fixture{ next, words, literal, pointer };
            next += 0x40u;
        };
        // A5-28: a pointer merged from register literals (mov.l lit0,r1 ; tst r4,r4 ; bt L ; mov.l lit1,r1 ; nop ; L: mov.w @r1,r0 ; tst #8,r0 ; bt L).
        add("a5_28", { 0xD105, 0x2448, 0x8901, 0xD105, 0x0009, 0x6011, 0xC808, 0x89FC, 0x000B, 0x0009, 0x0009, 0x0009, 0x05F8, 0x0004, 0x05F8, 0x000A },
            0x18, 0x05F80004u);
        add("a5_28_through", { 0xD105, 0x2448, 0x8901, 0xD105, 0x0009, 0x6011, 0xC808, 0x89FC, 0x000B, 0x0009, 0x0009, 0x0009, 0x25F8, 0x0004, 0x25F8, 0x000A },
            0x18, 0x25F80004u);   // control: the same poll through the cache-through partition
        // ...a write through a merged pointer (VDP1_ENDR or VDP1_PTMR; the value written is 0)
        add("a5_28_write", { 0xD105, 0x2448, 0x8901, 0xD105, 0x0009, 0xE000, 0x2101, 0x000B, 0x0009, 0x0009, 0x0009, 0x0009, 0x25D0, 0x000C, 0x25D0, 0x0004 },
            0x18, 0x25D0000Cu);
        // ...a pointer that is a register address or a parameter: said, with "may hold other values"
        add("a5_28_other", { 0xD104, 0x2448, 0x8900, 0x6153, 0x6011, 0x000B, 0x0009, 0x0009, 0x0009, 0x0009, 0x05F8, 0x0004 }, 0x14, 0x05F80004u);
        // ...control: a pointer that is only a parameter names no register
        add("a5_28_param", { 0x6041, 0x000B, 0x0009, 0x0009 }, 0x04, 0x00000000u);
        // ...a pointer built from a number that is not a register (`param_1 - 1` is INT_ADD(0xFFFFFFFF, param_1)): no register is named
        add("a5_28_minus1", { 0x6143, 0x71FF, 0x6010, 0x000B, 0x0009 }, 0x08, 0x00000000u);
        // ...a number that is a register's address (0xFFFFFFF4, SH2_RTCNT's) added to a parameter prints as `param_1 + -0xc`: no register is named
        add("a5_28_rawconst", { 0xD102, 0x314C, 0x6011, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFFF4 }, 0x0C, 0xFFFFFFF4u);
        // ...an index added to a register's address: said for the register itself
        add("a5_28_addend", { 0xD102, 0x314C, 0x6011, 0x000B, 0x0009, 0x0009, 0x05F8, 0x0004 }, 0x0C, 0x05F80004u);
        add("a5_28_index", { 0xD105, 0x6043, 0x4008, 0x4008, 0x4000, 0xD204, 0x0126, 0x000B, 0x0009, 0x0009, 0x0009, 0x0009, 0x25FE, 0x0010, 0x0000, 0x0101 },
            0x18, 0x25FE0010u);   // (&SCU_D0EN)[param_1 * 8] = 0x101, the corpora's shape (hwram007 0601B7B4, hwram008 0605A964)
        // ...a write through a pointer that may be SMPC_PDR1 or SMPC_PDR2, then a read of SMPC_PDR1: the write is not certain, so the read
        // is not "read back after a write" (the split note); the control writes SMPC_PDR1 directly
        add("a5_28_split", { 0xD105, 0x2448, 0x8901, 0xD105, 0x0009, 0xE000, 0x2100, 0xD204, 0x6020, 0x000B, 0x0009, 0x0009, 0x2010, 0x0075, 0x2010, 0x0077, 0x2010, 0x0075 },
            0x18, 0x20100075u);
        add("a5_28_split_direct", { 0xD103, 0xE000, 0x2100, 0x6010, 0x000B, 0x0009, 0x0009, 0x0009, 0x2010, 0x0075 }, 0x10, 0x20100075u);
        // A5-29: a misaligned on-chip access (a: word write at 0xFFFFFF05, b: word read at 0xFFFFFF8D, c: long write at 0xFFFFFF06, d: word read at 0xFFFFFE11).
        add("a5_29a", { 0xD102, 0xE003, 0x2101, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFF05 }, 0x0C, 0xFFFFFF05u);
        add("a5_29b", { 0xD101, 0x6011, 0x000B, 0x0009, 0xFFFF, 0xFF8D }, 0x08, 0xFFFFFF8Du);
        add("a5_29c", { 0xD102, 0xE003, 0x2102, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFF06 }, 0x0C, 0xFFFFFF06u);
        add("a5_29d", { 0xD101, 0x6011, 0x000B, 0x0009, 0xFFFF, 0xFE11 }, 0x08, 0xFFFFFE11u);
        add("a5_29e", { 0xD102, 0xE003, 0x2101, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE11 }, 0x0C, 0xFFFFFE11u);   // a word write at FTCSR's own address
        add("a5_29f", { 0xD101, 0x6011, 0x000B, 0x0009, 0xFFFF, 0xFE61 }, 0x08, 0xFFFFFE61u);   // a word read at IPRB + 1: IPRB, whole
        add("a5_29g", { 0xD101, 0x6011, 0x000B, 0x0009, 0xFFFF, 0xFE81 }, 0x08, 0xFFFFFE81u);   // a word read at WTCNT's address
        add("a5_29h", { 0xD102, 0xE003, 0x2101, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE81 }, 0x0C, 0xFFFFFE81u);   // a word write at WTCNT's address: not the key port
        // A5-30: a byte write to the reserved low byte of IPRB / VCRD; controls: the high byte, VCRA's low byte, a word write.
        add("a5_30iprb", { 0xD102, 0xE003, 0x2100, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE61 }, 0x0C, 0xFFFFFE61u);
        add("a5_30vcrd", { 0xD102, 0xE003, 0x2100, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE69 }, 0x0C, 0xFFFFFE69u);
        add("a5_30high", { 0xD102, 0xE003, 0x2100, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE60 }, 0x0C, 0xFFFFFE60u);
        add("a5_30vcra", { 0xD102, 0xE003, 0x2100, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE63 }, 0x0C, 0xFFFFFE63u);
        add("a5_30word", { 0xD102, 0xE003, 0x2101, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE60 }, 0x0C, 0xFFFFFE60u);
        // A5-31: what Mednafen does after the address error (a: byte write to DVDNT, b: byte read of CHCR0, c: long write at TIER, d: long read at TIER).
        add("a5_31a", { 0xD102, 0xE003, 0x2100, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFF07 }, 0x0C, 0xFFFFFF07u);
        add("a5_31b", { 0xD101, 0x6010, 0x000B, 0x0009, 0xFFFF, 0xFF8F }, 0x08, 0xFFFFFF8Fu);
        add("a5_31c", { 0xD102, 0xE003, 0x2102, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE10 }, 0x0C, 0xFFFFFE10u);
        add("a5_31d", { 0xD101, 0x6012, 0x000B, 0x0009, 0xFFFF, 0xFE10 }, 0x08, 0xFFFFFE10u);
        add("a5_31iprb", { 0xD102, 0xE003, 0x2102, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE60 }, 0x0C, 0xFFFFFE60u);   // a long at IPRB: bits 15-8 into its high byte
        add("a5_31odd", { 0xD102, 0xE003, 0x2102, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE61 }, 0x0C, 0xFFFFFE61u);   // a long at IPRB + 1: nothing is stored
        add("a5_31wdt", { 0xD102, 0xE003, 0x2102, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE80 }, 0x0C, 0xFFFFFE80u);   // a long at the watchdog: nothing is stored
        add("a5_31ccr", { 0xD102, 0xE010, 0x2102, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE92 }, 0x0C, 0xFFFFFE92u);   // a long write to CCR with CP set: purges
        add("a5_31bsc", { 0xD102, 0xE003, 0x2100, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFFE3 }, 0x0C, 0xFFFFFFE3u);   // a byte write to a bus state controller register
        // A5-32: a: VDP1_ENDR = 0, b: SCU_D0EN = 0x101, c: SCU_AIACK = 1; and DSTP, T1MD, level 1, and controls without the bits.
        add("a5_32a", { 0xD102, 0xE000, 0x2101, 0x000B, 0x0009, 0x0009, 0x25D0, 0x000C }, 0x0C, 0x25D0000Cu);
        add("a5_32b", { 0xD102, 0xD003, 0x2102, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0010, 0x0000, 0x0101 }, 0x0C, 0x25FE0010u);
        add("a5_32c", { 0xD102, 0xE001, 0x2102, 0x000B, 0x0009, 0x0009, 0x25FE, 0x00A8 }, 0x0C, 0x25FE00A8u);
        add("a5_32dstp", { 0xD102, 0xE001, 0x2102, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0060 }, 0x0C, 0x25FE0060u);
        add("a5_32t1md", { 0xD102, 0xE000, 0x2102, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0098 }, 0x0C, 0x25FE0098u);
        add("a5_32level1", { 0xD102, 0xD003, 0x2102, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0030, 0x0000, 0x0101 }, 0x0C, 0x25FE0030u);
        add("a5_32en1", { 0xD102, 0xE001, 0x2102, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0010 }, 0x0C, 0x25FE0010u);   // control: bit 0 only, no bit 8
        add("a5_32aiack0", { 0xD102, 0xE000, 0x2102, 0x000B, 0x0009, 0x0009, 0x25FE, 0x00A8 }, 0x0C, 0x25FE00A8u);   // control: bit 0 clear
        // ...the lane that carries the bits: a word at +0 and a byte at +0 carry neither bit 0 nor bit 8; a word at +2 carries both; a byte at +3 carries bit 0
        add("a5_32word0", { 0xD102, 0xD003, 0x2101, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0010, 0x0000, 0x0101 }, 0x0C, 0x25FE0010u);
        add("a5_32word2", { 0xD102, 0xD003, 0x2101, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0012, 0x0000, 0x0101 }, 0x0C, 0x25FE0012u);
        add("a5_32byte0", { 0xD102, 0xE001, 0x2100, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0010 }, 0x0C, 0x25FE0010u);
        add("a5_32byte3", { 0xD102, 0xE001, 0x2100, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0013 }, 0x0C, 0x25FE0013u);
        add("a5_32t1md1", { 0xD102, 0xD003, 0x2102, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0098, 0x0000, 0x0101 }, 0x0C, 0x25FE0098u);   // control: enable set
        // The reviewer's questions (A5-33, A5-34): a store to the BIOS ROM that the engine folds; the cache address array.
        add("a5_33", { 0xD103, 0xE005, 0x2100, 0x6010, 0x000B, 0x0009, 0x0009, 0x0009, 0x0000, 0x1000 }, 0x10, 0x00001000u);   // mov.b 5 -> ROM, read it back
        add("a5_33_through", { 0xD103, 0xE005, 0x2100, 0x6010, 0x000B, 0x0009, 0x0009, 0x0009, 0x2000, 0x1000 }, 0x10, 0x20001000u);   // ...through the cache-through partition
        add("a5_33_ram", { 0xD103, 0xE005, 0x2100, 0x6010, 0x000B, 0x0009, 0x0009, 0x0009, 0x0600, 0x1000 }, 0x10, 0x06001000u);   // control: work RAM
        add("a5_34", { 0xD102, 0xE005, 0x2102, 0x000B, 0x0009, 0x0009, 0x6000, 0x0010 }, 0x0C, 0x60000010u);   // a long write to the address array, entry 1
        // A5-45 (the nightly run, seed 19409 shrunk): a pointer merged from the SMPC's word view of OREG0 (the symbol at the even address
        // 0x00100020, `&SMPC_OREG0`) and a RAM address; mov.l lit0,r6 ; cmp/eq r2,r3 ; bt L ; mov.l lit1,r6 ; L: mov.w @r6,r2.
        add("a5_45", { 0xD603, 0x3320, 0x8900, 0xD603, 0x6261, 0x000B, 0x0009, 0x0009, 0x0010, 0x0020, 0x2600, 0x2080 }, 0x10, 0x00100020u);
        add("a5_45_through", { 0xD603, 0x3320, 0x8900, 0xD603, 0x6261, 0x000B, 0x0009, 0x0009, 0x2010, 0x0020, 0x2600, 0x2080 }, 0x10, 0x20100020u);   // the cache-through partition
        add("a5_45_byte", { 0xD603, 0x3320, 0x8900, 0xD603, 0x6260, 0x000B, 0x0009, 0x0009, 0x0010, 0x0021, 0x2600, 0x2080 }, 0x10, 0x00100021u);   // control: the byte register's own address, mov.b
        // Round 6 (A5-37 to A5-41): the reviewer's fixtures, words verbatim. A5-37: a constant offset from a merged register pointer.
        add("a5_37a", { 0xD104, 0x2448, 0x8901, 0xD104, 0x0009, 0xD004, 0x1104, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0000, 0x25FE, 0x0020, 0x0000, 0x0101 }, 0x14, 0x25FE0000u);
        add("a5_37b", { 0xD104, 0x2448, 0x8901, 0xD104, 0x0009, 0xD004, 0x1101, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0010, 0x25FE, 0x0030, 0x0000, 0x0101 }, 0x14, 0x25FE0010u);
        add("a5_37c", { 0xD104, 0x2448, 0x8901, 0xD104, 0x0009, 0x8512, 0x000B, 0x0009, 0x0009, 0x0009, 0x05F8, 0x0000, 0x05F8, 0x0002 }, 0x14, 0x05F80000u);
        add("a5_37_mirror", { 0xD104, 0x2448, 0x8901, 0xD104, 0x0009, 0xD004, 0x1104, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0100, 0x25FE, 0x0120, 0x0000, 0x0101 }, 0x14, 0x25FE0100u);   // the same through the mirrors 0x25FE0100, 0x25FE0120
        add("a5_37d", { 0xD104, 0x2448, 0x8901, 0xD104, 0x0009, 0x71FC, 0xD003, 0x2102, 0x000B, 0x0009, 0x25FE, 0x0014, 0x25FE, 0x0034, 0x0000, 0x0101 }, 0x14, 0x25FE0014u);
        // A5-38: a merged pointer whose values are register aliases (printed as numbers).
        add("a5_38a", { 0xD104, 0x2448, 0x8901, 0xD104, 0x0009, 0xD004, 0x2102, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0110, 0x25FE, 0x0130, 0x0000, 0x0101 }, 0x14, 0x25FE0110u);
        add("a5_38a_direct", { 0xD102, 0xD003, 0x2102, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0110, 0x0000, 0x0101 }, 0x0C, 0x25FE0110u);   // control: the direct write
        add("a5_38b", { 0xD105, 0x2448, 0x8901, 0xD105, 0x0009, 0x6011, 0xC808, 0x89FC, 0x000B, 0x0009, 0x0009, 0x0009, 0x05F8, 0x0204, 0x05F8, 0x020A }, 0x18, 0x05F80204u);
        // A5-39: a pointer stepped in a loop.
        add("a5_39a", { 0xD104, 0xE205, 0xD004, 0x2102, 0x7104, 0x4210, 0x8BFB, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0000, 0x0000, 0x0101 }, 0x14, 0x25FE0000u);
        add("a5_39b", { 0xD103, 0xE203, 0x6015, 0x4210, 0x8BFC, 0x000B, 0x0009, 0x0009, 0x25F8, 0x0004 }, 0x10, 0x25F80004u);
        // A5-40: SCU_PPAF and CD_CR4 writes.
        add("a5_40a", { 0xD102, 0xE000, 0x2102, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0080 }, 0x0C, 0x25FE0080u);
        add("a5_40b", { 0xD102, 0xE003, 0x2101, 0x000B, 0x0009, 0x0009, 0x2589, 0x0024 }, 0x0C, 0x25890024u);
        add("a5_40b_byte", { 0xD102, 0xE003, 0x2100, 0x000B, 0x0009, 0x0009, 0x2589, 0x0024 }, 0x0C, 0x25890024u);   // control: a byte write does not issue it
        // A5-41: a long below the split with bytes above it.
        add("a5_41a", { 0xD102, 0xE003, 0x2102, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFEFE }, 0x0C, 0xFFFFFEFEu);
        add("a5_41a_fd", { 0xD102, 0xE003, 0x2102, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFEFD }, 0x0C, 0xFFFFFEFDu);
        add("a5_41b", { 0xD101, 0x6012, 0x000B, 0x0009, 0xFFFF, 0xFEFE }, 0x08, 0xFFFFFEFEu);
    }
    // ---- Area 5, round 7 (REVIEW_LEDGER.md A5-50 to A5-56): the reviewer's words verbatim (only the base moves), with the variants and controls
    // each fix needs. In the words the literals are among them, as in the ledger.
    std::map<std::string, Fixture> r7;
    {
        uint32_t next = 0x06010000u;
        auto add = [&](const char* name, std::vector<uint16_t> words, uint32_t literal, uint32_t pointer) {
            r7[name] = Fixture{ next, words, literal, pointer };
            next += 0x40u;
        };
        // A5-50: mov.b 3 to the odd byte of colour RAM, then mov.w @r1,r0; controls: a word write, VDP2 VRAM and the VDP1 frame buffer and VRAM (right as they are).
        add("a5_50", { 0xD103, 0xE003, 0x8011, 0x6011, 0x000B, 0x0009, 0x0009, 0x0009, 0x25F0, 0x0000 }, 0x10, 0x25F00000u);
        add("a5_50_even", { 0xD103, 0xE003, 0x2100, 0x6011, 0x000B, 0x0009, 0x0009, 0x0009, 0x25F0, 0x0000 }, 0x10, 0x25F00000u);   // mov.b r0,@r1: the even byte
        add("a5_50_word", { 0xD103, 0xE003, 0x2101, 0x6011, 0x000B, 0x0009, 0x0009, 0x0009, 0x25F0, 0x0000 }, 0x10, 0x25F00000u);   // control: the word form
        add("a5_50_vram", { 0xD103, 0xE003, 0x8011, 0x6011, 0x000B, 0x0009, 0x0009, 0x0009, 0x25E0, 0x0000 }, 0x10, 0x25E00000u);   // control: VDP2 VRAM
        add("a5_50_fb", { 0xD103, 0xE003, 0x8011, 0x6011, 0x000B, 0x0009, 0x0009, 0x0009, 0x25C8, 0x0000 }, 0x10, 0x25C80000u);   // control: the frame buffer
        add("a5_50_v1ram", { 0xD103, 0xE003, 0x8011, 0x6011, 0x000B, 0x0009, 0x0009, 0x0009, 0x25C0, 0x0000 }, 0x10, 0x25C00000u);   // control: VDP1 VRAM
        // A5-51: a pointer merged from register addresses, then moved between partitions: a: `& 0x1fffffff` (cached), b: `| 0x20000000`, c: the store
        // to SCU_D0EN / SCU_D1EN; d: a mask that is a parameter (nothing is known).
        add("a5_51a", { 0xD104, 0x2448, 0x8901, 0xD104, 0x0009, 0xD204, 0x2129, 0x6011, 0x000B, 0x0009, 0x25F8, 0x0004, 0x25F8, 0x000A, 0x1FFF, 0xFFFF }, 0x14, 0x25F80004u);
        add("a5_51b", { 0xD104, 0x2448, 0x8901, 0xD104, 0x0009, 0xD204, 0x212B, 0x6011, 0x000B, 0x0009, 0x05F8, 0x0004, 0x05F8, 0x000A, 0x2000, 0x0000 }, 0x14, 0x05F80004u);
        add("a5_51c", { 0xD105, 0x2448, 0x8901, 0xD105, 0x0009, 0xD205, 0x212B, 0xD305, 0x2132, 0x000B, 0x0009, 0x0009, 0x05FE, 0x0010, 0x05FE, 0x0030, 0x2000, 0x0000, 0x0000, 0x0101 }, 0x18, 0x05FE0010u);
        add("a5_51e", { 0xD105, 0x2448, 0x8901, 0xD105, 0x0009, 0xD205, 0x212B, 0xD305, 0x2132, 0x000B, 0x0009, 0x0009, 0x05FE, 0x0110, 0x05FE, 0x0130, 0x2000, 0x0000, 0x0000, 0x0101 }, 0x18, 0x05FE0110u);   // c with the SCU's mirrors
        add("a5_51d", { 0xD103, 0x2149, 0x6011, 0x000B, 0x0009, 0x0009, 0x0009, 0x0009, 0x25F8, 0x0004 }, 0x10, 0x25F80004u);   // control: and r4,r1
        // A5-52: two names of one work-RAM word: mov.l lit0,r2 ; mov.l lit1,r1 ; mov #5,r0 ; mov.w r0,@r2 ; mov #7,r0 ; mov.w r0,@r1 ; mov.w @r2,r0.
        add("a5_52a", { 0xD204, 0xD105, 0xE005, 0x2201, 0xE007, 0x2101, 0x6021, 0x000B, 0x0009, 0x0009, 0x2600, 0x1000, 0x0600, 0x1000 }, 0x14, 0x26001000u);
        add("a5_52_repeat", { 0xD204, 0xD105, 0xE005, 0x2201, 0xE007, 0x2101, 0x6021, 0x000B, 0x0009, 0x0009, 0x2610, 0x0000, 0x2600, 0x0000 }, 0x14, 0x26100000u);   // the 1 MiB repeat of high work RAM
        add("a5_52_bit27", { 0xD204, 0xD105, 0xE005, 0x2201, 0xE007, 0x2101, 0x6021, 0x000B, 0x0009, 0x0009, 0x2E00, 0x1000, 0x2600, 0x1000 }, 0x14, 0x2E001000u);   // bits 28-27
        add("a5_52_low", { 0xD204, 0xD105, 0xE005, 0x2201, 0xE007, 0x2101, 0x6021, 0x000B, 0x0009, 0x0009, 0x2020, 0x1000, 0x0020, 0x1000 }, 0x14, 0x20201000u);   // low work RAM
        add("a5_52_long", { 0xD204, 0xD105, 0xE005, 0x2202, 0xE007, 0x2102, 0x6022, 0x000B, 0x0009, 0x0009, 0x2600, 0x1000, 0x0600, 0x1000 }, 0x14, 0x26001000u);   // the long form
        add("a5_52_same", { 0xD204, 0xD105, 0xE005, 0x2201, 0xE007, 0x2101, 0x6021, 0x000B, 0x0009, 0x0009, 0x2600, 0x1000, 0x2600, 0x1000 }, 0x14, 0x26001000u);   // control: one name, the fold stands
        add("a5_52_apart", { 0xD204, 0xD105, 0xE005, 0x2201, 0xE007, 0x2101, 0x6021, 0x000B, 0x0009, 0x0009, 0x2600, 0x1000, 0x0600, 0x1004 }, 0x14, 0x26001000u);   // control: two words, no alias
        // the slave's idle loop: a store through the cache-through name, the associative purge of the line, and the poll through the cached name
        add("a5_52_idle", { 0xD205, 0xD306, 0xD106, 0xE000, 0x2202, 0x2302, 0x6012, 0x2008, 0x8BFC, 0x000B, 0x0009, 0x0009, 0x2600, 0x3304, 0x4600, 0x3304, 0x0600, 0x3304 }, 0x18, 0x26003304u);
        add("a5_52_purge", { 0xD205, 0xD306, 0xD106, 0xE000, 0x2202, 0x2302, 0x6012, 0x2008, 0x8BFC, 0x000B, 0x0009, 0x0009, 0x0600, 0x3304, 0x4600, 0x3304, 0x0600, 0x3304 }, 0x18, 0x06003304u);   // the same with one name besides the purge's
        // A5-53: colour RAM (4 KiB repeat; mode 0's entry 0x400 up), VDP2 VRAM (512 KiB), the frame buffer (256 KiB)
        add("a5_53_cram", { 0xD204, 0xD105, 0xE005, 0x2201, 0xE007, 0x2101, 0x6021, 0x000B, 0x0009, 0x0009, 0x25F0, 0x1000, 0x25F0, 0x0000 }, 0x14, 0x25F01000u);
        add("a5_53_mode0", { 0xD204, 0xD105, 0xE005, 0x2201, 0xE007, 0x2101, 0x6021, 0x000B, 0x0009, 0x0009, 0x25F0, 0x0800, 0x25F0, 0x0000 }, 0x14, 0x25F00800u);
        add("a5_53_vram", { 0xD204, 0xD105, 0xE005, 0x2201, 0xE007, 0x2101, 0x6021, 0x000B, 0x0009, 0x0009, 0x25E8, 0x0000, 0x25E0, 0x0000 }, 0x14, 0x25E80000u);
        add("a5_53_fb", { 0xD204, 0xD105, 0xE005, 0x2201, 0xE007, 0x2101, 0x6021, 0x000B, 0x0009, 0x0009, 0x25CC, 0x0000, 0x25C8, 0x0000 }, 0x14, 0x25CC0000u);
        add("a5_53_cram_apart", { 0xD204, 0xD105, 0xE005, 0x2201, 0xE007, 0x2101, 0x6021, 0x000B, 0x0009, 0x0009, 0x25F0, 0x0004, 0x25F0, 0x0000 }, 0x14, 0x25F00004u);   // control: two entries
        // A5-54: a store loop from the SCU's hole (seven stores reach SCU_D1EN) and from SCSP slot 31 (eight reach MVOL, MOBUF); a non-constant
        // step from SCU_D0EN; a walk through the SCSP's DSP program area (no register near).
        add("a5_54_hole", { 0xD104, 0xE207, 0xD004, 0x2102, 0x7104, 0x4210, 0x8BFB, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0018, 0x0000, 0x0101 }, 0x14, 0x25FE0018u);
        add("a5_54_slot", { 0xD104, 0xE208, 0xD004, 0x2102, 0x7104, 0x4210, 0x8BFB, 0x000B, 0x0009, 0x0009, 0x25B0, 0x03F0, 0x0000, 0x0101 }, 0x14, 0x25B003F0u);
        add("a5_54_stepvar", { 0xD104, 0xE205, 0xD004, 0x2102, 0x315C, 0x4210, 0x8BFB, 0x000B, 0x0009, 0x0009, 0x25FE, 0x0010, 0x0000, 0x0101 }, 0x14, 0x25FE0010u);
        add("a5_54_dsp", { 0xD104, 0xE208, 0xD004, 0x2102, 0x7104, 0x4210, 0x8BFB, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0A00, 0x0000, 0x0101 }, 0x14, 0x25B00A00u);   // control: the DSP program area
        // A5-55: writes whose action is chosen by the value: DTLG with EX, KYONEX in a slot's word 0, SCIRE, COMREG, FBCR with FCM
        add("a5_55_dtlg", { 0xD102, 0xE010, 0x4018, 0x2101, 0x000B, 0x0009, 0x25B0, 0x0416 }, 0x0C, 0x25B00416u);
        add("a5_55_dtlg0", { 0xD102, 0xE001, 0x2101, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0416 }, 0x0C, 0x25B00416u);   // control: EX clear
        add("a5_55_kyonex", { 0xD102, 0xE018, 0x4018, 0x2101, 0x000B, 0x0009, 0x25B0, 0x0000 }, 0x0C, 0x25B00000u);
        add("a5_55_kyonex0", { 0xD102, 0xE008, 0x4018, 0x2101, 0x000B, 0x0009, 0x25B0, 0x0000 }, 0x0C, 0x25B00000u);   // control: bit 12 clear (0x0800)
        add("a5_55_scire", { 0xD102, 0xE020, 0x2101, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0422 }, 0x0C, 0x25B00422u);
        add("a5_55_mcire", { 0xD102, 0xE020, 0x2101, 0x000B, 0x0009, 0x0009, 0x25B0, 0x042E }, 0x0C, 0x25B0042Eu);
        add("a5_55_scire0", { 0xD102, 0xE000, 0x2101, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0422 }, 0x0C, 0x25B00422u);   // control: clears nothing
        add("a5_55_comreg", { 0xD102, 0xE019, 0x2100, 0x000B, 0x0009, 0x0009, 0x2010, 0x001F }, 0x0C, 0x2010001Fu);
        add("a5_55_fbcr", { 0xD102, 0xE003, 0x2101, 0x000B, 0x0009, 0x0009, 0x25D0, 0x0002 }, 0x0C, 0x25D00002u);
        add("a5_55_fbcr0", { 0xD102, 0xE001, 0x2101, 0x000B, 0x0009, 0x0009, 0x25D0, 0x0002 }, 0x0C, 0x25D00002u);   // control: FCM clear
        // A5-56: a read of a write-only SMPC register returns the bus buffer; SMPC_SF read whole (not masked to bit 0), and masked
        add("a5_56", { 0xD103, 0xE005, 0x2100, 0x6010, 0x000B, 0x0009, 0x0009, 0x0009, 0x2010, 0x0001 }, 0x10, 0x20100001u);
        add("a5_56_sf", { 0xD102, 0x6010, 0x2008, 0x0029, 0x000B, 0x0009, 0x2010, 0x0063 }, 0x0C, 0x20100063u);   // mov.b @r1,r0 ; tst r0,r0 ; movt r0: SMPC_SF == 0 tests bits 7-1 too
        add("a5_56_sf_masked", { 0xD102, 0x6010, 0xC901, 0x000B, 0x0009, 0x0009, 0x2010, 0x0063 }, 0x0C, 0x20100063u);   // and #1,r0: SMPC_SF & 1, every game's poll
        // ---- Area 5, round 8 (REVIEW_LEDGER.md A5-59 to A5-68): the reviewer's words verbatim (only the base moves).
        // A5-59: the two cautions that were cut off in the middle of a word.
        add("a5_59_cache", { 0xD101, 0x6011, 0x000B, 0x0009, 0x05F8, 0x0004 }, 0x08, 0x05F80004u);   // mov.w @r1,r0 through the cached partition
        add("a5_59_wdt", { 0xD102, 0xD003, 0x2101, 0x000B, 0x0009, 0x0009, 0xFFFF, 0xFE80, 0x0000, 0x1234 }, 0x0C, 0xFFFFFE80u);   // a watchdog write with key 0x12
        // A5-60: a pointer moved by and / or / xor round a loop (r1 = 0x25F80004 or 0x05F80004; r3 the mask; loop 3 times)
        add("a5_60_and", { 0xD104, 0xD305, 0xE403, 0x6011, 0x2139, 0x4410, 0x8BFB, 0x000B, 0x0009, 0x0009, 0x25F8, 0x0004, 0x1FFF, 0xFFFF }, 0x14, 0x25F80004u);
        add("a5_60_or", { 0xD104, 0xD305, 0xE403, 0x6011, 0x213B, 0x4410, 0x8BFB, 0x000B, 0x0009, 0x0009, 0x05F8, 0x0004, 0x2000, 0x0000 }, 0x14, 0x05F80004u);
        add("a5_60_xor", { 0xD104, 0xD305, 0xE403, 0x6011, 0x213A, 0x4410, 0x8BFB, 0x000B, 0x0009, 0x0009, 0x05F8, 0x0004, 0x2000, 0x0000 }, 0x14, 0x05F80004u);
        add("a5_60_xor_after", { 0xD104, 0xD305, 0xE403, 0x213A, 0x6011, 0x4410, 0x8BFB, 0x000B, 0x0009, 0x0009, 0x25F8, 0x0004, 0x2000, 0x0000 }, 0x14, 0x25F80004u);   // the operation before the read: 0x05F80004 and 0x25F80004 alternate
        // A5-61: a mask applied after an index add; the control is the same without the mask
        add("a5_61", { 0xD104, 0x4408, 0x4408, 0x314C, 0xD303, 0x2139, 0xD003, 0x2102, 0x000B, 0x0009, 0x25FE, 0x0010, 0x1FFF, 0xFFFF, 0x0000, 0x0101 }, 0x14, 0x25FE0010u);
        add("a5_61_nomask", { 0xD103, 0x4408, 0x4408, 0x314C, 0xD002, 0x2102, 0x000B, 0x0009, 0x25FE, 0x0010, 0x0000, 0x0101 }, 0x10, 0x25FE0010u);
        // A5-62: the associative purge through the partition 0xA; the control through the partition 0x4
        add("a5_62", { 0xD106, 0xD207, 0xE000, 0x2102, 0x2202, 0x6312, 0x2338, 0x89FC, 0xD404, 0xD005, 0x2402, 0x000B, 0x0009, 0x0009, 0x0600, 0x3304, 0xA600, 0x3304, 0x25FE, 0x0010, 0x0000, 0x0101 }, 0x1C, 0x06003304u);
        add("a5_62_part4", { 0xD106, 0xD207, 0xE000, 0x2102, 0x2202, 0x6312, 0x2338, 0x89FC, 0xD404, 0xD005, 0x2402, 0x000B, 0x0009, 0x0009, 0x0600, 0x3304, 0x4600, 0x3304, 0x25FE, 0x0010, 0x0000, 0x0101 }, 0x1C, 0x06003304u);
        // A5-64: a store of a constant to a register whose action the value chooses (mov.b / mov.l / mov.w r0,@r1; the literal at 0x0C)
        add("a5_64_ireg0", { 0xD102, 0xD003, 0x2100, 0x000B, 0x0009, 0x0009, 0x0010, 0x0001, 0x0000, 0x0080 }, 0x0C, 0x00100001u);
        add("a5_64_ims", { 0xD102, 0xD003, 0x2102, 0x000B, 0x0009, 0x0009, 0x25FE, 0x00A0, 0x0000, 0x0000 }, 0x0C, 0x25FE00A0u);
        add("a5_64_mobuf", { 0xD102, 0xD003, 0x2101, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0406, 0x0000, 0x0041 }, 0x0C, 0x25B00406u);
        add("a5_64_slot12", { 0xD102, 0xD003, 0x2101, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0018, 0x0000, 0x1234 }, 0x0C, 0x25B00018u);
        add("a5_64_mcieb", { 0xD102, 0xD003, 0x2101, 0x000B, 0x0009, 0x0009, 0x25B0, 0x042A, 0x0000, 0x0040 }, 0x0C, 0x25B0042Au);
        add("a5_64_scieb", { 0xD102, 0xD003, 0x2101, 0x000B, 0x0009, 0x0009, 0x25B0, 0x041E, 0x0000, 0x0040 }, 0x0C, 0x25B0041Eu);
        add("a5_64_tima", { 0xD102, 0xD003, 0x2101, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0418, 0x0000, 0x0105 }, 0x0C, 0x25B00418u);
        add("a5_64_scilv1", { 0xD102, 0xD003, 0x2101, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0426, 0x0000, 0x0007 }, 0x0C, 0x25B00426u);
        add("a5_64_scipd", { 0xD102, 0xD003, 0x2101, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0420, 0x0000, 0x0020 }, 0x0C, 0x25B00420u);
        add("a5_64_mcipd0", { 0xD102, 0xD003, 0x2101, 0x000B, 0x0009, 0x0009, 0x25B0, 0x042C, 0x0000, 0x0010 }, 0x0C, 0x25B0042Cu);   // control: bit 5 clear sets nothing
        add("a5_64_slot11", { 0xD102, 0xD003, 0x2101, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0016, 0x0000, 0x1234 }, 0x0C, 0x25B00016u);   // control: word 11 is stored
        add("a5_64_slot12l", { 0xD102, 0xD003, 0x2102, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0018, 0x0000, 0x1234 }, 0x0C, 0x25B00018u);   // a long at word 12 covers words 12 and 13
        add("a5_64_slot10l", { 0xD102, 0xD003, 0x2102, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0014, 0x0000, 0x1234 }, 0x0C, 0x25B00014u);   // control: a long at word 10 covers words 10 and 11
        add("a5_64_mobuf_hi", { 0xD102, 0xD003, 0x2100, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0406, 0x0000, 0x0041 }, 0x0C, 0x25B00406u);   // control: the high byte is not sent
        // A5-65: the extended RAM cartridge's alias; the control is the other bank
        add("a5_65", { 0xD104, 0xD205, 0xE005, 0x2102, 0xE007, 0x2202, 0x6012, 0x000B, 0x0009, 0x0009, 0x0240, 0x0000, 0x0248, 0x0000 }, 0x14, 0x02400000u);
        add("a5_65_bank", { 0xD104, 0xD205, 0xE005, 0x2102, 0xE007, 0x2202, 0x6012, 0x000B, 0x0009, 0x0009, 0x0240, 0x0000, 0x0260, 0x0000 }, 0x14, 0x02400000u);
        // A5-66: the SMPC_SF byte stored to a constant address of work RAM; the control stores it through a pointer parameter
        add("a5_66", { 0xD102, 0xD403, 0x6010, 0x2400, 0x000B, 0x0009, 0x2010, 0x0063, 0x0600, 0x4000 }, 0x0C, 0x20100063u);
        add("a5_66_param", { 0xD102, 0x6010, 0x2400, 0x000B, 0x0009, 0x0009, 0x2010, 0x0063 }, 0x0C, 0x20100063u);
        // A5-67: a byte store to colour RAM through a stepped pointer and through a merged one
        add("a5_67_loop", { 0xD104, 0xE403, 0xE005, 0x2100, 0x7101, 0x4410, 0x8BFB, 0x000B, 0x0009, 0x0009, 0x25F0, 0x0000 }, 0x14, 0x25F00000u);
        add("a5_67_merge", { 0xD104, 0xD205, 0x2448, 0x8D01, 0x6313, 0x6323, 0xE005, 0x2300, 0x000B, 0x0009, 0x25F0, 0x0000, 0x25F0, 0x0002 }, 0x14, 0x25F00000u);
        // A5-68: a store, a write to the register that re-addresses the memory, and a load
        add("a5_68_tvmr", { 0xD104, 0xD205, 0xE005, 0x2100, 0xE303, 0x2231, 0x6010, 0x000B, 0x0009, 0x0009, 0x25C8, 0x0200, 0x25D0, 0x0000 }, 0x14, 0x25C80200u);
        add("a5_68_ramctl", { 0xD104, 0xD205, 0xD005, 0x2101, 0xD305, 0x2231, 0x6011, 0x000B, 0x0009, 0x0009, 0x25F0, 0x0004, 0x25F8, 0x000E, 0x0000, 0x1234, 0x0000, 0x3000 }, 0x14, 0x25F00004u);
    }
    // ---- Area 5, round 9 (REVIEW_LEDGER.md A5-70 to A5-79): the reviewer's words verbatim (only the base moves), with the controls each fix needs.
    std::map<std::string, Fixture> r9;
    {
        uint32_t next = 0x06014000u;
        auto add = [&](const char* name, std::vector<uint16_t> words, uint32_t literal, uint32_t pointer) {
            r9[name] = Fixture{ next, words, literal, pointer };
            next += 0x40u;
        };
        // A5-70: a bitwise operation round a loop that is conditional, one of two in a turn, on one path of two, or with a step (r1 = 0x25F80004, r3 the mask,
        // r4 the count 3). (a) mask after the read on one path; (a') the control, before the read; (b) and (c) two operations in a turn; (d) a mask on one
        // path and a step on the other; (e) a step and a mask in one turn.
        add("a5_70a", { 0xD105, 0xD306, 0xE403, 0x6011, 0x2008, 0x8900, 0x2139, 0x4410, 0x8BF9, 0x000B, 0x0009, 0x0009, 0x25F8, 0x0004, 0x1FFF, 0xFFFF }, 0x18, 0x25F80004u);
        add("a5_70a_control", { 0xD105, 0xD306, 0xE403, 0x2448, 0x8900, 0x2139, 0x6011, 0x4410, 0x8BF9, 0x000B, 0x0009, 0x0009, 0x25F8, 0x0004, 0x1FFF, 0xFFFF }, 0x18, 0x25F80004u);
        add("a5_70b", { 0xD105, 0xD306, 0xE504, 0xE403, 0x6011, 0x2139, 0x215A, 0x4410, 0x8BFA, 0x000B, 0x0009, 0x0009, 0x25F8, 0x0008, 0xFFFF, 0xFFF0 }, 0x18, 0x25F80008u);
        add("a5_70c", { 0xD105, 0xD306, 0xE504, 0xE403, 0x6011, 0x2139, 0x215A, 0x4410, 0x8BFA, 0x000B, 0x0009, 0x0009, 0x25F8, 0x0006, 0xFFFF, 0xFFF9 }, 0x18, 0x25F80006u);
        add("a5_70d", { 0xD106, 0xD307, 0xE403, 0x6011, 0x2008, 0x8902, 0x2139, 0xA001, 0x0009, 0x7104, 0x4410, 0x8BF6, 0x000B, 0x0009, 0x25F8, 0x0004, 0x1FFF, 0xFFFF }, 0x1C, 0x25F80004u);
        add("a5_70e", { 0xD104, 0xD305, 0xE403, 0x6011, 0x7104, 0x2139, 0x4410, 0x8BFA, 0x000B, 0x0009, 0x25F8, 0x0004, 0x1FFF, 0xFFFF }, 0x14, 0x25F80004u);
        // ...and the shapes a closure by paths must also get right: a mask that loses nothing on one path and a reload on the other (a join with a number
        // of its own), a step with no mask (A5-39's), the mask before the step, and a mask whose operand is the pointer itself (unknown).
        add("a5_70_join", { 0xD107, 0xD308, 0xE403, 0x6011, 0x2008, 0x8902, 0x2139, 0xA001, 0x0009, 0xD105, 0x4410, 0x8BF6, 0x000B, 0x0009, 0x0009, 0x0009, 0x25F8, 0x0004, 0x1FFF, 0xFFFF, 0x25F8, 0x000A }, 0x20, 0x25F80004u);
        add("a5_70_mask_step", { 0xD104, 0xD305, 0xE403, 0x6011, 0x2139, 0x7104, 0x4410, 0x8BFA, 0x000B, 0x0009, 0x25F8, 0x0004, 0x1FFF, 0xFFFF }, 0x14, 0x25F80004u);
        // A5-71: the cache data array, 4 KiB, partitions 4 and 6 (a: 0xC0000000 and 0xC0001000; b: 0x80000010 and 0xC0000010; controls: two cells)
        add("a5_71a", { 0xD104, 0xD205, 0xE305, 0xE407, 0x2132, 0x2242, 0x6012, 0x000B, 0x0009, 0x0009, 0xC000, 0x0000, 0xC000, 0x1000 }, 0x14, 0xC0000000u);
        add("a5_71b", { 0xD104, 0xD205, 0xE305, 0xE407, 0x2132, 0x2242, 0x6012, 0x000B, 0x0009, 0x0009, 0x8000, 0x0010, 0xC000, 0x0010 }, 0x14, 0x80000010u);
        add("a5_71_cells", { 0xD104, 0xD205, 0xE305, 0xE407, 0x2132, 0x2242, 0x6012, 0x000B, 0x0009, 0x0009, 0xC000, 0x0000, 0xC000, 0x0004 }, 0x14, 0xC0000000u);   // control: two cells
        add("a5_71_same", { 0xD104, 0xD205, 0xE305, 0xE407, 0x2132, 0x2242, 0x6012, 0x000B, 0x0009, 0x0009, 0xC000, 0x0000, 0xC000, 0x0000 }, 0x14, 0xC0000000u);   // control: one name, the fold stands
        // A5-72: the purge names a line; the flag word is another word of it (0x06003308; the purge through 0x46003304), and the control purges the polled word
        add("a5_72", { 0xD206, 0xD307, 0xE000, 0x2202, 0x2302, 0x6122, 0x2118, 0x89FC, 0xD404, 0xD505, 0x2452, 0x000B, 0x0009, 0x0009, 0x0600, 0x3308, 0x4600, 0x3304, 0x25FE, 0x0010, 0x0000, 0x0101 }, 0x1C, 0x06003308u);
        add("a5_72_word", { 0xD206, 0xD307, 0xE000, 0x2202, 0x2302, 0x6122, 0x2118, 0x89FC, 0xD404, 0xD505, 0x2452, 0x000B, 0x0009, 0x0009, 0x0600, 0x3304, 0x4600, 0x3304, 0x25FE, 0x0010, 0x0000, 0x0101 }, 0x1C, 0x06003304u);
        add("a5_72_apart", { 0xD206, 0xD307, 0xE000, 0x2202, 0x2302, 0x6122, 0x2118, 0x89FC, 0xD404, 0xD505, 0x2452, 0x000B, 0x0009, 0x0009, 0x0600, 0x3310, 0x4600, 0x3304, 0x25FE, 0x0010, 0x0000, 0x0101 }, 0x1C, 0x06003310u);   // control: the next line
        // A5-73: the cartridge probe: store a pattern to the extended RAM cartridge, read it back, compare (and a store to the backup RAM cartridge's area)
        add("a5_73", { 0xD106, 0x9209, 0x2121, 0x6311, 0x3320, 0x8902, 0xE001, 0x000B, 0x0009, 0xE000, 0x000B, 0x0009, 0x1234, 0x0009, 0x0240, 0x0000 }, 0x1C, 0x02400000u);
        add("a5_73_backup", { 0xD103, 0xE005, 0x2100, 0x6010, 0x000B, 0x0009, 0x0009, 0x0009, 0x0400, 0x0001 }, 0x10, 0x04000001u);
        add("a5_73_ram", { 0xD103, 0xE005, 0x2100, 0x6010, 0x000B, 0x0009, 0x0009, 0x0009, 0x0600, 0x1000 }, 0x10, 0x06001000u);   // control: work RAM
        // A5-74: VDP1_PTMR = 2 (the draw-at-frame-change mode); controls 0, 1 and 3
        add("a5_74", { 0xD102, 0xE002, 0x2101, 0x000B, 0x0009, 0x0009, 0x25D0, 0x0004 }, 0x0C, 0x25D00004u);
        add("a5_74_0", { 0xD102, 0xE000, 0x2101, 0x000B, 0x0009, 0x0009, 0x25D0, 0x0004 }, 0x0C, 0x25D00004u);
        add("a5_74_1", { 0xD102, 0xE001, 0x2101, 0x000B, 0x0009, 0x0009, 0x25D0, 0x0004 }, 0x0C, 0x25D00004u);
        add("a5_74_3", { 0xD102, 0xE003, 0x2101, 0x000B, 0x0009, 0x0009, 0x25D0, 0x0004 }, 0x0C, 0x25D00004u);
        // A5-75: SMPC_IREG0 = 0 as the INTBACK's own first parameter (mov.b to the register at 0x20100001)
        add("a5_75", { 0xD102, 0xE000, 0x2100, 0x000B, 0x0009, 0x0009, 0x2010, 0x0001 }, 0x0C, 0x20100001u);
        add("a5_75_break", { 0xD102, 0xE040, 0x2100, 0x000B, 0x0009, 0x0009, 0x2010, 0x0001 }, 0x0C, 0x20100001u);
        // A5-76: CD_HIRQMSK = 0xFFFF, SCU_IST = 0, CD_HIRQ = 0
        add("a5_76_hirqmsk", { 0xD102, 0xE0FF, 0x2101, 0x000B, 0x0009, 0x0009, 0x2589, 0x000C }, 0x0C, 0x2589000Cu);
        add("a5_76_ist", { 0xD102, 0xE000, 0x2102, 0x000B, 0x0009, 0x0009, 0x25FE, 0x00A4 }, 0x0C, 0x25FE00A4u);
        add("a5_76_hirq", { 0xD102, 0xE000, 0x2101, 0x000B, 0x0009, 0x0009, 0x2589, 0x0008 }, 0x0C, 0x25890008u);
        // A5-77: a byte write to the high byte of SCSP_SCILV0 (0x25B00424: stores nothing); the control is the low byte (0x25B00425)
        add("a5_77", { 0xD102, 0xE005, 0x2100, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0424 }, 0x0C, 0x25B00424u);
        add("a5_77_low", { 0xD102, 0xE005, 0x2100, 0x000B, 0x0009, 0x0009, 0x25B0, 0x0425 }, 0x0C, 0x25B00425u);
        // A5-78: a byte store to colour RAM at `param + 0x25F00000` (both operand orders); the control adds a small offset
        add("a5_78a", { 0xD102, 0x315C, 0xE005, 0x2100, 0x000B, 0x0009, 0x25F0, 0x0000 }, 0x0C, 0x25F00000u);
        add("a5_78b", { 0xD102, 0xE005, 0x314C, 0x2100, 0x000B, 0x0009, 0x25F0, 0x0000 }, 0x0C, 0x25F00000u);
        add("a5_78_small", { 0xD102, 0x315C, 0xE005, 0x2100, 0x000B, 0x0009, 0x0000, 0x0010 }, 0x0C, 0x00000010u);   // control: param + 0x10
        // A5-83 (generator seed 603025): a pointer from a register area no name is near (an SCSP slot's, 0x05B000C8) stepped by an add and flipped
        // between the partitions by an xor in one turn: `p = (p + 4) ^ 0x20000000` reads 0x35B000C8, then 0x15B000CC through the cached partition, then
        // 0x35B000D0, and so on. The first control steps only (no cached read to say); the second writes through the same pointer (a write is written through).
        add("a5_83", { 0xD104, 0xD305, 0xE403, 0x6012, 0x7104, 0x213A, 0x4410, 0x8BFA, 0x000B, 0x0009, 0x35B0, 0x00C8, 0x2000, 0x0000 }, 0x14, 0x35B000C8u);
        add("a5_83_step", { 0xD103, 0xE403, 0x6012, 0x7104, 0x4410, 0x8BFB, 0x000B, 0x0009, 0x35B0, 0x00C8 }, 0x10, 0x35B000C8u);
        add("a5_83_write", { 0xD104, 0xD305, 0xE403, 0x2102, 0x7104, 0x213A, 0x4410, 0x8BFA, 0x000B, 0x0009, 0x35B0, 0x00C8, 0x2000, 0x0000 }, 0x14, 0x35B000C8u);
        // A5-79: a frame buffer store, a frame change (VDP1_FBCR = 3), a wait for the frame end, the load of the same address
        add("a5_79", { 0xD807, 0x900B, 0x2801, 0xD107, 0xE203, 0x2121, 0xD306, 0x6031, 0xC808, 0x89FC, 0x6081, 0x000B, 0x0009, 0x0009, 0x1234, 0x0009, 0x25C8, 0x0200, 0x25D0, 0x0002, 0x25F8, 0x0004 }, 0x20, 0x25C80200u);
    }
    for (const auto* group : { &r4, &r5, &r7, &r9 })
        for (const auto& x : *group)
        {
            size_t off = x.second.base - 0x06000000u;
            for (uint16_t c : x.second.code) { dts::Put16(hw, off, c); off += 2; }
            dts::Put32(hw, x.second.base + x.second.literal - 0x06000000u, x.second.pointer);
        }
    for (const auto& w : wf)
    {
        size_t off = w.second.f.base - 0x06000000u;
        for (uint16_t c : w.second.f.code) { dts::Put16(hw, off, c); off += 2; }
        dts::Put32(hw, w.second.f.base + w.second.f.literal - 0x06000000u, w.second.f.pointer);
    }
    {
        const char text[] = "HELLO";
        for (size_t i = 0; i < sizeof text; ++i) hw[0x9B40 + i] = static_cast<uint8_t>(text[i]);
    }
    auto snap = MakeSnapshot(hw);
    auto run = [&](const char* label, const Fixture& f) {
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        Print(label, r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(ReferenceOk(d, snap, Request(f.base), r));
        return r;
    };

    DecompileResult r = run("SMPC_SF poll", poll);
    CHECK(Ledger(r, { Wr(0x20100063u, 1), Rd(0x20100063u, 1, true) }));
    CHECK(Has(r.Text(), "SMPC_SF = 1;"));
    CHECK(Count(r.Text(), "SMPC_SF") >= 2);   // the write and the poll's read
    CHECK(!Has(r.Text(), "while( true )") && !Has(r.Text(), "while (true)"));
    CHECK(r.hardwareNotes.empty());

    r = run("SCU_IST write then read", echo);
    CHECK(Ledger(r, { Wr(0x25FE00A4u, 4), Rd(0x25FE00A4u, 4, false, true) }));
    CHECK(Has(r.Text(), "SCU_IST = param_1;"));
    CHECK(Has(r.Text(), "Var1 = SCU_IST;"));   // read back after the write: the read is kept
    CHECK(!Has(r.Text(), "return param_1;"));
    CHECK(HasNote(r, "SCU_IST: a write of 0 clears that interrupt; a read returns the pending ones", false));

    r = run("VDP2_TVMD twice", twice);
    CHECK(Ledger(r, { Wr(0x25F80000u, 2), Wr(0x25F80000u, 2) }));
    CHECK(Has(r.Text(), "VDP2_TVMD = 0;"));
    CHECK(Has(r.Text(), "VDP2_TVMD = 1;"));

    r = run("SCU_IST dummy read", dummy);
    CHECK(Ledger(r, { Rd(0x25FE00A4u, 4), Wr(0x25FE00A4u, 4) }));
    CHECK(Count(r.Text(), "SCU_IST") >= 2);   // the read stays, before the write
    CHECK(Has(r.Text(), "SCU_IST = 0;"));

    r = run("VDP2_VCNT twice", counter);
    CHECK(Ledger(r, { Rd(0x25F8000Au, 2), Rd(0x25F8000Au, 2) }));
    CHECK(Count(r.Text(), "VDP2_VCNT") >= 2);
    CHECK(!Has(r.Text(), "return 0;"));

    r = run("SH-2 divider", divide);
    CHECK(Ledger(r, { Wr(0xFFFFFF00u, 4), Wr(0xFFFFFF04u, 4), Rd(0xFFFFFF04u, 4, false, true) }));
    CHECK(Has(r.Text(), "SH2_DVSR = param_2;"));
    CHECK(Has(r.Text(), "SH2_DVDNT = param_1;"));
    CHECK(Has(r.Text(), "Var1 = SH2_DVDNT;"));   // the quotient, read after the dividend's write
    CHECK(!Has(r.Text(), "return param_1;"));

    r = run("VDP2 32-bit scroll write", wide);
    CHECK(Ledger(r, { Wr(0x25F80070u, 4), Wr(0x25F80070u, 4) }));
    CHECK(Has(r.Text(), "param_1"));   // the first write is not lost to the second
    CHECK(Has(r.Text(), "param_2"));
    // Documented as one 32-bit register: explained on the line, not a caution.
    CHECK(HasNote(r, "VDP2_SCXN0: one 4-byte write of VDP2_SCXIN0 and VDP2_SCXDN0, as documented", false));
    CHECK(!WarnsAbout(r, "SCXIN0"));

    r = run("SMPC_SF through a mirror", mirror);
    CHECK(Ledger(r, { Wr(0x201000E3u, 1), Rd(0x201000E3u, 1, true) }));
    CHECK(!Has(r.Text(), "while( true )") && !Has(r.Text(), "while (true)"));
    CHECK(HasNote(r, "0x201000E3 is SMPC_SF through a mirror", false));

    r = run("write-only read", woRead);
    CHECK(Ledger(r, { Wr(0x25D00002u, 2), Rd(0x25D00002u, 2, false, true) }));
    CHECK(Has(r.Text(), "Var1 = VDP1_FBCR;"));
    CHECK(!Has(r.Text(), "return param_1;"));
    CHECK(HasNote(r, "reads VDP1_FBCR, which is write-only"));
    CHECK(!r.warnings.empty() && Has(r.warnings.back(), "write-only"));

    r = run("unused read, address folds late", late);
    CHECK(Ledger(r, { Rd(0x20100063u, 1) }));
    CHECK(Has(r.Text(), "= SMPC_SF;"));

    r = run("unused read, indexed address", indexed);
    CHECK(Ledger(r, { Rd(0, 1) }));
    CHECK(Has(r.Text(), "(&SMPC_SF)[param_1]"));

    r = run("unused read, address folds late to work RAM", lateRam);
    CHECK(Ledger(r, {}));
    CHECK(!Has(r.Text(), "Var1"));   // nothing but the return

    r = run("SCSP MVOL read", mvolRead);
    CHECK(Ledger(r, { Rd(0x25B00400u, 2, false, true) }));
    CHECK(Has(r.Text(), "= SCSP_MVOL;"));
    CHECK(HasNote(r, "reads SCSP_MVOL, which is write-only"));

    r = run("SCSP MIBUF write", mibufWrite);
    CHECK(Ledger(r, { Wr(0x25B00404u, 2) }));
    CHECK(Has(r.Text(), "SCSP_MIBUF = param_1;"));
    CHECK(HasNote(r, "writes SCSP_MIBUF, which is read-only"));

    // The watchdog's write ports: one word write, its register picked by the key.
    r = run("watchdog WTCNT", wtcnt);
    CHECK(Ledger(r, { Wr(0xFFFFFE80u, 2) }));
    CHECK(Count(r.Text(), "= 0x5a00;") == 1);
    CHECK(HasNote(r, "keyed watchdog write: requests SH2_WTCNT = 0x00 (taken while SH2_WTCSR's TME is set)", false));
    CHECK(!HasNote(r, "covers") && !HasNote(r, "read-only"));
    CHECK(r.warnings.empty());

    r = run("watchdog WTCSR", wtcsr);
    CHECK(Ledger(r, { Wr(0xFFFFFE80u, 2) }));
    // A5-21: the data is 0x00, so TME (bit 5) is clear: Mednafen clears OVF whatever a read saw and resets WTCNT.
    CHECK(HasNote(r, "keyed watchdog write: SH2_WTCSR = 0x00 (TME is clear: OVF is cleared whatever a read saw, and SH2_WTCNT is reset to 0)", false));

    r = run("watchdog RSTCSR", wovf);
    CHECK(Ledger(r, { Wr(0xFFFFFE82u, 2) }));
    CHECK(Has(r.Text(), "0xa500"));
    CHECK(HasNote(r, "keyed watchdog write: clears SH2_RSTCSR's WOVF, if a read saw it set", false));

    r = run("watchdog wrong key", badKey);
    CHECK(Ledger(r, { Wr(0xFFFFFE80u, 2) }));
    CHECK(HasNote(r, "watchdog write with key 0x12", true));

    r = run("watchdog key unknown", keyVar);
    CHECK(Ledger(r, { Wr(0xFFFFFE80u, 2) }));
    CHECK(HasNote(r, "SH2_WTCNT when the upper byte is 0x5A (taken while SH2_WTCSR's TME is set)", false));
    CHECK(HasNote(r, "with TME set, OVF is cleared only if a read saw it set", false));

    r = run("divider mirror", divMirror);
    CHECK(Ledger(r, { Rd(0xFFFFFF24u, 4, false, true) }));
    CHECK(Has(r.Text(), "Ramffffff24"));
    CHECK(HasNote(r, "0xFFFFFF24 is SH2_DVDNT through a mirror", false));

    // 0xA: the associative purge for Mednafen, which SE runs. Kept, and not called VDP2_TVMD.
    r = run("0xA partition", purge5);
    CHECK(Ledger(r, { Rd(0xA5F80000u, 2, false, true) }));
    CHECK(Has(r.Text(), "a5f80000"));
    // ...and said to be the purge, not a read of anything (A5 round 4): the line for 0x05F80000 is invalidated, the read is all ones.
    CHECK(r.hardwareNotes.size() == 1 && HasNote(r, "an access at 0xA5F80000 is an associative purge, not a memory access: it invalidates the cache line for 0x05F80000 (a read returns all ones)", false));

    r = run("undocumented pair", oddPair);
    CHECK(Ledger(r, { Wr(0x25F80000u, 4) }));
    CHECK(HasNote(r, "one 4-byte write covers VDP2_TVMD and VDP2_EXTEN", true));

    r = run("UBC pair", ubcPair);
    CHECK(Ledger(r, { Wr(0xFFFFFF40u, 4) }));
    CHECK(HasNote(r, "SH2_BARA: one 4-byte write of SH2_BARAH and SH2_BARAL, as documented", false));
    CHECK(r.warnings.empty());

    r = run("sound RAM mailbox", soundMailbox);
    CHECK(Ledger(r, Mailbox(0x25A01000u)));
    CHECK(Has(r.Text(), "Ram25a01000 = 1;"));
    CHECK(Count(r.Text(), "Ram25a01000") >= 2);
    CHECK(!Has(r.Text(), "while( true )") && !Has(r.Text(), "while (true)"));

    // Work RAM folds by default (it is ordinary memory)...
    r = run("work RAM mailbox, not marked", ramMailbox);
    CHECK(Ledger(r, {}));
    CHECK(!Has(r.Text(), "Ram26001000 = 1;"));
    // ...and is kept once marked, through either partition.
    for (uint32_t marked : { 0x26001000u, 0x06001000u })
    {
        DecompileRequest q = Request(ramMailbox.base);
        q.volatileMemory.emplace_back(marked, marked + 1);
        r = d.Decompile(snap, q);
        Print("work RAM mailbox, marked", r);
        CHECK(Ledger(r, Mailbox(0x26001000u)));
        CHECK(Has(r.Text(), "Ram26001000 = 1;"));
        CHECK(Count(r.Text(), "Ram26001000") >= 2);
        CHECK(!Has(r.Text(), "while( true )") && !Has(r.Text(), "while (true)"));
    }

    // Marked once, the mailbox is kept through every address that reaches it...
    auto kept = [&](const DecompileResult& x, const char* name) {
        return Has(x.Text(), (std::string(name) + " = 1;").c_str()) && Count(x.Text(), name) >= 2 &&
               !Has(x.Text(), "while( true )") && !Has(x.Text(), "while (true)");
    };
    {
        struct { const Fixture* f; const char* name; } via[] = {
            { &repeatMailbox, "Ram26101000" }, { &farMailbox, "Ram26801000" }, { &bit27Mailbox, "Ram2e001000" } };
        for (const auto& v : via)
        {
            DecompileRequest q = Request(v.f->base);
            q.volatileMemory.emplace_back(0x26001000u, 0x26001001u);
            r = d.Decompile(snap, q);
            Print("mailbox through an alias, marked", r);
            CHECK(Ledger(r, Mailbox(v.f->pointer)));
            CHECK(kept(r, v.name));
        }
    }
    {
        struct { uint32_t first, last; const Fixture* f; const char* name; } wide[] = {
            { 0x25FFFFFCu, 0x26001003u, &repeatMailbox, "Ram26101000" },        // crosses into HWRAM
            // A whole bus period: all of it, the function's own literal pool included, so even the
            // mailbox pointer is a read now -- and the write and the poll through it are kept.
            { 0x26000000u, 0x2E000003u, &repeatMailbox, nullptr },
            { 0x27FFFFFCu, 0x28001003u, &hwramMirrorMailbox, "Ram260ffffc" },   // wraps the bus period
        };
        for (const auto& w : wide)
        {
            DecompileRequest q = Request(w.f->base);
            q.volatileMemory.emplace_back(w.first, w.last);
            r = d.Decompile(snap, q);
            Print("mailbox, wide range", r);
            if (w.name) CHECK(Ledger(r, Mailbox(w.f->pointer)));
            else CHECK(Ledger(r, { Rd(w.f->base + w.f->literal, 4), Wr(0, 2), Rd(0, 2, true) }));   // pointer, then mailbox
            if (w.name) CHECK(kept(r, w.name));
            else CHECK(Has(r.Text(), "*puVar1 = 1;") && Count(r.Text(), "*puVar1") == 3 && !Has(r.Text(), "while( true )"));
        }
    }
    // The mailbox pointer volatile too: the poll's sample is read once per pass through the loop
    // and returned as read, not dereferenced again after it (review of aa3f7d1).
    {
        DecompileRequest q = Request(ptrMailbox.base);
        q.volatileMemory.emplace_back(ptrMailbox.base + ptrMailbox.literal, ptrMailbox.base + ptrMailbox.literal + 3);
        q.volatileMemory.emplace_back(0x26001000u, 0x26001001u);
        r = d.Decompile(snap, q);
        Print("mailbox through a volatile pointer", r);
        CHECK(Ledger(r, { Rd(ptrMailbox.base + ptrMailbox.literal, 4), Wr(0, 2), Rd(0, 2, true) }));
        CHECK(Has(r.Text(), "*puVar1 = 1;"));
        CHECK(Count(r.Text(), "*puVar1") == 3);   // the declaration, the write, the one read in the loop
        CHECK(!Has(r.Text(), "return (int4)*puVar1"));
        CHECK(!Has(r.Text(), "while( true )"));
    }
    // An inverted range is no range: ignored, and said.
    {
        DecompileRequest q = Request(sharedLiteral.base);
        q.volatileMemory.emplace_back(0x26002000u, 0x26001000u);
        r = d.Decompile(snap, q);
        CHECK(Has(r.Text(), "return 0x12345678;"));
        CHECK(WarnsAbout(r, "is inverted: ignored"));
    }
    // ...and through low work RAM's second image (review of 0e3cd12).
    {
        DecompileRequest q = Request(lowRamMirrorMailbox.base);
        q.volatileMemory.emplace_back(0x20201000u, 0x20201001u);
        r = d.Decompile(snap, q);
        Print("low work RAM mailbox through its mirror", r);
        CHECK(Ledger(r, Mailbox(0x20301000u)));
        CHECK(kept(r, "Ram20301000"));
    }
    // ...and a word access is kept when the user marked only one byte of it.
    {
        DecompileRequest q = Request(ramMailbox.base);
        q.volatileMemory.emplace_back(0x26001001u, 0x26001001u);
        r = d.Decompile(snap, q);
        Print("mailbox, one byte marked", r);
        CHECK(Ledger(r, Mailbox(0x26001000u)));
        CHECK(kept(r, "Ram26001000"));
    }

    // A literal marked volatile is read, not folded from the captured bytes.
    r = run("literal, not marked", sharedLiteral);
    CHECK(Ledger(r, {}));
    CHECK(Has(r.Text(), "return 0x12345678;"));
    {
        DecompileRequest q = Request(sharedLiteral.base);
        q.volatileMemory.emplace_back(0x06009748u, 0x0600974Bu);
        r = d.Decompile(snap, q);
        Print("literal, marked", r);
        CHECK(Ledger(r, { Rd(0x06009748u, 4, false, true) }));
        CHECK(!Has(r.Text(), "0x12345678"));
        CHECK(Has(r.Text(), "Ram06009748"));
    }

    r = run("watchdog second copy", wdtMirror);
    CHECK(Ledger(r, { Wr(0xFFFFFE88u, 2) }));
    CHECK(HasNote(r, "requests SH2_WTCNT = 0x00", false));
    CHECK(!HasNote(r, "covers"));

    r = run("watchdog byte write", wdtByte);
    CHECK(Ledger(r, { Wr(0xFFFFFE81u, 1) }));
    CHECK(HasNote(r, "SH2_WTCNT takes no 1-byte write: it is written by a keyed word at 0xFFFFFE80", true));
    CHECK(!HasNote(r, "read-only"));

    r = run("VDP2 LCTA", lcta);
    CHECK(Ledger(r, { Wr(0x25F800A8u, 4) }));
    CHECK(HasNote(r, "VDP2_LCTA: one 4-byte write", false));
    CHECK(r.warnings.empty());

    // A read's own effect is explained even where nothing is written.
    r = run("SCU PPAF read", ppaf);
    CHECK(Ledger(r, { Rd(0x25FE0080u, 4, false, true) }));
    CHECK(HasNote(r, "SCU_PPAF: a read clears the DSP's V and E flags and its end interrupt", false));

    r = run("divider shadow", shadow);
    CHECK(Ledger(r, { Wr(0xFFFFFF18u, 4), Rd(0xFFFFFF18u, 4, false, true) }));
    CHECK(!HasNote(r, "read-only"));
    CHECK(HasNote(r, "SH2_DVDNTH_SHADOW: Mednafen's divider shadow register, not one in the SH7604's register table", false));
    CHECK(Has(r.Text(), "SH2_DVDNTH_SHADOW = param_1;"));

    r = run("SCU PDD read", pdd);
    CHECK(Ledger(r, { Rd(0x25FE008Cu, 4, false, true) }));
    CHECK(HasNote(r, "while the DSP is stopped, a read returns data and advances the address", false));

    r = run("read-modify-write", rmw);
    CHECK(Ledger(r, { Rd(0x25F80000u, 2), Wr(0x25F80000u, 2) }));
    r = run("a read used twice", reuse);
    CHECK(Ledger(r, { Rd(0x25F8000Au, 2) }));
    r = run("two reads through an argument", argTwice);
    CHECK(Ledger(r, { Rd(0, 2), Rd(0, 2) }));
    r = run("unused read through an argument", argUnused);
    CHECK(Ledger(r, { Rd(0, 2) }));
    r = run("write then read through an argument", argEcho);
    CHECK(Ledger(r, { Wr(0, 2), Rd(0, 2, false, true) }));
    r = run("read-modify-write through an argument", argRmw);
    CHECK(Ledger(r, { Rd(0, 2), Wr(0, 2) }));

    // A string the engine may print as its contents -- unless the bytes are volatile, when the
    // snapshot's copy is no evidence of what the string holds.
    r = run("string pointer", strPtr);
    std::printf("  (unmarked string output above)\n");
    {
        DecompileRequest q = Request(strPtr.base);
        q.volatileMemory.emplace_back(0x06009B40u, 0x06009B45u);
        r = d.Decompile(snap, q);
        Print("string pointer, string marked volatile", r);
        CHECK(!Has(r.Text(), "HELLO"));
    }

    // Diagnostics follow the direction: a read's effect is not claimed for a write, a write's
    // caution not for a read, and a split register written without being read says nothing.
    r = run("PPAF written", ppafWrite);
    CHECK(Ledger(r, { Wr(0x25FE0080u, 4) }));
    CHECK(!HasNote(r, "a read returns its status", false));   // (the split-read text is for a read after the write)
    CHECK(HasNote(r, "SCU_PPAF: a write acts on the DSP program whatever is written", false));   // A5-40
    r = run("IST written only", istWrite);
    CHECK(Ledger(r, { Wr(0x25FE00A4u, 4) }));
    CHECK(!HasNote(r, "a read returns the pending ones", false));   // (the split-read text is for a read after the write)
    CHECK(r.hardwareNotes.size() == 1 && HasNote(r, "SCU_IST: a write clears the pending interrupts whose bits are 0 (the acknowledge)", false));   // A5-76
    r = run("WTCNT read", wtcntRead);
    CHECK(Ledger(r, { Rd(0xFFFFFE81u, 1, false, true) }));
    CHECK(r.hardwareNotes.empty());   // read where it is read: nothing to say
    r = run("TVSTAT read", tvstatRead);
    // Read-only, so no caution; but the read is not free (A5-16, vdp2.c RegsRead case 0x04): it clears EXLTFG.
    CHECK(r.hardwareNotes.size() == 1 && HasNote(r, "VDP2_TVSTAT: a read clears EXLTFG", false));
    r = run("PTMR written", ptmrWrite);
    // The value is unknown: bit 0 starts drawing (vdp1.c WriteReg case 2), which the line does not say (A5 round 4).
    // ...and bit 1 arms it at the frame change (A5-74): two actions, two notes.
    CHECK(r.hardwareNotes.size() == 2 && HasNote(r, "VDP1_PTMR: a write with bit 0 set starts drawing the command table", false) &&
          HasNote(r, "VDP1_PTMR: a write with bit 1 set arms drawing to start at each frame change", false));
    // Every note sits on the instruction of the access it is about.
    for (const Fixture* f : { &woRead, &roWrite, &mvolRead, &mibufWrite, &wtcnt, &badKey, &wdtByte, &ppaf, &oddPair, &mirror })
    {
        r = d.Decompile(snap, Request(f->base));
        std::set<uint32_t> insns;
        for (const MemoryAccess& m : r.accesses) insns.insert(m.insn);
        CHECK(!r.hardwareNotes.empty());
        for (const HardwareNote& n : r.hardwareNotes) CHECK(insns.count(n.address) == 1);
    }

    // The read is printed before the command it precedes, not moved into the return after it.
    r = run("read through a pointer, then a command", readThenCommand);
    CHECK(Ledger(r, { Rd(0, 4, false, true), Wr(0x2010001Fu, 1) }));
    // ...and two FIFO reads through a volatile pointer keep their order and their roles.
    {
        DecompileRequest q = Request(fifoTwice.base);
        q.volatileMemory.emplace_back(fifoTwice.base + fifoTwice.literal, fifoTwice.base + fifoTwice.literal + 3);
        r = d.Decompile(snap, q);
        Print("two FIFO reads through a volatile pointer", r);
        CHECK(Ledger(r, { Rd(fifoTwice.base + fifoTwice.literal, 4), Rd(0, 4), Rd(0, 4) }));
    }
    // A single read used in place stays inline.
    r = run("unused read through an argument (inline check)", argUnused);

    r = run("read combined with &&", andRead);
    CHECK(Ledger(r, { Rd(0, 4) }));
    r = run("read combined with ||", orRead);
    CHECK(Ledger(r, { Rd(0, 4) }));
    r = run("byte read combined with &&", andByte);
    CHECK(Ledger(r, { Rd(0, 1) }));
    r = run("two reads combined with &&", twoBools);
    CHECK(Ledger(r, { Rd(0, 4), Rd(0, 4) }));
    // A read under a real branch stays under it: not hoisted to run unconditionally.
    r = run("read under a branch", branchRead);
    CHECK(Ledger(r, { Rd(0, 4) }));
    CHECK(!r.accesses.empty() && r.accesses[0].block != 0);

    r = run("word store through a byte pointer", wordToBytes);
    CHECK(Ledger(r, { Rd(0, 1, false, true), Wr(0, 2) }));

    // The loop step read before another read stays before it; the genuine for loop still prints
    // as one; the initialiser read stays before the read after it.
    r = run("for loop: the step read is not the body's last", forStep);
    CHECK(Ledger(r, { Rd(0, 4, true), Rd(0, 4, true, true) }));
    r = run("for loop: the step read is the body's last", forStepLast);
    CHECK(Ledger(r, { Rd(0, 4, true, true), Rd(0, 4, true) }));
    CHECK(Has(r.Text(), "for ("));
    r = run("for loop: the initialiser read is not its block's last", forInit);
    CHECK(Ledger(r, { Rd(0, 4), Rd(0, 4, false, true), Rd(0, 4, true) }));
    // The read stays in the block it is made in, before the branch, not inside one side of it.
    r = run("read before a branch, used on one side", sideRead);
    CHECK(Ledger(r, { Rd(0, 4, false, true) }));
    CHECK(!r.accesses.empty() && r.accesses[0].block == 0 && r.accesses[0].statementBlock == 0);
    r = run("read in a conditional branch's delay slot, used on one side", slotRead);
    CHECK(Ledger(r, { Rd(0, 4, false, true) }));
    CHECK(!r.accesses.empty() && r.accesses[0].block == 0 && r.accesses[0].statementBlock == 0);

    r = run("tas.b on a register", tasReg);
    CHECK(Ledger(r, { Rd(0x20100063u, 1), Wr(0x20100063u, 1) }));
    CHECK(Count(r.Text(), "SMPC_SF") >= 2);
    r = run("GBR-relative read", gbrRead);
    CHECK(Ledger(r, { Rd(0, 1, false, true) }));
    r = run("read in a return's delay slot", rtsSlot);
    CHECK(Ledger(r, { Rd(0, 4, false, true) }));
    r = run("one byte of a 4-byte read", byteOfLong);
    CHECK(Ledger(r, { Rd(0, 4) }));   // the read keeps its width; the narrowing is in the expression
    r = run("a 4-byte read stored as a byte", longToByte);
    CHECK(Ledger(r, { Rd(0, 4), Wr(0, 1) }));   // r0 is stored and returned: another use, so no return (patches/0045)
    r = run("two stores through one pointer", twoStores);
    CHECK(Ledger(r, { Wr(0, 4), Wr(0, 4) }));
    r = run("compound assignment through pointers", compound);
    CHECK(Ledger(r, { Rd(0, 4), Rd(0, 4), Wr(0, 4) }));
    r = run("two reads as call arguments", callArgs);
    CHECK(Ledger(r, { Rd(0, 4), Rd(0, 4) }));

    // The read outlives the store that consumed it.
    r = run("a read stored to a global the function overwrites", storeOverwritten);
    CHECK(Ledger(r, { Rd(0, 4) }));
    CHECK(Has(r.Text(), "*param_1"));
    // ...and the engine's record: both stores to the global became direct writes of ordinary memory
    // (the first then removed as overwritten), and nothing else ended.
    CHECK(FateAt(r, AccessFate::FoldedOrdinary, 0x0600A4C4u, true) == 1 && FateAt(r, AccessFate::FoldedOrdinary, 0x0600A4C8u, true) == 1);
    CHECK(FateAt(r, AccessFate::Dead, 0, false) + FateAt(r, AccessFate::Dead, 0, true) == 0);
    CHECK(FateAt(r, AccessFate::Unreachable, 0, false) + FateAt(r, AccessFate::Unreachable, 0, true) == 0);
    r = run("the same, the pointer read from ordinary RAM", ptrFromRam);
    CHECK(Ledger(r, { Rd(0, 4) }));
    // The slot read and the two stores folded; the read through the pointer was held, not dropped.
    CHECK(FateAt(r, AccessFate::FoldedOrdinary, 0x0600A784u, false) == 1);
    CHECK(FateAt(r, AccessFate::FoldedOrdinary, 0x0600A788u, true) == 1 && FateAt(r, AccessFate::FoldedOrdinary, 0x0600A78Cu, true) == 1);
    CHECK(FateAt(r, AccessFate::Dead, 0, false) == 0 && FateAt(r, AccessFate::Dead, 0x0600A786u, false) == 0);

    // Area 5, round 1: a read stays when its value's only consumers go. Each of these was dropped from
    // the output and the ledger with no note, in the reference run alike.
    r = run("a register read, then xor r0,r0", deadXor);
    CHECK(Ledger(r, { Rd(0x25FE00A4u, 1) }));
    CHECK(FateAt(r, AccessFate::Dead, 0, false) == 0 && Has(r.Text(), "SCU_IST"));
    r = run("a register read, then cmp/eq r3,r3", deadCmp);
    CHECK(Ledger(r, { Rd(0x05FE007Cu, 4) }));
    CHECK(FateAt(r, AccessFate::Dead, 0, false) == 0);
    r = run("a register read, then and #0,r0", deadAnd);
    CHECK(Ledger(r, { Rd(0x25FE00A4u, 1) }));
    CHECK(FateAt(r, AccessFate::Dead, 0, false) == 0 && Has(r.Text(), "SCU_IST"));
    {
        DecompileRequest q = Request(deadMarked.base);
        q.volatileMemory.emplace_back(0x26001030u, 0x2600103Fu);
        r = d.Decompile(snap, q);
        Print("marked RAM read, then xor r2,r2, then written back", r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(Ledger(r, { Rd(0x06001034u, 2), Wr(0x06001034u, 2) }));
        CHECK(ReferenceOk(d, snap, q, r));
        CHECK(FateAt(r, AccessFate::Dead, 0, false) == 0);
    }
    r = run("a read through a pointer, then xor r0,r0", deadPtr);
    CHECK(Ledger(r, { Rd(0, 4) }));
    CHECK(FateAt(r, AccessFate::Dead, 0, false) == 0 && Has(r.Text(), "*param_1"));
    r = run("a read through a pointer shifted out of its register", deadShift);
    CHECK(Ledger(r, { Rd(0, 4) }));
    CHECK(FateAt(r, AccessFate::Dead, 0, false) == 0 && Has(r.Text(), "*param_1"));
    r = run("a read through a pointer stored to a stack slot nothing reads", deadStack);
    CHECK(Ledger(r, { Rd(0, 4) }));
    CHECK(FateAt(r, AccessFate::Dead, 0, false) == 0 && Has(r.Text(), "*param_1"));
    // The caution that removed code is missing a read has to name it (A5-2).
    r = run("a poll of unmarked RAM that folds, then a read through a pointer, then xor r0,r0", pollThenRead);
    CHECK(Ledger(r, {}));
    CHECK(SaidMissing(r, 0x0600A90Cu) && HasNote(r, "the 1-byte read through a pointer here is missing"));
    CHECK(FateAt(r, AccessFate::Unreachable, 0x0600A90Cu, false) == 1 && FateAt(r, AccessFate::Dead, 0, false) == 0);
    // A4-1: a test of one condition, a read, a test of the same condition. The read is outside any if.
    r = run("if (c) a; hardware read; if (c) b;", twoTests);
    CHECK(Ledger(r, { Rd(0x05F800DEu, 2) }));
    CHECK(FateAt(r, AccessFate::Dead, 0, false) == 0 && !Has(r.Text(), "if ("));
    r = run("the same with a write at the read's place", twoTestsWrite);
    CHECK(Ledger(r, { Wr(0x05F800DEu, 2) }));
    CHECK(!Has(r.Text(), "if ("));

    // The folded poll: nothing is shown after it, and the missing write is said, with the cause.
    r = run("mailbox poll on unmarked RAM removes a hardware write", mailboxFold);
    CHECK(Ledger(r, {}));
    CHECK(WarnsAbout(r, "0600A48E: the 1-byte write of SMPC_SF here is missing") && WarnsAbout(r, "marked volatile"));
    CHECK(FateAt(r, AccessFate::Unreachable, 0x0600A48Eu, true) == 1);   // SMPC_SF = 0, removed with its block
    CHECK(!r.prototype.known && Has(r.prototypeWithheld, "removed as unreachable"));
    CHECK(HasNote(r, "the 1-byte write of SMPC_SF here is missing", true));
    {
        DecompileRequest q = Request(mailboxFold.base);
        q.volatileMemory.emplace_back(0x26001000u, 0x26001003u);
        r = d.Decompile(snap, q);
        Print("mailbox poll on marked RAM keeps the hardware write", r);
        CHECK(Ledger(r, { Wr(0x26001000u, 4), Rd(0x26001000u, 4, true), Wr(0x20100063u, 1) }));
        CHECK(ReferenceOk(d, snap, q, r));
        CHECK(!WarnsAbout(r, "missing") && r.prototype.known);
    }
    // The reference run is not the request's progress: the observer sees the two passes, not the
    // reference's as well (one pass: it follows the flow with the shown pass's literals and callees).
    {
        int observed = 0, entered = 0;
        d.SetPhaseObserver([&](DecompilePhase p) { if (p == DecompilePhase::Init) ++observed; });
        Sh2Decompiler::SetPhaseHookForTests([&](DecompilePhase p) { if (p == DecompilePhase::Init) ++entered; });
        r = d.Decompile(snap, Request(mailboxFold.base));
        d.SetPhaseObserver(nullptr);
        Sh2Decompiler::SetPhaseHookForTests(nullptr);
        std::printf("  mailbox poll: the observer saw %d passes begin, the engine began %d\n", observed, entered);
        CHECK(observed == 2 && entered == 3);
    }
    // The reference run cut short (the budget, a stop): the finished output stands, the removal is
    // said without its contents, and the signature is withheld.
    {
        std::atomic<bool> cancel(false);
        Sh2Decompiler::SetBeforeReferenceHookForTests([&] { cancel = true; });
        r = d.Decompile(snap, Request(mailboxFold.base), &cancel);
        Sh2Decompiler::SetBeforeReferenceHookForTests(nullptr);
        Print("mailbox poll, the reference run cancelled", r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(WarnsAbout(r, "could not be checked for hardware accesses (cancelled)"));
        CHECK(!r.prototype.known && Has(r.prototypeWithheld, "not checked"));
        CHECK(!HasNote(r, "here is missing"));
    }
    // The pointer saved in RAM: the write through it is in the removed code, so it is said.
    r = run("mailbox poll, the hardware pointer saved in RAM", ptrSavedInRam);
    CHECK(Ledger(r, {}));
    CHECK(SaidMissing(r, 0x0600A554u) && HasNote(r, "the 1-byte write through a pointer here is missing") && !Has(r.Text(), "of through"));
    CHECK(!r.prototype.known && Has(r.prototypeWithheld, "removed as unreachable"));
    {
        DecompileRequest q = Request(ptrSavedInRam.base);
        q.volatileMemory.emplace_back(0x26001000u, 0x26001003u);
        r = d.Decompile(snap, q);
        Print("...on marked RAM", r);
        CHECK(Ledger(r, { Wr(0x26001000u, 4), Rd(0x26001000u, 4, true), Wr(0x20100063u, 1) }));
        CHECK(ReferenceOk(d, snap, q, r));
        CHECK(!WarnsAbout(r, "missing") && r.prototype.known && Has(r.Text(), "SMPC_SF = 0;"));
    }
    // The switch after the poll: its bound is a literal the second pass folds, and its cases are
    // the removed code. The reference run must see them to say what they hold.
    r = run("mailbox poll, then a literal-bounded switch writing SMPC_SF", litSwitchAfterPoll);
    CHECK(Ledger(r, {}));
    CHECK(SaidMissing(r, 0x0600A636u) && SaidMissing(r, 0x0600A63Eu) && SaidMissing(r, 0x0600A646u) && SaidMissing(r, 0x0600A60Eu));
    CHECK(HasNote(r, "the 1-byte write of SMPC_SF here is missing") && !r.prototype.known);
    {
        DecompileRequest q = Request(litSwitchAfterPoll.base);
        q.volatileMemory.emplace_back(0x26001000u, 0x26001003u);
        r = d.Decompile(snap, q);
        Print("...on marked RAM", r);
        CHECK(Ledger(r, { Wr(0x26001000u, 4), Rd(0x26001000u, 4, true), Rd(0, 4), Wr(0x20100063u, 1), Wr(0x20100063u, 1),
                          Wr(0x20100063u, 1) }));
        CHECK(ReferenceOk(d, snap, q, r));
        CHECK(Has(r.Text(), "switch(*param_1)") && !WarnsAbout(r, "missing"));
    }
    r = run("two tests of the mailbox, two removed blocks, the second with a live flow predecessor", twoCuts);
    CHECK(Ledger(r, {}));
    CHECK(SaidMissing(r, 0x0600A71Au) && SaidMissing(r, 0x0600A722u) && !r.prototype.known);
    {
        // Both blocks' accesses went with them, each recorded as removed with unreachable code.
        int unreachable = 0;
        for (const AccessFate& f : r.accessFates) unreachable += f.fate == AccessFate::Unreachable;
        CHECK(unreachable >= 2);
    }
    r = run("a jump through a pointer kept in RAM", jmpPointer);
    CHECK(Has(r.prototypeWithheld, "indirect call"));
    {
        DecompileRequest q = Request(jmpPointer.base);
        q.volatileMemory.emplace_back(0x26001000u, 0x26001003u);
        r = d.Decompile(snap, q);
        Print("...marked: the pointer read stays", r);
        CHECK(Ledger(r, { Rd(0x26001000u, 4) }));
        CHECK(ReferenceOk(d, snap, q, r));
    }
    // No loop: the caution says a branch, not a loop.
    r = run("a test of the mailbox taken always one way", forwardFold);
    CHECK(Ledger(r, {}));
    CHECK(SaidMissing(r, 0x0600A690u) && HasNote(r, "after a branch it takes always the same way") && !HasNote(r, "for ever"));
    CHECK(!r.prototype.known);

    // Correct outputs the ledger must accept: the joined blocks in the code's order, and the for loop.
    r = run("a branch on a constant, the blocks joined in flow order", backSplice);
    CHECK(Ledger(r, { Rd(0, 4, false, true), Rd(0, 4) }));
    CHECK(r.accesses.size() == 2 && r.accesses[0].block == r.accesses[1].block);
    r = run("for loop: the step read last, the body's read under a branch", forCondBody);
    CHECK(Ledger(r, { Rd(0, 4, true, true), Rd(0, 4, true) }));
    CHECK(Has(r.Text(), "for ("));

    // The guarded read stays under the guard: its block is not the selector's, and the guard is an if.
    r = run("switch: a read in the guarded block", switchRead);
    CHECK(Ledger(r, { Rd(0, 4), Rd(0, 4) }));
    CHECK(r.accesses.size() == 2 && r.accesses[0].block != r.accesses[1].block);
    CHECK(Has(r.Text(), "if (3 < ") && !Has(r.Text(), "default:"));
    r = run("switch: a read in the jump's delay slot", switchSlotRead);
    CHECK(Ledger(r, { Rd(0, 4), Rd(0, 4) }));
    CHECK(r.accesses.size() == 2 && r.accesses[0].block != r.accesses[1].block);
    CHECK(Has(r.Text(), "if (3 < ") && !Has(r.Text(), "default:"));
    // With only the table load in the block, the guard is still folded into the switch's default.
    r = run("switch: the control, folded", switchFold);
    CHECK(Ledger(r, { Rd(0, 4) }));
    CHECK(FateAt(r, AccessFate::SwitchLookup, 0x0600A38Cu, false) == 1);   // the table's read, which the switch stands for
    CHECK(Has(r.Text(), "default:") && !Has(r.Text(), "if ("));
    // The table read's value is used after the switch: the read is the code's, kept, and made under
    // the guard (an if), not folded into the switch ahead of every index.
    r = run("switch: the table read's value used after the switch", switchUsed);
    CHECK(Ledger(r, { Rd(0, 2), Rd(0, 4), Wr(0, 2) }));
    CHECK(FateAt(r, AccessFate::SwitchLookup, 0x0600B40Cu, false) == 0);
    CHECK(Has(r.Text(), "if (") && !Has(r.Text(), "default:"));
    {
        const MemoryAccess* guarded = nullptr;
        const MemoryAccess* before = nullptr;
        for (const MemoryAccess& m : r.accesses)
        {
            if (m.insn == 0x0600B40Cu) guarded = &m;
            if (m.insn == 0x0600B400u) before = &m;
        }
        CHECK(guarded && guarded->printed == 1 && !guarded->conditionalInPrint);
        // The unused read ahead of the guard is not in the lookup's block: the guard was not folded over it.
        CHECK(guarded && before && guarded->block != before->block);
    }
    // A guard on one read does not bound a switch on a second read of the same pointer: both reads
    // stay, the guard is an if, and the table (unbounded now) is not recovered, so nothing is lent.
    r = run("switch: the selector is a second read of the guarded pointer", switchSecondRead);
    CHECK(Ledger(r, { Rd(0, 4), Rd(0, 4), Rd(0, 4) }));
    CHECK(Has(r.Text(), "if (*param_1 < 4)") && !Has(r.Text(), "switch"));
    CHECK(!r.prototype.known);
    // Every case the same: the switch goes, its selector read stays, and is the argument.
    r = run("switch: every case the same destination", switchSameDest);
    CHECK(Ledger(r, { Rd(0, 4) }));
    CHECK(Has(r.Text(), "= *param_1;") && Has(r.Text(), "*param_1)") && !Has(r.Text(), "switch"));

    r = run("one read tested on two paths", twoPaths);
    CHECK(Ledger(r, { Rd(0x25F80004u, 2) }));

    r = run("indexed read-modify-write", indexRmw);
    CHECK(Ledger(r, { Rd(0, 1), Rd(0, 2), Wr(0, 2) }));

    r = run("read-only write", roWrite);
    CHECK(Ledger(r, { Wr(0x25D00010u, 2) }));
    CHECK(Has(r.Text(), "VDP1_EDSR = param_1;"));
    CHECK(HasNote(r, "writes VDP1_EDSR, which is read-only"));

    // Area 5, round 2, A5-3: an access narrower than its register. The ledger lists it at its real
    // width, the line is as the engine prints it, and a caution on the instruction says what the
    // hardware does (Mednafen): the access-kind table decides, for every register of the kind.
    r = run("a byte write to a 16-bit VDP2 register", vdp2Byte1);
    CHECK(Ledger(r, { Wr(0x25F80001u, 1) }));
    CHECK(Has(r.Text(), "VDP2_TVMD._1_1_ = 3;"));
    CHECK(HasNoteAt(r, 0x0600AB04u, "a 1-byte write to VDP2_TVMD writes the whole 16-bit register: its other byte is whatever was last on the data bus"));
    CHECK(WarnsAbout(r, "0x0600AB04: a 1-byte write to VDP2_TVMD"));
    r = run("...the other byte of it", vdp2Byte0);
    CHECK(Ledger(r, { Wr(0x25F80000u, 1) }) && Has(r.Text(), "VDP2_TVMD._0_1_ = 3;"));
    CHECK(HasNoteAt(r, 0x0600AB44u, "a 1-byte write to VDP2_TVMD writes the whole 16-bit register"));
    r = run("a byte write to a 16-bit VDP1 register", vdp1Byte);
    CHECK(Ledger(r, { Wr(0x25D00001u, 1) }) && Has(r.Text(), "VDP1_TVMR._1_1_ = 3;"));
    CHECK(HasNoteAt(r, 0x0600AB84u, "a 1-byte write to VDP1_TVMR writes the whole 16-bit register"));
    r = run("a byte read of a 16-bit VDP2 register: the part is read as it is", vdp2ByteRead);
    // ...but the B bus reads 32 bits, so EXTEN at +2 is read too, latching HCNT and VCNT (A5-15): said, not a caution.
    CHECK(Ledger(r, { Rd(0x25F80001u, 1, false, true) }) && r.hardwareNotes.size() == 1 &&
          HasNote(r, "a 1-byte read at VDP2_TVMD also reads VDP2_EXTEN (the bus reads 32 bits): a read latches HCNT and VCNT while EXLTEN is 0", false));
    r = run("a word write to a 16-bit VDP2 register: the plain access", vdp2Word);
    CHECK(Ledger(r, { Wr(0x25F80000u, 2) }) && r.hardwareNotes.empty() && Has(r.Text(), "VDP2_TVMD = 3;"));

    r = run("a word write to the high half of a 32-bit on-chip register", divWordHigh);
    CHECK(Ledger(r, { Wr(0xFFFFFF04u, 2) }) && Has(r.Text(), "SH2_DVDNT._0_2_ = 3;"));
    CHECK(HasNoteAt(r, 0x0600AC84u, "a 2-byte write to SH2_DVDNT, a 32-bit register, writes the whole register with the 16-bit value (upper half 0) and starts a 32-by-32 division"));
    r = run("a word write to the low half of a 32-bit on-chip register", divWordLow);
    CHECK(Ledger(r, { Wr(0xFFFFFF06u, 2) }) && Has(r.Text(), "SH2_DVDNT._2_2_ = 3;"));
    CHECK(HasNoteAt(r, 0x0600AC44u, "a 2-byte write to the low half of SH2_DVDNT, a 32-bit register, does nothing"));
    r = run("a byte write to a 32-bit on-chip register", divByte);
    CHECK(Ledger(r, { Wr(0xFFFFFF04u, 1) }) && Has(r.Text(), "SH2_DVDNT._0_1_ = 3;"));
    CHECK(HasNoteAt(r, 0x0600ACC4u, "a 1-byte write to SH2_DVDNT, a 32-bit register, is an address error on the SH-2"));
    r = run("a byte read of a 32-bit on-chip register", divByteRead);
    CHECK(Ledger(r, { Rd(0xFFFFFF07u, 1, false, true) }));
    CHECK(HasNoteAt(r, 0x0600AD02u, "a 1-byte read of SH2_DVDNT, a 32-bit register, is an address error on the SH-2"));
    r = run("a word read at the high half of a 32-bit on-chip register", divWordReadHigh);
    CHECK(Ledger(r, { Rd(0xFFFFFF04u, 2, false, true) }) && Has(r.Text(), "SH2_DVDNT._0_2_"));
    CHECK(HasNoteAt(r, 0x0600AD42u, "a 2-byte read at SH2_DVDNT, a 32-bit register, returns its low 16 bits, not the upper half"));
    r = run("a word read at the low half: what Mednafen returns at either", divWordReadLow);
    CHECK(Ledger(r, { Rd(0xFFFFFF06u, 2, false, true) }) && r.hardwareNotes.empty());
    r = run("a long write to the 32-bit register: the plain access", divLong);
    CHECK(Ledger(r, { Wr(0xFFFFFF04u, 4) }) && r.hardwareNotes.empty());
    r = run("a word write to a keyed 32-bit register", bscWord);
    CHECK(Ledger(r, { Wr(0xFFFFFFE0u, 2) }));
    CHECK(HasNoteAt(r, 0x0600AE04u, "a 2-byte write to SH2_BCR1, a 32-bit register, does nothing: it is set only by a 4-byte write with 0xA55A"));
    r = run("a byte write to a 16-bit on-chip register at 0xFFFFFF00 and up", ubcByte);
    CHECK(Ledger(r, { Wr(0xFFFFFF40u, 1) }));
    CHECK(HasNoteAt(r, 0x0600AE44u, "a 1-byte write to SH2_BARAH, a 16-bit register, is an address error on the SH-2"));

    // A5-4: the SMPC is a byte device on the low lane. The word write names the register on the line,
    // and says it is a byte write; the byte at the even address does nothing; a long is two bus writes.
    r = run("a word write at the even address beside SMPC_COMREG", smpcWord);
    CHECK(Ledger(r, { Wr(0x2010001Eu, 2) }));
    CHECK(Has(r.Text(), "SMPC_COMREG = 0x19;") && !Has(r.Text(), "Ram2010001e"));
    CHECK(HasNoteAt(r, 0x0600AE84u, "a 2-byte write to SMPC_COMREG is a byte write: the register takes the low byte of the value and the high byte goes nowhere"));
    r = run("a byte write at the even address", smpcByteEven);
    CHECK(Ledger(r, { Wr(0x2010001Eu, 1) }));
    CHECK(HasNoteAt(r, 0x0600AEC4u, "a 1-byte write at the even address beside SMPC_COMREG does nothing"));
    r = run("a byte write at the odd address: the register", smpcByteOdd);
    CHECK(Ledger(r, { Wr(0x2010001Fu, 1) }) && Has(r.Text(), "SMPC_COMREG = 0x19;") && r.hardwareNotes.size() == 1);
    CHECK(HasNote(r, "SMPC_COMREG: issues SMPC command 0x19 (RESENAB)", false));   // A5-55: the command the byte is
    r = run("a long write: two 16-bit bus writes", smpcLong);
    CHECK(Ledger(r, { Wr(0x2010001Cu, 4) }));
    CHECK(HasNoteAt(r, 0x0600AF44u, "a 4-byte write reaching SMPC_COMREG is two 16-bit bus writes: the register takes bits 7-0 of the value"));
    r = run("a long write over two SMPC registers", smpcLongPair);
    CHECK(Ledger(r, { Wr(0x20100000u, 4) }));
    CHECK(HasNoteAt(r, 0x0600AF84u, "a 4-byte write reaching SMPC_IREG0 is two 16-bit bus writes: the register takes bits 23-16 of the value"));
    CHECK(HasNoteAt(r, 0x0600AF84u, "a 4-byte write reaching SMPC_IREG1 is two 16-bit bus writes: the register takes bits 7-0 of the value"));
    CHECK(!HasNote(r, "covers"));   // the lane note says it, not a register pair
    r = run("a word read at the even address beside SMPC_SR", smpcWordRead);
    CHECK(Ledger(r, { Rd(0x20100060u, 2, false, true) }) && Has(r.Text(), "SMPC_SR"));
    CHECK(HasNoteAt(r, 0x0600AFC2u, "a 2-byte read of SMPC_SR returns 0xFF00 or the register"));
    r = run("a byte read at the even address beside SMPC_SR", smpcByteEvenRead);
    CHECK(Ledger(r, { Rd(0x20100060u, 1, false, true) }));
    CHECK(HasNoteAt(r, 0x0600B042u, "a 1-byte read at the even address beside SMPC_SR returns 0xFF, not the register"));
    r = run("a word write through the SMPC's mirror", smpcWordMirror);
    CHECK(Ledger(r, { Wr(0x2010009Eu, 2) }) && Has(r.Text(), "Ram2010009e"));
    CHECK(HasNoteAt(r, 0x0600B004u, "0x2010009E is SMPC_COMREG through a mirror", false));
    CHECK(HasNoteAt(r, 0x0600B004u, "a 2-byte write to SMPC_COMREG is a byte write"));

    // ---- Area 5, round 3: the width table ----------------------------------------------------------
    // Every caution below is generated from a cell of the table (NarrowAccessEffects); the golden file
    // (HardwareWidthTable.golden.txt) lists them all, these pin the text and the register it lands on.
    auto W = [&](const char* name, const char* label) {
        const WidthFixture& x = wf.at(name);
        return std::make_pair(run(label, x.f), x.insn);
    };
    {
        // A5-9: a 4-byte access to the on-chip registers below 0xFFFFFF00 is an address error.
        auto rr = W("icrLong", "a long write over SH2_ICR and SH2_IPRA");
        CHECK(Ledger(rr.first, { Wr(0xFFFFFEE0u, 4) }));
        CHECK(HasNoteAt(rr.first, rr.second, "a 4-byte write to SH2_ICR, a 16-bit register, is an address error on the SH-2"));
        // (A5-31: Mednafen makes the access on the register at the long's own address; the caution is one, not one for each register
        // under its four bytes, and says what then happens)
        CHECK(HasNoteAt(rr.first, rr.second, "Mednafen then writes only SH2_ICR, the register at 0xFFFFFEE0, with bits 15-8 of the value in its high byte (the low byte is left as it was); SH2_IPRA is not written"));
        CHECK(!HasNote(rr.first, "a 4-byte write to SH2_IPRA") && !HasNote(rr.first, "covers"));
        rr = W("iprbLong", "a long write over SH2_IPRB and SH2_VCRA");
        CHECK(HasNoteAt(rr.first, rr.second, "a 4-byte write to SH2_IPRB, a 16-bit register, is an address error on the SH-2") && !HasNote(rr.first, "covers"));
        CHECK(!HasNote(rr.first, "a 4-byte write to SH2_VCRA"));
        rr = W("sbycrLong", "a long write over SH2_SBYCR and SH2_CCR");
        CHECK(HasNoteAt(rr.first, rr.second, "a 4-byte write to SH2_SBYCR, an 8-bit register, is an address error on the SH-2") &&
              HasNoteAt(rr.first, rr.second, "Mednafen then writes nothing: SH2_SBYCR and SH2_CCR are not written") &&   // no register is at 0xFFFFFE90
              !HasNote(rr.first, "a 4-byte write to SH2_CCR") && !HasNote(rr.first, "covers"));
        rr = W("vcrwdtLongRead", "a long read over SH2_VCRWDT and a gap");
        CHECK(HasNoteAt(rr.first, rr.second, "a 4-byte read of SH2_VCRWDT, a 16-bit register, is an address error on the SH-2"));

        // A5-10: a word reaches the even byte register only, with the low byte.
        rr = W("tierWord", "a word write over SH2_TIER and SH2_FTCSR");
        CHECK(Ledger(rr.first, { Wr(0xFFFFFE10u, 2) }));
        CHECK(HasNoteAt(rr.first, rr.second, "a 2-byte write to SH2_TIER, an 8-bit register, reaches only it, with the low byte of the value"));
        CHECK(HasNoteAt(rr.first, rr.second, "SH2_FTCSR, the register beside it, is not written"));
        CHECK(HasNoteAt(rr.first, rr.second, "a 2-byte write at the even address beside SH2_FTCSR does not write it: a word reaches only the register at its own address (SH2_TIER)"));
        CHECK(!HasNote(rr.first, "covers"));
        rr = W("frchWordRead", "a word read over SH2_FRCH and SH2_FRCL");
        CHECK(HasNoteAt(rr.first, rr.second, "a 2-byte read at SH2_FRCH, an 8-bit register, returns only it, in bits 7-0"));
        CHECK(HasNoteAt(rr.first, rr.second, "a 2-byte read at the even address beside SH2_FRCL does not read it"));
        CHECK(HasNoteAt(rr.first, rr.second, "SH2_FRCH: a read latches SH2_FRCL for the next low-byte read", false) && !HasNote(rr.first, "covers"));
        rr = W("tierWordRead", "a word read over SH2_TIER and SH2_FTCSR: FTCSR is not read");
        CHECK(HasNoteAt(rr.first, rr.second, "a 2-byte read at SH2_TIER, an 8-bit register, returns only it"));
        CHECK(!HasNote(rr.first, "arms clearing", false) && !HasNote(rr.first, "covers"));
        rr = W("sbycrWord", "a word write at the even address beside SH2_SBYCR");
        CHECK(Has(rr.first.Text(), "Ramfffffe90") || Has(rr.first.Text(), "fffffe90"));
        CHECK(HasNoteAt(rr.first, rr.second, "a 2-byte write at the even address beside SH2_SBYCR does not write it: a word reaches only the register at its own address, and there is none"));
        rr = W("drcr0Word", "a word write at the even address beside SH2_DRCR0");
        CHECK(HasNoteAt(rr.first, rr.second, "a 2-byte write at the even address beside SH2_DRCR0 does not write it"));

        // A5-11: the SCU's DSP ports take the whole data bus.
        rr = W("ppdByte", "a byte write to SCU_PPD");
        CHECK(Ledger(rr.first, { Wr(0x25FE0087u, 1) }));
        CHECK(HasNoteAt(rr.first, rr.second, "a 1-byte write to SCU_PPD writes the whole 32-bit register: its other bytes are whatever was last on the data bus"));
        CHECK(HasNoteAt(rr.first, rr.second, "and loads one DSP program word at the program counter and advances it"));
        rr = W("ppafByte", "a byte write to SCU_PPAF");
        CHECK(HasNoteAt(rr.first, rr.second, "a 1-byte write to SCU_PPAF writes the whole 32-bit register"));
        rr = W("pdaWord", "a word write to SCU_PDA");
        CHECK(HasNoteAt(rr.first, rr.second, "a 2-byte write to SCU_PDA writes the whole 32-bit register") &&
              HasNoteAt(rr.first, rr.second, "and sets the DSP data RAM address"));
        rr = W("ppdLong", "a long write to SCU_PPD: the register whole");
        CHECK(rr.first.hardwareNotes.empty());
        rr = W("pddWordRead", "a word read of SCU_PDD");
        CHECK(HasNoteAt(rr.first, rr.second, "a 2-byte read of SCU_PDD is a read of the whole 32-bit register"));

        // A5-12: a long write to the bus state controller takes effect only with the key.
        rr = W("bcrLong", "a long write of 3 to SH2_BCR1");
        CHECK(Ledger(rr.first, { Wr(0xFFFFFFE0u, 4) }));
        CHECK(HasNoteAt(rr.first, rr.second, "write to SH2_BCR1 without 0xA55A in the upper half (it is 0x0000): the write does nothing"));
        rr = W("bcrKeyed", "a long write of 0xA55A0003 to SH2_BCR1");
        CHECK(Has(rr.first.Text(), "SH2_BCR1 = 0xa55a0003;"));
        CHECK(HasNoteAt(rr.first, rr.second, "SH2_BCR1 keyed write: the low half 0x0003 is taken", false));
        for (const HardwareNote& n : rr.first.hardwareNotes) CHECK(!n.caution);
        rr = W("bcrUnknown", "a long write of an unknown value to SH2_BCR1");
        CHECK(HasNoteAt(rr.first, rr.second, "SH2_BCR1 takes a 4-byte write only with 0xA55A in its upper half", false));

        // A5-13: MINIT and SINIT signal on a 16-bit write.
        rr = W("minitByte", "a byte write to MINIT");
        CHECK(HasNoteAt(rr.first, rr.second, "a 1-byte write to MINIT does nothing: the signal to the other CPU takes a 16-bit write"));
        rr = W("minitLong", "a long write to MINIT");
        CHECK(HasNoteAt(rr.first, rr.second, "a 4-byte write to MINIT is two 16-bit bus writes, so it signals the other CPU twice"));
        rr = W("minitWord", "a word write to MINIT: the signal");
        CHECK(rr.first.hardwareNotes.empty());

        // A5-14: a long over one 16-bit register and a word the map does not name.
        rr = W("prirLong", "a long write over VDP2_PRIR and the reserved word");
        CHECK(HasNoteAt(rr.first, rr.second, "a 4-byte write over VDP2_PRIR is two 16-bit bus writes, high half first: VDP2_PRIR takes bits 31-16 of the value, and bits 15-0 go to the word at 0x25F800FE, which is no register in the map"));
        rr = W("ramctlLong", "a long write over the reserved word and VDP2_RAMCTL");
        CHECK(HasNoteAt(rr.first, rr.second, "VDP2_RAMCTL takes bits 15-0 of the value, and bits 31-16 go to the word at 0x25F8000C, which is no register in the map"));
        rr = W("endrLong", "a long write over VDP1_ENDR");
        CHECK(HasNoteAt(rr.first, rr.second, "VDP1_ENDR takes bits 31-16 of the value, and bits 15-0 go to the word at 0x25D0000E"));
        rr = W("dmealLong", "a long write over SCSP_DMEAL");
        CHECK(HasNoteAt(rr.first, rr.second, "SCSP_DMEAL takes bits 15-0 of the value, and bits 31-16 go to the word at 0x25B00410"));
        rr = W("hirqLongRead", "a long read of CD_HIRQ");
        CHECK(HasNoteAt(rr.first, rr.second, "a 4-byte read of CD_HIRQ reads it once and returns it in both halves"));
        rr = W("hirqLong", "a long write to CD_HIRQ");
        CHECK(HasNoteAt(rr.first, rr.second, "a 4-byte write to CD_HIRQ is two 16-bit writes to the same register"));
        rr = W("cr4Byte", "a byte write to CD_CR4");
        CHECK(HasNoteAt(rr.first, rr.second, "a 1-byte write to CD_CR4 stores that byte only: it does not start the command, which takes a 16-bit write"));

        // A5-15: a read narrower than 32 bits on the A and B buses is a 32-bit read.
        rr = W("tvmdWordRead", "a word read of VDP2_TVMD");
        CHECK(HasNoteAt(rr.first, rr.second, "a 2-byte read at VDP2_TVMD also reads VDP2_EXTEN (the bus reads 32 bits): a read latches HCNT and VCNT while EXLTEN is 0", false));
        rr = W("fifoWordRead", "a word read of the CD FIFO");
        CHECK(HasNoteAt(rr.first, rr.second, "a 2-byte read of CD_DATATRNS takes two 16-bit words off the FIFO"));
        CHECK(HasNoteAt(rr.first, rr.second, "CD_DATATRNS: a read takes the next data off the FIFO", false));
        rr = W("fifoWindowRead", "a word read of the CD FIFO in the register window");
        CHECK(!HasNote(rr.first, "takes two 16-bit words off the FIFO") && HasNote(rr.first, "CD_DATATRNS: a read takes the next data off the FIFO", false));
        rr = W("fifoWindowLong", "a long read of the CD FIFO in the register window");
        CHECK(HasNoteAt(rr.first, rr.second, "a 4-byte read of CD_DATATRNS reads it once and returns it in both halves"));
        rr = W("fifoLong", "a long read of the CD FIFO in its window");
        CHECK(!HasNote(rr.first, "both halves") && !HasNote(rr.first, "two 16-bit words off the FIFO"));

        // A5-16: read effects (said, not cautions), AIACK reads 0, CCR's mirrors.
        rr = W("extenRead", "a word read of VDP2_EXTEN");
        CHECK(HasNoteAt(rr.first, rr.second, "VDP2_EXTEN: a read latches HCNT and VCNT while EXLTEN is 0", false));
        rr = W("aiackRead", "a long read of SCU_AIACK");
        CHECK(HasNoteAt(rr.first, rr.second, "reads SCU_AIACK, which is write-only"));
        rr = W("ccrMirrorRead", "a byte read of SH2_CCR's mirror");
        CHECK(HasNoteAt(rr.first, rr.second, "0xFFFFFE93 is SH2_CCR through a mirror", false));
        rr = W("ccrMirrorWrite", "a byte write to SH2_CCR's mirror");
        CHECK(HasNoteAt(rr.first, rr.second, "a 1-byte write at 0xFFFFFE95 does nothing: SH2_CCR reads back at 0xFFFFFE93-0xFFFFFE9F, but takes writes at 0xFFFFFE92 only"));
    }


    // ---- Area 5, round 4 ---------------------------------------------------------------------------
    {
        auto R4 = [&](const char* name, const char* label) { return run(label, r4.at(name)); };
        // A5-18: the DMAC has no word case at +2: it reads 0, arms nothing, and the +0 caution does not claim +2.
        r = R4("a5_18a", "a word read at CHCR0 + 2");
        CHECK(HasNote(r, "a 2-byte read at +2 of SH2_CHCR0, a 32-bit register, returns 0 and reads nothing", true));
        CHECK(!HasNote(r, "arms clearing TE", false));
        r = R4("a5_18b", "a word read at DMAOR + 2");
        CHECK(HasNote(r, "a 2-byte read at +2 of SH2_DMAOR, a 32-bit register, returns 0 and reads nothing", true));
        CHECK(!HasNote(r, "arms clearing AE", false));
        r = R4("a5_18c", "a word read at SAR0 + 2");
        CHECK(HasNote(r, "a 2-byte read at +2 of SH2_SAR0, a 32-bit register, returns 0 and reads nothing", true));
        r = R4("a5_18d", "a word read at CHCR0");
        CHECK(HasNote(r, "a 2-byte read at SH2_CHCR0, a 32-bit register, returns its low 16 bits, not the upper half this address suggests (a read at +2 returns 0 and reads nothing)", true));
        CHECK(!HasNote(r, "a read at +2 returns them too", true));
        CHECK(HasNote(r, "SH2_CHCR0: a read arms clearing TE", false));   // the read at +0 does arm it
        r = R4("a5_18dvsr", "a word read at DVSR + 2: the divider has the case");
        CHECK(!HasNote(r, "returns 0 and reads nothing", true));

        // A5-19: a register read through the cached partition is a cache read; through 0x2xxxxxxx and writes it is not.
        r = R4("a5_19a", "a poll of VDP2_TVSTAT through the cached partition");
        CHECK(HasNote(r, "a read of VDP2_TVSTAT through the cached partition is a cache read when the cache is on (SH2_CCR.CE): a hit returns the cached bytes without reading the hardware, so a poll of it may never see it change, and a miss reads the whole 16-byte line 0x05F80000-0x05F8000F, every register in it with its read effect", true));
        CHECK(HasNote(r, "VDP2_TVSTAT: a read clears EXLTFG", false));
        r = R4("a5_19a_through", "the same poll through 0x25F80004");
        CHECK(!HasNote(r, "cache read", true) && HasNote(r, "VDP2_TVSTAT: a read clears EXLTFG", false));
        r = R4("a5_19b", "a long read of the CD FIFO through the cached partition");
        CHECK(HasNote(r, "a read of CD_DATATRNS through the cached partition is a cache read", true));
        r = R4("a5_19c", "a byte read of SMPC_SF through the cached partition");
        CHECK(HasNote(r, "a read of SMPC_SF through the cached partition is a cache read", true));
        r = R4("a5_19write", "a write through the cached partition");
        CHECK(!HasNote(r, "cache", true));   // MemWrite case 0 falls into case 1: written through

        // A5-20: the CCR's word read returns it in both bytes; a word beside RSTCSR reads 0x00FF.
        r = R4("a5_20a", "a word read of SH2_CCR");
        CHECK(HasNote(r, "a 2-byte read at SH2_CCR, an 8-bit register, returns it in both bytes (bits 15-8 and 7-0)", true));
        CHECK(!HasNote(r, "the register beside it", true));
        r = R4("a5_20b", "a word write to SH2_CCR");
        CHECK(HasNote(r, "a 2-byte write to SH2_CCR, an 8-bit register, reaches only it, with the low byte of the value", true));
        CHECK(!HasNote(r, "the register beside it", true));
        r = R4("a5_20c", "a word read beside SH2_RSTCSR");
        CHECK(HasNote(r, "a word reaches only the register at its own address, and there is none (it reads 0x00FF)", true));

        // A5-21: the watchdog's read and write effects.
        r = R4("a5_21rstcsr", "a byte read of SH2_RSTCSR");
        CHECK(HasNote(r, "SH2_RSTCSR: a read arms clearing WOVF: write 0xA500 to it after reading 1", false));
        r = R4("a5_21wtcsr", "a byte read of SH2_WTCSR");
        CHECK(HasNote(r, "SH2_WTCSR: a read arms clearing OVF: write 0 to it after reading 1", false));
        r = R4("a5_21b", "a keyed write of 0xA51F to SH2_WTCSR");
        CHECK(HasNote(r, "keyed watchdog write: SH2_WTCSR = 0x07 (TME is clear: OVF is cleared whatever a read saw, and SH2_WTCNT is reset to 0)", false));
        r = R4("a5_21b_tme", "a keyed write of 0xA53F to SH2_WTCSR");
        CHECK(HasNote(r, "keyed watchdog write: SH2_WTCSR = 0x27 (TME is set: OVF is cleared only if a read saw it set, and SBYCR's SBY bit is cleared)", false));
        r = R4("a5_21c", "a byte write to SH2_WTCSR");
        CHECK(HasNote(r, "SH2_WTCSR takes no 1-byte write: it is written by a keyed word at 0xFFFFFE80; this write does not write it, and it cancels a pending SH2_RSTCSR WOVF clear (a read arms it)", true));

        // A5-22: any write to SMPC_SF sets it; a write of 1 is what the line says.
        r = R4("a5_22", "SMPC_SF = 0");
        CHECK(HasNote(r, "SMPC_SF: a write sets SF to 1 whatever is written (the SMPC clears it when its command ends)", false));

        // A5-23: a byte write to the CD FIFO pushes a whole word.
        r = R4("a5_23", "a byte write to the CD FIFO");
        CHECK(HasNote(r, "a 1-byte write to CD_DATATRNS pushes a whole 16-bit word into the transfer", true));

        // A5-24: the byte access to a 32-bit on-chip register is an address error and then happens: the arming note stays.
        r = R4("a5_24", "a byte read of SH2_CHCR0");
        CHECK(HasNote(r, "a 1-byte read of SH2_CHCR0, a 32-bit register, is an address error on the SH-2: it is accessed whole", true));
        CHECK(HasNote(r, "SH2_CHCR0: a read arms clearing TE", false));

        // A5-25: a misaligned access is an address error, made at the aligned address.
        r = R4("a5_25long", "a long write at 0x25FE00A2");
        CHECK(HasNote(r, "a misaligned 4-byte write at 0x25FE00A2 is an address error on the SH-2 (an access must be aligned to its width); the access is made at 0x25FE00A0 (SCU_IMS)", true));
        CHECK(!HasNote(r, "through a mirror", false));
        r = R4("a5_25word", "a word write at 0x25F80001");
        CHECK(HasNote(r, "a misaligned 2-byte write at 0x25F80001 is an address error on the SH-2 (an access must be aligned to its width); the access is made at 0x25F80000 (VDP2_TVMD)", true));
        CHECK(!HasNote(r, "VDP2_EXTEN", true));   // the access is the word at 0x25F80000, not the bytes from 0x25F80001

        // A5-26: backup RAM is a byte device on the low lane: not folded, and what it does is said.
        r = R4("a5_26", "mov.b 5 then mov.b back at backup RAM");
        CHECK(!Has(r.Text(), "return 5;"));
        CHECK(Ledger(r, { Wr(0x20180000u, 1), Rd(0x20180000u, 1, false, true) }));
        CHECK(HasNote(r, "a 1-byte write to backup RAM at an even address does nothing (backup RAM is a byte device on the low lane of the 16-bit bus: its bytes are at the odd addresses)", true));
        CHECK(HasNote(r, "a 1-byte read of backup RAM at an even address returns 0xFF, not a byte of it", true));

        // A5-27: a long access to an on-chip address below 0xFFFFFF00 is an address error whether or not a register is there.
        r = R4("a5_27", "a long write at 0xFFFFFE20");
        CHECK(HasNote(r, "a 4-byte write at 0xFFFFFE20 is an address error on the SH-2: no register is named there, and the on-chip registers at 0xFFFFFE00-0xFFFFFEFF take byte and word accesses only", true));

        // The reviewer's questions. Which reference wins: Mednafen decodes 0xE0000000-0xFFFFFFFF by A & 0x1FF.
        r = R4("q_alias", "a byte read at 0xFFFF8E10");
        CHECK(HasNote(r, "0xFFFF8E10 is SH2_TIER through a mirror (shown by address)", false));
        r = R4("q_ccr", "SH2_CCR = 0x11");
        CHECK(HasNote(r, "SH2_CCR: CP (bit 4) is set, so this write purges the whole cache", false));
        r = R4("q_ccr_ce", "SH2_CCR = 1");
        CHECK(r.hardwareNotes.empty());
        r = R4("q_purge", "a read at 0x46003304");
        CHECK(HasNote(r, "an access at 0x46003304 is an associative purge, not a memory access: it invalidates the cache line for 0x06003300 (a read returns all ones)", false));
        r = R4("q_ptmr", "VDP1_PTMR = 1");
        CHECK(HasNote(r, "VDP1_PTMR: bit 0 is set, so this write starts drawing the command table", false));
        r = R4("q_ptmr0", "VDP1_PTMR = 0");
        CHECK(r.hardwareNotes.empty());
    }

    // ---- Area 5, round 5 ---------------------------------------------------------------------------
    {
        auto R5 = [&](const char* name, const char* label) { return run(label, r5.at(name)); };

        // A5-28: a register read through a pointer merged from register literals is said as a direct read is, for each register the
        // pointer can hold, though the access is a plain load (the output prints &REG).
        r = R5("a5_28", "a poll of VDP2_TVSTAT or VDP2_VCNT through a merged pointer, cached partition");
        CHECK(Has(r.Text(), "puVar1 = &VDP2_TVSTAT;") && Has(r.Text(), "puVar1 = &VDP2_VCNT;"));
        CHECK(HasNote(r, "if the pointer holds 0x05F80004 (VDP2_TVSTAT): a read of VDP2_TVSTAT through the cached partition is a cache read when the cache is on (SH2_CCR.CE)", true));
        CHECK(HasNote(r, "if the pointer holds 0x05F8000A (VDP2_VCNT): a read of VDP2_VCNT through the cached partition is a cache read when the cache is on (SH2_CCR.CE)", true));
        CHECK(HasNote(r, "if the pointer holds 0x05F80004 (VDP2_TVSTAT): VDP2_TVSTAT: a read clears EXLTFG", false));
        CHECK(!HasNote(r, "VDP2_VCNT: a read", false));   // VCNT has no read effect
        CHECK(WarnsAbout(r, "if the pointer holds 0x05F80004 (VDP2_TVSTAT): a read of VDP2_TVSTAT through the cached partition"));   // a caution is a warning too
        r = R5("a5_28_through", "the same poll through the cache-through partition");
        CHECK(!HasNote(r, "cache read", true) && HasNote(r, "if the pointer holds 0x25F80004 (VDP2_TVSTAT): VDP2_TVSTAT: a read clears EXLTFG", false));
        r = R5("a5_28_write", "a write through a pointer that is VDP1_ENDR or VDP1_PTMR");
        CHECK(HasNote(r, "if the pointer holds 0x25D0000C (VDP1_ENDR): VDP1_ENDR: any write ends the drawing in progress, whatever is written", false));
        CHECK(!HasNote(r, "VDP1_PTMR", false));   // the value written is 0: PTMR starts nothing
        r = R5("a5_28_other", "a pointer that is VDP2_TVSTAT or a parameter");
        CHECK(HasNote(r, "if the pointer holds 0x05F80004 (VDP2_TVSTAT) (it may hold other values too): a read of VDP2_TVSTAT through the cached partition is a cache read", true));
        r = R5("a5_28_param", "a pointer that is only a parameter: no register is named");
        CHECK(r.hardwareNotes.empty());
        r = R5("a5_28_split", "a write through PDR1 or PDR2, then a read of PDR1");
        CHECK(!HasNote(r, "SMPC_PDR1: a write sets the port's output bits", false));   // the write may not be PDR1's
        r = R5("a5_28_split_direct", "control: PDR1 written, then read");
        CHECK(HasNote(r, "SMPC_PDR1: a write sets the port's output bits; a read returns its pin levels", false));
        r = R5("a5_28_minus1", "a pointer that is param_1 - 1: the number is not a register");
        CHECK(r.hardwareNotes.empty());
        r = R5("a5_28_rawconst", "param_1 + 0xFFFFFFF4: a number, not &SH2_RTCNT");
        CHECK(!Has(r.Text(), "SH2_RTCNT") && r.hardwareNotes.empty());
        r = R5("a5_28_addend", "a read at &VDP2_TVSTAT + param_1");
        CHECK(Has(r.Text(), "&VDP2_TVSTAT"));
        CHECK(HasNote(r, "if the index is 0 (the pointer is based at 0x05F80004 (VDP2_TVSTAT)): a read of VDP2_TVSTAT through the cached partition is a cache read", true));
        r = R5("a5_28_index", "(&SCU_D0EN)[param_1 * 8] = 0x101");
        CHECK(Has(r.Text(), "(&SCU_D0EN)[param_1 * 8] = 0x101;"));
        CHECK(HasNote(r, "if the index is 0 (the pointer is based at 0x25FE0010 (SCU_D0EN)): SCU_D0EN: bits 8 and 0 are set, so this write starts level-0 DMA when its start factor (SCU_D0MD bits 2-0) is 7 (start by this register)", false));

        // A5-29: a misaligned on-chip access. From 0xFFFFFF00 up the handlers align the address (the register is the aligned one's,
        // its effects happen); below it they decode the address as given (case 0x11 is FTCSR).
        r = R5("a5_29a", "a word write at 0xFFFFFF05");
        CHECK(HasNote(r, "a misaligned 2-byte write at 0xFFFFFF05 is an address error on the SH-2 (an access must be aligned to its width); the access is made at 0xFFFFFF04 (SH2_DVDNT)", true));
        CHECK(HasNote(r, "a 2-byte write to SH2_DVDNT, a 32-bit register, writes the whole register with the 16-bit value (upper half 0) and starts a 32-by-32 division", true));
        CHECK(!HasNote(r, "does nothing", true));
        r = R5("a5_29b", "a word read at 0xFFFFFF8D");
        CHECK(HasNote(r, "the access is made at 0xFFFFFF8C (SH2_CHCR0)", true));
        CHECK(HasNote(r, "SH2_CHCR0: a read arms clearing TE: write 0 to it after reading 1", false));
        CHECK(!HasNote(r, "a 2-byte read at +2 of SH2_CHCR0", true));
        r = R5("a5_29c", "a long write at 0xFFFFFF06");
        CHECK(HasNote(r, "a misaligned 4-byte write at 0xFFFFFF06 is an address error on the SH-2 (an access must be aligned to its width); the access is made at 0xFFFFFF04 (SH2_DVDNT)", true));
        r = R5("a5_29d", "a word read at 0xFFFFFE11");
        CHECK(HasNote(r, "the on-chip handlers below 0xFFFFFF00 decode the address as given, so the access is made at 0xFFFFFE11 (SH2_FTCSR)", true));
        CHECK(HasNote(r, "a 2-byte read at SH2_FTCSR, an 8-bit register, returns only it, in bits 7-0; SH2_FRCH, the register beside it, is not read", true));
        CHECK(HasNote(r, "SH2_FTCSR: a read arms clearing its flags: write 0 to a flag read as 1", false));
        CHECK(!HasNote(r, "SH2_TIER", true) && !HasNote(r, "latches SH2_FRCL", false));   // neither TIER nor FRCH is read
        r = R5("a5_29e", "a word write at 0xFFFFFE11");
        CHECK(HasNote(r, "a 2-byte write to SH2_FTCSR, an 8-bit register, reaches only it, with the low byte of the value", true) && !HasNote(r, "SH2_TIER", true));
        r = R5("a5_29f", "a word read at 0xFFFFFE61: IPRB, whole");
        CHECK(HasNote(r, "so the access is made at 0xFFFFFE61 (SH2_IPRB)", true) && !HasNote(r, "does not read it", true));
        r = R5("a5_29g", "a word read at WTCNT's address");
        CHECK(HasNote(r, "a 2-byte read at SH2_WTCNT, an 8-bit register, returns only it, in bits 7-0", true) && !HasNote(r, "SH2_WTCSR", true));

        // A5-30: the low byte of IPRB and VCRD is reserved: a byte write there stores nothing.
        r = R5("a5_29h", "a word write at 0xFFFFFE81");
        CHECK(HasNote(r, "SH2_WTCNT takes no 2-byte write: it is written by a keyed word at 0xFFFFFE80; this write does not write it", true) && !HasNote(r, "keyed watchdog write", false));
        r = R5("a5_30iprb", "a byte write to 0xFFFFFE61");
        CHECK(HasNote(r, "a 1-byte write to the low byte of SH2_IPRB does nothing: that byte is reserved, and the handler masks the store with 0xFF00 (a read there returns 0)", true));
        r = R5("a5_30vcrd", "a byte write to 0xFFFFFE69");
        CHECK(HasNote(r, "a 1-byte write to the low byte of SH2_VCRD does nothing: that byte is reserved, and the handler masks the store with 0x7F00 (a read there returns 0)", true));
        r = R5("a5_30high", "a byte write to the high byte of IPRB");
        CHECK(r.hardwareNotes.empty());
        r = R5("a5_30vcra", "a byte write to the low byte of VCRA, which takes it (0x7F7F)");
        CHECK(r.hardwareNotes.empty());
        r = R5("a5_30word", "a word write to IPRB");
        CHECK(r.hardwareNotes.empty());

        // A5-31: what Mednafen does after the address error.
        r = R5("a5_31a", "a byte write to SH2_DVDNT");
        CHECK(HasNote(r, "a 1-byte write to SH2_DVDNT, a 32-bit register, is an address error on the SH-2: it is accessed whole; Mednafen then makes the access: the register takes "
                         "the byte twice (V | V << 8, bits 15-0; the upper 16 bits are 0) and starts a 32-by-32 division", true));
        r = R5("a5_31b", "a byte read of SH2_CHCR0");
        CHECK(HasNote(r, "a 1-byte read of SH2_CHCR0, a 32-bit register, is an address error on the SH-2: it is accessed whole; Mednafen then returns bits 15-8 of the register, whichever byte was asked for", true));
        r = R5("a5_31c", "a long write at SH2_TIER");
        CHECK(HasNote(r, "a 4-byte write to SH2_TIER, an 8-bit register, is an address error on the SH-2: the registers at 0xFFFFFE00-0xFFFFFEFF take byte and word accesses only; "
                         "Mednafen then writes only SH2_TIER, the register at 0xFFFFFE10, with bits 7-0 of the value; SH2_FTCSR, SH2_FRCH and SH2_FRCL are not written", true));
        CHECK(!HasNote(r, "a 4-byte write to SH2_FTCSR", true) && !HasNote(r, "a 4-byte write to SH2_FRCH", true) && !HasNote(r, "a 4-byte write to SH2_FRCL", true));   // not repeated
        r = R5("a5_31d", "a long read at SH2_TIER");
        CHECK(HasNote(r, "a 4-byte read of SH2_TIER, an 8-bit register, is an address error on the SH-2: the registers at 0xFFFFFE00-0xFFFFFEFF take byte and word accesses only; "
                         "Mednafen then returns 0 and reads none of SH2_TIER, SH2_FTCSR, SH2_FRCH and SH2_FRCL", true));
        CHECK(!HasNote(r, "a 4-byte read of SH2_FTCSR", true));
        r = R5("a5_31iprb", "a long write at SH2_IPRB");
        CHECK(HasNote(r, "Mednafen then writes only SH2_IPRB, the register at 0xFFFFFE60, with bits 15-8 of the value in its high byte (the low byte is left as it was); SH2_VCRA is not written", true));
        r = R5("a5_31odd", "a long write at 0xFFFFFE61");
        CHECK(HasNote(r, "which a 4-byte write at its low byte does not reach: nothing is stored; SH2_VCRA and SH2_VCRB are not written", true));
        r = R5("a5_31wdt", "a long write at the watchdog");
        CHECK(HasNote(r, "Mednafen then writes only SH2_WTCSR, the register at 0xFFFFFE80, which a 4-byte write does not write (the watchdog takes only a keyed word); "
                         "the write cancels a pending SH2_RSTCSR WOVF clear; SH2_WTCNT and SH2_RSTCSR are not written", true));
        CHECK(!HasNote(r, "SH2_WTCNT takes no 4-byte write") && !HasNote(r, "SH2_RSTCSR takes no 4-byte write"));   // the registers under the long's other bytes are not written
        r = R5("a5_31ccr", "a long write to SH2_CCR with CP set");
        CHECK(HasNote(r, "SH2_CCR: CP (bit 4) is set, so this write purges the whole cache", false));
        r = R5("a5_31bsc", "a byte write to SH2_BCR1");
        CHECK(HasNote(r, "Mednafen then ignores the write: the bus state controller takes a 4-byte write with 0xA55A in its upper half only", true));

        // A5-32: writes that start or stop work whatever (or nearly whatever) is written.
        r = R5("a5_32a", "VDP1_ENDR = 0");
        CHECK(HasNote(r, "VDP1_ENDR: any write ends the drawing in progress, whatever is written", false));
        r = R5("a5_32b", "SCU_D0EN = 0x101");
        CHECK(HasNote(r, "SCU_D0EN: bits 8 and 0 are set, so this write starts level-0 DMA when its start factor (SCU_D0MD bits 2-0) is 7 (start by this register)", false));
        r = R5("a5_32level1", "SCU_D1EN = 0x101");
        CHECK(HasNote(r, "SCU_D1EN: bits 8 and 0 are set, so this write starts level-1 DMA when its start factor (SCU_D1MD bits 2-0) is 7 (start by this register)", false));
        r = R5("a5_32c", "SCU_AIACK = 1");
        CHECK(HasNote(r, "SCU_AIACK: bit 0 is set, so this write acknowledges the A-bus interrupt (the SCU may raise it again)", false));
        r = R5("a5_32dstp", "SCU_DSTP = 1");
        CHECK(HasNote(r, "SCU_DSTP: bit 0 is set, so this write stops DMA on every level", false));
        r = R5("a5_32t1md", "SCU_T1MD = 0");
        CHECK(HasNote(r, "SCU_T1MD: bit 0 (the timer enable) is clear, so this write also resets timer 0's counter", false));
        r = R5("a5_32en1", "SCU_D0EN = 1: no bit 8");
        CHECK(r.hardwareNotes.empty());
        r = R5("a5_32aiack0", "SCU_AIACK = 0");
        CHECK(r.hardwareNotes.empty());
        r = R5("a5_32word0", "SCU_D0EN word write at +0 (bits 31-16)");
        CHECK(r.hardwareNotes.empty());
        r = R5("a5_32word2", "SCU_D0EN word write at +2 with 0x0101");
        CHECK(HasNote(r, "SCU_D0EN: bits 8 and 0 are set, so this write starts level-0 DMA", false));
        r = R5("a5_32byte0", "SCU_D0EN byte write at +0");
        CHECK(r.hardwareNotes.empty());
        r = R5("a5_32byte3", "SCU_D0EN byte write at +3 with bit 0");
        CHECK(HasNote(r, "SCU_D0EN: bit 0 is set, so this write starts level-0 DMA when its start factor (SCU_D0MD bits 2-0) is 7 (start by this register), when bit 8 is already set", false));
        r = R5("a5_32t1md1", "SCU_T1MD = 0x101: the timer is enabled");
        CHECK(r.hardwareNotes.empty());

        // The reviewer's questions. A store to the BIOS ROM is dropped by the bus (ss.c: the ROM branch stores nothing), and the engine
        // folds it into ordinary memory, so the read after it is the value written: said (A5-33). The cache address array is not memory (A5-34).
        r = R5("a5_33", "mov.b 5 to 0x00001000 (BIOS ROM) and back");
        CHECK(HasNote(r, "a write to the BIOS ROM at 0x00001000 does nothing (the ROM takes no write), but the decompiler treats the address as ordinary memory: a later read of it in this function is shown as the value written, which the ROM does not hold", true));
        r = R5("a5_33_through", "the same through 0x20001000");
        CHECK(HasNote(r, "a write to the BIOS ROM at 0x20001000 does nothing", true));
        r = R5("a5_33_ram", "the same in work RAM");
        CHECK(r.hardwareNotes.empty());
        r = R5("a5_34", "a long write to 0x60000010");
        CHECK(HasNote(r, "an access at 0x60000010 is the cache address array, not a memory access: entry 1 of the way SH2_CCR selects; a write sets its tag (from the address, valid is bit 2 inverted) and its LRU bits (bits 9-4 of the value), a read returns tag, valid and LRU", false));

        // A5-45 (nightly seed 19409): the word view of an SMPC register is a symbol at the even address, and a merged pointer at it is followed.
        r = R5("a5_45", "a word read through a pointer that is &SMPC_OREG0 (the word view) or a RAM address");
        CHECK(Has(r.Text(), "puVar2 = &SMPC_OREG0;") && Has(r.Text(), "puVar2 = (uint2 *)0x26002080;"));
        CHECK(HasNote(r, "if the pointer holds 0x00100020 (SMPC_OREG0) (it may hold other values too): a read of SMPC_OREG0 through the cached partition is a cache read when the cache is on (SH2_CCR.CE)", true));
        CHECK(HasNote(r, "if the pointer holds 0x00100020 (SMPC_OREG0) (it may hold other values too): a 2-byte read of SMPC_OREG0 returns 0xFF00 or the register", true));
        r = R5("a5_45_through", "the same through the cache-through partition");
        CHECK(!HasNote(r, "cache read", true) && HasNote(r, "if the pointer holds 0x20100020 (SMPC_OREG0) (it may hold other values too): a 2-byte read of SMPC_OREG0 returns 0xFF00", true));
        r = R5("a5_45_byte", "control: a byte read at the register's own address");
        CHECK(HasNote(r, "if the pointer holds 0x00100021 (SMPC_OREG0) (it may hold other values too): a read of SMPC_OREG0 through the cached partition is a cache read", true));

        // A5-37: a constant offset from a merged register pointer is the register at base + offset, said as such.
        r = R5("a5_37a", "puVar1[4] = 0x101 from &SCU_D0R or &SCU_D1R: SCU_D0EN or SCU_D1EN");
        CHECK(Has(r.Text(), "puVar1[4] = 0x101;"));
        CHECK(HasNote(r, "if the pointer holds 0x25FE0010 (SCU_D0EN): SCU_D0EN: bits 8 and 0 are set, so this write starts level-0 DMA when its start factor (SCU_D0MD bits 2-0) is 7 (start by this register)", false));
        CHECK(HasNote(r, "if the pointer holds 0x25FE0030 (SCU_D1EN): SCU_D1EN: bits 8 and 0 are set, so this write starts level-1 DMA when its start factor (SCU_D1MD bits 2-0) is 7 (start by this register)", false));
        CHECK(!HasNote(r, "if the index is 0", false) && !HasNote(r, "through a mirror", false));
        r = R5("a5_37b", "puVar1[1] = 0x101 from &SCU_D0EN or &SCU_D1EN: SCU_D0MD or SCU_D1MD, which start nothing");
        CHECK(Has(r.Text(), "puVar1[1] = 0x101;") && r.hardwareNotes.empty());
        r = R5("a5_37c", "a word read at puVar1[2] from &VDP2_TVMD or &VDP2_EXTEN: VDP2_TVSTAT or VDP2_VRSIZE");
        CHECK(HasNote(r, "if the pointer holds 0x05F80004 (VDP2_TVSTAT): VDP2_TVSTAT: a read clears EXLTFG", false));
        CHECK(HasNote(r, "if the pointer holds 0x05F80004 (VDP2_TVSTAT): a read of VDP2_TVSTAT through the cached partition is a cache read", true));
        CHECK(HasNote(r, "if the pointer holds 0x05F80006 (VDP2_VRSIZE): a read of VDP2_VRSIZE through the cached partition is a cache read", true));
        CHECK(!HasNote(r, "latches HCNT and VCNT", false) && !HasNote(r, "if the index is 0", false));
        r = R5("a5_37_mirror", "puVar1[4] = 0x101 from the mirrors 0x25FE0100 and 0x25FE0120: SCU_D0EN and SCU_D1EN through their mirrors");
        CHECK(HasNote(r, "if the pointer holds 0x25FE0110 (SCU_D0EN): SCU_D0EN: bits 8 and 0 are set, so this write starts level-0 DMA", false));
        CHECK(HasNote(r, "if the pointer holds 0x25FE0130 (SCU_D1EN): SCU_D1EN: bits 8 and 0 are set, so this write starts level-1 DMA", false));
        CHECK(!HasNote(r, "through a mirror (shown by address)", false));   // the access is printed as puVar1[4], not by address
        r = R5("a5_37d", "puVar1[-1] = 0x101 from &SCU_D0MD or &SCU_D1MD: SCU_D0EN or SCU_D1EN");
        CHECK(Has(r.Text(), "puVar1[-1] = 0x101;"));
        CHECK(HasNote(r, "if the pointer holds 0x25FE0010 (SCU_D0EN): SCU_D0EN: bits 8 and 0 are set", false) && HasNote(r, "if the pointer holds 0x25FE0030 (SCU_D1EN): SCU_D1EN: bits 8 and 0 are set", false));

        // A5-38: a merged pointer whose values are register aliases (printed as numbers) is said as the direct access is.
        r = R5("a5_38a_direct", "control: the direct write to 0x25FE0110");
        CHECK(HasNote(r, "SCU_D0EN: bits 8 and 0 are set, so this write starts level-0 DMA", false) && HasNote(r, "0x25FE0110 is SCU_D0EN through a mirror (shown by address)", false));
        r = R5("a5_38a", "a write through a pointer that is 0x25FE0110 or 0x25FE0130");
        CHECK(Has(r.Text(), "(xunknown4 *)0x25fe0110") && Has(r.Text(), "(xunknown4 *)0x25fe0130"));
        CHECK(HasNote(r, "if the pointer holds 0x25FE0110 (SCU_D0EN): SCU_D0EN: bits 8 and 0 are set, so this write starts level-0 DMA", false));
        CHECK(HasNote(r, "if the pointer holds 0x25FE0110 (SCU_D0EN): 0x25FE0110 is SCU_D0EN through a mirror (shown by address)", false));
        CHECK(HasNote(r, "if the pointer holds 0x25FE0130 (SCU_D1EN): SCU_D1EN: bits 8 and 0 are set, so this write starts level-1 DMA", false));
        r = R5("a5_38b", "the cached poll through the mirrors 0x05F80204 and 0x05F8020A");
        CHECK(HasNote(r, "if the pointer holds 0x05F80204 (VDP2_TVSTAT): a read of VDP2_TVSTAT through the cached partition is a cache read", true));
        CHECK(HasNote(r, "if the pointer holds 0x05F8020A (VDP2_VCNT): a read of VDP2_VCNT through the cached partition is a cache read", true));
        CHECK(HasNote(r, "if the pointer holds 0x05F80204 (VDP2_TVSTAT): VDP2_TVSTAT: a read clears EXLTFG", false));
        CHECK(!HasNote(r, "stepped in a loop", false));   // a loop that reads through the same pointer does not step it

        // A5-39: a pointer stepped in a loop is not certain, and the registers after the first are said not to be said.
        r = R5("a5_39a", "five stores from &SCU_D0R, stepping: the fifth is SCU_D0EN");
        CHECK(HasNote(r, "the pointer is stepped in a loop from 0x25FE0000: the iterations after the first reach other addresses, other registers perhaps, which are not said", false));
        r = R5("a5_39b", "three word reads from &VDP2_TVSTAT, stepping");
        CHECK(HasNote(r, "if the pointer holds 0x25F80004 (VDP2_TVSTAT) (it may hold other values too): VDP2_TVSTAT: a read clears EXLTFG", false));
        CHECK(HasNote(r, "the pointer is stepped in a loop from 0x25F80004", false));

        // A5-40: SCU_PPAF and CD_CR4 writes act whatever is written.
        r = R5("a5_40a", "SCU_PPAF = 0");
        CHECK(HasNote(r, "SCU_PPAF: a write acts on the DSP program whatever is written: bit 25 pauses it, else bit 26 unpauses it, else bit 16 set runs it and clear stops it if it runs, bit 17 steps it; bit 15 loads its program counter from the value", false));
        r = R5("a5_40b", "a word write to CD_CR4");
        CHECK(HasNote(r, "CD_CR4: a 16-bit write issues the command in CD_CR1 to CD_CR4, whatever is written", false));
        r = R5("a5_40b_byte", "control: a byte write to CD_CR4");
        CHECK(!HasNote(r, "issues the command", false) && HasNote(r, "a 1-byte write to CD_CR4 stores that byte only: it does not start the command", true));

        // A5-41: a long below the split with bytes above it is an address error with no register under it; DVSR is not the register.
        r = R5("a5_41a", "a long write at 0xFFFFFEFE");
        CHECK(HasNote(r, "a 4-byte write at 0xFFFFFEFE is an address error on the SH-2: no register is named there, and the on-chip registers at 0xFFFFFE00-0xFFFFFEFF take byte and word accesses only; Mednafen then writes nothing", true));
        CHECK(!HasNote(r, "SH2_DVSR", true));
        r = R5("a5_41a_fd", "a long write at 0xFFFFFEFD");
        CHECK(HasNote(r, "Mednafen then writes nothing", true) && !HasNote(r, "SH2_DVSR", true));
        r = R5("a5_41b", "a long read at 0xFFFFFEFE");
        CHECK(HasNote(r, "Mednafen then returns 0 and reads nothing", true) && !HasNote(r, "SH2_DVSR", true));
    }

    // ---- Area 5, round 7 ----------------------------------------------------------------------------
    {
        auto R7 = [&](const char* name, const char* label) { return run(label, r7.at(name)); };
        auto accessAt = [](const DecompileResult& x, bool write, uint32_t address) {
            for (const MemoryAccess& m : x.accesses)
                if (m.write == write && m.direct && m.address == address) return true;
            return false;
        };

        // A5-50: a byte write to colour RAM stores the whole 16-bit entry from the bus; no value of the entry is carried past it.
        r = R7("a5_50", "mov.b 3 to the odd byte of colour RAM, then a word read of the entry");
        CHECK(Ledger(r, { Wr(0x25F00001u, 1), Rd(0x25F00000u, 2, false, true) }));
        CHECK(!Has(r.Text(), "CONCAT11") && Has(r.Text(), "Ram25f00001 = 3;"));
        CHECK(HasNote(r, "a 1-byte write to colour RAM at 0x25F00001 stores the whole 16-bit entry: the byte not written is the last value on the data bus", true));
        r = R7("a5_50_even", "mov.b 3 to the even byte");
        CHECK(Ledger(r, { Wr(0x25F00000u, 1), Rd(0x25F00000u, 2, false, true) }) && !Has(r.Text(), "CONCAT11"));
        CHECK(HasNote(r, "a 1-byte write to colour RAM at 0x25F00000 stores the whole 16-bit entry", true));
        r = R7("a5_50_word", "control: a word write to colour RAM stores what it says");
        CHECK(Ledger(r, {}) && Has(r.Text(), "return 3;") && r.hardwareNotes.empty() && r.aliasedMemory.empty());
        r = R7("a5_50_vram", "control: VDP2 VRAM keeps the byte it does not write (CONCAT11 is right)");
        CHECK(Ledger(r, {}) && Has(r.Text(), "CONCAT11(iRam25e00000._0_1_,3)") && r.hardwareNotes.empty() && r.aliasedMemory.empty());
        r = R7("a5_50_fb", "control: the VDP1 frame buffer");
        CHECK(Ledger(r, {}) && Has(r.Text(), "CONCAT11(") && r.hardwareNotes.empty());
        r = R7("a5_50_v1ram", "control: VDP1 VRAM");
        CHECK(Ledger(r, {}) && Has(r.Text(), "CONCAT11(") && r.hardwareNotes.empty());

        // A5-51: a pointer merged from register addresses, then moved between partitions with a mask, is followed.
        r = R7("a5_51a", "a word read through (&VDP2_TVSTAT or &VDP2_VCNT) & 0x1fffffff");
        CHECK(Has(r.Text(), "& 0x1fffffff"));
        CHECK(HasNote(r, "if the pointer holds 0x05F80004 (VDP2_TVSTAT): VDP2_TVSTAT: a read clears EXLTFG", false));
        CHECK(HasNote(r, "if the pointer holds 0x05F80004 (VDP2_TVSTAT): a read of VDP2_TVSTAT through the cached partition is a cache read", true));
        CHECK(HasNote(r, "if the pointer holds 0x05F8000A (VDP2_VCNT): a read of VDP2_VCNT through the cached partition is a cache read", true));
        CHECK(!HasNote(r, "through a mirror (shown by address)", false) && !HasNote(r, "it may hold other values too", false));
        r = R7("a5_51b", "the same read through | 0x20000000: the cache-through partition");
        CHECK(Has(r.Text(), "| 0x20000000") || Has(r.Text(), "or 0x20000000"));
        CHECK(HasNote(r, "if the pointer holds 0x25F80004 (VDP2_TVSTAT): VDP2_TVSTAT: a read clears EXLTFG", false));
        CHECK(!HasNote(r, "cache read", true));
        r = R7("a5_51c", "a store of 0x101 through (&SCU_D0EN or &SCU_D1EN) | 0x20000000");
        CHECK(HasNote(r, "if the pointer holds 0x25FE0010 (SCU_D0EN): SCU_D0EN: bits 8 and 0 are set, so this write starts level-0 DMA", false));
        CHECK(HasNote(r, "if the pointer holds 0x25FE0030 (SCU_D1EN): SCU_D1EN: bits 8 and 0 are set, so this write starts level-1 DMA", false));
        r = R7("a5_51e", "a store through (0x05FE0110 or 0x05FE0130) | 0x20000000: the mirrors of SCU_D0EN and SCU_D1EN, not shown by address");
        CHECK(HasNote(r, "if the pointer holds 0x25FE0110 (SCU_D0EN): SCU_D0EN: bits 8 and 0 are set, so this write starts level-0 DMA", false));
        CHECK(HasNote(r, "if the pointer holds 0x25FE0130 (SCU_D1EN): SCU_D1EN: bits 8 and 0 are set, so this write starts level-1 DMA", false));
        CHECK(!HasNote(r, "through a mirror (shown by address)", false));   // the access is printed as an expression of the pointer, not by address
        r = R7("a5_51d", "control: a mask that is a parameter says nothing");
        CHECK(r.hardwareNotes.empty());

        // A5-52: two names of one work-RAM word are one memory: the accesses to it are kept, in order, and the read is not folded to the other
        // name's store. (A volatile mark on the range did this before; the default request now does for the words the function names twice.)
        const std::vector<Op> twoNames = { Wr(0x26001000u, 2), Wr(0x06001000u, 2), Rd(0x26001000u, 2, false, true) };
        r = R7("a5_52a", "xRam26001000 = 5; xRam06001000 = 7; return xRam26001000");
        CHECK(Ledger(r, twoNames) && !Has(r.Text(), "return 5;") && !r.aliasedMemory.empty());
        r = R7("a5_52_repeat", "the 1 MiB repeat of high work RAM");
        CHECK(Ledger(r, { Wr(0x26100000u, 2), Wr(0x26000000u, 2), Rd(0x26100000u, 2, false, true) }) && !Has(r.Text(), "return 5;"));
        r = R7("a5_52_bit27", "bits 28-27 are not decoded");
        CHECK(Ledger(r, { Wr(0x2E001000u, 2), Wr(0x26001000u, 2), Rd(0x2E001000u, 2, false, true) }) && !Has(r.Text(), "return 5;"));
        r = R7("a5_52_low", "low work RAM through its two partitions");
        CHECK(Ledger(r, { Wr(0x20201000u, 2), Wr(0x00201000u, 2), Rd(0x20201000u, 2, false, true) }) && !Has(r.Text(), "return 5;"));
        r = R7("a5_52_long", "the long form");
        CHECK(Ledger(r, { Wr(0x26001000u, 4), Wr(0x06001000u, 4), Rd(0x26001000u, 4, false, true) }) && !Has(r.Text(), "return 5;"));
        r = R7("a5_52_same", "control: one name, the fold stands");
        CHECK(Ledger(r, {}) && Has(r.Text(), "return 7;") && r.aliasedMemory.empty());
        r = R7("a5_52_apart", "control: two words, no alias");
        CHECK(Ledger(r, {}) && Has(r.Text(), "return 5;") && r.aliasedMemory.empty());
        // The slave's idle loop (corpus hwram008 06003C46): the poll through the cached name stays a read, as the hardware reads it: the store was
        // through the other name, and the purge of its line is the barrier.
        r = R7("a5_52_idle", "store 0 through 0x26003304, purge 0x46003304, poll 0x06003304");
        CHECK(accessAt(r, true, 0x26003304u) && accessAt(r, true, 0x46003304u) && accessAt(r, false, 0x06003304u));
        CHECK(Has(r.Text(), "= Ram06003304;") || Has(r.Text(), "Ram06003304"));
        r = R7("a5_52_purge", "store 0 and poll 0x06003304, the line purged through 0x46003304 between: still a read");
        CHECK(accessAt(r, true, 0x06003304u) && accessAt(r, true, 0x46003304u) && accessAt(r, false, 0x06003304u));

        // A5-53: colour RAM (4 KiB repeat, mode 0's entry 0x400 up), VDP2 VRAM (512 KiB) and the frame buffer (256 KiB) are kept apart no longer.
        r = R7("a5_53_cram", "colour RAM repeats every 4 KiB");
        CHECK(Ledger(r, { Wr(0x25F01000u, 2), Wr(0x25F00000u, 2), Rd(0x25F01000u, 2, false, true) }) && !Has(r.Text(), "return 5;"));
        r = R7("a5_53_mode0", "in colour RAM mode 0 an entry is written 0x800 bytes on too");
        CHECK(Ledger(r, { Wr(0x25F00800u, 2), Wr(0x25F00000u, 2), Rd(0x25F00800u, 2, false, true) }) && !Has(r.Text(), "return 5;"));
        r = R7("a5_53_vram", "VDP2 VRAM repeats every 512 KiB");
        CHECK(Ledger(r, { Wr(0x25E80000u, 2), Wr(0x25E00000u, 2), Rd(0x25E80000u, 2, false, true) }) && !Has(r.Text(), "return 5;"));
        r = R7("a5_53_fb", "the VDP1 frame buffer repeats every 256 KiB");
        CHECK(Ledger(r, { Wr(0x25CC0000u, 2), Wr(0x25C80000u, 2), Rd(0x25CC0000u, 2, false, true) }) && !Has(r.Text(), "return 5;"));
        r = R7("a5_53_cram_apart", "control: two colour RAM entries");
        CHECK(Ledger(r, {}) && Has(r.Text(), "return 5;") && r.aliasedMemory.empty());

        // A5-54: the stepped-pointer note is said for a walk that starts in a register area and can reach a named register.
        r = R7("a5_54_hole", "a store loop from the SCU's hole at 0x25FE0018");
        CHECK(HasNote(r, "the pointer is stepped in a loop from 0x25FE0018: the iterations after the first reach other addresses, other registers perhaps, which are not said", false));
        r = R7("a5_54_slot", "a store loop from SCSP slot 31's registers");
        CHECK(HasNote(r, "the pointer is stepped in a loop from 0x25B003F0: the iterations after the first reach other addresses", false));
        r = R7("a5_54_stepvar", "a non-constant step from SCU_D0EN");
        CHECK(HasNote(r, "(it may hold other values too): SCU_D0EN: bits 8 and 0 are set", false) && HasNote(r, "the pointer is stepped in a loop from 0x25FE0010", false));
        r = R7("a5_54_dsp", "control: a walk through the SCSP's DSP program area, with no register near");
        CHECK(!HasNote(r, "stepped in a loop", false));

        // A5-55: writes whose action the value chooses.
        r = R7("a5_55_dtlg", "SCSP_DTLG = 0x1000");
        CHECK(HasNote(r, "SCSP_DTLG: EX (bit 12) is set, so this write starts the SCSP DMA", false));
        r = R7("a5_55_dtlg0", "control: SCSP_DTLG = 1, EX clear");
        CHECK(r.hardwareNotes.empty());
        r = R7("a5_55_kyonex", "a slot's word 0 = 0x1800");
        CHECK(HasNote(r, "SCSP slot 0 word 0: KYONEX (bit 12) is set, so this write keys every slot on or off by its own KYONB", false));
        r = R7("a5_55_kyonex0", "control: a slot's word 0 = 0x0800, KYONEX clear");
        CHECK(r.hardwareNotes.empty());
        r = R7("a5_55_scire", "SCSP_SCIRE = 0x20");
        CHECK(HasNote(r, "SCSP_SCIRE: clears the pending sound-CPU interrupts (SCSP_SCIPD) whose bits are set in the value written (0x20 here)", false));
        r = R7("a5_55_mcire", "SCSP_MCIRE = 0x20");
        CHECK(HasNote(r, "SCSP_MCIRE: clears the pending main-CPU interrupts (SCSP_MCIPD) whose bits are set in the value written (0x20 here)", false));
        r = R7("a5_55_scire0", "control: SCSP_SCIRE = 0 clears nothing");
        CHECK(r.hardwareNotes.empty());
        r = R7("a5_55_comreg", "SMPC_COMREG = 0x19");
        CHECK(HasNote(r, "SMPC_COMREG: issues SMPC command 0x19 (RESENAB)", false));
        r = R7("a5_55_fbcr", "VDP1_FBCR = 3");
        CHECK(HasNote(r, "VDP1_FBCR: bit 1 (FCM) is set, so this write requests a manual frame-buffer change", false));
        r = R7("a5_55_fbcr0", "control: VDP1_FBCR = 1, FCM clear");
        CHECK(r.hardwareNotes.empty());

        // A5-56: a read of a write-only SMPC register returns the last byte written to the SMPC; SMPC_SF's high bits are the same byte.
        r = R7("a5_56", "SMPC_IREG0 = 5; x = SMPC_IREG0");
        CHECK(HasNote(r, "reads SMPC_IREG0, which is write-only: Mednafen returns the last byte written to the SMPC, to any register (its bus buffer), not the register", true));
        CHECK(!HasNote(r, "the value is not what was written", true));
        r = R7("a5_56_sf", "SMPC_SF read whole");
        CHECK(HasNote(r, "SMPC_SF: bits 7-1 of a read are the last byte written to the SMPC (its bus buffer), not the register's", false));
        r = R7("a5_56_sf_masked", "control: SMPC_SF & 1, every game's poll");
        CHECK(r.hardwareNotes.empty());

        // ---- Area 5, round 8.
        // A5-59: the cautions end in their last words (they were formatted into buffers a few characters short).
        r = R7("a5_59_cache", "a word read of VDP2_TVSTAT through the cached partition");
        CHECK(HasNote(r, "a read of VDP2_TVSTAT through the cached partition is a cache read when the cache is on (SH2_CCR.CE): a hit returns the cached bytes "
                         "without reading the hardware, so a poll of it may never see it change, and a miss reads the whole 16-byte line 0x05F80000-0x05F8000F, "
                         "every register in it with its read effect; read it through 0x2xxxxxxx to bypass the cache", true));
        CHECK(WarnsAbout(r, "read it through 0x2xxxxxxx to bypass the cache"));
        r = R7("a5_59_wdt", "a watchdog write with key 0x12");
        CHECK(HasNote(r, "watchdog write with key 0x12, which neither SH2_WTCNT (0x5A) nor SH2_WTCSR (0xA5) takes: ignored (it still cancels a pending SH2_RSTCSR WOVF clear)", true));

        // A5-60: a pointer that a bitwise operation moves round a loop holds the operation's results too: the cache caution of the cached value is said.
        const char* const kTvstatCached = "if the pointer holds 0x05F80004 (VDP2_TVSTAT): a read of VDP2_TVSTAT through the cached partition is a cache read";
        const char* const kTvstatThrough = "if the pointer holds 0x25F80004 (VDP2_TVSTAT): VDP2_TVSTAT: a read clears EXLTFG";
        r = R7("a5_60_and", "r1 = 0x25F80004, loop 3: read @r1; r1 &= 0x1FFFFFFF");
        CHECK(HasNote(r, kTvstatCached, true) && HasNote(r, kTvstatThrough, false) && !HasNote(r, "stepped in a loop", false));
        r = R7("a5_60_or", "r1 = 0x05F80004, loop 3: read @r1; r1 |= 0x20000000");
        CHECK(HasNote(r, kTvstatCached, true) && HasNote(r, kTvstatThrough, false));
        r = R7("a5_60_xor", "r1 = 0x05F80004, loop 3: read @r1; r1 ^= 0x20000000");
        CHECK(HasNote(r, kTvstatCached, true) && HasNote(r, kTvstatThrough, false));

        r = R7("a5_60_xor_after", "r1 = 0x25F80004, loop 3: r1 ^= 0x20000000; read @r1");
        CHECK(HasNote(r, kTvstatCached, true) && HasNote(r, kTvstatThrough, false));

        // A5-61: a mask applied after an index add keeps the base the index starts from, and the note for the index 0.
        r = R7("a5_61_nomask", "control: (&SCU_D0EN)[param_1 * 4] = 0x101");
        CHECK(HasNote(r, "if the index is 0 (the pointer is based at 0x25FE0010 (SCU_D0EN)): SCU_D0EN: bits 8 and 0 are set, so this write starts level-0 DMA", false));
        r = R7("a5_61", "((&SCU_D0EN + param_1 * 4) & 0x1fffffff) = 0x101");
        CHECK(HasNote(r, "if the index is 0 (the pointer is based at 0x05FE0010 (SCU_D0EN)): SCU_D0EN: bits 8 and 0 are set, so this write starts level-0 DMA", false));

        // A5-62: the associative purge through the partition 0xA is the same purge as through 0x4: the poll stays a read, the code after it stays.
        auto accessAt8 = [](const DecompileResult& x, bool write, uint32_t address) {
            for (const MemoryAccess& m : x.accesses)
                if (m.write == write && m.direct && m.address == address) return true;
            return false;
        };
        for (const char* name : { "a5_62_part4", "a5_62" })
        {
            r = R7(name, "store 0 through 0x06003304, purge the line (0x46003304 or 0xA6003304), poll 0x06003304, then SCU_D0EN = 0x101");
            CHECK(accessAt8(r, false, 0x06003304u) && accessAt8(r, true, 0x25FE0010u) && Has(r.Text(), "SCU_D0EN = 0x101;"));
            CHECK(!Has(r.Text(), "while( true )") && !Has(r.Text(), "do { } while") && !WarnsAbout(r, "is missing"));
            CHECK(HasNote(r, "is an associative purge, not a memory access: it invalidates the cache line for 0x06003300", false));
        }

        // A5-64: writes whose action the value chooses, said from Mednafen's handlers.
        r = R7("a5_64_ireg0", "SMPC_IREG0 = 0x80");
        CHECK(HasNote(r, "SMPC_IREG0: while an INTBACK waits for its next request, the SMPC takes the byte as a continue when bit 7 equals the continue bit it expects "
                         "(set for the first request, then alternating) and ends the INTBACK when bit 6 is set; otherwise the byte is a parameter; the SMPC acts on the "
                         "write at once (the byte here has bit 7 set and bit 6 clear)", false));   // (A5-75: round 8's text read bit 7 as a fixed "continue")
        r = R7("a5_64_ims", "SCU_IMS = 0");
        CHECK(HasNote(r, "SCU_IMS: a write recalculates the SCU's interrupt at once (a set bit masks): clearing the mask bit of an interrupt that is already pending "
                         "raises it to the SH-2 on the spot", false));
        r = R7("a5_64_mobuf", "SCSP_MOBUF = 0x41");
        CHECK(HasNote(r, "SCSP_MOBUF: a write sends its low byte out as a MIDI byte, whatever it is", false));
        r = R7("a5_64_mobuf_hi", "control: a byte at the register's own address is the high byte, which is not sent");
        CHECK(r.hardwareNotes.empty());
        r = R7("a5_64_slot12", "SCSP slot 0 word 12 = 0x1234");
        CHECK(HasNote(r, "SCSP slot 0 word 12: any write stores 0 here, whatever is written", false) && !HasNote(r, "word 13", false));
        r = R7("a5_64_slot12l", "a long write over slot 0 words 12 and 13");
        CHECK(HasNote(r, "SCSP slot 0 word 12: any write stores 0 here", false) && HasNote(r, "SCSP slot 0 word 13: any write stores 0 here", false));
        r = R7("a5_64_slot11", "control: slot 0 word 11 is stored");
        CHECK(r.hardwareNotes.empty());
        r = R7("a5_64_slot10l", "control: a long over slot 0 words 10 and 11");
        CHECK(r.hardwareNotes.empty());
        r = R7("a5_64_mcieb", "SCSP_MCIEB = 0x40");
        CHECK(HasNote(r, "SCSP_MCIEB: sets which main-CPU interrupts are enabled; the request the SCSP makes of the SCU (MCIPD & MCIEB) is recalculated at once", false));
        r = R7("a5_64_scieb", "SCSP_SCIEB = 0x40");
        CHECK(HasNote(r, "SCSP_SCIEB: sets which sound-CPU interrupts are enabled; the sound-CPU interrupt is recalculated at once", false));
        r = R7("a5_64_scilv1", "SCSP_SCILV1 = 7");
        CHECK(HasNote(r, "SCSP_SCILV1: sets bit 1 of the level of each sound-CPU interrupt", false));
        r = R7("a5_64_scipd", "SCSP_SCIPD = 0x20: bit 5 requests an interrupt");
        CHECK(HasNote(r, "SCSP_SCIPD: a write of bit 5 requests sound-CPU interrupt 5", false));
        r = R7("a5_64_mcipd0", "control: SCSP_MCIPD = 0x10 sets nothing");
        CHECK(r.hardwareNotes.empty());
        r = R7("a5_64_tima", "SCSP_TIMA = 0x105");
        CHECK(HasNote(r, "SCSP_TIMA: the low byte is the timer's reload value (loaded at its next count) and bits 10-8 set its clock divider", false));

        // A5-65: the extended RAM cartridge repeats its 512 KiB bank four times in each 2 MiB half; the other bank is another memory.
        r = R7("a5_65", "xRam02400000 = 5; xRam02480000 = 7; return xRam02400000");
        CHECK(Ledger(r, { Wr(0x02400000u, 4), Wr(0x02480000u, 4), Rd(0x02400000u, 4, false, true) }) && !Has(r.Text(), "return 5;") && !r.aliasedMemory.empty());
        r = R7("a5_65_bank", "control: 0x02600000 is the other bank");
        CHECK(Ledger(r, {}) && Has(r.Text(), "return 5;") && r.aliasedMemory.empty());

        // A5-66: the SMPC_SF byte stored whole to a constant address of work RAM escapes the mask, as one stored through a pointer.
        r = R7("a5_66_param", "control: the byte stored through a pointer parameter");
        CHECK(HasNote(r, "SMPC_SF: bits 7-1 of a read are the last byte written to the SMPC (its bus buffer), not the register's", false));
        r = R7("a5_66", "the byte stored to the constant address 0x06004000");
        CHECK(HasNote(r, "SMPC_SF: bits 7-1 of a read are the last byte written to the SMPC (its bus buffer), not the register's", false));

        // A5-67: a byte store to colour RAM through a stepped or a merged pointer says what a byte store to colour RAM does.
        r = R7("a5_67_loop", "a byte store loop from 0x25F00000");
        CHECK(HasNote(r, "if the pointer holds 0x25F00000 (it may hold other values too): a 1-byte write to colour RAM at 0x25F00000 stores the whole 16-bit entry", true));
        r = R7("a5_67_merge", "a byte store through 0x25F00000 or 0x25F00002");
        CHECK(HasNote(r, "if the pointer holds 0x25F00000: a 1-byte write to colour RAM at 0x25F00000 stores the whole 16-bit entry", true) &&
              HasNote(r, "if the pointer holds 0x25F00002: a 1-byte write to colour RAM at 0x25F00002 stores the whole 16-bit entry", true));

        // A5-68: a write to VDP1_TVMR or VDP2_RAMCTL re-addresses the memory it is about: the load after it is not the store before it.
        r = R7("a5_68_tvmr", "frame buffer byte store, VDP1_TVMR = 3, load of the byte");
        CHECK(Ledger(r, { Wr(0x25C80200u, 1), Wr(0x25D00000u, 2), Rd(0x25C80200u, 1, false, true) }) && !Has(r.Text(), "return 5;") && !r.aliasedMemory.empty());
        r = R7("a5_68_ramctl", "colour RAM word store, VDP2_RAMCTL = 0x3000, load of the word");
        CHECK(Ledger(r, { Wr(0x25F00004u, 2), Wr(0x25F8000Eu, 2), Rd(0x25F00004u, 2, false, true) }) && !Has(r.Text(), "return 0x1234;") && !r.aliasedMemory.empty());
    }

    // ---- Area 5, round 9 (REVIEW_LEDGER.md A5-70 to A5-79) ----------------------------------------------------------------------------
    {
        auto R9 = [&](const char* name, const char* label) { return run(label, r9.at(name)); };
        auto accessAt = [](const DecompileResult& x, bool write, uint32_t address) {
            for (const MemoryAccess& m : x.accesses)
                if (m.write == write && m.direct && m.address == address) return true;
            return false;
        };
        const char* const kCacheNote = "a read of VDP2_TVSTAT through the cached partition is a cache read when the cache is on (SH2_CCR.CE)";
        const char* const kThroughNote = "if the pointer holds 0x25F80004 (VDP2_TVSTAT): VDP2_TVSTAT: a read clears EXLTFG";

        // A5-70: a pointer's bitwise values round a loop are closed over every path of the loop body, however the body branches.
        // (a) a mask on one path, after the read: the cached TVSTAT is read in the next turn.
        r = R9("a5_70a", "r1 = 0x25F80004; loop 3: read @r1; if (r0) r1 &= 0x1FFFFFFF");
        CHECK(HasNote(r, "if the pointer holds 0x05F80004 (VDP2_TVSTAT): ", true) && HasNote(r, kCacheNote, true) && HasNote(r, kThroughNote, false));
        r = R9("a5_70a_control", "control: the conditional mask before the read");
        CHECK(HasNote(r, "if the pointer holds 0x05F80004 (VDP2_TVSTAT): ", true) && HasNote(r, kCacheNote, true));
        // (b) two operations in one turn, p = (p and 0xFFFFFFF0) xor 4 from 0x25F80008: the reads are VDP2_HCNT, then VDP2_TVSTAT for ever.
        r = R9("a5_70b", "r1 = 0x25F80008; loop 3: read @r1; r1 = (r1 & 0xFFFFFFF0) ^ 4");
        CHECK(HasNote(r, "if the pointer holds 0x25F80004 (VDP2_TVSTAT): VDP2_TVSTAT: a read clears EXLTFG", false));
        // (c) the same with and 0xFFFFFFF9 from 0x25F80006: TVSTAT is read, VDP2_EXTEN (the xor alone: 0x25F80002) never is.
        r = R9("a5_70c", "r1 = 0x25F80006; loop 3: read @r1; r1 = (r1 & 0xFFFFFFF9) ^ 4");
        CHECK(HasNote(r, "if the pointer holds 0x25F80004 (VDP2_TVSTAT): VDP2_TVSTAT: a read clears EXLTFG", false));
        CHECK(!HasNote(r, "VDP2_EXTEN", false) && !HasNote(r, "VDP2_EXTEN", true) && !Has(r.Text(), "0x25f80002"));
        for (const HardwareNote& n : r.hardwareNotes) CHECK(!Has(n.text, "0x25F80002"));
        // (d) a mask on one path and a step on the other.
        r = R9("a5_70d", "r1 = 0x25F80004; loop 3: read @r1; if (r0) r1 &= 0x1FFFFFFF else r1 += 4");
        CHECK(HasNote(r, "if the pointer holds 0x05F80004 (VDP2_TVSTAT) (it may hold other values too): ", true) && HasNote(r, kCacheNote, true));
        CHECK(HasNote(r, "the pointer is stepped in a loop from 0x05F80004: the iterations after the first reach other addresses, other registers perhaps, which are not said", false));
        // (e) a step and a mask in one turn: the later reads are through the cached partition, which the stepped note says.
        r = R9("a5_70e", "r1 = 0x25F80004; loop 3: read @r1; r1 += 4; r1 &= 0x1FFFFFFF");
        CHECK(HasNote(r, "the pointer is stepped in a loop from 0x25F80004: the iterations after the first reach other addresses, other registers perhaps, which are not said; "
                         "they read through the cached partition (0x05F80008 is one) too, and a read of a register through the cached partition is a cache read when the cache is on",
                      true));
        r = R9("a5_70_mask_step", "r1 = 0x25F80004; loop 3: read @r1; r1 &= 0x1FFFFFFF; r1 += 4");
        CHECK(HasNote(r, "they read through the cached partition (0x05F80008 is one) too", true));
        r = R9("a5_70_join", "r1 = 0x25F80004; loop 3: read @r1; if (r0) r1 &= 0x1FFFFFFF else r1 = 0x25F8000A");
        CHECK(HasNote(r, "if the pointer holds 0x05F8000A (VDP2_VCNT): ", true) && HasNote(r, "if the pointer holds 0x05F80004 (VDP2_TVSTAT): ", true) &&
              HasNote(r, "if the pointer holds 0x25F80004 (VDP2_TVSTAT): VDP2_TVSTAT: a read clears EXLTFG", false) && !HasNote(r, "stepped in a loop", false) &&
              !HasNote(r, "it may hold other values too", true));

        // A5-83: the stepped note says the cache caution whatever is near, when the later values read through the cached partition. The
        // registers around 0x05B000C8 (an SCSP slot's) have no name, so the note was gated on a name within kSteppedReach and never said.
        r = R9("a5_83", "p = 0x35B000C8; loop 3: read @p; p = (p + 4) ^ 0x20000000");
        CHECK(HasNote(r, "the pointer is stepped in a loop from 0x35B000C8: the iterations after the first reach other addresses, other registers perhaps, which are not said; "
                         "they read through the cached partition (0x15B000CC is one) too, and a read of a register through the cached partition is a cache read when the cache is on",
                      true));
        r = R9("a5_83_step", "control: p = 0x35B000C8; loop 3: read @p; p += 4 (no cached read, no name near: nothing to say)");
        CHECK(!HasNote(r, "stepped in a loop", false) && !HasNote(r, "stepped in a loop", true));
        r = R9("a5_83_write", "control: the same walk with a write: the cache is not read by a write");
        CHECK(!HasNote(r, "cache read", true) && !HasNote(r, "cache read", false));

        // A5-71: the cache data array is 4 KiB under both partitions: the names of one cell are one memory.
        r = R9("a5_71a", "xRamc0000000 = 5; xRamc0001000 = 7; return xRamc0000000");
        CHECK(Ledger(r, { Wr(0xC0000000u, 4), Wr(0xC0001000u, 4), Rd(0xC0000000u, 4, false, true) }) && !Has(r.Text(), "return 5;") && !r.aliasedMemory.empty());
        r = R9("a5_71b", "xRam80000010 = 5; xRamc0000010 = 7; return xRam80000010");
        CHECK(Ledger(r, { Wr(0x80000010u, 4), Wr(0xC0000010u, 4), Rd(0x80000010u, 4, false, true) }) && !Has(r.Text(), "return 5;") && !r.aliasedMemory.empty());
        r = R9("a5_71_cells", "control: two cells of the array");
        CHECK(Ledger(r, {}) && Has(r.Text(), "return 5;") && r.aliasedMemory.empty());
        r = R9("a5_71_same", "control: one name");
        CHECK(Ledger(r, {}) && Has(r.Text(), "return 7;") && r.aliasedMemory.empty());

        // A5-72: the associative purge invalidates the 16-byte line of the word it names: a poll of another word of the line is a poll.
        r = R9("a5_72", "store 0 to 0x06003308, purge 0x46003304, poll 0x06003308, then SCU_D0EN = 0x101");
        CHECK(accessAt(r, false, 0x06003308u) && accessAt(r, true, 0x25FE0010u) && Has(r.Text(), "SCU_D0EN = 0x101;"));
        CHECK(!Has(r.Text(), "while( true )") && !Has(r.Text(), "do { } while") && !WarnsAbout(r, "is missing"));
        CHECK(HasNote(r, "is an associative purge, not a memory access: it invalidates the cache line for 0x06003300", false));
        r = R9("a5_72_word", "control: the purge names the polled word");
        CHECK(accessAt(r, false, 0x06003304u) && accessAt(r, true, 0x25FE0010u) && !WarnsAbout(r, "is missing"));
        r = R9("a5_72_apart", "control: the poll is in the next line");
        CHECK(!accessAt(r, false, 0x06003310u) && Has(r.Text(), "while( true )"));

        // A5-73: the A-bus cartridge areas: what a write does is the cartridge's; the folded store is cautioned.
        r = R9("a5_73", "the cartridge probe: store a pattern to 0x02400000, read it back, compare");
        CHECK(HasNote(r, "a write to the cartridge area at 0x02400000 may be dropped (no cartridge, a ROM cartridge, a backup RAM cartridge for a word or an even byte): "
                         "a later read of it in this function is shown as the value written, which the cartridge may not hold", true));
        r = R9("a5_73_backup", "a byte store to the backup RAM cartridge's area, read back");
        CHECK(HasNote(r, "a write to the cartridge area at 0x04000001 may be dropped", true));
        r = R9("a5_73_ram", "control: work RAM");
        CHECK(r.hardwareNotes.empty());

        // A5-74: VDP1_PTMR bit 1 arms drawing at each frame change; bit 0 starts it now.
        r = R9("a5_74", "VDP1_PTMR = 2");
        CHECK(HasNote(r, "VDP1_PTMR: bit 1 is set, so drawing starts at each frame change (when the frame buffers swap) until PTMR is written with bit 1 clear", false) &&
              !HasNote(r, "starts drawing the command table", false));
        r = R9("a5_74_0", "control: VDP1_PTMR = 0 disarms it");
        CHECK(r.hardwareNotes.empty());
        r = R9("a5_74_1", "VDP1_PTMR = 1");
        CHECK(HasNote(r, "VDP1_PTMR: bit 0 is set, so this write starts drawing the command table", false) && !HasNote(r, "frame change", false));
        r = R9("a5_74_3", "VDP1_PTMR = 3");
        CHECK(HasNote(r, "VDP1_PTMR: bit 0 is set, so this write starts drawing the command table", false) && HasNote(r, "VDP1_PTMR: bit 1 is set, so drawing starts at each frame change", false));

        // A5-75: the continue bit alternates (smpc.c NextContBit): a byte of 0 is the continue of every second request, not "no continue".
        r = R9("a5_75", "SMPC_IREG0 = 0");
        CHECK(HasNote(r, "SMPC_IREG0: while an INTBACK waits for its next request, the SMPC takes the byte as a continue when bit 7 equals the continue bit it expects "
                         "(set for the first request, then alternating) and ends the INTBACK when bit 6 is set; otherwise the byte is a parameter", false));
        CHECK(!HasNote(r, "here bit 7 is clear", false));
        r = R9("a5_75_break", "SMPC_IREG0 = 0x40");
        CHECK(HasNote(r, "(the byte here has bit 7 clear and bit 6 set)", false));

        // A5-76: writes that recalculate or lower an interrupt line on the spot.
        r = R9("a5_76_hirqmsk", "CD_HIRQMSK = 0xFFFF");
        CHECK(HasNote(r, "CD_HIRQMSK: a write recalculates the CD block's interrupt to the SCU at once (a set bit enables): enabling a flag that is already set raises it", false));
        r = R9("a5_76_ist", "SCU_IST = 0");
        CHECK(HasNote(r, "SCU_IST: a write clears the pending interrupts whose bits are 0 (the acknowledge)", false));
        r = R9("a5_76_hirq", "CD_HIRQ = 0");
        CHECK(HasNote(r, "CD_HIRQ: a write clears the flags whose bits are 0 (a 1 leaves a flag as it is) and recalculates the CD block's interrupt to the SCU at once", false));

        // A5-77: the SCILV registers store the low byte only: a byte at the even address stores nothing and still recalculates.
        r = R9("a5_77", "SCSP_SCILV0 high byte = 5");
        CHECK(HasNote(r, "SCSP_SCILV0: this byte is not stored (only the low byte of the register is); the sound-CPU interrupt level is recalculated at once", false) &&
              !HasNote(r, "sets bit 0 of the level", false));
        r = R9("a5_77_low", "control: SCSP_SCILV0 low byte = 5");
        CHECK(HasNote(r, "SCSP_SCILV0: sets bit 0 of the level of each sound-CPU interrupt (the low byte)", false) && !HasNote(r, "is not stored", false));

        // A5-78: a byte store to colour RAM through `param + 0x25F00000` says what a byte store there does, for the index 0.
        for (const char* name : { "a5_78a", "a5_78b" })
        {
            r = R9(name, "*(char *)(param + 0x25F00000) = 5");
            CHECK(HasNote(r, "if the index is 0 (the pointer is based at 0x25F00000): a 1-byte write to colour RAM at 0x25F00000 stores the whole 16-bit entry", true));
        }
        r = R9("a5_78_small", "control: param + 0x10");
        CHECK(r.hardwareNotes.empty());

        // A5-79: a frame change (a write to VDP1_FBCR) makes the frame buffer the other buffer: the load after it is not the store before it.
        r = R9("a5_79", "frame buffer store, VDP1_FBCR = 3, wait for the frame end, load of the same address");
        CHECK(Ledger(r, { Wr(0x25C80200u, 2), Wr(0x25D00002u, 2), Rd(0x25F80004u, 2, true), Rd(0x25C80200u, 2, false, true) }) && !Has(r.Text(), "return 0x1234;") && !r.aliasedMemory.empty());
    }

    // A5-6: both operand reads of mac.l and mac.w are accesses, not one made twice.
    r = run("mac.l @r5+,@r4+ through pointers", macPtr);
    CHECK(Ledger(r, { Rd(0, 4), Rd(0, 4) }));
    CHECK(Has(r.Text(), "*param_2") && Has(r.Text(), "*param_1"));
    r = run("mac.w through pointers", macWPtr);
    CHECK(Ledger(r, { Rd(0, 2), Rd(0, 2) }));
    r = run("mac.l @r4+,@r4+: one pointer, two reads", macSame);
    CHECK(Ledger(r, { Rd(0, 4), Rd(0, 4) }));
    r = run("mac.l of two registers", macReg);
    CHECK(Ledger(r, { Rd(0x25FE00A4u, 4), Rd(0x25FE00C8u, 4) }));

    // A5-5: two accesses of one instruction are not each other's account. A mac.l whose first operand
    // is SCU_IST and second ordinary RAM, after a poll of unmarked RAM that folds: the RAM read folds,
    // the register read goes with the removed code, and that is said.
    r = run("mac.l after a poll that folds: a register and a variable", macRemoved);
    CHECK(Ledger(r, {}));
    CHECK(SaidCount(r, 0x0600B110u) == 1 && HasNoteAt(r, 0x0600B110u, "the 4-byte read of SCU_IST here is missing"));
    CHECK(FateAt(r, AccessFate::Unreachable, 0x0600B110u, false) == 1 && FateAt(r, AccessFate::FoldedOrdinary, 0x0600B110u, false) == 1);
    CHECK(!r.prototype.known && Has(r.prototypeWithheld, "removed as unreachable"));
    r = run("...with the register second", macRemovedSecond);
    CHECK(SaidCount(r, 0x0600B190u) == 1 && HasNoteAt(r, 0x0600B190u, "the 4-byte read of SCU_IST here is missing") && !r.prototype.known);
    r = run("...with two registers: both are said", macRemovedBoth);
    CHECK(SaidCount(r, 0x0600B150u) == 2 && HasNoteAt(r, 0x0600B150u, "the 4-byte read of SCU_IST here is missing") &&
          HasNoteAt(r, 0x0600B150u, "the 4-byte read of SCU_IMS here is missing") && !r.prototype.known);
    r = run("a register read in the slot of an rte, beside its two stack pops", rteSlot);
    CHECK(SaidCount(r, 0x0600B1CEu) == 1 && HasNoteAt(r, 0x0600B1CEu, "the 4-byte read of SCU_IST here is missing") && !r.prototype.known);
    {
        // The pops are folds into the function's own frame: offsets, not bus addresses.
        int frames = 0;
        for (const AccessFate& f : r.accessFates) frames += f.frame && !f.constantAddress && f.fate == AccessFate::FoldedOrdinary;
        CHECK(frames == 2);
    }
    r = run("...with an rts for the rte: the control", rtsSlot2);
    CHECK(SaidCount(r, 0x0600B20Eu) == 1 && HasNoteAt(r, 0x0600B20Eu, "the 4-byte read of SCU_IST here is missing"));
    // The oracle itself: it must not let one access excuse another. Records and accesses are made up.
    {
        const std::function<bool(uint32_t)> none = [](uint32_t) { return false; };
        auto rd = [](uint32_t insn, uint32_t address) {
            MemoryAccess m;
            m.insn = insn;
            m.direct = address != 0;
            m.address = address;
            m.size = 4;
            return m;
        };
        auto fate = [](uint32_t insn, AccessFate::Kind kind, uint32_t address, bool frame = false) {
            AccessFate f;
            f.insn = insn;
            f.size = 4;
            f.fate = kind;
            f.address = address;
            f.constantAddress = address != 0 && !frame;
            f.frame = frame;
            return f;
        };
        const uint32_t at = 0x06009010u;
        DecompileResult fake;
        // mac's two operands: the variable that folded is the second's account, not the register's.
        fake.accessFates = { fate(at, AccessFate::FoldedOrdinary, 0x26003000u), fate(at, AccessFate::Unreachable, 0x25FE00A4u) };
        std::vector<Account> a = Accounts(fake, { rd(at, 0x25FE00A4u), rd(at, 0x26003000u) }, none);
        CHECK(a[0] == Account::Removed && a[1] == Account::Folded);
        a = Accounts(fake, { rd(at, 0x26003000u), rd(at, 0x25FE00A4u) }, none);   // in the other order
        CHECK(a[0] == Account::Folded && a[1] == Account::Removed);
        // one fold is one account: the second access of the instruction has none
        fake.accessFates = { fate(at, AccessFate::FoldedOrdinary, 0x26003000u) };
        a = Accounts(fake, { rd(at, 0x25FE00A4u), rd(at, 0x26003000u) }, none);
        CHECK(a[0] == Account::Dropped && a[1] == Account::Folded);
        a = Accounts(fake, { rd(at, 0), rd(at, 0) }, none);   // two through pointers, one record
        CHECK(a[0] == Account::Folded && a[1] == Account::Dropped);
        // one shown access is one account too
        fake.accessFates.clear();
        fake.accesses = { rd(at, 0x25FE00A4u) };
        a = Accounts(fake, { rd(at, 0x25FE00A4u), rd(at, 0x25FE00A4u) }, none);
        CHECK(a[0] == Account::Printed && a[1] == Account::Dropped);
        // a pop in the frame is no bus access: the offset -4 is not the register at 0xFFFFFFFC, and the
        // pop is not the account of the read in an rte's slot
        fake.accesses.clear();
        fake.accessFates = { fate(at, AccessFate::FoldedOrdinary, 0xFFFFFFFCu, true), fate(at, AccessFate::FoldedOrdinary, 0, true) };
        a = Accounts(fake, { rd(at, 0x25FE00A4u) }, none);
        CHECK(a[0] == Account::Dropped);
        fake.accessFates = { fate(at, AccessFate::Dead, 0xFFFFFFFCu, true) };
        CHECK(!DeadIsLoss(fake.accessFates[0], none));
        fake.accessFates[0].frame = false;
        fake.accessFates[0].constantAddress = true;   // the same number as a bus address is a register
        CHECK(DeadIsLoss(fake.accessFates[0], none));
        // the removal said once is said for one removed access
        fake.accessFates = { fate(at, AccessFate::Unreachable, 0x25FE00A4u), fate(at, AccessFate::Unreachable, 0x25FE00A0u) };
        a = Accounts(fake, { rd(at, 0x25FE00A0u), rd(at, 0x25FE00A4u) }, none);
        CHECK(a[0] == Account::Removed && a[1] == Account::Removed);
    }

    // A5-7: the removal caution names every register the access covers, and its width.
    r = run("a 4-byte write over two registers, removed", removedLong);
    CHECK(SaidMissing(r, 0x0600B34Eu) && HasNoteAt(r, 0x0600B34Eu, "the 4-byte write of VDP2_TVMD and VDP2_EXTEN here is missing"));
    r = run("a byte write, removed", removedByte);
    CHECK(HasNoteAt(r, 0x0600B38Eu, "the 1-byte write of VDP2_TVMD here is missing"));
    r = run("a word write at an SMPC register, removed", removedWord);
    CHECK(HasNoteAt(r, 0x0600B3CEu, "the 2-byte write of SMPC_COMREG here is missing"));
}

// The map itself: names through every alias the bus decodes, widths, access kinds, and the
// volatile ranges covering every register.
void TestHardwareMap()
{
    auto name = [](uint32_t a) { const HardwareRegister* r = FindHardwareRegister(a); return std::string(r ? r->name : ""); };
    CHECK(name(0x05F80000u) == "VDP2_TVMD");
    CHECK(name(0x25F80201u) == "VDP2_TVMD");    // VDP2 repeats every 512 bytes
    CHECK(name(0x2DF80000u) == "VDP2_TVMD");    // bits 28-27 are not decoded
    CHECK(name(0xA5F80000u) == "");             // 0xA: Mednafen's associative purge, not the bus
    CHECK(InHardwareRange(0xA0000000u) && InHardwareRange(0xBFFFFFFFu));
    CHECK(name(0xFFFFFF24u) == "SH2_DVDNT");    // the divider's second copy
    CHECK(name(0xFFFFFF3Cu) == "SH2_DVDNTL_SHADOW");   // Mednafen's shadow, through the same copy
    CHECK(FindHardwareRegister(0xFFFFFF18u)->access == RegisterAccess::ReadWrite);   // Mednafen writes it
    CHECK(FindHardwareRegister(0xFFFFFF18u)->about != nullptr);
    CHECK(name(0xFFFFFE88u) == "SH2_WTCSR");    // the watchdog's second copy
    CHECK(name(0xFFFFFE8Bu) == "SH2_RSTCSR");
    CHECK(name(0xFFFFFF40u) == "SH2_BARAH");
    CHECK(name(0x201000E3u) == "SMPC_SF");      // SMPC every 128 bytes
    CHECK(name(0x25FE01A4u) == "SCU_IST");      // SCU every 256 bytes
    CHECK(name(0x25B01422u) == "SCSP_SCIRE");   // SCSP every 4 KiB
    CHECK(name(0x25D00000u) == "VDP1_TVMR");
    CHECK(name(0x25D00020u) == "");             // VDP1 does not repeat
    CHECK(name(0x25890008u) == "CD_HIRQ");
    CHECK(name(0x2589000Au) == "CD_HIRQ");      // one CD register per 4 bytes
    CHECK(name(0x25818000u) == "CD_DATATRNS");
    CHECK(name(0x25890000u) == "CD_DATATRNS");  // the FIFO answers in the register window too
    CHECK(name(0x21000000u) == "MINIT");
    CHECK(name(0x21800000u) == "SINIT");
    CHECK(name(0xFFFFFF04u) == "SH2_DVDNT");
    // The access-kind table's narrow-access axis (A5-3, A5-4): which kind each block's registers have,
    // and what the kind says of an access. Mednafen: vdp1.c, vdp2.c, ss.c, sh7095.inc.
    {
        auto kind = [](uint32_t a) { const HardwareRegister* r = FindHardwareRegister(a); return r ? r->narrow : NarrowAccess::Part; };
        CHECK(kind(0x05F80000u) == NarrowAccess::WholeOnByte && kind(0x05D00000u) == NarrowAccess::WholeOnByte);
        CHECK(kind(0x0010001Fu) == NarrowAccess::ByteLane);
        CHECK(kind(0xFFFFFF04u) == NarrowAccess::OnChipLong && kind(0xFFFFFF80u) == NarrowAccess::OnChipDmac);   // A5-18: the DMAC has no word case at +2
        CHECK(kind(0xFFFFFFE0u) == NarrowAccess::OnChipKeyedLong && kind(0xFFFFFFF8u) == NarrowAccess::OnChipKeyedLong);
        CHECK(kind(0xFFFFFF40u) == NarrowAccess::OnChipWord);
        // Round 3 (the width table): below 0xFFFFFF00 a register is a byte or a half, whose word and
        // long accesses have rules of their own (they were `Part`, "taken by lane", which is only
        // the byte access); the CD block answers every 4 bytes (a long is two writes to one register).
        CHECK(kind(0xFFFFFE60u) == NarrowAccess::OnChipHalf);   // IPRB
        CHECK(kind(0xFFFFFE10u) == NarrowAccess::OnChipByte);   // TIER
        CHECK(kind(0xFFFFFE00u) == NarrowAccess::OnChipSci);    // SMR: Mednafen has no case for it
        CHECK(kind(0x05FE00A4u) == NarrowAccess::Part && kind(0x05B00400u) == NarrowAccess::Part);   // SCU masks, the SCSP takes the bytes given
        CHECK(kind(0x05FE0084u) == NarrowAccess::ScuPort && kind(0x05FE008Cu) == NarrowAccess::ScuPort);
        CHECK(kind(0x05890008u) == NarrowAccess::CdWindow && kind(0x05818000u) == NarrowAccess::CdWindow);
        CHECK(kind(0x01000000u) == NarrowAccess::Strobe && kind(0x01800000u) == NarrowAccess::Strobe);
        // Every register of a kind gets its rule: sample each block.
        size_t n = 0;
        const HardwareRegister* all = SaturnHardwareRegisters(n);
        size_t vdpBytes = 0, laneWords = 0, longs = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const HardwareRegister& g = all[i];
            const uint32_t at = g.address | (g.address < 0xE0000000u ? 0x20000000u : 0u);
            // The width table answers a direction the register has; the other is the access kind's.
            const size_t canW = g.access != RegisterAccess::ReadOnly, canR = g.access != RegisterAccess::WriteOnly;
            auto cautions = [](const std::vector<NarrowEffect>& v) { size_t k = 0; for (const NarrowEffect& e : v) k += e.caution; return k; };
            if (g.narrow == NarrowAccess::WholeOnByte)
            {
                vdpBytes += NarrowAccessEffects(at + 1, 1, true).size() == canW && NarrowAccessEffects(at, 1, true).size() == canW &&
                            cautions(NarrowAccessEffects(at, 1, false)) == 0 && NarrowAccessEffects(at, 2, true).empty();
            }
            else if (g.narrow == NarrowAccess::ByteLane)
            {
                laneWords += NarrowAccessEffects(at - 1, 2, true).size() == canW && NarrowAccessEffects(at - 1, 2, false).size() == canR &&
                             NarrowAccessEffects(at - 1, 1, true).size() == 1 && NarrowAccessEffects(at, 1, true).empty();
            }
            else if (g.narrow == NarrowAccess::OnChipLong || g.narrow == NarrowAccess::OnChipKeyedLong || g.narrow == NarrowAccess::OnChipDmac)
            {
                longs += NarrowAccessEffects(at, 1, true).size() == 1 && NarrowAccessEffects(at + 3, 1, false).size() == 1 &&
                         NarrowAccessEffects(at, 2, true).size() == 1 && NarrowAccessEffects(at + 2, 2, true).size() == 1 &&
                         NarrowAccessEffects(at, 4, true).empty();
            }
        }
        size_t wantVdp = 0, wantLane = 0, wantLong = 0;
        for (size_t i = 0; i < n; ++i)
        {
            wantVdp += all[i].narrow == NarrowAccess::WholeOnByte;
            wantLane += all[i].narrow == NarrowAccess::ByteLane;
            wantLong += all[i].narrow == NarrowAccess::OnChipLong || all[i].narrow == NarrowAccess::OnChipKeyedLong ||
                        all[i].narrow == NarrowAccess::OnChipDmac;
        }
        CHECK(wantVdp > 100 && wantLane == 48 && wantLong > 20);   // VDP1 + VDP2, the SMPC's 48 registers, the 32-bit on-chip ones
        CHECK(vdpBytes == wantVdp && laneWords == wantLane && longs == wantLong);
        // The word views: one per SMPC register, at the even address, 2 bytes, the register's name.
        size_t vn = 0;
        const HardwareRegister* views = SaturnHardwareWordViews(vn);
        CHECK(vn == wantLane);
        for (size_t i = 0; i < vn; ++i)
        {
            const HardwareRegister* odd = FindHardwareRegister(views[i].address + 1);
            CHECK(odd && std::string(odd->name) == views[i].name && !(views[i].address & 1) && views[i].size == 2 &&
                  odd->address == views[i].address + 1);
        }
        // The byte at the even address is the bus word's unused lane (A5-17): the width table has a row
        // for it, named by the word's offset, but it is not a symbol (see SaturnHardwareWordViews).
        {
            size_t pads = 0;
            for (const WidthProbe& p : WidthTable())
            {
                if (std::string(p.reg->name).compare(0, 12, "SMPC_UNUSED_") != 0) continue;
                char want[32];
                std::snprintf(want, sizeof want, "SMPC_UNUSED_%02X", unsigned(p.reg->address & 0x7Fu));
                pads += p.width == 1 && std::string(p.reg->name) == want &&
                        p.cell.rule == (p.write ? WidthRule::SmpcPadWrite : WidthRule::SmpcPadRead);
            }
            CHECK(pads == 2 * wantLane);
        }
        // An access that is the plain access the code wrote says nothing; a long to DVDNT, a word to a VDP register.
        CHECK(NarrowAccessEffects(0x25F80000u, 2, true).empty() && NarrowAccessEffects(0xFFFFFF04u, 4, false).empty());
        CHECK(NarrowAccessEffects(0xFFFFFF06u, 2, false).empty());   // the low half is what a read at +2 returns
        CHECK(NarrowAccessEffects(0xFFFFFE11u, 1, true).empty());    // a byte register
        CHECK(NarrowAccessEffects(0x04000000u, 1, true).empty());    // nothing there
        // Said by name, width and effect (the text is the output's).
        auto only = [](uint32_t a, uint32_t size, bool write) {
            const std::vector<NarrowEffect> e = NarrowAccessEffects(a, size, write);
            return e.size() == 1 ? e[0].text : std::string("<") + std::to_string(e.size()) + ">";
        };
        CHECK(Has(only(0x25F80001u, 1, true), "1-byte write to VDP2_TVMD writes the whole 16-bit register"));
        CHECK(Has(only(0xFFFFFF04u, 2, true), "starts a 32-by-32 division") && Has(only(0xFFFFFF14u, 2, true), "starts a 64-by-32 division"));
        CHECK(!Has(only(0xFFFFFF00u, 2, true), "division"));   // DVSR
        CHECK(Has(only(0x20100060u, 2, false), "returns 0xFF00 or the register"));
        CHECK(Has(only(0x0010001Eu, 1, true), "even address beside SMPC_COMREG does nothing"));
        // A long over two SMPC registers: each gets its half's low byte.
        {
            const std::vector<NarrowEffect> e = NarrowAccessEffects(0x20100000u, 4, true);
            CHECK(e.size() == 2 && Has(e[0].text, "SMPC_IREG0") && Has(e[0].text, "bits 23-16") &&
                  Has(e[1].text, "SMPC_IREG1") && Has(e[1].text, "bits 7-0"));
        }
    }
    CHECK(name(0xFFFFFE11u) == "SH2_FTCSR");
    CHECK(name(0xE0004104u) == "SH2_DVDNT");    // on-chip upper half: only bit 14 matters
    // A5 round 4: Mednafen decodes the whole partition 0xE0000000-0xFFFFFFFF by A & 0x1FF (MemRead / MemWrite case 7), and it is the
    // reference SE runs; the manual's bit 14 (and bits 28 and 12 for the low half) is real hardware's. The emulator wins.
    CHECK(name(0xE0004011u) == "SH2_FTCSR");   // was "": the lower half needed bits 28 and 12 as well
    CHECK(name(0xFFFFBF04u) == "SH2_DVDNT");   // was "": bit 14 clear
    CHECK(name(0x45F80000u) == "");             // associative purge: not memory
    CHECK(name(0x05F8000Cu) == "");             // VDP2's reserved word
    CHECK(name(0x20100062u) == "");             // SMPC's even bytes are not registers

    const HardwareRegister* r = FindHardwareRegister(0x05F80004u);
    CHECK(r && r->access == RegisterAccess::ReadOnly);    // TVSTAT
    r = FindHardwareRegister(0x05F80070u);
    CHECK(r && r->access == RegisterAccess::WriteOnly);   // SCXIN0
    r = FindHardwareRegister(0x05D00004u);
    CHECK(r && r->access == RegisterAccess::WriteOnly);   // PTMR
    r = FindHardwareRegister(0x001000E1u);
    CHECK(r && r->access == RegisterAccess::ReadOnly);    // OREG0
    r = FindHardwareRegister(0x05FE00A0u);
    CHECK(r && r->access == RegisterAccess::WriteOnly);   // IMS
    r = FindHardwareRegister(0xFFFFFF00u);
    CHECK(r && r->size == 4);
    r = FindHardwareRegister(0xFFFFFE92u);
    CHECK(r && r->size == 1);                             // CCR
    r = FindHardwareRegister(0x25B00404u);
    CHECK(r && r->access == RegisterAccess::ReadOnly);    // MIBUF
    r = FindHardwareRegister(0x25B0042Au);
    CHECK(r && r->access == RegisterAccess::WriteOnly);   // MCIEB, per Mednafen
    r = FindHardwareRegister(0x25B00408u);
    CHECK(r && r->split != nullptr);                      // MSLC written, CA read
    r = FindHardwareRegister(0xFFFFFE80u);
    CHECK(r && r->access == RegisterAccess::KeyedWrite);  // WTCSR: written only through the key
    std::string text;
    bool ignored = false;
    CHECK(KeyedWrite(0xFFFFFE80u, 2, true, 0x5A12u, text, ignored) && !ignored && Has(text, "requests SH2_WTCNT = 0x12"));
    CHECK(KeyedWrite(0xFFFFFE88u, 2, true, 0x5A12u, text, ignored) && !ignored);   // its second copy
    CHECK(KeyedWrite(0xFFFFFE8Au, 2, true, 0xA500u, text, ignored) && Has(text, "WOVF"));
    CHECK(KeyedWrite(0xFFFFFE82u, 2, false, 0, text, ignored) && Has(text, "clears WOVF if a read saw it set"));
    CHECK(DocumentedWideAccess(0x25F800A8u, 4) && DocumentedWideAccess(0x25F800ACu, 4));   // LCTA, BKTA
    CHECK(KeyedWrite(0xFFFFFE82u, 2, true, 0x5A40u, text, ignored) && !ignored && Has(text, "RSTE/RSTS = 0x40"));
    CHECK(KeyedWrite(0xFFFFFE82u, 2, true, 0xA501u, text, ignored) && ignored);
    CHECK(!KeyedWrite(0xFFFFFE80u, 1, true, 0x5Au, text, ignored));   // a byte write is no keyed write
    CHECK(DocumentedWideAccess(0x25F800A0u, 4) && std::string(DocumentedWideAccess(0x25F800A0u, 4)) == "VDP2_LSTA0");
    CHECK(DocumentedWideAccess(0x25F80000u, 4) == nullptr);

    const std::vector<const HardwareRegister*> pair = HardwareRegistersCovering(0x25F80070u, 4);
    CHECK(pair.size() == 2 && std::string(pair[0]->name) == "VDP2_SCXIN0" && std::string(pair[1]->name) == "VDP2_SCXDN0");

    size_t n = 0;
    const HardwareRegister* all = SaturnHardwareRegisters(n);
    for (size_t i = 1; i < n; ++i) CHECK(all[i - 1].address + all[i - 1].size <= all[i].address);
    for (size_t i = 0; i < n; ++i)
    {
        uint32_t c = 0;
        CHECK(HardwareAlias(all[i].address, c) && c == all[i].address);   // canonical is canonical
        CHECK(InHardwareRange(all[i].address));
        if (all[i].address < 0xE0000000u)
        {
            CHECK(InHardwareRange(all[i].address | 0x20000000u));
            CHECK(InHardwareRange(all[i].address | 0x08000000u));
        }
    }
    CHECK(InHardwareRange(0x25B00000u));    // SCSP slot registers: unnamed, still volatile
    CHECK(InHardwareRange(0x25A01000u));    // sound RAM: shared with the 68000
    auto covers = [](const std::vector<HardwareRange>& v, uint32_t a) {
        for (const HardwareRange& h : v)
            if (a >= h.first && a <= h.last) return true;
        return false;
    };
    {
        const std::vector<HardwareRange> x = VolatileAliases(0x25FFFFFCu, 0x26001003u);   // into HWRAM
        CHECK(covers(x, 0x26101000u) && covers(x, 0x07F01000u) && covers(x, 0x05FFFFFCu) && covers(x, 0x3DFFFFFCu));
        CHECK(!covers(x, 0x26002000u) && !covers(x, 0x05FFFFF8u));
        const std::vector<HardwareRange> y = VolatileAliases(0x26000000u, 0x2E000003u);   // >= 128 MiB
        CHECK(covers(y, 0x00000000u) && covers(y, 0x3FFFFFFFu) && covers(y, 0x26101000u));
        const std::vector<HardwareRange> z = VolatileAliases(0x27FFFFFCu, 0x28001003u);   // wraps the period
        CHECK(covers(z, 0x260FFFFCu) && covers(z, 0x00001000u) && covers(z, 0x38001000u) && !covers(z, 0x00002000u));
        CHECK(VolatileAliases(0x26002000u, 0x26001000u).empty());   // inverted
        CHECK(VolatileAliases(0x26001003u, 0x26001001u).empty());   // inverted within one longword
        const std::vector<HardwareRange> lw = VolatileAliases(0x20201000u, 0x20201001u);   // low work RAM
        CHECK(covers(lw, 0x20301000u) && covers(lw, 0x00301003u) && covers(lw, 0x28201000u) && !covers(lw, 0x20401000u));
        const std::vector<HardwareRange> lwEdge = VolatileAliases(0x001FFFFCu, 0x00400003u);   // across the whole area
        CHECK(covers(lwEdge, 0x00301234u) && covers(lwEdge, 0x001FFFFCu) && covers(lwEdge, 0x00400000u) && !covers(lwEdge, 0x00400004u));
        const std::vector<HardwareRange> o = VolatileAliases(0x45000000u, 0x45000003u);   // not the bus
        CHECK(o.size() == 1 && o[0].first == 0x45000000u && o[0].last == 0x45000003u);
        const std::vector<HardwareRange> m = VolatileAliases(0x3FFFFFFCu, 0x40000003u);   // bus and beyond
        CHECK(covers(m, 0x40000000u) && covers(m, 0x07FFFFFCu) && covers(m, 0x07FFFFFFu));
    }
    CHECK(!InHardwareRange(0x26001000u));   // work RAM: the user's to mark

    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    std::string error;
    CHECK(ParseAddressRanges("0x26001000-0x26001003, 25A01000\n06002000", ranges, error));
    CHECK(ranges.size() == 3 && ranges[0] == std::make_pair(0x26001000u, 0x26001003u) &&
          ranges[1] == std::make_pair(0x25A01000u, 0x25A01000u) && ranges[2].first == 0x06002000u);
    CHECK(ParseAddressRanges("", ranges, error) && ranges.empty());
    CHECK(ParseAddressRanges("26001000 - 26001003", ranges, error) && ranges.size() == 1 &&
          ranges[0] == std::make_pair(0x26001000u, 0x26001003u));
    CHECK(!ParseAddressRanges("0x10-0x08", ranges, error) && Has(error, "0x10-0x08"));
    CHECK(!ParseAddressRanges("zz", ranges, error));
    CHECK(!ParseAddressRanges("123456789", ranges, error));
    CHECK(InHardwareRange(0x40001234u));    // associative purge
    CHECK(!InHardwareRange(0x06000000u));   // work RAM
    CHECK(!InHardwareRange(0x25E00000u));   // VDP2 VRAM
    CHECK(!InHardwareRange(0xC0000000u));   // cache used as RAM
}

// VolatileAliases against an independent model of the bus decode, on seeded random ranges and
// addresses biased to the boundaries (consolidated review). The model states the rule directly:
// an address is covered when some byte of the range reaches the same physical longword. It finds
// that by listing every address that reaches the longword, not by decomposing the range.
namespace rangemodel
{
// The physical longword an address reaches, and whether it is on the bus at all.
void Decode(uint32_t x, bool& bus, uint32_t& lw)
{
    bus = (x >> 29) <= 1;
    if (!bus) { lw = x & ~3u; return; }
    uint32_t a = x & 0x07FFFFFFu;                                // bits 31-27 do not reach the bus
    if (a >= 0x06000000u) a = 0x06000000u | (a & 0x000FFFFFu);   // high work RAM repeats every 1 MiB
    else if (a >= 0x00200000u && a < 0x00400000u) a = 0x00200000u | (a & 0x000FFFFFu);   // low work RAM: 1 MiB, twice
    lw = a & ~3u;
}

bool Covered(uint32_t x, uint32_t first, uint32_t last)
{
    if (last < first) return false;
    bool bus;
    uint32_t lw;
    Decode(x, bus, lw);
    auto hits = [&](uint64_t base) { return base <= last && base + 3 >= first; };
    if (!bus) return hits(lw);
    for (uint32_t partition = 0; partition < 2; ++partition)
        for (uint32_t period = 0; period < 4; ++period)
        {
            const uint32_t top = (partition << 29) | (period << 27);
            if (lw >= 0x00200000u && lw < 0x00400000u)
            {
                if (hits(top | lw) || hits(top | (lw ^ 0x00100000u))) return true;
                continue;
            }
            if (lw < 0x06000000u)
            {
                if (hits(top | lw)) return true;
                continue;
            }
            for (uint32_t m = 0; m < 32; ++m)
                if (hits(top | (0x06000000u + m * 0x00100000u + (lw & 0x000FFFFFu)))) return true;
        }
    return false;
}
}  // namespace rangemodel

void TestVolatileRangesAgainstModel()
{
    uint64_t state = 0x5EED5A7u;
    auto next = [&]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<uint32_t>(state);
    };
    const uint32_t edges[] = { 0x00000000u, 0x00100000u, 0x00200000u, 0x00300000u, 0x05A00000u, 0x05FFFFFCu,
                               0x06000000u, 0x060FFFFCu, 0x06100000u, 0x06F00000u, 0x07FFFFFCu, 0x08000000u,
                               0x0E000000u, 0x10000000u, 0x18000000u, 0x1FFFFFFCu, 0x20000000u, 0x25FFFFFCu,
                               0x26000000u, 0x27FFFFFCu, 0x28000000u, 0x3FFFFFFCu, 0x40000000u, 0x5FFFFFFCu,
                               0xA0000000u, 0xC0000000u, 0xFFFFFE00u, 0xFFFFFFFCu };
    const size_t nEdges = sizeof edges / sizeof edges[0];
    auto endpoint = [&]() -> uint32_t {
        switch (next() % 4)
        {
        case 0: return next();
        case 1: return edges[next() % nEdges] + (next() % 9) - 4;
        default: return edges[next() % nEdges] + (next() % 0x400) - 0x200;
        }
    };
    // Every other address that decodes like 'x' on the bus, give or take a few bytes.
    auto alias = [&](uint32_t x) -> uint32_t {
        uint32_t a = x;
        if ((a >> 29) <= 1)
        {
            a = (a & 0x07FFFFFFu) | ((next() % 2) << 29) | ((next() % 4) << 27);
            if ((a & 0x07FFFFFFu) >= 0x06000000u) a = (a & ~0x01F00000u) | ((next() % 32) << 20);
        }
        return a + (next() % 9) - 4;
    };
    int checked = 0, wrong = 0;
    for (int round = 0; round < 4000; ++round)
    {
        uint32_t first = endpoint(), last;
        switch (next() % 4)
        {
        case 0: last = first + next() % 16; break;
        case 1: last = first + next() % 0x00300000u; break;
        case 2: last = first + next() % 0x20000000u; break;
        default: last = endpoint(); break;
        }
        if (last < first && next() % 2) std::swap(first, last);   // inverted ones kept too, half the time
        const std::vector<HardwareRange> got = VolatileAliases(first, last);
        auto covers = [&](uint32_t a) {
            for (const HardwareRange& h : got)
                if (a >= h.first && a <= h.last) return true;
            return false;
        };
        for (int probe = 0; probe < 48; ++probe)
        {
            uint32_t x;
            switch (probe % 6)
            {
            case 0: x = alias(first); break;
            case 1: x = alias(last); break;
            case 2: x = first + (next() % 9) - 4; break;
            case 3: x = last + (next() % 9) - 4; break;
            case 4: x = alias(first + (last - first) / 2); break;
            default: x = next(); break;
            }
            ++checked;
            if (covers(x) != rangemodel::Covered(x, first, last))
            {
                if (++wrong <= 10)
                    std::printf("  model disagrees: range %08X-%08X, address %08X: VolatileAliases %d, model %d\n", first,
                                last, x, int(covers(x)), int(rangemodel::Covered(x, first, last)));
            }
        }
    }
    std::printf("  %d addresses checked against the model, %d disagreements\n", checked, wrong);
    CHECK(wrong == 0);
}

// The width of the memory operand an SH-2 instruction reads or writes, or 0 if it has none.
// (Stack pushes and pops included: lds.l/sts.l/ldc.l/stc.l move longwords.)
int MemoryWidth(uint16_t op)
{
    const int n = op >> 12, lo = op & 0xF, mid = (op >> 8) & 0xF;
    switch (n)
    {
    case 0x0:
        if (lo >= 0x4 && lo <= 0x6) return 1 << (lo - 4);    // mov.x Rm,@(R0,Rn)
        if (lo >= 0xC && lo <= 0xE) return 1 << (lo - 0xC);  // mov.x @(R0,Rm),Rn
        if (lo == 0xF) return 4;                              // mac.l
        return 0;
    case 0x1: return 4;                                       // mov.l Rm,@(disp,Rn)
    case 0x2:
        if (lo <= 0x2) return 1 << lo;                        // mov.x Rm,@Rn
        if (lo >= 0x4 && lo <= 0x6) return 1 << (lo - 4);    // mov.x Rm,@-Rn
        return 0;
    case 0x4:
        if ((op & 0xFF) == 0x1B) return 1;                    // tas.b
        if (lo == 0xF) return 2;                              // mac.w
        if (lo == 0x2 || lo == 0x3 || lo == 0x6 || lo == 0x7) return 4;   // sts.l/stc.l/lds.l/ldc.l
        return 0;
    case 0x5: return 4;                                       // mov.l @(disp,Rm),Rn
    case 0x6:
        if (lo <= 0x2) return 1 << lo;                        // mov.x @Rm,Rn
        if (lo >= 0x4 && lo <= 0x6) return 1 << (lo - 4);    // mov.x @Rm+,Rn
        return 0;
    case 0x8:
        if (mid == 0x0 || mid == 0x4) return 1;
        if (mid == 0x1 || mid == 0x5) return 2;
        return 0;
    case 0x9: return 2;                                       // mov.w @(disp,PC),Rn
    case 0xC:
        if (mid <= 0x2) return 1 << mid;                      // mov.x R0,@(disp,GBR)
        if (mid >= 0x4 && mid <= 0x6) return 1 << (mid - 4);  // mov.x @(disp,GBR),R0
        if (mid >= 0xC) return 1;                             // tst.b/and.b/xor.b/or.b @(R0,GBR)
        return 0;
    case 0xD: return 4;                                       // mov.l @(disp,PC),Rn
    default: return 0;
    }
}

// Real game code, when given: SE_GAME_HWRAM names a 1 MiB high work RAM image (big-endian, as the
// SH-2 sees it). Every function prologue (sts.l pr,@-r15) is decompiled; the summary says how many
// succeeded, how long they took, and what the hardware handling found. SE_GAME_OUT, if set, is a
// directory that receives each output, for comparing two builds. Not run otherwise: the image is
// a game's, and is not in the repository.
void TestGameCode()
{
    const char* path = std::getenv("SE_GAME_HWRAM");
    if (!path) return;
    const char* outDir = std::getenv("SE_GAME_OUT");
    const char* only = std::getenv("SE_GAME_ONLY");   // one entry, hex: print its output and ledger
    std::vector<uint8_t> hw(0x100000u);
    FILE* f = std::fopen(path, "rb");
    CHECK(f != nullptr);
    if (!f) return;
    const size_t got = std::fread(hw.data(), 1, hw.size(), f);
    std::fclose(f);
    CHECK(got == hw.size());
    auto snap = MakeSnapshot(hw);
    Sh2Decompiler d(gConfig);
    int n = 0, ok = 0, failed = 0, other = 0, withAccesses = 0, direct = 0, pointer = 0, twice = 0, notes = 0, cautions = 0;
    int duplicated = 0, wrongWidth = 0, unordered = 0, moved = 0, shortCircuit = 0, outOfOrder = 0, folded = 0, dropped = 0,
        noReference = 0, droppedSaid = 0;
    // The conservation law over the corpus: the engine's records by fate, and the reference run's
    // accesses by how the shown output accounts for them.
    int fateCount[4] = { 0, 0, 0, 0 };
    int accountCount[4] = { 0, 0, 0, 0 };   // Printed, Folded, Removed, Dropped
    int foldedLookup = 0, removedOrdinary = 0;
    int deadPointerReads = 0, deadHardwareReads = 0, deadConstantReads = 0, deadWrites = 0, deadLost = 0;
    const std::function<bool(uint32_t)> noneMarked = [](uint32_t) { return false; };
    double total = 0, worst = 0;
    uint32_t worstAt = 0;
    // Area 7: the pseudocode-to-assembly mapping over every function (MappingOracle.h).
    dts::MappingAudit mapping;
    const std::string mappingMutate = std::getenv("SE_GAME_MAPPING_MUTATE") ? std::getenv("SE_GAME_MAPPING_MUTATE") : "";
    const std::function<bool(uint32_t, uint16_t&)> wordAt = [&hw](uint32_t a, uint16_t& w) {
        if (a < 0x06000000u || a - 0x06000000u + 1 >= hw.size()) return false;
        w = static_cast<uint16_t>(hw[a - 0x06000000u] << 8 | hw[a - 0x06000000u + 1]);
        return true;
    };
    for (uint32_t k = 0; k + 1 < hw.size(); k += 2)
    {
        if (hw[k] != 0x4F || hw[k + 1] != 0x22) continue;
        const uint32_t entry = 0x06000000u + k;
        if (only && entry != static_cast<uint32_t>(std::strtoul(only, nullptr, 16))) continue;
        const DecompileResult r = d.Decompile(snap, Request(entry));
        if (only) Print("game function", r);
        if (only && std::getenv("SE_GAME_DISASM"))
        {
            const uint32_t from = static_cast<uint32_t>(std::strtoul(std::getenv("SE_GAME_DISASM"), nullptr, 16));
            for (const std::string& l : d.Disassemble(snap, from, 64)) std::printf("    %s\n", l.c_str());
        }
        ++n;
        if (r.status == DecompileStatus::Ok) ++ok;
        else if (r.status == DecompileStatus::Failed) ++failed;
        else ++other;
        if (r.status == DecompileStatus::Ok)
        {
            char where[32];
            std::snprintf(where, sizeof where, "function %08X", entry);
            // SE_GAME_MAPPING_MUTATE=<kind> (MappingOracle.h) damages the result as one defect would; the run must then fail.
            DecompileResult damaged = r;
            if (!mappingMutate.empty()) dts::MutateMapping(damaged, mappingMutate);
            mapping.Add(dts::AuditMapping(damaged, wordAt, where, [&](const Line& l) { return dts::MutatedOriginText(mappingMutate, l); }));
        }
        total += r.elapsedMs;
        if (r.elapsedMs > worst) { worst = r.elapsedMs; worstAt = entry; }
        if (!r.accesses.empty()) ++withAccesses;
        for (const MemoryAccess& m : r.accesses)
        {
            (m.direct ? direct : pointer) += 1;
            if (m.printed != 1 && ++twice <= 12)
            {
                std::printf("  printed %d times: %s %s %08X size %u at %08X in %08X\n", m.printed, m.write ? "write" : "read",
                            m.direct ? "direct" : "via pointer", m.address, unsigned(m.size), m.insn, entry);
                for (const Line& l : r.lines)
                    for (const Token& t : l.tokens)
                        if (t.addr == m.insn) { std::printf("      | %s\n", l.text.c_str()); break; }
            }
        }
        for (const HardwareNote& h : r.hardwareNotes)
        {
            (h.caution ? cautions : notes) += 1;
            // SE_GAME_NOTES=1 lists them, so two builds' notes can be diffed (the width table's corpus check).
            if (std::getenv("SE_GAME_NOTES"))
                std::printf("  game %s in %08X at %08X: %s\n", h.caution ? "caution" : "note", entry, h.address, h.text.c_str());
        }
        // Per instruction: an SH-2 instruction reads at most once and writes at most once on a path
        // (mac.l/mac.w read twice). Copies of an access on one path are an access the analysis
        // made twice; copies on separate paths (a block copied into each) are not.
        // Two reads in one statement: C leaves their order open.
        // A read printed later than it happens (in the statement its value ends in) must not pass
        // another access or a call on the way.
        for (const MemoryAccess& m : r.accesses)
        {
            std::string what;
            if (PrintedPast(m, r.accesses, r.calls, &what) && ++moved <= 10)
                std::printf("  read at %08X printed past %s in %08X\n", m.insn, what.c_str(), entry);
        }
        for (const MemoryAccess& m : r.accesses)
            if (m.conditionalInPrint && ++shortCircuit <= 10)
                std::printf("  read at %08X printed in the right operand of && or || in %08X\n", m.insn, entry);
        // Two accesses in one final block in an order the code cannot make them in, and two joined
        // into a switch's block across a branch (FlowReach, from the flow's own blocks).
        {
            const FlowReach fr(r.flowSuccessors);
            for (size_t i = 0; i < r.accesses.size(); ++i)
                for (size_t j = i + 1; j < r.accesses.size(); ++j)
                {
                    if (OutOfFlowOrder(fr, r.accesses[i], r.accesses[j]) && ++outOfOrder <= 10)
                        std::printf("  accesses at %08X and %08X are out of the code's order in block %d in %08X\n",
                                    r.accesses[i].insn, r.accesses[j].insn, r.accesses[i].block, entry);
                    if (FoldedIntoSwitch(fr, r.accesses[i], r.accesses[j]) && ++folded <= 10)
                        std::printf("  accesses at %08X and %08X joined into a switch's block across a branch in %08X\n",
                                    r.accesses[i].insn, r.accesses[j].insn, entry);
                }
        }
        // Every memory instruction the flow reaches is accounted for exactly once: the reference run
        // (every address volatile) lists them all, and each is printed, folded into ordinary
        // memory, the read of a switch's table, or removed with unreachable code that the output
        // says (Accounts). A dead or unrecorded one is dropped.
        for (const AccessFate& f : r.accessFates)
        {
            ++fateCount[int(f.fate)];
            // What ended dead is no access the output lost only if it is a direct access of ordinary
            // memory: the reference run cannot list a loss that both runs share, so the record is the
            // check (DeadIsLoss; before patch 0018, 31 and 34 reads through a pointer were such losses).
            // The kinds are counted for the record.
            if (DeadIsLoss(f, noneMarked)) ++deadLost;
            if (f.fate == AccessFate::Dead && !f.write)
            {
                if (!f.constantAddress) ++deadPointerReads;
                else if (InHardwareRange(f.address)) ++deadHardwareReads;
                else ++deadConstantReads;
            }
            if (f.fate == AccessFate::Dead && f.write) ++deadWrites;
            if (std::getenv("SE_GAME_FATES") && f.fate != AccessFate::FoldedOrdinary)
                std::printf("  fate %d: %s of %u bytes at %08X (%s%08X) in %08X\n", int(f.fate), f.write ? "write" : "read",
                            unsigned(f.size), f.insn, f.constantAddress ? "" : "through a pointer; ", f.address, entry);
        }
        {
            DecompileRequest ref = Request(entry);
            ref.allMemoryVolatile = true;
            const DecompileResult rr = d.Decompile(snap, ref);
            if (rr.status != DecompileStatus::Ok) ++noReference;
            else
            {
                const std::vector<Account> accounts = Accounts(r, rr.accesses, noneMarked);
                std::map<uint32_t, int> said;   // the removal cautions on an instruction, spent one per removed access
                for (size_t ai = 0; ai < rr.accesses.size(); ++ai)
                {
                    const MemoryAccess& m = rr.accesses[ai];
                    const Account a = accounts[ai];
                    ++accountCount[int(a)];
                    if (a == Account::Printed) continue;
                    if (a == Account::Folded)
                    {
                        // Counted apart: a switch's table lookup is listed by the reference only when the
                        // shown run recovered the switch and the reference did not.
                        for (const AccessFate& f : r.accessFates)
                            if (f.insn == m.insn && f.write == m.write && f.size == m.size && f.fate == AccessFate::SwitchLookup)
                            {
                                ++foldedLookup;
                                break;
                            }
                        continue;
                    }
                    if (a == Account::Removed)
                    {
                        if (OrdinaryDirect(m, noneMarked)) ++removedOrdinary;   // no loss: a variable in removed code
                        else if (++said[m.insn] <= SaidCount(r, m.insn)) ++droppedSaid;   // removed as unreachable, and the output says so
                        else if (++dropped <= 10)
                            std::printf("  the %s of %u bytes at %08X (%s%08X) is removed as unreachable in %08X, and not said\n",
                                        m.write ? "write" : "read", unsigned(m.size), m.insn, m.direct ? "" : "through a pointer; ",
                                        m.address, entry);
                        continue;
                    }
                    if (++dropped <= 10)
                        std::printf("  the %s of %u bytes at %08X (%s%08X) is dropped in %08X\n", m.write ? "write" : "read",
                                    unsigned(m.size), m.insn, m.direct ? "" : "through a pointer; ", m.address, entry);
                }
            }
        }
        for (size_t i = 0; i < r.accesses.size(); ++i)
            for (size_t j = i + 1; j < r.accesses.size(); ++j)
                if (Unordered(r.accesses[i], r.accesses[j]) && ++unordered <= 10)
                    std::printf("  two reads in one statement, neither feeding the other (%08X, %08X) in %08X\n",
                                r.accesses[i].insn, r.accesses[j].insn, entry);
        std::map<uint32_t, std::pair<int, int>> perInsn;
        for (const MemoryAccess& m : r.accesses)
            if (m.repeatedOnPath) (m.write ? perInsn[m.insn].second : perInsn[m.insn].first) += 1;
        // Every access as wide as the instruction's operand (its own, or its delay slot's: the
        // slot's p-code carries the branch's address).
        for (const MemoryAccess& m : r.accesses)
        {
            const uint32_t off = m.insn - 0x06000000u;
            if (off + 3 >= hw.size()) continue;
            const int w0 = MemoryWidth(uint16_t(hw[off] << 8 | hw[off + 1]));
            const int w1 = MemoryWidth(uint16_t(hw[off + 2] << 8 | hw[off + 3]));
            const bool fits = w0 ? m.size == w0 : (w1 && m.size == w1);
            if (!fits && (w0 || w1) && ++wrongWidth <= 10)
                std::printf("  %s of %u bytes at %08X (operand %d, slot %d) in %08X\n", m.write ? "write" : "read",
                            unsigned(m.size), m.insn, w0, w1, entry);
        }
        for (const auto& pi : perInsn)
        {
            const uint32_t off = pi.first - 0x06000000u;
            const uint16_t op = off + 1 < hw.size() ? uint16_t(hw[off] << 8 | hw[off + 1]) : 0;
            // mac.l and mac.w read twice, two accesses: repeatedOnPath no longer flags them (A5-6),
            // so they need no exemption here.
            if (pi.second.first > 0 || pi.second.second > 0)
            {
                if (++duplicated <= 10)
                    std::printf("  %d reads, %d writes at %08X (op %04X) in %08X\n", pi.second.first, pi.second.second,
                                pi.first, op, entry);
            }
        }
        if (outDir)
        {
            char name[512];
            std::snprintf(name, sizeof name, "%s/%08X.c", outDir, entry);
            if (FILE* o = std::fopen(name, "w"))
            {
                std::fprintf(o, "status %d\n", int(r.status));
                for (const std::string& w : r.warnings) std::fprintf(o, "// warning: %s\n", w.c_str());
                // Whether its signature is lent to callers, so two builds' outputs also differ where that changes.
                if (!r.prototypeWithheld.empty()) std::fprintf(o, "// signature withheld: %s\n", r.prototypeWithheld.c_str());
                else if (r.prototype.known) std::fprintf(o, "// signature lent\n");
                std::fputs(r.Text().c_str(), o);
                std::fclose(o);
            }
        }
    }
    std::printf("  game code: %d functions, %d ok, %d failed, %d other; %.0f ms total, worst %.1f ms at %08X\n", n, ok,
                failed, other, total, worst, worstAt);
    std::printf("  accesses: %d functions with any, %d direct (volatile), %d through pointers, %d not printed "
                "exactly once; notes %d, cautions %d\n", withAccesses, direct, pointer, twice, notes, cautions);
    std::printf("  %d instructions with more accesses than they make, %d accesses of another width, %d statements "
                "with two reads\n", duplicated, wrongWidth, unordered);
    std::printf("  %d reads printed past another access or a call, %d printed as conditional, %d pairs out of the code's "
                "order, %d joined into a switch across a branch\n", moved, shortCircuit, outOfOrder, folded);
    std::printf("  %d accesses dropped against the reference run, %d missing and said (code removed as unreachable); "
                "%d functions without a reference\n", dropped, droppedSaid, noReference);
    std::printf("  the engine's records: %d folded into ordinary memory, %d dead, %d unreachable, %d switch lookups\n",
                fateCount[0], fateCount[1], fateCount[2], fateCount[3]);
    std::printf("  the reference run's accesses: %d printed, %d folded (%d of them a switch lookup), %d removed (%d of ordinary "
                "memory, %d said), %d dropped\n", accountCount[int(Account::Printed)], accountCount[int(Account::Folded)],
                foldedLookup, accountCount[int(Account::Removed)], removedOrdinary, droppedSaid, accountCount[int(Account::Dropped)]);
    std::printf("  ended dead: %d reads through a pointer, %d reads of constant addresses of ordinary memory, %d of "
                "hardware, %d writes; %d of them are no direct access of ordinary memory\n", deadPointerReads, deadConstantReads,
                deadHardwareReads, deadWrites, deadLost);
    dts::PrintMappingAudit(mapping, "alone");
    CHECK(mapping.Violations() == 0);
    CHECK(dropped == 0);
    CHECK(deadLost == 0);
    CHECK(noReference == 0);
    CHECK(folded == 0);
    CHECK(outOfOrder == 0);
    CHECK(shortCircuit == 0);
    CHECK(unordered == 0);
    CHECK(moved == 0);
    CHECK(wrongWidth == 0);
    CHECK(twice == 0);
    CHECK(duplicated == 0);
    CHECK(failed == 0);
}

// The corpus as the panel decompiles it, with lending. TestGameCode decompiles each function alone,
// so it lends nothing and cannot show what a lent signature does to a caller. The panel lends every
// signature it has learned (DecompilerPanel::Learn, Lendable) to the next function it decompiles; this
// decompiles every function after the functions it calls (the order of a first run's calledAt, a cycle
// broken where it is entered) and does the same, twice: without the effects each signature proved
// (what lending was before patch 0028) and with them, then compares. SE_GAME_LEND=1 with
// SE_GAME_HWRAM; SE_GAME_OUT also receives lend_before/ and lend_after/ with each function's text.
void TestGameLending()
{
    const char* path = std::getenv("SE_GAME_HWRAM");
    if (!path || !std::getenv("SE_GAME_LEND")) return;
    const char* outDir = std::getenv("SE_GAME_OUT");
    std::vector<uint8_t> hw(0x100000u);
    FILE* f = std::fopen(path, "rb");
    CHECK(f != nullptr);
    if (!f) return;
    const size_t got = std::fread(hw.data(), 1, hw.size(), f);
    std::fclose(f);
    CHECK(got == hw.size());
    auto snap = MakeSnapshot(hw);
    Sh2Decompiler d(gConfig);

    // The functions: every prologue (sts.l pr,@-r15) as TestGameCode has them, and every function in
    // high work RAM they call, transitively -- the leaves that never save PR, the ones that keep r4-r7.
    std::vector<uint32_t> entries;
    for (uint32_t k = 0; k + 1 < hw.size(); k += 2)
        if (hw[k] == 0x4F && hw[k + 1] == 0x22) entries.push_back(0x06000000u + k);
    const std::vector<uint32_t> prologues = entries;
    std::map<uint32_t, size_t> index;
    for (size_t i = 0; i < entries.size(); ++i) index[entries[i]] = i;
    auto indexOf = [&](uint32_t a) {
        if ((a & 0xDFF00000u) != 0x06000000u) return SIZE_MAX;   // high work RAM, cached or not
        auto it = index.find(0x06000000u | (a & 0x000FFFFFu));
        return it == index.end() ? SIZE_MAX : it->second;
    };

    // The first pass: each alone, for who calls whom.
    std::vector<std::vector<uint32_t>> calls;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const DecompileResult r = d.Decompile(snap, Request(entries[i]));
        calls.push_back(r.calledAt);
        for (uint32_t c : r.calledAt)
            if (indexOf(c) == SIZE_MAX && (c & 0xDFF00000u) == 0x06000000u)
            {
                const uint32_t canonical = 0x06000000u | (c & 0x000FFFFFu);
                index[canonical] = entries.size();
                entries.push_back(canonical);
            }
    }
    // Callees first: a post-order walk.
    std::vector<size_t> order;
    {
        std::vector<char> state(entries.size(), 0);
        for (size_t root = 0; root < entries.size(); ++root)
        {
            if (state[root]) continue;
            std::vector<std::pair<size_t, size_t>> stack = { { root, 0 } };
            state[root] = 1;
            while (!stack.empty())
            {
                const size_t at = stack.back().first;
                if (stack.back().second < calls[at].size())
                {
                    const size_t c = indexOf(calls[at][stack.back().second++]);
                    if (c != SIZE_MAX && !state[c])
                    {
                        state[c] = 1;
                        stack.push_back({ c, 0 });
                    }
                    continue;
                }
                state[at] = 2;
                order.push_back(at);
                stack.pop_back();
            }
        }
    }
    CHECK(order.size() == entries.size());

    struct Row
    {
        bool ok = false, failed = false, known = false;
        std::string withheld, text;
        size_t params = 0;
        uint32_t preserved = 0;
        int lentCallees = 0;         // callees whose signature it was lent
        int keepingCallees = 0;      // ...of which keep some of r4-r7
        bool extraout = false;       // the text has a call's r4-r7 as extraout_rN
        int changingCallees = 0;     // ...of which change r8-r14, GBR or VBR, which the model keeps (A3-3)
        bool extraoutKept = false;   // the text has a call's r8-r14, GBR or VBR as extraout_
    };
    constexpr uint32_t kModelKept = (0x7Fu << 8) | (1u << 17) | (1u << 18);   // r8-r14, GBR, VBR
    // Area 7: the mapping of the texts the panel shows, with lent signatures (MappingOracle.h).
    dts::MappingAudit mappingLent;
    const std::function<bool(uint32_t, uint16_t&)> wordAt = [&hw](uint32_t a, uint16_t& w) {
        if (a < 0x06000000u || a - 0x06000000u + 1 >= hw.size()) return false;
        w = static_cast<uint16_t>(hw[a - 0x06000000u] << 8 | hw[a - 0x06000000u + 1]);
        return true;
    };
    auto pass = [&](bool lendEffects, const char* label) {
        Sh2Decompiler::SetLendEffectsForTests(lendEffects);
        std::vector<Row> rows(entries.size());
        std::map<uint32_t, Prototype> learned;
        double ms = 0;
        if (outDir)
        {
            char dir[512];
            std::snprintf(dir, sizeof dir, "%s/lend_%s", outDir, label);
            sfe::MakeDirectory(dir);
        }
        for (size_t i : order)
        {
            DecompileRequest rq = Request(entries[i]);
            // Keyed by the address a call runs at: a signature learned at the cached address is
            // lent to a call through the uncached one too (the panel learns what it was asked for).
            for (const auto& kv : learned)
            {
                rq.prototypes.emplace_back(kv);
                rq.prototypes.emplace_back(kv.first | 0x20000000u, kv.second);
            }
            const DecompileResult r = d.Decompile(snap, rq);
            ms += r.elapsedMs;
            if (lendEffects && r.status == DecompileStatus::Ok)
            {
                char where[32];
                std::snprintf(where, sizeof where, "lent function %08X", entries[i]);
                mappingLent.Add(dts::AuditMapping(r, wordAt, where));
            }
            Row& row = rows[i];
            row.ok = r.status == DecompileStatus::Ok;
            row.failed = r.status == DecompileStatus::Failed;
            row.known = row.ok && r.prototype.known;
            row.withheld = r.prototypeWithheld;
            row.params = r.prototype.params.size();
            row.preserved = r.prototype.effects.preserved;
            row.text = r.Text();
            for (uint32_t c : r.calledAt)
            {
                auto l = learned.find(c & ~0x20000000u);
                if (l == learned.end()) continue;
                ++row.lentCallees;
                if (l->second.effects.preserved & 0xF0u) ++row.keepingCallees;
                if ((l->second.effects.preserved & kModelKept) != kModelKept) ++row.changingCallees;
            }
            for (const char* name : { "r8", "r9", "r10", "r11", "r12", "r13", "r14", "gbr", "vbr" })
                if (row.text.find(std::string("extraout_") + name) != std::string::npos) row.extraoutKept = true;
            for (int reg = 4; reg < 8; ++reg)
                if (row.text.find("extraout_r" + std::to_string(reg)) != std::string::npos) row.extraout = true;
            if (row.known) learned[entries[i]] = r.prototype;
            if (outDir)
            {
                char name[512];
                std::snprintf(name, sizeof name, "%s/lend_%s/%08X.c", outDir, label, entries[i]);
                if (FILE* o = std::fopen(name, "w"))
                {
                    if (!row.withheld.empty()) std::fprintf(o, "// signature withheld: %s\n", row.withheld.c_str());
                    else if (row.known) std::fprintf(o, "// signature lent\n");
                    std::fputs(row.text.c_str(), o);
                    std::fclose(o);
                }
            }
        }
        std::printf("  [%s] %.0f ms in the decompiler\n", label, ms);
        return rows;
    };
    const std::vector<Row> before = pass(false, "before");
    const std::vector<Row> after = pass(true, "after");
    Sh2Decompiler::SetLendEffectsForTests(true);
    dts::PrintMappingAudit(mappingLent, "lent");
    CHECK(mappingLent.Violations() == 0);

    auto summarize = [&](const std::vector<Row>& rows, const char* label) {
        int ok = 0, known = 0, afterCall = 0, withLent = 0, withKeeping = 0, keepingKnown = 0, keepingExtraout = 0, extraout = 0;
        int keepers = 0;
        size_t params = 0;
        for (const Row& r : rows)
        {
            ok += r.ok;
            known += r.known;
            params += r.params;
            keepers += r.known && (r.preserved & 0xF0u) != 0;
            afterCall += r.withheld.find("after calling") != std::string::npos;
            withLent += r.lentCallees > 0;
            extraout += r.extraout;
            if (r.keepingCallees > 0)
            {
                ++withKeeping;
                keepingKnown += r.known;
                keepingExtraout += r.extraout;
            }
        }
        std::printf("  [%s] %zu functions, %d ok; signatures lent (known) %d (%zu parameters in all), %d of them keep some of r4-r7;\n"
                    "         withheld \"uses a register after calling\" %d; %d were lent a callee's signature, %d of them one that keeps "
                    "r4-r7 (%d of those lent, %d with extraout_r4-7 in the text); %d functions print extraout_r4-7\n",
                    label, rows.size(), ok, known, params, keepers, afterCall, withLent, withKeeping, keepingKnown, keepingExtraout,
                    extraout);
    };
    summarize(before, "before");
    summarize(after, "after");
    // A3-3: the registers the model keeps across a call, changed by a lent function. What each caller of one
    // does with it: reads it as the callee's value (extraout_), or is withheld, never lent on the old value.
    {
        int changers = 0, changersChangingSp = 0, callers = 0, callersLent = 0, callersExtraout = 0, callersExtraoutLent = 0;
        for (const Row& r : after)
        {
            changers += r.known && (r.preserved & kModelKept) != kModelKept;
            changersChangingSp += r.known && !((r.preserved >> 15) & 1u);
            if (r.changingCallees == 0) continue;
            ++callers;
            callersLent += r.known;
            callersExtraout += r.extraoutKept;
            callersExtraoutLent += r.extraoutKept && r.known;
        }
        std::printf("  [after] lent functions that change r8-r14, GBR or VBR: %d (that move r15: %d); %d functions were lent one: %d lent, "
                    "%d print their register as extraout_ (%d of those lent)\n",
                    changers, changersChangingSp, callers, callersLent, callersExtraout, callersExtraoutLent);
        CHECK(callersExtraoutLent == 0);   // a lent caller never reads such a register after the call: it is withheld on it
    }
    // Why the signatures that are not lent are not, in the run with the effects lent: the reason with
    // the callee's address taken out, so the same reason of two functions counts once.
    {
        std::map<std::string, int> reasons;
        for (const Row& r : after)
        {
            if (!r.ok || r.known) continue;
            std::string why = r.withheld;
            for (size_t at = why.find("0x"); at != std::string::npos; at = why.find("0x", at + 1))
                if (at + 10 <= why.size()) why.replace(at, 10, "0x........");
            ++reasons[why];
        }
        for (const auto& kv : reasons) std::printf("    withheld %5d  %s\n", kv.second, kv.first.c_str());
    }
    int textDiffers = 0, gainedSignature = 0, lostSignature = 0, moreParams = 0, fewerParams = 0, lessExtraout = 0, moreExtraout = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const Row& b = before[i];
        const Row& a = after[i];
        textDiffers += b.text != a.text;
        gainedSignature += !b.known && a.known;
        lostSignature += b.known && !a.known;
        moreParams += a.params > b.params;
        fewerParams += a.params < b.params;
        lessExtraout += b.extraout && !a.extraout;
        moreExtraout += !b.extraout && a.extraout;
        if (std::getenv("SE_GAME_LEND_LIST") && b.text != a.text)
            std::printf("    %08X: known %d -> %d, params %zu -> %zu, extraout %d -> %d\n", entries[i], b.known, a.known, b.params,
                        a.params, b.extraout, a.extraout);
    }
    std::printf("  lending the proved effects changes the text of %d functions: %d gain a lent signature, %d lose one; %d gain "
                "parameters, %d lose some; extraout_r4-7 gone from %d, new in %d\n",
                textDiffers, gainedSignature, lostSignature, moreParams, fewerParams, lessExtraout, moreExtraout);
    // A callee found by a call can be an address that holds no function (a computed target the first
    // pass took for constant): the engine reports that as a failed result, in both runs alike. The
    // prologue functions TestGameCode decompiles never fail.
    int notFunctions = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        CHECK(before[i].failed == after[i].failed && before[i].ok == after[i].ok);
        if (before[i].failed)
        {
            ++notFunctions;
            std::printf("    %08X: not a function the engine can decompile\n", entries[i]);
            CHECK(std::find(prologues.begin(), prologues.end(), entries[i]) == prologues.end());
        }
    }
    std::printf("  %d of the called addresses are no decompilable function\n", notFunctions);
}

// A branch out of the captured region: the function is marked truncated where it left, naming
// the area, instead of the whole decompilation failing on the first unreadable byte.
void TestTruncatedFlow()
{
    Sh2Decompiler d(gConfig);
    // At the end of LWRAM, a bt to just past it: LWRAM is not assumed to repeat (A2), so the
    // target is not captured memory.
    std::vector<uint8_t> none;
    auto snap = MakeSnapshot(none);
    auto* lw = const_cast<MemoryRegion*>(snap->RegionAt(MemorySnapshot::kLwramBase));
    const uint32_t f = 0x002FFFF0u;
    size_t off = f - lw->base;
    for (uint16_t w : { 0x4415,     // cmp/pl r4
                        0x8907,     // bt 0x00300004 (0x002FFFF2 + 4 + 7*2), past LWRAM's end
                        0xE000,     // mov #0,r0
                        0x000B,     // rts
                        0x0009 })
    { lw->bytes[off] = uint8_t(w >> 8); lw->bytes[off + 1] = uint8_t(w); off += 2; }
    const DecompileResult r = d.Decompile(snap, Request(f));
    Print("truncated", r);
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(r.truncated);
    CHECK(r.truncatedAt == 0x00300004u);
    CHECK(Has(r.truncatedWhy, "0x00300004"));
}

// A cancel already requested stops at the first check-point.
void TestCancelledBeforeStart()
{
    Sh2Decompiler d(gConfig);
    std::atomic<bool> cancel(true);
    const DecompileResult r = d.Decompile(Fixture1Snapshot(), Request(dts::kFixture1Caller), &cancel);
    CHECK(r.status == DecompileStatus::Cancelled);
    CHECK(r.lines.empty());
    cancel = false;
    CHECK(d.Decompile(Fixture1Snapshot(), Request(dts::kFixture1Caller), &cancel).status == DecompileStatus::Ok);
}

// Five branches into uncaptured memory: each is a halt in the flow, and the function is still
// decompiled and reported truncated -- Ghidra's default bad-data allowance of four would fail it.
void TestManyUncapturedBranches()
{
    Sh2Decompiler d(gConfig);
    std::vector<uint8_t> none;
    auto snap = MakeSnapshot(none);
    auto* lw = const_cast<MemoryRegion*>(snap->RegionAt(MemorySnapshot::kLwramBase));
    const uint32_t f = 0x002FFFE0u;
    uint32_t a = f;
    auto put = [&](uint16_t w) { lw->bytes[a - lw->base] = uint8_t(w >> 8); lw->bytes[a - lw->base + 1] = uint8_t(w); a += 2; };
    for (int i = 0; i < 5; ++i)
    {
        put(0x4415);                                                    // cmp/pl r4
        const uint32_t target = 0x00300000u + 4u * uint32_t(i);         // past LWRAM, distinct
        put(uint16_t(0x8900 | ((target - (a + 4)) / 2)));               // bt target
    }
    put(0xE000);   // mov #0,r0
    put(0x000B);   // rts
    put(0x0009);
    const DecompileResult r = d.Decompile(snap, Request(f));
    Print("five uncaptured branches", r);
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(r.truncated);
    // Every cut is reported, the lowest first, whatever order flow found them in.
    CHECK(r.truncatedAt == 0x00300000u);
    CHECK(r.truncatedCount == 5);
    int cuts = 0;
    for (const FlowWarning& w : r.flowWarnings) cuts += w.kind == FlowWarning::Cut;
    CHECK(cuts == 5);
    CHECK(!r.lines.empty());
}


// Callees: a direct bsr is harvested (its target lives in the call's spec, not the CALL's
// address input), a callee already known is still listed, and a call through a mirror of the
// callee is named and navigates to the canonical entry.
void TestCalleeHarvest()
{
    Sh2Decompiler d(gConfig);
    // Fixture 1 with the jsr replaced by bsr 0x06005000 (0x0600400E + 4 + 2 * 0x7F8).
    std::vector<uint8_t> hw;
    dts::PlaceFixture1(hw);
    dts::Put16(hw, dts::kFixture1Jsr - 0x06000000u, uint16_t(0xB000 | ((dts::kFixture1Callee - (dts::kFixture1Jsr + 4)) / 2)));
    auto direct = MakeSnapshot(hw, 4);
    const DecompileResult r = d.Decompile(direct, Request(dts::kFixture1Caller));
    Print("bsr", r);
    CHECK(Has(r.Text(), "func_0x06005000("));
    CHECK(r.callees == std::vector<uint32_t>{ dts::kFixture1Callee });

    DecompileRequest known = Request(dts::kFixture1Caller);
    known.knownFunctions.push_back(dts::kFixture1Callee);
    const DecompileResult rk = d.Decompile(Fixture1Snapshot(), known);
    CHECK(rk.callees == std::vector<uint32_t>{ dts::kFixture1Callee });

    for (uint32_t mirror : { 0x26005000u, 0x06105000u })
    {
        const DecompileResult rm = d.Decompile(Fixture1Snapshot(mirror, 5), Request(dts::kFixture1Caller));
        Print("call through a mirror", rm);
        CHECK(Has(rm.Text(), "func_0x06005000(param_1)"));
        CHECK(!Has(rm.Text(), "(*(code *)"));
        CHECK(rm.callees == std::vector<uint32_t>{ dts::kFixture1Callee });
        bool target = false;
        for (const Line& l : rm.lines)
            for (const Token& t : l.tokens)
                if (t.kind == TokenKind::FuncName && t.target == dts::kFixture1Callee) target = true;
        CHECK(target);
    }
}

// Step 4: with a BIOS image loaded, a call into it is a named callee, bios_0x..., whichever
// mirror the literal uses; and a BIOS function decompiled directly carries the same name. Without
// the image the call stays opaque (TestFixture1b).
void TestBiosFunctionNames()
{
    Sh2Decompiler d(gConfig);
    for (uint32_t literal : { 0x00000600u, 0x20000600u })
    {
        auto s = Fixture1Snapshot(literal, 6);
        std::vector<uint8_t> bios(0x80000, 0);
        dts::Put16(bios, 0x600, 0x000B);   // rts
        dts::Put16(bios, 0x602, 0xE007);   //   mov #7,r0
        auto image = BiosImage::Make(bios);
        MemoryRegion region;
        region.name = "BIOS";
        region.base = MemorySnapshot::kBiosBase;
        region.bytes = image->bytes;
        s->regions.push_back(region);
        s->biosHash = image->contentHash;
        s->key.regionSetHash = s->ComputeRegionSetHash();

        const DecompileResult r = d.Decompile(s, Request(dts::kFixture1Caller));
        Print("call into the BIOS", r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(Has(r.Text(), "bios_0x00000600("));
        CHECK(!Has(r.Text(), "func_0x00000600") && !Has(r.Text(), "(*(code *)"));
        CHECK(r.callees == std::vector<uint32_t>{ 0x600u });

        const DecompileResult b = d.Decompile(s, Request(literal));
        CHECK(b.status == DecompileStatus::Ok);
        CHECK(b.name == "bios_0x00000600");

        // A user's name still wins.
        DecompileRequest named = Request(dts::kFixture1Caller);
        named.names.emplace_back(0x600u, "SYS_Reset");
        CHECK(Has(d.Decompile(s, named).Text(), "SYS_Reset("));
    }
}

// Step 4: a callee's signature, recovered when it was decompiled, is locked into the caller's
// request. The caller sets up r4-r6 but the callee reads only r4 and returns nothing: guessing
// from the call site shows more than it takes, the lent prototype shows exactly what it takes.
// Encodings checked with Capstone (SH2, BE).
void TestCalleePrototypes()
{
    Sh2Decompiler d(gConfig);
    const uint32_t caller = 0x06007300u, callee = 0x06007340u;
    std::vector<uint8_t> hw;
    size_t off = caller - 0x06000000u;
    for (uint16_t w : { 0xE401,     // mov #1,r4
                        0xE502,     // mov #2,r5
                        0xE603,     // mov #3,r6
                        0x4F22,     // sts.l pr,@-r15
                        0xB01A,     // bsr 0x06007340
                        0x0009,     //   nop
                        0x4F26,     // lds.l @r15+,pr
                        0x000B,     // rts
                        0x0009 })   //   nop
    { dts::Put16(hw, off, w); off += 2; }
    off = callee - 0x06000000u;
    for (uint16_t w : { 0xD101,     // mov.l @(4,pc),r1  -> 0x06008000
                        0x2142,     // mov.l r4,@r1
                        0x000B,     // rts
                        0x0009 })   //   nop
    { dts::Put16(hw, off, w); off += 2; }
    dts::Put32(hw, 0x06007348u - 0x06000000u, 0x06008000u);
    auto snap = MakeSnapshot(hw);

    const DecompileResult guessed = d.Decompile(snap, Request(caller));
    Print("caller, callee unknown", guessed);
    const DecompileResult c = d.Decompile(snap, Request(callee));
    Print("callee", c);
    CHECK(c.prototype.known);
    CHECK(c.prototype.ret.kind == TypeKind::Void);
    CHECK(c.prototype.params.size() == 1);

    DecompileRequest lent = Request(caller);
    lent.prototypes.emplace_back(callee, c.prototype);
    const DecompileResult r = d.Decompile(snap, lent);
    Print("caller, callee's prototype lent", r);
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(Has(r.Text(), "func_0x06007340(1);"));
    CHECK(r.Text() != guessed.Text());
    // Review of Step 4: the same call through a literal, at the canonical address and through the
    // cache-through and repeated-HWRAM mirrors. The engine registers each mirror as its own function
    // object, and each must carry the lent signature: same arguments, no return value.
    const uint32_t viaLiteral = 0x06007380u;
    for (uint32_t literal : { callee, callee | 0x20000000u, callee + 0x00100000u })
    {
        std::vector<uint8_t> hw2 = hw;
        size_t o = viaLiteral - 0x06000000u;
        for (uint16_t w : { 0xE401,     // mov #1,r4
                            0xE502,     // mov #2,r5
                            0xE603,     // mov #3,r6
                            0x4F22,     // sts.l pr,@-r15
                            0xD002,     // mov.l @(8,pc),r0  -> the literal at 0x06007394
                            0x400B,     // jsr @r0
                            0x0009,     //   nop
                            0x4F26,     // lds.l @r15+,pr
                            0x000B,     // rts
                            0x0009 })   //   nop
        { dts::Put16(hw2, o, w); o += 2; }
        dts::Put32(hw2, 0x06007394u - 0x06000000u, literal);
        // Lent under the address called: that mirror's function object carries it.
        DecompileRequest lentTo = Request(viaLiteral);
        lentTo.prototypes.emplace_back(literal, c.prototype);
        const DecompileResult m = d.Decompile(MakeSnapshot(hw2, 7), lentTo);
        Print("call through a literal, prototype lent", m);
        CHECK(Has(m.Text(), "func_0x06007340(1);"));
        CHECK(!Has(m.Text(), "(1,2,3)"));
        CHECK(std::find(m.calledAt.begin(), m.calledAt.end(), literal) != m.calledAt.end());
        if (literal == callee) continue;
        // Review of 37a0535: a signature learned at another mirror is not this call's. The same
        // bytes run elsewhere can take other arguments (a mova-derived pointer).
        DecompileRequest other = Request(viaLiteral);
        other.prototypes.emplace_back(callee, c.prototype);
        const DecompileResult n = d.Decompile(MakeSnapshot(hw2, 7), other);
        CHECK(Has(n.Text(), "(1,2,3)"));
    }

    // The function being decompiled keeps its own recovery even when a prototype is given for it.
    DecompileRequest self = Request(callee);
    Prototype wrong;
    wrong.known = true;
    wrong.ret.kind = TypeKind::Int;
    wrong.ret.size = 4;
    self.prototypes.emplace_back(callee, wrong);
    CHECK(d.Decompile(snap, self).Text() == c.Text());
}

// Review of 8639b80: a signature is lent only when the analysis that recovered it is complete.
// Each fixture below is a function whose recovered signature is plausible but wrong, so it must
// not be known (lendable); the controls are ones that are right.
uint16_t Bsr(uint32_t from, uint32_t to) { return uint16_t(0xB000 | (((to - (from + 4)) / 2) & 0x0FFF)); }

void Place(std::vector<uint8_t>& hw, uint32_t at, std::initializer_list<uint16_t> words)
{
    size_t off = at - 0x06000000u;
    for (uint16_t w : words) { dts::Put16(hw, off, w); off += 2; }
}

// Control-flow uncertainty is reported, never left to look like a complete function: bad data in
// captured memory, an indirect jump the engine cannot resolve, flow into another function's entry
// (a tail branch, a fall-through), each cut and each bad word attributed to its own address. The
// body is what flow reached, not one range from the entry.
void TestFlowUncertainty()
{
    Sh2Decompiler d(gConfig);
    std::vector<uint8_t> hw;
    auto any = [](const DecompileResult& r, FlowWarning::Kind kind, uint32_t at, const char* text) {
        for (const FlowWarning& w : r.flowWarnings)
            if (w.kind == kind && w.address == at && Has(w.text, text)) return true;
        return false;
    };
    auto warned = [](const DecompileResult& r, const char* text) {
        for (const std::string& w : r.warnings)
            if (Has(w, text)) return true;
        return false;
    };
    // Flow into a word in captured memory that does not decode (embedded data).
    const uint32_t bad = 0x06020000u;
    Place(hw, bad, { 0x4415,     // cmp/pl r4
                     0x8902,     // bt bad+0x0A
                     0xE000,     // mov #0,r0
                     0x000B,     // rts
                     0x0009,     //   nop
                     0xFFFF });  // not an instruction
    // jmp @r4: no table, so the engine treats it as a call and the function as ending there.
    const uint32_t jump = 0x06020080u;
    Place(hw, jump, { 0x442B, 0x0009 });
    // A tail branch into a named function, and a fall-through into a known entry.
    const uint32_t tail = 0x06020100u, helper = 0x06020200u, falls = 0x06020300u, next = 0x06020302u;
    Place(hw, tail, { 0xE401,     // mov #1,r4
                      0xA07D,     // bra helper
                      0x0009 });  //   nop
    Place(hw, helper, { 0x000B, 0x6043 });               // rts ; mov r4,r0
    Place(hw, falls, { 0xE402, 0x000B, 0x6043 });         // mov #2,r4 ; next: rts ; mov r4,r0
    // A shared epilogue below the entry.
    const uint32_t epilogue = 0x06020400u, shared = 0x06020410u;
    Place(hw, epilogue, { 0x4F26, 0x000B, 0x0009 });     // lds.l @r15+,pr ; rts ; nop
    Place(hw, shared, { 0x4F22,                          // sts.l pr,@-r15
                        0xAFF5,                          // bra epilogue
                        0x0009 });                       //   nop
    // jmp @r1 with r1 = PR: a return, as rts is.
    const uint32_t viaPr = 0x06020500u;
    Place(hw, viaPr, { 0x012A,     // sts pr,r1
                       0x412B,     // jmp @r1
                       0x6043 });  //   mov r4,r0
    auto snap = MakeSnapshot(hw);

    DecompileResult r = d.Decompile(snap, Request(bad));
    Print("bad data", r);
    CHECK(r.status == DecompileStatus::Ok && !r.truncated);
    CHECK(r.badDataAt == std::vector<uint32_t>{ bad + 0x0A });
    CHECK(any(r, FlowWarning::BadData, bad + 0x0A, "do not decode"));
    CHECK(warned(r, "do not decode"));
    CHECK(!r.prototype.known);

    r = d.Decompile(snap, Request(jump));
    Print("unresolved jump", r);
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(any(r, FlowWarning::Engine, jump, "indirect jump"));
    CHECK(warned(r, "indirect jump"));

    DecompileRequest q = Request(tail);
    q.names.emplace_back(helper, "helper");
    r = d.Decompile(snap, q);
    Print("tail branch", r);
    CHECK(any(r, FlowWarning::IntoFunction, helper, "helper"));
    CHECK(warned(r, "another function's entry"));
    q = Request(falls);
    q.knownFunctions.push_back(next);
    r = d.Decompile(snap, q);
    Print("fall-through", r);
    CHECK(any(r, FlowWarning::IntoFunction, next, "another function's entry"));
    // A4-8: an entry the request knows only because it lends a signature there (the panel lends every function the
    // user decompiled, at the address the user chose, and lists the finder's bounds apart from that).
    {
        Prototype lent;
        lent.known = true;
        q = Request(falls);
        q.prototypes.emplace_back(next, lent);
        r = d.Decompile(snap, q);
        Print("fall-through into a lent function", r);
        CHECK(any(r, FlowWarning::IntoFunction, next, "another function's entry"));
        q = Request(falls);
        q.prototypes.emplace_back(0x26000000u | (next & 0x000FFFFFu), lent);   // lent at a mirror of it
        CHECK(any(d.Decompile(snap, q), FlowWarning::IntoFunction, next, "another function's entry"));
        Prototype notLent;   // known = false lends nothing, so it is no entry either
        q = Request(falls);
        q.prototypes.emplace_back(next, notLent);
        CHECK(d.Decompile(snap, q).flowWarnings.empty());
        q = Request(falls);
        q.prototypes.emplace_back(helper, lent);   // an entry the flow does not reach
        CHECK(d.Decompile(snap, q).flowWarnings.empty());
    }
    r = d.Decompile(snap, Request(helper));   // its own entry is not "another"
    CHECK(r.flowWarnings.empty() && r.warnings.empty());

    r = d.Decompile(snap, Request(shared));
    Print("shared epilogue", r);
    CHECK(r.status == DecompileStatus::Ok);
    using Ranges = std::vector<std::pair<uint32_t, uint32_t>>;
    CHECK(r.body == (Ranges{ { epilogue, epilogue + 6 }, { shared, shared + 6 } }));
    CHECK(r.InBody(epilogue + 2) && r.InBody(shared + 4));
    CHECK(!r.InBody(epilogue + 6) && !r.InBody(shared - 2) && !r.InBody(shared + 6));
    r = d.Decompile(snap, Request(0x26020410u));   // run through a mirror: still canonical
    CHECK(r.body == (Ranges{ { epilogue, epilogue + 6 }, { shared, shared + 6 } }));

    // Past the instruction limit the engine fails the function (its default, error_toomanyinstructions),
    // so partial flow never shows as complete nor lends a signature.
    DecompileRequest few = Request(falls);
    few.maxInstructions = 1;
    r = d.Decompile(snap, Request(falls));
    CHECK(r.prototype.known);
    r = d.Decompile(snap, few);
    Print("instruction limit", r);
    CHECK(r.status == DecompileStatus::Failed && Has(r.error, "maximum allowable instructions") && !r.prototype.known);

    r = d.Decompile(snap, Request(viaPr));
    Print("jmp through PR", r);
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(r.prototype.known && r.prototypeWithheld.empty());

    // A bounded three-case switch (review of 7eaa935, finding 1). Recovered whole, it takes the
    // selector and r5 (case 2 returns it). With case 1's destination zeroed, the engine truncates
    // the table before cases 1 and 2: said, and the one-argument signature is not lent.
    auto switchAt = [&](uint32_t case1) {
        std::vector<uint8_t> sw;
        const uint32_t base = 0x06050000u;
        Place(sw, base, { 0xE102,      // mov #2,r1
                          0x3416,      // cmp/hi r1,r4
                          0x8904,      // bt default
                          0xC703,      // mova table,r0
                          0x4408,      // shll2 r4
                          0x014E,      // mov.l @(r0,r4),r1
                          0x412B,      // jmp @r1
                          0x0009,      //   nop
                          0x000B,      // default: rts
                          0xE0FF });   //   mov #-1,r0
        dts::Put32(sw, 0x00050014u, 0x06050020u);
        dts::Put32(sw, 0x00050018u, case1);
        dts::Put32(sw, 0x0005001Cu, 0x06050028u);
        Place(sw, 0x06050020u, { 0x000B, 0xE000, 0x000B, 0xE001, 0x000B, 0x6053 });
        return MakeSnapshot(sw);
    };
    r = d.Decompile(switchAt(0x06050024u), Request(0x06050000u));
    Print("switch", r);
    CHECK(r.status == DecompileStatus::Ok && r.flowWarnings.empty());
    CHECK(r.prototype.known && r.prototype.params.size() == 2);
    r = d.Decompile(switchAt(0), Request(0x06050000u));
    Print("switch, truncated table", r);
    CHECK(any(r, FlowWarning::JumpTable, 0x06050000u + 0x0C, "truncation of jumptable"));
    CHECK(!r.prototype.known && Has(r.prototypeWithheld, "jump table"));
    CHECK(warned(r, "Exceeded maximum restarts"));   // and withheld for it too, were the table whole

    // A jump the engine takes for a thunk (finding 2): jmp to a constant, into the BIOS that is not
    // captured. It used to print a call and a return with no warning at all.
    std::vector<uint8_t> th;
    Place(th, 0x06050000u, { 0xE100, 0x412B, 0x0009 });   // mov #0,r1 ; jmp @r1 ; nop
    r = d.Decompile(MakeSnapshot(th), Request(0x06050000u));
    Print("thunk", r);
    CHECK(any(r, FlowWarning::TailJump, 0x06050002u, "tail call to 0x00000000 (uncaptured"));
    CHECK(warned(r, "tail call"));
    // A jmp through a literal to code within the same function: the engine makes it a goto, which
    // is not a tail call (own review of 0c3d989).
    std::vector<uint8_t> local;
    Place(local, 0x06050100u, { 0x2542,      // mov.l r4,@r5  (a side effect: the jump is no thunk)
                                0xD101,      // mov.l @(4,pc),r1  (the literal at +8)
                                0x412B,      // jmp @r1
                                0x0009 });   //   nop
    dts::Put32(local, 0x00050108u, 0x0605010Cu);
    Place(local, 0x0605010Cu, { 0x000B, 0x6043 });   // rts ; mov r4,r0
    r = d.Decompile(MakeSnapshot(local), Request(0x06050100u));
    Print("jmp within the function", r);
    CHECK(Has(r.Text(), "return param_1;") && !warned(r, "tail call") && r.InBody(0x0605010Eu));
    CHECK(r.callees.empty() && r.flowWarnings.empty());   // the first pass took it for a call
    // ...and the goto the table became is followed: the signature is lent (review of 54bd571).
    CHECK(r.prototype.known && r.prototypeWithheld.empty());

    // Every case and the default reach one destination: the engine makes it a goto, and the
    // signature is still lent.
    std::vector<uint8_t> same;
    Place(same, 0x06050000u, { 0xE102, 0x3416, 0x8904, 0xC703, 0x4408, 0x014E, 0x412B, 0x0009,
                               0xA006,      // default: bra 0x06050020
                               0x0009 });   //   nop
    for (uint32_t i = 0; i < 3; ++i) dts::Put32(same, 0x00050014u + 4 * i, 0x06050020u);
    Place(same, 0x06050020u, { 0x000B, 0xE000 });
    r = d.Decompile(MakeSnapshot(same), Request(0x06050000u));
    Print("switch, one destination", r);
    CHECK(Has(r.Text(), "return 0;") && !warned(r, "tail call") && !warned(r, "cases may be missing"));
    CHECK(r.prototype.known && r.prototypeWithheld.empty());

    // A switch whose bound is a literal-pool word: recovered, since the second pass marks the first
    // pass's literals read-only before flow (jump tables are recovered during flow).
    std::vector<uint8_t> lb;
    Place(lb, 0x06050200u, { 0x9108,      // mov.w @(lit,pc),r1  (2)
                             0x3416,      // cmp/hi r1,r4
                             0x8904,      // bt default
                             0xC704,      // mova table,r0
                             0x4408,      // shll2 r4
                             0x014E,      // mov.l @(r0,r4),r1
                             0x412B,      // jmp @r1
                             0x0009,      //   nop
                             0x000B,      // default: rts
                             0xE0FF,      //   mov #-1,r0
                             0x0002,      // lit
                             0x0009 });
    dts::Put32(lb, 0x00050218u, 0x06050224u);
    dts::Put32(lb, 0x0005021Cu, 0x06050228u);
    dts::Put32(lb, 0x00050220u, 0x0605022Cu);
    Place(lb, 0x06050224u, { 0x000B, 0xE000, 0x000B, 0xE001, 0x000B, 0x6053 });
    r = d.Decompile(MakeSnapshot(lb), Request(0x06050200u));
    Print("switch, literal bound", r);
    CHECK(r.flowWarnings.empty() && r.prototype.known && r.prototype.params.size() == 2);
    // The same table marked volatile: another processor may rewrite it, so its captured
    // destinations are no fixed switch (review of 80620fb). Recovery reads it through the data
    // image, which refuses volatile memory: the jump stays unresolved, said, and nothing is lent.
    {
        DecompileRequest q = Request(0x06050200u);
        q.volatileMemory.emplace_back(0x06050218u, 0x06050223u);
        r = d.Decompile(MakeSnapshot(lb), q);
        Print("switch, volatile table", r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!Has(r.Text(), "switch"));
        CHECK(!r.flowWarnings.empty());
        CHECK(!r.prototype.known);
    }

    // Two ways out of one function: one into memory that is not captured, one into a bad word in
    // captured memory. Each is reported as what it is, at its own address.
    std::vector<uint8_t> none;
    auto lwSnap = MakeSnapshot(none);
    auto* lw = const_cast<MemoryRegion*>(lwSnap->RegionAt(MemorySnapshot::kLwramBase));
    const uint32_t f = 0x002FFFE8u;
    size_t off = f - lw->base;
    for (uint16_t w : { 0x4415,     // cmp/pl r4
                        0x890B,     // bt 0x00300004, past LWRAM's end
                        0x4411,     // cmp/pz r4
                        0x8903,     // bt 0x002FFFF8
                        0x000B,     // rts
                        0x0009,     //   nop
                        0x0009,
                        0x0009,
                        0xFFFF })   // 0x002FFFF8: not an instruction
    { lw->bytes[off] = uint8_t(w >> 8); lw->bytes[off + 1] = uint8_t(w); off += 2; }
    r = d.Decompile(lwSnap, Request(f));
    Print("cut and bad data", r);
    CHECK(r.truncated && r.truncatedAt == 0x00300004u && r.truncatedCount == 1);
    CHECK(r.badDataAt == std::vector<uint32_t>{ 0x002FFFF8u });
    CHECK(any(r, FlowWarning::Cut, 0x00300004u, "cut off"));
    CHECK(any(r, FlowWarning::BadData, 0x002FFFF8u, "do not decode"));

    // rts in LWRAM's last halfword: its slot is not captured, so the pair halts. Neither is body.
    auto slotSnap = MakeSnapshot(none);
    auto* sl = const_cast<MemoryRegion*>(slotSnap->RegionAt(MemorySnapshot::kLwramBase));
    off = 0x002FFFFAu - sl->base;
    for (uint16_t w : { 0x7401, 0x0009, 0x000B })   // add #1,r4 ; nop ; rts | slot at 0x00300000
    { sl->bytes[off] = uint8_t(w >> 8); sl->bytes[off + 1] = uint8_t(w); off += 2; }
    r = d.Decompile(slotSnap, Request(0x002FFFFAu));
    Print("rts with an uncaptured slot", r);
    CHECK(r.truncated && r.truncatedAt == 0x00300000u);
    CHECK(!r.InBody(0x00300000u) && !r.InBody(0x002FFFFEu) && r.InBody(0x002FFFFAu));

    // A 300-case switch, 299 of its cases in memory that is not captured (finding 4): every one is
    // a cut, however many refusals the fetch image keeps, and none is taken for bad data.
    auto bigSnap = MakeSnapshot(none);
    auto* big = const_cast<MemoryRegion*>(bigSnap->RegionAt(MemorySnapshot::kLwramBase));
    auto put16 = [&](uint32_t a, uint16_t w) { big->bytes[a - big->base] = uint8_t(w >> 8); big->bytes[a - big->base + 1] = uint8_t(w); };
    auto put32 = [&](uint32_t a, uint32_t v) { put16(a, uint16_t(v >> 16)); put16(a + 2, uint16_t(v)); };
    const uint32_t g = 0x002FE000u;
    const uint16_t code[] = { 0xE14B,     // mov #75,r1
                              0x4108,     // shll2 r1  (300)
                              0x3412,     // cmp/hs r1,r4
                              0x8904,     // bt default
                              0xC703,     // mova table,r0
                              0x4408,     // shll2 r4
                              0x014E,     // mov.l @(r0,r4),r1
                              0x412B,     // jmp @r1
                              0x0009,     //   nop
                              0x000B,     // default: rts
                              0xE0FF,     //   mov #-1,r0
                              0x0009 };
    for (size_t i = 0; i < sizeof code / sizeof code[0]; ++i) put16(g + 2 * uint32_t(i), code[i]);
    put32(g + 0x18, 0x002FFFF0u);
    for (uint32_t i = 1; i < 300; ++i) put32(g + 0x18 + 4 * i, 0x00300000u + 2 * i);
    put16(0x002FFFF0u, 0x000B);
    put16(0x002FFFF2u, 0x0009);
    r = d.Decompile(bigSnap, Request(g));
    std::printf("300-case switch: truncated %d, cut %d times at lowest %08X, %zu bad-data halts\n", int(r.truncated),
                r.truncatedCount, r.truncatedAt, r.badDataAt.size());
    CHECK(r.truncated && r.truncatedCount == 299 && r.truncatedAt == 0x00300002u);
    CHECK(r.badDataAt.empty());
}

// Flow that leaves a function through a jump into another known function, and from there through a
// second jump into a third (review of the generator's second grammar, A4-2). The first jump is
// resolved when the flow is first followed, the second only after a restart in the actions has
// folded the literal it jumps through, and that restart follows the flow again. Two things followed
// from reading the first flow alone: the entry the second jump reaches was not in the body when the
// actions removed its first instruction as dead (an 'add #-0x40,r15' whose frame is never used),
// so no "flow runs into" warning was made for it; and any instruction the later flow reached that
// the actions then removed was in no part of the body, though the code ran it. The body is what
// the last flow translated, whether or not the p-code survived (patch 0021), and every known entry
// in it is warned about.
void TestFlowReachedBody()
{
    Sh2Decompiler d(gConfig);
    std::vector<uint8_t> hw;
    using Ranges = std::vector<std::pair<uint32_t, uint32_t>>;
    auto into = [](const DecompileResult& r, uint32_t at) {
        int n = 0;
        for (const FlowWarning& w : r.flowWarnings)
            n += w.kind == FlowWarning::IntoFunction && w.address == at && Has(w.text, "another function's entry");
        return n;
    };
    auto intoCount = [](const DecompileResult& r) {
        int n = 0;
        for (const FlowWarning& w : r.flowWarnings) n += w.kind == FlowWarning::IntoFunction;
        return n;
    };
    // F0 (+0x00): saves r12 and PR in a register, stores r2 through r4, restores, and 'jmp @r3' to F1
    // (+0x14): 'sts.l pr,@-r15 ; mov.l @(pool),r2 ; lds.l @r15+,pr ; jmp @r2' to F2 (+0x24), the
    // third, which is given. Each jump goes through a literal pool word.
    auto chain = [&](uint32_t base, std::initializer_list<uint16_t> f2) {
        Place(hw, base, { 0x2FC6,     // mov.l r12,@-r15
                          0x0C2A,     // sts pr,r12
                          0x2456,     // mov.l r5,@-r4   (r5, a parameter: an r2 would be an input no signature passes, A3-5)
                          0xD302,     // mov.l @(pool),r3   -> F1
                          0x4C2A,     // lds r12,pr
                          0x6CF6,     // mov.l @r15+,r12
                          0x432B,     // jmp @r3
                          0x0009,     //   nop
                          uint16_t((base + 0x14) >> 16), uint16_t(base + 0x14),
                          0x4F22,     // F1: sts.l pr,@-r15
                          0xD202,     // mov.l @(pool),r2   -> F2
                          0x4F26,     // lds.l @r15+,pr
                          0x422B,     // jmp @r2
                          0x0009,     //   nop
                          0x0009,
                          uint16_t((base + 0x24) >> 16), uint16_t(base + 0x24) });
        Place(hw, base + 0x24, f2);
    };
    // F2 takes a frame it never uses: the first and the next-to-last instruction are dead.
    const uint32_t a = 0x06009000u;
    chain(a, { 0x7FC0,      // add #-0x40,r15
               0x6252,      // mov.l @r5,r2
               0x6070,      // mov.b @r7,r0
               0x7F40,      // add #0x40,r15
               0x000B, 0x0009 });
    // F2 opens with a test and a branch whose arms meet: both removed as dead.
    const uint32_t b = 0x06009100u;
    chain(b, { 0x2448,      // tst r4,r4
               0x8BFF,      // bf next
               0x6252,      // mov.l @r5,r2
               0x6070,      // mov.b @r7,r0
               0x000B, 0x0009 });
    auto snap = MakeSnapshot(hw);
    // As the panel asks: the later functions decompiled first, what they learned lent to the caller.
    auto request = [&](uint32_t base, bool second) {
        DecompileRequest q = Request(base);
        q.knownFunctions.push_back(base + 0x14);
        if (second) q.knownFunctions.push_back(base + 0x24);
        for (uint32_t callee : { base + 0x24, base + 0x14 })
        {
            if (callee == base + 0x24 && !second) continue;   // a function nobody named is lent nothing
            const DecompileResult c = d.Decompile(snap, Request(callee));
            if (c.prototype.known) q.prototypes.emplace_back(callee, c.prototype);
        }
        return q;
    };

    DecompileResult r = d.Decompile(snap, request(a, true));
    Print("two hops", r);
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(into(r, a + 0x14) == 1 && into(r, a + 0x24) == 1 && intoCount(r) == 2);
    // Everything the flow reached, the entry's 'add #-0x40,r15' and the 'add #0x40,r15' included.
    CHECK(r.body == (Ranges{ { a, a + 0x10 }, { a + 0x14, a + 0x1E }, { a + 0x24, a + 0x30 } }));
    CHECK(r.InBody(a + 0x24) && r.InBody(a + 0x2A));
    // The jumps are gotos once the actions have folded their literals; the signature is lent, as the
    // destinations the flow recovered are known to the return check (patch 0021), not only the ones left at the end.
    CHECK(r.prototype.known && r.prototypeWithheld.empty());
    // Only the first function known: the second jump goes to an address nobody named, which the engine
    // cannot follow (it is a tail call to it, not flow into it). One warning, and nothing of F2 in the body.
    r = d.Decompile(snap, request(a, false));
    Print("two hops, the second function not named", r);
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(into(r, a + 0x14) == 1 && intoCount(r) == 1);
    CHECK(!r.InBody(a + 0x24) && r.InBody(a + 0x14));
    // The entry of F2 is no "other" function for itself.
    r = d.Decompile(snap, request(a + 0x24, true));
    CHECK(r.status == DecompileStatus::Ok && intoCount(r) == 0 && r.warnings.empty());

    r = d.Decompile(snap, request(b, true));
    Print("two hops, a dead test and branch", r);
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(into(r, b + 0x14) == 1 && into(r, b + 0x24) == 1 && intoCount(r) == 2);
    CHECK(r.body == (Ranges{ { b, b + 0x10 }, { b + 0x14, b + 0x1E }, { b + 0x24, b + 0x30 } }));
    CHECK(r.InBody(b + 0x24) && r.InBody(b + 0x26));   // the tst and the bf
    CHECK(r.prototype.known && r.prototypeWithheld.empty());
}

// ===== Area 4, round 2: the default that is also a case (A4-4), the tail jump to code nothing else reaches
// (A4-5, a decision, not a fix) =============================================================================
// A jump table one of whose entries is the default's own destination. The bound check folds into
// the switch as its default, which makes the guard's branch always enter the switch block, so the
// edge from the guard into the join goes; the join keeps the edge from the switch block, which
// carries what the switch block computed (`mova`'s address in r0) and the default path took it too.
// The switch block holds no statement (`noInterveningStatement`), but it passes a COPY whatever uses
// its output. Patch 0027 refuses the fold when a value the join reads differs between the two edges.
void TestDefaultAlsoCase()
{
    Sh2Decompiler d(gConfig);
    std::vector<uint8_t> hw;
    const uint32_t scuIst = 0x25FE00A4u;
    // mov.l @(pool),r7 ; mov #4,r0 ; mov #2,r1 ; cmp/hs r1,r4 ; bt join ; shll2 r4 ; mova table,r0 ;
    // mov.l @(r0,r4),r1 ; jmp @r1 ; nop ; table (2 entries) ; the pool (SCU_IST) ; case 0: bra join ;
    // nop ; case 1: bra join ; nop ; join: tst #0x10,r0 ; bf skip ; mov.b r0,@r7 ; skip: rts ; nop.
    // The default path reaches the join with r0 = 4, the switch path with r0 = the mova address, so
    // `tst #0x10` is nonzero on the default path (4 & 0x10 = 0: the write is made) and 0x14 & 0x10 on the
    // cases (the mova address 0x06009014: 0x10 set, no write).
    auto program = [&](uint32_t base, uint32_t entry1) {
        Place(hw, base, { 0xD706, 0xE004, 0xE102, 0x3412, 0x890E, 0x4408, 0xC701, 0x014E, 0x412B, 0x0009,
                          uint16_t((base + 0x20) >> 16), uint16_t(base + 0x20),    // table: case 0
                          uint16_t(entry1 >> 16), uint16_t(entry1),                // ...entry 1
                          uint16_t(scuIst >> 16), uint16_t(scuIst),                // the pool
                          0xA002, 0x0009,                                          // 0x20: case 0: bra join
                          0xA000, 0x0009,                                          // 0x24: bra join
                          0xC810, 0x8B00, 0x2700, 0x000B, 0x0009 });               // 0x28: join
    };
    const uint32_t a = 0x06009000u, b = 0x06009100u;
    program(a, a + 0x28);   // entry 1 is the join itself, the default's destination
    program(b, b + 0x24);   // the control: entry 1 is its own 'bra join'
    auto snap = MakeSnapshot(hw);
    auto run = [&](const char* label, uint32_t base) {
        const DecompileResult r = d.Decompile(snap, Request(base));
        Print(label, r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(ReferenceOk(d, snap, Request(base), r));
        return r;
    };
    auto removedAsUnreachable = [](const DecompileResult& r) {
        for (const std::string& w : r.warnings)
            if (Has(w, "Removing unreachable block")) return true;
        return false;
    };

    DecompileResult r = run("default is also a case", a);
    // The join's test reads the default path's r0 (4) as well as the switch path's: the write is
    // reachable, listed once and printed under a test, and nothing is removed.
    CHECK(Ledger(r, { Wr(scuIst, 1) }));
    CHECK(r.InBody(a + 0x2C) && FateAt(r, AccessFate::Unreachable, 0, true) == 0 && !removedAsUnreachable(r));
    CHECK(Has(r.Text(), "& 0x10") && Has(r.Text(), "SCU_IST"));
    // The guard stays an if ahead of the switch: the 4 the default path carries is assigned there.
    CHECK(Has(r.Text(), "if (param_1 < 2)") && Has(r.Text(), "= 4;"));
    CHECK(!Has(r.Text(), "return 0x6009014;"));
    // The control: the same code with entry 1 at its own 'bra join' was always right.
    r = run("the control, entry 1 at its own bra", b);
    CHECK(Ledger(r, { Wr(scuIst, 1) }));
    CHECK(r.InBody(b + 0x2C) && !removedAsUnreachable(r));
    CHECK(Has(r.Text(), "& 0x10") && Has(r.Text(), "SCU_IST"));

    // A default that is also a case where the join reads nothing the switch block computed: the fold
    // stays (the join is `rts ; mov #5,r0`, the mova's r0 and the table entry in r1 are dead there).
    // case 0 stores r5 through r6 and goes to the join; entry 1 and the default are the join.
    std::vector<uint8_t> hw2;
    const uint32_t c = 0x06009000u;
    Place(hw2, c, { 0xE102, 0x3412, 0x8908, 0x4408, 0xC701, 0x014E, 0x412B, 0x0009,    // as above, no pool
                    0x0600, 0x901C, 0x0600, 0x9018,                                       // 0x10: table: case 0, the join
                    0x000B, 0xE005,                                                       // 0x18: join: rts ; mov #5,r0
                    0xAFFC, 0x2652 });                                                    // 0x1C: case 0: bra join ; mov.l r5,@r6
    auto snap2 = MakeSnapshot(hw2);
    r = d.Decompile(snap2, Request(c));
    Print("default is also a case, nothing differs", r);
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(Ledger(r, { Wr(0, 4) }));
    CHECK(Has(r.Text(), "switch(") && !Has(r.Text(), "if ("));
}

// A4-5, accepted by the design: an indirect jump with one destination, known only after the first
// flow, to code nothing else reaches, is a tail call. The destination is a function of its own: not
// in the body (the same rule that keeps an unnamed third function out in TestFlowReachedBody), named
// in the warning, and the signature is withheld, so nothing wrong is lent. Naming the entry shows it.
void TestTailJumpToUnreachedCode()
{
    Sh2Decompiler d(gConfig);
    std::vector<uint8_t> hw;
    auto tail = [](const DecompileResult& r, uint32_t at, const char* text) {
        for (const FlowWarning& w : r.flowWarnings)
            if (w.kind == FlowWarning::TailJump && w.address == at && Has(w.text, text)) return true;
        return false;
    };
    // mov #4,r0 ; braf r0 ; nop ; two data words ; add #1,r4 ; rts ; mov r4,r0
    const uint32_t a = 0x06009000u;
    Place(hw, a, { 0xE004, 0x0023, 0x0009, 0xFFFF, 0xFFFF, 0x7401, 0x000B, 0x6043 });
    // mov.l @(pool),r1 ; jmp @r1 ; nop ; data ; the pool (the destination) ; add #1,r4 ; rts ; mov r4,r0
    const uint32_t b = 0x06009100u;
    Place(hw, b, { 0xD101, 0x412B, 0x0009, 0xFFFF, 0x0600, 0x910C, 0x7401, 0x000B, 0x6043, 0x0009 });
    auto snap = MakeSnapshot(hw);

    DecompileResult r = d.Decompile(snap, Request(a));
    Print("braf over data to code that returns", r);
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(tail(r, a + 2, "braf is taken for a tail call to 0x0600900A") && tail(r, a + 2, "not part of this function's body"));
    CHECK(!r.InBody(a + 0x0A) && r.InBody(a + 2) && !r.prototype.known && Has(r.prototypeWithheld, "0x0600900A"));
    r = d.Decompile(snap, Request(b));
    Print("jmp through a literal over data to code that returns", r);
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(tail(r, b + 2, "jmp is taken for a tail call to 0x0600910C") && tail(r, b + 2, "not part of this function's body"));
    CHECK(!r.InBody(b + 0x0C) && !r.prototype.known);
    // Named as a function of its own, the destination is decompiled as one, and a caller lent its signature
    // calls it with the argument, and returns what it returns: since patch 0044 the jump's text states the callee's value, and the
    // caller is lent with it (A3-29; it was withheld while the engine printed `f(); return;`).
    DecompileRequest q = Request(a + 0x0A);
    const DecompileResult callee = d.Decompile(snap, q);
    CHECK(callee.status == DecompileStatus::Ok && callee.prototype.known && Has(callee.Text(), "param_1 + 1"));
    q = Request(a);
    q.prototypes.emplace_back(a + 0x0A, callee.prototype);
    r = d.Decompile(snap, q);
    Print("the destination named and lent", r);
    CHECK(r.status == DecompileStatus::Ok && r.prototype.known && r.prototype.ret.kind != TypeKind::Void &&
          Has(r.Text(), "func_0x0600900a(param_1)") && Has(r.Text(), "return iVar1;"));
}

// The engine paths the control-flow review could only check by reading the source. Two are reached
// end to end: the restart limit alone (no jump table involved) and a call the engine treats as never
// returning (through the test hook: nothing in SE marks one). The third, a failed second-stage
// jump-table recovery, has no SH-2 fixture -- both stages analyse the same scratch copy of the
// function -- so its warning's handling is pinned on the engine's own text.
void TestEngineEdgePaths()
{
    Sh2Decompiler d(gConfig);
    auto warned = [](const DecompileResult& r, const char* text) {
        for (const std::string& w : r.warnings)
            if (Has(w, text)) return true;
        return false;
    };

    // Restart limit. A pointer to a stack slot, spilled and reloaded, is found only after the stack's
    // dead code was removed (restart one); the global it leads to, whose first store was dead, only
    // after that (restart two, past the limit of one).
    std::vector<uint8_t> hw;
    Place(hw, 0x06060300u, { 0x7FF8,      // add #-8,r15
                             0xD206,      // mov.l @(lit,pc),r2   (0x06070000)
                             0x2242,      // mov.l r4,@r2         (dead: overwritten)
                             0x2252,      // mov.l r5,@r2
                             0x2F22,      // mov.l r2,@r15
                             0x61F3,      // mov r15,r1
                             0x1F11,      // mov.l r1,@(4,r15)
                             0x53F1,      // mov.l @(4,r15),r3    (&slot, reloaded)
                             0x6332,      // mov.l @r3,r3         (the global's address, through it)
                             0x6032,      // mov.l @r3,r0         (the global)
                             0x7F08,      // add #8,r15
                             0x000B,      // rts
                             0x0009,      //   nop
                             0x0009 });
    dts::Put32(hw, 0x0006031Cu, 0x06070000u);
    // A callee that never returns, and a caller with data after its call to it.
    const uint32_t dies = 0x06060400u, caller = 0x06060500u;
    Place(hw, dies, { 0xAFFE, 0x0009 });                                   // bra dies ; nop
    Place(hw, caller, { 0x4F22, Bsr(caller + 2, dies), 0x0009, 0xFFFF });  // sts.l pr ; bsr dies ; nop ; data
    auto snap = MakeSnapshot(hw);

    DecompileResult r = d.Decompile(snap, Request(0x06060300u));
    Print("restart limit", r);
    CHECK(r.status == DecompileStatus::Ok && warned(r, "Exceeded maximum restarts"));
    for (const FlowWarning& w : r.flowWarnings) CHECK(w.kind != FlowWarning::JumpTable);
    CHECK(!r.prototype.known && Has(r.prototypeWithheld, "restart limit"));

    // Never returns. Flow stops at the call, so the data after it is not reached, and ReturnCheck
    // ends the path there too: the caller's signature is lent. Without the mark, the same caller
    // flows into the data.
    const DecompileResult callee = d.Decompile(snap, Request(dies));
    CHECK(callee.prototype.known);
    DecompileRequest q = Request(caller);
    q.prototypes.emplace_back(dies, callee.prototype);
    r = d.Decompile(snap, q);
    CHECK(!r.badDataAt.empty() && !r.prototype.known);
    Sh2Decompiler::SetNoReturnForTests({ dies });
    r = d.Decompile(snap, q);
    Sh2Decompiler::SetNoReturnForTests({});
    Print("call that never returns", r);
    bool noReturn = false;
    for (const FlowWarning& w : r.flowWarnings) noReturn |= w.address == caller + 2 && Has(w.text, "does not return");
    CHECK(noReturn && r.badDataAt.empty() && !r.InBody(caller + 6));
    CHECK(r.prototype.known && r.prototypeWithheld.empty());

    // Failed second stage, on the engine's text (jumptable.cc, JumpTable::recoverMultistage, raised in
    // the scratch copy): kept, cases missing. The scratch copy's other warnings are dropped; a case
    // label on the function proper is kept without counting as missing cases.
    EngineWarning w = ClassifyEngineWarning("WARNING (jumptable): Second-stage recovery error");
    CHECK(w.keep && w.casesMissing && !w.restartLimit && w.text == "Second-stage recovery error");
    w = ClassifyEngineWarning("WARNING (jumptable): Sanity check requires truncation of jumptable");
    CHECK(w.keep && w.casesMissing);
    w = ClassifyEngineWarning("WARNING (jumptable): Bad instruction - Truncating control flow here");
    CHECK(!w.keep);
    w = ClassifyEngineWarning("WARNING: Bad switch case");
    CHECK(w.keep && !w.casesMissing);
    w = ClassifyEngineWarning("WARNING: Exceeded maximum restarts with more pending\n");
    CHECK(w.keep && w.restartLimit && w.text == "Exceeded maximum restarts with more pending");
}

void TestPrototypeEligibility()
{
    Sh2Decompiler d(gConfig);
    std::vector<uint8_t> hw;
    // An identity callee, and a wrapper forwarding its unchanged r4 to it.
    const uint32_t identity = 0x06010000u, wrapper = 0x06010100u, viaWrapper = 0x06010200u;
    Place(hw, identity, { 0x000B,      // rts
                          0x6043 });   //   mov r4,r0
    Place(hw, wrapper, { 0x4F22,                          // sts.l pr,@-r15
                         Bsr(wrapper + 2, identity),      // bsr identity
                         0x0009,                          //   nop
                         0x4F26,                          // lds.l @r15+,pr
                         0x000B,                          // rts
                         0x0009 });                       //   nop
    Place(hw, viaWrapper, { 0x4F22,                       // sts.l pr,@-r15
                            0xE42A,                       // mov #0x2a,r4
                            Bsr(viaWrapper + 4, wrapper), // bsr wrapper
                            0x0009,                       //   nop
                            0x4F26,                       // lds.l @r15+,pr
                            0x000B,                       // rts
                            0x0009 });                    //   nop
    // Recursion, an unresolved indirect call, and an unrecovered jump.
    const uint32_t recursive = 0x06010300u, indirect = 0x06010400u, jump = 0x06010500u;
    Place(hw, recursive, { 0x4F22, Bsr(recursive + 2, recursive), 0x0009, 0x4F26, 0x000B, 0x0009 });
    Place(hw, indirect, { 0x4F22,      // sts.l pr,@-r15
                          0x450B,      // jsr @r5
                          0x0009,      //   nop
                          0x4F26,      // lds.l @r15+,pr
                          0x000B,      // rts
                          0x0009 });   //   nop
    Place(hw, jump, { 0x442B,          // jmp @r4
                      0x0009 });       //   nop
    // The fifth and sixth arguments without the four register ones, and all five.
    const uint32_t fifth = 0x06010600u, sixth = 0x06010700u, five = 0x06010800u;
    Place(hw, fifth, { 0x000B,         // rts
                       0x60F2 });      //   mov.l @r15,r0
    Place(hw, sixth, { 0x000B,         // rts
                       0x50F1 });      //   mov.l @(4,r15),r0
    Place(hw, five, { 0x345C,          // add r5,r4
                      0x346C,          // add r6,r4
                      0x347C,          // add r7,r4
                      0x60F2,          // mov.l @r15,r0
                      0x000B,          // rts
                      0x304C });       //   add r4,r0
    // An invalid instruction.
    const uint32_t invalid = 0x06010900u;
    Place(hw, invalid, { 0xE001,       // mov #1,r0
                         0xFFFF });    // (no such instruction)
    // A 64-bit return: R0 is the high half, R1 the low.
    const uint32_t wide = 0x06010A00u, wideCaller = 0x06010B00u;
    Place(hw, wide, { 0xE001,          // mov #1,r0
                      0x000B,          // rts
                      0xE102 });       //   mov #2,r1
    Place(hw, wideCaller, { 0x4F22,                       // sts.l pr,@-r15
                            0x6D43,                       // mov r4,r13
                            Bsr(wideCaller + 4, wide),    // bsr wide
                            0x6E53,                       //   mov r5,r14
                            0x2D02,                       // mov.l r0,@r13
                            0x2E12,                       // mov.l r1,@r14
                            0x4F26,                       // lds.l @r15+,pr
                            0x000B,                       // rts
                            0x0009 });                    //   nop
    // PR not the caller's: replaced from r4, and saved in a bsr's slot (so it holds the
    // continuation, and the restore returns into this function's own epilogue).
    const uint32_t prFromR4 = 0x06010C00u, prInSlot = 0x06010D00u, leaf = 0x06010E00u;
    Place(hw, prFromR4, { 0x442A,      // lds r4,pr
                          0x000B,      // rts
                          0x0009 });   //   nop
    Place(hw, prInSlot, { Bsr(prInSlot, leaf),   // bsr leaf
                          0x4F22,                //   sts.l pr,@-r15 (the new PR)
                          0x4F26,                // lds.l @r15+,pr
                          0x000B,                // rts
                          0xE005 });             //   mov #5,r0
    Place(hw, leaf, { 0x000B, 0x0009 });         // rts; nop
    // A frame-pointer prologue and epilogue: PR saved below r14, reloaded through the restored r15.
    const uint32_t framed = 0x06010F00u;
    Place(hw, framed, { 0x4F22,                  // sts.l pr,@-r15
                        0x2FE6,                  // mov.l r14,@-r15
                        0x6EF3,                  // mov r15,r14
                        0x7FF8,                  // add #-8,r15
                        Bsr(framed + 8, leaf),   // bsr leaf
                        0x0009,                  //   nop
                        0x6FE3,                  // mov r14,r15
                        0x6EF6,                  // mov.l @r15+,r14
                        0x4F26,                  // lds.l @r15+,pr
                        0x000B,                  // rts
                        0x0009 });               //   nop
    // Review of 0be0808: saved PR overwritten (a) through a stack address the checker lost track of,
    // (b) by a callee handed a stack address, (c) in r8 by a callee that does not preserve r8.
    const uint32_t aliasStore = 0x06011000u, zeroArg = 0x06011100u, passesSp = 0x06011200u,
                   passesConst = 0x06011300u, clobbersR8 = 0x06011400u, prInR8 = 0x06011500u, prInR8Kept = 0x06011600u;
    Place(hw, aliasStore, { 0x4F22,      // sts.l pr,@-r15
                            0x60F3,      // mov r15,r0
                            0xCB00,      // or #0,r0
                            0xE100,      // mov #0,r1
                            0x2012,      // mov.l r1,@r0   (the saved PR)
                            0x4F26,      // lds.l @r15+,pr
                            0x000B,      // rts
                            0xE007 });   //   mov #7,r0
    Place(hw, zeroArg, { 0xE000,         // mov #0,r0
                         0x000B,         // rts
                         0x2402 });      //   mov.l r0,@r4
    Place(hw, passesSp, { 0x4F22,                       // sts.l pr,@-r15
                          0x64F3,                       // mov r15,r4
                          Bsr(passesSp + 4, zeroArg),   // bsr zeroArg
                          0x0009,                       //   nop
                          0x4F26,                       // lds.l @r15+,pr
                          0x000B,                       // rts
                          0xE007 });                    //   mov #7,r0
    Place(hw, passesConst, { 0x4F22,                          // sts.l pr,@-r15
                             0xE410,                          // mov #0x10,r4
                             Bsr(passesConst + 4, zeroArg),   // bsr zeroArg
                             0x0009,                          //   nop
                             0x4F26,                          // lds.l @r15+,pr
                             0x000B,                          // rts
                             0xE007 });                       //   mov #7,r0
    Place(hw, clobbersR8, { 0xE800,      // mov #0,r8
                            0x000B,      // rts
                            0x0009 });   //   nop
    Place(hw, prInR8, { 0x082A,                         // sts pr,r8
                        Bsr(prInR8 + 2, clobbersR8),    // bsr clobbersR8
                        0x0009,                         //   nop
                        0x482A,                         // lds r8,pr
                        0x000B,                         // rts
                        0xE007 });                      //   mov #7,r0
    Place(hw, prInR8Kept, { 0x082A,                     // sts pr,r8
                            Bsr(prInR8Kept + 2, leaf),  // bsr leaf
                            0x0009,                     //   nop
                            0x482A,                     // lds r8,pr
                            0x000B,                     // rts
                            0xE007 });                  //   mov #7,r0
    // Review of b9faafe: (d) a wrapper forwarding r4 to zeroArg stores through r4 too, so a caller
    // handing it the stack loses its saved PR; (e) PR saved at entry r15 + 0 -- the caller's frame,
    // which an entry-register pointer may reach -- is lost to zeroArg called through r4.
    const uint32_t forwards = 0x06011700u, passesSpOn = 0x06011800u, passesConstOn = 0x06011900u,
                   savedAtEntrySp = 0x06011A00u;
    Place(hw, forwards, { 0x4F22,                       // sts.l pr,@-r15
                          Bsr(forwards + 2, zeroArg),   // bsr zeroArg
                          0x0009,                       //   nop
                          0x4F26,                       // lds.l @r15+,pr
                          0x000B,                       // rts
                          0x0009 });                    //   nop
    Place(hw, passesSpOn, { 0x4F22,                         // sts.l pr,@-r15
                            0x64F3,                         // mov r15,r4
                            Bsr(passesSpOn + 4, forwards),  // bsr forwards
                            0x0009,                         //   nop
                            0x4F26,                         // lds.l @r15+,pr
                            0x000B,                         // rts
                            0xE007 });                      //   mov #7,r0
    Place(hw, passesConstOn, { 0x4F22,                            // sts.l pr,@-r15
                               0xE410,                            // mov #0x10,r4
                               Bsr(passesConstOn + 4, forwards),  // bsr forwards
                               0x0009,                            //   nop
                               0x4F26,                            // lds.l @r15+,pr
                               0x000B,                            // rts
                               0xE007 });                         //   mov #7,r0
    Place(hw, savedAtEntrySp, { 0x002A,                              // sts pr,r0
                                0x2F02,                              // mov.l r0,@r15
                                Bsr(savedAtEntrySp + 4, zeroArg),    // bsr zeroArg (r4 as given)
                                0x0009,                              //   nop
                                0x60F2,                              // mov.l @r15,r0
                                0x402A,                              // lds r0,pr
                                0x000B,                              // rts
                                0xE007 });                           //   mov #7,r0
    // Own review of da2c9d2: (f) a stack address saved in a slot, read back through an inexact
    // stack address; (g) PR left below r15 across a call; (h) sleep keeps everything; (i) a stack
    // address let out two calls down.
    const uint32_t inexactLoad = 0x06012000u, belowSp = 0x06012100u, sleeps = 0x06012200u, keepsR4 = 0x06012300u,
                   handsOn = 0x06012400u, storesThroughG = 0x06012500u, letOut = 0x06012600u, nearLeaf = 0x06012700u;
    Place(hw, nearLeaf, { 0x000B, 0x0009 });   // rts; nop  (leaf's bytes, within a bsr's reach)
    Place(hw, inexactLoad, { 0x4F22,      // sts.l pr,@-r15
                             0x61F3,      // mov r15,r1
                             0x2F16,      // mov.l r1,@-r15  (the saved PR's address, on the stack)
                             0x60F3,      // mov r15,r0
                             0xCB00,      // or #0,r0
                             0x6202,      // mov.l @r0,r2    (that address)
                             0x2232,      // mov.l r3,@r2    (over the saved PR)
                             0x7F04,      // add #4,r15
                             0x4F26,      // lds.l @r15+,pr
                             0x000B,      // rts
                             0xE007 });   //   mov #7,r0
    Place(hw, belowSp, { 0x002A,                     // sts pr,r0
                         0x61F3,                     // mov r15,r1
                         0x71FC,                     // add #-4,r1
                         0x2102,                     // mov.l r0,@r1   (PR saved below r15, which never moves)
                         Bsr(belowSp + 8, nearLeaf), // bsr nearLeaf   (its pushes land there)
                         0x0009,                     //   nop
                         0x6012,                     // mov.l @r1,r0
                         0x402A,                     // lds r0,pr
                         0x000B,                     // rts
                         0xE007 });                  //   mov #7,r0
    // (k) A stack address stored into an indexed local: the stack, not memory let out.
    const uint32_t indexedLocal = 0x06012900u;
    // (A frame is taken first: an indexed store off the entry r15 itself is a word of the caller's frame, a hand-back, A3-39.)
    Place(hw, indexedLocal, { 0x7FF8,      // add #-8,r15
                              0x61F3,      // mov r15,r1
                              0x314C,      // add r4,r1   (an unknown index; r0 would be an input no signature passes, A3-5)
                              0x62F3,      // mov r15,r2
                              0x2122,      // mov.l r2,@r1
                              0x000B,      // rts
                              0x7F08 });   //   add #8,r15
    // (l) A word of free stack (below r15), read exactly and through an inexact address, stored
    // through: it may hold the saved PR's address.
    const uint32_t freeExact = 0x06012A00u, freeInexact = 0x06012B00u;
    for (uint32_t at : { freeExact, freeInexact })
        Place(hw, at, { 0x4F22,                                // sts.l pr,@-r15
                        0x60F3,                                // mov r15,r0
                        0x70FC,                                // add #-4,r0   (below r15)
                        uint16_t(at == freeExact ? 0x0009      // nop
                                                 : 0xCB00),    // or #0,r0     (the same address, inexact)
                        0x6102,                                // mov.l @r0,r1
                        0xE200,                                // mov #0,r2
                        0x2122,                                // mov.l r2,@r1
                        0x4F26,                                // lds.l @r15+,pr
                        0x000B,                                // rts
                        0xE007 });                             //   mov #7,r0
    // (j) A stack address stored in two halves, read back whole and stored through.
    const uint32_t halves = 0x06012800u;
    Place(hw, halves, { 0x4F22,      // sts.l pr,@-r15
                        0x7FFC,      // add #-4,r15
                        0x60F3,      // mov r15,r0
                        0x7004,      // add #4,r0      (the saved PR's address)
                        0x81F1,      // mov.w r0,@(2,r15)
                        0x4029,      // shlr16 r0
                        0x2F01,      // mov.w r0,@r15
                        0x61F2,      // mov.l @r15,r1
                        0xE200,      // mov #0,r2
                        0x2122,      // mov.l r2,@r1
                        0x7F04,      // add #4,r15
                        0x4F26,      // lds.l @r15+,pr
                        0x000B,      // rts
                        0xE007 });   //   mov #7,r0
    Place(hw, sleeps, { 0x4F22,      // sts.l pr,@-r15
                        0x001B,      // sleep
                        0x4F26,      // lds.l @r15+,pr
                        0x000B,      // rts
                        0x0009 });   //   nop
    Place(hw, keepsR4, { 0x000B,      // rts
                         0x2542 });   //   mov.l r4,@r5   (keeps its argument in *r5)
    Place(hw, handsOn, { 0x4F22,                       // sts.l pr,@-r15
                         0x64F3,                       // mov r15,r4
                         0x7404,                       // add #4,r4      (its caller's frame: the saved PR)
                         Bsr(handsOn + 6, keepsR4),    // bsr keepsR4
                         0x0009,                       //   nop
                         0xE400,                       // mov #0,r4      (nothing handed back)
                         0x4F26,                       // lds.l @r15+,pr
                         0x000B,                       // rts
                         0x0009 });                    //   nop
    Place(hw, storesThroughG, { 0x6152,      // mov.l @r5,r1
                                0xE000,      // mov #0,r0
                                0x000B,      // rts
                                0x2102 });   //   mov.l r0,@r1
    Place(hw, letOut, { 0x4F22,                          // sts.l pr,@-r15
                        0xE540,                          // mov #0x40,r5
                        Bsr(letOut + 4, handsOn),        // bsr handsOn
                        0x0009,                          //   nop
                        0xE540,                          // mov #0x40,r5
                        Bsr(letOut + 10, storesThroughG),// bsr storesThroughG
                        0x0009,                          //   nop
                        0x4F26,                          // lds.l @r15+,pr
                        0x000B,                          // rts
                        0xE007 });                       //   mov #7,r0
    // Review of 0e3cd12: a callee that returns nothing, a caller that uses r0 after calling it (as if
    // it returned a value), and a caller storing a register's low word (its signature was withheld).
    const uint32_t voidLeaf = 0x06013000u, usesR0After = 0x06013100u, wordStoreCaller = 0x06013200u;
    Place(hw, voidLeaf, { 0x000B,          // rts
                          0x0009 });       //   nop
    Place(hw, usesR0After, { 0x4F22,                              // sts.l pr,@-r15
                             Bsr(usesR0After + 2, voidLeaf),      // bsr voidLeaf
                             0x0009,                              //   nop
                             0x7001,                              // add #1,r0      r0: what the callee left
                             0x4F26,                              // lds.l @r15+,pr
                             0x000B,                              // rts
                             0x0009 });                           //   nop
    // (r4 and r5 are held in r8 and r9 over the call: ledger A2-10 made r4-r7 unknown after a call,
    // as the SH ABI has them, so the caller that stored through r4 itself would read extraout_r4.)
    Place(hw, wordStoreCaller, { 0x4F22,                              // sts.l pr,@-r15
                                 0x6843,                              // mov r4,r8
                                 0x6953,                              // mov r5,r9
                                 Bsr(wordStoreCaller + 6, voidLeaf),  // bsr voidLeaf
                                 0x0009,                              //   nop
                                 0x2891,                              // mov.w r9,@r8   r9's low word
                                 0x4F26,                              // lds.l @r15+,pr
                                 0x000B,                              // rts
                                 0x0009 });                           //   nop
    auto snap = MakeSnapshot(hw);

    auto run = [&](uint32_t entry, std::initializer_list<std::pair<uint32_t, Prototype>> lent, const char* label) {
        DecompileRequest q = Request(entry);
        for (const auto& p : lent) q.prototypes.push_back(p);
        const DecompileResult r = d.Decompile(snap, q);
        Print(label, r);
        CHECK(r.status == DecompileStatus::Ok);
        return r;
    };

    // Controls: a leaf, five arguments.
    const DecompileResult id = run(identity, {}, "identity");
    CHECK(id.prototype.known && id.prototype.params.size() == 1 && id.prototype.ret.kind != TypeKind::Void);
    const DecompileResult f5 = run(five, {}, "five arguments");
    CHECK(f5.prototype.known && f5.prototype.params.size() == 5);

    // 1. The wrapper is tentative until its callee's signature is known, then it takes r4.
    const DecompileResult w0 = run(wrapper, {}, "wrapper, callee unknown");
    CHECK(!w0.prototype.known);
    const DecompileResult w1 = run(wrapper, { { identity, id.prototype } }, "wrapper, callee lent");
    CHECK(w1.prototype.known && w1.prototype.params.size() == 1);
    const DecompileResult v = run(viaWrapper, { { wrapper, w1.prototype } }, "caller of the wrapper");
    CHECK(Has(v.Text(), "(0x2a)"));
    CHECK(!run(recursive, {}, "recursive").prototype.known);
    CHECK(!run(indirect, {}, "indirect call").prototype.known);

    // 2. Stack arguments past unused register ones.
    CHECK(!run(fifth, {}, "fifth argument only").prototype.known);
    CHECK(!run(sixth, {}, "sixth argument only").prototype.known);

    // 3. Bad instructions and unrecovered jumps.
    CHECK(!run(invalid, {}, "invalid instruction").prototype.known);
    CHECK(!run(jump, {}, "unrecovered jump").prototype.known);

    // 4. R0 is the high half of a 64-bit return, in the callee and in a caller lent its signature.
    const DecompileResult wr = run(wide, {}, "64-bit return");
    CHECK(Has(wr.Text(), "0x100000002"));
    CHECK(!Has(wr.Text(), "0x200000001"));
    CHECK(wr.prototype.known && wr.prototype.ret.size == 8);
    const DecompileResult wc = run(wideCaller, { { wide, wr.prototype } }, "caller of the 64-bit return");
    const Line* high = LineWith(wc, "*param_1 =");
    const Line* low = LineWith(wc, "*param_2 =");
    // The high half, directly or through the variable the shift is assigned to.
    const Line* shift = LineWith(wc, ">> 0x20");
    std::string highHalf = ">> 0x20";
    if (shift && Has(shift->text, " = "))
    {
        const std::string t = shift->text.substr(0, shift->text.find(" = "));
        highHalf = t.substr(t.find_first_not_of(' '));
    }
    CHECK(shift && high && high->text.find(highHalf) != std::string::npos);
    CHECK(low && !Has(low->text, ">>") && (shift == nullptr || low->text.find(highHalf) == std::string::npos));

    // 5. Returns that do not go to the caller.
    const DecompileResult lf = run(leaf, {}, "leaf");
    CHECK(lf.prototype.known);
    CHECK(run(framed, { { leaf, lf.prototype } }, "frame pointer").prototype.known);
    CHECK(!run(prFromR4, {}, "PR from r4").prototype.known);
    CHECK(!run(prInSlot, { { leaf, lf.prototype } }, "PR saved in a bsr slot").prototype.known);

    // (a) A store through a stack address the checker cannot follow (or #0 keeps it a stack
    // address) may hit the saved PR.
    CHECK(!run(aliasStore, {}, "store through a lost stack address").prototype.known);
    // (b) A callee storing through its first argument: handed a stack address, it may overwrite the
    // saved PR; handed a constant, it cannot.
    const DecompileResult za = run(zeroArg, {}, "stores through its argument");
    CHECK(za.prototype.known && (za.prototype.effects.storesThrough & (1u << 4)));
    CHECK(!run(passesSp, { { zeroArg, za.prototype } }, "passes the stack to it").prototype.known);
    CHECK(run(passesConst, { { zeroArg, za.prototype } }, "passes a constant to it").prototype.known);
    // (c) PR kept in r8 across a call: only a callee verified to preserve r8 keeps it.
    const DecompileResult c8 = run(clobbersR8, {}, "clobbers r8");
    CHECK(c8.prototype.known && !(c8.prototype.effects.preserved & (1u << 8)));
    CHECK(lf.prototype.effects.preserved & (1u << 8));
    CHECK(!run(prInR8, { { clobbersR8, c8.prototype } }, "PR in r8 across a clobber").prototype.known);
    CHECK(run(prInR8Kept, { { leaf, lf.prototype } }, "PR in r8 across a leaf").prototype.known);

    // (d) Callee writes are part of the caller's own summary.
    const DecompileResult fw = run(forwards, { { zeroArg, za.prototype } }, "forwards r4 to zeroArg");
    CHECK(fw.prototype.known && (fw.prototype.effects.storesThrough & (1u << 4)));
    CHECK(!run(passesSpOn, { { forwards, fw.prototype } }, "passes the stack on through a wrapper").prototype.known);
    CHECK(run(passesConstOn, { { forwards, fw.prototype } }, "passes a constant through a wrapper").prototype.known);
    // (e) A callee's write through an entry register may reach the caller's frame, as a direct
    // store through it may.
    CHECK(!run(savedAtEntrySp, { { zeroArg, za.prototype } }, "PR saved at entry r15").prototype.known);

    // (f) A load through an address that may be in the stack can read any slot.
    CHECK(!run(inexactLoad, {}, "a stack address read back inexactly").prototype.known);
    // (g) Below r15 is free stack: pushes and interrupts write there.
    CHECK(!run(belowSp, { { nearLeaf, lf.prototype } }, "PR below r15 across a call").prototype.known);
    // (l) Free stack holds anything, however its address is known.
    CHECK(!run(freeExact, {}, "free stack read exactly").prototype.known);
    CHECK(!run(freeInexact, {}, "free stack read inexactly").prototype.known);
    // (j) Stores of part of a word are kept too.
    CHECK(!run(halves, {}, "a stack address in two halves").prototype.known);
    // (k) ...and a store that can only land in the stack lets nothing out to memory.
    const DecompileResult il = run(indexedLocal, {}, "an indexed local");
    CHECK(il.prototype.known && !(il.prototype.effects.leaks & origin::kStack) &&
          (il.prototype.effects.frameValues & origin::kStack));
    // (h) sleep changes no register or memory.
    CHECK(run(sleeps, {}, "sleep").prototype.known);
    // (i) handsOn lets its caller's frame out (through keepsR4), so a later store through memory in
    // that caller may hit its saved PR.
    const DecompileResult kr = run(keepsR4, {}, "keeps r4");
    CHECK(kr.prototype.known && (kr.prototype.effects.leaks & (1u << 4)));
    const DecompileResult ho = run(handsOn, { { keepsR4, kr.prototype } }, "hands its caller's frame on");
    CHECK(ho.prototype.known && (ho.prototype.effects.leaks & origin::kStack));
    const DecompileResult sg = run(storesThroughG, {}, "stores through a loaded pointer");
    CHECK(sg.prototype.known && (sg.prototype.effects.storesThrough & origin::kMemory));
    CHECK(!run(letOut, { { handsOn, ho.prototype }, { storesThroughG, sg.prototype } }, "frame let out, then stored through")
               .prototype.known);
    // The void callee is lent as it is; the caller that uses r0 after it is not lent on it; the
    // caller that stores a register's low word keeps its signature.
    {
        const DecompileResult leafR = d.Decompile(snap, Request(voidLeaf));
        CHECK(leafR.prototype.known && leafR.prototype.ret.kind == TypeKind::Void);
        DecompileRequest q = Request(usesR0After);
        q.prototypes.emplace_back(voidLeaf, leafR.prototype);
        const DecompileResult r1 = d.Decompile(snap, q);
        Print("uses r0 after a void callee", r1);
        // (The callee keeps r0 too, CallEffects::preserved, and that is not lent: r0 and r1 are the
        // return registers, A3-2.)
        CHECK(Has(r1.Text(), "extraout_r0"));
        CHECK(!r1.prototype.known && Has(r1.prototypeWithheld, "not known to return it") && Has(r1.prototypeWithheld, "r0"));
        q = Request(wordStoreCaller);
        q.prototypes.emplace_back(voidLeaf, leafR.prototype);
        const DecompileResult r2 = d.Decompile(snap, q);
        Print("stores a register's low word around a call", r2);
        CHECK(r2.prototype.known && r2.prototypeWithheld.empty());
        CHECK(Has(r2.Text(), "*param_1 = param_2;"));
    }
}

// ---- Review of area 2, round 2: what a call and a trap leave in the registers --------------
//
// No other oracle compares a printed value with a run: the access ledger and the generator's
// checks look at which memory is touched and which parameters are claimed, never at what the text
// says a register holds after a call. This one runs a fixture on the p-code emulator (the shipped
// translator, the callee and the trap handler present) and evaluates the pseudocode's returns for
// the same inputs, then requires the register the code returned to be among them.
//
// Limits, deliberate: the pseudocode is evaluated, not compiled -- straight-line code, `if`/`else`
// and `return` over 32-bit integer expressions; a loop, a goto, a switch or an operator it does
// not know fails the check loudly rather than passing. A value it cannot know (a call's result,
// `extraout_rN`, a read of memory, an input register) is "unknown" and agrees with any run, so
// the check finds a pseudocode that states a wrong value, never one that states too little; the
// fixtures' expected text pins the second. Casts are ignored, compares are signed. A condition
// that is unknown makes both arms possible. The run starts from one set of inputs per call (a
// few), not from every input: it is a witness, not a proof.

// One run of a snapshot's code. Memory is the snapshot, writes go to an overlay; every register
// starts as the caller sets it.
class RunMachine
{
public:
    explicit RunMachine(const std::shared_ptr<MemorySnapshot>& snap) : mSnap(snap)
    {
        mArch.reset(new SaturnArchitecture(mSnap, &mErrors));
        mArch->Initialise(20000, nullptr);
        ghidra::Translate* t = const_cast<ghidra::Translate*>(mArch->translate);
        mRam = t->getDefaultCodeSpace();
        mImage.reset(new ghidra::MemoryImage(mRam, 8, 4096, mArch->loader));
        mRamState.reset(new ghidra::MemoryPageOverlay(mRam, 8, 4096, mImage.get()));
        mRegs.reset(new ghidra::MemoryHashOverlay(t->getSpaceByName("register"), 8, 4096, 4096, nullptr));
        mTemps.reset(new ghidra::MemoryHashOverlay(t->getUniqueSpace(), 8, 4096, 4096, nullptr));
        mState.reset(new ghidra::MemoryState(t));
        mState->setMemoryBank(mRamState.get());
        mState->setMemoryBank(mRegs.get());
        mState->setMemoryBank(mTemps.get());
        mBreaks.reset(new ghidra::BreakTableCallBack(t));
        dts::HookSh2Userops(*mBreaks);
        mEmu.reset(new ghidra::EmulatePcodeCache(t, mState.get(), mBreaks.get()));
    }
    ~RunMachine()
    {
        mEmu.reset();
        mBreaks.reset();
        mState.reset();
    }
    void Set(const char* reg, uint32_t v) { mState->setValue(reg, v); }
    uint32_t Get(const char* reg) const { return static_cast<uint32_t>(mState->getValue(reg)); }
    // From 'start' until the next instruction is at 'stop'. False if it never gets there.
    bool Run(uint32_t start, uint32_t stop, int limit = 400)
    {
        mEmu->setExecuteAddress(ghidra::Address(mRam, start));
        mEmu->setHalt(false);
        for (int i = 0; i < limit; ++i)
        {
            mEmu->executeInstruction();
            if (mEmu->getExecuteAddress().getOffset() == stop) return true;
        }
        return false;
    }

private:
    std::ostringstream mErrors;
    std::shared_ptr<MemorySnapshot> mSnap;
    std::unique_ptr<SaturnArchitecture> mArch;
    ghidra::AddrSpace* mRam = nullptr;
    std::unique_ptr<ghidra::MemoryImage> mImage;
    std::unique_ptr<ghidra::MemoryPageOverlay> mRamState;
    std::unique_ptr<ghidra::MemoryHashOverlay> mRegs, mTemps;
    std::unique_ptr<ghidra::MemoryState> mState;
    std::unique_ptr<ghidra::BreakTableCallBack> mBreaks;
    std::unique_ptr<ghidra::EmulatePcodeCache> mEmu;
};

// The pseudocode, evaluated. Values carry the width and signedness the declarations give them (A3-11):
// a parameter or local is converted to its declared type on every assignment, a cast truncates or
// extends, and `<`, `>>`, `/` and `%` read their operands as signed or unsigned by the C rules. A type
// the printer leaves unknown (`xunknown4`, `undefined4`) has no sign: an operation that reads it both
// ways and gets two answers is unknown, which agrees with any run. What the text does not let the
// evaluator read (a construct it has no rule for, a 64-bit return, CONCAT or SUB of a width it cannot
// hold) is an error, which fails the check: it is not an unknown value.
class PseudoEval
{
public:
    enum Sign : uint8_t { Unsigned = 0, Signed = 1, Unknown = 2 };
    struct Val
    {
        bool known = false;
        uint64_t v = 0;   // the bits, within 'size' bytes
        int size = 4;
        Sign sg = Unknown;
    };
    struct Outcome
    {
        bool value = false;   // false: `return;` or falling off the end
        Val v;
    };

    // What the run gave the code on entry (A3-15, round 5): every register has a known value, and so does every
    // stack word (the snapshot's, below and above the entry r15). The names the engine gives these -- `in_r8`,
    // `unaff_r8`, `in_sr`, `in_stack_00000004`, and a stack local nothing in the text assigned (`xStack_8`, the
    // slot a callee pushed) -- read as those values. `extraout_rN` stays unknown: it is what a call returned.
    struct Entry
    {
        std::map<std::string, uint32_t> regs;   // "r0" ... "r15", "sr", "gbr", "vbr", "mach", "macl", "pr"
        std::shared_ptr<MemorySnapshot> snap;   // the stack words
        uint32_t sp = 0;                        // r15 on entry
        // Round 8: a read of the snapshot's code area is the value it holds (`*(xunknown4 *)(addr)`, `xRam06009034`), for a fixture that stores
        // nothing there. Off by default: a memory read is unknown, as it is for every other check.
        bool readCode = false;
        uint32_t codeLo = 0x06009000u, codeHi = 0x06010000u;
        std::map<uint32_t, int> globalSize;   // the width of each constant-address access the host recorded (set by the harness)
    };
    void SetEntry(const Entry* e) { mEntry = e; }

    // 'args' are the values of param_1, param_2, ... (r4, r5, ...).
    PseudoEval(const std::string& text, const std::vector<uint32_t>& args) : mArgs(args)
    {
        Lex(text);
        for (size_t i = 0; i + 1 < mTok.size(); ++i)
            if (mTok[i] == "&") mAddrTaken.insert(mTok[i + 1]);   // a local whose address is taken: a call may write it
        // The engine's note at a call that a frame address reaches in r2 (A3-28): every stack word may have been written by it.
        mEscapeNote = text.find("a frame address is in r2 here") != std::string::npos || text.find("a frame address is in r7 here") != std::string::npos ||
                      text.find("a frame address is in a stack argument slot here") != std::string::npos;
        // The header: everything up to the body's opening brace.
        while (mPos < mTok.size() && mTok[mPos] != "{") ++mPos;
        if (mPos >= mTok.size())
        {
            mError = "no body";
            return;
        }
        Header(mPos);
        ++mPos;
        mBody = Block();
        if (mError.empty() && mPos < mTok.size()) mError = "text after the body";
    }

    // Every value the function can return; empty, with an Error(), if the text is not understood.
    std::vector<Outcome> Run()
    {
        std::vector<Outcome> out;
        if (!mError.empty()) return out;
        std::map<std::string, Val> env;
        // Each parameter the header declares holds its register's value, converted to its type (a `char`
        // parameter is the low byte, signed).
        for (const auto& t : mTypes)
        {
            if (t.first.compare(0, 6, "param_") != 0) continue;
            const size_t i = static_cast<size_t>(std::atoi(t.first.c_str() + 6));
            Val v = Unk(4, Unknown);
            if (i >= 1 && i <= mArgs.size()) v = Make(mArgs[i - 1], 4, Unsigned);
            env[t.first] = Convert(v, t.second);
        }
        Exec({ { &mBody, 0 } }, env, out, 0);
        return out;
    }
    const std::string& Error() const { return mError; }

private:
    struct Ty
    {
        int size = 4;   // 0: void
        Sign sg = Unknown;
        bool isBool = false;
    };
    struct Node;
    using Seq = std::vector<Node>;
    struct Node
    {
        enum Kind { Assign, If, Return, Switch, Break, Skip, Call } kind = Skip;
        std::string var;
        size_t expr = 0, exprEnd = 0;   // the expression's tokens (Assign, the If's condition, Return, a Switch's selector)
        bool hasExpr = false;
        std::shared_ptr<Seq> yes, no;   // a Switch's body is 'yes': its statements in order, the labels apart
        struct Label
        {
            bool isDefault;
            uint64_t value;
            size_t at;   // the index in the body of the statement the label stands before
        };
        std::vector<Label> labels;
    };
    struct Frame
    {
        const Seq* seq;
        size_t next;
        bool sw = false;   // the body of a switch: a break leaves it
    };
    struct Cursor
    {
        size_t pos, end;
    };

    // ---- values ----
    static uint64_t Mask(int size) { return size >= 8 ? ~0ull : (1ull << (8 * size)) - 1; }
    static Val Make(uint64_t v, int size, Sign sg)
    {
        Val x;
        x.known = true;
        x.v = v & Mask(size);
        x.size = size;
        x.sg = sg;
        return x;
    }
    static Val Unk(int size, Sign sg)
    {
        Val x;
        x.size = size;
        x.sg = sg;
        return x;
    }
    static int64_t SignedOf(const Val& a)
    {
        const int bits = 8 * a.size;
        if (bits >= 64) return static_cast<int64_t>(a.v);
        const uint64_t m = 1ull << (bits - 1);
        return static_cast<int64_t>((a.v ^ m) - m);
    }
    // The integer a value stands for, as the bits of a wider one: a signed type extends its sign, any other
    // its zero.
    static uint64_t Widen(const Val& a) { return a.sg == Signed ? static_cast<uint64_t>(SignedOf(a)) : a.v; }
    static Val Convert(const Val& a, const Ty& t)
    {
        if (t.size <= 0) return Unk(4, Unknown);
        if (!a.known) return Unk(t.size, t.sg);
        if (t.isBool) return Make(a.v != 0, t.size, t.sg);
        return Make(Widen(a), t.size, t.sg);
    }
    // Narrower than an int becomes one (the usual promotion); a type without a sign stays without.
    static Val Promote(const Val& a)
    {
        if (a.size >= 4) return a;
        const Sign sg = a.sg == Unknown ? Unknown : Signed;
        return a.known ? Make(Widen(a), 4, sg) : Unk(4, sg);
    }
    // The type two promoted operands are brought to: the wider, and at one width the unsigned one.
    static Ty Common(const Val& a, const Val& b)
    {
        Ty t;
        t.size = std::max(a.size, b.size);
        if (a.size != b.size) t.sg = (a.size > b.size ? a : b).sg;
        else if (a.sg == Unsigned || b.sg == Unsigned) t.sg = Unsigned;
        else if (a.sg == Unknown || b.sg == Unknown) t.sg = Unknown;
        else t.sg = Signed;
        return t;
    }

    // ---- types ----
    // A type word as the printer writes them: int, uint4, xunknown4, undefined2, char, code, ...
    static bool IsType(const std::string& t)
    {
        static const char* const prefixes[] = { "int", "uint", "char", "uchar", "short", "ushort", "long", "ulong", "bool",
                                                "code", "byte", "xunknown", "unknown", "undefined", "void", "unsigned",
                                                "signed", "ulonglong", "longlong" };
        for (const char* p : prefixes)
        {
            const size_t n = std::strlen(p);
            if (t.compare(0, n, p) != 0) continue;
            bool digits = true;
            for (size_t i = n; i < t.size(); ++i) digits = digits && std::isdigit(static_cast<unsigned char>(t[i]));
            if (digits) return true;
        }
        return false;
    }
    // One type word: its size and sign. `int`/`uint` without a width are 4 bytes, `long` 4 (the SH-2's), `longlong` 8.
    static Ty TypeOfWord(const std::string& t)
    {
        size_t k = t.size();
        while (k > 0 && std::isdigit(static_cast<unsigned char>(t[k - 1]))) --k;
        const std::string name = t.substr(0, k);
        const int n = k < t.size() ? std::atoi(t.c_str() + k) : 0;
        Ty r;
        if (name == "int") r = { n ? n : 4, Signed, false };
        else if (name == "uint") r = { n ? n : 4, Unsigned, false };
        else if (name == "char") r = { 1, Signed, false };
        else if (name == "uchar" || name == "byte") r = { 1, Unsigned, false };
        else if (name == "bool") r = { 1, Unsigned, true };
        else if (name == "short") r = { 2, Signed, false };
        else if (name == "ushort") r = { 2, Unsigned, false };
        else if (name == "long") r = { 4, Signed, false };
        else if (name == "ulong") r = { 4, Unsigned, false };
        else if (name == "longlong") r = { 8, Signed, false };
        else if (name == "ulonglong") r = { 8, Unsigned, false };
        else if (name == "code") r = { 4, Unsigned, false };
        else if (name == "void") r = { 0, Unknown, false };
        else if (name == "unsigned") r = { 4, Unsigned, false };
        else if (name == "signed") r = { 4, Signed, false };
        else r = { n ? n : 4, Unknown, false };   // xunknown4, undefined4: a width and no sign
        if (r.size > 8) r.size = 8;
        return r;
    }
    // The type the tokens [s, e) spell: a star makes a pointer (4 bytes), `unsigned` before a word makes it unsigned.
    Ty TypeOf(size_t s, size_t e) const
    {
        Ty r;
        bool uns = false, sgn = false, any = false;
        for (size_t i = s; i < e; ++i)
        {
            if (mTok[i] == "*")
            {
                Ty p;
                p.size = 4;
                p.sg = Unsigned;
                return p;
            }
            if (mTok[i] == "unsigned") uns = true;
            else if (mTok[i] == "signed") sgn = true;
            else if (IsType(mTok[i]) && !any)
            {
                r = TypeOfWord(mTok[i]);
                any = true;
            }
        }
        if (uns && (!any || r.sg == Signed)) r.sg = Unsigned;
        if (sgn && !any) r.sg = Signed;
        return r;
    }

    // The function's own declaration: its return type, and each parameter's.
    void Header(size_t bodyAt)
    {
        size_t p = 0;
        while (p < bodyAt && mTok[p] != "(") ++p;
        if (p == 0 || p >= bodyAt) return;
        mRet = TypeOf(0, p - 1);   // the tokens before the function's name
        size_t depth = 0, s = p + 1;
        for (size_t i = p + 1; i < bodyAt; ++i)
        {
            if (mTok[i] == "(") ++depth;
            const bool close = mTok[i] == ")" && depth == 0;
            if (mTok[i] == ")" && depth > 0) --depth;
            if (close || (mTok[i] == "," && depth == 0))
            {
                if (i > s && !(i == s + 1 && mTok[s] == "void") && i - s >= 2)
                    mTypes[mTok[i - 1]] = TypeOf(s, i - 1);
                s = i + 1;
                if (close) break;
            }
        }
    }

    // `type name, *name;`: the tokens [s, e) of a declaration, each name given the type.
    void Declare(size_t s, size_t e)
    {
        size_t j = s;
        while (j < e && IsType(mTok[j])) ++j;
        if (j == s) return;
        const Ty base = TypeOf(s, j);
        while (j < e)
        {
            bool ptr = false;
            while (j < e && mTok[j] == "*")
            {
                ptr = true;
                ++j;
            }
            if (j >= e) break;
            Ty t = base;
            if (ptr)
            {
                t.size = 4;
                t.sg = Unsigned;
                t.isBool = false;
            }
            mTypes[mTok[j]] = t;
            ++j;
            if (j < e && mTok[j] == ",") ++j;
        }
    }

    void Lex(const std::string& s)
    {
        size_t i = 0;
        while (i < s.size())
        {
            const char c = s[i];
            if (std::isspace(static_cast<unsigned char>(c)))
            {
                ++i;
                continue;
            }
            if (c == '/' && i + 1 < s.size() && s[i + 1] == '*')   // a comment
            {
                const size_t end = s.find("*/", i + 2);
                i = end == std::string::npos ? s.size() : end + 2;
                continue;
            }
            if (c == '\'')   // a character constant, one token with its quotes: '\x02', 'A', '\\'
            {
                size_t j = i + 1;
                while (j < s.size() && s[j] != '\'')
                    j += s[j] == '\\' ? 2 : 1;
                mTok.push_back(s.substr(i, std::min(j + 1, s.size()) - i));
                i = j + 1;
                continue;
            }
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            {
                size_t j = i;
                while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
                mTok.push_back(s.substr(i, j - i));
                i = j;
                continue;
            }
            static const char* const two[] = { "<<", ">>", "==", "!=", "<=", ">=", "&&", "||", "->" };
            bool done = false;
            for (const char* t : two)
                if (s.compare(i, 2, t) == 0)
                {
                    mTok.push_back(t);
                    i += 2;
                    done = true;
                    break;
                }
            if (done) continue;
            mTok.push_back(std::string(1, c));
            ++i;
        }
    }

    // A block up to its closing brace, which is consumed.
    Seq Block()
    {
        Seq seq;
        while (mError.empty() && mPos < mTok.size() && mTok[mPos] != "}") seq.push_back(Statement());
        if (mPos < mTok.size()) ++mPos;
        return seq;
    }

    void SkipTo(const char* stop)
    {
        int depth = 0;
        while (mPos < mTok.size())
        {
            const std::string& t = mTok[mPos];
            if (t == "(") ++depth;
            else if (t == ")") --depth;
            else if (depth == 0 && t == stop) return;
            ++mPos;
        }
        mError = "unterminated statement";
    }

    Seq Arm()
    {
        if (mPos < mTok.size() && mTok[mPos] == "{")
        {
            ++mPos;
            return Block();
        }
        Seq s;
        s.push_back(Statement());
        return s;
    }

    // The value of a character constant's token ('\x02'), as an int (char is signed).
    static bool CharValue(const std::string& t, int64_t& v)
    {
        if (t.size() < 3 || t[0] != '\'' || t.back() != '\'') return false;
        const std::string b = t.substr(1, t.size() - 2);
        int c = 0;
        if (b[0] != '\\') c = static_cast<unsigned char>(b[0]);
        else if (b.size() < 2) return false;
        else if (b[1] == 'x') c = static_cast<int>(std::strtoul(b.c_str() + 2, nullptr, 16));
        else if (b[1] >= '0' && b[1] <= '7') c = static_cast<int>(std::strtoul(b.c_str() + 1, nullptr, 8));
        else
            switch (b[1])
            {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'a': c = 7; break;
            case 'b': c = 8; break;
            case 'f': c = 12; break;
            case 'v': c = 11; break;
            case '\\': c = '\\'; break;
            case '\'': c = '\''; break;
            case '"': c = '"'; break;
            case '?': c = '?'; break;
            default: return false;
            }
        v = static_cast<int8_t>(static_cast<uint8_t>(c));
        return true;
    }

    Node Statement()
    {
        Node n;
        const std::string t = mTok[mPos];
        if (t == "if")
        {
            n.kind = Node::If;
            mPos += 2;   // if (
            n.expr = mPos;
            int depth = 1;
            while (mPos < mTok.size())
            {
                if (mTok[mPos] == "(") ++depth;
                else if (mTok[mPos] == ")" && --depth == 0) break;
                ++mPos;
            }
            n.exprEnd = mPos;
            n.hasExpr = true;
            ++mPos;
            n.yes = std::make_shared<Seq>(Arm());
            if (mPos < mTok.size() && mTok[mPos] == "else")
            {
                ++mPos;
                n.no = std::make_shared<Seq>(Arm());
            }
            return n;
        }
        if (t == "return")
        {
            n.kind = Node::Return;
            ++mPos;
            n.expr = mPos;
            SkipTo(";");
            n.exprEnd = mPos;
            n.hasExpr = n.exprEnd > n.expr;
            ++mPos;
            return n;
        }
        // switch (selector) { case N: ... break; ... default: ... }: falls through from a label to the next
        // statements, as C does; a case value may be negative or a character constant.
        if (t == "switch")
        {
            n.kind = Node::Switch;
            mPos += 2;   // switch (
            n.expr = mPos;
            int depth = 1;
            while (mPos < mTok.size())
            {
                if (mTok[mPos] == "(") ++depth;
                else if (mTok[mPos] == ")" && --depth == 0) break;
                ++mPos;
            }
            n.exprEnd = mPos;
            n.hasExpr = true;
            ++mPos;
            if (mPos >= mTok.size() || mTok[mPos] != "{")
            {
                mError = "a switch without its body";
                return n;
            }
            ++mPos;
            n.yes = std::make_shared<Seq>();
            while (mError.empty() && mPos < mTok.size() && mTok[mPos] != "}")
            {
                if (mTok[mPos] == "case")
                {
                    ++mPos;
                    bool neg = false;
                    if (mPos < mTok.size() && mTok[mPos] == "-")
                    {
                        neg = true;
                        ++mPos;
                    }
                    int64_t cv = 0;
                    uint64_t v = 0;
                    const bool number = mPos < mTok.size() && std::isdigit(static_cast<unsigned char>(mTok[mPos][0]));
                    const bool chr = mPos < mTok.size() && CharValue(mTok[mPos], cv);
                    if (mPos + 1 >= mTok.size() || !(number || chr) || mTok[mPos + 1] != ":")
                    {
                        mError = "a case label this evaluator does not read";
                        return n;
                    }
                    v = number ? std::strtoull(mTok[mPos].c_str(), nullptr, 0) : static_cast<uint64_t>(cv);
                    n.labels.push_back({ false, neg ? 0ull - v : v, n.yes->size() });
                    mPos += 2;
                }
                else if (mTok[mPos] == "default" && mPos + 1 < mTok.size() && mTok[mPos + 1] == ":")
                {
                    n.labels.push_back({ true, 0, n.yes->size() });
                    mPos += 2;
                }
                else n.yes->push_back(Statement());
            }
            if (mPos < mTok.size()) ++mPos;   // the closing brace
            return n;
        }
        if (t == "break" && mPos + 1 < mTok.size() && mTok[mPos + 1] == ";")
        {
            n.kind = Node::Break;
            mPos += 2;
            return n;
        }
        if (t == "while" || t == "do" || t == "for" || t == "goto" || t == "break" || t == "continue" ||
            (mPos + 1 < mTok.size() && mTok[mPos + 1] == ":"))
        {
            mError = "unsupported statement: " + t;
            mPos = mTok.size();
            return n;
        }
        // A declaration: names and stars only, at least two names (`xunknown4 xVar1;`).
        {
            size_t k = mPos;
            int names = 0;
            bool decl = true;
            while (decl && k < mTok.size() && mTok[k] != ";")
            {
                const std::string& u = mTok[k];
                if (std::isalpha(static_cast<unsigned char>(u[0])) || u[0] == '_') ++names;
                else if (u != "*" && u != ",") decl = false;
                ++k;
            }
            if (decl && names >= 2 && k < mTok.size())
            {
                Declare(mPos, k);
                mPos = k + 1;
                return n;
            }
        }
        // `name = expr;`
        if (mPos + 1 < mTok.size() && mTok[mPos + 1] == "=" && (std::isalpha(static_cast<unsigned char>(t[0])) || t[0] == '_'))
        {
            n.kind = Node::Assign;
            n.var = t;
            mPos += 2;
            n.expr = mPos;
            SkipTo(";");
            n.exprEnd = mPos;
            n.hasExpr = true;
            ++mPos;
            return n;
        }
        // `name += expr;`, `name++;`: a change to a local this evaluator would not see.
        if (mPos + 2 < mTok.size() && (std::isalpha(static_cast<unsigned char>(t[0])) || t[0] == '_') && Prec(mTok[mPos + 1]) > 0 &&
            (mTok[mPos + 2] == "=" || (mTok[mPos + 2] == mTok[mPos + 1] && (mTok[mPos + 1] == "+" || mTok[mPos + 1] == "-"))))
        {
            mError = "an assignment this evaluator does not read: " + t + " " + mTok[mPos + 1] + mTok[mPos + 2];
            mPos = mTok.size();
            return n;
        }
        // Anything else is a call or a store: it changes no register local. A call may write a stack local
        // (the engine keeps one that a call could reach, A3-9), so the evaluator forgets those.
        {
            const size_t from = mPos;
            SkipTo(";");
            for (size_t k = from; k < mPos && k < mTok.size(); ++k)
                if (mTok[k] == "(") n.kind = Node::Call;
        }
        ++mPos;
        return n;
    }

    // ---- expressions ----
    static int Prec(const std::string& op)
    {
        if (op == "||") return 1;
        if (op == "&&") return 2;
        if (op == "|") return 3;
        if (op == "^") return 4;
        if (op == "&") return 5;
        if (op == "==" || op == "!=") return 6;
        if (op == "<" || op == ">" || op == "<=" || op == ">=") return 7;
        if (op == "<<" || op == ">>") return 8;
        if (op == "+" || op == "-") return 9;
        if (op == "*" || op == "/" || op == "%") return 10;
        return -1;
    }

    // 'op' on two values of one width, read as signed or unsigned; false if it has no value (a division by zero).
    static bool Compute(const std::string& op, const Val& a, const Val& b, bool sgn, uint64_t& r)
    {
        const uint64_t x = a.v, y = b.v;
        const int64_t sx = SignedOf(a), sy = SignedOf(b);
        if (op == "+") r = x + y;
        else if (op == "-") r = x - y;
        else if (op == "*") r = x * y;
        else if (op == "&") r = x & y;
        else if (op == "|") r = x | y;
        else if (op == "^") r = x ^ y;
        else if (op == "/" || op == "%")
        {
            if (!y) return false;
            if (!sgn) r = op == "/" ? x / y : x % y;
            else if (sy == -1) r = op == "/" ? 0ull - static_cast<uint64_t>(sx) : 0;   // no INT_MIN / -1 trap
            else r = static_cast<uint64_t>(op == "/" ? sx / sy : sx % sy);
        }
        else if (op == "==") r = x == y;
        else if (op == "!=") r = x != y;
        else if (op == "<") r = sgn ? sx < sy : x < y;
        else if (op == ">") r = sgn ? sx > sy : x > y;
        else if (op == "<=") r = sgn ? sx <= sy : x <= y;
        else if (op == ">=") r = sgn ? sx >= sy : x >= y;
        else return false;
        return true;
    }

    Val Apply(const std::string& op, Val a, Val b)
    {
        if (op == "&&" || op == "||")
        {
            const bool and_ = op == "&&";
            if ((a.known && (a.v != 0) == !and_) || (b.known && (b.v != 0) == !and_)) return Make(!and_, 4, Signed);   // 0 && x, 1 || x
            if (!a.known || !b.known) return Unk(4, Signed);
            return Make(and_, 4, Signed);
        }
        if (op == "<<" || op == ">>")
        {
            a = Promote(a);
            b = Promote(b);
            if (!a.known) return Unk(a.size, a.sg);
            if (a.v == 0) return Make(0, a.size, a.sg);
            if (!b.known) return Unk(a.size, a.sg);
            const int bits = 8 * a.size;
            const uint64_t count = b.v;
            if (b.sg == Signed && SignedOf(b) < 0) return Unk(a.size, a.sg);
            if (op == "<<") return Make(count >= static_cast<uint64_t>(bits) ? 0 : a.v << count, a.size, a.sg);
            const uint64_t logical = count >= static_cast<uint64_t>(bits) ? 0 : a.v >> count;
            const int64_t s = SignedOf(a);
            const uint64_t arithmetic = count >= static_cast<uint64_t>(bits) ? (s < 0 ? ~0ull : 0ull) : static_cast<uint64_t>(s >> count);
            if (a.sg == Unsigned) return Make(logical, a.size, a.sg);
            if (a.sg == Signed) return Make(arithmetic, a.size, a.sg);
            if ((logical & Mask(a.size)) == (arithmetic & Mask(a.size))) return Make(logical, a.size, a.sg);
            return Unk(a.size, a.sg);   // a right shift of a value without a sign: it depends on which
        }
        if (Prec(op) < 0)
        {
            mError = "unsupported operator " + op;
            return Val();
        }
        a = Promote(a);
        b = Promote(b);
        const Ty t = Common(a, b);
        a = Convert(a, t);
        b = Convert(b, t);
        const bool compare = Prec(op) == 6 || Prec(op) == 7;
        const Ty result = compare ? Ty{ 4, Signed, false } : t;
        // An absorbing operand decides without the other side.
        if ((op == "&" || op == "*") && ((a.known && !a.v) || (b.known && !b.v))) return Make(0, result.size, result.sg);
        if (!a.known || !b.known) return Unk(result.size, result.sg);
        uint64_t r = 0;
        const bool dependsOnSign = op == "/" || op == "%" || (compare && Prec(op) == 7);
        if (dependsOnSign && t.sg == Unknown)
        {
            uint64_t u = 0, s = 0;
            const bool okU = Compute(op, a, b, false, u), okS = Compute(op, a, b, true, s);
            if (!okU || !okS || (u & Mask(result.size)) != (s & Mask(result.size))) return Unk(result.size, result.sg);
            return Make(u, result.size, result.sg);
        }
        if (!Compute(op, a, b, t.sg == Signed, r)) return Unk(result.size, result.sg);
        return Make(r, result.size, result.sg);
    }

    void SkipGroup(Cursor& c)
    {
        int depth = 0;
        while (c.pos < c.end)
        {
            if (mTok[c.pos] == "(") ++depth;
            else if (mTok[c.pos] == ")" && --depth == 0)
            {
                ++c.pos;
                return;
            }
            ++c.pos;
        }
    }

    // The arguments of a call, `(` at c.pos, to the `)`: comma-separated expressions.
    std::vector<Val> Arguments(Cursor& c, const std::map<std::string, Val>& env)
    {
        std::vector<Val> args;
        ++c.pos;   // (
        if (c.pos < c.end && mTok[c.pos] == ")")
        {
            ++c.pos;
            return args;
        }
        while (c.pos < c.end && mError.empty())
        {
            args.push_back(ParseExpr(c, env, 1));
            if (c.pos < c.end && mTok[c.pos] == ",") ++c.pos;
            else break;
        }
        if (c.pos < c.end && mTok[c.pos] == ")") ++c.pos;
        else if (mError.empty()) mError = "unbalanced parentheses";
        return args;
    }

    // The decompiler's own operators, written like calls: CONCAT31(a, b), SUB41(x, 0), ZEXT48(x), SEXT48(x),
    // CARRY4(a, b), SCARRY4(a, b), SBORROW4(a, b). The digits are widths in bytes. Any other name of that shape
    // is one this evaluator has no rule for.
    static bool BuiltinShape(const std::string& name)
    {
        size_t k = name.size();
        while (k > 0 && std::isdigit(static_cast<unsigned char>(name[k - 1]))) --k;
        if (k == 0 || k == name.size()) return false;
        for (size_t i = 0; i < k; ++i)
            if (!std::isupper(static_cast<unsigned char>(name[i]))) return false;
        return true;
    }
    bool Builtin(const std::string& name, const std::vector<Val>& a, Val& out)
    {
        size_t k = name.size();
        while (k > 0 && std::isdigit(static_cast<unsigned char>(name[k - 1]))) --k;
        const std::string base = name.substr(0, k), digits = name.substr(k);
        auto known = [&](size_t n) {
            if (a.size() != n) return false;
            for (const Val& v : a)
                if (!v.known) return false;
            return true;
        };
        auto fail = [&](const char* what) {
            mError = std::string(what) + " " + name + " this evaluator does not read";
            return true;
        };
        if (base == "CONCAT" && digits.size() == 2)
        {
            const int hi = digits[0] - '0', lo = digits[1] - '0';
            if (hi < 1 || lo < 1 || hi + lo > 8) return fail("a width of");
            if (a.size() != 2) return fail("the arguments of");
            out = known(2) ? Make((a[0].v << (8 * lo)) | a[1].v, hi + lo, Unknown) : Unk(hi + lo, Unknown);
            return true;
        }
        if (base == "SUB" && digits.size() == 2)
        {
            const int from = digits[0] - '0', to = digits[1] - '0';
            if (from < 1 || to < 1 || from > 8 || to > 8) return fail("a width of");
            if (a.size() != 2 || (a[1].known && static_cast<int>(a[1].v) + to > from)) return fail("the arguments of");
            out = known(2) ? Make(a[0].v >> (8 * a[1].v), to, Unknown) : Unk(to, Unknown);
            return true;
        }
        if ((base == "ZEXT" || base == "SEXT") && digits.size() == 2)
        {
            const int from = digits[0] - '0', to = digits[1] - '0';
            if (from < 1 || to < from || to > 8 || a.size() != 1) return fail("a width of");
            if (!a[0].known) out = Unk(to, Unknown);
            else
            {
                Val src = Make(a[0].v, from, base == "SEXT" ? Signed : Unsigned);
                out = Make(Widen(src), to, Unknown);
            }
            return true;
        }
        if ((base == "CARRY" || base == "SCARRY" || base == "SBORROW") && digits.size() == 1)
        {
            const int n = digits[0] - '0';
            if (n < 1 || n > 8 || a.size() != 2) return fail("a width of");
            if (!known(2))
            {
                out = Unk(1, Unsigned);
                return true;
            }
            const Val x = Make(a[0].v, n, Signed), y = Make(a[1].v, n, Signed);
            const uint64_t ux = x.v, uy = y.v, m = Mask(n);
            bool r = false;
            if (base == "CARRY") r = n == 8 ? ux + uy < ux : ux + uy > m;
            else
            {
                const int64_t sx = SignedOf(x), sy = SignedOf(y);
                const uint64_t sum = static_cast<uint64_t>(sx) + static_cast<uint64_t>(sy);
                const uint64_t dif = static_cast<uint64_t>(sx) - static_cast<uint64_t>(sy);
                const bool sxNeg = sx < 0, syNeg = sy < 0;
                const bool sumNeg = (sum >> (8 * n - 1)) & 1, difNeg = (dif >> (8 * n - 1)) & 1;
                r = base == "SCARRY" ? (sxNeg == syNeg && sumNeg != sxNeg) : (sxNeg != syNeg && difNeg != sxNeg);
            }
            out = Make(r, 1, Unsigned);
            return true;
        }
        return fail("the operator");
    }

    Val ParseUnary(Cursor& c, const std::map<std::string, Val>& env)
    {
        if (c.pos >= c.end)
        {
            mError = "expression ends early";
            return Val();
        }
        const std::string t = mTok[c.pos++];
        if (t == "-")
        {
            const Val v = Promote(ParseUnary(c, env));
            return v.known ? Make(0ull - v.v, v.size, v.sg) : Unk(v.size, v.sg);
        }
        if (t == "!")
        {
            const Val v = ParseUnary(c, env);
            return v.known ? Make(v.v ? 0 : 1, 4, Signed) : Unk(4, Signed);
        }
        if (t == "~")
        {
            const Val v = Promote(ParseUnary(c, env));
            return v.known ? Make(~v.v, v.size, v.sg) : Unk(v.size, v.sg);
        }
        if (t == "*" && mEntry != nullptr && mEntry->readCode && mEntry->snap != nullptr && c.pos < c.end && mTok[c.pos] == "(")
        {
            // `*(T *)operand`: the value T-wide at the operand's address when the snapshot holds it in the fixture's code area.
            size_t k = c.pos + 1;
            bool cast = k < c.end && IsType(mTok[k]);
            while (cast && k < c.end && mTok[k] != ")")
            {
                if (!(IsType(mTok[k]) || mTok[k] == "*")) cast = false;
                ++k;
            }
            if (cast && k < c.end && k >= c.pos + 3 && mTok[k - 1] == "*")
            {
                const Ty pointee = TypeOf(c.pos + 1, k - 1);
                c.pos = k + 1;
                const Val p = ParseUnary(c, env);
                if (p.known && (pointee.size == 1 || pointee.size == 2 || pointee.size == 4))
                {
                    const uint32_t a = static_cast<uint32_t>(p.v);
                    uint32_t word = 0, value = 0;
                    if (a >= mEntry->codeLo && a + pointee.size <= mEntry->codeHi && mEntry->snap->ReadU32(a & ~3u, word))
                    {
                        const uint32_t sh = 32 - 8 * (a & 3) - 8 * pointee.size;
                        if ((a & 3) + pointee.size <= 4)
                        {
                            value = (word >> sh) & (pointee.size == 4 ? 0xFFFFFFFFu : (1u << (8 * pointee.size)) - 1u);
                            return Make(value, pointee.size, pointee.sg);
                        }
                    }
                }
                return Val();
            }
        }
        if (t == "*" || t == "&")   // a memory read or an address: unknown
        {
            ParseUnary(c, env);
            return Val();
        }
        if (t == "(")
        {
            // A cast: type words and stars, `)`, then an operand.
            size_t k = c.pos;
            bool cast = k < c.end && IsType(mTok[k]);
            while (cast && k < c.end && mTok[k] != ")")
            {
                if (!(IsType(mTok[k]) || mTok[k] == "*")) cast = false;
                ++k;
            }
            if (cast && k < c.end)
            {
                const Ty to = TypeOf(c.pos, k);
                c.pos = k + 1;
                return Convert(ParseUnary(c, env), to);
            }
            const Val v = ParseExpr(c, env, 1);
            if (c.pos < c.end && mTok[c.pos] == ")") ++c.pos;
            else mError = "unbalanced parentheses";
            if (c.pos < c.end && mTok[c.pos] == "(")   // (f)(args): an indirect call
            {
                SkipGroup(c);
                return Val();
            }
            return v;
        }
        if (t[0] == '\'')
        {
            int64_t v = 0;
            if (!CharValue(t, v))
            {
                mError = "character constant " + t;
                return Val();
            }
            return Make(static_cast<uint64_t>(v), 4, Signed);
        }
        if (std::isdigit(static_cast<unsigned char>(t[0])))
        {
            char* endp = nullptr;
            const unsigned long long v = std::strtoull(t.c_str(), &endp, 0);
            bool u = false;
            int l = 0;
            for (; endp && *endp; ++endp)
            {
                if (*endp == 'u' || *endp == 'U') u = true;
                else if (*endp == 'l' || *endp == 'L') ++l;
                else
                {
                    mError = "number " + t;
                    break;
                }
            }
            if (v > 0xFFFFFFFFull || l >= 2) return Make(v, 8, u || v > 0x7FFFFFFFFFFFFFFFull ? Unsigned : Signed);
            if (u || v > 0x7FFFFFFFull) return Make(v, 4, Unsigned);
            return Make(v, 4, Signed);
        }
        if (t == "true" || t == "false") return Make(t == "true", 1, Unsigned);
        // A name; followed by `(` it is a call, whose result is unknown, or one of the decompiler's own operators.
        if (c.pos < c.end && mTok[c.pos] == "(")
        {
            if (BuiltinShape(t))
            {
                const std::vector<Val> args = Arguments(c, env);
                Val out;
                if (mError.empty() && Builtin(t, args, out)) return out;
                return Val();
            }
            SkipGroup(c);
            return Val();
        }
        // An element of an array local (`axStack_c [0]`): the engine prints a local as an array when a
        // pointer into it exists, so it is memory something else may write -- a read of it is unknown,
        // as `*p` is.
        if (c.pos < c.end && mTok[c.pos] == "[")
        {
            while (c.pos < c.end && mTok[c.pos] == "[")
            {
                int depth = 0;
                while (c.pos < c.end)
                {
                    if (mTok[c.pos] == "[") ++depth;
                    else if (mTok[c.pos] == "]" && --depth == 0)
                    {
                        ++c.pos;
                        break;
                    }
                    ++c.pos;
                }
            }
            return Val();
        }
        const auto it = env.find(t);
        if (it != env.end()) return it->second;
        {
            // A global of the fixture's code area, `iRam06009024`: its width is the declaration's, which the text does not give for a global.
            // The host's record of the access (Entry::globalSize: address -> bytes, from the fates and the printed accesses) does; the letter
            // gives the sign (`i`, `s`, `c` signed, `u`, `w`, `b` unsigned, `x` none).
            const size_t r = t.find("Ram");
            if (r != std::string::npos && t.size() == r + 11 && mEntry != nullptr && mEntry->readCode && mEntry->snap != nullptr)
            {
                char* endp = nullptr;
                const uint32_t a = static_cast<uint32_t>(std::strtoul(t.c_str() + r + 3, &endp, 16));
                const auto sz = mEntry->globalSize.find(a);
                if (*endp == '\0' && sz != mEntry->globalSize.end() && a >= mEntry->codeLo && a + sz->second <= mEntry->codeHi)
                {
                    uint32_t word = 0;
                    if (mEntry->snap->ReadU32(a & ~3u, word))
                    {
                        const int n = sz->second;
                        if ((a & 3) + n <= 4 && (n == 1 || n == 2 || n == 4))
                        {
                            const uint32_t sh = 32 - 8 * (a & 3) - 8 * n;
                            const uint32_t v = (word >> sh) & (n == 4 ? 0xFFFFFFFFu : (1u << (8 * n)) - 1u);
                            const char c = t[0];
                            const Sign sg = (c == 'i' || c == 's' || c == 'c') ? Signed : (c == 'u' || c == 'w' || c == 'b') ? Unsigned : Unknown;
                            return Make(v, n, sg);
                        }
                    }
                }
            }
        }
        const auto ty = mTypes.find(t);
        {
            Val init;
            if (EntryValue(t, ty != mTypes.end() ? &ty->second : nullptr, init)) return init;
        }
        if (ty != mTypes.end()) return Convert(Unk(4, Unknown), ty->second);   // declared, never assigned
        // A parameter the header does not declare (a fragment): the register's value as an int.
        if (t.compare(0, 6, "param_") == 0)
        {
            const size_t i = static_cast<size_t>(std::atoi(t.c_str() + 6));
            if (i >= 1 && i <= mArgs.size()) return Make(mArgs[i - 1], 4, Signed);
        }
        return Val();
    }

    Val ParseExpr(Cursor& c, const std::map<std::string, Val>& env, int minPrec)
    {
        Val lhs = ParseUnary(c, env);
        while (c.pos < c.end)
        {
            const std::string op = mTok[c.pos];
            const int p = Prec(op);
            if (p < 0 || p < minPrec) break;
            ++c.pos;
            const Val rhs = ParseExpr(c, env, p + 1);
            lhs = Apply(op, lhs, rhs);
        }
        return lhs;
    }

    Val Eval(const Node& n, const std::map<std::string, Val>& env)
    {
        Cursor c{ n.expr, n.exprEnd };
        const Val v = ParseExpr(c, env, 1);
        if (c.pos != c.end && mError.empty()) mError = "text left in an expression";
        for (size_t k = n.expr; k < n.exprEnd && k < mTok.size(); ++k)
            if (mTok[k] == "(" && k > n.expr && (std::isalnum(static_cast<unsigned char>(mTok[k - 1][0])) || mTok[k - 1][0] == '_' || mTok[k - 1] == ")"))
                mCalled = true;   // `f(...)`, `(*f)(...)`: a call in the expression
        return v;
    }

    // The value the run gave a name the code reads without writing it (see Entry), when it can be said.
    bool EntryValue(const std::string& name, const Ty* ty, Val& out) const
    {
        if (mEntry == nullptr) return false;
        if (name.compare(0, 9, "in_stack_") == 0 || IsStackLocal(name))
        {
            if (mEscapeNote) return false;   // a call the text marks may have written it: no longer the start word, nor known
            if (ty != nullptr && ty->size != 4) return false;
            if (mEntry->snap == nullptr) return false;
            int64_t off = 0;
            if (name.compare(0, 9, "in_stack_") == 0)
            {
                if (name.size() != 17) return false;
                char* endp = nullptr;
                off = static_cast<int64_t>(std::strtoul(name.c_str() + 9, &endp, 16));
                if (*endp != '\0') return false;
            }
            else
            {
                if (mAddrTaken.count(name)) return false;
                if (!StackLocalOffset(name, off)) return false;
            }
            uint32_t word = 0;
            if (!mEntry->snap->ReadU32(static_cast<uint32_t>(mEntry->sp + off), word)) return false;
            out = Make(word, 4, Unsigned);
            if (ty != nullptr) out = Convert(out, *ty);
            return true;
        }
        const char* reg = name.compare(0, 3, "in_") == 0 ? name.c_str() + 3 : name.compare(0, 6, "unaff_") == 0 ? name.c_str() + 6 : nullptr;
        if (reg == nullptr) return false;
        if (std::strcmp(reg, "retaddr") == 0) reg = "pr";   // the engine's name for PR on entry
        const auto r = mEntry->regs.find(reg);
        if (r == mEntry->regs.end()) return false;
        out = Make(r->second, 4, Unsigned);
        if (ty != nullptr) out = Convert(out, *ty);
        return true;
    }

    // A stack local (`xStack_c`, `iStack_8`): memory the code can reach through a pointer, which a call may have written.
    // `xStack_8` (8 below the entry r15), `iStack00000000` and `_xStack00000004` (at or above it): the stack offset the name gives.
    static bool StackLocalOffset(const std::string& name, int64_t& off)
    {
        const size_t at = name.find("Stack");
        if (at == std::string::npos) return false;
        size_t i = at + 5;
        const bool below = i < name.size() && name[i] == '_';
        if (below) ++i;
        if (i >= name.size()) return false;
        for (size_t k = i; k < name.size(); ++k)
            if (!std::isxdigit(static_cast<unsigned char>(name[k]))) return false;
        const int64_t v = static_cast<int64_t>(std::strtoul(name.c_str() + i, nullptr, 16));
        off = below ? -v : v;
        return true;
    }
    static bool IsStackLocal(const std::string& name)
    {
        int64_t off = 0;
        return StackLocalOffset(name, off);
    }
    // A call may have written a stack local, and anything whose address the text takes (`&param_5`, a stack parameter whose address
    // reaches the callee: the engine prints the variable, which the callee changed: A3-23).
    void ForgetStackLocals(std::map<std::string, Val>& env)
    {
        for (auto& e : env)
            if (IsStackLocal(e.first) || mAddrTaken.count(e.first)) e.second = Unk(e.second.size, e.second.sg);
        mCalled = false;
    }

    void Exec(std::vector<Frame> frames, std::map<std::string, Val> env, std::vector<Outcome>& out, int depth)
    {
        if (depth > 16)
        {
            mError = "too many unknown branches";
            return;
        }
        while (mError.empty())
        {
            while (!frames.empty() && frames.back().next >= frames.back().seq->size()) frames.pop_back();
            if (frames.empty())
            {
                out.push_back(Outcome());   // fell off the end
                return;
            }
            const Node& n = (*frames.back().seq)[frames.back().next++];
            switch (n.kind)
            {
            case Node::Skip: break;
            case Node::Call: ForgetStackLocals(env); break;
            case Node::Assign:
            {
                const Val v = Eval(n, env);
                if (mCalled) ForgetStackLocals(env);
                const auto ty = mTypes.find(n.var);
                env[n.var] = ty == mTypes.end() ? v : Convert(v, ty->second);
                break;
            }
            case Node::Return:
            {
                Outcome o;
                o.value = n.hasExpr;
                if (n.hasExpr)
                {
                    o.v = Eval(n, env);
                    mCalled = false;
                    if (mRet.size > 0) o.v = Convert(o.v, mRet);
                    if (o.v.known && o.v.size > 4 && mError.empty()) mError = "a 64-bit return, which r0 alone does not hold";
                }
                out.push_back(o);
                return;
            }
            case Node::Break:
                while (!frames.empty())
                {
                    const bool sw = frames.back().sw;
                    frames.pop_back();
                    if (sw) break;
                }
                break;
            case Node::Switch:
            {
                // Where execution starts: the label that matches, else the default's, else after the switch.
                // An unknown selector tries each label and the default, as an unknown condition tries both arms.
                const Val sel = Promote(Eval(n, env));
                if (mCalled) ForgetStackLocals(env);
                const uint64_t mask = Mask(sel.size);
                std::vector<long> starts;   // an index into the body, or -1: past the switch
                long deflt = -1;
                for (const Node::Label& l : n.labels)
                {
                    if (l.isDefault) deflt = static_cast<long>(l.at);
                    else if (!sel.known || (sel.v & mask) == (l.value & mask))
                    {
                        starts.push_back(static_cast<long>(l.at));
                        if (sel.known) break;
                    }
                }
                if (!sel.known || starts.empty()) starts.push_back(deflt);
                for (size_t k = 0; k + 1 < starts.size(); ++k)
                {
                    std::vector<Frame> other = frames;
                    if (starts[k] >= 0) other.push_back({ n.yes.get(), static_cast<size_t>(starts[k]), true });
                    Exec(other, env, out, depth + 1);
                }
                if (starts.back() >= 0) frames.push_back({ n.yes.get(), static_cast<size_t>(starts.back()), true });
                break;
            }
            case Node::If:
            {
                const Val c = Eval(n, env);
                if (mCalled) ForgetStackLocals(env);
                static const Seq empty;
                const Seq* yes = n.yes.get();
                const Seq* no = n.no ? n.no.get() : &empty;
                if (c.known)
                {
                    frames.push_back({ c.v ? yes : no, 0 });
                    break;
                }
                std::vector<Frame> other = frames;
                frames.push_back({ yes, 0 });
                other.push_back({ no, 0 });
                Exec(other, env, out, depth + 1);
                break;
            }
            }
        }
    }

    std::vector<std::string> mTok;
    size_t mPos = 0;
    std::vector<uint32_t> mArgs;
    std::map<std::string, Ty> mTypes;   // the declared type of each parameter and local
    const Entry* mEntry = nullptr;
    std::set<std::string> mAddrTaken;   // names after a `&`
    bool mEscapeNote = false;           // the text carries the engine's note about a frame address in r2
    Ty mRet;                            // the return type
    Seq mBody;
    std::string mError;
    bool mCalled = false;   // the expression last evaluated made a call
};

// A fixture for the call oracle: code from 'base' (entered at 'base'); the trap handler and the
// vector table VBR points to are placed beside it.
struct CallFixture
{
    std::string name;
    uint32_t base;
    std::vector<uint16_t> code;
    // The function's result is not in r0: r0 at its return is scratch, so a text that returns nothing is right.
    // Otherwise r0 is the result (the default: a `return;` from code that leaves a value there is wrong, A3-11).
    bool noResult = false;
};

constexpr uint32_t kRunSentinel = 0x0600FF00u;   // PR on entry: the run ends when it is the next instruction
constexpr uint32_t kRunStack = 0x0600F000u;
constexpr uint32_t kRunVbr = 0x06009800u;        // 256 vectors, all holding kRunHandler
constexpr uint32_t kRunHandler = 0x06009C00u;    // rte ; nop

std::shared_ptr<MemorySnapshot> CallSnapshot(const CallFixture& f, uint64_t serial)
{
    std::vector<uint8_t> hw;
    size_t off = f.base - 0x06000000u;
    for (uint16_t w : f.code)
    {
        dts::Put16(hw, off, w);
        off += 2;
    }
    for (uint32_t vec = 0; vec < 0x100; ++vec) dts::Put32(hw, kRunVbr - 0x06000000u + vec * 4, kRunHandler);
    dts::Put16(hw, kRunHandler - 0x06000000u, 0x002B);        // rte
    dts::Put16(hw, kRunHandler - 0x06000000u + 2, 0x0009);    //   nop
    dts::Put16(hw, kRunSentinel - 0x06000000u, 0x0009);       // the emulator decodes the instruction it stops at
    return MakeSnapshot(hw, serial);
}

// The registers on entry to a run: r0-r15 distinct (0x1000 + 0x111 i), r4..r7 the arguments, r15 the stack, PR the sentinel,
// VBR the vector table, SR, GBR, MACH and MACL zero. The evaluator reads `in_` and `unaff_` names from the same table.
std::map<std::string, uint32_t> RunEntryRegisters(const std::vector<uint32_t>& args)
{
    std::map<std::string, uint32_t> regs;
    for (int i = 0; i < 16; ++i) regs["r" + std::to_string(i)] = 0x1000u + 0x111u * i;
    for (size_t i = 0; i < args.size() && i < 4; ++i) regs["r" + std::to_string(4 + i)] = args[i];
    regs["r15"] = kRunStack;
    regs["pr"] = kRunSentinel;
    regs["vbr"] = kRunVbr;
    regs["sr"] = 0;
    regs["gbr"] = 0;
    regs["mach"] = 0;
    regs["macl"] = 0;
    return regs;
}

// The function run with r4..r7 = args and the other registers set to distinct values; r0 at its return.
bool CallRunResult(const std::shared_ptr<MemorySnapshot>& snap, const CallFixture& f, const std::vector<uint32_t>& args,
                   uint32_t& r0)
{
    try
    {
        RunMachine m(snap);
        for (const auto& kv : RunEntryRegisters(args)) m.Set(kv.first.c_str(), kv.second);
        if (!m.Run(f.base, kRunSentinel)) return false;
        r0 = m.Get("r0");
        return true;
    }
    catch (ghidra::LowlevelError& e)
    {
        std::printf("  oracle %s: the emulator stopped: %s\n", f.name.c_str(), e.explain.c_str());
        return false;
    }
}

// Whether the pseudocode may return 'value' for these inputs: some path returns a value it cannot
// know, or returns exactly that (as many low bytes as the return type has). A path that returns nothing
// (`return;`, or falling off the end) agrees only when r0 is no result at all ('resultInR0' false): the
// code leaves its value there, and a text that states none dropped it. 'why' says what it does return when not.
bool MayReturn(const std::string& text, const std::vector<uint32_t>& args, uint32_t value, std::string& why,
               bool resultInR0 = true, const PseudoEval::Entry* entry = nullptr)
{
    PseudoEval eval(text, args);
    eval.SetEntry(entry);
    const std::vector<PseudoEval::Outcome> outs = eval.Run();
    if (!eval.Error().empty())
    {
        why = "cannot evaluate the pseudocode (" + eval.Error() + ")";
        return false;
    }
    char buf[32];
    for (const PseudoEval::Outcome& o : outs)
    {
        if (!o.value)
        {
            if (!resultInR0) return true;
            why += " nothing";
            continue;
        }
        if (!o.v.known) return true;
        const uint32_t mask = o.v.size >= 4 ? 0xFFFFFFFFu : (1u << (8 * o.v.size)) - 1u;
        if ((static_cast<uint32_t>(o.v.v) & mask) == (value & mask)) return true;
        std::snprintf(buf, sizeof buf, " %08X", static_cast<uint32_t>(o.v.v));
        why += buf;
    }
    why = "returns" + why;
    return outs.empty();
}

// The code run on the emulator for each input, against the pseudocode evaluated for the same.
// 'voidOk': a text that returns nothing is not a disagreement. For a function whose signature is withheld, which says nothing about what
// it returns, and whose text need not either: the engine returns r0 only where the value has no other use (upstream's heuristic, which the
// nop in rts's slot used to hide, A2-31); a text that returns a value still has to be the right one.
bool PseudoAgreesWithRun(const DecompileResult& r, const std::shared_ptr<MemorySnapshot>& snap, const CallFixture& f,
                         const std::vector<std::vector<uint32_t>>& inputs, bool voidOk = false)
{
    bool ok = true;
    for (const std::vector<uint32_t>& args : inputs)
    {
        uint32_t ran = 0;
        if (!CallRunResult(snap, f, args, ran))
        {
            std::printf("  oracle %s: the code did not return on the emulator\n", f.name.c_str());
            return false;
        }
        std::string why;
        PseudoEval::Entry entry;
        entry.regs = RunEntryRegisters(args);
        entry.snap = snap;
        entry.sp = kRunStack;
        if (!MayReturn(r.Text(), args, ran, why, !f.noResult && !voidOk, &entry))
        {
            std::printf("  oracle %s: the code returned %08X for r4=%08X, the pseudocode %s\n", f.name.c_str(), ran,
                        args.empty() ? 0u : args[0], why.c_str());
            ok = false;
        }
    }
    return ok;
}

// ===== Area 4, round 3: a jump table bounded only by a signed compare (A4-7) ============================
// `cmp/gt` or `cmp/ge` against the last index sends the larger indices to the default and lets every
// negative one through: on the hardware index -1 reads the word before the table and runs whatever
// that points at. The engine used to assume the switch value non-negative whenever the guards left a
// range too big for a table (upstream's heuristic) and printed the three cases and a default, with the
// signature lent. Patch 0029 drops the assumption: the range keeps the negatives, no table can fill
// it, and the jump is reported as unresolved, so nothing wrong is shown and nothing is lent.
// A guard that excludes the negatives (an unsigned compare, cmp/pz, a zero-extended index) leaves a
// small range and the switch is recovered as before.
void TestSignedGuardTable()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 500;
    const uint32_t base = 0x06009000u;
    // mov.l @(lit),r2 ; mov #bound,r1 ; <cmp> ; bt def ; shll2 r4 ; mova table,r0 ; mov.l @(r0,r4),r1 ; jmp @r1 ;
    // nop ; nop ; lit 0x06009034 (case 2: the word before the table) ; table (3 entries) ; def: rts ; mov r2,r0 ;
    // cases 0 to 2: rts ; mov #n,r0.
    auto oneGuard = [](uint16_t bound, uint16_t cmp) {
        return std::vector<uint16_t>{ 0xD204, bound, cmp, 0x890F, 0x4408, 0xC703, 0x014E, 0x412B, 0x0009, 0x0009, 0x0600, 0x9034,
                                      0x0600, 0x902C, 0x0600, 0x9030, 0x0600, 0x9034, 0x0009, 0x0009, 0x000B, 0x6023, 0x000B, 0xE000,
                                      0x000B, 0xE001, 0x000B, 0xE002 };
    };
    // cmp/pz r4 ; bf def ; cmp/gt r1,r4 ; bt def: both signs bounded.
    const std::vector<uint16_t> twoGuards = { 0xD205, 0xE102, 0x4411, 0x8B0F, 0x3417, 0x890D, 0x4408, 0xC703, 0x014E, 0x412B, 0x0009, 0x0009,
                                              0x0600, 0x9034, 0x0600, 0x902C, 0x0600, 0x9030, 0x0600, 0x9034, 0x000B, 0x6023, 0x000B, 0xE000,
                                              0x000B, 0xE001, 0x000B, 0xE002 };
    // extu.b r4,r4 first: a byte cannot be negative, so the signed bound is the whole bound.
    const std::vector<uint16_t> zeroExtended = { 0x644C, 0xD204, 0xE102, 0x3417, 0x890C, 0x4408, 0xC702, 0x014E, 0x412B, 0x0009, 0x0600, 0x9030,
                                                 0x0600, 0x9028, 0x0600, 0x902C, 0x0600, 0x9030, 0x000B, 0x6023, 0x000B, 0xE000, 0x000B, 0xE001,
                                                 0x000B, 0xE002 };
    // exts.b r4,r4 first: the index is the low byte as a signed number, so the signed bound lets 0x80 to 0xFF through.
    std::vector<uint16_t> signExtended = zeroExtended;
    signExtended[0] = 0x644E;
    struct Case
    {
        const char* name;
        std::vector<uint16_t> code;
        bool recovered;   // the switch is shown and the signature lent
        bool zeroExtended = false;
        bool truncated = false;   // not recovered: a switch over the byte, said to miss cases (the engine's own sanity check)
    };
    const std::vector<Case> cases = {
        { "table, cmp/gt only", oneGuard(0xE102, 0x3417), false },
        { "table, cmp/ge only", oneGuard(0xE103, 0x3413), false },
        { "table, cmp/hi (control)", oneGuard(0xE102, 0x3416), true },
        { "table, cmp/hs (control)", oneGuard(0xE103, 0x3412), true },
        { "table, cmp/pz and cmp/gt (control)", twoGuards, true },
        { "table, zero-extended index and cmp/gt (control)", zeroExtended, true, true },
        { "table, sign-extended index and cmp/gt", signExtended, false, false, true },
    };
    // The oracle reads a switch (it could not before) and can fail on the text this engine printed before
    // patch 0029: three cases and a default, which the hardware contradicts for index -1 (it returns 2).
    {
        std::string why;
        const char* const old = "int f(int param_1)\n{\n  switch(param_1) {\n  case 0:\n    return 0;\n  case 1:\n    return 1;\n"
                                "  case 2:\n    return 2;\n  default:\n    return 0x6009034;\n  }\n}";
        CHECK(MayReturn(old, { 1 }, 1, why) && MayReturn(old, { 5 }, 0x6009034, why));
        CHECK(!MayReturn(old, { 0xFFFFFFFFu }, 2, why) && Has(why, "6009034"));
        CHECK(MayReturn("int f(int param_1)\n{\n  int x;\n  x = 0;\n  switch(param_1) {\n  case 1:\n    x = 7;\n  case 2:\n"
                        "    x = x + 1;\n    break;\n  case 3:\n    x = 9;\n  }\n  return x;\n}",
                        { 1 }, 8, why));   // falls through from case 1 to case 2, then leaves
        CHECK(!MayReturn("int f(int param_1)\n{\n  switch(param_1) {\n  case 0xffffffff:\n    return 5;\n  default:\n"
                         "    return 6;\n  }\n}", { 0xFFFFFFFFu }, 6, why));
        // A break inside an if leaves the switch, not only the if.
        const char* const nested = "int f(int param_1)\n{\n  int x;\n  x = 0;\n  switch(param_1) {\n  case 1:\n    if (param_1 == 1) {\n"
                                   "      break;\n    }\n    x = 7;\n  case 2:\n    x = 9;\n  }\n  return x;\n}";
        CHECK(MayReturn(nested, { 1 }, 0, why) && !MayReturn(nested, { 1 }, 9, why));
        CHECK(MayReturn(nested, { 2 }, 9, why) && MayReturn(nested, { 3 }, 0, why));
    }
    // r4 = 3 is past the bound, 0xFFFFFFFF is -1, 0x80000000 the most negative.
    const std::vector<std::vector<uint32_t>> indices = { { 0 }, { 1 }, { 2 }, { 3 }, { 0xFFFFFFFFu }, { 0x80000000u }, { 0x7FFFFFFFu } };
    // The evaluator takes the parameter as the whole register, so the byte-wide one is fed bytes and a -1.
    const std::vector<std::vector<uint32_t>> byteIndices = { { 0 }, { 1 }, { 2 }, { 3 }, { 0xFFu }, { 0xFFFFFFFFu } };
    for (const Case& c : cases)
    {
        const CallFixture f = { c.name, base, c.code };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(base));
        Print(c.name, r);
        CHECK(r.status == DecompileStatus::Ok);
        // The text is checked against the emulator, index by index (negatives included). (A truncated switch is on a
        // signed char, whose case labels the evaluator does not read.)
        if (!c.truncated) CHECK(PseudoAgreesWithRun(r, snap, f, c.zeroExtended ? byteIndices : indices));
        CHECK(Has(r.Text(), "switch(") == (c.recovered || c.truncated));
        CHECK(r.prototype.known == c.recovered);
        CHECK(r.prototypeWithheld.empty() == c.recovered);
        uint32_t minusOne = 0;
        CHECK(CallRunResult(snap, f, { 0xFFFFFFFFu }, minusOne));
        if (c.recovered)
        {
            // The negative indices go to the default, the literal r2 holds.
            CHECK(minusOne == (c.zeroExtended ? 0x06009030u : 0x06009034u));
        }
        else
        {
            // Index -1 reads the word before the table, case 2's address; the guard does not exclude it. Said: the
            // jump is not resolved, the larger indices still reach the default through the engine's own test.
            CHECK(minusOne == 2);
            if (c.truncated)
            {
                // The byte is the switch variable and the table is cut at the words that are code: said, and not lent.
                CHECK(WarnsAbout(r, "cases may be missing") && Has(r.prototypeWithheld, "jump table"));
            }
            else
            {
                CHECK(WarnsAbout(r, "Could not recover jumptable") && WarnsAbout(r, "Treating indirect jump as call"));
                CHECK(Has(r.prototypeWithheld, "indirect"));
                CHECK(!Has(r.Text(), "case ") && !Has(r.Text(), "default"));
            }
            // Nothing is lent, so no caller is told it can pass any index.
            DecompileRequest q = Request(base);
            CHECK(!d.Decompile(snap, q).prototype.known);
        }
    }
}

// ===== Area 2, round 3: an access that runs past the top of memory (A2-11) ================================
// After constant propagation `mov.w r1,@-r4` on the path where `tst` proved r4 = 0 leaves r4 = 0xFFFFFFFE,
// and the longword store at it is four bytes past 0xFFFFFFFF. The engine could not make a Varnode for it
// ("Trying to construct memory range beyond end of address space") and failed the whole function. Patch
// 0031 leaves such a LOAD or STORE alone: the function decompiles, the access is printed through its
// (constant) pointer, and the ledger lists it once. On the hardware that path is an address error.
void TestAccessPastSpaceEnd()
{
    Sh2Decompiler d(gConfig);
    std::vector<uint8_t> hw;
    // tst r4,r4 ; bf/s L ; <step r4 down> ; <access at @r4> ; L: rts ; nop
    auto fn = [&](uint32_t base, uint16_t step, uint16_t access) {
        Place(hw, base, { 0x2448, 0x8F01, step, access, 0x000B, 0x0009 });
    };
    const uint32_t store4 = 0x06009000u, load4 = 0x06009020u, store2 = 0x06009040u, last4 = 0x06009060u, store1 = 0x06009080u;
    fn(store4, 0x2415, 0x2432);   // mov.w r1,@-r4 ; mov.l r3,@r4      past the top by 2 bytes
    fn(load4, 0x2415, 0x6042);    // mov.w r1,@-r4 ; mov.l @r4,r0
    fn(store2, 0x2414, 0x2431);   // mov.b r1,@-r4 ; mov.w r3,@r4      past the top by 1 byte
    fn(last4, 0x2416, 0x2432);    // mov.l r1,@-r4 ; mov.l r3,@r4      the last longword: inside, as before
    fn(store1, 0x2414, 0x2430);   // mov.b r1,@-r4 ; mov.b r3,@r4      the last byte: inside, as before
    auto snap = MakeSnapshot(hw);
    auto run = [&](const char* label, uint32_t base) {
        const DecompileResult r = d.Decompile(snap, Request(base));
        Print(label, r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(r.error.empty());
        CHECK(ReferenceOk(d, snap, Request(base), r));
        return r;
    };
    // The access past the top is in the ledger once, through the pointer the engine knows only as a constant.
    auto listed = [](const DecompileResult& r, uint32_t insn, bool write, uint8_t size, uint32_t address) {
        int n = 0;
        for (const MemoryAccess& m : r.accesses)
            if (m.insn == insn && m.write == write && m.size == size && !m.direct && m.address == address && m.printed == 1) ++n;
        return n == 1;
    };
    DecompileResult r = run("longword store past the top", store4);
    CHECK(Has(r.Text(), "*(xunknown4 *)0xfffffffe = in_r3;") && listed(r, store4 + 6, true, 4, 0xFFFFFFFEu));
    r = run("longword load past the top", load4);
    CHECK(Has(r.Text(), "xVar1 = *(xunknown4 *)0xfffffffe;") && listed(r, load4 + 6, false, 4, 0xFFFFFFFEu));
    r = run("word store past the top by one byte", store2);
    CHECK(Has(r.Text(), "*(xunknown2 *)0xffffffff = in_r3;") && listed(r, store2 + 6, true, 2, 0xFFFFFFFFu));
    // Inside the space, at its very end: the same constant-address treatment as any other.
    r = run("longword store at the last longword", last4);
    CHECK(Has(r.Text(), "Ramfffffffc = in_r3;") && Ledger(r, { Wr(0, 4), Wr(0xFFFFFFFCu, 4) }));
    r = run("byte store at the last byte", store1);
    CHECK(Has(r.Text(), "Ramffffffff = in_r3;") && Ledger(r, { Wr(0, 1), Wr(0xFFFFFFFFu, 1) }));
}

// Ledger A2-9 (trapa) and A2-10 (what a call leaves in r4-r7, T, MACH and MACL), both from the
// round-2 review of area 2. The handler of every trap is `rte ; nop` (CallSnapshot).
void TestCallsAndTraps()
{
    Sh2Decompiler d(gConfig);

    // The oracle can fail: the texts the engine printed for the review's fixtures before the fix,
    // evaluated, state values the code does not return; an unknown value agrees with any run.
    {
        std::string why;
        CHECK(!MayReturn("int f(void)\n{\n  int xVar1;\n  xVar1 = 5;\n  func_0x06009020();\n  return xVar1;\n}", {}, 7, why));
        CHECK(MayReturn("int f(void)\n{\n  int xVar1;\n  xVar1 = 5;\n  func_0x06009020();\n  return xVar1;\n}", {}, 5, why));
        CHECK(!MayReturn("int f(void)\n{\n  uint1 uVar1;\n  uVar1 = 1;\n  func_0x06009020();\n  if ((uVar1 & 1) == 1) {\n"
                         "    return 1;\n  }\n  return 0;\n}", {}, 0, why));
        CHECK(!MayReturn("int f(void)\n{\n  int4 in_vbr;\n  (**(code **)(in_vbr + 4))();\n  return 0x6009006;\n}", {}, 0x1234, why));
        CHECK(MayReturn("int f(int param_1)\n{\n  (**(code **)(in_vbr + 4))();\n  return param_1;\n}", { 0x1234 }, 0x1234, why));
        CHECK(MayReturn("int f(void)\n{\n  int extraout_r7;\n  func_0x06009020();\n  return extraout_r7;\n}", {}, 7, why));
        CHECK(MayReturn("int f(int param_1)\n{\n  if (param_1 < 4) {\n    return 1;\n  }\n  return 2;\n}", { 1 }, 1, why));
        CHECK(!MayReturn("int f(int param_1)\n{\n  if (param_1 < 4) {\n    return 1;\n  }\n  return 2;\n}", { 9 }, 1, why));
        CHECK(!MayReturn("int f(int param_1)\n{\n  while (param_1 != 0) {\n    param_1 = param_1 - 1;\n  }\n  return 0;\n}", { 1 }, 0, why));
    }

    // ---- Round 5: the evaluator knows what the run gave the code on entry ------------------------------
    // `in_` and `unaff_` names are the registers' starting values, `in_stack_` and a local nothing assigned the words the stack
    // held, and `extraout_` (what a call returned) stays unknown. Before, all of them were unknown and agreed with any run, so a
    // text that named the wrong register, or read the slot a callee pushed, was accepted (A3-15).
    {
        std::string why;
        CallFixture dummy = { "entry", 0x06009000u, { 0x000B, 0x0009 } };
        PseudoEval::Entry e;
        e.regs = RunEntryRegisters({ 0x5555u });
        e.snap = CallSnapshot(dummy, 99);
        e.sp = kRunStack;
        const std::string r8 = "uint f(void)\n{\n  uint unaff_r8;\n  return unaff_r8;\n}";
        CHECK(MayReturn(r8, { 0x5555u }, 0x1888, why, true, &e) && !MayReturn(r8, { 0x5555u }, 0x1999, why, true, &e));
        CHECK(MayReturn(r8, { 0x5555u }, 0x1999, why));   // no entry state: unknown, agrees with any
        const std::string r4 = "uint f(void)\n{\n  uint in_r4;\n  return in_r4 + 1;\n}";
        CHECK(MayReturn(r4, { 0x5555u }, 0x5556, why, true, &e) && !MayReturn(r4, { 0x5555u }, 0x1445, why, true, &e));
        const std::string sr = "uint f(void)\n{\n  uint in_sr;\n  return in_sr | 0xf0;\n}";
        CHECK(MayReturn(sr, {}, 0xF0, why, true, &e) && !MayReturn(sr, {}, 0, why, true, &e));
        const std::string retaddr = "uint f(void)\n{\n  uint unaff_retaddr;\n  return unaff_retaddr;\n}";
        CHECK(MayReturn(retaddr, {}, kRunSentinel, why, true, &e) && !MayReturn(retaddr, {}, 1, why, true, &e));
        // The slot a callee pushed is not the word the run started with.
        const std::string local = "uint f(void)\n{\n  uint xStack_8;\n  func_0x06009020();\n  return xStack_8;\n}";
        CHECK(MayReturn(local, {}, 0, why, true, &e) && !MayReturn(local, {}, 0x22222222, why, true, &e));
        CHECK(MayReturn(local, {}, 0x22222222, why));
        // ...but a local the text assigned is unknown after a call, and one whose address is taken may have been written by it.
        const std::string assigned = "uint f(void)\n{\n  uint xStack_8;\n  xStack_8 = 5;\n  func_0x06009020();\n  return xStack_8;\n}";
        CHECK(MayReturn(assigned, {}, 7, why, true, &e));
        const std::string taken = "uint f(void)\n{\n  uint xStack_8;\n  func_0x06009020(&xStack_8);\n  return xStack_8;\n}";
        CHECK(MayReturn(taken, {}, 7, why, true, &e));
        const std::string pushed = "uint f(void)\n{\n  return in_stack_00000004;\n}";
        CHECK(MayReturn(pushed, {}, 0, why, true, &e) && !MayReturn(pushed, {}, 9, why, true, &e));
        const std::string out = "uint f(void)\n{\n  uint extraout_r7;\n  func_0x06009020();\n  return extraout_r7;\n}";
        CHECK(MayReturn(out, {}, 0x1777, why, true, &e) && MayReturn(out, {}, 5, why, true, &e));   // what a call returned: unknown
    }

    // ---- A3-11: the evaluator reads what the text says ----------------------------------------
    // Text it used to accept for a run it does not agree with ...
    {
        std::string why;
        // A `return;` states no value: right only for a function whose r0 is no result (the fixture says so).
        const std::string bare = "void f(void)\n{\n  func_0x06009020();\n  return;\n}";
        why.clear();
        CHECK(!MayReturn(bare, {}, 7, why) && Has(why, "nothing"));
        CHECK(MayReturn(bare, {}, 7, why, false));
        CHECK(!MayReturn("void f(void)\n{\n  func_0x06009020();\n}", {}, 7, why));   // falling off the end is the same
        CHECK(!MayReturn("void f(int param_1)\n{\n  if (param_1 != 0) {\n    return;\n  }\n  return;\n}", { 1 }, 7, why));
        // true and false are 1 and 0, not names of unknown value.
        CHECK(!MayReturn("bool f(void)\n{\n  return false;\n}", {}, 1, why));
        CHECK(MayReturn("bool f(void)\n{\n  return false;\n}", {}, 0, why));
        CHECK(MayReturn("bool f(void)\n{\n  return true;\n}", {}, 1, why) && !MayReturn("bool f(void)\n{\n  return true;\n}", {}, 0, why));
        // A parameter assigned to holds the new value.
        const std::string reassigned = "int f(int param_1)\n{\n  param_1 = param_1 + 1;\n  return param_1;\n}";
        CHECK(!MayReturn(reassigned, { 5 }, 5, why) && MayReturn(reassigned, { 5 }, 6, why));
        // A cast has a width: (char) keeps a byte and extends its sign, (uint1) keeps it and extends none.
        const std::string viaChar = "int f(int param_1)\n{\n  return (int)(char)param_1;\n}";
        CHECK(!MayReturn(viaChar, { 0xFF }, 0xFF, why) && MayReturn(viaChar, { 0xFF }, 0xFFFFFFFFu, why));
        const std::string viaByte = "int f(int param_1)\n{\n  return (int)(uint1)param_1;\n}";
        CHECK(MayReturn(viaByte, { 0x1FF }, 0xFF, why) && !MayReturn(viaByte, { 0x1FF }, 0xFFFFFFFFu, why));
        CHECK(MayReturn("int f(int param_1)\n{\n  return (short)param_1;\n}", { 0x18000 }, 0xFFFF8000u, why));
        // The operators the decompiler writes like calls are evaluated: CONCAT, SUB, ZEXT, SEXT.
        const std::string concat = "uint f(uint param_1)\n{\n  return CONCAT31((int3)(param_1 >> 8),(char)param_1);\n}";
        CHECK(MayReturn(concat, { 0x12345678 }, 0x12345678, why) && !MayReturn(concat, { 0x12345678 }, 0x12345600, why));
        const std::string sub = "uint f(uint param_1)\n{\n  return SUB41(param_1,0) + 1;\n}";
        CHECK(MayReturn(sub, { 0x12345678 }, 0x79, why) && !MayReturn(sub, { 0x12345678 }, 0x12345679, why));
        CHECK(MayReturn("uint f(uint param_1)\n{\n  return SUB42(param_1,2);\n}", { 0x12345678 }, 0x1234, why));
        CHECK(MayReturn("uint f(uint param_1)\n{\n  return ZEXT24(SUB42(param_1,0));\n}", { 0x12348000 }, 0x8000, why));
        CHECK(MayReturn("int f(uint param_1)\n{\n  return SEXT24(SUB42(param_1,0));\n}", { 0x12348000 }, 0xFFFF8000u, why));
        // ...what it has no rule for is an error, not an unknown value.
        why.clear();
        CHECK(!MayReturn("uint f(uint param_1)\n{\n  return FROBNICATE4(param_1);\n}", { 1 }, 1, why) && Has(why, "cannot evaluate"));
        why.clear();
        CHECK(!MayReturn("uint f(uint param_1)\n{\n  return CONCAT55(param_1,param_1);\n}", { 1 }, 1, why) && Has(why, "cannot evaluate"));
        why.clear();
        CHECK(!MayReturn("ulonglong f(uint param_1)\n{\n  return 1;\n}", { 1 }, 1, why) && Has(why, "64-bit"));
        why.clear();
        CHECK(!MayReturn("int f(int param_1)\n{\n  param_1 += 1;\n  return param_1;\n}", { 1 }, 2, why) && Has(why, "an assignment this evaluator does not read"));
        // ...and text it used to reject. A parameter assigned under an if, the other path reading it as it was:
        const std::string underIf = "int4 f(int4 param_1,int4 param_2)\n{\n  if (param_2 != 0) {\n    param_1 = param_1 + 1;\n  }\n"
                                    "  return param_1 + 1;\n}";
        CHECK(MayReturn(underIf, { 5, 1 }, 7, why) && MayReturn(underIf, { 5, 0 }, 6, why) && !MayReturn(underIf, { 5, 1 }, 6, why));
        // >> reads a uint4 as unsigned and an int4 as signed
        const std::string shr = "uint4 f(uint4 param_1)\n{\n  return param_1 >> 1;\n}";
        CHECK(MayReturn(shr, { 0x80000000u }, 0x40000000, why) && !MayReturn(shr, { 0x80000000u }, 0xC0000000u, why));
        const std::string sar = "int4 f(int4 param_1)\n{\n  return param_1 >> 1;\n}";
        CHECK(MayReturn(sar, { 0x80000000u }, 0xC0000000u, why) && !MayReturn(sar, { 0x80000000u }, 0x40000000, why));
        // < reads its operands as the declarations say
        const std::string ult = "uint4 f(uint4 param_1)\n{\n  if (param_1 < 3) {\n    return 1;\n  }\n  return 0;\n}";
        CHECK(MayReturn(ult, { 0xFFFFFFFFu }, 0, why) && !MayReturn(ult, { 0xFFFFFFFFu }, 1, why));
        const std::string slt = "int4 f(int4 param_1)\n{\n  if (param_1 < 3) {\n    return 1;\n  }\n  return 0;\n}";
        CHECK(MayReturn(slt, { 0xFFFFFFFFu }, 1, why) && !MayReturn(slt, { 0xFFFFFFFFu }, 0, why));
        // ...and where a value has no sign the two readings disagree, the answer is not known: any run agrees.
        const std::string xlt = "xunknown4 f(xunknown4 param_1)\n{\n  if (param_1 < 3) {\n    return 1;\n  }\n  return 0;\n}";
        CHECK(MayReturn(xlt, { 0xFFFFFFFFu }, 1, why) && MayReturn(xlt, { 0xFFFFFFFFu }, 0, why));
        CHECK(!MayReturn(xlt, { 0 }, 0, why) && MayReturn(xlt, { 0 }, 1, why));   // small: both readings give 1
        // A 64-bit product's high word (dmuls.l): (int4)((uint8)((int8)b * (int8)a) >> 0x20)
        const std::string hi = "int4 f(int4 param_1,int4 param_2)\n{\n  return (int4)((uint8)((int8)param_2 * (int8)param_1) >> 0x20);\n}";
        CHECK(MayReturn(hi, { 0x10000, 0x10000 }, 1, why) && !MayReturn(hi, { 0x10000, 0x10000 }, 0, why));
        CHECK(MayReturn(hi, { 0xFFFFFFFFu, 2 }, 0xFFFFFFFFu, why));
        // A narrow parameter is the register's low byte; a `char` case label is a character.
        CHECK(MayReturn("int f(char param_1)\n{\n  return param_1;\n}", { 0x1FF }, 0xFFFFFFFFu, why));
        CHECK(!MayReturn("int f(char param_1)\n{\n  return param_1;\n}", { 0x1FF }, 0x1FF, why));
        const std::string sw = "int f(char param_1)\n{\n  switch(param_1) {\n  case '\\0':\n    return 7;\n  case '\\x02':\n    return 9;\n"
                               "  case '\\xff':\n    return 4;\n  default:\n    return 5;\n  }\n}";
        CHECK(MayReturn(sw, { 2 }, 9, why) && !MayReturn(sw, { 2 }, 5, why) && MayReturn(sw, { 0xFF }, 4, why) && MayReturn(sw, { 0x1FF }, 4, why));
        CHECK(MayReturn(sw, { 0 }, 7, why) && MayReturn(sw, { 3 }, 5, why));
        // / and % read their operands as the declarations say: -7 / 2 is -3 signed, 0x7FFFFFFC unsigned
        const std::string sdiv = "int f(int param_1)\n{\n  return param_1 / 2;\n}", udiv = "uint f(uint param_1)\n{\n  return param_1 / 2;\n}";
        CHECK(MayReturn(sdiv, { 0xFFFFFFF9u }, 0xFFFFFFFDu, why) && !MayReturn(sdiv, { 0xFFFFFFF9u }, 0x7FFFFFFCu, why));
        CHECK(MayReturn(udiv, { 0xFFFFFFF9u }, 0x7FFFFFFCu, why) && !MayReturn(udiv, { 0xFFFFFFF9u }, 0xFFFFFFFDu, why));
        CHECK(MayReturn("int f(int param_1)\n{\n  return param_1 % 4;\n}", { 0xFFFFFFF9u }, 0xFFFFFFFDu, why));
        CHECK(MayReturn("uint f(uint param_1)\n{\n  return param_1 % 4;\n}", { 0xFFFFFFF9u }, 1, why) &&
              !MayReturn("uint f(uint param_1)\n{\n  return param_1 % 4;\n}", { 0xFFFFFFF9u }, 0xFFFFFFFDu, why));
        // A stack local is memory a call may have written: the engine keeps one a call could reach (A3-9); a register local is not.
        const std::string stackLocal = "int f(void)\n{\n  int xStack_c;\n  xStack_c = 5;\n  func_0x06009020();\n  return xStack_c;\n}";
        CHECK(MayReturn(stackLocal, {}, 7, why) && MayReturn(stackLocal, {}, 5, why));
        CHECK(!MayReturn("int f(void)\n{\n  int xStack_c;\n  xStack_c = 5;\n  return xStack_c;\n}", {}, 7, why));
        CHECK(!MayReturn("int f(void)\n{\n  int xVar1;\n  xVar1 = 5;\n  func_0x06009020();\n  return xVar1;\n}", {}, 7, why));
        // A return of a narrow type compares as many low bytes as it has.
        CHECK(MayReturn("uint1 f(uint param_1)\n{\n  return (uint1)param_1;\n}", { 0x1FF }, 0xFFFFFFFFu, why));
        CHECK(!MayReturn("uint1 f(uint param_1)\n{\n  return (uint1)param_1;\n}", { 0x1FF }, 0xFE, why));
    }

    const std::vector<std::vector<uint32_t>> inputs = { { 0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u },
                                                        { 0x00000000u, 0x00000001u, 0x7FFFFFFFu, 0x80000000u } };
    uint64_t serial = 100;
    // Decompile, require the oracle's agreement, and return the result for the text checks.
    auto run = [&](const CallFixture& f, const std::vector<std::vector<uint32_t>>* with = nullptr) {
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(PseudoAgreesWithRun(r, snap, f, with ? *with : inputs, !r.prototype.known));
        return r;
    };
    const uint32_t base = 0x06009000u;
    // The ledger's callee is at 0x06009020 (word 16): rts, then its delay slot's instruction.
    auto withCallee = [&](std::vector<uint16_t> caller, std::vector<uint16_t> callee) {
        caller.resize(16, 0x0009);
        caller.insert(caller.end(), callee.begin(), callee.end());
        return caller;
    };

    // ---- A3-11: the decompiler's own texts the evaluator used to reject (each run on the emulator) ----
    // The engine's right output, against the code's run: a parameter reassigned under an if, a logical
    // right shift, an unsigned compare (cmp/hs and cmp/hi), the high word of a 64-bit product.
    {
        const std::vector<std::vector<uint32_t>> pair = { { 5, 0 }, { 5, 1 }, { 0, 1 } };
        const DecompileResult r1 = run({ "parameter reassigned under an if", base, { 0x2558, 0x8900, 0x7401, 0x7401, 0x000B, 0x6043 } }, &pair);
        CHECK(Has(r1.Text(), "param_1 = param_1 + 1;"));
        const std::vector<std::vector<uint32_t>> shifted = { { 0x80000000u }, { 3 }, { 0xFFFFFFFFu } };
        const DecompileResult r2 = run({ "shlr on a uint4", base, { 0x4401, 0x000B, 0x6043 } }, &shifted);
        CHECK(Has(r2.Text(), "param_1 >> 1"));
        const std::vector<std::vector<uint32_t>> compared = { { 0 }, { 2 }, { 3 }, { 0xFFFFFFFFu }, { 0x80000000u } };
        const DecompileResult r3 = run({ "cmp/hs against 3", base, { 0xE103, 0x3412, 0x8901, 0x000B, 0xE001, 0x000B, 0xE000 } }, &compared);
        CHECK(Has(r3.Text(), "< 3"));
        const DecompileResult r4 = run({ "cmp/hi against 3", base, { 0xE103, 0x3416, 0x8901, 0x000B, 0xE001, 0x000B, 0xE000 } }, &compared);
        CHECK(Has(r4.Text(), "< 4") || Has(r4.Text(), "< 3"));
        const std::vector<std::vector<uint32_t>> products = { { 0x10000, 0x10000 }, { 0xFFFFFFFFu, 2 }, { 3, 5 } };
        run({ "dmuls.l high word", base, { 0x354D, 0x000B, 0x000A } }, &products);
    }

    // ---- A2-9: trapa -------------------------------------------------------------------------
    // The 8 bytes it pushes are popped by the handler's rte, so every stack access after it is
    // where the code put it. a: a register saved before and restored after, with PR.
    {
        const CallFixture a = { "trapa, saved register", base, { 0x4F22, 0x2F46, 0xC301, 0x60F6, 0x4F26, 0x000B, 0x0009 } };
        const DecompileResult r = run(a);
        CHECK(Has(r.Text(), "return param_1;"));
        CHECK(Has(r.Text(), "(**(code **)(unaff_vbr + 4))();"));   // the trap is still a call through the vector
        CHECK(!Has(r.Text(), "0x6009006"));
    }
    {
        const CallFixture b = { "trapa, word at r15", base, { 0xC320, 0x60F2, 0x000B, 0x0009 } };
        const DecompileResult r = run(b);
        CHECK(Has(r.Text(), "return in_stack_00000000;"));   // the caller's word, not the address trapa pushed
        const CallFixture c = { "trapa, word at r15+4", base, { 0xC320, 0x50F1, 0x000B, 0x0009 } };
        const DecompileResult rc = run(c);
        CHECK(Has(rc.Text(), "return in_stack_00000004;"));   // not the SR it pushed (`return in_sr;`)
        // A word the code wrote itself, round a trap that reads nothing of its own.
        const CallFixture w = { "trapa, local", base, { 0x7FFC, 0x2F42, 0xC305, 0x60F2, 0x7F04, 0x000B, 0x0009 } };
        const DecompileResult rw = run(w);
        CHECK(Has(rw.Text(), "return param_1;"));
        // Two traps in a row give back 8 bytes each.
        const CallFixture t2 = { "two traps", base, { 0x4F22, 0x2F46, 0xC301, 0xC302, 0x60F6, 0x4F26, 0x000B, 0x0009 } };
        const DecompileResult r2 = run(t2);
        CHECK(Has(r2.Text(), "return param_1;"));
        // r15 itself: the stack pointer after the trap is the one before it.
        const CallFixture sp = { "trapa, r15", base, { 0xC301, 0x60F3, 0x000B, 0x0009 } };
        const DecompileResult rs = run(sp);
        CHECK(Has(rs.Text(), "return &xStack00000000;"));   // the entry r15 itself
    }

    // ---- A2-10: what a call leaves in the registers ------------------------------------------
    // r4-r7, T (sr), MACH and MACL are caller-saved in the SH ABI: a call leaves them unknown, as it
    // already left r1, r2, r3, so the text must not read as if the callee had not touched them.
    {
        for (int reg = 4; reg <= 7; ++reg)
        {
            const uint16_t setReg = uint16_t(0xE005 | (reg << 8)), readIt = uint16_t(0x6003 | (reg << 4));
            const uint16_t callee = uint16_t(0xE007 | (reg << 8));
            const std::string name = "r" + std::to_string(reg) + " after a call";
            const CallFixture f = { name, base,
                                    withCallee({ 0x4F22, setReg, 0xB00C, 0x0009, readIt, 0x4F26, 0x000B, 0x0009 },
                                               { 0x000B, callee }) };
            const DecompileResult r = run(f);
            CHECK(Has(r.Text(), ("return extraout_r" + std::to_string(reg) + ";").c_str()));
            CHECK(!Has(r.Text(), "xVar1 = 5"));
        }
        // The ledger's own r7 fixture, word for word.
        const CallFixture r7 = { "ledger A2-10 a", base,
                                 { 0x4F22, 0xE705, 0xB00C, 0x0009, 0x6073, 0x4F26, 0x000B, 0x0009, 0x0009, 0x0009, 0x0009,
                                   0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x000B, 0xE707 } };
        const DecompileResult rr = run(r7);
        CHECK(Has(rr.Text(), "return extraout_r7;"));
        // T: sett ; bsr f ; nop ; bt L -- f: rts ; clrt. The branch is on f's T.
        const CallFixture t = { "ledger A2-10 b", base,
                                { 0x4F22, 0x0018, 0xB00C, 0x0009, 0x8902, 0x4F26, 0x000B, 0xE000, 0x4F26, 0x000B, 0xE001,
                                  0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x000B, 0x0008 } };
        const DecompileResult rt = run(t);
        CHECK(Has(rt.Text(), "extraout_sr"));
        CHECK(!Has(rt.Text(), "uVar1 = 1;"));
        // ...and with T set by the callee and cleared by the caller before the call.
        const CallFixture t2 = { "T set by the callee", base,
                                 { 0x4F22, 0x0008, 0xB00C, 0x0009, 0x8902, 0x4F26, 0x000B, 0xE000, 0x4F26, 0x000B, 0xE001,
                                   0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x000B, 0x0018 } };
        const DecompileResult rt2 = run(t2);
        CHECK(Has(rt2.Text(), "extraout_sr"));
        // MACL and MACH, set by the caller and by the callee.
        const CallFixture macl = { "macl after a call", base,
                                   withCallee({ 0x4F22, 0xE005, 0x401A, 0xB00B, 0x0009, 0x4F26, 0x000B, 0x001A },
                                              { 0xE007, 0x401A, 0x000B, 0x0009 }) };
        CHECK(Has(run(macl).Text(), "return extraout_macl;"));
        const CallFixture mach = { "mach after a call", base,
                                   withCallee({ 0x4F22, 0xE005, 0x400A, 0xB00B, 0x0009, 0x4F26, 0x000B, 0x000A },
                                              { 0xE007, 0x400A, 0x000B, 0x0009 }) };
        CHECK(Has(run(mach).Text(), "return extraout_mach;"));
        // Controls. r2, r1 and r3 were killed already; r8 and r9 are the callee-saved registers, a
        // value held in them across the call is the value after it; VBR is a system register the
        // compilers never touch, so the trap vector read after a call is the one read before.
        const CallFixture r2 = { "r2 after a call", base,
                                 withCallee({ 0x4F22, 0xE205, 0xB00C, 0x0009, 0x6023, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0xE207 }) };
        CHECK(Has(run(r2).Text(), "return extraout_r2;"));
        const CallFixture r1 = { "r1 after a call", base,
                                 withCallee({ 0x4F22, 0xE105, 0xB00C, 0x0009, 0x6013, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0xE107 }) };
        CHECK(Has(run(r1).Text(), "return extraout_r1;"));
        const CallFixture r8 = { "r8 across a call", base,
                                 withCallee({ 0x4F22, 0xE805, 0xB00C, 0x0009, 0x6083, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x0009 }) };
        CHECK(Has(run(r8).Text(), "return 5;"));
        // An argument held in r8 over the call stays the argument.
        const CallFixture kept = { "argument kept in r8", base,
                                   withCallee({ 0x4F22, 0x6843, 0xB00C, 0x0009, 0x6083, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x0009 }) };
        CHECK(Has(run(kept).Text(), "return param_1;"));
        const CallFixture vbr = { "trapa after a call", base,
                                  withCallee({ 0x4F22, 0xB00D, 0x0009, 0xC303, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x0009 }) };
        const DecompileResult rv = run(vbr);
        CHECK(Has(rv.Text(), "unaff_vbr + 0xc") && !Has(rv.Text(), "extraout_vbr"));
    }

    // ---- Recursion ---------------------------------------------------------------------------
    // f(n): r1 = 5 ; if (n != 0) { f(n - 1); } ... return r1, with the base case setting r1 = 7. The
    // call is to the function itself, whose effects nobody has proved, so r1 after it is unknown;
    // the hardware returns 7 for every n. (The signature is withheld: "it calls itself".)
    {
        const CallFixture f = { "recursion changes r1", base,
                                { 0x4F22, 0x2448, 0x8907, 0xE105, 0x74FF, 0xBFF9, 0x0009, 0x6013, 0x4F26, 0x000B, 0x0009,
                                  0xE107, 0x6013, 0x4F26, 0x000B, 0x0009 } };
        const std::vector<std::vector<uint32_t>> depths = { { 0 }, { 1 }, { 3 } };
        const DecompileResult r = run(f, &depths);
        CHECK(Has(r.prototypeWithheld, "calls itself"));
        CHECK(!Has(r.Text(), "return 5;"));
        CHECK(Has(r.Text(), "extraout_r1"));
        // The same through r7 (the argument registers the recursion rewrites), which the base case
        // sets and the other path sets differently before the call.
        const CallFixture g = { "recursion changes r7", base,
                                { 0x4F22, 0x2448, 0x8907, 0xE705, 0x74FF, 0xBFF9, 0x0009, 0x6073, 0x4F26, 0x000B, 0x0009,
                                  0xE707, 0x6073, 0x4F26, 0x000B, 0x0009 } };
        const DecompileResult rg = run(g, &depths);
        CHECK(Has(rg.prototypeWithheld, "calls itself"));
        CHECK(Has(rg.Text(), "extraout_r7"));
    }

    // ---- Lent prototypes ---------------------------------------------------------------------
    // ApplyPrototype gives a callee's function object the default model, so a lent signature does
    // not by itself change what the call leaves in the registers: the callee that sets r7 still
    // leaves r7 unknown, and a caller that reads it is not lent a signature on a value no signature
    // gives. (What the callee was proved to keep is lent with it: A3-2, below.)
    {
        const CallFixture f = { "r7 after a call, callee lent", base,
                                withCallee({ 0x4F22, 0xE705, 0xB00C, 0x0009, 0x6073, 0x4F26, 0x000B, 0x0009 },
                                           { 0x000B, 0xE707 }) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult callee = d.Decompile(snap, Request(0x06009020u));
        CHECK(callee.prototype.known && callee.prototype.ret.kind == TypeKind::Void);
        DecompileRequest q = Request(f.base);
        q.prototypes.emplace_back(0x06009020u, callee.prototype);
        const DecompileResult r = d.Decompile(snap, q);
        Print("r7 after a call, callee lent", r);
        CHECK(Has(r.Text(), "return extraout_r7;"));
        CHECK(!Has(r.Text(), "xVar1 = 5"));
        CHECK(!r.prototype.known && Has(r.prototypeWithheld, "r7") && Has(r.prototypeWithheld, "not known to return it"));
        CHECK(PseudoAgreesWithRun(r, snap, f, inputs));
    }
    // r1 is an output register only for a 64-bit return: with the callee's signature lent as void
    // (or a 32-bit return) the engine no longer takes r1 for a possible output, and without a
    // killed-by-call entry it read, after the call, as the value before it.
    {
        const CallFixture f = { "r1 after a call, callee lent", base,
                                withCallee({ 0x4F22, 0xE105, 0xB00C, 0x0009, 0x6013, 0x4F26, 0x000B, 0x0009 },
                                           { 0x000B, 0xE107 }) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult callee = d.Decompile(snap, Request(0x06009020u));
        CHECK(callee.prototype.known && callee.prototype.ret.kind == TypeKind::Void);
        DecompileRequest q = Request(f.base);
        q.prototypes.emplace_back(0x06009020u, callee.prototype);
        const DecompileResult r = d.Decompile(snap, q);
        Print("r1 after a call, callee lent", r);
        CHECK(Has(r.Text(), "return extraout_r1;"));
        CHECK(!Has(r.Text(), "return 5;"));
        CHECK(PseudoAgreesWithRun(r, snap, f, inputs));
    }
    // ---- A3-2: the effects a callee proves are lent with its signature (patch 0028) ----------
    // A callee verified to keep r4-r7, MACH, MACL or SR at every return (CallEffects::preserved,
    // proved by ReturnCheck) is lent those effects with its signature: a call to it does not kill
    // them, so the caller reads them as they were and keeps the parameter it passed through. Before,
    // the default model's killedbycall (patch 0026) applied to every lent callee, the caller read
    // extraout_r4, lost the parameter and was withheld ("it uses r4 after calling X"). Only what was
    // proved counts: a register the callee changes on any path stays killed. Each fixture runs on
    // the emulator, and the evaluated text must agree.
    {
        // lend: the callee (0x06009020) decompiled first, its signature and effects lent to the caller.
        struct Lent
        {
            DecompileResult callee, caller;
        };
        auto lend = [&](const CallFixture& f, bool known = true, bool oracle = true) {
            auto snap = CallSnapshot(f, ++serial);
            Lent l;
            l.callee = d.Decompile(snap, Request(0x06009020u));
            DecompileRequest q = Request(f.base);
            Prototype p = l.callee.prototype;
            p.known = p.known && known;
            q.prototypes.emplace_back(0x06009020u, p);
            l.caller = d.Decompile(snap, q);
            Print(f.name.c_str(), l.caller);
            CHECK(l.caller.status == DecompileStatus::Ok);
            CHECK(!oracle || PseudoAgreesWithRun(l.caller, snap, f, inputs, !l.caller.prototype.known));
            return l;
        };
        // r4: a leaf that never touches it. The caller returns what it passed in r4.
        const CallFixture f = { "r4 kept by the callee", base,
                                withCallee({ 0x4F22, 0xB00D, 0x0009, 0x6043, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x0009 }) };
        const Lent l = lend(f);
        CHECK(l.callee.prototype.known);
        constexpr uint32_t kKeptByLeaf = 0xFFu | (1u << 16) | (1u << 19) | (1u << 20);   // r0-r7, SR, MACH, MACL
        CHECK((l.callee.prototype.effects.preserved & kKeptByLeaf) == kKeptByLeaf);
        CHECK(Has(l.caller.Text(), "return param_1;"));
        CHECK(!Has(l.caller.Text(), "extraout"));
        CHECK(l.caller.prototype.known && l.caller.prototypeWithheld.empty() && l.caller.prototype.params.size() == 1);
        // What was lent is the callee's own: the caller's text is the same with the signature lent
        // and none of its effects, bar the parameter it loses (the switch is the test hook).
        Sh2Decompiler::SetLendEffectsForTests(false);
        const Lent without = lend(f);
        Sh2Decompiler::SetLendEffectsForTests(true);
        CHECK(Has(without.caller.Text(), "return extraout_r4;"));
        CHECK(!without.caller.prototype.known && Has(without.caller.prototypeWithheld, "r4"));
        // Control: the callee's signature not lent at all, the model's kill stands.
        {
            auto snap = CallSnapshot(f, ++serial);
            const DecompileResult r = d.Decompile(snap, Request(f.base));
            Print("r4 kept by the callee, nothing lent", r);
            CHECK(Has(r.Text(), "return extraout_r4;"));
            CHECK(!r.prototype.known);
            CHECK(PseudoAgreesWithRun(r, snap, f, inputs));
        }
        // Control: a prototype the host does not call known lends nothing, effects included.
        {
            const Lent notKnown = lend(f, false);
            CHECK(Has(notKnown.caller.Text(), "return extraout_r4;"));
            CHECK(!notKnown.caller.prototype.known);
        }
        // r5 and r6, each read after the call: the matching parameter of the caller's signature.
        for (int reg = 5; reg <= 6; ++reg)
        {
            const CallFixture g = { "r" + std::to_string(reg) + " kept by the callee", base,
                                    withCallee({ 0x4F22, 0xB00D, 0x0009, uint16_t(0x6003 | (reg << 4)), 0x4F26, 0x000B, 0x0009 },
                                               { 0x000B, 0x0009 }) };
            const Lent lg = lend(g);
            const std::string param = "param_" + std::to_string(reg - 3);
            CHECK(Has(lg.caller.Text(), ("return " + param + ";").c_str()));
            CHECK(!Has(lg.caller.Text(), "extraout"));
            CHECK(lg.caller.prototype.known && lg.caller.prototype.params.size() == static_cast<size_t>(reg - 3));
        }
        // r2 and r3 are no parameters: kept by the callee they read after the call as the caller's own
        // register (an input), not as a value the call made.
        for (int reg = 2; reg <= 3; ++reg)
        {
            const CallFixture g = { "r" + std::to_string(reg) + " kept by the callee", base,
                                    withCallee({ 0x4F22, 0xB00D, 0x0009, uint16_t(0x2002 | (4 << 8) | (reg << 4)), 0x4F26, 0x000B, 0x0009 },
                                               { 0x000B, 0x0009 }),
                                    true };   // mov.l rN,@r4 after the call; r0 is left as it was (0x1000 on the run): no result
            const Lent lg = lend(g);
            CHECK(Has(lg.caller.Text(), ("= in_r" + std::to_string(reg) + ";").c_str()) && !Has(lg.caller.Text(), "extraout"));
            Sh2Decompiler::SetLendEffectsForTests(false);
            CHECK(Has(lend(g).caller.Text(), ("= extraout_r" + std::to_string(reg) + ";").c_str()));
            Sh2Decompiler::SetLendEffectsForTests(true);
        }
        // r7 alone is a read of an input the engine does not take for a parameter without r4-r6 (a
        // caller that only reads r7 is `in_r7` with its signature withheld, lent or not): what lending
        // changes is that it is no longer the callee's r7 (extraout_r7).
        {
            const CallFixture g = { "r7 kept by the callee", base,
                                    withCallee({ 0x4F22, 0xB00D, 0x0009, 0x6073, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x0009 }) };
            const Lent lg = lend(g);
            CHECK(Has(lg.caller.Text(), "return in_r7;") && !Has(lg.caller.Text(), "extraout"));
            Sh2Decompiler::SetLendEffectsForTests(false);
            CHECK(Has(lend(g).caller.Text(), "return extraout_r7;"));
            Sh2Decompiler::SetLendEffectsForTests(true);
        }
        // MACL and MACH: set from the argument by the caller, read after the call.
        const CallFixture macl = { "macl kept by the callee", base,
                                   withCallee({ 0x4F22, 0x441A, 0xB00C, 0x0009, 0x001A, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x0009 }) };
        const Lent lmacl = lend(macl);
        CHECK(Has(lmacl.caller.Text(), "return param_1;") && !Has(lmacl.caller.Text(), "extraout"));
        CHECK(lmacl.caller.prototype.known);
        const CallFixture mach = { "mach kept by the callee", base,
                                   withCallee({ 0x4F22, 0x440A, 0xB00C, 0x0009, 0x000A, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x0009 }) };
        const Lent lmach = lend(mach);
        CHECK(Has(lmach.caller.Text(), "return param_1;") && !Has(lmach.caller.Text(), "extraout"));
        CHECK(lmach.caller.prototype.known);
        // T: sett ; bsr f ; nop ; bt L -- f keeps SR, so the branch is on the T from before the call.
        const CallFixture t = { "T kept by the callee", base,
                                { 0x4F22, 0x0018, 0xB00C, 0x0009, 0x8902, 0x4F26, 0x000B, 0xE000, 0x4F26, 0x000B, 0xE001,
                                  0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x000B, 0x0009 } };
        const Lent lt = lend(t);
        CHECK(!Has(lt.caller.Text(), "extraout_sr") && Has(lt.caller.Text(), "return 1;"));   // the T it set
        // r1 (and r0) are the return registers: kept by the callee and proved so, they are still the
        // model's kill (see LendEffects), as they were before this change.
        const CallFixture r1 = { "r1 kept by the callee", base,
                                 withCallee({ 0x4F22, 0xB00D, 0x0009, 0x6013, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x0009 }) };
        const Lent lr1 = lend(r1);
        CHECK(lr1.callee.prototype.effects.preserved & (1u << 1));
        CHECK(Has(lr1.caller.Text(), "return extraout_r1;"));
        // A callee that returns a value keeps r4 all the same, and r0 is its return.
        const CallFixture ret = { "r4 kept by a callee that returns", base,
                                  withCallee({ 0x4F22, 0xB00D, 0x0009, 0x6143, 0x301C, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0xE005 }) };
        const Lent lret = lend(ret);
        CHECK(lret.callee.prototype.known && lret.callee.prototype.ret.kind != TypeKind::Void);
        CHECK((lret.callee.prototype.effects.preserved & (1u << 4)) && !(lret.callee.prototype.effects.preserved & 1u));
        CHECK(Has(lret.caller.Text(), "param_1") && !Has(lret.caller.Text(), "extraout"));
        CHECK(lret.caller.prototype.known && lret.caller.prototype.params.size() == 1);

        // Controls: what the callee changes stays killed. r4 set in the callee's delay slot: the
        // caller reads the callee's r4, and r5, which that callee keeps, stays its parameter.
        const CallFixture clobbers = { "r4 clobbered by the callee", base,
                                       withCallee({ 0x4F22, 0xB00D, 0x0009, 0x6043, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0xE407 }) };
        const Lent lc = lend(clobbers);
        CHECK(!(lc.callee.prototype.effects.preserved & (1u << 4)) && (lc.callee.prototype.effects.preserved & (1u << 5)));
        CHECK(Has(lc.caller.Text(), "return extraout_r4;"));
        CHECK(!lc.caller.prototype.known && Has(lc.caller.prototypeWithheld, "r4"));
        const CallFixture other = { "r5 kept by a callee that clobbers r4", base,
                                    withCallee({ 0x4F22, 0xB00D, 0x0009, 0x6053, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0xE407 }) };
        const Lent lo = lend(other);
        CHECK(Has(lo.caller.Text(), "return param_2;") && !Has(lo.caller.Text(), "extraout"));
        CHECK(lo.caller.prototype.known && lo.caller.prototype.params.size() == 2);
        // A callee that keeps r4 on one path only is not proved: cmp/eq #0,r0 ; bt L ; mov #7,r4 ; L: rts ; nop.
        const CallFixture onePath = { "r4 kept on one path only", base,
                                      withCallee({ 0x4F22, 0xB00D, 0x0009, 0x6043, 0x4F26, 0x000B, 0x0009 },
                                                 { 0x8800, 0x8900, 0xE407, 0x000B, 0x0009 }) };
        const Lent lp = lend(onePath);
        CHECK(!(lp.callee.prototype.effects.preserved & (1u << 4)));
        CHECK(Has(lp.caller.Text(), "return extraout_r4;"));
        CHECK(!lp.caller.prototype.known);

        // ---- A3-3 (round 3): a register the callee is proved to change is killed at the call ------------
        // The default model keeps r8-r14, GBR and VBR across a call (patch 0026), so a lent callee that
        // changes one of them -- which ReturnCheck shows by clearing its bit in CallEffects::preserved --
        // used to be read as keeping it: the caller printed the value from before the call and was lent a
        // signature on it. Lent, the proved change is killedbycall (LendEffects); the caller reads the
        // callee's value (extraout_rN) and, as for r4-r7, is withheld on it.
        for (int reg : { 8, 9, 14 })
        {
            const CallFixture g = { "r" + std::to_string(reg) + " set by the callee", base,
                                    withCallee({ 0x4F22, uint16_t(0xE005 | (reg << 8)), 0xB00C, 0x0009, uint16_t(0x6003 | (reg << 4)), 0x4F26, 0x000B, 0x0009 },
                                               { 0x000B, uint16_t(0xE007 | (reg << 8)) }) };
            const Lent lg = lend(g);
            CHECK(lg.callee.prototype.known && lg.callee.prototype.effects.preserved != 0);
            CHECK(!((lg.callee.prototype.effects.preserved >> reg) & 1u));
            CHECK(Has(lg.caller.Text(), ("return extraout_r" + std::to_string(reg) + ";").c_str()));
            CHECK(!Has(lg.caller.Text(), "return 5;"));
            CHECK(!lg.caller.prototype.known && Has(lg.caller.prototypeWithheld, ("r" + std::to_string(reg)).c_str()));
            // The same callee with its effects not lent (the hook): the model keeps the register, the caller
            // states the value it set -- which the run shows wrong, so the oracle is not asked.
            Sh2Decompiler::SetLendEffectsForTests(false);
            auto snap = CallSnapshot(g, ++serial);
            DecompileRequest q = Request(g.base);
            q.prototypes.emplace_back(0x06009020u, lg.callee.prototype);
            CHECK(Has(d.Decompile(snap, q).Text(), "return 5;"));
            Sh2Decompiler::SetLendEffectsForTests(true);
        }
        // A callee that uses r8 and gives it back (saved and restored) keeps it, proved: lent kept.
        {
            const CallFixture g = { "r8 saved and restored by the callee", base,
                                    withCallee({ 0x4F22, 0xE805, 0xB00C, 0x0009, 0x6083, 0x4F26, 0x000B, 0x0009 },
                                               { 0x2F86, 0xE807, 0x000B, 0x68F6 }) };   // mov.l r8,@-r15 ; mov #7,r8 ; rts ; mov.l @r15+,r8
            const Lent lg = lend(g);
            CHECK(lg.callee.prototype.known && ((lg.callee.prototype.effects.preserved >> 8) & 1u));
            CHECK(Has(lg.caller.Text(), "return 5;") && !Has(lg.caller.Text(), "extraout"));
            CHECK(lg.caller.prototype.known);
        }
        // GBR and VBR, loaded from the callee's second parameter.
        for (int vbr = 0; vbr <= 1; ++vbr)
        {
            const CallFixture g = { vbr ? "VBR set by the callee" : "GBR set by the callee", base,
                                    withCallee({ 0x4F22, uint16_t(vbr ? 0x442E : 0x441E), 0xB00C, 0x0009, uint16_t(vbr ? 0x0022 : 0x0012), 0x4F26, 0x000B, 0x0009 },
                                               { 0x000B, uint16_t(vbr ? 0x452E : 0x451E) }) };
            const Lent lg = lend(g);
            const char* name = vbr ? "vbr" : "gbr";
            CHECK(lg.callee.prototype.known && !((lg.callee.prototype.effects.preserved >> (vbr ? 18 : 17)) & 1u));
            CHECK(Has(lg.caller.Text(), (std::string("return extraout_") + name + ";").c_str()));
            CHECK(!Has(lg.caller.Text(), "return param_1;"));
            CHECK(!lg.caller.prototype.known && Has(lg.caller.prototypeWithheld, name));
        }
        // The ledger's own b: `rts ; ldc r1,gbr`. The callee reads r1 and changes GBR. Before patch 0034 nothing in the callee
        // observed GBR, so the engine dropped the load and with it the read: the callee was lent with GBR killed and the caller
        // read it as extraout_gbr. The load stays in the code now (Write_GBR(in_r1)), which is right: r1 is an input no signature
        // passes (A3-5), so the callee is withheld, and so is its caller. The caller's text reads GBR as the model has it for a
        // callee that is not lent (kept), which the run shows wrong; the oracle is not asked, as for every unlent callee (the
        // same limit as a callee not decompiled first that changes r8). What is pinned is that nothing incomplete is lent (the same load from r5 is lent, with GBR killed: the loop above).
        {
            const CallFixture g = { "GBR loaded from r1 by the callee", base,
                                    withCallee({ 0x4F22, 0x441E, 0xB00C, 0x0009, 0x0012, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x411E }) };
            const Lent lg = lend(g, true, false);
            CHECK(!lg.callee.prototype.known && Has(lg.callee.prototypeWithheld, "reads r1 on entry"));
            CHECK(!lg.caller.prototype.known && Has(lg.caller.prototypeWithheld, "whose signature is not known"));
        }
        // MACH and MACL, which the model kills for every callee: set by a lent callee, they are its values.
        for (int h = 0; h <= 1; ++h)
        {
            const CallFixture g = { h ? "MACH set by the callee" : "MACL set by the callee", base,
                                    withCallee({ 0x4F22, uint16_t(h ? 0x440A : 0x441A), 0xB00C, 0x0009, uint16_t(h ? 0x000A : 0x001A), 0x4F26, 0x000B, 0x0009 },
                                               { 0x000B, uint16_t(h ? 0x450A : 0x451A) }) };   // lds r5,mach / lds r5,macl
            const Lent lg = lend(g);
            const char* name = h ? "mach" : "macl";
            CHECK(lg.callee.prototype.known && !((lg.callee.prototype.effects.preserved >> (h ? 19 : 20)) & 1u));
            CHECK(Has(lg.caller.Text(), (std::string("return extraout_") + name + ";").c_str()));
            CHECK(!lg.caller.prototype.known && Has(lg.caller.prototypeWithheld, name));
        }
        // r15: the callee leaves 4 bytes of its own on the stack (add #-4,r15 ; rts ; mov.l r5,@r15) and the caller
        // pops them. Round 3 lent both, with the callee's r15 difference as its extrapop; the caller returns the
        // callee's r5 (the word it popped), and its lent signature had no parameter for it: the callee's store is below
        // its own entry r15, a dead store of the engine's, so r5 was no input, and the caller read a local nothing wrote.
        // The oracle took that local for unknown. A3-15 (round 5): a negative difference is a value handed through the
        // stack, which a signature of registers cannot carry; the callee is withheld and so is its caller. The run
        // proves the return (r5), and the oracle, which now reads the unwritten local as the word the run started with,
        // rejects the caller's text (the model's, as for any unlent callee).
        {
            const CallFixture g = { "r15 lowered by the callee", base,
                                    withCallee({ 0x0E2A, 0x2F46, 0xB00C, 0x0009, 0x60F6, 0x4E2A, 0x000B, 0x0009 },
                                               { 0x7FFC, 0x000B, 0x2F52 }) };
            const Lent lg = lend(g, true, false);
            CHECK(!lg.callee.prototype.known && Has(lg.callee.prototypeWithheld, "lower"));
            CHECK(!lg.caller.prototype.known && Has(lg.caller.prototypeWithheld, "not known"));
            uint32_t ran = 0;
            CHECK(CallRunResult(CallSnapshot(g, ++serial), g, inputs[0], ran) && ran == 0x22222222u);   // r5, the second argument
            CHECK(Has(lg.caller.Text(), "return param_1;"));   // the model's r15: the word the caller pushed
            auto snap = CallSnapshot(g, ++serial);
            CHECK(!PseudoAgreesWithRun(lg.caller, snap, g, inputs));   // the text reads the unwritten local, the word the run started with
            // The ledger's A3-15 fixture: pushes with `mov.l r5,@-r15` in rts's slot, popped into r0 by the caller.
            const CallFixture h = { "register pushed by the callee and popped by the caller", base,
                                    withCallee({ 0x4F22, 0xB00D, 0x0009, 0x60F6, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x2F56 }) };
            const Lent lh = lend(h, true, false);
            CHECK(!lh.callee.prototype.known && Has(lh.callee.prototypeWithheld, "lower"));
            CHECK(!lh.caller.prototype.known);
            CHECK(Has(lh.caller.Text(), "unaff_retaddr"));   // the model pops the saved PR; the run pops r5
            CHECK(!PseudoAgreesWithRun(lh.caller, CallSnapshot(h, ++serial), h, inputs));
            // Controls: a callee that pops what its caller pushed (add #4,r15 after it read nothing) is lent as before, and
            // so is the caller that pushed it.
            const CallFixture pop = { "callee pops the caller's push", base,
                                      withCallee({ 0x4F22, 0x2F46, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x7F04 }) };
            const Lent lp = lend(pop, true, false);
            CHECK(lp.callee.prototype.known && lp.callee.prototype.effects.spDelta == 4);
            CHECK(lp.caller.prototype.known);
        }
        // ...and a callee that moves r15 by an amount that depends on an argument cannot say where it leaves
        // it: not lent, and so not its caller either.
        {
            const CallFixture g = { "r15 moved by an argument", base,
                                    withCallee({ 0x0E2A, 0x2F46, 0xB00C, 0x0009, 0x60F6, 0x4E2A, 0x000B, 0x0009 },
                                               { 0x3F5C, 0x000B, 0x0009 }) };   // add r5,r15 ; rts ; nop
            auto snap = CallSnapshot(g, ++serial);
            const DecompileResult callee = d.Decompile(snap, Request(0x06009020u));
            Print(g.name.c_str(), callee);
            CHECK(!callee.prototype.known && Has(callee.prototypeWithheld, "r15"));
            DecompileRequest q = Request(g.base);
            q.prototypes.emplace_back(0x06009020u, callee.prototype);
            const DecompileResult caller = d.Decompile(snap, q);
            CHECK(!caller.prototype.known && Has(caller.prototypeWithheld, "not known"));
        }

        // ---- A3-5 (round 3): an input outside r4-r7 and the stack is not lent away --------------------------
        // A callee that reads r0-r3, r8-r14, GBR, MACH, MACL or T before it writes it took the value from its
        // caller, which no signature passes: the caller's setup of it is dead in the caller's text, and the
        // callee was lent as if it took nothing (or, when the input alone made its return, as returning
        // nothing: `rts ; sts macl,r0` printed `return;`). Such a callee is withheld, and so are its callers.
        struct Input
        {
            const char* what;
            std::vector<uint16_t> caller, callee;
            const char* reg;
        };
        const std::vector<uint16_t> noSetup = { 0x4F22, 0x0009, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 };
        const Input reads[] = {
            { "r0", { 0x4F22, 0xE005, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x7001 }, "r0" },     // add #1,r0
            { "macl", { 0x4F22, 0x441A, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x001A }, "macl" },   // sts macl,r0
            { "mach", { 0x4F22, 0x440A, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x000A }, "mach" },   // sts mach,r0
            { "r2", { 0x4F22, 0x6243, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x6023 }, "r2" },       // mov r2,r0
            { "r3", { 0x4F22, 0x6343, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x6033 }, "r3" },       // mov r3,r0
            { "T", { 0x4F22, 0x0018, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x0029 }, "sr" },       // movt r0 after sett
            { "r8", { 0x4F22, 0x6843, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x6083 }, "r8" },       // mov r8,r0
            { "r14 as a pointer", { 0x4F22, 0x6E43, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0x60E2, 0x000B, 0x0009 }, "r14" },   // mov.l @r14,r0
            { "GBR", { 0x4F22, 0x441E, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0xC600, 0x000B, 0x0009 }, "gbr" },   // mov.l @(0,gbr),r0
            // T branched on, no value of it in r0: only the input the engine keeps shows it
            { "T branched on", { 0x4F22, 0x0018, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 },
              { 0x8902, 0xE001, 0x000B, 0x0009, 0xE002, 0x000B, 0x0009 }, "sr" },   // bt L ; mov #1,r0 ; rts ; nop ; L: mov #2,r0 ; rts ; nop
            // the whole SR, but computed from: the interrupt mask a caller set up
            { "SR masked", { 0x4F22, 0x0018, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0x0002, 0xC9F0, 0x000B, 0x0009 }, "sr" },   // stc sr,r0 ; and #0xF0,r0 ; rts
        };
        for (const Input& in : reads)
        {
            const CallFixture g = { std::string(in.what) + " read by the callee", base, withCallee(in.caller, in.callee) };
            const Lent lg = lend(g);
            CHECK(!lg.callee.prototype.known);
            CHECK(Has(lg.callee.prototypeWithheld, (std::string("reads ") + in.reg + " on entry").c_str()) ||
                  Has(lg.callee.prototypeWithheld, (std::string("computed from ") + in.reg + " on entry").c_str()));
            CHECK(!lg.caller.prototype.known && Has(lg.caller.prototypeWithheld, "whose signature is not known"));
        }
        // What stays lent: parameters in r4-r7, a flag written here and read here, a register saved and
        // restored (above).
        {
            const CallFixture g = { "r4 and r5 read by the callee", base,
                                    withCallee(noSetup, { 0x6043, 0x000B, 0x305C }) };   // mov r4,r0 ; rts ; add r5,r0
            const Lent lg = lend(g);
            CHECK(lg.callee.prototype.known && lg.callee.prototype.params.size() == 2);
            const CallFixture t = { "T written and read by the callee", base,
                                    withCallee(noSetup, { 0x2448, 0x000B, 0x0029 }) };   // tst r4,r4 ; rts ; movt r0
            const Lent lt = lend(t);
            CHECK(lt.callee.prototype.known && lt.callee.prototype.params.size() == 1);
            // The status register returned whole is machine state, not an input (the idiom of an
            // interrupt-mask helper): the compiler's `stc sr,r1 ; mov.l r1,@r14 ; mov.l @r14,r0` is lent as
            // returning it, the engine keeps that return...
            const CallFixture sr = { "SR returned whole by the callee", base,
                                     withCallee(noSetup, { 0x2FE6, 0x7FFC, 0x6EF3, 0x0102, 0x2E12, 0x60E2, 0x7E04, 0x6FE3, 0x000B, 0x6EF6 }) };
            const Lent lsr = lend(sr);
            CHECK(lsr.callee.prototype.known && lsr.callee.prototype.ret.kind != TypeKind::Void);
            CHECK(Has(lsr.callee.Text(), "in_sr") && lsr.caller.prototype.known);
        }
        // ...but the bare `rts ; stc sr,r0` is printed as returning nothing, like `sts macl,r0`: withheld.
        {
            const CallFixture sr = { "SR copied to r0 in rts's slot", base, withCallee(noSetup, { 0x000B, 0x0002 }) };
            const Lent lsr = lend(sr);
            CHECK(!lsr.callee.prototype.known && Has(lsr.callee.prototypeWithheld, "computed from sr on entry"));
        }

        // The interrupt mask raised and put back round a call whose SR the model kills: the SR read after the call
        // (`Write_SR(extraout_sr | 0xf0)`) goes only into a write of SR, so it is the machine's state and not a value the callee
        // was supposed to return; the signature is lent. The oracle is not asked (the call's SR is not a value it can evaluate).
        {
            const CallFixture g = { "mask raised after a call that sets T", base,
                                    withCallee({ 0x4F22, 0xB00D, 0x0009, 0x0002, 0xCBF0, 0x400E, 0xE000, 0x4F26, 0x000B, 0x0009 }, { 0x2448, 0x000B, 0x0009 }) };
            const Lent lg = lend(g, true, false);
            CHECK(lg.callee.prototype.known && lg.caller.prototype.known);
            CHECK(Has(lg.caller.Text(), "Write_SR(extraout_sr"));
        }

        // A caller whose callee's registers share one set of origins is not a function that returns them (the
        // first form of the dropped-return rule withheld 23 corpus functions that save r8 and call): the callee
        // reads r8 only to save it and scratches r1 with a copy, then sets r0.
        {
            const CallFixture g = { "void caller of a callee that scratches r1 from r8", base,
                                    withCallee({ 0x4F22, 0xB00D, 0x0009, 0x4F26, 0x000B, 0x0009 },
                                               { 0x2F86, 0x6183, 0xE805, 0xE003, 0x000B, 0x68F6 }) };   // push r8 ; mov r8,r1 ; mov #5,r8 ; mov #3,r0 ; rts ; pop r8
            const Lent lg = lend(g);
            CHECK(lg.callee.prototype.known && ((lg.callee.prototype.effects.preserved >> 8) & 1u));
            CHECK(lg.caller.prototype.known && lg.caller.prototypeWithheld.empty());
        }

        // ---- A3-7 (round 4): a dropped return in a function that calls -------------------------------------------
        // `bsr g ; ... ; rts ; mov r8,r0` returns the caller's r8 across a call that keeps it, and the engine drops
        // that return as it drops the leaf's (`return;`). Rule (b) asked only paths that had made no call, so the
        // function was lent `void` and the input was lost. It asks every path now: what a call returned is no read of
        // an input (Value::Direct), what this function copied to r0 from a register that holds one is. The text is
        // still `return;` (the engine's), so the oracle rejects it; the signature is withheld.
        {
            struct Dropped
            {
                const char* what;
                std::vector<uint16_t> caller;
                const char* reg;
            };
            const Dropped cases[] = {
                { "r8 returned after a lent call", { 0x4F22, 0xB00D, 0x0009, 0x4F26, 0x000B, 0x6083, 0x0009 }, "r8" },
                { "macl returned after a lent call", { 0x4F22, 0xB00D, 0x0009, 0x4F26, 0x000B, 0x001A, 0x0009 }, "macl" },
                { "gbr returned after a lent call", { 0x4F22, 0xB00D, 0x0009, 0x4F26, 0x000B, 0x0012, 0x0009 }, "gbr" },
                // the call on one path only: the path with r4 = 0 makes none
                { "macl returned, call on one path", { 0x4F22, 0x2448, 0x8901, 0xB00B, 0x0009, 0x4F26, 0x000B, 0x001A }, "macl" },
                // the value copied before the call, kept across it by a callee that keeps r0 (this one does)
                { "r9 copied to r0 before the call", { 0x4F22, 0x6093, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 }, "r9" },
            };
            for (const Dropped& c : cases)
            {
                const CallFixture g = { c.what, base, withCallee(c.caller, { 0x000B, 0x0009 }) };
                auto snap = CallSnapshot(g, ++serial);
                const DecompileResult callee = d.Decompile(snap, Request(0x06009020u));
                CHECK(callee.prototype.known);
                DecompileRequest q = Request(g.base);
                q.prototypes.emplace_back(0x06009020u, callee.prototype);
                const DecompileResult r = d.Decompile(snap, q);
                Print(c.what, r);
                CHECK(r.status == DecompileStatus::Ok);
                CHECK(!r.prototype.known && Has(r.prototypeWithheld, (std::string("computed from ") + c.reg + " on entry").c_str()));
                CHECK(!PseudoAgreesWithRun(r, snap, g, inputs));   // the text states no return where the code returns the register
            }
            // Controls: the function that saves r8, calls and returns the callee's value is lent; so is the one
            // that returns a value it computed from a parameter after the call.
            const CallFixture saves = { "r8 saved around a call, the callee's value returned", base,
                                        withCallee({ 0x4F22, 0x2F86, 0xB00C, 0x0009, 0x68F6, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0xE005 }) };
            const Lent ls = lend(saves);
            CHECK(ls.callee.prototype.known && ls.caller.prototype.known && ls.caller.prototype.ret.kind != TypeKind::Void);
            const CallFixture later = { "parameter returned after a call", base,
                                        withCallee({ 0x4F22, 0xB00D, 0x0009, 0x6043, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x0009 }) };
            CHECK(lend(later).caller.prototype.known);
        }

        // ---- A3-4 (round 3): the address of a returned structure, in r2 ----------------------------------------
        // The SH ABI of both compilers passes it in r2, and the engine's calling convention has no input
        // there: `mov r15,r2` is dead code, the local's address never reaches the call, and the call was
        // taken to leave the local alone -- the read after it printed the value stored before (`return 5;`
        // where the code returns 7). A call whose signature is not lent may read r2, so a frame address in
        // it is a pointer into the frame as far as the engine's alias analysis is concerned (the host
        // declares it before the actions, the engine's patch 0030); the local is then memory a call may
        // write, and printed so. The callee here reads r2, so it is not lent (A3-5) and nor is the caller.
        {
            const CallFixture g = { "structure return through r2", base,
                                    { 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0x62F3, 0xB009, 0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009,
                                      0x0009, 0x0009, 0x0009, 0x0009, 0xE107, 0x000B, 0x2212 } };
            const Lent lg = lend(g);   // the oracle: the run returns 7
            CHECK(!lg.callee.prototype.known && Has(lg.callee.prototypeWithheld, "reads r2"));
            CHECK(!Has(lg.caller.Text(), "return 5;") && Has(lg.caller.Text(), "return axStack_"));
            CHECK(!lg.caller.prototype.known);
            // The callee not lent at all, as before it was decompiled: the same text.
            auto snap = CallSnapshot(g, ++serial);
            const DecompileResult alone = d.Decompile(snap, Request(g.base));
            Print("structure return through r2, nothing lent", alone);
            CHECK(!Has(alone.Text(), "return 5;") && Has(alone.Text(), "return axStack_"));
            CHECK(PseudoAgreesWithRun(alone, snap, g, inputs));
            // Controls. The same caller and a callee that does not read r2 (rts ; nop): lent, so r2 is
            // dead at the call, the local is its own and the folded read stands -- and the run agrees.
            const CallFixture quiet = { "frame address in r2, callee that ignores it", base,
                                        withCallee({ 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0x62F3, 0xB009, 0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009 },
                                                   { 0x000B, 0x0009 }) };
            const Lent lq = lend(quiet);
            CHECK(lq.callee.prototype.known);
            CHECK(Has(lq.caller.Text(), "return 5;") && lq.caller.prototype.known);
            // ...and the address of the local in r4 (a parameter register) was never a problem: the call
            // takes it as its argument.
            const CallFixture viaR4 = { "frame address in r4", base,
                                        withCallee({ 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0x64F3, 0xB009, 0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009 },
                                                   { 0xE107, 0x000B, 0x2412 }) };   // mov #7,r1 ; rts ; mov.l r1,@r4
            const Lent l4 = lend(viaR4);
            CHECK(l4.callee.prototype.known && l4.callee.prototype.params.size() == 1);
            CHECK(!Has(l4.caller.Text(), "return 5;"));
        }
        // ---- A3-9 (round 4): the address of an array element in r2 --------------------------------------------
        // `a[i] = f()` for a returned structure passes frame address + index in r2. ReturnCheck declared only an
        // exact frame address, so the two locals here were the caller's own, the callee's write through r2 was
        // taken for none and the read after the call printed the value stored before (`return 10;` where the code
        // returns 12 for the index 0 and for 4). The address now keeps the offset it was made from, and that is the
        // alias's lower bound whatever index is added.
        {
            std::vector<uint16_t> a = { 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0x1F01, 0x6043, 0xC904, 0x62F3, 0x320C, 0xB015, 0x0009, 0x60F2,
                                        0x51F1, 0x301C, 0x7F08, 0x4F26, 0x000B, 0x0009 };   // mov r4,r0 ; and #4,r0 ; mov r15,r2 ; add r0,r2 ; bsr g
            a.resize(32, 0x0009);
            a.insert(a.end(), { 0xE107, 0x000B, 0x2212 });   // g: mov #7,r1 ; rts ; mov.l r1,@r2
            const std::vector<std::vector<uint32_t>> idx = { { 0 }, { 4 }, { 0x11111111u } };
            const CallFixture g = { "structure return through r2 plus a masked index", base, a };
            auto snap = CallSnapshot(g, ++serial);
            const DecompileResult r = d.Decompile(snap, Request(g.base));
            Print(g.name.c_str(), r);
            CHECK(r.status == DecompileStatus::Ok);
            CHECK(!Has(r.Text(), "return 10;") && Has(r.Text(), "iStack_c + iStack_8"));   // the read after the call is not folded
            CHECK(PseudoAgreesWithRun(r, snap, g, idx));
            CHECK(!r.prototype.known);   // the callee is not lent (it reads r2), and so is the caller not
            // ...with the callee decompiled first and lent: it reads r2, so it is withheld, the same text.
            const DecompileResult callee = d.Decompile(snap, Request(0x06009040u));
            CHECK(!callee.prototype.known && Has(callee.prototypeWithheld, "reads r2"));
            // The index a parameter, added whole (the run keeps to the two words the indices 0 and 4 reach).
            std::vector<uint16_t> b = a;
            b[5] = 0x62F3;    // mov r15,r2
            b[6] = 0x324C;    // add r4,r2
            b[7] = b[8] = 0x0009;
            const CallFixture h = { "structure return through r2 plus a parameter", base, b };
            auto snapH = CallSnapshot(h, ++serial);
            const DecompileResult rh = d.Decompile(snapH, Request(h.base));
            Print(h.name.c_str(), rh);
            CHECK(!Has(rh.Text(), "return 10;") && Has(rh.Text(), "iStack_c + iStack_8"));
            CHECK(PseudoAgreesWithRun(rh, snapH, h, { { 0 }, { 4 } }));
            // Control: a frame address plus an index that the callee does not read (r2 is dead in a lent callee):
            // the locals are the caller's own, as before.
            std::vector<uint16_t> q = a;
            q.resize(32, 0x0009);
            q.insert(q.end(), { 0x000B, 0x0009 });
            const CallFixture quiet = { "frame address plus an index, callee that ignores it", base, q };
            auto snapQ = CallSnapshot(quiet, ++serial);
            const DecompileResult calleeQ = d.Decompile(snapQ, Request(0x06009040u));
            DecompileRequest qq = Request(quiet.base);
            qq.prototypes.emplace_back(0x06009040u, calleeQ.prototype);
            const DecompileResult callerQ = d.Decompile(snapQ, qq);
            Print(quiet.name.c_str(), callerQ);
            CHECK(calleeQ.prototype.known && callerQ.prototype.known);
            CHECK(Has(callerQ.Text(), "return 10;"));
            CHECK(PseudoAgreesWithRun(callerQ, snapQ, quiet, idx));
        }
        // ---- A3-10 (round 4), accepted: a frame address in r3 is not declared -----------------------------------
        // The SH ABI passes the address of a returned structure in r2 and nowhere else (gcc's SH_STRUCT_VALUE_REGNUM,
        // Renesas' compilers); r3 is call-clobbered scratch, and gcc's static chain (a nested function's pointer to
        // its parent's frame) is the one other pointer to a frame it puts there, which neither the Hitachi/SN compilers
        // nor game code use. A frame address sits in r3 at 36 and 53 of the call sites the two corpus images have (and
        // in r14 at 852 and 2,068: the frame pointer), so a rule over r3 would alias the locals of those functions to
        // protect a convention nothing uses. Hand-written code that passes a frame pointer in r3 gets the folded read
        // (the callee reads r3, so it is not lent and nor is the caller; only the text is wrong). Pinned as a limit.
        {
            const CallFixture g = { "frame address in r3", base,
                                    withCallee({ 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0x63F3, 0xB009, 0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009 },
                                               { 0xE107, 0x000B, 0x2312 }) };
            auto snap = CallSnapshot(g, ++serial);
            const DecompileResult r = d.Decompile(snap, Request(g.base));
            Print(g.name.c_str(), r);
            CHECK(Has(r.Text(), "return 5;") && !r.prototype.known);   // the accepted limit
            uint32_t ran = 0;
            CHECK(CallRunResult(snap, g, inputs[0], ran) && ran == 7u);   // not 5
        }
        // ---- A3-8 (round 4), accepted: T set by the caller reaches a lent SR getter ---------------------------------
        // A callee that returns the status register whole is machine state, not an input (A3-5): it is lent. A caller
        // that sets T (`cmp/eq r6,r7`), calls it and returns bit 0 of the result depends on r6 and r7 through the
        // callee's read of SR, though SR is no input of any signature, and is lent with no parameters. Accepted:
        // compiled code does not keep T across a call (the SH ABI makes it caller-saved) or read it out of a returned SR
        // (the 20 getters of the corpus are interrupt-mask helpers whose callers mask the I bits, which the getter
        // takes from the machine). Withholding every caller that has set T before such a call would cost the callers
        // that never use it. Hand-written code can; the generator, asked by hand, reports it (param-dropped:r6) and
        // its grammar does not emit the shape. Pinned: the run depends on r6 and r7, the lent caller has no parameter for them.
        {
            const CallFixture g = { "caller sets T, lent SR getter", base,
                                    withCallee({ 0x4F22, 0x3760, 0xB00C, 0x0009, 0xC901, 0x4F26, 0x000B, 0x0009 },
                                               { 0x2FE6, 0x7FFC, 0x6EF3, 0x0102, 0x2E12, 0x60E2, 0x7E04, 0x6FE3, 0x000B, 0x6EF6 }) };
            const Lent lg = lend(g);
            CHECK(lg.callee.prototype.known && lg.caller.prototype.known && lg.caller.prototype.params.empty());
            uint32_t equal = 0, differ = 0;
            CHECK(CallRunResult(CallSnapshot(g, ++serial), g, { 1, 2, 5, 5 }, equal) && equal == 1u);
            CHECK(CallRunResult(CallSnapshot(g, ++serial), g, { 1, 2, 5, 6 }, differ) && differ == 0u);
        }
        // ---- A3-6 (round 3), accepted: an absolute address is never in the stack -------------------------------
        // ReturnCheck, like the engine's own stack analysis, takes a store through a constant address to land
        // outside the function's frame. So a callee that overwrites its own saved r4 through the constant
        // address its push slot has when it is entered with r15 = 0x0600F000 is proved to keep r4, and the
        // caller is lent that. The run returns 7. Kept as a pinned limit (this is the one place the oracle is
        // not asked to agree): the stack is ordinary work RAM at an address the program chose, but a store to
        // a fixed address that is also a stack slot is a coincidence of one entry state, and inside one function
        // the engine makes the same assumption (DECOMPILER_GUIDE.md).
        {
            const CallFixture g = { "absolute store over the callee's saved register", base,
                                    withCallee({ 0x4F22, 0xB00D, 0x0009, 0x6043, 0x4F26, 0x000B, 0x0009 },
                                               { 0x2F46, 0xD103, 0xE207, 0x2122, 0x64F6, 0x000B, 0x0009, 0x0009, 0x0600, 0xEFF8 }) };
            auto snap = CallSnapshot(g, ++serial);
            const DecompileResult callee = d.Decompile(snap, Request(0x06009020u));
            DecompileRequest q = Request(g.base);
            q.prototypes.emplace_back(0x06009020u, callee.prototype);
            const DecompileResult caller = d.Decompile(snap, q);
            Print("absolute store over the callee's saved register", caller);
            CHECK(callee.prototype.known && ((callee.prototype.effects.preserved >> 4) & 1u));
            CHECK(Has(caller.Text(), "return param_1;") && caller.prototype.known);
            uint32_t ran = 0;
            CHECK(CallRunResult(snap, g, inputs[0], ran) && ran == 7u);   // not param_1: the accepted limit
        }
        // ---- A3-13 (round 5): a value put in r0 before a call to a callee that keeps r0 -----------------------------
        // The engine kills r0 at every call, so the copy before the call is dead and the return is dropped; ReturnCheck sees
        // r0 as the function's own value (a parameter, a constant, a sum), and rule (b) asked only registers no signature
        // passes. A value this function put in r0 and a callee left there is a return the engine does not see: withheld.
        // Compiled code does it as scratch (cmp/eq #imm,r0 before a call): the cost is in the corpus numbers.
        {
            struct Carried
            {
                const char* what;
                std::vector<uint16_t> caller;
            };
            const Carried cases[] = {
                { "r4 copied to r0 before a call that keeps r0", { 0x4F22, 0x6043, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 } },
                { "r5 copied to r0 before the call", { 0x4F22, 0x6053, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 } },
                { "a constant put in r0 before the call", { 0x4F22, 0xE005, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 } },
                { "r4 + r5 in r0, the sum finished in the delay slot", { 0x4F22, 0x6043, 0xB00C, 0x305C, 0x4F26, 0x000B, 0x0009 } },
            };
            for (const Carried& c : cases)
            {
                const CallFixture g = { c.what, base, withCallee(c.caller, { 0x000B, 0x0009 }) };
                const Lent lg = lend(g, true, false);
                CHECK(lg.callee.prototype.known);
                CHECK(!lg.caller.prototype.known && Has(lg.caller.prototypeWithheld, "across a call that keeps r0"));
                CHECK(!PseudoAgreesWithRun(lg.caller, CallSnapshot(g, ++serial), g, inputs));   // the text returns nothing, the code r4 / r5 / 5 / r4 + r5
            }
            // Controls. A callee that does not keep r0 (it sets it): the engine sees the callee's value, and the caller is lent.
            const CallFixture sets = { "r0 set by the callee after the caller's copy", base,
                                       withCallee({ 0x4F22, 0x6043, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0xE005, 0x000B, 0xE007 }) };
            // (mov #5,r0 ; rts ; mov #7,r0: the callee's r0 is 7)
            CHECK(lend(sets).caller.prototype.known);
            // The value put in r0 after the call is the engine's own return.
            const CallFixture after = { "r4 copied to r0 after the call", base,
                                         withCallee({ 0x4F22, 0xB00D, 0x0009, 0x6043, 0x4F26, 0x000B, 0x0009 }, { 0x000B, 0x0009 }) };
            CHECK(lend(after).caller.prototype.known);
            // `jsr @r0`: compiled code leaves the callee's address in r0 and the callee that keeps r0 leaves it there. Not a
            // return: lent. (mov.l @(3,pc),r0 ; jsr @r0 ; nop ; ... the literal is the callee's address.)
            const CallFixture jsr = { "callee's address in r0 for jsr @r0", base,
                                      withCallee({ 0x4F22, 0xD003, 0x400B, 0x0009, 0x4F26, 0x000B, 0x0009, 0x0009, 0x0600, 0x9020 }, { 0x000B, 0x0009 }) };
            auto snapJ = CallSnapshot(jsr, ++serial);
            const DecompileResult calleeJ = d.Decompile(snapJ, Request(0x06009020u));
            DecompileRequest qj = Request(jsr.base);
            qj.prototypes.emplace_back(0x06009020u, calleeJ.prototype);
            const DecompileResult callerJ = d.Decompile(snapJ, qj);
            Print(jsr.name.c_str(), callerJ);
            CHECK(calleeJ.prototype.known && callerJ.prototype.known);
        }
        // ---- A3-14 (round 5): a join of entry values the engine drops -------------------------------------------------
        // `phi(in_sr, in_sr | 0xf0)` and `phi(in_r0, in_r0 + 1)` print as no return at all (the engine takes a return made only of
        // inputs no signature passes for none), and the function was lent `void f(void)`, losing r4, which chooses the path.
        // The SR shape is what `sr = get_cr(); if (!x) sr &= 0xf0; return sr;` compiles to. Withheld.
        {
            struct Joined
            {
                const char* what;
                std::vector<uint16_t> code;
                const char* reg;
            };
            const Joined cases[] = {
                { "SR or'ed with 0xf0 on one path", { 0x0002, 0x4411, 0x8900, 0xCBF0, 0x000B, 0x0009 }, "sr" },
                { "SR and'ed with 0xf0 on one path", { 0x0002, 0x2448, 0x8900, 0xC9F0, 0x000B, 0x0009 }, "sr" },
                { "r0 incremented on one path", { 0x2448, 0x8900, 0x7001, 0x000B, 0x0009 }, "r0" },
            };
            for (const Joined& c : cases)
            {
                const CallFixture g = { c.what, base, c.code };
                auto snap = CallSnapshot(g, ++serial);
                const DecompileResult r = d.Decompile(snap, Request(g.base));
                Print(g.name.c_str(), r);
                CHECK(r.status == DecompileStatus::Ok);
                CHECK(!r.prototype.known && Has(r.prototypeWithheld, (std::string("computed from ") + c.reg + " on entry").c_str()));
                CHECK(!PseudoAgreesWithRun(r, snap, g, inputs));   // the engine's text returns nothing
            }
            // A path that leaves r0 alone and another that sets it to a constant (`phi(in_r0, 5)`) chosen by r4, which the lent `void f(void)`
            // drops: withheld since area 3 round 9 (A3-62; it was accepted as the scratch r0 of compiled code here, but the choice is a read of
            // r4 that the signature loses, and a compiled function that tests r4 holds it: 0 corpus functions withheld for it). Chosen by a
            // constant it stays lent (A3-25), and so does the same join when the signature holds r4 (TestArea3Round9Fix). The interrupt-mask
            // idiom's plain transform of SR left in r0 (06010054) is not a join, and stays lent.
            const CallFixture alone = { "r0 left alone on one path, set to a constant on the other", base, { 0x2448, 0x8900, 0xE005, 0x000B, 0x0009 } };
            auto snapA = CallSnapshot(alone, ++serial);
            const DecompileResult ra = d.Decompile(snapA, Request(alone.base));
            Print(alone.name.c_str(), ra);
            CHECK(!ra.prototype.known && Has(ra.prototypeWithheld, "its return is chosen by r4 on entry, which the signature does not hold"));
            CHECK(!PseudoAgreesWithRun(ra, snapA, alone, inputs));   // `return;` where the code returns 5 or r0's entry value
        }
        // ---- A3-16 (round 5): a loop that moves a stack address down -----------------------------------------------
        // A3-9 made a join of two stack addresses keep the lower base; a loop that lowers an address each pass never
        // settled, ReturnCheck ran to its step limit (3.4 s for 11 instructions) and withheld the function "too many
        // paths", and the probe for a frame address in r2 never got past the loop, so the alias was not declared and
        // the read after the call was folded (`return 10;` where the code returns 12). After a few growths at one
        // address the bases there are given up for the frame's lowest slot.
        {
            // a: a leaf that clears four words of its frame with `mov.l r0,@-r1`. Lent in milliseconds.
            const CallFixture a = { "loop that moves a stack address down", base,
                                    { 0x7FF0, 0x61F3, 0x7110, 0xE204, 0xE000, 0x4210, 0x8FFD, 0x2106, 0x7F10, 0x000B, 0x0009 } };
            auto snapA = CallSnapshot(a, ++serial);
            const DecompileResult ra = d.Decompile(snapA, Request(a.base));
            Print(a.name.c_str(), ra);
            CHECK(ra.status == DecompileStatus::Ok);
            CHECK(ra.prototype.known && ra.prototypeWithheld.empty());
            CHECK(ra.elapsedMs < 1000);   // 3,393 ms and withheld before
            // b: the frame of A3-9's two words, r2 walked down from r15 + 8 to r15 by a loop, then passed to a callee
            // that writes through it. The callee reads r2 (not lent), so the alias has to be declared for the read
            // after the call not to fold.
            std::vector<uint16_t> bw = { 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0x1F01, 0x62F3, 0x7208, 0xE102, 0x4110, 0x8FFD, 0x72FC, 0xB013,
                                         0x0009, 0x60F2, 0x51F1, 0x301C, 0x7F08, 0x4F26, 0x000B, 0x0009 };
            bw.resize(32, 0x0009);
            bw.insert(bw.end(), { 0xE107, 0x000B, 0x2212 });   // g: mov #7,r1 ; rts ; mov.l r1,@r2
            const CallFixture b = { "frame address walked down by a loop, then passed in r2", base, bw };
            auto snapB = CallSnapshot(b, ++serial);
            const DecompileResult rb = d.Decompile(snapB, Request(b.base));
            Print(b.name.c_str(), rb);
            CHECK(rb.status == DecompileStatus::Ok);
            CHECK(rb.elapsedMs < 1000);   // 5,223 ms before
            CHECK(!Has(rb.Text(), "return 10;"));   // the code returns 12
            uint32_t ran = 0;
            CHECK(CallRunResult(snapB, b, inputs[0], ran) && ran == 12u);
            CHECK(Has(rb.Text(), "iStack_c + iStack_8"));   // both words read after the call (the evaluator reads no loop)
            // Control: the same walk upward, from r15 - 8 to r15, converged before and still does.
            std::vector<uint16_t> cw = bw;
            cw[6] = 0x72F8;    // add #-8,r2
            cw[10] = 0x7204;   // add #4,r2
            const CallFixture c = { "frame address walked up by a loop, then passed in r2", base, cw };
            auto snapC = CallSnapshot(c, ++serial);
            const DecompileResult rc = d.Decompile(snapC, Request(c.base));
            Print(c.name.c_str(), rc);
            CHECK(rc.elapsedMs < 1000 && !Has(rc.Text(), "return 10;") && Has(rc.Text(), "iStack_c + iStack_8"));
        }
        // ---- A3-17 (round 5): a negative or a subtracted index from a frame address ---------------------------------
        // Round 4 took an index added to a frame address as not negative, and dropped the base for a subtracted
        // register, so `p - i` and `p + (-1)` from the address of the second local reached the one below it, which the
        // declared alias did not cover: the read after the call was folded (`return 5;` where the code returns 7).
        // An index that is not a constant gives up the base for the frame's lowest slot (p - i, and p + i for an i that may
        // be negative, are ordinary C when p points into an array).
        {
            const std::vector<uint16_t> callee = { 0xE107, 0x000B, 0x2212 };   // g: mov #7,r1 ; rts ; mov.l r1,@r2
            for (const bool subtract : { false, true })
            {
                const CallFixture g = { subtract ? "frame address minus an index, in r2" : "frame address plus a negative index, in r2", base,
                                        withCallee({ 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0x1F01, 0x62F3, 0x7204, uint16_t(subtract ? 0x3248 : 0x324C), 0xB006,
                                                     0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009 }, callee) };
                // add: r2 = r15 + 4 + r4, so r4 = -4 reaches the word at r15 (the code returns 7); sub: r2 = r15 + 4 - r4, so
                // r4 = 4 does.
                const std::vector<std::vector<uint32_t>> reach = { { 0 }, { subtract ? 4u : 0xFFFFFFFCu } };
                auto snap = CallSnapshot(g, ++serial);
                const DecompileResult r = d.Decompile(snap, Request(g.base));
                Print(g.name.c_str(), r);
                CHECK(r.status == DecompileStatus::Ok);
                CHECK(!Has(r.Text(), "return 5;"));
                CHECK(PseudoAgreesWithRun(r, snap, g, reach));
                uint32_t ran = 0;
                CHECK(CallRunResult(snap, g, reach[1], ran) && ran == 7u);
                CHECK(!r.prototype.known);   // the callee reads r2: withheld, and so is the caller
            }
        }
        // ==== Area 3, round 6 (A3-18 .. A3-28) =========================================================================
        // Decompile 'calleeAt' functions in the order given, each lent to the next, then the entry; the results in that order.
        auto lendAll = [&](const CallFixture& f, const std::vector<uint32_t>& callees) {
            auto snap = CallSnapshot(f, ++serial);
            std::vector<DecompileResult> out;
            std::vector<std::pair<uint32_t, Prototype>> protos;
            for (uint32_t at : callees)
            {
                DecompileRequest q = Request(at);
                q.prototypes = protos;
                out.push_back(d.Decompile(snap, q));
                protos.emplace_back(at, out.back().prototype);
            }
            DecompileRequest q = Request(f.base);
            q.prototypes = protos;
            out.push_back(d.Decompile(snap, q));
            Print(f.name.c_str(), out.back());
            CHECK(out.back().status == DecompileStatus::Ok);
            return out;
        };
        auto agrees = [&](const DecompileResult& r, const CallFixture& f, const std::vector<std::vector<uint32_t>>& in) {
            return PseudoAgreesWithRun(r, CallSnapshot(f, ++serial), f, in);
        };
        const std::vector<uint16_t> keeper = { 0x000B, 0x0009 };   // g: rts ; nop
        const char* const kAcross = "across a call that keeps r0";

        // ---- A3-18: a value the delay slot of `jsr @r0` puts in r0 is no call target ----------------------------------------
        // `callTarget` exempted whatever r0 held after the slot; the target is the copy made before it. A value moved into r0 in
        // the slot and held across a callee that keeps r0 is a return the engine's call killed.
        {
            struct Case
            {
                const char* what;
                uint16_t slot;
                bool lent;
            };
            for (const Case& c : { Case{ "a3_18 a: mov r4,r0 in the slot of jsr @r0", 0x6043, false },
                                   Case{ "a3_18 b: mov #5,r0 in the slot of jsr @r0", 0xE005, false },
                                   Case{ "a3_18 control: nop in the slot (r0 stays the callee's address)", 0x0009, true } })
            {
                const CallFixture g = { c.what, base,
                                        withCallee({ 0x4F22, 0xD003, 0x400B, c.slot, 0x4F26, 0x000B, 0x0009, 0x0009, 0x0600, 0x9020 }, keeper), c.lent };
                const Lent lg = lend(g, true, false);
                CHECK(lg.callee.prototype.known);
                if (c.lent)
                {
                    CHECK(lg.caller.prototype.known);
                    CHECK(PseudoAgreesWithRun(lg.caller, CallSnapshot(g, ++serial), g, inputs));
                }
                else
                {
                    CHECK(!lg.caller.prototype.known && Has(lg.caller.prototypeWithheld, kAcross));
                    CHECK(!PseudoAgreesWithRun(lg.caller, CallSnapshot(g, ++serial), g, inputs));   // the text returns nothing; the code r4 / 5
                }
            }
        }

        // ---- A3-19: a value a call returned in r0, held across a second call that keeps r0 -------------------------------------
        {
            const std::vector<uint16_t> h = { 0x000B, 0xE007 }, g = { 0x000B, 0x0009 };
            auto two = [&](std::vector<uint16_t> caller) {
                caller.resize(16, 0x0009);
                caller.insert(caller.end(), h.begin(), h.end());
                caller.resize(24, 0x0009);
                caller.insert(caller.end(), g.begin(), g.end());
                return caller;
            };
            const CallFixture a = { "a3_19 a: h returns 7 in r0, g keeps r0", base, two({ 0x4F22, 0xB00D, 0x0009, 0xB013, 0x0009, 0x4F26, 0x000B, 0x0009 }) };
            const std::vector<DecompileResult> ra = lendAll(a, { 0x06009020u, 0x06009030u });
            CHECK(ra[0].prototype.known && ra[0].prototype.ret.kind != TypeKind::Void && ra[1].prototype.known);
            CHECK(!ra[2].prototype.known && Has(ra[2].prototypeWithheld, kAcross));
            CHECK(!agrees(ra[2], a, inputs));   // `return;` where the code returns 7
            // Control: the value moved to a saved register across g is the engine's own return.
            const CallFixture c = { "a3_19 control: the value kept in r8 across g", base,
                                    two({ 0x2F86, 0x4F22, 0xB00C, 0x0009, 0x6803, 0xB011, 0x0009, 0x6083, 0x4F26, 0x000B, 0x68F6 }) };
            const std::vector<DecompileResult> rc = lendAll(c, { 0x06009020u, 0x06009030u });
            CHECK(rc[2].prototype.known && Has(rc[2].Text(), "return xVar1;"));
            CHECK(agrees(rc[2], c, inputs));
        }

        // A3-19, pinned: a result some instruction has read (copied to r1) before the call that keeps r0 is not carried: compiled code
        // leaves it in r0 as scratch (the corpus's void callers of a bool function: 2 and 6 more would be withheld).
        {
            const std::vector<uint16_t> h = { 0x000B, 0xE007 }, g = { 0x000B, 0x0009 };
            std::vector<uint16_t> w = { 0x4F22, 0xB00D, 0x0009, 0x6103, 0xB012, 0x0009, 0x4F26, 0x000B, 0x0009 };
            w.resize(16, 0x0009);
            w.insert(w.end(), h.begin(), h.end());
            w.resize(24, 0x0009);
            w.insert(w.end(), g.begin(), g.end());
            const CallFixture used = { "a3_19 control: the result was read before the second call", base, w };
            const std::vector<DecompileResult> ru = lendAll(used, { 0x06009020u, 0x06009030u });
            CHECK(ru[2].prototype.known && ru[2].prototype.ret.kind == TypeKind::Void);
        }

        // ---- A3-20: a join of r0's entry value with a parameter -------------------------------------------------------------
        // `int f(int a, int b) { if (b) return a; }`: the engine takes the join for no return and drops both parameters.
        {
            const std::vector<std::vector<uint32_t>> both = { { 0x11111111u, 0 }, { 0x11111111u, 1 } };
            const CallFixture a = { "a3_20 a: tst r5,r5 ; bt ; mov r4,r0", base, { 0x2558, 0x8900, 0x6043, 0x000B, 0x0009 } };
            auto snap = CallSnapshot(a, ++serial);
            const DecompileResult r = d.Decompile(snap, Request(a.base));
            Print(a.name.c_str(), r);
            CHECK(r.status == DecompileStatus::Ok);
            CHECK(!r.prototype.known && Has(r.prototypeWithheld, "joins r0's entry value with a parameter"));
            CHECK(!PseudoAgreesWithRun(r, snap, a, both));   // `return;` where the code returns r4 / r0's entry value
            // Controls. A parameter copied to r0 on every path is the engine's return; a constant on the other path, pinned in A3-14.
            const CallFixture every = { "a3_20 control: r4 copied to r0 on both paths", base, { 0x2558, 0x8902, 0x6043, 0x000B, 0x0009, 0x6053, 0x000B, 0x0009 } };
            auto snapE = CallSnapshot(every, ++serial);
            const DecompileResult re = d.Decompile(snapE, Request(every.base));
            Print(every.name.c_str(), re);
            CHECK(re.prototype.known && re.prototype.ret.kind != TypeKind::Void);
            // Accepted, pinned: the same join where the signature holds the parameter (r4 and r5 are read by the store and the test,
            // r6 is the pointer): compiled code leaves exactly this as scratch in r0 (four functions of image 008), so it stays lent
            // void. The rule is about a parameter the signature drops.
            const CallFixture held = { "a3_20 control: the parameter is held by the signature", base,
                                       { 0x2642, 0x2558, 0x8900, 0x6043, 0x000B, 0x0009 } };
            auto snapH = CallSnapshot(held, ++serial);
            const DecompileResult rh = d.Decompile(snapH, Request(held.base));
            Print(held.name.c_str(), rh);
            CHECK(rh.prototype.known && rh.prototype.ret.kind == TypeKind::Void && rh.prototype.params.size() >= 2);
        }

        // ---- A3-21: a callee that hands a value back through its caller's stack slot ----------------------------------------------
        // A3-15 went by the sign of the r15 change; the store decides. Each callee is withheld, and so is its caller, whose text
        // (the value written before the call, `return 5;`) the oracle rejects.
        {
            struct Case
            {
                const char* what;
                std::vector<uint16_t> caller, callee;
            };
            const std::vector<uint16_t> pr14 = { 0x2FE6, 0x0E2A, 0x7FFC, 0xE005, 0x2F02, 0xB009, 0x0009, 0x60F2, 0x7F04, 0x4E2A, 0x000B, 0x6EF6 };
            const std::vector<uint16_t> pr8 = { 0x082A, 0x7FFC, 0xE005, 0x2F02, 0xB00A, 0x0009, 0x60F2, 0x7F04, 0x4F26, 0x000B, 0x0009 };
            for (const Case& c : { Case{ "a3_21 a: rts ; mov.l r5,@r15", pr14, { 0x000B, 0x2F52 } },
                                   Case{ "a3_21 b: pop then push, difference 0", { 0x082A, 0x7FFC, 0xE005, 0x2F02, 0xB00A, 0x0009, 0x60F2, 0x7F04, 0x482A, 0x000B, 0x0009 }, { 0x61F6, 0x000B, 0x2F56 } },
                                   Case{ "a3_21 c: mov.l r5,@(4,r15)", { 0x082A, 0x7FF8, 0xE005, 0x2F02, 0x1F01, 0xB009, 0x0009, 0x50F1, 0x7F08, 0x482A, 0x000B, 0x0009 }, { 0x000B, 0x1F51 } } })
            {
                const CallFixture g = { c.what, base, withCallee(c.caller, c.callee) };
                const Lent lg = lend(g, true, false);
                CHECK(!lg.callee.prototype.known && Has(lg.callee.prototypeWithheld, "caller's frame"));
                CHECK(!lg.caller.prototype.known && Has(lg.caller.prototypeWithheld, "not known"));
                // The caller's text read the local as what it stored before the call (`return 5;`) until area 3 round 9 (A3-61): the probe reads
                // the callee's stores of its caller's words, the local is not folded and the note says why.
                CHECK(Has(lg.caller.Text(), "a local at or above the stack pointer is read after this call") && !Has(lg.caller.Text(), "return 5;"));
                CHECK(PseudoAgreesWithRun(lg.caller, CallSnapshot(g, ++serial), g, inputs, true));
            }
            (void)pr8;
            // Controls. A callee that spills a parameter its signature holds into its caller's home area (`mov r4,r0 ; rts ;
            // mov.l r4,@r15`; one function of image 007 spills r7 so) is lent: no caller reads the word back.
            const CallFixture spill = { "a3_21 control: an argument register spilled to the caller's home area", base,
                                        withCallee({ 0x4F22, 0xB00D, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0x6043, 0x000B, 0x2F42 }) };
            const Lent ls = lend(spill, true, false);
            // (This caller pushes PR and calls with r15 at it: the callee's store at its entry r15 lands on the saved PR, and the caller is
            // withheld "a return not to its caller", rightly. One that reserved the home area is lent, below: A3-31.)
            CHECK(ls.callee.prototype.known && ls.callee.prototype.params.size() == 1);
            CHECK(!ls.caller.prototype.known && Has(ls.caller.prototypeWithheld, "not to its caller"));
            // A3-31: the same callee called by a function that reserved the word (sts.l pr,@-r15 ; add #-4,r15): the store lands in the
            // reserved word, the saved PR above it is untouched, and the caller is lent. The callee's words are what CallEffects::frameWords
            // names, not every slot from the call's r15 up.
            const std::vector<uint16_t> reserve = { 0x4F22, 0x7FFC, 0xB00C, 0x0009, 0x7F04, 0x4F26, 0x000B, 0x0009 };
            const CallFixture homed = { "a3_31 a: a callee that spills r4 into the home area its caller reserved", base, withCallee(reserve, { 0x6043, 0x000B, 0x2F42 }) };
            const Lent lh = lend(homed);
            CHECK(lh.callee.prototype.known && lh.callee.prototype.effects.frameWords == 1u);
            CHECK(lh.caller.prototype.known && lh.caller.prototypeWithheld.empty());
            // ...a second word (r5 at 4(r15)) is the saved PR: withheld, and the oracle rejects the lent text if it were given.
            const CallFixture two = { "a3_31 b: a callee that spills r4 and r5, over the saved PR", base,
                                      withCallee(reserve, { 0x6043, 0x305C, 0x2F42, 0x000B, 0x1F51 }) };   // mov r4,r0 ; add r5,r0 ; mov.l r4,@r15 ; rts ; mov.l r5,@(4,r15)
            const Lent l2 = lend(two, true, false);
            CHECK(l2.callee.prototype.known && l2.callee.prototype.effects.frameWords == 3u);
            CHECK(!l2.caller.prototype.known && Has(l2.caller.prototypeWithheld, "not to its caller"));
            // ...and a callee that stores at an offset it cannot place says so: the caller clobbers from its r15 up, as before.
            const CallFixture any = { "a3_31 c: a callee that stores through r15 plus an index", base, withCallee(reserve, { 0x6063, 0x000B, 0x0F56 }) };
            const Lent la = lend(any, true, false);
            CHECK(!la.callee.prototype.known || (la.callee.prototype.effects.frameWords & CallEffects::kFrameAny));
            // ...and a callee that stores a constant there is not.
            const CallFixture konst = { "a3_21: a constant stored at the callee's entry r15", base,
                                        withCallee({ 0x4F22, 0xB00D, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0xE105, 0x000B, 0x2F12 }) };
            CHECK(!lend(konst, true, false).callee.prototype.known);
            // Control: a callee that stores below its entry r15 (its own frame) and returns is lent.
            const CallFixture own = { "a3_21 control: the callee's own frame", base,
                                      withCallee({ 0x4F22, 0xB00D, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0x7FFC, 0x2F52, 0x000B, 0x7F04 }) };
            CHECK(lend(own, true, false).callee.prototype.known);
        }

        // ---- A3-22: a frame address plus an index, or a walk, in r4 for a lent callee that writes through it --------------------
        // The caller keeps PR in r8 (the generator's shape), so the callee's write does not reach a saved PR (A3-24).
        {
            const std::vector<uint16_t> writer = { 0xE107, 0x000B, 0x2412 };   // g: mov #7,r1 ; rts ; mov.l r1,@r4
            for (const bool subtract : { false, true })
            {
                const CallFixture g = { subtract ? "a3_22 b: frame address minus an index in r4" : "a3_22 a: frame address plus a negative index in r4", base,
                                        withCallee({ 0x082A, 0x7FF8, 0xE005, 0x2F02, 0x1F01, 0x64F3, 0x7404, uint16_t(subtract ? 0x3458 : 0x345C), 0xB006,
                                                     0x0009, 0x60F2, 0x7F08, 0x482A, 0x000B, 0x0009 }, writer) };
                const std::vector<std::vector<uint32_t>> reach = { { 0, 0 }, { 0, subtract ? 4u : 0xFFFFFFFCu } };
                const Lent lg = lend(g, true, false);
                CHECK(lg.callee.prototype.known && ((lg.callee.prototype.effects.storesThrough >> 4) & 1u));
                CHECK(lg.caller.prototype.known);   // lent, and not folded
                CHECK(!Has(lg.caller.Text(), "return 5;"));
                CHECK(PseudoAgreesWithRun(lg.caller, CallSnapshot(g, ++serial), g, reach));
                uint32_t ran = 0;
                CHECK(CallRunResult(CallSnapshot(g, ++serial), g, reach[1], ran) && ran == 7u);
            }
            // c: A3-16 b's walk, in r4 (the evaluator reads no `do` loop: the text and the run).
            const CallFixture c = { "a3_22 c: frame address walked down by a loop, in r4", base,
                                    [&] {
                                        std::vector<uint16_t> w = { 0x082A, 0x7FF8, 0xE005, 0x2F02, 0x1F01, 0x64F3, 0x7408, 0xE102, 0x4110, 0x8FFD, 0x74FC,
                                                                    0xB013, 0x0009, 0x60F2, 0x51F1, 0x301C, 0x7F08, 0x482A, 0x000B, 0x0009 };
                                        w.resize(32, 0x0009);
                                        w.insert(w.end(), writer.begin(), writer.end());
                                        return w;
                                    }() };
            auto snapC = CallSnapshot(c, ++serial);
            DecompileRequest qc = Request(c.base);
            const DecompileResult calleeC = d.Decompile(snapC, Request(0x06009040u));
            qc.prototypes.emplace_back(0x06009040u, calleeC.prototype);
            const DecompileResult rc = d.Decompile(snapC, qc);
            Print(c.name.c_str(), rc);
            CHECK(rc.prototype.known && !Has(rc.Text(), "return 10;") && Has(rc.Text(), "iStack_8 + iStack_4"));
            uint32_t ranC = 0;
            CHECK(CallRunResult(snapC, c, inputs[0], ranC) && ranC == 12u);
            // Controls: the exact address and a walk upward were right before and are now.
            const CallFixture exact = { "a3_22 control: the exact address of the second local", base,
                                        withCallee({ 0x082A, 0x7FF8, 0xE005, 0x2F02, 0x1F01, 0x64F3, 0x7404, 0x0009, 0xB006, 0x0009, 0x50F1, 0x7F08, 0x482A, 0x000B, 0x0009 }, writer) };
            const Lent le = lend(exact);
            CHECK(le.caller.prototype.known && Has(le.caller.Text(), "return xStack_4;"));
        }

        // ---- A3-23: the address of a stack parameter passed to a lent callee ------------------------------------------------------
        // The engine prints the parameter as the variable the callee changed (`g(&param_5); return param_5 + ...`, and a copy taken before
        // the call keeps the old value), which is right C. The oracle read it as the word the run started with: a false positive,
        // fixed in PseudoEval (a name whose address the text takes is unknown after a call). Probed on the engine: the read after the
        // call is not folded when the parameter was assigned before the call or read into a copy before it.
        {
            const std::vector<uint16_t> writer = { 0xE107, 0x000B, 0x2412 };
            const std::vector<std::vector<uint32_t>> five = { { 0, 1, 2, 3, 0 } };
            const CallFixture a = { "a3_23 a: g(&param_5); return param_5 + param_2 + param_3 + param_4", base,
                                    withCallee({ 0x082A, 0x64F3, 0xB00C, 0x0009, 0x60F2, 0x305C, 0x306C, 0x307C, 0x482A, 0x000B, 0x0009 }, writer) };
            const Lent la = lend(a, true, false);
            // (Since A3-39 the caller is withheld: the address of its stack parameter handed to a writer is a word of its caller's frame that
            // the writer stores to. The text, which is what this block is about, is the same.)
            CHECK(!la.caller.prototype.known && Has(la.caller.prototypeWithheld, "caller's frame") && Has(la.caller.Text(), "&param_5"));
            uint32_t ran = 0;
            CHECK(CallRunResult(CallSnapshot(a, ++serial), a, five[0], ran) && ran == 0xDu);
            CHECK(agrees(la.caller, a, five));
            // The value before the call, read into a copy, and used after: the copy keeps the old word.
            const CallFixture b = { "a3_23 b: the stack word read before the call and used after it", base,
                                    withCallee({ 0x082A, 0x59F0, 0x64F3, 0xB00B, 0x0009, 0x60F2, 0x309C, 0x305C, 0x306C, 0x307C, 0x482A, 0x000B, 0x0009 }, writer) };
            const Lent lb = lend(b, true, false);
            CHECK(!lb.caller.prototype.known && Has(lb.caller.prototypeWithheld, "caller's frame") && Has(lb.caller.Text(), "= param_5;"));
            CHECK(agrees(lb.caller, b, five));
            // The evaluator itself.
            std::string why;
            const PseudoEval::Entry none;
            CHECK(MayReturn("int f(int param_1,int param_2)\n{\n  func_0x06009020(&param_2);\n  return param_2;\n}", { 1, 2 }, 9, why));
            CHECK(!MayReturn("int f(int param_1,int param_2)\n{\n  return param_2;\n}", { 1, 2 }, 9, why));
            (void)none;
        }

        // ---- A3-24: a compiled `f(&local)` to a lent callee that stores through it ------------------------------------------------
        // The callee's store through a pointer to a local does not reach the caller's saved PR (the engine assumes as much): lent,
        // where "a return not to its caller" withheld 96 and 125 corpus functions.
        {
            const CallFixture g = { "a3_24: g(&local) with PR pushed, g stores through it", base,
                                    withCallee({ 0x4F22, 0x7FFC, 0xE005, 0x2F02, 0x64F3, 0xB009, 0x0009, 0x60F2, 0x7F04, 0x4F26, 0x000B, 0x0009 }, { 0xE107, 0x000B, 0x2412 }) };
            const Lent lg = lend(g);
            CHECK(lg.callee.prototype.known && lg.caller.prototype.known);
            CHECK(Has(lg.caller.Text(), "return xStack_8;") && Has(lg.caller.Text(), "&xStack_8"));
            // The word that holds the saved PR is not spared when the callee is handed its address: withheld, as before.
            const CallFixture pr = { "a3_24: the callee is handed the address of the saved PR", base,
                                     withCallee({ 0x4F22, 0x64F3, 0x7404, 0x7404, 0xB00B, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0xE107, 0x000B, 0x2412 }) };
            // (r4 = r15 + 8: not the slot; r4 = r15 - 4 + ... below)
            const CallFixture pr2 = { "a3_24: the callee writes the saved PR through the exact address of its slot", base,
                                      withCallee({ 0x4F22, 0x64F3, 0xB00C, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0xE107, 0x000B, 0x2412 }) };
            (void)pr;
            const Lent l2 = lend(pr2, true, false);
            CHECK(!l2.caller.prototype.known && Has(l2.caller.prototypeWithheld, "not to its caller"));
        }

        // ---- A3-25, accepted: r0 left alone on a path that never runs -------------------------------------------------------------
        // `phi(in_r0, 5)` is lent void (A3-14, pinned): neither the engine nor ReturnCheck folds a branch on a constant, and folding it
        // in ReturnCheck would not help (the engine has already decided the return was void from the join). The code returns 5.
        {
            const CallFixture a = { "a3_25: bt on a test of a constant", base, { 0xE101, 0x2118, 0x8900, 0xE005, 0x000B, 0x0009 } };
            auto snap = CallSnapshot(a, ++serial);
            const DecompileResult r = d.Decompile(snap, Request(a.base));
            Print(a.name.c_str(), r);
            CHECK(r.prototype.known && r.prototype.ret.kind == TypeKind::Void);   // the accepted limit
            uint32_t ran = 0;
            CHECK(CallRunResult(snap, a, inputs[0], ran) && ran == 5u);
        }

        // ---- A3-26: a frame address joined with a value that is no address ----------------------------------------------------------
        {
            const CallFixture g = { "a3_26: r2 = c ? &local : 0 for an unlent callee", base,
                                    withCallee({ 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0x1F01, 0xE200, 0x2448, 0x8900, 0x62F3, 0xB005, 0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009 },
                                               { 0xE107, 0x000B, 0x2212 }) };
            auto snap = CallSnapshot(g, ++serial);
            const DecompileResult r = d.Decompile(snap, Request(g.base));
            Print(g.name.c_str(), r);
            CHECK(r.status == DecompileStatus::Ok);
            CHECK(!Has(r.Text(), "return 5;"));
            CHECK(PseudoAgreesWithRun(r, snap, g, { { 1 } }));
            uint32_t ran = 0;
            CHECK(CallRunResult(snap, g, { 1 }, ran) && ran == 7u);
        }

        // ---- A3-27: a `jmp` tail call to a lent callee ---------------------------------------------------------------------------------
        // The engine turns the jump into a call and a return; ReturnCheck now does too. The caller returns r4 through a callee that keeps r0.
        {
            const CallFixture t = { "a3_27 a: mov r4,r0 ; jmp to a callee that keeps r0", base,
                                    withCallee({ 0x6043, 0xD102, 0x412B, 0x0009, 0x0009, 0x0009, 0x0600, 0x9020 }, keeper) };
            const Lent lt = lend(t, true, false);
            CHECK(lt.callee.prototype.known);
            CHECK(!lt.caller.prototype.known && Has(lt.caller.prototypeWithheld, kAcross));
            CHECK(!PseudoAgreesWithRun(lt.caller, CallSnapshot(t, ++serial), t, inputs));
            // Controls. The same through `bra` (followed into the callee's code): lent, right.
            const CallFixture bra = { "a3_27 control: bra", base, withCallee({ 0x6043, 0xA00D, 0x0009 }, keeper) };
            CHECK(lend(bra).caller.prototype.known);
            // A tail call to a callee that returns a value is lent returning it (below, A3-29); one with no value in r0 is lent void.
            const CallFixture val = { "a3_27 b: jmp to a callee that returns 7", base,
                                      withCallee({ 0xD102, 0x412B, 0x0009, 0x0009, 0x0009, 0x0009, 0x0600, 0x9020 }, { 0x000B, 0xE007 }) };
            const Lent lv = lend(val, true, false);
            // Until patch 0044 the engine printed the jump as `f(); return;` whatever the callee returned; since it (a RETURN of a
            // locked returning call's output is a return) the text returns the callee's value and the oracle accepts it (A3-29).
            // The caller is lent, returning it (A3-29: the host withheld it "which the engine takes for a call that returns nothing"
            // after the engine had stopped doing so).
            CHECK(lv.caller.prototype.known && lv.caller.prototype.ret.kind != TypeKind::Void && !Has(lv.caller.prototypeWithheld, "returns a value"));
            CHECK(PseudoAgreesWithRun(lv.caller, CallSnapshot(val, ++serial), val, inputs));
            const CallFixture none = { "a3_27 c: jmp to a callee that keeps r0, r0 untouched", base,
                                       withCallee({ 0xD102, 0x412B, 0x0009, 0x0009, 0x0009, 0x0009, 0x0600, 0x9020 }, keeper), true };
            CHECK(lend(none).caller.prototype.known);
            // A3-29: a tail call to a callee lent void whose r0 is live in the caller's caller. g stores 7 through r5 and leaves it in r0
            // (r0 has another use, so it is no return: lent void); f jumps to it through a literal; h calls f and returns r0. f is lent void,
            // as the code it jumps to is; h stores r0 after the call, a register f's signature does not return, and is withheld for that, not for f.
            {
                std::vector<uint16_t> w(0x30, 0x0009);
                const uint16_t h[] = { 0x4F22, 0xB00D, 0x0009, 0xD103, 0x2102, 0x4F26, 0x000B, 0x0009, 0x0009, 0x0009, 0x0600, 0x2000 };   // sts.l pr,@-r15 ; bsr f ; nop ; mov.l @(3,pc),r1 ; mov.l r0,@r1 ; lds.l @r15+,pr ; rts ; nop ; pool: 0x06002000
                const uint16_t f[] = { 0xD101, 0x412B, 0x0009, 0x0009, 0x0600, 0x9040 };   // mov.l @(1,pc),r1 ; jmp @r1 ; nop ; nop ; the pool: g
                const uint16_t g[] = { 0xE007, 0x2502, 0x000B, 0x0009 };                    // mov #7,r0 ; mov.l r0,@r5 ; rts ; nop
                for (size_t i = 0; i < 12; ++i) w[i] = h[i];
                for (size_t i = 0; i < 6; ++i) w[0x20 / 2 + i] = f[i];
                for (size_t i = 0; i < 4; ++i) w[0x40 / 2 + i] = g[i];
                const CallFixture t = { "a3_29 c: h calls f, which jumps to g (lent void, r0 = 7 live)", base, w, true };
                auto snap = CallSnapshot(t, ++serial);
                const DecompileResult rg = d.Decompile(snap, Request(0x06009040u));
                CHECK(rg.prototype.known && rg.prototype.ret.kind == TypeKind::Void);
                DecompileRequest qf = Request(0x06009020u);
                qf.prototypes.emplace_back(0x06009040u, rg.prototype);
                const DecompileResult rf = d.Decompile(snap, qf);
                Print(t.name.c_str(), rf);
                CHECK(rf.status == DecompileStatus::Ok && rf.prototype.known && rf.prototype.ret.kind == TypeKind::Void);
                DecompileRequest qh = Request(base);
                qh.prototypes.emplace_back(0x06009040u, rg.prototype);
                qh.prototypes.emplace_back(0x06009020u, rf.prototype);
                const DecompileResult rh = d.Decompile(snap, qh);
                Print("a3_29 c: the caller of f", rh);
                CHECK(rh.status == DecompileStatus::Ok && !rh.prototype.known && Has(rh.prototypeWithheld, "uses r0 after calling"));
            }
        }

        // ---- A3-28: the escape is in the text, and the oracle reads it -----------------------------------------------------------------
        // A local nothing in the text assigns, written by an unlent callee through r2: `func(); return xStack_8;` is right there and
        // wrong for a callee that pushed (A3-15). The engine puts a comment at the call when the host declared the escape, and the
        // evaluator reads the stack locals of a text with the comment as unknown.
        {
            const CallFixture s = { "a3_28: local written through r2 by an unlent callee", base,
                                    withCallee({ 0x4F22, 0x7FFC, 0x62F3, 0xB00B, 0x0009, 0x60F2, 0x7F04, 0x4F26, 0x000B, 0x0009 }, { 0xE107, 0x000B, 0x2212 }) };
            auto snap = CallSnapshot(s, ++serial);
            const DecompileResult r = d.Decompile(snap, Request(s.base));
            Print(s.name.c_str(), r);
            CHECK(Has(r.Text(), kFrameEscapeNote) || Has(r.Text(), "a frame address is in r2 here"));
            CHECK(Has(r.Text(), "func_0x06009020();") && Has(r.Text(), "return xStack_8;"));
            CHECK(PseudoAgreesWithRun(r, snap, s, inputs));   // the run returns 7
            // A text without the comment is read as before: the same body is wrong for A3-15 and stays rejected.
            PseudoEval::Entry e;
            e.regs = RunEntryRegisters({});
            e.snap = snap;
            e.sp = kRunStack;
            std::string why;
            const std::string body = "xunknown4 f(void)\n{\n  xunknown4 xStack_8;\n\n  func_0x06009020();\n  return xStack_8;\n}";
            CHECK(!MayReturn(body, {}, 7, why, true, &e));
            const std::string noted = "xunknown4 f(void)\n{\n  xunknown4 xStack_8;\n\n  /* a frame address is in r2 here, in none of the arguments: x */\n  func_0x06009020();\n  return xStack_8;\n}";
            CHECK(MayReturn(noted, {}, 7, why, true, &e));
            // The note is for a callee that may read r2: a `jsr @r0` through a literal to a lent callee (the probe cannot tell before the
            // literal folds) has none, and the same call to a callee that is not lent has it (A3-32).
            const CallFixture j = { "a3_28: jsr through a literal with a frame address in r2", base,
                                    withCallee({ 0x4F22, 0x7FFC, 0x62F3, 0xD003, 0x400B, 0x0009, 0x7F04, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9020 }, keeper) };
            const Lent lj = lend(j, true, false);
            CHECK(lj.caller.prototype.known && !Has(lj.caller.Text(), "a frame address is in r2 here"));
            const Lent lu = lend(j, false, false);
            CHECK(!lu.caller.prototype.known && Has(lu.caller.Text(), "a frame address is in r2 here"));
            // The parameter area: the same through a stack word at or above r15.
            const CallFixture k = { "a3_28: a stack parameter written through r2", base,
                                    withCallee({ 0x082A, 0xE905, 0x1F90, 0x62F3, 0xB00A, 0x0009, 0x60F2, 0x309C, 0x482A, 0x000B, 0x0009 }, { 0xE107, 0x000B, 0x2212 }) };
            auto snapK = CallSnapshot(k, ++serial);
            const DecompileResult rk = d.Decompile(snapK, Request(k.base));
            Print(k.name.c_str(), rk);
            CHECK(Has(rk.Text(), "a frame address is in r2 here") && Has(rk.Text(), "iStack00000000 + 5"));
            CHECK(PseudoAgreesWithRun(rk, snapK, k, { { 0, 1, 2, 3 } }));
        }
        // The evaluator reads an array local as memory (it cannot know what a call wrote to it).
        {
            std::string why;
            CHECK(MayReturn("int f(void)\n{\n  int axStack_c [2];\n\n  axStack_c[0] = 5;\n  func_0x06009020();\n  return axStack_c[0];\n}", {}, 7, why));
            CHECK(!MayReturn("int f(void)\n{\n  func_0x06009020();\n  return 5;\n}", {}, 7, why));
        }
        // ==== Area 3, round 7 (A3-36 .. A3-42) =========================================================================
        // The reviewer's fixtures, word for word (the ledger rows and "Area 3, round 7 review" in 15_DECOMPILER_STEP5.md). 'placed' puts a
        // callee at word 'at' of the caller's words (0x06009000 + 2 at); lendAll decompiles the callees in the order given and lends each.
        auto placed = [&](std::vector<uint16_t> w, size_t at, const std::vector<uint16_t>& callee) {
            w.resize(at, 0x0009);
            w.insert(w.end(), callee.begin(), callee.end());
            return w;
        };
        auto afterRun = [&](const CallFixture& f, const char* reg, const std::vector<uint32_t>& args, uint32_t& out) {
            RunMachine m(CallSnapshot(f, ++serial));
            for (const auto& kv : RunEntryRegisters(args)) m.Set(kv.first.c_str(), kv.second);
            if (!m.Run(f.base, kRunSentinel)) return false;
            out = m.Get(reg);
            return true;
        };
        auto returns = [&](const CallFixture& f, const std::vector<uint32_t>& args, uint32_t want) {
            uint32_t ran = 0;
            return CallRunResult(CallSnapshot(f, ++serial), f, args, ran) && ran == want;
        };
        const std::vector<uint16_t> writer4 = { 0xE107, 0x000B, 0x2412 };   // g: mov #7,r1 ; rts ; mov.l r1,@r4
        // r5 = 0: the code returns 5; r5 = -4 reaches the lower local: 7.
        const std::vector<std::vector<uint32_t>> reach = { { 0, 0 }, { 0, 0xFFFFFFFCu } };
        const char* const kHandedBack = "caller's frame";

        // ---- A3-36: the probe sees a call through a literal, and keeps the frame registers across an unresolved call ----------------
        // The probe runs on the first flow, where `jsr @r1` is a CALLIND with no known target: a lent callee was taken for any callee, and
        // the frame address plus an index handed to it was not declared an alias (J1, J2); after any such call r15 and r14 were 'returned'
        // values with no stack base, so the address made later was not seen either (K1).
        {
            struct Case
            {
                const char* what;
                std::vector<uint16_t> words;
                std::vector<uint32_t> callees;
                const char* read;
            };
            const std::vector<uint16_t> keep = { 0x000B, 0x0009 };
            for (const Case& c : { Case{ "a3_36 J1: PR in r8, jsr @r1 through a literal, frame address + index in r4",
                                         placed({ 0x082A, 0x7FF8, 0xE005, 0x2F02, 0x1F01, 0x64F3, 0x7404, 0x345C, 0xD103, 0x410B, 0x0009, 0x60F2, 0x7F08, 0x482A,
                                                  0x000B, 0x0009, 0x0600, 0x9040 }, 32, writer4),
                                         { 0x06009040u }, "return xStack_8;" },
                                   Case{ "a3_36 J2: the compiled prologue (sts.l pr,@-r15), jsr @r1",
                                         placed({ 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0x1F01, 0x64F3, 0x7404, 0x345C, 0xD103, 0x410B, 0x0009, 0x60F2, 0x7F08, 0x4F26,
                                                  0x000B, 0x0009, 0x0600, 0x9040 }, 32, writer4),
                                         { 0x06009040u }, "return xStack_c;" },
                                   Case{ "a3_36 K1: frame pointer, an earlier jsr @r1 to a lent callee, then bsr",
                                         placed(placed({ 0x2FE6, 0x4F22, 0x7FF8, 0x6EF3, 0xE005, 0x2E02, 0x1E01, 0xD107, 0x410B, 0x0009, 0x64E3, 0x7404, 0x345C,
                                                         0xB021, 0x0009, 0x60E2, 0x7E08, 0x6FE3, 0x4F26, 0x000B, 0x6EF6, 0x0009, 0x0600, 0x9040 }, 32, keep),
                                                48, writer4),
                                         { 0x06009040u, 0x06009060u }, "return xStack_10;" } })
            {
                const CallFixture f = { c.what, base, c.words };
                const std::vector<DecompileResult> out = lendAll(f, c.callees);
                const DecompileResult& r = out.back();
                CHECK(r.prototype.known && Has(r.Text(), c.read) && !Has(r.Text(), "return 5;"));   // 5 was the folded local
                CHECK(PseudoAgreesWithRun(r, CallSnapshot(f, ++serial), f, reach));
                CHECK(returns(f, reach[0], 5) && returns(f, reach[1], 7));
            }
            // Controls. The same with bsr (the engine's own call spec) printed the local read all along.
            const CallFixture j0 = { "a3_36 control J0: bsr", base,
                                     placed({ 0x082A, 0x7FF8, 0xE005, 0x2F02, 0x1F01, 0x64F3, 0x7404, 0x345C, 0xB016, 0x0009, 0x60F2, 0x7F08, 0x482A, 0x000B, 0x0009 },
                                            32, writer4) };
            const std::vector<DecompileResult> r0 = lendAll(j0, { 0x06009040u });
            CHECK(r0.back().prototype.known && Has(r0.back().Text(), "return xStack_8;") && agrees(r0.back(), j0, reach));
            // A lent callee that stores through nothing it is handed gets no alias, however it is called: the local folds, and is right.
            const CallFixture keepJsr = { "a3_36 control: a lent keeper through jsr @r1 is no alias", base,
                                          placed({ 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0x1F01, 0x64F3, 0x7404, 0x345C, 0xD103, 0x410B, 0x0009, 0x60F2, 0x7F08, 0x4F26,
                                                   0x000B, 0x0009, 0x0600, 0x9040 }, 32, keep) };
            const std::vector<DecompileResult> rk = lendAll(keepJsr, { 0x06009040u });
            CHECK(rk.back().prototype.known && Has(rk.back().Text(), "return 5;") && agrees(rk.back(), keepJsr, reach));
        }

        // ---- A3-37: the same for a caller that is withheld (nothing lent, so only the reader is misled) ---------------------------------
        {
            const std::vector<uint16_t> keep = { 0x000B, 0x0009 }, writer2 = { 0xE107, 0x000B, 0x2212 };   // g: mov #7,r1 ; rts ; mov.l r1,@r2
            struct Case
            {
                const char* what;
                std::vector<uint16_t> words;
                const char* read;   // null: the note only
            };
            const std::vector<std::vector<uint32_t>> any = { { 0 } };
            for (const Case& c : { Case{ "a3_37 K2: an earlier jsr @r1 to an unlent callee, then r2 = &local for an unlent writer",
                                         placed(placed({ 0x2FE6, 0x4F22, 0x7FFC, 0x6EF3, 0xE005, 0x2E02, 0xD105, 0x410B, 0x0009, 0x62E3, 0xB024, 0x0009, 0x60E2,
                                                         0x7E04, 0x6FE3, 0x4F26, 0x000B, 0x6EF6, 0x0600, 0x9040 }, 32, keep), 48, writer2),
                                         nullptr },
                                   Case{ "a3_37 K4: an earlier bsr to an unlent callee", placed(placed({ 0x2FE6, 0x4F22, 0x7FFC, 0x6EF3, 0xE005, 0x2E02, 0xB018, 0x0009,
                                                                                                    0x62E3, 0xB025, 0x0009, 0x60E2, 0x7E04, 0x6FE3, 0x4F26, 0x000B,
                                                                                                    0x6EF6 }, 32, keep), 48, writer2),
                                         nullptr },
                                   Case{ "a3_37 K3 control: no earlier call", placed({ 0x2FE6, 0x4F22, 0x7FFC, 0x6EF3, 0xE005, 0x2E02, 0x62E3, 0xB027, 0x0009, 0x60E2,
                                                                                      0x7E04, 0x6FE3, 0x4F26, 0x000B, 0x6EF6 }, 48, writer2),
                                         nullptr } })
            {
                const CallFixture f = { c.what, base, c.words };
                auto snap = CallSnapshot(f, ++serial);
                const DecompileResult r = d.Decompile(snap, Request(f.base));
                Print(f.name.c_str(), r);
                CHECK(r.status == DecompileStatus::Ok && !r.prototype.known);
                CHECK(Has(r.Text(), "a frame address is in r2 here"));
                CHECK(PseudoAgreesWithRun(r, snap, f, any));   // the run returns 7
                CHECK(returns(f, any[0], 7));
            }
            // b: a frame address plus an index, or minus one, in r4 for a callee that is not lent (the engine read the index as non-negative).
            for (const bool subtract : { false, true })
            {
                const CallFixture f = { subtract ? "a3_37 U2: frame address minus an index in r4, callee not lent" : "a3_37 U1: frame address plus a negative index in r4, callee not lent",
                                        base,
                                        withCallee({ 0x082A, 0x7FF8, 0xE005, 0x2F02, 0x1F01, 0x64F3, 0x7404, uint16_t(subtract ? 0x3458 : 0x345C), 0xB006, 0x0009,
                                                     0x60F2, 0x7F08, 0x482A, 0x000B, 0x0009 }, writer4) };
                const std::vector<std::vector<uint32_t>> in = { { 0, 0 }, { 0, subtract ? 4u : 0xFFFFFFFCu } };
                auto snap = CallSnapshot(f, ++serial);
                const DecompileResult r = d.Decompile(snap, Request(f.base));
                Print(f.name.c_str(), r);
                CHECK(r.status == DecompileStatus::Ok && !r.prototype.known);
                CHECK(!Has(r.Text(), "return 5;") && Has(r.Text(), "return xStack_8;"));
                CHECK(PseudoAgreesWithRun(r, snap, f, in));
                CHECK(returns(f, in[1], 7));
            }
        }

        // ---- A3-38: `jsr @r0` with a load in the slot: the load is not the call target --------------------------------------------------
        // A3-18 compared the value r0 held after the slot with the copy's; two memory loads are equal Values. The target is the copy while
        // nothing has written r0 since.
        {
            struct Case
            {
                const char* what;
                uint16_t slot;
                bool lent;
            };
            const std::vector<std::vector<uint32_t>> viaMemory = { { 0x11111111u, 0x06009040u, 0, 0 }, { 0, 0x06009042u, 0, 0 } };
            for (const Case& c : { Case{ "a3_38 P1: jsr @r0 ; mov.l @r5,r0", 0x6052, false }, Case{ "a3_38 P3: jsr @r0 ; mov.b @r5,r0", 0x6050, false },
                                   Case{ "a3_38 P2 control (A3-18 a): jsr @r0 ; mov r4,r0", 0x6043, false },
                                   Case{ "a3_38 control: jsr @r0 ; nop (r0 stays the callee's address)", 0x0009, true } })
            {
                const CallFixture f = { c.what, base, placed({ 0x4F22, 0xD003, 0x400B, c.slot, 0x4F26, 0x000B, 0x0009, 0x0009, 0x0600, 0x9040 }, 32, keeper), c.lent };
                const std::vector<DecompileResult> out = lendAll(f, { 0x06009040u });
                const DecompileResult& r = out.back();
                CHECK(out[0].prototype.known);
                if (c.lent) CHECK(r.prototype.known);
                else
                {
                    CHECK(!r.prototype.known && Has(r.prototypeWithheld, kAcross));
                    CHECK(!PseudoAgreesWithRun(r, CallSnapshot(f, ++serial), f, viaMemory));   // the text returns nothing; the code returns r0
                }
            }
            uint32_t ran = 0;
            const CallFixture p1 = { "a3_38 P1", base, placed({ 0x4F22, 0xD003, 0x400B, 0x6052, 0x4F26, 0x000B, 0x0009, 0x0009, 0x0600, 0x9040 }, 32, keeper) };
            CHECK(CallRunResult(CallSnapshot(p1, ++serial), p1, viaMemory[0], ran) && ran == 0x000B0009u);
        }

        // ---- A3-39: a hand-back through a store the callee does not make at an exact offset -----------------------------------------------
        // Through r15 plus an index (`mov r15,r1 ; add r6,r1 ; mov.l r5,@r1`, `mov.l r5,@(r0,r15)`), and by a lent writer handed the callee's own
        // r15-based address. The store of r5, which the signature holds, is no spill. The callee and its callers are withheld; a local array below
        // r15 indexed by a variable is not a hand-back.
        {
            const std::vector<uint16_t> pr14 = { 0x2FE6, 0x0E2A, 0x7FFC, 0xE005, 0x2F02, 0xB009, 0xE600, 0x60F2, 0x7F04, 0x4E2A, 0x000B, 0x6EF6 };
            struct Case
            {
                const char* what;
                std::vector<uint16_t> callee;
            };
            const std::vector<std::vector<uint32_t>> viaR5 = { { 0x11111111u, 0x22222222u, 0, 0x44444444u }, { 0, 1, 0, 0 } };
            for (const Case& c : { Case{ "a3_39 S2b: mov r15,r1 ; add r6,r1 ; mov.l r5,@r1", { 0x61F3, 0x316C, 0x000B, 0x2152 } },
                                   Case{ "a3_39 S2b2: mov.l r5,@(r0,r15)", { 0x6063, 0x000B, 0x0F56 } } })
            {
                const CallFixture f = { c.what, base, withCallee(pr14, c.callee) };
                const Lent lg = lend(f, true, false);
                CHECK(!lg.callee.prototype.known && Has(lg.callee.prototypeWithheld, kHandedBack));
                CHECK(!lg.caller.prototype.known && Has(lg.caller.prototypeWithheld, "not known"));
                // The withheld caller's text read the stale 5 until area 3 round 9 (A3-61: a store the callee makes through an index off its entry r15
                // may be any word of its caller's frame from the call's r15 up, so every local from there up is the alias's).
                CHECK(Has(lg.caller.Text(), "a local at or above the stack pointer is read after this call") && !Has(lg.caller.Text(), "return 5;"));
                CHECK(PseudoAgreesWithRun(lg.caller, CallSnapshot(f, ++serial), f, viaR5, true));
                CHECK(returns(f, viaR5[0], 0x22222222u));
            }
            // N1: the callee hands its caller's word to a lent writer through its own entry r15 + 4.
            const CallFixture n1 = { "a3_39 N1: g gives a lent writer the address of its caller's word", base,
                                     placed(placed({ 0x2FE6, 0x0E2A, 0x7FFC, 0xE005, 0x2F02, 0xB019, 0x0009, 0x60F2, 0x7F04, 0x4E2A, 0x000B, 0x6EF6 }, 32,
                                                   { 0x4F22, 0x64F3, 0x7404, 0xB00B, 0x0009, 0x4F26, 0x000B, 0x0009 }), 48, writer4) };
            const std::vector<DecompileResult> rn = lendAll(n1, { 0x06009060u, 0x06009040u });
            CHECK(rn[0].prototype.known && !rn[1].prototype.known && Has(rn[1].prototypeWithheld, kHandedBack));
            CHECK(!rn[2].prototype.known && Has(rn[2].prototypeWithheld, "not known"));
            CHECK(returns(n1, inputs[0], 7));
            // Controls. A local array below r15 indexed by a variable (compiled code) and a store through the frame pointer's own locals are lent.
            const CallFixture array = { "a3_39 control: a local array below the entry r15, variable index", base,
                                        withCallee({ 0x4F22, 0xB00D, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0x7FF8, 0x61F3, 0x316C, 0x2152, 0x000B, 0x7F08 }) };
            CHECK(lend(array, true, false).callee.prototype.known);
            // ...and the same store from the entry r15 itself, with no frame taken, is the hand-back.
            const CallFixture bare = { "a3_39: an indexed store off the entry r15", base, withCallee({ 0x4F22, 0xB00D, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0x61F3, 0x314C, 0x62F3, 0x2122, 0x000B, 0x0009 }) };
            CHECK(!lend(bare, true, false).callee.prototype.known);
            // The address of a stack parameter handed to a lent writer is the same (A3-23's callers, which were lent).
            const CallFixture param = { "a3_39: the address of a stack parameter handed to a lent writer", base,
                                        withCallee({ 0x082A, 0x64F3, 0xB00C, 0x0009, 0x60F2, 0x305C, 0x306C, 0x307C, 0x482A, 0x000B, 0x0009 }, writer4) };
            const Lent lp = lend(param, true, false);
            CHECK(lp.callee.prototype.known && !lp.caller.prototype.known && Has(lp.caller.prototypeWithheld, kHandedBack));
        }

        // ---- A3-40: only a plain spill of a held parameter is excepted --------------------------------------------------------------------
        {
            const std::vector<uint16_t> pr14 = { 0x2FE6, 0x0E2A, 0x7FFC, 0xE005, 0x2F02, 0xB009, 0x0009, 0x60F2, 0x7F04, 0x4E2A, 0x000B, 0x6EF6 };
            const CallFixture s2a2 = { "a3_40 S2a2: mov r4,r1 ; add r5,r1 ; mov.l r1,@r15 ; rts ; mov r1,r0", base, withCallee(pr14, { 0x6143, 0x315C, 0x2F12, 0x000B, 0x6013 }) };
            const Lent l2 = lend(s2a2, true, false);
            CHECK(!l2.callee.prototype.known && Has(l2.callee.prototypeWithheld, kHandedBack));
            CHECK(!l2.caller.prototype.known && Has(l2.caller.prototypeWithheld, "not known"));
            CHECK(Has(l2.caller.Text(), "a local at or above the stack pointer is read after this call"));   // the stale local, until area 3 round 9 (A3-61)
            CHECK(PseudoAgreesWithRun(l2.caller, CallSnapshot(s2a2, ++serial), s2a2, inputs, true));
            const CallFixture s2a = { "a3_40 S2a: mov r4,r0 ; add r5,r0 ; mov.l r0,@r15", base, withCallee(pr14, { 0x6043, 0x305C, 0x2F02, 0x000B, 0x0009 }) };
            CHECK(!lend(s2a, true, false).callee.prototype.known);
            // Controls. The plain spill of a held parameter stays lent, whole (A3-35, accepted) or as a halfword.
            const CallFixture s2a0 = { "a3_40 S2a0 control: mov r5,r0 ; mov.l r5,@r15 (A3-35)", base, withCallee(pr14, { 0x6053, 0x2F52, 0x000B, 0x0009 }) };
            CHECK(lend(s2a0, true, false).callee.prototype.known);
            const CallFixture half = { "a3_40 control: mov.w r5,@r15 of a held parameter", base, withCallee(pr14, { 0x6053, 0x2F51, 0x000B, 0x0009 }) };
            CHECK(lend(half, true, false).callee.prototype.known);
        }

        // ---- A3-41, accepted: a result some instruction has read is not carried ------------------------------------------------------------
        // `x = h(); <read x>; g(); return x;` with x in r0 across the keeper g is lent void whatever the read is: a compare, a byte read, a
        // store of the result, a push and a reload. Compiled code leaves a result it has used in r0 as scratch (31 and 46 lent signatures on the
        // images when only the copy counted), and the callers that read the value and mean it cannot be told from them. Pinned as limits; the
        // controls are right: the result pushed across the call and popped (R1d), and a read on one path only (R1f).
        {
            const std::vector<uint16_t> h = { 0x000B, 0xE007 };   // rts ; mov #7,r0
            auto two = [&](std::vector<uint16_t> caller) { return placed(placed(std::move(caller), 16, h), 24, keeper); };
            struct Case
            {
                const char* what;
                std::vector<uint16_t> caller;
            };
            for (const Case& c : { Case{ "a3_41 R1a: cmp/eq #7,r0 between the calls", { 0x4F22, 0xB00D, 0x0009, 0x8807, 0xB012, 0x0009, 0x4F26, 0x000B, 0x0009 } },
                                   Case{ "a3_41 R1b: extu.b r0,r1", { 0x4F22, 0xB00D, 0x0009, 0x610C, 0xB012, 0x0009, 0x4F26, 0x000B, 0x0009 } },
                                   Case{ "a3_41 R1c: mov.l r0,@-r15 ; mov.l @r15+,r0", { 0x4F22, 0xB00D, 0x0009, 0x2F06, 0x60F6, 0xB011, 0x0009, 0x4F26, 0x000B, 0x0009 } },
                                   Case{ "a3_41 R1e: mov.b r0,@r4", { 0x4F22, 0xB00D, 0x0009, 0x2400, 0xB012, 0x0009, 0x4F26, 0x000B, 0x0009 } } })
            {
                const CallFixture f = { c.what, base, two(c.caller) };
                const std::vector<DecompileResult> out = lendAll(f, { 0x06009020u, 0x06009030u });
                CHECK(out[2].prototype.known && out[2].prototype.ret.kind == TypeKind::Void);   // the accepted limit
                CHECK(returns(f, inputs[0], 7));
            }
            const CallFixture r1d = { "a3_41 R1d control: the result pushed across g and popped", base,
                                      two({ 0x4F22, 0xB00D, 0x0009, 0x2F06, 0xB012, 0x0009, 0x60F6, 0x4F26, 0x000B, 0x0009 }) };
            const std::vector<DecompileResult> rd = lendAll(r1d, { 0x06009020u, 0x06009030u });
            CHECK(rd[2].prototype.known && rd[2].prototype.ret.kind != TypeKind::Void && Has(rd[2].Text(), "return xVar1;") && agrees(rd[2], r1d, inputs));
            const CallFixture r1f = { "a3_41 R1f control: read on one path only", base,
                                      two({ 0x4F22, 0xB00D, 0x0009, 0x2448, 0x8900, 0x6103, 0xB010, 0x0009, 0x4F26, 0x000B, 0x0009 }) };
            const std::vector<DecompileResult> rf = lendAll(r1f, { 0x06009020u, 0x06009030u });
            CHECK(!rf[2].prototype.known && Has(rf[2].prototypeWithheld, kAcross));
        }

        // ---- A3-42: a saved register's word is spared only when the address handed over does not lie in it ----------------------------------
        {
            const std::vector<uint16_t> g = { 0xE107, 0x000B, 0x2411 };   // mov #7,r1 ; rts ; mov.w r1,@r4
            // S3a: &(saved r14) + 2 and a halfword store; S3b: + 3 and a byte store. r14 changes: it is not claimed kept.
            for (const bool byte : { false, true })
            {
                const CallFixture f = { byte ? "a3_42 S3b: byte store at the saved r14 word + 3" : "a3_42 S3a: halfword store at the saved r14 word + 2", base,
                                        withCallee({ 0x2FE6, 0x0E2A, 0x64F3, uint16_t(byte ? 0x7403 : 0x7402), 0xB00A, 0x0009, 0x4E2A, 0x000B, 0x6EF6 },
                                                   { 0xE107, 0x000B, uint16_t(byte ? 0x2410 : 0x2411) }) };
                const Lent l = lend(f, true, false);
                CHECK(l.callee.prototype.known && l.caller.prototype.known);
                CHECK(!((l.caller.prototype.effects.preserved >> 14) & 1u));
                uint32_t r14 = 0;
                CHECK(afterRun(f, "r14", inputs[0], r14) && r14 != 0x1EEEu);   // the run changes it
            }
            // S3g: PR pushed, &(PR's word) + 2: the run returns to a corrupted PR, so the caller is withheld.
            const CallFixture s3g = { "a3_42 S3g: the saved PR's word + 2, halfword store", base,
                                      withCallee({ 0x4F22, 0x64F3, 0x7402, 0xB00B, 0x0009, 0x4F26, 0x000B, 0x0009 }, { 0xE100, 0x000B, 0x2411 }) };
            const Lent l3 = lend(s3g, true, false);
            CHECK(l3.callee.prototype.known && !l3.caller.prototype.known && Has(l3.caller.prototypeWithheld, "not to its caller"));
            // Controls. The exact address with a longword store (S3a0) was never spared; r14 is not claimed.
            const CallFixture s3a0 = { "a3_42 S3a0 control: the exact address, mov.l", base,
                                       withCallee({ 0x2FE6, 0x0E2A, 0x64F3, 0xB00B, 0x0009, 0x4E2A, 0x000B, 0x6EF6 }, writer4) };
            const Lent l0 = lend(s3a0, true, false);
            CHECK(l0.caller.prototype.known && !((l0.caller.prototype.effects.preserved >> 14) & 1u));
            // A byte local in the word just below the saved r14 (compiled code: `char c; f(&c)`): the address is not inside the saved word, the
            // slot is spared and r14 stays claimed kept. Not over-withheld by the overlap test.
            const CallFixture below = { "a3_42 control: a byte local just below the saved r14 word", base,
                                        withCallee({ 0x2FE6, 0x0E2A, 0x7FFC, 0x64F3, 0x7403, 0xB009, 0x0009, 0x7F04, 0x4E2A, 0x000B, 0x6EF6 },
                                                   { 0xE107, 0x000B, 0x2410 }) };
            const Lent lb = lend(below, true, false);
            CHECK(lb.caller.prototype.known && ((lb.caller.prototype.effects.preserved >> 14) & 1u));
            uint32_t r14b = 0;
            CHECK(afterRun(below, "r14", inputs[0], r14b) && r14b == 0x1EEEu);
        }
    }
}

// Out of memory anywhere before the engine starts -- preparing the request, the name map --
// comes back as a failed result, never as an exception; the next request is unaffected.
// Area 2, round 3 (ledger A2-12 .. A2-15): the printed value of register-only code against what it does. The
// compiled-C differential in Sh2GeneratedTests.cpp (SaturnExplorerSh2PrintedCTests) runs the same fixtures
// against the emulator for 24 inputs each; these are their text and signature checks.
void TestArea2Round3()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 300;
    auto decompile = [&](const char* label, const std::vector<uint16_t>& words) {
        std::vector<uint8_t> hw;
        size_t off = 0x9000;
        for (uint16_t w : words)
        {
            dts::Put16(hw, off, w);
            off += 2;
        }
        const DecompileResult r = d.Decompile(MakeSnapshot(hw, ++serial), Request(0x06009000u));
        Print(label, r);
        CHECK(r.status == DecompileStatus::Ok);
        return r;
    };

    // A2-12: r0 = 0 with r1 holding a value derived from an input. The 8-byte return (r0:r1, high half in r0) was
    // narrowed to its low half, r1, and r1's value printed as the 4-byte return, which is r0. The join is
    // consumed whole now: the value returned is the whole pair, r0 = 0 in the high half.
    for (const std::vector<uint16_t>& words : { std::vector<uint16_t>{ 0x6153, 0xE000, 0x000B, 0x0009 },
                                                std::vector<uint16_t>{ 0x6153, 0x7101, 0xE000, 0x000B, 0x0009 },
                                                std::vector<uint16_t>{ 0x6153, 0x4501, 0x8D01, 0x4129, 0x324D, 0xE200, 0x000B, 0x6023 } })
    {
        const DecompileResult r = decompile("A2-12 r0 = 0, r1 dirty", words);
        CHECK(r.prototype.known);
        CHECK(r.prototype.ret.size == 8);
        CHECK(!Has(r.Text(), "return param_2;"));
        CHECK(!Has(r.Text(), "return param_2 + 1;"));
        CHECK(!Has(r.Text(), "return param_2 >> 0x10;"));
    }

    // A2-14: addv's overflow test with one addend known. `src == 2` on a flag printed as `== true`, so the
    // test read as a tautology; the constant is printed as it is.
    for (const std::vector<uint16_t>& words : { std::vector<uint16_t>{ 0xE501, 0x345F, 0x000B, 0x0029 },
                                                std::vector<uint16_t>{ 0x4501, 0x345F, 0x000B, 0x0029 },
                                                std::vector<uint16_t>{ 0x655D, 0x345F, 0x000B, 0x0029 },
                                                std::vector<uint16_t>{ 0x4501, 0x354F, 0x000B, 0x0029 } })
    {
        const DecompileResult r = decompile("A2-14 addv", words);
        CHECK(!Has(r.Text(), "== true"));
        CHECK(Has(r.Text(), "== 2"));
    }

    // A2-15: shlr ; rotcl with one byte of the result used. The T bit put back by rotcl is the argument's bit 0; the
    // text read an input at a unique-space address ("unaff_10000008") no register holds.
    {
        const DecompileResult r = decompile("A2-15 shlr, rotcl, extu.b", { 0x4401, 0x4424, 0x000B, 0x604C });
        CHECK(!Has(r.Text(), "unaff_1"));
        CHECK(Has(r.Text(), "param_1 & 1"));
    }
    {
        const DecompileResult r = decompile("A2-15 control: 32-bit", { 0x4401, 0x4424, 0x000B, 0x6043 });
        CHECK(!Has(r.Text(), "unaff_1"));
    }

    // A2-13: a write to SR, GBR or VBR is a side effect the caller sees. It stays in the code, in order, with the
    // value written, and the register it is written from stays a parameter of the lent signature.
    {
        const DecompileResult r = decompile("A2-13 ldc r4,vbr", { 0x442E, 0x000B, 0x0009 });
        CHECK(Has(r.Text(), "Write_VBR(param_1);"));
        CHECK(r.prototype.known && r.prototype.params.size() == 1);
    }
    {
        const DecompileResult r = decompile("A2-13 ldc r4,gbr", { 0x441E, 0x000B, 0x0009 });
        CHECK(Has(r.Text(), "Write_GBR(param_1);"));
        CHECK(r.prototype.known && r.prototype.params.size() == 1);
    }
    {
        const DecompileResult r = decompile("A2-13 ldc r4,sr", { 0x440E, 0x000B, 0x0009 });
        CHECK(Has(r.Text(), "Write_SR(param_1 & 0x3f3);"));   // the mask the CPU applies (patch 0019)
        CHECK(r.prototype.known && r.prototype.params.size() == 1);
    }
    {
        // di: stc sr,r0 ; or #0xF0,r0 ; ldc r0,sr
        const DecompileResult r = decompile("A2-13 di", { 0x0002, 0xCBF0, 0x400E, 0x000B, 0x0009 });
        CHECK(Has(r.Text(), "Write_SR("));
        CHECK(Has(r.Text(), "0xf0"));
    }
    {
        // An interrupt-masked store: both writes are in the text, in order, round the store.
        const DecompileResult r = decompile("A2-13 critical section", { 0x0102, 0x6013, 0xCBF0, 0x400E, 0x2452, 0x410E, 0x000B, 0x0009 });
        const std::string text = r.Text();
        const size_t first = text.find("Write_SR(");
        const size_t store = text.find("*param_1 = param_2;");
        const size_t second = first == std::string::npos ? first : text.find("Write_SR(", first + 1);
        CHECK(first != std::string::npos && store != std::string::npos && second != std::string::npos);
        CHECK(first < store && store < second);
        // The compiler's interrupt-mask idiom: the SR it reads is the SR it writes back, so no caller passes it (A3-5 does not
        // withhold on it), and the signature is lent with its two parameters.
        CHECK(r.prototype.known && r.prototype.params.size() == 2);
    }
    {
        // ...but an SR read that does not go back into SR is the caller's flags, and withholds (A3-5): the mask raised and
        // returned with no write; the T flag selected after the restore and returned.
        const DecompileResult mask = decompile("A2-13 SR raised and returned", { 0x0002, 0xCBF0, 0x000B, 0x0009 });   // stc sr,r0 ; or #0xF0,r0 ; rts
        CHECK(!mask.prototype.known && Has(mask.prototypeWithheld, "sr"));
        const DecompileResult flag = decompile("A2-13 SR restored, T returned", { 0x0102, 0x410E, 0x0029, 0x000B, 0x0009 });   // stc sr,r1 ; ldc r1,sr ; movt r0 ; rts
        CHECK(!flag.prototype.known && Has(flag.prototypeWithheld, "sr"));
        // a field of it (the S and T bits), 4 bytes wide, returned after the write back
        const DecompileResult field = decompile("A2-13 SR restored, field returned", { 0x0102, 0x410E, 0x6013, 0xC903, 0x000B, 0x0009 });   // stc sr,r1 ; ldc r1,sr ; mov r1,r0 ; and #3,r0 ; rts
        CHECK(!field.prototype.known && Has(field.prototypeWithheld, "sr"));
        // The same for GBR and VBR: saved and put back, or changed and kept, is the machine's state (lent); returned, or used as
        // an address, is an input (withheld). Before patch 0034 the engine dropped the writes and with them the reads.
        const DecompileResult gbrSave = decompile("A2-13 GBR set and restored", { 0x0112, 0x441E, 0x411E, 0x000B, 0x0009 });   // stc gbr,r1 ; ldc r4,gbr ; ldc r1,gbr ; rts
        CHECK(gbrSave.prototype.known && gbrSave.prototype.params.size() == 1 && Has(gbrSave.Text(), "Write_GBR(unaff_gbr);"));
        CHECK(((gbrSave.prototype.effects.preserved >> 17) & 1u) != 0);   // GBR ends as it began: a caller keeps it
        const DecompileResult vbrSave = decompile("A2-13 VBR set and restored", { 0x0122, 0x442E, 0x412E, 0x000B, 0x0009 });   // stc vbr,r1 ; ldc r4,vbr ; ldc r1,vbr ; rts
        CHECK(vbrSave.prototype.known && vbrSave.prototype.params.size() == 1 && Has(vbrSave.Text(), "Write_VBR(unaff_vbr);"));
        const DecompileResult gbrAdd = decompile("A2-13 GBR advanced", { 0x0112, 0x7104, 0x411E, 0x000B, 0x0009 });   // stc gbr,r1 ; add #4,r1 ; ldc r1,gbr ; rts
        CHECK(gbrAdd.prototype.known && ((gbrAdd.prototype.effects.preserved >> 17) & 1u) == 0);   // changed: a caller reads the new one
        const DecompileResult gbrRet = decompile("A2-13 GBR returned", { 0x0012, 0x441E, 0x000B, 0x0009 });   // stc gbr,r0 ; ldc r4,gbr ; rts
        CHECK(!gbrRet.prototype.known && Has(gbrRet.prototypeWithheld, "gbr"));
        const DecompileResult gbrAddr = decompile("A2-13 GBR saved, then an address", { 0x0112, 0x7104, 0xC600, 0x411E, 0x000B, 0x0009 });   // stc gbr,r1 ; add #4,r1 ; mov.l @(0,gbr),r0 ; ldc r1,gbr
        CHECK(!gbrAddr.prototype.known && Has(gbrAddr.prototypeWithheld, "gbr"));
    }
}

// Area 2, round 4 (ledger A2-21 .. A2-24): a division scaled by its last shift, a byte put into a stacked longword, SR
// stored through a pointer and loaded back, and what an rte restores. The compiled-C differential
// (SaturnExplorerSh2PrintedCTests) runs the A2-21 and A2-22 fixtures against the emulator; these are their text and signature checks.
void TestArea2Round4()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 400;
    auto decompile = [&](const char* label, const std::vector<uint16_t>& words) {
        std::vector<uint8_t> hw;
        size_t off = 0x9000;
        for (uint16_t w : words)
        {
            dts::Put16(hw, off, w);
            off += 2;
        }
        const DecompileResult r = d.Decompile(MakeSnapshot(hw, ++serial), Request(0x06009000u));
        Print(label, r);
        CHECK(r.status == DecompileStatus::Ok);
        return r;
    };

    // A2-21: x / 7 * 4 is `t = mulhu(x, c) ; (((x - t) >> 1) + t) >> 2 << 2`. The shift pair merges into a mask, RuleDivOpt
    // no longer sees a division, and RuleDivTermAdd2's 65-bit product, in an 8-byte multiply, was printed.
    for (const std::vector<uint16_t>& words : { std::vector<uint16_t>{ 0xD104, 0x3415, 0x000A, 0x3408, 0x4401, 0x340C, 0x4409, 0x4408, 0x000B, 0x6043, 0x2492, 0x4925 },
                                                std::vector<uint16_t>{ 0xD104, 0x3415, 0x000A, 0x3408, 0x4401, 0x340C, 0x4401, 0x000B, 0x6043, 0x0009, 0x2492, 0x4925 },
                                                std::vector<uint16_t>{ 0xD102, 0x3415, 0x000A, 0x4001, 0x000B, 0x304C, 0x1234, 0x5678 },
                                                std::vector<uint16_t>{ 0xD102, 0x314D, 0x000A, 0x4021, 0x000B, 0x304C, 0x1234, 0x5678 } })
    {
        const DecompileResult r = decompile("A2-21 overflowing product", words);
        CHECK(!Has(r.Text(), "0x124924925") && !Has(r.Text(), "0x212345678"));
    }
    {
        // ...and the quotient alone is still recovered as a division (the product is only an intermediate there)
        const DecompileResult r = decompile("A2-21 control: x / 7", { 0xD104, 0x3415, 0x000A, 0x3408, 0x4401, 0x340C, 0x4409, 0x000B, 0x6043, 0x0009, 0x2492, 0x4925 });
        CHECK(Has(r.Text(), "param_1 / 7"));
    }

    {
        // A product that does fit stays folded: mulhs(x, c) + x is x * (c + 2^32) >> 32, a 63-bit product of a 31-bit value and a 32-bit
        // constant in a signed 8-byte multiply (the guard once refused every such rewrite and the divisions by 60 and 7 of the game image lost
        // their recovered division), and the signed division by 7 is still a division
        const DecompileResult r = decompile("A2-21 control: the signed product that fits", { 0xD102, 0x314D, 0x000A, 0x304C, 0x000B, 0x0009, 0xB60B, 0x60B7 });
        CHECK(Has(r.Text(), "0xb60b60b7"));
        const DecompileResult q = decompile("A2-21 control: signed x / 7", { 0xD106, 0x341D, 0x000A, 0x304C, 0x4021, 0x4021, 0x6203, 0x4200, 0xE200, 0x302E,
                                                                              0x6003, 0x000B, 0x0009, 0x0009, 0x9249, 0x2493 });
        CHECK(Has(q.Text(), "param_1 / 7"));
    }

    // A2-22: a longword stored to the stack, a byte or word of it overwritten, the longword loaded back. The part of r0 that
    // is still the stored register was dropped (RulePiecePathology took the concatenation for a partial write to a register
    // the function never wrote): the return is the whole longword.
    for (const std::vector<uint16_t>& words : { std::vector<uint16_t>{ 0x7FFC, 0x2F42, 0x6053, 0x80F3, 0x60F2, 0x000B, 0x7F04 },
                                                std::vector<uint16_t>{ 0x6053, 0x2F46, 0x80F2, 0x60F6, 0x000B, 0x0009 },
                                                std::vector<uint16_t>{ 0x6053, 0x2F46, 0x80F1, 0x60F6, 0x000B, 0x0009 },
                                                std::vector<uint16_t>{ 0x6053, 0x2F46, 0x81F1, 0x60F6, 0x000B, 0x0009 } })
    {
        const DecompileResult r = decompile("A2-22 stack insert", words);
        CHECK(r.prototype.known && r.prototype.ret.size == 4 && r.prototype.params.size() == 2);
        CHECK(!Has(r.Text(), "return param_2;"));
    }
    {
        const DecompileResult r = decompile("A2-22 byte at 3", { 0x7FFC, 0x2F42, 0x6053, 0x80F3, 0x60F2, 0x000B, 0x7F04 });
        CHECK(Has(r.Text(), "CONCAT31") && Has(r.Text(), "param_2"));
    }

    // A2-23: SR stored through a pointer and loaded back. The engine does not forward a store through a pointer to the load, so the
    // reload is no descendant of the entry SR: the function is withheld, not lent without the T it reads.
    for (const std::vector<uint16_t>& words : { std::vector<uint16_t>{ 0x0002, 0x2402, 0x6042, 0x000B, 0xC901 },   // stc sr,r0 ; mov.l r0,@r4 ; mov.l @r4,r0 ; rts ; and #1,r0
                                                std::vector<uint16_t>{ 0x4403, 0x6042, 0x000B, 0xC901 } })        // stc.l sr,@-r4 ; mov.l @r4,r0 ; rts ; and #1,r0
    {
        const DecompileResult r = decompile("A2-23 SR through a pointer", words);
        CHECK(!r.prototype.known && Has(r.prototypeWithheld, "sr"));
    }
    {
        // SR only stored is withheld too since round 5 (A2-35: the memory outlives the function and a caller reads it back; TestArea2Round5)
        const DecompileResult r = decompile("A2-23 SR stored only", { 0x0102, 0x2412, 0x000B, 0x0009 });   // stc sr,r1 ; mov.l r1,@r4 ; rts
        CHECK(!r.prototype.known && Has(r.prototypeWithheld, "sr"));
    }

    // A2-24: rte restores SR and PC from the stack; a handler that changes the stacked words changes where and how the
    // interrupted code resumes. The restores are user ops, so the stores that feed them stay.
    {
        const DecompileResult r = decompile("A2-24 stacked SR", { 0x51F1, 0xE201, 0x212B, 0x1F11, 0x002B, 0x0009 });
        CHECK(Has(r.Text(), "Write_SR(") && Has(r.Text(), "| 1") && Has(r.Text(), "Rte_PC("));
    }
    {
        const DecompileResult r = decompile("A2-24 stacked PC", { 0x61F2, 0x7102, 0x2F12, 0x002B, 0x0009 });
        CHECK(Has(r.Text(), "Rte_PC(") && Has(r.Text(), "+ 2"));
    }

    // A2-25: cmp/str on a register that is not a parameter compares its four bytes; each input is named with the byte it is
    // (counted from the least significant), not by a counter that tells nothing.
    {
        const DecompileResult r = decompile("A2-25 cmp/str r6,r3", { 0x236C, 0x000B, 0x0029 });
        CHECK(Has(r.Text(), "in_r3;") && Has(r.Text(), "in_r3_b1") && Has(r.Text(), "in_r3_b2") && Has(r.Text(), "in_r3_b3"));
        CHECK(!Has(r.Text(), "in_r3_00") && !Has(r.Text(), "in_r3_01"));
    }
}

// Area 2, round 5 (ledger A2-31 .. A2-37): what decides that r0 is returned, a division by a divisor at or above 2^31, what rte leaves in SR,
// SR stored through a pointer, and the names of the bytes of r8-r15. The printed-C differential (SaturnExplorerSh2GeneratedTests) runs the
// A2-32 and A2-37 fixtures against the emulator (rte is not run there, A2-20); these are the text and signature checks.
void TestArea2Round5()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 500;
    auto decompile = [&](const char* label, const std::vector<uint16_t>& words) {
        std::vector<uint8_t> hw;
        size_t off = 0x9000;
        for (uint16_t w : words)
        {
            dts::Put16(hw, off, w);
            off += 2;
        }
        const DecompileResult r = d.Decompile(MakeSnapshot(hw, ++serial), Request(0x06009000u));
        Print(label, r);
        CHECK(r.status == DecompileStatus::Ok);
        return r;
    };
    // The caller at 0x06009000 and the callee at 'calleeAt' (the words are one image), the callee decompiled first and its signature
    // lent to the caller, as the panel does.
    struct Pair
    {
        DecompileResult callee, caller;
    };
    auto pair = [&](const char* label, const std::vector<uint16_t>& words, uint32_t calleeAt) {
        std::vector<uint8_t> hw;
        size_t off = 0x9000;
        for (uint16_t w : words)
        {
            dts::Put16(hw, off, w);
            off += 2;
        }
        auto snap = MakeSnapshot(hw, ++serial);
        Pair p;
        p.callee = d.Decompile(snap, Request(calleeAt));
        DecompileRequest q = Request(0x06009000u);
        q.prototypes.emplace_back(calleeAt, p.callee.prototype);
        p.caller = d.Decompile(snap, q);
        Print(label, p.caller);
        CHECK(p.callee.status == DecompileStatus::Ok && p.caller.status == DecompileStatus::Ok);
        return p;
    };

    // A2-31: whether r0 was returned depended on the instruction in rts's delay slot. `nop` was `r0 = r0`, a COPY that was the last write of
    // r0 and its only use, so a function ending `rts ; nop` returned the value and the same function with any other instruction in the slot
    // (the epilogue's last pop, `add #n,r15`) did not. A nop writes nothing now, and a value that is the output of a call to a callee
    // whose signature is lent as returning is returned whatever else it is used for.
    {
        const std::vector<uint16_t> callee = { 0x6042, 0x000B, 0x0009 };   // mov.l @r4,r0 ; rts ; nop
        // return g(p) with the usual epilogue: sts.l pr ... lds.l @r15+,pr ; rts ; mov.l @r15+,r14 (the pop in the slot)
        std::vector<uint16_t> wrapper = { 0x2FE6, 0x4F22, 0xB003, 0x0009, 0x4F26, 0x000B, 0x6EF6 };
        wrapper.insert(wrapper.end(), callee.begin(), callee.end());
        const Pair filled = pair("A2-31 return g(p), the pop in the slot", wrapper, 0x0600900Eu);
        CHECK(filled.callee.prototype.known && filled.callee.prototype.ret.kind != TypeKind::Void);
        CHECK(filled.caller.prototype.known && filled.caller.prototype.ret.kind != TypeKind::Void);
        CHECK(Has(filled.caller.Text(), "return xVar1;"));
        // ...the pop before rts and a nop in the slot: the same text, the same signature
        std::vector<uint16_t> nopped = { 0x2FE6, 0x4F22, 0xB004, 0x0009, 0x4F26, 0x6EF6, 0x000B, 0x0009 };
        nopped.insert(nopped.end(), callee.begin(), callee.end());
        const Pair withNop = pair("A2-31 return g(p), nop in the slot", nopped, 0x06009010u);
        CHECK(withNop.caller.prototype.known && withNop.caller.prototype.ret.kind != TypeKind::Void);
        CHECK(Has(withNop.caller.Text(), "return xVar1;"));
        // p = g(x); if (p) *p = 0; return p;  (the value is also compared and used as an address)
        const Pair checked = pair("A2-31 p = g(x); if (p) *p = 0; return p;",
                                  { 0x2FE6, 0x4F22, 0xB008, 0x0009, 0x2008, 0x8901, 0xE100, 0x2012, 0x4F26, 0x000B, 0x6EF6, 0x0009, 0x6042, 0x000B, 0x0009 },
                                  0x06009018u);
        CHECK(checked.caller.prototype.known && checked.caller.prototype.ret.kind != TypeKind::Void);
        CHECK(Has(checked.caller.Text(), "return pxVar1;"));
        // ...and a 64-bit result (r0 and r1, a piece of the call's output in each) whose high half is also tested (generator seed 5234)
        const Pair wide = pair("A2-31 a 64-bit result tested and returned",
                               { 0x4F22, 0xB006, 0x0009, 0x2008, 0x8900, 0xE303, 0x4F26, 0x000B, 0x7F00, 0xE001, 0x000B, 0xE102 }, 0x06009012u);
        CHECK(wide.callee.prototype.known && wide.callee.prototype.ret.size == 8);
        CHECK(wide.caller.prototype.known && wide.caller.prototype.ret.size == 8 && Has(wide.caller.Text(), "return "));
        // A callee lent as returning nothing is not a value to return: the wrapper stays void.
        const Pair none = pair("A2-31 control: the callee returns nothing",
                               { 0x2FE6, 0x4F22, 0xB003, 0x0009, 0x4F26, 0x000B, 0x6EF6, 0x000B, 0x0009 }, 0x0600900Eu);
        CHECK(none.callee.prototype.known && none.callee.prototype.ret.kind == TypeKind::Void);
        CHECK(none.caller.prototype.ret.kind == TypeKind::Void);
    }
    {
        // The slot does not decide for a value that is not a call's either: `return ++*p` (the store in the slot) is the same function with a
        // nop and with any other instruction there. r0 is used for the store as well as the return, which upstream's heuristic takes for
        // scratch; what it takes for scratch it takes for scratch in every slot.
        const DecompileResult filled = decompile("A2-31 ++*p, add #0,r15 in the slot", { 0x6042, 0x7001, 0x2402, 0x000B, 0x7F00 });
        const DecompileResult nopped = decompile("A2-31 ++*p, nop in the slot", { 0x6042, 0x7001, 0x2402, 0x000B, 0x0009 });
        CHECK(filled.prototype.known && nopped.prototype.known);
        CHECK(filled.prototype.ret.kind == nopped.prototype.ret.kind);
        CHECK(filled.prototype.params.size() == nopped.prototype.params.size());
        // if (x == 5) *p = v; return x;
        const DecompileResult cmpFilled = decompile("A2-31 compare, add #0,r15 in the slot", { 0x6043, 0x8805, 0x8B00, 0x2652, 0x000B, 0x7F00 });
        const DecompileResult cmpNopped = decompile("A2-31 compare, nop in the slot", { 0x6043, 0x8805, 0x8B00, 0x2652, 0x000B, 0x0009 });
        CHECK(cmpFilled.prototype.ret.kind == cmpNopped.prototype.ret.kind);
        CHECK(cmpFilled.prototype.params.size() == cmpNopped.prototype.params.size());
        // A2-43: a write of r0 with the value it holds (`mov r0,r0`, `and r0,r0`, `or r0,r0`, `add #0,r0`, `xor #0,r0`, `or #0,r0`) is no new
        // value: in the slot and before the rts it is a nop, and the compare's r0 is the scratch of A2-42 whichever of them ends the function.
        // The pair `mov.l @r4,r0 ; cmp/eq #5,r0 ; bf +1 ; mov.l r0,@r5` is the reviewer's.
        const DecompileResult idNop = decompile("A2-43 reference: nop in the slot", { 0x6042, 0x8805, 0x8B00, 0x2502, 0x000B, 0x0009 });
        CHECK(idNop.prototype.known && idNop.prototype.ret.kind == TypeKind::Void);
        for (uint16_t identity : { uint16_t(0x6003), uint16_t(0x2009), uint16_t(0x200B), uint16_t(0x7000), uint16_t(0xCA00), uint16_t(0xCB00) })
        {
            const DecompileResult inSlot = decompile("A2-43 an identity write of r0 in the slot", { 0x6042, 0x8805, 0x8B00, 0x2502, 0x000B, identity });
            const DecompileResult before = decompile("A2-43 an identity write of r0 before the rts", { 0x6042, 0x8805, 0x8B00, 0x2502, identity, 0x000B, 0x0009 });
            for (const DecompileResult* r : { &inSlot, &before })
            {
                CHECK(r->prototype.known && r->prototype.ret.kind == idNop.prototype.ret.kind);
                CHECK(r->prototype.params.size() == idNop.prototype.params.size());
                CHECK(Has(r->Text(), "return;"));
            }
        }
        // ...and the identity ops that are not identities are values: `and #0`, `add #1`, `xor #1` of r0 are written, and returned.
        for (uint16_t value : { uint16_t(0xC900), uint16_t(0x7001), uint16_t(0xCA01) })
        {
            const DecompileResult r = decompile("A2-43 control: r0 changed by the last instruction", { 0x6042, 0x2502, 0x000B, value });
            CHECK(r.prototype.known && r.prototype.ret.kind != TypeKind::Void);
        }
        // r0 computed from its own entry value and then compared: the engine drops the return (another use), and with it the text's read of
        // r0, so the lent `void` hid a dependence on a register no signature passes. A nop in the slot hid it by keeping the return; the
        // host (ReturnCheck, InputsInReturn) withholds it in every slot now.
        for (uint16_t slot : { uint16_t(0x0009), uint16_t(0x7F00) })
        {
            const DecompileResult shll = decompile("A2-31 shll r0 ; cmp/eq #1,r0 ; rts", { 0x4000, 0x8801, 0x8BFF, 0x000B, slot });
            CHECK(!shll.prototype.known && Has(shll.prototypeWithheld, "computed from r0 on entry"));
            // ...with a parameter in it as well (generator seed 12623: the nightly's second grammar)
            const DecompileResult sum = decompile("A2-31 add r4,r0 ; xor r0,r1 ; rts", { 0x304C, 0x210A, 0x000B, slot });
            CHECK(!sum.prototype.known && Has(sum.prototypeWithheld, "computed from r0 on entry"));
        }
        // ...and r0 left alone on the path that does not write it is not a read (A3-14's scratch r0): lent
        const DecompileResult alone = decompile("A2-31 control: cmp/eq #1,r0 ; rts", { 0x8801, 0x8BFF, 0x000B, 0x0009 });
        CHECK(alone.prototype.known);
        // ...and `xor r0,r0`, the compiler's zero, reads no input: lent (image 008's 06073C7C was withheld on it by the rule above until the
        // interpretation took x ^ x and x - x for 0)
        const DecompileResult zeroed = decompile("A2-31 control: xor r0,r0 ; mov.l r0,@r4 ; rts", { 0x200A, 0x2402, 0x000B, 0x0009 });
        CHECK(zeroed.prototype.known);
        // A function that sets r0 and does nothing else with it still returns it, in any slot
        const DecompileResult one = decompile("A2-31 control: mov #1,r0 ; rts ; nop", { 0xE001, 0x000B, 0x0009 });
        CHECK(one.prototype.known && one.prototype.ret.kind != TypeKind::Void && Has(one.Text(), "return 1;"));
        const DecompileResult oneB = decompile("A2-31 control: mov #1,r0 ; rts ; add #0,r15", { 0xE001, 0x000B, 0x7F00 });
        CHECK(oneB.prototype.known && oneB.prototype.ret.kind != TypeKind::Void && Has(oneB.Text(), "return 1;"));
    }

    // A2-32: x / 0xFFFFFFFE (multiplier 3, the add form, a total shift of 64) printed `return 0;` and dropped the parameter: the shift of 64 in
    // an 8-byte multiply is 0, so the product that needs 65 bits was left in the data-flow where nothing divided it. 0xFFFFFFFC and
    // 0xFFFFFFF0 the same; the divisors with a total shift of 63 and the others were always right.
    for (uint16_t multiplier : { uint16_t(3), uint16_t(5), uint16_t(0x11) })
    {
        const DecompileResult r = decompile("A2-32 x / 0xFFFFFFFx",
                                            { 0xD106, 0x3415, 0x000A, 0x3408, 0x4401, 0x340C, 0x4429, 0x4419, 0x4409, 0x4409, 0x4409, 0x4401, 0x000B, 0x6043, 0x0000, multiplier });
        CHECK(!Has(r.Text(), "return 0;"));
        CHECK(r.prototype.known && r.prototype.params.size() == 1 && r.prototype.ret.kind != TypeKind::Void);
        CHECK(Has(r.Text(), "param_1"));
    }
    {
        const DecompileResult r = decompile("A2-32 x / 0xFFFFFFFE * 4", { 0xD307, 0x3535, 0x000A, 0x3508, 0x4501, 0x350C, 0x4529, 0x4519, 0x4509, 0x4509, 0x4509, 0x4501, 0x4508, 0x6053, 0x000B, 0x0009, 0x0000, 0x0003 });
        CHECK(!Has(r.Text(), "return 0;"));
        CHECK(r.prototype.known && Has(r.Text(), "param_2"));
    }

    // A2-36: rte restores SR and then runs its slot, so a slot that changes T changes the SR the interrupted code resumes with.
    {
        const DecompileResult sett = decompile("A2-36 rte ; sett", { 0x002B, 0x0018 });
        CHECK(Has(sett.Text(), "Write_SR(in_stack_00000004 & 0x3f2 | 1);"));
        const DecompileResult clrt = decompile("A2-36 rte ; clrt", { 0x002B, 0x0008 });
        CHECK(Has(clrt.Text(), "Write_SR(in_stack_00000004 & 0x3f2);"));
        const DecompileResult cmp = decompile("A2-36 rte ; cmp/eq r4,r5", { 0x002B, 0x3540 });
        CHECK(Has(cmp.Text(), "& 0x3f2 |") && Has(cmp.Text(), "param_2 == param_1"));
        // controls: a slot that leaves T alone, and one that stores to the stacked SR (the restore has read it already)
        const DecompileResult plain = decompile("A2-36 control: rte ; nop", { 0x002B, 0x0009 });
        CHECK(Has(plain.Text(), "Write_SR(in_stack_00000004 & 0x3f3);"));
        const DecompileResult stored = decompile("A2-36 control: a store to the stacked SR in the slot", { 0x51F1, 0xE201, 0x212B, 0x002B, 0x1F11 });
        CHECK(Has(stored.Text(), "Write_SR(in_stack_00000004 & 0x3f3);"));
        const DecompileResult vbr = decompile("A2-36 control: rte ; ldc r4,vbr", { 0x002B, 0x442E });
        CHECK(Has(vbr.Text(), "Write_VBR(param_1)"));
    }

    // A2-35: SR stored through a pointer is an input: the caller reads the word back after the call, so its own T at the call is the callee's.
    {
        const Pair p = pair("A2-35 SR stored, the word read back by the caller",
                            { 0x4F22, 0x3650, 0xB004, 0x0009, 0x4F26, 0x6042, 0x000B, 0xC901, 0x0102, 0x2412, 0x000B, 0x0009 }, 0x06009010u);
        CHECK(!p.callee.prototype.known && Has(p.callee.prototypeWithheld, "sr"));
        CHECK(!p.caller.prototype.known);   // nothing is lent to the caller on a callee that reads T
        const DecompileResult stcl = decompile("A2-35 stc.l sr,@-r4", { 0x4403, 0x000B, 0x0009 });
        CHECK(!stcl.prototype.known && Has(stcl.prototypeWithheld, "sr"));
        // a control that stays lent: the interrupt mask raised around a store and put back (SR is only written back)
        const DecompileResult ret = decompile("A2-35 control: SR saved and put back", { 0x0102, 0x6013, 0xCBF0, 0x400E, 0x2452, 0x410E, 0x000B, 0x0009 });
        CHECK(ret.prototype.known && ret.prototype.params.size() == 2);
    }

    // A2-37: the bytes of a register no signature passes are named with the byte they are, as r0-r7's are (A2-25), not by a counter.
    for (const std::vector<uint16_t>& words : { std::vector<uint16_t>{ 0x286C, 0x000B, 0x0029 },     // cmp/str r6,r8 ; rts ; movt r0
                                                std::vector<uint16_t>{ 0x2E6C, 0x000B, 0x0029 },     // r14
                                                std::vector<uint16_t>{ 0x2F6C, 0x000B, 0x0029 } })   // r15
    {
        const DecompileResult r = decompile("A2-37 cmp/str on r8-r15", words);
        CHECK(Has(r.Text(), "_b1") && Has(r.Text(), "_b2") && Has(r.Text(), "_b3"));
        CHECK(!Has(r.Text(), "_00") && !Has(r.Text(), "_01") && !Has(r.Text(), "_02"));
        CHECK(Has(r.Text(), "unaff_r"));
    }
    {
        const DecompileResult r = decompile("A2-37 control: cmp/str on r1", { 0x221C, 0x000B, 0x0029 });
        CHECK(Has(r.Text(), "in_r1_b1") && Has(r.Text(), "in_r1_b3"));
    }
}

void TestAllocationFailureIsReported()
{
    if (!dts::kInjectionUsable)
    {
        std::printf("  SKIPPED in an MSVC Debug build: its STL allocates inside noexcept moves, so an\n"
                    "  injected failure there terminates by design (the Windows Release run covers this)\n");
        return;
    }
    Sh2Decompiler d(gConfig);
    auto snap = Fixture1Snapshot();
    DecompileRequest q = Request(dts::kFixture1Caller);
    q.names.emplace_back(dts::kFixture1Callee, "ReadCounter");
    q.names.emplace_back(dts::kFixture1Caller, "Tick");
    // How many allocations happen before the engine's first phase: the window swept.
    long before = 0;
    d.SetPhaseObserver([&](DecompilePhase p) {
        if (p == DecompilePhase::Init && before == 0) before = dts::AllocCount().load();
    });
    dts::AllocCount() = 0;
    CHECK(d.Decompile(snap, q).status == DecompileStatus::Ok);
    d.SetPhaseObserver(nullptr);
    std::printf("  %ld allocations before the engine starts; failing each in turn\n", before);
    CHECK(before > 0);
    int failed = 0;
    for (long k = 0; k < before; ++k)
    {
        DecompileResult r;
        bool escaped = false;
        dts::FailAfter() = k;
        try
        {
            r = d.Decompile(snap, q);
        }
        catch (...)
        {
            escaped = true;
        }
        dts::FailAfter() = -1;
        CHECK(!escaped);
        if (r.status == DecompileStatus::Failed) ++failed;
        else CHECK(r.status == DecompileStatus::Ok);   // an allocation the result did not need
    }
    std::printf("  %d of %ld injected failures reported as Failed, none escaped\n", failed, before);
    CHECK(failed > 0);

    const DecompileResult after = d.Decompile(snap, q);
    CHECK(after.status == DecompileStatus::Ok && Has(after.Text(), "ReadCounter(param_1)"));
}


// The two refusals, by who asks rather than by how much: the Architecture's own loader (values:
// literals, jump tables, strings) refuses with DataUnavailError whatever the size -- 16 bytes
// included -- and never zero-fills; the translator's (instruction fetches) refuses with
// BadDataError, so flow halts there instead of aborting.
// ===== Areas 2, 3 and 4: the open rows and the seeds beyond the nightly (ledger A3-29 .. A3-31, A3-45 .. A3-48, A4-10, A4-11) =====
// The fixtures that need the evaluator and the call oracle; the ones of A3-29 and A3-31 are with the rows they belong to in TestCallsAndTraps
// and TestTailJumpToUnreachedCode, the delay slots (A2-4) in TestDelaySlotPcRelative and SaturnExplorerSh2DecodeTests.
void TestAreas234Leftovers()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 800;
    const uint32_t base = 0x06009000u;
    auto withCallee = [&](std::vector<uint16_t> caller, std::vector<uint16_t> callee) {
        caller.resize(16, 0x0009);
        caller.insert(caller.end(), callee.begin(), callee.end());
        return caller;
    };
    auto decompile = [&](const char* label, const std::vector<uint16_t>& words) {
        std::vector<uint8_t> hw;
        size_t off = 0x9000;
        for (uint16_t w : words)
        {
            dts::Put16(hw, off, w);
            off += 2;
        }
        const DecompileResult r = d.Decompile(MakeSnapshot(hw, ++serial), Request(base));
        Print(label, r);
        CHECK(r.status == DecompileStatus::Ok);
        return r;
    };

    // ---- A3-45: r0 rotated through the carry in a loop is an input ------------------------------------------------------------------
    // `mov #1,r3 ; L: rotcr r0 ; tas.b @r5 ; dt r3 ; bf L ; rts ; nop`: the rotate is dead code in the engine's text, the function returns
    // nothing, and r0 on entry decides what it leaves in r0. Not lent (a caller's setup of r0 is dead in its text). The same loop entered only
    // if r4 is not zero leaves r0 alone on the other path, and rotated on the path that runs the loop: a read of r0 all the same, and withheld
    // since area 3 round 9 (A3-59, A3-62: it was lent as A3-14's scratch r0, which is a path that leaves r0 alone and no other that reads it).
    {
        const DecompileResult r = decompile("A3-45 rotcr r0 in a do-while loop", { 0xE301, 0x4025, 0x451B, 0x4310, 0x8BFB, 0x000B, 0x0009 });
        CHECK(!r.prototype.known && Has(r.prototypeWithheld, "computed from r0 on entry"));
        const DecompileResult c = decompile("A3-45 control: the loop may be skipped", { 0x2448, 0x8902, 0x4025, 0x4410, 0x8BFC, 0x000B, 0x0009 });
        CHECK(!c.prototype.known && Has(c.prototypeWithheld, "computed from r0 on entry"));
        // A3-46's function: r0 left alone on the path that skips both writes, its `and r0,r0` no write (A2-43) -- and r0's entry value the test
        // that chooses between r0 and 0 (`tst #0x80,r0`): withheld since round 9 (A3-56; it was lent), "chosen by r0".
        const DecompileResult n = decompile("A3-46 r0 written by xor on one path, and r0,r0 on another", { 0x5264, 0xC880, 0x8900, 0x200A, 0x2028, 0x8B00, 0x2009, 0x000B, 0x0009 });
        CHECK(!n.prototype.known && Has(n.prototypeWithheld, "its return is chosen by r0 on entry, which no signature passes"));
    }

    // ---- A4-10, A4-11: a switch the engine could not normalize prints on the loaded address ----------------------------------------
    // The bound check is `sr = (sr & ~1) | (5 < r6)` and that SR word is returned too (the case `stc sr,r2 ; mov r2,r0`), so the compare cannot
    // be read from the branch: the finished function has no guard for the model to find and falls back to one case per destination, labelled with
    // the address the table holds. The text is right: each destination's code is under its label, the bound check is an `if` before the lookup.
    {
        const std::vector<uint16_t> module = { 0x2F86, 0x2F96, 0x2FA6, 0x4F22, 0x7FF8, 0x6843, 0x6953, 0x6A63, 0xD203, 0x7F08, 0x4F26, 0x6AF6, 0x69F6, 0x68F6, 0x422B, 0x0009,
                                               0x0600, 0x9024, 0xE105, 0x3616, 0x891B, 0x4608, 0xC701, 0x016E, 0x412B, 0x0009, 0x0600, 0x904C, 0x0600, 0x9050, 0x0600, 0x9054,
                                               0x0600, 0x9054, 0x0600, 0x9058, 0x0600, 0x905C, 0xA009, 0x0009, 0xA007, 0x0009, 0xA005, 0x0009, 0xA003, 0x0009, 0x517F, 0xA000,
                                               0x0009, 0x0202, 0x6023, 0x000B, 0x0009 };
        std::vector<uint8_t> hw;
        size_t off = 0x9000;
        for (uint16_t w : module)
        {
            dts::Put16(hw, off, w);
            off += 2;
        }
        const DecompileResult r = d.Decompile(MakeSnapshot(hw, ++serial), Request(0x06009024u));
        Print("A4-11 switch on the loaded address", r);
        CHECK(r.status == DecompileStatus::Ok);
        const std::string t = r.Text();
        CHECK(Has(t, "switch(") && Has(t, "case 0x600905c:"));
        // the code of table index 5 (0600905C: mov.l @(0x3c,r7),r1) is under the label of its address, and the lookup is made before the switch
        const size_t sw = t.find("switch(");
        const size_t label = t.find("case 0x600905c:");
        const size_t read = t.find("param_4 + 0x3c");
        CHECK(sw != std::string::npos && label != std::string::npos && read != std::string::npos && sw < label && label < read);
        CHECK(Has(t, "param_3 * 4 + 0x6009034"));
        CHECK(!r.prototype.known);   // the specials alphabet's function is withheld on SR, as before
    }

    // ---- A2-20: bsrf, which the generator draws now ---------------------------------------------------------------------------------
    // `mov.l @(pool),r1 ; bsrf r1 ; mov #5,r4` calls the function at the distance the pool holds from the bsrf + 4; the literal folds, the callee is
    // found, and its lent signature gives the call its argument. The run and the text agree.
    {
        const CallFixture f = { "a2_20: bsrf through a distance in the pool", base,
                                withCallee({ 0x4F22, 0xD103, 0x0103, 0xE405, 0x4F26, 0x000B, 0x0009, 0x0009, 0x0000, 0x0018 }, { 0x6043, 0x000B, 0x7001 }) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult callee = d.Decompile(snap, Request(0x06009020u));
        DecompileRequest q = Request(base);
        q.prototypes.emplace_back(0x06009020u, callee.prototype);
        const DecompileResult r = d.Decompile(snap, q);
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok && callee.prototype.known && r.prototype.known);
        CHECK(Has(r.Text(), "func_0x06009020(5)"));
        CHECK(PseudoAgreesWithRun(r, snap, f, { { 1 } }));
        uint32_t ran = 0;
        CHECK(CallRunResult(snap, f, { 1 }, ran) && ran == 6u);
    }

    // ---- A3-47: a caller that publishes a frame address through a pointer loaded from memory ----------------------------------------
    // `mov r15,r1 ; mov.l @(pool),r6 ; mov.l r1,@r6` stores the address of a local in a global, and a lent callee that stores through the global writes
    // the local. The store that published the address was taken for one through it (the leak came before the question), over the saved PR: the caller
    // was withheld "a return not to its caller".
    {
        const std::vector<uint16_t> caller = { 0x4F22, 0x7FFC, 0xE05A, 0x2F02, 0x61F3, 0xD604, 0x2612, 0xB007, 0x0009, 0x60F2, 0x7F04, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9080 };
        // mov.l @(3,pc),r2 ; mov.l @r2,r2 ; mov #0x37,r1 ; rts ; mov.l r1,@r2 ; the cell's address
        const std::vector<uint16_t> writer = { 0xD203, 0x6222, 0xE137, 0x000B, 0x2212, 0x0009, 0x0009, 0x0009, 0x0600, 0x9080 };
        const CallFixture f = { "a3_47: the frame address published in a global, a lent callee writes through it", base, withCallee(caller, writer) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult w = d.Decompile(snap, Request(0x06009020u));
        CHECK(w.prototype.known && w.prototype.params.empty());
        DecompileRequest q = Request(base);
        q.prototypes.emplace_back(0x06009020u, w.prototype);
        const DecompileResult r = d.Decompile(snap, q);
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(r.prototype.known && r.prototypeWithheld.empty());
        CHECK(!Has(r.Text(), "return 0x5a;"));
        uint32_t ran = 0;
        CHECK(CallRunResult(snap, f, {}, ran) && ran == 0x37u);
        // The control: a store through the loaded pointer of a value that is no stack address does not matter, and the callee that stores
        // through it without the address ever published is the same lent shape as before.
        std::vector<uint16_t> noPublish = caller;
        noPublish[4] = 0xE100;   // mov #0,r1 in the place of mov r15,r1
        const CallFixture g = { "a3_47 control: a constant published", base, withCallee(noPublish, writer) };
        auto snapG = CallSnapshot(g, ++serial);
        DecompileRequest qg = Request(base);
        qg.prototypes.emplace_back(0x06009020u, d.Decompile(snapG, Request(0x06009020u)).prototype);
        CHECK(d.Decompile(snapG, qg).prototype.known);
    }

    // ---- A3-48: a frame address in r7 alone, handed to a callee that is not lent --------------------------------------------------------
    // The engine reads a call's arguments in order r4..r7 and fills in at most two unset registers before one that is set, so with r4-r6
    // untouched the address in r7 is no argument (`func(); return 0x5a;`, the run returns what the callee wrote). Said like the address in r2.
    {
        const std::vector<uint16_t> unlent = { 0x2712, 0x000B, 0x0009 };   // mov.l r1,@r7 ; rts ; nop (r1 is the caller's)
        const CallFixture f = { "a3_48: mov r15,r7 ; bsr (r4-r6 untouched)", base,
                                withCallee({ 0x4F22, 0x7FFC, 0xE05A, 0x2F02, 0x67F3, 0xB009, 0x0009, 0x60F2, 0x7F04, 0x4F26, 0x000B, 0x0009 }, unlent) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok && !r.prototype.known);
        CHECK(Has(r.Text(), "a frame address is in r7 here") && !Has(r.Text(), "return 0x5a;"));
        CHECK(PseudoAgreesWithRun(r, snap, f, { { 1 }, { 2 } }));
        uint32_t ran = 0;
        CHECK(CallRunResult(snap, f, { 1 }, ran) && ran == 0x1111u);   // r1 on entry, which the callee stored
        // Controls. r6 set before the call (the engine passes r4-r7, the address is an argument): no note. And r5, r6 as the register: the
        // fill-in rule covers them.
        const CallFixture c6 = { "a3_48 control: r6 set, so r7 is an argument", base,
                                 withCallee({ 0x4F22, 0x7FFC, 0xE05A, 0x2F02, 0x67F3, 0xE601, 0xB008, 0x0009, 0x60F2, 0x7F04, 0x4F26, 0x000B, 0x0009 }, unlent) };
        auto snapC = CallSnapshot(c6, ++serial);
        const DecompileResult rc = d.Decompile(snapC, Request(base));
        Print(c6.name.c_str(), rc);
        CHECK(!Has(rc.Text(), "a frame address is in r7 here") && !Has(rc.Text(), "return 0x5a;"));
        for (int reg = 5; reg <= 6; ++reg)
        {
            const CallFixture cr = { "a3_48 control: the address in r" + std::to_string(reg), base,
                                     withCallee({ 0x4F22, 0x7FFC, 0xE05A, 0x2F02, uint16_t(0x6003 | reg << 8 | 15 << 4), 0xB009, 0x0009, 0x60F2, 0x7F04, 0x4F26, 0x000B, 0x0009 },
                                                { uint16_t(0x2012 | reg << 8), 0x000B, 0x0009 }) };
            auto snapR = CallSnapshot(cr, ++serial);
            const DecompileResult rr = d.Decompile(snapR, Request(base));
            Print(cr.name.c_str(), rr);
            CHECK(!Has(rr.Text(), "return 0x5a;") && !Has(rr.Text(), "a frame address is in r7 here"));
        }
    }
}

// ===== Area 3, round 8: a join with a value from memory, and the stack argument of a call (ledger A3-49, A3-50, A3-51) =====
// A3-49 and A3-50: A3-20's rule (a return that joins r0's entry value with a parameter the signature does not hold) and A3-14's (r0 computed from its
// entry value on a path, left alone on another) took a join out of the rule when another alternative came from memory: a load, or a literal-pool word,
// which the check reads as a load. The premise was that the engine keeps the return of a value from memory; it drops `phi(in_r0, *p)` and
// `phi(in_r0, literal)` as it drops `phi(in_r0, in_r6)`. Each fixture is withheld, and its text is wrong (the run returns the value it drops).
// A3-51: A3-48's channel through the stack. The engine drops an argument that follows three unset registers, the stack word with it, so a frame
// address stored in the call's first stack slot is no argument and the local it points at was folded across the call.
void TestArea3Round8()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 1000;
    const uint32_t base = 0x06009020u;
    const std::vector<std::vector<uint32_t>> inputs = { { 0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u },
                                                        { 0x00000000u, 0x00000001u, 0x7FFFFFFFu, 0x80000000u },
                                                        { 0x11111111u, 0u, 0x33333333u, 0x44444444u } };
    auto decompile = [&](const CallFixture& f, uint32_t entry) {
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(entry));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        return r;
    };
    // The function is withheld for the reason, and the text it would have been lent with is wrong: the oracle rejects it.
    auto withheld = [&](const char* name, std::vector<uint16_t> words, const char* reason) {
        const CallFixture f = { name, base, std::move(words) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!r.prototype.known && Has(r.prototypeWithheld, reason));
        CHECK(!PseudoAgreesWithRun(r, snap, f, inputs));
    };
    auto lent = [&](const char* name, std::vector<uint16_t> words, bool returnsValue) {
        const CallFixture f = { name, base, std::move(words) };
        const DecompileResult r = decompile(f, f.base);
        CHECK(r.prototype.known && (r.prototype.ret.kind != TypeKind::Void) == returnsValue);
        return r;
    };

    // ---- A3-49 ---------------------------------------------------------------------------------------------------------------------------
    // (V8) tst r4,r4 ; bt L ; mov r6,r0 ; tst r5,r5 ; bt L ; mov.l @r5,r0 ; L: rts ; nop -- r6 is the value on one path, a word from memory on another
    withheld("a3_49 a: r0 joins r6 and a loaded word", { 0x2448, 0x8903, 0x6063, 0x2558, 0x8900, 0x6052, 0x000B, 0x0009 }, "joins r0's entry value with a parameter");
    // (V10) the last alternative a literal-pool word
    withheld("a3_49 b: r0 joins r6 and a literal", { 0x2448, 0x8903, 0x6063, 0x2558, 0x8900, 0xD002, 0x000B, 0x0009, 0x0009, 0x0009, 0x0000, 0x1038 },
             "joins r0's entry value with a parameter");
    // seed 112124, function 1 alone: the literal in r6, `mov r6,r0` on one path
    withheld("a3_49 c: seed 112124, function 1", { 0x7FFC, 0x2228, 0x8B00, 0xD603, 0x2558, 0x8900, 0x6063, 0x7F04, 0x000B, 0x0009, 0x2600, 0x1038 },
             "joins r0's entry value with a parameter");
    // Controls. The register-only join (V9, A3-20) stays withheld; a parameter the signature holds (r6 is stored through r4 here) is the scratch r0 of
    // compiled code and stays lent void, with a memory alternative as without; r0 loaded on every path has no join with its entry value.
    withheld("a3_49 control: r0 joins r6 only", { 0x2448, 0x8900, 0x6063, 0x000B, 0x0009 }, "joins r0's entry value with a parameter");
    {
        const DecompileResult r = lent("a3_49 control: r6 held by the signature", { 0x2462, 0x2448, 0x8903, 0x6063, 0x2558, 0x8900, 0x6052, 0x000B, 0x0009 }, false);
        CHECK(r.prototype.params.size() >= 3);
        const DecompileResult every = lent("a3_49 control: r0 loaded on both paths", { 0x2448, 0x8902, 0x6052, 0x000B, 0x0009, 0x6062, 0x000B, 0x0009 }, true);
        (void)every;
    }

    // After a frame address has escaped (`mov r15,r1 ; mov.l r1,@r4`) every load carries a stack origin too (Loaded): the same join is withheld with it.
    withheld("a3_49 d: V8 after a frame address escaped", { 0x61F3, 0x2412, 0x2448, 0x8903, 0x6063, 0x2558, 0x8900, 0x6052, 0x000B, 0x0009 },
             "joins r0's entry value with a parameter");

    // ---- A3-50 ---------------------------------------------------------------------------------------------------------------------------
    // (W1) tst r4,r4 ; bt L ; mov.l @r5,r1 ; add r1,r0 ; L: rts -- r0 + a word from memory on one path, r0's entry value on the other
    withheld("a3_50 a: r0 + a loaded word on one path", { 0x2448, 0x8901, 0x6152, 0x301C, 0x000B, 0x0009 }, "computed from r0 on entry");
    withheld("a3_50 a': r0 ^ a loaded word on one path", { 0x2448, 0x8901, 0x6152, 0x201A, 0x000B, 0x0009 }, "computed from r0 on entry");
    // (W3) the operand a literal
    withheld("a3_50 b: r0 + a literal on one path", { 0x2448, 0x8901, 0xD102, 0x301C, 0x000B, 0x0009, 0x0009, 0x0009, 0x0000, 0x1038 }, "computed from r0 on entry");
    // (W4) SR on one path, a literal on the other
    withheld("a3_50 c: sr or a literal", { 0x0002, 0x2448, 0x8900, 0xD002, 0x000B, 0x0009, 0x0009, 0x0009, 0x0000, 0x1038 }, "computed from sr on entry");
    withheld("a3_50 d: W1 after a frame address escaped", { 0x61F3, 0x2412, 0x2448, 0x8901, 0x6152, 0x301C, 0x000B, 0x0009 }, "computed from r0 on entry");
    // Controls (A3-14): the register operands were withheld before.
    withheld("a3_50 control: add #1,r0 on one path", { 0x2448, 0x8900, 0x7001, 0x000B, 0x0009 }, "computed from r0 on entry");
    withheld("a3_50 control: add r4,r0 on one path", { 0x2448, 0x8900, 0x304C, 0x000B, 0x0009 }, "joins r0's entry value with a parameter");

    // ---- A3-51 ---------------------------------------------------------------------------------------------------------------------------
    // sts.l pr,@-r15 ; add #-8,r15 ; mov #0x5A,r0 ; mov.l r0,@(4,r15) ; mov r15,r1 ; add #4,r1 ; [mov #1,r4 ;] mov.l r1,@r15 ; bsr g ; nop ; mov.l @(4,r15),r0 ;
    // add #8,r15 ; lds.l @r15+,pr ; rts ; nop. g (at 0x06009040) reads the word at its r15, the address of the caller's local, and stores 0x37 through it.
    // r5, r6 and r7 are unset (r4 too, or set to 1): the engine drops the stack word from the call, and the local was folded: `return 0x5a;`.
    const uint32_t gAt = 0x06009040u;
    auto atG = [&](std::vector<uint16_t> caller, std::vector<uint16_t> g) {
        caller.resize(32, 0x0009);
        caller.insert(caller.end(), g.begin(), g.end());
        return caller;
    };
    const std::vector<uint16_t> writer = { 0x62F2, 0xE337, 0x000B, 0x2232 };   // mov.l @r15,r2 ; mov #0x37,r3 ; rts ; mov.l r3,@r2
    const char* const kStackNote = "a frame address is in a stack argument slot here";
    {
        const CallFixture f = { "a3_51 ST1: the address in the first stack slot, r4-r7 unset", 0x06009000u,
                                atG({ 0x4F22, 0x7FF8, 0xE05A, 0x1F01, 0x61F3, 0x7104, 0x2F12, 0xB017, 0x0009, 0x50F1, 0x7F08, 0x4F26, 0x000B, 0x0009 }, writer) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult callee = d.Decompile(snap, Request(gAt));
        CHECK(callee.status == DecompileStatus::Ok && !callee.prototype.known && Has(callee.prototypeWithheld, "outside its signature"));
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok && !r.prototype.known);
        CHECK(Has(r.Text(), kStackNote) && !Has(r.Text(), "return 0x5a;"));
        CHECK(PseudoAgreesWithRun(r, snap, f, { { 1 }, { 2 } }, true));
        uint32_t ran = 0;
        CHECK(CallRunResult(snap, f, { 1 }, ran) && ran == 0x37u);
        // ST4: r4 set to 1 -- r5, r6 and r7 are still three unset registers before the stack word
        const CallFixture f4 = { "a3_51 ST4: r4 set, r5-r7 unset", 0x06009000u,
                                 atG({ 0x4F22, 0x7FF8, 0xE05A, 0x1F01, 0x61F3, 0x7104, 0x2F12, 0xE401, 0xB016, 0x0009, 0x50F1, 0x7F08, 0x4F26, 0x000B, 0x0009 }, writer) };
        auto snap4 = CallSnapshot(f4, ++serial);
        const DecompileResult r4 = d.Decompile(snap4, Request(f4.base));
        Print(f4.name.c_str(), r4);
        CHECK(Has(r4.Text(), kStackNote) && !Has(r4.Text(), "return 0x5a;"));
        CHECK(PseudoAgreesWithRun(r4, snap4, f4, { { 1 }, { 2 } }, true));
        // ST3 (control): r5 set to 1 -- the engine shows the stack word as the fifth argument; no note, the local is not folded
        const CallFixture f3 = { "a3_51 ST3 control: r5 set, the stack word is an argument", 0x06009000u,
                                 atG({ 0x4F22, 0x7FF8, 0xE05A, 0x1F01, 0x61F3, 0x7104, 0x2F12, 0xE501, 0xB016, 0x0009, 0x50F1, 0x7F08, 0x4F26, 0x000B, 0x0009 }, writer) };
        auto snap3 = CallSnapshot(f3, ++serial);
        const DecompileResult r3 = d.Decompile(snap3, Request(f3.base));
        Print(f3.name.c_str(), r3);
        CHECK(!Has(r3.Text(), kStackNote) && !Has(r3.Text(), "return 0x5a;"));
        CHECK(PseudoAgreesWithRun(r3, snap3, f3, { { 1 }, { 2 } }, true));
        // ST5 (control): the same address also in r4 (a pointer to a local kept in a local, passed too): the engine shows it, no note
        const CallFixture f5 = { "a3_51 ST5 control: the address in the stack slot and in r4", 0x06009000u,
                                 atG({ 0x4F22, 0x7FF8, 0xE05A, 0x1F01, 0x61F3, 0x7104, 0x2F12, 0x6413, 0xB016, 0x0009, 0x50F1, 0x7F08, 0x4F26, 0x000B, 0x0009 }, writer) };
        auto snap5 = CallSnapshot(f5, ++serial);
        const DecompileResult r5 = d.Decompile(snap5, Request(f5.base));
        Print(f5.name.c_str(), r5);
        CHECK(Has(r5.Text(), "func_0x06009040(&xStack_8)") && !Has(r5.Text(), kStackNote) && !Has(r5.Text(), "return 0x5a;"));
        // ST6: the address in the second stack word (a callee that reads its sixth argument), the first word unwritten: the same drop
        const std::vector<uint16_t> writer2 = { 0x52F1, 0xE137, 0x000B, 0x2212 };   // mov.l @(4,r15),r2 ; mov #K,r1 ; rts ; mov.l r1,@r2
        const CallFixture f6 = { "a3_51 ST6: the address in the second stack word", 0x06009000u,
                                 atG({ 0x4F22, 0x7FF4, 0xE05A, 0x1F02, 0x61F3, 0x7108, 0x1F11, 0xB017, 0x0009, 0x50F2, 0x7F0C, 0x4F26, 0x000B, 0x0009 }, writer2) };
        auto snap6 = CallSnapshot(f6, ++serial);
        const DecompileResult r6 = d.Decompile(snap6, Request(f6.base));
        Print(f6.name.c_str(), r6);
        CHECK(Has(r6.Text(), kStackNote) && !Has(r6.Text(), "return 0x5a;"));
        CHECK(PseudoAgreesWithRun(r6, snap6, f6, { { 1 }, { 2 } }, true));
        uint32_t ran6 = 0;
        CHECK(CallRunResult(snap6, f6, { 1 }, ran6) && ran6 == 0x37u);
        // ST2 (control): the address in r4, the engine's own argument
        const CallFixture f2 = { "a3_51 ST2 control: the address in r4", 0x06009000u,
                                 atG({ 0x4F22, 0x7FF8, 0xE05A, 0x1F01, 0x64F3, 0x7404, 0xB018, 0x0009, 0x50F1, 0x7F08, 0x4F26, 0x000B, 0x0009 }, { 0xE337, 0x000B, 0x2432 }) };
        auto snap2 = CallSnapshot(f2, ++serial);
        const DecompileResult r2 = d.Decompile(snap2, Request(f2.base));
        Print(f2.name.c_str(), r2);
        CHECK(!Has(r2.Text(), kStackNote) && !Has(r2.Text(), "return 0x5a;"));
    }
    {
        // Every subset of r4-r7 set before the call (`mov #1,rN`), the address in the first stack slot: the engine drops the stack word from the call when
        // r4-r6 or r5-r7 are all unset (the rule of the probe) and shows it as the fifth argument otherwise; in both the local is not folded (no
        // `return 0x5a;`), by the note in the first case and by the argument in the second, and the text agrees with the run.
        for (int subset = 0; subset < 16; ++subset)
        {
            std::vector<uint16_t> w = { 0x4F22, 0x7FF8, 0xE05A, 0x1F01, 0x61F3, 0x7104, 0x2F12 };
            int k = 0;
            for (int reg = 4; reg < 8; ++reg)
                if (subset >> (reg - 4) & 1)
                {
                    w.push_back(static_cast<uint16_t>(0xE001 | reg << 8));
                    ++k;
                }
            for (uint16_t t : { static_cast<uint16_t>(0xB000 | (0x17 - k)), uint16_t(0x0009), uint16_t(0x50F1), uint16_t(0x7F08), uint16_t(0x4F26), uint16_t(0x000B), uint16_t(0x0009) }) w.push_back(t);
            const bool r4 = subset & 1, r5 = subset >> 1 & 1, r6 = subset >> 2 & 1, r7 = subset >> 3 & 1;
            const bool dropped = (!r4 && !r5 && !r6) || (!r5 && !r6 && !r7);
            const std::string name = "a3_51 subset " + std::to_string(subset) + " of r4-r7 set";
            const CallFixture f = { name, 0x06009000u, atG(w, writer) };
            auto snap = CallSnapshot(f, ++serial);
            const DecompileResult r = d.Decompile(snap, Request(f.base));
            Print(f.name.c_str(), r);
            CHECK(r.status == DecompileStatus::Ok);
            CHECK(!Has(r.Text(), "return 0x5a;"));
            CHECK(Has(r.Text(), kStackNote) == dropped);
            CHECK(PseudoAgreesWithRun(r, snap, f, { { 1 }, { 2 } }, true));
        }
    }
    {
        // SA1: a plain value in the stack word, the fifth argument. The engine drops it from the call (`func_0x06009040(3);` where the code passes 7
        // as well), and the callee, which reads a stack word its registers do not lead up to, is never lent, so the caller is withheld and the call
        // is the unlent one. No local is folded and no frame address is lost: the accepted limit of an unlent call's text (A3-5: the arguments it shows
        // are the ones the engine sees); pinned so a change to it is seen.
        const CallFixture f = { "a3_51 SA1: a plain value in the first stack slot", 0x06009000u,
                                atG({ 0x4F22, 0x7FFC, 0xE107, 0x2F12, 0xB01A, 0xE403, 0x7F04, 0x4F26, 0x000B, 0x0009 }, { 0x60F2, 0x000B, 0x304C }) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult callee = d.Decompile(snap, Request(gAt));
        CHECK(!callee.prototype.known && Has(callee.prototypeWithheld, "outside its signature"));
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok && !r.prototype.known && Has(r.Text(), "func_0x06009040(3)") && !Has(r.Text(), kStackNote));
        uint32_t ran = 0;
        CHECK(CallRunResult(snap, f, { 1 }, ran) && ran == 10u);
    }
}

// ===== Area 2, round 7 review: the value the slot's PC-relative instruction makes, as the text computes it, against the run (A2-47) =====
// TestDelaySlotPcRelative holds the text of these shapes to a substring ("unaff_retaddr + 4"); the decode oracle holds the p-code to the
// hardware. Neither holds what the engine prints to the run: the printed expression is evaluated here and compared with the code run on the
// emulator, for bt/s and bf/s taken and not (the slot's PC is the branch target + 2 or its own address + 4, chosen by T) with the target at an
// even and an odd displacement (the two candidate PCs differ by 2 mod 4 for the odd one, which is where the alignment of mova and mov.l
// matters), and for rts (PR + 2).
//
// A2-47 (patch 0046): `bt/s` + mova / mov.l with an odd displacement printed the address without its `& ~3`: `(uint4)(0 < param_1) * 6 + 0x6009018`
// where the code makes 0x600901C. The sum's non-zero mask (`Varnode::nzm`) was stale when RuleAndMask read it: RuleCollectTerms rewrote
// `T * 0x600900E + ((1 - T) * 0x6009008)` into `T * 6 + 0x6009008` by changing the inputs of an existing INT_ADD, whose output kept the mask of
// the value it used to be (`(1 - T) * 0x6009008`: 0xfffffffc, the low two bits clear), and the AND with 0xfffffffc was "redundant".
// `Funcdata::opSetInput` now forgets the mask of the output it feeds. Not particular to the slot: the same arithmetic written as code (control
// 2 below) printed the same wrong text. Each case is held to the run, and the odd displacements to the mask in the text.
void TestArea2Round7SlotValues()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 900;
    const uint32_t base = 0x06009000u;
    // 16 bytes of code, then distinct words 0x4000 + (address - base) / 2 up to 0x06009060, which the slots' loads read.
    auto padded = [&](std::vector<uint16_t> code) {
        if (code.size() < 8) code.resize(8, 0x0009);
        for (uint32_t a = static_cast<uint32_t>(code.size()) * 2; a < 0x60; a += 2) code.push_back(static_cast<uint16_t>(0x4000 + a / 2));
        return code;
    };
    struct Slot { const char* name; uint16_t word; };
    // Not taken: the slot at 0x9004 reads PC 0x9008 (mov.w d=6: 0x9008 + 12 = 0x9014; mov.l, mova d=4: 0x9008 + 16 = 0x9018). Taken, target 0x900A
    // (sdisp 2): PC 0x900C (mov.w 0x9018; mov.l, mova 0x900C + 16 = 0x901C); target 0x900C (sdisp 3): PC 0x900E (mov.w 0x901A; mov.l, mova
    // 0x900C + 16 = 0x901C).
    const Slot slots[] = { { "mova @(4,pc),r0", 0xC704 }, { "mov.w @(6,pc),r0", 0x9006 }, { "mov.l @(4,pc),r0", 0xD004 } };
    const std::vector<std::vector<uint32_t>> inputs = { { 0 }, { 1 }, { 0xFFFFFFFFu }, { 5 }, { 0x80000000u } };
    // The branch at the two alignments of the code (the function's first word a nop or not): the mask is the first thing to go wrong at one
    // alignment for bt/s and at the other for bf/s, so each odd displacement is held at both.
    for (int shift = 0; shift < 2; ++shift)
        for (const Slot& s : slots)
            for (int sense = 0; sense < 2; ++sense)
                for (int sdisp = 2; sdisp <= 3; ++sdisp)
                {
                    // [nop ;] cmp/pl r4 ; bt/s L (bf/s L) ; <slot> ; rts ; nop ; ... ; L: rts ; nop
                    std::vector<uint16_t> head;
                    if (shift) head.push_back(0x0009);
                    for (uint16_t w : { uint16_t(0x4415), uint16_t((sense ? 0x8F00 : 0x8D00) | sdisp), s.word, uint16_t(0x000B), uint16_t(0x0009) }) head.push_back(w);
                    while (head.size() < size_t(3 + sdisp + shift)) head.push_back(0x0009);
                    head.push_back(0x000B);
                    head.push_back(0x0009);
                    const std::string name = std::string(sense ? "bf/s + " : "bt/s + ") + s.name + (sdisp == 2 ? ", even displacement" : ", odd displacement") +
                                             (shift ? ", branch at 0x06009004" : ", branch at 0x06009002");
                    const CallFixture f = { name, base, padded(head) };
                    auto snap = CallSnapshot(f, ++serial);
                    const DecompileResult r = d.Decompile(snap, Request(base));
                    Print(name.c_str(), r);
                    CHECK(r.status == DecompileStatus::Ok && !r.unsupported);
                    CHECK(PseudoAgreesWithRun(r, snap, f, inputs));
                    // A2-47: the odd displacement with an aligned load. The evaluator does not know the value read by mov.l (a typed-unknown
                    // load), so there the text is held to the mask: the load must be made at the aligned address, `... & 0xfffffffc`.
                    if (s.word != 0x9006 && sdisp == 3) CHECK(Has(r.Text(), "0xfffffffc"));
                }
    // rts + mova @(1,pc),r0 returns (PR + 2 & ~3) + 4.
    {
        const CallFixture f = { "rts + mova", base, padded({ 0x000B, 0xC701 }) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok && !r.unsupported);
        uint32_t ran = 0;
        CHECK(CallRunResult(snap, f, { 0 }, ran) && ran == ((kRunSentinel + 2) & ~3u) + 4);
        CHECK(PseudoAgreesWithRun(r, snap, f, { { 0 }, { 7 } }));
    }
    // Control 1: (6 * T + 0x06009008) & -4 written as code: the mask stays.
    {
        const CallFixture f = { "control: (6 * T + 0x06009008) & -4 as code", base,
                                { 0x4415, 0x0129, 0xE306, 0x231E, 0x021A, 0xD503, 0x325C, 0xE6FC, 0x2269, 0x6023, 0x000B, 0x0009, 0x0600, 0x9008 } };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(PseudoAgreesWithRun(r, snap, f, inputs));
    }
    // Control 2 (A2-47, the same arithmetic as the slot's, written as code): r6 = T * 0x0600900E + (1 - T) * 0x06009008 ; r6 &= -4 ; return r6.
    // Printed `(uint4)(0 < param_1) * 6 + 0x6009008` and returned 0x0600900E for T = 1 where the code returns 0x0600900C.
    {
        const CallFixture f = { "A2-47: (T * A + (1 - T) * B) & -4 as code", base,
                                { 0x4415, 0x0129, 0xD206, 0xD307, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0xE7FC, 0x2679, 0x6063, 0x000B, 0x0009,
                                  0x0600, 0x900E, 0x0600, 0x9008 } };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(PseudoAgreesWithRun(r, snap, f, inputs));
        CHECK(Has(r.Text(), "0xfffffffc"));
    }
}

// ===== Area 2, round 8 review: fixtures held to the run, with the ways of agreeing told apart =====
// R8Check decompiles one fixture and compares the text with the code run on the emulator for each input. The verdicts: `agrees` (the text states the
// run's value), `agrees-global` (it names a global of the fixture's code area, `xRam06009034`, whose word or halfword is the run's value: the
// evaluator does not know a global), `unknown` (it states a value the evaluator cannot know: nothing was compared), `void-ok` (it states none and
// the fixture allows that), `differs`, `cannot-evaluate` (the evaluator could not read the text), `no-run` (the code did not return on the
// emulator), `not-ok` (the request failed or the function is marked unsupported). PseudoAgreesWithRun counts the third and fourth as agreeing,
// which is how a value check of a text that states none of its own can pass without checking anything; the fixtures below are held to `agrees`
// or `agrees-global`.
// Flags (a string): `n` r0 is no result, `v` a text that returns nothing is not a difference, `c` the evaluator reads the code area (a `*(T *)addr`
// and a global named by its address are the snapshot's word).
struct R8Outcome
{
    std::string verdict, detail;
};

// The printer writes the saturation of mac.l and mac.w as `if (A && (v = K, B)) { v = X; }`: an assignment and a comma inside a condition, which the
// evaluator has no rule for. It means `if (A) { v = K; if (B) { v = X; } }` (the assignment happens when A holds, B decides the body); the text is
// rewritten so before it is evaluated, and one the rewrite does not fit is left alone (and is then `cannot-evaluate`, not an agreement).
std::string R8FlattenCommaCondition(std::string text)
{
    static const std::regex pat("if \\(([\\s\\S]*?) &&\\s*\\((\\w+) = ([^,()]+), ([\\s\\S]*?)\\)\\) \\{");
    for (int guard = 0; guard < 4; ++guard)
    {
        std::smatch m;
        if (!std::regex_search(text, m, pat)) break;
        // the block this if opens ends at its matching close brace; one more brace closes the new inner if
        const size_t open = static_cast<size_t>(m.position(0) + m.length(0));
        int depth = 1;
        size_t close = open;
        for (; close < text.size() && depth > 0; ++close)
        {
            if (text[close] == '{') ++depth;
            else if (text[close] == '}') --depth;
        }
        if (depth != 0) break;
        const std::string repl = "if (" + m.str(1) + ") {\n " + m.str(2) + " = " + m.str(3) + ";\n if (" + m.str(4) + ") {";
        text = text.substr(0, static_cast<size_t>(m.position(0))) + repl + text.substr(open, close - open) + "}" + text.substr(close);
    }
    return text;
}

R8Outcome R8Check(Sh2Decompiler& d, const std::string& name, const std::vector<uint16_t>& words, const std::vector<std::vector<uint32_t>>& inputs,
                  const std::string& flags, const std::vector<uint32_t>& lend, uint64_t serial, uint32_t base = 0x06009000u, DecompileResult* keep = nullptr,
                  uint64_t* hash = nullptr)
{
    R8Outcome out;
    CallFixture f = { name, base, words };
    f.noResult = flags.find('n') != std::string::npos;
    const bool voidOk = flags.find('v') != std::string::npos;
    auto snap = CallSnapshot(f, serial);
    DecompileRequest q = Request(base);
    for (uint32_t addr : lend) q.prototypes.emplace_back(addr, d.Decompile(snap, Request(addr)).prototype);
    const DecompileResult r = d.Decompile(snap, q);
    if (keep) *keep = r;
    if (hash)
    {
        uint64_t h = 1469598103934665603ull;
        for (char ch : r.Text()) h = (h ^ static_cast<unsigned char>(ch)) * 1099511628211ull;
        *hash = h;
    }
    out.verdict = "agrees";
    bool viaGlobal = false;
    if (r.status != DecompileStatus::Ok || r.unsupported)
    {
        out.verdict = "not-ok";
        return out;
    }
    for (const std::vector<uint32_t>& args : inputs)
    {
        uint32_t ran = 0;
        if (!CallRunResult(snap, f, args, ran))
        {
            out.verdict = "no-run";
            return out;
        }
        PseudoEval::Entry entry;
        entry.regs = RunEntryRegisters(args);
        entry.snap = snap;
        entry.sp = kRunStack;
        entry.readCode = flags.find('c') != std::string::npos;
        for (const AccessFate& fate : r.accessFates)
            if (!fate.write && fate.constantAddress) entry.globalSize[fate.address] = static_cast<int>(fate.size);
        for (const MemoryAccess& m : r.accesses)
            if (!m.write && m.direct) entry.globalSize[m.address] = static_cast<int>(m.size);
        PseudoEval eval(R8FlattenCommaCondition(r.Text()), args);
        eval.SetEntry(&entry);
        const std::vector<PseudoEval::Outcome> outs = eval.Run();
        if (!eval.Error().empty())
        {
            out.verdict = "cannot-evaluate";
            out.detail = eval.Error();
            return out;
        }
        bool equal = false, unknown = false, none = false;
        std::string saw;
        for (const PseudoEval::Outcome& o : outs)
        {
            if (!o.value)
            {
                none = true;
                continue;
            }
            if (!o.v.known)
            {
                unknown = true;
                continue;
            }
            const uint32_t mask = o.v.size >= 4 ? 0xFFFFFFFFu : (1u << (8 * o.v.size)) - 1u;
            char b[32];
            std::snprintf(b, sizeof b, " %08X", static_cast<uint32_t>(o.v.v));
            saw += b;
            if ((static_cast<uint32_t>(o.v.v) & mask) == (ran & mask)) equal = true;
        }
        char b[160];
        std::snprintf(b, sizeof b, "r4=%08X: the code returned %08X, the text returns%s%s%s", args.empty() ? 0u : args[0], ran, saw.c_str(),
                      unknown ? " (a value it cannot know)" : "", none ? " (nothing)" : "");
        if (equal) continue;
        if (unknown && entry.readCode)
        {
            bool named = false, matched = false;
            const std::string text = r.Text();
            for (size_t at = text.find("Ram"); at != std::string::npos; at = text.find("Ram", at + 3))
            {
                if (at + 11 > text.size()) break;
                char* endp = nullptr;
                const std::string hex = text.substr(at + 3, 8);
                const uint32_t a = static_cast<uint32_t>(std::strtoul(hex.c_str(), &endp, 16));
                if (*endp != '\0') continue;
                named = true;
                uint32_t word = 0;
                if (!snap->ReadU32(a & ~3u, word)) continue;
                const uint32_t half = (a & 2) ? (word & 0xFFFF) : (word >> 16);
                const uint32_t sext = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(half)));
                if ((a & 3) == 0 && word == ran) matched = true;
                if ((a & 1) == 0 && (sext == ran || half == ran)) matched = true;
            }
            if (named && matched)
            {
                viaGlobal = true;
                continue;
            }
            if (named) unknown = false;   // named an address the run's value does not come from: a difference, not an unknown
        }
        out.detail = b;
        out.verdict = unknown ? "unknown" : (none && (f.noResult || voidOk)) ? "void-ok" : "differs";
        if (out.verdict == "differs" || out.verdict == "unknown") return out;
    }
    if (out.verdict == "agrees" && viaGlobal) out.verdict = "agrees-global";
    return out;
}

// SE_AREA2_FIXTURES=<file> runs only this (see main): one fixture a line, `name|words|inputs|flags|lend`; words and inputs in hex (inputs: sets
// separated by `;`, registers r4.. by `,`); flags as above and `p` (print the text whatever the verdict), `b=<hex>` (the base, default
// 0x06009000); lend: callee addresses, hex, comma separated, whose prototypes are lent. SE_AREA2_HASH=1 prints a hash of each text, to compare two
// orders of one file (a leak from a request into the next). Built so a script can draw thousands of fixtures without rebuilding this suite.
std::vector<std::string> R8Split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (c == sep)
        {
            out.push_back(cur);
            cur.clear();
        }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

int TestArea2Round8Fixtures(const char* path)
{
    Sh2Decompiler d(gConfig);
    std::ifstream in(path);
    std::string line;
    uint64_t serial = 70000;
    std::map<std::string, int> verdicts;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> field = R8Split(line, '|');
        if (field.size() < 3) continue;
        std::vector<uint16_t> words;
        {
            std::istringstream ws(field[1]);
            std::string tok;
            while (ws >> tok) words.push_back(static_cast<uint16_t>(std::strtoul(tok.c_str(), nullptr, 16)));
        }
        std::vector<std::vector<uint32_t>> inputs;
        for (const std::string& set : R8Split(field[2], ';'))
        {
            std::vector<uint32_t> args;
            for (const std::string& a : R8Split(set, ','))
                if (!a.empty()) args.push_back(static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16)));
            inputs.push_back(args);
        }
        if (inputs.empty()) inputs.push_back({});
        const std::string flags = field.size() > 3 ? field[3] : "";
        uint32_t base = 0x06009000u;
        const size_t bAt = flags.find("b=");
        if (bAt != std::string::npos) base = static_cast<uint32_t>(std::strtoul(flags.c_str() + bAt + 2, nullptr, 16));
        std::vector<uint32_t> lend;
        if (field.size() > 4)
            for (const std::string& a : R8Split(field[4], ','))
                if (!a.empty()) lend.push_back(static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16)));
        DecompileResult r;
        uint64_t h = 0;
        const R8Outcome o = R8Check(d, field[0], words, inputs, bAt == std::string::npos ? flags : flags.substr(0, bAt), lend, ++serial, base, &r, &h);
        ++verdicts[o.verdict];
        if (std::getenv("SE_AREA2_HASH")) std::printf("R8TEXT %s %016llx\n", field[0].c_str(), static_cast<unsigned long long>(h));
        std::printf("R8 %s [%s] %s\n", o.verdict.c_str(), field[0].c_str(), o.detail.c_str());
        if ((o.verdict != "agrees" && o.verdict != "agrees-global") || flags.find('p') != std::string::npos)
        {
            std::printf("  words: %s\n", field[1].c_str());
            Print(field[0].c_str(), r);
        }
    }
    std::printf("R8 fixtures:");
    for (const auto& kv : verdicts) std::printf(" %s=%d", kv.first.c_str(), kv.second);
    std::printf("\n");
    return 0;
}

// ---- The slot's PC-relative load, by value, for the branches whose slot the round 7 test left to substrings --------------------------------
// jmp, braf, jsr, bsrf and rte, each with mova, mov.w and mov.l in the slot, the branch at both alignments of the code, the target at both
// alignments, a handful of displacements. The loads put distinct halfwords (0x4000 + offset / 2) in the code area, so any wrong address reads a
// different value. What the text can state: jmp and braf to a constant are tail calls (the callee returns r4, which the slot loaded: `func(value)`),
// jsr and bsrf call the same callee, rte returns to the target with r0 from the slot. A `mova` in the slot of a call or a tail call writes r0, which
// the engine's convention does not pass, so there the text states nothing of it (compared as `void-ok`, not as a value). Layout, as slots.py of the
// round: code from 0 (a leading nop if `shift`), the pool word at 0x20, the target at 0x28 or 0x2A, the data from 0x30 on.
struct R8Slot
{
    std::vector<uint16_t> words;
    std::vector<uint32_t> lend;
};

R8Slot R8SlotFixture(const std::string& kind, const std::string& slot, int d, int shift, int talign)
{
    std::map<uint32_t, uint16_t> mem;
    const uint32_t base = 0x06009000u, pool = 0x20u, target = 0x28u + (talign ? 2u : 0u);
    uint32_t off = 0;
    auto put = [&](uint32_t o, uint32_t v) { mem[o] = static_cast<uint16_t>(v); };
    auto poolLoad = [&](uint32_t at, uint32_t reg) {
        const uint32_t b = (at & ~3u) + 4;
        return 0xD000u | reg << 8 | ((pool - b) / 4);
    };
    uint16_t slotWord = 0;
    if (slot == "mova") slotWord = static_cast<uint16_t>(0xC700 | d);
    else if (slot == "movw0") slotWord = static_cast<uint16_t>(0x9000 | d);
    else if (slot == "movl0") slotWord = static_cast<uint16_t>(0xD000 | d);
    else if (slot == "movw4") slotWord = static_cast<uint16_t>(0x9400 | d);
    else slotWord = static_cast<uint16_t>(0xD400 | d);   // movl4
    const bool r4Slot = slot == "movw4" || slot == "movl4";
    R8Slot out;
    uint32_t poolValue = 0;
    bool hasPool = true;
    if (shift) put(off, 0x0009), off += 2;
    if (kind == "jmp" || kind == "braf")
    {
        put(off, poolLoad(off, 1)), off += 2;
        const uint32_t at = off;
        put(off, kind == "jmp" ? 0x412B : 0x0123), off += 2;
        put(off, slotWord), off += 2;
        poolValue = kind == "jmp" ? base + target : target - (at + 4);
        put(target, 0x000B);
        put(target + 2, r4Slot ? 0x6043 : 0x0009);
        if (r4Slot) out.lend.push_back(base + target);
    }
    else if (kind == "jsr" || kind == "bsrf")
    {
        put(off, 0x4F22), off += 2;
        put(off, poolLoad(off, 1)), off += 2;
        const uint32_t at = off;
        put(off, kind == "jsr" ? 0x410B : 0x0103), off += 2;
        put(off, slotWord), off += 2;
        put(off, 0x4F26), off += 2;
        put(off, 0x000B), off += 2;
        put(off, 0x0009), off += 2;
        poolValue = kind == "jsr" ? base + target : target - (at + 4);
        put(target, 0x000B);
        put(target + 2, r4Slot ? 0x6043 : 0x0009);
        out.lend.push_back(base + target);
    }
    else if (kind == "rte_t")
    {
        put(off, 0x4F03), off += 2;
        put(off, poolLoad(off, 1)), off += 2;
        put(off, 0x2F16), off += 2;
        put(off, 0x002B), off += 2;
        put(off, slotWord), off += 2;
        poolValue = base + target;
        put(target, 0x000B);
        put(target + 2, 0x0009);
    }
    else   // rte_s: returns to the sentinel in PR
    {
        put(off, 0x4F03), off += 2;
        put(off, 0x4F22), off += 2;
        put(off, 0x002B), off += 2;
        put(off, slotWord), off += 2;
        hasPool = false;
        put(target, 0x000B);
        put(target + 2, 0x0009);
    }
    if (hasPool)
    {
        put(pool, poolValue >> 16);
        put(pool + 2, poolValue & 0xFFFF);
    }
    for (uint32_t o = 0; o < 0x100; o += 2)
    {
        auto it = mem.find(o);
        if (it != mem.end()) out.words.push_back(it->second);
        else out.words.push_back(o < pool && o >= off ? 0x0009 : static_cast<uint16_t>(0x4000 + o / 2));
    }
    return out;
}

// ===== The routed seeds (ledger A3-55, A4-16; A7-6 below): what the area 2 round 8 review found beyond the nightly =====
// A3-55: seed 519315's function 1 (A3-49's literal variant, withheld since cee85f5) and seed 608802's (a join of a loaded word and a constant, chosen by
// r3, a register no signature passes: the engine drops the return because each alternative has another use, and the read of r3 goes with it).
// A4-16: seed 511038, a jump table read at a constant index its own bound check excludes: flow follows the destination (a literal word, 0x25FE0008,
// uncaptured memory), the actions remove that block as unreachable, and the function was reported cut off by a path it does not have.
void TestRoutedSeeds()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 60000;
    const uint32_t base = 0x06009020u;
    const std::vector<std::vector<uint32_t>> inputs = { { 0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u },
                                                        { 0x00000000u, 0x00000001u, 0x7FFFFFFFu, 0x80000000u },
                                                        { 0x11111111u, 0u, 0x33333333u, 0x44444444u } };
    // The function is withheld for the reason, and the text it would have been lent with is wrong: the oracle rejects it.
    auto withheld = [&](const char* name, std::vector<uint16_t> words, const char* reason) {
        const CallFixture f = { name, base, std::move(words) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!r.prototype.known && Has(r.prototypeWithheld, reason));
        CHECK(!PseudoAgreesWithRun(r, snap, f, inputs));
    };
    auto lentVoid = [&](const char* name, std::vector<uint16_t> words) {
        const CallFixture f = { name, base, std::move(words) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(r.prototype.known && r.prototype.ret.kind == TypeKind::Void);
        return r;
    };

    // ---- A3-55 ---------------------------------------------------------------------------------------------------------------------------
    // Seed 519315, function 1: tst r4,r4 ; bf L ; mov.l @(lit),r6 ; L: tst r5,r5 ; bt M ; mov r6,r0 ; M: rts. A3-49's shape (r6 or a literal on one
    // path, r0's entry value on the other), in a frame of 12 bytes. It failed at 572c29d, before cee85f5, and is withheld since.
    withheld("a3_55 a: seed 519315, function 1", { 0x7FF4, 0x2448, 0x8B00, 0xD603, 0x2558, 0x8900, 0x6063, 0x7F0C, 0x000B, 0x0009, 0x2D80, 0x00C0 },
             "joins r0's entry value with a parameter");
    // Seed 608802: mov.l @(lit),r7 ; mov.w @(0x12,r7),r0 ; mov #-0x5d,r2 ; cmp/eq r0,r3 ; bf L ; mov r2,r0 ; or r0,r1 ; L: rts. r0 is the word, or the
    // constant when it equals r3 (the entry r3: nothing passes it). Lent `void f(void)` and printed `return;`, the caller shown without r3's setup.
    withheld("a3_55 b: seed 608802, r0 is a word or a constant by r3", { 0xD704, 0x8579, 0xE2A3, 0x3300, 0x8B01, 0x6023, 0x210B, 0x000B, 0x0009, 0x0009, 0x2600, 0x2000 },
             "chosen by r3 on entry");
    // The same chosen by r4, which the lent `void f(void)` does not hold: withheld since area 3 round 9 (A3-62; it was a control that stayed lent).
    // A join chosen by a constant has no input in it and stays lent (the scratch r0 of a void function: the signature drops the join, and a caller
    // that reads r0 afterwards is withheld); so does one chosen by a parameter the signature holds (TestArea3Round9Fix).
    withheld("a3_55 control: chosen by r4", { 0xD704, 0x8579, 0xE2A3, 0x3400, 0x8B01, 0x6023, 0x210B, 0x000B, 0x0009, 0x0009, 0x2600, 0x2000 },
             "its return is chosen by r4 on entry, which the signature does not hold");
    lentVoid("a3_55 control: chosen by a constant", { 0xD704, 0x8579, 0xE2A3, 0x8805, 0x8B01, 0x6023, 0x210B, 0x000B, 0x0009, 0x0009, 0x2600, 0x2000 });
    // r3 is compared and decides nothing: r0 is the word on both paths (a join needs two values), so there is no choice for r3 to make.
    // mov.l @(lit),r7 ; mov.w @(0x12,r7),r0 ; cmp/eq r0,r3 ; bf L ; mov r0,r2 ; L: rts ; nop.
    lentVoid("a3_55 control: r3 is compared, r0 is the word on both paths", { 0xD704, 0x8579, 0x3300, 0x8B00, 0x6203, 0x000B, 0x0009, 0x0009, 0x0009, 0x0009, 0x2600, 0x2000 });
    // ...and by a value nothing is known of: a word loaded through a pointer that may be in the stack, after a frame address escaped (every
    // register is among its origins). mov r15,r1 ; mov.l r1,@r4 ; mov.l @r4,r2 ; mov.l @r2,r1 ; tst r1,r1 ; bt L ; mov #1,r0 ; L: rts ; nop.
    // Reversed by the area 3 round 10 fix (the nothing-known exception removed): the pointer reloaded from *r4 may be the entry r15 the function
    // stored there, so the word it reads may be the stack argument at r15 + 0, and that word chooses r0.
    withheld("a3_55 control: chosen by a value nothing is known of", { 0x61F3, 0x2412, 0x6242, 0x6122, 0x2118, 0x8900, 0xE001, 0x000B, 0x0009 },
             "its return is chosen by a stack argument");

    // ---- A4-16 ---------------------------------------------------------------------------------------------------------------------------
    // Seed 511038 shrunk: function 0 is `mov.l @(lit),r6` and runs into function 1, a switch on r6 (`cmp/hi #4,r6 ; bt default ; shll2 r6 ; mova ; mov.l
    // @(r0,r6),r1 ; jmp @r1`). r6 is 0xC0000010, so the guard always branches to the default, but the flow is made before the actions can know:
    // it reads the table at the index 0xC0000010 << 2 (the literal after the table, 0x25FE0008), follows it into uncaptured memory, and the actions
    // remove the block as unreachable. The text is `return 0x6001030;`, right; the function is not cut off.
    {
        const CallFixture f = { "a4_16: a table read at a constant index its guard excludes", 0x06009000u,
                                { 0xD613, 0x7FD0, 0xE104, 0x3616, 0x8919, 0x4608, 0xC701, 0x016E, 0x412B, 0x0009, 0x0600, 0x9028, 0x0600, 0x9028, 0x0600, 0x902E,
                                  0x0600, 0x9032, 0x0600, 0x9038, 0xD70A, 0xA009, 0x0009, 0xA007, 0x0009, 0x855F, 0xA004, 0x0009, 0xE03C, 0xA001, 0x0009, 0xD706,
                                  0xA001, 0x0009, 0xFFFF, 0x6073, 0x7F30, 0x000B, 0x0009, 0x0009, 0xC000, 0x0010, 0x25FE, 0x0008, 0x0600, 0x1030 } };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!r.truncated && r.truncatedCount == 0 && r.truncatedWhy.empty());
        int cuts = 0, said = 0;
        for (const FlowWarning& w : r.flowWarnings)
        {
            cuts += w.kind == FlowWarning::Cut;
            said += w.kind == FlowWarning::Engine && w.address == 0x05FE0008u && Has(w.text, "proved cannot be taken and removed") &&
                    Has(w.text, "uncaptured memory at 0x05FE0008");
        }
        CHECK(cuts == 0 && said == 1);   // said once, where the flow went, and not as a cut
        CHECK(Has(r.Text(), "return 0x6001030;"));
        CHECK(r.badDataAt.empty());      // the function holds no bytes that do not decode
        CHECK(!r.prototype.known);       // still withheld: the engine's own flag for the halt it made stays (bad instruction data), the safe side
    }

    // ---- A7-6 ----------------------------------------------------------------------------------------------------------------------------
    // An SH-2 instruction starts at an even address. A jump table entry that is odd (here the middle of the table's three destinations; a word of data
    // the bound does not exclude) sent flow into the middle of the instruction at the even address before it: the engine said "Instruction at ...
    // overlaps instruction at ..." and the text was made of what it decoded there, under tokens with odd addresses (patch 0052: flow halts at such an
    // address as it does at bytes that do not decode). mov.l @(lit),r2 ; mov #2,r1 ; cmp/hi r1,r4 ; bt def ; shll2 r4 ; mova table,r0 ; mov.l @(r0,r4),r1 ;
    // jmp @r1 ; nop ; nop ; lit 0x06009034 ; table 0x0600902C, 0x06009031 (odd), 0x06009034 ; def: rts ; mov r2,r0 ; cases 0 to 2: rts ; mov #n,r0.
    {
        const uint32_t at = 0x06009000u;
        const CallFixture f = { "a7_6: a jump table with an odd entry", at,
                                { 0xD204, 0xE102, 0x3416, 0x890F, 0x4408, 0xC703, 0x014E, 0x412B, 0x0009, 0x0009, 0x0600, 0x9034, 0x0600, 0x902C, 0x0600, 0x9031,
                                  0x0600, 0x9034, 0x0009, 0x0009, 0x000B, 0x6023, 0x000B, 0xE000, 0x000B, 0xE001, 0x000B, 0xE002 } };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(at));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(std::find(r.badDataAt.begin(), r.badDataAt.end(), 0x06009031u) != r.badDataAt.end());
        bool said = false;
        for (const FlowWarning& w : r.flowWarnings) said |= w.kind == FlowWarning::BadData && w.address == 0x06009031u && Has(w.text, "odd address");
        CHECK(said);
        for (const std::string& w : r.warnings) CHECK(!Has(w, "overlaps instruction"));
        CHECK(Has(r.Text(), "halt_baddata();"));
        // Every token is at an even address but the halt's own, which is counted, not failed; the other destinations are still cases.
        const std::function<bool(uint32_t, uint16_t&)> wordAt = [&f, at](uint32_t a, uint16_t& w) {
            if ((a & 1) != 0 || a < at || (a - at) / 2 >= f.code.size()) return false;
            w = f.code[(a - at) / 2];
            return true;
        };
        const dts::MappingAudit a = dts::AuditMapping(r, wordAt, "a7_6");
        CHECK(a.Violations() == 0 && a.odd == 0 && a.badDataTokens >= 1);
        uint32_t odd = 0;
        for (const Line& l : r.lines)
            for (const Token& t : l.tokens) odd += (t.addr & 1u) != 0 && t.addr != 0x06009031u;
        CHECK(odd == 0);
        // The same table without the odd entry halts nowhere (the control: the even destination is a case).
        std::vector<uint16_t> even = f.code;
        even[15] = 0x9030;
        const CallFixture fe = { "a7_6 control: the same table, an even entry", at, even };
        auto snapEven = CallSnapshot(fe, ++serial);
        const DecompileResult re = d.Decompile(snapEven, Request(at));
        Print(fe.name.c_str(), re);
        CHECK(re.status == DecompileStatus::Ok && re.badDataAt.empty() && !Has(re.Text(), "halt_baddata"));
    }
    // A function asked for at an odd address: refused, not decompiled from the middle of an instruction. Typed in the entry box, or found as the
    // target of a call through a literal that is no code address (58 of the lending runs' functions).
    {
        const uint32_t at = 0x06009000u;
        const CallFixture f = { "a7_6: an odd entry and an odd call target", at,
                                { 0xD102, 0x410B, 0x0009, 0x000B, 0x0009, 0x0009, 0x0600, 0x9007 } };   // mov.l @(lit),r1 ; jsr @r1 ; nop ; rts ; nop ; lit
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult odd = d.Decompile(snap, Request(at + 1));
        CHECK(odd.status == DecompileStatus::NoCode && Has(odd.error, "an SH-2 instruction starts at an even address") && odd.lines.empty());
        const DecompileResult mirror = d.Decompile(snap, Request(0x20000000u + at + 3));   // through the cache-through mirror too
        CHECK(mirror.status == DecompileStatus::NoCode && Has(mirror.error, "even address"));
        const DecompileResult r = d.Decompile(snap, Request(at));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(std::find(r.callees.begin(), r.callees.end(), 0x06009007u) == r.callees.end());   // an odd target is no function to decompile
        CHECK(std::find(r.calledAt.begin(), r.calledAt.end(), 0x06009007u) == r.calledAt.end());
    }
}

// ===== Area 3, round 9 review: what the round 8 fixes held under (controls; the findings are rows A3-56 to A3-64 of the ledger) =====
// None of these is a defect: each prints what the record of round 9 says it held, so a fix of the open rows that disturbs one is seen.
// A3-51 (the stack-slot note): the frame address in the slot is a note or an argument, the local is never folded, whether it is pushed right before
// the call, held in an r14-based frame, in the second slot, passed through two calls, or r5/r6 is set on one path or by a copy. A3-55 (the join
// chosen by a register no signature passes): the choice read through a spilled word, a byte, a push and a pop, a T flag set blocks before, a
// counter, a join of distinct registers; the choice kept in the text (a second return, a load through the register) is the engine's own read and
// withheld as such. Tail calls: the effects of a caller that jumps to a lent callee after restoring a register, popping its frame in the slot or
// restoring PR in the slot.
void TestArea3Round9()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 70000;
    const uint32_t gAt = 0x06009040u;
    const char* const kStackNote = "a frame address is in a stack argument slot here";
    // A caller of an unlent callee that reads (and writes through) a frame address in a stack slot: the text is held to the run.
    auto stackAddress = [&](const CallFixture& f, const std::vector<std::vector<uint32_t>>& inputs) {
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult callee = d.Decompile(snap, Request(gAt));
        CHECK(callee.status == DecompileStatus::Ok && !callee.prototype.known);
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok && !r.prototype.known);
        CHECK(!Has(r.Text(), "return 0x5a;"));
        CHECK(PseudoAgreesWithRun(r, snap, f, inputs, true));
        uint32_t ran = 0;
        CHECK(CallRunResult(snap, f, inputs[0], ran) && ran == 0x37u);
        return r;
    };
    auto withheld = [&](const CallFixture& f, const char* reason) {
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!r.prototype.known && Has(r.prototypeWithheld, reason));
    };
    // A caller that jumps to the lent callee at 0x06009040: lent, held to the run, and the registers it says it keeps are kept.
    auto tailCall = [&](const CallFixture& f, uint32_t keptBit, bool kept) {
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult callee = d.Decompile(snap, Request(gAt));
        CHECK(callee.status == DecompileStatus::Ok && callee.prototype.known);
        DecompileRequest q = Request(f.base);
        q.prototypes.emplace_back(gAt, callee.prototype);
        const DecompileResult r = d.Decompile(snap, q);
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok && r.prototype.known);
        CHECK(PseudoAgreesWithRun(r, snap, f, { { 1, 2, 3, 4 }, { 0, 1, 2, 3 } }));
        CHECK((((r.prototype.effects.preserved >> keptBit) & 1u) != 0) == kept);
        CHECK(r.prototype.effects.spDelta == 0);
    };

    // ---- A3-51: other slot offsets, pushes, frames, calls ----
    {
        const CallFixture f = { "a3_51 round 9 pushed (@-r15) right before the call", 0x06009000u,
{
        0x4F22, 0x7FF8, 0xE05A, 0x1F01, 0x61F3, 0x7104, 0x2F16, 0xD304, 0x430B, 0x0009, 0x7F04, 0x50F1,
        0x7F08, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x62F2, 0xE337, 0x000B, 0x2232 } };
        stackAddress(f, { { 1 }, { 2 } });
    }
    {
        const CallFixture f = { "a3_51 round 9 r14-based frame", 0x06009000u,
{
        0x4F22, 0x2FE6, 0x7FF8, 0x6EF3, 0xE05A, 0x1E01, 0x61E3, 0x7104, 0x2E12, 0xD304, 0x430B, 0x0009,
        0x50E1, 0x7F08, 0x6EF6, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040, 0x0009, 0x0009, 0x0009, 0x0009,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x62F2, 0xE337, 0x000B, 0x2232 } };
        stackAddress(f, { { 1 }, { 2 } });
    }
    {
        const CallFixture f = { "a3_51 round 9 the address passed through two calls", 0x06009000u,
{
        0x4F22, 0x7FF8, 0xE05A, 0x1F01, 0x61F3, 0x7104, 0x2F12, 0xD305, 0x430B, 0x0009, 0xD304, 0x430B,
        0x0009, 0x50F1, 0x7F08, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9060, 0x0600, 0x9040, 0x0009, 0x0009,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x62F2, 0xE337, 0x000B, 0x2232,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
        0x62F2, 0x000B, 0x6122, 0x0009 } };
        stackAddress(f, { { 1 }, { 2 } });
    }
    {
        const CallFixture f = { "a3_51 round 9 the address in the second slot, the first written", 0x06009000u,
{
        0x4F22, 0x7FF4, 0xE05A, 0x1F02, 0x61F3, 0x7108, 0x1F11, 0xD304, 0x430B, 0x0009, 0x50F2, 0x7F0C,
        0x4F26, 0x000B, 0x0009, 0x0009, 0x0600, 0x9040, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x52F1, 0xE337, 0x000B, 0x2232 } };
        stackAddress(f, { { 1 }, { 2 } });
    }
    {
        const CallFixture f = { "a3_51 round 9 r5 set on one path only", 0x06009000u,
{
        0x4F22, 0x7FF8, 0xE05A, 0x1F01, 0x61F3, 0x7104, 0x2F12, 0x2448, 0x8900, 0xE501, 0xD303, 0x430B,
        0x0009, 0x50F1, 0x7F08, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040, 0x0009, 0x0009, 0x0009, 0x0009,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x62F2, 0xE337, 0x000B, 0x2232 } };
        stackAddress(f, { { 1 }, { 0 } });
    }
    {
        const CallFixture f = { "a3_51 round 9 r6 set by a copy of r4", 0x06009000u,
{
        0x4F22, 0x7FF8, 0xE05A, 0x1F01, 0x61F3, 0x7104, 0x2F12, 0x6643, 0xD303, 0x430B, 0x0009, 0x50F1,
        0x7F08, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x62F2, 0xE337, 0x000B, 0x2232 } };
        stackAddress(f, { { 1 }, { 2 } });
    }
    (void)kStackNote;
    // ---- A3-55: the choice is in the text, or is read through a register the rule follows ----
    {
        const CallFixture f = { "a3_55 round 9 chosen by r3 spilled to a stack word and reloaded", 0x06009000u,
{
        0x7FF8, 0x2F32, 0x61F2, 0x2118, 0x8903, 0xE001, 0x210B, 0xA002, 0x0009, 0xE002, 0x210B, 0x7F08,
        0x000B, 0x0009 } };
        withheld(f, "chosen by r3");
    }
    {
        const CallFixture f = { "a3_55 round 9 chosen by a byte of r3 spilled and reloaded", 0x06009000u,
{
        0x7FF8, 0x2F30, 0x61F0, 0x2118, 0x8903, 0xE001, 0x210B, 0xA002, 0x0009, 0xE002, 0x210B, 0x7F08,
        0x000B, 0x0009 } };
        withheld(f, "chosen by r3");
    }
    {
        const CallFixture f = { "a3_55 round 9 chosen by r3 pushed and popped", 0x06009000u,
{
        0x2F36, 0x61F6, 0x2118, 0x8903, 0xE001, 0x210B, 0xA002, 0x0009, 0xE002, 0x210B, 0x000B, 0x0009 } };
        withheld(f, "chosen by r3");
    }
    {
        const CallFixture f = { "a3_55 round 9 chosen by T set by tst r3 two blocks earlier", 0x06009000u,
{
        0x2338, 0xE203, 0xA000, 0x0009, 0xE204, 0x8903, 0xE001, 0x210B, 0xA002, 0x0009, 0xE002, 0x210B,
        0x000B, 0x0009 } };
        withheld(f, "chosen by r3");
    }
    {
        const CallFixture f = { "a3_55 round 9 chosen by dt r3", 0x06009000u,
{
        0x4310, 0x8903, 0xE001, 0x210B, 0xA002, 0x0009, 0xE002, 0x210B, 0x000B, 0x0009 } };
        withheld(f, "chosen by r3");
    }
    {
        const CallFixture f = { "a3_55 round 9 r5 or r6 chosen by r3", 0x06009000u,
{
        0x6053, 0x3300, 0x8B01, 0x6063, 0x210B, 0x000B, 0x0009, 0x0009 } };
        withheld(f, "chosen by r3");
    }
    {
        const CallFixture f = { "a3_55 round 9 chosen by a load through r3 (the engine keeps the read)", 0x06009000u,
{
        0xD705, 0x6071, 0xE2A3, 0x3200, 0x6132, 0x2118, 0x8B01, 0x6023, 0x210B, 0x000B, 0x0009, 0x0009,
        0x0600, 0x9100 } };
        withheld(f, "reads r3 on entry");
    }
    {
        const CallFixture f = { "a3_55 round 9 two return sites chosen by r3 (the engine keeps the branch)", 0x06009000u,
{
        0x2338, 0x8903, 0xE001, 0x210B, 0x000B, 0x0009, 0xE002, 0x210B, 0x000B, 0x0009 } };
        withheld(f, "reads r3 on entry");
    }
    // ---- tail calls: the caller's effects ----
    {
        const CallFixture f = { "a3_27 round 9 tail call after r8 is saved and restored: r8 kept", 0x06009000u,
{
        0x2F86, 0x6843, 0x7801, 0x68F6, 0xD101, 0x412B, 0x0009, 0x0009, 0x0600, 0x9040, 0x0009, 0x0009,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x6043, 0x7001, 0x000B, 0x0009 } };
        tailCall(f, 8, true);
    }
    {
        const CallFixture f = { "a3_27 round 9 tail call to a callee that clobbers r9 without saving it: r9 not kept", 0x06009000u,
{
        0xD101, 0x412B, 0x0009, 0x0009, 0x0600, 0x9040, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0xE905, 0x6043, 0x000B, 0x0009 } };
        tailCall(f, 9, false);
    }
    {
        const CallFixture f = { "a3_27 round 9 tail call restoring PR in the slot of the jump", 0x06009000u,
{
        0x4F22, 0x6543, 0xD101, 0x412B, 0x4F26, 0x0009, 0x0600, 0x9040, 0x0009, 0x0009, 0x0009, 0x0009,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x6053, 0x7003, 0x000B, 0x0009 } };
        tailCall(f, 8, true);
    }
    {
        const CallFixture f = { "a3_27 round 9 tail call popping the frame in the slot of the jump", 0x06009000u,
{
        0x7FF8, 0x6543, 0xD101, 0x412B, 0x7F08, 0x0009, 0x0600, 0x9040, 0x0009, 0x0009, 0x0009, 0x0009,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
        0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x6053, 0x7003, 0x000B, 0x0009 } };
        tailCall(f, 8, true);
    }
}

// ===== Area 3, round 9 fix: the return rule, and the stack argument words (A3-56 to A3-62) =====
// The return rule (WithholdReason, ReturnCheck::Inputs): a function the engine made `void` is lent only when no input its signature does not hold
// can change what it leaves in r0, by data or by the choice of the path, with r0's value at all the returns taken as one value whose
// alternatives are told apart by the op that made them. Each fixture of the round's rows is withheld and the text it would be lent with is
// rejected by the run; the controls are the limits that stay lent.
void TestArea3Round9Fix()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 90000;
    const uint32_t base = 0x06009000u;
    const std::vector<std::vector<uint32_t>> inputs = { { 0x11111111u, 0x22222222u, 0x0600B000u, 0x44444444u },
                                                        { 0x00000000u, 0x00000001u, 0x0600B000u, 0x80000000u },
                                                        { 0x00000001u, 0u, 0x0600B000u, 0x44444444u } };
    auto withheld = [&](const char* name, std::vector<uint16_t> words, const char* reason) {
        const CallFixture f = { name, base, std::move(words) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!r.prototype.known && Has(r.prototypeWithheld, reason));
        CHECK(!PseudoAgreesWithRun(r, snap, f, inputs));   // the engine's `void` text loses what the code returns
    };
    auto lentVoid = [&](const char* name, std::vector<uint16_t> words) {
        const CallFixture f = { name, base, std::move(words) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(r.prototype.known && r.prototype.ret.kind == TypeKind::Void);
        return r;
    };
    // The two alternatives each used by an `or r0,r1` nothing reads, so the engine drops the return (A3-55): `<test> ; bt L ; mov #1,r0 ;
    // or r0,r1 ; bra M ; nop ; L: mov #2,r0 ; or r0,r1 ; M: rts ; nop`.
    auto choice = [](std::vector<uint16_t> test) {
        for (uint16_t w : { 0x8903, 0xE001, 0x210B, 0xA002, 0x0009, 0xE002, 0x210B, 0x000B, 0x0009 }) test.push_back(w);
        return test;
    };

    // ---- A3-56: r0's entry value as the chooser (H2), `tst r0,r0` and `cmp/eq #5,r0` --------------------------------------------------------
    withheld("a3_56 H2: tst r0,r0 chooses", choice({ 0x2008 }), "its return is chosen by r0 on entry, which no signature passes");
    withheld("a3_56: cmp/eq #5,r0 chooses", choice({ 0x8805 }), "its return is chosen by r0 on entry, which no signature passes");
    withheld("a3_56 control: tst r3,r3 chooses", choice({ 0x2338 }), "its return is chosen by r3 on entry, which no signature passes");

    // ---- A3-57: the entry T flag (H5) or SR (H6) as the chooser --------------------------------------------------------------------------------
    withheld("a3_57 H5: bt on the T the caller left", choice({}), "its return is chosen by the T flag on entry, which no signature passes");
    withheld("a3_57 H6: stc sr,r1 ; tst r1,r1", choice({ 0x0102, 0x2118 }), "its return is chosen by sr on entry, which no signature passes");
    // Controls: a T this function wrote (clrt, sett, a compare of a constant) chooses nothing that is an input; a T written by `tst r3,r3` is r3.
    lentVoid("a3_57 control: clrt before the branch", choice({ 0x0008 }));
    lentVoid("a3_57 control: cmp/eq of two constants", choice({ 0xE105, 0xE205, 0x3210 }));
    withheld("a3_57 control: T written by tst r3,r3", choice({ 0x2338 }), "chosen by r3 on entry");
    // ...and SR's interrupt mask transformed whole and left in r0 with no join (`stc sr,r0 ; or #0xF0,r0 ; ldc r0,sr`, 06010054's idiom): lent (the
    // engine returns it here; image 007's 06010054, where it does not, is lent in the corpus run).
    {
        const CallFixture f = { "a3_57 control: the interrupt-mask idiom, no join", base, { 0x0002, 0xCBF0, 0x400E, 0x000B, 0x0009 } };
        const DecompileResult r = d.Decompile(CallSnapshot(f, ++serial), Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.prototype.known);
    }

    // ---- A3-58: two alternatives equal as values and made by two ops ---------------------------------------------------------------------------
    // (D1) two words through two literal pointers, by r3: mov.l @(lit),r7 ; mov.l @(lit),r2 ; mov.w @r7,r0 ; cmp/eq r0,r3 ; bf L ; mov.w @r2,r0 ;
    // or r0,r1 ; L: rts ; nop -- the words at 0x06009040 (0x1234) and 0x06009044 (0x5678).
    {
        std::vector<uint16_t> w = { 0xD704, 0xD205, 0x6071, 0x3300, 0x8B01, 0x6021, 0x210B, 0x000B, 0x0009, 0x0009, 0x0600, 0x9040, 0x0600, 0x9044 };
        w.resize(32, 0x0009);
        for (uint16_t x : { 0x1234, 0x0000, 0x5678, 0x0000 }) w.push_back(x);
        withheld("a3_58 D1: two loaded words, chosen by r3", w, "its return is chosen by r3 on entry, which no signature passes");
    }
    // (D4) r4 + 1 or r4 + 2 by r3: the value is r4's as well, which the signature drops.
    withheld("a3_58 D4: r4 + 1 or r4 + 2, chosen by r3", { 0x6043, 0x7001, 0x3300, 0x8B02, 0x6043, 0x7002, 0x210B, 0x000B, 0x0009 },
             "its return is chosen by r3 on entry, which no signature passes");
    // Control: one load on both paths is one value (no choice for r3 to make), as A3-55's control "r0 is the word on both paths".
    lentVoid("a3_58 control: one word on both paths", { 0x6052, 0x3300, 0x8B00, 0x6203, 0x2542, 0x000B, 0x0009 });

    // ---- A3-59: r0's entry value computed from on one path, with a parameter the signature holds -----------------------------------------------
    withheld("a3_59 E1: add r4,r0 under tst r5, r4 stored", { 0x2558, 0x8901, 0x304C, 0x2642, 0x000B, 0x0009 }, "its return is computed from r0 on entry");
    withheld("a3_59 E3: add r4,r0 under tst r4, r4 stored", { 0x2448, 0x8901, 0x304C, 0x2642, 0x000B, 0x0009 }, "its return is computed from r0 on entry");
    // ...and r0 read as the index of a load on one path (`mov.b @(r0,r4),r0`): the address is read too (ReturnCheck's loads; here the engine keeps
    // the load, and with it its own read of r0).
    withheld("a3_59: r0 an index on one path", { 0x2558, 0x8901, 0x004C, 0x2642, 0x000B, 0x0009 }, "r0 on entry");

    // ---- A3-62: a chooser the signature does not hold: a parameter (H4), a stack argument (S1) ---------------------------------------------------
    withheld("a3_62 H4: tst r4,r4 chooses", choice({ 0x2448 }), "its return is chosen by r4 on entry, which the signature does not hold");
    withheld("a3_62 S1: the word at r15 chooses", choice({ 0x63F2, 0x2338 }),
             "its return is chosen by a stack argument (the word at r15 + 0 on entry), which the signature does not hold");
    withheld("a3_62: the second stack word chooses", choice({ 0x53F1, 0x2338 }),
             "its return is chosen by a stack argument (the word at r15 + 4 on entry), which the signature does not hold");
    // Controls: chosen by r4 when the signature holds it (stored through r5 as well), and chosen by a constant (A3-25): lent.
    lentVoid("a3_62 control: r4 held by the signature", choice({ 0x2542, 0x2448 }));
    lentVoid("a3_62 control: chosen by a constant", choice({ 0xE101, 0x2118 }));
    // ...and a word of the frame the function wrote before it reads it is no stack argument: add #-4,r15 ; mov.l r4,@r15 ; mov.l @r15,r3 ;
    // tst r3,r3 ; ... ; add #4,r15 in the slot. r4 is the chooser, which the signature does not hold.
    {
        std::vector<uint16_t> w = choice({ 0x7FFC, 0x2F42, 0x63F2, 0x2338 });
        w.back() = 0x7F04;
        withheld("a3_62: a spilled r4 chooses", w, "its return is chosen by r4 on entry, which the signature does not hold");
    }

    // ---- The rule's other parts, each pinned ---------------------------------------------------------------------------------------------------
    // Two returns, one value each, chosen by r3: the values at the returns are one value only when one op made them (the returns' r0s are joined
    // as at a common exit). tst r3,r3 ; bt L ; mov #1,r0 ; rts ; cmp/eq #5,r0 ; L: mov #2,r0 ; rts ; cmp/eq #5,r0. The engine keeps the branch
    // between two returns (here and in every such shape built), and its own read of r3 withholds the function: a control.
    {
        const CallFixture f = { "round 9 fix: two returns chosen by r3", base, { 0x2338, 0x8902, 0xE001, 0x000B, 0x8805, 0xE002, 0x000B, 0x8805 } };
        const DecompileResult r = d.Decompile(CallSnapshot(f, ++serial), Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(!r.prototype.known && Has(r.prototypeWithheld, "r3 on entry"));
    }
    // D4's shape (A3-58) with the compare against a word loaded through r5 (memory, no input) for r3, the values made from r7, which the dropped
    // return takes with it: mov.l @r5,r3 ; mov r7,r0 ; add #1,r0 ; cmp/eq r0,r3 ; bf L ; mov r7,r0 ; add #2,r0 ; or r0,r1 ; L: rts ; nop.
    withheld("round 9 fix: r7 + 1 or r7 + 2, chosen by a loaded word", { 0x6352, 0x6073, 0x7001, 0x3300, 0x8B02, 0x6073, 0x7002, 0x210B, 0x000B, 0x0009 },
             "its return is computed from r7 on entry, which the signature does not hold");
    // r0 joined with a word loaded through a pointer that may be anywhere in the stack (a frame address escaped through r4 first): nothing is known
    // of it, which is taken for memory (A3-55's limit, for data); r5 is held (stored too). Lent void.
    // Reversed by the area 3 round 10 fix: the word may be the stack argument at the entry r15 (the frame address escaped through r4 and came back).
    withheld("round 9 fix: r0 or a word nothing is known of", { 0x61F3, 0x2412, 0x6242, 0x1451, 0x2558, 0x8900, 0x6022, 0x000B, 0x0009 },
             "its return is computed from a stack argument");
    // The interrupt-mask idiom as 06010054 has it (SR whole with the mask set, written back, and the saved SR masked and written back), r0 left
    // with no other use: the machine's state, lent void.
    lentVoid("round 9 fix: the interrupt-mask idiom, 06010054's", { 0x0002, 0xCBF0, 0x400E, 0xE30F, 0x0102, 0x2139, 0x410E, 0x000B, 0x0009 });
    // An identity op on one path (`and r0,r0`, A2-43) is no read of r0: r4 is held (stored through r5), so nothing is dropped. Lent void.
    lentVoid("round 9 fix: and r0,r0 on one path", { 0x2542, 0x2448, 0x8900, 0x2009, 0x000B, 0x0009 });
    // SR transformed whole (the interrupt-mask idiom) and compared, with no join: lent (the engine returns it here; image 007's 06010054, where it
    // does not, is the corpus's).
    {
        const CallFixture f = { "round 9 fix: the interrupt-mask idiom compared", base, { 0x0002, 0xCBF0, 0x400E, 0x8801, 0x000B, 0x0009 } };
        const DecompileResult r = d.Decompile(CallSnapshot(f, ++serial), Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.prototype.known);
    }

    // ---- A3-60: the stack words the engine drops, held to the engine -------------------------------------------------------------------------
    // A caller whose stack word K (at the call's r15 + 4K) holds the address of its local (the word above them), with 'earlier' (bit j: word j < K)
    // written and 'regs' (bit n - 4: rN) set before a `bsr` to g, which reads its word K and stores 0x37 through it; the caller returns the local.
    // The engine drops every stack word when r4-r6 or r5-r7 are all unset, and otherwise the words from the first one not written up, whatever
    // r4-r7 hold: the note is said exactly then, and in no cell is the local folded (`return 0x5a;`), by the note or by the argument the call shows.
    {
        const uint32_t gAt = 0x06009040u;
        const char* const kStackNote = "a frame address is in a stack argument slot here";
        int cells = 0, noted = 0;
        auto cell = [&](int K, uint32_t earlier, uint32_t regs) {
            std::vector<uint16_t> w = { 0x4F22, static_cast<uint16_t>(0x7F00 | ((-(4 * (K + 2))) & 0xFF)), 0xE05A, static_cast<uint16_t>(0x1F00 | (K + 1)),
                                        0x61F3, static_cast<uint16_t>(0x7100 | (4 * (K + 1))), static_cast<uint16_t>(K == 0 ? 0x2F12 : 0x1F10 | K) };
            for (int j = 0; j < K; ++j)
                if ((earlier >> j) & 1u) w.push_back(static_cast<uint16_t>(j == 0 ? 0x2F02 : 0x1F00 | j));   // mov.l r0,@(4j,r15)
            for (int n = 4; n < 8; ++n)
                if ((regs >> (n - 4)) & 1u) w.push_back(static_cast<uint16_t>(0xE001 | n << 8));         // mov #1,rN
            const uint32_t bsrAt = base + 2 * static_cast<uint32_t>(w.size());
            w.push_back(static_cast<uint16_t>(0xB000 | (((gAt - bsrAt - 4) / 2) & 0xFFF)));
            for (uint16_t x : { uint16_t(0x0009), static_cast<uint16_t>(0x50F0 | (K + 1)), static_cast<uint16_t>(0x7F00 | (4 * (K + 2))), uint16_t(0x4F26),
                                uint16_t(0x000B), uint16_t(0x0009) })
                w.push_back(x);
            w.resize(32, 0x0009);
            for (uint16_t x : { static_cast<uint16_t>(K == 0 ? 0x62F2 : 0x52F0 | K), uint16_t(0xE337), uint16_t(0x000B), uint16_t(0x2232) }) w.push_back(x);
            bool hole = false;
            for (int j = 0; j < K; ++j) hole = hole || !((earlier >> j) & 1u);
            const bool r4 = regs & 1u, r5 = regs & 2u, r6 = regs & 4u, r7 = regs & 8u;
            const bool dropped = (!r4 && !r5 && !r6) || (!r5 && !r6 && !r7) || hole;
            const std::string name = "a3_60 cell: word " + std::to_string(K) + ", earlier words " + std::to_string(earlier) + ", r4-r7 " + std::to_string(regs);
            const CallFixture f = { name, base, w };
            auto snap = CallSnapshot(f, ++serial);
            const DecompileResult r = d.Decompile(snap, Request(f.base));
            Print(f.name.c_str(), r);
            CHECK(r.status == DecompileStatus::Ok);
            CHECK(!Has(r.Text(), "return 0x5a;"));
            CHECK(Has(r.Text(), kStackNote) == dropped);
            CHECK(PseudoAgreesWithRun(r, snap, f, { { 1 }, { 2 } }, true));
            uint32_t ran = 0;
            CHECK(CallRunResult(snap, f, { 1 }, ran) && ran == 0x37u);
            ++cells;
            noted += dropped;
        };
        // (i) the address in the second word, the first written or not, every subset of r4-r7: 32 cells
        for (uint32_t earlier = 0; earlier < 2; ++earlier)
            for (uint32_t regs = 0; regs < 16; ++regs) cell(1, earlier, regs);
        // (ii) the address in word 1, 2 or 3, every subset of the words before it written, r4-r7 all unset, all set, or r5 and r6: 42 cells
        for (int K = 1; K <= 3; ++K)
            for (uint32_t earlier = 0; earlier < (1u << K); ++earlier)
                for (uint32_t regs : { 0u, 0xFu, 0x6u }) cell(K, earlier, regs);
        // (iii) the address in word 1 and word 0 written on one path only (`tst r4,r4 ; bt L ; mov.l r0,@r15 ; L:`, r5-r7 set): a word written on one
        // path is not written for the engine, which drops the address with the word after it: the note.
        {
            std::vector<uint16_t> w = { 0x4F22, 0x7FF4, 0xE05A, 0x1F02, 0x61F3, 0x7108, 0x1F11, 0x2448, 0x8900, 0x2F02, 0xE501, 0xE601, 0xE701 };
            const uint32_t bsrAt = base + 2 * static_cast<uint32_t>(w.size());
            w.push_back(static_cast<uint16_t>(0xB000 | (((gAt - bsrAt - 4) / 2) & 0xFFF)));
            for (uint16_t x : { 0x0009, 0x50F2, 0x7F0C, 0x4F26, 0x000B, 0x0009 }) w.push_back(x);
            w.resize(32, 0x0009);
            for (uint16_t x : { 0x52F1, 0xE337, 0x000B, 0x2232 }) w.push_back(x);
            const CallFixture f = { "a3_60: word 0 written on one path", base, w };
            auto snap = CallSnapshot(f, ++serial);
            const DecompileResult r = d.Decompile(snap, Request(f.base));
            Print(f.name.c_str(), r);
            CHECK(Has(r.Text(), kStackNote) && !Has(r.Text(), "return 0x5a;"));
            CHECK(PseudoAgreesWithRun(r, snap, f, { { 1 }, { 0 } }, true));
        }
        std::printf("a3_60: %d cells, the note in %d\n", cells, noted);
        CHECK(cells == 74 && noted == 6 + 13 + 14 + 22);
    }

    // ---- A3-61: a callee that writes its own stack argument slot --------------------------------------------------------------------------------
    // The caller stores a value in the word at its r15 (the callee's fifth argument) and reads it back after a call through a literal; the callee
    // writes 0x37 there through its own r15. The engine took the call to write none of its caller's words and folded the read (`return 7;`).
    {
        const char* const kSlotNote = "a local at or above the stack pointer is read after this call";
        auto atG = [](std::vector<uint16_t> caller, std::vector<uint16_t> g) {
            caller.resize(32, 0x0009);
            caller.insert(caller.end(), g.begin(), g.end());
            return caller;
        };
        auto slot = [&](const char* name, std::vector<uint16_t> words, uint32_t returns, bool note, const char* stale) {
            const CallFixture f = { name, base, std::move(words) };
            auto snap = CallSnapshot(f, ++serial);
            const DecompileResult r = d.Decompile(snap, Request(f.base));
            Print(f.name.c_str(), r);
            CHECK(r.status == DecompileStatus::Ok && !r.prototype.known);
            CHECK(Has(r.Text(), kSlotNote) == note);
            if (stale) CHECK(!Has(r.Text(), stale));
            CHECK(PseudoAgreesWithRun(r, snap, f, { { 1 }, { 2 } }, true));
            uint32_t ran = 0;
            CHECK(CallRunResult(snap, f, { 1 }, ran) && ran == returns);
        };
        // (SA2) the first word, r4-r7 unset (the engine drops the word from the call too)
        slot("a3_61 SA2: the callee writes the word at its r15", atG({ 0x4F22, 0x7FF8, 0xE007, 0x2F02, 0xD303, 0x430B, 0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040 },
                                                                     { 0xE337, 0x2F32, 0x000B, 0x0009 }), 0x37u, true, "return 7;");
        // (SA3) the second word
        slot("a3_61 SA3: the second word", atG({ 0x4F22, 0x7FF8, 0xE007, 0x1F01, 0xD303, 0x430B, 0x0009, 0x50F1, 0x7F08, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040 },
                                                { 0xE337, 0x1F31, 0x000B, 0x0009 }), 0x37u, true, "return 7;");
        // (SA5) r5 set: the engine shows the word as the fifth argument, and folded the local all the same
        slot("a3_61 SA5: r5 set, the word an argument", atG({ 0x4F22, 0x7FF8, 0xE007, 0x2F02, 0xE501, 0xD304, 0x430B, 0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009, 0x0009, 0x0600, 0x9040 },
                                                         { 0xE337, 0x2F32, 0x000B, 0x0009 }), 0x37u, true, "return 7;");
        // (SA6) the callee copies its fifth argument (7) into its sixth (the caller's 9): the text returned 9
        slot("a3_61 SA6: the callee copies the fifth word into the sixth", atG({ 0x4F22, 0x7FF8, 0xE007, 0x2F02, 0xE009, 0x1F01, 0xD303, 0x430B, 0x0009, 0x50F1, 0x7F08, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040 },
                                                                            { 0x61F2, 0x1F11, 0x000B, 0x0009 }), 7u, true, "return 9;");
        // ...and written again after the call, before the read: the read is the caller's own store, no note.
        slot("a3_61 control: the word rewritten after the call", atG({ 0x4F22, 0x7FF8, 0xE007, 0x2F02, 0xD304, 0x430B, 0x0009, 0xE109, 0x2F12, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040 },
                                                                   { 0xE337, 0x2F32, 0x000B, 0x0009 }), 9u, false, nullptr);
        // Controls: a callee that only reads the word (SA1's, A3-51: the accepted text of an unlent call), and one that writes its own frame below its
        // entry r15: no note, and the local is the value stored (the engine's fold is right).
        slot("a3_61 control: the callee reads the word", atG({ 0x4F22, 0x7FF8, 0xE007, 0x2F02, 0xD303, 0x430B, 0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040 },
                                                          { 0x61F2, 0x000B, 0x0009 }), 7u, false, nullptr);
        slot("a3_61 control: the callee writes below its entry r15", atG({ 0x4F22, 0x7FF8, 0xE007, 0x2F02, 0xD303, 0x430B, 0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040 },
                                                                       { 0x7FFC, 0xE337, 0x2F32, 0x000B, 0x7F04 }), 7u, false, nullptr);
    }
}


// ===== Area 2, round 8 review =====
// (1) The slot loads above, by value. (2) The A2-47 family in ordinary code, 19 fixtures that printed a wrong value with patch 0046 reverted:
// a value built from a boolean as `T * A + (1 - T) * B` (mul.l, sts macl), then a mask or a shift pair or an extension that reads the low
// bits (Sh2GeneratedTests' scratch grammar of the round: 20,000 functions of these templates and 20,000 of a wider register-only grammar, 0 findings
// on the fixed tree, 18 of 400 with patch 0046 reverted). (3) A2-53: the stack pointer on entry is taken to be 256-aligned. (5) A2-52: what patch 0046 leaves of A2-47. (6) the spec's decompiler-only lines (A2-55).
void TestArea2Round8()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 40000;
    // ---- (1) ----
    {
        std::map<std::string, int> verdicts;
        int fixtures = 0;
        const char* const kinds[] = { "jmp", "braf", "jsr", "bsrf", "rte_t", "rte_s" };
        for (const char* kind : kinds)
        {
            const bool call = std::strcmp(kind, "jsr") == 0 || std::strcmp(kind, "bsrf") == 0;
            const bool tail = std::strcmp(kind, "jmp") == 0 || std::strcmp(kind, "braf") == 0;
            std::vector<const char*> slots;
            if (tail) slots = { "movw4", "movl4" };
            else if (call) slots = { "movw4", "movl4", "mova" };
            else slots = { "mova", "movw0", "movl0" };
            for (const char* slot : slots)
                for (int d2 : { 1, 2, 3, 5 })
                    for (int shift = 0; shift < 2; ++shift)
                        for (int talign = 0; talign < 2; ++talign)
                        {
                            if (std::strcmp(kind, "rte_s") == 0 && talign) continue;
                            const R8Slot f = R8SlotFixture(kind, slot, d2, shift, talign);
                            const std::string name = std::string(kind) + " + " + slot + " d=" + std::to_string(d2) + (shift ? " branch at 2" : " branch at 0") +
                                                     (talign ? " target at 2 mod 4" : " target at 0 mod 4");
                            const bool voidSlot = call && std::strcmp(slot, "mova") == 0;
                            const R8Outcome o = R8Check(d, name, f.words, { { 0 } }, voidSlot ? "cv" : "c", f.lend, ++serial);
                            ++fixtures;
                            ++verdicts[o.verdict];
                            if (voidSlot) CHECK(o.verdict == "void-ok" || o.verdict == "agrees");
                            else if (o.verdict != "agrees" && o.verdict != "agrees-global")
                            {
                                // d = 0 of a call reads the callee's own code, a global named by the function: not a fixture
                                std::printf("  slot value %s: %s %s\n", name.c_str(), o.verdict.c_str(), o.detail.c_str());
                                CHECK(false);
                            }
                        }
        }
        std::printf("A2 round 8, slot loads of jmp/braf/jsr/bsrf/rte by value: %d fixtures:", fixtures);
        for (const auto& kv : verdicts) std::printf(" %s=%d", kv.first.c_str(), kv.second);
        std::printf("\n");
        CHECK(verdicts["differs"] == 0 && verdicts["unknown"] == 0 && verdicts["not-ok"] == 0 && verdicts["no-run"] == 0);
        CHECK(verdicts["agrees"] + verdicts["agrees-global"] >= 100);
    }
    struct Family
    {
        const char* name;
        std::vector<uint16_t> code;
    };
    // ---- (2) ----
    {
        const std::vector<Family> families = {
            { "t5", { 0x4511, 0x0129, 0xD208, 0xD309, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x4009, 0x4009, 0x4008, 0x4008, 0x7001, 0x000B, 0x0009, 0x0009, 0x0001, 0x0022, 0x0000, 0x0020 } },
            { "t7", { 0x3567, 0x0129, 0xD207, 0xD308, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0xD704, 0x2679, 0x6063, 0x4000, 0x000B, 0x0009, 0x0000, 0x0106, 0x0000, 0x0100, 0x0000, 0x003C } },
            { "t55", { 0x3457, 0x0129, 0xD207, 0xD308, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x4009, 0x4008, 0x4000, 0x000B, 0x0009, 0x0009, 0x0600, 0x901E, 0x0600, 0x900C } },
            { "t59", { 0x4415, 0x0129, 0xD207, 0xD308, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x4009, 0x4008, 0x4000, 0x000B, 0x0009, 0x0009, 0x0600, 0x911A, 0x0600, 0x9018 } },
            { "t127", { 0x2458, 0x0129, 0xD207, 0xD308, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0xD704, 0x2679, 0x6063, 0x4000, 0x000B, 0x0009, 0x0000, 0x0FFA, 0x0000, 0x1000, 0xFFFF, 0xFFFC } },
            { "t131", { 0x3566, 0x0129, 0xD206, 0xD307, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0xC903, 0x4000, 0x000B, 0x0009, 0x0601, 0x8FFE, 0x0600, 0x8FFC } },
            { "t146", { 0x3467, 0x0129, 0xD207, 0xD308, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x4009, 0x4008, 0x4000, 0x000B, 0x0009, 0x0009, 0x0000, 0x1016, 0x0000, 0x1000 } },
            { "t165", { 0x4415, 0x0129, 0xD208, 0xD309, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x4009, 0x4009, 0x4008, 0x4008, 0x7001, 0x000B, 0x0009, 0x0009, 0x0600, 0x9052, 0x0600, 0x9020 } },
            { "t209", { 0x3457, 0x0129, 0xD206, 0xD307, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x4009, 0x4008, 0x000B, 0x0009, 0x0000, 0x1012, 0x0000, 0x1000 } },
            { "t237", { 0x3477, 0x0129, 0xD207, 0xD308, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x4009, 0x4008, 0x4001, 0x000B, 0x0009, 0x0009, 0x0600, 0x9042, 0x0600, 0x9010 } },
            { "t238", { 0x4515, 0x0129, 0xD207, 0xD308, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x4009, 0x4008, 0x7001, 0x000B, 0x0009, 0x0009, 0x0000, 0x0101, 0x0000, 0x0100 } },
            { "t246", { 0x2458, 0x0129, 0xD208, 0xD309, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x4009, 0x4009, 0x4008, 0x4008, 0x7001, 0x000B, 0x0009, 0x0009, 0x0000, 0x011A, 0x0000, 0x0100 } },
            { "t268", { 0x4515, 0x0129, 0xD207, 0xD308, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x4009, 0x4008, 0x4000, 0x000B, 0x0009, 0x0009, 0x0600, 0x902E, 0x0600, 0x8FFC } },
            { "t291", { 0x4411, 0x0129, 0xD206, 0xD307, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0xC903, 0x4001, 0x000B, 0x0009, 0x0601, 0x901A, 0x0600, 0x9018 } },
            { "t314", { 0x3460, 0x0129, 0xD206, 0xD307, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x600C, 0x7001, 0x000B, 0x0009, 0x0600, 0x9008, 0x0600, 0x9000 } },
            { "t315", { 0x4411, 0x0129, 0xD206, 0xD307, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x4019, 0x4018, 0x000B, 0x0009, 0x0000, 0x0116, 0x0000, 0x0100 } },
            { "t330", { 0x4415, 0x0129, 0xD207, 0xD308, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x4019, 0x4018, 0x4000, 0x000B, 0x0009, 0x0009, 0x0000, 0x00FE, 0x0000, 0x0100 } },
            { "t388", { 0x3566, 0x0129, 0xD208, 0xD309, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x4009, 0x4009, 0x4008, 0x4008, 0x7001, 0x000B, 0x0009, 0x0009, 0x0000, 0x0028, 0x0000, 0x0020 } },
            { "t397", { 0x4415, 0x0129, 0xD206, 0xD307, 0x0217, 0x061A, 0xE401, 0x3418, 0x0347, 0x051A, 0x365C, 0x6063, 0x600C, 0x7001, 0x000B, 0x0009, 0x0000, 0x0202, 0x0000, 0x0100 } },
        };
        const std::vector<std::vector<uint32_t>> inputs = { { 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 5, 3, 1, 2 }, { 0xFFFFFFFFu, 1, 7, 9 },
                                                            { 0x80000000u, 0x7FFFFFFFu, 3, 3 }, { 7, 7, 7, 7 }, { 2, 9, 0xFFFFFFFEu, 5 } };
        int agree = 0;
        for (const Family& fam : families)
        {
            const R8Outcome o = R8Check(d, fam.name, fam.code, inputs, "", {}, ++serial);
            if (o.verdict != "agrees") std::printf("  A2-47 family %s: %s %s\n", fam.name, o.verdict.c_str(), o.detail.c_str());
            CHECK(o.verdict == "agrees");
            agree += o.verdict == "agrees" ? 1 : 0;
        }
        std::printf("A2 round 8, the A2-47 family in ordinary code: %d of %zu fixtures agree with the run\n", agree, families.size());
    }
    // ---- (4) mac.l and mac.w by value (round 7 left them to a hand reading) ----
    {
        // mova @(7,pc),r0 ; mov r0,r4 ; mova @(7,pc),r0 ; mov r0,r5 ; lds r6,mach ; lds r7,macl ; mov #S,r0 ; ldc r0,sr ; mac.x @r4+,@r5+ ;
        // sts mac?,r0 ; rts ; nop ; padding ; the two operands at 0x20 and 0x24. The accumulator is MACH:MACL = r6:r7. S is SR bit 1: the saturation.
        // The evaluator reads the operands as the globals the engine names by their addresses (Entry::readCode); the saturation is the printer's
        // `if (A && (v = K, B)) { v = X; }` (R8FlattenCommaCondition).
        const uint32_t kL[] = { 0, 1, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 0x00010000u, 0x12345678u, 0xFFFF8000u };
        const uint32_t kW[] = { 0, 1, 0xFFFF, 0x7FFF, 0x8000, 0x00FF, 0x1234, 0x4000 };
        const std::pair<uint32_t, uint32_t> acc[] = { { 0, 0 }, { 0, 1 }, { 0, 0xFFFFFFFFu }, { 0xFFFFFFFFu, 0xFFFFFFFFu }, { 0, 0x7FFFFFFFu }, { 0, 0x80000000u },
                                                      { 0x7FFF, 0xFFFFFFFFu }, { 0xFFFF8000u, 0 }, { 0x7FFF, 0x7FFFFFFFu }, { 0xFFFF8000u, 0x80000000u },
                                                      { 0x80000000u, 0 }, { 0x7FFFFFFFu, 0xFFFFFFFFu }, { 1, 0 }, { 0xFFFFFFFFu, 0 }, { 0x8000, 0 },
                                                      { 0xFFFF7FFFu, 0xFFFFFFFFu }, { 0x7FFF, 0xFFFF8000u } };
        std::vector<std::vector<uint32_t>> inputs;
        for (const auto& a : acc) inputs.push_back({ 0, 0, a.first, a.second });
        int fixtures = 0, agree = 0;
        for (int wide = 0; wide < 2; ++wide)
            for (int sat = 0; sat < 2; ++sat)
                for (int high = 0; high < 2; ++high)
                    for (uint32_t a : (wide ? kL : kW))
                        for (uint32_t b : (wide ? kL : kW))
                        {
                            std::vector<uint16_t> w = { 0xC707, 0x6403, 0xC707, 0x6503, 0x460A, 0x471A, static_cast<uint16_t>(sat ? 0xE002 : 0xE000), 0x400E,
                                                        static_cast<uint16_t>(wide ? 0x054F : 0x455F), static_cast<uint16_t>(high ? 0x000A : 0x001A), 0x000B, 0x0009 };
                            w.resize(16, 0x0009);
                            if (wide) w.insert(w.end(), { uint16_t(a >> 16), uint16_t(a), uint16_t(b >> 16), uint16_t(b) });
                            else w.insert(w.end(), { uint16_t(a), 0, uint16_t(b), 0 });
                            char name[96];
                            std::snprintf(name, sizeof name, "mac.%c S=%d %s a=%x b=%x", wide ? 'l' : 'w', sat, high ? "mach" : "macl", a, b);
                            const R8Outcome o = R8Check(d, name, w, inputs, "c", {}, ++serial);
                            ++fixtures;
                            if (o.verdict != "agrees" && o.verdict != "agrees-global") std::printf("  %s: %s %s\n", name, o.verdict.c_str(), o.detail.c_str());
                            CHECK(o.verdict == "agrees" || o.verdict == "agrees-global");
                            agree += o.verdict == "agrees" || o.verdict == "agrees-global" ? 1 : 0;
                        }
        std::printf("A2 round 8, mac.l and mac.w by value: %d of %d fixtures agree with the run (%zu accumulators each)\n", agree, fixtures, inputs.size());
    }
    // ---- (5) A2-52 (P1): the A2-47 family that patch 0046 left ----
    {
        // A sum of terms over shared variables, `c + 8 * x + 4 * y + 14 * x` (constants from the literal pool, `mul.l`, `sts macl`), read through a mask
        // (`shlr2 ; shll2`). RuleCollectTerms combines the two x terms: it zeroes the coefficient of one (the op that held `8 * x` is now `c + 0`) and puts
        // `22 * x` in the other. Patch 0046 reset the non-zero mask of the output of each op whose input it replaces, and that was those two; the partial
        // sum between them (`+ 4 * y`) read the first as a multiple of 8, was a multiple of 4, and is not now (22 * x is 2 mod 4), but kept the mask
        // 0xfffffffc and so was the op RuleAndMask read when it removed the mask as redundant. The text dropped it: `x * 0x16 + 0x100 + y * 4`
        // for r4 = 1, r6 = 0 is 0x116, the code returns 0x114. Patch 0049 forgets the mask of everything downstream of a changed input that had
        // one narrower than its size, and the mask is in the text again: every fixture must AGREE with the run now (the pins held the wrong text, and
        // every one differed, before the fix).
        const std::vector<Family> wrong = {
            { "A2-52 c + 8x + 4y + 14x, shlr2 ; shll2", { 0xD008, 0xD309, 0x0347, 0x031A, 0x303C, 0xD308, 0x0367, 0x031A, 0x303C, 0xD307, 0x0347, 0x031A, 0x303C, 0x4009, 0x4008, 0x000B, 0x0009, 0x0009, 0x0000, 0x0100, 0x0000, 0x0008, 0x0000, 0x0004, 0x0000, 0x000E } },
            { "termgen 0", { 0xD00A, 0xD30B, 0x0367, 0x031A, 0x303C, 0x6353, 0x4308, 0x4308, 0x303C, 0xD308, 0x0347, 0x031A, 0x303C, 0xD307, 0x0367, 0x031A, 0x303C, 0x4009, 0x4008, 0x000B, 0x0009, 0x0009, 0x0000, 0x0100, 0x0000, 0x0010, 0x0000, 0x0008, 0x0000, 0x0012 } },
            { "termgen 1", { 0xD008, 0xD309, 0x0347, 0x031A, 0x303C, 0xD308, 0x0367, 0x031A, 0x303C, 0xD307, 0x0347, 0x031A, 0x303C, 0x4009, 0x4008, 0x000B, 0x0009, 0x0009, 0x0000, 0x0100, 0x0000, 0x0008, 0x0600, 0x9008, 0x0600, 0x900E } },
            { "termgen 2", { 0x2468, 0x0129, 0xD00A, 0x6343, 0x4308, 0x4308, 0x303C, 0x6313, 0x4308, 0x4308, 0x303C, 0xD307, 0x0367, 0x031A, 0x303C, 0xD306, 0x0347, 0x031A, 0x303C, 0xD305, 0x2039, 0x000B, 0x0009, 0x0009, 0x0000, 0x0000, 0x0000, 0x0008, 0xFFFF, 0xFFFA, 0xFFFF, 0xFFFC } },
            { "termgen 3", { 0x2678, 0x0129, 0x3673, 0x0229, 0xD00A, 0xD30B, 0x0317, 0x031A, 0x303C, 0xD30A, 0x0357, 0x031A, 0x3038, 0xD309, 0x0367, 0x031A, 0x3038, 0xD308, 0x0357, 0x031A, 0x303C, 0xC903, 0x4000, 0x000B, 0x0009, 0x0009, 0x0000, 0x0000, 0x0000, 0x0004, 0x0000, 0x00B8, 0xFFFF, 0xFFFC, 0x0600, 0x900A } },
            { "termgen 4", { 0x3456, 0x0129, 0xD00A, 0xD30B, 0x0317, 0x031A, 0x3038, 0xD30A, 0x0367, 0x031A, 0x303C, 0xD309, 0x0347, 0x031A, 0x3038, 0xD308, 0x0317, 0x031A, 0x303C, 0xD307, 0x2039, 0x000B, 0x0009, 0x0009, 0x0000, 0x0000, 0x0000, 0x0048, 0x0000, 0x001C, 0x0000, 0x0024, 0x0000, 0x0001, 0xFFFF, 0xFFFC } },
            { "termgen 5", { 0xD00A, 0xD30B, 0x0357, 0x031A, 0x303C, 0xD30A, 0x0357, 0x031A, 0x303C, 0xD309, 0x0347, 0x031A, 0x303C, 0xD308, 0x0357, 0x031A, 0x3038, 0xD307, 0x2039, 0x000B, 0x0009, 0x0009, 0x0000, 0x0010, 0x0000, 0x0014, 0xFFFF, 0xFFFA, 0x0000, 0x0024, 0x0000, 0x0001, 0xFFFF, 0xFFFE } },
            { "termgen 6", { 0xD008, 0xD309, 0x0357, 0x031A, 0x303C, 0xD308, 0x0367, 0x031A, 0x303C, 0xD307, 0x0357, 0x031A, 0x303C, 0x4009, 0x4008, 0x000B, 0x0009, 0x0009, 0x0000, 0x0004, 0x0000, 0x0008, 0x0000, 0x0010, 0x0000, 0x0001 } }
        };
        const std::vector<std::vector<uint32_t>> inputs = { { 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 1, 0, 1, 0 }, { 5, 3, 1, 2 },
                                                            { 1, 0xFFFFFFFEu, 0x100, 0x7F7F7F7Fu }, { 7, 7, 7, 7 }, { 2, 9, 0xFFFFFFFEu, 5 },
                                                            { 0x80000001u, 3, 5, 9 }, { 5, 2, 3, 1 } };
        int differ = 0;
        for (const Family& fam : wrong)
        {
            DecompileResult r;
            const R8Outcome o = R8Check(d, fam.name, fam.code, inputs, "", {}, ++serial, 0x06009000u, &r);
            if (o.verdict != "agrees") std::printf("  A2-52 pin %s: %s %s\n", fam.name, o.verdict.c_str(), o.detail.c_str());
            Print(fam.name, r);
            CHECK(o.verdict == "agrees");
            differ += o.verdict == "agrees" ? 1 : 0;
            if (std::strncmp(fam.name, "A2-52 c + 8x", 12) == 0) CHECK(Has(r.Text(), "& 0xfffffffc"));   // the mask the sum is read through is in the text
        }
        std::printf("A2 round 8, A2-52 (the A2-47 family beyond patch 0046): %d of %zu fixtures agree with the run\n", differ, wrong.size());
    }
    // ---- (6) the spec's decompiler-only lines ----
    // Mutants of superh.sinc that survived the decode oracle (its emulator does not run the op, or compares registers, not text), Sh2SemanticsTests
    // and the whole of Sh2DecompilerTests before these pins: deleting `Write_VBR(rm)` from `ldc Rm,VBR`, `Write_VBR(value)` from `ldc.l @Rm+,VBR`,
    // `Write_SR(value)` from `ldc.l @Rm+,SR`, and `r15 = r15 + 8` after trapa's call (deleted, or `- 8`). Each changes the printed text: the write
    // is no longer shown, or the word pushed before a trap is read back from the wrong stack slot after it.
    {
        auto text = [&](const char* name, std::vector<uint16_t> code) {
            auto snap = CallSnapshot({ name, 0x06009000u, std::move(code) }, ++serial);
            const DecompileResult r = d.Decompile(snap, Request(0x06009000u));
            Print(name, r);
            CHECK(r.status == DecompileStatus::Ok);
            return r.Text();
        };
        CHECK(Has(text("ldc r4,vbr", { 0x442E, 0x000B, 0x0009 }), "Write_VBR(param_1);"));
        CHECK(Has(text("ldc.l @r4+,vbr", { 0x4427, 0x000B, 0x0009 }), "Write_VBR(*param_1);"));
        CHECK(Has(text("ldc.l @r4+,sr", { 0x4407, 0x000B, 0x0009 }), "Write_SR(*param_1 & 0x3f3);"));
        // mov.l r4,@-r15 ; trapa #5 ; mov.l @r15+,r0 ; rts ; nop: the trap pushes SR and PC and its handler's rte pops them, so r15 is where it was and the
        // word read back is r4. The run (the handler at kRunHandler is rte ; nop) returns r4; the text must say so.
        const R8Outcome o = R8Check(d, "push r4 ; trapa #5 ; pop r0", { 0x2F46, 0xC305, 0x60F6, 0x000B, 0x0009 }, { { 5 }, { 0x1234 }, { 0xFFFFFFFFu } }, "", {}, ++serial);
        if (o.verdict != "agrees") std::printf("  trapa: %s %s\n", o.verdict.c_str(), o.detail.c_str());
        CHECK(o.verdict == "agrees");
    }
    // ---- (7) A2-56 (P1): a consume mask that the common-subexpression merge left behind ----
    {
        // addc r6,r4 ; movt r5 ; dmuls.l r0,r4 ; stc sr,r2 ; cmp/str r2,r6 ; bf over the next two ; stc sr,r5 ; sts macl,r4 ; rts ; mov r5,r0 (generator seed 965717).
        // The low byte of SR is read twice, once through `& 1` (the T bit of the carry chain) and once whole (cmp/str compares bytes): the two SUBPIECEs
        // are equal and RuleSelectCse merged them with Funcdata::totalReplace, which re-pointed the readers and did not merge the consume masks, so the
        // survivor kept the 0x01 of the reader that was there first. The next ActionVarnodeProps (the first action of the main loop, before ActionDeadCode
        // recomputes the masks) replaces any varnode whose non-zero mask and consume mask do not meet by a zero: the byte was printed as 0, and the
        // text compared `(bool)(char)param_3` where the code compares the whole byte `(uint1)(in_sr_low | carry) == (uint1)param_3`. The run returns 1 where
        // the text returned 0x203 for SR = 0x202. Patch 0051 makes totalReplace OR the replaced varnode's consume mask into the survivor's.
        auto snap = CallSnapshot({ "consume", 0x06009000u, { 0x346F, 0x0529, 0x340F, 0x0202, 0x262C, 0x8B01, 0x0502, 0x041A, 0x000B, 0x6053 } }, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(0x06009000u));
        Print("A2-56: cmp/str on the SR byte after addc", r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!Has(r.Text(), "== (bool)(char)param_3"));   // the defect was a compare of the bool with the byte
        CHECK(Has(r.Text(), "(uint1)param_3"));            // the byte is compared whole
        // The value is the printed-C differential's (the fixture "A2-56 ..." of `kPrintedCFixtures`): it runs SR through the inputs, which this
        // suite's call oracle does not (SR is 0 on entry).
    }
    // ---- (3) A2-53 ----
    {
        // mov r15,r0 ; and #0xFF,r0: r15 on entry is whatever the caller had (the run's is 0x0600F000, 256-aligned, so the run cannot tell). The engine
        // took the stack pointer on entry to be aligned to 256 (Funcdata::calcNZMask: "Treat spacebase input as aligned") and printed `return 0;`;
        // the code returns the low byte of r15. Patch 0053 takes it to be aligned to a longword, which the SH-2 requires of a stack that is pushed with
        // `mov.l Rm,@-r15` (an address error otherwise) and both compilers' ABIs promise: the text keeps the dependence on r15.
        auto snap = CallSnapshot({ "sp", 0x06009000u, { 0x60F3, 0xC9FF, 0x000B, 0x0009 } }, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(0x06009000u));
        Print("A2-53: mov r15,r0 ; and #0xff,r0", r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!Has(r.Text(), "return 0;"));
        CHECK(Has(r.Text(), "& 0xff;"));   // the low byte of the stack pointer, not a constant
        // The same assumption through the idiom that rounds a pointer up to 8: `mov r15,r0 ; add #7,r0 ; mov #-8,r1 ; and r1,r0`. A 4-aligned stack
        // pointer that is 4 mod 8 gives r15 + 4; the AND is not redundant for it, and the text keeps the rounding.
        auto snap2 = CallSnapshot({ "sp", 0x06009000u, { 0x60F3, 0x7007, 0xE1F8, 0x2019, 0x000B, 0x0009 } }, ++serial);
        const DecompileResult r2 = d.Decompile(snap2, Request(0x06009000u));
        Print("A2-53: mov r15,r0 ; add #7,r0 ; and #-8", r2);
        CHECK(!Has(r2.Text(), "return &xStack00000000;"));
        CHECK(Has(r2.Text(), "& 0xfffffff8"));
        // ...and `(r15 - 5) & 0x1c`: bits 2 to 4 of the stack pointer, printed as the constant 0x18
        auto snap3 = CallSnapshot({ "sp", 0x06009000u, { 0x7FFB, 0x60F3, 0xC91C, 0x7F05, 0x000B, 0x0009 } }, ++serial);
        const DecompileResult r3 = d.Decompile(snap3, Request(0x06009000u));
        Print("A2-53: add #-5,r15 ; mov r15,r0 ; and #0x1c,r0", r3);
        CHECK(!Has(r3.Text(), "return 0x18;"));
        CHECK(Has(r3.Text(), "& 0x1c;"));
        // What a longword-aligned stack pointer does decide stays decided: the two low bits are clear (`and #3`), and a frame rounded to 4 is no-op.
        auto snap4 = CallSnapshot({ "sp", 0x06009000u, { 0x60F3, 0xC903, 0x000B, 0x0009 } }, ++serial);
        const DecompileResult r4 = d.Decompile(snap4, Request(0x06009000u));
        Print("A2-53 control: mov r15,r0 ; and #3,r0", r4);
        CHECK(Has(r4.Text(), "return 0;"));
    }
}

// ---- Area 2, round 9 ---------------------------------------------------------------------------------------------------------------------------
// (1) A2-58 (P1): RulePushMulti moves a reader onto an existing MULTIEQUAL whose consume mask was worked out without that reader. Each predecessor of a
// join makes `r1 = src & 0x80000000` and a second value from it (`r2 = r1 + 1`, or the test `tst r1,r1`); the join reads the phi of r1 through a few low
// bits and the phi of the second value whole. The engine merges the two second-value computations into one, in the join, reading the phi of r1 (the
// op `Funcdata::opSetInput` re-points at an existing Varnode: patch 0051 covers `totalReplace` and not this). That phi keeps the consume mask of its
// first readers (0xff, 0x3fffffff), and its non-zero mask is the sign bit alone, so the next ActionVarnodeProps (first in the main loop, before
// ActionDeadCode works the masks out again) replaces it by a zero: `return 1;` for r1 + 1, `return 0xffffffff;` for -(r1 == 0). The two texts below are
// WRONG; the pins hold them so that a fix flips them (flip the `differs` and the two text checks with it). The values are the printed-C differential's
// (`SE_GENERATED_PRINTEDC_WORDS`), which the A2-58 row replays; the controls read the join whole and agree already.
void TestArea2Round9()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 50000;
    {
        // mov #1,r3 ; rotr r3 (r3 = 0x80000000) ; cmp/pl r4 ; bt B ; A: mov r5,r1 ; and r3,r1 ; mov r1,r2 ; add #1,r2 ; bra J ; nop ;
        // B: mov r6,r1 ; and r3,r1 ; mov r1,r2 ; add #1,r2 ; J: mov r1,r0 ; extu.b r0,r0 ; or r2,r0 ; rts ; nop. The code returns r1 + 1 (r1 is 0 or 0x80000000).
        const std::vector<uint16_t> words = { 0xE301, 0x4305, 0x4415, 0x8905, 0x6153, 0x2139, 0x6213, 0x7201, 0xA004, 0x0009, 0x6163, 0x2139, 0x6213, 0x7201,
                                              0x6013, 0x600C, 0x202B, 0x000B, 0x0009, 0x0009 };
        DecompileResult r;
        const R8Outcome o = R8Check(d, "A2-58 add", words, { { 0, 0x80000001u, 0xFFu }, { 0, 0x7FFFFFFFu, 0x80000000u }, { 5, 0x0u, 0x80000000u } }, "", {}, ++serial, 0x06009000u, &r);
        Print("A2-58: r1 = src & 0x80000000 in both arms, r2 = r1 + 1, join reads r1 low byte and r2", r);
        CHECK(r.status == DecompileStatus::Ok);
        // Fixed by patch 0054: the phi of r1 that RulePushMulti points the merged add at is consumed in every bit the add takes, and so
        // are the two `src & 0x80000000` above it, which RuleAndMask then keeps. Was `return 1;` with no parameter (the verdict `differs`).
        CHECK(o.verdict == "agrees");
        CHECK(!Has(r.Text(), "return 1;"));
        CHECK(Has(r.Text(), "return (param_3 & 0x80000000) + 1;"));
    }
    {
        // the same with the test: tst r1,r1 in each arm, the join returns -T (shll2 r0 ; movt r2 ; sub r2,r0): the code returns 0 when the chosen source has
        // its sign bit set, and -1 when it has not; the text returns -1 always
        const std::vector<uint16_t> words = { 0xE301, 0x4305, 0x4411, 0x8904, 0x6153, 0x2139, 0x2118, 0xA003, 0x0009, 0x6163, 0x2139, 0x2118, 0x6013, 0x4008,
                                              0x0229, 0x3028, 0x000B, 0x0009 };
        DecompileResult r;
        const R8Outcome o = R8Check(d, "A2-58 tst", words, { { 2, 0x7FFFu, 0xFFFF8000u }, { 0, 0x7FFFu, 0xFFFF8000u }, { 2, 0x80000000u, 0u } }, "", {}, ++serial, 0x06009000u, &r);
        Print("A2-58: tst r1,r1 in both arms, the join returns -T of the test", r);
        CHECK(r.status == DecompileStatus::Ok);
        // Fixed by patch 0054 (was `return 0xffffffff;`, the verdict `differs`: r4 = 2 reads r6 = 0xFFFF8000, whose sign bit is set, and the code returns 0)
        CHECK(o.verdict == "agrees");
        CHECK(!Has(r.Text(), "return 0xffffffff;"));
        CHECK(Has(r.Text(), "(param_3 & 0x80000000) == 0"));
    }
    {
        // control: the join reads r1 whole (no extu.b): the phi is consumed in every bit and nothing is lost
        const std::vector<uint16_t> words = { 0xE301, 0x4305, 0x4415, 0x8905, 0x6153, 0x2139, 0x6213, 0x7201, 0xA004, 0x0009, 0x6163, 0x2139, 0x6213, 0x7201,
                                              0x6013, 0x202B, 0x000B, 0x0009 };
        DecompileResult r;
        const R8Outcome o = R8Check(d, "A2-58 control", words, { { 0, 0x80000001u, 0xFFu }, { 0, 0x7FFFFFFFu, 0x80000000u }, { 5, 0x0u, 0x80000000u } }, "", {}, ++serial, 0x06009000u, &r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(o.verdict == "agrees");
    }
    // ---- (2) A2-59 (P2): a counted loop that ANDs a constant-seeded accumulator with a value assembled from the pieces of a register ----
    // mov #4,r1 ; L: swap.b r2,r3 ; and r3,r1 ; dt r6 ; bf L ; rts ; mov r1,r0. Patch 0049 forgot the non-zero mask of the phi of r1 after the first
    // RuleAndDistribute (`r1 & (A | B)` into `(A & r1) | (B & r1)`), so the second distribution of the nest (the mask `0x4` it keys on was gone) did not
    // fire in the pass and RuleHumptyOr merged the first one back; ActionNonzeroMask worked the mask out again at the next pass and the two rules undid
    // each other for ever (about 44 MB of operations a second), until the cancel flag. Patch 0054 forgets nothing that is still true (the
    // distribution keeps the value, and the phi its `0x4`), and patch 0055 bounds the passes of a repeating action. The request runs against a cancel
    // flag raised after 30 s, so that a regression fails here instead of hanging the suite; it must finish, settled: no warning of the bound, the
    // signature not withheld for it. (The other three words of the row: swap.w, a shift and a swap, xtrct.)
    {
        struct Loop
        {
            const char* name;
            std::vector<uint16_t> words;
        };
        const Loop loops[] = { { "A2-59 swap.b", { 0xE104, 0x6328, 0x2139, 0x4610, 0x8BFB, 0x000B, 0x6013, 0x0009 } },
                               { "A2-59 swap.w", { 0xE104, 0x6329, 0x2139, 0x4610, 0x8BFB, 0x000B, 0x6013, 0x0009 } },
                               { "A2-59 shlr8 ; swap.b", { 0xE11C, 0xE601, 0x6323, 0x4319, 0x6338, 0x2139, 0x4610, 0x8BF9, 0x000B, 0x6013, 0x000B, 0x6033 } },
                               { "A2-59 xtrct", { 0xE601, 0xE17F, 0x222B, 0x200D, 0x2109, 0x4610, 0x8BFA, 0x4308, 0x6023, 0x000B, 0x0009, 0x0009 } } };
        for (const Loop& loop : loops)
        {
            auto snap = CallSnapshot({ loop.name, 0x06009000u, loop.words }, ++serial);
            std::atomic<bool> cancel(false), done(false);
            std::thread stopper([&cancel, &done] {
                for (int i = 0; i < 3000 && !done; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
                cancel = true;
            });
            const auto start = std::chrono::steady_clock::now();
            const DecompileResult r = d.Decompile(snap, Request(0x06009000u), &cancel);
            done = true;
            stopper.join();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::printf("%s: status %d, %.1f ms\n", loop.name, static_cast<int>(r.status), ms);
            Print(loop.name, r);
            bool bounded = false;
            for (const std::string& w : r.warnings) bounded |= Has(w, "Exceeded maximum repeats");
            CHECK(r.status == DecompileStatus::Ok);
            CHECK(!bounded);
            CHECK(!Has(r.prototypeWithheld, "did not settle"));   // (withheld for reading r2 on entry, which no signature passes)
            CHECK(Has(r.Text(), "while ("));
        }
        // (By value they are printed-C fixtures of Sh2GeneratedTests, `A2-59 ...`: this evaluator does not run loops.)
    }
    // ---- (3) patch 0055: a repeating action that does not settle stops, says so, and the signature is withheld ----
    {
        // The bound lowered to 2 passes for the A2-58 join (whose rule pool needs more): the text is still printed (every pass keeps the meaning of the
        // function), the engine's warning is passed on, and the signature is withheld as for the restart limit. With the bound at the engine's own
        // (300 passes; the most any function of the two game images needs is 60, of the rule pool) nothing is said.
        const std::vector<uint16_t> words = { 0xE301, 0x4305, 0x4415, 0x8905, 0x6153, 0x2139, 0x6213, 0x7201, 0xA004, 0x0009, 0x6163, 0x2139, 0x6213, 0x7201,
                                              0x6013, 0x600C, 0x202B, 0x000B, 0x0009, 0x0009 };
        Sh2Decompiler::SetActionRepeatLimitForTests(2);
        const DecompileResult r = d.Decompile(CallSnapshot({ "0055 bound", 0x06009000u, words }, ++serial), Request(0x06009000u));
        Sh2Decompiler::SetActionRepeatLimitForTests(0);
        Print("0055: the passes of one action bounded to 2", r);
        bool bounded = false;
        for (const std::string& w : r.warnings) bounded |= Has(w, "Exceeded maximum repeats of ");
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(bounded);
        CHECK(r.prototypeWithheld == "the analysis did not settle (repeat limit)");
        CHECK(Has(r.Text(), "return"));
        const DecompileResult again = d.Decompile(CallSnapshot({ "0055 bound", 0x06009000u, words }, ++serial), Request(0x06009000u));
        bounded = false;
        for (const std::string& w : again.warnings) bounded |= Has(w, "Exceeded maximum repeats");
        CHECK(again.status == DecompileStatus::Ok && !bounded && again.prototypeWithheld.empty());
        EngineWarning w = ClassifyEngineWarning("WARNING: Exceeded maximum repeats of mainloop with changes still pending");
        CHECK(w.keep && w.repeatLimit && !w.restartLimit);
        w = ClassifyEngineWarning("WARNING (jumptable): Exceeded maximum repeats of oppool1 with changes still pending");
        CHECK(!w.keep);   // the scratch copy's own analysis, as every warning of it that is not about the table
    }
}

// ===== Area 3, round 10 review: the controls of the round's rows that hold today =============================================================
// The round's findings (A3-66 to A3-75) are wrong outputs and are in the ledger with their fixtures, not here. These are the shapes beside them that
// the engine and the host get right, so a fix that moves the exceptions of the return rule (the nothing-known value, the interrupt-mask SR, the part
// of a register), the walk of a callee (A3-61) or patch 0056's stepping down does not take them with it.
void TestArea3Round10()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 100000;
    const uint32_t base = 0x06009000u, gAt = 0x06009040u;
    const std::vector<std::vector<uint32_t>> inputs = { { 0x11111111u, 0x22222222u, 0x0600B000u, 0x44444444u },
                                                        { 0x00000000u, 0x00000001u, 0x0600B000u, 0x80000000u } };
    auto withheld = [&](const char* name, std::vector<uint16_t> words, const char* reason) {
        const CallFixture f = { name, base, std::move(words) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!r.prototype.known && Has(r.prototypeWithheld, reason));
    };
    // The callee at 0x06009040 is decompiled first and lent to the caller.
    auto together = [](std::vector<uint16_t> caller, const std::vector<uint16_t>& callee) {
        caller.resize(32, 0x0009);
        caller.insert(caller.end(), callee.begin(), callee.end());
        return caller;
    };
    auto lentCallee = [&](const char* name, std::vector<uint16_t> words, bool* callerKnown, std::string* text, bool* agrees) {
        const CallFixture f = { name, base, std::move(words) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult callee = d.Decompile(snap, Request(gAt));
        CHECK(callee.status == DecompileStatus::Ok && callee.prototype.known);
        DecompileRequest q = Request(f.base);
        q.prototypes.emplace_back(gAt, callee.prototype);
        const DecompileResult r = d.Decompile(snap, q);
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        if (callerKnown) *callerKnown = r.prototype.known;
        if (text) *text = r.Text();
        if (agrees) *agrees = PseudoAgreesWithRun(r, snap, f, inputs);
        return r;
    };
    // A text for a function with a loop (the evaluator has none): the local the walk wrote is returned, not the value stored before the walk, and the
    // stack register does not appear.
    auto walkText = [&](const char* name, std::vector<uint16_t> words, const char* want) {
        const CallFixture f = { name, base, std::move(words) };
        const DecompileResult r = d.Decompile(CallSnapshot(f, ++serial), Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(Has(r.Text(), want) && !Has(r.Text(), "return 5;") && !Has(r.Text(), "register0x0000003c"));
    };

    // ---- A3-66, A3-67: a stack word at a frame offset the check can place, and a hidden register next to a constant or a loaded word ------------
    withheld("a3_66 control: the stack word at +20 by add #20", { 0x62F3, 0x7214, 0x6022, 0x210B, 0x000B, 0x0009 },
             "its return is computed from a stack argument");
    withheld("a3_66 control: r3 or a constant, chosen by r4 (held)", { 0xE201, 0x2422, 0x2448, 0x8902, 0x6033, 0xA001, 0x0009, 0xE009, 0x000B, 0x0009 },
             "its return is computed from r3 on entry");
    withheld("a3_66 control: r3 or a word loaded through the held r4", { 0xE201, 0x2422, 0x2448, 0x8902, 0x6033, 0xA001, 0x0009, 0x6042, 0x000B, 0x0009 },
             "its return is computed from r3 on entry");
    withheld("a3_67 control: cmp/eq r3 with a constant chooses", { 0xE201, 0x2422, 0xE207, 0x3230, 0x8903, 0xE001, 0x210B, 0xA002, 0x0009, 0xE002, 0x210B, 0x000B, 0x0009, 0x0009 },
             "its return is chosen by r3 on entry");

    // ---- A3-68, A3-69: a part of r3 is still r3; the T flag as movt; a value held in r0 across a callee that keeps it ----------------------------
    withheld("a3_68 control: extu.b r3,r0 ; or r0,r1", { 0x603C, 0x210B, 0x000B, 0x0009 }, "its return is computed from r3 on entry");
    withheld("a3_69 control: movt r0 ; or r0,r1", { 0x0029, 0x210B, 0x000B, 0x0009 }, "its return is computed from the T flag on entry");
    {
        bool known = true;
        lentCallee("a3_68 control: r0 = 5 held across a callee that keeps r0",
                   together({ 0x4F22, 0xE005, 0xD102, 0x410B, 0x0009, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040 }, { 0xE207, 0x000B, 0x0009, 0x0009 }), &known,
                   nullptr, nullptr);
        CHECK(!known);
    }

    // ---- A3-71: a lent callee that walks UP from the address it is handed, and one that only reads below it ------------------------------------
    {
        bool known = false, agrees = false;
        std::string text;
        lentCallee("a3_71 control: the callee walks up from the pointer",
                   together({ 0x4F22, 0x7FEC, 0xE005, 0x2F02, 0x64F3, 0xD104, 0x410B, 0x0009, 0x60F2, 0x7F14, 0x4F26, 0x000B, 0x0009, 0x0009, 0x0600, 0x9040 },
                            { 0xE104, 0x2412, 0x7404, 0x4110, 0x8BFB, 0x000B, 0x0009, 0x0009 }),
                   &known, &text, &agrees);
        CHECK(known && agrees && !Has(text, "return 5;"));
        lentCallee("a3_71 control: the callee reads the word below its pointer",
                   together({ 0x4F22, 0x7FEC, 0xE005, 0x2F02, 0x64F3, 0x7404, 0xD103, 0x410B, 0x0009, 0x7F14, 0x4F26, 0x000B, 0x0009, 0x0009, 0x0600, 0x9040 },
                            { 0x74FC, 0x6042, 0x000B, 0x0009 }),
                   &known, &text, &agrees);
        CHECK(agrees);
    }

    // ---- A3-72: steps that A3-65's patch covers: an if/else of steps, a conditional store, the pointer in a local; and a walk from the caller's frame --
    walkText("a3_72 control: step 4 or 8 on the arms of an if", { 0x7FE0, 0x62F3, 0x7220, 0xE005, 0x2F02, 0xE103, 0x2448, 0x8902, 0x72FC, 0xA001, 0x0009, 0x72F8, 0x2212, 0x4110, 0x8BF6, 0x60F2, 0x7F20, 0x000B, 0x0009, 0x0009 },
             "return xStack_20;");
    walkText("a3_72 control: the step always, the store under a condition", { 0x7FF0, 0x62F3, 0x7210, 0xE005, 0x2F02, 0xE104, 0x72FC, 0x2448, 0x8900, 0x2212, 0x4110, 0x8BF9, 0x60F2, 0x7F10, 0x000B, 0x0009 },
             "return xStack_10;");
    walkText("a3_72 control: the pointer kept in a stack local", { 0x7FE8, 0x62F3, 0x7210, 0x1F24, 0xE005, 0x2F02, 0xE104, 0x52F4, 0x72FC, 0x1F24, 0x2212, 0x4110, 0x8BF9, 0x60F2, 0x7F18, 0x000B, 0x0009, 0x0009 },
             "return xStack_18;");
    withheld("a3_72 control: a walk from r15 + 16 goes into the caller's frame", { 0x7FF8, 0xE005, 0x2F02, 0x62F3, 0x7210, 0xE104, 0x2216, 0x4110, 0x8BFC, 0x60F2, 0x7F08, 0x000B, 0x0009, 0x0009 },
             "it writes a word of its caller's frame");
}

// ===== Area 3, round 10 fix: the fail-closed return rule and the new frame claims (A3-66 to A3-77) ============================================
// The findings of the round 10 review, each flipped to the right answer. The rule: a function the engine made `void` is lent only when no input
// the signature does not hold can change what it leaves in r0; a value, op or address the interpreter cannot model contributes all of its inputs'
// dependences, never none (A3-66 to A3-69, A3-76, A3-77). A lent callee that writes below the frame address it was handed exposes the locals there
// (A3-71); the callee walk reaches thunks and onward-handing callees and is bounded (A3-70, A3-74); every unlent call a word was exposed at is kept
// (A3-75); a stack argument past the model's window is withheld (A3-73); engine patch 0057 reaches a pointer stepped down under a condition (A3-72).
void TestArea3Round10Fix()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 101000;
    const uint32_t base = 0x06009000u, gAt = 0x06009040u, hAt = 0x06009080u;
    const std::vector<std::vector<uint32_t>> inputs = { { 0x11111111u, 0x22222222u, 0x0600B000u, 0x44444444u },
                                                        { 0x00000000u, 0x00000001u, 0x0600B000u, 0x80000000u } };
    auto withheld = [&](const char* name, std::vector<uint16_t> words, const char* reason) {
        const CallFixture f = { name, base, std::move(words) };
        const DecompileResult r = d.Decompile(CallSnapshot(f, ++serial), Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(!r.prototype.known && Has(r.prototypeWithheld, reason));
    };
    auto lent = [&](const char* name, std::vector<uint16_t> words) {
        const CallFixture f = { name, base, std::move(words) };
        const DecompileResult r = d.Decompile(CallSnapshot(f, ++serial), Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(r.prototype.known && r.prototypeWithheld.empty());
    };
    // caller + one callee at gAt (the callee decompiled and, if 'lendCallee', lent). The caller's last two words are the literal that holds gAt
    // (`mov.l @(disp,PC),r1 ; jsr @r1`); 'together' keeps them in place and the callee lands at word 32 = gAt.
    auto withCallee = [&](const char* name, std::vector<uint16_t> caller, const std::vector<uint16_t>& callee, bool lendCallee,
                          bool* callerKnown, std::string* text, bool* agrees) -> DecompileResult {
        caller.resize(32, 0x0009);
        caller.insert(caller.end(), callee.begin(), callee.end());
        const CallFixture f = { name, base, std::move(caller) };
        auto snap = CallSnapshot(f, ++serial);
        DecompileRequest q = Request(f.base);
        if (lendCallee)
        {
            const DecompileResult c = d.Decompile(snap, Request(gAt));
            CHECK(c.status == DecompileStatus::Ok && c.prototype.known);
            q.prototypes.emplace_back(gAt, c.prototype);
        }
        const DecompileResult r = d.Decompile(snap, q);
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        if (callerKnown) *callerKnown = r.prototype.known;
        if (text) *text = r.Text();
        if (agrees) *agrees = PseudoAgreesWithRun(r, snap, f, inputs);
        return r;
    };
    // caller + h at gAt + k at hAt (neither lent: the caller is withheld, the pin is the text).
    auto withTwoCallees = [&](const char* name, std::vector<uint16_t> caller, const std::vector<uint16_t>& h, const std::vector<uint16_t>& k,
                              std::string* text, bool lendK = false) {
        caller.resize(32, 0x0009);
        std::vector<uint16_t> hh = h;
        hh.resize(32, 0x0009);
        caller.insert(caller.end(), hh.begin(), hh.end());
        caller.insert(caller.end(), k.begin(), k.end());
        const CallFixture f = { name, base, std::move(caller) };
        auto snap = CallSnapshot(f, ++serial);
        DecompileRequest q = Request(f.base);
        if (lendK)
        {
            const DecompileResult c = d.Decompile(snap, Request(hAt));
            CHECK(c.status == DecompileStatus::Ok && c.prototype.known);
            q.prototypes.emplace_back(hAt, c.prototype);
        }
        const DecompileResult r = d.Decompile(snap, q);
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok && !r.prototype.known);
        if (text) *text = r.Text();
    };
    auto walkText = [&](const char* name, std::vector<uint16_t> words, const char* want) {
        const CallFixture f = { name, base, std::move(words) };
        const DecompileResult r = d.Decompile(CallSnapshot(f, ++serial), Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(Has(r.Text(), want) && !Has(r.Text(), "return 5;") && !Has(r.Text(), "register0x0000003c"));
    };

    // ---- A3-66: a hidden register joined/summed with a nothing-known value is kept; a pool-offset stack access folds ----------------------------
    withheld("a3_66a: join of r3 with the word below r15, chosen by r4", { 0xE201, 0x2422, 0x2448, 0x8902, 0x6033, 0xA003, 0x0009, 0x61F3, 0x71FC, 0x6012, 0x000B, 0x0009 },
             "its return is computed from r3 on entry");
    withheld("a3_66b: the word below r15 summed with r3", { 0xE201, 0x2422, 0x61F3, 0x71FC, 0x6012, 0x303C, 0x210B, 0x000B, 0x0009 },
             "its return is computed from r3 on entry");
    withheld("a3_66c: a stack argument read through a pool-offset address (the mov.w literal folded)", { 0x9204, 0x32FC, 0x6022, 0x210B, 0x000B, 0x0009, 0x0014, 0x0009 },
             "its return is computed from a stack argument");
    // ---- A3-67: the chooser side -------------------------------------------------------------------------------------------------------------
    withheld("a3_67a: r3 compared with the word below r15 chooses", { 0xE201, 0x2422, 0x61F3, 0x71FC, 0x6212, 0x3230, 0x8903, 0xE001, 0x210B, 0xA002, 0x0009, 0xE002, 0x210B, 0x000B, 0x0009, 0x0009 },
             "its return is chosen by r3 on entry");
    withheld("a3_67b: a pool-offset stack argument chooses (the mov.w literal folded)", { 0x910C, 0x31FC, 0x6212, 0x2228, 0x8903, 0xE001, 0x210B, 0xA002, 0x0009, 0xE002, 0x210B, 0x000B, 0x0009, 0x0009, 0x0014, 0x0009 },
             "its return is chosen by a stack argument");
    // ---- A3-68: a part of r0, a byte spill, a carried value through extu.b -----------------------------------------------------------------------
    withheld("a3_68a: extu.b r0,r0", { 0x600C, 0x210B, 0x000B, 0x0009 }, "its return is computed from r0 on entry");
    withheld("a3_68b: extu.w r0,r0", { 0x600D, 0x210B, 0x000B, 0x0009 }, "its return is computed from r0 on entry");
    withheld("a3_68c: exts.b r0,r2 ; mov r2,r0", { 0x620E, 0x6023, 0x210B, 0x000B, 0x0009, 0x0009 }, "its return is computed from r0 on entry");
    withheld("a3_68e: byte spill and reload of r0", { 0x2F04, 0x60F4, 0x210B, 0x000B, 0x0009, 0x0009 }, "its return is computed from r0 on entry");
    {
        const DecompileResult r = withCallee("a3_68f: r0 = 5 held across a keeps-r0 callee, then extu.b",
                   { 0x4F22, 0xE005, 0xD103, 0x410B, 0x0009, 0x600C, 0x210B, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040 },
                   { 0xE207, 0x000B, 0x0009, 0x0009 }, true, nullptr, nullptr, nullptr);
        CHECK(!r.prototype.known && Has(r.prototypeWithheld, "it holds a value in r0 across a call that keeps r0"));
    }
    // ---- A3-69: the SR exception no longer clears the entry T; A3-77: xor #0,r0 is the identity op the table means -------------------------------
    withheld("a3_69a: stc sr,r0 ; and #1,r0 returns the entry T", { 0x0002, 0xC901, 0x210B, 0x000B, 0x0009, 0x0009 },
             "its return is computed from the T flag on entry");
    // ...and SR's exception is only for r0 holding the value `ldc` wrote back (06010054's idiom stays lent, TestArea3Round9Fix): SR summed, or the
    // mask set with no write-back, is an input.
    withheld("a3_69: stc sr,r0 ; add #1,r0", { 0x0002, 0x7001, 0x210B, 0x000B, 0x0009, 0x0009 }, "its return is computed from sr on entry");
    withheld("a3_69: the mask idiom with no write-back", { 0x0002, 0xCBF0, 0x210B, 0x000B, 0x0009, 0x0009 }, "its return is computed from sr on entry");
    lent("a3_77: xor #0,r0 is a nop", { 0xCA00, 0x210B, 0x000B, 0x0009 });
    // ---- A3-76: the sum of a stack address and an index keeps the index --------------------------------------------------------------------------
    withheld("a3_76a: r15 + r3 returned", { 0x60F3, 0x303C, 0x210B, 0x000B, 0x0009, 0x0009 }, "its return is computed from r3 on entry");
    withheld("a3_76b: r15 + r3 compared chooses", { 0x62F3, 0x323C, 0xD105, 0x3210, 0x8903, 0xE001, 0x210B, 0xA002, 0x0009, 0xE002, 0x210B, 0x000B, 0x0009, 0x0009, 0x0600, 0xF000 },
             "its return is chosen by r3 on entry");
    withheld("a3_76c: addc of a frame address into r11", { 0x7FF0, 0x64F3, 0x7408, 0x3B4E, 0x60B3, 0x7F10, 0x210B, 0x000B, 0x0009, 0x0009 },
             "its return is computed from r11 on entry");
    // ---- A3-73: a stack argument past the 500-byte model window ---------------------------------------------------------------------------------
    withheld("a3_73: a return read from r15 + 600", { 0x9104, 0x31FC, 0x6012, 0x000B, 0x0009, 0x0009, 0x0258, 0x0009 },
             "an incoming argument outside its signature");

    // ---- A3-71: a lent callee that writes below the exact frame address it was handed -----------------------------------------------------------
    {
        bool known = false, agrees = false;
        std::string text;
        withCallee("a3_71a: g fills backwards through r4 (mov.l r1,@-r4)", { 0x4F22, 0x7FEC, 0xE005, 0x2F02, 0x64F3, 0x7410, 0xD103, 0x410B, 0x0009, 0x60F2, 0x7F14, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040 },
                   { 0xE104, 0x2416, 0x4110, 0x8BFC, 0x000B, 0x0009 }, true, &known, &text, &agrees);
        CHECK(known && agrees && !Has(text, "return 5;"));
        withCallee("a3_71b: g stores one word below r4 (add #-4,r4 ; mov.l r1,@r4)", { 0x4F22, 0x7FEC, 0xE005, 0x1F02, 0x64F3, 0x740C, 0xD103, 0x410B, 0x0009, 0x50F2, 0x7F14, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040 },
                   { 0xE104, 0x74FC, 0x2412, 0x000B, 0x0009, 0x0009 }, true, &known, &text, &agrees);
        CHECK(known && agrees && !Has(text, "return 5;"));
    }

    // ---- A3-70: the callee walk reaches a thunk and an onward-handing callee ---------------------------------------------------------------------
    {
        std::string text;
        // h at gAt is `mov.l @(lit),r1 ; jmp @r1 ; nop` to k at hAt, which writes the caller's frame word.
        withTwoCallees("a3_70a: a thunk hands the write on", { 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0xD103, 0x410B, 0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040 },
                       { 0xD101, 0x412B, 0x0009, 0x0009, 0x0600, 0x9080 }, { 0xE109, 0x2F12, 0x000B, 0x0009 }, &text);
        CHECK(!Has(text, "return 5;") && Has(text, "a local at or above the stack pointer is read after this call"));
        // Control (precision of the transitive walk): the thunk's target writes nothing, so nothing is exposed and the local stays folded.
        withTwoCallees("a3_70 control: a thunk to a callee that writes nothing", { 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0xD103, 0x410B, 0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040 },
                       { 0xD101, 0x412B, 0x0009, 0x0009, 0x0600, 0x9080 }, { 0xE207, 0x000B, 0x0009, 0x0009 }, &text);
        CHECK(Has(text, "return 5;") && !Has(text, "a local at or above the stack pointer is read after this call"));
        // h hands its entry r15 + 4 in r4 to k, which stores through r4: k lent (b), and not lent (c).
        for (bool lendK : { true, false })
        {
            withTwoCallees(lendK ? "a3_70b: h hands its incoming slot to a lent k" : "a3_70c: h hands its incoming slot to an unlent k",
                           { 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0xD103, 0x410B, 0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009, 0x0600, 0x9040 },
                           { 0x4F22, 0x64F3, 0x7404, 0xD103, 0x410B, 0x0009, 0x4F26, 0x000B, 0x0009, 0x0009, 0x0600, 0x9080 }, { 0xE109, 0x2412, 0x000B, 0x0009 },
                           &text, lendK);
            CHECK(!Has(text, "return 5;") && Has(text, "a local at or above the stack pointer is read after this call"));
        }
    }

    // ---- A3-75: every unlent call a word was exposed at is kept ----------------------------------------------------------------------------------
    {
        std::string text;
        // g at gAt writes the word, h at hAt leaves it alone; the word is read back after both.
        withTwoCallees("a3_75: a word the first call wrote, the second leaves alone", { 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0xD105, 0x410B, 0x0009, 0xD105, 0x410B, 0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009, 0x0009, 0x0600, 0x9040, 0x0600, 0x9080 },
                       { 0xE109, 0x2F12, 0x000B, 0x0009 }, { 0xE201, 0x000B, 0x0009, 0x0009 }, &text);
        CHECK(!Has(text, "return 5;") && Has(text, "a local at or above the stack pointer is read after this call"));
    }

    // ---- A3-74: the walk of a callee of 6,000 pushes is bounded (the head took 14.7 s and 2.2 GB) and fails closed ---------------------------------
    {
        std::vector<uint16_t> w = { 0x4F22, 0x7FF8, 0xE005, 0x2F02, 0xD103, 0x410B, 0x0009, 0x60F2, 0x7F08, 0x4F26, 0x000B, 0x0009, 0x0602, 0x0000 };
        w.resize((0x06020000u - base) / 2, 0x0009);
        w.insert(w.end(), 6000, 0x2F16);   // mov.l r1,@-r15
        w.push_back(0x000B);
        w.push_back(0x0009);
        const CallFixture f = { "a3_74: a callee of 6,000 pushes, the caller restores PR from the stack", base, std::move(w) };
        const DecompileResult r = d.Decompile(CallSnapshot(f, ++serial), Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok && r.elapsedMs < 3000.0);
        CHECK(Has(r.Text(), "a local at or above the stack pointer is read after this call") && !Has(r.Text(), "return 5;"));
    }

    // ---- A3-72: engine patch 0057, a pointer stepped down under a condition in a loop -----------------------------------------------------------
    walkText("a3_72a: if (c) *--p = v in a counted loop", { 0x7FF0, 0x62F3, 0x7210, 0xE005, 0x2F02, 0xE104, 0x2448, 0x8900, 0x2216, 0x4110, 0x8BFA, 0x60F2, 0x7F10, 0x000B, 0x0009, 0x0009 },
             "return xStack_10;");
    walkText("a3_72b: the conditional step through the r14 frame pointer", { 0x2FE6, 0x7FF0, 0x6EF3, 0x7E10, 0xE005, 0x2F02, 0xE104, 0x2448, 0x8900, 0x2E16, 0x4110, 0x8BFA, 0x60F2, 0x7F10, 0x6EF6, 0x000B, 0x0009, 0x0009 },
             "return xStack_14;");
    walkText("a3_72c: the conditional step in an inner loop", { 0x7FE0, 0x62F3, 0x7220, 0xE005, 0x2F02, 0xE302, 0xE104, 0x2448, 0x8900, 0x2216, 0x4110, 0x8BFA, 0x4310, 0x8BF7, 0x60F2, 0x7F20, 0x000B, 0x0009 },
             "return xStack_20;");
}

// ===== Area 3, round 10 fix: the hidden-input fuzzer (the round 10 reviewer's scratch fuzzer, ported) ====================================
// Random register and frame programs of 2 to 7 items -- moves, ALU, partial reads and extensions, shifts, addc/subc/negc, movt, stc sr, mul.l and
// sts macl, a store of one width and a reload of another through a copy of r15, a branch over one instruction -- each ending `add #16,r15 ;
// or r0,r1 ; rts ; nop` so r0 has a second use. A program the host lends is run on the emulator once as it is and once with each input the
// signature does not hold perturbed: r0-r3 and r8-r11, the T flag the caller left, and the r4-r7 beyond its parameters. A lent function whose
// r0 changes with one of them drops an input (r0 left as it came in is the one excused case, A3-14). The review's 44,100 programs found A3-68,
// A3-69 and A3-76 this way; SE_A3FUZZ_PROGRAMS and SE_A3FUZZ_SEED widen or move the run.
void TestArea3HiddenInputFuzz()
{
    Sh2Decompiler d(gConfig);
    const char* countEnv = std::getenv("SE_A3FUZZ_PROGRAMS");
    const char* seedEnv = std::getenv("SE_A3FUZZ_SEED");
    const int programs = countEnv ? std::atoi(countEnv) : 400;
    std::mt19937 rng(seedEnv ? static_cast<uint32_t>(std::strtoul(seedEnv, nullptr, 10)) : 1u);
    auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<uint32_t>(n)); };
    const int pool[] = { 0, 1, 2, 3, 8, 9, 10, 11, 4 };
    auto reg = [&]() { return pool[pick(9)]; };
    auto low = [&]() { return pick(4); };
    uint64_t serial = 150000;
    int lent = 0, findings = 0, checkedRuns = 0;
    for (int p = 0; p < programs; ++p)
    {
        std::vector<uint16_t> w = { 0x7FF0 };   // add #-16,r15
        const int n = 2 + pick(6);
        for (int k = 0; k < n; ++k)
        {
            const int c = pick(41), dd = reg(), ss = reg();
            const uint16_t D = static_cast<uint16_t>(dd << 8), S = static_cast<uint16_t>(ss << 4);
            if (c < 4) w.push_back(0x6003 | D | S);
            else if (c < 8) { const uint16_t ops[] = { 0x300C, 0x3008, 0x2009, 0x200B, 0x200A }; w.push_back(ops[pick(5)] | D | S); }
            else if (c < 10) w.push_back((pick(2) ? 0x600B : 0x6007) | D | S);
            else if (c < 14) { const uint16_t ops[] = { 0x600C, 0x600D, 0x600E, 0x600F, 0x6008, 0x6009 }; w.push_back(ops[pick(6)] | D | S); }
            else if (c < 17) { const uint16_t ops[] = { 0x4000, 0x4001, 0x4008, 0x4009, 0x4018, 0x4019, 0x4028, 0x4029, 0x4004, 0x4005, 0x4021, 0x4020 }; w.push_back(ops[pick(12)] | D); }
            else if (c < 19) { const int imm[] = { 1, -1, 5, 127, -128, 0 }; w.push_back(0x7000 | D | (imm[pick(6)] & 0xFF)); }
            else if (c < 21) { const int imm[] = { 0, 1, 5, -1, 100 }; w.push_back(0xE000 | D | (imm[pick(5)] & 0xFF)); }
            else if (c < 23) { const uint16_t ops[] = { 0xC900, 0xCB00, 0xCA00 }; const int imm[] = { 1, 2, 0xF0, 0xFF, 0x0F, 0 }; w.push_back(ops[pick(3)] | imm[pick(6)]); }
            else if (c < 25) { w.push_back(0x3000 | D | S); w.push_back(0x0029 | static_cast<uint16_t>(low() << 8)); }
            else if (c < 26) { w.push_back(0x2008 | D | S); w.push_back(0x0029 | static_cast<uint16_t>(low() << 8)); }
            else if (c < 28) w.push_back(0x0002 | static_cast<uint16_t>(low() << 8));   // stc sr,rX
            else if (c < 29) w.push_back(pick(2) ? 0x0008 : 0x0018);                     // clrt, sett
            else if (c < 31) { const uint16_t ops[] = { 0x300E, 0x300A, 0x600A }; w.push_back(ops[pick(3)] | D | S); }
            else if (c < 32) { w.push_back(0x0007 | D | S); w.push_back(0x001A | static_cast<uint16_t>(low() << 8)); }
            else if (c < 36)
            {
                const int off[] = { 0, 4, 8 };
                w.push_back(0x64F3);                                                  // mov r15,r4
                w.push_back(0x7400 | static_cast<uint16_t>(off[pick(3)]));            // add #off,r4
                w.push_back(0x2400 | S | static_cast<uint16_t>(pick(3)));             // mov.x rS,@r4
                w.push_back(0x6040 | static_cast<uint16_t>(low() << 8) | static_cast<uint16_t>(pick(3)));   // mov.y @r4,rX
            }
            else if (c < 38)
            {
                const uint16_t tests[] = { static_cast<uint16_t>(0x2008 | (ss << 8) | S), static_cast<uint16_t>(0x3000 | D | S), static_cast<uint16_t>(0x4015 | (ss << 8)) };
                w.push_back(tests[pick(3)]);
                w.push_back(0x8900);                                                  // bt over the next instruction
                w.push_back(0x6003 | static_cast<uint16_t>(low() << 8) | static_cast<uint16_t>(reg() << 4));
            }
            else w.push_back(0x6003 | S);                                             // mov rS,r0
        }
        if (pick(2)) w.push_back(0x6003 | static_cast<uint16_t>(reg() << 4));
        for (uint16_t t : { 0x7F10, 0x210B, 0x000B, 0x0009 }) w.push_back(t);
        const CallFixture f = { "a3 fuzz " + std::to_string(p), 0x06009000u, w };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        if (r.status != DecompileStatus::Ok || !r.prototype.known) continue;
        ++lent;
        const size_t params = r.prototype.params.size();
        auto run = [&](const std::map<std::string, uint32_t>& over, uint32_t& r0) {
            try
            {
                RunMachine m(snap);
                for (const auto& kv : RunEntryRegisters({ 0x0600F100u, 0, 0, 0 })) m.Set(kv.first.c_str(), kv.second);
                for (const auto& kv : over) m.Set(kv.first.c_str(), kv.second);
                if (!m.Run(f.base, kRunSentinel, 100000)) return false;
                r0 = m.Get("r0");
                return true;
            }
            catch (ghidra::LowlevelError&) { return false; }
        };
        uint32_t base = 0;
        if (!run({}, base)) continue;
        std::vector<std::pair<std::string, uint32_t>> perturb;
        for (int h : { 0, 1, 2, 3, 8, 9, 10, 11 }) perturb.emplace_back("r" + std::to_string(h), 0x7000u + 0x11u * static_cast<uint32_t>(h));
        for (size_t a = params; a < 4; ++a) perturb.emplace_back("r" + std::to_string(4 + a), 0x0600F200u + 0x10u * static_cast<uint32_t>(a));
        perturb.emplace_back("sr", 1);
        for (const auto& pv : perturb)
        {
            uint32_t v = 0;
            if (!run({ { pv.first, pv.second } }, v)) continue;
            ++checkedRuns;
            if (v == base) continue;
            if (pv.first == "r0" && v == pv.second) continue;   // r0 left as it came in: the excused case
            ++findings;
            std::string words;
            for (uint16_t x : w)
            {
                char b[8];
                std::snprintf(b, sizeof b, "%04X ", x);
                words += b;
            }
            std::printf("  a3 fuzz FINDING program %d: lent (%zu parameters, %s) and r0 changes with %s: %08X -> %08X; words %s\n", p, params,
                        r.prototype.ret.kind == TypeKind::Void ? "void" : "returning", pv.first.c_str(), base, v, words.c_str());
            break;
        }
    }
    std::printf("  a3 fuzz: %d programs, %d lent, %d perturbed runs checked, %d findings\n", programs, lent, checkedRuns, findings);
    CHECK(lent > programs / 20);   // the fuzzer still reaches lent functions
    CHECK(findings == 0);
}

// ===== A3-65 (P1): a copy of r15 walked down by pre-decrement stores ===========================================================================
// `*--p = v` over a local array compiles to a copy of r15 (or of the frame pointer r14), moved to the top of the array and walked down by
// `mov.l Rm,@-Rn`. The engine's alias analysis (AliasChecker, upstream's) takes a pointer into the frame to reach only what lies at or above its base,
// as an index does, and the guard of an indexed store (LoadGuard::establishRange) covered only the few iterations its partial analysis ran or what
// lies above the base: the locals below the walk's start were taken for unaliased, their INDIRECTs at the walk's stores were collapsed, and a read
// of one after the loop printed the value it had before it (`return 5;`, the code returns 1), with the signature lent and no note. A walk from the
// entry r15 itself printed as the stack register (`register0x0000003c = (BADSPACEBASE *)((int4)register0x0000003c + -4);`). Patch 0056: such a base
// aliases what lies below it, the guard covers the stack below a pointer that is stepped down, the join of the entry r15 is a pointer to the frame
// (and no reference to a stack parameter). Each fixture by value (the pseudocode evaluated against the code run on the emulator), the walk's store
// printed, and no stack register in the text; the printed-C differential holds the same fixtures compiled (Sh2GeneratedTests.cpp, "A3-65").
void TestA365WalkDown()
{
    Sh2Decompiler d(gConfig);
    uint64_t serial = 36500;
    // 'walkStore' is the store instruction of the walk (its offset in words); 'want' the return statement, which reads the local the walk wrote last
    // (the value of every fixture is held by the printed-C differential, compiled and run: the evaluator here has no loops); 'raw' whether the text
    // may name the stack register (a pointer into the save area of a pushed register has no local to be named after: upstream prints those raw,
    // `(int4 *)((int4)register0x0000003c + -4)`, here and at the head alike, with the right value).
    auto check = [&](const char* name, std::vector<uint16_t> words, size_t walkStore, bool raw, const char* want) {
        const CallFixture f = { name, 0x06009000u, std::move(words) };
        auto snap = CallSnapshot(f, ++serial);
        const DecompileResult r = d.Decompile(snap, Request(f.base));
        Print(f.name.c_str(), r);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(Has(r.Text(), want));
        CHECK(!Has(r.Text(), "return 5;"));   // WRONG at the head: the value stored before the walk
        if (!raw) CHECK(!Has(r.Text(), "register0x") && !Has(r.Text(), "BADSPACEBASE"));
        CHECK(!std::regex_search(r.Text(), std::regex("\\[[0-9]{3,}\\];")));   // no array of the 64 KiB guessed below the walk (LoadGuard::guessedBelow)
        bool printed = false;
        for (const MemoryAccess& m : r.accesses) printed |= m.insn == f.base + 2 * static_cast<uint32_t>(walkStore) && m.printed > 0;
        CHECK(printed);
        return r;
    };
    // The leaf of the ledger row: add #-8,r15 ; mov #5,r0 ; mov.l r0,@r15 ; mov r15,r2 ; add #8,r2 ; mov #2,r1 ; L: mov.l r1,@-r2 ; dt r1 ; bf L ;
    // mov.l @r15,r0 ; rts ; add #8,r15. The code returns 1.
    {
        const DecompileResult r = check("A3-65 leaf: r15 + 8 walked down by longwords",
                                        { 0x7FF8, 0xE005, 0x2F02, 0x62F3, 0x7208, 0xE102, 0x2216, 0x4110, 0x8BFC, 0x60F2, 0x000B, 0x7F08 }, 6, false, "return xStack_8;");
        CHECK(r.prototype.known);   // right, and so lent
    }
    check("A3-65 walked down by bytes", { 0x7FF8, 0xE005, 0x2F00, 0x62F3, 0x7208, 0xE108, 0x2214, 0x4110, 0x8BFC, 0x60F0, 0x000B, 0x7F08 }, 6, false, "return (int4)cStack_8;");
    check("A3-65 walked down by words", { 0x7FF8, 0xE005, 0x2F01, 0x62F3, 0x7208, 0xE104, 0x2215, 0x4110, 0x8BFC, 0x60F1, 0x000B, 0x7F08 }, 6, false, "return (int4)iStack_8;");
    // from r15 + N: the walk starts inside the frame (4, 8 of 12, 12 of 16)
    check("A3-65 from r15 + 4", { 0x7FF8, 0xE005, 0x2F02, 0x62F3, 0x7204, 0xE101, 0x2216, 0x4110, 0x8BFC, 0x60F2, 0x000B, 0x7F08 }, 6, false, "return xStack_8;");
    check("A3-65 from r15 + 8 of a 12-byte frame", { 0x7FF4, 0xE005, 0x2F02, 0x62F3, 0x7208, 0xE102, 0x2216, 0x4110, 0x8BFC, 0x60F2, 0x000B, 0x7F0C }, 6, false, "return xStack_c;");
    check("A3-65 from r15 + 12 of a 16-byte frame", { 0x7FF0, 0xE005, 0x2F02, 0x62F3, 0x720C, 0xE103, 0x2216, 0x4110, 0x8BFC, 0x60F2, 0x000B, 0x7F10 }, 6, false, "return xStack_10;");
    // ...a deeper walk than the partial analysis's three iterations: eight longwords from the top of a 32-byte frame, the lowest read back
    check("A3-65 eight longwords down a 32-byte frame", { 0x7FE0, 0xE005, 0x2F02, 0x62F3, 0x7220, 0xE108, 0x2216, 0x4110, 0x8BFC, 0x60F2, 0x000B, 0x7F20 }, 6, false, "return xStack_20;");
    // from the frame pointer r14: mov.l r14,@-r15 ; add #-8,r15 ; mov r15,r14 ; mov #5,r0 ; mov.l r0,@r14 ; mov r14,r2 ; add #8,r2 ; mov #2,r1 ;
    // L: mov.l r1,@-r2 ; dt r1 ; bf L ; mov.l @r14,r0 ; mov r14,r15 ; add #8,r15 ; rts ; mov.l @r15+,r14. The walk starts at the word r14 was pushed
    // to, which is no local (raw, see above).
    check("A3-65 from the frame pointer r14", { 0x2FE6, 0x7FF8, 0x6EF3, 0xE005, 0x2E02, 0x62E3, 0x7208, 0xE102, 0x2216, 0x4110, 0x8BFC, 0x60E2, 0x6FE3, 0x7F08, 0x000B, 0x6EF6 },
          8, true, "return xStack_c;");
    // ...and from r14 + 8 of a 12-byte frame, inside it: the pointer is a local's address
    check("A3-65 from the frame pointer r14, inside the frame", { 0x2FE6, 0x7FF4, 0x6EF3, 0xE005, 0x2E02, 0x62E3, 0x7208, 0xE102, 0x2216, 0x4110, 0x8BFC, 0x60E2, 0x6FE3, 0x7F0C, 0x000B, 0x6EF6 },
          8, false, "return xStack_10;");
    // a loop that writes, then a loop that reads back up (`mov.l @r2+,r4`): 2 + 1
    check("A3-65 walked down, then read back up", { 0x7FF8, 0xE005, 0x2F02, 0x62F3, 0x7208, 0xE102, 0x2216, 0x4110, 0x8BFC, 0xE102, 0xE300, 0x6426, 0x334C, 0x4110, 0x8BFB, 0x6033, 0x000B, 0x7F08 },
          6, false, "return iVar3;");
    // written by stores at constant offsets (5 and 7), read by a walk down (`add #-4,r2 ; mov.l @r2,r4`): the stores are kept, the walk reads them
    {
        const DecompileResult r = check("A3-65 written, then read down",
                                        { 0x7FF8, 0xE005, 0x2F02, 0xE007, 0x1F01, 0x62F3, 0x7208, 0xE102, 0xE300, 0x72FC, 0x6422, 0x334C, 0x4110, 0x8BFA, 0x6033, 0x000B, 0x7F08 },
                                        10, false, "return iVar3;");
        CHECK(Has(r.Text(), "= 5;") && Has(r.Text(), "= 7;"));
    }
    // over the save slot of a register its pop freed (`mov.l r13,@-r15 ; mov.l @r15+,r13` before the frame, the shape of seed 901303's run-on): the
    // slot was storage no local is mapped to, one variable with r13, and the text returned `unaff_r13`
    check("A3-65 over a freed save slot", { 0x2FD6, 0x6DF6, 0x7FF8, 0x62F3, 0x7208, 0xE102, 0x2216, 0x4110, 0x8BFC, 0x50F1, 0x000B, 0x7F08 }, 6, false, "return xStack_4;");
    // The controls: the same walked up (`mov.l r1,@r2 ; add #4,r2`), which the engine always had right: the local the walk wrote first.
    check("A3-65 control: walked up by longwords", { 0x7FF8, 0xE005, 0x2F02, 0x62F3, 0xE102, 0x2212, 0x7204, 0x4110, 0x8BFB, 0x60F2, 0x000B, 0x7F08 }, 5, false, "return aiStack_8[0];");
    check("A3-65 control: walked up by bytes", { 0x7FF8, 0xE005, 0x2F00, 0x62F3, 0xE108, 0x2210, 0x7201, 0x4110, 0x8BFB, 0x60F0, 0x000B, 0x7F08 }, 5, false, "return (int4)acStack_8[0];");
    // A walk down from the entry r15 reads no stack argument: the signature has none (the join's pointer is no reference to one).
    {
        const DecompileResult r = check("A3-65 a walk down from the entry r15 has no stack argument",
                                        { 0x7FF8, 0x6073, 0x81F0, 0x64F3, 0x7408, 0xE102, 0x2455, 0x4110, 0x8BFC, 0xE102, 0xE200, 0x6645, 0x326C, 0x4110, 0x8BFB, 0x85F3, 0x302C, 0x7F08, 0x000B, 0x0009 },
                                        6, false, "return iStack_2 + iVar3;");
        CHECK(!Has(r.Text(), "param_3") && !Has(r.Text(), "in_stack"));
    }
}

void TestFetchAndDataRefusals()
{
    Sh2Decompiler d(gConfig);
    auto snap = Fixture1Snapshot();
    CHECK(d.Decompile(snap, Request(dts::kFixture1Caller)).status == DecompileStatus::Ok);   // library started
    std::ostringstream errors;
    SaturnArchitecture arch(snap, &errors);
    arch.Initialise(20000, nullptr);
    ghidra::AddrSpace* ram = arch.getDefaultCodeSpace();
    ghidra::uint1 buf[16];
    for (int size : { 4, 16 })
    {
        bool data = false;
        try
        {
            arch.loader->loadFill(buf, size, ghidra::Address(ram, 0x00300000u - uint32_t(size) / 2));   // straddles LWRAM's end
        }
        catch (ghidra::DataUnavailError&)
        {
            data = true;
        }
        catch (ghidra::LowlevelError&)
        {
        }
        std::printf("  data read of %d bytes across LWRAM's end: %s\n", size, data ? "DataUnavailError" : "WRONG");
        CHECK(data);
    }
    bool fetch = false;
    struct Ignore : ghidra::AssemblyEmit
    {
        void dump(const ghidra::Address&, const std::string&, const std::string&) override {}
    } ignore;
    try
    {
        arch.translate->printAssembly(ignore, ghidra::Address(ram, 0x00000600u));
    }
    catch (ghidra::BadDataError&)
    {
        fetch = true;
    }
    catch (ghidra::LowlevelError&)
    {
    }
    std::printf("  instruction fetch from the BIOS: %s\n", fetch ? "BadDataError" : "WRONG");
    CHECK(fetch);
    CHECK(arch.FetchImage().HadUnavailable() && arch.FetchImage().Refused().begin()->first == 0x00000600u);
}


// Out of memory inside the engine, in a child process (this one's engine must stay usable, and
// the damaged one's static teardown is not run): the failure is reported, and every later
// request is refused as Unavailable rather than built on a translator that may be half-reset.
// Fails the next allocation, once, when the translator is about to be built or reset: the first
// allocation there is Sleigh::reset's replacement cache, after it has deleted the old ones.
// 'gResetOffset' picks which: 0 is Sleigh::reset's replacement cache; 1 is the next one, inside
// Sleigh::initialize on the reused translator.
std::atomic<bool> gFailInReset(false);
long gResetOffset = 0;
void FailNextAllocationInReset()
{
    if (gFailInReset.exchange(false)) dts::FailAfter() = gResetOffset;
}

// Out of memory inside Sleigh::reset on the reused translator: reported, every later request and
// disassembly refused without touching the engine, and then -- unlike the other children -- the
// process exits normally, through static destruction of that translator (patch 0002: reset no
// longer leaves deleted cache pointers behind for the destructor to delete again).
int ResetOomChild(const std::string& scratch, long offset)
{
    gResetOffset = offset;
    dts::PrepareProcess();
    gConfig = dts::Join(scratch, "config");
    sfe::MakeDirectory(scratch);
    sfe::MakeDirectory(gConfig);
    {
        Sh2Decompiler d(gConfig);
        auto snap = Fixture1Snapshot();
        const DecompileRequest q = Request(dts::kFixture1Caller);
        CHECK(d.Decompile(snap, q).status == DecompileStatus::Ok);   // the translator exists: reused below
        SaturnArchitecture::SetBuildTranslatorHookForTests(FailNextAllocationInReset);
        gFailInReset = true;
        DecompileResult r;
        bool escaped = false;
        try
        {
            r = d.Decompile(snap, q);
        }
        catch (...)
        {
            escaped = true;
        }
        dts::FailAfter() = -1;
        SaturnArchitecture::SetBuildTranslatorHookForTests(nullptr);
        std::printf("  child (reset +%ld): allocation %s fails -> status %d (%s)\n", offset,
                    offset == 0 ? "in Sleigh::reset" : "in Sleigh::initialize", int(r.status), r.error.c_str());
        CHECK(!escaped);
        CHECK(r.status == DecompileStatus::Failed);
        const DecompileResult after = d.Decompile(snap, q);
        CHECK(after.status == DecompileStatus::Unavailable);
        const std::vector<std::string> listing = d.Disassemble(snap, dts::kFixture1Caller, 4);
        std::printf("  child (reset): then decompile -> status %d, disassemble -> %s\n", int(after.status),
                    listing.empty() ? "(nothing)" : listing[0].c_str());
        CHECK(listing.size() == 1 && Has(listing[0], "unavailable"));
    }
    std::printf("  child (reset): returning normally, through static destruction\n");
    std::fflush(stdout);
    return dts::Failures() ? 1 : 0;
}

// A phase observer that throws an exception with a long message while the allocation that would
// copy that message fails: still a Failed result, nothing escapes.
int ObserverOomChild(const std::string& scratch)
{
    dts::PrepareProcess();
    gConfig = dts::Join(scratch, "config");
    sfe::MakeDirectory(scratch);
    sfe::MakeDirectory(gConfig);
    Sh2Decompiler d(gConfig);
    auto snap = Fixture1Snapshot();
    CHECK(d.Decompile(snap, Request(dts::kFixture1Caller)).status == DecompileStatus::Ok);
    static const std::runtime_error kLong(std::string(300, 'x'));
    d.SetPhaseObserver([](DecompilePhase p) {
        if (p != DecompilePhase::Flow) return;
        dts::FailAfter() = 0;   // the next allocation: copying the message into the result
        throw kLong;
    });
    DecompileResult r;
    bool escaped = false;
    try
    {
        r = d.Decompile(snap, Request(dts::kFixture1Caller));
    }
    catch (...)
    {
        escaped = true;
    }
    dts::FailAfter() = -1;
    std::printf("  child (observer): throwing observer + failed message copy -> status %d, error '%.20s'\n",
                int(r.status), r.error.c_str());
    CHECK(!escaped);
    CHECK(r.status == DecompileStatus::Failed);
    std::fflush(stdout);
    std::_Exit(dts::Failures() ? 1 : 0);
}

// Out of memory inside the reference run (the one that checks code removed as unreachable for
// hardware accesses): the same outcome as in the shown run -- Failed, and every later request
// refused -- not a result that claims the check was made. The first allocation after the
// reference run's actions begin is a parameter's replacement (ProtoStore::setOutput), which patch
// 0015 makes before the old one is freed: before it, the old one was freed twice at teardown.
int ReferenceOomChild(const std::string& scratch, long offset)
{
    dts::PrepareProcess();
    gConfig = dts::Join(scratch, "config");
    sfe::MakeDirectory(scratch);
    sfe::MakeDirectory(gConfig);
    Sh2Decompiler d(gConfig);
    std::vector<uint8_t> hw(0x100000u);
    const uint32_t base = 0x0600A480u;
    const uint16_t code[] = { 0xD104, 0xE001, 0x2102, 0x6012, 0x2008, 0x8BFC, 0xD202, 0x2200, 0x000B, 0x0009,
                              0x2600, 0x1000, 0x2010, 0x0063 };   // TestHardwareVolatility's mailboxFold
    size_t off = base - 0x06000000u;
    for (uint16_t w : code) { dts::Put16(hw, off, w); off += 2; }
    auto snap = MakeSnapshot(hw);
    DecompileRequest q = Request(base);
    {
        const DecompileResult warm = d.Decompile(snap, q);
        CHECK(warm.status == DecompileStatus::Ok && HasNote(warm, "here is missing"));
    }
    bool inReference = false;
    Sh2Decompiler::SetBeforeReferenceHookForTests([&] { inReference = true; });
    Sh2Decompiler::SetPhaseHookForTests([&](DecompilePhase p) {
        if (inReference && p == DecompilePhase::Actions && dts::FailAfter() < 0) dts::FailAfter() = offset;
    });
    DecompileResult r;
    bool escaped = false;
    try
    {
        r = d.Decompile(snap, q);
    }
    catch (...)
    {
        escaped = true;
    }
    dts::FailAfter() = -1;
    Sh2Decompiler::SetBeforeReferenceHookForTests(nullptr);
    Sh2Decompiler::SetPhaseHookForTests(nullptr);
    std::printf("  child (reference +%ld): allocation fails in the reference run's actions -> status %d (%s)\n", offset,
                int(r.status), r.error.c_str());
    CHECK(!escaped);
    CHECK(r.status == DecompileStatus::Failed && Has(r.error, "out of memory"));
    const DecompileResult after = d.Decompile(snap, q);
    std::printf("  child: next request -> status %d (%s)\n", int(after.status), after.error.c_str());
    CHECK(after.status == DecompileStatus::Unavailable);
    std::fflush(stdout);
    std::_Exit(dts::Failures() ? 1 : 0);
}

int EngineOomChild(const std::string& scratch, const std::string& where, long offset)
{
    dts::PrepareProcess();
    gConfig = dts::Join(scratch, "config");
    sfe::MakeDirectory(scratch);
    sfe::MakeDirectory(gConfig);
    Sh2Decompiler d(gConfig);
    auto snap = Fixture1Snapshot();
    DecompileRequest q = Request(dts::kFixture1Caller);
    // Counted on a warm run: the first builds the translator, so it allocates far more.
    CHECK(d.Decompile(snap, q).status == DecompileStatus::Ok);
    const DecompilePhase phase = where == "construct" ? DecompilePhase::Init : DecompilePhase::Flow;
    long at = -1;
    d.SetPhaseObserver([&](DecompilePhase p) {
        if (p == phase && at < 0) at = dts::AllocCount().load();
    });
    dts::AllocCount() = 0;
    CHECK(d.Decompile(snap, q).status == DecompileStatus::Ok);
    d.SetPhaseObserver(nullptr);
    const long useCount = snap.use_count();
    dts::FailAfter() = at + offset;   // 'construct': the Architecture and its load images; 'flow': flow following
    DecompileResult r;
    bool escaped = false;
    try
    {
        r = d.Decompile(snap, q);
    }
    catch (...)
    {
        escaped = true;
    }
    dts::FailAfter() = -1;
    std::printf("  child (%s +%ld): allocation %ld fails -> status %d (%s); snapshot references %ld -> %ld\n",
                where.c_str(), offset, at + offset, int(r.status), r.error.c_str(), useCount, long(snap.use_count()));
    CHECK(!escaped);
    CHECK(r.status == DecompileStatus::Failed);
    CHECK(snap.use_count() == useCount);   // nothing built on the snapshot was leaked
    const DecompileResult after = d.Decompile(snap, q);
    std::printf("  child: next request -> status %d (%s)\n", int(after.status), after.error.c_str());
    CHECK(after.status == DecompileStatus::Unavailable);
    CHECK(Has(after.error, "out of memory"));
    std::fflush(stdout);
    std::_Exit(dts::Failures() ? 1 : 0);   // skip static teardown of the damaged engine
}

// patches/0004-funcdata-frees-active-output.patch: a function destroyed while it still owns its
// return-recovery state (ActionPrototypeTypes allocated it; an abandoned analysis never reached
// the normal cleanup) frees it. Deterministic: the state is created directly and its pointer
// watched through the test allocator, rather than hoping a cancel lands in that window.
void TestFunctionFreesActiveOutput()
{
    Sh2Decompiler d(gConfig);
    auto snap = Fixture1Snapshot();
    CHECK(d.Decompile(snap, Request(dts::kFixture1Caller)).status == DecompileStatus::Ok);   // library started
    dts::WatchedFreed() = false;
    {
        std::ostringstream errors;
        SaturnArchitecture arch(snap, &errors);
        arch.Initialise(20000, nullptr);
        const ghidra::Address entry(arch.getDefaultCodeSpace(), dts::kFixture1Caller);
        ghidra::FunctionSymbol* sym = arch.symboltab->getGlobalScope()->addFunction(entry, "f");
        ghidra::Funcdata* fd = sym->getFunction();
        fd->initActiveOutput();
        CHECK(fd->getActiveOutput() != nullptr);
        dts::Watched() = fd->getActiveOutput();
    }   // the Architecture, its scopes, the symbol and the function go here
    dts::Watched() = nullptr;
    std::printf("  return-recovery state freed with its function: %s\n", dts::WatchedFreed() ? "yes" : "NO (leaked)");
    CHECK(dts::WatchedFreed());
}

void TestEngineAllocationFailureInChild(const char* self, const std::string& scratch)
{
    if (!dts::kInjectionUsable)
    {
        std::printf("  SKIPPED in an MSVC Debug build: its STL allocates inside noexcept moves, so an\n"
                    "  injected failure there terminates by design (the Windows Release run covers this)\n");
        return;
    }
    // Inside flow following, and each of the first allocations of building an Architecture (the
    // Architecture itself, then its two load images).
    const char* where[] = { "flow", "construct", "construct", "construct", "construct", "reset", "reset", "observer",
                            "reference", "reference", "reference" };
    const long offset[] = { 10, 0, 1, 2, 3, 0, 1, 0, 0, 1, 50 };
    for (int i = 0; i < 11; ++i)
    {
        std::string cmd = std::string("\"") + self + "\" --engine-oom " + where[i] + " " + std::to_string(offset[i]) +
                          " \"" + dts::Join(scratch, "oom") + "\"";
#ifdef _WIN32
        cmd = "\"" + cmd + "\"";   // cmd.exe strips the outer pair
#endif
        const int rc = std::system(cmd.c_str());
        CHECK(rc == 0);
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc == 5 && std::string(argv[1]) == "--engine-oom")
    {
        const std::string where = argv[2];
        if (where == "reset") return ResetOomChild(argv[4], std::atol(argv[3]));
        if (where == "observer") return ObserverOomChild(argv[4]);
        if (where == "reference") return ReferenceOomChild(argv[4], std::atol(argv[3]));
        return EngineOomChild(argv[4], where, std::atol(argv[3]));
    }
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
    if (const char* fixtures = std::getenv("SE_AREA2_FIXTURES"))   // round 8: the fixtures of a file, nothing else
    {
        try
        {
            TestArea2Round8Fixtures(fixtures);
        }
        catch (std::exception& e)
        {
            std::printf("FATAL: uncaught std::exception: %s\n", e.what());
            return 3;
        }
        dts::RemoveTree(scratch);
        return dts::Finish("Sh2DecompilerTests (fixtures)", start);
    }
    try
    {
        RUN(TestFixture1());
        RUN(TestMappingOracleOnFixtures());
        RUN(TestFixture1b());
        RUN(TestNoCodeAndMirror());
        RUN(TestMovaFollowsExecutionAddress());
        RUN(TestDelaySlotPcRelative());
        RUN(TestEditsChangeKeyAndOutput());
        RUN(TestMovWFold());
        RUN(TestHardwareRegisterNames());
        RUN(TestHardwareVolatility());
        RUN(TestHardwareMap());
        RUN(TestVolatileRangesAgainstModel());
        RUN(TestGameCode());
        RUN(TestGameLending());
        RUN(TestTruncatedFlow());
        RUN(TestCancelledBeforeStart());
        RUN(TestManyUncapturedBranches());
        RUN(TestFlowUncertainty());
        RUN(TestFlowReachedBody());
        RUN(TestDefaultAlsoCase());
        RUN(TestTailJumpToUnreachedCode());
        RUN(TestSignedGuardTable());
        RUN(TestAccessPastSpaceEnd());
        RUN(TestEngineEdgePaths());
        RUN(TestCalleeHarvest());
        RUN(TestBiosFunctionNames());
        RUN(TestCalleePrototypes());
        RUN(TestPrototypeEligibility());
        RUN(TestCallsAndTraps());
        RUN(TestArea2Round3());
        RUN(TestArea2Round4());
        RUN(TestArea2Round5());
        RUN(TestAreas234Leftovers());
        RUN(TestArea2Round7SlotValues());
        RUN(TestArea3Round8());
        RUN(TestRoutedSeeds());
        RUN(TestArea3Round9());
        RUN(TestArea3Round9Fix());
        RUN(TestArea3Round10());
        RUN(TestArea3Round10Fix());
        RUN(TestArea3HiddenInputFuzz());
        RUN(TestArea2Round8());
        RUN(TestArea2Round9());
        RUN(TestA365WalkDown());
        RUN(TestFetchAndDataRefusals());
        RUN(TestAllocationFailureIsReported());
        RUN(TestFunctionFreesActiveOutput());
        RUN(TestEngineAllocationFailureInChild(argv[0], scratch));
    }
    catch (std::exception& e)
    {
        std::printf("FATAL: uncaught std::exception: %s\n", e.what());
        return 3;
    }
    catch (...)
    {
        std::printf("FATAL: uncaught exception\n");
        return 3;
    }
    dts::RemoveTree(scratch);
    return dts::Finish("Sh2DecompilerTests", start);
}
