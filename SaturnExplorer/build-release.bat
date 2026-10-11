@echo off
REM ===========================================================================
REM  Saturn Explorer - Release build (Windows)
REM
REM  Configures with CMake and builds the Release app (Win32 + Direct3D 11,
REM  with live-mode support). Requires:
REM    - CMake             https://cmake.org/download/  (on PATH)
REM    - Visual Studio (2019 or newer) with "Desktop development with C++"
REM
REM  Usage:  build-release.bat            (newest installed Visual Studio, x64)
REM          build-release.bat 2022       (force VS 2022)
REM          build-release.bat 2019       (force VS 2019)
REM
REM  Output: build\bin\Release\SaturnExplorerFrontEnd.exe
REM ===========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

REM With no argument CMake picks the newest Visual Studio it finds, so the script keeps
REM working as new releases replace old ones (a machine with only VS 2026 has no VS 2022).
set "GENERATOR="
if "%~1"=="2022" set "GENERATOR=Visual Studio 17 2022"
if "%~1"=="2019" set "GENERATOR=Visual Studio 16 2019"

echo.
if defined GENERATOR (
    echo === Configuring ^(%GENERATOR%, x64^) ===
    cmake -B build -G "%GENERATOR%" -A x64
) else (
    echo === Configuring ^(newest Visual Studio, x64^) ===
    cmake -B build -A x64
)
if errorlevel 1 goto :error

echo.
echo === Building (Release) ===
cmake --build build --config Release --parallel
if errorlevel 1 goto :error

echo.
echo === Build succeeded ===
echo App:  "%cd%\build\bin\Release\SaturnExplorerFrontEnd.exe"
echo Libs: "%cd%\build\lib\Release"
echo.
echo Run it, or pass a savestate:  SaturnExplorerFrontEnd.exe state.yss
echo Live mode (running Yabause):  SaturnExplorerFrontEnd.exe --live
endlocal
exit /b 0

:error
echo.
echo *** Build FAILED. See the messages above. ***
echo   - Is CMake on your PATH?        cmake --version
echo   - Installed the VS C++ workload?
echo   - Force a version:              build-release.bat 2022  (or 2019)
endlocal
exit /b 1
