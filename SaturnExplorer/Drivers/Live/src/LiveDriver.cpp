// LiveDriver — see LiveDriver.h. A background thread polls the patched Yabause
// for a fresh VDP snapshot; the se_data_source callbacks serve the latest one.

#include "LiveDriver.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "ByteQueue.h"
#include "SaturnStateShared.h"
#include "SeLiveProtocol.h"
#include "saturnexplorer/SeGuard.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace
{

// One recorded shadow-stack frame (v9+); mirrors se_live_call_frame on the wire.
struct LiveCallFrame
{
    uint32_t callSite = 0, func = 0, ret = 0, sp = 0;
    uint64_t cycle = 0;
    uint32_t frameNo = 0;
};

// The per-CPU shadow call stacks from one snapshot (master, slave). A snapshot value,
// not a queue: each poll replaces it.
struct LiveCallStacks
{
    std::vector<LiveCallFrame> cpu[2];
};

// Why the emulator last stopped (control block, v5+): reason / cpu / pc of a breakpoint hit.
struct StopInfo { uint32_t reason = 0; uint32_t cpu = 0; uint32_t pc = 0; };

/* ---- One decoded, core-ready frame. All buffers are Saturn-native big-endian
       and register images are hardware-offset (directly usable by the core). ---- */
struct LiveSnapshot
{
    std::vector<uint8_t> vdp1Vram;
    std::vector<uint8_t> vdp2Vram;
    std::vector<uint8_t> cram;
    std::vector<uint8_t> vdp2Regs;   // hw-offset BE image
    std::vector<uint8_t> vdp1Regs;   // hw-offset BE image
    std::vector<uint8_t> wramLow;    // 0x00200000, 1 MiB (normalized to big-endian)
    std::vector<uint8_t> wramHigh;   // 0x06000000, 1 MiB (normalized to big-endian)
    std::vector<uint8_t> vdp1Fb;     // VDP1 frame buffer (drawn output)
    std::vector<uint8_t> soundRam;   // SCSP sound RAM (v13+; empty if server predates it)
    std::vector<se_scsp_slot> scspSlots;  // decoded SCSP voices (v14+; empty otherwise)
    se_cd_status         cdStatus = {};      // live CD-block state (v15+)
    bool                 hasCdStatus = false;
    se_sh2_regs          sh2[2] = {};        // [0] master, [1] slave (v5+)
    bool                 hasSh2[2] = { false, false };
    // Restore outcomes from the control block (v19+): how many LST/ELS loads the emulator has
    // applied / refused. Travel with the snapshot so a client that reads them and then captures
    // knows the capture is at least that new.
    uint32_t             restoreDone = 0;
    uint32_t             restoreFailed = 0;
    bool                 hasRestoreInfo = false;
    // Everything below describes the SAME frame as the memory above and is published with it, so
    // a capture pinned to this snapshot reports a frame number, run state, stop and call stack
    // that belong to the VRAM it read -- rather than whatever the emulator reached since.
    uint64_t             frame = 0;          // frame number of the frame served
    bool                 paused = false;
    StopInfo             stop;
    LiveCallStacks       callStacks;         // v9+; empty on an older server
    // Step completion (v20+): the newest frame in the server's ring (which a gap-free GET can
    // be behind), and the frames a STP granted that have not been published yet.
    uint32_t             latestFrame = 0;
    uint32_t             stepPending = 0;
    bool                 hasStepInfo = false;
    bool                 valid = false;
};

using SnapshotPtr = std::shared_ptr<const LiveSnapshot>;

// A fired tracepoint event (v8+): its id, the CPU, the frame it fired on, and the
// captured SH-2 register file (se_sh2_regs order). The client formats the message.
struct LiveEvent
{
    uint32_t id = 0;
    uint32_t cpu = 0;
    uint32_t frame = 0;
    uint32_t regs[SE_LIVE_EVENT_REGS] = {};
};

// The emulator's live host keyboard bindings from one snapshot (v10+): per port, the
// USB-HID scancode bound to each Saturn pad button (ascending SE_PAD_* order), -1 where
// unbound. 'valid' stays false until a v10+ server actually sends the block.
struct LiveKeyMap
{
    int32_t k[SE_LIVE_KEYMAP_PORTS][SE_LIVE_KEYMAP_BUTTONS];
    bool valid = false;
};

// The emulator's own numbered save slots (v17), as reported each reply. The client cannot
// find these on disk -- their path depends on the emulator's base directory, its state-path
// setting and a hash of the disc -- so the emulator reports the inventory instead.
struct LiveEmuSlots
{
    uint8_t  present[SE_LIVE_EMU_SLOTS] = {};
    uint64_t mtime[SE_LIVE_EMU_SLOTS] = {};
    bool     valid = false;   // false on a pre-v17 server, or a build with no slot hook
};

// One received savestate block (v16): a keyframe or a delta-vs-keyframe, frame-tagged. The
// payload is the opaque, RLE-compressed emulator image (the client never interprets it, only
// stores it and reconstructs a full image to hand back via LST). Blocks arrive lagging.
struct LiveStateBlock
{
    uint8_t  kind = 0;   // SE_LIVE_STATE_KIND_*
    uint32_t frame = 0;
    uint32_t base = 0;   // frame_no of the keyframe a delta is against (== frame for a keyframe)
    uint32_t fullLen = 0;   // decoded full-savestate size
    std::vector<uint8_t> payload;
};

// How ByteQueue measures a savestate block (see ByteQueue.h).
inline size_t ByteSizeOf(const LiveStateBlock& b) { return b.payload.size(); }

// The minimum server version a verb requires, or 0 for the verbs every server has known.
//
// One table consulted at the single point a request leaves, rather than a check per verb where
// the next verb added is the one nobody remembers to gate -- which had already happened: a
// version floor for WRS was defined and never read, so the one payload verb the scheme existed
// for was the one it missed.
static uint32_t MinVerFor(const char* verb)
{
    // Compared by content, like the server's own dispatch. Pointer equality would happen to
    // work while every caller passes the same macro, but it would be resting on the compiler
    // pooling identical string literals.
    auto is = [verb](const char* v) { return std::memcmp(verb, v, SE_LIVE_VERB_LEN) == 0; };
    if (is(SE_LIVE_VERB_LOADSTATE)) return SE_LIVE_MINVER_LOADSTATE;
    if (is(SE_LIVE_VERB_WRITESND))  return SE_LIVE_MINVER_WRITESND;
    if (is(SE_LIVE_VERB_TRACE))     return SE_LIVE_MINVER_TRACE;
    if (is(SE_LIVE_VERB_REWIND))    return SE_LIVE_MINVER_REWIND;
    return 0u;
}

// Every length and address this driver puts on the wire is little-endian u32.
inline void PushU32LE(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
    out.push_back((uint8_t)((v >> 16) & 0xFF));
    out.push_back((uint8_t)((v >> 24) & 0xFF));
}

// Ceilings on the driver's two byte-budgeted queues.
//
// Pokes: the poll thread ships one per cycle (~8 ms), so a producer that outruns it -- a
// hex-editor drag, a script writing every frame -- would grow the queue for as long as it kept
// writing. No single legitimate poke approaches this, the largest Saturn region being 1 MiB.
//
// Savestate blocks: the client's FrameRecorder ring is the real rewind budget; this only has to
// survive a UI thread that has not drained for a while (a modal dialog, a long load).
static const size_t kMaxPokeBytes = 8u * 1024u * 1024u;
static const size_t kMaxQueuedStateBytes = 64u * 1024u * 1024u;

// Pending control command the UI thread hands to the poll thread (which owns the
// single server connection). Best-effort: the poll thread drains it within one
// cycle (~8 ms). Steps accumulate so rapid presses aren't lost.
enum class Ctl { None, Pause, Resume, Step, StepInsn };

std::atomic<uint64_t> gNextStateId{1};

struct LiveState
{
    // Distinguishes this instance in the thread-local pins below. Not the address: a destroyed
    // instance's address can be handed to the next one, which would then inherit a stale pin.
    const uint64_t    id = gNextStateId.fetch_add(1);
    std::string       endpoint;
    std::thread       thread;
    std::atomic<bool> running{false};
    // The newest published snapshot, immutable once published. Readers copy the pointer under
    // 'mtx' and read from the copy, so a poll that replaces it mid-read cannot change what an
    // in-progress capture sees (see begin_capture in SeDataSource.h).
    std::mutex        mtx;
    SnapshotPtr       front;

    std::atomic<uint32_t> serverVersion{0};   // protocol version of the CURRENT connection (0 = unknown)
    // Bumped when the poll thread publishes the first snapshot of a (re)attached connection --
    // together with that snapshot, under 'mtx', so a client that sees the new number is
    // guaranteed to capture the new emulator's data and never the previous one's. The thread
    // reconnects on its own, so a client that only watches for errors never learns the emulator
    // it is talking to was replaced -- stop Mednafen, launch another game, and the same
    // se_context keeps streaming as though nothing happened, while everything the client
    // derived from the old run (recorded frames, call stack, scrub position) is silently
    // about to be mixed with the new one. Exposed so the client can drop that state.
    std::atomic<uint32_t> connGeneration{0};
    std::mutex            ctlMtx;    // guards pending / stepFrames / bkpts and every queue below
    // True while the current connection has completed its first exchange. Mutations (writes,
    // loads, steps) are only accepted -- and only ever shipped -- while it is set, and each
    // connection ends by discarding everything queued for it: work the user issued against one
    // emulator must not execute against the next one that happens to answer on the endpoint.
    // Guarded by ctlMtx.
    bool                  connected = false;
    Ctl                   pending = Ctl::None;
    int32_t               stepFrames = 0;
    int32_t               stepInsns = 0;    // instruction-step count for Ctl::StepInsn (IST)
    // Frame steps posted by the UI and steps the server has answered. Unequal means a step is
    // still on its way to the emulator (or its reply has not come back), which a capture-pending
    // check has to count: until the reply the snapshot says nothing about the step yet.
    // Guarded by ctlMtx.
    uint32_t              stepsPosted = 0;
    uint32_t              stepsAnswered = 0;
    // Pending breakpoint-set sync: when the UI changes breakpoints it bumps
    // 'bkptsDirty' and stashes the descriptor blob; the poll thread ships it with
    // a BKP command on its next cycle. Each descriptor is SE_LIVE_BKPT_DESC_LEN.
    std::vector<uint8_t>  bkpts;
    bool                  bkptsDirty = false;
    // Pending work-RAM pokes from the Hex Editor. Each entry is a WRM payload:
    // address(u32 LE) + big-endian bytes. The poll thread ships one per cycle.
    sfe::ByteQueue<std::vector<uint8_t>> writes;
    // Pending sound-RAM pokes (v13). Each entry is a WRS payload: offset(u32 LE) + bytes.
    sfe::ByteQueue<std::vector<uint8_t>> soundWrites;
    // True once we've told the emulator to pause/step and not since resumed, so the
    // poll thread knows to release it on close (never leave Yabause paused).
    std::atomic<bool>     pausedByUs{false};
    // Controller input to inject (v7+): packed (port << 16) | SE_PAD_* mask. The poll
    // thread sends an INP whenever this is non-zero or has changed since the last one
    // sent, so a held button survives even a glue that doesn't latch, and the release
    // edge (back to 0) is always delivered.
    std::atomic<uint32_t> inputState{0};
    uint32_t              lastInputSent = 0;   // poll-thread-local (guarded by ctlMtx use)
    // Gap-free cursor: the frame number of the last snapshot we received. A GET carries it
    // as its arg so the server hands back the next unseen frame (server keeps an N-deep
    // ring); the emulator can run ahead without us silently skipping frames. Poll-thread-local.
    uint32_t              lastSeenFrame = 0;
    // Pending tracepoint-set sync (v8+): the UI thread stashes the 16-byte descriptor
    // blob and bumps the dirty flag; the poll thread ships it with a TRC command. Same
    // pattern as breakpoints. Guarded by ctlMtx.
    std::vector<uint8_t>  traces;
    bool                  tracesDirty = false;
    // Tracepoint events received from the server, drained by the UI thread via
    // se_live_poll_events. Bounded so a flood can't grow unbounded. Guarded by evMtx.
    std::mutex            evMtx;
    std::deque<LiveEvent> events;
    // Latest host keyboard bindings (v10+): per port, the USB-HID scancode bound to each
    // Saturn pad button (ascending SE_PAD_* order), -1 where unbound. Replaced each
    // snapshot so it tracks the emulator's live config. Guarded by kmMtx.
    std::mutex            kmMtx;
    int32_t               keyMap[SE_LIVE_KEYMAP_PORTS][SE_LIVE_KEYMAP_BUTTONS];
    bool                  keyMapValid = false;   // false until a v10+ block is seen
    // Diagnostic log lines from the emulator (v11+), drained by the UI thread via
    // se_live_poll_log. Bounded so a flood can't grow unbounded. Guarded by logMtx.
    std::mutex            logMtx;
    std::deque<std::string> logLines;
    // Received savestate blocks (v16), drained by the UI thread via se_live_drain_state_blocks
    // and stored in the client's FrameRecorder. Bounded. Guarded by stateMtx.
    std::mutex            stateMtx;
    sfe::ByteQueue<LiveStateBlock> stateBlocks;
    // Pending LST rewind payload (v16): the whole wire payload (frame + edits_len + edits +
    // state), stashed by CbLoadState for the poll thread to ship one-shot. Guarded by ctlMtx.
    std::vector<uint8_t>  loadPayload;
    bool                  loadDirty = false;
    uint32_t              loadFrame = 0;
    // The emulator's own save-slot inventory (v17), refreshed every reply. Guarded by ctlMtx.
    uint8_t               emuSlotPresent[SE_LIVE_EMU_SLOTS] = {};
    uint64_t              emuSlotMtime[SE_LIVE_EMU_SLOTS] = {};
    bool                  emuSlotsValid = false;
    // Pending ELS (v17): slot + 1, or 0 for none. Guarded by ctlMtx.
    int                   emuLoadSlot = 0;
    // Rewind capture (v18). 'rewindDirty' means the server has not been told the current value:
    // set by the setter, and again on a reconnect, since a fresh server starts with capture on.
    // Guarded by ctlMtx.
    bool                  rewindWanted = true;
    bool                  rewindDirty = false;
};

// The snapshot a capture on this thread is pinned to (between begin_capture and end_capture),
// and the last one such a capture used. Thread-local because the pin belongs to the one thread
// running the capture; every other thread, and every read outside a capture, sees the newest.
// 'id' says which driver instance the entry belongs to, so two live sources never share a pin.
struct ThreadPin
{
    uint64_t    id = 0;
    SnapshotPtr snap;
    int         depth = 0;
};
thread_local ThreadPin gPinned;
thread_local ThreadPin gLastCaptured;

SnapshotPtr Newest(LiveState* st)
{
    std::lock_guard<std::mutex> lk(st->mtx);
    return st->front;
}

// The snapshot a read should be served from: the pinned one inside a capture (even when that
// is "nothing yet" -- a snapshot published mid-capture must not leak into it), else the newest.
SnapshotPtr CurrentSnapshot(LiveState* st)
{
    if (gPinned.id == st->id && gPinned.depth > 0) { return gPinned.snap; }
    return Newest(st);
}

// The snapshot the host's display was last captured from on this thread, falling back to the
// newest when it has not captured yet. What per-frame state (stop, call stack) must be read
// from so that it matches the frame on screen.
SnapshotPtr DisplayedSnapshot(LiveState* st)
{
    if (gPinned.id == st->id && gPinned.depth > 0) { return gPinned.snap; }
    if (gLastCaptured.id == st->id && gLastCaptured.snap) { return gLastCaptured.snap; }
    return Newest(st);
}

/* ---- Local-socket transport (POSIX Unix socket / Windows named pipe). ----
 *
 * Every wait here is bounded and cancellable. The poll thread is the only thing that talks to
 * the emulator, and CbClose joins it -- from the UI thread, on disconnect and on application
 * exit. A blocking read on an emulator that keeps the connection open but has stopped answering
 * (suspended in a debugger, hung, or simply wedged) would therefore freeze the UI, and the same
 * wait is what would keep a hung connection from ever being replaced. So I/O proceeds in short
 * slices that re-check the stop flag, and gives up after 'idleMs' without progress. */
const int kIoSliceMs = 100;          // how often a wait re-checks the stop flag
const int kIdleTimeoutMs = 10000;    // no progress for this long: the emulator is not answering
const int kConnectTimeoutMs = 2000;  // bound on establishing a connection

struct Conn
{
#if defined(_WIN32)
    HANDLE h = INVALID_HANDLE_VALUE;
    HANDLE ev = nullptr;     // completion event for overlapped I/O
    bool ok() const { return h != INVALID_HANDLE_VALUE; }
#else
    int fd = -1;
    bool ok() const { return fd >= 0; }
#endif
    // When set and false, any I/O on this connection gives up. Null = not cancellable (the
    // final best-effort resume on close, which runs after 'running' has already been cleared).
    const std::atomic<bool>* running = nullptr;
    int idleMs = kIdleTimeoutMs;

    bool Aborted() const { return running && !running->load(); }
};

// True for a TCP endpoint written as "tcp:host:port" (used for the web bridge,
// where the browser tunnels a normal TCP connect over a WebSocket proxy).
bool IsTcpEndpoint(const char* ep) { return ep && std::strncmp(ep, "tcp:", 4) == 0; }

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
// A peer that closes mid-send makes send() raise SIGPIPE, whose default action ends the whole
// process. Linux suppresses it per call (MSG_NOSIGNAL); macOS/BSD have no such flag and do it
// per socket (SO_NOSIGPIPE). Either way a dead peer becomes an ordinary send() error that sends
// the poll thread down its reconnect path instead of taking SaturnExplorer with it.
#if defined(MSG_NOSIGNAL)
const int kSendFlags = MSG_NOSIGNAL;
#else
const int kSendFlags = 0;
#endif

void ConfigureSocket(int fd)
{
#if defined(SO_NOSIGPIPE)
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) { ::fcntl(fd, F_SETFL, flags | O_NONBLOCK); }
}

