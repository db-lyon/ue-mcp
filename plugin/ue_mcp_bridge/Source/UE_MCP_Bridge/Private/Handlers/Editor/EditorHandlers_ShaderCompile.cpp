#include "EditorHandlers.h"
#include "HandlerUtils.h"
#include "ShaderCompiler.h"
#include "MaterialShared.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectIterator.h"

TSharedPtr<FJsonValue> FEditorHandlers::GetShaderCompileStatus(const TSharedPtr<FJsonObject>& Params)
{
	const bool WaitUntilIdle = OptionalBool(Params, TEXT("waitUntilIdle"), false);
	const int32 OutstandingJobs = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
	const bool IsCompiling = GShaderCompilingManager && GShaderCompilingManager->IsCompiling();
	TSet<uint32> PendingShaderMaps;
	TSet<const FMaterial*> PendingMaterials;
	if (GShaderCompilingManager)
	{
		// The engine's shader map list is private, so read each loaded material's
		// resource for the active platform. Instances sharing a parent resource count once.
		for (TObjectIterator<UMaterialInterface> It; It; ++It)
		{
			const UMaterialInterface* Material = *It;
			const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
			if (!Resource || PendingMaterials.Contains(Resource)) continue;
			const uint32 Id = Resource->GetGameThreadCompilingShaderMapId();
			if (Id == 0 || !GShaderCompilingManager->IsCompilingShaderMap(Id)) continue;
			PendingShaderMaps.Add(Id);
			PendingMaterials.Add(Resource);
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
	// The server polls this short game-thread read between ticks when
	// waitUntilIdle is set; the handler never sleeps or blocks on compilation.
	return MCPResult(Result);
}
