// PCG Custom HLSL node source, and edge validation (#1828).
//
// pcg(get_custom_hlsl) and pcg(set_custom_hlsl) read and write the kernel source
// of a Custom HLSL node (UPCGCustomHLSLSettings). That class is declared in a
// private PCG header, so it is reached by reflection: the text lives in the
// ShaderSource and ShaderFunctions properties, the read-only declarations in
// InputDeclarations, OutputDeclarations and HelperDeclarations, and the node is
// read-only while KernelSourceOverride names a compute source asset. Those are
// exactly what GetSourceText, GetFunctionsText, GetDeclarationsText and
// IsReadOnly return. On UE 5.9 a write calls the class's own SetSourceText and
// SetFunctionsText UFUNCTIONs; before it, the property is written the way those
// functions write it (PreEditChange, Modify, assign, PostEditChangeProperty).
//
// pcg(validate_edge) asks UPCGGraph::ValidateEdge whether an edge can be added
// and why not, without touching the graph (PCGEdgeValidation.h).
//
// Translation-unit partition of FPCGHandlers; registration is
// RegisterCustomHLSLHandlers below, called from PCGHandlers.cpp.

#include "PCGHandlers.h"
#include "PCGHandlers_Internal.h"
#include "PCGEdgeValidation.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	const TCHAR* const MCPHlslClassName = TEXT("PCGCustomHLSLSettings");

	/** The node's settings when they are a Custom HLSL node's, or an error to return. */
	UPCGSettings* MCPHlslResolveSettings(UPCGGraph* Graph, const FString& NodeName, TSharedPtr<FJsonValue>& OutError)
	{
		UPCGNode* Node = MCPPCG::FindNode(Graph, NodeName);
		if (!Node)
		{
			OutError = MCPPCG::NodeNotFoundError(Graph, NodeName);
			return nullptr;
		}
		UPCGSettings* Settings = Node->GetSettings();
		if (!Settings || Settings->GetClass()->GetName() != MCPHlslClassName)
		{
			OutError = MCPError(FString::Printf(TEXT("Node '%s' is a %s, not a Custom HLSL node (%s)."),
				*NodeName, Settings ? *Settings->GetClass()->GetName() : TEXT("node without settings"), MCPHlslClassName));
			return nullptr;
		}
		return Settings;
	}

	FStrProperty* MCPHlslStringProperty(const UPCGSettings* Settings, const TCHAR* Name)
	{
		return FindFProperty<FStrProperty>(Settings->GetClass(), Name);
	}

	FString MCPHlslReadString(const UPCGSettings* Settings, const TCHAR* Name)
	{
		const FStrProperty* Prop = MCPHlslStringProperty(Settings, Name);
		return Prop ? Prop->GetPropertyValue_InContainer(Settings) : FString();
	}

	/** KernelSourceOverride names a compute source asset, which then supplies the kernel. */
	UObject* MCPHlslKernelOverride(const UPCGSettings* Settings)
	{
		const FObjectPropertyBase* Prop = FindFProperty<FObjectPropertyBase>(Settings->GetClass(), TEXT("KernelSourceOverride"));
		return Prop ? Prop->GetObjectPropertyValue_InContainer(Settings) : nullptr;
	}

	/** Writes one text property the way UPCGCustomHLSLSettings::SetSourceText / SetFunctionsText do. */
	bool MCPHlslWriteText(UPCGSettings* Settings, const TCHAR* PropertyName, const TCHAR* EngineFunction, const FString& Text, FString& OutError)
	{
#if UE_MCP_HAS_5_9_API
		if (UFunction* Function = Settings->FindFunction(FName(EngineFunction)))
		{
			struct FParams { FString InText; } Args{ Text };
			Settings->ProcessEvent(Function, &Args);
			return true;
		}
#endif
		FStrProperty* Prop = MCPHlslStringProperty(Settings, PropertyName);
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("%s has no %s property on this engine."), *Settings->GetClass()->GetName(), PropertyName);
			return false;
		}
		// Public on UObject, protected on the settings classes; the call still dispatches to their override.
		static_cast<UObject*>(Settings)->PreEditChange(Prop);
		Settings->Modify();
		Prop->SetPropertyValue_InContainer(Settings, Text);
		MCPPCG::NotifySettingsChanged(Settings, { FString(PropertyName) });
		return true;
	}
}