// Wait until 'fd' is ready for 'events'. 1 = ready, 0 = gave up (stop requested or no progress
// for 'idleMs'), -1 = the descriptor is bad. A hang-up or error is reported as "ready": the
// send/recv that follows is what reports it, with the real errno.
int WaitFd(int fd, short events, const std::atomic<bool>* running, int idleMs)
{
    int waited = 0;
    for (;;)
    {
        if (running && !running->load()) { return 0; }
        pollfd p;
        p.fd = fd;
        p.events = events;
        p.revents = 0;
        const int r = ::poll(&p, 1, kIoSliceMs);
        if (r > 0) { return (p.revents & POLLNVAL) ? -1 : 1; }
        if (r < 0)
        {
            if (errno == EINTR) { continue; }
            return -1;
        }
        waited += kIoSliceMs;
        if (waited >= idleMs) { return 0; }
    }
}

// Connect 'fd' (already non-blocking) with a bound on how long it may take.
bool ConnectBounded(int fd, const sockaddr* addr, socklen_t len,
                    const std::atomic<bool>* running, int timeoutMs)
{
    if (::connect(fd, addr, len) == 0) { return true; }
    if (errno != EINPROGRESS) { return false; }   // includes EAGAIN: a full local backlog
    if (WaitFd(fd, POLLOUT, running, timeoutMs) != 1) { return false; }
    int err = 0;
    socklen_t elen = sizeof(err);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) != 0) { return false; }
    return err == 0;
}

// Connect a POSIX TCP socket to "tcp:host:port". This is the path the Emscripten
// build takes (its sockets are proxied to a WebSocket bridge), and the one the
// native test harness uses; Windows native uses the named pipe instead.
bool ConnOpenTcp(Conn& c, const char* endpoint, int timeoutMs)
{
    const char* rest = endpoint + 4;                 // skip "tcp:"
    const char* colon = std::strrchr(rest, ':');
    if (!colon || colon == rest) { return false; }
    std::string host(rest, static_cast<size_t>(colon - rest));
    const char* port = colon + 1;

    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), port, &hints, &res) != 0 || !res) { return false; }
    for (addrinfo* ai = res; ai; ai = ai->ai_next)
    {
        int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) { continue; }
        ConfigureSocket(fd);
        if (ConnectBounded(fd, ai->ai_addr, ai->ai_addrlen, c.running, timeoutMs)) { c.fd = fd; break; }
        ::close(fd);
    }
    ::freeaddrinfo(res);
    return c.fd >= 0;
}
#elif defined(__EMSCRIPTEN__)
// The browser build keeps the plain blocking calls: its sockets are proxied to a WebSocket
// bridge, and it has no poll()-driven stop path to exercise (the tab owns the page's lifetime).
bool ConnOpenTcp(Conn& c, const char* endpoint, int)
{
    const char* rest = endpoint + 4;
    const char* colon = std::strrchr(rest, ':');
    if (!colon || colon == rest) { return false; }
    std::string host(rest, static_cast<size_t>(colon - rest));
    const char* port = colon + 1;
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), port, &hints, &res) != 0 || !res) { return false; }
    for (addrinfo* ai = res; ai; ai = ai->ai_next)
    {
        int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) { continue; }
        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) { c.fd = fd; break; }
        ::close(fd);
    }
    ::freeaddrinfo(res);
    return c.fd >= 0;
}
#endif

