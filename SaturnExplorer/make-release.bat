@echo off
REM ===========================================================================
REM  Saturn Explorer - build and zip a Windows release, in one step.
REM
REM  1. Builds Saturn Explorer (Release) and the patched Mednafen (Saturn core only)
REM     with Integration\install.py. Missing build tools are offered via winget/pacman;
REM     it also upgrades MSYS2 and records this checkout's Mednafen path in
REM     %APPDATA%\SaturnExplorer\settings.ini.
REM  2. Packages them with Integration\package_release.py: SaturnExplorerFrontEnd.exe,
REM     the Visual C++ runtime, mednafen-se\, licenses\, SOURCE.txt, the patched-source
REM     tarball and the copyleft library sources, then zips the folder.
REM
REM  Usage:  make-release.bat             label = the short commit hash
REM          make-release.bat v1.0        label = v1.0 (letters, digits, . _ -)
REM
REM  Output: dist\SaturnExplorer-<label>-windows.zip
REM
REM  The packager refuses to run when Integration\ has uncommitted changes or the
REM  Mednafen checkout is not at the commit pinned in Integration\Mednafen\UPSTREAM.json:
REM  the source offer in the zip is rebuilt from the commit, so commit first.
REM  A release for others should come from the GitHub workflow instead (see
REM  Integration\DISTRIBUTION.md, "Cutting a release"); this is for local builds.
REM ===========================================================================
setlocal
cd /d "%~dp0"

REM Prefer the py launcher, fall back to python on PATH.
set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY (
    where python >nul 2>nul && set "PY=python"
)
if not defined PY (
    echo Python 3 was not found on your PATH.
    echo Install it, then re-run:  winget install --id Python.Python.3 -e
    endlocal
    exit /b 9
)

where git >nul 2>nul
if errorlevel 1 (
    echo git was not found on your PATH.
    endlocal
    exit /b 9
)

set "LABEL=%~1"
if not defined LABEL (
    for /f %%i in ('git rev-parse --short^=10 HEAD') do set "LABEL=%%i"
)
if not defined LABEL (
    echo Could not work out a release label; pass one:  make-release.bat v1.0
    endlocal
    exit /b 9
)

echo.
echo === 1/2  Building Saturn Explorer and the patched Mednafen ===
%PY% "Integration\install.py" --yes --mednafen-saturn-only
if errorlevel 1 goto :fail

echo.
echo === 2/2  Packaging release %LABEL% ===
%PY% "Integration\package_release.py" --se-app "build\bin\Release" --mednafen "_emu\mednafen" --ref "%LABEL%" --out "dist"
if errorlevel 1 goto :fail

echo.
echo Done: %~dp0dist\SaturnExplorer-%LABEL%-windows.zip
endlocal
exit /b 0

:fail
set "RC=%errorlevel%"
echo.
echo Release build FAILED (exit code %RC%); see the messages above.
endlocal & exit /b %RC%
