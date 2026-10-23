#!/bin/bash
# usage: mutbuild13.sh <tool.cpp> <output> <build dir> <SaturnExplorer dir> <mutated ruleaction.cc> [<mutated rangeutil.cc>]
# Like build13.sh, but with the given copies of ruleaction.cc (and rangeutil.cc) compiled in place of the engine's.
src=$1; out=$2; b=$3; se=$4; ra=$5; ru=$6
d=$se/FrontEnd/third_party/ghidra-decompiler/cpp
objdir=$b/FrontEnd/third_party/ghidra-decompiler/CMakeFiles/SaturnExplorerGhidraDecomp.dir/cpp
tmp=$(mktemp -d)
g++ -O2 -std=gnu++14 -I$d -c "$ra" -o $tmp/ruleaction.cc.o || exit 1
extra=$tmp/ruleaction.cc.o
skip="ruleaction.cc.o"
if [ -n "$ru" ]; then g++ -O2 -std=gnu++14 -I$d -c "$ru" -o $tmp/rangeutil.cc.o || exit 1; extra="$extra $tmp/rangeutil.cc.o"; skip="$skip|rangeutil.cc.o"; fi
objs=$(ls $objdir/*.o | grep -Ev "/($skip)\$")
g++ -O2 -std=gnu++14 -Wno-format -I$se/FrontEnd/src -I$d -I$se/FrontEnd/third_party/zlib "$src" $se/FrontEnd/src/Decompiler/SpecBundle.cpp $se/FrontEnd/src/Sha256.cpp $se/FrontEnd/src/FileWrite.cpp $objs $extra $b/lib/libSaturnExplorerZlib.a -lpthread -o "$out"
rm -rf $tmp
