#!/usr/bin/env python3
"""Find expressions whose C evaluation order can differ from the TypeScript.

TypeScript evaluates operands strictly left to right. C leaves call arguments
and the operands of most binary operators unsequenced, and initializer-list
entries indeterminately sequenced (C17 6.5p2, 6.5.2.2p10, 6.7.9p23). A port
such as `vec(read(r), read(r), read(r))` may therefore run backwards; GCC on
x86-64 usually evaluates call arguments right to left.

Method
  Every translation unit under src/ is parsed with libclang and the Makefile's
  flags, so macros are expanded and types resolved. Memory is named by access
  paths: a root variable plus segments (`p->a[i].b` is p * a [] b). For each
  function body the tool records the paths it writes and reads, mapped to its
  parameters and globals; local pointer variables are resolved to the roots they
  are assigned from. A whole-program fixpoint maps each call's effects into the
  caller through its arguments. A call through a function pointer reaches every
  function whose address is taken with that function type (closed world; a cast
  to the type from anything but a function makes the type open, hence impure).

  Unsequenced nodes: call (callee and arguments), initializer list, array
  subscript, binary and compound-assignment operators other than && || , and
  the plain-assignment pair (whose store is sequenced after both operands, as in
  JS, so only the operands' own effects count). Two operands conflict when one
  writes a path the other reads or writes. Each flagged site carries a rule:

    call-call     both sides' accesses come from calls
    write-call    a direct write (++, =, +=) against a call's access
    call-access   a call's write against a direct read or write
    write-access  direct writes and reads only (what -Wsequence-point sees)

  Library functions without a body use the tables below; others are impure and
  may write anything. malloc-family results are fresh storage; allocator state
  is not an observable effect.

Classification
  A flagged site is classified when tools/eval_order_ledger.tsv has a line
  `path <TAB> function <TAB> text-hash <TAB> reason`; otherwise it is
  unclassified. A TU that fails to parse is unclassified too. The gate is zero.
  --criterion also counts the cruder rule (two operands each containing a call
  that is not const), which ignores what the calls touch. --strict makes every
  function-pointer call write anything.

Limits
  Only code compiled under the Makefile's configuration is seen. Two differently
  named pointers are assumed not to alias. A local pointer is resolved
  flow-insensitively. A noreturn callee has no effect, so which of two aborts
  fires first is not modelled. Function pointers stored through void ** casts
  (dlsym style) are invisible.
"""

import argparse
import collections
import ctypes
import gzip
import hashlib
import json
import multiprocessing
import os
import shlex
import subprocess
import sys
import threading

import clang.cindex as ci
from clang.cindex import BinaryOperator as BO
from clang.cindex import CursorKind as K
from clang.cindex import StorageClass, TypeKind

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEDGER = os.path.join(ROOT, "tools", "eval_order_ledger.tsv")

LIB_CONST = set(
    """abs labs llabs fabs fabsf floor floorf ceil ceilf trunc truncf round roundf
    rint rintf nearbyint nearbyintf sqrt sqrtf cbrt cbrtf sin sinf cos cosf tan tanf
    asin asinf acos acosf atan atanf atan2 atan2f sinh cosh tanh exp expf exp2 log
    logf log2 log2f log10 log10f pow powf hypot hypotf fmod fmodf fmin fminf fmax
    fmaxf copysign copysignf ldexp ldexpf lround lroundf llround llroundf lrint
    lrintf expm1 log1p asinh acosh atanh isalpha isdigit isalnum isspace isupper
    islower ispunct isprint isxdigit iscntrl isgraph toupper tolower nan nanf
    remainder remainderf scalbn fdim
    __builtin_nan __builtin_nanf __builtin_inf __builtin_inff __builtin_huge_val
    __builtin_huge_valf __builtin_isnan __builtin_isinf __builtin_isfinite
    __builtin_isinf_sign __builtin_signbit __builtin_fabs __builtin_fabsf
    __builtin_expect __builtin_clz __builtin_ctz __builtin_popcount __builtin_clzll
    __builtin_ctzll __builtin_popcountll __builtin_bswap16 __builtin_bswap32
    __builtin_bswap64 __builtin_constant_p __builtin_fpclassify __builtin_isnormal
    __builtin_signbitf __builtin_copysign __builtin_ffs __builtin_clzl __builtin_ctzl
    __builtin_popcountl __builtin_isless __builtin_isgreater __builtin_islessequal
    __builtin_isgreaterequal __builtin_isunordered __builtin_islessgreater
    __isnan __isnanf __isinf __isinff __finite __finitef __fpclassify __fpclassifyf
    __signbit __signbitf ntohs ntohl htons htonl __builtin_va_start __builtin_va_end
    __builtin_va_copy __builtin_va_arg __builtin_unreachable __builtin_trap
    __ctype_b_loc __ctype_tolower_loc __ctype_toupper_loc __errno_location""".split()
)
# Reads through every pointer argument, writes nothing.
LIB_READONLY = set(
    """strlen strnlen strcmp strncmp strcasecmp strncasecmp memcmp strchr strrchr
    memchr strstr strspn strcspn strpbrk getenv atoi atol atoll atof bsearch
    __builtin_memcmp __builtin_strlen __builtin_strcmp""".split()
)
# name -> argument indexes whose pointee is written; reads through all pointers.
LIB_WRITES = {
    "memcpy": [0], "memmove": [0], "memset": [0], "strcpy": [0], "strncpy": [0],
    "strcat": [0], "strncat": [0], "snprintf": [0], "vsnprintf": [0], "sprintf": [0],
    "vsprintf": [0], "frexp": [1], "frexpf": [1], "modf": [1], "modff": [1],
    "strtod": [1], "strtof": [1], "strtol": [1], "strtoul": [1], "strtoll": [1],
    "strtoull": [1], "qsort": [0], "__builtin_memcpy": [0], "__builtin_memset": [0],
    "__builtin_memmove": [0], "__builtin___memcpy_chk": [0], "__builtin___memset_chk": [0],
    "__builtin___memmove_chk": [0], "__builtin___snprintf_chk": [0],
    "__builtin___vsnprintf_chk": [0], "__builtin___strcpy_chk": [0],
    "__builtin___strncpy_chk": [0], "__builtin___strcat_chk": [0],
    "__builtin___sprintf_chk": [0], "__builtin___vsprintf_chk": [0],
    "__builtin_add_overflow": [2], "__builtin_sub_overflow": [2],
    "__builtin_mul_overflow": [2],
}
# Allocation: returns fresh storage; allocator state is not an observable effect.
LIB_ALLOC = {"malloc", "calloc", "realloc", "free", "strdup", "strndup", "aligned_alloc",
             "reallocarray"}
