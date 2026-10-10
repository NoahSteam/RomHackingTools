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
#include "FileWrite.h"

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
// A read error is not end-of-file: a loop that stops at either would convert a prefix of the
// image and call it done.
bool IsoToRawBin(const std::string& iso, const std::string& bin, std::string& error)
{
    std::ifstream in(iso, std::ios::binary);
    std::ofstream out(bin, std::ios::binary | std::ios::trunc);
    if (!in) { error = "Could not read the data-track image: " + iso; return false; }
    if (!out) { error = "Could not write the raw data track: " + bin; return false; }
    const uint64_t expected = (FileSize(iso) + 2047) / 2048;
    uint8_t user[2048], raw[2352];
    uint32_t lba = 0;
    for (;;)
    {
        in.read(reinterpret_cast<char*>(user), 2048);
        if (in.bad()) { error = "Read error on the data-track image: " + iso; return false; }
        if (in.gcount() == 0) break;
        if (in.gcount() < 2048) std::memset(user + in.gcount(), 0, size_t(2048 - in.gcount()));
        EncodeMode1Sector(lba++, user, raw);
        out.write(reinterpret_cast<char*>(raw), 2352);
        if (!out) { error = "Write error on the raw data track: " + bin; return false; }
        if (in.eof()) break;
    }
    if (lba != expected)
    {
        error = "The data-track image ended early (" + std::to_string(lba) + " of " +
                std::to_string(expected) + " sectors): " + iso;
        return false;
    }
    out.close();   // flushes; a full disk reports itself here
    if (!out) { error = "Write error on the raw data track: " + bin; return false; }
    return true;
}

// Copy a byte range [offset, offset+length) from src to dst.
bool CopyRange(const std::string& src, uint64_t offset, uint64_t length, const std::string& dst)
{
    std::ifstream in(src, std::ios::binary);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!in || !out) return false;
    in.seekg(std::streamoff(offset), std::ios::beg);
    if (!in) return false;
    std::vector<char> buf(1 << 20);
    uint64_t remaining = length;
    while (remaining > 0)
    {
        const std::streamsize n = std::streamsize(std::min<uint64_t>(remaining, buf.size()));
        in.read(buf.data(), n);
        if (in.gcount() != n) return false;
        out.write(buf.data(), n);
        if (!out) return false;
        remaining -= uint64_t(n);
    }
    out.close();
    return bool(out);
}

bool WriteText(const std::string& path, const std::string& text)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    f.close();
    return bool(f);
}

// Every output is written to a staging file of its own and only moved into place once the whole
// set has been produced. Writing the final names directly meant a failure halfway left a mix
// of old and new files -- or no previous image at all -- and a final name that was a symlink or
// hard link to a source file was written straight through it.
struct StagedOutput
{
    std::string temp;
    std::string final;
};

struct Staging
{
    std::vector<StagedOutput> outputs;
    std::vector<std::string>  scratch;   // staged but never published (the intermediate .iso)

    ~Staging()
    {
        for (const StagedOutput& o : outputs) if (!o.temp.empty()) RemoveFile(o.temp);
        for (const std::string& t : scratch) RemoveFile(t);
    }
    // Create a staging file for 'final'; "" (with 'error') on failure.
    std::string Add(const std::string& final, std::string& error, bool publish = true)
    {
        std::string why;
        const std::string temp = CreateStagingFile(final, why);
        if (temp.empty()) { error = "Could not create an output file beside " + final + ": " + why; return temp; }
        if (publish) outputs.push_back({ temp, final });
        else scratch.push_back(temp);
        return temp;
    }
    // Move every staged file into place, the cue last so it never names a track that is not
    // there yet. All or nothing: each previous output is first moved aside, and if any step
    // fails every published file is taken back out and the previous ones are put back, so a
    // failure here cannot leave new tracks beside an old cue.
    bool Publish(std::string& error)
    {
        struct Saved { std::string final, backup; };
        std::vector<Saved> saved;
        std::vector<std::string> published;
        bool failed = false;

        for (const StagedOutput& o : outputs)
        {
            if (!PathEntryExists(o.final)) continue;
            std::string why;
            const std::string backup = CreateStagingFile(o.final, why);
            if (backup.empty() || !PublishFile(o.final, backup, why))
            {
                if (!backup.empty()) RemoveFile(backup);
                error = "Could not move the previous " + o.final + " aside: " + why;
                failed = true;
                break;
            }
            saved.push_back({ o.final, backup });
        }
        for (StagedOutput& o : outputs)
        {
            if (failed) break;
            std::string why;
            if (!PublishFile(o.temp, o.final, why))
            {
                error = "Could not move the finished output into place: " + why;
                failed = true;
                break;
            }
            published.push_back(o.final);
            o.temp.clear();
        }

        if (!failed)
        {
            for (const Saved& b : saved) RemoveFile(b.backup);
            return true;
        }

        // Roll back: new files out, previous ones back in.
        std::string stuck;
        for (const std::string& f : published)
        {
            bool hadPrevious = false;
            for (const Saved& b : saved) if (b.final == f) hadPrevious = true;
            if (!hadPrevious) RemoveFile(f);
        }
        for (const Saved& b : saved)
        {
            std::string why;
            if (!PublishFile(b.backup, b.final, why)) stuck += "\n  " + b.backup + " (was " + b.final + ")";
        }
        if (!stuck.empty())
            error += "\nThe previous output could not be fully restored; it is kept as:" + stuck;
        else
            error += "\nThe previous output was restored unchanged.";
        return false;
    }
};

