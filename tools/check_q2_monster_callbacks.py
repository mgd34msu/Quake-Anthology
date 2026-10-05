#!/usr/bin/env python3
"""Check authored move callbacks against their actual C dispatch branches.

This interprets only callback selection and handled/return flow. Game operations
are unknown values; no game state or callbacks execute. Unrecognized selection
syntax is an error, so a new dispatch shape must be supported here explicitly.
"""

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys


UNKNOWN = object()


class CallbackValue(int):
    """Keep unknown callback-dependent expressions out of accepted branches."""


TOKEN = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|'
                   r'[A-Za-z_]\w*|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[fFuUlL]*|'
                   r'->|==|!=|<=|>=|&&|\|\||<<|>>|\+\+|--|[^\s]', re.S)


def tokens(text):
    return [token for token in TOKEN.findall(text) if not token.startswith(('//', '/*'))]


def close(ts, start):
    opening = ts[start]
    ending = {'(': ')', '[': ']', '{': '}'}[opening]
    depth = 1
    for i in range(start + 1, len(ts)):
        depth += (ts[i] == opening) - (ts[i] == ending)
        if not depth:
            return i
    raise ValueError('unclosed C delimiter')


def split(ts, delimiter=','):
    result, start, i = [], 0, 0
    while i < len(ts):
        if ts[i] in ('(', '[', '{'):
            i = close(ts, i)
        elif ts[i] == delimiter:
            result.append(ts[start:i])
            start = i + 1
        i += 1
    if start < len(ts):
        result.append(ts[start:])
    return result


def truth(value):
    return {False, True} if value is UNKNOWN else {bool(value)}


@dataclass(frozen=True)
class Pointer:
    table: str
    index: int


