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
#include "SeStateCodec.h"   /* XOR + RLE savestate delta codec (v16 rewind) */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#if !defined(__GNUC__) && !defined(__clang__)
#include <intrin.h>   /* _Interlocked* for the MSVC branch of the atomics below */
#endif
#else
#include <pthread.h>
#include <errno.h>
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
    unsigned char cd[SE_LIVE_CD_BLOCK_LEN];  /* CD-block status (v15); has_cd gates the block */
    int has_cd;                /* 1 if this frame captured CD status */
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
/* Frame-step completion (v20). sStepOutstanding is the frames a STP granted that have not been
 * published yet; sStepInflight marks that the frame now running was granted by the budget (as
 * opposed to one that began free-running before the STP landed), so only a granted frame
 * retires an outstanding step. Together they let the client tell "the step has been run and
 * published" from "it is still on its way", instead of counting UI frames. */
static volatile int sStepOutstanding;
static volatile int sStepInflight;

/* The pause/step state is shared by three threads: the server thread (verbs), the emulate thread
 * (the frame gate) and the CPU hook (breakpoint stops). A plain volatile int is neither atomic
 * across a read-modify-write nor ordered, and the old unlocked decrement in the gate could
 * overwrite a concurrent STP's increment and lose a requested step. Every access goes through
 * these instead. */
#if defined(__GNUC__) || defined(__clang__)
#define SeAtLoad(p)       __atomic_load_n((p), __ATOMIC_SEQ_CST)
#define SeAtStore(p, v)   __atomic_store_n((p), (v), __ATOMIC_SEQ_CST)
#define SeAtAdd(p, v)     ((void)__atomic_fetch_add((p), (v), __ATOMIC_SEQ_CST))
#define SeAtCas(p, e, d)  __atomic_compare_exchange_n((p), (e), (d), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#define SeAtXchg(p, v)    __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)
#define SeAtLoad64(p)     __atomic_load_n((p), __ATOMIC_SEQ_CST)
#define SeAtStore64(p, v) __atomic_store_n((p), (v), __ATOMIC_SEQ_CST)
#define SeAtCas64(p, e, d) __atomic_compare_exchange_n((p), (e), (d), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#elif defined(_WIN32)
#define SeAtLoad(p)       ((int)_InterlockedCompareExchange((volatile long*)(p), 0, 0))
#define SeAtStore(p, v)   ((void)_InterlockedExchange((volatile long*)(p), (long)(v)))
#define SeAtAdd(p, v)     ((void)_InterlockedExchangeAdd((volatile long*)(p), (long)(v)))
static int SeAtCasWin(volatile int* p, int* e, int d)
{
    long prev = _InterlockedCompareExchange((volatile long*)p, (long)d, (long)*e);
    if (prev == (long)*e) return 1;
    *e = (int)prev;
    return 0;
}
#define SeAtCas(p, e, d)  SeAtCasWin((p), (e), (d))
#define SeAtXchg(p, v)    ((int)_InterlockedExchange((volatile long*)(p), (long)(v)))
#define SeAtLoad64(p)     ((unsigned long long)_InterlockedCompareExchange64((volatile __int64*)(p), 0, 0))
#define SeAtStore64(p, v) ((void)_InterlockedExchange64((volatile __int64*)(p), (__int64)(v)))
static int SeAtCas64Win(volatile unsigned long long* p, unsigned long long* e, unsigned long long d)
{
    const unsigned long long prev = (unsigned long long)_InterlockedCompareExchange64(
        (volatile __int64*)p, (__int64)d, (__int64)*e);
    if (prev == *e) return 1;
    *e = prev;
    return 0;
}
#define SeAtCas64(p, e, d) SeAtCas64Win((p), (e), (d))
#else
#error "se_export.c needs atomic operations: GCC/Clang builtins or the Windows Interlocked API"
#endif

/* Drop whatever step budget is left: a resume, a pause, a breakpoint stop or a restore all
 * make the remaining requested frames moot, and the client must not wait for them. */
static void SeCancelSteps(void)
{
    SeAtStore(&sStepBudget, 0);
    SeAtStore(&sStepOutstanding, 0);
    SeAtStore(&sStepInflight, 0);
}

/* A STP verb: grant 'n' more frames. Outstanding first, so a reader never sees budget without
 * the matching pending count. */
static void SeGrantSteps(int n)
{
    SeAtAdd(&sStepOutstanding, n);
    SeAtAdd(&sStepBudget, n);
}

/* Emulate thread, at the frame gate: take one frame of budget, if any. */
static int SeTakeStepFrame(void)
{
    int b = SeAtLoad(&sStepBudget);
    while (b > 0)
    {
        SeAtStore(&sStepInflight, 1);   /* before the decrement, so pending never reads 0 mid-grant */
        if (SeAtCas(&sStepBudget, &b, b - 1)) return 1;
    }
    SeAtStore(&sStepInflight, 0);   /* the budget was cancelled under us: nothing was granted */
    return 0;
}

/* A frame has just been published: if it was a granted step, retire it. */
static void SeStepFramePublished(void)
{
    if (SeAtLoad(&sStepInflight))
    {
        int o = SeAtLoad(&sStepOutstanding);
        SeAtStore(&sStepInflight, 0);
        while (o > 0 && !SeAtCas(&sStepOutstanding, &o, o - 1)) { }
    }
}
static volatile unsigned long long sFrameNo;

/* ---- Stop-event state (v5+). When the emulator halts on an execution
 * breakpoint, Yabause's breakpoint callback calls SeExportNotifyStop(); the next
 * snapshot's control block reports it so the debugger can jump to the halted PC.
 * A resume / run / step clears it. ---- */
/* The fields describe ONE stop, written by the CPU thread (a breakpoint hit) and by the server
 * thread (a resume clears it) and read by the server thread when it builds a reply. As separate
 * variables a reader could pair one stop's reason with another's PC, so they are packed into a
 * single word that is only ever loaded and stored whole:
 *   seq << 36 | reason << 33 | cpu << 32 | pc
 * 'seq' counts the stops that have been published, and survives a clear. It is what lets a client
 * tell a NEW halt from a re-report of the one it already has: comparing PCs cannot, because a
 * step that lands on the PC it started from (a taken branch to itself) is a different halt at the
 * same address. 28 bits, wrapping; 0 means no stop has been published yet. */
static volatile unsigned long long sStopWord;   /* reason: SE_LIVE_STOP_*, cpu: 0 master / 1 slave */
#define SE_STOP_SEQ_MASK 0x0FFFFFFFu
#define SE_STOP_PACK(reason, cpu, pc, seq) \
    (((unsigned long long)((seq) & SE_STOP_SEQ_MASK) << 36) | \
     ((unsigned long long)((reason) & 7u) << 33) | \
     ((unsigned long long)((cpu) ? 1u : 0u) << 32) | \
     (unsigned long long)(unsigned int)(pc))
#define SE_STOP_REASON(w) ((unsigned int)(((w) >> 33) & 7u))
#define SE_STOP_CPU(w)    ((unsigned int)(((w) >> 32) & 1u))
#define SE_STOP_PC(w)     ((unsigned int)((w) & 0xFFFFFFFFu))
#define SE_STOP_SEQ(w)    ((unsigned int)(((w) >> 36) & SE_STOP_SEQ_MASK))

/* Latch a stop (CPU thread). The only writer of a NEW stop, so the sequence number it takes is
 * not contended; a clear from the server thread is a compare-and-swap (below), so it can neither
 * lose this write nor resurrect an older word over it. */
static void SeStopSet(unsigned int reason, int cpu, unsigned int pc)
{
    unsigned long long w = SeAtLoad64(&sStopWord);
    for (;;)
    {
        const unsigned long long next = SE_STOP_PACK(reason, cpu, pc, SE_STOP_SEQ(w) + 1u);
        if (SeAtCas64(&sStopWord, &w, next)) return;
    }
}

/* Clear the stop reason, keeping which CPU and PC it was on (an instruction step reads the CPU
 * after the clear is requested) and the sequence number (the next stop continues from it). A
 * compare-and-swap loop: a plain load-modify-store here could overwrite a stop the CPU thread
 * published in between, leaving the NEW stop's reason erased under its own sequence number. */
static void SeStopClear(void)
{
    unsigned long long w = SeAtLoad64(&sStopWord);
    for (;;)
    {
        if (SE_STOP_REASON(w) == SE_LIVE_STOP_NONE) return;
        if (SeAtCas64(&sStopWord, &w,
                      SE_STOP_PACK(SE_LIVE_STOP_NONE, SE_STOP_CPU(w), SE_STOP_PC(w), SE_STOP_SEQ(w))))
            return;
    }
}

/* ---- Restore outcomes (v19). A load (LST/ELS) is applied later, on the emulate thread, and
 * the request's own reply says nothing about it. These let the client tell "finished" from
 * "not yet" instead of guessing from elapsed time. Every accepted load request ends in exactly
 * one of the two counters: sRestoreDone once the first frame of the restored timeline has been
 * published to the ring (so a reply that reports it also carries post-restore data), or
 * sRestoreFailed when the load was refused or could not be applied. Both are guarded by
 * SE_LOCK and only ever increase. ---- */
static unsigned int sRestoreDone;
static unsigned int sRestoreFailed;
/* Every state load that has been settled, one way or the other: bumped when a load is applied and
 * when one is refused or superseded. Stamped on each savestate block at capture (v22), so a
 * client can tell the blocks of the timeline a load abandoned from those of the one it started.
 * Guarded by the same lock as the two above. */
static unsigned int sRestoreResolved;
/* Restores applied whose "done" has not been counted yet. A COUNT, not a flag: a rewind load and an
 * emulator-slot load use separate mailboxes, so both can run in one gate call, and a flag would
 * collapse the two into a single completion -- the client, which waits for one outcome per accepted
 * request, would then wait for the second forever. */
static unsigned int sRestoreAckPending;

/* ---- Instruction-step state (v12+). The "IST" verb requests running the halted CPU
 * N instructions then halting. sInsnStepPending is set by the server thread and picked
 * up on the CPU thread (SeExportInsnStepBegin) after the halt gate releases; the per-
 * instruction hook then ticks sInsnStepBudget down (SeExportInsnStepTick) and halts at 0.
 * Only the CPU we were halted on (sInsnStepCpu) is counted.
 *
 * What counts is a RETIRED instruction, not a per-instruction-hook call: the hook fires
 * before each attempted step, and the SH-2 can be bus-stalled (e.g. held off the bus by a
 * long SCU DMA), where the same PC is presented repeatedly without retiring. The tick
 * therefore does not spend budget on a presentation of the PC it last counted -- with two
 * explicit exceptions that a PC comparison alone gets wrong:
 *
 *  - A taken branch TO ITSELF (bt/bf/bra with a displacement that lands back on the branch)
 *    retires an instruction and leaves the PC where it was, so for that instruction a repeated
 *    PC IS a retirement. The caller, which can decode the instruction, says so
 *    (selfBranchTaken). Without it a one-instruction step over such a loop never completes.
 *  - The halt hook runs BEFORE the instruction at the halt PC executes, so after resuming from
 *    it the instruction has been run and a presentation of the same PC is its successor. The
 *    exception is a halt that did not come from the hook -- an SCU-DMA watchpoint, which stops
 *    between instructions: there the instruction at the halt PC is still pending, and its first
 *    presentation is not a retirement (sStepPendingFirst). ---- */
