// App — the portable, platform-agnostic half of an AppKit application. It owns
// UI state and panels and depends only on IPlatform (Seam C) + ImGui — never on
// any OS/GPU type. The entry points (WinMain / WebMain) construct one App and one
// IPlatform, then call BuildUI(platform) once per frame.
//
// This is the file you rewrite for a real app: swap the demo panels for yours,
// and (if you want an embeddable engine) add a Core + data-source drivers behind
// a flat C ABI as Saturn Explorer does. Everything else in AppKit stays as-is.
#pragma once

#include <cstddef>
#include <cstdint>

#include "appkit/IPlatform.h"

namespace ak
{

class App
{
public:
    void Initialize();
    void Shutdown();

    // Build one frame of UI. `platform` is injected each frame so panels can reach
    // the texture bridge and file dialogs without ever seeing an OS type.
    void BuildUI(IPlatform& platform);

    // A file's bytes arrived (native: from the Open dialog; web: from the page's
    // file input / drag-drop via ak_web_load_file). The demo just records the size.
    void OnFileLoaded(const uint8_t* data, size_t size);

    // True once the user picks File > Quit; the native loop uses it to exit.
    bool WantsQuit() const { return mQuitRequested; }

private:
    void EnsureDemoTexture(IPlatform& platform);

    bool          mShowDemoWindow = true;
    bool          mQuitRequested  = false;

    TextureHandle mTexture = 0;      // GPU handle from IPlatform::CreateTexture
    int           mTexW    = 0;
    int           mTexH    = 0;

    size_t        mLastLoadedSize = 0;
    uint8_t       mLastLoadedHead[8] = {0};
};

}  // namespace ak
