// ShiftJis — minimal Shift-JIS (JIS X 0208) decoding for the Hex Editor's text
// pane, so Japanese game strings render as glyphs instead of dots. Pure, no ImGui.
#pragma once

#include <cstdint>

namespace sfe
{

// True if 'b' can start a Shift-JIS double-byte sequence (JIS X 0208 lead byte).
bool SjisIsLead(uint8_t b);
// True if 'b' is a valid Shift-JIS trail byte.
bool SjisIsTrail(uint8_t b);
// True if 'b' is a single-byte half-width katakana (0xA1..0xDF).
bool SjisIsHalfKana(uint8_t b);

// Decode a lead+trail double-byte pair to a Unicode codepoint; 0 if not mapped.
uint32_t SjisDecode(uint8_t lead, uint8_t trail);
// Decode a single half-width-katakana byte to its Unicode codepoint (U+FF61..).
uint32_t SjisHalfKana(uint8_t b);

// Encode a codepoint as UTF-8 into 'out' (needs >= 5 bytes, NUL-terminated).
// Returns the byte length (excluding the terminator).
int Utf8Encode(uint32_t cp, char* out);

}  // namespace sfe
