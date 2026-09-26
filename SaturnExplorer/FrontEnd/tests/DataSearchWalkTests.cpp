// The recursive data-search walk, specifically its handling of symbolic links. POSIX
// stat() resolves a link, so a directory symlink pointing at an ancestor used to report as
// a directory and the walk recursed into itself until the stack ran out. That is easy to
// reach by accident on macOS -- a data folder holding a convenience link back to its
// parent is enough -- so the behaviour is pinned here rather than left to inspection.
#include "DataSearch.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what, int line)
{
    if (ok) return;
    std::printf("CHECK failed at line %d: %s\n", line, what);
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

const char kNeedle[] = "SATURNEXPLORERNEEDLE";

std::string TempRoot()
{
    std::string root = "/tmp/se_datasearch_" + std::to_string(::getpid());
    ::mkdir(root.c_str(), 0700);
    return root;
}

void WriteFile(const std::string& path, const std::string& body)
{
    std::ofstream f(path, std::ios::binary);
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
}

void RemoveTree(const std::string& root)
{
    const std::string cmd = "rm -rf '" + root + "'";
    if (std::system(cmd.c_str()) != 0) { /* best effort; it is under /tmp */ }
}

// A directory symlink pointing back at its own parent. Before the fix this walk never
// terminated; the test would hang or crash rather than fail.
void TestDirectoryLinkCycleTerminates()
{
    const std::string root = TempRoot();
    ::mkdir((root + "/data").c_str(), 0700);
    WriteFile(root + "/data/payload.bin", std::string("xx") + kNeedle + "yy");
    // data/loop -> data, i.e. a cycle one level deep.
    CHECK(::symlink((root + "/data").c_str(), (root + "/data/loop").c_str()) == 0);

    std::vector<sfe::DataSearchHit> hits;
    const std::vector<std::string> roots{root};
    const size_t scanned = sfe::SearchData(
        roots, reinterpret_cast<const uint8_t*>(kNeedle),
        sizeof(kNeedle) - 1, sfe::SearchCompression::None, hits);

    // The real file is found exactly once: the cycle contributes nothing rather than the
    // same file over and over at ever deeper paths.
    CHECK(scanned == 1);
    CHECK(hits.size() == 1);
    if (hits.size() == 1)
    {
        CHECK(hits[0].offsets.size() == 1);
        CHECK(hits[0].path == root + "/data/payload.bin");
    }
    RemoveTree(root);
}

// A link to a regular file is not a cycle risk and stays searchable -- users do symlink
// large images into a data folder rather than copying them.
void TestFileLinkIsStillSearched()
{
    const std::string root = TempRoot();
    ::mkdir((root + "/real").c_str(), 0700);
    WriteFile(root + "/real/image.bin", std::string("aa") + kNeedle);
    CHECK(::symlink((root + "/real/image.bin").c_str(), (root + "/alias.bin").c_str()) == 0);

    std::vector<sfe::DataSearchHit> hits;
    const std::vector<std::string> roots{root};
    sfe::SearchData(roots, reinterpret_cast<const uint8_t*>(kNeedle),
                    sizeof(kNeedle) - 1, sfe::SearchCompression::None, hits);

    CHECK(hits.size() == 2);   // the file itself and the link to it
    RemoveTree(root);
}

// A chain of directories deeper than the cap stops rather than descending forever. Built
// with real directories, so it exercises the depth guard and not the link rule.
void TestDepthIsCapped()
{
    const std::string root = TempRoot();
    std::string path = root;
    for (int i = 0; i < 40; ++i)
    {
        path += "/d";
        ::mkdir(path.c_str(), 0700);
    }
    WriteFile(path + "/deep.bin", std::string(kNeedle));

    std::vector<sfe::DataSearchHit> hits;
    const std::vector<std::string> roots{root};
    sfe::SearchData(roots, reinterpret_cast<const uint8_t*>(kNeedle),
                    sizeof(kNeedle) - 1, sfe::SearchCompression::None, hits);

    // 40 levels is past the cap, so the file is out of reach. The point is that the walk
    // returns at all; finding nothing that deep is the documented trade.
    CHECK(hits.empty());
    RemoveTree(root);
}

}  // namespace

int main()
{
    TestDirectoryLinkCycleTerminates();
    TestFileLinkIsStillSearched();
    TestDepthIsCapped();
    if (gFailures)
    {
        std::printf("DataSearchWalkTests: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("DataSearchWalkTests: all checks passed\n");
    return 0;
}
