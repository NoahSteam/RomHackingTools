#!/usr/bin/env python3
# derive_handler_cases.py -- derive, from Mednafen's own sources, which switch cases each register
# handler decodes at each width and direction, as the independent check of the hardware width table
# (Docs/FunctionalityVerification/15_DECOMPILER_STEP5.md, "Area 5, round 4").
#
#   python3 -I derive_handler_cases.py <mednafen>/mednafen/ss               # prints the cases file
#   python3 -I derive_handler_cases.py <mednafen>/mednafen/ss --check FrontEnd/tests/MednafenHandlerCases.txt
#
# Why: HardwareWidthTable.golden.txt is printed from WidthCellFor itself, so a rule that disagrees
# with a handler is a wrong golden line (A5-18: eleven DMAC cells claimed a word read at +2 reaches
# the register; sh7095.inc has no case there). This script reads the handlers, not the rules: it
# never looks at SaturnHardwareMap.cpp. FrontEnd/tests/HardwareWidthTableTests.cpp reads the cases
# file it writes and compares it, cell by cell, with the table (the Mednafen sources are not in the
# repository, so the checked-in file is the bridge; --check re-derives it and fails when it is stale).
#
# What a line holds:
#   <handler> <attributes> : <group> ; <group> ...
#   handler     onchip.<rd|wr><8|16|32>.<hi|lo> (sh7095.inc, hi = A & 0x100), scu.<rd|wr><8|16|32> (scu.inc
#               SCU_RegRW_DB_u*_W0 / _W1), vdp1.rd/wr, vdp2.rd/wr, vdp2r.wr, smpc.rd/wr, cd.rd/wr, scsp.rd/wr
#   attributes  pex=<0|1>   the branch raises an address error (SH7095_SetPEX) before it decodes
#               sw=<0|1>    the branch decodes anything (a switch); an address-error-only branch has none
#               mask=<hex|am>  the mask the offset gets (A &= 0xFC) or `am`: the low byte as it is
#   group       <label>,<label>...=<flags>   labels that share one body (fall-through): one register
#   flags       M  the case does something with the access (a read returns a value that is not a
#                  constant, a write stores or acts); - decoded and inert (a bare break, ret = 0xFF)
#               E  a read has a side effect (a flag armed, a latch, a FIFO pop)
#               D  a write starts a division   S  a write starts a DMA   C  a write sets CCR
#               G  a write starts VDP1 drawing   F  a write stores a constant whatever the value (SF = true)
# Timing bookkeeping (MA_until, FRT/WDT updates, interrupt recalculation) is not an effect.
import hashlib, os, re, sys

def strip_comments(t):
    t = re.sub(r'/\*.*?\*/', ' ', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)

def read_source(med, *names):
    for n in names:
        p = os.path.join(med, n)
        if os.path.exists(p):
            return n, open(p, encoding='utf-8', errors='replace').read()
    raise SystemExit('none of %s in %s' % (names, med))

def match_brace(text, i):
    assert text[i] == '{'
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '{': depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0: return j
    raise SystemExit('unbalanced braces')

def func_body(text, header_regex):
    m = re.search(header_regex, text)
    if not m: raise SystemExit('function not found: ' + header_regex)
    i = text.index('{', m.end())
    return text[i:match_brace(text, i) + 1]

def split_hi_lo(body):
    # on-chip handlers: if(A & 0x100) { hi } else { lo }
    i = body.index('if(A & 0x100)')
    j = body.index('{', i)
    k = match_brace(body, j)
    e = body.index('{', k)
    return body[j:k + 1], body[e:match_brace(body, e) + 1]

def switch_body(branch, var=r'(?:A|Am|A\s*&\s*0xFF)'):
    m = re.search(r'switch\(\s*' + var + r'\s*\)', branch)
    if not m: return None
    i = branch.index('{', m.end())
    return branch[i:match_brace(branch, i) + 1]

def expand(label):
    return label

def parse_cases(body):
    """[(labels, text)] -- labels that share a body (a fall-through run), in source order."""
    body = strip_comments(body)
    def rng(m):
        a, b = int(m.group(1), 0), int(m.group(2), 0)
        return ' '.join('case 0x%X:' % v for v in range(a, b + 1))
    body = re.sub(r'case\s+(0x[0-9A-Fa-f]+|\d+)\s*\.\.\.\s*(0x[0-9A-Fa-f]+|\d+)\s*:', rng, body)
    parts = re.split(r'(case\s+(?:0x[0-9A-Fa-f]+|\d+)\s*:|default\s*:)', body)
    out, pending = [], []
    for idx in range(1, len(parts), 2):
        label, text = parts[idx], parts[idx + 1] if idx + 1 < len(parts) else ''
        if label.startswith('default'):
            pending = []
            continue
        pending.append(int(re.search(r'0x[0-9A-Fa-f]+|\d+', label).group(0), 0))
        if text.strip() == '':
            continue
        out.append((pending, text))
        pending = []
    return out

