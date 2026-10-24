p='FrontEnd/src/Decompiler/Sh2Decompiler.cpp'; s=open(p).read()
a="""                for (int r = 2; r < 8; ++r)
                    if (((kConvention >> r) & 1u)) HandOut(s, Get(s, mReg[r]));
"""
assert a in s; s=s.replace(a,""); 
b="""                    for (const Value& w : words) HandOut(s, w);
                }
                const uint32_t regs = haveTarget"""
assert b in s; s=s.replace(b, """                    (void)words;
                }
                const uint32_t regs = haveTarget"""); open(p,'w').write(s)
