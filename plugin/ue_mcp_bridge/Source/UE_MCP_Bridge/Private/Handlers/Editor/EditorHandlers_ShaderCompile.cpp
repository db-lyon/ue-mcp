#include "EditorHandlers.h"
#include "HandlerUtils.h"
#include "ShaderCompiler.h"
#include "MaterialShared.h"
#include "Misc/ScopeLock.h"

TSharedPtr<FJsonValue> FEditorHandlers::GetShaderCompileStatus(const TSharedPtr<FJsonObject>& Params)
{
	const bool WaitUntilIdle = OptionalBool(Params, TEXT("waitUntilIdle"), false);
	const int32 OutstandingJobs = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
	const bool IsCompiling = GShaderCompilingManager && GShaderCompilingManager->IsCompiling();
	TSet<uint32> PendingShaderMaps;
	TSet<FString> PendingMaterials;
	if (GShaderCompilingManager)
	{
		// The array can change on the async loading thread. Only inspect maps
		// still owned by the compiler; completed maps also remain in this array.
		FScopeLock Lock(&FMaterialShaderMap::AllMaterialShaderMapsGuard);
		for (const FMaterialShaderMap* Map : FMaterialShaderMap::AllMaterialShaderMaps)
		{
			if (!Map || Map->GetCompilingId() == 0 || !GShaderCompilingManager->IsCompilingShaderMap(Map->GetCompilingId())) continue;
			PendingShaderMaps.Add(Map->GetCompilingId());
#if UE_MCP_HAS_5_8_API
			PendingMaterials.Add(Map->GetMaterialPath());
#else
			PendingMaterials.Add(Map->GetFriendlyName());
#endif
		}
	}
	const bool Idle = !IsCompiling && OutstandingJobs == 0 && PendingShaderMaps.IsEmpty();
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("managerAvailable"), GShaderCompilingManager != nullptr);
	Result->SetNumberField(TEXT("outstandingJobs"), OutstandingJobs);
	Result->SetBoolField(TEXT("isCompiling"), IsCompiling);
	Result->SetNumberField(TEXT("pendingMaterials"), PendingMaterials.Num());
	Result->SetNumberField(TEXT("pendingShaderMaps"), PendingShaderMaps.Num());
	Result->SetBoolField(TEXT("idle"), Idle);
	Result->SetBoolField(TEXT("waitUntilIdle"), WaitUntilIdle);
	Result->SetStringField(TEXT("state"), Idle ? TEXT("idle") : (WaitUntilIdle ? TEXT("waiting") : TEXT("compiling")));
	// As with PCG generation, the server polls this short game-thread read
	// between ticks. The handler never sleeps or finishes compilation itself.
	return MCPResult(Result);
}
