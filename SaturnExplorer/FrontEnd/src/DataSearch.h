// DataSearch — scan a game's data directory (a folder of extracted files, or a
// single image such as an ISO) for an exact byte sequence, e.g. the raw packed
// VRAM bytes of a selected texture. Desktop only: it reads the local filesystem
// directly (POSIX / Win32). On the web build there is no host filesystem, so a
// search simply finds nothing.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sfe
{

struct DataSearchHit
{
    std::string           path;     // absolute path of the file the needle was found in
    std::vector<uint64_t> offsets;  // byte offset of each match within that file
};

// Search `root` for the exact bytes [needle, needle+len). If `root` is a directory it
// is walked recursively; if it is a single file (an ISO/disc image) that one file is
// scanned. Fills `hits` (one entry per file with >= 1 match), most-hit files first is
// NOT guaranteed — order follows directory traversal. Returns the number of files
// scanned. `maxHitsPerFile` caps matches recorded per file (keeps a pathological file
// from flooding the UI); 0 disables the cap.
size_t SearchDataDir(const std::string& root, const uint8_t* needle, size_t len,
                     std::vector<DataSearchHit>& hits, size_t maxHitsPerFile = 256);

// True if `path` names an existing directory (vs a file or nothing).
bool IsDirectory(const std::string& path);

// True if `path` exists (file or directory).
bool PathExists(const std::string& path);

}  // namespace sfe
