/* Start/stop churn of the live tap's listener threads.
 *
 * SeExportInit starts the unix-socket and TCP listener threads; each publishes its listening
 * descriptor for SeExportDeinit to close and wake it. Nothing else in the suite stops the tap
 * the instant it has started, so this is the case where the two sides meet: Deinit running while
 * a listener is still between socket() and accept(), or just returning from it.
 *
 * It pins two things. (1) No data race on the published descriptors; ThreadSanitizer (the
 * SE_SANITIZE=thread build) flagged an unsynchronised read/write of them before they went
 * through atomics, and a plain build cannot see that. (2) No descriptor is closed twice: Deinit
 * and the listener each used to close the same number, and the second close lands on whatever
 * the emulator had been handed that number for in between. A leak shows up as the open
 * descriptor count drifting over the loop.
 * POSIX-only, like the attach test.
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "SeLiveProtocol.h"
#include "se_export.h"

static int gFailures;

#define CHECK(cond) do {                                                      \
    if (!(cond)) {                                                            \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
        ++gFailures;                                                          \
    }                                                                         \
} while (0)

static void SleepUs(long us)
{
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = us * 1000L;
    nanosleep(&ts, NULL);
}

static int OpenFdCount(void)
{
    int n = 0;
    DIR* d = opendir("/dev/fd");
    struct dirent* e;
    if (!d) return -1;
    while ((e = readdir(d)) != NULL) { if (e->d_name[0] != '.') ++n; }
    closedir(d);
    return n;
}

int main(void)
{
    enum { ROUNDS = 300 };
    int i, baseline, after;

    /* One warm-up round so lazily opened descriptors are not counted as drift. */
    if (SeExportInit() != 0) { printf("FAIL: SeExportInit\n"); return 1; }
    SeExportDeinit();
    baseline = OpenFdCount();

    for (i = 0; i < ROUNDS; ++i)
    {
        if (SeExportInit() != 0) { printf("FAIL: SeExportInit round %d\n", i); return 1; }
        if (i % 3 == 1) SleepUs(200);   /* vary how far the listeners got; no ordering relied on */
        if (i % 3 == 2) SleepUs(3000);
        SeExportDeinit();
    }

    after = OpenFdCount();
    CHECK(baseline >= 0 && after == baseline);

    printf(gFailures ? "FAILURES: %d\n" : "all cases passed\n", gFailures);
    return gFailures ? 1 : 0;
}
