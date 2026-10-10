#include "Disc/CueSheet.h"

#include <cstdio>
#include <sstream>

namespace sfe
{
namespace
{
std::string UpperTok(const std::string& s)
{
    std::string o = s;
    for (char& c : o) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return o;
}

// The bytes-per-sector implied by a track type token.
// 0 for a type this parser does not model: guessing a sector size for it would mis-split
// every track that follows in the same file.
uint32_t SectorSizeOf(const std::string& type)
{
    const std::string t = UpperTok(type);
    if (t == "MODE1/2048") return 2048;
    if (t == "MODE2/2336") return 2336;
    if (t == "AUDIO" || t == "MODE1/2352" || t == "MODE2/2352") return 2352;
    return 0;
}

// Extract the quoted name from a FILE line, else the first whitespace-delimited token after FILE.
// 'type' receives the token after the name (BINARY, MOTOROLA, WAVE, ...), upper-cased.
std::string CueFileName(const std::string& line, std::string& type)
{
    std::string name;
    std::string rest;
    const size_t q1 = line.find('"');
    const size_t q2 = q1 == std::string::npos ? std::string::npos : line.find('"', q1 + 1);
    if (q2 != std::string::npos)
    {
        name = line.substr(q1 + 1, q2 - q1 - 1);
        rest = line.substr(q2 + 1);
        std::istringstream is(rest);
        is >> type;
    }
    else
    {
        std::istringstream is(line);
        std::string kw;
        is >> kw >> name >> type;
    }
    type = UpperTok(type);
    return name;
}
}  // namespace

bool ParseMsf(const std::string& msf, uint32_t& frames)
{
    uint32_t field[3] = { 0, 0, 0 };
    size_t pos = 0;
    for (int i = 0; i < 3; ++i)
    {
        const size_t start = pos;
        uint32_t v = 0;
        while (pos < msf.size() && msf[pos] >= '0' && msf[pos] <= '9')
        {
            v = v * 10 + uint32_t(msf[pos] - '0');
            if (v > 9999) return false;   // also keeps the accumulator far from overflow
            ++pos;
        }
        if (pos == start) return false;   // empty, signed or non-numeric field
        field[i] = v;
        if (i < 2)
        {
            if (pos >= msf.size() || msf[pos] != ':') return false;
            ++pos;
        }
    }
    if (pos != msf.size()) return false;
    if (field[1] >= 60 || field[2] >= 75) return false;
    frames = (field[0] * 60 + field[1]) * 75 + field[2];
    return true;
}

uint32_t MsfToFrames(const std::string& msf)
{
    uint32_t frames = 0;
    return ParseMsf(msf, frames) ? frames : 0;
}

std::string FramesToMsf(uint32_t frames)
{
    const uint32_t mm = frames / (60 * 75);
    const uint32_t ss = (frames / 75) % 60;
    const uint32_t ff = frames % 75;
    char b[16];
    std::snprintf(b, sizeof b, "%02u:%02u:%02u", mm, ss, ff);
    return b;
}

CueSheet ParseCueText(const std::string& text, const std::string& baseDir)
{
    CueSheet sheet;
    std::istringstream in(text);
    std::string line;
    std::string currentFile;
    std::string currentType;
    while (std::getline(in, line))
    {
        // Trim leading whitespace and a trailing CR.
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        if (line.back() == '\r') line.pop_back();
        const std::string trimmed = line.substr(a);
        std::istringstream ls(trimmed);
        std::string kw;
        ls >> kw;
        const std::string KW = UpperTok(kw);

        if (KW == "FILE")
        {
            currentFile = baseDir + CueFileName(trimmed, currentType);
        }
        else if (KW == "TRACK")
        {
            CueTrack t;
            std::string typeTok;
            if (!(ls >> t.number >> typeTok))
            {
                sheet.error = "incomplete TRACK line: " + trimmed;
                return sheet;
            }
            // Track numbers name the output files, so a repeated or out-of-order number would
            // overwrite an earlier track's output (track 01 included) instead of failing.
            if (t.number < 1 || t.number > 99 ||
                (!sheet.tracks.empty() && t.number <= sheet.tracks.back().number))
            {
                sheet.error = "invalid or out-of-order TRACK number: " + trimmed;
                return sheet;
            }
            t.typeStr = typeTok;
            t.sectorSize = SectorSizeOf(typeTok);
            if (t.sectorSize == 0)
            {
                sheet.error = "unsupported TRACK type: " + typeTok;
                return sheet;
            }
            t.isData = UpperTok(typeTok).compare(0, 4, "MODE") == 0;
            t.file = currentFile;
            t.fileType = currentType;
            sheet.tracks.push_back(t);
        }
        else if (KW == "INDEX" && !sheet.tracks.empty())
        {
            CueIndex idx;
            std::string msf;
            if (!(ls >> idx.number >> msf) || idx.number < 0 || idx.number > 99)
            {
                sheet.error = "invalid INDEX line: " + trimmed;
                return sheet;
            }
            if (!ParseMsf(msf, idx.frames))
            {
                sheet.error = "invalid INDEX timestamp: " + msf;
                return sheet;
            }
            sheet.tracks.back().indices.push_back(idx);
        }
        else if (KW == "PREGAP" && !sheet.tracks.empty())
        {
            std::string msf;
            ls >> msf;
            uint32_t gap = 0;
            if (!ParseMsf(msf, gap))
            {
                sheet.error = "invalid PREGAP timestamp: " + msf;
                return sheet;
            }
            sheet.tracks.back().pregapFrames = int(gap);
        }
    }

    if (sheet.tracks.empty()) { sheet.error = "no tracks in cue"; return sheet; }
    for (const CueTrack& t : sheet.tracks)
        if (t.file.empty()) { sheet.error = "track with no FILE"; return sheet; }
    sheet.ok = true;
    return sheet;
}

uint32_t TrackStartFrame(const CueTrack& t)
{
    uint32_t f = 0; bool any = false;
    for (const CueIndex& i : t.indices) { if (!any || i.frames < f) { f = i.frames; any = true; } }
    return any ? f : 0;
}

std::vector<CueTrackRange> CueTrackRanges(
    const CueSheet& sheet, const std::function<uint64_t(const std::string&)>& fileSize)
{
    std::vector<CueTrackRange> out(sheet.tracks.size());
    for (size_t i = 0; i < sheet.tracks.size(); ++i)
    {
        const CueTrack& t = sheet.tracks[i];
        const uint32_t start = TrackStartFrame(t);
        // The track ends where the next track SHARING THE SAME FILE begins, else at end of file.
        uint32_t end = 0; bool haveEnd = false;
        for (size_t j = i + 1; j < sheet.tracks.size(); ++j)
            if (sheet.tracks[j].file == t.file) { end = TrackStartFrame(sheet.tracks[j]); haveEnd = true; break; }
        uint64_t endBytes = 0;
        if (haveEnd)
        {
            endBytes = uint64_t(end) * t.sectorSize;
        }
        else
        {
            endBytes = fileSize ? fileSize(t.file) : 0;
            if (endBytes % t.sectorSize != 0)
                out[i].problem = "its file is not a whole number of " +
                                 std::to_string(t.sectorSize) + "-byte sectors";
        }
        for (size_t k = 1; k < t.indices.size(); ++k)
            if (t.indices[k].frames < t.indices[k - 1].frames ||
                t.indices[k].number <= t.indices[k - 1].number)
                out[i].problem = "its indices are out of order";
        out[i].file = t.file;
        out[i].sectorSize = t.sectorSize;
        out[i].offset = uint64_t(start) * t.sectorSize;
        if (endBytes <= out[i].offset)
        {
            out[i].length = 0;
            if (out[i].problem.empty()) out[i].problem = "it has no sectors";
        }
        else
        {
            out[i].length = (endBytes - out[i].offset) / t.sectorSize * t.sectorSize;
            // Every index has to land on a sector the track actually has. One at or past the
            // end (the file's last sector, or the next track's start in a shared file) would be
            // written into a cue that points outside the copied track.
            const uint64_t endFrame = endBytes / t.sectorSize;
            for (const CueIndex& idx : t.indices)
                if (idx.frames >= endFrame && out[i].problem.empty())
                    out[i].problem = "its INDEX " + std::to_string(idx.number) +
                                     " lies beyond the end of the track";
        }
    }
    return out;
}

}  // namespace sfe
