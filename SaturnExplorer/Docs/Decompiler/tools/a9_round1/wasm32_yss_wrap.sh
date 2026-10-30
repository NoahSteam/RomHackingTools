#!/usr/bin/env bash
# Builds yss_wrap32.cpp with the savestate driver as it is in the working tree (or at a given revision) for wasm32 and runs
# it under node. Needs an emsdk (EMSDK_DIR, default $PWD/emsdk) and node.
#   wasm32_yss_wrap.sh [git-revision]     (no argument: the working tree)
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../../.." && pwd)"          # SaturnExplorer/
emsdk="${EMSDK_DIR:-$PWD/emsdk}"
out="${OUT_DIR:-$(mktemp -d)}"
mkdir -p "$out/src"
if [ $# -gt 0 ]; then
  git -C "$root" show "$1:SaturnExplorer/Drivers/Savestate/src/SavestateDriver.cpp" > "$out/src/SavestateDriver.cpp"
else
  cp "$root/Drivers/Savestate/src/SavestateDriver.cpp" "$out/src/SavestateDriver.cpp"
fi
"$emsdk/upstream/emscripten/em++" -std=c++14 -O1 -I"$root/Drivers/Savestate/src" -I"$root/Drivers/Common/src" -I"$root/include" \
  "$out/src/SavestateDriver.cpp" "$root/Drivers/Common/src/SaturnStateShared.cpp" "$here/yss_wrap32.cpp" -o "$out/yss_wrap32.js"
# a program that never returns is the finding: 124 is the timeout
timeout "${WRAP_TIMEOUT:-20}" node "$out/yss_wrap32.js"; rc=$?
[ $rc -eq 124 ] && echo "NO RETURN within ${WRAP_TIMEOUT:-20} s: the walk over the section chain never ends"
exit $rc
