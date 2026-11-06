// Area 9, round 6 (review): the area 9 fixer's changes for A9-45 to A9-50 attacked. A9-45 (number-led remarks skipped), A9-46 (the exit
// wait and the frame's no-wait settings try), A9-47 (the lock texts), A9-48 (the sync before the rename) and A9-50 (the two-process
// lock test's retry). Findings are in REVIEW_LEDGER.md (A9-52 and later); a check marked PIN asserts today's behaviour of a finding
// and must be turned round when the finding is fixed. A check marked RESIDUAL holds a case the fixer left open on purpose.
//
// Linux only (fork, flock, /proc). Runs in a scratch folder under the current directory; nothing outside it is touched.
// SE_A9R6_LONG=1 adds the exit with four stuck locks (about 40 s).
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
void SyncOrder()
{
    TakeEvents();
    std::string err;
    Check(WriteFileAtomically("order.txt", "06004000 main\n", 14, err), "write");
    const std::vector<std::string> ev = TakeEvents();
    for (const std::string& e : ev) std::printf("  %s\n", e.c_str());
    Check(ev.size() == 3 && ev[0].rfind("sync:", 0) == 0 && Has(ev[0], "order.txt.separt-") && ev[1].rfind("rename:", 0) == 0 &&
              Has(ev[1], "order.txt.separt-") && ev[2].rfind("syncdir:", 0) == 0,
          "A9-48: the staging file is synced before the rename, the folder after it");
}

// A9-48: a sync the file system refuses. An I/O error refuses the save with the reason, leaves the old file and no staging file, and
// keeps the change pending for the next save; a file system that cannot sync (EINVAL, ENOTSUP, EROFS) saves; a folder sync that fails
// is ignored (the file is already published).
void SyncRefused()
{
    Put("saturn_functions.txt", "06004000 main_loop\n");
    FunctionNames fn;
    fn.Load();
    fn.Rename(0x06004100u, "pending_name");
    gFailFileErrno = EIO;
    std::string err;
    const bool ok = fn.Save(&err);
    gFailFileErrno = 0;
    std::printf("  names save with fsync EIO: %s: %s\n", ok ? "saved" : "refused", err.c_str());
    Check(!ok && Has(err, "flush") && Has(err, "Input/output error"), "A9-48: an fsync I/O error refuses the save and says so");
    Check(Slurp("saturn_functions.txt") == "06004000 main_loop\n" && CountStaging("saturn_functions.txt") == 0,
          "A9-48: after a refused sync the old file is as it was and no staging file is left");
    Check(fn.Save(&err) && Has(Slurp("saturn_functions.txt"), "06004100 pending_name"), "A9-48: the next save writes the pending change");

    const int soft[] = { EINVAL, ENOTSUP, EROFS };
    for (int e : soft)
    {
        Settings s;
        s.Set("a", "k", std::to_string(e));
        gFailFileErrno = e;
        const bool saved = s.SaveTo("soft.ini", &err);
        gFailFileErrno = 0;
        Check(saved && Has(Slurp("soft.ini"), ("k = " + std::to_string(e)).c_str()), "A9-48: a file system that cannot sync (EINVAL, ENOTSUP, EROFS) still saves");
    }
    gFailDirErrno = EIO;
    {
        Settings s;
        s.Set("a", "dir", "1");
        const bool saved = s.SaveTo("dirfail.ini", &err);
        Check(saved && Has(Slurp("dirfail.ini"), "dir = 1"), "A9-48: a folder sync that fails is best effort; the file is published");
    }
    gFailDirErrno = 0;
    // A sync that fails on a file the store refused at load is never reached; a refused sync on the settings keeps the old settings.
    Put("keep.ini", "[a]\nx = 1\n");
    {
        Settings s;
        s.Set("a", "y", "2");
        gFailFileErrno = EIO;
        const bool saved = s.SaveTo("keep.ini", &err);
        gFailFileErrno = 0;
        Check(!saved && Slurp("keep.ini") == "[a]\nx = 1\n" && CountStaging("keep.ini") == 0, "A9-48: settings: a refused sync keeps the old file");
    }
}

// A9-48: what the two syncs cost per save here (the fixer did not measure). Printed, not checked: it depends on the disk.
void SyncCost()
{
    std::string body(4096, 'x');
    const int n = 40;
    auto t0 = std::chrono::steady_clock::now();
    std::string err;
    for (int i = 0; i < n; ++i) WriteFileAtomically("cost.txt", body.data(), body.size(), err);
    const double with = Since(t0) / n * 1000.0;
    std::printf("  a 4 KiB save with its two syncs: %.2f ms (mean of %d, in %s)\n", with, n, FdPath(open(".", O_RDONLY)).c_str());
    TakeEvents();
}

