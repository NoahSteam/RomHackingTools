#!/bin/bash
# Area 3 round 38 review: each fixture of fixtures.src through run.py (the generator's SE_GENERATED_WORDS mode, so A11-33's oracle runs on it).
# Prints per fixture: function 0's return lines; each caution with the call printed after it ("+likewise" / "+likewise-other"); the frame,
# slot and stack-argument notes (first words); the oracle's caution counts; and every finding line.
# usage: W=<SaturnExplorer dir> S=<scratch dir> run.sh [fixture[:KEY=VAL,...] ...]   (default: every fixture of fixtures.src)
set -o pipefail
here=$(cd "$(dirname "$0")" && pwd)
status=0
names=${*:-$(grep -oE '^name [A-Za-z0-9_]+' "$here/fixtures.src" | cut -d' ' -f2)}
for spec in $names; do
    f=${spec%%:*}; extra=""; [ "$spec" != "$f" ] && extra=$(echo "${spec#*:}" | tr ',' ' ')
    python3 -I "$here/run.py" "$f" $extra 2>&1 | python3 -I -c '
import re, sys
out = sys.stdin.read()
if "RUN FAILED" in out or not re.search(r"^\d+ finding\(s\)$", out, re.M):
    sys.stdout.write(out); print("%s: FAILED (see above)" % sys.argv[1]); sys.exit(1)
body = out[out.find("function 0 at"):]
body = body[:body.find("finding(s)")] if "finding(s)" in body else body
flat = re.sub(r"\s+", " ", body)
rets = re.findall(r"return [^;]*;", flat[flat.find("decompiled"):])
notes = []
for m in re.finditer(r"/\* (.*?) \*/", flat):
    c = m.group(1)
    if c.startswith("WARNING"): continue
    call = re.search(r"func_0x[0-9a-f]{8}", flat[m.end():])
    mm = re.match(r"this call may change ((?:(?:r\d+|gbr|vbr)(?:, | and )?)+)", c)
    kind = ("caution " + mm.group(1).replace(" and ", ",").replace(", ", ",").rstrip(",")) if mm else c[:40]
    notes.append("%s@%s%s" % (kind, call.group(0)[7:] if call else "?", " +likewise" if "likewise at the calls below" in c else " +likewise-other" if "likewise at other calls" in c else ""))
cnt = re.search(r"^checks: (.*)$", out, re.M)
cc = " ".join(x for x in (cnt.group(1).split() if cnt else []) if x.startswith("caution.") or x.startswith("excluded.stale"))
finds = sorted(set(re.findall(r"^  \[([nv]:[^\]]+)\]", out, re.M)))
if int(re.search(r"^(\d+) finding\(s\)$", out, re.M).group(1)) and not finds:
    sys.stdout.write(out); print("%s: FAILED (a nonzero finding count with no finding line)" % sys.argv[1]); sys.exit(1)
print("%s: %s | %s | %s | findings %s" % (sys.argv[1], " ".join(rets), "; ".join(notes) or "no note", cc, ",".join(finds) or "none"))
for l in out.splitlines():
    if l.startswith("  [") and "caution" in l: print("    " + l.strip()[:400])
' "$spec" || status=1
done
exit $status
