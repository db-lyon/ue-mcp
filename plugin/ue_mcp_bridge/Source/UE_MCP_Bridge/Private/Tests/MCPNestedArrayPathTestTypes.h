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

/** Data-asset shape used only by the nested array path tests: TArray<Struct{Struct{Object, Transform}}>. */
UCLASS()
class UUEMCPNestedArrayPathTestAsset : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FUEMCPNestedArrayPathProfile> Profiles;
};
