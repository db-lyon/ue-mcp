// A PCG component's graph parameters, on a placed actor or on a Blueprint's component template. A component a
// Blueprint's construction script builds is rebuilt from its template on every rerun, so an override written only to
// the placed instance can be discarded; the template is the write that lasts.
#include "PCGHandlers.h"
#include "PCGHandlers_Internal.h"
#include "HandlerUtils.h"
#include "HandlerPackageSave.h"
#include "Handlers/Blueprint/BlueprintHandlers_Internal.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "StructUtils/PropertyBag.h"

namespace
{
	/** Where a PCG component read or write lands: a placed actor's component, or a Blueprint's component template. */
	struct FMCPPCGComponentTarget
	{
		UPCGComponent* Component = nullptr;
		AActor* Actor = nullptr;
		UBlueprint* Blueprint = nullptr;
		FString BlueprintPath;
		FString ComponentName;
		bool bInherited = false;
		/** A placed actor's component that a construction script (SCS or UCS) builds, and so rebuilds on a rerun. */
		bool bConstructed = false;
	};

	/** The PCG component templates a Blueprint has: its own and inherited SCS nodes, then native components. */
	TArray<FString> MCPPCGTemplateNames(UBlueprint* Blueprint)
	{
		TArray<FString> Names;
		for (UBlueprint* BP = Blueprint; BP; BP = UBlueprint::GetBlueprintFromClass(BP->ParentClass))
		{
			if (!BP->SimpleConstructionScript) continue;
			for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Cast<UPCGComponent>(Node->ComponentTemplate)) Names.AddUnique(Node->GetVariableName().ToString());
			}
		}
		if (const AActor* CDO = Blueprint->GeneratedClass ? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(false)) : nullptr)
		{
			TInlineComponentArray<UPCGComponent*> Native;
			CDO->GetComponents(Native);
			for (const UPCGComponent* C : Native) if (C) Names.AddUnique(C->GetName());
		}
		return Names;
	}

	/**
	 * Resolves blueprintPath or actorLabel/actorPath, plus componentName (default: the only PCG component). bForWrite
	 * gives a child Blueprint its own override of an inherited template rather than the parent's.
	 */
	bool MCPPCGResolveComponentTarget(const TSharedPtr<FJsonObject>& Params, bool bForWrite, FMCPPCGComponentTarget& Out, TSharedPtr<FJsonValue>& OutError)
	{
		const FString ComponentName = OptionalString(Params, TEXT("componentName"));
		Out.BlueprintPath = OptionalString(Params, TEXT("blueprintPath"));
		if (!Out.BlueprintPath.IsEmpty())
		{
			Out.Blueprint = LoadAssetByPath<UBlueprint>(Out.BlueprintPath);
			if (!Out.Blueprint)
			{
				OutError = MCPAssetLoadError(Out.BlueprintPath, TEXT("Blueprint"));
				return false;
			}
			const TArray<FString> Candidates = MCPPCGTemplateNames(Out.Blueprint);
			FString Name = ComponentName;
			if (Name.IsEmpty())
			{
				if (Candidates.Num() != 1)
				{
					OutError = MCPError(FString::Printf(TEXT("%s has %d PCG components; name one with componentName: %s"),
						*Out.BlueprintPath, Candidates.Num(), Candidates.Num() > 0 ? *FString::Join(Candidates, TEXT(", ")) : TEXT("none")));
					return false;
				}
				Name = Candidates[0];
			}
			TArray<FString> Available;
			UActorComponent* Template = ResolveComponentTemplate(Out.Blueprint, Name, bForWrite, Out.bInherited, Available);
			Out.Component = Cast<UPCGComponent>(Template);
			if (!Out.Component)
			{
				OutError = MCPError(FString::Printf(TEXT("%s has no PCG component '%s'. Its PCG components: %s."), *Out.BlueprintPath, *Name,
					Candidates.Num() > 0 ? *FString::Join(Candidates, TEXT(", ")) : TEXT("none")));
				return false;
			}
			Out.ComponentName = Name;
			return true;
		}

		UWorld* World = GetEditorWorld();
		if (!World)
		{
			OutError = MCPError(TEXT("No editor world is open."));
			return false;
		}
		Out.Actor = MCPResolveActor(World, Params, OutError);
		if (!Out.Actor) return false;
		TArray<UPCGComponent*> Comps;
		Out.Actor->GetComponents<UPCGComponent>(Comps);
		Comps.RemoveAll([&ComponentName](const UPCGComponent* C) { return !C || (!ComponentName.IsEmpty() && !C->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)); });
		if (Comps.Num() != 1)
		{
			TArray<UPCGComponent*> All;
			Out.Actor->GetComponents<UPCGComponent>(All);
			TArray<FString> Names;
			for (const UPCGComponent* C : All) if (C) Names.Add(C->GetName());
			OutError = MCPError(Comps.Num() == 0
				? FString::Printf(TEXT("Actor %s has no PCG component%s. Its PCG components: %s."), *Out.Actor->GetActorLabel(),
					ComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" named '%s'"), *ComponentName), Names.Num() > 0 ? *FString::Join(Names, TEXT(", ")) : TEXT("none"))
				: FString::Printf(TEXT("Actor %s has %d PCG components; name one with componentName: %s"), *Out.Actor->GetActorLabel(), Comps.Num(), *FString::Join(Names, TEXT(", "))));
			return false;
		}
		Out.Component = Comps[0];
		Out.ComponentName = Out.Component->GetName();
		Out.bConstructed = Out.Component->CreationMethod == EComponentCreationMethod::SimpleConstructionScript
			|| Out.Component->CreationMethod == EComponentCreationMethod::UserConstructionScript;
		return true;
	}

	/** The target fields every component result carries, and the warning for an instance a rerun rebuilds. */
	void MCPPCGDescribeTarget(const FMCPPCGComponentTarget& Target, const TSharedPtr<FJsonObject>& Result)
	{
		Result->SetStringField(TEXT("target"), Target.Blueprint ? TEXT("blueprint") : TEXT("actor"));
		if (Target.Blueprint)
		{
			Result->SetStringField(TEXT("blueprintPath"), Target.Blueprint->GetPathName());
			Result->SetBoolField(TEXT("inherited"), Target.bInherited);
		}
		if (Target.Actor)
		{
			Result->SetStringField(TEXT("actorLabel"), Target.Actor->GetActorLabel());
			Result->SetStringField(TEXT("actorPath"), Target.Actor->GetPathName());
			Result->SetStringField(TEXT("creationMethod"), StaticEnum<EComponentCreationMethod>()->GetNameStringByValue(static_cast<int64>(Target.Component->CreationMethod)));
			if (const UBlueprint* BP = UBlueprint::GetBlueprintFromClass(Target.Actor->GetClass())) Result->SetStringField(TEXT("actorBlueprint"), BP->GetPathName());
		}
		Result->SetStringField(TEXT("componentName"), Target.ComponentName);
		Result->SetBoolField(TEXT("blueprintOwned"), Target.bConstructed);
	}

	FString MCPPCGReconstructionWarning(const FMCPPCGComponentTarget& Target)
	{
		const UBlueprint* BP = Target.Actor ? UBlueprint::GetBlueprintFromClass(Target.Actor->GetClass()) : nullptr;
		return FString::Printf(TEXT("%s on %s is built by its Blueprint's construction script, so a value written to the placed actor can be discarded when the actor is reconstructed. Write it to the Blueprint (blueprintPath %s, componentName %s) so every reconstruction keeps it."),
			*Target.ComponentName, *Target.Actor->GetActorLabel(), BP ? *BP->GetPathName() : TEXT("<the actor's Blueprint>"), *Target.ComponentName);
	}

	void MCPPCGDescribeGraph(const UPCGComponent* Comp, const TSharedPtr<FJsonObject>& Result)
	{
		const UPCGGraphInstance* Instance = Comp->GetGraphInstance();
		const UPCGGraphInterface* Assigned = Instance ? Instance->Graph.Get() : nullptr;
		if (Assigned) Result->SetStringField(TEXT("graph"), Assigned->GetPathName());
		else Result->SetField(TEXT("graph"), MakeShared<FJsonValueNull>());
		if (const UPCGGraph* Top = Comp->GetGraph(); Top && Top != Assigned) Result->SetStringField(TEXT("topGraph"), Top->GetPathName());
		Result->SetArrayField(TEXT("parameters"), Instance ? MCPPCG::DescribeParameterBag(Instance->GetUserParametersStruct(), Instance) : TArray<TSharedPtr<FJsonValue>>());
	}

	/** name -> "overridden|export text", for comparing a component's overrides before and after a rerun. */
	TMap<FString, FString> MCPPCGOverrideSnapshot(const UPCGComponent* Comp)
	{
		TMap<FString, FString> Out;
		const UPCGGraphInstance* Instance = Comp ? Comp->GetGraphInstance() : nullptr;
		const FInstancedPropertyBag* Bag = Instance ? Instance->GetUserParametersStruct() : nullptr;
		const UPropertyBag* BagStruct = Bag ? Bag->GetPropertyBagStruct() : nullptr;
		if (!BagStruct) return Out;
		for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
		{
			if (!Desc.CachedProperty) continue;
			FString Text;
			Desc.CachedProperty->ExportTextItem_Direct(Text, Desc.CachedProperty->ContainerPtrToValuePtr<void>(Bag->GetValue().GetMemory()), nullptr, nullptr, PPF_None);
			Out.Add(Desc.Name.ToString(), FString::Printf(TEXT("%d|%s"), Instance->IsPropertyOverridden(Desc.CachedProperty) ? 1 : 0, *Text));
		}
		return Out;
	}

	/**
	 * Reruns the placed actor's construction scripts, as the editor does after a details edit, and returns the
	 * component the rerun left under the same name (null when it is gone).
	 */
	UPCGComponent* MCPPCGRerunAndRefind(AActor* Actor, const FString& ComponentName)
	{
		Actor->RerunConstructionScripts();
		TArray<UPCGComponent*> Comps;
		Actor->GetComponents<UPCGComponent>(Comps);
		for (UPCGComponent* C : Comps) if (C && C->GetName() == ComponentName) return C;
		return nullptr;
	}

	/** The target selector of Params, for a rollback payload that addresses the same component. */
	TSharedPtr<FJsonObject> MCPPCGTargetPayload(const FMCPPCGComponentTarget& Target)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		if (Target.Blueprint) Payload->SetStringField(TEXT("blueprintPath"), Target.Blueprint->GetPathName());
		if (Target.Actor) Payload->SetStringField(TEXT("actorPath"), Target.Actor->GetPathName());
		Payload->SetStringField(TEXT("componentName"), Target.ComponentName);
		return Payload;
	}

	/** Compiles and saves a Blueprint after a template write, so its placed instances rebuild from the template. */
	void MCPPCGFinishTemplateWrite(const FMCPPCGComponentTarget& Target, const TSharedPtr<FJsonObject>& Result)
	{
		Target.Component->PostEditChange();
		FKismetEditorUtilities::CompileBlueprint(Target.Blueprint);
		FString SaveError;
		const bool bSaved = SaveAssetPackageChecked(Target.Blueprint, SaveError);
		MCPNoteSaveOutcome(Result, Target.Blueprint->GetPathName(), bSaved, SaveError);
	}
}

