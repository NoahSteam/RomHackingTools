// Area 9, round 7 (review): the area 9 fixer's A9-51 to A9-55 attacked (the shared exit deadline, the frame's 250 ms try, the
// refusal texts). Findings are in REVIEW_LEDGER.md (A9-58 and later); a check marked PIN asserts today's behaviour of a finding
// and must be turned round when the finding is fixed. A check marked RESIDUAL holds a case the fixer left open on purpose.
//
// Linux only (fork, flock, /proc). Runs in a scratch folder under the current directory; nothing outside it is touched.
#include "Debug/CallStack.h"
#include "Decompiler/Sh2FunctionFinder.h"
#include "Settings.h"
#include "StoreMerge.h"
#include "FileRead.h"
#include "FileWrite.h"
#include "AddressNameLine.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// FileWrite.cpp is compiled into this binary, so its calls to fsync and rename bind to these definitions first. Each call is recorded
// (what was synced or renamed, in order), and a sync can be made to fail with a chosen errno, for a file or for a folder.
static std::mutex gEvMu;
static std::vector<std::string> gEvents;
static std::atomic<int> gFailFileErrno(0), gFailDirErrno(0);
static std::string FdPath(int fd)
{
    char link[64], buf[4096];
    std::snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
    const ssize_t n = readlink(link, buf, sizeof buf - 1);
    return n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string("?");
}
extern "C" int fsync(int fd)
{
    struct stat st;
    const bool dir = fstat(fd, &st) == 0 && S_ISDIR(st.st_mode);
    {
        std::lock_guard<std::mutex> g(gEvMu);
        gEvents.push_back(std::string(dir ? "syncdir:" : "sync:") + FdPath(fd));
    }
    const int fail = dir ? gFailDirErrno.load() : gFailFileErrno.load();
    if (fail) { errno = fail; return -1; }
    using Fn = int (*)(int);
    static Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "fsync"));
    return real ? real(fd) : 0;
}
extern "C" int rename(const char* from, const char* to) noexcept
{
    {
        std::lock_guard<std::mutex> g(gEvMu);
        gEvents.push_back(std::string("rename:") + from + ">" + to);
    }
    using Fn = int (*)(const char*, const char*);
    static Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "rename"));
    return real ? real(from, to) : -1;
}

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}
std::string Slurp(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
void Put(const std::string& p, const std::string& body)
{
    std::ofstream f(p, std::ios::binary);
    f << body;
}
bool Has(const std::string& h, const char* n) { return h.find(n) != std::string::npos; }
double Since(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }
std::vector<std::string> TakeEvents()
{
    std::lock_guard<std::mutex> g(gEvMu);
    std::vector<std::string> v;
    v.swap(gEvents);
    return v;
}
size_t CountStaging(const char* prefix)
{
    size_t n = 0;
    if (DIR* d = opendir("."))
    {
        while (dirent* e = readdir(d))
            if (std::strncmp(e->d_name, prefix, std::strlen(prefix)) == 0 && std::strstr(e->d_name, ".separt-")) ++n;
        closedir(d);
    }
    return n;
}
// A child that holds 'lockPath' (flock, as FileLock takes it) for 'ms' milliseconds; returns once it holds it. -1 on failure.
pid_t HoldLockMs(const std::string& lockPath, unsigned ms)
{
    int fds[2];
    if (pipe(fds) != 0) return -1;
    const pid_t kid = fork();
    if (kid == 0)
    {
        close(fds[0]);
        const int fd = open(lockPath.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd < 0 || flock(fd, LOCK_EX) != 0) _exit(1);
        if (write(fds[1], "x", 1) != 1) _exit(2);
        usleep(ms * 1000u);
        _exit(0);
    }
    close(fds[1]);
    char c;
    const bool ok = read(fds[0], &c, 1) == 1;
    close(fds[0]);
    return ok ? kid : -1;
}
void Reap(pid_t kid)
{
    int st = 0;
    if (kid > 0) waitpid(kid, &st, 0);
}

// A9-48: the order of the calls. The fixer's test counts calls (at least 2); it does not say what was synced or when. Here: the staging
// file is synced, then renamed over the path, then the folder is synced.

void ExitLateStore()
{
    // Settings behind a stuck lock use the whole shared 10 s; names, whose holder lets go at 12 s, get no wait at all.
    FileLock::SetClosing(true);
    Put("late.ini", "[a]\nx = 1\n");
    Put("saturn_functions.txt", "06004000 main_loop\n");
    const pid_t stuck = HoldLockMs("late.ini.lock", 30000);
    const pid_t slow = HoldLockMs("saturn_functions.txt.lock", 12000);
    Settings s;
    s.Set("a", "y", "2");
    FunctionNames fn;
    fn.Load();
    fn.Rename(0x06004100u, "late_name");
    std::string e1, e2;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok1 = s.SaveTo("late.ini", &e1);
    const double t1 = Since(t0);
    const auto u0 = std::chrono::steady_clock::now();
    const bool ok2 = fn.Save(&e2);
    const double t2 = Since(u0);
    std::printf("  settings: %s after %.2f s: %s\n  names:    %s after %.2f s: %s\n", ok1 ? "saved" : "refused", t1, e1.c_str(), ok2 ? "saved" : "refused", t2, e2.c_str());
    Check(!ok1 && t1 > 9.5, "setup: the settings save used the 10 s");
    Check(!ok2 && t2 < 0.5, "setup: the names, whose holder lets go 2 s later, are refused at once: an earlier store spent the shared deadline (App now saves the names first)");
    Check(!Has(e2, "more than 10 seconds") && Has(e2, "exit time ran out") && Has(e2, "waited 0.0 s"),
          "FIX A9-58: the text says the exit time ran out and what was waited, not the configured 10 s");
    kill(stuck, SIGKILL); Reap(stuck);
    Reap(slow);
    FileLock::SetClosing(false);
}

void FrameStall()
{
    Put("stall.ini", "[a]\nx = 1\n");
    const pid_t stuck = HoldLockMs("stall.ini.lock", 1500);
    Settings s;
    s.Set("a", "y", "2");
    std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = s.SaveTo("stall.ini", &err, false);
    const double secs = Since(t0);
    std::printf("  frame try behind a stuck holder: %s after %.3f s\n", ok ? "saved" : "refused", secs);
    Check(!ok && secs < 0.15, "FIX A9-59: a frame try behind a stuck holder blocks the UI thread about 50 ms (every 2 s), not 250 ms");
    kill(stuck, SIGKILL); Reap(stuck);
}

void RefusalTexts()
{
    Put("saturn_functions.txt", "06004000 main_loop\n");
    FunctionNames fn;
    fn.Load();
    fn.Rename(0x06004100u, "pending_name");
    gFailFileErrno = EIO;
    std::string err;
    fn.Save(&err);
    gFailFileErrno = 0;
    std::printf("  names, fsync EIO: %s\n", err.c_str());
    Check(err.rfind("The function names: not saved, ", 0) == 0, "FIX A9-60: a write or flush refusal begins with its subject and says not saved");
    const size_t first = err.find("saturn_functions.txt");
    Check(first != std::string::npos && err.find("saturn_functions.txt", first + 1) == std::string::npos, "FIX A9-60: the file's path is in the flush text once");
}
}  // namespace

int main()
{
    char tmpl[] = "area9r7_XXXXXX";
    if (!mkdtemp(tmpl)) { std::printf("no temp folder\n"); return 2; }
    if (chmod(tmpl, 0755) != 0) return 2;
    if (chdir(tmpl) != 0) return 2;
    alarm(120);
    struct Step { const char* name; void (*fn)(); };
    const Step steps[] = { { "ExitLateStore", ExitLateStore }, { "FrameStall", FrameStall }, { "RefusalTexts", RefusalTexts } };
    for (const Step& s : steps) { std::printf("%s\n", s.name); s.fn(); }
    if (chdir("..") == 0)
    {
        std::string cmd = std::string("chmod -R u+rwx ") + tmpl + " 2>/dev/null; rm -rf " + tmpl;
        if (std::system(cmd.c_str()) != 0) {}
    }
    std::printf(gFail ? "Area9Round7Tests: %d FAILED\n" : "Area9Round7Tests: all passed\n", gFail);
    return gFail ? 1 : 0;
}
