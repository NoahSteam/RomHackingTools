#!/usr/bin/env bash
# A long run of SaturnExplorerLoaderFuzzTests in pieces, each of at most PIECE_SECONDS (default 1200, the program's 20
# minutes): the case indices are split into ranges of CASES_PER_PIECE per module (SE_LOADFUZZ_FROM / SE_LOADFUZZ_CASES),
# a piece that ends in a crash or a violation stops the run, and the range to resume from is printed.
#   run_pieces.sh <test-binary> <scratch-dir> <first-piece> <last-piece> [seed]
# Each piece is `timeout`ed. Under ASan/UBSan do not use `ulimit -v` (the shadow memory is address space).
set -u
bin="${1:?test binary}"; scratch="${2:?scratch dir}"; first="${3:?first piece}"; last="${4:?last piece}"; seed="${5:-9}"
per="${CASES_PER_PIECE:-1500}"
secs="${PIECE_SECONDS:-1200}"
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=0}"
for ((k = first; k <= last; k++)); do
  from=$((k * per))
  log="$scratch/piece_$k.log"
  start=$SECONDS
  SE_LOADFUZZ_SEED="$seed" SE_LOADFUZZ_FROM="$from" SE_LOADFUZZ_CASES="$per" SE_LOADFUZZ_NOFIXED=1 \
    timeout "$secs" "$bin" "$scratch/run_$k" > "$log" 2>&1
  rc=$?
  printf 'piece %d: cases %d..%d, exit %d, %ds, violations: %s, runtime errors: %s\n' "$k" "$from" "$((from + per - 1))" "$rc" \
    "$((SECONDS - start))" "$(grep -c '^VIOLATION' "$log")" "$(grep -c 'runtime error' "$log")"
  if [ "$rc" -ne 0 ]; then echo "stopped at piece $k (resume with first-piece=$k); see $log"; exit "$rc"; fi
done
