@echo off
REM ===========================================================================
REM  Saturn Explorer - one-command Windows installer (bootstrap).
REM
REM  Finds Python, then runs Integration\install.py, which builds Saturn
REM  Explorer and downloads + patches + builds a Saturn emulator (Mednafen by
REM  default; add --with-yabause for a Yabause/Yaba Sanshiro/Kronos build).
REM
REM  Examples:
REM    install.bat                          SE + Mednafen
REM    install.bat --mednafen-saturn-only   SE + Mednafen (Saturn core only; faster build)
REM    install.bat --with-yabause           also build Yabause (Qt)
REM    install.bat --dry-run                show the plan, change nothing
REM    install.bat --yes                    don't prompt before installs/builds
REM
REM  Everything the Python script needs (git, CMake, MSVC C++, MSYS2) is
REM  detected and, with your consent, installed via winget. See Integration\INSTALL.md.
REM ===========================================================================
setlocal
cd /d "%~dp0"

REM Prefer the py launcher, fall back to python on PATH.
where py >nul 2>nul
if %errorlevel%==0 (
    py -3 "Integration\install.py" %*
    goto :done
)
where python >nul 2>nul
if %errorlevel%==0 (
    python "Integration\install.py" %*
    goto :done
)

echo Python 3 was not found on your PATH.
echo Install it, then re-run:  winget install --id Python.Python.3 -e
echo (or from https://www.python.org/downloads/ - tick "Add python.exe to PATH")
endlocal
exit /b 9

:done
endlocal
exit /b %errorlevel%
