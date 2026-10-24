p='FrontEnd/src/Decompiler/Sh2Decompiler.cpp'; s=open(p).read()
a="""        for (const auto& slot : s.slots)
            if (slot.second.Frame()) s.out |= EscapeClass(s, slot.second);
"""
assert a in s; s=s.replace(a,""); open(p,'w').write(s)
