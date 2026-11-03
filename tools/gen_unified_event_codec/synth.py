"""Synthesize valid wire events from the TS reader ASTs: covers every switch branch,
optional present/absent, nullable null/non-null. Writes JSON lines of wire events."""
import json, sys, re, collections
import tsparse
orig=tsparse.P.stmt
def stmt(self):
    if self.peek()[0]=='id' and self.peek(1)[1]=='.' and self.peek(3)[1]=='=' and self.peek(2)[0]=='id':
        base=('id',self.eat()[1]);self.eat('.');key=self.eat()[1];self.eat('=');v=self.expr();self.try_eat(';')
        return ('expr',('assign',('member',base,key,False),v))
    return orig(self)
tsparse.P.stmt=stmt
text=open('/home/buzzkill/Projects/quake-anthology-ts/src/app/bootstrap/network/unified-event-codec.ts').read()
import io,contextlib
with contextlib.redirect_stdout(io.StringIO()):
    fns={f['name']:f for f in tsparse.split_functions(text)}
class N:
    __slots__=('v',)
    def __init__(s): s.v=None
    def field(s,k):
        if s.v is None: s.v={}
        if not isinstance(s.v,(dict,)):
            return N()
        if k not in s.v: s.v[k]=N()
        return s.v[k]
def plain(n):
    if isinstance(n,N): return plain(n.v)
    if isinstance(n,dict):
        return {k:plain(v) for k,v in n.items() if not (isinstance(v,N) and v.v is None)}
    if isinstance(n,list): return [plain(x) for x in n]
    return n
visited=collections.Counter(); allbr=set()
cnt=collections.Counter()
HEX='a'*64
def tick(key,n):
    i=cnt[key]; cnt[key]+=1; return i%n
num=[0]
def nextnum():
    num[0]+=1
    return num[0]+(0.5 if num[0]%3==0 else 0)
class V: pass
def isnode(x): return isinstance(x,N)
def hand(name,node,fnkey):
    if name in('readActorId','readClientId'):
        node.field('slot').v=3; node.field('generation').v=7; return
    if name=='readSeatId': node.field('index').v=1; return
    if name=='readResourceId': node.v='resource:unified:'+HEX; return
    if name=='readContentId': node.v='q3:classic:baseq3:test'; return
    if name=='readEventQ2PresentationEventPoiFields':
        node.v=[{'key':'image','value':'i_help'},{'key':'b','value':'c'}]; return
    if name=='readSourcePlayerState':
        node.v=[0]*468; return
    raise Exception(name)
def run(name,node,stack):
    if name in('readEventQ2PresentationEventPoiFields','readSourcePlayerState','readActorId','readClientId','readSeatId','readResourceId','readContentId'):
        hand(name,node,None); return
    f=fns[name]
    if f['body'] is None: raise Exception(name)
    env={f['params'][0]:node}
    for s in f['body']:
        r=st(s,env,name,stack)
        if r: return
def st(s,env,fn,stack):
    k=s[0]
    if k=='expr':
        if s[1][0]=='void': return
        if s[1][0]=='assign': ex(s[1][2],env,fn,stack); return
        ex(s[1],env,fn,stack); return
    if k=='return':
        if s[1] is not None: ex(s[1],env,fn,stack)
        return True
    if k=='const':
        for nm,init in s[1]:
            if init[0]=='call' and init[1]==('id','boundedList') and init[2][1][2]==('id','item'):
                base=ex(init[2][0],env,fn,stack); base.v=[N(),N()]; env[nm]=base
            elif init[0]=='index':
                env[nm]=env[init[1][1]].v[int(init[2][1])]
            else:
                env[nm]=ex(init,env,fn,stack)
        return
    if k=='if': # only failure guards
        return
    if k=='blockstmt':
        for x in s[1]:
            if st(x,env,fn,stack): return True
        return
    if k=='switch':
        cases=s[2]
        d=s[1]
        base=ex(d[1][1][1],env,fn,stack)
        key=d[1][1][2][0][1]
        labels=[l[1] for ls,_ in cases for l in ls]
        for lb in labels: allbr.add((fn,lb))
        # prefer unvisited branch unless we are deep: pick the least visited
        lb=min(labels,key=lambda l:(visited[(fn,l)],labels.index(l)))
        visited[(fn,lb)]+=1
        base.field(key).v=lb
        for ls,body in cases:
            if any(l[1]==lb for l in ls):
                for x in body:
                    if st(x,env,fn,stack): return True
        return True
    raise Exception(s)
def setscalar(node,v):
    if node.v is None: node.v=v
    return node
