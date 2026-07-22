// WebPlatform — Seam C backend for the browser (Emscripten + SDL2 + WebGL2) and,
// when built natively, an SDL2 + desktop-OpenGL window. It is the web counterpart
// of WindowsPlatform: it owns the SDL window, the GL context, the ImGui SDL2 +
// OpenGL3 backends, and the RGBA texture bridge the panels draw with.
//
// Emscripten-specific calls (device-pixel-ratio, EM_JS file picker) are guarded
// by __EMSCRIPTEN__, so the same translation unit also builds as a native SDL2
// desktop app — handy for validating the frontend without a browser.
#pragma once

#include <string>

#include "Platform/IPlatform.h"

struct SDL_Window;
typedef void* SDL_GLContext;

namespace sfe
{

class WebPlatform : public IPlatform
{
public:
    bool Initialize(const PlatformConfig& config) override;
    void Shutdown() override;

    bool PumpEvents() override;
    void BeginFrame() override;
    void EndFrame() override;

    TextureHandle CreateTexture(int width, int height) override;
    void UpdateTexture(TextureHandle handle, const void* rgba,
                       int width, int height) override;
    void DestroyTexture(TextureHandle handle) override;

    // Resize the SDL window (and thus the canvas) to a new logical size. Called
    // from JS on browser resize so the UI stays crisp instead of CSS-stretched.
    void Resize(int width, int height);

    // On the web there is no synchronous native dialog: this triggers the page's
    // hidden <input type=file> and returns false. The chosen file's bytes arrive
    // asynchronously via se_web_load_file() (see WebMain.cpp). On the desktop
    // build it returns false (use drag-and-drop or a launch argument instead).
    bool OpenFileDialog(std::string& outPath) override;

    // Web: trigger a browser download of the blob. Native desktop: write it to the
    // current directory under 'suggestedName'.
    bool SaveFile(const char* suggestedName, const void* data, size_t size) override;

    // Native desktop only (zenity/kdialog/osascript for the folder picker; the OS
    // file manager for reveal). On the web these keep the base "unsupported" default.
#ifndef __EMSCRIPTEN__
    bool PickDirectory(std::string& outPath) override;
    bool RevealPath(const char* path) override;
    bool LaunchProcess(const char* path, const char* args, const char* workingDir) override;
#endif

private:
    SDL_Window*   mWindow  = nullptr;
    SDL_GLContext mGlContext = nullptr;
    bool          mQuit = false;
};

}  // namespace sfe