class Expression:
    precedence = {'||': 1, '&&': 2, '|': 3, '^': 4, '&': 5,
                  '==': 6, '!=': 6, '<': 7, '>': 7, '<=': 7, '>=': 7,
                  '<<': 8, '>>': 8, '+': 9, '-': 9, '*': 10, '/': 10, '%': 10}

    def __init__(self, ts, values, constants, tables):
        self.ts, self.at = ts, 0
        self.values, self.constants, self.tables = values, constants, tables

    def atom(self):
        token = self.ts[self.at]
        self.at += 1
        if token in ('!', '-', '+', '~', '&', '*'):
            handled_value = token == '*' and self.ts[self.at:self.at + 1] == ['handled']
            value = self.atom()
            if token == '!':
                value = not value if value is not UNKNOWN else UNKNOWN
            elif token == '-' and value is not UNKNOWN:
                value = -value
            elif token == '~' and value is not UNKNOWN:
                value = ~value
            elif token in ('&', '*') and not handled_value:
                value = UNKNOWN
        elif token == '(':
            end = close(self.ts, self.at - 1)
            inside = self.ts[self.at:end]
            if inside and all(t in ('unsigned', 'signed', 'int', 'float', 'double',
                                     'bool', 'size_t', 'uint64_t', 'uint32_t', '*') for t in inside):
                self.at = end + 1
                value = self.atom()
            else:
                value = self.parse()
                if self.ts[self.at] != ')':
                    raise ValueError('unsupported callback grouping')
                self.at += 1
        elif token.startswith(('"', "'")):
            value = token
        elif re.match(r'^(?:\d|\.\d)', token):
            value = float(re.sub(r'[fFuUlL]+$', '', token))
            if value.is_integer():
                value = int(value)
        else:
            value = self.values.get(token, self.constants.get(token, UNKNOWN))
            if token in self.tables:
                value = Pointer(token, 0)
        while self.at < len(self.ts) and self.ts[self.at] in ('(', '[', '.', '->'):
            operator = self.ts[self.at]
            self.at += 1
            if operator == '(':
                end = close(self.ts, self.at - 1)
                arguments = self.ts[self.at:end]
                if any(isinstance(self.values.get(t), CallbackValue) and
                       (i == 0 or arguments[i - 1] != '[')
                       for i, t in enumerate(arguments)):
                    raise ValueError(f'unsupported callback selector call {token}')
                self.at, value = end + 1, UNKNOWN
            elif operator == '[':
                end = close(self.ts, self.at - 1)
                index = Expression(self.ts[self.at:end], self.values,
                                   self.constants, self.tables).read()
                self.at = end + 1
                if isinstance(value, Pointer) and index is UNKNOWN:
                    raise ValueError(f'unknown callback table index {value.table}')
                value = Pointer(value.table, index) if isinstance(value, Pointer) else UNKNOWN
            else:
                member = self.ts[self.at]
                self.at += 1
                if isinstance(value, Pointer):
                    table = self.tables[value.table]
                    if member not in table['fields']:
                        raise ValueError(f'unknown callback table member {value.table}.{member}')
                    value = table['rows'].get(value.index, {}).get(member, 0)
                else:
                    value = UNKNOWN
        return value

    def parse(self, minimum=0):
        value = self.atom()
        while self.at < len(self.ts):
            operator = self.ts[self.at]
            precedence = self.precedence.get(operator, -1)
            if precedence < minimum:
                break
            self.at += 1
            right = self.parse(precedence + 1)
            if operator in ('&&', '||'):
                possibilities = {a and b if operator == '&&' else a or b
                                 for a in truth(value) for b in truth(right)}
                value = possibilities.pop() if len(possibilities) == 1 else UNKNOWN
            elif isinstance(value, Pointer) and operator == '+' and right is not UNKNOWN:
                value = Pointer(value.table, value.index + right)
            elif value is UNKNOWN or right is UNKNOWN:
                if isinstance(value, CallbackValue) or isinstance(right, CallbackValue):
                    raise ValueError(f'unknown callback selector operand {operator}')
                value = UNKNOWN
            else:
                operations = {'==': lambda: value == right, '!=': lambda: value != right,
                              '<': lambda: value < right, '>': lambda: value > right,
                              '<=': lambda: value <= right, '>=': lambda: value >= right,
                              '&': lambda: value & right, '|': lambda: value | right,
                              '^': lambda: value ^ right, '<<': lambda: value << right,
                              '>>': lambda: value >> right, '+': lambda: value + right,
                              '-': lambda: value - right, '*': lambda: value * right,
                              '/': lambda: value / right, '%': lambda: value % right}
                selected = isinstance(value, CallbackValue) or isinstance(right, CallbackValue)
                value = operations[operator]()
                if selected and operator not in ('==', '!=', '<', '>', '<=', '>='):
                    value = CallbackValue(value)
        if minimum == 0 and self.at < len(self.ts) and self.ts[self.at] == '?':
            self.at += 1
            yes = self.parse()
            if self.ts[self.at] != ':':
                raise ValueError('unsupported callback conditional')
            self.at += 1
            no = self.parse()
            value = yes if value is not UNKNOWN and value else no if value is not UNKNOWN else yes if yes == no else UNKNOWN
        return value

    def read(self):
        if not self.ts:
            return UNKNOWN
        value = self.parse()
        if self.at != len(self.ts):
            raise ValueError(f'unsupported callback expression: {" ".join(self.ts)}')
        return value


def statement(ts, at):
    first = ts[at]
    if first == '{':
        end, body = close(ts, at), []
        at += 1
        while at < end:
            child, at = statement(ts, at)
            body.append(child)
        return ('block', body), end + 1
    if first == 'if':
        end = close(ts, at + 1)
        yes, after = statement(ts, end + 1)
        no = ('block', [])
        if after < len(ts) and ts[after] == 'else':
            no, after = statement(ts, after + 1)
        return ('if', ts[at + 2:end], yes, no), after
    if first in ('for', 'while'):
        end = close(ts, at + 1)
        body, after = statement(ts, end + 1)
        return ('loop', body), after
    if first in ('switch', 'do', 'goto'):
        raise ValueError(f'unsupported callback control flow: {first}')
    end = at
    while end < len(ts) and ts[end] != ';':
        if ts[end] in ('(', '[', '{'):
            end = close(ts, end)
        end += 1
    if end == len(ts):
        raise ValueError('unterminated callback statement')
    return ('return' if first == 'return' else 'simple', ts[at + (first == 'return'):end]), end + 1


