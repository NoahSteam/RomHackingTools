// DataSearch — see DataSearch.h. Directory walking uses Win32 FindFirstFile on
// Windows and POSIX opendir/stat elsewhere; file scanning reads in overlapping
// chunks so a match spanning a chunk boundary is still found, and large ISOs never
// have to be held in memory at once.

#include "DataSearch.h"

#include <algorithm>
#include <fstream>

#include "Prs.h"

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

// Read an entire file into `data`. Returns false on open failure. Used by the PRS scan,
// which needs random access to try decompression at every offset.
bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& data)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
    {
        return false;
    }
    const std::streamoff size = f.tellg();
    if (size <= 0)
    {
        data.clear();
        return true;
    }
    data.resize(static_cast<size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(data.data()), size);
    data.resize(static_cast<size_t>(f.gcount()));
    return true;
}

// Scan one file for the needle appearing inside a PRS-compressed block: try to decompress
// starting at every offset and check whether the needle lands in the decompressed output.
// Records the offset of each compressed block that yields a match. Mirrors the approach of
// SakuraTaisen's FindCompressedData, but bounds-checked and cancellable.
void SearchFilePrs(const std::string& path, const uint8_t* needle, size_t len,
                   std::vector<uint64_t>& offsets, size_t maxHits, SearchProgress* progress)
{
    if (len == 0)
    {
        return;
    }
    std::vector<uint8_t> data;
    if (!ReadWholeFile(path, data) || data.size() < 3)
    {
        return;
    }
    if (progress)
    {
        progress->curFileSize.store(data.size(), std::memory_order_relaxed);
    }

    PRSDecompressor dec;   // reused across offsets; each Uncompress frees the previous buffer
    const size_t last = data.size() >= 3 ? data.size() - 3 : 0;
    for (size_t start = 0; start <= last; ++start)
    {
        // Poll for cancellation and publish progress only occasionally (atomics aren't free).
        if ((start & 0x3FFF) == 0)
        {
            if (progress)
            {
                if (progress->cancel.load(std::memory_order_relaxed))
                {
                    return;
                }
                progress->curOffset.store(start, std::memory_order_relaxed);
            }
        }

        if (!dec.UncompressData(data.data() + start,
                                static_cast<unsigned int>(data.size() - start)))
        {
            continue;
        }
        if (dec.mUncompressedDataSize < len)
        {
            continue;
        }
        const uint8_t* out = reinterpret_cast<const uint8_t*>(dec.mpUncompressedData);
        if (std::search(out, out + dec.mUncompressedDataSize, needle, needle + len) !=
            out + dec.mUncompressedDataSize)
        {
            offsets.push_back(static_cast<uint64_t>(start));
            if (maxHits != 0 && offsets.size() >= maxHits)
            {
                return;
            }
        }
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

namespace
{
uint64_t FileSizeBytes(const std::string& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
    {
        return 0;
    }
    const std::streamoff s = f.tellg();
    return s > 0 ? static_cast<uint64_t>(s) : 0;
}
}  // namespace

size_t SearchData(const std::vector<std::string>& roots, const uint8_t* needle, size_t len,
                  SearchCompression comp, std::vector<DataSearchHit>& hits,
                  size_t maxHitsPerFile, SearchProgress* progress)
{
    // Expand every root (file or directory) into a flat, de-duplicated file list.
    std::vector<std::string> files;
    for (const std::string& root : roots)
    {
        if (IsDirectory(root))
        {
            EnumerateFiles(root, files);
        }
        else if (PathExists(root))
        {
            files.push_back(root);
        }
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());

    if (progress)
    {
        progress->filesTotal.store(files.size(), std::memory_order_relaxed);
    }

    for (const std::string& file : files)
    {
        if (progress && progress->cancel.load(std::memory_order_relaxed))
        {
            break;
        }

        std::vector<uint64_t> offsets;
        if (comp == SearchCompression::Prs)
        {
            if (FileSizeBytes(file) > kPrsMaxFileBytes)
            {
                if (progress)
                {
                    progress->filesSkipped.fetch_add(1, std::memory_order_relaxed);
                    progress->filesScanned.fetch_add(1, std::memory_order_relaxed);
                }
                continue;   // too large to decompress-scan byte by byte
            }
            SearchFilePrs(file, needle, len, offsets, maxHitsPerFile, progress);
        }
        else
        {
            SearchFile(file, needle, len, offsets, maxHitsPerFile);
        }

        if (!offsets.empty())
        {
            DataSearchHit hit;
            hit.path = file;
            hit.offsets = std::move(offsets);
            hits.push_back(std::move(hit));
        }
        if (progress)
        {
            progress->filesScanned.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return files.size();
}

}  // namespace sfe
