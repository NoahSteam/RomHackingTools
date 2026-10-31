# Cutting a macOS release — the steps still to do

State on 2026-10-25: signing and notarization of a **trial** build worked end to end (Apple returned
"Accepted", the stapled image passes `spctl`, scratch image `SaturnExplorer-trial1-macos.dmg`). That trial
is **not publishable** for two reasons, which these steps fix:

- the local Homebrew `sdl2-compat` / `sdl3` are older than the versions Homebrew publishes source for, so
  `SOURCE.txt` would name the wrong source (the packager refuses this unless `--allow-mismatch` is given);
- the local `_emu/mednafen` build record is no longer Saturn-only (probably an incremental `update.sh`).

Background and the rules the packager enforces: `Integration/DISTRIBUTION.md` ("Cutting a release",
"macOS Developer ID signing and notarisation"). Windows is verified separately
(`Docs/WindowsReleaseVerification.md`).

Prerequisites already in place: a **Developer ID Application** certificate in the login keychain
(`security find-identity -v -p codesigning`), and the notarization profile `se-notary`
(`xcrun notarytool store-credentials`). Click **Always Allow** if macOS asks whether `codesign` may use the key.

> **Alternative: let GitHub do all of it.** The release workflow builds the archive on a runner with current
> Homebrew packages (so steps 1 and 2 below are not needed there) and its `macos-sign` job signs, notarises and
> staples it, once the secrets in `Integration/DISTRIBUTION.md` ("Signing in CI") are set up. Push a `v*` tag, approve
> the signing run, and the draft release holds the notarized `.dmg`. The steps below are the by-hand route on this Mac.
> To notarize an archive CI built, on this Mac: `python3 Integration/notarize_release.py --dmg <ad hoc .dmg> --out <signed .dmg> --keychain-profile se-notary`.

## 1. Bring the Homebrew libraries up to date

```sh
brew upgrade sdl2-compat sdl3
```

This changes packages other builds on this Mac also use; rebuild anything that links them afterwards.
(The CI runner always has the current versions, so this only matters for a local release.)

## 2. Rebuild Mednafen, Saturn-only, from scratch

Not `update.sh` / `--incremental`: an incremental run keeps the old configuration and the old checkout.

```sh
cd SaturnExplorer
python3 Integration/install.py --yes --no-se --mednafen-saturn-only
```

(`--no-se` skips the Saturn Explorer build; the next step builds the signed app.) The packager will refuse
unless the result matches `Integration/Mednafen/UPSTREAM.json`: pinned commit, sources identical to the
pinned base plus the patch, the Saturn-only and rewind flags in `.se-configure`, and no uncommitted changes in
`Integration/`. Commit or stash first.

## 3. Build, package, notarize, staple, check

```sh
ID="Developer ID Application: Rizwan Ahmed (C4LMSS93H3)"
LABEL=v1.0                                   # the release tag; SOURCE.txt names it, so tag the commit too

# SE_RELEASE_VERSION bakes the tag into the app, so Check for Updates can compare it with the latest GitHub
# release (without it the app calls itself "not a tagged release"). It is read by a script that runs during the
# BUILD, so it must be exported for the whole sequence, not just the configure line. SE_DEMO_MENU=OFF removes the Demo menu.
export SE_RELEASE_VERSION="$LABEL"
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DSE_MACOS_BUNDLE_LIBS=ON \
      -DSE_DEMO_MENU=OFF -DSE_MACOS_SIGN_IDENTITY="$ID"
cmake --build build-release --parallel 8 --target SaturnExplorerFrontEnd
cmake --install build-release --prefix "$PWD/stage-release"      # signs the app; fails if it does not verify

python3 Integration/package_release.py --se-app stage-release/SaturnExplorer.app \
        --mednafen _emu/mednafen/src/mednafen --ref "$LABEL" --out dist --sign-identity "$ID"

xcrun notarytool submit dist/SaturnExplorer-$LABEL-macos.dmg --keychain-profile se-notary --wait
xcrun stapler staple dist/SaturnExplorer-$LABEL-macos.dmg
spctl -a -vvv -t install dist/SaturnExplorer-$LABEL-macos.dmg    # expect: accepted, Notarized Developer ID
grep -a -c "$LABEL" stage-release/SaturnExplorer.app/Contents/MacOS/SaturnExplorer   # the tag is in the binary (non-zero)
```

If notarization says **Invalid**, fetch the reasons with
`xcrun notarytool log <submission-id> --keychain-profile se-notary` (usually an unsigned or non-hardened
nested binary).

Finally test the file as a downloader gets it: copy it, `xattr -w com.apple.quarantine "0083;$(printf '%x' $(date +%s));Safari;" copy.dmg`,
mount it, `spctl -a -vvv -t exec` the app, and run `SaturnExplorer.app/Contents/MacOS/SaturnExplorer --selftest`.

## Afterwards

- Tag the commit (`git tag $LABEL && git push origin $LABEL`) — `SOURCE.txt` refers to that tag.
- Upload the `.dmg` (and the Windows zip) to a GitHub release, with the notes from the workflow's draft, and
  attach a `SHA256SUMS-macos.txt` made from *your* notarized file (`shasum -a 256`), not the one from CI.
- **Publish** the release (a draft is invisible to Check for Updates), and name the tag `v<number>[.<number>...]`.
- Record the version, commit and `spctl` output in `Docs/FunctionalityVerification/`.
- Back up the signing certificate as a `.p12` (Keychain Access → My Certificates → Export), kept outside the repo.
