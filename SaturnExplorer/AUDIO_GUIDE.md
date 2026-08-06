# Saturn Audio — a plain-English guide

This guide explains **how the Sega Saturn makes sound** and **how Saturn Explorer's
audio tools let you see, hear, extract, and trace that sound**. No prior knowledge of
the hardware is assumed — every term is defined the first time it shows up.

If you only remember one thing: the Saturn has **two completely separate ways** of
playing audio, and knowing which one a game is using tells you which tool to reach for.

---

## Part 1 — How the Saturn makes sound

### The two paths to your ears

1. **Sampled/synthesized sound (the SCSP path).** Sound effects, voices, and most music
   are short recordings ("samples") stored in the game's data. A dedicated sound chip
   plays them back — changing their pitch, volume, and stereo position on the fly. This
   is where the vast majority of a game's audio lives, and it's the path the tool can
   inspect in depth.

2. **CD audio (the CD-DA path).** Some games put full music tracks on the disc as
   ordinary **Red Book audio** — the same format as a normal music CD. The Saturn just
   spins the disc and pipes those tracks straight to the speakers. This audio never
   passes through the sound chip or sound memory at all; it's mixed in hardware.

Almost everything below is about path 1, because that's the rich, inspectable one.
Path 2 matters mainly so you recognize it when you see it (more on that later).

### The cast of characters (path 1)

Think of the Saturn's sound system as a small self-contained studio bolted onto the
main console:

| Part | Nickname | What it does |
|------|----------|--------------|
| **SCSP** | "the sound chip" | The synthesizer. Plays up to **32 sounds at once**, each with its own pitch, volume, and pan. (SCSP = *Saturn Custom Sound Processor*.) |
| **Sound CPU** | "the 68000" | A small second processor (a Motorola 68000) that runs the sound program — deciding which notes to trigger, when, and how loud. The main game CPUs hand it commands and it takes care of the music. |
| **Sound RAM** | "the sound memory" | 512 KB of memory that holds *both* the sound program the 68000 runs *and* the raw audio samples the SCSP plays. |
| **Main CPUs (SH-2)** | "the game" | The two main processors running the actual game. They tell the sound system what to play. |

The key idea: **the samples the SCSP plays live in sound RAM.** If you can read sound
RAM, you can see (and grab) the actual audio data.

### Voices (a.k.a. "slots")

The SCSP can play 32 sounds simultaneously. Each of those 32 playback channels is called
a **voice** or a **slot**. At any moment, a voice is either silent or playing one sample.
A rich battle scene might have a dozen voices going at once — drums on one, a melody on
another, a sword clang on a third, a voice clip on a fourth, and so on.

Each active voice carries a bundle of settings:

- **Which sample** it's playing (a location in sound RAM) and **how far into it** playback
  currently is.
- **Format** — how the audio is stored (see below).
- **Pitch** — how fast to play the sample, which sets the note. The same recording played
  faster sounds higher.
- **Volume envelope** — how the sound fades in and out over time (the classic
  attack → decay → sustain → release shape). This is why a piano note swells then dies
  away instead of just clicking on and off.
- **Pan** — where it sits in the stereo field (left ↔ right).
- **Loop points** — many samples are short and loop forever (a held string note, an
  engine hum), so the voice knows where to jump back to.

### Sample formats

Saturn samples are stored as plain **PCM** — uncompressed audio, the simplest possible
format — in one of two sizes:

- **16-bit PCM** — full-quality, two bytes per sample. Clean but larger.
- **8-bit PCM** — half the size, a bit noisier. Used to save memory.

That's it. (If you've worked with the Dreamcast, note its 4-bit ADPCM compression is
**not** a Saturn feature — the Saturn's sound chip only does 8- and 16-bit PCM.)

### "Streamed" sound — the important special case

512 KB of sound RAM isn't much. A long piece of music or a lengthy voice line won't fit.
So games **stream** it: they keep only a small chunk in sound RAM at a time, and the CPU
continuously refills that chunk from the disc as it plays — like pouring water through a
funnel that never fills up. The refilled region is called a **ring buffer** (playback
chases the writer around a circular patch of memory).

This is the case you most often want to trace, because "where is this streamed audio
coming from?" has a real answer on the disc — and the tool can follow the trail all the
way there.

### How the disc is addressed: FAD

To talk about "where on the disc" something is, you need an address. Saturn CD addresses
are called **FAD** — *Frame Address*. A "frame" here is one CD sector (a 2,352-byte block,
the smallest unit the drive reads).

There's one quirk worth knowing: **FAD = LBA + 150**. LBA (*Logical Block Address*) is the
plain sector number counting from the start of the data. FAD adds 150 because a CD has a
2-second lead-in gap (150 sectors) before the data begins. The tool handles this
conversion for you, but that's why you'll sometimes see both numbers.

Once you know the FAD the drive is reading, you can look up **which file** on the disc
contains that sector — and that's the final link between "I hear this sound" and "it's
this file."

---

## Part 2 — The tool's audio features

Each feature below inspects a different stage of the pipeline. Together they cover the
whole journey from "a voice is sounding" to "here's the file on the disc."

### 🎚️ Sound (SCSP) panel — *what's playing right now*

A live table with **one row per voice** (all 32). For each sounding voice you see its
format, pitch (in Hz), envelope stage and level, pan, loop points, and how far playback
has progressed. Silent voices are greyed out, so at a glance you can tell *how many* and
*which* sounds are active this instant.

Two buttons per row make it hands-on:

- **Play** — preview that exact sample through your speakers, right in the app.
- **Export** — save the sample as a `.wav` file you can open in any audio editor.

