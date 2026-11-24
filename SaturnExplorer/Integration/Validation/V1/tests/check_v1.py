#!/usr/bin/env python3
"""V1 checks that need no BIOS and no emulator.

    check_v1.py <v1_disasm executable>

1. the build script runs and its output is deterministic;
2. every emitted instruction, decoded by the repository's own SH-2 decoder (the v1_disasm helper links
   FrontEnd/src/Debug/Sh2Disasm.cpp), is the instruction the source asked for;
3. the cycle arithmetic of the pure loops in EXPECTED.md (4N-2) is recomputed from the decoded
   instruction list, and from a simulated run, and compared with the numbers printed in EXPECTED.md;
4. the guest is run on a fake machine (tests/sh2sim.py) for every phase and checked against what
   EXPECTED.md says the guest does: phase sequence, SMPC commands, VDP1 table words, DMA registers,
   sound-RAM program, and the number of bus requests per device in the BUS phase;
5. the boot trampoline copies the payload to its run address; the disc builder's refusals work.

What this does NOT show: any profiler counter, and anything about the real BIOS boot path.
"""
import hashlib
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
V1 = os.path.dirname(HERE)
sys.path.insert(0, V1)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(V1, "..", "..", "Mednafen"))

import v1_program as vp          # noqa: E402
import sh2sim                    # noqa: E402
from bus_decode import BUS_RANGES  # noqa: E402

fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
        print("FAIL:", msg)


def canon(s):
    s = s.lower().replace(" ", "")
    return re.sub(r"0x[0-9a-f]+|\d+", lambda m: str(int(m.group(0), 0)), s)


# ------------------------------------------------------------------ 1. build, determinism
params = vp.read_params()
a, t, tramp, payload = vp.build_objects()
a2, t2, tramp2, payload2 = vp.build_objects()
check(tramp == tramp2 and payload == payload2, "build is not deterministic")
check(len(payload) < 0x2000, "payload does not fit the IP area budget (%d bytes)" % len(payload))
print("payload %d bytes, trampoline %d bytes" % (len(payload), len(tramp)))

# ------------------------------------------------------------------ 2. decode every instruction
disasm = sys.argv[1] if len(sys.argv) > 1 else None
check(disasm and os.path.exists(disasm), "v1_disasm helper missing (argv[1])")


def decode(blob, base):
    with tempfile.NamedTemporaryFile(delete=False, suffix=".bin") as f:
        f.write(blob)
        name = f.name
    try:
        out = subprocess.run([disasm, name, "%X" % base], capture_output=True, text=True, check=True).stdout
    finally:
        os.unlink(name)
    rows = {}
    for line in out.splitlines():
        head, flags = line.rsplit("|", 1)
        addr, op, rest = head.split(" ", 2)
        rows[int(addr, 16)] = (int(op, 16), rest.strip(), flags)
    return rows


def expected_text(asm, addr, idx, mn, ops):
    """The decoder-format text the source line should decode to."""
    if mn == "ldl":
        pidx = asm.ldl_pool[idx]
        return "mov.l @(0x%08X),%s" % (asm.lit_addr[(pidx, ops[1].strip())], ops[0])
    if mn in ("bra", "bsr", "bt", "bf", "bt/s", "bf/s"):
        return "%s 0x%08X" % (mn, asm.ev(ops[0]))
    if mn == "mova":
        return "mova @(0x%08X),r0" % asm.ev(ops[0])
    txt = mn + " " + ",".join(ops)

    def imm(m):
        return "#%d" % asm.ev(m.group(1))
    txt = re.sub(r"#([A-Za-z_]\w*)", imm, txt)
    return txt


def check_decode(name, asm, blob, base):
    rows = decode(blob, base)
    n = 0
    for addr, idx, mn, ops in asm.code:
        op, text, flags = rows[addr]
        want = canon(expected_text(asm, addr, idx, mn, ops))
        got = canon(text)
        check(flags[0] == "1", "%s %08X: decoder says invalid (%04X)" % (name, addr, op))
        check(got == want, "%s %08X: source %r -> decoder %r (want %r)" % (name, addr, mn + " " + ",".join(ops), text, want))
        n += 1
    print("%s: %d instructions decode to the intended source" % (name, n))


if disasm and os.path.exists(disasm):
    check_decode("payload", a, payload, vp.BASE)
    check_decode("trampoline", t, tramp, 0)

# ------------------------------------------------------------------ 3. cycle arithmetic
exp = open(os.path.join(V1, "EXPECTED.md"), encoding="utf-8").read()


