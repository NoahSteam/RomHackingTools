# Creating `NoahSteam/mednafen-git` with clean standard-Mednafen provenance

The installer clones **our fork** `NoahSteam/mednafen-git` and expects it to hold
**standard (standalone) Mednafen** source — the emulator we patch via `ss.cpp`'s
`Emulate()`, *not* the Beetle Saturn libretro core. This guide creates that fork with
the cleanest possible lineage: straight from Mednafen's **official release source**, so
nothing in the history comes from a third-party mirror.

You only do this once. After it exists, `install.bat` uses it every run with no flags.

> Mednafen's author does not publish a browsable git repo ("too cool for git"), and the
> official development source is Mercurial with no widely-advertised public clone URL.
> The most authoritative, reproducible source is therefore the **official release
> tarball** on <https://mednafen.github.io/releases/>. Method 1 uses it. Method 2 is for
> full upstream history if you have the Mercurial URL.

---

## Method 1 — official source tarball → git (recommended)

Byte-for-byte the author's released source; zero third-party lineage.

```bash
# 1. Grab the current source tarball. Check https://mednafen.github.io/releases/ for the
#    latest version + its listed SHA-256, then (example uses 1.32.1):
VER=1.32.1
curl -fLO "https://mednafen.github.io/releases/files/mednafen-$VER.tar.xz"
# Verify it matches the SHA-256 printed on the releases page before trusting it:
sha256sum "mednafen-$VER.tar.xz"

# 2. Unpack and turn it into a git repo.
tar xf "mednafen-$VER.tar.xz"          # -> ./mednafen/
cd mednafen
git init -b master
git add -A
git commit -m "Import Mednafen $VER (official release source)"
git tag "v$VER"

# 3. Create an EMPTY repo named mednafen-git under your account on GitHub
#    (no README/license/gitignore), then push:
git remote add origin https://github.com/NoahSteam/mednafen-git.git
git push -u origin master --tags
```

**Updating to a newer Mednafen later:** download the new tarball, extract it over a
clean checkout (or `rsync --delete` its contents into the working tree), then
`git add -A && git commit -m "Import Mednafen <new-ver>" && git tag v<new-ver> && git push`.
Each release is one clean commit — easy to diff, easy to pin.

---

## Method 2 — Mercurial → git, with full upstream history (optional)

Use this only if you want the complete commit history and you have Mednafen's Mercurial
clone URL (copy it from the **Source**/development section of
<https://mednafen.github.io/> — it wasn't reachable from this environment to hard-code
here). Then convert with `hg-fast-export`:

```bash
pip install mercurial
git clone https://github.com/frej/fast-export.git

hg clone <MEDNAFEN_HG_URL> mednafen-hg        # <-- the confirmed official hg URL
git init -b master mednafen-git
cd mednafen-git
../fast-export/hg-fast-export.sh -r ../mednafen-hg
git checkout HEAD
git remote add origin https://github.com/NoahSteam/mednafen-git.git
git push -u origin --all --tags
```

(`git-remote-hg` is an alternative if you prefer an ongoing hg↔git bridge.)

---

## After the fork exists

- **Keep it pristine.** The fork holds *vanilla* Mednafen. The Saturn Explorer edits are
  applied on top by `apply.py` at install time — never committed to the fork — so it
  stays a clean, updatable base (the "fork and pin" model in
  [`../DISTRIBUTION.md`](../DISTRIBUTION.md)). If you'd rather bake the patch in, do it on
  a separate `saturn-explorer` branch and leave `master` clean.
- **Pin a revision.** For reproducible builds, pin the tag/commit — pass
  `--mednafen-rev v1.32.1` to the installer, or set `rev` in the `EMULATORS` table in
  [`../install.py`](../install.py).
- **Nothing else to configure.** The installer already defaults to
  `NoahSteam/mednafen-git`; `--fork-owner` / `--mednafen-repo` override it if needed.

The same pattern works for the Yabause-lineage forks (`NoahSteam/yabause`,
`yaba-sanshiro`, `Kronos`) — those upstreams *are* on GitHub, so there you can just use
GitHub's **Fork** button instead of importing a tarball.
