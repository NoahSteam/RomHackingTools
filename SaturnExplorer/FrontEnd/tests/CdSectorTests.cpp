// Tests for the MODE1/2352 raw-sector encoder: field layout, the MSF address, EDC self-
// consistency, and sensitivity (a changed payload must change EDC and ECC). The definitive
// end-to-end proof (build a real .bin and read it back) lives in the disc-build round-trip.
#include "Disc/CdSector.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; } }

// Independent EDC (same CD-ROM polynomial, computed straight rather than via the encoder) so the
// stored EDC is checked against a second implementation, not the encoder's own table use.
uint32_t RefEdc(const uint8_t* p, size_t n)
{
    uint32_t edc = 0;
    for (size_t i = 0; i < n; ++i)
    {
        edc ^= p[i];
        for (int b = 0; b < 8; ++b) edc = (edc >> 1) ^ ((edc & 1) ? 0xD8018001u : 0);
    }
    return edc;
}
}  // namespace

int main()
{
    std::vector<uint8_t> user(2048);
    for (size_t i = 0; i < user.size(); ++i) user[i] = uint8_t(i * 7 + 3);

    uint8_t s[2352];
    EncodeMode1Sector(0, user.data(), s);

    // Sync pattern.
    Check(s[0] == 0x00, "sync[0]");
    bool sync = true;
    for (int i = 1; i <= 10; ++i) if (s[i] != 0xFF) sync = false;
    Check(sync, "sync FF x10");
    Check(s[11] == 0x00, "sync[11]");

    // Address: lba 0 -> lba+150 = 150 frames = 00:02:00 (BCD), mode 1.
    Check(s[12] == 0x00 && s[13] == 0x02 && s[14] == 0x00, "MSF of lba 0 is 00:02:00");
    Check(s[15] == 0x01, "mode byte");

    // User data placed at offset 16.
    Check(std::memcmp(s + 16, user.data(), 2048) == 0, "user data at offset 16");

    // EDC over bytes [0..2063], little-endian at 2064, matches the independent computation.
    const uint32_t edc = RefEdc(s, 2064);
    Check(s[2064] == uint8_t(edc) && s[2065] == uint8_t(edc >> 8) &&
          s[2066] == uint8_t(edc >> 16) && s[2067] == uint8_t(edc >> 24), "EDC matches");

    // Reserved 8 bytes are zero.
    bool reserved = true;
    for (int i = 2068; i < 2076; ++i) if (s[i] != 0) reserved = false;
    Check(reserved, "reserved zero");

    // ECC is present (not all-zero) and the address MSF differs by sector.
    bool eccNonZero = false;
    for (int i = 2076; i < 2352; ++i) if (s[i] != 0) eccNonZero = true;
    Check(eccNonZero, "ECC present");

    uint8_t s2[2352];
    EncodeMode1Sector(1, user.data(), s2);
    Check(s2[12] == 0x00 && s2[13] == 0x02 && s2[14] == 0x01, "MSF of lba 1 is 00:02:01");
    // A different address changes EDC and ECC even with identical payload.
    Check(std::memcmp(s + 2064, s2 + 2064, 2352 - 2064) != 0, "EDC/ECC depend on address");

    // Idempotent: encoding the same input twice yields identical bytes.
    uint8_t s3[2352];
    EncodeMode1Sector(0, user.data(), s3);
    Check(std::memcmp(s, s3, 2352) == 0, "encode is deterministic");

    // Sensitive: flipping one payload byte changes EDC and ECC.
    user[1000] ^= 0xFF;
    uint8_t s4[2352];
    EncodeMode1Sector(0, user.data(), s4);
    Check(std::memcmp(s + 2064, s4 + 2064, 2352 - 2064) != 0, "EDC/ECC depend on payload");

    if (gFail == 0) std::printf("All CdSector tests passed.\n");
    return gFail ? 1 : 0;
}
