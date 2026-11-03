#!/usr/bin/env python3
"""Find C sites whose library function or operator differs from the JS operation.

The TypeScript runs under Bun (JavaScriptCore). Where its port calls a C library
function or applies a C conversion, the result can differ from JS on reachable
inputs. This parses every src/**/*.c with libclang (real types, not regexes) and
flags, per site:

  minmax     fmin/fmax[f|l]: drop NaN where Math.min/Math.max return NaN, and leave
             -0/+0 order open. Use qa_js_min/qa_js_max.
  round      round/lround/llround/rint/nearbyint[f]: ties away from zero or to even;
             Math.round ties toward +Infinity. Use qa_js_round.
  pow        pow[f]: JSC computes integer exponents 0..1000 by repeated squaring and
             +-0.5 by sqrt. Use qa_js_pow for Math.pow and `**`.
  hypot      hypot[f]: matches Math.hypot only for two arguments; nested hypot() or
             sqrt(sum of squares) ports of three or more differ. qa_js_hypot*. Also
             flagged per file: the TS counterpart has more 3+-argument Math.hypot
             calls than the C file has qa_js_hypot3/qa_js_hypot calls.
  libm       cbrt/fma/expm1/log1p...: not checked against JSC (see --help notes).
  parse      atoi/atol/atof/strtol/strtoul/strtoll/strtoull/strtod/strtof/sscanf:
             prefix, white space, "inf"/"nan", hex and empty-string rules differ from
             parseInt/parseFloat/Number. Use qa_js_parse_int/parse_float/
             string_to_number, or the profile-specific native/game atoi/atof.
  sort       qsort: unstable; Array.prototype.sort is stable. qa_js_sort.
  fixed      printf-family %f/%e conversions: toFixed rounds exact ties up and
             prints NaN/-0 differently. qa_js_to_fixed. (%g belongs to js_number.h.)
  case       tolower/toupper/strcasecmp/strncasecmp: ASCII only; JS case mapping
             covers non-ASCII.
  float2int  explicit or implicit conversion from a floating type to an integer
             type: undefined behavior for NaN and out-of-range values, where JS
             ToInt32/ToUint32 wrap (qa_js_to_int32 ...).
  truthy     a floating value used as a condition: C NaN is true, JS NaN is falsy.
  bitwise64  & | ^ << >> ~ (or a compound assignment) computed in a signed 64-bit type,
             or an unsigned 64-bit one fed by a double cast: JS converts each operand
             with ToInt32 and returns a signed 32-bit result, so bits above 2^31 are
             dropped and bit 31 makes the result negative. Use qa_js_to_int32 on the
             operands (and the result). TS number fields stored as int64_t or double
             in C (flags, spawnflags, items) are the usual source.
  bitwise-sign  an unsigned 32-bit & | ^ << ~ result converted to a double or a
             64-bit integer: JS returns these signed (0x80000000 | 0 is -2147483648);
             only >>> is unsigned.
  helper     a local static js_*/ts_* min/max/round/parse helper that duplicates
             include/qa/foundation/js.h.

Classes used in the TSV: same, port-of-c, c-only (see its header), and absent
(a TS-side count row whose TS expression has no C port yet; the port, when
written, must use the js.h helper).

Each flagged site is unclassified until tools/js_semantics_audit.tsv lists it
(category, file, function, expression, class, reason) or a built-in rule below
classifies it. Keys ignore line numbers, so entries survive edits elsewhere.

Built-in rules (each reported by name):
  const-operand    the converted operand is a compile-time constant.
  two-argument     hypot(a, b) with plain arguments: JSC's two-argument Math.hypot
                   is glibc hypot (checked over random inputs under bun).

A classification row may use `*` as the function (every site of that category in
the file) or as the expression (every site in that function), after review.

Headers: sites inside include/qa/**/*.h (static inline helpers) are reported
once, under the header's path, from whichever .c file includes them.

Limits: libclang must parse the file (a file mid-edit by another porter is
reported as a parse failure, not as clean); macros expand to their call sites;
integer overflow where TS used doubles, `%` on integers, and evaluation order
are not detected.

Usage: tools/js_semantics_audit.py [--module PREFIX] [--category C[,C]] [--all]
                                   [--summary] [--tsv] [--jobs N]
  --all       list classified sites too.   --summary  counts only.
  --tsv       print unclassified sites as classification rows to fill in.
  --no-cache  rescan every file. Results are cached per file content under
              build/js_semantics_audit_cache; a header-only change needs --no-cache.
Exit status 1 when an unclassified site or a parse failure remains.
"""
import collections, concurrent.futures, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSV = os.path.join(ROOT, 'tools', 'js_semantics_audit.tsv')

