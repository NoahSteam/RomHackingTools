// IPlatform — Seam C: the abstraction between the portable frontend and the
// OS / GPU backend. The portable app + panels depend only on this interface
// (plus ImGui and the core's Seam B), so porting the frontend to a new platform
// means implementing IPlatform and providing an entry point. See ARCHITECTURE.md.
#pragma once

#include <cstdint>
#include <string>

#include "imgui.h"  // ImTextureID — ImGui is portable and shared by all platforms.

namespace sfe
{

struct PlatformConfig
{
    const char* mTitle  = "Saturn Explorer";
    int         mWidth  = 1600;
    int         mHeight = 900;
};

// GPU texture the panels draw with ImGui::Image. 0 means "no texture".
using TextureHandle = ImTextureID;

class IPlatform
{
public:
    virtual ~IPlatform() = default;

    virtual bool Initialize(const PlatformConfig& config) = 0;
    virtual void Shutdown() = 0;

    // Pump the OS event queue. Returns false once the user asks to quit.
    virtual bool PumpEvents() = 0;

    // Start a new ImGui frame (renderer + platform NewFrame, then ImGui::NewFrame).
    virtual void BeginFrame() = 0;
    // Render ImGui draw data and present the frame.
    virtual void EndFrame() = 0;

    // --- GPU texture bridge for se_image (RGBA8888). The core decodes pixels;
    // the platform owns the GPU upload so panels stay platform-agnostic. ---
    virtual TextureHandle CreateTexture(int width, int height) = 0;
    virtual void UpdateTexture(TextureHandle handle, const void* rgba,
                               int width, int height) = 0;
    virtual void DestroyTexture(TextureHandle handle) = 0;

    // Native "open file" dialog. Returns false if cancelled or unsupported.
    virtual bool OpenFileDialog(std::string& outPath) = 0;

    // Save a blob to disk. 'suggestedName' seeds the save dialog / download name.
    // Windows shows a Save-As dialog; the web build triggers a browser download;
    // the native desktop build writes to the current directory. Returns false if
    // cancelled or on write error.
    virtual bool SaveFile(const char* suggestedName, const void* data, size_t size) = 0;

    // --- Optional capabilities (default: unsupported). Used by the game-data
    // search: pick the data folder, and reveal a found file in the OS file
    // manager. Not pure-virtual so a backend that can't offer them (e.g. the web
    // build, with no host filesystem) simply inherits the "false" default. ---

    // Native "choose folder" dialog. Returns false if cancelled or unsupported.
    virtual bool PickDirectory(std::string& outPath) { (void)outPath; return false; }

    // Open-file dialog filtered to a file type (e.g. Saturn disc images for the ROM
    // picker). `filterLabel` names the group ("Saturn discs"); `extCsv` is a
    // comma-separated extension list without dots ("cue,chd,iso"). The base falls back
    // to the unfiltered OpenFileDialog so a platform that can't filter still works.
    virtual bool OpenFileDialogFiltered(std::string& outPath, const char* filterLabel,
                                        const char* extCsv)
    {
        (void)filterLabel;
        (void)extCsv;
        return OpenFileDialog(outPath);
    }

    // Open the OS file manager with `path` selected/highlighted (Explorer on
    // Windows, Finder on macOS, the file manager on Linux). Returns false if
    // unsupported or the path doesn't exist.
    virtual bool RevealPath(const char* path) { (void)path; return false; }

    // Launch an executable (e.g. the patched Mednafen/Yabause recorded at install
    // time), non-blocking, with an optional command-line `args` string (NULL/"" = none;
    // e.g. the ROM to boot) and optional `workingDir` (NULL = the exe's folder, so an
    // emulator finds its config/saves). `args` is a single command-line string parsed by
    // the OS/shell, so callers quote paths with spaces themselves. Returns false if
    // unsupported or launch failed. The web build has no host process control and
    // inherits the default.
    virtual bool LaunchProcess(const char* path, const char* args, const char* workingDir)
    {
        (void)path;
        (void)args;
        (void)workingDir;
        return false;
    }

    // --- HTTPS GET (Seam C, optional). The one network primitive the portable app
    // needs — currently only the update check (UpdateChecker) uses it. Kept minimal and
    // synchronous on purpose: the caller runs it on a worker thread, so each platform only
    // has to implement a blocking "fetch this URL" (Windows: WinHTTP; native desktop /
    // macOS: libcurl; browser: unsupported — the web build is always the freshest). ---
    struct HttpResponse
    {
        bool        ok = false;   // transport succeeded (a reply was received)
        long        status = 0;   // HTTP status code (200, 404, …) when ok
        std::string body;         // response body when ok
        std::string error;        // human-readable reason when !ok
    };

    // Blocking HTTPS GET of 'url' with the given User-Agent (GitHub's API requires one).
    // Returns false with out.error set on any transport failure or if unsupported by this
    // platform (the default). MUST be called off the UI thread — it blocks until the
    // request completes or times out.
    virtual bool HttpsGet(const std::string& url, const std::string& userAgent,
                          HttpResponse& out)
    {
        (void)url;
        (void)userAgent;
        out.ok = false;
        out.error = "Online update check is not available in this build.";
        return false;
    }
};

}  // namespace sfe
