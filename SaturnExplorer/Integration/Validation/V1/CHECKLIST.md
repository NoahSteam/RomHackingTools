# V1 run checklist

Run date: ______  emulator revision / title string: ______________  cache mode (`data` / `full`): ______
Observed frame cycles `F` (Frame tab, any steady frame): ______   68000/SH-2 ratio read: ______
Read steady frames only (5..118 of each phase). Pass = observed inside the Expected cell.
`EXPECTED.md` has the derivation for each row; **Expected numbers are unconfirmed predictions.**

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

## Findings

(Anything that disagreed: which side is wrong, the profiler or the derivation in EXPECTED.md.)
