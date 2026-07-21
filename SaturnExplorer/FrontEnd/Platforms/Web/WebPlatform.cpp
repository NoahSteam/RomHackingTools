// WebPlatform — SDL2 + OpenGL(ES) implementation of Seam C. See WebPlatform.h.

#include "WebPlatform.h"

#include <cstdio>
#include <cstdlib>   // system() for RevealPath (native desktop)

#include <SDL.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#include "Theme.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <GLES3/gl3.h>
#else
#include <SDL_opengl.h>   // desktop GL for the native verification build
#endif

namespace sfe
{

#ifdef __EMSCRIPTEN__
// Trigger the page's hidden <input type=file> (defined in the shell HTML). The
// selected file's bytes come back through se_web_load_file() (WebMain.cpp).
EM_JS(void, SeWebOpenFilePicker, (), {
    var el = document.getElementById('se-file-input');
    if (el) el.click();
});

// Download 'len' bytes at 'data' as a file named 'name' (client-side blob).
EM_JS(void, SeWebDownload, (const char* name, const uint8_t* data, int len), {
    var bytes = HEAPU8.slice(data, data + len);
    var blob = new Blob([bytes], { type: 'application/octet-stream' });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = UTF8ToString(name);
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
});
#endif

bool WebPlatform::Initialize(const PlatformConfig& config)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        return false;
    }

    // Request a GLES 3.0 context on the web (→ WebGL2); a matching GL 3.0 context
    // natively. The OpenGL3 backend adapts to whichever it gets.
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                         SDL_WINDOW_ALLOW_HIGHDPI;
    mWindow = SDL_CreateWindow(config.mTitle,
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               config.mWidth, config.mHeight, flags);
    if (!mWindow)
    {
        return false;
    }
    mGlContext = SDL_GL_CreateContext(mWindow);
    if (!mGlContext)
    {
        return false;
    }
    SDL_GL_MakeCurrent(mWindow, mGlContext);
    SDL_GL_SetSwapInterval(1);   // vsync (ignored under Emscripten's rAF loop)

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // Docking inside the canvas is fine; multi-viewport spawns real OS windows and
    // cannot work in a browser, so it is deliberately not enabled here.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    sfe::ApplyTheme(ImGui::GetStyle());   // Saturn Explorer theme (shared, portable)
    sfe::LoadFonts(io);                   // embedded proportional UI font

    // Match the Windows backend's DPI handling: scale sizes + the (1.92 scalable)
    // default font by the display's device-pixel ratio.
    float dpiScale = 1.0f;
#ifdef __EMSCRIPTEN__
    dpiScale = static_cast<float>(emscripten_get_device_pixel_ratio());
#endif
    if (dpiScale < 1.0f)
    {
        dpiScale = 1.0f;
    }
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(dpiScale);
    style.FontScaleMain = dpiScale;

    ImGui_ImplSDL2_InitForOpenGL(mWindow, mGlContext);
#ifdef __EMSCRIPTEN__
    ImGui_ImplOpenGL3_Init("#version 300 es");
#else
    ImGui_ImplOpenGL3_Init("#version 130");
#endif
    return true;
}

void WebPlatform::Shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (mGlContext)
    {
        SDL_GL_DeleteContext(mGlContext);
        mGlContext = nullptr;
    }
    if (mWindow)
    {
        SDL_DestroyWindow(mWindow);
        mWindow = nullptr;
    }
    SDL_Quit();
}

bool WebPlatform::PumpEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT)
        {
            mQuit = true;
        }
    }
    // On the web the browser owns the loop and never sends SDL_QUIT, so this stays
    // true; natively it returns false when the window is closed.
    return !mQuit;
}

