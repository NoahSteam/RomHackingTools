// Hardware event log component test: the REAL patched Mednafen sources, with the hooks apply.py injected for
// the event log (DebuggerFeaturesPlan.md D5) running in them. Two programs are built from this one file:
//
//   SE_VDP1_CPP  the patched vdp1.cpp, driven as perf_vdp1_harness.cpp drives it: draw start, draw end
//                (END bit, and aborted by a frame-buffer swap), the frame-buffer swap and the v-blank erase.
//                The hooks call a capture that stands in for the glue; what is checked is what the HOOK
//                passes (kind, arguments), and that nothing at all is passed while the mask is clear.
//   SE_SS_CPP    the patched ss.cpp, i.e. the real SH-2 core, SCU and on-chip DMAC, driven as
//                perf_bus_harness.cpp drives them, calling the REAL glue (se_mednafen_glue.c, built wired so
//                its clock and PC come from the emulator's own accessors) whose sink is the capture below.
//                Checked: SH-2 exceptions (illegal instruction, TRAPA, an SCU interrupt, a CPU address
//                error, a DMA address error; on both the master's Step and the slave's resumable copy) with
//                vector, class, level, saved PC and handler -- the saved PC compared with what the core
//                itself pushed on the stack --; the on-chip DMAC start / end edges; SCU DMA start / end /
//                illegal, SCU interrupt delivery and mask write, and the v-blank edges.
//
// NOT covered here (their translation units need a whole machine): the CD block, SMPC and VDP2 display hooks.
// check_component.py checks that those are present, once, guarded by the mask, and that cdb.cpp / smpc.cpp /
// vdp2.cpp still compile with them (check_migration.py).
//
// Built and run by Integration/Mednafen/check_component.py (defines SE_VDP1_CPP or SE_SS_CPP).
#if defined(SE_VDP1_CPP)
#include SE_VDP1_CPP
#else
#include SE_SS_CPP
#endif

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
int gFail = 0;
void Expect(const char* what, long long got, long long want)
{
    if (got != want) { std::printf("FAIL: %s: got %lld (0x%llx), want %lld (0x%llx)\n", what, got, got, want, want); ++gFail; }
    else std::printf("  ok  %s = %lld\n", what, got);
}
}

#if defined(SE_VDP1_CPP)
// ------------------------------------------------------------------------------------------------------------
// VDP1
// ------------------------------------------------------------------------------------------------------------
struct Seen { unsigned kind; int cpu; unsigned pc, a[4]; };
static std::vector<Seen> gSeen;
extern "C"
{
unsigned long long SeSsPerfC[SE_PERF_COUNT];
unsigned int SeSsCacheMode;
unsigned int SeSsHwEvMask;
void SeMednafenHwEvent(unsigned kind, int cpu, unsigned pc, unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    Seen s = { kind, cpu, pc, { a0, a1, a2, a3 } };
    gSeen.push_back(s);
}
}

namespace MDFN_IEN_SS
{
uint32 ss_horrible_hacks;
int32 SH7095_mem_timestamp;
event_list_entry events[SS_EVENT__COUNT];
void SS_SetEventNT(event_list_entry*, const sscpu_timestamp_t) {}
void SS_SetPhysMemMap(uint32, uint32, uint16*, uint32, bool) {}
void SCU_SetInt(unsigned, bool) {}
bool SCU_CheckVDP1HaltKludge(void) { return false; }
namespace VDP2 { sscpu_timestamp_t Update(sscpu_timestamp_t t) { return t; } }
namespace VDP1
{
int32 CMD_NormalSprite(const uint16*) { return 400; }
int32 RESUME_Sprite(const uint16*) { return 0; }
int32 CMD_ScaledSprite(const uint16*) { return 0; }
int32 CMD_DistortedSprite(const uint16*) { return 0; }
int32 CMD_Polygon(const uint16*) { return 0; }
int32 CMD_Line(const uint16*) { return 0; }
int32 RESUME_Polygon(const uint16*) { return 0; }
int32 RESUME_Line(const uint16*) { return 0; }
}
}

