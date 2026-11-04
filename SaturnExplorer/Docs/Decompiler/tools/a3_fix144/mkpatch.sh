#!/bin/bash
# (area 3 fixer, A3-144 to A3-153) paths are the fixer's scratch directory; set S to your own
# builds patches/0076 from d88e06e1's cpp files (a) and the worktree's (b)
set -e
S=/tmp/claude-0/-home-user-RomHackingTools/aae0ae36-3afa-5288-8786-5063bd965426/scratchpad/a3fix144
W=/home/user/RomHackingTools/.claude/worktrees/a3fix144
CPP=SaturnExplorer/FrontEnd/third_party/ghidra-decompiler/cpp
D=$S/mk76
U=Ghidra/Features/Decompiler/src/decompile/cpp
rm -rf $D && mkdir -p $D/a/$U $D/b/$U
cd $W
for f in $(git diff --name-only d88e06e1 -- $CPP | xargs -n1 basename); do
  git show d88e06e1:$CPP/$f > $D/a/$U/$f
  cp $W/$CPP/$f $D/b/$U/$f
done
cd $D
export GIT_CEILING_DIRECTORIES=$S
git diff --no-index a b > raw.patch || true
sed -e 's#a/a/#a/#g' -e 's#b/b/#b/#g' -e '/^index /d' raw.patch > 0076-engine-frame-loads-keep-writes.patch
grep -c '^diff --git' 0076-engine-frame-loads-keep-writes.patch
cp 0076-engine-frame-loads-keep-writes.patch $W/SaturnExplorer/FrontEnd/third_party/ghidra-decompiler/patches/
