// WebPlatform — SDL2 + OpenGL(ES) implementation of Seam C. See WebPlatform.h.

#include "WebPlatform.h"

#include <cstdio>
#include <cstdlib>   // system() for RevealPath (native desktop)
#include <cstring>   // strchr/strlen for the file-dialog filter (native desktop)
#include <string>
#include <vector>

#include <SDL.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#include "Theme.h"

#if defined(SE_HAVE_CURL) && !defined(__EMSCRIPTEN__)
#include <curl/curl.h>   // native desktop HttpsGet (update check)
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <GLES3/gl3.h>
#else
#include <SDL_opengl.h>   // desktop GL for the native verification build
#include <sys/wait.h>     // WIFEXITED/WEXITSTATUS to tell "dialog cancelled" from "not installed"
#include <sys/types.h>    // pid_t
#include <unistd.h>       // fork/execl/setsid/dup2 for LaunchProcess (killable emulator child)
#include <signal.h>       // kill() to stop the launched emulator on relaunch
#include <fcntl.h>        // open() /dev/null for the launched child's stdio
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
    // Audio is optional and initialized separately so a host with no audio device (headless
    // CI, some servers) still runs — the Sound panel's Play button just stays disabled.
    mAudioOk = (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0);

    // Request the GL context the ImGui OpenGL3 backend needs. The web wants GLES 3.0
    // (→ WebGL2); Linux is happy with a 3.0 compatibility context; macOS is the special case —
    // it only exposes legacy 2.1 OR a 3.2+ *Core* (forward-compatible) profile, so a bare 3.0
    // request there silently yields a 2.1 context and the GL3 shaders fail to compile (blank
    // window). Ask macOS for Core 3.2 explicitly.
    // All three want GL/GLES major version 3; only the profile and minor version differ.
#if defined(__EMSCRIPTEN__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
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
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    io.ConfigMacOSXBehaviors = true;   // ⌘-based shortcuts + Mac-style text-edit navigation
#endif

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
#if defined(__EMSCRIPTEN__)
    ImGui_ImplOpenGL3_Init("#version 300 es");
#elif defined(__APPLE__)
    ImGui_ImplOpenGL3_Init("#version 150");   // GLSL for the Core 3.2 context requested above
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
    if (mAudioDev)
    {
        SDL_CloseAudioDevice(mAudioDev);
        mAudioDev = 0;
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
            // Don't quit immediately: raise the close-request flag so the app can veto (e.g.
            // to warn about unsaved patch changes). It confirms via AcknowledgeClose(), which
            // sets mCloseAcked and ends the loop below; CancelClose() clears the request.
            mCloseRequested = true;
        }
    }
    // On the web the browser owns the loop and never sends SDL_QUIT, so this stays
    // true; natively it returns false only once the app confirms the close.
    return !mCloseAcked;
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

// Wrap an AppleScript chooser expression in the standard "try / POSIX path of (...) / end try"
// form, so a user-cancel returns cleanly (exit 0, empty output) instead of a scripting error.
std::string Osascript(const std::string& expr)
{
    return "osascript -e 'try' -e 'POSIX path of (" + expr + ")' -e 'end try' 2>/dev/null";
}

// SDL has no native file dialog, so the desktop dialogs shell out to the OS chooser (macOS
// osascript, Linux zenity/kdialog). RunChooser tries each command until one yields a path, and
// reports which of three things happened so callers can react correctly:
enum class ChooserResult { Found, Cancelled, Unavailable };

// Try each command in order. A path -> Found (in outPath). If a command actually ran but gave
// no path (the user dismissed the dialog) -> Cancelled. If every command failed to launch (no
// chooser installed, e.g. a headless host; the shell exits 127) -> Unavailable. This lets
// SaveFile abort on cancel yet still fall back to a plain write when there's no dialog at all.
ChooserResult RunChooser(const std::vector<std::string>& cmds, std::string& outPath)
{
    bool anyRan = false;
    for (const std::string& cmd : cmds)
    {
        FILE* p = ::popen(cmd.c_str(), "r");
        if (!p) continue;
        std::string line;
        char buf[1024];
        while (std::fgets(buf, sizeof(buf), p)) line += buf;
        const int status = ::pclose(p);
        const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (code != 127) anyRan = true;   // 127 = shell couldn't find the program
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (code == 0 && !line.empty()) { outPath = line; return ChooserResult::Found; }
    }
    return anyRan ? ChooserResult::Cancelled : ChooserResult::Unavailable;
}
}  // namespace
#endif