using namespace MDFN_IEN_SS;

namespace
{
sscpu_timestamp_t gNow = 0;

void Fresh(unsigned mask)
{
    std::memset(SeSsPerfC, 0, sizeof SeSsPerfC);
    ss_horrible_hacks = 0;
    VDP1::Reset(true);
    gNow = 0;
    VDP1::lastts = 0;
    gSeen.clear();
    SeSsHwEvMask = mask;
}
void Cmd(unsigned n, uint16 w0) { VDP1::VRAM[n * 16] = w0; }
enum : uint16 { END = 0x8000, SPRITE = 0x0000 };
void Reg(unsigned r, uint16 v) { VDP1::Write16_DB(0x100000 + (r << 1), v); }
void Start() { Reg(0x2, 1); }                  // PTMR = 1: draw now
// One whole v-blank, as VDP2 would announce it: enter, first H-blank inside it (erase starts), leave, first
// H-blank outside it (erase runs, frame buffers swap).
void VBlank()
{
    VDP1::SetHBVB(gNow, false, true);
    VDP1::SetHBVB(gNow += 100, true, true);
    VDP1::SetHBVB(gNow += 100, false, false);
    VDP1::SetHBVB(gNow += 100000, true, false);
}
void ExpectEv(const char* what, size_t i, unsigned kind, unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    char nm[160];
    if (i >= gSeen.size()) { std::printf("FAIL: %s: no event #%zu (have %zu)\n", what, i, gSeen.size()); ++gFail; return; }
    const Seen& s = gSeen[i];
    std::snprintf(nm, sizeof nm, "%s: kind", what);   Expect(nm, s.kind, kind);
    std::snprintf(nm, sizeof nm, "%s: cpu is none", what); Expect(nm, (unsigned char)s.cpu, SE_HWEV_CPU_NONE);
    std::snprintf(nm, sizeof nm, "%s: arg0", what);   Expect(nm, s.a[0], a0);
    std::snprintf(nm, sizeof nm, "%s: arg1", what);   Expect(nm, s.a[1], a1);
    std::snprintf(nm, sizeof nm, "%s: arg2", what);   Expect(nm, s.a[2], a2);
    std::snprintf(nm, sizeof nm, "%s: arg3", what);   Expect(nm, s.a[3], a3);
}
}

