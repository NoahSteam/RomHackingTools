// Area 9, round 5 (review): the area 9 fixer's round 4 changes (A9-40 to A9-43) attacked, and the parts of area 9 the earlier rounds
// did not reach (a full disk mid-write, durability of the rename, paths with spaces and Unicode, files at the read cap). Findings
// A9-45 to A9-48 are fixed (REVIEW_LEDGER.md); each pin has been turned round into a check of the fixed behaviour.
//
// Linux only (fork, flock, setrlimit). Runs in a scratch folder under the current directory; nothing outside it is touched.
#include "Debug/CallStack.h"
#include "Decompiler/Sh2FunctionFinder.h"
#include "Decompiler/SpecBundle.h"
#include "Settings.h"
#include "StoreMerge.h"
#include "FileRead.h"
#include "FileWrite.h"
#include "AddressNameLine.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// Every call the product makes to flush a file to the device passes through here (FileWrite.cpp is compiled into this binary, so
// its calls bind to these definitions first): the count says whether a save asks for its bytes to be on disk before the rename.
static std::atomic<int> gSyncCalls(0);
extern "C" int fsync(int fd)
{
    ++gSyncCalls;
    using Fn = int (*)(int);
    static Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "fsync"));
    return real ? real(fd) : 0;
}
extern "C" int fdatasync(int fd)
{
    ++gSyncCalls;
    using Fn = int (*)(int);
    static Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "fdatasync"));
    return real ? real(fd) : 0;
}
extern "C" int syncfs(int fd)
{
    ++gSyncCalls;
    using Fn = int (*)(int);
    static Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "syncfs"));
    return real ? real(fd) : 0;
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
bool Has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }
double Since(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }

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

// A child that holds 'lockPath' (flock, as FileLock takes it) for 'secs' seconds; returns once it holds it. -1 on failure.
pid_t HoldLock(const std::string& lockPath, unsigned secs)
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
        sleep(secs);
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

// A9-45: a first word of decimal digits (or hex digits with a decimal digit among them) is an address, so a hand-written remark line
// that opens with a number is an entry: in saturn_function_bounds.txt a function start, in the names or notes file a name or a
// note, and no line is skipped, so the Log says nothing. A9-18 and A9-41 closed the case for words of the letters a-f; a year, a
// day of the month, a count or a date written YYYYMMDD opens a remark as often.
void DigitProseLines()
{
    uint32_t a = 0;
    std::string n;
    struct Line { const char* text; uint32_t addr; };
    const Line lines[] = {
        { "2026 trace: these starts are from the November run", 0x2026u },   // a year: an even BIOS ROM address
        { "12 Nov: from the trace", 0x12u },                                   // a day of the month
        { "2 more below, unconfirmed", 0x2u },                                 // a count
        { "20261104 starts found by the trace", 0x20261104u },                 // a date, eight digits: low work RAM, cache-through
        { "3D engine hooks below", 0x3Du },
    };
    int entries = 0;
    for (const Line& l : lines)
        if (ParseAddressNameLine(l.text, a, n) == AddressNameLine::Entry && a == l.addr) ++entries;
    // FIXED A9-45: a number below 0x80000 without 0x is skipped (0x2026, 0x12, 0x2, 0x3D); only the eight-digit date, which cannot be told
    // from an address, is still one.
    Check(entries == 1, "A9-45 fixed: of five remark lines that open with a number only the eight-digit date is an entry in a names file");

    // The bounds file, which the user edits by hand and where round 1 invited remarks: only eight digits or 0x are starts.
    Put("bounds.txt",
        "; Function entries set by hand\n"
        "2026 trace: these starts are from the November run\n"
        "06004000 main loop\n"
        "12 Nov: from the trace\n"
        "06004100\n"
        "3D engine hooks below\n"
        "0x06004200 written with the prefix\n"
        "6004300 older unpadded start\n");
    decomp::FunctionBounds fb;
    std::string err;
    Check(fb.Load("bounds.txt", &err), "bounds load");
    const auto& e = fb.Entries();
    Check(!e.count(0x2026u) && !e.count(0x12u) && !e.count(0x3Du) && e.count(0x06004000u) && e.count(0x06004100u) && e.count(0x06004200u) &&
              e.size() == 3 && fb.SkippedLines() == 4 && fb.FirstSkippedLine() == 2,
          "A9-45 fixed: in saturn_function_bounds.txt only eight digits or 0x are starts; the number-led remarks and the unpadded start are skipped and counted (4, the first is line 2)");
    // Clearing a start that was never made does not touch the remark.
    fb.ClearEntry(0x2026u);
    Check(fb.Save("bounds.txt", &err), "bounds save");
    Check(Has(Slurp("bounds.txt"), "2026 trace"), "A9-45 fixed: the remark line stays in the file");

    // The names file: the remark names nothing, and is counted.
    Put("saturn_functions.txt", "06004000 main_loop\n2026 trace: names from the November run\n06004100 init_vdp2\n");
    FunctionNames fn;
    fn.Load();
    Check(!fn.HasName(0x2026u) && fn.SkippedLines() == 1 && fn.NameOf(0x06004100u) == "init_vdp2",
          "A9-45 fixed: in saturn_functions.txt the remark names nothing and is counted as skipped");

    // What is right: the forms the application writes (eight digits) and an older version's unpadded address with a digit load.
    Check(ParseAddressNameLine("06004000 main", a, n) == AddressNameLine::Entry && a == 0x06004000u &&
              ParseAddressNameLine("6004000 main", a, n) == AddressNameLine::Entry && a == 0x06004000u,
          "eight digits and an older version's unpadded address load");
}

