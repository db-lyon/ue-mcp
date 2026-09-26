#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

// Instanced static mesh instances: add, read back, move and remove them on an
// actor's ISMC or HISMC. Registration stays in LevelHandlers.cpp.

// #434: add instance transforms to a HISMC / ISMC component. The reporter
// hit a Python add_instance crash on UE 5.7; the C++ path through
// UInstancedStaticMeshComponent::AddInstance is stable and HISMC inherits
// it (UHierarchicalInstancedStaticMeshComponent extends UInstancedStaticMeshComponent).
//
// Params:
//   actorLabel: actor that owns the HISMC/ISMC
//   componentName?: pick a specific InstancedStaticMeshComponent on the actor;
//                   omitted = first ISMC/HISMC found
//   transforms: array of [{location: {x,y,z}, rotation? : {pitch,yaw,roll},
//                          scale? : {x,y,z}}]
//   worldSpace? (default true)
TSharedPtr<FJsonValue> FLevelHandlers::AddHismcInstances(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentName"), TEXT("transforms"), TEXT("worldSpace"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	FString ComponentName = OptionalString(Params, TEXT("componentName"));
	UInstancedStaticMeshComponent* ISMC = nullptr;
	for (UActorComponent* Comp : Actor->GetComponents())
	{
		UInstancedStaticMeshComponent* AsISMC = Cast<UInstancedStaticMeshComponent>(Comp);
		if (!AsISMC) continue;
		if (ComponentName.IsEmpty()) { ISMC = AsISMC; break; }
		if (AsISMC->GetName() == ComponentName) { ISMC = AsISMC; break; }
	}
	if (!ISMC)
	{
		return MCPError(FString::Printf(TEXT("No InstancedStaticMeshComponent / HISMC on actor '%s'%s"),
			*ActorLabel, ComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" named '%s'"), *ComponentName)));
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!TryGetArrayParam(Params, TEXT("transforms"), Arr) || !Arr)
	{
		return MCPError(TEXT("Missing 'transforms' array ([{location, rotation?, scale?}])"));
	}
	const bool bWorldSpace = OptionalBool(Params, TEXT("worldSpace"), true);

	TArray<FTransform> Transforms;
	Transforms.Reserve(Arr->Num());
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject>* TObj = nullptr;
		if (!V->TryGetObject(TObj) || !*TObj) continue;
		FVector Location = FVector::ZeroVector;
		FVector Scale = FVector(1, 1, 1);
		FRotator Rotator = FRotator::ZeroRotator;
		const TSharedPtr<FJsonObject>* LObj = nullptr;
		const TSharedPtr<FJsonObject>* SObj = nullptr;
		const TSharedPtr<FJsonObject>* RObj = nullptr;
		if ((*TObj)->TryGetObjectField(TEXT("location"), LObj) && LObj) ReadVec3Fields(*LObj, Location);
		if ((*TObj)->TryGetObjectField(TEXT("scale"), SObj) && SObj) ReadVec3Fields(*SObj, Scale);
		if ((*TObj)->TryGetObjectField(TEXT("rotation"), RObj) && RObj) ReadRotatorFields(*RObj, Rotator);

		Transforms.Add(FTransform(Rotator, Location, Scale));
	}

	if (Transforms.Num() == 0)
	{
		return MCPError(TEXT("transforms array contained no valid entries"));
	}

	ISMC->Modify();
	const int32 FirstIndex = ISMC->GetInstanceCount();
	const TArray<int32> AddedIndices = ISMC->AddInstances(Transforms, /*bShouldReturnIndices*/ true, bWorldSpace);
	ISMC->MarkRenderStateDirty();

	TArray<TSharedPtr<FJsonValue>> IndicesJson;
	IndicesJson.Reserve(AddedIndices.Num());
	for (int32 Idx : AddedIndices) IndicesJson.Add(MakeShared<FJsonValueNumber>(Idx));

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), ISMC->GetName());
	Result->SetStringField(TEXT("componentClass"), ISMC->GetClass()->GetName());
	Result->SetNumberField(TEXT("addedCount"), AddedIndices.Num());
	Result->SetNumberField(TEXT("firstIndex"), FirstIndex);
	Result->SetNumberField(TEXT("totalInstances"), ISMC->GetInstanceCount());
	Result->SetArrayField(TEXT("instanceIndices"), IndicesJson);
	Result->SetBoolField(TEXT("worldSpace"), bWorldSpace);

	// remove_instance takes one index per call, so a single inverse exists only
	// when a single instance was added. Naming it for a multi-instance add
	// would leave every other instance in place while reporting a rollback.
	if (AddedIndices.Num() == 1)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("componentName"), ISMC->GetName());
		Payload->SetNumberField(TEXT("index"), AddedIndices[0]);
		MCPSetRollback(Result, TEXT("remove_instance"), Payload);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Instances are addressed by index, and an index is only a stable handle while the component's instance list is unchanged. The inverse removes the instance this call appended, so it has to run before anything else adds or removes one on the same component. The instance actions do NOT compose under a reverse-order unwind either: this one inverts by index while remove_instance inverts by appending, so unwinding a mixed sequence restores the instance COUNT but not the numbering, and this index-keyed inverse would then delete a different instance. On a World Partition map the inverse resolves the actor by path against loaded actors only, so it fails if the actor's cell unloaded in between."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("%d instances were added and level(remove_instance) removes one index per call, so no single inverse undoes this. instanceIndices names every index added, and removing them highest-first is what unwinds it."),
			AddedIndices.Num()));
	}
	return MCPResult(Result);
}

