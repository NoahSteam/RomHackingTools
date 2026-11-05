// Area 9, round 4 (review) and its fixer: the area 9 fixer's changes (A9-31 to A9-38) and the Windows run's A9-40, attacked. "PIN"
// asserts a finding as it was (REVIEW_LEDGER.md, A9-40 and later); the findings A9-40 to A9-42 are fixed and their checks are now
// "FIX" checks of the fixed behaviour. The rest hold what the fixer got right (a regression guard): the settings merge by lines, the stale staging
// cleanup, the blank-is-delete rule, a link loop.
//
// Linux only (fork, flock, setuid). Runs in a scratch folder under the current directory; nothing outside it is touched.
#include "Debug/CallStack.h"
#include "Decompiler/Sh2FunctionFinder.h"
#include "Settings.h"
#include "StoreMerge.h"
#include "FileRead.h"
#include "FileWrite.h"
#include "AddressNameLine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utime.h>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}
void Pin(bool holds, const char* what)
{
    // A pinned finding: printed either way so the run shows it, and a failure when the behaviour changed (fixed or moved).
    std::printf("  %s %s\n", holds ? "PIN holds:" : "PIN CHANGED:", what);
    if (!holds) ++gFail;
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
bool Exists(const std::string& p) { struct stat st; return ::lstat(p.c_str(), &st) == 0; }

auto kClassify = [](const std::string& line, uint32_t& a) {
    std::string n;
    return ParseAddressNameLine(line, a, n) == AddressNameLine::Entry;
};
auto kFormat = [](uint32_t a, const std::string& n) {
    char b[16];
    std::snprintf(b, sizeof b, "%08x ", a);
    return std::string(b) + n + "\n";
};

// A9-41 (A9-32's fix, undone): a word of the letters a-f alone in front of a line is a word, not an address, in every file: the
// prose lines of a hand-edited file ("add it later", "be careful", "fade routine", "bad idea", "dead code") are skipped and
// counted, so the Log warns, and the remark stays in the file. (A9-32's alternative was taken: the warning says the line may be an
// old unpadded address to be written with eight digits.)
void ProseLinesAreSkipped()
{
    uint32_t a = 0;
    std::string n;
    const char* prose[] = {
        "add it later",                    // A9-4's and A9-18's own example
        "be careful: the next one is a guess",
        "fade routine is near 06012000, not found yet",
        "bad idea to rename this",
        "dead code below",
        "face sprites are loaded by the next one",
    };
    int skipped = 0;
    for (const char* line : prose)
        if (ParseAddressNameLine(line, a, n) == AddressNameLine::Malformed) ++skipped;
    Check(skipped == 6, "FIX A9-41: six prose lines opening with a lower-case a-f word are skipped, not entries at 0xADD, 0xBE, 0xFADE, 0xBAD, 0xDEAD, 0xFACE");

    // The names file: the prose is no name, and it is counted, so App logs the warning.
    Put("saturn_functions.txt", "06004000 main_loop\nbe careful: the next one is a guess\n06004100 init_vdp2\nfade routine is near 06012000, not found yet\n");
    FunctionNames fn;
    fn.Load();
    Check(!fn.HasName(0xBEu) && !fn.HasName(0xFADEu) && fn.HasName(0x06004000u) && fn.HasName(0x06004100u) && fn.SkippedLines() == 2 && fn.FirstSkippedLine() == 2,
          "FIX A9-41: in saturn_functions.txt the prose lines are skipped and counted (the first is line 2)");

    // The bounds file (the one the user edits by hand, A9-4): the prose is no function start, and clearing a real one keeps the remark.
    Put("bounds.txt", "; Function entries set by hand\n06004000 main loop\nbe careful: the next one is a guess\n06004100\nfade in handler starts below\n");
    decomp::FunctionBounds fb;
    std::string err;
    Check(fb.Load("bounds.txt", &err), "bounds load");
    Check(!fb.Entries().count(0xBEu) && !fb.Entries().count(0xFADEu) && fb.Entries().count(0x06004000u) && fb.Entries().count(0x06004100u) && fb.SkippedLines() == 2,
          "FIX A9-41: in saturn_function_bounds.txt the prose lines are no function starts and are counted");
    fb.ClearEntry(0x06004000u);
    Check(fb.Save("bounds.txt", &err), "bounds save");
    Check(Has(Slurp("bounds.txt"), "be careful: the next one is a guess") && Has(Slurp("bounds.txt"), "fade in handler starts below") && !Has(Slurp("bounds.txt"), "main loop"),
          "FIX A9-41: a save keeps the user's remark lines");

    // Capitals, one letter and words with other letters: words, as before; an old unpadded address with a digit and the padded form load.
    Check(ParseAddressNameLine("Add it later", a, n) == AddressNameLine::Malformed && ParseAddressNameLine("a note", a, n) == AddressNameLine::Malformed &&
          ParseAddressNameLine("faded colours", a, n) == AddressNameLine::Malformed,
          "A9-32: a capital, a single letter, and a word with other letters stay words");
    Check(ParseAddressNameLine("0000DEAD dead_handler", a, n) == AddressNameLine::Entry && a == 0xDEADu && ParseAddressNameLine("0xdead h", a, n) == AddressNameLine::Entry,
          "A9-32: the way out the warning names: the same address written with eight digits (or 0x) is an entry");
}

// A9-33's refusal, from an ordinary user: the text (A9-42: with the OS reason) when the folder is not writable, when the folder is gone, and when the lock file
// belongs to someone else (sudo); and that the change is kept and goes out at the next save once the cause is removed.
void LockRefusal()
{
    mkdir("ro", 0755);
    int fds[2];
    if (pipe(fds) != 0) { Check(false, "pipe"); return; }
    const pid_t kid = fork();
    if (kid == 0)
    {
        close(fds[0]);
        auto say = [&](const std::string& s) { std::string t = s + "\n"; if (write(fds[1], t.data(), t.size()) < 0) {} };
        if (chdir("ro") != 0) _exit(10);
        Put("names.txt", "06004000 old\n");
        Put("other.txt", "06004000 old\n");
        Put("other.txt.lock", "");                                  // made by root (a sudo run): 0644, not ours
        if (chmod("other.txt.lock", 0644) != 0) _exit(11);
        if (::getuid() == 0)
        {
            if (::chown(".", 65534, 65534) != 0 || ::chown("names.txt", 65534, 65534) != 0 || ::chown("other.txt", 65534, 65534) != 0) _exit(12);
            if (::setgid(65534) != 0 || ::setuid(65534) != 0) _exit(13);
        }
        std::string err;
        // 1. The folder is not writable (a read-only share, a folder of another user): the lock file cannot be created.
        if (chmod(".", 0555) != 0) _exit(14);
        AddressMap base, cur;
        cur[0x06004100u] = "new";
        const bool ok1 = SaveStore("names.txt", "The function names", std::string(), 1u << 20, base, cur, kClassify, kFormat, std::string(), &err);
        say(std::string("readonly ") + (ok1 ? "saved" : "refused") + " " + err);
        if (chmod(".", 0755) != 0) _exit(15);
        // 2. The lock file is root's: refused, and the message names it.
        err.clear();
        AddressMap base2, cur2;
        cur2[0x06004100u] = "new";
        const bool ok2 = SaveStore("other.txt", "The function names", std::string(), 1u << 20, base2, cur2, kClassify, kFormat, std::string(), &err);
        say(std::string("rootlock ") + (ok2 ? "saved" : "refused") + " " + err);
        // 3. The user removes it (the folder is theirs) and the next save sends the pending change.
        if (unlink("other.txt.lock") != 0) _exit(16);
        err.clear();
        const bool ok3 = SaveStore("other.txt", "The function names", std::string(), 1u << 20, base2, cur2, kClassify, kFormat, std::string(), &err);
        say(std::string("retry ") + (ok3 ? "saved" : "refused") + " " + Slurp("other.txt"));
        _exit(0);
    }
    close(fds[1]);
    std::string got;
    char buf[512];
    ssize_t r;
    while ((r = read(fds[0], buf, sizeof buf)) > 0) got.append(buf, static_cast<size_t>(r));
    close(fds[0]);
    int st = 0;
    waitpid(kid, &st, 0);
    Check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "lock refusal child ran");
    std::printf("  %s", got.c_str());
    // 1: refused (nothing could be saved there anyway), and the text gives the OS reason.
    const size_t l1 = got.find("readonly ");
    const std::string line1 = l1 == std::string::npos ? std::string() : got.substr(l1, got.find('\n', l1) - l1);
    Check(Has(line1, "refused"), "A9-33: a folder the user cannot write refuses the save");
    Check(Has(line1, "names.txt.lock") && Has(line1, "ermission denied") && !Has(line1, "refused lock") && Has(line1, "not saved"),
          "FIX A9-42: in a folder the user cannot write the refusal gives the OS reason (Permission denied)");
    Check(Has(got, "rootlock refused") && Has(got, "other.txt.lock"), "A9-33: a lock file of another user refuses the save and names the file");
    Check(Has(got, "retry saved") && Has(got, "06004100 new"), "A9-33: the refused change is kept and is written by the next save once the lock file is gone");

    // 4. A folder that is gone (the working folder removed or unmounted while the app runs): the text has ENOENT.
    std::string err;
    AddressMap base, cur;
    cur[0x06004100u] = "new";
    Check(!SaveStore("gone/names.txt", "The function names", std::string(), 1u << 20, base, cur, kClassify, kFormat, std::string(), &err), "A9-33: no folder, refused");
    Check(Has(err, "gone/names.txt.lock") && Has(err, "No such file") && !Has(err, "refused lock"),
          "FIX A9-42: when the folder is gone the refusal gives the OS reason (No such file or directory)");
}

