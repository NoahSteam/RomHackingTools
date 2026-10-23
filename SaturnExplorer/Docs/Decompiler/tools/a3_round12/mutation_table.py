#!/usr/bin/env python3
"""mutation_table.py <run_mut OUT dir> : the markdown table of run_mut.sh's results (the .res files) and run_pins.sh's (the .pins files)."""
import sys, os, re, glob
D = os.path.join(sys.argv[1], 'mut')
WHAT = {
 'E1_bitwise': 'engine: AND, OR, XOR of a frame address taken as modeled (no escape)',
 'E2_pieces': 'engine: SUBPIECE, PIECE, ZEXT, SEXT taken as modeled',
 'E3_negate': 'engine: INT_2COMP, INT_NEGATE taken as modeled',
 'E4_shifts': 'engine: INT_LEFT, INT_RIGHT, INT_SRIGHT, INT_MULT taken as modeled',
 'E5_memoryescape': 'engine: (b), a frame address stored to memory, no longer escapes',
 'E6_join': 'engine: (c), a join with a non-frame value, no longer unmodeled',
 'E7_callout': 'engine: (d), the output of a call handed a frame address, no longer unmodeled',
 'E9_widen': 'engine: `widenEscapedStoreGuards` off',
 'E10_markstores': 'engine: `markEscapedStores` marks nothing',
 'E11_lowest': 'engine: the lowest frame offset ignores the function\'s stack varnodes',
 'E13_freestack': 'engine: the pre-heritage free stack reads are no members',
 'E17_propagate': 'engine: an unmodeled value no longer makes the values computed from it unmodeled',
 'E18_loaduse': 'engine: a LOAD through an unmodeled address no longer escapes',
 'E19_calluse': 'engine: a CALLOTHER handed an unmodeled address no longer escapes (CALL and CALLIND kept)',
 'H1_store_below': 'host: `Store` records no `storesBelow` (tracked and laundered)',
 'H2_store_laundered': 'host: `Store` records none for a laundered pointer (A3-83)',
 'H3_transitive': 'host: `Call` does not carry a lent callee\'s `storesBelow` into the caller\'s (A3-82)',
 'H4_exposure_exact': 'host: a lent callee\'s `storesBelow` register exposes the frame only for an exact address (A3-84)',
 'H5_load_taint': 'host: `Load` keeps no parameter provenance (A3-83 c, g)',
 'H6b_tail_jump': 'host: a tail jump in a walked callee carries no `storesBelow`',
 'H7_probe_below': 'host: the probe reads no `storesBelow` of an unlent callee (A3-86 a)',
 'H8_possibleframe': 'host: `PossibleFrame` is false',
 'H9_leak_escaped': 'host: a callee\'s leak of a frame address sets no `State::escaped`',
}
rows = []
for res in sorted(glob.glob(os.path.join(D, '*.res'))):
    n = os.path.basename(res)[:-4]
    t = open(res).read().strip()
    m = re.match(r'[^:]*: printed-C findings (\d+), grammar 2 findings (\S+);(.*)', t)
    if not m:
        rows.append((n, t, '', '')); continue
    pc, g2, rest = m.group(1), m.group(2), m.group(3).strip()
    pins = ''
    pf = os.path.join(D, n + '.pins')
    if os.path.exists(pf): pins = open(pf).read().strip().split(': ', 1)[-1]
    rows.append((n, pc, g2, pins))
print('| Reverted | Printed-C failing findings (3,000 seeds) | Grammar 2 (3,000) | The suite, when the generator did not fail |')
print('|---|---|---|---|')
for n, pc, g2, pins in sorted(rows, key=lambda r: (r[0][0], int(re.match(r'[EH](\d+)', r[0]).group(1)))):
    print('| %s | %s | %s | %s |' % (WHAT.get(n, n), pc, g2 if g2 != '-' else 'not run', pins or ('' if pc != '0' else 'not run')))
