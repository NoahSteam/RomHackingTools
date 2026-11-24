#!/usr/bin/env python3
"""Build the V1 validation guest.

    build_v1.py --raw OUT.bin                    the guest only (trampoline + payload), no disc
    build_v1.py --ip-bin IP.BIN --out DIR        a bootable disc image: DIR/v1.iso + DIR/v1.cue
    build_v1.py --probe IP.BIN                   print the header fields this script assumes about

The Saturn boot header carries Sega's licensing/security block (bytes 0x100..0xDFF of the first
sector area). That block is Sega-copyrighted and is NOT in this repository and is never downloaded.
`--ip-bin` takes a file you already own -- an IP.BIN, or any Saturn disc image (2048- or 2352-byte
sectors, first sector at offset 0) -- and copies only that block (after proving it is the block
Mednafen recognises, by SHA-256) and the area/peripheral header bytes from it.

Everything about HOW the BIOS boots the result is an assumption listed in README-style form in
EXPECTED/CHECKLIST and run_v1.sh; it could not be checked here (no BIOS). See ASSUMPTIONS below.
"""
import argparse
import hashlib
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import v1_program  # noqa: E402

# Mednafen's IsSaturnDisc() (ss.cpp): SHA-256 of sector-area bytes 0x100..0xDFF, plus the magic.
SECURITY_SHA256 = "96b8ea48819cfa589f24c40aa149c224c420dccf38b730f00156efe25c9bbc8f"
SYS_AREA = 16 * 2048

# ASSUMPTIONS (unverified here, no BIOS available; see run_v1.sh "if it does not boot"):
#  A1 the BIOS loads the initial program (IP) to 0x06002000 and the code at IP offset 0xE00 runs
#     at 0x06002E00;
#  A2 the header words at 0xF0 / 0xF4 are the master / slave start addresses.
# Both are overridable; --probe prints what a real IP.BIN says so the user can judge.
DEFAULT_IP_LOAD = 0x06002000
CODE_OFFSET = 0xE00


def read_sys_area(path):
    d = open(path, "rb").read()
    for off, stride in ((0, 2048), (16, 2352), (24, 2352)):
        if d[off:off + 16] == b"SEGA SEGASATURN ":
            if stride == 2048:
                return d[:SYS_AREA]
            out = b""
            for i in range(16):
                out += d[off + i * 2352: off + i * 2352 + 2048]
            return out
    raise SystemExit("%s: no 'SEGA SEGASATURN ' header at offset 0, 16 or 24" % path)


def check_security(sys_area):
    h = hashlib.sha256(sys_area[0x100:0xE00]).hexdigest()
    if h != SECURITY_SHA256:
        raise SystemExit("bytes 0x100..0xDFF of the supplied image are not the block Mednafen's "
                         "IsSaturnDisc() accepts (sha256 %s); the result would not be recognised as a "
                         "Saturn disc. Use an unmodified IP.BIN / disc image." % h)


def probe(path):
    s = read_sys_area(path)
    print("security block sha256 matches Mednafen:", hashlib.sha256(s[0x100:0xE00]).hexdigest() == SECURITY_SHA256)
    for off in range(0xD0, 0x100, 4):
        print("  +0x%02X = 0x%08X" % (off, struct.unpack(">I", s[off:off + 4])[0]))
    print("  product %r  version %r  date %r  area %r" % (s[0x20:0x2A], s[0x2A:0x30], s[0x30:0x38], s[0x40:0x4A]))
    print("Assumed: master PC at +0xF0, slave PC at +0xF4, code runs at 0x%08X + 0x%X." % (DEFAULT_IP_LOAD, CODE_OFFSET))
    print("If +0xF0 above is not an address in 0x06000000..0x060FFFFF the layout assumption is suspect.")


def iso9660(total_sectors):
    """Sectors 16..: a valid, empty ISO 9660 volume (PVD, terminator, path tables, root directory)."""
    def both32(v): return struct.pack("<I", v) + struct.pack(">I", v)
    def both16(v): return struct.pack("<H", v) + struct.pack(">H", v)
    def pad(s, n): return s.ljust(n, b" ")[:n]
    date7 = bytes([126, 1, 1, 0, 0, 0, 0])
    root = (bytes([34, 0]) + both32(20) + both32(2048) + date7 + bytes([2, 0, 0]) + both16(1) + bytes([1, 0]))
    assert len(root) == 34
    pvd = bytearray(2048)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = pad(b"", 32)
    pvd[40:72] = pad(b"V1VALID", 32)
    pvd[80:88] = both32(total_sectors)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(2048)
    pvd[132:140] = both32(10)
    pvd[140:144] = struct.pack("<I", 18)
    pvd[148:152] = struct.pack(">I", 19)
    pvd[156:190] = root
    for off in (190, 318, 446, 574):
        pvd[off:off + 128] = pad(b"", 128)
    for off in (813, 830, 847, 864):
        pvd[off:off + 17] = b"0" * 16 + b"\0"
    pvd[881] = 1
    term = bytearray(2048)
    term[0] = 255
    term[1:6] = b"CD001"
    term[6] = 1
    ptl = bytearray(2048)
    ptl[0:10] = bytes([1, 0]) + struct.pack("<I", 20) + struct.pack("<H", 1) + bytes([0, 0])
    ptm = bytearray(2048)
    ptm[0:10] = bytes([1, 0]) + struct.pack(">I", 20) + struct.pack(">H", 1) + bytes([0, 0])
    rd = bytearray(2048)
    dot = bytes([34, 0]) + both32(20) + both32(2048) + date7 + bytes([2, 0, 0]) + both16(1) + bytes([1, 0])
    dotdot = dot[:-1] + bytes([1])
    rd[0:34] = dot
    rd[34:68] = dotdot
    return bytes(pvd) + bytes(term) + bytes(ptl) + bytes(ptm) + bytes(rd)


