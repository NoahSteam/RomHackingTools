// Unit tests for the savestate delta codec (Integration/Common/SeStateCodec.h), the
// XOR + zero-run-RLE primitives shared by the emulator exporter (se_export.c) and the
// client FrameRecorder for the v16 rewind stream. The codec is byte-agnostic, so these
// tests drive it with synthetic buffers only — no emulator or savestate is involved.
#include "SeStateCodec.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

// RLE round-trip: encode 'src' then decode and require it comes back identical.
bool RleRoundTrips(const std::vector<unsigned char>& src)
{
    std::vector<unsigned char> enc(src.size() * 2 + 16);
    const size_t elen = se_state_rle_encode(enc.data(), enc.size(), src.data(), src.size());
    if (src.empty()) return elen == 0;   // empty input -> empty output
    if (elen == 0) return false;
    std::vector<unsigned char> dec(src.size());
    const size_t dlen = se_state_rle_decode(dec.data(), dec.size(), enc.data(), elen);
    return dlen == src.size() && dec == src;
}
}  // namespace

int main()
{
    // --- XOR is its own inverse ---
    {
        const unsigned char a[8] = { 0x00, 0xFF, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC };
        const unsigned char b[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
        unsigned char d[8], back[8];
        se_state_xor(d, a, b, 8);
        se_state_xor(back, d, b, 8);   // (a^b)^b == a
        bool ok = true;
        for (int i = 0; i < 8; ++i) ok = ok && back[i] == a[i];
        Check(ok, "xor is self-inverse");
    }

    // --- RLE round-trips across shapes ---
    Check(RleRoundTrips({}), "rle empty");
    Check(RleRoundTrips(std::vector<unsigned char>(1000, 0)), "rle all-zero collapses + round-trips");
    {
        std::vector<unsigned char> lit(300);
        for (size_t i = 0; i < lit.size(); ++i) lit[i] = (unsigned char)(i * 7 + 1);   // no zeros
        Check(RleRoundTrips(lit), "rle all-literal round-trips");
    }
    {
        // Zero runs interleaved with literals, including runs that exceed one varint byte.
        std::vector<unsigned char> mix;
        mix.insert(mix.end(), 500, 0);
        for (int i = 0; i < 10; ++i) mix.push_back((unsigned char)(i + 1));
        mix.insert(mix.end(), 3, 0);
        mix.push_back(0xAB);
        mix.insert(mix.end(), 130, 0);   // > 127 -> multi-byte varint count
        Check(RleRoundTrips(mix), "rle mixed zero/literal round-trips");
    }

    // --- A mostly-zero delta compresses to far less than the original ---
    {
        std::vector<unsigned char> full(64 * 1024, 0);
        full[100] = 0x5A; full[40000] = 0x33;   // two changed bytes vs an all-zero keyframe
        std::vector<unsigned char> enc(full.size() + 16);
        const size_t elen = se_state_rle_encode(enc.data(), enc.size(), full.data(), full.size());
        Check(elen != 0 && elen < full.size() / 100, "sparse delta compresses hugely");
    }

    // --- Keyframe + delta reconstruction: full == keyframe ^ delta, both via the codec ---
    {
        const size_t n = 4096;
        std::vector<unsigned char> keyframe(n), full(n);
        for (size_t i = 0; i < n; ++i) keyframe[i] = (unsigned char)(i * 3 + 5);
        full = keyframe;
        full[10] ^= 0xFF; full[2000] = 0x77; full[4095] = 0x01;   // a few frame-to-frame changes

        std::vector<unsigned char> delta(n);
        se_state_xor(delta.data(), full.data(), keyframe.data(), n);   // delta = full ^ keyframe

        // Wire form: RLE(keyframe) and RLE(delta).
        std::vector<unsigned char> encKf(n * 2 + 16), encD(n * 2 + 16);
        const size_t lkf = se_state_rle_encode(encKf.data(), encKf.size(), keyframe.data(), n);
        const size_t ld  = se_state_rle_encode(encD.data(),  encD.size(),  delta.data(),    n);
        Check(lkf != 0 && ld != 0, "keyframe + delta encode");
        // The delta is mostly zero, so it must be much smaller than the keyframe.
        Check(ld < lkf, "delta smaller than keyframe");

        // Decode both, reconstruct full = keyframe ^ delta, compare to the original.
        std::vector<unsigned char> decKf(n), decD(n), recon(n);
        const size_t dkf = se_state_rle_decode(decKf.data(), n, encKf.data(), lkf);
        const size_t dd  = se_state_rle_decode(decD.data(),  n, encD.data(),  ld);
        Check(dkf == n && dd == n, "keyframe + delta decode to full length");
        se_state_xor(recon.data(), decKf.data(), decD.data(), n);
        Check(recon == full, "reconstructed frame matches original");
    }

    // --- Encoder refuses a too-small destination (caller falls back to a keyframe) ---
    {
        std::vector<unsigned char> lit(100);
        for (size_t i = 0; i < lit.size(); ++i) lit[i] = (unsigned char)(i + 1);
        unsigned char tiny[8];
        Check(se_state_rle_encode(tiny, sizeof(tiny), lit.data(), lit.size()) == 0,
              "encode into too-small buffer returns 0");
    }

    // --- Decoder rejects malformed / overflowing input ---
    {
        unsigned char dst[64];
        const unsigned char badTag[2] = { 0x02, 0x05 };   // 0x02 is not a valid token
        Check(se_state_rle_decode(dst, sizeof(dst), badTag, sizeof(badTag)) == 0,
              "decode unknown tag returns 0");
        // A zero-run claiming more bytes than the destination can hold must fail, not overflow.
        const unsigned char bigZero[3] = { 0x00, 0x80, 0x01 };   // count = 128 into a 64-byte dst
        Check(se_state_rle_decode(dst, sizeof(dst), bigZero, sizeof(bigZero)) == 0,
              "decode overflowing zero-run returns 0");
        // A literal run whose byte payload is truncated must fail.
        const unsigned char truncLit[2] = { 0x01, 0x05 };   // says 5 literals, none follow
        Check(se_state_rle_decode(dst, sizeof(dst), truncLit, sizeof(truncLit)) == 0,
              "decode truncated literal run returns 0");
    }

    if (gFail == 0) std::printf("All SeStateCodec tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
