# xcount20.py log... : narrow-c and agreeing texts in which an xunknown local or field is read by an ordered compare or a right shift with no cast
import re, sys
pat = re.compile(r'(?<![\)\w])(xStack_\w+(?:\._\d+_\d+_)?)\s*(<=|<|>>|>=|>)(?!=)|(<=|<|>=|>)\s*(xStack_\w+(?:\._\d+_\d+_)?)(?!\w)')
for fn in sys.argv[1:]:
    lines = open(fn).read().split('\n')
    blocks = []; i = 0
    while i < len(lines):
        m = re.search(r'\[(narrow-c|agrees|mismatch)\]', lines[i])
        if m:
            j = i + 1; body = []
            while j < len(lines) and lines[j] != '}': body.append(lines[j]); j += 1
            blocks.append((lines[i], m.group(1), body)); i = j
        i += 1
    hits = [(h, c, b) for h, c, b in blocks if any(pat.search(l) and not re.search(r'xunknown\d xStack|^\s*xStack\w* = ', l) for l in b)]
    print(fn, 'blocks', len(blocks), 'narrow-c', sum(1 for b in blocks if b[1] == 'narrow-c'), 'xunknown read by an ordered compare or shift:', len(hits))
    for h, c, b in hits[:6]:
        print('  ', h[:110]); print('     ', [l.strip() for l in b if pat.search(l)][:2])
