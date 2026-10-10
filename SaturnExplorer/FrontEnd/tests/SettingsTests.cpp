// Settings::SaveTo -- a failed save must not cost the settings already on disk, and must say it failed.
// It used to open the file with truncation and report the stream's state before the destructor flushed:
// a write that failed left the previous settings emptied and still returned true.
#include "Settings.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

using namespace sfe;

namespace
{
int gFailures;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)

std::string ReadAll(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
}  // namespace

int main()
{
    const char* t = std::getenv("TMPDIR");
    std::string tmpl = std::string(t && *t ? t : "/tmp") + "/se_settings_test_XXXXXX";
    std::string buf = tmpl;
    if (!::mkdtemp(&buf[0])) { std::printf("no temp dir\n"); return 2; }
    const std::string dir = buf, path = dir + "/settings.ini";

    Settings s;
    s.Set("data", "dir", "/games/one");
    std::string error;
    CHECK(s.SaveTo(path, &error));
    CHECK(error.empty());
    const std::string first = ReadAll(path);
    CHECK(first.find("dir = /games/one") != std::string::npos);

    // The new content replaces the old whole, and survives a round trip.
    s.Set("data", "dir", "/games/two");
    CHECK(s.SaveTo(path, &error));
    CHECK(ReadAll(path).find("dir = /games/two") != std::string::npos);

    // A folder that cannot take the staged file: the save fails, says why, and the file is untouched.
    const std::string good = ReadAll(path);
    s.Set("data", "dir", "/games/three");
    ::chmod(dir.c_str(), 0555);
    if (::geteuid() != 0)   // root writes into a read-only folder regardless
    {
        error.clear();
        CHECK(!s.SaveTo(path, &error));
        CHECK(!error.empty());
        CHECK(ReadAll(path) == good);
    }
    ::chmod(dir.c_str(), 0755);

    // A destination whose writes fail at the flush (a full disk): reported, not swallowed.
    if (::access("/dev/full", W_OK) == 0)
    {
        error.clear();
        CHECK(!s.SaveTo("/dev/full", &error));
        CHECK(!error.empty());
    }

    // A path that does not exist.
    error.clear();
    CHECK(!s.SaveTo(dir + "/no/such/folder/settings.ini", &error));
    CHECK(!error.empty());

    std::system(("rm -rf '" + dir + "'").c_str());
    if (gFailures == 0) std::printf("Settings: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
