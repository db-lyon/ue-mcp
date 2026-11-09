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
	TSet<const FMaterial*> PendingMaterials;
#if WITH_EDITOR
	if (GShaderCompilingManager)
	{
		// The engine's shader map list is private, so ask each loaded material's
		// resource for the active platform. Instances sharing a resource count once.
		for (TObjectIterator<UMaterialInterface> It; It; ++It)
		{
			const UMaterialInterface* Material = *It;
			const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
			if (!Resource || PendingMaterials.Contains(Resource) || Resource->IsCompilationFinished()) continue;
			PendingMaterials.Add(Resource);
		}
	}
#endif
	// A resource compiles one shader map at a time, so the counts match.
	const int32 PendingShaderMaps = PendingMaterials.Num();
	const bool Idle = !IsCompiling && OutstandingJobs == 0 && PendingShaderMaps == 0;
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("managerAvailable"), GShaderCompilingManager != nullptr);
	Result->SetNumberField(TEXT("outstandingJobs"), OutstandingJobs);
	Result->SetBoolField(TEXT("isCompiling"), IsCompiling);
	Result->SetNumberField(TEXT("pendingMaterials"), PendingMaterials.Num());
	Result->SetNumberField(TEXT("pendingShaderMaps"), PendingShaderMaps);
	Result->SetBoolField(TEXT("idle"), Idle);
	Result->SetBoolField(TEXT("waitUntilIdle"), WaitUntilIdle);
	Result->SetStringField(TEXT("state"), Idle ? TEXT("idle") : (WaitUntilIdle ? TEXT("waiting") : TEXT("compiling")));
	// The server polls this short game-thread read between ticks when
	// waitUntilIdle is set; the handler never sleeps or blocks on compilation.
	return MCPResult(Result);
}
