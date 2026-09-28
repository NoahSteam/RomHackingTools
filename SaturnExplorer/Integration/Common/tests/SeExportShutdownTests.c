/* The Windows live-tap shutdown path: does SeExportDeinit actually end the server thread?
 *
 * Review finding HOOK-01. The old deinit waited a second, closed the thread handle, and then
 * destroyed both critical sections and freed the frame ring -- but closing a thread handle does
 * not end the thread, and the server is parked in a synchronous call that clearing sRunning does
 * not reach. A thread that woke afterwards entered a deleted critical section and read freed
 * memory, in someone else's emulator, with nothing in the stack pointing at this file.
 *
 * Three cases, covering both calls the server can be parked in:
 *   1. no client ever connected -- the server is in ConnectNamedPipe (the ordinary case on exit)
 *   2. a client connected and went quiet -- the server is in ReadFile
 *   3. a client connected and vanished
 *
 * The check is the pipe, not the clock: the old code also returned in about a second, by giving
 * up. The server owns the pipe handle and closes it only on its way out of its loop, so while the
 * pipe object still exists, so does the thread. Run against the pre-fix deinit, all three cases
 * fail; against the current one, all three pass.
 *
 * Windows-only, by subject. It builds natively there; from a Linux checkout it cross-compiles and
 * runs under wine, which is how it was first verified:
 *
 *   x86_64-w64-mingw32-gcc -O1 -I Integration/Common -I include \
 *       Integration/Common/se_export.c Integration/Common/tests/SeExportShutdownTests.c \
 *       -o /tmp/shutdown.exe && wine /tmp/shutdown.exe
 */
#include <windows.h>
#include <stdio.h>

#include "SeLiveProtocol.h"

extern int  SeExportInit(void);
extern void SeExportDeinit(void);

#define PIPE SE_LIVE_DEFAULT_PIPE_NAME

static double Elapsed(DWORD t0) { return (GetTickCount() - t0) / 1000.0; }

static int Case(const char* name, int connect, int keepOpen)
{
    HANDLE cl = INVALID_HANDLE_VALUE;
    DWORD t0;
    double secs;

    if (SeExportInit() != 0) { printf("FAIL %s: init failed\n", name); return 1; }
    Sleep(200);   /* let the server reach ConnectNamedPipe */

    if (connect)
    {
        cl = CreateFileA(PIPE, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (cl == INVALID_HANDLE_VALUE)
        {
            printf("FAIL %s: could not connect (err %lu)\n", name, GetLastError());
            SeExportDeinit();
            return 1;
        }
        Sleep(200);   /* let the server reach its blocking read */
        if (!keepOpen) { CloseHandle(cl); cl = INVALID_HANDLE_VALUE; Sleep(100); }
    }

    t0 = GetTickCount();
    SeExportDeinit();
    secs = Elapsed(t0);
    if (cl != INVALID_HANDLE_VALUE) CloseHandle(cl);

    /* The old code also returned in about a second -- by giving up -- so the time alone proves
     * nothing. What proves the thread is gone is the pipe: the server owns that handle and closes
     * it only on its way out of the loop, so while the pipe object still exists, so does the
     * thread that would later wake into deleted locks and a freed ring. */
    {
        HANDLE probe = CreateFileA(PIPE, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        const DWORD err = GetLastError();
        if (probe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(probe);
            printf("FAIL %s: the pipe is still open after deinit (%.2fs) -- the server thread is "
                   "still in its loop\n", name, secs);
            return 1;
        }
        if (err != ERROR_FILE_NOT_FOUND)
        {
            printf("FAIL %s: the pipe still exists after deinit (err %lu, %.2fs)\n",
                   name, err, secs);
            return 1;
        }
    }
    /* And the tap can be started again: the sticky incomplete-shutdown flag refuses when the
     * join did not happen, so this also fails if deinit took the leak path. */
    if (SeExportInit() != 0)
    {
        printf("FAIL %s: shutdown was incomplete (%.2fs) -- a thread survived\n", name, secs);
        return 1;
    }
    SeExportDeinit();
    printf("PASS %s (deinit %.2fs)\n", name, secs);
    return 0;
}

int main(void)
{
    int fails = 0;
    fails += Case("idle server (parked in ConnectNamedPipe)", 0, 0);
    fails += Case("client connected and silent (parked in ReadFile)", 1, 1);
    fails += Case("client connected then gone", 1, 0);
    printf(fails ? "FAILURES: %d\n" : "all cases passed\n", fails);
    return fails ? 1 : 0;
}