def execute(node, values, constants, tables):
    kind = node[0]
    if kind == 'block':
        live, returns = [values], []
        for child in node[1]:
            next_live = []
            for current in live:
                continuing, finished = execute(child, current, constants, tables)
                next_live.extend(continuing)
                returns.extend(finished)
            live = next_live
        return live, returns
    if kind == 'if':
        choices = truth(Expression(node[1], values, constants, tables).read())
        live, returns = [], []
        for choice in choices:
            branch = values.copy()
            if node[1] in (['handled'], ['*', 'handled']):
                branch['handled'] = choice
            elif node[1] in (['!', 'handled'], ['!', '*', 'handled']):
                branch['handled'] = not choice
            continuing, finished = execute(node[2 if choice else 3], branch, constants, tables)
            live.extend(continuing)
            returns.extend(finished)
        return live, returns
    if kind == 'loop':
        continuing, finished = execute(node[1], values.copy(), constants, tables)
        return [values] + continuing, finished
    if kind == 'return':
        # Calls here execute game actions, not callback selection. Their runtime
        # success is outside this structural coverage check.
        result = constants[node[1][0]] if node[1] in (['true'], ['false']) else UNKNOWN
        return [], [(result, values.get('handled', False))]
    ts = node[1]
    if '=' in ts:
        declaration = ts[:1] in (['bool'], ['q2m_callback_id']) or ts[:2] == ['const', 'bool']
        for part in split(ts):
            if '=' not in part:
                continue
            index = part.index('=')
            before = part[:index]
            if before == ['*', 'handled']:
                name = 'handled'
            elif before and (declaration or before[-1] in values or
                             any(t in tables for t in part[index + 1:])):
                name = before[-1]
            else:
                continue
            values = values.copy()
            values[name] = Expression(part[index + 1:], values, constants, tables).read()
    elif 'handled' in ts:
        raise ValueError(f'unsupported handled mutation: {" ".join(ts)}')
    return [values], []


