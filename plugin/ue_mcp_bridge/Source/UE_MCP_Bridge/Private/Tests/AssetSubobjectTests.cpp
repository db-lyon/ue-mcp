// Coverage for asset(create_subobject) (#975).
//
// The two failures the issue is about are both about survival, not creation:
// a fresh object with nothing referencing it was collected inside roughly one
// bridge call, and a plugin class had to be nameable by /Script path because
// the Python `unreal` module does not expose it. Both are asserted here, along
// with the preflight that stops a bad property from creating a half-configured
// object.
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
	const TCHAR* const MCPSubobjectTestRoot = TEXT("/UEMCPSubobjectTest/");

	/** Mount a private content root for the duration of the test and take it
	 *  back down again, so no assertion here can touch a user's content. */
	struct FScopedSubobjectTestMount
	{
		FString RootPath;
		FString ContentPath;

		FScopedSubobjectTestMount()
			: RootPath(MCPSubobjectTestRoot)
			, ContentPath(FPaths::Combine(
				FPaths::ConvertRelativePathToFull(FString(FPlatformProcess::UserTempDir())),
				FString(TEXT("UEMCPSubobjectTest")),
				FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
			IFileManager::Get().MakeDirectory(*ContentPath, /*Tree=*/true);
			FPackageName::RegisterMountPoint(RootPath, ContentPath);
		}

		~FScopedSubobjectTestMount()
		{
			FPackageName::UnRegisterMountPoint(RootPath, ContentPath);
			IFileManager::Get().DeleteDirectory(*ContentPath, /*RequireExists=*/false, /*Tree=*/true);
		}

		FScopedSubobjectTestMount(const FScopedSubobjectTestMount&) = delete;
		FScopedSubobjectTestMount& operator=(const FScopedSubobjectTestMount&) = delete;
	};

	TSharedPtr<FJsonObject> SubobjectResponseObject(const TSharedPtr<FJsonValue>& Response)
	{
		return (Response.IsValid() && Response->Type == EJson::Object)
			? Response->AsObject()
			: TSharedPtr<FJsonObject>();
	}

	bool SubobjectResponseBool(const TSharedPtr<FJsonValue>& Response, const TCHAR* Field, bool bDefault = false)
	{
		const TSharedPtr<FJsonObject> Obj = SubobjectResponseObject(Response);
		if (!Obj.IsValid()) return bDefault;
		bool bValue = bDefault;
		Obj->TryGetBoolField(Field, bValue);
		return bValue;
	}

	FString SubobjectResponseString(const TSharedPtr<FJsonValue>& Response, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject> Obj = SubobjectResponseObject(Response);
		if (!Obj.IsValid()) return FString();
		FString Value;
		Obj->TryGetStringField(Field, Value);
		return Value;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPCreateSubobjectTest,
	"UE.MCP.Asset.CreateSubobject.SurvivesCollectionAndReachesThePackage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCreateSubobjectTest::RunTest(const FString& Parameters)
{
	const FScopedSubobjectTestMount Mount;

	const FString PackageName = FString(MCPSubobjectTestRoot) + TEXT("DA_SubobjectOwner");
	UPackage* Package = CreatePackage(*PackageName);
	if (!TestNotNull(TEXT("owner package created"), Package)) return false;

	UDataTable* Owner = NewObject<UDataTable>(
		Package, FName(TEXT("DA_SubobjectOwner")), RF_Public | RF_Standalone);
	if (!TestNotNull(TEXT("owner asset created"), Owner))
	{
		Package->SetDirtyFlag(false);
		return false;
	}
	Owner->RowStruct = FPerPlatformInt::StaticStruct();
	const FGCRootScope KeepOwnerAlive(Owner);

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	if (!TestTrue(TEXT("create_subobject is registered"), Registry.HasHandler(TEXT("create_subobject"))))
	{
		Package->SetDirtyFlag(false);
		return false;
	}

	// A component class named by its /Script object path: the two things
	// create_asset_by_class could not do (#975).
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), PackageName);
		Params->SetStringField(TEXT("className"), TEXT("/Script/Engine.StaticMeshComponent"));
		Params->SetStringField(TEXT("name"), TEXT("PayloadEntry"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("create_subobject"), Params);
		TestTrue(FString::Printf(TEXT("the subobject was created (%s)"),
			*SubobjectResponseString(Response, TEXT("error"))),
			SubobjectResponseBool(Response, TEXT("success")));
		TestTrue(TEXT("the response reports it as newly created"),
			SubobjectResponseBool(Response, TEXT("created")));
		TestEqual(TEXT("the response names the resolved class"),
			SubobjectResponseString(Response, TEXT("className")), FString(TEXT("StaticMeshComponent")));

		const FString ObjectPath = SubobjectResponseString(Response, TEXT("objectPath"));
		TestFalse(TEXT("the response returns an object path to use in later writes"), ObjectPath.IsEmpty());

		UObject* Created = StaticFindObjectFast(
			UObject::StaticClass(), Owner, FName(TEXT("PayloadEntry")));
		if (!TestNotNull(TEXT("the subobject is owned by the asset"), Created))
		{
			Package->SetDirtyFlag(false);
			return false;
		}
		TestEqual(TEXT("the reported object path is the subobject's own"),
			ObjectPath, Created->GetPathName());
		TestTrue(TEXT("the subobject lives in the owner's package"),
			Created->GetOutermost() == Package);

		// The heart of the issue: an object nothing references must survive a
		// collection, because the caller cannot reference it until the next
		// call. RF_Standalone is what carries it across.
		TestTrue(TEXT("the subobject is standalone, so nothing collects it before it is referenced"),
			Created->HasAnyFlags(RF_Standalone));
		const TWeakObjectPtr<UObject> WeakCreated(Created);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		TestTrue(TEXT("the subobject survives a garbage collection with nothing referencing it"),
			WeakCreated.IsValid());
		TestTrue(TEXT("the subobject is still resolvable by path after a collection"),
			MCPLoadAssetObject(ObjectPath) != nullptr);
	}

	// Replaying the same call reuses the object rather than creating a second.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), PackageName);
		Params->SetStringField(TEXT("className"), TEXT("/Script/Engine.StaticMeshComponent"));
		Params->SetStringField(TEXT("name"), TEXT("PayloadEntry"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("create_subobject"), Params);
		TestTrue(TEXT("a replay succeeds"), SubobjectResponseBool(Response, TEXT("success")));
		TestTrue(TEXT("a replay reports the subobject as already existing"),
			SubobjectResponseBool(Response, TEXT("existed")));
		TestFalse(TEXT("a replay creates nothing"),
			SubobjectResponseBool(Response, TEXT("created"), true));
	}

	// A property that cannot apply rejects the call before anything is created.
	{
		TSharedPtr<FJsonObject> BadProperties = MakeShared<FJsonObject>();
		BadProperties->SetStringField(TEXT("NoSuchPropertyOnThisClass"), TEXT("x"));

		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), PackageName);
		Params->SetStringField(TEXT("className"), TEXT("/Script/Engine.StaticMeshComponent"));
		Params->SetStringField(TEXT("name"), TEXT("NeverCreated"));
		Params->SetObjectField(TEXT("properties"), BadProperties);

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("create_subobject"), Params);
		TestFalse(TEXT("a bad property fails the call"),
			SubobjectResponseBool(Response, TEXT("success"), true));
		TestEqual(TEXT("the failure names the preflight"),
			SubobjectResponseString(Response, TEXT("reason")), FString(TEXT("property_preflight_failed")));
		TestNull(TEXT("nothing was created for the rejected call"),
			StaticFindObjectFast(UObject::StaticClass(), Owner, FName(TEXT("NeverCreated"))));
	}

	// An Actor class is refused rather than constructed outside a level.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), PackageName);
		Params->SetStringField(TEXT("className"), TEXT("/Script/Engine.StaticMeshActor"));
		Params->SetStringField(TEXT("name"), TEXT("NotAnActor"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("create_subobject"), Params);
		TestFalse(TEXT("an Actor class is refused"),
			SubobjectResponseBool(Response, TEXT("success"), true));
		TestEqual(TEXT("the refusal says why"),
			SubobjectResponseString(Response, TEXT("reason")), FString(TEXT("actor_class")));
	}

	// A protected mount is refused before anything is resolved.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), TEXT("/Engine/EngineMaterials/DefaultMaterial"));
		Params->SetStringField(TEXT("className"), TEXT("/Script/Engine.StaticMeshComponent"));
		Params->SetStringField(TEXT("name"), TEXT("NeverCreated"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("create_subobject"), Params);
		TestFalse(TEXT("a protected mount is refused"),
			SubobjectResponseBool(Response, TEXT("success"), true));
	}

	// Leave nothing dirty behind: a later editor(save_dirty) would otherwise
	// find this package and try to flush it through an unmounted root.
	Package->SetDirtyFlag(false);
	return true;
}

#endif
