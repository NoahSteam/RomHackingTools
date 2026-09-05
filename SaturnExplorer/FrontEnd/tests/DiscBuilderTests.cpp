// End-to-end test for Build Disc Image. Builds a BIN/CUE from a small Data Directory plus a
// synthetic source disc (data track + one audio track), then proves:
//   - the rebuilt Track 01 is a valid MODE1/2352 raw track (DiscImage opens it, IsoParse reads
//     the files back) — which exercises the sync/header/EDC/ECC layout end to end,
//   - the audio track is copied byte-for-byte,
//   - the emitted .cue mirrors the original track types and (relative) indices.
// Also checks ISO (data-only) output.
#include "Disc/DiscBuilder.h"
#include "Disc/DiscImage.h"
#include "Disc/IsoFs.h"

#include <cstdint>
#include <cstdio>
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

void WriteBytes(const std::string& path, const std::vector<uint8_t>& data)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}
void WriteText(const std::string& path, const std::string& text)
{
    std::ofstream f(path, std::ios::trunc); f << text;
}
std::string ReadText(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
std::vector<uint8_t> ReadBytes(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
const IsoEntry* Find(const IsoFs& fs, const std::string& p)
{
    for (const IsoEntry& e : fs.entries) if (e.path == p) return &e; return nullptr;
}
}  // namespace

int main()
{
    const std::string base = "discbuild_test_tmp";
    const std::string disc = base + "/disc";
    MKDIR(base.c_str()); MKDIR(disc.c_str());
    { std::vector<uint8_t> d(4000, 'A'); WriteBytes(disc + "/MAIN.BIN", d); }
    { std::vector<uint8_t> d(50, 'B'); WriteBytes(disc + "/DATA.DAT", d); }

    // Synthetic source disc: 300 sectors of 2352. Track 2 (audio) occupies frames [225, 300),
    // filled with a recognizable pattern so we can prove it's copied verbatim.
    constexpr uint32_t S = 2352;
    std::vector<uint8_t> src(size_t(300) * S, 0xAA);
    for (size_t i = size_t(225) * S; i < src.size(); ++i) src[i] = uint8_t((i * 13 + 7) & 0xFF);
    WriteBytes(base + "/src.bin", src);
    WriteText(base + "/src.cue",
        "FILE \"src.bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 00 00:03:00\n"     // 225 frames
        "    INDEX 01 00:05:00\n");   // 375 frames

    DiscBuildOptions opt;
    opt.iso.rootDir = disc;
    opt.iso.volumeId = "TESTVOL";
    opt.iso.ipBin.assign(32768, 0x5A);
    opt.sourceImage = base + "/src.cue";
    opt.outPath = base + "/out.cue";
    opt.binCue = true;

    const DiscBuildResult r = BuildDiscImage(opt);
    Check(r.ok, r.ok ? "build ok" : r.error.c_str());
    Check(r.ipBinInjected, "IP.BIN injected");
    Check(r.trackCount == 2, "2 tracks in output");
    Check(r.audioTracksCopied == 1, "1 audio track copied");

    const std::string track01 = base + "/out (Track 01).bin";
    const std::string track02 = base + "/out (Track 02).bin";
    const std::string cue = base + "/out.cue";

    // Track 01 is a valid raw MODE1/2352 disc: DiscImage opens it and IsoParse reads the files.
    DiscImage img;
    Check(img.Open(track01), "open rebuilt Track 01 .bin");
    Check(img.SectorSize() == 2352, "Track 01 is 2352-byte raw sectors");
    IsoFs fs = IsoParse(img.Reader());
    Check(fs.ok, fs.ok ? "parse rebuilt data track" : fs.error.c_str());
    Check(fs.volumeId == "TESTVOL", "volume id in rebuilt track");
    const IsoEntry* mainbin = Find(fs, "/MAIN.BIN");
    Check(mainbin && mainbin->size == 4000, "MAIN.BIN present, size 4000");
    Check(Find(fs, "/DATA.DAT") != nullptr, "DATA.DAT present");
    // Its content survived through the ISO->raw conversion.
    if (mainbin)
    {
        uint8_t s[2048];
        Check(img.ReadSector(mainbin->lba, s) && s[0] == 'A' && s[2047] == 'A', "MAIN.BIN content is 'A'");
    }

    // Track 02 is the source's audio track copied verbatim.
    std::vector<uint8_t> t2 = ReadBytes(track02);
    Check(t2.size() == size_t(75) * S, "Track 02 is 75 sectors");
    bool verbatim = t2.size() == size_t(75) * S;
    for (size_t i = 0; verbatim && i < t2.size(); ++i)
        if (t2[i] != src[size_t(225) * S + i]) verbatim = false;
    Check(verbatim, "Track 02 copied byte-for-byte");

    // The emitted cue mirrors the layout with relative indices.
    const std::string cueText = ReadText(cue);
    Check(Contains(cueText, "TRACK 01 MODE1/2352"), "cue Track 01 MODE1/2352");
    Check(Contains(cueText, "\"out (Track 01).bin\""), "cue references rebuilt bin");
    Check(Contains(cueText, "TRACK 02 AUDIO"), "cue Track 02 AUDIO");
    Check(Contains(cueText, "\"out (Track 02).bin\""), "cue references audio bin");
    Check(Contains(cueText, "INDEX 00 00:00:00") && Contains(cueText, "INDEX 01 00:02:00"),
          "audio track pregap preserved as relative indices");

    // ISO (data-only) output.
    DiscBuildOptions iso = opt;
    iso.binCue = false;
    iso.outPath = base + "/out.iso";
    const DiscBuildResult ri = BuildDiscImage(iso);
    Check(ri.ok, ri.ok ? "iso build ok" : ri.error.c_str());
    DiscImage img2;
    Check(img2.Open(base + "/out.iso") && img2.SectorSize() == 2048, "iso opens as 2048");
    IsoFs fs2 = IsoParse(img2.Reader());
    Check(fs2.ok && Find(fs2, "/MAIN.BIN") != nullptr, "iso data-track parses");

    if (gFail == 0) std::printf("All DiscBuilder tests passed.\n");
    return gFail ? 1 : 0;
}
