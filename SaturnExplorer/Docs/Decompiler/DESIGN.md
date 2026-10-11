# SH-2 pseudocode panel — design

> Status: **design, revised after review.** Implementation steps and the binding contracts (A1–A7) are in [PLAN.md](PLAN.md); where this document and PLAN.md §A differ, PLAN.md wins. The feasibility gate the design depends on has been passed
> (see [SPIKE_RESULTS.md](SPIKE_RESULTS.md)): Ghidra's native C++ decompiler, built as a plain static
> library, decompiles real SH-2 bytes at real Saturn addresses from a SaturnExplorer memory capture,
> with no Java, no Ghidra installation, no subprocess. No production UI has been written.

## 1. Recommendation

Embed **Ghidra's decompiler C++ sources directly** (`Ghidra/Features/Decompiler/src/decompile/cpp`,
Apache-2.0, pinned to the 12.3 tree) as a vendored static library linked into the desktop FrontEnd
only, with the `SuperH:BE:32:SH-2` SLEIGH spec compiled at build time by Ghidra's own `sleigh`
compiler. Do **not** embed Rizin + rz-ghidra. The reasons are in §3; in one line: we need one
component (the decompiler) and Saturn-specific glue that is a few hundred lines, and rz-ghidra
brings a second analysis framework, a Ghidra fork, LGPL, C++20 and an SH-4-only arch mapping to get
the same engine.

Nothing changes in the portable core: Seam A (`SeDataSource.h`), Seam B (`SeHost.h`) and
`SE_ABI_VERSION` are untouched. The decompiler is a FrontEnd feature over `IMemoryBackend` and
`se_get_sh2_regs`, both of which already exist. The web build never sees a Ghidra header.

## 2. What the spike proved and what it did not

Proved (Linux, GCC 13):
- Zero-patch vendoring: 84 of Ghidra's `.cc` files compile as-is into an 11 MB static library;
  the `sleigh` compiler builds from the same tree and compiles `sh-2.slaspec` in milliseconds.
- `LoadImage` over captured LWRAM/HWRAM with Saturn address folding; `DataUnavailError` for
  BIOS/cartridge/on-chip stops flow cleanly.
- Correct output on a Capstone-verified hand-assembled fixture (indirect call through a literal
  pool, delay-slot argument move, conditional, prologue/epilogue) and on two real functions from a
  Yabause savestate in this repo, whose 143-instruction SLEIGH listing matches Capstone exactly.
- Literal-pool folding, callee registration, mirrored addresses, token→address mapping for
  navigation, timings in the low milliseconds.

Not proved, and therefore the first implementation gate (§9, Phase 1):
- MSVC and Apple clang builds. Ghidra itself ships the same sources built for `win_x86_64`,
  `win_arm_64`, `mac_x86_64` and `mac_arm_64` (`buildNatives.gradle`: `/EHsc /W3 /O2`, define
  `WINDOWS`; `-std=c++11` elsewhere), so the risk is low but it has not been exercised here.
- Behaviour on the live driver (the spike used a static savestate).

## 3. Direct Ghidra C++ versus Rizin + rz-ghidra

