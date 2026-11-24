// F5 component test: the REAL patched 68000 core (hw_cpu/m68k) running real STOP / interrupt wake /
// external halt, and the REAL patched sound.cpp (SOUND_Update, SoundCPU, the SCSP) reaching a real
// faulting sound-bus read whose handler MDFN_longjmp()s back into SOUND_Update past everything a slice
// did after SoundCPU.Run(). Nothing about the CPU or SOUND_Update is mocked; only the SH-2 side's
// memory map, the SCU interrupt line, the CD-DA source and the resampler are stubs.
//
// Built and run by Integration/Mednafen/check_component.py (defines SE_SOUND_CPP).
#include SE_SOUND_CPP

#include <cstdio>
#include <cstring>

extern "C" { unsigned long long SeSsPerfC[SE_PERF_COUNT]; unsigned int SeSsCacheMode; }

namespace MDFN_IEN_SS
{
void SS_SetPhysMemMap(uint32, uint32, uint16*, uint32, bool) {}
void SCU_SetInt(unsigned, bool) {}
void CDB_GetCDDA(uint16* o) { o[0] = o[1] = 0; }
}

using namespace MDFN_IEN_SS;
using Mednafen::M68K;

namespace
{
int gFail = 0;
void Expect(const char* what, long long got, long long want)
{
    if (got != want) { std::printf("FAIL: %s: got %lld, want %lld\n", what, got, want); ++gFail; }
    else std::printf("  ok  %s = %lld\n", what, got);
}

// ---- a bare 68000 with 64 KiB of big-endian test memory ----
struct Bare
{
    uint16 mem[0x8000];
    M68K cpu;
    static Bare* cur;
    // The core charges no time of its own for a bus access: the bus handlers do (as sound.cpp's do).
    static uint16 Instr(uint32 A) { cur->cpu.timestamp += 4; return cur->mem[(A >> 1) & 0x7FFF]; }
    static uint16 R16(uint32 A) { cur->cpu.timestamp += 4; return cur->mem[(A >> 1) & 0x7FFF]; }
    static uint8 R8(uint32 A) { cur->cpu.timestamp += 4; const uint16 w = cur->mem[(A >> 1) & 0x7FFF]; return (A & 1) ? (w & 0xFF) : (w >> 8); }
    static void W16(uint32 A, uint16 V) { cur->cpu.timestamp += 4; cur->mem[(A >> 1) & 0x7FFF] = V; }
    static void W8(uint32 A, uint8 V)
    {
        cur->cpu.timestamp += 4;
        uint16& w = cur->mem[(A >> 1) & 0x7FFF];
        w = (A & 1) ? ((w & 0xFF00) | V) : ((w & 0x00FF) | (V << 8));
    }
    static void RMW(uint32, uint8 (*)(M68K*, uint8)) {}
    static unsigned IntAck(uint8) { return M68K::BUS_INT_ACK_AUTO; }
    Bare() : cpu(true)
    {
        std::memset(mem, 0, sizeof mem);
        cpu.BusReadInstr = Instr; cpu.BusRead16 = R16; cpu.BusRead8 = R8;
        cpu.BusWrite16 = W16; cpu.BusWrite8 = W8; cpu.BusIntAck = IntAck;
        // vectors: SSP 0x1000, PC 0x100; level-4 autovector (vector 28, at 0x70) -> 0x200
        mem[0] = 0; mem[1] = 0x1000; mem[2] = 0; mem[3] = 0x100;
        mem[0x70 / 2] = 0; mem[0x70 / 2 + 1] = 0x200;
        // 0x100: NOP NOP NOP STOP #0x2000 ; BRA.S * after the STOP
        mem[0x100 / 2 + 0] = 0x4E71; mem[0x100 / 2 + 1] = 0x4E71; mem[0x100 / 2 + 2] = 0x4E71;
        mem[0x100 / 2 + 3] = 0x4E72; mem[0x100 / 2 + 4] = 0x2000; mem[0x100 / 2 + 5] = 0x60FE;
        // 0x200: NOP NOP BRA.S *
        mem[0x200 / 2 + 0] = 0x4E71; mem[0x200 / 2 + 1] = 0x4E71; mem[0x200 / 2 + 2] = 0x60FE;
        // 0x300: a program that never stops: NOP ; BRA.S -4
        mem[0x300 / 2 + 0] = 0x4E71; mem[0x300 / 2 + 1] = 0x60FC;
        cpu.Reset(true);
    }
};
Bare* Bare::cur;

// Independent oracle: single-step a twin from the same program and add up the time of exactly the
// steps that began stopped / halted with no interrupt to take. It never reads IdleCycleCount.
struct Oracle { long long total = 0, idle = 0; };
Oracle RunOracle(Bare& b, int32 until)
{
    Oracle o;
    Bare::cur = &b;
    const int32 t0 = b.cpu.timestamp;
    while (b.cpu.timestamp < until)
    {
        const uint32 x = b.cpu.XPending;
        const bool wouldIdle =
            (x & (M68K::XPENDING_MASK_ERRORHALTED | M68K::XPENDING_MASK_DTACKHALTED | M68K::XPENDING_MASK_EXTHALTED)) ||
            ((x & M68K::XPENDING_MASK_STOPPED) &&
             !(x & (M68K::XPENDING_MASK_RESET | M68K::XPENDING_MASK_ADDRESS | M68K::XPENDING_MASK_BUS |
                    M68K::XPENDING_MASK_INT | M68K::XPENDING_MASK_NMI)));
        const int32 before = b.cpu.timestamp;
        b.cpu.Step();
        if (wouldIdle) o.idle += b.cpu.timestamp - before;
    }
    o.total = b.cpu.timestamp - t0;
    return o;
}

// ---- the Saturn sound path ----
static uint32 gFaultTs;
static bool gFaulted;
static uint16 (MDFN_FASTCALL *gRealRead16)(uint32);
static uint16 MDFN_FASTCALL FaultSpy16(uint32 A)
{
    if (A & 0xE00000) { gFaultTs = SoundCPU.timestamp; gFaulted = true; }   // the handler adds 4, then longjmps
    return gRealRead16(A);
}
void PokeWord(uint32 a, uint16 w) { SOUND_PokeRAM(a, w >> 8); SOUND_PokeRAM(a + 1, w & 0xFF); }
}

