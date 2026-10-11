// Encoding tests (PLAN.md §5.3, §6.3): one canonical spelling for addresses, offsets, lengths and
// counters, and the text grammars of names, handles and comments.
#include "AIInvestigation/Encoding.h"

#include <cstdio>
#include <string>

using namespace sfe::ai;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

void TestHex()
{
    uint32_t v = 0;
    std::string why;
    Check(ParseHex32("0x06012340", &v) && v == 0x06012340u, "eight digits");
    Check(ParseHex32("0x2", &v) && v == 2, "one digit");
    Check(ParseHex32("0x0", &v) && v == 0, "zero");
    Check(ParseHex32("0xFFFFFFFF", &v) && v == 0xFFFFFFFFu, "32-bit maximum");
    Check(ParseHex32("0x0000002A", &v) && v == 0x2A, "leading zeros are not significant");
    Check(ParseHex32("0xABCDEF01", &v) && v == 0xABCDEF01u, "A-F upper case");

    // Everything else is refused rather than normalised: one value, one spelling.
    Check(!ParseHex32("0x0601234a", &v, &why) && why.find("upper case") != std::string::npos, "lower-case digit is refused, and says why");
    Check(!ParseHex32("0X06012340", &v, &why) && why.find("prefix") != std::string::npos, "upper-case prefix is refused");
    Check(!ParseHex32("06012340", &v), "no prefix");
    Check(!ParseHex32("0x", &v), "no digits");
    Check(!ParseHex32("", &v), "empty");
    Check(!ParseHex32("0x100000000", &v, &why) && why.find("32 bits") != std::string::npos, "nine digits do not fit 32 bits");
    Check(!ParseHex32("0x0G", &v), "non-hex digit");
    Check(!ParseHex32(" 0x1", &v) && !ParseHex32("0x1 ", &v), "no whitespace");
    Check(!ParseHex32("-0x1", &v) && !ParseHex32("+0x1", &v), "no sign");
    Check(!ParseHex32("0x1_000", &v), "no separators");
    Check(!ParseHex32("1", &v) && !ParseHex32("16", &v), "decimal is not hex");

    uint64_t w = 0;
    Check(ParseHex64("0xFFFFFFFFFFFFFFFF", &w) && w == UINT64_MAX, "64-bit maximum");
    Check(!ParseHex64("0x10000000000000000", &w), "seventeen digits do not fit");
    Check(!ParseHex64("0xffff", &w), "64-bit parser is upper case too");

    Check(FormatHex(0) == "0x0" && FormatHex(2) == "0x2" && FormatHex(0xABCD) == "0xABCD", "FormatHex is minimal, upper case");
    Check(FormatHex(UINT64_MAX) == "0xFFFFFFFFFFFFFFFF", "FormatHex 64-bit");
    Check(FormatAddress(0x06012340) == "0x06012340" && FormatAddress(0x2A) == "0x0000002A" && FormatAddress(0xFFFFFFFFu) == "0xFFFFFFFF",
          "FormatAddress is eight digits");
    for (uint32_t x : { 0u, 1u, 0xFu, 0x10u, 0xDEADBEEFu, 0xFFFFFFFFu })
    {
        Check(ParseHex32(FormatAddress(x), &v) && v == x, "FormatAddress round trips");
        Check(ParseHex32(FormatHex(x), &v) && v == x, "FormatHex round trips");
    }
}

void TestDecimal()
{
    uint64_t v = 0;
    Check(ParseDecimal64("0", &v) && v == 0, "zero");
    Check(ParseDecimal64("9007199254740993", &v) && v == 9007199254740993ull, "2^53+1 is exact");
    Check(ParseDecimal64("18446744073709551615", &v) && v == UINT64_MAX, "2^64-1");
    Check(!ParseDecimal64("18446744073709551616", &v), "2^64 does not fit");
    Check(!ParseDecimal64("99999999999999999999", &v), "twenty nines do not fit");
    Check(!ParseDecimal64("123456789012345678901", &v), "twenty-one digits");
    Check(!ParseDecimal64("", &v), "empty");
    Check(!ParseDecimal64("01", &v), "no leading zeros");
    Check(!ParseDecimal64("-1", &v) && !ParseDecimal64("+1", &v), "no sign");
    Check(!ParseDecimal64("1.0", &v) && !ParseDecimal64("1e3", &v), "digits only");
    Check(!ParseDecimal64(" 1", &v), "no whitespace");
    Check(FormatDecimal(0) == "0" && FormatDecimal(UINT64_MAX) == "18446744073709551615", "FormatDecimal");
    Check(ParseDecimal64(FormatDecimal(9007199254740993ull), &v) && v == 9007199254740993ull, "round trip");
}

