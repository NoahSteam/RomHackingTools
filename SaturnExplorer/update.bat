@echo off
REM ===========================================================================
REM  Saturn Explorer - iterative re-install (Windows).
REM
REM  Same as install.bat, but for RE-builds after you edit the Integration\
REM  folder: it keeps the existing emulator checkout, re-applies the idempotent
REM  content-aware patch, and rebuilds ONLY what changed (a single edited
REM  se_export.c / glue / SeLiveProtocol.h recompiles just that object; an
REM  unchanged tree builds nothing). It skips the prerequisite package installs
REM  and skips ./configure when the tree is already configured.
REM
REM  It's exactly `install.py --incremental`, so every install.bat flag still
REM  works and just passes through:
REM    update.bat                          rebuild only what changed
REM    update.bat --mednafen-saturn-only   ...and (re)configure Saturn-only
REM    update.bat --with-yabause           also refresh the Yabause build
REM    update.bat --dry-run                show the plan, change nothing
REM ===========================================================================
setlocal
cd /d "%~dp0"

REM Prefer the py launcher, fall back to python on PATH.
where py >nul 2>nul
if %errorlevel%==0 (
    py -3 "Integration\install.py" --incremental %*
    goto :done
)
where python >nul 2>nul
if %errorlevel%==0 (
    python "Integration\install.py" --incremental %*
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
