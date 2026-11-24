"""The V1 guest program, as SH-2 assembly text generated from the parameters in EXPECTED.md.

Memory map of the running guest (all work RAM high):
    0x06010000  code + data (the payload; copied here by the boot trampoline)
    0x0600FE00  initial stack (grows down)          0x0600FF00  state block (r14)
    0x06020000  DMA source (16 KB, whatever is there)
State block: +0 phase id, +4 VBlank-IN count (written by the handler), +8 count already acted on,
+12 frames spent in the phase.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = 0x06010000
STACK = 0x0600FE00
STATE = 0x0600FF00
DMA_SRC = 0x06020000
DMA_DST = 0x25C20000
N_PHASES = 14

PHASE_NAMES = ["IDLE", "BUSY", "FUNC", "BUS", "VDP1_ONE", "VDP1_MANY", "DMA_BIG", "DMA_SMALL",
               "DMA_DRAW", "SSH_ON", "SSH_OFF", "M68K_STOP", "M68K_BUSY", "M68K_OFF"]
COLORS = [0x0000, 0x001F, 0x03E0, 0x7C00, 0x03FF, 0x7FE0, 0x7C1F, 0x4210, 0x2D6B, 0x5294, 0x35AD,
          0x1CE7, 0x739C, 0x0842]

# (address, count, kind) kind 0 = mov.l read, 1 = mov.b read, 2 = mov.l write. The order is the
# order of the rows in EXPECTED.md, phase "BUS".
BUS_TABLE = [
    (0x20000000, 10, 0), (0x20100061, 20, 1), (0x20180001, 30, 1), (0x20200000, 40, 0),
    (0x20280000, 11, 2), (0x20400000, 50, 0), (0x22000000, 60, 0), (0x24000000, 70, 0),
    (0x25890008, 80, 0), (0x25A7F000, 90, 0), (0x25A7F100, 15, 2), (0x25C7F000, 100, 0),
    (0x25C7F100, 13, 2), (0x25E7F000, 110, 0), (0x25E7F100, 14, 2), (0x25FE00A0, 120, 0),
    (0x26000000, 130, 0), (0x26080000, 12, 2), (0x25000000, 140, 0), (0x25900000, 150, 0),
]


def read_params(path=None):
    path = path or os.path.join(HERE, "EXPECTED.md")
    txt = open(path, encoding="utf-8").read()
    m = re.search(r"```params\n(.*?)```", txt, re.S)
    if not m:
        raise SystemExit("EXPECTED.md has no params block")
    out = {}
    for line in m.group(1).splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = int(v.strip())
    return out


def wr(size, addr, val):
    """Absolute register write: uses r1 (address) and r2 (value)."""
    mn = {2: "mov.w", 4: "mov.l"}[size]
    return ["ldl r1, 0x%08X" % addr, "ldl r2, 0x%X" % val, "%s r2,@r1" % mn]


def fn(name, body, leaf=False):
    lines = ["%s:" % name]
    if not leaf:
        lines.append("sts.l pr,@-r15")
    lines += body
    if not leaf:
        lines.append("lds.l @r15+,pr")
    lines += ["rts", "nop", ".pool"]
    return lines


def call(target):
    return ["bsr %s" % target, "nop"]


def source(p):
    S = []
    add = S.extend
    S.append(".equ PHASE_FRAMES, %d" % p["PHASE_FRAMES"])
    S.append(".equ N_PHASES, %d" % N_PHASES)

    # ---------------------------------------------------------------- start-up
    add(["start:",
         "ldl r15, 0x%08X" % STACK,
         "ldl r0, 0xF0", "ldc r0,sr",
         "ldl r0, vtab", "ldc r0,vbr",
         "ldl r14, 0x%08X" % STATE,
         "mov #0,r0",
         "mov.l r0,@r14", "mov.l r0,@(4,r14)", "mov.l r0,@(8,r14)", "mov.l r0,@(12,r14)"])
    add(call("hw_init"))
    add(["ldl r1, 0x25FE00A0", "ldl r0, 0xFFFFFFFE", "mov.l r0,@r1",     # SCU IMS: only VBlank-IN unmasked
         "mov #0,r4"])
    add(call("phase_enter"))
    add(["mov #0,r0", "ldc r0,sr"])                                      # interrupts on
    # ---------------------------------------------------------------- the frame loop
    add(["main_loop:",
         "sleep",
         "mov.l @(4,r14),r0", "mov.l @(8,r14),r1",
         "cmp/eq r0,r1", "bt main_loop",                                 # woken for another reason
         "mov.l r0,@(8,r14)",
         "mov.l @(12,r14),r2", "add #1,r2",
         "ldl r3, PHASE_FRAMES",
         "cmp/hs r3,r2", "bf same_phase",
         "mov.l @r14,r4", "add #1,r4",
         "mov r4,r0", "cmp/eq #N_PHASES,r0", "bf np_ok", "mov #0,r4",
         "np_ok:",
         "mov #0,r2", "mov.l r2,@(12,r14)"])
    add(call("phase_enter"))
    add(["bra main_loop", "nop",
         "same_phase:",
         "mov.l r2,@(12,r14)", "mov.l @r14,r4"])
    add(call("phase_frame"))
    add(["bra main_loop", "nop", ".pool"])

    # ---------------------------------------------------------------- interrupt handlers
    add(["vbin:",
         "mov.l r0,@-r15", "mov.l r1,@-r15",
         "ldl r1, 0x%08X" % (STATE + 4),
         "mov.l @r1,r0", "add #1,r0", "mov.l r0,@r1",
         "mov.l @r15+,r1", "mov.l @r15+,r0",
         "rte", "nop", ".pool",
         "bad_irq:", "bra bad_irq", "nop"])

    # ---------------------------------------------------------------- hardware init and quiesce
    body = []
    body += wr(2, 0x25F80000, 0x0100)          # VDP2 TVMD: display off, back screen shown
    body += wr(2, 0x25F800AC, 0x0003)          # BKTAU
    body += wr(2, 0x25F800AE, 0xFFFF)          # BKTAL: back-screen colour at VRAM word 0x3FFFF
    body += wr(2, 0x25D00000, 0)               # VDP1 TVMR
    body += wr(2, 0x25D00002, 0)               # FBCR: automatic swap every frame
    body += wr(2, 0x25D00004, 0)               # PTMR: no drawing
    add(fn("hw_init", body, leaf=True))
    body = wr(2, 0x25D00004, 0) + wr(4, 0x25FE0010, 0) + wr(4, 0x25FE0014, 0)   # PTMR, D0EN, D0MD
    add(fn("quiesce", body, leaf=True))

    # ---------------------------------------------------------------- phase dispatch
    add(fn("phase_enter", [
        "mov.l r4,@r14",
        "mov r4,r0", "shll r0",
        "ldl r1, colortab", "mov.w @(r0,r1),r2",
        "ldl r1, 0x25E7FFFE", "mov.w r2,@r1",                        # back-screen colour
        "mov r4,r0", "shll2 r0",
        "ldl r1, enter_tab", "mov.l @(r0,r1),r1",
        "mov.l r1,@-r15", "mov.l r4,@-r15"] + call("quiesce") + [
        "mov.l @r15+,r4", "mov.l @r15+,r1",
        "jsr @r1", "nop"]))
    add(fn("phase_frame", [
        "mov r4,r0", "shll2 r0",
        "ldl r1, frame_tab", "mov.l @(r0,r1),r1",
        "tst r1,r1", "bt pf_done",
        "jsr @r1", "nop",
        "pf_done:"]))

    # ---------------------------------------------------------------- SMPC command
    add(["smpc_cmd:",                                                    # r4 = command
         "ldl r2, 0x20100063", "ldl r3, 0x2010001F",
         "sc_w1:", "mov.b @r2,r0", "tst #1,r0", "bf sc_w1",
         "mov #1,r0", "mov.b r0,@r2",
         "mov r4,r0", "mov.b r0,@r3",
         "sc_w2:", "mov.b @r2,r0", "tst #1,r0", "bf sc_w2",
         "rts", "nop", ".pool"])

    def smpc(c):
        return ["mov #%d,r4" % c] + call("smpc_cmd")

    # ---------------------------------------------------------------- phase enter routines
    add(fn("e_idle", smpc(0x07) + smpc(0x03)))                            # SNDOFF, SSHOFF
    add(["e_nop:", "rts", "nop"])
    add(fn("build_list", [                                               # r4 = command count
        "ldl r1, 0x25C00000", "mov #10,r2", "mov r4,r3",
        "bl_l:", "mov.w r2,@r1", "add #32,r1", "dt r3", "bf bl_l",
        "ldl r2, 0x8000", "mov.w r2,@r1"], leaf=True))

    def vdp1_enter(name, n, extra=()):
        b = ["ldl r4, %d" % n] + call("build_list") + wr(2, 0x25D00004, 2) + list(extra)
        add(fn(name, b))
    vdp1_enter("e_vdp1_one", p["VDP1_ONE_CMDS"])
    vdp1_enter("e_vdp1_many", p["VDP1_MANY_CMDS"])

    def dma(nbytes):
        return (wr(4, 0x25FE0000, DMA_SRC) + wr(4, 0x25FE0004, DMA_DST) + wr(4, 0x25FE0008, nbytes)
                + wr(4, 0x25FE000C, 0x101) + wr(4, 0x25FE0014, 1) + wr(4, 0x25FE0010, 0x100))
    add(fn("e_dma_big", dma(p["DMA_BIG_BYTES"]), leaf=True))
    add(fn("e_dma_small", dma(p["DMA_SMALL_BYTES"]), leaf=True))
    vdp1_enter("e_dma_draw", p["DMA_DRAW_CMDS"], dma(p["DMA_BIG_BYTES"]))
    add(fn("e_ssh_on", smpc(0x02)))
    add(fn("e_ssh_off", smpc(0x03)))

    def m68k(prog):
        b = smpc(0x07)
        for i, w in enumerate([0x0000, 0x0800, 0x0000, 0x0100]):
            b += wr(2, 0x25A00000 + 2 * i, w)
        for i, w in enumerate(prog):
            b += wr(2, 0x25A00100 + 2 * i, w)
        return b + smpc(0x06)
    add(fn("e_m68k_stop", m68k([0x4E72, 0x2700])))
    add(fn("e_m68k_busy", m68k([0x60FE])))
    add(fn("e_m68k_off", smpc(0x07)))

    # ---------------------------------------------------------------- per-frame work
    add(["f_busy:", "ldl r1, %d" % p["BUSY_L"], "fb_l:", "dt r1", "bf fb_l", "rts", "nop", ".pool"])
    add(fn("f_func", ["ldl r2, func_a", "jsr @r2", "nop", "ldl r2, func_b", "jsr @r2", "nop"]))
    add(["func_a:", "ldl r1, %d" % p["FUNC_A_N"], "fa_l:", "dt r1", "bf fa_l", "rts", "nop", ".pool"])
    add(["func_b:", "ldl r1, %d" % p["FUNC_B_N"], "fbl_l:", "dt r1", "bf fbl_l", "rts", "nop", ".pool"])
    add(["f_bus:", "ldl r4, bus_tab",
         "fbus_next:", "mov.l @r4+,r2", "tst r2,r2", "bt fbus_done",
         "mov.l @r4+,r1", "mov.l @r4+,r3",
         "tst r3,r3", "bt fbus_rd32",
         "mov #1,r0", "cmp/eq r0,r3", "bt fbus_rd8",
         "fbus_wr:", "mov.l r0,@r2", "dt r1", "bf fbus_wr", "bra fbus_next", "nop",
         "fbus_rd32:", "mov.l @r2,r0", "dt r1", "bf fbus_rd32", "bra fbus_next", "nop",
         "fbus_rd8:", "mov.b @r2,r0", "dt r1", "bf fbus_rd8", "bra fbus_next", "nop",
         "fbus_done:", "rts", "nop", ".pool"])

    # ---------------------------------------------------------------- data
    enters = ["e_idle", "e_nop", "e_nop", "e_nop", "e_vdp1_one", "e_vdp1_many", "e_dma_big", "e_dma_small",
              "e_dma_draw", "e_ssh_on", "e_ssh_off", "e_m68k_stop", "e_m68k_busy", "e_m68k_off"]
    frames = ["0", "f_busy", "f_func", "f_bus"] + ["0"] * 10
    add([".align 4", "enter_tab:", ".long " + ", ".join(enters),
         "frame_tab:", ".long " + ", ".join(frames),
         "colortab:", ".word " + ", ".join("0x%04X" % c for c in COLORS),
         ".align 4", "bus_tab:"])
    for a, c, k in BUS_TABLE:
        S.append(".long 0x%08X, %d, %d" % (a, c, k))
    S.append(".long 0, 0, 0")
    add([".align 4", "vtab:"])
    for v in range(0x42):
        S.append(".long %s" % ("vbin" if v == 0x40 else "bad_irq"))
    add(["payload_end:"])
    return "\n".join(S) + "\n"


TRAMP = """
tramp:
    mova payload, r0
    mov r0, r1
    ldl r2, 0x%08X
    ldl r3, PAYLOAD_WORDS
cp:
    mov.l @r1+, r0
    mov.l r0, @r2
    add #4, r2
    dt r3
    bf cp
    ldl r0, 0x%08X
    jmp @r0
    nop
    .pool
payload:
"""


def build_objects(p=None):
    """Assemble; return (payload Asm, trampoline Asm, tramp bytes, payload bytes). `p` may override
    parameters (the test shortens BUSY_L for its whole-program simulation)."""
    from sh2asm import Asm
    p = p or read_params()
    a = Asm(BASE)
    a.parse(source(p))
    payload = a.assemble()
    payload += b"\0" * (-len(payload) % 4)
    t = Asm(0)
    t.parse(".equ PAYLOAD_WORDS, %d\n" % (len(payload) // 4) + TRAMP % (BASE + 0x20000000, BASE))
    tramp = t.assemble()
    assert len(tramp) % 4 == 0
    return a, t, tramp, payload


def build(p=None):
    """Return (tramp_bytes, payload_bytes, symbols of the payload)."""
    a, t, tramp, payload = build_objects(p)
    return tramp, payload, dict(a.syms)
