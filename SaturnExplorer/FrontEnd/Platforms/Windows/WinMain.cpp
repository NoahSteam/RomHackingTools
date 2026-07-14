// Windows entry point. Wires the Win32/D3D11 platform to the portable App and
// runs the main loop: PumpEvents -> BeginFrame -> App::BuildUI -> EndFrame.
// This is the only platform-specific glue; everything the app does lives in
// the portable App class.
#include <windows.h>

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
