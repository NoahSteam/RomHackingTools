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
uint32_t SectorSizeOf(const std::string& type)
{
    const std::string t = UpperTok(type);
    if (t == "MODE1/2048") return 2048;
    if (t == "MODE2/2336") return 2336;
    return 2352;   // AUDIO, MODE1/2352, MODE2/2352, and anything else raw
}

// Extract the quoted name from a FILE line, else the first whitespace-delimited token after FILE.
std::string CueFileName(const std::string& line)
{
    const size_t q1 = line.find('"');
    if (q1 != std::string::npos)
    {
        const size_t q2 = line.find('"', q1 + 1);
        if (q2 != std::string::npos) return line.substr(q1 + 1, q2 - q1 - 1);
    }
    std::istringstream is(line);
    std::string kw, name;
    is >> kw >> name;
    return name;
}
}  // namespace

uint32_t MsfToFrames(const std::string& msf)
{
    int mm = 0, ss = 0, ff = 0;
    if (std::sscanf(msf.c_str(), "%d:%d:%d", &mm, &ss, &ff) != 3) return 0;
    return uint32_t(((mm * 60) + ss) * 75 + ff);
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
            currentFile = baseDir + CueFileName(trimmed);
        }
        else if (KW == "TRACK")
        {
            CueTrack t;
            std::string typeTok;
            ls >> t.number >> typeTok;
            t.typeStr = typeTok;
            t.sectorSize = SectorSizeOf(typeTok);
            t.isData = UpperTok(typeTok).compare(0, 4, "MODE") == 0;
            t.file = currentFile;
            sheet.tracks.push_back(t);
        }
        else if (KW == "INDEX" && !sheet.tracks.empty())
        {
            CueIndex idx;
            std::string msf;
            ls >> idx.number >> msf;
            idx.frames = MsfToFrames(msf);
            sheet.tracks.back().indices.push_back(idx);
        }
        else if (KW == "PREGAP" && !sheet.tracks.empty())
        {
            std::string msf;
            ls >> msf;
            sheet.tracks.back().pregapFrames = int(MsfToFrames(msf));
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
        if (!haveEnd)
        {
            const uint64_t bytes = fileSize ? fileSize(t.file) : 0;
            end = uint32_t(bytes / t.sectorSize);
        }
        out[i].file = t.file;
        out[i].sectorSize = t.sectorSize;
        out[i].offset = uint64_t(start) * t.sectorSize;
        out[i].length = end > start ? uint64_t(end - start) * t.sectorSize : 0;
    }
    return out;
}

}  // namespace sfe
