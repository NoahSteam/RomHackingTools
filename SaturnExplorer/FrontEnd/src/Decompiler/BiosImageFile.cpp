// LoadBiosImageFile: the BIOS image the user points the decompiler panel at. A file of its own, away from
// MemorySnapshot.cpp, so the snapshot's tests need not link the file layer; tested by LoaderFuzzTests.
#include "Decompiler/MemorySnapshot.h"

#include "Disc/PathUtil.h"   // ReadWholeFile

namespace sfe
{
namespace decomp
{

BiosLoadResult LoadBiosImageFile(const std::string& path)
{
    BiosLoadResult r;
    std::string bytes;
    if (!ReadWholeFile(path, bytes) || bytes.empty())
    {
        r.error = "could not read the BIOS image " + path;
        return r;
    }
    if (bytes.size() > kMaxBiosImageBytes)
    {
        r.error = path + " is larger than a Saturn BIOS (512 KiB); not used";
        return r;
    }
    r.image = BiosImage::Make(std::vector<uint8_t>(bytes.begin(), bytes.end()));
    return r;
}

}  // namespace decomp
}  // namespace sfe
