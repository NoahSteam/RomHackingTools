// DecompilerRunner — the one engine thread, and the handshake between it and the UI (PLAN.md A4).
//
// The decompiler library keeps process-global state, above all the SLEIGH translator that every
// new Architecture resets, so exactly one thread ever touches the engine, for the life of the
// process. This class owns that thread. The UI thread submits requests and polls for results;
// it never waits on the engine except at exit.
//
//   Submit        queues a request for a requester ('slot', one per panel). A request still queued
//                 for the same slot is replaced (coalesced away before it runs); one running for
//                 the same slot is cancelled, since its result could no longer be installed.
//   Poll          once per UI frame. Hands back finished results that are still current: the
//                 newest generation for their slot, from the current epoch. Also enforces the
//                 wall-clock budget and the cancel-acknowledgement limit below.
//   InvalidateSession  on a source change, close or reconnect: bumps the epoch, clears the queue,
//                 cancels a running request. Never joins: the worker drops the abandoned function
//                 and takes the next request, and Poll drops anything from the old epoch.
//
// Cancellation is cooperative (patches/0001-cancel-flag.patch): honoured at the engine's next
// check-point, after the current uncheckable segment -- Architecture construction, one action's
// body, printing, teardown. So:
//   - The wall-clock budget (default 10 s) sets the cancel flag from Poll; the result comes back
//     "too large to decompile within N s", and that entry is not retried automatically this
//     session.
//   - If a cancel is not acknowledged within the limit (default 5 s), decompilation is disabled
//     for the session, with a reason naming the entry. No replacement worker is started: a second
//     engine thread would reset the translator under the stuck one.
//   - The destructor sets cancel and the epoch, joins unconditionally, then calls
//     SleighArchitecture::shutdown(). Exit therefore waits for the next check-point; a detached
//     worker would still be running while static objects are destroyed.
//
// Only one runner may be alive at a time (a second is born disabled). No ImGui.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "Decompiler/Sh2Decompiler.h"

namespace sfe
{
namespace decomp
{

struct RunnerOptions
{
    double budgetSeconds = 10.0;        // per request (setting)
    double cancelAckSeconds = 5.0;      // a cancel not acknowledged by then disables decompilation
};

class DecompilerRunner
{
public:
    explicit DecompilerRunner(std::string configDir, RunnerOptions options = RunnerOptions());
    ~DecompilerRunner();
    DecompilerRunner(const DecompilerRunner&) = delete;
    DecompilerRunner& operator=(const DecompilerRunner&) = delete;

    // Returns the request's generation, or 0 when decompilation is disabled. An entry that ran out
    // of budget earlier this session is not run again unless 'retryTooLarge' (an explicit
    // Refresh): Poll hands back its TooLarge result at once.
    uint64_t Submit(int slot, std::shared_ptr<const MemorySnapshot> snapshot, DecompileRequest request,
                    bool retryTooLarge = false);

    // Cancel the running or queued request for 'slot' (the panel's Stop). Its Cancelled result
    // still comes back through Poll.
    void Cancel(int slot);

    // UI thread, once per frame. True with 'out' filled for each current finished result; call
    // until false.
    bool Poll(DecompileResult& out);

    void InvalidateSession();

    bool Busy() const;
    uint64_t Epoch() const;
    bool Disabled() const;
    std::string DisabledReason() const;

    // Tests only: requests the worker has started running, and an observer of the engine's phases
    // (called on the worker; may block to hold the engine at the start of a phase). Set the
    // observer before submitting.
    uint64_t RunsStarted() const;
    void SetPhaseObserverForTests(std::function<void(DecompilePhase)> observer);

private:
    using Clock = std::chrono::steady_clock;

    struct Job
    {
        uint64_t generation = 0;
        uint64_t epoch = 0;
        int slot = 0;
        std::shared_ptr<const MemorySnapshot> snapshot;
        DecompileRequest request;
    };

    enum class CancelReason { None, User, Superseded, Session, Budget, Exit };

    void WorkerMain();
    void RequestCancelLocked(CancelReason reason);
    void DisableLocked(const std::string& reason);

    const std::string mConfigDir;
    const RunnerOptions mOptions;

    mutable std::mutex mMutex;
    std::condition_variable mWake;
    std::deque<Job> mQueue;
    std::deque<std::pair<uint64_t, DecompileResult>> mDone;   // (epoch, result)
    std::map<int, uint64_t> mLatest;            // slot -> newest generation submitted
    std::set<uint32_t> mTooLarge;               // entries that ran out of budget this epoch
    uint64_t mNextGeneration = 1;
    uint64_t mEpoch = 1;
    bool mStopping = false;
    bool mDisabled = false;
    std::string mDisabledReason;
    uint64_t mRunsStarted = 0;

    // The running job, if any.
    bool mRunning = false;
    Job mRunningJob;
    Clock::time_point mRunningSince;
    CancelReason mCancelReason = CancelReason::None;
    Clock::time_point mCancelRequestedAt;

    std::atomic<bool> mCancel{ false };          // the engine's flag (Architecture::cancelRequested)
    std::function<void(DecompilePhase)> mObserver;
    bool mOwnsEngine = false;                    // false when another runner was alive first

    std::thread mThread;
};

}  // namespace decomp
}  // namespace sfe