bool WebPlatform::OpenFileDialog(std::string& outPath)
{
#ifdef __EMSCRIPTEN__
    (void)outPath;
    SeWebOpenFilePicker();   // async: bytes arrive via se_web_load_file()
    return false;            // no synchronous path result on the web
#else
    return OpenFileDialogFiltered(outPath, nullptr, nullptr);   // native: an unfiltered open
#endif
}

bool WebPlatform::SaveFile(const char* suggestedName, const void* data, size_t size)
{
    const char* name = suggestedName ? suggestedName : "dump.bin";
#ifdef __EMSCRIPTEN__
    SeWebDownload(name, static_cast<const uint8_t*>(data), static_cast<int>(size));
    return true;
#else
    // Ask the desktop's native save dialog for a destination (macOS osascript / Linux zenity /
    // kdialog). A cancelled dialog aborts the save; only when no chooser exists at all (e.g. a
    // headless host) do we fall back to writing `name` in the current directory.
    std::string path = name;
    std::vector<std::string> cmds;
#if defined(__APPLE__)
    cmds.push_back(Osascript("choose file name default name \"" + std::string(name) + "\""));
#endif
    cmds.push_back("zenity --file-selection --save --confirm-overwrite --filename=" +
                   ShellQuote(name) + " 2>/dev/null");
    cmds.push_back("kdialog --getsavefilename . " + ShellQuote(name) + " 2>/dev/null");
    if (RunChooser(cmds, path) == ChooserResult::Cancelled) return false;   // user dismissed

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const size_t wrote = std::fwrite(data, 1, size, f);
    std::fclose(f);
    return wrote == size;
#endif
}

// The remaining dialog/shell entry points are native-desktop only (declared under
// #ifndef __EMSCRIPTEN__ in the header); the browser build inherits the base no-ops.
#ifndef __EMSCRIPTEN__
bool WebPlatform::PickDirectory(std::string& outPath)
{
    std::vector<std::string> cmds;
#if defined(__APPLE__)
    cmds.push_back(Osascript("choose folder"));
#endif
    cmds.push_back("zenity --file-selection --directory 2>/dev/null");
    cmds.push_back("kdialog --getexistingdirectory 2>/dev/null");
    return RunChooser(cmds, outPath) == ChooserResult::Found;
}

bool WebPlatform::OpenFileDialogFiltered(std::string& outPath, const char* filterLabel,
                                         const char* extCsv)
{
    // Build the per-chooser filter from the csv extension list: zenity wants
    // "--file-filter=Label | *.cue *.chd", kdialog wants "*.cue *.chd|Label", and macOS's
    // `choose file of type` wants a brace list of quoted extensions {"cue", "chd"}.
    std::string globs, macTypes;
    for (const char* p = extCsv ? extCsv : ""; *p; )
    {
        const char* comma = std::strchr(p, ',');
        const size_t len = comma ? static_cast<size_t>(comma - p) : std::strlen(p);
        if (len)
        {
            if (!globs.empty()) globs += ' ';
            globs += "*.";
            globs.append(p, len);
            if (!macTypes.empty()) macTypes += ", ";
            macTypes += '"';
            macTypes.append(p, len);
            macTypes += '"';
        }
        p += len;
        if (comma) ++p;
    }
    const std::string label = filterLabel ? filterLabel : "Files";
    std::vector<std::string> cmds;
#if defined(__APPLE__)
    cmds.push_back(Osascript(macTypes.empty() ? std::string("choose file")
                                              : "choose file of type {" + macTypes + "}"));
#endif
    const std::string zenF  = globs.empty() ? std::string()
                            : " --file-filter=" + ShellQuote(label + " | " + globs) +
                              " --file-filter=" + ShellQuote("All files | *");
    const std::string kdeF  = globs.empty() ? std::string()
                            : " " + ShellQuote(globs + "|" + label);
    cmds.push_back("zenity --file-selection" + zenF + " 2>/dev/null");
    cmds.push_back("kdialog --getopenfilename ." + kdeF + " 2>/dev/null");
    return RunChooser(cmds, outPath) == ChooserResult::Found;
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

bool WebPlatform::OpenURL(const char* url)
{
    if (!url || !*url)
    {
        return false;
    }
    // SDL_OpenURL routes to the OS default browser on native desktop and to
    // window.open() under the Emscripten SDL2 port. Returns 0 on success.
    return SDL_OpenURL(url) == 0;
}

bool WebPlatform::LaunchProcess(const char* path, const char* args, const char* workingDir)
{
    if (!path || !*path)
    {
        return false;
    }
    const std::string dir = (workingDir && *workingDir) ? std::string(workingDir)
                                                        : ParentDir(path);
    // The exe is shell-quoted; `args` is passed through verbatim (it already carries its
    // own quoting around a "<rom>" path, matching how ShellExecute treats the param string
    // on Windows), so the child sees the same argv on both platforms. `exec` so the shell
    // replaces itself with the emulator — then the forked pid *is* the emulator, and we can
    // stop it later (TerminateLaunchedProcess) to relaunch a different game.
    std::string cmd = "cd " + ShellQuote(dir) + " && exec " + ShellQuote(path);
    if (args && *args) { cmd += ' '; cmd += args; }

    const pid_t pid = ::fork();
    if (pid < 0) return false;
    if (pid == 0)
    {
        // Child: new session (survives independent of SE, and terminal signals to SE don't
        // reach it), stdio to /dev/null, then become the emulator via a shell.
        ::setsid();
        const int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0)
        {
            ::dup2(devnull, 0); ::dup2(devnull, 1); ::dup2(devnull, 2);
            if (devnull > 2) ::close(devnull);
        }
        ::execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);   // exec failed
    }
    mLaunchedPid = pid;   // parent: remember it so a relaunch can stop it first
    return true;
}

