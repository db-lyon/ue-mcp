"""Add an #include after the first line matching an anchor include, if absent.

usage: addinc.py INCLUDE ANCHOR FILE...
"""
import sys

inc, anchor, files = sys.argv[1], sys.argv[2], sys.argv[3:]
line = f'#include "{inc}"'
for path in files:
    raw = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in raw else '\n'
    lines = raw.split(nl)
    if line in lines:
        print('has', path)
        continue
    anchor_line = f'#include "{anchor}"'
    try:
        i = lines.index(anchor_line)
    except ValueError:
        sys.exit(f'no anchor in {path}')
    lines.insert(i + 1, line)
    open(path, 'w', encoding='utf-8', newline='').write(nl.join(lines))
    print('ok', path)
