#pragma once

#include "CoreMinimal.h"
#include "MCPNestedArrayPathTestTypes.generated.h"

/** The struct nested inside each array element: an object reference and a transform. */
USTRUCT()
struct FUEMCPNestedArrayPathPresentation
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UObject> PrimaryUse = nullptr;

	UPROPERTY()
	FTransform CameraRelativeTransform = FTransform::Identity;
};

/** One array element, holding the nested struct. */
USTRUCT()
struct FUEMCPNestedArrayPathProfile
{
	GENERATED_BODY()

	UPROPERTY()
	FName Id;

	UPROPERTY()
	FUEMCPNestedArrayPathPresentation Presentation;
};

USTRUCT()
struct FUEMCPNestedArrayPathMapProfile
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FIntPoint, FTransform> Offsets;
};

/** Data-asset shape used only by the nested array path tests. */
UCLASS()
class UUEMCPNestedArrayPathTestAsset : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FUEMCPNestedArrayPathProfile> Profiles;

	UPROPERTY()
	TArray<FUEMCPNestedArrayPathMapProfile> MapProfiles;
};
