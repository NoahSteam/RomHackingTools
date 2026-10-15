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

## Disagreements

| Register | Table | Manual | Mednafen | Decision |
|---|---|---|---|---|
| SCU_AIACK (`05FE00A8`) | read/write | read/write | reads 0 (its read is commented out) | Manual. No note claims a value. |
| SCSP_MCIEB (`05B0042A`) | write-only | paired with the readable SCIEB | reads 0 | Mednafen (the backend), recorded in the table's comment. |
| SH2_SMR..SH2_RDR (`FFFFFE00`-`05`) | per SH7604 | SCI registers | not emulated | Manual. |
| SH2_BARAH..SH2_BRCR (`FFFFFF40`-`78`) | per SH7604 | UBC registers | not emulated | Manual. |
| SH2_DVDNTH/L_SHADOW (`FFFFFF18`/`1C`) | read/write | not in the register table | read and written | Mednafen; every access says it is Mednafen's. |
| 0xA partition | volatile, no aliases | (not in the SH7604 table) | associative purge | Mednafen; `SaturnMemory.txt` calls it cache-through. |
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
- **Split fields:**
  - SCSP MSLC/CA, DTLG, SCIPD, MCIPD (scsp.inc);
  - SCU IST, PPAF (scu.inc);
  - CD HIRQ and CR1-CR4 (cdb.cpp);
  - SMPC PDR1-2 (smpc.cpp).
- **Read effects:**
  - PPAF clears V and E and the DSP interrupt;
  - PDD returns data and advances the address only while the DSP is stopped;
  - the MIBUF and CD FIFO reads pop;
  - CR4 marks the response read;
  - FRCH and FICRH latch the low byte;
  - FTCSR, CHCR, DMAOR and RTCSR arm their "write 0 after reading 1" clear.

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
| `05FE00A8` | SCU_AIACK | 4 | RW | DIFFERS: Mednafen read returns 0/nothing |
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
