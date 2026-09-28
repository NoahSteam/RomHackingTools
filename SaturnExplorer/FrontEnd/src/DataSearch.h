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

// Files larger than this are skipped in a PRS search (a byte-by-byte decompress scan of a
// mult-hundred-MB image is impractical, and it would be read fully into memory).
constexpr uint64_t kPrsMaxFileBytes = 64ull << 20;   // 64 MiB

// A PRS search attempts a decompression at every byte offset, which is quadratic in the file
// size in the worst case: the cost of one offset is bounded by how much output its stream emits,
// and a highly compressible (or deliberately crafted) stream emits a great deal from very little
// input -- a long copy spends about three input bytes to emit up to 256, so roughly 80x. Two
// bounds keep that in hand.
//
// Per offset: a decompressed block is only useful if it fits in Saturn memory, and the largest
// single region is 1 MiB of work RAM -- a "block" bigger than that is not a block, it is a
// garbage offset that happens to decode. This is far below the decompressor's own 32 MiB default,
// which stays as it is for the extract path, where the user has pointed at a specific stream.
constexpr size_t kPrsMaxBlockBytes = 1u << 20;   // 1 MiB

// Per file: the total output across every offset tried. At the ratio above, a 4 MiB file could
// legitimately be driven to hundreds of gigabytes of emitted bytes before any cancel poll gets a
// look in. 4 GiB is minutes of work, not hours, and a file that needs more than that is not going
// to be searched to the end by waiting longer.
constexpr uint64_t kPrsMaxFileOutputBytes = 4ull << 30;   // 4 GiB of decompressed bytes

// Live progress + cancellation for a (potentially slow) search, shared with the worker
// thread. All fields are atomic; set `cancel` from any thread to stop early.
struct SearchProgress
{
    std::atomic<bool>     cancel{false};
    std::atomic<size_t>   filesTotal{0};
    std::atomic<size_t>   filesScanned{0};
    std::atomic<size_t>   filesSkipped{0};   // too large for a PRS scan (see kPrsMaxFileBytes)
    // Files whose PRS scan ran out of work budget part-way through (see kPrsMaxFileOutputBytes).
    // A nonzero count means the search covered less than it was asked to: those files may hold a
    // match at an offset that was never reached. Reporting a hit count without it is reporting a
    // number the search cannot stand behind.
    std::atomic<size_t>   filesBudgetExhausted{0};
    std::atomic<uint64_t> curOffset{0};      // scan position within the current file (PRS)
    std::atomic<uint64_t> curFileSize{0};    // size of the current file (PRS)
    // The per-file decompressed-output budget (see kPrsMaxFileOutputBytes) this run works to.
    // Here rather than a fixed constant because it is the same kind of knob as `cancel`: how much
    // of the user's time the search may spend before giving up on a file.
    std::atomic<uint64_t> prsOutputBudget{kPrsMaxFileOutputBytes};

    void Reset()   // clear all counters + cancel before starting a run
    {
        cancel.store(false);
        filesTotal.store(0);
        filesScanned.store(0);
        filesSkipped.store(0);
        filesBudgetExhausted.store(0);
        curOffset.store(0);
        curFileSize.store(0);
        prsOutputBudget.store(kPrsMaxFileOutputBytes);
    }
};

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
