import json,random
R='/home/buzzkill/Projects/quake-anthology-c/tests/network/'
random.seed(11)
rows=[json.loads(l) for l in open('corpus.jsonl')]
out=open('corpus.ts.txt').read().splitlines()
def toks(x,p=''):
    if isinstance(x,dict):
        if isinstance(x.get('kind'),str): yield p+'#'+x['kind']
        for k,v in x.items():
            yield p+'.'+k
            yield from toks(v,p+'.'+k)
    elif isinstance(x,list):
        for v in x[:2]: yield from toks(v,p+'[]')
    else:
        yield p+'='+type(x).__name__
        if isinstance(x,bool): yield p+'=='+str(x)
cand=[(i,set(toks(r['event']))) for i,r in enumerate(rows) if out[i].startswith('OK')]
need=set().union(*(t for _,t in cand))
picked=[]
while need:
    i,t=max(cand,key=lambda c:len(c[1]&need)); picked.append(i); need-=t
print('valid rows',len(picked))
mut=[json.loads(l) for l in open('mutated.jsonl')]
mo=open('mutated.ts.txt').read().splitlines()
errs=[i for i,x in enumerate(mo) if x.startswith('ERR')]
msgs={}
for i in errs:
    m=mo[i].split(': ',1)[-1]
    msgs.setdefault(m,[]).append(i)
epick=[]
for m,ids in msgs.items(): epick+=ids[:2]
random.shuffle(errs); 
epick=sorted(set(epick+errs[:150]))
print('error rows',len(epick),'distinct messages',len(msgs))
def c(s):
    s=s.encode('utf-8')
    out=[];cur=''
    for b in s:
        ch=chr(b)
        if ch in '"\\': cur+='\\'+ch
        elif 32<=b<127 and ch!='?': cur+=ch
        else: cur+='\\%03o'%b
        if len(cur)>100: out.append(cur);cur=''
    out.append(cur)
    return ' '.join('"'+x+'"' for x in out)
with open(R+'unified_events_corpus.h','w') as f:
    f.write('/* Rows captured from the TypeScript codec (bun): every switch branch of its readers,\n * decoded and re-encoded, identity remapped by +10/+1 (actor), +20/+2 (client), +1 (seat).\n * Error rows hold the SaveFormatError message the TS codec throws for that input. */\n')
    f.write('typedef struct corpus_row { int presentation; const char *wire; const char *expected; } corpus_row;\n')
    f.write('static const corpus_row valid_rows[] = {\n')
    for i in picked:
        r=rows[i]
        f.write('  {%d, %s,\n   %s},\n'%(r['root']=='presentation',c(json.dumps(r['event'],ensure_ascii=False,separators=(',',':'))),c(out[i][3:])))
    f.write('};\nstatic const corpus_row error_rows[] = {\n')
    for i in epick:
        r=mut[i]
        f.write('  {%d, %s,\n   %s},\n'%(r['root']=='presentation',c(json.dumps(r['event'],ensure_ascii=False,separators=(',',':'))),c(mo[i][4:])))
    f.write('};\n')
    d=json.load(open('ts_events.json'))
    f.write('static const char ts_test_events[] = %s;\n'%c(d['events']))
    f.write('static const char ts_test_player_event[] = %s;\n'%c(d['player']))
    f.write('static const char ts_test_player_event_reencoded[] = %s;\n'%c(d['player_decoded']))
    f.write('static const char ts_test_damage[] = %s;\n'%c(d['damage']))
    f.write('static const char ts_test_sound[] = %s;\n'%c(d['sound']))
