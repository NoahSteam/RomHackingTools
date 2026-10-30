// LoadBiosImageFile: the BIOS image the user points the decompiler panel at. A file of its own, away from
// MemorySnapshot.cpp, so the snapshot's tests need not link the file layer; tested by LoaderFuzzTests.
#include "Decompiler/MemorySnapshot.h"

#include "FileRead.h"

namespace sfe
{
namespace decomp
{

BiosLoadResult LoadBiosImageFile(const std::string& path)
{
    BiosLoadResult r;
    std::string bytes;
    // Sized before it is read, and read no further than the ceiling: a disc image picked by mistake used to be read whole
    // (and a FIFO waited on, and /dev/zero never finished) before the size was looked at (A9-1).
    switch (ReadFileCapped(path, kMaxBiosImageBytes, bytes))
    {
    case CappedRead::Ok: break;
    case CappedRead::Missing:
    case CappedRead::Unreadable: r.error = "could not read the BIOS image " + path; return r;
    case CappedRead::NotRegular: r.error = path + " is not a regular file; not used"; return r;
    case CappedRead::TooLarge: r.error = path + " is larger than a Saturn BIOS (512 KiB); not used"; return r;
    }
    if (bytes.empty())
    {
        r.error = "could not read the BIOS image " + path + " (the file is empty)";
        return r;
    }
    r.image = BiosImage::Make(std::vector<uint8_t>(bytes.begin(), bytes.end()));
    return r;
}

}  // namespace decomp
}  // namespace sfe