| | Direct Ghidra decompiler | Rizin + rz-ghidra |
|---|---|---|
| What is embedded | 84 `.cc` + headers (~3.5 MB source) + 5 SuperH spec files | `librz_core` and its plugin tree (meson; capstone, tree-sitter, libzip, lz4, xxhash, …) plus rz-ghidra (CMake, C++20, pugixml, a `rizinorg/ghidra` fork as submodule carrying **all** processors, each compiled to `.sla` at build time) |
| SH-2 selection | `SuperH:BE:32:SH-2:default`, chosen by us | rz-ghidra's `ArchMap.cpp` maps rizin's `sh` to `SuperH4` only; SH-2 needs the `ghidra.lang` override, and rizin's own `sh` analysis plugin (function finding, xrefs, ESIL) is SH-4 flavoured |
| Memory source | Our `LoadImage` reads a `MemorySnapshot` built from `IMemoryBackend`; folding and read-only literal pools are ours to define | Would need a custom `RzIO` plugin bridging `IMemoryBackend`, or a copy into an `RzBuffer`; rizin's section/map model in between |
| Addresses / endianness | SLEIGH spec is big-endian; addresses are whatever we register | Same engine; configured through rizin's `asm.*`/`analysis.*` config |
| Output markup | Build from `EmitMarkup` tokens (`opref` → instruction address); ~100 lines, demonstrated | `RzAnnotatedCode` with offsets, ready-made |
| Function discovery | Ours (§6.4); cheap, Saturn-aware | rizin `aaa` for free, but SH-4-centric and heavy (whole-image analysis) |
| Windows / macOS | Ghidra ships these targets; CMake, MSVC `/EHsc`, clang | rizin builds with meson/MSVC and on macOS; two build systems to carry |
| C++ standard | C++11 required; compiles as the project's C++14 | rz-ghidra requires C++20 |
| License | Apache-2.0 (+ zlib) | LGPL-3.0 (rizin) + LGPL-3.0 (rz-ghidra), Apache-2.0 underneath |
| Binary cost | +3.8 MB stripped (measured) | Tens of MB (rizin core + plugins + rz-ghidra) |
| Upstream tracking | One upstream; `VENDOR.md` with the commit; bump = copy files, rebuild | Three: rizin, rz-ghidra, and rz-ghidra's Ghidra fork, which lags Ghidra releases; version-locked pairs (`rz-x.y.z` tags) |
| Maintenance risk | Decompiler internals are stable across releases (the Makefile groups have barely moved); specs are data | Any of three upstreams moving breaks the pair |

rz-ghidra's genuine advantages are the annotated output and rizin's type/flag databases. The spike
reproduced the annotation in-process; types are out of scope for this feature (§10). Everything
else is cost without benefit for a tool that only needs "decompile this function from these bytes".

## 4. Architecture

```
UI thread (App::BuildUI, after ScopedContextSwap)             Decompiler worker thread (one, owns the engine)
┌────────────────────────────────────────────────┐             ┌─────────────────────────────────────────────┐
│ DecompilerPanel                                │  request    │ DecompilerRunner::WorkerMain                │
│  - CPU combo, entry, Follow PC, Refresh, status│ ──────────▶ │  - pops latest request per panel (coalesce) │
│  - lines + tokens, click → Request.jumpAssembly│             │  - Sh2Decompiler::Decompile(snapshot, req)  │
│  - right-click: rename / set entry / copy      │  result     │     ├ CaptureLoadImage (fold, regions)      │
│                                                │ ◀────────── │     ├ SaturnArchitecture (SH-2 SLEIGH)      │
│ App glue: snapshot capture, CaptureGuard,      │  (Poll)     │     ├ literal pools → readonly, callees     │
│  GoTo(cpu, addr), FunctionNames, settings      │             │     └ markup → DecompileResult tokens       │
└────────────────────────────────────────────────┘             └─────────────────────────────────────────────┘
          │ ReadRegionBytes(IMemoryBackend)  se_get_sh2_regs  SourceId  se_derive_serial  se_frame_number
          ▼
   MemorySnapshot (immutable, shared_ptr; LWRAM + HWRAM copies; key = {sourceId, deriveSerial, frame})
```

New code lives in `FrontEnd/src/Decompiler/` and `FrontEnd/third_party/ghidra-decompiler/`.

| Component | Responsibility |
|---|---|
| `MemorySnapshot` | Immutable copy of the captured regions (the two `Sh2Dump` regions: LWRAM `0x00200000`, HWRAM `0x06000000`, 1 MiB each) plus both SH-2 register files and a `SnapshotKey`. Built on the UI thread with `ReadRegionBytes`, shared by both CPU views. 2 MiB, a few ms to copy. |
| `CaptureLoadImage : ghidra::LoadImage` | `loadFill` over the snapshot with bus folding (§6.1); `getReadonly` returns the literal-pool ranges; throws `DataUnavailError` for everything else. |
| `SaturnArchitecture : ghidra::SleighArchitecture` | Language `SuperH:BE:32:SH-2:default`, `readonlypropagate = true`, our loader, a `Scope` pre-seeded with `FunctionNames` and Saturn hardware-register symbols. One instance per snapshot (1 ms to build), destroyed with it. |
| `Sh2Decompiler` | The engine wrapper: `Decompile(snapshot, entry, options) → DecompileResult`. Two passes: follow flow, harvest literal pools and constant call targets, register callees, decompile, emit markup, resolve tokens to addresses. Catches every `LowlevelError`; never throws across the thread boundary. |
| `Sh2FunctionFinder` | Entry discovery and caching (§6.4). Pure functions over a snapshot; unit-testable without the engine. |
| `DecompilerRunner` | The worker thread and the request/result handshake, modelled on `MemorySearchRunner` (UI-thread capture, `std::thread`, `Poll()` installs results). Generation stamps as in `FrameRecorder`. |
| `DecompilerPanel` | Dockable ImGui window "SH-2 Pseudocode". Draws the last valid result, status, and raises `Request`s that `App` consumes, like `LogPanel::Request`. |
| `ghidra-decompiler` (third_party) | Pristine Ghidra sources + SuperH spec + `LICENSE`, `NOTICE`, `VENDOR.md`. Built as `SaturnExplorerGhidraDecomp` (static) and `se-sleighc` (host tool) only when `SE_ENABLE_DECOMPILER` is on. |

