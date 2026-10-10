// SpawnDetached -- the launcher behind LaunchTool on macOS/Linux. It used to hand one command line to
// /bin/sh and report success as soon as the first fork worked. These pin the two things that went
// wrong: arguments reaching the program literally (a `$` or a quote in a path was reinterpreted), and a
// launch that did not happen being reported as one that did.
#include "PosixSpawn.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace sfe;

namespace
{
int gFailures;

#define CHECK(cond) do {                                                      \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } \
} while (0)

std::string MakeTempDir()
{
    const char* t = std::getenv("TMPDIR");
    std::string tmpl = std::string(t && *t ? t : "/tmp") + "/se_spawn_test_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    return ::mkdtemp(buf.data()) ? std::string(buf.data()) : std::string();
}

void WriteFile(const std::string& path, const std::string& text, mode_t mode)
{
    std::ofstream(path, std::ios::binary) << text;
    ::chmod(path.c_str(), mode);
}

// The lines of 'path', once it exists and its writer has finished (the tool is detached, so the test
// has to wait for it).
bool WaitForLines(const std::string& path, size_t want, std::vector<std::string>& lines)
{
    for (int i = 0; i < 500; ++i)
    {
        std::ifstream in(path);
        lines.clear();
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
        if (lines.size() >= want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void TestArgumentsArriveLiterally(const std::string& dir)
{
    const std::string out = dir + "/argv.txt", tool = dir + "/tool.sh";
    // First line is where it ran; then one line per argument, then a sentinel (an empty argument must
    // still produce its own empty line).
    WriteFile(tool, "#!/bin/sh\nexec > \"" + out + ".tmp\"\npwd\nfor a in \"$@\"; do printf '%s\\n' \"$a\"; done\nprintf 'END\\n'\nmv \"" + out + ".tmp\" \"" + out + "\"\n", 0755);
    const std::vector<std::string> args = { "two words", "cfg$SE_UNSET/A", "`id`", "it's", "\"q\"", "", "{b}", "*", "a;b" };
    std::string error;
    CHECK(SpawnDetached(tool, args, dir, error));
    CHECK(error.empty());
    std::vector<std::string> lines;
    CHECK(WaitForLines(out, args.size() + 2, lines));
    if (lines.size() == args.size() + 2)
    {
        char* real = ::realpath(dir.c_str(), nullptr);
        CHECK(lines[0] == (real ? real : dir) || lines[0] == dir);   // the working directory was applied
        std::free(real);
        for (size_t i = 0; i < args.size(); ++i) CHECK(lines[1 + i] == args[i]);
        CHECK(lines.back() == "END");
    }
}

void TestFailuresAreReported(const std::string& dir)
{
    std::string error;
    CHECK(!SpawnDetached(dir + "/does/not/exist", {}, "", error));
    CHECK(error.find("does/not/exist") != std::string::npos);
    CHECK(error.find(std::strerror(ENOENT)) != std::string::npos);

    error.clear();
    CHECK(!SpawnDetached("se_definitely_no_such_tool_on_path", {}, "", error));   // a PATH lookup that finds nothing
    CHECK(error.find("se_definitely_no_such_tool_on_path") != std::string::npos);

    // Present, but not executable.
    const std::string plain = dir + "/not_executable";
    WriteFile(plain, "#!/bin/sh\n", 0644);
    error.clear();
    CHECK(!SpawnDetached(plain, {}, "", error));
    CHECK(error.find(std::strerror(EACCES)) != std::string::npos);

    // A working directory that cannot be entered.
    const std::string tool = dir + "/ok.sh";
    WriteFile(tool, "#!/bin/sh\nexit 0\n", 0755);
    error.clear();
    CHECK(!SpawnDetached(tool, {}, dir + "/no_such_folder", error));
    CHECK(error.find("working directory") != std::string::npos);

    // A macOS application bundle goes through `open`, which this host does not have.
    error.clear();
    if (::access("/usr/bin/open", X_OK) != 0)
    {
        CHECK(!SpawnDetached(dir + "/Some.app", {}, "", error));
        CHECK(!error.empty());
    }

    CHECK(!SpawnDetached("", {}, "", error) && !error.empty());
}

// The double fork exists so nothing is left to reap: a launched tool must not leave a child behind.
void TestNoZombie(const std::string& dir)
{
    const std::string tool = dir + "/quick.sh";
    WriteFile(tool, "#!/bin/sh\nexit 0\n", 0755);
    std::string error;
    CHECK(SpawnDetached(tool, {}, dir, error));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    int status = 0;
    errno = 0;
    CHECK(::waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);   // no child of ours, running or zombie
}
// The failure paths a real run cannot reach. A second fork() that fails used to take the same exit as a
// second fork that worked, and an interrupted wait left status at 0, which reads as "exited cleanly";
// both reported a launch that never happened as a success.
void TestSyscallFailures(const std::string& dir)
{
    const std::string tool = dir + "/ok2.sh";
    WriteFile(tool, "#!/bin/sh\nexit 0\n", 0755);
    std::string error;

    // The second fork (made by the first child) fails. The count is bumped before the call, so the
    // child that inherits it sees 2 on its own call.
    {
        static int forks;
        forks = 0;
        SpawnSyscalls sys;
        sys.fork = [] {
            if (++forks == 2) { errno = EAGAIN; return -1; }
            return static_cast<int>(::fork());
        };
        sys.waitpid = [](int pid, int* status, int options) { return static_cast<int>(::waitpid(pid, status, options)); };
        CHECK(!SpawnDetachedWith(sys, tool, {}, dir, error));
        CHECK(error.find("fork failed") != std::string::npos);
        CHECK(error.find(std::strerror(EAGAIN)) != std::string::npos);
        // Nothing may be left running or unreaped by the failed attempt.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        int status = 0;
        errno = 0;
        CHECK(::waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    }

    // The first fork fails.
    {
        SpawnSyscalls sys;
        sys.fork = [] { errno = EAGAIN; return -1; };
        sys.waitpid = [](int, int*, int) { return -1; };
        error.clear();
        CHECK(!SpawnDetachedWith(sys, tool, {}, dir, error));
        CHECK(error.find("fork failed") != std::string::npos);
    }

    // A bundle launch: `open` is stood in for by a program that exits 0 (a fake /usr/bin/open is not
    // available, so the child exec fails and exits 127 -- which is what makes the EINTR case
    // meaningful: a retried wait must still see the real status, not the zero it started as).
    {
        static int waits;
        waits = 0;
        SpawnSyscalls sys;
        sys.fork = [] { return static_cast<int>(::fork()); };
        sys.waitpid = [](int pid, int* status, int options) {
            if (++waits == 1) { errno = EINTR; return -1; }   // interrupted once, then the real wait
            return static_cast<int>(::waitpid(pid, status, options));
        };
        error.clear();
        const bool hasOpen = ::access("/usr/bin/open", X_OK) == 0;
        const bool ok = SpawnDetachedWith(sys, dir + "/Some.app", {}, "", error);
        CHECK(waits >= 2);                      // the interrupted wait was retried
        if (!hasOpen) CHECK(!ok && !error.empty());   // and the real status (127) was seen, not a stale 0
    }

    // A wait that fails for any other reason is not a success.
    {
        SpawnSyscalls sys;
        sys.fork = [] { return static_cast<int>(::fork()); };
        sys.waitpid = [](int pid, int* status, int options) {
            ::waitpid(pid, status, options);   // reap it so nothing leaks, then report failure
            errno = ECHILD;
            return -1;
        };
        error.clear();
        CHECK(!SpawnDetachedWith(sys, dir + "/Some.app", {}, "", error));
        CHECK(error.find("could not wait") != std::string::npos);
    }
}

// The emulator launcher: a child this program keeps, started without a shell. A ROM called
// Game$(touch marker).cue used to be expanded by /bin/sh; and a launch that could not happen (not
// executable, no such working directory, a relative program path) was reported as started.
void TestSpawnChildIsLiteralAndOwned(const std::string& dir)
{
    const std::string out = dir + "/child_argv.txt", marker = dir + "/injected";
    const std::string tool = dir + "/emu.sh";
    WriteFile(tool, "#!/bin/sh\nexec > \"" + out + ".tmp\"\npwd\nprintf '%s\\n' \"$MEDNAFEN_HOME\"\nfor a in \"$@\"; do printf '%s\\n' \"$a\"; done\nprintf 'END\\n'\nmv \"" + out + ".tmp\" \"" + out + "\"\n", 0755);
    const std::vector<std::string> args = { "Game$(touch " + marker + ").cue", "`touch " + marker + "`", "it's \"x\"", "a b", "" };
    int pid = -1;
    std::string error;
    CHECK(SpawnChild(tool, args, dir, { { "MEDNAFEN_HOME", "/some/home dir" } }, pid, error));
    CHECK(error.empty());
    CHECK(pid > 0);
    std::vector<std::string> lines;
    CHECK(WaitForLines(out, args.size() + 3, lines));
    if (lines.size() == args.size() + 3)
    {
        CHECK(lines[1] == "/some/home dir");                       // the environment override arrived
        for (size_t i = 0; i < args.size(); ++i) CHECK(lines[2 + i] == args[i]);
        CHECK(lines.back() == "END");
    }
    CHECK(::access(marker.c_str(), F_OK) != 0);                    // nothing was run through a shell
    // It is OUR child: it can be waited on (a detached grandchild could not be).
    int status = 0;
    CHECK(::waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

void TestSpawnChildFailuresAreReported(const std::string& dir)
{
    int pid = 0;
    std::string error;
    CHECK(!SpawnChild(dir + "/no/such/emulator", {}, "", {}, pid, error));
    CHECK(error.find("no/such/emulator") != std::string::npos);
    CHECK(error.find(std::strerror(ENOENT)) != std::string::npos);
    CHECK(pid == -1);

    const std::string plain = dir + "/child_plain";
    WriteFile(plain, "#!/bin/sh\n", 0644);
    error.clear();
    CHECK(!SpawnChild(plain, {}, "", {}, pid, error));
    CHECK(error.find(std::strerror(EACCES)) != std::string::npos);

    const std::string ok = dir + "/child_ok.sh";
    WriteFile(ok, "#!/bin/sh\nexit 0\n", 0755);
    error.clear();
    CHECK(!SpawnChild(ok, {}, dir + "/no_such_folder", {}, pid, error));
    CHECK(error.find("working directory") != std::string::npos);
    error.clear();
    CHECK(!SpawnChild(ok, {}, plain, {}, pid, error));              // a file is not a directory
    CHECK(error.find("working directory") != std::string::npos);

    CHECK(!SpawnChild("", {}, "", {}, pid, error) && !error.empty());
}

// "tools/emu" with a working directory of its own folder used to look for tools/tools/emu.
void TestSpawnChildResolvesRelativePaths(const std::string& dir)
{
    const std::string tools = dir + "/rel_tools", out = dir + "/rel_out.txt";
    std::system(("mkdir -p '" + tools + "'").c_str());
    WriteFile(tools + "/emu.sh", "#!/bin/sh\nprintf 'ran\\n' > \"" + out + "\"\n", 0755);
    char* here = ::getcwd(nullptr, 0);
    CHECK(::chdir(dir.c_str()) == 0);
    int pid = -1;
    std::string error;
    const bool ok = SpawnChild("rel_tools/emu.sh", {}, tools, {}, pid, error);   // working dir is the program's own
    CHECK(::chdir(here) == 0);
    std::free(here);
    CHECK(ok);
    CHECK(error.empty());
    std::vector<std::string> lines;
    CHECK(WaitForLines(out, 1, lines));
    if (pid > 0) { int st = 0; ::waitpid(pid, &st, 0); }
}

// A child that ignores SIGTERM used to hold the caller in a blocking waitpid for as long as it lived.
void TestStopChildIsBounded(const std::string& dir)
{
    const std::string ready = dir + "/stubborn_ready";
    const std::string tool = dir + "/stubborn.sh";
    WriteFile(tool, "#!/bin/sh\ntrap '' TERM\n: > \"" + ready + "\"\nwhile :; do sleep 1; done\n", 0755);
    int pid = -1;
    std::string error;
    CHECK(SpawnChild(tool, {}, dir, {}, pid, error));
    std::vector<std::string> lines;
    for (int i = 0; i < 500 && ::access(ready.c_str(), F_OK) != 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(::access(ready.c_str(), F_OK) == 0);                      // the trap is installed
    const auto t0 = std::chrono::steady_clock::now();
    const StopResult r = StopChild(pid, 150, 1000);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(r == StopResult::Killed);
    CHECK(ms >= 140 && ms < 1500);                                  // waited the grace period, then forced it
    CHECK(::kill(pid, 0) != 0);                                     // gone and reaped

    // One that leaves when asked.
    const std::string polite = dir + "/polite.sh";
    WriteFile(polite, "#!/bin/sh\nwhile :; do sleep 1; done\n", 0755);
    CHECK(SpawnChild(polite, {}, dir, {}, pid, error));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(StopChild(pid, 2000, 1000) == StopResult::Terminated);

    // One that had already exited, and a pid that was never ours.
    CHECK(SpawnChild(dir + "/ok2.sh", {}, dir, {}, pid, error));
    // Wait for it to be a zombie without reaping it -- the first run of a new script can take a while on a
    // loaded machine, so a fixed sleep is not a guarantee it has exited.
    for (int i = 0; i < 1000; ++i)
    {
        siginfo_t info = {};
        if (::waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOWAIT | WNOHANG) == 0 && info.si_pid == pid) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(StopChild(pid, 100, 100) == StopResult::AlreadyGone);
    CHECK(StopChild(-1, 100, 100) == StopResult::AlreadyGone);
}

// The ownership rule: an emulator that will not stop stays owned, and no replacement starts beside it.
// (A stop that reported "done" for a child still running let a second emulator launch while the first
// still held the live endpoint.)
void TestStuckChildBlocksTheReplacement()
{
    OwnedChild slot;
    std::string error;
    int spawned = 0;
    auto spawn = [&](int& pid, std::string&) { pid = 4000 + ++spawned; return true; };
    auto stops = [](int) { return StopResult::Terminated; };
    auto stuck = [](int) { return StopResult::Stuck; };

    CHECK(slot.Replace(stops, spawn, error));              // nothing owned yet: just starts
    CHECK(slot.Owns() && slot.Pid() == 4001 && spawned == 1);

    CHECK(!slot.Replace(stuck, spawn, error));             // it would not die
    CHECK(spawned == 1);                                   // so no second child was started
    CHECK(slot.Owns() && slot.Pid() == 4001);              // and the first is still the owned one
    CHECK(!error.empty());
    CHECK(!slot.Stop(stuck, error) && slot.Pid() == 4001); // a plain stop reports it too

    error.clear();
    CHECK(slot.Replace(stops, spawn, error));              // once it does exit, the replacement goes ahead
    CHECK(slot.Pid() == 4002 && spawned == 2 && error.empty());

    // A replacement that fails to start leaves nothing owned (the old one is already gone).
    auto failing = [](int&, std::string& e) { e = "no such program"; return false; };
    CHECK(!slot.Replace(stops, failing, error));
    CHECK(!slot.Owns() && error == "no such program");

    CHECK(slot.Stop(stuck, error));                        // nothing owned: stopping is trivially done
}
}  // namespace

// An application bundle that ships a command-line helper (Beyond Compare's bcomp) is run through the
// helper: `open -a --args` hands nothing to an instance that is already running. The path as a file
// dialog returns it ends in a slash, which used to hide the bundle and make the folder itself the
// program ("Permission denied").
void TestBundleUsesItsCommandLineHelper(const std::string& dir)
{
    const std::string bundle = dir + "/Tool.app", macos = bundle + "/Contents/MacOS", out = dir + "/bundle_argv.txt";
    std::system(("mkdir -p '" + macos + "'").c_str());
    WriteFile(macos + "/bcomp", "#!/bin/sh\nexec > \"" + out + ".tmp\"\nfor a in \"$@\"; do printf '%s\\n' \"$a\"; done\nmv \"" + out + ".tmp\" \"" + out + "\"\n", 0755);
    for (const std::string& given : { bundle, bundle + "/" })
    {
        std::remove(out.c_str());
        std::string error;
        CHECK(SpawnDetached(given, { "left folder", "right" }, "", error));
        CHECK(error.empty());
        std::vector<std::string> lines;
        CHECK(WaitForLines(out, 2, lines));
        if (lines.size() == 2) CHECK(lines[0] == "left folder" && lines[1] == "right");
    }
}

int main()
{
    const std::string dir = MakeTempDir();
    if (dir.empty()) { std::printf("could not make a temp directory\n"); return 2; }
    TestArgumentsArriveLiterally(dir);
    TestFailuresAreReported(dir);
    TestNoZombie(dir);
    TestSyscallFailures(dir);
    TestBundleUsesItsCommandLineHelper(dir);
    TestSpawnChildIsLiteralAndOwned(dir);
    TestSpawnChildFailuresAreReported(dir);
    TestSpawnChildResolvesRelativePaths(dir);
    TestStopChildIsBounded(dir);
    TestStuckChildBlocksTheReplacement();
    std::system(("rm -rf '" + dir + "'").c_str());
    if (gFailures == 0) std::printf("PosixSpawn: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