void TestUuid()
{
    Check(IsUuid("2f1c0a9e-4b7d-4c21-9a3e-0d5b6c7e8f90"), "a v4 UUID");
    Check(IsUuid("00000000-0000-0000-0000-000000000000"), "nil UUID (opaque ID)");
    Check(!IsUuid("2F1C0A9E-4B7D-4C21-9A3E-0D5B6C7E8F90"), "upper case is not canonical");
    Check(!IsUuid("2f1c0a9e4b7d4c219a3e0d5b6c7e8f90"), "hyphens required");
    Check(!IsUuid("2f1c0a9e-4b7d-4c21-9a3e-0d5b6c7e8f9"), "short");
    Check(!IsUuid("2f1c0a9e-4b7d-4c21-9a3e-0d5b6c7e8f900"), "long");
    Check(!IsUuid("2f1c0a9e-4b7d-4c21-9a3e-0d5b6c7e8f9g"), "non-hex");
    Check(!IsUuid("{2f1c0a9e-4b7d-4c21-9a3e-0d5b6c7e8f90}"), "braces");
    Check(!IsUuid("2f1c"), "the abbreviated ID in the plan's example is not a UUID");
}

void TestNames()
{
    Check(IsIdentifierName("GeneralHP") && IsIdentifierName("_x") && IsIdentifierName("a1_B2"), "ordinary names");
    Check(IsIdentifierName(std::string(64, 'a')), "64 characters");
    Check(!IsIdentifierName(std::string(65, 'a')), "65 characters");
    Check(!IsIdentifierName("") && !IsIdentifierName("1abc"), "empty, or starting with a digit");
    Check(!IsIdentifierName("a b") && !IsIdentifierName("a-b") && !IsIdentifierName("a.b") && !IsIdentifierName("a/b"), "no separators");
    Check(!IsIdentifierName("h\xC3\xA9") && !IsIdentifierName("a\n"), "ASCII only, no control characters");
    Check(IsHandle("h1") && IsHandle("1") && IsHandle("range-12") && IsHandle("a.b_c-d"), "handles");
    Check(!IsHandle("") && !IsHandle("-a") && !IsHandle("a/b") && !IsHandle("a b") && !IsHandle(std::string(65, 'a')), "bad handles");
}

void TestSingleLineText()
{
    std::string why;
    Check(IsSingleLineText("", 256), "empty is allowed here");
    Check(IsSingleLineText("HP is stored as a u16 at +0x10", 256), "ordinary comment");
    Check(IsSingleLineText(std::string(256, 'x'), 256), "256 characters");
    Check(!IsSingleLineText(std::string(257, 'x'), 256, &why) && why.find("256") != std::string::npos, "257 characters");

    // The limit counts characters, not bytes.
    std::string e;
    for (int i = 0; i < 256; ++i) e += "\xC3\xA9";
    Check(IsSingleLineText(e, 256) && e.size() == 512, "256 two-byte characters fit a 256-character limit");
    Check(!IsSingleLineText(e + "\xC3\xA9", 256), "257 characters of two bytes do not");
    std::string cjk;
    for (int i = 0; i < 100; ++i) cjk += "\xE3\x81\x82";
    Check(IsSingleLineText(cjk, 100) && CountCodePoints(cjk) == 100, "three-byte characters count once");
    Check(CountCodePoints("\xF0\x9F\x98\x80") == 1, "a four-byte character counts once");

    for (const char* bad : { "a\nb", "a\rb", "a\tb", "a\x01" "b", "a\x1F" "b", "a\x7F" "b", "a\xC2\x85" "b", "a\xC2\x9F" "b",
                             "a\xE2\x80\xA8" "b", "a\xE2\x80\xA9" "b" })
        Check(!IsSingleLineText(bad, 256), "a control or separator character is refused");
    Check(IsSingleLineText("a\xC2\xA0" "b", 256), "no-break space is ordinary text");
}
}  // namespace

int main()
{
    TestHex();
    TestDecimal();
    TestUuid();
    TestNames();
    TestSingleLineText();
    if (gFail == 0) std::printf("AIEncodingTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
