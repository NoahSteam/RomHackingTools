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
    CHECK(r.truncatedAt == 0x00300000u || (r.truncatedAt >= 0x00300000u && r.truncatedAt <= 0x00300010u));
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
    // The function being decompiled keeps its own recovery even when a prototype is given for it.
    DecompileRequest self = Request(callee);
    Prototype wrong;
    wrong.known = true;
    wrong.ret.kind = TypeKind::Int;
    wrong.ret.size = 4;
    self.prototypes.emplace_back(callee, wrong);
    CHECK(d.Decompile(snap, self).Text() == c.Text());
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
    CHECK(arch.FetchImage().HadUnavailable() && arch.FetchImage().UnavailableAddress() == 0x00000600u);
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
        RUN(TestDelaySlotPcRelative());
        RUN(TestEditsChangeKeyAndOutput());
        RUN(TestMovWFold());
        RUN(TestHardwareRegisterNames());
        RUN(TestTruncatedFlow());
        RUN(TestCancelledBeforeStart());
        RUN(TestManyUncapturedBranches());
        RUN(TestCalleeHarvest());
        RUN(TestBiosFunctionNames());
        RUN(TestCalleePrototypes());
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
