#!/usr/bin/env python3
"""Per-file TS drift: which C ports are older than their TS source.

For every TS file under ../quake-anthology-ts/src and tests, finds the time of its
last commit, and compares it with the newest modification time of its C files
(docs/manifest.tsv c_source and header; c_source may be a glob). A C port whose
newest edit predates the TS file's last commit is "stale": the TS changed after the
C was last touched, so it must be re-read against current TS. A C edit after the TS
commit does not prove parity (it may have been for another reason); it only means
the file is not provably stale. TS files with no manifest row, or whose C file does
not exist, are "missing".

The TS tree must be committed (uncommitted TS edits are not dated). Writes
docs/ts_drift.tsv. Usage: tools/ts_drift_files.py [--ts ../quake-anthology-ts]
"""
import csv, glob, os, subprocess, sys
ts = sys.argv[sys.argv.index('--ts') + 1] if '--ts' in sys.argv else '../quake-anthology-ts'
dirty = subprocess.run(['git', '-C', ts, 'status', '--porcelain', '--', 'src', 'tests'], capture_output=True, text=True).stdout.strip()
if dirty:
    print('warning: TS tree has uncommitted changes; their files are dated by commit only', file=sys.stderr)
log = subprocess.run(['git', '-C', ts, 'log', '--format=@%ct', '--name-only', '--', 'src', 'tests'], capture_output=True, text=True).stdout
last = {}
stamp = 0
for line in log.splitlines():
    if line.startswith('@'):
        stamp = int(line[1:])
    elif line.strip() and line not in last:
        last[line] = stamp
rows = {r['ts_source']: r for r in csv.DictReader(open('docs/manifest.tsv'), delimiter='\t')}
def c_mtime(r):
    times = []
    for key in ('c_source', 'header'):
        v = r.get(key) or '-'
        if v in ('-', ''):
            continue
        for f in (glob.glob(v) if any(ch in v for ch in '*?[') else [v]):
            if os.path.exists(f):
                times.append(os.path.getmtime(f))
    return max(times) if times else None
out = []
for d, _, fs in os.walk(os.path.join(ts, 'src')):
    for f in fs:
        if not f.endswith('.ts') or f.endswith('.d.ts'):
            continue
        rel = os.path.relpath(os.path.join(d, f), ts)
        t = last.get(rel, 0)
        r = rows.get(rel)
        if r is None:
            out.append(('missing-row', rel, t, 0, '-'))
            continue
        m = c_mtime(r)
        if m is None:
            out.append(('missing-c', rel, t, 0, r['c_source']))
        elif m < t:
            out.append(('stale', rel, t, m, r['c_source']))
        else:
            out.append(('newer', rel, t, m, r['c_source']))
counts = {}
for k, *_ in out:
    counts[k] = counts.get(k, 0) + 1
print(' '.join(f'{k}={v}' for k, v in sorted(counts.items())))
with open('docs/ts_drift.tsv', 'w') as w:
    w.write('status\tts_source\tts_last_commit\tc_mtime\tc_source\n')
    for k, rel, t, m, c in sorted(out, key=lambda x: (x[0], x[1])):
        w.write(f'{k}\t{rel}\t{int(t)}\t{int(m)}\t{c}\n')
