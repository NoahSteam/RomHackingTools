/* Saturn Explorer — Yabause memory-export server. See se_export.h + README.md.
 * Self-contained: only needs SeLiveProtocol.h (copy it in from
 * Drivers/Common/src/) and the platform's sockets/threads. */

/* Ask glibc to declare usleep() from <unistd.h> even under strict -std=c11
 * (must precede any system header). */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif

#include "se_export.h"
#include "SeLiveProtocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#define SE_V1 SE_LIVE_VDP1_VRAM_LEN
#define SE_V2 SE_LIVE_VDP2_VRAM_LEN
#define SE_CR SE_LIVE_CRAM_LEN
#define SE_VS SE_LIVE_VDP2_STRUCT_LEN
#define SE_VR SE_LIVE_VDP1_REGS_LEN
#define SE_WL SE_LIVE_WRAM_LOW_LEN
#define SE_WH SE_LIVE_WRAM_HIGH_LEN
#define SE_FB SE_LIVE_VDP1_FB_LEN
#define SE_CT SE_LIVE_CONTROL_LEN
#define SE_SH SE_LIVE_SH2_LEN

typedef struct
{
    unsigned char v1[SE_V1];   /* VDP1 VRAM */
    unsigned char v2[SE_V2];   /* VDP2 VRAM */
    unsigned char cr[SE_CR];   /* CRAM */
    unsigned char vs[SE_VS];   /* raw Vdp2 register struct */
    unsigned char vr[SE_VR];   /* hardware-offset BE VDP1 register image */
    unsigned char wl[SE_WL];   /* low work RAM */
    unsigned char wh[SE_WH];   /* high work RAM */
    unsigned char fb[SE_FB];   /* VDP1 frame buffer (drawn output) */
    unsigned char sh[SE_SH];   /* SH-2 state: master then slave sh2regs_struct */
    int valid;
} SeFrame;

/* Build the hardware-offset, big-endian VDP1 register image from Yabause's Vdp1
 * struct (first 11 u16 fields: TVMR,FBCR,PTMR,EWDR,EWLR,EWRR,ENDR,EDSR,LOPR,COPR,
 * MODR, host byte order). Hardware has a 1-word gap at 0x0E, so EDSR..MODR shift
 * up by 2 relative to their struct index. */
static void SeBuildVdp1Image(const unsigned char* dst_img, const void* vdp1struct)
{
    unsigned char* out = (unsigned char*)dst_img;
    const unsigned short* r = (const unsigned short*)vdp1struct;
    int i;
    memset(out, 0, SE_VR);
    if (!r)
    {
        return;
    }
    for (i = 0; i < 11; ++i)
    {
        unsigned hw = (i <= 6) ? (unsigned)(i * 2) : (unsigned)(i * 2 + 2);
        if (hw + 1 < SE_VR)
        {
            out[hw]     = (unsigned char)((r[i] >> 8) & 0xFF);
            out[hw + 1] = (unsigned char)(r[i] & 0xFF);
        }
    }
}

static SeFrame* sFront;
static SeFrame* sBack;
static volatile int sRunning;

/* ---- Frame-control state (see SeExportGateFrame + the "PAU/RUN/STP" verbs). ----
 * sPaused holds the emulator when set; sStepBudget lets a paused emulator run a
 * bounded number of frames (single-step) before halting again. sFrameNo counts
 * emulated frames (bumped by SeExportSnapshot, i.e. once per completed frame). */
static volatile int sPaused;
static volatile int sStepBudget;
static volatile unsigned long long sFrameNo;

/* ---- Stop-event state (v5+). When the emulator halts on an execution
 * breakpoint, Yabause's breakpoint callback calls SeExportNotifyStop(); the next
 * snapshot's control block reports it so the debugger can jump to the halted PC.
 * A resume / run / step clears it. ---- */
static volatile unsigned int sStopReason;   /* SE_LIVE_STOP_* */
static volatile unsigned int sStopCpu;      /* 0 master, 1 slave */
static volatile unsigned int sStopPc;

