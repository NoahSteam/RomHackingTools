/* The profiler's frame-lifecycle contract (F2 + F6) and function ownership (F9), driven without an
 * emulator: the real glue's accounting (se_mednafen_glue.c, included because it has no header) against
 * the real shadow call stack (se_export.c), on a clock the test controls. The glue entry points the
 * emulator calls at a power edge, a CPU reset, an exception entry, a state load and the end of a frame
 * (SeMednafenSlaveActive / CpuReset / ExceptionEntry / StateLoaded / FrameClose) are called here exactly
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
#define OP_RTE      0x002Bu
#define OP_JMP_R0   0x402Bu
#define OP_BSR(d)   ((unsigned short)(0xB000u | ((d) & 0x0FFFu)))
#define OP_TRAPA(i) ((unsigned short)(0xC300u | ((i) & 0xFFu)))

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

/* ============================== F9: function ownership ============================== */

#define U_PC  0x06001000u   /* the unknown parent */
#define K_PC  0x06002000u   /* the observed callee */

/* The positive oracle from the plan's Step 0 additions: an unknown parent calls K. The call and its
 * delay slot charge the parent's raw bucket; K's body and its return delay slot charge K's named key;
 * after the return, charging resumes in the raw bucket. */
static void TestPositiveOracle(void)
{
    Fresh(1);
    Nop(0, U_PC, 0);
    Insn(0, U_PC + 2, BsrTo(U_PC + 2, K_PC), 0, 0x060F0000, 4);    /* bsr K */
    Nop(0, U_PC + 4, 8);                                           /* its delay slot */
    Nop(0, K_PC, 12);                                              /* K's first instruction */
    Nop(0, K_PC + 2, 16);
    Insn(0, K_PC + 4, OP_RTS, 0, 0x060F0000, 20);                  /* K's rts */
    Nop(0, K_PC + 6, 24);                                          /* its delay slot */
    Nop(0, U_PC + 6, 28);                                          /* back in the parent (the frame's return address) */
    Nop(0, U_PC + 8, 32);
    Nop(0, U_PC + 10, 36);

    CHECK_EQ(Cycles(FnKey(K_PC, 0)), 16);            /* body [12,20) + rts [20,24) + its slot [24,28) */
    CHECK_EQ(Cycles(RawKey(U_PC, 0)), 12 + 8);       /* call + slot [0,12), then the parent after the return [28,36) */
    CHECK_EQ(CpuTotal(0), 36);
    CHECK_EQ(SeExportCurrentFunction(0), 0u);        /* unknown parent: raw again */
}

/* Known parent: main (unknown) -> A (observed) -> B (observed). B's return goes back to A, which is known,
 * so A is named again, and A's own return goes back to the unknown main. */
static void TestKnownParentReturn(void)
{
    const unsigned int A = 0x06001800u, B = 0x06002000u;
    Fresh(1);
    Nop(0, U_PC, 0);
    Insn(0, U_PC + 2, BsrTo(U_PC + 2, A), 0, 0x060F0000, 4);
    Nop(0, U_PC + 4, 8);
    Insn(0, A, BsrTo(A, B), 0, 0x060EFFF0, 12);                    /* A calls B */
    Nop(0, A + 2, 16);
    Insn(0, B, OP_RTS, 0, 0x060EFFE0, 20);                         /* B returns at once */
    Nop(0, B + 2, 24);
    Nop(0, A + 4, 28);                                             /* back in A */
    Nop(0, A + 6, 32);
    Insn(0, A + 8, OP_RTS, 0, 0x060EFFF0, 36);                     /* A returns to main */
    Nop(0, A + 10, 40);
    Nop(0, U_PC + 6, 44);
    Nop(0, U_PC + 8, 48);

    /* main [0,12): the call, its slot and the first look at A; A [12,20) its call to B and the slot; B [20,28) its
     * rts and slot; A again [28,44) after B's return landed on the frame's return address (A was known when it called
     * B), through its own rts and slot; main [44,48) after A's return into the unknown parent. */
    CHECK_EQ(Cycles(FnKey(B, 0)), 8);
    CHECK_EQ(Cycles(FnKey(A, 0)), (20 - 12) + (44 - 28));
    CHECK_EQ(Cycles(RawKey(U_PC, 0)), 12 + (48 - 44));
    CHECK_EQ(CpuTotal(0), 48);
}

/* An asynchronous interrupt: the handler's cycles go to the raw bucket, not to the function it
 * interrupted; once the interrupted function's own return proves ownership again, naming resumes. */
