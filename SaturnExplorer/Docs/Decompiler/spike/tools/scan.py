import struct, sys
from capstone import Cs, CS_ARCH_SH, CS_MODE_SH2, CS_MODE_BIG_ENDIAN
hw = open(sys.argv[1],'rb').read(); BASE=0x06000000
md = Cs(CS_ARCH_SH, CS_MODE_SH2|CS_MODE_BIG_ENDIAN)
def w(a): return struct.unpack_from('>H', hw, a-BASE)[0]
def dis(a):
    ins = list(md.disasm(hw[a-BASE:a-BASE+2], a))
    return f"{ins[0].mnemonic} {ins[0].op_str}" if ins else "<invalid>"
for target in [int(x,16) for x in sys.argv[2:]]:
    # walk back to the nearest rts (000B); the entry candidate is 4 bytes after it (rts + delay slot)
    a = target
    while w(a) != 0x000B: a -= 2
    entry = a + 4
    while w(entry) == 0x0009: entry += 2  # skip nop padding
    print(f"--- candidate function containing {target:08X}: entry {entry:08X}")
    a = entry; n = 0
    while n < 200:
        print(f"  {a:08X}  {w(a):04X}  {dis(a)}")
        if w(a) == 0x000B: print(f"  {a+2:08X}  {w(a+2):04X}  {dis(a+2)}   ; delay slot"); break
        a += 2; n += 1
