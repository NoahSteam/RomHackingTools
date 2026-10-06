/* Shadow call stack (v9) — the emulator-side recording model, driven without an emulator.
 * The tracker is a pure function of the instruction stream plus R15, so a synthetic
 * stream exercises the real glue code against the real se_export stack. se_export.h
 * documents the rules being pinned here; the case that matters is the interrupt.
 *
 * The glue has no header (apply.py copies the single .c into the emulator tree), so
 * include it — unwired, it pulls in no emulator symbols, and its file-scope tracker comes
 * into scope with it. */
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

/* ---- the synthetic instruction stream --------------------------------------------- */

#define OP_RTS        0x000Bu
#define OP_RTE        0x002Bu
#define OP_JMP_R0     0x402Bu                       /* jmp @R0 — a tail call */
#define OP_BRAF_R0    0x0023u                       /* braf R0 — a plain branch */
#define OP_BSR(d)     ((unsigned short)(0xB000u | ((d) & 0x0FFFu)))
#define OP_BSRF(n)    ((unsigned short)(0x0003u | ((n) << 8)))
#define OP_JSR(n)     ((unsigned short)(0x400Bu | ((n) << 8)))
#define OP_TRAPA(i)   ((unsigned short)(0xC300u | ((i) & 0xFFu)))

static void Step(int cpu, unsigned int pc, unsigned short op, unsigned int rn, unsigned int sp)
{
    SeMdfnApplyFlow(cpu, SeMdfnClassifyFlow(op), pc, op, rn, sp, 0);
}

/* An interrupt taken at R15 = 'sp'. Entry is invisible to the hook — the hardware pushes
 * SR and PC and vectors through VBR without executing anything — so the whole of it is
 * the handler running eight bytes lower and returning with rte at that R15. */
static void Interrupt(int cpu, unsigned int handlerPc, unsigned int sp)
{
    Step(cpu, handlerPc, OP_RTE, 0, sp - 8);
}

/* ---- reading the recorded stack back ----------------------------------------------- */

typedef struct { unsigned int callSite, func, ret, sp, frameNo; unsigned long long cycle; } Frame;

static unsigned char gBlock[SE_LIVE_CALLSTACK_BLOCK_MAX];
static unsigned int  gCount;

/* Serialize 'cpu' and return the reported frame count, leaving the block in gBlock. */
static unsigned int Snapshot(int cpu)
{
    const unsigned int len = SeExportSerializeCallStack(cpu, gBlock);
    gCount = SeRd32LE(gBlock);
    CHECK(len == 4u + gCount * SE_LIVE_CALLFRAME_LEN);
    return gCount;
}

/* Frame 'i' of the last Snapshot, innermost first. */
static Frame At(unsigned int i)
{
    Frame f;
    const unsigned char* p = gBlock + 4 + i * SE_LIVE_CALLFRAME_LEN;
    memset(&f, 0, sizeof f);
    CHECK(i < gCount);
    if (i >= gCount) return f;
    f.callSite = SeRd32LE(p);
    f.func     = SeRd32LE(p + 4);
    f.ret      = SeRd32LE(p + 8);
    f.sp       = SeRd32LE(p + 12);
    f.cycle    = (unsigned long long)SeRd32LE(p + 16) |
                 ((unsigned long long)SeRd32LE(p + 20) << 32);
    f.frameNo  = SeRd32LE(p + 24);
    return f;
}

static void Reset(void)
{
    SeExportResetCallStack(0);
    SeExportResetCallStack(1);
}

/* ---- tests -------------------------------------------------------------------------- */

/* Each call form resolves the target the SH-2 would, and the frame reaches the wire
 * innermost-first with the fields the client reads. */
