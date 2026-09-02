// IsoBuilder — rebuild a bootable Sega Saturn disc image from a directory of extracted disc
// contents. Produces a plain 2048-byte Mode 1 ISO-9660 (Level 1, 8.3 names) image with the
// original 32 KB Saturn boot header (IP.BIN) injected into the system area, plus a matching
// single-track .cue. It is the write-side counterpart of IsoFs (the reader): an image this
// builds parses back through IsoParse with the same files and sizes.
//
// Native only (uses std::ifstream/ofstream and the platform directory walk). Pure logic + file
// I/O, no ImGui — the App wires it to the Build ISO / Build & Launch menu items. See
// ARCHITECTURE.md (this lives in the FrontEnd, like the rest of the Disc/ code).
//
// Scope, and the caveat the Saturn romhacking notes stress: this does a FULL filesystem rebuild,
// so files are laid out at fresh LBAs (sorted 8.3 order). That is fine for a game that finds its
// data by filename, but a title that hard-codes sector numbers can break. The boot header is
// preserved verbatim; the filesystem is regenerated.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sfe
{

struct IsoBuildOptions
{
    std::string rootDir;                     // directory whose contents become the disc
    std::string outIso;                      // output .iso path (a sibling .cue is derived)
    std::string volumeId = "SATURN_GAME";    // PVD volume identifier (<=32 chars)
    std::string systemId = "SEGA SEGASATURN";// PVD system identifier (<=32 chars)
    std::vector<uint8_t> ipBin;              // 32 KB boot header for the system area (empty = zeroed)
    // File basenames (case-insensitive) to skip — the disc image being built, project files, etc.
    // Defaults are filled by the App; extension matching is on the caller's list.
    std::vector<std::string> skipExtensions; // e.g. {".iso", ".cue", ".bin", ".seproj"}
    std::vector<std::string> skipNames;      // exact basenames, e.g. {"IP.BIN", "se_patch.py"}
};

struct IsoBuildResult
{
    bool        ok = false;
    std::string error;                 // reason when !ok
    std::string isoPath;               // the .iso written
    std::string cuePath;               // the .cue written
    uint32_t    fileCount = 0;         // regular files packed
    uint32_t    dirCount = 0;          // directories (excluding the root)
    uint64_t    imageBytes = 0;        // size of the written .iso
    bool        ipBinInjected = false; // a non-empty IP.BIN was placed in the system area
    uint32_t    renamedForIso = 0;     // names changed to satisfy 8.3 / de-duplicate
    std::vector<std::string> warnings; // non-fatal notes (e.g. a renamed file)
};

// Build the image described by 'opts'. Never throws; on any error returns a result with ok=false
// and a filled 'error'. Writes both the .iso and its .cue on success.
IsoBuildResult IsoBuild(const IsoBuildOptions& opts);

}  // namespace sfe