def check(root):
    directory = root / 'src/gameplay/q2/monsters'
    sources = {p: tokens(p.read_text()) for p in directory.glob('*.c')}
    constants = {'true': True, 'false': False, 'NULL': 0}
    for path in (directory / 'identities.h', directory / 'internal.h'):
        ts = tokens(path.read_text())
        for i, token in enumerate(ts):
            if token != 'enum' or '{' not in ts[i:i + 4]:
                continue
            start = ts.index('{', i)
            value = 0
            for row in split(ts[start + 1:close(ts, start)]):
                if len(row) > 1:
                    value = Expression(row[2:], {}, constants, {}).read()
                constants[row[0]] = value
                value += 1
    functions, tables_by_path = {}, {}
    for path, ts in sources.items():
        structs, tables = {}, {}
        for i, token in enumerate(ts):
            if token == 'struct' and i and ts[i - 1] == 'typedef':
                start = i + 2
                if ts[start] != '{':
                    continue
                end = close(ts, start)
                fields = []
                for declaration in split(ts[start + 1:end], ';'):
                    fields.extend(next(t for t in reversed(field) if re.match(r'^\w+$', t))
                                  for field in split(declaration))
                structs[ts[end + 1]] = fields
            if token == '(' and i and re.match(r'^\w+$', ts[i - 1]):
                end = close(ts, i)
                if end + 1 < len(ts) and ts[end + 1] == '{' and 'q2m_callback_id' in ts[i + 1:end]:
                    body_end = close(ts, end + 1)
                    parameters = ts[i + 1:end]
                    name = parameters[parameters.index('q2m_callback_id') + 1]
                    function = ts[i - 1]
                    if function in functions:
                        raise ValueError(f'duplicate callback function {function}')
                    functions[function] = (path, name, ts[end + 1:body_end + 1])
            if token != '[' or ts[i + 1:i + 5] != ['Q2M_CALLBACK_COUNT', ']', '=', '{']:
                continue
            name, start = ts[i - 1], i + 4
            end = close(ts, start)
            type_name = ts[i - 2]
            if type_name == '}':
                struct_start = i - 2
                while ts[struct_start] != '{':
                    struct_start -= 1
                fields = []
                for declaration in split(ts[struct_start + 1:i - 2], ';'):
                    fields.extend(field[-1] for field in split(declaration))
            else:
                fields = structs.get(type_name, [])
            if name == 'q2m_callbacks':
                fields = ['invoke', 'id', 'name', 'flags']
            if not fields:
                raise ValueError(f'unknown callback table type {path.name}:{name}')
            rows = {}
            for row in split(ts[start + 1:end]):
                if len(row) < 6 or row[:1] != ['['] or row[2:5] != [']', '=', '{'] or row[-1] != '}':
                    raise ValueError(f'unsupported callback table row {path.name}:{name}')
                values = split(row[5:-1])
                if len(values) != len(fields):
                    raise ValueError(f'callback field count {path.name}:{name}')
                rows[constants[row[1]]] = dict(zip(fields, (Expression(v, {}, constants, {}).read() for v in values)))
                if name == 'q2m_callbacks':
                    rows[constants[row[1]]]['invoke'] = values[0][0]
            tables[name] = {'fields': fields, 'rows': rows}
        tables_by_path[path] = tables
    registry = tables_by_path[directory / 'actions.c']['q2m_callbacks']
    enum_ids = {name: value for name, value in constants.items()
                if name.startswith('Q2M_CALLBACK_') and 0 < value < constants['Q2M_CALLBACK_COUNT']}
    # Only the identity enum contains callback values; flags may share integers.
    identity_tokens = tokens((directory / 'identities.h').read_text())
    enum_ids = {name: value for name, value in enum_ids.items() if name in identity_tokens}
    if set(registry['rows']) != set(enum_ids.values()):
        raise ValueError('callback registry does not cover the identity enum')
    referenced = set()
    for path in (directory / 'moves.c', directory / 'stalker_moves.c'):
        ts = sources[path]
        referenced.update(ts[i + 2] for i, t in enumerate(ts[:-3])
                          if t == 'q2m_callbacks' and ts[i + 1] == '[')
    parsed = {}

    def covers(function, callback):
        if function == 'callback_unsupported':
            return False
        if function not in functions:
            raise ValueError(f'missing callback function {function}')
        path, argument, body = functions[function]
        text = ' '.join(body[1:-1])
        if '& handled' in text:
            pattern = (r'if \( ! (\w+) \( context , callback , & handled , error \) \) '
                       r'return false ; if \( handled \) return true ;')
            remaining = re.sub(r'^bool handled = false ;\s*', '', text)
            while match := re.match(pattern, remaining):
                if accepts(match[1], callback):
                    return True
                remaining = remaining[match.end():].strip()
            terminal = re.fullmatch(r'return (\w+) \( context , callback , error \) ;', remaining)
            if not terminal:
                raise ValueError(f'unsupported callback chain {function}')
            return covers(terminal[1], callback)
        if 'callback_unsupported' in body:
            raise ValueError(f'unrecognized unsupported callback path {function}')
        return True

    def accepts(function, callback):
        path, argument, body = functions[function]
        if function not in parsed:
            parsed[function], after = statement(body, 0)
            if after != len(body):
                raise ValueError(f'unsupported callback body {function}')
        tables = dict(tables_by_path[path], q2m_callbacks=registry)
        _, returns = execute(parsed[function], {argument: CallbackValue(callback), 'handled': False}, constants, tables)
        return any(True in truth(result) and True in truth(handled) for result, handled in returns)

    if referenced - enum_ids.keys():
        raise ValueError(f'unknown move callbacks {sorted(referenced - enum_ids.keys())}')
    for name in sorted(enum_ids):
        row = registry['rows'][enum_ids[name]]
        if row['id'] != enum_ids[name] or not covers(row['invoke'], enum_ids[name]):
            raise ValueError(f'{name}: callback is not accepted by {row["invoke"]}')
    print(f'Q2 monster callbacks: {len(referenced)} move IDs / {len(enum_ids)} registered IDs checked')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path, help='source tree root')
    arguments = parser.parse_args()
    try:
        check(arguments.root.resolve())
    except (ValueError, KeyError, IndexError, TypeError, ZeroDivisionError) as error:
        print(f'Q2 monster callback coverage: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
