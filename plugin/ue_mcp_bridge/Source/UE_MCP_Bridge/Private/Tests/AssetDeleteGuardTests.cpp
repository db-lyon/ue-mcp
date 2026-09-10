// Regression coverage for #976.
//
// asset(delete) and asset(delete_batch) both called
// UEditorAssetLibrary::DeleteAsset, whose own header says "This is a Force
// Delete. It doesn't check if the asset has references in other Levels or by
// Actors." They called it whatever the caller passed for `force`, so a
// referenced asset was destroyed and every package pointing at it was rewritten
// to point at nothing, while `force=false` read like a guard that did not
// exist.
//
// The fix asks the Asset Registry for referencers before the delete and refuses
// when it finds any, taking the force path only when the caller asked for it.
// What follows asserts both halves, plus that the protected-mount guardrail
// still stands in front of either path.
//
// Everything happens under a private mount point in the system temp area, for
// the same reason the property-persistence tests do it: run_automation_tests
// dispatches every EditorContext test against whatever project the bridge
// happens to be attached to, and a delete test must never be able to reach it.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/AssetHandlers.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "MCPEngineCompat.h"

namespace
{
	const TCHAR* const MCPDeleteGuardTestRoot = TEXT("/UEMCPAssetDeleteGuardTest/");

	/** Mount a private content root for the duration of the test and take it
	 *  back down again, so no delete here can reach a user's content. */
	struct FScopedDeleteGuardMount
	{
		FString RootPath;
		FString ContentPath;

		FScopedDeleteGuardMount()
			: RootPath(MCPDeleteGuardTestRoot)
			, ContentPath(FPaths::Combine(
				FPaths::ConvertRelativePathToFull(FString(FPlatformProcess::UserTempDir())),
				FString(TEXT("UEMCPAssetDeleteGuardTest")),
				FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
			IFileManager::Get().MakeDirectory(*ContentPath, /*Tree=*/true);
			FPackageName::RegisterMountPoint(RootPath, ContentPath);
		}

		~FScopedDeleteGuardMount()
		{
			FPackageName::UnRegisterMountPoint(RootPath, ContentPath);
			IFileManager::Get().DeleteDirectory(*ContentPath, /*RequireExists=*/false, /*Tree=*/true);
		}

		FScopedDeleteGuardMount(const FScopedDeleteGuardMount&) = delete;
		FScopedDeleteGuardMount& operator=(const FScopedDeleteGuardMount&) = delete;
	};

	TSharedPtr<FJsonObject> GuardResponseObject(const TSharedPtr<FJsonValue>& Response)
	{
		return (Response.IsValid() && Response->Type == EJson::Object)
			? Response->AsObject()
			: TSharedPtr<FJsonObject>();
	}

	bool GuardResponseBool(const TSharedPtr<FJsonValue>& Response, const TCHAR* Field, bool bDefault = false)
	{
		const TSharedPtr<FJsonObject> Obj = GuardResponseObject(Response);
		if (!Obj.IsValid()) return bDefault;
		bool bValue = bDefault;
		Obj->TryGetBoolField(Field, bValue);
		return bValue;
	}

	FString GuardResponseString(const TSharedPtr<FJsonValue>& Response, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject> Obj = GuardResponseObject(Response);
		if (!Obj.IsValid()) return FString();
		FString Value;
		Obj->TryGetStringField(Field, Value);
		return Value;
	}

	/** Create a DataTable asset in its own package and write it to disk.
	 *  Returns null when either step failed. */
	UDataTable* MakeSavedProbeTable(const FString& PackageName)
	{
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package) return nullptr;

		const FString AssetName = FPackageName::GetShortName(PackageName);
		UDataTable* Table = NewObject<UDataTable>(
			Package, FName(*AssetName), RF_Public | RF_Standalone);
		if (!Table)
		{
			Package->SetDirtyFlag(false);
			return nullptr;
		}
		Table->RowStruct = FPerPlatformInt::StaticStruct();

