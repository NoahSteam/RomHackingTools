#include "WindowsPlatform.h"

#include <commdlg.h>
#include <shlobj.h>     // SHBrowseForFolder (PickDirectory)
#include <shellapi.h>   // ShellExecute (RevealPath)
#include <cstdio>
#include <cstring>
#include <string>
#include <tchar.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include "Theme.h"

// Forward declared in the Win32 backend; handles ImGui's own input messages.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                             WPARAM wParam, LPARAM lParam);

namespace sfe
{

WindowsPlatform* WindowsPlatform::sInstance = nullptr;

bool WindowsPlatform::Initialize(const PlatformConfig& config)
{
    sInstance = this;

    // Make the process DPI-aware BEFORE creating the window. Without this,
    // Windows renders the app at 96 DPI and bitmap-stretches it up on a
    // high-DPI display — the cause of the blur. Now the app draws at native
    // resolution and we scale the UI ourselves (below) so it stays crisp.
    ImGui_ImplWin32_EnableDpiAwareness();

    mWindowClass = { sizeof(mWindowClass), CS_CLASSDC, WndProc, 0L, 0L,
                     GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
                     L"SaturnExplorerWindow", nullptr };
    ::RegisterClassExW(&mWindowClass);

    mHwnd = ::CreateWindowW(mWindowClass.lpszClassName, L"Saturn Explorer",
                            WS_OVERLAPPEDWINDOW, 100, 100, config.mWidth, config.mHeight,
                            nullptr, nullptr, mWindowClass.hInstance, nullptr);
    if (!mHwnd)
    {
        ::UnregisterClassW(mWindowClass.lpszClassName, mWindowClass.hInstance);
        return false;
    }

    if (!CreateDeviceD3D())
    {
        CleanupDeviceD3D();
        ::DestroyWindow(mHwnd);
        ::UnregisterClassW(mWindowClass.lpszClassName, mWindowClass.hInstance);
        return false;
    }

    ::ShowWindow(mHwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(mHwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    sfe::ApplyTheme(ImGui::GetStyle());   // Saturn Explorer theme (shared, portable)
    sfe::LoadFonts(io);                   // embedded proportional UI font

    // Scale the whole UI to the window's DPI so it is both crisp and the right
    // physical size. ImGui 1.92's font system keeps the scalable default font
    // sharp at the scaled size; ScaleAllSizes handles spacing/padding.
    const float dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(mHwnd);
    if (dpiScale > 0.0f)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(dpiScale);
        style.FontScaleMain = dpiScale;
    }

    ImGui_ImplWin32_Init(mHwnd);
    ImGui_ImplDX11_Init(mDevice, mDeviceContext);
    return true;
}

void WindowsPlatform::Shutdown()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    if (mHwnd)
    {
        ::DestroyWindow(mHwnd);
        mHwnd = nullptr;
    }
    ::UnregisterClassW(mWindowClass.lpszClassName, mWindowClass.hInstance);
    sInstance = nullptr;
}

bool WindowsPlatform::PumpEvents()
{
    MSG msg;
    while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
        if (msg.message == WM_QUIT)
        {
            return false;
        }
    }
    return true;
}

