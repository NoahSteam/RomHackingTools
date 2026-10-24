p='FrontEnd/third_party/ghidra-decompiler/cpp/heritage.cc'; s=open(p).read()
a="""    if (scope != (const ScopeLocal *)0 && scope->getFrameFloor(floor))
      candidates.push_back(floor);
"""
assert a in s; s=s.replace(a,"")
b="  if (haveLowest && scope != (const ScopeLocal *)0) scope->setFrameFloor(lowest);\n"
assert b in s; s=s.replace(b,"")
c="""	if (fd->getScopeLocal() != (const ScopeLocal *)0 && fd->getScopeLocal()->getSpaceId() == spc)
	  haveLowest = fd->getScopeLocal()->getFrameFloor(lowest);
"""
assert c in s; s=s.replace(c,"")
open(p,'w').write(s)