def loop_cycles_static(asm, label, n):
    """Cycles of `label: <body...>; bf label` for n iterations from the assembler's own instruction list."""
    start = asm.syms[label]
    body = [c for c in asm.code if c[0] >= start]
    ins = []
    for addr, idx, mn, ops in body:
        ins.append(mn)
        if mn == "bf" and asm.ev(ops[0]) == start:
            break
    assert ins[-1] == "bf", ins
    k = len(ins)                 # every instruction 1 cycle; a taken bf costs 2 more
    return (n - 1) * (k + 2) + k


def table_value(label):
    m = re.search(r"\|\s*%s\s*\|[^|]*\|\s*\*\*([\d,]+)\*\*\s*\|" % label, exp)
    check(m is not None, "EXPECTED.md has no cycle row for %s" % label)
    return int(m.group(1).replace(",", "")) if m else None


for label, key, row in (("fb_l", "BUSY_L", "busy phase"), ("fa_l", "FUNC_A_N", "function A"),
                        ("fbl_l", "FUNC_B_N", "function B")):
    n = params[key]
    want = table_value(row)
    got = loop_cycles_static(a, label, n)
    check(got == 4 * n - 2, "%s: instruction list gives %d, 4N-2 is %d" % (row, got, 4 * n - 2))
    check(got == want, "%s: instruction list gives %d, EXPECTED.md says %s" % (row, got, want))
    # and by running the emitted bytes
    m = sh2sim.Machine()
    m.load(vp.BASE, payload)
    m.R[1] = n
    m.PC = a.syms[label]
    c0 = m.cycles
    while m.PC == a.syms[label] or m.PC == a.syms[label] + 2:
        m.step()
    check(m.cycles - c0 == want, "%s: simulated %d cycles, EXPECTED.md says %s" % (row, m.cycles - c0, want))
    print("%s: N=%d -> %d cycles (static, simulated, EXPECTED.md agree)" % (row, n, want))

# ------------------------------------------------------------------ 5a. trampoline copies the payload
m = sh2sim.Machine()
IPLOAD = 0x06002E00
m.load(IPLOAD, tramp + payload)
m.PC = IPLOAD
steps = 0
while m.PC != vp.BASE and steps < 200000:
    m.step()
    steps += 1
check(m.PC == vp.BASE, "trampoline did not jump to the payload base (pc=%08X)" % m.PC)
copied = bytes(m.mem.get(vp.BASE + i, 0) for i in range(len(payload)))
check(copied == payload, "trampoline copy differs from the payload")
print("trampoline: ran at 0x%08X, copied %d bytes, jumped to 0x%08X" % (IPLOAD, len(payload), m.PC))

# ------------------------------------------------------------------ 4. run the whole guest
small = dict(params)
small["BUSY_L"] = 100                # the real value is checked above; a short loop keeps the run fast
pa, pt, ptramp, ppayload = vp.build_objects(small)
m = sh2sim.Machine()
m.load(vp.BASE, ppayload)
m.PC = pa.syms["start"]
PF = params["PHASE_FRAMES"]
frames = []
total_frames = PF * vp.N_PHASES + 2 * PF
for f in range(total_frames):
    m.run_frame()
    frames.append((m.log, m.events, m.rd(vp.STATE, 4, log=False)))
# frames[f] holds what the guest did between wake f-1 and sleep f. Phase p is entered during the
# wake that is number PF*p (counting from 1), so frame index PF*p belongs to the phase's first frame.

def phase_of(f):
    return frames[f][2]

