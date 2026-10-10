# Hand-assembled SH-2 fixture. Encodings taken from the Hitachi SH-1/SH-2 Programming Manual
# opcode tables (Docs/Saturn/SH-1_SH-2_CPU_Core_Architecture.pdf) and independently checked
# below with Capstone's SH module (not derived from Ghidra's SLEIGH).
import struct, sys
from capstone import Cs, CS_ARCH_SH, CS_MODE_SH2, CS_MODE_BIG_ENDIAN
BASE = 0x06004000
caller = [
 (0x2FE6, "mov.l r14,@-r15"),       # 0010 nnnn mmmm 0110  n=15 m=14
 (0x4F22, "sts.l pr,@-r15"),        # 0100 nnnn 0010 0010  n=15
 (0x6E43, "mov r4,r14"),            # 0110 nnnn mmmm 0011  n=14 m=4
 (0xE000, "mov #0,r0"),             # 1110 nnnn iiiiiiii
 (0x4415, "cmp/pl r4"),             # 0100 nnnn 0001 0101  n=4
 (0x8B03, "bf 0x06004014"),         # 1000 1011 dddddddd  target = 0x0600400A+4+3*2
 (0xD003, "mov.l @(12,pc),r0"),     # 1101 nnnn dddddddd  lit = (0x0600400C&~3)+4+3*4 = 0x0600401C
 (0x400B, "jsr @r0"),               # 0100 nnnn 0000 1011
 (0x64E3, "mov r14,r4"),            #   delay slot: 0110 nnnn mmmm 0011 n=4 m=14
 (0x7001, "add #1,r0"),             # 0111 nnnn iiiiiiii
 (0x4F26, "lds.l @r15+,pr"),        # 0100 mmmm 0010 0110  m=15     <- 0x06004014
 (0x000B, "rts"),
 (0x6EF6, "mov.l @r15+,r14"),       #   delay slot: 0110 nnnn mmmm 0110 n=14 m=15
 (0x0009, "nop"),                   # pad to align the literal
]
CALLEE = 0x06005000
LIT = int(sys.argv[2],16) if len(sys.argv)>2 else CALLEE
callee = [
 (0x6042, "mov.l @r4,r0"),          # 0110 nnnn mmmm 0010 n=0 m=4
 (0x000B, "rts"),
 (0x7002, "add #2,r0"),             #   delay slot
]
img = bytearray(0x2000)  # 0x06004000..0x06005FFF
off = 0
for w,_ in caller: struct.pack_into(">H", img, off, w); off += 2
assert BASE+off == 0x0600401C
struct.pack_into(">I", img, off, LIT)
coff = CALLEE-BASE
for w,_ in callee: struct.pack_into(">H", img, coff, w); coff += 2
open(sys.argv[1], "wb").write(img)
md = Cs(CS_ARCH_SH, CS_MODE_SH2 | CS_MODE_BIG_ENDIAN)
def listing(addr, words):
    for i,(w,intent) in enumerate(words):
        b = struct.pack(">H", w)
        ins = list(md.disasm(b, addr+2*i))
        got = f"{ins[0].mnemonic} {ins[0].op_str}".strip() if ins else "<capstone: invalid>"
        print(f"  {addr+2*i:08X}  {w:04X}  {got:28s} ; intended: {intent}")
print("caller @%08X (capstone SH2 BE):" % BASE); listing(BASE, caller)
print("  %08X  %08X  .long (literal -> callee)" % (0x0600401C, CALLEE))
print("callee @%08X:" % CALLEE); listing(CALLEE, callee)
