// The snapshot DELIVERY sequence of the exporter's ring, against the real exporter over the unix
// socket, with the emulate thread played by the test.
//
// Every snapshot the producer writes -- a halt's or a normal frame's -- takes the next number of one
// strictly monotonic delivery sequence, and a GET is served the oldest slot whose delivery number is
// newer than the one this connection was last served. The frame number in the reply header is NOT
// that sequence: it stays the emulated-frame identity the client displays and rewinds to (a normal
// snapshot carries its frame; a halt carries the last committed frame, or 1 before any frame has
// committed). Two snapshots can therefore show the same frame number and still be two deliveries,
// which used to make a first halt (shown as 1) swallow the first normal snapshot (also 1) as soon
// as the client had consumed the halt and the next snapshot had already arrived.
//
// Each snapshot is tagged: the first four bytes of the VDP1 VRAM block identify which snapshot a
// reply carried, so the checks are on WHICH snapshot was delivered, not on a number.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

extern "C" {
#include "se_export.h"
}
#include "SeLiveProtocol.h"

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what)
{
    if (ok) return;
    std::cerr << "FAIL: " << what << '\n';
    ++gFailures;
}
#define CHECK_EQ(got, want, what) do { const uint64_t g_ = (uint64_t)(got), w_ = (uint64_t)(want); \
    if (g_ != w_) { std::cerr << "FAIL: " << what << " (got " << g_ << ", want " << w_ << ")\n"; ++gFailures; } } while (0)

void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
uint32_t Rd32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

int ConnectRaw()
{
    for (int i = 0; i < 400; ++i)
    {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, SE_LIVE_DEFAULT_SOCK_PATH, sizeof(addr.sun_path) - 1);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) return fd;
        ::close(fd);
        Sleep(5);
    }
    return -1;
}
bool RecvAll(int fd, uint8_t* d, size_t n)
{
    while (n)
    {
        const ssize_t r = ::recv(fd, d, n, 0);
        if (r <= 0) return false;
        d += r; n -= static_cast<size_t>(r);
    }
    return true;
}
bool SkipBytes(int fd, size_t n)
{
    uint8_t sink[65536];
    while (n)
    {
        const size_t k = n < sizeof(sink) ? n : sizeof(sink);
        if (!RecvAll(fd, sink, k)) return false;
        n -= k;
    }
    return true;
}

struct Reply
{
    bool     ok = false;
    uint32_t tag = 0;        // the snapshot's tag (first 4 bytes of its VDP1 VRAM)
    uint64_t frame = 0;      // the header's frame field
    uint32_t latest = 0;     // control block: frame of the newest ring slot
    uint32_t caps = 0;
};

class Client
{
public:
    bool Connect() { mFd = ConnectRaw(); return mFd >= 0; }
    ~Client() { if (mFd >= 0) ::close(mFd); }

