# Call Stack + Paused-State Workspace — design

When execution stops (a breakpoint / watch hit, or a loaded savestate), the debugger
should show **where the CPU is in the program** — the chain of function calls that led
to the halted instruction — and turn the Assembly area into a coordinated *paused-state
workspace* where every panel reflects the selected frame.

This is a distinguishing feature: a Saturn game has no debug symbols and no unwind
metadata, so a dependable call stack is not free. We get one two ways, and label every
frame with how much we trust it.

---

## The frame model

```c
enum SaturnCpu      { Master = 0, Slave = 1 };
enum FrameConfidence{ Confirmed, Probable, Heuristic };   // ● ◐ ○

struct CallStackFrame
{
    SaturnCpu Cpu;
    uint32_t  CallSite;            // the bsr/jsr instruction that made the call
    uint32_t  FunctionAddress;     // entry point of the frame's function (best known)
    uint32_t  ReturnAddress;       // where this frame returns to (PR / saved-PR)
    uint32_t  StackPointerAtCall;  // R15 at the call
    uint64_t  Cycle;               // when the call happened (shadow stack only)
    uint32_t  FrameNumber;         // emulated frame of the call (shadow stack only)
    FrameConfidence Confidence;
    // display: resolved function name (user rename / symbol / sub_XXXXXX)
};
```

Frame #0 is the halted instruction itself (its `FunctionAddress` is the function the PC
is in; `ReturnAddress` = PR). Frames above it are its callers, innermost first.

A frame's confidence reflects its source:

| Source | Confidence |
|---|---|
| Recorded call/return event from the instrumented emulator (shadow stack) | ● Confirmed |
| Saved-PR on the stack whose target is preceded by a `bsr`/`jsr` | ◐ Probable |
| A stack word that merely *looks* like a code pointer | ○ Heuristic |

---

## Two ways to get a stack

### 1. Shadow call stack (reliable — live, instrumented emulator)

The best stack is one we *record as it happens* instead of reconstructing after the
fact. The emulator tap watches the SH-2 control flow and maintains a logical stack per
CPU:

```
bsr / jsr / bsrf / jmp-as-call  → push a frame {callSite, target, retAddr, R15, cycle, frame}
rts                             → pop a frame
trap / interrupt / exception    → push an exception frame
rte                             → pop the exception frame
```

A shadow stack per **Master** and **Slave** SH-2. Every frame is ● Confirmed. This
requires the per-instruction hook the tracepoint feature already established
(`SeMednafenTraceHook` on Mednafen; the Yabause SH2 hooks), so it is fenced the same way
and only active on a `--enable-debugger` build.

### 2. Heuristic reconstruction (fallback — savestates, dumps, un-instrumented live)

With only a memory image + registers (a `.yss`/dump, or a live emulator without the
call/return hook), we reconstruct:

- Frame #0 from PC (+ PR for its return).
- Walk the stack from R15 upward, read each 32-bit big-endian word, and for any word
  that lands in a plausible code region treat it as a saved return address → a caller
  frame. If the two bytes *before* that target decode as a `bsr`/`jsr` (i.e. the return
  address really is the instruction after a call), mark the frame ◐ Probable; otherwise
  ○ Heuristic. Cap depth and stack span so a garbage SP can't run away.

Heuristic entries are always labelled as such in the UI — never presented as ground
truth. This is the **Reconstruct Stack** mode, and it is what makes the panel useful on
static savestates with no emulator at all.

The two sources compose: when the shadow stack is present it wins; the heuristic fills
in below the deepest confirmed frame (e.g. frames already on the stack before recording
started).

---

## The panel

```
Call Stack — Master SH-2                                   [Reconstruct]
  #  Function / Label        Return Address   SP           Source
● 0  ProcessDialogueByte     0x06018442       0x060FFB80   Current
◐ 1  DecodeDialogueCommand   0x06017F2A       0x060FFB98
○ 2  UpdateDialogueWindow    0x06015310       0x060FFBC0
```

- Docks **beside/below the Assembly** window; part of the paused-state workspace.
- While *running* it can stay collapsed ("Available when execution is paused"); on a
  stop it auto-expands and selects frame #0.
- **Double-click a frame** → navigate Assembly to the frame address, show that frame's
  registers where recoverable, highlight its stack range in the Hex Editor, and (later)
  update a Locals/Arguments inspector.
- **Confidence glyph** ● ◐ ○ per row, with a tooltip explaining the source.
- **Right-click** a frame: Go to Call Site · Go to Function · Go to Return Address ·
  View Stack Memory · Add Address to Watch · Set Execution Breakpoint · Rename Function ·
  Copy Stack.
- Function names come from a persisted address→name store (Rename Function), falling
  back to a user comment or `sub_XXXXXX`.

### Breakpoint-hit strip

On a stop, a prominent strip announces it and offers run control:

```
BREAKPOINT HIT   Master SH-2 · PC 0x06018442 · Memory write to 0x25A000
[Continue] [Step Into] [Step Over] [Step Out]
```

and everything synchronizes: Assembly → current instruction; Call Stack → frame #0;
Registers → stopped CPU; Log → the breakpoint event; Hex Editor → the accessed memory;
Selected Object → the corresponding VDP object when applicable.

(Instruction-granular Step Into/Over need an instruction-step protocol verb — see phases;
Step Out = run to frame #0's return address via a temp breakpoint, achievable today.)

---

## Phases

**Phase 1 — client model + panel + heuristic reconstructor + paused workspace
(verifiable in-repo).** The `CallStackFrame`/`CallStack` model; the heuristic
reconstructor (works on a loaded savestate, so it renders real frames with no emulator);
the Call Stack panel with confidence glyphs, double-click navigation (Assembly + Hex
highlight), the context menu, function rename (persisted); the breakpoint-hit strip;
dock beside Assembly; auto-populate on pause / savestate load. Verified headless against
the Sakura Wars savestate.

**Phase 2 — protocol + driver (done).** v8→v9: a per-CPU call-stack block trails the
reply (`SeExportPushFrame`/`PopFrame`/`ResetCallStack` in se_export own the shadow
stacks; the server serializes them innermost-first). LiveDriver reads the block and
exposes `se_live_poll_callstack`; the App prefers these ● Confirmed frames over the
heuristic when a live source supplies them. Verified end-to-end with the real
se_export.c sender + real LiveDriver reader (v9 negotiated, frames + fields correct,
push/pop reflected across snapshots). (The instruction-step verb for Step Into/Over is
tracked separately — it's a stepping feature, not part of the stack itself.)

**Phase 3 — emulator glue (not verifiable in-repo).** The shadow call stack in
`se_export` + the Mednafen/Yabause glue: maintain per-CPU logical stacks from
bsr/jsr/rts/exception/rte in the per-instruction hook; serialize into the reply. Fenced
like the tracepoint hook; confirmed on a real build.

**Phase 4 — depth.** Register state per frame (from shadow-stack captures), a
Locals/Arguments inspector, symbol/map-file import to name functions, and confidence
reconciliation between the shadow and heuristic sources.

## Not doing (yet)
- Full DWARF-style unwinding — there is no unwind metadata on Saturn.
- Cross-CPU merged stacks — Master and Slave keep separate stacks.
