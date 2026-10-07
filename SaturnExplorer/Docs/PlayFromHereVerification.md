# Play From Here: verification plan

"Play From Here" restores the scrubbed frame's savestate into the emulator and resumes from it,
discarding the recorded frames after it. This is how to show it works on a real Mednafen running a
real game, and what each step is for.

## What is already covered without an emulator

`RewindPipelineTests` runs the real exporter, live driver and `FrameRecorder` together (only the
emulator is faked): blocks reach the recorder, frames become resumable, and after a rewind no state
from the abandoned timeline is ever filed under a reused frame number. `FrameRecorderTests` pins the
recorder's rules. Neither can say whether the *real* emulator's savestates round-trip, whether the
button is wired to all of it, or what the player sees. That is this plan.

## Setup

- `./update.sh` on the branch under test, so Saturn Explorer **and** the patched Mednafen are both
  current (the exporter's protocol version must match: v22 or later).
- Settings: Rewind (save state every frame) **on**; Data directory set; launch config pointing at
  Sakura Wars 2 (`SW.cue`).
- Nothing else listening on the live socket (`/tmp/saturn_explorer.sock`, TCP 6845).
- Checks 1 and 3 to 7 can be run without the window, by `se-rewind-live-check` against a running
  emulator (`./build/bin/se-rewind-live-check`; start Mednafen with `-ss.smpc.autortc 0 -sound 0`).
  Checks 2, 4, 8 and 9 are about the button and need a person at the window (see Results).

## The checks

Each check names what it proves. A check passes only on what is **seen**, not on the absence of an
error.

1. **Launch and connect.** Saturn Explorer starts the emulator with Sakura Wars 2; status bar says
   *Connected to Emulator*; the game is visibly running in the VDP Output view and the frame counter
   advances. *Proves the environment is real before anything is judged on it.*
2. **Button offered, and why it is off.** With rewind on, the button sits right of Play. While the
   game *runs*, hover it: the tooltip must say to pause first, and what the button does.
   *Proves the "how to enable it" tooltip.*
3. **Savestates arrive.** Let the game run past one full buffer (5 s = 300 frames), then pause. The
   scrub bar shows a count near 300 and a frame number. *Proves frames are recorded.*
4. **Enabled when it should be.** Paused with recorded frames: the button is lit with no scrubbing
   needed (it acts on the newest frame), and its tooltip names the frame it will restore and how
   many frames after it will be discarded. If it is grey, hover it and read the savestate numbers in
   the tooltip. *Proves the original failure is fixed: the button was never available.*
5. **Scrub, then restore.** Scrub back to roughly the middle (note frame **N** and the position
   *i* of *n*). Take a screenshot of the picture shown. Press the button. Expect:
   - the game continues running (frame number goes up from about **N**, not from the old newest);
   - the first picture after resuming matches the screenshot for **N** (same scene, same dialogue);
   - the status area reports *Playing from frame N*.
   *Proves the state really is restored and the game resumes from it.*
6. **The future is cleared.** Pause again. Expect the scrub bar's count to be roughly *i* plus the
   frames recorded since resuming, and **not** the old *n*; stepping the bar from the old position
   forward must show frames numbered N+1, N+2, … from the *new* run, with no jump back up to the old
   newest. *Proves the buffer after N was discarded, as specified (e.g. 100 recorded, restore at 40,
   frames 41–100 are gone).*
7. **It can be done again.** Scrub back within the new history and restore a second time.
   *Proves the keyframes and epoch accounting survive a second rewind.*
8. **Plain Play is not Play From Here.** Pause, scrub back, press **Play**. Expect the game to carry
   on from the *present* (frame number does not drop back to N). *Proves the two buttons differ.*
9. **Rewind off.** Turn Rewind off in Settings: the button disappears and nothing is recorded.
   *Proves the setting gates it.*

## Evidence and reporting

Screenshots for steps 2, 4, 5 (before and after), 6 and 8 are kept with the results below. A step
that cannot be completed is reported as such with the reason; it is not marked passed.

## Results

Run against the real patched Mednafen, with Sakura Wars 2 (`SW.cue`), on macOS arm64, using
`se-rewind-live-check` (`FrontEnd/tools/RewindLiveCheck.cpp`). Emulator started with
`-ss.smpc.autortc 0 -sound 0` so that it is deterministic. That tool drives the same live driver and
`FrameRecorder`, in the same per-frame order, as the front end; it does not click the button.

### What was verified, on the real emulator

| Check | Result |
|---|---|
| Connects; server speaks protocol v22 | pass |
| Frames are recorded (300 frames in about 8 s) | pass |
| The emulator's savestate blocks arrive; none refused | pass (about 470 received, 0 invalid) |
| Recorded frames can be resumed from | pass: **300 of 300** (also when the client attaches to an emulator already hundreds of frames in) |
| A restore of a 5.2 MB state is accepted and applied exactly once, none refused | pass |
| The game resumes **from the rewound frame**, not from where it had got to | pass (rewound to #1429, first new frame #1430) |
| The frames after the rewound one are discarded at once | pass (153 discarded; 100-recorded/restore-at-40 scenario holds) |
| Frame numbers stay strictly increasing through the restore, twice in a row | pass |
| Savestate blocks of the abandoned run are kept out of the new one | pass (2 to 4 dropped per restore) |
| A second restore of the **same frame** replays identically to the first | pass: **40 of 40** frames, all runs |
| The replay matches the original run | **pass, with one caveat below**: about 95 percent of compared frames are bit-identical (VDP1/VDP2 VRAM, color RAM, work RAM), and the final 10 compared frames always are |

### The caveat: a short settling transient after a restore

Roughly 4 of 85 compared frames differ from the original run, **always within the first ~20 frames
after the load**, after which the replay matches the original exactly again. So the game is back on
its original course; Mednafen's own savestate leaves something small unrestored that the game
settles within a few dozen milliseconds. It never drifts, and it is the same every time (two
restores of one frame agree exactly). Not investigated further.

### Defects the verification found and fixed

1. **The savestate was taken in the wrong place in the frame** (mid-frame, inside `MidSync()`). A
   restored game ran off its original course: only 3 of 76 compared frames matched, and high work
   RAM differed in 73. Moving the capture to the end of `Emulate()` brought that to about 95 percent
   and to 100 percent for a second restore.
2. **A client attaching mid-run could resume from only ~40 percent of what it recorded** (114 to 128
   of 300), because the keyframe its deltas were measured against predated it. The first state a
   client receives is now a keyframe: 300 of 300.
3. Earlier in the same effort: the button was never available at all (blocks arrived before their
   frames and were dropped; keyframes were looked up as recorded frames).

### Not verified

- **The button itself, its tooltips and the window.** Driving the GUI needs screen access, and macOS
  raised a system prompt for it (*"Claude" is requesting to bypass the system private window picker
  and directly access your screen and audio*). That is a security permission and was left for the
  user to decide; no GUI interaction was attempted. A person should check, once, with the checks
  above: pause a running game, hover the button (the tooltip should say it will restore frame #N
  and how many frames it discards), scrub back, press it, confirm the game continues from there
  and the scrub bar's count falls to the frames up to it.
- Windows. The tool and the pipeline test are POSIX-only.
- Panzer Dragoon Saga: not run.
