#!/bin/bash
# usage: relink13.sh <build dir> <target (e.g. SaturnExplorerSh2GeneratedTests)> <replacement action.cc.o> <output exe> [<engine object name>=<replacement object> ...]
# Relinks a CMake test target with the scratch action.cc object in place of the engine's (and any further engine object, e.g. ruleaction.cc.o=<mutant>).
b=$1; t=$2; obj=$3; out=$4; shift 4
cd "$b" || exit 1
link=$(cat CMakeFiles/$t.dir/link.txt)
pre="FrontEnd/third_party/ghidra-decompiler/CMakeFiles/SaturnExplorerGhidraDecomp.dir/cpp"
link=$(echo "$link" | sed -E "s#\"?$pre/action.cc.o\"?#$obj#; s#-o bin/$t#-o $out#")
for kv in "$@"; do
  name=${kv%%=*}; rep=${kv#*=}
  link=$(echo "$link" | sed -E "s#\"?$pre/$name\"?#$rep#")
done
eval "$link"