bool ConnOpen(Conn& c, const char* endpoint, const std::atomic<bool>* running = nullptr,
              int timeoutMs = kConnectTimeoutMs)
{
    c.running = running;
    c.idleMs = kIdleTimeoutMs;
#if defined(_WIN32)
    (void)timeoutMs;
    // Windows native: local named pipe. (TCP for the web bridge is POSIX-side.) Opened for
    // overlapped I/O so a read or write can be abandoned (see WinIo) -- a synchronous ReadFile
    // on a pipe whose server stopped answering cannot be interrupted from another thread.
    c.h = CreateFileA(endpoint, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (c.h == INVALID_HANDLE_VALUE) { return false; }
    c.ev = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!c.ev)
    {
        CloseHandle(c.h);
        c.h = INVALID_HANDLE_VALUE;
        return false;
    }
    return true;
#else
    if (IsTcpEndpoint(endpoint))
    {
        return ConnOpenTcp(c, endpoint, timeoutMs);
    }
#if defined(__EMSCRIPTEN__)
    return false;
#else
    c.fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (c.fd < 0)
    {
        return false;
    }
    ConfigureSocket(c.fd);
    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, endpoint, sizeof(addr.sun_path) - 1);
    if (!ConnectBounded(c.fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr), running, timeoutMs))
    {
        ::close(c.fd);
        c.fd = -1;
        return false;
    }
    return true;
#endif
#endif
}

void ConnClose(Conn& c)
{
#if defined(_WIN32)
    if (c.h != INVALID_HANDLE_VALUE) { CloseHandle(c.h); c.h = INVALID_HANDLE_VALUE; }
    if (c.ev) { CloseHandle(c.ev); c.ev = nullptr; }
#else
    if (c.fd >= 0) { ::close(c.fd); c.fd = -1; }
#endif
}

#if defined(_WIN32)
// One overlapped read or write of up to 'len' bytes. Waits in slices so a stop request or an
// idle timeout can cancel it; CancelIoEx + a final GetOverlappedResult(wait) is what makes the
// buffer safe to release afterwards (the kernel may still be writing to it until then).
bool WinIo(Conn& c, bool write, void* buf, DWORD len, DWORD& got)
{
    OVERLAPPED ov;
    std::memset(&ov, 0, sizeof(ov));
    ov.hEvent = c.ev;
    ResetEvent(c.ev);
    got = 0;
    const BOOL started = write ? WriteFile(c.h, buf, len, nullptr, &ov)
                               : ReadFile(c.h, buf, len, nullptr, &ov);
    if (!started)
    {
        if (GetLastError() != ERROR_IO_PENDING) { return false; }
        int waited = 0;
        for (;;)
        {
            if (WaitForSingleObject(c.ev, kIoSliceMs) == WAIT_OBJECT_0) { break; }
            waited += kIoSliceMs;
            if (c.Aborted() || waited >= c.idleMs)
            {
                CancelIoEx(c.h, &ov);
                DWORD ignored = 0;
                GetOverlappedResult(c.h, &ov, &ignored, TRUE);
                return false;
            }
        }
    }
    if (!GetOverlappedResult(c.h, &ov, &got, FALSE)) { return false; }
    return got != 0;
}
#endif

bool ConnWrite(Conn& c, const void* data, size_t size)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < size)
    {
#if defined(_WIN32)
        DWORD n = 0;
        if (!WinIo(c, true, const_cast<uint8_t*>(p + done), static_cast<DWORD>(size - done), n))
            return false;
#elif defined(__EMSCRIPTEN__)
        ssize_t n = ::send(c.fd, p + done, size - done, 0);
        if (n <= 0)
            return false;
#else
        ssize_t n = ::send(c.fd, p + done, size - done, kSendFlags);
        if (n < 0)
        {
            if (errno == EINTR) { continue; }
            if (errno != EAGAIN && errno != EWOULDBLOCK) { return false; }
            if (WaitFd(c.fd, POLLOUT, c.running, c.idleMs) != 1) { return false; }
            continue;
        }
        if (n == 0)
            return false;
#endif
        done += static_cast<size_t>(n);
    }
    return true;
}

bool ConnReadFull(Conn& c, void* data, size_t size)
{
    uint8_t* p = static_cast<uint8_t*>(data);
    size_t done = 0;
    while (done < size)
    {
#if defined(_WIN32)
        DWORD n = 0;
        if (!WinIo(c, false, p + done, static_cast<DWORD>(size - done), n))
            return false;
#elif defined(__EMSCRIPTEN__)
        ssize_t n = ::recv(c.fd, p + done, size - done, 0);
        if (n <= 0)
            return false;
#else
        ssize_t n = ::recv(c.fd, p + done, size - done, 0);
        if (n < 0)
        {
            if (errno == EINTR) { continue; }
            if (errno != EAGAIN && errno != EWOULDBLOCK) { return false; }
            if (WaitFd(c.fd, POLLIN, c.running, c.idleMs) != 1) { return false; }
            continue;
        }
        if (n == 0)
            return false;
#endif
        done += static_cast<size_t>(n);
    }
    return true;
}

