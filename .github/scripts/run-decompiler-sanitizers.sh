#!/usr/bin/env bash
# Run the decompiler test binaries of a sanitizer build and capture each one's output.
#
#   run-decompiler-sanitizers.sh <build-dir> <log-dir>
#
# <build-dir> is a CMake build of SaturnExplorer configured with
#   -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
# whose decompiler test targets (the list below) are built. Writes <log-dir>/<target>.log (stdout and
# stderr, also echoed) and <log-dir>/<target>.rc (exit status), and a timing table. It never
# stops at the first failing binary and always exits 0: pass/fail is decided afterwards by
# check-sanitizer-logs.sh, so every binary's report is seen in one run.
#
# The arguments are the ones SaturnExplorer/CMakeLists.txt gives ctest: a scratch directory
# per test (the Sla freshness test takes se-sleighc and the build directory instead).
set -u

build="${1:?usage: run-decompiler-sanitizers.sh <build-dir> <log-dir>}"
logs="${2:?usage: run-decompiler-sanitizers.sh <build-dir> <log-dir>}"
mkdir -p "$logs"
build="$(cd "$build" && pwd)"
logs="$(cd "$logs" && pwd)"

# Options are ':'-separated. LeakSanitizer is on explicitly (it is the default on Linux x86_64,
# but off under some ptrace-restricted containers; the job must not depend on that).
# halt_on_error=0 is UBSan's default, stated so a runtime error does not stop the run: every
# error in a binary is reported, and check-sanitizer-logs.sh sorts the accepted ones out.
export ASAN_OPTIONS="detect_leaks=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0"

summary="$logs/timings.txt"
: > "$summary"

# SE_SANITIZER_ONLY=<extended regex> runs only the matching binaries (for a quick local check).
# run <name> <command...>   gating: its log lands in <log-dir>, where check-sanitizer-logs.sh looks.
# run_info <name> <command...>   informational: same, but in <log-dir>/informational/, which the
#                                check does not read. See the SlaFreshness note below.
run_in() {
  local dest="$1" name="$2"; shift 2
  if [ -n "${SE_SANITIZER_ONLY:-}" ] && ! [[ "$name" =~ $SE_SANITIZER_ONLY ]]; then return; fi
  local start=$SECONDS rc
  mkdir -p "$dest"
  echo "::group::$name"
  "$@" 2>&1 | tee "$dest/$name.log"
  rc="${PIPESTATUS[0]}"
  echo "::endgroup::"
  echo "$rc" > "$dest/$name.rc"
  printf '%-44s exit %-3s %5ss%s\n' "$name" "$rc" "$((SECONDS - start))" \
    "$([ "$dest" = "$logs" ] || echo '  (informational, not gating)')" | tee -a "$summary"
}
run() { run_in "$logs" "$@"; }
run_info() { run_in "$logs/informational" "$@"; }

scratch() { rm -rf "$build/$1"; echo "$build/$1"; }

# Informational, not gating: this test runs se-sleighc, the SLEIGH compiler, as a child process,
# and the sanitizer build instruments it too. Upstream's compiler is not clean under them (an
# invalid downcast to LabelSymbol at slgh_compile.cc:2340, a load of an out-of-range enum at
# semantics.hh:88, a few leaked bytes), so the child exits non-zero and the test fails for a
# reason that has nothing to do with the engine the job is about. Its log is kept to watch.
run_info SaturnExplorerSlaFreshnessTests \
  "$build/bin/SaturnExplorerSlaFreshnessTests" "$build/bin/se-sleighc" "$build"
run SaturnExplorerSh2DecompilerEngineTests \
  "$build/bin/SaturnExplorerSh2DecompilerEngineTests" "$(scratch decompiler_engine_test)"
run SaturnExplorerSh2SemanticsTests \
  "$build/bin/SaturnExplorerSh2SemanticsTests" "$(scratch decompiler_Sh2Semantics_test)"
run SaturnExplorerSh2DecodeTests \
  "$build/bin/SaturnExplorerSh2DecodeTests" "$(scratch decompiler_Sh2Decode_test)"
run SaturnExplorerSh2GeneratedTests \
  "$build/bin/SaturnExplorerSh2GeneratedTests" "$(scratch decompiler_Sh2Generated_test)"
# The printed-C differential (a mode of the same binary): the generated functions' text compiled with the system
# compiler and compared with the emulator. It exits 77 when no C compiler is found (ctest reports that as skipped);
# the job counts any non-zero exit as a failure, so a runner without a compiler fails here instead of passing.
run SaturnExplorerSh2PrintedCTests \
  env SE_GENERATED_PRINTEDC=only "$build/bin/SaturnExplorerSh2GeneratedTests" "$(scratch decompiler_Sh2PrintedC_test)"
run SaturnExplorerSh2DecompilerTests \
  "$build/bin/SaturnExplorerSh2DecompilerTests" "$(scratch decompiler_Sh2Decompiler_test)"
run SaturnExplorerSh2DecompilerFixtureTests \
  "$build/bin/SaturnExplorerSh2DecompilerFixtureTests" "$(scratch decompiler_fixture_test)"
run SaturnExplorerDecompilerPanelTests \
  "$build/bin/SaturnExplorerDecompilerPanelTests" "$(scratch decompiler_panel_test)"
run SaturnExplorerDecompilerRunnerTests \
  "$build/bin/SaturnExplorerDecompilerRunnerTests" "$(scratch decompiler_DecompilerRunner_test)"
# The allocation-failure / cancel matrix (REVIEW_LEDGER.md, area 6): its default subset, which is deterministic. Every
# cell runs in a child process of this binary, so a crash, a hang, an ASan or UBSan report or a leak is a failed cell
# and the binary exits non-zero; the matrix does its own leak check per cell (LeakSanitizer's recoverable check, with
# the accepted leaks of A6-7 named in DecompilerFaultTests.cpp) and its own sorting of the two accepted upstream UBSan
# sites. Built without AddressSanitizer its leak check is a no-op, which is why it runs here and not only in ctest.
# SE_FAULT_FULL=1 (the whole matrix, hours) is not set.
# The environment cannot weaken the gate: the full matrix, a job filter and the leak-check switch are cleared.
run SaturnExplorerDecompilerFaultTests \
  env -u SE_FAULT_FULL -u SE_FAULT_ONLY -u SE_FAULT_LEAKS -u SE_FAULT_LEAKLIST -u SE_FAULT_RANGE -u SE_FAULT_PHASE \
  "$build/bin/SaturnExplorerDecompilerFaultTests" "$(scratch decompiler_fault_test)"

if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
  { echo '### Decompiler tests under ASan + UBSan + LSan'; echo '```'; cat "$summary"; echo '```'; } >> "$GITHUB_STEP_SUMMARY"
fi
exit 0
