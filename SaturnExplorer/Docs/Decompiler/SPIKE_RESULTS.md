# SH-2 decompiler feasibility spike — results

**Question.** Can Ghidra's native C++ decompiler, built as an ordinary static library with no
Java, no Ghidra installation, no subprocess and no network, decompile real SH-2 code sitting at
real Saturn addresses, fed from the kind of memory capture SaturnExplorer already makes?

**Answer: yes.** Everything below was produced by `spike/sh2_decomp_spike.cpp`, a 250-line program
linked against ~70 of Ghidra's decompiler sources compiled straight from the Ghidra 12.3 tree
(commit `918d44e`, `Ghidra/Features/Decompiler/src/decompile/cpp`), with the SH-2 SLEIGH spec
(`Ghidra/Processors/SuperH/data/languages/sh-2.slaspec`, language id `SuperH:BE:32:SH-2`)
compiled by Ghidra's own `sleigh` compiler, also built from that tree. Linux x86-64, GCC 13,
`-O2`. No Ghidra source file was modified.

| Measurement | Value |
|---|---|
| Decompiler sources compiled | 84 `.cc` files (Makefile groups CORE + DECCORE + SLEIGH, plus `sleigh_arch`, `inject_sleigh`, `libdecomp`) |
| Library build time | about 2 minutes on 4 cores |
| `libghidra_decomp.a` | 11.0 MB (unstripped, static) |
| Spike executable, stripped | 3.8 MB |
| `sh-2.sla` (compiled SLEIGH spec) | 13 KB, compiles in under 20 ms |
| Language load + Architecture init | 1 ms |
| Decompile, 30-byte function | under 1 ms |
| Decompile, 286-byte function, 13 literal pools, 15 callees registered | 3–5 ms |

## Fixtures

### Fixture 1 — hand-assembled, independently verified

A caller/callee pair placed at `0x06004000` / `0x06005000` (high work RAM). It exercises the SH-2
idioms that matter for Saturn code: a prologue/epilogue through `r15`, a conditional branch, a
PC-relative literal-pool load, an **indirect call through a register** (`jsr @r0`, the normal way
Saturn compilers call anything), a **delay slot that moves the argument** (`mov r14,r4` after the
`jsr`), and `rts` with a delay-slot pop. Every word was encoded by hand from the Hitachi SH-1/SH-2
manual opcode tables and then **verified with Capstone 5.0.7's SuperH module**, which shares no code
with Ghidra's SLEIGH spec (`spike/tools/fixture1.py`):

```
caller @06004000 (capstone SH2 BE):
  06004000  2FE6  mov.l r14,@-r15              ; intended: mov.l r14,@-r15
  06004002  4F22  sts.l pr,@-r15               ; intended: sts.l pr,@-r15
  06004004  6E43  mov r4,r14                   ; intended: mov r4,r14
  06004006  E000  mov #0,r0                    ; intended: mov #0,r0
  06004008  4415  cmp/pl r4                    ; intended: cmp/pl r4
  0600400A  8B03  bf 0x6004014                 ; intended: bf 0x06004014
  0600400C  D003  mov.l 0x600401c,r0           ; intended: mov.l @(12,pc),r0
  0600400E  400B  jsr @r0                      ; intended: jsr @r0
  06004010  64E3  mov r14,r4                   ; intended: mov r14,r4
  06004012  7001  add #1,r0                    ; intended: add #1,r0
  06004014  4F26  lds.l @r15+,pr               ; intended: lds.l @r15+,pr
  06004016  000B  rts                          ; intended: rts
  06004018  6EF6  mov.l @r15+,r14              ; intended: mov.l @r15+,r14
  0600401A  0009  nop                          ; intended: nop
  0600401C  06005000  .long (literal -> callee)
callee @06005000:
  06005000  6042  mov.l @r4,r0                 ; intended: mov.l @r4,r0
  06005002  000B  rts                          ; intended: rts
  06005004  7002  add #2,r0                    ; intended: add #2,r0
```

Decompiled by the spike, with the callee pre-registered as a function and the literal-pool word
marked read-only (both are things the integration does automatically, see DESIGN.md):

