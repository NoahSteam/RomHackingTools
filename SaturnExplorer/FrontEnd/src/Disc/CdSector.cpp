#include "Disc/CdSector.h"

#include <cstring>

namespace sfe
{
namespace
{
// Galois-field lookup tables for the CD-ROM Reed-Solomon ECC, and the EDC CRC table. Built once
// on first use. This is Neill Corlett's "ecm" formulation, the de-facto standard shared by every
// CD-ROM tool (bchunk, ecm, mednafen's CD layer, ...).
uint8_t  gEccF[256];
uint8_t  gEccB[256];
uint32_t gEdc[256];
bool     gReady = false;

void InitTables()
{
    for (int i = 0; i < 256; ++i)
    {
        const uint8_t j = uint8_t((i << 1) ^ ((i & 0x80) ? 0x11D : 0));
        gEccF[i] = j;
        gEccB[i ^ j] = uint8_t(i);
        uint32_t edc = uint32_t(i);
        for (int k = 0; k < 8; ++k) edc = (edc >> 1) ^ ((edc & 1) ? 0xD8018001u : 0);
        gEdc[i] = edc;
    }
    gReady = true;
}

uint32_t EdcCompute(const uint8_t* src, size_t size)
{
    uint32_t edc = 0;
    while (size--) edc = (edc >> 8) ^ gEdc[(edc ^ *src++) & 0xFF];
    return edc;
}

// One ECC pass (P or Q). Reads the interleaved bytes of 'src' and writes 2*majorCount parity
// bytes to 'dest'. Parameters differ for the P and Q codes; values per the CD-ROM spec.
void EccBlock(const uint8_t* src, uint32_t majorCount, uint32_t minorCount,
              uint32_t majorMult, uint32_t minorMult, uint8_t* dest)
{
    const uint32_t size = majorCount * minorCount;
    for (uint32_t major = 0; major < majorCount; ++major)
    {
        uint32_t index = (major >> 1) * majorMult + (major & 1);
        uint8_t eccA = 0, eccB = 0;
        for (uint32_t minor = 0; minor < minorCount; ++minor)
        {
            const uint8_t temp = src[index];
            index += minorMult;
            if (index >= size) index -= size;
            eccA ^= temp;
            eccB ^= temp;
            eccA = gEccF[eccA];
        }
        eccA = gEccB[gEccF[eccA] ^ eccB];
        dest[major] = eccA;
        dest[major + majorCount] = uint8_t(eccA ^ eccB);
    }
}

uint8_t Bcd(uint8_t v) { return uint8_t(((v / 10) << 4) | (v % 10)); }
}  // namespace

void EncodeMode1Sector(uint32_t lba, const uint8_t* user, uint8_t* out)
{
    if (!gReady) InitTables();
    std::memset(out, 0, 2352);

    // Sync pattern.
    std::memset(out + 1, 0xFF, 10);   // out[0] and out[11] stay 0

    // Address: MSF (BCD) of lba + 150 (the standard 2-second lead-in), then mode 1.
    const uint32_t a = lba + 150;
    out[12] = Bcd(uint8_t(a / (75 * 60)));
    out[13] = Bcd(uint8_t((a / 75) % 60));
    out[14] = Bcd(uint8_t(a % 75));
    out[15] = 0x01;

    std::memcpy(out + 16, user, 2048);

    // EDC over the sync + address + user data (bytes 0..2063), stored little-endian.
    const uint32_t edc = EdcCompute(out, 2064);
    out[2064] = uint8_t(edc);
    out[2065] = uint8_t(edc >> 8);
    out[2066] = uint8_t(edc >> 16);
    out[2067] = uint8_t(edc >> 24);
    // out[2068..2075] reserved (zeroed above).

    // ECC P and Q over the address + data + EDC + reserved (address is NOT zeroed for Mode 1).
    EccBlock(out + 12, 86, 24, 2, 86, out + 2076);   // P: 172 bytes
    EccBlock(out + 12, 52, 43, 86, 88, out + 2248);  // Q: 104 bytes
}

}  // namespace sfe
