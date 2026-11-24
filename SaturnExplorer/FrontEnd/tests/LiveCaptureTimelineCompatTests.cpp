// Compatibility of the control block's snapshot timeline (+56) against a scripted server:
//  * a server that stamps it (60-byte control block): the client reports the capture's timeline and
//    frame, and a timeline of 0 reads as "not stated";
//  * a server that predates it (56 bytes, the v27 block as shipped): the capture works, the identity
//    is unknown, and the tracker therefore waits out a stream timeline change rather than guessing;
//  * a longer block (64 bytes, the most this client's own sanity clamp allows) still parses.
// The other direction -- an OLD client against a NEW server -- is the client's clamp (ct <= 64) and
// that it reads only the first 56 bytes; it was checked by running the previous revision's driver
// against this revision's exporter (see Profiler.md, "Verification").
#include "FakeLiveServer.h"
#include "LiveDriver.h"
#include "SeLiveProtocol.h"
#include "saturnexplorer/SaturnExplorer.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what, int line)
{
    if (ok) return;
    std::printf("CHECK failed at line %d: %s\n", line, what);
    ++gFailures;
}
#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

struct Got { bool captured = false; int known = -1; uint32_t timeline = 99; uint64_t frame = 0; };

Got Capture(const fakelive::Reply& reply)
{
    Got g;
    const std::vector<uint8_t> bytes = fakelive::Build(reply);
    fakelive::Server server([&bytes](int fd, int index) {
        if (index == 0) return;
        fakelive::Request r;
        while (fakelive::ReadRequest(fd, r))
            if (!fakelive::WriteExact(fd, bytes.data(), bytes.size())) return;
    });
    if (!server.Start()) { CHECK(false && "listener"); return g; }
    se_data_source ds = {};
    if (se_live_open(server.Endpoint().c_str(), &ds) != SE_OK) { CHECK(false && "open"); return g; }
    se_config cfg;
    cfg.abi_version = SE_ABI_VERSION;
    cfg.reserved = 0;
    se_context* ctx = se_create(&ds, &cfg);
    CHECK(ctx != nullptr);
    if (ctx)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline && !g.captured)
        {
            if (se_begin_frame(ctx) == SE_OK && se_live_captured_revision(&ds) != 0) g.captured = true;
            else std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        g.known = se_live_captured_identity(&ds, &g.timeline, &g.frame);
        se_destroy(ctx);
    }
    else ds.close(ds.user);
    server.Stop();
    return g;
}
}  // namespace

int main()
{
    fakelive::Reply r;
    r.version = SE_LIVE_VERSION;
    r.frame = 0x100000007ull;

    r.ctlLen = SE_LIVE_CONTROL_LEN;   // as this revision's exporter sends it
    r.snapshotTimeline = 0x00ABCDEFu;
    Got g = Capture(r);
    CHECK(g.captured && g.known == 1 && g.timeline == 0x00ABCDEFu && g.frame == 0x100000007ull);

    r.snapshotTimeline = 0;           // a server that sends the field but does not know
    g = Capture(r);
    CHECK(g.captured && g.known == 0 && g.timeline == 0 && g.frame == 0x100000007ull);

    r.ctlLen = 56;                    // the v27 block as shipped before the field
    r.snapshotTimeline = 5;           // (not on the wire)
    g = Capture(r);
    CHECK(g.captured && g.known == 0 && g.timeline == 0 && g.frame == 0x100000007ull);

    r.ctlLen = 64;                    // longer than the field: still parses, the field is where it was
    r.snapshotTimeline = 7;
    g = Capture(r);
    CHECK(g.captured && g.known == 1 && g.timeline == 7);

    if (gFailures) { std::printf("%d failure(s)\n", gFailures); return 1; }
    std::printf("LiveCaptureTimelineCompatTests: all passed\n");
    return 0;
}
