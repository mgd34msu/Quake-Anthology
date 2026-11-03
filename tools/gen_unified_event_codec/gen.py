"""Transliterate unified-event-codec.ts into C, function by function.
Any construct outside the known subset raises; nothing is skipped."""
import json, re, sys
import tsparse

TS = sys.path[0] + '/../../../quake-anthology-ts/src/app/bootstrap/network/unified-event-codec.ts'
OUT = sys.path[0] + '/../../src/app/bootstrap/network/unified_event_codec.c'
TEMPLATE = sys.path[0] + '/template_head.c'
TAIL = sys.path[0] + '/template_tail.c'

HAND = {
    'readActorId', 'writeActorId', 'readClientId', 'writeClientId', 'readSeatId', 'writeSeatId',
    'readResourceId', 'writeResourceId', 'readContentId', 'readEventQ2PresentationEventPoiFields',
    'writeEventQ2PresentationEventPoiFields', 'readSourcePlayerState', 'writeSourcePlayerState',
    'sourceClientInteger', 'writeUnifiedSimulationEvent', 'readUnifiedSimulationEvent',
    'encodeUnifiedPresentationEvents', 'decodeUnifiedPresentationEvents',
}


def snake(name):
    name = re.sub(r'(?<=[A-Z])([A-Z][a-z])', r'_\1', name.lstrip('#'))
    return re.sub(r'(?<=[a-z0-9])([A-Z])', r'_\1', name).lower()


def cstr(s):
    return json.dumps(s, ensure_ascii=True)


def numval(e):
    if e[0] == 'num':
        return e[1]
    if e[0] == 'neg':
        return -numval(e[1])
    raise ValueError(e)


def cnum(v):
    return repr(int(v)) + '.0' if v == int(v) else repr(v)