static void TestInterrupt(void)
{
    const unsigned int A = 0x06001800u, B = 0x06002000u, H = 0x06008000u;
    Fresh(1);
    Insn(0, U_PC, BsrTo(U_PC, A), 0, 0x060F0000, 0);
    Nop(0, U_PC + 2, 4);
    Insn(0, A, BsrTo(A, B), 0, 0x060EFFF0, 8);
    Nop(0, A + 2, 12);
    Nop(0, B, 16);                                   /* B, named */
    Nop(0, B + 2, 20);
    SeMednafenExceptionEntry(0);                     /* the interrupt is taken between B+2 and B+4 */
    Nop(0, H, 50);                                   /* the handler: 30 cycles of entry stacking + first insn; charged to B's last key */
    Nop(0, H + 2, 54);
    Insn(0, H + 4, OP_RTE, 0, 0x060EFF00, 58);
    Nop(0, H + 6, 62);
    Nop(0, B + 4, 66);                               /* interrupted code again (the rte landed) */
    Insn(0, B + 6, OP_RTS, 0, 0x060EFFE0, 70);
    Nop(0, B + 8, 74);
    Nop(0, A + 4, 78);                               /* B returned to A: A was known when it called B */
    Nop(0, A + 6, 82);

    /* B is named [16,50): its own two instructions and the interval in which the interrupt was taken (the entry
     * stacking is not separable from them). From the handler's first presentation on nothing is named: the
     * handler's bucket [50,66), then B's own bucket for the interrupted code after the unobserved rte [66,78),
     * until B's rts lands on the frame's return address: A was known when it called B, so A is named again. */
    CHECK_EQ(Cycles(FnKey(B, 0)), 50 - 16);
    CHECK_EQ(Cycles(RawKey(H, 0)), 66 - 50);
    CHECK_EQ(Cycles(RawKey(B, 0)), 78 - 66);
    CHECK_EQ(Cycles(FnKey(A, 0)), (16 - 8) + (82 - 78));
    CHECK_EQ(Cycles(RawKey(U_PC, 0)), 8);
    CHECK_EQ(CpuTotal(0), 82);
}

/* A computed jump (switch or tail call): the stack is kept, but the body that follows is not charged to
 * the function that was running; the tail callee's own return proves ownership of the original caller. */
static void TestTailJump(void)
{
    const unsigned int A = 0x06001800u, G = 0x06005000u, M = 0x06001000u;
    Fresh(1);
    Insn(0, M, BsrTo(M, A), 0, 0x060F0000, 0);       /* main calls A (parent unknown) */
    Nop(0, M + 2, 4);
    Nop(0, A, 8);                                    /* A named */
    Insn(0, A + 2, OP_JMP_R0, G, 0x060EFFF0, 12);    /* A tail-jumps to G */
    Nop(0, A + 4, 16);                               /* its delay slot: still A's */
    Nop(0, G, 20);                                   /* the tail callee's body: raw, NOT A */
    Nop(0, G + 2, 28);
    Insn(0, G + 4, OP_RTS, 0, 0x060EFFF0, 36);       /* G returns to main through A's frame */
    Nop(0, G + 6, 40);
    Nop(0, M + 6, 44);
    Nop(0, M + 8, 48);

    CHECK_EQ(Cycles(FnKey(A, 0)), (20 - 8));         /* [8,12) jmp prefix, the jmp, its slot -> [8,20) */
    CHECK_EQ(Cycles(RawKey(G, 0)), (44 - 20));       /* G's body, its return and its slot -- all raw */
    CHECK_EQ(Cycles(FnKey(G, 0)), 0);
}

/* A call whose target is not where execution arrived (an interrupt came between the call completing
 * and the callee's first instruction) must not name the callee. */
static void TestInterruptedCall(void)
{
    const unsigned int A = 0x06001800u, H = 0x06008000u;
    Fresh(1);
    Insn(0, U_PC, BsrTo(U_PC, A), 0, 0x060F0000, 0);
    Nop(0, U_PC + 2, 4);
    SeMednafenExceptionEntry(0);                     /* taken before A's first instruction ran */
    Nop(0, H, 40);                                   /* the handler's first presentation completes the call */
    Nop(0, H + 2, 44);
    CHECK_EQ(Cycles(FnKey(A, 0)), 0);                /* A was never entered, so not named */
    CHECK_EQ(Cycles(RawKey(H, 0)), 4);
}

/* A trapa is the exception that IS observed: its handler is named, and its rte restores the parent. */
static void TestTrapa(void)
{
    const unsigned int H = 0x06008000u, A = 0x06001800u;
    Fresh(1);
    Insn(0, U_PC, BsrTo(U_PC, A), 0, 0x060F0000, 0);
    Nop(0, U_PC + 2, 4);
    Nop(0, A, 8);
    {
        /* trapa at A+2: the handler address is VBR-vectored (the glue reads it; here it is passed in) */
        SeMdfnFlowDefer(0, A + 2, SeFlowTrap, OP_TRAPA(5), 0, 0x060EFFF0, H);
        SeMdfnProfSampleAt(0, A + 2, 12);
        SeMednafenExceptionEntry(0);                 /* the core's Exception() macro ran: not an ownership loss */
    }
    Nop(0, H, 16);
    Nop(0, H + 2, 20);
    CHECK_EQ(SeExportCurrentFunction(0), FnKey(H, 0));
    Insn(0, H + 4, OP_RTE, 0, 0x060EFFF0 - 8, 24);
    Nop(0, H + 6, 28);
    Nop(0, A + 4, 32);                               /* the saved PC: trapa's PC + 2 */
    CHECK_EQ(Cycles(FnKey(H, 0)), 28 - 12 + 0);
    CHECK_EQ(SeExportCurrentFunction(0), FnKey(A, 0) & SE_PERF_FN_ADDR_MASK);   /* A is named again: it was known at the trap */
}

/* Observation lost: disabling and re-enabling profiling while a callee returns unseen leaves a stale
 * frame; nothing is charged to it afterwards. Likewise a hook that stops being continuous. */
