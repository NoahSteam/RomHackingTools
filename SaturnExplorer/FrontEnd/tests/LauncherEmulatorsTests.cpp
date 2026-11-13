// Which emulators the Emulator menu and Launch Settings offer. Both are drawn from Launcher::Emulators(),
// so this is what a user can pick: Mednafen only. A settings file written when Yabause was offered must
// still load, select Mednafen, and keep its old Yabause keys rather than drop them.
#include "Launcher.h"
#include "Settings.h"

#include <cstdio>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#define SE_MKDIR(p) _mkdir(p)
#define SE_RMDIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define SE_MKDIR(p) ::mkdir(p, 0755)
#define SE_RMDIR(p) ::rmdir(p)
#endif

using namespace sfe;

namespace
{
int gFailures;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)
}  // namespace

int main()
{
    // No bundled Mednafen next to this (non-existent) program path.
    const std::string exe = "nowhere/SaturnExplorerFrontEnd";

    {
        Settings none;
        Launcher l;
        l.Load(none, exe);
        CHECK(l.Emulators().size() == 1);
        CHECK(l.Emulators()[0].key == "mednafen" && l.Emulators()[0].label == "Mednafen");
        CHECK(l.Selected() != nullptr && l.Selected()->key == "mednafen");
        for (const EmulatorSpec& e : l.Emulators()) CHECK(e.key != "yabause");
    }

    {
        Settings old;
        old.Set("launch", "emulator", "yabause");
        old.Set("emulators", "yabause", "C:/old/yabause-qt.exe");
        old.Set("launch.yabause", "args", "-a -i \"{rom}\"");
        old.Set("emulators", "mednafen", "C:/emu/mednafen.exe");
        Launcher l;
        l.Load(old, exe);
        CHECK(l.Emulators().size() == 1);
        CHECK(l.Selected() != nullptr && l.Selected()->key == "mednafen");   // the removed selection falls back
        CHECK(l.Emulators()[0].exePath == "C:/emu/mednafen.exe");

        l.Save(old);
        CHECK(old.Get("launch", "emulator", "") == "mednafen");
        CHECK(old.Get("emulators", "yabause", "") == "C:/old/yabause-qt.exe");   // left in the file
        CHECK(old.Get("launch.yabause", "args", "") == "-a -i \"{rom}\"");
    }

    // Executable set to the release's mednafen-se folder: it stands for the Mednafen inside, so the BIOS is
    // looked for in mednafen-se/firmware, not beside the folder. Runs on Windows too (mednafen.exe there),
    // where the POSIX-only LaunchPathTests do not.
    {
#ifdef _WIN32
        const char* leaf = "mednafen.exe";
        const std::string sep = "\\";
#else
        const char* leaf = "mednafen";
        const std::string sep = "/";
#endif
        const std::string folder = "se_launcher_resolve_test", inside = folder + sep + leaf;
        SE_MKDIR(folder.c_str());
        std::ofstream(inside.c_str(), std::ios::binary) << "x";
        CHECK(ResolveMednafenExe(folder) == inside);
        CHECK(ResolveMednafenExe(inside) == inside);
        CHECK(MednafenFirmwareDir(ResolveMednafenExe(folder)) == folder + sep + "firmware");

        Settings saved;
        saved.Set("emulators", "mednafen", folder);
        Launcher l;
        l.Load(saved, exe);
        CHECK(l.Emulators()[0].exePath == inside);

        std::remove(inside.c_str());
        CHECK(ResolveMednafenExe(folder) == folder);   // an empty folder is left for Validate to reject
        Launcher bad;
        bad.Load(saved, exe);
        bad.SetRom(inside + ".cue");
        std::ofstream((inside + ".cue").c_str()) << "x";
        const LaunchValidation v = bad.Validate();
        CHECK(!v.valid && v.message.find("folder") != std::string::npos);
        std::remove((inside + ".cue").c_str());
        SE_RMDIR(folder.c_str());
    }

    if (gFailures == 0) std::printf("LauncherEmulators: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