TIMING = ('MA_until', 'FRT_WDT_Update', 'RecalcPendingIntPEX', 'FRT_WDT_Recalc_NET', 'FRT_CheckOCR',
          'DMA_Update', 'DMA_RecalcRunning', 'SS_SetEventNT', 'VDP2_Update', 'VDP1_Update', 'RecalcIRQOut',
          'CDB_Update', 'UpdateIOBus', 'SMPC_Update')

def strip_control(s):
    # "if(cond) stmt", "else stmt": the statement; a condition alone is nothing.
    while True:
        m = re.match(r'^(?:else\s+)?(?:if|while)\s*\(', s)
        if m:
            depth, i = 0, m.end() - 1
            while i < len(s):
                if s[i] == '(': depth += 1
                elif s[i] == ')':
                    depth -= 1
                    if depth == 0: break
                i += 1
            s = s[i + 1:].strip()
            continue
        if s.startswith('else '):
            s = s[5:].strip()
            continue
        return s

def statements(text):
    text = strip_comments(text)
    text = re.sub(r'for\s*\([^)]*\)', ' ', text)   # loop headers hold semicolons
    text = text.replace('{', ';').replace('}', ';')
    out = []
    for s in text.split(';'):
        s = strip_control(' '.join(s.split()))
        if not s or s == 'break':
            continue
        out.append(s)
    return out

# A value expression that also changes state: a post-increment of a pointer (SCU_PDD's RA++), the MIDI input
# FIFO's pop. Everything else on the right of a value assignment is a pure read.
EFFECT_IN_VALUE = ('++', '--', 'ReadInput')

DECL = r'(?:const\s+)?(?:u?int\d+_t|unsigned|int|bool|DMALevelS)\b'

def locals_of(text):
    names = set()
    for m in re.finditer(r'(?:^|[;{}\s])' + DECL + r'\s*\*?\s*(?:const\s+)?(\w+)\s*(?:=|;|\[)', strip_comments(text)):
        names.add(m.group(1))
    return names

def classify_read(text):
    m, e = False, False
    loc = locals_of(text)
    for s in statements(text):
        if any(t in s for t in EFFECT_IN_VALUE):
            m = e = True   # the value is taken off a FIFO, or a pointer advances
            continue
        if re.match(r'^' + DECL, s):
            continue   # a declaration with an initialiser
        if any(t in s for t in TIMING) and not s.startswith('ret'): continue
        k = re.match(r'^\*?\(?\*?(?:ret|DB|DBV_p)\)?\s*=\s*(0x[0-9A-Fa-f]+|\d+)$', s)
        if k:   # a constant: the filler a register that is not there returns, or a value (SCU_VER)
            if int(k.group(1), 0) not in (0, 0xFF, 0xFFFF, 0xFFFFFFFF): m = True
            continue
        if re.match(r'^(ret|\*DB|\(\*DBV_p\)|\*\(DBV_p\))\s*(=|>>=|\|=|&=)', s):
            m = True
            continue
        if re.match(r'^return\b', s): m = True; continue
        a = re.match(r'^[\*\(]*(\w+)[^=]*?(?:[\|&\^+\-<>]?=)(?!=)', s)
        if a and a.group(1) in loc: continue            # an assignment to a local
        e = True
        m = True
    return ('M' if m else '') + ('E' if e else '')

def classify_write(text):
    flags = ''
    stm = [s for s in statements(text) if not any(t in s for t in TIMING)]
    if any('DIVU_' in s for s in stm): flags += 'D'
    if any('DMA_StartSG' in s for s in stm): flags += 'S'
    if any('SetCCR' in s for s in stm): flags += 'C'
    if any('StartDrawing' in s for s in stm): flags += 'G'
    # F: a store of a constant that does not depend on the value written (SMPC SF = true, CommandPending = true)
    if any(re.match(r'^[\w\.\[\]>-]+\s*=\s*(true|false|0x[0-9A-Fa-f]+|\d+)$', s) and not re.search(r'\bV\b', s) for s in stm): flags += 'F'
    stm = [s for s in stm if not re.match(r'^z->WDT\.RSTCSRM\s*=\s*0$', s)]
    return ('M' if stm else '') + flags

