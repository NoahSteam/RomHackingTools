// Unit tests for FunctionNames::Import — the symbol/map-file parser behind "Load Symbols".
// Writes a temp file exercising the tolerated formats (0x prefix, tabs, extra whitespace,
// comments, junk) and asserts the merged names + count.
#include "Debug/CallStack.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "AddressNameLine.h"
#include "FileWrite.h"
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

const char* kTmp = "functest_symbols.tmp";

void Write(const char* body)
{
    std::ofstream f(kTmp, std::ios::binary);
    f << body;
}
std::string Slurp(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool Has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

// The save after a load (A9-18, A9-19, A9-20, A9-21, A9-25): each check fails with the matching fix reverted.
void SaveAfterLoadPins()
{
#ifndef _WIN32
    char dir[] = "functest_dir_XXXXXX";
    if (!mkdtemp(dir)) { Check(false, "temp folder"); return; }
    char old[4096];
    if (!getcwd(old, sizeof old) || chdir(dir) != 0) { Check(false, "chdir"); return; }
    const char* kFile = "saturn_functions.txt";

    // A9-18: a word of the letters a-f is a word; an old unpadded address with a digit still loads; eight letters are an address.
    {
        uint32_t a = 0;
        std::string n;
        Check(ParseAddressNameLine("Dead function in the old build", a, n) == AddressNameLine::Malformed, "A9-18: 'Dead function' is not an address");
        Check(ParseAddressNameLine("add it later", a, n) == AddressNameLine::Malformed, "A9-18: 'add it later' is not an address");
        Check(ParseAddressNameLine("a note", a, n) == AddressNameLine::Malformed, "A9-18: 'a note' is not an address");
        Check(ParseAddressNameLine("6004000 main", a, n) == AddressNameLine::Entry && a == 0x6004000u, "A9-18: an old unpadded address loads");
        Check(ParseAddressNameLine("0xdead main", a, n) == AddressNameLine::Entry && a == 0xDEADu, "A9-18: 0x makes letters an address");
        Check(ParseAddressNameLine("FFFFFFFF main", a, n) == AddressNameLine::Entry && a == 0xFFFFFFFFu, "A9-18: eight letters are an address");
    }
    // A9-18 + the save writes eight digits.
    {
        std::remove(kFile);
        FunctionNames fn;
        fn.Load();
        fn.Rename(0x00000ABCu, "low");
        Check(fn.Save(), "save to a new file");
        Check(Has(Slurp(kFile), "00000abc low\n"), "A9-18: the save writes eight digits");
        FunctionNames again;
        again.Load();
        Check(again.NameOf(0x00000ABCu) == "low", "A9-18: what is saved loads");
    }
    // A9-21 + A9-20 + A9-25: comments, skipped lines and another instance's names survive; an idle instance writes nothing.
    {
        std::ofstream(kFile, std::ios::binary) << "; names for the 1.01 release\r06004000 main_loop\r0000000106004100 from_a_64bit_tool\n";
        FunctionNames a, b;
        a.Load();
        b.Load();
        Check(a.All().size() == 1 && a.SkippedLines() == 1, "A9-25: a lone CR ends a line");
        a.Rename(0x06004200u, "read_pad");
        Check(a.Save(), "A saves");
        const std::string afterA = Slurp(kFile);
        Check(b.Save(), "idle B saves");
        Check(Slurp(kFile) == afterA, "A9-20: an instance that changed nothing writes nothing");
        b.Rename(0x06004300u, "wait_vblank");
        Check(b.Save(), "B saves its own rename");
        const std::string text = Slurp(kFile);
        Check(Has(text, "read_pad") && Has(text, "wait_vblank"), "A9-20: both instances' renames are in the file");
        Check(Has(text, "1.01 release") && Has(text, "from_a_64bit_tool"), "A9-21: the comment and the skipped line are kept");
        a.Rename(0x06004000u, "");
        Check(a.Save(), "A deletes");
        Check(!Has(Slurp(kFile), "main_loop") && Has(Slurp(kFile), "wait_vblank"), "A9-20: a delete removes only that name");
    }
    // A9-19: a file the loader refused is never replaced.
    {
        std::string big;
        for (int i = 0; big.size() < 8u * 1024u * 1024u + 100; ++i) big += "06000000 " + std::string(200, 'x') + "\n";
        std::ofstream(kFile, std::ios::binary) << big;
        FunctionNames fn;
        std::string err, serr;
        Check(!fn.Load(&err), "A9-19: an 8 MiB file is refused");
        Check(fn.Save(&serr), "A9-19: a refused store with no changes writes nothing and says nothing");
        fn.Rename(0x06004000u, "x");
        Check(!fn.Save(&serr) && Has(serr, kFile) && Has(serr, "refused"), "A9-19: a save over a refused file is refused, naming the file");
        Check(Slurp(kFile) == big, "A9-19: the refused file is untouched");
    }
    // A9-19: Load Symbols does not make a names file the next start refuses.
    {
        std::string own, map;
        char b[64];
        for (unsigned i = 0; i < 24000; ++i) { std::snprintf(b, sizeof b, "%08x ", 0x06000000u + 2 * i); own += b; own += "renamed_" + std::to_string(i) + std::string(200, 'x') + "\n"; }
        for (unsigned i = 0; i < 24000; ++i) { std::snprintf(b, sizeof b, "%08x ", 0x06100000u + 2 * i); map += b; map += "symbol_" + std::to_string(i) + std::string(200, 'x') + "\n"; }
        std::ofstream(kFile, std::ios::binary) << own;
        std::ofstream("game.map", std::ios::binary) << map;
        FunctionNames fn;
        std::string err;
        fn.Load(&err);
        Check(fn.Import("game.map", &err) == 0 && Has(err, "not merged"), "A9-19: a merge that would pass the ceiling is refused with a message");
        Check(fn.NameOf(0x06100000u) == "sub_100000", "A9-19: and nothing of it was merged");
    }
    // A9-28: the mode is kept; a read-only file is not replaced.
    {
        std::ofstream(kFile, std::ios::binary) << "06004000 main_loop\n";
        ::chmod(kFile, 0600);
        FunctionNames fn;
        fn.Load();
        fn.Rename(0x06004100u, "init");
        Check(fn.Save(), "save a 0600 file");
        struct stat st;
        Check(::stat(kFile, &st) == 0 && (st.st_mode & 0777) == 0600, "A9-28: the mode is kept");
        ::chmod(kFile, 0444);
        fn.Rename(0x06004200u, "more");
        std::string err;
        Check(!fn.Save(&err) && Has(err, "read-only"), "A9-28: a read-only file is not replaced, and the save says so");
        ::chmod(kFile, 0644);
    }
    if (chdir(old) == 0) { std::string rm = std::string("rm -rf ") + dir; std::system(rm.c_str()); }
#endif
}
}  // namespace

