#!/usr/bin/env python3
# Area 2 round 9 instrument (A2-59): shrink a word list that the engine does not finish by replacing words with nop while it still does not.
# usage: GEN=<SaturnExplorerSh2GeneratedTests> ddmin_hang.py "<words>" [scratch dir]
import os, subprocess, sys
gen = os.environ["GEN"]; scratch = sys.argv[2] if len(sys.argv) > 2 else "/tmp/ddmin_hang"
def hangs(words, timeout=8):
    env = dict(os.environ, SE_GENERATED_PRINTEDC="only", SE_GENERATED_PRINTEDC_WORDS=" ".join(words))
    try:
        subprocess.run([gen, scratch], env=env, capture_output=True, timeout=timeout)
        return False
    except subprocess.TimeoutExpired:
        return True
w = sys.argv[1].split()
assert hangs(w), "the engine finishes"
again = True
while again:
    again = False
    for i in range(len(w)):
        if w[i] == "0009": continue
        t = list(w); t[i] = "0009"
        if hangs(t): w = t; again = True
print(" ".join(w))
