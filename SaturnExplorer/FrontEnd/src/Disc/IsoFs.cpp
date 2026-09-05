#include "Disc/IsoFs.h"

#include <cstring>
#include <deque>

namespace sfe
{
namespace
{
constexpr uint32_t kSector = 2048;

uint32_t Le32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Sectors an extent of 'bytes' spans.
uint32_t SectorSpan(uint32_t bytes) { return (bytes + kSector - 1) / kSector; }

// ISO file identifier -> readable name: strip the ";version" suffix; keep the (upper-case)
// 8.3 name as stored. '.' / '..' records use single 0x00 / 0x01 bytes (handled by the caller).
std::string CleanName(const uint8_t* id, int len)
{
    std::string s(reinterpret_cast<const char*>(id), (size_t)len);
    const size_t semi = s.find(';');
    if (semi != std::string::npos) s.erase(semi);
    // A trailing '.' is common on extension-less names ("FILE." -> "FILE").
    while (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}
}  // namespace

const IsoEntry* IsoFs::FileAt(uint32_t lba) const
{
    for (const IsoEntry& e : entries)
    {
        if (e.isDir) continue;
        const uint32_t span = e.size ? SectorSpan(e.size) : 1;
        if (lba >= e.lba && lba < e.lba + span) return &e;
    }
    return nullptr;
}

IsoFs IsoParse(const SectorReader& read)
{
    IsoFs fs;
    if (!read) { fs.error = "no sector reader"; return fs; }

    // Find the Primary Volume Descriptor. The descriptor set starts at LBA 16 and ends at a
    // terminator (type 255); scan a bounded window for a type-1 "CD001" descriptor.
    uint8_t pvd[kSector];
    bool found = false;
    for (uint32_t lba = 16; lba < 16 + 8 && !found; ++lba)
    {
        if (!read(lba, pvd)) break;
        if (std::memcmp(pvd + 1, "CD001", 5) != 0) continue;
        if (pvd[0] == 1) found = true;             // primary
        else if (pvd[0] == 255) break;             // terminator
    }
    if (!found) { fs.error = "no ISO 9660 primary volume descriptor (not a data disc image?)"; return fs; }

    // PVD text identifiers, each trailing-trimmed. System (32 @ 8), volume (32 @ 40), and the
    // 128-char publisher / preparer / application ids (@ 318 / 446 / 574).
    auto trimmed = [&](int off, int width) {
        std::string s(reinterpret_cast<const char*>(pvd + off), size_t(width));
        while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
        return s;
    };
    fs.systemId      = trimmed(8, 32);
    fs.volumeId      = trimmed(40, 32);
    fs.publisherId   = trimmed(318, 128);
    fs.preparerId    = trimmed(446, 128);
    fs.applicationId = trimmed(574, 128);

    // Root directory record lives at offset 156 of the PVD.
    const uint8_t* root = pvd + 156;
    const uint32_t rootLba = Le32(root + 2);
    const uint32_t rootSize = Le32(root + 10);

    // Walk the tree breadth-first. Each queued item is a directory to expand.
    struct Dir { uint32_t lba, size; std::string prefix; int depth; };
    std::deque<Dir> queue;
    queue.push_back({ rootLba, rootSize, std::string(), 0 });

    constexpr int      kMaxDepth   = 32;
    constexpr size_t   kMaxEntries = 200000;
    constexpr uint32_t kMaxDirSect = 2048;   // cap a single directory's extent

    while (!queue.empty() && fs.entries.size() < kMaxEntries)
    {
        const Dir d = queue.front();
        queue.pop_front();
        const uint32_t sectors = d.size ? SectorSpan(d.size) : 1;

        for (uint32_t s = 0; s < sectors && s < kMaxDirSect; ++s)
        {
            uint8_t buf[kSector];
            if (!read(d.lba + s, buf)) break;

            uint32_t off = 0;
            while (off + 33 <= kSector)
            {
                const uint8_t lenDR = buf[off];
                if (lenDR == 0) break;                       // rest of the sector is padding
                if (off + lenDR > kSector || lenDR < 34) break;   // malformed record

                const uint8_t* rec = buf + off;
                const uint32_t exLba = Le32(rec + 2);
                const uint32_t exSize = Le32(rec + 10);
                const uint8_t  flags = rec[25];
                const uint8_t  nameLen = rec[32];
                off += lenDR;

                if (nameLen == 0 || off - lenDR + 33 + nameLen > kSector) continue;
                // Skip the "." (0x00) and ".." (0x01) self/parent records.
                if (nameLen == 1 && (rec[33] == 0x00 || rec[33] == 0x01)) continue;

                const bool isDir = (flags & 0x02) != 0;
                const std::string name = CleanName(rec + 33, nameLen);
                if (name.empty()) continue;

                IsoEntry e;
                e.path = d.prefix + "/" + name;
                e.lba = exLba;
                e.size = isDir ? 0 : exSize;
                e.isDir = isDir;
                fs.entries.push_back(e);

                if (isDir && d.depth + 1 < kMaxDepth)
                    queue.push_back({ exLba, exSize, e.path, d.depth + 1 });
            }
        }
    }

    fs.ok = true;
    return fs;
}

}  // namespace sfe
