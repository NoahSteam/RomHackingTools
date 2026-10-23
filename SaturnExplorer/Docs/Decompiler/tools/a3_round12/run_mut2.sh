#!/bin/bash
# The instrument mutations of area 3 round 12, one at a time, resumable (a mutation with a .done file is skipped); never two heavy jobs at once.
# Each: a mutated copy of an engine file (heritage.cc) or of Sh2Decompiler.cpp is built into the generator, which runs 3000 seeds of printed-C
# mode (where the laundered walks are drawn) and, for the host rules check 10b holds, 3000 seeds of grammar 2. A mutation the generator does not
# fail is an instrument row.
# usage: W=<worktree>/SaturnExplorer B=<build dir> OUT=<dir> run_mut.sh
: "${W:?}" "${B:?}" "${OUT:?}"
here=$(cd "$(dirname "$0")" && pwd)
H=$W/FrontEnd/third_party/ghidra-decompiler/cpp/heritage.cc
S=$W/FrontEnd/src/Decompiler/Sh2Decompiler.cpp
mkdir -p $OUT/mut
mut() {   # name kind(engine|host) grammar2(0|1) mutation...   (mutation = old===new, see mutate.py)
  local n=$1 kind=$2 g2=$3; shift 3
  [ -f $OUT/mut/$n.done ] && return
  mkdir -p $OUT/mut/$n
  if [ $kind = engine ]; then
    python3 $here/mutate.py $H $OUT/mut/$n/heritage.cc "$@" || { echo "$n: mutation failed" > $OUT/mut/$n.res; touch $OUT/mut/$n.done; return; }
    W=$W B=$B OUT=$OUT/mut bash $here/build_emut.sh $n gen -- heritage.cc > $OUT/mut/$n.build 2>&1
  else
    python3 $here/mutate.py $S $OUT/mut/$n/Sh2Decompiler.cpp "$@" || { echo "$n: mutation failed" > $OUT/mut/$n.res; touch $OUT/mut/$n.done; return; }
    W=$W B=$B OUT=$OUT/mut bash $here/build_hmut.sh $n gen > $OUT/mut/$n.build 2>&1
  fi
  [ -x $OUT/mut/$n/gen ] || { echo "$n: build failed" > $OUT/mut/$n.res; touch $OUT/mut/$n.done; return; }
  ( ulimit -v 4000000; SE_GENERATED_PRINTEDC=only SE_GENERATED_SEEDS=3000 timeout 1150 $OUT/mut/$n/gen $OUT/mut/$n/w > $OUT/mut/$n.pc.log 2>&1 )
  local pcf=$(grep -c '^FINDING' $OUT/mut/$n.pc.log)
  local g2f=-
  if [ $g2 = 1 ]; then
    ( ulimit -v 4000000; SE_GENERATED_GRAMMAR=2 SE_GENERATED_SEEDS=3000 timeout 1150 $OUT/mut/$n/gen $OUT/mut/$n/w > $OUT/mut/$n.g2.log 2>&1 )
    g2f=$(grep -c '^FINDING' $OUT/mut/$n.g2.log)
  fi
  local cls=$(grep -h '^  \[' $OUT/mut/$n.pc.log $OUT/mut/$n.g2.log 2>/dev/null | sed -E 's/^  \[([^]]*)\].*/\1/' | sort | uniq -c | sort -rn | head -4 | tr '\n' ' ')
  echo "$n: printed-C findings $pcf, grammar 2 findings $g2f; $cls" > $OUT/mut/$n.res
  rm -rf $OUT/mut/$n/w
  touch $OUT/mut/$n.done
}
# (a second list, run after the first: more of the walk's own rules)
mut E11_lowest engine 0 "      candidates.push_back((*liter)->getOffset());===      {}"
mut E13_freestack engine 0 "  if (preHeritage) {\n    VarnodeLocSet::const_iterator iter = fd->beginLoc(spc);===  if (false) {\n    VarnodeLocSet::const_iterator iter = fd->beginLoc(spc);"
mut E17_propagate engine 0 "      if (unmodeled.insert(out).second) uwork.push_back(out);===      {}"
mut E18_loaduse engine 0 "      case CPUI_LOAD:\n\tif (op->getIn(1) == vn) escaped = true;===      case CPUI_LOAD:\n\tif (false) escaped = true;"
mut E19_calluse engine 0 "      case CPUI_CALLOTHER:\n\tescaped = true;\n\tbreak;===      case CPUI_CALLOTHER:\n\tbreak;"
mut H6b_tail_jump host 0 "                                    mStoresBelow |= Get(s, mReg[r]).Origins() & 0x7FFFu;\n===                                    {}\n"
