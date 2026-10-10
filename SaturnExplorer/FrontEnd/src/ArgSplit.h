// ArgSplit -- split a command-line TEMPLATE into arguments, and fill {tokens} into them as literal text.
//
// Shared by the diff tool's argument template and the emulator launch template. The order is the point:
// the template is split FIRST and the values (a ROM path, a folder) are substituted AFTER, so a value
// holding a quote, a `$`, a backtick or spaces is still exactly one argument and is never parsed. Nothing
// here goes through a shell. Header-only and platform-free.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace sfe
{

// Split on whitespace outside quotes; "..." and '...' group and are removed; nothing is escaped (a
// backslash is just a character, so a Windows path survives). A quote left open runs to the end. An
// empty quoted pair is an empty argument.
inline std::vector<std::string> SplitCommandLine(const std::string& t)
{
    std::vector<std::string> out;
    std::string cur;
    bool have = false;   // 'cur' is an argument even if empty (it held a quote pair)
    char quote = 0;
    for (const char c : t)
    {
        if (quote)
        {
            if (c == quote) quote = 0; else cur += c;
        }
        else if (c == '"' || c == '\'')
        {
            quote = c;
            have = true;
        }
        else if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            if (have) { out.push_back(cur); cur.clear(); have = false; }
        }
        else
        {
            cur += c;
            have = true;
        }
    }
    if (have) out.push_back(cur);
    return out;
}

// Replace each token ("{rom}") in 'arg' with its value, in ONE pass over 'arg': a value that happens to
// contain another token's spelling is not expanded again. 'touched' (optional) is set when any token
// matched.
inline std::string SubstituteTokens(const std::string& arg,
                                    const std::vector<std::pair<std::string, std::string>>& tokens,
                                    bool* touched = nullptr)
{
    std::string result;
    if (touched) *touched = false;
    for (size_t i = 0; i < arg.size();)
    {
        bool matched = false;
        for (const auto& t : tokens)
        {
            if (!t.first.empty() && arg.compare(i, t.first.size(), t.first) == 0)
            {
                result += t.second;
                i += t.first.size();
                matched = true;
                if (touched) *touched = true;
                break;
            }
        }
        if (!matched) result += arg[i++];
    }
    return result;
}

}  // namespace sfe
