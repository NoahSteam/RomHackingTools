// LiveDriver — see LiveDriver.h. A background thread polls the patched Yabause
// for a fresh VDP snapshot; the se_data_source callbacks serve the latest one.

#include "LiveDriver.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "SaturnStateShared.h"
#include "SeLiveProtocol.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdlib>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace
{

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
    se_sh2_regs          sh2[2] = {};        // [0] master, [1] slave (v5+)
    bool                 hasSh2[2] = { false, false };
    bool                 valid = false;
};

// Pending control command the UI thread hands to the poll thread (which owns the
// single server connection). Best-effort: the poll thread drains it within one
// cycle (~8 ms). Steps accumulate so rapid presses aren't lost.
enum class Ctl { None, Pause, Resume, Step };

struct LiveState
{
    std::string       endpoint;
    std::thread       thread;
    std::atomic<bool> running{false};
    std::mutex        mtx;           // guards 'front'
    LiveSnapshot      front;

    // Frame control (SE_CAP_FRAME_STEP). Updated from each snapshot's control
    // block; the callbacks post a command for the poll thread to send.
    std::atomic<uint64_t> frameNumber{0};
    std::atomic<bool>     paused{false};
    std::atomic<uint32_t> serverVersion{0};   // protocol version last seen from server
    // Last stop event from the control block (v5+): reason / cpu / pc of a
    // breakpoint hit, so the UI can jump to the halted PC.
    std::atomic<uint32_t> stopReason{0};
    std::atomic<uint32_t> stopCpu{0};
    std::atomic<uint32_t> stopPc{0};
    std::mutex            ctlMtx;    // guards pending / stepFrames / bkpts
    Ctl                   pending = Ctl::None;
    int32_t               stepFrames = 0;
    // Pending breakpoint-set sync: when the UI changes breakpoints it bumps
    // 'bkptsDirty' and stashes the descriptor blob; the poll thread ships it with
    // a BKP command on its next cycle. Each descriptor is SE_LIVE_BKPT_DESC_LEN.
    std::vector<uint8_t>  bkpts;
    bool                  bkptsDirty = false;
    // Pending work-RAM pokes from the Hex Editor. Each entry is a WRM payload:
    // address(u32 LE) + big-endian bytes. The poll thread ships one per cycle.
    std::vector<std::vector<uint8_t>> writes;
    // True once we've told the emulator to pause/step and not since resumed, so the
    // poll thread knows to release it on close (never leave Yabause paused).
    std::atomic<bool>     pausedByUs{false};
    // Controller input to inject (v7+): packed (port << 16) | SE_PAD_* mask. The poll
    // thread sends an INP whenever this is non-zero or has changed since the last one
    // sent, so a held button survives even a glue that doesn't latch, and the release
    // edge (back to 0) is always delivered.
    std::atomic<uint32_t> inputState{0};
    uint32_t              lastInputSent = 0;   // poll-thread-local (guarded by ctlMtx use)
};

/* ---- Local-socket transport (POSIX Unix socket / Windows named pipe). ---- */
struct Conn
{
#if defined(_WIN32)
    HANDLE h = INVALID_HANDLE_VALUE;
    bool ok() const { return h != INVALID_HANDLE_VALUE; }
#else
    int fd = -1;
    bool ok() const { return fd >= 0; }
#endif
};

// True for a TCP endpoint written as "tcp:host:port" (used for the web bridge,
// where the browser tunnels a normal TCP connect over a WebSocket proxy).
bool IsTcpEndpoint(const char* ep) { return ep && std::strncmp(ep, "tcp:", 4) == 0; }

#if !defined(_WIN32)
// Connect a POSIX TCP socket to "tcp:host:port". This is the path the Emscripten
// build takes (its sockets are proxied to a WebSocket bridge), and the one the
// native test harness uses; Windows native uses the named pipe instead.
bool ConnOpenTcp(Conn& c, const char* endpoint)
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
        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) { c.fd = fd; break; }
        ::close(fd);
    }
    ::freeaddrinfo(res);
    return c.fd >= 0;
}
#endif

bool ConnOpen(Conn& c, const char* endpoint)
{
#if defined(_WIN32)
    // Windows native: local named pipe. (TCP for the web bridge is POSIX-side.)
    c.h = CreateFileA(endpoint, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                      OPEN_EXISTING, 0, nullptr);
    return c.h != INVALID_HANDLE_VALUE;
#else
    if (IsTcpEndpoint(endpoint))
    {
        return ConnOpenTcp(c, endpoint);
    }
    c.fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (c.fd < 0)
    {
        return false;
    }
    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, endpoint, sizeof(addr.sun_path) - 1);
    if (::connect(c.fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        ::close(c.fd);
        c.fd = -1;
        return false;
    }
    return true;
#endif
}

