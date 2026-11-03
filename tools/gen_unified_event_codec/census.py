import tsparse,collections,re,json
text=open('/home/buzzkill/Projects/quake-anthology-ts/src/app/bootstrap/network/unified-event-codec.ts').read()
fns=tsparse.split_functions(text)
print(len(fns))
shapes=collections.Counter()
def norm(e):
    if isinstance(e,tuple):
        k=e[0]
        if k=='str':return 'S'
        if k=='num':return 'N'
        if k=='id':return e[1] if e[1] in('reader','identity','value','item','items','undefined') or e[1].startswith('item') else ('readX' if e[1].startswith('read') else 'writeX' if e[1].startswith('write') else e[1])
        if k=='member':return norm(e[1])+'.'+e[2]
        if k=='index':return norm(e[1])+'['+norm(e[2])+']'
        if k=='call':return norm(e[1])+'('+','.join(norm(a) for a in e[2])+')'
        if k=='obj':
            return 'OBJ'
        return k
    return str(e)
def walk(e):
    if isinstance(e,tuple):
        if e[0] in('call','member','index','cond','eq','ne','typeof','template','regex','new','array','arrow','and','or','not','<','>','neg'):
            shapes[norm(e)]+=1
        for x in e[1:]: walk(x)
    elif isinstance(e,list):
        for x in e: walk(x)
for f in fns:
    if f['body']: walk(f['body'])
for k,v in sorted(shapes.items(),key=lambda x:-x[1]): print(v,k[:170])
