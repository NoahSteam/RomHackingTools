// Round-trip tests for the ISO builder: write a small directory tree to a real .iso with
// IsoBuild, then read it back through DiscImage + IsoParse (the shipping reader) and assert the
// files, sizes, directory structure, skip rules, volume id, and injected IP.BIN all survive.
#include "Disc/IsoBuilder.h"
#include "Disc/DiscImage.h"
#include "Disc/IsoFs.h"

#include <cstdio>
#include <cstring>
#include <fstream>
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
    Check(fs.volumeId == "TESTVOL", "volume id preserved");

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

    if (gFail == 0) std::printf("All IsoBuilder tests passed.\n");
    return gFail ? 1 : 0;
}
