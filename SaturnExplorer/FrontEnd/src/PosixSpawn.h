// PosixSpawn — start an auxiliary program on macOS/Linux with an argument VECTOR, and find out whether
// it actually started.
//
// The launcher that was here handed one command line to /bin/sh. That broke two ways: a path holding
// `$`, a backtick or a quote was reinterpreted by the shell (config$X/A became config/A), and because
// the tool was started by a grandchild of a double fork, the parent only learned that the FIRST fork
// worked -- a missing program, a permission error or an unusable working directory still reported
// success. Here the program is exec'd directly with argv exactly as given, and the grandchild reports
// a failed chdir/exec back over a close-on-exec pipe (an exec that succeeds closes the pipe, which the
// parent reads as end of file).
//
// Free of ImGui and the platform layer so it can be tested; compiles to nothing off POSIX.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace sfe
{

// Run 'path' with 'args' (argv[1..]) in 'workingDir' (empty = leave it), detached: new session, stdio
// on /dev/null, reparented to init so it is never a zombie and outlives this program. A bare name is
// looked up on PATH. A macOS application bundle (Foo.app) is handed to `open -a` with `--args`.
// True once the program has started (for a bundle: once `open` has accepted it); false with 'error'
// filled -- naming the program and the reason -- when it did not.
bool SpawnDetached(const std::string& path, const std::vector<std::string>& args,
                   const std::string& workingDir, std::string& error);

// Start 'path' as a child this program KEEPS: the emulator, which a later relaunch must be able to stop.
// Unlike SpawnDetached it is not reparented, so 'pid' can be waited on and signalled (StopChild). The
// arguments reach it as an argv -- nothing is run through a shell -- with 'env' added to (or replacing
// entries in) the inherited environment as NAME=VALUE pairs. A path with a '/' is made absolute against
// the CURRENT directory before the working directory is applied, so "tools/emu" means the file the user
// meant and not one inside the child's folder; a bare name is looked up on PATH. The child gets its own
// session and stdio on /dev/null. False, with 'error' naming the program and the reason, when it did
// not start (missing or non-executable file, an unusable working directory).
struct EnvVar { std::string name, value; };
bool SpawnChild(const std::string& path, const std::vector<std::string>& args, const std::string& workingDir,
                const std::vector<EnvVar>& env, int& pid, std::string& error);

enum class StopResult
{
    AlreadyGone,   // it had exited (and is reaped now) before any signal was sent
    Terminated,    // it left after SIGTERM
    Killed,        // it ignored SIGTERM past the grace period and SIGKILL ended it
    Stuck,         // it would not die; a background thread will reap it whenever it does
};

// Stop a child started by SpawnChild: SIGTERM, wait up to 'termGraceMs', then SIGKILL and wait up to
// 'killGraceMs' more. The wait is polled, never open-ended, so a hung emulator cannot hold the caller
// (the UI thread, on relaunch) forever.
StopResult StopChild(int pid, int termGraceMs, int killGraceMs);

// The two system calls whose failure paths a test cannot otherwise reach (a second fork() that fails,
// a wait interrupted by a signal). SpawnDetached passes the real ones; a test substitutes its own.
struct SpawnSyscalls
{
    std::function<int()>                fork;      // fork(): the child's pid, 0 in the child, -1 + errno
    std::function<int(int, int*, int)>  waitpid;   // waitpid(pid, &status, options)
};
bool SpawnDetachedWith(const SpawnSyscalls& sys, const std::string& path, const std::vector<std::string>& args,
                       const std::string& workingDir, std::string& error);

}  // namespace sfe
