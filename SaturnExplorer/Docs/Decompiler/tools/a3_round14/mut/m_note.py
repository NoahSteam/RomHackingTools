p='FrontEnd/src/Decompiler/Sh2Decompiler.cpp'; s=open(p).read()
a="""            // note's "from there up") reaches only the locals at and above it; the escape is the engine's, of the whole frame (HandOut).
            HandOut(s, v);
"""
assert a in s; s=s.replace(a, a.replace("            HandOut(s, v);\n","")); open(p,'w').write(s)
