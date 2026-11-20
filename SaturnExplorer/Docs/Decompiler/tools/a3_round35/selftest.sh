#!/bin/bash
# Smoke test of run.sh's failure path: a generator that fails a CHECK and exits 42, one that times out-like exits 124, and one that
# exits 0 without its completion lines, and one whose CHECK is not the findings check must each make run.sh exit non-zero and print FAILED, never "findings none".
here=$(cd "$(dirname "$0")" && pwd)
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/W/build/bin" "$t/S"
fail=0
for body in 'echo "CHECK failed: injected"; exit 42' 'exit 124' 'echo "decompiled"; exit 0' 'printf "1 finding(s)\nCHECK failed at x.cpp:1: other()\n"; exit 1'; do
    printf '#!/bin/bash\n%s\n' "$body" > "$t/W/build/bin/SaturnExplorerSh2GeneratedTests"; chmod +x "$t/W/build/bin/SaturnExplorerSh2GeneratedTests"
    o=$(W="$t/W" S="$t/S" "$here/run.sh" x1 2>&1); rc=$?
    if [ $rc -eq 0 ] || ! grep -q 'x1: FAILED' <<<"$o" || grep -q 'findings none' <<<"$o"; then echo "selftest: not caught: $body (rc=$rc)"; fail=1; fi
done
[ $fail -eq 0 ] && echo "selftest: ok (4 failing children caught)"
exit $fail