// A9-40: a save waits for the lock for FileLock::kWaitMs (3 s) at most. Another process holds <file>.lock for five seconds; SaveStore and
// Settings::SaveTo each give up after the bound, refuse the save (A9-33's path: the text names the lock file and the wait, the file is
// left as it is, the session keeps the change pending), and save the same change once the holder has let go.
struct TimedSave
{
    double secs = -1;
    bool refused = false;
    std::string err;
    bool untouched = false;   // the data file as it was, the session's base not advanced
    bool retried = false;     // the same change saved after the holder let go
};
TimedSave TimedSaveWhileLocked(const std::string& lockPath, bool settings)
{
    TimedSave r;
    int fds[2];
    if (pipe(fds) != 0) return r;
    const pid_t kid = fork();
    if (kid == 0)
    {
        close(fds[0]);
        const int fd = open(lockPath.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd < 0 || flock(fd, LOCK_EX) != 0) _exit(1);
        if (write(fds[1], "x", 1) != 1) _exit(2);
        sleep(5);
        _exit(0);
    }
    close(fds[1]);
    char c;
    if (read(fds[0], &c, 1) != 1) { close(fds[0]); return r; }
    close(fds[0]);
    const auto t0 = std::chrono::steady_clock::now();
    bool ok;
    Settings s;
    AddressMap base, cur;
    const char* data = settings ? "held.ini" : "held.txt";
    Put(data, settings ? "[a]\nx = 0\n" : "06004100 old\n");
    if (settings)
        s.Set("a", "b", "1");
    else
        cur[0x06004000u] = "x";
    auto save = [&](std::string& err) {
        return settings ? s.SaveTo(data, &err)
                        : SaveStore(data, "t", std::string(), 1u << 20, base, cur, kClassify, kFormat, std::string(), &err);
    };
    ok = save(r.err);
    r.secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    r.refused = !ok;
    r.untouched = Slurp(data) == (settings ? "[a]\nx = 0\n" : "06004100 old\n") && (settings || base.empty());
    int st = 0;
    waitpid(kid, &st, 0);
    std::string err2;
    r.retried = save(err2) && Has(Slurp(data), settings ? "b = 1" : "06004000 x");
    return r;
}
void LockWait()
{
    const TimedSave a = TimedSaveWhileLocked("held.txt.lock", false);
    const TimedSave b = TimedSaveWhileLocked("held.ini.lock", true);
    std::printf("  SaveStore waited %.2f s, Settings::SaveTo %.2f s, for a lock another process held 5 s\n  %s\n  %s\n", a.secs, b.secs, a.err.c_str(), b.err.c_str());
    Check(a.secs > 2.5 && a.secs < 4.5 && b.secs > 2.5 && b.secs < 4.5, "FIX A9-40: SaveStore and Settings::SaveTo give up waiting for another process's lock after about 3 s");
    Check(a.refused && b.refused && Has(a.err, "held.txt.lock") && Has(a.err, "more than 3 seconds") && Has(b.err, "held.ini.lock") && Has(b.err, "kept in memory"),
          "FIX A9-40: the save is refused through A9-33's path, naming the lock file and the wait");
    Check(a.untouched && b.untouched, "FIX A9-40: the file is as it was and the session keeps its change pending");
    Check(a.retried && b.retried, "FIX A9-40: the next save, once the holder has let go, writes the pending change");
}

