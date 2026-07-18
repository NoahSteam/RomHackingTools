// Windows entry point. Wires the Win32/D3D11 platform to the portable App and
// runs the main loop: PumpEvents -> BeginFrame -> App::BuildUI -> EndFrame.
// This is the only platform-specific glue; everything the app does lives in
// the portable App class.
#include <windows.h>

#include <cstdio>    // snprintf
#include <cstdlib>   // __argc / __argv
#include <cstring>

#include "WindowsPlatform.h"
#include "App.h"
#include "SeLiveProtocol.h"   // SE_LIVE_VERSION (live-tap protocol version)

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    sfe::WindowsPlatform platform;
    sfe::PlatformConfig config;
    // Show the live-tap protocol version in the title so a client/emulator
    // mismatch (which breaks live capture) is obvious at a glance.
    char title[64];
    std::snprintf(title, sizeof(title), "Saturn Explorer  \xE2\x80\x94  live proto v%u", SE_LIVE_VERSION);
    config.mTitle = title;
    if (!platform.Initialize(config))
    {
        return 1;
    }

    sfe::App app;
    app.Initialize();

    // Auto-connect to a running Yabause on boot, retrying until one appears
    // (see the Integration/Yabause module). `--live [endpoint]` picks a specific
    // endpoint; otherwise the default socket/pipe is polled. Files open from the
    // toolbar and stop the polling.
    const char* liveEndpoint = nullptr;
    for (int i = 1; i < __argc; ++i)
    {
        if (std::strcmp(__argv[i], "--live") == 0)
        {
            liveEndpoint = (i + 1 < __argc) ? __argv[i + 1] : nullptr;
            break;
        }
    }
    app.EnableLiveAutoConnect(liveEndpoint);

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
