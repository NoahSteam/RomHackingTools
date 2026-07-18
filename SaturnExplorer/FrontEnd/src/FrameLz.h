// FrameLz — a small, self-contained LZ (LZSS-style) codec used by the frame
// recorder to compress captured Saturn memory regions. No external dependency,
// so it builds identically on the desktop and Windows frontends.
//
// Format: a stream of groups. Each group starts with one flag byte whose 8 bits
// (LSB first) mark the next 8 tokens: 1 = literal (one byte follows), 0 = match
// (three bytes follow: offset low, offset high, length). offset is 1..65535 back
// from the current output position; length is 3..258 (stored as length-3). A
// group may end early at the end of input. Decompression is a plain byte copy,
// so it's fast and allocation-free apart from the output buffer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sfe
{

// Reusable match-finder scratch. Hoist one of these out of a hot loop and pass it
// to FrameLzCompress so the (large) hash-chain buffers are allocated once and
// reused across calls instead of per call. Contents are transient — no meaning
// between calls.
struct FrameLzScratch
{
    std::vector<int32_t> head;   // hash bucket -> most recent position
    std::vector<int32_t> prev;   // position   -> previous position with same hash
};

// Compress 'src'[0..size) into 'out' (cleared then filled). Returns out.size().
// The first form allocates its match-finder scratch internally; the second reuses
// caller-owned scratch (see FrameLzScratch) to avoid per-call allocation.
size_t FrameLzCompress(const uint8_t* src, size_t size, std::vector<uint8_t>& out);
size_t FrameLzCompress(const uint8_t* src, size_t size, std::vector<uint8_t>& out,
                       FrameLzScratch& scratch);

// Decompress 'src'[0..size) into 'out', which must be resized to the exact
// original length by the caller (the recorder stores it alongside). Returns true
// on success, false if the stream is malformed for that length.
bool FrameLzDecompress(const uint8_t* src, size_t size, uint8_t* out, size_t outSize);

}  // namespace sfe
