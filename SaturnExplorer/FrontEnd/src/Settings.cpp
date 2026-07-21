#include "Settings.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <direct.h>
#define SE_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define SE_MKDIR(p) ::mkdir((p), 0755)
#endif

namespace sfe
{

namespace
{

std::string Lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Create `dir` and any missing parents. Accepts '/' or '\\' separators.
bool CreateDirs(const std::string& dir)
{
    std::string p;
    for (size_t i = 0; i <= dir.size(); ++i)
    {
        const char c = (i < dir.size()) ? dir[i] : '\0';
        if (c == '/' || c == '\\' || c == '\0')
        {
            // Don't try to mkdir a drive root ("C:") or an empty leading segment.
            if (p.size() > 1 && !(p.size() == 2 && p[1] == ':'))
            {
                SE_MKDIR(p.c_str());   // ignore EEXIST; a real failure surfaces on write
            }
            if (c != '\0') p.push_back(c);
        }
        else
        {
            p.push_back(c);
        }
    }
    return true;
}

}  // namespace

std::string Settings::ConfigDir()
{
#ifdef _WIN32
    const char* base = std::getenv("APPDATA");
    const char sep = '\\';
#else
    const char* base = std::getenv("XDG_CONFIG_HOME");
    std::string fallback;
    if (!base || !*base)
    {
        const char* home = std::getenv("HOME");
        if (!home || !*home) return std::string();
        fallback = std::string(home) + "/.config";
        base = fallback.c_str();
    }
    const char sep = '/';
#endif
    if (!base || !*base) return std::string();
    return std::string(base) + sep + "SaturnExplorer";
}

std::string Settings::EnsureConfigDir()
{
    std::string dir = ConfigDir();
    if (!dir.empty()) CreateDirs(dir);
    return dir;
}

std::string Settings::FilePath()
{
    std::string dir = ConfigDir();
    if (dir.empty()) return std::string();
#ifdef _WIN32
    return dir + "\\settings.ini";
#else
    return dir + "/settings.ini";
#endif
}

void Settings::Load()
{
    const std::string path = FilePath();
    if (path.empty()) return;
    std::ifstream f(path);
    if (!f) return;

    std::string line, section;
    while (std::getline(f, line))
    {
        // Strip a trailing CR (an INI written on Windows read on POSIX, or vice versa).
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = Trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']')
        {
            section = Lower(Trim(t.substr(1, t.size() - 2)));
            continue;
        }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Lower(Trim(t.substr(0, eq)));
        const std::string val = Trim(t.substr(eq + 1));
        if (!key.empty()) mData[section][key] = val;
    }
}

bool Settings::Save() const
{
    const std::string dir = EnsureConfigDir();
    const std::string path = FilePath();
    if (path.empty()) return false;
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    for (const auto& sec : mData)
    {
        f << '[' << sec.first << "]\n";
        for (const auto& kv : sec.second)
        {
            f << kv.first << " = " << kv.second << '\n';
        }
        f << '\n';
    }
    return static_cast<bool>(f);
}

bool Settings::Has(const std::string& section, const std::string& key) const
{
    auto s = mData.find(Lower(section));
    if (s == mData.end()) return false;
    return s->second.find(Lower(key)) != s->second.end();
}

std::string Settings::Get(const std::string& section, const std::string& key,
                          const std::string& def) const
{
    auto s = mData.find(Lower(section));
    if (s == mData.end()) return def;
    auto k = s->second.find(Lower(key));
    return (k == s->second.end()) ? def : k->second;
}

void Settings::Set(const std::string& section, const std::string& key, const std::string& value)
{
    mData[Lower(section)][Lower(key)] = value;
}

bool Settings::GetBool(const std::string& section, const std::string& key, bool def) const
{
    if (!Has(section, key)) return def;
    const std::string v = Lower(Get(section, key));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

void Settings::SetBool(const std::string& section, const std::string& key, bool value)
{
    Set(section, key, value ? "1" : "0");
}

}  // namespace sfe