    Reply Exchange(const char* verb, uint32_t arg, const std::vector<uint8_t>& payload = {})
    {
        Reply c;
        const uint8_t req[8] = { uint8_t(verb[0]), uint8_t(verb[1]), uint8_t(verb[2]), uint8_t(verb[3]),
                                 uint8_t(arg), uint8_t(arg >> 8), uint8_t(arg >> 16), uint8_t(arg >> 24) };
        if (::send(mFd, req, sizeof(req), MSG_NOSIGNAL) != static_cast<ssize_t>(sizeof(req))) return c;
        if (!payload.empty() &&
            ::send(mFd, payload.data(), payload.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(payload.size()))
            return c;
        uint8_t hdr[SE_LIVE_HEADER_LEN];
        if (!RecvAll(mFd, hdr, sizeof(hdr)) || Rd32(hdr + 4) != SE_LIVE_VERSION) return c;
        const uint32_t v1 = Rd32(hdr + 8);
        size_t rest = 0;
        for (int i = 1; i < 8; ++i) rest += Rd32(hdr + 8 + 4 * i);
        const uint32_t ct = Rd32(hdr + 40);
        const uint32_t sh = Rd32(hdr + 44);
        uint8_t tagb[4];
        if (v1 < 4 || !RecvAll(mFd, tagb, 4) || !SkipBytes(mFd, v1 - 4 + rest)) return c;
        std::vector<uint8_t> ctl(ct);
        if (ct < 56 || !RecvAll(mFd, ctl.data(), ct)) return c;
        c.tag = Rd32(tagb);
        c.frame = uint64_t(Rd32(ctl.data() + 4)) | (uint64_t(Rd32(ctl.data() + 8)) << 32);
        c.latest = Rd32(ctl.data() + 32);
        c.caps = Rd32(ctl.data() + 52);
        if (!SkipBytes(mFd, sh) || !SkipTrailer()) return c;
        c.ok = true;
        return c;
    }

private:
    bool Count(uint32_t& n)
    {
        uint8_t b[4];
        if (!RecvAll(mFd, b, 4)) return false;
        n = Rd32(b);
        return true;
    }
    bool SkipTrailer()
    {
        uint32_t n;
        if (!Count(n) || !SkipBytes(mFd, size_t(n) * SE_LIVE_EVENT_LEN)) return false;
        for (int cpu = 0; cpu < 2; ++cpu)
            if (!Count(n) || !SkipBytes(mFd, size_t(n) * SE_LIVE_CALLFRAME_LEN)) return false;
        if (!SkipBytes(mFd, SE_LIVE_KEYMAP_LEN)) return false;
        if (!Count(n) || !SkipBytes(mFd, size_t(n) * SE_LIVE_LOG_LINE_LEN)) return false;
        for (int blk = 0; blk < 3; ++blk)
            if (!Count(n) || !SkipBytes(mFd, n)) return false;
        if (!Count(n)) return false;
        for (uint32_t i = 0; i < n; ++i)
        {
            uint8_t h[SE_LIVE_STATE_HDR_LEN];
            if (!RecvAll(mFd, h, sizeof(h)) || !SkipBytes(mFd, Rd32(h + 12))) return false;
        }
        if (!Count(n) || !SkipBytes(mFd, size_t(n) * SE_LIVE_EMU_SLOT_LEN)) return false;
        if (!Count(n) || !SkipBytes(mFd, 24)) return false;
        for (uint32_t i = 0; i < n; ++i)
        {
            uint32_t len;
            if (!Count(len) || !SkipBytes(mFd, len)) return false;
        }
        return true;
    }
    int mFd = -1;
};

// The emulate thread's side. 4 MiB of zeroes is more than any block the exporter copies from it.
std::vector<uint8_t> gVdp1(4u << 20, 0);

void Snap(uint32_t tag)
{
    std::memcpy(gVdp1.data(), &tag, 4);
    SeExportSnapshot(gVdp1.data(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, nullptr);
}
// A halt as the CPU hook makes one: latch the stop (which raises the pause), publish the snapshot at it.
void Halt(uint32_t tag) { SeExportNotifyStop(0, 0x06001000u); Snap(tag); }
// A whole emulated frame: the gate, the end-of-frame snapshot, the end-of-frame bookkeeping.
void Frame(uint32_t tag)
{
    while (!SeExportGateFrame()) { }
    Snap(tag);
    SeExportEndFrame();
}

extern "C" size_t FakeSave(unsigned char* buf, size_t cap)
{
    if (!buf) return 64;
    if (cap < 64) return 0;
    std::memset(buf, 0xA5, 64);
    return 64;
}
extern "C" int FakeLoad(const unsigned char*, size_t) { return 0; }

unsigned long long IdentityFrame()
{
    unsigned int tl = 0; unsigned long long fr = 0;
    SeExportFrameIdentity(&tl, &fr);
    return fr;
}

// Every scenario runs on a fresh exporter and a fresh connection.
struct Session
{
    Client cl;
    bool ok = false;
    Session()
    {
        if (SeExportInit() != 0) return;
        SeExportSetSaveStateHook(FakeSave);
        SeExportSetLoadStateHook(FakeLoad);
        if (!cl.Connect()) return;
        ok = true;
    }
    ~Session() { SeExportDeinit(); }
};

void WaitRewindCap(Client& cl)
{
    for (int i = 0; i < 1000; ++i)
    {
        const Reply r = cl.Exchange(SE_LIVE_VERB_GET, 0);
        if (r.ok && (r.caps & SE_LIVE_CAP_STATE_REWIND)) return;
        Sleep(5);
    }
    Check(false, "the exporter advertised the rewind capability");
}

// The reviewer's case: halt first, then two normal snapshots queued before the client's next GET.
void TestFirstHaltThenTwoNormals()
{
    Session s;
    if (!s.ok) { Check(false, "session"); return; }
    s.cl.Exchange(SE_LIVE_VERB_GET, 0);                 // attached
    CHECK_EQ(IdentityFrame(), 1, "frame identity before the halt");
    Halt(100);
    CHECK_EQ(IdentityFrame(), 1, "a halt does not move the reserved identity");
    Reply r = s.cl.Exchange(SE_LIVE_VERB_GET, 0);
    CHECK_EQ(r.tag, 100, "the halt is delivered");
    CHECK_EQ(r.frame, 1, "its reply carries frame 1 (a halt before any frame has committed)");
    s.cl.Exchange(SE_LIVE_VERB_RESUME, 0);              // client's cursor: frame 1
    Frame(201);
    Frame(202);                                         // both queued before the client asks again
    CHECK_EQ(IdentityFrame(), 3, "two committed frames, the third reserved");
    r = s.cl.Exchange(SE_LIVE_VERB_GET, 1);
    CHECK_EQ(r.tag, 201, "the first normal snapshot is delivered, not skipped");
    CHECK_EQ(r.frame, 1, "carrying emulated frame 1");
    r = s.cl.Exchange(SE_LIVE_VERB_GET, 1);
    CHECK_EQ(r.tag, 202, "then the second");
    CHECK_EQ(r.frame, 2, "carrying emulated frame 2");
    r = s.cl.Exchange(SE_LIVE_VERB_GET, 2);
    CHECK_EQ(r.tag, 202, "nothing newer: the latest is served again");
    CHECK_EQ(r.frame, 2, "with the same frame");
}

// Three halts inside the first frame, then the frame's own snapshot.
void TestThreeHaltsInTheFirstFrame()
{
    Session s;
    if (!s.ok) { Check(false, "session"); return; }
    s.cl.Exchange(SE_LIVE_VERB_GET, 0);
    Halt(100);
    Reply r = s.cl.Exchange(SE_LIVE_VERB_GET, 0);
    CHECK_EQ(r.tag, 100, "the first halt");
    Halt(101);
    Halt(102);
    s.cl.Exchange(SE_LIVE_VERB_RESUME, 0);
    Frame(201);
    CHECK_EQ(IdentityFrame(), 2, "the frame committed once, whatever the halts did");
    // The RESUME reply served the latest slot at that moment (halt 102); the client's cursor is there.
    r = s.cl.Exchange(SE_LIVE_VERB_GET, 1);
    CHECK_EQ(r.tag, 201, "the frame's snapshot follows the halts");
    CHECK_EQ(r.frame, 1, "frame 1");
}

// With no RESUME reply in between, every halt is delivered in order, then the frame.
void TestEveryHaltIsDeliveredInOrder()
{
    Session s;
    if (!s.ok) { Check(false, "session"); return; }
    s.cl.Exchange(SE_LIVE_VERB_GET, 0);
    Halt(100);
    Reply r = s.cl.Exchange(SE_LIVE_VERB_GET, 0);
    CHECK_EQ(r.tag, 100, "the first halt");
    Halt(101);
    Halt(102);
    SeExportGateFrame();
    // (the pause is still up: the exporter's own gate does nothing here; the frame follows the resume)
    r = s.cl.Exchange(SE_LIVE_VERB_GET, 1);
    CHECK_EQ(r.tag, 101, "the second halt, though it shows the same frame number");
    r = s.cl.Exchange(SE_LIVE_VERB_GET, 1);
    CHECK_EQ(r.tag, 102, "the third halt");
    CHECK_EQ(r.frame, 1, "all three show frame 1");
    r = s.cl.Exchange(SE_LIVE_VERB_GET, 1);
    CHECK_EQ(r.tag, 102, "nothing newer: the latest again");
}

// A halt in a later frame, then two normal frames.
void TestLaterHaltThenTwoNormals()
{
    Session s;
    if (!s.ok) { Check(false, "session"); return; }
    s.cl.Exchange(SE_LIVE_VERB_GET, 0);
    Frame(201);
    Frame(202);
    Reply r = s.cl.Exchange(SE_LIVE_VERB_GET, 0);
    CHECK_EQ(r.tag, 202, "the latest of the first two frames");
    CHECK_EQ(r.frame, 2, "frame 2");
    Halt(100);                                          // mid-frame 3
    CHECK_EQ(IdentityFrame(), 3, "the frame the halt is inside keeps its reserved identity");
    r = s.cl.Exchange(SE_LIVE_VERB_GET, 2);
    CHECK_EQ(r.tag, 100, "the halt is delivered");
    CHECK_EQ(r.frame, 2, "showing the last committed frame");
    s.cl.Exchange(SE_LIVE_VERB_RESUME, 0);
    Frame(203);                                         // frame 3
    Frame(204);                                         // frame 4
    r = s.cl.Exchange(SE_LIVE_VERB_GET, 2);
    CHECK_EQ(r.tag, 203, "the first frame after the halt");
    CHECK_EQ(r.frame, 3, "frame 3");
    r = s.cl.Exchange(SE_LIVE_VERB_GET, 3);
    CHECK_EQ(r.tag, 204, "the second");
    CHECK_EQ(r.frame, 4, "frame 4");
}

// Rewind after a halt: the target is still an emulated frame number, and the first frame after the
// load is N + 1 on the new timeline, with a delivery number newer than anything the abandoned
// timeline produced.
void TestRewindAfterAHalt()
{
    Session s;
    if (!s.ok) { Check(false, "session"); return; }
    WaitRewindCap(s.cl);
    Frame(201); Frame(202); Frame(203);
    Reply r = s.cl.Exchange(SE_LIVE_VERB_GET, 0);
    CHECK_EQ(r.tag, 203, "three frames in");
    CHECK_EQ(r.frame, 3, "frame 3");
    Halt(100);                                          // mid-frame 4, shown as frame 3
    r = s.cl.Exchange(SE_LIVE_VERB_GET, 3);
    CHECK_EQ(r.tag, 100, "the halt is delivered");
    CHECK_EQ(r.frame, 3, "as frame 3");

    // Rewind to frame 2 (LST payload: frame(4) + edits_len(4) + state).
    std::vector<uint8_t> payload(8 + 64, 0x5A);
    payload[0] = 2; payload[1] = payload[2] = payload[3] = 0;
    payload[4] = payload[5] = payload[6] = payload[7] = 0;
    s.cl.Exchange(SE_LIVE_VERB_LOADSTATE, uint32_t(payload.size()), payload);
    while (!SeExportGateFrame()) { }                    // the gate applies the load and resumes
    CHECK_EQ(IdentityFrame(), 3, "after a rewind to frame 2 the frame about to run is 3");
    Halt(300);                                          // a halt right after the load: still frame 2
    r = s.cl.Exchange(SE_LIVE_VERB_GET, 2);
    CHECK_EQ(r.tag, 300, "a halt on the new timeline is delivered");
    CHECK_EQ(r.frame, 2, "showing frame 2, the frame rewound to");
    s.cl.Exchange(SE_LIVE_VERB_RESUME, 0);
    Frame(301);
    Frame(302);
    r = s.cl.Exchange(SE_LIVE_VERB_GET, 2);
    CHECK_EQ(r.tag, 301, "the first frame after the load");
    CHECK_EQ(r.frame, 3, "is frame N + 1 = 3 on the new timeline");
    r = s.cl.Exchange(SE_LIVE_VERB_GET, 3);
    CHECK_EQ(r.tag, 302, "then 4");
    CHECK_EQ(r.frame, 4, "frame 4");
}
}  // namespace

int main()
{
    TestFirstHaltThenTwoNormals();
    TestThreeHaltsInTheFirstFrame();
    TestEveryHaltIsDeliveredInOrder();
    TestLaterHaltThenTwoNormals();
    TestRewindAfterAHalt();
    if (gFailures) { std::cerr << gFailures << " check(s) failed\n"; return 1; }
    std::cout << "LiveSnapshotDeliveryTests passed\n";
    return 0;
}
