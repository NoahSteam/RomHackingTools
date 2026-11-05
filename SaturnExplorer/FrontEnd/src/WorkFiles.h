// WorkFiles -- where the user's working files live: saturn_functions.txt, assembly_comments.txt, saturn_function_bounds.txt.
//
// They were relative paths, so they followed the process's current directory, and on Windows the Explorer-style open dialogs
// change it (A9-23). The application now fixes the folder once at startup (SetWorkFolder, the current directory then) and every
// load and save goes through WorkFilePath. With no folder set (tests, tools) the name stays relative, as before.
#pragma once

#include <string>

namespace sfe
{

inline std::string& WorkFolderRef()
{
    static std::string folder;
    return folder;
}

inline void SetWorkFolder(const std::string& folder) { WorkFolderRef() = folder; }

inline std::string WorkFilePath(const std::string& name)
{
    const std::string& f = WorkFolderRef();
    if (f.empty()) return name;
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    const char last = f.back();
    return (last == '/' || last == '\\') ? f + name : f + sep + name;
}

}  // namespace sfe
