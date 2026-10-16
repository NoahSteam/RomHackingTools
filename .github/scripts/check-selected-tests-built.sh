#!/usr/bin/env bash
# Fail unless every CTest test the verify workflow is about to select has an executable that exists.
#
#   check-selected-tests-built.sh <build-dir> <name>... [-- <extra ctest args, e.g. -C Debug>]
#
# <name> is the middle of a test name: SaturnExplorer<name>Tests. The same list builds the targets
# (SaturnExplorer<name>Tests each), forms the -R regex, and is checked here, so a test cannot be
# selected without being built (A12-10: Sh2DecodeTests and Sh2GeneratedTests were in the regex and in
# no build list, and ctest ended "Not Run" with exit 8 for the missing executables).
#
# It asks ctest itself (--show-only=json-v1), which leaves out the command of a test whose executable
# it cannot find, and then looks at the file as well. Fails when
#   * a listed name selects no test (the regex or the CMake test names drifted);
#   * a selected test has no command, or its executable is not a file.
# Needs python3 (every runner has one) for the JSON.
set -u

build="${1:?usage: check-selected-tests-built.sh <build-dir> <name>... [-- <ctest args>]}"
shift
names=()
extra=()
while [ "$#" -gt 0 ]; do
  if [ "$1" = "--" ]; then shift; extra=("$@"); break; fi
  names+=("$1"); shift
done
[ "${#names[@]}" -gt 0 ] || { echo "::error::no test names given"; exit 2; }

alt=""
for n in "${names[@]}"; do alt="${alt:+$alt|}$n"; done
regex="SaturnExplorer($alt)Tests"

json="$(mktemp)"
trap 'rm -f "$json" "$json.err"' EXIT
if ! ctest --test-dir "$build" -N --show-only=json-v1 -R "$regex" ${extra[@]+"${extra[@]}"} > "$json" 2> "$json.err"; then
  cat "$json.err"
  echo "::error::ctest could not list the tests selected by -R '$regex' in $build"
  exit 1
fi

python3 - "$json" "$regex" "${names[@]}" <<'PY'
import json, os, sys

path, regex, names = sys.argv[1], sys.argv[2], sys.argv[3:]
tests = json.load(open(path)).get("tests", [])
byname = {t["name"]: t for t in tests}
bad = 0
for n in names:
    full = "SaturnExplorer%sTests" % n
    if full not in byname:
        print("::error::no CTest test named %s (selected by -R '%s')" % (full, regex))
        bad += 1
for t in tests:
    cmd = t.get("command") or []
    exe = cmd[0] if cmd else ""
    ok = bool(exe) and os.path.isfile(exe)
    print("%-48s %s" % (t["name"], exe if ok else "MISSING (not built)"))
    if not ok:
        print("::error::%s is selected but its executable does not exist: build its target before running ctest" % t["name"])
        bad += 1
sys.exit(1 if bad else 0)
PY
rc=$?
if [ "$rc" -ne 0 ]; then echo "Selected-tests check FAILED."; exit 1; fi
echo "All ${#names[@]} named tests are selected and built."
