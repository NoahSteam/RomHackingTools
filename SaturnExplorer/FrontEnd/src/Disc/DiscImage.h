// DiscImage — opens a Saturn CD image on disk and reads its 2048-byte logical sectors,
// hiding the on-disk layout (plain 2048-byte .iso, or raw 2352-byte .bin in Mode 1 / Mode 2
// Form 1, optionally named by a .cue). Provides a SectorReader for IsoFs and reports the file
// byte offset of any sector, so a CD read address maps to a place in the image file. Native
// only (uses std::ifstream); the web build simply doesn't offer disc browsing.
#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "Disc/IsoFs.h"   // SectorReader

namespace sfe
{

class DiscImage
{
public:
    // Open 'path' (.iso / .bin / .img / .cue). Detects the sector layout by locating the ISO
    // volume descriptor. Returns false if the file can't be opened or no data track is found.
    bool Open(const std::string& path);
    void Close();
    bool IsOpen() const { return mFile.is_open(); }

    const std::string& Path()      const { return mPath; }
    uint32_t           SectorSize() const { return mSectorSize; }   // 2048 or 2352
    uint32_t           SectorCount() const { return mSectorCount; } // total logical sectors

    // Read the 2048-byte user data of logical sector 'lba' into 'out'. False past the end.
    bool ReadSector(uint32_t lba, uint8_t* out);

    // File byte offset of sector 'lba': RawOffset = start of the (2352) sector on disk;
    // UserOffset = start of its 2048-byte user data (== RawOffset for a 2048 image).
    uint64_t RawOffset(uint32_t lba)  const { return uint64_t(lba) * mSectorSize; }
    uint64_t UserOffset(uint32_t lba) const { return RawOffset(lba) + mHeaderOffset; }

    // A SectorReader bound to this image (valid while the image stays open).
    SectorReader Reader();

private:
    // Try to validate a candidate layout by finding "CD001" at LBA 16. Returns true + sets
    // mSectorSize/mHeaderOffset on success.
    bool Detect(uint32_t sectorSize, uint32_t headerOffset);

    std::ifstream mFile;
    std::string   mPath;
    uint64_t      mFileSize = 0;
    uint32_t      mSectorSize = 2048;
    uint32_t      mHeaderOffset = 0;    // user data offset within a raw sector (0/16/24)
    uint32_t      mSectorCount = 0;
};

}  // namespace sfe
