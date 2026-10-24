import sys
p='/home/user/RomHackingTools/.claude/worktrees/a3r13rev/SaturnExplorer/FrontEnd/third_party/ghidra-decompiler/cpp/heritage.cc'
s=open(p).read()
old="""  iter = fd->beginOp(CPUI_STORE);
  enditer = fd->endOp(CPUI_STORE);
  for(;iter!=enditer;++iter) {
    PcodeOp *op = *iter;
    if (op->isDead()) continue;
    Varnode *ptr = op->getIn(1);
    if (ptr->isConstant()) continue;		// an absolute address is never the stack
    if (all || unknown.find(ptr) != unknown.end())
      unmodeledStores.insert(op);
  }
}"""
new="""  iter = fd->beginOp(CPUI_STORE);
  enditer = fd->endOp(CPUI_STORE);
  for(;iter!=enditer;++iter) {
    PcodeOp *op = *iter;
    if (op->isDead()) continue;
    Varnode *ptr = op->getIn(1);
    if (getenv("SE_DBG_ESC")) { fprintf(stderr,"DBG store at %llx ptr const=%d unknown=%d all=%d nunknown=%zu members=%zu\\n",(unsigned long long)op->getAddr().getOffset(),(int)ptr->isConstant(),(int)(unknown.find(ptr)!=unknown.end()),(int)all,unknown.size(),members.size()); }
    if (ptr->isConstant()) continue;		// an absolute address is never the stack
    if (all || unknown.find(ptr) != unknown.end())
      unmodeledStores.insert(op);
  }
}"""
assert old in s
s=s.replace(old,new)
open(p,'w').write(s)
