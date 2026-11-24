# Expected bus-request counts: cache fills and SH-2 DMAC (F8)

Written from the patched Mednafen source BEFORE `perf_bus_harness.cpp` existed; the harness asserts exactly
these numbers (each row has an id the harness prints). Line numbers are in the tree as patched by the current
`apply.py` (pinned checkout); the function / macro names are the stable anchors.

The unit is one **request to the bus state controller**: the hook `SeSsPerfBus(A, isWrite)` is the first
statement of `SH7095::BSC_BusWrite` (`sh7095.inc:2167`) and `SH7095::BSC_BusRead` (`sh7095.inc:2245`), nothing
else increments the bus buckets. It runs once per call, before the CS0 / CS3 / CS1-2 split, so a 32-bit access
to a 16-bit CS0 region (BIOS, SMPC, backup, `BusRW_DB_CS0<uint16>` twice, `sh7095.inc:2174-2186`,
`2252-2262`) is still ONE request. The bucket is `SeSsBusSlot[(A >> 16) & 2047]` of the **27-bit** address
(`ss.cpp:37-72`): both callers below mask with `0x07FFFFFF` first, so the cache-through mirror
`0x2xxxxxxx` and the cached `0x0xxxxxxx` address land in the same bucket.

Callers of `BSC_BusRead/Write`:

* `ExtBusRead_INLINE` / `ExtBusWrite_INLINE` (`sh7095.inc:2320`, `2357`), reached from `MemRead` / `MemWrite`
  (`sh7095.inc:2597`, `2727`) for every access that really leaves the CPU.
* `DMA_DoTransfer` (`sh7095.inc:547`), which calls `BSC_Bus*` directly (the on-chip DMAC).

Nothing else reaches the bus: cache hits, the on-chip register space, the associative-purge space and the
cache address / data array spaces never call it.

## Cache (`MemRead`, `sh7095.inc:2597`)

`MemRead` switches on `region = A >> 29`: 0 = cached, 1 = cache-through (`0x2xxxxxxx`), 2 and 5 =
associative purge, 3 = address array, 4 and 6 = data array, 7 = on-chip registers. The data cache is
emulated in every mode; the **instruction** fetch goes through it only when `NeedEmuICache` (mode `ON`);
in mode `OFF` `FetchIF()` (`sh7095.inc:3154`) reads `SH7095_FastMap` directly and never calls `MemRead`.

