import sys,collections
a=open(sys.argv[1]).read().splitlines(); b=open(sys.argv[2]).read().splitlines(); src=open(sys.argv[3]).read().splitlines()
print(len(a),len(b))
ok=collections.Counter(); samples={}
a=[l.replace("ERR SaveFormatError: ","ERR ") for l in a]; b=[l.replace("ERR SaveFormatError: ","ERR ") for l in b]
import json
for i,(x,y) in enumerate(zip(a,b)):
    if x==y: ok['same']+=1; continue
    xe,ye=x.startswith('ERR'),y.startswith('ERR')
    if xe and ye:
        k='errmsg'
    elif xe!=ye: k='status'
    else: k='bytes'
    ok[k]+=1
    samples.setdefault(k,[]).append((i,x[:400],y[:400]))
print(ok)
for k,v in samples.items():
    for i,x,y in v[:int(sys.argv[4]) if len(sys.argv)>4 else 5]:
        print('---',k,'line',i); print(' TS',x); print(' C ',y)
