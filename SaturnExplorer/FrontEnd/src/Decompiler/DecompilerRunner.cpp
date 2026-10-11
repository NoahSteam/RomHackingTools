#include "Decompiler/DecompilerRunner.h"

#include "Decompiler/SaturnArchitecture.h"   // SleighArchitecture::shutdown

#include <cstdio>

namespace sfe
{
namespace decomp
{
namespace
{

// One engine thread per process (the SLEIGH translator is process-global): a second runner is
// born disabled rather than starting a second thread.
std::atomic<bool> gEngineOwned(false);

}  // namespace

DecompilerRunner::DecompilerRunner(std::string configDir, RunnerOptions options)
    : mConfigDir(std::move(configDir)), mOptions(options)
{
    if (gEngineOwned.exchange(true))
    {
        mDisabled = true;
        mDisabledReason = "another decompiler engine thread is already running in this process";
        return;
    }
    mOwnsEngine = true;
    mThread = std::thread(&DecompilerRunner::WorkerMain, this);
}

DecompilerRunner::~DecompilerRunner()
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStopping = true;
        ++mEpoch;
        mQueue.clear();
        RequestCancelLocked(CancelReason::Exit);
    }
    mWake.notify_all();
    // Unconditionally: a detached engine thread would still be running while static objects are
    // destroyed. This waits for the next check-point plus the current uncheckable segment.
    if (mThread.joinable()) mThread.join();
    if (mOwnsEngine)
    {
        ghidra::SleighArchitecture::shutdown();
        gEngineOwned = false;
    }
}

uint64_t DecompilerRunner::Submit(int slot, std::shared_ptr<const MemorySnapshot> snapshot, DecompileRequest request,
                                  bool retryTooLarge)
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mDisabled) return 0;
    const uint64_t generation = mNextGeneration++;
    mLatest[slot] = generation;

    uint32_t entry = 0;
    if (CanonicalAddress(request.entry, entry) && mTooLarge.count(entry) && !retryTooLarge)
    {
        DecompileResult r;
        r.status = DecompileStatus::TooLarge;
        r.error = "too large to decompile within the time budget (not retried automatically; use Refresh)";
        r.key = snapshot->key;
        r.generation = generation;
        r.slot = slot;
        r.requestedEntry = request.entry;
        r.entry = entry;
        mDone.push_back(std::make_pair(mEpoch, std::move(r)));
        return generation;
    }

    for (auto it = mQueue.begin(); it != mQueue.end();)
        it = it->slot == slot ? mQueue.erase(it) : it + 1;   // coalesced: never run
    if (mRunning && mRunningJob.slot == slot) RequestCancelLocked(CancelReason::Superseded);

    Job job;
    job.generation = generation;
    job.epoch = mEpoch;
    job.slot = slot;
    job.snapshot = std::move(snapshot);
    job.request = std::move(request);
    mQueue.push_back(std::move(job));
    mWake.notify_all();
    return generation;
}

void DecompilerRunner::Cancel(int slot)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto it = mQueue.begin(); it != mQueue.end();)
    {
        if (it->slot != slot)
        {
            ++it;
            continue;
        }
        DecompileResult r;
        r.status = DecompileStatus::Cancelled;
        r.error = "cancelled";
        r.key = it->snapshot->key;
        r.generation = it->generation;
        r.slot = slot;
        r.requestedEntry = it->request.entry;
        mDone.push_back(std::make_pair(it->epoch, std::move(r)));
        it = mQueue.erase(it);
    }
    if (mRunning && mRunningJob.slot == slot) RequestCancelLocked(CancelReason::User);
}

void DecompilerRunner::RequestCancelLocked(CancelReason reason)
{
    if (!mRunning || mCancelReason != CancelReason::None) return;   // the first request's clock stands
    mCancelReason = reason;
    mCancelRequestedAt = Clock::now();
    mCancel = true;
}

void DecompilerRunner::DisableLocked(const std::string& reason)
{
    mDisabled = true;
    mDisabledReason = reason;
    mQueue.clear();
    std::fprintf(stderr, "SH-2 decompiler: %s\n", reason.c_str());
}

