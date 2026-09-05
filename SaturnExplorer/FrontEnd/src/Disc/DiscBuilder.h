// DiscBuilder — the "Build Disc Image" orchestrator. It rebuilds ONLY the data track from a
// directory of (modified) files and copies every other track of the original disc verbatim, so
// CD-audio and the exact track/index/pregap layout survive:
//
//   BIN/CUE (default):  Track 01 = internal ISO-9660 build -> MODE1/2352 raw .bin (real EDC/ECC),
//                       audio/extra tracks binary-copied from the source, plus a .cue that mirrors
//                       the original track types, pregaps and indices.
//   ISO (data only):    just the MODE1/2048 data track; no audio, no raw sectors.
//
// It composes IsoBuilder (filesystem), CdSector (raw sectors) and CueSheet (track layout). Native
// file I/O; no ImGui. The App fills in the data-track contents + IP.BIN + PVD ids and picks the
// format; a plain full-filesystem rebuild of the data track is fine here (files may grow/shrink),
// because nothing about the surrounding disc structure is regenerated.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Disc/IsoBuilder.h"   // IsoBuildOptions (data-track contents, ids, IP.BIN, skips)

namespace sfe
{

struct DiscBuildOptions
{
    IsoBuildOptions iso;              // data-track (Track 01) contents + PVD ids + IP.BIN
    std::string     sourceImage;      // original disc: a .cue (for audio + track layout) or a
                                      // bare .bin/.iso (single data track, no audio to copy)
    std::string     outPath;          // output path: the .cue (BIN/CUE) or the .iso (ISO)
    bool            binCue = true;    // true = BIN/CUE (recommended), false = ISO (data only)
};

struct DiscBuildResult
{
    bool        ok = false;
    std::string error;
    std::string cuePath;                 // the written .cue (BIN/CUE mode; IsoBuilder's cue in ISO mode)
    std::vector<std::string> outputs;    // every file written (track .bin(s), or the .iso)
    uint32_t    fileCount = 0;           // files packed into the data track
    uint32_t    dirCount = 0;
    uint32_t    trackCount = 0;          // tracks in the output disc
    uint32_t    audioTracksCopied = 0;
    uint64_t    totalBytes = 0;          // sum of the written image files
    bool        ipBinInjected = false;
    std::vector<std::string> warnings;
};

// Build the disc described by 'opt'. Never throws; on any error returns ok=false with 'error'.
DiscBuildResult BuildDiscImage(const DiscBuildOptions& opt);

struct VerifyEncodeResult
{
    bool        ok = false;          // the check ran to completion (independent of match/mismatch)
    std::string error;              // set when the check could not run
    bool        match = false;       // every checked sector re-encoded byte-for-byte identical
    uint32_t    sectorsChecked = 0;
    uint32_t    mismatches = 0;
    uint32_t    firstMismatchLba = 0;
    int         firstMismatchByte = -1;  // byte offset within the 2352-byte sector, -1 if none
};

// Independently validate the MODE1/2352 encoder against a real disc: read each raw sector of the
// source disc's Track 01, re-encode its 2048 user bytes with EncodeMode1Sector, and byte-compare
// the result (sync + address + EDC + ECC) against the original. A full match proves the encoder
// reproduces genuine CD-ROM sectors bit-for-bit. 'sourceImage' is a .cue or a bare MODE1/2352 .bin;
// it must be a raw 2352-byte data track (a MODE1/2048 .iso carries no EDC/ECC to check against).
VerifyEncodeResult VerifyDataTrackEncoding(const std::string& sourceImage);

}  // namespace sfe
