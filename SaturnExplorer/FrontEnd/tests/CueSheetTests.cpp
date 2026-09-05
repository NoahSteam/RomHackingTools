// Tests for the .cue parser and per-track byte-range computation, for both single-BIN (tracks
// split at index boundaries) and one-BIN-per-track layouts.
#include "Disc/CueSheet.h"

#include <cstdio>
#include <map>
#include <string>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; } }
constexpr uint32_t S = 2352;
}  // namespace

int main()
{
    // MSF round-trip.
    Check(MsfToFrames("00:02:00") == 150, "00:02:00 = 150 frames");
    Check(MsfToFrames("05:00:00") == 22500, "05:00:00 = 22500 frames");
    Check(FramesToMsf(150) == "00:02:00", "150 -> 00:02:00");
    Check(FramesToMsf(22650) == "05:02:00", "22650 -> 05:02:00");

    // --- Single-BIN cue: data track + two audio tracks. ---
    const std::string single =
        "FILE \"game.bin\" BINARY\r\n"
        "  TRACK 01 MODE1/2352\r\n"
        "    INDEX 01 00:00:00\r\n"
        "  TRACK 02 AUDIO\r\n"
        "    INDEX 00 05:00:00\r\n"
        "    INDEX 01 05:02:00\r\n"
        "  TRACK 03 AUDIO\r\n"
        "    INDEX 00 10:00:00\r\n"
        "    INDEX 01 10:02:00\r\n";
    CueSheet cs = ParseCueText(single, "disc/");
    Check(cs.ok, cs.ok ? "single parsed" : cs.error.c_str());
    Check(cs.tracks.size() == 3, "3 tracks");
    Check(cs.tracks[0].file == "disc/game.bin", "FILE resolved with baseDir");
    Check(cs.tracks[0].isData && cs.tracks[0].typeStr == "MODE1/2352", "track 1 is data mode1/2352");
    Check(!cs.tracks[1].isData && cs.tracks[1].typeStr == "AUDIO", "track 2 is audio");
    Check(cs.tracks[1].indices.size() == 2 && cs.tracks[1].indices[0].number == 0, "track 2 has INDEX 00");

    auto sizeSingle = [](const std::string& f) -> uint64_t {
        return f == "disc/game.bin" ? uint64_t(270000) * S : 0;   // 270000 sectors
    };
    std::vector<CueTrackRange> r = CueTrackRanges(cs, sizeSingle);
    Check(r[0].offset == 0 && r[0].length == uint64_t(22500) * S, "track1 [0, 22500)");
    Check(r[1].offset == uint64_t(22500) * S && r[1].length == uint64_t(22500) * S, "track2 [22500, 45000)");
    Check(r[2].offset == uint64_t(45000) * S && r[2].length == uint64_t(225000) * S, "track3 [45000, EOF)");

    // --- One-BIN-per-track cue. ---
    const std::string multi =
        "FILE \"t1.bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n"
        "FILE \"t2.bin\" BINARY\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 00 00:00:00\n"
        "    INDEX 01 00:02:00\n";
    CueSheet cm = ParseCueText(multi, "");
    Check(cm.ok && cm.tracks.size() == 2, "multi parsed, 2 tracks");
    Check(cm.tracks[0].file == "t1.bin" && cm.tracks[1].file == "t2.bin", "per-track files");
    auto sizeMulti = [](const std::string& f) -> uint64_t {
        if (f == "t1.bin") return uint64_t(1000) * S;
        if (f == "t2.bin") return uint64_t(500) * S;
        return 0;
    };
    std::vector<CueTrackRange> rm = CueTrackRanges(cm, sizeMulti);
    Check(rm[0].offset == 0 && rm[0].length == uint64_t(1000) * S, "t1 whole file");
    Check(rm[1].offset == 0 && rm[1].length == uint64_t(500) * S, "t2 whole file (incl. in-file pregap)");

    // A .iso-style single data track (2048).
    CueSheet ci = ParseCueText("FILE \"g.iso\" BINARY\n TRACK 01 MODE1/2048\n  INDEX 01 00:00:00\n", "");
    Check(ci.ok && ci.tracks[0].sectorSize == 2048, "mode1/2048 sector size");

    if (gFail == 0) std::printf("All CueSheet tests passed.\n");
    return gFail ? 1 : 0;
}