static void TestLostObservation(void)
{
    const unsigned int A = 0x06001800u;
    Fresh(1);
    Insn(0, U_PC, BsrTo(U_PC, A), 0, 0x060F0000, 0);
    Nop(0, U_PC + 2, 4);
    Nop(0, A, 8);
    Nop(0, A + 2, 12);
    CHECK_EQ(SeExportCurrentFunction(0), FnKey(A, 0));
    /* the client turns attribution off; A returns unseen; the client turns it on again */
    SeMdfnApplyMode(0);
    SeMdfnApplyMode(SE_LIVE_PROF_FUNCTIONS);
    Nop(0, U_PC + 6, 100);                           /* long since back in the parent; no baseline yet */
    Nop(0, U_PC + 8, 110);
    CHECK_EQ(SeExportCurrentFunction(0), 0u);        /* the stale frame names nothing */
    CHECK_EQ(Cycles(FnKey(A, 0)), 12 - 8);           /* only what was seen */
    CHECK_EQ(Cycles(RawKey(U_PC, 0)), 8 + 10);       /* the call and its slot [0,8) before A, then [100,110) after the re-enable */
}

/* The hook stops presenting every instruction (a breakpoint-only callback): same loss, no profiler toggle. */
static void TestHookModeLoss(void)
{
    const unsigned int A = 0x06001800u;
    Fresh(1);
    Insn(0, U_PC, BsrTo(U_PC, A), 0, 0x060F0000, 0);
    Nop(0, U_PC + 2, 4);
    Nop(0, A, 8);
    CHECK_EQ(SeExportCurrentFunction(0), FnKey(A, 0));
    SeMednafenHookMode(0);
    CHECK_EQ(SeExportCurrentFunction(0), 0u);
    SeMednafenHookMode(1);
    Nop(0, A + 2, 20);
    CHECK_EQ(SeExportCurrentFunction(0), 0u);
}

/* A nested call from unknown code on top of a STALE frame. main calls A (A is named), an interrupt is taken
 * inside A, and the handler (unknown code, running on top of A's recorded frame) calls K. K's body is named; its
 * return lands in the handler, whose owner is unknown -- it must NOT fall back to naming A, the stale top. */
static void TestNestedCallFromUnknown(void)
{
    const unsigned int A = 0x06001800u, H = 0x06008000u, K = 0x06009000u;
    Fresh(1);
    Insn(0, U_PC, BsrTo(U_PC, A), 0, 0x060F0000, 0);
    Nop(0, U_PC + 2, 4);
    Nop(0, A, 8);
    Nop(0, A + 2, 12);
    SeMednafenExceptionEntry(0);
    Nop(0, H, 40);                                   /* handler: raw */
    Insn(0, H + 2, BsrTo(H + 2, K), 0, 0x060EFF00, 44);   /* the handler calls K */
    Nop(0, H + 4, 48);
    Nop(0, K, 52);                                   /* K: named */
    Insn(0, K + 2, OP_RTS, 0, 0x060EFEF0, 56);
    Nop(0, K + 4, 60);
    Nop(0, H + 6, 64);                               /* back in the handler: the owner is unknown */
    Nop(0, H + 8, 68);
    Nop(0, H + 10, 72);
    CHECK_EQ(SeExportCurrentFunction(0), 0u);
    CHECK_EQ(Cycles(FnKey(K, 0)), 12);               /* K's first insn [52,56), its rts and slot [56,64) */
    CHECK_EQ(Cycles(RawKey(H, 0)), (52 - 40) + (72 - 64));   /* the handler before K ran, and after it returned */
    CHECK_EQ(Cycles(FnKey(A, 0)), 40 - 8);           /* A is charged up to the handler's first presentation, never after */
}

/* A return that does not land where its frame said (a manipulated return address, a longjmp-like unwind)
 * proves nothing about the parent. */
static void TestReturnToTheWrongAddress(void)
{
    const unsigned int A = 0x06001800u, B = 0x06002000u, ELSEWHERE = 0x06004000u;
    Fresh(1);
    Insn(0, U_PC, BsrTo(U_PC, A), 0, 0x060F0000, 0);
    Nop(0, U_PC + 2, 4);
    Insn(0, A, BsrTo(A, B), 0, 0x060EFFF0, 8);       /* A (called from unknown main) calls B: B's parent A is known */
    Nop(0, A + 2, 12);
    Insn(0, B, OP_RTS, 0, 0x060EFFE0, 16);
    Nop(0, B + 2, 20);
    Nop(0, ELSEWHERE, 24);                           /* ... but B returned somewhere else entirely */
    Nop(0, ELSEWHERE + 2, 28);
    CHECK_EQ(SeExportCurrentFunction(0), 0u);        /* not A: the frame's return address was not honoured */
    CHECK_EQ(Cycles(RawKey(ELSEWHERE, 0)), 4);
}

/* A frame recorded before observation was lost cannot restore ownership later, even when a return happens
 * to land on its return address (the call it describes may have returned unseen long ago). */
static void TestStaleFrameCannotRestoreOwnership(void)
{
    const unsigned int A = 0x06001800u, B = 0x06002000u;
    Fresh(1);
    Insn(0, U_PC, BsrTo(U_PC, A), 0, 0x060F0000, 0);
    Nop(0, U_PC + 2, 4);
    Insn(0, A, BsrTo(A, B), 0, 0x060EFFF0, 8);       /* stack [A, B]: B's parent A is known */
    Nop(0, A + 2, 12);
    Nop(0, B, 16);
    SeMdfnApplyMode(0);                              /* observation is lost ... */
    SeMdfnApplyMode(SE_LIVE_PROF_FUNCTIONS);         /* ... and resumes */
    Insn(0, B + 2, OP_RTS, 0, 0x060EFFE0, 100);      /* some rts (B's, or the next invocation's: unprovable) */
    Nop(0, B + 4, 104);
    Nop(0, A + 4, 108);                              /* lands exactly on B's recorded return address */
    Nop(0, A + 6, 112);
    CHECK_EQ(SeExportCurrentFunction(0), 0u);
    CHECK_EQ(Cycles(FnKey(A, 0)), 16 - 8);           /* only what was seen */
    CHECK_EQ(Cycles(FnKey(B, 0)), 0);
    CHECK_EQ(Cycles(RawKey(B, 0)), 108 - 100);       /* the rts and its slot, unnamed */
    CHECK_EQ(Cycles(RawKey(A, 0)), 112 - 108);       /* back on the recorded return address: still unproven */
}

