// Sh2Decompiler over synthetic snapshots (PLAN.md Step 2): what the engine wrapper produces
// for the hand-assembled, Capstone-verified fixtures, and that a result follows the bytes it
// was made from (A1).
//
//   fixture 1   a caller calling through a literal pool, the argument moved in the delay slot:
//               the callee is found and named, the literal folded, tokens carry addresses
//   fixture 1b  the same call into BIOS, which is never captured: an opaque call, not truncated
//   fixture 3   a PC-relative load in a delay slot (A3): unsupported, neither literal folded
//   A1          an edited instruction word, an edited literal, a replaced BIOS image: new key,
//               new output
//
// usage: SaturnExplorerSh2DecompilerTests <scratch dir>
#include "DecompilerTestSupport.h"

#include "Decompiler/Sh2Decompiler.h"

#include <cstring>

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

// A3: a PC-relative load in a delay slot. The spec computes the slot's own PC-relative address,
// the hardware reads relative to the branch target; the fixture holds different literals at
// the two. Step 2 must flag the function and fold neither.
void TestDelaySlotPcRelative()
{
    Sh2Decompiler d(gConfig);
    std::vector<uint8_t> hw;
    dts::PlaceFixture3(hw);
    auto snap = MakeSnapshot(hw);
    struct Case { const char* name; uint32_t entry, slot; } cases[] = {
        { "bra", dts::kFixture3Bra, dts::kFixture3BraSlot },
        { "bf/s", dts::kFixture3Bfs, dts::kFixture3BfsSlot },
    };
    for (const Case& c : cases)
    {
        const DecompileResult r = d.Decompile(snap, Request(c.entry));
        Print(c.name, r);
        const std::string text = r.Text();
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(r.unsupported);
        CHECK(r.unsupportedAt == c.slot);
        CHECK(!Has(text, "0x11111111") && !Has(text, "286331153"));   // the spec's literal
        CHECK(!Has(text, "0x22222222") && !Has(text, "572662306"));   // the hardware's literal
        CHECK(r.literalPools.empty());
        bool warned = false;
        for (const std::string& w : r.warnings) warned |= Has(w, "delay slot");
        CHECK(warned);
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
    try
    {
        RUN(TestFixture1());
        RUN(TestFixture1b());
        RUN(TestNoCodeAndMirror());
        RUN(TestDelaySlotPcRelative());
        RUN(TestEditsChangeKeyAndOutput());
        RUN(TestMovWFold());
        RUN(TestTruncatedFlow());
        RUN(TestCancelledBeforeStart());
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
