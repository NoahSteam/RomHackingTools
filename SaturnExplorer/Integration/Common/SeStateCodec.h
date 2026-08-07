/* Saturn Explorer — savestate delta codec (v16 rewind).
 *
 * A tiny, self-contained codec shared by the emulator-side exporter (se_export.c, C) and the
 * client-side FrameRecorder (C++). It compresses savestate images for the per-frame rewind
 * stream. Two primitives:
 *   - XOR:  delta = full ^ keyframe   (unchanged regions become long runs of zero bytes)
 *   - RLE:  a zero-run / literal-run encoding with LEB128 counts, so megabytes of unchanged
 *           state collapse to a few bytes while arbitrary keyframe bytes still round-trip.
 *
 * The codec is byte-agnostic: neither side interprets savestate contents. Functions are
 * `static inline` so the header can be included from any TU without an extra .c in every build
 * (and unused ones don't warn). Encoders return the encoded length, or 0 if it would not fit
 * in the destination. Decoders return the decoded length, or 0 on malformed/overflowing input.
 */
#ifndef SATURNEXPLORER_SE_STATE_CODEC_H
#define SATURNEXPLORER_SE_STATE_CODEC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* dst = a ^ b, byte-wise, for n bytes. a and b must both be at least n bytes. */
static inline void se_state_xor(unsigned char* dst, const unsigned char* a,
                                const unsigned char* b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) dst[i] = (unsigned char)(a[i] ^ b[i]);
}

/* --- internal LEB128 helpers --- */
static inline size_t se_state_put_varint(unsigned char* p, size_t cap, size_t* pos, size_t v)
{
    for (;;)
    {
        if (*pos >= cap) return 0;
        if (v < 0x80) { p[(*pos)++] = (unsigned char)v; return 1; }
        p[(*pos)++] = (unsigned char)(v | 0x80u);
        v >>= 7;
    }
}
static inline int se_state_get_varint(const unsigned char* p, size_t n, size_t* pos, size_t* out)
{
    size_t v = 0; int sh = 0;
    while (*pos < n)
    {
        unsigned char b = p[(*pos)++];
        v |= (size_t)(b & 0x7Fu) << sh;
        if (!(b & 0x80u)) { *out = v; return 1; }
        sh += 7;
        if (sh >= (int)(sizeof(size_t) * 8)) return 0;   /* overflow guard */
    }
    return 0;
}

/* RLE-encode `src` (n bytes) into `dst` (cap bytes). Format: repeated tokens
 *   0x00 <varint count>            -> `count` zero bytes
 *   0x01 <varint count> <bytes...> -> `count` literal bytes
 * Returns encoded length, or 0 if it would not fit (caller should fall back to a keyframe /
 * treat as incompressible). */
static inline size_t se_state_rle_encode(unsigned char* dst, size_t cap,
                                         const unsigned char* src, size_t n)
{
    size_t out = 0, i = 0;
    while (i < n)
    {
        if (src[i] == 0)
        {
            size_t run = 0;
            while (i < n && src[i] == 0) { ++i; ++run; }
            if (out >= cap) return 0;
            dst[out++] = 0x00;
            if (!se_state_put_varint(dst, cap, &out, run)) return 0;
        }
        else
        {
            size_t start = i;
            while (i < n && src[i] != 0) ++i;
            size_t run = i - start;
            if (out >= cap) return 0;
            dst[out++] = 0x01;
            if (!se_state_put_varint(dst, cap, &out, run)) return 0;
            if (out + run > cap) return 0;
            {
                size_t k;
                for (k = 0; k < run; ++k) dst[out++] = src[start + k];
            }
        }
    }
    return out;
}

/* Inverse of se_state_rle_encode. Returns decoded length (bytes written to dst), or 0 on a
 * malformed stream or if the output would exceed `cap`. */
static inline size_t se_state_rle_decode(unsigned char* dst, size_t cap,
                                         const unsigned char* src, size_t n)
{
    size_t out = 0, pos = 0;
    while (pos < n)
    {
        unsigned char tag = src[pos++];
        size_t count = 0;
        if (!se_state_get_varint(src, n, &pos, &count)) return 0;
        if (tag == 0x00)
        {
            if (out + count > cap) return 0;
            { size_t k; for (k = 0; k < count; ++k) dst[out++] = 0; }
        }
        else if (tag == 0x01)
        {
            if (pos + count > n || out + count > cap) return 0;
            { size_t k; for (k = 0; k < count; ++k) dst[out++] = src[pos++]; }
        }
        else
        {
            return 0;   /* unknown tag */
        }
    }
    return out;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SATURNEXPLORER_SE_STATE_CODEC_H */
