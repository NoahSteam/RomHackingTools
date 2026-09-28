// DataSearchRunner's lifecycle: the part of the data search that was nine members of App and had
// no test at all (review finding UI-01).
//
// What is worth pinning is not that a search finds bytes -- PrsSearchTests covers that -- but the
// machine around it: one worker at a time, a second request cancels and queues rather than being
// dropped, a third replaces the queued one, each outcome reaches the caller exactly once carrying
// the routing tag its request had, and nothing hangs on the way out.
#include "DataSearchRunner.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) ::_mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) ::mkdir(p, 0777)
#endif

using namespace sfe;

namespace {

int gFail = 0;
void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; } }

const std::string kDir = "searchrunner_test_tmp";

// A file big enough that a PRS scan of it takes long enough to still be running by the time the
// next Start() call returns -- that is milliseconds of work against microseconds of bookkeeping,
// not a race the test has to win.
void WriteSlowFile(const std::string& name, size_t bytes)
{
    std::vector<char> data(bytes);
    std::mt19937 rng(4242);
    for (char& c : data) c = static_cast<char>(rng());
    std::ofstream f(kDir + "/" + name, std::ios::binary | std::ios::trunc);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

DataSearchRequest Request(int destination)
{
    DataSearchRequest r;
    r.roots = { kDir };
    r.needle = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
    r.compression = SearchCompression::Prs;   // the slow path, deliberately
    r.label = "request " + std::to_string(destination);
    r.scopeText = "the test directory";
    r.destination = destination;
    return r;
}

// Poll until an outcome arrives, or give up. Returns false on timeout rather than hanging the
// suite: a runner that never delivers is the failure, not a reason to wait forever.
bool WaitForOutcome(DataSearchRunner& runner, DataSearchOutcome& out)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (runner.Poll(out)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

// One search, start to finish: the outcome arrives once, carrying its own routing tag.
void TestOneSearchDelivered()
{
    DataSearchRunner runner;
    DataSearchOutcome out;
    Check(!runner.Running(), "idle before any request");
    Check(!runner.Poll(out), "an idle runner delivers nothing");

    runner.Start(Request(7));
    Check(WaitForOutcome(runner, out), "the outcome arrives");
    Check(out.destination == 7, "it carries the request's routing tag");
    Check(!out.cancelled, "an uninterrupted search is not cancelled");
    Check(!out.summary.empty(), "and has a summary to display");
    Check(!runner.Running(), "the runner is idle again");
    Check(!runner.Poll(out), "and does not deliver the same outcome twice");
}

// A second request while one runs: the first is cancelled and the second runs after it, so
// neither is silently dropped. Both outcomes reach the caller, in order, each with its own tag.
void TestSecondRequestCancelsAndQueues()
{
    DataSearchRunner runner;
    runner.Start(Request(1));
    runner.Start(Request(2));
    Check(runner.Running(), "still working after the second request");

    DataSearchOutcome first;
    Check(WaitForOutcome(runner, first), "the first outcome arrives");
    Check(first.destination == 1, "it is the first request's");
    Check(first.cancelled, "and it was cancelled to make way");

    DataSearchOutcome second;
    Check(WaitForOutcome(runner, second), "the queued search then runs and delivers");
    Check(second.destination == 2, "it is the second request's");
    Check(!second.cancelled, "and it ran to completion");
}

// A third request while one is queued replaces the queued one. Asking again means the latest
// question, not a backlog of them.
void TestThirdRequestReplacesTheQueuedOne()
{
    DataSearchRunner runner;
    runner.Start(Request(1));
    runner.Start(Request(2));
    runner.Start(Request(3));

    DataSearchOutcome out;
    Check(WaitForOutcome(runner, out), "the running search delivers");
    Check(out.destination == 1, "the first request finishes first");
    Check(WaitForOutcome(runner, out), "then the queued one");
    Check(out.destination == 3, "which is the third request, not the second");
    Check(!runner.Poll(out), "and nothing else is waiting");
}

// Stop() during a search returns rather than hanging, and leaves the runner idle -- which is what
// makes it safe to call from a shutdown path before the rest of the frontend goes away.
void TestStopDuringASearch()
{
    DataSearchRunner runner;
    runner.Start(Request(1));
    runner.Start(Request(2));   // a queued request must not be started by Stop()
    runner.Stop();
    Check(!runner.Running(), "idle after Stop");
    DataSearchOutcome out;
    Check(!runner.Poll(out), "and delivers nothing afterwards");
    runner.Stop();   // idempotent
}

// The destructor joins a running worker. If it did not, the thread would outlive the members it
// writes to -- which is the shape of the Windows shutdown bug this codebase already has on record
// (HOOK-01), one layer up.
void TestDestructorJoins()
{
    {
        DataSearchRunner runner;
        runner.Start(Request(1));
    }
    Check(true, "destruction with a search in flight returns");
}

}  // namespace

int main()
{
    MKDIR(kDir.c_str());
    WriteSlowFile("SLOW.BIN", 6u << 20);

    TestOneSearchDelivered();
    TestSecondRequestCancelsAndQueues();
    TestThirdRequestReplacesTheQueuedOne();
    TestStopDuringASearch();
    TestDestructorJoins();

#ifndef _WIN32
    (void)!std::system(("rm -rf '" + kDir + "'").c_str());
#endif
    if (gFail == 0) std::printf("All DataSearchRunner tests passed.\n");
    return gFail ? 1 : 0;
}