## 5. Interfaces

Sketches, not final signatures. Everything is C++14, no exceptions escape, no Ghidra type appears
outside `FrontEnd/src/Decompiler/*.cpp` (headers forward-declare an opaque `Engine`).

```cpp
// MemorySnapshot.h — built on the UI thread, read anywhere.
struct SnapshotKey { uint64_t sourceId; uint64_t deriveSerial; uint64_t frame; bool operator==(...) const; };
struct MemoryRegion { uint32_t base; std::vector<uint8_t> bytes; };
struct MemorySnapshot {
    SnapshotKey key;
    std::vector<MemoryRegion> regions;            // LWRAM, HWRAM; optional BIOS image (§6.2)
    bool haveRegs[2]; se_sh2_regs regs[2];        // master, slave
    static std::shared_ptr<const MemorySnapshot> Capture(IMemoryBackend&, se_context*, SnapshotKey);
    static uint32_t Canonical(uint32_t addr);     // bus fold, see §6.1
};

// Sh2Decompiler.h
struct DecompileRequest {
    uint32_t entry;                               // canonical function entry
    std::vector<std::pair<uint32_t,std::string>> names;   // FunctionNames snapshot (addr → name)
    std::vector<uint32_t> knownFunctions;         // extra entries to register (call stack, finder)
    bool registerCallees = true;                  // second pass with constant call targets registered
    uint32_t maxInstructions = 50000;             // Architecture "maxinstructions" option
};
enum class TokenKind : uint8_t { Plain, Keyword, Type, FuncName, Variable, Const, Comment, Global, Label };
struct Token  { uint16_t begin, end; TokenKind kind; uint32_t addr; uint32_t target; }; // addr/target 0 = none
struct Line   { std::string text; std::vector<Token> tokens; uint32_t firstAddr; };
struct DecompileResult {
    SnapshotKey key; uint64_t generation; uint32_t entry; std::string name;
    std::vector<Line> lines;                      // pseudocode
    std::vector<uint32_t> callees, literalPools;  // discovered
    uint32_t bodyBytes; bool noCode, truncated /*flowed into uncaptured memory*/, unimplemented;
    std::vector<std::string> warnings; double elapsedMs;
};
class Sh2Decompiler {                              // worker thread only
public:
    explicit Sh2Decompiler(const SpecPaths&);     // where the .ldefs/.sla live (§7.2)
    DecompileResult Decompile(const MemorySnapshot&, const DecompileRequest&);
    static bool Disassemble(const MemorySnapshot&, uint32_t addr, int count, std::vector<std::string>& out); // SLEIGH view, for tests/diagnostics
};

// Sh2FunctionFinder.h — no engine, testable with byte vectors.
struct EntryGuess { uint32_t entry; enum Source { User, Symbol, CallSite, RtsScan, Callee } source; int confidence; };
EntryGuess FindFunctionContaining(const MemorySnapshot&, uint32_t addr, const FunctionNames&, const CallStack* optional);
std::vector<uint32_t> CallTargetsFromListing(const MemorySnapshot&, uint32_t entry, uint32_t end); // bsr / jsr-via-literal

// DecompilerRunner.h — modelled on MemorySearchRunner.
class DecompilerRunner {
public:
    uint64_t Submit(std::shared_ptr<const MemorySnapshot>, DecompileRequest);  // returns generation; coalesces per panel
    bool Poll(DecompileResult& out);              // UI thread; true at most once per completed job
    void InvalidateSession();                     // epoch bump + cancel flag; never joins (PLAN.md A4)
    bool Busy() const;                            // ~DecompilerRunner cancels, joins, then SleighArchitecture::shutdown()
};

// DecompilerPanel.h
class DecompilerPanel {
public:
    struct Request { bool jumpAssembly; int cpu; uint32_t addr; bool jumpHex; uint32_t hexAddr;
                     bool rename; uint32_t renameAddr; std::string newName; };
    void Draw(se_context*, IMemoryBackend&, DecompilerRunner&, FunctionNames&, bool live, bool paused, Request&);
    void ShowFunctionContaining(int cpu, uint32_t addr);   // from AssemblyPanel context menu / call stack
    int  Cpu() const; void SetCpu(int);
};
```

