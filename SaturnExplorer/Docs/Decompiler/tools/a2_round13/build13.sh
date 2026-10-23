#!/bin/bash
# usage: build13.sh <source.cpp> <output> <build dir of the tree> <SaturnExplorer dir>
# Links a round 13 tool against the engine objects of a build tree (the host files of SpecBundle are compiled in).
src=$1; out=$2; b=$3; se=$4
objs=$(ls $b/FrontEnd/third_party/ghidra-decompiler/CMakeFiles/SaturnExplorerGhidraDecomp.dir/cpp/*.o)
gen=$(find $b -name 'Sh2SpecData.h' -printf '%h\n' | head -1)
inc="-I$se/FrontEnd/src -I$se/FrontEnd/third_party/ghidra-decompiler/cpp -I$se/FrontEnd/third_party/zlib"
[ -n "$gen" ] && inc="$inc -I$gen -I$gen/.."
g++ -O2 -std=gnu++14 -Wno-format $inc "$src" $se/FrontEnd/src/Decompiler/SpecBundle.cpp $se/FrontEnd/src/Sha256.cpp $se/FrontEnd/src/FileWrite.cpp $objs $b/lib/libSaturnExplorerZlib.a -lpthread -o "$out"
