// Area 9, round 3 (review): the stores' save (StoreMerge.h SaveStore, Settings::SaveTo), the address word, the capped read and the
// line ends, attacked after the area 9 fixer. Each "PIN" below asserts what the product does TODAY for a finding filed in
// REVIEW_LEDGER.md (A9-31 and later); when a finding is fixed its pin fails and is turned round (the ledger row names it).
// The rest are fuzz and sequence cases that hold (a regression guard for what the fixer got right).
//
// Linux only (fork, kill, FIFOs). Runs in a scratch folder under the current directory; nothing outside it is touched.
#include "Debug/CallStack.h"
#include "Settings.h"
#include "StoreMerge.h"
#include "FileRead.h"
#include "FileWrite.h"
#include "AddressNameLine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <atomic>

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}
void Note(const char* fmt, long a = 0, long b = 0, long c = 0) { std::printf("  note: "); std::printf(fmt, a, b, c); std::printf("\n"); }

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
size_t CountLeftovers(const std::string& dir, const char* needle)
{
    size_t n = 0;
    if (DIR* d = opendir(dir.c_str()))
    {
        while (dirent* e = readdir(d)) if (std::strstr(e->d_name, needle)) ++n;
        closedir(d);
    }
    return n;
}

const char* kNames = "saturn_functions.txt";

auto kClassify = [](const std::string& line, uint32_t& a) {
    std::string n;
    return ParseAddressNameLine(line, a, n) == AddressNameLine::Entry;
};
auto kFormat = [](uint32_t a, const std::string& n) {
    char b[16];
    std::snprintf(b, sizeof b, "%08x ", a);
    return std::string(b) + n + "\n";
};

// A9-31: a name that is only white space is kept by the session and saved as a line its own loader skips.
void WhitespaceNames()
{
    std::remove(kNames);
    FunctionNames fn;
    fn.Load();
    fn.Rename(0x06004000u, "   ");
    fn.Rename(0x06004100u, "  keep  ");
    std::string err;
    Check(fn.Save(&err), "A9-31: the save of a blank name reports success");
    FunctionNames again;
    again.Load();
    Check(!again.HasName(0x06004000u) && again.SkippedLines() == 1, "PIN A9-31: a blank name is saved as a line the next start skips (and counts)");
    Check(again.NameOf(0x06004100u) == "keep", "A9-31: surrounding white space of a name does not survive a restart");
    Check(fn.HasName(0x06004000u), "A9-31: this session shows the blank name as a name");
}

// A9-32: an address of letters a-f alone below 0x10000000 is "in no memory of the machine" (the fixer's reason); the BIOS ROM
// (0x00000000 to 0x0007FFFF) holds many of them (0x0000DEAD, 0x0000ABCD, 0x00000FAB).
void LettersOnlyBiosAddresses()
{
    uint32_t a = 0;
    std::string n;
    const char* words[] = { "dead", "abcd", "fab", "cafe", "fade", "bad" };
    int skipped = 0;
    for (const char* w : words)
        if (ParseAddressNameLine(std::string(w) + " BiosFunc", a, n) == AddressNameLine::Malformed) ++skipped;
    Check(skipped == 6, "PIN A9-32: an old file's names at BIOS addresses made of letters a-f alone (written 'dead', 'abcd', 'fab') are not entries");
    Check(0xDEADu < 0x80000u && 0xABCDu < 0x80000u, "A9-32: those addresses are inside the 512 KiB BIOS ROM the decompiler maps at 0");
    // What an older version wrote: %x, unpadded.
    Put(kNames, "dead dead_handler\nabcd abcd_handler\n6004000 main\n");
    FunctionNames fn;
    fn.Load();
    Check(!fn.HasName(0xDEADu) && !fn.HasName(0xABCDu) && fn.HasName(0x6004000u) && fn.SkippedLines() == 2,
          "PIN A9-32: of an old file, the two BIOS names are skipped and counted, the RAM one loads");
}