// A9-46 (fixed): at exit the lock is waited for up to 10 s (FileLock::SetClosing, which App::Shutdown calls first), so a holder that
// lets go within that time no longer costs the pending change; the settings retry in a frame takes the lock without waiting.
void RefusedAtExit()
{
    std::remove("saturn_functions.txt");
    std::remove("saturn_functions.txt.lock");
    Put("saturn_functions.txt", "06004000 main_loop\n");
    FileLock::SetClosing(true);
    {
        FunctionNames fn;
        fn.Load();
        fn.Rename(0x06004100u, "renamed_before_exit");
        const pid_t kid = HoldLock("saturn_functions.txt.lock", 5);
        Check(kid > 0, "holder started");
        std::string err;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = fn.Save(&err);   // App::Shutdown's SaveFunctionNames
        const double secs = Since(t0);
        std::printf("  exit save of the names: %s after %.2f s: %s\n", ok ? "saved" : "refused", secs, err.c_str());
        Check(ok && secs > 4.0 && secs < 8.0, "A9-46 fixed: the exit save waits past the 3 s of a session and writes once the holder has gone");
        Reap(kid);
    }
    FunctionNames next;
    next.Load();
    Check(next.HasName(0x06004100u), "A9-46 fixed: the rename pending at exit is in the file at the next start");

    // The same for the function starts (A9-43's exit save).
    std::remove("bounds.txt");
    Put("bounds.txt", "06004000\n");
    {
        decomp::FunctionBounds fb;
        std::string err;
        fb.Load("bounds.txt", &err);
        fb.SetEntry(0x06004200u);
        const pid_t kid = HoldLock("bounds.txt.lock", 4);
        const bool ok = fb.Save("bounds.txt", &err);
        Check(ok, "A9-46 fixed: the exit save of the starts waits and writes");
        Reap(kid);
    }
    FileLock::SetClosing(false);
    decomp::FunctionBounds back;
    std::string err;
    back.Load("bounds.txt", &err);
    Check(back.Entries().count(0x06004200u), "A9-46 fixed: the function start pending at exit is in the file at the next start");

    // A stuck holder at exit: refused after the longer bound, with the true reason (the app then says it in a box).
    {
        FileLock::SetClosing(true);
        FileLock probe("stuck.lock", true);   // free: held at once
        Check(probe.Held(), "a free lock is taken at once at exit");
        FileLock::SetClosing(false);
    }
    // The frame's settings retry takes the lock without waiting: a stuck holder costs the frame no time.
    {
        const pid_t kid = HoldLock("frame.ini.lock", 2);
        Settings s;
        s.Set("a", "b", "1");
        std::string e2;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = s.SaveTo("frame.ini", &e2, false);
        const double secs = Since(t0);
        Check(!ok && secs < 0.5 && Has(e2, "another process holds"),
              "A9-46 fixed: Settings::SaveTo(wait=false) against a held lock returns at once and says another process holds it");
        std::printf("  frame retry against a held lock: refused after %.3f s: %s\n", secs, e2.c_str());
        Reap(kid);
    }
}

