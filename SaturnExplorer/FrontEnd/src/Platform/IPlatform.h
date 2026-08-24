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

// Sensible bounds for an audio sample rate. Clamped before opening a device so a
// garbage slot pitch (or an odd emulator value) can't ask the backend for an
// absurd rate. Shared by the decode path and every PlayAudio implementation.
constexpr int kAudioMinRate = 2000;
constexpr int kAudioMaxRate = 192000;

class IPlatform
{
public:
    virtual ~IPlatform() = default;

    virtual bool Initialize(const PlatformConfig& config) = 0;
    virtual void Shutdown() = 0;

    // Pump the OS event queue. Returns false once the user asks to quit.
    virtual bool PumpEvents() = 0;

    // Window-close veto (used by the Patch feature's unsaved-changes guard). When the user
    // requests to close the window, a backend that supports this keeps PumpEvents() returning
    // true while CloseRequested() is true, so the app can render a confirmation prompt instead
    // of exiting immediately. The app then either AcknowledgeClose() (proceed — the next
    // PumpEvents() returns false) or CancelClose() (dismiss the request and keep running).
    // Backends that don't override these close immediately as before (CloseRequested() stays
    // false, so the app never defers).
    virtual bool CloseRequested() { return false; }
    virtual void AcknowledgeClose() {}
    virtual void CancelClose() {}

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

    // Open a URL in the user's default web browser (e.g. the online hardware guides).
    // Returns false if unsupported. The default no-ops so a headless backend is fine.
    virtual bool OpenURL(const char* url) { (void)url; return false; }

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

    // Stop the emulator most recently started by LaunchProcess, if it is still running.
    // Used to *relaunch* — start a fresh emulator with a different game rather than leaving
    // the old one running and stacking a second instance. Only affects a process this app
    // launched (a user-started emulator is never touched). No-op if none was launched, it
    // already exited, or the backend has no process control (web/headless).
    virtual void TerminateLaunchedProcess() {}

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

    // --- Audio output (optional). The Sound panel's per-voice Play uses this to preview an
    // SCSP sample. Fire-and-forget playback of one short interleaved 16-bit PCM buffer on the
    // default output device; a new Play replaces the previous one. Base is a no-op so a build
    // with no audio backend simply can't preview (HasAudio() gates the button). ---
    virtual bool HasAudio() { return false; }
    virtual bool PlayAudio(const int16_t* pcm, size_t frames, int sampleRate, int channels)
    {
        (void)pcm; (void)frames; (void)sampleRate; (void)channels;
        return false;   // no audio output in this build
    }
};

}  // namespace sfe
