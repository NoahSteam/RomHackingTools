import sys,os,re,collections
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import textdiff
def canon(l):
    l=re.sub(r'\b[a-z]{0,3}[a-z]Stack_[0-9a-f]+\b','S',l)
    l=re.sub(r'\b[a-z]{0,3}Var\d+\b','V',l)
    l=re.sub(r'\bin_stack_[0-9a-f]+\b','IS',l)
    l=re.sub(r'\bparam_\d+\b','P',l)
    l=re.sub(r'\(\s*(u?int[1-8]|xunknown[1-8]|undefined[1-8]|[a-z0-9]+)\s*\*?\)','',l)
    return re.sub(r'\s+','',l)
img=sys.argv[1]
A='out_base_%s/lend_after'%img; B='out_new_%s/lend_after'%img
res=collections.defaultdict(list)
for f in sorted(os.listdir(B)):
    if not os.path.exists(os.path.join(A,f)): continue
    a=open(os.path.join(A,f)).read(); b=open(os.path.join(B,f)).read()
    if a==b: continue
    _,sa,da,ta,na=textdiff.norm(a); _,sb,db,tb,nb=textdiff.norm(b)
    ca=collections.Counter(canon(x) for x in ta if x not in ('}','{')); cb=collections.Counter(canon(x) for x in tb if x not in ('}','{'))
    lost=ca-cb; gained=cb-ca
    # lost lines that are calls or stores through pointers or returns
    key=[]
    if any(re.search(r'func_0x|\*',x) for x in lost): key.append('lost-call-or-store')
    if any(x.startswith('return') for x in lost): key.append('lost-return')
    if gained and not lost: key.append('gain-only')
    if lost and not gained: key.append('lose-only')
    if lost and gained: key.append('changed')
    if not lost and not gained: key.append('same-stmts')
    res[tuple(key)].append(f)
for k,v in sorted(res.items(),key=lambda x:-len(x[1])): print(len(v),k,v[:4])
