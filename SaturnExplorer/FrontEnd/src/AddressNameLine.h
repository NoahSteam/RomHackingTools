// AddressNameLine -- one line of a "<hex-address> <name>" text file: the function names (saturn_functions.txt), a symbol map
// brought in with Load Symbols, and the assembly notes (assembly_comments.txt, "<hex-address> <note>").
//
// The address is one to eight hex digits, optionally 0x-prefixed, all of the line's first word; then white space; then the
// name. Blank lines and lines opening with ; # // are comments. Any other line is Malformed and the caller skips it and
// counts it. The readers used strtoul / sscanf on the front of the line, so "100000000 f" named address 0 (the high bits
// cut off), "-1 f" named 0xFFFFFFFF, and "0608<garbage>" named 0x608 after the digits it began with (A9-2, A9-10).
#pragma once

#include <cctype>
#include <cstdint>
#include <string>

namespace sfe
{

enum class AddressNameLine { Entry, Comment, Malformed };

inline AddressNameLine ParseAddressNameLine(const std::string& line, uint32_t& addr, std::string& name)
{
    auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    size_t b = 0;
    while (b < line.size() && space(line[b])) ++b;
    if (b >= line.size()) return AddressNameLine::Comment;
    if (line[b] == ';' || line[b] == '#' ||
        (line[b] == '/' && b + 1 < line.size() && line[b + 1] == '/')) return AddressNameLine::Comment;

    size_t e = b;
    while (e < line.size() && !space(line[e])) ++e;
    size_t d = b;
    if (e - b > 2 && line[b] == '0' && (line[b + 1] == 'x' || line[b + 1] == 'X')) d = b + 2;
    if (d == e || e - d > 8) return AddressNameLine::Malformed;
    uint32_t a = 0;
    for (size_t i = d; i < e; ++i)
    {
        const char c = line[i];
        if (!std::isxdigit(static_cast<unsigned char>(c))) return AddressNameLine::Malformed;
        a = (a << 4) | static_cast<uint32_t>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                                                                                         : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
    }
    size_t np = e;
    while (np < line.size() && space(line[np])) ++np;
    size_t ne = line.size();
    while (ne > np && space(line[ne - 1])) --ne;   // trim CR / trailing ws
    if (np >= ne) return AddressNameLine::Malformed;
    addr = a;
    name = line.substr(np, ne - np);
    return AddressNameLine::Entry;
}

}  // namespace sfe
