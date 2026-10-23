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
EB="\tcase CPUI_LOAD:\n\t  continue;\t\t\t// an access through it"
mut E1_bitwise engine 0 "${EB}===\tcase CPUI_INT_AND:\n\tcase CPUI_INT_XOR:\n\tcase CPUI_INT_OR:\n\t  break;\n${EB}"
mut E2_pieces engine 0 "${EB}===\tcase CPUI_SUBPIECE:\n\tcase CPUI_PIECE:\n\tcase CPUI_INT_ZEXT:\n\tcase CPUI_INT_SEXT:\n\t  break;\n${EB}"
mut E3_negate engine 0 "${EB}===\tcase CPUI_INT_2COMP:\n\tcase CPUI_INT_NEGATE:\n\t  break;\n${EB}"
mut E4_shifts engine 0 "${EB}===\tcase CPUI_INT_LEFT:\n\tcase CPUI_INT_RIGHT:\n\tcase CPUI_INT_SRIGHT:\n\tcase CPUI_INT_MULT:\n\t  break;\n${EB}"
mut E5_memoryescape engine 0 "\t  if (op->getIn(2) == vn) memoryEscape = true;\t// (b)===\t  if (false) memoryEscape = true;\t// (b)"
mut E6_join engine 0 "if (members.find(multi->getIn(j)) == members.end()) {===if (false) {"
mut E7_callout engine 0 "if (out != (Varnode *)0) unmodeled.insert(out);\t// (d): what a call handed a frame address returns may be one===;"
mut E9_widen engine 0 "  if (storeGuard.empty()) return;\n  AddrSpace *stackSpc===  return;\n  AddrSpace *stackSpc"
mut E10_markstores engine 0 "    if (!escape.storeIsUnmodeled(op)) continue;\n    fd->opMarkSpacebasePtr(op);\n    freeStores.push_back(op);===    continue;\n    fd->opMarkSpacebasePtr(op);\n    freeStores.push_back(op);"
mut H1_store_below host 1 "        if (addr.StoresBelow()) mStoresBelow |= 1u << addr.baseReg;\n===" "        else if (addr.kind == Value::Fuzzy && addr.Base() == Value::kNoBase && addr.baseReg < 0)\n            mStoresBelow |= addr.Origins() & 0x7FFFu;\n==="
mut H2_store_laundered host 1 "        else if (addr.kind == Value::Fuzzy && addr.Base() == Value::kNoBase && addr.baseReg < 0)\n            mStoresBelow |= addr.Origins() & 0x7FFFu;\n==="
mut H3_transitive host 0 "A caller handing us a frame address is then exposed.\n                    mStoresBelow |= h.Origins() & 0x7FFFu;===A caller handing us a frame address is then exposed.\n"
mut H4_exposure_exact host 0 "                    if (h.Origins() & origin::kStack)\n                    {\n                        mEscaped = true;\n                        mEscapedLow = true;   // from the frame's lowest slot (FrameEscape)===                    if (h.kind == Value::Sp)\n                    {\n                        mEscaped = true;\n                        mEscapedLow = true;   // from the frame's lowest slot (FrameEscape)"
mut H5_load_taint host 0 "        f.from |= taint;\n===" "            l.from |= taint;\n==="
mut H6_tail_jump host 0 "                                    mStoresBelow |= Get(s, mReg[r]).Origins() & 0x7FFFu;\n==="
mut H7_probe_below host 0 "                const uint32_t sb = CalleeStoresBelow(target);===                const uint32_t sb = 0;"
mut H8_possibleframe host 0 "        return x.kind == Value::Fuzzy && x.own && (x.Origins() & origin::kStack) && (x.Origins() & kAnything) != kAnything;===        return false;"
mut H9_leak_escaped host 0 "        if (map(e.leaks & ~origin::kStack) & origin::kStack) s.escaped = true;\n==="
