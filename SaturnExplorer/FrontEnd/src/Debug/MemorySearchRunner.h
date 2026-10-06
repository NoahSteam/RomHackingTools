// MemorySearchRunner — runs a MemorySearch scan off the UI thread.
//
// A first scan of both WRAM banks decodes two million candidates, which is tens of
// milliseconds -- long enough to drop frames if it runs inside a button handler. The runner
// does the part that touches the backend (copying the bytes) on the calling thread, then hands
// that private snapshot and the previous hit list to a worker; Poll() installs the result
// back into the MemorySearch on the calling thread, once. While a scan runs the search's hit
// list is empty (it was handed to the worker), so the UI has nothing to read half-written.
//
// A build with no threads to run a worker on (the base web viewer is not built with pthreads)
// cannot do that, and running the scan inline would finish it all before the browser could
// repaint "Scanning...". There the scan is stepped instead: Poll() runs a time-budgeted slice
// per UI frame and returns the frame to the browser between slices.
//
// No ImGui and no App, like DataSearchRunner: testable on its own.
#pragma once

#include <atomic>
#include <thread>
#include <vector>

#include "Debug/MemorySearch.h"

namespace sfe
{

class MemorySearchRunner
{
public:
    enum class Mode
    {
        Thread,        // scan on a worker thread
        Incremental,   // scan in time-budgeted slices inside Poll(), on the caller's thread
    };
    // Where threads are unavailable the runner steps; everywhere else it uses a worker.
    static Mode DefaultMode()
    {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
        return Mode::Incremental;
#else
        return Mode::Thread;
#endif
    }

    explicit MemorySearchRunner(Mode mode = DefaultMode(), double sliceMs = 4.0)
        : mMode(mode), mSliceMs(sliceMs) {}
    ~MemorySearchRunner() { Stop(); }
    MemorySearchRunner(const MemorySearchRunner&) = delete;
    MemorySearchRunner& operator=(const MemorySearchRunner&) = delete;

    // Start a scan. Ignored (returns false) while another is running.
    bool StartFirst(MemorySearch& search, IMemoryBackend& backend,
                    const std::vector<SearchRegion>& regions, WatchType type,
                    SearchCompare cmp, int64_t operand);
    bool StartNext(MemorySearch& search, IMemoryBackend& backend, SearchCompare cmp,
                   int64_t operand);

    // Install a finished scan into 'search' on the calling thread. Returns true exactly once
    // per scan. Call it once per frame.
    bool Poll(MemorySearch& search);

    bool Running() const { return mRunning.load(); }

    // Join any worker and drop its result. 'search' must be Reset() by the caller if it was
    // mid-scan, since its hits went with the worker.
    void Stop();

private:
    bool Step();   // Incremental: run one time-budgeted slice; true when the scan is finished
    void Launch(SearchSnapshot snap, WatchType type, std::vector<SearchHit> previous, bool first,
                SearchCompare cmp, int64_t operand);

    Mode              mMode;
    double            mSliceMs;
    std::thread       mThread;

    // Incremental mode's scan in progress.
    SearchSnapshot         mSnap;
    WatchType              mType = WatchType::U8;
    SearchCompare          mCmp = SearchCompare::Equal;
    int64_t                mOperand = 0;
    bool                   mFirst = true;
    SearchScanCursor       mCursor;
    std::vector<SearchHit> mPrevious;
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mDone{false};
    SearchScan        mResult;   // written by the worker, read only after the join in Poll()
};

}  // namespace sfe