bool DecompilerRunner::Poll(DecompileResult& out)
{
    std::lock_guard<std::mutex> lock(mMutex);
    const Clock::time_point now = Clock::now();
    if (mRunning && !mDisabled)
    {
        const double running = std::chrono::duration<double>(now - mRunningSince).count();
        if (mCancelReason == CancelReason::None && mOptions.budgetSeconds > 0 && running > mOptions.budgetSeconds)
            RequestCancelLocked(CancelReason::Budget);
        if (mCancelReason != CancelReason::None &&
            std::chrono::duration<double>(now - mCancelRequestedAt).count() > mOptions.cancelAckSeconds)
        {
            char b[200];
            std::snprintf(b, sizeof b,
                          "a cancelled decompilation of 0x%08X did not stop within %.1f s; "
                          "decompilation is disabled for this session",
                          mRunningJob.request.entry, mOptions.cancelAckSeconds);
            DisableLocked(b);
        }
    }
    while (!mDone.empty())
    {
        std::pair<uint64_t, DecompileResult> done = std::move(mDone.front());
        mDone.pop_front();
        // Only a current result is installed: the session it was made for, and the newest request
        // its requester made. Anything else is the answer to a question nobody is asking now.
        if (mDisabled || done.first != mEpoch) continue;
        auto latest = mLatest.find(done.second.slot);
        if (latest == mLatest.end() || latest->second != done.second.generation) continue;
        out = std::move(done.second);
        return true;
    }
    return false;
}

void DecompilerRunner::InvalidateSession()
{
    std::lock_guard<std::mutex> lock(mMutex);
    ++mEpoch;
    mQueue.clear();
    mDone.clear();
    mTooLarge.clear();
    RequestCancelLocked(CancelReason::Session);
}

bool DecompilerRunner::Busy() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mRunning || !mQueue.empty();
}

uint64_t DecompilerRunner::Epoch() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mEpoch;
}

bool DecompilerRunner::Disabled() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mDisabled;
}

std::string DecompilerRunner::DisabledReason() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mDisabledReason;
}

uint64_t DecompilerRunner::RunsStarted() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mRunsStarted;
}

void DecompilerRunner::SetPhaseObserverForTests(std::function<void(DecompilePhase)> observer)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mObserver = std::move(observer);
}

void DecompilerRunner::WorkerMain()
{
    // Every engine object is created, used and destroyed on this thread. Each request builds a
    // fresh Architecture (and with it resets the shared translator), so a failed or cancelled
    // request leaves nothing behind for the next one.
    Sh2Decompiler engine(mConfigDir);
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mWake.wait(lock, [&] { return mStopping || !mQueue.empty(); });
            if (mStopping) return;
            job = std::move(mQueue.front());
            mQueue.pop_front();
            mRunning = true;
            mRunningJob.generation = job.generation;
            mRunningJob.epoch = job.epoch;
            mRunningJob.slot = job.slot;
            mRunningJob.request.entry = job.request.entry;
            mRunningSince = Clock::now();
            mCancelReason = CancelReason::None;
            mCancel = false;
            ++mRunsStarted;
            engine.SetPhaseObserver(mObserver);
        }

        DecompileResult r = engine.Decompile(job.snapshot, job.request, &mCancel);
        r.generation = job.generation;
        r.slot = job.slot;
        job.snapshot.reset();   // the snapshot's memory goes now, not when the next job replaces it

        std::lock_guard<std::mutex> lock(mMutex);
        const CancelReason reason = mCancelReason;
        mRunning = false;
        mCancel = false;
        mCancelReason = CancelReason::None;
        if (r.status == DecompileStatus::Cancelled && reason == CancelReason::Budget)
        {
            char b[120];
            std::snprintf(b, sizeof b, "too large to decompile within %g s", mOptions.budgetSeconds);
            r.status = DecompileStatus::TooLarge;
            r.error = b;
            if (job.epoch == mEpoch) mTooLarge.insert(r.entry);
        }
        mDone.push_back(std::make_pair(job.epoch, std::move(r)));
    }
}

}  // namespace decomp
}  // namespace sfe
