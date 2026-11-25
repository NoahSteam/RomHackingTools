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

The layout follows the documented system ID format (see "Layout" below). It was run on a real BIOS
(2026-11-25): it booted once the ISO root directory held a file, which is why it now always does.
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

# Layout, from the documented Saturn system ID format (still NOT run against a BIOS):
#   0x000..0x0FF  system ID (+0x40 area symbols, +0xE0 IP size, +0xE8/+0xEC stacks, +0xF0/+0xF4 1st read addr/size)
#   0x100..0xDFF  security code and its tables (the BIOS runs it at 0x06002100 after loading the IP to 0x06002000)
#   0xE00..       one 0x20-byte area-code block per region letter in +0x40; the BIOS validates them and
#                 execution falls through them
#   0xE00+0x20*n  the application's initial code (this guest); it never returns and loads nothing itself.
#                 The ISO root directory holds one small file (V1.BIN) only because the BIOS will not boot a
#                 disc with an empty root directory; its content is the payload's first bytes (see build_disc)
DEFAULT_IP_LOAD = 0x06002000
AREA_BLOCK = 0x20
AREA_BASE = 0xE00
GUEST_BASE = 0x06010000     # where the trampoline copies the payload (v1_program.BASE); header "1st read address"
DEFAULT_STACK_M = 0x06004000
DEFAULT_STACK_S = 0x06001E00


def area_blocks(sysarea):
    """(n, [offsets]) -- one block per non-space letter of the area-symbols field; each must look like one."""
    letters = [c for c in sysarea[0x40:0x4A] if c not in (0x20, 0x00)]
    n = len(letters)
    if n == 0:
        raise SystemExit("area-symbols field (+0x40) is empty: not a usable system ID")
    offs = [AREA_BASE + AREA_BLOCK * i for i in range(n)]
    for o in offs:
        if b"For " not in sysarea[o:o + AREA_BLOCK]:
            raise SystemExit("area block at +0x%X does not contain 'For ' (area symbols %r, n=%d): the "
                             "image is not laid out like a system ID, refusing to overwrite code after it"
                             % (o, bytes(sysarea[0x40:0x4A]), n))
    return n, offs


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


def check_security(sys_area, want=SECURITY_SHA256):
    h = hashlib.sha256(sys_area[0x100:0xE00]).hexdigest()
    if h != want:
        raise SystemExit("bytes 0x100..0xDFF of the supplied image are not the block Mednafen's "
                         "IsSaturnDisc() accepts (sha256 %s); the result would not be recognised as a "
                         "Saturn disc. Use an unmodified IP.BIN / disc image." % h)


def probe(path):
    s = read_sys_area(path)
    print("security block (0x100..0xDFF) sha256 matches Mednafen:",
          hashlib.sha256(s[0x100:0xE00]).hexdigest() == SECURITY_SHA256)
    names = {0xE0: "IP size", 0xE8: "master stack", 0xEC: "slave stack", 0xF0: "1st read address", 0xF4: "1st read size"}
    for off in range(0xD0, 0x100, 4):
        print("  +0x%02X %-17s = 0x%08X" % (off, names.get(off, ""), struct.unpack(">I", s[off:off + 4])[0]))
    print("  product %r  version %r  date %r  area symbols %r" % (s[0x20:0x2A], s[0x2A:0x30], s[0x30:0x38], s[0x40:0x4A]))
    n, offs = area_blocks(s)
    print("  area blocks n=%d:" % n)
    for o in offs:
        print("    +0x%X %r" % (o, bytes(s[o:o + AREA_BLOCK]).rstrip(b"\0")))
    print("  application code would start at IP offset 0x%X (runs at 0x%08X with the IP at 0x%08X)"
          % (AREA_BASE + AREA_BLOCK * n, DEFAULT_IP_LOAD + AREA_BASE + AREA_BLOCK * n, DEFAULT_IP_LOAD))


FILE_NAME = b"V1.BIN;1"
FILE_SECTOR = 21                # 16 PVD, 17 terminator, 18/19 path tables, 20 root directory, 21 the file


