// Name -> address lookup the SH-2 assembler uses for branch targets and absolute PC-relative
// operands. A free function over the plain name map so it is testable without App or ImGui; App
// binds it to FunctionNames::All().
#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

namespace sfe
{

// Resolve 'name' against the user's function names, then the default "sub_XXXXXX" spelling that
// FunctionNames::NameOf prints for a function with no name.
//   * A name that maps to more than one address is refused with the addresses listed: the
//     assembler must never guess which function the user meant.
//   * "sub_XXXXXX" carries only the low 24 bits of the address (NameOf masks them), so it names
//     high work RAM (06000000..060FFFFF, printed 000000..0FFFFF) or low work RAM (00200000..002FFFFF,
//     printed 200000..2FFFFF); any other value is ambiguous between mirrors and is refused. Eight
//     digits ("sub_0600ABCD") are taken as the full address.
//   * A rename replaces the default spelling in the UI, but the default still resolves: it is the
//     spelling already in the user's notes and in copied disassembly.
inline bool ResolveFunctionSymbol(const std::map<uint32_t, std::string>& names, const std::string& name,
                                  uint32_t& addr, std::string& err)
{
    auto hex8 = [](uint32_t a) { char b[16]; std::snprintf(b, sizeof b, "%08X", a); return std::string(b); };

    uint32_t first = 0;
    size_t   count = 0;
    std::string list;
    for (const auto& kv : names)
        if (kv.second == name)
        {
            if (count == 0) first = kv.first;
            list += (count ? ", " : "") + hex8(kv.first);
            ++count;
        }
    if (count == 1) { addr = first; return true; }
    if (count > 1)
    {
        err = "'" + name + "' names " + std::to_string(count) + " functions (" + list +
              "); use the address instead";
        return false;
    }

    // The default spelling, case-insensitive like the rest of the assembler's tokens.
    if (name.size() > 4 && (name[0] == 's' || name[0] == 'S') && (name[1] == 'u' || name[1] == 'U') &&
        (name[2] == 'b' || name[2] == 'B') && name[3] == '_')
    {
        const size_t digits = name.size() - 4;
        uint32_t v = 0;
        bool hex = digits == 6 || digits == 8;
        for (size_t i = 4; hex && i < name.size(); ++i)
        {
            const char c = name[i];
            const int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                        : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) hex = false; else v = (v << 4) | (uint32_t)d;
        }
        if (hex)
        {
            if (digits == 8) { addr = v; return true; }
            if (v < 0x100000u)                    { addr = 0x06000000u | v; return true; }
            if (v >= 0x200000u && v < 0x300000u)  { addr = v; return true; }
            err = "'" + name + "' does not say which memory it is in (the name keeps only 24 bits of the address); use the full address";
            return false;
        }
    }
    err = "unknown symbol '" + name + "' (not a function name or sub_XXXXXX)";
    return false;
}

}  // namespace sfe