uint32_t Rd32LE(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// The wire carries 64-bit values (frame counters, cycle counts, timestamps) as two
// little-endian words, low half first.
uint64_t Rd64LE(const uint8_t* p)
{
    return static_cast<uint64_t>(Rd32LE(p)) | (static_cast<uint64_t>(Rd32LE(p + 4)) << 32);
}

// Send one command frame (verb + little-endian arg) to the server.
bool SendCommand(Conn& c, const char* verb, int32_t arg)
{
    uint8_t req[SE_LIVE_REQUEST_LEN];
    std::memcpy(req, verb, SE_LIVE_VERB_LEN);
    const uint32_t a = static_cast<uint32_t>(arg);
    req[4] = static_cast<uint8_t>(a & 0xFF);
    req[5] = static_cast<uint8_t>((a >> 8) & 0xFF);
    req[6] = static_cast<uint8_t>((a >> 16) & 0xFF);
    req[7] = static_cast<uint8_t>((a >> 24) & 0xFF);
    return ConnWrite(c, req, sizeof(req));
}

// Issue 'verb' (arg) and read the snapshot the server replies with into 'snap'
// (converted to core-ready form). Also returns the run state via outPaused /
// outFrame. Returns false on any protocol/socket error.
bool ReadSnapshot(Conn& c, const char* verb, int32_t arg,
                  const uint8_t* extra, size_t extraLen, LiveSnapshot& snap,
                  bool& outPaused, uint64_t& outFrame, uint32_t& outVersion,
                  StopInfo& outStop, std::vector<LiveEvent>& outEvents,
                  LiveCallStacks& outCallStacks, LiveKeyMap& outKeyMap,
                  std::vector<std::string>& outLog,
                  std::vector<LiveStateBlock>& outStateBlocks, LiveEmuSlots& outEmuSlots)
{
    if (!SendCommand(c, verb, arg))
    {
        return false;
    }
    // Some commands (BKP) carry a payload after the 8-byte frame; ship it now so
    // the server can consume it before replying with the snapshot.
    if (extra && extraLen && !ConnWrite(c, extra, extraLen))
    {
        return false;
    }
    // Read magic + version first, then size the section-length table to the
    // server's version so we stay in sync across versions: v3 has 8 sections, v4
    // adds the VDP1 frame buffer (9), v5 adds SH-2 state (10). A newer client stays
    // compatible with an older, not-yet-rebuilt Yabause: the missing sections read
    // as length 0 and their consumers become no-ops.
    uint8_t head[8];
    if (!ConnReadFull(c, head, sizeof(head)))
    {
        return false;
    }
    if (head[0] != SE_LIVE_MAGIC0 || head[1] != SE_LIVE_MAGIC1 ||
        head[2] != SE_LIVE_MAGIC2 || head[3] != SE_LIVE_MAGIC3)
    {
        return false;
    }
    const uint32_t version = Rd32LE(head + 4);
    outVersion = version;
    const int hasFb = (version >= 4u) ? 1 : 0;    // FB section added in v4
    const int hasSh2 = (version >= 5u) ? 1 : 0;   // SH-2 state added in v5
    const int numLen = 8 + hasFb + hasSh2;        // section-length entries
    uint8_t lens[10 * 4];
    if (!ConnReadFull(c, lens, static_cast<size_t>(numLen) * 4))
    {
        return false;
    }
    int off = 0;
    const uint32_t v1 = Rd32LE(lens + (off++ * 4));
    const uint32_t v2 = Rd32LE(lens + (off++ * 4));
    const uint32_t cr = Rd32LE(lens + (off++ * 4));
    const uint32_t vs = Rd32LE(lens + (off++ * 4));
    const uint32_t vr = Rd32LE(lens + (off++ * 4));
    const uint32_t wl = Rd32LE(lens + (off++ * 4));
    const uint32_t wh = Rd32LE(lens + (off++ * 4));
    const uint32_t fb = hasFb  ? Rd32LE(lens + (off++ * 4)) : 0u;
    const uint32_t ct = Rd32LE(lens + (off++ * 4));
    const uint32_t sh = hasSh2 ? Rd32LE(lens + (off++ * 4)) : 0u;
    // Sanity clamps so a malformed header can't drive a huge allocation.
    if (v1 > 0x100000u || v2 > 0x100000u || cr > 0x4000u || vs > 4096u ||
        vr > 256u || wl > 0x100000u || wh > 0x100000u || fb > 0x40000u ||
        ct > 64u || sh > 256u)
    {
        return false;
    }

    snap.vdp1Vram.resize(v1);
    snap.vdp2Vram.resize(v2);
    snap.cram.resize(cr);
    std::vector<uint8_t> vdp2Struct(vs);
    snap.vdp1Regs.resize(vr);
    snap.wramLow.resize(wl);
    snap.wramHigh.resize(wh);
    snap.vdp1Fb.resize(fb);
    std::vector<uint8_t> ctl(ct);
    std::vector<uint8_t> sh2(sh);
    if (!ConnReadFull(c, snap.vdp1Vram.data(), v1) ||
        !ConnReadFull(c, snap.vdp2Vram.data(), v2) ||
        !ConnReadFull(c, snap.cram.data(), cr) ||
        !ConnReadFull(c, vdp2Struct.data(), vs) ||
        !ConnReadFull(c, snap.vdp1Regs.data(), vr) ||
        !ConnReadFull(c, snap.wramLow.data(), wl) ||
        !ConnReadFull(c, snap.wramHigh.data(), wh) ||
        !ConnReadFull(c, snap.vdp1Fb.data(), fb) ||
        !ConnReadFull(c, ctl.data(), ct) ||
        !ConnReadFull(c, sh2.data(), sh))
    {
        return false;
    }

    // v8+ trailing block: fired tracepoint events (u32 count, then count records).
    // Read only when the server speaks v8, so a v8 client stays compatible with an
    // older server (which sends no trailing block).
    if (version >= 8u)
    {
        uint8_t cntb[4];
        if (!ConnReadFull(c, cntb, 4)) return false;
        const uint32_t n = Rd32LE(cntb);
        if (n > SE_LIVE_EVENTS_MAX) return false;   // desync guard
        for (uint32_t i = 0; i < n; ++i)
        {
            uint8_t eb[SE_LIVE_EVENT_LEN];
            if (!ConnReadFull(c, eb, SE_LIVE_EVENT_LEN)) return false;
            LiveEvent ev;
            ev.id = Rd32LE(eb);
            ev.cpu = Rd32LE(eb + 4);
            ev.frame = Rd32LE(eb + 8);
            for (int j = 0; j < SE_LIVE_EVENT_REGS; ++j) ev.regs[j] = Rd32LE(eb + 12 + j * 4);
            outEvents.push_back(ev);
        }
    }

    // v9+ trailing block: the per-CPU shadow call stack (master then slave). Read only
    // when the server speaks v9, so a v9 client stays compatible with an older server.
    if (version >= 9u)
    {
        for (int cpu = 0; cpu < 2; ++cpu)
        {
            uint8_t cntb[4];
            if (!ConnReadFull(c, cntb, 4)) return false;
            const uint32_t n = Rd32LE(cntb);
            if (n > SE_LIVE_CALLSTACK_MAX) return false;   // desync guard
            outCallStacks.cpu[cpu].clear();
            outCallStacks.cpu[cpu].reserve(n);
            for (uint32_t i = 0; i < n; ++i)
            {
                uint8_t fb[SE_LIVE_CALLFRAME_LEN];
                if (!ConnReadFull(c, fb, SE_LIVE_CALLFRAME_LEN)) return false;
                LiveCallFrame f;
                f.callSite = Rd32LE(fb + 0);
                f.func     = Rd32LE(fb + 4);
                f.ret      = Rd32LE(fb + 8);
                f.sp       = Rd32LE(fb + 12);
                f.cycle    = Rd64LE(fb + 16);
                f.frameNo  = Rd32LE(fb + 24);
                outCallStacks.cpu[cpu].push_back(f);
            }
        }
    }

    // v10+ trailing block: the emulator's live host keyboard bindings. For port 0 then
    // port 1, 13 int32 (LE) USB-HID scancodes (-1 where unbound). Read only when the
    // server speaks v10, so a v10 client stays compatible with an older server.
    if (version >= 10u)
    {
        uint8_t kb[SE_LIVE_KEYMAP_LEN];
        if (!ConnReadFull(c, kb, SE_LIVE_KEYMAP_LEN)) return false;
        for (int p = 0; p < SE_LIVE_KEYMAP_PORTS; ++p)
            for (int b = 0; b < SE_LIVE_KEYMAP_BUTTONS; ++b)
                outKeyMap.k[p][b] =
                    (int32_t)Rd32LE(kb + (p * SE_LIVE_KEYMAP_BUTTONS + b) * 4);
        outKeyMap.valid = true;
    }

    // v11+ trailing block: diagnostic log lines. u32 count (capped), then that many
    // fixed-length NUL-padded records. Read only when the server speaks v11.
    if (version >= 11u)
    {
        uint8_t cntb[4];
        if (!ConnReadFull(c, cntb, 4)) return false;
        const uint32_t n = Rd32LE(cntb);
        if (n > SE_LIVE_LOG_MAX) return false;   // desync guard
        for (uint32_t i = 0; i < n; ++i)
        {
            char line[SE_LIVE_LOG_LINE_LEN];
            if (!ConnReadFull(c, reinterpret_cast<uint8_t*>(line), SE_LIVE_LOG_LINE_LEN))
                return false;
            line[SE_LIVE_LOG_LINE_LEN - 1] = 0;
            outLog.emplace_back(line);
        }
    }

    // v13+ trailing block: SCSP sound RAM. u32 length (LE), then that many bytes (0 when
    // the emulator didn't supply it). Read only when the server speaks v13.
    if (version >= 13u)
    {
        uint8_t lenb[4];
        if (!ConnReadFull(c, lenb, 4)) return false;
        const uint32_t n = Rd32LE(lenb);
        if (n != 0u && n != SE_LIVE_SOUND_RAM_LEN) return false;   // desync guard
        snap.soundRam.resize(n);
        if (n && !ConnReadFull(c, snap.soundRam.data(), n)) return false;
    }

    // v14+ trailing block: decoded SCSP voices. u32 length, then SE_LIVE_SCSP_SLOTS fixed
    // records (little-endian, layout per SeLiveProtocol.h). Read only when server speaks v14.
    if (version >= 14u)
    {
        uint8_t lenb[4];
        if (!ConnReadFull(c, lenb, 4)) return false;
        const uint32_t n = Rd32LE(lenb);
        if (n != 0u && n != SE_LIVE_SCSP_BLOCK_LEN) return false;   // desync guard
        snap.scspSlots.clear();
        if (n)
        {
            uint8_t blk[SE_LIVE_SCSP_BLOCK_LEN];
            if (!ConnReadFull(c, blk, n)) return false;
            snap.scspSlots.resize(SE_LIVE_SCSP_SLOTS);
            for (uint32_t i = 0; i < SE_LIVE_SCSP_SLOTS; ++i)
            {
                const uint8_t* r = blk + i * SE_LIVE_SCSP_SLOT_LEN;
                se_scsp_slot& s = snap.scspSlots[i];
                s.key_on = r[0]; s.active = r[1]; s.eg_phase = r[2]; s.format = r[3];
                s.loop_mode = r[4]; s.octave = static_cast<int8_t>(r[5]);
                s.total_level = r[6]; s.direct_level = r[7]; s.direct_pan = r[8];
                s.effect_level = r[9]; s.effect_pan = r[10];
                s.ar = r[11]; s.d1r = r[12]; s.d2r = r[13]; s.rr = r[14]; s.dl = r[15];
                s.eg_level = static_cast<uint16_t>(r[16] | (r[17] << 8));
                s.freq_num = static_cast<uint16_t>(r[18] | (r[19] << 8));
                s.start_addr = Rd32LE(r + 20); s.loop_start = Rd32LE(r + 24);
                s.loop_end = Rd32LE(r + 28);   s.cur_addr = Rd32LE(r + 32);
            }
        }
    }

    // v15+ trailing block: live CD-block status. u32 length, then the fixed record (or 0 when
    // the emulator has no CD tap). Read only when the server speaks v15.
    if (version >= 15u)
    {
        uint8_t lenb[4];
        if (!ConnReadFull(c, lenb, 4)) return false;
        const uint32_t n = Rd32LE(lenb);
        if (n != 0u && n != SE_LIVE_CD_BLOCK_LEN) return false;   // desync guard
        snap.hasCdStatus = false;
        if (n)
        {
            uint8_t blk[SE_LIVE_CD_BLOCK_LEN];
            if (!ConnReadFull(c, blk, n)) return false;
            snap.cdStatus.current_fad    = Rd32LE(blk + 0);
            snap.cdStatus.play_start_fad = Rd32LE(blk + 4);
            snap.cdStatus.play_end_fad   = Rd32LE(blk + 8);
            snap.cdStatus.status         = blk[12];
            snap.hasCdStatus = true;
        }
    }

    // v16+ trailing section: savestate rewind stream. u32 count, then 'count' blocks (each a
    // kind+frame+base+len header + payload). Lagging: usually 0 or 1 per reply.
    if (version >= 16u)
    {
        uint8_t cntb[4];
        if (!ConnReadFull(c, cntb, 4)) return false;
        const uint32_t count = Rd32LE(cntb);
        if (count > SE_LIVE_STATE_MAX_PER_REPLY) return false;   // desync guard
        for (uint32_t i = 0; i < count; ++i)
        {
            uint8_t h[SE_LIVE_STATE_HDR_LEN];
            if (!ConnReadFull(c, h, SE_LIVE_STATE_HDR_LEN)) return false;
            LiveStateBlock b;
            b.kind    = h[0];
            b.frame   = Rd32LE(h + 4);
            b.base    = Rd32LE(h + 8);
            const uint32_t plen = Rd32LE(h + 12);
            b.fullLen = Rd32LE(h + 16);
            if (plen > SE_LIVE_STATE_MAX_PAYLOAD || b.fullLen > SE_LIVE_STATE_MAX_PAYLOAD) return false;
            b.payload.resize(plen);
            if (plen && !ConnReadFull(c, b.payload.data(), plen)) return false;
            outStateBlocks.push_back(std::move(b));
        }
    }

    // v17: the emulator's own save-slot inventory. Fixed-size records, so a count other
    // than 0 or SE_LIVE_EMU_SLOTS means the stream is out of step.
    if (version >= 17u)
    {
        uint8_t cntb[4];
        if (!ConnReadFull(c, cntb, 4)) return false;
        const uint32_t count = Rd32LE(cntb);
        if (count != 0u && count != SE_LIVE_EMU_SLOTS) return false;
        for (uint32_t i = 0; i < count; ++i)
        {
            uint8_t rec[SE_LIVE_EMU_SLOT_LEN];
            if (!ConnReadFull(c, rec, SE_LIVE_EMU_SLOT_LEN)) return false;
            outEmuSlots.present[i] = rec[0];
            outEmuSlots.mtime[i] = Rd64LE(rec + 4);
        }
        outEmuSlots.valid = count != 0u;
    }

    // Control block: paused (u32 LE) + frame (u64 LE), then (v5+) stop reason/cpu/pc.
    // Absent fields default to 0 on older servers.
    if (ct >= 32)
    {
        snap.restoreDone = Rd32LE(ctl.data() + 24);
        snap.restoreFailed = Rd32LE(ctl.data() + 28);
        snap.hasRestoreInfo = true;
    }
    if (ct >= 40)
    {
        snap.latestFrame = Rd32LE(ctl.data() + 32);
        snap.stepPending = Rd32LE(ctl.data() + 36);
        snap.hasStepInfo = true;
    }
    outPaused = ct >= 4 && Rd32LE(ctl.data()) != 0;
    outFrame = ct >= 12 ? Rd64LE(ctl.data() + 4) : 0;
    outStop = StopInfo{};
    if (ct >= 24)
    {
        outStop.reason = Rd32LE(ctl.data() + 12);
        outStop.cpu    = Rd32LE(ctl.data() + 16);
        outStop.pc     = Rd32LE(ctl.data() + 20);
    }

    // SH-2 state (v5+): master then slave, each a 92-byte sh2regs_struct.
    for (int cpu = 0; cpu < 2; ++cpu)
    {
        const size_t base = static_cast<size_t>(cpu) * SE_LIVE_SH2_REGS_LEN;
        if (sh >= base + SE_LIVE_SH2_REGS_LEN)
        {
            sedrv::ParseSh2Regs(sh2.data() + base, snap.sh2[cpu]);
            snap.hasSh2[cpu] = true;
        }
    }

    // Work RAM arrives in Yabause host order; normalize to Saturn big-endian so
    // watches and the SH-2 disassembler read it correctly (same as the savestate). An odd
    // length is a malformed reply, not a region with a spare byte: fail the snapshot rather
    // than publish memory that is byte-swapped up to a point and raw after it.
    if (!sedrv::Bswap16(snap.wramLow.data(), snap.wramLow.size()) ||
        !sedrv::Bswap16(snap.wramHigh.data(), snap.wramHigh.size()))
    {
        return false;
    }

    // VRAM is already big-endian; build the VDP2 register image and use RAMCTL's
    // CRAM mode to normalize CRAM — exactly like the savestate path.
    sedrv::BuildVdp2RegImage(vdp2Struct, 0, snap.vdp2Regs);
    const uint16_t ramctl = sedrv::ReadReg16(snap.vdp2Regs, 0x0E);
    sedrv::NormalizeCramToBigEndian(snap.cram, (ramctl >> 12) & 0x3u);
    snap.valid = true;
    return true;
}

// Sleep up to 'ms', but wake promptly when the driver is asked to stop, so a reconnect backoff
// never holds up CbClose's join.
void SleepWhileRunning(LiveState* st, int ms)
{
    for (int waited = 0; waited < ms && st->running.load(); waited += 10)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Discard everything that was queued for a connection that has just ended. Run when a read or
// write on it fails, under the same lock the producers take, so that once 'connected' is clear
// the callbacks refuse new work instead of queueing it for whichever emulator answers next.
//
// What this protects against: the emulator exits with writes, a savestate load or step commands
// still queued; another game is launched on the same endpoint; the poll thread reconnects and
// would, without this, run the old session's pokes and restore against a machine the user never
// pointed them at -- and could do it before learning what protocol version that machine speaks.
void ForgetConnection(LiveState* st)
{
    {
        std::lock_guard<std::mutex> lk(st->ctlMtx);
        st->connected = false;
        st->writes.Clear();
        st->soundWrites.Clear();
        st->loadPayload.clear();
        st->loadDirty = false;
        st->emuLoadSlot = 0;
        st->pending = Ctl::None;
        st->stepFrames = 0;
        st->stepInsns = 0;
        st->stepsAnswered = st->stepsPosted;
        st->lastInputSent = 0;
        st->emuSlotsValid = false;
    }
    st->pausedByUs.store(false);   // the exporter releases a pause when a client leaves
    // Unknown until the next connection answers: every version-gated verb refuses meanwhile.
    st->serverVersion.store(0);
}

void PollLoop(LiveState* st)
{
    Conn conn;
    // True once this connection has completed its first exchange. Until then the only thing
    // sent is a plain GET: the emulator on the other end may not be the one the cached protocol
    // version and queued work were meant for, and a GET is the one request every version
    // answers, so it is what establishes the version before anything that mutates is shipped.
    bool handshaken = false;
    while (st->running.load())
    {
        if (!conn.ok())
        {
            if (!ConnOpen(conn, st->endpoint.c_str(), &st->running))
            {
                SleepWhileRunning(st, 250);
                continue;
            }
            // A fresh socket may be a different emulator process on the same endpoint, and
            // nothing in the protocol distinguishes the two. Restart the gap-free cursor --
            // an emulator that just booted is on frame 0, and asking it for the frame after
            // the last one the *previous* instance sent would skip its entire run. The
            // generation is NOT published here: it goes out with the first snapshot of this
            // connection, so a client that sees the new number never captures the old data.
            st->lastSeenFrame = 0;
            handshaken = false;
        }

        // Drain any control command posted by the UI thread; otherwise poll. A
        // pending breakpoint-set sync takes priority so new breakpoints install
        // before the next frame runs; it carries the descriptor blob as payload.
        const char* verb = SE_LIVE_VERB_GET;
        int32_t arg = 0;
        std::vector<uint8_t> payload;
        bool shippedLoad = false;
        bool shippedStep = false;
        uint32_t shippedStepSeq = 0;
        uint32_t loadResyncFrame = 0;
        if (handshaken)
        {
            std::lock_guard<std::mutex> lk(st->ctlMtx);
            if (st->loadDirty)
            {
                // Rewind (v16) takes top priority: one atomic LST (restore + edits + resume).
                verb = SE_LIVE_VERB_LOADSTATE;
                arg = static_cast<int32_t>(st->loadPayload.size());
                payload = std::move(st->loadPayload);
                st->loadPayload.clear();
                st->loadDirty = false;
                shippedLoad = true;
                loadResyncFrame = st->loadFrame;
            }
            else if (st->emuLoadSlot != 0)
            {
                // Emulator-native slot load (v17): no payload, the emulator does the work.
                verb = SE_LIVE_VERB_EMULOAD;
                arg = st->emuLoadSlot - 1;
                st->emuLoadSlot = 0;
            }
            else if (st->rewindDirty)
            {
                // Before the breakpoint/tracepoint syncs: this one decides whether the emulator
                // spends a full savestate on every frame, so it should take effect promptly.
                verb = SE_LIVE_VERB_REWIND;
                arg = st->rewindWanted ? 1 : 0;
                st->rewindDirty = false;
            }
            else if (st->bkptsDirty)
            {
                verb = SE_LIVE_VERB_BKPTS;
                arg = static_cast<int32_t>(st->bkpts.size() / SE_LIVE_BKPT_DESC_LEN);
                payload = st->bkpts;
                st->bkptsDirty = false;
            }
            else if (st->tracesDirty)
            {
                verb = SE_LIVE_VERB_TRACE;
                arg = static_cast<int32_t>(st->traces.size() / SE_LIVE_TRACE_DESC_LEN);
                payload = st->traces;
                st->tracesDirty = false;
            }
            else if (!st->writes.Empty())
            {
                // Ship one poke: payload = address(4) + bytes; arg = byte count.
                payload = st->writes.Pop();
                verb = SE_LIVE_VERB_WRITE;
                arg = static_cast<int32_t>(payload.size() >= 4 ? payload.size() - 4 : 0);
            }
            else if (!st->soundWrites.Empty())
            {
                // Ship one sound-RAM poke: payload = offset(4) + bytes; arg = byte count.
                payload = st->soundWrites.Pop();
                verb = SE_LIVE_VERB_WRITESND;
                arg = static_cast<int32_t>(payload.size() >= 4 ? payload.size() - 4 : 0);
            }
            else if (st->pending != Ctl::None)
            {
                switch (st->pending)
                {
                    case Ctl::Pause:  verb = SE_LIVE_VERB_PAUSE;  break;
                    case Ctl::Resume: verb = SE_LIVE_VERB_RESUME; break;
                    case Ctl::Step:
                        verb = SE_LIVE_VERB_STEP;
                        arg = st->stepFrames;
                        shippedStep = true;
                        shippedStepSeq = st->stepsPosted;   // covers every post folded into this one
                        break;
                    case Ctl::StepInsn: verb = SE_LIVE_VERB_ISTEP; arg = st->stepInsns; break;
                    default: break;
                }
                st->pending = Ctl::None;
                st->stepFrames = 0;
                st->stepInsns = 0;
            }
            else
            {
                // Inject controller input when a button is held or the mask changed
                // since the last send (covers the release edge and a non-latching
                // glue). INP still returns a full snapshot, so we lose no frame data.
                const uint32_t inp = st->inputState.load();
                if (inp != 0 || inp != st->lastInputSent)
                {
                    verb = SE_LIVE_VERB_INPUT;
                    arg = static_cast<int32_t>(inp);
                    st->lastInputSent = inp;
                }
            }
        }

        // A plain GET carries our last-seen frame so the server serves the next unseen one
        // (gap-free). Control verbs keep their own arg and just return the latest snapshot.
        // Compared by content, like MinVerFor and the server's own dispatch: `verb` is a
        // const char*, so == would compare addresses and only work while the compiler pools
        // identical literals. If it ever stopped, this would silently send arg 0 on every
        // GET and the server would re-serve frame 0's successor forever.
        if (std::memcmp(verb, SE_LIVE_VERB_GET, SE_LIVE_VERB_LEN) == 0)
        {
            arg = static_cast<int32_t>(st->lastSeenFrame);
        }

        auto fresh = std::make_shared<LiveSnapshot>();
        LiveSnapshot& snap = *fresh;
        bool paused = false;
        uint64_t frame = 0;
        uint32_t sver = 0;
        StopInfo stop;
        std::vector<LiveEvent> events;
        LiveCallStacks callStacks;
        LiveKeyMap keyMap;
        std::vector<std::string> logLines;
        std::vector<LiveStateBlock> stateBlocks;
        LiveEmuSlots emuSlots;

        // One version gate, at the only point a request leaves. A server that does not know a
        // verb ignores it and never consumes the attached payload, so its next reply is read
        // from the middle of our bytes and the session desyncs (see SeLiveProtocol.h). Fall back
        // to a plain GET and drop the payload instead. serverVersion is 0 until the first
        // exchange has answered, and "unknown" counts as too old -- the alternative is shipping
        // the payload to find out.
        if (st->serverVersion.load() < MinVerFor(verb))
        {
            verb = SE_LIVE_VERB_GET;
            arg = 0;
            payload.clear();
        }
        if (!ReadSnapshot(conn, verb, arg, payload.data(), payload.size(),
                          snap, paused, frame, sver, stop, events, callStacks, keyMap,
                          logLines, stateBlocks, emuSlots))
        {
            // Whatever was queued belonged to this connection, and it is gone.
            ForgetConnection(st);
            ConnClose(conn);   // will reconnect next iteration
            SleepWhileRunning(st, 100);
            continue;
        }
        snap.paused = paused;
        snap.frame = frame;
        snap.stop = stop;
        snap.callStacks = std::move(callStacks);

        const bool firstOnConnection = !handshaken;
        if (firstOnConnection)
        {
            handshaken = true;
            // Everything still queued for the consumer was produced by the PREVIOUS connection's
            // emulator. The client resets its history when it sees the new generation and would
            // otherwise drain these straight back into it.
            {
                std::lock_guard<std::mutex> lk(st->evMtx);
                st->events.clear();
            }
            {
                std::lock_guard<std::mutex> lk(st->stateMtx);
                st->stateBlocks.Clear();
            }
            st->serverVersion.store(sver);   // known before 'connected' lets any version-gated verb out
        }
        if (keyMap.valid)
        {
            std::lock_guard<std::mutex> lk(st->kmMtx);
            std::memcpy(st->keyMap, keyMap.k, sizeof(st->keyMap));
            st->keyMapValid = true;
        }
        {
            std::lock_guard<std::mutex> lk(st->ctlMtx);
            if (emuSlots.valid)
            {
                std::memcpy(st->emuSlotPresent, emuSlots.present, sizeof(st->emuSlotPresent));
                std::memcpy(st->emuSlotMtime, emuSlots.mtime, sizeof(st->emuSlotMtime));
                st->emuSlotsValid = true;
            }
        }
        {
            // Snapshot and generation together: a reader that sees the new generation is
            // guaranteed (it takes this lock to read the snapshot) to capture the new data.
            std::lock_guard<std::mutex> lk(st->mtx);
            st->front = fresh;
            if (firstOnConnection) { st->connGeneration.fetch_add(1); }
        }
        if (shippedStep)
        {
            // After the snapshot is visible: this reply carries the step's effect (granted
            // frames, or their completion), so "answered" must never be observable before it.
            std::lock_guard<std::mutex> lk(st->ctlMtx);
            st->stepsAnswered = shippedStepSeq;
        }
        if (firstOnConnection)
        {
            // Only now may callbacks queue work: after the client can see this emulator's
            // generation, so an edit it issues from here on is one it made knowing which
            // emulator it is talking to.
            std::lock_guard<std::mutex> lk(st->ctlMtx);
            // A fresh server starts with rewind capture ON (so a pre-v18 client keeps the old
            // behavior), so an "off" setting has to be restated or this emulator would spend a
            // full savestate per frame on a feature the user switched off. Unconditional: the
            // value we want is the value this connection has not been told.
            st->rewindDirty = true;
            st->connected = true;
        }
        if (!events.empty())
        {
            std::lock_guard<std::mutex> lk(st->evMtx);
            for (LiveEvent& e : events) st->events.push_back(e);
            while (st->events.size() > 4096) st->events.pop_front();   // bound the queue
        }
        if (!logLines.empty())
        {
            std::lock_guard<std::mutex> lk(st->logMtx);
            for (std::string& s : logLines) st->logLines.push_back(std::move(s));
            while (st->logLines.size() > 4096) st->logLines.pop_front();   // bound the queue
        }
        if (!stateBlocks.empty())
        {
            std::lock_guard<std::mutex> lk(st->stateMtx);
            // Drop-oldest rather than refuse: these are a rolling rewind history, so losing the
            // oldest shortens how far back the user can scrub, while refusing the newest would
            // put a hole in the middle of it. The client already drops blocks for frames it no
            // longer holds.
            for (LiveStateBlock& b : stateBlocks) st->stateBlocks.PushEvicting(std::move(b));
            st->stateBlocks.TrimTo(kMaxQueuedStateBytes);
        }
        st->lastSeenFrame = static_cast<uint32_t>(frame);   // advance the gap-free cursor
        if (shippedLoad)
        {
            // The server just rewound to frame N (and cleared its ring); ignore the pre-load
            // frame this LST returned and resync the cursor to N so we fetch N+1 onward.
            st->lastSeenFrame = loadResyncFrame;
        }
        SleepWhileRunning(st, 8);   // ~120 Hz cap
    }
    // Closing: if we left the emulator paused/stepped, release it before dropping
    // the connection so Yabause never stays frozen after the debugger disconnects.
    // Done here on the poll thread (after running went false) so it's race-free. Bounded and
    // not cancellable by 'running' (which is already clear): a short idle limit instead, because
    // an emulator that has stopped answering must not be able to hold up shutdown.
    if (conn.ok() && handshaken && st->pausedByUs.load())
    {
        conn.running = nullptr;
        conn.idleMs = 500;
        LiveSnapshot tmp;
        bool p = false;
        uint64_t fr = 0;
        uint32_t sv = 0;
        StopInfo si;
        std::vector<LiveEvent> ev;
        LiveCallStacks cs;
        LiveKeyMap km;
        std::vector<std::string> lg;
        std::vector<LiveStateBlock> sb;
        LiveEmuSlots es;
        ReadSnapshot(conn, SE_LIVE_VERB_RESUME, 0, nullptr, 0, tmp, p, fr, sv, si, ev, cs, km, lg, sb, es);  // best-effort
    }
    ConnClose(conn);
}

size_t CopyRegion(const std::vector<uint8_t>& buf, uint32_t off, void* dst, size_t size)
{
    if (off >= buf.size())
    {
        return 0;
    }
    const size_t avail = buf.size() - off;
    const size_t n = size < avail ? size : avail;
    std::memcpy(dst, buf.data() + off, n);
    return n;
}

/* ---- se_data_source callbacks ---- */
LiveState* St(void* user) { return static_cast<LiveState*>(user); }

// Every read is served from ONE immutable snapshot (the capture's pinned one, else the newest),
// copied out of the pointer rather than under a lock held for the memcpy: the poll thread can
// publish a new frame at any moment, and a reader that re-locked per call could stitch a
// region from one frame onto registers from the next.
size_t CbVdp1Vram(void* u, uint32_t off, void* dst, size_t size)
{
    SnapshotPtr s = CurrentSnapshot(St(u));
    return s ? CopyRegion(s->vdp1Vram, off, dst, size) : 0;
}
size_t CbVdp2Vram(void* u, uint32_t off, void* dst, size_t size)
{
    SnapshotPtr s = CurrentSnapshot(St(u));
    return s ? CopyRegion(s->vdp2Vram, off, dst, size) : 0;
}
size_t CbCram(void* u, uint32_t off, void* dst, size_t size)
{
    SnapshotPtr s = CurrentSnapshot(St(u));
    return s ? CopyRegion(s->cram, off, dst, size) : 0;
}
size_t CbMainRam(void* u, uint32_t address, void* dst, size_t size)
{
    SnapshotPtr s = CurrentSnapshot(St(u));
    if (!s) { return 0; }
    if (address >= 0x06000000u)
    {
        return CopyRegion(s->wramHigh, address - 0x06000000u, dst, size);
    }
    if (address >= 0x00200000u)
    {
        return CopyRegion(s->wramLow, address - 0x00200000u, dst, size);
    }
    return 0;
}
size_t CbVdp1Fb(void* u, uint32_t off, void* dst, size_t size)
{
    SnapshotPtr s = CurrentSnapshot(St(u));
    return s ? CopyRegion(s->vdp1Fb, off, dst, size) : 0;
}
size_t CbSoundRam(void* u, uint32_t off, void* dst, size_t size)
{
    SnapshotPtr s = CurrentSnapshot(St(u));
    return s ? CopyRegion(s->soundRam, off, dst, size) : 0;
}
int CbScspSlots(void* u, se_scsp_slot out[SE_SCSP_SLOT_COUNT])
{
    SnapshotPtr s = CurrentSnapshot(St(u));
    if (!s) { return 0; }
    int n = static_cast<int>(s->scspSlots.size());
    if (n > SE_SCSP_SLOT_COUNT) n = SE_SCSP_SLOT_COUNT;
    for (int i = 0; i < n; ++i) out[i] = s->scspSlots[i];
    return n;
}
int CbCdStatus(void* u, se_cd_status* out)
{
    SnapshotPtr s = CurrentSnapshot(St(u));
    if (!s || !s->hasCdStatus) return 0;
    *out = s->cdStatus;
    return 1;
}

// Bracket one capture: pin the newest snapshot for every read this thread makes until
// end_capture. Nested pairs share the outermost pin.
void CbBeginCapture(void* u)
{
    LiveState* st = St(u);
    if (gPinned.id == st->id && gPinned.depth > 0) { ++gPinned.depth; return; }
    gPinned.snap = Newest(st);
    gPinned.id = st->id;
    gPinned.depth = 1;
}

void CbEndCapture(void* u)
{
    LiveState* st = St(u);
    if (gPinned.id != st->id || gPinned.depth == 0) { return; }
    if (--gPinned.depth > 0) { return; }
    // What the display now shows -- the base the capture-pending check compares against, and
    // what stop info and the call stack are read from.
    gLastCaptured.id = st->id;
    gLastCaptured.snap = std::move(gPinned.snap);
    gPinned.snap.reset();
    gPinned.id = 0;
}

// A poke payload: the destination (u32 LE) followed by the raw bytes. WRM reads it as a bus
// address, WRS as a sound-RAM offset; the framing is the same.
std::vector<uint8_t> BuildPoke(uint32_t dest, const void* src, size_t size)
{
    std::vector<uint8_t> payload;
    payload.reserve(4 + size);
    PushU32LE(payload, dest);
    const uint8_t* p = static_cast<const uint8_t*>(src);
    payload.insert(payload.end(), p, p + size);
    return payload;
}

// A full queue returns 0 -- the ABI's "wrote no bytes" -- rather than dropping an older poke,
// which would discard an edit the user already watched go through. Note what 0 does and does
// not buy: the Hex Editor leaves the byte unchanged when a write comes back short, so the edit
// visibly does not stick, but nothing retries it and nothing says why. Reaching the budget at
// all takes a producer far beyond any real one (the poll thread ships ~125 pokes/s, so 8 MiB of
// single-byte edits would take hours to queue), so this is a backstop on memory, not a path the
// UI is expected to travel.
//
// So does a write while no emulator is attached: there is nothing to apply it to, and queueing
// it would apply it to whichever one answers next (see ForgetConnection).
size_t CbWriteMainRam(void* u, uint32_t address, const void* src, size_t size)
{
    if (!src || size == 0) return 0;
    LiveState* st = St(u);
    std::vector<uint8_t> payload = BuildPoke(address, src, size);
    std::lock_guard<std::mutex> lk(st->ctlMtx);
    if (!st->connected) return 0;
    return st->writes.Push(std::move(payload), kMaxPokeBytes) ? size : 0;
}

size_t CbWriteSoundRam(void* u, uint32_t offset, const void* src, size_t size)
{
    if (!src || size == 0) return 0;
    LiveState* st = St(u);
    std::vector<uint8_t> payload = BuildPoke(offset, src, size);   // shipped as WRS
    std::lock_guard<std::mutex> lk(st->ctlMtx);
    if (!st->connected) return 0;
    return st->soundWrites.Push(std::move(payload), kMaxPokeBytes) ? size : 0;
}

// VDP memory poke: map the region-local offset to its Saturn bus address and ship it as a
// WRM (the emulator glue's CheatMemWrite handles any bus region, VRAM/CRAM included). This
// is how a paused VDP1/VDP2 VRAM, CRAM, or framebuffer edit persists in the running game.
size_t CbWriteVram(void* u, se_vram_kind kind, uint32_t offset, const void* src, size_t size)
{
    uint32_t base;
    switch (kind)
    {
        case SE_VRAM_KIND_VDP1_VRAM: base = 0x05C00000u; break;
        case SE_VRAM_KIND_VDP1_FB:   base = 0x05C80000u; break;
        case SE_VRAM_KIND_VDP2_VRAM: base = 0x05E00000u; break;
        case SE_VRAM_KIND_CRAM:      base = 0x05F00000u; break;
        default: return 0;   // work/sound RAM go through their own callbacks
    }
    return CbWriteMainRam(u, base + offset, src, size);
}

// Rewind (v16): pack the LST wire payload (frame(4) + edits_len(4) + edits + state) and hand
// it to the poll thread to ship as one atomic command. The emulator restores + applies the
// edits + resumes at its frame gate. Returns 0 (success) — best-effort/async like the pokes.
int CbLoadState(void* u, uint64_t frame, const void* state, size_t state_len,
                const void* edits, size_t edits_len)
{
    LiveState* st = St(u);
    // Validate the inputs before building anything. The payload is frame(4) + edits_len(4) +
    // edits + state, and PollLoop casts its *total* size to the int32_t command arg, so:
    //  - `frame` is its own 32-bit field;
    //  - a nonzero length with a null pointer would ship an edits_len (or a claimed total) the
    //    bytes don't back — the server would then read the state blob as edits;
    //  - the aggregate (8 + edits + state) must fit INT32_MAX, checked stepwise so the sum can't
    //    itself wrap for extreme size_t values before the comparison.
    // A real state+edits payload is a few MB, far below these limits.
    if (frame > 0xFFFFFFFFull) return -1;
    // Refuse before building anything if the server cannot accept LST. The frontend guards this
    // too, but the guard cannot live only there: this is a public ABI entry point, and shipping
    // LST to a pre-v16 server desyncs the connection rather than failing politely. 0 means the
    // version is not known yet (no exchange has completed), which has to count as "no".
    if (st->serverVersion.load() < SE_LIVE_MINVER_LOADSTATE) return -1;
    if ((edits_len && !edits) || (state_len && !state)) return -1;
    if (edits_len > 0x7FFFFFFFull - 8) return -1;
    if (state_len > 0x7FFFFFFFull - 8 - edits_len) return -1;
    std::vector<uint8_t> payload;
    payload.reserve(8 + edits_len + state_len);
    PushU32LE(payload, static_cast<uint32_t>(frame));
    PushU32LE(payload, static_cast<uint32_t>(edits_len));
    if (edits && edits_len)
    {
        const uint8_t* e = static_cast<const uint8_t*>(edits);
        payload.insert(payload.end(), e, e + edits_len);
    }
    if (state && state_len)
    {
        const uint8_t* s = static_cast<const uint8_t*>(state);
        payload.insert(payload.end(), s, s + state_len);
    }
    std::lock_guard<std::mutex> lk(st->ctlMtx);
    if (!st->connected) return -1;   // no emulator to restore: don't hold it for the next one
    st->loadPayload = std::move(payload);
    st->loadFrame = static_cast<uint32_t>(frame);
    st->loadDirty = true;
    st->pausedByUs.store(false);   // the rewind resumes the emulator itself
    return 0;
}

int CbSh2Regs(void* u, int cpu, se_sh2_regs* out)
{
    if (cpu < 0 || cpu > 1 || !out) { return 0; }
    SnapshotPtr s = CurrentSnapshot(St(u));
    if (!s || !s->hasSh2[cpu]) { return 0; }   // server predates v5 / no data yet
    *out = s->sh2[cpu];
    return 1;
}

uint16_t CbVdp1Reg(void* u, uint32_t reg)
{
    SnapshotPtr s = CurrentSnapshot(St(u));
    return s ? sedrv::ReadReg16(s->vdp1Regs, reg) : 0;
}
uint16_t CbVdp2Reg(void* u, uint32_t reg)
{
    SnapshotPtr s = CurrentSnapshot(St(u));
    return s ? sedrv::ReadReg16(s->vdp2Regs, reg) : 0;
}

// ---- Frame control. The UI thread posts a command; the poll thread sends it
//      over the shared connection on its next cycle (see PollLoop). ----
bool PostCmd(LiveState* st, Ctl cmd, int32_t frames)
{
    std::lock_guard<std::mutex> lk(st->ctlMtx);
    if (!st->connected) { return false; }   // nothing to pause or step; don't queue it for the next one

    // Track whether the emulator is currently held by us: pause/step halt it,
    // resume releases it. The poll thread uses this to resume on close.
    if (cmd == Ctl::Pause || cmd == Ctl::Step || cmd == Ctl::StepInsn) { st->pausedByUs.store(true); }
    else if (cmd == Ctl::Resume)                                       { st->pausedByUs.store(false); }

    if (cmd == Ctl::Step) { ++st->stepsPosted; }
    if (cmd == Ctl::Step && st->pending == Ctl::Step)
    {
        st->stepFrames += frames;   // accumulate rapid presses
    }
    else if (cmd == Ctl::StepInsn && st->pending == Ctl::StepInsn)
    {
        st->stepInsns += frames;    // accumulate rapid instruction-step presses
    }
    else
    {
        st->pending = cmd;
        st->stepFrames = (cmd == Ctl::Step) ? frames : 0;
        st->stepInsns  = (cmd == Ctl::StepInsn) ? frames : 0;
    }
    return true;
}

int CbFramePause(void* u)
{
    return PostCmd(St(u), Ctl::Pause, 0) ? 0 : -1;
}
int CbFrameStep(void* u, int32_t frames)
{
    // By the seam's contract, frames <= 0 means "resume" (run free).
    if (frames <= 0) { return PostCmd(St(u), Ctl::Resume, 0) ? 0 : -1; }
    return PostCmd(St(u), Ctl::Step, frames) ? 0 : -1;
}
// The frame the (pinned) snapshot is of -- not the newest the emulator has reached.
uint64_t CbFrameNumber(void* u)
{
    SnapshotPtr s = CurrentSnapshot(St(u));
    return s ? s->frame : 0;
}

void CbClose(void* u)
{
    LiveState* st = St(u);
    if (!st) { return; }
    st->running.store(false);
    // Bounded: every wait in the poll thread re-checks 'running' within ~100 ms, and the final
    // resume is capped at a short idle limit, so an emulator that has stopped answering cannot
    // hold this up.
    if (st->thread.joinable()) { st->thread.join(); }
    if (gPinned.id == st->id) { gPinned = ThreadPin(); }
    if (gLastCaptured.id == st->id) { gLastCaptured = ThreadPin(); }   // this thread's; don't pin 2.7 MB past the source
    delete st;
}

}  // namespace

// Everything below is a seam crossing, so no exception may leave it: see SeGuard.h. Two shapes
// are reachable here rather than hypothetical -- se_live_open allocates a LiveState and starts a
// thread, and every other entry point takes a lock, which throws std::system_error when the OS
// refuses it. The control verbs return nothing, so a failure there means the request did not
// happen, the same as failing their argument checks.
extern "C" se_result se_live_open(const char* endpoint, se_data_source* out)
{
    if (!out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));

    return se::Guard(SE_ERR_NO_MEMORY, [&]() -> se_result
    {
        const char* ep = endpoint;
        if (!ep || !ep[0])
        {
    #if defined(_WIN32)
            ep = SE_LIVE_DEFAULT_PIPE_NAME;
    #elif defined(__EMSCRIPTEN__)
            ep = SE_LIVE_DEFAULT_TCP_ENDPOINT;   // browser: WebSocket->TCP bridge
    #else
            ep = SE_LIVE_DEFAULT_SOCK_PATH;
    #endif
        }

        // Fail fast if the emulator isn't reachable right now.
        Conn probe;
        if (!ConnOpen(probe, ep))
        {
            return SE_ERR_IO;
        }
        ConnClose(probe);

        // CbClose is what knows how to stop the poll thread, so it is also the deleter: if
        // assigning the endpoint or starting the thread throws, this unwinds through the same
        // shutdown a normal close takes rather than leaking a LiveState with a thread in it.
        std::unique_ptr<LiveState, void (*)(LiveState*)> st(
            new (std::nothrow) LiveState(), [](LiveState* p) { CbClose(p); });
        if (!st)
        {
            return SE_ERR_NO_DATA;
        }
        st->endpoint = ep;
        st->running.store(true);
        st->thread = std::thread(PollLoop, st.get());

        out->abi_version = SE_ABI_VERSION;
        // SE_CAP_SOUND_RAM is advertised unconditionally, like the other version-gated caps:
        // against a pre-v13 server the sound-RAM snapshot stays empty and reads return 0.
        out->capabilities = SE_CAP_VDP1_VRAM | SE_CAP_VDP2_VRAM | SE_CAP_CRAM |
                            SE_CAP_VDP1_REGS | SE_CAP_VDP2_REGS | SE_CAP_MAIN_RAM |
                            SE_CAP_VDP1_FB | SE_CAP_FRAME_STEP | SE_CAP_SH2_REGS |
                            SE_CAP_MEM_WRITE | SE_CAP_SOUND_RAM | SE_CAP_SCSP_SLOTS |
                            SE_CAP_CD_STATUS | SE_CAP_STATE_REWIND;
        out->user = st.get();
        out->read_vdp1_vram = CbVdp1Vram;
        out->read_vdp2_vram = CbVdp2Vram;
        out->read_cram      = CbCram;
        out->read_main_ram  = CbMainRam;
        out->write_main_ram = CbWriteMainRam;
        out->read_sound_ram = CbSoundRam;
        out->write_sound_ram = CbWriteSoundRam;
        out->write_vram     = CbWriteVram;
        out->load_state     = CbLoadState;
        out->begin_capture  = CbBeginCapture;
        out->end_capture    = CbEndCapture;
        out->read_scsp_slots = CbScspSlots;
        out->read_cd_status = CbCdStatus;
        out->read_vdp1_fb   = CbVdp1Fb;
        out->read_vdp1_reg  = CbVdp1Reg;
        out->read_vdp2_reg  = CbVdp2Reg;
        out->read_sh2_regs  = CbSh2Regs;
        out->frame_pause    = CbFramePause;
        out->frame_step     = CbFrameStep;
        out->frame_number   = CbFrameNumber;
        out->close          = CbClose;
        st.release();   // the data source owns it now; se_create calls close on destroy
        return SE_OK;
    });
}

