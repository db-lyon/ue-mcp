#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "MCPInstancedStructPathTestTypes.generated.h"

/** Reflected payload used only by the FInstancedStruct property-path tests. */
USTRUCT()
struct FUEMCPInstancedStructPathElement
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Scalar = 0;

	UPROPERTY()
	FString Sibling;
};

/** Payload shape with both scalar and indexed nested leaves. */
USTRUCT()
struct FUEMCPInstancedStructPathPayload
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Scalar = 0;

	UPROPERTY()
	FString Sibling;

	UPROPERTY()
	TArray<FUEMCPInstancedStructPathElement> Elements;
};

/** In-memory UObject target resolved by both property handlers through its object path. */
UCLASS()
class UUEMCPInstancedStructPathTestObject : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FInstancedStruct Payload;

	UPROPERTY()
	FInstancedStruct EmptyPayload;

	UPROPERTY()
	TArray<FInstancedStruct> OpStack;
};