def build_disc(ip_path, outdir, ip_load, master_field, slave_field):
    s = bytearray(read_sys_area(ip_path))
    check_security(bytes(s))
    tramp, payload, syms = v1_program.build()
    code = tramp + payload
    if CODE_OFFSET + len(code) > SYS_AREA:
        raise SystemExit("guest does not fit the system area")
    entry = ip_load + CODE_OFFSET
    # the trampoline's slave_park label: re-assemble just to learn its offset
    from sh2asm import Asm
    t = Asm(0)
    t.parse(".equ PAYLOAD_WORDS, %d\n" % (len(payload) // 4) + v1_program.TRAMP % (v1_program.BASE + 0x20000000, v1_program.BASE))
    t.assemble()
    park = entry + t.syms["slave_park"]
    old = [struct.unpack(">I", s[f:f + 4])[0] for f in (master_field, slave_field)]
    if not 0x06000000 <= old[0] <= 0x060FFFFF:
        print("WARNING: the supplied header's master-PC word at +0x%X is 0x%08X, not a work-RAM-high "
              "address: the header-layout assumption (A2) is suspect." % (master_field, old[0]), file=sys.stderr)
    s[0x20:0x2A] = b"V1VALID".ljust(10)
    s[0x2A:0x30] = b"V1.000"
    s[0x30:0x38] = b"20260101"
    s[0x60:0xD0] = b"SATURN EXPLORER V1 PROFILER VALIDATION".ljust(0x70)
    s[master_field:master_field + 4] = struct.pack(">I", entry)
    s[slave_field:slave_field + 4] = struct.pack(">I", park)
    s[CODE_OFFSET:] = b"\0" * (SYS_AREA - CODE_OFFSET)
    s[CODE_OFFSET:CODE_OFFSET + len(code)] = code
    vol = iso9660(32)
    img = bytes(s) + vol
    img += b"\0" * (32 * 2048 - len(img))
    os.makedirs(outdir, exist_ok=True)
    iso = os.path.join(outdir, "v1.iso")
    open(iso, "wb").write(img)
    open(os.path.join(outdir, "v1.cue"), "w").write(
        'FILE "v1.iso" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n')
    info = {"entry": hex(entry), "slave_park": hex(park), "master_field_before": hex(old[0]),
            "slave_field_before": hex(old[1]), "symbols": {k: hex(v) for k, v in syms.items()
                                                           if k in ("start", "func_a", "func_b", "f_busy", "f_bus", "vbin")}}
    json.dump(info, open(os.path.join(outdir, "v1_symbols.json"), "w"), indent=1)
    print(json.dumps(info, indent=1))
    print("wrote", iso, "and v1.cue")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--raw", metavar="OUT.bin", help="write trampoline+payload only")
    ap.add_argument("--ip-bin", metavar="PATH")
    ap.add_argument("--out", default=".")
    ap.add_argument("--probe", metavar="PATH")
    ap.add_argument("--ip-load", type=lambda x: int(x, 0), default=DEFAULT_IP_LOAD)
    ap.add_argument("--master-field", type=lambda x: int(x, 0), default=0xF0)
    ap.add_argument("--slave-field", type=lambda x: int(x, 0), default=0xF4)
    a = ap.parse_args()
    if a.probe:
        probe(a.probe)
    elif a.raw:
        tramp, payload, syms = v1_program.build()
        open(a.raw, "wb").write(tramp + payload)
        json.dump({"tramp_len": len(tramp), "payload_len": len(payload),
                   "base": hex(v1_program.BASE), "symbols": {k: v for k, v in syms.items()}},
                  open(a.raw + ".json", "w"), indent=1)
        print("wrote %s (%d bytes: trampoline %d + payload %d)" % (a.raw, len(tramp) + len(payload), len(tramp), len(payload)))
    elif a.ip_bin:
        build_disc(a.ip_bin, a.out, a.ip_load, a.master_field, a.slave_field)
    else:
        ap.print_help()
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
