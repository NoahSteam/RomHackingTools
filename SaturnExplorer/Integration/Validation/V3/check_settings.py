#!/usr/bin/env python3
"""Check an override-settings file against `mednafen -dump_settings_def FILE` of the emulator
that will run it. Mednafen silently ignores unknown names in an override file, so a misspelt
or renamed setting would otherwise run unpinned without a word.

usage: check_settings.py settings_def.txt pinned.cfg
exit 0 all names exist and every value is legal for its type, 1 otherwise (reasons on stderr).

Dump format (Mednafen settings.cpp, SettingsManager::DumpDef), one record per setting:
  name / flags / description / description_extra / type / default / minimum / maximum /
  enum count / {value, description, description_extra} * count / alias count / aliases...
"""
import sys


def parse_def(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().split("\n")
    out = {}
    i = 0
    while i + 8 < len(lines):
        name, flags, typ = lines[i], lines[i + 1], lines[i + 4]
        lo, hi = lines[i + 6], lines[i + 7]
        try:
            n = int(lines[i + 8])
        except ValueError:
            break
        enums = [lines[i + 9 + 3 * k] for k in range(n)]
        j = i + 9 + 3 * n
        try:
            na = int(lines[j])
        except (ValueError, IndexError):
            break
        out[name] = {"type": typ, "flags": flags.split(), "min": lo, "max": hi, "enums": enums}
        for a in lines[j + 1:j + 1 + na]:
            out.setdefault(a, out[name])
        i = j + 1 + na
    return out


def check(defs, cfg_path):
    errs = []
    with open(cfg_path, encoding="utf-8") as f:
        for ln, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line or line.startswith(";"):
                continue
            if " " not in line:
                errs.append("%s:%d: no value" % (cfg_path, ln))
                continue
            name, value = line.split(" ", 1)
            d = defs.get(name)
            if d is None:
                errs.append("%s: this emulator has no setting '%s'" % (cfg_path, name))
                continue
            t = d["type"]
            if t == "MDFNST_BOOL":
                if value not in ("0", "1"):
                    errs.append("'%s' is boolean, got '%s' (must be 0 or 1)" % (name, value))
            elif t in ("MDFNST_ENUM",):
                if value not in d["enums"]:
                    errs.append("'%s' accepts %s, got '%s'" % (name, "|".join(d["enums"]), value))
            elif t in ("MDFNST_INT", "MDFNST_UINT", "MDFNST_FLOAT"):
                try:
                    v = float(value)
                except ValueError:
                    errs.append("'%s' is numeric, got '%s'" % (name, value))
                    continue
                if d["min"] and v < float(d["min"]) or d["max"] and v > float(d["max"]):
                    errs.append("'%s' = %s is outside [%s, %s]" % (name, value, d["min"], d["max"]))
    return errs


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 1
    defs = parse_def(argv[1])
    if not defs:
        print("no settings parsed from " + argv[1], file=sys.stderr)
        return 1
    errs = check(defs, argv[2])
    for e in errs:
        print("preflight: " + e, file=sys.stderr)
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
