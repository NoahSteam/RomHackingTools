#!/usr/bin/env bash
# Self-test of the sanitizer harness: run-decompiler-sanitizers.sh + check-sanitizer-logs.sh, against stub
# binaries instead of a sanitizer build. It proves the gate, not the engine:
#
#   * the runner invokes every binary, the fault matrix (SaturnExplorerDecompilerFaultTests) included;
#   * a fault-matrix run that exits non-zero, prints an AddressSanitizer / LeakSanitizer report, or prints an
#     unaccepted UBSan error fails the checker, and one that is clean (with the two accepted upstream UBSan lines
#     in its output) passes it;
#   * a binary the runner never ran fails the checker (the shape of A12-11: a target that was in no list);
#   * each of the model test and the two live tests (SaturnExplorerLiveRestoreSignalTests, which prints its own pass
#     line, and SaturnExplorerLiveDecompilerCaptureTests) is on the list: left out, the checker fails;
#   * an empty log with exit status 0, or a log without the binary's pass line, fails it (A6-20), for the fault matrix
#     and for an ordinary binary.
#
#   test-sanitizer-harness.sh            (from anywhere; uses a temporary directory)
set -u
unset SE_SANITIZER_ONLY

here="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

NAMES=(
  SaturnExplorerSh2DecompilerEngineTests SaturnExplorerSh2SemanticsTests SaturnExplorerSh2DecodeTests
  SaturnExplorerSh2GeneratedTests SaturnExplorerSh2DecompilerTests SaturnExplorerSh2DecompilerFixtureTests
  SaturnExplorerDecompilerPanelTests SaturnExplorerDecompilerRunnerTests SaturnExplorerSlaFreshnessTests
  SaturnExplorerDecompilerModelTests SaturnExplorerLiveRestoreSignalTests SaturnExplorerLiveDecompilerCaptureTests
)

# stub <build> <name> <exit code> [output line]
stub() {
  local build="$1" name="$2" rc="$3" out="${4:-}"
  mkdir -p "$build/bin"
  printf '#!/usr/bin/env bash\necho "$0 invoked: $*" >> "%s/invoked.txt"\n[ -n %q ] && echo %q\nexit %s\n' \
    "$build" "$out" "$out" "$rc" > "$build/bin/$name"
  chmod +x "$build/bin/$name"
}

# one <label> <fault stub exit> <fault stub output> <omit the fault stub: 0|1> <expected checker result: pass|fail> [<output of SaturnExplorerSh2DecompilerTests> [<another binary to leave out>]]
# A stub prints its binary's pass line unless told otherwise; the fault stub's output given here comes after it, and "-"
# means no pass line at all.
failures=0
one() {
  local label="$1" frc="$2" fout="$3" omit="$4" want="$5" other="${6-all passed}" leave="${7-}"
  local build="$tmp/$label/build" logs="$tmp/$label/logs"
  mkdir -p "$build" "$logs"
  for n in "${NAMES[@]}"; do
    [ "$n" = "$leave" ] && continue
    # Each binary prints its own pass line: dts::Finish's, except LiveRestoreSignalTests' (check-sanitizer-logs.sh).
    if [ "$n" = SaturnExplorerSh2DecompilerTests ]; then stub "$build" "$n" 0 "$other"
    elif [ "$n" = SaturnExplorerLiveRestoreSignalTests ]; then stub "$build" "$n" 0 "LiveRestoreSignalTests passed"
    else stub "$build" "$n" 0 "all passed"; fi
  done
  stub "$build" se-sleighc 0
  [ "$omit" = 1 ] || stub "$build" SaturnExplorerDecompilerFaultTests "$frc" "$fout"
  bash "$here/run-decompiler-sanitizers.sh" "$build" "$logs" > "$tmp/$label/run.out" 2>&1
  local run_rc=$?
  # A binary the runner never ran has no log at all (left out of its list), which is not what running a missing
  # stub gives (exit 127 and a log): take the log away, so only the checker's own list can fail it.
  [ -z "$leave" ] || rm -f "$logs/$leave.log" "$logs/$leave.rc"
  bash "$here/check-sanitizer-logs.sh" "$logs" > "$tmp/$label/check.out" 2>&1
  local check_rc=$?
  local got=pass
  [ "$check_rc" -eq 0 ] || got=fail
  local invoked=no
  grep -q "SaturnExplorerDecompilerFaultTests invoked" "$build/invoked.txt" 2>/dev/null && invoked=yes
  local ok=ok
  if [ "$run_rc" -ne 0 ] || [ "$got" != "$want" ]; then ok=WRONG; failures=$((failures + 1)); fi
  # A stubbed fault suite that exists must have been invoked by the runner.
  if [ "$omit" != 1 ] && [ "$invoked" != yes ]; then ok=WRONG; failures=$((failures + 1)); fi
  printf '%-34s runner rc=%s fault-suite-invoked=%-3s checker=%-4s (want %s)  %s\n' "$label" "$run_rc" "$invoked" "$got" "$want" "$ok"
  if [ "$ok" = WRONG ]; then sed 's/^/    | /' "$tmp/$label/check.out" | tail -8; fi
}

FM="fault matrix: 0 of 25 job(s) failed"
one clean                0  "$FM"                                                                        0 pass
one accepted-ubsan       0  "src/address.hh:605:9: runtime error: left shift of 1 by 31 places
$FM"                                                                                                     0 pass
one status-42            42 "$FM"                                                                        0 fail
one failed-jobs          0  "fault matrix: 1 of 25 job(s) failed"                                        0 fail
one asan-report          0  "==1==ERROR: AddressSanitizer: heap-use-after-free on address 0x1
$FM"                                                                                                     0 fail
one leak-report          0  "==1==ERROR: LeakSanitizer: detected memory leaks
$FM"                                                                                                     0 fail
one new-ubsan            0  "src/other.cc:12:3: runtime error: signed integer overflow
$FM"                                                                                                     0 fail
one fault-suite-omitted  0  ""                                                                           1 fail
one fault-empty-log      0  ""                                                                           0 fail
one other-empty-log      0  "$FM"                                                                        0 fail ""
one other-no-pass-line   0  "$FM"                                                                        0 fail "ran 3 tests"
# The round 4 tests are on the list too: one of them never run is a failure, as the fault matrix's was (A12-11).
one model-omitted        0  "$FM"                                                                        0 fail "all passed" SaturnExplorerDecompilerModelTests
one live-restore-omitted 0  "$FM"                                                                        0 fail "all passed" SaturnExplorerLiveRestoreSignalTests
one live-capture-omitted 0  "$FM"                                                                        0 fail "all passed" SaturnExplorerLiveDecompilerCaptureTests

if [ "$failures" -ne 0 ]; then
  echo "Sanitizer harness self-test FAILED ($failures wrong outcome(s))."
  exit 1
fi
echo "Sanitizer harness self-test passed."
