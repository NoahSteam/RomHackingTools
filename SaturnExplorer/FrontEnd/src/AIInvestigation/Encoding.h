// Encoding -- how values are spelled in AI investigation files (PLAN.md §5.3), and the small text
// grammars the response contract uses (§6.3).
//
//   * Addresses, offsets and lengths are "0x"-prefixed UPPER-case hex strings. A bus address is
//     written with eight digits ("0x06012340"), an offset or length with as few as it needs
//     ("0x2"). The reader accepts one to eight digits for a 32-bit value (leading zeros are not
//     significant) but only this spelling: lower-case digits, "0X" and bare hex are errors, never
//     quietly accepted, so one value has one canonical spelling in everything SE writes.
//   * Counters and frame numbers that may pass 2^53 are plain decimal strings, because a JSON
//     number is a double in many readers.
//   * IDs are lower-case hyphenated UUID strings.
//
// Pure functions, no ImGui and no platform headers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sfe
{
namespace ai
{

// "0x" + 1..8 upper-case hex digits. On failure *why says what is wrong.
bool ParseHex32(const std::string& s, uint32_t* out, std::string* why = nullptr);
// "0x" + 1..16 upper-case hex digits.
bool ParseHex64(const std::string& s, uint64_t* out, std::string* why = nullptr);

std::string FormatHex(uint64_t value);          // "0x2A": no padding
std::string FormatAddress(uint32_t value);      // "0x06012340": eight digits

// Digits only: no sign, no space, no leading zero except "0" itself, and at most 2^64-1.
bool ParseDecimal64(const std::string& s, uint64_t* out, std::string* why = nullptr);
std::string FormatDecimal(uint64_t value);

// Lower-case 8-4-4-4-12 hexadecimal with hyphens. The version and variant nibbles are not
// inspected: SE writes v4 IDs, but an ID is an opaque name.
bool IsUuid(const std::string& s);

// [A-Za-z_][A-Za-z0-9_]{0,63}: the grammar of labels, function names and watch names.
bool IsIdentifierName(const std::string& s);

// A handle for an item inside one document (hypothesis, action, evidence item or range):
// [A-Za-z0-9][A-Za-z0-9_.-]{0,63}. Unlike a name it may start with a digit and contain '.' and '-',
// so "h1", "a-2" and "range-12" are all handles. It never contains '/', which separates an item from
// its range in an evidence reference ("diff/range-12").
bool IsHandle(const std::string& s);

// One line of text of at most 'maxChars' characters with no control characters: C0 (including tab,
// CR and LF), DEL, C1, and the Unicode line and paragraph separators U+2028 / U+2029. Empty text is
// allowed here; callers that need non-empty text check that themselves.
bool IsSingleLineText(const std::string& s, size_t maxChars, std::string* why = nullptr);

}  // namespace ai
}  // namespace sfe