// A9-40: a refused save keeps the change pending, and a later successful save writes it with the later change (names, starts,
// settings). A save made after a refusal must not drop what the refused one carried.
void LaterSaveCarriesRefused()
{
    std::remove("saturn_functions.txt");
    Put("saturn_functions.txt", "06004000 main_loop\n");
    FunctionNames fn;
    fn.Load();
    fn.Rename(0x06004100u, "first");
    pid_t kid = HoldLock("saturn_functions.txt.lock", 4);
    std::string err;
    Check(!fn.Save(&err), "names: refused while held");
    Reap(kid);
    fn.Rename(0x06004200u, "second");
    Check(fn.Save(&err), "names: the next save succeeds");
    const std::string t = Slurp("saturn_functions.txt");
    Check(Has(t, "06004100 first") && Has(t, "06004200 second") && Has(t, "06004000 main_loop"), "A9-40: the later save writes the refused rename too");

    Put("bounds.txt", "06004000\n");
    decomp::FunctionBounds fb;
    fb.Load("bounds.txt", &err);
    fb.SetEntry(0x06004100u);
    kid = HoldLock("bounds.txt.lock", 4);
    Check(!fb.Save("bounds.txt", &err), "starts: refused while held");
    Reap(kid);
    fb.ClearEntry(0x06004000u);
    Check(fb.Save("bounds.txt", &err), "starts: the next save succeeds");
    const std::string b = Slurp("bounds.txt");
    Check(Has(b, "06004100") && !Has(b, "06004000"), "A9-40: the later save of the starts writes the refused start and the later clear");

    Put("held.ini", "[a]\nx = 0\n");
    Settings s;
    s.Set("a", "first", "1");
    kid = HoldLock("held.ini.lock", 4);
    Check(!s.SaveTo("held.ini", &err), "settings: refused while held");
    Reap(kid);
    s.Set("a", "second", "2");
    Check(s.SaveTo("held.ini", &err), "settings: the next save succeeds");
    const std::string si = Slurp("held.ini");
    Check(Has(si, "first = 1") && Has(si, "second = 2") && Has(si, "x = 0"), "A9-40: the later settings save writes the refused key too");
}

// A long save by the lock's holder: how long a SaturnExplorer save holds the lock for a names file at the 8 MiB read cap. The bound
// (3 s) refuses the other instance's save only if a holder keeps it longer.
void HolderTime()
{
    std::string big;
    big.reserve(8u << 20);
    char line[64];
    for (uint32_t i = 0; big.size() + 40 < (8u << 20) - 64; ++i)
    {
        std::snprintf(line, sizeof line, "%08x function_number_%07u_xx\n", 0x06000000u + i * 2, i);
        big += line;
    }
    Put("saturn_functions.txt", big);
    FunctionNames fn;
    std::string err;
    Check(fn.Load(&err), "8 MiB names load");
    fn.Rename(0x06000000u, "renamed");
    const auto t0 = std::chrono::steady_clock::now();
    Check(fn.Save(&err), "8 MiB names save");
    const double secs = Since(t0);
    std::printf("  a save of a names file at the 8 MiB cap (%zu bytes) took %.3f s, the lock held for at most that\n", big.size(), secs);
    Check(secs < 3.0, "a save at the cap holds the lock well under the 3 s bound on this disk");
}

// A9-42 and the lock's other texts. (a) FileLock(wait=false) against a held lock gives the open-failure text with EWOULDBLOCK, not
// "another process holds": the branch for that text is never reached on POSIX. (b) SpecBundle's own refusal, after the bounded wait,
// still says the lock file "cannot be opened", the text A9-42 corrected in the stores.
void LockTexts()
{
    pid_t kid = HoldLock("x.lock", 2);
    {
        FileLock lock("x.lock", false);
        const std::string why = lock.WhyNotHeld();
        std::printf("  FileLock(wait=false) against a held lock: \"%s\"\n", why.c_str());
        Check(!lock.Held() && Has(why, "another process holds") && !Has(why, "cannot create or lock"),
              "A9-47 fixed: FileLock(wait=false) on a lock another process holds says \"another process holds ...\"");
    }
    Reap(kid);

    // SpecBundle on a fresh configuration folder while another process holds its lock for 5 s.
    mkdir("speccfg", 0755);
    mkdir("speccfg/decompiler", 0755);
    const std::string hash = decomp::SpecBundleHash(decomp::Sh2SpecFiles());
    kid = HoldLock("speccfg/decompiler/" + hash + ".lock", 5);
    const auto t0 = std::chrono::steady_clock::now();
    const decomp::SpecBundleResult r = decomp::MaterialiseSh2SpecBundle("speccfg");
    const double secs = Since(t0);
    std::printf("  SpecBundle with its lock held 5 s: ok=%d after %.2f s: %s\n", r.ok ? 1 : 0, secs, r.error.c_str());
    Check(!r.ok && secs > 2.5 && secs < 4.5, "A9-40: SpecBundle gives up after about 3 s on a folder it must create");
    Check(!Has(r.error, "cannot be opened") && Has(r.error, "more than 3 seconds"),
          "A9-47 fixed: SpecBundle's refusal after the timeout says another process held the lock for more than 3 seconds");
    Reap(kid);
    const decomp::SpecBundleResult again = decomp::MaterialiseSh2SpecBundle("speccfg");
    Check(again.ok, "SpecBundle: the next request, once the holder has gone, extracts and verifies (the caller retries)");
    // A verified directory is used without the lock: a stuck holder costs each request the 3 s wait, not the decompiler.
    kid = HoldLock("speccfg/decompiler/" + hash + ".lock", 5);
    const decomp::SpecBundleResult held = decomp::MaterialiseSh2SpecBundle("speccfg");
    Check(held.ok, "SpecBundle: a verified directory is used while another process holds the lock");
    Reap(kid);
}

