#pragma once

// Helpers shared between BlueprintHandlers.cpp and BlueprintHandlers_Graph.cpp
// after the file was split. Kept in Private/ because it is internal to the
// plugin - no downstream code is expected to include this.

#include "CoreMinimal.h"

class UBlueprint;
class UActorComponent;

// Resolve the named component template on a blueprint, honouring inheritance.
// See definition in BlueprintHandlers_Graph.cpp for the full contract (bForWrite
// semantics, ICH-override creation on write, CDO fallback on read, etc.).
UActorComponent* ResolveComponentTemplate(
	UBlueprint* Blueprint,
	const FString& ComponentName,
	bool bForWrite,
	bool& bOutIsInherited,
	TArray<FString>& OutAvailable);