class Fn:
    lifted = []  # emitted helper functions (text)

    def __init__(self, name, cname, mode, param):
        self.name, self.cname, self.mode, self.param = name, cname, mode, param
        self.lines = []
        self.depth = 1
        self.scopes = [{param: 'rd' if mode == 'read' else 'borrow'}]
        self.cleanup = [[]]
        self.n = 0

    def e(self, text):
        self.lines.append('  ' * self.depth + text)

    def tmp(self, prefix='t'):
        self.n += 1
        return f'{prefix}{self.n}'

    def kind(self, name):
        for s in reversed(self.scopes):
            if name in s:
                return s[name]
        return None

    def push(self):
        self.scopes.append({})
        self.cleanup.append([])

    def pop(self, emit=True):
        if emit:
            self.flush(self.cleanup[-1])
        self.scopes.pop()
        self.cleanup.pop()

    def flush(self, items):
        for c in reversed(items):
            self.e(c)

    def flush_all(self):
        for items in reversed(self.cleanup):
            self.flush(items)

    # readers -----------------------------------------------------------
    def is_rd(self, e):
        if e[0] == 'id':
            return self.kind(e[1]) == 'rd' or self.kind(e[1]) == 'rdlocal'
        if e[0] == 'call' and e[1][0] == 'member' and e[1][2] == 'field':
            return self.is_rd(e[1][1])
        if e[0] == 'index' and e[1][0] == 'id' and self.kind(e[1][1]) == 'items':
            return True
        return False

    def rd(self, e):
        if e[0] == 'id':
            k = self.kind(e[1])
            if k == 'rd':
                return e[1]
            if k == 'rdlocal':
                return '&' + e[1]
            raise ValueError(f'{self.name}: not a reader {e}')
        if e[0] == 'call' and e[1][0] == 'member' and e[1][2] == 'field':
            base = self.rd(e[1][1])
            t = self.tmp('f')
            self.e(f'qa_save_reader {t};')
            self.e(f'ev_field({base}, {cstr(e[2][0][1])}, &{t});')
            self.cleanup[-1].append(f'qa_save_reader_destroy(&{t});')
            return '&' + t
        raise ValueError(f'{self.name}: not a reader {e}')

    def split_field(self, e):
        """reader.field("x") => (base C reader, "x") when no temp is needed for base."""
        if e[0] == 'call' and e[1][0] == 'member' and e[1][2] == 'field':
            return self.rd(e[1][1]), e[2][0][1]
        return None

    def lits(self, args):
        parts = []
        for a in args:
            if a[0] == 'str':
                parts.append(f'{{{cstr(a[1])}, 0}}')
            else:
                parts.append(f'{{NULL, {cnum(numval(a))}}}')
        return f'(const ev_lit[]){{{", ".join(parts)}}}, {len(parts)}'

    def method(self, recv, meth, args):
        """reader method returning an owned value."""
        sf = self.split_field(recv)
        pre, base = ('ev_f_', f'{sf[0]}, {cstr(sf[1])}') if sf else ('ev_r_', self.rd(recv))
        if meth in ('finite', 'string', 'boolean', 'number'):
            return f'{pre}{meth}({base})'
        if meth == 'integer':
            if args:
                return f'{pre}integer({base}, {cnum(numval(args[0]))})'
            return f'{pre}integer({base}, -9007199254740991.0)'
        if meth == 'literal':
            return f'{pre}choice({base}, {self.lits(args)})' if False else f'{pre}literal({base}, {self.lits(args)})'
        if meth == 'choice':
            return f'{pre}choice({base}, {self.lits(args)})'
        raise ValueError(f'{self.name}: method {meth}')

    # conditions -----------------------------------------------------------
    def cond(self, e):
        k = e[0]
        if k == 'or':
            return f'({self.cond(e[1])} || {self.cond(e[2])})'
        if k == 'and':
            return f'({self.cond(e[1])} && {self.cond(e[2])})'
        if k == 'not':
            return f'!{self.cond(e[1])}'
        if k == 'ne':
            return '!(' + self.cond(('eq', e[1], e[2])) + ')'
        if k == 'call' and e[1] == ('member', ('id', 'Number'), 'isSafeInteger', False):
            return f'ev_safe_integer({self.borrow(e[2][0])})'
        if k == 'eq':
            l, r = e[1], e[2]
            if l[0] == 'member' and l[2] == 'length' and l[1][0] == 'id' and self.kind(l[1][1]) == 'items':
                return f'{l[1][1]}_length == {int(numval(r))}'
            if l[0] == 'typeof':
                inner = l[1]
                if inner[0] != 'member' or inner[2] != 'value' or r != ('str', 'string'):
                    raise ValueError(f'{self.name}: typeof {e}')
                return f'ev_r_is_string({self.rd(inner[1])})'
            if r == ('const', 'undefined') or r == ('const', 'null'):
                what = r[1]
                if l[0] == 'member' and l[2] == 'value' and self.is_rd(l[1]):
                    sf = self.split_field(l[1])
                    if sf:
                        return f'ev_f_{what}({sf[0]}, {cstr(sf[1])})'
                    return f'ev_r_{what}({self.rd(l[1])})'
                return f'ev_{what}_is({self.borrow(l)})'
            if r[0] == 'str':
                if l[0] == 'call':
                    return f'ev_take_eq({self.value(l)}, {cstr(r[1])})'
                return f'ev_eq({self.borrow(l)}, {cstr(r[1])})'
        raise ValueError(f'{self.name}: cond {e}')

    # borrowed values ------------------------------------------------------
    def borrow(self, e):
        k = e[0]
        if k == 'id':
            kind = self.kind(e[1])
            if kind in ('borrow', 'val'):
                return e[1]
            raise ValueError(f'{self.name}: id {e[1]} kind {kind}')
        if k == 'member':
            return f'ev_get({self.borrow(e[1])}, {cstr(e[2])})'
        if k == 'index':
            if e[2][0] == 'str':
                return f'ev_get({self.borrow(e[1])}, {cstr(e[2][1])})'
            if e[2][0] == 'num':
                return f'ev_at({self.borrow(e[1])}, {int(e[2][1])})'
        raise ValueError(f'{self.name}: borrow {e}')

    # owned values ------------------------------------------------------
    def value(self, e):
        k = e[0]
        if k == 'const':
            return {'null': 'ev_null()', 'undefined': 'ev_undefined()', 'true': 'ev_bool(true)', 'false': 'ev_bool(false)'}[e[1]]
        if k == 'str':
            return f'ev_string({cstr(e[1])})'
        if k == 'num' or k == 'neg':
            return f'ev_number({cnum(numval(e))})'
        if k == 'id' or k == 'member' or k == 'index':
            return f'ev_clone({self.borrow(e)})'
        if k == 'obj':
            return self.obj(e)
        if k == 'array':
            t = self.tmp('a')
            self.e(f'qa_save_value *{t} = ev_array();')
            for item in e[1]:
                if item[0] == 'spread':
                    raise ValueError(f'{self.name}: array spread')
                self.e(f'ev_push({t}, {self.value(item)});')
            return t
        if k == 'cond':
            t = self.tmp('c')
            self.e(f'qa_save_value *{t};')
            self.e(f'if ({self.cond(e[1])}) {{')
            self.depth += 1
            self.push()
            v = self.value(e[2])
            self.e(f'{t} = {v};')
            self.pop()
            self.depth -= 1
            self.e('} else {')
            self.depth += 1
            self.push()
            v = self.value(e[3])
            self.e(f'{t} = {v};')
            self.pop()
            self.depth -= 1
            self.e('}')
            return t
        if k == 'new' and e[1] == ('call', ('id', 'EntityState'), []):
            return 'ev_object()'
        if k == 'call':
            return self.call(e)
        raise ValueError(f'{self.name}: value {e}')

    def lift(self, arrow, what):
        """arrow -> C function; returns its name"""
        names, body = arrow[1], arrow[2]
        if len(names) != 1:
            raise ValueError(f'{self.name}: arrow params {names}')
        p = names[0]
        if body[0] == 'call' and body[1][0] == 'id' and len(body[2]) >= 1 and body[2][0] == ('id', p):
            fname = body[1][1]
            if self.mode == 'read' and fname.startswith('read') and (len(body[2]) == 1 or body[2][1] == ('id', 'identity')):
                return snake(fname)
            if self.mode == 'write' and fname.startswith('write') and len(body[2]) == 1:
                return snake(fname)
        Fn.lift_n = getattr(Fn, 'lift_n', 0) + 1
        name = f'{self.cname}_{what}_{Fn.lift_n}'
        f = Fn(self.name + '/arrow', name, self.mode, p)
        c = f.value(body)
        f.e(f'qa_save_value *result = {c};')
        f.flush_all()
        f.e('return result;')
        sig = (f'static qa_save_value *{name}(const qa_save_reader *{p}, const qa_app_unified_identity_decoder *identity)'
               if self.mode == 'read' else f'static qa_save_value *{name}(const qa_save_value *{p})')
        head = sig + ' {\n' + ('  (void)identity;\n' if self.mode == 'read' else '')
        Fn.lifted.append(head + '\n'.join(f.lines) + '\n}\n')
        return name

    def call(self, e):
        callee, args = e[1], e[2]
        if callee[0] == 'id':
            f = callee[1]
            if f == 'boundedList':
                if self.mode != 'read':
                    raise ValueError('boundedList in write')
                return f'ev_list({self.rd(args[0])}, {self.lift(args[1], "item")}, identity)'
            if f == 'namespaced':
                return f'ev_namespaced({self.rd(args[0])})'
            if f in ('nonNegative', 'positive'):
                sf = self.split_field(args[0])
                base = f'{sf[0]}, {cstr(sf[1])}' if sf else self.rd(args[0])
                return f'ev_{"f" if sf else "r"}_{snake(f)}({base})'
            if f == 'sourceClientInteger':
                return f'source_client_integer({self.rd(args[0])}, {cnum(numval(args[1]))}, {cnum(numval(args[2]))})'
            if f == 'readModule':
                return f'read_module({self.rd(args[0])})'
            if f.startswith('read'):
                sf = self.split_field(args[0])
                if sf:
                    return f'ev_read({sf[0]}, {cstr(sf[1])}, {snake(f)}, identity)'
                return f'{snake(f)}({self.rd(args[0])}, identity)'
            if f.startswith('write'):
                if len(args) != 1:
                    raise ValueError(f'{self.name}: write args {args}')
                return f'{snake(f)}({self.borrow(args[0])})'
            raise ValueError(f'{self.name}: call {f}')
        if callee[0] == 'member':
            recv, meth = callee[1], callee[2]
            if self.mode == 'read' and self.is_rd(recv):
                if meth == 'nullable':
                    return f'ev_nullable({self.rd(recv)}, {self.lift(args[0], "value")}, identity)'
                return self.method(recv, meth, args)
            if meth == 'map':
                return f'ev_map({self.borrow(recv)}, {self.lift_map(args[0])})'
        raise ValueError(f'{self.name}: call {e}')

    def lift_map(self, arrow):
        if arrow[2] == ('id', arrow[1][0]):
            return 'ev_map_identity'
        return self.lift(arrow, 'item')

    def obj(self, e):
        t = self.tmp('o')
        self.e(f'qa_save_value *{t} = ev_object();')
        for p in e[1]:
            if p[0] == 'prop':
                _, key, v = p
                self.e(f'ev_set({t}, {cstr(key)}, {self.value(v)});')
            else:
                self.spread(t, p[1])
        return t

    def obj_props_into(self, t, e):
        if e[0] != 'obj':
            raise ValueError(f'{self.name}: spread body {e}')
        for p in e[1]:
            if p[0] == 'prop':
                self.e(f'ev_set({t}, {cstr(p[1])}, {self.value(p[2])});')
            else:
                self.spread(t, p[1])

    def spread(self, t, s):
        if s[0] == 'cond':
            a, b = s[2], s[3]
            if a == ('obj', []):
                self.e(f'if (!{self.cond(s[1])}) {{')
                body = b
            elif b == ('obj', []):
                self.e(f'if ({self.cond(s[1])}) {{')
                body = a
            else:
                raise ValueError(f'{self.name}: spread cond')
            self.depth += 1
            self.push()
            self.obj_props_into(t, body)
            self.pop()
            self.depth -= 1
            self.e('}')
            return
        if s[0] == 'call':
            x = self.tmp('s')
            self.e(f'qa_save_value *{x} = {self.value(s)};')
            self.e(f'ev_spread({t}, {x});')
            self.e(f'qa_save_value_free({x});')
            return
        self.e(f'ev_spread({t}, {self.borrow(s)});')

    # statements ---------------------------------------------------------
    def stmts(self, body):
        for s in body:
            self.stmt(s)

    def stmt(self, s):
        k = s[0]
        if k == 'expr':
            ex = s[1]
            if ex[0] == 'void':
                return
            if ex[0] == 'assign':
                tgt, v = ex[1], ex[2]
                if tgt[0] != 'member':
                    raise ValueError(f'{self.name}: assign {tgt}')
                self.e(f'ev_set({self.borrow_mut(tgt[1])}, {cstr(tgt[2])}, {self.value(v)});')
                return
            raise ValueError(f'{self.name}: expr stmt {ex}')
        if k == 'return':
            ex = s[1]
            if ex is None:
                raise ValueError('bare return')
            if ex[0] == 'call' and ex[1][0] == 'member' and ex[1][2] == 'fail':
                self.e(f'qa_save_reader_fail({self.rd(ex[1][1])}, "%s", {cstr(ex[2][0][1])});')
                return
            v = self.value(ex)
            self.e(f'qa_save_value *result = {v};' if not self.has_result() else f'result = {v};')
            self.flush_all()
            self.e('return result;')
            return
        if k == 'switch':
            return self.switch(s)
        if k == 'const':
            for name, init in s[1]:
                self.decl(name, init)
            return
        if k == 'if':
            c, a, b = s[1], s[2], s[3]
            if c[0] == 'eq' and c[2] == ('const', 'undefined') and c[1][0] == 'id' and self.kind(c[1][1]) == 'rdlocal':
                return  # items[i] was bounds-checked by the tuple length test just above
            self.e(f'if ({self.cond(c)}) {{')
            self.depth += 1
            self.push()
            self.stmt(a)
            self.pop(emit=not self.returned(a))
            self.depth -= 1
            if b is not None:
                self.e('} else {')
                self.depth += 1
                self.push()
                self.stmt(b)
                self.pop(emit=not self.returned(b))
                self.depth -= 1
            self.e('}')
            return
        if k == 'blockstmt':
            self.stmts(s[1])
            return
        raise ValueError(f'{self.name}: stmt {s}')

    def returned(self, s):
        return s[0] == 'return'

    def has_result(self):
        return False

    def borrow_mut(self, e):
        if e[0] == 'id' and self.kind(e[1]) == 'val':
            return e[1]
        raise ValueError(f'{self.name}: mut {e}')

    def decl(self, name, init):
        if init[0] == 'call' and init[1] == ('id', 'boundedList') and init[2][1][2] == ('id', 'item'):
            self.e(f'size_t {name}_length = ev_bounded_count({self.rd(init[2][0])});')
            self.scopes[-1][name] = 'items'
            self.items_base = init[2][0]
            return
        if init[0] == 'index' and init[1][0] == 'id' and self.kind(init[1][1]) == 'items':
            self.e(f'qa_save_reader {name};')
            self.e(f'ev_item({self.rd(self.items_base)}, {int(init[2][1])}, &{name});')
            self.cleanup[-1].append(f'qa_save_reader_destroy(&{name});')
            self.scopes[-1][name] = 'rdlocal'
            return
        if self.mode == 'read' and self.is_rd(init):
            sf = self.split_field(init)
            self.e(f'qa_save_reader {name};')
            self.e(f'ev_field({sf[0]}, {cstr(sf[1])}, &{name});')
            self.cleanup[-1].append(f'qa_save_reader_destroy(&{name});')
            self.scopes[-1][name] = 'rdlocal'
            return
        self.e(f'qa_save_value *{name} = {self.value(init)};')
        self.cleanup[-1].append(f'qa_save_value_free({name});')
        self.scopes[-1][name] = 'val'

    def switch(self, s):
        d = s[1]
        name = self.tmp('kind')
        if self.mode == 'read':
            if not (d[0] == 'call' and d[1][0] == 'member' and d[1][2] == 'string' and not d[2]):
                raise ValueError(f'{self.name}: switch discriminant {d}')
            sf = self.split_field(d[1][1])
            self.e(f'const char *{name} = ev_f_text({sf[0]}, {cstr(sf[1])});')
        else:
            self.e(f'const char *{name} = ev_text({self.borrow(d)});')
        for labels, body in s[2]:
            tests = ' || '.join(f'ev_is({name}, {cstr(l[1])})' for l in labels)
            for l in labels:
                if l[0] != 'str':
                    raise ValueError(f'{self.name}: case {l}')
            self.e(f'if ({tests}) {{')
            self.depth += 1
            self.push()
            self.stmts(body)
            last = body[-1] if body else None
            if last is not None and last[0] == 'blockstmt':
                last = last[1][-1]
            if last is None or last[0] != 'return':
                raise ValueError(f'{self.name}: case without return')
            self.pop(emit=False)
            self.depth -= 1
            self.e('}')
        if s[3] is not None:
            self.stmts(s[3])
        else:
            if self.mode == 'read':
                raise ValueError(f'{self.name}: read switch no default')
            self.flush_all()
            self.e('return ev_undefined();  /* JS: a switch with no match returns undefined. */')


