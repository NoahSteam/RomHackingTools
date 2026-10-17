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
#   * a binary on the EXPECTED list has no log or exit status (the runner did not run it);
#   * a log is empty, or lacks its binary's pass line ("all passed"; the fault matrix's "fault matrix: 0 of N job(s) failed");
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

# The binaries the runner must have run. A log that is not there is a failure: a binary the runner
# (or the workflow's build list) no longer covers would otherwise pass unseen, which is how
# SaturnExplorerDecompilerFaultTests stayed out of the gate (A12-11). Keep in step with
# run-decompiler-sanitizers.sh. SE_SANITIZER_ONLY (an extended regex, as for the runner) narrows
# the list for a local partial run.
EXPECTED=(
  SaturnExplorerSh2DecompilerEngineTests
  SaturnExplorerSh2SemanticsTests
  SaturnExplorerSh2DecodeTests
  SaturnExplorerSh2GeneratedTests
  SaturnExplorerSh2PrintedCTests
  SaturnExplorerSh2DecompilerTests
  SaturnExplorerSh2DecompilerFixtureTests
  SaturnExplorerDecompilerPanelTests
  SaturnExplorerDecompilerRunnerTests
  SaturnExplorerDecompilerFaultTests
  SaturnExplorerDecompilerModelTests
  SaturnExplorerLiveRestoreSignalTests
  SaturnExplorerLiveDecompilerCaptureTests
)

# The line each binary prints when, and only when, all of its tests passed: dts::Finish's "all passed" (and the older
# binaries' own), the fault matrix's summary, LiveRestoreSignalTests' own line.
pass_re() {
  case "$1" in
    SaturnExplorerDecompilerFaultTests) echo '^fault matrix: 0 of [0-9]+ job\(s\) failed' ;;
    SaturnExplorerLiveRestoreSignalTests) echo '^LiveRestoreSignalTests passed$' ;;
    *) echo '^all passed$' ;;
  esac
}

shopt -s nullglob
logs=("$dir"/*.log)
if [ "${#logs[@]}" -eq 0 ]; then
  echo "::error::no sanitizer logs found in $dir"
  exit 1
fi

fail=0
for name in "${EXPECTED[@]}"; do
  if [ -n "${SE_SANITIZER_ONLY:-}" ] && ! [[ "$name" =~ $SE_SANITIZER_ONLY ]]; then continue; fi
  if [ ! -f "$dir/$name.log" ] || [ ! -f "$dir/$name.rc" ]; then
    echo "::error::$name has no log or no exit status in $dir: the runner did not run it"
    fail=1
  fi
done
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

  # An empty log, or one without its binary's own pass line, is a failure whatever the exit status says: a binary that
  # left without running its tests (or a log truncated, or an empty file with rc 0) must not pass (A6-20).
  if [ ! -s "$log" ]; then
    echo "::error::$name: the log is empty"
    fail=1
  elif ! grep -q -E "$(pass_re "$name")" "$log"; then
    echo "::error::$name: no pass line ($(pass_re "$name")) in the log: it did not finish its tests"
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
