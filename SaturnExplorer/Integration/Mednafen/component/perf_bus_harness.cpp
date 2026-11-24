// F8 component test: bus-request counts of SH-2 cache fills and the SH-2 on-chip DMAC, on the REAL patched code.
// The expected numbers are derived from the source in BUS_EXPECTED.md (ids C1.. and D1.. below) and were written
// before this file.
//
// REAL (nothing copied or re-typed):
//   * the whole patched ss.cpp is #included, so the SH-2 core (sh7095.inc: MemRead / MemWrite, the cache
//     (Cache_FindWay, LRU, SetCCR, associative purge), ExtBus*_INLINE, BSC_BusRead / BSC_BusWrite WITH the injected
//     SeSsPerfBus hook, DMA_DoTransfer / DMA_Update, the on-chip register writes that start a DMA) and the bus
//     handlers behind the hook (BusRW_DB_CS0 / CS3 / CS12, the SCU's SCU_FromSH2_BusRW_DB address decode, work RAM,
//     BIOS ROM array) are Mednafen's own, as patched by the current apply.py;
//   * SeSsBusSlot (the address -> bucket table the hook indexes) and SeSsPerfC, from the patched ss.cpp;
//   * the instruction fetch is driven through the real CPU[0].Step<0,EIC,false>() on a NOP stream.
// STUBBED (they sit BEHIND the hook, or are state the emulator would have set up):
//   * the devices the SCU forwards to (VDP1, VDP2, SCSP, CD block, cartridge, SMPC): empty bodies, below;
//   * the memory map: SH7095_FastMap is filled by hand for BIOS / low WRAM / high WRAM (what SS_SetPhysMemMap does
//     in the emulator), the event list is the real one with every event disabled;
//   * InitCommon's `SeSsCacheMode = NeedEmuICache ? ...` is not executed here (it needs a loaded game); its
//     placement is checked as text by check_component.py, and the two cache modes are produced by CPU[0].Init(EIC).
// What this does NOT prove: that the numbers match a running game (V1 needs a BIOS), or the cache-mode label itself.
//
// Built and run by Integration/Mednafen/check_component.py (defines SE_SS_CPP).
#include SE_SS_CPP

#include <cstdio>
#include <cstring>

extern "C"
{
unsigned int SeExportCallStackEpoch(int cpu);
}

namespace Mednafen
{
MDFNGI* MDFNGameInfo;
const std::vector<CheatFormatStruct> CheatFormatInfo_Empty;
}
extern "C" { char SeHarnessNvfsStorage[512] asm("_ZN8Mednafen4NVFSE") __attribute__((visibility("hidden"), aligned(64))); }
namespace MDFN_IEN_SS
{
CartInfo Cart;
const std::vector<InputPortInfoStruct> SMPC_PortInfo;
void (*const DSP_GenFuncTable[2][16][8][8][4])(void) = {};
void (*const DSP_MVIFuncTable[2][16][128])(void) = {};
void (*const DSP_JMPFuncTable[2][128])(void) = {};
void (*const DSP_MiscFuncTable[2][4])(void) = {};
namespace VDP1
{
uint16 VRAM[0x40000];
uint16 FB[2][0x20000];
}
}

using namespace MDFN_IEN_SS;

// The devices behind the SCU / CS0 decode. Empty on purpose: the count is decided in BSC_BusRead/Write before
// any of them runs. The cartridge (CS0 / CS1 / CS2 function tables in Cart) is left null and never addressed.
namespace MDFN_IEN_SS
{
namespace VDP1
{
MDFN_FASTCALL void Write_CheckDrawSlowdown(uint32 A, sscpu_timestamp_t time_thing) {}
MDFN_FASTCALL void Read_CheckDrawSlowdown(uint32 A, sscpu_timestamp_t time_thing) {}
MDFN_FASTCALL void Write8_DB(uint32 A, uint16 DB) {}
MDFN_FASTCALL void Write16_DB(uint32 A, uint16 DB) {}
MDFN_FASTCALL uint16 Read16_DB(uint32 A) { return 0; }
}
namespace VDP2
{
uint32 Write8_DB(uint32 A, uint16 DB) { return 0; }
uint32 Write16_DB(uint32 A, uint16 DB) { return 0; }
uint16 Read16_DB(uint32 A) { return 0; }
}
uint16 SOUND_Read16(uint32 A) { return 0; }
void SOUND_Write8(uint32 A, uint8 V) {}
void SOUND_Write16(uint32 A, uint16 V) {}
void CDB_Write_DBM(uint32 offset, uint16 DB, uint16 mask) {}
uint16 CDB_Read(uint32 offset) { return 0; }
void SMPC_Write(const sscpu_timestamp_t timestamp, uint8 A, uint8 V) {}
uint8 SMPC_Read(const sscpu_timestamp_t timestamp, uint8 A) { return 0; }
}

