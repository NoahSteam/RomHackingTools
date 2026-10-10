import re, struct, sys
from capstone import Cs, CS_ARCH_SH, CS_MODE_SH2, CS_MODE_BIG_ENDIAN
hw = open(sys.argv[1],'rb').read(); BASE=0x06000000
md = Cs(CS_ARCH_SH, CS_MODE_SH2|CS_MODE_BIG_ENDIAN)
sleigh = {}
for line in open(sys.argv[2]):
    m = re.match(r';\s+([0-9A-Fa-f]+)\s+(\S+)\s*(.*)', line)
    if m: sleigh[int(m.group(1),16)] = (m.group(2), m.group(3))
def norm(mn, ops):
    mn = mn.lower().replace('/s','.s') ; ops = ops.lower()
    # unify: @(0xADDR,pc) vs 0xADDR ; 0x1 vs #0x1 vs #1 ; 44 vs 0x2c ; r15 vs sp
    ops = ops.replace('@(','(').replace(',pc)','')
    toks = re.findall(r'-?0x[0-9a-f]+|-?\d+|[a-z]+\d*', ops)
    out = []
    for t in toks:
        try: out.append(str(int(t,0)))
        except ValueError: out.append(t)
    return mn, out
mism = agree = 0
for a,(mn,ops) in sorted(sleigh.items()):
    ins = list(md.disasm(hw[a-BASE:a-BASE+2], a))
    if not ins: print(f"  {a:08X} capstone invalid vs sleigh {mn} {ops}"); mism += 1; continue
    c = norm(ins[0].mnemonic, ins[0].op_str); s = norm(mn, ops)
    if c == s: agree += 1
    else:
        mism += 1; print(f"  {a:08X} {struct.unpack_from('>H',hw,a-BASE)[0]:04X}  capstone: {ins[0].mnemonic} {ins[0].op_str:22s} sleigh: {mn} {ops}")
print(f"{agree} instructions agree, {mism} differ (of {len(sleigh)})")
