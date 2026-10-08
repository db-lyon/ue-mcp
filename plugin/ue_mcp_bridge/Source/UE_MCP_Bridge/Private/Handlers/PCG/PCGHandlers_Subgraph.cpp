#include "PCGHandlers.h"
#include "PCGHandlers_Internal.h"
#include "HandlerUtils.h"
#include "HandlerPackageSave.h"
#include "HandlerJsonProperty.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSubgraph.h"

// #1253: assigning a subgraph by reflection skips SetSubgraph, so the instance never builds its parameter bag.

namespace
{
	struct FMCPSubgraphNode
	{
		UPCGGraph* Graph = nullptr;
		UPCGNode* Node = nullptr;
		UPCGSubgraphSettings* Settings = nullptr;
	};

	TSharedPtr<FJsonValue> MCPResolveSubgraphNode(const TSharedPtr<FJsonObject>& Params, FMCPSubgraphNode& Out)
	{
		FString AssetPath;
		if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
		FString NodeName;
		if (auto Err = RequireString(Params, TEXT("nodeName"), NodeName)) return Err;

		Out.Graph = LoadAssetByPath<UPCGGraph>(AssetPath);
		if (!Out.Graph) return MCPAssetLoadError(AssetPath, TEXT("PCGGraph"));
		Out.Node = MCPPCG::FindNode(Out.Graph, NodeName);
		if (!Out.Node) return MCPPCG::NodeNotFoundError(Out.Graph, NodeName);
		Out.Settings = Cast<UPCGSubgraphSettings>(const_cast<UPCGSettings*>(Out.Node->GetSettings()));
		if (!Out.Settings)
		{
			return MCPError(FString::Printf(TEXT("Node %s is a %s, not a Subgraph node."), *NodeName,
				Out.Node->GetSettings() ? *Out.Node->GetSettings()->GetClass()->GetName() : TEXT("node without settings")));
		}
		return nullptr;
	}

	FString MCPSubgraphPathOf(const UPCGSubgraphSettings* Settings)
	{
		const UPCGGraphInterface* Sub = Settings && Settings->SubgraphInstance ? Settings->SubgraphInstance->Graph.Get() : nullptr;
		return Sub ? Sub->GetPathName() : FString();
	}

