// Regression coverage for #931.
//
// asset(set_property) marked the package dirty and stopped there. The value
// read back correctly, stayed correct for the rest of the editor session, and
// was gone on the next start, because nothing ever wrote the package. A
// GameplayAbility whose default reverted that way computed correct values and
// applied no effect, with nothing pointing at the cause.
//
// UPackage::IsDirty is the engine's own record and is the direct check: after a
// write that persisted, the package is clean; after a write that did not, it is
// still dirty and the response says why.
//
// The test writes nowhere near the attached project. run_automation_tests
// dispatches every EditorContext/EngineFilter test in the process against
// whatever project the bridge happens to be attached to, so this registers its
// own mount point over a fresh directory in the system temp area, does the whole
// exercise there, then unmounts and deletes it.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/AssetHandlers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "MCPEngineCompat.h"

namespace
{
	const TCHAR* const MCPPersistTestRoot = TEXT("/UEMCPAssetPersistTest/");

	/** Mount a private content root for the duration of the test and take it
	 *  back down again, so no assertion here can touch a user's content. */
	struct FScopedPersistTestMount
	{
		FString RootPath;
		FString ContentPath;

		FScopedPersistTestMount()
			: RootPath(MCPPersistTestRoot)
			, ContentPath(FPaths::Combine(
				FPaths::ConvertRelativePathToFull(FString(FPlatformProcess::UserTempDir())),
				FString(TEXT("UEMCPAssetPersistTest")),
				FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
			IFileManager::Get().MakeDirectory(*ContentPath, /*Tree=*/true);
			FPackageName::RegisterMountPoint(RootPath, ContentPath);
		}

		~FScopedPersistTestMount()
		{
			FPackageName::UnRegisterMountPoint(RootPath, ContentPath);
			IFileManager::Get().DeleteDirectory(*ContentPath, /*RequireExists=*/false, /*Tree=*/true);
		}

		FScopedPersistTestMount(const FScopedPersistTestMount&) = delete;
		FScopedPersistTestMount& operator=(const FScopedPersistTestMount&) = delete;
	};

	TSharedPtr<FJsonObject> ResponseObject(const TSharedPtr<FJsonValue>& Response)
	{
		return (Response.IsValid() && Response->Type == EJson::Object)
			? Response->AsObject()
			: TSharedPtr<FJsonObject>();
	}

	bool ResponseBool(const TSharedPtr<FJsonValue>& Response, const TCHAR* Field, bool bDefault = false)
	{
		const TSharedPtr<FJsonObject> Obj = ResponseObject(Response);
		if (!Obj.IsValid()) return bDefault;
		bool bValue = bDefault;
		Obj->TryGetBoolField(Field, bValue);
		return bValue;
	}

