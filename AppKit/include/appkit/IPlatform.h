// IPlatform — the abstraction between a portable ImGui app and the OS / GPU
// backend. Your app + panels depend only on this interface (plus ImGui), so
// porting to a new platform means implementing IPlatform and an entry point.
// Extracted from Saturn Explorer's "Seam C"; see AppKit/README.md.
#pragma once

#include <cstdint>
#include <string>

#include "imgui.h"  // ImTextureID — ImGui is portable and shared by all platforms.

namespace ak
{

struct PlatformConfig
{
    const char* mTitle  = "AppKit App";
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
};

}  // namespace ak
