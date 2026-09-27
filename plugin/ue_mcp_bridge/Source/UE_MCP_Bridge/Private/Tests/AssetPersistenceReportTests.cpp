// set_asset_property reports persistence through the shared rule the IMC
// actions use: a failed save carries saveError, and an edit Unreal did not mark
// dirty under save=false is a failure, since nothing will ever save it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Asset/AssetHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "UObject/PerPlatformProperties.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "UObject/GCObjectScopeGuard.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

namespace MCPAssetPersistenceReportTests
{
	TSharedPtr<FJsonObject> Call(FMCPHandlerRegistry& Registry, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(TEXT("set_asset_property"), Params);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : MakeShared<FJsonObject>();
	}

	bool Bool(const TSharedPtr<FJsonObject>& Result, const TCHAR* Key, bool bDefault)
	{
		bool bValue = bDefault;
		Result->TryGetBoolField(Key, bValue);
		return bValue;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAssetPersistenceReportTest,
	"UE.MCP.Asset.SetProperty.ReportsPersistenceLikeEveryEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetPersistenceReportTest::RunTest(const FString& Parameters)
{
	using namespace MCPAssetPersistenceReportTests;
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPPersistReport_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPPersistReport"));
	const FString PackageName = Mount.RootPath + TEXT("DT_ReportProbe");
	UPackage* Package = CreatePackage(*PackageName);
	UDataTable* Probe = NewObject<UDataTable>(Package, FName(TEXT("DT_ReportProbe")), RF_Public | RF_Standalone);
	Probe->RowStruct = FPerPlatformInt::StaticStruct();
	const FGCObjectScopeGuard KeepProbe(Probe);

	FString Reason;
	if (!TestTrue(TEXT("baseline saves"), SaveAssetPackageChecked(Probe, Reason))) return false;
	FString FileName;
	FPackageName::TryConvertLongPackageNameToFilename(PackageName, FileName, FPackageName::GetAssetPackageExtension());

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);

	auto Args = [&PackageName](const TCHAR* Value)
	{
		auto Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("assetPath"), PackageName);
		Out->SetStringField(TEXT("propertyName"), TEXT("ImportKeyField"));
		Out->SetStringField(TEXT("value"), Value);
		return Out;
	};

	// A save the file system refuses is reported the way every save is.
	{
		IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
		Files.SetReadOnly(*FileName, true);
		const TSharedPtr<FJsonObject> Result = Call(Registry, Args(TEXT("ReadOnlyKey")));
		Files.SetReadOnly(*FileName, false);
		TestFalse(TEXT("a refused save fails the write"), Bool(Result, TEXT("success"), true));
		TestFalse(TEXT("a refused save is not persisted"), Bool(Result, TEXT("persisted"), true));
		TestTrue(TEXT("a refused save names why in saveError"), Result->HasField(TEXT("saveError")));
	}
	if (!TestTrue(TEXT("baseline saves again"), SaveAssetPackageChecked(Probe, Reason))) return false;

	// save=false while Unreal suppresses dirty marking leaves an edit nothing
	// will discover, so it is refused rather than reported as deferred.
	{
		TGuardValue<bool> DuringPIE(GIsPlayInEditorWorld, true);
		auto Deferred = Args(TEXT("SuppressedKey"));
		Deferred->SetBoolField(TEXT("save"), false);
		const TSharedPtr<FJsonObject> Result = Call(Registry, Deferred);
		TestFalse(TEXT("an unmarked deferred edit fails"), Bool(Result, TEXT("success"), true));
		TestFalse(TEXT("an unmarked deferred edit is not persisted"), Bool(Result, TEXT("persisted"), true));
		TestFalse(TEXT("the package really was not marked dirty"), Bool(Result, TEXT("packageDirty"), true));
		FString PersistError;
		TestTrue(TEXT("the reason says Unreal did not mark it dirty"),
			Result->TryGetStringField(TEXT("persistError"), PersistError) && PersistError.Contains(TEXT("did not mark")));
	}

	Package->SetDirtyFlag(false);
	return true;
}

#endif
