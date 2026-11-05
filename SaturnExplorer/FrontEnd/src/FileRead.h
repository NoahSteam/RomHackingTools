// FileRead -- read a whole file the way a loader of the application's own files should: only a regular file, never more
// than the ceiling its caller names, and a verdict that says which of those it was.
//
// ReadWholeFile (Disc/PathUtil.h) reads until the file ends. That is right for a file whose size is trusted and wrong for
// the files a user points the application at or that sit in a folder anything can write to: a mistyped path at a disc
// image held the whole image in memory before the size was looked at, a FIFO blocked the opening thread for ever, and
// /dev/zero never ended (A9-1, A9-5, A9-11). The file's size is checked first, and the read itself stops one byte past
// the ceiling, so a file that grows after the check is still bounded.
//
// Header only and free of the application's other modules, so every target that reads such a file can use it.
#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <string>

#include <sys/stat.h>
#include <sys/types.h>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sfe
{

enum class CappedRead
{
    Ok,
    Missing,       // nothing at the path (a link that points nowhere counts)
    Unreadable,    // it exists and could not be read
    NotRegular,    // a directory, a device, a FIFO, a socket
    TooLarge,      // more than 'maxBytes'
};

inline CappedRead ReadFileCapped(const std::string& path, size_t maxBytes, std::string& out)
{
    out.clear();
#ifdef _WIN32
    struct __stat64 st;
    if (_stat64(path.c_str(), &st) != 0) return errno == ENOENT ? CappedRead::Missing : CappedRead::Unreadable;
    if ((st.st_mode & _S_IFMT) != _S_IFREG) return CappedRead::NotRegular;
#else
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return errno == ENOENT || errno == ENOTDIR || errno == ELOOP ? CappedRead::Missing : CappedRead::Unreadable;
    if (!S_ISREG(st.st_mode)) return CappedRead::NotRegular;
#endif
    if (static_cast<unsigned long long>(st.st_size) > maxBytes) return CappedRead::TooLarge;
#ifdef _WIN32
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return CappedRead::Unreadable;
#else
    // Opened without waiting (O_NONBLOCK) and looked at again through the descriptor: a link retargeted from a file to a FIFO
    // between the stat above and this open would otherwise block the open for ever (A9-24).
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return CappedRead::Unreadable;
    struct stat st2;
    if (::fstat(fd, &st2) != 0) { ::close(fd); return CappedRead::Unreadable; }
    if (!S_ISREG(st2.st_mode)) { ::close(fd); return CappedRead::NotRegular; }
    if (static_cast<unsigned long long>(st2.st_size) > maxBytes) { ::close(fd); return CappedRead::TooLarge; }
    std::FILE* f = ::fdopen(fd, "rb");
    if (!f) { ::close(fd); return CappedRead::Unreadable; }
#endif
    CappedRead result = CappedRead::Ok;
    try
    {
        char buf[64 * 1024];
        for (;;)
        {
            const size_t want = sizeof buf;
            const size_t got = std::fread(buf, 1, want, f);
            if (got > maxBytes - out.size() && out.size() + got > maxBytes)
            {
                result = CappedRead::TooLarge;
                break;
            }
            out.append(buf, got);
            if (got < want)
            {
                if (std::ferror(f)) result = CappedRead::Unreadable;
                break;
            }
        }
    }
    catch (...)
    {
        std::fclose(f);
        throw;
    }
    std::fclose(f);
    if (result != CappedRead::Ok) out.clear();
    return result;
}

}  // namespace sfe
