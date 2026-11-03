"""Small TS parser for the generated unified-event-codec.ts subset.
Every construct it does not know raises, so nothing is skipped silently."""
import re

TOK = re.compile(r"""
    (?P<ws>\s+|//[^\n]*)
  | (?P<str>"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'|`(?:[^`\\]|\\.)*`)
  | (?P<regex>(?<=[(!])/\^(?:[^/\\\n]|\\.)*/)
  | (?P<num>0x[0-9a-fA-F]+|\d+(?:\.\d+)?)
  | (?P<id>[A-Za-z_$][\w$]*)
  | (?P<op>\.\.\.|===|!==|=>|&&|\|\||\?\.|[{}()\[\]<>,;:?.=!|&+\-*/])
""", re.X)


def tokenize(text):
    out = []
    pos = 0
    while pos < len(text):
        m = TOK.match(text, pos)
        if not m:
            raise SyntaxError(f"bad token at {text[pos:pos+60]!r}")
        pos = m.end()
        kind = m.lastgroup
        if kind == 'ws':
            continue
        out.append((kind, m.group()))
    out.append(('eof', ''))
    return out


class P:
    def __init__(self, toks):
        self.t = toks
        self.i = 0

    def peek(self, k=0):
        return self.t[self.i + k]

    def at(self, v):
        return self.t[self.i][1] == v and self.t[self.i][0] != 'str'

    def eat(self, v=None):
        tok = self.t[self.i]
        if v is not None and (tok[1] != v or tok[0] == 'str'):
            ctx = ' '.join(x[1] for x in self.t[max(0, self.i - 15):self.i + 15])
            raise SyntaxError(f"expected {v!r} got {tok!r} near: {ctx}")
        self.i += 1
        return tok

    def try_eat(self, v):
        if self.at(v):
            self.i += 1
            return True
        return False

    # expressions -----------------------------------------------------
    def expr(self):
        return self.ternary()

    def ternary(self):
        c = self.orexpr()
        if self.try_eat('?'):
            a = self.expr()
            self.eat(':')
            b = self.expr()
            return ('cond', c, a, b)
        return c

    def orexpr(self):
        l = self.andexpr()
        while self.try_eat('||'):
            l = ('or', l, self.andexpr())
        return l

    def andexpr(self):
        l = self.eq()
        while self.try_eat('&&'):
            l = ('and', l, self.eq())
        return l

    def eq(self):
        l = self.unary()
        while True:
            if self.try_eat('==='):
                l = ('eq', l, self.unary())
            elif self.try_eat('!=='):
                l = ('ne', l, self.unary())
            elif self.at('>') or self.at('<'):
                op = self.eat()[1]
                l = (op, l, self.unary())
            else:
                return l

    def unary(self):
        if self.at('typeof'):
            self.eat()
            return ('typeof', self.unary())
        if self.at('!'):
            self.eat()
            return ('not', self.unary())
        if self.at('-'):
            self.eat()
            return ('neg', self.unary())
        if self.at('void'):
            self.eat()
            return ('void', self.unary())
        if self.at('new'):
            self.eat()
            e = self.postfix(self.primary())
            return ('new', e)
        return self.postfix(self.primary())

    def skip_generic(self):
        """skip a <...> type argument list just before '('"""
        depth = 0
        j = self.i
        while True:
            v = self.t[j][1]
            if v == '<' and self.t[j][0] != 'str':
                depth += 1
            elif v == '>' and self.t[j][0] != 'str':
                depth -= 1
                if depth == 0:
                    break
            elif v == ';' or self.t[j][0] == 'eof':
                raise SyntaxError('bad generic')
            j += 1
        self.i = j + 1

    def postfix(self, e):
        while True:
            if self.at('.') or self.at('?.'):
                opt = self.eat()[1] == '?.'
                name = self.eat()[1]
                e = ('member', e, name, opt)
            elif self.at('['):
                self.eat()
                idx = self.expr()
                self.eat(']')
                e = ('index', e, idx)
            elif self.at('<') and e[0] == 'member' and e[2] in ('choice', 'literal', 'list', 'map'):
                self.skip_generic()
            elif self.at('('):
                self.eat()
                args = []
                while not self.at(')'):
                    if self.try_eat('...'):
                        args.append(('spread', self.expr()))
                    else:
                        args.append(self.arrow_or_expr())
                    if not self.try_eat(','):
                        break
                self.eat(')')
                e = ('call', e, args)
            elif self.at('as'):
                self.eat()
                self.skip_type()
            else:
                return e

    def skip_type(self):
        depth = 0
        while True:
            v = self.peek()[1]
            if v in '<([{' and self.peek()[0] != 'str':
                depth += 1
            elif v in '>)]}' and self.peek()[0] != 'str':
                if depth == 0:
                    return
                depth -= 1
            elif v in (',', ';', '?', ':') and depth == 0:
                return
            self.i += 1

    def arrow_or_expr(self):
        if self.peek()[0] == 'id' and self.peek(1)[1] == '=>':
            name = self.eat()[1]
            self.eat('=>')
            return ('arrow', [name], self.arrow_body())
        if self.at('('):
            j = self.i + 1
            names = []
            while self.t[j][0] == 'id':
                names.append(self.t[j][1])
                j += 1
                if self.t[j][1] == ',':
                    j += 1
            if self.t[j][1] == ')' and self.t[j + 1][1] == '=>':
                self.i = j + 2
                return ('arrow', names, self.arrow_body())
        return self.expr()

    def arrow_body(self):
        if self.at('{'):
            return ('block', self.block())
        return self.arrow_or_expr()

    def primary(self):
        kind, v = self.peek()
        if kind == 'str':
            self.eat()
            if v[0] == '`':
                return ('template', v[1:-1])
            return ('str', re.sub(r'\\(.)', r'\1', v[1:-1]))
        if kind == 'num':
            self.eat()
            return ('num', float(int(v, 16)) if v.startswith('0x') else float(v))
        if kind == 'regex':
            self.eat()
            return ('regex', v)
        if v == '(':
            self.eat()
            e = self.arrow_or_expr()
            self.eat(')')
            return e
        if v == '{':
            return self.objlit()
        if v == '[':
            self.eat()
            items = []
            while not self.at(']'):
                if self.try_eat('...'):
                    items.append(('spread', self.expr()))
                else:
                    items.append(self.arrow_or_expr())
                if not self.try_eat(','):
                    break
            self.eat(']')
            return ('array', items)
        if kind == 'id':
            self.eat()
            if v in ('null', 'undefined', 'true', 'false'):
                return ('const', v)
            return ('id', v)
        raise SyntaxError(f"primary: {v!r} near {' '.join(x[1] for x in self.t[self.i-10:self.i+10])}")

    def objlit(self):
        self.eat('{')
        props = []
        while not self.at('}'):
            if self.try_eat('...'):
                props.append(('spread', self.expr()))
            else:
                tok = self.eat()
                key = tok[1]
                if tok[0] == 'str':
                    key = key[1:-1]
                if self.try_eat(':'):
                    props.append(('prop', key, self.arrow_or_expr()))
                else:
                    props.append(('prop', key, ('id', key)))
            if not self.try_eat(','):
                break
        self.eat('}')
        return ('obj', props)

    # statements ------------------------------------------------------
    def block(self):
        self.eat('{')
        out = []
        while not self.at('}'):
            out.append(self.stmt())
        self.eat('}')
        return out

    def stmt(self):
        if self.at('return'):
            self.eat()
            if self.try_eat(';'):
                return ('return', None)
            e = self.arrow_or_expr()
            self.try_eat(';')
            return ('return', e)
        if self.at('switch'):
            self.eat()
            self.eat('(')
            d = self.expr()
            self.eat(')')
            self.eat('{')
            cases = []
            default = None
            while not self.at('}'):
                labels = []
                is_default = False
                while self.at('case') or self.at('default'):
                    if self.try_eat('default'):
                        is_default = True
                    else:
                        self.eat('case')
                        labels.append(self.expr())
                    self.eat(':')
                body = []
                while not (self.at('case') or self.at('default') or self.at('}')):
                    body.append(self.stmt())
                if is_default:
                    if labels:
                        raise SyntaxError('mixed default')
                    default = body
                else:
                    cases.append((labels, body))
            self.eat('}')
            return ('switch', d, cases, default)
        if self.at('const') or self.at('let'):
            self.eat()
            decls = []
            while True:
                name = self.eat()[1]
                if self.at(':'):
                    self.eat(':')
                    self.skip_type_until_eq()
                self.eat('=')
                decls.append((name, self.arrow_or_expr()))
                if not self.try_eat(','):
                    break
            self.try_eat(';')
            return ('const', decls)
        if self.at('if'):
            self.eat()
            self.eat('(')
            c = self.expr()
            self.eat(')')
            a = self.stmt()
            b = None
            if self.try_eat('else'):
                b = self.stmt()
            return ('if', c, a, b)
        if self.at('{'):
            return ('blockstmt', self.block())
        e = self.expr()
        self.try_eat(';')
        return ('expr', e)

    def skip_type_until_eq(self):
        depth = 0
        while True:
            v = self.peek()[1]
            if v in ('<', '(', '[', '{'):
                depth += 1
            elif v in ('>', ')', ']', '}'):
                depth -= 1
            elif v == '=' and depth == 0:
                return
            self.i += 1


