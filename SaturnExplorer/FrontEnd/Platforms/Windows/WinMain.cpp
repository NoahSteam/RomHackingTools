// Windows entry point. Wires the Win32/D3D11 platform to the portable App and
// runs the main loop: PumpEvents -> BeginFrame -> App::BuildUI -> EndFrame.
// This is the only platform-specific glue; everything the app does lives in
// the portable App class.
#include <windows.h>

#include <cstdlib>   // __argc / __argv
#include <cstring>

#include "WindowsPlatform.h"
#include "App.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    sfe::WindowsPlatform platform;
    sfe::PlatformConfig config;
    if (!platform.Initialize(config))
    {
        return 1;
    }

    sfe::App app;
    app.Initialize();

    // `--live [endpoint]` connects to a running Yabause on startup (see the
    // Integration/Yabause module). Otherwise the user opens files from the toolbar.
    for (int i = 1; i < __argc; ++i)
    {
        if (std::strcmp(__argv[i], "--live") == 0)
        {
            app.OpenLive((i + 1 < __argc) ? __argv[i + 1] : nullptr);
            break;
        }
    }

    while (platform.PumpEvents())
    {
        platform.BeginFrame();
        app.BuildUI(platform);
        platform.EndFrame();
    }

    app.Shutdown();
    platform.Shutdown();
    return 0;
}
