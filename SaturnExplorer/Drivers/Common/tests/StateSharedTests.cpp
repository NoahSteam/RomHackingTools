// The shared endian-normalization helpers the savestate and live drivers both apply to
// emulator memory. What matters here is what they do with input that is NOT the shape the
// format promised: a region that is normalized up to a point and raw after it reads as
// plausible memory, and every 16-bit value past the seam is wrong with nothing to point at.
#include "SaturnStateShared.h"

#include <cstdio>
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

void TestBswap16SwapsWholeWords()
{
    std::vector<uint8_t> v{ 0x11, 0x22, 0x33, 0x44 };
    CHECK(sedrv::Bswap16(v.data(), v.size()));
    CHECK(v[0] == 0x22 && v[1] == 0x11 && v[2] == 0x44 && v[3] == 0x33);

    std::vector<uint8_t> empty;
    CHECK(sedrv::Bswap16(empty.data(), empty.size()));   // nothing to swap is not malformed
}

void TestBswap16RejectsOddLength()
{
    // An odd length means the field is not the u16 array the format declared. The old form
    // rounded down and swapped the rest, leaving the last byte unswapped inside otherwise
    // normalized memory -- so assert both the refusal AND that nothing was touched, since a
    // caller that ignores the refusal must not be handed a half-converted region either.
    std::vector<uint8_t> v{ 0x11, 0x22, 0x33 };
    const std::vector<uint8_t> before = v;
    CHECK(!sedrv::Bswap16(v.data(), v.size()));
    CHECK(v == before);

    std::vector<uint8_t> one{ 0xAB };
    CHECK(!sedrv::Bswap16(one.data(), one.size()));
    CHECK(one[0] == 0xAB);
}

// CRAM mode 2 is RGB888 (4-byte entries); every other mode is 16-bit. A length that is not a
// whole number of entries is refused and left untouched: swapping the whole entries and leaving
// the tail raw published a palette that was right up to a point.
void TestCramNormalizationRefusesPartialEntries()
{
    std::vector<uint8_t> whole{ 0x01, 0x02, 0x03, 0x04 };
    CHECK(sedrv::NormalizeCramToBigEndian(whole, 2));
    CHECK(whole[0] == 0x04 && whole[1] == 0x03 && whole[2] == 0x02 && whole[3] == 0x01);

    std::vector<uint8_t> rgb888{ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
    CHECK(!sedrv::NormalizeCramToBigEndian(rgb888, 2));
    CHECK(rgb888 == (std::vector<uint8_t>{ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 }));

    std::vector<uint8_t> rgb555{ 0x11, 0x22, 0x33 };
    CHECK(!sedrv::NormalizeCramToBigEndian(rgb555, 0));
    CHECK(rgb555 == (std::vector<uint8_t>{ 0x11, 0x22, 0x33 }));
}

}  // namespace

int main()
{
    TestBswap16SwapsWholeWords();
    TestBswap16RejectsOddLength();
    TestCramNormalizationRefusesPartialEntries();
    if (gFailures)
    {
        std::printf("StateSharedTests: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("StateSharedTests: all checks passed\n");
    return 0;
}
