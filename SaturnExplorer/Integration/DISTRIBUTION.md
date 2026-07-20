# Distribution & building the emulator side

The live tap patches a *third-party* emulator (Yabause, Mednafen/Beetle). That raises
two practical questions — how to keep the patcher from breaking when upstream moves,
and whether you can hand users a prebuilt binary. Short answers below; none of this is
legal advice.

## Fork and pin your upstream

`apply.py` injects at specific upstream symbols and code sites (`espec->MasterCycles`,
the `ss/Makefile.am` source list, `DBG_AddBreakPoint`, `VDP2::RawRegs`, `CheatMemWrite`,
Yabause's `Vdp2VBlankOUT` / `MappedMemoryWriteByteNocache`, …). If upstream renames or
moves one, the anchor misses. It fails **loudly** — the patcher prints `ANCHOR MISS`
and the injected accessors become compile errors, never silent corruption — but that's
still a broken build until someone re-anchors it.

So:

- **Fork the emulator and pin the patcher to a known-good commit.** That's your stable
  base; you update deliberately, not whenever upstream shifts.
- **Periodically run `apply.py --check` against fresh upstream** to detect drift early —
  it reports exactly which anchor moved, so you can re-anchor before it bites.

**Mednafen vs. Beetle Saturn:** they share the `ss/` Saturn core but are *different
build targets*. `Integration/Mednafen/apply.py` targets **standalone `mednafen-git`**
(autotools: `src/ss/Makefile.am`, `./configure`, `--enable-debugger` for breakpoints) —
that's the path verified end-to-end (it builds + links into a real Mednafen 1.32.1
binary). Beetle Saturn wraps the same core in the libretro API with a different build
and hook sites (`retro_run` instead of `Emulate()`), so its accessors would port but
the build wiring + lifecycle hooks need a separate Beetle patcher. Pick one to support
first; standalone Mednafen is the tested one.

## Prebuilt binaries: Yabause yes (with source), Mednafen no

Both emulators are GPLv2, so *legally* you may distribute a modified binary **as long as
you also provide the corresponding modified source** (which you already do — the patch
is public). But policy and practicality differ:

- **Yabause** — fine to ship a prebuilt patched binary alongside a link to the patched
  source. Standard GPL compliance.
- **Mednafen** — the project **explicitly discourages redistributing modified "Mednafen"
  builds** (an author's-wish / naming concern), even though GPL permits it with source.
  The community-accepted path is what this repo does: **ship the patch, let users build.**
- **The blocker that hits both:** a Saturn emulator needs a **Saturn BIOS**, which is
  copyrighted and **cannot be redistributed**. A "double-click this exe" bundle still
  can't include the one file users must supply themselves (plus disc images). So the
  BIOS step is the real user friction, not the build.

**Recommendation:** ship the **patch + one-command setup** (below), optionally a prebuilt
**Yabause** binary (GPL-clean, with a source link) to lower friction; keep Mednafen
patch-only. Document the BIOS-is-yours-to-supply step prominently either way.

## The one-command setup users run

```sh
# Yabause:
python3 Integration/Yabause/apply.py  /path/to/yabause     # then build Yabause
# Mednafen (add --with-pause for the pause/step gate; --enable-debugger for breakpoints):
python3 Integration/Mednafen/apply.py /path/to/mednafen --with-pause
cd /path/to/mednafen && ./configure --enable-debugger && make
```

Both patchers are idempotent (`--check` to preview, `--revert` to undo). Then launch
Saturn Explorer and **File → Connect to emulator (live)** (or start it with `--live`).
For **browser** live viewing, see the "Web (browser) live viewing" section in
`Integration/Yabause/README.md` (build with `-DSE_WEB_LIVE=ON`, serve cross-origin
isolated, run a WebSocket→TCP relay).
