#include "Disc/DiscBuilder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include "Disc/CdSector.h"
#include "Disc/CueSheet.h"
#include "Disc/PathUtil.h"

namespace sfe
{
namespace
{
uint64_t FileSize(const std::string& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    return f ? uint64_t(f.tellg()) : 0;
}
std::string Track2(int n)
{
    char b[8];
    std::snprintf(b, sizeof b, "%02d", n);
    return b;
}

// Convert a MODE1/2048 .iso into a MODE1/2352 raw .bin (real sync/EDC/ECC per sector).
bool IsoToRawBin(const std::string& iso, const std::string& bin)
{
    std::ifstream in(iso, std::ios::binary);
    std::ofstream out(bin, std::ios::binary | std::ios::trunc);
    if (!in || !out) return false;
    uint8_t user[2048], raw[2352];
    uint32_t lba = 0;
    for (;;)
    {
        in.read(reinterpret_cast<char*>(user), 2048);
        if (in.gcount() == 0) break;
        if (in.gcount() < 2048) std::memset(user + in.gcount(), 0, size_t(2048 - in.gcount()));
        EncodeMode1Sector(lba++, user, raw);
        out.write(reinterpret_cast<char*>(raw), 2352);
    }
    return bool(out);
}

// Copy a byte range [offset, offset+length) from src to a fresh dst file.
bool CopyRange(const std::string& src, uint64_t offset, uint64_t length, const std::string& dst)
{
    std::ifstream in(src, std::ios::binary);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!in || !out) return false;
    in.seekg(std::streamoff(offset), std::ios::beg);
    std::vector<char> buf(1 << 20);
    uint64_t remaining = length;
    while (remaining > 0)
    {
        const std::streamsize n = std::streamsize(std::min<uint64_t>(remaining, buf.size()));
        in.read(buf.data(), n);
        if (in.gcount() != n) return false;
        out.write(buf.data(), n);
        remaining -= uint64_t(n);
    }
    return bool(out);
}
}  // namespace

