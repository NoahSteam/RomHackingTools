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

> **On authenticity — read this.** Mednafen publishes **no checksum or signature** for its
> release tarballs — there is no SHA-256 on the releases page and no `.sha256` / `.sig` /
> `.asc` / `SHA256SUMS` / `CHECKSUMS` file. So the download **cannot be verified against
> upstream**; your only trust anchor is **HTTPS from the author's own domain**
> (`mednafen.github.io`). Record the hash of what you downloaded anyway — it is a
> *reproducibility baseline* (lets you or another machine confirm a later download is the
> same bytes), but it proves nothing about the original.

```bash
# 1. Grab the current source tarball (check https://mednafen.github.io/releases/ for the
#    latest version; example uses 1.32.1):
VER=1.32.1
curl -fLO "https://mednafen.github.io/releases/files/mednafen-$VER.tar.xz"
sha256sum "mednafen-$VER.tar.xz"   # record this as YOUR baseline (not an upstream check)

tar xf "mednafen-$VER.tar.xz"      # -> ./mednafen/
cd mednafen

# 2. Import byte-for-byte. Neutralize two Windows defaults FIRST, or the import silently
#    stops being a faithful copy:
#     - core.autocrlf=true rewrites LF->CRLF on add. That drifts the source off upstream
#       and can break apply.py's exact-anchor patching AND the MSYS2/autotools build
#       (which expects LF). Force it off for this repo.
#     - a global gitignore / core.excludesFile (e.g. a `*.exe` rule) silently DROPS shipped
#       files — Mednafen ships PSX test .exe binaries. `git add -Af` (-f) forces every file
#       in regardless of any global ignore, so nothing is lost.
git init -b master
git config core.autocrlf false
git config core.safecrlf false
git add -Af
git commit -m "Import Mednafen $VER (official release source, unmodified)"
git tag "v$VER"

# 3. Sanity-check completeness: the committed file set must match the tarball.
tar tf "../mednafen-$VER.tar.xz" | grep -v '/$' | wc -l   # files in the tarball
git ls-files | wc -l                                       # files git committed
#    The two counts must match. The DIRECTION of a mismatch tells you what went wrong:
#      * git  <  tarball : a global ignore ate files -> re-run `git add -Af`.
#      * git  >  tarball : a symlink got DEREFERENCED into a duplicate tree (see below);
#                          `git add -Af` cannot fix this — you would just loop.
```

### Windows symlink gotcha (`git > tarball`)
Mednafen ships **`include/mednafen` as a symlink → `../src`**, and its build reaches every
`#include <mednafen/...>` through it. On Windows, Git sets `core.symlinks=false` by default
(no admin / Developer Mode), and MSYS `tar` then **dereferences** the symlink on extract —
copying the entire `src/` tree (~1,587 files) into `include/mednafen/`. Your import ends up
with `src/` twice and a file count far *above* the tarball. This is the worst failure mode:
`apply.py` patches `src/ss/ss.cpp`, but the duplicate `include/mednafen/ss/ss.cpp` sits
unpatched and the build pulls headers through `include/mednafen/` — the patch looks applied
and then partially isn't.

Fix it by storing the symlink **directly in git's index** (works with `core.symlinks=false`,
no admin/Developer Mode). First see which paths the archive marks as symlinks:

```bash
tar tvf "../mednafen-$VER.tar.xz" | grep '^l'    # type 'l' lines show 'path -> target'
```

For each — Mednafen has exactly one, `include/mednafen -> ../src` — drop the dereferenced
copy and write the symlink blob:

```bash
git rm -r --cached -q include/mednafen && rm -rf include/mednafen
BLOB=$(printf '../src' | git hash-object -w --stdin)
git update-index --add --cacheinfo 120000,"$BLOB",include/mednafen
git ls-files -s include/mednafen        # -> "120000 <blob> 0  include/mednafen" (a symlink)
git commit --amend -m "Import Mednafen $VER (official release source, unmodified)"
```

Re-run the step-3 counts: they should now match exactly, with `include/mednafen` stored as
a symlink and no `include/mednafen/` duplicates. (When you later update to a new Mednafen
release, re-check the counts and re-apply this if the re-extract dereferences again.)

> **Windows checkout / build caveat:** with `core.symlinks=false`, cloning the fork
> materializes `include/mednafen` as a plain text file (`../src`), *not* a working symlink,
> which would break the build's include path. The installer's Mednafen build step now
> recreates the symlink in the MSYS2 shell before `./configure`, so a normal `install.bat`
> run is fine. Building by hand? Run `ln -s ../src include/mednafen` in the checkout first
> (MSYS2 / Git Bash), or clone with `-c core.symlinks=true` (needs Developer Mode / admin).

```bash
# 4. Create an EMPTY, PUBLIC repo named mednafen-git under your account on GitHub (no
#    README/license/gitignore), then push:
git remote add origin https://github.com/NoahSteam/mednafen-git.git
git push -u origin master --tags
```

**Public, not private.** `install.bat` clones anonymously, and a private repo returns
`404 Not Found` to unauthenticated clones — so anyone running the installer (including
you, unless you configure git credentials) needs the fork to be public. That's also
GPL-clean: Mednafen is GPLv2 and redistributing *verbatim source* is expressly permitted.
As a courtesy to the author (who dislikes forks being mistaken for official Mednafen), set
the repo **description** to make the relationship clear, e.g. *"Unmodified mirror of
official Mednafen source (mednafen.github.io) + one `.gitattributes`; build base for
Saturn Explorer, not an official Mednafen distribution."*

**Commit-message note:** the import commit represents *unmodified upstream source*. Keep
the message factual and add **no** authorship / AI co-author trailers — the whole value of
this procedure is that the commit is a verifiable byte-for-byte copy of someone else's
released work; a co-author trailer would misrepresent exactly the provenance it protects.

### Durable byte-preservation (add `.gitattributes`)
The `core.autocrlf false` above is **local config that does not survive a clone** — anyone
(including `install.bat`) who clones the fork on a Windows box with `autocrlf=true` set
globally gets CRLF working files again, re-introducing the drift. The durable fix is a
committed `.gitattributes` that pins every file to no-EOL-conversion. It's one file
upstream doesn't ship, so add it as a **separate commit** — that keeps the import commit
above a clean, auditable byte-for-byte copy:

```bash
printf '* -text\n' > .gitattributes
git add .gitattributes
git commit -m "Add .gitattributes (* -text): byte-exact checkout on every platform"
git push
```

This is a deliberate, tiny deviation from "literal file-for-file purity": a base that
`apply.py` can patch **reliably on any OS** matters more than matching upstream's (absent)
packaging. `* -text` disables EOL munging while leaving diffs working.

**Updating to a newer Mednafen later:** extract the new tarball over the checkout,
`git add -Af`, `git commit -m "Import Mednafen <new-ver> (unmodified)"`, `git tag v<new-ver>`,
`git push`. The `.gitattributes` commit rides along. Each release stays one clean,
diffable commit.

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

- **Keep it pristine.** The fork holds *vanilla* Mednafen (plus the single
  `.gitattributes` above, added as its own commit for byte-preservation). The Saturn
  Explorer edits are applied on top by `apply.py` at install time — never committed to the
  fork — so it stays a clean, updatable base (the "fork and pin" model in
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
