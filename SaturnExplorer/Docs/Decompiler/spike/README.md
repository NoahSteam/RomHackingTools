# Decompiler feasibility spike

Throwaway code behind `../SPIKE_RESULTS.md`. It is **not** part of the SaturnExplorer build and
is not the production design; see `../DESIGN.md` for that.

| File | Purpose |
|---|---|
| `CMakeLists.txt` | Builds Ghidra's decompiler as a static library from a Ghidra checkout (`-DGHIDRA_DECOMP_DIR`), the `sleigh` compiler, the spike, and `yss_dump` (`-DSE_BUILD` pointing at a SaturnExplorer build). |
| `sh2_decomp_spike.cpp` | `LoadImage` over captured Saturn regions with bus-address folding, a `SleighArchitecture` subclass for `SuperH:BE:32:SH-2`, literal-pool detection from raw p-code, decompile, and markup → address resolution. |
| `yss_dump.cpp` | Pulls LWRAM, HWRAM and both SH-2 register files out of a `.yss` through SaturnExplorer's savestate driver. |
| `tools/fixture1.py` | Hand-assembled SH-2 fixture, every word checked against Capstone. |
| `tools/scan.py` | Finds the function containing an address in a HWRAM dump (back-scan to `rts`) and lists it with Capstone. |
| `tools/compare.py` | Compares the spike's SLEIGH listing with Capstone instruction by instruction. |
| `real_06031598_listing.txt` | The 143-instruction SLEIGH listing of the real-game fixture that Capstone agreed with. |

Reproduction steps are at the end of `../SPIKE_RESULTS.md`.
