/* Saturn Explorer — portable memory-export server (the emulator-side live tap).
 * See se_export.h + the per-emulator READMEs (Integration/Yabause, ...).
 *
 * Emulator-agnostic: it takes raw memory pointers and function-pointer hooks, with
 * no emulator types, so one copy serves every backend. Each emulator's patcher
 * (e.g. Integration/Yabause/apply.py) copies this file + se_export.h +
 * SeLiveProtocol.h (all here in Integration/Common) into that emulator's tree and
 * injects the glue. Only needs SeLiveProtocol.h (beside this file) and the
 * platform's sockets/threads. */

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
#define SE_SR SE_LIVE_SOUND_RAM_LEN
#define SE_SL SE_LIVE_SCSP_BLOCK_LEN

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
    unsigned char sr[SE_SR];   /* SCSP sound RAM (v13); has_sr gates the wire block */
    int has_sr;                /* 1 if this frame captured sound RAM */
    unsigned char sl[SE_SL];   /* decoded SCSP slot block (v14); has_sl gates the wire block */
    int has_sl;                /* 1 if this frame captured SCSP slots */
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

/* N-frame ring of completed snapshots. The producer (SeExportSnapshot, CPU thread) writes
 * the next slot each frame and tags it with a monotonic frame number; a GET consumer asks
 * for the oldest frame newer than the one it last saw (gap-free) or, by default, the latest.
 * Fixed N keeps memory bounded. Was a 2-buffer front/back swap; widened so a client that
 * momentarily can't keep up doesn't miss transient one-frame states. */
#define SE_RING 4
static SeFrame* sRing[SE_RING];
static uint64_t sRingFrame[SE_RING];   /* frame number stored in each slot (0 = empty) */
static int      sRingWrite;            /* next slot the producer will write */
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

/* ---- Instruction-step state (v12+). The "IST" verb requests running the halted CPU
 * N instructions then halting. sInsnStepPending is set by the server thread and picked
 * up on the CPU thread (SeExportInsnStepBegin) after the halt gate releases; the per-
 * instruction hook then ticks sInsnStepBudget down (SeExportInsnStepTick) and halts at 0.
 * Only the CPU we were halted on (sInsnStepCpu) is counted. ---- */
static volatile int sInsnStepPending;   /* instruction count requested, 0 = none */
static volatile int sInsnStepBudget;    /* instructions remaining in the active step */
static volatile unsigned int sInsnStepCpu;

/* ---- Tracepoint events (v8+). The glue calls SeExportQueueTraceEvent() when an
 * installed tracepoint PC is hit (CPU thread); the server thread drains the ring into
 * each reply's trailing events block. FIFO ring; overflow drops the newest and counts
 * it so the client can note dropped events. Guarded by the frame lock (SE_LOCK). ---- */
#define SE_EVQ_CAP 128
typedef struct {
    unsigned int id, cpu, frame;
    unsigned int regs[SE_LIVE_EVENT_REGS];
} SeTraceEvent;
static SeTraceEvent sEvQ[SE_EVQ_CAP];
static unsigned int sEvHead;    /* index of the oldest pending event */
static unsigned int sEvCount;   /* number pending (<= SE_EVQ_CAP) */
static unsigned int sEvDropped; /* events dropped on overflow (reported once) */

/* ---- Debug log lines (v11+). The glue calls SeExportLog() to surface a diagnostic
 * string (e.g. the controller input it received); the server thread drains the ring into
 * each reply's trailing log block for the client's Log window. FIFO ring; overflow drops
 * the oldest. Guarded by SE_LOCK. ---- */
#define SE_LOGQ_CAP 64
static char sLogQ[SE_LOGQ_CAP][SE_LIVE_LOG_LINE_LEN];
static unsigned int sLogHead;   /* index of the oldest pending line */
static unsigned int sLogCount;  /* number pending (<= SE_LOGQ_CAP) */

/* ---- Shadow call stack (v9+). The glue records calls/returns as they execute
 * (SeExportPushFrame on bsr/jsr..., SeExportPopFrame on rts, SeExportResetCallStack on an
 * exception boundary), building a logical per-CPU stack. The server thread serializes it
 * into each reply's v9 call-stack block. Every frame here is genuinely observed, so the
 * client marks them ● Confirmed. Guarded by SE_LOCK. sCallDepth may exceed the cap; only
 * the innermost SE_LIVE_CALLSTACK_MAX are serialized. ---- */
