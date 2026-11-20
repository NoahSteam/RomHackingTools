#!/bin/bash
# Smoke test of run.sh's result handling (A3-254) with a substituted generator binary.
# Failing children must make run.sh exit non-zero and print FAILED, never "findings none": a CHECK with exit 42, exit 124 (timeout),
# exit 0 without the completion lines, and exit 1 with a CHECK other than the findings one.
# Finished children must be summarised with what they found: a generator-class finding and an ordinary one are named, a clean run is "none".
here=$(cd "$(dirname "$0")" && pwd)
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/W/build/bin" "$t/S"
bin="$t/W/build/bin/SaturnExplorerSh2GeneratedTests"
fail=0
run() { printf '#!/bin/bash\n%s\n' "$1" > "$bin"; chmod +x "$bin"; o=$(W="$t/W" S="$t/S" "$here/run.sh" x1 2>&1); rc=$?; }
for body in 'echo "CHECK failed: injected"; exit 42' 'exit 124' 'echo "decompiled"; exit 0' \
            'printf "1 finding(s)\nCHECK failed at x.cpp:1: other()\n"; exit 1'; do
    run "$body"
    if [ $rc -eq 0 ] || ! grep -q 'x1: FAILED' <<<"$o" || grep -q 'findings none' <<<"$o"; then echo "selftest: not caught: $body (rc=$rc)"; fail=1; fi
done
done1='printf "function 0 at 06009000\n  [%s] mode 0\n1 finding(s)\nCHECK failed at x.cpp:1: rep.findings.empty()\n1 failure(s)\n" '
for want in n:generator:no-return n:caution-unsaid; do
    run "$done1$want; exit 1"
    if [ $rc -ne 0 ] || ! grep -q "findings $want\$" <<<"$o"; then echo "selftest: finding not named: $want (rc=$rc): $o"; fail=1; fi
done
run 'printf "function 0 at 06009000\n0 finding(s)\nall passed\n"; exit 0'
if [ $rc -ne 0 ] || ! grep -q 'findings none$' <<<"$o"; then echo "selftest: clean run not clean (rc=$rc): $o"; fail=1; fi
[ $fail -eq 0 ] && echo "selftest: ok (4 failing children caught, 2 findings named, 1 clean run)"
exit $fail
