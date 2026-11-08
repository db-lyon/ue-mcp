import re
p = 'plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/Gas/GasHandlers_Runtime.cpp'
raw = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in raw else '\n'
s = raw.replace('\r\n', '\n')
pat = re.compile(r'(\t+)RollbackPayload->SetStringField\(TEXT\("world"\), [^\n]*\);\n\t+CopyPieInstanceToRollback\(Params, RollbackPayload\);\n')
s, n = pat.subn(lambda m: f'{m.group(1)}FMCPRuntimeWorld::Resolve(Params, TEXT("auto")).Echo(RollbackPayload);\n', s)
print('echo sites', n)
old = """	// Defaults to "auto" (prefer PIE), since runtime GAS control is almost
	// always exercised during Play-In-Editor.
	UWorld* ResolveRuntimeWorld(const TSharedPtr<FJsonObject>& Params)
	{
		return ResolveWorldFromParams(Params, TEXT("auto"));
	}

	// A rollback replays against the PIE instance the call acted on.
	void CopyPieInstanceToRollback(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonObject>& Payload)
	{
		double PieInstance = 0.0;
		if (TryGetNumberParam(Params, TEXT("pieInstance"), PieInstance))
		{
			Payload->SetNumberField(TEXT("pieInstance"), PieInstance);
		}
	}

"""
assert s.count(old) == 1
s = s.replace(old, '')
old2 = """		UWorld* World = ResolveRuntimeWorld(Params);
		if (!World)
		{
			OutError = MCPError(TEXT("No world available. For PIE actors, start Play-In-Editor first."));
			return nullptr;
		}
"""
new2 = """		// Defaults to "auto" (prefer PIE): runtime GAS control is almost always exercised in PIE.
		const FMCPRuntimeWorld Where = FMCPRuntimeWorld::Resolve(Params, TEXT("auto"));
		if (auto Err = Where.Require(TEXT("No world available. For PIE actors, start Play-In-Editor first.")))
		{
			OutError = Err;
			return nullptr;
		}
		UWorld* World = Where.World;
"""
assert s.count(old2) == 1
s = s.replace(old2, new2)
if '#include "Families/MCPRuntime.h"' not in s:
    s = s.replace('#include "HandlerUtils.h"\n', '#include "HandlerUtils.h"\n#include "Families/MCPRuntime.h"\n', 1)
open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('ok')