extern "C" uint32_t se_live_server_version(const se_data_source* ds)
{
    return se::Guard(0u, [&]() -> uint32_t
    {
        // Only meaningful for a data source we produced (identified by our close cb).
        if (!ds || !ds->user || ds->close != CbClose)
        {
            return 0;
        }
        return St(ds->user)->serverVersion.load();
    });
}

extern "C" uint32_t se_live_connection_generation(const se_data_source* ds)
{
    if (!ds || !ds->user || ds->close != CbClose) { return 0; }
    return se::Guard(0u, [&]() -> uint32_t
    {
        return St(ds->user)->connGeneration.load();
    });
}

extern "C" int se_live_restore_state(const se_data_source* ds, uint32_t* done, uint32_t* failed)
{
    if (!ds || !ds->user || ds->close != CbClose || !done || !failed) { return 0; }
    return se::Guard(0, [&]() -> int
    {
        // The NEWEST snapshot, not the pinned/displayed one: the caller reads this before
        // starting a capture and relies on the capture being at least as new as what it read.
        SnapshotPtr snap = Newest(St(ds->user));
        if (!snap || !snap->valid || !snap->hasRestoreInfo) { return 0; }   // pre-v19 server
        *done = snap->restoreDone;
        *failed = snap->restoreFailed;
        return 1;
    });
}

