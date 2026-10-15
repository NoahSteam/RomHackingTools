// The SH-2 SLEIGH spec's semantics, executed: each case runs the instruction words through the
// vendored p-code emulator on the same translator the decompiler uses (the checked-in sh-2.sla,
// through SpecBundle), and compares registers and memory with a reference model.
//
// The reference is Mednafen's SH-2 core (ss/sh7095_ops.inc), which matches the Renesas SH-1/SH-2
// Programming Manual where the manual is explicit and fills in what it leaves open (MAC.L's 48-bit
// saturation on the full MAC, MAC.W's MACH bit 0). Each block below pins one fix of patch 0006: a
// wrong spec reaches ordinary C output without any warning, so only executing it shows the error.
//
// usage: SaturnExplorerSh2SemanticsTests <scratch dir>
#include "DecompilerTestSupport.h"

#include "Decompiler/SaturnArchitecture.h"
#include "emulate.hh"
#include "memstate.hh"

#include <cstring>
#include <sstream>

using namespace sfe::decomp;

namespace
{

std::string gConfig;

const uint32_t kCode = 0x06001000u;   // where each case's instruction words go
const uint32_t kData = 0x06002000u;   // operands in memory

// One instruction sequence on the emulator. Memory starts as the snapshot (the code) and takes
// writes in an overlay; registers start at zero.
class Machine
{
public:
    explicit Machine(const std::vector<uint16_t>& words)
    {
        std::vector<uint8_t> hw;
        // nops around the case: the emulator decodes the instruction it stops at.
        for (uint32_t a = 0; a < 0x200; a += 2) dts::Put16(hw, kCode - 0x06000000u + a, 0x0009);
        for (size_t i = 0; i < words.size(); ++i) dts::Put16(hw, kCode - 0x06000000u + 2 * i, words[i]);
        mSnap = dts::MakeSnapshot(hw);
        mArch.reset(new SaturnArchitecture(mSnap, &mErrors));
        mArch->Initialise(20000, nullptr);
        // The emulator API takes a mutable translator; it only decodes through it.
        ghidra::Translate* t = const_cast<ghidra::Translate*>(mArch->translate);
        mRam = t->getDefaultCodeSpace();
        // Eight-byte bank words: MAC.L/MAC.W compute in 8-byte temporaries, and a bank only moves a
        // value of up to its word size in one piece.
        mImage.reset(new ghidra::MemoryImage(mRam, 8, 4096, mArch->loader));
        mRamState.reset(new ghidra::MemoryPageOverlay(mRam, 8, 4096, mImage.get()));
        mRegs.reset(new ghidra::MemoryHashOverlay(t->getSpaceByName("register"), 8, 4096, 4096, nullptr));
        mTemps.reset(new ghidra::MemoryHashOverlay(t->getUniqueSpace(), 8, 4096, 4096, nullptr));
        mState.reset(new ghidra::MemoryState(t));
        mState->setMemoryBank(mRamState.get());
        mState->setMemoryBank(mRegs.get());
        mState->setMemoryBank(mTemps.get());
        mBreaks.reset(new ghidra::BreakTableCallBack(t));
        mEmu.reset(new ghidra::EmulatePcodeCache(t, mState.get(), mBreaks.get()));
    }
    ~Machine()
    {
        // The emulator and state point into the translator: gone before the Architecture.
        mEmu.reset();
        mBreaks.reset();
        mState.reset();
    }

    void Set(const char* reg, uint32_t v) { mState->setValue(reg, v); }
    uint32_t Get(const char* reg) const { return static_cast<uint32_t>(mState->getValue(reg)); }
    void Poke(uint32_t a, int size, uint32_t v) { mState->setValue(mRam, a, size, v); }
    uint32_t Peek(uint32_t a, int size) const { return static_cast<uint32_t>(mState->getValue(mRam, a, size)); }

