#!/bin/bash
# Self-test of run_census.sh's exit status: two chunks with a stub generator that passes, fails, or ends as a timeout
# does (exit 124). The run must exit 0 only when every chunk did, and run every chunk either way.
#   run_census_selftest.sh        (from anywhere; it uses the run_census.sh next to it)
set -u
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0
for case in "pass 0 0" "fail 42 1" "timeout 124 1"; do
    set -- $case
    name=$1 child=$2 want=$3
    stub=$work/$name.gen
    printf '#!/bin/bash\nexit %s\n' "$child" > "$stub"; chmod +x "$stub"
    bash "$here/run_census.sh" "$stub" "$work/$name" 1 1 2 100 > "$work/$name.out" 2>&1
    rc=$?
    chunks=$(grep -c "rc=$child" "$work/$name/chunks_g1.txt" 2>/dev/null)
    if [ "$rc" -ne "$want" ] || [ "$chunks" != 2 ]; then
        echo "FAIL $name: child exit $child, run exit $rc (want $want), chunks logged $chunks (want 2)"
        sed 's/^/  /' "$work/$name.out"; fail=1
    else
        echo "ok   $name: child exit $child, run exit $rc"
    fi
done
exit $fail
