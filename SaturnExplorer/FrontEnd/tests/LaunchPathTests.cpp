// A complete launch with a relative ROM: the argument builder, AbsolutePath and the real spawn, against a
// disposable "emulator" that starts in its own folder and checks whether the ROM it was handed exists.
// The helper alone can classify a path wrongly and still pass its own unit test; this is the check that
// the file the user picked is the file the emulator is given -- including POSIX names that look like
// Windows paths (a leading backslash, a "C:" prefix).
#include "Launcher.h"
#include "Settings.h"
#include "PosixSpawn.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace sfe;

namespace
{
int gFailures;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)

void Write(const std::string& path, const std::string& text, mode_t mode = 0644)
{
    std::ofstream(path, std::ios::binary) << text;
    ::chmod(path.c_str(), mode);
}

std::string ReadWhenReady(const std::string& path)
{
    for (int i = 0; i < 500; ++i)
    {
        std::ifstream in(path);
        std::string line;
        if (in && std::getline(in, line)) return line;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return std::string();
}

// What the emulator reports for 'rom' given as the user picked it (relative to SE's directory).
std::string Launch(const std::string& seDir, const std::string& emuDir, const std::string& rom, bool resolve)
{
    const std::string out = emuDir + "/result.txt";
    std::remove(out.c_str());
    CHECK(::chdir(seDir.c_str()) == 0);
    const std::vector<std::string> argv =
        BuildLaunchArgv("\"{rom}\"", resolve ? AbsolutePath(rom) : rom, std::string());
    int pid = -1;
    std::string error;
    // Started the way the app starts it: in the emulator's own folder, not SE's.
    CHECK(SpawnChild(emuDir + "/emu.sh", argv, emuDir, {}, pid, error));
    const std::string answer = ReadWhenReady(out);
    if (pid > 0) { int st = 0; ::waitpid(pid, &st, 0); }
    return answer;
}
}  // namespace

int main()
{
    const char* t = std::getenv("TMPDIR");
    std::string buf = std::string(t && *t ? t : "/tmp") + "/se_launchpath_XXXXXX";
    if (!::mkdtemp(&buf[0])) { std::printf("no temp dir\n"); return 2; }
    char* real = ::realpath(buf.c_str(), nullptr);   // the temp dir may sit behind a symlink
    const std::string dir = real ? real : buf;
    std::free(real);
    char* here = ::getcwd(nullptr, 0);

    const std::string se = dir + "/se", emu = dir + "/tools";
    std::system(("mkdir -p '" + se + "' '" + emu + "' '" + se + "/games'").c_str());
    Write(emu + "/emu.sh",
          "#!/bin/sh\nif [ -f \"$1\" ]; then r=found; else r=missing; fi\nprintf '%s\\n' \"$r\" > result.txt.tmp\nmv result.txt.tmp result.txt\n",
          0755);
    Write(se + "/disc.cue", "x");
    Write(se + "/\\disc.cue", "x");          // a POSIX file whose name starts with a backslash
    Write(se + "/C:disc.cue", "x");          // ... or looks drive-relative
    Write(se + "/games/a b.cue", "x");

    for (const char* rom : { "disc.cue", "\\disc.cue", "C:disc.cue", "games/a b.cue" })
        CHECK(Launch(se, emu, rom, true) == "found");

    // The control: handed over as picked, the emulator looks in its own folder and does not find it.
    CHECK(Launch(se, emu, "disc.cue", false) == "missing");

    // The Mednafen a release ships: beside the program, or beside the .app when the program sits inside one.
    std::system(("mkdir -p '" + dir + "/plain/mednafen-se' '" + dir + "/mac/mednafen-se' '" + dir +
                 "/mac/SaturnExplorer.app/Contents/MacOS' '" + dir + "/bare'").c_str());
    Write(dir + "/plain/mednafen-se/mednafen", "x", 0755);
    Write(dir + "/mac/mednafen-se/mednafen", "x", 0755);
    CHECK(BundledMednafenPath(dir + "/plain/SaturnExplorerFrontEnd") == dir + "/plain/mednafen-se/mednafen");
    CHECK(BundledMednafenPath(dir + "/mac/SaturnExplorer.app/Contents/MacOS/SaturnExplorer") ==
          dir + "/mac/mednafen-se/mednafen");
    CHECK(BundledMednafenPath(dir + "/bare/SaturnExplorerFrontEnd").empty());   // developer build: nothing beside it
    CHECK(BundledMednafenPath("SaturnExplorerFrontEnd").empty());               // no folder to look in
    CHECK(BundledMednafenPath("").empty());
    // An .app further up the path is not the bundle the program sits in.
    std::system(("mkdir -p '" + dir + "/outer/Outer.app/Contents/Tools/Inner/mednafen-se' '" + dir + "/outer/mednafen-se'").c_str());
    Write(dir + "/outer/mednafen-se/mednafen", "x", 0755);
    Write(dir + "/outer/Outer.app/Contents/Tools/Inner/mednafen-se/mednafen", "x", 0755);
    CHECK(BundledMednafenPath(dir + "/outer/Outer.app/Contents/Tools/Inner/Tool") ==
          dir + "/outer/Outer.app/Contents/Tools/Inner/mednafen-se/mednafen");

    // Load and Save: the bundled copy is used when no path is recorded and is not written back; a recorded
    // path -- present or not -- is never replaced.
    {
        const std::string exe = dir + "/plain/SaturnExplorerFrontEnd", bundled = dir + "/plain/mednafen-se/mednafen";
        Settings none;
        Launcher a;
        a.Load(none, exe);
        CHECK(a.Emulators()[0].exePath == bundled);
        CHECK(a.UsesBundledMednafen());
        Settings out;
        a.Save(out);
        CHECK(out.Get("emulators", "mednafen", "x").empty());
        Launcher again;
        again.Load(out, dir + "/mac/SaturnExplorer.app/Contents/MacOS/SaturnExplorer");   // moved elsewhere
        CHECK(again.Emulators()[0].exePath == dir + "/mac/mednafen-se/mednafen");

        Settings custom;
        custom.Set("emulators", "mednafen", "/Volumes/gone/mednafen");
        Launcher b;
        b.Load(custom, exe);
        CHECK(b.Emulators()[0].exePath == "/Volumes/gone/mednafen");
        CHECK(!b.UsesBundledMednafen());
        Settings kept;
        b.Save(kept);
        CHECK(kept.Get("emulators", "mednafen", "") == "/Volumes/gone/mednafen");

        Launcher dev;
        dev.Load(none, dir + "/bare/SaturnExplorerFrontEnd");   // developer build: nothing beside it
        CHECK(dev.Emulators()[0].exePath.empty());
        CHECK(!dev.UsesBundledMednafen());
    }
    CHECK(MednafenFirmwareDir("/opt/se/mednafen-se/mednafen") == "/opt/se/mednafen-se/firmware");
    CHECK(MednafenFirmwareDir("mednafen").empty());

    CHECK(::chdir(here) == 0);
    std::free(here);
    std::system(("rm -rf '" + dir + "'").c_str());
    if (gFailures == 0) std::printf("LaunchPath: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
