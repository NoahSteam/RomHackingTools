#include "Debug/MemorySearchRunner.h"

#include <utility>

namespace sfe
{

void MemorySearchRunner::Launch(SearchSnapshot snap, WatchType type,
                                std::vector<SearchHit> previous, bool first, SearchCompare cmp,
                                int64_t operand)
{
    mRunning.store(true);
    mDone.store(false);
    auto work = [this, snap = std::move(snap), type, previous = std::move(previous), first, cmp,
                 operand]() mutable {
        mResult = first ? MemorySearch::ScanFirst(snap, type, cmp, operand)
                        : MemorySearch::ScanNext(snap, type, std::move(previous), cmp, operand);
        mDone.store(true);
    };
#ifdef __EMSCRIPTEN__
    // The base web viewer is not built with pthreads, so a std::thread cannot start there:
    // run inline. Poll() still delivers the result on its next call, so the caller's flow is
    // the same.
    work();
#else
    mThread = std::thread(std::move(work));
#endif
}

bool MemorySearchRunner::StartFirst(MemorySearch& search, IMemoryBackend& backend,
                                    const std::vector<SearchRegion>& regions, WatchType type,
                                    SearchCompare cmp, int64_t operand)
{
    if (mRunning.load()) return false;
    search.BeginFirst(regions, type);
    Launch(MemorySearch::CaptureFirst(backend, regions), type, {}, true, cmp, operand);
    return true;
}

bool MemorySearchRunner::StartNext(MemorySearch& search, IMemoryBackend& backend,
                                   SearchCompare cmp, int64_t operand)
{
    if (mRunning.load() || !search.Active()) return false;
    SearchSnapshot snap = search.CaptureNext(backend);
    Launch(std::move(snap), search.Type(), search.TakeHits(), false, cmp, operand);
    return true;
}

bool MemorySearchRunner::Poll(MemorySearch& search)
{
    if (!mRunning.load() || !mDone.load()) return false;
    if (mThread.joinable()) mThread.join();   // makes the worker's write to mResult visible
    search.Complete(std::move(mResult));
    mResult = SearchScan{};
    mRunning.store(false);
    mDone.store(false);
    return true;
}

void MemorySearchRunner::Stop()
{
    if (mThread.joinable()) mThread.join();
    mResult = SearchScan{};
    mRunning.store(false);
    mDone.store(false);
}

}  // namespace sfe
