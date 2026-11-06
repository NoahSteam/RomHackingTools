// Area 11 round 2: does Settings::SaveTo's A9-19 guard (a file Load refused is not saved over) matter on its own?
// The lifecycle oracle refuses an oversize settings.ini at the save too (the save reads the file again), so the guard
// has a second line behind it. This probe makes the case the second line does not cover: the file is refused at Load
// (over 4 MiB), then is back to a readable size before the save (the user trimmed it; another instance rewrote it).
// The session then holds no settings of the file, sets one key as App does for every key at each save (A9-35), and saves.
// Correct: the save is refused (exit 0). With the guard removed (mutant T-refused): the user's key is overwritten with the
// session's value (exit 1).
//
// build (from the repository root; XDG_CONFIG_HOME picks the folder):
//   g++ -std=c++17 -O1 -ISaturnExplorer/FrontEnd/src SaturnExplorer/Docs/Decompiler/tools/a11_round2/settings_refused_probe.cpp \
//       SaturnExplorer/FrontEnd/src/Settings.cpp SaturnExplorer/FrontEnd/src/FileWrite.cpp -o <scratch>/probe
//   XDG_CONFIG_HOME=<empty dir> <scratch>/probe
#include "Settings.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

int main()
{
    sfe::Settings::EnsureConfigDir();   // the folder Load and Save use
    const std::string path = sfe::Settings::FilePath();
    if (path.empty()) { std::printf("no settings path\n"); return 2; }
    {
        std::ofstream f(path, std::ios::binary);
        f << "[bios]\npath = /home/user/bios/sega_101.bin\n";
        f << "; padding\n" << std::string(5u << 20, 'x') << "\n";   // over the 4 MiB Load reads
    }
    sfe::Settings s;
    std::string err;
    const bool loaded = s.Load(&err);
    std::printf("Load: %s (%s)\n", loaded ? "ok" : "refused", err.c_str());
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);   // back to a readable file before the save
        f << "[bios]\npath = /home/user/bios/sega_101.bin\n";
    }
    s.Set("bios", "path", "");   // App::SaveSettings sets every key at each save (A9-35): the session's empty default
    const bool saved = s.Save(&err);
    std::ifstream in(path, std::ios::binary);
    std::stringstream b;
    b << in.rdbuf();
    std::printf("Save: %s (%s)\nfile now:\n%s", saved ? "saved" : "refused", err.c_str(), b.str().c_str());
    const bool kept = b.str().find("sega_101.bin") != std::string::npos;
    std::printf("%s\n", kept ? "the user's BIOS path is kept" : "LOST: the user's BIOS path was overwritten by the session's default");
    return kept ? 0 : 1;
}
