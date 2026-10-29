// Which emulators the Emulator menu and Launch Settings offer. Both are drawn from Launcher::Emulators(),
// so this is what a user can pick: Mednafen only. A settings file written when Yabause was offered must
// still load, select Mednafen, and keep its old Yabause keys rather than drop them.
#include "Launcher.h"
#include "Settings.h"

#include <cstdio>
#include <string>

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

    if (gFailures == 0) std::printf("LauncherEmulators: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
