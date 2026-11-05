// Helpers shared by the PCG handler files. The module is a unity build, so a
// helper used by more than one PCGHandlers_*.cpp lives here, not as copies.
#pragma once

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGEdge.h"
#include "PCGSettings.h"
#include "HandlerJsonProperty.h"

namespace MCPPCG
{
	/** A node by engine name. UPCGGraph::GetNodes() leaves out the graph's input and output nodes, so they are checked too (#1324). */
	inline UPCGNode* FindNode(UPCGGraph* Graph, const FString& Name)
	{
		if (!Graph || Name.IsEmpty()) return nullptr;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node && Node->GetName() == Name) return Node;
		}
		if (UPCGNode* In = Graph->GetInputNode(); In && In->GetName() == Name) return In;
		if (UPCGNode* Out = Graph->GetOutputNode(); Out && Out->GetName() == Name) return Out;
		return nullptr;
	}

	/** Whether Node is the graph's input or output node, which the graph owns and no handler may remove. */
	inline bool IsGraphIONode(const UPCGGraph* Graph, const UPCGNode* Node)
	{
		return Graph && Node && (Node == Graph->GetInputNode() || Node == Graph->GetOutputNode());
	}

	/** " - did you mean 'DefaultInputNode'?" for the names callers guess for the input/output nodes (#239), else empty. */
	inline FString IONodeNameHint(const UPCGGraph* Graph, const FString& Name)
	{
		if (!Graph) return FString();
		auto Hint = [](const UPCGNode* Node)
		{
			return Node ? FString::Printf(TEXT(" - did you mean '%s'?"), *Node->GetName()) : FString();
		};
		for (const TCHAR* Guess : { TEXT("Input"), TEXT("InputNode"), TEXT("GraphInput") })
		{
			if (Name.Equals(Guess, ESearchCase::IgnoreCase)) return Hint(Graph->GetInputNode());
		}
		for (const TCHAR* Guess : { TEXT("Output"), TEXT("OutputNode"), TEXT("GraphOutput") })
		{
			if (Name.Equals(Guess, ESearchCase::IgnoreCase)) return Hint(Graph->GetOutputNode());
		}
		return FString();
	}

	/** "<Role> not found: <name>", with the input/output hint when the name is a guess at one. */
	inline TSharedPtr<FJsonValue> NodeNotFoundError(const UPCGGraph* Graph, const FString& Name, const TCHAR* Role = TEXT("Node"))
	{
		const FString Hint = IONodeNameHint(Graph, Name);
		return MCPError(FString::Printf(TEXT("%s not found: %s%s"), Role, *Name,
			Hint.IsEmpty() ? TEXT(". read_graph lists the node names.") : *Hint));
	}
	/**
	 * Delivers a change event per written top-level property. Settings that rebuild pins from a property
	 * (Load PCG Data Asset's Asset, an IO node's Pins) react only to an event naming it (#1256).
	 */
	inline void NotifySettingsChanged(UPCGSettings* Settings, const TArray<FString>& WrittenKeys)
	{
		TSet<FName> Notified;
		for (const FString& Key : WrittenKeys)
		{
			int32 Cut = INDEX_NONE;
			FString Top = Key;
			if (Top.FindChar(TEXT('.'), Cut)) Top.LeftInline(Cut);
			if (Top.FindChar(TEXT('['), Cut)) Top.LeftInline(Cut);
			FProperty* Prop = Settings->GetClass()->FindPropertyByName(FName(*Top));
			if (!Prop || Notified.Contains(Prop->GetFName())) continue;
			Notified.Add(Prop->GetFName());
			FPropertyChangedEvent Event(Prop, EPropertyChangeType::ValueSet);
			// Public on UObject, protected on UPCGSettings; the call still dispatches to the settings override.
			static_cast<UObject*>(Settings)->PostEditChangeProperty(Event);
		}
		if (Notified.Num() == 0) Settings->PostEditChange();
	}

	/** One edge touching a node, as "from.fromPin->to.toPin" plus its parts. */
	struct FEdgeKey
	{
		FString From, FromPin, To, ToPin;
		FString Key() const { return From + TEXT(".") + FromPin + TEXT("->") + To + TEXT(".") + ToPin; }
		TSharedPtr<FJsonValue> ToJson() const
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("from"), From);
			Obj->SetStringField(TEXT("fromPin"), FromPin);
			Obj->SetStringField(TEXT("to"), To);
			Obj->SetStringField(TEXT("toPin"), ToPin);
			return MakeShared<FJsonValueObject>(Obj);
		}
	};

	/** Every edge on Node's input and output pins. */
	inline TArray<FEdgeKey> CollectNodeEdges(const UPCGNode* Node)
	{
		TArray<FEdgeKey> Out;
		if (!Node) return Out;
		for (const TObjectPtr<UPCGPin>& Pin : Node->GetOutputPins())
		{
			if (!Pin) continue;
			for (const TObjectPtr<UPCGEdge>& Edge : Pin->Edges)
			{
				const UPCGPin* Other = Edge ? (Edge->InputPin == Pin ? Edge->OutputPin.Get() : Edge->InputPin.Get()) : nullptr;
				if (!Other || !Other->Node) continue;
				Out.Add({ Node->GetName(), Pin->Properties.Label.ToString(), Other->Node->GetName(), Other->Properties.Label.ToString() });
			}
		}
		for (const TObjectPtr<UPCGPin>& Pin : Node->GetInputPins())
		{
			if (!Pin) continue;
			for (const TObjectPtr<UPCGEdge>& Edge : Pin->Edges)
			{
				const UPCGPin* Other = Edge ? (Edge->OutputPin == Pin ? Edge->InputPin.Get() : Edge->OutputPin.Get()) : nullptr;
				if (!Other || !Other->Node) continue;
				Out.Add({ Other->Node->GetName(), Other->Properties.Label.ToString(), Node->GetName(), Pin->Properties.Label.ToString() });
			}
		}
		return Out;
	}

	/** The edges in Before that are not in After, as JSON {from, fromPin, to, toPin}. */
	inline TArray<TSharedPtr<FJsonValue>> DroppedEdges(const TArray<FEdgeKey>& Before, const TArray<FEdgeKey>& After)
	{
		TSet<FString> Still;
		for (const FEdgeKey& E : After) Still.Add(E.Key());
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FEdgeKey& E : Before)
		{
			if (!Still.Contains(E.Key())) Out.Add(E.ToJson());
		}
		return Out;
	}

	inline TArray<TSharedPtr<FJsonValue>> PinLabelsJson(const TArray<TObjectPtr<UPCGPin>>& Pins)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const UPCGPin* Pin : Pins)
		{
			if (Pin) Out.Add(MakeShared<FJsonValueString>(Pin->Properties.Label.ToString()));
		}
		return Out;
	}

	/** Sets inputPins and outputPins, each the node's pin labels, on Obj: the labels connect_nodes takes. */
	inline void SetPinLabelFields(const UPCGNode* Node, const TSharedPtr<FJsonObject>& Obj)
	{
		if (!Node) return;
		Obj->SetArrayField(TEXT("inputPins"), PinLabelsJson(Node->GetInputPins()));
		Obj->SetArrayField(TEXT("outputPins"), PinLabelsJson(Node->GetOutputPins()));
	}

	inline bool HasPinLabel(const TArray<TObjectPtr<UPCGPin>>& Pins, FName Label)
	{
		return Pins.ContainsByPredicate([Label](const TObjectPtr<UPCGPin>& Pin) { return Pin && Pin->Properties.Label == Label; });
	}

	/**
	 * The refusal for a pin label Node does not have on that side, listing the labels it does have, and saying so
	 * when the label is a pin on the other side.
	 */
	inline TSharedPtr<FJsonValue> PinNotFoundError(const UPCGNode* Node, FName Label, bool bOutput)
	{
		const TArray<TObjectPtr<UPCGPin>>& Pins = bOutput ? Node->GetOutputPins() : Node->GetInputPins();
		const TArray<TObjectPtr<UPCGPin>>& Other = bOutput ? Node->GetInputPins() : Node->GetOutputPins();
		TArray<FString> Labels;
		for (const UPCGPin* Pin : Pins) if (Pin) Labels.Add(Pin->Properties.Label.ToString());
		const TCHAR* Side = bOutput ? TEXT("output") : TEXT("input");
		FString Message = FString::Printf(TEXT("Node '%s' has no %s pin '%s'. Its %s pins: %s."), *Node->GetName(), Side, *Label.ToString(), Side,
			Labels.Num() > 0 ? *FString::Join(Labels, TEXT(", ")) : TEXT("none"));
		if (HasPinLabel(Other, Label))
		{
			Message += FString::Printf(TEXT(" '%s' is one of its %s pins."), *Label.ToString(), bOutput ? TEXT("input") : TEXT("output"));
		}
		TSharedPtr<FJsonObject> Err = MCPErrorObject(Message);
		Err->SetStringField(TEXT("reason"), TEXT("pin_not_found"));
		Err->SetStringField(TEXT("nodeName"), Node->GetName());
		Err->SetStringField(TEXT("pinSide"), Side);
		Err->SetArrayField(TEXT("validPins"), PinLabelsJson(Pins));
		return MakeShared<FJsonValueObject>(Err);
	}

	/** A user-parameter value as JSON: bool and numbers natively, anything else (enums included) as export text. */
	inline TSharedPtr<FJsonValue> ParamValueJson(const FProperty* Property, const void* ValueAddr)
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

	/** Each parameter of a bag: name, type, value. Instance, when given, adds whether it overrides the parameter. */
	inline TArray<TSharedPtr<FJsonValue>> DescribeParameterBag(const FInstancedPropertyBag* Bag, const UPCGGraphInstance* Instance = nullptr)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		const UPropertyBag* BagStruct = Bag ? Bag->GetPropertyBagStruct() : nullptr;
		const void* Memory = Bag ? Bag->GetValue().GetMemory() : nullptr;
		if (!BagStruct || !Memory) return Out;
		for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
		{
			if (!Desc.CachedProperty) continue;
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Desc.Name.ToString());
			Entry->SetStringField(TEXT("type"), Desc.CachedProperty->GetCPPType());
			Entry->SetField(TEXT("value"), ParamValueJson(Desc.CachedProperty, Desc.CachedProperty->ContainerPtrToValuePtr<void>(Memory)));
			if (Instance) Entry->SetBoolField(TEXT("overridden"), Instance->IsPropertyOverridden(Desc.CachedProperty));
			Out.Add(MakeShared<FJsonValueObject>(Entry));
		}
		return Out;
	}

	/**
	 * Sets Values ({name: value}, null clears an override) on a graph instance's parameter overrides. Every value is
	 * checked on a scratch copy first, so one bad entry refuses the call with nothing changed. ToModify are Modify()'d
	 * before the write; PreviousValues gets each touched override as it was (null when it was not overridden).
	 */
	inline bool ApplyInstanceOverrides(UPCGGraphInstance* Instance, const TSharedPtr<FJsonObject>& Values,
		const TArray<UObject*>& ToModify, TSharedPtr<FJsonObject>& PreviousValues, FString& Error)
	{
		// The mutable accessor is protected; the instance owns this bag, and UpdatePropertyOverride is its notification.
		FInstancedPropertyBag* Bag = Instance ? const_cast<FInstancedPropertyBag*>(Instance->GetUserParametersStruct()) : nullptr;
		const UPropertyBag* BagStruct = Bag ? Bag->GetPropertyBagStruct() : nullptr;
		if (!BagStruct)
		{
			Error = TEXT("The graph instance has no user parameters: its graph declares none, or it has no graph.");
			return false;
		}
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
				Problems.Add(FString::Printf(TEXT("%s is not a parameter of this graph"), *Name));
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
			Error = FString::Printf(TEXT("Nothing was changed: %s."), *FString::Join(Problems, TEXT("; ")));
			return false;
		}

		PreviousValues = MakeShared<FJsonObject>();
		for (UObject* Obj : ToModify) if (Obj) Obj->Modify();
		Instance->Modify();
		const void* Live = Bag->GetValue().GetMemory();
		for (const FWrite& W : Writes)
		{
			const FProperty* Prop = W.Desc->CachedProperty;
			PreviousValues->SetField(W.Desc->Name.ToString(), Instance->IsPropertyOverridden(Prop)
				? ParamValueJson(Prop, Prop->ContainerPtrToValuePtr<void>(Live))
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
