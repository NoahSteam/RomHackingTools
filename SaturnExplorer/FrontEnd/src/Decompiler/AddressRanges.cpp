// ParseAddressRanges: the volatile-memory list as the user types it (and as settings.ini keeps it). A file of its own so
// the parser can be tested, and fuzzed (LoaderFuzzTests), without the engine.
#include "Decompiler/Sh2Decompiler.h"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace sfe
{
namespace decomp
{

bool ParseAddressRanges(const std::string& input, std::vector<std::pair<uint32_t, uint32_t>>& out, std::string& error)
{
    out.clear();
    error.clear();
    // "a - b" is one range: spaces around a dash go before entries are split.
    std::string text;
    for (size_t k = 0; k < input.size(); ++k)
    {
        if (std::isspace(static_cast<unsigned char>(input[k])))
        {
            size_t n = k;
            while (n < input.size() && std::isspace(static_cast<unsigned char>(input[n]))) ++n;
            const bool nextDash = n < input.size() && input[n] == '-';
            const bool afterDash = !text.empty() && text.back() == '-';
            if (nextDash || afterDash) { k = n - 1; continue; }
        }
        text += input[k];
    }
    auto hex = [](const std::string& t, uint32_t& v) {
        std::string d = t;
        if (d.size() > 2 && d[0] == '0' && (d[1] == 'x' || d[1] == 'X')) d = d.substr(2);
        if (d.empty() || d.size() > 8) return false;
        v = 0;
        for (char c : d)
        {
            if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
            v = (v << 4) | static_cast<uint32_t>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (std::tolower(c) - 'a' + 10));
        }
        return true;
    };
    size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && (text[i] == ',' || std::isspace(static_cast<unsigned char>(text[i])))) ++i;
        size_t j = i;
        while (j < text.size() && text[j] != ',' && !std::isspace(static_cast<unsigned char>(text[j]))) ++j;
        if (j == i) break;
        const std::string entry = text.substr(i, j - i);
        const size_t dash = entry.find('-');
        uint32_t lo = 0, hi = 0;
        const bool ok = dash == std::string::npos ? hex(entry, lo) && hex(entry, hi)
                                                  : hex(entry.substr(0, dash), lo) && hex(entry.substr(dash + 1), hi);
        if (!ok || hi < lo)
        {
            error = "\"" + entry + "\" is not an address or a range first-last";
            out.clear();
            return false;
        }
        out.emplace_back(lo, hi);
        i = j;
    }
    return true;
}

}  // namespace decomp
}  // namespace sfe