def iso9660(total_sectors, file_data):
    """Sectors 16..: a valid ISO 9660 volume (PVD, terminator, path tables, root directory) whose root
    directory holds exactly one file, V1.BIN;1, extent at sector FILE_SECTOR, content `file_data`.
    Without a file the BIOS does not boot the disc: it drops to its CD player."""
    assert 0 < len(file_data) <= 2048
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
    # file record: length, xattr, extent (both-endian), size (both-endian), date, flags 0 (plain file),
    # unit size 0, gap 0, volume sequence 1 (both-endian), name length, name, pad to an even length
    rec = (bytes([0, 0]) + both32(FILE_SECTOR) + both32(len(file_data)) + date7 + bytes([0, 0, 0]) + both16(1)
           + bytes([len(FILE_NAME)]) + FILE_NAME)
    rec += b"\0" * (len(rec) & 1)
    rec = bytes([len(rec)]) + rec[1:]
    rd[68:68 + len(rec)] = rec
    fsec = bytearray(2048)
    fsec[:len(file_data)] = file_data
    return bytes(pvd) + bytes(term) + bytes(ptl) + bytes(ptm) + bytes(rd) + bytes(fsec)


def code_offset(n):
    return AREA_BASE + AREA_BLOCK * n


def build_disc(ip_path, outdir, ip_load, security_sha=SECURITY_SHA256):
    s = bytearray(read_sys_area(ip_path))
    check_security(bytes(s), security_sha)
    n, offs = area_blocks(bytes(s))
    tramp, payload, syms = v1_program.build()
    code = tramp + payload
    off = code_offset(n)
    ip_size = (off + len(code) + 0x7FF) & ~0x7FF
    if ip_size > 0x8000:
        raise SystemExit("guest (%d bytes at +0x%X) does not fit the 32 KB initial program" % (len(code), off))
    entry = ip_load + off
    s[0x20:0x2A] = b"V1VALID".ljust(10)
    s[0x2A:0x30] = b"V1.000"
    s[0x30:0x38] = b"20260101"
    s[0x60:0xD0] = b"SATURN EXPLORER V1 PROFILER VALIDATION".ljust(0x70)
    struct.pack_into(">I", s, 0xE0, ip_size)
    if struct.unpack(">I", s[0xE8:0xEC])[0] == 0:
        struct.pack_into(">I", s, 0xE8, DEFAULT_STACK_M)
    if struct.unpack(">I", s[0xEC:0xF0])[0] == 0:
        struct.pack_into(">I", s, 0xEC, DEFAULT_STACK_S)
    # 1st read address / size: unchanged from the real run (address 0x06010000 = where the IP code copies the
    # payload, size 0 = the BIOS is not asked to load the file). The root directory still needs a real file or
    # the BIOS refuses to boot the disc (real run 2026-11-25). V1.BIN is the first bytes of the payload, so
    # whatever order a loader and the IP copy happen in, 0x06010000 ends up holding payload bytes.
    struct.pack_into(">I", s, 0xF0, GUEST_BASE)
    struct.pack_into(">I", s, 0xF4, 0)
    s[off:] = b"\0" * (SYS_AREA - off)             # bytes 0..off (system ID, security code, area blocks) stay the user's
    s[off:off + len(code)] = code
    vol = iso9660(32, payload[:2048])
    img = bytes(s) + vol
    img += b"\0" * (32 * 2048 - len(img))
    os.makedirs(outdir, exist_ok=True)
    iso = os.path.join(outdir, "v1.iso")
    open(iso, "wb").write(img)
    open(os.path.join(outdir, "v1.cue"), "w").write(
        'FILE "v1.iso" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n')
    info = {"entry": hex(entry), "area_blocks": n, "code_offset": hex(off), "ip_size": hex(ip_size),
            "symbols": {k: hex(v) for k, v in syms.items()
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
        build_disc(a.ip_bin, a.out, a.ip_load)
    else:
        ap.print_help()
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
