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
#include "Platform/CommandLine.h"

using namespace sfe;

namespace
{
int gFailures;

#define CHECK(cond) do {                                                      \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } \
} while (0)

size_t Ix(RegionId id) { return static_cast<size_t>(id); }

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
        img.bytes.assign(SnapshotSize(img.id), 0);
        if (!img.bytes.empty())
        {
            img.bytes[0] = static_cast<uint8_t>(i + 1);
            img.bytes.back() = 0xEE;
        }
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

using Argv = std::vector<std::string>;

void TestArgs()
{
    CHECK((BuildDiffArgv("{a} {b}", "X", "Y") == Argv{ "X", "Y" }));
    // A quoted token is one argument however its value is spelled: the split happens before the
    // substitution, so a space in a path cannot split it.
    CHECK((BuildDiffArgv("\"{a}\" \"{b}\"", "C:\\a b", "C:\\c") == Argv{ "C:\\a b", "C:\\c" }));
    CHECK((BuildDiffArgv("-l {b} {a} {a}", "A", "B") == Argv{ "-l", "B", "A", "A" }));
    CHECK((BuildDiffArgv("", "A", "B") == Argv{ "A", "B" }));                      // empty = the default
    CHECK((BuildDiffArgv("{a}", "x{b}y", "B") == Argv{ "x{b}y" }));                // a path is not expanded again
    CHECK((BuildDiffArgv("{c} {A} {a", "A", "B") == Argv{ "{c}", "{A}", "{a" }));  // lookalikes stay
    CHECK((BuildDiffArgv("--left={a} 'two words' \"\"", "A", "B") == Argv{ "--left=A", "two words", "" }));
    CHECK((BuildDiffArgv("  {a}\t{b}  ", "A", "B") == Argv{ "A", "B" }));

    // The folder is a literal value. Nothing in it is a shell's business: `$`, backticks, quotes and
    // the other token all arrive exactly as they are (this used to reach a /bin/sh).
    const std::string nasty = "cfg$SE_UNSET/`id`/\"q\"/it's/{b}";
    const Argv v = BuildDiffArgv("\"{a}\" \"{b}\"", nasty, "B");
    CHECK(v.size() == 2 && v[0] == nasty && v[1] == "B");

    CHECK(DiffArgsUseFrames("--left {a}"));
    CHECK(DiffArgsUseFrames("{b}"));
    CHECK(!DiffArgsUseFrames("--help"));
}

void TestWindowsCommandLine()
{
    CHECK(QuoteWindowsArg("plain") == "plain");
    CHECK(QuoteWindowsArg("") == "\"\"");
    CHECK(QuoteWindowsArg("C:\\a b\\c") == "\"C:\\a b\\c\"");
    CHECK(QuoteWindowsArg("say \"hi\"") == "\"say \\\"hi\\\"\"");
    CHECK(QuoteWindowsArg("dir with space\\") == "\"dir with space\\\\\"");   // trailing backslashes double
    CHECK(QuoteWindowsArg("a\\\"b c") == "\"a\\\\\\\"b c\"");              // backslashes before a quote double, plus the quote's own
    CHECK(JoinWindowsCommandLine({ "C:\\a b", "C:\\c" }) == "\"C:\\a b\" C:\\c");
    CHECK(JoinWindowsCommandLine({}).empty());
}