CALLS = {
    'minmax': {'fmin', 'fmax', 'fminf', 'fmaxf', 'fminl', 'fmaxl'},
    'round': {'round', 'roundf', 'roundl', 'lround', 'lroundf', 'llround', 'llroundf', 'rint', 'rintf',
              'lrint', 'lrintf', 'llrint', 'nearbyint', 'nearbyintf'},
    'pow': {'pow', 'powf', 'powl'},
    'hypot': {'hypot', 'hypotf', 'hypotl'},
    'libm': {'cbrt', 'cbrtf', 'expm1', 'log1p', 'fma', 'fmaf', 'erf', 'tgamma', 'lgamma'},
    'parse': {'atoi', 'atol', 'atoll', 'atof', 'strtol', 'strtoul', 'strtoll', 'strtoull', 'strtod',
              'strtof', 'strtold', 'strtoimax', 'strtoumax', 'sscanf', 'vsscanf'},
    'sort': {'qsort', 'qsort_r'},
    'case': {'tolower', 'toupper', 'strcasecmp', 'strncasecmp'},
}
CALL_CATEGORY = {name: cat for cat, names in CALLS.items() for name in names}
PRINTF = {'printf': 0, 'fprintf': 1, 'sprintf': 1, 'snprintf': 2, 'vprintf': 0, 'vfprintf': 1,
          'vsprintf': 1, 'vsnprintf': 2, 'dprintf': 1, 'asprintf': 1}
FIXED_SPEC = re.compile(r'%[-+ #0]*(\d+|\*)?(\.(\d+|\*))?[lL]?[fFeE]')
HELPER = re.compile(r'^(js|ts|math)_(min|max|minimum|maximum|round|sign|parse_int|parse_float|number_of|'
                    r'to_int32|to_uint32|fixed|clamp|trunc|floor)$')
SAFE_CALLS = set()
FLOOR_HALF = re.compile(r'\+\s*0?\.5f?\s*\)$')

TS_ROOT = os.path.join(os.path.dirname(ROOT), 'quake-anthology-ts', 'src')

def ts_counterpart(rel):
    """src/a/b_c.c -> ../quake-anthology-ts/src/a/b-c.ts (or b_c.ts), or None."""
    stem = rel[len('src/'):-2]
    for name in (stem.replace('_', '-'), stem):
        path = os.path.join(TS_ROOT, name + '.ts')
        if os.path.exists(path):
            return path
    return None

def ts_wide_hypot_calls(text):
    """Number of Math.hypot calls with three or more arguments."""
    count = 0
    for match in re.finditer(r'Math\.hypot\(', text):
        depth, commas, i = 1, 0, match.end()
        while i < len(text) and depth:
            ch = text[i]
            if ch in '([{':
                depth += 1
            elif ch in ')]}':
                depth -= 1
            elif ch == ',' and depth == 1:
                commas += 1
            i += 1
        if commas >= 2 or '...' in text[match.end():i]:
            count += 1
    return count

