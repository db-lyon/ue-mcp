"""Replace whole C++ function definitions (signature line to its closing brace at column 0).

usage: replfn.py FILE SPECFILE
SPECFILE holds blocks separated by lines '=====' ; each block's first line is the
exact signature prefix to find, the rest is the replacement text. An empty
replacement deletes the function. A comment block directly above the old
function goes too when the replacement is empty or brings its own comment.
"""
import sys, re

path, spec = sys.argv[1], sys.argv[2]
src = open(path, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in src else '\n'
src = src.replace('\r\n', '\n')
blocks = open(spec, encoding='utf-8').read().replace('\r\n', '\n').split('\n=====\n')
for block in blocks:
    if not block.strip():
        continue
    head, _, body = block.partition('\n')
    head = head.strip()
    i = src.find(head)
    if i < 0:
        sys.exit(f'not found: {head}')
    if src.find(head, i + 1) >= 0:
        sys.exit(f'ambiguous: {head}')
    start = src.rfind('\n', 0, i) + 1
    m = re.compile(r'\n}\n').search(src, i)
    if not m:
        sys.exit(f'no end: {head}')
    end = m.end()
    empty = body.strip() == ''
    if empty or body.lstrip().startswith('//'):
        lines_before = src[:start].split('\n')
        j = len(lines_before) - 2
        while j >= 0 and (lines_before[j].startswith('//') or lines_before[j].startswith(' *') or lines_before[j].startswith('/*')):
            j -= 1
        start = len('\n'.join(lines_before[:j + 1])) + (1 if j >= 0 else 0)
    if empty:
        if src[end:end + 1] == '\n':
            end += 1
        src = src[:start] + src[end:]
    else:
        src = src[:start] + body.rstrip('\n') + '\n' + src[end:]
open(path, 'w', encoding='utf-8', newline='').write(src.replace('\n', nl))
print('ok', path)
