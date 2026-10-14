# audit_hardware_map.py -- cross-check the decompiler's register table against Mednafen.
#
#   python3 -I audit_hardware_map.py <mednafen>/src/ss FrontEnd/src/Decompiler/SaturnHardwareMap.cpp
#
# For every register in SaturnHardwareMap.cpp it finds the Mednafen handler that serves the
# register at its width (SCU, VDP1, VDP2 and its renderer, SMPC, SCSP common block, CD block, SH-2
# on-chip by byte/word/longword) and says whether a read returns a value and a write takes effect
# there, then compares that with the table's access kind. The register table and its findings are
# Docs/Decompiler/HARDWARE_REGISTER_AUDIT.md. Needs Mednafen's scu.inc, vdp1.cpp, vdp2.cpp,
# vdp2_render.c (or .cpp), smpc.cpp, scsp.inc, cdb.cpp and sh7095.inc in the given directory.
# Cross-check SaturnHardwareMap.cpp's register table against Mednafen's register handlers.
import re, sys
MED, SRC = sys.argv[1], sys.argv[2]

def func_body(text, header_regex):
    m = re.search(header_regex, text)
    assert m, header_regex
    i = text.index('{', m.end())
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '{': depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0: return text[i:j+1]
    raise Exception('no end')

def split_branches(body):
    # on-chip handlers: if(A & 0x100) { high } else { low }
    i = body.index('if(A & 0x100)')
    j = body.index('{', i); depth = 0
    for k in range(j, len(body)):
        if body[k] == '{': depth += 1
        elif body[k] == '}':
            depth -= 1
            if depth == 0: break
    return body[j:k+1], body[k+1:]

def cases(body):
    # label -> body text shared by consecutive labels
    out = {}
    body = re.sub(r'case\s+(0x[0-9A-Fa-f]+)\s*\.\.\.\s*(0x[0-9A-Fa-f]+)\s*:', lambda m: ' '.join('case 0x%X:' % v for v in range(int(m.group(1),16), int(m.group(2),16)+1)), body)
    parts = re.split(r'(case\s+0x[0-9A-Fa-f]+\s*:|default\s*:)', body)
    pending = []
    for idx in range(1, len(parts), 2):
        label = parts[idx]; text = parts[idx+1] if idx+1 < len(parts) else ''
        if label.startswith('default'):
            pending = []
            continue
        pending.append(int(re.search(r'0x[0-9A-Fa-f]+', label).group(0), 16))
        stripped = re.sub(r'//[^\n]*', '', text).strip()
        if stripped == '':   # fall-through label
            continue
        for l in pending: out[l] = stripped
        pending = []
    return out

def meaningful_read(t):
    t2 = re.sub(r'\s+', ' ', t)
    return not re.fullmatch(r'(\*DB = 0;|\(\*DBV_p\) = 0;|ret = 0xFF;|ret = 0;)? ?break;.*', t2)

def meaningful_write(t):
    t2 = re.sub(r'\s+', ' ', t)
    return not re.fullmatch(r'(z->WDT\.RSTCSRM = 0;)? ?break;.*', t2)

med = {n: open(f'{MED}/{n}').read() for n in ['scu.inc','vdp2.cpp','smpc.cpp','vdp1.cpp','cdb.cpp','sh7095.inc','scsp.inc']}
R, W = {}, {}   # canonical address -> True/False

def add(dst, mapping, off_fn, pred):
    for lab, t in mapping.items():
        a = off_fn(lab)
        dst[a] = dst.get(a, False) or pred(t)

# SCU (u32)
add(R, cases(func_body(med['scu.inc'], r'static INLINE void SCU_RegRW_DB_u32_W0\(')), lambda l: 0x05FE0000 + l, meaningful_read)
add(W, cases(func_body(med['scu.inc'], r'static INLINE void SCU_RegRW_DB_u32_W1\(')), lambda l: 0x05FE0000 + l, meaningful_write)
# VDP2
add(R, cases(func_body(med['vdp2.cpp'], r'static INLINE uint16_t RegsRead\(')), lambda l: 0x05F80000 + l, meaningful_read)
# A VDP2 write reaches vdp2.cpp's RegsWrite (timing, windows) and the renderer's own RegsWrite
# (vdp2_render.c); RawRegs is for debugging. Writable when either decodes it.
import os
med['vdp2_render.c'] = open(f'{MED}/vdp2_render.c' if os.path.exists(f'{MED}/vdp2_render.c') else f'{MED}/vdp2_render.cpp').read()
add(W, cases(func_body(med['vdp2.cpp'], r'static INLINE void RegsWrite\(')), lambda l: 0x05F80000 + l, meaningful_write)
add(W, cases(func_body(med['vdp2_render.c'], r'static INLINE void RegsWrite\(')), lambda l: 0x05F80000 + l, meaningful_write)
# SMPC
add(R, cases(func_body(med['smpc.cpp'], r'uint8_t SMPC_Read\(')), lambda l: 0x00100000 + l*2 + 1, meaningful_read)
add(W, cases(func_body(med['smpc.cpp'], r'void SMPC_Write\(')), lambda l: 0x00100000 + l*2 + 1, meaningful_write)
# VDP1
add(R, cases(func_body(med['vdp1.cpp'], r'static INLINE uint16_t ReadReg\(')), lambda l: 0x05D00000 + l*2, meaningful_read)
add(W, cases(func_body(med['vdp1.cpp'], r'static INLINE void WriteReg\(')), lambda l: 0x05D00000 + l*2, meaningful_write)
# CD block
cdoff = lambda l: 0x05818000 if l == 0 else 0x05890000 + l*4
add(R, cases(func_body(med['cdb.cpp'], r'uint16_t CDB_Read\(')), cdoff, meaningful_read)
add(W, cases(func_body(med['cdb.cpp'], r'void CDB_Write_DBM\(')), cdoff, meaningful_write)
# SCSP common (u16): the "Common regs" switch inside the u16 read and write handlers
for hdr, dst, pred in ((r'INLINE void SS_SCSP_RW_u16_W0\(', R, meaningful_read), (r'INLINE void SS_SCSP_RW_u16_W1\(', W, meaningful_write)):
    b = func_body(med['scsp.inc'], hdr)
    b = b[b.index('Common regs'):]
    add(dst, cases(b), lambda l: 0x05B00400 + l*2, pred)
