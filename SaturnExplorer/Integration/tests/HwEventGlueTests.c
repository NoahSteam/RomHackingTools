/* The hardware event log's glue (se_mednafen_glue.c): the enable mask, the kind -> source / flags mapping, the
 * per-source per-frame cap and its SUPPRESSED record, the on-chip DMAC edge shadow, and the resets. Driven
 * without an emulator: the glue source is included (it has no header) with its sink replaced by a capture, and
 * the clock and PC are the test-settable statics it uses when it is not built wired. The hooks apply.py injects
 * call SeMednafenHwEvent / SeMednafenDmacRecalc exactly as called here; check_component.py runs those hooks in
 * the real patched sources against the real glue. */
#define SE_MDFN_HWEV_SINK TestSink
#include "SeHwEvents.h"
static void TestSink(const SeHwEvent* ev);
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

#define MAXREC 1024
static SeHwEvent gRec[MAXREC];
static unsigned int gN;
static void TestSink(const SeHwEvent* ev) { if (gN < MAXREC) gRec[gN] = *ev; ++gN; }

static void Reset(unsigned int mask)
{
    SeMednafenSetHwEvents(mask);
    SeMednafenStateLoaded(0);   /* counters and the DMAC shadow */
    gN = 0;
    sHwEvTestTs = 0x1234u;
    sHwEvTestPc = 0x06000100u;
}

/* One row per kind: the source bit index and the flags the glue must stamp. */
static const struct { unsigned int kind, source, flags; } kKinds[] = {
    { SE_HWEV_K_SH2_EXCEPTION,   0, SE_HWEV_F_HAS_PC },
    { SE_HWEV_K_SCU_INT_ACCEPT,  1, 0 },
    { SE_HWEV_K_SCU_INT_MASK,    1, 0 },
    { SE_HWEV_K_SCU_DMA_START,   2, 0 },
    { SE_HWEV_K_SCU_DMA_END,     2, 0 },
    { SE_HWEV_K_SCU_DMA_ILLEGAL, 2, 0 },
    { SE_HWEV_K_DMAC_START,      3, SE_HWEV_F_HAS_PC | SE_HWEV_F_PC_APPROX },
    { SE_HWEV_K_DMAC_END,        3, 0 },
    { SE_HWEV_K_CD_COMMAND,      6, 0 },
    { SE_HWEV_K_CD_RESPONSE,     6, 0 },
    { SE_HWEV_K_SMPC_COMMAND,    7, 0 },
    { SE_HWEV_K_VDP1_DRAW_START, 4, 0 },
    { SE_HWEV_K_VDP1_DRAW_END,   4, 0 },
    { SE_HWEV_K_VDP1_FB_SWAP,    4, 0 },
    { SE_HWEV_K_VDP1_FB_ERASE,   4, 0 },
    { SE_HWEV_K_VDP2_VBLANK,     5, 0 },
    { SE_HWEV_K_VDP2_DISPLAY,    5, 0 },
};
#define NKINDS (sizeof kKinds / sizeof kKinds[0])

static void TestMaskZero(void)
{
    size_t i;
    Reset(0);
    for (i = 0; i < NKINDS; ++i)
        SeMednafenHwEvent(kKinds[i].kind, 0, 0x100, 1, 2, 3, 4);
    SeMednafenDmacRecalc(0, 1, 0x5, 0x10, 0x20, 0x30, 0, 0, 0, 0);
    SeMednafenDmacRecalc(0, 0, 0x4, 0x10, 0x20, 0x30, 0, 0, 0, 0);
    SeMdfnHwEvEndFrame();
    CHECK_EQ(gN, 0);
    CHECK_EQ(SeSsHwEvMask, 0);
}

