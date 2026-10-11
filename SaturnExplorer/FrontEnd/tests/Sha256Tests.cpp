// Sha256 tests: the FIPS 180-4 known answers, and that the streaming form agrees with the
// one-shot form wherever the input is split -- the AI package writer hashes files in chunks, and
// a split that crosses the 64-byte block boundary is where a streaming hash goes wrong.
#include "Sha256.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

void TestKnownAnswers()
{
    Check(Sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty");
    Check(Sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc");
    const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    Check(Sha256Hex(two, std::strlen(two)) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "two-block message");
    const std::string million(1000000, 'a');
    Check(Sha256Hex(million.data(), million.size()) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "one million 'a'");
}

// Lengths around the padding thresholds (55/56/63/64/65 bytes) are where the length field spills
// into an extra block.
void TestStreamingMatchesOneShot()
{
    std::string data;
    for (int i = 0; i < 300; ++i) data += static_cast<char>(i * 7 + 3);
    for (size_t len : { size_t(0), size_t(1), size_t(55), size_t(56), size_t(63), size_t(64), size_t(65), size_t(119),
                        size_t(120), size_t(128), size_t(300) })
    {
        const std::string whole = Sha256Hex(data.data(), len);
        for (size_t split = 0; split <= len; split += (len > 70 ? 13 : 1))
        {
            Sha256 h;
            h.Update(data.data(), split);
            h.Update(data.data() + split, len - split);
            if (h.HexDigest() != whole)
            {
                std::printf("FAIL: streaming differs at len=%zu split=%zu\n", len, split);
                ++gFail;
                return;
            }
        }
    }
}

void TestHexValidator()
{
    const std::string good = Sha256Hex("x", 1);
    Check(IsSha256Hex(good), "a digest validates");
    Check(!IsSha256Hex(good.substr(1)), "63 digits are rejected");
    Check(!IsSha256Hex(good + "0"), "65 digits are rejected");
    std::string upper = good;
    for (char& c : upper) if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 32);
    Check(!IsSha256Hex(upper), "upper case is not the canonical spelling");
    std::string bad = good;
    bad[10] = 'g';
    Check(!IsSha256Hex(bad), "a non-hex digit is rejected");
}
}  // namespace

int main()
{
    TestKnownAnswers();
    TestStreamingMatchesOneShot();
    TestHexValidator();
    if (gFail == 0) std::printf("Sha256Tests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
