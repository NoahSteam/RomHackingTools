#!/usr/bin/env python3
# usage: divexh_cmp14.py <old.bin> <new.bin>  -- the outcome characters of two engines side by side (see divexh14.cpp for the alphabet); a pair (old, new) per line with its count
import sys
from collections import Counter
a = open(sys.argv[1], 'rb').read(); b = open(sys.argv[2], 'rb').read()
print(len(a), len(b))
for k, v in sorted(Counter(zip(map(chr, a), map(chr, b))).items()): print(k, v)
