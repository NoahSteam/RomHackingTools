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

# What the savestate parsers do with a container whose shape is not what the format declares,
# and with one that holds only part of the machine. Synthesizes the .yss and MDFNSVST containers
# so the tests reach the code a user's file goes through.
if(BUILD_TESTING)
    add_executable(SaturnExplorerSavestateShapeTests
        Drivers/Savestate/tests/SavestateShapeTests.cpp)
    target_link_libraries(SaturnExplorerSavestateShapeTests
        PRIVATE SaturnExplorerSavestateDriver)
    add_test(NAME SaturnExplorerSavestateShapeTests
             COMMAND SaturnExplorerSavestateShapeTests)

    # And the same parser against real Yabause states, so the structural validation is checked
    # against files that exist rather than only against synthesized ones. Skips itself when the
    # game data is not in the checkout.
    add_executable(SaturnExplorerYssFixtureTests
        Drivers/Savestate/tests/YssFixtureTests.cpp)
    target_link_libraries(SaturnExplorerYssFixtureTests
        PRIVATE SaturnExplorerSavestateDriver)
    target_compile_definitions(SaturnExplorerYssFixtureTests PRIVATE
        SE_YSS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../SakuraTaisen2/Data/SaveFiles/SaveStates/Yabause/OhrimeMiniGame")
    add_test(NAME SaturnExplorerYssFixtureTests
             COMMAND SaturnExplorerYssFixtureTests)
endif()

# Does the Windows live tap's shutdown actually end its server thread (HOOK-01)? The threads it
# has to join are parked in synchronous named-pipe calls, so this needs the real Win32 pipe
# implementation -- it is built on Windows, and cross-compiles to run under wine from a Linux
# checkout (the command is in the test's header comment).
if(BUILD_TESTING AND WIN32)
    add_executable(SaturnExplorerExportShutdownTests
        Integration/Common/tests/SeExportShutdownTests.c
        Integration/Common/se_export.c)
    target_include_directories(SaturnExplorerExportShutdownTests
        PRIVATE Integration/Common ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerExportShutdownTests
             COMMAND SaturnExplorerExportShutdownTests)
endif()

# Does the live tap leave the emulator alone while nothing is attached? The per-frame capture
# (ring copy + a full savestate for rewind) has no reader until Saturn Explorer connects, and it
# costs real frame rate, so this pins that it does not run. Needs the real unix-socket listener,
# so it is the POSIX counterpart of the Windows-only shutdown test above.
if(BUILD_TESTING AND NOT WIN32 AND NOT EMSCRIPTEN)
    enable_language(C)
    find_package(Threads REQUIRED)   # se_export runs its listener on a thread
    add_executable(SaturnExplorerExportAttachTests
        Integration/Common/tests/SeExportAttachTests.c
        Integration/Common/se_export.c)
    target_include_directories(SaturnExplorerExportAttachTests
        PRIVATE Integration/Common)
    target_link_libraries(SaturnExplorerExportAttachTests PRIVATE Threads::Threads)
    add_test(NAME SaturnExplorerExportAttachTests
             COMMAND SaturnExplorerExportAttachTests)
endif()

# The shared normalization helpers: what they do with input that is not the shape the
# savestate/live formats promised. Portable, no emulator and no socket needed.
if(BUILD_TESTING)
    add_executable(SaturnExplorerStateSharedTests
        Drivers/Common/tests/StateSharedTests.cpp)
    target_link_libraries(SaturnExplorerStateSharedTests PRIVATE SaturnExplorerDriverCommon)
    add_test(NAME SaturnExplorerStateSharedTests COMMAND SaturnExplorerStateSharedTests)
endif()

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

# Dear ImGui sources. Defined here rather than with the FrontEnd target because the
# headless panel interaction tests link the same core (no backend, no window).
set(SE_IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/FrontEnd/third_party/imgui)
set(SE_IMGUI_CORE
    ${SE_IMGUI_DIR}/imgui.cpp
    ${SE_IMGUI_DIR}/imgui_draw.cpp
    ${SE_IMGUI_DIR}/imgui_tables.cpp
    ${SE_IMGUI_DIR}/imgui_widgets.cpp
    ${SE_IMGUI_DIR}/imgui_demo.cpp
)

if(BUILD_TESTING)
    add_executable(SaturnExplorerVdp2Tests
        Core/tests/Vdp2CompositorTests.cpp
    )
    target_link_libraries(SaturnExplorerVdp2Tests PRIVATE SaturnExplorerCore)
    add_test(NAME SaturnExplorerVdp2Tests COMMAND SaturnExplorerVdp2Tests)

    # CRAM index wrapping: exact on the hardware sizes, and still correct on a partially
    # captured CRAM, where power-of-two masking is not modulo.
    add_executable(SaturnExplorerCramWrapTests
        Core/tests/CramWrapTests.cpp
    )
    target_link_libraries(SaturnExplorerCramWrapTests PRIVATE SaturnExplorerCore)
    # Core/src is PRIVATE to the Core library on purpose — the other Core tests go through the
    # public ABI. This one asserts on a decode helper the ABI has no direct route to, so it
    # needs the internal header; the include stays on this target rather than widening Core's.
    # (Which is also why the snapshot-validity tests that were once here live in
    # Vdp2CompositorTests: they are ABI-level, and belong on the target that has no such reach.)
    target_include_directories(SaturnExplorerCramWrapTests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Core/src)
    add_test(NAME SaturnExplorerCramWrapTests COMMAND SaturnExplorerCramWrapTests)

    # Seam B's no-throw contract, exercised by replacing operator new so the core's
    # allocations fail while the harness's own still work.
    add_executable(SaturnExplorerAbiNoThrowTests
        Core/tests/AbiNoThrowTests.cpp
    )
    target_link_libraries(SaturnExplorerAbiNoThrowTests PRIVATE SaturnExplorerCore)
    target_include_directories(SaturnExplorerAbiNoThrowTests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Core/tests)
    add_test(NAME SaturnExplorerAbiNoThrowTests COMMAND SaturnExplorerAbiNoThrowTests)

    # 3D View orientation — the exploded world view checked against the 2D composite.
    add_executable(SaturnExplorerGeometry3DTests
        Core/tests/Geometry3DTests.cpp
    )
    target_link_libraries(SaturnExplorerGeometry3DTests PRIVATE SaturnExplorerCore)
    add_test(NAME SaturnExplorerGeometry3DTests COMMAND SaturnExplorerGeometry3DTests)

    add_executable(SaturnExplorerTopBarTests
        FrontEnd/tests/TopBarTests.cpp
        FrontEnd/src/TopBar.cpp
        FrontEnd/src/NativeMenu.cpp
        FrontEnd/src/Launcher.cpp
        FrontEnd/src/Settings.cpp
        FrontEnd/src/DataSearch.cpp
        FrontEnd/src/DataSearchRunner.cpp
        FrontEnd/src/Prs.cpp
    )
    target_include_directories(SaturnExplorerTopBarTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
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

    # The byte-sequence scan behind the Memory panel's "find these bytes in RAM": a match
    # straddling the 64 KiB read chunk, overlapping occurrences, an unreadable region, the cap.
    add_executable(SaturnExplorerBytePatternSearchTests
        FrontEnd/tests/BytePatternSearchTests.cpp
        FrontEnd/src/Debug/BytePatternSearch.cpp
    )
    target_include_directories(SaturnExplorerBytePatternSearchTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerBytePatternSearchTests
             COMMAND SaturnExplorerBytePatternSearchTests)

    add_executable(SaturnExplorerMemorySearchTests
        FrontEnd/tests/MemorySearchTests.cpp
        FrontEnd/src/Debug/MemorySearch.cpp
        FrontEnd/src/Debug/WatchList.cpp
    )
    target_include_directories(SaturnExplorerMemorySearchTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerMemorySearchTests COMMAND SaturnExplorerMemorySearchTests)

    # The worker that keeps a RAM scan off the UI thread: same results as the synchronous scan,
    # one scan at a time, and the sparse narrowing pass reads only the pages the hits are in.
    add_executable(SaturnExplorerMemorySearchRunnerTests
        FrontEnd/tests/MemorySearchRunnerTests.cpp
        FrontEnd/src/Debug/MemorySearch.cpp
        FrontEnd/src/Debug/MemorySearchRunner.cpp
        FrontEnd/src/Debug/WatchList.cpp
    )
    target_include_directories(SaturnExplorerMemorySearchRunnerTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    find_package(Threads REQUIRED)
    target_link_libraries(SaturnExplorerMemorySearchRunnerTests PRIVATE Threads::Threads)
    add_test(NAME SaturnExplorerMemorySearchRunnerTests COMMAND SaturnExplorerMemorySearchRunnerTests)

    # The PRS search's bounds (ROM-04): the candidate filter must not drop a real match, and the
    # per-file work budget must report a scan it cut short.
    # The search worker's lifecycle, which had no test while it was nine members of App:
    # one worker at a time, cancel-and-queue, result routing, and a clean shutdown.
    add_executable(SaturnExplorerDataSearchRunnerTests
        FrontEnd/tests/DataSearchRunnerTests.cpp
        FrontEnd/src/DataSearchRunner.cpp
        FrontEnd/src/DataSearch.cpp
        FrontEnd/src/Prs.cpp       # DataSearch's PRS path, which these tests use to be slow
    )
    target_include_directories(SaturnExplorerDataSearchRunnerTests PRIVATE FrontEnd/src)
    find_package(Threads REQUIRED)   # the runner owns a std::thread
    target_link_libraries(SaturnExplorerDataSearchRunnerTests PRIVATE Threads::Threads)
    add_test(NAME SaturnExplorerDataSearchRunnerTests
             COMMAND SaturnExplorerDataSearchRunnerTests)

    add_executable(SaturnExplorerPrsSearchTests
        FrontEnd/tests/PrsSearchTests.cpp
        FrontEnd/src/DataSearch.cpp
        FrontEnd/src/Prs.cpp
    )
    target_include_directories(SaturnExplorerPrsSearchTests PRIVATE FrontEnd/src)
    add_test(NAME SaturnExplorerPrsSearchTests COMMAND SaturnExplorerPrsSearchTests)

    # The data-search directory walk: symlink cycles and the depth cap. Creates real
    # symlinks, so POSIX only -- the Windows walk uses the reparse-point attribute and
    # would need its own harness.
    if(NOT WIN32)
        add_executable(SaturnExplorerDataSearchWalkTests
            FrontEnd/tests/DataSearchWalkTests.cpp
            FrontEnd/src/DataSearch.cpp
            FrontEnd/src/Prs.cpp)
        target_include_directories(SaturnExplorerDataSearchWalkTests
            PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
        add_test(NAME SaturnExplorerDataSearchWalkTests
                 COMMAND SaturnExplorerDataSearchWalkTests)
    endif()

    add_executable(SaturnExplorerExpressionResolverTests
        FrontEnd/tests/ExpressionResolverTests.cpp
        FrontEnd/src/Debug/WatchList.cpp
    )
    target_include_directories(SaturnExplorerExpressionResolverTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerExpressionResolverTests COMMAND SaturnExplorerExpressionResolverTests)

    add_executable(SaturnExplorerAccessLogTests
        FrontEnd/tests/AccessLogTests.cpp
        FrontEnd/src/Debug/AccessLog.cpp
    )
    target_include_directories(SaturnExplorerAccessLogTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerAccessLogTests COMMAND SaturnExplorerAccessLogTests)

    add_executable(SaturnExplorerExecutionActionsTests
        FrontEnd/tests/ExecutionActionsTests.cpp
        FrontEnd/src/Debug/ExecutionActions.cpp
    )
    target_include_directories(SaturnExplorerExecutionActionsTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerExecutionActionsTests COMMAND SaturnExplorerExecutionActionsTests)

    add_executable(SaturnExplorerStepHaltMachineTests
        FrontEnd/tests/StepHaltMachineTests.cpp
        FrontEnd/src/Debug/StepHaltMachine.cpp
    )
    target_include_directories(SaturnExplorerStepHaltMachineTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerStepHaltMachineTests COMMAND SaturnExplorerStepHaltMachineTests)

    add_executable(SaturnExplorerBreakpointManagerTests
        FrontEnd/tests/BreakpointManagerTests.cpp
        FrontEnd/src/Debug/BreakpointManager.cpp
    )
    target_include_directories(SaturnExplorerBreakpointManagerTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerBreakpointManagerTests COMMAND SaturnExplorerBreakpointManagerTests)

    add_executable(SaturnExplorerFunctionNamesTests
        FrontEnd/tests/FunctionNamesTests.cpp
        FrontEnd/src/Debug/CallStack.cpp
    )
    target_include_directories(SaturnExplorerFunctionNamesTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerFunctionNamesTests COMMAND SaturnExplorerFunctionNamesTests)

    # What the heuristic call-stack reconstruction claims to know: a bsr encodes the callee's
    # entry point, a jsr through a register does not, and a return address is not one.
    add_executable(SaturnExplorerCallStackHeuristicTests
        FrontEnd/tests/CallStackHeuristicTests.cpp
        FrontEnd/src/Debug/CallStack.cpp
    )
    target_include_directories(SaturnExplorerCallStackHeuristicTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerCallStackHeuristicTests
             COMMAND SaturnExplorerCallStackHeuristicTests)

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

    # ISO builder — round-trips a built image back through the reader (DiscImage + IsoParse).
    add_executable(SaturnExplorerIsoBuilderTests
        FrontEnd/tests/IsoBuilderTests.cpp
        FrontEnd/src/Disc/IsoBuilder.cpp
        FrontEnd/src/Disc/DiscImage.cpp
        FrontEnd/src/Disc/IsoFs.cpp
    )
    target_include_directories(SaturnExplorerIsoBuilderTests PRIVATE FrontEnd/src)
    add_test(NAME SaturnExplorerIsoBuilderTests COMMAND SaturnExplorerIsoBuilderTests)

    # MODE1/2352 raw-sector encoder (EDC/ECC).
    add_executable(SaturnExplorerCdSectorTests
        FrontEnd/tests/CdSectorTests.cpp
        FrontEnd/src/Disc/CdSector.cpp
    )
    target_include_directories(SaturnExplorerCdSectorTests PRIVATE FrontEnd/src)
    add_test(NAME SaturnExplorerCdSectorTests COMMAND SaturnExplorerCdSectorTests)

    # .cue parser + per-track byte ranges.
    add_executable(SaturnExplorerCueSheetTests
        FrontEnd/tests/CueSheetTests.cpp
        FrontEnd/src/Disc/CueSheet.cpp
    )
    target_include_directories(SaturnExplorerCueSheetTests PRIVATE FrontEnd/src)
    add_test(NAME SaturnExplorerCueSheetTests COMMAND SaturnExplorerCueSheetTests)

    # Build Disc Image — end-to-end BIN/CUE + ISO, read back through DiscImage + IsoParse.
    add_executable(SaturnExplorerDiscBuilderTests
        FrontEnd/tests/DiscBuilderTests.cpp
        FrontEnd/src/Disc/DiscBuilder.cpp
        FrontEnd/src/Disc/IsoBuilder.cpp
        FrontEnd/src/Disc/CdSector.cpp
        FrontEnd/src/Disc/CueSheet.cpp
        FrontEnd/src/Disc/DiscImage.cpp
        FrontEnd/src/Disc/IsoFs.cpp
    )
    target_include_directories(SaturnExplorerDiscBuilderTests PRIVATE FrontEnd/src)
    add_test(NAME SaturnExplorerDiscBuilderTests COMMAND SaturnExplorerDiscBuilderTests)

    # Savestate delta codec (XOR + zero-run RLE) — the v16 rewind stream primitives. Header-
    # only; the test just needs the Integration/Common include path.
    add_executable(SaturnExplorerSeStateCodecTests
        Core/tests/SeStateCodecTests.cpp
    )
    target_include_directories(SaturnExplorerSeStateCodecTests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Integration/Common)
    add_test(NAME SaturnExplorerSeStateCodecTests COMMAND SaturnExplorerSeStateCodecTests)

    # Panel interaction — ImGui hit testing driven headlessly (no backend or window).
    # Guards the row/cell click behaviour the panels depend on; see PanelWidgets.h.
    add_executable(SaturnExplorerPanelInteractionTests
        FrontEnd/tests/PanelInteractionTests.cpp
        FrontEnd/src/Settings.cpp      # the Call Stack split height's persistence
        ${SE_IMGUI_CORE}
    )
    target_include_directories(SaturnExplorerPanelInteractionTests
        PRIVATE FrontEnd/src FrontEnd/tests ${SE_IMGUI_DIR})
    add_test(NAME SaturnExplorerPanelInteractionTests
             COMMAND SaturnExplorerPanelInteractionTests)

    # The Memory panel -- the real panel under the app's real theme and font, against a mock
    # backend: typed digits, edit-box fit, row-height/scroll agreement, and click gating.
    add_executable(SaturnExplorerHexEditorPanelTests
        FrontEnd/tests/HexEditorPanelTests.cpp
        FrontEnd/src/HexEditorPanel.cpp
        FrontEnd/src/Theme.cpp
        FrontEnd/src/Debug/BytePatternSearch.cpp
        FrontEnd/src/Debug/MemorySearch.cpp
        FrontEnd/src/Debug/WatchList.cpp      # WatchTypeSize, used by MemorySearch
        FrontEnd/src/Debug/ShiftJis.cpp
        ${SE_IMGUI_CORE}
    )
    target_include_directories(SaturnExplorerHexEditorPanelTests
        PRIVATE FrontEnd/src FrontEnd/tests ${SE_IMGUI_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerHexEditorPanelTests
             COMMAND SaturnExplorerHexEditorPanelTests)

    # The SH-2 Assembly panel itself -- links the real panel (an unoptimized C++14 build catches an
    # ODR-use of a class-static constant) and pins that its virtual list's row height does not
    # change while a comment is being edited, which would shift every jump.
    add_executable(SaturnExplorerAssemblyPanelTests
        FrontEnd/tests/AssemblyPanelTests.cpp
        FrontEnd/src/AssemblyPanel.cpp
        FrontEnd/src/Sh2Operands.cpp
        FrontEnd/src/WatchPanel.cpp
        FrontEnd/src/Debug/Sh2Disasm.cpp
        FrontEnd/src/Debug/BreakpointManager.cpp
        FrontEnd/src/Debug/ExecutionActions.cpp
        FrontEnd/src/Debug/WatchList.cpp
        FrontEnd/src/Debug/MemoryBackend.cpp
        ${SE_IMGUI_CORE}
    )
    target_include_directories(SaturnExplorerAssemblyPanelTests
        PRIVATE FrontEnd/src FrontEnd/tests Core/tests ${SE_IMGUI_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(SaturnExplorerAssemblyPanelTests PRIVATE SaturnExplorerCore)
    add_test(NAME SaturnExplorerAssemblyPanelTests COMMAND SaturnExplorerAssemblyPanelTests)

    # ContextBackend over a real core context: read-only enforced at write time, driver refusals
    # surfaced as unwritten bytes, and SourceId moving with the data behind an address.
    add_executable(SaturnExplorerMemoryBackendTests
        FrontEnd/tests/MemoryBackendTests.cpp
        FrontEnd/src/Debug/MemoryBackend.cpp
    )
    target_include_directories(SaturnExplorerMemoryBackendTests PRIVATE
        FrontEnd/src Core/tests ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(SaturnExplorerMemoryBackendTests PRIVATE SaturnExplorerCore)
    add_test(NAME SaturnExplorerMemoryBackendTests COMMAND SaturnExplorerMemoryBackendTests)

    # The Assembly panel's decode window, including its lead-in: it must stay inside the region
    # it is in, at both ends, or the single whole-window read is rejected and every line of a
    # perfectly readable window shows "????". Header-only, so nothing to link.
    add_executable(SaturnExplorerAsmCodeWindowTests
        FrontEnd/tests/AsmCodeWindowTests.cpp
    )
    target_include_directories(SaturnExplorerAsmCodeWindowTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerAsmCodeWindowTests
             COMMAND SaturnExplorerAsmCodeWindowTests)

    # The Assembly panel's scroll rule, driven headlessly: a jump to an address that is not the
    # PC (a Call Stack frame, a branch target, Back) has to scroll, and land the target on the
    # first visible line. Keyed off the PC row, those jumps never moved the view at all.
    add_executable(SaturnExplorerAsmScrollTargetTests
        FrontEnd/tests/AsmScrollTargetTests.cpp
        ${SE_IMGUI_CORE}
    )
    target_include_directories(SaturnExplorerAsmScrollTargetTests
        PRIVATE FrontEnd/src FrontEnd/tests ${SE_IMGUI_DIR})
    add_test(NAME SaturnExplorerAsmScrollTargetTests
             COMMAND SaturnExplorerAsmScrollTargetTests)

    # SH-2 operand hover — the Assembly panel's operand cell, driven headlessly. Pins that
    # every operand is its own hover target (see FrontEnd/src/Sh2Operands.h).
    add_executable(SaturnExplorerSh2OperandHoverTests
        FrontEnd/tests/Sh2OperandHoverTests.cpp
        FrontEnd/src/Sh2Operands.cpp
        FrontEnd/src/Debug/Sh2Disasm.cpp
        ${SE_IMGUI_CORE}
    )
    target_include_directories(SaturnExplorerSh2OperandHoverTests
        PRIVATE FrontEnd/src FrontEnd/tests ${SE_IMGUI_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerSh2OperandHoverTests
             COMMAND SaturnExplorerSh2OperandHoverTests)

    # Save-state slots: keyframe/delta reconstruction + the slot file format.
    add_executable(SaturnExplorerSavestateSlotsTests
        FrontEnd/tests/SavestateSlotsTests.cpp
        FrontEnd/src/SavestateSlots.cpp
        FrontEnd/src/Settings.cpp
    )
    target_include_directories(SaturnExplorerSavestateSlotsTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/Integration/Common)
    add_test(NAME SaturnExplorerSavestateSlotsTests COMMAND SaturnExplorerSavestateSlotsTests)

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
        Core/tests                       # FakeVdpSource.h, the shared Seam A mock
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Integration/Common)
    target_link_libraries(SaturnExplorerFrameRecorderTests PRIVATE SaturnExplorerCore)
    find_package(Threads REQUIRED)
    target_link_libraries(SaturnExplorerFrameRecorderTests PRIVATE Threads::Threads)
    add_test(NAME SaturnExplorerFrameRecorderTests COMMAND SaturnExplorerFrameRecorderTests)

    # SCSP frame mixer — the attenuation/pan laws and the voice summing behind Play Frame.
    add_executable(SaturnExplorerScspMixTests
        FrontEnd/tests/ScspMixTests.cpp
        FrontEnd/src/ScspMix.cpp
    )
    target_include_directories(SaturnExplorerScspMixTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerScspMixTests COMMAND SaturnExplorerScspMixTests)

    # SH-2 register metadata + the SR flag decode behind the Registers panel's SH-2 tab.
    add_executable(SaturnExplorerSh2RegInfoTests
        FrontEnd/tests/Sh2RegInfoTests.cpp
    )
    target_include_directories(SaturnExplorerSh2RegInfoTests
        PRIVATE FrontEnd/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME SaturnExplorerSh2RegInfoTests COMMAND SaturnExplorerSh2RegInfoTests)

    # PatchLibrary — known-locations model, project I/O, and Python patch-script emission.
    # The e2e leg runs the emitted script with python3 when available (skips gracefully if not).
    add_executable(SaturnExplorerPatchLibraryTests
        FrontEnd/tests/PatchLibraryTests.cpp
        FrontEnd/src/PatchLibrary.cpp
    )
    target_include_directories(SaturnExplorerPatchLibraryTests PRIVATE FrontEnd/src)
    add_test(NAME SaturnExplorerPatchLibraryTests COMMAND SaturnExplorerPatchLibraryTests)

    # Per-layer viewers: the isolated layer render, the tile map/tileset extraction, and
    # the export artifacts (tileset BMP, tile-index CSV, per-sprite images). Links the core
    # against a synthetic VDP2 snapshot -- no renderer, no window.
    # The write/close/publish layer every save and export ends in. Uses /dev/full for the
    # close-time failure, which is the one fault that needs no mock.
    add_executable(SaturnExplorerFileWriteTests
        FrontEnd/tests/FileWriteTests.cpp
        FrontEnd/src/FileWrite.cpp
    )
    target_include_directories(SaturnExplorerFileWriteTests PRIVATE FrontEnd/src)
    add_test(NAME SaturnExplorerFileWriteTests COMMAND SaturnExplorerFileWriteTests)

    add_executable(SaturnExplorerLayerExportTests
        FrontEnd/tests/LayerExportTests.cpp
        FrontEnd/src/LayerExport.cpp
        FrontEnd/src/FileWrite.cpp
        FrontEnd/src/BinaryWriter.cpp
        FrontEnd/src/Settings.cpp
    )
    target_include_directories(SaturnExplorerLayerExportTests
        PRIVATE FrontEnd/src Core/tests ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(SaturnExplorerLayerExportTests PRIVATE SaturnExplorerCore)
    add_test(NAME SaturnExplorerLayerExportTests COMMAND SaturnExplorerLayerExportTests)

    # Per-layer viewer panels: window registration and the Tile Grid toggle, driven
    # headlessly through ImGui (no backend or window) -- see FrontEnd/tests/ImGuiHarness.h.
    add_executable(SaturnExplorerLayerPanelTests
        FrontEnd/tests/LayerPanelTests.cpp
        FrontEnd/src/LayerPanels.cpp
        FrontEnd/src/LayerExport.cpp
        FrontEnd/src/FileWrite.cpp
        FrontEnd/src/BinaryWriter.cpp
        FrontEnd/src/Settings.cpp
        ${SE_IMGUI_CORE}
    )
    target_include_directories(SaturnExplorerLayerPanelTests
        PRIVATE FrontEnd/src FrontEnd/tests Core/tests ${SE_IMGUI_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(SaturnExplorerLayerPanelTests PRIVATE SaturnExplorerCore)
    add_test(NAME SaturnExplorerLayerPanelTests COMMAND SaturnExplorerLayerPanelTests)

    # Feature-tour demo engine — the .sedemo parser + the beat sequencer (both pure).
    add_executable(SaturnExplorerDemoScriptTests
        FrontEnd/tests/DemoScriptTests.cpp
        FrontEnd/src/Demo/DemoScript.cpp
        FrontEnd/src/Demo/DemoPlayer.cpp
    )
    target_include_directories(SaturnExplorerDemoScriptTests PRIVATE FrontEnd/src)
    add_test(NAME SaturnExplorerDemoScriptTests COMMAND SaturnExplorerDemoScriptTests)

    # Shadow call stack — the emulator-side recording model (the Mednafen glue's flow
    # tracker driving se_export's per-CPU stack), run against a synthetic instruction
    # stream. The glue compiles to a stub without SE_MEDNAFEN_WIRED, so this needs no
    # emulator; the test includes the glue source directly (it has no header) and links
    # the real se_export. Both are C, hence the extra language.
    enable_language(C)
    add_executable(SaturnExplorerShadowCallStackTests
        Integration/tests/ShadowCallStackTests.c
        Integration/Common/se_export.c
    )
    target_include_directories(SaturnExplorerShadowCallStackTests
        PRIVATE Integration/Common Integration/Mednafen)
    target_link_libraries(SaturnExplorerShadowCallStackTests PRIVATE Threads::Threads)
    add_test(NAME SaturnExplorerShadowCallStackTests COMMAND SaturnExplorerShadowCallStackTests)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME SaturnExplorerMednafenHaltOrdering
                 COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/Integration/tests/check_halt_ordering.py
                         ${CMAKE_CURRENT_SOURCE_DIR}/Integration/Mednafen/apply.py)
    endif()
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

    # The driver's byte-budgeted queue. A header-only container needing no socket, so unlike
    # the reconnect test below it builds wherever the driver is configured — MSVC included,
    # where the template's ADL size lookup is the part most worth having a compiler check.
    if(BUILD_TESTING)
        add_executable(SaturnExplorerByteQueueTests
            FrontEnd/tests/ByteQueueTests.cpp)
        target_include_directories(SaturnExplorerByteQueueTests PRIVATE
            Drivers/Live/src)
        add_test(NAME SaturnExplorerByteQueueTests
                 COMMAND SaturnExplorerByteQueueTests)
    endif()

    # The driver's reconnect counter — how a client notices the emulator on the other end
    # was replaced. Stands up a loopback listener, so POSIX sockets only; the Windows
    # driver speaks a named pipe and would need its own harness.
    if(BUILD_TESTING AND NOT WIN32 AND NOT EMSCRIPTEN)
        add_executable(SaturnExplorerLiveReconnectTests
            FrontEnd/tests/LiveReconnectTests.cpp)
        target_include_directories(SaturnExplorerLiveReconnectTests PRIVATE
            Drivers/Live/src
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/Integration/Common)
        target_link_libraries(SaturnExplorerLiveReconnectTests PRIVATE
            SaturnExplorerLiveDriver SaturnExplorerCore Threads::Threads)
        add_test(NAME SaturnExplorerLiveReconnectTests
                 COMMAND SaturnExplorerLiveReconnectTests)

        # The restore-completion signal, end to end: the real exporter against the real driver
        # over the default unix socket, so it shares that socket (and a lock) with the attach test.
        add_executable(SaturnExplorerLiveRestoreSignalTests
            FrontEnd/tests/LiveRestoreSignalTests.cpp
            Integration/Common/se_export.c)
        target_include_directories(SaturnExplorerLiveRestoreSignalTests PRIVATE
            Drivers/Live/src
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/Integration/Common)
        target_link_libraries(SaturnExplorerLiveRestoreSignalTests PRIVATE
            SaturnExplorerLiveDriver SaturnExplorerCore Threads::Threads)
        add_test(NAME SaturnExplorerLiveRestoreSignalTests
                 COMMAND SaturnExplorerLiveRestoreSignalTests)

        # The rewind path as a whole -- exporter, driver and FrameRecorder -- asking whether a
        # recorded frame can be resumed from. Listens on a socket and port of its own, so unlike
        # the tests around it it neither needs the shared lock nor disturbs a running emulator.
        add_executable(SaturnExplorerRewindPipelineTests
            FrontEnd/tests/RewindPipelineTests.cpp
            FrontEnd/src/FrameRecorder.cpp
            FrontEnd/src/FrameLz.cpp
            Integration/Common/se_export.c)
        target_compile_definitions(SaturnExplorerRewindPipelineTests PRIVATE
            SE_LIVE_DEFAULT_SOCK_PATH="/tmp/saturn_explorer_rewind_pipeline_test.sock"
            SE_LIVE_DEFAULT_TCP_PORT=6947)
        target_include_directories(SaturnExplorerRewindPipelineTests PRIVATE
            FrontEnd/src
            Drivers/Live/src
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/Integration/Common)
        target_link_libraries(SaturnExplorerRewindPipelineTests PRIVATE
            SaturnExplorerLiveDriver SaturnExplorerCore Threads::Threads)
        add_test(NAME SaturnExplorerRewindPipelineTests COMMAND SaturnExplorerRewindPipelineTests)

        # Play From Here against a REAL emulator: restores a recorded frame and checks the game
        # re-runs the following frames byte for byte as it did the first time. Needs a patched
        # Mednafen already running a game (see Docs/PlayFromHereVerification.md), so it is a tool
        # to run by hand and deliberately not a test.
        add_executable(se-rewind-live-check
            FrontEnd/tools/RewindLiveCheck.cpp
            FrontEnd/src/FrameRecorder.cpp
            FrontEnd/src/FrameLz.cpp)
        target_include_directories(se-rewind-live-check PRIVATE
            FrontEnd/src
            Drivers/Live/src
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/Integration/Common)
        target_link_libraries(se-rewind-live-check PRIVATE
            SaturnExplorerLiveDriver SaturnExplorerCore Threads::Threads)

        # The exporter against hostile/slow peers, and frame-step accounting, over the same
        # default socket.
        add_executable(SaturnExplorerLiveExporterRobustnessTests
            FrontEnd/tests/LiveExporterRobustnessTests.cpp
            Integration/Common/se_export.c)
        target_include_directories(SaturnExplorerLiveExporterRobustnessTests PRIVATE
            Drivers/Live/src
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/Integration/Common)
        target_link_libraries(SaturnExplorerLiveExporterRobustnessTests PRIVATE
            SaturnExplorerLiveDriver SaturnExplorerCore Threads::Threads)
        add_test(NAME SaturnExplorerLiveExporterRobustnessTests
                 COMMAND SaturnExplorerLiveExporterRobustnessTests)
        # The halt handshake (lost stops, stop sequence, instruction-step retirement). Built with the
        # halt gate spinning so the emulate thread is at the gate the instant the server releases it:
        # the race under test is a few hundred nanoseconds wide, and the production 2 ms sleep
        # makes it effectively unreachable.
        add_executable(SaturnExplorerLiveHaltRaceTests
            FrontEnd/tests/LiveHaltRaceTests.cpp
            Integration/Common/se_export.c)
        target_compile_definitions(SaturnExplorerLiveHaltRaceTests PRIVATE SE_EXPORT_SPIN_GATE=1)
        target_include_directories(SaturnExplorerLiveHaltRaceTests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/Integration/Common)
        target_link_libraries(SaturnExplorerLiveHaltRaceTests PRIVATE Threads::Threads)
        add_test(NAME SaturnExplorerLiveHaltRaceTests COMMAND SaturnExplorerLiveHaltRaceTests)
        set_tests_properties(SaturnExplorerLiveRestoreSignalTests SaturnExplorerExportAttachTests
                             SaturnExplorerLiveExporterRobustnessTests SaturnExplorerLiveHaltRaceTests
                             PROPERTIES RESOURCE_LOCK live_tap_socket)
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
        FrontEnd/src/DataSearchRunner.cpp
        FrontEnd/src/Prs.cpp
        FrontEnd/src/Settings.cpp
        FrontEnd/src/Launcher.cpp
        FrontEnd/src/TopBar.cpp
        FrontEnd/src/NativeMenu.cpp
        FrontEnd/src/WatchPanel.cpp
        FrontEnd/src/AssemblyPanel.cpp
        FrontEnd/src/Sh2Operands.cpp
        FrontEnd/src/HexEditorPanel.cpp
        FrontEnd/src/ControllerPanel.cpp
        FrontEnd/src/LogPanel.cpp
        FrontEnd/src/LayerPanels.cpp
        FrontEnd/src/LayerExport.cpp
        FrontEnd/src/FileWrite.cpp
        FrontEnd/src/BinaryWriter.cpp
        FrontEnd/src/Debug/WatchList.cpp
        FrontEnd/src/Debug/MemoryBackend.cpp
        FrontEnd/src/Debug/Sh2Disasm.cpp
        FrontEnd/src/Debug/BreakpointManager.cpp
        FrontEnd/src/Debug/StepHaltMachine.cpp
        FrontEnd/src/Debug/ShiftJis.cpp
        FrontEnd/src/Debug/FormatString.cpp
        FrontEnd/src/Debug/ConditionEval.cpp
        FrontEnd/src/Debug/MemorySearch.cpp
        FrontEnd/src/Debug/MemorySearchRunner.cpp
        FrontEnd/src/Debug/BytePatternSearch.cpp
        FrontEnd/src/Debug/AccessLog.cpp
        FrontEnd/src/Debug/M68kDisasm.cpp
        FrontEnd/src/Disc/IsoFs.cpp
        FrontEnd/src/Disc/DiscImage.cpp
        FrontEnd/src/Disc/IsoBuilder.cpp
        FrontEnd/src/Disc/CdSector.cpp
        FrontEnd/src/Disc/CueSheet.cpp
        FrontEnd/src/Disc/DiscBuilder.cpp
        FrontEnd/src/Debug/ExecutionActions.cpp
        FrontEnd/src/Debug/CallStack.cpp
        FrontEnd/src/FrameRecorder.cpp
        FrontEnd/src/FrameLz.cpp
        FrontEnd/src/SavestateSlots.cpp
        FrontEnd/src/ScspMix.cpp
        FrontEnd/src/PatchLibrary.cpp
        FrontEnd/src/Demo/DemoScript.cpp
        FrontEnd/src/Demo/DemoPlayer.cpp
        FrontEnd/src/UpdateChecker.cpp
        FrontEnd/Platforms/Windows/WindowsPlatform.cpp
        FrontEnd/Platforms/Windows/Win32MenuBar.cpp
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
        SE_NATIVE_MENUBAR   # use the native Win32 HMENU menu bar instead of the ImGui toolbar
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
        FrontEnd/src/DataSearchRunner.cpp
        FrontEnd/src/Prs.cpp
        FrontEnd/src/Settings.cpp
        FrontEnd/src/Launcher.cpp
        FrontEnd/src/TopBar.cpp
        FrontEnd/src/NativeMenu.cpp
        FrontEnd/src/WatchPanel.cpp
        FrontEnd/src/AssemblyPanel.cpp
        FrontEnd/src/Sh2Operands.cpp
        FrontEnd/src/HexEditorPanel.cpp
        FrontEnd/src/ControllerPanel.cpp
        FrontEnd/src/LogPanel.cpp
        FrontEnd/src/LayerPanels.cpp
        FrontEnd/src/LayerExport.cpp
        FrontEnd/src/FileWrite.cpp
        FrontEnd/src/BinaryWriter.cpp
        FrontEnd/src/Debug/WatchList.cpp
        FrontEnd/src/Debug/MemoryBackend.cpp
        FrontEnd/src/Debug/Sh2Disasm.cpp
        FrontEnd/src/Debug/BreakpointManager.cpp
        FrontEnd/src/Debug/StepHaltMachine.cpp
        FrontEnd/src/Debug/ShiftJis.cpp
        FrontEnd/src/Debug/FormatString.cpp
        FrontEnd/src/Debug/ConditionEval.cpp
        FrontEnd/src/Debug/MemorySearch.cpp
        FrontEnd/src/Debug/MemorySearchRunner.cpp
        FrontEnd/src/Debug/BytePatternSearch.cpp
        FrontEnd/src/Debug/AccessLog.cpp
        FrontEnd/src/Debug/M68kDisasm.cpp
        FrontEnd/src/Disc/IsoFs.cpp
        FrontEnd/src/Disc/DiscImage.cpp
        FrontEnd/src/Disc/IsoBuilder.cpp
        FrontEnd/src/Disc/CdSector.cpp
        FrontEnd/src/Disc/CueSheet.cpp
        FrontEnd/src/Disc/DiscBuilder.cpp
        FrontEnd/src/Debug/ExecutionActions.cpp
        FrontEnd/src/Debug/CallStack.cpp
        FrontEnd/src/Demo/DemoScript.cpp
        FrontEnd/src/Demo/DemoPlayer.cpp
        FrontEnd/src/UpdateChecker.cpp
        FrontEnd/Platforms/Web/WebPlatform.cpp
        FrontEnd/Platforms/Web/WebMain.cpp
        ${SE_IMGUI_CORE}
        ${SE_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${SE_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    if(SE_WEB_LIVE)
        list(APPEND SE_WEB_SRC FrontEnd/src/FrameRecorder.cpp FrontEnd/src/FrameLz.cpp
                               FrontEnd/src/SavestateSlots.cpp
            FrontEnd/src/ScspMix.cpp
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
        FrontEnd/src/DataSearchRunner.cpp
            FrontEnd/src/Prs.cpp
            FrontEnd/src/Settings.cpp
            FrontEnd/src/Launcher.cpp
            FrontEnd/src/TopBar.cpp
            FrontEnd/src/NativeMenu.cpp
            FrontEnd/src/WatchPanel.cpp
            FrontEnd/src/AssemblyPanel.cpp
            FrontEnd/src/Sh2Operands.cpp
            FrontEnd/src/HexEditorPanel.cpp
            FrontEnd/src/ControllerPanel.cpp
            FrontEnd/src/LogPanel.cpp
            FrontEnd/src/LayerPanels.cpp
            FrontEnd/src/LayerExport.cpp
            FrontEnd/src/FileWrite.cpp
            FrontEnd/src/BinaryWriter.cpp
            FrontEnd/src/Debug/WatchList.cpp
            FrontEnd/src/Debug/MemoryBackend.cpp
            FrontEnd/src/Debug/Sh2Disasm.cpp
            FrontEnd/src/Debug/BreakpointManager.cpp
            FrontEnd/src/Debug/StepHaltMachine.cpp
            FrontEnd/src/Debug/ShiftJis.cpp
            FrontEnd/src/Debug/FormatString.cpp
            FrontEnd/src/Debug/ConditionEval.cpp
            FrontEnd/src/Debug/MemorySearch.cpp
            FrontEnd/src/Debug/MemorySearchRunner.cpp
            FrontEnd/src/Debug/BytePatternSearch.cpp
            FrontEnd/src/Debug/AccessLog.cpp
            FrontEnd/src/Debug/M68kDisasm.cpp
            FrontEnd/src/Disc/IsoFs.cpp
            FrontEnd/src/Disc/DiscImage.cpp
            FrontEnd/src/Disc/IsoBuilder.cpp
            FrontEnd/src/Disc/CdSector.cpp
            FrontEnd/src/Disc/CueSheet.cpp
            FrontEnd/src/Disc/DiscBuilder.cpp
            FrontEnd/src/Debug/ExecutionActions.cpp
            FrontEnd/src/Debug/CallStack.cpp
            FrontEnd/src/FrameRecorder.cpp
            FrontEnd/src/FrameLz.cpp
            FrontEnd/src/SavestateSlots.cpp
            FrontEnd/src/ScspMix.cpp
            FrontEnd/src/PatchLibrary.cpp
            FrontEnd/src/Demo/DemoScript.cpp
            FrontEnd/src/Demo/DemoPlayer.cpp
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
        # Core goes after the drivers: they depend on it and a static library must follow its
        # dependents. Listed first, CMake emits it a second time after them anyway, and Apple's
        # linker warns about the duplicate.
        target_link_libraries(SaturnExplorerFrontEnd PRIVATE
            SaturnExplorerSavestateDriver
            SaturnExplorerLiveDriver
            SaturnExplorerCore
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
            # Native macOS menu bar (parity with the Windows SE_NATIVE_MENUBAR build): the Cocoa
            # NSMenu renderer replaces the ImGui top toolbar on this desktop build. Linux desktop
            # keeps the ImGui toolbar (SDL2 there has no OS menu-bar API and SE_NATIVE_MENUBAR is
            # only defined here). MacMenuBar.mm is Objective-C++ (ARC) and the only Cocoa TU; the
            # rest of the frontend stays plain C++ behind the MacMenuBar facade.
            target_sources(SaturnExplorerFrontEnd PRIVATE
                FrontEnd/Platforms/macOS/MacMenuBar.mm)
            set_source_files_properties(FrontEnd/Platforms/macOS/MacMenuBar.mm PROPERTIES
                COMPILE_OPTIONS "-fobjc-arc")
            target_include_directories(SaturnExplorerFrontEnd PRIVATE FrontEnd/Platforms/macOS)
            target_compile_definitions(SaturnExplorerFrontEnd PRIVATE SE_NATIVE_MENUBAR)
            target_link_libraries(SaturnExplorerFrontEnd PRIVATE "-framework AppKit" "-framework Foundation")

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