```c
int4 func_0x06004000(int4 param_1)

{
  int4 iVar1;
  
  iVar1 = 0;
  if (0 < param_1) {
    iVar1 = func_0x06005000(param_1);
    iVar1 = iVar1 + 1;
  }
  return iVar1;
}
```

The output is semantically exact: `r0 = 0; if (r4 > 0) r0 = callee(r4) + 1; return r0`. The callee
alone decompiles to `return *param_1 + 2;`.

**Pseudocode → assembly mapping.** The markup emitter tags every token with the p-code op it came
from; the spike resolves those to SH-2 instruction addresses in-process. These are the addresses
each line navigates to:

```
int4func_0x06004000(int4param_1)                   ; 
{                                                  ; 
  int4iVar1;                                       ; 
                                                   ; 
  iVar1=0;                                         ; 06004006 
  if(0<param_1){                                   ; 06004008 0600400A 
    iVar1=func_0x06005000(param_1);                ; 0600400E 
    iVar1=iVar1+1;                                 ; 06004012 
  }                                                ; 
  returniVar1;                                     ; 06004016 
}                                                  ; 
```

Note the call line resolves to `0600400E` (the `jsr`) and `iVar1 = 0` to `06004006` (the `mov #0,r0`),
both correct.

### Fixture 1b — the callee lives in memory we do not have

Same caller, literal pool pointing at `0x00000600` (BIOS, never captured). Flow following stops at
the uncaptured address instead of failing; the call is shown as an opaque code pointer:

```c
int4 func_0x06004000(int4 param_1)

{
  int4 iVar1;
  
  iVar1 = 0;
  if (0 < param_1) {
    iVar1 = (*(code *)0x600)(param_1);
    iVar1 = iVar1 + 1;
  }
  return iVar1;
}
```

### Fixture 2 — real game code from a Yabause savestate already in this repository

`SakuraTaisen2/Data/SaveFiles/SaveStates/Yabause/OhrimeMiniGame/GS-9169_007.yss`, read through
SaturnExplorer's **own savestate driver** (`se_savestate_open_yss` → `read_main_ram` /
`read_sh2_regs`, `spike/yss_dump.cpp`), so the bytes are exactly what `ContextBackend` would hand a
panel. The driver reported master `pc=0603055A pr=0603168A`, slave `pc=003C3000`.

**2a. The function the master SH-2 is paused in** (entry `0x06030548`, found by scanning back
from PC to the previous `rts`). SLEIGH disassembly from the spike:

```
6030548  mov.w @(0x6030566,pc),r1
603054A  mul.l r1,r4
603054C  mov.l r14,@-r15
603054E  sts macl,r2
6030550  mov #0x0,r1
6030552  cmp/ge r2,r1
6030554  bt/s 0x6030560
6030556  mov r15,r14
6030558  add 0x1,r1
603055A  cmp/ge r2,r1
603055C  bf/s 0x603055a
603055E  add 0x1,r1
6030560  mov r14,r15
6030562  rts 
6030564  mov.l @r15+,r14
```

```c
void func_0x06030548(int4 param_1)

{
  bool bVar1;
  int4 iVar2;
  
  if (0 < param_1 * 7000) {
    iVar2 = 1;
    do {
      bVar1 = iVar2 < param_1 * 7000;
      iVar2 = iVar2 + 1;
    } while (bVar1);
  }
  return;
}
```

That is a GCC-compiled busy-wait (`for (i = 1; i < n * 7000; i++);`), with the `mov.w` literal
(`7000` at `0x06030566`) folded, the `bt/s`/`bf/s` delay slots handled, and the frame-pointer
`mov r15,r14` / `mov r14,r15` pair eliminated. Line → address mapping:

```
voidfunc_0x06030548(int4param_1)                   ; 
{                                                  ; 
  boolbVar1;                                       ; 
  int4iVar2;                                       ; 
                                                   ; 
  if(0<param_1*7000){                              ; 0603054A 06030552 06030554 
    iVar2=1;                                       ; 06030558 
    do{                                            ; 
      bVar1=iVar2<param_1*7000;                    ; 0603054A 0603055A 
      iVar2=iVar2+1;                               ; 0603055C 
    }while(bVar1);                                 ; 0603055C 
  }                                                ; 
  return;                                          ; 06030562 
}                                                  ; 
```