# SH-2 on-chip, per width
onchip = {}
for width in (8, 16, 32):
    rb = func_body(med['sh7095.inc'], r'static INLINE uint%d_t SH7095_OnChipRegRead_INLINE_u%d\(SH7095\* z, uint32_t A\)' % (width if width != 8 else 8, width))
    wb = func_body(med['sh7095.inc'], r'static NO_INLINE MDFN_HOT void SH7095_OnChipRegWrite_u%d\(' % width)
    rh, rl = split_branches(rb); wh, wl = split_branches(wb)
    r_, w_ = {}, {}
    add(r_, cases(rh), lambda l: 0xFFFFFF00 + l, meaningful_read); add(r_, cases(rl), lambda l: 0xFFFFFE00 + l, meaningful_read)
    add(w_, cases(wh), lambda l: 0xFFFFFF00 + l, meaningful_write); add(w_, cases(wl), lambda l: 0xFFFFFE00 + l, meaningful_write)
    onchip[width // 8] = (r_, w_)

# Our table
src = open(SRC).read()
blocks = {'kSmpc': 0x00100000, 'kScsp': 0x05B00000, 'kVdp1': 0x05D00000, 'kVdp2': 0x05F80000, 'kScu': 0x05FE0000,
          'kCdBlock': 0x05890000, 'kCdFifo': 0x05818000, 'kMinit': 0x01000000, 'kSinit': 0x01800000, 'kOnChip': 0xFFFFFE00}
sizes = {'kSmpc': 1, 'kScsp': 2, 'kVdp1': 2, 'kVdp2': 2, 'kScu': 4, 'kCdBlock': 2, 'kCdFifo': 4, 'kMinit': 2, 'kSinit': 2, 'kOnChip': 0}
rows = []
for name, base in blocks.items():
    body = re.search(r'const Reg ' + name + r'\[\] = \{(.*?)\};', src, re.S).group(1)
    for m in re.finditer(r'\{ (0x[0-9A-Fa-f]+), "(\w+)", (RW|RO|WO|KW)(?:, (\d+))?', body):
        off, reg, acc, sz = int(m.group(1), 16), m.group(2), m.group(3), m.group(4)
        size = int(sz) if sz and int(sz) else sizes[name]
        rows.append((base + off, reg, acc, size))

mismatch = []
lines = []
for a, reg, acc, size in sorted(rows):
    if a >= 0xFFFFFE00:
        rr, ww = onchip[size]
    else:
        rr, ww = R, W
    if a in (0x01000000, 0x01800000):
        mr, mw = False, True   # ss.cpp: writes trigger the other CPU's FRT capture, reads return nothing
    else:
        mr, mw = rr.get(a), ww.get(a)
    want_r = acc in ('RW', 'RO', 'KW'); want_w = acc in ('RW', 'WO')
    if acc == 'KW': mw_expect = False
    status = 'ok'
    notes = []
    if mr is None and mw is None: status = 'not decoded by Mednafen'
    else:
        if bool(mr) != want_r: notes.append('Mednafen read %s' % ('returns a value' if mr else 'returns 0/nothing'))
        if acc != 'KW' and bool(mw) != want_w: notes.append('Mednafen write %s' % ('takes effect' if mw else 'is ignored'))
        if notes: status = 'DIFFERS: ' + '; '.join(notes)
    lines.append('| `%08X` | %s | %d | %s | %s |' % (a, reg, size, acc, status))
    if status != 'ok': mismatch.append(lines[-1])
print('| Address | Register | Width | Ours | Mednafen |')
print('|---|---|---|---|---|')
print('\n'.join(lines))
print()
print('MISMATCHES (%d of %d):' % (len(mismatch), len(rows)))
print('\n'.join(mismatch))