/* ---- Breakpoint hooks (v5+). se_export stays free of Yabause headers: apply.py
 * wires these to Yabause's SH2 breakpoint API. SeAddExecBp(cpu, addr) installs one
 * execution breakpoint; SeClearBps() removes all. Both may be NULL (breakpoints
 * simply won't install, but the protocol still round-trips). ---- */
typedef void (*SeAddExecBpFn)(int cpu, unsigned int address);
typedef void (*SeClearBpsFn)(void);
static SeAddExecBpFn sAddExecBp;
static SeClearBpsFn  sClearBps;

void SeExportSetBreakpointHooks(SeAddExecBpFn add, SeClearBpsFn clear)
{
    sAddExecBp = add;
    sClearBps = clear;
}

/* Called from Yabause's breakpoint callback when the master/slave SH-2 hits an
 * execution breakpoint: latch the stop and hold the emulator paused. Plain
 * volatile writes (like sPaused elsewhere) — this runs in the CPU thread and must
 * not take the frame lock. */
void SeExportNotifyStop(int cpu, unsigned int pc)
{
    sStopReason = SE_LIVE_STOP_EXEC_BP;
    sStopCpu = (cpu != 0) ? 1u : 0u;
    sStopPc = pc;
    sPaused = 1;
    sStepBudget = 0;
}

/* Short self-contained sleep so the gate can spin-wait without a Yabause-
 * specific sleep primitive and without pegging a CPU core while paused. */
static void SeGateSleep(void)
{
#if defined(_WIN32)
    Sleep(2);
#else
    usleep(2000);
#endif
}

/* Called by the emulator's run loop at the top of each frame: returns 1 if the
 * frame should run, 0 if the debugger is holding it paused. When paused with a
 * pending single-step budget, it releases exactly one frame per call. When it
 * would return 0 it first sleeps ~2 ms internally, so the caller can simply spin
 *   while (!SeExportGateFrame()) { }
 * without its own sleep and without busy-pegging a core. The export server
 * thread keeps running, so a resume/step from Saturn Explorer releases the loop.
 * Safe to call even before SeExportInit (returns 1). */
int SeExportGateFrame(void)
{
    if (!sPaused)
    {
        return 1;
    }
    if (sStepBudget > 0)
    {
        --sStepBudget;
        return 1;
    }
    SeGateSleep();
    return 0;
}

#if defined(_WIN32)
static HANDLE sThread;
static CRITICAL_SECTION sLock;
#define SE_LOCK()   EnterCriticalSection(&sLock)
#define SE_UNLOCK() LeaveCriticalSection(&sLock)
#else
static pthread_t sThread;
static pthread_t sTcpThread;
static int sTcpThreadStarted = 0;
static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static int sListenFd = -1;
static int sTcpListenFd = -1;
#define SE_LOCK()   pthread_mutex_lock(&sLock)
#define SE_UNLOCK() pthread_mutex_unlock(&sLock)
#endif