static void TestCallFormsAndSerialization(void)
{
    Reset();
    Step(0, 0x06001000, OP_BSR(0x010), 0, 0x060FFF00);    /* target = PC + 4 + 0x10*2 */
    Step(0, 0x06002000, OP_BSRF(3), 0x400, 0x060FFEF0);   /* target = PC + 4 + R3 */
    Step(0, 0x06003000, OP_JSR(5), 0x06008000, 0x060FFEE0); /* target = R5 */

    CHECK(Snapshot(0) == 3);
    CHECK(At(0).callSite == 0x06003000u);                 /* innermost first */
    CHECK(At(0).func == 0x06008000u);
    CHECK(At(0).ret == 0x06003004u);                      /* one past the delay slot */
    CHECK(At(0).sp == 0x060FFEE0u);
    CHECK(At(1).callSite == 0x06002000u);
    CHECK(At(1).func == 0x06002000u + 4u + 0x400u);
    CHECK(At(2).callSite == 0x06001000u);
    CHECK(At(2).func == 0x06001000u + 4u + 0x20u);

    Step(0, 0x06008010, OP_RTS, 0, 0x060FFEE0);
    CHECK(Snapshot(0) == 2);
    CHECK(At(0).callSite == 0x06002000u);
}

/* bsr's displacement is signed: a backward call must not resolve above the call site. */
static void TestBackwardBsr(void)
{
    Reset();
    Step(0, 0x06001000, OP_BSR(0xFF0), 0, 0x060FFF00);    /* disp = -16 -> PC + 4 - 32 */
    CHECK(Snapshot(0) == 1);
    CHECK(At(0).func == 0x06001000u + 4u - 32u);
}

/* THE REGRESSION. Interrupts arrive between any two instructions and their handlers end
 * with rte. The recorded call frames must survive all of them, untouched. */
static void TestInterruptsDoNotDrainTheStack(void)
{
    int i;
    Reset();
    Step(0, 0x06001000, OP_BSR(0x010), 0, 0x060FFF00);
    Step(0, 0x06002000, OP_BSR(0x010), 0, 0x060FFEF0);
    Step(0, 0x06003000, OP_BSR(0x010), 0, 0x060FFEE0);
    CHECK(Snapshot(0) == 3);

    for (i = 0; i < 200; ++i) Interrupt(0, 0x06000400, 0x060FFEE0);

    CHECK(Snapshot(0) == 3);
    CHECK(At(0).callSite == 0x06003000u);
    CHECK(At(1).callSite == 0x06002000u);
    CHECK(At(2).callSite == 0x06001000u);

    /* And an interrupt taken at depth 0 must not underflow into anything. */
    Step(0, 0x06003010, OP_RTS, 0, 0x060FFEE0);
    Step(0, 0x06002010, OP_RTS, 0, 0x060FFEF0);
    Step(0, 0x06001010, OP_RTS, 0, 0x060FFF00);
    CHECK(Snapshot(0) == 0);
    for (i = 0; i < 10; ++i) Interrupt(0, 0x06000400, 0x060FFF00);
    CHECK(Snapshot(0) == 0);
}

/* An interrupt handler runs with R15 eight bytes below where the interrupt was taken, so
 * sooner or later that lands exactly on the R15 some recorded call was made at. The SP is
 * a tie-breaker between exception frames, not a licence to unwind a call frame. */
static void TestRteDoesNotUnwindACallFrameWithAMatchingSp(void)
{
    Reset();
    Step(0, 0x06001000, OP_BSR(0x010), 0, 0x060FFEE8);   /* call made at this R15 ... */
    CHECK(Snapshot(0) == 1);
    Interrupt(0, 0x06000400, 0x060FFEF0);                /* ... handler runs at R15 - 8 */
    CHECK(Snapshot(0) == 1);
    CHECK(At(0).callSite == 0x06001000u);
}

/* A handler's own calls are ordinary calls: they push above the interrupted program's
 * frames and unwind completely before the handler returns. */
static void TestHandlerCallsBalance(void)
{
    Reset();
    Step(0, 0x06001000, OP_BSR(0x010), 0, 0x060FFF00);
    Step(0, 0x06000400, OP_JSR(2), 0x06000500, 0x060FFEF8);   /* inside the handler */
    CHECK(Snapshot(0) == 2);
    Step(0, 0x06000520, OP_RTS, 0, 0x060FFEF8);
    Step(0, 0x06000410, OP_RTE, 0, 0x060FFEF8);
    CHECK(Snapshot(0) == 1);
    CHECK(At(0).callSite == 0x06001000u);
}

/* trapa IS visible (it is an instruction), so it gets a frame, and its rte — identified
 * by the R15 the entry left behind — unwinds exactly that frame. */
