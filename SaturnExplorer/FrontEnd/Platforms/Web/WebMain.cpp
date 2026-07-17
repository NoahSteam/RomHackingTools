// WebMain — entry point for the SDL2/WebGL frontend. On the web it registers an
// Emscripten main loop (the browser drives it via requestAnimationFrame) and
// exposes se_web_load_file() for JS to hand in a savestate's bytes. Built
// natively it runs an ordinary event loop, so the same App + WebPlatform can be
// exercised as a desktop app for verification.

#include <cstddef>
#include <cstdint>

#include "App.h"
#include "WebPlatform.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <cstdio>
#include <cstring>
#include <vector>
#endif

namespace
{
sfe::WebPlatform gPlatform;
sfe::App         gApp;
bool             gReady = false;

#ifdef __EMSCRIPTEN__
// The per-frame body the browser calls via requestAnimationFrame. The native
// build uses an explicit loop in main() instead.
void FrameOnce()
{
    gPlatform.PumpEvents();
    gPlatform.BeginFrame();
    gApp.BuildUI(gPlatform);
    gPlatform.EndFrame();
}
#endif
}  // namespace

extern "C"
{
// Called from JS after a file is read into WASM memory: hands the savestate/dump
// bytes to the App, which parses them into a fresh context. The App copies what
// it needs, so JS may free the buffer immediately after this returns.
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void se_web_load_file(const uint8_t* data, size_t size)
{
    if (gReady && data && size)
    {
        gApp.OpenSavestateBuffer(data, size);
    }
}

// Called from JS on browser resize with the new logical (CSS-pixel) canvas size.
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void se_web_resize(int width, int height)
{
    if (gReady)
    {
        gPlatform.Resize(width, height);
    }
}
}  // extern "C"

int main(int argc, char** argv)
{
    sfe::PlatformConfig config;
    if (!gPlatform.Initialize(config))
    {
        return 1;
    }
    gApp.Initialize();
    gReady = true;

#ifdef __EMSCRIPTEN__
    (void)argc;
    (void)argv;
    // fps = 0 → drive from requestAnimationFrame; simulate_infinite_loop = 1 so
    // main() does not return and tear down the context.
    emscripten_set_main_loop(FrameOnce, 0, 1);
#else
    // Native (desktop) convenience: `--live [endpoint]` connects to a running
    // Yabause; otherwise a savestate/dump path loads it (no browser picker here).
    if (argc > 1 && std::strcmp(argv[1], "--live") == 0)
    {
        gApp.OpenLive(argc > 2 ? argv[2] : nullptr);
    }
    else if (argc > 1)
    {
        if (FILE* f = std::fopen(argv[1], "rb"))
        {
            std::fseek(f, 0, SEEK_END);
            long n = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (n > 0)
            {
                std::vector<uint8_t> bytes(static_cast<size_t>(n));
                if (std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size())
                {
                    gApp.OpenSavestateBuffer(bytes.data(), bytes.size());
                }
            }
            std::fclose(f);
        }
    }
    while (gPlatform.PumpEvents())
    {
        gPlatform.BeginFrame();
        gApp.BuildUI(gPlatform);
        gPlatform.EndFrame();
    }
    gApp.Shutdown();
    gPlatform.Shutdown();
#endif
    return 0;
}
