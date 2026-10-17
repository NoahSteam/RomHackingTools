#include "Decompiler/DecompilerRunner.h"

#include "Decompiler/SaturnArchitecture.h"   // SleighArchitecture::shutdown

#include <cerrno>
#include <cstdio>
#include <exception>
#include <system_error>

#ifndef _WIN32
#include <pthread.h>
#endif

namespace sfe
{
namespace decomp
{
namespace
{

// One engine thread per process (the SLEIGH translator is process-global): a second runner is
// born disabled rather than starting a second thread.
std::atomic<bool> gEngineOwned(false);

void (*gRecordHook)() = nullptr;
std::atomic<bool> gFailThreadStart(false);

// Two independent 64-bit FNV-style lanes over the same words.
struct Digest
{
    uint64_t a = 1469598103934665603ull, b = 0x9E3779B97F4A7C15ull;
    void Mix(uint64_t v)
    {
        a = (a ^ v) * 1099511628211ull;
        b = (b + v + 0x9E3779B97F4A7C15ull) * 0xFF51AFD7ED558CCDull;
        b ^= b >> 29;
    }
    void MixString(const std::string& s)
    {
        Mix(s.size());
        for (unsigned char ch : s) Mix(ch);
    }
    void Mix(const TypeShape& t)
    {
        Mix((uint64_t(t.kind) << 24) | (uint64_t(t.size) << 16) | (uint64_t(t.pointee) << 8) | t.pointeeSize);
    }
};

}  // namespace

DecompilerRunner::RequestKey DecompilerRunner::MakeRequestKey(const SnapshotKey& snapshotKey,
                                                              const DecompileRequest& request)
{
    Digest d;
    d.Mix(snapshotKey.sourceId);
    d.Mix(snapshotKey.deriveSerial);
    d.Mix(snapshotKey.frameNumber);
    d.Mix(snapshotKey.regionSetHash);
    d.Mix(request.entry);   // as asked, not canonical: another mirror is another question
    d.Mix(request.names.size());
    for (const auto& n : request.names)
    {
        d.Mix(n.first);
        d.MixString(n.second);
    }
    d.Mix(request.knownFunctions.size());
    for (uint32_t f : request.knownFunctions) d.Mix(f);
    d.Mix(request.prototypes.size());
    for (const auto& p : request.prototypes)
    {
        d.Mix(p.first);
        d.Mix(p.second.known);
        d.Mix(p.second.ret);
        d.Mix(p.second.params.size());
        for (const TypeShape& t : p.second.params) d.Mix(t);
        d.Mix(p.second.effects.preserved);
        d.Mix(p.second.effects.storesThrough);
        d.Mix(p.second.effects.outOrigins);
        d.Mix(p.second.effects.leaks);
        d.Mix(p.second.effects.frameValues);
        d.Mix(p.second.effects.frameWords);
        d.Mix(static_cast<uint32_t>(p.second.effects.spDelta));
    }
    d.Mix(request.registerCallees);
    d.Mix(request.maxInstructions);
    d.Mix(request.volatileMemory.size());
    for (const auto& r : request.volatileMemory)
    {
        d.Mix(r.first);
        d.Mix(r.second);
    }
    d.Mix(request.allMemoryVolatile);
    RequestKey k;
    k.lo = d.a;
    k.hi = d.b;
    return k;
}

const size_t DecompilerRunner::kWorkerStackBytes;

struct DecompilerRunner::WorkerThread
{
    // Starts runner->WorkerMain() with kWorkerStackBytes of stack; throws std::system_error as
    // std::thread would. Defined at the end of the file, after the platform headers.
    explicit WorkerThread(DecompilerRunner* runner);
    void Join();

    DecompilerRunner* runner;
#ifdef _WIN32
    void* handle = nullptr;
#else
    pthread_t id;
#endif
    bool joinable = false;

#ifdef _WIN32
    static unsigned __stdcall Entry(void* self);
#else
    static void* Entry(void* self);
#endif
    void Run()
    {
        // As std::thread: an exception escaping the thread ends the process (WorkerMain catches
        // everything it can recover from).
        try
        {
            runner->WorkerMain();
        }
        catch (...)
        {
            std::terminate();
        }
    }
};

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
        if (gFailThreadStart)
            throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again),
                                    "the worker thread could not be started (test)");
        mThread.reset(new WorkerThread(this));
    }
    catch (...)
    {
        // No worker, so no destructor will release the claim: release it here, or every later
        // runner would be born disabled with no engine thread anywhere.
        gEngineOwned = false;
        throw;
    }
}

