#include "AIInvestigation/Encoding.h"

#include <cstdio>

namespace sfe
{
namespace ai
{
namespace
{

bool ParseHex(const std::string& s, size_t maxDigits, uint64_t* out, std::string* why)
{
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    if (s.size() < 3 || s[0] != '0') return fail("expected a \"0x\"-prefixed upper-case hex string such as \"0x06012340\"");
    if (s[1] == 'X') return fail("the prefix is lower-case \"0x\"");
    if (s[1] != 'x') return fail("expected a \"0x\"-prefixed upper-case hex string such as \"0x06012340\"");
    const size_t digits = s.size() - 2;
    if (digits > maxDigits) return fail(maxDigits == 8 ? "more than eight hex digits: the value does not fit 32 bits"
                                                      : "more than sixteen hex digits: the value does not fit 64 bits");
    uint64_t v = 0;
    for (size_t i = 2; i < s.size(); ++i)
    {
        const char c = s[i];
        uint32_t d;
        if (c >= '0' && c <= '9') d = static_cast<uint32_t>(c - '0');
        else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(c - 'A') + 10u;
        else if (c >= 'a' && c <= 'f') return fail("hex digits must be upper case (\"0xABCD\", not \"0xabcd\")");
        else return fail("not a hex digit");
        v = (v << 4) | d;
    }
    if (out) *out = v;
    return true;
}

}  // namespace

bool ParseHex32(const std::string& s, uint32_t* out, std::string* why)
{
    uint64_t v = 0;
    if (!ParseHex(s, 8, &v, why)) return false;
    if (out) *out = static_cast<uint32_t>(v);
    return true;
}

bool ParseHex64(const std::string& s, uint64_t* out, std::string* why)
{
    return ParseHex(s, 16, out, why);
}

std::string FormatHex(uint64_t value)
{
    char b[24];
    std::snprintf(b, sizeof b, "0x%llX", static_cast<unsigned long long>(value));
    return b;
}

std::string FormatAddress(uint32_t value)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", value);
    return b;
}

bool ParseDecimal64(const std::string& s, uint64_t* out, std::string* why)
{
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    if (s.empty()) return fail("expected a decimal string");
    if (s.size() > 1 && s[0] == '0') return fail("no leading zeros");
    if (s.size() > 20) return fail("does not fit 64 bits");
    uint64_t v = 0;
    for (const char c : s)
    {
        if (c < '0' || c > '9') return fail("digits only");
        const uint64_t d = static_cast<uint64_t>(c - '0');
        if (v > (UINT64_MAX - d) / 10u) return fail("does not fit 64 bits");
        v = v * 10u + d;
    }
    if (out) *out = v;
    return true;
}

std::string FormatDecimal(uint64_t value) { return std::to_string(value); }

bool IsUuid(const std::string& s)
{
    if (s.size() != 36) return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (c != '-') return false;
        }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        {
            return false;
        }
    }
    return true;
}

bool IsIdentifierName(const std::string& s)
{
    if (s.empty() || s.size() > 64) return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        const bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        const bool digit = c >= '0' && c <= '9';
        if (!(alpha || (digit && i > 0))) return false;
    }
    return true;
}

bool IsHandle(const std::string& s)
{
    if (s.empty() || s.size() > 64) return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!(alnum || (i > 0 && (c == '_' || c == '.' || c == '-')))) return false;
    }
    return true;
}

namespace
{
// Decodes one code point starting at s[i]; returns its length in bytes (>= 1) and sets *cp, or 1
// and 0xFFFD for a byte that does not start a well-formed sequence.
size_t Decode(const std::string& s, size_t i, uint32_t* cp)
{
    const unsigned char c = static_cast<unsigned char>(s[i]);
    auto cont = [&](size_t k) { return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80; };
    if (c < 0x80) { *cp = c; return 1; }
    if (c >= 0xC2 && c <= 0xDF && cont(1))
    {
        *cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
        return 2;
    }
    if (c >= 0xE0 && c <= 0xEF && cont(1) && cont(2))
    {
        *cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
              (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4 && cont(1) && cont(2) && cont(3))
    {
        *cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
              ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
        return 4;
    }
    *cp = 0xFFFD;
    return 1;
}
}  // namespace

bool IsSingleLineText(const std::string& s, size_t maxChars, std::string* why)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++n)
    {
        uint32_t cp;
        i += Decode(s, i, &cp);
        if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F) || cp == 0x2028 || cp == 0x2029)
        {
            if (why) *why = "contains a control or line-break character (text must be a single line)";
            return false;
        }
    }
    if (n > maxChars)
    {
        if (why) *why = "longer than " + std::to_string(maxChars) + " characters";
        return false;
    }
    return true;
}

}  // namespace ai
}  // namespace sfe