void TestNames()
{
    CHECK(DiffRegionFileName(RegionId::Hwram) == "HWRAM_06000000.bin");
    CHECK(DiffRegionFileName(RegionId::Lwram) == "LWRAM_00200000.bin");
    CHECK(DiffSideFolderName("cmp1760000000-9f3a07c1", 'A', 1234) == "cmp1760000000-9f3a07c1_A_frame_1234");
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
    const std::string id = NewDiffComparisonId(1000000);
    const std::string a = root + PathSeparator() + DiffSideFolderName(id, 'A', 10);
    const std::string b = root + PathSeparator() + DiffSideFolderName(id, 'B', 11);
    std::string error;
    const MemSnapshot sa = Snapshot(10), sb = Snapshot(11);
    CHECK(WriteSnapshotFolder(sa, a, error));
    CHECK(WriteSnapshotFolder(sb, b, error));
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        const RegionId rid = static_cast<RegionId>(i);
        std::vector<uint8_t> got;
        if (!InCompare(rid))
        {
            // Left out of comparisons: no file, rather than an empty one for the tool to list.
            CHECK(!Read(a + PathSeparator() + DiffRegionFileName(rid), got));
            continue;
        }
        CHECK(Read(a + PathSeparator() + DiffRegionFileName(rid), got));
        CHECK(got == sa.regions[i].bytes);
        CHECK(got.size() == Traits(rid).size);
        CHECK(got[0] == i + 1);
    }

    // Writing the same side again replaces the files rather than failing on them.
    CHECK(WriteSnapshotFolder(sa, a, error));

    // A comparison made a week ago is gone, a recent one stays, and a file that is not ours is never
    // touched.
    const std::string stray = root + PathSeparator() + "keep.txt";
    CHECK(WriteFileAtomically(stray, "x", 1, error));
    PurgeDiffFolders(root, 1000000 + kDiffKeepSeconds - 1, kDiffKeepSeconds);
    CHECK(FileOrDirectoryExists(a) && FileOrDirectoryExists(b));          // one second short of a week
    PurgeDiffFolders(root, 1000000 + kDiffKeepSeconds, kDiffKeepSeconds);
    CHECK(!FileOrDirectoryExists(a));
    CHECK(!FileOrDirectoryExists(b));
    CHECK(FileOrDirectoryExists(stray));   // a plain file is not a folder of ours

    // keepSeconds 0 is the explicit "delete them all", whatever the age.
    CHECK(WriteSnapshotFolder(sa, a, error));
    PurgeDiffFolders(root, 1000001, 0);
    CHECK(!FileOrDirectoryExists(a));

    // The un-numbered folders earlier versions wrote carry no date, and a diff window opened before an
    // upgrade may still be showing one: the automatic cleanup leaves them, an explicit one removes them.
    // A folder named like neither is not ours either way.
    const std::string legacy = root + PathSeparator() + "A_frame_7";
    const std::string foreign = root + PathSeparator() + "my_notes";
    CHECK(MakeDirectory(legacy));
    CHECK(MakeDirectory(foreign));
    CHECK(WriteFileAtomically(foreign + PathSeparator() + "f.txt", "x", 1, error));
    PurgeDiffFolders(root, 2000000000, kDiffKeepSeconds);
    CHECK(FileOrDirectoryExists(legacy));
    CHECK(FileOrDirectoryExists(foreign + PathSeparator() + "f.txt"));
    PurgeDiffFolders(root, 2000000000, 0);
    CHECK(!FileOrDirectoryExists(legacy));
    CHECK(FileOrDirectoryExists(foreign + PathSeparator() + "f.txt"));

    // A folder of ours that holds a subfolder is refused wholesale: unexpected content, so not deleted.
    const std::string odd = root + PathSeparator() + DiffSideFolderName(NewDiffComparisonId(5), 'A', 1);
    CHECK(MakeDirectory(odd));
    CHECK(MakeDirectory(odd + PathSeparator() + "inner"));
    PurgeDiffFolders(root, 2000000, kDiffKeepSeconds);
    CHECK(FileOrDirectoryExists(odd + PathSeparator() + "inner"));

    // A missing root is not an error.
    PurgeDiffFolders(root + PathSeparator() + "nope", 1, 0);

    RemoveEmptyDirectory(odd + PathSeparator() + "inner");
    RemoveEmptyDirectory(odd);
    RemoveFile(foreign + PathSeparator() + "f.txt");
    RemoveEmptyDirectory(foreign);
    RemoveFile(stray);
    RemoveEmptyDirectory(root);
}

// Two comparisons that used the same frame numbers -- the memory behind a number changes across a state
// load, a branch of history or a reconnect -- must not share files: the diff window of the first is
// still showing them.
void TestComparisonsDoNotShareFolders()
{
    const std::string root = TempRoot() + "_unique";
    CHECK(MakeDirectory(root));
    const std::string id1 = NewDiffComparisonId(5000), id2 = NewDiffComparisonId(5000);
    CHECK(id1 != id2);                                                    // even in the same second
    const std::string a1 = root + PathSeparator() + DiffSideFolderName(id1, 'A', 10);
    const std::string a2 = root + PathSeparator() + DiffSideFolderName(id2, 'A', 10);   // same frame number
    CHECK(a1 != a2);

    MemSnapshot first = Snapshot(10), second = Snapshot(10);
    first.regions[Ix(RegionId::Hwram)].bytes[0] = 0x00;
    second.regions[Ix(RegionId::Hwram)].bytes[0] = 0x55;   // different memory, same frame number
    std::string error;
    CHECK(WriteSnapshotFolder(first, a1, error));
    CHECK(WriteSnapshotFolder(second, a2, error));
    std::vector<uint8_t> got;
    CHECK(Read(a1 + PathSeparator() + DiffRegionFileName(RegionId::Hwram), got) && got[0] == 0x00);
    CHECK(Read(a2 + PathSeparator() + DiffRegionFileName(RegionId::Hwram), got) && got[0] == 0x55);

    PurgeDiffFolders(root, 5000, 0);
    CHECK(!FileOrDirectoryExists(a1) && !FileOrDirectoryExists(a2));
    RemoveEmptyDirectory(root);
}
}  // namespace

int main()
{
    TestArgs();
    TestWindowsCommandLine();
    TestNames();
    TestWriteAndPurge();
    TestComparisonsDoNotShareFolders();
    if (gFailures == 0) std::printf("DiffTool: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
