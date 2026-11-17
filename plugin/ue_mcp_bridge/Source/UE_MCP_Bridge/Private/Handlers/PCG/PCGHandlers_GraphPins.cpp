// pcg(set_graph_pins): add, update, replace or remove a graph's own input or output pins (#1318).
#include "PCGHandlers.h"
#include "PCGHandlers_Internal.h"
#include "HandlerUtils.h"
#include "HandlerPackageSave.h"
#include "PCGCommon.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "Runtime/Launch/Resources/Version.h"

namespace
{
	/** An EPCGDataType by enumerator name ("Point", "Param") or display name ("Attribute Set"); false when neither matches. */
	bool MCPGraphPinsParseType(const FString& Text, EPCGDataType& Out)
	{
		const UEnum* Enum = StaticEnum<EPCGDataType>();
		if (!Enum) return false;
		int64 Value = Enum->GetValueByNameString(Text);
		if (Value == INDEX_NONE)
		{
			for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
			{
				if (Enum->GetDisplayNameTextByIndex(i).ToString().Equals(Text, ESearchCase::IgnoreCase)
					|| Enum->GetNameStringByIndex(i).Equals(Text, ESearchCase::IgnoreCase))
				{
					Value = Enum->GetValueByIndex(i);
					break;
				}
			}
		}
		if (Value == INDEX_NONE) return false;
		Out = static_cast<EPCGDataType>(Value);
		return true;
	}

	/** A pin with the label, type and flags given. */
	FPCGPinProperties MCPGraphPinsMake(const FName& Label, EPCGDataType Type, bool bMultipleConnections, bool bMultipleData, const FText& Tooltip)
	{
#if UE_MCP_HAS_5_7_API
		return FPCGPinProperties(Label, FPCGDataTypeIdentifier{Type}, bMultipleConnections, bMultipleData, Tooltip);
#else
		return FPCGPinProperties(Label, Type, bMultipleConnections, bMultipleData, Tooltip);
#endif
	}

	/** Each pin as {label, type, tooltip, allowMultipleData, allowMultipleConnections}; type is the property's export text. */
	TArray<TSharedPtr<FJsonValue>> MCPGraphPinsDescribe(const FArrayProperty* PinsProp, const TArray<FPCGPinProperties>& Pins)
	{
		const FStructProperty* Inner = PinsProp ? CastField<FStructProperty>(PinsProp->Inner) : nullptr;
		const FProperty* TypeProp = Inner ? Inner->Struct->FindPropertyByName(TEXT("AllowedTypes")) : nullptr;
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FPCGPinProperties& Pin : Pins)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("label"), Pin.Label.ToString());
			if (TypeProp)
			{
				FString TypeText;
				TypeProp->ExportTextItem_Direct(TypeText, TypeProp->ContainerPtrToValuePtr<void>(&Pin), nullptr, nullptr, PPF_None);
				Row->SetStringField(TEXT("type"), TypeText);
			}
			Row->SetStringField(TEXT("tooltip"), Pin.Tooltip.ToString());
			Row->SetBoolField(TEXT("allowMultipleData"), Pin.bAllowMultipleData);
			Row->SetBoolField(TEXT("allowMultipleConnections"), Pin.AllowsMultipleConnections());
			Out.Add(MakeShared<FJsonValueObject>(Row));
		}
		return Out;
	}
}