		if (!SaveAssetPackage(Table))
		{
			Package->SetDirtyFlag(false);
			return nullptr;
		}
		return Table;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPDeleteAssetRefusesReferencedAssetTest,
	"UE.MCP.Asset.Delete.ForceFalseRefusesAReferencedAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPDeleteAssetRefusesReferencedAssetTest::RunTest(const FString& Parameters)
{
	const FScopedDeleteGuardMount Mount;

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	if (!TestTrue(TEXT("delete_asset is registered"), Registry.HasHandler(TEXT("delete_asset")))) return false;

	const FString TargetPackage = FString(MCPDeleteGuardTestRoot) + TEXT("DT_DeleteTarget");
	const FString ReferencerPackage = FString(MCPDeleteGuardTestRoot) + TEXT("R_DeleteReferencer");

	UDataTable* Target = MakeSavedProbeTable(TargetPackage);
	if (!TestNotNull(TEXT("the target asset was created and saved"), Target)) return false;
	const FGCRootScope KeepTargetAlive(Target);

	// A redirector is the cheapest asset that holds a hard reference to another
	// package's object, which is exactly the edge the guard has to see.
	UPackage* RefPackage = CreatePackage(*ReferencerPackage);
	if (!TestNotNull(TEXT("the referencer package was created"), RefPackage)) return false;
	UObjectRedirector* Referencer = NewObject<UObjectRedirector>(
		RefPackage, FName(TEXT("R_DeleteReferencer")), RF_Public | RF_Standalone);
	if (!TestNotNull(TEXT("the referencer asset was created"), Referencer))
	{
		RefPackage->SetDirtyFlag(false);
		return false;
	}
	Referencer->DestinationObject = Target;
	const FGCRootScope KeepReferencerAlive(Referencer);
	if (!TestTrue(TEXT("the referencer package reached disk"), SaveAssetPackage(Referencer)))
	{
		RefPackage->SetDirtyFlag(false);
		return false;
	}

	// The registry is the guard's only source, so the reference has to be
	// indexed before the refusal can be asserted. A rescan of the private mount
	// is what makes that happen inside one test run.
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FString> ScanRoots;
	ScanRoots.Add(FString(MCPDeleteGuardTestRoot).LeftChop(1));
	ARM.Get().ScanPathsSynchronous(ScanRoots, /*bForceRescan=*/true);

	TArray<FName> Referencers;
	ARM.Get().GetReferencers(FName(*TargetPackage), Referencers);
	const bool bIndexed = Referencers.Contains(FName(*ReferencerPackage));

	if (bIndexed)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), TargetPackage);

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("delete_asset"), Params);

		// The whole point of #976: the default is a refusal, not a delete.
		TestFalse(TEXT("a referenced asset is refused with force=false"),
			GuardResponseBool(Response, TEXT("success"), true));
		TestEqual(TEXT("the refusal names why"),
			GuardResponseString(Response, TEXT("reason")), FString(TEXT("has_referencers")));
		TestFalse(TEXT("the refusal reports deleted=false"),
			GuardResponseBool(Response, TEXT("deleted"), true));
		TestTrue(TEXT("the refusal names the referencing package"),
			GuardResponseString(Response, TEXT("error")).Contains(ReferencerPackage));
		TestTrue(TEXT("the asset the caller asked to delete is still there"),
			IsValid(Target));
	}
	else
	{
		// Nothing here can assert a guard whose only input is empty. Say so
		// rather than reporting a pass that tested nothing.
		AddInfo(TEXT("The Asset Registry did not index the private test mount's dependency graph in this ")
			TEXT("environment, so the referencer refusal could not be exercised. The unreferenced-delete ")
			TEXT("assertion below still ran."));
	}

	// An asset nothing points at still deletes on the default path: the guard
	// must not turn every delete into a refusal.
	{
		const FString LonePackage = FString(MCPDeleteGuardTestRoot) + TEXT("DT_DeleteLone");
		UDataTable* Lone = MakeSavedProbeTable(LonePackage);
		if (TestNotNull(TEXT("the unreferenced asset was created and saved"), Lone))
		{
			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("assetPath"), LonePackage);

			const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("delete_asset"), Params);
			TestTrue(FString::Printf(TEXT("an unreferenced asset deletes with force=false (%s)"),
				*GuardResponseString(Response, TEXT("error"))),
				GuardResponseBool(Response, TEXT("success")));
			TestFalse(TEXT("an unreferenced delete is not reported as forced"),
				GuardResponseBool(Response, TEXT("forced"), true));
		}
	}

	if (UPackage* TargetPkg = Target->GetOutermost()) TargetPkg->SetDirtyFlag(false);
	RefPackage->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPDeleteAssetProtectedMountTest,
	"UE.MCP.Asset.Delete.ProtectedMountIsRefusedOnBothPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPDeleteAssetProtectedMountTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	if (!TestTrue(TEXT("delete_asset is registered"), Registry.HasHandler(TEXT("delete_asset")))) return false;
	if (!TestTrue(TEXT("delete_asset_batch is registered"), Registry.HasHandler(TEXT("delete_asset_batch")))) return false;

	// force=true is the path that used to be the only path, so the guardrail
	// has to hold with it set, not just on the default.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), TEXT("/Engine/EngineMaterials/DefaultMaterial"));
		Params->SetBoolField(TEXT("force"), true);

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("delete_asset"), Params);
		TestFalse(TEXT("engine content is refused even with force=true"),
			GuardResponseBool(Response, TEXT("success"), true));
	}

	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Paths;
		Paths.Add(MakeShared<FJsonValueString>(TEXT("/Engine/EngineMaterials/DefaultMaterial")));
		Params->SetArrayField(TEXT("assetPaths"), Paths);
		Params->SetBoolField(TEXT("force"), true);

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("delete_asset_batch"), Params);
		const TSharedPtr<FJsonObject> Obj = GuardResponseObject(Response);
		if (TestTrue(TEXT("the batch answered with an object"), Obj.IsValid()))
		{
			double ProtectedCount = 0.0;
			Obj->TryGetNumberField(TEXT("protected"), ProtectedCount);
			TestEqual(TEXT("the engine path is counted as protected, not deleted"), ProtectedCount, 1.0);

			double DeletedCount = 1.0;
			Obj->TryGetNumberField(TEXT("deleted"), DeletedCount);
			TestEqual(TEXT("nothing on a protected mount was deleted"), DeletedCount, 0.0);
		}
	}

	return true;
}

#endif
