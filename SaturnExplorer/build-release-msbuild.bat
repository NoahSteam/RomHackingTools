@echo off
REM ===========================================================================
REM  Saturn Explorer - Release build via MSBuild (no CMake)
REM
REM  Builds the FrontEnd app (and its Core + DriverCommon + Savestate + Live
REM  driver dependencies) straight from the checked-in solution RomHackingTools.sln,
REM  using the Visual Studio toolchain already on the machine. No CMake required.
REM
REM  Feature-parity with the CMake build (build-release.bat): live mode (connect to
REM  a running emulator, Work RAM streaming, pause / frame-step) is compiled in here
REM  too -- FrontEnd.vcxproj defines SE_ENABLE_LIVE and links the Live driver. The
REM  two scripts differ only in build system (raw MSBuild vs CMake) and output path.
REM
REM  Requires: Visual Studio 2022 or 2019 with "Desktop development with C++".
REM  (MSBuild is located automatically via vswhere; the toolset follows it --
REM  v142 on 2019, v143 on 2022 -- or set SE_TOOLSET to choose.)
REM
REM  Usage:  build-release-msbuild.bat            (Release, x64)
REM          build-release-msbuild.bat x86        (Release, Win32/x86)
REM
REM  Output: <repo>\x64\Release\SaturnExplorer.exe   (or Win32\Release for x86)
REM ===========================================================================
setlocal enabledelayedexpansion

REM Repo root is one level up from this script (SaturnExplorer\..).
set "REPO=%~dp0.."
set "SLN=%REPO%\RomHackingTools.sln"

REM Platform: x64 by default; "x86" -> Win32 (the solution's 32-bit platform name).
set "PLATFORM=x64"
if /I "%~1"=="x86"   set "PLATFORM=Win32"
if /I "%~1"=="Win32" set "PLATFORM=Win32"

REM --- Locate MSBuild via vswhere (ships with VS 2017+). ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo *** Could not find vswhere.exe - is Visual Studio 2017+ installed?
    echo     Alternatively, run this from a "Developer Command Prompt for VS"
    echo     where MSBuild is already on PATH, then use: build-release.bat
    goto :error
)

set "MSBUILD="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
    echo *** vswhere did not find MSBuild. Install the "Desktop development
    echo     with C++" workload in the Visual Studio Installer.
    goto :error
)

REM --- Pick the toolset for the Visual Studio that owns this MSBuild. ---
REM The projects name v142 (VS 2019). VS 2022 installs v143 and only has v142 if the optional
REM "MSVC v142 build tools" component was added, so building as checked in fails on a stock
REM VS 2022. Overriding the toolset on the command line leaves the projects as they are, so
REM VS 2019 users and the IDE see no change. Set SE_TOOLSET to force one (e.g. v142 on VS 2022).
set "TOOLSET=%SE_TOOLSET%"
if not defined TOOLSET (
    set "VSMAJOR="
    for /f "usebackq tokens=1 delims=." %%v in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -property installationVersion`) do set "VSMAJOR=%%v"
    if "!VSMAJOR!"=="16" set "TOOLSET=v142"
    if "!VSMAJOR!"=="17" set "TOOLSET=v143"
    if not defined TOOLSET (
        echo *** Visual Studio !VSMAJOR! is not 2019 or 2022; using v143.
        echo     Set SE_TOOLSET to override.
        set "TOOLSET=v143"
    )
)

echo.
echo === MSBuild: "%MSBUILD%"
echo === Toolset: %TOOLSET%
echo === Building FrontEnd  (Release ^| %PLATFORM%) ===
REM /t:FrontEnd builds the app target and its project dependencies only.
"%MSBUILD%" "%SLN%" /t:FrontEnd /p:Configuration=Release /p:Platform=%PLATFORM% /p:PlatformToolset=%TOOLSET% /m /nologo /v:minimal
if errorlevel 1 goto :error

echo.
echo === Build succeeded ===
set "EXE=%REPO%\%PLATFORM%\Release\SaturnExplorer.exe"
if exist "%EXE%" (
    echo App: "%EXE%"
) else (
    echo App built. Locating SaturnExplorer.exe under the repo...
    for /r "%REPO%" %%f in (SaturnExplorer.exe) do echo   %%f
)
echo.
echo Run it, or pass a savestate:  SaturnExplorer.exe state.yss
echo Live mode (running emulator):  SaturnExplorer.exe --live
endlocal
exit /b 0

:error
echo.
echo *** Build FAILED. See the messages above. ***
endlocal
exit /b 1
