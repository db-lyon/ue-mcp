#pragma once

#include "CoreMinimal.h"

struct FEdGraphPinType;

// The one spelling of a pin type every handler reports: the vocabulary
// FBlueprintHandlers::ParsePinTypeSpec accepts back ("int", "double",
// "struct:/Script/CoreUObject.Vector", "object:/Script/Engine.Actor[]",
// "set<name>", "map<name,int>"). A category that vocabulary cannot write
// (exec, wildcard, delegate, interface, a non-K2 schema's own categories) is
// spelled category, then ":" and its object's path or "." and its subcategory,
// and bOutRoundTrips is false.
FString MCPPinTypeSpec(const FEdGraphPinType& PinType, bool& bOutRoundTrips);

inline FString MCPPinTypeSpec(const FEdGraphPinType& PinType)
{
	bool bRoundTrips = true;
	return MCPPinTypeSpec(PinType, bRoundTrips);
}
