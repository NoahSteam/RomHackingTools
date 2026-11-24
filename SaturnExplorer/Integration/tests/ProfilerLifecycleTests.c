/* The profiler's frame-lifecycle contract (F2 + F6), driven without an
 * emulator: the real glue's accounting (se_mednafen_glue.c, included because it has no header) against
 * the real shadow call stack (se_export.c), on a clock the test controls. The glue entry points the
 * emulator calls at a power edge, a CPU reset, an exception entry, a state load and the end of a frame
 * (SeMednafenSlaveActive / CpuReset / StateLoaded / FrameClose) are called here exactly
 * as apply.py's injected hooks call them; Integration/Mednafen/check_component.py checks that those
 * hooks sit where this file assumes.
 *
 * Every check asserts CYCLES -- exact on-time, exact cycles charged to a named key or to a raw-PC
 * bucket -- never just a flag. Timestamps are SH-2 clocks, frame-relative. */
#include "se_mednafen_glue.c"

#include <stdio.h>
#include <string.h>

static int gFailures;

static void Check(int condition, const char* expression, int line)
{
    if (condition) return;
    fprintf(stderr, "CHECK failed at line %d: %s\n", line, expression);
    ++gFailures;
}
#define CHECK(expr) Check((expr) ? 1 : 0, #expr, __LINE__)
#define CHECK_EQ(got, want) do { const unsigned long long g_ = (unsigned long long)(got), w_ = (unsigned long long)(want); \
    if (g_ != w_) { fprintf(stderr, "CHECK failed at line %d: %s == %llu, want %llu\n", __LINE__, #got, g_, w_); ++gFailures; } } while (0)

#define OP_NOP      0x0009u
#define OP_RTS      0x000Bu
#define OP_BSR(d)   ((unsigned short)(0xB000u | ((d) & 0x0FFFu)))

/* BSR displacement that reaches 'target' from the instruction at 'pc'. */
static unsigned short BsrTo(unsigned int pc, unsigned int target)
{
    return OP_BSR(((int)(target - (pc + 4u)) / 2) & 0x0FFF);
}

static unsigned int RawKey(unsigned int pc, int cpu)
{
    return (SE_PERF_FN_RAW_PC | (pc & SE_PERF_FN_ADDR_MASK & ~(SE_PERF_FN_PC_BUCKET - 1u))) | (cpu ? SE_PERF_FN_SLAVE : 0u);
}
static unsigned int FnKey(unsigned int entry, int cpu)
{
    return (entry & SE_PERF_FN_ADDR_MASK) | (cpu ? SE_PERF_FN_SLAVE : 0u);
}

/* Cycles the per-frame table holds for one key, and for a CPU as a whole. */
static unsigned long long Cycles(unsigned int key)
{
    unsigned int i;
    for (i = 0; i < SE_PROF_SLOTS; ++i)
        if (sProfTab[i].key == key) return sProfTab[i].cycles;
    return 0;
}
static unsigned long long CpuTotal(int cpu)
{
    unsigned long long t = 0;
    unsigned int i;
    for (i = 0; i < SE_PROF_SLOTS; ++i)
        if (sProfTab[i].key && ((sProfTab[i].key & SE_PERF_FN_SLAVE) != 0) == (cpu != 0)) t += sProfTab[i].cycles;
    return t + (unsigned long long)0;
}

static void Fresh(int profiling)
{
    memset(SeSsPerfC, 0, sizeof SeSsPerfC);
    memset(sProfTab, 0, sizeof sProfTab);
    sProfUsed = 0;
    sProfOther = 0;
    memset(sProfSynced, 0, sizeof sProfSynced);
    memset(sProfLastTs, 0, sizeof sProfLastTs);
    memset(sProfLastIdle, 0, sizeof sProfLastIdle);
    memset(sFlowPending, 0, sizeof sFlowPending);
    sSlaveOn = -1;
    sSlaveSince = 0;
    SeMdfnApplyMode(profiling ? SE_LIVE_PROF_FUNCTIONS : 0u);
    SeExportResetCallStack(0);
    SeExportResetCallStack(1);
}

/* One presentation of the per-instruction hook as the emulator makes it: the flow tracker (what
 * SeMdfnTrackFlow does minus its register reads), then the sampler, which sees the clock. */
static void Insn(int cpu, unsigned int pc, unsigned short op, unsigned int rn, unsigned int sp, int ts)
{
    if (!SeMdfnFlowAdvance(cpu, pc))
    {
        const SeFlowKind kind = SeMdfnClassifyFlow(op);
        if (kind != SeFlowNone) SeMdfnFlowDefer(cpu, pc, kind, op, rn, sp, 0);
    }
    if (sProfOn) SeMdfnProfSampleAt(cpu, pc, ts);
}
static void Nop(int cpu, unsigned int pc, int ts) { Insn(cpu, pc, OP_NOP, 0, 0, ts); }

/* ================================ F2: slave on-time ================================ */

/* The plan's case: in a 1,000-cycle frame a slave on during [0,200) and [800,1000) reports exactly 400
 * on-cycles, and the 600-cycle gap contributes zero function cycles. */
static void TestSlaveOnTimeAndGap(void)
{
    Fresh(1);
    SeMednafenStateLoaded(1);                       /* the frame starts with the slave on */
    /* slave runs a function at 0x06004000 (an observed call) */
    Insn(1, 0x06003000, BsrTo(0x06003000, 0x06004000), 0, 0x060F0000, 0);
    Nop(1, 0x06003002, 4);
    Nop(1, 0x06004000, 8);                          /* the call lands: the callee is now named */
    Nop(1, 0x06004002, 100);
    Nop(1, 0x06004004, 150);
    SeMednafenSlaveActive(0, 200);                  /* SetActive(false): the slave had run to 200 */
    SeMednafenCpuReset(1, 200);                     /* ... and SetActive's own Reset(true) */
    Nop(1, 0x06009000, 800);                        /* SetActive(true) at 800, fresh code */
    SeMednafenSlaveActive(1, 800);
    SeMednafenCpuReset(1, 800);
    Nop(1, 0x06009000, 800);
    Nop(1, 0x06009002, 900);
    SeMednafenFrameClose(1000, 0, 1000, 1);

    CHECK_EQ(SeSsPerfC[SE_PERF_SSH2_ON_CYCLES], 400);
    CHECK_EQ(SeSsPerfC[SE_PERF_RESERVED_54], 0);
    /* Function cycles: [0,200) and [800,1000) only. Nothing for the 600 cycles between. */
    CHECK_EQ(CpuTotal(1), 400);
    CHECK_EQ(Cycles(FnKey(0x06004000, 1)), 200 - 8);   /* the call itself ran [0,8) under the (unknown) caller */
    CHECK_EQ(Cycles(RawKey(0x06003000, 1)), 8);
    CHECK_EQ(Cycles(RawKey(0x06009000, 1)), 200);       /* [800,1000) */
}

/* The same across a frame rebase, a redundant SetActive, and a reopened interval. */
static void TestRebaseRedundantAndReopen(void)
{
    Fresh(0);
    SeMednafenStateLoaded(0);
    SeMednafenSlaveActive(1, 300);                  /* on at 300 */
    SeMednafenSlaveActive(1, 450);                  /* redundant: no edge, the interval is not restarted */
    SeMednafenFrameClose(1000, 0, 1000, 1);
    CHECK_EQ(SeSsPerfC[SE_PERF_SSH2_ON_CYCLES], 700);

    /* next frame: still on, the interval reopens at 0 (the clocks were rebased) */
    SeSsPerfC[SE_PERF_SSH2_ON_CYCLES] = 0;
    SeMednafenSlaveActive(1, 10);                   /* a redundant call right after the rebase */
    SeMednafenSlaveActive(0, 400);
    SeMednafenFrameClose(1000, 0, 0, 0);
    CHECK_EQ(SeSsPerfC[SE_PERF_SSH2_ON_CYCLES], 400);

    /* third frame: off for all of it, then on at the very end */
    SeSsPerfC[SE_PERF_SSH2_ON_CYCLES] = 0;
    SeMednafenSlaveActive(1, 1000);
    SeMednafenFrameClose(1000, 0, 1000, 1);
    CHECK_EQ(SeSsPerfC[SE_PERF_SSH2_ON_CYCLES], 0);
    /* and it is on from 0 in the fourth */
    SeMednafenFrameClose(1000, 0, 1000, 1);
    CHECK_EQ(SeSsPerfC[SE_PERF_SSH2_ON_CYCLES], 1000);
}

/* A state load never calls SetActive: the on/off state comes from the loaded CPU, and what was
 * accumulated for the frame on the abandoned timeline is discarded. */
static void TestStateRestore(void)
{
    /* on -> off: the slave was on and had accumulated time; the loaded state has it off */
    Fresh(0);
    SeMednafenStateLoaded(1);
    SeMednafenSlaveActive(0, 700);                  /* 700 cycles banked ... */
    SeMednafenSlaveActive(1, 710);
    SeMednafenStateLoaded(0);                       /* ... then a rewind to a state where it is off */
    CHECK_EQ(SeSsPerfC[SE_PERF_SSH2_ON_CYCLES], 0); /* partial accounting discarded */
    SeMednafenFrameClose(1000, 0, 0, 0);
    CHECK_EQ(SeSsPerfC[SE_PERF_SSH2_ON_CYCLES], 0);

    /* off -> on: our record says off, the loaded CPU is on (no SetActive ran): the whole frame counts */
    Fresh(0);
    SeMednafenStateLoaded(0);
    SeMednafenSlaveActive(0, 0);                    /* record: off */
    SeMednafenStateLoaded(1);                       /* loaded state: on */
    SeMednafenFrameClose(1000, 0, 1000, 1);
    CHECK_EQ(SeSsPerfC[SE_PERF_SSH2_ON_CYCLES], 1000);

    /* The record and the CPU disagree at the frame end (an edge nobody told us about): never over-claim. */
    Fresh(0);
    SeMednafenStateLoaded(1);
    SeMednafenFrameClose(1000, 0, 0, 0);            /* the CPU turned out to be off */
    CHECK_EQ(SeSsPerfC[SE_PERF_SSH2_ON_CYCLES], 0);
    SeMednafenFrameClose(1000, 0, 1000, 1);         /* and then on, from 0 */
    SeMednafenFrameClose(1000, 0, 1000, 1);
    CHECK_EQ(SeSsPerfC[SE_PERF_SSH2_ON_CYCLES], 1000);
}

/* A watchdog reset with the power left on: the final valid interval is charged, then the recorded
 * stack, the pending flow and the baseline are dropped, so what runs after it is not the old function. */
static void TestWatchdogReset(void)
{
    Fresh(1);
    SeMednafenStateLoaded(1);
    Insn(1, 0x06003000, BsrTo(0x06003000, 0x06004000), 0, 0, 0);
    Nop(1, 0x06003002, 4);
    Nop(1, 0x06004000, 8);
    Nop(1, 0x06004002, 40);                         /* the callee, named */
    SeMednafenCpuReset(1, 100);                     /* the watchdog fires: [40,100) is the callee's last */
    CHECK_EQ(Cycles(FnKey(0x06004000, 1)), 100 - 8);
    CHECK_EQ(SeExportCurrentFunction(1), 0u);       /* ownership gone with the stack */
    Nop(1, 0x00000200, 140);                        /* the reset vector's code, 40 cycles after: the first sample has no baseline */
    Nop(1, 0x00000202, 150);
    CHECK_EQ(Cycles(FnKey(0x06004000, 1)), 100 - 8);   /* nothing more reached the old function */
    CHECK_EQ(Cycles(RawKey(0x00000200, 1)), 10);
    SeMednafenFrameClose(1000, 0, 1000, 1);
    CHECK_EQ(SeSsPerfC[SE_PERF_SSH2_ON_CYCLES], 1000);   /* the power never changed */
}

/* A last instruction with a 37-cycle interval of which 7 are idle puts exactly 30 on its prior key; the
 * frame's final interval is charged before the rebase and the next frame carries on from the frame start. */
static void TestIdleAndFrameBoundary(void)
{
    Fresh(1);
    Nop(0, 0x06001000, 0);
    SeSsPerfC[SE_PERF_SH2_SLEEP] += 7;              /* 7 of the next 37 cycles were sleep */
    Nop(0, 0x06001002, 37);
    CHECK_EQ(Cycles(RawKey(0x06001000, 0)), 30);

    /* the frame's last instruction ran [37, 1003): it is charged at the close, with its idle removed */
    SeSsPerfC[SE_PERF_SH2_STALL] += 6;
    SeMednafenFrameClose(1000, 1003, 0, 0);
    CHECK_EQ(Cycles(RawKey(0x06001000, 0)), 30 + (1003 - 37 - 6));
    /* publication zeroes the counters and so the idle copies; the baseline is now frame-relative */
    memset(SeSsPerfC, 0, sizeof SeSsPerfC);
    sProfLastIdle[0] = sProfLastIdle[1] = 0;
    Nop(0, 0x06001004, 10);                         /* 10 cycles into the next frame: 3 (rebased 1003 -> 3) .. 10 */
    CHECK_EQ(Cycles(RawKey(0x06001000, 0)), 30 + (1003 - 37 - 6) + (10 - 3));
}

#define U_PC  0x06001000u

/* ================================ F6: the latched mode ================================ */

static void TestModeLatch(void)
{
    Fresh(0);
    CHECK_EQ(sFrameMode, 0u);
    SeMdfnApplyMode(SE_LIVE_PROF_FUNCTIONS);
    CHECK_EQ(sFrameMode, SE_LIVE_PROF_FUNCTIONS);
    CHECK(sProfOn == 1);
    Nop(0, U_PC, 0);
    Nop(0, U_PC + 2, 10);
    SeMdfnApplyMode(0);                              /* applied between frames only: the table of the frame that ran attributed stays whole */
    CHECK_EQ(sFrameMode, 0u);
    CHECK(sProfOn == 0);
    CHECK_EQ(Cycles(RawKey(U_PC, 0)), 10);
}

int main(void)
{
    TestSlaveOnTimeAndGap();
    TestRebaseRedundantAndReopen();
    TestStateRestore();
    TestWatchdogReset();
    TestIdleAndFrameBoundary();
    TestModeLatch();
    if (gFailures) { fprintf(stderr, "%d failure(s)\n", gFailures); return 1; }
    printf("ProfilerLifecycleTests: all passed\n");
    return 0;
}
