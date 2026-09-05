// CueSheet — parse a .cue into its track layout, and compute each track's raw byte range within
// its BIN file(s). Build Disc Image uses this to preserve the original disc exactly: it rebuilds
// only the data track and copies every other (audio / extra data) track verbatim, re-emitting a
// .cue with the same track types, pregaps and indices. Pure text/logic — no file I/O here; the
// caller supplies file sizes so this stays unit-testable.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sfe
{

// "MM:SS:FF" (75 frames/second) <-> absolute frame (= sector) count.
uint32_t MsfToFrames(const std::string& msf);
std::string FramesToMsf(uint32_t frames);

struct CueIndex
{
    int      number = 0;      // INDEX number (0 = pregap start, 1 = track start)
    uint32_t frames = 0;      // position, in frames/sectors (absolute within its FILE)
};

struct CueTrack
{
    int         number = 0;
    std::string typeStr;      // exact token as written, e.g. "MODE1/2352", "AUDIO"
    uint32_t    sectorSize = 2352;  // bytes per sector for this track (2048 / 2336 / 2352)
    bool        isData = false;     // a MODE1/MODE2 track (the rebuild candidate) vs AUDIO
    std::string file;         // resolved path to the BIN/IMG this track lives in
    std::vector<CueIndex> indices;
    int         pregapFrames = 0;   // PREGAP command (a gap NOT present in the file), else 0
};

struct CueSheet
{
    bool                  ok = false;
    std::string           error;
    std::vector<CueTrack> tracks;
};

// Parse cue text. 'baseDir' (with trailing separator, or empty) resolves each FILE reference to
// a full path. Never throws; on a structural problem returns ok=false with 'error' set.
CueSheet ParseCueText(const std::string& text, const std::string& baseDir);

// The lowest INDEX frame of a track (INDEX 00 pregap if present, else INDEX 01) — where its data
// begins within its FILE. 0 if the track has no indices.
uint32_t TrackStartFrame(const CueTrack& t);

// One track's raw extent in its file.
struct CueTrackRange
{
    std::string file;
    uint64_t    offset = 0;     // byte offset of the track's first sector in 'file'
    uint64_t    length = 0;     // byte length of the track
    uint32_t    sectorSize = 2352;
};

// Compute every track's byte range. 'fileSize(path)' returns the byte size of a track file (0 if
// unknown) — used to bound the last track in each file. Handles single-BIN (tracks share a file,
// split at index boundaries) and one-BIN-per-track layouts.
std::vector<CueTrackRange> CueTrackRanges(
    const CueSheet& sheet, const std::function<uint64_t(const std::string&)>& fileSize);

}  // namespace sfe