static void TestTrapaPairsWithItsRte(void)
{
    Reset();
    Step(0, 0x06001000, OP_BSR(0x010), 0, 0x060FFF00);
    Step(0, 0x06001100, OP_TRAPA(0x20), 0, 0x060FFEF0);
    CHECK(Snapshot(0) == 2);
    CHECK(At(0).callSite == 0x06001100u);
    CHECK(At(0).ret == 0x06001102u);              /* trapa has no delay slot */
    CHECK(At(0).sp == 0x060FFEF0u - 8u);          /* SR + PC pushed by the entry */

    Step(0, 0x06000700, OP_RTE, 0, 0x060FFEF0u - 8u);
    CHECK(Snapshot(0) == 1);
    CHECK(At(0).callSite == 0x06001000u);
}

/* An interrupt taken inside a trapa handler returns with a different R15, so its rte must
 * not be mistaken for the trapa's own. */
static void TestNestedInterruptDoesNotStealTheTrapFrame(void)
{
    Reset();
    Step(0, 0x06001100, OP_TRAPA(0x20), 0, 0x060FFEF0);
    CHECK(Snapshot(0) == 1);

    Interrupt(0, 0x06000400, 0x060FFEF0u - 8u);   /* preempts the handler */
    CHECK(Snapshot(0) == 1);
    CHECK(At(0).callSite == 0x06001100u);

    Step(0, 0x06000700, OP_RTE, 0, 0x060FFEF0u - 8u);
    CHECK(Snapshot(0) == 0);
}

/* rts cannot return across an exception boundary, so meeting an exception frame means the
 * call it belongs to predates recording — leave the frame alone. */
static void TestRtsWillNotUnwindAnExceptionFrame(void)
{
    Reset();
    Step(0, 0x06001100, OP_TRAPA(0x20), 0, 0x060FFEF0);
    Step(0, 0x06000700, OP_RTS, 0, 0x060FFEE0);
    CHECK(Snapshot(0) == 1);
    CHECK(At(0).callSite == 0x06001100u);
}

/* Tail calls and plain register branches are not calls: `jmp @Rn` leaves the caller's
 * frame in place for the callee's rts to unwind, and braf touches nothing. */
static void TestTailCallAndBranchAreNotCalls(void)
{
    Reset();
    Step(0, 0x06001000, OP_BSR(0x010), 0, 0x060FFF00);
    Step(0, 0x06002000, OP_JMP_R0, 0x06009000, 0x060FFEF0);
    Step(0, 0x06002100, OP_BRAF_R0, 0x40, 0x060FFEF0);
    CHECK(Snapshot(0) == 1);
    Step(0, 0x06009010, OP_RTS, 0, 0x060FFF00);   /* the tail callee returns to the caller */
    CHECK(Snapshot(0) == 0);
}

/* Master and slave keep separate stacks. */
static void TestPerCpuStacks(void)
{
    Reset();
    Step(0, 0x06001000, OP_BSR(0x010), 0, 0x060FFF00);
    Step(1, 0x06002000, OP_BSR(0x010), 0, 0x060EFF00);
    Step(1, 0x06003000, OP_BSR(0x010), 0, 0x060EFEF0);
    CHECK(Snapshot(0) == 1);
    CHECK(At(0).callSite == 0x06001000u);
    CHECK(Snapshot(1) == 2);
    CHECK(At(0).callSite == 0x06003000u);
    SeExportResetCallStack(1);
    CHECK(Snapshot(0) == 1);
    CHECK(Snapshot(1) == 0);
}

/* Deeper than we store: the overflow is counted, so the returns that belong to those
 * frames unwind the counter instead of eating the stored ones, and the block stays capped
 * at what the wire carries. */
#define CALLSITE_AT(i) (0x06000000u + (i) * 0x10u)

static void TestOverflowDoesNotEatStoredFrames(void)
{
    unsigned int i;
    Reset();
    for (i = 0; i < SE_CALLSTACK_CAP + 3u; ++i)
        Step(0, CALLSITE_AT(i), OP_BSR(0x010), 0, 0x060FFF00u);

    CHECK(Snapshot(0) == SE_LIVE_CALLSTACK_MAX);                /* capped for the wire */
    CHECK(At(0).callSite == CALLSITE_AT(SE_CALLSTACK_CAP - 1u));/* the last one stored */

    for (i = 0; i < 3u; ++i) Step(0, CALLSITE_AT(0), OP_RTS, 0, 0x060FFF00u);
    CHECK(Snapshot(0) == SE_LIVE_CALLSTACK_MAX);
    CHECK(At(0).callSite == CALLSITE_AT(SE_CALLSTACK_CAP - 1u));/* stored frames untouched */

    Step(0, CALLSITE_AT(0), OP_RTS, 0, 0x060FFF00u);            /* now a stored frame goes */
    CHECK(Snapshot(0) == SE_LIVE_CALLSTACK_MAX);
    CHECK(At(0).callSite == CALLSITE_AT(SE_CALLSTACK_CAP - 2u));
}

