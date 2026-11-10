#pragma once

// Whether a direct PCG edge can be added, and why not (#1828).
//
// UE 5.9 added UPCGGraph::ValidateEdge, which checks node ownership, the pin
// labels, cycles and the pin types without touching the graph, and
// UPCGGraph::TryAddEdge, which adds the edge or reports the same reason. On
// older engines the same checks are made here from the pieces those engines
// have: the pins by label, an upstream walk for the cycle, and the pin type
// compatibility the PCG editor's schema asks for (GetCompatibilityWithOtherPin
// from 5.7, IsCompatible before it). The result names match
// EPCGGraphConnectionResult on every engine.

#include "CoreMinimal.h"
#include "HandlerEngineVersion.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGEdge.h"
#if UE_MCP_HAS_5_7_API && !UE_MCP_HAS_5_9_API
#include "Data/Registry/PCGDataTypeCommon.h"
#endif
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/** The outcome of an edge check: an EPCGGraphConnectionResult name, and the reason when it is not Success. */
struct FMCPPCGEdgeCheck
{
	FString Result;
	FString Reason;
	bool IsSuccess() const { return Result == TEXT("Success"); }
};

/** UPCGGraph::ValidateEdge on 5.9, and the same checks by hand before it. */
inline FMCPPCGEdgeCheck MCPPCGValidateEdge(const UPCGGraph* Graph, const UPCGNode* From, FName FromLabel, const UPCGNode* To, FName ToLabel)
{
	FMCPPCGEdgeCheck Out;
#if UE_MCP_HAS_5_9_API
	FText Reason;
	const EPCGGraphConnectionResult Result = Graph->ValidateEdge(From, FromLabel, To, ToLabel, Reason);
	Out.Result = StaticEnum<EPCGGraphConnectionResult>()->GetNameStringByValue(static_cast<int64>(Result));
	Out.Reason = Reason.ToString();
#else
	if (!From || !To)
	{
		Out.Result = TEXT("InvalidNode");
		Out.Reason = TEXT("Both connection nodes must be valid.");
		return Out;
	}
	if (From == To)
	{
		Out.Result = TEXT("InvalidNode");
		Out.Reason = TEXT("Both pins are on the same node.");
		return Out;
	}
	auto FindPin = [](const TArray<TObjectPtr<UPCGPin>>& Pins, FName Label) -> const UPCGPin*
	{
		for (const TObjectPtr<UPCGPin>& Pin : Pins)
		{
			if (Pin && Pin->Properties.Label == Label) return Pin;
		}
		return nullptr;
	};
	const UPCGPin* FromPin = FindPin(From->GetOutputPins(), FromLabel);
	if (!FromPin)
	{
		Out.Result = TEXT("InvalidPin");
		Out.Reason = FString::Printf(TEXT("Node '%s' has no output pin named '%s'."), *From->GetName(), *FromLabel.ToString());
		return Out;
	}
	const UPCGPin* ToPin = FindPin(To->GetInputPins(), ToLabel);
	if (!ToPin)
	{
		Out.Result = TEXT("InvalidPin");
		Out.Reason = FString::Printf(TEXT("Node '%s' has no input pin named '%s'."), *To->GetName(), *ToLabel.ToString());
		return Out;
	}

	// A cycle: To is already upstream of From.
	TSet<const UPCGNode*> Visited;
	TArray<const UPCGNode*> ToVisit{ From };
	while (ToVisit.Num() > 0)
	{
		const UPCGNode* Node = ToVisit.Pop();
		if (Node == To)
		{
			Out.Result = TEXT("CycleDetected");
			Out.Reason = TEXT("Connection would create a cycle.");
			return Out;
		}
		if (Visited.Contains(Node)) continue;
		Visited.Add(Node);
		for (const TObjectPtr<UPCGPin>& InputPin : Node->GetInputPins())
		{
			if (!InputPin) continue;
			for (const TObjectPtr<UPCGEdge>& Edge : InputPin->Edges)
			{
				const UPCGPin* Upstream = Edge ? (Edge->InputPin == InputPin ? Edge->OutputPin.Get() : Edge->InputPin.Get()) : nullptr;
				if (Upstream && Upstream->Node) ToVisit.Add(Upstream->Node);
			}
		}
	}

#if UE_MCP_HAS_5_7_API
	FText TypeReason;
	switch (FromPin->GetCompatibilityWithOtherPin(ToPin, &TypeReason))
	{
	case EPCGDataTypeCompatibilityResult::Compatible:
		Out.Result = TEXT("Success");
		return Out;
	case EPCGDataTypeCompatibilityResult::RequireFilter:
		Out.Result = TEXT("RequiresFilter");
		Out.Reason = TypeReason.IsEmpty() ? FString(TEXT("Insert a type filter before connecting these pins.")) : TypeReason.ToString();
		return Out;
	case EPCGDataTypeCompatibilityResult::RequireConversion:
		Out.Result = TEXT("RequiresConversion");
		Out.Reason = TypeReason.IsEmpty() ? FString(TEXT("Insert a conversion node before connecting these pins.")) : TypeReason.ToString();
		return Out;
	default:
		Out.Result = TEXT("IncompatibleTypes");
		Out.Reason = TypeReason.IsEmpty() ? FString(TEXT("Pins have incompatible types.")) : TypeReason.ToString();
		return Out;
	}
#else
	if (!FromPin->IsCompatible(ToPin))
	{
		Out.Result = TEXT("IncompatibleTypes");
		Out.Reason = TEXT("Pins have incompatible types.");
		return Out;
	}
	Out.Result = TEXT("Success");
#endif
#endif
	return Out;
}

/** The refusal connect_nodes returns when the edge cannot be added: the reason, and the result name as connectionResult. */
inline TSharedPtr<FJsonValue> MCPPCGEdgeRefusal(const FMCPPCGEdgeCheck& Check, const FString& Source, FName SourcePin, const FString& Target, FName TargetPin)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), FString::Printf(TEXT("Cannot connect '%s.%s' -> '%s.%s': %s"),
		*Source, *SourcePin.ToString(), *Target, *TargetPin.ToString(),
		Check.Reason.IsEmpty() ? TEXT("the graph refused the edge.") : *Check.Reason));
	Obj->SetStringField(TEXT("connectionResult"), Check.Result);
	Obj->SetStringField(TEXT("connectionReason"), Check.Reason);
	Obj->SetStringField(TEXT("sourcePinLabel"), SourcePin.ToString());
	Obj->SetStringField(TEXT("targetPinLabel"), TargetPin.ToString());
	return MakeShared<FJsonValueObject>(Obj);
}
