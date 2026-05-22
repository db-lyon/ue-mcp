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
	static TSharedPtr<FJsonValue> SpawnNavModifierVolume(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RebuildNavmesh(const TSharedPtr<FJsonObject>& Params);
	// #424: synchronous path query + invoker enumeration.
	static TSharedPtr<FJsonValue> FindNavPath(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListNavInvokers(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetWorldGameMode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddBlackboardKey(const TSharedPtr<FJsonObject>& Params);
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

	// IMC read/write (#57 / #60 / #75 / #158)
	static TSharedPtr<FJsonValue> ReadImc(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddImcMapping(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetMappingModifiers(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveImcMapping(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetImcMappingKey(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetImcMappingAction(const TSharedPtr<FJsonObject>& Params);

	// PIE inspection (#54 / #89 / #90)
	static TSharedPtr<FJsonValue> InspectPie(const TSharedPtr<FJsonObject>& Params);

	// PIE anim state (#26)
	static TSharedPtr<FJsonValue> GetPieAnimState(const TSharedPtr<FJsonObject>& Params);

	// PIE inspection (#139) — arbitrary UPROPERTY reads on AnimInstance + subsystems
	static TSharedPtr<FJsonValue> GetPieAnimProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetPieSubsystemState(const TSharedPtr<FJsonObject>& Params);

	// Helper to create a blueprint with a given parent class
	static TSharedPtr<FJsonValue> CreateBlueprintWithParent(const FString& Name, const FString& PackagePath, const FString& ParentClassPath, const FString& FriendlyTypeName);

	// v0.7.11 — BT graph traversal (#124)
	static TSharedPtr<FJsonValue> ReadBehaviorTreeGraph(const TSharedPtr<FJsonObject>& Params);

	// #163 — detailed navmesh configuration
	static TSharedPtr<FJsonValue> GetNavmeshDetails(const TSharedPtr<FJsonObject>& Params);

	// #186 — apply damage to PIE actor
	static TSharedPtr<FJsonValue> ApplyDamageInPie(const TSharedPtr<FJsonObject>& Params);

	// PIE input injection (inject_input + continuous + tape).
	static TSharedPtr<FJsonValue> InjectInput(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InjectInputStart(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InjectInputUpdate(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InjectInputStop(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InjectInputTape(const TSharedPtr<FJsonObject>& Params);

	// PIE record (arm/disarm/stop/status/list/read/delete/mark).
	static TSharedPtr<FJsonValue> PieRecordArm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordDisarm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordStop(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordStatus(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordList(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordRead(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordDelete(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieMark(const TSharedPtr<FJsonObject>& Params);

	// PIE replay (arm/disarm/stop/status).
	static TSharedPtr<FJsonValue> PieReplayArm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieReplayDisarm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieReplayStop(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieReplayStatus(const TSharedPtr<FJsonObject>& Params);
};
