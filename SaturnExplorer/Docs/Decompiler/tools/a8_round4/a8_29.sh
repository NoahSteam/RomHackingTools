#!/bin/bash
# Area 8, round 4, A8-29: the CPU switch under Follow PC lands on a question whose Stop was forgotten, and asks nothing. Each script fails
# NOT REFRESHED on the last step (paused live source, Follow PC on): the output is stale, nothing is outstanding, the Stop is gone.
#   a8_29.sh <build dir>        (SaturnExplorerDecompilerTrustTests built in it)
# Then the two fresh seeds of the CPU-weighted mixes (SE_TRUST_FOLLOW_CPU=1, SE_TRUST_NOHOVER=1): 50534 (mix 8) and 50814 (mix 9).
B=${1:?build dir}
T=$B/bin/SaturnExplorerDecompilerTrustTests
S=$(mktemp -d)
ulimit -v 3500000
for sc in \
  "cpu; regsoff; cpu; hold; edit; stop; release; cpu; cpu" \
  "cpu; pcbios; cpu; hold; edit; stop; release; cpu; cpu" \
  "hold; containc; pcc; cpu; regsoff; stop; follow; cpu" \
  "pct; cpu; regsoff; hold; typet; cpu; cpu; refresh; follow; stop; cpu"; do
  echo "== $sc"
  SE_TRUST_FOLLOW_SCRIPT="$sc" timeout 300 $T $S/x 2>&1 | grep -E "^follow script|TRUST" | cut -c1-200
done
SE_TRUST_FOLLOW_CPU=1 SE_TRUST_NOHOVER=1 SE_TRUST_FOLLOW_SEED=50534 timeout 300 $T $S/x 2>&1 | grep -E "^follow seed|TRUST" | cut -c1-200
SE_TRUST_FOLLOW_AUTO=1 SE_TRUST_FOLLOW_CPU=1 SE_TRUST_NOHOVER=1 SE_TRUST_FOLLOW_SEED=50814 timeout 300 $T $S/x 2>&1 | grep -E "^follow seed|TRUST" | cut -c1-200
rm -rf "$S"