**2b. The function containing the master's return address** (entry `0x06031598`, a 286-byte
function with a seven-register prologue, two nested loops and ten indirect calls through literal
pools). **Independent verification:** the spike's SLEIGH listing of all 143 instructions was
compared against Capstone's disassembly of the same bytes (`spike/tools/compare.py`, normalising
operand spelling): **143 agree, 0 differ.** The listing is kept in `spike/real_06031598_listing.txt`.

```
; function func_0x06031598: 286 bytes of body
; literal pool: 13 PC-relative loads marked read-only from raw p-code
; decompiled (result 858) in 3 ms
```

```c
int4 func_0x06031598(int4 param_1,int4 param_2,int4 param_3,int4 param_4)

{
  int4 iVar1;
  int4 iVar2;
  int4 iVar3;
  int4 iVar4;
  int4 iVar5;
  
  if (param_1 == 0) {
    iVar1 = 0;
  }
  else {
    iVar1 = *(int4 *)(param_1 + 0xc) + *(int4 *)(param_1 + 0x2c) * -0x800;
    if (iVar1 < param_4) {
      param_4 = iVar1;
    }
    if (param_4 < 0) {
      param_4 = 0;
    }
    if (param_2 * 0x800 < param_4) {
      param_4 = param_2 * 0x800;
    }
    iVar1 = param_4 + 0x7ff;
    if (iVar1 < 0) {
      iVar1 = param_4 + 0xffe;
    }
    iVar4 = param_2;
    func_0x060198de();
    if (iVar1 < param_2) {
      param_2 = iVar1;
    }
    iVar4 = iVar4 - param_2;
    iVar1 = 0;
    iVar5 = 0;
    do {
      iVar2 = func_0x06031510(param_1,0);
      if (iVar2 < 0) {
        iVar2 = func_0x060305b0(0);
        if (iVar2 - 1U < 2) {
          func_0x06031340(param_1,*(xunknown4 *)(param_1 + 0x2c),0);
          iVar2 = func_0x06030db0(param_1,param_2);
          if (0 < iVar2) {
            iVar5 = 0;
          }
        }
      }
      else if (iVar2 == *(int4 *)(param_1 + 0x2c)) {
        if (param_3 == 0) {
          iVar2 = 0;
        }
        else {
          iVar2 = param_3 + iVar1;
        }
        iVar2 = func_0x06031168(param_1,1,iVar2,param_4 - iVar1);
        if (0 < iVar2) {
          iVar3 = iVar2 + 0x7ff;
          iVar1 = iVar1 + iVar2;
          if (iVar3 < 0) {
            iVar3 = iVar2 + 0xffe;
          }
          iVar5 = 0;
          func_0x060198de();
          param_2 = param_2 - iVar3;
          *(int4 *)(param_1 + 0x2c) = *(int4 *)(param_1 + 0x2c) + iVar3;
        }
        if (param_4 <= iVar1) break;
      }
      else {
        func_0x060313d0(param_1);
        iVar5 = 0;
      }
      func_0x06030548(1);
      iVar5 = iVar5 + 1;
    } while (iVar5 < 20000);
    *(int4 *)(param_1 + 0x2c) = *(int4 *)(param_1 + 0x2c) + iVar4;
    *(xunknown4 *)(param_1 + 0x34) = 0;
  }
  return iVar1;
}
```

Every `(*pcRam...)` indirect call became a named direct call once the 13 literal-pool words were
marked read-only and the call targets were registered as functions — a two-pass scheme the design
adopts. Arguments at the call sites were recovered from the caller side.

### Mirrored address

Decompiling the same function via its cache-through mirror `0x26031598` produces identical code;
the load image folds bits 29–31 and the 1 MiB HWRAM repeat, while the function keeps the address
the user asked for:

```
; function func_0x26031598: 286 bytes of body
int4 func_0x26031598(int4 param_1,int4 param_2,int4 param_3,int4 param_4)
```

### Unmapped address (the slave SH-2's PC in this savestate)

```
; followFlow: Unable to load 16 bytes at 3c3000
; function func_0x003c3000: 0 bytes of body
; decompile error: Unable to load 16 bytes at 3c3000
```

