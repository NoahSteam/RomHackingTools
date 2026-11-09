import struct,sys
src,dst=sys.argv[1],sys.argv[2]
f=open(src,'rb').read()
i=f.find(b'OTHR')
size=struct.unpack_from('<I',f,i+8)[0]
d=i+12+0x10000
h=bytearray(f[d:d+0x100000])
h[0::2],h[1::2]=h[1::2],h[0::2]
open(dst,'wb').write(h)
print(dst,len(h),size)
