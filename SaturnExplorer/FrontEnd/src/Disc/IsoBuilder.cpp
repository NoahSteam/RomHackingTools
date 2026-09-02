#include "Disc/IsoBuilder.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace sfe
{
namespace
{
constexpr uint32_t kSector   = 2048;
constexpr uint32_t kSysAreaSectors = 16;     // 32 KB Saturn boot header (IP.BIN)
constexpr uint32_t kPvdLba   = 16;

// --- little-endian / both-endian field writers (ISO 9660 stores many fields both ways) ---
void Put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void Put32(uint8_t* p, uint32_t v)
{
    p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24);
}
void PutBoth16(uint8_t* p, uint16_t v) { Put16(p, v); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); }
void PutBoth32(uint8_t* p, uint32_t v)
{
    Put32(p, v);
    p[4] = uint8_t(v >> 24); p[5] = uint8_t(v >> 16); p[6] = uint8_t(v >> 8); p[7] = uint8_t(v);
}

uint32_t SectorSpan(uint64_t bytes) { return uint32_t((bytes + kSector - 1) / kSector); }
uint32_t RoundUpSector(uint32_t off) { return (off + kSector - 1) / kSector * kSector; }

// One directory-record's byte length for an identifier of 'nameLen' bytes: a 33-byte fixed head
// plus the name, padded to an even total (ISO requires records to be even-length).
uint32_t DirRecordLen(uint32_t nameLen)
{
    uint32_t len = 33 + nameLen;
    if (len & 1) ++len;
    return len;
}

// --- ISO 8.3 name mapping. d-characters are A-Z 0-9 and '_'; everything else maps to '_'. ---
char DChar(char c)
{
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    return ok ? c : '_';
}

// Map a file name to a Level-1 identifier "NAME.EXT;1" (8.3, upper-case). IsoParse's CleanName
// strips the ";1" and a trailing '.', so this round-trips to the original 8.3 name.
std::string FileIdentifier(const std::string& name)
{
    const size_t dot = name.find_last_of('.');
    std::string base = dot == std::string::npos ? name : name.substr(0, dot);
    std::string ext  = dot == std::string::npos ? std::string() : name.substr(dot + 1);
    if (base.size() > 8) base.resize(8);
    if (ext.size() > 3)  ext.resize(3);
    std::string id;
    for (char c : base) id += DChar(c);
    id += '.';
    for (char c : ext) id += DChar(c);
    id += ";1";
    return id;
}

// Map a directory name to a Level-1 identifier (up to 8 d-characters, no extension/version).
std::string DirIdentifier(const std::string& name)
{
    std::string id;
    for (char c : name) { if (id.size() >= 8) break; id += DChar(c); }
    if (id.empty()) id = "_";
    return id;
}

bool IEqualsExt(const std::string& name, const std::string& ext)
{
    if (name.size() < ext.size()) return false;
    for (size_t i = 0; i < ext.size(); ++i)
    {
        char a = name[name.size() - ext.size() + i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}
bool IEquals(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && IEqualsExt(a, b);
}

struct File { std::string identifier; std::string diskPath; uint32_t size; uint32_t lba; };
struct Dir
{
    std::string identifier;          // "" for the root
    std::string diskPath;
    int         parent;              // index into dirs; root's parent is itself (0)
    int         number = 0;          // 1-based path-table number (BFS order)
    std::vector<int>  subdirs;       // child directory indices
    std::vector<File> files;
    uint32_t    lba = 0;
    uint32_t    sizeBytes = 0;
    uint32_t    sectors = 0;
};

#ifdef _WIN32
bool StatSize(const std::string& path, uint32_t& outSize)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!::GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad)) return false;
    if (fad.nFileSizeHigh != 0) return false;   // > 4 GiB won't fit an ISO extent field
    outSize = fad.nFileSizeLow;
    return true;
}
#else
bool StatSize(const std::string& path, uint32_t& outSize)
{
    struct stat st;
    if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if (st.st_size > 0xFFFFFFFFll) return false;
    outSize = uint32_t(st.st_size);
    return true;
}
#endif

// Skip predicate: the output image, disc images, project files, and the caller's skip lists.
bool ShouldSkip(const std::string& name, const IsoBuildOptions& o)
{
    for (const std::string& n : o.skipNames)      if (IEquals(name, n)) return true;
    for (const std::string& e : o.skipExtensions) if (IEqualsExt(name, e)) return true;
    return false;
}

