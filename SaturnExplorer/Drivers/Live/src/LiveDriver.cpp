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
    bool                 valid = false;
};

struct LiveState
{
    std::string       endpoint;
    std::thread       thread;
    std::atomic<bool> running{false};
    std::mutex        mtx;           // guards 'front'
    LiveSnapshot      front;
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

bool ConnOpen(Conn& c, const char* endpoint)
{
#if defined(_WIN32)
    c.h = CreateFileA(endpoint, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                      OPEN_EXISTING, 0, nullptr);
    return c.h != INVALID_HANDLE_VALUE;
#else
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

// Read one snapshot from a connected socket into 'snap' (converted to core-ready
// form). Returns false on any protocol/socket error.
bool ReadSnapshot(Conn& c, LiveSnapshot& snap)
{
    if (!ConnWrite(c, SE_LIVE_REQUEST, SE_LIVE_REQUEST_LEN))
    {
        return false;
    }
    uint8_t hdr[SE_LIVE_HEADER_LEN];
    if (!ConnReadFull(c, hdr, sizeof(hdr)))
    {
        return false;
    }
    if (hdr[0] != SE_LIVE_MAGIC0 || hdr[1] != SE_LIVE_MAGIC1 ||
        hdr[2] != SE_LIVE_MAGIC2 || hdr[3] != SE_LIVE_MAGIC3)
    {
        return false;
    }
    // hdr[4..7] version (unused beyond presence for now)
    const uint32_t v1 = Rd32LE(hdr + 8);
    const uint32_t v2 = Rd32LE(hdr + 12);
    const uint32_t cr = Rd32LE(hdr + 16);
    const uint32_t vs = Rd32LE(hdr + 20);
    const uint32_t vr = Rd32LE(hdr + 24);
    const uint32_t wl = Rd32LE(hdr + 28);
    const uint32_t wh = Rd32LE(hdr + 32);
    // Sanity clamps so a malformed header can't drive a huge allocation.
    if (v1 > 0x100000u || v2 > 0x100000u || cr > 0x4000u || vs > 4096u ||
        vr > 256u || wl > 0x100000u || wh > 0x100000u)
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
    if (!ConnReadFull(c, snap.vdp1Vram.data(), v1) ||
        !ConnReadFull(c, snap.vdp2Vram.data(), v2) ||
        !ConnReadFull(c, snap.cram.data(), cr) ||
        !ConnReadFull(c, vdp2Struct.data(), vs) ||
        !ConnReadFull(c, snap.vdp1Regs.data(), vr) ||
        !ConnReadFull(c, snap.wramLow.data(), wl) ||
        !ConnReadFull(c, snap.wramHigh.data(), wh))
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
        LiveSnapshot snap;
        if (!ReadSnapshot(conn, snap))
        {
            ConnClose(conn);   // will reconnect next iteration
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        {
            std::lock_guard<std::mutex> lk(st->mtx);
            st->front = std::move(snap);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));   // ~120 Hz cap
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
                        SE_CAP_VDP1_REGS | SE_CAP_VDP2_REGS | SE_CAP_MAIN_RAM;
    out->user = st;
    out->read_vdp1_vram = CbVdp1Vram;
    out->read_vdp2_vram = CbVdp2Vram;
    out->read_cram      = CbCram;
    out->read_main_ram  = CbMainRam;
    out->read_vdp1_reg  = CbVdp1Reg;
    out->read_vdp2_reg  = CbVdp2Reg;
    out->close          = CbClose;
    return SE_OK;
}
