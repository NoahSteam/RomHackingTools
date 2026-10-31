import sys
b=open(sys.argv[1]).read().split('\nname ')
for x in b:
    x=x if x.startswith('name ') else 'name '+x
    if x.split('\n')[0].startswith('name '+sys.argv[2]+' '):
        print(x.rstrip()+'\n'); break
