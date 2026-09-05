// PathUtil — tiny path/string helpers shared across the Disc/ layer (DiscImage, IsoBuilder,
// DiscBuilder). Header-only and self-contained: the Disc/ code deliberately avoids depending on
// the app-layer PathBasename (Launcher.h), so these live here instead of being copy-pasted into
// each translation unit's anonymous namespace.
#pragma once

#include <string>

namespace sfe
{

// Directory prefix of a path, including the trailing separator ("a/b/c.bin" -> "a/b/"); "" if none.
inline std::string DirOf(const std::string& path)
{
    const size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? std::string() : path.substr(0, s + 1);
}

// Final path component ("a/b/c.bin" -> "c.bin").
inline std::string BaseName(const std::string& path)
{
    const size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}

// Base name without its final extension ("a/b/Game.cue" -> "Game").
inline std::string Stem(const std::string& path)
{
    std::string b = BaseName(path);
    const size_t d = b.find_last_of('.');
    if (d != std::string::npos) b.resize(d);
    return b;
}

// Case-insensitive (ASCII): does 'name' end with 'suffix'? Used for extension matches.
inline bool IEqualsExt(const std::string& name, const std::string& suffix)
{
    if (name.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i)
    {
        char a = name[name.size() - suffix.size() + i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

// Case-insensitive (ASCII) full-string equality.
inline bool IEquals(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && IEqualsExt(a, b);
}

}  // namespace sfe