App wiring (all existing idioms): `mPanels.decompiler` + `PanelList()` row
`{"decompiler", "SH-2 Pseudocode", &Panels::decompiler, "Debugger"}`; `BuildDefaultLayout` and
`AdoptNewPanels` dock it beside "SH-2 Assembly"; `BuildUI` draws it after the `ScopedContextSwap`;
`Request.jumpAssembly` → `mAssemblyPanel.GoTo(cpu, addr); mPanels.assembly = true;`. The reverse
direction is a new "Decompile here" item in the Assembly panel's context menu and a "Decompile"
button on Call Stack rows, both calling `ShowFunctionContaining`.

## 6. Semantics

### 6.1 Memory, mirrors, missing memory
- **Regions** are exactly `Sh2Dump`'s: LWRAM and HWRAM. BIOS, cartridge, SMPC/SCU/SCSP/VDP
  registers and on-chip space are absent, so `loadFill` throws for them and flow stops there. A
  function that reaches such an address is reported `truncated` and the panel says where
  ("flows into uncaptured memory at 0x000xxxxx (BIOS)").
- **Folding classifies first.** Bits 31–29 of an SH-2 address select a partition; only `000`
  (cached) and `001` (cache-through) are bus accesses. Associative purge (`010`), the cache
  address array (`011`), the cache data array (`110`), on-chip space (`111`) and the reserved
  partitions are rejected as "not memory", never treated as RAM mirrors. Within the bus map only
  documented aliases are folded: the HWRAM 1 MiB repeat across `0x06000000–0x07FFFFFF`; LWRAM is
  not assumed to repeat. The spike's `fold()` masked bits 29–31 unconditionally and is superseded
  by this rule (PLAN.md A2). `ContextBackend::Canonical`'s broader `& 0x07FFFFFF` is a panel
  convenience and is not reused; its discrepancy is a logged follow-up for `MemoryBackend.cpp`.
- **Entry addresses are canonicalised** before anything else, so caches, names and
  `FunctionNames` keys agree whichever mirror the user typed. Navigation hands the canonical
  address to `AssemblyPanel::GoTo`, which already accepts any mirror.
- **Literal pools** are discovered from raw p-code after `followFlow` (every `LOAD` through an
  instruction-local constant pointer) and marked read-only one word at a time. Only those words
  fold; globals in RAM stay symbolic (`iRam0600xxxx`), which is what a debugger wants.
- **Optional BIOS image.** A user-supplied BIOS file (Settings → Decompiler → BIOS image) is
  mapped at `0x00000000` as a read-only region of the snapshot. Calls into the BIOS then decompile
  and get `bios_` names. Not required for anything else.

### 6.2 Source kinds and staleness
`SourceId()` alone is not enough: it does not move on live frames (`se_begin_frame` recaptures in
place) and does not say whether the source is live, paused, a dump or a scrub frame. The panel
therefore keys everything on `SnapshotKey = {SourceId, se_derive_serial, se_frame_number}`, the
same triple `MemoryCompare`'s `CaptureGuard` uses, and reads the App flags for policy:

A result is valid only under the exact key it was produced with, whatever the source kind: a
Hex Editor write to a savestate bumps `se_derive_serial` without touching `SourceId`, so
"cache until SourceId changes" would leave pseudocode permanently outdated (PLAN.md A1). The key
also carries a hash of the region set, so adding or removing the optional BIOS image is a new
snapshot. Renames re-run the request (names are engine input); comments are a display overlay;
an entry correction replaces the request for that function.