def ex(e,env,fn,stack):
    k=e[0]
    if k in('str','num','const','neg','regex','template'): return None
    if k=='id': return env.get(e[1])
    if k=='obj':
        for p in e[1]:
            if p[0]=='prop': ex(p[2],env,fn,stack)
            else: ex(p[1],env,fn,stack)
        return None
    if k=='spread': return ex(e[1],env,fn,stack)
    if k=='array':
        for x in e[1]: ex(x,env,fn,stack)
        return None
    if k=='new': return N() if e[1][1][0][1]=='EntityState' else None
    if k=='cond':
        c=e[1]
        # optional field or null guard
        if c[0]=='eq' and c[1][0]=='member' and c[1][2]=='value':
            what=c[2][1]
            target=ex(c[1][1],env,fn,stack)
            present=tick((fn,repr(c)[:80]),2)==1
            if what=='undefined':
                if not present: 
                    return None
                return ex(e[3],env,fn,stack)
            if what=='null':
                if not present:
                    target.v=None; target.__class__=NullN
                    return None
                return ex(e[3],env,fn,stack)
        if c[0]=='eq' and c[1][0]=='typeof':
            target=ex(c[1][1][1],env,fn,stack)
            if tick((fn,'typeof'),2): target.v='text'
            else: target.v=nextnum()
            return None
        if c[0]=='eq' and c[1][0]=='call':
            # .choice(...) === "x"
            ex(c[1],env,fn,stack)
            lit=c[2][1]
            recv=ex(c[1][1][1],env,fn,stack)
            return ex(e[2] if recv.v==lit else e[3],env,fn,stack)
        if c[0]=='ne' or c[0]=='eq':
            return ex(e[3],env,fn,stack)
        raise Exception(c)
    if k=='member':
        return ex(e[1],env,fn,stack)
    if k=='call':
        cal,args=e[1],e[2]
        if cal[0]=='id':
            f=cal[1]
            if f=='boundedList':
                base=ex(args[0],env,fn,stack)
                base.v=[N(),N()]
                arrow=args[1]
                for it in base.v:
                    env2=dict(env); env2[arrow[1][0]]=it
                    ex(arrow[2],env2,fn,stack)
                    if it.v is None: it.v=nextnum()
                return None
            if f=='namespaced':
                n=ex(args[0],env,fn,stack); setscalar(n,'mod:thing'); return None
            if f=='sourceClientInteger':
                n=ex(args[0],env,fn,stack); setscalar(n,5); return None
            if f=='readModule':
                n=ex(args[0],env,fn,stack)
                n.field('id').v='mod:events'; n.field('artifactPath').v='vm/qagame.qvm'; n.field('revision').v='original'
                n.field('digest').v='sha256:9751bad99a2d138f96a9b0436d2ea2d965b86214175dc33e4cea95e059419337'
                return None
            if f.startswith('read'):
                n=ex(args[0],env,fn,stack)
                run(f,n,stack+[f])
                return None
            raise Exception(f)
        if cal[0]=='member':
            recv,m=cal[1],cal[2]
            if m=='field':
                base=ex(recv,env,fn,stack)
                return base.field(args[0][1])
            n=ex(recv,env,fn,stack)
            if m=='finite' or m=='number':
                setscalar(n,nextnum()); return n
            if m=='string':
                setscalar(n,'str"é\\%d'%num[0]); return n
            if m=='boolean':
                setscalar(n,bool(tick((fn,'b'),2))); return n
            if m=='integer':
                setscalar(n,int(args[0][1])+2 if args and args[0][0]=='num' else 4); return n
            if m=='literal':
                setscalar(n,args[0][1]); return n
            if m=='choice':
                vals=[a[1] if a[0]=='str' else (-a[1][1] if a[0]=='neg' else a[1]) for a in args]
                if n.v is None: n.v=vals[tick((fn,repr(cal)[:120]),len(vals))]
                return n
            if m=='nullable':
                if tick((fn,'nullable'+repr(args)[:60]),2)==0:
                    n.__class__=NullN; return None
                arrow=args[0]
                env2=dict(env); env2[arrow[1][0]]=n
                ex(arrow[2],env2,fn,stack)
                return None
        raise Exception(e)
    if k=='typeof' or k=='index':
        return None
    raise Exception(e)
class NullN(N):
    __slots__=()
def plain(n):
    if isinstance(n,NullN): return None
    if isinstance(n,N): return plain(n.v)
    if isinstance(n,dict):
        return {k:plain(v) for k,v in n.items() if not (type(v) is N and v.v is None)}
    if isinstance(n,list): return [plain(x) for x in n]
    return n
def gen(root):
    n=N(); run(root,n,[root]); return plain(n)
out=[]
# find all switch branches in reachable functions by repeated generation until saturated
for root in('readEventSimulationPresentationEvent','readEventSimulationEvent'):
    prev=-1; stall=0
    for i in range(8000):
        ev=gen(root); out.append((root,ev))
        cov=sum(1 for b in allbr if visited[b]>0), len(allbr)
        if cov[0]==prev: stall+=1
        else: stall=0
        prev=cov[0]
        if stall>800: break
print('branches',cov,'events',len(out),file=sys.stderr)
print('unvisited',[b for b in allbr if not visited[b]][:30],file=sys.stderr)
for root,ev in out:
    print(json.dumps({'root':'presentation' if 'Presentation' in root else 'simulation','event':ev},ensure_ascii=False))
