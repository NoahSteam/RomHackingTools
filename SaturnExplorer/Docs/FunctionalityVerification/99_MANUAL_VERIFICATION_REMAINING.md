# Remaining Manual / Fixture-Backed Verification

**Supported platforms: Windows and macOS only.**

A feature should not be marked Verified until it has a repeatable automated fixture or documented manual reproduction tied to a build SHA.

## Real state files

Yabause, Yaba Sanshiro, Kronos, Mednafen, Beetle Saturn, full/region dumps.

## Real graphics

Golden `.sedump` captures for VDP1/VDP2 modes, windows, rotation, color calculation, interlace/high-res and mixed priorities.

## Patched emulator

Connect/reconnect, pause/resume, frame/instruction stepping, Step Over/Out, both SH-2 CPUs, conditional BPs, watchpoints, tracepoints, call stack, controller injection, all memory writes, audio/CD status, slots, rewind + scrub edits.

## Real disc workflow

Search/mapping -> memory edit -> generated patch with baseline protection -> BIN/CUE rebuild -> verify every track -> boot modified image.

## Frame Memory Compare (needs a live emulator)

The engine, panel and marker rules are unit-tested; the transport bar and the App wiring are not
reachable headlessly. See `Docs/MemoryCompare/`. With a live game and Rewind on:

- pause, right-click the scrub bar, **Set as Compare Frame A**; scrub elsewhere, **Set as Compare
  Frame B**: both carets sit exactly over their frames and the row under the bar shows both numbers
- **Compare Memory...** opens the Memory Compare tab, docked beside Memory, with plausible changed
  regions; the summary, Changes Only, Show Context and Jump to behave
- marking at the live head while paused works; marking a scrubbed frame works; marking a scrubbed
  frame with an unapplied hex edit is refused with a message
- **Go to A / Go to B** scrub the timeline to that frame; right-click a changed byte: Go to Memory,
  Add to Watch, Break on Write, View in Assembly and Export land where expected, and the disabled
  items (Break on Write on VDP1 FB, View in Assembly outside work RAM) say why
- Break on Write on a changed Sound RAM byte: the item is enabled and its tooltip says 68K writes are
  not caught; it halts when the SH-2 (or SCU DMA) writes that byte
- let the ring evict a marked frame: its caret goes hollow and pinned left, the label says
  `(detached)`, **Go to** is disabled, and the comparison still works
- Play From Here: a marker past the resume frame detaches; one at or before it stays attached
- restart the emulator, or load another source (savestate file, dump, disc): both markers and the
  comparison are cleared
- load an emulator or app save slot (history replaced, same game): the markers stay but go hollow and
  `(detached)`; the snapshots still compare
- mark a live-head frame the recorder never kept (after Stop Recording): it is accepted, shows
  `(detached)`, and still compares
- **Clear Compare Markers** removes both carets and closes the row
- shrink the dock to a narrow width with two detached markers and a refusal message showing: the
  labels, **Compare Memory...** and the message wrap onto further lines and stay reachable, and the
  VDP image above the bar gives up exactly the height they take
- start Go to A/B while the recorder is still publishing frames (just after pausing): it opens the
  marked frame, never a neighbour; if that frame was evicted meanwhile, the row says so and the view
  stays live
- after Go to A/B, keep the game paused while frames are still arriving (just after pausing): the shown
  frame stays the marked one as the ring shifts, and a hex edit made on it is not carried to another
  frame by navigating or scrubbing (Play From Here replays only the edits made on the frame it resumes)
- edit a byte on the shown frame, then choose Go to that same frame: the byte keeps its edited value (it is
  still staged); seek away and back: the recorded value returns and Play From Here replays nothing
- **Export...** writes a CSV whose line count equals the changed-byte total for what was exported; on a
  large diff (All Memory with the VDP1 FB and VDP2 VRAM busy) the UI keeps drawing behind an
  "Exporting Memory Diff" progress popup, **Cancel** writes nothing, and the save dialog appears only
  when the export finishes

## Supported platform UI

After Windows build defects are fixed, verify on both supported platforms:

### Windows
- launch and shutdown
- D3D11 rendering
- native menus
- file dialogs
- audio playback
- live emulator connection
- keyboard/controller input
- debugger interactions

### macOS
- launch and shutdown
- SDL2/OpenGL rendering
- native menu bar
- file dialogs
- audio playback
- live emulator connection
- keyboard/controller input
- debugger interactions

Linux and Web do not require release verification.
