// create_blueprint and create_blueprint_interface must write the new asset to
// disk and say so, like every other creator. Assets live in a private mount.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Blueprint/BlueprintHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPBlueprintCreateSavesTest,
	"UE.MCP.Blueprint.Create.SavesTheNewAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBlueprintCreateSavesTest::RunTest(const FString& Parameters)
{
	// Each creator first asks whether the asset exists, which logs a miss.
	AddExpectedError(TEXT("LoadAsset failed"), EAutomationExpectedErrorFlags::Contains, 0);
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPCreateBlueprint_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPCreateBlueprint"));

	FMCPHandlerRegistry Registry;
	FBlueprintHandlers::RegisterHandlers(Registry);

	for (const TCHAR* Method : { TEXT("create_blueprint"), TEXT("create_blueprint_interface") })
	{
		const FString PackageName = Mount.RootPath + (FString(Method).Contains(TEXT("interface")) ? TEXT("BPI_SaveProbe") : TEXT("BP_SaveProbe"));
		auto Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("assetPath"), PackageName);
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Args);
		const TSharedPtr<FJsonObject> Result = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s answered"), Method), Result.Get())) continue;

		bool bSuccess = false;
		Result->TryGetBoolField(TEXT("success"), bSuccess);
		if (!TestTrue(FString::Printf(TEXT("%s succeeds"), Method), bSuccess)) continue;
		bool bSaved = false;
		TestTrue(FString::Printf(TEXT("%s reports saved"), Method), Result->TryGetBoolField(TEXT("saved"), bSaved) && bSaved);

		FString FileName;
		TestTrue(TEXT("the package maps into the mount"),
			FPackageName::TryConvertLongPackageNameToFilename(PackageName, FileName, FPackageName::GetAssetPackageExtension()));
		TestTrue(FString::Printf(TEXT("%s wrote %s"), Method, *FileName), IFileManager::Get().FileExists(*FileName));
		if (UPackage* Package = FindPackage(nullptr, *PackageName))
		{
			TestFalse(FString::Printf(TEXT("%s left the package clean"), Method), Package->IsDirty());
			Package->SetDirtyFlag(false);
		}
	}
	return true;
}

#endif