static void SeWr32(unsigned char* p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

void SeExportSnapshot(const void* vdp1, const void* vdp2, const void* cram,
                      const void* vdp2struct, const void* vdp1struct,
                      const void* wramLow, const void* wramHigh,
                      const void* vdp1fb, const void* msh2, const void* ssh2)
{
    if (!sBack)
    {
        return;
    }
    SE_LOCK();
    if (vdp1) memcpy(sBack->v1, vdp1, SE_V1); else memset(sBack->v1, 0, SE_V1);
    if (vdp2) memcpy(sBack->v2, vdp2, SE_V2); else memset(sBack->v2, 0, SE_V2);
    if (cram) memcpy(sBack->cr, cram, SE_CR); else memset(sBack->cr, 0, SE_CR);
    if (vdp2struct) memcpy(sBack->vs, vdp2struct, SE_VS); else memset(sBack->vs, 0, SE_VS);
    SeBuildVdp1Image(sBack->vr, vdp1struct);
    if (wramLow)  memcpy(sBack->wl, wramLow,  SE_WL); else memset(sBack->wl, 0, SE_WL);
    if (wramHigh) memcpy(sBack->wh, wramHigh, SE_WH); else memset(sBack->wh, 0, SE_WH);
    if (vdp1fb)   memcpy(sBack->fb, vdp1fb,   SE_FB); else memset(sBack->fb, 0, SE_FB);
    /* SH-2 state: master then slave, each a 92-byte sh2regs_struct (host order). */
    if (msh2) memcpy(sBack->sh, msh2, SE_LIVE_SH2_REGS_LEN);
    else      memset(sBack->sh, 0, SE_LIVE_SH2_REGS_LEN);
    if (ssh2) memcpy(sBack->sh + SE_LIVE_SH2_REGS_LEN, ssh2, SE_LIVE_SH2_REGS_LEN);
    else      memset(sBack->sh + SE_LIVE_SH2_REGS_LEN, 0, SE_LIVE_SH2_REGS_LEN);
    sBack->valid = 1;
    {
        SeFrame* tmp = sFront; sFront = sBack; sBack = tmp;   /* swap */
    }
    ++sFrameNo;   /* one completed emulated frame */
    SE_UNLOCK();
}

/* ---- Blocking, exact-length socket I/O (0 = success). ---- */
#if defined(_WIN32)
static int SeSend(HANDLE h, const void* d, size_t n)
{
    const unsigned char* p = (const unsigned char*)d; DWORD w;
    while (n) { if (!WriteFile(h, p, (DWORD)n, &w, NULL) || w == 0) return -1; p += w; n -= w; }
    return 0;
}
static int SeRecv(HANDLE h, void* d, size_t n)
{
    unsigned char* p = (unsigned char*)d; DWORD r;
    while (n) { if (!ReadFile(h, p, (DWORD)n, &r, NULL) || r == 0) return -1; p += r; n -= r; }
    return 0;
}
#else
static int SeSend(int fd, const void* d, size_t n)
{
    const unsigned char* p = (const unsigned char*)d;
    while (n) { ssize_t w = send(fd, p, n, 0); if (w <= 0) return -1; p += w; n -= (size_t)w; }
    return 0;
}
static int SeRecv(int fd, void* d, size_t n)
{
    unsigned char* p = (unsigned char*)d;
    while (n) { ssize_t r = recv(fd, p, n, 0); if (r <= 0) return -1; p += r; n -= (size_t)r; }
    return 0;
}
#endif

/* Serve one connected client until it disconnects or the server stops. 'snap' is
 * scratch the size of one frame. */
#if defined(_WIN32)
static void SeServeClient(HANDLE cl, SeFrame* snap)
#else
static void SeServeClient(int cl, SeFrame* snap)
#endif
{
    while (sRunning)
    {
        unsigned char req[SE_LIVE_REQUEST_LEN];
        unsigned int arg;
        if (SeRecv(cl, req, SE_LIVE_REQUEST_LEN) != 0) return;
        arg = (unsigned int)req[4] | ((unsigned int)req[5] << 8) |
              ((unsigned int)req[6] << 16) | ((unsigned int)req[7] << 24);

        /* Apply any control verb before snapshotting, so the reply's control
         * block reflects the new state. Unknown verbs act like GET. Resuming or
         * stepping clears any latched breakpoint stop. */
        if (memcmp(req, SE_LIVE_VERB_PAUSE, SE_LIVE_VERB_LEN) == 0)
        {
            SE_LOCK(); sPaused = 1; sStepBudget = 0; SE_UNLOCK();
        }
        else if (memcmp(req, SE_LIVE_VERB_RESUME, SE_LIVE_VERB_LEN) == 0)
        {
            SE_LOCK(); sPaused = 0; sStepBudget = 0; sStopReason = SE_LIVE_STOP_NONE; SE_UNLOCK();
        }
        else if (memcmp(req, SE_LIVE_VERB_STEP, SE_LIVE_VERB_LEN) == 0)
        {
            SE_LOCK();
            sPaused = 1;
            sStepBudget += (arg > 0) ? (int)arg : 1;
            sStopReason = SE_LIVE_STOP_NONE;
            SE_UNLOCK();
        }
        else if (memcmp(req, SE_LIVE_VERB_BKPTS, SE_LIVE_VERB_LEN) == 0)
        {
            /* Read all 'arg' 12-byte descriptors (every one is consumed to keep the
             * stream aligned) and install the enabled execution breakpoints. */
            unsigned int i;
            if (sClearBps) { sClearBps(); }
            for (i = 0; i < arg; ++i)
            {
                unsigned char d[SE_LIVE_BKPT_DESC_LEN];
                unsigned int address, flags, kind, cpu, enabled;
                if (SeRecv(cl, d, SE_LIVE_BKPT_DESC_LEN) != 0) return;
                address = (unsigned int)d[0] | ((unsigned int)d[1] << 8) |
                          ((unsigned int)d[2] << 16) | ((unsigned int)d[3] << 24);
                /* d[4..7] size (unused for execution breakpoints). */
                flags = (unsigned int)d[8] | ((unsigned int)d[9] << 8) |
                        ((unsigned int)d[10] << 16) | ((unsigned int)d[11] << 24);
                kind = flags & SE_LIVE_BP_KIND_MASK;
                cpu = (flags & SE_LIVE_BP_CPU_SLAVE) ? 1u : 0u;
                enabled = (flags & SE_LIVE_BP_ENABLED) ? 1u : 0u;
                /* Only execution breakpoints are installed for now; memory
                 * breakpoints round-trip but need SH2AddMemoryBreakpoint wiring. */
                if (enabled && kind == 0u && sAddExecBp)
                {
                    sAddExecBp((int)cpu, address);
                }
            }
        }

        unsigned char ctl[SE_CT];
        SE_LOCK();
        memcpy(snap, sFront, sizeof(SeFrame));
        SeWr32(ctl, (unsigned int)(sPaused ? 1 : 0));
        SeWr32(ctl + 4, (unsigned int)(sFrameNo & 0xFFFFFFFFu));
        SeWr32(ctl + 8, (unsigned int)((sFrameNo >> 32) & 0xFFFFFFFFu));
        SeWr32(ctl + 12, sStopReason);
        SeWr32(ctl + 16, sStopCpu);
        SeWr32(ctl + 20, sStopPc);
        SE_UNLOCK();

        unsigned char hdr[SE_LIVE_HEADER_LEN];
        hdr[0] = SE_LIVE_MAGIC0; hdr[1] = SE_LIVE_MAGIC1;
        hdr[2] = SE_LIVE_MAGIC2; hdr[3] = SE_LIVE_MAGIC3;
        SeWr32(hdr + 4, SE_LIVE_VERSION);
        SeWr32(hdr + 8, SE_V1);  SeWr32(hdr + 12, SE_V2); SeWr32(hdr + 16, SE_CR);
        SeWr32(hdr + 20, SE_VS); SeWr32(hdr + 24, SE_VR); SeWr32(hdr + 28, SE_WL);
        SeWr32(hdr + 32, SE_WH); SeWr32(hdr + 36, SE_FB); SeWr32(hdr + 40, SE_CT);
        SeWr32(hdr + 44, SE_SH);
        if (SeSend(cl, hdr, sizeof(hdr)) != 0) return;
        if (SeSend(cl, snap->v1, SE_V1) != 0) return;
        if (SeSend(cl, snap->v2, SE_V2) != 0) return;
        if (SeSend(cl, snap->cr, SE_CR) != 0) return;
        if (SeSend(cl, snap->vs, SE_VS) != 0) return;
        if (SeSend(cl, snap->vr, SE_VR) != 0) return;
        if (SeSend(cl, snap->wl, SE_WL) != 0) return;
        if (SeSend(cl, snap->wh, SE_WH) != 0) return;
        if (SeSend(cl, snap->fb, SE_FB) != 0) return;
        if (SeSend(cl, ctl, SE_CT) != 0) return;
        if (SeSend(cl, snap->sh, SE_SH) != 0) return;
    }
}

#if defined(_WIN32)
static DWORD WINAPI SeServerThread(LPVOID arg)
{
    SeFrame* snap = (SeFrame*)malloc(sizeof(SeFrame));
    (void)arg;
    while (sRunning && snap)
    {
        HANDLE pipe = CreateNamedPipeA(SE_LIVE_DEFAULT_PIPE_NAME, PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                       1, 0, 0, 0, NULL);
        if (pipe == INVALID_HANDLE_VALUE) break;
        BOOL ok = ConnectNamedPipe(pipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (ok) SeServeClient(pipe, snap);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    free(snap);
    return 0;
}
#else
static void* SeServerThread(void* arg)
{
    SeFrame* snap = (SeFrame*)malloc(sizeof(SeFrame));
    struct sockaddr_un addr;
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    (void)arg;
    if (srv < 0 || !snap) { free(snap); return NULL; }
    unlink(SE_LIVE_DEFAULT_SOCK_PATH);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SE_LIVE_DEFAULT_SOCK_PATH, sizeof(addr.sun_path) - 1);
    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(srv, 1) != 0)
    {
        close(srv); free(snap); return NULL;
    }
    sListenFd = srv;
    while (sRunning)
    {
        int cl = accept(srv, NULL, NULL);
        if (cl < 0) break;   /* closed on deinit */
        SeServeClient(cl, snap);
        close(cl);
    }
    close(srv);
    unlink(SE_LIVE_DEFAULT_SOCK_PATH);
    free(snap);
    return NULL;
}

/* TCP listener on localhost:SE_LIVE_DEFAULT_TCP_PORT, serving the same blob. This
 * is the endpoint the web build reaches (its sockets are tunneled to a WebSocket
 * proxy); a native client connects with "tcp:127.0.0.1:6845". */
static void* SeTcpServerThread(void* arg)
{
    SeFrame* snap = (SeFrame*)malloc(sizeof(SeFrame));
    struct sockaddr_in addr;
    int on = 1;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    (void)arg;
    if (srv < 0 || !snap) { if (srv >= 0) close(srv); free(snap); return NULL; }
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(SE_LIVE_DEFAULT_TCP_PORT);
    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(srv, 1) != 0)
    {
        close(srv); free(snap); return NULL;
    }
    sTcpListenFd = srv;
    while (sRunning)
    {
        int cl = accept(srv, NULL, NULL);
        if (cl < 0) break;   /* closed on deinit */
        SeServeClient(cl, snap);
        close(cl);
    }
    close(srv);
    free(snap);
    return NULL;
}
#endif