def compile_flags():
    flags = ['-std=c17', '-I' + os.path.join(ROOT, 'include')]
    try:
        out = subprocess.run(['pkg-config', '--cflags', 'sdl2', 'freetype2', 'vorbisfile', 'theoradec'],
                             capture_output=True, text=True, check=True).stdout
        flags += out.split()
    except (OSError, subprocess.CalledProcessError):
        pass
    try:
        res = subprocess.run(['clang', '-print-resource-dir'], capture_output=True, text=True, check=True)
        flags.append('-I' + os.path.join(res.stdout.strip(), 'include'))
    except (OSError, subprocess.CalledProcessError):
        pass
    return flags

def norm(text):
    return re.sub(r'\s+', ' ', text).strip()

def scan(path, flags):
    """Returns (sites, error). A site is (category, line, function, expression)."""
    import clang.cindex as ci
    K = ci.CursorKind
    index = ci.Index.create()
    try:
        tu = index.parse(path, args=flags, options=ci.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD * 0)
    except ci.TranslationUnitLoadError as error:
        return [], str(error)
    errors = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error and d.location.file
              and d.location.file.name == path]
    if errors:
        return [], '%d: %s' % (errors[0].location.line, errors[0].spelling)
    source = open(path, 'rb').read()

    sources = {path: source}

    def text_of(cursor):
        ext = cursor.extent
        name = ext.start.file.name if ext.start.file is not None else path
        if name not in sources:
            sources[name] = open(name, 'rb').read()
        return norm(sources[name][ext.start.offset:ext.end.offset].decode('utf-8', 'replace'))

    def is_float(t):
        return t.get_canonical().kind in (ci.TypeKind.FLOAT, ci.TypeKind.DOUBLE, ci.TypeKind.LONGDOUBLE)

    INT_KINDS = {ci.TypeKind.CHAR_U, ci.TypeKind.UCHAR, ci.TypeKind.USHORT, ci.TypeKind.UINT,
                 ci.TypeKind.ULONG, ci.TypeKind.ULONGLONG, ci.TypeKind.CHAR_S, ci.TypeKind.SCHAR,
                 ci.TypeKind.SHORT, ci.TypeKind.INT, ci.TypeKind.LONG, ci.TypeKind.LONGLONG,
                 ci.TypeKind.ENUM}

    def is_int(t):
        return t.get_canonical().kind in INT_KINDS

    def is_bool(t):
        return t.get_canonical().kind == ci.TypeKind.BOOL

    CONST_KINDS = {K.INTEGER_LITERAL, K.FLOATING_LITERAL, K.CHARACTER_LITERAL, K.PAREN_EXPR,
                   K.UNARY_OPERATOR, K.BINARY_OPERATOR, K.UNEXPOSED_EXPR, K.CSTYLE_CAST_EXPR,
                   K.CONDITIONAL_OPERATOR}

    def constant(cursor):
        if cursor.kind not in CONST_KINDS:
            return False
        if cursor.kind == K.UNARY_OPERATOR and text_of(cursor).startswith('sizeof'):
            return True
        return all(constant(c) for c in cursor.get_children())

    sites = []
    converted = set()
    B = ci.BinaryOperator
    BITWISE = {B.And, B.Or, B.Xor, B.Shl, B.Shr, B.AndAssign, B.OrAssign, B.XorAssign, B.ShlAssign,
               B.ShrAssign}
    SIGNED_RESULT = {B.And, B.Or, B.Xor, B.Shl, B.AndAssign, B.OrAssign, B.XorAssign, B.ShlAssign}
    WIDE = {ci.TypeKind.LONG, ci.TypeKind.LONGLONG, ci.TypeKind.ULONG, ci.TypeKind.ULONGLONG}

    def is_wide(t):
        return t.get_canonical().kind in WIDE

    def is_u32(t):
        return t.get_canonical().kind == ci.TypeKind.UINT

    def strip(cursor):
        while cursor is not None and cursor.kind in (K.PAREN_EXPR, K.UNEXPOSED_EXPR):
            kids = [c for c in cursor.get_children() if c.kind.is_expression()]
            if len(kids) != 1:
                break
            if cursor.kind == K.UNEXPOSED_EXPR and kids[0].type.get_canonical().kind != cursor.type.get_canonical().kind:
                break
            cursor = kids[0]
        return cursor

    def is_bitwise(cursor):
        if cursor.kind in (K.BINARY_OPERATOR, K.COMPOUND_ASSIGNMENT_OPERATOR):
            try:
                return cursor.binary_operator in SIGNED_RESULT
            except Exception:
                return False
        if cursor.kind == K.UNARY_OPERATOR:
            return text_of(cursor).startswith('~')
        return False

    def float_cast_below(cursor):
        for c in cursor.walk_preorder():
            if c.kind in (K.CSTYLE_CAST_EXPR, K.UNEXPOSED_EXPR):
                inner = operand(c)
                if inner is not None and is_float(inner.type) and is_int(c.type):
                    return True
        return False

    def check_bitwise(cursor, func):
        """A JS bitwise operator converts with ToInt32 and returns int32. A C operator on
        a 64-bit integer keeps bits above 2^31 and never wraps to a negative int32."""
        if cursor.kind == K.UNARY_OPERATOR:
            if not text_of(cursor).startswith('~'):
                return
        else:
            try:
                op = cursor.binary_operator
            except Exception:
                return
            if op not in BITWISE:
                return
        t = cursor.type.get_canonical()
        if t.kind not in WIDE or constant(cursor):
            return
        if t.kind in (ci.TypeKind.ULONG, ci.TypeKind.ULONGLONG) and not float_cast_below(cursor):
            return
        add('bitwise64', cursor, func, text_of(cursor))

    include_root = os.path.join(ROOT, 'include') + os.sep

    def in_scope(location_file):
        """The scanned .c file, or a project header (its static inline bodies)."""
        return location_file is not None and (location_file.name == path or
                                              location_file.name.startswith(include_root))

    def add(cat, cursor, func, expr):
        if not in_scope(cursor.location.file):
            return
        sites.append((cat, cursor.location.line, func, expr,
                      os.path.relpath(cursor.location.file.name, ROOT)))

    def operand(cursor):
        kids = [c for c in cursor.get_children() if c.kind.is_expression()]
        return kids[-1] if kids else None

    def walk(cursor, func):
        for child in cursor.get_children():
            if child.location.file is not None and not in_scope(child.location.file):
                continue
            f = func
            if child.kind == K.FUNCTION_DECL:
                f = child.spelling
                if child.is_definition() and HELPER.match(child.spelling):
                    add('helper', child, f, child.spelling)
            kind = child.kind
            if kind == K.CALL_EXPR and child.spelling.startswith('qa_js_to_'):
                # The argument is already converted with JS semantics.
                for argument in child.get_arguments():
                    converted.add((argument.extent.start.offset, argument.extent.end.offset))
            if kind == K.CALL_EXPR:
                name = child.spelling
                cat = CALL_CATEGORY.get(name)
                if cat == 'hypot':
                    args = list(child.get_arguments())
                    nested = any(a.kind == K.CALL_EXPR and a.spelling.startswith('hypot') or
                                 'hypot' in text_of(a) for a in args)
                    add('hypot' if nested else 'hypot:two-argument', child, f, text_of(child))
                elif cat:
                    add(cat, child, f, text_of(child))
                elif name in ('floor', 'floorf') and FLOOR_HALF.search(text_of(child)):
                    add('round', child, f, text_of(child))
                elif name in PRINTF:
                    args = list(child.get_arguments())
                    fmt_index = PRINTF[name]
                    if fmt_index < len(args):
                        fmt = text_of(args[fmt_index])
                        if FIXED_SPEC.search(fmt):
                            add('fixed', child, f, text_of(child))
            elif kind in (K.CSTYLE_CAST_EXPR, K.UNEXPOSED_EXPR):
                inner = operand(child)
                if inner is not None and is_float(inner.type) and (is_int(child.type) or is_bool(child.type)):
                    cat = 'truthy' if is_bool(child.type) else 'float2int'
                    rule = 'const-operand' if constant(inner) else None
                    expr = '(%s) %s' % (child.type.get_canonical().spelling, text_of(inner))
                    if kind == K.CSTYLE_CAST_EXPR:
                        expr = text_of(child)
                    add(cat if rule is None else cat + ':' + rule, child, f, expr)
            elif kind in (K.IF_STMT, K.WHILE_STMT, K.DO_STMT, K.FOR_STMT, K.CONDITIONAL_OPERATOR):
                kids = list(child.get_children())
                cond = None
                if kind in (K.IF_STMT, K.WHILE_STMT, K.CONDITIONAL_OPERATOR) and kids:
                    cond = kids[0]
                elif kind == K.DO_STMT and len(kids) > 1:
                    cond = kids[1]
                if cond is not None and cond.kind.is_expression() and is_float(cond.type):
                    add('truthy', cond, f, text_of(cond))
            elif kind == K.UNARY_OPERATOR:
                kids = list(child.get_children())
                spelled = text_of(child)
                if kids and spelled.startswith('!') and is_float(kids[0].type):
                    add('truthy', child, f, spelled)
            if kind in (K.BINARY_OPERATOR, K.COMPOUND_ASSIGNMENT_OPERATOR, K.UNARY_OPERATOR):
                check_bitwise(child, f)
            if kind in (K.CSTYLE_CAST_EXPR, K.UNEXPOSED_EXPR) and (is_float(child.type) or is_wide(child.type)):
                inner = operand(child)
                core = strip(inner) if inner is not None else None
                if (core is not None and is_bitwise(core) and is_u32(core.type)
                        and not constant(core)
                        and (child.extent.start.offset, child.extent.end.offset) not in converted):
                    add('bitwise-sign', child, f, '(%s) %s' % (child.type.get_canonical().spelling, text_of(core)))
            if kind == K.BINARY_OPERATOR:
                kids = list(child.get_children())
                op = None
                if len(kids) == 2:
                    toks = [t.spelling for t in child.get_tokens()]
                    left_len = len(list(kids[0].get_tokens()))
                    if left_len < len(toks):
                        op = toks[left_len]
                if op in ('&&', '||'):
                    for side in kids:
                        if is_float(side.type):
                            add('truthy', side, f, text_of(side))
            walk(child, f)

    walk(tu.cursor, '')
    return sites, None

