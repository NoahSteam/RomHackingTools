# Narration — Live Debugging walkthrough

Read over `LiveDebugging.sedemo`. This one is **operator-driven**: the script sequences the
panels, and *you* perform the actions it describes (set a breakpoint, create a tracepoint)
while you narrate. Use **manual mode** (F8) and take as long as each step needs.

> Prep: connect to a live, patched emulator (Session → Source → Connect to Emulator) and get
> the game to a point where a repeatable action triggers code you want to watch (a line of
> dialogue, a menu open, a hit landing).

---

**1 · intro** ⏱9s — *SH-2 Assembly*
> Saturn Explorer isn't just a viewer — connected to a patched emulator, it's a live debugger
> built for reverse-engineers. Here's the SH-2 Assembly panel, disassembling around the CPU's
> program counter in real time as the game runs.

**2 · breakpoints** ⏱12s — *Assembly + Breakpoints* — *(click the gutter to set one)*
> Click in the gutter to set a breakpoint, exactly like Visual Studio. The emulator halts the
> instant the CPU reaches that address. From here you get Run to Here, and step into, over, and
> out — full instruction-level control of a running Saturn game.

**3 · callstack** ⏱11s — *Call Stack + Assembly* — *(trigger the break, then click a frame)*
> The moment it breaks, the Call Stack panel reconstructs how the code got here — a confirmed
> stack when the emulator provides one, or a heuristic walk when it doesn't. Click any frame and
> the Assembly and Memory panels jump to it. This is the paused-state workspace.

**4 · tracepoints-open** ⏱13s — *Tracepoints + Assembly* — *(create a tracepoint with a format string)*
> But breakpoints only let you stop. What reverse-engineers really want is to watch. That's a
> tracepoint — a non-halting action attached to an address. Open the Tracepoints panel and create
> one, with a format string like: "Dialogue ID equals, open-brace r4". When the CPU hits that
> address, it logs the value and keeps running.

**5 · tracepoints-log** ⏱14s — *Log + Tracepoints* — *(resume the game; let it fire)*
> Now let the game play. Every time execution crosses that address, the tracepoint fires and
> writes a line into the structured Log — the game narrating itself. You can even dereference
> pointers, like reading a character's name straight out of memory as a string, with no
> emulator-side code at all. The Log is filterable by category, and every entry jumps back to its
> instruction.

**6 · memory** ⏱11s — *Watch + Memory + RAM Search*
> Alongside that: the Watch panel evaluates expressions and memory reads every frame; the Memory
> hex editor views and edits work RAM live; and RAM Search scans for a value the way Cheat Engine
> does — change it in-game, scan again, and narrow millions of addresses down to the one you want.

**7 · outro** ⏱8s — *Assembly + Log*
> Breakpoints, a reconstructed call stack, tracepoints that make a game explain itself, and live
> memory tools — Saturn Explorer turns a running game into something you can read. Combine this
> with the graphics tools, and you can follow a sprite from the screen all the way back to the
> code that drew it.

---

Approx. auto-mode runtime: ~1:18 (manual, with you performing each action, runs longer —
budget ~4–6 min).