| Source | When a snapshot is taken | Result lifetime |
|---|---|---|
| Static dump / savestate (`SourceType::Dump`) | On first request, and again whenever the key moves (an in-place edit bumps the derive serial) | Until the key moves; cached per entry under that key. |
| Live, **paused** or halted at a breakpoint/step | On request; re-taken when the key moves (write, state load, frame change) | Until resume. Follow-PC selects the function containing the halted CPU's PC. |
| Live, **running** | Only on explicit Refresh, or on the opt-in auto-refresh (debounced: at most every N frames and only when the followed PC leaves the current function) | Shown with a "frame N — stale" badge as soon as the key moves; never silently replaced by a result from a different key. |
| Scrubbed history | Each scrub frame is a new `SourceId` (`RefreshScrubContext` → `NoteSourceChanged`); snapshot per frame, LRU of a few frames keyed by the full key | Discarded when the scrub context is dropped. |

### 6.3 Off the UI thread, no stale results
- One worker thread owns the engine. The decompiler library has process-global state
  (`SleighArchitecture::translators`, capability registries), so no second engine thread.
- A request carries `{SnapshotKey, generation}`. `Poll()` installs a result only if its
  `generation` is the newest the panel issued **and** its key equals the key of the snapshot
  currently displayed; otherwise it is dropped. Requests queued behind a newer one for the same
  panel are coalesced away before they run.
- A source change, close or reconnect bumps the runner's session **epoch** and clears the queue;
  it never joins. Results and queued requests from an older epoch are dropped. The UI therefore
  never waits on obsolete work (PLAN.md A4).
- A fresh `SaturnArchitecture` is built per snapshot (1 ms), so function bodies, symbols and
  read-only ranges from an earlier memory image never leak into a later one.
- Cancellation: the bare library has none (`registerAction` is private and the action tree is
  synchronous), so a 21-line upstream patch adds a host-owned atomic flag checked per instruction
  in flow following, per jump-table round, before every action, and every 1024 ops in rule pools;
  it is demonstrated on the real engine in SPIKE_RESULTS.md "Cancellation spike". It is
  cooperative: construction, one action's body, printing and teardown finish first (measured
  floors in PLAN.md A4). A wall-clock budget per request uses the same flag.
  There is exactly one engine thread for the life of the process, because the SLEIGH translator
  is a process-global object that every new `Architecture` resets; no replacement worker is ever
  started, and exit cancels, joins, then calls `SleighArchitecture::shutdown()`. Any
  `LowlevelError` becomes a `warnings` entry; a `std::bad_alloc` or a second consecutive failure
  rebuilds the engine on that same thread (PLAN.md A4).
- Emscripten has no thread here and no Ghidra: `SE_ENABLE_DECOMPILER` is forced off and the panel
  is compiled out (`#if SE_ENABLE_DECOMPILER`), the same way `SE_ENABLE_LIVE` gates live-only code.

### 6.4 Function boundaries, delay slots, indirect calls
- **Entry discovery ladder** (`Sh2FunctionFinder`), highest confidence first: user-set entry →
  `FunctionNames`/imported symbols → call-site target from the call stack (`DecodeCallTarget`,
  extended to `jsr` through a literal) → backward scan from the address to the previous `rts` +
  delay slot, skipping `nop` padding and literal-pool words, validated by decoding the first few
  instructions with `Sh2Decode` → callee discovered while decompiling a caller. The panel shows the
  source ("entry guessed from rts scan") and offers "Set function start here". User corrections
  persist in `saturn_function_bounds.txt` next to `saturn_functions.txt`.
- **Function end** is Ghidra's: flow following from the entry, so interleaved literal pools and
  shared tails are handled by the engine, not by us.
- **Delay slots** are the SLEIGH spec's job (`delayslot(1)`); the spike shows `bf/s`, `bt/s`,
  `jsr`+slot and `rts`+slot decompiling correctly. A token from a delay-slot instruction maps to
  the slot's own address. **One construct the spec gets wrong:** a PC-relative load or `mova`
  executing in the delay slot of a taken branch sees `PC = branch target + 2`
  (`Sh2DecodeAfterBranch` models this), while the spec computes `inst_start + 4`. Policy
  (PLAN.md A3): Step 2 detects the construct from the function's instruction stream, refuses to
  fold that literal, marks the result `unsupported` with the address and shows a banner; Step 4
  patches the spec so static-target branches (`bra`, `bsr`, constant `jsr`/`jmp`) publish their
  target through a context register and the slot computes the right address, while conditional
  and register-target slots stay flagged. A synthetic fixture with different literals at the two
  candidate addresses pins both stages. Nothing in this area is shown as trustworthy on the
  strength of a matching disassembly listing alone.
