#include "FileWrite.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>        // _unlink, _open
#include <process.h>   // _getpid
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
#include <fcntl.h>
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

// fwrite/fclose with every result checked. This is the whole point of the file: the close is
// where a buffered write to a full device finally fails. Takes ownership of 'f'.
bool WriteAndClose(FILE* f, const std::string& path, const void* data, size_t size,
                   std::string& error)
{
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

bool WriteDirect(const std::string& path, const void* data, size_t size, std::string& error)
{
    errno = 0;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
    {
        error = Reason("could not open", path);
        return false;
    }
    return WriteAndClose(f, path, data, size, error);
}

// Create a staging file beside 'path' that did not exist before this call, and open it.
//
// A fixed name ("<path>.separt") cannot be told apart from somebody else's file of that name,
// so clearing it before use deleted whatever was there. O_EXCL makes the file ours by
// construction: only a name this call created is ever written, and so only it is ever removed.
FILE* OpenStagingFile(const std::string& path, std::string& temp, std::string& error)
{
    static std::atomic<unsigned> counter(0);
#ifdef _WIN32
    const unsigned long pid = (unsigned long)_getpid();
#else
    const unsigned long pid = (unsigned long)::getpid();
#endif
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        temp = path + ".separt-" + std::to_string(pid) + "-" + std::to_string(++counter);
        errno = 0;
#ifdef _WIN32
        const int fd = ::_open(temp.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                               _S_IREAD | _S_IWRITE);
#else
        const int fd = ::open(temp.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666);
#endif
        if (fd < 0)
        {
            if (errno == EEXIST) continue;
            error = Reason("could not open", temp);
            temp.clear();
            return nullptr;
        }
#ifdef _WIN32
        FILE* f = ::_fdopen(fd, "wb");
#else
        FILE* f = ::fdopen(fd, "wb");
#endif
        if (f == nullptr)
        {
            error = Reason("could not open", temp);
#ifdef _WIN32
            ::_close(fd);
#else
            ::close(fd);
#endif
            RemoveFile(temp);
            temp.clear();
        }
        return f;
    }
    errno = EEXIST;
    error = Reason("could not create a staging file for", path);
    temp.clear();
    return nullptr;
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

    std::string temp;
    FILE* f = OpenStagingFile(path, temp, error);
    if (f == nullptr) return false;
    if (!WriteAndClose(f, temp, data, size, error))
    {
        RemoveFile(temp);
        return false;
    }
    if (!PublishFile(temp, path, error))
    {
        RemoveFile(temp);
        return false;
    }
    return true;
}

bool PublishFile(const std::string& from, const std::string& path, std::string& error)
{
    // Publish by replacing, never by deleting first. std::rename does that on POSIX. On
    // Windows std::rename refuses an existing destination, and the obvious workaround --
    // remove it, then rename -- is what this function exists to avoid: if the rename then
    // failed, or the process died in between, the old file would be gone and the new one
    // never published. MoveFileEx with MOVEFILE_REPLACE_EXISTING replaces in one step, so
    // the destination always holds one complete file or the other.
    //
    // MOVEFILE_WRITE_THROUGH asks for the change to reach the disk before it returns, so a
    // power loss cannot leave the directory entry pointing at a file whose contents never
    // landed.
#ifdef _WIN32
    if (::MoveFileExA(from.c_str(), path.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
    {
        // Nothing has been removed, so the previous contents of 'path' are still there.
        error = "could not publish " + path;
        return false;
    }
#else
    errno = 0;
    if (std::rename(from.c_str(), path.c_str()) != 0)
    {
        // Same guarantee: a failed rename leaves the destination untouched.
        error = Reason("could not publish", path);
        return false;
    }
#endif
    error.clear();
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

// Staging must not be used for these; see WriteFileAtomically in the header.
bool ExistsAsNonRegularFile(const std::string& path)
{
    SE_STAT st;
    if (SE_STAT_FN(path.c_str(), &st) != 0) return false;   // missing, or unstattable
    return !S_ISREG(st.st_mode);
}

std::string CreateStagingFile(const std::string& path, std::string& error)
{
    std::string temp;
    FILE* f = OpenStagingFile(path, temp, error);
    if (f == nullptr) return std::string();
    errno = 0;
    if (std::fclose(f) != 0)
    {
        error = Reason("could not create", temp);
        RemoveFile(temp);
        return std::string();
    }
    return temp;
}

std::string CanonicalPath(const std::string& path)
{
    if (path.empty()) return std::string();
#ifdef _WIN32
    // Resolve the final path of the object itself, so a junction or symlink is followed the way
    // the OS would follow it when the file is opened.
    const HANDLE h = ::CreateFileA(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE |
                                   FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    char buf[4096];
    const DWORD n = ::GetFinalPathNameByHandleA(h, buf, sizeof buf, 0);
    ::CloseHandle(h);
    if (n == 0 || n >= sizeof buf) return std::string();
    std::string out(buf, n);
    if (out.compare(0, 8, "\\\\?\\UNC\\") == 0) out = "\\\\" + out.substr(8);
    else if (out.compare(0, 4, "\\\\?\\") == 0) out = out.substr(4);
    return out;
#else
    char* r = ::realpath(path.c_str(), nullptr);
    if (r == nullptr) return std::string();
    std::string out(r);
    std::free(r);
    return out;
#endif
}

bool SameFile(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty()) return false;
#ifdef _WIN32
    auto ident = [](const std::string& p, BY_HANDLE_FILE_INFORMATION& info) {
        const HANDLE h = ::CreateFileA(p.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE |
                                       FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        const BOOL ok = ::GetFileInformationByHandle(h, &info);
        ::CloseHandle(h);
        return ok != 0;
    };
    BY_HANDLE_FILE_INFORMATION ia, ib;
    if (!ident(a, ia) || !ident(b, ib)) return false;
    return ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
           ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
#else
    struct stat sa, sb;
    if (::stat(a.c_str(), &sa) != 0 || ::stat(b.c_str(), &sb) != 0) return false;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
#endif
}

bool PathIsWithin(const std::string& inner, const std::string& outer)
{
    std::string i = CanonicalPath(inner), o = CanonicalPath(outer);
    if (i.empty() || o.empty()) return false;
#ifdef _WIN32
    for (char& c : i) { if (c == '/') c = '\\'; if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); }
    for (char& c : o) { if (c == '/') c = '\\'; if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); }
#endif
    while (o.size() > 1 && o.back() == kSep) o.pop_back();
    if (i == o) return true;
    if (o.back() == kSep) return i.compare(0, o.size(), o) == 0;   // the filesystem root
    return i.size() > o.size() && i.compare(0, o.size(), o) == 0 && i[o.size()] == kSep;
}

}  // namespace sfe
