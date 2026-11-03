import json,random,copy,sys
random.seed(7)
rows=[json.loads(l) for l in open('corpus.jsonl')]
# dedupe by structure shape
seen=set(); base=[]
def shape(x):
    if isinstance(x,dict): return '{'+','.join(k+':'+shape(v) for k,v in x.items())+'}'
    if isinstance(x,list): return '['+(shape(x[0]) if x else '')+']'
    return type(x).__name__
for r in rows:
    s=shape(r)
    if s not in seen: seen.add(s); base.append(r)
print('distinct shapes',len(base),file=sys.stderr)
def paths(x,p=()):
    if p: yield p
    if isinstance(x,dict):
        for k,v in x.items(): yield from paths(v,p+(k,))
    elif isinstance(x,list):
        for i,v in enumerate(x[:2]): yield from paths(v,p+(i,))
def getp(x,p):
    for k in p: x=x[k]
    return x
def setp(x,p,v):
    for k in p[:-1]: x=x[k]
    x[p[-1]]=v
def delp(x,p):
    for k in p[:-1]: x=x[k]
    if isinstance(x,list): x.pop(p[-1])
    else: del x[p[-1]]
muts=[]
for r in base:
    ev=r['event']
    for p in paths(ev):
        for m in ('del','wrong','null','nan_kind'):
            e2=copy.deepcopy(ev); cur=getp(e2,p)
            if m=='del': delp(e2,p)
            elif m=='wrong':
                setp(e2,p, 'zzz' if not isinstance(cur,str) else 12345)
            elif m=='null': setp(e2,p,None)
            else:
                if isinstance(cur,(int,float)) and not isinstance(cur,bool): setp(e2,p,-1.5)
                elif isinstance(cur,str): setp(e2,p,'q9:bad')
                else: continue
            muts.append({'root':r['root'],'event':e2})
random.shuffle(muts)
muts=muts[:30000]
print(len(muts),file=sys.stderr)
with open('mutated.jsonl','w') as f:
    for m in muts: f.write(json.dumps(m,ensure_ascii=False)+'\n')
