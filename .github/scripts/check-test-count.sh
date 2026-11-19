#!/usr/bin/env bash
# Fail when a configured build registers fewer CTest tests than a floor.
#
#   check-test-count.sh <build-dir> <minimum> [-- <extra ctest args, e.g. -C Debug>]
#
# A CMake condition that stops holding (Python not found, a changed platform test, a target that no
# longer registers) drops tests from the registration without any error, and `ctest` then runs fewer
# tests and passes (A12-37). The floor sits a little under the count the platform registered when it
# was set, so it catches a silent loss of several tests, not the deliberate removal of one: lower it
# in the same change that removes tests on purpose, and raise it when a platform gains many.
set -u

build="${1:?usage: check-test-count.sh <build-dir> <minimum> [-- <ctest args>]}"
min="${2:?usage: check-test-count.sh <build-dir> <minimum> [-- <ctest args>]}"
shift 2
[ "${1:-}" = "--" ] && shift

out="$(ctest --test-dir "$build" -N "$@" 2>&1)" || { echo "$out"; echo "::error::ctest could not list the tests in $build"; exit 1; }
count="$(printf '%s\n' "$out" | sed -n 's/^Total Tests: *\([0-9][0-9]*\).*/\1/p' | tail -n 1)"
[ -n "$count" ] || { echo "$out" | tail -n 5; echo "::error::no 'Total Tests:' line in the ctest -N output"; exit 1; }

echo "ctest registers $count tests in $build (floor $min)"
if [ "$count" -lt "$min" ]; then
  echo "::error::only $count tests are registered, fewer than the floor of $min: a registration condition stopped holding, or tests were removed without lowering the floor"
  exit 1
fi
