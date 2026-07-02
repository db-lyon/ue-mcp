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
	// Enhanced Input asset authoring lives here (core authoring, not test
	// automation). pie-studio handles PIE record/replay/inject of inputs.
	static TSharedPtr<FJsonValue> CreateInputAction(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateInputMappingContext(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadImc(const TSharedPtr<FJsonObject>& Params);
	// #604 read a live PIE player's applied Input Mapping Contexts
	static TSharedPtr<FJsonValue> GetAppliedImcs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddImcMapping(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetMappingModifiers(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveImcMapping(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetImcMappingKey(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetImcMappingAction(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateBlackboard(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateBehaviorTree(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateEqsQuery(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateStateTree(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateGameMode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateGameState(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreatePlayerController(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreatePlayerState(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateHud(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnNavModifierVolume(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RebuildNavmesh(const TSharedPtr<FJsonObject>& Params);
	// #424: synchronous path query + invoker enumeration.
	static TSharedPtr<FJsonValue> FindNavPath(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListNavInvokers(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetWorldGameMode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddBlackboardKey(const TSharedPtr<FJsonObject>& Params);
	// #469: child-of-parent blackboard pattern + per-key removal + read.
	static TSharedPtr<FJsonValue> SetBlackboardParent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveBlackboardKey(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadBlackboard(const TSharedPtr<FJsonObject>& Params);
	// #494: discover available BT node classes so authoring scripts can
	// build asset-specific BTs without grepping the engine source.
	static TSharedPtr<FJsonValue> ListBTNodeClasses(const TSharedPtr<FJsonObject>& Params);
	// #250: rebind a BehaviorTree asset's BlackboardAsset (the C++ field is
	// protected, so reflection is the only way to write it cleanly).
	static TSharedPtr<FJsonValue> SetBehaviorTreeBlackboard(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetBehaviorTreeInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddPerceptionComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ConfigureAiPerceptionSense(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddStateTreeComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddSmartObjectComponent(const TSharedPtr<FJsonObject>& Params);
	// #416: slot authoring on USmartObjectDefinition via UPROPERTY reflection
	// (no Build.cs dependency on SmartObjectsModule).
	static TSharedPtr<FJsonValue> AddSmartObjectSlot(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetSmartObjectSlot(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveSmartObjectSlot(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListSmartObjectSlots(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddSmartObjectSlotBehavior(const TSharedPtr<FJsonObject>& Params);

	// IMC read/write, PIE inspection, anim state, subsystem state — moved to pie-studio

	// Helper to create a blueprint with a given parent class
	static TSharedPtr<FJsonValue> CreateBlueprintWithParent(const FString& Name, const FString& PackagePath, const FString& ParentClassPath, const FString& FriendlyTypeName);

	// v0.7.11 — BT graph traversal (#124)
	static TSharedPtr<FJsonValue> ReadBehaviorTreeGraph(const TSharedPtr<FJsonObject>& Params);

	// Behavior Tree node authoring — builds the runtime tree (RootNode +
	// composite children + decorators + services) directly, reusing the AIModule
	// classes the read path already walks. Defined in GameplayHandlers_BTAuthor.cpp.
	static TSharedPtr<FJsonValue> SetBTRoot(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddBTChild(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddBTDecorator(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddBTService(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveBTNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetBTNodeProperty(const TSharedPtr<FJsonObject>& Params);

	// #163 — detailed navmesh configuration
	static TSharedPtr<FJsonValue> GetNavmeshDetails(const TSharedPtr<FJsonObject>& Params);

	// ApplyDamageInPie moved to pie-studio

};
