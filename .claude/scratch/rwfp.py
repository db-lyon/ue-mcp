"""Route ResolveWorldFromParams(Params, <scope>) through the F7 resolver.

usage: rwfp.py FILE...
`ResolveWorldFromParams(Params, X)` becomes `FMCPRuntimeWorld::Resolve(Params, X).World`,
so a pieInstance that names no running world is never answered from another one.
"""
import re, sys

for path in sys.argv[1:]:
    raw = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in raw else '\n'
    s = raw.replace('\r\n', '\n')
    s, n = re.subn(r'ResolveWorldFromParams\(Params, ([^;\n]*?)\)(?=;|\s*[;)])', r'FMCPRuntimeWorld::Resolve(Params, \1).World', s)
    if n and '#include "Families/MCPRuntime.h"' not in s:
        s = s.replace('#include "HandlerUtils.h"\n', '#include "HandlerUtils.h"\n#include "Families/MCPRuntime.h"\n', 1)
        if '#include "Families/MCPRuntime.h"' not in s:
            sys.exit('no HandlerUtils include in ' + path)
    open(path, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
    print(n, path)
