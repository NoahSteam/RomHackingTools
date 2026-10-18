#!/bin/bash
# usage: build_tool.sh <source.cpp> <output> <build dir of the tree> <SaturnExplorer dir>
# Links one of the audits in this directory against the engine objects of a build tree.
src=$1; out=$2; b=$3; se=$4
objs=$(ls $b/FrontEnd/third_party/ghidra-decompiler/CMakeFiles/SaturnExplorerGhidraDecomp.dir/cpp/*.o)
g++ -O2 -std=c++14 -Wno-format -I$se/FrontEnd/third_party/ghidra-decompiler/cpp "$src" $objs -lz -o "$out"
