// Area 9, round 4 (review): the area 9 fixer's changes (A9-31 to A9-38) and the Windows run's A9-40, attacked. "PIN" asserts a
// finding as it is today (REVIEW_LEDGER.md, A9-40 and later); when the finding is fixed its check fails and is turned round in the
// same change. The rest hold what the fixer got right (a regression guard): the settings merge by lines, the stale staging
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

// A9-41 (A9-32's fix): two to four lower-case letters a-f in front of a line are an address again when they spell one in the BIOS
// ROM. A prose line in a hand-edited file is an entry, with no line skipped and so no Log warning: the A9-4 and A9-18 examples
// ("add it later") come back, and so do "be careful", "fade routine", "bad idea", "dead code". The fixer's tests and the fuzz
// oracle were changed to "Add it later" to pass.
void ProseLinesAreEntries()
{
    uint32_t a = 0;
    std::string n;
    struct P { const char* line; uint32_t addr; };
    const P prose[] = {
        { "add it later", 0xADDu },        // A9-4's and A9-18's own example
        { "be careful: the next one is a guess", 0xBEu },
        { "fade routine is near 06012000, not found yet", 0xFADEu },
        { "bad idea to rename this", 0xBADu },
        { "dead code below", 0xDEADu },
        { "face sprites are loaded by the next one", 0xFACEu },
    };
    int entries = 0;
    for (const P& p : prose)
        if (ParseAddressNameLine(p.line, a, n) == AddressNameLine::Entry && a == p.addr) ++entries;
    Pin(entries == 6, "A9-41: six prose lines opening with a lower-case a-f word are entries at 0xADD, 0xBE, 0xFADE, 0xBAD, 0xDEAD, 0xFACE");

    // The names file: the prose is a name, and nothing is counted as skipped, so App logs nothing.
    Put("saturn_functions.txt", "06004000 main_loop\nbe careful: the next one is a guess\n06004100 init_vdp2\nfade routine is near 06012000, not found yet\n");
    FunctionNames fn;
    fn.Load();
    Pin(fn.HasName(0xBEu) && fn.NameOf(0xBEu) == "careful: the next one is a guess" && fn.HasName(0xFADEu) && fn.SkippedLines() == 0,
        "A9-41: in saturn_functions.txt the prose lines are names at 0xBE and 0xFADE (BIOS ROM, even: callable), with no line skipped");

    // The bounds file (the one the user edits by hand, A9-4): the prose is a function start the decompiler uses.
    Put("bounds.txt", "; Function entries set by hand\n06004000 main loop\nbe careful: the next one is a guess\n06004100\nfade in handler starts below\n");
    decomp::FunctionBounds fb;
    std::string err;
    Check(fb.Load("bounds.txt", &err), "bounds load");
    Pin(fb.Entries().count(0xBEu) && fb.Entries().count(0xFADEu) && fb.SkippedLines() == 0,
        "A9-41: in saturn_function_bounds.txt the prose lines are function starts at 0x000000BE and 0x0000FADE, with no line skipped");
    // And the remark is the entry, so clearing the stray start (the only way the user can get rid of it) erases the remark.
    fb.ClearEntry(0xBEu);
    Check(fb.Save("bounds.txt", &err), "bounds save");
    Pin(!Has(Slurp("bounds.txt"), "be careful"), "A9-41: clearing the stray start at 0xBE deletes the user's remark line from the file");

    // What round 1 to 3 did (the comparison): capitals, one letter and five or more stay words, skipped and counted.
    Check(ParseAddressNameLine("Add it later", a, n) == AddressNameLine::Malformed && ParseAddressNameLine("a note", a, n) == AddressNameLine::Malformed &&
          ParseAddressNameLine("faded colours", a, n) == AddressNameLine::Malformed,
          "A9-32: a capital, a single letter, and a word with other letters stay words");
}

// A9-33's refusal, from an ordinary user: the text when the folder is not writable, when the folder is gone, and when the lock file
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
    // 1: refused (nothing could be saved there anyway), but the text gives the three lock causes and not the real one.
    const size_t l1 = got.find("readonly ");
    const std::string line1 = l1 == std::string::npos ? std::string() : got.substr(l1, got.find('\n', l1) - l1);
    Check(Has(line1, "refused"), "A9-33: a folder the user cannot write refuses the save");
    Pin(Has(line1, "names.txt.lock cannot be opened or locked") && !Has(line1, "folder cannot be written") && !Has(line1, "ermission"),
        "A9-42: in a folder the user cannot write the refusal blames the lock file (another user's lock, a folder, a refused lock) and gives no OS reason");
    Check(Has(got, "rootlock refused") && Has(got, "other.txt.lock"), "A9-33: a lock file of another user refuses the save and names the file");
    Check(Has(got, "retry saved") && Has(got, "06004100 new"), "A9-33: the refused change is kept and is written by the next save once the lock file is gone");

    // 4. A folder that is gone (the working folder removed or unmounted while the app runs): the same lock text, no ENOENT.
    std::string err;
    AddressMap base, cur;
    cur[0x06004100u] = "new";
    Check(!SaveStore("gone/names.txt", "The function names", std::string(), 1u << 20, base, cur, kClassify, kFormat, std::string(), &err), "A9-33: no folder, refused");
    Pin(Has(err, "gone/names.txt.lock cannot be opened or locked") && !Has(err, "No such file"),
        "A9-42: when the folder is gone the refusal blames the lock file too");
}

// A9-40: a save waits for the lock with no limit. Another process holds <file>.lock for three seconds; SaveStore and Settings::SaveTo
// each return only when it lets go (on the UI thread, the window stops for that long).
double TimedSaveWhileLocked(const std::string& lockPath, bool settings)
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
        sleep(3);
        _exit(0);
    }
    close(fds[1]);
    char c;
    if (read(fds[0], &c, 1) != 1) { close(fds[0]); return -1; }
    close(fds[0]);
    const auto t0 = std::chrono::steady_clock::now();
    std::string err;
    bool ok;
    if (settings)
    {
        Settings s;
        s.Set("a", "b", "1");
        ok = s.SaveTo("held.ini", &err);
    }
    else
    {
        AddressMap base, cur;
        cur[0x06004000u] = "x";
        ok = SaveStore("held.txt", "t", std::string(), 1u << 20, base, cur, kClassify, kFormat, std::string(), &err);
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    int st = 0;
    waitpid(kid, &st, 0);
    Check(ok, "A9-40: the save succeeds after the holder lets go");
    return secs;
}
void LockWait()
{
    const double s1 = TimedSaveWhileLocked("held.txt.lock", false);
    const double s2 = TimedSaveWhileLocked("held.ini.lock", true);
    std::printf("  SaveStore waited %.2f s, Settings::SaveTo %.2f s, for a lock another process held 3 s\n", s1, s2);
    Pin(s1 > 2.5 && s2 > 2.5, "A9-40: SaveStore and Settings::SaveTo wait for another process's lock with no limit (Linux, as on Windows)");
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
        { "ProseLinesAreEntries", ProseLinesAreEntries }, { "LockRefusal", LockRefusal }, { "LockWait", LockWait },
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