def fmt_groups(groups, classify):
    parts = []
    for labels, text in groups:
        f = classify(text) or '-'
        parts.append('%s=%s' % (','.join('%X' % l for l in sorted(labels)), f))
    return ' ; '.join(parts)

def line(name, attrs, groups, classify):
    return '%s %s : %s' % (name, ' '.join('%s=%s' % kv for kv in attrs), fmt_groups(groups, classify))

def onchip_lines(inc):
    out = []
    for width in (8, 16, 32):
        rb = func_body(inc, r'static INLINE uint%d_t SH7095_OnChipRegRead_INLINE_u%d\(SH7095\* z, uint32_t A\)' % (width, width))
        wb = func_body(inc, r'static NO_INLINE MDFN_HOT void SH7095_OnChipRegWrite_u%d\(' % width)
        for tag, body, classify in (('rd', rb, classify_read), ('wr', wb, classify_write)):
            hi, lo = split_hi_lo(body)
            for half, br in (('hi', hi), ('lo', lo)):
                sw = switch_body(br)
                m = re.search(r'A\s*&=\s*(0x[0-9A-Fa-f]+)', br)
                mask = ('%X' % int(m.group(1), 16)) if m else 'am'
                attrs = [('pex', int('SH7095_SetPEX' in br)), ('sw', int(sw is not None)), ('mask', mask)]
                out.append(line('onchip.%s%d.%s' % (tag, width, half), attrs, parse_cases(sw) if sw else [], classify))
    return out

def bus_lines(inc):
    # The CPU's bus dispatch (sh7095.inc MemRead / MemWrite): facts about the partitions, as present(1) or absent(0).
    i, j, k = inc.index('#define MemRead('), inc.index('#define MemWrite('), inc.index('#define CHECK_EXIT_RESUME')
    unmacro = lambda t: strip_comments(re.sub(r'\\\n', '\n', t))   # a macro body: drop its line continuations
    rd, wr = unmacro(inc[i:j]), unmacro(inc[j:k])
    def has(pat, text): return int(re.search(pat, text, re.S) is not None)
    out = []
    out.append('bus.misaligned rd_pex=%d wr_pex=%d align_down=%d' % (
        has(r'A\s*&\s*\(sizeof\(T\)\s*-\s*1\)\)\)\s*\{[^}]*SH7095_SetPEX', rd),
        has(r'A\s*&\s*\(sizeof\(T\)\s*-\s*1\)\)\)\s*\{[^}]*SH7095_SetPEX', wr),
        has(r'A\s*&=\s*~\(sizeof\(T\)\s*-\s*1\)', rd) & has(r'A\s*&=\s*~\(sizeof\(T\)\s*-\s*1\)', wr)))
    out.append('bus.cache rd_cached_partition=%d rd_hit_no_bus=%d rd_miss_fills_line=%d wr_through=%d' % (
        has(r'case\s+0:\s*if\(MDFN_LIKELY\(CacheEnabled\)\)', rd),
        has(r'SH7095_Cache_FindWay.*?retval\s*=\s*\*\(T\*\)&z->CacheData', rd),
        has(r'ExtBusRead\(uint32_t,\s*false,\s*\(A\s*&~\s*0xF\)\s*\+\s*di\).*?for\(unsigned i = 4; i < 16; i \+= 4\).*?ExtBusRead\(uint32_t,\s*true', rd),
        has(r'case\s+0:\s*if\(MDFN_LIKELY\(CacheEnabled\)\).*?SH7095_Cache_WriteUpdate_u32\(z, A, V\);\s*\}\s*case\s+1:.*?ExtBusWrite', wr)))
    out.append('bus.partition7 onchip_rd=%d onchip_wr=%d' % (
        has(r'case\s+7:\s*retval\s*=\s*OnChipRegRead\(T,\s*unmasked_A\)', rd),
        has(r'case\s+7:\s*if\(sizeof\(T\) == 1\)\s*SH7095_OnChipRegWrite_u8\(z,\s*unmasked_A', wr)))
    out.append('bus.partition25 assoc_purge_rd=%d assoc_purge_wr=%d' % (
        has(r'case\s+2:\s*case\s+5:.*?SH7095_Cache_AssocPurge', rd), has(r'case\s+2:\s*case\s+5:.*?SH7095_Cache_AssocPurge', wr)))
    return out

