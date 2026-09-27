#pragma once

// Shared by the StateTree family tests: a fixture tree in a private mount, and
// a test base that does not fail on the compiler and validator logging the
// problems these tests provoke on purpose. Their outcome is asserted from the
// responses instead.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HandlerRegistry.h"
#include "Misc/AutomationTest.h"
#include "StateTree.h"
#include "UObject/Package.h"

class FMCPStateTreeTestBase : public FAutomationTestBase
{
public:
	FMCPStateTreeTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.Add(TEXT("LogStateTree"));
		Categories.Add(TEXT("LogStateTreeEditor"));
		return Categories;
	}
};

namespace UEMCPStateTreeTests
{
	/** A bare StateTree asset with no editor data, as asset(create_asset_by_class) leaves one. */
	inline UStateTree* MakeBareTree(const FString& PackageName)
	{
		UPackage* Package = CreatePackage(*PackageName);
		const FString AssetName = FPackageName::GetShortName(PackageName);
		return NewObject<UStateTree>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	}

	inline TSharedPtr<FJsonObject> Call(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : MakeShared<FJsonObject>();
	}

	inline bool Succeeded(const TSharedPtr<FJsonObject>& Result)
	{
		bool bSuccess = false;
		return Result.IsValid() && Result->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
