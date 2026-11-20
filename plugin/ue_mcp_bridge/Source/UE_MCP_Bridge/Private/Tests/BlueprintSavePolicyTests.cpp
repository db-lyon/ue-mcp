// #1910: the Blueprint save policy. A write saves its asset by default; with
// save=false it marks the package dirty, writes nothing, needs no writable file
// and says so on the result. Assets live in a private temp mount.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Blueprint/BlueprintHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

namespace MCPBlueprintSavePolicyTests
{
TSharedPtr<FJsonObject> Call(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
{
	const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
	return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
}

bool BoolField(const TSharedPtr<FJsonObject>& Result, const TCHAR* Field, bool bDefault = false)
{
	bool bValue = bDefault;
	if (Result.IsValid()) Result->TryGetBoolField(Field, bValue);
	return bValue;
}

TSharedPtr<FJsonObject> AddVariableParams(const FString& PackageName, const TCHAR* Name, TOptional<bool> Save)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("assetPath"), PackageName);
	Params->SetStringField(TEXT("name"), Name);
	Params->SetStringField(TEXT("varType"), TEXT("int"));
	if (Save.IsSet()) Params->SetBoolField(TEXT("save"), Save.GetValue());
	return Params;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPBlueprintSavePolicyDeferTest,
	"UE.MCP.Blueprint.SavePolicy.SaveFalseLeavesPackageDirty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBlueprintSavePolicyDeferTest::RunTest(const FString& Parameters)
{
	using namespace MCPBlueprintSavePolicyTests;
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPSavePolicy_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPSavePolicy"));
	const FString PackageName = Mount.RootPath + TEXT("BP_SavePolicy");

	UPackage* Package = CreatePackage(*PackageName);
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(), Package, FName(TEXT("BP_SavePolicy")), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("Blueprint fixture created"), Blueprint)) return false;
	FString SaveReason;
	if (!TestTrue(TEXT("fixture saves: ") + SaveReason, SaveAssetPackageChecked(Blueprint, SaveReason))) return false;

	FString FileName;
	if (!TestTrue(TEXT("fixture has a file"), FPackageName::TryConvertLongPackageNameToFilename(
		PackageName, FileName, FPackageName::GetAssetPackageExtension()))) return false;
	const int64 SizeBefore = IFileManager::Get().FileSize(*FileName);
	TestTrue(TEXT("fixture file is on disk"), SizeBefore > 0);

	FMCPHandlerRegistry Registry;
	FBlueprintHandlers::RegisterHandlers(Registry);

	// Every write action under the policy advertises save.
	for (const TPair<FString, FMCPHandlerSpec>& Entry : Registry.GetHandlerSpecs())
	{
		if (!Registry.FollowsSavePolicy(Entry.Key)) continue;
		const bool bDeclares = Entry.Value.Params.ContainsByPredicate(
			[](const FMCPParamSpec& Param) { return Param.Name == TEXT("save") && Param.Type == EMCPParamType::Boolean; });
		TestTrue(FString::Printf(TEXT("%s declares save"), *Entry.Key), bDeclares);
	}
	TestTrue(TEXT("add_variable follows the save policy"), Registry.FollowsSavePolicy(TEXT("add_variable")));
	TestFalse(TEXT("read_blueprint does not"), Registry.FollowsSavePolicy(TEXT("read_blueprint")));

	// The file is read-only: save=false must not need it to be writable, and
	// must not try to check it out.
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.SetReadOnly(*FileName, true);
	const FDateTime StampBefore = IFileManager::Get().GetTimeStamp(*FileName);

	const TSharedPtr<FJsonObject> Deferred = Call(Registry, TEXT("add_variable"), AddVariableParams(PackageName, TEXT("Deferred"), false));
	PlatformFile.SetReadOnly(*FileName, false);
	if (!TestNotNull(TEXT("add_variable answers"), Deferred.Get())) return false;
	TestTrue(TEXT("add_variable with save=false succeeds on a read-only file: ") + Deferred->GetStringField(TEXT("error")),
		BoolField(Deferred, TEXT("success")));
	TestFalse(TEXT("result reports saved=false"), BoolField(Deferred, TEXT("saved"), true));
	TestTrue(TEXT("result reports saveDeferred"), BoolField(Deferred, TEXT("saveDeferred")));
	const TArray<TSharedPtr<FJsonValue>>* Dirty = nullptr;
	const bool bListed = Deferred->TryGetArrayField(TEXT("dirtyPackages"), Dirty) && Dirty
		&& Dirty->ContainsByPredicate([&PackageName](const TSharedPtr<FJsonValue>& V) { return V.IsValid() && V->AsString() == PackageName; });
	TestTrue(TEXT("dirtyPackages names the Blueprint's package"), bListed);
	TestTrue(TEXT("the package is dirty"), Package->IsDirty());
	TestEqual(TEXT("the file was not rewritten"), IFileManager::Get().GetTimeStamp(*FileName), StampBefore);
	TestEqual(TEXT("the file size is unchanged"), IFileManager::Get().FileSize(*FileName), SizeBefore);
	TestTrue(TEXT("the variable exists in memory"),
		FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(TEXT("Deferred"))) != INDEX_NONE);

	// The default still saves, and the earlier in-memory edit goes with it.
	const TSharedPtr<FJsonObject> Saved = Call(Registry, TEXT("add_variable"), AddVariableParams(PackageName, TEXT("Saved"), {}));
	if (!TestNotNull(TEXT("add_variable answers"), Saved.Get())) return false;
	TestTrue(TEXT("add_variable with the default succeeds"), BoolField(Saved, TEXT("success")));
	TestTrue(TEXT("result reports saved=true"), BoolField(Saved, TEXT("saved")));
	TestFalse(TEXT("no saveDeferred on a saving call"), Saved->HasField(TEXT("saveDeferred")));
	TestFalse(TEXT("the package is clean after the save"), Package->IsDirty());
	TestTrue(TEXT("the file was rewritten"), IFileManager::Get().GetTimeStamp(*FileName) >= StampBefore
		&& IFileManager::Get().FileSize(*FileName) != SizeBefore);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