void WindowsPlatform::BeginFrame()
{
    // Handle a pending resize requested from WndProc.
    if (mResizeWidth != 0 && mResizeHeight != 0)
    {
        CleanupRenderTarget();
        mSwapChain->ResizeBuffers(0, mResizeWidth, mResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
        mResizeWidth = 0;
        mResizeHeight = 0;
        CreateRenderTarget();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void WindowsPlatform::EndFrame()
{
    ImGui::Render();

    const float clearColor[4] = { 0.10f, 0.10f, 0.12f, 1.0f };
    mDeviceContext->OMSetRenderTargets(1, &mRenderTargetView, nullptr);
    mDeviceContext->ClearRenderTargetView(mRenderTargetView, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Multi-viewport (undocked panels dragged outside the main window).
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    HRESULT hr = mSwapChain->Present(1, 0);  // vsync
    mbSwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
}

TextureHandle WindowsPlatform::CreateTexture(int width, int height)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    ID3D11Texture2D* texture = nullptr;
    if (FAILED(mDevice->CreateTexture2D(&desc, nullptr, &texture)))
    {
        return 0;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = mDevice->CreateShaderResourceView(texture, &srvDesc, &srv);
    texture->Release();  // the SRV keeps the resource alive
    if (FAILED(hr))
    {
        return 0;
    }
    // Match ImGui's idiom (via intptr_t) so this is correct on 32-bit too.
    return (TextureHandle)(intptr_t)srv;
}

void WindowsPlatform::UpdateTexture(TextureHandle handle, const void* rgba,
                                    int width, int height)
{
    if (!handle || !rgba)
    {
        return;
    }

    ID3D11ShaderResourceView* srv = (ID3D11ShaderResourceView*)(intptr_t)handle;
    ID3D11Resource* resource = nullptr;
    srv->GetResource(&resource);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(mDeviceContext->Map(resource, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        const uint8_t* src = static_cast<const uint8_t*>(rgba);
        uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
        const size_t rowBytes = static_cast<size_t>(width) * 4;
        for (int y = 0; y < height; ++y)
        {
            std::memcpy(dst + y * mapped.RowPitch, src + y * rowBytes, rowBytes);
        }
        mDeviceContext->Unmap(resource, 0);
    }
    resource->Release();
}

void WindowsPlatform::DestroyTexture(TextureHandle handle)
{
    if (!handle)
    {
        return;
    }
    ((ID3D11ShaderResourceView*)(intptr_t)handle)->Release();
}

bool WindowsPlatform::OpenFileDialog(std::string& outPath)
{
    char file[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = mHwnd;
    ofn.lpstrFilter = "All Files\0*.*\0Memory Dumps\0*.bin;*.dump;*.sav\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (!::GetOpenFileNameA(&ofn))
    {
        return false;
    }
    outPath = file;
    return true;
}

bool WindowsPlatform::OpenFileDialogFiltered(std::string& outPath, const char* filterLabel,
                                             const char* extCsv)
{
    // Build a double-NUL-terminated GetOpenFileName filter: "<label>\0*.a;*.b\0All Files\0*.*\0".
    // extCsv is "cue,chd,iso"; turn it into "*.cue;*.chd;*.iso".
    std::string pats;
    for (const char* p = extCsv ? extCsv : ""; *p; )
    {
        const char* comma = std::strchr(p, ',');
        const size_t len = comma ? static_cast<size_t>(comma - p) : std::strlen(p);
        if (len)
        {
            if (!pats.empty()) pats += ';';
            pats += "*.";
            pats.append(p, len);
        }
        p += len;
        if (comma) ++p;
    }
    std::string filter = (filterLabel ? filterLabel : "Files");
    filter.push_back('\0');
    filter += pats;
    filter.push_back('\0');
    filter += "All Files";
    filter.push_back('\0');
    filter += "*.*";
    filter.push_back('\0');
    filter.push_back('\0');

    char file[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = mHwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!::GetOpenFileNameA(&ofn))
    {
        return false;
    }
    outPath = file;
    return true;
}

bool WindowsPlatform::SaveFile(const char* suggestedName, const void* data, size_t size)
{
    char file[MAX_PATH] = {};
    if (suggestedName)
    {
        std::strncpy(file, suggestedName, MAX_PATH - 1);
    }
    // Generic saver: the caller's 'suggestedName' carries the extension, so this
    // stays format-agnostic (no dump-specific filter baked into the platform seam).
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = mHwnd;
    ofn.lpstrFilter = "All Files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (!::GetSaveFileNameA(&ofn))
    {
        return false;
    }
    FILE* f = std::fopen(file, "wb");
    if (!f)
    {
        return false;
    }
    const size_t wrote = std::fwrite(data, 1, size, f);
    std::fclose(f);
    return wrote == size;
}

bool WindowsPlatform::PickDirectory(std::string& outPath)
{
    // SHBrowseForFolder: a folder picker with no COM init required.
    char display[MAX_PATH] = {};
    BROWSEINFOA bi = {};
    bi.hwndOwner = mHwnd;
    bi.pszDisplayName = display;
    bi.lpszTitle = "Select the game's data directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = ::SHBrowseForFolderA(&bi);
    if (!pidl)
    {
        return false;   // cancelled
    }
    char path[MAX_PATH] = {};
    const bool ok = ::SHGetPathFromIDListA(pidl, path) != FALSE;
    ::CoTaskMemFree(pidl);
    if (ok)
    {
        outPath = path;
    }
    return ok;
}

bool WindowsPlatform::RevealPath(const char* path)
{
    if (!path || !*path)
    {
        return false;
    }
    // Open Explorer with the file selected: explorer.exe /select,"C:\path\file".
    std::string args = "/select,\"";
    args += path;
    args += "\"";
    const HINSTANCE r = ::ShellExecuteA(nullptr, "open", "explorer.exe",
                                        args.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(r) > 32;   // >32 == success per the API
}

bool WindowsPlatform::LaunchProcess(const char* path, const char* args, const char* workingDir)
{
    if (!path || !*path)
    {
        return false;
    }
    // Per the IPlatform contract, a NULL workingDir means "the exe's own folder" so
    // the launched emulator finds its config/saves; derive it from the path.
    std::string derived;
    if (!(workingDir && *workingDir))
    {
        const std::string p = path;
        const size_t slash = p.find_last_of("/\\");
        if (slash != std::string::npos) derived = p.substr(0, slash);
        workingDir = derived.empty() ? nullptr : derived.c_str();
    }
    // ShellExecute's 4th arg is the command-line parameter string (NULL if none); the
    // launched program's CRT parses it, so a quoted "<rom>" survives spaces.
    const char* params = (args && *args) ? args : nullptr;
    const HINSTANCE r = ::ShellExecuteA(nullptr, "open", path, params, workingDir, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(r) > 32;   // >32 == success per the API
}

bool WindowsPlatform::CreateDeviceD3D()
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = mHwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags, levels, 2,
        D3D11_SDK_VERSION, &sd, &mSwapChain, &mDevice, &featureLevel, &mDeviceContext);
    if (hr == DXGI_ERROR_UNSUPPORTED)
    {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createFlags, levels, 2,
            D3D11_SDK_VERSION, &sd, &mSwapChain, &mDevice, &featureLevel, &mDeviceContext);
    }
    if (FAILED(hr))
    {
        return false;
    }

    CreateRenderTarget();
    return true;
}

void WindowsPlatform::CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (mSwapChain)      { mSwapChain->Release(); mSwapChain = nullptr; }
    if (mDeviceContext)  { mDeviceContext->Release(); mDeviceContext = nullptr; }
    if (mDevice)         { mDevice->Release(); mDevice = nullptr; }
}

void WindowsPlatform::CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    mSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer)
    {
        mDevice->CreateRenderTargetView(backBuffer, nullptr, &mRenderTargetView);
        backBuffer->Release();
    }
}

void WindowsPlatform::CleanupRenderTarget()
{
    if (mRenderTargetView)
    {
        mRenderTargetView->Release();
        mRenderTargetView = nullptr;
    }
}

LRESULT WINAPI WindowsPlatform::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
    {
        return true;
    }

    switch (msg)
    {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED && sInstance)
        {
            sInstance->mResizeWidth = static_cast<UINT>(LOWORD(lparam));
            sInstance->mResizeHeight = static_cast<UINT>(HIWORD(lparam));
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU)  // disable ALT application menu
        {
            return 0;
        }
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace sfe
