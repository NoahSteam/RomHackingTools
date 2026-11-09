# Coordinator state (rebuilt 2026-11-09 after a container restart wiped the scratchpad)
Rules: 3 concurrent agents; Sonnet unless complex then Opus, alternating; keep the queue moving.
After every merge: ledger check, check-literal-size.py, patch lint. Push master only after a green matrix.
Matrix: cd /home/user/RomHackingTools; ( ulimit -v 6000000; VENDOR=1 $S/matrix.sh $S/mNNN ) > $S/mNNN.log 2>&1
Expected: ctest all passed (111) + Live 6/6; fault 0 of 94; failing seeds=0; findings=0 x3; agrees=5196;
  missing and said 0 and 4; real entries 999: 474 (645) and 1543: 724 (975); BUILD_OK; VENDOR_DIFF_LINES 0; ALL_DONE.
Corpus: h/hwram007.bin, h/hwram008.bin extracted from SakuraTaisen2/.../Yabause/OhrimeMiniGame/GS-9169_00{7,8}.yss (yss_hwram.py).
Backups: master -> origin backup/master-latest; agents push backup/<branch>; coordinator tools -> origin backup/coord-tools.
Windows bot: session_0185U2n9qYPKsm6n5C9UjCTF (console only). Next verify: confirm A12-29 and A12-5 (engine /W3 warnings 0).
Patch numbers: through 0088 vendored. 0089 A3-175 fixer; 0090 A5 fixer (0093+ if more); 0091 area 7 r12; 0092 free.

## Log
- restart: master restored to backup/master-latest 43e945b2 (69 commits ahead of origin/master de0ddde; not pushed: 4123 red in m303).
- relaunched: A5-114..118 fixer (opus, a8r4, review/5-a5114-fix); A3-175/172 fixer (opus, a7fix22, review/3-a3175-fix); area 7 r12 (sonnet, a11mut, review/7-round12). All from 43e945b2.
- Queue after: area 3 round 22 review (after A3-175); area 5 review round (after fixer); area 7 further rounds if seeds remain; area 10 round 3 if needed; Windows verify after push.