/* ---- tracepoint repeat policy (v21) --------------------------------------------------- */

/* Install up to two tracepoints at consecutive addresses, as the server's TRC verb would, and let
 * the CPU side pick the new set up (what its first per-instruction call after an install does). */
static void InstallTps(unsigned int n, const unsigned int* ids, const unsigned int* flags)
{
    unsigned char d[2 * SE_LIVE_TRACE_DESC_LEN];
    unsigned int i;
    int k;
    for (i = 0; i < n; ++i)
    {
        const unsigned int words[4] = { ids[i], 0u, 0x06001000u + 2u * i, flags[i] };
        for (k = 0; k < 4; ++k)
        {
            d[i * SE_LIVE_TRACE_DESC_LEN + k * 4 + 0] = (unsigned char)(words[k]);
            d[i * SE_LIVE_TRACE_DESC_LEN + k * 4 + 1] = (unsigned char)(words[k] >> 8);
            d[i * SE_LIVE_TRACE_DESC_LEN + k * 4 + 2] = (unsigned char)(words[k] >> 16);
            d[i * SE_LIVE_TRACE_DESC_LEN + k * 4 + 3] = (unsigned char)(words[k] >> 24);
        }
    }
    SeMdfnSetTracepoints(n, d);
    SeMdfnTpSync();
}

static void InstallTp(unsigned int id, unsigned int flags) { InstallTps(1, &id, &flags); }

/* How many of 'n' executions of tracepoint 'idx' fire, and on which of them. */
static unsigned int FiresAt(unsigned int idx, unsigned int n, unsigned int* which, unsigned int whichCap)
{
    unsigned int i, fired = 0;
    for (i = 1; i <= n; ++i)
    {
        if (SeMdfnTpFires(idx))
        {
            if (fired < whichCap) which[fired] = i;
            ++fired;
        }
    }
    return fired;
}
static unsigned int Fires(unsigned int n, unsigned int* which, unsigned int whichCap)
{
    return FiresAt(0, n, which, whichCap);
}

static void TestTracepointRepeatPolicy(void)
{
    unsigned int w[8] = { 0 };

    InstallTp(1, SE_LIVE_TP_ENABLED);
    CHECK(Fires(10, w, 8) == 10);                      /* every time */

    InstallTp(2, SE_LIVE_TP_ENABLED | SE_LIVE_TP_ONCE);
    CHECK(Fires(10, w, 8) == 1 && w[0] == 1);          /* once: the first execution, then never */

    InstallTp(3, SE_LIVE_TP_ENABLED | (3u << SE_LIVE_TP_EVERY_SHIFT));
    CHECK(Fires(10, w, 8) == 3 && w[0] == 3 && w[1] == 6 && w[2] == 9);   /* every 3rd */

    InstallTp(4, SE_LIVE_TP_ENABLED | (1u << SE_LIVE_TP_EVERY_SHIFT));
    CHECK(Fires(5, w, 8) == 5);                        /* every 1st is every time */

    /* A client-side condition: the emulator cannot count, so every execution is forwarded even
     * when a repeat policy is also set -- the client counts the ones whose condition held. */
    InstallTp(5, SE_LIVE_TP_ENABLED | SE_LIVE_TP_GUARDED | SE_LIVE_TP_ONCE);
    CHECK(Fires(10, w, 8) == 10);
    InstallTp(6, SE_LIVE_TP_ENABLED | SE_LIVE_TP_GUARDED | (4u << SE_LIVE_TP_EVERY_SHIFT));
    CHECK(Fires(10, w, 8) == 10);

    InstallTp(7, 0);                                   /* disabled never fires */
    CHECK(Fires(10, w, 8) == 0);
}