TSharedPtr<FJsonValue> FPCGHandlers::GetComponentParameters(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("blueprintPath"), TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentName") });
	FMCPPCGComponentTarget Target;
	TSharedPtr<FJsonValue> Error;
	if (!MCPPCGResolveComponentTarget(Params, /*bForWrite*/ false, Target, Error)) return Error;

	auto Result = MCPSuccess();
	MCPPCGDescribeTarget(Target, Result);
	MCPPCGDescribeGraph(Target.Component, Result);
	if (Target.bConstructed)
	{
		Result->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueString>(MCPPCGReconstructionWarning(Target)) });
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::SetComponentParameters(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("blueprintPath"), TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentName"), TEXT("overrides") });
	const TSharedPtr<FJsonObject>* Overrides = nullptr;
	if (!TryGetObjectParam(Params, TEXT("overrides"), Overrides) || !Overrides || (*Overrides)->Values.Num() == 0)
	{
		return MCPError(TEXT("Missing 'overrides': {parameterName: value}; null clears that override."));
	}
	FMCPPCGComponentTarget Target;
	TSharedPtr<FJsonValue> Error;
	if (!MCPPCGResolveComponentTarget(Params, /*bForWrite*/ true, Target, Error)) return Error;
	UPCGGraphInstance* Instance = Target.Component->GetGraphInstance();
	if (!Instance || !Instance->GetGraph())
	{
		return MCPError(FString::Printf(TEXT("PCG component '%s' has no graph, so it has no parameters. Assign one with set_component_graph first."), *Target.ComponentName));
	}
	if (Target.Blueprint)
	{
		if (auto Blocked = MCPAssetWriteBlockedError(Target.Blueprint, Target.BlueprintPath, TEXT("change this Blueprint's PCG parameter overrides"))) return Blocked;
	}

	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "SetPCGComponentParameters", "Set PCG Component Parameters"));
	TSharedPtr<FJsonObject> Previous;
	FString ApplyError;
	if (!MCPPCG::ApplyInstanceOverrides(Instance, *Overrides, { Target.Component }, Previous, ApplyError)) return MCPError(ApplyError);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetObjectField(TEXT("previous"), Previous);
	UPCGComponent* After = Target.Component;
	if (Target.Blueprint)
	{
		MCPPCGFinishTemplateWrite(Target, Result);
		// The compile can rebuild the template object; read back what the Blueprint now holds.
		FMCPPCGComponentTarget Reread;
		TSharedPtr<FJsonValue> RereadError;
		if (MCPPCGResolveComponentTarget(Params, /*bForWrite*/ false, Reread, RereadError)) After = Reread.Component;
	}
	else
	{
		Target.Component->PostEditChange();
		Target.Actor->MarkPackageDirty();
		if (Target.bConstructed)
		{
			// The editor reruns the construction script after a details edit; do the same and see what survived.
			const TMap<FString, FString> Written = MCPPCGOverrideSnapshot(Target.Component);
			After = MCPPCGRerunAndRefind(Target.Actor, Target.ComponentName);
			const TMap<FString, FString> Rebuilt = MCPPCGOverrideSnapshot(After);
			TArray<FString> Lost;
			for (const auto& Pair : (*Overrides)->Values)
			{
				const FString Name(*Pair.Key);
				const FString* Want = Written.Find(Name);
				const FString* Got = Rebuilt.Find(Name);
				if (!Want || !Got || *Want != *Got) Lost.Add(Name);
			}
			Result->SetBoolField(TEXT("persistedThroughReconstruction"), Lost.Num() == 0);
			if (Lost.Num() > 0)
			{
				Result = MCPErrorObject(FString::Printf(TEXT("The construction script rerun discarded the override(s) %s. %s"),
					*FString::Join(Lost, TEXT(", ")), *MCPPCGReconstructionWarning(Target)));
				Result->SetStringField(TEXT("reason"), TEXT("discarded_by_reconstruction"));
				Result->SetBoolField(TEXT("persistedThroughReconstruction"), false);
				TArray<TSharedPtr<FJsonValue>> LostJson;
				for (const FString& Name : Lost) LostJson.Add(MakeShared<FJsonValueString>(Name));
				Result->SetArrayField(TEXT("discarded"), LostJson);
			}
		}
	}
	MCPPCGDescribeTarget(Target, Result);
	if (After) MCPPCGDescribeGraph(After, Result);
	bool bSuccess = true;
	if (Result->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess)
	{
		TSharedPtr<FJsonObject> Payload = MCPPCGTargetPayload(Target);
		Payload->SetObjectField(TEXT("overrides"), Previous);
		MCPSetRollback(Result, TEXT("set_pcg_component_parameters"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::SetComponentGraph(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("blueprintPath"), TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentName"), TEXT("graphPath") });
	FString GraphPath;
	if (!TryGetStringParam(Params, TEXT("graphPath"), GraphPath))
	{
		return MCPError(TEXT("Missing 'graphPath': the PCGGraph or PCGGraphInstance to assign, or \"\" to clear the graph."));
	}
	UPCGGraphInterface* NewGraph = nullptr;
	if (!GraphPath.IsEmpty())
	{
		NewGraph = LoadAssetByPath<UPCGGraphInterface>(GraphPath);
		if (!NewGraph) return MCPAssetLoadError(GraphPath, TEXT("PCGGraph or PCGGraphInstance"));
	}
	FMCPPCGComponentTarget Target;
	TSharedPtr<FJsonValue> Error;
	if (!MCPPCGResolveComponentTarget(Params, /*bForWrite*/ true, Target, Error)) return Error;
	if (Target.Blueprint)
	{
		if (auto Blocked = MCPAssetWriteBlockedError(Target.Blueprint, Target.BlueprintPath, TEXT("change this Blueprint's PCG graph"))) return Blocked;
	}

	const UPCGGraphInstance* Before = Target.Component->GetGraphInstance();
	const UPCGGraphInterface* PreviousGraph = Before ? Before->Graph.Get() : nullptr;
	const FString PreviousPath = PreviousGraph ? PreviousGraph->GetPathName() : FString();
	bool bHadOverrides = false;
	for (const auto& Pair : MCPPCGOverrideSnapshot(Target.Component)) bHadOverrides |= Pair.Value.StartsWith(TEXT("1|"));

	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "SetPCGComponentGraph", "Set PCG Component Graph"));
	Target.Component->Modify();
	if (UPCGGraphInstance* Instance = Target.Component->GetGraphInstance()) Instance->Modify();
	Target.Component->SetGraph(NewGraph);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	if (PreviousGraph) Result->SetStringField(TEXT("previousGraph"), PreviousPath);
	else Result->SetField(TEXT("previousGraph"), MakeShared<FJsonValueNull>());
	UPCGComponent* After = Target.Component;
	if (Target.Blueprint)
	{
		MCPPCGFinishTemplateWrite(Target, Result);
		FMCPPCGComponentTarget Reread;
		TSharedPtr<FJsonValue> RereadError;
		if (MCPPCGResolveComponentTarget(Params, /*bForWrite*/ false, Reread, RereadError)) After = Reread.Component;
	}
	else
	{
		Target.Component->PostEditChange();
		Target.Actor->MarkPackageDirty();
		if (Target.bConstructed)
		{
			After = MCPPCGRerunAndRefind(Target.Actor, Target.ComponentName);
			const UPCGGraphInstance* Rebuilt = After ? After->GetGraphInstance() : nullptr;
			const bool bKept = (Rebuilt ? Rebuilt->Graph.Get() : nullptr) == NewGraph;
			Result->SetBoolField(TEXT("persistedThroughReconstruction"), bKept);
			if (!bKept)
			{
				Result = MCPErrorObject(FString::Printf(TEXT("The construction script rerun discarded the graph assignment. %s"), *MCPPCGReconstructionWarning(Target)));
				Result->SetStringField(TEXT("reason"), TEXT("discarded_by_reconstruction"));
				Result->SetBoolField(TEXT("persistedThroughReconstruction"), false);
			}
		}
	}
	MCPPCGDescribeTarget(Target, Result);
	if (After) MCPPCGDescribeGraph(After, Result);
	bool bSuccess = true;
	if (Result->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess)
	{
		TSharedPtr<FJsonObject> Payload = MCPPCGTargetPayload(Target);
		Payload->SetStringField(TEXT("graphPath"), PreviousPath);
		MCPSetRollback(Result, TEXT("set_pcg_component_graph"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), bHadOverrides);
		if (bHadOverrides)
		{
			Result->SetStringField(TEXT("rollbackNote"), TEXT("The rollback assigns the previous graph again, but not the parameter overrides the component held for it; reapply them with set_component_parameters."));
		}
	}
	return MCPResult(Result);
}
