# Hardware register audit

Every entry of `FrontEnd/src/Decompiler/SaturnHardwareMap.cpp` checked against the hardware
manuals and against what Mednafen, the emulator SE runs, actually does. Done for the consolidated
hardware-semantics review; re-run `tools/audit_hardware_map.py` after any change to the table.

## Method

- **Addresses and widths.** Taken from the Sega manuals (SMPC, SCSP, VDP1 and VDP2 in
  `Docs/Saturn`, SCU) and Hitachi's SH7604 manual. The script confirms Mednafen decodes each
  address in the handler for that width.
- **Direction.** Read-only, write-only or both, from the manuals. The script checks it against
  Mednafen's handlers: a read that returns state, a write with an effect. Mednafen was read from
  libretro's beetle-saturn copy (`mednafen/ss`), since `_emu/mednafen` was not present.
- **Mirrors, keyed writes, split fields and read effects.** Checked by hand against the same
  sources; listed below.
- **The audit catches errors.** Putting the old SCSP_MVOL (read/write) and a planted SCU_IMS
  (read/write) back into a copy of the table makes the script report both.
- **Re-run for the review at `5670323`** against the same beetle-saturn sources (`mednafen/ss`,
  with its `.c` files given the `.cpp` names the script expects): the same 22 differences, all
  listed here, and nothing new. Re-read by hand from the handlers: the mirror periods (VDP2
  `A & 0x1FE`, SCU `A & 0xFC`, SMPC `(A & 0x7F) >> 1`), `SCU_IST` reading the pending set, and
  `SCU_PDD` advancing the DSP's address on a read while the DSP is stopped.
- **Re-run in area 5 round 3** against the same sources: 21 of 337 registers differ, all of them "not decoded
  by Mednafen" (the serial interface and the UBC); `SCU_AIACK` now agrees (write-only), and `VDP2_EXTEN`,
  `VDP2_TVSTAT` are `ok` with their read effects in the map.

## Disagreements

| Register | Table | Manual | Mednafen | Decision |
|---|---|---|---|---|
| SCU_AIACK (`05FE00A8`) | write-only | read/write | reads 0 at every width (`SCU_RegRW_DB_u*_W0` case 0xA8: `*DB = 0; //ABusIAck`) | Mednafen (area 5 round 3, A5-16): a read is the write-only caution. The manual's label is the only thing against it. |
| SCSP_MCIEB (`05B0042A`) | write-only | paired with the readable SCIEB | reads 0 | Mednafen (the backend), recorded in the table's comment. |
| SH2_SMR..SH2_RDR (`FFFFFE00`-`05`) | per SH7604 | SCI registers | not emulated | Manual. |
| SH2_BARAH..SH2_BRCR (`FFFFFF40`-`78`) | per SH7604 | UBC registers | not emulated | Manual. |
| SH2_DVDNTH/L_SHADOW (`FFFFFF18`/`1C`) | read/write | not in the register table | read and written | Mednafen; every access says it is Mednafen's. |
| 0xA partition | volatile, no aliases | (not in the SH7604 table) | associative purge | Mednafen; `SaturnMemory.txt` calls it cache-through. |
| `0xE0000000`-`0xFFFFFFFF` | every address is an on-chip register by `A & 0x1FF` | registers where bit 14 is set (bits 28 and 12 below 0x100), garbage elsewhere | decodes the whole partition by `A & 0x1FF` (`MemRead` / `MemWrite` case 7) | Mednafen (area 5 round 4): the emulator is the reference; a program meant for the console never relies on the difference. |
| Backup RAM (`0x00180000`-`0x001FFFFF`) | volatile; accesses cautioned | a byte device on the low lane | `ss.c` `BusRW_DB_CS0_*`: `A & 1` byte store, `0xFF00` ORed with the byte on reads | Agree (A5-26). |
| VDP2 TVSTAT, HCNT, VCNT | read-only | read-only | a write is kept only in the debug `RawRegs` | Agree: no effect. |

## Checked by hand

- **Mirrors** (`HardwareAlias`):
  - SMPC `A & 0x7F` (ss.cpp);
  - SCSP every 4 KiB (MemoryLayout);
  - VDP2 `A & 0x1FE` (vdp2.cpp);
  - SCU `A & 0xFF`;
  - the CD block where `(A & 0x7FFF) < 0x1000`, `(A & 0x3F) >> 2` (scu.inc);
  - MINIT/SINIT across their 8 MiB;
  - on-chip:
    - the partial decode (SaturnMemory.txt);
    - the divider at `+0x20` (sh7095.inc cases `0x00`/`0x20`);
    - the watchdog at `+8` (cases `0x80`/`0x88`, `0x82`/`0x8A`, `0x81`/`0x89`, `0x83`/`0x8B`).
- **Keyed writes:** the watchdog.
  - Word writes at FE80 (and FE88): 0x5A loads WTCNT only while TME is set; 0xA5 loads WTCSR,
    where OVF clears only after a read.
  - Word writes at FE82 (and FE8A): 0xA500 clears WOVF only after a read; 0x5A sets RSTE/RSTS.
  - Byte writes at FE80-FE83 have no effect.

  Source: sh7095.inc `OnChipRegWrite_u16`, SH7604 §12.2.4.
- **Access width and lane** (area 5, rounds 2 and 3): superseded by the width table above, which has
  every register, width and direction, the Mednafen handler each rule is read from, and the golden file.
  Round 2's findings are in it unchanged (the SMPC lane, the VDP byte write, the on-chip 32-bit
  registers' word and byte rules); round 3 added the on-chip registers below `0xFFFFFF00` (a word
  reaches the even byte only, a long is an address error), the SCU's DSP ports, the bus state
  controller's keyed long write, `MINIT`/`SINIT`, the CD block's windows and the B bus's 32-bit reads.
- **Split fields:**
  - SCSP MSLC/CA, DTLG, SCIPD, MCIPD (scsp.inc);
  - SCU IST, PPAF (scu.inc);
  - CD HIRQ and CR1-CR4 (cdb.cpp);
  - SMPC PDR1-2 (smpc.cpp).