# Output streams share one pseudo-global, so two prints in one expression conflict.
LIB_IO = {"printf", "fprintf", "vfprintf", "vprintf", "puts", "fputs", "fputc", "putc",
          "putchar", "fwrite", "fflush", "perror"}
NORETURN = {"abort", "exit", "_exit", "_Exit", "__assert_fail", "__stack_chk_fail",
            "__builtin_unreachable", "__builtin_trap", "longjmp", "siglongjmp"}

ANY = "..."           # path segment: anything below, through pointers too
STAR = "*"            # path segment: through a pointer
IDX = "[]"            # path segment: an array element
MAXPATH = 4
MAXSET = 64
ANYACC = ("any", "", ())

U_POSTINC, U_POSTDEC, U_PREINC, U_PREDEC, U_ADDR, U_DEREF = 1, 2, 3, 4, 5, 6
NO_OP = {BO.LAnd, BO.LOr, BO.Comma, BO.Invalid}
ASSIGN = {BO.Assign, BO.MulAssign, BO.DivAssign, BO.RemAssign, BO.AddAssign, BO.SubAssign,
          BO.ShlAssign, BO.ShrAssign, BO.AndAssign, BO.XorAssign, BO.OrAssign}
TRANSPARENT = {K.UNEXPOSED_EXPR, K.PAREN_EXPR, K.CSTYLE_CAST_EXPR}
UNEVALUATED = {K.CXX_UNARY_EXPR, K.TYPE_REF}
ARRAYS = (TypeKind.CONSTANTARRAY, TypeKind.INCOMPLETEARRAY, TypeKind.VARIABLEARRAY)

_UNARY = None


def unary_kind(c):
    """clang_getCursorUnaryOperatorKind: the Python binding does not expose it."""
    global _UNARY
    if _UNARY is None:
        _UNARY = ci.conf.lib.clang_getCursorUnaryOperatorKind
        _UNARY.argtypes = [ci.Cursor]
        _UNARY.restype = ctypes.c_int
    return _UNARY(c)


def compile_flags():
    out = subprocess.run(
        ["pkg-config", "--cflags", "sdl2", "freetype2", "vorbisfile", "theoradec"],
        capture_output=True, text=True, check=True).stdout
    res = subprocess.run(["clang", "-print-resource-dir"], capture_output=True, text=True,
                         check=True).stdout.strip()
    return ["-std=c17", "-Iinclude", "-isystem", os.path.join(res, "include")] + shlex.split(out)


def clip(path):
    path = tuple(path)
    if ANY in path:
        path = path[:path.index(ANY) + 1]
    if len(path) > MAXPATH:
        path = path[:MAXPATH] + (ANY,)
    return path


def overlap(p, q):
    """Two paths from one root name overlapping storage."""
    n = min(len(p), len(q))
    for i in range(n):
        a, b = p[i], q[i]
        if a == ANY or b == ANY:
            return True
        if a != b:
            return False
    rest = p[n:] if len(p) > n else q[n:]
    return STAR not in rest  # a longer path stays inside the same object


def fn_type(t):
    """Canonical spelling of a function type, from a function or pointer-to-function."""
    c = t.get_canonical()
    while c.kind == TypeKind.POINTER:
        c = c.get_pointee().get_canonical()
    if c.kind in (TypeKind.FUNCTIONPROTO, TypeKind.FUNCTIONNOPROTO):
        return c.spelling
    return None


class Info:
    """Effects of one expression: calls made, paths written, paths read."""
    __slots__ = ("calls", "writes", "reads")

    def __init__(self):
        self.calls, self.writes, self.reads = set(), set(), set()

    def add(self, o):
        self.calls |= o.calls
        self.writes |= o.writes
        self.reads |= o.reads


