#!/usr/bin/env python3
"""Object-level reachability from src/main.c, using nm symbol tables.

An object is reachable if a reachable object references one of its global
symbols. This is an upper bound on what runs (taking a function pointer counts
as a call; pulling one symbol pulls its whole object), so "unreachable" is a
hard finding: that code cannot execute in the shipped binary.

Usage: tools/reachability.py [--list] [--module PREFIX]
"""
import collections, os, subprocess, sys
root = 'build/src'
objs = sorted(os.path.join(d, f) for d, _, fs in os.walk(root) for f in fs if f.endswith('.o'))
defs, undefs = {}, collections.defaultdict(set)
out = subprocess.run(['nm', '-A', '--no-sort'] + objs, capture_output=True, text=True).stdout
for line in out.splitlines():
    head, _, rest = line.partition(':')
    parts = rest.split()
    if len(parts) == 3 and parts[1] in 'TDBRVWGSC':
        defs.setdefault(parts[2], head)
    elif len(parts) == 2 and parts[0] == 'U':
        undefs[head].add(parts[1])
start = os.path.join(root, 'main.o')
seen, stack = {start}, [start]
while stack:
    for sym in undefs[stack.pop()]:
        target = defs.get(sym)
        if target and target not in seen:
            seen.add(target); stack.append(target)
name = lambda o: o[len(root) + 1:-2]
def module(o):
    p = name(o).split('/')
    return '/'.join(p[:2]) if p[0] in ('app', 'content', 'compat') and len(p) > 2 else p[0]
tot, rch = collections.Counter(), collections.Counter()
for o in objs:
    tot[module(o)] += 1
    rch[module(o)] += o in seen
print(f'{len(seen)}/{len(objs)} objects reachable from main')
for m, n in sorted(tot.items(), key=lambda kv: kv[1] - rch[kv[0]], reverse=True):
    print(f'{rch[m]:5d}/{n:<5d} {m}')
prefix = sys.argv[sys.argv.index('--module') + 1] if '--module' in sys.argv else None
if '--list' in sys.argv or prefix:
    for o in objs:
        if o not in seen and (not prefix or name(o).startswith(prefix)):
            print(name(o))