// A9-34: Settings::SaveTo merges by lines. Odd files: each must read back with the session's change and keep the rest.
struct Ini
{
    const char* name;
    const char* before;
};
std::string SaveAndReadBack(const std::string& body, const char* sec, const char* key, const char* value, Settings& back)
{
    mkdir("cfg", 0755);
    setenv("XDG_CONFIG_HOME", "cfg", 1);
    mkdir("cfg/SaturnExplorer", 0755);
    const std::string ini = "cfg/SaturnExplorer/settings.ini";
    Put(ini, body);
    Settings s;
    std::string err;
    s.Load(&err);
    s.Set(sec, key, value);
    if (!s.Save(&err)) std::printf("  save failed: %s\n", err.c_str());
    back = Settings();
    back.Load(&err);
    return Slurp(ini);
}
void SettingsMergeEdges()
{
    Settings back;
    std::string t;
    // A key written twice in one section: the loader takes the last; the change must win.
    t = SaveAndReadBack("[decompiler]\nbios = /a.bin\nbios = /b.bin\n", "decompiler", "bios", "/c.bin", back);
    Check(back.Get("decompiler", "bios") == "/c.bin" && !Has(t, "/b.bin"), "A9-34 merge: a duplicated key takes the change");
    // A section written twice; the key in the second only; a new key.
    t = SaveAndReadBack("[decompiler]\nautorefresh = 1\n[ui]\ntooltips = 1\n[decompiler]\nbios = /a.bin\n", "decompiler", "bios", "/c.bin", back);
    Check(back.Get("decompiler", "bios") == "/c.bin" && back.Get("decompiler", "autorefresh") == "1" && back.Get("ui", "tooltips") == "1",
          "A9-34 merge: a section written twice, the key in its second part");
    t = SaveAndReadBack("[decompiler]\nautorefresh = 1\n[ui]\ntooltips = 1\n[decompiler]\nbios = /a.bin\n", "decompiler", "volatile", "0x06000000", back);
    Check(back.Get("decompiler", "volatile") == "0x06000000" && back.Get("decompiler", "bios") == "/a.bin", "A9-34 merge: a new key in a section written twice");
    // The same key in two sections: only the session's section changes.
    t = SaveAndReadBack("[ui]\nbios = keep\n[decompiler]\nbios = /a.bin\n", "decompiler", "bios", "/c.bin", back);
    Check(back.Get("ui", "bios") == "keep" && back.Get("decompiler", "bios") == "/c.bin", "A9-34 merge: a key in two sections");
    // Odd spacing and case in the header and the key.
    t = SaveAndReadBack("  [ DeCompiler ]  \n\tBIOS\t=\t/a.bin\n; a comment\n", "decompiler", "bios", "/c.bin", back);
    Check(back.Get("decompiler", "bios") == "/c.bin" && Has(t, "; a comment") && Has(t, "[ DeCompiler ]"), "A9-34 merge: odd spacing and case");
    // No line end at the end; CRLF; a byte-order mark and CRLF; a mark then a new section.
    t = SaveAndReadBack("[ui]\ntooltips = 1", "ui", "extra", "2", back);
    Check(back.Get("ui", "tooltips") == "1" && back.Get("ui", "extra") == "2", "A9-34 merge: no final line end");
    t = SaveAndReadBack("; win\r\n[decompiler]\r\nbios = /a.bin\r\n[ui]\r\ntooltips = 1\r\n", "decompiler", "bios", "/c.bin", back);
    Check(back.Get("decompiler", "bios") == "/c.bin" && back.Get("ui", "tooltips") == "1" && Has(t, "; win"), "A9-34 merge: CRLF");
    t = SaveAndReadBack("\xEF\xBB\xBF[decompiler]\r\nbios = /a.bin\r\n", "debug", "rewind", "1", back);
    Check(back.Get("decompiler", "bios") == "/a.bin" && back.Get("debug", "rewind") == "1" && !Has(t, "[]") && !Has(t, "\xEF\xBB\xBF"),
          "A9-34 merge: a byte-order mark, CRLF, and a new section");
    // A header with a remark after it is no header (in the loader and the merge alike): its keys stay in the section before.
    t = SaveAndReadBack("[ui]\ntooltips = 1\n[decompiler] ; mine\nbios = /a.bin\n", "decompiler", "bios", "/c.bin", back);
    Check(back.Get("decompiler", "bios") == "/c.bin" && back.Get("ui", "bios") == "/a.bin", "A9-34 merge: a header with a remark is read the same by both");
    // A key before any section.
    t = SaveAndReadBack("top = 1\n[ui]\ntooltips = 1\n", "ui", "tooltips", "0", back);
    Check(back.Get("", "top") == "1" && back.Get("ui", "tooltips") == "0", "A9-34 merge: a key before the first section");
}