static volatile int sInsnStepPending;   /* instruction count requested, 0 = none */
static volatile int sInsnStepBudget;    /* instructions remaining in the active step */
static volatile int sInsnStepCpu;
static volatile unsigned int sStepLastPc[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu }; /* last-counted PC per CPU */
static volatile int sStepPendingFirst[2];   /* the instruction at sStepLastPc has not run yet */

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

/* ---- Shadow call stack (v9+). The glue records calls/returns as they execute, building
 * a logical per-CPU stack that the server thread serializes into each reply's v9
 * call-stack block. Every frame here is genuinely observed, so the client marks them
 * ● Confirmed. Guarded by SE_LOCK. Only the innermost SE_LIVE_CALLSTACK_MAX are
 * serialized.
 *
 * Each frame records what created it, because on SH-2 that decides what may remove it:
 * `rts` returns to PR (written by a bsr/bsrf/jsr) and `rte` returns to the PC the
 * hardware pushed onto R15 at an exception entry. See se_export.h for why conflating the
 * two drained the stack on any interrupt-driven program. ---- */
#define SE_FRAME_CALL       0u   /* pushed by bsr/bsrf/jsr; removed by rts */
#define SE_FRAME_EXCEPTION  1u   /* pushed at an exception entry; removed by rte */
typedef struct {
    unsigned int callSite, func, ret, sp;
    unsigned long long cycle;
    unsigned int frameNo;
    unsigned int kind;           /* SE_FRAME_* — internal, not on the wire */
} SeCallFrame;
static SeCallFrame sCallStack[2][SE_CALLSTACK_CAP];
static unsigned int sCallDepth[2];   /* frames stored (saturates at CAP) */
/* Frames deeper than we store. Counted rather than dropped so their returns unwind the
 * counter instead of deleting a stored frame that is still live. */
static unsigned int sCallOverflow[2];

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

/* Frame lock + server/transport handles (used across the file; declared up here so the
 * savestate rewind code below can invalidate the frame ring under SE_LOCK). */
#if defined(_WIN32)
static HANDLE sThread;
static CRITICAL_SECTION sLock;
/* A CRITICAL_SECTION needs runtime InitializeCriticalSection (done in SeExportInit), unlike
 * the POSIX PTHREAD_MUTEX_INITIALIZER below which is ready at load. But some producers run
 * mid-frame inside Emulate() -- input-diagnostic logs (smpc.cpp), the shadow call stack,
 * tracepoints -- and the injected SeExportInit call fires only at the *end-of-frame* hook,
 * so on frame 1 the lock is entered before it is initialized. Gate entry on sLocksReady:
 * until init completes the emulator is single-threaded (the server and savestate-worker
 * threads are created *by* SeExportInit), so the guarded data can be touched safely without
 * a lock; entering an uninitialized CRITICAL_SECTION would instead fault inside
 * RtlpEnterCriticalSectionContended. Shared by SE_SLOCK below (both CSes init together). */
static volatile LONG sLocksReady = 0;
#define SE_LOCK()   do { if (sLocksReady) EnterCriticalSection(&sLock); } while (0)
#define SE_UNLOCK() do { if (sLocksReady) LeaveCriticalSection(&sLock); } while (0)
#else
static pthread_t sThread;
static pthread_t sTcpThread;
static int sTcpThreadStarted = 0;
static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static int sListenFd = -1;
static int sTcpListenFd = -1;
/* The accepted client of each listener (-1 = none), so shutdown can interrupt a server thread
 * that is parked in recv()/send() on a client that has gone quiet. Closing the listening socket
 * only wakes accept(); a thread already serving a client never sees it, and the join in
 * SeExportDeinit would wait on that client forever. Guarded by sConnLock; the owning server
 * thread clears its slot under the lock BEFORE closing the fd, so shutdown() can never be
 * called on a descriptor number that has already been reused. */
static int sActiveFd[2] = { -1, -1 };   /* [0] local socket, [1] TCP */
static pthread_mutex_t sConnLock = PTHREAD_MUTEX_INITIALIZER;
#define SE_LOCK()   pthread_mutex_lock(&sLock)
#define SE_UNLOCK() pthread_mutex_unlock(&sLock)
#endif

/* ===================== Savestate rewind (v16) ===========================
 * A per-frame full-savestate ring, delta-compressed on a worker thread, that powers
 * the rewind timeline's "Play from here". The emulate thread does only the (heavy)
 * SAVE at a frame boundary and stages the raw buffer on a bounded queue (drop-if-full);
 * a worker thread XOR-diffs it against the current keyframe and RLE-compresses it into
 * a compact block; the server thread ships those blocks lagging on GET responses. The
 * client keeps them and, on resume-from-scrub, hands a reconstructed full state (plus the
 * pending memory edits) back via the LST verb; the emulate thread restores + patches +
 * resumes at the gate. All savestate work is off the emulate thread except SAVE and LOAD. */
