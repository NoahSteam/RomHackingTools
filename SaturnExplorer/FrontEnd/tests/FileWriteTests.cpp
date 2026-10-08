// FileWrite tests — the two things that made saves lie, exercised against a real filesystem.
//
// The first is the close. A stdio write smaller than the buffer does not touch the device, so
// a full disk reports nothing at fwrite and fails only in the flush inside fclose. /dev/full
// is exactly that device and is what this file uses: writing two bytes to it returns success
// from fwrite and -1 from fclose, which is why "wrote == size" was not a success check.
//
// The second is truncation. "wb" empties the file before the first byte arrives, so a save
// that then fails has destroyed what was there. The staged write is what fixes it, and the
// test for it forces the staging write to fail with the destination already in place.

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "FileWrite.h"

using namespace sfe;

namespace
{
int gFailures = 0;

void Check(bool ok, const char* expression, int line)
{
    if (ok) return;
    std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

const char* kDir = "se_filewrite_test";

std::string P(const std::string& name)
{
    return std::string(kDir) + PathSeparator() + name;
}

bool Put(const std::string& path, const std::string& text)
{
    std::string error;
    return WriteFileAtomically(path, text.data(), text.size(), error);
}

// Read a whole file; returns false when it cannot be opened.
bool Slurp(const std::string& path, std::string& out)
{
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char buf[512];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

// --- Tests ---

void TestWriteAndReadBack()
{
    CHECK(MakeDirectory(kDir));
    std::string error = "not cleared";
    const std::string body = "0123456789";
    CHECK(WriteFileAtomically(P("plain.bin"), body.data(), body.size(), error));
    CHECK(error.empty());
    std::string got;
    CHECK(Slurp(P("plain.bin"), got));
    CHECK(got == body);

    // A zero-byte artifact is a legitimate result, not a failure.
    CHECK(WriteFileAtomically(P("empty.bin"), nullptr, 0, error));
    CHECK(Slurp(P("empty.bin"), got));
    CHECK(got.empty());

    // Overwriting replaces the contents rather than appending to them.
    const std::string shorter = "ab";
    CHECK(WriteFileAtomically(P("plain.bin"), shorter.data(), shorter.size(), error));
    CHECK(Slurp(P("plain.bin"), got));
    CHECK(got == shorter);

    // Nothing is left beside the published files.
    std::vector<std::string> names;
    CHECK(ListDirectory(kDir, names));
    for (size_t i = 0; i < names.size(); ++i)
        CHECK(names[i].find(".separt") == std::string::npos);

    CHECK(RemoveFile(P("plain.bin")));
    CHECK(RemoveFile(P("empty.bin")));
}

// THE regression. Two bytes to /dev/full: fwrite succeeds, fclose fails. The old code
// returned "wrote == size" and called that saved.
void TestCloseFailureIsReported()
{
    const std::string body = "hi";
    std::string error;
    CHECK(!WriteFileAtomically("/dev/full", body.data(), body.size(), error));
    CHECK(!error.empty());                              // and says why
    CHECK(error.find("/dev/full") != std::string::npos);

    // A device is written in place, never staged -- a rename would have replaced the node
    // itself. So the device is still there and no temporary was left beside it.
    CHECK(FileOrDirectoryExists("/dev/full"));
    CHECK(!FileOrDirectoryExists("/dev/full.separt"));
}

// A staged write that fails must leave the destination holding its previous contents. The
// staging path is made unopenable (a directory sits where the temporary file would go), which
// is the one fault that is reachable without a full disk or a permission trick.
void TestFailedWriteKeepsThePreviousFile()
{
    CHECK(MakeDirectory(kDir));
    const std::string target = P("precious.bin");
    const std::string good = "the original contents";
    CHECK(Put(target, good));

    const std::string blocker = target + ".separt";
    CHECK(MakeDirectory(blocker));   // fopen(blocker, "wb") now fails with EISDIR

    const std::string replacement = "this must never land";
    std::string error;
    CHECK(!WriteFileAtomically(target, replacement.data(), replacement.size(), error));
    CHECK(!error.empty());

    std::string got;
    CHECK(Slurp(target, got));
    CHECK(got == good);   // untouched, where "wb" would have emptied it first

    CHECK(RemoveEmptyDirectory(blocker));
    CHECK(RemoveFile(target));
}

void TestOpenFailureIsReported()
{
    std::string error;
    const std::string body = "x";
    // No such directory, so neither the staging file nor the target can be created.
    CHECK(!WriteFileAtomically("se_filewrite_missing_dir/nope.bin", body.data(), body.size(),
                               error));
    CHECK(!error.empty());
    CHECK(!WriteFileAtomically("", body.data(), body.size(), error));
    CHECK(!error.empty());
}

void TestDirectoryHelpers()
{
    const std::string dir = std::string(kDir) + PathSeparator() + "sub";
    CHECK(MakeDirectory(kDir));
    CHECK(MakeDirectory(dir));
    CHECK(MakeDirectory(dir));            // already there is success, not failure
    CHECK(FileOrDirectoryExists(dir));

    CHECK(Put(dir + PathSeparator() + "a.txt", "a"));
    CHECK(Put(dir + PathSeparator() + "b.txt", "bb"));
    std::vector<std::string> names;
    CHECK(ListDirectory(dir, names));
    CHECK(names.size() == 2);             // "." and ".." are not entries
    CHECK(!ListDirectory(dir + PathSeparator() + "absent", names));

    // A directory inside means something unexpected is there, so the removal refuses -- and
    // refuses before deleting anything, so the files it would have taken are still present.
    const std::string nested = dir + PathSeparator() + "nested";
    CHECK(MakeDirectory(nested));
    CHECK(!RemoveFlatDirectory(dir));
    CHECK(ListDirectory(dir, names));
    CHECK(names.size() == 3);
    CHECK(RemoveEmptyDirectory(nested));

    CHECK(RemoveFlatDirectory(dir));
    CHECK(!FileOrDirectoryExists(dir));
    CHECK(RemoveFlatDirectory(dir));      // gone already is success
    CHECK(RemoveFile(dir + PathSeparator() + "a.txt"));   // so is removing what is absent
    CHECK(RemoveEmptyDirectory(dir));
}

void TestMovePath()
{
    CHECK(MakeDirectory(kDir));
    const std::string from = P("from.bin");
    const std::string to = P("to.bin");
    CHECK(Put(from, "payload"));
    CHECK(MovePath(from, to));
    CHECK(!FileOrDirectoryExists(from));
    std::string got;
    CHECK(Slurp(to, got));
    CHECK(got == "payload");
    CHECK(!MovePath(P("absent.bin"), to));
    CHECK(!MovePath("", to));
    CHECK(RemoveFile(to));
}

}  // namespace

int main()
{
    TestWriteAndReadBack();
    TestCloseFailureIsReported();
    TestFailedWriteKeepsThePreviousFile();
    TestOpenFailureIsReported();
    TestDirectoryHelpers();
    TestMovePath();
    RemoveFlatDirectory(kDir);

    if (gFailures != 0)
    {
        std::cerr << gFailures << " file-write check(s) failed\n";
        return 1;
    }
    std::cout << "FileWriteTests: all checks passed\n";
    return 0;
}