int main()
{
    // Mixed formats: bare hex, 0x-prefixed, tab separator, extra spaces, comments, junk, blank.
    Write("06001234 main\n"
          "0x06005678\tvblank_handler\n"
          "   0600abcd   spaced_name  \n"
          "; a comment line\n"
          "# another comment\n"
          "// slash comment\n"
          "nothex should_be_ignored\n"
          "\n"
          "06009999\n"                 // address but no name -> ignored
          "0600AAAA lower_hex_addr\r\n" // CRLF line ending trimmed
    );

    FunctionNames fn;
    const size_t n = fn.Import(kTmp);

    Check(n == 4, "imported exactly the four named rows");
    Check(fn.NameOf(0x06001234) == "main", "bare hex + name");
    Check(fn.NameOf(0x06005678) == "vblank_handler", "0x prefix + tab separator");
    Check(fn.NameOf(0x0600ABCD) == "spaced_name", "leading/trailing/inner whitespace trimmed");
    Check(fn.NameOf(0x0600AAAA) == "lower_hex_addr", "CRLF trimmed, uppercase hex");
    Check(fn.HasName(0x06001234) && !fn.HasName(0x06009999), "no-name row not stored");
    Check(!fn.HasName(0x06002000), "unseen address has no name");
    Check(fn.NameOf(0x06002000) == "sub_002000", "unknown falls back to sub_XXXXXX");

    // A second import merges (and overrides) without dropping the first batch.
    Write("06001234 main_renamed\n0600BBBB extra\n");
    const size_t n2 = fn.Import(kTmp);
    Check(n2 == 2, "second import counts its own rows");
    Check(fn.NameOf(0x06001234) == "main_renamed", "later import overrides an address");
    Check(fn.NameOf(0x06005678) == "vblank_handler", "earlier names survive the merge");
    Check(fn.NameOf(0x0600BBBB) == "extra", "new name from the second import");

    // Missing file imports nothing, harmlessly.
    Check(fn.Import("no_such_file_here.tmp") == 0, "missing file imports zero");

    std::remove(kTmp);
    SaveAfterLoadPins();
    if (gFail == 0) std::printf("All FunctionNames tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
