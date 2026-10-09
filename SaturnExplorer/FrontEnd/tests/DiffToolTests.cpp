// DiffTool -- the argument template and the folders of raw region files an external diff tool is
// handed: tokens substituted once, the files named and sized by region, and clean-up that only
// ever removes what this feature wrote.
#include "DiffTool.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "FileWrite.h"

using namespace sfe;

namespace
{
int gFailures;

#define CHECK(cond) do {                                                      \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } \
} while (0)

std::string TempRoot()
{
    const char* t = std::getenv("TMPDIR");
    std::string base = t && *t ? t : "/tmp";
    if (base.back() == '/') base.pop_back();
    return base + "/se_difftool_test_" + std::to_string(static_cast<unsigned long long>(std::time(nullptr)));
}

// Every region zero, then one byte set per region to its index + 1, so a file read back can be
// matched to the region it came from.
MemSnapshot Snapshot(uint64_t frame)
{
    MemSnapshot s;
    s.origin = { 1, 1, frame, false };
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        MemRegionImage img;
        img.id = static_cast<RegionId>(i);
        img.bytes.assign(Traits(img.id).size, 0);
        img.bytes[0] = static_cast<uint8_t>(i + 1);
        img.bytes.back() = 0xEE;
        s.regions.push_back(std::move(img));
    }
    return s;
}

bool Read(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}

void TestArgs()
{
    CHECK(BuildDiffArgs("{a} {b}", "X", "Y") == "X Y");
    CHECK(BuildDiffArgs("\"{a}\" \"{b}\"", "C:\\a b", "C:\\c") == "\"C:\\a b\" \"C:\\c\"");
    CHECK(BuildDiffArgs("-l {b} {a} {a}", "A", "B") == "-l B A A");
    CHECK(BuildDiffArgs("", "A", "B") == "\"A\" \"B\"");                  // empty = the default
    CHECK(BuildDiffArgs("{a}", "x{b}y", "B") == "x{b}y");                    // a path is not expanded again
    CHECK(BuildDiffArgs("{c} {A} {a", "A", "B") == "{c} {A} {a");            // lookalikes stay
    CHECK(DiffArgsUseFrames("--left {a}"));
    CHECK(DiffArgsUseFrames("{b}"));
    CHECK(!DiffArgsUseFrames("--help"));
}

void TestNames()
{
    CHECK(DiffRegionFileName(RegionId::Hwram) == "HWRAM_06000000.bin");
    CHECK(DiffRegionFileName(RegionId::Lwram) == "LWRAM_00200000.bin");
    CHECK(DiffSideFolderName('A', 1234) == "A_frame_1234");
    // Every region gets a distinct name made only of characters a file may hold.
    std::vector<std::string> seen;
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        const std::string n = DiffRegionFileName(static_cast<RegionId>(i));
        CHECK(n.find_first_of(" /\\:*?\"<>|") == std::string::npos);
        for (const std::string& o : seen) CHECK(o != n);
        seen.push_back(n);
    }
    // The VDP1 frame buffer is an image, not bus memory: no address in its name.
    CHECK(!HasBusAddress(Traits(RegionId::Vdp1Fb)));
    CHECK(DiffRegionFileName(RegionId::Vdp1Fb) == "VDP1_FB.bin");
}

void TestWriteAndPurge()
{
    const std::string root = TempRoot();
    CHECK(MakeDirectory(root));
    const std::string a = root + PathSeparator() + DiffSideFolderName('A', 10);
    const std::string b = root + PathSeparator() + DiffSideFolderName('B', 11);
    std::string error;
    const MemSnapshot sa = Snapshot(10), sb = Snapshot(11);
    CHECK(WriteSnapshotFolder(sa, a, error));
    CHECK(WriteSnapshotFolder(sb, b, error));
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        const RegionId id = static_cast<RegionId>(i);
        std::vector<uint8_t> got;
        CHECK(Read(a + PathSeparator() + DiffRegionFileName(id), got));
        CHECK(got == sa.regions[i].bytes);
        CHECK(got.size() == Traits(id).size);
        CHECK(got[0] == i + 1);
    }

    // Writing the same side again replaces the files rather than failing on them.
    CHECK(WriteSnapshotFolder(sa, a, error));

    // Purge removes the folders it wrote and nothing it did not.
    const std::string stray = root + PathSeparator() + "keep.txt";
    CHECK(WriteFileAtomically(stray, "x", 1, error));
    PurgeDiffFolders(root);
    CHECK(!FileOrDirectoryExists(a));
    CHECK(!FileOrDirectoryExists(b));
    CHECK(FileOrDirectoryExists(stray));   // a plain file is not a folder of ours

    // A folder holding a subfolder is refused wholesale: not ours, so not deleted.
    const std::string odd = root + PathSeparator() + "odd";
    CHECK(MakeDirectory(odd));
    CHECK(MakeDirectory(odd + PathSeparator() + "inner"));
    PurgeDiffFolders(root);
    CHECK(FileOrDirectoryExists(odd + PathSeparator() + "inner"));

    // A missing root is not an error.
    PurgeDiffFolders(root + PathSeparator() + "nope");

    RemoveEmptyDirectory(odd + PathSeparator() + "inner");
    RemoveEmptyDirectory(odd);
    RemoveFile(stray);
    RemoveEmptyDirectory(root);
}
}  // namespace

int main()
{
    TestArgs();
    TestNames();
    TestWriteAndPurge();
    if (gFailures == 0) std::printf("DiffTool: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
