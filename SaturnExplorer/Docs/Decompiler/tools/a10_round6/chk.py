"""chk.py <diff> : apply the diff to a scratch copy of the vendored cpp/ and syntax-check every changed .cc and the host sources that include the engine."""
import os, shutil, subprocess, sys
WT = "/home/user/RomHackingTools/.claude/worktrees/agent-a0d9c8ee0454a4784"
R = "SaturnExplorer/FrontEnd/third_party/ghidra-decompiler"
S = "/tmp/claude-0/r6/scr"
shutil.rmtree(S, ignore_errors=True)
os.makedirs(S + "/" + R)
shutil.copytree(WT + "/" + R + "/cpp", S + "/" + R + "/cpp")
shutil.copytree(WT + "/" + R + "/processors", S + "/" + R + "/processors")
r = subprocess.run(["git", "apply", "--recount", sys.argv[1]], cwd=S, capture_output=True, text=True)
print("apply rc", r.returncode, r.stderr[:300])
files = [l.split(" b/")[1].strip() for l in open(sys.argv[1]) if l.startswith("diff --git")]
inc = ["-I" + S + "/" + R + "/cpp", "-I" + WT + "/SaturnExplorer/include", "-I" + WT + "/SaturnExplorer/FrontEnd/src", "-I" + WT + "/SaturnExplorer/FrontEnd/tests"]
todo = [S + "/" + f for f in files if f.endswith(".cc")]
host = [WT + "/SaturnExplorer/FrontEnd/src/Decompiler/Sh2Decompiler.cpp"]
if len(sys.argv) > 2:
    todo = []
nerr = 0
for f in todo + host:
    c = subprocess.run(["g++", "-std=gnu++14", "-fsyntax-only", "-w", "-D_GNU_SOURCE"] + inc + [f], capture_output=True, text=True)
    errs = [l for l in c.stderr.splitlines() if "error" in l]
    nerr += len(errs)
    print(os.path.basename(f), "errors:", len(errs))
    for l in errs[:6]:
        print("   ", l.replace(S + "/" + R + "/", "").replace(WT + "/", "")[:230])
print("total errors", nerr)
