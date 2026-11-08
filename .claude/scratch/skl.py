p = 'plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/Animation/AnimationHandlers_SkeletalLive.cpp'
raw = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in raw else '\n'
s = raw.replace('\r\n', '\n')
old = """	static TArray<TPair<FString, UWorld*>> BuildWorldCandidates(const FString& RequestedScope)
	{
		TArray<TPair<FString, UWorld*>> Candidates;
		if (RequestedScope == TEXT("auto"))
		{
			AddWorldCandidate(Candidates, TEXT("pie"), GetPIEWorld());
			AddWorldCandidate(Candidates, TEXT("editor"), GetEditorWorld());
			return Candidates;
		}
		if (RequestedScope == TEXT("pie") || RequestedScope == TEXT("game"))
		{
			AddWorldCandidate(Candidates, RequestedScope, GetPIEWorld());
			return Candidates;
		}
"""
new = """	// The worlds a query searches, in order. A pieInstance names one live world
	// and is never widened to another (F7); without one the first live world answers.
	static TArray<TPair<FString, UWorld*>> BuildWorldCandidates(const FString& RequestedScope, int32 PIEInstance)
	{
		TArray<TPair<FString, UWorld*>> Candidates;
		const FWorldContext* Live = FMCPRuntimeWorld::LiveContext(PIEInstance);
		if (RequestedScope == TEXT("auto"))
		{
			AddWorldCandidate(Candidates, TEXT("pie"), Live ? Live->World() : nullptr);
			if (PIEInstance == INDEX_NONE) AddWorldCandidate(Candidates, TEXT("editor"), GetEditorWorld());
			return Candidates;
		}
		if (RequestedScope == TEXT("pie") || RequestedScope == TEXT("game"))
		{
			AddWorldCandidate(Candidates, RequestedScope, Live ? Live->World() : nullptr);
			return Candidates;
		}
"""
assert s.count(old) == 1
s = s.replace(old, new)
old2 = """	static TSharedPtr<FJsonValue> ResolveSkeletalActorForQuery(
		const FString& WorldScope,"""
new2 = """	static TSharedPtr<FJsonValue> ResolveSkeletalActorForQuery(
		const TSharedPtr<FJsonObject>& Params,
		const FString& WorldScope,"""
assert s.count(old2) == 1
s = s.replace(old2, new2)
old3 = """		TArray<TPair<FString, UWorld*>> Candidates = BuildWorldCandidates(RequestedScope);"""
new3 = """		int32 PIEInstance = INDEX_NONE;
		TryGetNumberParam(Params, TEXT("pieInstance"), PIEInstance);
		TArray<TPair<FString, UWorld*>> Candidates = BuildWorldCandidates(RequestedScope, PIEInstance);"""
assert s.count(old3) == 1
s = s.replace(old3, new3)
old4 = """		if (Candidates.Num() == 0)
		{
			return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *RequestedScope));
		}"""
new4 = """		if (Candidates.Num() == 0)
		{
			FMCPRuntimeWorld Missing;
			Missing.Requested = PIEInstance;
			return MCPError(Missing.Missing(FString::Printf(TEXT("World not available for scope '%s'"), *RequestedScope)));
		}"""
assert s.count(old4) == 1
s = s.replace(old4, new4)
import re
s, n = re.subn(r'(?<!static TSharedPtr<FJsonValue> )ResolveSkeletalActorForQuery\((?!\n)', 'ResolveSkeletalActorForQuery(Params, ', s)
print('call sites', n)
if '#include "Families/MCPRuntime.h"' not in s:
    s = s.replace('#include "HandlerUtils.h"\n', '#include "HandlerUtils.h"\n#include "Families/MCPRuntime.h"\n', 1)
open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
