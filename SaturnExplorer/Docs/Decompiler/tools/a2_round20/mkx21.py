# r21: stack pieces (r20's) read by compares in BRANCH form (bt/bf), both arms, plus register compare pairs
import itertools,sys
STORES = {'L': ['2F52'], 'W0': ['2F51'], 'W2': ['6053', '81F1']}
OVER = {'b0': ['6073', '80F0'], 'b1': ['6073', '80F1'], 'b2': ['6073', '80F2'], 'b3': ['6073', '80F3'], 'h0': ['6073', '81F0'], 'h2': ['6073', '81F1']}
READS = {'w0': ['85F0'], 'w2': ['85F1'], 'l': ['60F2'], 'b0':['84F0'],'b3':['84F3']}
def span(rd): return {'w0': (0, 2), 'w2': (2, 4), 'b0': (0, 1), 'b3': (3, 4), 'l': (0, 4)}[rd]
def ospan(o): return {'b0': (0, 1), 'b1': (1, 2), 'b2': (2, 3), 'b3': (3, 4), 'h0': (0, 2), 'h2': (2, 4)}[o]
def sspan(s): return {'L': (0, 4), 'W0': (0, 2), 'W2': (2, 4)}[s]
CUTS={'none':[], 'extuw':['600D'],'extsw':['600F'],'extsb':['600E'],'extub':['600C']}
CONSTS={'5':'E105','m1':'E1FF','80':'E180','7f':'E17F'}
CMPS={'gt':'3017','ge':'3013','hi':'3016','hs':'3012','eq':'3010','gt_r':'3107','ge_r':'3103','hi_r':'3106','hs_r':'3102'}
BR={'bt':'8902','bf':'8B02'}
combos=[]
for s,o,r in itertools.product(STORES,OVER,READS):
    a,b=span(r);oa,ob=ospan(o);sa,sb=sspan(s)
    if not(a<ob and oa<b):continue
    if not(a>=min(sa,oa) and b<=max(sb,ob)):continue
    if(a,b)==(oa,ob):continue
    if s!='L' and not(a<sb and sa<b):continue
    combos.append((s,o,r))
for (s,o,r) in combos:
  for cn,cw in CUTS.items():
    for k,kw in CONSTS.items():
      for c,cv in CMPS.items():
        for bn,bw in BR.items():
          w=['7FFC']+STORES[s]+OVER[o]+READS[r]+['7F04']+cw+[kw,cv,bw,'E001','000B','0009','E000','000B','0009']
          print(f'{s}.{o}.{r}.{cn}.{k}.{c}.{bn}',' '.join(w))