DiscBuildResult BuildDiscImage(const DiscBuildOptions& opt)
{
    DiscBuildResult r;

    // --- 1) Build the data track's ISO-9660 filesystem (MODE1/2048). ---
    IsoBuildOptions iso = opt.iso;
    const std::string outDir = DirOf(opt.outPath);
    const std::string stem = Stem(opt.outPath);
    const std::string tempIso = opt.binCue ? (outDir + stem + ".__track01.iso") : opt.outPath;
    iso.outIso = tempIso;
    // Never pack the images we're about to write.
    iso.skipNames.push_back(BaseName(tempIso));
    iso.skipNames.push_back(stem + ".cue");

    const IsoBuildResult ib = IsoBuild(iso);
    if (!ib.ok) { r.error = "Data-track build failed: " + ib.error; return r; }
    r.fileCount = ib.fileCount;
    r.dirCount = ib.dirCount;
    r.ipBinInjected = ib.ipBinInjected;
    r.warnings = ib.warnings;

    // --- ISO output: the filesystem image is the final artifact. ---
    if (!opt.binCue)
    {
        r.ok = true;
        r.cuePath = ib.cuePath;
        r.outputs = { ib.isoPath, ib.cuePath };
        r.totalBytes = ib.imageBytes;
        r.trackCount = 1;
        r.warnings.push_back("ISO holds the data track only — use BIN/CUE to keep CD-audio tracks.");
        return r;
    }

    // --- 2) Convert the data track to a raw MODE1/2352 .bin, then drop the temp ISO. ---
    const std::string track01 = outDir + stem + " (Track 01).bin";
    if (!IsoToRawBin(tempIso, track01))
    {
        std::remove(tempIso.c_str());
        r.error = "Could not write the raw data track: " + track01;
        return r;
    }
    std::remove(tempIso.c_str());
    r.outputs.push_back(track01);
    r.totalBytes += FileSize(track01);

    // --- 3) Parse the source disc's track layout (for audio / extra tracks). ---
    CueSheet sheet;
    if (IEqualsExt(opt.sourceImage, ".cue"))
    {
        std::ifstream cf(opt.sourceImage, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
        sheet = ParseCueText(text, DirOf(opt.sourceImage));
    }
    const std::vector<CueTrackRange> ranges =
        sheet.ok ? CueTrackRanges(sheet, FileSize) : std::vector<CueTrackRange>();

    // Track-01 type: keep the source's if it was a raw data track, else default to MODE1/2352.
    std::string t1type = "MODE1/2352";
    if (sheet.ok && !sheet.tracks.empty() && sheet.tracks[0].isData &&
        sheet.tracks[0].sectorSize == 2352)
        t1type = sheet.tracks[0].typeStr;

    std::ostringstream cue;
    cue << "FILE \"" << BaseName(track01) << "\" BINARY\n"
        << "  TRACK 01 " << t1type << "\n"
        << "    INDEX 01 00:00:00\n";
    r.trackCount = 1;

    // --- 4) Copy every track after the first (audio / extra data) verbatim, mirroring the cue. ---
    for (size_t i = 1; sheet.ok && i < sheet.tracks.size(); ++i)
    {
        const CueTrack& t = sheet.tracks[i];
        const CueTrackRange& rg = ranges[i];
        const std::string outBin = outDir + stem + " (Track " + Track2(t.number) + ").bin";
        if (rg.length == 0 || !CopyRange(rg.file, rg.offset, rg.length, outBin))
        {
            r.warnings.push_back("Could not copy track " + Track2(t.number) + " from " + rg.file);
            continue;
        }
        r.outputs.push_back(outBin);
        r.totalBytes += rg.length;
        r.trackCount++;
        if (!t.isData) ++r.audioTracksCopied;

        cue << "FILE \"" << BaseName(outBin) << "\" BINARY\n"
            << "  TRACK " << Track2(t.number) << " " << t.typeStr << "\n";
        if (t.pregapFrames > 0) cue << "    PREGAP " << FramesToMsf(uint32_t(t.pregapFrames)) << "\n";
        const uint32_t start = TrackStartFrame(t);
        for (const CueIndex& idx : t.indices)
            cue << "    INDEX " << Track2(idx.number) << " "
                << FramesToMsf(idx.frames >= start ? idx.frames - start : 0) << "\n";
    }

    // --- 5) Write the .cue. ---
    const std::string cuePath = outDir + stem + ".cue";
    std::ofstream cueOut(cuePath, std::ios::trunc);
    if (!cueOut) { r.error = "Could not write cue: " + cuePath; return r; }
    cueOut << cue.str();
    if (!cueOut) { r.error = "Write error on cue: " + cuePath; return r; }
    r.cuePath = cuePath;
    r.outputs.push_back(cuePath);
    r.ok = true;
    return r;
}

VerifyEncodeResult VerifyDataTrackEncoding(const std::string& sourceImage)
{
    VerifyEncodeResult v;

    // Locate Track 01's raw byte range. A .cue names the real track file(s); a bare path is taken
    // as the whole data track. Either way we need a raw MODE1/2352 track to check EDC/ECC against.
    std::string trackFile = sourceImage;
    uint64_t    offset = 0;
    uint64_t    length = 0;
    uint32_t    startLba = 0;
    if (IEqualsExt(sourceImage, ".cue"))
    {
        std::ifstream cf(sourceImage, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
        const CueSheet sheet = ParseCueText(text, DirOf(sourceImage));
        if (!sheet.ok) { v.error = "Could not parse the source cue: " + sheet.error; return v; }
        const CueTrack& t1 = sheet.tracks[0];
        if (!t1.isData || t1.sectorSize != 2352)
        {
            v.error = "Track 01 is not a raw MODE1/2352 data track (" + t1.typeStr +
                      "); nothing to verify.";
            return v;
        }
        const std::vector<CueTrackRange> ranges = CueTrackRanges(sheet, FileSize);
        trackFile = ranges[0].file;
        offset = ranges[0].offset;
        length = ranges[0].length;
        startLba = TrackStartFrame(t1);
    }
    else
    {
        if (IEqualsExt(sourceImage, ".iso"))
        {
            v.error = "A MODE1/2048 .iso carries no EDC/ECC to verify; use a raw .bin/.cue.";
            return v;
        }
        length = FileSize(sourceImage);
    }

    if (length < 2352) { v.error = "Track 01 is empty or shorter than one sector."; return v; }

    std::ifstream in(trackFile, std::ios::binary);
    if (!in) { v.error = "Could not open the source track: " + trackFile; return v; }
    in.seekg(std::streamoff(offset), std::ios::beg);

    const uint32_t sectors = uint32_t(length / 2352);
    uint8_t raw[2352], enc[2352];
    for (uint32_t i = 0; i < sectors; ++i)
    {
        in.read(reinterpret_cast<char*>(raw), 2352);
        if (in.gcount() != 2352) { v.error = "Short read on the source track."; return v; }
        // Only MODE1 sectors carry the sync+EDC+ECC our encoder produces; leave others uncounted.
        if (raw[15] != 0x01) continue;
        EncodeMode1Sector(startLba + i, raw + 16, enc);
        ++v.sectorsChecked;
        if (std::memcmp(enc, raw, 2352) != 0)
        {
            if (v.mismatches == 0)
            {
                v.firstMismatchLba = startLba + i;
                for (int b = 0; b < 2352; ++b)
                    if (enc[b] != raw[b]) { v.firstMismatchByte = b; break; }
            }
            ++v.mismatches;
        }
    }

    v.ok = true;
    v.match = (v.mismatches == 0 && v.sectorsChecked > 0);
    return v;
}

}  // namespace sfe