// Recursively scan 'diskPath' into dirs[dirIndex]. Enumerates children (Win32 / POSIX), applying
// the skip lists, and recurses into subdirectories. Bounded so a runaway tree can't hang.
void ScanDir(std::vector<Dir>& dirs, int dirIndex, const IsoBuildOptions& o,
             uint32_t& fileCount, int depth)
{
    if (depth > 24) return;
    const std::string base = dirs[dirIndex].diskPath;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = ::FindFirstFileA((base + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        const std::string full = base + "\\" + name;
        const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    DIR* d = ::opendir(base.c_str());
    if (!d) return;
    while (struct dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        const std::string full = base + "/" + name;
        struct stat st;
        if (::stat(full.c_str(), &st) != 0) continue;
        const bool isDir = S_ISDIR(st.st_mode);
#endif
        if (name.empty() || name[0] == '.') continue;   // skip dotfiles
        if (isDir)
        {
            Dir sub;
            sub.identifier = DirIdentifier(name);
            sub.diskPath = full;
            sub.parent = dirIndex;
            const int idx = int(dirs.size());
            dirs.push_back(sub);
            dirs[dirIndex].subdirs.push_back(idx);
            ScanDir(dirs, idx, o, fileCount, depth + 1);
        }
        else if (!ShouldSkip(name, o))
        {
            uint32_t size = 0;
            if (StatSize(full, size))
            {
                File f; f.identifier = FileIdentifier(name); f.diskPath = full; f.size = size; f.lba = 0;
                dirs[dirIndex].files.push_back(f);
                ++fileCount;
            }
        }
#ifdef _WIN32
    } while (::FindNextFileA(h, &fd));
    ::FindClose(h);
#else
    }
    ::closedir(d);
#endif
}

std::string CuePathFor(const std::string& iso)
{
    const size_t dot = iso.find_last_of('.');
    const size_t slash = iso.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        return iso.substr(0, dot) + ".cue";
    return iso + ".cue";
}
std::string BaseName(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
}  // namespace

IsoBuildResult IsoBuild(const IsoBuildOptions& o)
{
    IsoBuildResult r;
    r.isoPath = o.outIso;
    r.cuePath = CuePathFor(o.outIso);

    // 1) Scan the tree. dirs[0] is the root.
    std::vector<Dir> dirs;
    Dir root; root.identifier = ""; root.diskPath = o.rootDir; root.parent = 0;
    dirs.push_back(root);
    uint32_t fileCount = 0;
    ScanDir(dirs, 0, o, fileCount, 0);
    r.dirCount = uint32_t(dirs.size() - 1);
    r.fileCount = fileCount;

    // 2) Assign path-table numbers in breadth-first order (root = 1). Also de-duplicate colliding
    //    identifiers within each directory (8.3 mapping can, rarely, collide) so the image is valid.
    std::vector<int> bfs;
    bfs.push_back(0);
    for (size_t i = 0; i < bfs.size(); ++i)
    {
        Dir& d = dirs[bfs[i]];
        d.number = int(i + 1);
        for (int s : d.subdirs) bfs.push_back(s);
    }
    auto dedupe = [&](std::vector<std::string>& ids) {
        for (size_t i = 0; i < ids.size(); ++i)
            for (size_t j = 0; j < i; ++j)
                if (IEquals(ids[i], ids[j]))
                {
                    // Insert a numeric tag before the ";1" (files) or at the end (dirs).
                    std::string tag = "_" + std::to_string(int(i));
                    const size_t semi = ids[i].find(';');
                    if (semi != std::string::npos) ids[i].insert(semi, tag); else ids[i] += tag;
                    ++r.renamedForIso;
                    r.warnings.push_back("Renamed duplicate ISO name to " + ids[i]);
                    j = size_t(-1);   // recheck against all
                }
    };
    for (Dir& d : dirs)
    {
        std::vector<std::string> fileIds, dirIds;
        for (const File& f : d.files) fileIds.push_back(f.identifier);
        for (int s : d.subdirs) dirIds.push_back(dirs[s].identifier);
        dedupe(fileIds);
        dedupe(dirIds);
        for (size_t i = 0; i < d.files.size(); ++i) d.files[i].identifier = fileIds[i];
        for (size_t i = 0; i < d.subdirs.size(); ++i) dirs[d.subdirs[i]].identifier = dirIds[i];
    }

    // 3) Sort each directory's children (subdirs + files) together by identifier, then compute the
    //    directory extent size: records for '.', '..', and each child, packed without a record
    //    crossing a 2048-byte boundary.
    struct Child { bool isDir; int index; std::string id; };
    std::vector<std::vector<Child>> children(dirs.size());
    for (size_t di = 0; di < dirs.size(); ++di)
    {
        std::vector<Child>& cs = children[di];
        for (int s : dirs[di].subdirs) cs.push_back({ true, s, dirs[s].identifier });
        for (size_t fi = 0; fi < dirs[di].files.size(); ++fi)
            cs.push_back({ false, int(fi), dirs[di].files[fi].identifier });
        std::sort(cs.begin(), cs.end(), [](const Child& a, const Child& b){ return a.id < b.id; });

        uint32_t off = DirRecordLen(1) + DirRecordLen(1);   // '.' and '..'
        for (const Child& c : cs)
        {
            const uint32_t len = DirRecordLen(uint32_t(c.id.size()));
            if ((off % kSector) + len > kSector) off = RoundUpSector(off);
            off += len;
        }
        dirs[di].sizeBytes = std::max<uint32_t>(RoundUpSector(off), kSector);
        dirs[di].sectors = dirs[di].sizeBytes / kSector;
    }

    // 4) Path table size: one record per directory (both L and M tables are this many bytes).
    uint32_t pathTableBytes = 0;
    for (const Dir& d : dirs)
    {
        const uint32_t idLen = d.identifier.empty() ? 1 : uint32_t(d.identifier.size());
        pathTableBytes += 8 + idLen + (idLen & 1);
    }
    const uint32_t pathTableSectors = std::max<uint32_t>(SectorSpan(pathTableBytes), 1);

    // 5) Assign LBAs: system area, PVD, terminator, L/M path tables, directory extents, file data.
    uint32_t lba = kPvdLba + 2;                 // after PVD (16) and terminator (17)
    const uint32_t lPathLba = lba; lba += pathTableSectors;
    const uint32_t mPathLba = lba; lba += pathTableSectors;
    for (int di : bfs) { dirs[di].lba = lba; lba += dirs[di].sectors; }
    const uint32_t firstFileLba = lba;
    for (int di : bfs)
        for (File& f : dirs[di].files) { f.lba = lba; lba += SectorSpan(f.size); }
    const uint32_t totalSectors = std::max(lba, firstFileLba);

    // 6) Build the header region (everything before the first file) in memory — small even for a
    //    large disc (system area + descriptors + path tables + directory extents).
    std::vector<uint8_t> head(size_t(firstFileLba) * kSector, 0);
    auto sec = [&](uint32_t l) -> uint8_t* { return head.data() + size_t(l) * kSector; };

    // System area: the Saturn boot header (IP.BIN).
    if (!o.ipBin.empty())
    {
        const size_t n = std::min<size_t>(o.ipBin.size(), size_t(kSysAreaSectors) * kSector);
        std::memcpy(head.data(), o.ipBin.data(), n);
        r.ipBinInjected = true;
    }

    // Write one directory record into 'p'; returns its length. name/nameLen carries 0x00/0x01 for
    // '.'/'..'. isDir sets the directory flag.
    auto writeRecord = [](uint8_t* p, uint32_t extentLba, uint32_t dataLen, bool isDir,
                          const char* name, uint32_t nameLen) -> uint32_t {
        const uint32_t len = DirRecordLen(nameLen);
        std::memset(p, 0, len);
        p[0] = uint8_t(len);
        PutBoth32(p + 2, extentLba);
        PutBoth32(p + 10, dataLen);
        // p[18..24] recording date/time left zero (unspecified).
        p[25] = isDir ? 0x02 : 0x00;                 // file flags
        PutBoth16(p + 28, 1);                        // volume sequence number
        p[32] = uint8_t(nameLen);
        std::memcpy(p + 33, name, nameLen);
        return len;
    };

    // Directory extents.
    for (size_t di = 0; di < dirs.size(); ++di)
    {
        const Dir& d = dirs[di];
        uint8_t* base = sec(d.lba);
        uint32_t off = 0;
        const char dot = 0x00, dotdot = 0x01;
        off += writeRecord(base + off, d.lba, d.sizeBytes, true, &dot, 1);
        const Dir& parent = dirs[d.parent];
        off += writeRecord(base + off, parent.lba, parent.sizeBytes, true, &dotdot, 1);
        for (const Child& c : children[di])
        {
            const uint32_t len = DirRecordLen(uint32_t(c.id.size()));
            if ((off % kSector) + len > kSector) off = RoundUpSector(off);
            if (c.isDir)
            {
                const Dir& s = dirs[c.index];
                off += writeRecord(base + off, s.lba, s.sizeBytes, true,
                                   c.id.data(), uint32_t(c.id.size()));
            }
            else
            {
                const File& f = d.files[c.index];
                off += writeRecord(base + off, f.lba, f.size, false,
                                   c.id.data(), uint32_t(c.id.size()));
            }
        }
    }

    // Path tables (L: little-endian extent + parent; M: big-endian).
    auto writePathTable = [&](uint32_t tableLba, bool bigEndian) {
        uint8_t* p = sec(tableLba);
        for (int di : bfs)
        {
            const Dir& d = dirs[di];
            const std::string id = d.identifier.empty() ? std::string(1, '\0') : d.identifier;
            const uint32_t idLen = uint32_t(id.size());
            p[0] = uint8_t(idLen);
            p[1] = 0;                                // extended attr length
            if (bigEndian)
            {
                p[2] = uint8_t(d.lba >> 24); p[3] = uint8_t(d.lba >> 16);
                p[4] = uint8_t(d.lba >> 8);  p[5] = uint8_t(d.lba);
                p[6] = uint8_t(d.number >> 8); p[7] = uint8_t(d.number);
            }
            else
            {
                Put32(p + 2, d.lba);
                Put16(p + 6, uint16_t(d.number));
            }
            std::memcpy(p + 8, id.data(), idLen);
            p += 8 + idLen + (idLen & 1);
        }
    };
    writePathTable(lPathLba, false);
    writePathTable(mPathLba, true);

    // Primary Volume Descriptor (sector 16) and the descriptor-set terminator (sector 17).
    {
        uint8_t* p = sec(kPvdLba);
        p[0] = 1;                                    // primary
        std::memcpy(p + 1, "CD001", 5);
        p[6] = 1;                                    // version
        auto field = [&](int off, int width, const std::string& s, char pad) {
            std::memset(p + off, pad, width);
            std::memcpy(p + off, s.data(), std::min<size_t>(s.size(), size_t(width)));
        };
        field(8, 32, o.systemId, ' ');               // system identifier
        field(40, 32, o.volumeId, ' ');              // volume identifier
        PutBoth32(p + 80, totalSectors);             // volume space size (sectors)
        PutBoth16(p + 120, 1);                       // volume set size
        PutBoth16(p + 124, 1);                       // volume sequence number
        PutBoth16(p + 128, uint16_t(kSector));       // logical block size
        PutBoth32(p + 132, pathTableBytes);          // path table size
        Put32(p + 140, lPathLba);                    // type-L path table location (LE)
        // p[144..147] optional L path table = 0
        p[148] = uint8_t(mPathLba >> 24); p[149] = uint8_t(mPathLba >> 16);
        p[150] = uint8_t(mPathLba >> 8);  p[151] = uint8_t(mPathLba);   // type-M (BE)
        // p[152..155] optional M path table = 0
        writeRecord(p + 156, dirs[0].lba, dirs[0].sizeBytes, true, "\0", 1);  // root dir record
        field(190, 128, "", ' ');                    // volume set identifier
        field(318, 128, "", ' ');                    // publisher
        field(446, 128, "", ' ');                    // data preparer
        field(574, 128, "", ' ');                    // application
        field(702, 37, "", ' ');                     // copyright file id
        field(739, 37, "", ' ');                     // abstract file id
        field(776, 37, "", ' ');                     // bibliographic file id
        static const char kNoDate[17] = { '0','0','0','0','0','0','0','0',
                                          '0','0','0','0','0','0','0','0', 0 };
        std::memcpy(p + 813, kNoDate, 17);           // creation
        std::memcpy(p + 830, kNoDate, 17);           // modification
        std::memcpy(p + 847, kNoDate, 17);           // expiration
        std::memcpy(p + 864, kNoDate, 17);           // effective
        p[881] = 1;                                  // file structure version

        uint8_t* t = sec(kPvdLba + 1);
        t[0] = 255;                                  // volume descriptor set terminator
        std::memcpy(t + 1, "CD001", 5);
        t[6] = 1;
    }

    // 7) Write the image: the header region, then each file's data sector-padded, in LBA order.
    std::ofstream out(o.outIso, std::ios::binary | std::ios::trunc);
    if (!out) { r.error = "Could not open output image for writing: " + o.outIso; return r; }
    out.write(reinterpret_cast<const char*>(head.data()), std::streamsize(head.size()));

    std::vector<char> buf(kSector);
    for (int di : bfs)
        for (const File& f : dirs[di].files)
        {
            std::ifstream in(f.diskPath, std::ios::binary);
            if (!in) { r.error = "Could not read game file: " + f.diskPath; return r; }
            uint32_t remaining = f.size;
            while (remaining > 0)
            {
                const uint32_t n = std::min<uint32_t>(remaining, kSector);
                in.read(buf.data(), n);
                if (uint32_t(in.gcount()) != n) { r.error = "Short read on " + f.diskPath; return r; }
                if (n < kSector) std::memset(buf.data() + n, 0, kSector - n);  // pad the last sector
                out.write(buf.data(), kSector);
                remaining -= n;
            }
        }
    if (!out) { r.error = "Write error while producing " + o.outIso; return r; }
    out.close();
    r.imageBytes = uint64_t(totalSectors) * kSector;

    // 8) A single-track MODE1/2048 cue beside the image.
    std::ofstream cue(r.cuePath, std::ios::trunc);
    if (cue)
    {
        cue << "FILE \"" << BaseName(o.outIso) << "\" BINARY\n"
            << "  TRACK 01 MODE1/2048\n"
            << "    INDEX 01 00:00:00\n";
    }

    r.ok = true;
    return r;
}

}  // namespace sfe