- **Indirect calls.** `jsr @rN` through a literal pool resolves to a direct, named call (spike).
  Computed targets (function-pointer tables, `braf`/`bsrf`) stay `(*pfn)(...)`; Ghidra's jump-table
  recovery handles the common `switch` shapes. With a paused CPU, the panel can offer the register's
  live value as a hint ("r2 = 0x06031510 at the halt") without pretending it is static.
- **Both CPUs, one RAM.** The CPU combo selects which register file resolves PC/PR for Follow PC
  and hints; the snapshot, caches and names are shared. The spike's savestate shows why this must
  not assume anything about the slave: its PC pointed at unmapped memory and the engine reported
  "no code" instead of guessing.

## 7. Build and dependencies

### 7.1 Vendoring
`FrontEnd/third_party/ghidra-decompiler/` holds, unmodified except for the two patches in `patches/` (PLAN.md A3, A4):
- `cpp/`: the 84 sources + headers from the Makefile's CORE, DECCORE and SLEIGH groups plus
  `sleigh_arch`, `inject_sleigh`, `libdecomp`, and the three `sleigh` compiler files
  (`slgh_compile`, `slghparse`, `slghscan`; the generated parsers are checked in upstream, so no
  bison/flex at build time). Excluded: `ghidra_*` (the Java bridge), BFD, XML/raw loaders, console
  and tests.
- `processors/SuperH/`: `superh.sinc`, `sh-2.slaspec`, `superh.pspec`, `superh.cspec`, `superh.ldefs`.
- `LICENSE` (Apache-2.0), `NOTICE`, `VENDOR.md` (upstream commit, version `12.3`, file list, how
  to bump: copy the same groups from the new tree, rebuild, run the golden tests).

### 7.2 CMake
- `option(SE_ENABLE_DECOMPILER ON)`; forced `OFF` under `EMSCRIPTEN`.
- `SaturnExplorerGhidraDecomp` static library, C++14 (upstream needs C++11), warnings from
  upstream silenced locally (`-Wno-sign-compare` as Ghidra does; `/W3 /EHsc` on MSVC). Linked only
  into `SaturnExplorerFrontEnd` and the decompiler tests; never into Core or the drivers.
- `se-sleighc` host executable from the same library, run as a custom command to turn
  `sh-2.slaspec` into `sh-2.sla` at build time (the spike measured 13 KB, milliseconds).
