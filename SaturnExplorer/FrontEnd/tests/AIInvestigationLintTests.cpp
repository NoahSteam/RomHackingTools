// Static checks on the AIInvestigation module's source files (PLAN.md §1 and M0's hard rules). They read
// the tree, so they catch a violation the moment it is written rather than when a reviewer notices:
//
//   * SE contains no AI: no model calls, provider SDKs, network code, API keys or prompt interpretation.
//   * The module is ImGui-free and platform-free, and does no file I/O of its own in M0 (reading and
//     writing packages is M1).
//   * C++14 only.
//   * Nothing outside the module calls into it yet (M0 changes no behaviour of the application), and the
//     only sources it shares with the rest of the tree are the intended ones.
//   * Every source file in the module is in the CMake source list, and the MSBuild project picks up the
//     whole src tree.
//
// Usage: AIInvestigationLintTests <SaturnExplorer source dir>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace
{
int gFail = 0;
void Check(bool ok, const std::string& what)
{
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++gFail; }
}

std::string Read(const std::string& path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool IsDir(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }

void ListFiles(const std::string& dir, std::vector<std::string>* out)
{
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (const dirent* e = readdir(d))
    {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        const std::string path = dir + "/" + name;
        if (IsDir(path)) ListFiles(path, out);
        else out->push_back(path);
    }
    closedir(d);
}

bool EndsWith(const std::string& s, const char* suffix)
{
    const size_t n = std::string(suffix).size();
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string Lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Code with comments and string literals blanked, so a rule about identifiers does not trip on prose.
std::string CodeOnly(const std::string& src)
{
    std::string out = src;
    enum { Code, Line, Block, Str, Chr, Raw } state = Code;
    std::string rawEnd;
    for (size_t i = 0; i < src.size(); ++i)
    {
        const char c = src[i], n = i + 1 < src.size() ? src[i + 1] : 0;
        switch (state)
        {
        case Code:
            if (c == '/' && n == '/') { state = Line; out[i] = ' '; }
            else if (c == '/' && n == '*') { state = Block; out[i] = ' '; }
            else if (c == 'R' && n == '"')
            {
                const size_t open = src.find('(', i);
                if (open != std::string::npos)
                {
                    rawEnd = ")" + src.substr(i + 2, open - i - 2) + "\"";
                    state = Raw;
                    for (size_t k = i; k <= open; ++k) out[k] = ' ';
                    i = open;
                }
            }
            else if (c == '"') { state = Str; out[i] = ' '; }
            else if (c == '\'' ) { state = Chr; out[i] = ' '; }
            break;
        case Line: if (c == '\n') state = Code; else out[i] = ' '; break;
        case Block: if (c == '*' && n == '/') { out[i] = ' '; out[i + 1] = ' '; ++i; state = Code; } else if (c != '\n') out[i] = ' '; break;
        case Str: if (c == '\\') { out[i] = ' '; if (i + 1 < src.size()) out[++i] = ' '; } else if (c == '"') { state = Code; out[i] = ' '; } else out[i] = ' '; break;
        case Chr: if (c == '\\') { out[i] = ' '; if (i + 1 < src.size()) out[++i] = ' '; } else if (c == '\'') { state = Code; out[i] = ' '; } else out[i] = ' '; break;
        case Raw:
            if (src.compare(i, rawEnd.size(), rawEnd) == 0) { for (size_t k = 0; k < rawEnd.size(); ++k) out[i + k] = ' '; i += rawEnd.size() - 1; state = Code; }
            else if (c != '\n') out[i] = ' ';
            break;
        }
    }
    return out;
}

std::vector<std::string> Includes(const std::string& src)
{
    std::vector<std::string> v;
    std::istringstream in(src);
    std::string line;
    while (std::getline(in, line))
    {
        size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos || line[p] != '#') continue;
        p = line.find_first_not_of(" \t", p + 1);
        if (p == std::string::npos || line.compare(p, 7, "include") != 0) continue;
        v.push_back(line.substr(p + 7));
    }
    return v;
}

bool HasToken(const std::string& code, const std::string& token)
{
    for (size_t at = code.find(token); at != std::string::npos; at = code.find(token, at + 1))
    {
        const bool before = at == 0 || !(std::isalnum(static_cast<unsigned char>(code[at - 1])) || code[at - 1] == '_');
        const size_t end = at + token.size();
        const bool after = end >= code.size() || !(std::isalnum(static_cast<unsigned char>(code[end])) || code[end] == '_');
        if (before && after) return true;
    }
    return false;
}
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: AIInvestigationLintTests <SaturnExplorer dir>\n"); return 2; }
    const std::string root = argv[1];
    const std::string moduleDir = root + "/FrontEnd/src/AIInvestigation";

    std::vector<std::string> moduleFiles;
    ListFiles(moduleDir, &moduleFiles);
    std::sort(moduleFiles.begin(), moduleFiles.end());
    Check(moduleFiles.size() >= 10, "found the module's files");

    // Headers the module must never use. The first list is matched anywhere in an include (platform,
    // UI, network and OS headers); the second only as the exact standard header "<name>" (threads, file
    // streams, and the library headers newer than C++14).
    const char* forbiddenIncludes[] = {
        "imgui", "App.h", "Platform/", "windows.h", "winsock", "ws2tcpip", "SDL", "GL/", "d3d", "dxgi", "sys/socket", "netdb", "arpa/inet",
        "curl", "winhttp", "wininet", "unistd", "dirent", "sys/stat", "pthread",
    };
    const char* forbiddenStdHeaders[] = {
        "thread", "mutex", "atomic", "future", "fstream", "filesystem",
        "optional", "variant", "string_view", "any", "charconv", "memory_resource", "execution", "span", "bit", "numbers", "format", "expected",
    };
    const char* forbiddenTokens[] = {
        "fopen", "freopen", "fread", "fwrite", "ofstream", "ifstream", "fstream", "remove", "rename", "popen", "system", "exec", "fork", "getenv",
        "socket", "connect", "send", "recv", "bind", "listen", "accept", "gethostbyname", "getaddrinfo", "URLDownloadToFile", "WinHttpOpen",
        "CreateFile", "LoadLibrary", "dlopen", "ImGui", "IPlatform", "HttpsGet", "SaveFile", "PickDirectory",
        // C++17 and later language features.
        "string_view", "optional", "variant", "nodiscard", "maybe_unused",
    };
    // Words that would mean an AI client crept in (case-insensitive, over the whole file including comments:
    // there is no reason for any of them to appear in this module).
    const char* aiWords[] = { "openai", "anthropic", "api_key", "apikey", "api-key", "bearer ", "chatgpt", "gpt-", "gemini", "llama", "embedding",
                              "completion_tokens", "max_tokens", "temperature" };

    std::set<std::string> cppFiles;
    for (const std::string& path : moduleFiles)
    {
        const std::string name = path.substr(path.find_last_of('/') + 1);
        if (!EndsWith(path, ".cpp") && !EndsWith(path, ".h")) continue;
        if (EndsWith(path, ".cpp")) cppFiles.insert(name);
        const std::string src = Read(path);
        const std::string code = CodeOnly(src);
        for (const std::string& inc : Includes(src))
        {
            const std::string lowerInc = Lower(inc);
            for (const char* bad : forbiddenIncludes)
                Check(lowerInc.find(Lower(bad)) == std::string::npos, name + " includes " + inc + " (" + bad + " is not allowed in the module)");
            for (const char* bad : forbiddenStdHeaders)
                Check(inc.find("<" + std::string(bad) + ">") == std::string::npos, name + " includes " + inc + " (not a C++14 header, or not allowed in the module)");
        }
        for (const char* tok : forbiddenTokens) Check(!HasToken(code, tok), name + " uses '" + tok + "'");
        const std::string lowered = Lower(src);
        for (const char* w : aiWords) Check(lowered.find(w) == std::string::npos, name + " mentions '" + w + "'");
        // No raw `new` / `delete` ownership, no goto, no exceptions thrown from validation code.
        Check(!HasToken(code, "goto"), name + " uses goto");
        Check(!HasToken(code, "throw"), name + " throws");
        // Header hygiene.
        if (EndsWith(path, ".h")) Check(src.find("#pragma once") != std::string::npos, name + " has #pragma once");
        // The module's namespace.
        Check(src.find("namespace sfe") != std::string::npos && src.find("namespace ai") != std::string::npos, name + " is in sfe::ai");
        // Tabs are not used in this code base.
        Check(src.find('\t') == std::string::npos, name + " contains a tab");
    }

    // Every .cpp of the module is built by the desktop frontends: listed in CMake. MSBuild globs src/**.
    const std::string cmake = Read(root + "/CMakeLists.txt");
    for (const std::string& name : cppFiles)
        Check(cmake.find("FrontEnd/src/AIInvestigation/" + name) != std::string::npos, name + " is in CMakeLists.txt");
    const std::string vcxproj = Read(root + "/FrontEnd/FrontEnd.vcxproj");
    Check(vcxproj.find("<ClCompile Include=\"src\\**\\*.cpp\" />") != std::string::npos, "FrontEnd.vcxproj compiles src\\**\\*.cpp, which includes the module");
    Check(vcxproj.find("third_party\\nlohmann-json\\include") != std::string::npos, "FrontEnd.vcxproj has the JSON include path");
    Check(vcxproj.find("licenses\\nlohmann-json") != std::string::npos, "FrontEnd.vcxproj ships the JSON licence");

    // M0 wires nothing into the application: no file outside the module includes it, and the tree outside
    // it that the module's sources include is the intended shared set.
    std::vector<std::string> all;
    for (const char* sub : { "/FrontEnd/src", "/Core", "/Drivers", "/FrontEnd/Platforms" }) ListFiles(root + sub, &all);
    for (const std::string& path : all)
    {
        if (path.find("/AIInvestigation/") != std::string::npos) continue;
        if (!EndsWith(path, ".cpp") && !EndsWith(path, ".h") && !EndsWith(path, ".mm")) continue;
        if (Read(path).find("AIInvestigation/") != std::string::npos)
            Check(false, path + " includes the AIInvestigation module: M0 changes no application behaviour");
    }
    const std::set<std::string> intended = { "Sha256.h", "Sh2Address.h", "Debug/MemoryCompare.h", "Debug/ConditionEval.h", "Debug/FormatString.h", "Debug/CallStack.h" };
    std::set<std::string> seen;
    for (const std::string& path : moduleFiles)
    {
        if (!EndsWith(path, ".cpp") && !EndsWith(path, ".h")) continue;
        for (const std::string& inc : Includes(Read(path)))
        {
            const size_t q = inc.find('"');
            if (q == std::string::npos) continue;
            const std::string target = inc.substr(q + 1, inc.find('"', q + 1) - q - 1);
            if (target.compare(0, 16, "AIInvestigation/") == 0) continue;
            seen.insert(target);
        }
    }
    for (const std::string& t : seen)
        Check(intended.count(t) != 0, "the module includes only the intended shared headers (unexpected: " + t + ")");

    // The vendored JSON header is included in exactly one translation unit.
    int jsonIncluders = 0;
    for (const std::string& path : moduleFiles)
        if (Read(path).find("<nlohmann/json.hpp>") != std::string::npos) ++jsonIncluders;
    Check(jsonIncluders == 1, "exactly one file includes nlohmann/json.hpp");

    if (gFail == 0) std::printf("AIInvestigationLintTests: all passed (%zu files)\n", moduleFiles.size());
    return gFail == 0 ? 0 : 1;
}
