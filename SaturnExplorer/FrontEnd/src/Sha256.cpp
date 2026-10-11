#include "Sha256.h"

#include <cstring>

namespace sfe
{
namespace
{

const uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

}  // namespace

void Sha256::Update(const void* data, size_t size)
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    mLength += size;
    while (size > 0)
    {
        const size_t take = (size < 64 - mUsed) ? size : 64 - mUsed;
        std::memcpy(mBlock + mUsed, p, take);
        mUsed += take;
        p += take;
        size -= take;
        if (mUsed == 64)
        {
            Compress();
            mUsed = 0;
        }
    }
}

std::string Sha256::HexDigest()
{
    const uint64_t bits = mLength * 8u;
    static const unsigned char kPad[64] = { 0x80 };
    Update(kPad, (mUsed < 56 ? 56 : 120) - mUsed);
    unsigned char len[8];
    for (int i = 0; i < 8; ++i) len[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
    Update(len, 8);
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    for (uint32_t h : mState)
        for (int shift = 28; shift >= 0; shift -= 4) out += kHex[(h >> shift) & 0xF];
    return out;
}

namespace
{
uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
}  // namespace

void Sha256::Compress()
{
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t(mBlock[4 * i]) << 24) | (uint32_t(mBlock[4 * i + 1]) << 16) |
               (uint32_t(mBlock[4 * i + 2]) << 8) | uint32_t(mBlock[4 * i + 3]);
    for (int i = 16; i < 64; ++i)
    {
        const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = mState[0], b = mState[1], c = mState[2], d = mState[3];
    uint32_t e = mState[4], f = mState[5], g = mState[6], h = mState[7];
    for (int i = 0; i < 64; ++i)
    {
        const uint32_t t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
        const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    mState[0] += a; mState[1] += b; mState[2] += c; mState[3] += d;
    mState[4] += e; mState[5] += f; mState[6] += g; mState[7] += h;
}

std::string Sha256Hex(const void* data, size_t size)
{
    Sha256 h;
    h.Update(data, size);
    return h.HexDigest();
}

bool IsSha256Hex(const std::string& s)
{
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

}  // namespace sfe
