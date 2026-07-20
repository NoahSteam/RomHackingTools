// Windows entry point. Wires the Win32/D3D11 platform to the portable App and
// runs the main loop: PumpEvents -> BeginFrame -> App::BuildUI -> EndFrame.
// This is the only platform-specific glue; everything the app does lives in the
// portable App class.
#include <windows.h>

#include <cstdio>    // fopen for the optional launch-argument file
#include <cstdlib>   // __argc / __argv
#include <vector>

#include "WindowsPlatform.h"
#include "App.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    ak::WindowsPlatform platform;
    ak::PlatformConfig config;
    config.mTitle = "AppKit App";
    if (!platform.Initialize(config))
    {
        return 1;
    }

    ak::App app;
    app.Initialize();

    // Optional: load a file given on the command line so OnFileLoaded can be
    // driven without the Open dialog.
    if (__argc > 1)
    {
        if (FILE* f = std::fopen(__argv[1], "rb"))
        {
            std::fseek(f, 0, SEEK_END);
            long n = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (n > 0)
            {
                std::vector<uint8_t> bytes(static_cast<size_t>(n));
                if (std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size())
                {
                    app.OnFileLoaded(bytes.data(), bytes.size());
                }
            }
            std::fclose(f);
        }
    }

    while (platform.PumpEvents() && !app.WantsQuit())
    {
        platform.BeginFrame();
        app.BuildUI(platform);
        platform.EndFrame();
    }

    app.Shutdown();
    platform.Shutdown();
    return 0;
}