namespace
{
	// Resolve an ISMC/HISMC on an actor by optional name (first match if empty).
	UInstancedStaticMeshComponent* ResolveISMC(AActor* Actor, const FString& ComponentName)
	{
		if (!Actor) return nullptr;
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			UInstancedStaticMeshComponent* AsISMC = Cast<UInstancedStaticMeshComponent>(Comp);
			if (!AsISMC) continue;
			if (ComponentName.IsEmpty() || AsISMC->GetName() == ComponentName) return AsISMC;
		}
		return nullptr;
	}
}

// #697: read back every instance transform on an actor's ISMC/HISMC.
TSharedPtr<FJsonValue> FLevelHandlers::GetInstanceTransforms(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentName"), TEXT("worldSpace"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	UInstancedStaticMeshComponent* ISMC = ResolveISMC(Actor, ComponentName);
	if (!ISMC) return MCPError(FString::Printf(TEXT("No InstancedStaticMeshComponent on actor '%s'"), *ActorLabel));

	const bool bWorldSpace = OptionalBool(Params, TEXT("worldSpace"), true);
	TArray<TSharedPtr<FJsonValue>> Instances;
	const int32 Count = ISMC->GetInstanceCount();
	for (int32 i = 0; i < Count; ++i)
	{
		FTransform Xf;
		if (!ISMC->GetInstanceTransform(i, Xf, bWorldSpace)) continue;
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetNumberField(TEXT("index"), i);
		E->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Xf.GetLocation()));
		E->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Xf.Rotator()));
		E->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Xf.GetScale3D()));
		Instances.Add(MakeShared<FJsonValueObject>(E));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), ISMC->GetName());
	Result->SetNumberField(TEXT("count"), Count);
	Result->SetBoolField(TEXT("worldSpace"), bWorldSpace);
	Result->SetArrayField(TEXT("instances"), Instances);
	return MCPResult(Result);
}

