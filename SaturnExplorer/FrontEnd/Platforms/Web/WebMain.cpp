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

#include <SDL.h>
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
#ifndef __EMSCRIPTEN__
    // `--selftest`: load, start SDL without a window, and exit 0. A packaged .app can pass
    // `codesign --verify` and still fail to start: dyld rejects a bundled dylib (library
    // validation), or Homebrew's sdl2-compat cannot find the SDL3 it loads at run time. Launching
    // the bundled binary with this flag exercises both, headlessly, so CI can gate on it.
    if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0)
    {
        if (SDL_Init(SDL_INIT_EVENTS) != 0)
        {
            std::fprintf(stderr, "selftest: SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
        SDL_version linked;
        SDL_GetVersion(&linked);
        std::printf("selftest ok: SDL %d.%d.%d\n", linked.major, linked.minor, linked.patch);
        SDL_Quit();
        return 0;
    }
#endif
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
    // Native (desktop): auto-connect to a running Yabause on boot (retrying until
    // one appears). `--live [endpoint]` picks a specific endpoint; a savestate/dump
    // path instead loads that file and disables auto-connect.
    if (argc > 1 && std::strcmp(argv[1], "--live") == 0)
    {
        gApp.EnableLiveAutoConnect(argc > 2 ? argv[2] : nullptr);
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
    else
    {
        gApp.EnableLiveAutoConnect(nullptr);   // no args: poll for Yabause on boot
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
