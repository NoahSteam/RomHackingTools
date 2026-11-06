#!/bin/bash
# usage: pcwords.sh "WORDS" [env...]  (W = the SaturnExplorer directory, S = scratch): one printed-C differential, verbose
# usage: pc.sh "WORDS" [env...]  one printed-C differential, verbose
W=${W:?set W to the SaturnExplorer directory}
words="$1"; shift
D=$(mktemp -d ${S:-/tmp}/pcw.XXXX)
( ulimit -v 3500000; env "$@" SE_GENERATED_PRINTEDC=only SE_GENERATED_VERBOSE=1 SE_GENERATED_PRINTEDC_WORDS="$words" timeout 300 nice $W/build/bin/SaturnExplorerSh2GeneratedTests $D 2>&1 | grep -v '^-- \|^printed-C diff\|^Sh2Gen\|^all passed' )
rm -rf $D
