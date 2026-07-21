===============================================================================
                              SATURN EXPLORER
        A visual reverse-engineering and debugging tool for Sega Saturn
===============================================================================

Saturn Explorer opens up a Sega Saturn game's graphics hardware and CPU state
as an interactive interface. Point it at a savestate, a memory dump, or a
*running* emulator, and it reconstructs what the console is drawing -- VDP1
sprites, VDP2 backgrounds, VRAM, palettes, the command table -- and lets you
step through the SH-2 code behind it.

It is built for translators, romhackers, and anyone taking a Saturn game apart:
find where a texture or string lives, watch memory change frame by frame, set a
tracepoint on an instruction and log a value every time it runs.


-------------------------------------------------------------------------------
  WHAT IT OFFERS
-------------------------------------------------------------------------------

Graphics inspection
  * VDP Output      -- the composited frame, software-rendered from the sprite
                       quads and VDP2 background layers; click a pixel to select
                       the command that drew it.
  * VDP1 Framebuffer & 3D View -- the raw drawn output, and an exploded 3D view
                       that orbits the scene geometry.
  * VDP1 Command List & Selected Object -- the parsed command table, with every
                       field of the command under the cursor.
  * Texture & Palette Viewers -- decode any sprite's texture and CLUT.
  * VRAM Map        -- a color-coded map of VDP1 VRAM (textures / CLUTs / command
                       table / gouraud tables).
  * Color RAM, VDP1/VDP2 register tables, and layer controls to toggle
    individual VDP2 backgrounds, windows, shadows, and color calculation.

Live debugging (connect to a running emulator)
  * SH-2 Assembly   -- live disassembly of the master and slave SH-2, following
                       the program counter, with your own labels and comments.
  * Breakpoints     -- gutter breakpoints, run-to-cursor, single-step, pause /
                       frame-step the emulator.
  * Watch           -- track memory locations and expressions; break on change.
  * Hex Editor      -- view and edit work RAM in the running game.
  * Controller      -- an on-screen Saturn pad that drives the emulated game.
  * Tracepoints     -- non-halting logging: attach an instruction to a format
                       string like "Dialogue ID = {r4}" or "Name = {*r5:string}"
                       and every time that instruction runs, the value is
                       captured and written to the structured Log panel -- no
                       breakpoint, the game keeps running. A management table
                       lists every tracepoint with its address, output, and hit
                       count; the editor has autocomplete for register and
                       memory tokens.
  * Log             -- a filterable, searchable event log; click an entry to jump
                       to the instruction or memory address that produced it.

Asset tracing
  * Search game data for the bytes of a texture or palette to find where it
    lives on the disc / in the extracted files.


-------------------------------------------------------------------------------
  PLATFORMS SUPPORTED
-------------------------------------------------------------------------------

Saturn Explorer runs three ways from one codebase:

  * Windows (native)   -- Win32 + Direct3D 11. Full feature set, including live
                          mode. This is the primary target.
  * Desktop (SDL2)     -- SDL2 + OpenGL on Linux / macOS. Same features as the
                          Windows build, including live mode.
  * Web (browser)      -- SDL2 + WebGL2 via Emscripten. Savestate / dump analysis
                          only (live mode needs sockets + threads a browser can't
                          provide). Try it in a browser with no install.

Input it understands:
  * Yabause / Yaba Sanshiro `.yss` savestates
  * Mednafen savestates and raw memory dumps
  * A live, running, patched Mednafen or Yabause (see LIVE MODE below)


-------------------------------------------------------------------------------
  INSTALLING  (Windows -- the easy path)
-------------------------------------------------------------------------------

The one-command installer builds Saturn Explorer AND downloads, patches, and
builds a Saturn emulator for live mode. From the SaturnExplorer folder:

    install.bat                     Build Saturn Explorer + Mednafen (default)
    install.bat --with-yabause      Also build Yabause (Qt)
    install.bat --dry-run           Show the plan without changing anything
    install.bat --yes               Don't prompt before each install / build step

It needs Python 3 on your PATH (get it with `winget install --id Python.Python.3
-e`, or from python.org with "Add python.exe to PATH" ticked). Everything else it
needs -- git, CMake, the MSVC C++ toolchain, MSYS2 -- is detected and, with your
consent, installed automatically via winget.

When it finishes you'll have Saturn Explorer built and a patched emulator ready
to launch. The emulator paths are recorded so the "Launch Mednafen" toolbar
button starts it for you.

See Integration/INSTALL.md for the full installer reference and troubleshooting.


-------------------------------------------------------------------------------
  BUILDING FROM SOURCE  (any platform)
-------------------------------------------------------------------------------

Saturn Explorer builds with CMake. From the SaturnExplorer folder:

  Windows -- generate + build a Visual Studio solution:
    cmake -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Debug
    (creates build\SaturnExplorer.sln -- open it and press F5 if you prefer)

  Linux / macOS -- build directly:
    cmake -B build
    cmake --build build
    (needs SDL2 + OpenGL dev packages for the app, e.g.
     apt install libsdl2-dev libgl1-mesa-dev)
    Run it on a savestate:  ./build/bin/SaturnExplorerFrontEnd state.yss

  Web -- Emscripten + WebGL2:
    emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
    cmake --build build-web
    Serve build-web/bin over HTTP and open index.html.

You can also open the checked-in Visual Studio solution at the repository root
(RomHackingTools.sln), set FrontEnd as the startup project, and press F5.

Full details, prerequisites, and the component split are in BUILD.md.


-------------------------------------------------------------------------------
  LIVE MODE  (inspect a running emulator)
-------------------------------------------------------------------------------

The native builds (Windows / SDL2) can read a *running* emulator in realtime
instead of a static savestate. A small, self-contained tap is patched into the
emulator's source (the installer does this for you); it serves the console's
memory + CPU state to Saturn Explorer over a local socket.

  1. Run the installer (or apply the patch in Integration/Mednafen/ or
     Integration/Yabause/ to your own emulator build).
  2. Start a game in the patched emulator.
  3. Launch Saturn Explorer with  --live , or use the "Launch Mednafen" button /
     Open > Connect menu.

Every panel then tracks the live game -- the disassembly follows the PC,
breakpoints and tracepoints fire, the hex editor writes to live work RAM, and
the controller panel drives the game.


-------------------------------------------------------------------------------
  LAYOUT OF THIS FOLDER
-------------------------------------------------------------------------------

  Core/          The analysis engine (portable C++ static library).
  Drivers/       Data sources: savestate/dump reader, and the live socket driver.
  FrontEnd/      The app window (Dear ImGui) and its three platform backends.
  Integration/   The emulator taps + the Windows installer (install.py).
  include/        Public headers for the two ABI seams.
  BUILD.md       How to build, in detail.
  ARCHITECTURE.md The component split and the three interface seams.
  EXECUTION_ACTIONS.md, SCRIPTING.md  Design notes for tracepoints & scripting.

===============================================================================
