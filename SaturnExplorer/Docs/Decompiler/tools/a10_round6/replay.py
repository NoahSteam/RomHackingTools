"""replay.py <exclude,comma> <out diff> : from the reconstructed upstream (commit HEAD of stack), apply patches in order except the excluded,
skipping (and listing) any that no longer apply; write the diff final->result as a patch for git apply in the vendored layout."""
import glob, os, subprocess, sys
S = "/tmp/claude-0/r6/stack"
W = "/home/user/RomHackingTools/.claude/worktrees/agent-a0d9c8ee0454a4784/SaturnExplorer/FrontEnd/third_party/ghidra-decompiler"
excl = set(x for x in sys.argv[1].split(",") if x)
out = sys.argv[2]


def g(*a):
    return subprocess.run(["git", "-c", "user.email=a@b", "-c", "user.name=x"] + list(a), cwd=S, capture_output=True, text=True)


up = g("rev-parse", "HEAD").stdout.strip()
final = g("rev-parse", "HEAD~1").stdout.strip()
g("checkout", "-q", "--detach", up)
g("reset", "-q", "--hard", up)
skipped = []
for p in sorted(glob.glob(W + "/patches/*.patch")):
    n = os.path.basename(p)[:4]
    if n in excl:
        continue
    r = g("apply", "--check", p)
    if r.returncode:
        if os.environ.get("PARTIAL"):
            rr = g("apply", "--reject", p)
            rej = [l for l in rr.stderr.splitlines() if "rejected hunk" in l or "Rejected hunk" in l]
            skipped.append(n + "(partial, %d hunks rejected)" % len(rej))
            for f in glob.glob(S + "/**/*.rej", recursive=True):
                os.remove(f)
        else:
            skipped.append(n)
        continue
    g("apply", p)
g("add", "-A")
g("-c", "core.autocrlf=false", "commit", "-qm", "result", "--allow-empty")
res = g("rev-parse", "HEAD").stdout.strip()
d = g("diff", "--binary", res, final).stdout   # result -> final: applying it to the final tree... (we want final -> result)
d2 = g("diff", "--binary", final, res).stdout
d2 = d2.replace("a/Ghidra/Features/Decompiler/src/decompile/cpp/", "a/SaturnExplorer/FrontEnd/third_party/ghidra-decompiler/cpp/").replace("b/Ghidra/Features/Decompiler/src/decompile/cpp/", "b/SaturnExplorer/FrontEnd/third_party/ghidra-decompiler/cpp/")
d2 = d2.replace("a/Ghidra/Processors/SuperH/data/languages/", "a/SaturnExplorer/FrontEnd/third_party/ghidra-decompiler/processors/SuperH/").replace("b/Ghidra/Processors/SuperH/data/languages/", "b/SaturnExplorer/FrontEnd/third_party/ghidra-decompiler/processors/SuperH/")
open(out, "w").write(d2)
print("excluded", sorted(excl), "skipped (dependants)", skipped, "diff lines", d2.count("\n"))
g("checkout", "-q", "--detach", up)