void WebPlatform::BeginFrame()
{
    // ImGui_ImplSDL2_NewFrame sets io.DisplaySize + framebuffer scale from the SDL
    // window / drawable each frame, so canvas resizes are handled for free.
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void WebPlatform::EndFrame()
{
    ImGui::Render();
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(mWindow, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(mWindow);
}

TextureHandle WebPlatform::CreateTexture(int width, int height)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Unsized RGBA is valid in both desktop GL and WebGL2/GLES3.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    return static_cast<TextureHandle>(static_cast<intptr_t>(tex));
}

void WebPlatform::UpdateTexture(TextureHandle handle, const void* rgba,
                                int width, int height)
{
    const GLuint tex = static_cast<GLuint>(static_cast<intptr_t>(handle));
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);   // rows are width*4, always 4-aligned
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void WebPlatform::DestroyTexture(TextureHandle handle)
{
    const GLuint tex = static_cast<GLuint>(static_cast<intptr_t>(handle));
    if (tex != 0)
    {
        glDeleteTextures(1, &tex);
    }
}

void WebPlatform::Resize(int width, int height)
{
    if (mWindow && width > 0 && height > 0)
    {
        SDL_SetWindowSize(mWindow, width, height);
    }
}

bool WebPlatform::OpenFileDialog(std::string& outPath)
{
    (void)outPath;
#ifdef __EMSCRIPTEN__
    SeWebOpenFilePicker();   // async: bytes arrive via se_web_load_file()
#endif
    return false;   // no synchronous path result on the web
}

bool WebPlatform::SaveFile(const char* suggestedName, const void* data, size_t size)
{
#ifdef __EMSCRIPTEN__
    SeWebDownload(suggestedName ? suggestedName : "dump.bin",
                  static_cast<const uint8_t*>(data), static_cast<int>(size));
    return true;
#else
    // Native desktop verification build: write to the current directory.
    FILE* f = std::fopen(suggestedName ? suggestedName : "dump.bin", "wb");
    if (!f)
    {
        return false;
    }
    const size_t wrote = std::fwrite(data, 1, size, f);
    std::fclose(f);
    return wrote == size;
#endif
}

#ifndef __EMSCRIPTEN__
namespace
{
// Wrap a path in single quotes for a POSIX shell, escaping embedded single quotes.
std::string ShellQuote(const std::string& s)
{
    std::string out = "'";
    for (char c : s)
    {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    out += "'";
    return out;
}

// The directory containing `path` ("." if it has no separator).
std::string ParentDir(const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? std::string(".") : path.substr(0, slash);
}
}  // namespace

bool WebPlatform::PickDirectory(std::string& outPath)
{
    // Try the common desktop folder pickers in turn; capture the chosen path from
    // stdout. No GUI dialog available -> return false (caller falls back to typing).
    const char* const cmds[] = {
#if defined(__APPLE__)
        "osascript -e 'try' -e 'POSIX path of (choose folder)' -e 'end try' 2>/dev/null",
#endif
        "zenity --file-selection --directory 2>/dev/null",
        "kdialog --getexistingdirectory 2>/dev/null",
    };
    for (const char* cmd : cmds)
    {
        FILE* p = ::popen(cmd, "r");
        if (!p)
        {
            continue;
        }
        std::string line;
        char buf[1024];
        while (std::fgets(buf, sizeof(buf), p))
        {
            line += buf;
        }
        const int rc = ::pclose(p);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        {
            line.pop_back();
        }
        if (rc == 0 && !line.empty())
        {
            outPath = line;
            return true;
        }
    }
    return false;
}

bool WebPlatform::RevealPath(const char* path)
{
    if (!path || !*path)
    {
        return false;
    }
    const std::string q = ShellQuote(path);
    std::string cmd;
#if defined(__APPLE__)
    cmd = "open -R " + q + " >/dev/null 2>&1 &";
#else
    // Prefer the freedesktop "show and select" call; fall back to opening the parent
    // folder if no D-Bus file manager answers.
    const std::string parent = ParentDir(path);
    cmd = "dbus-send --session --print-reply --dest=org.freedesktop.FileManager1 "
          "--type=method_call /org/freedesktop/FileManager1 "
          "org.freedesktop.FileManager1.ShowItems array:string:\"file://" + std::string(path) +
          "\" string:\"\" >/dev/null 2>&1 || xdg-open " + ShellQuote(parent) +
          " >/dev/null 2>&1 &";
#endif
    return ::system(cmd.c_str()) != -1;
}

bool WebPlatform::LaunchProcess(const char* path, const char* workingDir)
{
    if (!path || !*path)
    {
        return false;
    }
    const std::string dir = (workingDir && *workingDir) ? std::string(workingDir)
                                                        : ParentDir(path);
    // Run detached (trailing &) from its own directory so an emulator finds its
    // config/saves next to the executable.
    const std::string cmd = "cd " + ShellQuote(dir) + " && " + ShellQuote(path) +
                            " >/dev/null 2>&1 &";
    return ::system(cmd.c_str()) != -1;
}
#endif  // !__EMSCRIPTEN__

}  // namespace sfe