// A9-48: the save renames the staging file over the data file without asking for its bytes to be on the device first, though
// FileWrite.h says the rename happens "once they are safely on disk". A power cut or a kernel crash shortly after a save can leave a
// file of zeros or no bytes in place of the whole names, notes, starts or settings file on a filesystem that does not order the
// data before the rename (XFS; ext4 with noauto_da_alloc or data=writeback; macOS without F_FULLFSYNC).
void NoSyncBeforeRename()
{
    // The hook itself: a call here is counted (so a count of 0 below means no call, not a hook that does not bind).
    gSyncCalls = 0;
    const int probe = open("probe.txt", O_WRONLY | O_CREAT, 0644);
    if (probe >= 0) { fsync(probe); close(probe); }
    Check(gSyncCalls.load() == 1, "the fsync hook counts a call");
    gSyncCalls = 0;
    std::string err;
    Check(WriteFileAtomically("sync.txt", "06004000 main\n", 14, err), "write");
    const int direct = gSyncCalls.load();
    Put("saturn_functions.txt", "06004000 main_loop\n");
    FunctionNames fn;
    fn.Load();
    fn.Rename(0x06004100u, "x");
    gSyncCalls = 0;
    Check(fn.Save(&err), "names save");
    const int store = gSyncCalls.load();
    Settings s;
    s.Set("a", "b", "1");
    gSyncCalls = 0;
    Check(s.SaveTo("sync.ini", &err), "settings save");
    const int ini = gSyncCalls.load();
    std::printf("  fsync/fdatasync/syncfs calls: WriteFileAtomically %d, a names save %d, a settings save %d\n", direct, store, ini);
    Check(direct >= 2 && store >= 2 && ini >= 2, "A9-48 fixed: each save syncs the staging file before the rename and the folder after it (at least 2 calls)");
}

// A full disk mid-write, as a file-size limit (EFBIG at the write or the close, the same path as ENOSPC). The data file must be as it
// was, no staging file left, the save refused with the OS reason, and the change pending for the next save.
void FullDiskMidWrite()
{
    std::string body;
    for (int i = 0; i < 400; ++i)
    {
        char l[48];
        std::snprintf(l, sizeof l, "%08x name_%04d\n", 0x06004000u + i * 4, i);
        body += l;
    }
    Put("saturn_functions.txt", body);
    Put("bounds.txt", "06004000\n");
    Put("full.ini", "[a]\nx = 0\n");
    const pid_t kid = fork();
    if (kid == 0)
    {
        signal(SIGXFSZ, SIG_IGN);
        struct rlimit rl;
        getrlimit(RLIMIT_FSIZE, &rl);
        const rlim_t old = rl.rlim_cur;
        rl.rlim_cur = 4096;   // the names file is 8 KB: the staging write fails half way
        if (setrlimit(RLIMIT_FSIZE, &rl) != 0) _exit(90);
        int bad = 0;
        FunctionNames fn;
        fn.Load();
        fn.Rename(0x06004000u, "renamed_on_a_full_disk");
        std::string err;
        if (fn.Save(&err)) bad |= 1;
        if (!Has(err, "File too large") && !Has(err, "No space")) bad |= 2;
        if (Slurp("saturn_functions.txt") != body) bad |= 4;
        if (CountStaging("saturn_functions.txt") != 0) bad |= 8;
        Settings s;
        s.Set("a", "big", std::string(8000, 'v'));
        if (s.SaveTo("full.ini", &err)) bad |= 16;
        if (Slurp("full.ini") != "[a]\nx = 0\n" || CountStaging("full.ini") != 0) bad |= 32;
        rl.rlim_cur = old;
        if (setrlimit(RLIMIT_FSIZE, &rl) != 0) _exit(91);
        if (!fn.Save(&err) || !Has(Slurp("saturn_functions.txt"), "06004000 renamed_on_a_full_disk")) bad |= 64;
        if (!s.SaveTo("full.ini", &err) || !Has(Slurp("full.ini"), "big = v")) bad |= 128;
        _exit(bad);
    }
    int st = 0;
    waitpid(kid, &st, 0);
    const int bad = WIFEXITED(st) ? WEXITSTATUS(st) : 255;
    if (bad) std::printf("  full disk: failure bits 0x%x\n", bad);
    Check(bad == 0, "full disk mid-write: refused with the OS reason, the file as it was, no staging file left, the change pending and written once space is back (names, settings)");
}

