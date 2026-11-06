#!/bin/bash
# Area 2 round 18 / area 3 round 17 reviewer. W = the SaturnExplorer directory (its build/ holds the binaries); S = a scratch directory where
# the scripts and asm.py/mk.py of ../a3_round16 sit (the driver drv is built there by ../a3_round16/bdrv16.sh).
# genrep.sh file.spec [substr]: each fixture through the generator's module checks (SE_GENERATED_WORDS + SE_GENERATED_FUNCS)
S=${S:-$(cd "$(dirname "$0")" && pwd)}
G=${W:?set W to the SaturnExplorer directory}/build/bin/SaturnExplorerSh2GeneratedTests
python3 $S/spec2gen.py "$@" | while IFS=$'\t' read name words funcs; do
  D=$(mktemp -d $S/gr.XXXX)
  r=$( ( ulimit -v 3500000; env $EXTRA SE_GENERATED_WORDS="$words" SE_GENERATED_FUNCS="$funcs" timeout 120 $G $D ) 2>&1 )
  echo "== $name: $(echo "$r" | grep -E 'FINDING|n:[a-z]' | head -5 | cut -c1-250 | tr '\n' '|') $(echo "$r" | tail -1)"
  [ -n "$V" ] && echo "$r" | tail -${V}
  rm -rf $D
done