- **Read effects** (said as notes, not cautions: the register returns what it says):
  - PPAF clears V and E and the DSP interrupt;
  - PDD returns data and advances the address only while the DSP is stopped;
  - the MIBUF and CD FIFO reads pop;
  - CR4 marks the response read;
  - FRCH and FICRH latch the low byte;
  - EXTEN latches HCNT and VCNT while EXLTEN is 0, TVSTAT clears EXLTFG (`vdp2.c` `RegsRead`);
  - the B bus reads 32 bits (SH0 at A, SH1 at A|2), so a read of an even VDP2/VDP1/SCSP register also reads the one 2 bytes on, and the CD FIFO pops twice at `0x25818000`;
  - FTCSR, CHCR, DMAOR and RTCSR arm their "write 0 after reading 1" clear.

## Width table (area 5, round 3)

The width of an access is not the register's width, and Mednafen's handler for each area decides
what the difference does. Round 1 and 2 found the instances one at a time (VDP byte writes, the
divider's words, the SMPC's lane); round 3 replaced the hand rules with a table, derived from the
handlers, with a cell for every register, each of the three widths, both directions, and every
aligned place the access can fall on the register (`WidthCellFor`, one rule per family in
`SaturnHardwareMap.cpp`). **Source of truth: the table in the code.** Two things are generated from it
and checked against it by `SaturnExplorerHardwareWidthTableTests` (`SE_UPDATE_GOLDEN=1` rewrites both):

- the **golden file** `FrontEnd/tests/HardwareWidthTable.golden.txt`: one line per register (and per
  window of the CD FIFO, and for each unused SMPC lane) with all its cells -- review it against the
  handlers named in the rules table, line by line, instead of probing registers;
- the **rules table below**, between the markers: each rule's category (a accepted, b whole register,
  c one lane/half/register, d nothing, e address error, f a side effect that differs, v depends on
  the value), the Mednafen function it is read from, how many cells use it, and the caution the
  decompiler gives for the first of them.

