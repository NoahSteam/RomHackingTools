// Unit tests for FunctionNames::Import — the symbol/map-file parser behind "Load Symbols".
// Writes a temp file exercising the tolerated formats (0x prefix, tabs, extra whitespace,
// comments, junk) and asserts the merged names + count.
#include "Debug/CallStack.h"

#include <cstdio>
#include <fstream>

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
    if (gFail == 0) std::printf("All FunctionNames tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