	TSharedPtr<FJsonValue> MCPSubgraphParamValue(const FProperty* Property, const void* ValueAddr)
	{
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property)) return MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(ValueAddr));
		if (const FNumericProperty* Number = CastField<FNumericProperty>(Property))
		{
			if (!Number->IsEnum())
			{
				return MakeShared<FJsonValueNumber>(Number->IsFloatingPoint()
					? Number->GetFloatingPointPropertyValue(ValueAddr)
					: static_cast<double>(Number->GetSignedIntPropertyValue(ValueAddr)));
			}
		}
		FString Text;
		Property->ExportTextItem_Direct(Text, ValueAddr, nullptr, nullptr, PPF_None);
		return MakeShared<FJsonValueString>(Text);
	}

	/** Every user parameter of the node's graph instance: name, type, current value, and whether this node overrides it. */
	TArray<TSharedPtr<FJsonValue>> MCPDescribeSubgraphParameters(UPCGSubgraphSettings* Settings)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		UPCGGraphInstance* Instance = Settings ? Settings->SubgraphInstance.Get() : nullptr;
		const FInstancedPropertyBag* Bag = Instance ? Instance->GetUserParametersStruct() : nullptr;
		const UPropertyBag* BagStruct = Bag ? Bag->GetPropertyBagStruct() : nullptr;
		if (!BagStruct) return Out;
		const void* Memory = Bag->GetValue().GetMemory();
		for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
		{
			if (!Desc.CachedProperty || !Memory) continue;
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Desc.Name.ToString());
			Entry->SetStringField(TEXT("type"), Desc.CachedProperty->GetCPPType());
			Entry->SetField(TEXT("value"), MCPSubgraphParamValue(Desc.CachedProperty, Desc.CachedProperty->ContainerPtrToValuePtr<void>(Memory)));
			Entry->SetBoolField(TEXT("overridden"), Instance->IsPropertyOverridden(Desc.CachedProperty));
			Out.Add(MakeShared<FJsonValueObject>(Entry));
		}
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> MCPPinLabels(const TArray<TObjectPtr<UPCGPin>>& Pins)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const UPCGPin* Pin : Pins)
		{
			if (Pin) Out.Add(MakeShared<FJsonValueString>(Pin->Properties.Label.ToString()));
		}
		return Out;
	}

	void MCPCommitSubgraphEdit(const FMCPSubgraphNode& Target, const TSharedPtr<FJsonObject>& Result)
	{
		Target.Settings->PostEditChange();
		Target.Graph->PostEditChange();
		Target.Graph->MarkPackageDirty();
		FString SaveError;
		const bool bSaved = SaveAssetPackageChecked(Target.Graph, SaveError);
		MCPNoteSaveOutcome(Result, Target.Graph->GetPathName(), bSaved, SaveError);
	}
	bool MCPApplySubgraphParameters(const FMCPSubgraphNode& Target, const TSharedPtr<FJsonObject>& Values,
		TSharedPtr<FJsonObject>& PreviousValues, FString& Error)
	{
		UPCGGraphInstance* Instance = Target.Settings->SubgraphInstance.Get();
		// The mutable accessor is protected; the instance owns this bag, and UpdatePropertyOverride below is its notification.
		FInstancedPropertyBag* Bag = Instance ? const_cast<FInstancedPropertyBag*>(Instance->GetUserParametersStruct()) : nullptr;
		const UPropertyBag* BagStruct = Bag ? Bag->GetPropertyBagStruct() : nullptr;
		if (!BagStruct)
		{
			Error = TEXT("The node's subgraph has no user parameters. Assign one with set_subgraph first.");
			return false;
		}

		// Every value is written to a scratch copy first, so a bad entry refuses the call with nothing changed.
		FInstancedPropertyBag Scratch = *Bag;
		TArray<FString> Problems;
		struct FWrite { const FPropertyBagPropertyDesc* Desc; bool bClear; };
		TArray<FWrite> Writes;
		for (const auto& Pair : Values->Values)
		{
			const FString Name(*Pair.Key);
			const FPropertyBagPropertyDesc* Desc = BagStruct->FindPropertyDescByName(FName(*Name));
			if (!Desc || !Desc->CachedProperty)
			{
				Problems.Add(FString::Printf(TEXT("%s is not a parameter of this subgraph"), *Name));
				continue;
			}
			const bool bClear = !Pair.Value.IsValid() || Pair.Value->IsNull();
			if (!bClear)
			{
				FString ValueError;
				void* Addr = Desc->CachedProperty->ContainerPtrToValuePtr<void>(Scratch.GetMutableValue().GetMemory());
				if (!MCPJsonProperty::SetJsonOnProperty(const_cast<FProperty*>(Desc->CachedProperty), Addr, Pair.Value, ValueError))
				{
					Problems.Add(FString::Printf(TEXT("%s: %s"), *Name, *ValueError));
					continue;
				}
			}
			Writes.Add({ Desc, bClear });
		}
		if (Problems.Num() > 0)
		{
			Error = FString::Printf(TEXT("Nothing was changed: %s. set_subgraph lists the parameters."), *FString::Join(Problems, TEXT("; ")));
			return false;
		}

		PreviousValues = MakeShared<FJsonObject>();
		Target.Graph->Modify();
		Target.Settings->Modify();
		Instance->Modify();
		const void* Live = Bag->GetValue().GetMemory();
		for (const FWrite& W : Writes)
		{
			const FProperty* Prop = W.Desc->CachedProperty;
			PreviousValues->SetField(W.Desc->Name.ToString(), Instance->IsPropertyOverridden(Prop)
				? MCPSubgraphParamValue(Prop, Prop->ContainerPtrToValuePtr<void>(Live))
				: MakeShared<FJsonValueNull>());
		}
		for (const FWrite& W : Writes)
		{
			const FProperty* Prop = W.Desc->CachedProperty;
			if (!W.bClear)
			{
				Prop->CopyCompleteValue(Prop->ContainerPtrToValuePtr<void>(Bag->GetMutableValue().GetMemory()),
					Prop->ContainerPtrToValuePtr<void>(Scratch.GetValue().GetMemory()));
			}
			Instance->UpdatePropertyOverride(Prop, !W.bClear);
		}
		return true;
	}

}

