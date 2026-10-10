// A scripted stand-in for the emulator end of the live protocol, for tests that need the driver
// to see a particular conversation: a server that answers a few requests and then goes quiet,
// one that hangs up mid-payload, one that replies with a chosen frame. The real exporter
// (se_export.c) is the other option; this exists for the cases the exporter will not produce on
// demand -- a peer that stops answering, closes in the middle of a transfer, or identifies
// itself differently from the connection before it.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "SeLiveProtocol.h"

namespace fakelive
{

// What one reply says. Everything not listed is zero-filled (the driver accepts empty regions).
struct Reply
{
    uint32_t version = SE_LIVE_VERSION;
    uint64_t frame = 1;          // the frame being served (control block)
    uint32_t latestFrame = 0;    // ring head; 0 = same as 'frame'
    uint32_t stepPending = 0;
    uint32_t stopReason = 0;     // SE_LIVE_STOP_*
    uint32_t stopCpu = 0;
    uint32_t stopPc = 0;
    uint32_t stopSeq = 0;        // v21
    uint32_t paused = 0;
    uint32_t restoreDone = 0;
    uint32_t restoreFailed = 0;
    uint8_t  fill = 0;           // every byte of VDP1 and VDP2 VRAM carries this value
    uint32_t vramLen = 16;
    uint32_t events = 0;         // tracepoint events to attach (zero-filled records)
    uint32_t vdp2StructLen = SE_LIVE_VDP2_STRUCT_LEN;   // 0 = section unavailable
    uint32_t vdp1RegsLen = SE_LIVE_VDP1_REGS_LEN;
    std::vector<uint8_t> cram;   // CRAM section bytes (empty = none)
};

inline void Put32(std::vector<uint8_t>& o, uint32_t v)
{
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

inline std::vector<uint8_t> Build(const Reply& r)
{
    std::vector<uint8_t> o;
    o.push_back(SE_LIVE_MAGIC0); o.push_back(SE_LIVE_MAGIC1);
    o.push_back(SE_LIVE_MAGIC2); o.push_back(SE_LIVE_MAGIC3);
    Put32(o, r.version);
    const uint32_t ct = r.version >= 21 ? 44u : (r.version >= 20 ? 40u : (r.version >= 19 ? 32u : 24u));
    const uint32_t vs = r.vdp2StructLen;
    const uint32_t vr = r.vdp1RegsLen;
    // Section lengths: v1 v2 cram vdp2struct vdp1regs wramLow wramHigh [fb] ctl [sh2]
    Put32(o, r.vramLen); Put32(o, r.vramLen); Put32(o, static_cast<uint32_t>(r.cram.size()));
    Put32(o, vs); Put32(o, vr);
    Put32(o, 0); Put32(o, 0);
    if (r.version >= 4) Put32(o, 0);
    Put32(o, ct);
    if (r.version >= 5) Put32(o, 0);
    o.insert(o.end(), r.vramLen, r.fill);
    o.insert(o.end(), r.vramLen, r.fill);
    o.insert(o.end(), r.cram.begin(), r.cram.end());
    o.insert(o.end(), vs, 0);
    o.insert(o.end(), vr, 0);
    // control block
    Put32(o, r.paused);
    Put32(o, static_cast<uint32_t>(r.frame & 0xFFFFFFFFu));
    Put32(o, static_cast<uint32_t>(r.frame >> 32));
    Put32(o, r.stopReason); Put32(o, r.stopCpu); Put32(o, r.stopPc);
    if (ct >= 32) { Put32(o, r.restoreDone); Put32(o, r.restoreFailed); }
    if (ct >= 40)
    {
        Put32(o, r.latestFrame ? r.latestFrame : static_cast<uint32_t>(r.frame));
        Put32(o, r.stepPending);
    }
    if (ct >= 44) Put32(o, r.stopSeq);
    if (r.version >= 8)
    {
        Put32(o, r.events);
        o.insert(o.end(), static_cast<size_t>(r.events) * SE_LIVE_EVENT_LEN, 0);
    }
    if (r.version >= 9) { Put32(o, 0); Put32(o, 0); }            // call stacks
    if (r.version >= 10) o.insert(o.end(), SE_LIVE_KEYMAP_LEN, 0xFF);
    if (r.version >= 11) Put32(o, 0);                            // log lines
    if (r.version >= 13) Put32(o, 0);                            // sound RAM
    if (r.version >= 14) Put32(o, 0);                            // SCSP slots
    if (r.version >= 15) Put32(o, 0);                            // CD block
    if (r.version >= 16) Put32(o, 0);                            // savestate blocks
    if (r.version >= 17) Put32(o, 0);                            // emulator slots
    return o;
}

inline bool ReadExact(int fd, void* d, size_t n)
{
    uint8_t* p = static_cast<uint8_t*>(d);
    while (n)
    {
        const ssize_t r = ::recv(fd, p, n, 0);
        if (r <= 0) return false;
        p += r; n -= static_cast<size_t>(r);
    }
    return true;
}

inline bool WriteExact(int fd, const void* d, size_t n)
{
    const uint8_t* p = static_cast<const uint8_t*>(d);
    while (n)
    {
        const ssize_t w = ::send(fd, p, n, MSG_NOSIGNAL);
        if (w <= 0) return false;
        p += w; n -= static_cast<size_t>(w);
    }
    return true;
}

struct Request
{
    char     verb[5] = {};
    uint32_t arg = 0;
    bool Is(const char* v) const { return std::memcmp(verb, v, SE_LIVE_VERB_LEN) == 0; }
};

// Reads the 8-byte request header. Payloads are left unread unless the handler reads them.
inline bool ReadRequest(int fd, Request& r)
{
    uint8_t h[SE_LIVE_REQUEST_LEN];
    if (!ReadExact(fd, h, sizeof(h))) return false;
    std::memcpy(r.verb, h, 4);
    r.verb[4] = 0;
    r.arg = static_cast<uint32_t>(h[4]) | (static_cast<uint32_t>(h[5]) << 8) |
            (static_cast<uint32_t>(h[6]) << 16) | (static_cast<uint32_t>(h[7]) << 24);
    return true;
}

// Accepts connections one at a time and hands each to 'handler(fd, connectionIndex)', which
// runs on the server's thread and decides what the "emulator" does. Listens on an ephemeral
// loopback port so a busy port cannot wedge a test.
class Server
{
public:
    using Handler = std::function<void(int fd, int index)>;

    explicit Server(Handler h) : mHandler(std::move(h)) {}
    ~Server() { Stop(); }

    bool Start()
    {
        mFd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (mFd < 0) return false;
        int on = 1;
        ::setsockopt(mFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(mFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
        if (::listen(mFd, 8) != 0) return false;
        socklen_t len = sizeof(addr);
        if (::getsockname(mFd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
        mPort = ntohs(addr.sin_port);
        mRunning.store(true);
        const int listenFd = mFd;   // the thread uses its own copy: Stop() resets mFd
        mThread = std::thread([this, listenFd] {
            while (mRunning.load())
            {
                const int client = ::accept(listenFd, nullptr, nullptr);
                if (client < 0) continue;
                const int index = mAccepted.fetch_add(1);
                mActive.store(client);
                mHandler(client, index);
                mActive.store(-1);
                ::close(client);
            }
        });
        return true;
    }

    void Stop()
    {
        if (!mRunning.exchange(false)) return;
        // A handler parked in recv() on a silent client must be woken, or the join would wait on
        // the very behaviour the test is exercising.
        const int a = mActive.load();
        if (a >= 0) ::shutdown(a, SHUT_RDWR);
        if (mFd >= 0) { ::shutdown(mFd, SHUT_RDWR); ::close(mFd); mFd = -1; }
        if (mThread.joinable()) mThread.join();
    }

    std::string Endpoint() const { return "tcp:127.0.0.1:" + std::to_string(mPort); }
    int Accepted() const { return mAccepted.load(); }

private:
    Handler           mHandler;
    int               mFd = -1;
    uint16_t          mPort = 0;
    std::atomic<bool> mRunning{false};
    std::atomic<int>  mAccepted{0};
    std::atomic<int>  mActive{-1};
    std::thread       mThread;
};

}  // namespace fakelive