// A9-45: what an older version wrote. Until a735123a (2026-11-05) the names and notes were saved with %x (std::hex before 1338f12),
// unpadded, so a name at a BIOS ROM address (below 0x80000) is "1c0 name" or "2f84 name". Those are skipped now, counted (the Log
// warns and names the first line, and says to write eight digits), and kept in the file by a later save; the RAM names load.
void OldUnpaddedFile()
{
    Put("saturn_functions.txt", "1c0 bios_init\n2f84 bios_cd_read\n6004000 main_loop\n200400 low_wram_func\ndead dead_handler\n");
    FunctionNames fn;
    fn.Load();
    Check(!fn.HasName(0x1C0u) && !fn.HasName(0x2F84u) && fn.HasName(0x06004000u) && fn.HasName(0x200400u) && fn.SkippedLines() == 3 &&
              fn.FirstSkippedLine() == 1,
          "A9-45: of an older version's file the three BIOS names (digits and letters) are skipped and counted, the RAM names load");
    fn.Rename(0x06004100u, "added_now");
    std::string err;
    Check(fn.Save(&err), "save");
    const std::string now = Slurp("saturn_functions.txt");
    Check(Has(now, "1c0 bios_init\n") && Has(now, "2f84 bios_cd_read\n") && Has(now, "dead dead_handler\n") && Has(now, "06004100 added_now"),
          "A9-45: the skipped BIOS lines stay in the file through a save (nothing is lost on disk)");
    // Padded by the user, as the Log asks, they load.
    Put("saturn_functions.txt", "000001c0 bios_init\n00002f84 bios_cd_read\n0x1c4 bios_other\n");
    FunctionNames padded;
    padded.Load();
    Check(padded.HasName(0x1C0u) && padded.HasName(0x2F84u) && padded.HasName(0x1C4u) && padded.SkippedLines() == 0,
          "A9-45: written with eight digits or 0x, the BIOS names load");
    // A symbol map from another tool that writes short BIOS addresses: nothing comes in, every line is counted.
    Put("other_tool.sym", "1C0 bios_init\n2F84 bios_cd_read\n");
    FunctionNames imp;
    const size_t got = imp.Import("other_tool.sym", &err);
    Check(got == 0 && imp.SkippedLines() == 2 && err.empty(), "A9-45: Load Symbols of a map with short BIOS addresses imports none and counts both lines");
}

// A9-45, what is left open by nature: a remark that opens with an eight-digit date in the bounds file, and one that opens with a number at
// or above 0x80000 in the names or notes file (an older version wrote every RAM address that way, "250000" for 0x00250000).
void Residuals()
{
    Put("bounds.txt", "; starts\n20261104 starts found by the trace\n06004000\n");
    decomp::FunctionBounds fb;
    std::string err;
    fb.Load("bounds.txt", &err);
    std::printf("  bounds file, \"20261104 starts found by the trace\": %zu starts, %zu skipped\n", fb.Entries().size(), fb.SkippedLines());
    Check(fb.Entries().count(0x00261104u) == 1 && fb.SkippedLines() == 0,
          "RESIDUAL A9-45: a remark that opens with an eight-digit date is the start 0x00261104 (low work RAM), with no Log line");
    // The date with separators, as people write it, is skipped.
    Put("bounds.txt", "2026-11-04 trace\n2026/11/04 trace\n04.11.2026 trace\n20261104: trace\n");
    decomp::FunctionBounds sep;
    sep.Load("bounds.txt", &err);
    Check(sep.Entries().empty() && sep.SkippedLines() == 4, "A9-45: a date with separators or a colon is skipped");
    uint32_t a = 0;
    std::string n;
    Check(ParseAddressNameLine("250000 cycles per frame, measured", a, n) == AddressNameLine::Entry && a == 0x250000u,
          "RESIDUAL A9-45: in the names file a remark that opens with 250000 names 0x00250000 (an older version's form of that address)");
}