// A real ENOSPC on a small filesystem, when the run is given one (SE_A9R5_FULLDIR, a tmpfs of a few KB mounted by the caller).
void RealFullDisk()
{
    const char* dir = std::getenv("SE_A9R5_FULLDIR");
    if (!dir || !*dir) { std::printf("  (skipped: SE_A9R5_FULLDIR not set)\n"); return; }
    const std::string names = std::string(dir) + "/saturn_functions.txt";
    std::string body;
    for (int i = 0; i < 200; ++i)
    {
        char l[48];
        std::snprintf(l, sizeof l, "%08x name_%04d\n", 0x06004000u + i * 4, i);
        body += l;
    }
    Put(names, body);
    // Fill the rest of the filesystem.
    {
        std::ofstream f(std::string(dir) + "/filler", std::ios::binary);
        std::string chunk(1024, 'f');
        for (int i = 0; i < 1 << 16 && f; ++i) f << chunk;
    }
    AddressMap base, cur;
    for (int i = 0; i < 200; ++i) { char nm[16]; std::snprintf(nm, sizeof nm, "name_%04d", i); base[0x06004000u + i * 4] = nm; }
    cur = base;
    cur[0x06004000u] = "renamed_on_a_full_disk";
    auto classify = [](const std::string& line, uint32_t& a) { std::string n; return ParseAddressNameLine(line, a, n) == AddressNameLine::Entry; };
    auto format = [](uint32_t a, const std::string& n) { char b[16]; std::snprintf(b, sizeof b, "%08x ", a); return std::string(b) + n + "\n"; };
    std::string err;
    const bool ok = SaveStore(names, "The function names", std::string(), 8u << 20, base, cur, classify, format, std::string(), &err);
    std::printf("  ENOSPC save: %s: %s\n", ok ? "saved" : "refused", err.c_str());
    size_t staging = 0;
    if (DIR* d = opendir(dir))
    {
        while (dirent* e = readdir(d)) if (std::strstr(e->d_name, ".separt-")) ++staging;
        closedir(d);
    }
    Check(!ok && Has(err, "No space") && Slurp(names) == body && staging == 0, "ENOSPC: refused with the reason, the file as it was, no staging file left");
    std::remove((std::string(dir) + "/filler").c_str());
    Check(SaveStore(names, "The function names", std::string(), 8u << 20, base, cur, classify, format, std::string(), &err) &&
              Has(Slurp(names), "06004000 renamed_on_a_full_disk"),
          "ENOSPC: the pending change is written once there is space");
}

