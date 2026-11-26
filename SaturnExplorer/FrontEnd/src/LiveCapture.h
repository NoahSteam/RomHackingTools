// One capture of the live context, with the bookkeeping that tells a repeat from news (A1-7).
// App's per-frame capture block and the tests that stand in for it call this one function, so the
// order -- serial, capture, serial, revision -- is written once.
#pragma once

#include "CaptureRepeatTracker.h"
#include "Debug/ProfilerModel.h"
#include "LiveDriver.h"
#include "saturnexplorer/SaturnExplorer.h"

namespace sfe
{

inline se_result CaptureLiveFrame(CaptureRepeatTracker& tracker, se_context* ctx, const se_data_source* ds)
{
    const uint64_t before = se_derive_serial(ctx);
    const se_result r = se_begin_frame(ctx);
    // The revision of what THIS capture pinned (the driver answers for the capturing thread), read
    // after it so that it is the capture's own and not a publish that arrived since.
    tracker.Note(ctx, before, se_derive_serial(ctx), se_live_captured_revision(ds));
    return r;
}

// What the Memory tab's tracker is told about a LIVE capture it is about to sample: the identity
// the server stamped on the very snapshot this thread captured (not the perf stream's, which arrives
// by another route and can already be on the next timeline), the capture's revision, and the stream's
// newest timeline as the fallback for a server that does not stamp. Read after the capture, on the
// capturing thread. App and the tests that stand in for it call this one function.
inline PoolCaptureSource LivePoolSource(const se_data_source* ds, uint64_t sourceId, uint64_t instance,
                                        uint32_t streamTimeline)
{
    PoolCaptureSource src;
    src.sourceId = sourceId;
    src.instance = instance;
    src.live = true;
    src.streamTimeline = streamTimeline;
    uint64_t captureFrame = 0;
    if (se_live_captured_identity(ds, &src.captureTimeline, &captureFrame) == 0) src.captureTimeline = 0;
    src.captureRevision = se_live_captured_revision(ds);
    return src;
}

}  // namespace sfe
