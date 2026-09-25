// Boundary tests for SimpleExpressionResolver — the watch/breakpoint address expression
// parser ("0xADDR [+|- N]"). These pin the overflow behaviour added for review finding
// MEM-01: a literal that overflows uint32_t, or a base +/- offset that wraps past the ends
// of the address space, must be reported as an error rather than silently wrapping into an
// unrelated Saturn region.
#include "Debug/WatchList.h"

#include <cstdio>
#include <string>

using namespace sfe;

namespace
{
int gFails = 0;

// Expect a successful resolve to 'want'.
void ok(const char* expr, uint32_t want)
{
    SimpleExpressionResolver r;
    uint32_t addr = 0xEEEEEEEE;
    std::string err;
    if (!r.Resolve(expr, addr, err))
    {
        std::printf("FAIL: '%s' expected 0x%08X, got error '%s'\n", expr, want, err.c_str());
        ++gFails;
    }
    else if (addr != want)
    {
        std::printf("FAIL: '%s' expected 0x%08X, got 0x%08X\n", expr, want, addr);
        ++gFails;
    }
}

// Expect a rejected resolve (any non-empty error).
void bad(const char* expr)
{
    SimpleExpressionResolver r;
    uint32_t addr = 0xEEEEEEEE;
    std::string err;
    if (r.Resolve(expr, addr, err))
    {
        std::printf("FAIL: '%s' expected error, got 0x%08X\n", expr, addr);
        ++gFails;
    }
}
}  // namespace

int main()
{
    // --- Ordinary cases still work. ---
    ok("0x06004000", 0x06004000);
    ok("0x06004000 + 0x10", 0x06004010);
    ok("0x06004000 - 0x10", 0x06003FF0);
    ok("0x100 + 256", 0x200);           // decimal offset
    ok("0xFFFFFFFF", 0xFFFFFFFFu);      // exact top of the space is valid

    // --- MEM-01: literal parse must not wrap. ---
    bad("4294967296");                  // 2^32, decimal — wraps to 0 without the check
    bad("0x100000000");                 // 2^32, hex
    bad("99999999999999999999");        // absurdly long decimal

    // --- MEM-01: base +/- offset must not wrap past either end. ---
    bad("0xFFFFFFFF + 1");              // overflow past the top
    bad("0x10 - 0x20");                 // underflow below 0
    bad("0x0 - 1");
    ok("0xFFFFFFFF - 0xFFFFFFFF", 0x0); // exact edges are fine
    ok("0x0 + 0xFFFFFFFF", 0xFFFFFFFFu);

    if (gFails == 0) std::printf("All expression-resolver tests passed.\n");
    return gFails == 0 ? 0 : 1;
}