def scu_lines(inc):
    out = []
    for width in (8, 16, 32):
        for tag, w, classify in (('rd', 0, classify_read), ('wr', 1, classify_write)):
            body = func_body(inc, r'static INLINE void SCU_RegRW_DB_u%d_W%d\(' % (width, w))
            sw = switch_body(body, r'A\s*&\s*0xFC')
            out.append(line('scu.%s%d' % (tag, width), [('sw', int(sw is not None)), ('mask', 'FC')], parse_cases(sw), classify))
    return out

def simple_line(name, text, header, classify, var=r'\w+', section=None, mask='-'):
    body = func_body(text, header)
    if section: body = body[body.index(section):]
    sw = switch_body(body, var)
    return line(name, [('sw', int(sw is not None)), ('mask', mask)], parse_cases(sw), classify)

def derive(med):
    files = {}
    def get(key, *names):
        n, t = read_source(med, *names)
        files[n] = t
        return t
    inc = get('sh7095', 'sh7095.inc')
    scu = get('scu', 'scu.inc')
    v2 = get('vdp2', 'vdp2.c', 'vdp2.cpp')
    v2r = get('vdp2r', 'vdp2_render.c', 'vdp2_render.cpp')
    v1 = get('vdp1', 'vdp1.c', 'vdp1.cpp')
    sm = get('smpc', 'smpc.c', 'smpc.cpp')
    cd = get('cdb', 'cdb.c', 'cdb.cpp')
    sc = get('scsp', 'scsp.inc')
    lines = []
    lines += onchip_lines(inc)
    lines += bus_lines(inc)
    lines += scu_lines(scu)
    lines.append(simple_line('vdp1.rd', v1, r'static INLINE uint16_t ReadReg\(', classify_read, r'which'))
    lines.append(simple_line('vdp1.wr', v1, r'static INLINE void WriteReg\(', classify_write, r'which'))
    lines.append(simple_line('vdp2.rd', v2, r'static INLINE uint16_t RegsRead\(', classify_read, r'A\s*&\s*0x1FE', mask='1FE'))
    lines.append(simple_line('vdp2.wr', v2, r'static INLINE void RegsWrite\(', classify_write, r'A', mask='1FE'))
    lines.append(simple_line('vdp2r.wr', v2r, r'static INLINE void RegsWrite\(', classify_write, r'A', mask='1FE'))
    lines.append(simple_line('smpc.rd', sm, r'uint8_t SMPC_Read\(', classify_read, r'A', mask='3F'))
    lines.append(simple_line('smpc.wr', sm, r'void SMPC_Write\(', classify_write, r'A', mask='3F'))
    lines.append(simple_line('cd.rd', cd, r'uint16_t CDB_Read\(', classify_read, r'offset'))
    lines.append(simple_line('cd.wr', cd, r'void CDB_Write_DBM\(', classify_write, r'offset'))
    lines.append(simple_line('scsp.rd', sc, r'INLINE void SS_SCSP_RW_u16_W0\(', classify_read, r'\(A\s*>>\s*1\)\s*&\s*0x1F', 'Common regs', mask='1F'))
    lines.append(simple_line('scsp.wr', sc, r'INLINE void SS_SCSP_RW_u16_W1\(', classify_write, r'\(A\s*>>\s*1\)\s*&\s*0x1F', 'Common regs', mask='1F'))
    head = ['# MednafenHandlerCases.txt -- generated by Docs/Decompiler/tools/derive_handler_cases.py from Mednafen\'s',
            '# sources (read-only reference for the Saturn core); do not edit by hand. See that script for the format.',
            '# Sources, by sha1 of the file read:']
    for n in sorted(files):
        head.append('#   %s %s' % (n, hashlib.sha1(files[n].encode('utf-8', 'replace')).hexdigest()))
    return '\n'.join(head + lines) + '\n'

if __name__ == '__main__':
    args = sys.argv[1:]
    if not args or len(args) not in (1, 3) or (len(args) == 3 and args[1] != '--check'):
        raise SystemExit(__doc__ if __doc__ else 'usage: derive_handler_cases.py <mednafen ss dir> [--check file]')
    text = derive(args[0])
    if len(args) == 1:
        sys.stdout.write(text)
    else:
        have = open(args[2]).read()
        if have != text:
            import difflib
            sys.stdout.writelines(difflib.unified_diff(have.splitlines(True), text.splitlines(True), 'checked in', 'derived'))
            raise SystemExit('the cases file is stale against ' + args[0])
        print('cases file is current')