void WebPlatform::TerminateLaunchedProcess()
{
    if (mLaunchedPid <= 0) return;
    int status = 0;
    // Reap first without blocking: if the emulator already exited it's a zombie holding the
    // pid, so this collects it and we skip the kill (never signal a recycled pid).
    if (::waitpid(mLaunchedPid, &status, WNOHANG) == 0)
    {
        ::kill(mLaunchedPid, SIGTERM);        // still running — ask it to quit
        ::waitpid(mLaunchedPid, &status, 0);  // then reap so we don't leave a zombie
    }
    mLaunchedPid = -1;
}
#endif  // !__EMSCRIPTEN__

// Native desktop HTTPS via libcurl (macOS ships it; Linux with libcurl-dev). The browser
// build and any native build without libcurl inherit IPlatform's "unsupported" default.
#if defined(SE_HAVE_CURL) && !defined(__EMSCRIPTEN__)
static size_t SeCurlWrite(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    const size_t n = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, n);
    return n;
}

bool WebPlatform::HttpsGet(const std::string& url, const std::string& userAgent,
                           HttpResponse& out)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        out.ok = false;
        out.error = "Could not initialize libcurl.";
        return false;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SeCurlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out.body);
    // Accept: GitHub honors a version header; the default JSON is fine either way.
    struct curl_slist* headers = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    const CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK)
    {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        out.status = code;
        out.ok = true;
    }
    else
    {
        out.ok = false;
        out.error = curl_easy_strerror(rc);
    }
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return out.ok;
}
#endif  // SE_HAVE_CURL && !__EMSCRIPTEN__

// --- Audio preview (SDL2; web + native desktop). Fire-and-forget one PCM16 buffer; a new
// Play replaces the previous. The device is (re)opened when the sample rate/channels change. ---
bool WebPlatform::PlayAudio(const int16_t* pcm, size_t frames, int sampleRate, int channels)
{
    if (!mAudioOk || !pcm || frames == 0)
    {
        return false;
    }
    if (channels < 1) channels = 1;
    if (sampleRate < kAudioMinRate || sampleRate > kAudioMaxRate) sampleRate = 44100;

    if (mAudioDev == 0 || sampleRate != mAudioRate || channels != mAudioChannels)
    {
        if (mAudioDev) { SDL_CloseAudioDevice(mAudioDev); mAudioDev = 0; }
        SDL_AudioSpec want;
        SDL_zero(want);
        want.freq = sampleRate;
        want.format = AUDIO_S16SYS;
        want.channels = static_cast<Uint8>(channels);
        want.samples = 1024;
        mAudioDev = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
        if (mAudioDev == 0)
        {
            return false;
        }
        mAudioRate = sampleRate;
        mAudioChannels = channels;
    }

    SDL_ClearQueuedAudio(mAudioDev);
    const Uint32 bytes = static_cast<Uint32>(frames * static_cast<size_t>(channels) * sizeof(int16_t));
    if (SDL_QueueAudio(mAudioDev, pcm, bytes) != 0)
    {
        return false;
    }
    SDL_PauseAudioDevice(mAudioDev, 0);   // unpause = start playing the queued buffer
    return true;
}

}  // namespace sfe
