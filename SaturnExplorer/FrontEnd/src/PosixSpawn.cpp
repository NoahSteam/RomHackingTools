#include "PosixSpawn.h"

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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

bool IsBundle(const std::string& p)
{
    return p.size() > 4 && p.compare(p.size() - 4, 4, ".app") == 0;
}
}  // namespace

bool SpawnDetached(const std::string& path, const std::vector<std::string>& args,
                   const std::string& workingDir, std::string& error)
{
    error.clear();
    if (path.empty())
    {
        error = "No program was given.";
        return false;
    }
    const bool bundle = IsBundle(path);

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
        const pid_t pid = ::fork();
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
        int status = 0;
        ::waitpid(pid, &status, 0);
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
    const pid_t first = ::fork();
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
        if (::fork() != 0) ::_exit(0);
        // The grandchild. A failure from here on is written as {stage, errno} and the pipe closes on
        // exit; a successful exec closes it (close-on-exec) without a byte, which the parent reads as
        // "it started".
        ::close(fds[0]);
        StdioToDevNull();
        int report[2];
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
    int status = 0;
    ::waitpid(first, &status, 0);   // the intermediate child exits at once; this only reaps it
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
    else
        error = path + ": " + std::strerror(report[1]);
    return false;
}

}  // namespace sfe

#endif
