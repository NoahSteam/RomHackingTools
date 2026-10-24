p='FrontEnd/src/Decompiler/Sh2Decompiler.cpp'; s=open(p).read()
a="            else HandOut(s, slot.second);\n"
assert a in s; s=s.replace(a,"            else {}\n"); open(p,'w').write(s)
