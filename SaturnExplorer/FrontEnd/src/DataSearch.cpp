// DataSearch — see DataSearch.h. Directory walking uses Win32 FindFirstFile on
// Windows and POSIX opendir/stat elsewhere; file scanning reads in overlapping
// chunks so a match spanning a chunk boundary is still found, and large ISOs never
// have to be held in memory at once.

#include "DataSearch.h"

#include <algorithm>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace sfe
{

bool IsDirectory(const std::string& path)
{
#ifdef _WIN32
    const DWORD a = ::GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool PathExists(const std::string& path)
{
#ifdef _WIN32
    return ::GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
#endif
}

namespace
{

void EnumerateFiles(const std::string& dir, std::vector<std::string>& out)
{
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    const std::string pattern = dir + "\\*";
    HANDLE h = ::FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        return;
    }
    do
    {
        const std::string name = fd.cFileName;
        if (name == "." || name == "..")
        {
            continue;
        }
        const std::string full = dir + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            EnumerateFiles(full, out);
        }
        else
        {
            out.push_back(full);
        }
    } while (::FindNextFileA(h, &fd));
    ::FindClose(h);
#else
    DIR* d = ::opendir(dir.c_str());
    if (!d)
    {
        return;
    }
    while (struct dirent* e = ::readdir(d))
    {
        const std::string name = e->d_name;
        if (name == "." || name == "..")
        {
            continue;
        }
        const std::string full = dir + "/" + name;
        struct stat st;
        if (::stat(full.c_str(), &st) != 0)
        {
            continue;
        }
        if (S_ISDIR(st.st_mode))
        {
            EnumerateFiles(full, out);
        }
        else if (S_ISREG(st.st_mode))
        {
            out.push_back(full);
        }
    }
    ::closedir(d);
#endif
}

// Scan one file for the needle, appending match offsets. Overlapping chunk reads
// (carry the trailing len-1 bytes) so a match straddling a chunk boundary is found.
void SearchFile(const std::string& path, const uint8_t* needle, size_t len,
                std::vector<uint64_t>& offsets, size_t maxHits)
{
    if (len == 0)
    {
        return;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        return;
    }

    const size_t kChunk = 8u << 20;   // 8 MiB
    std::vector<uint8_t> buf(kChunk);
    std::vector<uint8_t> comb;        // tail-carry + this chunk
    std::vector<uint8_t> tail;        // trailing (len-1) bytes of the previous chunk
    uint64_t filePos = 0;             // file offset of the first byte of `buf`

    while (f)
    {
        f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(kChunk));
        const std::streamsize got = f.gcount();
        if (got <= 0)
        {
            break;
        }
        comb.clear();
        comb.reserve(tail.size() + static_cast<size_t>(got));
        comb.insert(comb.end(), tail.begin(), tail.end());
        comb.insert(comb.end(), buf.begin(), buf.begin() + got);

        // comb index i maps to file offset (filePos - tail.size()) + i.
        const uint64_t combBase = filePos - tail.size();
        size_t from = 0;
        while (from + len <= comb.size())
        {
            auto it = std::search(comb.begin() + from, comb.end(), needle, needle + len);
            if (it == comb.end())
            {
                break;
            }
            const size_t pos = static_cast<size_t>(it - comb.begin());
            offsets.push_back(combBase + pos);
            if (maxHits != 0 && offsets.size() >= maxHits)
            {
                return;
            }
            from = pos + 1;
        }

        const size_t keep = std::min(len - 1, comb.size());
        tail.assign(comb.end() - keep, comb.end());
        filePos += static_cast<uint64_t>(got);
    }
}

}  // namespace

size_t SearchDataDir(const std::string& root, const uint8_t* needle, size_t len,
                     std::vector<DataSearchHit>& hits, size_t maxHitsPerFile)
{
    std::vector<std::string> files;
    if (IsDirectory(root))
    {
        EnumerateFiles(root, files);
    }
    else if (PathExists(root))
    {
        files.push_back(root);   // a single image (ISO/disc), scanned as one file
    }

    for (const std::string& file : files)
    {
        std::vector<uint64_t> offsets;
        SearchFile(file, needle, len, offsets, maxHitsPerFile);
        if (!offsets.empty())
        {
            DataSearchHit hit;
            hit.path = file;
            hit.offsets = std::move(offsets);
            hits.push_back(std::move(hit));
        }
    }
    return files.size();
}

}  // namespace sfe