// A9-46: the exit. A store with nothing pending takes no lock, so a stuck holder costs nothing; one with a change waits up to 10 s and
// then says how long. SE_A9R6_LONG: four pending stores (settings, notes, names, starts) behind four stuck locks, in App::Shutdown's order.
void ExitWaits()
{
    FileLock::SetClosing(true);
    Put("saturn_functions.txt", "06004000 main_loop\n");
    {
        FunctionNames fn;
        fn.Load();
        const pid_t kid = HoldLockMs("saturn_functions.txt.lock", 13000);
        std::string err;
        auto t0 = std::chrono::steady_clock::now();
        Check(fn.Save(&err) && Since(t0) < 0.5, "A9-46: at exit a store with nothing pending is not held up by a stuck lock");
        fn.Rename(0x06004100u, "pending");
        t0 = std::chrono::steady_clock::now();
        const bool ok = fn.Save(&err);
        const double secs = Since(t0);
        std::printf("  exit save behind a stuck holder: %s after %.2f s: %s\n", ok ? "saved" : "refused", secs, err.c_str());
        Check(!ok && secs > 9.5 && secs < 12.0 && Has(err, "more than 10 seconds"), "A9-46: at exit a stuck holder refuses after 10 s and says so");
        kill(kid, SIGKILL);
        Reap(kid);
    }
    const char* longRun = std::getenv("SE_A9R6_LONG");
    if (longRun && *longRun == '1')
    {
        Put("ex.ini", "[a]\nx = 1\n");
        Put("ex_notes.txt", "06004000 note\n");
        Put("ex_names.txt", "06004000 main\n");
        Put("ex_bounds.txt", "06004000\n");
        const char* locks[] = { "ex.ini.lock", "ex_notes.txt.lock", "ex_names.txt.lock", "ex_bounds.txt.lock" };
        pid_t kids[4];
        for (int i = 0; i < 4; ++i) kids[i] = HoldLockMs(locks[i], 60000);
        Settings s;
        s.Set("a", "y", "2");
        AddressMap nb, nc, mb, mc;
        nc[0x06004100u] = "n";
        mc[0x06004100u] = "m";
        decomp::FunctionBounds fb;
        std::string err;
        fb.Load("ex_bounds.txt", &err);
        fb.SetEntry(0x06004200u);
        auto classify = [](const std::string& line, uint32_t& addr) { std::string nm; return ParseAddressNameLine(line, addr, nm) == AddressNameLine::Entry; };
        auto format = [](uint32_t addr, const std::string& nm) { char b[16]; std::snprintf(b, sizeof b, "%08x ", addr); return std::string(b) + nm + "\n"; };
        const auto t0 = std::chrono::steady_clock::now();
        int refused = 0;
        refused += !s.SaveTo("ex.ini", &err);
        refused += !SaveStore("ex_notes.txt", "notes", std::string(), 1u << 20, nb, nc, classify, format, std::string(), &err);
        refused += !SaveStore("ex_names.txt", "names", std::string(), 1u << 20, mb, mc, classify, format, std::string(), &err);
        refused += !fb.Save("ex_bounds.txt", &err);
        const double secs = Since(t0);
        std::printf("  exit with four stuck locks: %d of 4 refused after %.2f s\n", refused, secs);
        Check(refused == 4 && secs > 38.0, "A9-46: four stuck locks hold the exit for about 40 s (10 s each, one after the other)");
        for (pid_t k : kids) { kill(k, SIGKILL); Reap(k); }
    }
    FileLock::SetClosing(false);
}

// A9-46: the frame's settings save takes no wait. Another instance's ordinary save holds settings.ini.lock for a moment (one read and
// one write, now with two syncs); a frame that tries in that moment is refused and App reports "Could not save settings" as an error
// (status line and Log), where a save that waited would have taken the lock a few milliseconds later.
void FrameTryCollides()
{
    Put("frame.ini", "[a]\nx = 1\n");
    const pid_t kid = HoldLockMs("frame.ini.lock", 150);
    Settings s;
    s.Set("a", "y", "2");
    std::string err;
    const bool ok = s.SaveTo("frame.ini", &err, false);
    std::printf("  frame try during another instance's 150 ms save: %s: %s\n", ok ? "saved" : "refused", err.c_str());
    Check(!ok && Has(err, "another process holds"), "PIN A9-52: the frame's no-wait try is refused by another instance's ordinary save");
    Reap(kid);
    const pid_t kid2 = HoldLockMs("frame.ini.lock", 150);
    const auto t0 = std::chrono::steady_clock::now();
    const bool waited = s.SaveTo("frame.ini", &err, true);
    std::printf("  a waiting save in the same moment: %s after %.2f s\n", waited ? "saved" : "refused", Since(t0));
    Check(waited, "a save that waits takes the lock once the other instance's save is done");
    Reap(kid2);
}
}  // namespace

int main()
{
    char tmpl[] = "area9r6_XXXXXX";
    if (!mkdtemp(tmpl)) { std::printf("no temp folder\n"); return 2; }
    if (chmod(tmpl, 0755) != 0) return 2;
    if (chdir(tmpl) != 0) return 2;
    alarm(170);
    struct Step { const char* name; void (*fn)(); };
    const Step steps[] = {
        { "SyncOrder", SyncOrder },       { "SyncRefused", SyncRefused }, { "SyncCost", SyncCost },
        { "OldUnpaddedFile", OldUnpaddedFile }, { "Residuals", Residuals }, { "ExitWaits", ExitWaits },
        { "FrameTryCollides", FrameTryCollides },
    };
    for (const Step& s : steps) { std::printf("%s\n", s.name); s.fn(); }
    if (chdir("..") == 0)
    {
        std::string cmd = std::string("chmod -R u+rwx ") + tmpl + " 2>/dev/null; rm -rf " + tmpl;
        if (std::system(cmd.c_str()) != 0) {}
    }
    std::printf(gFail ? "Area9Round6Tests: %d FAILED\n" : "Area9Round6Tests: all passed\n", gFail);
    return gFail ? 1 : 0;
}