The engine raises `DataUnavailError` from `LoadImage::loadFill`, flow following stops, and the
caller sees "no code". No crash, no partial garbage.

## What the spike established about the engine's API

- **Embedding surface** is five classes: subclass `LoadImage` (bytes), subclass
  `SleighArchitecture` (plug the loader in, name the language id), then
  `symboltab->getGlobalScope()->addFunction`, `Funcdata::followFlow`,
  `allacts.getCurrent()->perform`, and `print->docFunction`. `startDecompilerLibrary(paths)` takes
  a directory list containing `.ldefs`/`.pspec`/`.cspec`/`.sla`; no Ghidra directory tree needed.
- **Big-endian** is a property of the SLEIGH spec (`define endian=big`), nothing to configure.
- **Literal pools.** RAM is writable, so the decompiler will not fold a load from it unless the
  address is marked read-only *and* `Architecture::readonlypropagate` is on (Ghidra's GUI default;
  off in the bare library). SLEIGH emits `tmp = #addr; Rn = *(ram, tmp)` for
  `mov.l/mov.w @(disp,PC)`, so after `followFlow` a per-instruction constant walk over the raw
  p-code finds every literal address with no SH-2-specific decoding. Marking only those words
  read-only (not the whole RAM) is what keeps real globals as globals.
- **Markup.** `print->setMarkup(true)` emits tagged output; the default encoding is Ghidra's packed
  binary, `setPackedOutput(false)` switches to XML, and `PackedDecode`/`xml_tree` to read either are
  part of the same library. Tokens carry `opref` = the p-code op's sequence number, which maps to
  an instruction address via `Funcdata::beginOpMain()`.
- **Call targets** are not auto-created as functions in the bare library (Ghidra's Java side does
  that). Registering discovered constant call targets before the final pass is cheap and turns
  `(*(code *)0x6031510)(...)` into `func_0x06031510(...)`.
- **Thread model.** The library keeps process-wide static state (`SleighArchitecture::translators`,
  capability registries), so all engine calls belong on one worker thread. Architecture
  construction is 1 ms, so building a fresh one per memory snapshot is affordable and avoids any
  stale symbol/function state.

## Reproducing

```
# 1. Ghidra sources (sparse; only the decompiler and the SuperH spec)
git clone --depth 1 --filter=blob:none --sparse https://github.com/NationalSecurityAgency/ghidra.git
cd ghidra && git sparse-checkout set --skip-checks Ghidra/Features/Decompiler/src/decompile Ghidra/Processors/SuperH
# 2. SaturnExplorer's libraries (for yss_dump)
cmake -S SaturnExplorer -B se-build -DBUILD_TESTING=OFF && cmake --build se-build --target SaturnExplorerSavestateDriver
# 3. The spike
cmake -S SaturnExplorer/Docs/Decompiler/spike -B spike-build -DCMAKE_BUILD_TYPE=Release \
      -DGHIDRA_DECOMP_DIR=$PWD/ghidra/Ghidra/Features/Decompiler/src/decompile/cpp -DSE_BUILD=$PWD/se-build
cmake --build spike-build
# 4. Specs: copy superh.* and sh-2.slaspec from ghidra/Ghidra/Processors/SuperH/data/languages into specs/, then
spike-build/sleigh_compiler specs/sh-2.slaspec specs/sh-2.sla
# 5. Fixtures and runs
pip install capstone && python3 -I SaturnExplorer/Docs/Decompiler/spike/tools/fixture1.py fixture1.bin
spike-build/sh2_decomp_spike specs 06004000 06004000=fixture1.bin --auto-ro --known 06005000 --xml --asm 14
spike-build/yss_dump SakuraTaisen2/Data/SaveFiles/SaveStates/Yabause/OhrimeMiniGame/GS-9169_007.yss st2
spike-build/sh2_decomp_spike specs 06031598 06000000=st2_hwram.bin 00200000=st2_lwram.bin --auto-ro --asm 150 --xml
```

Flags: `--asm N` prints N instructions of SLEIGH disassembly; `--auto-ro` marks literal pools
read-only; `--known ADDR` pre-registers a function; `--ro BASE:SIZE` marks a range read-only;
`--xml` prints XML markup and the line → address table; `--pcode` dumps raw p-code.