extern "C" int se_live_capture_pending(const se_data_source* ds)
{
    if (!ds || !ds->user || ds->close != CbClose) { return -1; }
    return se::Guard(-1, [&]() -> int
    {
        LiveState* st = St(ds->user);
        bool queuedStep;
        {
            std::lock_guard<std::mutex> lk(st->ctlMtx);
            queuedStep = st->stepsPosted != st->stepsAnswered;
        }
        SnapshotPtr newest = Newest(st);
        if (!newest || !newest->valid || !newest->hasStepInfo) { return -1; }   // pre-v20: can't say
        // A step the emulator has not yet been told about, or has been told about and not yet
        // published: the display must keep following the stream.
        if (queuedStep || newest->stepPending > 0) { return 1; }
        // Published but not yet shown: the server's ring holds frames newer than the one this
        // reply served (a gap-free GET lags), or the newest snapshot is a different frame from
        // the one the display was captured from.
        if (static_cast<uint32_t>(newest->frame) != newest->latestFrame) { return 1; }
        SnapshotPtr shown = DisplayedSnapshot(st);
        if (!shown || shown->frame != newest->frame) { return 1; }
        return 0;
    });
}

extern "C" uint32_t se_live_drain_state_blocks(const se_data_source* ds,
                                               se_live_state_block_cb cb, void* user)
{
    if (!ds || !ds->user || ds->close != CbClose || !cb) { return 0; }
    return se::Guard(0u, [&]() -> uint32_t
    {
        LiveState* st = St(ds->user);
        std::deque<LiveStateBlock> local;
        {
            std::lock_guard<std::mutex> lk(st->stateMtx);
            st->stateBlocks.Drain(local);
        }
        for (const LiveStateBlock& b : local)
        {
            cb(user, b.kind, b.frame, b.base, b.fullLen,
               b.payload.empty() ? nullptr : b.payload.data(),
               static_cast<uint32_t>(b.payload.size()));
        }
        return static_cast<uint32_t>(local.size());
    });
}

