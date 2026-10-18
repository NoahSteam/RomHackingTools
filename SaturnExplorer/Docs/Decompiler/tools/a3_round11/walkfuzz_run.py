#!/usr/bin/env python3
"""walkfuzz_run.py seed count : generate, run through drv under ulimit, report pairs whose text returns the constant 5 (the stale local) while the run returns another value."""
import subprocess, sys, re, os, resource
seed,count=sys.argv[1],sys.argv[2]
D=os.path.dirname(os.path.abspath(__file__))
src=subprocess.run(['python3',D+'/walkfuzz.py',seed,count],capture_output=True,text=True).stdout
open(D+'/wf_%s.src'%seed,'w').write(src)
spec=subprocess.run(['python3',D+'/mk.py',D+'/wf_%s.src'%seed],capture_output=True,text=True)
if spec.returncode: print(spec.stderr[-400:]); sys.exit(1)
open(D+'/wf_%s.spec'%seed,'w').write(spec.stdout)
def lim(): resource.setrlimit(resource.RLIMIT_AS,(4_000_000_000,4_000_000_000))
p=subprocess.run([D+'/drv',D+'/scr_wf',D+'/wf_%s.spec'%seed],capture_output=True,text=True,preexec_fn=lim,timeout=1500)
out=p.stdout
open(D+'/wf_%s.out'%seed,'w').write(out)
blocks=out.split('\n====  ')[1:]
bad=0; lent=0; stale=0; rows=[]
for b in blocks:
    name=b.split('\n')[0]
    pm=[l for l in b.split('\n') if l.strip().startswith('PROTO')]
    if not pm: continue
    known='known=1' in pm[-1]
    runs=re.findall(r'RUN .*?-> (?:ret|NORET) r0=([0-9A-F]+)',b)
    if not runs: continue
    r0=int(runs[0],16)
    rets=[l.strip().split(';')[0].strip() for l in b.split('\n') if re.match(r'\s+return\b',l)]
    entryrets=rets[-1:] if rets else []
    text_const=[r for r in entryrets if re.fullmatch(r'return (0x)?[0-9a-f]+',r)]
    if known: lent+=1
    if text_const:
        v=int(text_const[0].split()[1],16 if text_const[0].split()[1].startswith('0x') else 10)
        if v!=r0:
            stale+=1
            rows.append((name,known,text_const[0],hex(r0)))
print('pairs',len(blocks),'lent',lent,'stale',stale)
for r in rows: print('STALE','LENT' if r[1] else 'withheld',r[0],r[2],'run',r[3])
