#include "PosixSpawn.h"

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <thread>

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace sfe
{

namespace
{
// A descriptor number the child's stdio redirection cannot overwrite: dup2(devnull, 0..2) would
// clobber a pipe end that happened to land there when the parent has its own stdio closed.
int MoveAboveStdio(int fd)
{
    if (fd < 0 || fd > 2) return fd;
    const int moved = ::fcntl(fd, F_DUPFD, 3);
    ::close(fd);
    return moved;
}

void SetCloseOnExec(int fd)
{
    const int flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0) ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void StdioToDevNull()
{
    const int devnull = ::open("/dev/null", O_RDWR);
    if (devnull >= 0)
    {
        ::dup2(devnull, 0); ::dup2(devnull, 1); ::dup2(devnull, 2);
        if (devnull > 2) ::close(devnull);
    }
}

// A folder picked in a file dialog arrives as "Foo.app/"; the slash must not hide the bundle.
std::string TrimTrailingSlashes(std::string p)
{
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

bool IsBundle(const std::string& p)
{
    return p.size() > 4 && p.compare(p.size() - 4, 4, ".app") == 0;
}

// The command-line launcher some applications ship inside their bundle. `open -a Foo --args ...` only
// delivers its arguments to a fresh instance -- an application that is already running ignores
// them, which for a diff tool means "opens, and shows nothing" -- and the bundle's main executable
// is not a way in either (Beyond Compare's does not forward them). The helper is what talks to the
// running instance. Empty when the bundle has none.
std::string BundleCliHelper(const std::string& bundle)
{
    static const char* const kHelpers[] = { "bcomp" };   // Beyond Compare
    for (const char* name : kHelpers)
    {
        const std::string candidate = bundle + "/Contents/MacOS/" + name;
        if (::access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    return std::string();
}
}  // namespace

bool SpawnDetached(const std::string& path, const std::vector<std::string>& args,
                   const std::string& workingDir, std::string& error)
{
    SpawnSyscalls real;
    real.fork = [] { return static_cast<int>(::fork()); };
    real.waitpid = [](int pid, int* status, int options) { return static_cast<int>(::waitpid(pid, status, options)); };
    return SpawnDetachedWith(real, path, args, workingDir, error);
}

bool SpawnDetachedWith(const SpawnSyscalls& sys, const std::string& pathGiven, const std::vector<std::string>& args,
                       const std::string& workingDir, std::string& error)
{
    error.clear();
    if (pathGiven.empty())
    {
        error = "No program was given.";
        return false;
    }
    std::string path = TrimTrailingSlashes(pathGiven);
    bool bundle = IsBundle(path);
    if (bundle)
    {
        const std::string helper = BundleCliHelper(path);
        if (!helper.empty())
        {
            path = helper;   // run it as the plain program it is
            bundle = false;
        }
    }

    // Everything the child needs is built before the fork: nothing allocates after it.
    std::vector<std::string> words;
    if (bundle)
    {
        words = { "/usr/bin/open", "-a", path };
        if (!args.empty())
        {
            words.push_back("--args");
            words.insert(words.end(), args.begin(), args.end());
        }
    }
    else
    {
        words.push_back(path);
        words.insert(words.end(), args.begin(), args.end());
    }
    std::vector<char*> argv;
    for (std::string& w : words) argv.push_back(&w[0]);
    argv.push_back(nullptr);
    const char* program = bundle ? "/usr/bin/open" : path.c_str();
    const char* dir = workingDir.empty() ? nullptr : workingDir.c_str();

    if (bundle)
    {
        // `open` hands the application to LaunchServices and exits straight away, so it is waited on
        // and its exit status is the answer.
        const pid_t pid = sys.fork();
        if (pid < 0)
        {
            error = std::string("fork failed: ") + std::strerror(errno);
            return false;
        }
        if (pid == 0)
        {
            StdioToDevNull();
            if (dir && ::chdir(dir) != 0) ::_exit(126);
            ::execv(program, argv.data());
            ::_exit(127);
        }
        // A signal can interrupt the wait: retry. Any other failure means the outcome is unknown, and
        // an unknown outcome is not a success (status would still be 0, which reads as one).
        int status = 0;
        pid_t waited;
        do { waited = sys.waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
        if (waited != pid)
        {
            error = path + ": could not wait for `open`: " + std::strerror(errno);
            return false;
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
        error = path + ": `open` could not start the application"
                + (WIFEXITED(status) ? " (exit " + std::to_string(WEXITSTATUS(status)) + ")" : std::string());
        return false;
    }

    int fds[2];
    if (::pipe(fds) != 0)
    {
        error = std::string("pipe failed: ") + std::strerror(errno);
        return false;
    }
    fds[0] = MoveAboveStdio(fds[0]);
    fds[1] = MoveAboveStdio(fds[1]);
    SetCloseOnExec(fds[0]);
    SetCloseOnExec(fds[1]);

    // Fork twice so the tool is reparented to init: nothing here waits on it, and a single fork would
    // leave a zombie for as long as this program runs.
    const pid_t first = sys.fork();
    if (first < 0)
    {
        error = std::string("fork failed: ") + std::strerror(errno);
        ::close(fds[0]);
        ::close(fds[1]);
        return false;
    }
    if (first == 0)
    {
        ::setsid();
        int report[2];
        const pid_t second = sys.fork();
        if (second < 0)
        {
            // Not the success path: nothing was started. Tell the parent, which would otherwise read
            // this exit as "the program is running".
            report[0] = 3; report[1] = errno;
            (void)!::write(fds[1], report, sizeof(report));
            ::_exit(127);
        }
        if (second != 0) ::_exit(0);
        // The grandchild. A failure from here on is written as {stage, errno} and the pipe closes on
        // exit; a successful exec closes it (close-on-exec) without a byte, which the parent reads as
        // "it started".
        ::close(fds[0]);
        StdioToDevNull();
        if (dir && ::chdir(dir) != 0)
        {
            report[0] = 1; report[1] = errno;
            (void)!::write(fds[1], report, sizeof(report));
            ::_exit(127);
        }
        ::execvp(program, argv.data());
        report[0] = 2; report[1] = errno;
        (void)!::write(fds[1], report, sizeof(report));
        ::_exit(127);
    }

    ::close(fds[1]);
    // The intermediate child exits at once; this only reaps it. A failed wait still falls through to the
    // pipe, which is the real answer: a pending read below blocks until every writer has gone.
    int status = 0;
    pid_t waited;
    do { waited = sys.waitpid(first, &status, 0); } while (waited < 0 && errno == EINTR);
    (void)waited;
    int report[2] = { 0, 0 };
    size_t got = 0;
    while (got < sizeof(report))
    {
        const ssize_t n = ::read(fds[0], reinterpret_cast<char*>(report) + got, sizeof(report) - got);
        if (n > 0) got += static_cast<size_t>(n);
        else if (n < 0 && errno == EINTR) continue;
        else break;
    }
    ::close(fds[0]);
    if (got < sizeof(report)) return true;   // end of file with nothing written: the exec happened

    if (report[0] == 1)
        error = "Could not use " + workingDir + " as the working directory: " + std::strerror(report[1]);
    else if (report[0] == 3)
        error = path + ": could not start it (fork failed: " + std::strerror(report[1]) + ")";
    else
        error = path + ": " + std::strerror(report[1]);
    return false;
}


namespace
{
// A path with a '/' that is not absolute, made absolute against the current directory.
std::string Absolutize(const std::string& path)
{
    if (path.empty() || path[0] == '/' || path.find('/') == std::string::npos) return path;
    char cwd[4096];
    if (!::getcwd(cwd, sizeof(cwd))) return path;
    return std::string(cwd) + "/" + path;
}
}  // namespace

bool SpawnChild(const std::string& pathGiven, const std::vector<std::string>& args, const std::string& workingDir,
                const std::vector<EnvVar>& env, int& pid, std::string& error)
{
    error.clear();
    pid = -1;
    if (pathGiven.empty())
    {
        error = "No program was given.";
        return false;
    }
    const std::string path = Absolutize(pathGiven);
    if (!workingDir.empty())
    {
        struct stat st;
        if (::stat(workingDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        {
            error = "Could not use " + workingDir + " as the working directory: "
                    + std::strerror(::stat(workingDir.c_str(), &st) != 0 ? errno : ENOTDIR);
            return false;
        }
    }

    std::vector<std::string> words;
    words.push_back(path);
    words.insert(words.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& w : words) argv.push_back(&w[0]);
    argv.push_back(nullptr);

    // The inherited environment, minus anything 'env' sets, plus 'env'.
    std::vector<std::string> vars;
    for (char** e = environ; e && *e; ++e)
    {
        bool replaced = false;
        for (const EnvVar& v : env)
            if (std::strncmp(*e, v.name.c_str(), v.name.size()) == 0 && (*e)[v.name.size()] == '=') replaced = true;
        if (!replaced) vars.push_back(*e);
    }
    for (const EnvVar& v : env) vars.push_back(v.name + "=" + v.value);
    std::vector<char*> envp;
    for (std::string& v : vars) envp.push_back(&v[0]);
    envp.push_back(nullptr);

    // posix_spawn reports a failed exec or chdir as its return value, which is what lets a launch that
    // did not happen be reported as one (a fork + exec would only find out in the child).
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attr;
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawnattr_init(&attr);
    ::posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);   // survives SE, and terminal signals to SE miss it
    ::posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDWR, 0);
    ::posix_spawn_file_actions_adddup2(&actions, 0, 1);
    ::posix_spawn_file_actions_adddup2(&actions, 0, 2);
    // The _np spelling exists from macOS 10.15 / glibc 2.29; the plain one (POSIX 2024) only from macOS 26,
    // which would cut off every release before it. Deprecated there, still present.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    if (!workingDir.empty()) ::posix_spawn_file_actions_addchdir_np(&actions, workingDir.c_str());
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    pid_t child = 0;
    const int rc = (path.find('/') != std::string::npos)
                       ? ::posix_spawn(&child, path.c_str(), &actions, &attr, argv.data(), envp.data())
                       : ::posix_spawnp(&child, path.c_str(), &actions, &attr, argv.data(), envp.data());
    ::posix_spawn_file_actions_destroy(&actions);
    ::posix_spawnattr_destroy(&attr);
    if (rc != 0)
    {
        error = path + ": " + std::strerror(rc);
        return false;
    }
    pid = static_cast<int>(child);
    return true;
}

namespace
{
// True once 'pid' is no longer ours to signal: reaped now, or never a child of ours.
bool Reaped(int pid)
{
    int status = 0;
    const pid_t r = ::waitpid(pid, &status, WNOHANG);
    return r == pid || (r < 0 && errno != EINTR);
}

bool WaitUntilReaped(int pid, int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!Reaped(pid))
    {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}
}  // namespace

StopResult StopChild(int pid, int termGraceMs, int killGraceMs)
{
    if (pid <= 0) return StopResult::AlreadyGone;
    // Reap first: if it already exited it is a zombie holding the pid, and signalling after that
    // could hit a recycled pid.
    if (Reaped(pid)) return StopResult::AlreadyGone;
    ::kill(pid, SIGTERM);
    if (WaitUntilReaped(pid, termGraceMs)) return StopResult::Terminated;
    ::kill(pid, SIGKILL);
    if (WaitUntilReaped(pid, killGraceMs)) return StopResult::Killed;
    // Not even SIGKILL took it (stuck in the kernel). Leave the reaping to a thread so the caller is
    // not held and no zombie is left once it does go.
    std::thread([pid] { int status = 0; while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {} }).detach();
    return StopResult::Stuck;
}

}  // namespace sfe

#endif
