# Saturn Explorer — cross-platform build.
#
# CMake generates the native build files for whatever toolchain you use, so you
# do not have to use Visual Studio. Examples:
#
#   Visual Studio solution (Windows):
#     cmake -B build -G "Visual Studio 17 2022" -A x64
#     cmake --build build --config Debug
#     (or just open build/SaturnExplorer.sln)
#
#   Ninja / Makefiles (Windows, macOS, Linux):
#     cmake -B build
#     cmake --build build
#
# The Core engine and the savestate driver are portable and build everywhere.
# The FrontEnd app is Win32 + Direct3D 11, so it is built only on Windows; on
# other platforms CMake configures the two libraries and skips the app.

cmake_minimum_required(VERSION 3.15)
project(SaturnExplorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Put binaries in one place regardless of generator.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Web live mode (opt-in). The browser has no local socket, so live connect tunnels a
# TCP connection over a WebSocket->TCP bridge; that needs Emscripten's proxied POSIX
# sockets + pthreads, which in turn need a COOP/COEP cross-origin-isolated host for
# SharedArrayBuffer. GitHub Pages can't set those headers, so the default web build
# stays viewer-only and this is off by default. Turn on with -DSE_WEB_LIVE=ON and host
# on a cross-origin-isolated server (see Integration/Yabause/README.md "Web live").
option(SE_WEB_LIVE "Emscripten: include the live driver (needs pthreads + a COOP/COEP host)" OFF)

# ---------------------------------------------------------------------------
# Core engine — portable static library (no UI, no OS types in public headers).
# ---------------------------------------------------------------------------
add_library(SaturnExplorerCore STATIC
    Core/src/HardwareSnapshot.cpp
    Core/src/HostAbi.cpp
    Core/src/Vdp1Parser.cpp
    Core/src/GeometryBuilder.cpp
    Core/src/Vdp1Rasterizer.cpp
    Core/src/Vdp2Compositor.cpp
)
target_include_directories(SaturnExplorerCore
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Core/src
)

# ---------------------------------------------------------------------------
# Driver common — shared, portable helpers (endian normalization + VDP2 register
# image reconstruction) used by the savestate and live drivers.
# ---------------------------------------------------------------------------
add_library(SaturnExplorerDriverCommon STATIC
    Drivers/Common/src/SaturnStateShared.cpp
)
target_include_directories(SaturnExplorerDriverCommon
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Common/src
           ${CMAKE_CURRENT_SOURCE_DIR}/include   # se_sh2_regs (SaturnStateShared.h)
)

# ---------------------------------------------------------------------------
# Savestate / memory-dump driver — portable static library (Seam A reference).
# ---------------------------------------------------------------------------
add_library(SaturnExplorerSavestateDriver STATIC
    Drivers/Savestate/src/SavestateDriver.cpp
)
target_include_directories(SaturnExplorerSavestateDriver
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Savestate/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(SaturnExplorerSavestateDriver
    PUBLIC SaturnExplorerCore SaturnExplorerDriverCommon)
# Real Mednafen mcs/ savestates are gzip-compressed (magic 1f 8b). zlib lets the
# driver transparently inflate them; without it the driver only reads already-
# decompressed states. Optional — SE_HAVE_ZLIB gates the inflate path.
if(EMSCRIPTEN)
    # Emscripten ships a zlib port; -sUSE_ZLIB makes <zlib.h> + libz available.
    target_compile_options(SaturnExplorerSavestateDriver PUBLIC "-sUSE_ZLIB=1")
    target_link_options(SaturnExplorerSavestateDriver PUBLIC "-sUSE_ZLIB=1")
    target_compile_definitions(SaturnExplorerSavestateDriver PRIVATE SE_HAVE_ZLIB)
else()
    find_package(ZLIB)
    if(ZLIB_FOUND)
        target_link_libraries(SaturnExplorerSavestateDriver PUBLIC ZLIB::ZLIB)
        target_compile_definitions(SaturnExplorerSavestateDriver PRIVATE SE_HAVE_ZLIB)
    else()
        message(STATUS "zlib not found — Mednafen gzip savestates will not decompress")
    endif()
endif()

# ---------------------------------------------------------------------------
# Live driver — reads a running, patched emulator over a local socket (native) or a
# WebSocket->TCP bridge (web). Uses threads + sockets, so on Emscripten it is built
# only for the opt-in web-live build (-DSE_WEB_LIVE=ON).
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN OR SE_WEB_LIVE)
    add_library(SaturnExplorerLiveDriver STATIC
        Drivers/Live/src/LiveDriver.cpp
    )
    target_include_directories(SaturnExplorerLiveDriver
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Live/src
                # The wire protocol (SeLiveProtocol.h) is the single source of truth
                # shared by the LiveDriver and every emulator's export server (Yabause,
                # Mednafen, ...); it lives in Integration/Common with the portable
                # se_export module the per-emulator patchers copy in. PUBLIC so the
                # FrontEnd (which #includes it under SE_ENABLE_LIVE) inherits the path.
                ${CMAKE_CURRENT_SOURCE_DIR}/Integration/Common
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(SaturnExplorerLiveDriver
        PUBLIC SaturnExplorerCore SaturnExplorerDriverCommon)
    if(EMSCRIPTEN)
        # Web: BSD socket calls are proxied to a worker; pthreads carry both that proxy
        # and the driver's own poll thread. PUBLIC so the FrontEnd inherits -pthread.
        target_compile_options(SaturnExplorerLiveDriver PUBLIC -pthread)
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(SaturnExplorerLiveDriver PUBLIC Threads::Threads)
        if(WIN32)
            target_link_libraries(SaturnExplorerLiveDriver PUBLIC ws2_32)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# FrontEnd — reference app (Dear ImGui). Three interchangeable Seam C backends:
#   * Windows  : Win32 + Direct3D 11        (native desktop)
#   * Emscripten: SDL2 + WebGL2/GLES3        (browser, via `emcmake cmake`)
#   * Desktop  : SDL2 + OpenGL (if SDL2 found) — a local build of the web backend
#                so the frontend can be validated without a browser.
# The portable App + all panels are shared; only the backend + entry point differ.
# ---------------------------------------------------------------------------
set(SE_IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/FrontEnd/third_party/imgui)
set(SE_IMGUI_CORE
    ${SE_IMGUI_DIR}/imgui.cpp
    ${SE_IMGUI_DIR}/imgui_draw.cpp
    ${SE_IMGUI_DIR}/imgui_tables.cpp
    ${SE_IMGUI_DIR}/imgui_widgets.cpp
    ${SE_IMGUI_DIR}/imgui_demo.cpp
)

if(WIN32)
    add_executable(SaturnExplorerFrontEnd WIN32
        FrontEnd/src/App.cpp
        FrontEnd/src/Theme.cpp
        FrontEnd/src/DataSearch.cpp
        FrontEnd/src/Settings.cpp
        FrontEnd/src/WatchPanel.cpp
        FrontEnd/src/AssemblyPanel.cpp
        FrontEnd/src/HexEditorPanel.cpp
        FrontEnd/src/ControllerPanel.cpp
        FrontEnd/src/LogPanel.cpp
        FrontEnd/src/Debug/WatchList.cpp
        FrontEnd/src/Debug/MemoryBackend.cpp
        FrontEnd/src/Debug/Sh2Disasm.cpp
        FrontEnd/src/Debug/BreakpointManager.cpp
        FrontEnd/src/Debug/ShiftJis.cpp
        FrontEnd/src/Debug/FormatString.cpp
        FrontEnd/src/Debug/ExecutionActions.cpp
        FrontEnd/src/FrameRecorder.cpp
        FrontEnd/src/FrameLz.cpp
        FrontEnd/Platforms/Windows/WindowsPlatform.cpp
        FrontEnd/Platforms/Windows/WinMain.cpp
        ${SE_IMGUI_CORE}
        ${SE_IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${SE_IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(SaturnExplorerFrontEnd PRIVATE
        FrontEnd/src
        ${SE_IMGUI_DIR}
        ${SE_IMGUI_DIR}/backends
    )
    target_compile_definitions(SaturnExplorerFrontEnd PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        SE_ENABLE_LIVE
    )
    target_include_directories(SaturnExplorerFrontEnd PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Live/src)
    target_link_libraries(SaturnExplorerFrontEnd PRIVATE
        SaturnExplorerCore
        SaturnExplorerSavestateDriver
        SaturnExplorerLiveDriver
        d3d11
        dxgi
        d3dcompiler
    )
    # Make F5 in Visual Studio launch the app.
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                 PROPERTY VS_STARTUP_PROJECT SaturnExplorerFrontEnd)

elseif(EMSCRIPTEN)
    # Web build: `emcmake cmake -B build-web && cmake --build build-web`.
    # -DSE_WEB_LIVE=ON additionally links the LiveDriver + the frame recorder
    # (pause/scrub) and Emscripten's proxied POSIX sockets + pthreads for live connect.
    set(SE_WEB_SRC
        FrontEnd/src/App.cpp
        FrontEnd/src/Theme.cpp
        FrontEnd/src/DataSearch.cpp
        FrontEnd/src/Settings.cpp
        FrontEnd/src/WatchPanel.cpp
        FrontEnd/src/AssemblyPanel.cpp
        FrontEnd/src/HexEditorPanel.cpp
        FrontEnd/src/ControllerPanel.cpp
        FrontEnd/src/LogPanel.cpp
        FrontEnd/src/Debug/WatchList.cpp
        FrontEnd/src/Debug/MemoryBackend.cpp
        FrontEnd/src/Debug/Sh2Disasm.cpp
        FrontEnd/src/Debug/BreakpointManager.cpp
        FrontEnd/src/Debug/ShiftJis.cpp
        FrontEnd/src/Debug/FormatString.cpp
        FrontEnd/src/Debug/ExecutionActions.cpp
        FrontEnd/Platforms/Web/WebPlatform.cpp
        FrontEnd/Platforms/Web/WebMain.cpp
        ${SE_IMGUI_CORE}
        ${SE_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${SE_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    if(SE_WEB_LIVE)
        list(APPEND SE_WEB_SRC FrontEnd/src/FrameRecorder.cpp FrontEnd/src/FrameLz.cpp)
    endif()
    add_executable(SaturnExplorerFrontEnd ${SE_WEB_SRC})
    target_include_directories(SaturnExplorerFrontEnd PRIVATE
        FrontEnd/src
        FrontEnd/Platforms/Web
        ${SE_IMGUI_DIR}
        ${SE_IMGUI_DIR}/backends
    )
    # The OpenGL3 backend must target GLES3 (WebGL2).
    target_compile_definitions(SaturnExplorerFrontEnd PRIVATE IMGUI_IMPL_OPENGL_ES3)
    # SDL2 is an Emscripten port; the flag is needed at compile (headers) + link.
    target_compile_options(SaturnExplorerFrontEnd PRIVATE "-sUSE_SDL=2")
    target_link_libraries(SaturnExplorerFrontEnd PRIVATE
        SaturnExplorerCore
        SaturnExplorerSavestateDriver
    )
    # Base (viewer) link flags; live mode appends pthreads + the socket bridge below.
    set(SE_WEB_LINK "-sUSE_SDL=2 -sUSE_WEBGL2=1 -sFULL_ES3 -sALLOW_MEMORY_GROWTH \
-sEXPORTED_FUNCTIONS=['_main','_se_web_load_file','_se_web_resize','_malloc','_free'] \
-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','UTF8ToString']")
    if(SE_WEB_LIVE)
        target_compile_definitions(SaturnExplorerFrontEnd PRIVATE SE_ENABLE_LIVE)
        target_compile_options(SaturnExplorerFrontEnd PRIVATE -pthread)
        target_include_directories(SaturnExplorerFrontEnd PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Live/src)
        target_link_libraries(SaturnExplorerFrontEnd PRIVATE SaturnExplorerLiveDriver)
        # pthreads carry the driver's poll thread + recorder worker + the socket proxy;
        # BSD sockets are tunneled to a WebSocket bridge (default ws://localhost:8846).
        # Requires a COOP/COEP cross-origin-isolated host at runtime (SharedArrayBuffer).
        string(APPEND SE_WEB_LINK " -pthread -sPTHREAD_POOL_SIZE=8 \
-sPROXY_POSIX_SOCKETS -sWEBSOCKET_URL=ws://localhost:8846")
    endif()
    set(SE_SHELL ${CMAKE_CURRENT_SOURCE_DIR}/FrontEnd/Platforms/Web/shell.html)
    string(APPEND SE_WEB_LINK " --shell-file ${SE_SHELL}")
    set_target_properties(SaturnExplorerFrontEnd PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html"
        LINK_DEPENDS ${SE_SHELL}
        LINK_FLAGS "${SE_WEB_LINK}")

else()
    # Non-Windows desktop: build the web backend natively against system SDL2 +
    # OpenGL when available (a browser-free way to run/validate the frontend).
    find_package(SDL2 QUIET)
    find_package(OpenGL QUIET)
    if(SDL2_FOUND AND OpenGL_OpenGL_FOUND)
        add_executable(SaturnExplorerFrontEnd
            FrontEnd/src/App.cpp
            FrontEnd/src/Theme.cpp
            FrontEnd/src/DataSearch.cpp
            FrontEnd/src/Settings.cpp
            FrontEnd/src/WatchPanel.cpp
            FrontEnd/src/AssemblyPanel.cpp
            FrontEnd/src/HexEditorPanel.cpp
            FrontEnd/src/ControllerPanel.cpp
            FrontEnd/src/LogPanel.cpp
            FrontEnd/src/Debug/WatchList.cpp
            FrontEnd/src/Debug/MemoryBackend.cpp
            FrontEnd/src/Debug/Sh2Disasm.cpp
            FrontEnd/src/Debug/BreakpointManager.cpp
            FrontEnd/src/Debug/ShiftJis.cpp
            FrontEnd/src/Debug/FormatString.cpp
            FrontEnd/src/Debug/ExecutionActions.cpp
            FrontEnd/src/FrameRecorder.cpp
            FrontEnd/src/FrameLz.cpp
            FrontEnd/Platforms/Web/WebPlatform.cpp
            FrontEnd/Platforms/Web/WebMain.cpp
            ${SE_IMGUI_CORE}
            ${SE_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
            ${SE_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        )
        target_include_directories(SaturnExplorerFrontEnd PRIVATE
            FrontEnd/src
            FrontEnd/Platforms/Web
            ${SE_IMGUI_DIR}
            ${SE_IMGUI_DIR}/backends
            ${SDL2_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Live/src
        )
        target_compile_definitions(SaturnExplorerFrontEnd PRIVATE SE_ENABLE_LIVE)
        target_link_libraries(SaturnExplorerFrontEnd PRIVATE
            SaturnExplorerCore
            SaturnExplorerSavestateDriver
            SaturnExplorerLiveDriver
            ${SDL2_LIBRARIES}
            OpenGL::GL
        )
    else()
        message(STATUS
            "SaturnExplorer: no SDL2/OpenGL found — configuring Core + savestate "
            "driver only. Use `emcmake cmake` for the web app, or install SDL2 for "
            "a native desktop build.")
    endif()
endif()
