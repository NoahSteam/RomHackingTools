#include "Debug/MemorySearchRunner.h"

#include <chrono>
#include <utility>

namespace sfe
{

void MemorySearchRunner::Launch(SearchSnapshot snap, WatchType type,
                                std::vector<SearchHit> previous, bool first, SearchCompare cmp,
                                int64_t operand)
{
    mRunning.store(true);
    mDone.store(false);
    if (mMode == Mode::Incremental)
    {
        // Nothing runs until Poll(): a slice per UI frame, so the browser repaints in between.
        mSnap = std::move(snap);
        mType = type;
        mPrevious = std::move(previous);
        mFirst = first;
        mCmp = cmp;
        mOperand = operand;
        mCursor = SearchScanCursor{};
        mResult = SearchScan{};
        return;
    }
    auto work = [this, snap = std::move(snap), type, previous = std::move(previous), first, cmp,
                 operand]() mutable {
        mResult = first ? MemorySearch::ScanFirst(snap, type, cmp, operand)
                        : MemorySearch::ScanNext(snap, type, std::move(previous), cmp, operand);
        mDone.store(true);
    };
    mThread = std::thread(std::move(work));
}

bool MemorySearchRunner::Step()
{
    // The clock is checked every few thousand candidates, not every one: reading it costs about
    // what decoding a candidate does.
    const std::size_t kChunk = 4096;
    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        const bool finished =
            mFirst ? MemorySearch::ScanFirstStep(mSnap, mType, mCmp, mOperand, mCursor, mResult,
                                                 kChunk)
                   : MemorySearch::ScanNextStep(mSnap, mType, mCmp, mOperand, mCursor, mPrevious,
                                                mResult, kChunk);
        if (finished) return true;
        const std::chrono::duration<double, std::milli> spent =
            std::chrono::steady_clock::now() - start;
        if (spent.count() >= mSliceMs) return false;
    }
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
    if (!mRunning.load()) return false;
    if (mMode == Mode::Incremental)
    {
        if (!Step()) return false;
        mSnap = SearchSnapshot{};   // the bytes were only needed to scan
        mPrevious.clear();
        mPrevious.shrink_to_fit();
    }
    else if (!mDone.load())
    {
        return false;
    }
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
    mSnap = SearchSnapshot{};
    mPrevious.clear();
    mRunning.store(false);
    mDone.store(false);
}

}  // namespace sfe
