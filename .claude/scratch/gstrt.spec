	// Find a component that carries StateTree runtime data (by having an
	// InstanceData property of type FStateTreeInstanceData + a StateTreeRef).
	UActorComponent* STComp = nullptr;
	FStructProperty* InstanceProp = nullptr;
	FStructProperty* RefProp = nullptr;
	for (UActorComponent* Comp : Actor->GetComponents())
	{
		if (!Comp) continue;
		if (!CompName.IsEmpty() && Comp->GetName() != CompName) continue;
		FStructProperty* IP = CastField<FStructProperty>(Comp->GetClass()->FindPropertyByName(TEXT("InstanceData")));
		FStructProperty* RP = CastField<FStructProperty>(Comp->GetClass()->FindPropertyByName(TEXT("StateTreeRef")));
		if (IP && IP->Struct == FStateTreeInstanceData::StaticStruct() &&
			RP && RP->Struct == FStateTreeReference::StaticStruct())
		{
			STComp = Comp; InstanceProp = IP; RefProp = RP; break;
		}
	}
	if (!STComp) return MCPError(FString::Printf(TEXT("No StateTree component found on '%s'"), *ActorLabel));

	FStateTreeReference* Ref = RefProp->ContainerPtrToValuePtr<FStateTreeReference>(STComp);
	const UStateTree* StateTree = Ref ? Ref->GetStateTree() : nullptr;
	FStateTreeInstanceData* InstanceData = InstanceProp->ContainerPtrToValuePtr<FStateTreeInstanceData>(STComp);
	if (!StateTree || !InstanceData) return MCPError(TEXT("StateTree asset or instance data unavailable"));
-----
	const MCPStateTreeRuntime::FComponent Found = MCPStateTreeRuntime::Find(Actor, CompName);
	UActorComponent* STComp = Found.Component;
	if (!STComp) return MCPError(FString::Printf(TEXT("No StateTree component found on '%s'"), *ActorLabel));
	const UStateTree* StateTree = Found.StateTree;
	FStateTreeInstanceData* InstanceData = Found.InstanceData;
	if (!StateTree || !InstanceData) return MCPError(TEXT("StateTree asset or instance data unavailable"));
=====
#include "Families/MCPWorldSpawn.h"
-----
#include "Families/MCPWorldSpawn.h"
#include "Handlers/StateTree/StateTreeRuntimeComponent.h"
