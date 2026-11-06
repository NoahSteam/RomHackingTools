// A12-25: a path with characters outside the ANSI code page (a Japanese folder name) must open.
//
// On Windows the CRT's fopen reads a char* in the ANSI code page, so a savestate given on the command line from such a folder
// did not open and nothing said why. The fix opens through saturnexplorer/SeFilePath.h (UTF-8 to UTF-16, the wide call). This
// test runs on Linux, where the native call already takes UTF-8, so it cannot show the Windows failure; it pins what Linux can:
//   * the UTF-8 <-> UTF-16 converters the Windows path depends on (shared code, built on every platform), including the
//     rejection of malformed UTF-8 that makes the callers fall back to the ANSI call;
//   * that every layer a user-chosen file goes through (FileWrite's folder, staging and publish calls, ReadFileCapped, the
//     savestate driver, the directory listing, SameFile and CanonicalPath) is handed a non-ASCII path and returns the file.
// The Windows bot runs this same test on Windows, where it does exercise the wide calls.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "FileRead.h"
#include "FileWrite.h"
#include "SavestateDriver.h"
#include "saturnexplorer/SeFilePath.h"

using namespace sfe;

namespace
{
int gFailures;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)

std::string TempRoot()
{
    const char* t = std::getenv("TMPDIR");
#ifdef _WIN32
    if (!t || !*t) t = std::getenv("TEMP");
    std::string base = t && *t ? t : ".";
#else
    std::string base = t && *t ? t : "/tmp";
#endif
    while (base.size() > 1 && (base.back() == '/' || base.back() == '\\')) base.pop_back();
    return base + PathSeparator() + "se_utf8path_test_" + std::to_string(static_cast<unsigned long long>(std::time(nullptr)));
}

// "テスト" and "ステート" in UTF-8, written as bytes so the source file's own encoding does not matter.
const char kDir[]  = "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88";
const char kFile[] = "\xE3\x82\xB9\xE3\x83\x86\xE3\x83\xBC\xE3\x83\x88.bin";

void TestConverters()
{
    std::wstring w;
    CHECK(se::TryUtf8ToWide("plain.bin", w) && w == L"plain.bin");
    CHECK(se::TryUtf8ToWide(kDir, w) && w.size() == 3 && w[0] == 0x30C6 && w[1] == 0x30B9 && w[2] == 0x30C8);
    CHECK(se::WideToUtf8(w) == kDir);
    CHECK(se::TryUtf8ToWide("\xC3\xA9", w) && w.size() == 1 && w[0] == 0x00E9);               // e acute
    CHECK(se::TryUtf8ToWide("\xF0\x9F\x8E\xAE", w) && w.size() == 2 &&                         // U+1F3AE: a surrogate pair
          static_cast<unsigned short>(w[0]) == 0xD83C && static_cast<unsigned short>(w[1]) == 0xDFAE);
    CHECK(se::WideToUtf8(w) == "\xF0\x9F\x8E\xAE");
    CHECK(se::TryUtf8ToWide("", w) && w.empty());
    // Not UTF-8: a lone lead byte, a truncated sequence, an overlong form, a surrogate, a stray continuation, past U+10FFFF.
    CHECK(!se::TryUtf8ToWide("\xE3", w));
    CHECK(!se::TryUtf8ToWide("a\xE3\x83", w));
    CHECK(!se::TryUtf8ToWide("\xC0\x80", w));
    CHECK(!se::TryUtf8ToWide("\xED\xA0\x80", w));
    CHECK(!se::TryUtf8ToWide("\x80", w));
    CHECK(!se::TryUtf8ToWide("\xF4\x90\x80\x80", w));
    CHECK(!se::TryUtf8ToWide("\xE3\x41\x42", w));
}

void TestRoundTripThroughEveryLayer()
{
    const std::string root = TempRoot();
    CHECK(MakeDirectory(root));
    const std::string dir = root + PathSeparator() + kDir;
    const std::string file = dir + PathSeparator() + kFile;
    CHECK(MakeDirectory(dir));
    CHECK(FileOrDirectoryExists(dir));

    std::vector<unsigned char> bytes(4096);
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<unsigned char>(i * 7 + 3);
    std::string error;
    CHECK(WriteFileAtomically(file, bytes.data(), bytes.size(), error));   // staging file, write, publish, all beside the file
    CHECK(error.empty());
    CHECK(WriteFileAtomically(file, bytes.data(), bytes.size(), error));   // and replacing it

    // ListDirectory returns the name as UTF-8 again, and only the file (no staging file left).
    std::vector<std::string> names;
    CHECK(ListDirectory(dir, names) && names.size() == 1 && names[0] == kFile);

    std::string back;
    CHECK(ReadFileCapped(file, 1u << 20, back) == CappedRead::Ok);
    CHECK(back.size() == bytes.size() && std::memcmp(back.data(), bytes.data(), bytes.size()) == 0);
    CHECK(ReadFileCapped(dir + PathSeparator() + "missing.bin", 1u << 20, back) == CappedRead::Missing);
    CHECK(ReadFileCapped(file, 100, back) == CappedRead::TooLarge);

    // The savestate driver: the call that failed silently on Windows. A raw dump of any size opens through the same LoadFile
    // as a savestate; the path and the buffer entry points must agree.
    se_data_source viaPath{}, viaBuffer{};
    const se_result a = se_savestate_open_full_dump(file.c_str(), 0x06000000u, &viaPath);
    const se_result b = se_savestate_open_full_dump_buffer(bytes.data(), bytes.size(), 0x06000000u, &viaBuffer);
    CHECK(a == b);
    CHECK(a != SE_ERR_IO);
    if (viaPath.close) viaPath.close(viaPath.user);
    if (viaBuffer.close) viaBuffer.close(viaBuffer.user);
    se_data_source none{};
    CHECK(se_savestate_open_full_dump((dir + PathSeparator() + "missing.bin").c_str(), 0x06000000u, &none) == SE_ERR_IO);

    // The stdio helper itself, read and write.
    std::FILE* f = se::OpenFile(dir + PathSeparator() + "second.bin", "wb");
    CHECK(f != nullptr);
    if (f) { std::fputc('x', f); std::fclose(f); }
    f = se::OpenFile(dir + PathSeparator() + "second.bin", "rb");
    CHECK(f != nullptr);
    if (f) { CHECK(std::fgetc(f) == 'x'); std::fclose(f); }
    CHECK(se::OpenFile(dir + PathSeparator() + "nothing.bin", "rb") == nullptr);

    CHECK(SameFile(file, file));
    CHECK(!CanonicalPath(file).empty());
    CHECK(PathIsWithin(file, root));
    CHECK(!ExistsAsNonRegularFile(file));

    CHECK(RemoveFile(dir + PathSeparator() + "second.bin"));
    CHECK(RemoveFlatDirectory(dir));
    CHECK(!FileOrDirectoryExists(dir));
    CHECK(RemoveEmptyDirectory(root));
}
}  // namespace

int main()
{
    TestConverters();
    TestRoundTripThroughEveryLayer();
    if (gFailures == 0) std::printf("Utf8Path: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
