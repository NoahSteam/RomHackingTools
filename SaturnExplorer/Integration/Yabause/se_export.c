/* Saturn Explorer — Yabause memory-export server. See se_export.h + README.md.
 * Self-contained: only needs SeLiveProtocol.h (copy it in from
 * Drivers/Common/src/) and the platform's sockets/threads. */

#include "se_export.h"
#include "SeLiveProtocol.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
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

typedef struct
{
    unsigned char v1[SE_V1];   /* VDP1 VRAM */
    unsigned char v2[SE_V2];   /* VDP2 VRAM */
    unsigned char cr[SE_CR];   /* CRAM */
    unsigned char vs[SE_VS];   /* raw Vdp2 register struct */
    unsigned char vr[SE_VR];   /* hardware-offset BE VDP1 register image */
    unsigned char wl[SE_WL];   /* low work RAM */
    unsigned char wh[SE_WH];   /* high work RAM */
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

#if defined(_WIN32)
static HANDLE sThread;
static CRITICAL_SECTION sLock;
#define SE_LOCK()   EnterCriticalSection(&sLock)
#define SE_UNLOCK() LeaveCriticalSection(&sLock)
#else
static pthread_t sThread;
static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static int sListenFd = -1;
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
                      const void* wramLow, const void* wramHigh)
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
    sBack->valid = 1;
    {
        SeFrame* tmp = sFront; sFront = sBack; sBack = tmp;   /* swap */
    }
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
        char req[SE_LIVE_REQUEST_LEN];
        if (SeRecv(cl, req, SE_LIVE_REQUEST_LEN) != 0) return;
        SE_LOCK(); memcpy(snap, sFront, sizeof(SeFrame)); SE_UNLOCK();

        unsigned char hdr[SE_LIVE_HEADER_LEN];
        hdr[0] = SE_LIVE_MAGIC0; hdr[1] = SE_LIVE_MAGIC1;
        hdr[2] = SE_LIVE_MAGIC2; hdr[3] = SE_LIVE_MAGIC3;
        SeWr32(hdr + 4, SE_LIVE_VERSION);
        SeWr32(hdr + 8, SE_V1);  SeWr32(hdr + 12, SE_V2); SeWr32(hdr + 16, SE_CR);
        SeWr32(hdr + 20, SE_VS); SeWr32(hdr + 24, SE_VR); SeWr32(hdr + 28, SE_WL);
        SeWr32(hdr + 32, SE_WH);
        if (SeSend(cl, hdr, sizeof(hdr)) != 0) return;
        if (SeSend(cl, snap->v1, SE_V1) != 0) return;
        if (SeSend(cl, snap->v2, SE_V2) != 0) return;
        if (SeSend(cl, snap->cr, SE_CR) != 0) return;
        if (SeSend(cl, snap->vs, SE_VS) != 0) return;
        if (SeSend(cl, snap->vr, SE_VR) != 0) return;
        if (SeSend(cl, snap->wl, SE_WL) != 0) return;
        if (SeSend(cl, snap->wh, SE_WH) != 0) return;
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
#endif

int SeExportInit(void)
{
    sFront = (SeFrame*)calloc(1, sizeof(SeFrame));
    sBack  = (SeFrame*)calloc(1, sizeof(SeFrame));
    if (!sFront || !sBack) { SeExportDeinit(); return -1; }
    sRunning = 1;
#if defined(_WIN32)
    InitializeCriticalSection(&sLock);
    sThread = CreateThread(NULL, 0, SeServerThread, NULL, 0, NULL);
    if (!sThread) { sRunning = 0; return -1; }
#else
    if (pthread_create(&sThread, NULL, SeServerThread, NULL) != 0) { sRunning = 0; return -1; }
#endif
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
    pthread_join(sThread, NULL);
#endif
    free(sFront); sFront = NULL;
    free(sBack);  sBack = NULL;
}
