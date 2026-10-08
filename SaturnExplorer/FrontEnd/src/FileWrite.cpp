#include "FileWrite.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <direct.h>
#include <io.h>        // _unlink
#include <windows.h>
#include <sys/stat.h>
#include <sys/types.h>
#define SE_MKDIR(p)  _mkdir(p)
#define SE_RMDIR(p)  _rmdir(p)
#define SE_UNLINK(p) _unlink(p)
#define SE_STAT      struct _stat
#define SE_STAT_FN   _stat
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define SE_MKDIR(p)  ::mkdir((p), 0755)
#define SE_RMDIR(p)  ::rmdir(p)
#define SE_UNLINK(p) ::unlink(p)
#define SE_STAT      struct stat
#define SE_STAT_FN   ::stat
#endif

namespace sfe
{

namespace
{

#ifdef _WIN32
const char kSep = '\\';
#else
const char kSep = '/';
#endif

std::string Reason(const char* what, const std::string& path)
{
    // errno is captured by the caller immediately after the failing call; strerror keeps the
    // message specific ("No space left on device") rather than a generic "could not write".
    const int err = errno;
    std::string out = std::string(what) + " " + path;
    if (err != 0) out += ": " + std::string(std::strerror(err));
    return out;
}

// True when 'path' exists and is NOT a regular file -- a device node, FIFO, socket or
// directory. Staging must not be used for those; see the header.
bool ExistsAsNonRegularFile(const std::string& path)
{
    SE_STAT st;
    if (SE_STAT_FN(path.c_str(), &st) != 0) return false;   // missing, or unstattable
    return !S_ISREG(st.st_mode);
}

// fopen/fwrite/fclose with every result checked. This is the whole point of the file: the
// close is where a buffered write to a full device finally fails.
bool WriteDirect(const std::string& path, const void* data, size_t size, std::string& error)
{
    errno = 0;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
    {
        error = Reason("could not open", path);
        return false;
    }
    errno = 0;
    const size_t wrote = (size == 0) ? 0 : std::fwrite(data, 1, size, f);
    if (wrote != size)
    {
        error = Reason("could not write", path);
        std::fclose(f);   // result ignored on purpose: the write already failed
        return false;
    }
    // THE line this file exists for. fclose flushes, and a small write has not reached the
    // device before this point, so this is where ENOSPC and EIO are reported.
    errno = 0;
    if (std::fclose(f) != 0)
    {
        error = Reason("could not finish writing", path);
        return false;
    }
    error.clear();
    return true;
}

}  // namespace

bool WriteFileAtomically(const std::string& path, const void* data, size_t size,
                         std::string& error)
{
    error.clear();
    if (path.empty())
    {
        error = "no destination path";
        return false;
    }
    // A device, FIFO or directory is written in place or not at all -- renaming over one
    // would replace the object instead of its contents.
    if (ExistsAsNonRegularFile(path))
    {
        return WriteDirect(path, data, size, error);
    }

    const std::string temp = path + ".separt";
    RemoveFile(temp);   // a temp left by a previous crash must not be mistaken for ours
    if (!WriteDirect(temp, data, size, error))
    {
        RemoveFile(temp);
        return false;
    }
    // std::rename replaces an existing regular file atomically on POSIX. On Windows it
    // fails if the destination exists, so the old file is removed first -- which opens a
    // window where neither exists, unavoidable without MoveFileEx, and still better than
    // truncating before the write.
#ifdef _WIN32
    RemoveFile(path);
#endif
    errno = 0;
    if (std::rename(temp.c_str(), path.c_str()) != 0)
    {
        error = Reason("could not publish", path);
        RemoveFile(temp);
        return false;
    }
    return true;
}

bool MakeDirectory(const std::string& path)
{
    if (path.empty()) return false;
    if (SE_MKDIR(path.c_str()) == 0) return true;
    return errno == EEXIST;
}

bool RemoveFile(const std::string& path)
{
    if (path.empty()) return false;
    errno = 0;
    // unlink, not std::remove: remove() falls back to rmdir, so it deletes an empty
    // DIRECTORY sitting at this path. The staging path is cleared before use, and a
    // directory that happens to share that name belongs to someone else.
    if (SE_UNLINK(path.c_str()) == 0) return true;
    return errno == ENOENT;
}

bool RemoveEmptyDirectory(const std::string& path)
{
    if (path.empty()) return false;
    errno = 0;
    if (SE_RMDIR(path.c_str()) == 0) return true;
    return errno == ENOENT;
}

bool MovePath(const std::string& from, const std::string& to)
{
    if (from.empty() || to.empty()) return false;
    return std::rename(from.c_str(), to.c_str()) == 0;
}

bool FileOrDirectoryExists(const std::string& path)
{
    if (path.empty()) return false;
    SE_STAT st;
    return SE_STAT_FN(path.c_str(), &st) == 0;
}

bool ListDirectory(const std::string& path, std::vector<std::string>& names)
{
    names.clear();
    if (path.empty()) return false;
#ifdef _WIN32
    WIN32_FIND_DATAA find;
    const HANDLE h = ::FindFirstFileA((path + kSep + "*").c_str(), &find);
    if (h == INVALID_HANDLE_VALUE) return false;
    do
    {
        const std::string name = find.cFileName;
        if (name != "." && name != "..") names.push_back(name);
    } while (::FindNextFileA(h, &find) != 0);
    ::FindClose(h);
    return true;
#else
    DIR* d = ::opendir(path.c_str());
    if (d == nullptr) return false;
    for (struct dirent* e = ::readdir(d); e != nullptr; e = ::readdir(d))
    {
        const std::string name = e->d_name;
        if (name != "." && name != "..") names.push_back(name);
    }
    ::closedir(d);
    return true;
#endif
}

bool RemoveFlatDirectory(const std::string& path)
{
    if (path.empty()) return false;
    if (!FileOrDirectoryExists(path)) return true;
    std::vector<std::string> names;
    if (!ListDirectory(path, names)) return false;
    // Checked before anything is deleted, so an unexpected entry leaves the directory whole
    // rather than half emptied.
    for (size_t i = 0; i < names.size(); ++i)
    {
        SE_STAT st;
        if (SE_STAT_FN((path + kSep + names[i]).c_str(), &st) != 0) return false;
        if (!S_ISREG(st.st_mode)) return false;
    }
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (!RemoveFile(path + kSep + names[i])) return false;
    }
    return RemoveEmptyDirectory(path);
}

char PathSeparator()
{
    return kSep;
}

}  // namespace sfe
