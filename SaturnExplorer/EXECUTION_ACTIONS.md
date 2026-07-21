# Design: Execution Actions (Tracepoints, structured Log, and beyond)

**Status:** proposal / not started. Captures the vision + architecture so it can be
built in phases later. Nothing here is implemented yet.

## The idea

Traditional emulator debuggers only let you *stop* execution. Reverse engineers usually
want the opposite: to **observe** execution without constantly breaking in. So generalize
the debugger's model from "breakpoints" to **Execution Actions** — every instruction
address can have zero or more actions attached, and when execution reaches that address
the emulator runs them. A **Break** is just one action type; a **Tracepoint** (a
non-halting "log this") is another. The model is open-ended: screenshot, memory snapshot,
counter, start/stop recording, notification, run-script — all future action types on the
same address, with no UI redesign.

This is the feature that distinguishes Saturn Explorer from a traditional emulator
debugger: *tracepoints* (the Visual Studio term RE-minded programmers already know) let
you watch a running game narrate itself.

## Why this fits the existing architecture

- **`BreakpointManager` already models per-address, per-CPU, enable-able things** synced
  to the emulator (`FrontEnd/src/Debug/BreakpointManager.{h,cpp}`, wire form `BKP` in
  SeLiveProtocol.h). Execution Actions is its generalization: a Break action is today's
  execution breakpoint; other action types ride the same install/sync path.
- **The Watch panel already parses `{expr}` / `*addr` expressions** (the expression
  resolver behind WatchList / MemoryBackend). Tracepoint format strings reuse it —
  no second expression language.
- **The Assembly panel already has a breakpoint gutter + right-click menu**
  (`AssemblyPanel.cpp`); tracepoints add a second gutter glyph and menu items.
- **The live protocol is a poll/reply snapshot stream**; log events attach to the reply
  (below), so no new transport is needed.

## Architecture — three decisions

### 1. Split evaluation: client compiles, emulator captures, client formats
A tracepoint fires the instant an instruction executes, so its condition + values must be
read from the CPU's *live* register/memory state at that instant — the client's polled
snapshot is asynchronous and can be a frame stale. But putting a string/expression parser
in the emulator glue is heavy and emulator-specific. So split it:

- **Client (C++), at install time:** parse the format string + condition into a compact
  **capture spec** — an ordered list of captures (`REG(n)`, `MEM(addr-expr, type)`) plus
  an optional condition (`value-expr OP constant`). Ship it with the tracepoint.
- **Emulator (glue), on hit:** run the tiny fixed interpreter — evaluate the condition;
  if true, read the listed registers + memory; queue an event carrying just those
  captured values (+ pc, cpu, frame, cycle). No string handling emulator-side.
- **Client, on receive:** substitute the captured values into the original format
  template and push a structured Log entry.

The emulator interpreter is small and fixed (register file access + a byte/word/long/
string memory read + one comparison); all the parsing/formatting complexity stays in the
client where it's easy and testable.

### 2. Log events piggyback on the snapshot reply
Events are emulator-*push* (they happen when instructions execute) but the protocol is
client-*pull*. Rather than add a second channel, append an **events section** to each
snapshot reply: `count` + `count` serialized events (tracepoint id, cpu, pc, frame,
cycle, captured-value blob). The client drains it into the Log each poll. At the ~120 Hz
poll cap that's ≤~8 ms latency. The install command (like `BKP`) carries the compiled
capture spec. Protocol bump v7→v8.

### 3. Generalize BreakpointManager into an action list
Evolve `BreakpointManager` into an execution-action store: `address, cpu, enabled,
type, condition, repeat{EveryTime|Once|EveryN, n}, hitCount`, plus per-type payload
(Log: format string + compiled captures). A Break is `type=Break`. The Assembly gutter
and the wire sync iterate this one store. Existing execution breakpoints migrate to
`type=Break` with no behavior change.

## The pieces

### Structured Log panel
Not a scrolling text box — a filterable event table (see the mockup): columns
**Frame · Category · Message**, filter chips (All / Info / Warnings / Errors / CPU / DMA /
VDP / CD / Scripts / Tracepoints), "Pause Auto-Scroll", and a search box. Categories are
an enum; sources today are system events (Connected, Loaded, protocol mismatch, frame
advance) and tracepoints; DMA/VDP/CD categories light up as the emulator emits them
(future), and Scripts ties into the Python proposal (SCRIPTING.md).

