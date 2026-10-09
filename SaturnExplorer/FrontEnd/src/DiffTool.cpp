#include "DiffTool.h"

#include <cctype>
#include <cstdio>
#include <vector>

#include "FileWrite.h"

namespace sfe
{

const char* const kDefaultDiffArgs = "\"{a}\" \"{b}\"";

std::string BuildDiffArgs(const std::string& tmpl, const std::string& folderA, const std::string& folderB)
{
    const std::string t = tmpl.empty() ? std::string(kDefaultDiffArgs) : tmpl;
    // One pass over the template, not two replaces over the result: a folder path that happens to
    // contain the other token must not be expanded again.
    std::string result;
    for (size_t i = 0; i < t.size();)
    {
        if (t.compare(i, 3, "{a}") == 0) { result += folderA; i += 3; }
        else if (t.compare(i, 3, "{b}") == 0) { result += folderB; i += 3; }
        else result += t[i++];
    }
    return result;
}

bool DiffArgsUseFrames(const std::string& tmpl)
{
    return tmpl.find("{a}") != std::string::npos || tmpl.find("{b}") != std::string::npos;
}

std::string DiffRegionFileName(RegionId id)
{
    const RegionTraits& t = Traits(id);
    std::string name;
    for (const char* c = t.name; *c; ++c)
        name += std::isalnum(static_cast<unsigned char>(*c)) ? *c : '_';
    if (HasBusAddress(t))
    {
        char addr[16];
        std::snprintf(addr, sizeof(addr), "_%08X", static_cast<unsigned>(t.busBase));
        name += addr;
    }
    return name + ".bin";
}

std::string DiffSideFolderName(char side, uint64_t frameNo)
{
    return std::string(1, side) + "_frame_" + std::to_string(static_cast<unsigned long long>(frameNo));
}

bool WriteSnapshotFolder(const MemSnapshot& snap, const std::string& dir, std::string& error)
{
    if (!MakeDirectory(dir))
    {
        error = "Could not create " + dir;
        return false;
    }
    for (const MemRegionImage& region : snap.regions)
    {
        const std::string path = dir + PathSeparator() + DiffRegionFileName(region.id);
        if (!WriteFileAtomically(path, region.bytes.data(), region.bytes.size(), error)) return false;
    }
    error.clear();
    return true;
}

void PurgeDiffFolders(const std::string& root)
{
    std::vector<std::string> names;
    if (!ListDirectory(root, names)) return;
    for (const std::string& name : names)
        RemoveFlatDirectory(root + PathSeparator() + name);
}

}  // namespace sfe
