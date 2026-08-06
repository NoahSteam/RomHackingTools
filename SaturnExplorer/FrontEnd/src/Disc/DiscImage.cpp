#include "Disc/DiscImage.h"

#include <cstring>

namespace sfe
{
namespace
{
// Directory of a path (everything up to and including the last '/' or '\'), for resolving a
// .cue's FILE reference relative to the cue.
std::string DirOf(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string Lower(std::string s)
{
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

// Extract the first FILE "name" reference from a .cue's text (BIN/IMG the tracks live in).
std::string CueFirstFile(const std::string& cueText)
{
    const size_t f = cueText.find("FILE");
    if (f == std::string::npos) return {};
    const size_t q1 = cueText.find('"', f);
    if (q1 == std::string::npos) return {};
    const size_t q2 = cueText.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return cueText.substr(q1 + 1, q2 - q1 - 1);
}
}  // namespace

bool DiscImage::Detect(uint32_t sectorSize, uint32_t headerOffset)
{
    const uint64_t off = uint64_t(16) * sectorSize + headerOffset;
    if (off + 6 > mFileSize) return false;
    mFile.clear();
    mFile.seekg((std::streamoff)off, std::ios::beg);
    char id[6] = {};
    mFile.read(id, 6);
    if (!mFile || std::memcmp(id + 1, "CD001", 5) != 0) return false;
    mSectorSize = sectorSize;
    mHeaderOffset = headerOffset;
    return true;
}

bool DiscImage::Open(const std::string& path)
{
    Close();
    mPath = path;

    // A .cue just names the real image; open the first FILE it references (raw 2352 track).
    std::string dataPath = path;
    if (Lower(path).size() >= 4 && Lower(path).substr(Lower(path).size() - 4) == ".cue")
    {
        std::ifstream cue(path, std::ios::binary);
        if (!cue) return false;
        std::string text((std::istreambuf_iterator<char>(cue)), std::istreambuf_iterator<char>());
        const std::string file = CueFirstFile(text);
        if (file.empty()) return false;
        dataPath = DirOf(path) + file;
        mPath = dataPath;
    }

    mFile.open(dataPath, std::ios::binary);
    if (!mFile) return false;
    mFile.seekg(0, std::ios::end);
    mFileSize = (uint64_t)mFile.tellg();
    mFile.seekg(0, std::ios::beg);

    // Candidate layouts, most specific first: plain 2048 (.iso), then raw 2352 Mode 1 (16-byte
    // header) and Mode 2 Form 1 (24-byte header).
    if (Detect(2048, 0) || Detect(2352, 16) || Detect(2352, 24))
    {
        mSectorCount = mSectorSize ? (uint32_t)(mFileSize / mSectorSize) : 0;
        return true;
    }
    Close();
    return false;
}

void DiscImage::Close()
{
    if (mFile.is_open()) mFile.close();
    mFileSize = 0;
    mSectorCount = 0;
    mSectorSize = 2048;
    mHeaderOffset = 0;
}

bool DiscImage::ReadSector(uint32_t lba, uint8_t* out)
{
    if (!mFile.is_open() || !out) return false;
    const uint64_t off = UserOffset(lba);
    if (off + 2048 > mFileSize) return false;
    mFile.clear();
    mFile.seekg((std::streamoff)off, std::ios::beg);
    mFile.read(reinterpret_cast<char*>(out), 2048);
    return (bool)mFile;
}

SectorReader DiscImage::Reader()
{
    return [this](uint32_t lba, uint8_t* out) { return ReadSector(lba, out); };
}

}  // namespace sfe