def split_functions(text):
    """Return ordered list of (name, params, body_ast, exported, src)."""
    fns = []
    for m in re.finditer(r'^(export )?function ([A-Za-z_]\w*)\(', text, re.M):
        # params: scan to matching ')'
        i = m.end()
        depth = 1
        while depth:
            c = text[i]
            if c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
            i += 1
        params = text[m.end():i - 1]
        # find body '{': first '{' at type depth 0 after ')', skipping a return type
        j = i
        while text[j] != ':' and text[j] != '{':
            j += 1
        if text[j] == ':':
            # return type: may hold braces (inline types); parse until ' {' that begins body
            j += 1
            d = 0
            while True:
                c = text[j]
                if c in '<([':
                    d += 1
                elif c in '>)]':
                    if not (c == '>' and text[j - 1] == '='):
                        d -= 1
                elif c == '{' and d == 0:
                    # type literal only if directly after ':' or '|' ; body otherwise
                    prev = text[:j].rstrip()[-1]
                    if prev not in ':|&':
                        break
                    d2 = 1
                    j += 1
                    while d2:
                        if text[j] == '{':
                            d2 += 1
                        elif text[j] == '}':
                            d2 -= 1
                        j += 1
                    continue
                j += 1
        # match body braces with the string-aware tokenizer
        k = j
        depth = 0
        pos = j
        while True:
            t = TOK.match(text, pos)
            if not t:
                raise SyntaxError(f'{m.group(2)}: bad token {text[pos:pos+40]!r}')
            pos = t.end()
            if t.lastgroup == 'op':
                if t.group() == '{':
                    depth += 1
                elif t.group() == '}':
                    depth -= 1
                    if depth == 0:
                        break
        body_text = text[j:pos]
        p = P(tokenize(body_text))
        try:
            body = p.block()
        except SyntaxError as err:
            body = None
            print('UNPARSED', m.group(2), str(err)[:120])
        names = []
        for part in re.split(r',(?![^<(]*[>)])', params):
            part = part.strip()
            if part:
                names.append(part.split(':')[0].strip())
        fns.append(dict(name=m.group(2), params=names, body=body, exported=bool(m.group(1)), src=text[m.start():pos]))
    return fns
