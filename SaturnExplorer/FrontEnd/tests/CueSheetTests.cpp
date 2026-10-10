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

    // Checked parsing: overflowing, signed, out-of-range and malformed stamps are errors.
    {
        uint32_t f = 123;
        Check(ParseMsf("00:02:00", f) && f == 150, "ParseMsf accepts a normal stamp");
        Check(!ParseMsf("2147483647:00:00", f), "huge minutes rejected (was signed overflow)");
        Check(!ParseMsf("99999999999:00:00", f), "absurd minutes rejected");
        Check(!ParseMsf("-1:00:00", f), "negative field rejected");
        Check(!ParseMsf("00:60:00", f), "seconds >= 60 rejected");
        Check(!ParseMsf("00:00:75", f), "frames >= 75 rejected");
        Check(!ParseMsf("00:00", f), "two fields rejected");
        Check(!ParseMsf("00:00:00:00", f), "four fields rejected");
        Check(!ParseMsf("00:0a:00", f), "non-numeric field rejected");
        Check(!ParseMsf("", f), "empty stamp rejected");
        CueSheet bad = ParseCueText("FILE \"a.bin\" BINARY\n TRACK 01 MODE1/2352\n"
                                    "  INDEX 01 2147483647:00:00\n", "");
        Check(!bad.ok && !bad.error.empty(), "a cue with an overflowing INDEX fails to parse");
    }
    Check(FramesToMsf(22650) == "05:02:00", "22650 -> 05:02:00");

    // Malformed TRACK / INDEX lines are errors, not a track with a default number or type.
    {
        const char* bad[] = {
            "FILE \"a.bin\" BINARY\n TRACK\n  INDEX 01 00:00:00\n",
            "FILE \"a.bin\" BINARY\n TRACK 01\n  INDEX 01 00:00:00\n",
            "FILE \"a.bin\" BINARY\n TRACK xx MODE1/2352\n  INDEX 01 00:00:00\n",
            "FILE \"a.bin\" BINARY\n TRACK 00 MODE1/2352\n  INDEX 01 00:00:00\n",
            "FILE \"a.bin\" BINARY\n TRACK 100 AUDIO\n  INDEX 01 00:00:00\n",
            "FILE \"a.bin\" BINARY\n TRACK 01 BOGUS/1234\n  INDEX 01 00:00:00\n",
            "FILE \"a.bin\" BINARY\n TRACK 01 MODE1/2352\n  INDEX 01 00:00:00\n"
            " TRACK 01 AUDIO\n  INDEX 01 00:00:00\n",
            "FILE \"a.bin\" BINARY\n TRACK 02 MODE1/2352\n  INDEX 01 00:00:00\n"
            " TRACK 01 AUDIO\n  INDEX 01 00:01:00\n",
            "FILE \"a.bin\" BINARY\n TRACK 01 MODE1/2352\n  INDEX\n",
            "FILE \"a.bin\" BINARY\n TRACK 01 MODE1/2352\n  INDEX x 00:00:00\n",
        };
        for (const char* text : bad)
        {
            const CueSheet cs = ParseCueText(text, "");
            Check(!cs.ok && !cs.error.empty(), text);
        }
        Check(ParseCueText("FILE \"a.bin\" BINARY\n track 01 mode1/2048\n  INDEX 01 00:00:00\n", "").ok,
              "lower-case track types still parse");
    }

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

    // FILE types are recorded, and a range that is not a whole number of sectors says so
    // instead of being floored.
    {
        CueSheet s = ParseCueText("FILE \"a.bin\" BINARY\n TRACK 01 MODE1/2352\n  INDEX 01 00:00:00\n"
                                  "FILE \"b.wav\" wave\n TRACK 02 AUDIO\n  INDEX 01 00:00:00\n"
                                  "FILE c.bin BINARY\n TRACK 03 AUDIO\n  INDEX 01 00:00:00\n", "d/");
        Check(s.ok, "multi-file cue parses");
        Check(s.tracks[0].fileType == "BINARY", "quoted FILE type");
        Check(s.tracks[1].fileType == "WAVE", "FILE type is upper-cased");
        Check(s.tracks[2].fileType == "BINARY" && s.tracks[2].file == "d/c.bin", "unquoted FILE type");

        auto sizes = [](const std::string& p) -> uint64_t {
            if (p == "d/a.bin") return 2352 * 10;
            if (p == "d/b.wav") return 2353;          // one byte past a sector
            if (p == "d/c.bin") return 2351;          // one byte short of a sector
            return 0;
        };
        const std::vector<CueTrackRange> r = CueTrackRanges(s, sizes);
        Check(r[0].problem.empty() && r[0].length == 2352 * 10, "whole sectors are exact");
        Check(!r[1].problem.empty(), "a partial trailing sector is a problem, not dropped");
        Check(!r[2].problem.empty(), "a file shorter than a sector is a problem");

        CueSheet o = ParseCueText("FILE \"a.bin\" BINARY\n TRACK 01 MODE1/2352\n  INDEX 01 00:00:00\n"
                                  " TRACK 02 AUDIO\n  INDEX 00 00:05:00\n  INDEX 01 00:04:00\n", "");
        const std::vector<CueTrackRange> ro =
            CueTrackRanges(o, [](const std::string&) -> uint64_t { return 2352 * 600; });
        Check(!ro[1].problem.empty(), "indices out of order are a problem");
    }

    if (gFail == 0) std::printf("All CueSheet tests passed.\n");
    return gFail ? 1 : 0;
}