void ConnClose(Conn& c)
{
#if defined(_WIN32)
    if (c.h != INVALID_HANDLE_VALUE) { CloseHandle(c.h); c.h = INVALID_HANDLE_VALUE; }
#else
    if (c.fd >= 0) { ::close(c.fd); c.fd = -1; }
#endif
}

bool ConnWrite(Conn& c, const void* data, size_t size)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < size)
    {
#if defined(_WIN32)
        DWORD n = 0;
        if (!WriteFile(c.h, p + done, static_cast<DWORD>(size - done), &n, nullptr) || n == 0)
            return false;
#else
        ssize_t n = ::send(c.fd, p + done, size - done, 0);
        if (n <= 0)
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
        if (!ReadFile(c.h, p + done, static_cast<DWORD>(size - done), &n, nullptr) || n == 0)
            return false;
#else
        ssize_t n = ::recv(c.fd, p + done, size - done, 0);
        if (n <= 0)
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
struct StopInfo { uint32_t reason = 0; uint32_t cpu = 0; uint32_t pc = 0; };

bool ReadSnapshot(Conn& c, const char* verb, int32_t arg,
                  const uint8_t* extra, size_t extraLen, LiveSnapshot& snap,
                  bool& outPaused, uint64_t& outFrame, uint32_t& outVersion,
                  StopInfo& outStop)
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

    // Control block: paused (u32 LE) + frame (u64 LE), then (v5+) stop reason/cpu/pc.
    // Absent fields default to 0 on older servers.
    outPaused = ct >= 4 && Rd32LE(ctl.data()) != 0;
    outFrame = ct >= 12 ? (static_cast<uint64_t>(Rd32LE(ctl.data() + 4)) |
                           (static_cast<uint64_t>(Rd32LE(ctl.data() + 8)) << 32))
                        : 0;
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
    // watches and the SH-2 disassembler read it correctly (same as the savestate).
    sedrv::Bswap16(snap.wramLow.data(), snap.wramLow.size());
    sedrv::Bswap16(snap.wramHigh.data(), snap.wramHigh.size());

    // VRAM is already big-endian; build the VDP2 register image and use RAMCTL's
    // CRAM mode to normalize CRAM — exactly like the savestate path.
    sedrv::BuildVdp2RegImage(vdp2Struct, 0, snap.vdp2Regs);
    const uint16_t ramctl = sedrv::ReadReg16(snap.vdp2Regs, 0x0E);
    sedrv::NormalizeCramToBigEndian(snap.cram, (ramctl >> 12) & 0x3u);
    snap.valid = true;
    return true;
}

