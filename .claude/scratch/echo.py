"""Insert a world echo before named rollback records.

usage: echo.py FILE DEFAULTSCOPE METHOD...
Before each `MCPSetRollback(Result, TEXT("<method>"), <Payload>);` line, adds
`FMCPRuntimeWorld::Resolve(Params, TEXT("<scope>")).Echo(<Payload>);` and the
F7 include after HandlerUtils.h.
"""
import re, sys

path, scope, methods = sys.argv[1], sys.argv[2], sys.argv[3:]
raw = open(path, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in raw else '\n'
s = raw.replace('\r\n', '\n')
total = 0
for m in methods:
    pat = re.compile(r'(\t+)(MCPSetRollback\((\w+), TEXT\("' + re.escape(m) + r'"\), (\w+)\);)')
    s, n = pat.subn(lambda x: f'{x.group(1)}FMCPRuntimeWorld::Resolve(Params, TEXT("{scope}")).Echo({x.group(4)});\n{x.group(1)}{x.group(2)}', s)
    total += n
    print(m, n)
if '#include "Families/MCPRuntime.h"' not in s:
    s = s.replace('#include "HandlerUtils.h"\n', '#include "HandlerUtils.h"\n#include "Families/MCPRuntime.h"\n', 1)
open(path, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('total', total)
