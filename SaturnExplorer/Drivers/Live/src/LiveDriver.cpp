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
    std::vector<uint8_t> wramLow;    // 0x00200000, 1 MiB
    std::vector<uint8_t> wramHigh;   // 0x06000000, 1 MiB
    std::vector<uint8_t> vdp1Fb;     // VDP1 frame buffer (drawn output)
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
    std::mutex            ctlMtx;    // guards pending / stepFrames
    Ctl                   pending = Ctl::None;
    int32_t               stepFrames = 0;
    // True once we've told the emulator to pause/step and not since resumed, so the
    // poll thread knows to release it on close (never leave Yabause paused).
    std::atomic<bool>     pausedByUs{false};
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
bool ReadSnapshot(Conn& c, const char* verb, int32_t arg, LiveSnapshot& snap,
                  bool& outPaused, uint64_t& outFrame)
{
    if (!SendCommand(c, verb, arg))
    {
        return false;
    }
    // Read magic + version first, then size the section-length table to the
    // server's version so we stay in sync with either a v3 (no VDP1 frame buffer,
    // 8 sections) or v4+ (adds the FB, 9 sections) server. This keeps a rebuilt
    // (v4) client working against a not-yet-rebuilt (v3) Yabause: it just reports
    // fb == 0 and the FB read below becomes a no-op.
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
    const int hasFb = (version >= 4u) ? 1 : 0;    // FB section added in v4
    const int numLen = 8 + hasFb;                 // section-length entries
    uint8_t lens[9 * 4];
    if (!ConnReadFull(c, lens, static_cast<size_t>(numLen) * 4))
    {
        return false;
    }
    const uint32_t v1 = Rd32LE(lens + 0);
    const uint32_t v2 = Rd32LE(lens + 4);
    const uint32_t cr = Rd32LE(lens + 8);
    const uint32_t vs = Rd32LE(lens + 12);
    const uint32_t vr = Rd32LE(lens + 16);
    const uint32_t wl = Rd32LE(lens + 20);
    const uint32_t wh = Rd32LE(lens + 24);
    const uint32_t fb = hasFb ? Rd32LE(lens + 28) : 0u;
    const uint32_t ct = Rd32LE(lens + (hasFb ? 32 : 28));
    // Sanity clamps so a malformed header can't drive a huge allocation.
    if (v1 > 0x100000u || v2 > 0x100000u || cr > 0x4000u || vs > 4096u ||
        vr > 256u || wl > 0x100000u || wh > 0x100000u || fb > 0x40000u || ct > 64u)
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
    if (!ConnReadFull(c, snap.vdp1Vram.data(), v1) ||
        !ConnReadFull(c, snap.vdp2Vram.data(), v2) ||
        !ConnReadFull(c, snap.cram.data(), cr) ||
        !ConnReadFull(c, vdp2Struct.data(), vs) ||
        !ConnReadFull(c, snap.vdp1Regs.data(), vr) ||
        !ConnReadFull(c, snap.wramLow.data(), wl) ||
        !ConnReadFull(c, snap.wramHigh.data(), wh) ||
        !ConnReadFull(c, snap.vdp1Fb.data(), fb) ||
        !ConnReadFull(c, ctl.data(), ct))
    {
        return false;
    }

    // Control block: paused (u32 LE) + frame (u64 LE). Absent on older servers.
    outPaused = ct >= 4 && Rd32LE(ctl.data()) != 0;
    outFrame = ct >= 12 ? (static_cast<uint64_t>(Rd32LE(ctl.data() + 4)) |
                           (static_cast<uint64_t>(Rd32LE(ctl.data() + 8)) << 32))
                        : 0;

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

        // Drain any control command posted by the UI thread; otherwise poll.
        const char* verb = SE_LIVE_VERB_GET;
        int32_t arg = 0;
        {
            std::lock_guard<std::mutex> lk(st->ctlMtx);
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

        LiveSnapshot snap;
        bool paused = false;
        uint64_t frame = 0;
        if (!ReadSnapshot(conn, verb, arg, snap, paused, frame))
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
        ReadSnapshot(conn, SE_LIVE_VERB_RESUME, 0, tmp, p, fr);   // best-effort
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
                        SE_CAP_VDP1_FB | SE_CAP_FRAME_STEP;
    out->user = st;
    out->read_vdp1_vram = CbVdp1Vram;
    out->read_vdp2_vram = CbVdp2Vram;
    out->read_cram      = CbCram;
    out->read_main_ram  = CbMainRam;
    out->read_vdp1_fb   = CbVdp1Fb;
    out->read_vdp1_reg  = CbVdp1Reg;
    out->read_vdp2_reg  = CbVdp2Reg;
    out->frame_pause    = CbFramePause;
    out->frame_step     = CbFrameStep;
    out->frame_number   = CbFrameNumber;
    out->close          = CbClose;
    return SE_OK;
}
