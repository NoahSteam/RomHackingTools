#!/usr/bin/env python3
# usage: fix17.py [replay]   -> the fixtures of the round 17 review, one per line as "name: words" (SE_GENERATED_PRINTEDC_WORDS). With "replay" it prints the shell line for each.
# A2-111 (patch 0071's gap: a signed narrow side printed as a cast against a constant of the other sign), A2-113 (a narrow named variable against a negated narrow expression),
# A2-114 (a store through a parameter that the printed-C differential calls crashed), and the pins of the big-endian reading (A2-110, SE_GENERATED_PRINTEDC_BE=1 with _PTR=4).
FIX = [
    ("A2-111 a",  "6153 611E 611F D304 3130 0229 D303 3316 0029 3028 000B 0009 FFFF FFFF 0000 0080", ""),
    ("A2-111 b",  "6163 611E 611F D305 3136 0229 D304 3310 0029 202B 8900 600B 000B 0009 0000 7FFF FFFF FF80", ""),
    ("A2-113 a",  "6153 611C 611B 611F 611F 6273 622F 3122 0029 000B 0009 0009", ""),
    ("A2-113 b",  "6153 611D 6263 622C 622B 622D 3127 0029 000B 0009", ""),
    ("A2-114",    "2576 000B 0009 0009", ""),
    ("A2-110 a (word stored, byte loaded)",     "2462 6040 000B 0009", "SE_GENERATED_PRINTEDC_BE=1 SE_GENERATED_PRINTEDC_PTR=4 "),
    ("A2-110 b (half-word stored, word loaded)", "2461 6042 000B 0009", "SE_GENERATED_PRINTEDC_BE=1 SE_GENERATED_PRINTEDC_PTR=4 "),
    ("A2-110 d (pre-decrement chain of three widths)", "2466 2475 2474 6144 6245 6346 312C 313C 6013 000B 0009", "SE_GENERATED_PRINTEDC_BE=1 SE_GENERATED_PRINTEDC_PTR=4 "),
]
import sys
for name, words, env in FIX:
    if len(sys.argv) > 1 and sys.argv[1] == 'replay':
        print('# %s\n%sSE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="%s" SaturnExplorerSh2GeneratedTests <scratch>' % (name, env, words))
    else:
        print('%s: %s' % (name, words))