def load_classes():
    classes = {}
    if os.path.exists(TSV):
        for raw in open(TSV, encoding='utf-8'):
            if raw.startswith('#') or not raw.strip():
                continue
            parts = raw.rstrip('\n').split('\t')
            if len(parts) < 6:
                continue
            cat, path, func, expr, klass, reason = parts[:6]
            classes[(cat, path, func, expr)] = (klass, reason)
    return classes

CACHE = os.path.join(ROOT, 'build', 'js_semantics_audit_cache')
SCANNER_VERSION = '7'

def scan_one(args):
    path, flags, use_cache = args
    import hashlib, json
    key = None
    if use_cache:
        digest = hashlib.sha1(SCANNER_VERSION.encode() + open(path, 'rb').read() + path.encode())
        key = os.path.join(CACHE, digest.hexdigest() + '.json')
        try:
            with open(key, encoding='utf-8') as cached:
                sites, error = json.load(cached)
                return path, ([tuple(site) for site in sites], error)
        except (OSError, ValueError):
            pass
    try:
        result = scan(path, flags)
    except Exception as error:  # libclang crash on one file must not hide others
        return path, ([], 'scanner: %s' % error)
    if key is not None and result[1] is None:
        os.makedirs(CACHE, exist_ok=True)
        temporary = '%s.%d.tmp' % (key, os.getpid())
        with open(temporary, 'w', encoding='utf-8') as out:
            json.dump(result, out)
        os.replace(temporary, key)
    return path, result

