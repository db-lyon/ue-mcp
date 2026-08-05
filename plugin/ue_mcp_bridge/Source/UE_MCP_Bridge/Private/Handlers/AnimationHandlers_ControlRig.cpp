// Control Rig RigVM graph inspection (#774).
//
// list_control_rig_variables reported each graph as just a name, class and node
// count, which is not enough to verify solver wiring - you cannot tell what a
// rig actually computes from a node count. Reading the RigVM models, their
// nodes, pins and links required dropping to Python.
//
// Translation-unit partition of FAnimationHandlers; registration lives in
// AnimationHandlers.cpp.

#include "AnimationHandlers.h"

#include "HandlerUtils.h"

#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMLink.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"

namespace
{
	FString PinDirectionName(ERigVMPinDirection Direction)
	{
		switch (Direction)
		{
			case ERigVMPinDirection::Input:   return TEXT("Input");
			case ERigVMPinDirection::Output:  return TEXT("Output");
			case ERigVMPinDirection::IO:      return TEXT("IO");
			case ERigVMPinDirection::Visible: return TEXT("Visible");
			case ERigVMPinDirection::Hidden:  return TEXT("Hidden");
			default:                          return TEXT("Invalid");
		}
	}

	TSharedPtr<FJsonObject> PinToJson(URigVMPin* Pin, bool bIncludeDefaults, int32 Depth)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Pin->GetName());
		Obj->SetStringField(TEXT("pinPath"), Pin->GetPinPath());
		Obj->SetStringField(TEXT("cppType"), Pin->GetCPPType());
		Obj->SetStringField(TEXT("direction"), PinDirectionName(Pin->GetDirection()));
		Obj->SetBoolField(TEXT("isExecute"), Pin->IsExecuteContext());
		if (bIncludeDefaults)
		{
			const FString Default = Pin->GetDefaultValue();
			if (!Default.IsEmpty()) Obj->SetStringField(TEXT("defaultValue"), Default);
		}

		// Struct pins expand into sub-pins; that nesting IS the wiring for
		// things like a transform's individual channels, so keep it, bounded.
		const TArray<URigVMPin*>& SubPins = Pin->GetSubPins();
		if (SubPins.Num() > 0 && Depth >= 3)
		{
			// Say so rather than returning a partial pin tree that reads complete.
			Obj->SetBoolField(TEXT("subPinsTruncated"), true);
		}
		if (SubPins.Num() > 0 && Depth < 3)
		{
			TArray<TSharedPtr<FJsonValue>> SubArray;
			for (URigVMPin* Sub : SubPins)
			{
				if (Sub) SubArray.Add(MakeShared<FJsonValueObject>(PinToJson(Sub, bIncludeDefaults, Depth + 1)));
			}
			Obj->SetArrayField(TEXT("subPins"), SubArray);
		}
		return Obj;
	}
}

TSharedPtr<FJsonValue> FAnimationHandlers::ReadControlRigGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UBlueprint* Blueprint = Cast<UBlueprint>(LoadedAsset);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Failed to load Blueprint at '%s'"), *AssetPath));
	}

	const FString GraphFilter = OptionalString(Params, TEXT("graphName"));
	const bool bIncludePins = OptionalBool(Params, TEXT("includePins"), true);
	const bool bIncludeDefaults = OptionalBool(Params, TEXT("includeDefaults"), true);
	const bool bIncludeLinks = OptionalBool(Params, TEXT("includeLinks"), true);
	const int32 NodeLimit = FMath::Max(1, OptionalInt(Params, TEXT("limit"), 200));

	// ControlRigBlueprint was removed in 5.7 and the client-host interface
	// moved, so reach the models by walking the blueprint's own subobjects for
	// URigVMGraph. That is version-tolerant and needs no editor-only host API.
	TArray<UObject*> Subobjects;
#if UE_MCP_HAS_5_8_API
	GetObjectsWithOuter(Blueprint, Subobjects, EGetObjectsFlags::IncludeNestedObjects);
#else
	GetObjectsWithOuter(Blueprint, Subobjects, /*bIncludeNestedObjects=*/true);
