"""A minimal SH-2 interpreter, just enough to execute the V1 guest on a fake machine.

It decodes the raw 16-bit opcodes itself (it does not reuse the assembler's tables), so running the
assembled bytes through it is an independent check that the program does what its source says. It
models: registers, T, SR.I, VBR, PR, delay slots, the exceptions that SLEEP waits for (an SCU VBlank-IN
at vector 0x40, taken only when SCU IMS bit 0 is clear and SR.I < 15), and per-instruction cycle cost
under the Mednafen model stated in EXPECTED.md (1 per instruction, +2 for a taken bt/bf; memory waits
are NOT modelled, so only register-only loops are cycle-exact). It is a test fixture, not an emulator.
"""


class SimError(Exception):
    pass


M32 = 0xFFFFFFFF


def s8(v): return v - 256 if v & 0x80 else v
def s16(v): return v - 65536 if v & 0x8000 else v
def s32(v): return v - (1 << 32) if v & 0x80000000 else v


class Machine:
    def __init__(self):
        self.mem = {}                 # byte address (27 bit) -> byte
        self.R = [0] * 16
        self.PC = 0
        self.PR = 0
        self.SR = 0xF0
        self.VBR = 0
        self.cycles = 0
        self.log = []                 # data accesses of the current frame: (addr27, size, 'r'|'w')
        self.events = []              # (kind, ...) writes to interesting registers, in order
        self.vblank_count = 0
        self.sleeping = False

    # ---------------------------------------------------------------- memory
    @staticmethod
    def phys(a):
        return a & 0x07FFFFFF         # the bus address the profiler classifies (cache bits dropped)

    def rd(self, a, size, log=True):
        p = self.phys(a)
        if log:
            self.log.append((p, size, "r", a))
        if p == 0x00100063:           # SMPC SF: commands complete instantly
            return 0
        v = 0
        for i in range(size):
            v = (v << 8) | self.mem.get(p + i, 0)
        return v

    def wr(self, a, size, v, log=True):
        p = self.phys(a)
        if log:
            self.log.append((p, size, "w", a))
        if p == 0x0010001F:
            self.events.append(("smpc_cmd", v & 0xFF))
        if p == 0x00100063:
            return
        if 0x05D00000 <= p < 0x05D00010 or 0x05FE0000 <= p < 0x05FE0070 or 0x05F80000 <= p < 0x05F80100:
            self.events.append(("reg", p, size, v & ((1 << (8 * size)) - 1)))
        for i in range(size):
            self.mem[p + i] = (v >> (8 * (size - 1 - i))) & 0xFF

    def load(self, addr, data):
        for i, b in enumerate(data):
            self.mem[self.phys(addr) + i] = b

    def fetch(self, a):
        p = self.phys(a)
        return (self.mem.get(p, 0) << 8) | self.mem.get(p + 1, 0)

    # ---------------------------------------------------------------- execution
    def T(self): return self.SR & 1
    def setT(self, b): self.SR = (self.SR & ~1) | (1 if b else 0)

    def push(self, v):
        self.R[15] = (self.R[15] - 4) & M32
        self.wr(self.R[15], 4, v)

    def pop(self):
        v = self.rd(self.R[15], 4)
        self.R[15] = (self.R[15] + 4) & M32
        return v

    def take_irq(self, vec):
        self.push(self.SR)
        self.push(self.PC)
        self.SR = (self.SR & ~0xF0) | 0xF0
        self.PC = self.rd(self.VBR + vec * 4, 4)
        self.cycles += 8              # informational; not asserted anywhere

    def irq_deliverable(self):
        ims = self.rd(0x05FE00A0, 4, log=False)
        return (not ims & 1) and ((self.SR >> 4) & 0xF) < 15

    def run_frame(self, max_instr=2000000):
        """Run until the next sleep, then deliver one VBlank-IN; return when the sleep is about to wake."""
        self.log = []
        self.events = []
        n = 0
        while not self.sleeping:
            self.step()
            n += 1
            if n > max_instr:
                raise SimError("no sleep within %d instructions at pc=%08X" % (max_instr, self.PC))
        if not self.irq_deliverable():
            raise SimError("sleeping with interrupts that cannot wake the CPU (SR=%X) pc=%08X" % (self.SR, self.PC))
        self.sleeping = False
        self.vblank_count += 1
        self.take_irq(0x40)

    def step(self, in_slot=False):
        op = self.fetch(self.PC)
        pc = self.PC
        self.PC = (self.PC + 2) & M32
        self.cycles += 1
        R = self.R
        n, m, d, i = (op >> 8) & 0xF, (op >> 4) & 0xF, op & 0xF, op & 0xFF
        hi = op >> 12

        def delayed(target):
            self.step(in_slot=True)
            self.PC = target & M32

        if op == 0x0009:
            return
        if op == 0x001B:
            self.sleeping = True
            return
        if op == 0x000B:                                  # rts
            t = self.PR
            return delayed(t)
        if op == 0x002B:                                  # rte
            t = self.pop()
            sr = self.pop()
            self.step(in_slot=True)
            self.PC = t
            self.SR = sr & 0x3F3
            return
        if hi == 0x0 and d in (4, 5, 6):                  # mov.x Rm,@(R0,Rn)
            self.wr((R[0] + R[n]) & M32, 1 << (d - 4), R[m])
            return
        if hi == 0x0 and d in (0xC, 0xD, 0xE):            # mov.x @(R0,Rm),Rn
            sz = 1 << (d - 0xC)
            v = self.rd((R[0] + R[m]) & M32, sz)
            R[n] = (s8(v) if sz == 1 else s16(v) if sz == 2 else v) & M32
            return
        if hi == 0x1:
            self.wr((R[n] + d * 4) & M32, 4, R[m])
            return
        if hi == 0x2:
            if d in (0, 1, 2):
                self.wr(R[n], 1 << d, R[m])
            elif d in (4, 5, 6):
                R[n] = (R[n] - (1 << (d - 4))) & M32
                self.wr(R[n], 1 << (d - 4), R[m])
            elif d == 8:
                self.setT((R[n] & R[m]) == 0)
            elif d == 9: R[n] &= R[m]
            elif d == 0xA: R[n] ^= R[m]
            elif d == 0xB: R[n] |= R[m]
            else: raise SimError("op %04X at %08X" % (op, pc))
            return
        if hi == 0x3:
            a, b = R[n], R[m]
            if d == 0: self.setT(a == b)
            elif d == 2: self.setT(a >= b)
            elif d == 3: self.setT(s32(a) >= s32(b))
            elif d == 6: self.setT(a > b)
            elif d == 7: self.setT(s32(a) > s32(b))
            elif d == 8: R[n] = (a - b) & M32
            elif d == 0xC: R[n] = (a + b) & M32
            else: raise SimError("op %04X at %08X" % (op, pc))
            return
        if hi == 0x4:
            lo = op & 0xFF
            if lo == 0x00: self.setT(R[n] >> 31); R[n] = (R[n] << 1) & M32
            elif lo == 0x01: self.setT(R[n] & 1); R[n] >>= 1
            elif lo == 0x08: R[n] = (R[n] << 2) & M32
            elif lo == 0x09: R[n] >>= 2
            elif lo == 0x18: R[n] = (R[n] << 8) & M32
            elif lo == 0x19: R[n] >>= 8
            elif lo == 0x10:
                R[n] = (R[n] - 1) & M32
                self.setT(R[n] == 0)
            elif lo == 0x0B:                              # jsr
                self.PR = self.PC + 2
                return delayed(R[n])
            elif lo == 0x2B:                              # jmp
                return delayed(R[n])
            elif lo == 0x0E: self.SR = R[n] & 0x3F3
            elif lo == 0x2E: self.VBR = R[n]
            elif lo == 0x22:
                R[n] = (R[n] - 4) & M32
                self.wr(R[n], 4, self.PR)
            elif lo == 0x26:
                self.PR = self.rd(R[n], 4)
                R[n] = (R[n] + 4) & M32
            else: raise SimError("op %04X at %08X" % (op, pc))
            return
        if hi == 0x5:
            R[n] = self.rd((R[m] + d * 4) & M32, 4)
            return
        if hi == 0x6:
            if d in (0, 1, 2):
                sz = 1 << d
                v = self.rd(R[m], sz)
                R[n] = (s8(v) if sz == 1 else s16(v) if sz == 2 else v) & M32
            elif d == 3: R[n] = R[m]
            elif d in (4, 5, 6):
                sz = 1 << (d - 4)
                v = self.rd(R[m], sz)
                inc = sz if m != n else 0
                R[m] = (R[m] + inc) & M32
                R[n] = (s8(v) if sz == 1 else s16(v) if sz == 2 else v) & M32
            elif d == 0xC: R[n] = R[m] & 0xFF
            elif d == 0xD: R[n] = R[m] & 0xFFFF
            else: raise SimError("op %04X at %08X" % (op, pc))
            return
        if hi == 0x7:
            R[n] = (R[n] + s8(i)) & M32
            return
        if hi == 0x8:
            sub = n
            if sub == 0x8: self.setT(R[0] == (s8(i) & M32))
            elif sub in (0x9, 0xB, 0xD, 0xF):
                cond = self.T() if sub in (0x9, 0xD) else not self.T()
                tgt = (pc + 4 + 2 * s8(i)) & M32
                if sub in (0x9, 0xB):
                    if cond:
                        self.PC = tgt
                        self.cycles += 2
                else:
                    if cond:
                        delayed(tgt)
                    else:
                        pass
            elif sub in (0x0, 0x1):
                self.wr((R[m] + d * (1 << sub)) & M32, 1 << sub, R[0])
            elif sub in (0x4, 0x5):
                sz = 1 << (sub - 4)
                v = self.rd((R[m] + d * sz) & M32, sz)
                R[0] = (s8(v) if sz == 1 else s16(v)) & M32
            else: raise SimError("op %04X at %08X" % (op, pc))
            return
        if hi == 0xA:
            d12 = op & 0xFFF
            d12 = d12 - 4096 if d12 & 0x800 else d12
            return delayed(pc + 4 + 2 * d12)
        if hi == 0xB:
            d12 = op & 0xFFF
            d12 = d12 - 4096 if d12 & 0x800 else d12
            self.PR = pc + 4
            return delayed(pc + 4 + 2 * d12)
        if hi == 0xC:
            if n == 0x7: R[0] = ((pc + 4) & ~3) + i * 4
            elif n == 0x8: self.setT((R[0] & i) == 0)
            elif n == 0x9: R[0] &= i
            elif n == 0xB: R[0] |= i
            else: raise SimError("op %04X at %08X" % (op, pc))
            return
        if hi == 0xD:
            R[n] = self.rd(((pc + 4) & ~3) + i * 4, 4)
            return
        if hi == 0xE:
            R[n] = s8(i) & M32
            return
        raise SimError("op %04X at %08X" % (op, pc))
