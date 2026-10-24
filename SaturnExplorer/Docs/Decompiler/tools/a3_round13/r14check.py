import os,re,subprocess,sys
R='/tmp/claude-0/-home-user-RomHackingTools/aae0ae36-3afa-5288-8786-5063bd965426/scratchpad/a3r13rev'
H='/tmp/claude-0/-home-user-RomHackingTools/aae0ae36-3afa-5288-8786-5063bd965426/scratchpad/h'
img=sys.argv[1]
d='%s/out_new_%s/lend_after'%(R,img)
res={'fp':0,'other':0}; other=[]
for f in sorted(os.listdir(d)):
    t=open(os.path.join(d,f)).read()
    if 'frame address is in r14 here' not in t: continue
    addrs=[int(x,16) for x in re.findall(r';\s*([0-9A-F]{8})\s*$',t,re.M)]
    start=int(f[:8],16); end=max(addrs) if addrs else start+80
    n=min(400,(end-start)//2+10)
    out=subprocess.run([R+'/drv/drv','x','--dis',H+'/hwram%s.bin'%img,f[:8],str(n)],capture_output=True,text=True).stdout
    if re.search(r'mov r15,r14|mov r14,r15',out) or re.search(r'add .*r14',out) and 'r15' in out: res['fp']+=1
    else: res['other']+=1; other.append(f)
print(img,res,other[:20])
