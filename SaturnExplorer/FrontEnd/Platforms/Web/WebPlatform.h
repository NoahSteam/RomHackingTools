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
    bool CloseRequested() override { return mCloseRequested && !mCloseAcked; }
    void AcknowledgeClose() override { mCloseAcked = true; }
    void CancelClose() override { mCloseRequested = false; }
    void BeginFrame() override;
    void EndFrame() override;

    TextureHandle CreateTexture(int width, int height) override;
    void UpdateTexture(TextureHandle handle, const void* rgba,
                       int width, int height) override;
    void DestroyTexture(TextureHandle handle) override;

    // Resize the SDL window (and thus the canvas) to a new logical size. Called
    // from JS on browser resize so the UI stays crisp instead of CSS-stretched.
    void Resize(int width, int height);

    // Web: no synchronous native dialog — this triggers the page's hidden
    // <input type=file> and returns false; the chosen file's bytes arrive
    // asynchronously via se_web_load_file() (see WebMain.cpp). Native desktop:
    // opens an unfiltered chooser (osascript/zenity/kdialog) and returns the path.
    bool OpenFileDialog(std::string& outPath) override;

    // Web: trigger a browser download of the blob. Native desktop: write it to the
    // current directory under 'suggestedName'.
    bool SaveFile(const char* suggestedName, const void* data, size_t size) override;

    // Native desktop only (zenity/kdialog/osascript for the folder picker; the OS
    // file manager for reveal). On the web these keep the base "unsupported" default.
#ifndef __EMSCRIPTEN__
    bool PickDirectory(std::string& outPath) override;
    bool OpenFileDialogFiltered(std::string& outPath, const char* filterLabel,
                                const char* extCsv) override;
    bool RevealPath(const char* path) override;
    bool OpenURL(const char* url) override;
    bool LaunchProcess(const char* path, const char* args, const char* workingDir) override;
    void TerminateLaunchedProcess() override;
#endif

    // HTTPS GET via libcurl on native desktop when SE_HAVE_CURL is defined (macOS/Linux);
    // otherwise inherits the base "unsupported" default (including the Emscripten build).
#if defined(SE_HAVE_CURL) && !defined(__EMSCRIPTEN__)
    bool HttpsGet(const std::string& url, const std::string& userAgent,
                  HttpResponse& out) override;
#endif

    // Audio preview via SDL2 (both web and native desktop). No-op if the audio subsystem
    // failed to init (headless host) — HasAudio() then returns false.
    bool HasAudio() override { return mAudioOk; }
    bool PlayAudio(const int16_t* pcm, size_t frames, int sampleRate, int channels) override;

private:
    SDL_Window*   mWindow  = nullptr;
    SDL_GLContext mGlContext = nullptr;
    bool          mCloseRequested = false;   // user asked to close (window-close veto seam)
    bool          mCloseAcked = false;       // app confirmed the close -> PumpEvents ends
    bool          mAudioOk = false;      // SDL audio subsystem initialized
    uint32_t      mAudioDev = 0;         // SDL_AudioDeviceID (0 = none), reopened on spec change
    int           mAudioRate = 0;        // spec of the currently-open device
    int           mAudioChannels = 0;
#ifndef __EMSCRIPTEN__
    int           mLaunchedPid = -1;     // pid of the emulator we last launched (-1 = none)
#endif
};

}  // namespace sfe
