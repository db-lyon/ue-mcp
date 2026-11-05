#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Tests/MCPNestedArrayPathTestTypes.h"
#include "MCPCollectionEditTestTypes.generated.h"

USTRUCT()
struct FUEMCPCollectionEditRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Count = 0;

	UPROPERTY()
	FUEMCPNestedArrayPathPresentation Presentation;
};

UCLASS()
class UUEMCPCollectionEditTestAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FUEMCPNestedArrayPathProfile> Profiles;

	UPROPERTY()
	TMap<FName, FUEMCPNestedArrayPathPresentation> ProfileMap;

	UFUNCTION(BlueprintCallable, Category="Tests")
	static FString DescribeEntry(FName EntryId, int32 Count)
	{
		return FString::Printf(TEXT("%s:%d"), *EntryId.ToString(), Count);
	}
};