Not in the table, and why: memory the decompiler treats as ordinary. A byte write to **VDP2 colour RAM**
(`0x25F00000`-`0x25F7FFFF`) writes the whole 16-bit entry from the data bus (`vdp2.c` `RW_W8`: the CRAM branch has no
byte mask, VRAM's has); a folded store there has no volatile operation to hang a caution on, and marking colour RAM
volatile would keep every palette copy as written, so it is accepted (ledger A5-16, part 4). Accesses to unnamed
addresses inside a register area (the SCSP slots, the gaps) are not rows either; a long over a gap and a register is, from
the register's side (`HalfBesideGap`).

The cautions are generated from the cells in one place (`NarrowAccessEffects`), and
`HardwareNotes` gates a register's own read effect (a latch, a flag armed) on the cell, so a word read
of TIER no longer claims to arm FTCSR. The per-register table that follows (Ours/Mednafen) is
still generated by `tools/audit_hardware_map.py` from the same `Reg` arrays; the width table is not
in the script because its source is the rule function, not a text pattern.

### The independent check (area 5, round 4)

The golden file is printed from the rule function, so it only says the table did not change: round 3's
DMAC rows (A5-18) were wrong in eleven golden lines and the test passed. Round 4 adds a check whose input
is Mednafen's source and not the table:

- `Docs/Decompiler/tools/derive_handler_cases.py <mednafen>/mednafen/ss` parses the handlers (`sh7095.inc`
  `OnChipRegRead_INLINE_u8/u16/u32` and `OnChipRegWrite_u8/u16/u32`, each in its `A & 0x100` halves;
  `scu.inc` `SCU_RegRW_DB_u8/u16/u32_W0/W1`; `vdp1.c`, `vdp2.c`, `vdp2_render.c`, `smpc.c`, `cdb.c`, `scsp.inc`) and writes
  `FrontEnd/tests/MednafenHandlerCases.txt`: per handler and width, the switch labels grouped by shared body, whether a
  case does anything with the access (`M`), has a read effect (`E`) or starts a division, DMA, drawing or sets CCR
  (`D S G C`), whether the branch raises an address error before it decodes, and the mask it applies; plus facts about the
  bus's own dispatch (misaligned access, cached partition, the partition decoded as on-chip). `--check <file>` re-derives it
  and fails when the checked-in copy is stale. It never reads `SaturnHardwareMap.cpp`.
- `FrontEnd/tests/HardwareHandlerCheck.h`, run by `SaturnExplorerHardwareWidthTableTests` against that file, compares
  every on-chip cell with the labels (a cell that claims the access reaches the register must land on a case of the register's
  own group at that width; one that claims nothing must not; an address-error cell needs the handler to raise it; the
  register's read or write effect must happen exactly when the case has it), every SCU cell with the per-width SCU
  handler, every VDP1, VDP2, SMPC, CD block and SCSP register and direction with its switch, the bus facts, and every
  aligned address of `0xFFFFFE00`-`0xFFFFFFFF`, named or not, for the address error. SCI and UBC are claimed from the manual
  and checked to stay undecoded.
- It cannot see: which bits of the value reach the register, the lane rules of the 16-bit buses (SMPC, SCSP, CD, MINIT/SINIT,
  backup RAM), the keys of the keyed writes, FIFO pops, constant filler values (`0xFF`, `0x00FF`), VDP2 colour RAM, value-independent
  write effects other than SMPC_SF (VDP1 `ENDR`, SCU `AIACK`/`T1MD`, SCSP; listed as `F` in the cases file), or the wording of any
  caution. Those stay with the hand fixtures and the golden file.

Decisions this round: the on-chip partition is decoded the way Mednafen decodes it (`A & 0x1FF` over all of
`0xE0000000`-`0xFFFFFFFF`), not by the manual's bit 14; a write to a DMA channel control register starts a transfer and
is not noted (the register's purpose); the cache purge of a CCR write, the VDP1 start and SMPC_SF's set are noted.

<!-- width-rules:begin (generated by HardwareWidthTableTests; SE_UPDATE_GOLDEN=1 rewrites it) -->

| Cat | Rule | Mednafen | Cells | Caution (first cell) |
|---|---|---|---|---|
| a | Plain | the register's own handler: scu.inc SCU_RegRW_DB_u*, SS_SCSP_RW_u*, vdp1.c ReadReg/WriteReg, vdp2.c RegsRead/RegsWrite, sh7095.inc OnChipReg*, cdb.c CDB_Read/CDB_Write_DBM | 811 | (none) |
| a | HalfOfLong | scu.inc SCU_FromSH2_BusRW_DB_u32_W*: the B bus is BBusRW_DB_u16_*_SH0 at A then _SH1 at A\|2, high half first | 199 | one 4-byte write covers SCSP_MVOL and SCSP_RBL_RBP (shown under the first)  [SCSP_MVOL, write 4 @05B00400] |
| - | AccessKind | (none: the register's kind -- write-only, read-only -- says it) | 983 | (none) |
| b | ByteWritesWord | scu.inc BBusRW_DB_u8_W1_SH0 -> vdp1.c VDP1_Write8_DB / vdp2.c VDP2_Write8_DB: WriteReg / RegsWrite(A, *DB), the 16-bit lane of the data bus (RW_W8's byte mask is VRAM's only) | 292 | a 1-byte write to VDP1_TVMR writes the whole 16-bit register: its other byte is whatever was last on the data bus  [VDP1_TVMR, write 1 @05D00000] |
| b | PortWritesWhole | scu.inc SCU_RegRW_DB_u8_W1 / _u16_W1 cases 0x80-0x8C: *DB is used whole, the mask every other case applies is not | 24 | a 1-byte write to SCU_PPAF writes the whole 32-bit register: its other bytes are whatever was last on the data bus (the SCU masks the byte and word writes of every other register, not the DSP ports) and controls the DSP from the whole 32-bit value: execute, pause, step, load the program counter  [SCU_PPAF, write 1 @05FE0080] |
| b | NarrowReadWhole | scu.inc SCU_RegRW_DB_u*_W0 cases 0x80, 0x8C: the whole 32-bit read happens, the CPU takes its part | 12 | a 1-byte read of SCU_PPAF is a read of the whole 32-bit register: what a read does to it happens in full, and only the bytes asked for are returned  [SCU_PPAF, read 1 @05FE0080] |
| b | WordWritesLong | sh7095.inc SH7095_OnChipRegWrite_u16, A & 0x100 set: the register takes V (DVDNT, DVDNTL also divide) | 19 | a 2-byte write to SH2_DVSR, a 32-bit register, writes the whole register with the 16-bit value (upper half 0)  [SH2_DVSR, write 2 @FFFFFF00] |
| b | FifoPopsTwice | scu.inc ABus_Read(A & ~3): ABusRW_DB_u16_W0_SH0 and _SH1 both reach CDB_Read(0) when bit 19 of A is clear | 6 | a 1-byte read of CD_DATATRNS takes two 16-bit words off the FIFO: the 32-bit bus reads it twice and the access returns only part of them (the register window at 0x25898000 reads it once)  [CD_DATATRNS, read 1 @05818000] |
| b | FifoByteWrite | scu.inc ABusRW_DB_u8_W1_SH0 (CS2, (A & 0x7FFF) < 0x1000): CDB_Write_DBM(0, *DB, 0xFF << ...); cdb.c case 0x0 stores (FIFO[WP] &~ mask) \| (DB & mask) and advances WP, InBufOffs and TotalCounter by a word | 8 | a 1-byte write to CD_DATATRNS pushes a whole 16-bit word into the transfer (the CD block takes the byte under a byte mask and leaves the other byte of the FIFO slot as it was): the sector gets one word per write, half of it stale  [CD_DATATRNS, write 1 @05818000] |
| c | HalfBesideGap | as HalfOfLong; the other half is a word nothing in the map names | 13 | a 4-byte write over SCSP_MSLC_CA is two 16-bit bus writes, high half first: SCSP_MSLC_CA takes bits 31-16 of the value, and bits 15-0 go to the word at 0x05B0040A, which is no register in the map  [SCSP_MSLC_CA, write 4 @05B00408] |
| c | SmpcLaneWrite | ss.c BusRW_DB_CS0_u16_W1: SMPC_Write((A & 0x7F) >> 1, low byte of the half); a long is two of them | 30 | a 2-byte write to SMPC_IREG0 is a byte write: the register takes the low byte of the value and the high byte goes nowhere (the SMPC is a byte device on the low lane of the 16-bit bus)  [SMPC_IREG0, write 2 @00100000] |
| c | SmpcLaneRead | ss.c BusRW_DB_CS0_u16_W0: 0xFF00 \| SMPC_Read((A & 0x7F) >> 1); a long is two of them | 72 | a 2-byte read of SMPC_OREG0 returns 0xFF00 or the register: the SMPC is a byte device on the low lane of the 16-bit bus, and the high byte reads as 0xFF  [SMPC_OREG0, read 2 @00100020] |
| c | EvenByteWrite | sh7095.inc SH7095_OnChipRegWrite_u16, A & 0x100 clear: switch (A & 0xFF) has the even address only; TIER = V (uint8_t) | 6 | a 2-byte write to SH2_TIER, an 8-bit register, reaches only it, with the low byte of the value (bits 7-0, the byte a big-endian word has at the odd address); SH2_FTCSR, the register beside it, is not written  [SH2_TIER, write 2 @FFFFFE10] |
| c | EvenByteRead | sh7095.inc SH7095_OnChipRegRead_INLINE_u16, A & 0x100 clear: ret is the register at the word's own address | 8 | a 2-byte read at SH2_TIER, an 8-bit register, returns only it, in bits 7-0; SH2_FTCSR, the register beside it, is not read  [SH2_TIER, read 2 @FFFFFE10] |
| c | LowHalfRead | sh7095.inc SH7095_OnChipRegRead_INLINE_u16, A & 0x100 set: A &= 0xFE, ret = the 32-bit register truncated | 26 | a 2-byte read at SH2_DVSR, a 32-bit register, returns its low 16 bits, not the upper half this address suggests (a read at +2 returns them too)  [SH2_DVSR, read 2 @FFFFFF00] |
| c | CdLongWrite | scu.inc ABus_Write_DB32_u32: ABusRW_DB_u16_W1_SH0 at A, _SH1 at A\|2; both decode (A & 0x3F) >> 2 to one CDB_Write_DBM | 6 | a 4-byte write to CD_HIRQ is two 16-bit writes to the same register (the CD block answers every 4 bytes): it is written with bits 31-16 and then with bits 15-0, which it keeps, and a write that has an effect happens twice  [CD_HIRQ, write 4 @05890008] |
| c | CdLongRead | scu.inc ABus_Read: ABusRW_DB_u16_W0_SH1 reads only if !(A & 0x80000), so the register window keeps the first value in both halves | 7 | a 4-byte read of CD_DATATRNS reads it once and returns it in both halves (the CD block ignores the second 16-bit access in its register window)  [CD_DATATRNS, read 4 @05898000] |
| d | SmpcPadWrite | ss.c BusRW_DB_CS0_u8_W1: SMPC_Write only if (A & 1) | 48 | a 1-byte write at the even address beside SMPC_IREG0 does nothing: the SMPC is a byte device on the low lane of the 16-bit bus, and its registers are at the odd addresses  [SMPC_UNUSED_00, write 1 @00100000] |
| d | SmpcPadRead | ss.c BusRW_DB_CS0_u8_W0: the high lane of the 16-bit word reads 0xFF | 48 | a 1-byte read at the even address beside SMPC_IREG0 returns 0xFF, not the register: the SMPC is a byte device on the low lane of the 16-bit bus, and its registers are at the odd addresses  [SMPC_UNUSED_00, read 1 @00100000] |
| d | OddByteWrite | sh7095.inc SH7095_OnChipRegWrite_u16, A & 0x100 clear: no case at the odd address of a word (A is even) | 6 | a 2-byte write at the even address beside SH2_FTCSR does not write it: a word reaches only the register at its own address (SH2_TIER)  [SH2_FTCSR, write 2 @FFFFFE10] |
| d | OddByteRead | sh7095.inc SH7095_OnChipRegRead_INLINE_u16, A & 0x100 clear: no case at the odd address of a word | 9 | a 2-byte read at the even address beside SH2_FTCSR does not read it: a word reaches only the register at its own address (SH2_TIER, in bits 7-0)  [SH2_FTCSR, read 2 @FFFFFE10] |
| d | LongHalfWrite | sh7095.inc SH7095_OnChipRegWrite_u16, A & 0x100 set: A &= 0xFE, and the cases are the registers' own addresses | 19 | a 2-byte write to the low half of SH2_DVSR, a 32-bit register, does nothing: a word takes effect only at the register's own address  [SH2_DVSR, write 2 @FFFFFF02] |
| d | LongHalfRead | sh7095.inc SH7095_OnChipRegRead_INLINE_u16, A & 0x100 set: A &= 0xFE; the DMA cases are 0x80 .. 0xB0 only, so +2 falls to default (ret = 0, no CHCRM / DMAORM clear) | 11 | a 2-byte read at +2 of SH2_SAR0, a 32-bit register, returns 0 and reads nothing: the DMA controller answers a word read at the register's own address only (what a read of it arms does not happen)  [SH2_SAR0, read 2 @FFFFFF82] |
| d | KeyedWordWrite | sh7095.inc SH7095_OnChipRegWrite_u16 cases 0xE0-0xF8: break (only the _u32 write checks 0xA55A) | 14 | a 2-byte write to SH2_BCR1, a 32-bit register, does nothing: it is set only by a 4-byte write with 0xA55A in the upper half  [SH2_BCR1, write 2 @FFFFFFE0] |
| d | KeyedByteWrite | sh7095.inc SH7095_OnChipRegWrite_u8 cases 0x80-0x8F: z->WDT.RSTCSRM = 0 and nothing else (only the _u16 write takes a key) | 3 | SH2_WTCSR takes no 1-byte write: it is written by a keyed word at 0xFFFFFE80; this write does not write it, and it cancels a pending SH2_RSTCSR WOVF clear (a read arms it)  [SH2_WTCSR, write 1 @FFFFFE80] |
| d | StrobeByte | ss.c BusRW_DB_CS0_u8_W1, "FRT trigger region": the block is empty (only _u16_W1 calls SH7095_SetFTI) | 4 | a 1-byte write to MINIT does nothing: the signal to the other CPU takes a 16-bit write (the CS0 byte path ignores the FRT trigger region)  [MINIT, write 1 @01000000] |
| d | MirrorWrite | sh7095.inc OnChipRegWrite_u*: case 0x92 only ("No 0x93...0x9F mirrors for writes") | 2 | a 1-byte write at 0xFFFFFE93 does nothing: SH2_CCR reads back at 0xFFFFFE93-0xFFFFFE9F, but takes writes at 0xFFFFFE92 only  [SH2_CCR, write 1 @FFFFFE93] |
| e | AddressError | sh7095.inc SH7095_SetPEX(z, SH7095_PEX_CPUADDR): OnChipRegWrite_u8 / RegRead_u8 with A & 0x100 set, OnChipRegWrite_u32 / RegRead_u32 with it clear | 316 | a 4-byte write to SH2_TIER, an 8-bit register, is an address error on the SH-2: the registers at 0xFFFFFE00-0xFFFFFEFF take byte and word accesses only  [SH2_TIER, write 4 @FFFFFE10] |
| f | StrobeLong | sh7095.inc SH7095_BSC_BusWrite_u32 (CS0): two BusRW_DB_CS0_u16_W1, each SH7095_SetFTI | 2 | a 4-byte write to MINIT is two 16-bit bus writes, so it signals the other CPU twice (its input capture takes the second)  [MINIT, write 4 @01000000] |
| f | CdByteWrite | cdb.c CDB_Write_DBM case 0x9: CommandPending only if mask == 0xFFFF | 2 | a 1-byte write to CD_CR4 stores that byte only: it does not start the command, which takes a 16-bit write  [CD_CR4, write 1 @05890024] |
| v | KeyedWrite | sh7095.inc OnChipRegWrite_u16 cases 0x80, 0x82 (the key in the upper byte); _u32 cases 0xE0-0xF8 ((V & 0xFFFF0000) == 0xA55A0000) | 10 | (none) |
| ? | Undecoded | sh7095.inc has no case for the serial interface: no rule is claimed | 22 | (none) |

<!-- width-rules:end -->

## Table

| Address | Register | Width | Ours | Mednafen |
|---|---|---|---|---|
| `00100001` | SMPC_IREG0 | 1 | WO | ok |
| `00100003` | SMPC_IREG1 | 1 | WO | ok |
| `00100005` | SMPC_IREG2 | 1 | WO | ok |
| `00100007` | SMPC_IREG3 | 1 | WO | ok |
| `00100009` | SMPC_IREG4 | 1 | WO | ok |
| `0010000B` | SMPC_IREG5 | 1 | WO | ok |
| `0010000D` | SMPC_IREG6 | 1 | WO | ok |
| `0010001F` | SMPC_COMREG | 1 | WO | ok |
| `00100021` | SMPC_OREG0 | 1 | RO | ok |
| `00100023` | SMPC_OREG1 | 1 | RO | ok |
| `00100025` | SMPC_OREG2 | 1 | RO | ok |
| `00100027` | SMPC_OREG3 | 1 | RO | ok |
| `00100029` | SMPC_OREG4 | 1 | RO | ok |
| `0010002B` | SMPC_OREG5 | 1 | RO | ok |
| `0010002D` | SMPC_OREG6 | 1 | RO | ok |
| `0010002F` | SMPC_OREG7 | 1 | RO | ok |
| `00100031` | SMPC_OREG8 | 1 | RO | ok |
| `00100033` | SMPC_OREG9 | 1 | RO | ok |
| `00100035` | SMPC_OREG10 | 1 | RO | ok |
| `00100037` | SMPC_OREG11 | 1 | RO | ok |
| `00100039` | SMPC_OREG12 | 1 | RO | ok |
| `0010003B` | SMPC_OREG13 | 1 | RO | ok |
| `0010003D` | SMPC_OREG14 | 1 | RO | ok |
| `0010003F` | SMPC_OREG15 | 1 | RO | ok |
| `00100041` | SMPC_OREG16 | 1 | RO | ok |
| `00100043` | SMPC_OREG17 | 1 | RO | ok |
| `00100045` | SMPC_OREG18 | 1 | RO | ok |
| `00100047` | SMPC_OREG19 | 1 | RO | ok |
| `00100049` | SMPC_OREG20 | 1 | RO | ok |
| `0010004B` | SMPC_OREG21 | 1 | RO | ok |
| `0010004D` | SMPC_OREG22 | 1 | RO | ok |
| `0010004F` | SMPC_OREG23 | 1 | RO | ok |
| `00100051` | SMPC_OREG24 | 1 | RO | ok |
| `00100053` | SMPC_OREG25 | 1 | RO | ok |
| `00100055` | SMPC_OREG26 | 1 | RO | ok |
| `00100057` | SMPC_OREG27 | 1 | RO | ok |
| `00100059` | SMPC_OREG28 | 1 | RO | ok |
| `0010005B` | SMPC_OREG29 | 1 | RO | ok |
| `0010005D` | SMPC_OREG30 | 1 | RO | ok |
| `0010005F` | SMPC_OREG31 | 1 | RO | ok |
| `00100061` | SMPC_SR | 1 | RO | ok |
| `00100063` | SMPC_SF | 1 | RW | ok |
| `00100075` | SMPC_PDR1 | 1 | RW | ok |
| `00100077` | SMPC_PDR2 | 1 | RW | ok |
| `00100079` | SMPC_DDR1 | 1 | WO | ok |
| `0010007B` | SMPC_DDR2 | 1 | WO | ok |
| `0010007D` | SMPC_IOSEL | 1 | WO | ok |
| `0010007F` | SMPC_EXLE | 1 | WO | ok |
| `01000000` | MINIT | 2 | WO | ok |
| `01800000` | SINIT | 2 | WO | ok |
| `05818000` | CD_DATATRNS | 4 | RW | ok |
| `05890008` | CD_HIRQ | 2 | RW | ok |
| `0589000C` | CD_HIRQMSK | 2 | RW | ok |
| `05890018` | CD_CR1 | 2 | RW | ok |
| `0589001C` | CD_CR2 | 2 | RW | ok |
| `05890020` | CD_CR3 | 2 | RW | ok |
| `05890024` | CD_CR4 | 2 | RW | ok |
| `05B00400` | SCSP_MVOL | 2 | WO | ok |
| `05B00402` | SCSP_RBL_RBP | 2 | WO | ok |
| `05B00404` | SCSP_MIBUF | 2 | RO | ok |
| `05B00406` | SCSP_MOBUF | 2 | WO | ok |
| `05B00408` | SCSP_MSLC_CA | 2 | RW | ok |
| `05B00412` | SCSP_DMEAL | 2 | WO | ok |
| `05B00414` | SCSP_DMEAH_DRGA | 2 | WO | ok |
| `05B00416` | SCSP_DTLG | 2 | RW | ok |
| `05B00418` | SCSP_TIMA | 2 | WO | ok |
| `05B0041A` | SCSP_TIMB | 2 | WO | ok |
| `05B0041C` | SCSP_TIMC | 2 | WO | ok |
| `05B0041E` | SCSP_SCIEB | 2 | RW | ok |
| `05B00420` | SCSP_SCIPD | 2 | RW | ok |
| `05B00422` | SCSP_SCIRE | 2 | WO | ok |
| `05B00424` | SCSP_SCILV0 | 2 | WO | ok |
| `05B00426` | SCSP_SCILV1 | 2 | WO | ok |
| `05B00428` | SCSP_SCILV2 | 2 | WO | ok |
| `05B0042A` | SCSP_MCIEB | 2 | WO | ok |
| `05B0042C` | SCSP_MCIPD | 2 | RW | ok |
| `05B0042E` | SCSP_MCIRE | 2 | WO | ok |
| `05D00000` | VDP1_TVMR | 2 | WO | ok |
| `05D00002` | VDP1_FBCR | 2 | WO | ok |
| `05D00004` | VDP1_PTMR | 2 | WO | ok |
| `05D00006` | VDP1_EWDR | 2 | WO | ok |
| `05D00008` | VDP1_EWLR | 2 | WO | ok |
| `05D0000A` | VDP1_EWRR | 2 | WO | ok |
| `05D0000C` | VDP1_ENDR | 2 | WO | ok |
| `05D00010` | VDP1_EDSR | 2 | RO | ok |
| `05D00012` | VDP1_LOPR | 2 | RO | ok |
| `05D00014` | VDP1_COPR | 2 | RO | ok |
| `05D00016` | VDP1_MODR | 2 | RO | ok |
| `05F80000` | VDP2_TVMD | 2 | RW | ok |
| `05F80002` | VDP2_EXTEN | 2 | RW | ok |
| `05F80004` | VDP2_TVSTAT | 2 | RO | ok |
| `05F80006` | VDP2_VRSIZE | 2 | RW | ok |
| `05F80008` | VDP2_HCNT | 2 | RO | ok |
| `05F8000A` | VDP2_VCNT | 2 | RO | ok |
| `05F8000E` | VDP2_RAMCTL | 2 | RW | ok |
| `05F80010` | VDP2_CYCA0L | 2 | WO | ok |
| `05F80012` | VDP2_CYCA0U | 2 | WO | ok |
| `05F80014` | VDP2_CYCA1L | 2 | WO | ok |
| `05F80016` | VDP2_CYCA1U | 2 | WO | ok |
| `05F80018` | VDP2_CYCB0L | 2 | WO | ok |
| `05F8001A` | VDP2_CYCB0U | 2 | WO | ok |
| `05F8001C` | VDP2_CYCB1L | 2 | WO | ok |
| `05F8001E` | VDP2_CYCB1U | 2 | WO | ok |
| `05F80020` | VDP2_BGON | 2 | WO | ok |
| `05F80022` | VDP2_MZCTL | 2 | WO | ok |
| `05F80024` | VDP2_SFSEL | 2 | WO | ok |
| `05F80026` | VDP2_SFCODE | 2 | WO | ok |
| `05F80028` | VDP2_CHCTLA | 2 | WO | ok |
| `05F8002A` | VDP2_CHCTLB | 2 | WO | ok |
| `05F8002C` | VDP2_BMPNA | 2 | WO | ok |
| `05F8002E` | VDP2_BMPNB | 2 | WO | ok |
| `05F80030` | VDP2_PNCN0 | 2 | WO | ok |
| `05F80032` | VDP2_PNCN1 | 2 | WO | ok |
| `05F80034` | VDP2_PNCN2 | 2 | WO | ok |
| `05F80036` | VDP2_PNCN3 | 2 | WO | ok |
| `05F80038` | VDP2_PNCR | 2 | WO | ok |
| `05F8003A` | VDP2_PLSZ | 2 | WO | ok |
| `05F8003C` | VDP2_MPOFN | 2 | WO | ok |
| `05F8003E` | VDP2_MPOFR | 2 | WO | ok |
| `05F80040` | VDP2_MPABN0 | 2 | WO | ok |
| `05F80042` | VDP2_MPCDN0 | 2 | WO | ok |
| `05F80044` | VDP2_MPABN1 | 2 | WO | ok |
| `05F80046` | VDP2_MPCDN1 | 2 | WO | ok |
| `05F80048` | VDP2_MPABN2 | 2 | WO | ok |
| `05F8004A` | VDP2_MPCDN2 | 2 | WO | ok |
| `05F8004C` | VDP2_MPABN3 | 2 | WO | ok |
| `05F8004E` | VDP2_MPCDN3 | 2 | WO | ok |
| `05F80050` | VDP2_MPABRA | 2 | WO | ok |
| `05F80052` | VDP2_MPCDRA | 2 | WO | ok |
| `05F80054` | VDP2_MPEFRA | 2 | WO | ok |
| `05F80056` | VDP2_MPGHRA | 2 | WO | ok |
| `05F80058` | VDP2_MPIJRA | 2 | WO | ok |
| `05F8005A` | VDP2_MPKLRA | 2 | WO | ok |
| `05F8005C` | VDP2_MPMNRA | 2 | WO | ok |
| `05F8005E` | VDP2_MPOPRA | 2 | WO | ok |
| `05F80060` | VDP2_MPABRB | 2 | WO | ok |
| `05F80062` | VDP2_MPCDRB | 2 | WO | ok |
| `05F80064` | VDP2_MPEFRB | 2 | WO | ok |
| `05F80066` | VDP2_MPGHRB | 2 | WO | ok |
| `05F80068` | VDP2_MPIJRB | 2 | WO | ok |
| `05F8006A` | VDP2_MPKLRB | 2 | WO | ok |
| `05F8006C` | VDP2_MPMNRB | 2 | WO | ok |
| `05F8006E` | VDP2_MPOPRB | 2 | WO | ok |
| `05F80070` | VDP2_SCXIN0 | 2 | WO | ok |
| `05F80072` | VDP2_SCXDN0 | 2 | WO | ok |
| `05F80074` | VDP2_SCYIN0 | 2 | WO | ok |
| `05F80076` | VDP2_SCYDN0 | 2 | WO | ok |
| `05F80078` | VDP2_ZMXIN0 | 2 | WO | ok |
| `05F8007A` | VDP2_ZMXDN0 | 2 | WO | ok |
| `05F8007C` | VDP2_ZMYIN0 | 2 | WO | ok |
| `05F8007E` | VDP2_ZMYDN0 | 2 | WO | ok |
| `05F80080` | VDP2_SCXIN1 | 2 | WO | ok |
| `05F80082` | VDP2_SCXDN1 | 2 | WO | ok |
| `05F80084` | VDP2_SCYIN1 | 2 | WO | ok |
| `05F80086` | VDP2_SCYDN1 | 2 | WO | ok |
| `05F80088` | VDP2_ZMXIN1 | 2 | WO | ok |
| `05F8008A` | VDP2_ZMXDN1 | 2 | WO | ok |
| `05F8008C` | VDP2_ZMYIN1 | 2 | WO | ok |
| `05F8008E` | VDP2_ZMYDN1 | 2 | WO | ok |
| `05F80090` | VDP2_SCXN2 | 2 | WO | ok |
| `05F80092` | VDP2_SCYN2 | 2 | WO | ok |
| `05F80094` | VDP2_SCXN3 | 2 | WO | ok |
| `05F80096` | VDP2_SCYN3 | 2 | WO | ok |
| `05F80098` | VDP2_ZMCTL | 2 | WO | ok |
| `05F8009A` | VDP2_SCRCTL | 2 | WO | ok |
| `05F8009C` | VDP2_VCSTAU | 2 | WO | ok |
| `05F8009E` | VDP2_VCSTAL | 2 | WO | ok |
| `05F800A0` | VDP2_LSTA0U | 2 | WO | ok |
| `05F800A2` | VDP2_LSTA0L | 2 | WO | ok |
| `05F800A4` | VDP2_LSTA1U | 2 | WO | ok |
| `05F800A6` | VDP2_LSTA1L | 2 | WO | ok |
| `05F800A8` | VDP2_LCTAU | 2 | WO | ok |
| `05F800AA` | VDP2_LCTAL | 2 | WO | ok |
| `05F800AC` | VDP2_BKTAU | 2 | WO | ok |
| `05F800AE` | VDP2_BKTAL | 2 | WO | ok |
| `05F800B0` | VDP2_RPMD | 2 | WO | ok |
| `05F800B2` | VDP2_RPRCTL | 2 | WO | ok |
| `05F800B4` | VDP2_KTCTL | 2 | WO | ok |
| `05F800B6` | VDP2_KTAOF | 2 | WO | ok |
| `05F800B8` | VDP2_OVPNRA | 2 | WO | ok |
| `05F800BA` | VDP2_OVPNRB | 2 | WO | ok |
| `05F800BC` | VDP2_RPTAU | 2 | WO | ok |
| `05F800BE` | VDP2_RPTAL | 2 | WO | ok |
| `05F800C0` | VDP2_WPSX0 | 2 | WO | ok |
| `05F800C2` | VDP2_WPSY0 | 2 | WO | ok |
| `05F800C4` | VDP2_WPEX0 | 2 | WO | ok |
| `05F800C6` | VDP2_WPEY0 | 2 | WO | ok |
| `05F800C8` | VDP2_WPSX1 | 2 | WO | ok |
| `05F800CA` | VDP2_WPSY1 | 2 | WO | ok |
| `05F800CC` | VDP2_WPEX1 | 2 | WO | ok |
| `05F800CE` | VDP2_WPEY1 | 2 | WO | ok |
| `05F800D0` | VDP2_WCTLA | 2 | WO | ok |
| `05F800D2` | VDP2_WCTLB | 2 | WO | ok |
| `05F800D4` | VDP2_WCTLC | 2 | WO | ok |
| `05F800D6` | VDP2_WCTLD | 2 | WO | ok |
| `05F800D8` | VDP2_LWTA0U | 2 | WO | ok |
| `05F800DA` | VDP2_LWTA0L | 2 | WO | ok |
| `05F800DC` | VDP2_LWTA1U | 2 | WO | ok |
| `05F800DE` | VDP2_LWTA1L | 2 | WO | ok |
| `05F800E0` | VDP2_SPCTL | 2 | WO | ok |
| `05F800E2` | VDP2_SDCTL | 2 | WO | ok |
| `05F800E4` | VDP2_CRAOFA | 2 | WO | ok |
| `05F800E6` | VDP2_CRAOFB | 2 | WO | ok |
| `05F800E8` | VDP2_LNCLEN | 2 | WO | ok |
| `05F800EA` | VDP2_SFPRMD | 2 | WO | ok |
| `05F800EC` | VDP2_CCCTL | 2 | WO | ok |
| `05F800EE` | VDP2_SFCCMD | 2 | WO | ok |
| `05F800F0` | VDP2_PRISA | 2 | WO | ok |
| `05F800F2` | VDP2_PRISB | 2 | WO | ok |
| `05F800F4` | VDP2_PRISC | 2 | WO | ok |
| `05F800F6` | VDP2_PRISD | 2 | WO | ok |
| `05F800F8` | VDP2_PRINA | 2 | WO | ok |
| `05F800FA` | VDP2_PRINB | 2 | WO | ok |
| `05F800FC` | VDP2_PRIR | 2 | WO | ok |
| `05F80100` | VDP2_CCRSA | 2 | WO | ok |
| `05F80102` | VDP2_CCRSB | 2 | WO | ok |
| `05F80104` | VDP2_CCRSC | 2 | WO | ok |
| `05F80106` | VDP2_CCRSD | 2 | WO | ok |
| `05F80108` | VDP2_CCRNA | 2 | WO | ok |
| `05F8010A` | VDP2_CCRNB | 2 | WO | ok |
| `05F8010C` | VDP2_CCRR | 2 | WO | ok |
| `05F8010E` | VDP2_CCRLB | 2 | WO | ok |
| `05F80110` | VDP2_CLOFEN | 2 | WO | ok |
| `05F80112` | VDP2_CLOFSL | 2 | WO | ok |
| `05F80114` | VDP2_COAR | 2 | WO | ok |
| `05F80116` | VDP2_COAG | 2 | WO | ok |
| `05F80118` | VDP2_COAB | 2 | WO | ok |
| `05F8011A` | VDP2_COBR | 2 | WO | ok |
| `05F8011C` | VDP2_COBG | 2 | WO | ok |
| `05F8011E` | VDP2_COBB | 2 | WO | ok |
| `05FE0000` | SCU_D0R | 4 | RW | ok |
| `05FE0004` | SCU_D0W | 4 | RW | ok |
| `05FE0008` | SCU_D0C | 4 | WO | ok |
| `05FE000C` | SCU_D0AD | 4 | WO | ok |
| `05FE0010` | SCU_D0EN | 4 | WO | ok |
| `05FE0014` | SCU_D0MD | 4 | WO | ok |
| `05FE0020` | SCU_D1R | 4 | RW | ok |
| `05FE0024` | SCU_D1W | 4 | RW | ok |
| `05FE0028` | SCU_D1C | 4 | WO | ok |
| `05FE002C` | SCU_D1AD | 4 | WO | ok |
| `05FE0030` | SCU_D1EN | 4 | WO | ok |
| `05FE0034` | SCU_D1MD | 4 | WO | ok |
| `05FE0040` | SCU_D2R | 4 | RW | ok |
| `05FE0044` | SCU_D2W | 4 | RW | ok |
| `05FE0048` | SCU_D2C | 4 | WO | ok |
| `05FE004C` | SCU_D2AD | 4 | WO | ok |
| `05FE0050` | SCU_D2EN | 4 | WO | ok |
| `05FE0054` | SCU_D2MD | 4 | WO | ok |
| `05FE0060` | SCU_DSTP | 4 | WO | ok |
| `05FE007C` | SCU_DSTA | 4 | RO | ok |
| `05FE0080` | SCU_PPAF | 4 | RW | ok |
| `05FE0084` | SCU_PPD | 4 | WO | ok |
| `05FE0088` | SCU_PDA | 4 | WO | ok |
| `05FE008C` | SCU_PDD | 4 | RW | ok |
| `05FE0090` | SCU_T0C | 4 | WO | ok |
| `05FE0094` | SCU_T1S | 4 | WO | ok |
| `05FE0098` | SCU_T1MD | 4 | WO | ok |
| `05FE00A0` | SCU_IMS | 4 | WO | ok |
| `05FE00A4` | SCU_IST | 4 | RW | ok |
| `05FE00A8` | SCU_AIACK | 4 | WO | ok |
| `05FE00B0` | SCU_ASR0 | 4 | WO | ok |
| `05FE00B4` | SCU_ASR1 | 4 | WO | ok |
| `05FE00B8` | SCU_AREF | 4 | WO | ok |
| `05FE00C4` | SCU_RSEL | 4 | RW | ok |
| `05FE00C8` | SCU_VER | 4 | RO | ok |
| `FFFFFE00` | SH2_SMR | 1 | RW | not decoded by Mednafen |
| `FFFFFE01` | SH2_BRR | 1 | RW | not decoded by Mednafen |
| `FFFFFE02` | SH2_SCR | 1 | RW | not decoded by Mednafen |
| `FFFFFE03` | SH2_TDR | 1 | RW | not decoded by Mednafen |
| `FFFFFE04` | SH2_SSR | 1 | RW | not decoded by Mednafen |
| `FFFFFE05` | SH2_RDR | 1 | RO | not decoded by Mednafen |
| `FFFFFE10` | SH2_TIER | 1 | RW | ok |
| `FFFFFE11` | SH2_FTCSR | 1 | RW | ok |
| `FFFFFE12` | SH2_FRCH | 1 | RW | ok |
| `FFFFFE13` | SH2_FRCL | 1 | RW | ok |
| `FFFFFE14` | SH2_OCRH | 1 | RW | ok |
| `FFFFFE15` | SH2_OCRL | 1 | RW | ok |
| `FFFFFE16` | SH2_TCR | 1 | RW | ok |
| `FFFFFE17` | SH2_TOCR | 1 | RW | ok |
| `FFFFFE18` | SH2_FICRH | 1 | RO | ok |
| `FFFFFE19` | SH2_FICRL | 1 | RO | ok |
| `FFFFFE60` | SH2_IPRB | 2 | RW | ok |
| `FFFFFE62` | SH2_VCRA | 2 | RW | ok |
| `FFFFFE64` | SH2_VCRB | 2 | RW | ok |
| `FFFFFE66` | SH2_VCRC | 2 | RW | ok |
| `FFFFFE68` | SH2_VCRD | 2 | RW | ok |
| `FFFFFE71` | SH2_DRCR0 | 1 | RW | ok |
| `FFFFFE72` | SH2_DRCR1 | 1 | RW | ok |
| `FFFFFE80` | SH2_WTCSR | 1 | KW | ok |
| `FFFFFE81` | SH2_WTCNT | 1 | KW | ok |
| `FFFFFE83` | SH2_RSTCSR | 1 | KW | ok |
| `FFFFFE91` | SH2_SBYCR | 1 | RW | ok |
| `FFFFFE92` | SH2_CCR | 1 | RW | ok |
| `FFFFFEE0` | SH2_ICR | 2 | RW | ok |
| `FFFFFEE2` | SH2_IPRA | 2 | RW | ok |
| `FFFFFEE4` | SH2_VCRWDT | 2 | RW | ok |
| `FFFFFF00` | SH2_DVSR | 4 | RW | ok |
| `FFFFFF04` | SH2_DVDNT | 4 | RW | ok |
| `FFFFFF08` | SH2_DVCR | 4 | RW | ok |
| `FFFFFF0C` | SH2_VCRDIV | 4 | RW | ok |
| `FFFFFF10` | SH2_DVDNTH | 4 | RW | ok |
| `FFFFFF14` | SH2_DVDNTL | 4 | RW | ok |
| `FFFFFF18` | SH2_DVDNTH_SHADOW | 4 | RW | ok |
| `FFFFFF1C` | SH2_DVDNTL_SHADOW | 4 | RW | ok |
| `FFFFFF40` | SH2_BARAH | 2 | RW | not decoded by Mednafen |
| `FFFFFF42` | SH2_BARAL | 2 | RW | not decoded by Mednafen |
| `FFFFFF44` | SH2_BAMRAH | 2 | RW | not decoded by Mednafen |
| `FFFFFF46` | SH2_BAMRAL | 2 | RW | not decoded by Mednafen |
| `FFFFFF48` | SH2_BBRA | 2 | RW | not decoded by Mednafen |
| `FFFFFF60` | SH2_BARBH | 2 | RW | not decoded by Mednafen |
| `FFFFFF62` | SH2_BARBL | 2 | RW | not decoded by Mednafen |
| `FFFFFF64` | SH2_BAMRBH | 2 | RW | not decoded by Mednafen |
| `FFFFFF66` | SH2_BAMRBL | 2 | RW | not decoded by Mednafen |
| `FFFFFF68` | SH2_BBRB | 2 | RW | not decoded by Mednafen |
| `FFFFFF70` | SH2_BDRBH | 2 | RW | not decoded by Mednafen |
| `FFFFFF72` | SH2_BDRBL | 2 | RW | not decoded by Mednafen |
| `FFFFFF74` | SH2_BDMRBH | 2 | RW | not decoded by Mednafen |
| `FFFFFF76` | SH2_BDMRBL | 2 | RW | not decoded by Mednafen |
| `FFFFFF78` | SH2_BRCR | 2 | RW | not decoded by Mednafen |
| `FFFFFF80` | SH2_SAR0 | 4 | RW | ok |
| `FFFFFF84` | SH2_DAR0 | 4 | RW | ok |
| `FFFFFF88` | SH2_TCR0 | 4 | RW | ok |
| `FFFFFF8C` | SH2_CHCR0 | 4 | RW | ok |
| `FFFFFF90` | SH2_SAR1 | 4 | RW | ok |
| `FFFFFF94` | SH2_DAR1 | 4 | RW | ok |
| `FFFFFF98` | SH2_TCR1 | 4 | RW | ok |
| `FFFFFF9C` | SH2_CHCR1 | 4 | RW | ok |
| `FFFFFFA0` | SH2_VCRDMA0 | 4 | RW | ok |
| `FFFFFFA8` | SH2_VCRDMA1 | 4 | RW | ok |
| `FFFFFFB0` | SH2_DMAOR | 4 | RW | ok |
| `FFFFFFE0` | SH2_BCR1 | 4 | RW | ok |
| `FFFFFFE4` | SH2_BCR2 | 4 | RW | ok |
| `FFFFFFE8` | SH2_WCR | 4 | RW | ok |
| `FFFFFFEC` | SH2_MCR | 4 | RW | ok |
| `FFFFFFF0` | SH2_RTCSR | 4 | RW | ok |
| `FFFFFFF4` | SH2_RTCNT | 4 | RW | ok |
| `FFFFFFF8` | SH2_RTCOR | 4 | RW | ok |
