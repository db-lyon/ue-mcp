import re
p = 'plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/Widget/WidgetHandlers.cpp'
raw = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in raw else '\n'
s = raw.replace('\r\n', '\n')
pat = re.compile(r'(\t+)UWorld\* World = ResolveWidgetRuntimeWorld\(Params\);\n\t+if \(!World\)\n\t+\{\n\t+return MCPError\(NoRuntimeWorldMessage\(Params, (TEXT\("[^"]*"\))\)\);\n\t+\}\n')
def rep(m):
    t = m.group(1)
    return (f'{t}const FMCPRuntimeWorld Live = FMCPRuntimeWorld::ResolveLive(Params);\n'
            f'{t}if (auto Err = Live.Require({m.group(2)})) return Err;\n'
            f'{t}UWorld* World = Live.World;\n')
s, n = pat.subn(rep, s)
print('call sites', n)
helpers = """	// The live world the call names: pieInstance picks one PIE client, and
	// without it the first PIE or Game world answers.
	static UWorld* ResolveWidgetRuntimeWorld(const TSharedPtr<FJsonObject>& Params)
	{
		UWorld* World = ResolveWorldFromParams(Params, TEXT("pie"));
		const bool bLive = World && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game);
		return bLive ? World : nullptr;
	}

	// Why no live world answered, naming the pieInstance when one was asked for.
	static FString NoRuntimeWorldMessage(const TSharedPtr<FJsonObject>& Params, const TCHAR* Default)
	{
		int32 Requested = INDEX_NONE;
		if (TryGetNumberParam(Params, TEXT("pieInstance"), Requested))
		{
			return FString::Printf(
				TEXT("No live PIE world has pieInstance %d. editor(list_pie_instances) lists the running ones."),
				Requested);
		}
		return Default;
	}

"""
assert s.count(helpers) == 1
s = s.replace(helpers, '')
s = s.replace('#include "Families/MCPCreate.h"\n', '#include "Families/MCPCreate.h"\n#include "Families/MCPRuntime.h"\n', 1)
open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