Every entry is **expandable** to a detail view: Time, PC, CPU, Frame, the captured
register values, and **Jump to Assembly / Jump to Memory** actions. Double-clicking a
tracepoint entry jumps the Assembly panel to that instruction.

### Assembly gutter + right-click
Two gutter glyphs, distinguished by **shape** (not just color) so they read at a glance:
🔴 Break (circle) · 🔷 Tracepoint (diamond) · ▶ current PC. Right-click menu (from the
mockup): Toggle Breakpoint · Toggle Tracepoint · Run To Here · Set PC Here · Follow
Branch · Go To Address · View In Hex Editor · Add Operand To Watch · Break On Read/Write
(Operand) · **Create Tracepoint…** · Create Counter… · Copy Address/Instruction. A
tracepoint glyph can **expand inline** to show its format + hit count without opening the
dialog.

### Tracepoint editor (property editor, not a cramped dialog)
Instruction (read-only) · Enabled · **Output** (the format string) · **Repeat** (Every
time / Once / Every N + N) · **Condition** (an expression) · **Actions** checklist
(Write to Log / Pause Emulator / Capture Screenshot / Save Memory Snapshot / Play Sound /
Run Script). "Actions" here is the per-tracepoint realization of the general
Execution-Actions model.

### Format mini-syntax (not a scripting language)
Literal text with `{…}` substitutions:

| Token | Prints |
|---|---|
| `Hello` | `Hello` |
| `Dialogue ID = {r4}` | `Dialogue ID = 182` |
| `Portrait = {r5:X8}` | `Portrait = 0603A420` |
| `PC={pc}` `Frame {frame}` | `PC=06018442` `Frame 1832` |
| `HP = {*0x6034F20:u16}` | `HP = 235` |
| `Name = {*r4:string}` | `Name = Sakura` |

Grammar: `{ value [:spec] }` where `value` is a register (`pc,frame,cycle,r0..r15,sr,
gbr,vbr,mach,macl,pr`) or a memory deref `*<addr-expr>` (addr-expr is a register or
constant, optionally `+off`); `spec` is a width/format (`X8`, `u16`, `s32`, `string`, …).
Each `{…}` compiles to one capture in the spec above.

**Autocomplete:** typing `{` pops the register list; typing `*0x` (or `*` then a value)
suggests the memory regions (VDP1 RAM, VDP2 RAM, Color RAM, High/Low Work RAM). This
makes the format box discoverable instead of a syntax users must memorize.

## Phasing

**Phase 1 — client model + Log panel + gutter/editor (mostly verifiable in-repo).**
Execution-action store (generalize BreakpointManager); structured Log panel; Assembly
TP diamond gutter + right-click Toggle/Create Tracepoint; the Tracepoint editor; the
format parser + a live **preview** of the formatted output; system log events. Verify the
panel + parser headlessly; feed synthetic tracepoint events from the mock server to
exercise the receive/format/display path (as the live features were mock-verified before).

**Phase 2 — protocol + driver.** v7→v8: install the compiled capture spec (extend the
`BKP`-style command); add the events section to the reply; LiveDriver drains events into
a queue the App polls. Mock-verified.

**Phase 3 — emulator glue (NOT verifiable in-repo).** The fixed capture interpreter +
condition eval in `se_mednafen_glue.c` / the Yabause glue: on a PC hit, eval condition,
read the listed registers/memory, queue the event. Fenced like the existing hooks;
confirmed on a real Windows build.

**Phase 4 — more action types + management.** Screenshot, memory snapshot, counter,
start/stop recording, notification, run-script (ties to SCRIPTING.md); the "Assembly
Actions / Tracepoints" management table (address · type · summary · enabled · hits) from
the mockup.

## Open questions
- **Condition richness:** v1 = one comparison (`expr OP const`); do we want full boolean
  expressions later (reuse the Watch resolver + a small RPN)?
- **Event volume / backpressure:** a hot tracepoint (fires every frame, or in a tight
  loop) could flood the events section. Cap per-poll event count emulator-side and report
  a "N events dropped" marker; the Repeat=EveryN policy is the user-facing throttle.
- **`{*r4:string}` length + encoding:** cap length; Shift-JIS vs ASCII (the ShiftJis
  helper already exists for the Assembly/Watch views).
- **Persistence:** execution actions belong in the per-session settings (the Settings INI
  the panels already use), keyed by ROM.

## Not doing (for now)
- Input macros (the earlier controller-panel concept) — dropped.
- A full scripting language in tracepoints — the format mini-syntax + the (separate)
  Python scripting proposal cover the spectrum.
