// CRAM index wrapping. Real Saturn CRAM holds 1024 or 2048 entries, so the fast
// `index & (words - 1)` is exact modulo there. A snapshot taken from a source that returned a
// short CRAM region can be any length, and the mask is then not modulo at all -- it silently
// returns a different, in-range entry, which is the worst shape of wrong: a plausible colour.
#include "Vdp1Color.h"
#include "saturnexplorer/SeAbi.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what, int line)
{
    if (ok) return;
    std::printf("CHECK failed at line %d: %s\n", line, what);
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

void TestPowerOfTwoWrapsByMasking()
{
    // The hardware sizes: wrapping is modulo, and every entry is reachable.
    for (uint32_t words : { 1u, 2u, 1024u, 2048u })
    {
        bool allMatch = true;
        for (uint32_t i = 0; i < words * 3; ++i)
        {
            if (se::CramWrap(i, words) != i % words) allMatch = false;
        }
        CHECK(allMatch);
    }
}

void TestNonPowerOfTwoStillReachesEveryEntry()
{
    // 3 entries: the old mask (words - 1 == 2) mapped every index onto {0, 2}, so entry 1 was
    // unreachable and index 1 silently read entry 0. Assert real modulo instead -- which is
    // both correct and the only form where every entry can be addressed.
    const uint32_t words = 3;
    CHECK(se::CramWrap(0, words) == 0);
    CHECK(se::CramWrap(1, words) == 1);   // the entry the mask could never reach
    CHECK(se::CramWrap(2, words) == 2);
    CHECK(se::CramWrap(3, words) == 0);
    CHECK(se::CramWrap(4, words) == 1);

    bool everyEntryReachable = true;
    for (uint32_t w : { 3u, 5u, 6u, 7u, 100u, 1000u, 1025u })
    {
        std::vector<bool> seen(w, false);
        for (uint32_t i = 0; i < w; ++i)
        {
            const uint32_t e = se::CramWrap(i, w);
            if (e >= w) { everyEntryReachable = false; break; }
            seen[e] = true;
        }
        for (uint32_t i = 0; i < w; ++i) if (!seen[i]) everyEntryReachable = false;
    }
    CHECK(everyEntryReachable);
}

void TestPartialCramReadsTheRightColour()
{
    // A 3-entry RGB555 CRAM, big-endian on the wire. Index 1 must be the middle colour, not
    // the first one -- this is the end-to-end form of the bug above.
    std::vector<uint8_t> cram{ 0x80, 0x1F,    /* entry 0: red  */
                               0x83, 0xE0,    /* entry 1: green */
                               0xFC, 0x00 };  /* entry 2: blue  */
    const se::Rgba e0 = se::CramColor(cram, SE_CRAM_RGB555_1024, 0);
    const se::Rgba e1 = se::CramColor(cram, SE_CRAM_RGB555_1024, 1);
    const se::Rgba e2 = se::CramColor(cram, SE_CRAM_RGB555_1024, 2);
    CHECK(e0.r > 200 && e0.g < 40 && e0.b < 40);
    CHECK(e1.g > 200 && e1.r < 40 && e1.b < 40);
    CHECK(e2.b > 200 && e2.r < 40 && e2.g < 40);
    // Wrapping past the end comes back to entry 0, not to some other entry.
    const se::Rgba wrapped = se::CramColor(cram, SE_CRAM_RGB555_1024, 3);
    CHECK(wrapped.r == e0.r && wrapped.g == e0.g && wrapped.b == e0.b);
    // An empty CRAM answers black rather than reading anything.
    std::vector<uint8_t> none;
    const se::Rgba black = se::CramColor(none, SE_CRAM_RGB555_1024, 7);
    CHECK(black.r == 0 && black.g == 0 && black.b == 0 && black.a == 255);
}

}  // namespace

int main()
{
    TestPowerOfTwoWrapsByMasking();
    TestNonPowerOfTwoStillReachesEveryEntry();
    TestPartialCramReadsTheRightColour();
    if (gFailures)
    {
        std::printf("CramWrapTests: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("CramWrapTests: all checks passed\n");
    return 0;
}
