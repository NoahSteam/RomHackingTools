cmake_minimum_required(VERSION 3.15)
project(AppKit LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Vendored ImGui. For an in-repo starter we reuse Saturn Explorer's copy; a
# standalone extraction would vendor its own AppKit/third_party/imgui (or fetch
# it with FetchContent). Override with -DAK_IMGUI_DIR=/path/to/imgui.
# ---------------------------------------------------------------------------
set(AK_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../SaturnExplorer/FrontEnd/third_party/imgui"
    CACHE PATH "Path to a Dear ImGui checkout (docking branch)")
set(AK_IMGUI_CORE
    ${AK_IMGUI_DIR}/imgui.cpp
    ${AK_IMGUI_DIR}/imgui_draw.cpp
    ${AK_IMGUI_DIR}/imgui_tables.cpp
    ${AK_IMGUI_DIR}/imgui_widgets.cpp
    ${AK_IMGUI_DIR}/imgui_demo.cpp
)

# Portable app sources — ONE list, shared by every platform branch below. (This
# is deliberately factored out: the project this was extracted from triplicated
# its source list across the three branches, which is how a file ends up listed
# twice. Keep new portable sources here, not in the per-platform add_executable.)
set(AK_APP_SRC
    src/App.cpp
)

set(AK_INCLUDE_COMMON include src ${AK_IMGUI_DIR} ${AK_IMGUI_DIR}/backends)

# Platform selection is implicit in the toolchain (WIN32 / EMSCRIPTEN are set by
# the compiler / emcmake), so there is no user flag to choose a backend.
if(WIN32)
    # Windows: Win32 + Direct3D 11.
    add_executable(AppKitApp WIN32
        ${AK_APP_SRC}
        Platforms/Windows/WindowsPlatform.cpp
        Platforms/Windows/WinMain.cpp
        ${AK_IMGUI_CORE}
        ${AK_IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${AK_IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(AppKitApp PRIVATE ${AK_INCLUDE_COMMON} Platforms/Windows)
    target_compile_definitions(AppKitApp PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(AppKitApp PRIVATE d3d11 dxgi d3dcompiler)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT AppKitApp)

elseif(EMSCRIPTEN)
    # Web: SDL2 + WebGL2/GLES3, via `emcmake cmake -B build-web && cmake --build build-web`.
    add_executable(AppKitApp
        ${AK_APP_SRC}
        Platforms/Web/WebPlatform.cpp
        Platforms/Web/WebMain.cpp
        ${AK_IMGUI_CORE}
        ${AK_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${AK_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(AppKitApp PRIVATE ${AK_INCLUDE_COMMON} Platforms/Web)
    target_compile_definitions(AppKitApp PRIVATE IMGUI_IMPL_OPENGL_ES3)
    target_compile_options(AppKitApp PRIVATE "-sUSE_SDL=2")
    set(AK_WEB_LINK "-sUSE_SDL=2 -sUSE_WEBGL2=1 -sFULL_ES3 -sALLOW_MEMORY_GROWTH \
-sEXPORTED_FUNCTIONS=['_main','_ak_web_load_file','_ak_web_resize','_malloc','_free'] \
-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','UTF8ToString']")
    set(AK_SHELL ${CMAKE_CURRENT_SOURCE_DIR}/Platforms/Web/shell.html)
    string(APPEND AK_WEB_LINK " --shell-file ${AK_SHELL}")
    set_target_properties(AppKitApp PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html"
        LINK_DEPENDS ${AK_SHELL}
        LINK_FLAGS "${AK_WEB_LINK}")

else()
    # Native desktop: build the web backend against system SDL2 + OpenGL — a
    # browser-free way to run and verify AppKit.
    find_package(SDL2 QUIET)
    find_package(OpenGL QUIET)
    if(SDL2_FOUND AND OpenGL_OpenGL_FOUND)
        add_executable(AppKitApp
            ${AK_APP_SRC}
            Platforms/Web/WebPlatform.cpp
            Platforms/Web/WebMain.cpp
            ${AK_IMGUI_CORE}
            ${AK_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
            ${AK_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        )
        target_include_directories(AppKitApp PRIVATE
            ${AK_INCLUDE_COMMON} Platforms/Web ${SDL2_INCLUDE_DIRS})
        target_link_libraries(AppKitApp PRIVATE ${SDL2_LIBRARIES} OpenGL::GL)
    else()
        message(STATUS
            "AppKit: no SDL2/OpenGL found — install SDL2 for a native desktop "
            "build, or use `emcmake cmake` for the web build.")
    endif()
endif()
