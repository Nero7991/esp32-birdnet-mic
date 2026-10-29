#!/usr/bin/env python3
"""Turn the Arduino sketch (.ino) into a C++ file for the ESP-IDF build.

Does what the Arduino builder does for a single .ino: adds prototypes for top-level
functions that are not already declared, right before the first function definition,
with #line directives so compiler errors point at the .ino. Default arguments move
from the definition to the prototype, since C++ allows them only once.
"""
import re
import sys

KEYWORDS = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'catch', 'main'}
SKIP_PREFIXES = ('namespace', 'struct', 'class', 'enum', 'union', 'template', 'extern', 'typedef', 'using')


def mask(source, strings=True):
    """Blank out comments (and string/char literals when strings=True), keeping offsets."""
    out = list(source)
    i, n = 0, len(source)

    def blank(a, b):
        for k in range(a, b):
            if out[k] != '\n':
                out[k] = ' '

    while i < n:
        c = source[i]
        if source.startswith('//', i):
            j = source.find('\n', i)
            j = n if j < 0 else j
            blank(i, j)
            i = j
        elif source.startswith('/*', i):
            j = source.find('*/', i + 2)
            j = n if j < 0 else j + 2
            blank(i, j)
            i = j
        elif c == 'R' and source.startswith('R"', i) and (i == 0 or not (source[i - 1].isalnum() or source[i - 1] == '_')):
            open_paren = source.find('(', i)
            delim = source[i + 2:open_paren]
            j = source.find(')' + delim + '"', open_paren)
            j = n if j < 0 else j + len(delim) + 2
            if strings:
                blank(i, j)
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and source[j] != c:
                j += 2 if source[j] == '\\' else 1
            j += 1
            if strings:
                blank(i, j)
            i = j
        else:
            i += 1
    return ''.join(out)


def split_params(text, masked):
    """Split a parameter list at top-level commas. Returns (start, end) spans."""
    spans, depth, start = [], 0, 0
    for k, ch in enumerate(masked):
        if ch in '(<[{':
            depth += 1
        elif ch in ')>]}':
            depth -= 1
        elif ch == ',' and depth == 0:
            spans.append((start, k))
            start = k + 1
    spans.append((start, len(text)))
    return spans


def find_functions(source):
    """Yield dicts for top-level function definitions, plus the set of declared names."""
    masked = mask(source)
    functions, declared = [], set()
    depth, boundary = 0, 0
    i = 0
    while i < len(masked):
        ch = masked[i]
        if ch == '#' and depth == 0 and masked[masked.rfind('\n', 0, i) + 1:i].strip() == '':
            # Preprocessor line (with continuations): acts as a statement boundary.
            j = i
            while True:
                e = masked.find('\n', j)
                if e < 0:
                    e = len(masked)
                    break
                if masked[e - 1] == '\\':
                    j = e + 1
                    continue
                break
            i = e
            boundary = e
            continue
        if ch == '{':
            if depth == 0:
                fn = parse_header(source, masked, boundary, i)
                if fn:
                    functions.append(fn)
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                boundary = i + 1
        elif ch == ';' and depth == 0:
            stmt = masked[boundary:i]
            m = re.search(r'([A-Za-z_]\w*)\s*\([^()]*(\([^()]*\)[^()]*)*\)\s*(const)?\s*$', stmt)
            if m and '=' not in stmt[:m.start()]:
                declared.add(m.group(1))
            boundary = i + 1
        i += 1
    return functions, declared


def parse_header(source, masked, start, brace):
    header = masked[start:brace]
    stripped = header.strip()
    if not stripped or stripped.startswith(SKIP_PREFIXES):
        return None
    if not stripped.endswith(')'):
        return None
    close = start + header.rstrip().rfind(')')
    depth, k = 0, close
    while k >= start:
        if masked[k] == ')':
            depth += 1
        elif masked[k] == '(':
            depth -= 1
            if depth == 0:
                break
        k -= 1
    open_paren = k
    before = masked[start:open_paren]
    m = re.search(r'([A-Za-z_]\w*)\s*$', before)
    if not m:
        return None
    name = m.group(1)
    prefix = before[:m.start()].strip()
    if name in KEYWORDS or '=' in before or '::' in before or not prefix:
        return None
    if prefix.endswith((':', ',')):
        return None
    head_start = start + (len(header) - len(header.lstrip()))
    return {
        'name': name,
        'head_start': head_start,
        'open': open_paren,
        'close': close,
    }


def params_with_defaults(source, comments_masked, fn):
    """Return (prototype params text, list of (abs_eq_index, abs_end_index) default spans)."""
    text = source[fn['open'] + 1:fn['close']]
    masked = mask(text)
    nocomment = comments_masked[fn['open'] + 1:fn['close']]
    parts, defaults = [], []
    for a, b in split_params(text, masked):
        seg_mask = masked[a:b]
        eq = seg_mask.find('=')
        piece = ' '.join(nocomment[a:b].split())
        if eq >= 0:
            defaults.append((fn['open'] + 1 + a + eq, fn['open'] + 1 + b))
        if piece:
            parts.append(piece)
    return ', '.join(parts), defaults


def prototypes(source):
    return [p for p, _ in _plan(source)[0]]


def _plan(source):
    functions, declared = find_functions(source)
    comments_masked = mask(source, strings=False)
    protos, edits, seen = [], [], set()
    for fn in functions:
        params, defaults = params_with_defaults(source, comments_masked, fn)
        edits.extend(defaults)
        if fn['name'] in declared or fn['name'] in seen:
            continue
        seen.add(fn['name'])
        head = ' '.join(comments_masked[fn['head_start']:fn['open']].split())
        protos.append((head + '(' + params + ');', fn))
    first = functions[0]['head_start'] if functions else None
    return protos, edits, first


def convert(source, path, header=''):
    protos, edits, first = _plan(source)
    out = source
    for eq, end in sorted(edits, reverse=True):
        out = out[:eq] + '/*' + out[eq:end].replace('*/', '* /') + '*/' + out[end:]
    if first is None:
        return header + '\n#line 1 "' + path + '"\n' + out
    # first is an offset in the original source; edits all come after it.
    line_start = source.rfind('\n', 0, first) + 1
    line_no = source.count('\n', 0, line_start) + 1
    proto_block = '\n'.join(p for p, _ in protos)
    return (header + '\n#line 1 "' + path + '"\n' + out[:line_start] + proto_block +
            '\n#line ' + str(line_no) + ' "' + path + '"\n' + out[line_start:])


def main(argv):
    src, dst = argv[1], argv[2]
    with open(src) as f:
        source = f.read()
    text = convert(source, src, '#include <Arduino.h>')
    with open(dst, 'w') as f:
        f.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