#define SE_CALLSTACK_CAP 256
typedef struct {
    unsigned int callSite, func, ret, sp;
    unsigned long long cycle;
    unsigned int frameNo;
} SeCallFrame;
static SeCallFrame sCallStack[2][SE_CALLSTACK_CAP];
static unsigned int sCallDepth[2];   /* frames pushed (may saturate at CAP) */

/* ---- Breakpoint hooks (v5+). se_export stays free of Yabause headers: apply.py
 * wires these to Yabause's SH2 breakpoint API. SeAddExecBp(cpu, addr) installs one
 * execution breakpoint; SeClearBps() removes all. Both may be NULL (breakpoints
 * simply won't install, but the protocol still round-trips). ---- */
typedef void (*SeAddExecBpFn)(int cpu, unsigned int address);
typedef void (*SeClearBpsFn)(void);
typedef void (*SeAddMemBpFn)(int cpu, unsigned int address,
                             unsigned int size, unsigned int kind);
static SeAddExecBpFn sAddExecBp;
static SeClearBpsFn  sClearBps;
static SeAddMemBpFn  sAddMemBp;

void SeExportSetBreakpointHooks(SeAddExecBpFn add, SeClearBpsFn clear)
{
    sAddExecBp = add;
    sClearBps = clear;
}

void SeExportSetMemBreakpointHook(SeAddMemBpFn add)
{
    sAddMemBp = add;
}

/* ---- Memory-write hook (v6+). apply.py wires this to the emulator's byte writer
 * (Yabause: MappedMemoryWriteByteNocache(MSH2, addr, val)) so the Hex Editor can
 * poke work RAM. Writing byte-by-byte at Saturn addresses keeps big-endian
 * semantics without a manual swap. ---- */
typedef void (*SeWriteByteFn)(unsigned int address, unsigned char value);
static SeWriteByteFn sWriteByte;

void SeExportSetMemWriteHook(SeWriteByteFn fn)
{
    sWriteByte = fn;
}

/* ---- Sound-RAM write hook (v13+). apply.py wires this to the emulator's SCSP RAM byte
 * writer so the Hex Editor's Sound RAM tab / the music-swap prototype can poke a running
 * game's sound RAM. write(offset, value) writes one byte at a 0-based offset in the 512 KiB
 * sound RAM. May be NULL (sound-RAM writes are then dropped). ---- */
typedef void (*SeWriteSoundByteFn)(unsigned int offset, unsigned char value);
static SeWriteSoundByteFn sWriteSoundByte;

void SeExportSetSoundWriteHook(SeWriteSoundByteFn fn)
{
    sWriteSoundByte = fn;
}

/* ---- Controller-input hook (v7+). apply.py wires this to the emulator's pad state
 * so the Saturn Explorer controller panel can drive the game directly, bypassing the
 * emulator's own host-input mapping. `buttons` is the emulator-agnostic SE_PAD_* mask;
 * the glue translates it to the emulator's own pad bit order. May be NULL (input is
 * simply ignored, but the protocol still round-trips). ---- */
typedef void (*SeSetPadFn)(unsigned int port, unsigned int buttons);
static SeSetPadFn sSetPad;

void SeExportSetInputHook(SeSetPadFn fn)
{
    sSetPad = fn;
}

/* ---- Keyboard-map hook (v10+). apply.py wires this to the emulator's own live host
 * keyboard bindings so the client can mirror the user's keys without a config-file
 * upload. get(port, out[13]) fills out with the USB-HID scancode bound to each Saturn
 * pad button (ascending SE_PAD_* order), -1 where unbound; returns the count matched.
 * May be NULL (the keymap block is then all -1, and the client keeps its own defaults). */
typedef int (*SeGetKeyMapFn)(unsigned int port, int out[13]);
static SeGetKeyMapFn sGetKeyMap;

void SeExportSetKeyMapHook(SeGetKeyMapFn fn)
{
    sGetKeyMap = fn;
}

/* ---- Port device-type hook (v12+). apply.py wires this to the emulator's port map so
 * the client can report the emulator's controller configuration. get(port) returns a
 * short human-readable device name ("Digital Control Pad", "3D Control Pad", ...) for
 * port 0/1. May be NULL (nothing is logged on connect). ---- */
typedef const char* (*SePortInfoFn)(unsigned int port);
static SePortInfoFn sPortInfo;

