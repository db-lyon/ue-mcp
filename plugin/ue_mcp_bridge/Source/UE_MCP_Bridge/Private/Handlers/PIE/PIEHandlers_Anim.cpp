#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNode_StateMachine.h"
#include "AnimationRuntime.h"
#include "Subsystems/SubsystemBlueprintLibrary.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"

// actorLabel matches label then object name; ambiguity is refused.
static FMCPActorSelector PieActorSelector()
{
	FMCPActorSelector Selector;
	Selector.Match = EMCPActorMatch::LabelOrName;
	Selector.WorldLabel = TEXT("PIE");
	return Selector;
}

// ─────────────────────────────────────────────────────────────
// get_pie_anim_state
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FPIEHandlers::GetPieAnimState(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	FWorldContext* PIEContext = GEditor->GetPIEWorldContext();
	if (!PIEContext || !PIEContext->World())
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	UWorld* PIEWorld = PIEContext->World();

	TSharedPtr<FJsonValue> ResolveErr;
	AActor* FoundActor = MCPResolveActorToken(PIEWorld, ActorLabel, ResolveErr, PieActorSelector());
	if (!FoundActor) return ResolveErr;

	USkeletalMeshComponent* SkelMesh = FoundActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!SkelMesh)
	{
		return MCPError(TEXT("Actor does not have a SkeletalMeshComponent"));
	}

	UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
	if (!AnimInst)
	{
		return MCPError(TEXT("No AnimInstance on the SkeletalMeshComponent"));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorName"), FoundActor->GetName());
	Result->SetStringField(TEXT("animClass"), AnimInst->GetClass()->GetName());

	UAnimMontage* CurrentMontage = AnimInst->GetCurrentActiveMontage();
	Result->SetStringField(TEXT("currentMontage"), CurrentMontage ? CurrentMontage->GetName() : TEXT("None"));
	if (CurrentMontage)
	{
		Result->SetNumberField(TEXT("montagePosition"), AnimInst->Montage_GetPosition(CurrentMontage));
		Result->SetBoolField(TEXT("montageIsPlaying"), AnimInst->Montage_IsPlaying(CurrentMontage));
	}

	TArray<TSharedPtr<FJsonValue>> StatesArr;
	if (const IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(AnimInst->GetClass()))
	{
		const TArray<FBakedAnimationStateMachine>& BakedMachines = AnimClassInterface->GetBakedStateMachines();
		for (int32 MachineIdx = 0; MachineIdx < BakedMachines.Num(); ++MachineIdx)
		{
			const FBakedAnimationStateMachine& BakedMachine = BakedMachines[MachineIdx];
			TSharedPtr<FJsonObject> SMObj = MakeShared<FJsonObject>();
			SMObj->SetStringField(TEXT("machineName"), BakedMachine.MachineName.ToString());
			SMObj->SetNumberField(TEXT("machineIndex"), MachineIdx);
			SMObj->SetNumberField(TEXT("stateCount"), BakedMachine.States.Num());

			const FAnimNode_StateMachine* SM = AnimInst->GetStateMachineInstance(MachineIdx);
			if (SM)
			{
				int32 StateIdx = SM->GetCurrentState();
				SMObj->SetNumberField(TEXT("currentStateIndex"), StateIdx);
				if (BakedMachine.States.IsValidIndex(StateIdx))
				{
					SMObj->SetStringField(TEXT("currentStateName"), BakedMachine.States[StateIdx].StateName.ToString());
				}
			}

			StatesArr.Add(MakeShared<FJsonValueObject>(SMObj));
		}
	}
	Result->SetArrayField(TEXT("stateMachines"), StatesArr);

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// get_pie_anim_properties
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FPIEHandlers::GetPieAnimProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;
	TArray<FString> RequestedNames;
	const TArray<TSharedPtr<FJsonValue>>* NamesArr = nullptr;
	if (TryGetArrayParam(Params, TEXT("propertyNames"), NamesArr) && NamesArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *NamesArr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S)) RequestedNames.Add(S);
		}
	}

	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	TSharedPtr<FJsonValue> ResolveErr;
	AActor* FoundActor = MCPResolveActorToken(PIEWorld, ActorLabel, ResolveErr, PieActorSelector());
	if (!FoundActor) return ResolveErr;

	USkeletalMeshComponent* SkelMesh = FoundActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!SkelMesh) return MCPError(TEXT("Actor has no SkeletalMeshComponent"));
	UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
	if (!AnimInst) return MCPError(TEXT("No AnimInstance on the SkeletalMeshComponent"));

	auto ExportOne = [AnimInst](FProperty* Prop) -> FString
	{
		FString ValueStr;
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(AnimInst);
		Prop->ExportText_Direct(ValueStr, ValuePtr, ValuePtr, AnimInst, PPF_None);
		return ValueStr;
	};

	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	if (RequestedNames.Num() > 0)
	{
		for (const FString& Name : RequestedNames)
		{
			FProperty* Prop = AnimInst->GetClass()->FindPropertyByName(*Name);
			if (!Prop)
			{
				Props->SetStringField(Name, TEXT("<not found>"));
				continue;
			}
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("type"), Prop->GetCPPType());
			Entry->SetStringField(TEXT("value"), ExportOne(Prop));
			Props->SetObjectField(Name, Entry);
		}
	}
	else
	{
		for (TFieldIterator<FProperty> It(AnimInst->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("type"), Prop->GetCPPType());
			Entry->SetStringField(TEXT("value"), ExportOne(Prop));
			Props->SetObjectField(Prop->GetName(), Entry);
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("animClass"), AnimInst->GetClass()->GetName());
	Result->SetObjectField(TEXT("properties"), Props);
	return MCPResult(Result);
}
