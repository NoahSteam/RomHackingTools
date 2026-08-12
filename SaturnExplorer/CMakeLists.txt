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

if(WIN32)
    enable_language(RC)
endif()

include(CTest)

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

if(BUILD_TESTING)
    add_executable(SaturnExplorerVdp2Tests
        Core/tests/Vdp2CompositorTests.cpp
    )
    target_link_libraries(SaturnExplorerVdp2Tests PRIVATE SaturnExplorerCore)
    add_test(NAME SaturnExplorerVdp2Tests COMMAND SaturnExplorerVdp2Tests)

    add_executable(SaturnExplorerTopBarTests
        FrontEnd/tests/TopBarTests.cpp
        FrontEnd/src/TopBar.cpp
        FrontEnd/src/Launcher.cpp
        FrontEnd/src/Settings.cpp
        FrontEnd/src/DataSearch.cpp
        FrontEnd/src/Prs.cpp
    )
    target_include_directories(SaturnExplorerTopBarTests PRIVATE FrontEnd/src)
    if(WIN32)
        target_compile_definitions(SaturnExplorerTopBarTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    add_test(NAME SaturnExplorerTopBarTests COMMAND SaturnExplorerTopBarTests)

    add_executable(SaturnExplorerConditionEvalTests
        FrontEnd/tests/ConditionEvalTests.cpp
        FrontEnd/src/Debug/ConditionEval.cpp
    )
    target_include_directories(SaturnExplorerConditionEvalTests PRIVATE FrontEnd/src)
    add_test(NAME SaturnExplorerConditionEvalTests COMMAND SaturnExplorerConditionEvalTests)

    add_executable(SaturnExplorerMemorySearchTests
        FrontEnd/tests/MemorySearchTests.cpp
        FrontEnd/src/Debug/MemorySearch.cpp
        FrontEnd/src/Debug/WatchList.cpp
    )
    target_include_directories(SaturnExplorerMemorySearchTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerMemorySearchTests COMMAND SaturnExplorerMemorySearchTests)

    add_executable(SaturnExplorerAccessLogTests
        FrontEnd/tests/AccessLogTests.cpp
        FrontEnd/src/Debug/AccessLog.cpp
    )
    target_include_directories(SaturnExplorerAccessLogTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerAccessLogTests COMMAND SaturnExplorerAccessLogTests)

    add_executable(SaturnExplorerFunctionNamesTests
        FrontEnd/tests/FunctionNamesTests.cpp
        FrontEnd/src/Debug/CallStack.cpp
    )
    target_include_directories(SaturnExplorerFunctionNamesTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerFunctionNamesTests COMMAND SaturnExplorerFunctionNamesTests)

    add_executable(SaturnExplorerM68kDisasmTests
        FrontEnd/tests/M68kDisasmTests.cpp
        FrontEnd/src/Debug/M68kDisasm.cpp
    )
    target_include_directories(SaturnExplorerM68kDisasmTests PRIVATE FrontEnd/src)
    add_test(NAME SaturnExplorerM68kDisasmTests COMMAND SaturnExplorerM68kDisasmTests)

    add_executable(SaturnExplorerIsoFsTests
        FrontEnd/tests/IsoFsTests.cpp
        FrontEnd/src/Disc/IsoFs.cpp
    )
    target_include_directories(SaturnExplorerIsoFsTests PRIVATE FrontEnd/src)
    add_test(NAME SaturnExplorerIsoFsTests COMMAND SaturnExplorerIsoFsTests)

    # Savestate delta codec (XOR + zero-run RLE) — the v16 rewind stream primitives. Header-
    # only; the test just needs the Integration/Common include path.
    add_executable(SaturnExplorerSeStateCodecTests
        Core/tests/SeStateCodecTests.cpp
    )
    target_include_directories(SaturnExplorerSeStateCodecTests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Integration/Common)
    add_test(NAME SaturnExplorerSeStateCodecTests COMMAND SaturnExplorerSeStateCodecTests)

    # FrameRecorder rewind machinery — attach/reconstruct/truncate/evict of per-frame
    # savestate blocks. Drives Capture() against a trivial context (needs the Core lib), so
    # it also uses threads (the recorder's compression worker).
    add_executable(SaturnExplorerFrameRecorderTests
        FrontEnd/tests/FrameRecorderTests.cpp
        FrontEnd/src/FrameRecorder.cpp
        FrontEnd/src/FrameLz.cpp
    )
    target_include_directories(SaturnExplorerFrameRecorderTests PRIVATE
        FrontEnd/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Integration/Common)
    target_link_libraries(SaturnExplorerFrameRecorderTests PRIVATE SaturnExplorerCore)
    find_package(Threads REQUIRED)
    target_link_libraries(SaturnExplorerFrameRecorderTests PRIVATE Threads::Threads)
    add_test(NAME SaturnExplorerFrameRecorderTests COMMAND SaturnExplorerFrameRecorderTests)

    # PatchLibrary — known-locations model, project I/O, and Python patch-script emission.
    # The e2e leg runs the emitted script with python3 when available (skips gracefully if not).
    add_executable(SaturnExplorerPatchLibraryTests
        FrontEnd/tests/PatchLibraryTests.cpp
        FrontEnd/src/PatchLibrary.cpp
    )
    target_include_directories(SaturnExplorerPatchLibraryTests PRIVATE FrontEnd/src)
    add_test(NAME SaturnExplorerPatchLibraryTests COMMAND SaturnExplorerPatchLibraryTests)
endif()

# ---------------------------------------------------------------------------
# se-render — headless golden-frame harness (roadmap E1): render a .sedump through
# the core and diff it against a reference PPM. A plain CLI utility, native only.
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
    add_executable(se-render Core/tools/SeRender.cpp)
    target_link_libraries(se-render PRIVATE SaturnExplorerCore)
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

# Bake the git commit into BuildInfo.h at build time so the in-app update check knows
# exactly which build it is. The custom target re-runs the resolver every build; the
# resolver only rewrites the header when the SHA changes, so it doesn't force rebuilds.
set(SE_BUILDINFO_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${SE_BUILDINFO_DIR})
add_custom_target(SaturnExplorerBuildInfo
    BYPRODUCTS ${SE_BUILDINFO_DIR}/BuildInfo.h
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/FrontEnd/BuildInfo.h.in
        -DOUT_FILE=${SE_BUILDINFO_DIR}/BuildInfo.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateBuildInfo.cmake
    COMMENT "Resolving build commit -> BuildInfo.h"
    VERBATIM)

if(WIN32)
    add_executable(SaturnExplorerFrontEnd WIN32
        FrontEnd/src/App.cpp
        FrontEnd/src/Theme.cpp
        FrontEnd/src/DataSearch.cpp
        FrontEnd/src/Prs.cpp
        FrontEnd/src/Settings.cpp
        FrontEnd/src/Launcher.cpp
        FrontEnd/src/TopBar.cpp
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
        FrontEnd/src/Debug/ConditionEval.cpp
        FrontEnd/src/Debug/MemorySearch.cpp
        FrontEnd/src/Debug/AccessLog.cpp
        FrontEnd/src/Debug/M68kDisasm.cpp
        FrontEnd/src/Disc/IsoFs.cpp
        FrontEnd/src/Disc/DiscImage.cpp
        FrontEnd/src/Debug/ExecutionActions.cpp
        FrontEnd/src/Debug/CallStack.cpp
        FrontEnd/src/FrameRecorder.cpp
        FrontEnd/src/FrameLz.cpp
        FrontEnd/src/PatchLibrary.cpp
        FrontEnd/src/UpdateChecker.cpp
        FrontEnd/Platforms/Windows/WindowsPlatform.cpp
        FrontEnd/Platforms/Windows/WinMain.cpp
        FrontEnd/Platforms/Windows/SaturnExplorer.rc
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
        winhttp                 # IPlatform::HttpsGet (update check)
        winmm                   # IPlatform::PlayAudio (waveOut sound preview)
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
        FrontEnd/src/Prs.cpp
        FrontEnd/src/Settings.cpp
        FrontEnd/src/Launcher.cpp
        FrontEnd/src/TopBar.cpp
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
        FrontEnd/src/Debug/ConditionEval.cpp
        FrontEnd/src/Debug/MemorySearch.cpp
        FrontEnd/src/Debug/AccessLog.cpp
        FrontEnd/src/Debug/M68kDisasm.cpp
        FrontEnd/src/Disc/IsoFs.cpp
        FrontEnd/src/Disc/DiscImage.cpp
        FrontEnd/src/Debug/ExecutionActions.cpp
        FrontEnd/src/Debug/CallStack.cpp
        FrontEnd/src/UpdateChecker.cpp
        FrontEnd/Platforms/Web/WebPlatform.cpp
        FrontEnd/Platforms/Web/WebMain.cpp
        ${SE_IMGUI_CORE}
        ${SE_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${SE_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    if(SE_WEB_LIVE)
        list(APPEND SE_WEB_SRC FrontEnd/src/FrameRecorder.cpp FrontEnd/src/FrameLz.cpp
                               FrontEnd/src/PatchLibrary.cpp)
    endif()
    add_executable(SaturnExplorerFrontEnd ${SE_WEB_SRC})
    target_include_directories(SaturnExplorerFrontEnd PRIVATE
        FrontEnd/src
        FrontEnd/Platforms/Web
        ${SE_IMGUI_DIR}
        ${SE_IMGUI_DIR}/backends
        # SeLiveProtocol.h (SE_PAD_* masks, protocol constants) is included
        # unconditionally by ControllerPanel.cpp/App.cpp, so the path is needed
        # even in the default (non-live) web build, not just under SE_WEB_LIVE.
        ${CMAKE_CURRENT_SOURCE_DIR}/Integration/Common
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
            FrontEnd/src/Prs.cpp
            FrontEnd/src/Settings.cpp
            FrontEnd/src/Launcher.cpp
            FrontEnd/src/TopBar.cpp
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
            FrontEnd/src/Debug/ConditionEval.cpp
            FrontEnd/src/Debug/MemorySearch.cpp
            FrontEnd/src/Debug/AccessLog.cpp
            FrontEnd/src/Debug/M68kDisasm.cpp
            FrontEnd/src/Disc/IsoFs.cpp
            FrontEnd/src/Disc/DiscImage.cpp
            FrontEnd/src/Debug/ExecutionActions.cpp
            FrontEnd/src/Debug/CallStack.cpp
            FrontEnd/src/FrameRecorder.cpp
            FrontEnd/src/FrameLz.cpp
            FrontEnd/src/PatchLibrary.cpp
            FrontEnd/src/UpdateChecker.cpp
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
        # Native desktop (Linux/macOS) HttpsGet uses libcurl when available (macOS ships
        # it). Without it the update check degrades to "unsupported" — no hard dependency.
        find_package(CURL QUIET)
        if(CURL_FOUND)
            target_compile_definitions(SaturnExplorerFrontEnd PRIVATE SE_HAVE_CURL)
            target_link_libraries(SaturnExplorerFrontEnd PRIVATE CURL::libcurl)
        else()
            message(STATUS "SaturnExplorer: libcurl not found — the in-app update check "
                           "will report 'unsupported' on this desktop build.")
        endif()
        # macOS: build a double-clickable SaturnExplorer.app bundle (parity with the Windows
        # .exe) so the app gets a Dock icon + proper window focus instead of running as a bare
        # CLI binary. A plain build is dynamically linked against Homebrew/system SDL2 — fine
        # locally; the SE_MACOS_BUNDLE_LIBS path below makes it self-contained + signable.
        if(APPLE)
            # Dock / Finder icon — the macOS counterpart of the Windows .ico wired via
            # Platforms/Windows/SaturnExplorer.rc. The .icns is generated from that same
            # .ico (see FrontEnd/Platforms/macOS/). Adding it as a target source with a
            # Resources package location makes CMake copy it into the .app's Contents/
            # Resources; MACOSX_BUNDLE_ICON_FILE points CFBundleIconFile at it.
            set(SE_MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/FrontEnd/Platforms/macOS/SaturnExplorer.icns")
            target_sources(SaturnExplorerFrontEnd PRIVATE "${SE_MACOS_ICON}")
            set_source_files_properties("${SE_MACOS_ICON}" PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources")
            set_target_properties(SaturnExplorerFrontEnd PROPERTIES
                OUTPUT_NAME "SaturnExplorer"
                MACOSX_BUNDLE TRUE
                MACOSX_BUNDLE_BUNDLE_NAME "Saturn Explorer"
                MACOSX_BUNDLE_GUI_IDENTIFIER "com.noahsteam.saturnexplorer"
                MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
                MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
                MACOSX_BUNDLE_ICON_FILE "SaturnExplorer.icns")

            # Distributable, self-contained .app: at `cmake --install` (or `cpack`) copy the
            # dynamic deps (SDL2, …) into the bundle and rewrite their install names, then
            # optionally code-sign. Off by default (a plain build stays fast + Homebrew-linked);
            # enable with -DSE_MACOS_BUNDLE_LIBS=ON. See BUILD.md "macOS packaging".
            option(SE_MACOS_BUNDLE_LIBS "macOS: bundle dylibs into the .app (self-contained)" OFF)
            if(SE_MACOS_BUNDLE_LIBS)
                set(SE_MACOS_SIGN_IDENTITY "" CACHE STRING
                    "codesign identity for the .app (e.g. 'Developer ID Application: …'); empty = skip signing")
                install(TARGETS SaturnExplorerFrontEnd BUNDLE DESTINATION .)
                set(SE_APP_PATH "\${CMAKE_INSTALL_PREFIX}/SaturnExplorer.app")
                # fixup_bundle follows the exe's dylib dependencies; the dir hints resolve
                # Homebrew/@rpath libraries on both Apple-silicon and Intel prefixes.
                install(CODE "
                    include(BundleUtilities)
                    set(BU_CHMOD_BUNDLE_ITEMS ON)
                    fixup_bundle(\"${SE_APP_PATH}\" \"\" \"/opt/homebrew/lib;/usr/local/lib\")
                ")
                if(SE_MACOS_SIGN_IDENTITY)
                    install(CODE "execute_process(COMMAND codesign --force --deep --timestamp
                        --options runtime --sign \"${SE_MACOS_SIGN_IDENTITY}\" \"${SE_APP_PATH}\")")
                endif()
                # `cpack -G DragNDrop` then produces a .dmg of the finished bundle.
                set(CPACK_GENERATOR "DragNDrop")
                set(CPACK_PACKAGE_NAME "SaturnExplorer")
                set(CPACK_PACKAGE_FILE_NAME "SaturnExplorer-macos")
                include(CPack)
            endif()
        endif()
    else()
        message(STATUS
            "SaturnExplorer: no SDL2/OpenGL found — configuring Core + savestate "
            "driver only. Use `emcmake cmake` for the web app, or install SDL2 for "
            "a native desktop build.")
    endif()
endif()

# Every frontend target consumes the generated BuildInfo.h and must be (re)generated
# before it compiles UpdateChecker.cpp.
if(TARGET SaturnExplorerFrontEnd)
    add_dependencies(SaturnExplorerFrontEnd SaturnExplorerBuildInfo)
    target_include_directories(SaturnExplorerFrontEnd PRIVATE ${SE_BUILDINFO_DIR})
endif()
