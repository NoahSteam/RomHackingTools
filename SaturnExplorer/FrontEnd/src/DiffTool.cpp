#include "DiffTool.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "ArgSplit.h"
#include "FileWrite.h"

namespace sfe
{

const char* const kDefaultDiffArgs = "\"{a}\" \"{b}\"";

std::vector<std::string> BuildDiffArgv(const std::string& tmpl, const std::string& folderA,
                                       const std::string& folderB)
{
    std::vector<std::string> args = SplitCommandLine(tmpl.empty() ? std::string(kDefaultDiffArgs) : tmpl);
    // One pass over each argument, not two replaces over the result: a folder path that happens to
    // contain the other token must not be expanded again.
    const std::vector<std::pair<std::string, std::string>> tokens = { { "{a}", folderA }, { "{b}", folderB } };
    for (std::string& arg : args) arg = SubstituteTokens(arg, tokens);
    return args;
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

std::string NewDiffComparisonId(uint64_t nowSeconds)
{
    static std::mt19937 rng{ std::random_device{}() };
    char id[48];
    std::snprintf(id, sizeof(id), "cmp%llu-%08x", static_cast<unsigned long long>(nowSeconds),
                  static_cast<unsigned>(rng()));
    return id;
}

std::string DiffSideFolderName(const std::string& comparisonId, char side, uint64_t frameNo)
{
    return comparisonId + "_" + std::string(1, side) + "_frame_" + std::to_string(static_cast<unsigned long long>(frameNo));
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
        if (!InCompare(region.id)) continue;   // no bytes held for it, so no empty file for the tool to list
        const std::string path = dir + PathSeparator() + DiffRegionFileName(region.id);
        if (!WriteFileAtomically(path, region.bytes.data(), region.bytes.size(), error)) return false;
    }
    error.clear();
    return true;
}

namespace
{
bool AllDigits(const std::string& s, size_t from, size_t to)
{
    if (from >= to || to > s.size()) return false;
    for (size_t i = from; i < to; ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

// "cmp<seconds>-<8 hex>_<A|B>_frame_<digits>" -> the seconds. False for anything else.
bool ParseComparisonFolder(const std::string& name, uint64_t& seconds)
{
    if (name.compare(0, 3, "cmp") != 0) return false;
    const size_t dash = name.find('-', 3);
    if (dash == std::string::npos || !AllDigits(name, 3, dash) || dash - 3 > 19) return false;
    if (name.size() < dash + 9 + 9 || name.compare(dash + 9, 1, "_") != 0) return false;
    for (size_t i = dash + 1; i < dash + 9; ++i)
        if (!std::isxdigit(static_cast<unsigned char>(name[i]))) return false;
    const std::string rest = name.substr(dash + 10);   // "A_frame_1234"
    if (rest.size() < 9 || (rest[0] != 'A' && rest[0] != 'B') || rest.compare(1, 7, "_frame_") != 0 ||
        !AllDigits(rest, 8, rest.size()))
        return false;
    seconds = std::strtoull(name.c_str() + 3, nullptr, 10);
    return true;
}

// The folders earlier versions wrote, "A_frame_1234": one per side, replaced by the next comparison.
bool IsLegacyFolder(const std::string& name)
{
    return name.size() >= 9 && (name[0] == 'A' || name[0] == 'B') && name.compare(1, 7, "_frame_") == 0 &&
           AllDigits(name, 8, name.size());
}
}  // namespace

void PurgeDiffFolders(const std::string& root, uint64_t nowSeconds, uint64_t keepSeconds)
{
    std::vector<std::string> names;
    if (!ListDirectory(root, names)) return;
    for (const std::string& name : names)
    {
        uint64_t made = 0;
        bool remove = false;
        if (ParseComparisonFolder(name, made))
            remove = keepSeconds == 0 || (made <= nowSeconds && nowSeconds - made >= keepSeconds);
        else if (IsLegacyFolder(name))
            remove = keepSeconds == 0;   // no date in the name to age it by, so only an explicit cleanup removes it
        if (remove) RemoveFlatDirectory(root + PathSeparator() + name);
    }
}

}  // namespace sfe
