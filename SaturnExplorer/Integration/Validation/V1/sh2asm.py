"""A deliberately small SH-2 assembler: only what the V1 guest needs, every encoding in one table.

Why it exists: no SH cross toolchain is assumed on the machine that builds V1. The encodings are checked
by tests/check_v1.py against the repository's own decoder (FrontEnd/src/Debug/Sh2Disasm.cpp), so a wrong
table entry fails a test instead of producing a guest that silently does something else.

Syntax (one statement per line, ';' starts a comment):
    label:                 a label (may share a line with a statement)
    mnemonic op,op         Hitachi operand order (source,destination), lower case
    ldl rN, expr           pseudo: mov.l @(disp,pc),rN with expr placed in the next .pool
    .pool                  flush pending literals here (4-aligned); put it after an unconditional branch
    .long/.word/.byte e,.. data;  .align n;  .space n;  .equ name,expr
Expressions are Python integer expressions over labels and .equ names (labels resolve to absolute
addresses, base + offset).
"""
import re

REG = {("r%d" % i): i for i in range(16)}


class AsmError(Exception):
    pass


def split_ops(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


class Asm:
    def __init__(self, base):
        self.base = base
        self.items = []          # (kind, payload, lineno, text)
        self.syms = {}

    # ---------------------------------------------------------------- parsing
    def parse(self, text):
        for no, raw in enumerate(text.splitlines(), 1):
            line = raw.split(";")[0].strip()
            while True:
                m = re.match(r"^([A-Za-z_.][\w.]*):\s*(.*)$", line)
                if not m:
                    break
                self.items.append(("label", m.group(1), no, raw))
                line = m.group(2)
            if not line:
                continue
            parts = line.split(None, 1)
            mn = parts[0].lower()
            ops = split_ops(parts[1]) if len(parts) > 1 else []
            self.items.append(("stmt", (mn, ops), no, raw))

    # ---------------------------------------------------------------- expressions
    def ev(self, expr, where=""):
        e = expr.strip()
        if not re.match(r"^[\w\s+\-*/()<>&|~.]+$", e):
            raise AsmError("bad expression %r %s" % (expr, where))
        try:
            return int(eval(e, {"__builtins__": {}}, self.syms))
        except NameError as ex:
            raise AsmError("undefined symbol in %r: %s %s" % (expr, ex, where))

    # ---------------------------------------------------------------- layout (pass 1) and emit (pass 2)
    def assemble(self):
        # pass 1: addresses; pools are laid out where .pool says; literal keys dedupe by expression text.
        pc = self.base
        self.syms = {}
        pending = []
        self.pool_at = {}      # id(item) -> (pool address, [keys])
        self.lit_addr = {}     # (pool item index, key) -> address
        self.layout = []       # (item index, address, size)
        cur_pool_keys = []
        self.ldl_pool = {}     # item index -> pool item index
        pending_ldl = []
        for idx, (kind, pl, no, raw) in enumerate(self.items):
            if kind == "label":
                self.syms[pl] = pc
                self.layout.append((idx, pc, 0))
                continue
            mn, ops = pl
            size = 2
            if mn == ".pool":
                pc = (pc + 3) & ~3
                keys = []
                for k in pending:
                    if k not in keys:
                        keys.append(k)
                for i, k in enumerate(keys):
                    self.lit_addr[(idx, k)] = pc + 4 * i
                for j in pending_ldl:
                    self.ldl_pool[j] = idx
                pending, pending_ldl = [], []
                size = 4 * len(keys)
                self.layout.append((idx, pc, size))
                pc += size
                continue
            if mn == "ldl":
                pending.append(ops[1].strip())
                pending_ldl.append(idx)
            elif mn == ".equ":
                self.syms[ops[0]] = self.ev(ops[1], "line %d" % no)
                size = 0
            elif mn == ".long":
                pc = pc  # no implicit alignment: author aligns
                size = 4 * len(ops)
            elif mn == ".word":
                size = 2 * len(ops)
            elif mn == ".byte":
                size = len(ops)
            elif mn == ".align":
                n = self.ev(ops[0])
                npc = (pc + n - 1) // n * n
                self.layout.append((idx, pc, npc - pc))
                pc = npc
                continue
            elif mn == ".space":
                size = self.ev(ops[0])
            self.layout.append((idx, pc, size))
            pc += size
        if pending:
            raise AsmError("literals pending at end of source: %s (missing .pool)" % pending)
        # pass 2
        self.code = []           # (address, item index, mnemonic, operands) of every instruction
        out = bytearray()
        for idx, addr, size in self.layout:
            kind, pl, no, raw = self.items[idx]
            if kind == "label":
                continue
            mn, ops = pl
            where = "(line %d: %s)" % (no, raw.strip())
            assert self.base + len(out) <= addr
            out += b"\0" * (addr - self.base - len(out))
            if mn == ".pool":
                keys = []
                for (pi, k), a in sorted(self.lit_addr.items(), key=lambda kv: kv[1]):
                    if pi == idx:
                        keys.append(k)
                for k in keys:
                    out += (self.ev(k, where) & 0xFFFFFFFF).to_bytes(4, "big")
            elif mn == ".long":
                for o in ops:
                    out += (self.ev(o, where) & 0xFFFFFFFF).to_bytes(4, "big")
            elif mn == ".word":
                for o in ops:
                    out += (self.ev(o, where) & 0xFFFF).to_bytes(2, "big")
            elif mn == ".byte":
                for o in ops:
                    out += bytes([self.ev(o, where) & 0xFF])
            elif mn in (".align", ".space"):
                out += b"\0" * size
            elif mn == ".equ":
                pass
            else:
                w = self.encode(idx, addr, mn, ops, where)
                self.code.append((addr, idx, mn, ops))
                out += w.to_bytes(2, "big")
        return bytes(out)

    # ---------------------------------------------------------------- encoding
    def r(self, s, where):
        s = s.strip().lower()
        if s not in REG:
            raise AsmError("expected register, got %r %s" % (s, where))
        return REG[s]

    def encode(self, idx, pc, mn, ops, where):
        R = lambda s: self.r(s, where)
        simple = {"nop": 0x0009, "rts": 0x000B, "rte": 0x002B, "sleep": 0x001B, "clrt": 0x0008, "sett": 0x0018}
        if mn in simple:
            return simple[mn]
        # one-register shifts / dt / cmp
        one = {"dt": 0x4010, "shll": 0x4000, "shlr": 0x4001, "shll2": 0x4008, "shlr2": 0x4009,
               "shll8": 0x4018, "shlr8": 0x4019, "shll16": 0x4028, "shlr16": 0x4029,
               "cmp/pl": 0x4015, "cmp/pz": 0x4011}
        if mn in one and len(ops) == 1:
            return one[mn] | (R(ops[0]) << 8)
        two = {"add": 0x300C, "sub": 0x3008, "and": 0x2009, "or": 0x200B, "xor": 0x200A,
               "cmp/eq": 0x3000, "cmp/hs": 0x3002, "cmp/ge": 0x3003, "cmp/hi": 0x3006, "cmp/gt": 0x3007,
               "tst": 0x2008, "extu.b": 0x600C, "extu.w": 0x600D}
        imm = lambda s: self.ev(s[1:], where) if s.startswith("#") else None
        if mn in two and len(ops) == 2 and not ops[0].startswith("#"):
            return two[mn] | (R(ops[1]) << 8) | (R(ops[0]) << 4)
        if mn == "add" and ops[0].startswith("#"):
            v = imm(ops[0])
            if not -128 <= v <= 127:
                raise AsmError("add immediate out of range %s" % where)
            return 0x7000 | (R(ops[1]) << 8) | (v & 0xFF)
        if mn == "cmp/eq" and ops[0].startswith("#"):
            return 0x8800 | (imm(ops[0]) & 0xFF)
        if mn in ("tst", "and", "or") and ops[0].startswith("#"):
            if R(ops[1]) != 0:
                raise AsmError("%s #imm needs r0 %s" % (mn, where))
            return {"tst": 0xC800, "and": 0xC900, "or": 0xCB00}[mn] | (imm(ops[0]) & 0xFF)
        # branches
        def tgt(s):
            return self.ev(s, where)
        if mn in ("bt", "bf", "bt/s", "bf/s"):
            d = (tgt(ops[0]) - (pc + 4)) // 2
            if (tgt(ops[0]) - (pc + 4)) % 2 or not -128 <= d <= 127:
                raise AsmError("branch out of range %s" % where)
            return {"bt": 0x8900, "bf": 0x8B00, "bt/s": 0x8D00, "bf/s": 0x8F00}[mn] | (d & 0xFF)
        if mn in ("bra", "bsr"):
            d = (tgt(ops[0]) - (pc + 4)) // 2
            if not -2048 <= d <= 2047:
                raise AsmError("bra/bsr out of range %s" % where)
            return (0xA000 if mn == "bra" else 0xB000) | (d & 0xFFF)
        if mn in ("jmp", "jsr"):
            m = re.match(r"^@(r\d+)$", ops[0].lower())
            if not m:
                raise AsmError("jmp/jsr operand %s" % where)
            return (0x402B if mn == "jmp" else 0x400B) | (R(m.group(1)) << 8)
        if mn == "ldc" and ops[1].lower() in ("sr", "vbr"):
            return (0x400E if ops[1].lower() == "sr" else 0x402E) | (R(ops[0]) << 8)
        if mn == "sts.l" and ops[0].lower() == "pr":
            m = re.match(r"^@-(r\d+)$", ops[1].lower())
            return 0x4022 | (R(m.group(1)) << 8)
        if mn == "lds.l" and ops[1].lower() == "pr":
            m = re.match(r"^@(r\d+)\+$", ops[0].lower())
            return 0x4026 | (R(m.group(1)) << 8)
        if mn == "mova":
            t = tgt(ops[0])
            base = (pc + 4) & ~3
            d = (t - base) // 4
            if (t - base) % 4 or not 0 <= d <= 255 or R(ops[1]) != 0:
                raise AsmError("mova target %s" % where)
            return 0xC700 | d
        if mn == "ldl":
            n = R(ops[0])
            pidx = self.ldl_pool[idx]
            a = self.lit_addr[(pidx, ops[1].strip())]
            d = (a - ((pc + 4) & ~3)) // 4
            if not 0 <= d <= 255:
                raise AsmError("literal pool out of range (%d) %s" % (d, where))
            return 0xD000 | (n << 8) | d
        if mn == "mov":
            if ops[0].startswith("#"):
                v = imm(ops[0])
                if not -128 <= v <= 255:
                    raise AsmError("mov immediate out of range %s" % where)
                return 0xE000 | (R(ops[1]) << 8) | (v & 0xFF)
            return 0x6003 | (R(ops[1]) << 8) | (R(ops[0]) << 4)
        sz = {"mov.b": 0, "mov.w": 1, "mov.l": 2}
        if mn in sz:
            s = sz[mn]
            a, b = ops[0].replace(" ", "").lower(), ops[1].replace(" ", "").lower()
            ma = re.match(r"^@(r\d+)$", a)
            mb = re.match(r"^@(r\d+)$", b)
            if ma and b in REG:                                   # @Rm,Rn
                return 0x6000 | (R(b) << 8) | (R(ma.group(1)) << 4) | s
            if mb and a in REG:                                   # Rm,@Rn
                return 0x2000 | (R(mb.group(1)) << 8) | (R(a) << 4) | s
            ma = re.match(r"^@(r\d+)\+$", a)
            if ma and b in REG:                                   # @Rm+,Rn
                return 0x6004 | (R(b) << 8) | (R(ma.group(1)) << 4) | s
            mb = re.match(r"^@-(r\d+)$", b)
            if mb and a in REG:                                   # Rm,@-Rn
                return 0x2004 | (R(mb.group(1)) << 8) | (R(a) << 4) | s
            ma = re.match(r"^@\(r0,(r\d+)\)$", a)
            if ma and b in REG:                                   # @(R0,Rm),Rn
                return 0x000C | s | (R(b) << 8) | (R(ma.group(1)) << 4)
            mb = re.match(r"^@\(r0,(r\d+)\)$", b)
            if mb and a in REG:                                   # Rm,@(R0,Rn)
                return 0x0004 | s | (R(mb.group(1)) << 8) | (R(a) << 4)
            mb = re.match(r"^@\((.+),(r\d+)\)$", b)
            if mb and a in REG:                                   # Rm,@(disp,Rn)
                d = self.ev(mb.group(1), where)
                n = R(mb.group(2))
                if s == 2:
                    if d % 4 or not 0 <= d <= 60:
                        raise AsmError("disp %s" % where)
                    return 0x1000 | (n << 8) | (R(a) << 4) | (d // 4)
                if R(a) != 0 or d % (1 << s) or not 0 <= d >> s <= 15:
                    raise AsmError("disp %s" % where)
                return (0x8000 | (s << 8)) | (n << 4) | (d >> s)
            ma = re.match(r"^@\((.+),(r\d+)\)$", a)
            if ma and b in REG:                                   # @(disp,Rm),Rn
                d = self.ev(ma.group(1), where)
                m = R(ma.group(2))
                if s == 2:
                    if d % 4 or not 0 <= d <= 60:
                        raise AsmError("disp %s" % where)
                    return 0x5000 | (R(b) << 8) | (m << 4) | (d // 4)
                if R(b) != 0 or d % (1 << s) or not 0 <= d >> s <= 15:
                    raise AsmError("disp %s" % where)
                return (0x8400 | (s << 8)) | (m << 4) | (d >> s)
        raise AsmError("cannot encode %r %r %s" % (mn, ops, where))
