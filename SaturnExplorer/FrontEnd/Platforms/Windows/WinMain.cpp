// Windows entry point. Wires the Win32/D3D11 platform to the portable App and
// runs the main loop: PumpEvents -> BeginFrame -> App::BuildUI -> EndFrame.
// This is the only platform-specific glue; everything the app does lives in
// the portable App class.
#include <windows.h>

#include <cstdio>    // snprintf
#include <cstdlib>   // __argc / __wargv
#include <string>
#include <vector>

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

    // Auto-connect to a running patched emulator on boot, retrying until one appears.
    // `--live [endpoint]` picks a specific
    // endpoint; otherwise the default socket/pipe is polled. Files open from the
    // toolbar and stop the polling.
    //
    // The arguments come from __wargv: this is a wWinMain program, so the CRT fills only the wide
    // array and __argv is null -- reading it crashed the app whenever any argument was given. Each
    // is converted to UTF-8, the encoding the rest of the app uses for paths and endpoints.
    std::vector<std::string> args;
    if (__wargv)
        for (int i = 0; i < __argc; ++i)
        {
            const wchar_t* w = __wargv[i];
            const int n = w ? WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr) : 0;
            std::string a(n > 0 ? static_cast<size_t>(n - 1) : 0, '\0');
            if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &a[0], n, nullptr, nullptr);
            args.push_back(a);
        }
    const char* liveEndpoint = nullptr;
    for (size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == "--live")
        {
            liveEndpoint = (i + 1 < args.size()) ? args[i + 1].c_str() : nullptr;
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
