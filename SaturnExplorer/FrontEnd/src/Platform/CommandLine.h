// Turn an argument vector into the single command-line string Windows wants, so a path is passed to a
// program as one literal argument whatever it holds. Header-only and platform-free so it can be tested
// on any host.
#pragma once

#include <string>
#include <vector>

namespace sfe
{

// One argument, quoted so CommandLineToArgvW (and the C runtime's parser) give it back unchanged:
// an argument with no space, tab, quote or newline, and not empty, is left bare; otherwise it is put
// in double quotes, with backslashes doubled where they precede a quote or the closing quote and every
// embedded quote escaped.
inline std::string QuoteWindowsArg(const std::string& arg)
{
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
    std::string out = "\"";
    size_t backslashes = 0;
    for (const char c : arg)
    {
        if (c == '\\')
        {
            ++backslashes;
            continue;
        }
        if (c == '"') out.append(backslashes * 2 + 1, '\\');   // escape the run, then the quote
        else          out.append(backslashes, '\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, '\\');   // a trailing run must not escape the closing quote
    out += '"';
    return out;
}

inline std::string JoinWindowsCommandLine(const std::vector<std::string>& args)
{
    std::string out;
    for (const std::string& a : args)
    {
        if (!out.empty()) out += ' ';
        out += QuoteWindowsArg(a);
    }
    return out;
}

}  // namespace sfe
