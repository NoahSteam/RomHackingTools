// SpawnDetached -- the launcher behind LaunchTool on macOS/Linux. It used to hand one command line to
// /bin/sh and report success as soon as the first fork worked. These pin the two things that went
// wrong: arguments reaching the program literally (a `$` or a quote in a path was reinterpreted), and a
// launch that did not happen being reported as one that did.
#include "PosixSpawn.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace sfe;

namespace
{
int gFailures;

#define CHECK(cond) do {                                                      \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } \
} while (0)

std::string MakeTempDir()
{
    const char* t = std::getenv("TMPDIR");
    std::string tmpl = std::string(t && *t ? t : "/tmp") + "/se_spawn_test_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    return ::mkdtemp(buf.data()) ? std::string(buf.data()) : std::string();
}

void WriteFile(const std::string& path, const std::string& text, mode_t mode)
{
    std::ofstream(path, std::ios::binary) << text;
    ::chmod(path.c_str(), mode);
}

// The lines of 'path', once it exists and its writer has finished (the tool is detached, so the test
// has to wait for it).
bool WaitForLines(const std::string& path, size_t want, std::vector<std::string>& lines)
{
    for (int i = 0; i < 500; ++i)
    {
        std::ifstream in(path);
        lines.clear();
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
        if (lines.size() >= want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void TestArgumentsArriveLiterally(const std::string& dir)
{
    const std::string out = dir + "/argv.txt", tool = dir + "/tool.sh";
    // First line is where it ran; then one line per argument, then a sentinel (an empty argument must
    // still produce its own empty line).
    WriteFile(tool, "#!/bin/sh\nexec > \"" + out + ".tmp\"\npwd\nfor a in \"$@\"; do printf '%s\\n' \"$a\"; done\nprintf 'END\\n'\nmv \"" + out + ".tmp\" \"" + out + "\"\n", 0755);
    const std::vector<std::string> args = { "two words", "cfg$SE_UNSET/A", "`id`", "it's", "\"q\"", "", "{b}", "*", "a;b" };
    std::string error;
    CHECK(SpawnDetached(tool, args, dir, error));
    CHECK(error.empty());
    std::vector<std::string> lines;
    CHECK(WaitForLines(out, args.size() + 2, lines));
    if (lines.size() == args.size() + 2)
    {
        char* real = ::realpath(dir.c_str(), nullptr);
        CHECK(lines[0] == (real ? real : dir) || lines[0] == dir);   // the working directory was applied
        std::free(real);
        for (size_t i = 0; i < args.size(); ++i) CHECK(lines[1 + i] == args[i]);
        CHECK(lines.back() == "END");
    }
}

void TestFailuresAreReported(const std::string& dir)
{
    std::string error;
    CHECK(!SpawnDetached(dir + "/does/not/exist", {}, "", error));
    CHECK(error.find("does/not/exist") != std::string::npos);
    CHECK(error.find(std::strerror(ENOENT)) != std::string::npos);

    error.clear();
    CHECK(!SpawnDetached("se_definitely_no_such_tool_on_path", {}, "", error));   // a PATH lookup that finds nothing
    CHECK(error.find("se_definitely_no_such_tool_on_path") != std::string::npos);

    // Present, but not executable.
    const std::string plain = dir + "/not_executable";
    WriteFile(plain, "#!/bin/sh\n", 0644);
    error.clear();
    CHECK(!SpawnDetached(plain, {}, "", error));
    CHECK(error.find(std::strerror(EACCES)) != std::string::npos);

    // A working directory that cannot be entered.
    const std::string tool = dir + "/ok.sh";
    WriteFile(tool, "#!/bin/sh\nexit 0\n", 0755);
    error.clear();
    CHECK(!SpawnDetached(tool, {}, dir + "/no_such_folder", error));
    CHECK(error.find("working directory") != std::string::npos);

    // A macOS application bundle goes through `open`, which this host does not have.
    error.clear();
    if (::access("/usr/bin/open", X_OK) != 0)
    {
        CHECK(!SpawnDetached(dir + "/Some.app", {}, "", error));
        CHECK(!error.empty());
    }

    CHECK(!SpawnDetached("", {}, "", error) && !error.empty());
}

// The double fork exists so nothing is left to reap: a launched tool must not leave a child behind.
void TestNoZombie(const std::string& dir)
{
    const std::string tool = dir + "/quick.sh";
    WriteFile(tool, "#!/bin/sh\nexit 0\n", 0755);
    std::string error;
    CHECK(SpawnDetached(tool, {}, dir, error));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    int status = 0;
    errno = 0;
    CHECK(::waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);   // no child of ours, running or zombie
}
}  // namespace

int main()
{
    const std::string dir = MakeTempDir();
    if (dir.empty()) { std::printf("could not make a temp directory\n"); return 2; }
    TestArgumentsArriveLiterally(dir);
    TestFailuresAreReported(dir);
    TestNoZombie(dir);
    std::system(("rm -rf '" + dir + "'").c_str());
    if (gFailures == 0) std::printf("PosixSpawn: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
