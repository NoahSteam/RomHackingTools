// DataSearch — scan a game's data directory (a folder of extracted files, or a
// single image such as an ISO) for an exact byte sequence, e.g. the raw packed
// VRAM bytes of a selected texture. Desktop only: it reads the local filesystem
// directly (POSIX / Win32). On the web build there is no host filesystem, so a
// search simply finds nothing.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sfe
{

struct DataSearchHit
{
    std::string           path;     // absolute path of the file the needle was found in
    std::vector<uint64_t> offsets;  // byte offset of each match within that file. For a
                                     // PRS search these are the offsets of the *compressed*
                                     // block whose decompressed output contains the needle.
};

// How the needle is expected to appear in the data.
enum class SearchCompression
{
    None,   // the raw bytes appear verbatim (exact match)
    Prs     // the bytes appear inside a PRS-compressed block (decompress + match)
};

// Live progress + cancellation for a (potentially slow) search, shared with the worker
// thread. All fields are atomic; set `cancel` from any thread to stop early.
struct SearchProgress
{
    std::atomic<bool>     cancel{false};
    std::atomic<size_t>   filesTotal{0};
    std::atomic<size_t>   filesScanned{0};
    std::atomic<size_t>   filesSkipped{0};   // too large for a PRS scan (see kPrsMaxFileBytes)
    std::atomic<uint64_t> curOffset{0};      // scan position within the current file (PRS)
    std::atomic<uint64_t> curFileSize{0};    // size of the current file (PRS)

    void Reset()   // clear all counters + cancel before starting a run
    {
        cancel.store(false);
        filesTotal.store(0);
        filesScanned.store(0);
        filesSkipped.store(0);
        curOffset.store(0);
        curFileSize.store(0);
    }
};

// Files larger than this are skipped in a PRS search (a byte-by-byte decompress scan of a
// mult-hundred-MB image is impractical, and it would be read fully into memory).
constexpr uint64_t kPrsMaxFileBytes = 64ull << 20;   // 64 MiB

// Search `root` for the exact bytes [needle, needle+len). If `root` is a directory it
// is walked recursively; if it is a single file (an ISO/disc image) that one file is
// scanned. Fills `hits` (one entry per file with >= 1 match), most-hit files first is
// NOT guaranteed — order follows directory traversal. Returns the number of files
// scanned. `maxHitsPerFile` caps matches recorded per file (keeps a pathological file
// from flooding the UI); 0 disables the cap.
// General search over one or more `roots` (each a file or a directory; directories are
// walked recursively). `comp` selects exact vs PRS-compressed matching. `progress` (may be
// null) is updated as files are scanned and is polled for cancellation. Returns the number
// of files scanned. Desktop only — on the web build there is no host filesystem.
size_t SearchData(const std::vector<std::string>& roots, const uint8_t* needle, size_t len,
                  SearchCompression comp, std::vector<DataSearchHit>& hits,
                  size_t maxHitsPerFile = 256, SearchProgress* progress = nullptr);

// True if `path` names an existing directory (vs a file or nothing).
bool IsDirectory(const std::string& path);

// True if `path` exists (file or directory).
bool PathExists(const std::string& path);

}  // namespace sfe
