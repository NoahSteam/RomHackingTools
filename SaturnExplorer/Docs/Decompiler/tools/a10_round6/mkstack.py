import os, shutil, subprocess, sys, glob
W = "/home/user/RomHackingTools/.claude/worktrees/agent-a0d9c8ee0454a4784/SaturnExplorer/FrontEnd/third_party/ghidra-decompiler"
S = "/tmp/claude-0/r6/stack"
shutil.rmtree(S, ignore_errors=True)
os.makedirs(S + "/Ghidra/Features/Decompiler/src/decompile")
os.makedirs(S + "/Ghidra/Processors/SuperH/data")
shutil.copytree(W + "/cpp", S + "/Ghidra/Features/Decompiler/src/decompile/cpp")
shutil.copytree(W + "/processors/SuperH", S + "/Ghidra/Processors/SuperH/data/languages")


def g(*a, ok=True):
    r = subprocess.run(["git", "-c", "user.email=a@b", "-c", "user.name=x"] + list(a), cwd=S, capture_output=True, text=True)
    if ok and r.returncode:
        print("FAIL", a, r.stderr[:300])
    return r.returncode


g("init", "-q")
g("add", "-A")
g("commit", "-qm", "final")
ps = sorted(glob.glob(W + "/patches/*.patch"))
bad = []
for p in reversed(ps):
    if g("apply", "-R", p, ok=False):
        bad.append(os.path.basename(p))
print("reverse failures:", bad)
g("add", "-A")
g("commit", "-qm", "upstream")