// Paths with spaces and Unicode, names and values in UTF-8: everything reads back as written.
void SpacesAndUnicode()
{
    const char* dir = "dir with spaces \xC3\xBC\xC3\xB1\xC3\xAF \xE3\x83\x87\xE3\x82\xA3\xE3\x83\xAC\xE3\x82\xAF\xE3\x83\x88\xE3\x83\xAA";
    mkdir(dir, 0755);
    if (chdir(dir) != 0) { Check(false, "chdir"); return; }
    const std::string uname = "\xE4\xB8\xBB\xE3\x83\xAB\xE3\x83\xBC\xE3\x83\x97 \xE2\x9C\x93 caf\xC3\xA9";   // "main loop" in Japanese, a check, cafe
    {
        FunctionNames fn;
        fn.Load();
        fn.Rename(0x06004000u, uname);
        fn.Rename(0x06004100u, "name\xE2\x80\xA8with a line separator");   // U+2028: not a line end to the loaders
        std::string err;
        Check(fn.Save(&err), "names save in a folder with spaces and Unicode");
    }
    FunctionNames back;
    back.Load();
    Check(back.NameOf(0x06004000u) == uname && back.NameOf(0x06004100u) == "name\xE2\x80\xA8with a line separator" && back.SkippedLines() == 0,
          "UTF-8 names (and U+2028) read back as written, in a folder with spaces and Unicode");
    decomp::FunctionBounds fb;
    std::string err;
    fb.Load("starts file.txt", &err);
    fb.SetEntry(0x06004000u);
    Check(fb.Save("starts file.txt", &err), "bounds save");
    decomp::FunctionBounds fb2;
    Check(fb2.Load("starts file.txt", &err) && fb2.Entries().count(0x06004000u), "starts read back in a folder with spaces and Unicode");
    Settings s;
    s.Set("decompiler", "bios", std::string("/home/u/BIOS images/Sega Saturn \xE3\x82\xBB\xE3\x82\xAC.bin"));
    Check(s.SaveTo("settings file.ini", &err), "settings save");
    // A UTF-8 byte-order mark in front of the names file (Windows editors): the first entry is read (A9-34's rule, in SplitLines).
    Put("saturn_functions.txt", "\xEF\xBB\xBF" "06004000 main_loop\n");
    FunctionNames bom;
    bom.Load();
    Check(bom.HasName(0x06004000u) && bom.SkippedLines() == 0, "a byte-order mark in front of the names file does not lose the first entry");
    if (chdir("..") != 0) {}
}

// Files at the read cap: a names file of exactly 8 MiB loads; one byte more is refused and no save replaces it; a rename that would
// take the file over the cap is refused with the reason and the file is untouched; a 7 MiB line (one name) loads and reads back.
void AtTheCap()
{
    const size_t cap = 8u << 20;
    std::string body = "06004000 ";
    body += std::string(cap - body.size() - 1, 'n');
    body += '\n';
    Put("saturn_functions.txt", body);
    FunctionNames fn;
    std::string err;
    Check(fn.Load(&err) && fn.NameOf(0x06004000u).size() == cap - 10, "a names file of exactly 8 MiB, one line, loads");
    fn.Rename(0x06004100u, "one_more");
    err.clear();
    Check(!fn.Save(&err) && Has(err, "over the") && Slurp("saturn_functions.txt").size() == cap, "a rename past the cap is refused with the reason; the file is untouched");
    Put("saturn_functions.txt", body + "x");
    FunctionNames over;
    err.clear();
    Check(!over.Load(&err) && Has(err, "larger than 8 MiB"), "one byte over the cap is refused at load, with the reason");
    over.Rename(0x06004000u, "x");
    Check(!over.Save(&err) && Slurp("saturn_functions.txt").size() == cap + 1, "and no save replaces the refused file");
}

}  // namespace

int main()
{
    char tmpl[] = "area9r5_XXXXXX";
    if (!mkdtemp(tmpl)) { std::printf("no temp folder\n"); return 2; }
    if (chmod(tmpl, 0755) != 0) return 2;
    if (chdir(tmpl) != 0) return 2;
    alarm(150);
    struct Step { const char* name; void (*fn)(); };
    const Step steps[] = {
        { "DigitProseLines", DigitProseLines },     { "RefusedAtExit", RefusedAtExit },       { "LaterSaveCarriesRefused", LaterSaveCarriesRefused },
        { "HolderTime", HolderTime },               { "LockTexts", LockTexts },               { "NoSyncBeforeRename", NoSyncBeforeRename },
        { "FullDiskMidWrite", FullDiskMidWrite },   { "RealFullDisk", RealFullDisk },         { "SpacesAndUnicode", SpacesAndUnicode },
        { "AtTheCap", AtTheCap },
    };
    for (const Step& s : steps) { std::printf("%s\n", s.name); s.fn(); }
    if (chdir("..") == 0)
    {
        std::string cmd = std::string("chmod -R u+rwx ") + tmpl + " 2>/dev/null; rm -rf " + tmpl;
        if (std::system(cmd.c_str()) != 0) {}
    }
    std::printf(gFail ? "Area9Round5Tests: %d FAILED\n" : "Area9Round5Tests: all passed\n", gFail);
    return gFail ? 1 : 0;
}
