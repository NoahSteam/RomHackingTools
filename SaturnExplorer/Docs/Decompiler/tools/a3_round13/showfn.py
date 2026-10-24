#!/usr/bin/env python3
"""showfn.py img(007|008) ADDR [-a]: the disassembly (to the last address the text names + a few), then the diff of base and new text (-a: both whole)"""
import sys,re,subprocess,difflib,os
import os
R=os.environ.get('A3R13_OUT','.')   # the directory with drv/drv, out_base_<img>/, out_new_<img>/
H=os.environ.get('A3R13_IMG','.')   # the directory with hwram007.bin and hwram008.bin
img,addr=sys.argv[1],sys.argv[2].upper()
a=open('%s/out_base_%s/lend_after/%s.c'%(R,img,addr)).read(); b=open('%s/out_new_%s/lend_after/%s.c'%(R,img,addr)).read()
adds=[int(x,16) for x in re.findall(r';\s*([0-9A-F]{8})\s*$',a+b,re.M)]
start=int(addr,16); end=max(adds) if adds else start+60
n=min(300,(end-start)//2+8)
out=subprocess.run([R+'/drv/drv','x','--dis',H+'/hwram%s.bin'%img,addr,str(n)],capture_output=True,text=True).stdout
print(out)
if '-a' in sys.argv:
    print('--- BASE\n'+a+'--- NEW\n'+b)
else:
    for l in difflib.unified_diff(a.splitlines(),b.splitlines(),'base','new',lineterm='',n=2): print(l)