/* ====================== a reset of one CPU leaves the other's pending flow alone ====================== */

/* CPU 'flow' runs observed code while the OTHER CPU is reset (SH7095::Reset by the watchdog, or the slave's
 * SetActive) between the flow instruction and its delay slot / landing. The reset invalidates the other CPU's
 * flow only; this CPU never lost observation. */
static void ResetPendingCall(int flow)
{
    const unsigned int A = 0x06001800u, B = 0x06002000u;
    Fresh(1);
    Insn(flow, U_PC, BsrTo(U_PC, A), 0, 0x060F0000, 0);        /* unknown code calls A */
    Nop(flow, U_PC + 2, 4);
    Insn(flow, A, BsrTo(A, B), 0, 0x060EFFF0, 8);              /* A calls B (presented at 8) */
    Nop(flow, A + 2, 12);                                      /* its delay slot */
    SeMednafenCpuReset(flow ? 0 : 1, 12);                      /* the OTHER cpu is reset while B's call is pending */
    Nop(flow, B, 16);
    Nop(flow, B + 2, 20);
    Nop(flow, B + 4, 24);
    CHECK_EQ(Cycles(FnKey(A, flow)), 8);                       /* [8,16): the call and its slot */
    CHECK_EQ(Cycles(FnKey(B, flow)), 8);                       /* [16,24) */
    CHECK(SeExportCurrentFunction(flow) == B);
}

static void ResetPendingReturn(int flow)
{
    const unsigned int A = 0x06001800u, B = 0x06002000u;
    Fresh(1);
    Insn(flow, U_PC, BsrTo(U_PC, A), 0, 0x060F0000, 0);
    Nop(flow, U_PC + 2, 4);
    Insn(flow, A, BsrTo(A, B), 0, 0x060EFFF0, 8);
    Nop(flow, A + 2, 12);
    Insn(flow, B, OP_RTS, 0, 0x060EFFE0, 16);                  /* B's rts */
    Nop(flow, B + 2, 20);                                      /* its delay slot */
    SeMednafenCpuReset(flow ? 0 : 1, 20);                      /* the other cpu is reset with the return pending */
    Nop(flow, A + 4, 24);                                      /* lands on the return address: A again */
    Nop(flow, A + 6, 32);
    CHECK_EQ(Cycles(FnKey(B, flow)), 8);                       /* [16,24): rts and slot */
    CHECK_EQ(Cycles(FnKey(A, flow)), (16 - 8) + (32 - 24));
    CHECK(SeExportCurrentFunction(flow) == A);
}

static void TestCrossCpuReset(void)
{
    ResetPendingCall(0);     /* master's pending call, slave reset */
    ResetPendingCall(1);     /* slave's pending call, master reset */
    ResetPendingReturn(0);
    ResetPendingReturn(1);
}

/* The reset CPU's OWN pending flow is still dropped. */
static void TestOwnResetDropsOwnFlow(void)
{
    const unsigned int A = 0x06001800u;
    Fresh(1);
    Insn(0, U_PC, BsrTo(U_PC, A), 0, 0x060F0000, 0);
    Nop(0, U_PC + 2, 4);
    SeMednafenCpuReset(0, 4);
    Nop(0, A, 8);
    Nop(0, A + 2, 12);
    CHECK(SeExportCurrentFunction(0) == 0u);                   /* the call was not applied after the reset */
}

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

/* ====================== F9 follow-up: unconditional jumps (bra / jmp @Rn / braf) ======================
 *
 * The rules under test (se_export.c, SeExportUncondJumpLanded). An unconditional jump COMPLETES at its
 * landing, after its delay slot, and only matters while ownership is known and the innermost frame is a
 * call frame (E = that frame's function):
 *   1. the landing is a KNOWN ENTRY (the target of an observed call on this CPU)  -> tail call: the
 *      frame's function becomes the landing, no frame is pushed or popped (its return address and the
 *      caller below it are untouched, so the callee's rts returns to the original caller);
 *   2. the landing is E itself or lies in [E, hi(E)], hi = the furthest PC executed while E was owner
 *      -> the function's own code, ownership is kept;
 *   3. otherwise, if the registers at the landing show the function still inside its body (SP is not the
 *      SP the call saw, or PR is not the return address the call set) and the landing lies in
 *      (hi(E), hi(E) + 0x1000] with no known entry in (E, landing] -> a switch / forward skip, kept;
 *   4. anything else -> unknown (raw PC bucket), never a name. */
#define OP_BRA(d)   ((unsigned short)(0xA000u | ((d) & 0x0FFFu)))
#define OP_BRAF_R0  0x0023u
static unsigned short BraTo(unsigned int pc, unsigned int target)
{
    return OP_BRA(((int)(target - (pc + 4u)) / 2) & 0x0FFF);
}

#define MAIN 0x06001000u
#define PP   0x06001800u
#define AA   0x06002000u
#define BB   0x06003000u
#define SP0  0x060F0000u
enum { J_BRA = 0, J_JMP = 1, J_BRAF = 2 };

