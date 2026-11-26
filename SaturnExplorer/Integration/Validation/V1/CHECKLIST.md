# V1 run checklist

Run date: ______  emulator revision / title string: ______________  cache mode (`data` / `full`): ______
Observed frame cycles `F` (Frame tab, any steady frame): ______   68000/SH-2 ratio read: ______
Read steady frames only (5..118 of each phase). Pass = observed inside the Expected cell.
`EXPECTED.md` has the derivation for each row. The expected numbers were written first; they were confirmed on a real run 2026-11-25 (Windows, Mednafen 1.32.1 patched, real BIOS, both cache modes, 48/48 checklist rows matched; exact matches for the phase 0 baseline, the 15 phase-3 bus buckets, VDP1 commands/work, SCU DMA byte counts, slave on-time and the 68000 phases; in-range: phase 1 busy 160,073, func_a/func_b 12,003/4,003, stall ratio 3.98, 68000/SH-2 clock ratio 0.42009).
Not checked on that run: the readings came through a headless recorder on the live connection, not the Profiler panel; hook-disable checks were done for slave on-time, VDP1 work and the bus buckets only (the F5, F9 and F11 hook checks were not run). The disc needed a manual fix to boot (an empty root directory made the BIOS drop to its CD player); `build_v1.py` now writes a real first file. The guest was changed after that run (see README: display bring-up for the backdrop colour); in the fake-machine simulation every steady frame performs exactly the same accesses at the same addresses as before (the change is one-time start-up code placed after all other payload bytes); not re-run on the emulator.

| Phase | Counter / view | Expected | Observed | Pass / fail | Notes |
|---|---|---|---|---|---|
| 0 IDLE | master sleep share | >= 99.8% (sleep in [F-600, F-30]) | | | |
| 0 | master stall | 0 | | | |
| 0 | slave on-cycles | 0 | | | |
| 0 | VDP1 commands / starts / work / discarded | 0 / 0 / 0 / 0 | | | |
| 0 | 68000 idle = total, executed | idle = total, executed 0 | | | |
| 0 | 68000 total / F | 0.4205 (320) or 0.3943 (352), +-0.5% | | | |
| 0 | bus requests, all devices except work RAM high and SCU | 0 | | | |
| 0 | SCU bus requests | reads 0 / writes 1 (IMS re-arm each frame) | | | |
| 0 | work RAM high reads / writes | 0..8 / 4..16 | | | |
| 1 BUSY | master busy cycles | [160,028, 160,598] | | | |
| 1 | everything else | as phase 0 | | | |
| 2 FUNC | func_a self cycles | [12,001, 12,038] | | | |
| 2 | func_b self cycles | [4,001, 4,038] | | | |
| 2 | func_a / func_b | [2.96, 3.02] | | | |
| 3 BUS | BIOS reads | 10 | | | |
| 3 | SMPC reads | 20 | | | |
| 3 | backup RAM reads | 30 | | | |
| 3 | work RAM low reads / writes | 40 / 11 | | | |
| 3 | CS0 other reads | 50 | | | |
| 3 | A-bus CS0 reads | 60 | | | |
| 3 | A-bus CS1 reads | 70 | | | |
| 3 | CD block reads | 80 | | | |
| 3 | SCSP reads / writes | 90 / 15 | | | |
| 3 | VDP1 reads / writes | 100 / 13 | | | |
| 3 | VDP2 reads / writes | 110 / 14 | | | |
| 3 | SCU reads / writes | 120 / 1 (baseline IMS re-arm) | | | |
| 3 | work RAM high reads / writes | 130 / 12 + (4..16) | | | |
| 3 | A-bus dummy reads | 140 | | | |
| 3 | unmapped reads | 150 | | | |
| 4 VDP1_ONE | draw starts / commands / work / overruns | 1 / 2 / 32 / 0 | | | |
| 5 VDP1_MANY | draw starts / commands / work / overruns | 1 / 65 / 1,040 / 0 | | | |
| 6 DMA_BIG | transfers L0 / bytes L0 / route 7 | 1 / 16384 / 16384 | | | |
| 6 | master stall | > 0 | | | |
| 6 | VDP1 discarded | 0 | | | |
| 7 DMA_SMALL | transfers / bytes / route 7 | 1 / 4096 / 4096 | | | |
| 7 | stall(6) / stall(7) | [3.0, 5.0] | | | |
| 8 DMA_DRAW | commands / work | <= 401 / <= 6,416 (401 / 6,416 if complete) | | | |
| 8 | VDP1 discarded | > 0 | | | |
| 8 | DMA transfers / bytes | 1 / 16384 | | | |
| 9 SSH_ON | slave on-cycles, command frame | 0 < on < F | | | |
| 9 | slave on-cycles, steady | F | | | |
| 10 SSH_OFF | slave on-cycles, command frame | 0 < on < F | | | |
| 10 | slave on-cycles, steady | 0 | | | |
| 11 M68K_STOP | idle / executed | idle = total / 0 | | | |
| 12 M68K_BUSY | idle / executed | 0 / total | | | |
| 13 M68K_OFF | idle / executed | total / 0 | | | |
| all | DSP cycles | 0 | | | |
| all | idle + executed share | 100% | | | |

