#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FGameplayHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> CreateSmartObjectDefinition(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetNavmeshInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetGameFrameworkInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListInputAssets(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListBehaviorTrees(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListEqsQueries(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListStateTrees(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ProjectPointToNavigation(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateInputAction(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateInputMappingContext(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateBlackboard(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateBehaviorTree(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateEqsQuery(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateStateTree(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateGameMode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateGameState(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreatePlayerController(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreatePlayerState(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateHud(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetCollisionProfile(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetPhysicsEnabled(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetCollisionType(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetBodyProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnNavModifierVolume(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RebuildNavmesh(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetCdoDefaults(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetWorldGameMode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateAiPerceptionConfig(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddBlackboardKey(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetupEnhancedInput(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ConfigureBehaviorTree(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetupPathFollowing(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RunEqsQuery(const TSharedPtr<FJsonObject>& Params);

	// Helper to create a blueprint with a given parent class
	static TSharedPtr<FJsonValue> CreateBlueprintWithParent(const FString& Name, const FString& PackagePath, const FString& ParentClassPath, const FString& FriendlyTypeName);
};
