#!/usr/bin/env python3
"""Per-file depth comparison of the C port against the TypeScript source.

Reads docs/manifest.tsv and flags, for every TS file: missing C files, thin
ports (C plus header tokens well below TS tokens), and whether a C counterpart is reachable
from main. Comments are excluded. Tokens, not lines: this code base is dense and
line counts mislead. TS type annotations inflate the TS side somewhat, while C
is normally larger than TS for the same logic, so below 0.5 means logic is absent.
Writes docs/port_gaps.tsv when --write is given. A manifest c_source may be a glob
(for a TS file split across several C files).

Usage: tools/depth_audit.py [--ts ../quake-anthology-ts] [--ratio 0.5] [--write]
"""
import os, re, subprocess, sys, csv
ts_root = sys.argv[sys.argv.index('--ts') + 1] if '--ts' in sys.argv else '../quake-anthology-ts'
ratio = float(sys.argv[sys.argv.index('--ratio') + 1]) if '--ratio' in sys.argv else 0.5
strip = re.compile(r'/\*.*?\*/', re.S)
token = re.compile(r'[A-Za-z_][A-Za-z_0-9]*|\d+\.?\d*|[^\sA-Za-z_0-9]')
import glob as _glob
def code_tokens(path):
    if any(ch in path for ch in '*?['):
        files = sorted(_glob.glob(path))
        if not files:
            return None
        return sum(code_tokens(f) or 0 for f in files)
    try:
        text = open(path, errors='replace').read()
    except OSError:
        return None
    text = re.sub(r'//[^\n]*', '', strip.sub('', text))
    return len(token.findall(text))
reach = subprocess.run([sys.executable, 'tools/reachability.py', '--list'], capture_output=True, text=True).stdout.splitlines()
unreach = set(l for l in reach if '/' in l and not re.match(r'^\s*\d', l))
rows = list(csv.DictReader(open('docs/manifest.tsv'), delimiter='\t'))
seen_ts = set()
thin, missing = [], []
for r in rows:
    ts = r['ts_source']; c = r['c_source']; seen_ts.add(ts)
    t = code_tokens(os.path.join(ts_root, ts)) or 0
    header = r.get('header') or '-'
    if c in ('-', ''):
        if t > 100 and ts.endswith('.ts') and not os.path.exists(header): missing.append((t, ts, r['status']))
        continue
    n = code_tokens(c)
    if n is not None:
        n += code_tokens(r['header']) or 0 if r.get('header') not in (None, '-', '') else 0
    if n is None:
        n = code_tokens(header)
        if n is None:
            missing.append((t, ts, 'neither c nor header present: ' + c)); continue
    if t >= 300 and n < t * ratio:
        thin.append((t - n, t, n, ts, c[4:-2] in unreach))
actual = set()
for d, _, fs in os.walk(os.path.join(ts_root, 'src')):
    for f in fs:
        if f.endswith('.ts'):
            actual.add(os.path.relpath(os.path.join(d, f), ts_root))
absent = sorted(actual - seen_ts)
print(f'ts files={len(actual)} manifest rows={len(rows)} not-in-manifest={len(absent)}')
for a in absent[:40]: print('  not in manifest:', a)
print(f'missing C ({len(missing)}):')
for t, ts, s in sorted(missing, reverse=True): print(f'  {t:6d} {ts} [{s}]')
print(f'thin ports, C < {ratio:.0%} of TS tokens ({len(thin)}), total deficit {sum(x[0] for x in thin)} tokens:')
for d, t, n, ts, un in sorted(thin, reverse=True): print(f'  ts={t:6d} c={n:6d} {"UNREACHED" if un else "reached  "} {ts}')
if '--write' in sys.argv:
    with open('docs/port_gaps.tsv', 'w') as out:
        out.write('ts_tokens\tc_tokens\treachable\tts_source\n')
        for d, t, n, ts, un in sorted(thin, reverse=True):
            out.write(f'{t}\t{n}\t{"no" if un else "yes"}\t{ts}\n')