// A9-33: SaveStore never looks at FileLock::Held(). A lock that cannot be made (here the lock path is a folder; in life a lock file
// another user, or sudo, made) means the save runs unlocked, silently, and two instances lose each other's entries.
int Hammer(const std::string& file, uint32_t base, int count)
{
    AddressMap b, cur;
    for (int i = 0; i < count; ++i)
    {
        cur[base + i] = "n";
        std::string err;
        if (!SaveStore(file, "t", std::string(), 1u << 20, b, cur, kClassify, kFormat, std::string(), &err)) return 2;
    }
    return 0;
}
size_t RunTwoWriters(const std::string& file, int count)
{
    std::remove(file.c_str());
    pid_t kids[2];
    for (int k = 0; k < 2; ++k)
    {
        kids[k] = fork();
        if (kids[k] == 0) _exit(Hammer(file, 0x06000000u + k * 0x10000u, count));
    }
    for (pid_t p : kids) { int st = 0; waitpid(p, &st, 0); Check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "writer child saved without an error"); }
    size_t n = 0;
    for (const std::string& l : SplitLines(Slurp(file))) { uint32_t a; if (kClassify(l, a)) ++n; }
    return n;
}
void LockNotHeld()
{
    const int count = 300;
    const size_t good = RunTwoWriters("lockok.txt", count);
    Check(good == 2u * count, "two instances saving 300 entries each under the lock lose none");
    mkdir("lockdir.txt.lock", 0755);   // a lock that cannot be made
    {
        FileLock probe("lockdir.txt.lock", false);
        Check(!probe.Held(), "A9-33: the lock cannot be taken when its file cannot be opened");
    }
    const size_t bad = RunTwoWriters("lockdir.txt", count);
    Note("with the lock unavailable: %ld of %ld entries survived", (long)bad, (long)(2 * count));
    Check(bad <= 2u * count, "A9-33: (information) unlocked saves");
    Check(bad < 2u * count, "PIN A9-33: with the lock path unusable the saves run unlocked and succeed (no error), and entries are lost");
}

// A9-34: settings.ini is rewritten whole from the parsed keys: a hand-written comment and the order are gone after the first
// save, and a file that opens with a byte-order mark loses its first section header.
void SettingsRewrite()
{
    mkdir("cfg", 0755);
    setenv("XDG_CONFIG_HOME", "cfg", 1);
    const std::string dir = "cfg/SaturnExplorer";
    mkdir(dir.c_str(), 0755);
    const std::string ini = dir + "/settings.ini";
    Put(ini, "; my notes about this file\n[decompiler]\nbios = /x/bios.bin\n\n[ui]\ntooltips = 1\n");
    Settings s;
    std::string err;
    Check(s.Load(&err), "settings load");
    s.Set("ui", "extra", "1");
    Check(s.Save(&err), "settings save");
    Check(!Has(Slurp(ini), "my notes"), "PIN A9-34: a hand-written comment in settings.ini is erased by the first save");

    // A byte-order mark (Windows editors, PowerShell Set-Content -Encoding UTF8).
    Put(ini, "\xEF\xBB\xBF[decompiler]\nbios = /x/bios.bin\nvolatile = 0x06002000\n[ui]\ntooltips = 0\n");
    Settings b;
    Check(b.Load(&err), "bom: load");
    Check(b.Get("decompiler", "bios", "none") == "none" && b.Get("", "bios", "none") == "/x/bios.bin",
          "PIN A9-34: with a byte-order mark the first section header is not read; its keys land in the section ''");
    Check(b.Get("ui", "tooltips", "") == "0", "bom: later sections are read");
    b.Set("ui", "extra", "1");
    Check(b.Save(&err), "bom: save");
    const std::string after = Slurp(ini);
    Check(Has(after, "[]\n") && Has(after, "bios = /x/bios.bin"), "PIN A9-34: and the save writes them under '[]' for good");

    // A choice equal to the file's value from the start is not a change, whatever another instance wrote since (A9-35).
    Put(ini, "[decompiler]\nbios = /p/one.bin\n");
    Settings a2, b2;
    a2.Load(&err);
    b2.Load(&err);
    a2.Set("decompiler", "bios", "/p/two.bin");
    Check(a2.Save(&err), "instance A saves its choice");
    b2.Set("decompiler", "bios", "/p/one.bin");   // B's user chooses the original again, after A's choice
    b2.Set("ui", "x", "1");
    Check(b2.Save(&err), "instance B saves");
    Check(Slurp(ini).find("/p/two.bin") != std::string::npos, "PIN A9-35: B's later choice of the value it started with is dropped; A's value stays");
}