seq_ok = True
for f in range(1, total_frames):
    want = ((f) // PF) % vp.N_PHASES
    if phase_of(f) != want:
        seq_ok = False
        check(False, "frame %d: phase %d, expected %d" % (f, phase_of(f), want))
        break
print("phase sequence: %d frames, %d phases then wrap: %s" % (total_frames, vp.N_PHASES, "ok" if seq_ok else "BAD"))
# the handler of the last delivered interrupt has not run yet when the last frame ends
check(m.rd(vp.STATE + 4, 4, log=False) == total_frames - 1, "VBlank count in state is %d" % m.rd(vp.STATE + 4, 4, log=False))


def first_frame(p, rnd=0):
    return PF * (p + rnd * vp.N_PHASES)


def events_of(p):
    return frames[first_frame(p)][1]


want_smpc = {0: [0x07, 0x03], 9: [0x02], 10: [0x03], 11: [0x07, 0x06], 12: [0x07, 0x06], 13: [0x07]}
for p in range(vp.N_PHASES):
    ev = events_of(p)
    cmds = [e[1] for e in ev if e[0] == "smpc_cmd"]
    check(cmds == want_smpc.get(p, []), "phase %d (%s): SMPC commands %s, expected %s" % (
        p, vp.PHASE_NAMES[p], [hex(c) for c in cmds], [hex(c) for c in want_smpc.get(p, [])]))

# final register values per phase entry frame
def last_reg(ev, addr):
    v = None
    for e in ev:
        if e[0] == "reg" and e[1] == addr:
            v = e[3]
    return v

for p in range(vp.N_PHASES):
    ev = events_of(p)
    ptmr = last_reg(ev, 0x05D00004)
    check(ptmr == (2 if p in (4, 5, 8) else 0), "phase %d: PTMR write %s" % (p, ptmr))
    den = last_reg(ev, 0x05FE0010)
    check(den == (0x100 if p in (6, 7, 8) else 0), "phase %d: D0EN final %s" % (p, den))
    if p in (6, 7, 8):
        nbytes = params["DMA_SMALL_BYTES"] if p == 7 else params["DMA_BIG_BYTES"]
        check(last_reg(ev, 0x05FE0008) == nbytes, "phase %d: D0C %s != %d" % (p, last_reg(ev, 0x05FE0008), nbytes))
        check(last_reg(ev, 0x05FE0014) == 1, "phase %d: D0MD != 1 (VBlank-OUT start)" % p)
        check(last_reg(ev, 0x05FE0000) == vp.DMA_SRC and last_reg(ev, 0x05FE0004) == vp.DMA_DST, "phase %d: DMA addresses" % p)
        check(last_reg(ev, 0x05FE000C) == 0x101, "phase %d: D0AD" % p)
        check(ev[-1][:2] == ("reg", 0x05FE0010) or any(e[1] == 0x05FE0010 and e[3] == 0x100 for e in ev if e[0] == "reg"),
              "phase %d: enable not written" % p)

# VDP1 tables as written at the end of the phase's first frame
def table(n):
    return [(m.mem.get(0x05C00000 + 32 * i, 0) << 8) | m.mem.get(0x05C00000 + 32 * i + 1, 0) for i in range(n + 1)]

# the table is rewritten at each phase entry, so check it in the machine state captured per phase:
for p, n in ((4, params["VDP1_ONE_CMDS"]), (5, params["VDP1_MANY_CMDS"]), (8, params["DMA_DRAW_CMDS"])):
    mm = sh2sim.Machine()
    mm.load(vp.BASE, ppayload)
    mm.PC = pa.syms["start"]
    for _ in range(PF * p + 2):
        mm.run_frame()
    words = [(mm.mem.get(0x05C00000 + 32 * i, 0) << 8) | mm.mem.get(0x05C00000 + 32 * i + 1, 0) for i in range(n + 1)]
    check(words[:n] == [0x000A] * n and words[n] == 0x8000, "phase %d: VDP1 table is not %d x 0x000A + END" % (p, n))
    # every 0x000A is SET LOCAL COORDINATE (cost 0) and the END is a fetch: commands = n+1, work = 16*(n+1)
    print("phase %d (%s): VDP1 table %d commands + END -> fetched %d, work cycles %d" % (p, vp.PHASE_NAMES[p], n, n + 1, 16 * (n + 1)))

check(params["VDP1_ONE_CMDS"] + 1 == 2 and 16 * (params["VDP1_ONE_CMDS"] + 1) == 32, "EXPECTED phase 4 numbers")
check(16 * (params["VDP1_MANY_CMDS"] + 1) == 1040, "EXPECTED phase 5 work is 1,040")
check(16 * (params["DMA_DRAW_CMDS"] + 1) == 6416, "EXPECTED phase 8 work is 6,416")

# 68000 programs in sound RAM
for p, prog in ((11, [0x4E72, 0x2700]), (12, [0x60FE])):
    mm = sh2sim.Machine()
    mm.load(vp.BASE, ppayload)
    mm.PC = pa.syms["start"]
    for _ in range(PF * p + 2):
        mm.run_frame()
    words = [(mm.mem.get(0x05A00000 + i, 0) << 8) | mm.mem.get(0x05A00000 + i + 1, 0) for i in range(0, 8, 2)]
    check(words == [0, 0x0800, 0, 0x0100], "phase %d: 68000 vectors %s" % (p, words))
    pw = [(mm.mem.get(0x05A00100 + 2 * i, 0) << 8) | mm.mem.get(0x05A00100 + 2 * i + 1, 0) for i in range(len(prog))]
    check(pw == prog, "phase %d: 68000 program %s" % (p, [hex(x) for x in pw]))

# ------------------------------------------------------------------ bus buckets
DEV = ["BIOS", "SMPC", "BACKUP", "WRAM_L", "CS0_OTHER", "ABUS_CS0", "ABUS_CS1", "ABUS_CD", "SCSP", "VDP1", "VDP2", "SCU", "WRAM_H"]


def bucket(addr27):
    for lo, hi, name in BUS_RANGES:
        if lo <= addr27 <= hi:
            return name
    return None


want_dev = {  # EXPECTED.md, phase BUS table: (device name, read K, write K)
    "SE_PERF_DEV_BIOS": (10, 0), "SE_PERF_DEV_SMPC": (20, 0), "SE_PERF_DEV_BACKUP": (30, 0),
    "SE_PERF_DEV_WRAM_L": (40, 11), "SE_PERF_DEV_CS0_OTHER": (50, 0), "SE_PERF_DEV_ABUS_CS0": (60, 0),
    "SE_PERF_DEV_ABUS_CS1": (70, 0), "SE_PERF_DEV_ABUS_CD": (80, 0), "SE_PERF_DEV_SCSP": (90, 15),
    "SE_PERF_DEV_VDP1": (100, 13), "SE_PERF_DEV_VDP2": (110, 14), "SE_PERF_DEV_SCU": (120, 0),
    "SE_PERF_DEV_WRAM_H": (130, 12), "X:SE_PERF_XDEV_ABUS_DUMMY": (140, 0), "X:SE_PERF_XDEV_UNMAPPED": (150, 0),
}
# the table in EXPECTED.md, parsed back out of the document so the two cannot drift apart
doc_rows = re.findall(r"^\|\s*\d+\s*\|[^|]*\|\s*(0x[0-9A-Fa-f]+)(?: / (0x[0-9A-Fa-f]+))?\s*\|[^|]*\|\s*(\d+)\s*\|\s*(\d*)\s*\|", exp, re.M)
doc_total_r = sum(int(r[2]) for r in doc_rows)
doc_total_w = sum(int(r[3] or 0) for r in doc_rows)
prog_total_r = sum(c for a_, c, k in vp.BUS_TABLE if k != 2)
prog_total_w = sum(c for a_, c, k in vp.BUS_TABLE if k == 2)
check(len(doc_rows) == 15, "EXPECTED.md BUS table: parsed %d rows, expected 15" % len(doc_rows))
check((doc_total_r, doc_total_w) == (prog_total_r, prog_total_w), "EXPECTED.md BUS K totals %s != program %s" % (
    (doc_total_r, doc_total_w), (prog_total_r, prog_total_w)))
doc_addrs = set()
for r in doc_rows:
    doc_addrs.add(int(r[0], 16))
    if r[1]:
        doc_addrs.add(int(r[1], 16))
check(doc_addrs == {a_ for a_, c, k in vp.BUS_TABLE}, "EXPECTED.md BUS addresses differ from the program's table")

busf = first_frame(3) + 20            # a steady frame of the BUS phase
log = frames[busf][0]
got = {}
cached_wh_reads = 0
for p27, size, rw, ea in log:
    b = bucket(p27)
    if b == "SE_PERF_DEV_WRAM_H" and rw == "r" and not ea & 0x20000000:
        cached_wh_reads += 1          # a data-cache hit in steady state: not a bus request
        continue
    r, w = got.get(b, (0, 0))
    got[b] = (r + (rw == "r"), w + (rw == "w"))
for name, (kr, kw) in want_dev.items():
    gr, gw = got.get(name, (0, 0))
    if name == "SE_PERF_DEV_WRAM_H":
        check(gr == kr and gw >= kw and gw <= kw + 16,
              "BUS frame WRAM_H reads %d writes %d vs K %d/%d" % (gr, gw, kr, kw))
    else:
        check((gr, gw) == (kr, kw), "BUS frame %s: simulated (reads,writes) %s != K %s" % (name, (gr, gw), (kr, kw)))
idle = frames[first_frame(0) + 20][0]
others = {bucket(p27) for p27, s, rw, ea in idle} - {"SE_PERF_DEV_WRAM_H"}
check(not others, "IDLE phase touches devices other than work RAM high: %s" % others)
iw = sum(1 for p27, s, rw, ea in idle if rw == "w")
ir = sum(1 for p27, s, rw, ea in idle if rw == "r")
check(4 <= iw <= 16 and ir <= 8 + 12, "IDLE baseline work-RAM-high requests reads %d writes %d outside EXPECTED.md bands" % (ir, iw))
print("BUS phase: simulated requests per device match the K table (%d cached work-RAM reads assumed to hit); IDLE baseline reads %d writes %d" % (cached_wh_reads, ir, iw))

# every table address lands in the device EXPECTED.md says
addr_dev = {0x20000000: "BIOS", 0x20100061: "SMPC", 0x20180001: "BACKUP", 0x20200000: "WRAM_L", 0x20280000: "WRAM_L",
            0x20400000: "CS0_OTHER", 0x22000000: "ABUS_CS0", 0x24000000: "ABUS_CS1", 0x25890008: "ABUS_CD",
            0x25A7F000: "SCSP", 0x25A7F100: "SCSP", 0x25C7F000: "VDP1", 0x25C7F100: "VDP1", 0x25E7F000: "VDP2",
            0x25E7F100: "VDP2", 0x25FE00A0: "SCU", 0x26000000: "WRAM_H", 0x26080000: "WRAM_H",
            0x25000000: "X:SE_PERF_XDEV_ABUS_DUMMY", 0x25900000: "X:SE_PERF_XDEV_UNMAPPED"}
for a_, c, k in vp.BUS_TABLE:
    want = addr_dev[a_]
    want = want if want.startswith("X:") else "SE_PERF_DEV_" + want
    check(bucket(a_ & 0x07FFFFFF) == want, "table address 0x%08X is in bucket %s, not %s" % (a_, bucket(a_ & 0x07FFFFFF), want))
    check(a_ & 0x20000000, "table address 0x%08X is not an uncached alias" % a_)

# ------------------------------------------------------------------ 5b. disc builder refusals
import build_v1  # noqa: E402
with tempfile.TemporaryDirectory() as td:
    bogus = os.path.join(td, "ip.bin")
    open(bogus, "wb").write(b"SEGA SEGASATURN " + b"\0" * (build_v1.SYS_AREA - 16))
    r = subprocess.run([sys.executable, "-I", os.path.join(V1, "build_v1.py"), "--ip-bin", bogus, "--out", td],
                       capture_output=True, text=True)
    check(r.returncode != 0 and "IsSaturnDisc" in (r.stderr + r.stdout), "builder accepted an image without the expected security block")
    # a synthetic image whose security block hashes right is impossible without the real block, so the
    # builder's layout is exercised through its helper with the hash check bypassed
    img = bytearray(build_v1.SYS_AREA)
    img[0:16] = b"SEGA SEGASATURN "
    struct.pack_into(">I", img, 0xF0, 0x06004000)
    struct.pack_into(">I", img, 0xF4, 0x06004000)
    saved = build_v1.check_security
    build_v1.check_security = lambda s: None
    try:
        open(bogus, "wb").write(bytes(img))
        build_v1.build_disc(bogus, td, build_v1.DEFAULT_IP_LOAD, 0xF0, 0xF4)
    finally:
        build_v1.check_security = saved
    iso = open(os.path.join(td, "v1.iso"), "rb").read()
    check(len(iso) == 32 * 2048, "iso size %d" % len(iso))
    check(struct.unpack(">I", iso[0xF0:0xF4])[0] == 0x06002E00, "master PC field not patched to IP load + 0xE00")
    park = struct.unpack(">I", iso[0xF4:0xF8])[0]
    check(0x06002E00 < park < 0x06002E00 + len(tramp), "slave PC field is not the parking loop")
    check(iso[0xE00:0xE00 + len(tramp)] == tramp and iso[0xE00 + len(tramp):0xE00 + len(tramp) + len(payload)] == payload,
          "code not at IP offset 0xE00")
    check(iso[16 * 2048 + 1:16 * 2048 + 6] == b"CD001", "no ISO 9660 primary volume descriptor at sector 16")
    check(open(os.path.join(td, "v1.cue")).read().count("MODE1/2048") == 1, "cue")
    check(hashlib.sha256(iso[0x100:0xE00]).hexdigest() != build_v1.SECURITY_SHA256, "synthetic image must not carry the real block")

print()
if fails:
    print("%d check(s) failed" % len(fails))
    sys.exit(1)
print("V1 checks passed")
