#include "DataSearchRunner.h"

#include <cstdio>
#include <utility>

namespace sfe
{

DataSearchRunner::~DataSearchRunner()
{
    Stop();
}

void DataSearchRunner::Stop()
{
    mProgress.cancel.store(true);
    if (mThread.joinable())
    {
        mThread.join();
    }
    mRunning.store(false);
    mDone.store(false);
    mQueued = false;
}

void DataSearchRunner::Start(DataSearchRequest request)
{
    if (mRunning.load())
    {
        mQueuedRequest = std::move(request);
        mQueued = true;
        mProgress.cancel.store(true);
        return;
    }
    Launch(std::move(request));
}

void DataSearchRunner::Launch(DataSearchRequest request)
{
    if (mThread.joinable())
    {
        mThread.join();   // reap a previous (finished) run
    }
    mProgress.Reset();
    mScopeText = request.scopeText;
    mOutcome = DataSearchOutcome{};
    mDone.store(false);
    mRunning.store(true);

    auto doWork = [this, request = std::move(request)]() mutable {
        std::vector<DataSearchHit> results;
        const size_t files = SearchData(request.roots, request.needle.data(), request.needle.size(),
                                        request.compression, results, 256, &mProgress);
        size_t total = 0;
        for (const DataSearchHit& h : results) total += h.offsets.size();

        const bool   cancelled = mProgress.cancel.load();
        const size_t skipped = mProgress.filesSkipped.load();
        // A file whose PRS scan ran out of work budget was searched as far as the budget allowed
        // and no further, so a match could be sitting at an offset never reached. That belongs in
        // the summary beside the count, not left for the user to assume away.
        const size_t partial = mProgress.filesBudgetExhausted.load();

        std::string notes;
        if (skipped) notes += "\nSome files were skipped (too large for a PRS scan).";
        if (partial)
        {
            notes += "\n" + std::to_string(partial) +
                     " file(s) were only searched partway: the PRS scan hit its work limit, so "
                     "a match past that point would have been missed.";
        }

        char sum[512];
        std::snprintf(sum, sizeof(sum),
                      "%s%s\n%zu match(es) in %zu file(s)  —  scanned %zu file%s in %s%s.%s",
                      cancelled ? "[Cancelled] " : "", request.label.c_str(), total, results.size(),
                      files, files == 1 ? "" : "s", request.scopeText.c_str(),
                      request.compression == SearchCompression::Prs ? " as PRS-compressed" : "",
                      notes.c_str());

        mOutcome.hits = std::move(results);
        mOutcome.summary = sum;
        mOutcome.destination = request.destination;
        mOutcome.cancelled = cancelled;
        mDone.store(true);   // reaped by Poll() on the caller's thread
    };

#ifdef __EMSCRIPTEN__
    // The browser build has no host filesystem (the search finds nothing) and the base viewer
    // isn't compiled with pthreads, so never start a std::thread there — run inline. Poll() still
    // delivers the outcome on the next call, so the caller's flow is identical.
    doWork();
#else
    mThread = std::thread(std::move(doWork));
#endif
}

bool DataSearchRunner::Poll(DataSearchOutcome& out)
{
    if (!mRunning.load() || !mDone.load())
    {
        return false;
    }
    if (mThread.joinable())
    {
        // The join is what makes every one of the worker's writes to mOutcome visible here.
        mThread.join();
    }
    mRunning.store(false);
    mDone.store(false);
    out = std::move(mOutcome);
    mOutcome = DataSearchOutcome{};

    if (mQueued)
    {
        mQueued = false;
        Launch(std::move(mQueuedRequest));
    }
    return true;
}

}  // namespace sfe