static int gT;
static void I(int cpu, unsigned int pc, unsigned short op, unsigned int rn, unsigned int sp) { Insn(cpu, pc, op, rn, sp, gT); gT += 4; }
static void N(int cpu, unsigned int pc) { I(cpu, pc, OP_NOP, 0, 0); }
static void Regs(int cpu, unsigned int sp, unsigned int pr) { sSeMdfnTestSp[cpu] = sp; sSeMdfnTestPr[cpu] = pr; }
static void Call(int cpu, unsigned int pc, unsigned int callee, unsigned int sp)
{
    I(cpu, pc, 0x400Bu, callee, sp);                   /* jsr @R0: any distance, unlike bsr's +-4 KB */
    N(cpu, pc + 2);                                    /* its delay slot */
}
static void Jump(int cpu, int how, unsigned int pc, unsigned int target, unsigned int sp)
{
    if (how == J_BRA) I(cpu, pc, BraTo(pc, target), 0, sp);
    else if (how == J_JMP) I(cpu, pc, OP_JMP_R0, target, sp);
    else I(cpu, pc, OP_BRAF_R0, target - (pc + 4u), sp);
    N(cpu, pc + 2);                                    /* its delay slot */
}
static void Ret(int cpu, unsigned int pc, unsigned int sp)
{
    I(cpu, pc, OP_RTS, 0, sp);
    N(cpu, pc + 2);
}
/* 'fn' is entered by an observed call and returns at once: afterwards it is a known entry. Leaves the stack empty. */
static void MakeKnown(int cpu, unsigned int fn)
{
    Call(cpu, MAIN, fn, SP0);
    N(cpu, fn);
    Ret(cpu, fn + 2, SP0);
}
static unsigned int Depth(int cpu)
{
    unsigned char b[4 + SE_LIVE_CALLSTACK_MAX * SE_LIVE_CALLFRAME_LEN];
    SeExportSerializeCallStack(cpu, b);
    return (unsigned int)b[0] | ((unsigned int)b[1] << 8) | ((unsigned int)b[2] << 16) | ((unsigned int)b[3] << 24);
}
/* The function of the i-th frame from the top of the recorded stack. */
static unsigned int FrameFn(int cpu, unsigned int i)
{
    unsigned char b[4 + SE_LIVE_CALLSTACK_MAX * SE_LIVE_CALLFRAME_LEN];
    const unsigned char* f = b + 4 + i * SE_LIVE_CALLFRAME_LEN + 4;
    SeExportSerializeCallStack(cpu, b);
    return (unsigned int)f[0] | ((unsigned int)f[1] << 8) | ((unsigned int)f[2] << 16) | ((unsigned int)f[3] << 24);
}
static unsigned int Named(int cpu) { return SeExportCurrentFunction(cpu); }

/* A tail call to a function that an observed call has entered before: the callee owns the rest, the delay
 * slot stays with the jumper, the stack depth is unchanged, and the callee's rts restores the ORIGINAL
 * caller (the frame below the replaced one), which is therefore named again. */
static void TailKnown(int cpu, int how)
{
    Fresh(1); gT = 0;
    MakeKnown(cpu, BB);
    N(cpu, MAIN + 4);
    Call(cpu, MAIN + 6, PP, SP0);
    N(cpu, PP);
    Call(cpu, PP + 2, AA, SP0 - 16);                   /* P (called from unknown main) calls A */
    N(cpu, AA);
    CHECK_EQ(Depth(cpu), 2);
    CHECK_EQ(Named(cpu), AA);
    Regs(cpu, SP0 - 16, PP + 6);                       /* A's epilogue is done: the state it was entered in */
    Jump(cpu, how, AA + 2, BB, SP0 - 16);              /* A: tail jump to B; the slot is A's */
    N(cpu, BB);                                        /* B's first instruction */
    CHECK_EQ(Named(cpu), BB);
    CHECK_EQ(Depth(cpu), 2);                           /* replaced, not pushed */
    CHECK_EQ(FrameFn(cpu, 0), BB);
    CHECK_EQ(FrameFn(cpu, 1), PP);
    Ret(cpu, BB + 2, SP0 - 16);                        /* B returns: to P, not to A */
    N(cpu, PP + 6);
    CHECK_EQ(Depth(cpu), 1);
    CHECK_EQ(Named(cpu), PP);                          /* the original caller is proven again */
    N(cpu, PP + 8);

    CHECK_EQ(Cycles(FnKey(AA, cpu)), 12);              /* A, the jump, its delay slot */
    CHECK_EQ(Cycles(FnKey(BB, cpu)), 12 + 12);         /* the earlier call (first, rts, slot) + the tail callee (first, rts, slot) */
    CHECK_EQ(Cycles(RawKey(BB, cpu)), 0);
}
static void TestTailCallToKnownEntry(void)
{
    int cpu, how;
    for (cpu = 0; cpu < 2; ++cpu)
        for (how = J_BRA; how <= J_BRAF; ++how)
            TailKnown(cpu, how);
}

/* A function that recurses and tail-calls: stack [P, A, A'], A' tail-jumps to the known B. B's return lands in A
 * (the recursion's return address), A's own return lands in P. Also a jump to the function's OWN entry. */