TSharedPtr<FJsonValue> FPCGHandlers::SetGraphPins(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString Which;
	if (auto Err = RequireString(Params, TEXT("side"), Which)) return Err;
	const TArray<TSharedPtr<FJsonValue>>* PinsArr = nullptr;
	const bool bHasPins = TryGetArrayParam(Params, TEXT("pins"), PinsArr) && PinsArr;
	const FString Mode = OptionalString(Params, TEXT("mode"), TEXT("append")).ToLower();

	const bool bInput = Which.Equals(TEXT("input"), ESearchCase::IgnoreCase);
	if (!bInput && !Which.Equals(TEXT("output"), ESearchCase::IgnoreCase))
	{
		return MCPError(FString::Printf(TEXT("side must be 'input' or 'output', not '%s'"), *Which));
	}
	if (Mode != TEXT("append") && Mode != TEXT("replace") && Mode != TEXT("remove"))
	{
		return MCPError(FString::Printf(TEXT("mode must be append, replace or remove, not '%s'"), *Mode));
	}
	if (!bHasPins || (PinsArr->Num() == 0 && Mode != TEXT("replace")))
	{
		return MCPError(TEXT("Missing 'pins': [{label, type?, tooltip?, allowMultipleData?, allowMultipleConnections?}]"));
	}

	UPCGGraph* Graph = LoadAssetByPath<UPCGGraph>(AssetPath);
	if (!Graph) return MCPAssetLoadError(AssetPath, TEXT("PCGGraph"));
	UPCGNode* Node = bInput ? Graph->GetInputNode() : Graph->GetOutputNode();
	UPCGSettings* Settings = Node ? const_cast<UPCGSettings*>(Node->GetSettings()) : nullptr;
	FArrayProperty* PinsProp = Settings ? CastField<FArrayProperty>(Settings->GetClass()->FindPropertyByName(TEXT("Pins"))) : nullptr;
	const FStructProperty* PinStruct = PinsProp ? CastField<FStructProperty>(PinsProp->Inner) : nullptr;
	if (!PinsProp || !PinStruct || PinStruct->Struct->GetFName() != FName(TEXT("PCGPinProperties")))
	{
		return MCPError(FString::Printf(TEXT("The graph's %s node has no Pins array to edit."), bInput ? TEXT("input") : TEXT("output")));
	}
	TArray<FPCGPinProperties>& Pins = *PinsProp->ContainerPtrToValuePtr<TArray<FPCGPinProperties>>(Settings);

	// Build the new list on a copy, so a bad entry refuses the call with nothing changed.
	TArray<FPCGPinProperties> Next = Mode == TEXT("replace") ? TArray<FPCGPinProperties>() : Pins;
	TArray<FString> Problems;
	TSet<FName> Seen;
	for (const TSharedPtr<FJsonValue>& Value : *PinsArr)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		FString Label;
		if (!Value.IsValid() || !Value->TryGetObject(Obj) || !Obj || !(*Obj)->TryGetStringField(TEXT("label"), Label) || Label.IsEmpty())
		{
			Problems.Add(TEXT("every pin needs a label"));
			continue;
		}
		const FName LabelName(*Label);
		if (Seen.Contains(LabelName))
		{
			Problems.Add(FString::Printf(TEXT("'%s' is given twice"), *Label));
			continue;
		}
		Seen.Add(LabelName);
		const FPCGPinProperties* Existing = Pins.FindByPredicate([&](const FPCGPinProperties& P) { return P.Label == LabelName; });

		if (Mode == TEXT("remove"))
		{
			if (!Existing) Problems.Add(FString::Printf(TEXT("no pin is labelled '%s'"), *Label));
			else Next.RemoveAll([&](const FPCGPinProperties& P) { return P.Label == LabelName; });
			continue;
		}

		// Unstated fields keep the existing pin's values, so a relabel-free update preserves its edges.
		EPCGDataType Type = EPCGDataType::Any;
		FString TypeText;
		const bool bHasType = (*Obj)->TryGetStringField(TEXT("type"), TypeText) && !TypeText.IsEmpty();
		if (bHasType && !MCPGraphPinsParseType(TypeText, Type))
		{
			Problems.Add(FString::Printf(TEXT("'%s': unknown type '%s' (Any, Point, Spatial, Param, PolyLine, Surface, Volume, Primitive, Landscape, Texture...)"), *Label, *TypeText));
			continue;
		}
		FPCGPinProperties Pin = Existing ? *Existing : MCPGraphPinsMake(LabelName, EPCGDataType::Any, true, true, FText::GetEmpty());
		bool bMultipleData = Pin.bAllowMultipleData;
		bool bMultipleConnections = Pin.AllowsMultipleConnections();
		(*Obj)->TryGetBoolField(TEXT("allowMultipleData"), bMultipleData);
		(*Obj)->TryGetBoolField(TEXT("allowMultipleConnections"), bMultipleConnections);
		FString Tooltip = Pin.Tooltip.ToString();
		(*Obj)->TryGetStringField(TEXT("tooltip"), Tooltip);
		if (bHasType || !Existing || bMultipleData != Pin.bAllowMultipleData || bMultipleConnections != Pin.AllowsMultipleConnections())
		{
			FPCGPinProperties Rebuilt = MCPGraphPinsMake(LabelName, bHasType ? Type : EPCGDataType::Any, bMultipleConnections, bMultipleData, FText::FromString(Tooltip));
			if (!bHasType && Existing) Rebuilt.AllowedTypes = Existing->AllowedTypes;
			Pin = Rebuilt;
		}
		Pin.Tooltip = FText::FromString(Tooltip);

		if (Mode == TEXT("replace"))
		{
			Next.Add(Pin);
		}
		else if (FPCGPinProperties* InPlace = Next.FindByPredicate([&](const FPCGPinProperties& P) { return P.Label == LabelName; }))
		{
			*InPlace = Pin;
		}
		else
		{
			Next.Add(Pin);
		}
	}
	if (Problems.Num() > 0)
	{
		return MCPError(FString::Printf(TEXT("Nothing was changed: %s."), *FString::Join(Problems, TEXT("; "))));
	}

	if (auto Blocked = MCPAssetWriteBlockedError(Graph, AssetPath, TEXT("edit this PCG graph's pins"))) return Blocked;

	FString PreviousText;
	PinsProp->ExportTextItem_Direct(PreviousText, &Pins, nullptr, Settings, PPF_None);
	const TArray<MCPPCG::FEdgeKey> EdgesBefore = MCPPCG::CollectNodeEdges(Node);

	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "SetPCGGraphPins", "Set PCG Graph Pins"));
	Graph->Modify();
	Settings->Modify();
	Pins = MoveTemp(Next);
	MCPPCG::NotifySettingsChanged(Settings, { TEXT("Pins") });
	Graph->PostEditChange();
	Graph->MarkPackageDirty();
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(Graph, SaveError);

	const TArray<TSharedPtr<FJsonValue>> Dropped = MCPPCG::DroppedEdges(EdgesBefore, MCPPCG::CollectNodeEdges(Node));
	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("nodeName"), Node->GetName());
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetArrayField(TEXT("pins"), MCPGraphPinsDescribe(PinsProp, Pins));
	Result->SetArrayField(bInput ? TEXT("outputPins") : TEXT("inputPins"),
		MCPPCG::PinLabelsJson(bInput ? Node->GetOutputPins() : Node->GetInputPins()));
	Result->SetArrayField(TEXT("removedEdges"), Dropped);

	// The exact previous Pins as export text, through the generic settings write.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("nodeName"), Node->GetName());
	Payload->SetStringField(TEXT("propertyName"), TEXT("Pins"));
	Payload->SetStringField(TEXT("propertyValue"), PreviousText);
	MCPSetRollback(Result, TEXT("set_pcg_node_settings"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), Dropped.Num() > 0);
	if (Dropped.Num() > 0)
	{
		Result->SetStringField(TEXT("rollbackNote"), TEXT("The rollback restores the previous pins, not the edges in removedEdges; replay those with connect_nodes."));
	}
	MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveError);
	return MCPResult(Result);
}
