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

// Compress 'src'[0..size) into 'out' (cleared then filled). Returns out.size().
size_t FrameLzCompress(const uint8_t* src, size_t size, std::vector<uint8_t>& out);

// Decompress 'src'[0..size) into 'out', which must be resized to the exact
// original length by the caller (the recorder stores it alongside). Returns true
// on success, false if the stream is malformed for that length.
bool FrameLzDecompress(const uint8_t* src, size_t size, uint8_t* out, size_t outSize);

}  // namespace sfe