class Walker:
    def __init__(self):
        self.funcs, self.decls, self.sites = {}, {}, []
        self.addr_taken = collections.defaultdict(set)   # function type -> keys
        self.open_types = set()
        self.field_funcs = collections.defaultdict(set)  # "struct s.f" -> keys
        self.field_edges = collections.defaultdict(set)  # "struct s.f" <- "struct t.g"
        self.field_open = set()
        self.fn_name = ""
        self.alias = {}
        self.params = {}

    # ---- declarations ----
    def rel(self, loc):
        f = loc.file
        if not f:
            return None
        p = os.path.normpath(os.path.join(ROOT, f.name))
        if not p.startswith(ROOT + os.sep):
            return None
        r = os.path.relpath(p, ROOT)
        return r if r.startswith(("src" + os.sep, "include" + os.sep)) else None

    def key_of(self, d):
        if d.storage_class == StorageClass.STATIC:
            f = d.location.file.name if d.location.file else "?"
            return os.path.relpath(os.path.normpath(os.path.join(ROOT, f)), ROOT) + ":" + d.spelling
        return d.spelling

    def note_decl(self, d):
        key = self.key_of(d)
        if key not in self.decls:
            noret = False
            for t in d.get_tokens():
                if t.spelling in ("_Noreturn", "noreturn", "__noreturn__"):
                    noret = True
                if t.spelling in ("{", ";", "("):
                    break
            self.decls[key] = {"name": d.spelling, "noreturn": noret}
        return key

    # ---- paths ----
    def strip(self, n):
        while n.kind in TRANSPARENT:
            kids = [k for k in n.get_children() if k.kind != K.TYPE_REF]
            if not kids:
                return n
            n = kids[-1]
        return n

    def tkind(self, n):
        return n.type.get_canonical().kind

    def var(self, ref):
        """(kind, root) for a DECL_REF; kind is local/param/global/value."""
        d = ref.referenced
        if d is None:
            return "unknown", ""
        if d.kind == K.PARM_DECL:
            return "param", d.spelling
        if d.kind == K.VAR_DECL:
            if d.storage_class in (StorageClass.STATIC, StorageClass.EXTERN) or \
               d.semantic_parent is None or d.semantic_parent.kind == K.TRANSLATION_UNIT:
                return "global", d.spelling
            return "local", d.spelling
        return "value", d.spelling

    def lpath(self, n):
        """(kind, root, path) of the storage an lvalue designates."""
        n = self.strip(n)
        k = n.kind
        if k == K.DECL_REF_EXPR:
            kind, root = self.var(n)
            return kind, root, ()
        if k == K.MEMBER_REF_EXPR:
            kids = list(n.get_children())
            if not kids:
                return "unknown", "", ()
            base = self.strip(kids[0])
            if self.tkind(base) == TypeKind.POINTER:
                kind, root, p = self.pointee(base)
            else:
                kind, root, p = self.lpath(base)
            return kind, root, clip(p + (n.spelling,))
        if k == K.ARRAY_SUBSCRIPT_EXPR:
            kids = [self.strip(x) for x in n.get_children()]
            if not kids:
                return "unknown", "", ()
            base = kids[0]
            if len(kids) > 1 and self.tkind(base) not in (TypeKind.POINTER,) + ARRAYS:
                base = kids[1]
            if self.tkind(base) in ARRAYS:
                kind, root, p = self.lpath(base)
                return kind, root, clip(p + (IDX,))
            return self.pointee(base)
        if k == K.UNARY_OPERATOR and unary_kind(n) == U_DEREF:
            kids = list(n.get_children())
            return self.pointee(kids[0]) if kids else ("unknown", "", ())
        if k == K.COMPOUND_LITERAL_EXPR:
            return "local", "<literal%d>" % n.extent.start.offset, ()
        if k == K.CALL_EXPR:  # a struct returned by value is a temporary
            return "local", "<temp%d>" % n.extent.start.offset, ()
        if k == K.STRING_LITERAL:
            return "value", "", ()
        return "unknown", "", ()

    def pointee(self, n):
        """(kind, root, path) of what a pointer-valued expression points into."""
        s = self.strip(n)
        k = s.kind
        if k == K.UNARY_OPERATOR and unary_kind(s) == U_ADDR:
            kids = list(s.get_children())
            return self.lpath(kids[0]) if kids else ("unknown", "", ())
        if self.tkind(s) in ARRAYS:
            return self.lpath(s)
        if k == K.STRING_LITERAL:
            return "value", "", ()
        if k == K.BINARY_OPERATOR and s.binary_operator in (BO.Add, BO.Sub):
            for kid in s.get_children():
                ks = self.strip(kid)
                if self.tkind(ks) in (TypeKind.POINTER,) + ARRAYS:
                    return self.pointee(ks)
            return "unknown", "", ()
        if k == K.DECL_REF_EXPR:
            kind, root = self.var(s)
            if kind == "local" and root in self.alias:
                return self.alias[root]
            if kind in ("local", "param", "global"):
                return kind, root, (STAR,)
            if kind == "value":
                return "value", "", ()
            return "unknown", "", ()
        if k in (K.MEMBER_REF_EXPR, K.ARRAY_SUBSCRIPT_EXPR) or \
           (k == K.UNARY_OPERATOR and unary_kind(s) == U_DEREF):
            kind, root, p = self.lpath(s)
            if kind in ("unknown", "value"):
                return kind, root, ()
            return kind, root, clip(p + (STAR,))
        if k == K.CALL_EXPR:
            d = s.referenced
            if d is not None and d.spelling in LIB_ALLOC:
                return "local", "<fresh%d>" % s.extent.start.offset, ()
            return "unknown", "", ()
        if k in (K.INTEGER_LITERAL, K.GNU_NULL_EXPR):
            return "value", "", ()
        if k == K.CONDITIONAL_OPERATOR:
            kids = list(s.get_children())
            if len(kids) == 3:
                return self.join(self.pointee(kids[1]), self.pointee(kids[2]))
        return "unknown", "", ()

    @staticmethod
    def join(a, b):
        if a == b:
            return a
        if a[0] == "value":
            return b
        if b[0] == "value":
            return a
        if a[0] != b[0] or a[1] != b[1] or a[0] == "unknown":
            return "unknown", "", ()
        pa, pb = a[2], b[2]
        i = 0
        while i < min(len(pa), len(pb)) and pa[i] == pb[i]:
            i += 1
        return a[0], a[1], clip(pa[:i] + (ANY,))

    def arg(self, n):
        """How a call argument exposes storage: ['p', kind, root, path] for a pointer
        whose pointee is root/path, ['r', ...] for a struct passed by value, or None."""
        s = self.strip(n)
        t = self.tkind(s)
        if t == TypeKind.POINTER or t in ARRAYS:
            kind, root, p = self.pointee(s)
            if kind == "value":
                return None
            return ("p", kind, root, p)
        if t == TypeKind.RECORD:
            kind, root, p = self.lpath(s)
            return ("r", kind, root, p)
        return None

    # ---- local pointer provenance ----
    def collect_alias(self, body):
        assigns = collections.defaultdict(list)

        def walk(n):
            k = n.kind
            if k == K.VAR_DECL and n.storage_class not in (StorageClass.STATIC, StorageClass.EXTERN):
                if n.type.get_canonical().kind == TypeKind.POINTER:
                    init = [c for c in n.get_children() if c.kind.is_expression()]
                    assigns[n.spelling].append(init[-1] if init else None)
            elif k == K.BINARY_OPERATOR and n.binary_operator == BO.Assign:
                kids = list(n.get_children())
                if len(kids) == 2:
                    lhs = self.strip(kids[0])
                    if lhs.kind == K.DECL_REF_EXPR and self.tkind(lhs) == TypeKind.POINTER:
                        kind, root = self.var(lhs)
                        if kind == "local":
                            assigns[root].append(kids[1])
            elif k == K.UNARY_OPERATOR and unary_kind(n) == U_ADDR:
                kids = list(n.get_children())
                if kids:
                    s = self.strip(kids[0])
                    if s.kind == K.DECL_REF_EXPR and self.tkind(s) == TypeKind.POINTER:
                        kind, root = self.var(s)
                        if kind == "local":
                            assigns[root].append("escape")
            for c in n.get_children():
                walk(c)

        walk(body)
        self.alias = {}
        for _ in range(4):
            new = {}
            for name, srcs in assigns.items():
                acc = None
                for src in srcs:
                    if src is None:
                        continue
                    if src == "escape":
                        acc = ("unknown", "", ())
                        break
                    p = self.pointee(src)
                    acc = p if acc is None else self.join(acc, p)
                    if acc[0] == "unknown":
                        break
                if acc is None:
                    acc = ("value", "", ())
                if acc[0] == "local" and acc[1] == name:
                    acc = ("unknown", "", ())
                # Pointer arithmetic keeps it among elements of the same type, which
                # the index-insensitive path already covers; only a join of
                # different sources widens it (to '...').
                new[name] = acc
            if new == self.alias:
                break
            self.alias = new

    # ---- expression walk ----
    def access(self, lv):
        kind, root, p = self.lpath(lv)
        if kind == "value":
            return None
        if kind == "unknown":
            return ANYACC
        return (kind, root, p)

    def lvalue_info(self, n):
        """Evaluate the address computation of an lvalue, without reading its value."""
        s = self.strip(n)
        info = Info()
        k = s.kind
        if k == K.DECL_REF_EXPR:
            return info
        if k == K.MEMBER_REF_EXPR:
            kids = list(s.get_children())
            if kids:
                base = self.strip(kids[0])
                info.add(self.visit(base) if self.tkind(base) == TypeKind.POINTER
                         else self.lvalue_info(base))
            return info
        if k == K.ARRAY_SUBSCRIPT_EXPR:
            for kid in s.get_children():
                ks = self.strip(kid)
                info.add(self.lvalue_info(ks) if self.tkind(ks) in ARRAYS else self.visit(ks))
            return info
        if k == K.UNARY_OPERATOR and unary_kind(s) == U_DEREF:
            for kid in s.get_children():
                info.add(self.visit(kid))
            return info
        return self.visit(s)

    def site(self, node, kind, op, operands):
        live = [o for o in operands if o[1].calls or o[1].writes]
        if not live or len(operands) < 2 or not self.fn_name:
            return
        if not any(o is not p and (p[1].calls or p[1].writes or p[1].reads)
                   for o in live for p in operands):
            return
        rel = self.rel(node.location)
        if not rel:
            return
        ext = node.extent
        self.sites.append({
            "file": rel, "line": node.location.line, "col": node.location.column,
            "start": ext.start.offset, "end": ext.end.offset,
            "func": self.fn_name, "kind": kind, "op": op,
            "operands": [{
                "calls": sorted(o[1].calls, key=str),
                "writes": sorted(o[1].writes),
                "reads": sorted(o[1].reads),
            } for o in operands],
        })

    def note_function_ref(self, n, cast_type=None):
        d = n.referenced
        if d is None or d.kind != K.FUNCTION_DECL:
            return
        t = cast_type or fn_type(d.type)
        if t:
            self.addr_taken[t].add(self.note_decl(d))

    @staticmethod
    def record_name(ty):
        c = ty.get_canonical()
        if c.kind == TypeKind.POINTER:
            c = c.get_pointee().get_canonical()
        return c.spelling.replace("const ", "").replace("volatile ", "")

    def field_key(self, member):
        kids = list(member.get_children())
        if not kids:
            return None
        return self.record_name(self.strip(kids[0]).type) + "." + member.spelling

    def note_field_value(self, fkey, value):
        """A function-pointer field receives `value`: a function, null, another
        such field, or something untraceable (the field is then open)."""
        s = self.strip(value)
        if s.kind == K.DECL_REF_EXPR and s.referenced is not None and \
           s.referenced.kind == K.FUNCTION_DECL:
            self.field_funcs[fkey].add(self.note_decl(s.referenced))
        elif s.kind in (K.INTEGER_LITERAL, K.GNU_NULL_EXPR):
            pass
        elif s.kind == K.MEMBER_REF_EXPR and fn_type(s.type):
            src = self.field_key(s)
            if src:
                self.field_edges[fkey].add(src)
            else:
                self.field_open.add(fkey)
        else:
            self.field_open.add(fkey)

    def note_init_fields(self, n):
        c = n.type.get_canonical()
        if c.kind != TypeKind.RECORD:
            return
        decl = c.get_declaration()
        fields = [f for f in decl.get_children() if f.kind == K.FIELD_DECL]
        rec = self.record_name(n.type)
        idx = 0
        for child in n.get_children():
            kids = list(child.get_children()) if child.kind == K.UNEXPOSED_EXPR else []
            designators = [k for k in kids if k.kind == K.MEMBER_REF]
            if designators:
                if len(designators) != 1:
                    continue  # nested designator: the inner list is visited on its own
                name = designators[0].spelling
                idx = next((i for i, f in enumerate(fields) if f.spelling == name), len(fields))
                value = kids[-1]
            else:
                value = child
            if idx < len(fields):
                f = fields[idx]
                if fn_type(f.type) and f.type.get_canonical().kind == TypeKind.POINTER:
                    self.note_field_value(rec + "." + f.spelling, value)
            idx += 1

    def visit(self, n):
        kind = n.kind
        info = Info()
        if kind in UNEVALUATED:
            return info
        if kind == K.CALL_EXPR:
            kids = list(n.get_children())
            operands = []
            for i, k in enumerate(kids):
                s = self.strip(k)
                if i == 0 and s.kind == K.DECL_REF_EXPR and s.referenced is not None and \
                   s.referenced.kind == K.FUNCTION_DECL:
                    operands.append((k, Info()))  # a direct callee: not an address taken
                else:
                    operands.append((k, self.visit(k)))
            for _, i in operands:
                info.add(i)
            d = n.referenced
            args = tuple(self.arg(k) for k in kids[1:])
            if d is not None and d.kind == K.FUNCTION_DECL:
                info.calls.add((self.note_decl(d), d.spelling, None, args))
            else:
                ftype = fn_type(kids[0].type) if kids else None
                callee = self.strip(kids[0]) if kids else None
                if ftype and callee is not None and callee.kind == K.MEMBER_REF_EXPR:
                    fkey = self.field_key(callee)
                    if fkey:
                        ftype = "F" + fkey + "\x00" + ftype
                info.calls.add((None, n.spelling or "<indirect>", ftype, args))
            self.site(n, "call", n.spelling or "<indirect>", operands)
            return info
        if kind == K.INIT_LIST_EXPR:
            self.note_init_fields(n)
            operands = [(k, self.visit(k)) for k in n.get_children()]
            for _, i in operands:
                info.add(i)
            self.site(n, "init", "{}", operands)
            return info
        if kind in (K.BINARY_OPERATOR, K.COMPOUND_ASSIGNMENT_OPERATOR):
            kids = list(n.get_children())
            op = n.binary_operator
            if len(kids) != 2:
                for k in kids:
                    info.add(self.visit(k))
                return info
            if op == BO.Assign:
                lhs = self.strip(kids[0])
                if lhs.kind == K.MEMBER_REF_EXPR and fn_type(lhs.type) and \
                   lhs.type.get_canonical().kind == TypeKind.POINTER:
                    fkey = self.field_key(lhs)
                    if fkey:
                        self.note_field_value(fkey, kids[1])
            if op in ASSIGN:
                # The store is sequenced after both operands (C17 6.5.16p3), as in
                # JS, so it is not an operand effect here; it counts for a parent.
                left = self.lvalue_info(kids[0])
                acc = self.access(kids[0])
                if op != BO.Assign and acc:
                    left.reads.add(acc)
                right = self.visit(kids[1])
                info.add(left)
                info.add(right)
                if acc:
                    info.writes.add(acc)
                self.site(n, "assign", op.name, [(kids[0], left), (kids[1], right)])
                return info
            a, b = self.visit(kids[0]), self.visit(kids[1])
            info.add(a)
            info.add(b)
            if op not in NO_OP:
                self.site(n, "binary", op.name, [(kids[0], a), (kids[1], b)])
            return info
        if kind == K.ARRAY_SUBSCRIPT_EXPR:
            operands = []
            for k in n.get_children():
                ks = self.strip(k)
                operands.append((k, self.lvalue_info(ks) if self.tkind(ks) in ARRAYS else self.visit(k)))
            for _, i in operands:
                info.add(i)
            acc = self.access(n)
            if acc:
                info.reads.add(acc)
            self.site(n, "subscript", "[]", operands)
            return info
        if kind == K.UNARY_OPERATOR:
            u = unary_kind(n)
            kids = list(n.get_children())
            if u in (U_POSTINC, U_POSTDEC, U_PREINC, U_PREDEC) and kids:
                info.add(self.lvalue_info(kids[0]))
                acc = self.access(kids[0])
                if acc:
                    info.reads.add(acc)
                    info.writes.add(acc)
                return info
            if u == U_ADDR and kids:
                inner = self.strip(kids[0])
                if inner.kind == K.DECL_REF_EXPR:
                    self.note_function_ref(inner)
                info.add(self.lvalue_info(kids[0]))
                return info
            if u == U_DEREF and kids:
                info.add(self.visit(kids[0]))
                acc = self.access(n)
                if acc:
                    info.reads.add(acc)
                return info
            for k in kids:
                info.add(self.visit(k))
            return info
        if kind == K.MEMBER_REF_EXPR:
            kids = list(n.get_children())
            if kids:
                base = self.strip(kids[0])
                info.add(self.visit(base) if self.tkind(base) == TypeKind.POINTER
                         else self.lvalue_info(base))
            acc = self.access(n)
            if acc:
                info.reads.add(acc)
            return info
        if kind == K.DECL_REF_EXPR:
            d = n.referenced
            if d is not None and d.kind == K.FUNCTION_DECL:
                self.note_function_ref(n)
                return info
            k, root = self.var(n)
            if k in ("local", "param", "global"):
                if k == "global" and d is not None and d.type.get_canonical().is_const_qualified():
                    return info
                info.reads.add((k, root, ()))
            return info
        if kind == K.CSTYLE_CAST_EXPR:
            kids = [k for k in n.get_children() if k.kind != K.TYPE_REF]
            t = fn_type(n.type) if self.tkind(n) == TypeKind.POINTER else None
            if t and kids:
                inner = self.strip(kids[-1])
                if inner.kind == K.DECL_REF_EXPR and inner.referenced is not None and \
                   inner.referenced.kind == K.FUNCTION_DECL:
                    self.note_function_ref(inner, t)
                elif fn_type(inner.type) != t and inner.kind not in (K.INTEGER_LITERAL,
                                                                     K.GNU_NULL_EXPR):
                    self.open_types.add(t)
            for k in kids:
                info.add(self.visit(k))
            return info
        for k in n.get_children():
            info.add(self.visit(k))
        return info

    def function(self, cur, rel):
        self.fn_name = cur.spelling
        self.params = {a.spelling: i for i, a in enumerate(cur.get_arguments())}
        body = [k for k in cur.get_children() if k.kind == K.COMPOUND_STMT]
        if not body:
            return
        self.collect_alias(body[0])
        info = self.visit(body[0])
        key = self.note_decl(cur)
        self.funcs[key] = {
            "file": rel, "line": cur.location.line, "name": cur.spelling,
            "params": self.params,
            "writes": sorted(info.writes), "reads": sorted(info.reads),
            "calls": sorted(info.calls, key=str),
        }


