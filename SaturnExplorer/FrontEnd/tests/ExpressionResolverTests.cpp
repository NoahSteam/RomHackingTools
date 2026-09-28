// Boundary tests for SimpleExpressionResolver — the watch/breakpoint address expression
// parser ("0xADDR [+|- N]"). These pin the overflow behaviour added for review finding
// MEM-01: a literal that overflows uint32_t, or a base +/- offset that wraps past the ends
// of the address space, must be reported as an error rather than silently wrapping into an
// unrelated Saturn region.
//
// Also MEM-03, which lives next door in WatchList: "is a Saturn address" and "is readable from
// the loaded source" are different questions, and a pointer watch answers both separately.
#include "Debug/WatchList.h"

#include <cstdio>
#include <string>
#include <vector>

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

// A plain assertion, for the MEM-03 cases below.
void check(bool cond, const char* what)
{
    if (!cond) { std::printf("FAIL: %s\n", what); ++gFails; }
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

    // --- MEM-03: plausible is not the same as available. ---
    //
    // The plausibility map covers the whole hardware layout, including BIOS, which no source
    // captures. A backend that serves only high work RAM stands in for a partial savestate.
    {
        class HwramOnly : public IMemoryBackend
        {
        public:
            bool Connected() const override { return true; }
            std::vector<MemoryReadResult> ReadMemoryBatch(
                const std::vector<MemoryReadRequest>& reqs) override
            {
                std::vector<MemoryReadResult> out;
                for (const MemoryReadRequest& q : reqs)
                {
                    MemoryReadResult r;
                    if (q.address >= 0x06000000u && q.address < 0x06100000u)
                    {
                        r.success = true;
                        r.bytes.assign(q.size, 0);
                    }
                    else
                    {
                        r.error = "not captured";
                    }
                    out.push_back(std::move(r));
                }
                return out;
            }
        };
        HwramOnly be;

        // BIOS is a real Saturn address and is never captured by any source.
        check(IsPlausibleSaturnAddress(0x00000100u), "BIOS is a plausible Saturn address");
        check(!IsReadableAddress(&be, 0x00000100u), "BIOS is not readable");

        // Low work RAM is plausible, and absent from this particular source.
        check(IsPlausibleSaturnAddress(0x00200000u), "LWRAM is a plausible Saturn address");
        check(!IsReadableAddress(&be, 0x00200000u), "LWRAM is not in this source");

        check(IsReadableAddress(&be, 0x06000000u), "HWRAM is readable here");
        check(!IsPlausibleSaturnAddress(0x12345678u), "an unmapped address is not plausible");

        // The pointer watch reports the two separately, so "outside the map" is never confused
        // with "this source does not have it".
        MemoryReadResult mem;
        mem.success = true;
        mem.bytes = { 0x00, 0x20, 0x00, 0x00 };   // -> 0x00200000, LWRAM
        WatchValue v = FormatWatchValue(WatchType::Pointer, mem, &be);
        check(v.isPointer && !v.pointerSuspicious && v.pointerUnavailable,
              "a valid but uncaptured target is unavailable, not suspicious");

        mem.bytes = { 0x12, 0x34, 0x56, 0x78 };
        v = FormatWatchValue(WatchType::Pointer, mem, &be);
        check(v.pointerSuspicious && !v.pointerUnavailable,
              "an unmapped target is reported as suspicious, the more specific answer");

        mem.bytes = { 0x06, 0x00, 0x10, 0x00 };
        v = FormatWatchValue(WatchType::Pointer, mem, &be);
        check(!v.pointerSuspicious && !v.pointerUnavailable, "a readable target is neither");

        // With no backend the availability question is simply not asked, so an existing caller
        // keeps the old behaviour rather than having every pointer flagged.
        v = FormatWatchValue(WatchType::Pointer, mem, nullptr);
        check(!v.pointerUnavailable, "no backend means no availability claim");
    }

    if (gFails == 0) std::printf("All expression-resolver tests passed.\n");
    return gFails == 0 ? 0 : 1;
}