static void TestMapping(void)
{
    size_t i;
    for (i = 0; i < NKINDS; ++i)
    {
        Reset(0xFFu);
        SeMednafenHwEvent(kKinds[i].kind, (int)(i & 1), 0xAABB0000u + (unsigned)i, 11, 22, 33, 44);
        CHECK_EQ(gN, 1);
        CHECK_EQ(gRec[0].kind, kKinds[i].kind);
        CHECK_EQ(gRec[0].source, kKinds[i].source);
        CHECK_EQ(gRec[0].flags, kKinds[i].flags);
        CHECK_EQ(gRec[0].ts, 0x1234u);
        CHECK_EQ(gRec[0].cpu, i & 1);
        CHECK_EQ(gRec[0].arg[0], 11); CHECK_EQ(gRec[0].arg[1], 22); CHECK_EQ(gRec[0].arg[2], 33); CHECK_EQ(gRec[0].arg[3], 44);
        /* the PC is carried only by a kind that has one */
        CHECK_EQ(gRec[0].pc, (kKinds[i].flags & SE_HWEV_F_HAS_PC) ? 0xAABB0000u + (unsigned)i : 0u);
        /* the event is for its own source only: with that bit clear nothing is produced */
        Reset(0xFFu & ~(1u << kKinds[i].source));
        SeMednafenHwEvent(kKinds[i].kind, 0, 0, 0, 0, 0, 0);
        CHECK_EQ(gN, 0);
        /* and with only that bit set it is */
        Reset(1u << kKinds[i].source);
        SeMednafenHwEvent(kKinds[i].kind, 0, 0, 0, 0, 0, 0);
        CHECK_EQ(gN, 1);
    }
    /* a device event has no CPU */
    Reset(0xFFu);
    SeMednafenHwEvent(SE_HWEV_K_VDP1_FB_SWAP, SE_HWEV_CPU_NONE, 0, 0, 0, 0, 0);
    CHECK_EQ(gRec[0].cpu, SE_HWEV_CPU_NONE);
    SeMednafenHwEvent(SE_HWEV_K_VDP1_FB_SWAP, -1, 0, 0, 0, 0, 0);
    CHECK_EQ(gRec[1].cpu, SE_HWEV_CPU_NONE);
    /* an unknown kind is dropped, not mis-sourced */
    gN = 0;
    SeMednafenHwEvent(99u, 0, 0, 0, 0, 0, 0);
    SeMednafenHwEvent(SE_HWEV_K_SUPPRESSED, 0, 0, 0, 0, 0, 0);   /* only the glue makes these */
    CHECK_EQ(gN, 0);
}

