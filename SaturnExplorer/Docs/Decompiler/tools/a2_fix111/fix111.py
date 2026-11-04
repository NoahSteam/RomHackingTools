#!/usr/bin/env python3
# usage: fix111.py [replay]   -> the fixtures of the area 2 fixer (A2-111 to A2-115), one per line as "name: words" (SE_GENERATED_PRINTEDC_WORDS). With "replay" the shell line for each.
# A2-111, A2-113: pinned in kPrintedCFixtures (patch 0077); A2-114: the pin 'A2-114 #0' / '#1'; A2-115 is a seed (SE_GENERATED_GRAMMAR=2 SE_GENERATED_FIRST=4515655 SE_GENERATED_SEEDS=1).
FIX = [
    ("A2-111 a",  "6153 611E 611F D304 3130 0229 D303 3316 0029 3028 000B 0009 FFFF FFFF 0000 0080", "agrees; text has `param_2 == -1`"),
    ("A2-111 b",  "6163 611E 611F D305 3136 0229 D304 3310 0029 202B 8900 600B 000B 0009 0000 7FFF FFFF FF80", "agrees; text has `param_3 != -0x80`"),
    ("A2-113 a",  "6153 611C 611B 611F 611F 6273 622F 3122 0029 000B 0009 0009", "agrees; text has `(uint2)-(param_2 & 0xff)`"),
    ("A2-113 b",  "6153 611D 6263 622C 622B 622D 3127 0029 000B 0009", "agrees; text has `(uint2)-(param_3 & 0xff) < param_2`"),
    ("A2-114 a",  "2576 000B 0009 0009", "skipped:memory (was crashed)"),
    ("A2-114 b",  "6576 000B 0009 0009", "skipped:memory"),
]
import sys
for name, words, want in FIX:
    if len(sys.argv) > 1 and sys.argv[1] == 'replay':
        print('# %s (%s)\nSE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="%s" SaturnExplorerSh2GeneratedTests <scratch>' % (name, want, words))
    else:
        print('%s: %s' % (name, words))
if len(sys.argv) > 1 and sys.argv[1] == 'replay':
    print('# A2-115 (was a segmentation fault)\nSE_GENERATED_GRAMMAR=2 SE_GENERATED_FIRST=4515655 SE_GENERATED_SEEDS=1 SaturnExplorerSh2GeneratedTests <scratch>')