// #697: update a single instance transform on an ISMC/HISMC by index.
TSharedPtr<FJsonValue> FLevelHandlers::UpdateInstanceTransform(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentName"), TEXT("index"), TEXT("worldSpace"),
		TEXT("location"), TEXT("rotation"), TEXT("scale"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	UInstancedStaticMeshComponent* ISMC = ResolveISMC(Actor, ComponentName);
	if (!ISMC) return MCPError(FString::Printf(TEXT("No InstancedStaticMeshComponent on actor '%s'"), *ActorLabel));

	if (!HasParam(Params, TEXT("index"))) return MCPError(TEXT("Missing 'index'"));
	const int32 Index = OptionalInt(Params, TEXT("index"), -1);
	if (Index < 0 || Index >= ISMC->GetInstanceCount())
	{
		return MCPError(FString::Printf(TEXT("index %d out of range (0..%d)"), Index, ISMC->GetInstanceCount() - 1));
	}
	const bool bWorldSpace = OptionalBool(Params, TEXT("worldSpace"), true);

	// Start from the current transform so partially-specified updates preserve
	// unspecified components.
	FTransform Xf;
	ISMC->GetInstanceTransform(Index, Xf, bWorldSpace);
	FVector Loc = Xf.GetLocation();
	FRotator Rot = Xf.Rotator();
	FVector Scale = Xf.GetScale3D();
	const TSharedPtr<FJsonObject>* Sub = nullptr;
	if (TryGetObjectParam(Params, TEXT("location"), Sub) && Sub) ReadVec3Fields(*Sub, Loc);
	if (TryGetObjectParam(Params, TEXT("rotation"), Sub) && Sub) ReadRotatorFields(*Sub, Rot);
	if (TryGetObjectParam(Params, TEXT("scale"), Sub) && Sub) ReadVec3Fields(*Sub, Scale);

	// The transform this call is about to overwrite, in the same space the
	// inverse will be replayed in.
	const FVector PreviousLoc = Xf.GetLocation();
	const FRotator PreviousRot = Xf.Rotator();
	const FVector PreviousScale = Xf.GetScale3D();

	// Both transforms are in the same space, so this says whether the write
	// moved the instance meaningfully. FTransform::Equals carries a 1e-4
	// tolerance, so it answers "worth reporting as an update", NOT "no write
	// happened": the write below runs either way and a sub-tolerance delta is
	// still a delta on disk. Only the marker keys off this. The rollback does
	// not, or a real edit would be left unrecoverable.
	const FTransform NewTransform(Rot, Loc, Scale);
	const bool bTransformChanged = !Xf.Equals(NewTransform);

	ISMC->Modify();
	const bool bOk = ISMC->UpdateInstanceTransform(Index, NewTransform, bWorldSpace, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
	if (!bOk) return MCPError(FString::Printf(TEXT("UpdateInstanceTransform failed for index %d"), Index));

	auto Result = MCPSuccess();
	if (bTransformChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bTransformChanged);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), ISMC->GetName());
	Result->SetNumberField(TEXT("index"), Index);

	// Emitted unconditionally: UpdateInstanceTransform above already ran.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetStringField(TEXT("componentName"), ISMC->GetName());
	Payload->SetNumberField(TEXT("index"), Index);
	Payload->SetBoolField(TEXT("worldSpace"), bWorldSpace);
	Payload->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(PreviousLoc));
	Payload->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(PreviousRot));
	Payload->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(PreviousScale));
	MCPSetRollback(Result, TEXT("update_instance_transform"), Payload);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Instances are addressed by index, so the inverse has to run before anything else adds or removes an instance on the same component. Note that the instance actions do NOT compose under a reverse-order unwind: add_instances inverts by index while remove_instance inverts by appending, so a sequence mixing them restores the instance COUNT but not the numbering, and an index-keyed inverse replayed after that reaches a different instance. On a World Partition map the inverse also resolves the actor by path against loaded actors only, so it fails if the actor's cell unloaded in between."));
	return MCPResult(Result);
}

// #697: remove a single instance on an ISMC/HISMC by index.
TSharedPtr<FJsonValue> FLevelHandlers::RemoveInstance(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentName"), TEXT("index"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	UInstancedStaticMeshComponent* ISMC = ResolveISMC(Actor, ComponentName);
	if (!ISMC) return MCPError(FString::Printf(TEXT("No InstancedStaticMeshComponent on actor '%s'"), *ActorLabel));

	if (!HasParam(Params, TEXT("index"))) return MCPError(TEXT("Missing 'index'"));
	const int32 Index = OptionalInt(Params, TEXT("index"), -1);
	if (Index < 0 || Index >= ISMC->GetInstanceCount())
	{
		return MCPError(FString::Printf(TEXT("index %d out of range (0..%d)"), Index, ISMC->GetInstanceCount() - 1));
	}

	// Capture the transform before it is dropped. World space, because that is
	// what add_instances defaults to and what makes the restore independent of
	// the component's own transform.
	FTransform RemovedTransform;
	const bool bHaveTransform = ISMC->GetInstanceTransform(Index, RemovedTransform, /*bWorldSpace*/ true);

	ISMC->Modify();
	const bool bOk = ISMC->RemoveInstance(Index);
	ISMC->MarkRenderStateDirty();

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("removed"), bOk);
	Result->SetBoolField(TEXT("unchanged"), !bOk);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), ISMC->GetName());
	Result->SetNumberField(TEXT("remainingInstances"), ISMC->GetInstanceCount());

	if (bOk && bHaveTransform)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(RemovedTransform.GetLocation()));
		Entry->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(RemovedTransform.Rotator()));
		Entry->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(RemovedTransform.GetScale3D()));
		TArray<TSharedPtr<FJsonValue>> Transforms;
		Transforms.Add(MakeShared<FJsonValueObject>(Entry));

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("componentName"), ISMC->GetName());
		Payload->SetBoolField(TEXT("worldSpace"), true);
		Payload->SetArrayField(TEXT("transforms"), Transforms);
		MCPSetRollback(Result, TEXT("add_instances"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The instance comes back at the same world transform but is appended, so it does not return to index %d. Removing an instance also renumbers the ones after it, and those indices are not restored either. Per-instance custom float data is not captured and does not come back. Because this inverse appends while add_instances and update_instance_transform invert BY INDEX, the instance actions do not compose under a reverse-order unwind: the count comes back but the numbering does not, so an index-keyed inverse replayed afterwards reaches a different instance. On a World Partition map the inverse also resolves the actor by path against loaded actors only, so it fails if the actor's cell unloaded in between."),
			Index));
	}
	return MCPResult(Result);
}
