#!/bin/bash
cd /home/user/RomHackingTools/.claude/worktrees/agent-a285aa6983641c17d
python3 SaturnExplorer/Docs/Decompiler/tools/a10_round4/mut.py . build /tmp/claude-0/s/hunks /tmp/claude-0/s/outA /tmp/claude-0/s/jobs_A.json > /tmp/claude-0/s/runA.log 2>&1
echo finished >> /tmp/claude-0/s/runA.log
