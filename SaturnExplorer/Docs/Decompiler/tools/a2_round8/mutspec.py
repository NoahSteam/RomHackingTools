#!/usr/bin/env python3
# Area 2 round 8 instrument: mutants of superh.sinc against the decode oracle, in a scratch COPY of the tree (se-regen-sla rewrites sh-2.sla and Sh2SpecData.h there).
# usage: A2_MUT_ROOT=<dir> mutspec.py list|run <first> <count> [seed]; results in $A2_MUT_ROOT/mut_spec.results
"""Spec mutants of superh.sinc in the scratch copy: mutate, regenerate sh-2.sla and Sh2SpecData.h there, rebuild the decode test, run it.
usage: mut_spec.py list|run <first> <count> [seed]"""
import re, random, subprocess, sys, os, shutil, time
T = os.environ["A2_MUT_ROOT"]   # a scratch directory with inst/SaturnExplorer (a copy of the tree) and inst/build (its build)
SRC = T + "/inst/SaturnExplorer"
SINC = SRC + "/FrontEnd/third_party/ghidra-decompiler/processors/SuperH/superh.sinc"
ORIG = T + "/superh.sinc.orig"
if not os.path.exists(ORIG): shutil.copy(SINC, ORIG)
lines = open(ORIG).read().split("\n")

def in_body_lines():
    out = []; depth = 0; start = None; stack = []
    for i, l in enumerate(lines):
        code = l.split("#")[0]
        t = code.strip()
        if t.startswith("@if"):
            stack.append(('SH_VERSION == "2")' in t) if "SH_VERSION" in t and "||" in t else not ('"2A"' in t or '"1"' in t or "FPU" in t))
            continue
        if t.startswith("@endif"):
            stack.pop(); continue
        if t.startswith("@else"):
            stack[-1] = not stack[-1]; continue
        if stack and not all(stack):
            continue
        if code.lstrip().startswith(":") and "{" not in code: pass
        if re.match(r"^\s*:", code) or re.match(r"^\s*\w+:.*is ", code): pass
        opens = code.count("{"); closes = code.count("}")
        if depth > 0 and code.strip() and not code.strip().startswith("}"):
            out.append(i)
        depth += opens - closes
        if depth < 0: depth = 0
    return out

OPS = [
    (r" \+ ", " - "), (r" - ", " + "), (r" s< ", " < "), (r" < ", " s< "), (r" s<= ", " <= "), (r" <= ", " s<= "), (r" s> ", " > "), (r" > ", " s> "),
    (r" s>= ", " >= "), (r" >= ", " s>= "), (r" & ", " | "), (r" \| ", " & "), (r" ^ ", " | "), (r" << ", " >> "), (r" >> ", " << "), (r" s>> ", " >> "),
    (r" == ", " != "), (r" != ", " == "), (r"sext\(", "zext("), (r"zext\(", "sext("), (r" \* ", " + "), (r" s\* ", " * "), (r" && ", " || "), (r" \|\| ", " && "),
    (r"\b1\b", "2"), (r"\b2\b", "1"), (r"\b4\b", "2"), (r"\b0\b", "1"), (r"\b8\b", "4"), (r"\b31\b", "30"), (r"\b32\b", "31"), (r"\b15\b", "14"), (r"\b16\b", "8"),
]
def candidates():
    cs = []
    for i in in_body_lines():
        l = lines[i]
        code = l.split("#")[0]
        if "local " in code and ":" in code and "export" in code: pass
        for pat, rep in OPS:
            for m in re.finditer(pat, code):
                cs.append((i, m.start(), m.end(), rep))
        if code.strip().endswith(";") and "export" not in code and "goto" not in code and "delayslot" not in code and "build" not in code:
            cs.append((i, -1, -1, "<DELETE>"))
    return cs

def apply(c):
    i, a, b, rep = c
    ls = list(lines)
    if rep == "<DELETE>":
        ls[i] = re.sub(r"\S.*", "", ls[i])
    else:
        code = lines[i]
        # keep any trailing comment
        ls[i] = code[:a] + (rep if not rep.startswith("\\") else rep) + code[b:]
    return "\n".join(ls)

def sh(cmd, timeout=900):
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)
    return r.returncode, r.stdout + r.stderr

def main():
    mode = sys.argv[1]
    cs = candidates()
    rnd = random.Random(int(sys.argv[4]) if len(sys.argv) > 4 else 7)
    order = list(range(len(cs)))
    rnd.shuffle(order)
    first, count = int(sys.argv[2]), int(sys.argv[3])
    if mode == "list":
        print(len(cs), "candidates")
        for k in order[first:first + count]:
            i, a, b, rep = cs[k]
            print(k, i + 1, repr(lines[i][a:b]) if a >= 0 else "DELETE", "->", rep, "|", lines[i].strip()[:100])
        return
    res = open(T + "/mut_spec.results", "a")
    for k in order[first:first + count]:
        i, a, b, rep = cs[k]
        open(SINC, "w").write(apply(cs[k]))
        desc = "%d line %d %s -> %s | %s" % (k, i + 1, repr(lines[i][a:b]) if a >= 0 else "DELETE", rep, lines[i].strip()[:90])
        rc, out = sh("ninja -C %s/inst/build se-regen-sla 2>&1 | tail -5" % T)
        if "error" in out.lower() or "FAILED" in out:
            res.write("COMPILE-FAIL " + desc + "\n"); res.flush(); continue
        rc, out = sh("ninja -C %s/inst/build -j2 SaturnExplorerSh2DecodeTests 2>&1 | tail -3" % T)
        if "FAILED" in out:
            res.write("BUILD-FAIL " + desc + "\n"); res.flush(); continue
        rc, out = sh("cd %s && %s/inst/build/bin/SaturnExplorerSh2DecodeTests %s/runMut 2>&1" % (T, T, T), 600)
        m = re.search(r"differential execution: (\d+) cases, (\d+) differ, (\d+) could not run", out)
        fails = len(re.findall(r"CHECK failed", out))
        killed = fails > 0 or rc != 0
        res.write("%s %s [%s fails=%d]\n" % ("KILLED" if killed else "SURVIVED", desc, m.group(0) if m else "no summary", fails)); res.flush()
    shutil.copy(ORIG, SINC)
    sh("ninja -C %s/inst/build se-regen-sla 2>&1 | tail -1" % T)
main()
