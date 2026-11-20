#!/usr/bin/env bash
# Fail unless a ctest -R selection matches exactly the number of tests it is meant to.
#
#   check-selected-count.sh <build-dir> <regex> <expected> [-- <extra ctest args, e.g. -C Release>]
#
# `ctest -R <regex> --no-tests=error` fails only when NOTHING matches, so a step that is meant to run three tests passes
# with one if two stop being registered (a CMake condition, a rename), and a regex that grew stale selects fewer than it
# names (A12-56). Run this before the step, with the count of the names in the regex.
set -u

build="${1:?usage: check-selected-count.sh <build-dir> <regex> <expected> [-- <ctest args>]}"
regex="${2:?usage: check-selected-count.sh <build-dir> <regex> <expected> [-- <ctest args>]}"
expected="${3:?usage: check-selected-count.sh <build-dir> <regex> <expected> [-- <ctest args>]}"
shift 3
[ "${1:-}" = "--" ] && shift

out="$(ctest --test-dir "$build" -N -R "$regex" "$@" 2>&1)" || { echo "$out"; echo "::error::ctest could not list the tests in $build"; exit 1; }
count="$(printf '%s\n' "$out" | sed -n 's/^Total Tests: *\([0-9][0-9]*\).*/\1/p' | tail -n 1)"
[ -n "$count" ] || { echo "$out" | tail -n 5; echo "::error::no 'Total Tests:' line in the ctest -N output"; exit 1; }

printf '%s\n' "$out" | sed -n 's/^ *Test *#[0-9]*: *//p'
echo "-R '$regex' selects $count tests (expected $expected)"
if [ "$count" -ne "$expected" ]; then
  echo "::error::-R '$regex' selects $count tests, expected $expected: a test is no longer registered (or was added: update the count with the regex)"
  exit 1
fi
