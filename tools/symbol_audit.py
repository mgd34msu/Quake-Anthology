#!/usr/bin/env python3
"""Name-level completeness check of the C port against the TypeScript.

For every TS file, collects exported and private functions, classes, and class
methods, then looks for each name's snake_case form among identifiers in the C
tree. Exact matches only count when the identifier ends with the name, or
contains it as a whole underscore-separated run.

A match proves a name exists, not that its body is right. A miss is strong
evidence of absent logic, though C may inline a small helper under another name.

Usage: tools/symbol_audit.py [--ts ../quake-anthology-ts] [--write] [--module PREFIX]
Writes docs/port_missing_symbols.tsv with --write.
"""
import collections, os, re, sys
ts_root = sys.argv[sys.argv.index('--ts') + 1] if '--ts' in sys.argv else '../quake-anthology-ts'
prefix = sys.argv[sys.argv.index('--module') + 1] if '--module' in sys.argv else None
src = os.path.join(ts_root, 'src')
SKIP = {'constructor', 'if', 'for', 'while', 'switch', 'catch', 'function', 'return', 'get', 'set',
        'super', 'async', 'await', 'new', 'typeof', 'of', 'in', 'do', 'else', 'try', 'throw'}
FUNC = re.compile(r'^\s*(export\s+)?(?:default\s+)?(?:async\s+)?function\*?\s+([A-Za-z_]\w*)', re.M)
CLASS = re.compile(r'^\s*(export\s+)?(?:default\s+)?(?:abstract\s+)?class\s+([A-Za-z_]\w*)', re.M)
METHOD = re.compile(r'^ {2,8}(?:(?:private|public|protected|static|async|override|readonly|get|set)\s+)*\*?(#?[A-Za-z_]\w*)\s*(?:<[^>\n]*>)?\([^;\n]*\)\s*(?::[^{;\n]+)?\{', re.M)
ARROW = re.compile(r'^(export\s+)?const\s+([A-Za-z_]\w*)\s*(?::[^=\n]+)?=\s*(?:async\s*)?(?:\([^)\n]*\)|[A-Za-z_]\w*)\s*(?::[^=\n]+)?=>', re.M)
def snake(name):
    name = re.sub(r'(?<=[A-Z])([A-Z][a-z])', r'_\1', name.lstrip('#'))
    return re.sub(r'(?<=[a-z0-9])([A-Z])', r'_\1', name).lower()
idents = set()
ident = re.compile(r'[A-Za-z_]\w*')
for root in ('src', 'include'):
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.endswith(('.c', '.h')):
                idents.update(i.lower() for i in ident.findall(open(os.path.join(d, f), errors='replace').read()))
runs = set()
for i in idents:
    parts = i.split('_')
    for a in range(len(parts)):
        for b in range(a + 1, len(parts) + 1):
            runs.add('_'.join(parts[a:b]))
total = found = 0
missing = collections.defaultdict(list)
per = collections.Counter(); per_found = collections.Counter()
for d, _, fs in os.walk(src):
    for f in sorted(fs):
        if not f.endswith('.ts') or f.endswith('.d.ts'):
            continue
        path = os.path.join(d, f)
        rel = os.path.relpath(path, src)
        if prefix and not rel.startswith(prefix):
            continue
        text = open(path, errors='replace').read()
        names = {}
        for rx in (FUNC, CLASS, ARROW):
            for exp, name in rx.findall(text):
                names[name] = 'export' if exp else 'private'
        for name in METHOD.findall(text):
            if name.lstrip('#') not in SKIP:
                names.setdefault(name, 'method')
        mod = rel.split('/')[0]
        for name, kind in names.items():
            if len(snake(name)) < 4:
                continue
            total += 1; per[mod] += 1
            if snake(name) in runs:
                found += 1; per_found[mod] += 1
            else:
                missing[rel].append((name, kind))
print(f'symbols={total} named-in-C={found} ({found / max(total, 1):.1%}) missing={total - found}')
for m, n in per.most_common():
    print(f'  {m:12s} {per_found[m]:6d}/{n:<6d} missing {n - per_found[m]}')
ranked = sorted(missing.items(), key=lambda kv: -len(kv[1]))
for rel, items in ranked[:25]:
    print(f'  {len(items):4d} {rel}')
if '--write' in sys.argv and prefix:
    sys.exit('--write with --module would overwrite the shared docs/port_missing_symbols.tsv with one module; run --write without --module')
if '--write' in sys.argv:
    with open('docs/port_missing_symbols.tsv', 'w') as out:
        out.write('ts_source\tkind\tname\n')
        for rel, items in ranked:
            for name, kind in items:
                out.write(f'src/{rel}\t{kind}\t{name}\n')
