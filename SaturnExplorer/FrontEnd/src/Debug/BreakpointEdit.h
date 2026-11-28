// Text parsing for the Breakpoints panel's Edit popup. Header-only and ImGui-free so it can be
// unit-tested; the panel just feeds it the raw InputText buffers.
#pragma once

#include <cstdint>
#include <string>

namespace sfe
{

namespace bpedit_detail
{
inline std::string Trim(const std::string& s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}
inline int HexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// Digits-only parse in 'base' (10 or 16); false on empty, a bad digit or >32-bit overflow.
inline bool ParseDigits(const std::string& d, int base, uint32_t& out)
{
    if (d.empty()) return false;
    uint64_t v = 0;
    for (char c : d)
    {
        const int h = HexVal(c);
        if (h < 0 || h >= base) return false;
        v = v * static_cast<uint64_t>(base) + static_cast<uint64_t>(h);
        if (v > 0xFFFFFFFFull) return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
}
inline bool HasHexPrefix(const std::string& t)
{
    return t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X');
}
}  // namespace bpedit_detail

// Hex address: optional 0x/0X, surrounding blanks ignored. No sign, no inner spaces.
inline bool ParseBpAddress(const std::string& text, uint32_t& out)
{
    using namespace bpedit_detail;
    std::string t = Trim(text);
    if (HasHexPrefix(t)) t = t.substr(2);
    return ParseDigits(t, 16, out);
}

// Byte count: decimal by default ("0299" is 299, never octal), hex only with a 0x prefix.
inline bool ParseBpByteCount(const std::string& text, uint32_t& out)
{
    using namespace bpedit_detail;
    const std::string t = Trim(text);
    if (HasHexPrefix(t)) return ParseDigits(t.substr(2), 16, out);
    return ParseDigits(t, 10, out);
}

}  // namespace sfe
