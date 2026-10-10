// Round-trip tests for the ISO builder: write a small directory tree to a real .iso with
// IsoBuild, then read it back through DiscImage + IsoParse (the shipping reader) and assert the
// files, sizes, directory structure, skip rules, volume id, and injected IP.BIN all survive.
#include "Disc/IsoBuilder.h"
#include "Disc/DiscImage.h"
#include "Disc/IsoFs.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) ::_mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) ::mkdir(p, 0777)
#endif

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; } }

void WriteFile(const std::string& path, char fill, uint32_t size)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    std::vector<char> data(size, fill);
    f.write(data.data(), std::streamsize(size));
}

const IsoEntry* Find(const IsoFs& fs, const std::string& path)
{
    for (const IsoEntry& e : fs.entries) if (e.path == path) return &e;
    return nullptr;
}
}  // namespace

int main()
{
    // A unique-ish working area under the test's CWD (the build dir).
    const std::string base = "isobuild_test_tmp";
    const std::string disc = base + "/disc";
    const std::string sound = disc + "/SOUND";
    MKDIR(base.c_str());
    MKDIR(disc.c_str());
    MKDIR(sound.c_str());

    WriteFile(disc + "/0.BIN", 'A', 3000);            // spans two sectors
    WriteFile(disc + "/GAME.DAT", 'B', 100);
    WriteFile(disc + "/EMPTY.BIN", 'C', 0);           // zero-length file
    WriteFile(sound + "/BGM01.PCM", 'D', 5000);
    WriteFile(disc + "/leftover.iso", 'X', 4096);     // must be skipped (skipExtensions)
    WriteFile(disc + "/se_patch.py", 'Y', 10);        // must be skipped (skipNames)

    IsoBuildOptions opt;
    opt.rootDir = disc;
    opt.outIso = base + "/out.iso";
    opt.volumeId = "TESTVOL";
    opt.systemId = "SEGA SEGASATURN";
    opt.publisherId = "SEGA ENTERPRISES";
    opt.preparerId = "SATURN EXPLORER";
    opt.applicationId = "TESTAPP";
    // leftover.iso and se_patch.py are dropped by the builder's built-in skip defaults; the
    // game's own *.BIN files are kept. No caller skip lists needed.
    opt.ipBin.assign(32768, 0);
    for (size_t i = 0; i < opt.ipBin.size(); ++i) opt.ipBin[i] = uint8_t(0x40 + (i & 0x1F));

    const IsoBuildResult r = IsoBuild(opt);
    Check(r.ok, r.ok ? "build ok" : r.error.c_str());
    Check(r.ipBinInjected, "IP.BIN injected");
    Check(r.fileCount == 4, "file count == 4 (skips excluded)");
    Check(r.dirCount == 1, "dir count == 1 (SOUND)");

    // Read the image back through the shipping reader.
    DiscImage img;
    Check(img.Open(opt.outIso), "reopen built image");
    Check(img.SectorSize() == 2048, "image is 2048-byte sectors");
    IsoFs fs = IsoParse(img.Reader());
    Check(fs.ok, fs.ok ? "parse ok" : fs.error.c_str());
    Check(fs.complete, fs.complete ? "parse complete" : fs.incomplete.c_str());
    Check(fs.volumeId == "TESTVOL", "volume id preserved");
    Check(fs.systemId == "SEGA SEGASATURN", "system id preserved");
    Check(fs.publisherId == "SEGA ENTERPRISES", "publisher id preserved");
    Check(fs.preparerId == "SATURN EXPLORER", "preparer id preserved");
    Check(fs.applicationId == "TESTAPP", "application id preserved");

    const IsoEntry* bin = Find(fs, "/0.BIN");
    const IsoEntry* dat = Find(fs, "/GAME.DAT");
    const IsoEntry* empty = Find(fs, "/EMPTY.BIN");
    const IsoEntry* pcm = Find(fs, "/SOUND/BGM01.PCM");
    Check(bin && bin->size == 3000, "0.BIN present, size 3000");
    Check(dat && dat->size == 100, "GAME.DAT present, size 100");
    Check(empty && empty->size == 0, "EMPTY.BIN present, size 0");
    Check(pcm && pcm->size == 5000, "SOUND/BGM01.PCM present, size 5000");
    Check(Find(fs, "/SOUND") != nullptr, "SOUND directory present");
    Check(Find(fs, "/leftover.iso") == nullptr, "leftover.iso skipped");
    Check(Find(fs, "/SE_PATCH.PY") == nullptr && Find(fs, "/se_patch.py") == nullptr,
          "se_patch.py skipped");

    // File content survives: the first sector of 0.BIN should be all 'A'.
    if (bin)
    {
        uint8_t s[2048];
        Check(img.ReadSector(bin->lba, s), "read 0.BIN first sector");
        bool allA = true;
        for (int i = 0; i < 2048; ++i) if (s[i] != 'A') { allA = false; break; }
        Check(allA, "0.BIN content is 'A'");
    }
    // sector -> file resolution works through the rebuilt filesystem.
    if (pcm) Check(fs.FileAt(pcm->lba) == pcm || (fs.FileAt(pcm->lba) &&
                   fs.FileAt(pcm->lba)->path == "/SOUND/BGM01.PCM"), "FileAt resolves BGM01");

    // IP.BIN survives verbatim in the 32 KB system area.
    {
        std::ifstream in(opt.outIso, std::ios::binary);
        std::vector<char> sys(32768);
        in.read(sys.data(), 32768);
        bool match = in.gcount() == 32768;
        for (size_t i = 0; match && i < 32768; ++i)
            if (uint8_t(sys[i]) != opt.ipBin[i]) match = false;
        Check(match, "IP.BIN system area matches");
    }

    // --- DISC-02: collision suffixes must fit inside the Level-1 widths. ---
    //
    // Long names that differ past the eighth character collapse to the same 8.3 identifier, which
    // is exactly when the de-duplicator runs. It used to insert "_N" before the ";1", producing
    // LONGNAME.BIN_1;1 -- a five-character extension -- and for a directory a ten-character name.
    // Both are outside Level 1, which is what the Saturn's own filesystem code reads.
    {
        const std::string d2 = base + "/disc2";
        MKDIR(d2.c_str());
        WriteFile(d2 + "/LONGNAME1.BIN", 'A', 16);
        WriteFile(d2 + "/LONGNAME2.BIN", 'B', 16);
        WriteFile(d2 + "/LONGNAME3.BIN", 'C', 16);
        const std::string sub1 = d2 + "/DIRECTORY_ONE";
        const std::string sub2 = d2 + "/DIRECTORY_TWO";
        MKDIR(sub1.c_str());
        MKDIR(sub2.c_str());
        WriteFile(sub1 + "/A.BIN", 'D', 16);
        WriteFile(sub2 + "/B.BIN", 'E', 16);

        IsoBuildOptions o2;
        o2.rootDir = d2;
        o2.outIso = base + "/dedupe.iso";
        const IsoBuildResult r2 = IsoBuild(o2);
        Check(r2.ok, "dedupe image builds");
        Check(r2.renamedForIso >= 3, "the colliding names were renamed");

        DiscImage img2;
        Check(img2.Open(o2.outIso), "open dedupe image");
        const IsoFs fs2 = IsoParse(img2.Reader());

        // Every identifier the image carries has to satisfy 8.3 (and 8 for a directory). The
        // reader strips ";1" and any trailing '.', so check the parsed component widths.
        int files = 0, dirsSeen = 0;
        for (const IsoEntry& e : fs2.entries)
        {
            const size_t slash = e.path.find_last_of('/');
            const std::string leaf = slash == std::string::npos ? e.path : e.path.substr(slash + 1);
            const size_t dot = leaf.find_last_of('.');
            if (e.isDir)
            {
                ++dirsSeen;
                Check(leaf.size() <= 8, ("directory name is 8 or fewer chars: " + leaf).c_str());
            }
            else
            {
                ++files;
                const std::string stem = dot == std::string::npos ? leaf : leaf.substr(0, dot);
                const std::string ext = dot == std::string::npos ? "" : leaf.substr(dot + 1);
                Check(stem.size() <= 8, ("file stem is 8 or fewer chars: " + leaf).c_str());
                Check(ext.size() <= 3, ("file extension is 3 or fewer chars: " + leaf).c_str());
            }
        }
        Check(files == 5, "all five files are present after renaming");
        Check(dirsSeen == 2, "both directories are present after renaming");

        // And the names are still distinct -- the point of the exercise.
        for (size_t i = 0; i < fs2.entries.size(); ++i)
            for (size_t j = i + 1; j < fs2.entries.size(); ++j)
                Check(fs2.entries[i].path != fs2.entries[j].path, "no duplicate paths remain");
    }

    // Path tables name each directory's PARENT, not the directory itself. A root-level SUB
    // directory has parent 1 (root); a directory inside it has parent 2.
    {
        const std::string pbase = "isobuild_ptable_tmp";
        MKDIR(pbase.c_str());
        MKDIR((pbase + "/disc").c_str());
        MKDIR((pbase + "/disc/SUB").c_str());
        MKDIR((pbase + "/disc/SUB/INNER").c_str());
        WriteFile(pbase + "/disc/SUB/INNER/X.BIN", 'Z', 10);
        IsoBuildOptions po;
        po.rootDir = pbase + "/disc";
        po.outIso = pbase + "/out.iso";
        po.ipBin.assign(32768, 0);
        const IsoBuildResult pr = IsoBuild(po);
        Check(pr.ok, pr.ok ? "path-table build ok" : pr.error.c_str());
        std::ifstream f(po.outIso, std::ios::binary);
        std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto le32 = [&](size_t o) { return uint32_t(img[o]) | uint32_t(img[o+1]) << 8 | uint32_t(img[o+2]) << 16 | uint32_t(img[o+3]) << 24; };
        auto le16 = [&](size_t o) { return uint32_t(img[o]) | uint32_t(img[o+1]) << 8; };
        const size_t pvd = size_t(16) * 2048;
        const uint32_t lPath = le32(pvd + 140);
        Check(img.size() > pvd + 2048 && lPath != 0, "PVD names an L path table");
        if (img.size() > pvd + 2048 && lPath != 0)
        {
            size_t p = size_t(lPath) * 2048;
            uint32_t parents[3] = { 0, 0, 0 };
            for (int i = 0; i < 3; ++i)
            {
                const uint32_t idLen = img[p];
                parents[i] = le16(p + 6);
                p += 8 + idLen + (idLen & 1);
            }
            Check(parents[0] == 1, "root's parent is itself (1)");
            Check(parents[1] == 1, "SUB's parent is root (1), not its own number");
            Check(parents[2] == 2, "INNER's parent is SUB (2)");
        }
    }

    // Nothing under the root is left off the disc silently: a tree deeper than the scan limit,
    // or an entry that is neither a file nor a directory, fails the build and names it.
    {
        const std::string dbase = "isobuild_depth_test_tmp";
        MKDIR(dbase.c_str());
        std::string dir = dbase + "/disc";
        MKDIR(dir.c_str());
        const std::string root = dir;
        for (int i = 0; i < 26; ++i) { dir += "/D"; MKDIR(dir.c_str()); }
        WriteFile(dir + "/DEEP.BIN", 'Q', 8);
        IsoBuildOptions o;
        o.rootDir = root;
        o.outIso = dbase + "/deep.iso";
        const IsoBuildResult r = IsoBuild(o);
        Check(!r.ok, "a file below the depth limit fails the build instead of vanishing");
        Check(r.error.find("deeper") != std::string::npos, "the error names the depth limit");

        // Exactly at the limit still builds.
        const std::string sbase = "isobuild_depthok_test_tmp";
        MKDIR(sbase.c_str());
        std::string sdir = sbase + "/disc";
        MKDIR(sdir.c_str());
        const std::string sroot = sdir;
        for (int i = 0; i < 24; ++i) { sdir += "/D"; MKDIR(sdir.c_str()); }
        WriteFile(sdir + "/DEEP.BIN", 'Q', 8);
        o.rootDir = sroot;
        o.outIso = sbase + "/ok.iso";
        const IsoBuildResult r2 = IsoBuild(o);
        Check(r2.ok && r2.fileCount == 1, r2.ok ? "a tree at the depth limit builds" : r2.error.c_str());

        o.rootDir = dbase + "/does_not_exist";
        o.outIso = dbase + "/missing.iso";
        Check(!IsoBuild(o).ok, "a root that cannot be listed fails the build");

#ifndef _WIN32
        const std::string fbase = "isobuild_fifo_test_tmp";
        MKDIR(fbase.c_str());
        MKDIR((fbase + "/disc").c_str());
        WriteFile(fbase + "/disc/A.BIN", 'A', 8);
        ::mkfifo((fbase + "/disc/PIPE").c_str(), 0600);
        o.rootDir = fbase + "/disc";
        o.outIso = fbase + "/fifo.iso";
        const IsoBuildResult rf = IsoBuild(o);
        Check(!rf.ok && rf.error.find("PIPE") != std::string::npos,
              "an entry that is not a regular file is reported, not skipped");

        // A cue that cannot be written is a failed build, not a silent omission.
        const std::string cbase = "isobuild_cue_test_tmp";
        MKDIR(cbase.c_str());
        MKDIR((cbase + "/blocked.cue").c_str());   // a directory where the cue goes
        IsoBuildOptions oc;
        oc.rootDir = disc;
        oc.outIso = cbase + "/blocked.iso";
        Check(!IsoBuild(oc).ok, "an unwritable cue fails the build");
#endif
    }

    // Empty files do not share an LBA with a file that has data: the reader resolves the data
    // file's sector to the data file.
    {
        const std::string ebase = "isobuild_empty_test_tmp";
        MKDIR(ebase.c_str());
        MKDIR((ebase + "/disc").c_str());
        WriteFile(ebase + "/disc/AEMPTY.BIN", 'E', 0);
        WriteFile(ebase + "/disc/BFILE.BIN", 'F', 7);
        IsoBuildOptions eo;
        eo.rootDir = ebase + "/disc";
        eo.outIso = ebase + "/empty.iso";
        Check(IsoBuild(eo).ok, "empty-file image builds");
        DiscImage ei;
        Check(ei.Open(eo.outIso), "open empty-file image");
        const IsoFs efs = IsoParse(ei.Reader());
        const IsoEntry* b = Find(efs, "/BFILE.BIN");
        Check(b && efs.FileAt(b->lba) == b, "the data file's sector resolves to the data file");
    }

    if (gFail == 0) std::printf("All IsoBuilder tests passed.\n");
    return gFail ? 1 : 0;
}
