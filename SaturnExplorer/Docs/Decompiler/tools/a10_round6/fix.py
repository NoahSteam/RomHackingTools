"""fix.py: from the scratch tree (final + family2.diff) re-add the declarations the dependants' remaining code needs, then write family3.diff (final -> scratch)."""
import os, re, subprocess
WT = "/home/user/RomHackingTools/.claude/worktrees/agent-a0d9c8ee0454a4784"
R = "SaturnExplorer/FrontEnd/third_party/ghidra-decompiler"
S = "/tmp/claude-0/r6/scr/" + R + "/cpp"


def edit(name, old, new):
    p = S + "/" + name
    t = open(p).read()
    assert t.count(old) == 1, (name, old, t.count(old))
    open(p, "w").write(t.replace(old, new))


edit("coreaction.hh", "  static bool callMayReadFrame(const Funcdata &data,const Varnode *vn);",
     "  static void keepGlobalWritesWithoutReturn(Funcdata &data,vector<Varnode *> &worklist);\n"
     "  static bool doomedBlocks(const Funcdata &data,vector<char> &dooms);\n"
     "  static bool globalWriteWithoutReturn(const Funcdata &data,const Varnode *vn,const vector<char> *dooms);\n"
     "  static bool callMayReadFrame(const Funcdata &data,const Varnode *vn);")
edit("coreaction.hh", "public:\n  ActionDeadCode(const string &g)", "public:\n  static bool isFoldedFlowStore(const Funcdata &data,const Varnode *vn);\n  ActionDeadCode(const string &g)")
edit("heritage.hh", "  int4 analysisState;\t///< 0=unanalyzed, 1=analyzed(partial result), 2=analyzed(full result)\n",
     "  int4 analysisState;\t///< 0=unanalyzed, 1=analyzed(partial result), 2=analyzed(full result)\n  bool guessedBelow;\n")
edit("heritage.hh", "step=0; analysisState=0;\n", "step=0; analysisState=0; guessedBelow=false;\n")
edit("funcdata.hh", "  bool isHighOn(void) const", "  bool isFrameLedgerCapped(void) const { return false; }\n  bool isHighOn(void) const")
base = "/tmp/claude-0/r6/base"
import shutil
shutil.rmtree(base, ignore_errors=True)
os.makedirs(base + "/" + R)
shutil.copytree(WT + "/" + R + "/cpp", base + "/" + R + "/cpp")
shutil.copytree(WT + "/" + R + "/processors", base + "/" + R + "/processors")
r = subprocess.run(["git", "diff", "--no-index", "--binary", base + "/" + R + "/cpp", "/tmp/claude-0/r6/scr/" + R + "/cpp"], capture_output=True, text=True)
d = r.stdout.replace(base + "/" + R + "/cpp", R + "/cpp").replace("/tmp/claude-0/r6/scr/" + R + "/cpp", R + "/cpp")
d = d.replace("a/" + "/", "a/").replace("b/" + "/", "b/")
d = d.replace("aSaturnExplorer/", "a/SaturnExplorer/").replace("bSaturnExplorer/", "b/SaturnExplorer/")
open("/tmp/claude-0/r6/family3.diff", "w").write(d)
print("diff lines", d.count("\n"))