## Disabling-a-hook checks (plan requirement)

For each, build the emulator with the named hook disabled and confirm the listed row fails.

| Hook disabled | Row that must fail | Failed as expected? |
|---|---|---|
| F2 slave edges | phase 9 / 10 command frame | |
| F4 debit placement | phase 5 work | |
| F5 idle count | phase 11 idle | |
| F8 classification | phase 3 dummy / unmapped rows | |
| F11 DMA counters | phase 6 route 7 | |
| F9 ownership | phase 2 rows | |

## Hardware events (see `EXPECTED_EVENTS.md`)

Arm all five Log chips. Counts are per steady frame, read as a sum over a 16-frame window divided by 16.

| Phase | Source / kind | Expected | Observed | Pass / fail | Notes |
|---|---|---|---|---|---|
| all | SH2_EXCEPTION (master) | 1: vector 0x40, INT, level 15, handler = `vbin`, PC = `main_loop` + 2 | | | |
| all | SCU_INT_ACCEPT | 1: bit 0, level 15, vector 0x40; IPending bit 0 clear | | | |
| all | SCU_INT_MASK | 1: new 0xBFFE, old 0xBFFF | | | |
| all | VDP2_VBLANK | 1 IN + 1 OUT | | | |
| all | VDP1_FB_SWAP | 1: FBCR 0000, TVMR 0000, FB alternating | | | |
| all | FB_ERASE, VDP2_DISPLAY, DMAC, SCU_DMA_ILLEGAL, CD, SUPPRESSED, lost row | 0 | | | |
| all | any other exception | 0 | | | |
| 0 IDLE | SMPC_COMMAND (first frame) | 0x07, 0x03 | | | |
| 4 VDP1_ONE | DRAW_START / DRAW_END | 1 / 1 (end address 0x20) | | | |
| 5 VDP1_MANY | DRAW_START / DRAW_END | 1 / 1 (end address 0x800) | | | |
| 6 DMA_BIG | SCU_DMA_START | arg0 0x12010000, src 0x06020000, dst 0x05C20000, bytes 16384 | | | |
| 6 | SCU_DMA_END | 1 (level 0) | | | |
| 7 DMA_SMALL | SCU_DMA_START bytes / END | 4096 / 1 | | | |
| 8 DMA_DRAW | SCU_DMA_START / END | as phase 6 | | | |
| 8 | DRAW_END | one per start; address 0x3200 reason 0, or reason 1 if cut | | | |
| 9 SSH_ON | SMPC_COMMAND (first frame) | 0x02 | | | |
| 9 | slave events | not asserted (write down what appears) | | | |
| 10 SSH_OFF | SMPC_COMMAND (first frame) / slave events after | 0x03 / none | | | |
| 11 M68K_STOP | SMPC_COMMAND (first frame) | 0x07, 0x06 | | | |
| 12 M68K_BUSY | SMPC_COMMAND (first frame) | 0x07, 0x06 | | | |
| 13 M68K_OFF | SMPC_COMMAND (first frame) | 0x07 | | | |
| 1, 2, 3, 11-13 | any event beyond the common set, 68000 or SCSP events | none | | | |

### Dropping a hardware-event hook (fill in)

| Hook dropped | Row that must change | Changed as expected? |
|---|---|---|
| `SE_HWEV_HOOK_EXC_INT` | all phases: no SH2_EXCEPTION | |
| `SE_HWEV_HOOK_SCU_INT_ACCEPT` | all phases: no SCU_INT_ACCEPT | |
| `SE_HWEV_HOOK_SCU_INT_MASK` | all phases: no SCU_INT_MASK | |
| `SE_HWEV_HOOK_VBLANK` | all phases: no VDP2_VBLANK | |
| `SE_HWEV_HOOK_VDP1_FB_SWAP` | all phases: no VDP1_FB_SWAP | |
| `SE_HWEV_HOOK_VDP1_DRAW_START` / `_DRAW_END` | phases 4, 5, 8 | |
| `SE_HWEV_HOOK_SCU_DMA_START` / `_DMA_END` | phases 6, 7, 8 | |
| `SE_HWEV_HOOK_SMPC_COMMAND` | phases 0, 9-13 first frames | |
| chips disarmed (mask 0) | no event in any phase | |

Hooks V1 cannot show (NMI and the other exceptions, DMAC, SCU DMA illegal, VDP1 abort and erase, VDP2
display, CD) are not listed: see `EXPECTED_EVENTS.md`.

## Findings

(Anything that disagreed: which side is wrong, the profiler or the derivation in EXPECTED.md.)
