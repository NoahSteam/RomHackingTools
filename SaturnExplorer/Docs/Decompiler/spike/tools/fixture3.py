# Fixture 3 (PLAN.md A3): PC-relative loads and mova in delay slots.
#
# On SH-2 the slot instruction of a taken delayed branch reads PC as the branch target + 2, so
# mov.l @(disp,PC) there loads from ((target + 2) & ~3) + disp, not from its own
# ((slot + 4) & ~3) + disp. Ghidra's SLEIGH spec computes the latter. The bra and bf/s cases put
# DIFFERENT literals at the two candidate addresses, so a decompiler that folds either one is
# caught; the others cover every other delayed-branch kind (rts, bsr, jmp, braf) and every
# PC-relative form (mov.w, mov.l, mova) in a slot, which Step 2 must flag as unsupported.
#
# Encodings from the Hitachi SH-1/SH-2 Programming Manual opcode tables. This script checks each
# one with Capstone's SH module (which shares no code with Ghidra's SLEIGH): the whole decoded
# instruction -- mnemonic, registers, immediates, branch targets, PC-relative addresses -- must be
# the intended one, and a PC-relative slot instruction's address must be the one the test expects
# the spec to load. It exits non-zero on any disagreement.
# Sh2DecompilerTests.cpp (DecompilerTestSupport.h, PlaceFixture3) embeds the same words.
#   python3 -I fixture3.py
import re, struct, sys
from capstone import Cs, CS_ARCH_SH, CS_MODE_SH2, CS_MODE_BIG_ENDIAN

DIRECT = 0x11111111   # at the address the spec computes
SLOT = 0x22222222     # at the address the hardware reads (branch taken)

# (name, base, [(addr, word, intended mnemonic, intended text)], slot address, slot's own-PC
#  literal address or None for mova (no load), literals {addr: (value, size)})
cases = []

B = 0x06006000
cases.append(("bra", B, [
    (B + 0x00, 0xA002, "bra", "bra 0x06006008"),
    (B + 0x02, 0xD103, "mov.l", "mov.l @(12,pc),r1    ; slot"),
    (B + 0x04, 0x0009, "nop", "nop"),
    (B + 0x06, 0x0009, "nop", "nop"),
    (B + 0x08, 0x000B, "rts", "rts                  ; <- target"),
    (B + 0x0A, 0x6013, "mov", "mov r1,r0            ; slot"),
    (B + 0x0C, 0x0009, "nop", "nop"),
    (B + 0x0E, 0x0009, "nop", "nop"),
], B + 0x02, B + 0x10, {B + 0x10: (DIRECT, 4), B + 0x14: (SLOT, 4)}))

C = 0x06006100
cases.append(("bf/s", C, [
    (C + 0x00, 0x4415, "cmp/pl", "cmp/pl r4"),
    (C + 0x02, 0x8F02, "bf/s", "bf/s 0x0600610A"),
    (C + 0x04, 0xD102, "mov.l", "mov.l @(8,pc),r1     ; slot"),
    (C + 0x06, 0x7101, "add", "add #1,r1"),
    (C + 0x08, 0x0009, "nop", "nop"),
    (C + 0x0A, 0x000B, "rts", "rts                  ; <- target"),
    (C + 0x0C, 0x6013, "mov", "mov r1,r0            ; slot"),
    (C + 0x0E, 0x0009, "nop", "nop"),
], C + 0x04, C + 0x10, {C + 0x10: (DIRECT, 4), C + 0x14: (SLOT, 4)}))

D = 0x06006200
cases.append(("rts + mov.w", D, [
    (D + 0x00, 0x000B, "rts", "rts"),
    (D + 0x02, 0x9001, "mov.w", "mov.w @(2,pc),r0     ; slot"),
    (D + 0x04, 0x0009, "nop", "nop"),
    (D + 0x06, 0x0009, "nop", "nop"),
], D + 0x02, D + 0x08, {D + 0x08: (0x1234, 2)}))