void PollLoop(LiveState* st)
{
    Conn conn;
    while (st->running.load())
    {
        if (!conn.ok())
        {
            if (!ConnOpen(conn, st->endpoint.c_str()))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                continue;
            }
        }

        // Drain any control command posted by the UI thread; otherwise poll. A
        // pending breakpoint-set sync takes priority so new breakpoints install
        // before the next frame runs; it carries the descriptor blob as payload.
        const char* verb = SE_LIVE_VERB_GET;
        int32_t arg = 0;
        std::vector<uint8_t> payload;
        {
            std::lock_guard<std::mutex> lk(st->ctlMtx);
            if (st->bkptsDirty)
            {
                verb = SE_LIVE_VERB_BKPTS;
                arg = static_cast<int32_t>(st->bkpts.size() / SE_LIVE_BKPT_DESC_LEN);
                payload = st->bkpts;
                st->bkptsDirty = false;
            }
            else if (!st->writes.empty())
            {
                // Ship one poke: payload = address(4) + bytes; arg = byte count.
                payload = std::move(st->writes.front());
                st->writes.erase(st->writes.begin());
                verb = SE_LIVE_VERB_WRITE;
                arg = static_cast<int32_t>(payload.size() >= 4 ? payload.size() - 4 : 0);
            }
            else if (st->pending != Ctl::None)
            {
                switch (st->pending)
                {
                    case Ctl::Pause:  verb = SE_LIVE_VERB_PAUSE;  break;
                    case Ctl::Resume: verb = SE_LIVE_VERB_RESUME; break;
                    case Ctl::Step:   verb = SE_LIVE_VERB_STEP; arg = st->stepFrames; break;
                    default: break;
                }
                st->pending = Ctl::None;
                st->stepFrames = 0;
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

        LiveSnapshot snap;
        bool paused = false;
        uint64_t frame = 0;
        uint32_t sver = 0;
        StopInfo stop;
        if (!ReadSnapshot(conn, verb, arg, payload.data(), payload.size(),
                          snap, paused, frame, sver, stop))
        {
            ConnClose(conn);   // will reconnect next iteration
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        {
            std::lock_guard<std::mutex> lk(st->mtx);
            st->front = std::move(snap);
        }
        st->paused.store(paused);
        st->frameNumber.store(frame);
        st->serverVersion.store(sver);
        st->stopReason.store(stop.reason);
        st->stopCpu.store(stop.cpu);
        st->stopPc.store(stop.pc);
        std::this_thread::sleep_for(std::chrono::milliseconds(8));   // ~120 Hz cap
    }
    // Closing: if we left the emulator paused/stepped, release it before dropping
    // the connection so Yabause never stays frozen after the debugger disconnects.
    // Done here on the poll thread (after running went false) so it's race-free.
    if (conn.ok() && st->pausedByUs.load())
    {
        LiveSnapshot tmp;
        bool p = false;
        uint64_t fr = 0;
        uint32_t sv = 0;
        StopInfo si;
        ReadSnapshot(conn, SE_LIVE_VERB_RESUME, 0, nullptr, 0, tmp, p, fr, sv, si);  // best-effort
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

size_t CbVdp1Vram(void* u, uint32_t off, void* dst, size_t size)
{
    LiveState* st = St(u); std::lock_guard<std::mutex> lk(st->mtx);
    return CopyRegion(st->front.vdp1Vram, off, dst, size);
}
size_t CbVdp2Vram(void* u, uint32_t off, void* dst, size_t size)
{
    LiveState* st = St(u); std::lock_guard<std::mutex> lk(st->mtx);
    return CopyRegion(st->front.vdp2Vram, off, dst, size);
}
size_t CbCram(void* u, uint32_t off, void* dst, size_t size)
{
    LiveState* st = St(u); std::lock_guard<std::mutex> lk(st->mtx);
    return CopyRegion(st->front.cram, off, dst, size);
}
size_t CbMainRam(void* u, uint32_t address, void* dst, size_t size)
{
    LiveState* st = St(u); std::lock_guard<std::mutex> lk(st->mtx);
    if (address >= 0x06000000u)
    {
        return CopyRegion(st->front.wramHigh, address - 0x06000000u, dst, size);
    }
    if (address >= 0x00200000u)
    {
        return CopyRegion(st->front.wramLow, address - 0x00200000u, dst, size);
    }
    return 0;
}
size_t CbVdp1Fb(void* u, uint32_t off, void* dst, size_t size)
{
    LiveState* st = St(u); std::lock_guard<std::mutex> lk(st->mtx);
    return CopyRegion(st->front.vdp1Fb, off, dst, size);
}

size_t CbWriteMainRam(void* u, uint32_t address, const void* src, size_t size)
{
    if (!src || size == 0) return 0;
    LiveState* st = St(u);
    std::vector<uint8_t> payload;
    payload.reserve(4 + size);
    payload.push_back((uint8_t)(address & 0xFF));
    payload.push_back((uint8_t)((address >> 8) & 0xFF));
    payload.push_back((uint8_t)((address >> 16) & 0xFF));
    payload.push_back((uint8_t)((address >> 24) & 0xFF));
    const uint8_t* p = static_cast<const uint8_t*>(src);
    payload.insert(payload.end(), p, p + size);
    std::lock_guard<std::mutex> lk(st->ctlMtx);
    st->writes.push_back(std::move(payload));   // poll thread ships it next cycle
    return size;
}

int CbSh2Regs(void* u, int cpu, se_sh2_regs* out)
{
    if (cpu < 0 || cpu > 1 || !out) { return 0; }
    LiveState* st = St(u); std::lock_guard<std::mutex> lk(st->mtx);
    if (!st->front.hasSh2[cpu]) { return 0; }   // server predates v5 / no data yet
    *out = st->front.sh2[cpu];
    return 1;
}

uint16_t CbVdp1Reg(void* u, uint32_t reg)
{
    LiveState* st = St(u); std::lock_guard<std::mutex> lk(st->mtx);
    return sedrv::ReadReg16(st->front.vdp1Regs, reg);
}
uint16_t CbVdp2Reg(void* u, uint32_t reg)
{
    LiveState* st = St(u); std::lock_guard<std::mutex> lk(st->mtx);
    return sedrv::ReadReg16(st->front.vdp2Regs, reg);
}

// ---- Frame control. The UI thread posts a command; the poll thread sends it
//      over the shared connection on its next cycle (see PollLoop). ----
void PostCmd(LiveState* st, Ctl cmd, int32_t frames)
{
    // Track whether the emulator is currently held by us: pause/step halt it,
    // resume releases it. The poll thread uses this to resume on close.
    if (cmd == Ctl::Pause || cmd == Ctl::Step) { st->pausedByUs.store(true); }
    else if (cmd == Ctl::Resume)               { st->pausedByUs.store(false); }

    std::lock_guard<std::mutex> lk(st->ctlMtx);
    if (cmd == Ctl::Step && st->pending == Ctl::Step)
    {
        st->stepFrames += frames;   // accumulate rapid presses
    }
    else
    {
        st->pending = cmd;
        st->stepFrames = (cmd == Ctl::Step) ? frames : 0;
    }
}

int CbFramePause(void* u)
{
    PostCmd(St(u), Ctl::Pause, 0);
    return 0;
}
int CbFrameStep(void* u, int32_t frames)
{
    // By the seam's contract, frames <= 0 means "resume" (run free).
    if (frames <= 0) { PostCmd(St(u), Ctl::Resume, 0); }
    else             { PostCmd(St(u), Ctl::Step, frames); }
    return 0;
}
uint64_t CbFrameNumber(void* u)
{
    return St(u)->frameNumber.load();
}

void CbClose(void* u)
{
    LiveState* st = St(u);
    if (!st) { return; }
    st->running.store(false);
    if (st->thread.joinable()) { st->thread.join(); }
    delete st;
}

}  // namespace

extern "C" se_result se_live_open(const char* endpoint, se_data_source* out)
{
    if (!out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));

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

    LiveState* st = new (std::nothrow) LiveState();
    if (!st)
    {
        return SE_ERR_NO_DATA;
    }
    st->endpoint = ep;
    st->running.store(true);
    st->thread = std::thread(PollLoop, st);

    out->abi_version = SE_ABI_VERSION;
    out->capabilities = SE_CAP_VDP1_VRAM | SE_CAP_VDP2_VRAM | SE_CAP_CRAM |
                        SE_CAP_VDP1_REGS | SE_CAP_VDP2_REGS | SE_CAP_MAIN_RAM |
                        SE_CAP_VDP1_FB | SE_CAP_FRAME_STEP | SE_CAP_SH2_REGS |
                        SE_CAP_MEM_WRITE;
    out->user = st;
    out->read_vdp1_vram = CbVdp1Vram;
    out->read_vdp2_vram = CbVdp2Vram;
    out->read_cram      = CbCram;
    out->read_main_ram  = CbMainRam;
    out->write_main_ram = CbWriteMainRam;
    out->read_vdp1_fb   = CbVdp1Fb;
    out->read_vdp1_reg  = CbVdp1Reg;
    out->read_vdp2_reg  = CbVdp2Reg;
    out->read_sh2_regs  = CbSh2Regs;
    out->frame_pause    = CbFramePause;
    out->frame_step     = CbFrameStep;
    out->frame_number   = CbFrameNumber;
    out->close          = CbClose;
    return SE_OK;
}

extern "C" uint32_t se_live_server_version(const se_data_source* ds)
{
    // Only meaningful for a data source we produced (identified by our close cb).
    if (!ds || !ds->user || ds->close != CbClose)
    {
        return 0;
    }
    return St(ds->user)->serverVersion.load();
}

extern "C" void se_live_set_breakpoints(const se_data_source* ds,
                                        const uint8_t* descs, uint32_t count)
{
    if (!ds || !ds->user || ds->close != CbClose) { return; }
    LiveState* st = St(ds->user);
    std::lock_guard<std::mutex> lk(st->ctlMtx);
    st->bkpts.assign(descs, descs + static_cast<size_t>(count) * SE_LIVE_BKPT_DESC_LEN);
    st->bkptsDirty = true;   // poll thread ships it on its next cycle
}

extern "C" void se_live_send_input(const se_data_source* ds, uint32_t port, uint32_t buttons)
{
    if (!ds || !ds->user || ds->close != CbClose) { return; }
    // Pack port + SE_PAD_* mask; the poll thread sends it (INP) on its next cycle.
    const uint32_t packed = ((port & 0xFFFFu) << 16) | (buttons & SE_PAD_ALL);
    St(ds->user)->inputState.store(packed);
}

extern "C" int se_live_get_stop(const se_data_source* ds, uint32_t* reason,
                                uint32_t* cpu, uint32_t* pc)
{
    if (!ds || !ds->user || ds->close != CbClose) { return 0; }
    LiveState* st = St(ds->user);
    const uint32_t r = st->stopReason.load();
    if (reason) { *reason = r; }
    if (cpu)    { *cpu = st->stopCpu.load(); }
    if (pc)     { *pc = st->stopPc.load(); }
    return r != SE_LIVE_STOP_NONE ? 1 : 0;
}