// A9-37: RemoveStaleStagingFiles, from the cases the task names: a reused pid (a live process, fresh file), a live process with an
// old file (a slow save), another user's process (EPERM), a pid that overflows.
void StaleStagingEdges()
{
    Put("s.txt", "06004000 x\n");
    Put("s.txt.separt-1-1", "pid 1 is alive (a reused pid), fresh");                  // stays
    Put("s.txt.separt-1-2", "pid 1, eleven minutes old");                            // goes
    struct utimbuf old;
    old.actime = old.modtime = time(nullptr) - 660;
    utime("s.txt.separt-1-2", &old);
    Put("s.txt.separt-99999999999999999999-1", "a pid past pid_t");                // kill(-1, 0): alive, fresh: stays
    Put("s.txt.separt-0-1", "pid 0");                                                // goes (no such writer)
    Put("t.txt.separt-999999-1", "another file's staging");                          // stays (other file)
    Put("s.txt.separtx-999999-1", "not the form");                                   // stays
    const size_t n = RemoveStaleStagingFiles("s.txt");
    Check(n == 2 && Exists("s.txt.separt-1-1") && !Exists("s.txt.separt-1-2") && Exists("s.txt.separt-99999999999999999999-1") &&
          !Exists("s.txt.separt-0-1") && Exists("t.txt.separt-999999-1") && Exists("s.txt.separtx-999999-1"),
          "A9-37: a live pid's fresh file stays, an old one goes, an overflowing pid stays, another file's stays");
    // Every caller holds the lock: a live writer of the same file cannot have a staging file then. A writer that holds the lock
    // and is slow: its staging file is younger than its own lock hold, and no other saver runs the cleanup while it holds it.
    // Shown: a child holds the lock and a fresh staging file; the parent's save waits for the lock, so the cleanup runs after the
    // child is done.
    int fds[2];
    if (pipe(fds) != 0) return;
    const pid_t kid = fork();
    if (kid == 0)
    {
        close(fds[0]);
        const int fd = open("slow.txt.lock", O_RDWR | O_CREAT, 0644);
        if (fd < 0 || flock(fd, LOCK_EX) != 0) _exit(1);
        const std::string mine = "slow.txt.separt-" + std::to_string(static_cast<long>(getpid())) + "-1";
        Put(mine, "06004000 slow\n");
        struct utimbuf o;
        o.actime = o.modtime = time(nullptr) - 900;   // a slow write: older than ten minutes
        utime(mine.c_str(), &o);
        if (write(fds[1], "x", 1) != 1) _exit(2);
        sleep(1);
        const bool kept = Exists(mine);
        rename(mine.c_str(), "slow.txt");
        _exit(kept ? 0 : 3);
    }
    close(fds[1]);
    char c;
    if (read(fds[0], &c, 1) != 1) {}
    close(fds[0]);
    AddressMap base, cur;
    cur[0x06004100u] = "after";
    std::string err;
    Check(SaveStore("slow.txt", "t", std::string(), 1u << 20, base, cur, kClassify, kFormat, std::string(), &err), "slow writer: the second save");
    int st = 0;
    waitpid(kid, &st, 0);
    Check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "A9-37: a slow writer's old staging file is not removed while it holds the lock");
    Check(Has(Slurp("slow.txt"), "06004000 slow") && Has(Slurp("slow.txt"), "06004100 after"), "A9-37: and both writes are in the file");
}