/* A re-install must not restart a tracepoint that did not change. The set is sent whole whenever
 * anything about any tracepoint changes, and a fire-once one elsewhere spending itself changes it. */
static void TestReinstallKeepsUnchangedCounts(void)
{
    unsigned int w[8] = { 0 };
    const unsigned int every3 = SE_LIVE_TP_ENABLED | (3u << SE_LIVE_TP_EVERY_SHIFT);
    const unsigned int once   = SE_LIVE_TP_ENABLED | SE_LIVE_TP_ONCE;
    unsigned int ids[2], flags[2];

    ids[0] = 10; flags[0] = every3;
    ids[1] = 11; flags[1] = once;
    InstallTps(2, ids, flags);
    CHECK(FiresAt(0, 2, w, 8) == 0);                   /* two of the three executions that fire it */
    CHECK(FiresAt(1, 1, w, 8) == 1);                   /* the fire-once one fires... */

    /* ...and the client, hearing of it, sends the set again with that one disabled. */
    flags[1] = 0;
    InstallTps(2, ids, flags);
    CHECK(FiresAt(0, 1, w, 8) == 1 && w[0] == 1);      /* the third execution still fires: count kept */
    CHECK(FiresAt(1, 5, w, 8) == 0);                   /* the disabled one stays quiet */

    /* The same set, sent again, changes nothing. */
    InstallTps(2, ids, flags);
    CHECK(FiresAt(0, 3, w, 8) == 1 && w[0] == 3);      /* 4th, 5th, 6th: the 6th fires */

    /* Re-enabling the fire-once one changes its flags, so it is armed afresh. */
    flags[1] = once;
    InstallTps(2, ids, flags);
    CHECK(FiresAt(1, 3, w, 8) == 1 && w[0] == 1);

    /* Changing the policy of the every-3rd one restarts it. */
    flags[0] = SE_LIVE_TP_ENABLED | (2u << SE_LIVE_TP_EVERY_SHIFT);
    InstallTps(2, ids, flags);
    CHECK(FiresAt(0, 3, w, 8) == 1 && w[0] == 2);
}

/* ---- a branch to itself ---------------------------------------------------------------- */

static void TestSelfBranchDetection(void)
{
    CHECK(SeMdfnIsTakenSelfBranch(0x89FEu, 1u) == 1);  /* bt .   with T set    */
    CHECK(SeMdfnIsTakenSelfBranch(0x89FEu, 0u) == 0);  /* bt .   with T clear: falls through */
    CHECK(SeMdfnIsTakenSelfBranch(0x8BFEu, 0u) == 1);  /* bf .   with T clear  */
    CHECK(SeMdfnIsTakenSelfBranch(0x8BFEu, 1u) == 0);  /* bf .   with T set: falls through   */
    CHECK(SeMdfnIsTakenSelfBranch(0x89FEu, 0xFFFFFFFEu) == 0);   /* only SR.T matters */
    CHECK(SeMdfnIsTakenSelfBranch(0x89FDu, 1u) == 0);  /* bt to the previous word: a real move */
    CHECK(SeMdfnIsTakenSelfBranch(0x89FFu, 1u) == 0);  /* bt to the next word */
    CHECK(SeMdfnIsTakenSelfBranch(0x8DFEu, 1u) == 0);  /* bt/s . -- delayed: its slot moves the PC */
    CHECK(SeMdfnIsTakenSelfBranch(0xAFFEu, 1u) == 0);  /* bra .  -- delayed too */
    CHECK(SeMdfnIsTakenSelfBranch(OP_RTS, 1u) == 0);
}

int main(void)
{
    TestCallFormsAndSerialization();
    TestBackwardBsr();
    TestInterruptsDoNotDrainTheStack();
    TestRteDoesNotUnwindACallFrameWithAMatchingSp();
    TestHandlerCallsBalance();
    TestTrapaPairsWithItsRte();
    TestNestedInterruptDoesNotStealTheTrapFrame();
    TestRtsWillNotUnwindAnExceptionFrame();
    TestTailCallAndBranchAreNotCalls();
    TestPerCpuStacks();
    TestOverflowDoesNotEatStoredFrames();
    TestTracepointRepeatPolicy();
    TestReinstallKeepsUnchangedCounts();
    TestSelfBranchDetection();
    if (gFailures)
    {
        fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    printf("Shadow call stack tests passed\n");
    return 0;
}