// pcg(get_custom_hlsl): GetSourceText, GetFunctionsText, GetDeclarationsText and IsReadOnly.
TSharedPtr<FJsonValue> FPCGHandlers::GetPCGCustomHLSL(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	FString NodeName;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("nodeName"), NodeName)) return Err;
	UPCGGraph* Graph = LoadAssetByPath<UPCGGraph>(AssetPath);
	if (!Graph) return MCPAssetLoadError(AssetPath, TEXT("PCGGraph"));
	TSharedPtr<FJsonValue> Error;
	UPCGSettings* Settings = MCPHlslResolveSettings(Graph, NodeName, Error);
	if (!Settings) return Error;

	UObject* Override = MCPHlslKernelOverride(Settings);
	const FString Declarations = MCPHlslReadString(Settings, TEXT("InputDeclarations")) + TEXT("\n\n")
		+ MCPHlslReadString(Settings, TEXT("OutputDeclarations")) + TEXT("\n\n")
		+ MCPHlslReadString(Settings, TEXT("HelperDeclarations"));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("nodeName"), NodeName);
	Result->SetStringField(TEXT("sourceText"), MCPHlslReadString(Settings, TEXT("ShaderSource")));
	Result->SetStringField(TEXT("functionsText"), MCPHlslReadString(Settings, TEXT("ShaderFunctions")));
	Result->SetStringField(TEXT("declarationsText"), Declarations);
	Result->SetBoolField(TEXT("readOnly"), Override != nullptr);
	if (Override) Result->SetStringField(TEXT("kernelSourceOverride"), Override->GetPathName());
	return MCPResult(Result);
}


// pcg(set_custom_hlsl): SetSourceText and SetFunctionsText.
TSharedPtr<FJsonValue> FPCGHandlers::SetPCGCustomHLSL(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	FString NodeName;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("nodeName"), NodeName)) return Err;
	const bool bHasSource = HasParam(Params, TEXT("sourceText"));
	const bool bHasFunctions = HasParam(Params, TEXT("functionsText"));
	const FString SourceText = OptionalString(Params, TEXT("sourceText"));
	const FString FunctionsText = OptionalString(Params, TEXT("functionsText"));
	if (!bHasSource && !bHasFunctions) return MCPError(TEXT("Pass sourceText, functionsText, or both."));

	UPCGGraph* Graph = LoadAssetByPath<UPCGGraph>(AssetPath);
	if (!Graph) return MCPAssetLoadError(AssetPath, TEXT("PCGGraph"));
	TSharedPtr<FJsonValue> Error;
	UPCGSettings* Settings = MCPHlslResolveSettings(Graph, NodeName, Error);
	if (!Settings) return Error;
	if (UObject* Override = MCPHlslKernelOverride(Settings))
	{
		return MCPError(FString::Printf(
			TEXT("Node '%s' is read-only: its Kernel Source Override (%s) supplies the kernel. Clear KernelSourceOverride with set_node_settings to author the kernel inline."),
			*NodeName, *Override->GetPathName()));
	}

	const FString PreviousSource = MCPHlslReadString(Settings, TEXT("ShaderSource"));
	const FString PreviousFunctions = MCPHlslReadString(Settings, TEXT("ShaderFunctions"));
	const bool bSourceChanges = bHasSource && SourceText != PreviousSource;
	const bool bFunctionsChange = bHasFunctions && FunctionsText != PreviousFunctions;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("nodeName"), NodeName);
	if (!bSourceChanges && !bFunctionsChange)
	{
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetStringField(TEXT("sourceText"), PreviousSource);
		Result->SetStringField(TEXT("functionsText"), PreviousFunctions);
		MCPSetNoRollback(Result, TEXT("The node already held this text, so nothing changed."));
		return MCPResult(Result);
	}

	if (auto Blocked = MCPAssetWriteBlockedError(Graph, AssetPath, TEXT("edit a Custom HLSL node in this PCG graph"))) return Blocked;
	{
		FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "SetPCGCustomHLSL", "Set Custom HLSL Text"));
		Graph->Modify();
		FString WriteError;
		if (bSourceChanges && !MCPHlslWriteText(Settings, TEXT("ShaderSource"), TEXT("BP_SetSourceText"), SourceText, WriteError))
		{
			Transaction.Cancel();
			return MCPError(WriteError);
		}
		if (bFunctionsChange && !MCPHlslWriteText(Settings, TEXT("ShaderFunctions"), TEXT("BP_SetFunctionsText"), FunctionsText, WriteError))
		{
			Transaction.Cancel();
			return MCPError(WriteError);
		}
	}
	Graph->PostEditChange();
	if (UPackage* Package = Graph->GetOutermost()) Package->MarkPackageDirty();
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(Graph, SaveError);

	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("sourceText"), MCPHlslReadString(Settings, TEXT("ShaderSource")));
	Result->SetStringField(TEXT("functionsText"), MCPHlslReadString(Settings, TEXT("ShaderFunctions")));
	// The source declares the pins' accessors, so report the pins as they now stand.
	if (const UPCGNode* Node = MCPPCG::FindNode(Graph, NodeName)) MCPPCG::SetPinLabelFields(Node, Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("nodeName"), NodeName);
	if (bSourceChanges) Payload->SetStringField(TEXT("sourceText"), PreviousSource);
	if (bFunctionsChange) Payload->SetStringField(TEXT("functionsText"), PreviousFunctions);
	MCPSetRollback(Result, TEXT("set_pcg_custom_hlsl"), Payload);
	MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveError);
	return MCPResult(Result);
}


