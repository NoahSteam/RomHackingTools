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
#include "emulate.hh"    // the p-code emulator, for the call oracle
#include "Sh2EmulatorHooks.h"
#include "funcdata.hh"   // Funcdata, for the 0004 patch test
#include "memstate.hh"
#include "Decompiler/Sh2Decompiler.h"
#include "Decompiler/SaturnHardwareMap.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <map>
#include <stdexcept>
#include <cstdlib>
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

// A mailbox: one write, then one read per pass through the poll, returned as read.
std::vector<Op> Mailbox(uint32_t a) { return { Wr(a, 2), Rd(a, 2, true, true) }; }

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
    }
    for (const auto* group : { &r4, &r5, &r7 })
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
    CHECK(Ledger(r, { Wr(0x20100063u, 1), Rd(0x20100063u, 1, true, true) }));
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
    CHECK(Ledger(r, { Wr(0x201000E3u, 1), Rd(0x201000E3u, 1, true, true) }));
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
            else CHECK(Ledger(r, { Rd(w.f->base + w.f->literal, 4), Wr(0, 2), Rd(0, 2, true, true) }));   // pointer, then mailbox
            if (w.name) CHECK(kept(r, w.name));
            else CHECK(Has(r.Text(), "*piVar1 = 1;") && Count(r.Text(), "*piVar1") == 3 && !Has(r.Text(), "while( true )"));
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
        CHECK(Ledger(r, { Rd(ptrMailbox.base + ptrMailbox.literal, 4), Wr(0, 2), Rd(0, 2, true, true) }));
        CHECK(Has(r.Text(), "*piVar1 = 1;"));
        CHECK(Count(r.Text(), "*piVar1") == 3);   // the declaration, the write, the one read in the loop
        CHECK(!Has(r.Text(), "return (int4)*piVar1"));
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
    CHECK(r.hardwareNotes.empty());
    r = run("WTCNT read", wtcntRead);
    CHECK(Ledger(r, { Rd(0xFFFFFE81u, 1, false, true) }));
    CHECK(r.hardwareNotes.empty());   // read where it is read: nothing to say
    r = run("TVSTAT read", tvstatRead);
    // Read-only, so no caution; but the read is not free (A5-16, vdp2.c RegsRead case 0x04): it clears EXLTFG.
    CHECK(r.hardwareNotes.size() == 1 && HasNote(r, "VDP2_TVSTAT: a read clears EXLTFG", false));
    r = run("PTMR written", ptmrWrite);
    // The value is unknown: bit 0 starts drawing (vdp1.c WriteReg case 2), which the line does not say (A5 round 4).
    CHECK(r.hardwareNotes.size() == 1 && HasNote(r, "VDP1_PTMR: a write with bit 0 set starts drawing the command table", false));
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
    CHECK(Ledger(r, { Rd(0, 4, false, true), Wr(0, 1) }));
    r = run("two stores through one pointer", twoStores);
    CHECK(Ledger(r, { Wr(0, 4), Wr(0, 4) }));
    r = run("compound assignment through pointers", compound);
    CHECK(Ledger(r, { Rd(0, 4, false, true), Rd(0, 4), Wr(0, 4) }));
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
        CHECK(Has(r.Text(), "puVar2 = &VDP2_TVSTAT;") && Has(r.Text(), "puVar2 = &VDP2_VCNT;"));
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
    // calls it with the argument (the caller's own signature stays withheld: the jump is still unresolved).
    DecompileRequest q = Request(a + 0x0A);
    const DecompileResult callee = d.Decompile(snap, q);
    CHECK(callee.status == DecompileStatus::Ok && callee.prototype.known && Has(callee.Text(), "param_1 + 1"));
    q = Request(a);
    q.prototypes.emplace_back(a + 0x0A, callee.prototype);
    r = d.Decompile(snap, q);
    Print("the destination named and lent", r);
    CHECK(r.status == DecompileStatus::Ok && !r.prototype.known && Has(r.Text(), "func_0x0600900a(param_1)"));
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
    Place(hw, indexedLocal, { 0x61F3,      // mov r15,r1
                              0x314C,      // add r4,r1   (an unknown index; r0 would be an input no signature passes, A3-5)
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
    };
    void SetEntry(const Entry* e) { mEntry = e; }

    // 'args' are the values of param_1, param_2, ... (r4, r5, ...).
    PseudoEval(const std::string& text, const std::vector<uint32_t>& args) : mArgs(args)
    {
        Lex(text);
        for (size_t i = 0; i + 1 < mTok.size(); ++i)
            if (mTok[i] == "&") mAddrTaken.insert(mTok[i + 1]);   // a local whose address is taken: a call may write it
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
                off = -static_cast<int64_t>(std::strtoul(name.c_str() + name.find("Stack_") + 6, nullptr, 16));
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
    static bool IsStackLocal(const std::string& name)
    {
        const size_t at = name.find("Stack_");
        if (at == std::string::npos || at + 6 >= name.size()) return false;
        for (size_t i = at + 6; i < name.size(); ++i)
            if (!std::isxdigit(static_cast<unsigned char>(name[i]))) return false;
        return true;
    }
    void ForgetStackLocals(std::map<std::string, Val>& env)
    {
        for (auto& e : env)
            if (IsStackLocal(e.first)) e.second = Unk(e.second.size, e.second.sg);
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
bool PseudoAgreesWithRun(const DecompileResult& r, const std::shared_ptr<MemorySnapshot>& snap, const CallFixture& f,
                         const std::vector<std::vector<uint32_t>>& inputs)
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
        if (!MayReturn(r.Text(), args, ran, why, !f.noResult, &entry))
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
        CHECK(PseudoAgreesWithRun(r, snap, f, with ? *with : inputs));
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
            CHECK(!oracle || PseudoAgreesWithRun(l.caller, snap, f, inputs));
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
            // Accepted, pinned: a path that leaves r0 alone and another that sets it to a constant (`phi(in_r0, 5)`) is lent
            // `void` as before. It hides the caller's r0 behind the constant, but compiled code leaves exactly this as scratch: the
            // rule that withholds it (a join with r0's entry value, whatever the other path) takes 15 functions of image 007 and
            // 24 of image 008 out of the lent set, every one a void function with a scratch r0. The interrupt-mask idiom's plain
            // transform of SR left in r0 (06010054) is not a join either, and stays lent.
            const CallFixture alone = { "r0 left alone on one path, set to a constant on the other", base, { 0x2448, 0x8900, 0xE005, 0x000B, 0x0009 } };
            auto snapA = CallSnapshot(alone, ++serial);
            const DecompileResult ra = d.Decompile(snapA, Request(alone.base));
            Print(alone.name.c_str(), ra);
            CHECK(ra.prototype.known);
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
        // The evaluator reads an array local as memory (it cannot know what a call wrote to it).
        {
            std::string why;
            CHECK(MayReturn("int f(void)\n{\n  int axStack_c [2];\n\n  axStack_c[0] = 5;\n  func_0x06009020();\n  return axStack_c[0];\n}", {}, 7, why));
            CHECK(!MayReturn("int f(void)\n{\n  func_0x06009020();\n  return 5;\n}", {}, 7, why));
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
        // ...but SR only stored (nothing can read it back here) is still machine state: lent
        const DecompileResult r = decompile("A2-23 control: SR stored only", { 0x0102, 0x2412, 0x000B, 0x0009 });   // stc sr,r1 ; mov.l r1,@r4 ; rts
        CHECK(r.prototype.known);
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
