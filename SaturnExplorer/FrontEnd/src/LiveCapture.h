// One capture of the live context, with the bookkeeping that tells a repeat from news (A1-7).
// App's per-frame capture block and the tests that stand in for it call this one function, so the
// order -- serial, capture, serial, revision -- is written once.
#pragma once

#include "CaptureRepeatTracker.h"
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

}  // namespace sfe
