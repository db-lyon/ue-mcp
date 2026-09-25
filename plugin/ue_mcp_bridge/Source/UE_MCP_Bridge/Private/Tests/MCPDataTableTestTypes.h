#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Templates/SubclassOf.h"
#include "MCPDataTableTestTypes.generated.h"

/** Row struct used only by the DataTable reference tests: one field of each
 *  reference kind a row names by path, and one plain value beside them. */
USTRUCT()
struct FUEMCPDataTableReferenceRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY()
	TSoftClassPtr<UObject> SoftClass;

	UPROPERTY()
	TObjectPtr<UTexture2D> HardObject = nullptr;

	UPROPERTY()
	TSubclassOf<UObject> HardClass;

	UPROPERTY()
	int32 Count = 0;
};
