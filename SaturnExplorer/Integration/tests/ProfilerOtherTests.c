/* F7 producer side: the per-frame function table's Other, per CPU and by cause. The real glue
 * (se_mednafen_glue.c, included because it has no header) runs its real SeMdfnProfAdd and
 * SeMdfnPerfEndFrame; only the transport is replaced -- SeExportPublishPerf is renamed to a capture
 * so the test reads exactly what the server would be handed. Every check is exact cycles.
 *
 * The identity that must always hold: for each CPU, listed rows + otherNotAdmitted + otherEvicted ==
 * every cycle that was charged to that CPU's keys. */
#define SeExportPublishPerf TestCapturePublish
#define SeExportHasClient   TestHasClient
#include "SePerfCounters.h"
#include "se_export.h"

int TestHasClient(void) { return 1; }
static unsigned long long gCapCounters[SE_PERF_COUNT];
static unsigned int gCapKeys[SE_LIVE_PERF_FN_MAX], gCapCycles[SE_LIVE_PERF_FN_MAX];
static unsigned int gCapFnCount, gCapOther, gCapPublished;
static SeExportPerfInfo gCapInfo;
void TestCapturePublish(const unsigned long long* counters, unsigned int count,
                               const unsigned int* fnKeys, const unsigned int* fnCycles,
                               unsigned int fnCount, unsigned int fnOtherCycles, const SeExportPerfInfo* info)
{
    unsigned int i;
    (void)count; (void)counters;
    gCapFnCount = fnCount; gCapOther = fnOtherCycles;
    for (i = 0; i < fnCount; ++i) { gCapKeys[i] = fnKeys[i]; gCapCycles[i] = fnCycles[i]; }
    gCapInfo = *info;
    ++gCapPublished;
}

#include "se_mednafen_glue.c"
#include <stdio.h>