int main()
{
    VDP1::Init();

    std::printf("mask clear: nothing is reported, whatever VDP1 does\n");
    Fresh(0);
    Cmd(0, SPRITE); Cmd(1, END);
    Start();
    VDP1::Update(gNow += 1000);
    Reg(0x2, 2);
    VBlank();
    Expect("events with the mask clear", gSeen.size(), 0);

    std::printf("another source's bit does not enable VDP1 events\n");
    Fresh(SE_HWEV_EN_VDP2 | SE_HWEV_EN_SCU_DMA | SE_HWEV_EN_CPU_EXC);
    Start();
    VDP1::Update(gNow += 1000);
    VBlank();
    Expect("events with only other bits set", gSeen.size(), 0);

    std::printf("a draw that reaches its END command\n");
    Fresh(SE_HWEV_EN_VDP1);
    Cmd(0, SPRITE); Cmd(1, SPRITE); Cmd(2, END);
    Start();
    Expect("one event after the start", gSeen.size(), 1);
    ExpectEv("draw start", 0, SE_HWEV_K_VDP1_DRAW_START, VDP1::FBDrawWhich ? 1 : 0, 0, 0, 0);
    for (unsigned i = 0; i < 20 && VDP1::DrawingActive; i++) VDP1::Update(gNow += 300);   // a sprite costs more than one slice
    Expect("drawing finished", VDP1::DrawingActive ? 1 : 0, 0);
    Expect("two events: start and end", gSeen.size(), 2);
    // the END command is entry 2 of the table: 2 * 32 bytes into VDP1 VRAM
    ExpectEv("draw end (END bit)", 1, SE_HWEV_K_VDP1_DRAW_END, 0x40, 0, 0, 0);

    std::printf("a start while a draw is still running says the previous one was interrupted\n");
    Fresh(SE_HWEV_EN_VDP1);
    Cmd(0, SPRITE); Cmd(1, END);
    Start();
    Start();
    Expect("two starts", gSeen.size(), 2);
    ExpectEv("second start", 1, SE_HWEV_K_VDP1_DRAW_START, VDP1::FBDrawWhich ? 1 : 0, 1, 0, 0);

    std::printf("a frame-buffer swap: erase start, then the swap; a draw still running is aborted by it\n");
    Fresh(SE_HWEV_EN_VDP1);
    Reg(0x0, 0x8);                               // TVMR: VBE (v-blank erase)
    Reg(0x1, 0);                                 // FBCR: automatic swap every frame
    Reg(0x3, 0x1234);                            // EWDR
    Reg(0x4, 0x5678);                            // EWLR
    Reg(0x5, 0x9ABC);                            // EWRR
    const unsigned drawFb = VDP1::FBDrawWhich ? 1 : 0;
    Cmd(0, SPRITE); Cmd(1, SPRITE); Cmd(2, END);
    Start();                                     // not finished: no Update runs before the swap
    gSeen.clear();
    VBlank();
    Expect("erase, abort, swap", gSeen.size(), 3);
    // erase starts on the buffer that is NOT being drawn into yet displayed ... (!FBDrawWhich at that moment)
    ExpectEv("erase start", 0, SE_HWEV_K_VDP1_FB_ERASE, drawFb ? 0 : 1, 0x1234, 0x5678, 0x9ABC);
    ExpectEv("draw aborted by the swap", 1, SE_HWEV_K_VDP1_DRAW_END, 0, 1, 0, 0);
    // after the swap the displayed buffer is the one that was drawn into
    ExpectEv("frame-buffer swap", 2, SE_HWEV_K_VDP1_FB_SWAP, drawFb, VDP1::FBCR, VDP1::TVMR, 0);
    Expect("FBCR argument is the register", VDP1::FBCR, 0);
    Expect("TVMR argument is the register", VDP1::TVMR, 0x8);

    std::printf("%s\n", gFail ? "FAILED" : "PASSED");
    return gFail ? 1 : 0;
}