- **Spec files at runtime.** The engine opens `.ldefs/.pspec/.cspec/.sla` by path. To keep the
  Windows build a single executable and the macOS bundle untouched at runtime without patching
  Ghidra, the five files are embedded as byte arrays (the fonts' mechanism) and materialised on
  first use into `ConfigDir()/decompiler/<content hash>/` via temp-directory-and-rename with a
  verified manifest; a mismatch repairs once, then the decompiler reports itself unavailable.
  The freshness test covers the `.slaspec` → checked-in `.sla` → embedded bytes chain, and the
  Step 1 smoke test loads through the materialised directory, so CI exercises what users run
  (PLAN.md A5).
- **zlib** is required when the decompiler is on (`.sla` files are compressed). It is vendored
  (`FrontEnd/third_party/zlib`, the way Ghidra's own build does with `LOCAL_ZLIB`) and used on
  both platforms, replacing the optional `find_package(ZLIB)`; the savestate driver's gzip path
  becomes unconditional on desktop as a result (PLAN.md A6).
- The FrontEnd source list appears three times in `CMakeLists.txt` (Win32, web, desktop) and once
  as a wildcard in `FrontEnd.vcxproj`; the new `Decompiler/*.cpp` files go in the two desktop
  lists and the test targets, not the web list.

## 8. Licensing and attribution

- Ghidra decompiler and the SuperH spec: **Apache License 2.0** (NSA; portions US Government work,
  per Ghidra's `NOTICE`). RomHackingTools is GPLv3; Apache-2.0 code may be incorporated into a
  GPLv3 work. Obligations: keep `LICENSE` and `NOTICE` in the vendored tree and in distributions,
  mark the modified files (the two patches carry prominent notices, as §4 of the license asks),
  and attribute. The About dialog
  (`App::DrawAboutModal`) gains a "Third-party" section listing Dear ImGui (MIT), IPA and Liberation
  fonts, zlib, and "Ghidra decompiler and SuperH SLEIGH specification — Apache-2.0, National
  Security Agency"; `DISTRIBUTION.md` lists the files that must ship.
- zlib: zlib license, attribution only.
- Capstone (BSD-3) is used **only** by the Python verification tools under `Docs/Decompiler/spike`;
  nothing of it ships.
- rz-ghidra/rizin (LGPL-3.0) are not used.

## 9. Implementation phases

Each phase ends with its tests green on Windows and macOS; the UI starts only in Phase 3.

| Phase | Deliverable | Gate |
|---|---|---|
| 0 ✅ | Feasibility spike, Capstone-verified fixtures, this design | Reviewed |
| 1 | Vendored tree + CMake (`SaturnExplorerGhidraDecomp`, `se-sleighc`, embedded specs with hashed materialisation, zlib), **MSVC and Apple clang builds**, the CI jobs of PLAN.md A7, `Sh2DecompilerEngineTests` through the materialised spec path | All A7 jobs green on Windows and macOS |
| 2 | `MemorySnapshot`, `CaptureLoadImage`, `SaturnArchitecture`, `Sh2Decompiler` (two-pass, literal pools, callees, markup → tokens), `Sh2FunctionFinder`, `DecompilerRunner`; headless tests incl. the `.yss` fixture test | Golden outputs for fixtures 1/1b/2a/2b; runner stale-drop and coalescing tests |
| 3 | `DecompilerPanel`, App wiring (PanelList, layouts, menus, Assembly/Call Stack entry points, settings), source-kind handling (§6.2) | `ImGuiHarness` interaction tests; manual pass on live Mednafen paused/running/scrub |
| 4 | Quality: `FunctionNames` round-trip (rename in either panel), Saturn hardware-register symbols (VDP1/VDP2/SCU/SMPC/SCSP maps as named globals), optional BIOS image, register hints at a halt, user comments carried into the C view | Fixture golden updates reviewed |
| 5 | Release: About attribution, `VENDOR.md`, `DISTRIBUTION.md`, `DECOMPILER_GUIDE.md`, demo script | Ship |

## 10. Test strategy

- **Engine unit tests** (`FrontEnd/tests/Sh2DecompilerTests.cpp`, no ImGui): fixture 1 bytes embedded
  as a C array (from `spike/tools/fixture1.py`), asserting the structure of the output
  (`func_0x06005000(param_1)`, the `+ 1`, the guard), the token addresses of the call and return
  lines, fixture 1b's opaque call to `0x600` with `truncated == false`, "no code" for an unmapped
  entry, identical text for a mirrored entry, and the `mov.w` literal fold.
- **Real-code fixture test** (skips itself when game data is absent, like `YssFixtureTests`):
  opens `GS-9169_007.yss` through the savestate driver, decompiles `0x06031598`, asserts 13 literal
  pools, the callee list, and compares the SLEIGH listing with the checked-in Capstone-verified
  listing (`spike/real_06031598_listing.txt`). Golden C output is version-pinned and reviewed on a
  Ghidra bump.
- **Finder tests**: rts-scan over hand-built byte vectors with literal pools and `nop` padding
  between functions; call-site extraction for `bsr` and `jsr`-via-literal.
- **Runner tests**: a result whose key or epoch is stale is never installed; two submissions
  coalesce to one run; a running request is cancelled from the test thread on the real engine
  and the next request completes; the wall-clock budget cancels an oversized function; the
  destructor joins (PLAN.md A4).
- **Panel tests** (`ImGuiHarness`): clicking a line raises `Request.jumpAssembly` with that line's
  first address; switching CPU keeps the result (same RAM) and changes only the Follow-PC function;
  whole-row selection goes through `RowSelectable`.
- **Build matrix**: Phase 1 is gated on real Windows and macOS builds, since that is the one
  thing the spike could not exercise.

## 11. Out of scope / later
User-defined structs and types, a type editor, cross-function data-flow (Ghidra's Java-side
analyzers), decompiling the 68000 sound CPU (a `68000` SLEIGH spec exists; same engine, later),
and SLEIGH changes beyond the one delay-slot context patch in PLAN.md A3.
