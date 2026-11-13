#!/bin/sh
# Rewrites patches/0094 as the difference between master (the base commit given as $1, default the merge base with master)
# and the working tree, for the three files the patch touches. Run from the repository root.
base=${1:-79d907f7}
D=SaturnExplorer/FrontEnd/third_party/ghidra-decompiler
C=Ghidra/Features/Decompiler/src/decompile/cpp
out=$D/patches/0094-engine-forced-write-and-pushed-join.patch
: > "$out.new"
for f in coreaction.cc funcdata_block.cc ruleaction.cc; do
  git diff "$base" -- "$D/cpp/$f" | grep -v '^index ' |
    sed -e "s#^diff --git a/.* b/.*#diff --git a/$C/$f b/$C/$f#" -e "s#^--- a/.*#--- a/$C/$f#" -e "s#^+++ b/.*#+++ b/$C/$f#" >> "$out.new"
done
mv "$out.new" "$out"