static void TestRecursionAndTail(void)
{
    int cpu;
    for (cpu = 0; cpu < 2; ++cpu)
    {
        Fresh(1); gT = 0;
        MakeKnown(cpu, BB);
        Call(cpu, MAIN + 4, PP, SP0);
        N(cpu, PP);
        Call(cpu, PP + 2, AA, SP0 - 16);               /* P -> A */
        N(cpu, AA);
        Call(cpu, AA + 2, AA, SP0 - 32);               /* A -> A (recursion): A is a known entry now */
        N(cpu, AA);
        CHECK_EQ(Depth(cpu), 3);
        Regs(cpu, SP0 - 32, AA + 6);
        Jump(cpu, J_JMP, AA + 4, AA, SP0 - 32);        /* A' jumps to its own entry: stays A', no frame change */
        N(cpu, AA);
        CHECK_EQ(Depth(cpu), 3);
        CHECK_EQ(Named(cpu), AA);
        Jump(cpu, J_BRA, AA + 4, BB, SP0 - 32);        /* A' tail-calls B */
        N(cpu, BB);
        CHECK_EQ(Depth(cpu), 3);
        CHECK_EQ(FrameFn(cpu, 0), BB);
        CHECK_EQ(FrameFn(cpu, 1), AA);
        Ret(cpu, BB + 2, SP0 - 32);
        N(cpu, AA + 6);                                /* back in the outer A */
        CHECK_EQ(Depth(cpu), 2);
        CHECK_EQ(Named(cpu), AA);
        Ret(cpu, AA + 8, SP0 - 16);
        N(cpu, PP + 6);
        CHECK_EQ(Depth(cpu), 1);
        CHECK_EQ(Named(cpu), PP);
    }
}

/* A tail call whose target no observed call has ever entered is not named: it is raw until a return proves
 * the original caller (A must not keep the name, and G is not given one it has not earned). */
static void TestTailCallToUnseenFunction(void)
{
    const unsigned int G = AA + 0x200u;
    int cpu, how;
    for (cpu = 0; cpu < 2; ++cpu)
        for (how = J_BRA; how <= J_BRAF; ++how)
        {
            Fresh(1); gT = 0;
            Call(cpu, MAIN, PP, SP0);
            N(cpu, PP);
            Call(cpu, PP + 2, AA, SP0 - 16);
            N(cpu, AA);
            Regs(cpu, SP0 - 16, PP + 6);               /* the state a tail call is made in */
            Jump(cpu, how, AA + 2, G, SP0 - 16);
            N(cpu, G);
            N(cpu, G + 2);
            CHECK_EQ(Named(cpu), 0u);
            Ret(cpu, G + 4, SP0 - 16);
            N(cpu, PP + 6);
            CHECK_EQ(Named(cpu), PP);                  /* its rts went to P through A's frame */
            N(cpu, PP + 8);
            CHECK_EQ(Cycles(FnKey(AA, cpu)), 12);
            CHECK_EQ(Cycles(FnKey(G, cpu)), 0);
            CHECK_EQ(Cycles(RawKey(G, cpu)), 4 * 4);   /* G's two instructions, its rts and slot */
        }
}

/* A jump out of range (far away, below the entry) or past a known entry is raw, even with the registers of a
 * function that is still inside its body. */
static void TestJumpOutsideAnyEvidence(void)
{
    const unsigned int X = AA + 0x80u, NEAR_BEYOND_X = AA + 0x100u, FARAWAY = 0x06009000u, BELOW = AA - 0x40u;
    int cpu, how;
    for (cpu = 0; cpu < 2; ++cpu)
        for (how = J_BRA; how <= J_BRAF; ++how)
        {
            unsigned int targets[3], k;
            targets[0] = FARAWAY; targets[1] = BELOW; targets[2] = NEAR_BEYOND_X;
            for (k = 0; k < 3; ++k)
            {
                if (how == J_BRA && k == 0) continue;   /* bra reaches +-4 KB only */
                Fresh(1); gT = 0;
                MakeKnown(cpu, X);                     /* a function starts at A+0x80 */
                Call(cpu, MAIN + 4, AA, SP0);
                N(cpu, AA);
                Regs(cpu, SP0 - 48, 0x06009999u);      /* mid-body: frame still live, PR clobbered by an inner call */
                Jump(cpu, how, AA + 2, targets[k], SP0 - 48);
                N(cpu, targets[k]);
                CHECK_EQ(Named(cpu), 0u);
                CHECK_EQ(Cycles(FnKey(targets[k], cpu)), 0);
                N(cpu, targets[k] + 2);
                CHECK_EQ(Cycles(FnKey(AA, cpu)), 12);  /* A, the jump, its slot: nothing after */
            }
        }
}

/* A switch inside a function that is still in its body: the dispatch (jmp @Rn, braf, and the forward bra)
 * keeps ownership, a second dispatch to an address the function already ran does so too, and every cycle
 * is charged to the function. */
static void TestSwitchInsideFunction(void)
{
    int cpu, how;
    for (cpu = 0; cpu < 2; ++cpu)
        for (how = J_BRA; how <= J_BRAF; ++how)
        {
            Fresh(1); gT = 0;
            Call(cpu, MAIN, PP, SP0);
            N(cpu, PP);
            Call(cpu, PP + 2, AA, SP0 - 16);
            N(cpu, AA);
            N(cpu, AA + 2);
            Regs(cpu, SP0 - 48, 0x06009999u);          /* frame live, PR clobbered */
            Jump(cpu, how, AA + 4, AA + 0x40u, SP0 - 48);   /* case 1: forward, never run before */
            N(cpu, AA + 0x40u);
            CHECK_EQ(Named(cpu), AA);
            N(cpu, AA + 0x42u);
            Jump(cpu, how, AA + 0x44u, AA + 0x10u, SP0 - 48);   /* back to code already run */
            N(cpu, AA + 0x10u);
            CHECK_EQ(Named(cpu), AA);
            Ret(cpu, AA + 0x12u, SP0 - 48);
            N(cpu, PP + 6);
            CHECK_EQ(Named(cpu), PP);
            CHECK_EQ(Depth(cpu), 1);
            /* A, A+2, jump, slot, case, case+2, jump, slot, A+0x10, rts, slot = 11 instructions */
            CHECK_EQ(Cycles(FnKey(AA, cpu)), 11 * 4);
            CHECK_EQ(Cycles(RawKey(AA, cpu)), 0);
            CHECK_EQ(Cycles(RawKey(AA + 0x40u, cpu)), 0);
        }
}

