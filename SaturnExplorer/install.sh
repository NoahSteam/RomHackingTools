#!/usr/bin/env bash
# ===========================================================================
#  Saturn Explorer - one-command macOS/Linux installer (bootstrap).
#
#  The Unix counterpart of install.bat. Finds Python 3, then runs
#  Integration/install.py, which builds Saturn Explorer and downloads +
#  patches + builds a Saturn emulator (Mednafen by default).
#
#  On macOS the script needs (and, with your consent, installs via Homebrew):
#  CMake, and for Mednafen the autotools + SDL2 + FLAC packages. Xcode's
#  command-line tools (clang, git) must already be present:
#      xcode-select --install
#
#  Examples:
#    ./install.sh                          SE + Mednafen
#    ./install.sh --mednafen-saturn-only   SE + Mednafen (Saturn core only; faster build)
#    ./install.sh --dry-run                show the plan, change nothing
#    ./install.sh --yes                    don't prompt before installs/builds
#
#  See Integration/INSTALL.md for details. (Yabause is Windows-only for now.)
# ===========================================================================
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if command -v python3 >/dev/null 2>&1; then
    exec python3 "$here/Integration/install.py" "$@"
fi

echo "Python 3 was not found on your PATH."
echo "Install it, then re-run this script:"
echo "  macOS : brew install python3   (or from https://www.python.org/downloads/)"
echo "  Linux : sudo apt install python3   (or your distro's package)"
exit 9