// A9-36: a name or note with a line end is written as two lines; the second is read as an entry of its own (settings do not: A9-12).
// Not reachable from the single-line boxes (the input filters them); reachable by any caller of the store.
void NameInjection()
{
    std::remove(kNames);
    FunctionNames fn;
    fn.Load();
    fn.Rename(0x06004000u, "x\n06004100 evil");
    fn.Rename(0x06004200u, "y\r06004300 evil2");
    std::string err;
    fn.Save(&err);
    FunctionNames again;
    again.Load();
    Check(again.HasName(0x06004100u) && again.HasName(0x06004300u), "PIN A9-36: a line end in a name starts another entry at the next start (API level)");
}

// Delete against edit, and the same key changed by both: the session that touched the key decides; the other's edit is lost.
void TwoSessionKeys()
{
    Put(kNames, "06004000 base\n06004100 base2\n");
    FunctionNames a, b;
    a.Load();
    b.Load();
    a.Rename(0x06004000u, "edited_by_a");
    a.Rename(0x06004100u, "edited_by_a2");
    std::string err;
    Check(a.Save(&err), "A saves");
    b.Rename(0x06004000u, "");               // B deletes what A edited
    b.Rename(0x06004100u, "edited_by_b");    // B edits what A edited
    Check(b.Save(&err), "B saves");
    const std::string t = Slurp(kNames);
    Check(!Has(t, "edited_by_a\n") && !Has(t, "06004000"), "delete by B beats A's edit (A's edit is lost without a word)");
    Check(Has(t, "edited_by_b") && !Has(t, "edited_by_a2"), "same key: the later saver wins");
    // A third instance that renames nothing leaves the file alone.
    struct stat s1, s2;
    stat(kNames, &s1);
    FunctionNames c;
    c.Load();
    c.Save(&err);
    stat(kNames, &s2);
    Check(s1.st_ino == s2.st_ino && s1.st_mtime == s2.st_mtime, "an idle instance writes nothing");
}

// Crash between steps: SIGKILL a child that saves in a loop. The file must always be whole and never shrink; litter is counted.
void KillFuzz()
{
    const std::string file = "killfuzz.txt";
    std::remove(file.c_str());
    size_t lastCount = 0;
    unsigned seed = 9;
    for (int round = 0; round < 60; ++round)
    {
        const pid_t p = fork();
        if (p == 0) _exit(Hammer(file, 0x06000000u + round * 0x1000u, 400));
        seed = seed * 1103515245u + 12345u;
        usleep(500 + (seed >> 16) % 4000);
        kill(p, SIGKILL);
        int st = 0;
        waitpid(p, &st, 0);
        size_t n = 0, bad = 0;
        for (const std::string& l : SplitLines(Slurp(file))) { uint32_t a; if (kClassify(l, a)) ++n; else if (!l.empty()) ++bad; }
        Check(bad == 0, "kill fuzz: the file has no torn line");
        Check(n >= lastCount, "kill fuzz: the file never shrinks across a kill");
        lastCount = n;
    }
    const size_t litter = CountLeftovers(".", "killfuzz.txt.separt-");
    Note("after 60 killed writers: %ld entries, %ld staging files left beside the file", (long)lastCount, (long)litter);
    Check(litter == 0 || litter > 0, "(information) staging litter");
    if (litter > 0) std::printf("  PIN A9-37: a writer killed between the staging write and the rename leaves '<file>.separt-<pid>-<n>' beside it for good\n");
}

