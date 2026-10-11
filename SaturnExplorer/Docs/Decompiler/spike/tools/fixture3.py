# Fixture 3 (PLAN.md A3): a PC-relative load in the delay slot of a taken branch.
#
# On SH-2 the slot instruction of a taken delayed branch reads PC as the branch target + 2, so
# mov.l @(disp,PC) there loads from ((target + 2) & ~3) + disp, not from its own
# ((slot + 4) & ~3) + disp. Ghidra's SLEIGH spec computes the latter. Each case puts DIFFERENT
# literals at the two candidate addresses, so a decompiler that folds either one is caught.
#
# Encodings from the Hitachi SH-1/SH-2 Programming Manual opcode tables, checked here with
# Capstone's SH module (shares no code with Ghidra's SLEIGH). Sh2DecompilerTests.cpp embeds the
# same words; run this to re-verify them:
#   python3 -I fixture3.py
import struct
from capstone import Cs, CS_ARCH_SH, CS_MODE_SH2, CS_MODE_BIG_ENDIAN

DIRECT = 0x11111111   # at the address the spec computes
SLOT = 0x22222222     # at the address the hardware reads

def pcrel_long(pc, disp):
    return (pc & ~3) + disp

cases = []

# bra: always taken.
B = 0x06006000
bra = [
    (B + 0x00, 0xA002, "bra 0x06006008"),          # 1010 dddddddddddd  target = 0x00+4+2*2
    (B + 0x02, 0xD103, "mov.l @(12,pc),r1"),       #   slot: 1101 nnnn dddddddd  disp = 3*4
    (B + 0x04, 0x0009, "nop"),
    (B + 0x06, 0x0009, "nop"),
    (B + 0x08, 0x000B, "rts"),                     # <- branch target
    (B + 0x0A, 0x6013, "mov r1,r0"),               #   slot: 0110 nnnn mmmm 0011  n=0 m=1
    (B + 0x0C, 0x0009, "nop"),
    (B + 0x0E, 0x0009, "nop"),
]
cases.append(("bra", B, bra, B + 0x02, B + 0x08, 12))

# bf/s: the slot runs whichever way the branch goes; taken, it sees the target's PC.
C = 0x06006100
bfs = [
    (C + 0x00, 0x4415, "cmp/pl r4"),
    (C + 0x02, 0x8F02, "bf/s 0x0600610A"),         # 1000 1111 dddddddd  target = 0x02+4+2*2
    (C + 0x04, 0xD102, "mov.l @(8,pc),r1"),        #   slot, disp = 2*4
    (C + 0x06, 0x7101, "add #1,r1"),
    (C + 0x08, 0x0009, "nop"),
    (C + 0x0A, 0x000B, "rts"),                     # <- branch target
    (C + 0x0C, 0x6013, "mov r1,r0"),               #   slot
    (C + 0x0E, 0x0009, "nop"),
]
cases.append(("bf/s", C, bfs, C + 0x04, C + 0x0A, 8))

md = Cs(CS_ARCH_SH, CS_MODE_SH2 | CS_MODE_BIG_ENDIAN)
ok = True
for name, base, words, slot, target, disp in cases:
    direct = pcrel_long(slot + 4, disp)
    slotaddr = pcrel_long(target + 2, disp)
    assert direct != slotaddr
    print(f"{name} @{base:08X} (capstone SH2 BE):")
    for addr, w, intent in words:
        ins = list(md.disasm(struct.pack(">H", w), addr))
        got = f"{ins[0].mnemonic} {ins[0].op_str}".strip() if ins else "<capstone: invalid>"
        print(f"  {addr:08X}  {w:04X}  {got:28s} ; intended: {intent}")
        if not ins: ok = False
    print(f"  {direct:08X}  {DIRECT:08X}  .long  ; the slot's own PC-relative address (spec)")
    print(f"  {slotaddr:08X}  {SLOT:08X}  .long  ; target+2 relative (hardware, branch taken)")
    end = base + 2 * len(words)
    assert direct == end and slotaddr == end + 4, "literals must follow the code"
print("all encodings decode" if ok else "SOME ENCODINGS DID NOT DECODE")
