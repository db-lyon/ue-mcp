#pragma once

#include "CoreMinimal.h"
#include "MCPKeyedPathTestTypes.generated.h"

/** Enum key used only by the keyed property-path tests. */
UENUM()
enum class EUEMCPKeyedPathSlot : uint8
{
	Back,
	Hip,
	Chest
};

/** Nested struct under a keyed element: a small struct leaf and a map. */
USTRUCT()
struct FUEMCPKeyedPathPresentation
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform MeshOffset;

	UPROPERTY()
	TMap<EUEMCPKeyedPathSlot, FTransform> SlotOffsets;
};

/** Array element with one field of every key kind the selector compares, and
 *  one float it must refuse as a key. */
USTRUCT()
struct FUEMCPKeyedPathProfile
{
	GENERATED_BODY()

	UPROPERTY()
	FName ProfileId;

	UPROPERTY()
	FString Label;

	UPROPERTY()
	int32 Rank = 0;

	UPROPERTY()
	bool bEnabled = false;

	UPROPERTY()
	EUEMCPKeyedPathSlot Slot = EUEMCPKeyedPathSlot::Back;

	UPROPERTY()
	float Weight = 0.f;

	UPROPERTY()
	FUEMCPKeyedPathPresentation Presentation;
};

/** In-memory target the property handlers resolve through its object path. */
UCLASS()
class UUEMCPKeyedPathTestObject : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FUEMCPKeyedPathProfile> Profiles;

	UPROPERTY()
	TMap<FName, int32> Lookup;

	UPROPERTY()
	int32 Plain = 0;
};