void SeExportSetPortInfoHook(SePortInfoFn fn)
{
    sPortInfo = fn;
}

/* Log the emulator's controller type for ports 1 & 2, once per client connection, so
 * the Saturn Explorer Log window shows how the emulated inputs are configured. */
static void SeLogPortDevices(void)
{
    unsigned int p;
    if (!sPortInfo) return;
    for (p = 0; p < 2; ++p)
    {
        const char* name = sPortInfo(p);
        char msg[SE_LIVE_LOG_LINE_LEN];
        snprintf(msg, sizeof(msg), "port %u: %s", p + 1, name ? name : "?");
        SeExportLog(msg);
    }
}

static void SeReleaseInjectedPads(void)
{
    /* A client can disappear while a button is held. Never leave that state latched
     * in the emulator after its pipe/socket closes. */
    if (sSetPad)
    {
        sSetPad(0, 0);
        sSetPad(1, 0);
    }
}

/* ---- Tracepoint-install hook (v8+). apply.py wires this to the emulator's PC-trap
 * mechanism so the glue knows which addresses to watch; on a hit the glue calls
 * SeExportQueueTraceEvent(). set(count, descs) receives 'count' SE_LIVE_TRACE_DESC_LEN
 * descriptors {id,cpu,address,flags} (u32 LE). May be NULL (tracepoints then never
 * fire, but TRC still round-trips). ---- */
typedef void (*SeSetTracepointsFn)(unsigned int count, const unsigned char* descs);
static SeSetTracepointsFn sSetTracepoints;

