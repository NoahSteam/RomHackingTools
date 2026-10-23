#!/bin/bash
# For the mutations the generator did not fail (run_mut.sh), link the suite with the mutated object and run it: the pins that catch it (or none).
# usage: W=<worktree>/SaturnExplorer B=<build dir> OUT=<dir given to run_mut.sh> run_pins.sh name...
: "${W:?}" "${B:?}" "${OUT:?}"
here=$(cd "$(dirname "$0")" && pwd)
for n in "$@"; do
  f=heritage.cc; case $n in H*) f=Sh2Decompiler.cpp;; esac
  if [ $f = heritage.cc ]; then W=$W B=$B OUT=$OUT/mut bash $here/build_emut.sh $n tests -- heritage.cc > $OUT/mut/$n.tbuild 2>&1
  else W=$W B=$B OUT=$OUT/mut bash $here/build_hmut.sh $n tests > $OUT/mut/$n.tbuild 2>&1; fi
  ( ulimit -v 4000000; timeout 900 $OUT/mut/$n/tests $OUT/mut/$n/scr > $OUT/mut/$n.suite.log 2>&1; echo "rc $?" >> $OUT/mut/$n.suite.log )
  fails=$(grep -c "CHECK failed" $OUT/mut/$n.suite.log)
  echo "$n: suite rc $(tail -1 $OUT/mut/$n.suite.log), CHECK failures $fails; $(grep 'CHECK failed' $OUT/mut/$n.suite.log | sed -E 's/.*Tests.cpp:([0-9]+).*/\1/' | tr '\n' ' ' | cut -c1-120)" | tee $OUT/mut/$n.pins
  rm -rf $OUT/mut/$n/scr $OUT/mut/$n/tests
done
