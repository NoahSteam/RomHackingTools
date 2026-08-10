#!/usr/bin/env bash
# ===========================================================================
#  Saturn Explorer - iterative re-install (macOS/Linux).
#
#  The Unix counterpart of update.bat. Same as install.sh, but for RE-builds
#  after you edit the Integration/ folder: it keeps the existing emulator
#  checkout, re-applies the idempotent content-aware patch, and rebuilds ONLY
#  what changed (a single edited se_export.c / glue / SeLiveProtocol.h
#  recompiles just that object; an unchanged tree builds nothing). It skips the
#  prerequisite package installs and skips ./configure when the tree is already
#  configured.
#
#  It's exactly `install.py --incremental`, so every install.sh flag still
#  works and just passes through:
#    ./update.sh                          rebuild only what changed
#    ./update.sh --mednafen-saturn-only   ...and (re)configure Saturn-only
#    ./update.sh --dry-run                show the plan, change nothing
#    ./update.sh --verbose                include complete underlying commands
# ===========================================================================
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if command -v python3 >/dev/null 2>&1; then
    exec python3 "$here/Integration/install.py" --incremental "$@"
fi

echo "Python 3 was not found on your PATH."
echo "Install it, then re-run this script:"
echo "  macOS : brew install python3   (or from https://www.python.org/downloads/)"
echo "  Linux : sudo apt install python3   (or your distro's package)"
exit 9
