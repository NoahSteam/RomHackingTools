@echo off
REM ===========================================================================
REM  Saturn Explorer - Release build via MSBuild (no CMake)
REM
REM  Builds the FrontEnd app (and its Core + Savestate-driver dependencies)
REM  straight from the checked-in solution RomHackingTools.sln, using the
REM  Visual Studio toolchain already on the machine. No CMake required.
REM
REM  NOTE: the checked-in .sln is SAVESTATE-ONLY. Live mode (connect to a
REM  running Yabause, Work RAM streaming, pause / frame-step) is wired into the
REM  CMake build, not this VS solution, so a build from here won't include it.
REM  For the full-featured build (live mode), use build-release.bat (CMake).
REM
REM  Requires: Visual Studio 2022 or 2019 with "Desktop development with C++".
REM  (MSBuild is located automatically via vswhere.)
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

echo.
echo === MSBuild: "%MSBUILD%"
echo === Building FrontEnd  (Release ^| %PLATFORM%) ===
REM /t:FrontEnd builds the app target and its project dependencies only.
"%MSBUILD%" "%SLN%" /t:FrontEnd /p:Configuration=Release /p:Platform=%PLATFORM% /m /nologo /v:minimal
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
echo Live mode (running Yabause):  SaturnExplorer.exe --live
endlocal
exit /b 0

:error
echo.
echo *** Build FAILED. See the messages above. ***
endlocal
exit /b 1
