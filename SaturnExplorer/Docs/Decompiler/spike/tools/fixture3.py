# Fixture 3 (PLAN.md A3): PC-relative loads and mova in delay slots.
#
# On SH-2 the slot instruction of a taken delayed branch reads PC as the branch target + 2, so
# mov.l @(disp,PC) there loads from ((target + 2) & ~3) + disp, not from its own
# ((slot + 4) & ~3) + disp. Ghidra's SLEIGH spec computes the latter. The bra and bf/s cases put
# DIFFERENT literals at the two candidate addresses, so a decompiler that folds either one is
# caught; the others cover every other delayed-branch kind (rts, bsr, jmp, braf) and every
# PC-relative form (mov.w, mov.l, mova) in a slot, which Step 2 must flag as unsupported. Step 4
# (patch 0005) fixes bra and bsr in the spec; the cases added for it are below.
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

# Step 4 (the 0005 SLEIGH patch): bra and bsr give the slot its real PC. These cases check that
# a bsr slot gets it, and that it reaches nothing else: the instruction after a bsr + slot pair
# (reached when the call returns) and the instruction at a bra's target each load from their own
# PC. A register-target jsr keeps the Step 2 flag.
H = 0x06006600
cases.append(("bsr + mov.l", H, [
    (H + 0x00, 0xB004, "bsr", "bsr 0x0600660C"),
    (H + 0x02, 0xD403, "mov.l", "mov.l @(12,pc),r4    ; slot"),
    (H + 0x04, 0xD003, "mov.l", "mov.l @(12,pc),r0    ; after the pair"),
    (H + 0x06, 0x000B, "rts", "rts"),
    (H + 0x08, 0x0009, "nop", "nop"),
    (H + 0x0A, 0x0009, "nop", "nop"),
    (H + 0x0C, 0x000B, "rts", "rts                  ; <- target"),
    (H + 0x0E, 0x6043, "mov", "mov r4,r0            ; slot"),
], H + 0x02, H + 0x10, {H + 0x10: (0x33333333, 4), H + 0x14: (0x35353535, 4), H + 0x18: (0x34343434, 4)}))

I = 0x06006700
cases.append(("bra + target load", I, [
    (I + 0x00, 0xA002, "bra", "bra 0x06006708"),
    (I + 0x02, 0xD103, "mov.l", "mov.l @(12,pc),r1    ; slot"),
    (I + 0x04, 0x0009, "nop", "nop"),
    (I + 0x06, 0x0009, "nop", "nop"),
    (I + 0x08, 0xD003, "mov.l", "mov.l @(12,pc),r0    ; <- target"),
    (I + 0x0A, 0x000B, "rts", "rts"),
    (I + 0x0C, 0x301C, "add", "add r1,r0            ; slot"),
    (I + 0x0E, 0x0009, "nop", "nop"),
], I + 0x02, I + 0x10, {I + 0x10: (0x33333333, 4), I + 0x14: (0x01000000, 4), I + 0x18: (0x00000234, 4)}))

J = 0x06006800
cases.append(("jsr + mov.l", J, [
    (J + 0x00, 0x410B, "jsr", "jsr @r1"),
    (J + 0x02, 0xD101, "mov.l", "mov.l @(4,pc),r1     ; slot"),
    (J + 0x04, 0x000B, "rts", "rts"),
    (J + 0x06, 0x0009, "nop", "nop"),
], J + 0x02, J + 0x08, {J + 0x08: (0x66666666, 4)}))

K = 0x06006900
cases.append(("bra slot also jumped to", K, [
    (K + 0x00, 0x4415, "cmp/pl", "cmp/pl r4"),
    (K + 0x02, 0x8900, "bt", "bt 0x06006906"),
    (K + 0x04, 0xA002, "bra", "bra 0x0600690C"),
    (K + 0x06, 0xD103, "mov.l", "mov.l @(12,pc),r1    ; slot, and where the bt lands"),
    (K + 0x08, 0x0009, "nop", "nop"),
    (K + 0x0A, 0x0009, "nop", "nop"),
    (K + 0x0C, 0x000B, "rts", "rts                  ; <- target"),
    (K + 0x0E, 0x6013, "mov", "mov r1,r0            ; slot"),
], K + 0x06, K + 0x14, {K + 0x14: (DIRECT, 4), K + 0x18: (SLOT, 4)}))

L = 0x06006A00
cases.append(("bra slot is a switch case", L, [
    (L + 0x00, 0xE102, "mov", "mov #2,r1"),
    (L + 0x02, 0x3412, "cmp/hs", "cmp/hs r1,r4"),
    (L + 0x04, 0x890E, "bt", "bt 0x06006A24          ; default"),
    (L + 0x06, 0xC702, "mova", "mova 0x06006A10,r0     ; the table"),
    (L + 0x08, 0x4408, "shll2", "shll2 r4"),
    (L + 0x0A, 0x014E, "mov.l", "mov.l @(r0,r4),r1"),
    (L + 0x0C, 0x412B, "jmp", "jmp @r1"),
    (L + 0x0E, 0x0009, "nop", "nop"),
    (L + 0x18, 0xA002, "bra", "bra 0x06006A20         ; case 0"),
    (L + 0x1A, 0xD103, "mov.l", "mov.l @(12,pc),r1    ; slot, and case 1"),
    (L + 0x1C, 0x0009, "nop", "nop"),
    (L + 0x1E, 0x0009, "nop", "nop"),
    (L + 0x20, 0x000B, "rts", "rts                  ; <- target"),
    (L + 0x22, 0x6013, "mov", "mov r1,r0"),
    (L + 0x24, 0x000B, "rts", "rts"),
    (L + 0x26, 0xE000, "mov", "mov #0,r0"),
], L + 0x1A, L + 0x28, {L + 0x10: (L + 0x18, 4), L + 0x14: (L + 0x1A, 4), L + 0x28: (DIRECT, 4), L + 0x2C: (SLOT, 4)}))

# Where the hardware reads each statically-targeted slot's literal (branch target + 2 as PC).
HARDWARE = {"bra": B + 0x14, "bf/s": C + 0x14, "bsr + mov.l": H + 0x18, "bra + target load": I + 0x14,
            "bra slot also jumped to": K + 0x18, "bra slot is a switch case": L + 0x2C}

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
    if name in HARDWARE:
        target = [a for a, _, _, t in words if "target" in t][0]
        slotaddr = ((target + 2) & ~3) + (literal - ((slot + 4) & ~3))
        if slotaddr != HARDWARE[name]:
            print(f"  hardware's slot-relative address {slotaddr:08X} is not {HARDWARE[name]:08X}  <-- MISMATCH")
            failures += 1
print("every word decodes to its intended instruction, operands and targets included" if failures == 0
      else f"{failures} MISMATCH(ES)")
sys.exit(1 if failures else 0)