static void TestCap(void)
{
    unsigned int i, n, vdp1 = 0, exc = 0;
    Reset(SE_HWEV_EN_VDP1 | SE_HWEV_EN_CPU_EXC | SE_HWEV_EN_CD);
    /* VDP1 overflows by 36, the CPU source stays exactly at its cap, CD sends 3 */
    for (i = 0; i < SE_HWEV_FRAME_CAP + 36u; ++i) SeMednafenHwEvent(SE_HWEV_K_VDP1_DRAW_START, -1, 0, i, 0, 0, 0);
    for (i = 0; i < SE_HWEV_FRAME_CAP; ++i) SeMednafenHwEvent(SE_HWEV_K_SH2_EXCEPTION, 0, 0x100, i, 0, 0, 0);
    for (i = 0; i < 3; ++i) SeMednafenHwEvent(SE_HWEV_K_CD_COMMAND, -1, 0, i, 0, 0, 0);
    CHECK_EQ(gN, SE_HWEV_FRAME_CAP * 2u + 3u);
    /* the records that got through are the FIRST ones, in order */
    CHECK_EQ(gRec[0].arg[0], 0);
    CHECK_EQ(gRec[SE_HWEV_FRAME_CAP - 1u].arg[0], SE_HWEV_FRAME_CAP - 1u);
    n = gN;
    SeMdfnHwEvEndFrame();
    /* one SUPPRESSED, for VDP1 only: count dropped and the cap */
    CHECK_EQ(gN, n + 1u);
    CHECK_EQ(gRec[n].kind, SE_HWEV_K_SUPPRESSED);
    CHECK_EQ(gRec[n].source, 4);
    CHECK_EQ(gRec[n].cpu, SE_HWEV_CPU_NONE);
    CHECK_EQ(gRec[n].flags, 0);
    CHECK_EQ(gRec[n].arg[0], 36);
    CHECK_EQ(gRec[n].arg[1], SE_HWEV_FRAME_CAP);
    /* the counters restart: the next frame gets its full cap again, and reports nothing if it stays under */
    gN = 0;
    for (i = 0; i < SE_HWEV_FRAME_CAP; ++i) SeMednafenHwEvent(SE_HWEV_K_VDP1_DRAW_START, -1, 0, 0, 0, 0, 0);
    CHECK_EQ(gN, SE_HWEV_FRAME_CAP);
    SeMdfnHwEvEndFrame();
    CHECK_EQ(gN, SE_HWEV_FRAME_CAP);
    /* two sources overflowing at once each get their own SUPPRESSED, in source order */
    gN = 0;
    for (i = 0; i < SE_HWEV_FRAME_CAP + 5u; ++i) SeMednafenHwEvent(SE_HWEV_K_SH2_EXCEPTION, 1, 0, 0, 0, 0, 0);
    for (i = 0; i < SE_HWEV_FRAME_CAP + 9u; ++i) SeMednafenHwEvent(SE_HWEV_K_VDP1_FB_SWAP, -1, 0, 0, 0, 0, 0);
    n = gN;
    SeMdfnHwEvEndFrame();
    CHECK_EQ(gN, n + 2u);
    for (i = n; i < gN; ++i)
    {
        if (gRec[i].source == 0) { exc = gRec[i].arg[0]; CHECK_EQ(gRec[i].kind, SE_HWEV_K_SUPPRESSED); }
        if (gRec[i].source == 4) { vdp1 = gRec[i].arg[0]; CHECK_EQ(gRec[i].kind, SE_HWEV_K_SUPPRESSED); }
    }
    CHECK_EQ(exc, 5);
    CHECK_EQ(vdp1, 9);
    /* the cap is per source: a flooded source does not stop another */
    Reset(SE_HWEV_EN_VDP1 | SE_HWEV_EN_CPU_EXC);
    for (i = 0; i < SE_HWEV_FRAME_CAP * 3u; ++i) SeMednafenHwEvent(SE_HWEV_K_VDP1_DRAW_START, -1, 0, 0, 0, 0, 0);
    n = gN;
    SeMednafenHwEvent(SE_HWEV_K_SH2_EXCEPTION, 0, 0, 0, 0, 0, 0);
    CHECK_EQ(gN, n + 1u);
    /* two kinds of one source share its budget */
    Reset(SE_HWEV_EN_VDP1);
    for (i = 0; i < SE_HWEV_FRAME_CAP; ++i) SeMednafenHwEvent((i & 1) ? SE_HWEV_K_VDP1_DRAW_END : SE_HWEV_K_VDP1_DRAW_START, -1, 0, 0, 0, 0, 0);
    SeMednafenHwEvent(SE_HWEV_K_VDP1_FB_SWAP, -1, 0, 0, 0, 0, 0);
    CHECK_EQ(gN, SE_HWEV_FRAME_CAP);
    /* a source that is off at the frame's end is not reported, whatever its counter */
    SeSsHwEvMask = 0;
    n = gN;
    SeMdfnHwEvEndFrame();
    CHECK_EQ(gN, n);
}