static unsigned int SeRd32(const unsigned char* p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* Whether to capture at all (the REW verb, v18). Starts ON so a pre-v18 client that never
 * sends REW keeps the old behavior; a v18 client states the user's setting on connect. Reset to
 * ON when a client leaves, so the next one starts from the same baseline rather than inheriting
 * a setting it never chose. Written by the server thread, read by the emulate thread. */
static volatile int sRewindWanted = 1;

static size_t (*sSaveState)(unsigned char* buf, size_t cap);
static int    (*sLoadState)(const unsigned char* buf, size_t len);

#define SE_STATE_QUEUE   8      /* raw full-savestate buffers staged for the worker */
#define SE_STATE_OUTQ    16     /* finished blocks awaiting the wire */
#define SE_STATE_KF_MAX  300u   /* force a keyframe at least this often (frames) */
#define SE_STATE_KF_NUM  1u     /* promote a delta to a keyframe when RLE(delta) size > */
#define SE_STATE_KF_DEN  3u     /*   (num/den) of the full state (i.e. a scene change) */

#if defined(_WIN32)
static CRITICAL_SECTION sStateLock;
/* Same pre-init gate as SE_LOCK above (sLocksReady covers both critical sections). */
#define SE_SLOCK()   do { if (sLocksReady) EnterCriticalSection(&sStateLock); } while (0)
#define SE_SUNLOCK() do { if (sLocksReady) LeaveCriticalSection(&sStateLock); } while (0)
static HANDLE sStateWorker;
#else
static pthread_mutex_t sStateLock = PTHREAD_MUTEX_INITIALIZER;
#define SE_SLOCK()   pthread_mutex_lock(&sStateLock)
#define SE_SUNLOCK() pthread_mutex_unlock(&sStateLock)
static pthread_t sStateWorker;
#endif
static volatile int sStateWorkerRun;
static int          sStateWorkerStarted;
/* Set when a deinit could not join a thread and therefore left its locks and buffers allocated
 * (see SeExportDeinit). Sticky: a second init would start a new server over the same globals and
 * hand the surviving thread a ring that two threads now write, so it refuses instead. */
static int          sShutdownIncomplete;

static size_t         sStateCap;                  /* per-buffer capacity (0 = feature off) */
static unsigned char* sStatePool[SE_STATE_QUEUE]; /* pooled full-state buffers */
static int            sFreeStack[SE_STATE_QUEUE]; /* free pool-buffer indices */
static int            sFreeCount;

/* Generation: bumped on every load so pre-load frames still in flight are dropped and the
 * first frame of each generation is a keyframe (the client truncated everything after N). */
static volatile unsigned sStateGen;
static unsigned          sKeyGen;                 /* generation the current keyframe belongs to */

typedef struct { unsigned long long frame; int poolIdx; size_t len; unsigned gen; unsigned epoch; } SeRawItem;
static SeRawItem sRawFifo[SE_STATE_QUEUE];
static int sRawHead, sRawCount;

typedef struct {
    unsigned char      kind;                      /* SE_LIVE_STATE_KIND_* */
    unsigned           epoch;                     /* loads resolved when captured (see sRestoreResolved) */
    unsigned long long frame, base;
    unsigned char*     payload; size_t len;       /* RLE payload */
    size_t             full;                      /* decoded full-state size */
} SeStateBlock;
static SeStateBlock sOutFifo[SE_STATE_OUTQ];
static int sOutHead, sOutCount;

static unsigned char*     sKeyFull;    /* worker-owned current keyframe bytes */
static size_t             sKeyLen;
static unsigned long long sKeyFrame;
static unsigned int       sSinceKeyframe;
static unsigned char*     sXorScratch; /* worker XOR scratch (state-sized) */

/* Load mailbox. The server thread (LST) receives the whole payload (frame + edits_len + edits +
 * state) into a buffer of its own and then PUBLISHES it here; the emulate thread takes it at the
 * gate. The buffer, its length and the pending flag are one piece of state and change together,
 * under the state lock -- the server never receives into the mailbox itself, because the emulate
 * thread may be reading it. (It used to: a second LST arriving before the gate had consumed the
 * first reallocated the buffer under the reader and overwrote it mid-copy.) sLoadPending is only
 * ever stored under the lock; the gate reads it unlocked as a cheap "anything to do?" hint and
 * re-checks under the lock before taking the payload. Ownership of the buffer moves to whoever
 * takes it. */
static unsigned char*     sLoadBuf; static size_t sLoadLen;
static volatile int       sLoadPending;

/* Worker thread body (defined after SeGateSleep). */
#if defined(_WIN32)
static DWORD WINAPI SeStateWorkerThread(LPVOID arg);
#else
static void* SeStateWorkerThread(void* arg);
#endif

/* Drop all queued/finished state and open a new generation. Two callers, on two threads, both
 * for the same reason -- the staged pipeline no longer describes anything the client wants:
 * the emulate thread right after a successful load, and the server thread when the client
 * switches rewind off (REW). Everything it touches is under the state lock, so either is safe. */
static void SeStateFlushAndRekey(void)
{
    int i;
    SE_SLOCK();
    for (i = 0; i < sRawCount; ++i)
        sFreeStack[sFreeCount++] = sRawFifo[(sRawHead + i) % SE_STATE_QUEUE].poolIdx;
    sRawHead = sRawCount = 0;
    for (i = 0; i < sOutCount; ++i)
        free(sOutFifo[(sOutHead + i) % SE_STATE_OUTQ].payload);
    sOutHead = sOutCount = 0;
    ++sStateGen;   /* first frame of the new generation becomes a keyframe */
    SE_SUNLOCK();
}

/* Emulate thread, from SeExportSnapshot: save the current full state and stage it for the
 * worker (SAVE only; no diff/RLE). Drops the frame (leaving it non-seekable) if the worker
 * is behind. No-op until a save hook + buffer pool exist. */
static void SeStateCapture(unsigned long long frame, unsigned epoch)
{
    int idx; size_t n;
    if (!sSaveState || sStateCap == 0) return;
    SE_SLOCK();
    if (sFreeCount == 0) { SE_SUNLOCK(); return; }   /* worker behind: drop */
    idx = sFreeStack[--sFreeCount];
    SE_SUNLOCK();
    n = sSaveState(sStatePool[idx], sStateCap);       /* expensive; off the lock */
    if (n == 0 || n > sStateCap)
    {
        SE_SLOCK(); sFreeStack[sFreeCount++] = idx; SE_SUNLOCK();
        return;
    }
    SE_SLOCK();
    if (sRawCount < SE_STATE_QUEUE)
    {
        int slot = (sRawHead + sRawCount) % SE_STATE_QUEUE;
        sRawFifo[slot].frame = frame; sRawFifo[slot].poolIdx = idx;
        sRawFifo[slot].len = n; sRawFifo[slot].gen = sStateGen; sRawFifo[slot].epoch = epoch;
        ++sRawCount;
    }
    else { sFreeStack[sFreeCount++] = idx; }
    SE_SUNLOCK();
}

/* Apply the LST memory-edit blob (SE_LIVE_EDIT_* layout) after a restore, so the game
 * re-simulates from frame N with the user's modifications. Byte-by-byte, like WRM/WRS. */
static void SeStateApplyEdits(const unsigned char* edits, size_t len)
{
    size_t pos; unsigned int count, e;
    if (len < 4) return;
    count = SeRd32(edits); pos = 4;
    for (e = 0; e < count; ++e)
    {
        unsigned int type, addr, elen, i;
        if (pos + SE_LIVE_EDIT_HDR_LEN > len) return;
        type = edits[pos];
        addr = SeRd32(edits + pos + 4);
        elen = SeRd32(edits + pos + 8);
        pos += SE_LIVE_EDIT_HDR_LEN;
        if (pos + elen > len) return;
        for (i = 0; i < elen; ++i)
        {
            unsigned char v = edits[pos + i];
            if (type == SE_LIVE_EDIT_TYPE_SOUND) { if (sWriteSoundByte) sWriteSoundByte(addr + i, v); }
            else                                 { if (sWriteByte)      sWriteByte(addr + i, v); }
        }
        pos += elen;
    }
}

/* Consume a pending LST at the gate (emulate thread). Copies the payload under the state
 * lock, then atomically: restore the savestate, apply the edits on top, adopt frame N,
 * invalidate the stale (> N) wire ring, drop the stale savestate pipeline, and resume. */
/* Everything that must be invalidated once the emulator's state has changed underneath us,
 * whether from a rewind (LST) or from the emulator loading one of its own slots (ELS). The
 * pre-restore wire ring would otherwise let a GET serve a frame from the abandoned timeline,
 * and the savestate pipeline would keep diffing against a keyframe that no longer describes
 * anything. Kept in one place so a future addition can't land on only one of the two paths. */
static void SeRestoreFailed(void)
{
    SE_LOCK();
    ++sRestoreFailed;
    ++sRestoreResolved;
    SE_UNLOCK();
}

static void SeStateAfterRestore(void)
{
    SE_LOCK();
    { int i; for (i = 0; i < SE_RING; ++i) sRingFrame[i] = 0; sRingWrite = 0; }
    ++sRestoreAckPending;   /* each restore is counted done when the next frame lands in the emptied ring */
    ++sRestoreResolved;
    SE_UNLOCK();
    /* The restored machine has its own, different SH-2 stacks; every frame we recorded
     * belongs to the timeline we just abandoned, and the returns that would have unwound
     * them will never execute. Drop them rather than let them sit under whatever the
     * restored code pushes next. */
    SeExportResetCallStack(0);
    SeExportResetCallStack(1);
    SeStateFlushAndRekey();
    SeStopClear();
    SeCancelSteps();
    SeAtStore(&sPaused, 0);   /* resume: re-simulate forward from wherever we now are */
}

/* Apply the payload taken from the mailbox. 'buf' is heap memory owned by the caller. */
static void SeStateApplyLoad(const unsigned char* buf, size_t len)
{
    unsigned long long frame;
    unsigned int edits_len;
    const unsigned char* state; size_t state_len;
    if (len < 8 || !sLoadState) { SeRestoreFailed(); return; }
    frame = SeRd32(buf);
    edits_len = SeRd32(buf + 4);
    if ((size_t)8 + edits_len > len) { SeRestoreFailed(); return; }   /* malformed */
    state = buf + 8 + edits_len;
    state_len = len - 8 - (size_t)edits_len;
    if (sLoadState(state, state_len) == 0)
    {
        SeStateApplyEdits(buf + 8, edits_len);   /* patch RAM on top of the restore */
        sFrameNo = frame;
        SeStateAfterRestore();
    }
    else { SeExportLog("rewind: load state failed"); SeRestoreFailed(); }
}

static void SeStateConsumeLoad(void)
{
    unsigned char* buf = NULL; size_t len = 0;
    if (!SeAtLoad(&sLoadPending)) return;
    SE_SLOCK();
    if (SeAtLoad(&sLoadPending))
    {
        buf = sLoadBuf; len = sLoadLen;      /* take ownership: the mailbox is empty again */
        sLoadBuf = NULL; sLoadLen = 0;
        SeAtStore(&sLoadPending, 0);
    }
    SE_SUNLOCK();
    if (!buf) return;
    SeStateApplyLoad(buf, len);
    free(buf);
}

/* Allocate the buffer pool and start the worker, once, when a non-NULL save hook is set. */
static void SeStateStartWorker(void)
{
    size_t probe, cap; int i;
    if (sStateWorkerStarted || !sSaveState || !SeAtLoad(&sRunning)) return;
    probe = sSaveState(NULL, 0);                 /* required savestate size (probe) */
    if (probe == 0) return;                      /* can't size: feature stays off */
    cap = probe + probe / 4u + 4096u;            /* margin for per-frame size variance */
    for (i = 0; i < SE_STATE_QUEUE; ++i)
    {
        sStatePool[i] = (unsigned char*)malloc(cap);
        if (!sStatePool[i]) return;              /* leave disabled; freed at deinit */
        sFreeStack[i] = i;
    }
    sKeyFull    = (unsigned char*)malloc(cap);
    sXorScratch = (unsigned char*)malloc(cap);
    if (!sKeyFull || !sXorScratch) return;
    sFreeCount = SE_STATE_QUEUE;
    sRawHead = sRawCount = sOutHead = sOutCount = 0;
    sKeyLen = 0; sSinceKeyframe = 0; sStateGen = 1; sKeyGen = 0;
    SE_SLOCK(); sStateCap = cap; SE_SUNLOCK();   /* the server thread reads it under this lock (LST) */
    SeAtStore(&sStateWorkerRun, 1);
#if defined(_WIN32)
    sStateWorker = CreateThread(NULL, 0, SeStateWorkerThread, NULL, 0, NULL);
    sStateWorkerStarted = (sStateWorker != NULL);
#else
    sStateWorkerStarted = (pthread_create(&sStateWorker, NULL, SeStateWorkerThread, NULL) == 0);
#endif
    if (!sStateWorkerStarted) { SeAtStore(&sStateWorkerRun, 0); SE_SLOCK(); sStateCap = 0; SE_SUNLOCK(); }
}

void SeExportSetSaveStateHook(size_t (*save)(unsigned char* buf, size_t cap))
{
    sSaveState = save;
    if (save) SeStateStartWorker();
}
void SeExportSetLoadStateHook(int (*load)(const unsigned char* buf, size_t len))
{
    sLoadState = load;
}

/* Free the savestate buffers + queued blocks. Call only after the worker is stopped and
 * joined and no server thread is mid-serialize. */
static void SeStateShutdown(void)
{
    int i;
    for (i = 0; i < SE_STATE_QUEUE; ++i) { free(sStatePool[i]); sStatePool[i] = NULL; }
    for (i = 0; i < sOutCount; ++i) free(sOutFifo[(sOutHead + i) % SE_STATE_OUTQ].payload);
    sOutHead = sOutCount = 0; sRawHead = sRawCount = 0; sFreeCount = 0;
    free(sKeyFull);     sKeyFull = NULL;
    free(sXorScratch);  sXorScratch = NULL;
    free(sLoadBuf);     sLoadBuf = NULL; sLoadLen = 0;
    sStateCap = 0; sStateWorkerStarted = 0; SeAtStore(&sLoadPending, 0);
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

/* ---- Emulator-native save slots (v17+). The client cannot locate these itself: the path
 * depends on the emulator's base directory, its state-path setting and a hash of the disc.
 * So the emulator reports the inventory and performs the load. ---- */
typedef int (*SeEmuSlotInfoFn)(unsigned int slot, unsigned long long* mtime);
typedef int (*SeEmuSlotLoadFn)(unsigned int slot);
static SeEmuSlotInfoFn sEmuSlotInfo;
static SeEmuSlotLoadFn sEmuSlotLoad;
/* Latched by the ELS verb, applied by the gate on the emulate thread (0 = none pending,
 * else slot + 1) so the emulator's own loader never runs underneath a mid-frame CPU. */
static volatile int sEmuLoadPending;

void SeExportSetEmuSlotHooks(SeEmuSlotInfoFn info, SeEmuSlotLoadFn load)
{
    sEmuSlotInfo = info;
    sEmuSlotLoad = load;
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

/* How many clients (Saturn Explorer) are attached. Bumped by the server threads around the
 * serve loop, read every frame by the emulate thread. With nobody listening the per-frame
 * capture is pure waste -- several MB of copying into the ring, plus a full savestate
 * serialization for the rewind pipeline -- so the producer skips all of it while this is 0,
 * and a patched emulator run on its own costs what an unpatched one costs. A count, not a
 * flag: on POSIX the local socket and the web-bridge TCP port are served by two threads. */
static volatile int sClients;

int SeExportHasClient(void)
{
    return SeAtLoad(&sClients) != 0;
}

static void SePublishBreakpoints(const unsigned char* descs, unsigned int count);   /* below */

static void SeOnClientDisconnect(void)
{
    /* A client (Saturn Explorer) can disappear at any time — mid-button-hold, or while the
     * emulator is paused or halted at a breakpoint. Restore a clean, free-running state so the
     * game never stays frozen after SE closes:
     *   - release any held pad (don't leave a button latched in the emulator);
     *   - drop ALL breakpoints — a leftover execution or data watchpoint would instantly re-halt
     *     the game on resume, which looks exactly like a freeze (and SE re-syncs its set when it
     *     reconnects, so nothing is lost);
     *   - clear the pause/step/stop state so the frame gate stops holding the emulate thread. */
    if (sSetPad)
    {
        sSetPad(0, 0);
        sSetPad(1, 0);
    }
    SePublishBreakpoints(NULL, 0);   /* the emulate thread drops them at its next gate or frame */
    SeAtStore(&sRewindWanted, 1);   /* the next client states its own setting; don't inherit this one's */
    SE_LOCK();
    SeCancelSteps();
    SeAtStore(&sInsnStepPending, 0);
    SeStopClear();
    SeAtStore(&sPaused, 0);   /* last: nothing the CPU publishes after the release is then erased */
    SE_UNLOCK();
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

/* ---- Debug-hook installs (breakpoints, watchpoints, tracepoints), applied on the EMULATE thread.
 * The BKP and TRC verbs arrive on a server thread, but what they install is state the emulate thread
 * reads on every instruction: the debugger's breakpoint lists, the per-instruction callback and its
 * arming, the glue's tracepoint table. Rewriting any of it from the server thread while the CPU is
 * scanning it hands the CPU half of one set and half of the next (a descriptor's id paired with
 * another's CPU and address), or a callback armed against a table that is being replaced. So the
 * server thread only PUBLISHES a complete set into a mailbox, under the state lock; the emulate
 * thread takes it at the next frame gate or frame snapshot -- and the halt gate, which spins on that
 * thread while a breakpoint holds it -- and runs the install hooks itself. A set that has not been
 * taken when the next arrives is replaced: only the latest matters. ---- */
static unsigned char sBpBox[SE_LIVE_MAX_BKPT_DESCS * SE_LIVE_BKPT_DESC_LEN];
static unsigned int  sBpBoxCount;
static volatile int  sBpPending;
static unsigned char sTpBox[SE_LIVE_MAX_TRACE_DESCS * SE_LIVE_TRACE_DESC_LEN];
static unsigned int  sTpBoxCount;
static volatile int  sTpPending;
/* Emulate-thread scratch the taken set is copied into, so no lock is held while the hooks run. */
static unsigned char sBpApply[SE_LIVE_MAX_BKPT_DESCS * SE_LIVE_BKPT_DESC_LEN];
static unsigned char sTpApply[SE_LIVE_MAX_TRACE_DESCS * SE_LIVE_TRACE_DESC_LEN];

/* Server thread: publish the set of 'count' breakpoint descriptors (count <= the protocol maximum). */
static void SePublishBreakpoints(const unsigned char* descs, unsigned int count)
{
    SE_SLOCK();
    if (count) memcpy(sBpBox, descs, (size_t)count * SE_LIVE_BKPT_DESC_LEN);
    sBpBoxCount = count;
    SeAtStore(&sBpPending, 1);
    SE_SUNLOCK();
}

/* Server thread: publish the set of 'count' tracepoint descriptors. */
static void SePublishTracepoints(const unsigned char* descs, unsigned int count)
{
    SE_SLOCK();
    if (count) memcpy(sTpBox, descs, (size_t)count * SE_LIVE_TRACE_DESC_LEN);
    sTpBoxCount = count;
    SeAtStore(&sTpPending, 1);
    SE_SUNLOCK();
}

void SeExportApplyInstalls(void);   /* public name, defined below */

/* EMULATE thread: take whatever has been published and install it. Cheap when nothing has (one
 * atomic load per call). */
static void SeApplyPendingInstalls(void)
{
    if (SeAtLoad(&sBpPending))
    {
        unsigned int n, i;
        SE_SLOCK();
        n = sBpBoxCount;
        if (n) memcpy(sBpApply, sBpBox, (size_t)n * SE_LIVE_BKPT_DESC_LEN);
        SeAtStore(&sBpPending, 0);   /* inside the lock: a set published after this copy sets it again */
        SE_SUNLOCK();
        if (sClearBps) sClearBps();
        for (i = 0; i < n; ++i)
        {
            const unsigned char* d = sBpApply + (size_t)i * SE_LIVE_BKPT_DESC_LEN;
            const unsigned int address = SeRd32(d);
            const unsigned int size    = SeRd32(d + 4);
            const unsigned int flags   = SeRd32(d + 8);
            const unsigned int kind    = flags & SE_LIVE_BP_KIND_MASK;
            const unsigned int cpu     = (flags & SE_LIVE_BP_CPU_SLAVE) ? 1u : 0u;
            /* kind 0 = execution (PC); 1/2/3 = read/write/read-write data breakpoints
             * (watchpoints) over [address, address+size). A disabled one installs nothing. */
            if (!(flags & SE_LIVE_BP_ENABLED)) continue;
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
    if (SeAtLoad(&sTpPending))
    {
        unsigned int n;
        SE_SLOCK();
        n = sTpBoxCount;
        if (n) memcpy(sTpApply, sTpBox, (size_t)n * SE_LIVE_TRACE_DESC_LEN);
        SeAtStore(&sTpPending, 0);
        SE_SUNLOCK();
        if (sSetTracepoints) sSetTracepoints(n, sTpApply);
    }
}

/* Common to every halt: the step that was in progress is over (a breakpoint on the OTHER CPU can
 * interrupt a step of this one, and its leftover budget must not survive into the next resume),
 * and the retire tracking restarts from the halt PC. 'pendingFirst' says the instruction at 'pc'
 * has not executed (a halt between instructions, not in the hook that precedes one). */
static void SeHaltCommon(int cpu, unsigned int pc, int pendingFirst)
{
    const unsigned int c = (cpu != 0) ? 1u : 0u;
    sInsnStepBudget = 0;
    sStepLastPc[c] = pc;
    sStepPendingFirst[c] = pendingFirst;
    SeAtStore(&sPaused, 1);
    SeCancelSteps();
}

/* Called from the emulator's per-instruction callback when the master/slave SH-2 hits an
 * execution breakpoint: latch the stop and hold the emulator paused. Plain atomic stores (like
 * sPaused elsewhere) -- this runs in the CPU thread and must not take the frame lock. The stop is
 * published BEFORE sPaused is raised, so a client that sees the pause also sees the reason. */
void SeExportNotifyStop(int cpu, unsigned int pc)
{
    SeStopSet(SE_LIVE_STOP_EXEC_BP, cpu, pc);
    SeHaltCommon(cpu, pc, 0);
}

/* Like SeExportNotifyStop, but latches SE_LIVE_STOP_STEP -- the halt that ends an
 * instruction step (IST) rather than a user breakpoint. Same CPU-thread contract. */
void SeExportNotifyStep(int cpu, unsigned int pc)
{
    SeStopSet(SE_LIVE_STOP_STEP, cpu, pc);
    SeHaltCommon(cpu, pc, 0);
}

/* A halt that did not come from the per-instruction hook: an SCU-DMA watchpoint. 'pc' is the
 * instruction the CPU is about to execute, and has not executed. */
void SeExportNotifyDmaStop(int cpu, unsigned int pc)
{
    SeStopSet(SE_LIVE_STOP_EXEC_BP, cpu, pc);
    SeHaltCommon(cpu, pc, 1);
}

/* CPU thread, called from the per-instruction hook right after the halt gate releases:
 * if an instruction step was requested (IST verb), activate its budget and return 1 so
 * the caller arms continuous per-instruction hooking. Returns 0 when no step is pending. */
int SeExportInsnStepBegin(void)
{
    /* Taken with an exchange so a request that lands as the CPU thread consumes the previous one
     * is either seen now or left for the next call, never zeroed unseen. */
    const int n = SeAtXchg(&sInsnStepPending, 0);
    if (n > 0)
    {
        sInsnStepBudget = n;
        return 1;
    }
    return 0;
}

/* CPU thread, called from the per-instruction hook BEFORE each attempted step, with the CPU's
 * current PC. Counts only the CPU the step targets, and only when an instruction actually
 * RETIRED (see the block above for what that means and the two cases where the PC alone cannot
 * say). 'selfBranchTaken' is nonzero when the instruction at 'pc' is a branch that will be taken
 * back to 'pc'. Returns 1 when the budget is exhausted (halt here), else 0. */
int SeExportInsnStepTick(int cpu, unsigned int pc, int selfBranchTaken)
{
    unsigned int c = (cpu != 0) ? 1u : 0u;
    if (sInsnStepBudget <= 0)
    {
        return 0;
    }
    if (c != (unsigned int)SeAtLoad(&sInsnStepCpu))
    {
        return 0;
    }
    if (pc == sStepLastPc[c])
    {
        if (sStepPendingFirst[c])
        {
            sStepPendingFirst[c] = 0;   /* the pending instruction presenting itself: not a retirement */
            return 0;
        }
        if (!selfBranchTaken)
        {
            return 0;   /* same PC as last count -> not retired yet (bus stall); don't spend budget */
        }
    }
    sStepPendingFirst[c] = 0;
    sStepLastPc[c] = pc;
    if (--sInsnStepBudget == 0)
    {
        return 1;
    }
    return 0;
}

void SeExportApplyInstalls(void)
{
    SeApplyPendingInstalls();
}

#if defined(SE_EXPORT_SPIN_GATE)
/* Test builds only: called inside the frame gate between its install check and its pause check, the
 * window a resume from the server thread can land in. A test uses it to place one there. */
void (*SeExportTestGateHook)(void);
#endif

/* Short self-contained sleep so the gate can spin-wait without a Yabause-
 * specific sleep primitive and without pegging a CPU core while paused. */
static void SeGateSleep(void)
{
#if defined(SE_EXPORT_SPIN_GATE)
    /* Test builds only: poll the pause flag flat out. The 2 ms sleep below makes the CPU notice a
     * release up to 2 ms late, which hides every race that lives in the few hundred nanoseconds
     * after the server thread releases it; a stress test needs the CPU to be there at once. */
    return;
#elif defined(_WIN32)
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
static int SeGateDecide(void)
{
    /* Install what the server thread has published (breakpoints, tracepoints) -- here, on the
     * emulate thread, because it is the only one that reads them. This is also reached from the halt
     * gate, so an edit made while a breakpoint holds the CPU still lands. */
    SeApplyPendingInstalls();
    /* Apply a pending rewind (LST) here, on the emulate thread at a frame boundary,
     * before honoring the pause: a load always leaves the emulator paused on frame N. */
    if (SeAtLoad(&sLoadPending))
    {
        SeStateConsumeLoad();
    }
    /* Same for an emulator-native slot load (ELS). The emulator loads it through its own
     * code, so nothing here knows the resulting frame; drop the savestate pipeline and the
     * wire ring for the same reason a rewind does, and let the client's history go with it. */
    {
        const int pendingSlot = SeAtXchg(&sEmuLoadPending, 0);   /* take it, so a new one is never lost */
        const int slot = pendingSlot - 1;
        if (pendingSlot != 0)
        {
            if (sEmuSlotLoad && sEmuSlotLoad((unsigned int)slot) == 0)
            {
                SeStateAfterRestore();
            }
            else
            {
                SeExportLog("load slot: the emulator refused it");
                SeRestoreFailed();
            }
        }
    }
#if defined(SE_EXPORT_SPIN_GATE)
    if (SeExportTestGateHook) SeExportTestGateHook();   /* the window a resume can land in; see below */
#endif
    if (!SeAtLoad(&sPaused))
    {
        return 1;
    }
    /* Held, but nobody is attached to release it. A client that left takes its breakpoints with it
     * (they are dropped on the emulate thread, at the next gate or frame, so a hit can land first),
     * and a halt at one of them would otherwise freeze the game for good. */
    if (!SeExportHasClient())
    {
        SeCancelSteps();
        SeAtStore(&sInsnStepPending, 0);
        SeStopClear();
        SeAtStore(&sPaused, 0);
        return 1;
    }
    if (SeTakeStepFrame())
    {
        return 1;
    }
    SeGateSleep();
    return 0;
}

int SeExportGateFrame(void)
{
    const int run = SeGateDecide();
    /* A release is ordered after the installs that preceded it: the server thread publishes a
     * breakpoint set BEFORE it clears the pause (RUN, STP, IST), so a CPU that has just seen the
     * pause lifted -- by the loads above -- must look at the mailbox AGAIN. The install check at the
     * top of the gate ran before the pause check, and a resume landing between the two released the
     * CPU with its temporary breakpoint (Step Over, Run to Cursor) still unapplied, which a short
     * subroutine can run straight past. */
    if (run) SeApplyPendingInstalls();
    return run;
}

/* Savestate worker thread: pop raw full states, diff against the current keyframe (or emit
 * a keyframe), RLE-compress, and queue the compact block for the wire. Off the emulate
 * thread, so the per-frame diff never causes a frame-rate hitch. */
static void SeStateWorkerBody(void)
{
    while (SeAtLoad(&sStateWorkerRun))
    {
        SeRawItem item; int have = 0; unsigned curGen = 0;
        unsigned char* full; size_t fullLen;
        int keyframe; unsigned char* rleSrc; size_t rleSrcLen, cap, plen;
        unsigned char* payload;

        SE_SLOCK();
        curGen = sStateGen;
        if (sRawCount > 0)
        {
            item = sRawFifo[sRawHead];
            sRawHead = (sRawHead + 1) % SE_STATE_QUEUE;
            --sRawCount;
            have = 1;
        }
        SE_SUNLOCK();
        if (!have) { SeGateSleep(); continue; }

        if (item.gen != curGen)   /* pre-load frame the client already discarded: drop */
        {
            SE_SLOCK(); sFreeStack[sFreeCount++] = item.poolIdx; SE_SUNLOCK();
            continue;
        }

        full = sStatePool[item.poolIdx]; fullLen = item.len;
        keyframe = (item.gen != sKeyGen) || (sKeyLen != fullLen) ||
                   (sSinceKeyframe >= SE_STATE_KF_MAX);
        if (keyframe) { rleSrc = full; rleSrcLen = fullLen; }
        else { se_state_xor(sXorScratch, full, sKeyFull, fullLen); rleSrc = sXorScratch; rleSrcLen = fullLen; }

        cap = rleSrcLen + rleSrcLen / 32u + 64u;
        payload = (unsigned char*)malloc(cap);
        plen = payload ? se_state_rle_encode(payload, cap, rleSrc, rleSrcLen) : 0;
        /* A delta bigger than (num/den) of the full state means a scene change — keyframe it. */
        if (!keyframe && (plen == 0 || plen > (fullLen * SE_STATE_KF_NUM) / SE_STATE_KF_DEN))
        {
            keyframe = 1;
            plen = payload ? se_state_rle_encode(payload, cap, full, fullLen) : 0;
        }
        if (keyframe && payload && plen > 0)
        {
            memcpy(sKeyFull, full, fullLen);
            sKeyLen = fullLen; sKeyFrame = item.frame; sKeyGen = item.gen; sSinceKeyframe = 0;
        }
        else { ++sSinceKeyframe; }

        SE_SLOCK(); sFreeStack[sFreeCount++] = item.poolIdx; SE_SUNLOCK();
        if (!payload || plen == 0) { free(payload); continue; }   /* couldn't compress: drop */

        SE_SLOCK();
        if (sOutCount == SE_STATE_OUTQ)   /* outgoing full: drop the oldest (independent blocks) */
        {
            free(sOutFifo[sOutHead].payload);
            sOutHead = (sOutHead + 1) % SE_STATE_OUTQ; --sOutCount;
        }
        {
            int slot = (sOutHead + sOutCount) % SE_STATE_OUTQ;
            sOutFifo[slot].kind  = keyframe ? (unsigned char)SE_LIVE_STATE_KIND_KEYFRAME
                                            : (unsigned char)SE_LIVE_STATE_KIND_DELTA;
            sOutFifo[slot].epoch = item.epoch;
            sOutFifo[slot].frame = item.frame;
            sOutFifo[slot].base  = keyframe ? item.frame : sKeyFrame;
            sOutFifo[slot].payload = payload; sOutFifo[slot].len = plen;
            sOutFifo[slot].full  = fullLen;
            ++sOutCount;
        }
        SE_SUNLOCK();
    }
}
#if defined(_WIN32)
static DWORD WINAPI SeStateWorkerThread(LPVOID arg) { (void)arg; SeStateWorkerBody(); return 0; }
#else
static void* SeStateWorkerThread(void* arg) { (void)arg; SeStateWorkerBody(); return NULL; }
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
 * executes; se_export.h documents the call/exception pairing they enforce. The frame
 * number is stamped here from the module counter. */
static void SePushCallFrame(int cpu, unsigned int kind, unsigned int callSite,
                            unsigned int func, unsigned int ret, unsigned int sp,
                            unsigned long long cycle)
{
    int c = cpu ? 1 : 0;
    SE_LOCK();
    if (sCallDepth[c] < SE_CALLSTACK_CAP)
    {
        SeCallFrame* f = &sCallStack[c][sCallDepth[c]];
        f->callSite = callSite; f->func = func; f->ret = ret; f->sp = sp;
        f->cycle = cycle;
        f->frameNo = (unsigned int)(sFrameNo & 0xFFFFFFFFu);
        f->kind = kind;
        ++sCallDepth[c];
    }
    else ++sCallOverflow[c];
    SE_UNLOCK();
}

void SeExportPushFrame(int cpu, unsigned int callSite, unsigned int func,
                       unsigned int ret, unsigned int sp, unsigned long long cycle)
{
    SePushCallFrame(cpu, SE_FRAME_CALL, callSite, func, ret, sp, cycle);
}

void SeExportPushExceptionFrame(int cpu, unsigned int site, unsigned int handler,
                                unsigned int ret, unsigned int sp, unsigned long long cycle)
{
    SePushCallFrame(cpu, SE_FRAME_EXCEPTION, site, handler, ret, sp, cycle);
}

/* rts. Unwinds the overflow first (those frames are the innermost), then a stored frame —
 * but only a call frame: an rts cannot return across an exception boundary, so meeting an
 * exception frame means this rts belongs to a call made before recording started. */
void SeExportPopFrame(int cpu)
{
    int c = cpu ? 1 : 0;
    SE_LOCK();
    if (sCallOverflow[c]) --sCallOverflow[c];
    else if (sCallDepth[c] && sCallStack[c][sCallDepth[c] - 1].kind == SE_FRAME_CALL)
        --sCallDepth[c];
    SE_UNLOCK();
}

/* rte, with R15 as the instruction sees it. Unwinds the exception frame this rte returns
 * from — identified by that R15, which SH-2 rte pops PC and SR from and which therefore
 * equals the sp recorded at the entry. No match means the exception was one we never
 * observed (asynchronous interrupts execute no instruction we can hook), and the call
 * stack must be left exactly as it is. Unlike rts this never touches the overflow count:
 * a frame we could not store kept no sp to match against. */
void SeExportPopExceptionFrame(int cpu, unsigned int sp)
{
    int c = cpu ? 1 : 0;
    SE_LOCK();
    if (sCallDepth[c])
    {
        const SeCallFrame* top = &sCallStack[c][sCallDepth[c] - 1];
        if (top->kind == SE_FRAME_EXCEPTION && top->sp == sp) --sCallDepth[c];
    }
    SE_UNLOCK();
}

static volatile unsigned int sCallStackEpoch;

void SeExportResetCallStack(int cpu)
{
    int c = cpu ? 1 : 0;
    SE_LOCK();
    sCallDepth[c] = 0;
    sCallOverflow[c] = 0;
    SeAtAdd((volatile int*)&sCallStackEpoch, 1);
    SE_UNLOCK();
}

unsigned int SeExportCallStackEpoch(void)
{
    return (unsigned int)SeAtLoad((volatile int*)&sCallStackEpoch);
}

unsigned int SeExportSerializeCallStack(int cpu, unsigned char* out)
{
    int c = cpu ? 1 : 0;
    unsigned int depth, n, i;
    SE_LOCK();
    depth = sCallDepth[c];
    n = (depth > SE_LIVE_CALLSTACK_MAX) ? SE_LIVE_CALLSTACK_MAX : depth;
    SeWr32(out, n);
    for (i = 0; i < n; ++i)
    {
        const SeCallFrame* f = &sCallStack[c][depth - 1 - i];   /* innermost first */
        unsigned char* fb = out + 4 + i * SE_LIVE_CALLFRAME_LEN;
        SeWr32(fb + 0,  f->callSite);
        SeWr32(fb + 4,  f->func);
        SeWr32(fb + 8,  f->ret);
        SeWr32(fb + 12, f->sp);
        SeWr32(fb + 16, (unsigned int)(f->cycle & 0xFFFFFFFFu));
        SeWr32(fb + 20, (unsigned int)((f->cycle >> 32) & 0xFFFFFFFFu));
        SeWr32(fb + 24, f->frameNo);
    }
    SE_UNLOCK();
    return 4u + n * SE_LIVE_CALLFRAME_LEN;
}

void SeExportSnapshot(const void* vdp1, const void* vdp2, const void* cram,
                      const void* vdp2struct, const void* vdp1struct,
                      const void* wramLow, const void* wramHigh,
                      const void* vdp1fb, const void* msh2, const void* ssh2,
                      const void* soundRam, const void* scspSlots, const void* cdStatus)
{
    SeApplyPendingInstalls();   /* the emulate thread's frame boundary; see the mailbox */

    /* Nobody attached: capture nothing. The ring copy and the staged savestate below would
     * both be thrown away unread, and the savestate is the single most expensive thing the
     * emulate thread does per frame. Capture resumes on the first frame after a client
     * attaches; the frame counter simply stands still across the gap. */
    if (!SeExportHasClient())
    {
        return;
    }
    if (!sRing[0])
    {
        return;
    }
    unsigned epochNow;
    SE_LOCK();
    SeFrame* dst = sRing[sRingWrite];   /* the ring slot this frame lands in */
    if (vdp1) memcpy(dst->v1, vdp1, SE_V1); else memset(dst->v1, 0, SE_V1);
    if (vdp2) memcpy(dst->v2, vdp2, SE_V2); else memset(dst->v2, 0, SE_V2);
    if (cram) memcpy(dst->cr, cram, SE_CR); else memset(dst->cr, 0, SE_CR);
    if (vdp2struct) memcpy(dst->vs, vdp2struct, SE_VS); else memset(dst->vs, 0, SE_VS);
    SeBuildVdp1Image(dst->vr, vdp1struct);
    if (wramLow)  memcpy(dst->wl, wramLow,  SE_WL); else memset(dst->wl, 0, SE_WL);
    if (wramHigh) memcpy(dst->wh, wramHigh, SE_WH); else memset(dst->wh, 0, SE_WH);
    if (vdp1fb)   memcpy(dst->fb, vdp1fb,   SE_FB); else memset(dst->fb, 0, SE_FB);
    /* SH-2 state: master then slave, each a 92-byte sh2regs_struct (host order). */
    if (msh2) memcpy(dst->sh, msh2, SE_LIVE_SH2_REGS_LEN);
    else      memset(dst->sh, 0, SE_LIVE_SH2_REGS_LEN);
    if (ssh2) memcpy(dst->sh + SE_LIVE_SH2_REGS_LEN, ssh2, SE_LIVE_SH2_REGS_LEN);
    else      memset(dst->sh + SE_LIVE_SH2_REGS_LEN, 0, SE_LIVE_SH2_REGS_LEN);
    /* SCSP sound RAM (v13): only served on the wire when the emulator supplied it. */
    if (soundRam) { memcpy(dst->sr, soundRam, SE_SR); dst->has_sr = 1; }
    else          { memset(dst->sr, 0, SE_SR);        dst->has_sr = 0; }
    /* Decoded SCSP slots (v14): the glue hands over the pre-serialized 1152-byte block. */
    if (scspSlots) { memcpy(dst->sl, scspSlots, SE_SL); dst->has_sl = 1; }
    else           { memset(dst->sl, 0, SE_SL);         dst->has_sl = 0; }
    /* CD-block status (v15): the glue hands over the pre-serialized 16-byte record. */
    if (cdStatus) { memcpy(dst->cd, cdStatus, SE_LIVE_CD_BLOCK_LEN); dst->has_cd = 1; }
    else          { memset(dst->cd, 0, SE_LIVE_CD_BLOCK_LEN);        dst->has_cd = 0; }
    dst->valid = 1;
    sRingFrame[sRingWrite] = ++sFrameNo;               /* tag this slot with its frame number */
    sRingWrite = (sRingWrite + 1) % SE_RING;           /* advance (wraps, overwriting oldest) */
    if (sRestoreAckPending) { sRestoreDone += sRestoreAckPending; sRestoreAckPending = 0; }   /* first post-restore frame */
    epochNow = sRestoreResolved;
    SeStepFramePublished();   /* in the same critical section as the ring write, so a reply that
                               * reports the step as retired also holds its frame */
    SE_UNLOCK();
    /* v16 rewind: stage a full savestate for this frame (off-lock; no-op unless a save hook
     * is wired). The worker delta-compresses it and the server ships it lagging. Skip it while
     * paused: a snapshot taken from inside a debugger halt (breakpoint/step) is mid-frame — the
     * emulator's event timing isn't at a frame boundary, so its savestate isn't a clean rewind
     * point. The rewind timeline simply omits halt frames; running frames still capture.
     * Also skipped entirely while the client has rewind switched off (REW, v18): the full
     * savestate is the most expensive thing on this thread and nothing would read it. */
    if (!SeAtLoad(&sPaused) && SeAtLoad(&sRewindWanted)) SeStateCapture(sFrameNo, epochNow);
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
/* A peer that closes mid-reply makes send() raise SIGPIPE, whose default action ends the whole
 * process -- the emulator, not just this connection. Linux suppresses it per call with
 * MSG_NOSIGNAL; the BSDs/macOS have no such flag and do it per socket (SO_NOSIGPIPE, set where a
 * connection is accepted -- see SeQuietSocket). Either way a dead peer becomes an ordinary
 * send() error, which every caller already treats as "drop this client". */
#if defined(MSG_NOSIGNAL)
#define SE_SEND_FLAGS MSG_NOSIGNAL
#else
#define SE_SEND_FLAGS 0
#endif
static void SeQuietSocket(int fd)
{
#if defined(SO_NOSIGPIPE)
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    (void)fd;
#endif
}
static int SeSend(int fd, const void* d, size_t n)
{
    const unsigned char* p = (const unsigned char*)d;
    while (n)
    {
        ssize_t w = send(fd, p, n, SE_SEND_FLAGS);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        p += w; n -= (size_t)w;
    }
    return 0;
}
static int SeRecv(int fd, void* d, size_t n)
{
    unsigned char* p = (unsigned char*)d;
    while (n)
    {
        ssize_t r = recv(fd, p, n, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        p += r; n -= (size_t)r;
    }
    return 0;
}
#endif

/* The connected-client handle, so the helpers below can be written once for both transports. */
#if defined(_WIN32)
typedef HANDLE SeConn;
#else
typedef int SeConn;
#endif

/* Consume and discard 'n' bytes, in bulk. Every capped verb needs this to stay stream-aligned
 * after it stops acting on a payload, and doing it a byte at a time is what made the cap
 * expensive: SeRecv wraps one recv() syscall, so a per-byte drain costs a syscall per byte --
 * roughly half a microsecond each, which is ~570 ms for a 1 MiB payload and far worse for a
 * request that claims more. Draining through a scratch buffer makes it a few dozen syscalls. */
/* Read and discard n bytes through a caller-supplied buffer. Split out so a function that
   already owns a scratch buffer can lend it rather than adding a second one: SeDrain's own
   16 KiB inlined into SeRecvPokeStream's 16 KiB put that frame over 32 KiB of stack, which
   the server thread runs on. */
static int SeDrainWith(SeConn cl, unsigned int n, unsigned char* buf, unsigned int bufLen)
{
    while (n)
    {
        const unsigned int take = n > bufLen ? bufLen : n;
        if (SeRecv(cl, buf, take) != 0) return -1;
        n -= take;
    }
    return 0;
}

static int SeDrain(SeConn cl, unsigned int n)
{
    unsigned char scratch[16u * 1024u];
    return SeDrainWith(cl, n, scratch, (unsigned int)sizeof(scratch));
}

/* Receive a poke stream -- destination(4 LE) + 'count' bytes -- and apply it through 'hook'.
 * Shared by WRM and WRS, which differ only in the hook and in whether the destination is a bus
 * address or a sound-RAM offset. Bytes past 'cap' are drained rather than written.
 *
 * The bytes are read in blocks rather than one at a time for the reason SeDrain gives: the old
 * per-byte loop spent a syscall per byte, so the protocol's own 1 MiB maximum cost the
 * emulator's server thread over half a second. */
static int SeRecvPokeStream(SeConn cl, void (*hook)(unsigned int, unsigned char),
                            unsigned int count, unsigned int cap)
{
    unsigned char destb[4];
    unsigned char block[16u * 1024u];
    unsigned int dest, done = 0;
    if (SeRecv(cl, destb, 4) != 0) return -1;
    dest = (unsigned int)destb[0] | ((unsigned int)destb[1] << 8) |
           ((unsigned int)destb[2] << 16) | ((unsigned int)destb[3] << 24);
    const unsigned int keep = count > cap ? cap : count;
    while (done < keep)
    {
        const unsigned int take = (keep - done) > sizeof(block)
                                      ? (unsigned int)sizeof(block) : (keep - done);
        unsigned int i;
        if (SeRecv(cl, block, take) != 0) return -1;
        if (hook) { for (i = 0; i < take; ++i) hook(dest + done + i, block[i]); }
        done += take;
    }
    /* Reuse 'block' rather than letting SeDrain's own buffer inline a second one in. */
    return SeDrainWith(cl, count - keep, block, (unsigned int)sizeof(block));
}

static void SeServeClientLoop(SeConn cl, SeFrame* snap);

/* Count this client in for as long as it is attached. A wrapper around the serve loop, rather
 * than a bump at each of the three accept sites: the loop returns from a dozen places on any I/O
 * error, and every one of them has to release the count. Keeping both halves here is what makes
 * them impossible to leave unpaired. */
static void SeServeClient(SeConn cl, SeFrame* snap)
{
    SeAtAdd(&sClients, 1);
    SeServeClientLoop(cl, snap);
    SeAtAdd(&sClients, -1);
}

/* Serve one connected client until it disconnects or the server stops. 'snap' is
 * scratch the size of one frame. */
static void SeServeClientLoop(SeConn cl, SeFrame* snap)
{
    SeLogPortDevices();   /* report the emulator's controller config on connect */
    while (SeAtLoad(&sRunning))
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
            SE_LOCK(); SeAtStore(&sPaused, 1); SeCancelSteps(); SE_UNLOCK();
        }
        else if (memcmp(req, SE_LIVE_VERB_RESUME, SE_LIVE_VERB_LEN) == 0)
        {
            /* The old stop is cleared BEFORE the CPU is released. The other order let a CPU that
             * re-halted straight away (a breakpoint on the next instruction, a loop that hits the
             * same one again) publish its new stop in the gap and have it erased by the clear that
             * followed: the emulator sat paused with no reason, and the client never saw the halt
             * it was waiting for. Cleared first, nothing can be published until the release,
             * because the CPU is parked in its halt gate until then. */
            SE_LOCK(); SeStopClear(); SeCancelSteps(); SeAtStore(&sPaused, 0); SE_UNLOCK();
        }
        else if (memcmp(req, SE_LIVE_VERB_STEP, SE_LIVE_VERB_LEN) == 0)
        {
            /* Same order: clear, then grant. The granted frame can hit a breakpoint. */
            SE_LOCK();
            SeStopClear();
            SeAtStore(&sPaused, 1);
            SeGrantSteps((arg > 0) ? (int)arg : 1);
            SE_UNLOCK();
        }
        else if (memcmp(req, SE_LIVE_VERB_REWIND, SE_LIVE_VERB_LEN) == 0)
        {
            /* Switching capture off drops whatever is already staged: those frames belong to a
             * timeline the client is no longer keeping, and holding them would pin the pool for
             * as long as the feature stayed off. Re-keying also means the first frame after it
             * is switched back on is a keyframe, which it has to be -- there is a gap behind it
             * and nothing to diff against. */
            const int want = (arg != 0) ? 1 : 0;
            if (want != SeAtLoad(&sRewindWanted))
            {
                SeAtStore(&sRewindWanted, want);
                SeStateFlushAndRekey();
            }
        }
        else if (memcmp(req, SE_LIVE_VERB_ISTEP, SE_LIVE_VERB_LEN) == 0)
        {
            /* Instruction step: release the CPU (it is spinning in the breakpoint gate)
             * and let it run `arg` instructions on the halted CPU before halting again.
             * The per-instruction hook picks up sInsnStepPending once the gate releases. */
            SE_LOCK();
            /* CPU first, then the request, then the old stop is cleared, and only then the release:
             * the CPU thread is parked in the halt gate until sPaused clears, then reads the request
             * and then the CPU, so each store is visible before the next one lets it proceed. The
             * clear has to precede the release -- a step that lands on its first instruction halts
             * again at once, and that new stop must not be erased by this verb's own clear. */
            SeAtStore(&sInsnStepCpu, (int)SE_STOP_CPU(SeAtLoad64(&sStopWord)));
            SeAtStore(&sInsnStepPending, (arg > 0) ? (int)arg : 1);
            SeCancelSteps();
            SeStopClear();
            SeAtStore(&sPaused, 0);
            SE_UNLOCK();
        }
        else if (memcmp(req, SE_LIVE_VERB_BKPTS, SE_LIVE_VERB_LEN) == 0)
        {
            /* Read all 'arg' 12-byte descriptors (every one is consumed to keep the
             * stream aligned) and install the enabled execution breakpoints. Past the protocol
             * maximum the descriptors are still consumed but not installed: without the cap a
             * request claiming 0xFFFFFFFF descriptors had the emulator installing breakpoints
             * for as long as a client kept feeding it. */
            /* Every descriptor is received before anything is installed, and installing is not done
             * here at all: the set is published whole and the emulate thread installs it (see the
             * mailbox above). The buffer is the thread's own, not a static: two server threads can be
             * serving at once. */
            unsigned char descs[SE_LIVE_MAX_BKPT_DESCS * SE_LIVE_BKPT_DESC_LEN];
            const unsigned int keep = arg > SE_LIVE_MAX_BKPT_DESCS ? SE_LIVE_MAX_BKPT_DESCS : arg;
            if (keep && SeRecv(cl, descs, keep * SE_LIVE_BKPT_DESC_LEN) != 0) return;
            SePublishBreakpoints(descs, keep);
            /* Descriptors past the cap are consumed without being decoded. */
            if (SeDrain(cl, (arg - keep) * SE_LIVE_BKPT_DESC_LEN) != 0) return;
        }
        else if (memcmp(req, SE_LIVE_VERB_WRITE, SE_LIVE_VERB_LEN) == 0)
        {
            /* Poke work RAM: payload = address(4 LE) + 'arg' big-endian bytes. */
            if (SeRecvPokeStream(cl, sWriteByte, arg, SE_LIVE_MAX_WRITE_BYTES) != 0) return;
        }
        else if (memcmp(req, SE_LIVE_VERB_WRITESND, SE_LIVE_VERB_LEN) == 0)
        {
            /* Poke sound RAM (v13+): payload = offset(4 LE) + 'arg' raw bytes. */
            if (SeRecvPokeStream(cl, sWriteSoundByte, arg, SE_LIVE_MAX_WRITE_BYTES) != 0) return;
        }
        else if (memcmp(req, SE_LIVE_VERB_LOADSTATE, SE_LIVE_VERB_LEN) == 0)
        {
            /* Rewind (v16): receive the whole 'arg'-byte payload (frame + edits_len + edits +
             * state) into a buffer of its own, then publish it to the load mailbox. The gate
             * applies it atomically on the emulate thread (restore + edits + resume), so nothing
             * races the async restore.
             *
             * The buffer is sized from 'arg', so without a bound a request claiming 4 GiB asks the
             * emulator for 4 GiB. Over the maximum the payload is drained and nothing is
             * allocated. */
            unsigned int payload = arg;
            const int tooLarge = payload > SE_LIVE_STATE_MAX_PAYLOAD;
            unsigned char* staging = NULL;
            size_t cap;
            SE_SLOCK(); cap = sStateCap; SE_SUNLOCK();
            if (payload >= 8u && !tooLarge && cap != 0) staging = (unsigned char*)malloc(payload);
            if (!staging)
            {
                /* Malformed, over the maximum, can't buffer, or feature off: drain to stay
                 * stream-aligned. The client is told the load did not happen. */
                SeRestoreFailed();
                if (SeDrain(cl, payload) != 0) return;
            }
            else
            {
                unsigned char* replaced; int superseded;
                if (SeRecv(cl, staging, payload) != 0) { free(staging); return; }
                SE_LOCK(); SeAtStore(&sPaused, 1); SeCancelSteps(); SeStopClear(); SE_UNLOCK();
                SE_SLOCK();
                superseded = SeAtLoad(&sLoadPending);   /* an earlier load the gate never reached */
                replaced = sLoadBuf;
                sLoadBuf = staging; sLoadLen = payload;
                SeAtStore(&sLoadPending, 1);   /* the gate picks this up on the emulate thread */
                SE_SUNLOCK();
                free(replaced);
                /* Every accepted load ends in exactly one counter. The one just replaced will
                 * never be applied, so it ends here, as refused. */
                if (superseded) SeRestoreFailed();
            }
        }
        else if (memcmp(req, SE_LIVE_VERB_EMULOAD, SE_LIVE_VERB_LEN) == 0)
        {
            /* Load one of the emulator's own save slots (v17). No payload; the gate runs it
             * on the emulate thread. Pause first so nothing advances underneath the load. */
            if (sEmuSlotLoad && arg < SE_LIVE_EMU_SLOTS)
            {
                SE_LOCK(); SeAtStore(&sPaused, 1); SeCancelSteps(); SeStopClear(); SE_UNLOCK();
                /* A slot load the gate has not reached yet is replaced, and ends as refused: every
                 * accepted load ends in exactly one counter. */
                if (SeAtXchg(&sEmuLoadPending, (int)arg + 1) != 0) SeRestoreFailed();
            }
            else
            {
                SeRestoreFailed();   /* no slot hook in this build, or no such slot */
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
             * publish them for the emulate thread; consume any beyond the cap to stay stream-aligned. */
            unsigned char tbuf[SE_LIVE_TRACE_DESC_LEN * SE_LIVE_MAX_TRACE_DESCS];   /* not static: two server threads */
            const unsigned int keep = arg > SE_LIVE_MAX_TRACE_DESCS ? SE_LIVE_MAX_TRACE_DESCS : arg;
            if (keep && SeRecv(cl, tbuf, keep * SE_LIVE_TRACE_DESC_LEN) != 0) return;
            if (SeDrain(cl, (arg - keep) * SE_LIVE_TRACE_DESC_LEN) != 0) return;
            SePublishTracepoints(tbuf, keep);   /* installed by the emulate thread */
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
                    /* Compare in 32-bit to match the client's frame counter, but test
                     * emptiness on the full 64-bit value (0 only means never-written). */
                    uint32_t f = (uint32_t)sRingFrame[i];
                    if (sRingFrame[i] != 0 && f > lastSeen && (found < 0 || f < best))
                    { best = f; found = i; }
                }
                if (found >= 0) slot = found;   /* oldest unseen; else stay on latest */
            }
            memcpy(snap, sRing[slot], sizeof(SeFrame));
            served = sRingFrame[slot];
        }
        SeWr32(ctl, (unsigned int)(SeAtLoad(&sPaused) ? 1 : 0));
        SeWr32(ctl + 4, (unsigned int)(served & 0xFFFFFFFFu));
        SeWr32(ctl + 8, (unsigned int)((served >> 32) & 0xFFFFFFFFu));
        {
            const unsigned long long stop = SeAtLoad64(&sStopWord);   /* one stop, read whole */
            SeWr32(ctl + 12, SE_STOP_REASON(stop));
            SeWr32(ctl + 16, SE_STOP_CPU(stop));
            SeWr32(ctl + 20, SE_STOP_PC(stop));
        }
        SeWr32(ctl + 24, sRestoreDone);
        SeWr32(ctl + 28, sRestoreFailed);
        SeWr32(ctl + 32, (unsigned int)(sRingFrame[(sRingWrite + SE_RING - 1) % SE_RING] & 0xFFFFFFFFu));
        SeWr32(ctl + 36, (unsigned int)SeAtLoad(&sStepOutstanding));
        SeWr32(ctl + 40, SE_STOP_SEQ(SeAtLoad64(&sStopWord)));
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
                unsigned char blk[SE_LIVE_CALLSTACK_BLOCK_MAX];
                const unsigned int len = SeExportSerializeCallStack(c, blk);
                if (SeSend(cl, blk, len) != 0) return;
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

        /* v15 trailing block: CD-block status. u32 length (SE_LIVE_CD_BLOCK_LEN when the glue
         * supplied it, else 0), then that many pre-serialized bytes. */
        {
            unsigned char lenb[4];
            unsigned int len = snap->has_cd ? (unsigned int)SE_LIVE_CD_BLOCK_LEN : 0u;
            SeWr32(lenb, len);
            if (SeSend(cl, lenb, 4) != 0) return;
            if (len && SeSend(cl, snap->cd, SE_LIVE_CD_BLOCK_LEN) != 0) return;
        }

        /* v16 trailing section: savestate rewind stream. u32 count, then 'count' blocks
         * (kind+frame+base+len header, then payload). Detach up to SE_LIVE_STATE_MAX_PER_REPLY
         * ready blocks under the lock, then send + free outside it. count 0 = nothing ready. */
        {
            SeStateBlock local[SE_LIVE_STATE_MAX_PER_REPLY];
            unsigned int cnt = 0, i;
            unsigned char cntb[4];
            SE_SLOCK();
            while (cnt < SE_LIVE_STATE_MAX_PER_REPLY && sOutCount > 0)
            {
                local[cnt++] = sOutFifo[sOutHead];
                sOutHead = (sOutHead + 1) % SE_STATE_OUTQ; --sOutCount;
            }
            SE_SUNLOCK();
            SeWr32(cntb, cnt);
            if (SeSend(cl, cntb, 4) != 0)
            {
                for (i = 0; i < cnt; ++i) free(local[i].payload);
                return;
            }
            for (i = 0; i < cnt; ++i)
            {
                unsigned char h[SE_LIVE_STATE_HDR_LEN];
                h[0] = local[i].kind;
                h[1] = (unsigned char)(local[i].epoch & 0xFFu);
                h[2] = (unsigned char)((local[i].epoch >> 8) & 0xFFu);
                h[3] = (unsigned char)((local[i].epoch >> 16) & 0xFFu);
                SeWr32(h + 4,  (unsigned int)(local[i].frame & 0xFFFFFFFFu));
                SeWr32(h + 8,  (unsigned int)(local[i].base  & 0xFFFFFFFFu));
                SeWr32(h + 12, (unsigned int)local[i].len);
                SeWr32(h + 16, (unsigned int)local[i].full);
                if (SeSend(cl, h, SE_LIVE_STATE_HDR_LEN) != 0 ||
                    (local[i].len && SeSend(cl, local[i].payload, local[i].len) != 0))
                {
                    unsigned int j;
                    for (j = i; j < cnt; ++j) free(local[j].payload);
                    return;
                }
                free(local[i].payload);
            }
        }

        /* v17 trailing block: the emulator's own save-slot inventory. u32 count (0 when this
         * build has no hook), then that many {present, mtime} records. */
        {
            unsigned char cntb[4];
            const unsigned int n = sEmuSlotInfo ? SE_LIVE_EMU_SLOTS : 0u;
            unsigned int i;
            SeWr32(cntb, n);
            if (SeSend(cl, cntb, 4) != 0) return;
            for (i = 0; i < n; ++i)
            {
                unsigned char rec[SE_LIVE_EMU_SLOT_LEN];
                unsigned long long mtime = 0;
                const int present = sEmuSlotInfo(i, &mtime);
                rec[0] = present ? 1u : 0u; rec[1] = rec[2] = rec[3] = 0;
                SeWr32(rec + 4, (unsigned int)(mtime & 0xFFFFFFFFu));
                SeWr32(rec + 8, (unsigned int)((mtime >> 32) & 0xFFFFFFFFu));
                if (SeSend(cl, rec, SE_LIVE_EMU_SLOT_LEN) != 0) return;
            }
        }
    }
}

#if defined(_WIN32)
/* --- Shutdown, the Windows half (review finding HOOK-01) ---------------------------------
 *
 * The server thread parks in one of two synchronous calls, and clearing sRunning reaches
 * neither of them:
 *
 *   ConnectNamedPipe(pipe, NULL)  waits for a client, with no timeout and no flag that a
 *                                 second thread can set to end the wait.
 *   ReadFile/WriteFile            waits on a connected client that may simply have stopped
 *                                 talking without closing.
 *
 * The old deinit waited one second, closed the thread handle and carried on into
 * DeleteCriticalSection and free() -- but closing a thread handle does not end the thread, so a
 * server still blocked in either call would wake later into a deleted critical section and a
 * freed frame ring. On POSIX the same shutdown is correct for a structural reason: closing the
 * listening socket makes accept() return, so the joins below it are unconditional.
 *
 * These two give Windows the same two properties: a way to make the blocking call return, and a
 * join that is either real or refuses to let the teardown proceed. */

/* Make a blocked server thread return. Connecting to our own pipe as a client and dropping the
 * connection at once is the analogue of closing the listening socket under accept(): the server
 * returns from ConnectNamedPipe, finds nothing to read, and re-tests sRunning. If a real client
 * is attached instead, the thread is inside ReadFile/WriteFile, and CancelSynchronousIo is the
 * documented way to interrupt a synchronous operation running on another thread -- it returns
 * ERROR_NOT_FOUND when nothing is pending, which is the ordinary case here and harmless. */
static void SeWinWakeServer(HANDLE thread)
{
    HANDLE poke = CreateFileA(SE_LIVE_DEFAULT_PIPE_NAME, GENERIC_READ | GENERIC_WRITE,
                              0, NULL, OPEN_EXISTING, 0, NULL);
    if (poke != INVALID_HANDLE_VALUE)
    {
        CloseHandle(poke);   /* the server's ConnectNamedPipe completes, then its read fails */
    }
    if (thread)
    {
        CancelSynchronousIo(thread);
    }
}

/* Wait for a thread to actually finish, nudging it between waits when it is the server.
 * Returns 1 only when the thread really has exited.
 *
 * The return value is the whole point. What follows a deinit destroys both critical sections and
 * frees the frame ring and the state pool, all of which a live thread touches -- so "we waited a
 * while and gave up" must not be followed by that teardown. */
static int SeWinJoinThread(HANDLE* thread, int nudge)
{
    int attempt;
    if (!thread || !*thread)
    {
        return 1;
    }
    for (attempt = 0; attempt < 5; ++attempt)
    {
        if (nudge)
        {
            SeWinWakeServer(*thread);
        }
        if (WaitForSingleObject(*thread, 1000) == WAIT_OBJECT_0)
        {
            CloseHandle(*thread);
            *thread = NULL;
            return 1;
        }
    }
    return 0;
}

static DWORD WINAPI SeServerThread(LPVOID arg)
{
    SeFrame* snap = (SeFrame*)malloc(sizeof(SeFrame));
    (void)arg;
    while (SeAtLoad(&sRunning) && snap)
    {
        HANDLE pipe = CreateNamedPipeA(SE_LIVE_DEFAULT_PIPE_NAME, PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                       1, 0, 0, 0, NULL);
        if (pipe == INVALID_HANDLE_VALUE) break;
        BOOL ok = ConnectNamedPipe(pipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (ok) SeServeClient(pipe, snap);
        SeOnClientDisconnect();
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    free(snap);
    return 0;
}
#else
/* Record the client a server thread is serving. If shutdown began between accept() and here
 * (sRunning already clear), nobody will interrupt this client, so interrupt it now. */
static void SeRegisterClient(int which, int fd)
{
    pthread_mutex_lock(&sConnLock);
    sActiveFd[which] = fd;
    if (!SeAtLoad(&sRunning)) shutdown(fd, SHUT_RDWR);
    pthread_mutex_unlock(&sConnLock);
}
static void SeUnregisterClient(int which)
{
    pthread_mutex_lock(&sConnLock);
    sActiveFd[which] = -1;
    pthread_mutex_unlock(&sConnLock);
}
/* Wake any server thread blocked on a client socket (shutdown makes its recv/send return). */
static void SeInterruptClients(void)
{
    int i;
    pthread_mutex_lock(&sConnLock);
    for (i = 0; i < 2; ++i)
    {
        if (sActiveFd[i] >= 0) shutdown(sActiveFd[i], SHUT_RDWR);
    }
    pthread_mutex_unlock(&sConnLock);
}

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
    while (SeAtLoad(&sRunning))
    {
        int cl = accept(srv, NULL, NULL);
        if (cl < 0) break;   /* closed on deinit */
        SeQuietSocket(cl);
        SeRegisterClient(0, cl);
        SeServeClient(cl, snap);
        SeOnClientDisconnect();
        SeUnregisterClient(0);
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
    while (SeAtLoad(&sRunning))
    {
        int cl = accept(srv, NULL, NULL);
        if (cl < 0) break;   /* closed on deinit */
        SeQuietSocket(cl);
        SeRegisterClient(1, cl);
        SeServeClient(cl, snap);
        SeOnClientDisconnect();
        SeUnregisterClient(1);
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
    if (sShutdownIncomplete)
    {
        /* The previous session left a thread running on these globals. Starting a second server
         * over them would give the survivor a frame ring that two threads write. */
        fprintf(stderr, "[SaturnExplorer] live tap: not restarting -- a thread from the previous "
                        "session never exited\n");
        return -1;
    }
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
    SeAtStore(&sPaused, 0); SeCancelSteps(); sFrameNo = 0; SeAtStore(&sClients, 0); SeAtStore(&sRewindWanted, 1);
    SeAtStore64(&sStopWord, 0);
    /* Savestate rewind (v16): the worker + buffer pool are created lazily when a save hook is
     * wired (SeExportSetSaveStateHook); here we only reset the bookkeeping for a fresh session. */
    sStateWorkerStarted = 0; SeAtStore(&sStateWorkerRun, 0); sStateCap = 0;
    sFreeCount = sRawHead = sRawCount = sOutHead = sOutCount = 0;
    SeAtStore(&sLoadPending, 0); SeAtStore(&sEmuLoadPending, 0);
    sStateGen = 1; sKeyGen = 0; sKeyLen = 0; sSinceKeyframe = 0;
    SeAtStore(&sRunning, 1);
#if defined(_WIN32)
    InitializeCriticalSection(&sLock);
    InitializeCriticalSection(&sStateLock);
    sLocksReady = 1;   /* both CSes are now real -- enable locking BEFORE any locking thread starts */
    sThread = CreateThread(NULL, 0, SeServerThread, NULL, 0, NULL);
    if (!sThread) { SeAtStore(&sRunning, 0); return -1; }
#else
    if (pthread_create(&sThread, NULL, SeServerThread, NULL) != 0) { SeAtStore(&sRunning, 0); return -1; }
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
    SeAtStore(&sRunning, 0);
    /* Stop the savestate worker first so nothing touches the state queues while we free them.
     * (The server threads are joined just below; ordering is safe either way.) */
    SeAtStore(&sStateWorkerRun, 0);
#if defined(_WIN32)
    {
        /* The state worker only ever sleeps between queue polls, so clearing its flag is enough
         * and it needs no nudge. The server does (see SeWinWakeServer). */
        int joined = SeWinJoinThread(&sStateWorker, 0);
        if (!SeWinJoinThread(&sThread, 1))
        {
            joined = 0;
        }
        if (!joined)
        {
            /* A thread is still inside a call we could not interrupt. Everything past this point
             * deletes a critical section or frees a buffer that such a thread uses, so the only
             * safe move is to leave it all allocated: the process is on its way out and the leak
             * costs nothing, whereas freeing the frame ring under a live server is a crash in
             * someone else's emulator with nothing in the stack pointing here. */
            fprintf(stderr, "[SaturnExplorer] live tap: a server thread did not exit; leaving its "
                            "locks and buffers allocated rather than freeing them underneath it\n");
            sShutdownIncomplete = 1;
            return;
        }
    }
    /* Both locking threads are joined -- single-threaded again -- so stop gating on the
     * locks and destroy them. Anything that still runs (e.g. SeStateShutdown) touches the
     * guarded data without a lock, which is safe with no other thread alive. */
    sLocksReady = 0;
    SeStateShutdown();
    DeleteCriticalSection(&sStateLock);
    DeleteCriticalSection(&sLock);
#else
    if (sStateWorkerStarted) { pthread_join(sStateWorker, NULL); }
    if (sListenFd >= 0) { shutdown(sListenFd, SHUT_RDWR); close(sListenFd); sListenFd = -1; }
    if (sTcpListenFd >= 0) { shutdown(sTcpListenFd, SHUT_RDWR); close(sTcpListenFd); sTcpListenFd = -1; }
    SeInterruptClients();   /* a client that stopped answering would otherwise pin the joins below */
    pthread_join(sThread, NULL);
    if (sTcpThreadStarted) { pthread_join(sTcpThread, NULL); sTcpThreadStarted = 0; }
    SeStateShutdown();
#endif
    {
        int i;
        for (i = 0; i < SE_RING; ++i) { free(sRing[i]); sRing[i] = NULL; sRingFrame[i] = 0; }
    }
    sRingWrite = 0;
}