/* A frameless function (SP and PR as the call left them) looks exactly like a tail call, so a forward jump to
 * code it has never run is NOT assumed to be its own: raw. Code it already ran is proven by that. */
static void TestFramelessForwardJumpIsRaw(void)
{
    int cpu, how;
    for (cpu = 0; cpu < 2; ++cpu)
        for (how = J_BRA; how <= J_BRAF; ++how)
        {
            Fresh(1); gT = 0;
            Call(cpu, MAIN, AA, SP0);
            N(cpu, AA);
            N(cpu, AA + 2);
            Regs(cpu, SP0, MAIN + 4);
            Jump(cpu, how, AA + 4, AA, SP0);           /* a loop back to its own entry: proven */
            N(cpu, AA);
            CHECK_EQ(Named(cpu), AA);
            N(cpu, AA + 2);
            Jump(cpu, how, AA + 4, AA + 2, SP0);       /* back to code already run: proven */
            N(cpu, AA + 2);
            CHECK_EQ(Named(cpu), AA);
            Jump(cpu, how, AA + 4, AA + 0x20u, SP0);   /* forward, never run, could be anybody's */
            N(cpu, AA + 0x20u);
            CHECK_EQ(Named(cpu), 0u);
            N(cpu, AA + 0x22u);
            N(cpu, AA + 0x24u);
            CHECK_EQ(Cycles(RawKey(AA + 0x20u, cpu)), 8);
        }
}

/* An interrupt taken between the jump and its target (before the delay slot, or after it): the target is not
 * what runs next, so nothing is named, and the handler's unobserved rte does not name anything either. */
static void TestInterruptAroundTailJump(void)
{
    const unsigned int H = 0x06008000u;
    int cpu, when;
    for (cpu = 0; cpu < 2; ++cpu)
        for (when = 0; when < 2; ++when)
        {
            Fresh(1); gT = 0;
            MakeKnown(cpu, BB);
            Call(cpu, MAIN + 4, AA, SP0);
            N(cpu, AA);
            Regs(cpu, SP0, MAIN + 8);
            I(cpu, AA + 2, BraTo(AA + 2, BB), 0, SP0);
            if (when == 0) SeMednafenExceptionEntry(cpu);   /* taken before the delay slot */
            else N(cpu, AA + 4);
            if (when == 1) SeMednafenExceptionEntry(cpu);   /* taken after it, before the target */
            N(cpu, H);
            N(cpu, H + 2);
            I(cpu, H + 4, OP_RTE, 0, SP0 - 8);
            N(cpu, H + 6);
            N(cpu, BB);                                     /* the interrupted jump finally lands */
            N(cpu, BB + 2);
            CHECK_EQ(Named(cpu), 0u);
            CHECK_EQ(Cycles(FnKey(BB, cpu)), 12);           /* only B's earlier, genuinely observed call */
            CHECK_EQ(Depth(cpu), 1);                        /* the stack is kept */
            Ret(cpu, BB + 4, SP0);
            N(cpu, MAIN + 8);
            CHECK_EQ(Depth(cpu), 0);
        }
}

/* Ownership already lost (an interrupt, then unknown code running on top of the recorded frame): a jump to a
 * known entry proves nothing about who jumped, so it names nothing and leaves the frame as it is. */
static void TestJumpFromUnknownOwner(void)
{
    const unsigned int H = 0x06008000u;
    int cpu;
    for (cpu = 0; cpu < 2; ++cpu)
    {
        Fresh(1); gT = 0;
        MakeKnown(cpu, BB);
        Call(cpu, MAIN + 4, AA, SP0);
        N(cpu, AA);
        SeMednafenExceptionEntry(cpu);
        N(cpu, H);
        Regs(cpu, SP0, MAIN + 8);
        Jump(cpu, J_JMP, H + 2, BB, SP0);
        N(cpu, BB);
        CHECK_EQ(Named(cpu), 0u);
        CHECK_EQ(FrameFn(cpu, 0), AA);
        CHECK_EQ(Depth(cpu), 1);
    }
}

/* Entries are per CPU: B is known on the master only, so the slave's identical jump is raw. */
static void TestEntriesArePerCpu(void)
{
    Fresh(1); gT = 0;
    MakeKnown(0, BB);
    Call(0, MAIN + 4, AA, SP0);
    Call(1, MAIN, AA, SP0);
    N(0, AA);
    N(1, AA);
    Regs(0, SP0, MAIN + 8);
    Regs(1, SP0, MAIN + 4);
    Jump(0, J_BRA, AA + 2, BB, SP0);
    Jump(1, J_BRA, AA + 2, BB, SP0);
    N(0, BB);
    N(1, BB);
    CHECK_EQ(Named(0), BB);
    CHECK_EQ(Named(1), 0u);
    CHECK_EQ(Cycles(FnKey(BB, 1)), 0);
}

