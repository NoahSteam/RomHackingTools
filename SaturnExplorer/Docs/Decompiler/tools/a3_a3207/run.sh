#!/bin/bash
# Area 3, A3-207: this fix's fixtures (fixtures.src, through run.py). For each: function 0's return lines; each caution ("this call may
# change ...": the registers) and each frame note ("frame": a frame address ...) with the call printed after it and "+likewise" when it says
# "likewise at the calls below"; the finding count.
# usage: W=<SaturnExplorer dir> S=<scratch dir> run.sh [fixture ...]   (default: every fixture of fixtures.src)
here=$(cd "$(dirname "$0")" && pwd)
names=${*:-$(grep -oE '^name [a-z0-9]+' "$here/fixtures.src" | cut -d' ' -f2)}
for f in $names; do
    python3 -I "$here/run.py" "$f" 2>&1 | python3 -I -c '
import re, sys
out = sys.stdin.read()
body = out[out.find("function 0 at"):]
body = body[:body.find("finding(s)")]
flat = re.sub(r"\s+", " ", body)
rets = re.findall(r"return [^;]*;", flat[flat.find("decompiled"):])
notes = []
for m in re.finditer(r"/\* (.*?) \*/", flat):
    c = m.group(1)
    call = re.search(r"func_0x[0-9a-f]{8}", flat[m.end():])
    kind = ("caution " + re.match(r"this call may change ((?:(?:r\d+|gbr|vbr)(?:, | and )?)+)", c).group(1).replace(" and ", ",").replace(", ", ",").rstrip(",")) if c.startswith("this call may change") else "frame" if c.startswith("a frame address") else c[:30]
    notes.append("%s@%s%s" % (kind, call.group(0)[7:] if call else "?", " +likewise" if "likewise at the calls below" in c else " +likewise-other" if "likewise at other calls" in c else ""))
print("%s: %s | %s | findings %d" % (sys.argv[1], " ".join(rets), "; ".join(notes) or "no note", out.count("\n  [")))
' "$f"
done
