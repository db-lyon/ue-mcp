#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FMCPHandlerRegistry;

/**
 * Demo scene builder handlers - Neon Shrine demo.
 * Ports the Python demo.py scene builder to C++ for the UE MCP Bridge.
 *
 * Registers 3 handlers:
 *   demo_step       - Execute a single step by index (param: step). If omitted, returns step list.
 *   demo_get_steps  - Return ordered step list.
 *   demo_cleanup    - Remove all demo actors and assets.
 */
class FDemoHandlers
{
public:
	static void RegisterHandlers(FMCPHandlerRegistry& Registry);

private:
	// Top-level handlers
	static TSharedPtr<FJsonValue> DemoStep(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DemoGetSteps(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DemoCleanup(const TSharedPtr<FJsonObject>& Params);

	// ---- Individual step implementations (19 steps) ----
	static TSharedPtr<FJsonObject> StepCreateLevel();
	static TSharedPtr<FJsonObject> StepMaterials();
	static TSharedPtr<FJsonObject> StepFloor();
	static TSharedPtr<FJsonObject> StepPedestal();
	static TSharedPtr<FJsonObject> StepHeroSphere();
	static TSharedPtr<FJsonObject> StepPillars();
	static TSharedPtr<FJsonObject> StepOrbs();
	static TSharedPtr<FJsonObject> StepNeonLights();
	static TSharedPtr<FJsonObject> StepHeroLight();
	static TSharedPtr<FJsonObject> StepMoonlight();
	static TSharedPtr<FJsonObject> StepSkyLight();
	static TSharedPtr<FJsonObject> StepFog();
	static TSharedPtr<FJsonObject> StepPostProcess();
	static TSharedPtr<FJsonObject> StepNiagaraVfx();
	static TSharedPtr<FJsonObject> StepPcgScatter();
	static TSharedPtr<FJsonObject> StepOrbitRings();
	static TSharedPtr<FJsonObject> StepLevelSequence();
	static TSharedPtr<FJsonObject> StepTuningPanel();
	static TSharedPtr<FJsonObject> StepSave();

	// ---- Utility helpers ----
	static AActor* SpawnMesh(const FString& Label, const FString& MeshPath,
		FVector Location,
		FRotator Rotation = FRotator::ZeroRotator,
		FVector Scale = FVector(1, 1, 1));

	static AActor* SpawnPointLight(const FString& Label, FVector Location,
		FColor Color, float Intensity);

	static UMaterialInterface* LoadDemoMat(const FString& Name);

	static void ApplyMat(AActor* Actor, UMaterialInterface* Mat);

	// Build the ordered step list (id, name, description)
	struct FDemoStep
	{
		int32 Index;
		FString Id;
		FString Description;
	};
	static TArray<FDemoStep> GetStepDefinitions();
};
