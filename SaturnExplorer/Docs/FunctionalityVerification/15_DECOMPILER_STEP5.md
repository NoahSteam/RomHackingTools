# SH-2 decompiler — Step 5 (Release) verification

**Gate status: MET.**
- CI run [38410604062](https://github.com/NoahSteam/RomHackingTools/actions/runs/38410604062) at `33326d2` passed all seven jobs, including the macOS bundle job's new `--selftest` launch without Homebrew SDL. Run 38405230972 at `9639d2a` had also passed all seven.
- The Windows column (at `9639d2a`) and macOS run 2 (at `33326d2`) are below.
- The only failures found were the bundle blockers and doc wording. All are fixed, and none were in decompiler code.

This record covers PLAN.md Step 5:
- the About dialog's "Third-party" section;
- every third-party licence text shipping with the binary;
- `Integration/DISTRIBUTION.md` "Shipping Saturn Explorer itself";
- `DECOMPILER_GUIDE.md`, the demo (`Docs/Decompiler/DEMO.md`) and the `CLAUDE.md` pointers;
- the `VENDOR.md` "Bumping Ghidra — checklist".

## Automated (CI)

| Check | Where |
|---|---|
| All seven licence texts beside the CMake exe | windows `build-release.bat` job |
| All seven beside the MSBuild exe, toolsets v143 and v142 | windows `build-release-msbuild.bat` matrix |
| All seven inside the installed `.app` and inside the DMG's `.app`, signature verified | macOS bundle job |
| Both bundled binaries start (`--selftest`) with Homebrew's SDL uninstalled | macOS bundle job |

The seven texts:
- `imgui/LICENSE.txt`
- `fonts/LICENSE-IPAfont.txt`
- `fonts/LICENSE-Liberation.txt`
- `zlib/LICENSE`
- `nlohmann-json/LICENSE.MIT`
- `ghidra-decompiler/LICENSE`
- `ghidra-decompiler/NOTICE`

## Manual

| # | Check | Windows | macOS |
|---|---|---|---|
| 1 | Help → About opens. Expand "Third-party": six rows (Dear ImGui, IPAGothic, Liberation Sans, zlib, JSON for Modern C++, Ghidra decompiler), with the NSA attribution sentence and the licence-folder line for this OS. The dialog fits on screen and Close works. | | |
| 2 | The folder named in row 1 exists next to the shipped app and holds the seven texts (Windows: `x64\Release\licenses`; macOS: `SaturnExplorer.app/Contents/Resources/licenses`). | | |
| 3 | `DECOMPILER_GUIDE.md`: every menu path and label it names exists in the app as written (Windows → Debugger → SH-2 Pseudocode, Decompile Here, Call Stack and Registers Decompile, Rename Callee 0x..., Set Function Start Here, the BIOS button → Choose BIOS Image..., Edit Comment). | | |
| 4 | `Docs/Decompiler/DEMO.md` run end to end on a real game: each step does what its right-hand column says. Note any step that does not, and why. | | |

## macOS run 1 (at `9639d2a`, Mac arm64, macOS 26)

The content rows (1 to 3) passed on a hand-patched copy of the bundle. The bundle the documented
recipe produces did **not start**, for three independent reasons:

| | Symptom | Cause | Fix |
|---|---|---|---|
| A | No identity given: `codesign --verify` "code has no resources but signature indicates they must be present"; launch killed (Code Signature Invalid) | `fixup_bundle` and the added Resources invalidate the linker's ad-hoc signature, and nothing re-signed it | The install step always signs, `SE_MACOS_SIGN_IDENTITY` defaulting to `-` |
| B | `-` identity: verifies, but dyld aborts: libSDL2 "not valid for use in process ... different Team IDs" | `--options runtime` turns on library validation; ad hoc has no Team ID | Hardened runtime only with a real identity |
| C | Re-signed without runtime: "Fatal error! Cannot continue! Failed loading SDL3 library." | Homebrew's sdl2 is sdl2-compat, which dlopens libSDL3; `fixup_bundle` never sees it | Detected at configure time; `libSDL3.dylib` bundled beside libSDL2 |

CI's `codesign --verify` passed on B, so A7 now also launches both bundled binaries with
`--selftest` after uninstalling Homebrew's SDL.

| Row | Result |
|---|---|
| 1 | Passed, on the patched copy. Six rows, the NSA sentence and "Licence texts: SaturnExplorer.app/Contents/Resources/licenses"; the dialog fits and Close works. |
| 2 | Passed. Exactly the seven texts, in both staged bundles. |
| 3 | Passed. Every label exists. The guide's "…" was changed to the app's ASCII "...", and BIOS is now described as a toolbar button. |
| 4 | Superseded by run 2. |

No Developer ID certificate was on that Mac, so notarisation was not run.

## macOS run 2 (at `33326d2`, the same Mac, the documented recipe as written, no identity)

The bundle:
- Configure reported "`/opt/homebrew/lib/libSDL2-2.0.0.dylib` is sdl2-compat; bundling `/opt/homebrew/Cellar/sdl3/3.4.14/lib/libSDL3.0.dylib`".
- `codesign --verify --deep --strict` gives "valid on disk / satisfies its Designated Requirement". The signature is ad hoc, `flags=0x2(adhoc)`, with no hardened runtime.
- `Contents/Frameworks` holds `libSDL2-2.0.0.dylib` and `libSDL3.dylib`, and the same seven licence texts are present.
- `--selftest` prints "selftest ok: SDL 2.32.70".
- `open` launches the app normally.

Row 4, DEMO.md on this bundle:
- Steps 1–5 and 10 used a `.yss` savestate.
- Steps 6–8 used live Mednafen.

| Step | Result |
|---|---|
| 1 | Passed. "dump / savestate · frame 0 · func_0x06030548 @ 0x06030548 (entry from rts scan, 60%)" |
| 2 | Passed. A line click shows its instruction in SH-2 Assembly. |
| 3 | Passed. `puVar7 = &VDP2_TVMD;` |
| 4 | Passed. Renaming a callee updates the line at once. After decompiling the callee, the caller's call shows its 4-argument signature. The name does not show in SH-2 Assembly's rows, because that panel has no label column; DEMO.md and the guide now say so. |
| 5 | Passed. `fill_block(...);  // demo note`, with no re-decompile. |
| 6 | Passed. The stale badge while running; Refresh; Auto-refresh followed the PC. |
| 7 | Passed. With a breakpoint on a dispatcher's `jsr @r2`: `(*pcRam060a9460)();  // r2 = 0x0600580C at the halt`, and the status line. DEMO.md now says to use a breakpoint, because "step until" is not reproducible. |
| 8 | Passed. With Follow PC on, the panel follows the slave's PC. DEMO.md's expected text is corrected: the output stays only with Follow PC off. |
| 9 | Partial. The BIOS popup has its three items. The OS file picker cannot be driven by the tester; the BIOS paths were covered through `settings.ini` and in the Step 3 record. |
| 10 | Passed. |

Observation, not a gate: after Reset Layout, the bottom dock is about 175 px tall on a 1470×956 logical
screen. That is about four lines, until the splitter is dragged. The split is the app-wide default
layout (`App::BuildDefaultLayout`, 24% bottom), which is not decompiler code, so it is left as a
follow-up.

## Windows run (at `9639d2a`, `build-release-msbuild.bat`)

The default build printed "Toolset: v143" and built. `SE_TOOLSET=v142` printed "Toolset: v142" and then
failed with MSB8020, because this PC has no VS 2019 build tools. CI's v142 job is the proof for VS 2019,
and it was green on `9639d2a`.

| Row | Result |
|---|---|
| 1 | Passed. Six rows, the NSA sentence, and "Licence texts: the "licenses" folder beside the executable"; the dialog fits and Close works. |
| 2 | Passed. `x64\Release\licenses` holds exactly the seven texts. A clean rebuild recreated them byte-identical to the sources. |
| 3 | Passed. Registers' Decompile is on the PC value cell in the SH-2 tab, not on the "PC" label; the guide now says so. |
| 4 | Passed except steps 4 and 8, both of which were wording. Steps 1–3, 5 and 7 passed, as did 9 (`bios_0x00000600()`, and back to `(*(code *)0x600)()` on Stop Using BIOS Image) and 10. Step 4: the rename and the recovered signature work. The name was absent from SH-2 Assembly, which has no label column, and from a Call Stack frame showing "Func unknown". Call Stack reads the same names, but only for frames whose function it has identified. DEMO.md and the guide now say both. Step 8: the same Follow PC wording as macOS, now corrected. |

Observations outside Step 5, left as follow-ups:
- the MSBuild build's About shows "Build: unknown", because no commit is stamped;
- with a savestate open, the live Connect reported "No compatible live emulator endpoint was found" until Close Source;
- the Rename dialog's text box does not take focus on open.

## Follow-ups fixed after the gate

The four follow-ups from the Windows and macOS runs, fixed after the gate was met:
- **Bottom dock.** Reset Layout gives the debugger strip at least 18 lines, capped at 45% of the window (`BottomDockFraction`, `TestBottomDockFraction`). A tall screen keeps 24%.
- **"Build: unknown" (MSBuild).** `FrontEnd.vcxproj` writes `BuildInfo.h` from git before compiling, as the CMake build does. CI greps the MSBuild exe for the commit.
- **Connect with a savestate open.** A user's Connect now replaces the open source. It was refused and reported as "No compatible live emulator endpoint". Auto-connect still never replaces a source. A source opened during the attempt is kept, with its own message (`LiveOpenMayAttach`, `TestLiveOpenMayAttach`, mutation-checked). Launch with a source open now says why it did not connect.
- **Rename dialog.** The name box takes focus when the dialog opens, with the old name selected.

### Follow-up round 2 (macOS at `fecdac6`)

What passed:
- Connect over an open savestate replaced it and went live. The pseudocode re-keyed, and an entry that was stale for the new state showed `halt_baddata()` rather than a stale function: A1 working as specified.
- The rename box takes focus with the old name selected.
- About shows "Build: fecdac62d0".

What was fixed:
- **Bottom dock.** It showed 12 output lines, not the intended number, because the first version counted the panel's tab bar, toolbar and status line as output. `BottomDockFraction` now asks for 16 output lines plus 5 lines of panel chrome, capped at half the window, and its test uses the Mac's measured line height.
- **Escape on the rename dialog.** It did not cancel, because the active text field consumes Escape. The dialog body is now `NameDialogBody` in `PanelWidgets.h`, which also treats Escape as Cancel. `TestNameDialogEnterAppliesTypedName` and `TestNameDialogEscapeCancels` cover it. Removing the Escape check, or the focus-on-open, fails them. A redundant AutoSelectAll flag, which no test could tell apart, was dropped: focus set from code already selects the text.

### Follow-up round 3 (macOS at `16a70c0`)

The rename dialog passed: Escape closes it without renaming, and Enter applies. The bottom strip showed
14 full output lines of the 16 targeted. The panel's chrome measured about 7 lines rather than 5, so
`BottomDockFraction` now allows 7, which gives about 0.46 of the window on this screen.

### Follow-up round 4 (macOS at `de8b136`): the result

The Mac still showed 14 lines, the same as before. Its measurement showed why: the strip already sits
on the 50% cap. The dock area is about 735 px in its screenshot space and the strip about 365 px, so
the chrome allowance changes nothing on that screen. Reaching 16 lines would take about 60% of the
window, which would leave the VDP and image views above it too short. The cap stays at half.
**Outcome:** Reset Layout on a 1470×956 screen gives 14 pseudocode lines and 12 to 13 assembly rows,
up from about 4. Taller screens get 16 or more.

### Review: inputs that outlive their snapshot (at `16a70c0`)

A review of every engine and finder input found five findings. Each one now has a test in
`DecompilerPanelTests` that fails without its fix.

1. **Learned signatures outlived the signatures they came from.** If B was decompiled before C, its
   return value came from C's r0. After C was found to be void, B's signature was obsolete, but A was
   still given it. Each learned signature now records the snapshot, the function set and the callee
   signatures it was given. When a signature changes, everything learned from it is dropped, all the
   way up the call chain. (`TestPrototypeProvenance`)
2. **Function boundaries were not part of the question.** The user's function starts and the named
   addresses decide whether a branch counts as a call. Their fingerprint is now part of each request
   and acceptance check, each cache key, and each learned signature. A correction now outdates its
   callers' cached results as well as its own. (`TestFunctionSetKeysQuestions`)
3. **Discovered callees were hints for any memory.** Each callee is now recorded with the snapshot
   that called it, and it is a hint only for that snapshot. (`TestCalleeHintsBelongToTheirSnapshot`)
4. **Moving from scrubbing to a running emulator kept the scrubbed snapshot.** When the source
   changes, the panel now captures again at once, even with auto-refresh off.
   (`TestRunningAfterScrubCaptures`)
5. **A new symbol or start did not change which function contained an address.** The panel now
   finds the containing function again when the function set, or the callees found for the held
   snapshot, change. (`TestHintsReResolve`)

The re-review at `4bd1c99` approved these and found one more gap: **call-stack hints**. They had no
snapshot of their own, so a stack read on a scrubbed frame could pick the function on another frame.
Rebuilding the stack also did not trigger a new resolution. App now records which memory each CPU's
stack was read from. It passes only stacks that match the held snapshot, and gives a revision that
changes whenever a stack is rebuilt or cleared. When the revision changes, the panel resolves again
before it accepts results. The stack is still read only while resolving, never every frame.
(`TestCallStackHints`)

CI for the follow-ups: run [38416564598](https://github.com/NoahSteam/RomHackingTools/actions/runs/38416564598) at `de8b136`
passed all seven jobs. That includes the MSBuild jobs, which check that the exe carries the commit,
and the bundle `--selftest` launch.

### Review: SH-2 semantics (at `6eaa133`)

This review checked the spec's semantics against the Renesas SH-1/SH-2 manual and found seven
errors. Their pseudocode read as ordinary C, with no unsupported or unimplemented warning. All seven
are fixed. Mednafen's SH-2 core (`ss/sh7095_ops.inc`) confirmed each expected behaviour.

1. **A mirrored entry was canonicalised before analysis.** A `mova` running at `26004000` yields a
   `0x26…` pointer. The engine now analyses the function at the address it was asked for, and keeps
   canonical names, prototypes, token addresses and reported locations. The panel requests the
   function in the mirror the PC or the typed address is in. The mirror is part of the cache key.
   (`TestMovaFollowsExecutionAddress`, `TestMirrorEntryIsKept`; two older panel tests that expected
   a canonical entry were updated.)
2. **`bsr`/`bsrf`/`jsr` wrote PR after the delay slot.**
3. **`MAC.L` produced a wrong carry.**
4. **`MAC.W` had wrong operand widths, sign handling and accumulation.**
5. **`XOR.B` performed an AND.**
6. **`mov.x Rm,@-Rn` stored the decremented value when Rm and Rn are the same register.**
7. **`DIV1` read an aliased divisor too early.**

Findings 2 to 7 are fixed by `patches/0006-sleigh-sh2-semantics.patch`. `sh-2.sla` and
`Sh2SpecData.h` were regenerated. The patch chain was checked: the upstream file with 0005 and
then 0006 applied gives the checked-in `superh.sinc`. The new `Sh2SemanticsTests` runs each
instruction on the vendored p-code emulator through the shipped translator and compares the result
with a Mednafen-derived reference model. On the old spec, every block fails, 17 checks in all. On
the new spec, all pass.

Golden review, as `VENDOR.md` requires: the output of `Sh2DecompilerTests` and the real-game
`Sh2DecompilerFixtureTests` was diffed before and after the change. Nothing changed except the new
mirror case.


The re-review at `37a0535` found four places where the execution mirror was still lost. All four are
now fixed, and each has a test that fails without its fix:

1. **Learned signatures were shared across mirrors.** The engine took a callee's signature by its
   canonical address and applied it to every alias. Signatures are now keyed by the address the
   function ran at, through the panel, the lending, the dependencies and the engine. The result
   reports `calledAt`, the addresses its calls go to, for the dependency tracking. Names stay
   canonical. (`TestPrototypesPerMirror`: a mova-dependent callee takes one argument canonically and
   none through `26004800`; learning the mirror no longer changes the canonical caller. Also
   `TestCalleePrototypes`: a signature lent for one mirror is not applied to a call through another.)
2. **Set Function Start Here dropped the mirror.** It records the start canonical and now asks for
   the new function in the mirror the shown one runs in. (`TestFunctionStartKeepsMirror`)
3. **The runner's too-large memo used the canonical entry.** It is now keyed by the address asked
   for, so a time-out at one mirror no longer blocks a first attempt at another. (`TestBudget`)
4. **The entry box did not update when switching mirrors.** It now updates whenever the address the
   shown function ran at changes. (`TestMirrorEntryIsKept`)

Behaviour change: because the canonical function and its mirror are now separate functions, a
canonical function's newly learned signature changes what the mirror's request lends. Switching back
to a mirror can therefore ask the engine again where it used to hit the cache.

The review's other answers:
- **MAC.L with S set:** keep Mednafen's behaviour. Mednafen's changelog records a saturation fix
  based on tests on a real Saturn.
- **MAC.W setting MACH bit 0:** matches Mednafen and the manual's prose. The manual's pseudocode
  marks it SH-1 only, so this is Mednafen parity, not independent hardware confirmation.

The re-review at `c426c2e` accepted those four fixes and found two more execution-address checks
missing in the panel. Both are now fixed:

1. **Auto-refresh did not follow the PC into a mirror.** The PC is still checked against the
   canonical map, but whether it has left the shown function is now judged against the address the
   function ran at. Moving from `06004012` to `26004012` therefore asks for the mirror.
   (`TestAutoRefreshFollowsMirror`)
2. **A halt hint could attach to output from another mirror.** A hint is now shown only when the
   halted PC is in the same mirror as the output. (`TestHaltHintMatchesMirror`: output pinned at
   `06004000` gets no hint for a halt at `2600400E`, and the `26004000` output does get one.)

The re-review at `a92a300` closed both findings and found one more. A call the output had already
resolved hid the halt hint whenever the canonical targets matched, so the CPU about to call
`26005000` was treated as the printed call to `06005000`. Each function-name token now also records
the exact address called (`Token::calledAt`). The hint is hidden only when that address equals the
runtime target, and is kept when there is no such token to compare. (`TestHaltHintMatchesMirror`)

At this point every input-invalidation and SH-2 semantics finding is closed. One hardware-validation
gap remains: MAC.W setting MACH bit 0 on overflow is confirmed only by Mednafen parity.

## Review of `8639b80`: prototype lending

The review found five ways a lent signature could be wrong. A lent signature is locked in the caller,
so a wrong one silently drops arguments or invents returns. The engine now lends a signature
(`DecompileResult::prototype.known`) only when the analysis that recovered it was complete;
otherwise `prototypeWithheld` says why. All five are pinned by `Sh2DecompilerTests`
(`TestPrototypeEligibility`), whose 13 checks fail on `8639b80`.

1. **Tentative recovery.** A wrapper forwarding unchanged r4 to a callee with no known signature
   was recovered as taking no arguments. Every call must now resolve to a callee whose signature was
   lent. A self-call, an unresolved indirect call, or a callee without a signature withholds it, so
   mutual recursion is never lent. The wrapper is lent once its callee is, with one argument, and its
   caller then prints `(0x2a)`.
2. **Stack arguments past unused registers.** `rts; mov.l @r15,r0` was lent as `(void)`. Any
   incoming varnode in a parameter location that no parameter covers now withholds it. The fifth and
   the sixth argument alone are withheld, and the five-argument control is still lent.
3. **Bad instructions and unrecovered jumps.** `hasBadData()` alone now withholds, not only when
   combined with uncaptured memory. An unrecovered jump becomes an unresolved indirect call and is
   withheld by (1).
4. **64-bit return order.** The cspec join had the halves swapped. Patch 0007 puts R0 as the high
   half. The callee now prints `0x100000002`, and its lent caller takes the high half from R0.
5. **Modified PR.** Ghidra's actions replace a RETURN's target with 0, so the check runs on our
   side: the function's p-code is interpreted per instruction, including internal branches. The
   domain is PR on entry, a stack offset, a constant, or unknown. Every return must use PR from entry,
   held in a register or saved and reloaded from the stack. `lds r4,pr; rts` is withheld, and so is
   PR saved in a `bsr` slot (the continuation). A normal `sts.l pr,@-r15` ... `lds.l @r15+,pr` is
   still lent. The check makes two assumptions, the same ones the engine's own stack analysis makes:
   stores through a non-stack pointer do not alias the stack, and a call leaves r8-r15 and gbr
   unchanged.

One panel test changed contract. `TestPrototypeProvenance` expected B, learned before its callee C,
to be lent and later dropped. It is now never lent, so A's output matches the bare one until C is
known.

Cost: a function that calls the BIOS without an image, or through a computed pointer, never lends
its signature.


The re-review at `0be0808` closed findings 1–4 and found three more gaps in the PR check (5).
`ReturnCheck` now tracks where each value can come from instead of discarding it, and every lent
signature carries `CallEffects` that a caller applies at the call:

1. **Precision loss.** A computed value keeps its origins, so `or #0,r0` on a stack address is still
   possibly a stack address. A store through it forgets every stack slot.
2. **Stack escaping to a callee.** `storesThrough` records where a function's store addresses come
   from. A caller that passes a stack address in a register its callee stores through forgets its
   slots.
3. **Unverified preservation.** `preserved` lists the registers equal to their entry value at every
   return. After a call, only those keep their value.

`TestPrototypeEligibility` covers each gap and its control; the three gap checks fail on `0be0808`.

The re-review at `b9faafe` found that a callee's writes were applied to the caller's state but never
recorded in the caller's own summary. They also skipped the caller-frame rule that a direct store
follows. A callee's mapped writes now go through the same `StoreThrough` as a direct store: they are
added to `storesThrough` and forget slots the same way, including slots at offsets ≥ 0 for an
entry-register pointer. A callee's `kFrame` is recorded too. (`TestPrototypeEligibility`, checks
(d) and (e): a wrapper forwarding r4 to `*param_1 = 0` exports the r4 bit, and its caller that
passes the stack is withheld while one that passes a constant is still lent. PR saved at entry
r15 + 0 is withheld across that leaf called through r4.)

Before going back to review, two `/code-review` passes on `da2c9d2` found more holes in the PR check.
All are fixed, and each new check below fails on `da2c9d2`:

- **(f) Inexact loads.** A load through an address that may be in the stack, but is not exact,
  can read any slot.
- **(g) Below r15.** A slot below r15 holds anything: a slot r15 rises past, a slot below a call's
  r15 (the callee's pushes), and any load from below r15 (interrupts).
- **(j) Part-word stores.** A store of part of a word merges into that word's slot. It used to be
  dropped.
- **(i) What a callee lets out.** `CallEffects` gains `leaks`: the values a function stores to
  memory it does not track. A caller maps them, so a stack address let out two calls down marks it
  escaped. The old "a stack address visible to the callee" rule is gone.
- **(k) Writes that stay in the stack.** `frameValues` holds the values written at or above entry
  r15, kept separate from `leaks`. A store that can only land in the stack is therefore not an escape.
  An index added to an exact stack address stays a stack address, as the engine's stack analysis
  also assumes.
- **(h) sleep.** CALLOTHER `Sleep_Standby` changes nothing. Any other user op takes the worst-case
  effects. It used to clobber everything.
- **Absolute addresses.** These are taken never to be in the stack (the engine's own assumption).
  This is now applied consistently: a store through one no longer forgets slots, though it still
  leaks the value.

The re-review at `987e023` closed both findings and found one more gap. A load through an inexact,
possibly-stack address returned only the tracked slots. That address cannot be shown to stay
above r15, so the load may read free stack. It now returns "anything", the same as an exact load
below r15. (`TestPrototypeEligibility` (l): the exact and inexact reads of the free-stack word are
both withheld; the inexact one is lent on `987e023`.) The cost is precision: a pointer loaded from a
local array, if stored through, now makes the function forget its saved PR.

The review accepts two assumptions as stated conditions of lending, not guarantees for arbitrary
SH-2 code:
- an index added to a stack address stays in the stack;
- an absolute address never points into the stack.

## Control-flow recovery and function boundaries (review at `b488688`)

The criterion: jump tables, indirect branches, shared epilogues, overlapping and multiple-entry
functions, embedded data and flow out of captured memory must produce warnings, not an
apparently complete function. Before this round the result carried only three conditions
(uncaptured memory, the delay-slot load, unimplemented instructions). Everything else the engine
noticed was visible only as the reason a signature was withheld, or as an inline comment.

- **The engine's flow warnings are part of the result.** `DecompileResult::flowWarnings` is read
  from the warning comments the engine keeps on the function: an unrecovered jump table treated as
  a call or a return, overlapping instructions, unreachable code removed, too many instructions.
  Each is also in `warnings`, and the panel shows them as amber lines (four, then "N more").
  Warnings from jump-table recovery's speculative copy of the function are left out.
- **Bad data in captured memory** (`badDataAt`) is reported where it is: flow halts there,
  "embedded data, or the entry is wrong". It is no longer folded into "truncated". Four or more
  such halts add "the entry is probably not code". This is the warning Ghidra's own default turns
  into a failure; `max_baddata` stays high so a function with many uncaptured branches is still
  shown.
- **Cuts are attributed.** The fetch image keeps every refused address, not only the first. A
  bad-instruction halt is a cut only when the image refused that fetch (or its delay slot's).
  `truncatedAt` is the lowest cut and `truncatedCount` how many there are, each one a warning.
- **Flow into another function's entry** is a warning naming it: a tail branch, a fall-through
  into the next function, shared code.
- **The body is what flow reached** (`body`, merged canonical ranges), not
  `[entry, entry + bodyBytes)`. The panel uses it for notes with no line of their own and for
  auto-refresh, so a shared epilogue below the entry is inside and the gap between is outside.
- **An address the entry never reaches** is warned about. The panel checks the PC (or the address
  asked about) against the body of the function found for it.
- **The finder.**
  - Flow from a known entry decides whether an `rts` after it is that function's end or an early
    return. The scan wins only when that flow provably never reaches the address. When the finder
    cannot follow it (a switch), the known entry is kept and the alternative stated.
  - A word that only reads as `rts` is skipped: one a PC-relative load reads, such as `.long 11`, or
    one whose delay slot could not be one.
  - A `mova`-addressed jump table after an `rts` is skipped, and its guessed end is stated.
  - When the code after the `rts` never reaches the address, the entry moves past that function's
    tail jump.
  - `EntryGuess::doubt` carries what the finder could not settle, and the confidence drops by 20.
- **ReturnCheck** follows the engine where it stops: a call the engine marks as not returning ends
  the path, and `jmp @rN` to the caller's PR is a return.
- **Patch 0008.** `superh.cspec` declares PR as the return address. Without it the engine read
  `sts pr,r1; jmp @r1` as an unrecovered jump table, `(*UNRECOVERED_JUMPTABLE)(); return;`, and
  withheld the signature.
- **A test race fixed.** `TestRegisterHintAtHalt` failed about one run in eight under parallel load,
  on the old code too: `WaitShown` could return before the panel had seen the new frame. It now waits
  for the panel to settle first.

Tests, each failing on `b488688` (checked with old-API variants of the same fixtures):
- `Sh2DecompilerTests` `TestFlowUncertainty`: bad data, an unresolved `jmp @r4`, a tail branch
  into a named function, a fall-through into a known entry, a shared epilogue's body (also through
  a mirror), `jmp` through PR lent, and one cut plus one bad word attributed separately.
  `TestManyUncapturedBranches` now requires the lowest cut and all five counted.
- `Sh2FunctionFinderTests` `TestBoundaries`: an early return in a named function, a switch the
  finder cannot follow, `.long 11`, a tail jump, a `mova` table.
- `DecompilerPanelTests` `TestFlowUncertaintyShown`: the amber lines for an entry that does not
  reach its address, an epilogue note listed, and auto-refresh with a PC in the epilogue and in the
  gap.

Not covered by a test: a call the engine marks as not returning. Nothing in this setup produces
one, since lent signatures carry no no-return flag.

Own review of `a5c6615`:
- **A distant known entry won whenever its flow could not be followed.** A symbol with a switch,
  several functions before the address, beat the scan's correct answer. That was a regression.
  Unfollowable flow now keeps the known entry only when the scan's `rts` is the only return in
  between.
- **A conditional tail call looked like an early return.** `bf F2; rts; nop; F2:` is the same
  shape. When flow from the known entry gets to the address only through the scan's entry, the
  known entry is kept and the doubt is stated.
- **A far jump within a function** (`jmp @rN` through a literal, past `bra`'s reach) was taken for
  a tail call. A literal target between the entry and the address is now followed.
- **The "not reached" advice** said "starts elsewhere" even when flow had stopped short, because of
  an unrecovered switch, a cut or data. When the result has flow warnings, it now says flow may stop
  short. It says nothing for a literal-pool word.
- **Auto-refresh with the PC parked** in a hole no answer holds re-captured every 30 frames. Each PC
  is now refreshed for once.
- **Not a gap:** a function past the instruction limit. The engine's default
  (`error_toomanyinstructions`) fails the whole decompile, so partial flow neither shows as complete
  nor lends a signature. `TestFlowUncertainty` pins this.

Each new check fails on `a5c6615`: `TestBoundaries` (doubt on the early return, the far
unfollowable symbol, the far jump) and `TestFlowUncertaintyShown` (the "stop short" wording, one
refresh for a parked PC).

Review of `7eaa935` (four findings, each reproduced by the reviewer):
- **Jump-table recovery outcomes were dropped** along with the scratch copy's speculative flow
  noise. A table the sanity check truncated (a zero destination) gave clean output and a lendable
  one-argument signature, though the dropped case reads r5. The scratch copy's warnings are now
  filtered by kind. Recovery outcomes are kept as `FlowWarning::JumpTable`: truncation,
  second-stage failure, bad or unlabelled cases, no normalized switch variable. They withhold the
  signature, as do a table still marked partial and "Exceeded maximum restarts".
- **A jump the engine takes for a thunk was silent.** `fail_thunk` (a table of one entry, such as
  `mov #0,r1; jmp @r1`) adds a call and a return without a warning. Every `jmp`/`braf` the flow
  reached that has no table and no engine warning is now `FlowWarning::TailJump`. The warning
  names the target when it is constant, and says when that target is not captured.
- **An early return could become a silent boundary.** With no known entry, the scan's entry after
  an early `rts` came back with no doubt. A branch within 4 KiB before the `rts` that jumps past it
  now raises a doubt. Any entry found only by the scan also says "guessed from the previous rts",
  without the confidence penalty, so a scanned boundary is never shown as settled.
- **The 256-refusal cap turned cuts into bad data.** A halt is now classified by asking the
  snapshot whether its address, or the delay slot decoded with it, is there. The fetch image's
  list is diagnostic only.

Tests, each failing on `7eaa935`:
- `TestFlowUncertainty`: the three-case switch (whole: two arguments, no warnings; truncated:
  warned and withheld), the thunk into the BIOS, and a 300-case switch with 299 targets uncaptured.
  That switch now gives 299 cuts and no bad data; the old code gave 256 cuts and 43 bad data.
- `TestBoundaries`: the early return without a symbol, and the plain-scan doubt.
- `TestFlowUncertaintyShown`: "Entry: guessed" for a scanned entry, none for a typed one.

Not tested in isolation: the restart limit alone (it fires alongside the truncated table above).

Own review of `0c3d989`:
- **Case-label warnings withheld working switches.** Only "Sanity check requires truncation" and
  "Second-stage recovery error" mean destinations were dropped. They come from the scratch copy;
  "cases may be missing" and the withheld signature are now for those two only. The label warnings
  ("may not be properly labeled", "Bad switch case", "Calculation of case label failed", "no
  normalized switch variable") come from the function proper after every destination is
  recovered. They are shown, not counted as missing cases.
- **A false tail call.** A jump the engine turned into a goto (a table of one destination,
  `RuleSwitchSingle`) or removed as dead was reported as a tail call. `TailJump` now requires the
  jump to be a call in the final p-code.
- **Literal-pool words that read as branches** no longer raise the early-return doubt.
- **Literals before flow.** Jump tables are recovered during flow, before a pass can mark its own
  literals read-only. So a switch bound or `jmp` target loaded from a literal pool did not fold:
  the switch was "Could not recover jumptable", and a far jump within a function became a call.
  The second pass now marks the first pass's literals before flow.
- **Callees come from the printed pass.** The first pass took such a jump for a call. Its target
  was then registered as a function, giving a false "flow runs into another function's entry" and
  a false callee.

Tests failing on `0c3d989` (`TestFlowUncertainty`): a `jmp` through a literal within the function
(now a goto, no tail-call warning, no callee), and a switch with a literal-pool bound (now
recovered, two-argument signature lent). A function whose whole body is `jmp` to a constant is
still a thunk to the engine and is reported as a tail call.

Re-review at `54bd571`: the four findings are closed. Two P2 follow-ups:
- **A resolved goto lost its signature.** `RuleSwitchSingle` removes a table whose destinations
  are all one address and makes the jump a plain goto. ReturnCheck looked the jump up in the final
  tables, found none, and withheld the signature for "an indirect jump it does not resolve". It
  now reads the destinations recovered during flow, merged with the final tables. Pinned: a `jmp`
  through a literal within the function, and a three-case switch whose cases and default share one
  destination. Both are now lent.
- **The body held an uncaptured delay slot.** For `rts` in LWRAM's last halfword, `BodyRanges`
  decoded the captured word and added its slot at 0x00300000, though the engine had halted the
  pair. Instructions flow halted at are now left out, and a slot is added only when it decodes.
  Pinned: neither the `rts` nor the slot is in the body.

Each check fails on `54bd571`. Coverage gaps the reviewer also lists: the restart limit alone,
failed multistage recovery, and a call the engine marks noreturn.

**Accepted at `6172fbf`.** The reviewer closed both P2s and reran the earlier counterexamples. It
accepted the control-flow criterion (uncertain recovery produces warnings, not an apparently
complete function) within the acknowledged limits:
- **Not established:** code/data distinction for data that decodes as valid code, and exclusive
  ownership of a shared epilogue.
- **Reviewed from source, not exercised by a regression test:** the restart limit alone, failed
  multistage recovery ("Second-stage recovery error"), and an engine-marked noreturn call. No
  silent-output reproduction was found in any of them.

The three reviewed-from-source paths now have tests (`Sh2DecompilerTests` `TestEngineEdgePaths`).
Each was checked by removing the handling it pins and watching it fail:
- **Restart limit alone, end to end.** No jump table is involved. A stack pointer is spilled and
  reloaded, and a global whose first store was dead is read through it. That takes two
  dead-code-delay restarts (stack, then ram), and the limit is one. The warning is kept and the
  signature is withheld for "restart limit".
- **A call that never returns, end to end.** `Sh2Decompiler::SetNoReturnForTests` marks a callee
  as never returning. Nothing in SE marks one: the spec's only user op has no output, so the
  engine's `fail_callother` mode cannot arise, and lent signatures carry no such flag. With the
  mark, the engine warns "Subroutine does not return" and flow stops at the call. The data after
  the call is neither reached nor in the body, and ReturnCheck ends the path there, so the caller is
  lent. Without the mark, the same caller flows into the data.
- **Failed second-stage recovery, on the engine's text.** No SH-2 fixture reaches it: both
  recovery stages analyse the same scratch copy of the function, which does not inherit the
  function's overrides, so the second sees what the first saw. The classification is factored into
  `ClassifyEngineWarning` and tested on the exact strings. "(jumptable) Second-stage recovery
  error" is kept and counted as missing cases. The scratch copy's other warnings are dropped. A
  case-label warning is kept without counting as missing cases, and the restart-limit text is
  recognised.

## Hardware-register semantics (review at `c4927c1`)

The criterion: register addresses, widths and aliases match the hardware documentation, and
naming a register must not let the optimiser remove a hardware access. The addresses of all 256
registers matched the Sega manuals. Volatility did not hold.

- **Registers were ordinary globals.** The engine turns a constant-address load or store into a
  global variable in SSA form, so it forwarded writes to reads, removed overwritten writes and dead
  reads, and folded polls. Each of these was reproduced:
  - `SMPC_SF = 1; while (SMPC_SF & 1);` printed as `do {} while (true);`.
  - Reading `SCU_IST` back after writing `param_1` to it printed as `return param_1;`.
  - The first of two `VDP2_TVMD` writes was missing.
  - A dummy read of `SCU_IST` was removed.
  - Two reads of `VDP2_VCNT` subtracted printed as `return 0;`.
  - The SH-2 divider's quotient printed as the dividend.

  Each register symbol is now `Varnode::volatil`. A symbol's own flags override a property range
  (`Scope::queryProperties`), so a range alone does not protect named registers.
- **Register areas are volatile ranges, every alias included.** This covers accesses wider than
  one register (no symbol contains them), unnamed registers (SCSP slots, the FIFOs) and mirrors.
  - The bus areas are SMPC, `MINIT`/`SINIT`, A-bus CS2 (the CD block), SCSP, VDP1, VDP2 and SCU.
    Each is covered in the cached, cache-through and 0xA partitions, and at the three 128 MiB
    repeats that bits 28-27 produce, since those bits are not decoded.
  - The associative-purge and cache-address-array partitions and the on-chip partition are
    covered whole.
  - Work RAM, VRAM, CRAM and the cache used as RAM are not covered.
- **The map is complete enough for the semantics.**
  - **SH-2 on-chip registers:** SCI, FRT, INTC, DMAC, WDT, SBYCR/CCR, DIVU, UBC and BSC, each at
    its own width (Hitachi SH7604 manual; Mednafen `sh7095.inc`).
  - **CD block:** `CD_HIRQ`, `CD_HIRQMSK`, `CD_CR1`-`CD_CR4` and `CD_DATATRNS`.
  - **`MINIT`/`SINIT`.**
  - **Access kinds:** every register is read/write, read-only or write-only.
    - VDP1: the manual's table.
    - VDP2: only the first eight words read back; the manual, register by register.
    - SMPC and SCU: what Mednafen returns on a read.
- **Aliases resolve.** `HardwareAlias` and `FindHardwareRegister` decode each chip's mirror:
  - SMPC every 128 bytes, SCSP every 4 KiB, VDP2 every 512 bytes, SCU every 256 bytes;
  - the CD block's 64-byte window;
  - bits 28-27 and the 0xA partition;
  - the SH-2's partial on-chip decode.

  The lookup now has a production caller: the notes.
- **Notes for what a name does not explain.** `DecompileResult::hardwareNotes` lists each of
  these, also in `warnings` and as amber lines:
  - a read of a write-only register;
  - a write to a read-only one;
  - one access covering two registers (the engine shows `_VDP2_SCXIN0` for a 32-bit scroll write);
  - a register reached through a mirror (shown by address).
- **The header's invariant was false.** "Nothing here folds: reads stay reads" described only the
  load image. It now says why both the symbol flag and the ranges are needed.

Not done: write-to-clear and trigger registers have no flag of their own. Every access is kept
whatever the register does, so a flag would change nothing in the output. Mirrors are not named;
they are protected and noted.

Tests:
- `Sh2DecompilerTests` `TestHardwareVolatility` covers the poll, the read-back, the double write,
  the dummy read, the two counter reads and the divider. It also covers the 32-bit scroll write
  (both writes kept, the pair noted), a poll through an SMPC mirror (kept, noted), a write-only
  read-back and a read-only write. `TestHardwareMap` covers every alias rule, the access kinds,
  widths and ranges.
- `DecompilerPanelTests` `TestFlowUncertaintyShown`: a write-only read-back is an amber line.
- Mutations, each rebuilt and run:
  - Without the symbol flag, 11 checks fail (the poll, the read-back, the double write, the dummy
    read, the divider, the write-only note).
  - Without the ranges, 3 fail (the wide write, the mirrored poll and its note).
  - Without both, 14 fail.

### Review of `20bfde7`: hardware-register semantics, round 2

- **P1: an unused read disappeared before its address became constant.** The engine keeps an
  unused load only when it can already see its pointer is constant (`lastChanceLoad`,
  `isEventualConstant(3,1)`), and drops that hold once heritage moves on. So these were removed as
  dead before their address could be seen to be volatile:
  - `mov r4,r2; xor r2,r2; add r2,r1; mov.b @r1,r3` with r1 = `SMPC_SF`;
  - `add r4,r1; mov.b @r1,r2`.

  Patch 0009 adds `Architecture::keepUnusedLoads`, which SaturnExplorer sets. It is a stated
  policy: a compiler drops an unused load unless it is volatile, so one that remains is there for
  its effect. Every unused load is held until its pointer is a constant; then the address's
  volatility decides. A load whose pointer never resolves stays. Outputs:
  - the first example prints `xVar1 = SMPC_SF;`;
  - the second prints `uVar1 = (&SMPC_SF)[param_1];`;
  - the same late-folding read of work RAM is still dropped.
- **SCSP access kinds**, per Mednafen `scsp.inc`, which reads 0 from every write-only field:
  - Write-only: MVOL, RBL/RBP, MOBUF, DMEAL, DMEAH/DRGA, TIMA-C, SCILV0-2 and MCIEB. The manual
    pairs MCIEB with the readable SCIEB; the emulator decides.
  - Read-only: MIBUF.
- **Registers whose reads and writes reach different fields** now carry a description (`split`):
  - SCSP: MSLC/CA, DTLG, SCIPD, MCIPD;
  - SCU: IST, PPAF;
  - CD block: HIRQ, CR1-CR4;
  - SMPC: PDR1-2.

  When a function both writes and reads one, the read gets an explanation.
- **The watchdog's write ports.** WTCSR, WTCNT and RSTCSR are named where they are read (a byte
  each at FE80/FE81/FE83); a byte write there is flagged as doing nothing. A word write to FE80
  or FE82 is described by its key (`KeyedWrite`):
  - FE80 with key 0x5A: "SH2_WTCNT = …";
  - FE80 with key 0xA5: "SH2_WTCSR = …";
  - FE82 with 0xA500: clears WOVF;
  - FE82 with 0x5A: sets RSTE/RSTS.

  The value is not always a constant: then the note says what each key does. A wrong key is a
  caution. The pair caution no longer fires for these writes.
- **The divider's second copy**, at `0xFFFFFF20`-`0xFFFFFF3F`, resolves to the same registers.
- **The 0xA partition.** SaturnMemory.txt says it is the cache-through partition again. Mednafen,
  which SE runs, sends it to the associative purge, as it does 0x4. SE follows the backend: 0xA
  addresses are not register aliases, and the whole partition is volatile.
- **Shared memory.**
  - Sound RAM, shared with the 68000, is volatile by default.
  - Other shared memory is the user's to list: `DecompileRequest::volatileMemory`, the panel's
    **Volatile** popup, `settings.ini [decompiler] volatile`. The list is part of the panel's
    question, so changing it asks again.
  - Work RAM is not volatile by default, since most of it is ordinary memory. A poll of unmarked
    shared work RAM still folds, with the engine's "Do nothing block with infinite loop" warning.
- **Cautions and explanations.** `HardwareNote::caution` separates the two:
  - Cautions stay amber and in `warnings`: a read of a write-only register, a write to a read-only
    one, a pair the manuals do not document, a wrong watchdog key.
  - Explanations go on the line as a trailing comment, like a user's note: a documented pair
    (VDP2 scroll, zoom, VCSTA and LSTA; UBC halves), a keyed write, a mirror, a split register.

Tests (all in `TestHardwareVolatility`, `TestHardwareMap` and the panel's
`TestFlowUncertaintyShown`): the two P1 reads and the work-RAM control, MVOL read and MIBUF write,
four keyed watchdog writes and a non-constant one, the divider mirror, an 0xA read, an
undocumented and two documented pairs, a sound-RAM mailbox, and a work-RAM mailbox unmarked,
marked through either partition. The panel tests check that an explanation is on its line and not
amber, and that a changed volatile list asks again. Mutations, each rebuilt and run, and each
failing its tests:
- 0009's flag off;
- keyed writes off;
- no divider mirror;
- 0xA decoded as cache-through;
- no sound-RAM range;
- no documented pairs;
- the user's ranges ignored;
- the old MVOL/MIBUF kinds.

Own review of `88295f7` found no engine problems. The 0009 flag was checked with and without
across action loops, repeated volatile reads, and a mailbox poll. Three small panel fixes:
- A result asked for under an older volatile list is no longer installed for a frame.
- A list longer than the popup's 2047-character box is refused instead of cut mid-address.
- `a - b` with spaces around the dash parses as one range.

### Review of `960051e`: hardware-register semantics, round 3

- **P1: a literal marked volatile was still folded.** Literal-pool words were marked read-only,
  and the engine's read-only fill wins over volatile (`ActionVarnodeProps`). Read-only marking
  now skips any literal in a user volatile range (every alias) or a hardware range, on both
  passes and for the literals carried between them. `mov.l @(4,pc),r0` over a marked literal
  prints `xVar1 = Ram06009748; return xVar1;`.
- **P1: learned signatures outlived the volatile list.** A learned signature now records the
  volatile list with the function set (`LendingId`). A change makes every learned signature
  unlendable, and through `deps`, every one recovered from them. The reviewer's callee, whose
  argument folds away while the mailbox is ordinary memory, is no longer lent as `(void)` once the
  mailbox is marked. Recovered again, it takes `param_1`.
- **P1: user ranges missed aliases and partial overlaps.** `VolatileAliases` expands a range to
  every address that reaches it: both partitions, bits 28-27, and high work RAM's 1 MiB repeat.
  It also widens each range to whole longwords. The engine tests volatility at an access's first
  byte, and SH-2 accesses are aligned, so every access touching the range then starts in it. The
  cost is the other bytes of those longwords. Mailboxes through `0x26101000`, `0x26801000` and
  `0x2E001000`, and a word access with only its second byte marked, are kept.
- **Watchdog wording.**
  - Keyed writes now state their conditions: WTCNT is "requested" (Mednafen loads it only while
    TME is set); WTCSR's OVF and RSTCSR's WOVF are cleared only if a read saw them set.
  - The read bytes are a new kind, `KeyedWrite`. A byte write to one is "takes no 1-byte write: it
    is written by a keyed word at 0xFFFFFE80", not "read-only".
  - The second copy at FE88-FE8B resolves, ports included.
- **Divider shadows.** FF18/FF1C (and FF38/FF3C) are named `SH2_DVDNTH_SHADOW` and
  `SH2_DVDNTL_SHADOW`, recorded as Mednafen's, not the SH7604's.
- **LCTA and BKTA** are documented 32-bit registers; a 32-bit access is an explanation now.
- **Read effects.** `onRead` explains what a read does besides return a value, on every read
  whether or not the function writes the register:
  - flags cleared: SCU PPAF;
  - FIFOs popped: SCSP MIBUF, CD_DATATRNS;
  - the DSP address advanced: SCU PDD;
  - a response marked read: CD CR4;
  - FRT latches: FRCH, FICRH;
  - "write 0 after reading 1" armed: FTCSR, CHCR0/1, DMAOR, RTCSR.
- **Shown staleness.** The shown answer records the volatile list it was asked under. A change
  makes it stale at once, pending request or not.
- **Partition labels.** `ClassifyPartition` follows Mednafen: 100 is the cache data array and 101
  the associative purge. "Reserved" is gone. Neither is a bus access, so code and data are still
  never read there.

Tests:
- `Sh2DecompilerTests`:
  - the three mailbox aliases, the one-byte overlap;
  - the marked literal with its unmarked control;
  - the watchdog second copy and byte write;
  - LCTA, a PPAF read;
  - the shadow and watchdog names;
  - LCTA/BKTA pairs, keyed writes through FE88/FE8A.
- `DecompilerPanelTests`: the reviewer's callee and caller, which also checks the shown answer is
  stale right after the change.
- `MemorySnapshotTests`: the relabelled partitions.

Mutations, each rebuilt and run, and each failing its tests:
- lending ignoring the list;
- staleness ignoring the list;
- literals always read-only;
- no longword widening;
- no bus aliases;
- no watchdog copy;
- no read effects.

### Review of `80620fb`: hardware-register semantics, round 4

- **P1: `VolatileAliases` lost coverage for large or boundary-crossing ranges.** It masked the
  endpoints before looking at the length, did not expand the high work RAM part of a range that
  started below it, and did not expand the pieces of a range that wrapped the bus period. It now
  works on physical (27-bit) intervals:
  - a bus part spanning a whole 128 MiB period covers the whole bus;
  - otherwise the interval is split where it wraps;
  - each physical interval's high work RAM part is expanded through every 1 MiB repeat;
  - every piece is placed in both partitions and all four periods;
  - anything above the bus partitions is kept as given.

  The reviewer's three ranges now keep their mailboxes. A range covering all of high work RAM
  also covers the function's own literal pool, so even the pointer becomes a read; the write and
  the poll through it are kept.
- **P1: jump-table recovery froze volatile tables.** Recovery reads its table through the data
  image, not through the read-only rule. The data image (`CaptureLoadImage::SetVolatile`) now
  refuses every hardware range and every alias of the user's volatile memory; instruction fetch
  keeps its own image. The literal-bound switch with its table marked volatile prints
  `(**(code **)(param_1 * 4 + 0x6050218))()` with "Could not emulate address calculation" and
  "Treating indirect jump as call", and its signature is withheld.
- **The divider shadows** are read/write, as Mednafen writes them. Every access explains they are
  Mednafen's, not in the SH7604's register table (`HardwareRegister::about`).
- **Wording.**
  - The unknown-value watchdog explanations carry the same conditions as the constant ones.
  - PDD says a read returns data and advances the address only while the DSP is stopped; while
    it runs, the read returns 0xFFFFFFFF.

Tests:
- the three wide ranges, end to end and in `TestHardwareMap`, with a non-bus range and one
  crossing out of the bus;
- the volatile jump table;
- a write and read-back of `SH2_DVDNTH_SHADOW`;
- PDD;
- the unknown-value watchdog texts.

Mutations, each failing its tests:
- the old `VolatileAliases`;
- no data-image refusal;
- shadows read-only.


### Review of `aa3f7d1`: hardware-register semantics, round 5

- **P1: a sample read through a volatile pointer was printed twice.** The engine prints a cheap
  expression with several uses at each use, and `*p` is one term, so the poll's sample appeared
  in the loop condition and again in `return (int4)*piVar1;`. Both mapped to the one instruction
  that reads it. Patch 0010 (`Architecture::loadsNotDuplicated`, set by SaturnExplorer) makes an
  expression containing a load explicit when it has several uses. The fixture now prints
  `uVar2 = (uint4)*piVar1;` in the loop and `return uVar2;`. The change is in analysis, not in the
  printed text. It applies to every load through a pointer (a constant-address access is a
  volatile op or ordinary memory instead), so ordinary code reading `*p` twice also shows one
  read into a variable.
- **Inverted ranges.** `VolatileAliases` returns nothing for `last < first`, checked before the
  longword widening. The decompiler ignores such a request entry and says so in `warnings`. The
  panel's parser already refuses them.

Tests: the reviewer's fixture (exactly one read, the sample returned), the whole-period range now
held to one read as well, inverted ranges in the map and end to end. Mutation: 0010's flag off
fails all three read-count checks.

### Consolidated hardware-semantics verification (after `d0ce10a`)

One pass over the whole pipeline, rather than the changed lines, with two new instruments:
- **The access ledger** (`DecompileResult::accesses`) lists every memory access the final p-code
  keeps: kind, address or pointer, width, instruction, whether it is in a loop, whether it is the
  returned value, and how often the printer emits it. The emission count is structural: an
  explicit value prints once, and an implied one once per use. Tokens alone cannot say, since a
  statement can wrap and `p[2]` prints a load through another op's tokens.
- **A reference model** for the volatile-range decode.

The pass found two more defects, both fixed here.

| Area | Coverage | Result |
|---|---|---|
| Access preservation | Every hardware fixture has a ledger written from its SH-2 instructions: reads, unused reads, repeated reads, overwritten writes, polls, read-modify-write (direct and through a pointer), reused results, keyed writes. Count, width, order, loop and returned value are checked, and each must print exactly once. | Pass. Turning patch 0010 off fails 2 ledgers, 0009 off fails 3, and symbols without the volatile flag fail 17. |
| Pointer resolution | Direct addresses, literals, late-folding arithmetic, indexed pointers, argument pointers (two reads, unused read, write then read, read-modify-write), pointers loaded from volatile memory, pointers that never resolve. | Pass. |
| Range expansion | `TestVolatileRangesAgainstModel`: 192,000 seeded addresses, biased to every region, partition and period boundary and to aliases, over small, medium, multi-period and inverted ranges, against an independent model (an address is covered when some byte of the range reaches the same physical longword). | 0 disagreements; the previous `VolatileAliases` gives 18,218. |
| Analysis data reads | Literal folding and jump tables refuse volatile bytes (earlier rounds). A string-pointer guard was added. | String and pointer inference are not reachable in SE's setup (no `char *` types), so the guard has no positive control. Unverified. |
| Register-map audit | `Docs/Decompiler/HARDWARE_REGISTER_AUDIT.md`, `tools/audit_hardware_map.py`: all 337 entries against Mednafen's handlers at their width. Mirrors, keyed writes, split fields and read effects by hand. | 22 differences, all recorded: SCU_AIACK (Mednafen reads 0), and the SCI and UBC, which Mednafen does not emulate. The script reports two planted errors. |
| Diagnostics | Reads and writes tested separately: no read effect claimed for a write, no write caution for a read, a split register written alone says nothing. Each note sits on its access's instruction. | Pass. |
| State and caching | Volatile list changed while a request is pending, hidden-panel staleness, a cache hit on switching back, signatures not lent across lists. | Pass; the panel suite passed 5 runs in a row. Transitive invalidation and reconnects rely on the existing mechanism and tests; nothing volatile-specific was added for them. |
| Engine stability | ASan + UBSan builds of `Sh2DecompilerTests`, `DecompilerPanelTests`, `DecompilerRunnerTests`, `Sh2DecompilerEngineTests` and `Sh2DecompilerFixtureTests`, the game code included. | No ASan error or leak. UBSan reports 2 signed shifts in upstream SLEIGH (`address.hh:605`, `slghpatexpress.cc:979`), which predate this work and are not blocking. MSVC and macOS were not run. |
| Representative code | `TestGameCode` (opt-in, `SE_GAME_HWRAM`): all 1,557 prologues in Sakura Taisen 2's high work RAM, from two save states. Every access must print once, no instruction's access may repeat on one path, and every access must be as wide as the instruction's operand. Compared with `c4927c1` (before this work) on the same code. | 1,557/1,557 decompile, 18,856 accesses, 0 violations. Time is 18.4 s before and 19.8-20.8 s after (about +8-13%); the worst function is 3.2-3.6 s either way. 292 outputs differ, and every difference was examined: |

The 292 differing outputs:
- 236 reads now read once into a variable.
- 27 registers are now named.
- 11 raw hardware addresses are now named.
- The other 30 include a `VDP2_BGON`/`VDP2_WCTLA` pair now in machine order (the old output had
  them swapped).
- In two functions, the old output printed a register read more often than the code reads it.

Defects found and fixed:
- **A read inside an index was printed twice.** In `p[*p].x -= 1` (game code 0603FB3E), the
  byte read `*p` appeared at both the load and the store. `processMultiplier` does not look into
  a PTRADD's index, so patch 0010 now searches the whole implied tree (`containsLoad`). Fixture:
  "indexed read-modify-write".
- **Cleanup rules changed accesses.** A word store through a byte pointer became two byte
  stores (`param_1[0] = 0; param_1[1] = 0;`); in game code a longword store became four byte
  stores. `RuleSplitLoad`/`RuleSplitStore` did this. `RuleDoubleLoad`/`RuleDoubleStore` merge two
  accesses into one, `RuleStringStore` collapses a run of stores, and `RuleExpandLoad` widens a
  load. All six are disabled on the engine's action (`disableRule`, no patch). If one is missing
  after an engine update, the result's warnings say so. Fixture: "word store through a byte
  pointer".

Not covered: whether every memory instruction in real code has an access in the output. Reads of
ordinary memory are dropped by design when unused, so that needs per-instruction pointer
knowledge; the fixtures cover it instead.

### Review of `d0ce10a`: order of reads through pointers

- **P1: single-use loads moved past other operations.** Patch 0010 kept a load from being
  duplicated, not from being moved. A load used once prints where its value is finally used, and
  the engine checked only stores that may alias it, and calls, in between. Two results:
  - A read through `param_1` before an SMPC command printed as
    `SMPC_COMREG = param_2; return *param_1;`.
  - Two FIFO reads through a volatile pointer printed as `*piVar1 - *piVar1`, in an order C
    leaves open.

  Patch 0010 now also makes `checkImpliedCover` keep a load explicit when any other memory
  operation lies inside the live range of the implied chain the load starts: a store, another
  load, a user op (volatile reads and writes), or a call. The first case now prints
  `xVar1 = *param_1; SMPC_COMREG = param_2; return xVar1;`, and the second
  `iVar2 = *piVar1; return *piVar1 - iVar2;`. A load with nothing in between still prints
  inline. The rule applies to every load, since every remaining LOAD may reach volatile memory.
- **The ledger now checks order.**
  - In the straight-line fixtures, accesses must be printed in the order of their instructions.
  - No two reads may share a statement unless one feeds the other (a pointer and the read through
    it). `MemoryAccess` gains `statement`, `through`, the block and the order.
  - "Two reads through an argument" (`*p - *p`) had the same defect; it passed before only
    because the ledger did not check order.
- **Game code.** `TestGameCode` also finds every read printed later than it happens, and checks
  that no other access lies between, within the block; and every statement with two independent
  reads.
  - With the rule: 0 and 0 in both save states.
  - Without it: 1,656 reads printed past another access, and 1,283 such statements.
  - Time and failures are unchanged: 1,557/1,557 decompile, in 19-21 s.
- **Sanitizers.** ASan and UBSan on the decompiler and panel suites, game code included: no
  errors beyond the two upstream SLEIGH shift sites.

Tests: "read through a pointer, then a command", "two FIFO reads through a volatile pointer",
and the order check in every ledger. Mutation: with the new rule off, 3 ledgers fail (including
the argument `*p - *p`) and the game-code order checks report 1,656 and 1,283.

### Review of `47ae113`: reads that always happen, printed as conditional

- **P1: a read could print in the right operand of `&&` or `||`.** `tst r5,r5; movt r1;
  mov.l @r4,r0; tst r0,r0; movt r0; and r0,r1` printed `return param_2 == 0 && *param_1 == 0;`.
  C evaluates the right operand only when the left one does not decide, so the FIFO read the
  code always makes appeared skipped when `param_2 != 0`; `or` gave the same with `||`. Patch
  0010 now also keeps a load explicit when its implied chain enters the right operand of a
  BOOL_AND or BOOL_OR:
  - the `&&` case prints `iVar1 = *param_1; return param_2 == 0 && iVar1 == 0;`;
  - two reads combined print as two statements, in order;
  - a byte read prints the same way.

  A genuine branch is the control: the code reads only when `param_2 == 0`, and the output
  keeps the read under `if (param_2 == 0)`.
- **The ledger rejects it.** `MemoryAccess::conditionalInPrint` marks a read printed in the
  right operand of `&&` or `||`. The ledger requires it false, and `TestGameCode` counts it:
  0 in both save states. That pattern does not occur in this game's code, so the fixtures carry
  it. With the rule off, 4 fixtures fail.
- **Patch 0011.** `RulePtrsubCharConstant` cast to `TypePointer *` before checking the metatype;
  the cast now follows the check. The UBSan vptr check reported it. `Sh2DecompilerTests`
  (game code included) under ASan and UBSan with vptr now reports nothing beyond the two SLEIGH
  signed-shift sites, which remain upstream, recorded and not blocking.
- **The earlier review of `f212a8d`** reported the two ordering failures that `47ae113` already
  fixes.


### Review of `5670323`: a load's place in the output

The external review of `5670323` listed areas rather than findings, so this round is an
adversarial pass of our own over the engine paths that print or move a load, with a fixture for
each shape. Two more shapes were found, both in the class the earlier rounds describe: a read
that is printed somewhere other than where the code makes it. Both are fixed by patch 0012, under
the `loadsNotDuplicated` policy, and the ledger now rejects both classes rather than the shapes.

- **P1: for-loop recovery moved a read past another read.** `BlockWhileDo::finalTransform`
  moves the statement that steps the loop variable, and the one that initialises it, to the end
  of their blocks so that they can print in the `for` header. `PcodeOp::isMoveable` already
  refused to move a load past a store, a user op or a call, but let it cross another load. For
  `body: mov.l @r4,r4; mov.l @r5,r1` (p = *p, then x = *q) the output was
  `for (; p != 0; p = *p) { x = *q; }`: C runs the step after the body, so the two reads are in
  the other order. The initialiser move did the same to a read before the loop. Through two FIFO
  or mailbox pointers that is a different program. A load now cannot cross a load; such a loop
  prints as `while` with both reads in place, and a loop whose step is already its body's last
  statement still prints as a `for`.
- **P1: a read printed inside one side of a branch.** An implied expression prints where its
  value is used, which the engine allows to be a later block. `mov.l @r4,r1; tst r5,r5; bt L`,
  with r1 used only at L, printed `if (param_2 == 0) { return *param_1; } return 0;`: the read
  the code always makes appears made on one path. The same with the read in the branch's delay
  slot. `checkImpliedCover` now keeps a load explicit when any use in its implied chain is in
  another block. In the game's work RAM this hoisted 16 reads out of a `&&` clause (one of them a
  sound RAM read, `*(Ram25a00414 + 0x25a00060)`, that had looked conditional on a RAM flag) or a
  call argument into the block where they happen; every other output is unchanged.
- **Why the ledger accepted both.** It compared printed lines with instruction order, which the
  `for` header defeats (the step prints above the body and runs after it), and it skipped a read
  whose statement is in another block instead of calling it conditional.
  - `MovedInBlock`: two accesses through pointers in one block must be in the order their ops
    were created, since a block is one straight run of instructions (or runs the engine joined),
    and nothing but a move changes that. Register accesses are user ops the engine made later,
    so they are not compared; they are never moved (`isMoveable` refuses user ops).
  - `conditionalInPrint` is also set for a read whose statement is in another block than the
    read.
  - A read printed in array form (`p[1]`) has no token of its own: the index expression prints
    the brackets. Its lines are now the pointer expression's, so the line checks see it.
  - `TestGameCode` counts both: 0 and 0 on both save states.
- **Other shapes tried, each now a fixture, none defective:** `tas.b` on a register (one read
  then one write, both kept), a GBR-relative read, a read in a return's delay slot, one byte of a
  4-byte read and a 4-byte read stored as a byte (the read keeps its width), two stores through
  one pointer (both kept), a compound assignment through pointers, two reads as a call's
  arguments, and nested booleans (`!(a && *p)`, `a && (b && *p)`: the read stays before the
  expression). The other op movers in the engine were read: `ConditionalExecution` refuses loads,
  stores and calls; `moveRespectingCover` crosses only COPY and CAST; the rest re-insert a
  converted MULTIEQUAL or INDIRECT at the same logical place.
- **Register map.** `tools/audit_hardware_map.py` re-run against beetle-saturn's `mednafen/ss`
  (upstream Mednafen's tarball is not reachable from the build host): the same 22 recorded
  differences, nothing new. Re-read by hand: the mirror periods (VDP2 `A & 0x1FE`, SCU `A & 0xFC`,
  SMPC `(A & 0x7F) >> 1`), `SCU_IST` returning the pending set, `SCU_PPAF` clearing V, E and the
  DSP interrupt on a read, and `SCU_PDD` advancing the DSP address on a read while it is stopped.

Mutation, each rebuilt and run: without the `isMoveable` rule the two for-loop fixtures fail
(`06009F84 was moved after 06009F86 in block 2`); without the cross-block rule the two
branch fixtures fail (the read's statement is in block 1, and the ledger calls it conditional).

Matrix, rerun whole:

| Check | Result |
|---|---|
| Fixtures (`Sh2DecompilerTests`, 13 new) and the whole suite | 91 of 91 tests pass. |
| Range model | 0 disagreements (unchanged). |
| Game code, two save states (839 and 1,554 prologues) | 839/839 and 1,554/1,554 decompile; 0 of every ledger count: duplicated, wrong width, two reads in a statement, printed past another access, conditional, moved within a block. 16 outputs differ from `5670323`, all the hoisted reads above. |
| Against `c4927c1` on the same images | 223 of 839 and 476 of 1,554 outputs differ (the earlier rounds' changes plus these 16; sampled: reads into variables, named registers, polls reading into a variable). Time 4,934 -> 4,895 ms and 15,618 -> 17,195 ms (about -1% and +10%); the worst function 2.5 s either way. |
| Sanitizers | gcc ASan + UBSan (vptr included) builds of `Sh2DecompilerTests` (game code included), `DecompilerPanelTests`, `DecompilerRunnerTests`, `Sh2DecompilerEngineTests` and `Sh2DecompilerFixtureTests`: no ASan error; UBSan reports only the two upstream SLEIGH shift sites (`address.hh:605`, `slghpatexpress.cc:979`), as before. The clang runtime is not installed on this host, so clang was not used. |

Unverified: switch selectors read from memory, MSVC and macOS builds, LeakSanitizer, and the
string and pointer-inference positive controls from the consolidated round.

### Review of `0e3cd12`: jump-table recovery, the low work RAM mirror, and the ledger's oracle

Two external reviews of `0e3cd12`. The first found two P1s in jump-table recovery, a missing
mirror, and that the ledger's order checks were anchored on op ids, addresses and output lines;
the second found the two shapes on which those anchors reject correct output. Everything
reproduced except where noted.

- **P1: a switch's guard folded over a read.** `x = *p; if (x > 3) goto def; y = *q;
  switch (x)` printed `y = *q` before the `switch`: with no statement in the switch block, the
  engine folds the guard into the switch as its default and joins the blocks, and an unused load
  (kept for its effect) is no statement to `noInterveningStatement`; a store or a call would have
  stopped it. The same with the read in the jump's delay slot. The read then appears made for
  `x > 3` too. Patch 0013: the fold is refused when the switch block holds a LOAD other than the
  table lookup. The table lookup stays exempt (the reviewer measured 11 game functions that fold
  through one); the control fixture still folds.
- **P1: a guard on one read bounding a switch on another.** `if (*p < 4) switch (*p)` lost the
  first read and its `if`: `GuardRecord::valueMatch` took two loads from one pointer for one value
  (upstream's own `TODO: we need to check for aliases`), so the guard bounded the switch on the
  second read and was folded away. With a store between the reads the guard printed, but the
  table was still bounded by the other read. Patch 0013: two different LOAD ops never match. The
  guard stays an `if`, both reads stay, and the table, unbounded, is not recovered: the jump is an
  indirect call and nothing is lent. In the game's work RAM this fires once per save state
  (0x06017E38 and 0x06017E48, on ordinary RAM).
- **P2: low work RAM's mirror.** Mednafen maps 0x00200000-0x003FFFFF as 1 MiB twice;
  `VolatileAliases` repeated only high work RAM, so a mailbox marked at 0x20201000 and polled
  through 0x20301000 folded to `do {} while (true)`. Both work RAM areas now repeat, the range
  model in the tests has the same rule (0 disagreements over 192,000 addresses), and a fixture
  polls through the second image.
- **P2: a `void` return lent as known.** A callee whose analysis found no value returned is
  lent as void, and a caller doing `return P() + 1` printed `P(); return extraout_r0 + 1;` with
  its own signature still reported as known. The caller's signature is now withheld: `it uses r0
  after calling P, which is not known to return it` (`extraout_` is the engine's name for a
  register read after a call that the callee's signature does not return). The mirror gap above
  was one way to such a callee: a poll that folded to an endless loop returned nothing.
- **P2/P3, the ledger (both reviews).** Order was judged by op creation ids within a block and by
  output lines across blocks. Both reject correct output: a branch on a constant whose blocks the
  engine joins in flow order (B's read, created later, rightly before A's), and a `for` loop
  whose step prints in the header above a body read it runs after. And neither could see a read
  dropped from the output, nor one printed as unconditional when the code makes it under a guard.
  The oracle is now the flow itself:
  - `DecompileResult::flowSuccessors` is the block graph as first followed, before any action,
    and each access and call carries its block and position there (`origBlock`, `origOrder`).
    Two accesses in one final block must be in an order the code can make them in
    (`FlowReach`: same block and earlier, or a path between the blocks). Op ids, addresses and
    lines are no longer compared. Both rejected outputs are fixtures and pass.
  - `DecompileResult::calls` lists the calls, and a read printed where its value is used must not
    pass a call any more than another access (`PrintedPast`).
  - `FoldedIntoSwitch`: two accesses from blocks the flow kept apart by a branch, joined into a
    block that ends in a switch's jump. With patch 0013 removed, the two guard fixtures fail on
    this check alone.
  - **The reference run.** `DecompileRequest::allMemoryVolatile` makes every address volatile
    (literal pools excepted: read-only wins), so the output holds every memory instruction the
    flow reaches. The ordinary output is compared with it: an access of the reference that is
    missing must be one of ordinary memory at a constant address, which folds into a variable;
    a register, user-marked memory or any access through a pointer that is missing was dropped
    (`ReferenceOk`, every fixture; `TestGameCode` counts them). A pointer whose value the
    reference read from ordinary memory (`pointerFromOrdinaryMemory`) is exempt too: the ordinary
    run may know that memory's value, when the function stored it, and fold the access into a
    variable (three byte stores in one game function did exactly that). Its limit: a drop that
    does not depend on volatility -- the second P1 above, inside jump-table recovery -- happens in
    both runs alike; that one is pinned by its fixture's text.
  - The ledger's chain from a read to its statement stops at a call: the read is the call's
    argument, printed in the call, whatever is done with the call's result (two game functions
    were otherwise reported as a read printed past a call).
- **Found by the reference run on game code: a folded poll removes hardware writes.** Two
  functions (0x06003C06, the master's wait for the slave; 0x0600C2DA) write a work RAM word and
  poll it. Unmarked, the write is forwarded to the read, the loop never ends as analysed, and the
  engine removes everything after it as unreachable: `SH2_IPRA = 0`, `SH2_IPRB = 0`,
  `SH2_TIER = 1`, `SINIT = 0xffff` and the `SH2_FTCSR` poll in the first, two `VDP2_BGON` writes in
  the second. The output was `do {} while (true);` with the engine's own warnings and a known
  `void` signature. The design still asks the user to mark the mailbox, but the output now says
  what that cost: when the engine reports an unreachable-block removal or a do-nothing loop, the
  function is analysed once more with every address volatile, and each hardware access of that
  run missing from the shown output is an amber line on its instruction and a warning (`the write
  of SH2_IPRA here is missing: the analysis removed the code around it as unreachable, after a loop
  it takes to run for ever. A poll of memory another processor writes folds so unless that memory
  is marked volatile`), and the signature is withheld. The cost is paid only by functions with such
  a removal. Fixture: the poll then an SMPC write, unmarked (said, withheld) and marked (three
  accesses, nothing said). `TestGameCode` counts a missing access the output says as said, not
  dropped.
- **Found by the reference run on game code: a read dropped with the store it fed.** In
  0x0601561A, `Ram060a51f6 = *(*(param_1 + 0x18) + 0x28)` and its two reads were gone: the global
  is overwritten on every path before any read, so the store is removed as dead -- late, once a
  later pass has made it a direct write -- and `ActionDeadCode::lastChanceLoad`, which holds an
  unused load for its effect, gives up after the first heritage pass, so the two reads went with
  it. Patch 0014: under `keepUnusedLoads` the hold is offered in every pass. One load must not be
  held: a recovered switch's table lookup becomes unused when the switch replaces it, and held it
  printed as a stray read and stopped patch 0013's guard fold; `foldInNormalization` now flags it
  (`PcodeOp::jumptable_lookup`) and both skip it. Fixture: `mov.l @r4,r1; mov.l r1,@global;
  mov.l #0,@global` keeps `*param_1`; without the patch the ledger's reference run rejects it.
- **P3s.** `ReturnCheck::Get` now reads part of a tracked register (a `mov.w r1,@r2` stores
  r1's low word; it was taken for anything, and a PR-saving caller lost its signature). The
  runner's too-large memo is keyed by the entry and the volatile list. A saved volatile list that
  does not parse is kept as it is, shown in the editor with the error, and saved back unchanged
  until a list that parses replaces it. The shown output is stale when the function set changed
  after it arrived, even with nothing pending (after a Stop, or a disabled runner).

Mutation, each rebuilt and run: without patch 0013 the two guard fixtures fail (`joined into a
switch's block across a branch`, from the ledger alone) and the second-read fixture fails on its
text and its withheld signature; without patch 0014 the overwritten-store fixture fails on its
ledger, its text and the reference run; the ledger's own earlier false positives are fixtures that
pass.

Matrix, rerun whole:

| Check | Result |
|---|---|
| Fixtures (`Sh2DecompilerTests`, 15 new; one each in the runner and panel suites) and the whole suite | 91 of 91 tests pass. |
| Range model | 0 disagreements, low work RAM's mirror included. |
| Game code, two save states (839 and 1,554 prologues), each function also decompiled as the reference | 839/839 and 1,554/1,554 decompile; 0 of every ledger count: duplicated, wrong width, two reads in a statement, printed past an access or a call, conditional, out of the code's order, joined into a switch across a branch, dropped against the reference. 16 and 20 accesses are missing and said (the folded polls above: 4 functions in all, 36 hardware accesses, each now an amber line and a warning). 5 and 8 outputs differ from `0e3cd12`: those cautions, the restored `Ram060a51f6` read, the guard no longer folded over a read (a `default:` becomes `case 3:` plus `default: return;`), and reads through a pointer now kept after the store they fed was removed as overwritten (`xVar1 = *param_1;` and two more in 0x06065EDC). |
| Against `c4927c1` on the same images | 225 of 839 and 480 of 1,554 outputs differ (the earlier rounds' changes plus this round's). Time 5,235 -> 5,668 ms and 17,610 -> 18,868 ms (about +8% and +7%); the worst function 2.8 s either way. The ledger's reference run is test-only and not in these times; the production caution's second analysis runs only for the 4 functions with a removal. |
| Sanitizers | gcc ASan + UBSan (vptr included) builds of `Sh2DecompilerTests` (game code and its reference runs included), `DecompilerPanelTests`, `DecompilerRunnerTests`, `Sh2DecompilerEngineTests` and `Sh2DecompilerFixtureTests`: no ASan error; UBSan reports only the two upstream SLEIGH shift sites (`address.hh:605`, `slghpatexpress.cc:979`), as before. `DecompilerRunnerTests` failed once under ASan while the game-code run shared the machine (its 50 ms budget test took a correct answer for a late one) and passed when run alone. |

Unverified: switch selectors loaded from memory beyond the fixtures here; MSVC and macOS builds;
LeakSanitizer; the string and pointer-inference positive controls.

### Review of `0c2dd20`: the reference run's exemptions, the lookup flag, and the reference run's own failures

Two external reviews of `0c2dd20`, both probing the three questions the last round left open.
Everything reproduced.

- **P1: a selector read from memory went with a switch whose cases all coincide.** `switch
  (*p)` with every case at one destination is removed by `RuleSwitchSingle`; the read `*p` went
  with it, and its pointer parameter too, with the signature known: `return 0;` for a function that
  reads memory. Patch 0014's `foldInNormalization` flagged every LOAD on the model's paths
  (`PathMeld`), and those paths reach above the switch variable to the read that produced it, so
  the selector carried the table lookup's flag and nothing held it. Patch 0015: only the loads
  between the switch variable and the jump are flagged, walked from the jump's address input back
  to the switch variable through the model's own ops. The selector stays (`xVar1 = *param_1;`)
  and is the argument; a switch with distinct cases still prints as one with its selector read
  once. Fixture: four cases to one `rts`, the selector clamped to 3 when out of range. That clamp
  (a MULTIEQUAL of the read and the constant feeding the jump) is what the first switch model
  cannot normalise, so this fixture is recovered by the second-stage model (`JumpBasic2`: an
  instrumented run reports it, and only it, recovering there; the game's work RAM tries that
  model in 4 functions and recovers none). The flagging walk is thus exercised on both models.
- **P1: a pointer read from ordinary memory exempted an access from the removal check.** The
  mailbox poll with the hardware pointer saved in work RAM first and read back after the poll
  printed `do {} while (true);` with no caution and a known `void` signature: the removed write
  was `pointerFromOrdinaryMemory`, which both the production check and the tests' oracle skipped,
  wherever the access was. The exemption was for a pointer that folds in the ordinary run (the
  function stored it; the access becomes a direct one to ordinary memory, which may be dropped as
  overwritten); a pointer's origin says nothing about whether the code around the access was
  removed. The removal is now known: the printed pass records the blocks the engine's warnings
  name (`Removing unreachable block (ram,0x...)`, one per block as it was) and, from the flow's
  own block graph, every block only reachable through them, as `DecompileResult::removedInsns`.
  In code so removed, every access that is a register, user-marked memory or through a pointer is
  said, whatever the pointer came from; the fold exemption applies only to code the analysis
  reached. A register or marked access that is missing is said whether or not its block is in
  that record, so the record gates only the exemption. The record is exact by the engine's own
  order: the edge is cut in `ActionDeterminedBranch` and the blocks removed in the action right
  after it (`ActionUnreachable`), while the two places that splice blocks (`ActionRedundBranch`,
  before the cut in each pass; `ActionDoNothing`, after the removal) never see an unreachable
  block. Fixture: two tests of the mailbox cutting both edges into a two-block region whose second
  block has the live second test as a flow predecessor; both blocks are warned by their own start
  and both writes are said. The tests' oracle (`ReferenceOk`, `TestGameCode`) draws the same line, and demands the
  caution itself on the instruction (`SaidMissing`: a caution note at that address) rather than
  any warning that prints the address -- the engine prints its own in lowercase, so an address of
  digits alone matched either. `pointerFromOrdinaryMemory` also no longer counts a pointer read
  from user-marked memory as ordinary (the mark keeps that read in an ordinary run too). A
  pointer made of a register's address plus an index read from RAM keeps its access
  (`(&VDP2_TVMD)[Ram26001000]`, probed).
- **P1: an allocation failure while a prototype parameter is replaced freed it twice (upstream).**
  `ProtoStoreSymbol::setOutput` deleted the old parameter and then allocated its replacement; when
  the allocation threw, the freed pointer stayed in the store and its destructor freed it again.
  The first allocation after the reference run's actions begin is exactly this one, so the
  reviewers' injected failure died in a double free. Patch 0015 makes the replacement first in
  `setOutput` (both stores), `setInput`, `clearOutput` and `getSymbolBacked`. Pinned by the
  `reference` children below: the mutant dies with a segmentation fault.
- **P2: the reference run swallowed failures and returned a result that claimed the check.** Its
  `catch (...)` returned with the signature known; an allocation failure inside the engine did not
  mark it damaged, unlike the same failure in the shown run. Now: an allocation failure or any
  unexpected exception leaves through `Decompile`'s handlers like the shown run's (Failed, later
  requests refused); an engine error, a status other than Ok, or a cancel (a stop, or the time
  budget -- the shown output is finished, so the result stands) adds a warning that the removed
  code could not be checked, with the reason, and withholds the signature (`code removed as
  unreachable was not checked for hardware accesses`). The reference run also no longer reports
  its phases to the observer (the panel's progress restarted); two test seams see them
  (`SetPhaseHookForTests`, `SetBeforeReferenceHookForTests`).
- **P2: the production reference run missed what a literal-bounded switch holds.** It ran one
  pass with no callees registered, so a switch bound that only the second pass folds (a `mov.w
  @(disp,pc)` literal, marked read-only before flow from the first pass's findings) stayed
  unbounded, the table was not recovered, and the three `SMPC_SF` writes in its cases were never
  reached: the caution named only the selector read. The reference run now follows the flow with
  the literals and callees the shown pass followed its with (`PassSeed`: what the first pass
  found, handed to the printed pass), in one pass, so both runs have the same flow. Fixture: the
  poll, then the switch; the three writes and the selector read are said.
- **P3: the caution claimed a loop that was not there.** A test of the mailbox taken always one
  way, with no loop, removes the other way; the caution said `after a loop it takes to run for
  ever`. It now says a branch (`after a branch it takes always the same way. A test of memory
  another processor writes is decided from this function's own write unless that memory is marked
  volatile`) unless the engine's do-nothing-loop warning is there. And `the write of through a
  pointer` reads `the write through a pointer`.
- **P3s in the tests.** `ReferenceOk` returned true when the reference run was not Ok; it fails
  now, and `TestGameCode` requires every function to have a reference.
- **Found by LeakSanitizer, run for the first time: a failed assisted jump model leaked.**
  `JumpTable::recoverModel` tries `JumpAssisted` first when the jump's address is a CALLOTHER
  output and, failing, moved on without freeing it. On SH-2 that CALLOTHER is a volatile read: a
  `jmp @rN` through a pointer kept in memory the user marked, or any such jump in the all-volatile
  reference run, where the game's work RAM leaked 48 bytes once. Patch 0016 deletes the failed
  model. Fixture: a jump through a pointer in work RAM, marked and in its reference run.

Mutation, each rebuilt and run: without patch 0015's flag change the same-destination fixture
fails (ledger and text); without its allocation order the `reference +0` child dies in a
segmentation fault (`rc == 0` fails); without patch 0016 LeakSanitizer reports the 48-byte leak on
the fixture suite; with the old pointer exemption the saved-pointer fixture
fails (`SaidMissing` and the withheld signature); with the reference run back to one unseeded pass
the literal-bounded switch fixture fails (the three writes not said); with the loop wording
forced the branch fixture fails; with the observer told of the reference run's phases the
observer fixture fails (3 passes seen, not 2); with a cancel in the reference run rethrown the
cancel fixture fails (Cancelled, not Ok); with the reference run's `catch (...)` restored the three
`reference` children fail (Ok with the signature known, not Failed).

Matrix, rerun whole:

| Check | Result |
|---|---|
| Fixtures (`Sh2DecompilerTests`, 4 new fixtures with 7 runs, the observer and cancel checks, 3 allocation-failure children) and the whole suite | 91 of 91 tests pass (`ctest`, 122 s). |
| Range model | 0 disagreements. |
| Game code, two save states (839 and 1,554 prologues), each function also decompiled as the reference | 839/839 and 1,554/1,554 decompile; 0 of every ledger count, 0 functions without a reference; 16 and 20 accesses missing and said, as before (the same 4 folded polls). 0 outputs differ from `0c2dd20` on either image: the selector fix and the removal rule change no game output, and the game's removals are all loops. |
| Against `c4927c1` on the same images | 225 of 839 and 480 of 1,554 outputs differ, as last round. Time, both builds run back to back on an otherwise idle machine: 4,970 -> 5,242 ms and 18,005 -> 19,649 ms (about +5% and +9%); the worst function 3.6 s either way. The production reference run is now one pass seeded with the shown pass's flow, so the 4 functions that pay for it pay for one pass, not two. |
| Sanitizers | gcc ASan + UBSan (vptr included) builds of `Sh2DecompilerTests` (its allocation-failure children and the game code on both images with their reference runs included), `DecompilerPanelTests`, `DecompilerRunnerTests`, `Sh2DecompilerEngineTests` and `Sh2DecompilerFixtureTests`: no ASan error; UBSan reports only the two upstream SLEIGH shift sites (`address.hh:605`, `slghpatexpress.cc:979`), as before. Before patch 0015 the `reference +0` child died in the double free the reviewers found. |
| LeakSanitizer (first run; `detect_leaks=1` on the same builds) | `Sh2DecompilerTests` (children and fixtures, the jump-through-pointer fixture included), `Sh2DecompilerEngineTests`, `Sh2DecompilerFixtureTests`, `DecompilerPanelTests`, `DecompilerRunnerTests` and the 839-function game run with its reference runs: 0 leaks with patch 0016; without it, 3 leaks of 48 bytes in the fixture suite and 1 in the game run, all at `JumpTable::recoverModel`. `DecompilerRunnerTests` failed its 50 ms budget check once while sharing the machine with a game run and passed alone, as last round. |

Unverified: switch selectors loaded from memory beyond the fixtures here (every case the same
through the second-stage model, distinct cases, the guard shapes of the last round); MSVC and macOS
builds; the string and pointer-inference positive controls (string and pointer inference are not
reachable in SE's setup, which has no `char *` types, so the guard has nothing to fire on). Noted
for the next review, not changed: a `jmp @rN` through a pointer kept in unmarked work RAM prints as
a call to the address the snapshot holds there (`(*pcRam00000000)()` when it is 0), the value at
the moment of the capture.


## Program Phase 0: the generator (`program/generator`)

`SaturnExplorerSh2GeneratedTests` (`FrontEnd/tests/Sh2GeneratedTests.cpp`, one `ctest` entry,
`SaturnExplorerSh2GeneratedTests`) generates random SH-2 functions from a small grammar, runs
each on the vendored p-code emulator for the memory accesses it really makes, decompiles it, and
checks the decompiler's access ledger (`DecompileResult::accesses`) against that trace. It is the
program's instrument for finding failure classes without a human reviewer
(`Docs/Decompiler/REVIEW_PROGRAM.md`, Phase 0). Generation is a pure function of the seed.

**Grammar.** 3 to 14 items after two pointer loads: ALU (`mov #imm`, `mov`, `add`, `sub`, `and`,
`or`, `xor`, `shll2`, `extu.b`, `extu.w`, into r0-r3 only); loads and stores of byte, word and
long through r4/r5 (scratch RAM parameters) and r6/r7 (pointers loaded from a literal pool word,
placed after the code and 4-aligned) as `@Rn`, `@(disp,Rn)`, `@(r0,Rn)`, `@Rm+`/`@-Rn` (r4/r5
only) and `tas.b`; the literal is a hardware register or range address (from
`SaturnHardwareRegisters`/`HardwareRanges`, cached or cache-through), an ordinary RAM address
(`0x2600xxxx`, sometimes `0x0600xxxx`) or a mailbox (16 bytes at `0x26001000`+, which the second
request marks volatile); a forward `bt`/`bf` or `bt/s`/`bf/s` (with a delay-slot instruction)
over one to three instructions, which may reload a pointer; a counted loop (`mov #n,r3 ; body ;
dt r3 ; bf`); at most one mailbox write-then-poll (write first or not, polling for bit 0 clear or
set, on a mailbox, a hardware register or unmarked RAM); `rts` with a `nop` or an instruction in
the slot. r15 is never touched and a delay slot never holds a PC-relative load or a branch.

**Ground truth.** `TracingEmulate` is an `EmulatePcodeCache` that hooks `executeLoad` and
`executeStore`, so each record has the exact width and the p-code op's instruction address (a
delay slot's access carries the branch's address, as the decompiler's ledger does). A
`MemoryBank` cannot give that: it sees aligned 8-byte words. High work RAM (both partitions, every
1 MiB repeat) is real; every other address is "hardware": reads return a default (zeros, ones or
pseudo-random by run), writes are dropped, all logged. Three runs with different register and
hardware inputs are united. A polled address reads busy twice and then ready, as if another
processor answered, when it is hardware, or memory the request marks volatile (in that request's
run only); an unmarked work-RAM poll has no one answering (it folds in the decompiler, by design),
so that run may stop at the step limit (2,000 instructions), and what follows did not run.

**Check**, per seed, for the default request and again with the mailbox marked volatile when the
program has one: status is `Ok` and the result is not unsupported, unimplemented, truncated or
holding bad data; every traced (instruction, read or write, size) has an entry in the ledger, or
is a literal-pool load (folded by design), or an access to ordinary memory at a constant address
(a literal-derived pointer; the address is not hardware, not marked, and not in the scratch area
the parameters point into), or is in code the output says it removed (`SaidMissing`: a caution
containing "here is missing"); every listed access is printed once, is not `repeatedOnPath` or
`conditionalInPrint`, and the ledger's ordering invariants hold (`Unordered`, `OutOfFlowOrder`,
`FoldedIntoSwitch`, `PrintedPast`, copied from `Sh2DecompilerTests.cpp`). Everything else is a
dropped access. A failing seed prints its words as a `Fixture` (the pool among the words, the
literal offset and value of the first pool word), the literals, a disassembly, the trace, the
ledger, the notes and the decompiled text, and the first findings are shrunk (items removed, a
`bt/s` made plain, a loop made one pass) to the smallest program that still fails the same way.

**Running.** `SaturnExplorerSh2GeneratedTests <scratch dir>`, with `SE_GENERATED_SEEDS=<n>` (default
300), `SE_GENERATED_FIRST=<k>` (a nightly run moves it), `SE_GENERATED_SEED=<k>` (one seed, with
everything printed), `SE_GENERATED_SHRINK=<n>` (default 3), `SE_GENERATED_VERBOSE=1`,
`SE_GENERATED_MUTATE=1` (deletes the first ledger entry of every decompilation; the run passes
only if the check reports it: 278 of 300 seeds do, the rest list nothing that must be listed),
`SE_GENERATED_FENCE=selfops|folds` (keeps the triggers of A5-1 out of the grammar, so a long run
can look past it) and `SE_GENERATED_WORDS="D102 6010 ..."` (one hand-written function, with
`SE_GENERATED_VOLATILE=<first>-<last>` and `SE_GENERATED_POLL=<address>[:until-set]`), which is how
a finding is reduced by hand.

**Default run** (300 seeds, `ctest -R Generated`, RelWithDebInfo on a 4-core container; a Debug build
takes 5.8 s with shrinking off): 4.6 s, 2.4 s of it in the decompiler (about 16 ms a seed, with
three or six emulator runs and one or two decompilations). The summary line:

```
SUMMARY: seeds=300 first=1 shapes covered: alu=1082 loads=885 stores=740 (plain 793 disp 530
indexed 230 inc/dec 72) tas=134 widths b/w/l=618/539/602 literal pointers hw/ram/mailbox=394/244/325
skips=393 (delayed 132) loops=179 polls=204 (write first 138, until set 70; mailbox 131 hw 55 ram 18)
rts slot insn=82; second requests (mailbox marked)=216; traced accesses=3002; unfinished emulator
runs=236; failing seeds=2; findings=2 (4946 ms total, 2430 ms in the decompiler, 16.5 ms per seed)
```

**The test failed, by design, until the findings below were fixed** (all three are fixed in "Area 5, round 1: the dead-read class" at the end of this file, and `WILL_FAIL` is gone): seeds 51 and 65 were A5-1. Longer
runs: 3,000 seeds, 40 failing seeds and 62 findings, every one a dropped read (the ordering and
printing invariants never fired); with `SE_GENERATED_FENCE=folds`, 20,000 seeds: 12 failing seeds
and 18 findings, 5 of them A4-1 and the rest A5-1 reached through a counter a loop left at 0 or a
byte stored to unmarked RAM (about 1 seed in 1,700). No dropped write, no wrong width, and no
status other than `Ok` in 23,000 seeds.

**Findings** (`Docs/Decompiler/REVIEW_LEDGER.md` has the fixtures, current and expected):

- **A5-1 (P2).** A read whose value feeds only an expression the engine folds to a constant
  (`xor r0,r0`, `sub`, `cmp/eq Rn,Rn`, a test against a register a loop left at 0) is dropped from
  the output and the ledger with no note: hardware registers, marked memory, and reads through a
  pointer alike. `mov.b @r1,r0 ; mov #0,r0` keeps the read. The all-volatile reference run loses the
  same reads, so `ReferenceOk` cannot see the class.
- **A5-2 (P3).** The "here is missing" caution shares that blind spot: in code removed after a poll
  that folds, a read A5-1 dropped is not named.
- **A4-1 (P2).** `if (c) a; hardware read; if (c) b;` prints no read at all.

Wrong in the first drafts of the check, so a later reader does not repeat them: a delay slot's
access belongs to the branch's address (the ledger's, and the emulator's op address), not the slot's;
whether a pointer is a literal's is judged by the address the run touched (the scratch area is
the parameters'), not by every literal that could reach the register on paths that never ran (the
decompiler knows some cannot); an unmarked RAM poll must not be answered by the emulator's other
processor, or the code after it runs in the trace and not in the analysis; an address that wraps
past 2^32 is a plain constant, not hardware.

Not done: calls (`jsr`/`bsr`, lent prototypes), jump tables and `switch`, and `bra`, in the
grammar (the brief's later version); a nightly job (CI is another branch); the MSVC build of this
file (it compiles without warnings under `-Wall -Wextra` on gcc); a wider fence than `folds`
(a store-forwarded constant still reaches A5-1).

## Program Phase 0: the conservation oracle

The review of `0c2dd20` ended with two exemptions in the reference-run oracle that a reviewer had
to probe by hand: `MemoryAccess::pointerFromOrdinaryMemory` ("a pointer read from ordinary memory
may have folded, so a missing access through it is excused when its code was reached") and
`DecompileResult::removedInsns` (the blocks the engine removed, parsed from the `Removing
unreachable block` warning text and closed over the flow graph). Both are gone. The engine now
records how every flow-time LOAD and STORE ended (patch 0017), and the oracle is a conservation
law: **every memory access the flow makes is accounted for exactly once.** Branch
`program/conservation`.

**What the engine records.** `Funcdata::getAccessFates()`: for each memory access of the flow that
stopped being a LOAD or STORE, `{ time, pc, addr, store, size, fate }`, cleared with the function (a
restart clears it with the ops it describes, so it matches the final p-code; the host matches by
instruction, not by op time, for the same reason). The fates:

| Fate | How it ends | Recorded in |
|---|---|---|
| `fate_folded_ordinary` | `RuleLoadVarnode` / `RuleStoreVarnode` made it a direct read or write (a COPY) of a Varnode that is not volatile: a variable data-flow may forward or remove. A volatile address is not recorded here: `replaceVolatile` makes a user op of it, a real access that stays in the p-code and is listed. | the two rules, through `Funcdata::recordAccessFold` |
| `fate_unreachable` | removed with an unreachable block, whether or not a warning is issued; the silent removal in flow generation comes through the same function | `blockRemoveInternal`, which records every access of the block before it destroys any op (destroying one unsets the inputs of the ops that read its output, so a STORE later in the block no longer knows its width; the first game-code run crashed on exactly that) |
| `fate_switch_lookup` | the table read of a recovered switch (`PcodeOp::jumptable_lookup`, patches 0014 and 0015); the switch stands for it | `opDestroy` |
| `fate_dead` | any other `opDestroy` of one | `opDestroy` |

Two deviations from the brief's list of hook sites, both to leave no path unrecorded rather than to
enumerate them. The destroy-time records are in `Funcdata::opDestroy` itself (its 60-odd callers
include dead code, copy propagation, block removal, and the `double`, `subflow` and `prefersplit`
transformations), not in `ActionDeadCode` and `JumpBasic::foldInNormalization`; `jumptable.cc` and
`coreaction.cc` are unchanged. And what counts as an access is wider than LOAD and STORE: the
volatile read and write user ops, and the COPY of a volatile Varnode in the interval between a rule
and `replaceVolatile` -- a hardware write in a removed block is one or the other, never a STORE,
and a record limited to LOAD and STORE (the first version) saw none of the hardware writes the
mailbox fixtures lose to a removed block. Only ops the flow made (their time is below the op count at the end of `followFlow`) are
recorded: rules and actions also make loads and stores (a call's stack parameters) which are not
the code's, and one of them folding would otherwise account for a real access with the same
instruction and width. An instrumented build confirmed that on every fixture and both save states
the only opcode changes of a flow LOAD or STORE are those two rules (122,222 LOAD and 37,601 STORE
conversions of flow ops on the second save state; none from any other function).

**The host.** `DecompileResult::accessFates` (`AccessFate`: canonical instruction, direction, width,
fate, constant address), filled in the printed pass next to `accesses`. The rule, in three places
(`NoteRemovedAccesses`, `ReferenceOk` and `TestGameCode`, the last two through one shared
`AccountFor`): for each access of the reference run, find it in the shown run by instruction,
direction and width:

- in `accesses`: **printed**;
- a `FoldedOrdinary` or `SwitchLookup` record: **folded** (a `FoldedOrdinary` at a hardware or marked
  address would not be ordinary, and does not count);
- an `Unreachable` record: **removed**. It must be said by the removal caution (production: said by
  `NoteRemovedAccesses`; tests: `SaidMissing`), unless it is a direct access of ordinary memory;
- a `Dead` record, or none: **dropped**, which fails the oracle.

`NoteRemovedAccesses` now runs when any access has an `Unreachable` record, not when the warnings
name a block, and says exactly the removed hardware, marked and pointer accesses. Removed with
their computation: `pointerFromOrdinaryMemory`, `removedInsns`, `RemovedInstructions`, the warning
parse, and `FlowRecord`'s block starts and instruction lists. The caution's wording and the single
seeded reference pass are unchanged. Direct accesses of ordinary memory are now checked too (they
used to be skipped): each must be printed, folded, or removed.

**Fixtures** (`TestHardwareVolatility`). `ptrFromRam`: `storeOverwritten` with the pointer read from
ordinary work RAM instead of a register. The pointer does not fold (the function never stores it),
the read is held for its effect, and the output is `xVar1 = *pxRam26002000;`. It is the shape the old
exemption excused: in the reference run the pointer is a volatile read of ordinary memory, so the
access stays a read through a pointer that is `pointerFromOrdinaryMemory` and in reached code. The
records are asserted in `storeOverwritten`, `ptrFromRam`, `mailboxFold` and `switchFold`, and
`twoCuts` asserts its removals are recorded. `Print` lists a result's records.

**Mutation**, each rebuilt and run (fixtures, then the 839-function save state; the 1,554-function
one for the store hook):

| Mutation | Result |
|---|---|
| `RuleStoreVarnode` hook off | 11 fixture failures (`storeOverwritten`, `ptrFromRam` and every fixture with a store to a global); game code: 905 and 3,925 accesses dropped on the two save states |
| `RuleLoadVarnode` hook off | 11 fixture failures; 969 dropped on the first save state |
| record in `blockRemoveInternal` off | `mailboxFold`, `ptrSavedInRam`, `litSwitchAfterPoll`, `twoCuts` and `forwardFold` fail the oracle and the caution checks (23 failures, with the allocation-failure children that check the caution); the 16 removed accesses of the first save state are dropped |
| switch-lookup fate off | the record check in `switchFold` fails; no oracle result changes (the reference run lists the lookup in no function of either save state) |
| patch 0014 reverted | `storeOverwritten` and `ptrFromRam` fail (9 failures); the game code is unaffected on the first save state (0 dropped) |

The last row is the one that matters for the fixture. A copy of the tree before this change, with
`ptrFromRam` added and patch 0014 reverted, gives `ReferenceOk` **true** for `ptrFromRam` under the
old oracle (excused; the Ledger and the text still reject it) and false for `storeOverwritten`
(its pointer is a register). Under the conservation rule `ReferenceOk` is false for both.

**Game code**, two save states, built `RelWithDebInfo`; every ledger count 0.

| | 839 prologues | 1,554 prologues |
|---|---|---|
| decompile | 839/839 | 1,554/1,554 |
| dropped against the reference run | 0 | 0 |
| missing and said (removed as unreachable) | 16 | 20 |
| records, all functions: folded into ordinary memory | 14,831 | 38,678 |
| ...dead | 41 | 60 |
| ...unreachable | 16 | 20 |
| ...switch lookup | 27 | 155 |
| reference-run accesses: printed | 5,038 | 18,855 |
| ...folded (a switch lookup among them) | 1,874 (0) | 7,390 (0) |
| ...removed (ordinary memory / said) | 16 (0 / 16) | 20 (0 / 20) |
| ...dropped | 0 | 0 |
| time | 5,368 ms | 19,791 ms |

The 16 and 20 unreachable records are the missing-and-said accesses, one for one. Time is +2% and
+1% against the last round's 5,242 and 19,649 ms. The 91 tests pass (`ctest`, 64 s).

**What the records show that the reference run cannot.** The oracle compares with the reference
run's list, so an access lost in both runs alike is invisible to it; the records are not. 41 and 60
flow loads ended dead with nothing for the reference run to list: 10 and 26 are reads at constant
addresses of ordinary memory (literal-pool words and unused globals; no loss), none of hardware or
marked memory, no write, and **31 and 34 are reads through a pointer** (listed by `SE_GAME_FATES=1`,
counted in the `TestGameCode` summary line "ended dead"). The one examined, `0x060228B4`, is `-O0`
code that copies bytes through a pointer into a stack buffer; four of its six byte reads through
`param_2` ended dead and the output shows two. A likely mechanism, not confirmed: the load's only
consumer is a stack store removed in the same dead-code pass, so the load is never unused at a pass
where `ActionDeadCode::lastChanceLoad` could hold it (patch 0014 holds a load that becomes unused in
a *later* pass). Through a pointer the memory may be a register's. Not fixed, not failing: the
brief's failure is a dropped access against the reference run, and these are in neither run. They
are the next input for area 5, and a ledger row.

**Limits.** An access is found in the shown run by instruction, direction and width, so when the
engine copies a block, one live or folded copy accounts for a dropped one (the printed ledger still
requires each copy on one path to print once). A load or store that a rule makes and a later action
removes is not tracked (it is not the code's). `opDestroyRaw` during flow (ops after a halt, the
call an injection replaces) is not recorded: none is a block's access. The default `cmake`
configuration (no build type) compiles the host unoptimised, and `DecompilerRunnerTests` then takes
156 s before this change and 162 s after, over its 120 s timeout; with `RelWithDebInfo` it takes
64 s. MSVC and macOS builds are unverified, as are the sanitizers (the CI agent's).

## Area 2, oracle: exhaustive decode and differential execution

`SaturnExplorerSh2DecodeTests` (`FrontEnd/tests/Sh2DecodeTests.cpp`, branch `review/2-semantics`) is the
oracle `REVIEW_PROGRAM.md` asks for in area 2, SH-2 semantics and SLEIGH correctness. Mednafen's SH-2 core
(`ss/sh7095.inc`, `sh7095_ops.inc`, `sh7095_opdefs.inc`, `sh7095_idecodetab.inc`) is the hardware reference. The
test reads no Mednafen file by default and the repository holds a transcription, not a copy;
`SE_MEDNAFEN_SS=<ss dir>` makes a run re-check the transcribed word-to-op table against
`sh7095_idecodetab.inc` (all 65,536 words equal when this was written). One ctest entry, marked `WILL_FAIL`
while its findings stand (`REVIEW_LEDGER.md`, A2-1 to A2-8), so master stays green. The default run takes about
1.2 s (RelWithDebInfo). `SE_DECODE_CASES=<n>` runs more cases per form (60,000 per form, 7.8 million cases, took
20 s); `SE_DECODE_SEED`, `SE_DECODE_FORM=<text>` (only the forms whose name contains it), `SE_DECODE_SHOW=<n>`
(failing cases printed per form), `SE_DECODE_DUMP="<hex words>"` (the three decoders' text for those words)
and `SE_DECODE_PCODE=1` (the p-code of the selected named shapes) are for reproducing a finding.

### 1. Exhaustive decode

Every word 0x0000..0xFFFF is decoded by three decoders and the results compared:

- **SLEIGH**: the translator the decompiler uses (the checked-in `sh-2.sla` through `SaturnArchitecture`), one
  `printAssembly` per word, mnemonic and operand text kept separate; an undecodable word throws and is
  "undefined".
- **`Sh2Disasm`**: the debugger's decoder, `Sh2Decode`, with its mnemonic, operand text and flags (`IsBranch`,
  `IsCall`, `IsReturn`, `IsConditional`, `HasDelaySlot`, `HasBranchTarget`/`BranchTarget`).
- **Mednafen**: a table of its 125 ops (name, mnemonic, operand kinds, bytes of memory touched) and 141
  mask/value patterns saying which word is which op. The patterns were generated from `sh7095_idecodetab.inc`
  and verified equal on all 65,536 words; Mednafen defines 53,752 of them.

*Normalisation.* The decoders spell operands differently, so each text is rewritten into one normal form and
only spelling is removed: case and spaces; the optional `#`; an 8-bit immediate compared modulo 256 (`add #-4`
is `#0xFC` in `Sh2Disasm`, `-0x4` in SLEIGH; the sign extension is the execution half's business);
displacements as decimal byte counts (already scaled); a PC-relative operand as the address it reads
(`@(0xA,pc)` and `@(0xA)` both); branch targets as absolute 32-bit addresses; `shal` is `shll` (Mednafen
decodes 4n20 and 4n00 as one op, `SHLL_REG`, because they are one operation). The mnemonic, the registers, the
`@` forms and the operand order are compared as written. The expected flags are the ones `Sh2Disasm.h`
defines (Mednafen has none).

*Layouts.* A PC-relative operand and a wrapping branch target depend on the address, so each word is decoded
at four: RAM at 0 mod 4 and at 2 mod 4 (a nop on the other side), address 0 (a 256 KB region at the bottom,
where a backward branch wraps below zero) and, for `Sh2Disasm` alone, 0xFFFC0000 upwards (where a forward
one wraps past 2^32; the snapshot refuses code there, so the engine cannot be asked).

*Result.* SLEIGH agrees with Mednafen on all 65,536 words in the three layouts it can be asked in: the same
53,752 defined, the same mnemonic and operands, nothing accepted that Mednafen leaves illegal. `Sh2Disasm`
agrees on validity and operands everywhere, including the wrap at both ends, and differs in two ways: A2-6
(`bt.s`/`bf.s` for `bt/s`/`bf/s`, 512 words) and A2-7 (`trapa` is not marked `IsBranch`, 256 words).

### 2. Differential execution

*The two sides.* The emulator side is the vendored `EmulatePcodeCache` on the shipped translator: registers in
the register space, and a `MemoryBank` for RAM whose unwritten bytes are a hash of (case seed, address), so any
address reads as something definite and no memory has to be filled in; the 8-byte words an instruction wrote
are logged. The reference side is `ExecRef`, a transcription of `sh7095_ops.inc` for every runnable op (all
but `sleep`), with the PC of `sh7095.inc`: an instruction runs with z->PC = its address + 4; a taken delayed
branch makes it target + 2 while the slot runs; a delayed branch not taken leaves slot address + 4. Reads and
writes go to flat big-endian memory (the same hash). A case is compared on r0-r15, SR (T, S, Q, M, I and the
rest), GBR, VBR, MACH, MACL, PR, the next PC (where the emulator's execute address ends up, whether or not the
target can be fetched) and every aligned 8-byte word either side wrote.

*Forms.* Every Mednafen op except `sleep` is a form (124), plus a second copy of the six relative branches
(`bt`, `bf`, `bt/s`, `bf/s`, `bra`, `bsr`) with the code at address 0 so that a backward branch wraps below
zero: 130 forms, 2,000 cases each, 260,000 cases, from a fixed seed. They cover each ALU, shift, compare,
extension, swap, multiply, MAC and DIV form; `mov` in every addressing mode (`@Rn`, `@Rn+`, `@-Rn`,
`@(disp,Rn)`, `@(R0,Rn)`, `@(disp,GBR)`, `@(disp,PC)`, `mova`); the bit operations on `@(R0,GBR)`; `tas.b`;
`ldc`, `lds`, `stc`, `sts` and their `.l` forms for SR, GBR, VBR, MACH, MACL and PR; `trapa` and `rte`; and
every branch, taken and not taken, with its delay slot.

*Words, states, slots.* A form with at most 512 encodings runs on all of them (so every Rn/Rm aliasing); a
larger one on the extremes of each field (all zero, all one, the sign bit, the bit below it) and a random
fill, 512 in all. PC-relative forms are placed at both alignments. Each case draws a cell and then a random
state: every register from a pool biased to carry and saturation values; SR within the bits that exist; the
registers an instruction addresses memory through, aligned to its access size, from RAM (55%), the bottom of
the address space (so a pre-decrement wraps below zero), the top (so an increment, an index or a displacement
carries past 2^32), constants and anywhere; register-target branches to near, far, bottom and top addresses. A
delayed branch has 512 cells, each with its own slot, half drawn from a list of instructions that make a slot's
effect visible (`add #imm`, `sett`, `clrt`, `movt`, `sts pr`, `lds pr`, a store, the PC-relative loads and
`mova`, `cmp/eq`, `ldc sr`, a post-increment load) and half from any non-branch op; a slot run twice or not at
all changes a register or a flag, so the result differs.

*Named shapes.* Eighteen fixed cases (registers and stack written out) pin the findings and their controls:
`ldc` to SR, `rte` with a slot that sets, clears or reads T, and a PC-relative load in the slot of each
delayed branch, taken and not taken.

*Result.* 114 of the 130 forms agree with Mednafen on all 2,000 cases: every ALU, shift, compare, extension,
swap, multiply, MAC and DIV form, every `mov` form at every addressing mode, the bit operations, `tas.b`,
`trapa`, `lds`/`stc`/`sts` and their `.l` forms, and `bt`/`bf` in full (displacement sign, wrap at 0). The 16
that differ do so for the reasons in the ledger:

| Form | Cases that differ (of 2,000) | Why |
|---|---|---|
| `ldc Rn,SR/GBR/VBR`, `ldc.l @Rn+,...` | 610, 657 | A2-1: SR mask |
| `rte` | 1,159 | A2-2 timing, A2-3 mask, A2-4 slot PC |
| `bt/s`, `bf/s` (RAM, address 0) | 234, 192, 190, 165 | A2-4 slot PC; A2-1 through an `ldc` slot |
| `braf`, `bsrf`, `jmp`, `jsr`, `rts` | 283, 264, 353, 353, 270 | A2-4; A2-1 through an `ldc` slot |
| `bra`, `bsr` (RAM, address 0) | 76, 41, 58, 97 | A2-1 only, through an `ldc` slot; the slot PC is right (patch 0005) |

Each failing case is tagged with what its slot was (`slot PC-relative`, `slot ldc to SR`, `slot other`), so a
cause shows as a count: when A2-1 and A2-4 are fixed `bra`, `bsr` and the SR rows drop to zero, and `rte` is
left with A2-2 and A2-3. No failure falls outside the causes in the ledger.

### 3. Translation depends on history (A2-5)

A stray `next` difference (1 case in 60,000, `bt/s` at address 0) showed that the same bytes could translate
to different p-code. `TestTranslationHistory` pins it for every delayed cell: translate the slot, the branch,
one other instruction, the branch again, and compare the two translations of the branch. All 5,632 delayed
cells (every delayed form) differ: the second translation is the slot's p-code alone (`rts` 4 ops, then 1;
`bt/s` 7, then 3). Cause: `Sleigh`'s cache of parse trees has two entries when a language's delay slots are one
instruction (`sleigh.cc:576-582`), a lookup that hits does not advance the ring, and the delay-slot lookup that
follows a miss takes the entry holding the branch's own tree. Whether decompiler flow reaches the sequence (the
slot as a jump target, then the branch translated twice) is not established.

### 4. How far to trust it

*Mutation check.* 20 single-line changes to the reference (addv's overflow term, MAC.L's saturation direction,
the `@Rm+` write order, a displacement scale, a sign extension, negc's T, the sign of `bf`'s and `bra`'s
displacement, `bsr`'s PR, the PC a slot sees, `tas.b`'s written value, `@Rm+`'s step, the `@-Rn` alias, `xor.b`,
a store address, `bt`'s condition, `dt`'s T, DIV1's borrow, `swap.b`, `mova`'s alignment, `trapa`'s pushed PC)
were each caught, in the form that was changed. The mutations are not in the repository; they are cheap to
repeat by editing `ExecRef`.

*Not modelled, so not seen (A2-8).* Address errors for misaligned accesses and odd jump targets, illegal
instruction and illegal slot exceptions, `sleep` (a user op), `tas.b`'s bus lock, interrupts and their
one-instruction delay after `ldc`/`lds`, code at the top of the address space, and the bus: the reference
memory is flat. Every case uses aligned addresses and even jump targets, and a delay slot never holds a
branch. The decoder's treatment of a PC-relative operand after a delayed branch (`Sh2DecodeAfterBranch`) is not
part of the comparison; `Sh2OperandHoverTests` and `Sh2FunctionFinderTests` cover it.

*Build and run.* `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build --parallel 2
--target SaturnExplorerSh2DecodeTests SaturnExplorerSh2SemanticsTests`, then
`build/bin/SaturnExplorerSh2DecodeTests <scratch dir>` (a non-zero exit while A2-1 to A2-7 stand).
`Sh2SemanticsTests` is unchanged and passes. CI's `-R` filters name the existing tests one by one and do not
select this one; adding it is the coordinator's call.

## Area 5, round 1: the dead-read class

Fixer, branch `review/5-dead-reads`. Ledger rows A5-1, A5-2 and A4-1, and the 31 and 34 reads
through a pointer that the conservation record showed `dead` in the game code, are one class: **a
load whose value's only consumers disappear in the same dead-code pass or just after it is removed
with them**, with no note and no entry in the access ledger. Patch 0018 (`0018-hold-every-load.patch`).

**Root cause.** `Architecture::keepUnusedLoads` (patches 0009, 0014) holds a load for its effect
through `ActionDeadCode::lastChanceLoad`, which sets `autolive_hold` on the load's output. The hold
was placed only on a load whose output the dead-code pass had *not reached* (`isConsumeVacuous` is
set by any `pushConsumed`, whatever the mask). Two ways a read still escaped it, both found by
stopping the engine at the destroy (`gdb` on `Funcdata::opDestroy` for the load, then a print in
`lastChanceLoad`):

1. **Used at the first pass, unused after a rule.** `mov.b @r1,r0 ; xor r0,r0`, `mov.l @r6,r3 ;
   cmp/eq r3,r3`, and the game code's byte copies into a local buffer. At the first pass the load
   is consumed (`consume=ff`, flag set), so it is not held. The rule pool then folds the use away
   (`RuleTrivialArith`: `x ^ x` is 0, `x == x` is true; a stack store the next write covers dies a
   pass later), the load's output has no descendant and no hold, and `RuleEarlyRemoval` destroys it
   (a break there shows exactly that). `lastChanceLoad` runs again at every pass (patch 0014), but
   the load is gone before the next one.
2. **Reached, consumed by nothing.** `mov.l @r4,r0 ; shll16 r0 ; shll16 r0`, an `and` with 0: the
   mask that reaches the load is 0, the flag is set, so it was skipped, and the final loop of
   `ActionDeadCode::apply` destroyed it (`neverConsumed`, which also puts a constant 0 where the
   load's pointer was read).

A third state of the same read is separate code: the reads of **A4-1** are not loads at the time.
`RuleLoadVarnode` makes a load of a constant address a COPY of the volatile Varnode, which
`ActionVarnodeProps` turns into the user op (`replaceVolatile`) only on the next pass of the main
loop, and `ActionConditionalExe` runs between: `ConditionalExecution::testRemovability` refuses a
LOAD, a STORE, a call and an INDIRECT but took this COPY for an ordinary op, and removed it with
the block it was in (the second of two tests of one condition; the block held nothing else). The
other half of the brief's hypothesis, a `volatile_read` CALLOTHER whose output goes unused, does not
lose the read: a CALLOTHER is a call (`isCall`), `RuleEarlyRemoval` and `ActionDeadCode` leave it,
and `opUnsetOutput` only drops its output.

**The patch.** About 12 functional lines in `coreaction.cc`, `condexe.cc`, `jumptable.cc`.
`lastChanceLoad`, under `keepUnusedLoads`, holds *every* load (`setAutoLiveHold`) and pushes it as
consumed when nothing consumes it (`getConsume() == 0`, which covers the unused and the reached-by-
mask-0 loads); the hold is kept while the output is a LOAD (0009), so a constant or volatile address
becomes a direct access or a user op and the existing rules decide. The one load not held is a
recovered switch's table lookup, which gave `foldInNormalization` a hold before it was recognised, so
it clears it. `testRemovability` refuses an op that reads a volatile Varnode. Without the push, a
load reached with mask 0 is held but its pointer is read as 0 (mutation M5, below).

**Fixtures** (`TestHardwareVolatility`, the ledger's moved to bases `0x0600A800` and up; the
code is PC-relative, so the words and literals are verbatim): `deadXor` (A5-1 a), `deadCmp` (b),
`deadMarked` (c, request volatile `0x26001030-0x2600103F`), `deadPtr` (d), `pollThenRead` (A5-2:
`SaidMissing` on `0x0600A90C`, engine record `Unreachable` there), `twoTests` (A4-1: ledger
`{ Rd(0x05F800DE, 2) }`, no `if` in the text) and `twoTestsWrite` (its control); mine: `deadAnd`
(`and #0,r0`, state 2), `deadShift` (`shll16` twice, state 2), `deadStack` (a read stored to the stack
and never used: **a control**, it was already held: the store dies at the first pass). Ledger c's
text gives `Rd(0x26001034, 2)`; the engine reports the literal as written, `0x06001034`, which the
fixture asserts.

**Oracles.** (1) `DeadIsLoss(AccessFate, marked)`: a record of fate `Dead` is a loss unless it is a
direct access of ordinary memory (constant address, not hardware, not marked). `ReferenceOk`
applies it to the shown run's own records, so it fails whether or not the reference run lists the
access (the reference run is all-volatile and lost the same reads, which is why the conservation
law was blind to the class); `TestGameCode` counts it (`deadLost`) and `CHECK(deadLost == 0)`.
Every fixture that goes through `run` or `ReferenceOk` is now covered, not only the new ones. (2)
The fixtures above. (3) `SaturnExplorerSh2GeneratedTests` passes unfenced and its `WILL_FAIL` is
gone from `CMakeLists.txt`.

**Mutation** (tests kept, engine reverted; each rebuilt and run: fixtures, both save states, the
generator's 300 seeds):

| Revert | Fixtures | Game code | Generator (300) |
|---|---|---|---|
| all of 0018 | 24 failures: `deadXor`, `deadCmp`, `deadAnd`, `deadMarked`, `deadPtr`, `deadShift`, `pollThenRead`, `twoTests`, each by its ledger, by `FateAt(Dead)` and by `ReferenceOk` (the new conservation clause: `the read ... ended dead, and is no direct access of ordinary memory`); `twoTestsWrite` and `deadStack` pass | `deadLost` = 31 and 34, `CHECK` fails; the reference run still lists **0 dropped** (the old oracle is blind to it, as the ledger said) | 2 failing seeds, 2 findings (seeds 51 and 65, A5-1) |
| the `condexe.cc` guard only | 3 failures, `twoTests` | none | none (A4-1 needs the folds-fenced 20,000-seed run, seed 7826) |
| the hold (`coreaction.cc`, `jumptable.cc`) only | 21 failures, every fixture but `twoTests` | 31 and 34 again | 2 findings |
| M4: the original test (a load reached by the pass is skipped), the rest held | the xor, cmp, `and`, marked, pointer and `pollThenRead` fixtures | 4 dead reads in `0x060228B4` | not run |
| M5: every load held, pushed only when the pass did not reach it | passes every fixture that existed then; the `deadShift` words fail in the generator | | |

(M4 and M5 are what separated the two states. A first M5, hold with no push at all, failed 15
existing fixtures, the unused reads of 0009 among them: the hold alone does not keep a load in the
pass that sets it.) For the full revert the checks that caught it: the new fixtures (by ledger,
by `FateAt` and by `ReferenceOk`'s new clause), the game-corpus `deadLost` check, and the generator.

**Matrix**, `RelWithDebInfo`, gcc, 4 cores shared with other sessions:

| | |
|---|---|
| `ctest` (92 tests) | 92 of 92 pass, 124 s (`DecompilerRunnerTests` 73 s); `SaturnExplorerSh2GeneratedTests` 4.4 s, no `WILL_FAIL` |
| Game code, 839 prologues / 1,554 | 839/839 and 1,554/1,554; every count 0; 16 and 20 missing and said (unchanged); records: dead 41 -> **0** and 60 -> **0** (reads through a pointer 31 -> 0 and 34 -> 0; reads of constant ordinary addresses 10 -> 0 and 26 -> 0: held until the address folds, then recorded as folded); reference run: printed 5,038 -> 5,068 and 18,855 -> 18,893; `deadLost` 0 |
| Outputs changed against the previous master (`559bdea`) | 14 of 839 and 17 of 1,554: reads that were dropped are now printed (`xVar2 = *unaff_r10;`, byte copies into a local buffer: all six reads of `0x060228B4` print where four of six did), and the variable numbering moves with them |
| Time (previous master vs this branch, back to back, three rounds each, load 1.3 to 2.4) | image 1: 5,587 / 5,287 / 5,253 ms -> 5,524 / 5,081 / 5,078 ms; image 2: 20,509 / 20,996 / 20,633 -> 20,171 / 20,091 / 19,825 ms. No measurable change (about -3% on both, inside the noise of a shared machine) |
| Vendored tree (the CI `vendored-tree` job, locally) | sparse clone at `918d44e`, `vendor.py` in a copy: applies 0001 to 0018, `diff -r` against the committed tree is empty |
| Patch applied to the pre-edit copies | `git apply --check`, then `git apply`, to copies of `condexe.cc`, `coreaction.cc`, `jumptable.cc` as committed at `559bdea`: clean, and the results are byte-identical to the committed files |
| gcc ASan + UBSan (`-fsanitize=address,undefined`, Debug), `ASAN_OPTIONS=detect_leaks=1`, `Sh2DecompilerTests` | fixtures (with the new ones) and both save states (62.8 s and 246.9 s of decompiling): no ASan error, no leak; UBSan reports only the two known sites, `address.hh:605` and `slghpatexpress.cc:979` |

**Generator** (`SaturnExplorerSh2GeneratedTests`, RelWithDebInfo; before: 2 failing seeds of 300, 40
of 3,000, 12 of 20,000 folds-fenced):

| Run | Seeds | Result |
|---|---|---|
| default | 300 | 0 findings, 4.5 s |
| unfenced | 3,000 | 0 findings, 42.7 s, 29,509 accesses traced |
| `SE_GENERATED_FENCE=folds` | 20,000 | 0 findings, 271 s (A4-1 and the rest of what the fence kept out are in range) |
| unfenced, `SE_GENERATED_FIRST=20001` | 20,000 | 0 findings, 278 s, 195,980 accesses traced |

No finding of any kind in these runs; the `selfops` and `folds` fences are no longer needed to get
a clean run. The grammar's reach is the limit now (see A11-1).

**Remaining.** Ledger rows A5-1, A5-2 and A4-1 are `fixed 28908ee`. One new row, **A11-1** (open,
P3): the generator never touches r15, so the shape the game code showed was out of its reach, and
the grammar cannot be widened until its ledger treats the function's own frame as folded. Nothing
else open from this round. Not done: MSVC and macOS builds; the CI sanitizer job (the same
commands were run locally); the generator under ASan; `deadStack` does not separate the patch from
its absence (kept as the control, with the game code's own functions as the real check); the push
for a load reached with mask 0 is exercised by `deadShift` only.