static void TestDmac(void)
{
    /* a recalc with a channel that is not running seeds the shadow after a load: silent */
    Reset(SE_HWEV_EN_SH2_DMAC);
    SeMednafenDmacRecalc(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    CHECK_EQ(gN, 0);
    /* rising edge: channel 0 */
    SeMednafenDmacRecalc(0, 1, 0x1A05u, 0x06000000u, 0x00200000u, 8, 0, 0, 0, 0);
    CHECK_EQ(gN, 1);
    CHECK_EQ(gRec[0].kind, SE_HWEV_K_DMAC_START);
    CHECK_EQ(gRec[0].source, 3);
    CHECK_EQ(gRec[0].cpu, 0);
    CHECK_EQ(gRec[0].flags, SE_HWEV_F_HAS_PC | SE_HWEV_F_PC_APPROX);
    CHECK_EQ(gRec[0].pc, 0x06000100u);
    CHECK_EQ(gRec[0].ts, 0x1234u);
    CHECK_EQ(gRec[0].arg[0], 0u | (0x1A05u << 8));
    CHECK_EQ(gRec[0].arg[1], 0x06000000u);
    CHECK_EQ(gRec[0].arg[2], 0x00200000u);
    CHECK_EQ(gRec[0].arg[3], 8);
    /* only CHCR's 16 bits go into arg0, whatever sits above them */
    SeMednafenDmacRecalc(1, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    gN = 0;
    SeMednafenDmacRecalc(1, 1, 0xFFFF1A05u, 0, 0, 0, 0, 0, 0, 0);
    CHECK_EQ(gRec[0].arg[0], 0u | (0x1A05u << 8));
    SeMednafenDmacRecalc(1, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    gN = 1;
    /* a recalc that changes nothing is no edge */
    SeMednafenDmacRecalc(0, 1, 0x1A05u, 0x06000000u, 0x00200000u, 8, 0, 0, 0, 0);
    CHECK_EQ(gN, 1);
    /* channel 1 joins: only it starts, with ITS registers */
    SeMednafenDmacRecalc(0, 3, 0x1A05u, 0x06000000u, 0x00200000u, 8, 0x1405u, 0x100, 0x200, 3);
    CHECK_EQ(gN, 2);
    CHECK_EQ(gRec[1].kind, SE_HWEV_K_DMAC_START);
    CHECK_EQ(gRec[1].arg[0], 1u | (0x1405u << 8));
    CHECK_EQ(gRec[1].arg[1], 0x100); CHECK_EQ(gRec[1].arg[2], 0x200); CHECK_EQ(gRec[1].arg[3], 3);
    /* falling edge: channel 0 finishes */
    SeMednafenDmacRecalc(0, 2, 0, 0, 0, 0, 0x1405u, 0x100, 0x200, 3);
    CHECK_EQ(gN, 3);
    CHECK_EQ(gRec[2].kind, SE_HWEV_K_DMAC_END);
    CHECK_EQ(gRec[2].source, 3);
    CHECK_EQ(gRec[2].cpu, 0);
    CHECK_EQ(gRec[2].flags, 0);
    CHECK_EQ(gRec[2].arg[0], 0);
    /* both fall together */
    SeMednafenDmacRecalc(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    CHECK_EQ(gN, 4);
    CHECK_EQ(gRec[3].kind, SE_HWEV_K_DMAC_END);
    CHECK_EQ(gRec[3].arg[0], 1);
    /* the two CPUs have their own shadows */
    SeMednafenDmacRecalc(1, 0, 0, 0, 0, 0, 0, 0, 0, 0);   /* first recalc of the slave: seeds */
    SeMednafenDmacRecalc(1, 1, 1, 2, 3, 4, 0, 0, 0, 0);
    CHECK_EQ(gN, 5);
    CHECK_EQ(gRec[4].cpu, 1);
    SeMednafenDmacRecalc(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    CHECK_EQ(gN, 5);                                      /* the master was already idle */

    /* unknown after a state load: a running transfer is not reported as starting, its end is */
    SeMednafenStateLoaded(0);
    gN = 0;
    SeMednafenDmacRecalc(0, 1, 7, 8, 9, 10, 0, 0, 0, 0);
    CHECK_EQ(gN, 0);
    SeMednafenDmacRecalc(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    CHECK_EQ(gN, 1);
    CHECK_EQ(gRec[0].kind, SE_HWEV_K_DMAC_END);
    /* ... and after a CPU reset, for that CPU only */
    SeMednafenDmacRecalc(1, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    SeMednafenDmacRecalc(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    gN = 0;
    SeMednafenCpuReset(1, 0);
    SeMednafenDmacRecalc(1, 1, 7, 8, 9, 10, 0, 0, 0, 0);
    CHECK_EQ(gN, 0);
    SeMednafenDmacRecalc(0, 1, 7, 8, 9, 10, 0, 0, 0, 0);
    CHECK_EQ(gN, 1);

    /* switching the source on re-seeds it: what ran while it was off is unknown */
    SeMednafenSetHwEvents(0);
    SeMednafenDmacRecalc(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);   /* ignored: the log is off */
    SeMednafenSetHwEvents(SE_HWEV_EN_SH2_DMAC);
    gN = 0;
    SeMednafenDmacRecalc(0, 1, 7, 8, 9, 10, 0, 0, 0, 0);
    CHECK_EQ(gN, 0);

    /* DMAC events count against the DMAC source's cap */
    Reset(SE_HWEV_EN_SH2_DMAC);
    SeMednafenDmacRecalc(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    {
        unsigned int i;
        for (i = 0; i < SE_HWEV_FRAME_CAP; ++i)
        {
            SeMednafenDmacRecalc(0, 1, 1, 1, 1, 1, 0, 0, 0, 0);
            SeMednafenDmacRecalc(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        }
    }
    CHECK_EQ(gN, SE_HWEV_FRAME_CAP);
    SeMdfnHwEvEndFrame();
    CHECK_EQ(gN, SE_HWEV_FRAME_CAP + 1u);
    CHECK_EQ(gRec[SE_HWEV_FRAME_CAP].kind, SE_HWEV_K_SUPPRESSED);
    CHECK_EQ(gRec[SE_HWEV_FRAME_CAP].source, 3);
    CHECK_EQ(gRec[SE_HWEV_FRAME_CAP].arg[0], SE_HWEV_FRAME_CAP);
    /* the shadow tracked the dropped edges, so the next real edge is still an edge, not a repeat */
    gN = 0;
    SeMednafenDmacRecalc(0, 1, 1, 1, 1, 1, 0, 0, 0, 0);
    CHECK_EQ(gN, 1);
}

static void TestResets(void)
{
    unsigned int i;
    /* a state load drops the frame's partial counts: the next frame has its full cap and no SUPPRESSED */
    Reset(SE_HWEV_EN_VDP1);
    for (i = 0; i < SE_HWEV_FRAME_CAP + 10u; ++i) SeMednafenHwEvent(SE_HWEV_K_VDP1_DRAW_START, -1, 0, 0, 0, 0, 0);
    SeMednafenStateLoaded(1);
    gN = 0;
    SeMdfnHwEvEndFrame();
    CHECK_EQ(gN, 0);
    for (i = 0; i < SE_HWEV_FRAME_CAP; ++i) SeMednafenHwEvent(SE_HWEV_K_VDP1_DRAW_START, -1, 0, 0, 0, 0, 0);
    CHECK_EQ(gN, SE_HWEV_FRAME_CAP);
    /* the load does not touch the mask: the log stays on across a rewind */
    CHECK_EQ(SeSsHwEvMask, SE_HWEV_EN_VDP1);
    /* a mask change at a frame boundary starts the frame clean, and masks to what is supported */
    for (i = 0; i < 5; ++i) SeMednafenHwEvent(SE_HWEV_K_VDP1_DRAW_START, -1, 0, 0, 0, 0, 0);
    SeMednafenSetHwEvents(0xFFFFFFFFu);
    CHECK_EQ(SeSsHwEvMask, SE_MDFN_HWEV_SUPPORTED);
    gN = 0;
    SeMdfnHwEvEndFrame();
    CHECK_EQ(gN, 0);
    for (i = 0; i < SE_HWEV_FRAME_CAP; ++i) SeMednafenHwEvent(SE_HWEV_K_VDP1_DRAW_START, -1, 0, 0, 0, 0, 0);
    CHECK_EQ(gN, SE_HWEV_FRAME_CAP);
    /* the frame end clears the counters: every source reports each frame independently */
    SeMdfnHwEvEndFrame();
    gN = 0;
    for (i = 0; i < SE_HWEV_FRAME_CAP; ++i) SeMednafenHwEvent(SE_HWEV_K_VDP1_DRAW_START, -1, 0, 0, 0, 0, 0);
    CHECK_EQ(gN, SE_HWEV_FRAME_CAP);
}

int main(void)
{
    TestMaskZero();
    TestMapping();
    TestCap();
    TestDmac();
    TestResets();
    if (gFailures)
    {
        fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    printf("HwEventGlueTests: ok\n");
    return 0;
}