// The first track is rebuilt from scratch and always starts at its INDEX 01. Only a PREGAP (a gap
// not stored in the file) can be carried over as written; an INDEX 00 or extra indices describe
// sectors in the file that the rebuilt track would not have.
bool FirstTrackLayoutSupported(const CueTrack& t)
{
    return t.indices.size() == 1 && t.indices[0].number == 1;
}
}  // namespace

DiscBuildResult BuildDiscImage(const DiscBuildOptions& opt)
{
    DiscBuildResult r;

    // --- 0) Read the source disc's track layout first. A CUE the caller named that cannot be
    //        read or parsed is an error: continuing would build a data-only disc that looks
    //        successful while silently dropping every audio track the CUE described. ---
    CueSheet sheet;
    std::vector<CueTrackRange> ranges;
    std::vector<std::string> sources;
    if (!opt.sourceImage.empty()) sources.push_back(opt.sourceImage);
    if (opt.binCue && IEqualsExt(opt.sourceImage, ".cue"))
    {
        std::string text;
        if (!ReadWholeFile(opt.sourceImage, text))
        {
            r.error = "Could not read the source CUE: " + opt.sourceImage;
            return r;
        }
        sheet = ParseCueText(text, DirOf(opt.sourceImage));
        if (!sheet.ok)
        {
            r.error = "The source CUE could not be parsed (" + sheet.error + "): " + opt.sourceImage;
            return r;
        }
        // The rebuilt data track is always MODE1/2352 sectors. A MODE2 first track would be
        // written with MODE1 sectors under a MODE2 label, so refuse rather than mislabel it.
        const CueTrack& first = sheet.tracks[0];
        if (first.isData && first.typeStr.size() >= 5 &&
            (first.typeStr[4] == '2'))
        {
            r.error = "The source disc's data track is " + first.typeStr +
                      ", which this builder cannot re-encode (it writes MODE1 sectors).";
            return r;
        }
        if (!FirstTrackLayoutSupported(first))
        {
            r.error = "The source disc's first track has an index layout (INDEX 00 or extra "
                      "indices) that a rebuilt data track cannot reproduce.";
            return r;
        }

        // Every other track is copied byte-for-byte and relabelled BINARY, which is only right
        // when the source already is raw little-endian sectors. A WAVE file would be copied
        // with its RIFF header as if it were samples; a MOTOROLA one would play byte-swapped.
        ranges = CueTrackRanges(sheet, FileSize);
        for (size_t i = 0; i < sheet.tracks.size(); ++i)
        {
            const CueTrack& t = sheet.tracks[i];
            sources.push_back(t.file);
            if (i == 0) continue;
            if (!t.fileType.empty() && t.fileType != "BINARY")
            {
                r.error = "Track " + Track2(t.number) + " is stored as " + t.fileType +
                          ", which this builder cannot copy (only raw BINARY tracks are supported).";
                return r;
            }
            // A missing file is the copy failure handled below (and allowed in a partial
            // build). A file that is present but does not hold whole sectors, or indices that
            // make no sense, is a layout this builder would silently get wrong.
            if (!ranges[i].problem.empty() && FileOrDirectoryExists(t.file))
            {
                r.error = "Track " + Track2(t.number) + " cannot be copied exactly: " +
                          ranges[i].problem + " (" + t.file + ").";
                return r;
            }
        }
    }

    // --- 1) Decide every output name, and refuse placements that would read from what they
    //        write: an output folder inside the packed tree packs the previous build into the
    //        new one, and an output name that is (a link to) a source file would overwrite it. ---
    const std::string outDir = DirOf(opt.outPath);
    const std::string stem = Stem(opt.outPath);
    if (stem.empty()) { r.error = "No output file name given."; return r; }
    if (PathIsWithin(outDir.empty() ? std::string(".") : outDir, opt.iso.rootDir))
    {
        r.error = "The output folder is inside the Data Directory being packed, so the build "
                  "would pack its own previous output. Choose an output folder outside it.";
        return r;
    }
    const std::string cuePath = outDir + stem + ".cue";
    const std::string track01 = opt.binCue ? outDir + stem + " (Track 01).bin" : opt.outPath;
    std::vector<std::string> finals = { track01 };
    for (size_t i = 1; i < sheet.tracks.size(); ++i)
        finals.push_back(outDir + stem + " (Track " + Track2(sheet.tracks[i].number) + ").bin");
    finals.push_back(cuePath);
    for (const std::string& f : finals)
    {
        // Outputs are published by rename, which would replace a device or directory itself.
        if (ExistsAsNonRegularFile(f))
        {
            r.error = "An output path exists and is not a regular file: " + f;
            return r;
        }
        for (const std::string& src : sources)
        {
            if (SameFile(f, src))
            {
                r.error = "The output " + f + " is the same file as the source " + src +
                          "; building would overwrite the source. Choose another output name.";
                return r;
            }
        }
    }

    Staging staging;

    // --- 2) Build the data track's ISO-9660 filesystem (MODE1/2048). ---
    IsoBuildOptions iso = opt.iso;
    iso.writeCue = false;
    iso.outIso = staging.Add(track01, r.error, /*publish=*/!opt.binCue);
    if (iso.outIso.empty()) return r;

    const IsoBuildResult ib = IsoBuild(iso);
    if (!ib.ok) { r.error = "Data-track build failed: " + ib.error; return r; }
    r.fileCount = ib.fileCount;
    r.dirCount = ib.dirCount;
    r.ipBinInjected = ib.ipBinInjected;
    r.warnings = ib.warnings;
    r.trackCount = 1;

    std::ostringstream cue;
    if (!opt.binCue)
    {
        // --- ISO output: the filesystem image is the final artifact, with a one-track cue. ---
        r.totalBytes = ib.imageBytes;
        cue << "FILE \"" << BaseName(track01) << "\" BINARY\n"
            << "  TRACK 01 MODE1/2048\n"
            << "    INDEX 01 00:00:00\n";
        r.warnings.push_back("ISO holds the data track only — use BIN/CUE to keep CD-audio tracks.");
    }
    else
    {
        // --- 3) Convert the data track to a raw MODE1/2352 .bin. ---
        const std::string rawTemp = staging.Add(track01, r.error);
        if (rawTemp.empty()) return r;
        if (!IsoToRawBin(iso.outIso, rawTemp, r.error)) return r;
        r.totalBytes += FileSize(rawTemp);

        cue << "FILE \"" << BaseName(track01) << "\" BINARY\n"
            << "  TRACK 01 MODE1/2352\n";   // what IsoToRawBin writes; MODE2 was refused above
        if (sheet.ok && sheet.tracks[0].pregapFrames > 0)
            cue << "    PREGAP " << FramesToMsf(uint32_t(sheet.tracks[0].pregapFrames)) << "\n";
        cue << "    INDEX 01 00:00:00\n";

        // --- 4) Copy every track after the first (audio / extra data) verbatim. ---
        for (size_t i = 1; sheet.ok && i < sheet.tracks.size(); ++i)
        {
            const CueTrack& t = sheet.tracks[i];
            const CueTrackRange& rg = ranges[i];
            const std::string& outBin = finals[i];
            bool copied = false;
            if (rg.length != 0)
            {
                const std::string temp = staging.Add(outBin, r.error);
                if (temp.empty()) return r;
                copied = CopyRange(rg.file, rg.offset, rg.length, temp);
                if (!copied)
                {
                    RemoveFile(temp);
                    staging.outputs.pop_back();
                }
            }
            if (!copied)
            {
                // Fail closed. A BIN/CUE build exists to preserve every non-data track
                // verbatim, so a dropped track is a failed build, not a warning: the cue would
                // otherwise describe a disc that plays without its music and gives no sign
                // anything is missing. Nothing has been published yet, so the previous build
                // (if any) is untouched and the staged files go with 'staging'.
                if (!opt.allowPartialTracks)
                {
                    r.error = "Could not copy track " + Track2(t.number) + " from " + rg.file +
                              ". The disc image would be missing it, so no output was changed. "
                              "Enable partial builds if that is wanted anyway.";
                    return r;
                }
                r.warnings.push_back("Could not copy track " + Track2(t.number) + " from " + rg.file);
                r.partial = true;
                continue;
            }
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
    }

    // --- 5) The .cue, then publish the whole set. ---
    const std::string cueTemp = staging.Add(cuePath, r.error);
    if (cueTemp.empty()) return r;
    if (!WriteText(cueTemp, cue.str())) { r.error = "Could not write cue: " + cuePath; return r; }

    for (const StagedOutput& o : staging.outputs) r.outputs.push_back(o.final);
    if (!staging.Publish(r.error))
    {
        r.outputs.clear();
        return r;
    }
    r.cuePath = cuePath;
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
        std::string text;
        if (!ReadWholeFile(sourceImage, text)) { v.error = "Could not read the source cue."; return v; }
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
