"""Exact-text substitutions on a file, line-ending agnostic.

usage: sub.py FILE SPECFILE
SPECFILE: blocks separated by '\n=====\n'; each block is OLD then a line '-----' then NEW.
Every OLD must occur exactly once.
"""
import sys

path, spec = sys.argv[1], sys.argv[2]
raw = open(path, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in raw else '\n'
src = raw.replace('\r\n', '\n')
for block in open(spec, encoding='utf-8').read().replace('\r\n', '\n').split('\n=====\n'):
    if not block.strip():
        continue
    old, sep, new = block.partition('\n-----\n')
    if not sep:
        sys.exit('bad block: ' + block[:80])
    n = src.count(old)
    if n != 1:
        sys.exit(f'{n} matches for: {old[:120]!r}')
    src = src.replace(old, new)
open(path, 'w', encoding='utf-8', newline='').write(src.replace('\n', nl))
print('ok', path)
