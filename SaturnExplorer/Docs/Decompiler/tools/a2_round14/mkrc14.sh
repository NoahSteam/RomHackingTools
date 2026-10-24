#!/bin/bash
# usage: mkrc14.sh <SaturnExplorer dir> <build dir> <scratch dir>
# Builds the scratch action.cc object with the in-situ rule check (rulecheck14.hh) hooked into ActionPool::processOp, leaving the vendored tree alone.
se=$1; b=$2; out=$3
d=$se/FrontEnd/third_party/ghidra-decompiler/cpp
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
python3 - "$d/action.cc" "$out/action_rc.cc" <<'PY'
import sys
s = open(sys.argv[1]).read()
s = s.replace('#include "coreaction.hh"\n', '#include "coreaction.hh"\n#ifdef SE_RULECHECK\n#include "rulecheck14.hh"\n#include "wholefn14.hh"\n#endif\n', 1)
old = "    rl->count_tests += 1;\n    res = rl->applyOp(op,data);\n"
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
       "    res = rl->applyOp(op,data);\n#ifdef SE_RULECHECK\n    if (rcsnap) rc14::verify(rl->getName(),op,*rcsnap,res);\n"
       "    if (wfsnap) wf14::after(data,*wfsnap,rl->getName(),res,op->isDead() ? (const PcodeOp *)0 : op);\n#endif\n")
assert old in s
s = s.replace(old, new, 1)
open(sys.argv[2], 'w').write(s)
PY
g++ -O2 -g -std=gnu++14 -DSE_RULECHECK -I"$d" -I"$here" -c "$out/action_rc.cc" -o "$out/action.cc.o" || exit 1
echo "built $out/action.cc.o"