def main():
    text = open(TS).read()
    # Parser extension: assignment statements in readEntityState.
    orig_stmt = tsparse.P.stmt

    def stmt(self):
        if self.peek()[0] == 'id' and self.peek(1)[1] == '.' and self.peek(3)[1] == '=' and self.peek(2)[0] == 'id':
            base = ('id', self.eat()[1])
            self.eat('.')
            key = self.eat()[1]
            self.eat('=')
            v = self.expr()
            self.try_eat(';')
            return ('expr', ('assign', ('member', base, key, False), v))
        return orig_stmt(self)
    tsparse.P.stmt = stmt
    fns = tsparse.split_functions(text)
    protos, bodies = [], []
    seen = []
    for f in fns:
        name = f['name']
        seen.append(name)
        if name in HAND:
            continue
        if f['body'] is None:
            raise ValueError(f'unparsed function needs a hand port: {name}')
        cname = snake(name)
        if name.startswith('read'):
            mode = 'read'
        elif name.startswith('write'):
            mode = 'write'
        else:
            raise ValueError(f'unknown function {name}')
        param = f['params'][0]
        fn = Fn(name, cname, mode, param)
        fn.stmts(f['body'])
        if mode == 'read':
            sig = f'static qa_save_value *{cname}(const qa_save_reader *{param}, const qa_app_unified_identity_decoder *identity)'
            head = '  (void)identity;\n'
            if len(f['params']) > 2:
                raise ValueError(f'{name}: params {f["params"]}')
        else:
            sig = f'static qa_save_value *{cname}(const qa_save_value *{param})'
            head = ''
            if len(f['params']) != 1:
                raise ValueError(f'{name}: params {f["params"]}')
        protos.append(sig + ';')
        bodies.append(sig + ' {\n' + head + '\n'.join(fn.lines) + '\n}\n')
    missing = HAND - set(seen)
    if missing:
        raise ValueError(f'hand list stale: {missing}')
    with open(OUT, 'w') as out:
        out.write(open(TEMPLATE).read())
        out.write('\n/* One C function per TS reader/writer, same order. */\n')
        out.write('\n'.join(protos) + '\n\n')
        for l in Fn.lifted:
            pass
        # lifted arrows need the protos, and the bodies call them.
        lifted_protos = [l.split(' {\n', 1)[0] + ';' for l in Fn.lifted]
        out.write('\n'.join(lifted_protos) + '\n\n')
        out.write('\n'.join(bodies) + '\n')
        out.write('\n'.join(Fn.lifted) + '\n')
        out.write(open(TAIL).read())
    print(len(protos), 'functions,', len(Fn.lifted), 'lifted arrows')


main()
