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
#include <stdint.h>   /* SIZE_MAX */
#include <string.h>   /* memcpy/memset */

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
/* Read a LEB128 value into *out. Returns 1 on success, 0 on a truncated or oversized encoding.
 *
 * A group's bits are checked against the width still unfilled BEFORE the shift, not after. The
 * shift itself is what loses them: `(size_t)2 << 63` on a 64-bit size_t is a well-defined 0, so
 * a final group of 0x02 after nine continuation groups used to drop its high bit and return
 * success, handing the caller a number the sender never wrote. The old `sh >= width` guard
 * could not catch it, because it only ran after `sh += 7` and a terminating group returns
 * first. 32-bit has the same shape at sh == 28 with a final group above 0x0F.
 *
 * `room` is at least 1 on every iteration (sh < width holds at the top of the loop), so the
 * `chunk >> room` test never shifts by the full width. */
static inline int se_state_get_varint(const unsigned char* p, size_t n, size_t* pos, size_t* out)
{
    const int width = (int)(sizeof(size_t) * 8);
    size_t v = 0; int sh = 0;
    while (*pos < n)
    {
        unsigned char b = p[(*pos)++];
        size_t chunk = (size_t)(b & 0x7Fu);
        const int room = width - sh;
        if (room < 7 && (chunk >> room) != 0) return 0;   /* would not survive the shift */
        v |= chunk << sh;
        if (!(b & 0x80u)) { *out = v; return 1; }
        sh += 7;
        if (sh >= width) return 0;   /* no room for another group at all */
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
            if (run > cap - out) return 0;   /* not out + run: the sum can wrap */
            memcpy(dst + out, src + start, run);
            out += run;
        }
    }
    return out;
}

/* Inverse of se_state_rle_encode. Returns the decoded length (bytes written to dst), or 0 on a
 * malformed stream or one whose output would exceed `cap`. A NULL `dst` measures instead of
 * decoding -- see se_state_rle_decoded_size, which is that call.
 *
 * The bounds are written `count > cap - out` rather than `out + count > cap` because `count`
 * comes straight off the wire and can be near SIZE_MAX: the sum wraps, passes the check, and
 * the copy then runs off the end of dst. `out <= cap` and `pos <= n` hold on every iteration,
 * so neither subtraction can underflow. */
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
            if (count > cap - out) return 0;
            if (dst && count) memset(dst + out, 0, count);
            out += count;
        }
        else if (tag == 0x01)
        {
            if (count > n - pos || count > cap - out) return 0;
            if (dst && count) memcpy(dst + out, src + pos, count);
            out += count;
            pos += count;
        }
        else
        {
            return 0;   /* unknown tag */
        }
    }
    return out;
}

/* The length se_state_rle_decode would produce, without materializing it (0 if malformed).
 * Lets a receiver check a payload against the full length its sender declared before storing
 * it: one token walk, no allocation, and zero runs are counted rather than written. */
static inline size_t se_state_rle_decoded_size(const unsigned char* src, size_t n)
{
    return se_state_rle_decode(NULL, SIZE_MAX, src, n);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SATURNEXPLORER_SE_STATE_CODEC_H */