TSharedPtr<FJsonValue> FPCGHandlers::SetSubgraph(const TSharedPtr<FJsonObject>& Params)
{
	// An empty path is a request to clear the subgraph, so only absence is an error.
	FString SubgraphPath;
	const bool bHasSubgraphPath = TryGetStringParam(Params, TEXT("subgraphPath"), SubgraphPath);
	FMCPSubgraphNode Target;
	if (auto Err = MCPResolveSubgraphNode(Params, Target)) return Err;
	if (!bHasSubgraphPath)
	{
		return MCPError(TEXT("Missing required parameter 'subgraphPath' (pass \"\" to clear the subgraph)"));
	}

	UPCGGraphInterface* Subgraph = nullptr;
	if (!SubgraphPath.IsEmpty())
	{
		Subgraph = LoadAssetByPath<UPCGGraphInterface>(SubgraphPath);
		if (!Subgraph) return MCPAssetLoadError(SubgraphPath, TEXT("PCGGraph or PCGGraphInstance"));
	}

	const FString Previous = MCPSubgraphPathOf(Target.Settings);
	const FString Next = Subgraph ? Subgraph->GetPathName() : FString();
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("nodeName"), Target.Node->GetName());
	Result->SetStringField(TEXT("previousSubgraph"), Previous);
	if (Previous == Next)
	{
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("updated"), false);
	}
	else
	{
		Target.Graph->Modify();
		Target.Settings->Modify();
		Target.Settings->SetSubgraph(Subgraph);
		Target.Node->UpdateAfterSettingsChangeDuringCreation();
		MCPSetUpdated(Result);
		MCPCommitSubgraphEdit(Target, Result);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Target.Graph->GetPathName());
		Payload->SetStringField(TEXT("nodeName"), Target.Node->GetName());
		Payload->SetStringField(TEXT("subgraphPath"), Previous);
		MCPSetRollback(Result, TEXT("set_pcg_subgraph"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), TEXT("Restores the previous subgraph; parameter overrides set on this node for the new subgraph are not carried back."));
	}
	Result->SetStringField(TEXT("subgraph"), Next);
	Result->SetStringField(TEXT("nodeTitle"), Target.Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());
	Result->SetArrayField(TEXT("inputPins"), MCPPinLabels(Target.Node->GetInputPins()));
	Result->SetArrayField(TEXT("outputPins"), MCPPinLabels(Target.Node->GetOutputPins()));
	Result->SetArrayField(TEXT("parameters"), MCPDescribeSubgraphParameters(Target.Settings));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::SetSubgraphParameters(const TSharedPtr<FJsonObject>& Params)
{
	const TSharedPtr<FJsonObject>* Values = nullptr;
	const bool bHasValues = TryGetObjectParam(Params, TEXT("parameters"), Values);
	FMCPSubgraphNode Target;
	if (auto Err = MCPResolveSubgraphNode(Params, Target)) return Err;
	if (!bHasValues || !Values || !Values->IsValid() || (*Values)->Values.Num() == 0)
	{
		return MCPError(TEXT("Missing 'parameters': an object of {parameterName: value}; null clears that override."));
	}

	TSharedPtr<FJsonObject> PreviousValues;
	FString Error;
	if (!MCPApplySubgraphParameters(Target, *Values, PreviousValues, Error)) return MCPError(Error);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("nodeName"), Target.Node->GetName());
	Result->SetStringField(TEXT("subgraph"), MCPSubgraphPathOf(Target.Settings));
	Result->SetObjectField(TEXT("previous"), PreviousValues);
	MCPSetUpdated(Result);
	MCPCommitSubgraphEdit(Target, Result);
	Result->SetArrayField(TEXT("parameters"), MCPDescribeSubgraphParameters(Target.Settings));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), Target.Graph->GetPathName());
	Payload->SetStringField(TEXT("nodeName"), Target.Node->GetName());
	Payload->SetObjectField(TEXT("parameters"), PreviousValues);
	MCPSetRollback(Result, TEXT("set_pcg_subgraph_parameters"), Payload);
	return MCPResult(Result);
}

// Export the graph asset and overridden values, never a path to the node-owned instance.
void FPCGHandlers::ExportSubgraphSettings(const UPCGSettings* Settings, const TSharedPtr<FJsonObject>& Node)
{
	const auto* Subgraph = Cast<UPCGSubgraphSettings>(Settings);
	if (!Subgraph) return;
	auto Snapshot = MakeShared<FJsonObject>();
	Snapshot->SetStringField(TEXT("path"), MCPSubgraphPathOf(Subgraph));
	auto Overrides = MakeShared<FJsonObject>();
	for (const auto& Value : MCPDescribeSubgraphParameters(const_cast<UPCGSubgraphSettings*>(Subgraph)))
	{
		const auto Param = Value->AsObject();
		if (Param->GetBoolField(TEXT("overridden"))) Overrides->SetField(Param->GetStringField(TEXT("name")), Param->TryGetField(TEXT("value")));
	}
	Snapshot->SetObjectField(TEXT("parameters"), Overrides);
	Node->SetObjectField(TEXT("subgraph"), Snapshot);
}

bool FPCGHandlers::ImportSubgraphSettings(UPCGSettings* Settings, const TSharedPtr<FJsonObject>& Node, FString& Error)
{
	if (!Node->HasField(TEXT("subgraph"))) return true;
	const TSharedPtr<FJsonObject>* Snapshot = nullptr;
	FString Path;
	const TSharedPtr<FJsonObject>* Overrides = nullptr;
	auto* Subgraph = Cast<UPCGSubgraphSettings>(Settings);
	if (!Subgraph || !Node->TryGetObjectField(TEXT("subgraph"), Snapshot) || !Snapshot
		|| !(*Snapshot)->TryGetStringField(TEXT("path"), Path)
		|| !(*Snapshot)->TryGetObjectField(TEXT("parameters"), Overrides) || !Overrides)
	{
		Error = TEXT("subgraph requires a Subgraph node and {path, parameters} snapshot");
		return false;
	}
	UPCGGraphInterface* Graph = Path.IsEmpty() ? nullptr : LoadAssetByPath<UPCGGraphInterface>(Path);
	if (!Path.IsEmpty() && !Graph)
	{
		Error = FString::Printf(TEXT("subgraph asset could not be loaded: %s"), *Path);
		return false;
	}
	Subgraph->SetSubgraph(Graph);
	FMCPSubgraphNode Target{Settings->GetTypedOuter<UPCGGraph>(), Settings->GetTypedOuter<UPCGNode>(), Subgraph};
	Target.Node->UpdateAfterSettingsChangeDuringCreation();
	if ((*Overrides)->Values.Num() == 0) return true;
	TSharedPtr<FJsonObject> Previous;
	return MCPApplySubgraphParameters(Target, *Overrides, Previous, Error);
}
