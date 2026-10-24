# Captured inspection implementation and validation

Branch: `codex/decompiler-variable-inspection`, based on `528b448253a52ee65db1a86a165f0fe5b90a28e2`.
Scope: PLAN.md P0–P3, using the existing Registers window. Optional embedded registers,
register/stack-local lifetime recovery, general expressions and historical values are excluded.

## Implementation evidence

- `CapturedValueInspectionTests`: endian/signed/bool/raw decoding, complete scalar reads,
  overflow/partition/device refusal, cached/cache-through/repeated WRAM aliases, loaded BIOS
  address zero, null pointers, bounded/escaped ASCII, truncation and canonical action ranges.
- `CapturedInspectionMetadataTests`: synthetic SH-2 scalar loads **and stores** at widths
  1/2/4, including user-volatile WRAM; pointer-valued globals; typed pointer literals;
  direct VARREF and null-VARREF packed markup; implied object and explicit address forms;
  refusal of partial/split/dynamic/stack/array/field mappings. Native fixtures use the same
  engine adapter and markup decoder as production, with actual engine symbols and Varnodes.
- `DecompilerPanelTests`: real headless ImGui panel, snapshot values and zero extra backend
  reads, CPU-selected Registers request, distinct canonical storage/target Memory and Watch
  requests, BIOS/device/null action refusal, missing registers, capture/restore freshness,
  CPU/result popup invalidation, UTF-8/scaled/clamped token hit testing and existing navigation.
- `LiveDecompilerCaptureTests`: same-frame, unchanged-memory captures whose master or slave
  registers alone change must advance the captured identity and contain the new registers.

## Executed checks

The final decompiler-enabled build passed, including a repeat build. All 16 selected
acceptance tests passed across the final parallel run and isolated retries. The parallel
run passed 14 tests (fault injection included); the engine and runner tests exceeded their
existing 60/120-second limits under concurrent load. The container's four-CPU quota also
covers the fault test's subprocesses. With no competing tests, the unchanged binaries
passed in 36.96 and 42.99 seconds respectively. No timeout or assertion was relaxed.

The final frontend passed `--selftest`, and opened a bundled Yabause savestate under
Xvfb/software OpenGL before a clean window close. A verbose real-game decompiler fixture
run completed with `all passed` and no skipped fixture message. The deterministic metadata
fixtures also passed, including every normal and user-volatile scalar load/store case.

The Linux cloud environment uses GCC 14.2, CMake 3.31 and Ninja, with SDL2/OpenGL development
packages extracted under `/workspace/.cloud-setup/sysroot`. Source the local activation script
before using the commands below. Build outputs stay outside the checkout.

```sh
source /workspace/.cloud-setup/activate.sh
cmake -S /workspace/RomHackingTools/SaturnExplorer -B /workspace/build/saturn-explorer \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DSE_ENABLE_DECOMPILER=ON \
  -DCMAKE_PROJECT_INCLUDE=/workspace/.cloud-setup/cmake-overrides.cmake
cmake --build /workspace/build/saturn-explorer --parallel 4
ctest --test-dir /workspace/build/saturn-explorer --output-on-failure --parallel 4 \
  -R 'CapturedInspectionMetadata|CapturedValueInspection|Decompiler|MemorySnapshot|Sh2RegInfo|MemoryBackend|ExpressionResolver|Watch|PanelInteraction'
# Isolated retries for the two timing-sensitive tests after the parallel run:
ctest --test-dir /workspace/build/saturn-explorer --output-on-failure --parallel 1 \
  -R '^SaturnExplorer(DecompilerRunner|Sh2DecompilerEngine)Tests$'
```

The existing environment override retains Debug assertions and symbols, optimizes
`Sh2Decompiler.cpp` with `-O1` for the pre-existing C++14 constexpr-linkage issue, and
optimizes the exhaustive `Sh2DecompilerEngineTests.cpp` loops with `-O2` to meet their
existing timeout. No test assertion, test timeout, engine volatility rule, vendored source
or generated SLEIGH artifact was changed for this feature.

The decompiler-disabled Debug build uses the same source checkout and a separate
`/workspace/build/saturn-explorer-no-decompiler` directory. Its full build, frontend
`--selftest`, and four tests (`ExpressionResolver`, `PanelInteraction`, `MemoryBackend`,
`Sh2RegInfo`) passed. The focused final metadata suite also passed after fixing annotation
export. Initial failures were investigated rather than suppressed: native fixtures needed
non-overlapping input Varnodes and automatic structure field layout; real volatile globals
needed the registered volatile annotation path.

## Independent reviews

| Review | Findings addressed | Final source result |
|---|---|---|
| Correctness | Distinguish explicit `&global` PTRSUB temporaries from implied object loads/stores; prove both markup forms; support exact volatile read/write annotations; reject saturated token spans | Clear after fixes; focused metadata suite passed |
| Reuse | Checked existing Registers, Watch, Memory navigation, canonical-address and compile-boundary reuse | No blocking findings |
| Performance | Avoid measuring tokens on every visible row; hit-test only the hovered row | Clear after fix; bounded snapshot reads, no backend reads/RAM copies/recursive evaluation |

Review agents were independent of the implementation owners. Their source review does not
stand in for the coordinator's acceptance runs.

## Remaining platform checks and limits

Windows/macOS native visual checks for scrolling, menu ownership and clipboard addresses
were not executable in this Linux environment. Linux real-panel interaction tests cover
hit testing, action routing and popup ownership; they are not a claim that those native
platform checks ran. Optional embedded register presentation was deliberately omitted.

## Follow-up coverage (post-review)

- Real-printer fixtures: a named hardware register load/store prints `VDP2_TVMD` and binds the
  object; a register passed as a pointer prints `(T *)&VDP2_TVMD` and binds the address only. The
  engine registers only volatile hardware symbols, so an implied load/store through the
  null-VARREF PTRSUB form stays covered by the native fixtures.
- The final decoder filters (member/array/`->` suffixes, character/string/named literals) have
  direct tests; disabling each filter fails them.
- App's restore gate, selected-CPU Registers routing and Watch expression are
  `Debug/InspectionRouting.h` helpers with unit tests. App's call sites are thin and still
  untested; `App.cpp` was only syntax-checked here (no SDL).