E = 0x06006300
cases.append(("bsr + mova", E, [
    (E + 0x00, 0xB002, "bsr", "bsr 0x06006308"),
    (E + 0x02, 0xC701, "mova", "mova @(4,pc),r0      ; slot"),
    (E + 0x04, 0x000B, "rts", "rts"),
    (E + 0x06, 0x0009, "nop", "nop"),
    (E + 0x08, 0x000B, "rts", "rts                  ; the bsr's target"),
    (E + 0x0A, 0x0009, "nop", "nop"),
], E + 0x02, None, {}))

F = 0x06006400
cases.append(("jmp + mov.l", F, [
    (F + 0x00, 0x412B, "jmp", "jmp @r1"),
    (F + 0x02, 0xD101, "mov.l", "mov.l @(4,pc),r1     ; slot"),
    (F + 0x04, 0x0009, "nop", "nop"),
    (F + 0x06, 0x0009, "nop", "nop"),
], F + 0x02, F + 0x08, {F + 0x08: (0x44444444, 4)}))

G = 0x06006500
cases.append(("braf + mov.l", G, [
    (G + 0x00, 0x0123, "braf", "braf r1"),
    (G + 0x02, 0xD101, "mov.l", "mov.l @(4,pc),r1     ; slot"),
    (G + 0x04, 0x0009, "nop", "nop"),
    (G + 0x06, 0x0009, "nop", "nop"),
], G + 0x02, G + 0x08, {G + 0x08: (0x55555555, 4)}))

md = Cs(CS_ARCH_SH, CS_MODE_SH2 | CS_MODE_BIG_ENDIAN)

def normalise(text):
    # Lower case, no spaces, every number (hex or decimal, after '#' or not) as decimal.
    text = text.lower().replace(" ", "")
    return re.sub(r"0x[0-9a-f]+|\d+", lambda m: str(int(m.group(0), 0)), text)

def resolve(addr, mnem, intent):
    # The intended text in Capstone's spelling: @(disp,pc) becomes the absolute address the
    # instruction reads from its own PC (word: pc + 4 + disp; long and mova: (pc + 4) & ~3 + disp).
    text = intent.split(";")[0].strip()
    m = re.search(r"@\((\d+),pc\)", text)
    if m:
        base = addr + 4 if mnem == "mov.w" else (addr + 4) & ~3
        text = text.replace(m.group(0), hex(base + int(m.group(1))))
    return text

failures = 0
for name, base, words, slot, literal, lits in cases:
    print(f"{name} @{base:08X} (capstone SH2 BE):")
    for addr, w, mnem, intent in words:
        ins = list(md.disasm(struct.pack(">H", w), addr))
        got = f"{ins[0].mnemonic} {ins[0].op_str}".strip() if ins else "<capstone: invalid>"
        # Every word must decode to exactly the intended instruction: mnemonic, registers,
        # immediates, branch targets and PC-relative addresses all compared.
        ok = bool(ins) and normalise(got) == normalise(resolve(addr, mnem, intent))
        if ok and addr == slot and literal is not None:
            # Capstone resolves @(disp,pc) from the instruction's own PC: exactly the address the
            # spec loads, which the test must see left unfolded.
            m = re.search(r"0x[0-9a-f]+", ins[0].op_str)
            ok = m is not None and int(m.group(0), 16) == literal
        print(f"  {addr:08X}  {w:04X}  {got:28s} ; intended: {intent}{'' if ok else '   <-- MISMATCH'}")
        failures += 0 if ok else 1
    for a, (v, size) in sorted(lits.items()):
        print(f"  {a:08X}  {v:0{size * 2}X}  .{'long' if size == 4 else 'word'}")
    if name in ("bra", "bf/s"):
        target = [a for a, _, _, t in words if "target" in t][0]
        slotaddr = ((target + 2) & ~3) + (literal - ((slot + 4) & ~3))
        if slotaddr != literal + 4:
            print(f"  hardware's slot-relative address {slotaddr:08X} is not {literal + 4:08X}  <-- MISMATCH")
            failures += 1
print("every word decodes to its intended instruction, operands and targets included" if failures == 0
      else f"{failures} MISMATCH(ES)")
sys.exit(1 if failures else 0)