std::unique_ptr<DecompilerRunner> DecompilerRunner::Start(const std::string& configDir, std::string& why,
                                                         RunnerOptions options) noexcept
{
    try
    {
        why.clear();
        return std::unique_ptr<DecompilerRunner>(new DecompilerRunner(configDir, options));
    }
    catch (const std::exception& e)
    {
        try
        {
            why = std::string("its worker could not be started: ") + e.what();
        }
        catch (...)
        {
        }
    }
    catch (...)
    {
        try
        {
            why = "its worker could not be started";
        }
        catch (...)
        {
        }
    }
    return nullptr;
}

void DecompilerRunner::SetThreadStartFailureForTests(bool fail)
{
    gFailThreadStart = fail;
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
    if (mThread) mThread->Join();
    if (mOwnsEngine)
    {
        ghidra::SleighArchitecture::shutdown();
        gEngineOwned = false;
    }
}

uint64_t DecompilerRunner::Submit(int slot, std::shared_ptr<const MemorySnapshot> snapshot, DecompileRequest request,
                                  bool retryTooLarge)
{
    // Hashed before the lock: the names can be a whole imported symbol file.
    const RequestKey memoKey = MakeRequestKey(snapshot ? snapshot->key : SnapshotKey(), request);
    std::lock_guard<std::mutex> lock(mMutex);
    if (mDisabled) return 0;
    const uint64_t generation = mNextGeneration++;
    mLatest[slot] = generation;

    // Whatever this slot asked before is obsolete now, whether or not this request runs.
    for (auto it = mQueue.begin(); it != mQueue.end();)
        it = it->slot == slot ? mQueue.erase(it) : it + 1;   // coalesced: never run
    if (mRunning && mRunningJob.slot == slot) RequestCancelLocked(CancelReason::Superseded);

    uint32_t entry = 0;
    // The memo answers the question that ran out of budget and no other: the key holds the snapshot
    // and every engine input, and the entry as asked (run through another mirror, PC-derived values
    // differ and so may the code reached).
    if (CanonicalAddress(request.entry, entry) && mTooLarge.count(memoKey) && !retryTooLarge)
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
    job.memoKey = memoKey;
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

void DecompilerRunner::SetRecordHookForTests(void (*hook)())
{
    gRecordHook = hook;
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
            if (gRecordHook) gRecordHook();

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
                if (job.epoch == mEpoch) mTooLarge.insert(job.memoKey);
            }
            else
            {
                // A run of this very request has completed (it fits, or failed, or was stopped): the
                // memo of an earlier run out of budget is overtaken and must not answer the next ask.
                mTooLarge.erase(job.memoKey);
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

// The platform thread, last: <windows.h> defines macros that must not reach the code above.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#endif

namespace sfe
{
namespace decomp
{
#ifdef _WIN32
unsigned __stdcall DecompilerRunner::WorkerThread::Entry(void* self)
{
    static_cast<WorkerThread*>(self)->Run();
    return 0;
}

DecompilerRunner::WorkerThread::WorkerThread(DecompilerRunner* r) : runner(r)
{
    const uintptr_t h = _beginthreadex(nullptr, static_cast<unsigned>(kWorkerStackBytes), &Entry, this,
                                       STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (h == 0)
        throw std::system_error(errno, std::generic_category(), "the worker thread could not be started");
    handle = reinterpret_cast<void*>(h);
    joinable = true;
}

void DecompilerRunner::WorkerThread::Join()
{
    if (!joinable) return;
    WaitForSingleObject(static_cast<HANDLE>(handle), INFINITE);
    CloseHandle(static_cast<HANDLE>(handle));
    joinable = false;
}
#else
void* DecompilerRunner::WorkerThread::Entry(void* self)
{
    static_cast<WorkerThread*>(self)->Run();
    return nullptr;
}

DecompilerRunner::WorkerThread::WorkerThread(DecompilerRunner* r) : runner(r)
{
    pthread_attr_t attr;
    int err = pthread_attr_init(&attr);
    if (err == 0)
    {
        err = pthread_attr_setstacksize(&attr, kWorkerStackBytes);
        if (err == 0) err = pthread_create(&id, &attr, &Entry, this);
        pthread_attr_destroy(&attr);
    }
    if (err != 0) throw std::system_error(err, std::generic_category(), "the worker thread could not be started");
    joinable = true;
}

void DecompilerRunner::WorkerThread::Join()
{
    if (!joinable) return;
    pthread_join(id, nullptr);
    joinable = false;
}
#endif

}  // namespace decomp
}  // namespace sfe