// A9-31 / A9-36: blank is a delete. An import cannot delete (it never goes through Rename, and the reader skips a blank name); an
// API caller passing white space of any kind deletes, as documented.
void BlankIsDelete()
{
    Put("saturn_functions.txt", "06004000 user_name\n");
    FunctionNames fn;
    fn.Load();
    Put("syms.txt", "06004000 \f\v\t\n06004100\t\t\n");
    std::string err;
    fn.Import("syms.txt", &err);
    Check(fn.NameOf(0x06004000u) == "user_name" && fn.SkippedLines() == 2, "A9-31: an import with blank names deletes nothing (both lines skipped)");
    fn.Rename(0x06004000u, "\xC2\xA0");   // a no-break space in UTF-8 is not white space to isspace: a name
    Check(fn.HasName(0x06004000u), "A9-31: a no-break space is a name, not a delete (and the loader reads it back the same)");
    Check(fn.Save(&err), "save");
    FunctionNames again;
    again.Load();
    Check(again.NameOf(0x06004000u) == "\xC2\xA0" && again.SkippedLines() == 0, "A9-31: what the session shows is what the next start reads");
}

// A9-38: a names file that is a link loop is refused at load, and the save does not replace the link.
void LinkLoop()
{
    std::remove("saturn_functions.txt");
    if (symlink("saturn_functions.txt", "saturn_functions.txt") != 0) { Check(false, "symlink"); return; }
    FunctionNames fn;
    std::string err;
    Check(!fn.Load(&err) && !err.empty(), "A9-38: a link loop is refused at load, with a reason");
    fn.Rename(0x06004000u, "x");
    err.clear();
    Check(!fn.Save(&err), "A9-38: and the save is refused");
    struct stat st;
    Check(lstat("saturn_functions.txt", &st) == 0 && S_ISLNK(st.st_mode), "A9-38: the link is still there");
    std::remove("saturn_functions.txt");
}

}  // namespace

int main()
{
    char tmpl[] = "area9r4_XXXXXX";
    if (!mkdtemp(tmpl)) { std::printf("no temp folder\n"); return 2; }
    if (chmod(tmpl, 0755) != 0) return 2;
    if (chdir(tmpl) != 0) return 2;
    alarm(120);
    struct Step { const char* name; void (*fn)(); };
    const Step steps[] = {
        { "ProseLinesAreSkipped", ProseLinesAreSkipped }, { "LockRefusal", LockRefusal }, { "LockWait", LockWait },
        { "SettingsMergeEdges", SettingsMergeEdges },     { "StaleStagingEdges", StaleStagingEdges },
        { "BlankIsDelete", BlankIsDelete },               { "LinkLoop", LinkLoop },
    };
    for (const Step& s : steps) { std::printf("%s\n", s.name); s.fn(); }
    if (chdir("..") == 0)
    {
        std::string cmd = std::string("chmod -R u+rwx ") + tmpl + " 2>/dev/null; rm -rf " + tmpl;
        if (std::system(cmd.c_str()) != 0) {}
    }
    std::printf(gFail ? "Area9Round4Tests: %d FAILED\n" : "Area9Round4Tests: all passed\n", gFail);
    return gFail ? 1 : 0;
}
