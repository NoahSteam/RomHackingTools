// FileLock and the settings save that sits on it, on every platform (A12-19). The Area9Round3..7 tests pin the same code with
// forked processes, user namespaces and tmpfs, which only Linux has; this is the part that needs none of that. Two FileLocks in
// one process exclude each other on both flock (POSIX) and LockFileEx (Windows), so a second lock stands in for "another process".
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <unistd.h>
#endif

#include "FileWrite.h"
#include "Settings.h"

using namespace sfe;

namespace
{
int gFailures = 0;

void Check(bool ok, const char* what)
{
    if (ok) return;
    std::cerr << "CHECK failed: " << what << '\n';
    ++gFailures;
}

bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

std::string Slurp(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

double Since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

std::string MakeTempDir()
{
#ifdef _WIN32
    const char* t = std::getenv("TEMP");
    const int pid = _getpid();
#else
    const char* t = std::getenv("TMPDIR");
    const int pid = static_cast<int>(getpid());
#endif
    const std::string dir = std::string(t && *t ? t : ".") + "/__se_filelock_" + std::to_string(pid);
    Settings::EnsureDirectory(dir);
    return dir;
}

void TestExclusion(const std::string& dir)
{
    const std::string lock = dir + "/a.lock";
    {
        FileLock first(lock, false);
        Check(first.Held() && first.WhyNotHeld().empty(), "the first lock is held");

        FileLock second(lock, false);
        Check(!second.Held(), "a second lock on the same file is refused");
        Check(Has(second.WhyNotHeld(), "another process holds"), "the refusal says another process holds it");

        // A short try (A9-52) gives up after about its time and says the same.
        const auto t0 = std::chrono::steady_clock::now();
        FileLock tried(lock, false, 300);
        const double secs = Since(t0);
        Check(!tried.Held() && secs >= 0.25 && secs < 2.0, "a 300 ms try waits about 300 ms, then refuses");
    }
    FileLock again(lock, false);
    Check(again.Held(), "the lock is free again once the holder is gone");
}

void TestBoundedWait(const std::string& dir)
{
    // A9-40: a wait ends after kWaitMs and names the time.
    const std::string lock = dir + "/b.lock";
    FileLock holder(lock, false);
    Check(holder.Held(), "holder");
    const auto t0 = std::chrono::steady_clock::now();
    FileLock waiter(lock, true);
    const double secs = Since(t0);
    Check(!waiter.Held(), "the waiter gives up");
    Check(secs > 2.5 && secs < 5.0, "after about 3 s");
    Check(Has(waiter.WhyNotHeld(), "for more than 3 seconds"), "and the text names the bound");
}

void TestUnopenableFile(const std::string& dir)
{
    // A9-42: a file that cannot be created is refused with the OS reason, not "another process".
    FileLock lost(dir + "/no_such_folder/x.lock", false);
    Check(!lost.Held(), "a lock in a missing folder is not held");
    const std::string why = lost.WhyNotHeld();
    Check(Has(why, "cannot create or lock") && !Has(why, "another process"), "the reason is the OS's, not a holder");
    std::cout << "  missing folder: " << why << '\n';
}

void TestSettingsSaveAgainstALock(const std::string& dir)
{
    // A9-46: the frame's save takes the lock without waiting; a held lock refuses it at once and leaves the file alone.
    const std::string ini = dir + "/s.ini";
    Settings s;
    s.Set("a", "b", "1");
    std::string err;
    {
        FileLock holder(ini + ".lock", false);
        Check(holder.Held(), "settings lock holder");
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = s.SaveTo(ini, &err, false);
        Check(!ok && Since(t0) < 0.5 && Has(err, "another process holds"), "SaveTo(wait=false) is refused at once and says why");
        Check(Slurp(ini).empty(), "and wrote nothing");
    }
    err.clear();
    Check(s.SaveTo(ini, &err, false), "the next save succeeds");   // writes, flushes (A9-48) and publishes
    Check(Has(Slurp(ini), "b = 1") || Has(Slurp(ini), "b=1"), "and the key is in the file");
    std::remove(ini.c_str());
    std::remove((ini + ".lock").c_str());
}
}  // namespace

int main()
{
    const std::string dir = MakeTempDir();
    TestExclusion(dir);
    TestUnopenableFile(dir);
    TestSettingsSaveAgainstALock(dir);
    TestBoundedWait(dir);
    std::remove((dir + "/a.lock").c_str());
    std::remove((dir + "/b.lock").c_str());
#ifdef _WIN32
    _rmdir(dir.c_str());
#else
    rmdir(dir.c_str());
#endif
    if (gFailures != 0)
    {
        std::cerr << gFailures << " file lock check(s) failed\n";
        return 1;
    }
    std::cout << "File lock tests passed\n";
    return 0;
}
