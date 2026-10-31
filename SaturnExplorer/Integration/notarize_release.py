#!/usr/bin/env python3
"""
notarize_release.py - Developer ID sign, notarise and staple a macOS archive made by package_release.py.

package_release.py leaves the archive ad hoc signed unless it was given a signing identity. This takes that
.dmg, replaces every signature with a Developer ID one (hardened runtime, timestamped), makes a new .dmg,
signs it, has Apple notarise it, staples the ticket and checks the result with Gatekeeper's own assessment.
Nothing is rebuilt: the contents are the archive's, only the signatures change. It is what the release
workflow's signing job runs, and it works the same on a Mac that holds the certificate:

  python3 Integration/notarize_release.py --dmg dist/SaturnExplorer-v1.0-macos.dmg --out dist/signed/SaturnExplorer-v1.0-macos.dmg \\
          --keychain-profile se-notary

Signing identity: --identity, else the (only) "Developer ID Application" identity in the keychain.

Notary credentials, first match wins:
  --keychain-profile NAME                          a profile saved with `xcrun notarytool store-credentials`
  NOTARY_KEY_P8 + NOTARY_KEY_ID + NOTARY_ISSUER_ID an App Store Connect API key (the contents of the .p8 file)
  NOTARY_APPLE_ID + NOTARY_TEAM_ID + NOTARY_PASSWORD   an Apple ID with an app-specific password
They are read from the environment, never from arguments, and the key file is written to a private temporary
folder that is removed afterwards.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


def out(cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def find_identity(requested):
    if requested:
        return requested
    lines = [l for l in out(["security", "find-identity", "-v", "-p", "codesigning"]).splitlines()
             if "Developer ID Application" in l]
    names = sorted({re.search(r'"(.+)"', l).group(1) for l in lines})
    if len(names) != 1:
        raise SystemExit("expected exactly one 'Developer ID Application' identity in the keychain, found "
                         f"{len(names)}; pass --identity")
    return names[0]


def sign(path, identity, deep=False):
    cmd = ["codesign", "--force", "--timestamp", "--options", "runtime", "--sign", identity]
    run(cmd + (["--deep"] if deep else []) + [path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def verify(path, deep=False):
    r = subprocess.run(["codesign", "--verify", "--strict"] + (["--deep"] if deep else []) + [path],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"{path} does not verify after signing:\n{r.stderr}")


def notary_args(profile, scratch):
    """The notarytool credential arguments, and the temporary files they need."""
    if profile:
        return ["--keychain-profile", profile]
    env = os.environ
    if env.get("NOTARY_KEY_P8") and env.get("NOTARY_KEY_ID") and env.get("NOTARY_ISSUER_ID"):
        key = os.path.join(scratch, "AuthKey.p8")
        fd = os.open(key, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(env["NOTARY_KEY_P8"])
        return ["--key", key, "--key-id", env["NOTARY_KEY_ID"], "--issuer", env["NOTARY_ISSUER_ID"]]
    if env.get("NOTARY_APPLE_ID") and env.get("NOTARY_TEAM_ID") and env.get("NOTARY_PASSWORD"):
        return ["--apple-id", env["NOTARY_APPLE_ID"], "--team-id", env["NOTARY_TEAM_ID"], "--password", env["NOTARY_PASSWORD"]]
    raise SystemExit("no notarization credentials: give --keychain-profile, or NOTARY_KEY_P8/NOTARY_KEY_ID/NOTARY_ISSUER_ID, "
                     "or NOTARY_APPLE_ID/NOTARY_TEAM_ID/NOTARY_PASSWORD")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dmg", required=True, help="the archive made by package_release.py")
    ap.add_argument("--out", required=True, help="where to write the signed, notarised, stapled .dmg")
    ap.add_argument("--identity", help="codesign identity (default: the only Developer ID Application one)")
    ap.add_argument("--keychain-profile", help="a saved notarytool credential profile")
    ap.add_argument("--volname", default="Saturn Explorer")
    args = ap.parse_args()

    identity = find_identity(args.identity)
    print("signing as", identity)
    work = tempfile.mkdtemp(prefix="se_notarize_")
    os.chmod(work, 0o700)
    mount = os.path.join(work, "mnt")
    stage = os.path.join(work, "stage")
    try:
        os.makedirs(mount)
        run(["hdiutil", "attach", "-nobrowse", "-readonly", "-quiet", "-mountpoint", mount, args.dmg])
        try:
            run(["ditto", mount, stage])          # keeps symlinks, modes and extended attributes
        finally:
            subprocess.run(["hdiutil", "detach", "-quiet", mount])
        app = os.path.join(stage, "SaturnExplorer.app")
        emu = os.path.join(stage, "mednafen-se")
        if not os.path.isdir(app) or not os.path.isfile(os.path.join(emu, "mednafen")):
            raise SystemExit("the image does not hold SaturnExplorer.app and mednafen-se/mednafen")

        # Libraries first, then what loads them, as package_release.py does; the app is signed as a bundle.
        libs = sorted(os.path.join(emu, "lib", f) for f in os.listdir(os.path.join(emu, "lib")))
        for path in libs + [os.path.join(emu, "mednafen")]:
            sign(path, identity)
        sign(app, identity, deep=True)
        for path in libs + [os.path.join(emu, "mednafen")]:
            verify(path)
        verify(app, deep=True)

        os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
        if os.path.exists(args.out):
            os.remove(args.out)
        run(["hdiutil", "create", "-quiet", "-volname", args.volname, "-srcfolder", stage, "-format", "UDZO", args.out])
        sign(args.out, identity)
        verify(args.out)

        creds = notary_args(args.keychain_profile, work)
        print("submitting to the Apple notary service (this waits for the verdict)...")
        r = subprocess.run(["xcrun", "notarytool", "submit", args.out, *creds, "--wait", "--output-format", "json"],
                           capture_output=True, text=True)
        try:
            result = json.loads(r.stdout)
        except ValueError:
            raise SystemExit("notarytool gave no result:\n" + r.stdout + r.stderr)
        print("notarization:", result.get("status"), result.get("id"))
        if result.get("status") != "Accepted":
            log = subprocess.run(["xcrun", "notarytool", "log", result.get("id", ""), *creds],
                                 capture_output=True, text=True)
            raise SystemExit("Apple did not accept the archive:\n" + log.stdout + log.stderr)
        run(["xcrun", "stapler", "staple", args.out], stdout=subprocess.DEVNULL)
        run(["xcrun", "stapler", "validate", args.out], stdout=subprocess.DEVNULL)
        a = subprocess.run(["spctl", "-a", "-vvv", "-t", "install", args.out], capture_output=True, text=True)
        text = a.stdout + a.stderr
        print(text.strip())
        if a.returncode != 0 or "Notarized Developer ID" not in text:
            raise SystemExit("Gatekeeper did not accept the stapled archive")
        print("done:", args.out)
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
