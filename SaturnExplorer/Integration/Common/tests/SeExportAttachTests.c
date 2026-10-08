/* Does the live tap stay out of the emulator's way while nothing is attached?
 *
 * The per-frame capture is not cheap: SeExportSnapshot copies several MB into the frame ring and
 * then stages a FULL emulator savestate for the rewind pipeline. None of that has a reader until
 * Saturn Explorer connects, so a patched emulator running on its own should cost what an
 * unpatched one costs. This is the check that it does.
 *
 * The savestate hook is what the test counts, because it is the expensive half and the only part
 * of the capture an outside observer can see happen. POSIX-only: it needs the real unix-socket
 * listener to attach to, which is also the transport the macOS/Linux builds use.
 */
#define _POSIX_C_SOURCE 200809L   /* nanosleep, under a strict -std=c99 */
#if defined(__APPLE__)
/* Darwin hides everything outside the named standard once _POSIX_C_SOURCE is set, MSG_DONTWAIT
 * included; glibc exposes it regardless, so only macOS needs to ask. */
#define _DARWIN_C_SOURCE 1
#endif

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
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

/* Stand in for the emulator's savestate. A NULL buffer is se_export's sizing probe, which runs
 * once when the hook is installed -- only a real capture counts. */
#define FAKE_STATE_LEN 64u
static int gCaptures;

static size_t FakeSaveState(unsigned char* buf, size_t cap)
{
    if (!buf) return FAKE_STATE_LEN;
    if (cap < FAKE_STATE_LEN) return 0;
    memset(buf, 0xA5, FAKE_STATE_LEN);
    ++gCaptures;
    return FAKE_STATE_LEN;
}

/* Every argument NULL: the sections then ship empty, which is enough to drive the capture path
 * (and keeps the test from having to allocate the real multi-MB blocks). */
static void Frame(void)
{
    SeExportSnapshot(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                     NULL, NULL, NULL);
    SeExportEndFrame();   /* the glue's last call of the frame: where the savestate is taken */
}

static void Sleep5ms(void)
{
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 5000000L;
    nanosleep(&ts, NULL);
}

/* Poll rather than sleep a fixed time: the attach is handed over by the server thread. */
static int WaitForClient(int want)
{
    int i;
    for (i = 0; i < 400; ++i)   /* <= 2s */
    {
        if ((SeExportHasClient() != 0) == (want != 0)) return 1;
        Sleep5ms();
    }
    return 0;
}

/* SeExportInit returns once the server thread exists, which is before that thread has reached
 * bind/listen -- so the first attach can lose the race. Retry rather than sleep a fixed time. */
static int Connect(void)
{
    struct sockaddr_un addr;
    int i;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SE_LIVE_DEFAULT_SOCK_PATH, sizeof(addr.sun_path) - 1);
    for (i = 0; i < 400; ++i)   /* <= 2s */
    {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        if (connect(fd, (struct sockaddr*)&addr, sizeof addr) == 0) return fd;
        close(fd);
        Sleep5ms();
    }
    return -1;
}

/* Send one 8-byte command frame (verb + LE arg), then swallow whatever the server replies with.
 * The reply is a whole snapshot -- megabytes -- and the server writes it synchronously, so a
 * test that does not drain would wedge the server in send() and never see its next command
 * handled. Draining on a time budget avoids having to parse the section lengths. */
static int SendVerbAndDrain(int fd, const char* verb, unsigned int arg)
{
    unsigned char req[8];
    int i;
    memcpy(req, verb, 4);
    req[4] = (unsigned char)(arg & 0xFF);
    req[5] = (unsigned char)((arg >> 8) & 0xFF);
    req[6] = (unsigned char)((arg >> 16) & 0xFF);
    req[7] = (unsigned char)((arg >> 24) & 0xFF);
    if (send(fd, req, sizeof req, 0) != (ssize_t)sizeof req) return 0;

    for (i = 0; i < 200; ++i)   /* <= ~1s of quiet before giving up on more */
    {
        unsigned char sink[65536];
        const ssize_t n = recv(fd, sink, sizeof sink, MSG_DONTWAIT);
        if (n > 0) { i = 0; continue; }          /* still arriving: restart the idle count */
        if (n == 0) return 0;                    /* server closed */
        Sleep5ms();
    }
    return 1;
}

/* The REW verb (v18): the emulator must stop saving a state per frame when the client says the
 * user has rewind switched off, and start again when it is switched back on. This is the whole
 * point of the verb -- the capture is the most expensive thing on the emulate thread.
 * 'fd' is an already-attached client. */
static void TestRewindVerbGatesCapture(int fd)
{
    CHECK(SendVerbAndDrain(fd, SE_LIVE_VERB_REWIND, 0u));
    gCaptures = 0;
    Frame(); Frame(); Frame();
    CHECK(gCaptures == 0);          /* switched off: nothing captured even though attached */

    CHECK(SendVerbAndDrain(fd, SE_LIVE_VERB_REWIND, 1u));
    gCaptures = 0;
    Frame(); Frame(); Frame();
    CHECK(gCaptures > 0);           /* and back on again */

}

int main(void)
{
    int fd, before;

    /* Asking before the tap is started is harmless and answers "nothing attached". */
    CHECK(SeExportHasClient() == 0);

    if (SeExportInit() != 0) { printf("FAIL: SeExportInit\n"); return 1; }
    SeExportSetSaveStateHook(FakeSaveState);
    CHECK(WaitForClient(0));

    /* Unattached: frames must cost nothing. */
    gCaptures = 0;
    Frame(); Frame(); Frame();
    CHECK(SeExportHasClient() == 0);
    CHECK(gCaptures == 0);

    fd = Connect();
    if (fd < 0) { printf("FAIL: could not attach to " SE_LIVE_DEFAULT_SOCK_PATH "\n");
                  SeExportDeinit(); return 1; }
    CHECK(WaitForClient(1));

    /* Attached: the capture runs again. Several frames, since the staging queue drops a frame
     * when the compression worker is behind and only the hook call itself is guaranteed. */
    gCaptures = 0;
    Frame(); Frame(); Frame();
    CHECK(gCaptures > 0);

    TestRewindVerbGatesCapture(fd);

    close(fd);
    CHECK(WaitForClient(0));

    /* And it stops again when the client goes away, rather than running for the rest of the
     * emulator's life because something attached once. */
    before = gCaptures;
    Frame(); Frame(); Frame();
    CHECK(gCaptures == before);

    SeExportDeinit();

    printf(gFailures ? "FAILURES: %d\n" : "all cases passed\n", gFailures);
    return gFailures ? 1 : 0;
}