extern "C" void se_live_step_insn(const se_data_source* ds, uint32_t count)
{
    if (!ds || !ds->user || ds->close != CbClose) { return; }
    se::GuardVoid([&]
    {
        // Single-step the halted CPU `count` instructions (IST). The poll thread ships it on
        // its next cycle; the server steps whichever CPU the stop latched.
        PostCmd(St(ds->user), Ctl::StepInsn, static_cast<int32_t>(count < 1 ? 1 : count));
    });
}

// Copy a descriptor blob handed to us across the C ABI. False if the (pointer, count) pair is
// not one we can act on, in which case 'out' is left as it was.
//
// Every part of that pair comes from outside, so none of it can be assumed. The old form built
// a vector range straight from (descs, descs + count * descLen), which is undefined for a null
// pointer with a nonzero count, wraps when the product exceeds a 32-bit size_t, and sizes an
// allocation from a number the caller picked. Checking the count against the protocol maximum
// first is also what makes the multiply safe: bounded count times a small constant cannot
// overflow. A rejected call leaves the previously installed set alone rather than clearing it --
// a malformed request should not silently remove the user's breakpoints.
static bool CopyDescs(const uint8_t* descs, uint32_t count, uint32_t descLen, uint32_t maxDescs,
                      std::vector<uint8_t>& out)
{
    if (count > maxDescs) { return false; }
    if (count != 0 && !descs) { return false; }
    return se::Guard(false, [&]
    {
        out.assign(descs, descs + static_cast<size_t>(count) * descLen);
        return true;
    });
}