| id | Access | Expected requests |
|---|---|---|
| C1 | CCR.CE = 0, any read of size 1/2/4 at region 0 or 1 | **1 read** on the bucket of A |
| C2 | CE = 1, region 0, **miss**, replacement allowed (`LRU_Replace_Tab[..] | CCRC_Replace_OR[isInstr]` >= 0), size 1/2/4 | **4 reads** (`sh7095.inc:2646-2653`: one `ExtBusRead(uint32, false)` + three `ExtBusRead(uint32, true)`, all 32-bit, at `(A & ~0xF) + 0,4,8,12` in rotated order), all on the bucket of A; the size of the triggering access does not matter |
| C3 | CE = 1, region 0, **hit** (any size) | **0** |
| C4 | CE = 1, miss, `CCR.OD` set, data read (`IsInstr` false) | **1 read** (`CCRC_Replace_OR[0] = -1` -> `way_match < 0` -> `goto EBRCase`, a single uncached read of the access's own size); a hit is still 0 |
| C5 | CE = 1, miss, `CCR.ID` set, instruction read (`MRFPI`) or `MRFP*_I` read (`IsInstr = -1`, `(bool)-1` selects the ID bit) | **1 read**; with only `OD` set the same read fills (4) |
| C6 | Read at region 1 (`0x2xxxxxxx`, cache-through), CE either | **1 read**, never a fill, never a hit |
| C7 | After `SetCCR(CE | CP)` (cache purge, `sh7095.inc:2836`) a line that hit before | miss again: **4 reads** to refill, then 0 on the following reads |
| C8 | Associative purge: read or write at region 2 or 5 (`Cache_AssocPurge`) | **0** requests; the next cached read of that line is a miss (4) |
| C9 | Cache address / data array (regions 3, 4, 6) and on-chip registers (region 7), read or write | **0** |
| C10 | Write, any size, region 0 with CE (`Cache_WriteUpdate` only updates a line that already hits; no allocate) or region 1 | **1 write**; the write-through is unconditional |
| C11 | Fill from a 16-bit CS0 device (BIOS at `0x0`) | still **4 reads** on BIOS (each 32-bit request is one) |
| C12 | Fill from CS3 (`0x06000000`, WRAM-H) and from A/B-bus space (VDP2 `0x05E00000` via `0x25E...` is region 1, so uncached; a cached region-0 read of `0x05E00000` fills) | 4 reads on the device of A |
| C13 | Instruction fetch, mode OFF (`Step<0,false,..>`) | **0** requests for any number of instructions |
| C14 | Instruction fetch, mode ON, CE = 1, N straight-line NOPs from a 16-byte-aligned PC | `MemReadInstr` runs once per 32-bit word (at `!(PC & 2)`); requests = **4 x number of distinct 16-byte lines fetched**; after N <= 8 steps = 4 |
| C15 | Instruction fetch, mode ON, CE = 0 | **1 read per 32-bit word fetched** |

The invariant behind C2/C14: *bus reads caused by the cache = 4 x (lines filled)*, and every filled line has a
valid tag afterwards.

## SH-2 DMAC (`DMA_DoTransfer`, `sh7095.inc:547`; `DMA_Update`, `664`)

A transfer runs only while `DMA_RunCond(ch)` (`sh7095.inc:517`): `DMAOR & 7 == 1` (DME set, NMIF and AE
clear) and `CHCR & 3 == 1` (DE set, TE clear). `DMA_Update(et)` calls `DMA_DoTransfer` while
`DMA_Timestamp < et`; the harness passes a large `et`. TCR counts transfer units. `CHCR.TS` (bits 10-11)
selects the unit, `CHCR.TB` (bit 4) selects burst over cycle-steal. The read and write addresses are
`SAR/DAR & 0x07FFFFFF` aligned down to the unit, so the bucket is the 27-bit address's 64 KiB window.

| id | Transfer | Expected requests |
|---|---|---|
| D1 | TS = 0 (8-bit), K units, SAR in device A, DAR in device B | **K reads on A, K writes on B**, nothing else |
| D2 | TS = 1 (16-bit), K units | K reads on A, K writes on B |
| D3 | TS = 2 (32-bit), K units | K reads on A, K writes on B |
| D4 | TS = 3 (16-byte): each `DMA_DoTransfer` does **4 reads** unconditionally, then writes until TCR hits 0 (`sh7095.inc:627-640`) | K a multiple of 4: K reads, K writes. Otherwise **reads = 4*ceil(K/4)**, writes = K (the final batch over-reads); the writes go to DAR's bucket |
| D5 | Burst (`CHCR.TB`) vs cycle-steal | **identical counts** (the mode only feeds `DMA_InBurst`, which stalls the CPU); the harness checks `DMA_InBurst()` really differs |
| D6 | SAR and DAR in the same device | K reads and K writes on the same device |
| D7 | Source or destination in the A-bus dummy window (`0x05000000-0x057FFFFF`) or an unmapped hole (`0x05900000`, `0x05D80000`, `0x05FC0000`, `0x05FF0000`) | counted in `SE_PERF_BUS_ACCESS_X` (58..61): index `58 + xdev*2 + isWrite`, dummy xdev 0, unmapped xdev 1; the device buckets stay 0 |
| D8 | Odd SAR with TS = 1 or 2 (address error) | the unit that is in flight is still read and written (the AE check follows the access, `sh7095.inc:577-584`, `598-604`): **1 read, 1 write**, then `DMAOR.AE` stops the channel (`DMA_RunCond` false): total stays 1/1 whatever K |
| D9 | Channel not enabled (DME = 0, DE = 0, TE already set) | 0 requests |
| D10 | Both channels active (priority mode and round-robin `DMAOR.PR`) | totals are the sum of the two channels' D1-D4 counts; ordering differs, counts do not |
| D11 | The transfer also writes through `BSC_BusWrite`, a stubbed device (VDP1/VDP2/SCSP/CD) changes nothing | counted on the device's bucket; the harness stubs the devices, not the hook or the decoder |

Notes the harness depends on: a 16-bit DMA write of 32-bit-wide devices and the `SH2DMAHax` timestamp
adjustments (`*SH2DMAHax += 3` etc.) change timing only, never the count; and the DMAC never goes through
`ExtBusRead_INLINE`, so `DMA_PenaltyKludgeAccum` and the CPU timestamp are not involved.
