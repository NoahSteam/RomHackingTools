// Unit tests for the ISO 9660 parser. Hand-builds a tiny in-memory image (PVD + a root
// directory with a file and a subdirectory, and a file inside the subdirectory) and asserts
// the flattened file list, sizes, and sector->file resolution.
#include "Disc/IsoFs.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace sfe;

namespace
{
constexpr uint32_t kSector = 2048;
int gFail = 0;
void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; } }

void PutLE32(uint8_t* p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
void PutBE32(uint8_t* p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }

// Append one ISO 9660 directory record to 'dir'.
void AppendRec(std::vector<uint8_t>& dir, uint32_t lba, uint32_t size, uint8_t flags,
               const std::string& name)
{
    uint8_t lenFi = (uint8_t)name.size();
    uint32_t lenDR = 33 + lenFi;
    if (lenDR & 1) ++lenDR;   // records are even-length
    const size_t base = dir.size();
    dir.resize(base + lenDR, 0);
    uint8_t* r = dir.data() + base;
    r[0] = (uint8_t)lenDR;
    PutLE32(r + 2, lba);  PutBE32(r + 6, lba);
    PutLE32(r + 10, size); PutBE32(r + 14, size);
    r[25] = flags;
    r[28] = 1; r[31] = 1;   // volume sequence number (LE=1, BE=1)
    r[32] = lenFi;
    std::memcpy(r + 33, name.data(), lenFi);
}

const IsoEntry* Find(const IsoFs& fs, const char* path)
{
    for (const IsoEntry& e : fs.entries) if (e.path == path) return &e;
    return nullptr;
}
}  // namespace

int main()
{
    std::vector<uint8_t> img(20 * kSector, 0);
    auto sector = [&](uint32_t lba) { return img.data() + (size_t)lba * kSector; };

    // --- PVD at LBA 16 ---
    uint8_t* pvd = sector(16);
    pvd[0] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    std::memset(pvd + 40, ' ', 32);
    std::memcpy(pvd + 40, "SATURNTEST", 10);
    // Root directory record (offset 156): root dir at LBA 18, one sector.
    {
        std::vector<uint8_t> rootRec;
        AppendRec(rootRec, 18, kSector, 0x02, std::string(1, '\0'));
        std::memcpy(pvd + 156, rootRec.data(), rootRec.size());
    }

    // --- Root directory at LBA 18 ---
    {
        std::vector<uint8_t> dir;
        AppendRec(dir, 18, kSector, 0x02, std::string(1, '\0'));   // "."
        AppendRec(dir, 18, kSector, 0x02, std::string(1, '\x01')); // ".."
        AppendRec(dir, 30, 5000,    0x00, "TEST.PCM;1");           // a file
        AppendRec(dir, 19, kSector, 0x02, "SOUND");                // a subdirectory
        std::memcpy(sector(18), dir.data(), dir.size());
    }

    // --- SOUND directory at LBA 19 ---
    {
        std::vector<uint8_t> dir;
        AppendRec(dir, 19, kSector, 0x02, std::string(1, '\0'));   // "."
        AppendRec(dir, 18, kSector, 0x02, std::string(1, '\x01')); // ".."
        AppendRec(dir, 40, 100000,  0x00, "BGM01.PCM;1");          // a file in the subdir
        std::memcpy(sector(19), dir.data(), dir.size());
    }

    SectorReader read = [&](uint32_t lba, uint8_t* out) -> bool {
        if (lba >= 20) return false;
        std::memcpy(out, sector(lba), kSector);
        return true;
    };

    IsoFs fs = IsoParse(read);
    Check(fs.ok, "parsed the volume");
    Check(fs.volumeId == "SATURNTEST", "volume id trimmed");

    const IsoEntry* f = Find(fs, "/TEST.PCM");
    Check(f && !f->isDir && f->lba == 30 && f->size == 5000, "root file TEST.PCM");
    const IsoEntry* d = Find(fs, "/SOUND");
    Check(d && d->isDir, "subdirectory SOUND");
    const IsoEntry* g = Find(fs, "/SOUND/BGM01.PCM");
    Check(g && !g->isDir && g->lba == 40 && g->size == 100000, "nested file BGM01.PCM");
    Check(Find(fs, "/.") == nullptr && Find(fs, "/..") == nullptr, "self/parent skipped");

    // Sector -> file resolution. TEST.PCM spans LBA 30..32 (5000 bytes = 3 sectors).
    Check(fs.FileAt(30) && fs.FileAt(30)->path == "/TEST.PCM", "FileAt start sector");
    Check(fs.FileAt(32) && fs.FileAt(32)->path == "/TEST.PCM", "FileAt last sector");
    Check(fs.FileAt(33) == nullptr, "FileAt past a file's extent");
    Check(fs.FileAt(40) && fs.FileAt(40)->path == "/SOUND/BGM01.PCM", "FileAt nested file");
    Check(fs.FileAt(88) && fs.FileAt(88)->path == "/SOUND/BGM01.PCM", "FileAt within big file");
    Check(fs.FileAt(500) == nullptr, "FileAt unmapped sector");

    // A non-ISO image reports a clean failure rather than crashing.
    IsoFs bad = IsoParse([](uint32_t, uint8_t* out) { std::memset(out, 0, kSector); return true; });
    Check(!bad.ok && !bad.error.empty(), "non-ISO image fails cleanly");

    if (gFail == 0) std::printf("All IsoFs tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
