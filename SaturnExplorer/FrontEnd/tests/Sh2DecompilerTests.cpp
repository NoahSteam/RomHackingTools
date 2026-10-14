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

#include "AllocFailure.h"
#include "Decompiler/SaturnArchitecture.h"
#include "funcdata.hh"   // Funcdata, for the 0004 patch test
#include "Decompiler/Sh2Decompiler.h"
#include "Decompiler/SaturnHardwareMap.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <sstream>

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
    for (const Line& l : r.lines)
    {
        if (l.firstAddr) std::printf("  %-50s ; %08X\n", l.text.c_str(), l.firstAddr);
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

    // bf/s: whether the slot sees the target depends on the branch being taken. Flagged, neither
    // literal folded. And a bra slot that a bt also jumps straight to: the spec gives that address
    // the slot's PC on both paths, so it is flagged too.
    struct Case { const char* name; uint32_t entry, slot; } cases[] = {
        { "bf/s", dts::kFixture3Bfs, dts::kFixture3BfsSlot },
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
    // Every other delayed-branch kind (rts, jmp, braf, jsr), and mov.w in the slot: flagged at the
    // slot, and no literal word marked read-only.
    for (const dts::Fixture3Case& c : dts::kFixture3Others)
    {
        const DecompileResult r = d.Decompile(snap, Request(c.entry));
        std::printf("  %-13s status %d, unsupported %d at %08X\n", c.name, int(r.status), r.unsupported, r.unsupportedAt);
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(r.unsupported);
        CHECK(r.unsupportedAt == c.slot);
        CHECK(r.literalPools.empty());
        CHECK(!Has(r.Text(), "0x1234") && !Has(r.Text(), "4660"));
        CHECK(!Has(r.Text(), "0x44444444") && !Has(r.Text(), "0x55555555") && !Has(r.Text(), "0x66666666"));
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

    // The lookup: either partition, inside a register's width, and nothing in other partitions.
    CHECK(FindHardwareRegister(0x25F80000u) && std::string(FindHardwareRegister(0x25F80000u)->name) == "VDP2_TVMD");
    CHECK(FindHardwareRegister(0x05F80001u) && std::string(FindHardwareRegister(0x05F80001u)->name) == "VDP2_TVMD");
    CHECK(FindHardwareRegister(0x25FE00A6u) && std::string(FindHardwareRegister(0x25FE00A6u)->name) == "SCU_IST");
    CHECK(FindHardwareRegister(0x45F80000u) == nullptr);   // associative purge: not memory
    CHECK(FindHardwareRegister(0x05F8000Cu) == nullptr);   // VDP2's reserved word
    CHECK(FindHardwareRegister(0x20100062u) == nullptr);   // SMPC's even bytes are not registers
    size_t n = 0;
    const HardwareRegister* all = SaturnHardwareRegisters(n);
    for (size_t i = 1; i < n; ++i) CHECK(all[i - 1].address + all[i - 1].size <= all[i].address);
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
    Place(hw, indexedLocal, { 0x61F3,      // mov r15,r1
                              0x310C,      // add r0,r1
                              0x62F3,      // mov r15,r2
                              0x2122,      // mov.l r2,@r1
                              0x000B,      // rts
                              0x0009 });   //   nop
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
}

// Out of memory anywhere before the engine starts -- preparing the request, the name map --
// comes back as a failed result, never as an exception; the next request is unaffected.
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
    const char* where[] = { "flow", "construct", "construct", "construct", "construct", "reset", "reset", "observer" };
    const long offset[] = { 10, 0, 1, 2, 3, 0, 1, 0 };
    for (int i = 0; i < 8; ++i)
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
    try
    {
        RUN(TestFixture1());
        RUN(TestFixture1b());
        RUN(TestNoCodeAndMirror());
        RUN(TestMovaFollowsExecutionAddress());
        RUN(TestDelaySlotPcRelative());
        RUN(TestEditsChangeKeyAndOutput());
        RUN(TestMovWFold());
        RUN(TestHardwareRegisterNames());
        RUN(TestTruncatedFlow());
        RUN(TestCancelledBeforeStart());
        RUN(TestManyUncapturedBranches());
        RUN(TestFlowUncertainty());
        RUN(TestCalleeHarvest());
        RUN(TestBiosFunctionNames());
        RUN(TestCalleePrototypes());
        RUN(TestPrototypeEligibility());
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
