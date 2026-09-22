// BinaryWriter — the little-endian byte emitters and the BMP encoders the app saves
// images with.
//
// BMP is what every image this app writes uses — the Texture Viewer's export, the F12
// screenshot, and the per-layer viewers' export — so the encoder lives here rather than
// in whichever feature happened to need it first. Depends on nothing but the standard
// library, so it is usable from the non-ImGui export path and its tests.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"   // se_palette, for the indexed encoder

namespace sfe
{

// Append a little-endian integer. Used by every container the app writes (BMP, WAV, the
// .sedump section table), all of which are little-endian on the wire.
void PushU16(std::vector<uint8_t>& v, uint16_t x);
void PushU32(std::vector<uint8_t>& v, uint32_t x);

// Assemble a Windows BMP. If paletteCount > 0: an 8-bpp indexed BMP with color table
// 'palBGRA' (paletteCount*4 bytes, B,G,R,0) and 'pixels' = row-major top-down indices.
// Otherwise: a 24-bpp BMP with 'pixels' = row-major top-down RGBA (4 bytes/pixel).
// Rows are written bottom-up (BMP convention) and padded to a 4-byte boundary.
std::vector<uint8_t> BuildBmp(int w, int h, const std::vector<uint8_t>& pixels,
                              int paletteCount, const uint8_t* palBGRA);

// A 32-bpp BGRA BMP from row-major top-down RGBA. Used where transparency matters — a
// single layer covers only part of the screen and a tileset is mostly see-through, so
// flattening those to 24 bpp would turn "nothing here" into black.
std::vector<uint8_t> BuildBmpRgba(int w, int h, const std::vector<uint8_t>& rgba);

// A decoded image as an indexed BMP carrying the game's own palette, so the export can be
// re-palettised in an image editor. The core only decodes to RGBA, so the indices are
// reconstructed by exact RGBA match; a palette that repeats a colour resolves to its first
// index, and a miss falls back to 0.
// TODO: a core indexed-decode entry point would make this exact and lossless.
std::vector<uint8_t> BuildIndexedBmp(int w, int h, const std::vector<uint8_t>& rgba,
                                     const se_palette& palette);

}  // namespace sfe