#else
// ------------------------------------------------------------------------------------------------------------
// SH-2 core, SCU and on-chip DMAC, with the real glue
// ------------------------------------------------------------------------------------------------------------
static std::vector<SeHwEvent> gSeen;
// What the core itself had done when each record arrived (the hook runs right after its Exception()): the PC it
// pushed on the stack and the PC it vectored to. The independent oracle for an exception record.
struct Snap { uint32 pushedPc; };
static std::vector<Snap> gSnap;
uint32 StackTop(unsigned cpu);
extern "C"
{
void SeHarnessHwSink(const SeHwEvent* ev)
{
    Snap s = { 0 };
    if (ev->cpu < 2) { s.pushedPc = StackTop(ev->cpu); }
    gSeen.push_back(*ev);
    gSnap.push_back(s);
}
void SeMednafenSetHwEvents(unsigned int mask);
void SeMdfnHwEvEndFrame(void);
void SeMednafenStateLoaded(int slaveOn);
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

using namespace MDFN_IEN_SS;

namespace
{
const uint32 WRAML_A = 0x00200000, WRAMH_A = 0x06000000, SCSP_A = 0x05A00000;
const uint32 VBR_A = WRAMH_A + 0x1000, STACK_A = WRAMH_A + 0x10000;

void MapFast()
{
    for (uint64 A = 0; A < (1ULL << 32); A += (1U << SH7095_EXT_MAP_GRAN_BITS))
        SH7095_FastMap[A >> SH7095_EXT_MAP_GRAN_BITS] = (uintptr_t)fmap_dummy - A;
    SS_SetPhysMemMap(0x00000000, 0x000FFFFF, BIOSROM, sizeof(BIOSROM), false);
    SS_SetPhysMemMap(0x00200000, 0x002FFFFF, WorkRAML, sizeof(WorkRAML), true);
    SS_SetPhysMemMap(0x06000000, 0x07FFFFFF, WorkRAMH, sizeof(WorkRAMH), true);
}

void W32(uint32 A, uint32 V) { ne16_wbo_be<uint32>(WorkRAMH, A & 0xFFFFF, V); }
uint32 R32(uint32 A) { return ne16_rbo_be<uint32>(WorkRAMH, A & 0xFFFFF); }
}
uint32 StackTop(unsigned cpu) { return ne16_rbo_be<uint32>(WorkRAMH, CPU[cpu].R[15] & 0xFFFFF); }
namespace
{

// A fresh CPU `n` (0 master, 1 slave): reset, no interrupt pending, vectors at VBR_A, stack at STACK_A.
void Fresh(unsigned n)
{
    SH7095& c = CPU[n];
    SH7095_mem_timestamp = 0;
    c.timestamp = 0;
    c.Init(false, false);
    c.Reset(true, false);
    c.SetCCR(SH7095::CCR_CP);
    c.SetCCR(0);
    c.SetRegister(SH7095::GSREG_VBR, VBR_A);
    c.SetRegister(SH7095::GSREG_R0 + 15, STACK_A);
    c.SetRegister(SH7095::GSREG_SR, 0);          // interrupt mask 0
    c.EPending = 0;
    c.MA_until = 0;
    gSeen.clear();
    gSnap.clear();
}

// Put `op` at `pc` as the instruction in the decode stage, with NOPs behind it. The core's PC is the fetch
// pointer, two instructions ahead of the one it is executing, so PC = pc + 4.
void Prime(unsigned n, uint32 pc, uint16 op)
{
    SH7095& c = CPU[n];
    ne16_wbo_be<uint16>(WorkRAMH, pc & 0xFFFFF, op);
    for (unsigned i = 1; i < 16; i++) ne16_wbo_be<uint16>(WorkRAMH, (pc & 0xFFFFF) + i * 2, 0x0009);
    c.PC = pc + 4;
    c.Pipe_ID = op | ((uint32)InstrDecodeTab[op] << 24);
    c.Pipe_IF = 0x0009;
    c.IBuffer = 0x00090009;
    c.EPending = 0;
    c.MA_until = 0;
}

void Run(unsigned n, unsigned steps)
{
    for (unsigned i = 0; i < steps; i++)
    {
        if (n == 0) CPU[0].Step<0, false, false>();
        else CPU[1].RunSlaveUntil(CPU[1].timestamp + 8);
    }
}

void Handler(uint32 vec, uint32 addr) { W32(VBR_A + vec * 4, addr); }

const SeHwEvent* Find(unsigned kind, size_t nth = 0)
{
    for (size_t i = 0; i < gSeen.size(); i++)
        if (gSeen[i].kind == kind && nth-- == 0) return &gSeen[i];
    return nullptr;
}
size_t Count(unsigned kind)
{
    size_t n = 0;
    for (size_t i = 0; i < gSeen.size(); i++) n += gSeen[i].kind == kind;
    return n;
}

// One exception record, checked against what the core itself did: the saved PC is the word it pushed.
void CheckException(const char* what, unsigned cpu, uint32 wantPc, uint32 vec, uint32 cls, uint32 level, uint32 handler)
{
    char nm[160];
    const SeHwEvent* e = Find(SE_HWEV_K_SH2_EXCEPTION);
    std::snprintf(nm, sizeof nm, "%s: an exception event was reported", what);
    Expect(nm, e != nullptr, 1);
    if (!e) return;
    std::snprintf(nm, sizeof nm, "%s: source is CPU_EXC", what);  Expect(nm, e->source, 0);
    std::snprintf(nm, sizeof nm, "%s: cpu", what);               Expect(nm, e->cpu, cpu);
    std::snprintf(nm, sizeof nm, "%s: has an exact PC", what);   Expect(nm, e->flags, SE_HWEV_F_HAS_PC);
    std::snprintf(nm, sizeof nm, "%s: saved PC", what);          Expect(nm, e->pc, wantPc);
    std::snprintf(nm, sizeof nm, "%s: vector", what);            Expect(nm, e->arg[0], vec);
    std::snprintf(nm, sizeof nm, "%s: class", what);             Expect(nm, e->arg[1], cls);
    std::snprintf(nm, sizeof nm, "%s: level", what);             Expect(nm, e->arg[2], level);
    std::snprintf(nm, sizeof nm, "%s: handler PC", what);        Expect(nm, e->arg[3], handler);
    // the hardware pushed SR then PC: R15 -> saved PC, R15 + 4 -> saved SR
    const Snap& sn = gSnap[e - &gSeen[0]];
    std::snprintf(nm, sizeof nm, "%s: the PC the core pushed on the stack", what);
    Expect(nm, sn.pushedPc, wantPc);
    // (the branch to the handler has not happened yet when the hook runs; arg3 is checked against the vector table above)
}

void ExceptionTests()
{
    std::printf("SH-2 exceptions, master, through the real Step()\n");
    SeMednafenSetHwEvents(SE_HWEV_EN_CPU_EXC);

    Fresh(0);
    Handler(4, WRAMH_A + 0x2000);
    Prime(0, WRAMH_A + 0x100, 0xFFFF);           // not an instruction: illegal
    Run(0, 1);
    CheckException("illegal instruction", 0, WRAMH_A + 0x100, 4, SE_HWEV_EXC_ILLINSTR, 0, WRAMH_A + 0x2000);

    Fresh(0);
    Handler(0x23, WRAMH_A + 0x2400);
    Prime(0, WRAMH_A + 0x200, 0xC323);           // TRAPA #0x23
    Run(0, 1);
    CheckException("TRAPA", 0, WRAMH_A + 0x202, 0x23, SE_HWEV_EXC_TRAP, 0, WRAMH_A + 0x2400);

    Fresh(0);
    Handler(9, WRAMH_A + 0x2800);
    CPU[0].SetRegister(SH7095::GSREG_R0, WRAMH_A + 0x5001);
    Prime(0, WRAMH_A + 0x300, 0x6102);           // MOV.L @R0,R1 with R0 odd: CPU address error
    Run(0, 3);
    // the address error is noticed when the access completes, one instruction later than the load; the stack
    // comparison inside CheckException is the oracle for the saved PC
    CheckException("CPU address error", 0, WRAMH_A + 0x304, 9, SE_HWEV_EXC_CPUADDR, 0, WRAMH_A + 0x2800);

    std::printf("an SCU interrupt: delivered (SCU_INT_ACCEPT) then taken (exception, level, vector)\n");
    SeMednafenSetHwEvents(SE_HWEV_EN_CPU_EXC | SE_HWEV_EN_SCU_INT);
    Fresh(0);
    IMask = 0;
    IPending = 0;
    ILevel = 0;
    Handler(0x46, WRAMH_A + 0x2C00);          // IRL level 13 autovector: 0x40 + (13 >> 1)
    Prime(0, WRAMH_A + 0x400, 0x0009);
    SCU_SetInt(SCU_INT_HBIN, true);              // SCU level 0xD, SCU vector 0x42
    SCU_SetInt(SCU_INT_HBIN, false);
    Expect("SCU interrupt accepted", Count(SE_HWEV_K_SCU_INT_ACCEPT), 1);
    {
        const SeHwEvent* e = Find(SE_HWEV_K_SCU_INT_ACCEPT);
        if (e)
        {
            Expect("accept: source is SCU_INT", e->source, 1);
            Expect("accept: no cpu", e->cpu, SE_HWEV_CPU_NONE);
            Expect("accept: source bit (HBlank-IN)", e->arg[0], SCU_INT_HBIN);
            Expect("accept: level", e->arg[1], 0xD);
            Expect("accept: vector", e->arg[2], 0x42);
            Expect("accept: IPending after", e->arg[3], 0);
        }
    }
    Run(0, 3);
    // the CPU vectors by its own IRL autovector rule, which is what the exception event reports
    CheckException("SCU interrupt", 0, WRAMH_A + 0x402, 0x46, SE_HWEV_EXC_INT, 0xD, WRAMH_A + 0x2C00);

    std::printf("SH-2 exceptions, slave, through the resumable copy RunSlaveUntil\n");
    SeMednafenSetHwEvents(SE_HWEV_EN_CPU_EXC);
    Fresh(1);
    Handler(4, WRAMH_A + 0x3000);
    Prime(1, WRAMH_A + 0x600, 0xFFFF);
    Run(1, 4);
    CheckException("slave illegal instruction", 1, WRAMH_A + 0x600, 4, SE_HWEV_EXC_ILLINSTR, 0, WRAMH_A + 0x3000);
    CPU[1].timestamp = SS_EVENT_DISABLED_TS;

    std::printf("mask clear: an exception reports nothing\n");
    SeMednafenSetHwEvents(0);
    Fresh(0);
    Handler(4, WRAMH_A + 0x2000);
    Prime(0, WRAMH_A + 0x100, 0xFFFF);
    Run(0, 1);
    Expect("exception with the mask clear: events", gSeen.size(), 0);
    Expect("... but the core still took it", CPU[0].PC - 4, WRAMH_A + 0x2000);
}

// ---- on-chip DMAC ----
struct Dma { uint32 sar, dar, tcr; unsigned ts; };
void ChSetup(unsigned ch, const Dma& d)
{
    const uint32 base = 0xFFFFFF80 + ch * 0x10;
    CPU[0].MWFP32[7](base + 0x0, d.sar);
    CPU[0].MWFP32[7](base + 0x4, d.dar);
    CPU[0].MWFP32[7](base + 0x8, d.tcr);
    CPU[0].MWFP32[7](base + 0xC, 1 | 0x200 | (d.ts << 10) | (1 << 12) | (1 << 14));
}

void DmacTests()
{
    std::printf("on-chip DMAC: start on the enabling write, end when the transfer finishes\n");
    SeMednafenSetHwEvents(SE_HWEV_EN_SH2_DMAC);
    Fresh(0);
    for (unsigned i = 0; i < 0x40000; i++) ne16_wbo_be<uint16>(WorkRAMH, i * 2, 0x1111);
    // a running instruction stream, so the PC the event carries is that of the CPU, not zero
    Prime(0, WRAMH_A + 0x700, 0x0009);
    Dma d = { WRAMH_A, WRAML_A, 8, 2 };
    ChSetup(0, d);
    Expect("channel set up but DMAOR.DME clear: no event", gSeen.size(), 0);
    CPU[0].MWFP32[7](0xFFFFFFB0, 1);             // DMAOR: DME
    const SeHwEvent* s = Find(SE_HWEV_K_DMAC_START);
    Expect("DMAC start reported", s != nullptr, 1);
    if (s)
    {
        Expect("start: source is SH2_DMAC", s->source, 3);
        Expect("start: cpu", s->cpu, 0);
        Expect("start: PC is present and approximate", s->flags, SE_HWEV_F_HAS_PC | SE_HWEV_F_PC_APPROX);
        Expect("start: PC is the CPU's (fetch pointer less two instructions)", s->pc, WRAMH_A + 0x700);
        Expect("start: channel | CHCR << 8", s->arg[0], 0u | ((1u | 0x200 | (2u << 10) | (1u << 12) | (1u << 14)) << 8));
        Expect("start: SAR", s->arg[1], WRAMH_A);
        Expect("start: DAR", s->arg[2], WRAML_A);
        Expect("start: TCR", s->arg[3], 8);
    }
    Expect("no end yet", Count(SE_HWEV_K_DMAC_END), 0);
    CPU[0].DMA_Update(0x3FFFFFFF);
    const SeHwEvent* e = Find(SE_HWEV_K_DMAC_END);
    Expect("DMAC end reported", e != nullptr, 1);
    if (e)
    {
        Expect("end: source is SH2_DMAC", e->source, 3);
        Expect("end: cpu", e->cpu, 0);
        Expect("end: channel", e->arg[0], 0);
        Expect("end: no PC", e->flags, 0);
    }
    Expect("exactly one start and one end", Count(SE_HWEV_K_DMAC_START) * 10 + Count(SE_HWEV_K_DMAC_END), 11);

    std::printf("a transfer already running when the source is switched on is not reported as a start\n");
    SeMednafenSetHwEvents(0);
    Fresh(0);
    ChSetup(1, d);
    CPU[0].MWFP32[7](0xFFFFFFB0, 1);
    SeMednafenSetHwEvents(SE_HWEV_EN_SH2_DMAC);  // channel 1 is running: the shadow is unknown, the next recalc seeds it
    CPU[0].MWFP32[7](0xFFFFFFB0, 1);             // a DMAOR write that changes nothing about channel 1
    Expect("seeding recalc: no start", Count(SE_HWEV_K_DMAC_START), 0);
    CPU[0].DMA_Update(0x3FFFFFFF);
    Expect("finish after the seed: one end", Count(SE_HWEV_K_DMAC_END), 1);
    {
        const SeHwEvent* x = Find(SE_HWEV_K_DMAC_END);
        if (x) Expect("end of channel 1", x->arg[0], 1);
    }

    std::printf("DMA address error: the on-chip DMAC raises it, the core takes it\n");
    SeMednafenSetHwEvents(SE_HWEV_EN_CPU_EXC | SE_HWEV_EN_SH2_DMAC);
    Fresh(0);
    Handler(0xA, WRAMH_A + 0x3400);
    Prime(0, WRAMH_A + 0x800, 0x0009);
    Dma odd = { WRAMH_A + 2, WRAML_A, 4, 2 };    // 32-bit units from an odd-word source: address error
    ChSetup(0, odd);
    CPU[0].MWFP32[7](0xFFFFFFB0, 1);
    CPU[0].DMA_Update(0x3FFFFFFF);
    Run(0, 2);
    const SeHwEvent* x = Find(SE_HWEV_K_SH2_EXCEPTION);
    Expect("DMA address error reported", x != nullptr, 1);
    if (x)
    {
        Expect("DMAADDR: vector", x->arg[0], 0xA);
        Expect("DMAADDR: class", x->arg[1], SE_HWEV_EXC_DMAADDR);
        Expect("DMAADDR: handler", x->arg[3], WRAMH_A + 0x3400);
    }
}

// ---- SCU ----
void ScuTests()
{
    std::printf("SCU DMA: start (src, dst, bytes, level, bus route), end, illegal\n");
    SeMednafenSetHwEvents(SE_HWEV_EN_SCU_DMA);
    gSeen.clear();
    DMALevel[1].Indirect = false;
    DMALevel[1].SF = 4;
    const bool ok = StartDMATransfer(&DMALevel[1], WRAMH_A + 0x4000, SCSP_A, 0x40);
    Expect("transfer started", ok, 1);
    const SeHwEvent* s = Find(SE_HWEV_K_SCU_DMA_START);
    Expect("SCU DMA start reported", s != nullptr, 1);
    if (s)
    {
        Expect("start: source is SCU_DMA", s->source, 2);
        Expect("start: no cpu", s->cpu, SE_HWEV_CPU_NONE);
        // level 1 | indirect 0 << 8 | start factor 4 << 16 | read bus (C-bus = 2) << 24 | write bus (B-bus = 1) << 28
        Expect("start: level | indirect | factor | buses", s->arg[0], 1u | (0u << 8) | (4u << 16) | (2u << 24) | (1u << 28));
        Expect("start: source address", s->arg[1], WRAMH_A + 0x4000);
        Expect("start: destination address", s->arg[2], SCSP_A);
        Expect("start: byte count", s->arg[3], 0x40);
    }
    gSeen.clear();
    DMALevel[2].Active = true;
    SCU_DoDMAEnd(2);
    const SeHwEvent* e = Find(SE_HWEV_K_SCU_DMA_END);
    Expect("SCU DMA end reported", e != nullptr, 1);
    if (e) Expect("end: level", e->arg[0], 2);
    gSeen.clear();
    const bool bad = StartDMATransfer(&DMALevel[0], 0x00000000, WRAMH_A, 4);
    Expect("illegal source refused", bad, 0);
    const SeHwEvent* i = Find(SE_HWEV_K_SCU_DMA_ILLEGAL);
    Expect("illegal SCU DMA reported", i != nullptr, 1);
    if (i)
    {
        Expect("illegal: level", i->arg[0], 0);
        Expect("illegal: source", i->arg[1], 0);
    }
    gSeen.clear();
    const bool bad2 = StartDMATransfer(&DMALevel[0], WRAMH_A, 0x00000000, 4);
    Expect("illegal destination refused", bad2, 0);
    Expect("illegal destination reported", Count(SE_HWEV_K_SCU_DMA_ILLEGAL), 1);

    std::printf("SCU interrupt mask write: new and old IMS\n");
    SeMednafenSetHwEvents(SE_HWEV_EN_SCU_INT);
    gSeen.clear();
    IMask = 0x00F0;
    IPending = 0;
    ILevel = 0;
    uint32 db = 0x0F0F;
    SCU_RegRW_DB<uint32, true>(0x25FE00A0, &db);
    const SeHwEvent* m = Find(SE_HWEV_K_SCU_INT_MASK);
    Expect("IMS write reported", m != nullptr, 1);
    if (m)
    {
        Expect("mask: source is SCU_INT", m->source, 1);
        Expect("mask: new IMask (bit 14 is not writable)", m->arg[0], 0x0F0F);
        Expect("mask: old IMask", m->arg[1], 0x00F0);
        Expect("mask: IMask really became the reported value", IMask, 0x0F0F);
    }

    std::printf("v-blank edges from SCU_SetHBVB\n");
    SeMednafenSetHwEvents(SE_HWEV_EN_VDP2);
    IMask = 0xFFFF;
    gSeen.clear();
    SCU_SetHBVB(0, false, false);
    Expect("no edge: no event", gSeen.size(), 0);
    SCU_SetHBVB(0, false, true);
    SCU_SetHBVB(0, true, true);
    SCU_SetHBVB(0, false, false);
    Expect("one IN, one OUT", gSeen.size(), 2);
    if (gSeen.size() == 2)
    {
        Expect("v-blank IN: kind", gSeen[0].kind, SE_HWEV_K_VDP2_VBLANK);
        Expect("v-blank IN: source is VDP2", gSeen[0].source, 5);
        Expect("v-blank IN: arg0", gSeen[0].arg[0], 1);
        Expect("v-blank OUT: arg0", gSeen[1].arg[0], 0);
    }
    SeMednafenSetHwEvents(0);
    gSeen.clear();
    SCU_SetHBVB(0, false, true);
    SCU_SetHBVB(0, false, false);
    Expect("mask clear: v-blank edges report nothing", gSeen.size(), 0);
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
    CPU[1].timestamp = SS_EVENT_DISABLED_TS;
    MapFast();
    std::memset(BIOSROM, 0x22, sizeof(BIOSROM));
    SeMednafenStateLoaded(0);

    ExceptionTests();
    DmacTests();
    ScuTests();

    std::printf("%s\n", gFail ? "FAILED" : "PASSED");
    return gFail ? 1 : 0;
}
#endif
