// MemorySearchRunner — runs a MemorySearch scan off the UI thread.
//
// A first scan of both WRAM banks decodes two million candidates, which is tens of
// milliseconds -- long enough to drop frames if it runs inside a button handler. The runner
// does the part that touches the backend (copying the bytes) on the calling thread, then hands
// that private snapshot and the previous hit list to a worker; Poll() installs the result
// back into the MemorySearch on the calling thread, once. While a scan runs the search's hit
// list is empty (it was handed to the worker), so the UI has nothing to read half-written.
//
// No ImGui and no App, like DataSearchRunner: testable on its own.
#pragma once

#include <atomic>
#include <thread>

#include "Debug/MemorySearch.h"

namespace sfe
{

class MemorySearchRunner
{
public:
    MemorySearchRunner() = default;
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
    void Launch(SearchSnapshot snap, WatchType type, std::vector<SearchHit> previous, bool first,
                SearchCompare cmp, int64_t operand);

    std::thread       mThread;
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mDone{false};
    SearchScan        mResult;   // written by the worker, read only after the join in Poll()
};

}  // namespace sfe
