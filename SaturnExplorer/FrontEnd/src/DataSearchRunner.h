// DataSearchRunner — the async data-search worker's lifecycle, separated from the UI that shows
// it (review finding UI-01).
//
// A PRS scan attempts a decompression at every offset of every file, so a search can take a
// while: it runs off the UI thread with live progress and cancellation, and asking for another
// search while one runs cancels the first and queues the second rather than dropping the request.
// That is a small state machine — thread, progress, queue, result routing — and it was nine
// members and three methods of App, mixed in with the windows that display it.
//
// Two things the separation fixes rather than merely moves:
//
//   * The results were written by the worker into members the UI also read, with a comment in the
//     draw code saying not to read them while a search was running. Here the worker writes only
//     into its own outcome, which Poll() hands over after the join — so there is nothing to read
//     early, rather than a rule about it.
//   * Which window a search's results belong to was three parallel bools (running, queued, and
//     pending-until-a-directory-is-set) that had to be kept in step by hand. It travels with the
//     request now.
//
// No ImGui and no App: the routing tag is an opaque int the caller assigns meaning to, so this
// stays testable and the UI keeps its own vocabulary.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "DataSearch.h"

namespace sfe
{

struct DataSearchRequest
{
    std::vector<std::string> roots;        // files and/or directories to scan
    std::vector<uint8_t>     needle;       // the bytes to find
    SearchCompression        compression = SearchCompression::None;
    std::string              label;        // human label, e.g. "Texture at 0x12340"
    std::string              scopeText;    // human description of what is being searched
    int                      destination = 0;   // caller-defined routing tag; opaque here
};

struct DataSearchOutcome
{
    std::vector<DataSearchHit> hits;
    std::string                summary;      // ready to display
    int                        destination = 0;
    bool                       cancelled = false;
};

class DataSearchRunner
{
public:
    DataSearchRunner() = default;
    ~DataSearchRunner();

    DataSearchRunner(const DataSearchRunner&) = delete;
    DataSearchRunner& operator=(const DataSearchRunner&) = delete;

    // Start 'request'. When a search is already running, cancel it and queue this one instead;
    // Poll() starts the queued request as soon as the old worker is reaped, so nothing is
    // silently lost. Only one request is ever queued — asking a third time replaces the second,
    // which is what asking again means.
    //
    // An empty needle or no roots is the caller's business to check: this starts what it is given.
    void Start(DataSearchRequest request);

    // Reap a finished worker on the calling thread. Returns true exactly once per completed
    // search, filling 'out' with its results, and starts a queued request if one is waiting.
    // Call it once per frame before reading anything.
    bool Poll(DataSearchOutcome& out);

    bool Running() const { return mRunning.load(); }
    void Cancel() { mProgress.cancel.store(true); }

    // Live counters for a progress display. Safe to read while a search runs; that is what they
    // are for.
    const SearchProgress& Progress() const { return mProgress; }

    // What the running search is scanning, for the progress line. Only written when no worker is
    // active, so reading it during one is safe.
    const std::string& ScopeText() const { return mScopeText; }

    // Stop and join any worker. Called by the destructor; also on app shutdown, where the wait
    // needs to happen before the rest of the frontend goes away.
    void Stop();

private:
    void Launch(DataSearchRequest request);

    std::thread       mThread;
    SearchProgress    mProgress;
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mDone{false};
    std::string       mScopeText;

    // The worker's private landing area. It is written only by the worker and read only after
    // the join in Poll(), which is what makes the hand-off free of any "do not read this yet"
    // discipline in the caller.
    DataSearchOutcome mOutcome;

    bool              mQueued = false;
    DataSearchRequest mQueuedRequest;
};

}  // namespace sfe
