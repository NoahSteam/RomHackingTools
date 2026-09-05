// IsoFs — a minimal ISO 9660 filesystem reader for Saturn disc images. Given a way to read
// 2048-byte logical sectors, it parses the Primary Volume Descriptor and walks the directory
// tree into a flat list of files (full path, start LBA, byte size). That lets a sector or CD
// frame address (FAD) be mapped back to the file it belongs to — the last hop in "audio is
// playing" -> "SCSP voice" -> "streaming code" -> "which file on the disc".
//
// Pure and self-contained (no ImGui, no file I/O): it reads through a SectorReader callback,
// so it is unit-testable against an in-memory image and reused by the real DiscImage. Scope is
// the common Saturn case (single data track, 8.3-ish ISO names, nested directories); it does
// not decode Joliet/Rock Ridge long names.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sfe
{

// Read one 2048-byte logical sector 'lba' into 'out' (>= 2048 bytes). Return false if the
// sector can't be read (past the end of the image, I/O error). LBA is the ISO logical block.
using SectorReader = std::function<bool(uint32_t lba, uint8_t* out)>;

struct IsoEntry
{
    std::string path;      // full path from the root, e.g. "/SOUND/BGM01.PCM"
    uint32_t    lba = 0;   // starting logical block (2048-byte sector) of the extent
    uint32_t    size = 0;  // byte length (0 for directories)
    bool        isDir = false;
};

struct IsoFs
{
    bool                  ok = false;   // a valid ISO 9660 volume was parsed
    std::string           volumeId;     // PVD volume identifier (trimmed)
    std::string           systemId;     // PVD system identifier (trimmed)
    std::string           publisherId;  // PVD publisher identifier (trimmed)
    std::string           preparerId;   // PVD data-preparer identifier (trimmed)
    std::string           applicationId;// PVD application identifier (trimmed)
    std::string           error;        // reason when !ok
    std::vector<IsoEntry> entries;      // files + directories, directory-first walk order

    // Return the file whose extent contains logical block 'lba' (nullptr if none). Ignores
    // directories. Used to resolve a CD read address to a filename.
    const IsoEntry* FileAt(uint32_t lba) const;
};

// Parse the ISO 9660 volume reachable through 'read'. Never throws; on any structural problem
// it returns an IsoFs with ok=false and a filled 'error'. Bounded in directory depth and total
// entries so a malformed image can't run away.
IsoFs IsoParse(const SectorReader& read);

}  // namespace sfe