// ReadFileCapped: a file that grows while it is read, a FIFO, a device, a loop of links, a file that shrinks.
void CappedReads()
{
    std::string out;
    mkfifo("fifo.txt", 0644);
    Check(ReadFileCapped("fifo.txt", 1024, out) == CappedRead::NotRegular, "a FIFO is refused without waiting");
    Check(ReadFileCapped("/dev/zero", 1024, out) == CappedRead::NotRegular, "a device is refused");
    Put("grow.txt", std::string(1000, 'a'));
    std::atomic<bool> stop(false);
    std::thread w([&] {
        while (!stop) { std::ofstream f("grow.txt", std::ios::binary | std::ios::app); f << std::string(512, 'b'); }
    });
    int ok = 0, tooLarge = 0, other = 0;
    for (int i = 0; i < 300; ++i)
    {
        const CappedRead r = ReadFileCapped("grow.txt", 64 * 1024, out);
        if (r == CappedRead::Ok) { ++ok; Check(out.size() <= 64 * 1024, "a grown file is still within the cap"); }
        else if (r == CappedRead::TooLarge) { ++tooLarge; Check(out.empty(), "too large leaves nothing"); }
        else ++other;
    }
    stop = true;
    w.join();
    Check(other == 0, "a file growing under the read gives Ok or TooLarge only");
    Note("growing file: %ld ok, %ld too large", ok, tooLarge);
    // A link that loops is 'Missing' (no file, no message): the loader says nothing and the save replaces the link.
    symlink("loop.txt", "loop.txt");
    Check(ReadFileCapped("loop.txt", 1024, out) == CappedRead::Missing, "PIN A9-38: a link loop reads as Missing (silent), not Unreadable");
}

// Old-file and odd-width address words.
void AddressWords()
{
    uint32_t a = 0;
    std::string n;
    struct C { const char* line; bool entry; uint32_t addr; };
    const C cases[] = {
        { "06004000 x", true, 0x06004000u }, { "0x6004000 x", true, 0x6004000u }, { "0X06004000 x", true, 0x06004000u },
        { "006004000 x", false, 0 },          { "0x006004000 x", false, 0 },       { "0x x", false, 0 },
        { "0x0600_4100 x", false, 0 },        { "FFFFFFFF x", true, 0xFFFFFFFFu }, { "deadbeef x", true, 0xDEADBEEFu },
        { "0xdead x", true, 0xDEADu },        { "1a x", true, 0x1Au },             { "0000000106004000 x", false, 0 },
        { "06004000", false, 0 },             { "06004000 \t ", false, 0 },        { "+6004000 x", false, 0 },
    };
    for (const C& c : cases)
    {
        const bool entry = ParseAddressNameLine(c.line, a, n) == AddressNameLine::Entry;
        char msg[96];
        std::snprintf(msg, sizeof msg, "address word: '%s'", c.line);
        Check(entry == c.entry && (!entry || a == c.addr), msg);
    }
    // Line ends.
    Check(SplitLines("a\r\nb").size() == 2 && SplitLines("a\rb").size() == 2 && SplitLines("a\n\rb").size() == 3 && SplitLines("a\n").size() == 1,
          "line ends: LF, CRLF, lone CR");
    // A UTF-8 byte-order mark in front of the first address (A9-34's names-file half).
    Check(ParseAddressNameLine("\xEF\xBB\xBF" "06004000 first", a, n) == AddressNameLine::Malformed, "PIN A9-34: a byte-order mark makes the first entry of a names file a skipped line");
}

}  // namespace

int main()
{
    char tmpl[] = "area9r3_XXXXXX";
    if (!mkdtemp(tmpl)) { std::printf("no temp folder\n"); return 2; }
    if (chdir(tmpl) != 0) return 2;
    alarm(300);
    struct Step { const char* name; void (*fn)(); };
    const Step steps[] = {
        { "WhitespaceNames", WhitespaceNames }, { "LettersOnlyBiosAddresses", LettersOnlyBiosAddresses }, { "LockNotHeld", LockNotHeld },
        { "SettingsRewrite", SettingsRewrite }, { "NameInjection", NameInjection }, { "TwoSessionKeys", TwoSessionKeys },
        { "KillFuzz", KillFuzz },               { "CappedReads", CappedReads },     { "AddressWords", AddressWords },
    };
    for (const Step& s : steps) { std::printf("%s\n", s.name); s.fn(); }
    if (chdir("..") == 0)
    {
        std::string cmd = std::string("rm -rf ") + tmpl;
        if (std::system(cmd.c_str()) != 0) {}
    }
    std::printf(gFail ? "Area9Round3Tests: %d FAILED\n" : "Area9Round3Tests: all passed\n", gFail);
    return gFail ? 1 : 0;
}
