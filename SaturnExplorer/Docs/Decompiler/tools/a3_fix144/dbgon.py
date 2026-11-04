# (area 3 fixer) the temporary DBG76 trace prints used to find A3-144's mechanism (opDestroy, dead code holds); strip.py removes every line tagged DBG76
import sys
D='/home/user/RomHackingTools/.claude/worktrees/a3fix144/SaturnExplorer/FrontEnd/third_party/ghidra-decompiler/cpp/'
def ed(p, old, new):
    s=open(D+p).read()
    if new in s: return
    assert old in s, (p, old)
    s=s.replace(old,new,1); open(D+p,'w').write(s)
ed('funcdata_op.cc','#include "funcdata.hh"\n','#include "funcdata.hh"\n#include <iostream> // DBG76\n')
ed('funcdata_op.cc',"""void Funcdata::opDestroy(PcodeOp *op)

{
""","""void Funcdata::opDestroy(PcodeOp *op)

{
  if (getenv("SE_DBG76") && op->getOut() != (Varnode *)0 && op->getOut()->getSpace()->getType() == IPTR_SPACEBASE) { // DBG76
    std::cerr << "DBG76 destroy ra=" << __builtin_return_address(0) << " pass=" << getHeritagePass() << " "; op->printRaw(std::cerr); std::cerr << std::endl; } // DBG76
""")
ed('coreaction.cc','#include "coreaction.hh"\n','#include "coreaction.hh"\n#include <iostream> // DBG76\n')
ed('coreaction.cc',"""  if (!reads) return;
  VarnodeLocSet::const_iterator viter = data.beginLoc(stackSpc);""","""  if (getenv("SE_DBG76")) { std::cerr << "DBG76 keep cands=" << candidates.size() << " reads=" << reads << " pass=" << data.getHeritagePass() << std::endl; // DBG76
    for(int4 i=0;i<candidates.size();++i) { std::cerr << "DBG76   "; candidates[i]->printRaw(std::cerr); std::cerr << std::endl; } } // DBG76
  if (!reads) return;
  VarnodeLocSet::const_iterator viter = data.beginLoc(stackSpc);""")
ed('coreaction.cc',"""    if (!vn->isAddrForce()) vn->setFrameLoadForce();""","""    if (getenv("SE_DBG76")) { std::cerr << "DBG76 push "; def->printRaw(std::cerr); std::cerr << std::endl; } // DBG76
    if (!vn->isAddrForce()) vn->setFrameLoadForce();""")
ed('coreaction.cc',"""  keepFrameWritesForLoads(data,worklist);	// SaturnExplorer""","""  if (getenv("SE_DBG76") && getenv("SE_DBG76RAW")) { std::cerr << "DBG76 deadcode pass=" << data.getHeritagePass() << " allowed=" << data.deadRemovalAllowed(data.getArch()->getStackSpace()) << std::endl; data.printRaw(std::cerr); } // DBG76
  keepFrameWritesForLoads(data,worklist);	// SaturnExplorer""")
ed('coreaction.cc',"""    if (op->isDead() || op->getIn(1)->isConstant()) continue;
    candidates.push_back(op);""","""    if (getenv("SE_DBG76")) { std::cerr << "DBG76 load "; op->printRaw(std::cerr); std::cerr << std::endl; } // DBG76
    if (op->isDead() || op->getIn(1)->isConstant()) continue;
    candidates.push_back(op);""")
