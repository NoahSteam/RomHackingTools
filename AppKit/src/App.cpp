// App — demo implementation. Exercises every IPlatform capability so a new app
// starts from a known-working base: the frame loop, the RGBA texture bridge, the
// Open dialog, and Save. Replace the panels below with your own.

#include "App.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "imgui.h"

namespace ak
{

void App::Initialize()
{
    // Nothing to set up for the demo; a real app would create its engine/context
    // here. The demo texture is created lazily on the first frame (it needs the
    // platform's GPU device, which only exists after platform Initialize()).
}

void App::Shutdown()
{
    // Texture is owned by the GPU backend; it's freed when the platform shuts down.
    // A real app would release its engine/context here.
}

void App::EnsureDemoTexture(IPlatform& platform)
{
    if (mTexture != 0)
    {
        return;
    }
    mTexW = 256;
    mTexH = 256;
    mTexture = platform.CreateTexture(mTexW, mTexH);

    // A simple RGBA gradient so the texture bridge is visibly working.
    std::vector<uint8_t> rgba(static_cast<size_t>(mTexW) * mTexH * 4);
    for (int y = 0; y < mTexH; ++y)
    {
        for (int x = 0; x < mTexW; ++x)
        {
            uint8_t* p = &rgba[(static_cast<size_t>(y) * mTexW + x) * 4];
            p[0] = static_cast<uint8_t>(x);              // R ramps across
            p[1] = static_cast<uint8_t>(y);              // G ramps down
            p[2] = static_cast<uint8_t>(128);            // B constant
            p[3] = 255;
        }
    }
    platform.UpdateTexture(mTexture, rgba.data(), mTexW, mTexH);
}

void App::OnFileLoaded(const uint8_t* data, size_t size)
{
    mLastLoadedSize = size;
    const size_t n = size < sizeof(mLastLoadedHead) ? size : sizeof(mLastLoadedHead);
    std::memcpy(mLastLoadedHead, data, n);
    if (n < sizeof(mLastLoadedHead))
    {
        std::memset(mLastLoadedHead + n, 0, sizeof(mLastLoadedHead) - n);
    }
}

void App::BuildUI(IPlatform& platform)
{
    EnsureDemoTexture(platform);

    // Full-window dockspace so panels can be arranged/tabbed like a real tool.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open..."))
            {
                // Native platforms return a path synchronously; read it here. The
                // web platform returns false and delivers bytes async via
                // ak_web_load_file -> OnFileLoaded.
                std::string path;
                if (platform.OpenFileDialog(path) && !path.empty())
                {
                    if (FILE* f = std::fopen(path.c_str(), "rb"))
                    {
                        std::fseek(f, 0, SEEK_END);
                        long len = std::ftell(f);
                        std::fseek(f, 0, SEEK_SET);
                        if (len > 0)
                        {
                            std::vector<uint8_t> bytes(static_cast<size_t>(len));
                            if (std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size())
                            {
                                OnFileLoaded(bytes.data(), bytes.size());
                            }
                        }
                        std::fclose(f);
                    }
                }
            }
            if (ImGui::MenuItem("Save demo blob..."))
            {
                // Prove the Save seam end-to-end: write 256 bytes 0x00..0xFF.
                uint8_t blob[256];
                for (int i = 0; i < 256; ++i) blob[i] = static_cast<uint8_t>(i);
                platform.SaveFile("appkit-demo.bin", blob, sizeof(blob));
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
            {
                mQuitRequested = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("ImGui Demo Window", nullptr, &mShowDemoWindow);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Panel proving the texture bridge (CreateTexture/UpdateTexture) + Open/Save.
    if (ImGui::Begin("AppKit"))
    {
        ImGui::TextUnformatted("Cross-platform ImGui base: Windows / desktop-SDL / web.");
        ImGui::Separator();
        ImGui::Text("Texture bridge (%dx%d RGBA uploaded via IPlatform):", mTexW, mTexH);
        if (mTexture != 0)
        {
            ImGui::Image(mTexture, ImVec2(192, 192));
        }
        ImGui::Separator();
        if (mLastLoadedSize > 0)
        {
            ImGui::Text("Last file: %zu bytes  head: %02X %02X %02X %02X",
                        mLastLoadedSize, mLastLoadedHead[0], mLastLoadedHead[1],
                        mLastLoadedHead[2], mLastLoadedHead[3]);
        }
        else
        {
            ImGui::TextDisabled("No file loaded (File > Open, or drop one on the web build).");
        }
    }
    ImGui::End();

    if (mShowDemoWindow)
    {
        ImGui::ShowDemoWindow(&mShowDemoWindow);
    }
}

}  // namespace ak