    // Execute from 'start' until the PC is 'stop'. False if it never gets there.
    bool Run(uint32_t start, uint32_t stop)
    {
        mEmu->setExecuteAddress(ghidra::Address(mRam, start));
        mEmu->setHalt(false);
        for (int i = 0; i < 64; ++i)
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

const uint32_t kT = 1u << 0, kS = 1u << 1, kQ = 1u << 8, kM = 1u << 9;

// xorshift64*, fixed seed: the same cases every run.
struct Rng
{
    uint64_t s = 0x9E3779B97F4A7C15ull;
    uint32_t Next()
    {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return static_cast<uint32_t>((s * 2685821657736338717ull) >> 32);
    }
    // Biased towards the values where carries and saturation happen.
    uint32_t Operand()
    {
        static const uint32_t edges[] = { 0u, 1u, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 0x00010001u, 0xFFFF0000u,
                                          0x0000FFFFu, 0x00008000u, 0xFFFF8000u, 0x00007FFFu, 0x80000001u };
        const uint32_t r = Next();
        return (r & 3) == 0 ? edges[Next() % (sizeof edges / sizeof edges[0])] : Next();
    }
};

// ---- Reference model (Mednafen ss/sh7095_ops.inc) ------------------------------------------

void RefMacL(uint32_t& mach, uint32_t& macl, int32_t m0, int32_t m1, bool s)
{
    const uint64_t a = (uint64_t(mach) << 32) | macl;
    uint64_t sum = a + uint64_t(int64_t(m0) * m1);
    if (s && sum > 0x00007FFFFFFFFFFFull && sum < 0xFFFF800000000000ull)
        sum = int32_t(m0 ^ m1) < 0 ? 0xFFFF800000000000ull : 0x00007FFFFFFFFFFFull;
    mach = uint32_t(sum >> 32);
    macl = uint32_t(sum);
}

void RefMacW(uint32_t& mach, uint32_t& macl, int16_t m0, int16_t m1, bool s)
{
    if (s)
    {
        const int32_t b = int32_t(m0) * m1;
        uint64_t sum = uint64_t(int64_t(int32_t(macl)) + b);
        if (sum > 0x000000007FFFFFFFull && sum < 0xFFFFFFFF80000000ull)
        {
            mach |= 1;
            sum = b < 0 ? 0x80000000ull : 0x7FFFFFFFull;
        }
        macl = uint32_t(sum);
    }
    else
    {
        const uint64_t sum = ((uint64_t(mach) << 32) | macl) + uint64_t(int64_t(m0) * m1);
        mach = uint32_t(sum >> 32);
        macl = uint32_t(sum);
    }
}

// DIV1 with the operands as the hardware reads them: Rm after Rn has been shifted.
void RefDiv1(uint32_t& rn, uint32_t rmIfDistinct, bool alias, uint32_t& sr)
{
    const bool q = sr & kQ, m = sr & kM, t = sr & kT;
    bool newQ = (rn >> 31) != 0;
    rn = (rn << 1) | (t ? 1u : 0u);
    const uint32_t tmp = rn;
    const uint32_t rm = alias ? rn : rmIfDistinct;
    newQ ^= m;
    if (!(q ^ m))
    {
        rn -= rm;
        newQ ^= rn > tmp;
    }
    else
    {
        rn += rm;
        newQ ^= rn < tmp;
    }
    sr = (sr & ~(kQ | kT)) | (newQ ? kQ : 0u) | (newQ == m ? kT : 0u);
}

// ---- Cases ----------------------------------------------------------------------------------

// Finding 2: bsr, bsrf and jsr write PR before their delay slot runs.
void TestCallWritesPrBeforeSlot()
{
    const uint32_t target = kCode + 0x100, ret = kCode + 4;
    struct Call { const char* name; uint16_t word; uint32_t r3; };
    const Call calls[] = {
        { "bsr", static_cast<uint16_t>(0xB000 | ((target - (kCode + 4)) / 2)), 0 },
        { "bsrf r3", 0x0303, target - (kCode + 4) },
        { "jsr @r3", 0x430B, target },
    };
    for (const Call& c : calls)
    {
        {
            Machine m({ c.word, 0x002A });   // sts pr,r0 in the slot: the new return address
            m.Set("r3", c.r3);
            m.Set("pr", 0x11111111u);
            CHECK(m.Run(kCode, target));
            if (m.Get("r0") != ret) std::printf("  %s: sts pr,r0 in the slot read %08X\n", c.name, m.Get("r0"));
            CHECK(m.Get("r0") == ret);
            CHECK(m.Get("pr") == ret);
        }
        {
            Machine m({ c.word, 0x422A });   // lds r2,pr in the slot: the slot's write stands
            m.Set("r3", c.r3);
            m.Set("r2", 0x06003000u);
            CHECK(m.Run(kCode, target));
            if (m.Get("pr") != 0x06003000u) std::printf("  %s: lds r2,pr in the slot left PR %08X\n", c.name, m.Get("pr"));
            CHECK(m.Get("pr") == 0x06003000u);
        }
    }
}

// Findings 3: MAC.L, signed 32 x 32 + 64, 48-bit saturation with S.
void TestMacL()
{
    {
        // The review's case: 00010001 x 00010001 + 0 with S clear is 00000001:00020001.
        Machine m({ 0x045F });   // mac.l @r5+,@r4+
        m.Poke(kData, 4, 0x00010001u);
        m.Poke(kData + 4, 4, 0x00010001u);
        m.Set("r5", kData);
        m.Set("r4", kData + 4);
        CHECK(m.Run(kCode, kCode + 2));
        CHECK(m.Get("mach") == 0x00000001u && m.Get("macl") == 0x00020001u);
        CHECK(m.Get("r5") == kData + 4 && m.Get("r4") == kData + 8);
    }
    Machine m({ 0x045F });
    Rng rng;
    int bad = 0;
    for (int i = 0; i < 20000; ++i)
    {
        const uint32_t a = rng.Operand(), b = rng.Operand();
        uint32_t mach = rng.Operand(), macl = rng.Operand();
        const bool s = rng.Next() & 1;
        if (s && (rng.Next() & 1)) mach = (mach & 0xFFFF) | ((mach & 0x8000) ? 0xFFFF0000u : 0u);   // in range
        m.Poke(kData, 4, a);
        m.Poke(kData + 4, 4, b);
        m.Set("r5", kData);
        m.Set("r4", kData + 4);
        m.Set("mach", mach);
        m.Set("macl", macl);
        m.Set("sr", s ? kS : 0u);
        uint32_t eh = mach, el = macl;
        RefMacL(eh, el, int32_t(a), int32_t(b), s);
        if (!m.Run(kCode, kCode + 2) || m.Get("mach") != eh || m.Get("macl") != el)
        {
            if (++bad <= 5)
                std::printf("  mac.l %08X x %08X + %08X:%08X S=%d: want %08X:%08X, got %08X:%08X\n", a, b, mach, macl,
                            s, eh, el, m.Get("mach"), m.Get("macl"));
        }
    }
    CHECK(bad == 0);

    // m == n reads two consecutive words.
    Machine alias({ 0x044F });   // mac.l @r4+,@r4+
    alias.Poke(kData, 4, 3);
    alias.Poke(kData + 4, 4, 0xFFFFFFFEu);
    alias.Set("r4", kData);
    CHECK(alias.Run(kCode, kCode + 2));
    CHECK(alias.Get("mach") == 0xFFFFFFFFu && alias.Get("macl") == 0xFFFFFFFAu);
    CHECK(alias.Get("r4") == kData + 8);
}

// Finding 4: MAC.W reads two words, sign-extended, and accumulates into all of MAC (S clear) or
// saturates MACL at 32 bits (S set).
void TestMacW()
{
    {
        // 1 x 1 + 0, whatever follows the operand words.
        Machine m({ 0x445F });   // mac.w @r5+,@r4+
        m.Poke(kData, 4, 0x0001FFFFu);
        m.Poke(kData + 4, 4, 0x00017FFFu);
        m.Set("r5", kData);
        m.Set("r4", kData + 4);
        CHECK(m.Run(kCode, kCode + 2));
        CHECK(m.Get("mach") == 0 && m.Get("macl") == 1);
        CHECK(m.Get("r5") == kData + 2 && m.Get("r4") == kData + 6);
    }
    {
        // S set: 7FFFFFFF + 1 saturates.
        Machine m({ 0x445F });
        m.Poke(kData, 2, 1);
        m.Poke(kData + 2, 2, 1);
        m.Set("r5", kData);
        m.Set("r4", kData + 2);
        m.Set("macl", 0x7FFFFFFFu);
        m.Set("sr", kS);
        CHECK(m.Run(kCode, kCode + 2));
        CHECK(m.Get("macl") == 0x7FFFFFFFu && (m.Get("mach") & 1) == 1);
    }
    Machine m({ 0x445F });
    Rng rng;
    int bad = 0;
    for (int i = 0; i < 20000; ++i)
    {
        const uint32_t wa = rng.Operand(), wb = rng.Operand();
        const uint16_t a = uint16_t(wa >> (rng.Next() & 16)), b = uint16_t(wb >> (rng.Next() & 16));
        const uint32_t mach = rng.Operand(), macl = rng.Operand();
        const bool s = rng.Next() & 1;
        m.Poke(kData, 2, a);
        m.Poke(kData + 2, 2, rng.Next() & 0xFFFF);   // the words after each operand must not matter
        m.Poke(kData + 4, 2, b);
        m.Poke(kData + 6, 2, rng.Next() & 0xFFFF);
        m.Set("r5", kData);
        m.Set("r4", kData + 4);
        m.Set("mach", mach);
        m.Set("macl", macl);
        m.Set("sr", s ? kS : 0u);
        uint32_t eh = mach, el = macl;
        RefMacW(eh, el, int16_t(a), int16_t(b), s);
        if (!m.Run(kCode, kCode + 2) || m.Get("mach") != eh || m.Get("macl") != el)
        {
            if (++bad <= 5)
                std::printf("  mac.w %04X x %04X + %08X:%08X S=%d: want %08X:%08X, got %08X:%08X\n", a, b, mach, macl,
                            s, eh, el, m.Get("mach"), m.Get("macl"));
        }
    }
    CHECK(bad == 0);
}

// Finding 5: XOR.B is an exclusive or.
void TestXorB()
{
    Machine m({ 0xCE0F });   // xor.b #0x0F,@(r0,gbr)
    m.Set("gbr", kData);
    m.Set("r0", 0x10);
    m.Poke(kData + 0x10, 1, 0xAA);
    CHECK(m.Run(kCode, kCode + 2));
    CHECK(m.Peek(kData + 0x10, 1) == 0xA5);
}

// Finding 6: mov.x Rm,@-Rn stores Rm as it was before Rn was decremented.
void TestPredecrementStoreAlias()
{
    struct Case { uint16_t word; int size; };
    for (const Case& c : { Case{ 0x2114, 1 }, Case{ 0x2115, 2 }, Case{ 0x2116, 4 } })   // mov.b/w/l r1,@-r1
    {
        Machine m({ c.word });
        const uint32_t r1 = 0x06080004u;
        m.Set("r1", r1);
        CHECK(m.Run(kCode, kCode + 2));
        const uint32_t at = r1 - uint32_t(c.size);
        const uint32_t mask = c.size == 4 ? 0xFFFFFFFFu : (1u << (8 * c.size)) - 1;
        CHECK(m.Get("r1") == at);
        if (m.Peek(at, c.size) != (r1 & mask))
            std::printf("  size %d: stored %08X, want %08X\n", c.size, m.Peek(at, c.size), r1 & mask);
        CHECK(m.Peek(at, c.size) == (r1 & mask));
    }
}

// Finding 7: DIV1 reads Rm after shifting Rn, which matters when they are the same register.
void TestDiv1()
{
    {
        Machine m({ 0x3114 });   // div1 r1,r1
        m.Set("r1", 5);
        CHECK(m.Run(kCode, kCode + 2));
        CHECK(m.Get("r1") == 0);
    }
    Rng rng;
    int bad = 0;
    for (int alias = 0; alias < 2; ++alias)
    {
        Machine m({ static_cast<uint16_t>(alias ? 0x3114 : 0x3124) });   // div1 r1,r1 / div1 r2,r1
        for (int i = 0; i < 5000; ++i)
        {
            uint32_t rn = rng.Operand();
            const uint32_t rm = rng.Operand();
            uint32_t sr = rng.Next() & (kQ | kM | kT);
            m.Set("r1", rn);
            m.Set("r2", rm);
            m.Set("sr", sr);
            const uint32_t rn0 = rn, sr0 = sr;
            RefDiv1(rn, rm, alias != 0, sr);
            if (!m.Run(kCode, kCode + 2) || m.Get("r1") != rn || (m.Get("sr") & (kQ | kT)) != (sr & (kQ | kT)))
            {
                if (++bad <= 5)
                    std::printf("  div1 %s r1=%08X r2=%08X sr=%03X: want %08X/%03X, got %08X/%03X\n",
                                alias ? "r1,r1" : "r2,r1", rn0, rm, sr0, rn, sr & (kQ | kT), m.Get("r1"),
                                m.Get("sr") & (kQ | kT));
            }
        }
    }
    CHECK(bad == 0);
}

// Ledger A2-9: trapa pushes SR and then the address after it, and its handler's rte pops them. The
// emulator ends the instruction at the call, so what it runs is the push alone -- the state the
// handler is entered with, which is what Mednafen reaches (the decode test compares exactly that).
// The spec also gives r15 its 8 bytes back after the call, for the decompiler, which carries on
// after a call; the emulator must never run that op, or the handler would be entered with the
// caller's r15 and its rte would pop the caller's stack.
void TestTrapaStack()
{
    const uint32_t handler = kCode + 0x20, vbr = kData, sp = kData + 0x100;
    // trapa #5 ; nop ... and the handler: rte ; nop
    std::vector<uint16_t> words(18, 0x0009);
    words[0] = 0xC305;
    words[16] = 0x002B;
    auto start = [&](Machine& m) {
        m.Set("vbr", vbr);
        m.Set("r15", sp);
        m.Set("sr", 0x000003F3u);
        m.Poke(vbr + 5 * 4, 4, handler);
    };
    {
        Machine m(words);
        start(m);
        CHECK(m.Run(kCode, handler));
        CHECK(m.Get("r15") == sp - 8);                 // entered with SR and the return address pushed
        CHECK(m.Peek(sp - 4, 4) == 0x000003F3u);       // SR first
        CHECK(m.Peek(sp - 8, 4) == kCode + 2);         // then the address of the next instruction
        CHECK(m.Get("sr") == 0x000003F3u);             // SR itself is unchanged
    }
    {
        Machine m(words);
        start(m);
        CHECK(m.Run(kCode, kCode + 2));                // the handler's rte comes back
        CHECK(m.Get("r15") == sp);                     // ...with both words popped
        CHECK(m.Get("sr") == 0x000003F3u);
    }
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
    std::string error;
    if (!SaturnArchitecture::StartLibrary(gConfig, error))
    {
        std::printf("FATAL: %s\n", error.c_str());
        return 3;
    }
    try
    {
        RUN(TestCallWritesPrBeforeSlot());
        RUN(TestMacL());
        RUN(TestMacW());
        RUN(TestXorB());
        RUN(TestPredecrementStoreAlias());
        RUN(TestDiv1());
        RUN(TestTrapaStack());
    }
    catch (ghidra::LowlevelError& e)
    {
        std::printf("FATAL: engine error: %s\n", e.explain.c_str());
        return 3;
    }
    catch (std::exception& e)
    {
        std::printf("FATAL: uncaught std::exception: %s\n", e.what());
        return 3;
    }
    dts::RemoveTree(scratch);
    return dts::Finish("Sh2SemanticsTests", start);
}