def scan_tu(path):
    result = {}

    def run():
        tu = ci.Index.create().parse(path, args=compile_flags())
        errors = [str(d) for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
        w = Walker()
        for cur in tu.cursor.get_children():
            if cur.kind == K.FUNCTION_DECL and cur.is_definition():
                if w.rel(cur.location):
                    w.function(cur, w.rel(cur.location))
            elif cur.kind == K.VAR_DECL:
                w.fn_name = ""  # function addresses in global tables count as taken
                w.alias = {}
                for c in cur.get_children():
                    w.visit(c)
        deps = sorted({os.path.relpath(i.include.name, ROOT) for i in tu.get_includes()
                       if i.include and os.path.normpath(os.path.join(ROOT, i.include.name))
                       .startswith(ROOT + os.sep)})
        result.update(path=path, errors=errors[:3], funcs=w.funcs, decls=w.decls, sites=w.sites,
                      addr_taken={k: sorted(v) for k, v in w.addr_taken.items()},
                      open_types=sorted(w.open_types), deps=deps,
                      field_funcs={k: sorted(v) for k, v in w.field_funcs.items()},
                      field_edges={k: sorted(v) for k, v in w.field_edges.items()},
                      field_open=sorted(w.field_open))

    sys.setrecursionlimit(200000)
    threading.stack_size(512 * 1024 * 1024)
    t = threading.Thread(target=run)
    t.start()
    t.join()
    if not result:
        result = {"path": path, "errors": ["walker crashed"], "funcs": {}, "decls": {}, "sites": [],
                  "addr_taken": {}, "open_types": [], "deps": []}
    return result


# Bump when Walker changes what it records; cached TU scans are then redone.
WALKER_VERSION = "5"


def stamp(paths):
    h = hashlib.sha1(WALKER_VERSION.encode())
    for p in paths:
        try:
            st = os.stat(os.path.join(ROOT, p))
            h.update(f"{p}:{st.st_mtime_ns}:{st.st_size};".encode())
        except OSError:
            h.update(f"{p}:missing;".encode())
    return h.hexdigest()


def scan_cached(args):
    path, cache_dir = args
    os.chdir(ROOT)
    cache = os.path.join(cache_dir, hashlib.sha1(path.encode()).hexdigest() + ".json.gz")
    try:
        with gzip.open(cache, "rt") as f:
            old = json.load(f)
        if old["stamp"] == stamp([path] + old["deps"]):
            return old
    except (OSError, ValueError, KeyError, EOFError):
        pass
    res = scan_tu(path)
    res["stamp"] = stamp([path] + res["deps"])
    if not res["errors"]:
        tmp = cache + f".{os.getpid()}"
        try:
            with gzip.open(tmp, "wt", compresslevel=3) as f:
                json.dump(res, f)
            os.replace(tmp, cache)
        except OSError:
            if os.path.exists(tmp):
                os.unlink(tmp)
    return res


# ---- whole program ----
def tup(x):
    return tuple(tup(i) for i in x) if isinstance(x, list) else x


class Program:
    """Per-function effects in callee terms: ('param', index, path), ('global', name,
    path), ('io', '', ()), or ('any', '', ())."""

    def __init__(self, results, strict=False):
        self.funcs, self.decls = {}, {}
        self.addr_taken = collections.defaultdict(set)
        self.open_types = set()
        field_funcs = collections.defaultdict(set)
        field_edges = collections.defaultdict(set)
        field_open = set()
        for r in results:
            for k, f in r["funcs"].items():
                f["writes"] = [tup(x) for x in f["writes"]]
                f["reads"] = [tup(x) for x in f["reads"]]
                f["calls"] = [tup(x) for x in f["calls"]]
                self.funcs[k] = f
            self.decls.update(r["decls"])
            for t, keys in r["addr_taken"].items():
                self.addr_taken[t].update(keys)
            self.open_types.update(r["open_types"])
            for k, v in r.get("field_funcs", {}).items():
                field_funcs[k].update(v)
            for k, v in r.get("field_edges", {}).items():
                field_edges[k].update(v)
            field_open.update(r.get("field_open", []))
        # A field's targets: functions stored into it, plus whatever flows in from
        # other fields; open if any source is untraceable.
        self.field_targets = {}
        for f in set(field_funcs) | set(field_edges) | field_open:
            seen, stack, keys, is_open = {f}, [f], set(), False
            while stack:
                g = stack.pop()
                if g in field_open:
                    is_open = True
                    break
                keys |= field_funcs.get(g, set())
                for h in field_edges.get(g, ()):
                    if h not in seen:
                        seen.add(h)
                        stack.append(h)
            self.field_targets[f] = None if is_open else frozenset(keys)
        self.strict = strict
        self.eff = {k: (frozenset(), frozenset()) for k in self.funcs}
        self.ptr_eff = {}
        self.map_cache = {}
        self.passing = False
        self.solve()

    def noreturn(self, key, name):
        return name in NORETURN or self.decls.get(key, {}).get("noreturn", False)

    @staticmethod
    def lib(name, nargs):
        """Library effects in callee terms, or None if the function is unknown."""
        if name in LIB_CONST:
            return frozenset(), frozenset()
        # These touch only the bytes their pointers address, never memory that
        # pointers stored in those bytes lead to.
        reads = frozenset(("param", i, (STAR,)) for i in range(nargs))
        if name in LIB_READONLY:
            return frozenset(), reads
        if name in LIB_WRITES:
            return frozenset(("param", i, (STAR,)) for i in LIB_WRITES[name]), reads
        if name in LIB_ALLOC:
            return frozenset(), frozenset()
        if name in LIB_IO:
            return frozenset({("io", "", ())}), reads
        return None

    def direct_effects(self, key, name, nargs):
        if self.noreturn(key, name):
            return frozenset(), frozenset()
        if key in self.eff:
            return self.eff[key]
        lib = self.lib(name, nargs)
        if lib is not None:
            return lib
        return frozenset({ANYACC}), frozenset({ANYACC})

    def callee_effects(self, key, name, ftype, nargs):
        """(writes, reads) in callee terms for one call."""
        if key is not None:
            return self.direct_effects(key, name, nargs)
        if ftype is not None and ftype.startswith("F"):
            fkey, _, ftype = ftype[1:].partition("\x00")
            targets = None if self.strict else self.field_targets.get(fkey)
            if targets is not None and targets:
                w, r = set(), set()
                for k in targets:
                    cw, cr = self.direct_effects(k, self.decls.get(k, {}).get("name", k), nargs)
                    w |= cw
                    r |= cr
                return self.compact(w), self.compact(r)
        if self.strict or ftype is None or ftype in self.open_types or ftype not in self.addr_taken:
            return frozenset({ANYACC}), frozenset({ANYACC})
        if self.passing:
            return self.ptr_eff.get(ftype, (frozenset(), frozenset()))
        cached = self.ptr_eff.get(ftype)
        if cached is None:
            w, r = set(), set()
            for k in self.addr_taken[ftype]:
                cw, cr = self.direct_effects(k, self.decls.get(k, {}).get("name", k), 0)
                w |= cw
                r |= cr
            cached = self.ptr_eff[ftype] = (self.compact(w), self.compact(r))
        return cached

    @staticmethod
    def map_one(acc, args):
        """Map a callee-terms access to caller terms through the call's arguments."""
        kind, root, path = acc
        if kind != "param":
            return acc
        if root >= len(args):
            return ANYACC if path[:1] in ((STAR,), (ANY,)) else None
        a = args[root]
        if path and path[0] in (STAR, ANY):
            rest = path[1:] if path[0] == STAR else (ANY,)
            if a is None:
                return None
            if a[0] != "p" or a[1] == "unknown":
                return ANYACC
            return (a[1], a[2], clip(tuple(a[3]) + tuple(rest)))
        if STAR in path or ANY in path:
            if a is not None and a[0] == "r":
                if a[1] == "unknown":
                    return ANYACC
                return (a[1], a[2], clip(tuple(a[3]) + tuple(path)))
            return ANYACC if a is not None else None
        return None  # the parameter's own copy

    def mapped(self, call):
        key, name, ftype, args = call
        eff = self.callee_effects(key, name, ftype, len(args))
        hit = self.map_cache.get(call)
        if hit is not None and hit[0] is eff:
            return hit[1]
        w, r = eff
        mw = frozenset(m for m in (self.map_one(a, args) for a in w) if m)
        mr = frozenset(m for m in (self.map_one(a, args) for a in r) if m)
        self.map_cache[call] = (eff, (mw, mr))
        return mw, mr

    @staticmethod
    def to_callee_terms(acc, params):
        kind, root, path = acc
        if kind == "param":
            if root not in params:
                return ANYACC
            if STAR not in path and ANY not in path:
                return None  # the parameter's own copy
            return ("param", params[root], path)
        if kind == "local":
            if str(root).startswith("<"):
                return None  # a literal, temporary, or fresh allocation
            # '...' inside a local names any part of that local; only a '*'
            # leads out of it.
            return None if STAR not in path else ANYACC
        if kind in ("global", "io", "any"):
            return acc
        return ANYACC

    @staticmethod
    def compact(s):
        s = set(s)
        if ANYACC in s:
            return frozenset({ANYACC} | {x for x in s if x[0] == "io"})
        if len(s) <= MAXSET:
            return frozenset(s)
        out = {(k, r, (STAR, ANY) if k == "param" else (ANY,)) for k, r, _ in s}
        if len(out) > MAXSET:
            return frozenset({ANYACC})
        return frozenset(out)

    def dependencies(self, fn):
        deps = set()
        for key, _, ftype, _ in fn["calls"]:
            if key is not None:
                deps.add(key)
                continue
            if ftype is not None and ftype.startswith("F"):
                fkey, _, ftype = ftype[1:].partition("\x00")
                targets = self.field_targets.get(fkey)
                if targets:
                    deps.update(targets)
                    continue
            if ftype is not None and ftype in self.addr_taken:
                deps.add(("type", ftype))
        return deps

    def summarize(self, fn):
        params = fn["params"]
        if "own" not in fn:
            ow = {m for m in (self.to_callee_terms(a, params) for a in fn["writes"]) if m}
            orr = {m for m in (self.to_callee_terms(a, params) for a in fn["reads"]) if m}
            fn["own"] = (ow, orr)
        w, r = set(fn["own"][0]), set(fn["own"][1])
        for call in fn["calls"]:
            cw, cr = self.mapped(call)
            for a in cw:
                m = self.to_callee_terms(a, params)
                if m:
                    w.add(m)
            for a in cr:
                m = self.to_callee_terms(a, params)
                if m:
                    r.add(m)
        return w, r

    def solve(self):
        """Worklist fixpoint: effects only grow, and a change re-queues the callers."""
        users = collections.defaultdict(set)
        member_of = collections.defaultdict(set)
        for t, keys in self.addr_taken.items():
            for k in keys:
                member_of[k].add(("type", t))
        for key, fn in self.funcs.items():
            for d in self.dependencies(fn):
                users[d].add(key)
        # Callees first (DFS postorder), so most functions settle in one visit.
        order, seen = [], set()
        for root in self.funcs:
            if root in seen:
                continue
            seen.add(root)
            stack = [(root, iter(self.dependencies(self.funcs[root])))]
            while stack:
                node, it = stack[-1]
                nxt = None
                for d in it:
                    if d in self.funcs and d not in seen:
                        nxt = d
                        break
                if nxt is None:
                    stack.pop()
                    order.append(node)
                else:
                    seen.add(nxt)
                    stack.append((nxt, iter(self.dependencies(self.funcs[nxt]))))
        # Function-pointer effects are refreshed between passes, not inside one:
        # one change to a widely shared callback type would otherwise requeue
        # thousands of callers per step.
        frozen = {}
        steps = 0
        pending = list(order)
        while pending:
            self.ptr_eff = frozen
            self.passing = True
            work = collections.deque(pending)
            queued = set(pending)
            while work:
                key = work.popleft()
                queued.discard(key)
                steps += 1
                w, r = self.summarize(self.funcs[key])
                old = self.eff[key]
                new = (self.compact(old[0] | w), self.compact(old[1] | r))
                if new == old:
                    continue
                self.eff[key] = new
                for u in users[key]:
                    if u not in queued:
                        queued.add(u)
                        work.append(u)
            self.ptr_eff = {}
            self.passing = False
            fresh = {}
            for t in self.addr_taken:
                if t in self.open_types:
                    continue
                fresh[t] = self.callee_effects(None, "", t, 0)
            changed = {t for t in fresh if fresh[t] != frozen.get(t)}
            frozen = fresh
            pending = sorted({u for t in changed for u in users[("type", t)]})
        self.rounds = steps

    def tier(self, key):
        w, r = self.eff[key]
        if not w and not r:
            return "const"
        if not w:
            return "readonly"
        if all(x[0] == "param" for x in w):
            return "params"
        return "impure"


def conflicts(a, b):
    """True when write a overlaps access b."""
    if a[0] == "any" or b[0] == "any":
        other = b if a[0] == "any" else a
        if other[0] == "local" and STAR not in other[2] and ANY not in other[2]:
            return False  # a local whose storage nothing else can name
        if other[0] == "io":
            return a[0] == b[0]
        return True
    if a[0] != b[0] or a[1] != b[1]:
        return False
    return overlap(a[2], b[2])


def operand_effects(prog, op):
    dw = {tup(x) for x in op["writes"]}
    dr = {tup(x) for x in op["reads"]}
    cw, cr = set(), set()
    for call in op["calls"]:
        w, r = prog.mapped(tup(call))
        cw |= w
        cr |= r
    return dw, dr, cw, cr


def pair_rule(ea, eb):
    """Rule for one ordered operand pair. A conflict that exists only through an
    unknown effect ('any') counts when both sides are calls (two impure calls in
    one expression); otherwise it is 'unknown': a call with unknown effects next
    to a plain read, which the tool cannot decide."""
    dwa, dra, cwa, cra = ea
    dwb, drb, cwb, crb = eb
    fallback = None
    for wa, a_call in [(x, False) for x in dwa] + [(x, True) for x in cwa]:
        for xb, b_call in [(x, False) for x in dwb | drb] + [(x, True) for x in cwb | crb]:
            if not conflicts(wa, xb):
                continue
            if a_call and b_call:
                rule = "call-call"
            elif a_call:
                rule = "call-access"
            elif b_call:
                rule = "write-call"
            else:
                rule = "write-access"
            if wa[0] != "any" and xb[0] != "any":
                return rule
            if rule == "call-call":
                fallback = "call-call"
            elif fallback is None:
                fallback = "unknown"
    return fallback


_TEXT = {}


def site_text(s):
    data = _TEXT.get(s["file"])
    if data is None:
        with open(os.path.join(ROOT, s["file"]), "rb") as f:
            data = _TEXT[s["file"]] = f.read()
    return " ".join(data[s["start"]:s["end"]].decode("utf-8", "replace").split())


def load_ledger():
    led = {}
    if os.path.exists(LEDGER):
        with open(LEDGER) as f:
            for line in f:
                if line.startswith("#") or not line.strip():
                    continue
                p = line.rstrip("\n").split("\t")
                if len(p) >= 4:
                    led[(p[0], p[1], p[2])] = p[3]
    return led


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("paths", nargs="*", help="limit reporting to these path prefixes")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    ap.add_argument("--list", action="store_true", help="print each unclassified site")
    ap.add_argument("--json", help="write every flagged site to this file")
    ap.add_argument("--cache", default=os.path.join(ROOT, "build", "eval_order_cache"))
    ap.add_argument("--strict", action="store_true",
                    help="every function-pointer call may write anything")
    ap.add_argument("--criterion", action="store_true",
                    help="also count sites with two operands containing non-const calls")
    ap.add_argument("--effects", action="append", default=[], help="print a function's effects")
    args = ap.parse_args()
    os.chdir(ROOT)
    os.makedirs(args.cache, exist_ok=True)

    sources = []
    for d, _, names in os.walk("src"):
        sources += [os.path.join(d, n) for n in names if n.endswith(".c")]
    sources.sort()
    with multiprocessing.Pool(args.jobs) as pool:
        results = pool.map(scan_cached, [(p, args.cache) for p in sources], chunksize=4)

    failed = [r for r in results if r["errors"]]
    prog = Program(results, strict=args.strict)
    for name in args.effects:
        for k in prog.funcs:
            if prog.funcs[k]["name"] == name or k == name:
                w, r = prog.eff[k]
                print(f"{k}: {prog.tier(k)}\n  writes {sorted(w)}\n  reads {sorted(r)}")
    led = load_ledger()

    seen, flagged, crit, unknown = set(), [], 0, []
    for r in results:
        for s in r["sites"]:
            sid = (s["file"], s["start"], s["end"], s["kind"], s["op"])
            if sid in seen:
                continue
            seen.add(sid)
            if args.paths and not any(s["file"].startswith(p) for p in args.paths):
                continue
            effs = [operand_effects(prog, o) for o in s["operands"]]
            if args.criterion and sum(1 for e in effs if e[2] or e[3]) >= 2:
                crit += 1
            rules = set()
            for i, a in enumerate(effs):
                for j, b in enumerate(effs):
                    if i != j:
                        rule = pair_rule(a, b)
                        if rule:
                            rules.add(rule)
            if rules == {"unknown"}:
                unknown.append(s)
                continue
            rules.discard("unknown")
            if rules:
                s["rules"] = sorted(rules)
                s["text"] = site_text(s)
                s["hash"] = hashlib.sha1(s["text"].encode()).hexdigest()[:12]
                s["callees"] = sorted({c[1] for o in s["operands"] for c in o["calls"]})
                s["ledger"] = led.get((s["file"], s["func"], s["hash"]))
                flagged.append(s)

    unclassified = [s for s in flagged if not s["ledger"]]
    flagged.sort(key=lambda s: (s["file"], s["start"]))
    unclassified.sort(key=lambda s: (s["file"], s["start"]))
    by_rule = collections.Counter(r for s in flagged for r in s["rules"])
    tiers = collections.Counter(prog.tier(k) for k in prog.funcs)
    print(f"translation units: {len(sources)}, parse failures: {len(failed)}")
    for r in failed:
        print(f"  parse failure {r['path']}: {r['errors'][0][:200]}")
    print(f"functions: {len(prog.funcs)} ({' '.join(f'{k}={v}' for k, v in sorted(tiers.items()))}), "
          f"fixpoint steps {prog.rounds}")
    print(f"function-pointer types: {len(prog.addr_taken)} with known targets, "
          f"{len(prog.open_types)} open")
    if args.criterion:
        print(f"sites with two operands holding non-const calls: {crit}")
    print(f"flagged sites: {len(flagged)} ({' '.join(f'{k}={v}' for k, v in sorted(by_rule.items()))})")
    print(f"ledgered: {len(flagged) - len(unclassified)}  unclassified: {len(unclassified)}")
    print(f"undecidable (a call with unknown effects beside a plain read): {len(unknown)}")
    if args.json:
        with open(args.json, "w") as f:
            json.dump(flagged, f)
        for s in unknown:
            s["text"] = site_text(s)
        with open(args.json + ".unknown", "w") as f:
            json.dump(unknown, f)
    if args.list:
        for s in unclassified:
            print(f"{s['file']}:{s['line']}:{s['col']}: [{','.join(s['rules'])}] {s['func']} "
                  f"{s['hash']}: {s['text'][:160]}")
    return 1 if unclassified or failed else 0


if __name__ == "__main__":
    sys.exit(main())
