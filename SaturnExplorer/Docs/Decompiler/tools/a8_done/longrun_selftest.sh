#!/bin/bash
# Self-test of longrun.sh's exit status: chunk 6 (one model job) with stub binaries that pass, fail, or end as a
# timeout does (exit 124). The chunk must exit 0 only when every job did, and keep each job's log either way.
#   longrun_selftest.sh        (from anywhere; it uses the longrun.sh next to it)
set -u
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0
for case in "pass 0 0" "fail 42 1" "timeout 124 1"; do
    set -- $case
    name=$1 child=$2 want=$3
    b=$work/$name/build; l=$work/$name/logs
    mkdir -p "$b/bin"
    for t in SaturnExplorerDecompilerTrustTests SaturnExplorerDecompilerModelTests; do
        printf '#!/bin/bash\necho "1 seed(s), %s failed"\n[ %s -ne 0 ] && echo "seed 1: FAILED"\nexit %s\n' \
            "$([ "$child" -eq 0 ] && echo 0 || echo 1)" "$child" "$child" > "$b/bin/$t"
        chmod +x "$b/bin/$t"
    done
    "$here/longrun.sh" "$b" "$l" 6 > "$work/$name.out" 2>&1
    rc=$?
    if [ "$rc" -ne "$want" ] || [ ! -s "$l/model_10000.log" ]; then
        echo "FAIL $name: child exit $child, chunk exit $rc (want $want), log $( [ -s "$l/model_10000.log" ] && echo kept || echo missing)"
        sed 's/^/  /' "$work/$name.out"; fail=1
    else
        echo "ok   $name: child exit $child, chunk exit $rc"
    fi
done
exit $fail