**Use it to:** identify which voices make up a scene, hear each one in isolation, and pull
out individual sound effects, instruments, or voice clips.

### 🧠 Sound RAM tab — *the raw sound memory*

A hex view of the full 512 KB of sound RAM — both the 68000's sound program and the
sample data. A voice's "start address" in the Sound panel links straight here, so you can
jump to the exact bytes a sound is playing. You can also **edit** it live, which is how a
music- or effect-swap experiment begins.

**Use it to:** see the actual sample bytes, confirm what a voice points at, or poke at
sound memory to test a change.

### 🔎 Access Log — *what code touched this memory*

Point it at an address in sound RAM and it records **which instructions read or wrote it**,
along with the call stack (the chain of functions that led there). For streamed audio,
this is how you find the routine that's refilling the ring buffer — the code responsible
for feeding the sound.

**Use it to:** answer "what part of the game is writing this audio into memory?" and walk
back up to the code that decided to play it.

### 💿 Disc Explorer — *the disc's files, and the live sector readout*

Two things in one panel:

1. **A file browser** for the game's disc image. It reads the disc's filesystem (ISO 9660,
   the standard CD layout) and lists every file with its size and its **byte offset inside
   the image** — so you can pull a file straight out of the disc. It's sorted biggest-first
   by default, because streamed audio is usually one of the largest files.

2. **A live "drive" readout.** When you're connected to a running game, it shows what the
   CD drive is doing *this frame* — its status (reading / seeking / playing CD-DA / paused)
   and the exact **FAD** it's reading. Best of all, it **automatically names the file** that
   FAD falls in. So the moment a track starts streaming, you'll see something like:

   > **Live drive: reading** · FAD 91234 → `/SOUND/BATTLE01.PCM` (+40960)

   That's the payoff: the sound you're hearing, resolved to a filename and an offset inside
   it, in real time.

You can also type any sector or FAD in by hand to resolve it manually.

### 🎛️ Sound-CPU (68000) debugging — *the sound program itself*

The tool can disassemble and debug the 68000 sound processor — the little CPU running the
music engine. You can read its code and step through the logic that sequences the audio.

**Use it to:** understand *how* a game drives its music, not just what samples it uses.

---

## Part 3 — Putting it together

Here's the end-to-end workflow the audio tools are built around. Say you hear a piece of
music during a battle and want to find and extract it:

1. **See what's sounding.** Open the **Sound (SCSP) panel**. Watch which voices light up
   when the music plays. Use **Play** to confirm you've found the right ones, and **Export**
   to save any voice as a `.wav`.

2. **Is it streamed?** If the music is long, it's almost certainly streamed from disc. Note
   the voice's start address and jump into the **Sound RAM tab** to see the buffer it's
   playing from.

3. **Find the feeder (optional).** Point the **Access Log** at that buffer to catch the code
   refilling it — useful if you want to understand or modify the streaming.

4. **Find it on the disc.** Open the **Disc Explorer**. With the game running, the **live
   drive readout** shows the FAD being read and names the file — e.g.
   `/SOUND/BATTLE01.PCM`. That's your source file.

5. **Extract it.** Grab the file's image offset from the Disc Explorer to pull the raw
   stream out of the disc image, or just use the Sound panel's **Export** for individual
   voices.

The same chain works for sound effects and voice clips — they're just shorter and usually
already fully resident in sound RAM (skip the streaming steps).

---

## A few things to watch out for

- **CD-DA (Red Book) tracks are invisible to the sound tools.** If a game plays music as a
  plain CD audio track, it bypasses sound RAM entirely — so the Sound panel won't show it
  and there are no samples to export. The live drive readout will still show
  **"playing (CD-DA)"** and a FAD, but that FAD points into an audio track, not a file, so
  it'll say *"(no file; audio track or gap)."* **This is expected**, not a bug — it's the
  tool correctly telling you "this audio doesn't live in the filesystem." To extract CD-DA,
  you rip the audio track itself, the same as ripping a music CD.

- **Streamed vs. resident.** Short sounds sit entirely in sound RAM and are easy to grab.
  Long sounds are streamed and only partly present at any moment — which is exactly why the
  disc-side tools (live FAD + Disc Explorer) exist.

- **Live features need the emulator hookup.** The real-time panels (live voices, live drive
  readout) work when Saturn Explorer is connected to a running emulator that's been built
  with its data taps. If you've just updated the tool, re-run **`update.bat`** once so the
  emulator patcher installs the latest hooks — after that the live readouts light up.

---

## Glossary

- **SCSP** — the Saturn's sound chip; plays up to 32 samples at once with pitch/volume/pan.
- **Voice / slot** — one of the SCSP's 32 simultaneous playback channels.
- **Sound RAM** — 512 KB of memory holding the sound program and the audio samples.
- **Sound CPU / 68000** — the small dedicated processor that runs the music engine.
- **PCM** — uncompressed audio; the Saturn uses 8-bit or 16-bit PCM.
- **Envelope** — how a sound's volume changes over time (fade in, hold, fade out).
- **Streaming / ring buffer** — playing audio too big for memory by continuously refilling
  a small circular buffer from disc.
- **CD-DA** — plain CD audio tracks (like a music CD); mixed in hardware, bypasses the SCSP.
- **FAD** — *Frame Address*, a CD sector address. **FAD = LBA + 150.**
- **LBA** — *Logical Block Address*, the plain sector number from the start of the data.
- **ISO 9660** — the standard filesystem layout on a CD, used to list the disc's files.
