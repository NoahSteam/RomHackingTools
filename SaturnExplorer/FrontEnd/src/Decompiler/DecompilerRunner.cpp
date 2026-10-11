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
    mOwnsEngine = true;   // before the worker starts: it reads this
    try
    {
        mThread = std::thread(&DecompilerRunner::WorkerMain, this);
    }
    catch (...)
    {
        // No worker, so no destructor will release the claim: release it here, or every later
        // runner would be born disabled with no engine thread anywhere.
        gEngineOwned = false;
        throw;
    }
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

    // Whatever this slot asked before is obsolete now, whether or not this request runs.
    for (auto it = mQueue.begin(); it != mQueue.end();)
        it = it->slot == slot ? mQueue.erase(it) : it + 1;   // coalesced: never run
    if (mRunning && mRunningJob.slot == slot) RequestCancelLocked(CancelReason::Superseded);

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

// The essential transition allocates nothing -- it may run because an allocation just failed --
// and happens first: disabled, and every queued request (with its snapshot) dropped. The reason
// is best effort.
void DecompilerRunner::DisableLocked(const char* reason) noexcept
{
    mDisabled = true;
    mDisabledEpoch = mEpoch;
    mQueue.clear();
    try
    {
        mDisabledReason = reason;
    }
    catch (...)
    {
        mDisabledReason.clear();
    }
    std::fprintf(stderr, "SH-2 decompiler: %s\n", reason);
}

// Disabled "for the session": a new session (epoch) on the same worker may decompile again once
// the request that would not stop has stopped. While it still runs nothing is re-enabled -- a
// new request would only queue behind it -- and no second worker is ever started. A runner that
// never owned the engine stays disabled.
void DecompilerRunner::RecoverLocked()
{
    if (mDisabled && mOwnsEngine && !mWorkerGone && mEpoch != mDisabledEpoch && !mRunning)
    {
        mDisabled = false;
        mDisabledReason.clear();
    }
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
    RecoverLocked();
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
    std::unique_ptr<Sh2Decompiler> enginePtr;
    try
    {
        enginePtr.reset(new Sh2Decompiler(mConfigDir));
    }
    catch (...)
    {
        // The worker could not even start: decompilation is off for good (no session can bring
        // back a worker that never ran), and the thread ends -- the destructor still joins it.
        std::lock_guard<std::mutex> lock(mMutex);
        mWorkerGone = true;
        DisableLocked("the decompiler could not start (out of memory)");
        return;
    }
    Sh2Decompiler& engine = *enginePtr;
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            // Never a new request while disabled, whatever is left in the queue.
            mWake.wait(lock, [&] { return mStopping || (!mQueue.empty() && !mDisabled); });
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
            std::function<void(DecompilePhase)> observer;
            try
            {
                observer = mObserver;   // tests only; a failed copy just means no observer
            }
            catch (...)
            {
            }
            engine.SetPhaseObserver(std::move(observer));
        }

        // Decompile never throws, but recording its result allocates. Should anything here
        // throw, the request is finished as failed and decompilation disabled for the session;
        // the worker itself never dies with an exception, which would end the process.
        try
        {
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
            RecoverLocked();
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mRunning = false;
            mCancel = false;
            mCancelReason = CancelReason::None;
            DisableLocked("the decompiler failed internally (out of memory?); decompilation is disabled for this session");
        }
    }
}

}  // namespace decomp
}  // namespace sfe