// pcg(validate_edge): UPCGGraph::ValidateEdge.
TSharedPtr<FJsonValue> FPCGHandlers::ValidatePCGEdge(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	FString SourceNodeName;
	FString TargetNodeName;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("sourceNode"), SourceNodeName)) return Err;
	if (auto Err = RequireString(Params, TEXT("targetNode"), TargetNodeName)) return Err;
	const FString SourcePin = OptionalString(Params, TEXT("sourcePin"));
	const FString TargetPin = OptionalString(Params, TEXT("targetPin"));

	UPCGGraph* Graph = LoadAssetByPath<UPCGGraph>(AssetPath);
	if (!Graph) return MCPAssetLoadError(AssetPath, TEXT("PCGGraph"));
	UPCGNode* SourceNode = MCPPCG::FindNode(Graph, SourceNodeName);
	if (!SourceNode) return MCPPCG::NodeNotFoundError(Graph, SourceNodeName, TEXT("Source node"));
	UPCGNode* TargetNode = MCPPCG::FindNode(Graph, TargetNodeName);
	if (!TargetNode) return MCPPCG::NodeNotFoundError(Graph, TargetNodeName, TEXT("Target node"));

	// The same defaults connect_nodes takes: the first output and the first input pin.
	FName SourceLabel = SourcePin.IsEmpty() ? NAME_None : FName(*SourcePin);
	FName TargetLabel = TargetPin.IsEmpty() ? NAME_None : FName(*TargetPin);
	if (SourcePin.IsEmpty() && SourceNode->GetOutputPins().Num() > 0 && SourceNode->GetOutputPins()[0])
	{
		SourceLabel = SourceNode->GetOutputPins()[0]->Properties.Label;
	}
	if (TargetPin.IsEmpty() && TargetNode->GetInputPins().Num() > 0 && TargetNode->GetInputPins()[0])
	{
		TargetLabel = TargetNode->GetInputPins()[0]->Properties.Label;
	}

	const FMCPPCGEdgeCheck Check = MCPPCGValidateEdge(Graph, SourceNode, SourceLabel, TargetNode, TargetLabel);
	bool bConnected = false;
	for (const TObjectPtr<UPCGPin>& OutPin : SourceNode->GetOutputPins())
	{
		if (!OutPin || OutPin->Properties.Label != SourceLabel) continue;
		for (const TObjectPtr<UPCGEdge>& Edge : OutPin->Edges)
		{
			if (Edge && Edge->OutputPin && Edge->OutputPin->Node == TargetNode && Edge->OutputPin->Properties.Label == TargetLabel) bConnected = true;
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("sourceNodeName"), SourceNodeName);
	Result->SetStringField(TEXT("targetNodeName"), TargetNodeName);
	Result->SetStringField(TEXT("sourcePinLabel"), SourceLabel.ToString());
	Result->SetStringField(TEXT("targetPinLabel"), TargetLabel.ToString());
	Result->SetBoolField(TEXT("valid"), Check.IsSuccess());
	Result->SetStringField(TEXT("connectionResult"), Check.Result);
	if (!Check.Reason.IsEmpty()) Result->SetStringField(TEXT("reason"), Check.Reason);
	Result->SetBoolField(TEXT("alreadyConnected"), bConnected);
	return MCPResult(Result);
}


void FPCGHandlers::RegisterCustomHLSLHandlers(FMCPHandlerRegistry& Registry)
{
	using EType = EMCPParamType;
	const FMCPParamSpec GraphPathSpec = MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("PCGGraph asset path")).Alias(TEXT("path"));
	const FMCPParamSpec NodeNameSpec = MCPParam::Required(TEXT("nodeName"), EType::String, TEXT("Engine name of the Custom HLSL node, as read_graph reports it"));

	Registry.RegisterHandler(TEXT("get_pcg_custom_hlsl"), &GetPCGCustomHLSL, {
		GraphPathSpec,
		NodeNameSpec,
	});
	Registry.RegisterHandler(TEXT("set_pcg_custom_hlsl"), &SetPCGCustomHLSL, {
		GraphPathSpec,
		NodeNameSpec,
		MCPParam::Optional(TEXT("sourceText"), EType::String, TEXT("Kernel body (the Source pane of the HLSL Source Editor)")),
		MCPParam::Optional(TEXT("functionsText"), EType::String, TEXT("Helper functions the source can call (the Functions pane)")),
	}, MCPSpec::AtLeastOne({ { TEXT("sourceText") }, { TEXT("functionsText") } }));
	Registry.RegisterHandler(TEXT("validate_pcg_edge"), &ValidatePCGEdge, {
		GraphPathSpec,
		MCPParam::Required(TEXT("sourceNode"), EType::String, TEXT("Node the edge leaves")).Alias(TEXT("sourceNodeName")),
		MCPParam::Optional(TEXT("sourcePin"), EType::String, TEXT("Output pin label (default: the first output pin)")).Alias(TEXT("sourcePinLabel")),
		MCPParam::Required(TEXT("targetNode"), EType::String, TEXT("Node the edge enters")).Alias(TEXT("targetNodeName")),
		MCPParam::Optional(TEXT("targetPin"), EType::String, TEXT("Input pin label (default: the first input pin)")).Alias(TEXT("targetPinLabel")),
	});
}