int main()
{
    std::printf("bare 68000: STOP partway through a slice\n");
    {
        Bare a, twin;
        Bare::cur = &a;
        const int32 end = 2000;
        a.cpu.Run(end);
        const Oracle o = RunOracle(twin, end);
        Expect("total cycles", a.cpu.timestamp, twin.cpu.timestamp);
        Expect("idle cycles (oracle)", a.cpu.IdleCycleCount, o.idle);
        Expect("idle is more than nothing and less than everything", o.idle > 0 && o.idle < o.total, 1);
        Expect("executed = total - idle", a.cpu.timestamp - (long long)a.cpu.IdleCycleCount, o.total - o.idle);

        std::printf("bare 68000: an interrupt wakes it from STOP; the wake step is not idle\n");
        const uint32 idleBefore = a.cpu.IdleCycleCount;
        const int32 tsBefore = a.cpu.timestamp;
        a.cpu.SetIPL(4);
        a.cpu.Run(tsBefore + 200);
        Expect("idle cycles added by the woken slice", a.cpu.IdleCycleCount - idleBefore, 0);
        Expect("the slice ran", a.cpu.timestamp >= tsBefore + 200, 1);
        Expect("no longer stopped", (a.cpu.XPending & M68K::XPENDING_MASK_STOPPED) ? 1 : 0, 0);

        std::printf("bare 68000: external halt, then release\n");
        a.cpu.SetExtHalted(true);
        const uint32 i1 = a.cpu.IdleCycleCount; const int32 t1 = a.cpu.timestamp;
        a.cpu.Run(t1 + 400);
        Expect("every cycle of the halted slice is idle", (long long)(a.cpu.IdleCycleCount - i1), (long long)(a.cpu.timestamp - t1));
        a.cpu.SetExtHalted(false);
        const uint32 i2 = a.cpu.IdleCycleCount; const int32 t2 = a.cpu.timestamp;
        a.cpu.Run(t2 + 100);
        Expect("none once released", a.cpu.IdleCycleCount - i2, 0);

        std::printf("bare 68000: halted by a missing DTACK\n");
        a.cpu.SignalDTACKHalted(0x300000);
        const uint32 i3 = a.cpu.IdleCycleCount; const int32 t3 = a.cpu.timestamp;
        a.cpu.Run(t3 + 80);
        Expect("every cycle idle", (long long)(a.cpu.IdleCycleCount - i3), (long long)(a.cpu.timestamp - t3));
    }

    std::printf("a second CPU instance is unaffected\n");
    {
        Bare busy, idleOne, untouched;
        Bare::cur = &busy;
        busy.cpu.PC = 0x300;   // runs a loop that never stops
        busy.cpu.XPending = 0; // out of reset; it only executes
        busy.cpu.Run(1000);
        Bare::cur = &idleOne;
        idleOne.cpu.Run(2000);
        Expect("the always-running CPU's idle count", busy.cpu.IdleCycleCount, 0);
        Expect("a CPU never run", untouched.cpu.IdleCycleCount, 0);
        Expect("the stopping CPU did idle", idleOne.cpu.IdleCycleCount > 0, 1);
    }

    std::printf("SOUND_Update: a real faulting bus read longjmps out of the slice\n");
    {
        SOUND_Init(false);
        SOUND_Reset(true);
        SoundCPU.SetExtHalted(false);
        SOUND_SetClockRatio(0x80000000u);   // 68000 cycles per SH-2 cycle, 0.32 fixed point
        // vectors: SSP 0x1000, PC 0x400; 0x400: NOP NOP NOP MOVE.W $300000,D0 (reads a nonexistent address)
        PokeWord(0, 0); PokeWord(2, 0x1000); PokeWord(4, 0); PokeWord(6, 0x400);
        PokeWord(0x400, 0x4E71); PokeWord(0x402, 0x4E71); PokeWord(0x404, 0x4E71);
        PokeWord(0x406, 0x3039); PokeWord(0x408, 0x0030); PokeWord(0x40A, 0x0000);
        gRealRead16 = SoundCPU.BusRead16;
        SoundCPU.BusRead16 = FaultSpy16;
        std::memset(SeSsPerfC, 0, sizeof SeSsPerfC);

        const int32 ts0 = SoundCPU.timestamp;
        SOUND_Update(8000);                              // 4000 68000 cycles of budget
        const int32 ts1 = SoundCPU.timestamp;
        Expect("the fault really happened", gFaulted ? 1 : 0, 1);
        Expect("SoundCPU is DTACK-halted", (SoundCPU.XPending & M68K::XPENDING_MASK_DTACKHALTED) ? 1 : 0, 1);
        const long long execExpected = (long long)gFaultTs + 4 - ts0;
        Expect("total = every cycle the CPU advanced", SeSsPerfC[SE_PERF_M68K_TOTAL_CYCLES], ts1 - ts0);
        Expect("executed = up to and including the faulting access", SeSsPerfC[SE_PERF_M68K_CYCLES], execExpected);
        Expect("idle = the rest of the slice, after the longjmp", SeSsPerfC[SE_PERF_M68K_IDLE_CYCLES], ts1 - gFaultTs - 4);
        Expect("executed + idle = total", SeSsPerfC[SE_PERF_M68K_CYCLES] + SeSsPerfC[SE_PERF_M68K_IDLE_CYCLES], SeSsPerfC[SE_PERF_M68K_TOTAL_CYCLES]);

        const int32 ts2a = SoundCPU.timestamp;
        std::memset(SeSsPerfC, 0, sizeof SeSsPerfC);
        SOUND_Update(16000);                             // still halted: all of it idle
        const int32 ts2 = SoundCPU.timestamp;
        Expect("next frame: total", SeSsPerfC[SE_PERF_M68K_TOTAL_CYCLES], ts2 - ts2a);
        Expect("next frame: all idle", SeSsPerfC[SE_PERF_M68K_IDLE_CYCLES], ts2 - ts2a);
        Expect("next frame: none executed", SeSsPerfC[SE_PERF_M68K_CYCLES], 0);
    }

    std::printf("%s\n", gFail ? "FAILED" : "PASSED");
    return gFail ? 1 : 0;
}
