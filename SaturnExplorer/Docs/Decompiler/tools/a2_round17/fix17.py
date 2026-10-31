#!/usr/bin/env python3
# usage: fix17.py   -> fixtures of the round 17 review as SE_GENERATED_PRINTEDC_WORDS lines (one per line, "name: words")
import importlib.util, os, sys
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("dagen17", os.path.join(here, "dagen17.py"))
d = importlib.util.module_from_spec(spec); spec.loader.exec_module(d)

def mk(name, words):
    r = d.random.Random(1)
    f = d.Fn(r)
    for w in words: (f.lit(*w[1:]) if isinstance(w, tuple) else f.add(w))
    f.add(0x000B, 0x0009)
    print("%s: %s" % (name, d.finish(f)))

M = d.mov_
# A2-110: a narrow value cut by a signed extension that the printer writes as a cast, against a constant of the other sign
for nm, cuts, c in [("sx.b+sx.w == -1", [d.extsb(1, 1), d.extsw(1, 1)], 0xFFFFFFFF), ("sx.b+sx.w == -128", [d.extsb(1, 1), d.extsw(1, 1)], 0xFFFFFF80),
                    ("zx.b+sx.w == 0xff", [d.extub(1, 1), d.extsw(1, 1)], 0xFF), ("sx.w == -1", [d.extsw(1, 1)], 0xFFFFFFFF), ("sx.b == -1", [d.extsb(1, 1)], 0xFFFFFFFF)]:
    mk("A2-110 " + nm, [M(1, 4)] + cuts + [("lit", 3, c), d.cmpeq(3, 1), d.movt(0)])
# A2-111: a narrow named variable against a narrow negated expression (the compare is two bytes wide in the engine, C widens both first)
mk("A2-111 uint2 <  -(uint2 & 0xff)", [M(1, 4), d.extuw(1, 1), M(2, 7), d.extub(2, 2), d.neg_(2, 2), d.extuw(2, 2), d.cmpgt(2, 1), d.movt(0)])