int SeExportInit(void)
{
    sFront = (SeFrame*)calloc(1, sizeof(SeFrame));
    sBack  = (SeFrame*)calloc(1, sizeof(SeFrame));
    if (!sFront || !sBack) { SeExportDeinit(); return -1; }
    sPaused = 0; sStepBudget = 0; sFrameNo = 0;
    sStopReason = SE_LIVE_STOP_NONE; sStopCpu = 0; sStopPc = 0;
    sRunning = 1;
#if defined(_WIN32)
    InitializeCriticalSection(&sLock);
    sThread = CreateThread(NULL, 0, SeServerThread, NULL, 0, NULL);
    if (!sThread) { sRunning = 0; return -1; }
#else
    if (pthread_create(&sThread, NULL, SeServerThread, NULL) != 0) { sRunning = 0; return -1; }
    /* Best-effort TCP listener for the web bridge; failure doesn't block the
     * local socket, which is the primary path for native clients. */
    sTcpThreadStarted = (pthread_create(&sTcpThread, NULL, SeTcpServerThread, NULL) == 0);
#endif
    /* Surface the protocol version so a client/emulator mismatch is diagnosable
     * (Saturn Explorer must be built for the same SE_LIVE_VERSION to capture). */
    fprintf(stderr, "[SaturnExplorer] live tap ready: protocol v%u\n",
            (unsigned)SE_LIVE_VERSION);
    return 0;
}

void SeExportDeinit(void)
{
    sRunning = 0;
#if defined(_WIN32)
    if (sThread) { WaitForSingleObject(sThread, 1000); CloseHandle(sThread); sThread = NULL; }
    DeleteCriticalSection(&sLock);
#else
    if (sListenFd >= 0) { shutdown(sListenFd, SHUT_RDWR); close(sListenFd); sListenFd = -1; }
    if (sTcpListenFd >= 0) { shutdown(sTcpListenFd, SHUT_RDWR); close(sTcpListenFd); sTcpListenFd = -1; }
    pthread_join(sThread, NULL);
    if (sTcpThreadStarted) { pthread_join(sTcpThread, NULL); sTcpThreadStarted = 0; }
#endif
    free(sFront); sFront = NULL;
    free(sBack);  sBack = NULL;
}