	FString ResponseString(const TSharedPtr<FJsonValue>& Response, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject> Obj = ResponseObject(Response);
		if (!Obj.IsValid()) return FString();
		FString Value;
		Obj->TryGetStringField(Field, Value);
		return Value;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPSetAssetPropertyPersistsTest,
	"UE.MCP.Asset.SetProperty.WriteReachesThePackage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSetAssetPropertyPersistsTest::RunTest(const FString& Parameters)
{
	const FScopedPersistTestMount Mount;

	const FString PackageName = FString(MCPPersistTestRoot) + TEXT("DT_PersistProbe");
	UPackage* Package = CreatePackage(*PackageName);
	if (!TestNotNull(TEXT("probe package created"), Package)) return false;

	UDataTable* Probe = NewObject<UDataTable>(
		Package, FName(TEXT("DT_PersistProbe")), RF_Public | RF_Standalone);
	if (!TestNotNull(TEXT("probe asset created"), Probe))
	{
		Package->SetDirtyFlag(false);
		return false;
	}
	Probe->RowStruct = FPerPlatformInt::StaticStruct();
	const FGCRootScope KeepProbeAlive(Probe);

	FString PackageFileName;
	if (!TestTrue(TEXT("the probe package name maps into the test mount"),
		FPackageName::TryConvertLongPackageNameToFilename(
			PackageName, PackageFileName, FPackageName::GetAssetPackageExtension())))
	{
		Package->SetDirtyFlag(false);
		return false;
	}
	TestFalse(TEXT("no package file on disk before the write"),
		IFileManager::Get().FileExists(*PackageFileName));

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	if (!TestTrue(TEXT("set_asset_property is registered"), Registry.HasHandler(TEXT("set_asset_property"))))
	{
		Package->SetDirtyFlag(false);
		return false;
	}

	// The default write. This is the #931 assertion: it must reach disk without
	// the caller asking for a save.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), PackageName);
		Params->SetStringField(TEXT("propertyName"), TEXT("ImportKeyField"));
		Params->SetStringField(TEXT("value"), TEXT("PersistedKey"));

		const TSharedPtr<FJsonValue> Response =
			Registry.ExecuteHandler(TEXT("set_asset_property"), Params);

		TestTrue(FString::Printf(TEXT("the write succeeded (%s)"), *ResponseString(Response, TEXT("error"))),
			ResponseBool(Response, TEXT("success")));
		TestEqual(TEXT("the value landed on the asset"),
			Probe->ImportKeyField, FString(TEXT("PersistedKey")));

		// The two halves of the fix: the response says it persisted, and the
		// engine's own dirty record agrees.
		TestTrue(TEXT("the response reports the write as persisted"),
			ResponseBool(Response, TEXT("persisted")));
		TestEqual(TEXT("the response names the package it wrote"),
			ResponseString(Response, TEXT("packageName")), PackageName);
		TestFalse(TEXT("the package is no longer dirty after a persisted write"), Package->IsDirty());
		TestTrue(TEXT("the package file exists on disk after the write"),
			IFileManager::Get().FileExists(*PackageFileName));
	}

	// The opt-out. A write the caller asked not to save is still reported
	// honestly rather than as a plain success.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), PackageName);
		Params->SetStringField(TEXT("propertyName"), TEXT("ImportKeyField"));
		Params->SetStringField(TEXT("value"), TEXT("InMemoryOnly"));
		Params->SetBoolField(TEXT("save"), false);

		const TSharedPtr<FJsonValue> Response =
			Registry.ExecuteHandler(TEXT("set_asset_property"), Params);

		TestTrue(TEXT("an unsaved write still reports the write itself as done"),
			ResponseBool(Response, TEXT("success")));
		TestEqual(TEXT("the value landed on the asset in memory"),
			Probe->ImportKeyField, FString(TEXT("InMemoryOnly")));
		TestFalse(TEXT("an unsaved write reports persisted=false"),
			ResponseBool(Response, TEXT("persisted"), true));
		TestFalse(TEXT("an unsaved write names the reason"),
			ResponseString(Response, TEXT("persistError")).IsEmpty());
		TestTrue(TEXT("an unsaved write leaves the package dirty"), Package->IsDirty());
	}

	// Leave nothing dirty behind: a later editor(save_dirty) would otherwise
	// find this package and try to flush it through an unmounted root.
	Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPSetAssetPropertyProtectedMountTest,
	"UE.MCP.Asset.SetProperty.ProtectedMountIsNeverWritten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSetAssetPropertyProtectedMountTest::RunTest(const FString& Parameters)
{
	// The persistence step routes through the shared protected-mount guardrail,
	// so a write that lands on engine content is reported as unpersisted rather
	// than written. Asserting the guardrail directly keeps this test off any
	// engine package.
	TestTrue(TEXT("engine content is a protected mount"),
		MCPIsProtectedAssetPath(TEXT("/Engine/EngineMaterials/DefaultMaterial")));
	TestTrue(TEXT("script paths are a protected mount"),
		MCPIsProtectedAssetPath(TEXT("/Script/Engine")));
	TestFalse(TEXT("game content is not a protected mount"),
		MCPIsProtectedAssetPath(TEXT("/Game/MyFolder/DT_Thing")));
	return true;
}

#endif
