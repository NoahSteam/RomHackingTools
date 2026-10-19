#!/usr/bin/env python3
# Area 2 round 12: RuleScarry (ruleaction.cc) against the arithmetic it claims, every operand pair at one byte.
# The rule rewrites, for a constant c (the other operand a):
#   scarry(a,c) != (a + c s< 0)  ->  a s< -c           scarry(a,c) != (0 s< a + c)  ->  -c s< a
#   scarry(a,c) == (0 s< a + c)  ->  a s<= -c          scarry(a,c) == (a + c s< 0)  ->  -c s<= a
# It refuses c = the minimum only when it swapped the operands itself (a constant first operand); a constant second operand reaches the rewrite.
B = 8; M = (1 << B) - 1; MIN = 1 << (B - 1)
sx = lambda v: v - (1 << B) if v & MIN else v
bad = {}
for c in range(1 << B):
    nc = (-c) & M
    for a in range(1 << B):
        s = (a + c) & M
        sc = int(not (-MIN <= sx(a) + sx(c) < MIN))
        forms = {
            'scarry != (sum s< 0) -> a s< -c':  (sc != int(sx(s) < 0), int(sx(a) < sx(nc))),
            'scarry != (0 s< sum) -> -c s< a':  (sc != int(0 < sx(s)), int(sx(nc) < sx(a))),
            'scarry == (0 s< sum) -> a s<= -c': (sc == int(0 < sx(s)), int(sx(a) <= sx(nc))),
            'scarry == (sum s< 0) -> -c s<= a': (sc == int(sx(s) < 0), int(sx(nc) <= sx(a))),
        }
        for k, (truth, rule) in forms.items():
            if int(truth) != rule: bad.setdefault((k, c), 0); bad[(k, c)] += 1
cs = sorted({c for (_, c) in bad})
print("constants c for which a rewrite is wrong:", [hex(c) for c in cs], "(the minimum is %s)" % hex(MIN))
for (k, c), n in sorted(bad.items()): print("  %-34s c=%s: %d of 256 values of a" % (k, hex(c), n))
