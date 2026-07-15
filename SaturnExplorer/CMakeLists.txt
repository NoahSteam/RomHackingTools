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
# Savestate / memory-dump driver — portable static library (Seam A reference).
# ---------------------------------------------------------------------------
add_library(SaturnExplorerSavestateDriver STATIC
    Drivers/Savestate/src/SavestateDriver.cpp
)
target_include_directories(SaturnExplorerSavestateDriver
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Savestate/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(SaturnExplorerSavestateDriver PUBLIC SaturnExplorerCore)

# ---------------------------------------------------------------------------
# FrontEnd — reference app (Dear ImGui + Win32 + Direct3D 11). Windows only.
# ---------------------------------------------------------------------------
if(WIN32)
    add_executable(SaturnExplorerFrontEnd WIN32
        FrontEnd/src/App.cpp
        FrontEnd/Platforms/Windows/WindowsPlatform.cpp
        FrontEnd/Platforms/Windows/WinMain.cpp
        FrontEnd/third_party/imgui/imgui.cpp
        FrontEnd/third_party/imgui/imgui_draw.cpp
        FrontEnd/third_party/imgui/imgui_tables.cpp
        FrontEnd/third_party/imgui/imgui_widgets.cpp
        FrontEnd/third_party/imgui/imgui_demo.cpp
        FrontEnd/third_party/imgui/backends/imgui_impl_win32.cpp
        FrontEnd/third_party/imgui/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(SaturnExplorerFrontEnd PRIVATE
        FrontEnd/src
        FrontEnd/third_party/imgui
        FrontEnd/third_party/imgui/backends
    )
    target_compile_definitions(SaturnExplorerFrontEnd PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(SaturnExplorerFrontEnd PRIVATE
        SaturnExplorerCore
        SaturnExplorerSavestateDriver
        d3d11
        dxgi
        d3dcompiler
    )
    # Make F5 in Visual Studio launch the app.
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                 PROPERTY VS_STARTUP_PROJECT SaturnExplorerFrontEnd)
else()
    message(STATUS
        "SaturnExplorer: FrontEnd app is Windows/Direct3D 11 only — "
        "configuring Core + savestate driver, skipping the app on this platform.")
endif()
