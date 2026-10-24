p='FrontEnd/third_party/ghidra-decompiler/cpp/heritage.cc'; s=open(p).read()
a="    if (pointerSteppedDown(op->getIn(1))) {"
assert a in s; s=s.replace(a,"    if (false) {")
open(p,'w').write(s)