static int gFailures;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expr); ++gFailures; } } while (0)
#define CHECK_EQ(got, want) do { const unsigned long long g_ = (unsigned long long)(got), w_ = (unsigned long long)(want); \
    if (g_ != w_) { fprintf(stderr, "CHECK failed at line %d: %s == %llu, want %llu\n", __LINE__, #got, g_, w_); ++gFailures; } } while (0)

static unsigned int Key(unsigned int n, int cpu) { return (0x06000000u + n * 4u) | (cpu ? SE_PERF_FN_SLAVE : 0u); }

static void Fresh(unsigned int mode)
{
    memset(SeSsPerfC, 0, sizeof SeSsPerfC);
    memset(sProfTab, 0, sizeof sProfTab);
    sProfUsed = 0;
    SeMdfnProfOtherReset();
    SeMdfnApplyMode(mode);
    gCapPublished = 0;
    memset(&gCapInfo, 0, sizeof gCapInfo);
}

static unsigned long long Listed(int cpu)
{
    unsigned long long t = 0;
    unsigned int i;
    for (i = 0; i < gCapFnCount; ++i)
        if (((gCapKeys[i] & SE_PERF_FN_SLAVE) != 0) == (cpu != 0)) t += gCapCycles[i];
    return t;
}
static unsigned long long OtherOf(int cpu) { return gCapInfo.otherNotAdmitted[cpu] + gCapInfo.otherEvicted[cpu]; }

/* Fewer than 64 keys: everything is listed, Other is a measured zero. */
static void TestNothingLost(void)
{
    Fresh(SE_LIVE_PROF_FUNCTIONS);
    SeMdfnProfAdd(Key(1, 0), 700);
    SeMdfnProfAdd(Key(2, 1), 300);
    SeMdfnPerfEndFrame();
    CHECK_EQ(gCapPublished, 1);
    CHECK((gCapInfo.valid & SE_PERF_VALID_OTHER) != 0);
    CHECK_EQ(gCapInfo.satFlags, 0);
    CHECK_EQ(OtherOf(0), 0);
    CHECK_EQ(OtherOf(1), 0);
    CHECK_EQ(Listed(0), 700);
    CHECK_EQ(Listed(1), 300);
    CHECK_EQ(gCapOther, 0);
}

/* A 65th key: the lowest of the 65 is evicted to Other under ITS CPU, and the flag says truncated.
 * Keys 1..64 are master with cycles 100+n; key 65 is the slave's with the lowest cycles. */
static void TestSixtyFifthKeyEvicts(void)
{
    unsigned int n;
    unsigned long long master = 0;
    Fresh(SE_LIVE_PROF_FUNCTIONS);
    for (n = 1; n <= 64; ++n) { SeMdfnProfAdd(Key(n, 0), 100u + n); master += 100u + n; }
    SeMdfnProfAdd(Key(65, 1), 5);
    SeMdfnPerfEndFrame();
    CHECK_EQ(gCapFnCount, 64);
    CHECK_EQ(gCapInfo.satFlags, SE_PERF_SAT_TRUNCATED);
    CHECK_EQ(gCapInfo.otherEvicted[1], 5);          /* the slave's key lost: charged to the slave */
    CHECK_EQ(gCapInfo.otherEvicted[0], 0);
    CHECK_EQ(gCapInfo.otherNotAdmitted[0] + gCapInfo.otherNotAdmitted[1], 0);
    CHECK_EQ(Listed(0) + OtherOf(0), master);
    CHECK_EQ(Listed(1) + OtherOf(1), 5);
    CHECK_EQ(gCapOther, 5);
}

/* A high newcomer pushes out a LISTED master key, so the evicted cycles belong to the master:
 * 64 master keys (100+n) plus a slave key of 1000 evicts master key 1 (101). */
static void TestEvictedCpuIsTheLosers(void)
{
    unsigned int n;
    unsigned long long master = 0;
    Fresh(SE_LIVE_PROF_FUNCTIONS);
    for (n = 1; n <= 64; ++n) { SeMdfnProfAdd(Key(n, 0), 100u + n); master += 100u + n; }
    SeMdfnProfAdd(Key(65, 1), 1000);
    SeMdfnPerfEndFrame();
    CHECK_EQ(gCapInfo.otherEvicted[0], 101);
    CHECK_EQ(gCapInfo.otherEvicted[1], 0);
    CHECK_EQ(Listed(0) + OtherOf(0), master);
    CHECK_EQ(Listed(1) + OtherOf(1), 1000);
}

/* The late dominant key: after 3,072 small keys the table refuses the 3,073rd even though it carries
 * most of the frame. Its cycles are not lost -- they are Other, not admitted, under its CPU. */
static void TestLateDominantKeyNotAdmitted(void)
{
    unsigned int n;
    unsigned long long master = 0;
    Fresh(SE_LIVE_PROF_FUNCTIONS);
    for (n = 1; n <= SE_PROF_FILL; ++n) { SeMdfnProfAdd(Key(n, 0), 10u); master += 10; }
    SeMdfnProfAdd(Key(SE_PROF_FILL + 1, 1), 90000);   /* the 3,073rd key, on the slave */
    SeMdfnProfAdd(Key(SE_PROF_FILL + 1, 1), 5000);    /* and again: still refused, still counted */
    SeMdfnProfAdd(Key(1, 0), 7);                      /* an admitted key still accumulates */
    master += 7;
    SeMdfnPerfEndFrame();
    CHECK_EQ(gCapFnCount, 64);
    CHECK((gCapInfo.satFlags & SE_PERF_SAT_TABLE_FULL) != 0);
    CHECK((gCapInfo.satFlags & SE_PERF_SAT_TRUNCATED) != 0);
    CHECK_EQ(gCapInfo.otherNotAdmitted[1], 95000);
    CHECK_EQ(gCapInfo.otherNotAdmitted[0], 0);
    CHECK_EQ(Listed(1), 0);
    CHECK_EQ(Listed(0) + OtherOf(0), master);
    CHECK_EQ(Listed(1) + OtherOf(1), 95000);
    CHECK_EQ(gCapOther, master - Listed(0) + 95000);
}

/* Sums past 32 bits: the words stay exact, fnOtherCycles saturates. */
static void TestSaturatedSum(void)
{
    unsigned int n;
    Fresh(SE_LIVE_PROF_FUNCTIONS);
    for (n = 1; n <= SE_PROF_FILL; ++n) SeMdfnProfAdd(Key(n, 0), 1u);
    SeMdfnProfAdd(Key(5000, 0), 0xF0000000u);
    SeMdfnProfAdd(Key(5001, 0), 0xF0000000u);
    SeMdfnPerfEndFrame();
    CHECK_EQ(gCapInfo.otherNotAdmitted[0], 0x1E0000000ull);
    CHECK_EQ(gCapOther, 0xFFFFFFFFu);
}

/* A frame that ran with attribution off measured nothing: Other is "not available", not zero. */
static void TestModeOffIsNotAMeasuredZero(void)
{
    Fresh(0);
    SeMdfnPerfEndFrame();
    CHECK_EQ(gCapPublished, 1);
    CHECK((gCapInfo.valid & SE_PERF_VALID_OTHER) == 0);
    CHECK_EQ(gCapInfo.satFlags, 0);
}

/* Each frame starts from zero: nothing carries over, including the saturation flags. */
static void TestResetsBetweenFrames(void)
{
    unsigned int n;
    Fresh(SE_LIVE_PROF_FUNCTIONS);
    for (n = 1; n <= SE_PROF_FILL; ++n) SeMdfnProfAdd(Key(n, 0), 3u);
    SeMdfnProfAdd(Key(9000, 1), 77);
    SeMdfnPerfEndFrame();
    CHECK_EQ(gCapInfo.otherNotAdmitted[1], 77);
    SeMdfnProfAdd(Key(1, 0), 11);
    SeMdfnPerfEndFrame();
    CHECK_EQ(gCapInfo.otherNotAdmitted[1], 0);
    CHECK_EQ(gCapInfo.otherEvicted[0], 0);
    CHECK_EQ(gCapInfo.satFlags, 0);
    CHECK_EQ(Listed(0), 11);
}

int main(void)
{
    TestNothingLost();
    TestSixtyFifthKeyEvicts();
    TestEvictedCpuIsTheLosers();
    TestLateDominantKeyNotAdmitted();
    TestSaturatedSum();
    TestModeOffIsNotAMeasuredZero();
    TestResetsBetweenFrames();
    if (gFailures) { fprintf(stderr, "%d check(s) failed\n", gFailures); return 1; }
    printf("ProfilerOtherTests: all checks passed\n");
    return 0;
}