def main():
    argv = sys.argv[1:]
    if '--help' in argv or '-h' in argv:
        print(__doc__)
        return 0
    prefix = argv[argv.index('--module') + 1] if '--module' in argv else None
    wanted = set(argv[argv.index('--category') + 1].split(',')) if '--category' in argv else None
    jobs = int(argv[argv.index('--jobs') + 1]) if '--jobs' in argv else os.cpu_count() or 4
    show_all = '--all' in argv
    summary_only = '--summary' in argv
    emit_tsv = '--tsv' in argv
    use_cache = '--no-cache' not in argv
    flags = compile_flags()
    files = []
    for d, _, fs in os.walk(os.path.join(ROOT, 'src')):
        for name in fs:
            if name.endswith('.c'):
                rel = os.path.relpath(os.path.join(d, name), os.path.join(ROOT, 'src'))
                if prefix is None or rel.startswith(prefix):
                    files.append(os.path.join(d, name))
    files.sort()
    classes = load_classes()
    counts = collections.Counter()
    unclassified = collections.Counter()
    failures = []
    lines = []
    header_seen = set()
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
        for path, (sites, error) in pool.map(scan_one, [(f, flags, use_cache) for f in files], chunksize=4):
            rel = os.path.relpath(path, ROOT)
            if error:
                failures.append('%s: parse failure: %s' % (rel, error))
                continue
            ts = ts_counterpart(rel)
            if ts is not None and (wanted is None or 'hypot' in wanted):
                ts_text = open(ts, encoding='utf-8', errors='replace').read()
                c_text = open(path, errors='replace').read()
                wide = ts_wide_hypot_calls(ts_text)
                if wide:
                    ported = len(re.findall(r'\b(qa_js_hypot3?|qa_hypot3)\s*\(', c_text))
                    if ported < wide:
                        sites = list(sites) + [('hypot', 0, 'ts-arity',
                                                'Math.hypot with 3+ arguments x%d, qa_js_hypot3/qa_js_hypot x%d'
                                                % (wide, ported), rel)]
                every = len(re.findall(r'Math\.hypot\(', ts_text))
                ported_any = len(re.findall(r'\b(qa_js_hypot[23]?|qa_hypot3|hypot)\s*\(', c_text))
                if every > ported_any:
                    sites = list(sites) + [('hypot', 0, 'ts-count',
                                            'Math.hypot x%d, hypot/qa_js_hypot*/qa_hypot3 x%d (sqrt of a sum is not Math.hypot)'
                                            % (every, ported_any), rel)]
            for cat, line, func, expr, where in sites:
                if where != rel:
                    # A header site: report it once, however many files include it.
                    if (cat, where, line, func, expr) in header_seen:
                        continue
                    header_seen.add((cat, where, line, func, expr))
                site_rel = where
                base, _, rule = cat.partition(':')
                if wanted and base not in wanted:
                    continue
                counts[base] += 1
                klass = (classes.get((base, site_rel, func, expr)) or classes.get((base, site_rel, func, '*'))
                         or classes.get((base, site_rel, '*', '*')))
                if rule:
                    status = 'rule:' + rule
                elif klass:
                    status = klass[0]
                else:
                    status = 'UNCLASSIFIED'
                    unclassified[base] += 1
                if emit_tsv:
                    if status == 'UNCLASSIFIED':
                        lines.append('%s\t%s\t%s\t%s\t?\t# line %d' % (base, site_rel, func, expr, line))
                elif status == 'UNCLASSIFIED' or show_all:
                    lines.append('%s\t%s:%d\t%s\t%s\t%s' % (base, site_rel, line, func, expr, status))
    if not summary_only:
        for line in lines:
            print(line)
        for failure in failures:
            print(failure)
    print('# sites by category (unclassified): ' + ', '.join(
        '%s %d (%d)' % (c, counts[c], unclassified[c]) for c in sorted(counts)), file=sys.stderr)
    print('# %d unclassified, %d parse failures, %d files' % (
        sum(unclassified.values()), len(failures), len(files)), file=sys.stderr)
    return 1 if unclassified or failures else 0

if __name__ == '__main__':
    sys.exit(main())
