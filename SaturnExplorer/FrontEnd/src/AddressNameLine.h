// AddressNameLine -- one line of a "<hex-address> <name>" text file: the function names (saturn_functions.txt), a symbol map
// brought in with Load Symbols, and the assembly notes (assembly_comments.txt, "<hex-address> <note>").
//
// The address is one to eight hex digits, optionally 0x-prefixed, all of the line's first word; then white space; then the
// name. Blank lines and lines opening with ; # // are comments. Any other line is Malformed and the caller skips it and
// counts it. The readers used strtoul / sscanf on the front of the line, so "100000000 f" named address 0 (the high bits
// cut off), "-1 f" named 0xFFFFFFFF, and "0608<garbage>" named 0x608 after the digits it began with (A9-2, A9-10).
//
// A word of the letters a-f alone is a word, not an address (A9-18, A9-41): "Dead function", "add it later", "beef", an article "a".
// The rule (ParseAddressWord): with 0x, one to eight hex digits; without it, eight hex digits (what the saves write), or one to
// seven hex digits that include a decimal digit (what older versions wrote for addresses below 0x10000000: "6004000"). So an
// older file's entry still loads unless every digit of its address is a-f ("dead" for 0x0000DEAD, a BIOS function an older version
// wrote unpadded); such a line is skipped and counted like any other that is not an entry, and the Log says it may be an old
// unpadded address and should be written as eight digits ("0000DEAD", A9-32). It stays in the file. Reading such words as addresses
// when they lie in the BIOS ROM was tried (A9-32) and made prose lines ("add it later", "be careful") entries again (A9-41).
//
// A9-45: a remark that opens with a number ("2026 trace", "12 Nov", "2 more below", "3D engine") is no entry either. A word of fewer than
// eight digits and no 0x that lies below 0x80000 (the BIOS ROM: a year, a day, a count) is skipped with the same warning ("write it as
// eight digits"); an unpadded address of an older version is always above that. In the function bounds file (strict), which the
// application has always written with eight digits ("%08X"), only eight digits or 0x are taken. A remark that opens with an
// eight-digit date ("20261104 ...") cannot be told from an address and is still one; the bounds file takes remarks after "; ".
#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace sfe
{

enum class AddressNameLine { Entry, Comment, Malformed };

// The lines of 'text' without their ends (and without a byte-order mark at its start). A line ends at LF, CRLF or a lone CR (a classic Mac tool, or a CRLF file that lost its
// LFs, A9-25); a last line without an end is a line, an end at the end of the text does not start an empty one.
inline std::vector<std::string> SplitLines(const std::string& text)
{
    std::vector<std::string> out;
    size_t b = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;   // a UTF-8 byte-order mark (Windows editors) is not part of the first line (A9-34)
    for (size_t i = b; i < text.size(); ++i)
    {
        if (text[i] != '\n' && text[i] != '\r') continue;
        out.push_back(text.substr(b, i - b));
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
        b = i + 1;
    }
    if (b < text.size()) out.push_back(text.substr(b));
    return out;
}

// The text of a name or a note as the files hold it (A9-31, A9-36): a line end becomes a space (it would start a second line, which the
// next start reads as an entry of its own), and white space at either end goes (the loader trims it, so a session would show a
// text the file cannot hold). All white space gives the empty string, which the stores treat as no entry.
inline std::string CleanEntryText(const std::string& text)
{
    std::string s = text;
    for (char& c : s) if (c == '\n' || c == '\r') c = ' ';
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// The word line[b, e) as an address, by the rule at the top of this file.
inline bool ParseAddressWord(const std::string& line, size_t b, size_t e, uint32_t& addr, bool strict = false)
{
    size_t d = b;
    const bool prefixed = e - b > 2 && line[b] == '0' && (line[b + 1] == 'x' || line[b + 1] == 'X');
    if (prefixed) d = b + 2;
    if (d == e || e - d > 8) return false;
    uint32_t a = 0;
    bool digit = false;
    for (size_t i = d; i < e; ++i)
    {
        const char c = line[i];
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        digit = digit || std::isdigit(static_cast<unsigned char>(c)) != 0;
        a = (a << 4) | static_cast<uint32_t>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                                                                                         : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
    }
    if (!prefixed && e - d < 8 && !digit) return false;   // "Dead", "add", "a": a word (A9-18, A9-41)
    if (!prefixed && e - d < 8 && (strict || a < 0x80000u)) return false;   // "2026", "12", "3D": a number of a remark (A9-45)
    addr = a;
    return true;
}

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
    uint32_t a = 0;
    if (!ParseAddressWord(line, b, e, a)) return AddressNameLine::Malformed;
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
