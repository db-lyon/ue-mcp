#pragma once

// The F7 component lookup for a running StateTree: the component on an actor
// that carries StateTree runtime data, found by shape (an InstanceData of
// FStateTreeInstanceData and a StateTreeRef of FStateTreeReference) so no
// GameplayStateTreeModule dependency is needed. Shared by
// gameplay(get_state_tree_runtime) and the statetree runtime handlers.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "StateTree.h"
#include "StateTreeInstanceData.h"
#include "StateTreeReference.h"

namespace MCPStateTreeRuntime
{
	struct FComponent
	{
		UActorComponent* Component = nullptr;
		const UStateTree* StateTree = nullptr;
		FStateTreeInstanceData* InstanceData = nullptr;
		/** Every StateTree-shaped component on the actor, by name. */
		TArray<FString> Candidates;
	};

	/** The StateTree component on Actor named Name, or the first one when Name is empty. */
	inline FComponent Find(AActor* Actor, const FString& Name)
	{
		FComponent Out;
		if (!Actor) return Out;
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (!Comp) continue;
			FStructProperty* IP = CastField<FStructProperty>(Comp->GetClass()->FindPropertyByName(TEXT("InstanceData")));
			FStructProperty* RP = CastField<FStructProperty>(Comp->GetClass()->FindPropertyByName(TEXT("StateTreeRef")));
			const bool bShaped = IP && IP->Struct == FStateTreeInstanceData::StaticStruct()
				&& RP && RP->Struct == FStateTreeReference::StaticStruct();
			if (!bShaped) continue;
			Out.Candidates.Add(Comp->GetName());
			if (Out.Component || (!Name.IsEmpty() && Comp->GetName() != Name)) continue;
			FStateTreeReference* Ref = RP->ContainerPtrToValuePtr<FStateTreeReference>(Comp);
			Out.Component = Comp;
			Out.StateTree = Ref ? Ref->GetStateTree() : nullptr;
			Out.InstanceData = IP->ContainerPtrToValuePtr<FStateTreeInstanceData>(Comp);
		}
		return Out;
	}
}