/* What the recorded entries describe is dropped with the stack: a CPU reset, a state load. */
static void TestEntriesDroppedOnResetAndLoad(void)
{
    int cpu, how;
    for (cpu = 0; cpu < 2; ++cpu)
        for (how = 0; how < 2; ++how)
        {
            Fresh(1); gT = 0;
            MakeKnown(cpu, BB);
            if (how == 0) SeMednafenCpuReset(cpu, gT);
            else SeMednafenStateLoaded(0);
            Call(cpu, MAIN + 4, AA, SP0);
            N(cpu, AA);
            Regs(cpu, SP0, MAIN + 8);
            Jump(cpu, J_JMP, AA + 2, BB, SP0);
            N(cpu, BB);
            CHECK_EQ(Named(cpu), 0u);
        }
}

/* The innermost frame is an exception frame (a trapa handler): its function is not a subroutine that can be
 * tail-replaced, so a jump from it to a known entry names nothing and the frame stays. */
static void TestJumpFromTrapaHandler(void)
{
    const unsigned int H = 0x06008000u;
    Fresh(1); gT = 0;
    MakeKnown(0, BB);
    Call(0, MAIN + 4, AA, SP0);
    N(0, AA);
    SeMdfnFlowDefer(0, AA + 2, SeFlowTrap, OP_TRAPA(5), 0, SP0, H);
    SeMdfnProfSampleAt(0, AA + 2, gT); gT += 4;
    SeMednafenExceptionEntry(0);
    N(0, H);
    CHECK_EQ(Named(0), H);
    CHECK_EQ(Depth(0), 2);
    Regs(0, SP0 - 8, AA + 4);
    Jump(0, J_JMP, H + 2, BB, SP0 - 8);
    N(0, BB);
    CHECK_EQ(Named(0), 0u);
    CHECK_EQ(Depth(0), 2);
    CHECK_EQ(FrameFn(0, 0), H);
}

/* The landing is not where the jump said (the hook missed something, or it was a different path entirely):
 * not a completed jump to its target. */
static void TestLandingMismatch(void)
{
    const unsigned int C = AA + 0x300u;
    Fresh(1); gT = 0;
    MakeKnown(0, BB);
    Call(0, MAIN + 4, AA, SP0);
    N(0, AA);
    Regs(0, SP0 - 32, 0x06009999u);                    /* mid-body: a landing near A would be believed */
    Jump(0, J_JMP, AA + 2, BB, SP0);
    N(0, C);                                           /* something else ran next */
    CHECK_EQ(Named(0), 0u);
}

/* More distinct entries than the table holds: the ones that did not fit are simply never known (raw), the
 * ones that did still are. No crash, and no push after the table filled changes what it holds. A is running
 * and calls 1500 distinct functions in turn (each returns at once into A, which stays the owner), then jumps. */
static unsigned int FillTableInsideA(unsigned int* lastOut)
{
    const unsigned int first = 0x06100000u;
    unsigned int i, pc = AA + 2, last = first;
    Fresh(1); gT = 0;
    Call(0, MAIN, AA, SP0);
    N(0, AA);
    for (i = 0; i < 1500; ++i, pc += 4)
    {
        last = first + i * 0x40u;
        Call(0, pc, last, SP0);
        N(0, last);
        Ret(0, last + 2, SP0);
    }
    N(0, pc);                                          /* back in A after the last return */
    CHECK_EQ(Named(0), AA);
    Regs(0, SP0, MAIN + 4);
    *lastOut = last;
    return pc + 2;                                     /* a free pc after that landing */
}
static void TestEntryTableFull(void)
{
    unsigned int last, pc = FillTableInsideA(&last);
    Jump(0, J_JMP, pc, 0x06100000u, SP0);              /* an early entry fitted: still a tail call */
    N(0, 0x06100000u);
    CHECK_EQ(Named(0), 0x06100000u);
    pc = FillTableInsideA(&last);
    Jump(0, J_JMP, pc, last, SP0);                     /* the last one did not fit: never named by a jump */
    N(0, last);
    CHECK_EQ(Named(0), 0u);
}

int main(void)
{
    TestSlaveOnTimeAndGap();
    TestRebaseRedundantAndReopen();
    TestStateRestore();
    TestWatchdogReset();
    TestIdleAndFrameBoundary();
    TestPositiveOracle();
    TestKnownParentReturn();
    TestInterrupt();
    TestTailJump();
    TestInterruptedCall();
    TestTrapa();
    TestNestedCallFromUnknown();
    TestReturnToTheWrongAddress();
    TestStaleFrameCannotRestoreOwnership();
    TestLostObservation();
    TestHookModeLoss();
    TestCrossCpuReset();
    TestOwnResetDropsOwnFlow();
    TestModeLatch();
    TestTailCallToKnownEntry();
    TestRecursionAndTail();
    TestTailCallToUnseenFunction();
    TestJumpOutsideAnyEvidence();
    TestSwitchInsideFunction();
    TestFramelessForwardJumpIsRaw();
    TestInterruptAroundTailJump();
    TestJumpFromUnknownOwner();
    TestEntriesArePerCpu();
    TestEntriesDroppedOnResetAndLoad();
    TestJumpFromTrapaHandler();
    TestLandingMismatch();
    TestEntryTableFull();
    if (gFailures) { fprintf(stderr, "%d failure(s)\n", gFailures); return 1; }
    printf("ProfilerLifecycleTests: all passed\n");
    return 0;
}
