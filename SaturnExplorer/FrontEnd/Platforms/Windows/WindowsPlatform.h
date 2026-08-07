// WindowsPlatform — the Win32 + Direct3D 11 implementation of IPlatform.
// Owns the window, the D3D11 device/swapchain, and the ImGui Win32 + DX11
// backends. This is the only file that touches OS/GPU APIs; the portable app
// depends solely on IPlatform. See ARCHITECTURE.md (Seam C).
#pragma once

#include <windows.h>
#include <mmsystem.h>   // waveOut audio preview (HWAVEOUT / WAVEHDR); link winmm
#include <d3d11.h>

#include <vector>

#include "Platform/IPlatform.h"

namespace sfe
{

class WindowsPlatform : public IPlatform
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

    bool OpenFileDialog(std::string& outPath) override;
    bool SaveFile(const char* suggestedName, const void* data, size_t size) override;
    bool PickDirectory(std::string& outPath) override;
    bool OpenFileDialogFiltered(std::string& outPath, const char* filterLabel,
                                const char* extCsv) override;
    bool RevealPath(const char* path) override;
    bool OpenURL(const char* url) override;
    bool LaunchProcess(const char* path, const char* args, const char* workingDir) override;
    bool HttpsGet(const std::string& url, const std::string& userAgent,
                  HttpResponse& out) override;

    // Audio preview via legacy waveOut (winmm). Fire-and-forget one PCM16 buffer; a new Play
    // resets the previous. Always available on Windows, so HasAudio() is true.
    bool HasAudio() override { return true; }
    bool PlayAudio(const int16_t* pcm, size_t frames, int sampleRate, int channels) override;

private:
    // Release any in-flight waveOut buffer/device. Called internally by PlayAudio (to
    // reset the previous preview) and Shutdown; not part of the IPlatform contract.
    void StopAudio();

    bool CreateDeviceD3D();
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();

    static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    HWND                    mHwnd = nullptr;
    WNDCLASSEXW             mWindowClass {};
    ID3D11Device*           mDevice = nullptr;
    ID3D11DeviceContext*    mDeviceContext = nullptr;
    IDXGISwapChain*         mSwapChain = nullptr;
    ID3D11RenderTargetView* mRenderTargetView = nullptr;
    bool                    mbSwapChainOccluded = false;
    UINT                    mResizeWidth = 0;
    UINT                    mResizeHeight = 0;
    bool                    mOleInitialized = false;

    // waveOut audio preview state (the copied PCM must outlive playback, hence a member).
    HWAVEOUT                mWaveOut = nullptr;
    WAVEHDR                 mWaveHdr {};
    std::vector<int16_t>    mWaveBuf;

    static WindowsPlatform* sInstance;  // for the static WndProc
};

}  // namespace sfe
