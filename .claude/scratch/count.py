import subprocess, re
root = 'plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers'
pats = ['ResolveWorldFromParams(Params', 'GetPIEWorld()', 'GetPIEWorldContext()', 'FMCPRuntimeWorld::',
        'MCPCreateAssetIdempotent', 'MCPCheckActorLabelExists(', 'World->SpawnActor', 'FMCPCreate ', 'FMCPSpawn ',
        'MCPSetDeleteAssetRollback(', 'SaveAssetPackageChecked(']
def count(rev, pat):
    args = ['git', 'grep', '-c', '-F', pat] + ([rev] if rev else []) + ['--', root]
    out = subprocess.run(args, capture_output=True, text=True).stdout
    return sum(int(l.rsplit(':', 1)[1]) for l in out.splitlines() if l.strip())
for p in pats:
    print(f'{p}: before={count("feat/families-blueprint", p)} after={count(None, p)}')
