#!/usr/bin/env bash
# Fail on any sanitizer report in the captured decompiler test logs.
#
#   check-sanitizer-logs.sh <log-dir>
#
# <log-dir> holds one <name>.log per test binary (stdout + stderr) and one <name>.rc with its
# exit status, as written by the "Run" step of the sanitizers job in
# .github/workflows/saturn-explorer-decompiler.yml.
#
# Fails when, in any log:
#   * a test binary exited non-zero (a failed assertion, or ASan/LSan aborting the process);
#   * AddressSanitizer or LeakSanitizer reported anything;
#   * UBSan printed a "runtime error:" line that is not one of the ALLOWED sites below.
#
# The allow-list is the two upstream SLEIGH shifts recorded as accepted in
# SaturnExplorer/Docs/FunctionalityVerification/15_DECOMPILER_STEP5.md. It is matched by
# file:line against the report's own location, not with a UBSan suppressions file: those match
# a whole source file (address.hh is included everywhere), so they would also hide any new shift
# in the same file, and the runtime has no line granularity. A bump of the vendored Ghidra that
# moves these lines fails this check on purpose: re-confirm the sites and update the patterns.
set -u

dir="${1:?usage: check-sanitizer-logs.sh <log-dir>}"

ALLOWED=(
  '(^|/)address\.hh:605:[0-9]+: runtime error: left shift of '
  '(^|/)slghpatexpress\.cc:979:[0-9]+: runtime error: left shift of negative value'
)
allow_re="$(IFS='|'; echo "${ALLOWED[*]}")"

shopt -s nullglob
logs=("$dir"/*.log)
if [ "${#logs[@]}" -eq 0 ]; then
  echo "::error::no sanitizer logs found in $dir"
  exit 1
fi

fail=0
for log in "${logs[@]}"; do
  name="$(basename "$log" .log)"
  rc_file="${log%.log}.rc"
  rc="$(cat "$rc_file" 2>/dev/null || echo missing)"

  if [ "$rc" != "0" ]; then
    echo "::error::$name exited with status $rc"
    fail=1
  fi

  if grep -n -E 'ERROR: (Address|Leak)Sanitizer|SUMMARY: (Address|Leak)Sanitizer' "$log"; then
    echo "::error::$name: AddressSanitizer / LeakSanitizer report (above)"
    fail=1
  fi

  bad="$(grep -n 'runtime error:' "$log" | grep -v -E "$allow_re" || true)"
  if [ -n "$bad" ]; then
    printf '%s\n' "$bad"
    echo "::error::$name: UBSan runtime error outside the accepted upstream sites (above)"
    fail=1
  fi

  allowed_n="$(grep -c -E "$allow_re" "$log" || true)"
  echo "$name: exit $rc, accepted upstream UBSan lines: ${allowed_n:-0}"
done

if [ "$fail" -ne 0 ]; then
  echo "Sanitizer check FAILED."
  exit 1
fi
echo "Sanitizer check passed: no ASan/LSan report, no UBSan error beyond the two accepted sites."
