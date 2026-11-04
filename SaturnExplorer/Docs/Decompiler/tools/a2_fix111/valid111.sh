#!/bin/bash
# usage: valid111.sh <SaturnExplorerSh2GeneratedTests> <out dir> <first seed> [functions per family (default 2000)]
# The area 2 fixer's validation of the printed-C differential: dagen16 families 98 and 99 and dagen17 families 100 to 103, 107 to 109 (register-only), the memory families 91 to 94 and 97
# (little-endian reading) and 104 to 106, 110, 111 (also on the big-endian arena, BE=1). One summary line per chunk in <out>/summary.txt (fam17.sh of round 17, which it calls).
here=$(cd "$(dirname "$0")" && pwd); f17=$here/../a2_round17/fam17.sh
gen=$1; out=$2; first=$3; n=${4:-2000}
$f17 $gen $out/reg $n $first 98 99 100 101 102 103 107 108 109
$f17 $gen $out/mem $n $first 91 92 93 94 97
BE=1 $f17 $gen $out/be $n $first 104 105 106 110 111
cat $out/*/summary.txt
