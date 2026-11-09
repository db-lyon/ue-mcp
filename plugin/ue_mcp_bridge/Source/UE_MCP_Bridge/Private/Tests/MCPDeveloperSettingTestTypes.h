#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"
#include "MCPDeveloperSettingTestTypes.generated.h"

/** A defaultconfig settings class with its own ini (DefaultUEMCPSettingTest.ini),
 *  so project(set_developer_setting) tests write a file nothing else reads. */
UCLASS(config = UEMCPSettingTest, defaultconfig)
class UUEMCPDeveloperSettingTestObject : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Config)
	int32 Count = 0;

	UPROPERTY(Config)
	TSoftClassPtr<UObject> RuntimeClass;

	UPROPERTY(Config)
	TArray<FString> Entries;

	UPROPERTY()
	int32 NotConfig = 0;
};