extern "C" void se_live_set_breakpoints(const se_data_source* ds,
                                        const uint8_t* descs, uint32_t count)
{
    if (!ds || !ds->user || ds->close != CbClose) { return; }
    se::GuardVoid([&]
    {
        LiveState* st = St(ds->user);
        std::lock_guard<std::mutex> lk(st->ctlMtx);
        if (CopyDescs(descs, count, SE_LIVE_BKPT_DESC_LEN, SE_LIVE_MAX_BKPT_DESCS, st->bkpts))
        {
            st->bkptsDirty = true;   // poll thread ships it on its next cycle
        }
    });
}

extern "C" void se_live_send_input(const se_data_source* ds, uint32_t port, uint32_t buttons)
{
    if (!ds || !ds->user || ds->close != CbClose) { return; }
    se::GuardVoid([&]
    {
        // Pack port + SE_PAD_* mask; the poll thread sends it (INP) on its next cycle.
        const uint32_t packed = ((port & 0xFFFFu) << 16) | (buttons & SE_PAD_ALL);
        St(ds->user)->inputState.store(packed);
    });
}

extern "C" uint32_t se_live_emu_slots(const se_data_source* ds, uint8_t* present,
                                     uint64_t* mtime, uint32_t max)
{
    if (!ds || !ds->user || ds->close != CbClose || !present || !max) { return 0; }
    return se::Guard(0u, [&]() -> uint32_t
    {
        LiveState* st = St(ds->user);
        std::lock_guard<std::mutex> lk(st->ctlMtx);
        if (!st->emuSlotsValid) { return 0; }   // pre-v17 server, or no slot hook in that build
        uint32_t n = 0;
        for (; n < max && n < SE_LIVE_EMU_SLOTS; ++n)
        {
            present[n] = st->emuSlotPresent[n];
            if (mtime) mtime[n] = st->emuSlotMtime[n];
        }
        return n;
    });
}

extern "C" void se_live_emu_load_slot(const se_data_source* ds, uint32_t slot)
{
    if (!ds || !ds->user || ds->close != CbClose || slot >= SE_LIVE_EMU_SLOTS) { return; }
    se::GuardVoid([&]
    {
        LiveState* st = St(ds->user);
        std::lock_guard<std::mutex> lk(st->ctlMtx);
        st->emuLoadSlot = static_cast<int>(slot) + 1;   // poll thread ships ELS next cycle
    });
}

extern "C" void se_live_set_rewind_enabled(const se_data_source* ds, int enabled)
{
    if (!ds || !ds->user || ds->close != CbClose) { return; }
    se::GuardVoid([&]
    {
        LiveState* st = St(ds->user);
        std::lock_guard<std::mutex> lk(st->ctlMtx);
        const bool want = (enabled != 0);
        // Only on a change -- but note this never CLEARS rewindDirty, so a setter call carrying
        // the value already in flight cannot swallow the resend the reconnect path queued.
        if (want != st->rewindWanted) { st->rewindWanted = want; st->rewindDirty = true; }
    });
}

extern "C" void se_live_set_tracepoints(const se_data_source* ds,
                                        const uint8_t* descs, uint32_t count)
{
    if (!ds || !ds->user || ds->close != CbClose) { return; }
    se::GuardVoid([&]
    {
        LiveState* st = St(ds->user);
        std::lock_guard<std::mutex> lk(st->ctlMtx);
        if (CopyDescs(descs, count, SE_LIVE_TRACE_DESC_LEN, SE_LIVE_MAX_TRACE_DESCS, st->traces))
        {
            st->tracesDirty = true;   // poll thread ships it (TRC) on its next cycle
        }
    });
}

extern "C" uint32_t se_live_poll_events(const se_data_source* ds,
                                        se_live_event* out, uint32_t max)
{
    if (!ds || !ds->user || ds->close != CbClose || !out || !max) { return 0; }
    return se::Guard(0u, [&]() -> uint32_t
    {
        LiveState* st = St(ds->user);
        std::lock_guard<std::mutex> lk(st->evMtx);
        uint32_t n = 0;
        while (n < max && !st->events.empty())
        {
            const LiveEvent& e = st->events.front();
            out[n].id = e.id;
            out[n].cpu = e.cpu;
            out[n].frame = e.frame;
            std::memcpy(out[n].regs, e.regs, sizeof(out[n].regs));
            st->events.pop_front();
            ++n;
        }
        return n;
    });
}

extern "C" uint32_t se_live_poll_callstack(const se_data_source* ds, int cpu,
                                           se_live_call_frame* out, uint32_t max)
{
    if (!ds || !ds->user || ds->close != CbClose || !out || !max) { return 0; }
    return se::Guard(0u, [&]() -> uint32_t
    {
        const int c = (cpu == 1) ? 1 : 0;
        // The stack that belongs to the frame the display shows, not the newest the poll thread
        // has since replaced it with.
        SnapshotPtr snap = DisplayedSnapshot(St(ds->user));
        if (!snap) { return 0; }
        const std::vector<LiveCallFrame>& src = snap->callStacks.cpu[c];
        uint32_t n = 0;
        for (; n < max && n < src.size(); ++n)
        {
            out[n].call_site = src[n].callSite;
            out[n].func      = src[n].func;
            out[n].ret       = src[n].ret;
            out[n].sp        = src[n].sp;
            out[n].cycle     = src[n].cycle;
            out[n].frame_no  = src[n].frameNo;
        }
        return n;
    });
}

extern "C" uint32_t se_live_poll_keymap(const se_data_source* ds, uint32_t port,
                                        int32_t* out, uint32_t max)
{
    if (!ds || !ds->user || ds->close != CbClose || !out || !max) { return 0; }
    return se::Guard(0u, [&]() -> uint32_t
    {
        if (port >= (uint32_t)SE_LIVE_KEYMAP_PORTS) { return 0; }
        LiveState* st = St(ds->user);
        std::lock_guard<std::mutex> lk(st->kmMtx);
        if (!st->keyMapValid) { return 0; }   // no v10+ block seen yet
        uint32_t n = 0;
        for (; n < max && n < (uint32_t)SE_LIVE_KEYMAP_BUTTONS; ++n)
            out[n] = st->keyMap[port][n];
        return n;
    });
}

extern "C" uint32_t se_live_poll_log(const se_data_source* ds, char* out,
                                     uint32_t lineLen, uint32_t maxLines)
{
    if (!ds || !ds->user || ds->close != CbClose || !out || !lineLen || !maxLines) { return 0; }
    return se::Guard(0u, [&]() -> uint32_t
    {
        LiveState* st = St(ds->user);
        std::lock_guard<std::mutex> lk(st->logMtx);
        uint32_t n = 0;
        while (n < maxLines && !st->logLines.empty())
        {
            const std::string& s = st->logLines.front();
            char* dst = out + (size_t)n * lineLen;
            uint32_t i = 0;
            for (; i + 1 < lineLen && i < s.size(); ++i) dst[i] = s[i];
            dst[i] = 0;
            st->logLines.pop_front();
            ++n;
        }
        return n;
    });
}

extern "C" int se_live_get_stop(const se_data_source* ds, uint32_t* reason,
                                uint32_t* cpu, uint32_t* pc)
{
    if (!ds || !ds->user || ds->close != CbClose) { return 0; }
    return se::Guard(0, [&]() -> int
    {
        // Paired with the displayed frame: a stop reported for a later frame than the one on
        // screen would point the disassembly at a PC the shown registers never held.
        SnapshotPtr snap = DisplayedSnapshot(St(ds->user));
        const StopInfo stop = snap ? snap->stop : StopInfo{};
        if (reason) { *reason = stop.reason; }
        if (cpu)    { *cpu = stop.cpu; }
        if (pc)     { *pc = stop.pc; }
        return stop.reason != SE_LIVE_STOP_NONE ? 1 : 0;
    });
}
