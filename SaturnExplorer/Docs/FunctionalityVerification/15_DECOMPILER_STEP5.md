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

*Round 1 of the fixes ("Area 2, round 1: SR, rte and the parse-tree ring", below) fixed A2-1, A2-2, A2-3, A2-5, A2-6 and A2-7 and removed the `WILL_FAIL` fence described here; A2-4 is now a reported known limit.*

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

## Area 2, round 1: SR, rte and the parse-tree ring

Branch `review/2-semantics-fix`, fixes in `4d37623` (patches 0019 and 0020, `Sh2Disasm`, the oracle). Ledger rows
A2-1, A2-2, A2-3, A2-5, A2-6 and A2-7 are `fixed 4d37623`; A2-4 (the PC a slot reads after a taken branch other
than `bra`/`bsr`) and A2-8 were not part of this round. `SaturnExplorerSh2DecodeTests` passes with no fence:
`WILL_FAIL` is removed from its ctest entry (the generated test's property is another agent's).

### 1. The spec: patch 0019 (A2-1, A2-2, A2-3)

All three are in `superh.sinc` and follow Mednafen (`sh7095_ops.inc`: `LDC`/`LDC_L` end with `z->SR &= 0x3F3`; `RTE`
reads the PC and the SR off the stack, sets `z->SR = new_SR & 0x3F3`, and only then calls `DelayBranch`).

| Row | Spec before | Spec now |
|---|---|---|
| A2-1 | `ldc Rm,SR`, `ldc.l @Rm+,SR`: `& 0x0FFF0FFF` | `& 0x000003F3` (T, S, I3-I0, Q, M) |
| A2-3 | `rte`: stacked SR `& 0x000063F3` | `& 0x000003F3` (bits 13 and 14 are the SH-2A's `CS` and `BO`) |
| A2-2 | `rte`: `delayslot(1); pc = _pc; sr = _sr;` | `sr = _sr; delayslot(1); pc = _pc;` |

*Ordering.* Patch 0006 solved the same question for PR in `bsr`, `bsrf` and `jsr`: the register the hardware writes
before the slot (`pr = inst_start + 4`) is assigned before `delayslot(1)`, and what the branch does after the slot
(`call`, `goto`) stays after it. `rte` follows that: SR is assigned before the slot, `pc = _pc` and `return [pc]`
after. A slot that reads T (`movt`) sees the restored one, and a slot that writes T (`sett`, `clrt`) is not undone by
the restore. `ldc` to GBR and VBR are not masked, in Mednafen or here (the control shape agrees).

*Procedure* (`patches/README.md` and `VENDOR.md`): `superh.sinc` edited with the "Modified for SaturnExplorer" notice
extended (one more line, as 0006 did); `cmake --build . --target se-regen-sla` regenerated `sh-2.sla` and
`Sh2SpecData.h` (regenerating again from the same spec gives byte-identical files); the patch is a `git diff` with
upstream paths (`a/Ghidra/Processors/SuperH/data/languages/superh.sinc`) between a copy of the file taken before the
edit and the edited one, proved with `git apply --check` and `git apply` on that copy under
`GIT_CEILING_DIRECTORIES` (the result is byte-identical to the working copy); and `python3 -I vendor.py` on the
pinned Ghidra checkout (`918d44e`) applied all 19 patches and rewrote the vendored tree with no difference from the
working copy. Rows added to `patches/README.md` and `VENDOR.md`; `SaturnExplorerSlaFreshnessTests` passes.

### 2. `Sh2Disasm` (A2-6, A2-7)

`bt/s` and `bf/s` are spelled as Renesas and the SLEIGH listing spell them (`Sh2Disasm.cpp`); `Sh2Operands.cpp`
matched `"bt.s"` to word its "if T set" comment and now matches `"bt/s"`. Nothing else in `FrontEnd`, `Core`, `web`
or the docs depends on the old spelling (`grep -i 'bt\.s\|bf\.s'` over the tree: the header comments and one test
comment, all updated; no golden listing holds it; `Sh2AccessWidth` reads the character before the last one, and
`/s` is no different from `.s` there). `trapa` is now `IsBranch` as well as `IsCall`. Its consumers: `Sh2FunctionFinder`
tests `IsCall` before `IsBranch` in the reachability walk and excludes calls in the scan that reads `IsBranch` for
tail jumps, and `Sh2Decompiler.cpp` (the body ranges) excludes `IsCall`; so the only effects are that a `trapa` between a literal load
and its use ends `LiteralInRegister`'s backward search (it reads "a branch or call in between" in its own comment) and
that a `trapa` in a delay slot makes the `rts` before it data (a branch cannot be a slot).

### 3. The oracle after the fixes

- `WILL_FAIL` is gone from `SaturnExplorerSh2DecodeTests`; the default run takes 1.2 s (RelWithDebInfo).
- **A2-4 is a known limit, not a failure.** After a taken `bt/s`, `bf/s`, `jmp`, `jsr`, `rts`, `braf`, `bsrf` or `rte`
  the spec places a PC-relative slot's literal from the slot's own address + 4, the hardware from the target + 2, and a
  register-target branch cannot be fixed in SLEIGH. The decompiler marks such a function unsupported
  (`ScanRawPcode`), so the row stays as it was. For the test to pass without hiding anything, a case that differs from
  the hardware reference is run once more against a reference with that single rule (`RunRef(..., specSlotPc)`); it is
  counted as a known limit only if every register, the next PC and every memory word then agree exactly, so any other
  difference in the same case still fails. Over the default run 1,780 of 260,000 cases (10 forms, 8 of 18 shapes) are
  known and 0 differ. `TestKnownLimitIsFlagged` decompiles each of the 8 shapes from its branch and requires
  `unsupported`: if the host stops marking them, the test fails.
- `TestRecycledTreeInDecompiler` is the decompiler-level fixture for A2-5 (section 5).
- Longer run: `SE_DECODE_CASES=30000 SE_DECODE_SEED=77` (3,900,000 cases, 9.7 s) with `SE_MEDNAFEN_SS` set: the
  transcribed word table equals `sh7095_idecodetab.inc` on all 65,536 words, 0 cases differ, 28,341 are the known limit.

### 4. Mutation

Each fix was reverted alone and the oracle run (the spec mutations regenerate `sh-2.sla` with `se-regen-sla`):

| Reverted | Result |
|---|---|
| all three spec hunks | 6 shapes differ (`ldc`, `ldc.l`, `rte; nop`, `rte; sett`, `rte; clrt`, `rte; movt`); `ldc` 610 and `ldc.l` 657 of 2,000 cases, `rte` 1,015 |
| A2-1 only (`ldc` masks back to `0x0FFF0FFF`) | the `ldc` and `ldc.l` shapes differ; 610 and 657 of 2,000 cases |
| A2-2 only (`sr = _sr` after the slot) | `rte; sett`, `rte; clrt`, `rte; movt r2` differ; `rte` 371 of 2,000 |
| A2-3 only (`rte` mask `0x63F3`) | `rte; nop; stacked SR = FFFFFFFF` differs; `rte` 750 of 2,000 |
| patch 0020 (`sleigh.cc` as before) | `TestRecycledTreeInDecompiler` fails (signature withheld: "an indirect jump it does not resolve"); `TestTranslationHistory`: 5,632 of 5,632 cells differ |
| A2-6 (`"bt.s"`, `"bf.s"`) | decode comparison: 2,048 disagreements in 2 groups |
| A2-7 (`trapa` without `IsBranch`) | decode comparison: 1,024 disagreements in 1 group |

Every mutation is caught by the check the fix is pinned by, and each restoration passes.

### 5. A2-5: can the decompiler translate a delayed branch, its slot, another instruction, then the branch again?

**Yes, and it did.** Verdict: reachable, fixed as patch 0020 (`4d37623`).

*Where an instruction is translated.* The engine has one caller of `Translate::oneInstruction`, `FlowInfo::processInstruction`
(`flow.cc`), and `FlowInfo` translates each address once (`visited` stops a second visit), so within one flow the
sequence slot, branch, other, branch cannot happen; jump-table recovery clones the raw p-code (`Funcdata::truncatedFlow`)
and does not translate again, and the engine has no inline or injection users here. The host has two: `Disassemble`,
which prints a run of distinct addresses with `printAssembly`, and `ReturnCheck::Step` (`Sh2Decompiler.cpp`), the
signature check that walks the function's instructions again after the flow, in its own order (a worklist ordered by
address, an instruction stepped again whenever the state reaching it changed). `printAssembly` builds the disassembly
state and has no slot; the walk in `ReturnCheck` is the one that can ask twice.

*What the ring needs.* `DisassemblyCache` hands out the entry allocated longest ago on a miss; a hit left the order alone.
`oneInstruction(A)` asks for A and then A+2 (the slot). The tree of A is recycled when A is a hit, A+2 a miss, and A's
entry is the next to hand out. With two entries that needs A to be the older of the two last allocations, which happens
when A was allocated while A+2 was already cached (A+2 was the newest allocation: translated as an instruction of its
own, last in the flow) and one plain instruction was translated since. No legal program needs a branch in a delay slot
for this; it needs the slot to be a jump target, the function to start at the delayed branch, and a loop back to it.

*The function* (base `06009000`; this is `TestRecycledTreeInDecompiler`):

```
0000  A002   bra   Z          entry; its slot is the word at 0002
0002  7201   add   #1,r2     slot, and the target of 'bt S'
0004  412B   jmp   @r1       P: a return, r1 is PR on every path that reaches it
0006  0009   nop
0008  8BFA   bf    A         Z: loop back to the branch
000A  012A   sts   pr,r1
000C  89F9   bt    S         (the slot as an instruction of its own)
000E  89F9   bt    P
0010  000B   rts
0012  0009   nop
```

The flow's translations, in order: `0, 8, A, C, E, 10, 4, 2` (the slot `2` last, its run ends at the already-visited `4`).
`ReturnCheck` then steps `0` (the branch misses, its slot is a hit: the slot's tree is the newest), `8` (one plain
instruction, which takes the slot's entry), and `0` again: the branch is a hit, the slot a miss, the miss takes the
branch's entry (an instrumented build printed `RECYCLED 06009000`). The branch is then built from its slot's parse,
`add #1,r2` alone, so the walk goes on to `4` with r1 unchanged, finds `jmp @r1` with r1 not holding PR, and the
signature is withheld ("an indirect jump it does not resolve") for a function that returns to its caller on every path.
With the patch the same run translates `0, 8, 0, A, C, 2, 4, E, 10`, nothing is recycled and the signature is lent.

*What I could and could not show.* The observable effect is a conservative one, a lost signature, because what a
recycled step drops (the branch, the call, the return) is joined into states the first visit of the same instruction
already set; I did not find a way to a wrong lent signature, and do not claim there is none. The row was P1 by
the rule for a silent wrong result (no warning, and it first showed as a stray `next` difference in the random run);
a lost call, branch or return in the pass that decides prototypes is the kind of defect this program exists to remove,
so it is fixed rather than accepted. The coordinator may re-triage it.

*The fix* (`sleigh.cc`, 8 functional lines): a hit makes the entry the newest (the list is rotated), so the cache is
least recently used instead of oldest allocated. A larger ring is not a fix: a tree can always be the oldest, and the
sequence needs only more plain instructions in between; upstream sizes the ring at 2 when `maxdelayslotbytes` is 1 and 8
otherwise (`Sleigh::initialize`), relying on the promise in `getParserContext`'s own comment that n different addresses get
n different trees while n is at most the size, which a hit on the oldest entry breaks. An LRU order restores it for every size.

### 6. The matrix

`cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo`, `--parallel 2`, whole tree built, whole `ctest`:

| Check | Result |
|---|---|
| `ctest` | 93 of 93 passed in 125 s |
| `SaturnExplorerSlaFreshnessTests` | passed |
| `SaturnExplorerSh2SemanticsTests` | passed (0.3 s) |
| `SaturnExplorerSh2DecompilerTests` | passed (1.2 s; its fixtures exercise the spec) |
| `SaturnExplorerSh2DecodeTests` | passed (1.2 s), no fence; the generated test's `WILL_FAIL` is untouched and still fails as expected |
| game corpus, `hwram008.bin` | 1,554 functions, 1,554 ok; 0 not printed exactly once, 0 accesses of another width, 0 statements with two reads, 0 reads printed past another access, 0 printed as conditional, 0 out of order, 0 joined into a switch, 0 dropped; 20 missing and said (unreachable code) |
| game corpus, `hwram007.bin` | 839 functions, 839 ok; the same counts 0; 16 missing and said |
| `vendor.py` on the pinned checkout | 19 patches apply, the vendored tree is unchanged |

Not run: the sanitizer jobs and the timing comparison (the whole-program matrix at the coordinator).

### 7. Not done, and for the coordinator

- A2-4 stays open (a spec cannot place the slot's PC after a register-target branch; the host flags it, and the oracle
  now pins the flag). A2-8 is accepted as written.
- CI's `-R` filters name tests one by one and do not select `SaturnExplorerSh2DecodeTests`; adding it is a CI change.
- Patch 0018 (`coreaction.cc`, another agent) and these two add rows at the end of the same two tables
  (`patches/README.md`, `VENDOR.md`): expect a trivial textual conflict, resolved by keeping both.

## Area 5, round 2 review

Reviewer, branch `review/5-round2-review`, at `1c0e752` (patches 0001 to 0020). Findings only; no source or
test changed. Every fixture is in `Docs/Decompiler/REVIEW_LEDGER.md` (A5-3 to A5-8) with its words, current
and expected output and the oracle's verdict. Reproduced with `SaturnExplorerSh2GeneratedTests`
(`SE_GENERATED_WORDS`, `SE_GENERATED_VERBOSE=1`) and, for the signature, with `TestGameCode` on a 1 MiB image
holding the same words after a `sts.l pr,@-r15 ; nop` prologue (`SE_GAME_ONLY`), RelWithDebInfo, gcc.

**Not held.**

- **A5-5 (P2).** The removal caution and the conservation oracle find an access by (instruction, direction,
  width). An instruction with two accesses of one kind can hide one behind the other: `mac.l @r1+,@r2+` with
  SCU_IST and an ordinary RAM word as operands, after a poll that folds, leaves the records `R4 folded` and
  `R4 unreachable` on one instruction; `NoteRemovedAccesses` takes the fold for the whole key, says nothing,
  and lends the signature; `ReferenceOk` and `TestGameCode` count the read Folded. Same with a register read in
  the slot of an `rte` (the slot's access carries the `rte`'s address, beside its two folded stack pops). With
  both operands registers, or `rts` for `rte`, both are said and the signature is withheld. Narrow reach, no
  corpus instance; no oracle sees it.
- **A5-3 (P2).** No width rule: a byte write to a VDP2 or VDP1 register (Mednafen writes the whole 16-bit
  register, the other byte from the bus), a word or byte access to the 32-bit on-chip modules (Mednafen: a word
  write at FF04 sets all of DVDNT and divides, at FF06 nothing; a word read returns the low half at either
  address; a byte access is an address error) print as a part of the register (`VDP2_TVMD._1_1_ = 3;`,
  `SH2_DVDNT._2_2_ = 3;`) with no note. No oracle models register widths.
- **A5-4 (P2).** SMPC registers are decoded on the low byte lane: a 16-bit write at 0x2010001E is an
  SMPC_COMREG write (an SMPC command) in Mednafen; the output says `Ram2010001e = 0x19;`. The same write in
  removed code is said as "the write of SMPC_COMREG". An even-address byte write does nothing; a long at
  0x2010001C is two register writes.
- **A5-6 (P3).** Both reads of a `mac` are flagged `repeatedOnPath`: the generator fails on any mac
  (`SE_GENERATED_WORDS="045F 000B 0009 0009"`, `n:repeated` twice) and `Ledger` cannot hold a mac fixture.
- **A5-7 (P3).** The removal caution names the first register only and no width ("the write of VDP2_TVMD" for a
  4-byte write over TVMD and EXTEN).
- **A5-8 (P3).** 0x00300000-0x003FFFFF is treated as a repeat of low work RAM; Mednafen (VA0/VA1) maps nothing
  there (reads 0xFFFF). Ordinary memory either way; a candidate for accepted.

**Held**, one line each:

1. Conservation, opcode changes: in the decompile group only `RuleLoadVarnode`/`RuleStoreVarnode` turn a flow
   LOAD or STORE into something else. The other rules that touch one (`RuleSplitLoad`/`Store`, `RuleDoubleLoad`,
   `RuleDoubleStore` -- grouped `doubleprecis`, named `doublestore` --, `RuleExpandLoad`, `RuleStringStore`)
   are disabled by name, and no output of either save state carries the "not found" warning;
   `RuleStructOffset0` changes the pointer only; `SplitDatatype`'s load and store paths are reached only from
   the disabled rules; `LaneDivide` and `PreferSplitManager` need laned registers or split records the SH-2
   spec does not have.
2. Accesses the engine makes after flow (a call's stack parameters through `opStackLoad`) get no record
   (`time >= flowOps`); the user-op exception covers only `replaceVolatile`'s ops, made from flow accesses.
3. Copied blocks: `nodeSplit`'s one caller is `ActionReturnSplit`, whose `isSplittable` admits only
   COPY, RETURN and MULTIEQUAL: no access is ever copied.
4. Flow's silent removals: `deleteRemainingOps` drops ops after a BRANCH, BRANCHIND or RETURN inside one
   instruction (no SH-2 instruction has a memory op after its branch: the slot comes first), and the removal
   after inlining needs inlining, which SE never asks for. Neither holds an access.
5. Restarts: `Funcdata::clear` clears the records and `PcodeOpBank::clear` resets op numbering, and the
   records are made again. A question, not a finding: `RecordFlow` is taken after the first `followFlow`,
   so after a restart whose flow differs (a multistage jump table) `origBlock`/`origOrder` describe ops of a
   flow that no longer exists; only the ordering oracles read them. Production always runs the first pass,
   whose literals let the printed pass recover a literal-bounded table at flow time, the usual cause of such a
   restart; a restart is not visible from outside the engine, so none was observed either way.
6. Patch 0018 on stray reads: none found. Stack loads fold (spacebase), literal and constant ordinary reads
   fold and go; dead reads through a MULTIEQUAL, in the slots of `bt/s` and `bra`, in a counted loop, after
   `x ^ x`, print once where made. Still lost: none found.
7. Patch 0018's condexe guard: `testRemovability` also refuses an op writing an address-tied Varnode (a
   STORE made a COPY), so writes were never at risk there.
8. The seeded reference run: same literals and callees, so the same flow; all-volatile does not stop table
   recovery (the table read is a LOAD at a non-constant address, which no volatility touches).
9. Removed accesses through a hardware base plus an index are said ("the write through a pointer"); user marks
   reach cache-through and every 1 MiB repeat, LWRAM included.
10. Width and count: `tas.b`, `and.b`/`or.b`/`tst.b #imm,@(r0,gbr)` and `mov.l @(disp,gbr)` with GBR loaded
    from a literal print at their width, named, read before write; a GBR poll of SMPC_SF stays a loop with the
    read in it. `mac.l`/`mac.w` read Rm first, as Mednafen (`MAC_L`, "Order confirmed").
11. The map against Mednafen, re-derived from `ss.c`, `vdp1.c`, `vdp2.c`, `scu.inc` and `sh7095.inc`: VDP2
    `A & 0x1FE`; the CD block where `(A & 0x7FFF) < 0x1000`, `(A & 0x3F) >> 2`; SCU byte writes masked
    (`SCU_RegRW_DB_u8_W1`); partition 4 is the cache data array like 6 (ordinary memory); `SH2_BCR1._2_2_`
    (word reads at 0xFFFFFFE2, 2 functions per save state) is right. Differences: A5-3, A5-4, A5-8.

**Runs.**

| | |
|---|---|
| Generator, unfenced, `SE_GENERATED_FIRST=40001`, 20,000 seeds | 0 findings, 196,860 accesses traced, 291 s |
| Game corpus, `hwram007.bin` | 839/839; every count 0; 16 missing and said; records: 14,842 folded, 0 dead, 16 unreachable, 27 switch lookups |
| Game corpus, `hwram008.bin` | 1,554/1,554; every count 0; 20 missing and said; records: 38,700 folded, 0 dead, 20 unreachable, 155 switch lookups |

**For the coordinator.** The merge of `review/2-semantics-fix` (`1c0e752`) put ledger rows A5-1, A5-2 and A4-1
back to `open` with their original fixture text; at `7fbbd1c` they read `fixed 28908ee` with the suite's fixture
names. The record above ("Area 5, round 1") still says fixed. This review did not touch those rows.

## Area 1, oracle: the invalidation model (`review/1-invalidation`)

`SaturnExplorerDecompilerModelTests` (`FrontEnd/tests/DecompilerModelTests.cpp`, one `ctest` entry of the
same name) is the oracle of area 1 of `Docs/Decompiler/REVIEW_PROGRAM.md`: snapshot, cache and
engine-input invalidation. A seeded RNG drives a sequence of 20 to 60 operations against the real
panel, runner and engine; after each step, once the runner is idle, and for every result as it is
installed, the shown result is compared with a from-scratch decompilation of the inputs of that moment.
A failure prints the seed, the operations so far, the inputs, and the expected and actual key, text and
stale state. It found three things, none in the snapshot key or the panel's install rule (below).

**Level: the panel, with the real runner and engine behind it.** `DecompilerPanel::Update` and `Draw`
run under `ImGuiHarness` exactly as in `DecompilerPanelTests`; Refresh, Stop and the entry box are
clicked and typed, everything else goes through the panel's and runner's public interface (`Shown`,
`ShownStale`, `HeldKey`, `Pending`, `Submissions`; `InvalidateSession` and `ResetSession` as
`App::ResetSessionDebugState` calls them). A runner-only model is not enough for the invariant: the
learned prototypes lent to callers, the LRU, the held snapshot, the refresh policy and the stale mark
live in the panel, and only the panel can show "a result for superseded inputs was installed" or "a
stale result carries no mark". The runner's side (generation and epoch filtering, the too-large memo,
cancel) is exercised through the panel. What the harness stands in for is `App`: it builds the
`DecompilerView` the way `App::BuildDecompilerView` does (source id, derive serial, frame, a recorded
frame's stable identity) and keeps the promise `App` makes, that a key names one set of bytes.

**The world.** A 1 MiB work RAM image with 3 to 5 functions of seven (built from Fixture 1's caller and
plain loads and stores, each after an `rts ; nop`): A reads `*r4` and returns it plus 2; B stores r4
through r5; P writes 3 to a mailbox and polls it (kept only if the mailbox is volatile; otherwise the
loop is endless as analysed and the `*r5` read after it is gone, so its signature differs); Q stores r4
in a mailbox and reads it back; C calls A, B, P or Q through a literal pool word; D calls C; E calls a
function at 0x700 in the BIOS image. Each function has "sites": bytes an edit may rewrite (a register
number, a constant, the callee's address, the mailbox), every value giving a valid function whose
signature or callee may change. Three images (builds) of the same code, three recorded frames per
recording, two BIOS images, four volatile lists, four names, user function starts at each entry and four
bytes in.

**The operations** (state-independent, so a sequence can be cut): edit a byte (a callee's signature, a
caller's callee, a constant); scrub to a recorded frame and back; Refresh; reconnect (same bytes, new
source, session ended: `InvalidateSession` and `ResetSession`); replace the source (another image, as a
dump, paused or running, with a frame of no source between); BIOS none, A, B (a new object each time:
a reload of the same bytes must not look like a change); the volatile list; a function's name; a
function start; select a function (typed into the entry box); Stop; hide and show the panel; the
emulator runs, pauses, advances a frame; the CPU. A step may *hold* the engine (a gate on the runner's
phase observer), so that several changes land with a request in flight; *finish* then lets the engine
complete everything and deliver nothing, and the next operation changes an input before the panel
collects the answers (the case the panel's own install checks exist for). *Starve* (seeds divisible by
six, with a one-second budget) refreshes the shown function and holds the engine past the budget, so
the runner answers "too large". A library of short motifs aims at the dependencies: a callee shown, its
caller, a change, the caller again; the volatile list changing what P's signature is; a chain D, C, A; a
BIOS function and another image; a result finishing and then a rename, a start, a volatile list, an
edit, a scrub, a BIOS image or a reconnect landing before the panel looks.

**The model.** `Oracle::Run` is a from-scratch `Sh2Decompiler::Decompile`, on the test thread while the
runner is idle (the worker is blocked on its condition variable; asserted by `Busy()`), of a snapshot the
test captures itself, with the request the panel would build: names, function starts, the parsed
volatile list, the entry and the lent prototypes. `Oracle::Full(callee)` is the prototype a callee lends
when its own callees are lent theirs, recursively, or none if its decompilation does not vouch for
one. The panel lends what it has learned in this session for these inputs, which is any subset of
the current ones, so the answers a shown caller may have are the 2^k from-scratch runs lending each
subset; a prototype from a previous revision of a callee, or learned under another volatile list or
function set, matches none unless it happens to give the same text. The model also mirrors the one
policy the panel states in its header: the snapshot a running emulator's panel holds lags until
Refresh, a selection, a new source or a BIOS change (everywhere else it follows the view at once).

**What is checked.**

1. For every result as it is installed (found by watching `Shown()` each frame): it is installed only
   while the snapshot held is what is on screen (a hidden panel must not install an answer for memory
   that has moved); its key is the key of the inputs it was asked under, which are the inputs of the
   frame it was installed in (so a superseded request is never installed); its entry, status, text,
   warnings, callees and prototype equal one of the model's candidates. The runner's "too large" is
   accepted for the request that was held past the budget, and for its memo while the function's bytes
   and its callees' are unchanged and nothing has since decompiled it.
2. At each step's end, with the runner idle: no request is outstanding; the snapshot the panel holds is
   the one the refresh policy says (`HeldKey`); a result is shown; the stale mark equals the model's
   (inputs shown differ from inputs now) in both directions, except that a running emulator's lag is
   stale by design and a question the user Stopped stays stale until Refresh; a result that is not
   stale is exactly a candidate answer for the inputs now.
3. With the engine held, after the panel has had frames: a shown result made from other inputs than the
   ones now says stale (so a missed invalidation shows even where nothing is asked yet).
4. Refresh with a function selected asks the runner (its force flag survives); a request cancelled by
   Stop while the engine held it installs nothing.
5. At the end of a sequence a callee-first walk through every function (with Refresh where a Stop left
   the question "asked already") leaves each one showing exactly the from-scratch answer with every
   callee prototype lent.

**Runs.** `ctest -R DecompilerModel`: 18 seeds, then the shrunk sequence of each open finding of the
ledger (`kPinned`); `WILL_FAIL TRUE` while A1-1 and A1-2 stand. `SE_MODEL_SEEDS=<n>` runs seeds 1 to n
(a long run; per-seed lines only below 60); `SE_MODEL_SEED=<k>` runs one seed with every step printed
(`SE_MODEL_TEXT=1` adds the shown text; `SE_MODEL_LIST=1` prints the sequence without running it;
`SE_MODEL_SKIP=3,5` and `SE_MODEL_STEPS=<n>` cut it, which is how a failure is shrunk by hand);
`SE_MODEL_SCRIPT="select A; starve; edit A 0 1" SE_MODEL_WORLD=A SE_MODEL_IMAGE=0` runs a sequence
written out (the words are in `ParseScript`); `SE_MODEL_NOSTARVE=1` leaves the budget operations out.

**Default run** (`ctest -R DecompilerModel`, RelWithDebInfo, a 4-core container shared with other
work): 18 seeds and the two pinned sequences, 12.2 s. 738 operations (105 of them without effect: a
Stop with nothing outstanding, a selection while the panel is hidden), 83 held steps, 42
finish-then-change races, 4 stops, 5 budget stalls; 422 results checked at install, 733 idle checks,
239 held-engine stale checks, 72 callee-first walks; the panel made 193 captures and 512 submissions,
discarded 23 answers and served 25 from its cache; the model ran the engine 480 times (2,857 memo
hits). Every random seed passes; the two pinned sequences fail, so the binary exits 1 and `ctest`
reports Passed through `WILL_FAIL`.

**Long run** (`SE_MODEL_SEEDS=1500`, 937 s): 58,208 operations, 33,659 results checked at install,
58,047 idle checks, 19,032 held-engine stale checks, 6,400 walks, 4,109 finish-then-change races,
321 stops, 156 budget stalls, 39,664 submissions, 1,968 discarded answers, 1,840 cache hits. 43 seeds
fail, every one of them a seed divisible by six (the ones with a budget stall) and every one a "TOO
LARGE" failure (A1-1 or A1-2); the other 1,250 seeds are clean.

**Mutation check.** The model is only as good as what it catches. 31 hand-made breaks of the code under
test (one expression each, in `DecompilerPanel.h/.cpp` and `DecompilerRunner.cpp`; reverted after each),
100 seeds apiece with the budget operations off:

| Break | Seeds that fail (of 100) | First check that fires |
|---|---|---|
| a lent prototype need not be of the snapshot held | 8 | text |
| ...nor of the function set and volatile list it was learned under | 3 | text |
| ...the lending id leaves out the volatile list | 2 | text |
| stale mark ignores the function set / the volatile list | 1 / 4 | stale shown as current |
| answer accepted without the names / function set / volatile list it was asked under | 5 / 2 / 5 | text, stale forever |
| answer accepted for a snapshot the panel no longer holds | 34 | key |
| answer accepted while the held snapshot is not on screen (a hidden panel) | 7 | on screen |
| user function starts left out of the function set | 66 | stale shown as current |
| volatile list hashed to nothing / left out of the question | 57 / 70 | stale shown as current, not refreshed |
| deriveSerial left out of the view's identity (an edit is not a change) | 94 | held |
| a running emulator ignores a BIOS change / a source change | 18 / 7 | held |
| the cache ignores prototypes lent / names / function set / volatile list / key | 26 / 13 / 64 / 55 / 96 | converged, not refreshed |
| Stop does not cancel | 11 | stop |
| Refresh does not force a new question | 76 | refresh |
| the stale mark ignores the names generation | 0 | |
| answer accepted without the lent-prototype hash | 0 | |
| answer accepted from another runner epoch / the runner's epoch not bumped / the runner's latest-generation filter removed | 0 / 0 / 0 | |
| `ResetSession` keeps the learned prototypes / the cache | 0 / 0 | |
| the cascade that drops prototypes learned from an obsolete one | 0 | |

23 of the 31 are caught. The eight survivors are layers behind other layers, which is how the code is
built ("the panel checks all of them itself, because the runner cannot know..."): the names generation
in the stale mark is shadowed by the outstanding request; the lent-prototype hash in `Accept` cannot
move without the target moving, which supersedes the request; the epoch is checked by the runner, by
`ResetSession` (pending cleared) and by `Accept`; learned prototypes and the cache carry the snapshot
key, which a session change moves; the cascade needs a callee's signature to change under unchanged
inputs. A break in one such layer is invisible unless another is broken too, and the harness cannot
break two things at once. They are listed, not hidden.

**What it found.** Three things, in `Docs/Decompiler/REVIEW_LEDGER.md`:

- **A1-1 (P2)** and **A1-2 (P2)**: the runner's too-large memo (`mTooLarge`, keyed by the entry as
  asked and the volatile list, cleared only by `InvalidateSession`) outlives the bytes it was reached
  for and a successful retry. A function that ran out of budget is answered "too large ... (not
  retried automatically; use Refresh)" for the bytes edited since (its own, a callee's, another
  recorded frame's), carrying the edited snapshot's key; and after a Refresh has decompiled it, any
  later request that is not a Refresh (a rename, an edit) is answered the same way without running
  the engine. `select A; starve; edit A 0 1` and `select A; starve; refresh; name A 1`, world A; the
  first through a callee as `select D; starve; edit C 2 1`, world D,C,A, and through a scrub as
  `select A; starve; scrub 1`, world A, image 1.
- **A1-3 (P3)**: after Stop, choosing the same function again (typing its entry) asks nothing: the
  question is "asked already" until Refresh, and the old function stays on screen stale. It is the
  rule `Accept` documents, so the model excuses a stale result after a Stop; recorded for the fixer
  to accept or change.

Nothing was found in the snapshot key, the install rule, the cache keys, the lent-prototype
provenance, the refresh policy for a running emulator, or the stale mark: 1,250 seeds without a
budget operation, and the break table above, say they were exercised.

**Not covered.** The function finder (Follow PC, "Decompile" from Assembly, call-stack hints,
selecting by an address inside a function; the model types an entry, so the finder's rule that a user
start is never overridden does not enter), auto-refresh and its debounce, registers and the halt hint,
mirror entries (0x26xxxxxx), low work RAM and the Sh2Dump regions, the user-note overlay, BIOS file
loading, the worker's failure paths (allocation, thread, cancel acknowledgement: area 6), and `App`
itself: the harness builds `DecompilerView` the way `App::BuildDecompilerView` does, but a key that
names the wrong bytes in `App` (a scrub frame's identity, `se_derive_serial`) is invisible to it. The
model accepts any subset of the current callee prototypes as "lent" for a shown caller, so a
prototype that should have been lent and was not is caught only by the final walk, which follows the
documented Refresh after a Stop. The one-second budget is wall-clock: on a machine so slow that an
ordinary decompilation takes a tenth of a second, a budget seed could see a spurious "too large". No
production source was edited: the runner and panel are driven through their public interfaces and the
runner's tests-only phase observer.

## Area 1, round 1: the too-large memo

Branch `review/1-invalidation-fix`, fix in `f5958a2`. Ledger rows A1-1 and A1-2 are `fixed`; A1-3 is `fixed` (the
panel half of the rule, below). `SaturnExplorerDecompilerModelTests` passes its default run with `WILL_FAIL` removed
from its ctest entry; its pinned sequences stay in the default run as regression pins.

**Root cause (A1-1, A1-2).** The runner kept its record of requests that ran out of budget as a set of
(entry, volatile list) that only `InvalidateSession` emptied. It was a verdict about one set of bytes, names and
lent prototypes stored under a key that named none of them, and nothing erased it when a run completed. So an edit
to the function, a callee or the recorded frame, a rename, a function start or different lent prototypes was answered
"too large (not retried automatically; use Refresh)" without running the engine, with the *new* snapshot's key (it
read as a verdict on the bytes on screen); and after a Refresh had shown that the function fits, the next ordinary
ask was answered the same way.

**The fix (`DecompilerRunner.cpp/.h`).** The memo is now an honest cache. `MakeRequestKey` digests (two independent
64-bit lanes, 128 bits) everything a result depends on: the snapshot's key (source, derive serial, frame, region
set and BIOS contents), the entry as asked (a mirror is another question), the names with their text, the function
starts, the lent prototypes (callee address and the whole signature and call effects), the volatile list,
`registerCallees`, `maxInstructions` and `allMemoryVolatile`. `Submit` computes it before the lock (the names can be a
symbol file) and stores it in the `Job`; `mTooLarge` is a `std::set` of those keys. A run that ends on the budget
inserts its key (in the current epoch); a run of the same key that completes with any other status -- `Ok`, `NoCode`,
`Failed`, `Unavailable`, `Cancelled` -- erases it. `InvalidateSession` still clears it. `VolatileKey` is gone.
Consequence, deliberate: the same bytes under a new key (a live game's next frame, a scrub to a frame that happens
to hold the same code) are a new question and are attempted again, up to the budget. The runner cannot know the
bytes are the same without knowing what the function reads, and the old memo's "same code under a new key" answer
was exactly the case that answered an edit with a stale verdict. The panel only asks when its question changes, and
a running emulator's held snapshot does not move until Refresh, so a slow function is still not retried every frame.
A Refresh that is Stopped half way also erases the memo of that question (any status), so the next ask after it is
attempted, not answered from the verdict the interrupted run was about to renew.

**A1-3 (`DecompilerPanel.cpp/.h`), fixed rather than accepted.** The rule "the same question is not asked again until
Refresh" exists to stop the panel asking the same question every frame (a failure is shown, not retried; after Stop
nothing re-asks). It was never meant to refuse the user. Enter in the entry box now sets `mAskAgain`, taken once in
the next `Update`: it re-asks a question that has been asked before (a cache hit when the answer is cached, the
runner's memo for a too-large one, the engine otherwise). It is dropped while the very question is outstanding and
wanted (a second Enter would only cancel and restart it); a request the user has Stopped (`Outstanding::cancelled`)
does not count as outstanding, so Stop followed at once by Enter asks afresh. Nothing else changed: the panel still
never re-asks by itself, and `DecompilerPanelTests` `TestEntriesAndNoRetryLoops` (60 idle frames, no submissions)
passes unchanged. Not Enter: Follow PC, the context menu and the finder keep the rule, as before. User-visible:
`DECOMPILER_GUIDE.md` (the entry-box row and the "too large" row).

**The model.** `TooLargeMemo` is now the list of the signatures that were stalled (snapshot identity, entry, names,
starts, volatile list); a "too large" answer is excused only for a signature `Same` as one of them, and a later
non-too-large answer for the same signature removes it. The old byte comparison over the reachable functions (which
excused the same code under another key) is gone. The Stop excuse still stands for a question Stopped and then left
alone, but a `select` (the Enter in the box) clears it, and the callee-first walk, which used to click Refresh after
a Stop, now fails (`WALK`) if typing a function's entry does not show it. `kPinned` holds five sequences, all in the
default run: A1-1 `select A; starve; edit A 0 1`; A1-1 through a callee `select D; starve; edit C 2 1` (world D,C,A) and
through a scrub `select A; starve; scrub 1` (world A, image 1); A1-2 `select A; starve; refresh; name A 1`; A1-3
`hold; select A; stop; wait; select A` (world C,A). The first four fail with TOO LARGE
on the runner before the fix, and A1-3 fails (NOT REFRESHED: the panel still shows C, stale) on the panel without
`mAskAgain` (the mutations below); all five pass after. The three A1-1 and the A1-2 scripts were also reproduced
first on the unfixed tree with the unchanged model.

**Tests added outside the model, and what they are.** `DecompilerRunnerTests.cpp`, `TestBudget`: one block (about 40
lines, after the volatile-list check) that submits the same over-budget function under another snapshot key, with a
name, with a function start and with a lent prototype (each must run: `RunsStarted` +1), the same question again
(answered at once, no run), and a Refresh stopped half way followed by a plain ask (runs again). `DecompilerPanelTests.cpp`:
`TestEnterAsksAgain` (Enter asks once after a failure and does not loop; Enter after Stop shows the new function,
not stale). CMake: `WILL_FAIL` removed from the model's ctest entry, comment rewritten.

**Mutation.** Revert the runner change (the old `DecompilerRunner.cpp/.h`, panel kept): the pinned A1-1 (three) and
A1-2 fail with TOO LARGE, default seed 6 fails, A1-3 passes; six checks of `TestBudget` fail (the four "another
question" runs, the stale ask and the stopped Refresh). Take out only the erase on completion: `TestBudget` fails
(`RunsStarted() == runs + 9`), the model does not -- the erase matters only when the question is asked again after the
panel's cache has dropped its Ok answer (more than `kCacheSize` = 8 other answers in between), which no generated
sequence does; the runner test is the check for it. Take `!askAgain` out of the panel's `q == mAsked` test: the
pinned A1-3, seeds 6 and 15 of the default run (NOT REFRESHED) and both `TestEnterAsksAgain` blocks fail.

**Runs** (RelWithDebInfo). `ctest -R "DecompilerModel|DecompilerRunner|DecompilerPanel"`: 3 of 3 pass (runner 48.8 s,
panel 2.6 s, model 16.4 s: 18 seeds, 755 operations, 7 budget stalls, 438 results checked at install, 80 walks, five
pinned sequences). `SE_MODEL_SEEDS=300` (165 s): 0 of 300 fail -- 11,907 operations, 29 budget stalls, 6,879 results
checked at install, 1,330 walks, 8,153 submissions, 1,490 cache hits; the previous long run's 43 failures were all
budget seeds (divisible by six; 60, 84, 174, 186 and 222 are named in the ledger), and all of those in this range
pass now.

**Not done.** The memo's key includes the snapshot key, so it cannot serve "the same code in a new frame"; if a
live game's slow function turns out to re-run on every Refresh-less scrub step, the remedy is a hash of the bytes the
function and its callees read, which only the engine knows (a result field), not a looser key. Cancelled-run erasure
is the literal reading of "any status" and could be narrowed to completed runs if a stopped Refresh turns out to
cost users a second wait.

## Areas 3 and 4, oracle: the generator's second grammar

Branch `review/3-4-generator`: `FrontEnd/tests/Sh2GeneratedTests.cpp`, and the comment on the ctest entry in
`CMakeLists.txt`. Ledger: A11-1 is `fixed 3a89718`; two rows are new, A4-2 and A4-3. No area 3 finding:
11,024 lent prototypes over 5,000 modules, none wrong.

### 1. The grammar (version 2)

Version 1 is untouched (one function, no stack; the same programs for the same seeds). Version 2 makes a
module of one to three functions laid out one after the other; each is built from version 1's items and
these. The default run takes the odd seeds as version 1 and the even ones as version 2
(`SE_GENERATED_GRAMMAR=1|2` picks one).

- **Stack.** r15 starts at `0x06040000`. A function pushes r8-r14 (`mov.l Rn,@-r15`) and PR
  (`sts.l pr,@-r15`, or `sts pr,Rn` into a pushed register), takes a frame (`add #-n,r15`, with r14 as
  frame pointer in 30%: `add #n,r14 ; mov r14,r15` to release), addresses locals as `@(disp,r15)` /
  `@(disp,r14)` / `@(r0,Rn)` with a constant index / `@r15` in every width, spills r1-r5 around a block
  (push, items, pop), and restores everything by `rts`; the last pop (or the return value's `mov`) may be
  `rts`'s delay slot. The harness checks r8-r15 and PR at the end of every run (a violation is reported
  as `generator:*`, not as the decompiler's).
- **Calls.** Callees come after callers. `bsr` with the right displacement, or `mov.l @(pool),Rn ; jsr @Rn`
  with the callee's address in the pool; 0-4 arguments in r4-r7, the last sometimes set in the delay slot;
  arguments kept across calls in r8-r11 and put back after (`mov r8,r4`); a call is sometimes the skipped
  part of a branch; PR is saved around nested calls. A function either writes r0 as its result on every
  path (the last write is a `mov #imm`, a register copy, an extension or a load, before the epilogue or in
  `rts`'s slot) or never writes r0 itself. Tail calls: `jmp @Rn` after the epilogue has restored PR and the
  registers (`lds.l @r15+,pr` / `lds Rn,pr`, the pops, the last as the slot). Fall-through: a frameless
  function with no `rts` that runs into the next one.
- **Flow.** `bra` forward to a loop test at the bottom with `bf` back; `bra` backward with an exit in the
  middle (`dt ; bt out ; bra top`); 1-4 data words skipped by `bra` (an `rts`, an invalid word and random
  words among them); a jump table through `mova + shll2 + mov.l @(r0,Rn) + jmp @Rn` with 3-6 entries
  (some shared by two entries, some going to the default), `cmp/hi` against the last index or `cmp/hs`
  against the count, `bt default`, the cases each ending in `bra join`, on an integer register the harness
  sets; a shared exit reached by two `bra` (each with a nop or an instruction in its slot); the exit placed
  before the entry (so the body is not one range from the entry).

Each function is a declarative record (pushed registers, frame, ending, items), so the shrinker removes
items, replaces a construct by its contents, turns `jsr` into `bsr`, drops the shared exit and the
exit-before-entry, and removes a function nothing refers to. A failing seed prints the line that replays
it: `SE_GENERATED_WORDS`, with `SE_GENERATED_FUNCS="start/entry/end,..."`, `SE_GENERATED_TABLES`,
`SE_GENERATED_DATA` and `SE_GENERATED_RETURNS` for a module.

### 2. The checks

Per function, against the emulator's runs of that function from its entry (4 inputs for version 2; a jump
table adds one run per index). Functions are decompiled last first, and what each learned (`prototype`,
when `known`) is lent to the ones before it through `DecompileRequest::prototypes`, as the panel does;
`knownFunctions` holds every entry. Once with the default request and once with the mailbox marked, when
the module has one.

- **Area 5, unchanged**, over the accesses the function made itself (not at a deeper call depth: a callee's
  accesses are its own, and a function that calls another and also jumps into it would otherwise be held to
  the callee's). **A11-1:** an access in the stack area (`0x0603E000-0x06041000`) through r15 or r14 is a
  local and exempt; a pointer into the stack handed to a callee is not. `FoldedIntoSwitch` now reports only
  what a run shows: a run that made one of the two accesses without the other (a branch whose condition is
  constant joins them rightly: the first draft reported that as a finding).
- **Area 4.** (a) Every instruction executed (the delay slot of a delayed branch included) is in
  `body`, or in a function this one calls (or that function's body), or a flow warning names it. (b) The
  entry of another function inside the body has a `FlowWarning::IntoFunction` at it. (c) No data word
  skipped by a `bra` is in the body. (d) Each index a run dispatched a jump table with reaches the label of
  the printed switch that holds the code the index runs: its own `case N:` (labels carry the address their
  code starts at), or the `default:` of a switch whose bound check the engine took out, or what follows the
  switch when the index leaves it; required when that code does something the ledger lists, or a warning
  says cases may be missing.
- **Area 3**, for a `known` prototype. (e) `known` and `prototypeWithheld` agree; it is not lent when the
  analysis is incomplete (truncated, unsupported, unimplemented, bad data, a table case missing, an
  executed instruction outside the body). (f) Every one of r4-r7 the function reads before writing (taken
  from the p-code of the run: every op's register inputs and outputs, the callees' too) that the
  prototype's parameter list does not reach must not matter: the function is run again with that register
  changed (+0x200, then 0), and if what it does to hardware, marked memory and memory through the
  parameter pointers (the accesses and values; r0 when it returns something) changes, the finding is
  `param-dropped:rN`. Stack traffic, literal-pool and table reads, and ordinary memory at a constant
  address do not count (the decompiler may drop them), or the check reports dead reads. (g) A return is
  claimed only if r0 is written on every run; a function the generator made to return r0 claims one.
  (h) A register `CallEffects::preserved` names ends every run with the value it started with
  (r0-r15, sr, pr).

### 3. Numbers

Default run (`ctest -R Generated`, RelWithDebInfo, 4 cores): 300 seeds, 150 of each grammar, 4.7 s
(3.2 s in the decompiler), green. Version 2 costs about 16 ms a seed, version 1 about 13.

```
SUMMARY: seeds=300 (grammar 1: 150, grammar 2: 150) ... failing seeds=0; findings=0 (4532 ms total, 3241 ms in the decompiler)
SUMMARY2: modules=150 functions=299 (end rts/tail/fall=253/33/13; returning 156, void 143) pushes=427
pr stack/reg=70/28 frames=162 (frame pointer 51) spills=95 locals ld/st=68/31 (stack accesses exempt 1723)
calls bsr/jsr=83/56 (arg in slot 51, args restored 139) tables=37 (entries 154, cases 109, shared entries 28,
default entries 17; destinations traced 204, printed 126) bra loops fwd/back=53/59 data skips=54
shared exits=63 exits below the entry=82 slot pops=117 ret in slot=53; lent prototypes=593 (void 192,
returning 401) withheld=47; parameter checks=593 (candidates 29, perturbed runs 222, insensitive 29);
preserved checks=593; flow: instructions executed=16674, into-function said/silent=56/0; runs=1993
```

`SE_GENERATED_SEEDS=5000` (both grammars, 2,500 each; 69 s): 9 failing seeds, all version 2: A4-3 in 8 and
A4-2 in 1. Version 1: no finding in 2,500 seeds. `SE_GENERATED_GRAMMAR=2 SE_GENERATED_SEEDS=5000` (81 s,
16.2 ms a seed): 15 failing seeds, 74 findings, 13 seeds A4-3 (`folded-into-switch`) and 2 seeds A4-2
(`outside-body`: 2226 and 3049); 9,854 functions, 8,380 ending in `rts`, 913 in a tail call, 561 falling
through; 11,024 prototypes lent (4,368 void, 6,656 returning), 825 withheld; 1,375 jump tables with 8,023
traced dispatches (4,537 reach a printed case, 3,486 run code that does nothing the ledger lists and may
be merged); 2,503 `bsr` and 1,686 `jsr`; 49,928 local accesses exempted; 344,628 executed instructions
accounted for; 1,820 "flow runs into" warnings, none missing for an entry in the body; parameter checks:
778 candidates (a register read before it is written that the prototype does not reach), 6,290 perturbed
runs, all 778 insensitive; 43,160 emulator runs; no `generator:*` finding.

Mutation (the checks checked; `SE_GENERATED_MUTATE=<kind>` damages every result as that defect would; 300
version 2 seeds; the run passes only if the check reports it): `ledger` (first ledger entry deleted) 287;
`params` (last parameter dropped) 278; `ret` (void and returning swapped) 209; `body` (last body range
dropped) 300; `cases` (case lines deleted) 50; `warn` (flow warnings deleted) 90; `preserved` (all registers
claimed preserved) 296; `lent` (withheld reasons cleared) 33; `data` (a data range added to the body) 91.
Version 1, `SE_GENERATED_MUTATE=1`: 277 of 300.

### 4. Findings

- **A4-2 (P2).** Flow that runs through a second jump into another function is shown as part of the
  first with no warning, and `body` omits what the actions removed after a restart that flowed further.
  Fixture in the ledger row (24 words, three functions). The first hop is warned; the second is not. The
  signature is withheld there, so no wrong prototype is lent.
- **A4-3 (P2).** The read of a jump table's entry is printed before the switch and unconditionally, when
  the code makes it only for an index in range (the bound check was folded into the switch). 28 words in the
  ledger row.
- **No area 3 row.** Nothing found: no wrong parameter list, no invented return, no wrong `preserved`, no
  prototype lent for an incomplete analysis. Incomplete analyses are withheld with a reason (825 of
  11,849); the commonest reasons are code removed as unreachable that holds hardware accesses, an incoming
  argument outside the signature (`mov.l @r7,r0` alone), a return not to the caller, a callee with no
  signature, and a register used after a call that is not known to return it.

### 5. Wrong in the first drafts of the checks, so a later reader does not repeat them

A frame pointer must release the frame with `add #n,r14 ; mov r14,r15` (the first epilogue did
`mov r14,r15` alone, and `lds Rn,pr` is `0x402A`, not `0x400A`, which is `lds Rn,mach`); a machine made
before a decompilation must not run after it (a decompilation takes the engine's global translator over;
the runner makes a new one); a case label can carry a cast (`case (xunknown4 *)0x2:`), and a bounded
switch prints its commonest destination as `default:`, so a label-per-index check is wrong, as is one that
reads the first `switch` of the text when a callee's was inlined (the switch line carries the address of its
`jmp`); an index that leaves the switch has no label at all (its code is what follows); a table whose entries
all go to one place has no switch ("Switch with 1 destination removed", warned); the accesses of a
function that is both called and jumped into belong to the callee when run at a deeper call depth;
`param-dropped` must not count a dead read of ordinary memory, a literal-pool load or stack traffic as
a difference; `mov r0,r0` is not a write of r0; a loop counter must not be the destination of a load that
a rewritten store turned into (a store with no register holding a value becomes a load).

### 6. Not done

The fixes (A4-2, A4-3); passing the address of a local to a callee (the callee's stores through it are the
case `ReturnCheck`'s escape tracking exists for, but a callee's `@-Rn` forms and displacements would
overflow the frame, and a corrupted return is the program's, not the decompiler's); arguments on the stack
(a fifth argument); polls and unmarked RAM polls in version 2; mirrors (a callee reached at `0x26...`);
recursion and mutual recursion (the call graph is acyclic); the panel's `Lendable` dependency rule (every
known prototype below is lent); the call's printed argument count against the lent prototype (not
checked); the generated test under ASan; MSVC and macOS builds. The default seeds do not reach A4-2 and
A4-3 (about 1 in 400 version 2 seeds), so the ctest entry is green and the nightly job (20,000 seeds) is
red until they are fixed.

## Area 5, round 2: fixes

Fixer, branch `review/5-round2-fix`, fix commit `342263f`. Ledger rows A5-3 to A5-7 are fixed, A5-8 is accepted,
and the A5-4 row, whose code spans held a `|` that broke the table, is mended. No engine patch: every root cause
was in the host (the map, `HardwareNotes`, `NoteRemovedAccesses`, `Accesses`) or in the test oracle, so
the vendored tree is unchanged (patches 0001 to 0020). Mednafen (`ss.c`, `smpc.c`, `vdp1.c`, `vdp2.c`, `sh7095.inc`)
was re-read for each rule; the audit document has them (`HARDWARE_REGISTER_AUDIT.md`, "Access width and lane").

**A5-3, A5-4: the access-kind table gets a second axis.** `HardwareRegister::narrow` (`NarrowAccess`) says what an
access narrower than the register does when the bus does not just take the bytes it carries; `NarrowAccessEffects`
turns the kind into a caution; `HardwareNotes` adds it on the instruction, naming the register and the width. Every
register of a kind gets the rule, from the block it is in (the on-chip block splits at 0xFFFFFF00):

| Kind | Registers | What the access does (Mednafen) | Caution |
|---|---|---|---|
| `WholeOnByte` | VDP1, VDP2 | a byte write writes the whole 16-bit register, the other byte from the data bus (`WriteReg`, `RegsWrite`) | "a 1-byte write to VDP2_TVMD writes the whole 16-bit register: ..." |
| `OnChipLong` | the divider, the DMAC | a word write at the register's address writes it whole (DVDNT, DVDNTL also divide); at +2 nothing; a word read returns the low half at +0 and +2; a byte access is a CPU address error | one caution per case |
| `OnChipKeyedLong` | BCR1 to RTCOR | no word write is taken: only a 4-byte write with 0xA55A above | "does nothing: it is set only by ..." |
| `OnChipWord` | the UBC | a byte access is an address error | the same |
| `ByteLane` | the SMPC | registers on the low lane at odd addresses: a word at the even address is a write of the low byte (or a read of 0xFF00 or the register); a byte at an even address does nothing (reads 0xFF); a long is two 16-bit accesses, bits 23-16 then bits 7-0 | one per register |

The SMPC word write also prints as the register now. Each SMPC register is registered a second time as a 2-byte
symbol at the even address (`SaturnHardwareWordViews`, both partitions, volatile); the engine takes the smallest symbol
that holds the access, so a byte at the odd address still finds the byte symbol, and `mov.w r0,@r1` at 0x2010001E
prints `SMPC_COMREG = 0x19;` with the caution that it is a byte write. A long is not named on the line (it covers
two registers, no single symbol can); the caution names each. The "covers REG1 and REG2" caution is not made for the
SMPC, the lane note replaces it. The ledger lists every access at its real width.
Also found, not fixed: the BSC's keyed long write (BCR1 to RTCOR with a key other than 0xA55A is ignored) is not
checked, only the word write that never works.

**A5-5: one to one.** `NoteRemovedAccesses` and the tests' `Accounts` (replacing `AccountFor`) match each access of
the reference run with one shown access or one engine record, never one for two, by instruction, direction and width
and by address where both sides have a constant one (exact pairs first, so an access through a pointer does not take
a constant one's record). `AccessFate::frame` marks a fold into the function's own stack frame (the record's address is
the frame offset, -4 for a pop: `constantAddress` is now false for it, the engine's space is read in `DecompileInto`);
such a record matches nothing, and `DeadIsLoss` ignores it. `ReferenceOk` and `TestGameCode` spend one removal
caution per removed access (`SaidCount`).
Fixtures: `macRemoved`, `macRemovedSecond`, `macRemovedBoth`, `rteSlot` (with its two frame folds), control `rtsSlot2`; and
made-up records for the oracle itself (a fold does not excuse a second access; one printed access likewise; a frame
fold at -4 is not a register).

**A5-6: mac.** `Accesses` now allows as many accesses of an instruction and direction on a path as the flow had LOADs or
STOREs for it, less the ones that ended another way (mac.l has two). `repeatedOnPath` is unchanged for one-access
instructions, and `TestGameCode` no longer exempts mac by opcode. Fixtures `macPtr`, `macWPtr`, `macSame`, `macReg` with `Ledger`.
*What the generator must change* (`Sh2GeneratedTests.cpp`, not edited here): nothing for the flag itself, since it reads
`repeatedOnPath`: `SE_GENERATED_WORDS="045F 000B 0009 0009"` (also `445F`, `044F`) now gives 0 findings. mac can go into
the grammar, with its trace expecting two reads of one instruction. But the "dropped" check lists a trace access as
printed when any ledger entry has the same (instruction, direction, size) -- the A5-5 hole, in the generator: it
should count (a ledger entry spent per trace access, by address when both have one), or one printed operand of a
mac excuses a lost second.

**A5-7: caution naming.** The removal caution reads "the N-byte write of A and B here is missing" (all registers
`HardwareRegistersCovering` finds, `RegisterNames`; an unnamed address in hex; "through a pointer" with the width).
The earlier fixtures' strings read "the 1-byte write of SMPC_SF ...". The guide's table does not quote them.

**A5-8: accepted.** Mednafen's low work RAM (`ss.c`, `A & 0x100000` in 0x00200000-0x003FFFFF) is not mapped for VA0
and VA1 (reads 0xFFFF, writes dropped) and mirrors for VA2. It is ordinary memory either way, no register is
involved, and a user mark reaching the mirror is a harmless superset: the aliases stay.

**Mutation checks** (the fix reverted, the suite fails; restored, it passes), all in `TestHardwareVolatility` and
`TestHardwareMap`: the `WholeOnByte` rule off: the three VDP fixtures and the sweep; the on-chip word rule off:
`divWordHigh`, `divWordLow`, `divWordReadHigh`, `bscWord` and the sweep; the on-chip byte rule and the SMPC word
rule off: `divByte`, `divByteRead`, `ubcByte`, `smpcWord`, `smpcWordRead`, the mirror fixture and the sweep; the SMPC word views not registered:
`smpcWord` and `smpcWordRead` (the line); the removal match without address and multiplicity: `macRemoved`,
`macRemovedSecond`, `rteSlot`, `ReferenceOk` on them; the allowed count fixed at 1: `macPtr`, `macWPtr`, `macSame`, `macReg`;
the caution naming the first register: `removedLong`; the oracle without the frame rule: its made-up cases (three
checks).

**Matrix** (RelWithDebInfo, gcc):

| | |
|---|---|
| `ctest` | 93 of 93 passed (about 66 s with -j3) |
| Game corpus, `hwram007.bin` | 839/839; every count 0; 16 missing and said; records: 14,842 folded, 0 dead, 16 unreachable, 27 switch lookups |
| Game corpus, `hwram008.bin` | 1,554/1,554; every count 0; 20 missing and said; records: 38,700 folded, 0 dead, 20 unreachable, 155 switch lookups |
| New cautions on game code | none: A5-3 and A5-4 add 0 notes to the 839 and 1,554 outputs (`SE_GAME_OUT`); no partial register access, no `Ram2010`/`Ram0010` line, in either (the 16 and 20 removal cautions now carry the width) |
| Generated test | passes (`ctest`); mac words give 0 findings |
| ASan + UBSan + LeakSanitizer, gcc Debug, `Sh2DecompilerTests` | clean beyond the two tolerated UBSan lines (`address.hh:605`, `slghpatexpress.cc:979`); no AddressSanitizer or LeakSanitizer report (`detect_leaks=1`); suite passes |

## Area 6, oracle: the fault-injection matrix

`SaturnExplorerDecompilerFaultTests` (`FrontEnd/tests/DecompilerFaultTests.cpp`, branch `review/6-lifecycle`, POSIX
only because it forks) is the oracle `REVIEW_PROGRAM.md` asks for in area 6: allocation `k` fails for every `k`,
the cancel flag goes up at every point of a run, and the runner's Stop, supersede, invalidate, budget, disable and
destroy happen with the worker held at every phase entry. Every cell runs in a child process; a crash, a hang, a
sanitizer report, a leak or an exit through static destruction from a damaged engine is a finding, not the end of the
test. No engine, spec or runner source was changed (the runner and one engine file were mutated for the checks
below and restored); no test seam was added: the existing `SetPhaseHookForTests` and
`SetBeforeReferenceHookForTests` give the phase entries, and the allocator that the test replaces gives the rest
(it counts, fails one allocation, or raises the cancel flag at one, which is the "op count" hook the brief allowed for).

**Machinery.** The parent spawns itself (`--driver`, `--runner`, `--runner-oom`, the `--engine-oom` pattern). A
driver warms one engine until two runs agree to the allocation, then `fork()`s a cell per `k` from that state, so
thousands of cells cost minutes, not a translator build each; a cell reports through its exit status and its output,
the driver kills it after `SE_FAULT_TIMEOUT` seconds (default 120), and the parent kills a driver after 10 minutes
(4 hours in full). A cell that leaves through `exit()` when its engine is damaged is caught by an `atexit` trap
registered after the translator's statics. Under ASan each cell ends with `__lsan_do_recoverable_leak_check()`,
since `_Exit` skips the exit-time check. Reproduce one cell with
`SaturnExplorerDecompilerFaultTests <scratch> --cell <family> <fixture> <k>` (it prints the failing allocation's
stack); `<family>` is `alloc`, `cancel-enter`, `cancel-alloc`, `cancel-us`, `cancel-oom` (`<k>` is
`cancel allocation * 8 + index into {0,1,2,4,8,64,512,4096}`), `runner` (`<kind> <entry>`) or `runner-oom`
(`<fixture> <entry> <j>`). `k` is an allocation index of one `Decompile` on a warm engine; the counts are the same in
the RelWithDebInfo and the ASan+UBSan build (gcc 13.3): mailbox 33,119, jumptable 32,080, fixture1 19,913, diamonds
411,659. Environment: `SE_FAULT_FULL=1` (whole matrix), `SE_FAULT_DENSE=<n>` (the dense prefix of the alloc sweep,
default 3,000; `100000` makes it every `k`), `SE_FAULT_JOBS`, `SE_FAULT_TIMEOUT`, `SE_FAULT_KEEP=1` (logs),
`SE_FAULT_ONLY=alloc,cancel-oom,...` (job-label prefixes), `SE_FAULT_EXIT=static` (a damaged engine leaves through
`SleighArchitecture::shutdown()` and `exit()`, as the application would, instead of `_Exit`), `SE_FAULT_LEAKS=0` (no
per-cell leak check).

**Fixtures** (one snapshot): `mailbox` (TestHardwareVolatility's `mailboxFold`: code is removed, so the reference run
happens: 14 phase entries), `jumptable` (a 4-way switch through a table: 9 entries), `fixture1` (a call through a
literal pool, two passes), `diamonds` (150 diamonds, one pass: a long action tree). Every request carries a name, a
known function and an unrelated volatile range, so that some allocations are made before the engine's first phase.

### The cells

| Family | Cell | Invariants checked |
|---|---|---|
| `alloc` | allocation `k` of a `Decompile` throws, mailbox and jumptable | no exception escapes; `Failed("out of memory")` (or `Ok` with the baseline's output if a `nothrow` allocation was absorbed); the snapshot's reference count returns; the next request is `Unavailable` ("out of memory", and `Disassemble` refuses) when the failure was inside the engine (`mWorkPhase != Idle`, seen from the phase hook) and `Ok` with the baseline's output otherwise; `_Exit` with no ASan, UBSan or LSan report |
| `cancel-enter` | the flag goes up at the start of phase entry `e` (the phase hook), all four fixtures, plus before the run and past its end | the outcome is the one the design gives (below); a cancelled result has no lines; the same `Sh2Decompiler` then gives exactly the baseline output |
| `cancel-alloc` | the allocator raises the flag at allocation `k` (a deterministic op count) | as above; the latency, in allocations, between the flag and the stop is recorded |
| `cancel-us` | a second thread raises the flag `t` microseconds in (full: every 100 us across the run and 2 ms past it, so 1 ms steps are a subset) | as above, over the range of phase entries the flag can have landed in |
| `cancel-oom` | cancel at `k`, and allocation `k + j` fails, `j` in 0..4096 | the failure may fall in the unwinding of the `CancelError`: Cancelled, Failed or Ok, never a crash; the `alloc` checks follow |
| `runner` | `Cancel`, `Submit` while busy, `InvalidateSession`, destruction, budget expiry (with the too-large memo), `DisableLocked` and both ways back, with the worker held at every phase entry | the worker joins; exactly the expected results come back and none for a stale generation or epoch; Cancel on a queued request answers at once; the memo is keyed by entry as asked and volatile list, answers without a run, is bypassed by Refresh, mirrors and other volatile lists, and is cleared by a new session; a stuck cancel disables, a new session re-enables only when nothing runs; the next request is correct |
| `runner-oom` | the worker fails its `j`-th allocation after being let go at phase entry `e`, a process per cell | Failed("out of memory") or the session disabled while recording; the worker answers the next request (Unavailable); no job holds the snapshot |

*Where a cancel must land* (`Expected` in the test, from `Sh2Decompiler.cpp`): Flow and Actions have check-points,
Init, Print and Teardown none. A flag raised in Flow is honoured in Flow (a check before Actions), a flag raised in
the printing pass's Actions in Actions (a check before Print); a flag raised in Init, Print or Teardown waits for the
next Flow; a flag raised after the last check-point completes `Ok`; a flag raised in the reference run only cuts the
reference run short (`Ok` with the "could not be checked ... (cancelled)" warning, the signature withheld). A flag in
the first pass's Actions is expected in Actions; it is not always (A6-6).

### Size and time

Machine: 4 cores, gcc 13.3, `--parallel 2` builds. "Full" is `SE_FAULT_FULL=1`, three drivers at a time.

| Run | Cells | Wall time |
|---|---|---|
| RelWithDebInfo, default subset (what ctest runs: `ctest -R SaturnExplorerDecompilerFaultTests`) | about 750 cells in 25 jobs, plus 4 `runner-oom` processes | 14-17 s |
| RelWithDebInfo, full, dense prefix 3,000 | about 28,700 cells: alloc 7,487; cancel-enter 45; cancel-alloc 4,582; cancel-us 1,288; cancel-oom 14,888; runner 192 scenario cells; 207 `runner-oom` processes | 284 s (245 jobs) |
| RelWithDebInfo, `alloc` for every `k` (`SE_FAULT_DENSE=100000`) | 65,234 cells (mailbox 33,121, jumptable 32,113) | 256 s for the whole matrix with this sweep in place of the dense one (the alloc drivers take 70 s each, three at a time); 268 s for the sweep alone with `SE_FAULT_EXIT=static` |
| ASan+UBSan, Debug, `alloc` for every `k`, no leak check | the same 65,234 cells | 1,312 s |
| ASan+UBSan, Debug, full with leak detection: `alloc` dense prefix 3,000, `cancel-enter`, `cancel-alloc` (not diamonds), `cancel-us` (diamonds killed: it needs thousands of cells of seconds each), runner scenarios, `runner-oom`; `cancel-oom` not run in full | over 10,000 cells and 207 `runner-oom` processes | 2,584 s |
| ASan+UBSan, Debug, the six runner scenarios in full after the budget fix | 192 cells | 59 s |
| ASan+UBSan, Debug, default subset | as the first row | 154 s (the ctest TIMEOUT is 600) |

The default subset is not under 60 s in a sanitizer build; in the plain build it is. The exhaustive run is the one
that found A6-1 to A6-5: the dense prefix of 3,000 and the geometric tail miss all of them, which is why the subset
**pins** one cell per finding (`kPins`: fixture, size and phase of the failing allocation, found again by size and phase
if a build numbers allocations differently, reported as STALE if it cannot be).

### Findings

Ledger rows A6-1 to A6-9 (`REVIEW_LEDGER.md`); all open. The ctest entry is `WILL_FAIL` (the default subset fails on the
pinned cells), with a comment naming the rows.

| Row | Severity | What | Cells |
|---|---|---|---|
| A6-1 | P1 | `EmitPrettyPrint::setMarkup` frees the emitter before allocating its replacement: double free in the Print phase | 6 (mailbox 4, jumptable 2) |
| A6-2 | P1 | `EmitMarkup::setPackedOutput`, the same | 3, and 1 with a cancel in flight |
| A6-3 | P1 | `BlockGraph::identifyInternal` moves blocks into the new switch block before it can no longer fail: double free, any function with a recovered switch | 16 (jumptable) |
| A6-4 | P3 | the markup stream swallows the failure; reported "unexpected exception" instead of "out of memory" | 8 |
| A6-5 | P2 | message streams swallow the failure: `Ok`, and a truncated warning in the output (`WARNING: Removing unreac`) | 4 (1 changes the output) |
| A6-6 | P3 | a cancel in the first pass's Actions tail is honoured in the second pass's Flow, after its uncheckable Init (40% of the fixture1 run) | 2 `cancel-alloc`, 1 `cancel-us` |
| A6-7 to A6-9 | P3 | leaks after an in-engine allocation failure: Architecture construction (about 70% of the sampled cells), followFlow (varnodes), Actions | thousands, bounded to one failure per process |

What passed: the cancel matrix has no other violation (no crash, no leak, no wrong status, the engine always
produces the baseline output afterwards, a cancelled result never carries lines); the runner matrix has none
(RelWithDebInfo and ASan+LSan, all entries); `runner-oom` has no ASan error and no failed check, only the leak reports
of A6-7 to A6-9 (42 of 207 cells under LSan); with `SE_FAULT_EXIT=static` no `k` crashes in `shutdown()` or static
destruction beyond the A6-1 to A6-3 cells themselves (patches 0002 and 0003 hold at every allocation of both
fixtures, which `_Exit` alone would not show).

*Cancel latency* (allocations between the flag and the stop, the worst cell of each class; the runs are 19,913 to
411,659 allocations): flag in Init or Teardown about 7,970 (Architecture construction has no check-point: about 24% of
a mailbox run, 40% of fixture1); in Flow up to 964 (jumptable), 8,685 on diamonds; in Actions up to 411 on the small
fixtures but **116,893 of 411,659 on diamonds**, one action body; the Print phase is not interruptible by design.
These are not findings (the header says so) but they are the numbers behind the budget and the cancel-acknowledge
limit: a Stop on a 150-diamond function waits for more than a quarter of its run.

### Mutation checks (done, then restored)

| Mutant | Caught by |
|---|---|
| `DecompilerRunner::Poll` without the generation test | `runner supersede`: 65 checks |
| `Poll` without the epoch test | `runner invalidate`: 23 checks |
| `InvalidateSession` without `mTooLarge.clear()` | `runner budget` |
| memo key without the volatile list | `runner budget`: 10 checks |
| `RecoverLocked` without the new-session test | `runner disable`: 28 checks |
| patch 0004 reverted (`~Funcdata` without `clearActiveOutput()`) | ASan default subset, `cancel-alloc jumptable`: LeakSanitizer in `Funcdata::initActiveOutput` |

A6-1 to A6-3 are further instances of patch 0015's class (the replacement allocated after the old object is freed),
found by the matrix itself, which is the stronger evidence for the `alloc` family; patch 0015 itself was not reverted.

### Not done

- The `cancel-oom` family was not run in full under ASan (RelWithDebInfo only); it is in the ASan default subset.
- `cancel-us` on `diamonds` under ASan: killed after 40 minutes; the plain build ran all 982 cells.
- No game-corpus function was used (the corpus is not in the repository); the largest function is the 150-diamond
  fixture. `Sh2Decompiler::Disassemble` has no sweep (only tests call it).
- Windows and macOS: the test forks; on other platforms it builds as a stub that reports SKIPPED.
- Leaks (A6-7 to A6-9) were sampled, not enumerated: the sweep with leak detection ran the dense prefix of 3,000 and the
  geometric tail, not every `k`.

## Area 6, round 1: allocation failure in the printer and the switch block; streams that swallow

Branch `review/6-lifecycle-fix`, fix in `d41c806`. Ledger rows A6-1 to A6-6 and A6-8 are `fixed`; A6-7 and A6-9 are fixed at
their cheap sites and `accepted` at the rest, with the bound stated; A12-6 (the runner test's 50 ms follow-up) is
`fixed`. `SaturnExplorerDecompilerFaultTests` passes its default subset and its ctest entry has lost `WILL_FAIL`. The
engine change is patch 0023 (`patches/0023-allocation-failure-order.patch`, 11 files, about 160 functional lines); 0021 is
taken by another branch and is not in this tree.

**Root causes.**

- *A6-1, A6-2 (P1): the replacement is allocated after the old object is freed* -- patch 0015's class again, in the
  Print phase. `EmitPrettyPrint::setMarkup` did `delete lowlevel; lowlevel = new EmitMarkup;`, and
  `EmitMarkup::setPackedOutput` (and `setOutputStream`) `delete encoder; encoder = new PackedEncode(*s);`. When the `new`
  throws, the freed pointer is still in the member and the destructor frees it again as the `Architecture` is torn
  down. Now the replacement is made first (and, for the emitter, given its stream), the old object is deleted after, and
  a failure leaves the old one in place.
- *A6-3 (P1): blocks owned twice.* `BlockGraph::identifyInternal` moved the case blocks into the new switch block one
  `addBlock` at a time (`parent` set, then `push_back`) and only then rebuilt its own list without them. A failed
  `push_back` left blocks in both lists, and the `unique_ptr<BlockSwitch>` of `newBlockSwitch` and the function's
  `BlockGraph::clear` freed them twice. Now the one allocation of the move -- room in the new block's list -- is made
  first (`ident->list.reserve`), nothing between there and the end of the loop can throw, and our own list is compacted in
  place (no `newlist`, so no second allocation). Every `newBlock*` now holds the block under construction in a
  `unique_ptr` until `identifyInternal` has returned, which is also what made the `BlockList`/`BlockInfLoop` leak of
  A6-9 go away; `newBlockCopy` the same until `addBlock` has returned.
- *A6-4, A6-5 (P3, P2): streams that swallow.* libstdc++'s `basic_ostream` catches a `std::bad_alloc` thrown while its
  buffer grows and only sets `badbit`. The engine builds the text of every warning, comment, name and message in an
  `ostringstream`, so the failure cut the text short and reported nothing: `Ok`, and `/* WARNING: Removing unreac */`
  in the pseudocode (A6-5); the markup stream of the Print phase did the same, and a later parse of the cut markup
  threw something that was none of the handled exception types ("unexpected exception", A6-4). The engine's
  `ostringstream` is now a class of its own (`error.hh`, in place of the using-declaration; `filemanage.hh` repeated
  the declaration and includes `error.hh` instead) whose constructor sets `exceptions(badbit)`, which makes the
  insertion rethrow the original `bad_alloc`. The host does the same for the two streams it hands the engine
  (`runPass`'s `errors` and `markup`, and `Disassemble`'s `errors`) and, before it builds anything from either, checks
  `bad()` and throws `std::bad_alloc` -- so the output is never built from a stream that has failed. The failure then
  leaves `Decompile` through the `std::bad_alloc` handler: `Failed("out of memory")` with the engine marked damaged, like
  any other allocation failure in the engine.
- *A6-6 (P3): a cancel in the first pass's Actions tail.* `runPass` returned from the non-printing branch before any
  check-point, so a flag raised there waited through the second pass's Init and was honoured in its Flow. The branch
  now calls `checkCancel()` before it returns: the cancel is honoured at the pass boundary, `cancelledIn` is Actions as
  the matrix's oracle always said it should be, and the second pass's Architecture is never built. A flag raised
  after that check (in the pass's Teardown) still waits for the next Flow, as it should (nothing in Teardown is
  interruptible). `Sh2Decompiler.cpp`'s changes are the stream masks and that call.
- *A12-6.* `TestDisableOnUnacknowledgedCancel` ran its follow-up requests under the stuck request's 50 ms budget (61 ms
  of work on the Windows Debug runner: "too large"). The budget is now ten times a `Small()` run measured on a generous
  runner first (50 ms at least), the way `TestBudget` already derives its own. The matrix's own budget and disable
  scenarios had the same shape (their follow-up runs under 6 times the measured request and failed once under a
  parallel build: "status 5"); they use 12 times now.

**Leaks (A6-7 to A6-9).** The sweep with leak detection is the survey: `SE_FAULT_LEAKLIST=1` (new) prints the innermost
frames of every leak instead of judging it. Fixed, because the fix is local and the order of operations in the failing
function is obvious: the print language and the options of a failed `Architecture` constructor
(`Architecture::Architecture`, `OptionDatabase::registerOption` and `OptionDatabase`'s constructor -- the first version of
the fix missed `ElementId::find`, which the survey found), `DisassemblyCache::initialize`, `TypeFactory::insert`,
`VarnodeBank::create` and `createDef`, `PcodeOpBank::create` (an op is owned by the lists; `optree` only indexes it) and
`markAlive`/`markDead`/`insertAfterDead` (the same erase-then-allocate shape as A6-1, with `std::list` nodes: the new node is
made before the old one is dropped; the survey found this one too), `CommentDatabaseInternal::addComment*`, `newBlockCopy`
and the `newBlock*` family. Accepted, each by the frames of its
allocation stack (`AcceptedLeaks` in the test; every frame of an entry must occur in the stack, in order; a leak that
matches none fails its cell, so this is not the blanket of LeakSanitizer's own suppressions, which match one frame):
the XML reader (`xml_parse`, `xml_tree`: the parser's values live on bison's stack, and a throw drops them), the action
tree (`ActionDatabase::universalAction` builds hundreds of `new Action`/`new Rule` into raw lists; `deriveAction` clones
it), the prototype models decoded from the compiler spec (`Architecture::parseCompilerConfig`), `findAddJoin`,
`ContextInternal::FreeArray::reset`, the host's register loop (`Scope::addSymbol`/`addMapPoint`, whose
`addSymbolInternal`/`addMap` insert into several indexes, so freeing in a handler would double-free a half-inserted
symbol), and the conflict symbol `ActionNameVars::linkSymbols` makes. All of them are in `Architecture` construction or
initialisation or in one action, after which the engine is refused, so there is at most one such leak per process: a few
hundred bytes to about 30 KB. `coreaction.cc` was not touched (another branch is editing it), so the Action sites would
have been accepted in any case.

**Other delete-then-new and move-then-allocate shapes** (`grep` over the vendored sources; the matrix reaches none of
them): `Varnode::calcCover` (`delete cover; cover = new Cover`; only called with a null cover), `TypeCode::setPrototype`
(`delete proto; proto = new FuncProto()`; `proto` is null at its one call), `FuncProto::setParamshift`
(`delete store; store = new ProtoStoreInternal`; only for a spec with a parameter shift, which this one has not),
`JumpTable::setOverride` (`jumptable.cc`: another branch's file; user overrides, which the host does not use) and
`PrintJava`'s constructor (Java only). Left as they are: none is reachable by a fixture, and the patch README's rule is a
test that fails without a fix. `Scope::addUnassigned` reads `entry->symbol` after `delete entry` (an upstream
use-after-free on the dynamic-symbol path, `isGlobal()` only); nothing in this decompiler calls it. Every other
`BlockGraph` mover (`buildCopy`, the `newBlock*`) was read: they allocate before they attach and own what they allocate,
now. No further move-then-allocate shape was searched for beyond `block.cc`.

**Pinning.** The old `kPins` named an allocation by its index and found it again within 400 of it; a merge since the
oracle was written had shifted every one by more than 1,000 (mailbox 33,119 allocations then, 35,420 now): of the eight,
three resolved to a neighbour of the same size that does not crash, and five to nothing (reported STALE, which fails the
run, so the default subset failed for the wrong reason). A pin is now `{fixture, phase, ordinal of the
phase entry, size, rank among that size in the entry}` -- stable under any change elsewhere in the run -- and
`--cell alloc <fixture> <k>` prints the coordinates of its allocation (`PIN-AT`). 31 pins: 16 for A6-1 to A6-5, 15 for the
leaks that are fixed (seen only under LeakSanitizer; in a plain build they are cells that pass). A fix that adds or
removes allocations just before a pin makes it resolve to a neighbour of the same phase and size; the mutation runs
below are made with the mutated engine, where the pins resolve to the defect.

**Mutation** (a copy of the tree, one change reverted, `ninja` incremental, RelWithDebInfo, `SE_FAULT_ONLY=alloc` or the
named job; restored and verified identical afterwards):

| Reverted | Cells that fail |
|---|---|
| `prettyprint.cc` (A6-1, A6-2) | 5 crash (SIGSEGV): mailbox print #0 48 B #22, 40 B #77 and 16 B #1, print #1 48 B #20; jumptable print #0 48 B #26 |
| `block.cc` (A6-3; also `newBlock*`) | 4 crash: jumptable actions #0 16 B #72, 32 B #59, 16 B #138 and actions #1 16 B #72 |
| the host's `markup.exceptions(badbit)` and `bad()` check (A6-4) | 3 fail "unexpected exception": mailbox print #0 513 B and 1025 B, jumptable print #0 513 B |
| `error.hh` and `filemanage.hh`, the engine's `ostringstream` (A6-5) | 4 fail: mailbox actions #0 ("a throwing allocation failure is reported, not swallowed") and #1 (`WARNING: Removing unreac`), jumptable followFlow #0 and #1 |
| the `checkCancel()` of the pass boundary (A6-6) | 2 fail: `cancel-alloc fixture1` 10662, 10663 ("expected Cancelled-in-Actions but got cancelledIn followFlow") |
| the host's `errors` stream mask and `bad()` check | none: the `errors` stream never reaches 513 bytes in these fixtures, so no cell grows it. It is the defensive twin of the `markup` change, not pinned |
| all the leak fixes of patch 0023 at once (`architecture.cc`, `options.cc`, `sleigh.cc`, `varnode.cc`, `op.cc`, `comment.cc`, `type.cc`, and `newBlockCopy`'s `unique_ptr`) | ASan + LeakSanitizer, `SE_FAULT_ONLY=alloc`: 24 cells leak and fail (mailbox 18, jumptable 6), at every fixed site: `Architecture::Architecture` / `OptionDatabase` (`PrintCCapability::buildLanguage`), `DisassemblyCache::initialize`, `TypeFactory::findAdd`, `PcodeOpBank::create` (from `PcodeEmitFd::dump` and `cloneOp`; and `markAlive`), `VarnodeBank::create` (`newConstant`, `newVarnode`) and `createDef`, `newBlockCopy`, `addCommentNoDuplicate` |
| every engine and host change (the tree before this commit, the 16 pins of A6-1 to A6-5) | default subset: 3 of 25 jobs fail: 9 crash cells, 7 invariant cells in `alloc`, and the 2 `cancel-alloc fixture1` cells |

**Merged tree.** After the first runs below, master moved (patches 0021 and 0024-0027, regenerated spec data; merged
as `4c838c5`, then `e7b4b9e`'s ledger row). The old index-based pins of the oracle had gone STALE there; the
phase/size/rank pins of this round all resolve on the merged tree (31 of 31, none STALE), and the five mutations were
run again on it with the same result (5, 4, 3, 4 and 2 cells fail: `prettyprint.cc`, `block.cc`, the host's markup
mask, the engine's `ostringstream`, the pass-boundary check). The numbers below are the merged tree's.

**Runs** (the merged tree).

- RelWithDebInfo, the full `alloc` sweep for every `k` (`SE_FAULT_FULL=1 SE_FAULT_DENSE=100000 SE_FAULT_ONLY=alloc`,
  4 shards): mailbox 35,956 cells and jumptable 34,018, **0 failed** (`Failed("out of memory")` or the baseline's output
  for each; every damaged engine refused the next request), 217 s. The same sweep on the unfixed tree (before the
  merge) fails 37 cells: 25 crashes (A6-1 to A6-3: mailbox 6, jumptable 19), 8 "unexpected exception" (A6-4) and 4
  swallowed failures (A6-5).
- ASan + UBSan, gcc 13.3, Debug, the default subset with `detect_leaks=1`: 25 of 25 jobs pass, 138 s (ctest's limit is
  600). Accepted leaks tolerated in it: A6-7 and A6-9 only.
- ASan + UBSan, Debug, `alloc` with leak detection (`SE_FAULT_FULL=1 SE_FAULT_ONLY=alloc`, 4 shards: the dense prefix of
  3,000, the geometric tail, 65 allocations around every phase entry, the end), before the merge: mailbox 3,887 cells and
  jumptable 3,616, **0 failed**, no ASan or UBSan report. Leaks tolerated by `AcceptedLeaks`: A6-7 4,662 cells and A6-9 3;
  every other leak would have failed its cell. (The first run of this survey, before the fix to `markAlive`, found three
  such cells: an op taken out of the dead list and not yet in the alive list when `insert` threw.) 1,002 s.
- `ctest` (whole, RelWithDebInfo): **95 of 95 pass** (87 s with `-j2` on the merged tree): `SaturnExplorerDecompilerFaultTests`
  32 s under load (no `WILL_FAIL`), `DecompilerRunnerTests` about 40-50 s, `Sh2DecompilerTests` 1.7 s. `se-sleighc` (which
  includes `error.hh`) was built with the new class; `se-regen-sla` was not run.
- The game corpus, `hwram008.bin`, once (`SE_GAME_HWRAM=.../h/hwram008.bin bin/SaturnExplorerSh2DecompilerTests <scratch>`):
  1,554 functions, 1,554 ok, 0 failed, 0 other; every ledger count 0 (0 not printed exactly once, 0 instructions with
  more accesses than they make, 0 accesses of another width, 0 statements with two reads, 0 reads printed past another
  access or a call, 0 printed as conditional, 0 pairs out of the code's order, 0 joined into a switch across a branch,
  0 accesses dropped against the reference run, 0 functions without a reference, 0 ended dead); the 20 "missing and
  said" are as recorded. 34 s.

**Not done.** The cancel-latency numbers are as before. The leaks accepted are bounded by the host's refusal of the engine
after the first in-engine failure; if that policy ever changes (the engine rebuilt in process), they are a leak per
failure, and the XML reader and the action tree are the sites to make exception-safe first. The host's `errors`
stream change has no cell. `Disassemble` still has no sweep. Windows and macOS: the matrix forks and is POSIX only;
the vendored change touches `error.hh`, which `se-sleighc` includes too (its `ostringstream`s now throw on a failed
allocation, which it does not expect to see), and the MSVC build of it was not run here.

## Area 4, round 1: the second hop and the table read

Fixer, branch `review/4-flow-fix`, fix commit `d140d0e`, on `6736a36`. Ledger rows A4-2 and A4-3 are `fixed d140d0e`; the A5-5
hole in the generator (the round 2 fixer's note "For the generator owner") is closed and mac is in the grammar. Two engine
patches, 0021 and 0024 (the vendored tree is patches 0001 to 0024; `vendor.py` on the pinned Ghidra reproduces it exactly,
checked in a copy); the host changes are in `Sh2Decompiler.cpp` (the body, the tail-jump search, the jump tables handed to
`ReturnCheck`).

### 1. A4-2: the second hop

*Root cause.* Everything the host knew of "what flow reached" came from the p-code of the first flow (`ScanRawPcode`, taken
right after `followFlow`) and from the p-code that survived the actions. A restart in the actions (here a literal the
second `jmp @r2` goes through folds only once the actions have run) clears the function and follows the flow again, and
that flow reaches further: F0 reaches F1 in the first flow, F2 only in the second. What the actions then removed as dead
(F2's `add #-0x40,r15` at its entry and `add #0x40,r15`; a `tst`/`bf` whose arms meet) was in neither list. So the entry of
F2 was not in the body (`InBody(entry)` false: no "flow runs into" warning for it), and `body` left out code the program ran.
The warning loop itself was right for every entry its test saw; the input was wrong.

*Fix.* Patch 0021: `Funcdata::flowInsns` (`FlowInsn { addr, halted }`, `getFlowInsns()`) is filled from `FlowInfo::visited` when
`followFlow` returns and cleared with the function, so after a restart it holds the last flow's instructions, p-code or none,
kept or removed. `FlowInfo::artificialHalt` notes where flow stopped for bad or unimplemented data. The host takes `body` from
it (halted ones left out, as before), and searches it, not the first flow's list, for `jmp`/`braf` taken for tail calls. The
same patch records the indirect jumps' destinations as the last flow recovered them (`Funcdata::flowTables`,
`getFlowTables()`), which `ReturnCheck` now has beside the first flow's and the final ones. `IntoFunction` fires for every known entry the body holds
with no change to its loop. `DECOMPILER_GUIDE.md`'s row for the warning now says "one line for each such entry" and
that an entry counts even when its first instruction is dead; the header comment of `DecompileResult::body` says the same.

*Suite.* `TestFlowReachedBody` (new): the ledger's 24 words (F0 jumps into F1, F1 into F2; the request lends F2's and F1's
signatures as the panel does): both entries warned, nothing else, body `[F0, F0+0x10) [F1, F1+0x0A) [F2, F2+0x0C)`, F2's two
dead `add`s in it; the same with F2 opening on a dead `tst`/`bf`; F2 itself warns of nothing; with F2 not named the second jump
is a tail call to an address nobody named (no flow into it: one warning, F2 not in the body).

### 2. A4-3: the table read

*Root cause.* The reviewer suspected a hold or a use that kept the read as a variable; it is neither. Patch 0013 refuses to fold a
switch's bound check into the switch when the switch block holds a LOAD other than the table lookup, and exempts the lookup
because the switch stands for it. That is true only while nothing else reads the lookup's value. In the fixture the join after
the switch stores `extu.b r1,r2`, r1 being 2 on the default path and the table entry on the others, so the lookup is the code's
own read of a value it uses. Upstream's `noInterveningStatement` (the check that the block makes no value used outside it) is
meant to refuse that: it did, until the actions narrowed the use to a SUBPIECE of the lookup in the switch block, which the
function skips over together with COPY whatever uses the output. The guard was folded, the read stayed in the switch's block,
and the block ran for the default path too.

*Fix.* Patch 0024: `JumpBasic::foldInOneGuard` also refuses when the lookup's value (followed through the model's own path
ops, `pathMeld`) has a use outside the table's calculation. The guard stays an `if` and the read stays under it; a lookup
nothing else reads folds as before (`switchFold`: still folded, one `SwitchLookup` record).

*Suite.* `switchUsed` (the ledger's 28 words, moved to 0x0600B400 because the original base is `poll`'s): `Ledger` with the reads
`{ Rd(0,2), Rd(0,4), Wr(0,2) }`, whose `FoldedIntoSwitch` invariant is the oracle; `if (` and no `default:` in the text; the lookup's
access is not in the block of the read ahead of the guard; no `SwitchLookup` record for it; `ReferenceOk`.

### 3. The generator

- **The A5-5 hole.** The "dropped" check treated a ledger entry with the same instruction, direction and size as covering
  every trace access. It now spends one entry per access: the accesses are grouped by the execution of the instruction that made
  them (`TraceRec::step`, the position in the run, not part of the identity: two accesses of one step are one instruction's operands,
  the same access of two steps is a loop), and each group is matched one to one with the entries, by address where the entry has a
  constant one (exact pairs first), then the accesses the decompiler must list (a parameter's memory, hardware, marked memory)
  before the ones it may fold into a variable. `SE_GENERATED_MUTATE=second` loses the second of two entries of one instruction
  and passes only if reported.
- **mac.l / mac.w** `@Rm+,@Rn+`, in one program of eight of version 1, from a stream of its own so every other program of a
  seed is the same as before. The operands are among r4 to r7 (a literal pointer only if aligned for the width), the same register
  in one case in seven.

### 4. Matrix

**Mutation** (the fix reverted, the checks fail; restored, they pass). The host and the one engine guard were switched off by a
temporary environment variable in a scratch build, not by editing the patches:

| Reverted | Fails |
|---|---|
| the body and the tail-jump search from the first flow's p-code plus what survived the actions (patch 0021's list unused) | `TestFlowReachedBody`: 6 checks (both warnings, the body, the entry and the dead `add`, for both fixtures); generator seeds 2226 and 3049 (`outside-body`, `lent-incomplete:executed-outside-body`) |
| one `IntoFunction` warning only | `TestFlowReachedBody`: 2 checks; seeds 2226 and 3049 (`into-function`) |
| the return check without the last flow's tables | `TestFlowReachedBody`: the 2 signature checks (the generator is silent: a withheld signature is the safe side) |
| the lookup's use check in `foldInOneGuard` (patch 0024) | `switchUsed`: 3 checks (`Ledger`'s `FoldedIntoSwitch`, the `if (`, the blocks); seeds 1009, 1968, 2522, 2677, 2876 (`folded-into-switch`) |
| the generator's "dropped" check as it was (any entry of the instruction, direction and size covers a trace access) | `SE_GENERATED_MUTATE=second` reports 0 of 1,000 version 1 seeds and the run fails; with the one-to-one check it reports 83 of 1,000 |

**Matrix** (RelWithDebInfo, gcc; the machine ran other work, load average 5 to 8):

| | |
|---|---|
| `ctest` | 94 of 94 passed (69 s with -j2) |
| Generated test, default (300 seeds) | passes |
| `SE_GENERATED_SEEDS=5000` (both grammars, 2,500 each) | 0 failing seeds, 0 findings (78 s). mac in 287 of the 2,500 version 1 programs (137 `mac.l`, 150 `mac.w`) |
| `SE_GENERATED_GRAMMAR=2 SE_GENERATED_SEEDS=5000` | 0 failing seeds, 0 findings (87 s); the ledger's 15 failing seeds and 74 findings (13 seeds A4-3, 2 seeds A4-2) are gone. 9,854 functions, 11,045 prototypes lent (4,375 void, 6,670 returning; 804 withheld, 825 before), 1,838 "flow runs into" warnings none missing for an entry in the body (1,820 before); 779 parameter candidates, all insensitive; 43,160 emulator runs |
| Game corpus, `hwram007.bin` | 839/839; every count 0; 16 missing and said; records 14,842 folded, 0 dead, 16 unreachable, 27 switch lookups: **the same as the previous master; 0 of 839 outputs changed** (`SE_GAME_OUT`, compared file by file). The body and the lend/withhold of every function were also compared with the old logic (a scratch build): no difference but the two new fixtures. 5,650 ms against 5,634 |
| Game corpus, `hwram008.bin` | 1,554/1,554; every count 0; 20 missing and said; records 38,700 folded, 0 dead, 20 unreachable, 155 switch lookups: **the same; 0 of 1,554 outputs changed**, bodies and lends unchanged. 19,571 ms against 18,591 |
| ASan + UBSan + LeakSanitizer, gcc Debug, `Sh2DecompilerTests` (`detect_leaks=1`) | clean beyond the two tolerated UBSan lines (`address.hh:605`, `slghpatexpress.cc:979`); no AddressSanitizer or LeakSanitizer report; the suite passes (18 s), and with `SE_GAME_HWRAM=hwram007.bin` (839 functions, 138 s) |

**Wrong in the first drafts, so a later reader does not repeat them.** The first draft of the one-to-one "dropped" check matched the
trace accesses of a `mac` in trace order, so an access of ordinary RAM, which the decompiler may fold, took the one entry that belonged
to the parameter's memory read beside it: 27 failing seeds in 5,000, all false. The accesses the decompiler must list now take the entries
first. The ledger's guess for A4-3 (a use or a hold keeping the read as a variable) was half right: the load is flagged and not held;
it is a use, of a kind the fold check did not see. A debug print of the switch block's ops
and the uses of each was what showed the SUBPIECE. A build killed half way leaves objects another compiler process finished after the
header changed (the ASan build overflowed a `Funcdata` allocated by a `database.cc` compiled before `funcdata.hh` grew): the headers
were touched and everything was rebuilt before the matrix was run.

**Not done.**
- The pre-action scan (`ScanRawPcode`: the A3 delay-slot check, the literal pools to mark read-only) still reads the first flow of a pass
  only. A function jumped into after a restart's flow has its literal words read as memory, not folded, and its delay-slot PC-relative
  loads are not looked at. Not reproduced: a third function that read a hardware register through its own literal was not absorbed
  (the engine took the jump to it for a call), so I could not build the case; the worst it would be is a literal word read as
  memory, which is held and listed, not lost.
- The generator's `SaidMissing` is still true for any removal caution on the instruction, not one caution per lost access as the host's
  `SaidCount` is; mac is in version 1 only (version 2's registers-hold-a-value discipline would need it taught).
- `noInterveningStatement` still passes a COPY or SUBPIECE whatever uses its output. Patch 0024 closes it for the one op that matters
  to us (the lookup, the only load a guard may be folded over); an ALU op there has no effect to repeat.

## Area 2, round 2 review

Branch `review/2-round2-review`, at `253f058`. Reviewer only: no source or test changed. Two findings, both P1
and both outside what any oracle can see: **A2-9** (`trapa` is modelled as a call whose return leaves r15 8
bytes low, so stack reads after it are wrong) and **A2-10** (r4-r7, T, MACH and MACL read after a call as their
value before it). Everything the brief listed for the spec, the reference model, the oracle and patches 0019 and
0020 held.

*Tools.* The two suites as shipped (`SaturnExplorerSh2DecodeTests`, `SaturnExplorerSh2SemanticsTests`: both pass),
plus a scratch probe outside the tree that compiles `Sh2DecodeTests.cpp` into one file with its own `main`, so it
can use the oracle's reference (`ExecRef`, `RunRef`, `RunCase`, `GenCase`) and the emulator harness, and drive
`Sh2Decompiler` on hand-written words. Nothing of it is committed; every result below that comes from it says how
to rebuild the case.

### 1. The reference model against Mednafen

Re-derived each of the 124 runnable ops from `sh7095_ops.inc` and the PC rules of `sh7095.inc` (`DelayBranch`,
`UCDelayBranch`, `CondRelDelayBranch`, `Exception`), line by line against `ExecRef`, and the same ops against
`superh.sinc` (SH-2 constructors only). No case where the model and the spec agree with each other but not with
Mednafen. Held:

- DIV0S, DIV0U, DIV1 (Q = msb ^ M ^ carry, T = (Q == M), Rm read after the shift); the spec's arithmetic-select
  form of DIV1 is equivalent (checked by hand and by the decompiled sequence in section 5).
- MAC.L: 64-bit accumulate, with S the 48-bit bound and the direction from the sign of `m0 ^ m1` (not of the
  product, so a zero product with a negative factor saturates low; spec and model both do this). MAC.W: with S,
  MACL only, `MACH |= 1` on overflow and MACH otherwise untouched; without S, 64-bit. `Sh2SemanticsTests`'
  `RefMacL`/`RefMacW`/`RefDiv1` agree with both. The JIT helpers `SH7095_JIT_MACL_Arith`/`MACW_Arith`
  (`sh7095.inc`) are the same statements.
- DMULS/DMULU, MUL.L, MULS.W/MULU.W; ADDC/SUBC/NEGC (carry from bit 32), ADDV/SUBV (the two sign tests);
  EXTS/EXTU, SWAP.B/W, XTRCT (with Rn = Rm); ROTL/ROTR/ROTCL/ROTCR, the shifts (`shal` and `shll` one op).
- `tas.b`, and the GBR read-modify-writes (`and.b`, `or.b`, `xor.b`; `tst.b` reads only).
- `trapa`: SR pushed first, then the address after `trapa` (`z->PC -= 2` before `Exception`), vector at VBR +
  imm*4, SR unchanged. `rte`: PC then SR popped, `SR = new & 0x3F3`, then `DelayBranch`. `ldc`/`ldc.l` to SR
  mask with `0x3F3`, to GBR and VBR do not; `stc`, `sts`, `lds` and their `.l` forms; `clrt`, `sett`, `clrmac`.
  Register order in Mednafen's `CtrlRegs` (SR, GBR, VBR) and `SysRegs` (MACH, MACL, PR) matches the oracle's index.
- SHAD and SHLD do not exist on the SH-2: Mednafen leaves them illegal and the decode oracle finds SLEIGH
  accepting nothing outside Mednafen's 53,752 words.
- Access widths and counts, which the differential run does not compare (it compares values and written words):
  the probe translated one word of every op and listed its LOADs and STOREs. Every op matches Mednafen's operand
  size and count (MAC two reads, the GBR operations read then write, `tas.b` read then write, `rte` two 4-byte
  reads, `trapa` two 4-byte writes then one read).

Mednafen's fused fast paths (`FUSE_COND_BRANCH`, `FUSE_LOAD_CMPTST_BRANCH`, `_LOW_ACCURACY_` only) compute the same
T as the unfused ops; the reference follows the unfused ones, as it should.

### 2. The oracle's normalisation and coverage

- *Immediates modulo 256.* The one rewrite that hides a real difference in text: `Sh2Disasm` prints `mov #0xFC,r0`,
  `add #0x80,r2`, `cmp/eq #0x80,r0` and SLEIGH `mov #-0x4,r0`, `add -0x80,r2`. Held: the encoding and the value
  the CPU uses are the same, `Sh2Disasm` follows the Renesas manual's own convention (`MOV #H'80,R1` leaves
  `R1 = H'FFFFFF80`), the execution half checks the sign extension, and the SLEIGH listing is not shown anywhere
  in the app (`Sh2Decompiler::Disassemble` has no caller outside the tests). `trapa 0x12` against `#0x12` and the
  missing `#` on SLEIGH's logical immediates are the same (unused) listing.
- *Operand fields.* Registers in both positions, the implied R0 and GBR, displacement scale (decimal byte counts),
  PC-relative addresses at both alignments and branch targets with their wrap are all in the compared text;
  `shal`/`shll` is one op. Nothing else is folded.
- *Undefined words.* Validity is compared on all 65,536 words at three layouts. A word decoded with the slot
  context that patch 0005 sets is not part of the decode half, so the probe ran every slot-legal word Mednafen
  defines (44,213) in the slot of each of the ten delayed branches, T clear and set (884,260 cases, section 3):
  every one translates and runs.
- *The known-limit rule for A2-4.* Tight: `RunRef(..., specSlotPc)` changes only the PC a slot sees after a taken
  branch other than `bra`/`bsr`, and a case is excused only if every register, the next PC and every written word
  then agree, so a second cause in the same case still fails (an SR mask, a wrong target, a not-taken slot, a
  regression of patch 0005). The decompiler side pins only the 8 named shapes; the probe decompiled every cell the
  default run counts as known (480 distinct cells), from its branch, each alone in otherwise zeroed memory (in the
  oracle's nop-filled image every decompile runs the flow out to its instruction limit): 480 of 480 marked
  unsupported.
- A longer run, `SE_DECODE_CASES=40000 SE_DECODE_SEED=4242` with `SE_MEDNAFEN_SS` set: 5,200,000 cases, 0 differ,
  37,896 known (A2-4), the transcribed word table equal to `sh7095_idecodetab.inc`, 15.8 s.

### 3. Delay slots and the PC

- *Every slot word in every delayed branch.* The probe placed each of the 44,213 slot-legal words after `bra`,
  `bsr`, `bt/s`, `bf/s`, `braf`, `bsrf`, `jmp`, `jsr`, `rts` and `rte`, at both alignments, and ran one random case
  with T clear and one with T set: 0 differ; the known-limit cases (8,448 for each conditional branch, about 16,890
  for each register-target branch) are exactly the PC-relative slots after a taken branch. This covers the slot
  reading or writing the branch's own register, `rts` with `lds ...,pr` in the slot, `bt/s` not taken with a slot
  that writes T, and `rte` with a slot that reads SR, writes PR or moves r15.
- *A slot that is a jump target.* A function that enters at a `bra` slot and loops back to the `bra` (`A00E D104
  4415 89FB 000B 6013` from 06009000, entry 06009002, literals 0x11111111 at 06009014 and 0x22222222 at 06009030):
  both paths fold the right literal (`if (param_1 < 1) return 0x11111111; return 0x22222222;`), unflagged and
  right. A `jmp @r2` through a literal, and a `braf r0` after `mov #4,r0`, into a `bra` slot: both flagged
  unsupported (the resolved jump is a jump table, so `ScanRawPcode` sees the target).
- *A branch to its own slot* (`bt/s`, `bf/s`, `bra` with displacement -1): decompiled as the hardware runs it (the
  slot twice when taken), with the engine's overlap warning.
- *`bsrf`/`braf` at the top of the address space*: covered by `PickTarget`'s top and bottom targets; agrees.
- *Cached parses and context.* `Sleigh::obtainContext` reuses a cached parse without comparing the context it was
  resolved under, so a `bra` slot parsed before the `bra` commits its context would keep the slot's own PC. The
  flow cannot reach it: a slot parsed first (as the entry) is followed by its fall-through, which evicts it before
  the `bra` is translated; a slot parsed first as a jump target is flagged. `ReturnCheck` can see either context on
  a second pass, but takes every load from a constant address as memory (`Loaded`), so the literal's address does
  not matter there. Held by argument; no oracle exercises it.

### 4. Patches 0019 and 0020

- 0019: the `rte` order (SR before the slot) and the masks are exercised by every slot word above; no difference.
- 0020: a history sweep of 400,000 translations over 48 contiguous `[branch, slot]` pairs (no `bra`/`bsr`, whose slot
  is meant to translate differently once the branch has committed), in four request orders: any address, a
  three-address cycle over branches and slots, branches and slots of four neighbouring pairs (jumps into the middle
  of a cached pair), and branch-or-slot at random. 0 translations differ from a cold one. With patch 0020 reverted
  (a scratch copy of `sleigh.cc` linked into the probe) the same sweep finds 285, so it has teeth. `ReturnCheck::Step`
  re-steps in address order; with translation independent of history its order cannot matter.

### 5. The decompiler's view

Decompiled shapes the emulator gets right, and read the output against the hardware: MAC.W with S set and MACL
from an argument (the saturation test, `0x7fffffff`/`0x80000000` by the product's sign), MAC.L with S set (48-bit
bounds, direction from `m0 ^ m1`); `div0u ; div1 r5,r4 ; div1 r5,r4 ; rotcl r4` (the printed C transcribed and run
on 200,000 inputs against the reference: identical); 64-bit `addc` and `negc` chains, `shll ; rotcl`, `addv`,
`subv` and `cmp/str` into `movt`; `rte ; movt r0` (reads the stacked SR's T); `bt/s` not taken with `sett` in the
slot; `jmp`/`jsr` whose slot overwrites the target register (the target is the old value). All read as the
hardware behaves. Two do not, and no oracle compares a printed value with a run:

- **A2-9**, `trapa`. The spec makes it `call [vector]` after pushing 8 bytes, and the default prototype keeps r15
  across a call, so after it the decompiler's r15 is 8 lower than the hardware's (the handler's `rte` pops them).
  `sts.l pr,@-r15 ; mov.l r4,@-r15 ; trapa #1 ; mov.l @r15+,r0 ; lds.l @r15+,pr ; rts ; nop` prints
  `return 0x6009006;`, the address `trapa` pushed, where the hardware returns r4.
- **A2-10**, a register the cspec neither keeps nor kills (r4-r7, SR, MACH, MACL) is merged across a call into
  one variable, so `mov #5,r7 ; bsr f ; nop ; mov r7,r0` prints `xVar1 = 5; func(); return xVar1;` and a
  `bt` on a T returned by the callee is printed as decided by the T from before the call. r2, which the cspec
  kills, prints `extraout_r2`. Likely area 3's to fix (the cspec); filed here for the T flag.

Seen on the way, not filed: a `rts` in the delay slot of a `rts` makes the request fail with the engine's text
"Could not obtain cached delay slot instruction": an illegal slot, already accepted under A2-8 (b), and reported,
not silent.

### 6. Held, in one line each

- The reference model: every op equal to Mednafen; no shared transcription error with the spec.
- Access widths and counts of every op's p-code: equal to Mednafen's.
- Decode normalisation: only spelling folded (the immediate sign is the Renesas convention; the SLEIGH listing is unused).
- Undefined words: compared on all words; every defined slot-legal word also translates and runs in each delayed slot.
- The A2-4 known-limit rule: excuses nothing but the one rule; decompiler flagging checked on every known cell.
- Delay slots: all slot words after all delayed branches agree (884,260 cases); slot-as-target, branch-to-own-slot,
  slot writes the branch register, top-of-memory `braf`/`bsrf`.
- Patch 0019 (`rte` order and masks) and patch 0020 (LRU): history sweep clean, and fails with 0020 reverted.
- MAC saturation, DIV1 sequences, carry chains and SR-reading slots read correctly in the pseudocode.

## Area 5 round 3 and area 4 round 2 review

Reviewer, branch `review/5-round3-review`, at `d781cb7` (patches 0001 to 0024). Findings only; no source or test
changed. Every fixture is in `Docs/Decompiler/REVIEW_LEDGER.md` (A5-9 to A5-17, A4-4 to A4-6, and A3-1 for the area 3
owner) with its words or replay line, the current and expected output and the oracle's verdict. Reproduced with
`SaturnExplorerSh2GeneratedTests` (`SE_GENERATED_WORDS`, `_FUNCS`, `_TABLES`, `_DATA`, `_RETURNS`, `_POLL`,
`SE_GENERATED_VERBOSE=1`), RelWithDebInfo, gcc. Mednafen read for each width rule: `sh7095.inc` (`SH7095_OnChipRegWrite_u8`,
`_u16`, `_u32`, `SH7095_OnChipRegRead_INLINE_u8`, `_u16`, `_u32`, `SH7095_BSC_BusWrite_u32`, `_BusRead_u32`), `ss.c`
(`BusRW_DB_CS0_u8_W0`, `_u8_W1`, `_u16_W1`, the FRT trigger region), `scu.inc` (`SCU_RegRW_DB_u8_W0` to `_u32_W1`,
`SCU_FromSH2_BusRW_DB_u8_W0`, `_u16_W0`, `ABus_Read`, `ABusRW_DB_u16_W0_SH1`), `vdp1.c` (`WriteReg`, `ReadReg`, `VDP1_Write8_DB`),
`vdp2.c` (`RegsRead`, `RW_W8`), `smpc.c` (`SMPC_Read`, `SMPC_Write`), `cdb.c` (`CDB_Read`).

**Area 5 is not clean.** No P1 in the register handling itself; seven P2, all of one kind: a width or a lane the map does not
model, so the line prints a part of the register, or names two, with no caution or the wrong one. None has an instance in
either save state. The one P1 of the round (A4-4) is a control-flow error that removes a hardware write.

**Not held.**

- **A4-4 (P1).** A jump table one of whose entries is the default's own destination: once the bound check is folded into the
  switch, the join is decided from the switch block's register values (`mova`'s r0) on the default path too. A reachable block
  holding a hardware write is removed as unreachable, no caution says so (the all-volatile reference run removes the same
  block, so the removal check has nothing to compare), and a signature is lent with a wrong return. Found by the generator in 2
  of 15,000 version 2 seeds (68620, 107923); a 25-word fixture. With the entry pointing to its own `bra join`, the output is
  right, so the shared destination is the trigger; the fixer's note on patch 0024 (`noInterveningStatement` passes a COPY
  whatever uses it) names the likely root.
- **A5-9 (P2).** A 4-byte access to the 8- and 16-bit on-chip registers below 0xFFFFFF00 is a CPU address error (Mednafen sets
  `SH7095_PEX_CPUADDR`, a read returns 0); the output says "one 4-byte write covers SH2_ICR and SH2_IPRA", or nothing
  (`xVar1 = _SH2_VCRWDT;`).
- **A5-10 (P2).** A 16-bit access there reaches only the register at the word's own address, with the value's low byte; the odd
  register is neither written nor read. The "covers SH2_TIER and SH2_FTCSR" caution and the FTCSR arming note are wrong; a
  word at 0xFFFFFE90 or 0xFFFFFE70 does nothing and prints by address with no note.
- **A5-11 (P2).** A byte or word write to the SCU's DSP ports (PPAF, PPD, PDA, PDD) writes the whole register from the data bus
  (unmasked, unlike every other SCU register); printed `SCU_PPD._3_1_ = 3;` with no note.
- **A5-12 (P2).** A 4-byte write to BCR1 to RTCOR without 0xA55A in the upper half does nothing; printed `SH2_BCR1 = 3;`. The
  round 2 fixer saw it and did not put it in the ledger.
- **A5-13 (P2).** A byte write to MINIT or SINIT sends no signal (the CS0 byte path is empty there); a long sends two. Printed
  `MINIT._0_1_ = 3;` and `_MINIT = 3;` with no note.
- **A5-14 (P2).** A 4-byte access over one 16-bit register and an unnamed half (VDP2 PRIR and the reserved word after it,
  RAMCTL after the reserved 0x0C, VDP1 ENDR, SCSP DMEAL, a long read of CD_HIRQ) gets no caution: the "covers" caution needs two
  named registers. `_VDP2_PRIR = 3;` writes PRIR with 0.
- **A5-15 (P2).** Narrow reads on the A and B bus are 32-bit bus reads in Mednafen: a word read of VDP2_TVMD also reads
  VDP2_EXTEN (which latches HCNT and VCNT), a word read of the CD FIFO at 0x25818000 pops two words. Nothing says so.
- **A5-16 (P3).** Read kinds the map lacks: EXTEN's latch and TVSTAT's flag clear on read, SCU_AIACK reads 0, SH2_CCR's read
  mirrors 0xFFFFFE93-0xFFFFFE9F, a byte write to VDP2 color RAM writes the whole entry.
- **A5-17 (P3).** A byte at an even SMPC address prints as a part of the register beside it since the word views
  (`SMPC_COMREG._0_1_ = 0x19;`); the caution says it does nothing.
- **A4-5 (P3).** An indirect jump whose one destination is known only after the first flow, to code nothing else reaches and that
  returns, is taken for a tail call (`braf` over data, `jmp @r1` through a literal, a table whose guard admits one index):
  warned at the jump, signature withheld, but the destination is not in the body and the shown function is `void`. The generator
  rejects it (`outside-body`).
- **A4-6 (P3).** The generator reports `trace-outside` for a callee reached through the cache-through mirror (its `UnitOf` does not
  canonicalize); the output is right.
- **A3-1 (P3, for the area 3 owner).** The generator's `param-dropped` check counts a read through a pointer register on the path
  where the code tested it to be 0 (address 0, ordinary memory): seeds 101046 and 107035. The output is right.

**Held**, one line each:

1. A5-3's rules, each against Mednafen: a byte write to VDP1 and VDP2 registers writes the whole word (VRAM is masked, the
   registers are not); a word write to a 32-bit on-chip register at +0 writes it whole, at +2 nothing; a word read returns the
   low half at both; a byte access is an address error; the BSC takes no word write. Through the mirrors (DVCR at 0xFFFFFF28,
   DVSR at 0xFFFF7F00) the caution is right. No false alarm found among the existing cautions.
2. A5-4's SMPC lane rules: a word at the even address (cached, cache-through, 0x3010001E through bits 28-27, 0x2017FF9E in the
   last 128-byte repeat), a long over two registers (0x2010001C, 0x00100080, a long read of 0x20100020), word and long reads
   through GBR (`mov.w @(0x62,gbr)` names SMPC_SF), a byte at the odd address unchanged. An even-address byte read calls
   `SMPC_Read` of the same register, which has no side effect, so "returns 0xFF" is the whole story. Only A5-17.
3. A5-5's matching by address: `mac.l` with one constant register operand and one through a pointer, both removed with
   unreachable code: two cautions, one by name and one "through a pointer"; both operands through pointers: two. A frame record
   (`AccessFate::frame`) is any record outside the ram space, which only the stack's folds are; no shape makes one stand for a bus
   access.
4. Patch 0021: a restart that reaches less than the first flow could not be built (a restart re-follows with more tables, and no
   callee is ever lent as not returning). `bsrf` to a known function, a backward `braf`, a `braf` into an `else` that falls into
   the join, and the SH compilers' `mov.w @(r0,r4),r0 ; braf r0` switch of three entries are right (0 findings). Flow into the
   middle of another known function gets no warning, by design (only entries are known). The scan before the actions still
   reads the first flow only (the round 1 fixer's note): not built here either: in the one attempt the second hop was taken for a call when
   the third function read a register through its own literal.
5. Patch 0024: the lookup used across the default through a MULTIEQUAL with a second guard (`cmp/pz ; bf ; cmp/gt ; bt`), the
   lookup used by one case only, and a hardware read between two guards all keep their reads under the guards (0 findings).
   The switchFold control on the game corpus: 27 and 155 switch lookups, as before. A4-4 is a COPY in the switch block, not
   the lookup.
6. The generator's holes, one hand-made module each: `mac.l` in a module (a callee `clrmac ; mac.l @r5+,@r4+`): 0 findings, two
   reads listed. The address of a local handed to a callee: 0 findings (the caller is withheld, "a return not to its caller":
   conservative, a question for area 3). A fifth argument on the stack: 0 findings (the callee withheld, "an incoming argument
   outside its signature"; the call prints five arguments). Recursion and mutual recursion, with the depth clamped: 0 findings,
   both withheld. A mirrored callee: A4-6. `SaidMissing` per instruction: no module can reach it. An unmarked poll is answered
   only in the marked request's run, where nothing is removed, so no run reaches code the default request removed (the A5-5 `mac`
   after a poll: "0 of 3 runs finished", the trace stops at the poll). The check is dead in practice. A4-4 is the first shape
   where the emulator runs removed code, through flow rather than a poll; its findings are `removed-unsaid`, which `SaidMissing`
   would excuse for any caution on the instruction.

**For the coordinator.**

- Area 3: an unknown or withheld callee is printed as leaving r1 and r4-r7 unchanged (the cspec's `killedbycall` names only r2
  and r3). The clamped recursion prints `return iVar1;` with `iVar1 = (param_1 & 3) - 1`, where every run returns 0. The signature
  is withheld, so nothing wrong is lent, but the text reads as a value. Not filed as a row (area 3, and Ghidra's default for an
  unknown effect).
- Seed 104955 (version 2) fails the generator's own contract (`generator:no-return`, modes 1 and 3), not the decompiler's.
- The default 300 seeds and the A4-2/A4-3 seeds do not reach A4-4 (about 1 in 7,500 version 2 seeds), so the ctest entry stays
  green; the nightly run of 20,000 will meet it.

**Runs.**

| | |
|---|---|
| Generator, both grammars, `SE_GENERATED_FIRST=60001`, 10,000 seeds | 1 failing seed (68620, A4-4), 2 findings; 132,679 accesses traced; 194 s |
| Generator, version 2, `SE_GENERATED_FIRST=100001`, 10,000 seeds | 4 failing seeds: 107923 (A4-4), 101046 and 107035 (A3-1), 104955 (`generator:no-return`); 173 s |
| Hand-made replays | about 50, listed in the ledger rows; the A5 ones give 0 findings (the generator has no register widths) |
| Game corpus, `hwram007.bin` | 839/839; every count 0; 16 missing and said; records: 14,842 folded, 0 dead, 16 unreachable, 27 switch lookups |
| Game corpus, `hwram008.bin` | 1,554/1,554; every count 0; 20 missing and said; records: 38,700 folded, 0 dead, 20 unreachable, 155 switch lookups |
| Width instances in the corpus | none: no `_SH2_` long or word over the on-chip byte registers, no narrow SCU DSP, MINIT or SINIT write, no `covers` caution; only `SH2_BCR1._2_2_` (right) |

## Areas 2 and 3, round 2: trapa's stack and the registers a call kills

Branch `review/2-3-callconv-fix`, from `276a4b4`. Fixes the two P1 findings of "Area 2, round 2 review" (ledger A2-9 and
A2-10), which no oracle could see because none compares a printed value with a run, and adds the oracle that does. Two
patches, both to the spec: `0025-sleigh-trapa-stack.patch` and `0026-cspec-call-clobbers.patch`. The coordinator's addendum
(the round-3 review of areas 4 and 5 found A2-10 from the other side: a call to an unknown or withheld function printed as if it
left r1 and r4-r7 alone, and a recursive function printed a wrong value) is covered: r0 and r1 are in the list, and a
recursion fixture is in the oracle.

### A2-9: trapa (patch 0025)

*Cause.* `trapa` is `r15 -= 4; *r15 = sr; r15 -= 4; *r15 = inst_next; call [*(vbr + imm*4)]`, and the cspec keeps r15 across a
call, so the decompiler's r15 after it was 8 below the hardware's: the handler's `rte` pops both words. A register saved before
the trap and restored after it read back the address `trapa` pushed (`return 0x6009006;`), and a word at r15 read the pushed SR
(`return in_sr;`).

*Fix.* `r15 = r15 + 8` after the `call` in `trapa`. The differential run needs the push and nothing else, and it gets it: the
p-code emulator ends an instruction at its call (`EmulatePcodeCache::executeInstruction` loops until `instruction_start`, and
`executeCall` sets the next address), so the op after the call is never run by it, and the state the emulator reaches after
`trapa` alone is still Mednafen's (r15 down 8, SR and `inst_next` pushed, PC at the vector). The decompiler carries on after a
call and sees the 8 bytes given back. `rte` is untouched, so decompiling a handler still pops PC and SR. A cspec route was
looked at and not taken: `callfixup` matches a callee by name and this call is through the vector table, and `extrapop` is per
model, not per call, so it would change every call.

*Result.* `4F22 2F46 C301 60F6 4F26 000B 0009` prints `return param_1;`; `C320 60F2 000B 0009` prints
`return in_stack_00000000;` (the caller's word) and with `50F1` `in_stack_00000004`; the entry r15 read after a trap is
`&xStack00000000`. Corpus: the one function with a `trapa` in each image (`06024ACC` and `0606476C`) no longer prints
the pushes it made for the handler, as the stores are dead once the read of the pushed address is gone (one access fewer
"through a pointer" per image, accounted as folded into ordinary memory).

### A2-10: what a call leaves (patch 0026)

*Cause.* The default prototype's `killedbycall` held r2 and r3. A register in neither list has the unknown effect, which the
engine prints as the value before the call. The review listed r4-r7, SR and MACH/MACL; the addendum added r1 (and r0): they are
outputs only while the return is open or 64-bit, so once a signature is lent as void or 32-bit, `guardCalls` no longer
treats them as outputs and r1 read as before the call as well (`f` lent void, `mov #5,r1 ; bsr f ; mov r1,r0` printed
`return 5;`).

*Fix.* `killedbycall`: r0, r1, r2, r3, r4-r7, MACH, MACL, SR. The SH ABI of both compilers has r0-r7 and MACH/MACL
caller-saved and T clobbered; the engine tracks SR whole, so SR is killed, which also leaves the interrupt mask and S, Q, M
unknown after a call (a callee can change them with `ldc`). VBR joins GBR in
`unaffected`: no compiled code touches it, and without an entry it was in the same unknown state r4 was. `unaffected` r8-r14,
r15 and GBR are as before. A lent prototype keeps the default model's lists (`ApplyPrototype` sets `pieces.model =
arch.defaultfp`), so lending changes none of this; the lent fixtures confirm it.

*What the text now says.* `xVar1 = 5; func(); return xVar1;` is `func(); return extraout_r7;`; the T fixture is
`if ((extraout_sr & 1) == 1)`; MACL and MACH read `extraout_macl` and `extraout_mach`; r8 (callee-saved) still reads 5; an
argument held in r8 across a call is still `param_1`; the recursion `f(n)` that sets r1 or r7 before calling itself and a base case
that sets it differently prints `extraout_r1` / `extraout_r7` (its signature is withheld, "it calls itself", as before).

*Prototype inference.* A register read after a call is a call output (`isExtraOut`), and `WithholdReason` refuses to lend a
signature on one ("it uses r4 after calling X, which is not known to return it", already there for r0-r3). So a caller that
reads r4-r7 after a call is no longer lent a signature, and in isolation it has lost the parameter: the old text read the
entry value through the call, the new reads the callee's. On the other side, the call's arguments are now shown: with an unknown effect the engine
left r4-r7 out of a call's argument list when the call looked to modify them.

### The oracle: a printed value against a run (`TestCallsAndTraps`)

`Sh2DecompilerTests.cpp` runs each fixture on the p-code emulator (the shipped translator, the callee or a `rte ; nop` trap
handler in memory, VBR pointing at a table of handlers), twice or more with different inputs, and evaluates the pseudocode's
`return` for the same inputs (`PseudoEval`: straight-line code, `if`/`else`, 32-bit integer expressions, the parameters taken
from r4-r7), and requires the register the code returned to be among the values the text can return. Limits: a value it cannot
know (a call's result, `extraout_rN`, a memory read) agrees with anything, so it finds a text that states a wrong value, never
one that states too little (the expected-text checks beside it pin that); loops, `goto` and `switch` are rejected loudly; casts
are ignored and compares are signed; the inputs are a few points, not a proof. Its own checks run first: the pre-fix texts of the
four ledger fixtures are evaluated and must be flagged, and a loop must be rejected, so a broken evaluator cannot make everything
agree. The fixtures: the two trapa fixtures of the ledger, trapa with a local, two traps in a row, the entry r15; r4, r5, r6
and r7 after a call that sets them; the ledger's r7 and T fixtures word for word; T set by the callee; MACL; MACH; r1, r2
(controls); r8 across a call and an argument kept in r8 (controls); a `trapa` after a call (VBR is not `extraout_vbr`);
recursion through r1 and through r7; r7 and r1 with the callee's signature lent; and a pinned limit, below.

`SaturnExplorerSh2SemanticsTests` gets `TestTrapaStack`: the emulator enters the handler with r15 down 8, SR at r15+4 and the
next address at r15, and the handler's `rte` returns with r15 and SR as they were.

`TestPrototypeEligibility`'s caller that stores a register's low word after a call held r4 and r5 across the call, which the
cspec now reads as `extraout_r4`/`extraout_r5`; it holds them in r8 and r9 now (same intent: a sub-word store of a register the
caller owns keeps its signature), and checks the text. `TestGameCode`'s `SE_GAME_OUT` files also say whether a signature is
lent or why it is withheld, so two builds' outputs differ where that changes.

### Mutation

Each spec change reverted alone (or one entry of the list dropped), the sla regenerated, the suites run:

| Reverted | `Sh2DecompilerTests` | `Sh2SemanticsTests`, `Sh2DecodeTests`, generator (600 seeds) |
|---|---|---|
| patch 0025 (no pop after the call) | fails: the ledger's a and b, the local, the two traps and r15 fixtures, each with the oracle (`the code returned 11111111, the pseudocode returns 06009006`) and the expected text | pass: **the decode test's differential run does not see the trapa change**, as designed (one instruction, and the emulator never runs the op) |
| patch 0026 (whole) | fails: `vbr` text, r4-r7, r7, T (both), MACL, MACH, with the oracle on each | pass |
| r0 and r1 dropped | fails: the lent r1 fixture, text and oracle (`returns 00000005`, the code returned 7) | pass |
| SR dropped | fails: both T fixtures, text and oracle | pass |
| MACH and MACL dropped | fails: MACL and MACH, text and oracle | pass |
| r4-r7 dropped | fails: the four argument registers, r7, the r7 recursion, the lent r7 and r4 limit | pass |
| VBR dropped | fails: the two `unaff_vbr` texts | pass |

So the area-2 oracle and the generator cannot see either defect, which is the finding's own claim; only the new check can.

### Corpus

Both images (`SE_GAME_HWRAM` hwram007 and hwram008), every ledger count 0, 16 and 20 reads "missing and said" (code
removed as unreachable) as before. Against the previous master (`SE_GAME_OUT` compared file by file):

| | functions | text differs | declaration (signature line) differs | call sites with more arguments | with fewer |
|---|---|---|---|---|---|
| 007 | 839 | 87 | 14 | 74 | 1 |
| 008 | 1554 | 136 | 20 | 112 | 0 |

`extraout_` names appear in the changed functions (r4 in 57 and 90, r7 in 35 and 60, r6, r5, SR in 10 and 11, MACL in 6 in the second);
`in_sr` is gone from the 10 and 11 functions that had it (it became `extraout_sr`). Three read, with the disassembly:

- `06024ACC` (007): `cmp/gt r1,r4 ; bf ; trapa 0x9` after five calls. Before: the pushes of the trap and a read of the pushed
  word were printed, `in_sr & 0xfffffffe | ...` carried a T from before the calls, and `param_1` was compared. Now the trap is a
  call through `unaff_vbr + 0x24`, the T logic is `(*(uint2 *)(unaff_gbr + 0x80) & 0x10) != 0`, and r4 after the calls is
  `extraout_r4`. Right for A2-9 and for T; the function lost its parameter, which is the ABI's answer (the author saved r5-r7 and
  not r4 around calls elsewhere, and may count on the callees keeping it: not decidable here).
- `0600847E` (007): a call to `060198DE`, which is eleven `shar r4`: a shift-by-11 helper that returns in r4. Before:
  `iVar2 = func_0x060198de(); param_1[2] = iVar1 + iVar2;`, the sum with r4 as it was before the helper shifted it: a wrong value,
  silent. Now `iVar1 = func_0x060198de(iVar1); param_1[2] = extraout_r4 + iVar1;`: the argument is shown, and the shifted r4 is
  the unknown it should be. This is the A2-10 defect in the game's own code (74 and 112 call sites gain an argument; the
  `060198DE`/`E4`/`E6` and `0605A12C`/`26` helpers are the common ones).
- `06027844` (007): five calls with r5-r7 pushed around some and r4 not, then `func_0x06026174(param_1, ...)`. Now
  `extraout_r4` and no parameter. The old text may have been right if the callees keep r4 (they are hand-written); the new one is
  honest and loses information. This is the limit below, seen in code.

### Limits, and what is left

- A callee verified to keep r4-r7 (`CallEffects.preserved`, which `ReturnCheck` proves and the host already uses to prove a return
  address) is not told apart from one that does not: the default model's lists are per model, and `FuncProto` takes an effect list
  only through its XML decoder. Pinned by the `r4 kept by the callee (limit)` fixture. The remedy is a small engine patch
  (a setter for the effect list) and `ApplyPrototype` marking `preserved` registers unaffected for a lent callee; it needs the
  engine and `Sh2Decompiler.cpp`, which this round was not to touch. Measured cost: of the generator's 5,648 lent prototypes
  (3,000 seeds), 207 (3.7%) are no longer lent, all for "it uses rN after calling ...".
- The corpus runs lend nothing (every function is decompiled alone), so it does not measure lending; the generator does.
- `trapa` through a handler that does not return (a debugger trap) is modelled as returning, as every call is.
- VBR unaffected is a judgement, as GBR's was: a callee that loads VBR is read as not changing it.

### Matrix (RelWithDebInfo)

`SlaFreshnessTests`, `Sh2SemanticsTests`, `Sh2DecodeTests`, `Sh2DecompilerTests` pass; `Sh2GeneratedTests` passes at the default
300 seeds and at 3,000 (both grammars, 1,500 each; 0 failing seeds, 0 findings); the game corpus above; `vendor.py` on the pinned
Ghidra (`918d44e`, a sparse checkout) reproduces the committed tree, `sh-2.sla` aside, with 0025 and 0026 applied. The whole
`ctest`: 95 of 95 pass (RelWithDebInfo, the final spec data).

## Area 4, round 2: the default that is also a case

Fixer, branch `review/4-round2-fix`, fix commit `c34ac94`, on `5ecc36d` (patches 0001 to 0024; this adds 0027, and
`vendor.py` on a sparse clone of the pinned Ghidra (`918d44e`) reproduces the vendored tree exactly with it). Ledger rows A4-4
(P1), A4-6 and A3-1 are `fixed c34ac94`; A4-5 is `accepted` with the reasoning below. The generator's own contract bug (seed
104955, `generator:no-return`) is fixed. The engine change is one function (`JumpBasic::foldInOneGuard`), about 14 functional
lines; the rest is the generator and a test block.

### 1. A4-4: the mechanism

The fixture (`mov #4,r0` on the default path; `mova table,r0` in the switch block; a join that tests `r0 & 0x10` and writes
SCU_IST under it) has a MULTIEQUAL at the join: r0 = (the guard block's edge: the 4 of `mov #4,r0`; the switch block's edge: the
`mova` COPY, `#0x6009014`). Entry 1 of the table is the join itself, so when `foldInOneGuard` folds the bound check into the
switch the guard's target is already one of the switch block's destinations (`pos != sizeOut`), and it takes its second branch:
it sets the guard `CBRANCH`'s condition to a constant (the branch now always enters the switch block) and marks the existing
destination as the default. The edge from the guard block into the join is then dead and goes, and with it the MULTIEQUAL's input
4: the join keeps one edge, the switch block's, and r0 at the join is the `mova` address for the default path as well. Then
`tst #0x10,r0` folds to a constant, `bf` is always taken, the write is unreachable and removed ("Removing unreachable block"), and
the function returns `0x6009014` for every index (the emulator returns 4 for an index of 2 or more). Nothing refused it because
`noInterveningStatement` (upstream's "the switch block makes no value used outside it") passes a COPY or SUBPIECE whatever uses
its output, and the `mova` is such a COPY: the round 1 fixer's note on patch 0024 named it. A temporary print in the fold showed
the two inputs: `r0(0x06009002:2)` against `r0(0x06009010:12) = #0x6009014`, the second defined in the switch block.

The other branch of the fold (`pos == sizeOut`: the guard's target is not a destination yet) is right, and the ledger's control
(entry 1 at its own `bra join`) is that branch: `pushBranch` moves the guard block's out edge to the switch block and the join's
MULTIEQUAL keeps its input for that edge, so the default path still carries its own 4 whatever the switch block computes.

*Fix, patch 0027.* When the guard's target is already a destination, the fold is refused if any MULTIEQUAL of that block takes a
different Varnode from the edge of the guard block and from the edge of the switch block. The guard stays an `if`, which assigns
the default path's values. Not under `loadsNotDuplicated`: it is a miscompile of the upstream fold, whatever is done with loads.
The fixed output of the 25 words, which the generator checks against the emulator:

```
uVar1 = 4;
if (param_1 < 2) { uVar1 = 0x6009014; switch(param_1) { case 0: uVar1 = 0x6009014; } }
if ((uVar1 & 0x10) == 0) { SCU_IST._0_1_ = (char)uVar1; }
return uVar1;
```

*Suite.* `TestDefaultAlsoCase` (a separate block after `TestFlowReachedBody`, with its own snapshot): the ledger's 25 words at
0x06009000: `Ledger` `{ Wr(SCU_IST, 1) }`, the write's block in the body, no `Unreachable` record, no removal warning, `& 0x10` and
`SCU_IST` in the text, `if (param_1 < 2)` and `= 4;`, not `return 0x6009014;`, `ReferenceOk`; the control (entry 1 at its own
`bra join`); and a default that is also a case whose join reads nothing the switch block computed (`rts ; mov #5,r0`), which must
still fold into a `switch` with no `if`.

*Oracle.* The generator replay of the 25 words (ledger A4-4): 0 findings (the trace writes SCU_IST once, the output lists it
once); seeds 68620 and 107923 pass. The switchFold control stays folded; the corpus's switch lookups are 27 and 155, as before.

### 2. A4-5: accepted, from the design

An indirect jump with one destination known after the first flow is a *tail call*: the engine turns it into a call and the host
warns "taken for a tail call to X, assumed to return to this function's caller". The code at X is a function of its own. That is
the design's rule for every jump the flow does not follow, and the suite already pins it from the other side:
`TestFlowReachedBody` has the second jump into an address nobody named, with the verdict "a tail call to it, not flow into it: one
warning, and nothing of F2 in the body". Putting the destination in the body for the three shapes of the row would make the body
disagree with the pseudocode, which shows a call there and not the code, and would make the panel's "address in this function"
test and the mapping (area 7) claim instructions the output does not contain. The signature is withheld (it calls an address
whose signature is not known, or "an indirect call it does not resolve"), so nothing wrong is lent; and the destination is
visible by naming it (a function start at X: the callee is decompiled, and a caller lent its signature prints
`func_0x0600900a(param_1)`; `TestTailJumpToUnreachedCode`). So:

- The warning says more: "...; the code there is not part of this function's body" (also for an address the code computes;
  an uncaptured destination keeps its "what runs there is not shown"), and the `DECOMPILER_GUIDE.md` row says the same and how
  to see the code.
- The generator's `outside-body` check takes what a run executes after a jump the output warns of as a tail call (or the engine
  of as an indirect jump it treats as a call: shape c, a table whose guard admits one index and whose word the engine reads as a
  pointer in RAM) as named by that warning, unless another run executed the same instruction before any such jump. The
  lent-prototype check still sees those instructions as outside the body: a signature lent with one is `lent-incomplete`.
  Shapes a, b and c of the row give 0 findings; the control with two entries stays a switch.
- Not changed: shape c would read `return 2;` if the table's word were known to be read-only. The host marks only the literal
  pools of PC-relative loads as read-only; a table reached through `mova` is read as ordinary memory, because a table in RAM may
  be edited. Marking it is a design change with its own risk (a table the code patches), and the output now says what it is
  (a call through a word in RAM). Left as it is.

### 3. The generator: A4-6, A3-1, seed 104955

- **A4-6.** The trace's instruction addresses (`TraceRec::insn`, the run's `pcs`) are canonical (`Env::Canon`), so a callee run
  through the cache-through mirror is function 1 at its canonical address and not "outside every function". The row's replay: 0
  findings.
- **A3-1.** The finding was not the check but the world it perturbs. A read of memory that is neither work RAM nor a register
  (the BIOS area: `mov.b @r5,r1` with r5 = 0) drew the next value of the hardware's pseudo-random sequence, so a perturbed run
  that made one read more saw other values from the registers after it, the traces differed in a *value* and the check called r5 a
  parameter. Ordinary non-RAM memory now has a value of its own by address, and the sequence is advanced only by hardware.
  Seeds 107035 and 101046 pass; the perturbation check is otherwise as it was (a read of hardware or of the parameter's own memory
  through the perturbed register still counts).
- **Seed 104955.** The second grammar's discipline says a register "holds a value" when every path to the instruction defines
  it, and after a jump table it kept r1 as the bound. On the paths through a case r0 is the `mova` address and r1 the table entry
  (a code address); a call's argument `mov r1,r6` after `add #0x6B,r1` passed a pointer into the program's own literal pool to a
  callee that stored through it (`mov.w r1,@r6` wrote the low half of the literal a later `jmp @r2` loaded), and the run restarted
  the function until the step limit. A function with a jump table now passes r0 to r3 on as arguments only as an integer
  (`mov #imm,Rk` in its place; the draws are the same, so every other program of a seed is unchanged: 107923 is the same
  program, 68620 differs in one word, `mov r0,r5` to `mov #0x20,r5`). The no-return finding says where the run stopped.

### 4. Matrix

**Mutation** (reverted by a temporary environment variable in a scratch build, not by editing the patch or the checks; restored,
all pass):

| Reverted | Fails |
|---|---|
| patch 0027's check | `TestDefaultAlsoCase`: 5 checks (ledger, body and no removal, text, `if (param_1 < 2)`, not `return 0x6009014;`); the 25-word replay (`dropped:W1:hardware:removed-unsaid`); seeds 68620 (two writes, hardware) and 107923 (param); the corpus is unchanged |
| the check made to refuse every fold of this shape | `TestDefaultAlsoCase`: 1 check (the fold that must still happen prints `switch(` and no `if (`) |
| canonical trace addresses | the mirror replay: 1 finding (`trace-outside`) |
| the value of ordinary non-RAM memory by address | seeds 107035 and 101046 (`param-dropped:r5`, `r7`) |
| the tail-jump naming rule | the row's shapes a, b and c: 3, 3 and 2 `outside-body` findings |
| integer arguments from a table function's r0 to r3 | seed 104955 (`generator:no-return`) |

**Matrix** (RelWithDebInfo, gcc; the machine ran other work):

| | |
|---|---|
| `ctest` | 95 of 95 passed (59 s with -j3) |
| `SE_GENERATED_GRAMMAR=2 SE_GENERATED_SEEDS=5000` | 0 failing seeds, 0 findings (184 s). 9,854 functions, 11,046 prototypes lent (4,375 void, 6,671 returning; 803 withheld), 779 parameter candidates all insensitive, 43,160 emulator runs |
| `SE_GENERATED_SEEDS=5000` (both grammars) | 0 failing seeds, 0 findings (178 s) |
| `SE_GENERATED_FIRST=60001 SE_GENERATED_SEEDS=10000` (the A4-4 window, both grammars) | 0 failing seeds, 0 findings (250 s); the review run had 1 (68620) |
| `SE_GENERATED_GRAMMAR=2 SE_GENERATED_FIRST=100001 SE_GENERATED_SEEDS=10000` | 0 failing seeds, 0 findings (247 s); the review run had 4 (107923, 101046, 107035, 104955) |
| Seeds 68620, 107923, 104955, 101046, 107035 (`SE_GENERATED_SEED`, `SE_GENERATED_GRAMMAR=2`, and with the default choice) | each passes; the 68620 and 107923 programs still reach A4-4 (the mutation above fails both) |
| Game corpus, `hwram007.bin` | 839/839; every count 0; 16 missing and said; records 14,842 folded, 0 dead, 16 unreachable, 27 switch lookups; **0 of 839 outputs changed** against the same build with patch 0027 off (`SE_GAME_OUT`, compared file by file; the patch refuses no fold in the corpus). 6,846 ms against 6,954 |
| Game corpus, `hwram008.bin` | 1,554/1,554; every count 0; 20 missing and said; records 38,700 folded, 0 dead, 20 unreachable, 155 switch lookups; **0 of 1,554 outputs changed**. 22,022 ms against 22,721 |
| `vendor.py` on a sparse clone of `918d44e` | reproduces the vendored tree exactly (84 library sources, 94 headers, 5 spec files; patches 0001 to 0027, less the numbers other branches hold) |
| ASan + UBSan + LeakSanitizer, gcc Debug, `Sh2DecompilerTests` (`detect_leaks=1`) | clean beyond the two tolerated UBSan lines (`address.hh:605`, `slghpatexpress.cc:979`); no AddressSanitizer or LeakSanitizer report; the suite passes (17 s), and with `SE_GAME_HWRAM=hwram007.bin` (839 functions, 128 s) |

**Wrong in the first drafts, so a later reader does not repeat them.** The first fix for seed 104955 cleared r0 and r1 from the
"holds a value" set after a table. That changed the draws of every later choice, so seeds 68620 and 107923 stopped being the
programs of the ledger (A4-4 no longer reproduced from the seed); the integer-argument form keeps the draws. A first version of
patch 0027 also skipped a MULTIEQUAL whose output nothing reads, to keep folds that a dead merge would refuse; it never fired
(neither the suite nor the two corpus images changed with it removed) and was dropped. The first guess for 104955, a runaway loop in the program, was wrong: the instruction histogram of the unfinished run showed the whole
function executed 28 times, and printing the first pass's writes found the store into the literal pool.

**Not done.**
- `noInterveningStatement` is as upstream: a COPY or SUBPIECE whose output is used outside the block still passes. Patch 0027
  closes the one place it matters (a join that is the guard's target and also a case); the other branch of the fold keeps
  the default path's values by moving the edge, as the control and the corpus show. A fold elsewhere that depends on the same
  blind spot is not known.
- A mirrored callee is still not in the generator's grammar (a new literal kind would shift every stream); the replay line
  covers it.
- The mova-reached table as read-only memory (shape c of A4-5), above.
- The scan before the actions still reads the first flow only (round 1's note), and the A4-5 shapes did not change that.

## Area 3, round 1: the effects a callee proves are lent

Branch `review/3-callee-effects`, from `72eeb36`. Fixes ledger A3-2 (P3, but it decided whether patch 0026 cost users their
parameters): a callee whose analysis proved it keeps r4-r7 was lent only its signature, and the default model's `killedbycall`
list (patch 0026) then made the caller read `extraout_rN`, lose the parameter from its own signature and be withheld ("it uses r4
after calling X, which is not known to return it"). One engine patch, `0028-funcproto-set-effects.patch`, and `ApplyPrototype`.

### What the engine needed

Upstream gives a `FuncProto` an effect list only through the XML a `<prototype>` is decoded from: `effectlist` is private and
`decodeEffect` (which merges the overrides into the model's list) is private too; there is no setter and the model override
(`setModel`) swaps the whole list for the model's. So patch 0028 adds `FuncProto::setEffects(const vector<EffectRecord>&)`: the
records replace the model's effect for the storage each names, the rest stays the model's (it stores them and calls
`decodeEffect`, as the decoder does); an empty list clears them. Six functional lines. A call site takes its callee's prototype
with `FuncProto::copy` (`ActionDefaultParams`), which copies `effectlist`, and `hasEffect` (used by `guardCalls` in heritage)
reads it, so nothing else in the engine changed.

`ApplyPrototype` (Sh2Decompiler.cpp) now calls `LendEffects` after `setPieces`: for a lent prototype that is `known`, every
register of r2-r7, SR, MACH and MACL whose bit is set in `Prototype::effects.preserved` (proved by `ReturnCheck` on every path)
is marked `unaffected`; everything else keeps the model's `killedbycall`. The lent effects were already part of the prototype
(`Prototype::effects`), of the panel's `ShapeOf` hash and of the runner's request digest, so no cache key changed.

One decision, made on measurement: **r0 and r1 are not lent**. They are the return registers, and the engine's own return
recovery for the caller depends on what the calls leave in them. With r0 and r1 lent, two corpus functions (`06056130`,
`06056136` in image 008; sr-saving wrappers whose r0/r1 are only temporaries) lost a lent `void` signature, because r0 and r1 now
came through the callees as "real" values and the engine gave the function a `uint8` return (withheld: it uses sr after a call).
Without them no function loses anything, and the generator's lending is identical (6,660 lent either way). Also measured:
dropping r2 and r3 as well changes no signature count but leaves 19 more texts with `extraout_r2/r3` where the callee keeps them
(one was a `jsr @r2` that stayed indirect because r2 passed through a helper), so they stay in.

### The oracle

`TestCallsAndTraps`, "A3-2" (every fixture run on the p-code emulator against the evaluated pseudocode, as the section above):
the callee keeps r4 (and the old `r4 kept by the callee (limit)` fixture flips: `return param_1;`, signature lent with one
parameter, no `extraout`); r5 and r6 (`param_2`, `param_3`); r7 read alone (`in_r7`, no longer `extraout_r7`); r2 and r3 stored
after the call (`in_rN`); MACL and MACH (`param_1`); T (`sett` before, `bt` after: `return 1;`, no `extraout_sr`); a callee that
returns a value and keeps r4. Controls: the same callee with the effects not lent (the test hook `SetLendEffectsForTests(false)`)
reads `extraout_r4`/`r7`/`r2`/`r3`; nothing lent at all reads `extraout_r4`; a prototype that is not `known` lends nothing; a callee
that sets r4 in its delay slot still makes the caller read `extraout_r4` (withheld) while its r5 stays `param_2`; a callee that
keeps r4 on one path only (`cmp/eq ; bt ; mov #7,r4`) is not proved and kills it; r1 kept by the callee is still `extraout_r1`.
`TestPrototypeEligibility`'s `uses r0 after a void callee` is unchanged (r0 is not lent), with a comment.

Generator (`Sh2GeneratedTests.cpp`): check 11, `effects-ignored:<reg>`: when every function a function calls was lent a signature
whose `preserved` has r2-r7, SR, MACH or MACL, the function's text never reads that register as `extraout_<reg>`; counted as
`lent.honoured.checked` (11,578 in 3,000 seeds). The mutation mode `SE_GENERATED_MUTATE=honour` adds such a line to every result
and the check reports it (149 of 300 seeds). The existing check 10 (`CallEffects::preserved` against the emulator's final registers) is unchanged
and green.

`Sh2DecompilerTests.cpp` also gets `TestGameLending` (`SE_GAME_LEND=1` with `SE_GAME_HWRAM`; `SE_GAME_OUT` receives
`lend_before/` and `lend_after/`): every function decompiled after the functions it calls, each proved signature lent to the next
requests as the panel does (`DecompilerPanel::Learn`/`Lendable`: all learned prototypes, keyed by the address called, here both
the cached and the uncached alias), once with the effects not lent and once lent. It covers the prologue functions
`TestGameCode` has plus every high-work-RAM function they call (the leaves that never save PR, which are the ones that keep r4).

### Numbers

Generator, grammar 2, 3,000 seeds (`SE_GENERATED_GRAMMAR=2 SE_GENERATED_SEEDS=3000`), before (master) and after:

| | lent prototypes | withheld | "uses rN after calling X" (r6 + r7) | "it calls X, whose signature is not known" (the cascade) | failing seeds / findings |
|---|---|---|---|---|---|
| before | 6,263 (void 2,498, returning 3,765) | 899 | 414 (181 + 233), plus r0 93 and r1 11 | 121 | 0 / 0 |
| after | 6,660 (void 2,661, returning 3,999) | 502 | 136 (45 + 91), r0 0, r1 11 | 83 | 0 / 0 |

The previous round measured 207 of 5,648 lent signatures lost to this (3.7%); on this sample 397 more are lent (+6.3%, the callers
and the callers of those). The r0 cases that were withheld before went with them: they were callers of callees that were not lent.
What is still withheld is a callee that really sets r6 or r7.

Game corpus, lending run (callees first, `TestGameLending`), both images:

| | 007 before | 007 after | 008 before | 008 after |
|---|---|---|---|---|
| functions (prologues + the called ones in HWRAM) | 1,733 | 1,733 | 2,998 | 2,998 |
| signatures lent | 617 | 680 | 1,005 | 1,083 |
| parameters in them | 780 | 845 | 1,208 | 1,302 |
| withheld "uses a register after calling" | 93 | 39 | 143 | 77 |
| functions lent a callee's signature that keeps some of r4-r7 | 844 | 876 | 1,500 | 1,534 |
| ...of which keep their own signature (parameters kept) | 249 | 308 | 432 | 506 |
| ...of which print `extraout_r4-7` | 114 | 81 | 186 | 134 |
| functions printing `extraout_r4-7` | 129 | 96 | 201 | 149 |

Text changes: 297 (007) and 559 (008) functions; 63 and 78 gain a lent signature, none loses one; 35 and 49 gain parameters,
none loses; `extraout_r4-7` is gone from 33 and 52 functions and new in none. (One called address in each image is no function
the engine can decompile; the result is a failed status in both runs.) Example, `06008204` (007): the call
`func_0x06031598(iVar2,extraout_r4 + extraout_r0,param_3)` becomes `...,param_3,param_2)`, r5 kept across a helper being the
fourth argument. The standalone corpus (`SE_GAME_OUT`, nothing lent, 839 and 1,554 functions) is byte-identical to master, as it must
be: the change is in what is lent.

### Mutation

Each reverted alone (the source restored after), `Sh2DecompilerTests` rebuilt and run:

| Reverted | `Sh2DecompilerTests` | generator (300 seeds, grammar 2) |
|---|---|---|
| `LendEffects` returns at once (no effects lent; also what reverting patch 0028 amounts to) | fails 21 checks: the r4 fixture (flipped), r5, r6, r7, r2, r3, MACL, MACH, T, the returning callee, the r5-with-clobbering-callee | fails: 32 seeds, 42 `effects-ignored` findings (r6, r7) |
| r4-r7 dropped from the lent list | fails the r4, r5, r6, r7, returning-callee and r5 fixtures | not run |
| MACH and MACL dropped | fails the MACL and MACH fixtures | not run |
| SR dropped | fails the T fixture | not run |
| r2 and r3 dropped | fails the two r2/r3 fixtures | not run |
| r0 and r1 lent as well | fails `uses r0 after a void callee` and the r1 fixture | not run |
| the proof ignored (all of them lent for every known callee) | fails 9 checks: the callee that sets r4 or r7 and the one-path callee read the wrong value, with the oracle on the first two (`PseudoAgreesWithRun`) | not run (the generator has no execution-vs-text check) |

### Matrix (RelWithDebInfo, gcc)

The whole `ctest`: 95 of 95 pass (the generated suite at its default 300 seeds). `Sh2GeneratedTests` at 3,000 seeds of grammar 2:
0 failing seeds, 0 findings. The game corpus, both images (`SE_GAME_HWRAM` hwram007 and hwram008): every ledger count 0, the
dropped accesses 0, 16 and 20 reads "missing and said" as before. gcc Debug with `-fsanitize=address,undefined`, `Sh2DecompilerTests`
with `detect_leaks=1`: exit 0, all passed, no report beyond `address.hh:605` and `slghpatexpress.cc:979`. `vendor.py` on the pinned Ghidra
(`918d44e`, the sparse checkout in the scratchpad) with 0028 applied reproduces the committed tree, `sh-2.sla` aside (the patch
touches no spec).

### Limits, and what is left

- A callee's effects are only as good as `ReturnCheck`'s modelling of its registers; the generator's check 10 compares them with the emulator's
  final registers, and nothing new was found.
- r0 and r1 stay killed for every callee (above). The caller that relies on a callee leaving r0 or r1 alone still reads `extraout_r0/r1`
  and is withheld; 11 generated cases (r1) and the corpus remainder are of this kind or of a callee that really changes the register.
- The effects are lent per callee address with the signature, so they follow the same rules (a mirror is lent under its own key; a
  signature learned under another volatile list is not lent).
- The corpus still has 39 and 77 functions withheld on "uses a register after calling": callers of helpers that change the register
  (or whose own signature is not lent).

## Area 5, round 3: the width table

Fixer, branch `review/5-round3-fix`, fix commit `4bc5668`. Ledger rows A5-9 to A5-15 are fixed, A5-16 is fixed except one
part that is accepted, A5-17 is accepted. No engine patch (patches 0001 to 0027 unchanged): every root cause was in the host's
map and its note code (`SaturnHardwareMap`, `HardwareNotes`), so `Sh2Decompiler.cpp` changed only inside `HardwareNotes`.

**The class, not the instances.** Rounds 1 to 3 each found a few registers whose narrow or wide access the pseudocode
mis-described, and each fix was a hand rule for the instances (`NarrowAccess::Part` for "everything else"). Round 3 replaces
the hand rules with a table derived from Mednafen's handlers: for every register of the map, each of the three widths, both
directions, and every aligned place the access can fall on the register, one **cell**:

| Category | Meaning | Rules |
|---|---|---|
| a | accepted as written | `Plain`, `HalfOfLong` (a long on a 16-bit register of a 16-bit bus: two accesses, the register takes one half, the other is another register) |
| b | the whole register, wider than asked | `ByteWritesWord` (VDP1, VDP2), `PortWritesWhole` and `NarrowReadWhole` (SCU DSP ports), `WordWritesLong` (on-chip 32-bit), `FifoPopsTwice` |
| c | one lane, half or register | `HalfBesideGap`, `SmpcLaneWrite/Read`, `EvenByteWrite/Read`, `LowHalfRead`, `CdLongWrite/Read` |
| d | nothing | `SmpcPadWrite/Read`, `OddByteWrite/Read`, `LongHalfWrite`, `KeyedWordWrite`, `StrobeByte`, `MirrorWrite` |
| e | address error | `AddressError` |
| f | a side effect that differs | `StrobeLong` (two signals), `CdByteWrite` (the command is not started) |
| v | depends on the value | `KeyedWrite` (watchdog words, bus state controller longs) |

plus flags on a cell: the register's own read or write effect happens or not (a division, a latch, a flag armed), the 32-bit
bus also reads the register 2 bytes on (B bus), or reads this one twice. The rules are per **family** (`NarrowAccess`, now 12
values: `Part`, `WholeOnByte`, `ByteLane`, `OnChipLong`, `OnChipKeyedLong`, `OnChipWord`, `OnChipByte`, `OnChipHalf`,
`OnChipSci`, `ScuPort`, `CdWindow`, `Strobe`), in one function, `WidthCellFor`, because Mednafen's handler is per area. The table is
enumerable (`WidthTable()`), and the cautions are generated from it in one place (`NarrowAccessEffects`): the "covers" caution
and the documented wide accesses moved there from `HardwareNotes`, which now also gates a register's own read effect on the cell
(a word read of TIER no longer claims to arm FTCSR).

**Shape and size.** 337 registers (and the 48 unused SMPC lanes as rows), 29 rules, 12 families, **3,038 cells**
(`-`/kind 983, `?` 22, a 1,030, b 353, c 167, d 150, e 316, f 4, v 13). Golden file
`FrontEnd/tests/HardwareWidthTable.golden.txt` (about 450 lines): the rules with the Mednafen function each is read from and
the caution the first cell generates, the families, a keyed-write sample section, and one line per register with all its cells
(`w4@-2=HalfBesideGap[15-0]`: write, 4 bytes, starting 2 below the register, the register takes bits 15-0). Review it against
`sh7095.inc`, `ss.c`, `scu.inc`, `vdp1.c`, `vdp2.c`, `cdb.c` by rule, not by register.
`SaturnExplorerHardwareWidthTableTests` fails when the printed table differs from the golden file or from the rules table in
`HARDWARE_REGISTER_AUDIT.md` (between the `width-rules` markers; `SE_UPDATE_GOLDEN=1` rewrites both). The audit document's other
tables stay generated by `tools/audit_hardware_map.py` from the `Reg` arrays.

**Rows.**

- **A5-9, fixed.** A 4-byte access to the 8- and 16-bit on-chip registers below `0xFFFFFF00` is `AddressError` (`OnChipByte`,
  `OnChipHalf`); one caution per register covered, in place of "covers". `icrLong`, `iprbLong`, `sbycrLong`, `vcrwdtLongRead`.
- **A5-10, fixed.** A word at an on-chip byte register reaches the even one only, with the low byte (`EvenByteWrite/Read`); the
  odd one is not written or read (`OddByteWrite/Read`, also when no register is at the even address: SBYCR, DRCR0). The
  register's read effect is gated: no arming note for FTCSR on a word read at `0xFFFFFE10`. `tierWord`, `frchWordRead`,
  `tierWordRead`, `sbycrWord`, `drcr0Word`. The watchdog stays `KeyedWrite`.
- **A5-11, fixed.** `ScuPort` (PPAF, PPD, PDA, PDD): a byte or word write is `PortWritesWhole`, with the register's `onWrite`
  effect in the caution (a program word loaded, the address set); a narrow read of PPAF or PDD is `NarrowReadWhole`.
  `ppdByte`, `ppafByte`, `pdaWord`, control `ppdLong`, `pddWordRead`.
- **A5-12, fixed.** `KeyedWrite()` handles the bus state controller's 4-byte writes: no key, a caution that it does nothing;
  the key, a note; an unknown value, a note that it needs the key. `bcrLong`, `bcrKeyed` (control), `bcrUnknown`; the golden file's
  keyed-write section.
- **A5-13, fixed.** `Strobe`: a byte write to MINIT/SINIT does nothing (`StrobeByte`), a long signals twice (`StrobeLong`).
  `minitByte`, `minitLong`, control `minitWord`.
- **A5-14, fixed.** `HalfBesideGap`: a long on a 16-bit register of the B bus whose other half is no register of the map says which
  half the register takes and where the other goes (PRIR and the word after it, RAMCTL and the word before it, VDP1 ENDR, SCSP
  DMEAL); a long on a CD register is two writes to it (`CdLongWrite`, the low half kept) and a long read returns it in both halves
  (`CdLongRead`); a byte write to CD_CR4 does not start the command (`CdByteWrite`, `cdb.c` `mask == 0xFFFF`). `prirLong`,
  `ramctlLong`, `endrLong`, `dmealLong`, `hirqLongRead`, `hirqLong`, `cr4Byte`.
- **A5-15, fixed.** A read narrower than 32 bits on the B bus is `SH0` at A and `SH1` at A|2: the register 2 bytes on is read too,
  with its effect (the note on a read of TVMD: EXTEN latches HCNT and VCNT), and a register at an address with bit 1 set is read
  twice. On the CD block's A bus a narrow read of the FIFO at `0x25818000` takes two words (`FifoPopsTwice`); in the register window
  `0x25898000` it takes one, and a long there returns that word in both halves. `tvmdWordRead`, `vdp2ByteRead`, `fifoWordRead`,
  `fifoWindowRead`, `fifoWindowLong`, `fifoLong`.
- **A5-16: read effects as notes, not cautions; fixed except (4).** (1) `VDP2_EXTEN` and `VDP2_TVSTAT` have `onRead`
  (latch, flag clear): documentation, a note and not a caution, because the register does return what the line says (a read
  of a write-only register, where the value is not the one written, is the caution). The round 2 fixture `tvstatRead` pinned "a
  read of a read-only register says nothing"; the table proves it incomplete (the read clears EXLTFG), so it now pins the note.
  (2) `SCU_AIACK` is write-only (reads 0 at every width; `aiackRead`), the audit's one `DIFFERS` row is gone. (3) `SH2_CCR` reads
  back at `0xFFFFFE93`-`0xFFFFFE9F`: an alias of the register (named through a mirror), and a write there is `MirrorWrite`
  (`ccrMirrorRead`, `ccrMirrorWrite`). (4) **Accepted:** a byte write to VDP2 colour RAM writes the whole 16-bit entry from the data
  bus (`vdp2.c` `RW_W8`). Colour RAM is ordinary memory to the decompiler, like VRAM: a folded store has no volatile operation for
  the note code to attach a caution to, and marking it volatile would keep every palette copy as written for a quirk of the
  emulator. The table's audit text names it.
- **A5-17, accepted.** A 1-byte symbol for the unused lane (`SMPC_UNUSED_1E`) beside the 2-byte word view was tried and works for
  the byte, but the engine takes the smallest symbol at an address for an access of any width, so every word write printed as the
  unused lane (`_SMPC_UNUSED_1E = 0x19;`) and A5-4 regressed. The lane is a row of the width table instead (`SmpcPadWrite/Read`),
  and the caution on the line already names the lane and the register beside it. The line `SMPC_COMREG._0_1_ = 0x19;` stays.

**Existing cautions kept** (every round 2 fixture passes unchanged in its text), with the table proving three things
incomplete or wrong: `tvstatRead` (above), `vdp2ByteRead` (now carries the EXTEN note), and the round 2 sample test of
`NarrowAccess` kinds (IPRB and TIER were `Part` "taken by lane", which is only the byte access; the CD block was `Part`, and a
long there is two writes to one register). Their assertions were edited, nothing else.

**Mutation checks** (break one entry, build, run: the golden file and the fixtures both fail; restored, both pass):

| Entry broken | Golden | Fixture CHECKs failed |
|---|---|---|
| `OnChipHalf` long not an address error (A5-9) | fails | 21 |
| `OnChipByte` word write plain (A5-10) | fails | 5 |
| `ScuPort` narrow write plain (A5-11) | fails | 15 |
| BSC key accepted whatever it is (A5-12) | fails (keyed-write section) | 1 |
| `Strobe` byte write signals (A5-13) | fails | 1 |
| gap neighbour treated as a pair (A5-14) | fails | 4 |
| FIFO pops once (A5-15) | fails | 1 |
| B bus reads only the addressed register (A5-15) | fails | 2 |
| CCR mirror takes writes (A5-16) | fails | 1 |
| SMPC word write plain (round 2) | fails | 5 |
| read effect not gated by the cell (A5-10b, in `HardwareNotes`) | passes: the gate is the cell's flag, which the golden file prints (`*`/`!`) | 1 |

**Matrix** (RelWithDebInfo, gcc):

| | |
|---|---|
| `ctest` | 96 of 96 passed (80 s with -j3), the new `SaturnExplorerHardwareWidthTableTests` among them |
| Game corpus, `hwram007.bin` | 839/839; every count 0; 16 missing and said; records: 14,843 folded, 0 dead, 16 unreachable, 27 switch lookups (the same as the master binary run beside it). The pseudocode of all 839 outputs is byte-identical to master's; notes 7 and cautions 16, as before |
| Game corpus, `hwram008.bin` | 1,554/1,554; every count 0; 20 missing and said; records: 38,701 folded, 0 dead, 20 unreachable, 155 switch lookups. All 1,554 outputs byte-identical; cautions 20 as before; notes 8 to 22 |
| Outputs that gain or lose a caution | none, in either image. Outputs that gain a note: 3, all in 008 (`060569B4`, `06056D30`, `06071912`; 14 notes, each `VDP2_TVSTAT: a read clears EXLTFG`). Read and judged: each is a `VDP2_TVSTAT & 1` (the PAL bit) test, the note is true of the hardware and harmless there; no caution, no change to the pseudocode. The 16-bit CD FIFO read at `0x25898000` (one pop) gets no new caution, as it should |
| Generated test | passes (`ctest`) |
| ASan + UBSan + LeakSanitizer, gcc Debug (`-O0`, no debug info: the disk was nearly full), `Sh2DecompilerTests`, `detect_leaks=1` | clean beyond the two tolerated UBSan sites (`address.hh:605`, `slghpatexpress.cc:979`); no AddressSanitizer or LeakSanitizer report; suite passes |

**Not done.** Accesses to unnamed addresses inside a register area are not rows of the table (the on-chip gaps below
`0xFFFFFF00`, where a long is an address error whether a register is there or not; the SCSP slots): a long over a gap and a
register is said from the register's side, a long over nothing but a gap is not said. VDP2 colour RAM (A5-16, part 4) and the
SMPC unused lane's symbol (A5-17) are accepted above. SCI and UBC rows carry no Mednafen evidence (Mednafen has no handler):
the SCI claims nothing at word and long widths (`Undecoded`), the UBC keeps round 2's manual-based byte rule. The audit script
does not check the width table (its source is the rule function, not a text pattern); the test checks it against the golden
file and the audit document instead.

## Areas 1 and 8, round 2 review

Branch `review/1-8-round2-review`, from master `f0684b9`. This round reviewed area 1 (the round 1 fix of the too-large memo,
`f5958a2`), the oracle itself, and area 8's indicators (stale, unsupported, truncated, unimplemented, unavailable, too-large) along
area 1's sequences. No production source was changed. Three ledger rows were added: **A1-4 (P3), A1-5 (P3) and A8-1 (P3)**. No P1 or
P2 was found.

**Record note for the coordinator.** The "Area 1, oracle" and "Area 1, round 1" sections are no longer in this file. Commit `b10bc98`
("Area 6 round 1: record the merged-tree runs") replaced 741 lines from line 1914 onwards. Along with the area 1 sections, it removed
"Areas 3 and 4, oracle: the generator's second grammar", "Area 5, round 2: fixes", "Area 6, oracle" and "Area 6, round 1". Their text
is intact in `253f058:SaturnExplorer/Docs/FunctionalityVerification/15_DECOMPILER_STEP5.md` and other commits before `b10bc98`. This
round read them from `253f058`. They were not restored here, because this branch fixes nothing.

### What changed in the model (test only)

`DecompilerModelTests.cpp` gains three things. None of them is generated, so every seed produces the same sequence as before and the
default run is unchanged.

- **`wedge`**: Refresh, hold the engine on that request, press Stop, and keep the engine from reaching a check-point until a 0.5 s
  cancel deadline has passed. The runner then disables decompilation for the session. The engine stays held until `wait`. While the
  runner is disabled, `CheckIdle` excuses an idle, stale or empty panel, and the final walk is skipped.
- **`selectm <fn>`**: types the entry through the 0x2xxxxxxx cache-through mirror.
- **`kOpen`**: the fixtures of the open rows, run alone with `SE_MODEL_OPEN=1` (the exit code is 1 while any of them fails). The fixer
  moves each one into `kPinned` once it passes.

### Findings

- **A1-4.** An answer discarded while the panel is closed leaves its question recorded in `mAsked`. When the view comes back to that
  question before the panel is shown, `Update` sees it as already asked and submits nothing. The view can come back by a resume after a
  scrub, by a scrub away and back to the same frame, by a BIOS image dropped and chosen again, or by a function start set and cleared.
  The function the user chose never appears; the previous one stays on screen, stale, with nothing outstanding. Fixture:
  `hold; select A; hide; scrub 1; wait; resume; show; wait`, world C,A.
- **A1-5.** `Update` records the question in `mAsked` before it checks `runner.Disabled()`. When the runner re-enables in a new session
  (the stuck request ends after the session changed: `RecoverLocked` on the worker), the panel never asks the new session's question.
  Fixture: `select A; wedge; reconnect; wait`, world C,A. Two controls pass: the stuck request ending before the session change, and an
  edit made while the session is still disabled.
- **A8-1.** For every view except a running emulator, the stale badge's tooltip promises "its replacement is shown when it is ready",
  whether or not anything is outstanding. After Stop, A1-4 and A1-5, nothing is outstanding. The panel exposes no stale reason that a
  state model could check.

### What held

- **The round 1 fix.** `MakeRequestKey` digests every field of `DecompileRequest`: the entry as asked, names with their text, function
  starts, each lent prototype with its whole signature and all five call-effect fields, `registerCallees`, `maxInstructions`, the volatile
  list and `allMemoryVolatile`. It also digests the snapshot key, which covers source, derive serial, frame and region set; the region
  set includes the BIOS content hash.
  - A run of the same key that ends in any other status erases its memo entry. That includes a user cancel, a supersession and a
    session cancel. A run from an old epoch never inserts one, and `InvalidateSession` clears the memo.
  - A stuck request that ends after a recovery cannot plant an entry either, because its epoch is old.
  - The pinned A1-1 to A1-3 sequences pass. `selectm A; starve; select A; selectm A; edit A 0 1; selectm A` passes: a mirror and the
    canonical entry are separate questions.
- **Inputs outside the body.** Every byte the engine can read is behind the derive serial: the body, a literal pool, a callee's code,
  data that flow reaches. `Context::WriteVram` calls `RebuildDerived` for every work-RAM and VRAM write, and that bumps
  `se_derive_serial`. An edit through a mirror address writes the same region, so it bumps the serial too.
  - On a scrubbed frame the identity is the timeline, the frame number, and 0 or a fresh edit counter. `mScrubTimeline` moves on Play
    From Here, on a ring clear, on a slot load (`DropRecordedHistory`) and on a session reset. `FrameRecorder::Capture` refuses a frame
    number that does not advance, so within one timeline a number is never reused.
  - The engine version, the patches, the hardware map and the budget are constant for a process. Nothing is cached across processes.
- **Lent prototypes.** Learned prototypes are keyed by the snapshot, `LendingId()` (the function set and the volatile list) and the
  address as run. They drop transitively through `deps`. The engine applies a lent prototype only to registered functions, and every
  call it makes is in `calledAt`, so `deps` sees every prototype a result used.
  - A shown result's lent set cannot change without a new install: only `Install` calls `Learn`. So `ShownStale` not looking at the
    prototype hash leaves no gap.
  - Mirror sequences pass: the C/A walk through mirrors with an edit in between, the volatile list through a mirror of P, and the
    mirrored BIOS function with another BIOS image.
- **Stale mark.** 600 random seeds produced no failure: 23,625 operations, 13,796 results checked at install, 23,577 idle checks, 7,649
  checks with the engine held, 1,628 finish-then-change races, 127 stops and 59 budget stalls (394 s). The default run still passes
  (18 seeds and 5 pins). Directed sequences also pass: Stop, then reconnect, then the same function; a starve, then hide, scrub away and
  back, show and Refresh; a starve, then reconnect, then the same function; a running emulator with a held request across hide and two
  frame advances.
- **Trust indicators.** Unsupported, truncated, bad data, flow warnings and too-large all come with the result and are replaced with
  it. A function that becomes supported, or no longer truncated, gets a new key and so a new result. The panel cache holds only `Ok`
  results, under the full question, so a truncated result is cached under the region set it was truncated for.

### Not probed, and why

- **`App` itself.** It was read but not run: the scrub identity, the call-site hints keyed by snapshot, `AdoptNewEmulatorInstance` and
  the slot load were checked against the code. A wrong identity in `App` is still invisible to the model, which builds the view the way
  `App` does.
- **Follow PC, Containing, auto-refresh and the halt hint.** The model's world has no registers. Auto-refresh's `mAutoPc` is not reset
  by `ResetSession`; with the same PC after a new session it could suppress one refresh. That needs registers to reproduce, so it is
  only a question.
- **"Unimplemented".** It is unreachable on this spec: `sh-2.slaspec` and `superh.sinc` contain no `unimpl` constructor. It would also
  show only as a grey warning count, not as an amber line; that should change if the spec ever gains one.
- **`DecompileStatus::Disabled`.** The panel draws it, but nothing produces it.
- **Speed.** Since round 1 the memo keys by snapshot, so stepping a paused emulator, or scrubbing, through a function that runs out of
  budget costs a full budget (10 s) on every step. That is behaviour as documented, but it is a speed regression from the old memo.
  There is no fixture: no oracle rejects it.
- **The runner suite.** `SaturnExplorerDecompilerRunnerTests` was not rebuilt: the container's disk was full while it compiled. The
  model and panel binaries were built and run.

## Areas 3 and 4, round 3 review

Reviewer, branch `review/3-4-round3-review`, on master `f0684b9` (patches 0001 to 0028). Adversarial: this round adds findings
only, no fixes. New ledger rows: A3-3 (P2), A3-4 (P2), A3-5 (P2), A3-6 (P3), A4-7 (P3), and A2-11 (P3, found by the generator
outside these areas). The instrument was a scratch driver, not committed: the `TestCallsAndTraps` oracle (`CallSnapshot`,
`PseudoAgreesWithRun`) over fixtures read from a file, with the callees decompiled first and lent. It also did a lending run of
image 008 that reports every lent prototype whose `preserved` misses a register the model keeps.

### What was probed and held

- **Patch 0025 (trapa).** A leaf callee with a `trapa` is withheld ("an indirect call it does not resolve"), and its caller
  reads `extraout_r4`. Conservative and right. The fixer's fixtures were not re-derived.
- **Patch 0026 (what a call kills).** T changed only by a `cmp/eq` in a lent callee clears SR's bit in `preserved`. A bit-range
  write to `sr` is an `INT_AND`/`INT_OR` that `ReturnCheck` takes for a new value. The caller prints `extraout_sr` and is
  withheld; the oracle agrees.
- **Patch 0028 (lent effects).** These hold: r4 set in the caller's own `bsr` slot with a keeping callee (`return 9;`); nested
  lending, where A calls B, B calls C, and C keeps r4 (`return param_1;`, all three lent); the one-path and delay-slot
  controls of the suite. `ReturnCheck`'s model held for these:
  - saves and restores through the stack;
  - width-mismatched restores (a 2-byte load from a 4-byte slot is a new value);
  - pushes below r15;
  - stores through a parameter or a callee's `storesThrough` (every slot clobbered);
  - recursion (the panel never lends a function its own signature, and the engine withholds "it calls itself", so no
    coinductive proof is possible).

  A stale callee is not lent: `Lendable` needs the snapshot key and the `LendingId`.
- **The return-width question.** `mov.b @r4,r0` is lent `int4`, `extu.b` `xunknown1`, `and #0xff` `uint4`: each consistent with
  the extension the callee makes. No mismatch was found.
- **Patch 0021 (body from the last flow), 0024, 0027 (switch folds).** These all match the emulator or read right by hand:
  - a conditional branch into an `rts` delay slot (Ghidra warns "overlaps"; the oracle agrees: `return 0x15` / `0x11`);
  - a table entry into a delay slot (`case 1: return 0x6009011;`);
  - a table entry that is the function's entry or the guard's compare (a loop, printed as `while` with a switch on the
    entry value);
  - two guards, signed and `cmp/pz`, with different defaults;
  - table entries through the cache-through mirror;
  - a table entry into uncaptured memory (truncated, warned, withheld);
  - the `cmp/hi` control of A4-7.

  For the patch 0027 three-way case: the default path's values can only differ from the switch block's through the switch
  block's own register ops. A store there fails `noInterveningStatement`, and the guard block's stores are on both paths. Every
  such value meets at the guard's target, where the patch compares it. No fixture found a way round.
- **Generator**, grammar 2, `SE_GENERATED_FIRST=200001 SE_GENERATED_SEEDS=20000` (a new window): 1 failing seed of 20,000
  (219488, A2-11), 1 finding. The ordinary suite with `SE_GAME_HWRAM=hwram008.bin`: all passed, 0 dropped, 20 missing and said.

### What failed

- **A3-3 (P2): a proved change to a "kept" register is ignored.** `LendEffects` lends only what was proved kept among the
  registers the model kills. What was proved changed among those the model keeps (r8-r14, r15, GBR, VBR) is never killed. A lent
  callee that sets r8, loads GBR, or leaves r15 4 bytes lower is read as keeping them. The caller prints the old value
  (`return 5;` where the emulator returns 7) and is lent a signature on it. The corpus has real instances:
  - `060647EC` takes and advances r8 and reads r14;
  - `06064228` sets GBR.

  Both are lent to callers that are lent in turn.
- **A3-4 (P2): structure-return pointer in r2.** The callee writes the caller's local through r2, and the caller's read after
  the call is folded to the value it stored before. The host's own proof (the callee's `storesThrough` names r2) is why the lent
  caller is withheld, but the text still states 5 where the code returns 7.
- **A3-5 (P2): inputs outside r4-r7 are dropped from a lent signature.** This covers r0-r3, r8-r14, GBR, MACH, MACL and T read
  before written. A return made only of such an input is dropped too: `rts ; sts macl,r0` prints
  `void f(void) { return; }`, lent `void`. The generator checks parameters for r4-r7 only.
- **A3-6 (P3): an absolute store into the callee's own frame.** `ReturnCheck`'s assumption that an absolute address is not in
  the stack lends r4 as kept when the callee overwrites its saved r4 through a constant address. It is the engine's own
  assumption inside a function; it is raised so it is decided where the proof is lent.
- **A4-7 (P3): a signed-only bound check.** A table guarded by `cmp/gt` or `cmp/ge` alone is shown with every other index going
  to the default. The hardware takes a negative index through the word before the table.
- **A2-11 (P3): an engine failure.** A store whose propagated constant address wraps 0xFFFFFFFF fails the whole function
  (status Failed): `mov.w r1,@-r4` on the path where r4 = 0.

### Oracle gaps (no fixture of a wrong output, so not rows)

- The generator's check 10 compares `preserved` with the emulator for r0-r15, SR and PR. It skips GBR, VBR, MACH and MACL
  (`else continue`), although patch 0028 lends MACH and MACL. Grammar 2 never touches MACH, MACL or GBR, and grammar 1 (which
  has `mac`) has no calls. So the MACH/MACL lending is pinned only by `TestCallsAndTraps`.
- Check 11 asks whether lent kept registers are honoured. It never asks whether a register lent as changed is still read as
  kept. With A3-3 fixed, a check 11b would be the mirror image: a register outside r2-r7/SR/MACH/MACL that a callee does not
  keep must not be read across the call as the caller's.
- Shapes outside the grammar, each found only by hand this round:
  - only unsigned guards (`cmp/hi`, `cmp/hs`);
  - no r8-r15 or GBR changes (the generator's own contract);
  - no inputs outside r4-r7;
  - no struct returns.
- `bodyBytes` (`fd->getSize()`) mixes mirrors: a function entered at 0x26009000 whose table jumps to 0x0600xxxx reports
  536,870,908 bytes. Nothing reads `bodyBytes` outside the tests' `Print`, so this is no finding.

### Not probed, and why

- Interrupt-driven changes to a callee's registers, a `trapa` handler that rewrites its stacked PC (inline parameters after the
  trap), and self-modifying literal pools. Each needs a model the oracle does not have.
- `SE_GAME_LEND=1` (`TestGameLending`) was not rerun as such. The scratch lending run covers the same order and lending on
  image 008 (2,998 functions, 1,004 lent). Image 007 was not run.
- Grammar 1 and longer generator windows. Not these areas; the fixers' runs stand.

## Area 6, round 2 review

Reviewer, branch `review/6-round2-review`, on master `f0684b9` (round 1's fix `d41c806` and its merged-tree record `b10bc98`).
No source changed: the probes were run from scratch copies, and each mutant was compiled into a scratch binary from a copy of
one file, with the tree left untouched. New ledger rows: A6-10 to A6-15 (two P1, two P2, two P3).

**How the probing was done.** `FaultProbe.cpp` is a scratch copy of `DecompilerFaultTests.cpp` with four additions:

- two fixtures, `shapes` (loops, `&&`, `||`, a do-while, an if/else; A6-11) and `stack` (a stack array; A6-14, A6-15);
- `SE_FAULT_STICKY=1`, which makes every allocation from `k` on fail (real exhaustion, not one transient failure);
- `SE_FAULT_RANGE=lo-hi`, every `k` in a range, with each cell's trace;
- `--repeat`, N requests in one process with RSS read before and after.

It was linked against the RelWithDebInfo build's objects. A second, ASan+UBSan build (`-O0 -g0`) was linked from the same
sources, because the disk was too full for a CMake Debug sanitizer tree.
The fixtures and the reproduction commands in the ledger use the matrix's own request (`MakeRequest`) and its `--cell`/`--driver`
syntax. `fixture1` is already in `kFixtures`, so A6-10 reproduces with the unmodified binary.

### What held

- **The default subset** on the merged tree: 25 of 25 jobs pass, 32 s. No pin is STALE.
- **Every pin of A6-1 and A6-2 is on its site.** The failing allocation's stack is `Emit::Emit` / `PackedEncode` in Print, in
  both passes and in the reference run.
- **The full cancel and runner matrix** (`SE_FAULT_FULL=1 SE_FAULT_ONLY=cancel-enter,cancel-alloc,cancel-oom,runner`), RelWithDebInfo:
  229 jobs pass, including 207 `runner-oom` processes; 267 s. This covers cancels at every phase entry and every allocation of all
  four fixtures, cancel with an allocation failure 0 to 4,096 allocations later, and Stop, supersede, invalidate, destroy, budget
  and disable at every phase entry. A6-6's pass-boundary check holds: no `cancel-alloc fixture1` cell is late.
- **Mutation of A6-3 by its likeliest regression.** Dropping only `ident->list.reserve` in `block.cc` (a partial re-apply of patch
  0023) is caught by the default subset: 3 pinned cells crash. Reverting A6-2 alone is caught too (the pinned cell 22940 crashes).
- **Sticky exhaustion.** With every allocation from `k` on failing, the every-k sweep of mailbox (35,956 cells) and jumptable has 0
  failures. Nothing on the way out (destructors, `SetError`, `TeardownNote`) needs an allocation that would turn a failure into a
  terminate. On `shapes` and `fixture1` the sticky sweep fails only the cells of A6-11 and A6-10 (12 and 6 cells). The runner was
  not run sticky.
- **Repeated failures in one process.** RSS holds still over 1,000 requests each of Ok, `Failed` by `LowlevelError` ("Flow exceeded
  maximum allowable instructions", which does not damage the engine) and Cancelled at a phase entry: 11,028 to 11,048 KB, 11,684
  to 11,700 KB and 11,668 to 11,684 KB. The non-damaging failures leak nothing per request.
- **The ASan sweep of the Actions phases** of mailbox and jumptable (both passes, every `k`, 5,102 cells) is clean.
- **The host paths.** A damaged engine refuses `Decompile` and `Disassemble`, and every `alloc` cell checks this. `Failed("out of
  memory")` reaches the panel's red line through `Poll`. A stale one is dropped, and the next request then shows the
  Unavailable text. `catch(...)` in the engine swallows only in `Scope::addMapSym` (symbol decoding, not reached).
  `CancelError` is a `LowlevelError`: the one engine catch of it in Actions that drops its type
  (`ActionActiveParam::apply`, coreaction.cc:1874) wraps no check-point.
- **Stack depth, by analysis only.** Recursion grows with the dominator tree and the structure's nesting. Both are bounded by
  `maxInstructions` (20,000) and by the SH-2's branch ranges (an if nests at most a few hundred deep through `bra`). The
  1000-diamond fixture needed more than 512 KiB in a Debug build, which puts the deepest reachable chain at a few MiB against the
  worker's 16 MiB. No fixture was built to test it.
- `Scope::addUnassigned`'s use-after-free (it reads `entry->symbol` after `delete entry`, on every call) is unreached. It is called
  only from `Scope::addMap` for a map entry with an invalid address, which only decoding a symbol without storage makes. The host
  and the actions map every symbol at a real address: the host's registers, `ScopeLocal::createEntry`,
  `ProtoStoreSymbol::setInput` and `linkSymbol`. Because the bug fires on every call, not only after a failure, the clean ASan
  runs of the decompiler suites and of these sweeps also show the path is never taken.

### What failed

| Row | Severity | What | Cells |
|---|---|---|---|
| A6-10 | P1 | `FuncProto::copy`: `delete store; store = op2.store->clone();` (A6-1's class in `fspec.cc`). A double free when a call through a literal pool is deindirected | `fixture1`: 6 of 21,831 (RelWithDebInfo), ASan heap-use-after-free in `~FuncProto` |
| A6-11 | P1 | `HighVariable::mergeInternal` moves the Varnodes before it allocates the merged list. Use-after-free of the high at teardown; in RelWithDebInfo it crashes only sometimes | `shapes`: 11 of 40,442 (RelWithDebInfo, nondeterministic), 7 under ASan in one pass |
| A6-12 | P2 | A pin has no identity. A6-2 reverted plus one unrelated 16-byte host allocation in Print passes the whole default subset; the A6-3 pins already sit on other code | mutant: 0 of 25 jobs fail |
| A6-13 | P2 | `alloc` and `cancel-oom` run on mailbox and jumptable only. `fixture1`, the matrix's own fixture, crashes at once | the every-k sweep of `fixture1` |
| A6-14 | P3 | More erase-then-insert leaks: `VarnodeBank::setInput`, `PcodeOpBank::destroy`, `MapState::addRange` | `stack`: 41 of 945 (ASan+LSan) |
| A6-15 | P3 | The "A6-7 host register loop" frames match every phase. It accepts Actions-phase leaks, among them a new class (`ScopeInternal::renameSymbol`) | `stack`: 29 of 945 accepted wrongly |

Both P1 rows are patch 0015's class. Round 1 searched `block.cc` and a grep for `delete X; X = new`. A grep for `delete X;` followed
within six lines by any `X = ...` over the engine finds `FuncProto::copy` and `ProtoStore*::clone`. The rest of its hits are null
assignments, are fixed already, or are on unreached paths (round 1's list, and `JumpTable::saveModel`/`restoreSavedModel`, which
only move pointers). Search does not find A6-11, whose state change and allocation are in different statements. The matrix does,
given a fixture that merges. So the case for A6-13 is stronger than the case for any further grep.

### Could not probe, and why

- **The CI's Debug ASan configuration, whole.** The disk was full (the shared scratch held about 24 GB of other agents' builds).
  The ASan runs here use `-O0 -g0` objects linked by hand. Allocation counts match the RelWithDebInfo build (fixture1 21,800 in
  both).
- **`diamonds` under the `alloc` family** (410,906 allocations) and `cancel-us` in full were not run, for time.
- **A deep-recursion fixture**, for the reasons under "What held".
- **Windows and macOS.** The matrix forks.

### For the coordinator, not area 6

`b10bc98` ("Area 6 round 1: record the merged-tree runs") rewrote this file. It added 26 lines and deleted 741, dropping six
sections: "Area 1, oracle", "Area 1, round 1", "Areas 3 and 4, oracle", "Area 5, round 2: fixes", "Area 6, oracle" and
"Area 6, round 1". What is left of them is a bare "**Merged tree.**" paragraph, which now sits inside "Area 5, round 2 review".
The sections are intact at `4c838c5` (`git show 4c838c5:SaturnExplorer/Docs/FunctionalityVerification/15_DECOMPILER_STEP5.md`).
They were not restored here, because this branch is a review.

## Areas 1 and 8, round 2: the question that got no answer, and the badge that cannot say why

Branch `review/1-8-round2-fix`, fix in `a4153ad`. Ledger rows A1-4, A1-5 and A8-1 are `fixed`. The open question of the round 2
review (`mAutoPc` and a session reset) is a real hole and is fixed in the same commit. All five `kOpen` fixtures are moved into
`kPinned`; `kOpen` is empty (a sentinel entry) and `SE_MODEL_OPEN=1` runs nothing.

**Root cause (A1-4, A1-5).** One rule broken twice: `mAsked` means "this question was put and its answer, or its failure, is what is on
screen". Two paths recorded a question as asked that nobody answered. (1) `Accept` threw away the answer to the outstanding request when
the panel no longer held what it was asked under (a closed panel, the view moved) and cleared `mPending`, but not `mAsked`; the inputs
returning to that question before the panel was shown again found it "asked already". (2) `Update` set `mAsked = q` before looking at
`runner.Disabled()`, so a question formed while the runner was off was recorded without a request, and the runner's recovery (a new
session, the stuck request ended) left it never asked.

**Fixes (`DecompilerPanel.cpp`).** (1) A discarded answer to the outstanding request clears `mAsked`, except when the user Stopped it
(`mPending.cancelled`): the Stop is the user's answer, and A1-3's rule (not asked again until Enter or Refresh) stands. A panel that
is shown asks the new question in the same `Update`, so nothing changes for it. (2) `mAsked = q` is set after the cache lookup and the
disabled check, i.e. only when the question is sent or answered from the cache. A disabled runner leaves it unrecorded, so each
frame re-forms the question and does nothing until the runner takes requests again. This costs one `Question` build per frame while
disabled and sends nothing (`Submissions()` is unchanged; the control "A1-5 (control, disabled)" runs through it).

**A8-1: the stale reason.** `DecompilerPanel::GetStaleReason()` returns `None`, `Pending` (a replacement was asked for and is coming),
`Stopped` (the user stopped it, including a Stop the runner has not yet acknowledged), `Disabled` (the runner is off for the session) or
`NeedsRefresh` (nothing outstanding, nothing will be asked on its own: a running emulator's lag, or an entry the box cannot take).
`StaleTooltip()` is the text the badge draws, one sentence for each; only `Pending` says "its replacement is shown when it is ready". The
model's `CheckStaleReason` runs on every idle check and every held-engine check of every sequence: the reason exists exactly when
`ShownStale()` does; with a request outstanding the reason is Pending (or Stopped) and the tooltip promises a replacement exactly for
Pending; with nothing outstanding the tooltip never promises one, Disabled is the reason exactly when the runner is disabled, the
user's Stop on this very question reads Stopped, and each text names what to do (Refresh, stopped, disabled). `DECOMPILER_GUIDE.md` has a
row for the plain **stale** badge.

**The `mAutoPc` question.** `ResetSession` kept `mAutoPcValid`/`mAutoPc`, the PC the last auto-refresh was for, "not refreshed for again".
That is a verdict about a session's PC. After a reconnect the new session's first capture is not an auto-refresh (it is taken because
no snapshot is held), so the old value survives it, and when the followed PC later leaves the shown function and lands on that same
value the one refresh that PC is owed is suppressed and the old function stays on screen. The model has no registers, so no model
sequence can show it; the fix is a panel test, `TestAutoRefreshPcBelongsToTheSession` (park the PC in a hole and let it refresh; reset
the session with a new source, PC back inside the function; PC to the hole again: one more capture), and `ResetSession` forgets the PC.
Also considered and left out: clearing `mAsked` in `Update`'s "disabled or session ended" drop of the outstanding request. A first
version did. No sequence distinguishes it (mutation survived), and it is dead in `App`: the runner re-enables only in a new epoch
(`RecoverLocked`), a session ends with `InvalidateSession` plus `ResetSession` together (`App.cpp:543`), and `ResetSession` forgets
`mAsked`. It was removed, with a comment saying why.

**Mutation check** (each one expression; revert the fix, the pinned fixture fails; restored, `ctest` passes).

| Break | Fails | First check |
|---|---|---|
| a discarded answer does not clear `mAsked` (A1-4 as it was) | "A1-4 (resume)", "(scrub)", "(BIOS)", "(start)" (4 of 4); no random seed | NOT REFRESHED |
| `mAsked = q` before the disabled check (A1-5 as it was) | "A1-5" | NOT REFRESHED |
| the reason is always Pending when stale | seeds 2, 3 and others, "A1-3", "A8-1 (stopped)", "A8-1 (disabled)" | STALE REASON |
| no Disabled reason | "A1-5", "A1-5 (control, disabled)", "A8-1 (disabled)" | STALE REASON |
| the Stop is not remembered (`mStopped` never set) | seeds 6, 15, 16, "A1-3", "A8-1 (stopped)" | STALE REASON |
| the Stopped tooltip promises a replacement | seeds 5, 6, 15, 16, "A1-3", "A8-1 (stopped)" | STALE REASON |
| `ResetSession` keeps the auto-refresh PC | `TestAutoRefreshPcBelongsToTheSession` | panel test |
| a clear of `mAsked` in the `Update` drop (removed) | none | survivor, see above |

The A1-4 break is invisible to the 18 default seeds and to all 320 of the long run, because the generator never closes the panel, moves
the view and returns it before reopening; the four pinned sequences are the only check, as they were the only way the reviewer found it.

**Matrix** (RelWithDebInfo, a 4-core container shared with other agents). `SaturnExplorerDecompilerModelTests` default run: 18 seeds,
14 pinned sequences, 0 failures (808 operations, 832 idle checks, 262 held-engine stale checks, 566 submissions, 27 discarded). The
same with `SE_MODEL_SEEDS=320` (147 s): 0 of 320 fail; 12,643 operations (1,798 without effect), 858 finish-then-change races, 69 stops,
29 budget stalls, 7,305 results checked at install, 12,595 idle checks, 4,174 held-engine stale checks, 1,414 walks, 8,691 submissions,
419 discarded answers, 1,584 cache hits. Full `ctest -j2`: 96 of 96 pass, among them DecompilerRunnerTests (64.6 s),
DecompilerPanelTests, DecompilerModelTests, DecompilerFaultTests, Sh2GeneratedTests and Sh2DecompilerTests. Not run here: the game
corpus (not in the repository), ASan/UBSan/LSan, the timing comparison against the previous master.

**Not done.** `App` itself is still out of the model's reach (the scrub identity, the finder's hints, `AdoptNewEmulatorInstance`). The
model still has no registers, so auto-refresh, Follow PC and the halt hint are covered only by `DecompilerPanelTests`. The `NeedsRefresh`
tooltip for an entry the box cannot take is not reached by the model (it types only valid entries) and is read, not run. The "unimplemented"
indicator and `DecompileStatus::Disabled` are unchanged from the round 2 review (unreachable on this spec; produced by nothing). The
speed regression the review noted (a function over the budget re-runs the full budget on every scrubbed or stepped frame) is untouched.

## Area 5, round 4 review

Reviewer, branch `review/5-round4-review`, at `6ae4727` (master). Findings only; no source or test changed. Every fixture is in
`Docs/Decompiler/REVIEW_LEDGER.md` (A5-18 to A5-27) with its words, current and expected output and the Mednafen lines that
prove it; in the ledger's code quotes `or` stands for the C bar, which the table cannot hold. Reproduced on a RelWithDebInfo
gcc build with a scratch harness linked against the decompiler's objects (words at 0x06009000, the corpus by function) and
with `SaturnExplorerSh2GeneratedTests` (`SE_GENERATED_WORDS`). Mednafen read for each rule from the sources the table was
derived from (`mednafen/ss`): `sh7095.inc` (`OnChipRegWrite_u8`, `_u16`, `_u32`, `OnChipRegRead_INLINE_u8`, `_u16`, `_u32`,
`BSC_BusRead/Write_u8/u16/u32`, `MemRead`, `MemWrite`), `ss.c` (`BusRW_DB_CS0_*`), `scu.inc` (`SCU_FromSH2_BusRW_DB_*`,
`SCU_RegRW_DB_u8_W0/W1`, `BBusRW_DB_*`, `ABusRW_DB_*`, `ABus_Read`, `ABus_Write_DB32_*`), `vdp1.c` (`WriteReg`, `ReadReg`,
`VDP1_Write8_DB`, `_Write16_DB`, `_Read16_DB`), `vdp2.c` (`RegsWrite`, `RegsRead`, `RW_W8`, `RW_R16`), `smpc.c` (`SMPC_Write`,
`SMPC_Read`), `cdb.c` (`CDB_Read`, `CDB_Write_DBM`), `scsp.inc` (`SS_SCSP_RW_u8_W0/W1`).

**Area 5 is not clean: two P2, eight P3.** Round 3 built the table right where its rule matches Mednafen's case list; the two
P2 are a family whose case list differs from its neighbours' (the DMAC) and a path the table does not model at all (the cache).

**Not held.**

- **A5-18 (P2).** A word read at +2 of a DMAC register returns 0 and arms nothing (`OnChipRegRead_INLINE_u16` has no case at
  0x82 ... 0xB2); the output prints `SH2_CHCR0._2_2_` with the arming note, and the +0 caution claims "a read at +2 returns them
  too". The divider and the BSC have the +2 cases, which is why one `OnChipLong` rule fits them and not the DMAC: 11 golden
  cells are wrong and the golden test passes, since it is printed from the rule function.
- **A5-19 (P2).** A register read through the cached partition (0x05F80004, 0x05818000, 0x00100063) is a cache read
  (`MemRead` case 0 with CCR.CE, which the games set): a hit makes no bus access, a miss reads the whole 16-byte line. The
  output is the same as through 0x2xxxxxxx, with no caution. Half the generator's register literals are cached addresses and
  its emulator has no cache, so no oracle sees it.
- **A5-20 (P3).** Word accesses to SH2_CCR: the read returns CCR in both bytes, and the caution names "SH2_CCR, the register
  beside it" (its own read mirror); a word read at 0xFFFFFE82 reads 0x00FF, not 0.
- **A5-21 (P3).** The watchdog: no arming note on a read of WTCSR or RSTCSR; a keyed WTCSR write with TME clear clears OVF and
  resets WTCNT, which the note does not say; a byte write "does nothing" but disarms RSTCSR's WOVF clear.
- **A5-22 (P3).** Any write to SMPC_SF sets SF; `SMPC_SF = 0;` says nothing.
- **A5-23 (P3).** A byte write to the CD FIFO pushes a whole 16-bit word.
- **A5-24 (P3).** The byte-access `AddressError` cells of the 32-bit on-chip registers carry `!`, but Mednafen makes the access
  after raising the error; `HardwareNotes` drops the true arming note of a byte read of CHCR0 on that flag.
- **A5-25 (P3).** A misaligned register access is an address error that reaches the aligned register (a long at 0x25FE00A2
  writes SCU_IMS); printed `ram0x25fe00a2 = 3;` with nothing said.
- **A5-26 (P3).** Backup RAM is a byte device on the low lane, treated as ordinary memory: `mov.b` 5 then `mov.b` back at
  0x20180000 prints `return 5;`, Mednafen reads 0xFF.
- **A5-27 (P3).** A long access to an unnamed on-chip address below 0xFFFFFF00 is an address error, unsaid (the round 3
  fixer's "not done", now a row).

**Cells checked against the handler**, 40, one or more per family; held unless a row is named: DVSR `w1`, `w2@0`
(`WordWritesLong`), `w2@2`, `r2@0` (`LowHalfRead`), `r2@2`; DVDNT `w2@0` (divides), `w1` (A5-24); SAR0, CHCR0, DMAOR, VCRDMA0
`r2@2` (A5-18), `w2@2`, CHCR0 `r1` (A5-24); BCR1 `w2` (`KeyedWordWrite`), `r2@0`, `r2@2`, `w4` (keyed: the BSC section of the
golden file against `OnChipRegWrite_u32` 0xE0-0xF8); RTCSR `r2@2` (arms: case 0xF2); TIER `w2`, `r2`; FTCSR `r2@-1`
(`OddByteRead!`); DRCR0 and SBYCR `w2@-1` (no case at 0x70, 0x90); WTCNT `r2@-1` (returns WTCSR); RSTCSR `r2@-1` (A5-20); CCR
`w2`, `r2` (A5-20), the write mirrors 0xFE93/0xFE94 (`MirrorWrite`); IPRB, ICR `w1`, `r1`, `w4` (`AddressError`); SMPC_COMREG
`w2@-1`, `w4@-3` (low lane, bits 23-16 then 7-0); SMPC_SF `r2@-1`, `w1` (A5-22); the even lane at 0x1E (`SmpcPad*`); MINIT
`w1`, `w2`, `w4`; SCSP_MIBUF `r1`, `r2`, `r4` (pops, reads MOBUF too); SCSP_SCIEB `r1` (`x2`); SCSP_MSLC_CA `w4`
(`HalfBesideGap`: 0x40A is case 0x05, nothing); VDP1_TVMR `w1` (`ByteWritesWord`: `WriteReg(..., DB)`); VDP1_ENDR `w4` (0x0E is
no case); VDP2_TVMD `r2` (reads EXTEN: latch), EXTEN `r` (`x2`), TVSTAT `r` (clears EXLTFG), RAMCTL and PRIR `w4`
(`HalfBesideGap`); SCU_D0R, SCU_IST `w1` (masked: `IPending &= *DB or ~mask`), SCU_AIACK `r` (reads 0), SCU_PPAF `w1`, `r1`
(whole); CD_DATATRNS `r1`, `r2` (`FifoPopsTwice` at bit 19 clear), `r4` in the register window (`CdLongRead`), `w1` (A5-23);
CD_HIRQ `w4`, `r4`; CD_CR4 `w1` (`CdByteWrite`: `mask == 0xFFFF`).

**Held**, one line each:

1. The mirrors: VDP2 `A & 0x1FE` within 0x05E00000-0x05FBFFFF (`RW_W8`, `RegsRead`), VDP1 registers not repeated (`which` has
   cases 0-6 and 8-0xB only), SMPC `(A & 0x7F) >> 1`, SCU `A & 0xFC`, the CD window `(A & 0x7FFF) < 0x1000`, `(A & 0x3F) >> 2`,
   MINIT/SINIT by bit 23, the divider at +0x20, the watchdog at +8, the CCR's read mirrors; the 0x2 cache-through bit (case 1
   of `MemRead`/`MemWrite`) and bits 28-27.
2. The keyed cells ('v'): the BSC's long write takes the low half only with 0xA55A above (`OnChipRegWrite_u32` 0xE0-0xF8), its
   word write never (`_u16` `break`); the watchdog words at FE80/FE82 and their mirrors FE88/FE8A pick the register by the
   upper byte. A5-21 is about what the note leaves out, not the key.
3. `HardwareNotes`' gate on the cell's effect flag is right for the cells below 0xFFFFFF00 (a word at TIER reads no FTCSR, a
   long there returns 0 without reaching the switch); only the 0xFFFFFF00 byte cells are wrong (A5-24).
4. Read side effects and the oracle: no folded, duplicated or dropped hardware read found (patch 0018); the B bus's `x2`
   (EXTEN latches twice, a CD register read twice in the FIFO window) has no effect that a second read changes, so it is
   rightly not said.
5. The conservation oracle: `Accounts` excuses a fold only of ordinary memory (`InHardwareRange` or a mark refuses it),
   `DeadIsLoss` refuses every dead read of a register, a mark or a pointer; both corpora: 0 dead, every removal said.
6. Writes through the cached partition are write-through (`MemWrite` case 0 falls into case 1): the cached-partition rows
   cover reads only.
7. The game corpus, read by function against the disassembly: the FRT setup at 060716A8 (IPRA, IPRB, TIER, the FTCSR poll and
   clear), the CCR writes at 0606554C (`SH2_CCR = 0x11;`: cache on and purged), the divider sequences at 060642EE, 06065BC0,
   06065C94 (DVSR, DVDNTH, DVDNTL, then DVDNTL_SHADOW), the slave idle loop at 06003C46 (FTCSR, SINIT word, the associative
   purge at 0x46003304): every access printed once at its width and address, the cautions and notes as listed.

**For the coordinator.**

- The golden file is the table, not a check of it: it is printed from `WidthCellFor`, so a rule that does not match Mednafen's
  case list passes (A5-18 sat in 11 golden lines). An independent oracle would derive, per handler, the set of `A & 0xFF` cases
  Mednafen decodes at each width (the switch labels of `sh7095.inc`, `scu.inc`, `smpc.c`, `vdp2.c`) and compare it with every
  cell that claims the access reaches a register.
- Questions without a fixture: Mednafen decodes the whole 0xE0000000-0xFFFFFFFF partition by `A & 0x1FF` (`MemRead` case 7),
  `HardwareAlias` only where bit 14 is set (and bits 28 and 12 below 0x100), per `SaturnMemory.txt`, so 0xFFFF8E10 is TIER in
  Mednafen and unnamed here: which reference wins? The SCI's byte cells claim `Plain` from the manual; Mednafen ignores the SCI.
  No `onWrite` says that a CCR write with CP set purges the cache, that a write to 0x4xxxxxxx purges a line (the corpus' slave
  loop does exactly that before polling 0x06003304), or that a PTMR write starts VDP1 drawing.
- The generator has no width or cache model: its register accesses use the register's own width (`NewLit`), so narrow
  accesses are rare, and half its register literals are cached addresses. It cannot see A5-18 to A5-27; the golden file and
  the hand fixtures are the only width oracle.

**Runs.**

| | |
|---|---|
| `SaturnExplorerHardwareWidthTableTests` | passes: 337 registers, 3,038 cells (with A5-18's 11 wrong cells) |
| `SaturnExplorerSh2DecompilerTests` | all passed |
| Generator replays of A5-18 a and A5-19 a | 0 findings each (the oracle accepts both) |
| Game corpus, `hwram007.bin` | 839/839; every count 0; 16 missing and said; records: 14,843 folded, 0 dead, 16 unreachable, 27 switch lookups; notes 7, cautions 16 |
| Game corpus, `hwram008.bin` | 1,554/1,554; every count 0; 20 missing and said; records: 38,701 folded, 0 dead, 20 unreachable, 155 switch lookups; notes 22, cautions 20 |
| Direct accesses to the hardware areas by partition (both images; sound RAM included) | 0x2xxxxxxx 108 and 170, on-chip 25 and 30, associative purge 2 and 2, cached 0: no instance of A5-19; no word read of a DMAC register (A5-18), no CCR word, no byte to the CD FIFO; the 31 SMPC_SF writes are all of 1 |

## Area 2, round 3 review

Reviewer, branch `review/2-round3-review`, on master `6ae4727` (patches 0001 to 0028). Findings only, no fixes. New ledger
rows: **A2-12 (P1)**, **A2-13 (P2)**, **A2-14 (P1)**, **A2-15 (P2)**. A2-11 (open, being fixed) was not re-probed. Two more
defects of the same kind were found and are already in the ledger: a callee that sets GBR is taken to keep it by its caller
(A3-3, fixture b), and a function that returns a copy of an input register prints `void` (A3-5; `stc sr,r0 ; rts ; nop`,
`stc vbr,r0`, `sts pr,r0`, `mov r1,r0` and `mov r8,r0` behave the same, so SR, VBR and PR belong in that row's list too).

### The instrument: printed C, compiled, against the emulator

The area's oracles compare the spec with Mednafen (decode, differential execution) and the decompiler's accesses with the
emulator (the generator), and `TestCallsAndTraps` evaluates a printed return for call fixtures. Nothing compares the printed
value of ordinary SH-2 arithmetic with a run. The probe (scratch, not committed) does: it decompiles a function at 06009000,
runs it on the p-code emulator (`RunMachine`, as `TestCallsAndTraps`) for 32 to 64 inputs in r4-r7 (edges and random), writes the
pseudocode into a C file with the engine's types and helpers (`CONCATxy`, `SUBxy`, `ZEXTxy`, `SEXTxy`, `CARRY4`, `SCARRY4`,
`SBORROW4`; `in_rN` set to what the run had), compiles it with `gcc -fwrapv` and compares the returned r0 (r0 and r1 for an
8-byte return). The emulator is the spec, which the decode oracle holds equal to Mednafen op by op, so a difference is the
printing or the engine's transforms. Inputs:

- every ALU, shift, compare, extension, swap, multiply and DIV op, alone, with T from an input, returning the register, T
  (`movt`) and SR (`stc sr,r0`); `udiv` 32/16 (`div0u` and 16 `div1`), signed `div1` steps, 64-bit `addc`/`subc`/`negc` chains,
  `dmuls` high and low halves, `xtrct` with r0, `shll`/`rotcl`, branches on `cmp/hi`, `cmp/str`, `addv`, `subv`: 276 functions,
  0 differences;
- random register-only functions (34 Rm,Rn ops, 22 Rn ops, `sett`/`clrt`/`div0u`/`clrmac`, the `#imm,r0` ops, `add #imm`,
  `mov #imm`, `bt`/`bf`/`bt/s`/`bf/s` skips), 8 to 22 instructions: 7,500 functions. Every difference is one of three rows:
  A2-12 (r0 = 0 with r1 holding an input), A2-14 (`addv`), A2-15 (`shlr`/`rotcl` narrowed). One more class is the C reading
  itself: `(int4)-param_1` with `char param_1` means a byte negation in the engine's convention and an `int` one in ISO C
  (differs for 0x80). Not filed; see the questions.

`mac.w`, `mac.l` and memory operands were not compiled (the host's pointers are 64-bit and memory is big-endian); round 2
read their texts by hand.

### Shapes chosen by hand

Control registers: `stc sr,r0` returned alone prints `void` (A3-5); `get_imask` (`stc sr ; shlr2 ; shlr2 ; and #15`) prints
right; every `ldc` to SR, GBR or VBR that outlives the function disappears (A2-13); `ldc r4,sr ; rts ; stc sr,r0` prints
`param_1 & 0x3f3` (patch 0019's mask holds). Illegal and reserved words (`FFFF`, `0000`, in a slot, on one arm) are said:
"flow reaches bytes that do not decode", `halt_baddata()`; a branch, `rte` or `bsrf` in a slot fails the request (status 3);
`trapa` in an `rts` slot prints a call through `unaff_vbr + 4` with no warning (slot-illegal on the SH-2: A2-8 (b)).

### What held

- `SaturnExplorerSh2DecodeTests` (default, and `SE_DECODE_CASES=20000 SE_DECODE_SEED=909` with `SE_MEDNAFEN_SS` set: the word
  table equals `sh7095_idecodetab.inc` on all 65,536 words; 2,600,000 cases, 0 differ, 18,526 the known limit A2-4; 5,632
  history cells, 0 differ; 8 of 8 A2-4 shapes marked unsupported) and `SaturnExplorerSh2SemanticsTests` pass.
- The reference model, read again against `sh7095_ops.inc` for MAC.L/MAC.W (S, the 48- and 32-bit bounds, MACH |= 1), LDC and
  LDC.L (`0x3F3`, then the interrupt recalculation), RTE (SR before `DelayBranch`), TRAPA (`PC -= 2`), ADDV, ROTCL, SHLR, STC: no
  difference from `ExecRef`.
- The spec's delayed branches: `bsrf`/`braf` compute the target before the slot, `rts` and `jmp`/`jsr` read PR or Rm before it,
  PR is written before the slot (0006), SR before `rte`'s slot (0019).
- Every ALU op's printed value, alone and in 7,500 random sequences, outside the three rows.
- Illegal encodings are reported, not taken for code.

### The generator's coverage (an oracle hole, no fixture of its own)

Mnemonics in the listings of seeds 1 to 400 (14,004 instructions): `mov.l`, `mov`, `mov.w`, `mov.b`, `add`, `tst`, `bf`, `bt`,
`bra`, `rts`, `dt`, `cmp/eq`, `tas.b`, `extu.w`, `extu.b`, `shll2`, `cmp/pl`, `bsr`, `or`, `sub`, `xor`, `and`, `bf/s`, `bt/s`,
`sts.l`, `lds.l`, `jmp`, `jsr`, `mova`, `sts`, `lds`, `cmp/hs`, `cmp/hi` (and a few `mac`, `not`, `neg`, `mulu.w`, `div0s`, `or.b`
that are literal-pool words read as code). It never emits `addc`, `addv`, `subc`, `subv`, `negc`, `div0u`, `div1`, `dmuls.l`,
`dmulu.l`, `mul.l`, `muls.w`, `exts.b`/`exts.w`, `swap`, `xtrct`, the rotates, `shal`/`shar`/`shll`/`shlr`/`shll8`/`shll16`/`shlr2`/
`shlr8`/`shlr16`, `cmp/ge`, `cmp/gt`, `cmp/pz`, `cmp/str`, `movt`, `sett`, `clrt`, `clrmac`, any `ldc`/`stc`, `trapa`, `rte`,
`braf`, `bsrf`, or the `#imm,r0` and `@(R0,GBR)` logic ops. A2-13, A2-14 and A2-15 are in that set; A2-12 needs r1 left holding
an argument, which the grammar does not do. The generator replays each fixture with 0 findings.

### Questions (no fixture, or not area 2)

- The C convention: the engine prints byte arithmetic on a `char` parameter (`(int4)-param_1`) meaning a byte-wide operation;
  read as ISO C it is an `int` one. Does the program read the pseudocode as C? If so, a compile-and-run oracle needs the
  engine's casts made explicit.
- A dirty r1 also gives 8-byte returns with the right values (`CONCAT44(0x11,param_2)` for a function that returns 0x11 and
  leaves an argument in r1): a signature that claims r1. Area 3.
- `trapa` (and other slot-illegal words) in a delay slot print silently; A2-8 (b) accepts the exception as not modelled. Is a
  warning wanted?

### Not probed

MAC and memory operands through the compiled-C check; interrupts (A2-8); the game corpus (not in this container); the
sanitizers; the generator's long run.

## Areas 2 and 4, round 3: signed bounds and the top of memory

Fixer, branch `review/2-4-round3-fix`, fix commit `5a1297a` (tests strengthened in `45dd3bd` and `d64c50f`), on master `f5268df` (patches 0001 to 0028; this adds 0029 and 0031,
0030 is another fixer's, and `vendor.py` on a sparse clone of the pinned Ghidra (`918d44e`) reproduces the vendored tree exactly
with them). Ledger rows A4-7 and A2-11 (both P3) are `fixed 5a1297a`. Two engine changes, about 30 functional lines in
`jumptable.cc` and `ruleaction.cc`; the rest is the generator, the evaluator the call oracle uses, and two test blocks.

### 1. A4-7: the mechanism

`JumpBasic::calcRange` intersects a candidate switch variable's range with each guard's. A guard `cmp/gt r1,r4 ; bt default`
leaves r4 in "signed at most 2": the circle `[0x80000000, 3)`, the whole negative half and 0 to 2. Upstream then does

```
// It may be an assumption that the switch value is positive in which case the guard might not check for it.
// If the size is too big, we try only positive values
if (rng.getSize() > 0x10000) { positive = [0, 2^31) ∩ rng; if (!positive.isEmpty()) rng = positive; }
```

so the table became `[0, 3)`: three cases and a default, with the larger indices sent to the default by the guard. The hardware
takes a negative index through the guard (the sign of the compare), `shll2` makes it a negative offset, and `mov.l @(r0,r4),r1`
reads the word before the table: index -1 runs whatever that points at (the fixture: case 2). The engine had assumed what only
the programmer knew. That is the root cause; neither the host nor the printer was wrong about the table the engine gave them.

*Fix, patch 0029.* The assumption is gone. The range keeps the negatives, no table can fill it, `recoverModel` fails
(`jrange->getSize() > maxtablesize`) and the engine takes the path it already has for an unrecoverable table: "Could not recover
jumptable at 0x0600900e. Too many branches" and "Treating indirect jump as call". The host withholds the signature ("an indirect
call it does not resolve"). The fixture's output, which agrees with the emulator for every index (it says nothing of the ones the
guard lets through):

```
if (param_1 < 3) { xVar1 = (**(code **)(param_1 * 4 + 0x6009018))(); return xVar1; }
return 0x6009034;
```

Not under `loadsNotDuplicated`: it is a wrong table from an upstream heuristic, whatever is done with loads (the same call as
patch 0027). What stays recovered, each pinned by a fixture and the emulator: an unsigned bound (`cmp/hi`, `cmp/hs`), a signed
bound with `cmp/pz` as a second guard (the pair leaves `[0, 3)`), and a zero-extended index (`extu.b r4,r4 ; cmp/gt`: a byte
cannot be negative). The first sound outcome the row names (the table's range reflects the signed bound) cannot be a switch: a
32-bit signed bound is 2^31 entries; the second (the guard recognised as not bounding the table) is the same outcome as the
engine reports it. A sign-extended index (`exts.b r4,r4 ; cmp/gt`, the shape the row calls "often one loaded with a sign-extending `mov.b`") was
the silent three-case switch with the signature lent; it is now a switch over the byte with the engine's own "Sanity check
requires truncation of jumptable: cases may be missing" and "Could not find normalized switch variable", and the signature is
withheld ("a jump table recovered incompletely"). The byte, as a candidate on its own, has the whole 0 to 255 range, and the
sanity check cuts it where the words stop being code addresses. Warned and withheld, as the row asks.

### 2. A2-11: the mechanism

`RuleStoreVarnode` and `RuleLoadVarnode` turn an access to a constant address into a COPY of a Varnode at that address. For a
constant that leaves fewer than `size` bytes before the end of `ram`, `Funcdata::newVarnodeOut` and `newVarnode` call
`AddrSpaceManager::constructWrappingAddress`, which throws "Trying to construct memory range beyond end of address space" for a
space that does not allow a wrapped range (it is the only caller of that exception). The fixture's path where `tst` proved r4 = 0
reaches `mov.w r1,@-r4` (r4 = 0xFFFFFFFE) and `mov.l r3,@r4`: a 4-byte store at 0xFFFFFFFE. The exception unwinds the whole
function, so the row's "the other path is ordinary code" did not matter.

*Fix, patch 0031.* The two rules leave the LOAD or STORE alone when the constant access does not fit in the space (a file-local
`pastSpaceEnd`, which is the exception's own condition). It stays a LOAD or STORE through its constant pointer and is printed as
one: `*(xunknown4 *)0xfffffffe = in_r3;` under `if (param_1 == 0)`. An access that ends at 0xFFFFFFFF is inside the space and is
folded as before. The ledger lists it once, as an access through a pointer. On the hardware that path is an address error (a
longword at 0xFFFFFFFE, in the on-chip register region); the output does not say so (see "Not done").

### 3. Tests, the evaluator and the generator

- **`TestSignedGuardTable`** (after `TestTailJumpToUnreachedCode`): the ledger's fixture with `cmp/gt` and with `cmp/ge`, the
  sign-extended form (`exts.b`), and four controls (`cmp/hi`, `cmp/hs`, `cmp/pz` with `cmp/gt`, `extu.b` with `cmp/gt`). Each runs on the emulator for r4 = 0, 1, 2, 3,
  -1, 0x80000000 and 0x7FFFFFFF, and the evaluated text must agree (`PseudoAgreesWithRun`). The signed ones must be an unresolved
  jump, warned, with the signature withheld and not lent to a caller; the emulator's answer for -1 is 2 (the word before the table).
  The oracle is the call oracle widened: **`PseudoEval` now reads `switch`** (`case`, `default`, fall-through, `break`, a case
  value that is negative), which it refused before, so the text that used to be printed for the fixture is rejected by value
  (it returns 0x6009034 where the code returns 2), not because the evaluator could not parse it. The evaluator's own checks
  include the old text, a fall-through, and a `break` inside an `if`.
- **`TestAccessPastSpaceEnd`**: the row's three shapes (a longword store, a longword load at 0xFFFFFFFE, a word store at
  0xFFFFFFFF), each `Ok`, with the text, the ledger entry (once, through the pointer) and the reference run (`ReferenceOk`); the
  last longword and the last byte of the space, folded as before (`Ramfffffffc`, `Ramffffffff`).
- **The generator** (grammar 2). A quarter of the first function's jump tables are bounded by `cmp/gt` or `cmp/ge` (the same
  bounds, `n-1` and `n`). The word before such a table is an entry of its own, to one of the table's cases, so that index -1 does
  something the emulator can follow; the run set gains a run with index -1 for such a function; and the table check (every
  destination the run took is a case the output prints, or a warning says cases may be missing) takes index -1 as an index, its
  destination being the entry before the table. A signed guard is made in the first function only: a callee is entered with
  whatever its caller left in the register, a negative number among them, and would jump anywhere. The choice is drawn from a
  stream of its own (as `macRng` is), so the programs of earlier seeds are unchanged except for the tables that became signed
  (219488, the A2-11 seed, is the same program). A second change was forced by the first: after an indirect jump the output treats
  as a call, what the run does is not the output's to list, though an instruction after it can be in the body by another path (the
  join after the cases). The `dropped` check now skips an access made after such a jump's delay slot, as the `outside-body` check
  has done since A4-5. Replay lines carry the entry before the table as a sixth field of `SE_GENERATED_TABLES`.

### 4. Matrix

**Mutation** (a scratch build with the change applied or reverted; restored, all pass):

| Mutation | Fails |
|---|---|
| patch 0029 reverted | `TestSignedGuardTable`: 20 checks (the evaluated text disagrees with the emulator for -1 and 0x80000000 on both signed fixtures; `switch(` and the lent signature where they must not be; the warning, the withheld reason, no `case`/`default`; the signature lent to a caller; and for the sign-extended index the lent signature and the missing "cases may be missing"); the generator: 50 of 3,000 seeds (`table-case`: index -1 goes to a case the printed switch does not hold; `lent-incomplete:table-case-missing` in 44) |
| patch 0031 reverted | `TestAccessPastSpaceEnd`: 12 checks (status Failed with the exception's text, on all three shapes); seed 219488 (`[n:status]`) |
| patch 0029 over-reaching (every range full) | 60 checks: the four controls and the existing switch fixtures |
| patch 0031's bound off by one (`size+1`) | 2 checks: the last longword and the last byte of the space are no longer folded |
| `PseudoEval` ignoring the case labels | 7 checks of the evaluator's own |
| `PseudoEval`'s `break` leaving only the innermost block | 1 check (the first version of the evaluator's checks passed this mutant; the `break` inside an `if` was added) |
| generator: no run with index -1 (patch 0029 reverted) | 0 findings of 3,000 (the unit fixture still fails): the run is what finds it |
| generator: no signed guards made (patch 0029 reverted) | 0 findings of 3,000: this is the grammar as it was, which could not see the class |
| generator: the table check ignoring index -1 (patch 0029 reverted) | 0 findings of 3,000 |
| generator: no exemption after an indirect jump treated as a call (patch 0029 in) | 6 of 3,000 seeds (`dropped:*`) |

**Matrix** (RelWithDebInfo, gcc; the machine ran other work):

| | |
|---|---|
| `ctest` | 96 of 96 passed (68 s with -j3), a full build of every target |
| `SE_GENERATED_GRAMMAR=2 SE_GENERATED_SEEDS=3000` | 0 failing seeds, 0 findings. 843 tables, 100 signed (52 `cmp/gt`, 48 `cmp/ge`); 5,941 functions |
| `SE_GENERATED_GRAMMAR=2 SE_GENERATED_FIRST=200001 SE_GENERATED_SEEDS=20000` (the review's window) | 0 failing seeds, 0 findings (497 s); the review run had 1 (219488). 39,066 functions; 5,307 tables, 657 signed (324 `cmp/gt`, 333 `cmp/ge`) |
| `SE_GENERATED_GRAMMAR=1 SE_GENERATED_SEEDS=1000` | 0 failing seeds, 0 findings |
| Seed 219488 (`SE_GENERATED_SEED`, grammar 2) | passes (it failed with `[n:status]` before) |
| Game corpus, `hwram007.bin` | 839/839; every count 0; 16 missing and said; records 14,843 folded, 0 dead, 16 unreachable, 27 switch lookups; **0 of 839 outputs changed** against the same build with patches 0029 and 0031 off (every prologue decompiled, outputs compared file by file) |
| Game corpus, `hwram008.bin` | 1,554/1,554; every count 0; 20 missing and said; records 38,701 folded, 0 dead, 20 unreachable, 155 switch lookups; **0 of 1,554 outputs changed** |
| Game corpus, every prologue decompiled twice, patches off and on | 1,554 functions: 23.1 s and 27.0 s with the patches off, 22.4 s and 21.1 s with them on (a loaded machine: no slowdown to see) |
| `vendor.py` on a sparse clone of `918d44e` | reproduces the vendored tree exactly (84 library sources, 94 headers, 5 spec files; patches 0001 to 0029 and 0031) |
| ASan + UBSan + LeakSanitizer, gcc Debug, `Sh2DecompilerTests` (`detect_leaks=1`) | no AddressSanitizer or LeakSanitizer report; the only UBSan lines are the two tolerated (`address.hh:605`, `slghpatexpress.cc:979`); the suite passes (55 s), and with `SE_GAME_HWRAM=hwram008.bin` (954 s); `Sh2GeneratedTests` over 300 grammar-2 seeds under the same build is clean and passes |

**Not done.**
- The access past the top of memory is printed as written. The hardware takes a longword at 0xFFFFFFFE as an address error, and
  nothing in the output says so; the width table (area 5) is the place for such a caution, and it has no row for an access that
  leaves the space.
- A signed bound with an index that is not a plain register (a `mov.b`-loaded index, a computed one) was probed by the
  sign-extended register form only (`exts.b`). A `mov.b @r4,r4` load adds a memory read to the same shape; it was not probed.
- The generator makes signed bound checks in the first function of a module only. A callee with a signed table would need its
  callers' arguments constrained, which the grammar does not do.
- Grammar 1 has no jump tables, so it has no signed bound.
- The `PseudoEval` ignores the width of a parameter (`xunknown1 param_1` is the whole register), so the fixture with a byte index
  is fed bytes.

## Area 5, round 4: the independent check, and A5-18 to A5-27

Fixer, branch `review/5-round4-fix` from master `4b690b1`, fix commit `5b01226`. Ledger rows A5-18 to A5-27 are fixed (none accepted); the work is in
`SaturnHardwareMap.{h,cpp}` and, inside `HardwareNotes`, `Sh2Decompiler.cpp`. No engine patch (0001 to 0028 unchanged). The
oracle was built first, run against the unfixed map, and found the reviewer's rows and three more gaps before any rule was touched.

**The root cause the reviewer named.** `HardwareWidthTable.golden.txt` is printed from `WidthCellFor`, so it cannot disagree with it:
eleven DMAC cells were wrong in the golden file and the test passed. Every other root cause below is a case the table had no cell for
or a fact about the bus that no cell models (the cache, alignment, a gap), and nothing compared those with Mednafen either.

### The independent check

- `Docs/Decompiler/tools/derive_handler_cases.py <mednafen>/mednafen/ss` reads Mednafen's handlers and writes
  `FrontEnd/tests/MednafenHandlerCases.txt` (44 lines, checked in; `--check <file>` re-derives and fails on a stale copy). It
  never reads the map. It parses `sh7095.inc` (the six on-chip handlers, each in its `A & 0x100` halves: the switch labels grouped by
  shared body, the mask each applies, whether the branch raises an address error before decoding, whether a case does something with
  the access, has a read effect, starts a division, a DMA, VDP1 drawing or sets CCR), `scu.inc` (`SCU_RegRW_DB_u8/u16/u32_W0/W1`, one
  switch per width), `vdp1.c`, `vdp2.c` and `vdp2_render.c`, `smpc.c`, `cdb.c`, `scsp.inc`, and states the CPU bus's own facts
  (`MemRead`/`MemWrite`: a misaligned access raises the address error and is made aligned down, case 0 is the cache, a write goes
  through, case 7 decodes the partition by the on-chip handlers, cases 2 and 5 purge). The Mednafen tree at the review path has the
  `.c` names the old audit script (`audit_hardware_map.py`, written for `.cpp`) does not find; the new script takes either.
- `FrontEnd/tests/HardwareHandlerCheck.h`, called from `SaturnExplorerHardwareWidthTableTests` (third argument, the cases file),
  compares: every **on-chip cell** (526 cells with a handler: 393 whose rule says the access reaches the register, 133 whose rule says
  it does not or is an address error) with the case labels at that width and half, the register's group, `pex`, and its own read or
  write effect (a flag armed, a latch, a division) happening exactly when the case has it; every **SCU cell** (490) with the width's
  own switch, decoded and doing something or not; every **VDP1, VDP2, SMPC, CD block and SCSP register and direction** (228) with its
  switch and its read effect; the **bus facts** (2,597 probes: every register misaligned at 2 and 4 bytes, read through the cached
  partition, through the cache-through one, written through the cached one, the on-chip partition from eight prefixes);
  and **every aligned address of 0xFFFFFE00-0xFFFFFFFF**, named or not, at three widths and both directions (1,792 probes): "address
  error" exactly when the handler raises one. The SCI and the UBC are claimed from the manual and checked to stay undecoded (21).
- **What it checks that the golden file does not:** that a rule's claim (reaches / does not reach / address error / effect happens)
  is true of Mednafen's labels. **What it cannot see:** which bits of the value reach the register (`[15-0]`), the lane rules of the
  16-bit buses (SMPC, SCSP, CD, MINIT/SINIT, backup RAM), the keys of the keyed writes, FIFO pops, filler values (`0xFF`, `0x00FF`,
  the divider's shadow), VDP2 colour RAM, value-independent write effects other than SMPC_SF (VDP1 `ENDR`, SCU `AIACK`/`T1MD`, the SCSP
  common block: `F` in the cases file, not said), a DMA transfer starting on a write to CHCR or DMAOR (excused in the test, with the
  reason: it is the register's purpose), and the text of any caution. Those stay with the hand fixtures and the golden file.
- **It fails on the A5-18 cells before the fix:** unfixed it reported 2,584 disagreements: the 11 DMAC cells at +2 (`SH2_SAR0 r2@2 ...
  the handler has no case that does`, and the same for DAR0, TCR0, CHCR0 (also the read effect), SAR1, DAR1, TCR1, CHCR1, VCRDMA0,
  VCRDMA1, DMAOR), 16 byte-read cells of CHCR0/CHCR1/DMAOR/RTCSR whose arming does happen (A5-24), 8 divider byte writes whose
  division happens (A5-24), WTCSR and RSTCSR read effects with no `onRead` (A5-21), the CCR purge unsaid (A5-20 neighbour), 72
  cached-partition register reads (A5-19), the misaligned accesses (A5-25), the unnamed on-chip addresses (A5-27, 90+90+50+49), 7
  bus-partition probes per register (the question below), and four it also found that were not rows: `SCU_VER` is a constant read the
  first classifier took for filler (script fix), the watchdog's byte writes are not keyed writes (A5-21 part 3), and two cells of
  the keyed word port that must be judged at the port's case. Fixed and passing: 0.

### Root causes and fixes

| Row | Root cause | Fix |
|---|---|---|
| A5-18 (P2) | One rule `OnChipLong` for three handlers; sh7095.inc has the +2 word case for the divider and the bus state controller, not for the DMAC (cases 0x80, 0x84, 0x88, 0x8C, 0x90, 0x94, 0x98, 0x9C, 0xA0, 0xA8, 0xB0 only) | New family `OnChipDmac` (SAR0 to DMAOR), rule `LongHalfRead` (d): a word read at +2 returns 0, reads nothing, arms nothing. The +0 caution says "a read at +2 returns 0 and reads nothing" for the DMAC and keeps "returns them too" for the divider |
| A5-19 (P2) | The table has no model of the bus before the register: a read through `0x0xxxxxxx` is a cache read with CCR.CE (hit: no bus access; miss: the 16-byte line, each register read with its effect), exactly as the games set it | `NarrowAccessEffects` gives every read of a register area (or backup RAM) through the cached partition a caution: "a read of REG through the cached partition is a cache read when the cache is on (SH2_CCR.CE) ... the whole 16-byte line ... read it through 0x2xxxxxxx to bypass the cache". Decided: no state about CCR is kept, so the caution cannot know a function runs with the cache off; writes get none (they are written through, `MemWrite` case 0 falls into case 1). The generator is aware: new rule 3b, every register read its emulator traces at an address below `0x20000000` that the output shows at that constant address must carry a cache-read caution on its instruction (769 said, 98 reads through merged pointers excused: a plain load names no register, patches/0009). Its emulator has no cache and the literal choice was left as it was: half the register literals stay cached addresses and are now checked, not suppressed |
| A5-20 (P3) | The text of `EvenByteRead` did not know the CCR reads back in both bytes; `NameAt(addr+1)` found the register's own mirror; the word beside RSTCSR reads `0x00FF` | CCR text, `NameBeside` (a register other than itself), `0x00FF` beside RSTCSR |
| A5-21 (P3) | WTCSR and RSTCSR had no `onRead`; the keyed WTCSR note ignored TME; a byte write was a `KeyedWrite` cell | `onRead` on both ("a read arms clearing OVF / WOVF"); the note stores `V & 0x67` and says TME clear = OVF cleared whatever a read saw and WTCNT reset, TME set = OVF only if armed and SBYCR's SBY cleared; rule `KeyedByteWrite` and `KeyedPortCaution`: "this write does not write it, and it cancels a pending SH2_RSTCSR WOVF clear (a read arms it)" (also on an ignored keyed word) |
| A5-22 (P3) | No `onWrite` for SMPC_SF | `onWrite` plus `noteWrite`: every write to SF says it sets SF to 1, except a write of 1 (the line says it) |
| A5-23 (P3) | A byte write to the CD FIFO was `Plain` | Rule `FifoByteWrite` (b), both windows |
| A5-24 (P3) | The byte `AddressError` cells of the 32-bit on-chip registers had the effect flag off; Mednafen raises the error and then makes the access | The cells keep `kWidthEffect` (golden `AddressError*`, no `!`); `HardwareNotes` prints the arming note beside the caution |
| A5-25 (P3) | `CoveredRegisters` took the misaligned address as given | `AccessAddress` (aligned down, except on-chip, which decodes the address as given); every consumer uses it; a caution "a misaligned N-byte write at ... is an address error on the SH-2 ...; the access is made at ... (REG)"; the "through a mirror" note is not made for a misaligned access |
| A5-26 (P3) | Backup RAM was ordinary memory | In `kBusAreas` (volatile, never folded, through both partitions and the bus repeats), with a caution per width and lane (byte at an even address: nothing / `0xFF`; word write keeps the low byte; long: two of them) |
| A5-27 (P3) | The table has rows for registers only | A generic caution for an on-chip access that is an address error (a long below `0xFFFFFF00`, a byte above) when no register's cell already said so; the check probes every aligned address of the area, named or not |

**The reviewer's two questions.** *Which reference wins for `0xE0000000-0xFFFFFFFF`?* Mednafen: its `MemRead`/`MemWrite` case 7 hands the
whole partition to the on-chip handlers, which look at bit 8 and the low byte, so 0xFFFF8E10 is `SH2_TIER` there. The manual's bit 14
(and bits 28 and 12 for the low half) is real hardware's, where the rest read garbage; the decompiler describes the emulator the tool
runs and every other round took Mednafen where the two differ (the 0xA partition, SCU_AIACK). `HardwareAlias` now decodes by `A & 0x1FF`;
an address that is not the register's own prints by address with "0x... is REG through a mirror". Fixture `q_alias`; the check's
bus probes (eight prefixes per register) pin it. The SCI's byte `Plain` stays from the manual (Mednafen ignores it) and the check
holds both families at "undecoded" so a Mednafen that grew a handler would fail. *Missing notes:* added, because cheap, as value
notes (`WriteEffects`) and an access note: `SH2_CCR` with CP set purges the whole cache ("CP (bit 4) is set, so this write purges the
whole cache"; unknown value: "a write with CP set purges"), a write to `VDP1_PTMR` with bit 0 set starts drawing, and an access
through `0x4xxxxxxx` or `0xAxxxxxxx` is an associative purge of the cache line for the address (a read returns all ones). Not done:
the same for CCR bits other than CP, and a model of CCR's state for the cache caution.

### Mutation checks

Every rule entry below was broken in the source, the three binaries rebuilt, and the golden file, the independent check, the
fixtures and (A5-19) the generator run; restored, all pass. "Handler" is the number of disagreements with Mednafen's handlers.

| Entry broken | Handler check | Golden | Fixture CHECKs | Generator |
|---|---|---|---|---|
| DMAC word read at +2 reaches the register (A5-18) | 14 | fails | 5 | |
| byte `AddressError` cell loses the effect flag (A5-24) | 24 | fails | 1 | |
| no cache caution (A5-19) | 72 | passes | 3 | 32 findings |
| WTCSR without `onRead` (A5-21) | 2 | fails | 1 | |
| keyed byte write a `KeyedWrite` cell (A5-21) | 3 | fails | 3 | |
| WTCSR note ignores TME (A5-21) | passes | fails | 2 | |
| SMPC_SF without `noteWrite` (A5-22) | 1 | passes | 1 | |
| FIFO byte write plain (A5-23) | passes | fails | 1 | |
| no misaligned caution (A5-25) | 1,756 | passes | 2 | |
| backup RAM not a hardware area (A5-26) | passes | passes | 4 | |
| backup RAM says nothing (A5-26) | passes | passes | 2 | |
| no unnamed-address error (A5-27) | 279 | passes | 1 | |
| CCR word read as before (A5-20) | passes | passes | 1 | |
| `NameBeside` finds the CCR mirror (A5-20) | passes | passes | 1 | |
| partition 7 needs bit 14 again (the question) | 360 | passes | 2 | |
| no CCR purge note | passes | passes | 1 | |
| no PTMR start note | passes | passes | 1 | |
| no associative purge note | passes | passes | 2 | |
| `HardwareNotes` gate on the cell's effect flag inverted | passes | passes | 17 | |
| the cases file gains a DMAC +2 case (the oracle itself) | 1 | passes | 0 | |

The first run of the last row passed: the check's list of "claims nothing" rules lacked `LongHalfRead` and `KeyedByteWrite`, so a
Mednafen that decoded the DMAC at +2 would have gone unseen. Added, rerun: caught. Mutations whose only witness is a fixture (the
notes, backup RAM) are as strong as their fixture: the independent check does not see lanes, notes or text, and says so above.

### Matrix

| | |
|---|---|
| Golden file | regenerated after the rules were right (`SE_UPDATE_GOLDEN=1`) and read line by line: 337 registers, 3,038 cells; rules 29 -> 32 (`FifoByteWrite`, `LongHalfRead`, `KeyedByteWrite`), families 12 -> 13 (`OnChipDmac`); the DMAC rows (`r2@2=LongHalfRead`, byte reads `AddressError*`), the divider byte writes `AddressError*`, CHCR/DMAOR/RTCSR byte reads `*` where they were `!`, WTCSR/RSTCSR `r1=Plain*`, the three watchdog byte writes `KeyedByteWrite`, the FIFO byte writes `FifoByteWrite`, SMPC_SF writes `*`, and the keyed-write section's text. Nothing else moved |
| `ctest` | 96 of 96 passed (83 s with -j3), `SaturnExplorerHardwareWidthTableTests` (with the handler check), `SaturnExplorerSh2DecompilerTests` and `SaturnExplorerSh2GeneratedTests` among them |
| Game corpus, `hwram007.bin` | 839/839; every count 0; 16 missing and said; records 14,843 folded, 0 dead, 16 unreachable, 27 switch lookups; notes 7 -> 12, cautions 16 -> 16 |
| Game corpus, `hwram008.bin` | 1,554/1,554; every count 0; 20 missing and said; records 38,701 folded, 0 dead, 20 unreachable, 155 switch lookups; notes 22 -> 27, cautions 20 -> 20 |
| Corpus text that changed (the master binary's output beside the new one's, every function compared) | the pseudocode, the warnings and the cautions of all 839 and 1,554 outputs are byte-identical; only note lines appeared, five in each image, each read. `hwram007`: `06003C46` and `060340FE` gain "an access at 0x46003304 is an associative purge, not a memory access: it invalidates the cache line for 0x06003300" (the slave idle loop's purge; true, `SH7095_Cache_AssocPurge`); `060258AC` (two) and `06025910` gain "SH2_CCR: CP (bit 4) is set, so this write purges the whole cache" on writes of `0x11` (CE and CP; true). `hwram008`: the same, in `06003C46` and `0607513E` (purge), `0606554C` (two) and `060655B0` (CCR) |
| Generator | `ctest` short run passes; `SE_GENERATED_SEEDS=3000`: 0 findings, 769 cached-partition register reads said, 98 through merged pointers excused |
| ASan + UBSan + LSan, gcc Debug | `Sh2DecompilerTests`, `HardwareWidthTableTests` and `Sh2GeneratedTests` (300 seeds), `detect_leaks=1`: no AddressSanitizer or LeakSanitizer report; the only UBSan lines are the two tolerated sites (`address.hh:605`, `slghpatexpress.cc:979`); all pass |

### Not done

- The SCI's and the UBC's cells stay from the manual (no Mednafen handler); the check only holds them undecoded.
- Value-independent write effects other than SMPC_SF (`F` in the cases file: VDP1 `ENDR`, SCU `AIACK`, `T1MD`, SCSP common block) are not said; a
  DMA start on a write to CHCR or DMAOR is not said (excused in the test).
- The lane rules of the 16-bit buses, the bits that reach a register, the keys, the FIFO pops, and the text of every caution are
  pinned by hand fixtures and the golden file only.
- The cache caution cannot know CCR.CE is clear for a function (it would need the CCR state, which is a flow property, not a map property).
- VDP2 colour RAM byte writes (A5-16 part 4) stay accepted.

## Area 3, round 3: lent effects, inputs outside r4-r7, the structure-return pointer

Fixer, branch `review/3-round3-fix`, from master `f5268df`; fix commit `fdc68ff`, with `b1d783a` and `fb2a517`. Ledger rows A3-3, A3-4 and A3-5 are fixed, A3-6 is
accepted. One engine patch, `0030-host-stack-alias.patch`; patch 0029 is the other fixer's. Every fixture of the four rows was
reproduced first (the A3-3 fixtures: 20 failing checks on master, the oracle's "the code returned 00000007 ..., the pseudocode returns
00000005" for r8, r9 and r14, and the GBR and VBR cases returning param_2 by the run and `param_1` by the text).

### The classes, not the instances

- **A3-3: the lent effects ran one way.** `LendEffects` (patch 0028) marks the registers the model kills (r2-r7, SR, MACH, MACL) that
  `ReturnCheck` proved kept as unaffected. The registers the model keeps (r8-r14, GBR, VBR, and r15 as the stack) were never looked at, so
  the proof `ReturnCheck` had already made (the bit clear in `CallEffects::preserved`) was thrown away at the call. Now the same proof
  kills them: a proved change to r8-r14, GBR or VBR is `killedbycall` in the lent effects, and the caller reads `extraout_r8` and is
  withheld on it, as for r4-r7. r15 cannot be killed (the engine's stack analysis would lose the frame); `ReturnCheck` now also records
  where r15 ends at every return (`CallEffects::spDelta`, the offset from its entry value, when it is the same constant at all of them),
  and `LendEffects` lends it as the callee's **extrapop**, which the engine applies to the stack pointer after the call. The caller of
  the ledger's fixture c then reads the word the callee pushed (`xStack_8`), where before it popped its own (`return param_1;`). A
  callee whose r15 differs between returns, or is not a constant, is withheld ("it leaves r15 changed by an amount that is not the same
  at every return"). `ReturnCheck`'s own call model applies the delta, so the caller's stack slots stay tracked.
- **A3-5: two holes.** (a) `WithholdReason` looked at the engine's input varnodes only where `possibleInputParam` accepted them (r4-r7,
  the stack). It now looks at every input left in the register space: a register that is no parameter and is read for its value on entry
  (r0-r3, r8-r14, GBR, MACH, MACL, or the T flag) withholds the signature, because no signature can pass it and the caller's setup of it
  is dead code in the caller's text. (b) The engine does not leave such an input in the text when it also drops the return that was made
  of it: `AncestorRealistic` takes a value that comes only from non-parameter inputs for no value, so `rts ; sts macl,r0` prints `void f(void)
  { return; }` and the input is gone with it. `ReturnCheck` sees r0's origins at every return (a `mov` or `sts` of another register
  leaves it exactly that register), and a function the engine calls void whose r0 comes from such a register is withheld ("its return is
  computed from macl on entry"). Both hold for the ledger's a, b, c, d.
- **A3-4: the engine cannot see the pointer, and the cspec cannot be made to.** The SH ABI's returned-structure address is in r2, which
  the default model has no input for. Adding one is not a cspec change that works: parameter trials are positional and a chain of
  inactive trials (r4-r7 untouched) cancels a later active one, while an r2 entry placed first would put a parameter on every call that
  sets r2 as a scratch. The engine's `HiddenReturnAction` needs a structure-typed return, which nothing here infers. The fixture's
  root is smaller: `mov r15,r2` is the only use of the frame address, so dead-code removal deletes the copy before
  `AliasChecker` looks for pointers into the frame, and the local has no alias, its INDIRECT at the call is collapsed, and the read after the call is the value
  stored before. The same code with the address in r4 prints `func(axStack_c); return axStack_c[0];` (checked). So the fix is for the host to say what
  the code does: `ReturnCheck`, run on the raw p-code before the actions, notes an **exact** frame address in r2 at a call whose callee has
  no lent signature (a lent one is never lent when it reads r2: A3-5), and declares the lowest such offset with `ScopeLocal::declareHostAlias`
  (patch 0030, 26 functional lines in varmap.hh and varmap.cc): `AliasChecker` counts it as one more pointer base with no Varnode, also
  when nothing else reads the stack pointer any more, and every local from that offset up is aliased. It lives as long as the function (the host
  sees no restart). The text now reads `axStack_c[0] = 5; func_0x06009020(); return axStack_c[0];`, the evaluated pseudocode no longer claims 5,
  and the call still shows no argument (the ABI register is not an input of the model).
- **A3-6: accepted.** `ReturnCheck` takes a constant address never to be in the stack, as the engine's own stack analysis does for every
  local it folds. The fixture needs a callee that stores over its own saved r4 at the one address that slot has for one entry state of
  r15. Refusing the assumption in the proof clobbers every saved slot, the saved PR included, at each store to a constant address, so every function
  that saves PR and writes a global would be withheld ("a return not to its caller"). `TestCallsAndTraps` keeps the fixture as a pinned
  limit, with the run's 7 beside the lent text (`return param_1;`); `DECOMPILER_GUIDE.md` says it where the proof is described.

### Decisions, each on a measurement

- **Withhold, do not carry (A3-5).** Carrying an input into the lent prototype needs a prototype with explicit storage per parameter:
  `FuncProto::setPieces` assigns positions from the model only, so it is an engine API (a further patch), a storage field in `Prototype`, in
  `ShapeOf` and in the runner's digest, a type-of-register convention for `sr` and `gbr`, and a caller printing `f(x, in_r2)`. Withholding is
  what the program's rule says ("nothing incomplete is lent") and costs 68 and 75 of 680 and 1,083 (above) lent signatures per image (below), all of them hand-written
  conventions. Carrying also would not fix the r2 case for a callee that was not decompiled first.
- **SR is not always an input.** `stc sr,r0 ; rts` is an interrupt-mask helper's idiom (20 of the lent functions of image 007 returned `in_sr`). When
  the engine keeps the value (the compiler's `stc sr,r1 ; mov.l r1,@r14 ; mov.l @r14,r0`) and every use of the input is a whole copy, a
  return or a store, it is machine state, not a call's input, and still lends. Anything computed from it (`movt`, `bt`, `addc`) is
  the T flag and withholds. The bare `rts ; stc sr,r0` is dropped by the engine like `sts macl,r0` and is withheld by (b).
- **(b) only on a path with no call, only r0.** First version flagged every origin of r0 and r1 at a return: 23 functions of image 007
  that save r8 and call were withheld "its return is computed from r8 on entry". A call's outputs share one set of origins (everything
  any of the callee's registers can come from), so after a call r0 has the origins of the callee's other registers. A path that has
  made a call is therefore not asked (`State::called`); the engine's returns that are dropped are leaf functions'. r1 is scratch for a 32-bit
  return and meets its own entry value wherever a loop may not run. r0's own entry value is no read either (a path that leaves r0 alone
  and one that does not meet). A non-leaf function whose dropped return comes from such a register is left to (a), which sees an input
  the engine kept. Not done: nothing sees one that neither does.
- **Only r2 (A3-4).** r0, r1, r3 hold scratch frame addresses the compiler leaves behind and r8-r14 hold the frame pointer (r14 = r15 in
  84 of the generator's 601 functions), so a rule over them would alias every local of every function. A frame address with an index added
  to it has no offset to declare: the first version declared the whole frame for it and printed `axStack_80000000 [2147483580]`, so only an exact one counts.
- **Five existing fixtures read an input by accident.** The three `macAfterPoll` fixtures end `sts macl,r0` after a `mac.l` (the accumulator
  is the caller's), the two-hop chain stores r2 (`mov.l r2,@-r4`), `indexedLocal` indexes with r0, and `Sh2DecodeTests`' A2-5 function (patch 0020's recycled tree) adds to an r2 and
  branches on a T nothing set (its slot is now `tst r4,r4`; not re-mutated against patch 0020, whose revert does not apply to a file with later notices). Each now uses what it needs from the
  parameters or a `clrmac` (`sts macl,r0` becomes `clrmac`, `mov.l r2,@-r4` becomes `mov.l r5,@-r4`, `add r0,r1` becomes `add r4,r1`); the
  assertions are the same, the machine-code reads are r4/r5.

### The oracle

- `PseudoEval` (the evaluated pseudocode of `TestCallsAndTraps`) reads an array element as memory: the engine prints a local as an array
  when a pointer into it exists, and whatever a call wrote to it is unknown. Without it the corrected A3-4 text could not be evaluated
  ("text left in an expression").
- Generator, grammar 2 (separate stream, so every other program of a seed is unchanged; one module in three): a function that leaves
  r12, r13 or r14 (one per module; the saved PR, frame pointer and extra push avoid it), GBR, VBR, MACH or MACL changed for good; a function that
  reads an input no signature passes (r1-r3 left by the caller, or the entry value of an unsaved r8-r14, GBR, VBR, MACH, MACL copied to a scratch register that
  later items store or return); a caller that copies the register a callee changed into a scratch register right after the call. r15
  is not moved by the grammar (a callee that leaves it changed takes its callers' saved PR with it, and the harness cannot say what they then mean): the extrapop is pinned by
  `TestCallsAndTraps` only.
- Check 8 widened (`input-dropped:<reg>`): r0-r3, r8-r14, GBR, VBR, MACH and MACL read before written and whose value changes what the function does (hardware,
  marked memory, memory through the parameter pointers, and r0 whenever the function writes it on every path, which is what a dropped return is) make a lent
  prototype wrong. SR is not asked (every compare reads it under a mask). Check 10 widened: GBR, VBR, MACH, MACL, which now start at values of their
  own. New check 12 (`kept-read`), the mirror of 11: a function whose lent callee changes r8-r14, GBR or VBR never prints that register's entry value
  (`unaff_r13`, `in_gbr`) unless it reads it on purpose. `SE_GENERATED_MUTATE=kept` adds such a line to every result.
- A bug in the generator's own instrument, found by the widened check: `nop` is `r0 = r0` in the spec, so `RegUse` counted every function that executes a nop as
  having read and written r0 on every path. Both checks that ask about r0 (`return-invented` among them) were blind to it; the self copy is ignored now.
  No new finding came out of that in 3,300 seeds.
- `TestGameLending` (SE_GAME_LEND) prints, per image, why each signature is not lent (the reason with the callee's address taken out), and
  the lent functions that change r8-r14, GBR or VBR with their callers.

### Numbers

Generator, grammar 2, 3,000 seeds (`SE_GENERATED_GRAMMAR=2 SE_GENERATED_SEEDS=3000`): 0 failing seeds, 0 findings. The grammar changed, so
there is no before; the new checks' reach: 5,809 lent prototypes (of 5,941 functions), 425 registers outside r4-r7 read for their value and checked by perturbing them
(3,752 extra runs; none changes what a lent function does: the ones that would are withheld), 292 functions checked by check 12 (a lent callee changes a
register they might read), 10,106 by check 11. The default run (300 seeds, both grammars): 0 findings. A new window (`SE_GENERATED_FIRST=200001 SE_GENERATED_SEEDS=20000`, the review's): 1 failing seed of 20,000, 219488 (A2-11, the engine address wrap, the other fixer's), 1 finding -- the review's own result.
A first form of check 8 counted a push (`mov.l Rn,@-r15`) as a read of Rn: 8,133 "candidates", and two false findings in the window below (seeds 202338 and 216993), where a function
reads an uninitialised local that is the slot an earlier callee pushed a perturbed r3 or r14 to -- a dependence on the stack's garbage, no register to pass. The push is no read now
(it saves the register, its value is not an input of the code that saves it); the two seeds pass.

Corpus, lending run (`SE_GAME_LEND=1`, callees first, each proved signature lent to the next as the panel does), master `f5268df` against this branch:

| | 007 master | 007 now | 008 master | 008 now |
|---|---|---|---|---|
| functions | 1,733 | 1,733 | 2,998 | 2,998 |
| signatures lent | 680 | 612 | 1,083 | 1,008 |
| parameters in them | 845 | 770 | 1,302 | 1,223 |
| withheld "uses a register after calling" | 39 | 30 | 77 | 67 |
| lent, lost (all withheld now) | | 68 | | 75 |
| lent, gained | | 0 | | 0 |
| function bodies whose text differs | | 76 | | 97 |

Why the 68 and 75 lost lent status: 007: 26 and 10 by the cascade ("it calls X, whose signature is not known") and "reads sr on entry" (the T flag, 10); the
rest are hand-written conventions: GBR 6, r0 6, r1 7, r8 4, r2 3, r10 3, MACH 1, r14 1, and one r15 that differs between returns. 008: the same kinds
(53 by the cascade, 11 on sr). The registers are the ones the ledger row names; nothing else is lost. (The first form of rule (b) lost 23 more per image that save r8 and
call: with the `State::called` condition reverted, image 007 lends 546, not 612.) Of the lent functions, 156 (007) and 299 (008) change r8-r14, GBR or VBR -- 154 and 295
of them also move r15, the entries the prologue heuristic finds one instruction after the real one (`mov.l r14,@-r15` before `sts.l pr,@-r15`) --
and 2 and 6 functions are lent one: the 2 lent ones do not read the register after the call; the 4 that do print it as `extraout_` and are withheld. The ledger's
corpus fixtures: `060647EC` (reads r8, advances it) is withheld ("it reads r8 on entry"), so is `0606538E` that called it, and `06064228` (sets GBR) is lent with GBR killed; its callers
`060641E0` and `06063F2E` keep their text (neither reads GBR after the call; `06063F2E` is withheld, "its return is computed from r14", a convention of its own).

Standalone corpus (`TestGameCode`, nothing lent), both images: every ledger count 0, the dropped accesses 0, 16 and 20 reads missing and said (unchanged). 3 of
839 and 11 of 1,554 function texts differ from master: all of them a local the frame-address rule aliases (`xStack_f` becomes `axStack_f [11]`, `cStack_12`
becomes `acStack_12 [14]`), where r2 holds a frame address at a call with no lent signature.

Time (`TestGameCode`, image 008, the machine loaded by other agents, load 6 to 14): wall 18.9 to 21.1 s for the first two quiet pairs, user CPU of the whole suite
33.7-35.8 s on master and 37.7-39.6 s now, about +11%: the probe is one more `ReturnCheck` over every function with a call, in the printing pass only (the first form ran it in
both passes and cost +25%). No function's time moved in the worst case (3.0 s at `0604F422` on both).

### Mutation

Each reverted alone (the source restored after), `Sh2DecompilerTests` rebuilt and run, and the generator (300 seeds, grammar 2) where the host change is one it
sees. The failing checks are the ones the table's reverse names.

| Reverted | `Sh2DecompilerTests` | generator |
|---|---|---|
| `LendEffects` kills nothing (A3-3) | fails 23 checks: r8, r9, r14 (with the oracle: the run returns 7), GBR, VBR, MACH, MACL | 3 seeds, `kept-read` r12 and r13 |
| the r15 delta not lent as the extrapop | fails 3: the oracle on fixture c (`xStack_8` missing, `return param_1;`) | not reached (the grammar never moves r15) |
| `ReturnCheck::Apply` leaves r15 fuzzy after a delta call | fails 1: the caller of fixture c is not lent | not reached |
| the varying-delta withhold removed | fails 2: `r15 moved by an argument` | not reached |
| the engine-input scan (a) removed | fails 21: r0 (`add #1,r0`), r2, r3, r8, r14, GBR, T branched on, SR masked, MACL... | 26 seeds, 50 findings, `input-dropped` for r1-r3, r8, r10, r11, r13, r14, GBR, VBR, MACH, MACL |
| the dropped-return rule (b) removed | fails 16: MACL, MACH, r2, r3, r8, the bare `stc sr,r0`, `r1 loaded` ... | 0 (the grammar's returns from an input are lent by neither or caught by (a)) |
| (b) not limited to a void return | fails 2: the compiler's SR getter is withheld | |
| (b) not limited to a path with no call | 0 in the suite; image 007 lends 546 instead of 612 (58 more withheld "computed from r8/r14/r2...") | |
| the SR whole-copy exception removed | fails 2: the compiler's SR getter | |
| the SR exception applied to every SR read | fails 3: `T branched on`, `SR masked`, `T` | |
| the frame-address declaration removed (patch 0030's caller) | fails 4: `structure return through r2`, lent or not | |
| the frame-address rule applied to lent callees too | fails 1: the control that ignores r2 reads `return axStack_`, not `return 5;` | |
| the rule read r3, not r2 | fails 4: the same | |
| patch 0030 ignores a declared alias when nothing reads r15 any more (`if (spacebase == 0) return`) | fails 4: the same (the copy of r15 is dead by then) | |
| `PseudoEval` reads no array element | fails 4: the corrected text cannot be evaluated | |
| GBR always "preserved" (check 10) | fails 9 (GBR fixtures) | 17 seeds, `preserved-wrong:gbr` (123 findings) |
| MACL always "preserved" (check 10) | fails 4 (MACL fixtures) | 14 seeds, `preserved-wrong:macl` (88 findings) |

`SE_GENERATED_MUTATE` (damage to a result): `kept` 22 of 300 seeds report it, `lent` 86, `preserved` 274, `honour` 134. Not mutated: the A3-6 pin (a fixture of a
limit, which fails if the limit is ever removed: the run returns 7 and the text says `param_1`); the fifth changed fixture (below) against patch 0020, whose
revert did not apply to the file with its later notices.

### Matrix

- Whole `ctest` (RelWithDebInfo, gcc, `-DSE_ENABLE_DECOMPILER=ON`): 96 of 96 pass. The first run found one failure: `Sh2DecodeTests`' A2-5 fixture (patch 0020's
  recycled parse tree) is a function whose slot reads an r2 and whose branches test a T nothing set -- inputs no signature passes. It now sets T and reads a
  parameter (`tst r4,r4`), the same shape. (A fifth existing fixture changed; its assertion is the same.)
- Game corpus, both images, ledger counts 0 and 16/20 missing and said, as above.
- gcc Debug with `-fsanitize=address,undefined`, `detect_leaks=1`: `Sh2DecompilerTests`, `Sh2DecompilerEngineTests`, `Sh2DecompilerFixtureTests`,
  `DecompilerPanelTests`, `DecompilerRunnerTests` and 60 seeds of `Sh2GeneratedTests` exit 0 with only the allow-listed reports (`address.hh:605`,
  `slghpatexpress.cc:979`). `DecompilerRunnerTests` failed once, under a load of 12 from other builds, on its timing-based "following request after the budget" and passed
  on the rerun; `Sh2DecodeTests` and `SlaFreshness` were not run under the sanitizers.
- `vendor.py` on the pinned Ghidra (`918d44e`, the sparse checkout in the scratchpad) with patch 0030 applied reproduces the committed tree, `sh-2.sla` aside (the
  patch touches no spec).
- The timing comparison, above.

### Limits, and what is not done

- r15 is not moved by the generator's grammar; the extrapop and the varying-delta withhold are pinned by `TestCallsAndTraps` alone.
- The frame-address rule is r2 only and exact only. A callee that reads another register the caller filled with a frame address, a hidden input
  in r0, r1, r3 or r8-r14 of a function not decompiled first, is read as before; for a function that was decompiled first it is withheld (A3-5).
- A function that calls and whose return was dropped because it came from a non-parameter register is found only through the input the engine
  kept, if it kept one.
- Carrying an input in the lent prototype (instead of withholding) is not done; see the decision above.
- The cache key: `spDelta` is in `ShapeOf` and in the runner's request digest. No other key changed.
- Patch 0030 touches `varmap.cc` and `varmap.hh` only. The brief puts the other fixer's work in the engine's address code and in `jumptable.cc`, so the patches should apply in either order; not checked, 0029 is not on this branch.

## Areas 1 and 8, round 3 review

Branch `review/1-8-round3-review`, from master `5b403ba`. This round reviewed area 1 (the round 2 fix `a4153ad`, the oracle's blind
spots: `App`, registers, the NeedsRefresh text, the speed question) and area 8 (the stale reason and its tooltip). No production
source and no test source was changed. `kOpen` stays empty: none of the findings can be written as a model script (they need registers,
the tooltip text or a refused entry), so each is a directed `DecompilerPanelTests` sequence, given below. Four ledger rows were added:
**A1-6, A8-2, A8-3 and A8-4, all P3**. No P1 or P2 was found. Round 2 found only P3s too, so area 1 has had two consecutive rounds
with no P1 or P2. Under the program's rule, area 1 is done once A1-6 is fixed or accepted.

### Findings

- **A1-6 (P3).** Auto-refresh re-runs a function the runner found too large every debounce period while the followed PC stays in
  it. A "too large" answer has no body, so every PC counts as having left it. Every frame of a running emulator is a new key, so the
  runner's memo never answers. Measured at a 0.3 s budget over 6 s: 12 runs, worker busy in 93% of UI frames, worst UI frame 3.6 ms.
  The UI thread is not blocked and nothing else is starved (the panel is the runner's only requester), but a core stays busy for as
  long as the game runs there. Stepping is the documented per-frame cost: 1.0 s per step at a 0.3 s budget (the budget, then about
  0.7 s until the engine reaches a check-point).
- **A8-2 (P3).** The NeedsRefresh tooltip says "the entry box names no function ... Correct the entry and press Enter" for any
  resolve failure. That includes Follow PC with the PC in the BIOS or with no registers, and "Decompile" on an address with no
  function. In each case the box holds a valid entry and was never touched.
- **A8-3 (P3).** Refresh of output that is current shows `[stale]` "from an earlier state" while the replacement runs. It also dims
  the output and drops the halt hint, though nothing changed.
- **A8-4 (P3).** A refused entry ("zz") leaves the target as it was. The next capture re-resolves the old target, and the badge and the
  error disappear with the box still saying `zz`.

### The sequences

Each is a function on the `Fixture` of `DecompilerPanelTests.cpp` (lines 1-360 at `5b403ba`, unchanged). Built against the panel
tests' objects with `Probe.o` in place of `DecompilerPanelTests.cpp.o`, they print the panel state and fail on the `CHECK`s below,
which state the expected behaviour.

```cpp
// A8-2
void ProbeFollowPcTooltip()
{
    Fixture f;
    f.kind = DecompViewKind::LivePaused;
    CHECK(f.WaitShown("+ 1"));
    f.regs[0].pc = 0x00001FF0u;   // in the BIOS, no image mapped: no function found
    ++f.frame;
    f.Idle(3);
    CHECK(f.panel.ShownStale());
    CHECK(!Has(f.panel.StaleTooltip(), "entry box names no function"));   // fails
}
// ...the same with f.haveRegs[0] = false instead of the PC, and with
// f.panel.ShowFunctionContaining(0, 0x00001FF0u) on the default Static view: the same tooltip.

// A8-3
void ProbeRefreshMarksCurrentStale()
{
    Fixture f;
    f.kind = DecompViewKind::LivePaused;
    f.PutWord(dts::kFixture1Jsr - 2, 0x6042);
    ++f.backend.serial;
    f.regs[0].pc = dts::kFixture1Jsr;
    f.regs[0].r[0] = dts::kFixture1Callee;
    CHECK(f.WaitShown("(*(code *)"));
    f.Idle(2);
    const SnapshotKey before = f.Shown().key;
    CHECK(!f.panel.ShownStale() && !f.panel.HintText().empty());
    f.gate.Close();
    f.ClickItem("refresh");
    CHECK(f.gate.WaitHeld());
    f.Idle(2);
    CHECK(f.Held() == before);                                  // nothing moved
    CHECK(!f.panel.ShownStale());                               // fails: stale, reason Pending
    CHECK(!Has(f.panel.StaleTooltip(), "earlier state"));       // fails
    CHECK(!f.panel.HintText().empty());                         // fails: the hint is gone
    f.gate.Open();
    CHECK(f.WaitShown("(*(code *)"));
}

// A8-4
void ProbeRefusedEntryForgotten()
{
    Fixture f;
    f.panel.ShowFunctionContaining(0, dts::kFixture1Caller);
    CHECK(f.WaitShown("+ 1"));
    f.TypeInto("entry", "zz");
    f.Idle(2);
    CHECK(f.panel.ShownStale());                  // NeedsRefresh, "Type the function's entry as a hex address..."
    f.backend.hw[0x0600F000u - kHw] = 0x11;       // a byte no function reads
    ++f.backend.serial;
    f.Idle();
    CHECK(f.WaitShown("+ 1"));
    f.Idle(2);
    CHECK(f.panel.ShownStale() || !f.panel.EntryError().empty());   // fails: neither, box still 'zz'
}

// A1-6
void ProbeAutoRefreshTooLarge()
{
    RunnerOptions o;
    o.budgetSeconds = 0.3;
    Fixture f(o);
    std::vector<uint8_t> image(f.backend.hw.begin(), f.backend.hw.end());
    dts::PlaceDiamonds(image, 1000);
    std::copy(image.begin(), image.end(), f.backend.hw.begin());
    f.PutWord(dts::kDiamondsBase - 4, 0x000B);
    f.PutWord(dts::kDiamondsBase - 2, 0x0009);
    ++f.backend.serial;
    f.kind = DecompViewKind::LiveRunning;
    f.trackSerial = false;
    f.regs[0].pc = dts::kDiamondsBase + 12;
    CHECK(f.PumpUntil([&] { return f.panel.Shown() && !f.panel.Pending(); }));
    CHECK(f.Shown().status == DecompileStatus::TooLarge);
    f.panel.SetAutoRefresh(true);
    const uint64_t runs0 = f.runner.RunsStarted();
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; dts::MsSince(t0) < 6000.0; ++i)
    {
        f.regs[0].pc = dts::kDiamondsBase + 12u * static_cast<uint32_t>((i * 7) % 900);   // inside the function
        ++f.frame;
        ++f.fixedSerial;
        f.Idle();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    CHECK(f.runner.RunsStarted() - runs0 <= 1);   // fails: 12
}
```

Results (RelWithDebInfo, a 4-core container shared with other work):

| Probe | Current |
|---|---|
| Follow PC into the BIOS | stale 1, reason NeedsRefresh, entry box `06004000`, error "No function found containing 0x00001FF0: 0x00001FF0 is not in captured memory", tooltip "...the entry box names no function to replace it (see the message below). Correct the entry and press Enter." |
| Follow PC, no registers | the same tooltip; error "This source has no master SH-2 registers to follow." |
| Decompile (Containing) in the BIOS | the same tooltip; the box holds `06004000` |
| Refresh running, nothing changed | held key = shown key; stale 1, reason Pending, "This output is from an earlier state; its replacement is shown when it is ready."; hint `''` (before: "r0 = 0x06005000 at the halt"). After the answer: not stale, hint back |
| Refused entry, then an unrelated edit | before: stale 1, NeedsRefresh, error set; after: stale 0, error `''`, box `zz`, `0x06004000` shown as current |
| Auto-refresh in a too-large function, 6 s | 350 UI frames: 12 engine runs, 12 submissions, 12 captures, worker busy in 325 frames (93%), worst UI frame 3.6 ms, `TooLarge` shown throughout |
| Stepping in a too-large function, 5 steps | 5 engine runs, 1,003 ms per step to the verdict (budget 300 ms) |

### What held

- **The round 2 fix.** The default run passes: 18 seeds and the 14 pinned sequences (808 operations, 469 results checked at install, 832
  idle checks, 262 held-engine stale checks, 94 walks). `SE_MODEL_OPEN=1` runs nothing. `DecompilerPanelTests` passes. Seeds 1501 to 1800, which no earlier
  round ran, were run one at a time (`SE_MODEL_SEED=<k>`), and none of the 300 fails.
- **Directed model scripts** (all pass). An edit landing between a request's submit and its answer, then reverted:
  `select D; hold; edit A 0 1; finish; edit A 0 0; wait`. Two answers for one question: `select D; hold; refresh; refresh; wait`, and a
  retry after a slow first, `select D; starve; refresh; wait; select C; select D`. A callee changed under a held caller, two levels
  down: `select A; select C; select D; hold; edit A 1 2; select C; finish; select D; wait` and the same through mirrors
  (`selectm A; selectm C; select D; edit A 1 2; selectm A; select D`). A callee learned bare and lent to its caller, then the
  callee's callee: `edit C 0 1|2|3; select C; select D; ...` (world D,C,B,P,Q). A BIOS image reloaded with the same bytes (a new
  object, the same content hash): `bios 1; select X; select E; bios 1; select E; bios 2; select X; bios 1; select E` serves the
  cache and asks nothing. Hidden while the bytes change and change back: `select A; hide; edit A 0 1; edit A 0 0; show; wait`. Stop,
  then a change and its reversal: `select C; hold; stop; edit A 0 1; finish; edit A 0 0; wait; select C`.
- **Follow PC across a session reset, hide/show, Stop and a scrub** (a directed panel sequence, passes). The PC is in the caller,
  then the session resets with the PC in the callee, then the panel is hidden while the PC goes back to the caller and shown again.
  Refresh is stopped, then the PC steps into the callee. A recorded frame has the PC in the caller, then the view returns to live with
  it in the callee. Each time the shown function is the one containing the PC, and it is not stale once answered.
- **`App`, read path by path.** Every input the key covers is recomputed by the panel each frame from the view, and `App`
  builds that view's identity the same way on every path:
  - The live identity is `ContextBackend::SourceId()`, the context pointer folded with a generation that `NoteSourceChanged`
    moves, together with `se_derive_serial`. The serial moves in `RebuildDerived`, which `BeginFrame` and every `WriteVram` and
    VDP-register write call. SH-2 registers are never written in place.
  - Every context replacement goes through `CloseData` (`OpenFullDump`, `OpenSavestate`, `AttachLiveSource`,
    `OpenSavestateBuffer`), and so through `ResetSessionDebugState`. `AdoptNewEmulatorInstance` calls it too.
  - The scrub identity (`mScrubTimeline`, the view serial, the shown frame) moves on Play From Here, a ring clear, a slot load, an
    emulator slot load and a session reset. `FrameRecorder::Capture` never reuses a number within a timeline. A Select that fails
    falls back to live (`mbScrubbing = false`), so a half-decoded frame is never keyed.
  - Names (`Rename`, `Load`, `Import`) all move `FunctionNames::Generation`. Function starts and the volatile list are hashed by the
    panel itself. The BIOS identity is the image's content hash, so a reload with the same bytes is no change, by design and
    correctly.
  - No path was found that changes an engine input without moving the identity.
- **Mirrors.** The entry as asked, lent prototypes by the address as run, names canonicalised by the engine but keyed raw by the
  panel (conservative), function starts canonicalised at `SetEntry`, and volatile ranges expanded by `VolatileAliases` are each
  consistent between the panel's question, the runner's `RequestKey` and the engine.
- **The runner.** Supersession, Stop and the budget share one cancel clock (the first reason stands), so a budget run superseded
  after its deadline still records "too large" for its own key only. A memo answer carries the asking request's generation. The
  queue coalesces per slot.

### Not probed, and why

- **App with a live emulator.** It was read, not run. One question has no reproducer. `App` calls `se_begin_frame` (which always moves
  the derive serial) on a paused emulator every UI frame while a state load is unconfirmed (`restoreWaiting`), while a step settles,
  or while the session is behind. A `LivePaused` panel follows every such move at once: it captures and asks again, superseding the
  run before. A function that takes longer than a frame would then never be shown while a state load stays unconfirmed (the wait
  does not end on the 10 s timeout). That needs a live server that never confirms a load.
- **`DecompileStatus::Disabled`.** Nothing produces it (the runner disables itself and returns generation 0 instead). The panel's
  branch for it is dead code, unchanged from round 2.
- **"Unimplemented".** Still unreachable on this spec.
- **Cancel acknowledgement on real large functions.** On `PlaceDiamonds(1000)` the engine acknowledged a budget cancel in about 0.7 s,
  well under the 5 s limit that disables the session. A function near `maxInstructions` was not measured.

## Area 5, round 5 review

Reviewer, branch `review/5-round5-review`, at `5b403ba` (master). Findings only; no source or test changed. Every fixture is in
`Docs/Decompiler/REVIEW_LEDGER.md` (A5-28 to A5-32) with its words, current and expected output and the Mednafen lines (in the
ledger's code quotes `or` stands for the C bar). Reproduced on a RelWithDebInfo gcc build with a scratch harness linked against the
decompiler's objects (it prints the notes and cautions, which `SE_GENERATED_WORDS` does not), with `SaturnExplorerSh2GeneratedTests`
(`SE_GENERATED_WORDS`) for the generator's verdict, and with the corpus (`SE_GAME_HWRAM`, `SE_GAME_NOTES=1`). Mednafen read from the
review path (`mednafen/ss`): `sh7095.inc` (the six on-chip handlers, `MemRead`, `MemWrite`, `SH7095_BSC_Bus*`), `scu.inc`
(`SCU_RegRW_DB_u8/u16/u32_W0/W1`, `SCU_FromSH2_BusRW_DB_u8_W0`, `ABusRW_DB_u8_W1_SH0`, `ABus_Read`), `ss.c` (`BusRW_DB_CS0_*`),
`vdp1.c` (`WriteReg`, `ReadReg`, `VDP1_Write8/16_DB`, `VDP1_Read16_DB`), `vdp2.c` (`RegsWrite`, `RegsRead`, `RW_R16`, `RW_W8`,
`RW_W16`), `smpc.c` (`SMPC_Write`, `SMPC_Read`), `cdb.c` (`CDB_Read`, `CDB_Write_DBM`), `scsp.inc` (`SS_SCSP_RW_u16_W0/W1`).

**Area 5 is not clean: one P2, four P3.** The round 4 check is faithful to the handlers it parses; what it cannot see is where the
rows are: a per-label mask inside a group, misaligned on-chip addresses, values after an address error, and a register reached
through a pointer.

**Not held.**

- **A5-28 (P2).** A register read through a pointer merged from register literals (`puVar2 = &VDP2_TVSTAT; ... *puVar2` in a
  poll) gets no cache caution through the cached partition and no read note in either partition, though the line names the
  registers. The generator's rule 3b excuses exactly this shape ("a plain load names no register"); the excuse holds for an
  anonymous pointer, not for one whose values are register symbols.
- **A5-29 (P3).** Misaligned on-chip accesses. "Decoded as given" is true below 0xFFFFFF00 only; the upper half aligns with
  `A &= 0xFE` / `0xFC` itself, so a word at 0xFFFFFF05 divides (the caution says it does nothing), a word at 0xFFFFFF8D arms CHCR0
  (the caution says it reads nothing). Below 0xFFFFFF00 the cells are the aligned table's: a word at 0xFFFFFE11 reads FTCSR and the
  cautions say TIER and FRCH.
- **A5-30 (P3).** A byte write to the reserved low byte of IPRB or VCRD does nothing (`& mask & 0xFF00`, `& 0x7F00`); printed as a
  plain part write. The handler check passes it: one flag per group of labels.
- **A5-31 (P3).** The address-error cautions do not say what Mednafen does next: a byte write to a 32-bit register stores
  `V or (V << 8)` whole (and divides at DVDNT), a byte read returns bits 15-8 whatever the byte; a long below 0xFFFFFF00 reads 0, and
  writes only the register at its own address (TIER bits 7-0, IPRB and the vector registers bits 15-8).
- **A5-32 (P3).** VDP1_ENDR (any write ends drawing), SCU_DnEN with bits 8 and 0 (starts that level's DMA), SCU_AIACK and SCU_DSTP:
  not said, where PTMR and SF are. Both images write `VDP1_ENDR = 0;` and `(&SCU_D0EN)[param_1 * 8] = 0x101;` with no note.

**The parser, read against the C** (`derive_handler_cases.py`; 25 groups and lines checked by hand, every line of the cases file
compared with its handler): the on-chip lines (all twelve; `pex`, `mask`, the hi/lo split, the label groups, `E` on FTCSR, FRCH,
FICRH, WTCSR, RSTCSR, CHCR, DMAOR, RTCSR, `D` on DVDNT/DVDNTL, `C` on 0x92, the watchdog's `F`), `scu.rd8`/`wr8` (and the three
widths' write bodies, which differ only in the mask and the PPD decoder), `vdp1.rd`/`wr`, `vdp2.rd`/`wr` (`RegsWrite` masks
`A &= 0x1FE` itself, `RegsRead` switches on `A & 0x1FE`), `smpc.rd`/`wr`, `cd.rd`/`wr`, `scsp.rd` (the common block only; slots,
DSP and the MIDI lane are outside it). Held, with two weaknesses that misstate nothing today: (1) a case body that falls through into
the next label is classified on its own text only; the one instance is SMPC case 0x00 (`if(...) nt = ...;` then `IREG[A] = V`), which
is `M` either way; (2) `ret = 0xFFFF >> shift` (on-chip 0x6A-0x6F and 0xE6-0xFF, the filler of unnamed addresses) is classified `M`,
not `-`, because the filler test wants a bare constant; no register is named there, so no cell is judged by it, but a register added
there would pass. The `F` flag is coarse (it also marks SCSP bookkeeping, `MPROG_Dirty = true`, and value-dependent stores such as
`Timer0_Counter = 0` under `if(!Timer_Enable)`), which is harmless while only `smpc.wr` reads it.

**The check's lists of rules.** Every rule an on-chip cell can have is in one of the two lists of `CheckOnChip` (reaches / claims
nothing) or is `AddressError`; no on-chip cell falls to `default`. The holes are below the cell: one flag per group (A5-30), aligned
addresses only (A5-29), no value (A5-31), and `F` read for the SMPC only (A5-32).

**Held**, one line each:

1. The cache caution fires on partition 0 only (0x0xxxxxxx and 0x1xxxxxxx): not on 0x2/0x3 (`MemRead` case 1), not on partition 7,
   not on writes (case 0 falls into case 1's `ExtBusWrite`); the associative purge (0x4/0xA) is its own note and names no register.
2. Misaligned accesses off chip align down (`A &= ~(sizeof(T) - 1)` before the partition switch; the cache, `ExtBusRead` and the purge
   all use the aligned `A`): A5-25's fixtures and a misaligned long in backup RAM (made at 0x20180000, its lane caution) are right.
3. Backup RAM (A5-26): a byte at an odd address is kept and re-read (volatile), a byte at the even address, a word and a long have the
   lane cautions `ss.c` `BusRW_DB_CS0_*` and `SH7095_BSC_Bus*` (CS0: `shift = (A & 1) << 3` for a word, the long split high half first)
   bear out; a cached read gets the cache caution.
4. The SMPC lanes, MINIT/SINIT (`c = ((A >> 23) & 1) ^ 1`), the CD window (`(A & 0x7FFF) < 0x1000`, `(A & 0x3F) >> 2`: 0x25818008 is
   HIRQ, 0x25898000 the FIFO read once), VDP1 (no repeat: `which` past 6 is `default`), VDP2 (`A & 0x1FE` within 0x05F80000-0x05FBFFFF),
   SCU (`A & 0xFC`, 0x05FE0000-0x05FEFFFF): as `HardwareAlias` has them.
5. Polled ordinary memory: a poll of unmarked work RAM (0x06001000, 0x26001000) stays a loop that re-reads (`do {} while (iRam06001000
   == 0);`), and the corpus' slave handshake (060241CC, `while (iRam26003304 != 0x534c4156)`) likewise; no volatility is inferred, as the
   program says (after the oracle).
6. The game corpus, read by function against the disassembly and the handler (30 functions): the SMPC INTBACK sequence (0600541E: SF
   poll, SF = 1, IREG0-2, COMREG, OREG12-15), the SMPC port setup (06019A56, 06058662), the sound setup (06008BB0: SF, COMREG,
   SCSP_MVOL = 0x200), the FRT/INTC setup (060241CC, 06063E6C), the FTCSR waits and the MINIT signal (06063ED4, 06063EF8), the cache
   init (0606554C: `SH2_CCR = 0x11` twice, purge notes true), the BCR1 master/slave test (06025D3C, 060278C4, 060659DC, 06067564: a
   word read at 0xFFFFFFE2, the low half, case 0xE2), the SCU timers (0600B736: T0C, T1MD = 0x101), the DSP loader (06033D5A,
   06074DEE: PPAF, PPD), the CD reads (06021414, 06021634, 060608F0: HIRQ; 06021540, 06060A1C: the FIFO at 0x25898000, popped once
   per word, said), the TVSTAT polls (060569B4, 06056D30, 06071912), VDP2 BGON/WCTLA (0600BCF6), VDP1 TVMR (06005906), and the two
   A5-32 instances (060056E6, 0600572A, 0601B7B4, 0605A964). Every access printed once at its width and address, the notes true. The
   `stc sr`/`ldc sr` masks around several of them are dropped (A2-13, area 2's, open). 0601B3B0 and 0605A560 print the bound
   0x25FE0000 of a pointer range check as `&SCU_D0R` (a constant that is a register's address used as a number; cosmetic, no row).

**For the coordinator.**

- A5-28 is the P2 because the oracle written for A5-19 excuses it by name; the fix is either notes that follow a pointer whose values
  are known register symbols, or a caution that the read may be of those registers, and rule 3b without the exemption when the output
  shows `&REG`.
- Questions without a fixture: the BIOS ROM (0x00000000-0x000FFFFF through either partition) drops writes (`BusRW_DB_CS0_u*_W1`: no
  store), and the decompiler folds a store and load there as ordinary memory; is that area 5 (a hardware area) or area 2? The cache
  arrays (0x6xxxxxxx address array, 0x8/0xCxxxxxxx data array) are ordinary memory to the decompiler; a game that uses the cache as
  RAM (two-way mode) is right, a write to the address array is not memory. The SCU's byte and word lanes are judged by register, not by
  lane (`CheckScu` uses the register's label): a byte write to the upper bytes of D0EN or T0C reaches no bit (`& 0x3FF`, `tmp >> 8`)
  and prints as a part write; that is the value blind spot, not a row. The SCSP MIDI output (`case 0x03: if(!shift)`) takes a byte
  write at the odd address only.
- Could not probe: the cache with CCR.OD/ID or two-way mode (Mednafen's replacement tables decide whether a miss fills the line), the
  slave CPU's view (the decompiler has one map for both), and VDP2 colour RAM (A5-16 part 4, accepted).

**Runs.**

| | |
|---|---|
| `SaturnExplorerHardwareWidthTableTests` | passes: 337 registers, 3,038 cells; handler check 526 on-chip cells, 490 SCU cells, 228 block registers, 1,792 unnamed-address probes, 2,597 bus probes |
| `derive_handler_cases.py --check` | the cases file is current against the review path's sources |
| Generator replays of A5-28 to A5-32 | 0 findings each (the oracle accepts all of them) |
| Game corpus, `hwram007.bin` | 839/839; every count 0; 16 missing and said; records 14,843 folded, 0 dead, 16 unreachable, 27 switch lookups; notes 12, cautions 16 (as round 4) |
| Game corpus, `hwram008.bin` | 1,554/1,554; every count 0; 20 missing and said; records 38,701 folded, 0 dead, 20 unreachable, 155 switch lookups; notes 27, cautions 20 (as round 4) |

## Areas 3 and 4, round 4 review

Reviewer, branch `review/3-4-round4-review`, on master `ec7751d` (patches 0001 to 0031). Findings only, no fixes. New ledger rows:
A3-7 (P2), A3-8 (P3), A3-9 (P2), A3-10 (P3), A3-11 (P3), A3-12 (P3), A4-8 (P3), A4-9 (P3). The instrument was a scratch driver, not
committed: `Sh2DecompilerTests.cpp` with a reader of fixtures from a file (callees decompiled first and lent, then the caller, the
emulator's run and `PseudoAgreesWithRun`), a mode that hands a text to `MayReturn` alone, and a callees-first lending pass over image 008
that lists lent `void` functions that call and end by moving a register no signature passes to r0. Rows A2-12 to A2-15 (open, another
agent's) were not re-reported.

### Area 3: what was probed and held

- **`LendEffects`.** A callee that pops its caller's pushed word (`rts ; add #4,r15`) is lent with extrapop +4; the caller that pushed it
  and then pops only its PR is proved to keep r15 (preserved 0x003FFFFF) and is lent `void f(void)`, which the emulator agrees with. The
  round-3 fixtures (r8, r9, r14, GBR, VBR, MACH, MACL, r15 lowered, r15 moved by an argument) pass in the suite.
- **The frame-address rule.** A frame address stored to a global that the callee writes through is aliased by the engine itself
  (`pxRam0600a000 = axStack_c; ... return axStack_c[0];`, the oracle agrees). The exact-r2 case (A3-4's fixture) holds.
- **Rule (a) and rule (b) in a leaf.** The round-3 leaf fixtures pass in the suite. A callee that stores SR whole through a parameter
  (`stc sr,r0 ; rts ; mov.l r0,@r4`) is withheld too: r0 is a copy of SR at the return, so (b) sees it.
- **The corpus.** `SE_GAME_LEND=1` with both images: the suite passes; image 007 lends 611 signatures (767 parameters), image 008 1,007
  (1,220); every `TestGameCode` count 0, 16 and 20 missing and said, 27 and 155 switch lookups, as round 3. The scan of image 008's
  346 lent `void` functions that call found no `rts` whose slot or previous instruction moves r1-r3, r8-r14, MACH, MACL or GBR to r0:
  A3-7 has no instance there.

### Area 3: what failed

- **A3-7 (P2): a dropped return after a call.** `sts.l pr,@-r15 ; bsr g ; nop ; lds.l @r15+,pr ; rts ; mov r8,r0`, with g lent: the
  function is lent `void f(void)` and returns its caller's r8. Rule (b) asks only paths with no call, and a call on any path joining the
  return counts (the variant with the call under `if (param_1 != 0)`, returning MACL). The generator's checks 8 and 9 reject each;
  `PseudoEval` accepts each (A3-11).
- **A3-9 (P2): r2 holds a frame address with an index added.** The struct-return address of an array element: `mov r15,r2 ; add r0,r2`.
  `NoteFrameEscape` takes only an exact address; the text says `return 10;` where the code returns 12. Withheld, so only the text is
  wrong, as A3-4's was.
- **A3-8 (P3): the SR whole-copy exception, the answer to "can a getter hide a real input".** Yes: a caller that sets T, calls the lent
  getter and returns bit 0 of the result is lent with no parameters, though its return depends on r6 and r7. P3 because compiled code
  does not read T out of a returned SR; a hand-written caller can.
- **A3-10 (P3): r3 instead of r2.** Round 3's deliberate choice. Raised to be accepted with a reason in the ledger, or narrowed.

### Area 4: what was probed and held

Each against the emulator for indices 0 to 3, -1 and 0x80000000 (or the bytes 00, 01, 02, 03, FF, 80, 7F):

- **The A4-7 shape with the index loaded by `mov.b @r4,r4`.** The engine finds the byte switch, warns "Sanity check requires truncation
  of jumptable: cases may be missing" and withholds the signature. Index 0xFF goes to case 2 on the hardware and is not shown, but the
  warning covers it. The `cmp/hi` control is a four-way switch, right for every byte, and lent.
- **`braf` from a bounded register with no table** (`shll2 r0 ; braf r0` into `rts ; mov #k,r0` cases). This is GCC's `casesi` (`mova`,
  `add r4,r4`, `mov.w @(r0,r4),r1`, `braf r1` over a table of words relative to the `braf`). It is a two-level table (a byte table of
  sub-indices, then a `jmp` table). Each is a switch that agrees with the emulator, and each is lent.
- **A `cmp/pz` guard with no upper bound.** "Too many branches", treated as a call, withheld.
- **A shared epilogue reached by `bra` from two functions.** Each function's text and body is right.
- **A function that falls into another's entry.** The text is right. The warning is there only when the entry is in `knownFunctions`
  (A4-8).
- **Generator, grammar 2, a new window** (`SE_GENERATED_FIRST=300001 SE_GENERATED_SEEDS=6000`). 11,706 functions, 1,637 tables. There
  were no decompiler findings. 2 seeds broke the generator's own contract (A4-9).

### Oracles

- **`PseudoEval` (A3-11).** It accepts `return;` for any value, so it reads A3-7 as right. It also accepts:
  - `true` and `false`, as unknown names;
  - a reassigned parameter, read as its entry value;
  - casts of any width;
  - `CONCAT`/`SUB`.

  It rejects five texts the engine prints rightly: a parameter reassigned under an `if`, `>>` on a `uint4`, `<` on a `uint4` (from
  `cmp/hs` and `cmp/hi`), and a 64-bit product; and a `char` case label. The unsigned bound of `TestSignedGuardTable`'s controls is
  printed inside a `switch`; the same bound printed as an `if` is rejected.
- **The generator's check 8 (A3-12).** It cannot make two argument registers equal and does not ask SR, so it accepts A3-8.
- **The table check (check 6).** It looks only at cases whose code makes an access the ledger lists (`table.inert` otherwise). It
  excuses every table of a function when any warning in it mentions a jump table ("umptable"), the warning of another table included.
  A case that sends an index to the wrong return value is seen by no generator check; the call oracle is the only one, and it does not
  run on generated code. No wrong output was found through either hole, so this is no row.

### Questions without a fixture

- Self-modifying literal pools (A4-9's seed 301364 is one, made by accident: the decompiler folds the literal the callee overwrote, and
  the code's second call goes where the folded literal does not say). Is a store into the function's own literal pool something the
  output should say?
- `PseudoEval` and narrow parameters: the engine prints `char param_1` when only the low byte is used, and the evaluator feeds the whole
  register. With A3-11's typing this would be settled. It is in that row's expectation.

### Could not probe

- Signed bounds in a callee whose callers bound the index (the generator makes signed tables in the first function only). An
  interrupt between a callee's push and its use. Flow that leaves captured memory and comes back: no captured code can be reached from
  uncaptured memory, so only a cut is possible, and that is warned (`TestTruncatedFlow`).
- Two indices that both escape a signed guard with distinct behaviour. Patch 0029 makes every signed-only guard unresolvable, so only
  index -1 was run.

**Done criteria.** Area 4: this round has only P3 rows (A4-8, A4-9) after round 3's P3 only. That is two consecutive rounds with no P1
or P2, so area 4 is done once A4-8 and A4-9 are fixed or accepted and the overnight generator run on grammar 2 is clean. A4-9 is the
generator's own failure in this window. Area 3: two P2 (A3-7, A3-9), so it needs a fix round and two clean review rounds.

## Area 2, round 3: the printed value against a run

Branch `review/2-round3-fix`, from master `e7cff08` (patches 0001 to 0028; 0029 to 0032 belong to other fixers this round, so this
round's are **0033** and **0034**). Fixes the four rows of "Area 2, round 3 review" (A2-12 to A2-15), answers its three questions,
and turns the reviewer's probe into an oracle that stays: ledger rows A2-12 to A2-15 are `fixed` (this section's commit), and A2-16
to A2-20 are new (below).

### 1. The oracle: the printed C, compiled, against the emulator

`SaturnExplorerSh2PrintedCTests` is `SaturnExplorerSh2GeneratedTests` told by its environment (`SE_GENERATED_PRINTEDC=only`) to run
`TestPrintedC` and nothing else. For each function of a register-only grammar (version 3 below, plus the ledger's fixtures,
`kPrintedCFixtures`) it:

1. decompiles it at `06009000`, and refuses a decompilation that is not Ok;
2. runs it on the p-code emulator (`Machine`, as the other oracles) for 32 inputs: r0 to r7, SR, MACH, MACL; the first 22 walk the
   edge values (0, 1, 2, `FFFFFFFF`, `7FFFFFFF`, `80000000`, ... ) through r4-r7 with T both ways, the rest are random;
3. writes the text into one C file with the engine's types and helpers (`CONCATxy`, `SUBxy`, `ZEXTxy`, `SEXTxy`, `CARRY4`,
   `SCARRY4`, `SBORROW4` ..., generated by loops from the engine's definitions) and every input the text names (`in_rN`, `in_sr`,
   `in_mach`, `in_macl`, `unaff_rN`) given the value the run started with; `Write_SR/GBR/VBR` are functions that record the
   last value written;
4. compiles all of it with the system compiler (`SE_C_COMPILER`, else `cc`, `gcc`, `clang`; `-O0 -fwrapv -fsigned-char`), runs it, and
   compares the return value with the emulator's r0 (r0 and r1 for an 8-byte return), and the control registers the `Write_*`
   calls leave with the registers the run ends with (GBR, VBR, and SR's S and interrupt-mask bits, which only `ldc` changes).

Without a C compiler it prints `SKIPPED: no C compiler found ... The printed-C differential did NOT run.` and exits 77, which
ctest reports as skipped (`SKIP_RETURN_CODE`), never as passed; the CI sanitizer step counts any non-zero exit as a failure, so a
runner without a compiler fails there. The default run is 300 seeds, 17 fixtures and 32 inputs each: about 5 seconds.
`SE_GENERATED_SEEDS`, `SE_GENERATED_FIRST`, `SE_GENERATED_SEED`, `SE_GENERATED_PRINTEDC_PER=n` (functions per seed) and
`SE_GENERATED_PRINTEDC_INPUTS=n` make it longer; the nightly job runs it after the generator. `SE_GENERATED_PRINTEDC_WORDS="6153 E000 000B 0009;..."`
checks hand-written functions and prints each text; a failing function is shrunk (items dropped while the same class fails) and
printed with its replay line; `SE_GENERATED_KEEP=1` keeps the compiled files.

Classes. Failing: `mismatch` (a different value), `input-outside-registers` (a name made of an address, `unaff_10000008`: A2-15's
signature), `undecompiled`, `not-compilable` (the text is not C), `crashed` (the compiled program died; the batch is bisected).
Counted and said, not failing: `narrow-c` (a mismatch on a function with a one- or two-byte variable, or only in the high word
of an 8-byte value written with a widening cast: C's promotion against the engine's width, A2-19), `void-return` (r0 changed,
signature void: A3-5's class, 934 of 10,017 functions; to become failing when A3-5 is fixed), `skipped:call-or-stack` (the text
calls, `braf` to a constant is a tail call, `extraout_`), `skipped:memory` (a dereference), `skipped:unknown-input`.

**The alphabet.** The generator's first two grammars drew their ALU items from eleven ops. `kRegOps` is the table of every
non-memory, non-PC-relative, non-delayed op (with what each reads and writes), and one ALU item in four, from a stream of its own
(the other draws of a seed are unchanged), is one of them; one memory item in ten is a GBR access (`and.b`/`or.b`/`xor.b`/`tst.b
#imm,@(r0,GBR)`, `mov.x @(disp,GBR),r0` and back, GBR pointing into scratch RAM). The register-only grammar (version 3) is
those ops plus `bt`/`bf`/`bt/s`/`bf/s` over one or two instructions, `braf` over one to three, a prologue that copies the arguments
into r0-r3 (75%) and an `rts` whose slot is a `mov Rm,r0`, an op or a nop. `SE_GENERATED_ALPHABET=narrow` restores the eleven.
Coverage, `SE_GENERATED_COVERAGE=1`, seeds 1 to 400, counted from the items' words (never the pool, tables or data):

| | distinct mnemonics | instructions |
|---|---|---|
| before (`SE_GENERATED_ALPHABET=narrow`, grammars 1 and 2) | 34 | 10,820 |
| after, grammars 1 and 2 | 80 | 11,003 |
| the register-only grammar, 400 functions | 67 | 7,400 |
| after, both together | 84 | 18,403 |

Before: `add and bf bf/s bra bsr bt bt/s cmp/eq cmp/hi cmp/pl dt extu.b extu.w jmp jsr lds lds.l mac.l mac.w mov mov.b mov.l mov.w mova or rts
shll2 sts sts.l sub tas.b tst xor`. Newly emitted in grammars 1 and 2: `addc addv subc subv negc div0s div0u div1 dmuls.l dmulu.l mul.l muls.w
mulu.w exts.b exts.w swap.b swap.w xtrct rotl rotr rotcl rotcr shal shll shlr shll8 shlr8 shll16 shlr16 shlr2 movt sett clrt clrmac neg not
cmp/ge cmp/gt cmp/hs cmp/pz cmp/str ldc and.b or.b xor.b tst.b`; the register-only grammar adds `shar stc sts braf` (`stc`/`sts` are fenced in the
first two, below). Still never emitted: **`bsrf`, `trapa`,
`rte`** (A2-20): `trapa` needs a vector table and a handler beside the function, `rte` pops the stack and ends a handler, `bsrf`
needs a callee at a distance known only after layout, which is the call grammar's (another fixer's this round).

What it found, with the fixes still out: the three fixtures of the review's ledger rows at once (A2-12, A2-14, A2-15), A2-12 again in
the first 40 of 3,000 random functions (the fixtures' shape reached through `rotcl`, `sts macl`, `ldc`), and, from the first two
grammars' own checks with the wider alphabet, two classes that are not these rows': A2-16 (`stc sr,r1` after a jump table, seed 358 of
3,000) and A3-5 (`stc gbr,r3 ; mov r3,r0` returned, seed 8168 of 10,000), both fenced by dropping `stc`/`sts` from those grammars.

### 2. A2-12: the 8-byte return narrowed to r1

*Cause.* r0 and r1 are the halves of the join output (`superh.cspec`, patch 0007: r0 high). With r0 = 0 the return is
`CONCAT44(0, r1)`, whose non-zero bits are the low four bytes. `ActionDeadCode::gatherConsumedReturn` takes that for what is
consumed, so dead-code removal and the subvariable rules cut the return down to the low half, **r1**, a 4-byte value that the engine
then stored as the function's output (`FuncProto::updateOutputNoTypes` takes the address of the narrowed varnode) and the host
lent as a 4-byte return, which is r0. With upstream's order (the low piece r0) the narrowing is storage-consistent, which is why
patch 0007 exposed it. The cspec is right; the narrowing was only ever valid when the low piece is the 4-byte entry.

*Fix* (patch 0033, `coreaction.cc`): a return varnode in the join space is consumed whole (8 bytes). The function now returns
the pair: `uint8 f(xunknown4 param_1, uint4 param_2) { return (uint8)param_2; }` for `mov r5,r1 ; mov #0,r0`, lent with an 8-byte
return; the emulator's r0 = 0 and r1 = r5 are both what the text says. Not done: telling a 64-bit return from a scratch r1 (A2-17,
decided below). Ledger fixtures `6153 E000 000B 0009`, `6153 7101 E000 000B 0009`, `6153 4501 8D01 4129 324D E200 000B 6023`, and
`E011 6153 000B 0009` (question 2).

### 3. A2-14: `bVar1 == true` for `== 2`

`PrintC::pushBoolConstant` printed any non-zero value of a boolean type as `true`. `addv` with a known addend folds `src == 2` to a
comparison of the flag with 2; the type of the constant follows the flag's. Patch 0033 (`printc.cc`) prints a value other than 0 and 1
as the integer: `(!bVar1 || bVar1 == 2) && ...`, always false in its left operand's second half, which is what the p-code says
(the text is not simplified: that is a rule's, not the printer's). The four fixtures `E501 345F 000B 0029`, `4501 345F 000B 0029`,
`655D 345F 000B 0029`, `4501 354F 000B 0029` agree with the emulator for every input; before, the first was wrong at r4 = `FFFFFFFF`
(1 against 0).

### 4. A2-15: an input no register holds

`unaff_10000008` is a unique-space varnode (the engine's own temporaries start at `10000000`) read as an input. gdb on
`Funcdata::setInputVarnode` found its maker: `SubvariableFlow::replaceInput`, called from `RuleSubvarZext` on the `ZEXT` of
`shlr`'s T bit. The flow narrows `r4` to its low byte; so that the narrow input does not overlap the 4-byte one, `replaceInput` swaps
the 4-byte input for a fresh unique-space input and rewires all its readers to it. An operation the flow leaves in place (the
original of a node whose result also has readers outside the flow: here `r4 & 1`, read by the comparison that makes T) still
reads that temporary, which nothing defines, and prints as `unaff_`. Upstream's comment says the temporary "gets quickly
eliminated"; it does not when such a reader remains. Patch 0033 (`subflow.cc`): after the patches are applied, a temporary that is
still read by an operation with a live result gets a `ZEXT` of the logical input at the top of the entry block and its readers
move to that (the flow was accepted on those readers using only the logical bits). `4401 4424 000B 604C` prints
`return param_1 & 0xfe | (param_1 & 1) == 1;` and agrees with the emulator. No function of either game image had an
`unaff_<address>` name, before or after.

### 5. A2-13: writes to SR, GBR and VBR

*Decision.* These are side effects the caller sees (the interrupt mask, GBR's base, the vector table), so the write must print, in
order, and the register it is written from is an input the function reads. The register itself stays dead at the return
(`killedbycall`/`unaffected`, patch 0026: the model is right about the ABI), so what is kept is the *act*.

*Fix* (patch 0034, `superh.sinc`): `ldc Rm,SR/GBR/VBR` and the `ldc.l @Rm+` forms also call a user op, `Write_SR(value)` (SR's value
masked by `0x3F3`, patch 0019), `Write_GBR(value)`, `Write_VBR(value)`. A user op is never dead code, and it reads its operand,
so the source register stays a parameter. `ldc r4,vbr ; rts ; nop` prints `void f(xunknown4 param_1) { Write_VBR(param_1); return; }`
and is lent one parameter; the critical section prints `Write_SR(in_sr & 0x3f3 | 0xf0); *param_1 = param_2; Write_SR(in_sr & 0x3f3);
return in_sr | 0xf0;` (`stc sr,r1 ; mov r1,r0 ; or #0xF0,r0 ; ldc r0,sr ; mov.l r5,@r4 ; ldc r1,sr`), `di` prints `Write_SR(in_sr &
0x3f3 | 0xf0)`. The p-code emulator refuses an unhooked user op, so the four emulator harnesses hook the three to do nothing
(`FrontEnd/tests/Sh2EmulatorHooks.h`; `Sh2DecompilerTests`, `Sh2SemanticsTests`, `Sh2DecodeTests`, `Sh2GeneratedTests`).

*Host, one line, outside the engine and the cspec:* `ReturnCheck` (`Sh2Decompiler.cpp`, the `CPUI_CALLOTHER` case) treated every user
op but `Sleep_Standby` as a call with unknown effects, which loses PR and r15 and withholds the signature (`a return not to its
caller`) of any function with an `ldc`. It now treats the three `Write_*` like `Sleep_Standby`. This is the only edit to
`Sh2Decompiler.cpp`, and it is not eligibility or lending logic proper; it is the one thing to look at when merging with the
lending fixer's branch (without it, the A2-13 fixtures print right but are lent no signature: the outcome the row allowed, "withheld",
and four `TestArea2Round3` checks fail).

*Left:* the restore of SR by `rte` is not a `Write_SR` (a handler's own business); the caller's side (a lent callee that changes GBR or
VBR is taken to keep it) is A3-3.

### 6. The three questions

1. **Byte-wide `char` arithmetic read as C.** The program does not read the pseudocode as C: `DECOMPILER_GUIDE.md` says "pseudocode for
   reading, not source that compiles", and Ghidra prints sub-`int` arithmetic at its own width everywhere. Decided: accepted
   (A2-19). The guide now says what the casts mean; the oracle counts the difference apart (`narrow-c`: 6 of 10,017 generated
   functions had it, none with a different value in the engine's reading) instead of failing it or hiding the class.
2. **A dirty r1 claims an 8-byte return.** Decided: accepted (A2-17). The code cannot say whether r1 is a scratch register or the low half
   of a 64-bit value; 8 bytes is never a wrong value (both halves are the code's), 4 bytes would drop half of a real 64-bit return
   silently. The corpus has 10 of 839 and 10 of 1,554 functions with an 8-byte return, unchanged by this round. A rule needs
   evidence (a MACH/MACL pair moved into r0:r1, a carry chain, a callee's 8-byte output), which a leaf function does not show.
3. **`trapa` in a delay slot, no warning.** Decided: accepted (A2-18, P3). Not small: the warning is the host's (the body's delay slots,
   decoded) or a slot context on all ten delayed branches in the spec; no compiler emits it; the SH-2 raises the slot-illegal
   exception, which A2-8 (b) already accepted as not modelled.

### 7. Mutation

Each change reverted alone, the engine/spec rebuilt (`se-regen-sla` for the spec), the printed-C run (300 seeds, the fixtures first) and
`Sh2DecompilerTests` run; restored, both pass.

| Reverted | printed-C (317 functions: 17 fixtures, 300 seeds) | `Sh2DecompilerTests` |
|---|---|---|
| A2-12: `gatherConsumedReturn` as before (`coreaction.cc`) | 5 findings (the fixtures and a generated function: `run=00000000:...` against `pseudo=...:00000000`) | 6 checks fail (`TestArea2Round3`: 8-byte return, `return param_2;`) |
| A2-14: `pushBoolConstant` as before (`printc.cc`) | 4 findings (the four `addv` fixtures, at r4 = `FFFFFFFF`) | 8 checks fail (`== true`, `== 2`) |
| A2-15: the `ZEXT` repair removed (`subflow.cc`) | 1 finding, `input-outside-registers` (`unaff_10000008`) | 2 checks fail |
| A2-13: the `Write_*` calls dropped from `ldc` (`superh.sinc`, sla regenerated) | 29 findings (the control registers differ from the run's) | 5 checks fail (`Write_VBR(param_1);` ...) |
| A2-13: `ReturnCheck` without the `Write_*` condition (`Sh2Decompiler.cpp`) | 0 (the oracle does not see lending) | 4 checks fail (the signature is withheld) |

Every revert is caught by the check it was added for; the first run of the A2-14 revert found nothing in the printed-C run and the
cause was the oracle's: 24 random inputs rarely drew r4 = `FFFFFFFF`. The first 22 of the 32 inputs now walk the edge values through
r4-r7. The same run found that a batch whose compiled program crashes (a fixture that stored through a pointer parameter) printed no
mismatch at all; a program that does not print its `BAD <n>` line is now a failure (`crashed`, bisected), and a text that dereferences is
skipped (`skipped:memory`).

### 8. Matrix (RelWithDebInfo, gcc 13.3, 4 cores shared with four other agents)

| Check | Result |
|---|---|
| whole `ctest` | 97 of 97 passed (106 s; `SaturnExplorerSh2PrintedCTests` 6.6 s, `SaturnExplorerSh2GeneratedTests` 7.7 s) |
| `SaturnExplorerSlaFreshnessTests` | passed (`sh-2.sla` and `Sh2SpecData.h` regenerated with `se-regen-sla`, committed) |
| `vendor.py` on the pinned Ghidra (`918d44e`) | applies patches 0001 to 0028, 0033, 0034; the tree is identical to the committed one, `sh-2.sla` aside |
| generator, 3,000 seeds (both grammars) | 0 failing seeds; 10,000 seeds: 1 finding, seed 7028, A2-11 (open, also fails with `SE_GENERATED_ALPHABET=narrow`) |
| printed-C, 10,017 functions (10,000 seeds and the fixtures), 32 inputs | 0 findings; 7,959 compared (8,211 agree), 6 `narrow-c`, 864 skipped (calls, `braf` to a constant), 934 `void-return` |
| gcc Debug ASan + UBSan + LSan (`detect_leaks=1`), the CI script | PanelTests, RunnerTests, DecodeTests, EngineTests, FixtureTests, Sh2DecompilerTests, GeneratedTests, **PrintedCTests**, SemanticsTests all exit 0; no ASan/LSan report; UBSan lines only the two accepted sites (`address.hh:605`, `slghpatexpress.cc:979`) |
| timing | game corpus 007: 12.2 s against 12.6 s, 008: 32.1 s against 33.4 s (the shared machine's noise; no per-function comparison made) |

### 9. Corpus

`SE_GAME_HWRAM=hwram007.bin` and `hwram008.bin`, `SaturnExplorerSh2DecompilerTests`, `SE_GAME_OUT` compared with the same run on `e7cff08`:

| | functions | ok | every ledger count | missing and said | text changed |
|---|---|---|---|---|---|
| 007 | 839 | 839 | 0 (not printed once, another width, two reads, past another access, conditional, out of order, switch, dropped) | 16 | 36 |
| 008 | 1,554 | 1,554 | 0 | 20 | 55 |

All 91 changed texts differ by `Write_SR`, `Write_GBR` or `Write_VBR` lines and what they bring (the value written is a use, so the
SR computations that fed an `ldc` now print): 89 `Write_SR` (24 and 65), 96 `Write_GBR` (48 and 48), 1 `Write_VBR`. Nothing else
changed: no signature line, no 8-byte return (10 and 10 before and after), no `unaff_<address>`. Read: `06024278` (007) is
`Write_GBR(0x6002400); xVar1 = func_0x06024350(); Write_GBR(unaff_gbr); return xVar1;`, the GBR-setting wrapper A2-13 is about; `06005520`
is `Write_SR(in_sr & 0x3f3 | 0xf0); ... Write_SR(extraout_sr & 0x303);`, an interrupt-masked section; `0600541E` brings its SR
arithmetic into a polling loop (`uVar1 = (uint4)((uVar3 & 1) == 0); ... while (uVar1 == 0)`), because the `ldc` after the loop writes the
whole SR including T, which the loop's compare set; the one `Write_VBR(extraout_r4 + 0x49)` sits in a function that ends in
`halt_baddata()`. The A2-12 fix changed no corpus text: the 10 functions with an 8-byte return in each image had both halves live.

### 10. Not done, and for the coordinator

- **A2-16 (open, P3, areas 4 and 3):** a jump table whose case does `stc sr,r1` prints `switch` on the loaded address. Fenced in the
  generator (the first two grammars emit no `stc`/`sts`, `SE_GENERATED_ALPHABET=specials` lets them in), so the nightly does not
  fail on it, nor on A3-5 through `stc gbr,r3 ; mov r3,r0` (a copy of an input register returned prints `void`); remove the
  fence (`gSpecialReads`) when both rows are fixed.
- **A2-20 (open, P3):** `bsrf`, `trapa`, `rte` are not emitted.
- **`void-return` is counted, not failed** (A3-5's class: a function that returns a copy of an input register prints `void`): when A3-5 is
  fixed, make `IsFailing` include it and the oracle will check those values too (934 of 10,017 functions).
- **GBR/VBR writes of a callee are still taken to be kept by its caller** (A3-3); and `Write_SR` after a call shows the callee's SR as
  `extraout_sr`, which is right but long.
- **`braf` to a constant** is a tail call to the target (documented in the guide); the oracle skips those (about 5% of functions), so
  `braf` is in the alphabet but its value is not compared. A `braf` over a computed distance is a jump table and is flagged.
- **Merge:** patches 0033 and 0034 add two rows at the end of `patches/README.md` and `VENDOR.md` (0029 to 0032 will too: a trivial
  conflict); `Sh2Decompiler.cpp` has one changed condition (section 5); `Sh2DecompilerTests.cpp` has `TestArea2Round3` added before
  `TestAllocationFailureIsReported` and the hook line in `RunMachine`; the `.github` workflow and sanitizer script have one step each.

## Areas 1 and 8, round 3: the too-large loop, the badge that blamed the box, and a refusal that was forgotten

Branch `review/1-8-round3-fix`, fix in `b1e2e0e` (tests tightened in `f79e7c7`). Ledger rows A1-6, A8-2, A8-3 and A8-4 are `fixed b1e2e0e`.
The reviewer's open question is a real finding and is recorded as **A1-7 (P3, open)**: see below. All four fixtures are directed
`DecompilerPanelTests` sequences, as the review said, plus two model pins for A8-3.

**Root causes.**

- **A1-6.** The auto-refresh rule was "only once the followed PC has left the function shown", tested as `!mShown.InBody(pc)`. A
  too-large answer has no body, so every PC had left it. Fix: while the shown answer is `TooLarge`, the panel asks the same question a
  refresh would put -- which entry Follow PC resolves this PC to (`FindFunctionContaining` on the held snapshot, with the hints
  `Resolve` uses, now `FinderHintsFor`) -- and the PC is inside when that is the entry that ran out of budget
  (`mShownAsked`, recorded by `Install`, since a too-large result carries no body to say). The finder pass runs at most once per
  `kAutoRefreshFrames` (`mTooLargeCheckTick`), not every frame the PC sits there. Leaving the function refreshes once, as before.
- **A8-2.** `StaleTooltip` read `mResolved && !mEntryOk` as "the entry box is wrong". `Resolve` now records `EntryFailure` (refused,
  not code, no registers, not found) and the tooltip names what to change for the target: the followed PC, the missing registers, the
  address "Decompile" asked about; the entry-box wording stays for a refused or non-code entry. The failure branch comes before the
  running emulator's "moved on, press Refresh", because a refresh alone would not help with the PC in the BIOS.
- **A8-3.** `ShownStale()` counted `mPending.active`. Stale is now a property of the output against the inputs on screen (view, names,
  volatile list, function set, entry); a request outstanding for the very question shown is "decompiling..." and
  `StaleReason::Pending` appears only when the inputs differ. So Refresh on current output keeps full brightness and the halt hint.
- **A8-4.** The box's refusal set `mResolved`/`mEntryOk=false` but left the target alone, so the next `Resolve` (any capture) brought
  the old target back. `mEntryRefused` now makes `Resolve` return the refusal until `SetTarget` (a valid entry, Follow PC toggled,
  "Decompile" from elsewhere). The retry of an unanswered Follow PC (`unanswered`) skips a refusal, so a moving PC does not copy 2 MiB
  per interval for it.

**Stepping a paused emulator inside a too-large function is not changed, and is accepted** (ledger A1-6). The brief asked that a paused
step not re-run. A paused step is a new snapshot, so a new question, as `DECOMPILER_GUIDE.md` documents ("another frame asks again").
Carrying the timing verdict across snapshots would also carry it across an edit that shrinks the function, and the two are
indistinguishable at the view identity (both move only the derive serial). The cost is bounded by the user's stepping, not by time.

**The model.** `CheckStaleMark` (held engine) is now two-way: a stale mark is a failure when the inputs shown are the inputs now,
as well as required when they differ. Its one excuse is the names: the panel keys names by generation, so a rename undone is
another generation and conservatively stale though the content is equal (`shownNamesGen`). `CheckStaleReason` accepts "Enter" as well
as "Refresh" for a `NeedsRefresh` tooltip (the new Containing text). Pinned "A8-3" (`select A; wait; hold; refresh; wait`) and
"A8-3 (twice)" (`select D; wait; hold; refresh; refresh; wait`). `kOpen` stays empty.

**A1-7 (open, P3): the emulator-load wait re-asks a paused panel every frame.** Read in `App.cpp`: `restoreWaiting =
mRestoreOutstanding > 0` makes a paused emulator capture every UI frame, and the `se_live_capture_unchanged` shortcut that spares a
halted one is deliberately not applied (the restore counters are read inside that block). `mRestoreTimedOut` after 10 s only
silences the log and lifts the recorder's wait; `mRestoreOutstanding` stays up until `ResolveRestoreWait`. Each capture runs
`RebuildDerived`, moving `se_derive_serial` with identical bytes, so a `LivePaused` view has a new identity every frame and the panel
captures, submits and supersedes its own run: a function longer than a frame is never shown while the wait lasts (forever against an
emulator that never confirms). Not reproduced (needs a live server that withholds confirmation). Not fixed: the race-free fix needs
a driver accessor for the captured snapshot's `revision` and a decompiler-only view serial (derive serial minus byte-identical
recaptures, with no edit between), in the driver and `App`'s per-frame capture, which cannot be exercised end to end here and is
outside the panel/test scope of this fix. A check-before-capture with `se_live_capture_unchanged` alone was rejected: a snapshot
published between the check and the capture would be taken while counted as a repeat, which is a stale-shown-as-current. Freezing the
view during the wait was rejected too: it would hide real changes against an emulator that applied the load but never confirmed.

**Mutation check** (each one expression; revert the fix, the new test fails; restored, passes).

| Break | Fails |
|---|---|
| TooLarge branch of `autoDue` disabled (A1-6 as it was) | `TestAutoRefreshTooLargeFunctionIsNotRerun` (5 checks: captures, submissions, `RunsStarted`, and the two that follow) |
| `ShownStale` counts `mPending.active` again (A8-3 as it was) | `TestRefreshOfCurrentOutputIsNotStale` (4 checks); model: seeds 8, 10, 11, 13, 17 of the default 18, and both pins "A8-3" and "A8-3 (twice)" (STALE SHOWN WHILE CURRENT) |
| `Resolve` ignores `mEntryRefused` (A8-4 as it was) | `TestRefusedEntryStandsUntilChanged` (4 checks) |
| `unanswered` retry does not skip a refusal | `TestRefusedEntryStandsUntilChanged` (the running-emulator part) |
| `NotFound` and `NoRegisters` recorded as `NotCode` (A8-2 as it was) | `TestStaleTooltipNamesTheCause` (7 checks: Follow PC, no registers, Containing) |
| the running emulator's "moved on" text ahead of the failure text | `TestStaleTooltipNamesTheCause` (the running-emulator part) |

Not mutation-checked: the once-per-interval throttle on the finder pass (`mTooLargeCheckTick`) changes cost, not behaviour, and no
test counts finder calls.

**Matrix** (RelWithDebInfo, a 4-core container shared with other agents, load average up to 25). `SaturnExplorerDecompilerModelTests`
default run: 18 seeds and 16 pinned sequences, 0 failures (817 operations, 478 results checked at install, 849 idle checks, 265
held-engine stale checks, 576 submissions, 27 discarded). `SE_MODEL_SEEDS=300` (164 s): 0 of 300 fail; 11,907 operations (1,729
without effect), 814 finish-then-change races, 65 stops, 29 budget stalls, 6,879 results checked at install, 11,923 idle checks, 3,898
held-engine stale checks (each now two-way), 1,330 walks, 8,144 submissions, 402 discarded answers, 1,499 cache hits.
`DecompilerPanelTests` (4 new tests; about 5 s) and `DecompilerRunnerTests` (41.6 s): pass. Full `ctest`: 96 of 96 pass (`ctest -j2`, 109 s), among them DecompilerRunnerTests, DecompilerPanelTests, DecompilerModelTests, DecompilerFaultTests, Sh2GeneratedTests and Sh2DecompilerTests. Not run here:
the game corpus (not in the repository), ASan/UBSan/LSan, the timing comparison against the previous master.

**Not done.** A1-7 (above). Stepping inside a too-large function still asks again (accepted). The lent-prototype set is not part of
"stale": a caller shown with an older callee signature is not marked while its re-ask runs (it was before, by the pending rule), as
it never was when nothing was outstanding; the model's `Sig` does not carry prototypes either. `App` is still out of the model's
reach. The "unimplemented" indicator and `DecompileStatus::Disabled` are unchanged.

## Area 6, round 2: clone before free, merge before move, pins with names, five fixtures

Fixer, branch `review/6-round2-fix` on master `b822035`. Fix commit `0b1f4d5` (engine patch 0032, the vendored tree, the host's `Disassemble`, the matrix, the `patches/` and `VENDOR.md` rows); this record and the ledger are the commit after it. Ledger rows A6-10 to A6-15 are `fixed 0b1f4d5`; new rows A6-16 (`Disassemble`, fixed), A6-17 (Stop during the reference run, accepted) and A6-18 (`jumptable.cc`, open: another branch's file). The vendored tree is patches 0001 to 0028 and 0032 (0029 to 0031 are other branches'); `vendor.py` on the pinned Ghidra reproduces `cpp/`, `sources.cmake` and `processors/` exactly (checked in a copy, `diff -r` empty).

**Oracle first.** Before any engine change the matrix was widened and run on the unfixed tree.

- *A6-13: five fixtures.* `shapes` (loops, `&&`, `||`, a do-while, an if/else: variables that merge) and `stack` (a stack array: restructuring and naming) are in `kFixtures` with the reviewer's words. `alloc` and `cancel-oom` run on `mailbox`, `jumptable`, `fixture1`, `shapes` and `stack` in the default subset, and on `diamonds` in the full run (411,659 allocations: its dense prefix and geometric tail only: 3,540 cells in 22 s; every k of it is 411,659 cells of up to 0.15 s, which I started and stopped after 25 minutes). The cancel families keep the four fixtures they had in the default subset (the ASan default subset is over five minutes already); `shapes` and `stack` join them in the full run. `SE_FAULT_PHASE=<names>` and `SE_FAULT_RANGE=<lo>-<hi>` select every k of chosen phases or a range, which is how the sanitizer sweeps below were run (the whole run under ASan is hours). The reviewer's numbers for the unfixed tree, RelWithDebInfo, every k: `fixture1` 6 crashes of 21,831 cells (A6-10), `shapes` 11 of 40,442 (A6-11); the mutations below reproduce the crashes on the pinned cells.
- *A6-12: a pin has a name.* Every pin carries `site`: the functions on the stack of the allocation, innermost first, each a substring of a demangled name, all in order (`ProtoStoreSymbol::clone;FuncProto::copy`). The test is linked `-rdynamic` (Linux), the allocator records the stack (`backtrace`) of the allocations that could be a pin (the pin's size, in its phase entry), and one extra run of the warmed engine in a child names them with `dladdr`. When the coordinates' allocation is the site, the pin is `site ok`; when not, every other allocation of that size in that entry that is the site becomes a cell (at most 16; the pin has `MOVED`); when none is, the driver fails `STALE` and says which function the coordinates did lead to. `--sites <fixture> <phase> <ord> <size>` lists the candidates with their stacks, which is how the 64 pins were written. On a platform without `backtrace` (not glibc) the coordinates alone are used and the pin says so.
- *A6-15: the accepted leaks are bounded by the phase.* `AcceptedLeak` has `initOnly`, set from the baseline log's phase of the failed allocation (`gFailedInInit`: before the engine or Init). With the frames alone, 29 of 945 cells of `stack`'s Actions passed as "the host register loop". After the engine fixes the register-loop, conflict-symbol and `deriveAction` rows are no longer used by any sweep and are gone; four rows remain (the XML reader, `universalAction`, the compiler spec's `parseCompilerConfig`/`findAddJoin`, `FreeArray::reset`), all Init only. The aggregator names each row (it counted all A6-7 rows as one).

**Root causes.**

- *A6-10 (P1).* `FuncProto::copy`: `delete store; store = op2.store->clone();`. When the clone fails (`ProtoStoreSymbol::clone` allocates the store and then its parameter) the freed pointer stays in `store`, and `~FuncProto` frees it again. It clones first, copies the effect list before anything is freed (a failure there frees the clone), then frees and assigns. `ProtoStoreSymbol::clone` and `ProtoStoreInternal::clone` deleted the constructor's parameter before copying the other's and leaked the store when a copy failed: they copy first and free the store on any failure.
- *A6-11 (P1).* `HighVariable::mergeInternal` pointed each Varnode of `tv2` at `this` (`setHigh`) and only then allocated `instcopy` and `resize`d `inst`. A failure left the Varnodes pointing at `this` while `tv2` still listed them; at teardown each removed itself from a HighVariable that the first removal had freed (heap-use-after-free; a SIGSEGV or an abort in a plain build, which the heap decides). The merged list is built first (`vector merged(n)`, `std::merge`), then the move (`setHigh` only assigns) and `inst.swap`, so nothing after the first allocation can throw but the cover merge; a failed cover merge marks the cover dirty and frees the emptied `tv2` (it was leaked).
- *A6-14 (P3).* `VarnodeBank::setInput`, `setDef` and `makeFree` erase a Varnode from `loc_tree` (which owns it) and reinsert it; a failed insertion left it in neither and the ops that read it still point at it, so it cannot be freed on the spot. It goes to `limbo`, a vector whose room is reserved before the Varnode leaves the lists and that `clear` frees (`keepOrphan` first checks the Varnode did not get into `loc_tree`). `PcodeOpBank::destroy` erased the op from its lists and then failed to put it on `deadandgone`: it is pushed first (the erasures cannot fail). `PcodeOpBank::createIndirect` is `create`'s twin (round 1 fixed `create`, not this) and `MapState::addRange` and its terminating hint freed nothing when the hint list could not grow.
- *A6-15 (P3).* `ScopeInternal::renameSymbol` (A6-8's shape): the symbol was erased from `nametree` and the reinsertion could fail, leaking it. It copies the strings first, keeps the symbol in `limbo` (freed by `~ScopeInternal`) when the insertion fails. The other Actions leaks the narrowed rows exposed were the same family: `ScopeInternal::addSymbolInternal` freed a symbol only on `LowlevelError` (the contract in its caller's comment is that it deletes on failure), so a `bad_alloc` in `insertNameTree` leaked it; `Scope::addMapPoint` and `addSymbolWithConflict` leaked the new map entry when the symbol's entry list could not grow (the symbol owns an entry once it lists it).
- *Found by the new sweeps, not named by the reviewer* (P3 leaks, fixed in the same patch because each is the A6-8/A6-9 shape): `FlowInfo::setupCallSpecs` and `setupCallindSpecs` (a `FuncCallSpecs` the function cannot list), `BlockGraph::newBlock` and `newBlockBasic` (round 1 made the other `newBlock*` own their block, not these two), the virtual root of `createVirtualRoot` and `calcForwardDominator`, `PackedDecode::allocateNextInputBuffer` and `endIngest` (a decoder buffer the stream cannot hold; the host's `LinesFromMarkup` is the caller), the cloned action tree (`ActionGroup`, `ActionRestartGroup`, `ActionPool::clone`, `ActionDatabase::deriveAction`: the jump-table analysis clones the tree in the Flow phase, 294 of 4,037 sampled jumptable cells leaked, round 1 had accepted this only for Init), `TypeFactory::getTypeVoid` (the "void" type is freed if the tree cannot take it), and `Database::attachScope`, which released the new `ScopeLocal` before its parent's map held it (leaked, and left its id in `idmap`; reached by `Funcdata`'s constructor).
- *A6-16 (P3).* `Disassemble` returned the listing so far after a failure, with no line saying it stopped, and three allocations (the copy of the result, the refusal's `std::vector`, `StartLibrary`) were outside the `try`. The whole body is inside it, the result is moved out, a failure ends the listing with `<stopped: out of memory or an unexpected exception>`, and the engine is marked damaged as before. A new `disasm` family (every k of a three-instruction listing) checks it.
- *A6-17 (decided, accepted).* A Stop in the reference run leaves `Ok` with the warning "could not be checked for hardware accesses (cancelled)" and the prototype withheld, and the panel caches it. That is the design (NoteRemovedAccesses: "the shown output is finished; only this check was cut short"), the result says what it lacks, and the panel's rule after a Stop is that the same question is not asked again until Refresh, which bypasses the cache. A cached degraded answer would be wrong only if re-asking were silent; it is not. Left to the panel's owner if the policy changes (the panel is another branch's file).

**What was searched, and what it found.**

1. *`delete X;` followed within eight lines by an assignment to `X`* (every engine `.cc` and `.hh`, 59 hits; the SLEIGH compiler's files, which the host does not run, `slghpatexpress.cc`, `slghpattern.cc`, `slghsymbol.cc`, `pcodeparse.cc`, are skipped). Fixed here: `FuncProto::copy`, `ProtoStoreSymbol::clone`, `ProtoStoreInternal::clone`, and four that no fixture reaches (no test): `FuncProto::setParamshift`, `TypeCode::setPrototype`, `Varnode::calcCover` (only called with a null cover), `circularqueue::setMax` (the `maxlinewidth` option). Fixed before (0015, 0023): `ProtoStore*::setOutput/setInput/clearOutput`, the three prettyprint sites. Assign null or move a pointer, no allocation between: `FuncProto` store clear, `Funcdata::clearActiveOutput`, `FreeArray::operator=` and `reset` (allocates first), `JumpTable::saveModel`/`restoreSavedModel`, `clearSavedModel`, `pcodeinject`, `Sleigh::clearForDelete` (0002), `Varnode::clearCover`, `TypeCode::setPrototype` (null overload), `AddrSpaceManager::insertResolver`, `UserOpManage` (the allocation is released from a `unique_ptr`), `InjectPayload` registration (allocates first), `XmlDecode::ingestStream`. Not reached by the host: `PrintJava`'s constructor (only the default language is built). **In `jumptable.cc`, another branch's file: `JumpTable::recoverModel` and `setOverride` free the old model before allocating the new one** (A6-18, open).
2. *Move-then-allocate in `variable.cc`, `merge.cc`, `heritage.cc`.* `variable.cc`: `HighVariable::mergeInternal` (A6-11, fixed); `merge` calls `moveIntersectTests` (cache only: the cache is freed with `Merge`) and `transferPiece` (pointer moves) before it, which cannot orphan anything; `VariableGroup::addPiece` and `VariablePiece::transferGroup` set a piece's group before the set insertion that can fail (the piece then belongs to a group that does not list it; teardown erases an absent key, harmless) and are reached only for a joined 64-bit variable's pieces, which no fixture builds. `merge.cc` and `heritage.cc` change owners only through `HighVariable`, `Varnode` and `PcodeOp` (`setHigh`, `opSetInput`, `descend` lists) whose containers do not own: a half-done update leaves a stale reference in a list that is torn down with the function, not a double free. The real owners (`VarnodeBank`, `PcodeOpBank`, `BlockGraph`, the scope's name tree, `TypeFactory::tree`) were read for the next shape.
3. *Erase-then-reinsert into an owning container* (`.erase(` followed within twelve lines by an insertion, 31 hits): `VarnodeBank::makeFree/setInput/setDef`, `PcodeOpBank::destroy`, `ScopeInternal::renameSymbol` (all fixed above); `RangeList::insertRange/removeRange` (`set<Range>`, values), `Heritage` location map and task list (values), `merge.cc:389` (a list of `HighVariable *` that nothing owns), `constseq.cc` (points), `PcodeOpBank::changeOpcode` (an index) own nothing. **Left: `TypeFactory::setName`, both `setFields`, `setPrototype`, `setEnumValues`, `recalcPointerSubmeta` erase a data-type from `tree`, which owns it, and reinsert it**: a failed insertion leaks the type (it stays referenced, so nothing is freed twice). No fixture reaches them after Init (`setName` runs for the core types, and the Init sweeps accept the XML and action-tree leaks around it); bounded like A6-7; no row, listed here.
4. *A new object, then a container takes it* (`x = new T` followed within four lines by a hand-over, 55 hits outside the SLEIGH compiler): the sweeps' leak survey is the oracle for which of them a failure reaches. It found `setupCallSpecs`/`setupCallindSpecs`, `newBlock`/`newBlockBasic`, `createVirtualRoot`, `PackedDecode`, the action clones and `attachScope`, all fixed. The rest were not hit by any sampled cell and are left: the compiler-side (`pcodecompile.cc`, `signature.cc` and `transform.cc` are not run by the host), `emulate.cc`, `memstate.cc` (the p-code emulator), `DynamicEntry` (`addDynamicMapInternal`, dynamic symbols are not used), `ScopeLocal::remapSymbol`/`remapSymbolDynamic`, `inject_sleigh.cc` (the injection library), `BranchPoint` and `BlockTrace` (`blockaction.cc:970`), `JumpTable` objects in `funcdata_block.cc` and `funcdata_op.cc` (jump-table sets: `jumpvec.push_back` after `new JumpTable`).

**Mutation** (a copy of the tree with one file put back; RelWithDebInfo for the crash rows, ASan + UBSan Debug with `detect_leaks=1` and `SE_FAULT_ONLY=alloc` for the leak rows; restored and verified identical after each):

| Reverted | Result |
|---|---|
| `fspec.cc` (A6-10) | default subset, `alloc fixture1`: 5 pinned cells crash (signal 11): 64 B #58, 72 B #27 and the second pass's two |
| `variable.cc` (A6-11) | `alloc shapes`: 3 pinned cells crash (14300, 14490, 29055), 1 pin STALE, 2 MOVED |
| `Disassemble` as in `b822035` (A6-16) | `disasm`: 38 of 40 default cells fail ("a failure that does not end the listing leaves it unchanged -- (empty)", and the last four allocations escape as `bad_alloc`) |
| A6-2 put back in `prettyprint.cc`, plus one 16-byte host allocation after `Enter(Print)` (the A6-12 mutant) | default subset: 1 of 38 jobs fails: the pin `mailbox print #0 16 B #1` is MOVED (it led to `setOutputStream`) and finds `setPackedOutput`; cell 22943 crashes. Before: 0 of 25 fail |
| the extra host allocation alone | default subset passes; the same pin is reported MOVED |
| `varnode.cc` (A6-14) | `alloc stack`: 3 cells leak (9224 `setInput`, 9310 `makeFree`, 9685 `setDef`) |
| `op.cc` (A6-14) | `alloc stack`: cell 9313 leaks (`PcodeOpBank::destroy`) |
| `varmap.cc` (A6-14) | `alloc stack`: cell 9334 leaks (`MapState::addRange`) |
| `database.cc` (A6-15, `attachScope`) | `alloc stack`: 9345, 9347, 10064 leak (`addSymbol`, `addMapPoint`, `renameSymbol`); `alloc jumptable`: 17110 (`Funcdata::Funcdata`, `attachScope`) |
| `action.cc` | `alloc jumptable`: 9678, 9679 leak (`ActionStart::clone`, `ActionRestartGroup::clone`) |
| `block.cc` | `alloc mailbox`: 9243 (`newBlockBasic`) and 10125 (`createVirtualRoot`) leak |
| `flow.cc` | `alloc fixture1`: 9240 leaks (`setupCallindSpecs`) |
| `marshal.hh` (`allocateNextInputBuffer`) | `alloc mailbox`: 22961 leaks |
| `type.cc` (`getTypeVoid`) | `alloc jumptable`: 16882 leaks (`TypeVoid::clone`) |
| `marshal.cc` alone (`endIngest`) | not caught: the pin is on the header's buffer; `endIngest`'s twin of it is a last-buffer case no fixture reaches |

**Runs.**

- RelWithDebInfo, `alloc` for every k (`SE_FAULT_DENSE=100000`, a driver per fixture, 5 in parallel on 4 cores): mailbox 35,963 cells, jumptable 34,024, fixture1 21,831, shapes 40,422, stack 21,373 (153,613 in all), **0 failed**, 262 to 574 s each.  The `diamonds` sweep of the full run (`SE_FAULT_FULL=1 SE_FAULT_ONLY="alloc diamonds"`: dense prefix and geometric tail) is 3,540 cells, 0 failed, 22 s. `disasm`, every k: 3,846 cells, 0 failed, 9 s.
- ASan + UBSan, gcc 13.3, Debug, the default subset with `detect_leaks=1`: **32 of 32 jobs pass, 318 s** (ctest's limit is 600; round 1: 25 jobs, 154 s; the five-fixture `alloc`, `cancel-oom`, the pins' census and `disasm` account for the rest). No UBSan line beyond `address.hh:605` and `slghpatexpress.cc:979` (none appeared).
- ASan + LeakSanitizer sweeps (accepted leaks narrowed to Init as above): every k of the Actions of `stack`, both passes, 1,894 cells, 0 failed, 0 accepted leaks; every 4th k of the Flow, Actions and Print phases of mailbox (2,283 cells), jumptable (4,037), fixture1 (987) and shapes (5,636), 0 failed after the fixes (before the last two: 6 + 294 + 5 + 4 leaks, listed above); every 8th k of Init and Teardown, two offsets (shards 0 and 3, then 5, of 8) of mailbox, jumptable, stack and fixture1: 0 failed after `getTypeVoid` and `attachScope`, with 190 to 490 accepted A6-7 cells per sweep in the three rows that remain (action tree, XML, compiler spec; `FreeArray::reset` in a few).
- `ctest` (whole, RelWithDebInfo, `-j2`): **96 of 96 pass**, 153 s; `SaturnExplorerDecompilerFaultTests` 30 s under load (no `WILL_FAIL`, never skipped), `DecompilerRunnerTests` 72 s.
- The game corpus, `hwram008.bin`: 1,554 functions, 1,554 ok, 0 failed, 0 other; every ledger count 0 (0 not printed exactly once, 0 instructions with more accesses than they make, 0 accesses of another width, 0 statements with two reads, 0 reads printed past another access or a call, 0 printed as conditional, 0 pairs out of the code's order, 0 joined into a switch across a branch, 0 accesses dropped against the reference run, 0 functions without a reference, 0 ended dead); 20 missing and said. 22 s.
- `vendor.py` on the pinned Ghidra (`918d44e`) in a copy of `ghidra-decompiler/`: `cpp/`, `sources.cmake` and `processors/` identical to the committed tree.

**Not done.**

- `diamonds` under the `alloc` family is in the full run only as the dense prefix and the geometric tail (3,540 cells, RelWithDebInfo); every k of it and any sanitizer run of it were not done; `cancel-us` on it under ASan is as before.
- The leaks were surveyed at a quarter of every k of Flow, Actions and Print for four fixtures, every k of `stack`'s Actions, and an eighth of Init and Teardown for three: a leak at a k that is in none of them is not excluded. The unfixed engine was not rerun under ASan (the reviewer's 41 of 945 cells of `stack` stand as the before; each fix is mutation-checked above).
- Five fixes have no fixture that reaches them and so no test (`setParamshift`, `TypeCode::setPrototype`, `calcCover`, `circularqueue::setMax`, `PackedDecode::endIngest`); the README of `patches/` says so.
- `jumptable.cc` (A6-18) and the `TypeFactory` re-inserts are listed, not fixed.
- Windows and macOS: the matrix forks, and pin identity needs glibc's `backtrace`; elsewhere pins fall back to coordinates. The MSVC build of the patched files was not run.

## Area 5, round 5: registers through pointers, misaligned on-chip accesses, what follows an address error

Fixer, branch `review/5-round5-fix` from master `ec7751d`; fix commit `9d794e3`, tests `427af9e`. Ledger rows A5-28 to A5-32 are fixed; the
reviewer's questions added A5-33 and A5-34 (fixed) and A5-35 and A5-36 (accepted). The work is in `SaturnHardwareMap.{h,cpp}`, inside
`HardwareNotes` in `Sh2Decompiler.cpp`, in `derive_handler_cases.py` and `HardwareHandlerCheck.h` (oracle), and in rule 3b of the generator.
No engine patch (0001 to 0031 unchanged; 0035 not needed). Every fixture of the five rows was reproduced first on the unfixed
code (a scratch harness linked against the master objects), then the oracle was widened and run against the unfixed map: **490
disagreements** (96 misaligned probes whose reached registers are wrong, 29 long accesses said to reach more than the handler reaches,
the cells of the same probes, IPRB and VCRD at +1), before any rule was touched.

### Root causes

| Row | Root cause | Fix |
|---|---|---|
| A5-28 (P2) | `HardwareNotes` looked only at the volatile ops the engine makes for a constant address. A pointer merged from register literals is not constant, so its load or store stays a plain LOAD or STORE (patches/0009), and the notes keyed on the op, not on what the output shows (`&REG`). The generator's rule 3b was written for the same blind spot and excused it. | `ConstantsOf` walks the pointer varnode through COPY, CAST, MULTIEQUAL, `PTRSUB(0, address)` (how the engine writes `&REG`) and, as bases, PTRADD / INT_ADD; a bare number is never a pointer to a register (the first version counted `param_1 + -0xc`, 0xFFFFFFF4, as an index from SH2_RTCNT: three false cautions in hwram008, found by comparing every output of both images with master's, fixture `a5_28_rawconst`), so only the `PTRSUB` form counts, filtered by `PrintsAsRegister` (a register named at its symbol's address); each is run through the same per-access code as a direct access (`account`: cache caution, read effects, width cautions, write effects), with the text prefixed "if the pointer holds 0x... (REG)" (and " (it may hold other values too)" when a path is not a constant). A pointer indexed from a register is said for the register as "if the index is 0 (the pointer is based at ...)". A pointer access does not count as a write for the split-read note. Rule 3b: an indirect read the emulator traced at a cached register whose register name appears as `&REG` in the output must be said (`cache.read.pointer.said` / `...unsaid`); only a pointer that names no register is excused (`cache.read.anonymous`). |
| A5-29 (P3) | `AccessAddress` kept every partition-7 address as given. True below `0xFFFFFF00` only; above it the handlers align the address themselves (`A &= 0xFE` / `0xFC`); below it, the table's aligned cells were used for an address the handler decodes as given. | `AccessAddress`: aligned down from `0xFFFFFF00` up, as given below. `HardwareRegistersReached` / `CoveredRegisters`: below it a word at an odd address and every long reach the register at their own address only (a long read none). `WidthCellFor`: a word at an odd 8-bit register's own address is `EvenByte*` (it was `OddByte*`), at the watchdog's odd registers `KeyedByteWrite`. The misaligned caution says which half it is. |
| A5-30 (P3) | The cases file kept one flag per group of labels; the lane mask applied to one label (`& mask & 0xFF00`) was not in it. | The parser evaluates the branch's `mask` / `shift` statements per label and lists a label the mask kills as its own inert group; the table has `ReservedByteWrite` for IPRB and VCRD at +1 (a read there returns 0). |
| A5-31 (P3) | The address-error cautions were generated per register from the cell, and a cell says "address error", not what the handler then does. | Byte accesses at `0xFFFFFF00` and up: the text of the cell says what Mednafen then does (`AfterByteError`: a write stores `V | V << 8` and does the register's write effect, a division; a read returns bits 15-8; the bus state controller ignores a byte write; the UBC has no handler). Longs below `0xFFFFFF00`: one caution for the access (`NarrowAccessEffects`), on the register at its own address, saying what is stored (bits 7-0, or bits 15-8 into a 16-bit register's high byte, nothing at an odd address or at the watchdog) and which registers under its other bytes are not written; a read says it returns 0 and reads none. `WriteEffects` lets a long write to CCR purge (it did for byte and word only). |
| A5-32 (P3) | The check read `F` (a write stores a constant whatever is written) for the SMPC only; `WriteEffects` knew PTMR and CCR only. | `WriteEffects`: VDP1_ENDR (any write, at any width reaching it), SCU_DnEN (bits 8 and 0, start factor 7; only the lane that carries bit 0 counts, a byte at +3, a word at +2, a long), SCU_AIACK and SCU_DSTP (bit 0), SCU_T1MD (bit 0 clear resets timer 0). The check requires a text for every register whose handler has `F`, in `scu.wr32` and `vdp1.wr` (PPAF is excused by its `split` text; the SCSP common block's `F` is bookkeeping and stays unchecked). |
| A5-33 | A store to the BIOS ROM was ordinary memory: the ROM takes no write. | A caution made from the engine's access record (fate "folded into ordinary memory", a store, a constant address in `0x00000000`-`0x000FFFFF` through either partition). |
| A5-34 | An access to the cache address array was volatile but said nothing. | A note (entry, way, what a write and a read do). |

### The reviewer's questions, decided

- **BIOS ROM writes folded as ordinary memory.** Area 5 (the bus map says what is not RAM; area 2 is the semantics of instructions). A defect in the shape
  (`mov.b 5,@ROM ; mov.b @ROM,r0` returns 5), none in either corpus (no store and no folded load in the range, measured with the
  engine's access records: 839 and 1,554 functions). The range cannot be made volatile, since a decompiled BIOS function reads its literal pool and its tables from it and they must fold;
  so it is said, not corrected: A5-33, fixed by a caution.
- **Cache arrays as ordinary memory.** The address array (`0x6xxxxxxx`) is already volatile, because it lies in `0x40000000`-`0x7FFFFFFF`; it only said nothing: A5-34, a note. The data array (`0x8xxxxxxx`, `0xCxxxxxxx`) is ordinary memory, which is exact when the
  cache is used as RAM and wrong when a cache fill overwrites it, which depends on CCR: A5-35, accepted, no instance in either corpus.
- **SCU byte lanes judged per register, not per lane.** The width table's SCU cells stay per register (value blind spot, as the reviewer said; the bits that reach a
  register are what the check cannot see). The new write effects are per lane, where the effect depends on it (a word at +0 or a byte at +0 carries neither bit of DnEN).
- **`0x25FE0000` printed as `&SCU_D0R`.** Cosmetic (a number that is a register's address); no row. With A5-28 it says nothing as a pointer (only loads and stores are followed).

### Mutation checks

Each entry below was broken in a scratch copy of the source, the three test binaries relinked against it, and the width table test, the decompiler suite and (A5-28) the
generator on the ledger's words run; "handler" is the number of disagreements with Mednafen's handlers, "golden" whether the golden file or the audit's rules table fails.

| Entry broken | Handler | Golden | Fixture CHECKs | Generator |
|---|---|---|---|---|
| no notes for pointer accesses (the unfixed A5-28) | 0 | passes | 9 | 1 finding on the ledger's words (`SE_GENERATED_WORDS`; 0 before) |
| a bare number counts as a pointer to a register (`param_1 + -0xc`) | 0 | passes | 1 | |
| `PrintsAsRegister` accepts any hardware-range address | 0 | passes | 0 (equivalent: the engine's `&REG` form exists only at a symbol's address, and a bare number is ignored before it) | |
| no indexed bases | 0 | passes | 2 | |
| a merged pointer treated as certain (no "if the pointer holds") | 0 | passes | 8 | |
| no "it may hold other values too" | 0 | passes | 1 | |
| a pointer write counts for the split-read note | 0 | passes | 1 | |
| no BIOS ROM caution | 0 | passes | 2 | |
| on-chip address as given at every half (round 4's rule, A5-29) | 290 | passes | 7 | |
| below `0xFFFFFF00` the access reaches every register under it | 166 | passes | 2 | |
| a long reaches the registers under all four bytes | 131 | passes | 1 | |
| no `ReservedByteWrite` | 2 | fails | 2 | |
| a word at an odd register's own address is `OddByte*` | 20 | passes | 5 | |
| a word at WTCNT's address is the key port | 4 | passes | 0 (HardwareNotes' own keyed caution says it; only the check sees the cell) | |
| no text after a byte address error | 0 | passes | 3 | |
| a long read not said to return 0 | 0 | passes | 1 | |
| a long write's "writes only REG" text removed | 0 | fails | 4 | |
| a long write to CCR does not purge | 1 | passes | 1 | |
| no VDP1_ENDR note | 1 | passes | 2 | |
| no SCU_DnEN note | 3 | passes | 5 | |
| DnEN note without bit 8 | 0 | passes | 1 | |
| SCU lane ignored (DnEN, AIACK, DSTP, T1MD) | 0 | passes | 2 | |
| no SCU_AIACK note | 1 | passes | 1 | |
| no SCU_DSTP note | 1 | passes | 1 | |
| no SCU_T1MD note | 1 | passes | 1 | |
| no address array note | 0 | passes | 1 | |
| the old cases file (no per-label masks) with the new table | 4 | passes | 0 | |
| the widened check against the unfixed map | 490 | | | |

Every mutation is caught by at least one of them; the generator's rule 3b is the only witness for nothing but the A5-28 row (the ledger's words, which the
round 4 rule accepted).

### Matrix

| | |
|---|---|
| Golden file | regenerated after the rules were right (`SE_UPDATE_GOLDEN=1`) and read: 337 registers, 3,038 cells, rules 32 -> 33 (`ReservedByteWrite`); the only cell lines that moved are IPRB `w1@1` and VCRD `w1@1` (`Plain` -> `ReservedByteWrite`); the audit's rules table follows (`Plain` 811 -> 809, class d 164 -> 166) |
| Handler check | 0 disagreements: 952 on-chip cells (the table's 526 and 426 from the misaligned probes), 490 SCU cells, 228 block registers, 1,792 aligned and 1,280 misaligned on-chip address probes, 2,597 bus probes, 8 write-effect registers; `MednafenHandlerCases.txt` 3 lines changed, 1 added (`bus.onchipbyte`); `derive_handler_cases.py --check` current |
| `ctest` | 96 of 96 passed (80 s with -j2). `SaturnExplorerLiveExporterRobustnessTests` failed once under -j3 load ("silent client connected", a socket timing check) and passes alone and in the -j2 run; no decompiler code is involved |
| Game corpus, `hwram007.bin` | 839/839; every count 0; 16 missing and said; records 14,843 folded, 0 dead, 16 unreachable, 27 switch lookups; notes 12 -> 14, cautions 16 -> 16 |
| Game corpus, `hwram008.bin` | 1,554/1,554; every count 0; 20 missing and said; records 38,701 folded, 0 dead, 20 unreachable, 155 switch lookups; notes 27 -> 29, cautions 20 -> 20 |
| Corpus text that changed (master's output beside the new one's, every function of both images compared, pseudocode, warnings, cautions and notes) | the pseudocode, the warnings and the cautions of all 839 and 1,554 outputs are byte-identical; four note lines appeared, each read: `VDP1_ENDR = 0;` at `hwram007` 060056E6 (0600571E) and `hwram008` 0600572A (06005762): "VDP1_ENDR: any write ends the drawing in progress, whatever is written"; `(&SCU_D0EN)[param_1 * 8] = 0x101;` at 0601B7B4 (0601B86E) and 0605A964 (0605AA1E): "if the index is 0 (the pointer is based at 0x25FE0010 (SCU_D0EN)): SCU_D0EN: bits 8 and 0 are set, so this write starts level-0 DMA when its start factor (SCU_D0MD bits 2-0) is 7 (start by this register)". (A first version also printed three cautions in `hwram008` at 0604A60A, 0604A61A, 0604ABDA, 0604ABE6: `*(int2 *)(param_1 + -0xc)` read as an index from SH2_RTCNT; fixed, `a5_28_rawconst`.) No register read through a merged pointer in either image: A5-28 has no corpus instance, as the review said |
| Generator | `SE_GENERATED_SEEDS=3000` `SE_GENERATED_GRAMMAR=2`: 0 findings; cached-partition register reads said 173 direct and 39 through a pointer the output names as `&REG`, 33 through a pointer that names none (excused); the ledger's A5-28 words: 0 findings (and 1 with the pointer notes switched off) |
| ASan + UBSan + LSan, gcc Debug | `Sh2DecompilerTests`, `HardwareWidthTableTests` and `Sh2GeneratedTests` (300 seeds), `detect_leaks=1`: no AddressSanitizer or LeakSanitizer report; the only UBSan lines are the two tolerated sites (`address.hh:605`, 2; `slghpatexpress.cc:979`, 5); all pass |


### Not done

- A pointer indexed from a register is said for the register itself only (A5-36): which register the index selects is not worked out (SCU_D1EN, SCU_D2EN at +0x20, +0x40).
- A store to the BIOS ROM is cautioned, the later read is not corrected (A5-33); the cache data array stays ordinary memory (A5-35).
- The cache caution still cannot know CCR.CE is clear for a function; SCI / UBC stay claimed from the manual.
- Value-independent write effects of the SCSP common block (`F`) are not said (bookkeeping, not an effect a note would help with).
- The bits of the value that reach a register, the keys, the FIFO pops and the text of every caution remain with the hand fixtures and the golden file.
- The slave CPU's view (one map for both CPUs) was not probed.

## Areas 3 and 4, round 4

Fixer, branch `review/3-4-round4-fix`, from master `4210e8e`. Commits: oracle `4f67521`, `c16f914`; A3-7 `dd90913`; A3-9 `9f9318d`; A3-8 and A3-10
accepted and pinned `7a61e6f`; A3-12 `e4274b0`; A4-8 and A4-9 `e1f6b4c`. Ledger rows A3-7, A3-9, A3-11, A3-12, A4-8 and A4-9 are fixed; A3-8 and A3-10
are accepted with their reasons. No engine patch (0036 was not needed). Every fixture was reproduced first, against the master binary: the three
A3-7 shapes (the oracle accepted all three, the generator rejected them), A3-9 (the emulator returns 12, the text said `return 10;`), the A3-8
replay (0 findings), seeds 301364 and 305201 (`generator:no-return`, `emulator-error`), and the A3-11 texts (each wrong verdict of the row).

### The oracle first (A3-11)

`PseudoEval` was a 32-bit signed interpreter that knew no declaration. It types its values now: a parameter or local has the width and sign its
declaration gives it and every assignment converts to it (a `char` parameter is the register's low byte, signed); a cast truncates or extends;
`<`, `>`, `/`, `%` and `>>` read their operands unsigned when the C rules say so (a literal over `0x7FFFFFFF` is a `uint4`); arithmetic is 64-bit
when an operand is 8 bytes (`dmuls.l`'s `(int4)((uint8)((int8)b * (int8)a) >> 0x20)`); a parameter assigned to holds the new value, under an `if` too;
`true` and `false` are 1 and 0; `'\x02'` is a character constant, in an expression and as a case label (matched modulo the selector's width);
`CONCATnm`, `SUBnm`, `ZEXTnm`, `SEXTnm`, `CARRYn`, `SCARRYn` and `SBORROWn` are evaluated. A type with no sign (`xunknown4`, `undefined4`) is read both ways
and is unknown when the two disagree, which agrees with any run: the evaluator never states a verdict it cannot support. What it cannot read is an
**error**, which fails the check, not an unknown value: an operator of the `NAME4` shape it has no rule for, a CONCAT wider than 8 bytes, a 64-bit
return (r0 alone does not hold it), `x += 1` and `x++`. A call may write a stack local (`xStack_c`, `iStack_8`: the engine keeps one a call could
reach, A3-9), so those are unknown after a call; register locals (`xVar1`) are not.

`MayReturn` and `PseudoAgreesWithRun` no longer accept `return;` for any value: a function that returns nothing agrees with a run only if the fixture says
r0 is no result (`CallFixture::noResult`, default false). The fixture is the proof: one existing fixture needed it (the r2 and r3 "kept by the callee"
callers, which end `mov.l rN,@r4`; the emulator returns r0's starting value, 0x1000, unchanged). Every other existing fixture passed unchanged. The verdicts
of the row are pinned: the wrong accepts (bare return, `false`, reassigned parameter, `(char)` width, `CONCAT31`, `SUB41`) as texts, the wrong rejects
(parameter reassigned under an `if`, `shlr` on a `uint4`, `cmp/hs` and `cmp/hi`, the `dmuls.l` high word) as code run on the emulator against the
engine's real output, the `char` case label as text. A weakness of the first version of those tests was found by mutation: the reason string was
appended across checks, so a check on "cannot evaluate" passed on an earlier check's text; it is cleared before each now (`c16f914`).

### The rows in the host

- **A3-7: the rule asked only paths with no call.** `State::called` was set at the join, so the call on one path hid the other, and the call
  itself hid everything: `bsr g ; ... ; rts ; mov r8,r0` (g lent, keeps r8) is a function whose return is a register no signature passes,
  and the engine drops the copy as it drops the leaf's. The reason `called` existed is real: a call's outputs share one set of origins (everything any
  of the callee's registers can come from), so r0 after a call has the callee's other registers among its origins; that is no read of an input.
  Replaced by the distinction itself: a `Value` carries `direct`, the origins computed by this function's own instructions, and a call's outputs
  have `direct = 0`. `Return` asks r0's `direct` origins on every path, with or without a call, exact copies (`mov r8,r0`, `sts macl,r0`, `stc gbr,r0`) included.
  The three shapes of the row and a fourth (`mov r9,r0` before the call, the callee keeps r0) are withheld ("its return is computed from r8 on entry")
  and the text is still the engine's `return;`, which the oracle now rejects. Controls: a function that saves r8, calls and returns the callee's value, and
  one that returns a parameter after the call, are lent.
- **A3-9: the exact address only.** `Value` keeps, for an address made of a stack address plus anything, the offset it was made from (`stackBase`); a
  constant added later moves it, an index leaves it (taken as non-negative, as the engine's own pointer arithmetic is), a subtraction of
  anything but a constant drops it, and a join keeps the lower. `NoteFrameEscape` declares that offset. The alias is "every local from there up", so
  the index and its bound do not matter. The row's fixture (index `r4 & 4`) and a second with the parameter added whole are no longer folded: the
  text is `return iStack_c + iStack_8;` with both stores before the call (the engine does not make an array of two scalars; patch 0030 declares the alias
  and the call is guarded, which is what prevents the fold). The round 3 first version, the whole frame for an inexact address, printed
  `axStack_80000000 [2147483580]` because it had lost the base; the base is kept now, so the declared offset is a real frame offset.
- **A3-10: accepted.** The corpus decides: a frame address is in r3 at a call to an unlent callee at 36 (007) and 53 (008) call sites, in r2 at
  39 and 74, in r14 at 852 and 2,068 (the frame pointer). GCC's SH static chain is r3, and is the only compiler convention that puts a frame pointer
  there (nested functions); Hitachi's and SN's compilers have none, and r2 is the one hidden pointer of the ABI. A rule over r3 would alias the
  locals of those 36 and 53 functions for a convention nothing in the corpus uses. Pinned in `TestCallsAndTraps` (the run returns 7, the text 5, the
  signature is withheld).
- **A3-8: accepted.** The exception is for machine state; the T flag the caller set is the only way a lent getter's result depends on the
  caller. Compiled code cannot do it (T is caller-saved), the 20 getters of image 007 are mask helpers whose callers mask the I bits, and the
  alternative (a `readsSr` bit in `CallEffects`, in `ShapeOf`, in two digests, and withholding every caller that has set T before such a call)
  buys nothing for compiled code and costs the callers that merely compared something earlier. Pinned in `TestCallsAndTraps`: the run depends on
  r6 and r7 (1 for r6 = r7, 0 otherwise), the caller is lent with no parameters. The generator reports the replay by hand (A3-12); its grammar does not
  emit the shape.
- **A4-8: the flow boundary set.** `entries` for "flow runs into" came from `knownFunctions`, names and call targets; it takes the lent prototypes'
  addresses too (host side, `Sh2Decompiler.cpp`; the panel's request assembly is untouched, it already passes `prototypes`). A mirror address is
  canonicalised; a prototype that is not known lends nothing and adds no entry; one outside the body adds no warning.
- **A4-9: the generator's contract.** Two causes, one fix. A call's arguments are drawn before the jsr's target register is chosen, so a `mov r1,r4` in
  the delay slot copied the callee's address (seed 301364: the callee stored through it over the caller's next literal); and a `mov #imm,Rk` reached a callee
  that stores or loads through Rk (seed 305201: address 0x66). Both are replaced after the draws (no draw is added, so every other program of a seed is
  unchanged: 11,706 functions in the window as before) by the caller's own copy of its entry argument register, a pointer into scratch memory;
  the second only where the callee, or anything it calls, jumps to or runs into, uses that register number as a memory or poll base.

### The generator (A3-12)

Check 8 now also starts each of r4-r7 at the value of another (r_k := r_j) as well as +0x200 and 0: the four start at distinct scratch pointers, so a
compare of two of them always had the same answer. The A3-8 replay, 0 findings on master, now reports `param-dropped:r6` and `r7`. The SR-getter
shape was not added to the grammar: it would be a finding on every module that has it, A3-8 being accepted.

### Mutation

Each reverted alone, the source restored after; failing checks of `Sh2DecompilerTests` unless stated.

| Reverted | Result |
|---|---|
| bare `return;` accepted (A3-11) | 8 |
| `true`/`false` unknown names | 2 |
| assignments to a parameter ignored | 3 |
| casts keep their value's width | 16 |
| `CONCAT`/`SUB` read as unknown calls | 4 |
| `>>` always arithmetic | 2 |
| compares and `/` `%` always signed | 3, and 2 |
| arithmetic 32-bit only | 2 |
| `char` case labels unread | 2 |
| narrow parameters fed whole | 10 |
| 64-bit return taken for unknown | 1 |
| `x += 1` skipped | 1 (0 before the reason was cleared between checks) |
| a narrow return compared at full width | 1 |
| `PseudoAgreesWithRun` accepts a bare return for every fixture | 5 |
| stack locals kept across a call | 3 |
| A3-7 rule off / exact copies ignored | 27 / 27 (the round 3 tests included) |
| A3-7 call outputs counted as direct | suite 0; image 007 lends 545, not 611 (66 withheld "computed from r1/r2/r3/r8/r14") |
| A3-9 exact address only | 4 |
| A3-10 pin: r3 declared like r2 | 1 |
| A4-8 prototypes not entries | 2 |
| A3-12 no equal-argument values | the A3-8 replay passes (0 findings) |
| A4-9 arguments unchecked | seeds 301364 and 305201 fail |

### Matrix

- Whole `ctest` (RelWithDebInfo, gcc, `-DSE_ENABLE_DECOMPILER=ON`): 96 of 96 pass.
- Generator: the default run (300 seeds, both grammars) 0 failing, 0 findings; grammar 2, `SE_GENERATED_FIRST=300001`, 6,000 seeds: 0 failing, 0 findings
  (11,706 functions, 1,637 tables, 1,020 parameter candidates over 20,555 perturbed runs, none sensitive; seeds 301364 and 305201 pass); grammar 2 from seed 1,
  3,000 seeds: 0 failing, 0 findings.
- Game corpus, both images, standalone and with `SE_GAME_LEND=1`: below.
- gcc Debug with `-fsanitize=address,undefined`, `detect_leaks=1` (`.github/scripts/run-decompiler-sanitizers.sh`): `Sh2DecompilerTests`, `Sh2DecompilerEngineTests`,
  `Sh2DecompilerFixtureTests`, `DecompilerPanelTests`, `DecompilerRunnerTests` and `Sh2GeneratedTests` (300 seeds) exit 0; `check-sanitizer-logs.sh` passes: only the
  allow-listed `address.hh:605` and `slghpatexpress.cc:979` lines.
- Timing: `TestGameCode` image 008, 22.6 s (master) and 25.7 s (this branch) wall on a loaded machine; image 007 6.6 and 7.3 s. The oracle is test code; the host
  change is one `Value` field more in `ReturnCheck`, which runs once per function with a call.

### Corpus

Standalone (`TestGameCode`, nothing lent) and lending (`SE_GAME_LEND=1`, callees first) on `hwram007.bin` and `hwram008.bin`, master `4210e8e` against this branch:
every ledger count 0, dropped accesses 0, 16 and 20 reads missing and said (unchanged); signatures lent 611 and 1,007 before and after (767 and 1,220
parameters), 156 and 299 lent functions that change r8-r14, GBR or VBR as before, text changed by lending 268 and 528 as before. Function texts that
differ from master: 0 of 839 (007) and 3 of 1,554 (008), standalone and lent alike, all by A3-9's base: `06038822` and `0603885E` gain an unused declaration
`xunknown1 axStack_23c [568];` (r2 = r15 + index at a call whose callee is not lent: the alias now starts at the frame base), `06035636` splits its
array (`aiStack_5c [4]` becomes `[3]` and `xStack_50`, the fourth element passed to a call). Every one read: the text states what the code does, none
states a folded value that the code does not return.

### Limits, and what is not done

- A3-8 and A3-10 are limits of hand-written code, pinned. The generator's grammar has no SR-getter shape.
- A3-9 takes an index as non-negative. A negative index from a frame address reaches below the declared base; the engine's own pointer arithmetic assumes the same.
- A3-9's text for two scalar locals is two scalars (no array); only the fold is gone. An engine change to merge them into an array would be patch 0036.
- The evaluator reads a call as unknown and stack locals as memory after a call: it cannot tell a call that writes a local from one that does not.
- The generator's pointer rule follows register numbers, not values: a callee that moves its r4 into r6 for a callee of its own which dereferences r6 is not seen (not found in 9,000 seeds).
- Not run under the sanitizers: `Sh2DecodeTests`, `SlaFreshness`.

## Areas 2 and 3, integration

Fixer, branch `review/2-3-integration`, from the merge `c358822` of `review/2-round3-fix` (patches 0033, 0034; the printed-C oracle and the
wide opcode alphabet) and `review/3-round3-fix` (the lent effects that kill, rule (a)/(b), `NoteFrameEscape`, patch 0030; the conventions
grammar). Two branches built in parallel from different bases; the merge compiled and passed its own halves but not each other's tests. A
second merge, `review/3-4-round4-fix` (areas 3/4 round 4), was brought in after the fixes below and merged without a source conflict (the
two docs by `merge_docs.py`). No patch, no ledger row changed disposition; no engine file touched. The host change is in `Sh2Decompiler.cpp`
only (below).

### What interacted

1. **The wide alphabet changes the registers the conventions grammar thought only it changed.** `mul.l`, `muls.w`, `mulu.w`, `dmuls.l`,
   `dmulu.l`, `clrmac`, `lds mach/macl`, `ldc gbr/vbr` change MACH, MACL, GBR or VBR for good, and the self-check
   (`generator:state-saved`, `generator:callee-saved`) compares those at the return with `Func::clobbers`, which only the conventions items
   filled in: 288 failing seeds of 3,000 (grammar 2), `state-saved` 281 + 49 findings. And the `@(disp,GBR)` items read GBR on purpose,
   which `Func::reads` did not say: `kept-read:gbr` (9 findings): a premise of check 12 (the generator's functions read GBR only on purpose),
   not a decompiler fault. `RunFunction` set GBR to one scratch address for every mode, which the area 3 checks (a function that loads it with
   what it already was) cannot tell from a change.
2. **`Write_SR/GBR/VBR` keep the read of what an `ldc` loads from.** Before, the engine dropped an `ldc` nothing read, and its source with it. Now
   the source is an input, and rule (a) (inputs outside r4-r7 withhold) sees it:
   - `rts ; ldc r1,gbr` (the ledger's A3-3 b) is a callee that reads r1, an input no signature passes: it is withheld (correctly, A3-5), and so is
     its caller. The fixture expected it lent with GBR killed because the engine used to drop the read.
   - Every function that saves GBR or SR and puts it back, or changes some of SR's bits and keeps the rest, reads the register on entry: 5 more
     withheld by "reads sr on entry" and 3 more by "reads gbr on entry" on image 007 than for area 3 alone, and `A2-13 critical section` (the
     compiler's interrupt-mask idiom) from two parameters to withheld.

### What changed

- **Generator** (`Sh2GeneratedTests.cpp`): `StateWritten`/`StateRead` say which state registers an op of `kRegOps` changes and reads; `MakeWideAlu` and the
  GBR memory items add them to `wideClobbers`/`wideReads`, which `MakeFunc` ORs into `Func::clobbers`, `effClobbers` and `reads` (so a caller that
  calls such a function observes the change, check 12's premise holds, and replay lines carry the masks). GBR starts at `kScratchGbr + 0x20 * mode`:
  scratch memory for the GBR accesses, a value of its own per mode for checks 10 and 12. A new exemption in the dropped-access check: a call to an
  unlent function that changes r8-r14, GBR or VBR leaves them kept in the engine's model, so a branch on such a register is folded and the code it
  guards can be removed as unreachable (seed 301832 of the 300001 window: a conventions callee withheld for a r1 read changes r12, the caller
  branches on its restored r12). That is A3-3's limit for an unlent callee, not a flow finding; counted as `dropped.after-unlent-changer` (12 in
  6,000 seeds).
- **Host** (`Sh2Decompiler.cpp`, `WithholdReason` only; `ReturnCheck`, `PseudoEval` untouched, so the round 4 merge is textual): `EntryStateUse` follows the
  entry value of SR, GBR or VBR through copies, phis and any operation; it is machine state, not an input, when every path ends in a `Write_<REG>` of
  the same register (GBR saved and restored; SR read-modify-write), or, for SR only, is returned or stored as the whole value or the whole with bits set
  (`or` with a constant: the critical section returns `in_sr | 0xf0`). A branch, a compare, a load address, a call argument, or a field of it (`and`)
  returned or stored makes it an input. The same test is applied to what a call left in SR/GBR/VBR (`Write_SR(extraout_sr & 0xf0 | ...)`, the mask
  raised round a call), which the "register read after a call" rule would otherwise withhold. The old test looked only at the direct uses of the
  input and let any copy through, so a function that copies SR and tests its S bit (`uVar8 = in_sr; ... (uVar8 >> 1 & 1)`, the saturating
  multiply-accumulate at `0602671C` and `060663BC`) was lent with a hidden input; it is withheld now.
- **Tests** (`Sh2DecompilerTests.cpp`): the GBR-from-r1 fixture pins the callee and caller withheld, the oracle not asked (the text is the model's, as for
  any unlent callee that changes a register it keeps); new fixtures for GBR/VBR set and restored (lent, GBR preserved), GBR advanced (lent, killed),
  GBR returned or used as an address (withheld), SR raised and returned with no write (withheld), SR restored and its T or a field returned (withheld),
  and the mask raised after a call that sets T (lent).

### Decisions

- The ledger's A3-3 b fixture is no longer a case the fix can reach: the callee is withheld, the caller is withheld, and the caller's text keeps GBR as
  the model has it (`return param_1;`, the run returns r1). Nothing incomplete is lent, which is the rule; the text of a caller of an unlent callee is
  the model's, a limit already pinned for r8 and by A3-6. The same load from a parameter (`ldc r5,gbr`) is lent with GBR killed (the loop above it).
  Carrying the callee's effects while withholding its signature was not done.
- The critical section is lent returning `in_sr | 0xf0`: r0 holds the raised mask on exit, and the engine keeps it as the return. The rule lends it because
  every use of the entry SR is a write back or the whole with bits set; `stc sr,r0 ; or #0xF0,r0 ; rts` with no write is withheld, and so is anything
  that returns a field of SR (`and #3`).

### Mutation

Each change reverted alone, `Sh2DecompilerTests` rebuilt (the generator for its two rows):

| Reverted | Fails |
|---|---|
| `Write_SR` not counted as a write back | 6 checks: the critical section, the mask after a call, GBR/VBR restored, GBR advanced |
| GBR and VBR not treated as write-back registers | 4 (GBR, VBR restore; GBR advanced) |
| an `and` result may be returned or stored | 2 (T returned after a restore, field returned) |
| an `or` with a constant is a field | 1 (the critical section) |
| a transformed SR need not reach a write | 1 (SR raised and returned) |
| the call's SR/GBR/VBR exemption removed | 1 (the mask after a call) |
| rule (a) off (the input scan skipped) | 26 incl. the new GBR-from-r1 callee |
| `ReturnCheck` without the `Write_*` condition (area 2's host line) | 20 |
| generator: wide state clobbers not registered | 24 of 300 seeds, 242 findings `state-saved` |
| generator: GBR items not marked as reads | 9 of 3,000 seeds, `kept-read:gbr` |
| generator: the unlent-changer exemption removed | seed 301832, 1 finding in the 300001 window |

### Matrix

Combined tree (this branch with `review/3-4-round4-fix` merged), RelWithDebInfo, gcc:

- Whole `ctest`: 97 of 97.
- Generator, grammar 2, 3,000 seeds: 0 failing, 0 findings (lent 4,475 of 5,941 functions: the GBR memory items withhold 976 as inputs, so area 3's
  checks see fewer lent functions than their 5,809: input candidates 341 against 425, check 12 260 against 292). Grammar 1, 1,000 seeds: 0.
  Window `SE_GENERATED_FIRST=300001 SE_GENERATED_SEEDS=6000`: 0 findings after the exemption above (one, seed 301832, before).
- Printed-C: default run in `ctest`; `SE_GENERATED_SEEDS=3000`: 3,017 functions, 0 findings (2,410 compared, 270 `void-return`, 268 skipped calls).
- Corpus, standalone, both images: every ledger count 0, 16 and 20 missing and said, 839 and 1,554 functions ok.
- Lending (`SE_GAME_LEND=1`): 007 610 lent (area 3 alone, built here for the comparison: 611), 008 1,006 (1,007); area 3 reported 612 and 1,008 from
  its own base. Against area 3 alone the lent set differs by one function per image, and the cascade through it: `0602671C` / `060663BC` (the S-bit
  reader above, withheld "reads sr on entry"; its two callers `060266E8`, `06026708` / `06066388`, `060663A8` were withheld "a return not to its caller"
  and are now withheld on the callee) and `06003800` (withheld both ways; the `Write_SR` makes the flow take `06003840` in as its own code). Before
  `EntryStateUse` was applied to GBR, VBR and what a call left (SR only, direct uses), 600 and 989 were lent: 11 and 18 lost to the `Write_*` reads
  (SR and GBR read on entry, `uses sr after calling`, and their callers); the ones read are the mask or restore idiom and are lent again. Every
  function that changed signature was read.
- gcc Debug, ASan + UBSan, `detect_leaks=1`: `Sh2DecompilerTests` (all), `Sh2GeneratedTests` 150 seeds and the printed-C run exit 0; the only reports are
  `address.hh:605` and `slghpatexpress.cc:979`. Not run under the sanitizers this round: the other five suites of the CI list.

### Not done

- A callee withheld for a hidden input does not lend the effects it proved; its caller's text keeps the model's registers (above).
- `ldc.l @Rm+,gbr/vbr/sr` forms and the `rte` restore are exercised by the generator's alphabet only through `ldc Rm,...`.
- The generator's lent coverage fell (GBR items withhold their functions); a grammar draw that keeps GBR accesses to non-convention modules would
  restore area 3's counts if they are wanted.

## Area 5, round 6 review

Reviewer, branch `review/5-round6-review`, at `124a5eb` (master). Findings only; no source or test changed. Every fixture is in
`Docs/Decompiler/REVIEW_LEDGER.md` (A5-37 to A5-41) with its words, current and expected output and the Mednafen lines (in the ledger's code
quotes `or` stands for the C bar). Reproduced on a RelWithDebInfo gcc build with a scratch harness linked against the decompiler's objects (it
prints the lines, the ledger and every note and caution), with `SaturnExplorerSh2GeneratedTests` (`SE_GENERATED_WORDS`) for the generator's
verdict, with `SaturnExplorerHardwareWidthTableTests` and `derive_handler_cases.py --check`, and with the corpus (`SE_GAME_HWRAM`,
`SE_GAME_NOTES=1`, `SE_GAME_OUT` to read every output). Mednafen read from the review path (`mednafen/ss`): `sh7095.inc` (`MemRead`,
`MemWrite`, `SH7095_OnChipRegWrite_u16/u32`, `SH7095_OnChipRegRead_INLINE_u32`), `scu.inc` (`SCU_RegRW_DB_u8_W1`, `SCU_RegRW_DB_u32_W1`, the
timers, the 16-bit CS2 write), `vdp2.c` (`RegsRead`, `RW_R16`), `cdb.c` (`CDB_Write_DBM`), `scsp.inc` (`SS_SCSP_RW_u16_W1`).

**Area 5 is not clean: one P2, four P3.** The streak has not started. Round 5's pointer-following holds for the shape it was written for (a
pointer merged from register literals, accessed at `@Rn`); it does not hold for the SH-2's base-plus-displacement form, and its text does not
say when a pointer moves.

**Not held.**

- **A5-37 (P2).** A constant offset from a merged register pointer (`mov.l r0,@(16,r1)`, `mov.w @(4,r1),r0`, `add #-4,r1`) is taken as an
  unknown index: `ConstantsOf` keeps the base of a PTRADD or INT_ADD and drops the literal addend, so the notes are made for the base
  registers "if the index is 0", which the printed `puVar1[4]` shows never happens. `puVar1[1] = 0x101` from `&SCU_D0EN`/`&SCU_D1EN` (a store to
  D0MD/D1MD) is said to start level-0 and level-1 DMA; `puVar1[4] = 0x101` from `&SCU_D0R`/`&SCU_D1R` (a store to D0EN/D1EN, which does start
  it) says nothing; `puVar1[2]` from `&VDP2_TVMD`/`&VDP2_EXTEN` (a read of TVSTAT, which clears EXLTFG) is said to latch HCNT and VCNT. The
  generator counts that read `cache.read.anonymous` (it looks for `&TVSTAT`), and passes it because the base's cache caution is on the same
  instruction. P2 as A5-28: the same class through the more common addressing form.
- **A5-38 (P3).** A merged pointer whose values are a register's aliases (`0x25FE0110`, SCU_D0EN through `A & 0xFC`; `0x05F80204`, TVSTAT
  through `A & 0x1FE`) prints as numbers and is said nothing; the direct access says everything. Rule 3b excuses the cached poll as anonymous:
  the excuse is keyed on the printed name, not on whether the engine knew the address.
- **A5-39 (P3).** A pointer stepped in a loop (`mov.w @r1+,r0`) is said as "if the pointer holds 0x25F80004 (VDP2_TVSTAT)" without "(it may hold
  other values too)"; a store loop from `&SCU_D0R` whose fifth store reaches D0EN (a literal trip count) says nothing at all.
- **A5-40 (P3).** Two `F` registers the check passes and the output does not say: SCU_PPAF (exempted by a `split` text that is said only when
  the function reads PPAF back; both corpora write `SCU_PPAF = 0;` and `SCU_PPAF = param_1 | 0x8000;` with no note, at 06033D5A and 06074DEE)
  and CD_CR4 (a word write issues the command; `cd.wr`'s `F` is not read by the check). The SCSP common block's `F` (MVOL, RBL/RBP) is
  bookkeeping, as round 5 decided.
- **A5-41 (P3).** A long at 0xFFFFFEFE or 0xFFFFFEFD gets a caution about SH2_DVSR ("a 4-byte write to SH2_DVSR, a 32-bit register, is an address
  error ...: the registers at 0xFFFFFE00-0xFFFFFEFF take byte and word accesses only"): the outcome is right (lower-half handler, `default`,
  nothing written, 0 read), the register and the reason are not.

**Held**, one line each:

1. A5-28's shapes: a merged `@Rn` read (cache caution and TVSTAT's note per value), a merged write (ENDR's note, PTMR's not with 0), a merged
   byte write to VDP2 (the whole-register caution per value), a merge with a RAM literal ("(it may hold other values too)"), on-chip merges
   (CHCR0/CHCR1 arming per value; FTCSR's arming, TIER none), `a5_28_rawconst`.
2. A register pointer stored to work RAM and read back in the same function folds to a direct access (`SCU_D0EN = 0x101;` with the DMA note).
3. The misaligned edge above the split: a word at 0xFFFFFF01 (write: DVSR whole, upper half 0; read: DVSR's low 16 bits, `A &= 0xFE` then
   case 0x00), a long at 0xFFFFFF02 (DVSR), a word at 0xFFFFFEFF (no register; nothing). Below it, A5-41's text only.
4. The write effects against `SCU_RegRW_DB_u8_W1` (532-790) and `_u32_W1`: DnEN by lane (a byte at +2 carries Enable and never bit 0, so no
   start; at +3 bit 0 with the old Enable; a word at +0 neither), a variable value said conditionally; T1MD (`if(!Timer_Enable) Timer0_Counter = 0`,
   and the counter moves only while enabled, so the note is harmless when the timer is already off); AIACK and DSTP by lane.
5. The BIOS ROM caution: a byte store and a word read of it (`CONCAT11(5, ...)`) have the caution; a store through a merged pointer to the ROM
   is not folded, its read is printed as a read, and no caution is needed.
6. CD_CR4's byte write ("does not start the command", `mask == 0xFFFF` only) and its long write (two word writes, the effect twice).
7. The oracles as they stand: `SaturnExplorerHardwareWidthTableTests` passes (337 registers, 3,038 cells; 952 on-chip cells, 490 SCU cells,
   228 block registers, 1,792 + 1,280 on-chip probes, 2,597 bus probes, 8 write-effect registers); `derive_handler_cases.py --check`: current.
8. The corpus, every pointer-based hardware access read: the indexed SMPC OREG reads (`(&SMPC_OREG0)[(7 - uStack_14) * 2]`, 0601A0C8 / 06058CD4;
   no read effect, nothing to say), the indexed D0EN store (A5-36, accepted), SMPC register pointers stored to RAM and used later (060715B6,
   0601A0C8; anonymous, rightly), the SCSP DSP program area at 0x25B00BE2 (0602FE4C / 0606FA64; no register there). No merged register pointer
   and no displacement from one in either image: A5-37 to A5-39 have no corpus instance; A5-40's PPAF has two.

**Questions without a fixture (for the coordinator).**

- The SCSP slot registers (0x25B00000-0x25B003FF) are not in the map: a write to a slot's word 0 with bit 12 (KYONEX) keys every slot on or off
  (`scsp.inc` `SS_SCSP_RW_u16_W1`, `z->KeyExecute |= (bool)(*SRV & 0x1000)`). SCSP_SCIRE / SCSP_MCIRE clear pending interrupts by the bits
  written (`z->MCIPD &= ~(...)`) and say nothing, where SCU_IST has a note for its clear. In scope for the map, or not?
- A merged on-chip pointer used with a displacement prints its values as signed numbers (`iVar1 = -0x80; ... *(xunknown4 *)(iVar1 + 0xc) = 3;`,
  SH2_CHCR0 or SH2_CHCR1): no register is named, so A5-37's fix would not reach it either. A printing matter (the type) or this area's?
- Rule 3b counted 41 anonymous cached-partition reads in 3,000 grammar-2 seeds (28 said through a named pointer, 160 direct); how many are
  A5-37's displacement shape (a base named, the traced register not) was not classified.

**Could not probe:** the cache with CCR.OD/ID or two-way mode; the slave CPU's view; the generator's anonymous reads one by one.

**Runs.**

| | |
|---|---|
| `SaturnExplorerHardwareWidthTableTests` | passes (counts above) |
| `derive_handler_cases.py --check` | the cases file is current against the review path's sources |
| Generator replays of A5-37a-d and A5-38b | 0 findings each |
| Generator, `SE_GENERATED_SEEDS=3000` `SE_GENERATED_GRAMMAR=2` | 0 findings; `cache.read.said` 160, `cache.read.pointer.said` 28, `cache.read.anonymous` 41 |
| Game corpus, `hwram007.bin` | 839/839; every count 0; 16 missing and said; records 14,843 folded, 0 dead, 16 unreachable, 27 switch lookups; notes 14, cautions 16 (as round 5) |
| Game corpus, `hwram008.bin` | 1,554/1,554; every count 0; 20 missing and said; records 38,701 folded, 0 dead, 20 unreachable, 155 switch lookups; notes 29, cautions 20 (as round 5) |

## Area 3, round 5 review

Reviewer, branch `review/3-round5-review`, on master `124a5eb`. Findings only, no fixes. New ledger rows: A3-13 (P2), A3-14 (P2),
A3-15 (P2), A3-16 (P2), A3-17 (P3). No area 4 row.

The instrument was a scratch driver, not committed. It is a reader of fixtures in `Sh2DecompilerTests.cpp`: callees are decompiled first and
lent, then the caller is decompiled, run on the emulator with any SR, GBR or other register on entry, and checked with `MayReturn`. It also has
a mode that hands a text to `MayReturn` alone. The same driver was put on a build of `4210e8e` (before round 4 and the integration) to tell
regressions from old limits. The generator's replay (`SE_GENERATED_WORDS`/`FUNCS`/`RETURNS`) was run on every fixture. The corpus was run with
`SE_GAME_LEND=1` and `SE_GAME_OUT` on both images and both builds, with a temporary line that printed every lent prototype whose r15 delta is
not 0 and every function over 300 ms.

### What held

- **`EntryStateUse`.** Each of these shapes was withheld, and the run agreed with the text:
  - SR or'ed with a parameter and written back, its value also returned.
  - SR written back on one path and returned on the other.
  - SR saved with `stc sr,r1` (or `stc.l sr,@-r15`), a `tst #2,r0` made, SR restored, then `movt r0`. This returns the entry T: the
    engine keeps `return (uint1)(in_sr & 0x3f3) & 1`, and the test follows the `ldc`'s own `& 0x3f3` past the `Write_SR` to the return.
  - GBR used as a base on one path and restored on the other.
  - GBR advanced and stored.
  - `rts ; movt r0`, and `stc sr,r0 ; and #0xF0,r0` with and without a join to 0.
- **Round 4's frame address.** These were each right:
  - The A3-9 index scaled by `shll2` (`r15 + ((r4 & 1) << 2)`): `return iStack_c + iStack_8;`, 12 as run.
  - A loop moving the address up.
  - The frame address in r2 stored to a global by a withheld callee and written through by a lent one (`return xStack_8;`, 7).
  - The same in r4 to a lent callee.
- **Extrapop and the r15 kill.** A callee that pops its caller's word on one path only is withheld ("r15 changed by an amount that is not
  the same"), and so is its caller. A callee that pops and returns its caller's pushed word reads a stack input and is withheld.
- **The 8-byte return (A2-17).** A caller that returns the r1 of a lent `CONCAT44(0x11,param_2)` callee prints `(int4)xVar1`, which is
  right. A callee whose r1 is the caller's on one path gives up the r1 half and its caller is withheld on `extraout_r1`. No shape gave a
  4-byte caller a wrong value without an `extraout`.
- **A3-3 b (unreachable).** A caller of a withheld callee is withheld by the first test in `WithholdReason`, and it covers every call spec.
  The only other paths are:
  - a user op (`trapa` is an indirect call through VBR and is withheld; `sleep` and the `Write_*` ops change nothing);
  - a jump into the callee's body, which is then the caller's own code and its GBR write is in the caller's own text.

  No shape was found where a lent caller reads the model's GBR after a withheld callee.
- **`TestGameLending`.** Image 007 lends 610 signatures and image 008 lends 1,006 (611 and 1,007 at `4210e8e`), as the integration said.
  The ledger counts and the suite pass. The signature changed in 38 functions of each image. All of 007's were read; 008's 38
  are the same families, judged by their signature lines (the same mask setters, and `060663BC` with its callers `06066388` and `060663A8`):
  - 33 of 007's are set-the-interrupt-mask functions that now take their level, because patch 0034 keeps the `ldc rN,sr` source. They are
    `0601B328` `void f(uint4)` (`Write_SR(param_1 & 0x3f3)`), the `0601B2DC`/`0601B2E2` wrappers (`b328(param_1 << 4 | b338() & 0xffffff0f)`),
    and `0602CB58`, `06030530`, `06023C48`/`06023C4C`, and the like. `0602CF70` takes its level in r5 (as `0602F270` does, by its signature), which the
    disassembly confirms (`shll2 r5 ; shll2 r5 ; or r1,r5 ; ldc r5,sr`). Before, they were lent `void f(void)`, which dropped the
    parameter. Each is right.
  - `0602671C` is withheld on the S bit (right, as the integration says). Its callers `060266E8` and `06026708` are now withheld on it.
  - `06003800` is now withheld for an indirect call, after the `Write_SR` flow took `06003840` in.
  - `0601B358` is withheld both before and after. Its text now calls the BIOS's `SYS_CHGSCUIM` at `0x06000344` with its two arguments,
    where it had five guessed ones.

  The parameter count of the lent signatures rose from 767 to 800 (007) and from 1,220 to 1,253 (008) through those mask functions; the
  integration's record gives no parameter count. Neither image lends a negative r15 delta (154 and 295 positive ones, the mid-prologue
  entries). Neither image has "too many paths" among its reasons.
- **`PseudoEval`.** Each of these read right: a `(char)` cast in a condition, `CONCAT22` of two narrowed parameters, `(int)(char)` of a
  `uint1` parameter compared with -1, and a local assigned on one path only. `?:` is "cannot evaluate", an honest rejection.

### What failed

- **A3-13 (P2): r0 carried across a lent call.** `mov r4,r0 ; bsr g ; nop ; ... ; rts`, with g lent and keeping r0, makes the caller
  lent `void f(void)`. It returns r4, and the same goes for r5, a constant, or `r4 + r5`. The engine kills r0 at every call
  (`LendEffects` lends no r0). Rule (b) sees the value but asks only the registers that no signature passes. The generator reports
  `return-dropped`. This is A3-7's fifth shape with a parameter or a constant in place of r9.
- **A3-14 (P2): a join that the engine drops.** `phi(in_sr, in_sr | 0xf0)`, `phi(in_sr, in_sr & 0xf0)` and `phi(in_r0, in_r0 + 1)` each
  print `return;` and are lent `void f(void)`, which loses the r4 that chooses the path. Rule (b) leaves out r0's entry and any computed SR,
  and a join is not `Value::Reg(kSr)`. The generator reports `return-dropped` and `input-dropped:r0`. The SR shape is what C compiles from
  `get_cr()` under an `if`.
- **A3-15 (P2): a negative extrapop drops what was pushed.** `rts ; mov.l r5,@-r15` is lent `void f(void)` with spDelta -4. Its caller pops
  the word, returns it, and is lent with 0 parameters. The oracle accepts the uninitialised `xStack_8`; the generator reports
  `param-dropped:r5`. Round 3's suite fixture "r15 lowered by the callee" is this shape and pins the caller as lent.
- **A3-16 (P2): a stack address moved down by a loop never converges.** The join keeps the lower `stackBase`, so each pass lowers it.
  `ReturnCheck` runs to 200,000 steps (3.4 s for 11 instructions) and the function is withheld "too many paths"; at `4210e8e` the same leaf
  was lent in 7 ms. The tolerant probe never reaches the call after such a loop, so a frame address in r2 is not declared. The text says
  `return 10;` where the run returns 12 (it did at `4210e8e` too, without the delay). Neither corpus image has the shape.
- **A3-17 (P3): a negative or subtracted index.** r2 = frame + 4 + r4 with r4 = -4, and r2 = frame + 4 - r4, are folded (`return 5;`, the
  run returns 7). This is round 4's written limit, which has no ledger row yet. The function is withheld, so only the text is wrong.

### Oracles

- `PseudoAgreesWithRun` rejects A3-13, A3-14 and A3-17, and cannot read A3-16's `do` loop (it rejects, for that reason). It accepts
  A3-15, because an uninitialised stack local is unknown.
- The generator's replay rejects A3-13, A3-14 and A3-15. It passes A3-16's leaf (withheld, and no check sees a lost signature or the
  time). Its grammar emits none of the four shapes, so the nightly run cannot find them.
- `PseudoEval` reads `unaff_r8`, `in_r8`, `in_sr` and `extraout_rN` as unknown, although `CallRunResult` gives every register a known
  starting value (r_i = 0x1000 + 0x111 i, SR 0). A text that names the wrong entry register would be accepted. No engine text was found that
  does, so this is no row.

### Questions without a fixture

- Should rule (b) ask r0 at a return for anything this function put there (a parameter, a constant, its own entry value changed) whenever
  the engine's return is void? A3-13 and A3-14 are its two gaps. The cost on the corpus was not measured: it needs a host change, which this
  round does not make.
- C4's text (A3-16 a) prints the loop's pointer as `register0x0000003c = (BADSPACEBASE *)((int4)register0x0000003c + -4);`, which reads as
  r15 being lowered in the loop. The code lowers r1, a copy of the entry r15, and r15 is restored. Is that a printing question for area 2 or
  area 7? It is not raised as an area 3 row.

### Could not probe

- How many corpus functions A3-13 or A3-14 would withhold. This needs the rule changed, which a reviewer does not do.
- An interrupt between a callee's push and the caller's pop in the A3-15 shape. The emulator takes no interrupt there.

**Done criteria.** Area 3 has four P2 rows this round (A3-13 to A3-16), so it needs a fix round and then two consecutive clean review
rounds. Area 4 has no row this round.

## Area 2, round 4 review

Reviewer, branch `review/2-round4-review`, on master `124a5eb` (patches 0001 to 0034, the printed-C oracle, the wide alphabet,
`EntryStateUse`). Findings only, no fixes. New ledger rows: **A2-21 (P1)**, **A2-22 (P1)**, **A2-23 (P2)**, **A2-24 (P2)**,
**A2-25 (P3)**, **A2-26 (P2)**. A2-16 and A2-20 (open P3s) were context, not re-probed. Two P1s: area 2 is not clean this round.

### Where the oracle could not look

The printed-C differential's grammar is register-only: no stack, no memory, and no constant wider than `mov #imm`'s 8 bits. Both
P1s are outside it, and both pass the generator (it compares accesses, not values or return widths):

- **A2-21**, a constant division whose quotient is scaled. Compilers divide by constants with `dmulu.l`/`dmuls.l` and a 32-bit magic
  number from the literal pool, which the grammar cannot draw. A corpus of compiler-shaped divisions (Hacker's Delight magic numbers;
  `mov.l @(lit),r1 ; dmulu.l r1,r4 ; sts mach,r0`, then the shift or the "add" form `sub ; shlr ; add ; shlr...`, signed with `dmuls.l`,
  `shar` and the sign correction; d = 3..159, 641, 1000, 6700417, 0x7FFFFFFF; the quotient alone and times 2, 4, 8, 16), 1,610 functions
  through `SE_GENERATED_PRINTEDC_WORDS`: 1,603 agree (`param_1 / 7`, `param_1 % 7`, `param_1 / 0xe`, signed with Ghidra's
  `x / 7 + (x >> 0x1f) + (x < 0)` form), 7 differ, all unsigned add-form divisions scaled by exactly the power of two of their final shift
  (`x / 7 * 4`, `x / 19 * 16` ...). The shift pair becomes a mask, `RuleDivOpt` no longer sees a division, and `RuleDivTermAdd2`'s
  intermediate `(uint8)x * 0x124924925 >> 0x21`, whose product needs 65 bits, is printed. `RuleDivTermAdd` has the same overflow for
  `(mulh(x, c) >> d) + x`. Both rules are upstream's.
- **A2-22**, a byte or word put into a stacked longword and the longword loaded back: the part of r0 that is still the stored register is
  dropped from the return (1, 2 or 3 bytes are returned, or `void` when r0 is also copied to r1), and the signature is lent. Through a global
  or registers the same code prints right; offset 0 prints right. The printed-C check said `agrees` on it, because it compares a narrow
  return in its low bytes only (**A2-26**).

Checked in scratch with the oracle widened: requiring a 1- or 2-byte return to be r0's zero or sign extension, and giving GBR and VBR
non-zero entry values (the oracle uses 0 for both, so a text that dropped either would pass): 6,017 functions (seeds 1-3000, two each),
0 such returns and 0 differences. So nothing else is hidden in the register-only grammar by these two; the grammar's alphabet is the
gap.

### Memory operands, checked against the emulator by hand

About thirty texts read against the run (`SE_GENERATED_WORDS`, trace and text), big-endian throughout. Right: `mov.b @r4` with `extu.b`/
`exts.b`; `mov.w @r4+` with `extu.w ; swap.b`; two `mov.b @r4+` joined by `shll8 ; or` (`CONCAT11`); a 64-bit `subc` chain from memory
(the borrow `uVar2 < uVar2 - iVar3`, the reads in order); `tas.b` (T from the old byte, `| 0x80` written); `tst.b`, `xor.b` through
`@(r0,GBR)`; `mac.w` after `clrmac` (the saturation branch removed as unreachable, correctly: a 16x16 product cannot overflow 32 bits from
0), `mac.w` with S and a live MAC (both halves, `MACH |= 1` on saturation), `mac.l` with S (the 48-bit bounds, the low word 0 for the
negative bound); `ldc.l @r4+,gbr`; `stc.l sr` / `ldc r4,sr` / `ldc.l @r15+,sr` (`Write_SR(param_1 & 0x3f3) ; Write_SR(in_sr & 0x3f3)`);
stack reads of a stored register's byte 0, byte 1, the low byte, the low word (`(char)(param_1 >> 0x18)` ...); a global byte insert
(`CONCAT31`). Wrong: the stack inserts (A2-22). `t[x/7]` prints the A2-21 address. One reading question, counted as `narrow-c` and so
accepted with A2-19: `(int4)xStack_2` with `xunknown2 xStack_2` is a sign extension in the engine's reading, and nothing in the text says so.

### The rarely emitted instructions, by hand

- `rte`: its restore of SR and PC is dead at the return, so a handler that changes the stacked SR (a status in T) or PC prints as nothing
  (**A2-24**). With an ordinary body it prints the body; the A2-4 slot limit is still flagged.
- `trapa #0x20`: `(**(code **)(unaff_vbr + 0x80))()` then the caller's use of r0; the push of PC and SR is balanced (A2-9 holds).
- `bsrf r1` (target computed before the slot): the call is to the right address, its return used.
- `braf` with a slot that writes the branch register: the target is taken from the value before the slot (`func_0x06009008()`, the tail
  call the guide documents with its warning).
- Delay slots: `bf/s` with `sett` in the slot and `bt/s` with `clrt` (the branch decided on the T before the slot), a slot that writes
  r0 on the taken path, `bf/s` with `movt` in the slot: all agree with the run in the printed-C check.
- `cmp/str` on a register that is not a parameter: four byte inputs named `in_r3`, `in_r3_00` ..., none saying which byte (**A2-25**);
  the oracle skips the function.
- `EntryStateUse`: SR stored whole is state; stored and loaded back through a pointer the reload is no descendant of `in_sr`, and the
  function is lent without its T input (**A2-23**). Through the stack the store is folded and the function is withheld, as it should be.
- `div0s`/`div1`, `movt` after `addc`/`subc`/`div1`, `rotcl`/`rotcr` chains, `dmuls`/`dmulu` high words, `swap.b`/`xtrct`: in the
  register-only alphabet; 6,017 more functions (seeds 1-3000, two each, 32 inputs): 0 findings (4,954 agree, 2 `narrow-c`, 536
  `void-return`, 524 skipped calls, 1 skipped unknown input, which is A2-25).
- Not SH-2 instructions, so not probed: `pref`, `shad`/`shld` (SH-3 and later), and the integer division ops (SH-2A); division by zero and
  INT_MIN / -1 arise in no SH-2 p-code (the divide unit is a memory-mapped peripheral, area 5), and the engine only makes divisions by
  constants.

### The C helpers

Read against p-code: `SEXTab` shifts left by 64 - 8a and back arithmetically (right for 1 to 8); `ZEXTab`, `SUBab`, `CONCATab` mask
correctly; `CARRY4`, `SCARRY4`, `SBORROW4` are the p-code definitions. `CARRY8`, `SCARRY8`, `SBORROW8` and 2-byte forms are not defined:
a text that used one would be `not-compilable`, a failure, not a pass. A shift by 32 or more of a 4-byte value would be undefined in C;
none was printed in the 7,627 functions of this round.

### What held

- `SaturnExplorerSh2DecodeTests` with `SE_MEDNAFEN_SS` (word table, decode comparison, 260,000 cases, 0 differ, 1,780 the known A2-4)
  and `SaturnExplorerSh2SemanticsTests` pass.
- Patch 0034: `ldc`/`ldc.l` to SR mask with `0x3F3` before `Write_SR`, as Mednafen's `LDC`/`LDC_L` (`sh7095_ops.inc:1653-1690`); GBR and
  VBR unmasked; `ldc.l` reads then increments, as `LDC_L`.
- Patch 0033: no `unaff_<address>` in any text; the `addv` fixtures; 8-byte returns with both halves right.
- `EntryStateUse` for GBR and VBR (any store or load address or return withholds), for transformed SR (a field returned or branched on
  withholds) and through the stack.

### Questions (no fixture, or not area 2)

- `(int4)x` of an `xunknown2`: is the extension's sign meant to be readable from the text? The oracle counts it as `narrow-c`.
- The printed-C grammar would reach both P1s with a literal-pool constant item (`mov.l @(disp,PC)`) and a stack item (push, partial
  store, reload). Area 11's call.

### Not probed

Interrupts and the slot-illegal exception (A2-8); the game corpus (not in this container); the sanitizers; the generator's long run;
`SE_GENERATED_ALPHABET=specials`.

## Area 6, round 3 review

Reviewer: branch `review/6-round3-review`, on master `124a5eb`. No source changed. The probes ran from a scratch copy of
`DecompilerFaultTests.cpp` (`FaultProbe.cpp`), compiled against the objects of two scratch builds with gcc 13.3: the CI's Debug ASan + UBSan
configuration (with `-g1`) and RelWithDebInfo. New ledger rows: A6-19 to A6-24, one P2 and five P3. No P1 was found: across 30,730
sanitizer cells there is no crash, no use-after-free, no double free, and no UBSan line outside the allow-list.

**The probe.** It is the matrix plus the following:

- Three fixtures. `gotos` is an irreducible loop. `loopbrk` is a counted loop with an early return. `dag` is two conditionals that share
  both targets and are chosen by a third. The words are in A6-22.
- `SE_PROBE_STICKY`: every allocation from k on fails.
- `SE_PROBE_NAMELEN`: pads the request's name, to tune the markup's length.
- `--repeat`: damages the engine, then sends N refused requests and fails each of the first 64 allocations of a refused `Decompile` and
  `Disassemble`.
- `--diffruns`: compares the allocation sizes of the baseline with those of the following runs.

### What held

- **The default subset on master.** RelWithDebInfo: 32 of 32 jobs, 28.5 s. ASan + UBSan with `detect_leaks=1`: 32 of 32, 297.5 s. No pin is
  STALE. Two pins are MOVED: A6-1's `jumptable print #0 48 B #26` and `mailbox print #1 48 B #20` now lead to `PcodeCacher::emit` <
  `Sleigh::oneInstruction`, an instruction decoded in Print since round 2. Each still finds its site (1 match), and that cell passes. Nothing
  makes anyone re-pin a MOVED pin, but a moved pin still tests its site.
- **Every k under ASan + LeakSanitizer, outside the round 2 sample:**

  | Fixture | Phases | Cells | Result |
  |---|---|---|---|
  | `fixture1` | Flow and Actions | 6,395 | 0 failed |
  | `gotos` | Flow, Actions and Print | 3,910 | 0 failed. Its goto comes from `labelLoops`, not `TraceDAG` |
  | `jumptable` | Flow | 5,262 | 4 leaks (A6-21) |
  | `dag` | Flow, Actions and Print | 6,425 | 94 leaks (A6-22) |
  | `loopbrk` | Flow, Actions and Print | 8,738 | 8 leaks (A6-22) |

  Every failure is a leak. There is no memory error.
- **`PackedDecode::endIngest`, the last-buffer case** (patch 0032, no fixture in the tree). The probe reached it on `stack` with the name
  padded by 922 characters, counted in the cells' numbering (A6-24 explains why it is not 932). Cells 21323 (the 1-byte end buffer) and 21324
  (the list node) fail cleanly under ASan and leak nothing.
- **Repeated refusal.** The engine was damaged at `mailbox` k=15000, then sent 5,000 refused requests in RelWithDebInfo: RSS 12,092 kB before
  and after. A refusal makes one allocation (its message), and the result frees it. When that allocation fails, the request ends `Failed("out
  of memory")`. A refused `Disassemble` whose allocations fail ends `<stopped: ...>`. Nothing escapes in any of the first 64 allocations of
  either.
- **Cancels on the new shape.** On `dag` in full, cancel-enter, cancel-alloc and cancel-oom (7,690 cells, RelWithDebInfo): 0 failed.
- **A6-18 is unreachable from the host, by reading.** `JumpTable::recoverModel` frees the model only when it is entered with a model that is not an
  override, and no entry does that:
  - `matchModel` and `recoverMultistage` call `saveModel` first;
  - the flow's trial table is new;
  - a table kept with 0 entries never got a model;
  - a restart's `clearJumpTables` deletes every table that is not an override.

  `JumpTable::setOverride` has no caller in the vendored engine. It stays open, with no fixture possible.
- **Pin identity, by reading and by `--sites`.** A frame `dladdr` cannot name is dropped and not misnamed. An inlined site or a build without
  `-rdynamic` makes the pins STALE, which fails loudly.
- **The allow-list.** `Sh2DecompilerTests` under ASan + UBSan exits 0. Its only UBSan lines are `address.hh:605` ("left shift of 40962 by 52 places
  cannot be represented", the `sign_extend` idiom) and `slghpatexpress.cc:979` ("left shift of negative value -4"). These are the two accepted
  reports, unchanged.

### What failed

| Row | Severity | What | Cells |
|---|---|---|---|
| A6-19 | P2 | No CI job runs the fault matrix with pin identity or leak checks. The sanitizer job does not build it, Windows skips it, and macOS runs it without a backtrace and without LSan | `grep DecompilerFault .github` finds nothing |
| A6-20 | P3 | `check-sanitizer-logs.sh` passes an empty log and any subset of binaries | an empty log with `.rc` 0 passes |
| A6-21 | P3 | Jump-table recovery leaks the table (`recoverJumpTable`'s `push_back`, `JumpBasic::clone`) on the matrix's own fixture | `jumptable` 10642, 10643, 23891, 23892 |
| A6-22 | P3 | `TraceDAG` leaks its branch points and traces. A loop with an early return reaches it; no matrix fixture does | `dag` 94 of 6,425; `loopbrk` 8 of 8,738 |
| A6-23 | P3 | Under sustained exhaustion, `Disassemble`'s `<stopped>` line cannot be allocated, so the listing comes back empty or cut short with no marker | sticky `disasm`: 3,852 of 3,854 |
| A6-24 | P3 | The baseline is not the run the cells perturb: Print's sizes keep changing for two runs after `Warm`. The census and `--sites` name run N+1's allocation under the baseline's size | `--cell alloc stack 21301`: `PIN-AT` 1114 B, fails 1124 B |

### Questions without a reproducer

- **The markup changes after `Warm`.** The markup of the same request grows over the first runs of a new `Sh2Decompiler` and then stays put:
  `stack` 1,114, 1,124 and 1,128 bytes; `mailbox` 840, 842 and 844. The shown text and every token (`addr`, `target`, `calledAt`) are
  identical. A second `Sh2Decompiler` in the same process starts from 1,114 again, and the library is started once per process. What state
  carries into the markup? This is also an area 1 question: the input of a request is not only its key.
- **A pin's site names a function, not an allocation.** Two allocations of one size in one function cannot be told apart: for example
  `ProtoStoreSymbol::clone` 72 B #26 (the constructor's parameter) and #27 (the copy). A shift of one between them is still `site ok`. For
  today's pins, every allocation in the window crashes the reverted code, so nothing is hidden. A pin whose defect is one allocation (A6-1's
  kind) inside a function that makes several of its size would not be protected.

### Could not probe

- `TypeFactory::setName`, `setFields`, `setPrototype`, `setEnumValues` and `recalcPointerSubmeta`, the dynamic-symbol entries,
  `ScopeLocal::remapSymbol*`, the injection library, the emulator and the SLEIGH compiler. The host builds no struct or enum type, uses no
  dynamic symbol and runs none of the others, so no SH-2 fixture reaches them.
- The runner under sustained exhaustion. Its record path is the existing `DisableOomChild`; the rest was checked by reading only.
- The race between two cancels: by reading, both take `mMutex`, and `RequestCancelLocked` keeps the first reason.
- A child that dies mid-write, and a full pipe. Cells and drivers write to files, not pipes, and a dead cell is classified by its signal.
- `diamonds` for every k, Windows and macOS (the matrix forks), and a deep-recursion fixture.

## Nightly, 20,000 seeds at 124a5eb: the merged-pointer class, a word view, and a literal the program overwrote

Fixer, branch `review/nightly-20k-fix` from master `124a5eb`, merged with `b941845` (the area 5 round 6 review, A5-37 to A5-41, whose cause is the
same code as the nightly's three cache seeds). Ledger rows A5-37 to A5-41 and the new A5-45, A5-46, A4-15 are fixed. The run `SE_GENERATED_SEEDS=20000`
(both grammars) had 4 failing seeds: 7134 and 14812 (grammar 2) and 19409 (grammar 1) `cache-read-pointer-unsaid`, 8306 (grammar 2) `trace-outside`.
Commits: `e8d472e` (the host and the generator), follow-ups `a5d8228` (any register-area address counts), `e6fe7d5` (the stepped-pointer note),
`c6fb2a4` (a mirror fixture), `85a49db` (rule 3b reads the instruction's own tokens; the ledger rows) and the commit that adds this section.
No engine patch (0035 not needed).

### Causes and dispositions

| Seed / row | Who was wrong | Cause | Fix |
|---|---|---|---|
| 19409 (A5-45) | the host | The SMPC registers are also symbols at the even address, the 16-bit word they sit in (`SaturnHardwareWordViews`). A merged pointer holding 0x00100020 prints `&SMPC_OREG0`; `PrintsAsRegister` admitted only the table's addresses (0x00100021), so `HardwareNotes` followed nothing. | Not a special case: the resolver below follows every address the pointer holds that the bus decodes to a register area; `PrintsAsRegister` is removed. |
| 7134, 14812 (A5-46) | the oracle | `func_0x06009048(param_1,param_2,&VDP2_VRSIZE); xVar2 = *extraout_r6;`: the register is the argument of a call and the read is through the same register after it, whose value after the call the engine does not know (a callee with no lent effects; the code runs on into that callee, which is decompiled alone). The host cannot know it and says nothing about it, rightly. Rule 3b asked for `&REG` anywhere in the output. | Rule 3b's premise is exact: the pointer's printed derivation (`PointerPrintsFromHardware`) -- the read instruction's own tokens, or a variable in them assigned (through assignments, call arguments left out) a `&REG`, a register address as a number, or such a base with an offset. A register named only elsewhere is counted `cache.read.pointer.unrelated` (3 in 20,000 seeds), not `cache.read.anonymous` (80); the anonymous excuse is not widened. |
| 8306 (A4-15) | the generator | Not a decompiler fault: the emulator executed 0600906E because the run overwrote the literal at 0600906C. Function 0 calls function 1 with `jsr @r1`, so r1 holds function 1's address; function 1 reads r1 for the value its caller left, copies it to r5 and calls function 2, which does `mov.b r2,@-r5`, over the last byte of function 0's literal; the tail `jmp @r1` then loads the changed word. The decompiler read the image's literal, as it must. A4-9's class. | A function that reads r1-r3 for its caller's value (`leftScratch`) never copies it into an argument register a callee, or what it calls, dereferences (`fixArgs`, no rng draw: no other program of a seed changes). The shrunk fixture is not a program the generator makes (shrinking removed a `mov #8,r1` that had masked it); the seed pins the contract. |
| A5-37 (P2) | the host | `ConstantsOf` kept the base of a PTRADD / INT_ADD and dropped a constant addend, so `puVar1[4]` was said as "if the index is 0" for the base registers. | `PointerResolver`: a constant offset shifts the base (the access is at base + offset); an unknown index makes the base an indexed one (A5-36's text, unchanged). The note names the effective register; "through a mirror (shown by address)" is not said for an address that is printed as `puVar1[4]`. |
| A5-38 (P3) | the host and the oracle | A bare constant on a path was treated as "another value"; right for `param_1 + -0xc` (an addend), wrong for a constant the pointer holds (`0x25fe0110`). Rule 3b excused the cached poll as anonymous because it keyed on the printed name. | A constant the pointer holds is a known address; as the addend of an unknown it is still an offset (`a5_28_rawconst` holds). Rule 3b: see A5-46. |
| A5-39 (P3) | the host | A pointer stepped in a loop was said as if certain; the registers after the first are not said. | A path that comes back to a node under evaluation after an add is a stepped pointer: "(it may hold other values too)", and a note that the iterations after the first reach other addresses, said for a pointer that starts at a named register (a walk through the SCSP DSP program area, where no register is named, has nothing to be missed: the first version noted two such walks in each corpus). A self-merge in a loop (a poll) steps nothing. The five-store loop to SCU_D0EN is not worked out (the trip count is not used): said as not said, as A5-36. |
| A5-40 (P3) | the host and the check | SCU_PPAF (every write acts on the DSP program) and CD_CR4 (a 16-bit write issues the command) said nothing on a write; the handler check exempted PPAF for its `split` text and did not read `cd.wr`'s `F`. | `WriteEffects` says both; the check requires a text for PPAF without the exemption and for `cd.wr` `F` (write-effect registers 8 -> 9). |
| A5-41 (P3) | the host | A long at 0xFFFFFEFE/FD got a second caution naming SH2_DVSR: the registers "under" the long were counted across the split. | The bytes counted stop at the half's end (`0x100 - (A & 0xFF)`): no register, "Mednafen then writes nothing" / "returns 0 and reads nothing". |

Rule 3b needed two more fixes after the first widening. Seed 347 of the 3,000-seed grammar-2 window (a program the changed generator now makes) had
`if ((uint2 *)(int4)*param_3 != &VDP2_CYCB0U)`, where the register is the comparison's other operand, from another instruction: the statement-level
premise flagged it, the read's own tokens (`Token::addr`) do not. And the first version of the host change followed only registers that are named; a read
at an unnamed SCSP register through a displaced pointer (seed 2213, 05B0043C) was then silent: any address in a register area counts, named or not.
(Both appeared only because the first premise was run over 20,000 seeds: 18 failing seeds at `e8d472e`, 0 at `a5d8228`.)

### Mutation checks

Each entry was applied to the source (`mut.py`), the decompiler suite, the width-table test and the generator on seeds 8306, 7134, 19409 and the
words of A5-37c and A5-38b rebuilt and run, and restored.

| Mutation | Suite CHECKs failing | Width table | Generator |
|---|---|---|---|
| addresses followed only at a table register's own address (the round 5 rule) | 18 (`a5_45*`, `a5_37a`, `a5_38*`, `a5_28_through`, ...) | passes | seed 19409 and the A5-38b words fail (`cache-read-pointer-unsaid`) |
| a constant offset dropped (the unfixed A5-37) | 8 (`a5_37a`, `b`, `c`, `d`) | passes | 0 findings (blind, as the review said: the base's cache caution is on the same instruction) |
| a constant the pointer holds counts as unknown | 6 (`a5_38a`, `a5_38b`) | passes | the A5-38b words fail |
| a cycle through an add is not a stepped pointer | 3 (`a5_39a`, `a5_39b`) | passes | 0 |
| any cycle makes the pointer unknown (a poll's self-merge) | 10 (`a5_28` family, `a5_38b`) | passes | 0 |
| no SCU_PPAF write text | 2 | fails (PPAF) | 0 |
| no CD_CR4 write text | 1 | fails (CR4) | 0 |
| bytes under the misaligned long counted across the split | 4 (`a5_41*`) | passes | 0 |
| the "shown by address" line kept for a displaced pointer | 1 (`a5_37_mirror`, added for it: the first mutation run found no fixture for it) | passes | 0 |
| rule 3b keyed on the register named anywhere (the old premise) | 0 | passes | seeds 7134 and 14812 fail again |
| rule 3b without the call-argument strip | 0 | passes | 0: not caught on these seeds; no generated program assigns a call's result from a call with `&REG` in its arguments |
| rule 3b without the chain through assignments | 0 | passes | 0: not caught; the oracle is then blind to `puVar2 = &REG` pointers (they count as anonymous); the counter `cache.read.pointer.said` (435 in 20,000 seeds) is what shows it |
| the jsr contract without `left` | 0 | passes | seed 8306 fails again (`trace-outside`) |

### Matrix

| | |
|---|---|
| Suite fixtures | `a5_45`, `a5_45_through`, `a5_45_byte`; `a5_37a`-`d`, `a5_37_mirror`; `a5_38a`, `a5_38a_direct`, `a5_38b`; `a5_39a`, `b`; `a5_40a`, `b`, `b_byte`; `a5_41a`, `a_fd`, `b`; the PPAF-written test now expects the PPAF text |
| `ctest` | 97 of 97 passed (before the last, generator-only commit) |
| Width-table test | 337 registers, 3,038 cells, passes; 9 write-effect registers (was 8) |
| Generator, `SE_GENERATED_SEEDS=20000` | 0 failing seeds, 0 findings (the unfixed run: 4); `cache.read.said` 4,687, `cache.read.pointer.said` 435, `cache.read.pointer.unrelated` 3, `cache.read.anonymous` 80 |
| Generator, `SE_GENERATED_GRAMMAR=2 SE_GENERATED_SEEDS=3000` | 0 findings; pointer said 53, anonymous 16, direct said 160 |
| Printed C, `SE_GENERATED_PRINTEDC=only SE_GENERATED_SEEDS=3000` | 3,017 functions, 2,410 compared, 2,477 agree (before the last, generator-only commit) |
| Game corpus, `hwram007.bin` | 839/839; every count 0; 16 missing and said; records 14,843 folded, 0 dead, 16 unreachable, 27 switch lookups; notes 14 -> 16, cautions 16 |
| Game corpus, `hwram008.bin` | 1,554/1,554; every count 0; 20 missing and said; records 38,701 folded, 0 dead, 20 unreachable, 155 switch lookups; notes 29 -> 31, cautions 20 |
| Corpus text that changed (master's build beside this one's, every function of both images, pseudocode, warnings, cautions and notes) | pseudocode, warnings and cautions byte-identical. Four note lines appeared, each read: "SCU_PPAF: a write acts on the DSP program whatever is written: ..." at `hwram007` 06033D80 and 06033D90 (function 06033D5A) and `hwram008` 06074E14 and 06074E24 (function 06074DEE): `SCU_PPAF = 0;` and `SCU_PPAF = param_1 or 0x8000;`, the two A5-40 instances. (The first version of the stepped-pointer note added two more in each image, at the SCSP DSP program area, 0602FE5E/0602FE70 and 0606FA76/0606FA88: removed, see A5-39.) |
| ASan + UBSan + LSan, gcc Debug | `Sh2DecompilerTests`, `HardwareWidthTableTests` and `Sh2GeneratedTests` (300 seeds), `detect_leaks=1`: no AddressSanitizer or LeakSanitizer report; UBSan only at the two tolerated sites (`address.hh:605`, `slghpatexpress.cc:979`); all pass |

### Not done

- The registers after the first of a stepped pointer (the five-store loop reaches SCU_D0EN) are said not to be said, not worked out (A5-36's limit).
- A merged on-chip pointer used with a displacement prints its values as signed numbers (`iVar1 = -0x80`); it is now followed when the values are known
  constants, but whether the printed type is this area's stays with the coordinator (round 6's question).
- The round 6 reviewer's other two questions (SCSP slot registers, KYONEX) are not in this branch.
- A callee-returned pointer (`*extraout_r6`) is not said: the host cannot know the value; the generator now says so (`cache.read.pointer.unrelated`).
- `SE_GENERATED_WORDS` of seed 8306 still reports `trace-outside`: the words are a self-modifying program, outside the model; the seed is the test.
- The ctest and printed-C runs were on `c6fb2a4`; the last code commit (`85a49db`) changed only the generator's rule 3b, which the 20,000-seed, 3,000-seed and ASan generator runs cover.