namespace
{
int gFail = 0;
void Expect(const char* what, long long got, long long want)
{
    if (got != want) { std::printf("FAIL: %s: got %lld, want %lld\n", what, got, want); ++gFail; }
    else std::printf("  ok  %s = %lld\n", what, got);
}

typedef unsigned long long u64;
u64 Rd(int dev) { return SeSsPerfC[SE_PERF_BUS_ACCESS + dev * 2]; }
u64 Wr(int dev) { return SeSsPerfC[SE_PERF_BUS_ACCESS + dev * 2 + 1]; }
u64 XRd(int x) { return SeSsPerfC[SE_PERF_BUS_ACCESS_X + x * 2]; }
u64 XWr(int x) { return SeSsPerfC[SE_PERF_BUS_ACCESS_X + x * 2 + 1]; }
u64 AllBus()
{
    u64 s = 0;
    for (int i = 0; i < SE_PERF_DEV_COUNT * 2; i++) s += SeSsPerfC[SE_PERF_BUS_ACCESS + i];
    for (int i = 0; i < 4; i++) s += SeSsPerfC[SE_PERF_BUS_ACCESS_X + i];
    return s;
}
u64 AllReads()
{
    u64 s = 0;
    for (int i = 0; i < SE_PERF_DEV_COUNT; i++) s += Rd(i);
    return s + XRd(0) + XRd(1);
}
u64 AllWrites() { return AllBus() - AllReads(); }
void Clear() { std::memset(SeSsPerfC, 0, sizeof(unsigned long long) * SE_PERF_COUNT); }

void MapFast()
{
    for (uint64 A = 0; A < (1ULL << 32); A += (1U << SH7095_EXT_MAP_GRAN_BITS))
        SH7095_FastMap[A >> SH7095_EXT_MAP_GRAN_BITS] = (uintptr_t)fmap_dummy - A;
    SS_SetPhysMemMap(0x00000000, 0x000FFFFF, BIOSROM, sizeof(BIOSROM), false);
    SS_SetPhysMemMap(0x00200000, 0x002FFFFF, WorkRAML, sizeof(WorkRAML), true);
    SS_SetPhysMemMap(0x06000000, 0x07FFFFFF, WorkRAMH, sizeof(WorkRAMH), true);
}

// A fresh master: reset, cache purged and empty, the given CCR, the given instruction-fetch mode.
void Fresh(bool emuICache, unsigned ccr)
{
    CPU[0].timestamp = 0;
    SH7095_mem_timestamp = 0;
    CPU[0].Init(emuICache, false);
    CPU[0].Reset(true, false);
    CPU[0].SetCCR(SH7095::CCR_CP);     // the purge; also leaves CE clear
    CPU[0].SetCCR((uint8)ccr);
    Clear();
}

uint32 R32(uint32 A) { return CPU[0].MRFP32[A >> 29](A); }
uint16 R16(uint32 A) { return CPU[0].MRFP16[A >> 29](A); }
uint8 R8(uint32 A) { return CPU[0].MRFP8[A >> 29](A); }
uint32 RI(uint32 A) { return CPU[0].MRFPI[A >> 29](A); }          // instruction read
uint32 R32I(uint32 A) { return CPU[0].MRFP32_I[A >> 29](A); }     // PC-relative data read (IsInstr = -1)
void W32(uint32 A, uint32 V) { CPU[0].MWFP32[A >> 29](A, V); }
void W16(uint32 A, uint16 V) { CPU[0].MWFP16[A >> 29](A, (uint16)V); }
void W8(uint32 A, uint8 V) { CPU[0].MWFP8[A >> 29](A, (uint8)V); }

enum : uint8 { CE = SH7095::CCR_CE, ID = SH7095::CCR_ID, OD = SH7095::CCR_OD, CP = SH7095::CCR_CP };

const uint32 BIOS_A = 0x00000100, WRAML_A = 0x00200000, WRAMH_A = 0x06000000;
const uint32 VDP2_A = 0x05E00000, VDP1_A = 0x05C00000, SCSP_A = 0x05A00000;

unsigned ValidLines()
{
    unsigned n = 0;
    for (unsigned e = 0; e < 64; e++)
        for (unsigned w = 0; w < 4; w++)
            if (!(CPU[0].Cache[e].Tag[w] & 1)) ++n;
    return n;
}

//
// Cache
//
void CacheTests()
{
    std::printf("cache: data reads (instruction fetch mode off)\n");
    for (int mode = 0; mode < 2; mode++)
    {
        const bool eic = mode;
        std::printf(" [data-cache behaviour is the same with the instruction cache %s]\n", eic ? "emulated" : "not emulated");
        Fresh(eic, 0);
        R32(WRAMH_A); R16(WRAMH_A + 2); R8(WRAMH_A + 5);
        Expect("C1 CE=0: three reads (32/16/8-bit) = 3 reads on WRAM_H", Rd(SE_PERF_DEV_WRAM_H), 3);
        Expect("C1 ... and 3 requests in all", AllBus(), 3);
        Fresh(eic, 0);
        R32(BIOS_A); R16(BIOS_A + 2); R8(BIOS_A + 5);
        Expect("C1 CE=0: three BIOS reads (16-bit CS0, 32-bit access splits) = 3", Rd(SE_PERF_DEV_BIOS), 3);
        Fresh(eic, 0);
        R32(0x20000000 | WRAMH_A);
        Expect("C6 region-1 mirror read is the WRAM_H bucket, 1 read", Rd(SE_PERF_DEV_WRAM_H), 1);

        for (int sz = 8; sz <= 32; sz *= 2)
        {
            char nm[96];
            Fresh(eic, CE);
            if (sz == 8) R8(WRAMH_A + 0x21); else if (sz == 16) R16(WRAMH_A + 0x22); else R32(WRAMH_A + 0x24);
            std::snprintf(nm, sizeof nm, "C2 CE=1 miss, %d-bit read: 4 reads on WRAM_H", sz);
            Expect(nm, Rd(SE_PERF_DEV_WRAM_H), 4);
            Expect("C2 ... nothing else", AllBus(), 4);
            Expect("C2 ... one line is valid", ValidLines(), 1);
            Clear();
            R8(WRAMH_A + 0x20); R16(WRAMH_A + 0x2E); R32(WRAMH_A + 0x28);
            Expect("C3 hits anywhere in the filled line = 0", AllBus(), 0);
        }
        Fresh(eic, CE);
        R32(WRAMH_A); R32(WRAMH_A + 0x10); R32(WRAMH_A + 0x20);
        Expect("C2 three different lines = 12 reads", Rd(SE_PERF_DEV_WRAM_H), 12);
        Expect("C2 requests = 4 x valid lines", AllBus(), 4 * ValidLines());

        Fresh(eic, CE);
        R32(BIOS_A);
        Expect("C11 fill from the 16-bit BIOS = 4 requests", Rd(SE_PERF_DEV_BIOS), 4);
        Expect("C11 ... nothing else", AllBus(), 4);
        Fresh(eic, CE);
        R32(WRAML_A);
        Expect("C12 fill from low WRAM = 4", Rd(SE_PERF_DEV_WRAM_L), 4);
        Fresh(eic, CE);
        R32(VDP2_A);
        Expect("C12 fill from VDP2 (region 0, cached) = 4", Rd(SE_PERF_DEV_VDP2), 4);
        Expect("C12 ... nothing else", AllBus(), 4);

        Fresh(eic, CE);
        R32(0x20000000 | WRAMH_A); R32(0x20000000 | WRAMH_A);
        Expect("C6 cache-through read twice = 2, never a fill", Rd(SE_PERF_DEV_WRAM_H), 2);
        Expect("C6 ... no valid line", ValidLines(), 0);
        Fresh(eic, CE);
        R32(WRAMH_A);
        Clear();
        R32(0x20000000 | WRAMH_A);
        Expect("C6 cache-through read of a cached line still reaches the bus (1)", Rd(SE_PERF_DEV_WRAM_H), 1);

        std::printf(" replacement disabled\n");
        Fresh(eic, CE | OD);
        R32(WRAMH_A); R8(WRAMH_A + 1);
        Expect("C4 OD, data miss twice = 2 single reads", Rd(SE_PERF_DEV_WRAM_H), 2);
        Expect("C4 ... no line filled", ValidLines(), 0);
        Fresh(eic, CE);
        R32(WRAMH_A);
        CPU[0].SetCCR(CE | OD);       // lines survive a CCR change that is not a purge
        Clear();
        R32(WRAMH_A + 4);
        Expect("C4 OD set but the line is already there: hit = 0", AllBus(), 0);
        Fresh(eic, CE | ID);
        R32(WRAMH_A);
        Expect("C4 ID does not stop a DATA fill: 4", Rd(SE_PERF_DEV_WRAM_H), 4);
        Fresh(eic, CE | ID);
        R32I(WRAMH_A + 0x40);
        Expect("C5 ID set, PC-relative (IsInstr=-1) read, miss = 1", Rd(SE_PERF_DEV_WRAM_H), 1);
        Fresh(eic, CE | OD);
        R32I(WRAMH_A + 0x40);
        Expect("C5 OD set only: the PC-relative read fills = 4", Rd(SE_PERF_DEV_WRAM_H), 4);

        std::printf(" purge, refill\n");
        Fresh(eic, CE);
        R32(WRAMH_A);
        Clear();
        CPU[0].SetCCR(CE | CP);
        Expect("C7 the purge itself touches no bus", AllBus(), 0);
        Expect("C7 ... and invalidates every line", ValidLines(), 0);
        R32(WRAMH_A);
        Expect("C7 the first read after the purge refills = 4", Rd(SE_PERF_DEV_WRAM_H), 4);
        R32(WRAMH_A + 8);
        Expect("C7 the next read of the line hits = still 4", AllBus(), 4);
        Fresh(eic, CE);
        R32(WRAMH_A);
        Clear();
        R32(0x40000000 | WRAMH_A);              // region 2: associative purge
        Expect("C8 associative purge by read = 0 requests", AllBus(), 0);
        R32(WRAMH_A);
        Expect("C8 ... and the line must be refilled = 4", Rd(SE_PERF_DEV_WRAM_H), 4);
        Clear();
        W32(0xA0000000 | WRAMH_A, 0);           // region 5: associative purge by write
        Expect("C8 associative purge by write (region 5) = 0 requests", AllBus(), 0);
        R32(WRAMH_A);
        Expect("C8 ... refill = 4", Rd(SE_PERF_DEV_WRAM_H), 4);

        std::printf(" array and on-chip spaces\n");
        Clear();
        R32(0x60000010); W32(0x60000010, 0); R32(0xC0000010); W32(0xC0000010, 0); R32(0x80000010); W32(0x80000010, 0);
        Expect("C9 address array / data array reads and writes = 0", AllBus(), 0);
        R32(0xFFFFFF80); W32(0xFFFFFF80, 0x06000000); R16(0xFFFFFE92);
        Expect("C9 on-chip register reads and writes = 0", AllBus(), 0);

        std::printf(" writes\n");
        Fresh(eic, 0);
        W32(WRAMH_A, 1); W16(WRAMH_A + 4, 2); W8(WRAMH_A + 6, 3);
        Expect("C10 CE=0: three writes = 3 on WRAM_H", Wr(SE_PERF_DEV_WRAM_H), 3);
        Expect("C10 ... no reads", AllReads(), 0);
        Fresh(eic, CE);
        W32(WRAMH_A, 1); W8(WRAMH_A + 4, 2);
        Expect("C10 CE=1 write to an absent line: 2 writes, no allocate (0 reads)", Wr(SE_PERF_DEV_WRAM_H) * 100 + AllReads(), 200);
        R32(WRAMH_A);
        Clear();
        W32(WRAMH_A + 8, 5);
        Expect("C10 CE=1 write to a present line is still written through = 1", Wr(SE_PERF_DEV_WRAM_H), 1);
        Expect("C10 ... and read back hits (0 reads)", (R32(WRAMH_A + 8) == 5) * 100 + AllReads(), 100);
        Fresh(eic, CE);
        W32(0x20000000 | WRAML_A, 1); W16(WRAML_A + 4, 7);
        Expect("C10 write via the region-1 mirror and via region 0 both = 2 on WRAM_L", Wr(SE_PERF_DEV_WRAM_L), 2);
        Fresh(eic, 0);
        W32(WRAMH_A, 1); W32(VDP1_A, 1); W16(SCSP_A, 1);
        Expect("C10 writes reach VDP1 and SCSP buckets", Wr(SE_PERF_DEV_VDP1) * 10 + Wr(SE_PERF_DEV_SCSP), 11);
    }
}

//
// Instruction fetch through the real Step()
//
void PrimeNops(uint32 pc)
{
    for (unsigned i = 0; i < 0x400 / 2; i++)
        ne16_wbo_be<uint16>(WorkRAMH, (pc & 0xFFFFF) + i * 2, 0x0009);     // NOP
    CPU[0].PC = pc;
    CPU[0].EPending = 0;
    CPU[0].Pipe_ID = 0x0009 | ((uint32)InstrDecodeTab[0x0009] << 24);
    CPU[0].Pipe_IF = 0x0009;
    CPU[0].IBuffer = 0x00090009;
    CPU[0].timestamp = 0;
    CPU[0].MA_until = 0;
}

template<bool EIC>
void RunSteps(unsigned n)
{
    for (unsigned i = 0; i < n; i++) CPU[0].Step<0, EIC, false>();
}

void FetchTests()
{
    std::printf("instruction fetch through the real SH7095::Step\n");
    Fresh(false, CE);
    PrimeNops(WRAMH_A);
    RunSteps<false>(64);
    Expect("C13 mode OFF, CE=1, 64 NOPs: 0 requests (fast map)", AllBus(), 0);
    Expect("C13 ... no line filled", ValidLines(), 0);
    Fresh(false, 0);
    PrimeNops(WRAMH_A);
    RunSteps<false>(64);
    Expect("C13 mode OFF, CE=0, 64 NOPs: 0 requests", AllBus(), 0);

    for (unsigned n = 1; n <= 24; n += (n < 8 ? 7 : 8))
    {
        char nm[96];
        Fresh(true, CE);
        PrimeNops(WRAMH_A);
        RunSteps<true>(n);
        // halfwords fetched at WRAMH_A + 2*i, i < n (+ the primed ones); a 32-bit word is read when !(PC & 2)
        const unsigned lines = ValidLines();
        std::snprintf(nm, sizeof nm, "C14 mode ON, CE=1, %u NOPs: requests = 4 x filled lines (%u)", n, lines);
        Expect(nm, AllBus(), 4 * lines);
        Expect("C14 ... all on WRAM_H reads", Rd(SE_PERF_DEV_WRAM_H), 4 * lines);
        Expect("C14 ... lines filled = ceil(n/8): n halfword fetches span n*2 bytes", lines, (n + 7) / 8);
        if (n == 1)
            Expect("C14 one NOP: exactly one fill", lines, 1);
        if (n <= 4)
            Expect("C14 four or fewer steps stay inside the first 16 bytes", lines, 1);
        }
    Fresh(true, CE | ID);
    PrimeNops(WRAMH_A);
    RunSteps<true>(8);
    Expect("C5 mode ON, ID set: no line filled", ValidLines(), 0);
    Expect("C5 ... 8 steps fetch 4 words: 4 uncached reads, not a multiple-of-4 fill", AllBus(), 4);
    Expect("C5 ... and all of them are 1 per fetched word (reads == bus total)", Rd(SE_PERF_DEV_WRAM_H), (long long)AllBus());
    const u64 withId = AllBus();
    Fresh(true, 0);
    PrimeNops(WRAMH_A);
    RunSteps<true>(8);
    Expect("C15 mode ON, CE=0: per-word reads equal the ID-set count", AllBus(), (long long)withId);
    Expect("C15 ... which is one read per 32-bit word: 8 steps = 4 words", AllBus(), 4);

    // direct instruction reads, exact
    Fresh(true, CE);
    RI(WRAMH_A);
    Expect("C14 one MemReadInstr miss = 4", Rd(SE_PERF_DEV_WRAM_H), 4);
    RI(WRAMH_A + 4); RI(WRAMH_A + 12);
    Expect("C14 two more in the same line hit = still 4", AllBus(), 4);
    Fresh(true, CE | ID);
    RI(WRAMH_A); RI(WRAMH_A + 4);
    Expect("C5 ID set: two instruction reads = 2", Rd(SE_PERF_DEV_WRAM_H), 2);
    Fresh(true, 0);
    RI(WRAMH_A); RI(WRAMH_A + 4); RI(WRAMH_A);
    Expect("C15 CE=0: three instruction reads = 3", Rd(SE_PERF_DEV_WRAM_H), 3);
}

//
// DMAC
//
struct Dma
{
    uint32 sar, dar, tcr;
    unsigned ts, sm = 1, dm = 1;      // ts: 0 8-bit .. 3 16-byte; modes: 1 increment
    bool burst = false;
};

void ChSetup(unsigned ch, const Dma& d)
{
    const uint32 base = 0xFFFFFF80 + ch * 0x10;
    CPU[0].MWFP32[7](base + 0x0, d.sar);
    CPU[0].MWFP32[7](base + 0x4, d.dar);
    CPU[0].MWFP32[7](base + 0x8, d.tcr);
    CPU[0].MWFP32[7](base + 0xC, 1 | (d.burst ? 0x10 : 0) | 0x200 | (d.ts << 10) | (d.sm << 12) | (d.dm << 14));
}

void DmaRun(unsigned drivePr = 0)
{
    CPU[0].MWFP32[7](0xFFFFFFB0, 1 | (drivePr ? 8 : 0));       // DMAOR: DME (and round-robin)
    CPU[0].DMA_Update(0x3FFFFFFF);
}

void DmaFresh()
{
    Fresh(true, 0);
    for (unsigned i = 0; i < 0x40000; i++) ne16_wbo_be<uint16>(WorkRAMH, i * 2, 0x1111);
}

void DmaCase(const char* id, const char* what, const Dma& d, uint32 srcDevR, uint32 dstDevW, unsigned wantReads, unsigned wantWrites,
             bool srcX = false, bool dstX = false)
{
    char nm[160];
    DmaFresh();
    ChSetup(0, d);
    DmaRun();
    const u64 r = srcX ? XRd(srcDevR) : Rd(srcDevR);
    const u64 w = dstX ? XWr(dstDevW) : Wr(dstDevW);
    std::snprintf(nm, sizeof nm, "%s %s: reads on source", id, what);
    Expect(nm, r, wantReads);
    std::snprintf(nm, sizeof nm, "%s %s: writes on destination", id, what);
    Expect(nm, w, wantWrites);
    std::snprintf(nm, sizeof nm, "%s %s: nothing else (reads+writes = total)", id, what);
    Expect(nm, AllBus(), (long long)(wantReads + wantWrites));
    if (srcX == dstX && srcDevR == dstDevW) { /* same bucket: the check above covers both */ }
}

void DmaTests()
{
    std::printf("DMAC: unit sizes, WRAM_H -> WRAM_L\n");
    const char* names[4] = {"8-bit", "16-bit", "32-bit", "16-byte"};
    for (unsigned ts = 0; ts < 4; ts++)
    {
        for (unsigned burst = 0; burst < 2; burst++)
        {
            Dma d;
            d.ts = ts; d.sar = WRAMH_A; d.dar = WRAML_A; d.tcr = 8; d.burst = burst;
            char w[64];
            std::snprintf(w, sizeof w, "%s units, K=8, %s", names[ts], burst ? "burst" : "cycle-steal");
            DmaCase(ts == 3 ? "D4" : (ts == 0 ? "D1" : ts == 1 ? "D2" : "D3"), w, d, SE_PERF_DEV_WRAM_H, SE_PERF_DEV_WRAM_L, 8, 8);
        }
    }
    {
        Dma d; d.ts = 2; d.sar = WRAMH_A; d.dar = WRAML_A; d.tcr = 8; d.burst = true;
        DmaFresh(); ChSetup(0, d);
        CPU[0].MWFP32[7](0xFFFFFFB0, 1);
        Expect("D5 burst channel enabled: DMA_InBurst()", CPU[0].DMA_InBurst(), 1);
        DmaFresh(); d.burst = false; ChSetup(0, d);
        CPU[0].MWFP32[7](0xFFFFFFB0, 1);
        Expect("D5 cycle-steal channel enabled: DMA_InBurst() is false", CPU[0].DMA_InBurst(), 0);
    }
    std::printf("DMAC: K not a multiple of 4 in 16-byte mode, K = 1 in the others\n");
    for (unsigned K = 1; K <= 7; K += 3)
    {
        Dma d; d.ts = 3; d.sar = WRAMH_A; d.dar = WRAML_A; d.tcr = K;
        char w[64]; std::snprintf(w, sizeof w, "16-byte, K=%u", K);
        DmaCase("D4", w, d, SE_PERF_DEV_WRAM_H, SE_PERF_DEV_WRAM_L, 4 * ((K + 3) / 4), K);
    }
    {
        Dma d; d.ts = 2; d.sar = WRAMH_A; d.dar = WRAML_A; d.tcr = 1;
        DmaCase("D3", "32-bit, K=1", d, SE_PERF_DEV_WRAM_H, SE_PERF_DEV_WRAM_L, 1, 1);
    }

    std::printf("DMAC: cross-device\n");
    { Dma d; d.ts = 2; d.sar = BIOS_A; d.dar = WRAMH_A + 0x1000; d.tcr = 5; DmaCase("D3", "BIOS -> WRAM_H 32-bit", d, SE_PERF_DEV_BIOS, SE_PERF_DEV_WRAM_H, 5, 5); }
    { Dma d; d.ts = 1; d.sar = BIOS_A; d.dar = WRAML_A; d.tcr = 6; DmaCase("D2", "BIOS -> WRAM_L 16-bit", d, SE_PERF_DEV_BIOS, SE_PERF_DEV_WRAM_L, 6, 6); }
    { Dma d; d.ts = 2; d.sar = WRAMH_A; d.dar = VDP1_A; d.tcr = 6; d.dm = 1; DmaCase("D3", "WRAM_H -> VDP1 32-bit", d, SE_PERF_DEV_WRAM_H, SE_PERF_DEV_VDP1, 6, 6); }
    { Dma d; d.ts = 1; d.sar = WRAMH_A; d.dar = VDP2_A; d.tcr = 6; DmaCase("D2", "WRAM_H -> VDP2 16-bit", d, SE_PERF_DEV_WRAM_H, SE_PERF_DEV_VDP2, 6, 6); }
    { Dma d; d.ts = 0; d.sar = WRAMH_A; d.dar = SCSP_A; d.tcr = 6; DmaCase("D1", "WRAM_H -> SCSP 8-bit", d, SE_PERF_DEV_WRAM_H, SE_PERF_DEV_SCSP, 6, 6); }
    { Dma d; d.ts = 3; d.sar = VDP2_A; d.dar = WRAMH_A; d.tcr = 8; DmaCase("D4", "VDP2 -> WRAM_H 16-byte", d, SE_PERF_DEV_VDP2, SE_PERF_DEV_WRAM_H, 8, 8); }
    { Dma d; d.ts = 2; d.sar = WRAMH_A; d.dar = 0x26000000 + 0x2000; d.tcr = 4; DmaCase("D6", "same device through the 0x26 mirror (masked to 27 bits)", d, SE_PERF_DEV_WRAM_H, SE_PERF_DEV_WRAM_H, 4, 4); }
    {
        Dma d; d.ts = 2; d.sar = WRAMH_A; d.dar = WRAMH_A + 0x4000; d.tcr = 4;
        DmaFresh(); ChSetup(0, d); DmaRun();
        Expect("D6 same device: 4 reads", Rd(SE_PERF_DEV_WRAM_H), 4);
        Expect("D6 same device: 4 writes", Wr(SE_PERF_DEV_WRAM_H), 4);
    }

    std::printf("DMAC: A-bus dummy and unmapped buckets\n");
    { Dma d; d.ts = 2; d.sar = WRAMH_A; d.dar = 0x05000000; d.tcr = 3; DmaCase("D7", "WRAM_H -> A-bus dummy", d, SE_PERF_DEV_WRAM_H, SE_PERF_XDEV_ABUS_DUMMY, 3, 3, false, true); }
    { Dma d; d.ts = 1; d.sar = 0x05700000; d.dar = WRAMH_A; d.tcr = 3; DmaCase("D7", "A-bus dummy -> WRAM_H", d, SE_PERF_XDEV_ABUS_DUMMY, SE_PERF_DEV_WRAM_H, 3, 3, true, false); }
    { Dma d; d.ts = 2; d.sar = WRAMH_A; d.dar = 0x05900000; d.tcr = 3; DmaCase("D7", "WRAM_H -> unmapped 0x05900000", d, SE_PERF_DEV_WRAM_H, SE_PERF_XDEV_UNMAPPED, 3, 3, false, true); }
    { Dma d; d.ts = 2; d.sar = 0x05D80000; d.dar = WRAMH_A; d.tcr = 3; DmaCase("D7", "unmapped 0x05D80000 -> WRAM_H", d, SE_PERF_XDEV_UNMAPPED, SE_PERF_DEV_WRAM_H, 3, 3, true, false); }
    { Dma d; d.ts = 0; d.sar = 0x05FC0000; d.dar = 0x05FF0000; d.tcr = 3; DmaCase("D7", "unmapped 0x05FC0000 -> unmapped 0x05FF0000 (both in the X bucket)", d, SE_PERF_XDEV_UNMAPPED, SE_PERF_XDEV_UNMAPPED, 3, 3, true, true); }
    {
        Dma d; d.ts = 2; d.sar = 0x05000000; d.dar = 0x05900000; d.tcr = 2;
        DmaFresh(); ChSetup(0, d); DmaRun();
        Expect("D7 dummy -> unmapped: dummy reads", XRd(SE_PERF_XDEV_ABUS_DUMMY), 2);
        Expect("D7 dummy -> unmapped: unmapped writes", XWr(SE_PERF_XDEV_UNMAPPED), 2);
        Expect("D7 the device buckets stay 0 (only X slots count)", AllBus(), 4);
    }

    std::printf("DMAC: address error, disabled channel, two channels\n");
    {
        Dma d; d.ts = 2; d.sar = WRAMH_A + 2; d.dar = WRAML_A; d.tcr = 5;
        DmaCase("D8", "32-bit, odd-word SAR: only the unit in flight", d, SE_PERF_DEV_WRAM_H, SE_PERF_DEV_WRAM_L, 1, 1);
        Dma e; e.ts = 1; e.sar = WRAMH_A + 1; e.dar = WRAML_A; e.tcr = 5;
        DmaCase("D8", "16-bit, odd SAR: only the unit in flight", e, SE_PERF_DEV_WRAM_H, SE_PERF_DEV_WRAM_L, 1, 1);
        Dma f; f.ts = 3; f.sar = WRAMH_A + 2; f.dar = WRAML_A; f.tcr = 8;
        DmaCase("D8", "16-byte, SAR misaligned: the batch of 4+4 happens, then AE stops it", f, SE_PERF_DEV_WRAM_H, SE_PERF_DEV_WRAM_L, 4, 4);
    }
    {
        Dma d; d.ts = 2; d.sar = WRAMH_A; d.dar = WRAML_A; d.tcr = 8;
        DmaFresh(); ChSetup(0, d);
        CPU[0].DMA_Update(1000);
        Expect("D9 DMAOR.DME clear: 0 requests", AllBus(), 0);
        CPU[0].MWFP32[7](0xFFFFFFB0, 1);
        CPU[0].DMA_Update(0x3FFFFFFF);
        const u64 first = AllBus();
        Expect("D9 enabled: 8 + 8", first, 16);
        CPU[0].DMA_Update(0x3FFFFFFF);
        Expect("D9 finished (TE set): further updates add nothing", AllBus(), 16);
    }
    {
        Dma a; a.ts = 2; a.sar = WRAMH_A; a.dar = WRAML_A; a.tcr = 6;
        Dma b; b.ts = 1; b.sar = BIOS_A; b.dar = WRAMH_A + 0x8000; b.tcr = 10;
        for (unsigned rr = 0; rr < 2; rr++)
        {
            DmaFresh(); ChSetup(0, a); ChSetup(1, b); DmaRun(rr);
            Expect(rr ? "D10 round-robin: WRAM_H reads (ch0) = 6" : "D10 priority: WRAM_H reads (ch0) = 6", Rd(SE_PERF_DEV_WRAM_H), 6);
            Expect("D10 ... WRAM_L writes (ch0) = 6", Wr(SE_PERF_DEV_WRAM_L), 6);
            Expect("D10 ... BIOS reads (ch1) = 10", Rd(SE_PERF_DEV_BIOS), 10);
            Expect("D10 ... WRAM_H writes (ch1) = 10", Wr(SE_PERF_DEV_WRAM_H), 10);
            Expect("D10 ... total = 32", AllBus(), 32);
        }
    }
}
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    InitEvents();
    for (unsigned i = 0; i < SS_EVENT__COUNT; i++)
        if (i != SS_EVENT__SYNFIRST && i != SS_EVENT__SYNLAST)
            SS_SetEventNT(&events[i], SS_EVENT_DISABLED_TS);
    next_event_ts = events[SS_EVENT__SYNFIRST].next->event_time;
    CPU[1].timestamp = SS_EVENT_DISABLED_TS;      // the slave stays off: RunSlaveUntil returns at once
    MapFast();
    std::memset(BIOSROM, 0x22, sizeof(BIOSROM));
    SeMednafenStateLoaded(0);

    CacheTests();
    FetchTests();
    DmaTests();

    std::printf("%s\n", gFail ? "FAILED" : "PASSED");
    return gFail ? 1 : 0;
}