#endif

	TArray<URigVMGraph*> Models;
	for (UObject* Object : Subobjects)
	{
		if (URigVMGraph* Graph = Cast<URigVMGraph>(Object))
		{
			// Nested graphs (function library entries, collapsed nodes) are
			// reachable from their own outer; keep them all but de-duplicate.
			Models.AddUnique(Graph);
		}
	}

	if (Models.Num() == 0)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' has no RigVM models. Is this a Control Rig / RigVM asset? (loaded class: %s)"),
			*AssetPath, *Blueprint->GetClass()->GetName()));
	}

	TArray<TSharedPtr<FJsonValue>> GraphsArray;
	int32 TotalNodes = 0;
	int32 TotalLinks = 0;
	bool bTruncatedAny = false;

	for (URigVMGraph* Graph : Models)
	{
		const FString GraphName = Graph->GetName();
		if (!GraphFilter.IsEmpty() && !GraphName.Contains(GraphFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("name"), GraphName);
		GraphObj->SetStringField(TEXT("path"), Graph->GetPathName());

		const TArray<URigVMNode*>& Nodes = Graph->GetNodes();
		GraphObj->SetNumberField(TEXT("nodeCount"), Nodes.Num());
		TotalNodes += Nodes.Num();

		TArray<TSharedPtr<FJsonValue>> NodesArray;
		bool bGraphTruncated = false;
		for (URigVMNode* Node : Nodes)
		{
			if (!Node) continue;
			if (NodesArray.Num() >= NodeLimit) { bTruncatedAny = true; bGraphTruncated = true; break; }

			TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetStringField(TEXT("name"), Node->GetName());
			NodeObj->SetStringField(TEXT("nodePath"), Node->GetNodePath());
			NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());

			if (bIncludePins)
			{
				TArray<TSharedPtr<FJsonValue>> PinsArray;
				for (URigVMPin* Pin : Node->GetPins())
				{
					if (Pin) PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin, bIncludeDefaults, 0)));
				}
				NodeObj->SetArrayField(TEXT("pins"), PinsArray);
			}
			NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
		}
		GraphObj->SetArrayField(TEXT("nodes"), NodesArray);
		// Set at the break site: deriving it from counts false-positives on a
		// graph that merely contains a null node.
		GraphObj->SetBoolField(TEXT("nodesTruncated"), bGraphTruncated);

		if (bIncludeLinks)
		{
			const TArray<URigVMLink*>& Links = Graph->GetLinks();
			TotalLinks += Links.Num();
			TArray<TSharedPtr<FJsonValue>> LinksArray;
			for (URigVMLink* Link : Links)
			{
				if (!Link) continue;
				URigVMPin* Source = Link->GetSourcePin();
				URigVMPin* Target = Link->GetTargetPin();
				if (!Source || !Target) continue;
				TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
				LinkObj->SetStringField(TEXT("source"), Source->GetPinPath(/*bUseNodePath=*/true));
				LinkObj->SetStringField(TEXT("target"), Target->GetPinPath(/*bUseNodePath=*/true));
				LinksArray.Add(MakeShared<FJsonValueObject>(LinkObj));
			}
			GraphObj->SetArrayField(TEXT("links"), LinksArray);
			GraphObj->SetNumberField(TEXT("linkCount"), LinksArray.Num());
		}

		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
	}

	// Member variables with their full metadata, not just a name and category.
	TArray<TSharedPtr<FJsonValue>> VariablesArray;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		VarObj->SetStringField(TEXT("subType"), Var.VarType.PinSubCategory.ToString());
		if (Var.VarType.PinSubCategoryObject.IsValid())
		{
			VarObj->SetStringField(TEXT("subTypeObject"), Var.VarType.PinSubCategoryObject->GetPathName());
		}
		VarObj->SetBoolField(TEXT("isArray"), Var.VarType.IsArray());
		VarObj->SetStringField(TEXT("defaultValue"), Var.DefaultValue);
		VarObj->SetBoolField(TEXT("isPublic"), (Var.PropertyFlags & CPF_BlueprintVisible) != 0);
		VarObj->SetBoolField(TEXT("readOnly"), (Var.PropertyFlags & CPF_BlueprintReadOnly) != 0);
		VariablesArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), Blueprint->GetName());
	if (Blueprint->ParentClass)
	{
		Result->SetStringField(TEXT("parentClass"), Blueprint->ParentClass->GetName());
	}
	Result->SetArrayField(TEXT("models"), GraphsArray);
	Result->SetNumberField(TEXT("modelCount"), GraphsArray.Num());
	Result->SetNumberField(TEXT("totalNodes"), TotalNodes);
	Result->SetNumberField(TEXT("totalLinks"), TotalLinks);
	Result->SetArrayField(TEXT("variables"), VariablesArray);
	Result->SetNumberField(TEXT("variableCount"), VariablesArray.Num());
	if (bTruncatedAny)
	{
		Result->SetStringField(TEXT("note"), TEXT("Some graphs hit the node limit; raise 'limit' or narrow with 'graphName'."));
	}
	return MCPResult(Result);
}
