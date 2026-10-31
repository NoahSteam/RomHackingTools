#!/bin/bash
# (round 17: the same as mkrc14.sh with the two lines of patch 0075 around the rule application kept) usage: mkrc17.sh <SaturnExplorer dir> <build dir> <scratch dir>
# Builds the scratch action.cc object with the in-situ rule check (rulecheck14.hh) hooked into ActionPool::processOp, leaving the vendored tree alone.
se=$1; b=$2; out=$3
d=$se/FrontEnd/third_party/ghidra-decompiler/cpp
here=$(cd "$(dirname "$0")" && pwd)/../a2_round14   # rulecheck14.hh, wholefn14.hh
mkdir -p "$out"
python3 - "$d/action.cc" "$out/action_rc.cc" <<'PY'
import sys
s = open(sys.argv[1]).read()
s = s.replace('#include "coreaction.hh"\n', '#include "coreaction.hh"\n#ifdef SE_RULECHECK\n#include "rulecheck14.hh"\n#include "wholefn14.hh"\n#endif\n', 1)
old = "    rl->count_tests += 1;\n    data.setSlotSource(op);\t// SaturnExplorer (0075)\n    res = rl->applyOp(op,data);\n    data.clearSlotSource();\n"
new = ("    rl->count_tests += 1;\n#ifdef SE_RULECHECK\n"
       "    static const bool rcOn = getenv(\"SE_RULECHECK\") != 0;\n"
       "    static rc14::Snap rcPool[4]; static int rcDepth = 0;\n"
       "    struct RcGuard { int &d; RcGuard(int &x) : d(x) { ++d; } ~RcGuard() { --d; } } rcGuard(rcDepth);\n"
       "    rc14::Snap *rcsnap = (rcOn && rcDepth <= 4) ? &rcPool[rcDepth - 1] : 0;\n"
       "    if (rcsnap) rc14::snapshot(op,*rcsnap);\n"
       "    static const bool wfOn = getenv(\"SE_WHOLEFN\") != 0;\n"
       "    static wf14::State wfPool[4];\n"
       "    wf14::State *wfsnap = (wfOn && rcDepth <= 4) ? &wfPool[rcDepth - 1] : 0;\n"
       "    if (wfsnap) wf14::before(data,*wfsnap);\n#endif\n"
       "    data.setSlotSource(op);\t// SaturnExplorer (0075)\n"
       "    res = rl->applyOp(op,data);\n    data.clearSlotSource();\n#ifdef SE_RULECHECK\n    if (rcsnap) rc14::verify(rl->getName(),op,*rcsnap,res);\n"
       "    if (wfsnap) wf14::after(data,*wfsnap,rl->getName(),res,op->isDead() ? (const PcodeOp *)0 : op);\n#endif\n")
assert old in s
s = s.replace(old, new, 1)
# round 14: the leaf actions, whole function before and after (SE_WHOLEFN_ACTIONS=1): not groups and not pools (their rules are checked one by one); only once heritage has run
old2 = "      res = apply(data);\t// Start or continue action\n"
new2 = ("      {\n#ifdef SE_RULECHECK\n"
        "      static const bool wfAct = getenv(\"SE_WHOLEFN_ACTIONS\") != 0 && getenv(\"SE_WHOLEFN\") != 0;\n"
        "      wf14::State wfActState;\n"
        "      const bool wfLeaf = wfAct && data.getHeritagePass() >= 1 && !data.isHighOn() && data.getBasicBlocks().getSize() > 0 &&\n"
        "                          dynamic_cast<ActionGroup *>(this) == (ActionGroup *)0 && dynamic_cast<ActionPool *>(this) == (ActionPool *)0;\n"
        "      if (wfLeaf) wf14::before(data,wfActState);\n"
        "#endif\n"
        "      res = apply(data);\t// Start or continue action\n"
        "#ifdef SE_RULECHECK\n"
        "      if (wfLeaf) wf14::after(data,wfActState,\"act:\" + getName(),res < 0 ? 0 : 1,(const PcodeOp *)0);\n"
        "#endif\n      }\n")
assert old2 in s
s = s.replace(old2, new2, 1)
open(sys.argv[2], 'w').write(s)
PY
g++ -O2 -g -std=gnu++14 -DSE_RULECHECK -I"$d" -I"$here" -c "$out/action_rc.cc" -o "$out/action.cc.o" || exit 1
echo "built $out/action.cc.o"
