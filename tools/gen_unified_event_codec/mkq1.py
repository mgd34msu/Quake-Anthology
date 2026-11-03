import json
R='/home/buzzkill/Projects/quake-anthology-c/tests/network/'
d=json.load(open('q1cap.json'))
seen=set();rows=[]
for r in d['rows']:
    if 'err' in r or 'decodeErr' in r: continue
    key=(r['p']['version'],r['p'].get('flags',0),r['standard'],r['mode'],r['bytes'])
    if key in seen: continue
    seen.add(key); rows.append(r)
print(len(rows)); import collections; print(collections.Counter(r.get('decodeErr') for r in d['rows'] if 'decodeErr' in r))
def C(s): return json.dumps(s)
with open(R+'q1_depth_corpus.h','w') as f:
    f.write('/* Captured from the TypeScript Q1 writers (bun). Hex strings. */\n')
    f.write('typedef struct q1_row { double version, flags; int standard; const char *mode, *kind, *hex, *rewritten; } q1_row;\nstatic const q1_row q1_rows[] = {\n')
    for r in rows:
        f.write('  {%d, %d, %d, %s, %s, %s, %s},\n'%(r['p']['version'],r['p'].get('flags',0),int(r['standard']),C(r['mode']),C(r['kind']),C(r['bytes']),C(r['re'])))
    f.write('};\nstatic const struct { double version, flags; const char *hex; } q1_moves[] = {\n')
    for m in d['moves']: f.write('  {%d, %d, %s},\n'%(m['p']['version'],m['p'].get('flags',0),C(m['bytes'])))
    f.write('};\nstatic const char *const q1_controls[] = {\n')
    for x in d['ctrl']: f.write('  %s,\n'%C(x))
    f.write('};\nstatic const struct { const char *text; int count; const char *reply; } q1_server[] = {\n')
    for o in d['out']:
        f.write('  {%s, %d, %s},\n'%(C(o['t']),len(o['rep']),C(o['rep'][0] if o['rep'] else '')))
    f.write('};\n')
    f.write('#define Q1_CHALLENGE %d\n'%d['ch'])
    f.write('static const char *const q1_client_flow[] = {%s};\n'%','.join(C(x) for x in d['flow']))
    f.write('static const char *const q1_beats[] = {%s};\n'%','.join(C(x) for x in d['beats']))
    f.write('static const char *const q1_nq_flow[] = {%s};\n'%','.join(C(x) for x in d['nqf']))
    f.write('static const char q1_big_heartbeat[] = %s;\n'%C(d['big']))
    f.write('static const struct { double qport, challenge; const char *userinfo; int spectator, donor; } q1_admitted[] = {\n')
    for c in d['conn']: f.write('  {%d, %d, %s, %d, %d},\n'%(c['qport'],c['challenge'],C(c['userinfo']),int(c['spectator']),int(c['donorWide'])))
    f.write('};\n')
