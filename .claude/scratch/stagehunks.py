"""Stage the hunks of one file whose changed lines match any regex.

usage: stagehunks.py [--invert] FILE REGEX...
A hunk is taken when any of its +/- lines (or its @@ header's function text)
matches. --invert takes the hunks that match none.
"""
import re, subprocess, sys

args = sys.argv[1:]
invert = False
if args and args[0] == '--invert':
    invert = True
    args = args[1:]
path, pats = args[0], [re.compile(p) for p in args[1:]]
diff = subprocess.run(['git', 'diff', '-U1', '--', path], capture_output=True, text=True, encoding='utf-8').stdout
if not diff:
    sys.exit('no diff for ' + path)
lines = diff.split('\n')
header, hunks, cur = [], [], None
for line in lines:
    if line.startswith('@@'):
        cur = [line]
        hunks.append(cur)
    elif cur is None:
        header.append(line)
    else:
        cur.append(line)
taken = []
for h in hunks:
    text = [h[0]] + [l for l in h[1:] if l.startswith('+') or l.startswith('-')]
    hit = any(p.search(t) for p in pats for t in text)
    if hit != invert:
        taken.append(h)
if not taken:
    sys.exit('no hunks matched in ' + path)
patch = '\n'.join(header) + '\n' + '\n'.join('\n'.join(h) for h in taken)
if not patch.endswith('\n'):
    patch += '\n'
r = subprocess.run(['git', 'apply', '--cached', '--recount', '-'], input=patch, text=True, encoding='utf-8', capture_output=True)
if r.returncode != 0:
    sys.exit('apply failed: ' + r.stderr)
print(f'staged {len(taken)}/{len(hunks)} hunks of {path}')