void SeExportSetTracepointHook(SeSetTracepointsFn fn)
{
    sSetTracepoints = fn;
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

/* Like SeExportNotifyStop, but latches SE_LIVE_STOP_STEP — the halt that ends an
 * instruction step (IST) rather than a user breakpoint. Same CPU-thread contract. */
void SeExportNotifyStep(int cpu, unsigned int pc)
{
    sStopReason = SE_LIVE_STOP_STEP;
    sStopCpu = (cpu != 0) ? 1u : 0u;
    sStopPc = pc;
    sPaused = 1;
    sStepBudget = 0;
}

/* CPU thread, called from the per-instruction hook right after the halt gate releases:
 * if an instruction step was requested (IST verb), activate its budget and return 1 so
 * the caller arms continuous per-instruction hooking. Returns 0 when no step is pending. */
int SeExportInsnStepBegin(void)
{
    if (sInsnStepPending > 0)
    {
        sInsnStepBudget = sInsnStepPending;
        sInsnStepPending = 0;
        return 1;
    }
    return 0;
}

/* CPU thread, called once per executed instruction while an instruction step is active.
 * Counts only the CPU the step targets; returns 1 when the budget is exhausted (halt
 * here), 0 to keep running. */
int SeExportInsnStepTick(int cpu)
{
    if (sInsnStepBudget <= 0)
    {
        return 0;
    }
    if (((cpu != 0) ? 1u : 0u) != sInsnStepCpu)
    {
        return 0;
    }
    if (--sInsnStepBudget == 0)
    {
        return 1;
    }
    return 0;
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

/* Queue a fired tracepoint (v8+). Called from the CPU thread by the glue with the
 * captured SH-2 register file (23 u32, se_sh2_regs order). The frame is stamped here
 * from the module's counter, so the glue needs no frame access. Drops the newest on
 * overflow. 'regs' may be NULL (queues zeros). */
void SeExportQueueTraceEvent(unsigned int id, unsigned int cpu, const unsigned int* regs)
{
    unsigned int slot, i;
    SE_LOCK();
    if (sEvCount >= SE_EVQ_CAP)
    {
        ++sEvDropped;
        SE_UNLOCK();
        return;
    }
    slot = (sEvHead + sEvCount) % SE_EVQ_CAP;
    sEvQ[slot].id = id;
    sEvQ[slot].cpu = cpu ? 1u : 0u;
    sEvQ[slot].frame = (unsigned int)(sFrameNo & 0xFFFFFFFFu);
    for (i = 0; i < SE_LIVE_EVENT_REGS; ++i)
        sEvQ[slot].regs[i] = regs ? regs[i] : 0u;
    ++sEvCount;
    SE_UNLOCK();
}

/* Queue a diagnostic log line (v11+). Any-thread safe. 'msg' is copied (truncated to
 * SE_LIVE_LOG_LINE_LEN-1 chars); NULL is ignored. On overflow the oldest line is dropped
 * so the newest is always kept. The client drains these into its Log window. */
void SeExportLog(const char* msg)
{
    unsigned int slot, i;
    if (!msg) return;
    SE_LOCK();
    if (sLogCount >= SE_LOGQ_CAP)          /* full: drop the oldest */
    {
        sLogHead = (sLogHead + 1) % SE_LOGQ_CAP;
        --sLogCount;
    }
    slot = (sLogHead + sLogCount) % SE_LOGQ_CAP;
    for (i = 0; i + 1 < SE_LIVE_LOG_LINE_LEN && msg[i]; ++i)
        sLogQ[slot][i] = msg[i];
    for (; i < SE_LIVE_LOG_LINE_LEN; ++i)
        sLogQ[slot][i] = 0;
    ++sLogCount;
    SE_UNLOCK();
}

/* Shadow call stack (v9+). The glue calls these from the CPU thread as control flow
 * executes. Push on a call (bsr/jsr/...); pop on rts; reset at an exception boundary or
 * a discontinuity. The frame number is stamped here from the module counter. Depth
 * saturates at SE_CALLSTACK_CAP (further pushes are dropped, pops still balance once it
 * unwinds). */
void SeExportPushFrame(int cpu, unsigned int callSite, unsigned int func,
                       unsigned int ret, unsigned int sp, unsigned long long cycle)
{
    int c = cpu ? 1 : 0;
    SE_LOCK();
    if (sCallDepth[c] < SE_CALLSTACK_CAP)
    {
        SeCallFrame* f = &sCallStack[c][sCallDepth[c]];
        f->callSite = callSite; f->func = func; f->ret = ret; f->sp = sp;
        f->cycle = cycle;
        f->frameNo = (unsigned int)(sFrameNo & 0xFFFFFFFFu);
        ++sCallDepth[c];
    }
    SE_UNLOCK();
}

void SeExportPopFrame(int cpu)
{
    int c = cpu ? 1 : 0;
    SE_LOCK();
    if (sCallDepth[c]) --sCallDepth[c];
    SE_UNLOCK();
}

void SeExportResetCallStack(int cpu)
{
    int c = cpu ? 1 : 0;
    SE_LOCK();
    sCallDepth[c] = 0;
    SE_UNLOCK();
}

void SeExportSnapshot(const void* vdp1, const void* vdp2, const void* cram,
                      const void* vdp2struct, const void* vdp1struct,
                      const void* wramLow, const void* wramHigh,
                      const void* vdp1fb, const void* msh2, const void* ssh2,
                      const void* soundRam, const void* scspSlots)
{
    if (!sRing[0])
    {
        return;
    }
    SE_LOCK();
    {
    SeFrame* sBack = sRing[sRingWrite];   /* the slot this frame lands in */
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
    /* SCSP sound RAM (v13): only served on the wire when the emulator supplied it. */
    if (soundRam) { memcpy(sBack->sr, soundRam, SE_SR); sBack->has_sr = 1; }
    else          { memset(sBack->sr, 0, SE_SR);        sBack->has_sr = 0; }
    /* Decoded SCSP slots (v14): the glue hands over the pre-serialized 1152-byte block. */
    if (scspSlots) { memcpy(sBack->sl, scspSlots, SE_SL); sBack->has_sl = 1; }
    else           { memset(sBack->sl, 0, SE_SL);         sBack->has_sl = 0; }
    sBack->valid = 1;
    sRingFrame[sRingWrite] = ++sFrameNo;               /* tag this slot with its frame number */
    sRingWrite = (sRingWrite + 1) % SE_RING;           /* advance (wraps, overwriting oldest) */
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
    SeLogPortDevices();   /* report the emulator's controller config on connect */
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
        else if (memcmp(req, SE_LIVE_VERB_ISTEP, SE_LIVE_VERB_LEN) == 0)
        {
            /* Instruction step: release the CPU (it is spinning in the breakpoint gate)
             * and let it run `arg` instructions on the halted CPU before halting again.
             * The per-instruction hook picks up sInsnStepPending once the gate releases. */
            SE_LOCK();
            sInsnStepPending = (arg > 0) ? (int)arg : 1;
            sInsnStepCpu = sStopCpu;
            sPaused = 0;
            sStepBudget = 0;
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
                unsigned int address, size, flags, kind, cpu, enabled;
                if (SeRecv(cl, d, SE_LIVE_BKPT_DESC_LEN) != 0) return;
                address = (unsigned int)d[0] | ((unsigned int)d[1] << 8) |
                          ((unsigned int)d[2] << 16) | ((unsigned int)d[3] << 24);
                size = (unsigned int)d[4] | ((unsigned int)d[5] << 8) |
                       ((unsigned int)d[6] << 16) | ((unsigned int)d[7] << 24);
                flags = (unsigned int)d[8] | ((unsigned int)d[9] << 8) |
                        ((unsigned int)d[10] << 16) | ((unsigned int)d[11] << 24);
                kind = flags & SE_LIVE_BP_KIND_MASK;
                cpu = (flags & SE_LIVE_BP_CPU_SLAVE) ? 1u : 0u;
                enabled = (flags & SE_LIVE_BP_ENABLED) ? 1u : 0u;
                /* kind 0 = execution (PC); 1/2/3 = read/write/read-write data
                 * breakpoints (watchpoints) over [address, address+size). The
                 * descriptor was already consumed above, so a disabled one just
                 * skips installation without desyncing the stream. */
                if (!enabled) continue;
                if (kind == 0u)
                {
                    if (sAddExecBp) sAddExecBp((int)cpu, address);
                }
                else if (sAddMemBp)
                {
                    sAddMemBp((int)cpu, address, size ? size : 1u, kind);
                }
            }
        }
        else if (memcmp(req, SE_LIVE_VERB_WRITE, SE_LIVE_VERB_LEN) == 0)
        {
            /* Poke work RAM: payload = address(4 LE) + 'arg' big-endian bytes. */
            unsigned char addrb[4];
            unsigned int i, address;
            if (SeRecv(cl, addrb, 4) != 0) return;
            address = (unsigned int)addrb[0] | ((unsigned int)addrb[1] << 8) |
                      ((unsigned int)addrb[2] << 16) | ((unsigned int)addrb[3] << 24);
            for (i = 0; i < arg; ++i)
            {
                unsigned char v;
                if (SeRecv(cl, &v, 1) != 0) return;
                if (sWriteByte) sWriteByte(address + i, v);
            }
        }
        else if (memcmp(req, SE_LIVE_VERB_WRITESND, SE_LIVE_VERB_LEN) == 0)
        {
            /* Poke sound RAM (v13+): payload = offset(4 LE) + 'arg' raw bytes. */
            unsigned char offb[4];
            unsigned int i, offset;
            if (SeRecv(cl, offb, 4) != 0) return;
            offset = (unsigned int)offb[0] | ((unsigned int)offb[1] << 8) |
                     ((unsigned int)offb[2] << 16) | ((unsigned int)offb[3] << 24);
            for (i = 0; i < arg; ++i)
            {
                unsigned char v;
                if (SeRecv(cl, &v, 1) != 0) return;
                if (sWriteSoundByte) sWriteSoundByte(offset + i, v);
            }
        }
        else if (memcmp(req, SE_LIVE_VERB_INPUT, SE_LIVE_VERB_LEN) == 0)
        {
            /* Inject controller state: arg packs port (high 16) + SE_PAD_* mask (low
             * 16). No payload. The glue drives the emulated pad directly. */
            if (sSetPad) sSetPad(SE_LIVE_INPUT_PORT(arg), SE_LIVE_INPUT_BUTTONS(arg));
        }
        else if (memcmp(req, SE_LIVE_VERB_TRACE, SE_LIVE_VERB_LEN) == 0)
        {
            /* Install tracepoints: 'arg' 16-byte descriptors. Buffer up to a cap and
             * hand them to the glue; consume any beyond the cap to stay stream-aligned. */
            static unsigned char tbuf[SE_LIVE_TRACE_DESC_LEN * 256];
            unsigned int keep = arg > 256u ? 256u : arg, i;
            if (keep && SeRecv(cl, tbuf, keep * SE_LIVE_TRACE_DESC_LEN) != 0) return;
            for (i = keep; i < arg; ++i)
            {
                unsigned char d[SE_LIVE_TRACE_DESC_LEN];
                if (SeRecv(cl, d, SE_LIVE_TRACE_DESC_LEN) != 0) return;
            }
            if (sSetTracepoints) sSetTracepoints(keep, tbuf);
        }

        /* Which ring frame to serve: a GET carries the client's last-seen frame (arg) and
         * gets the OLDEST frame newer than it (gap-free, so no emulated frame is skipped
         * while the client keeps up); arg 0, or any non-GET request, gets the latest. */
        unsigned char ctl[SE_CT];
        unsigned int lastSeen = 0;
        uint64_t served;
        if (memcmp(req, SE_LIVE_VERB_GET, SE_LIVE_VERB_LEN) == 0) lastSeen = arg;
        SE_LOCK();
        {
            int slot = (sRingWrite + SE_RING - 1) % SE_RING;   /* latest by default */
            if (lastSeen != 0)
            {
                int i, found = -1;
                uint32_t best = 0;
                for (i = 0; i < SE_RING; ++i)
                {
                    uint32_t f = (uint32_t)sRingFrame[i];
                    if (sRingFrame[i] != 0 && f > lastSeen && (found < 0 || f < best))
                    { best = f; found = i; }
                }
                if (found >= 0) slot = found;   /* oldest unseen; else stay on latest */
            }
            memcpy(snap, sRing[slot], sizeof(SeFrame));
            served = sRingFrame[slot];
        }
        SeWr32(ctl, (unsigned int)(sPaused ? 1 : 0));
        SeWr32(ctl + 4, (unsigned int)(served & 0xFFFFFFFFu));
        SeWr32(ctl + 8, (unsigned int)((served >> 32) & 0xFFFFFFFFu));
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

        /* v8 trailing block: fired tracepoint events. u32 count, then that many
         * SE_LIVE_EVENT_LEN records. Drained FIFO, capped per reply. */
        {
            unsigned char cntb[4];
            unsigned int n, i, j;
            SE_LOCK();
            n = sEvCount;
            SE_UNLOCK();
            if (n > SE_LIVE_EVENTS_MAX) n = SE_LIVE_EVENTS_MAX;
            SeWr32(cntb, n);
            if (SeSend(cl, cntb, 4) != 0) return;
            for (i = 0; i < n; ++i)
            {
                SeTraceEvent ev;
                unsigned char eb[SE_LIVE_EVENT_LEN];
                SE_LOCK();
                ev = sEvQ[sEvHead];
                sEvHead = (sEvHead + 1) % SE_EVQ_CAP;
                if (sEvCount) --sEvCount;
                SE_UNLOCK();
                SeWr32(eb, ev.id);
                SeWr32(eb + 4, ev.cpu);
                SeWr32(eb + 8, ev.frame);
                for (j = 0; j < SE_LIVE_EVENT_REGS; ++j)
                    SeWr32(eb + 12 + j * 4, ev.regs[j]);
                if (SeSend(cl, eb, SE_LIVE_EVENT_LEN) != 0) return;
            }
        }

        /* v9 trailing block: the recorded per-CPU shadow call stack. For master then
         * slave: u32 frameCount (capped), then that many frames, innermost (current)
         * first — so the client's frame #0 is the deepest call. */
        {
            int c;
            for (c = 0; c < 2; ++c)
            {
                unsigned char cntb[4];
                unsigned int depth, n, i;
                SE_LOCK();
                depth = sCallDepth[c];
                SE_UNLOCK();
                n = (depth > SE_LIVE_CALLSTACK_MAX) ? SE_LIVE_CALLSTACK_MAX : depth;
                SeWr32(cntb, n);
                if (SeSend(cl, cntb, 4) != 0) return;
                for (i = 0; i < n; ++i)
                {
                    SeCallFrame f;
                    unsigned char fb[SE_LIVE_CALLFRAME_LEN];
                    SE_LOCK();
                    /* Innermost first: index (depth-1) is the current frame. */
                    f = sCallStack[c][depth - 1 - i];
                    SE_UNLOCK();
                    SeWr32(fb + 0,  f.callSite);
                    SeWr32(fb + 4,  f.func);
                    SeWr32(fb + 8,  f.ret);
                    SeWr32(fb + 12, f.sp);
                    SeWr32(fb + 16, (unsigned int)(f.cycle & 0xFFFFFFFFu));
                    SeWr32(fb + 20, (unsigned int)((f.cycle >> 32) & 0xFFFFFFFFu));
                    SeWr32(fb + 24, f.frameNo);
                    if (SeSend(cl, fb, SE_LIVE_CALLFRAME_LEN) != 0) return;
                }
            }
        }

        /* v10 trailing block: the emulator's live host keyboard bindings. For port 0 then
         * port 1: 13 int32 (LE) USB-HID scancodes, one per Saturn pad button (ascending
         * SE_PAD_* order), -1 where no keyboard key is bound. Lets the client mirror the
         * user's keys with no config-file upload. All -1 when no hook is registered. */
        {
            int p, b;
            for (p = 0; p < SE_LIVE_KEYMAP_PORTS; ++p)
            {
                int km[SE_LIVE_KEYMAP_BUTTONS];
                unsigned char kb[SE_LIVE_KEYMAP_BUTTONS * 4];
                for (b = 0; b < SE_LIVE_KEYMAP_BUTTONS; ++b) km[b] = -1;
                if (sGetKeyMap) sGetKeyMap((unsigned int)p, km);
                for (b = 0; b < SE_LIVE_KEYMAP_BUTTONS; ++b)
                    SeWr32(kb + b * 4, (unsigned int)km[b]);
                if (SeSend(cl, kb, sizeof(kb)) != 0) return;
            }
        }

        /* v11 trailing block: diagnostic log lines. u32 count (capped), then that many
         * fixed-length NUL-padded records. Drained FIFO so the client sees them once. */
        {
            unsigned char cntb[4];
            unsigned int n, i;
            SE_LOCK();
            n = sLogCount;
            SE_UNLOCK();
            if (n > SE_LIVE_LOG_MAX) n = SE_LIVE_LOG_MAX;
            SeWr32(cntb, n);
            if (SeSend(cl, cntb, 4) != 0) return;
            for (i = 0; i < n; ++i)
            {
                char line[SE_LIVE_LOG_LINE_LEN];
                SE_LOCK();
                memcpy(line, sLogQ[sLogHead], SE_LIVE_LOG_LINE_LEN);
                sLogHead = (sLogHead + 1) % SE_LOGQ_CAP;
                if (sLogCount) --sLogCount;
                SE_UNLOCK();
                line[SE_LIVE_LOG_LINE_LEN - 1] = 0;
                if (SeSend(cl, (unsigned char*)line, SE_LIVE_LOG_LINE_LEN) != 0) return;
            }
        }

        /* v13 trailing block: SCSP sound RAM. u32 length (LE) — SE_LIVE_SOUND_RAM_LEN when
         * the emulator supplied it (then that many raw bytes follow), else 0 (no bytes). The
         * captured image was copied into 'snap' under the lock above. */
        {
            unsigned char lenb[4];
            unsigned int len = snap->has_sr ? (unsigned int)SE_SR : 0u;
            SeWr32(lenb, len);
            if (SeSend(cl, lenb, 4) != 0) return;
            if (len && SeSend(cl, snap->sr, SE_SR) != 0) return;
        }

        /* v14 trailing block: decoded SCSP slots. u32 length (SE_LIVE_SCSP_BLOCK_LEN when the
         * glue supplied it, else 0), then that many pre-serialized bytes. */
        {
            unsigned char lenb[4];
            unsigned int len = snap->has_sl ? (unsigned int)SE_SL : 0u;
            SeWr32(lenb, len);
            if (SeSend(cl, lenb, 4) != 0) return;
            if (len && SeSend(cl, snap->sl, SE_SL) != 0) return;
        }
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
        SeReleaseInjectedPads();
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
        SeReleaseInjectedPads();
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
        SeReleaseInjectedPads();
        close(cl);
    }
    close(srv);
    free(snap);
    return NULL;
}
#endif

const char* SeExportTitleSuffix(const char* emu_name, const char* emu_rev)
{
    static char buf[160];
    snprintf(buf, sizeof(buf), "(SaturnExplorer Enabled. %s / %s %s)",
             SE_EXPORT_VERSION, emu_name ? emu_name : "?", emu_rev ? emu_rev : "?");
    return buf;
}

int SeExportInit(void)
{
    {
        int i;
        for (i = 0; i < SE_RING; ++i)
        {
            sRing[i] = (SeFrame*)calloc(1, sizeof(SeFrame));
            sRingFrame[i] = 0;
            if (!sRing[i]) { SeExportDeinit(); return -1; }
        }
    }
    sRingWrite = 0;
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
    {
        int i;
        for (i = 0; i < SE_RING; ++i) { free(sRing[i]); sRing[i] = NULL; sRingFrame[i] = 0; }
    }
    sRingWrite = 0;
}
