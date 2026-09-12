// Regression coverage for #957 and #913.
//
// A short package path and its object path form name the same asset, and
// asset(search) reports the short one. Several actions accepted only the object
// path, so an asset that existed and had just been reported by search answered
// "Asset not found", and the sentence was the same one a genuinely missing path
// produced. These tests pin the two halves of the fix: every path form derives
// the same object path, and a miss carries enough structure to tell a path-shape
// bug from a missing asset.
//
// The test writes nowhere near the attached project. run_automation_tests
// dispatches every EditorContext/EngineFilter test in the process against
// whatever project the bridge happens to be attached to, so this registers its
// own mount point over a fresh directory in the system temp area, does the whole
// exercise there, then unmounts and deletes it.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"

#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace
{
	const TCHAR* const MCPAssetPathTestRoot = TEXT("/UEMCPAssetPathTest/");

	/** Mount a private content root for the duration of the test and take it
	 *  back down again, so no assertion here can touch a user's content. */
	struct FScopedAssetPathTestMount
	{
		FString RootPath;
		FString ContentPath;

		FScopedAssetPathTestMount()
			: RootPath(MCPAssetPathTestRoot)
			, ContentPath(FPaths::Combine(
				FPaths::ConvertRelativePathToFull(FString(FPlatformProcess::UserTempDir())),
				FString(TEXT("UEMCPAssetPathTest")),
				FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
			IFileManager::Get().MakeDirectory(*ContentPath, /*Tree=*/true);
			FPackageName::RegisterMountPoint(RootPath, ContentPath);
		}

		~FScopedAssetPathTestMount()
		{
			FPackageName::UnRegisterMountPoint(RootPath, ContentPath);
			IFileManager::Get().DeleteDirectory(*ContentPath, /*RequireExists=*/false, /*Tree=*/true);
		}

		FScopedAssetPathTestMount(const FScopedAssetPathTestMount&) = delete;
		FScopedAssetPathTestMount& operator=(const FScopedAssetPathTestMount&) = delete;
	};

	/** Read one string field off an error response. */
	FString ErrorField(const TSharedPtr<FJsonValue>& Value, const TCHAR* Field)
	{
		if (!Value.IsValid()) return FString();
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid()) return FString();
		FString Out;
		Obj->TryGetStringField(Field, Out);
		return Out;
	}

	bool ErrorSucceeded(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid()) return false;
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid()) return false;
		bool bSuccess = true;
		Obj->TryGetBoolField(TEXT("success"), bSuccess);
		return bSuccess;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPAssetPathFormsTest,
	"UE.MCP.Asset.PathResolution.EveryFormDerivesOneObjectPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetPathFormsTest::RunTest(const FString& Parameters)
{
	// The bare package path: the form asset(search) reports and #957 rejected.
	{
		const FMCPAssetPathForms Forms = MCPAssetPathForms(TEXT("/Game/MyFolder/DT_Thing"));
		TestEqual(TEXT("package path from a bare package path"),
			Forms.PackagePath, FString(TEXT("/Game/MyFolder/DT_Thing")));
		TestEqual(TEXT("object path derived from a bare package path"),
			Forms.ObjectPath, FString(TEXT("/Game/MyFolder/DT_Thing.DT_Thing")));
		TestEqual(TEXT("asset name from a bare package path"),
			Forms.AssetName, FString(TEXT("DT_Thing")));
		TestFalse(TEXT("a bare package path carried no object name"), Forms.bInputCarriedObjectName);
	}

	// The object path: unchanged, and the same object path as the short form.
	{
		const FMCPAssetPathForms Forms = MCPAssetPathForms(TEXT("/Game/MyFolder/DT_Thing.DT_Thing"));
		TestEqual(TEXT("package path from an object path"),
			Forms.PackagePath, FString(TEXT("/Game/MyFolder/DT_Thing")));
		TestEqual(TEXT("object path from an object path"),
			Forms.ObjectPath, FString(TEXT("/Game/MyFolder/DT_Thing.DT_Thing")));
		TestTrue(TEXT("an object path carried an object name"), Forms.bInputCarriedObjectName);
	}

	// The export-text form, which arrives from copied editor references.
	{
		const FMCPAssetPathForms Forms = MCPAssetPathForms(TEXT("DataTable'/Game/MyFolder/DT_Thing.DT_Thing'"));
		TestEqual(TEXT("object path from an export text path"),
			Forms.ObjectPath, FString(TEXT("/Game/MyFolder/DT_Thing.DT_Thing")));
	}

	// A subobject path keeps its own leaf, and still names its own package.
	{
		const FMCPAssetPathForms Forms = MCPAssetPathForms(TEXT("/Game/MyFolder/DT_Thing.DT_Thing:Inner"));
		TestEqual(TEXT("package path from a subobject path"),
			Forms.PackagePath, FString(TEXT("/Game/MyFolder/DT_Thing")));
		TestEqual(TEXT("object path from a subobject path"),
			Forms.ObjectPath, FString(TEXT("/Game/MyFolder/DT_Thing.DT_Thing:Inner")));
		TestEqual(TEXT("asset name from a subobject path"),
			Forms.AssetName, FString(TEXT("Inner")));
	}

	// Surrounding whitespace is a copy-paste artefact, not a different asset.
	{
		const FMCPAssetPathForms Forms = MCPAssetPathForms(TEXT("  /Game/MyFolder/DT_Thing  "));
		TestEqual(TEXT("whitespace does not change the derived object path"),
			Forms.ObjectPath, FString(TEXT("/Game/MyFolder/DT_Thing.DT_Thing")));
	}

	// An empty path derives nothing rather than an object path of ".".
	{
		const FMCPAssetPathForms Forms = MCPAssetPathForms(FString());
		TestTrue(TEXT("an empty path derives an empty object path"), Forms.ObjectPath.IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPAssetPathResolutionTest,
	"UE.MCP.Asset.PathResolution.ShortPathResolvesAndMissesAreDiagnosable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetPathResolutionTest::RunTest(const FString& Parameters)
{
	const FScopedAssetPathTestMount Mount;

	const FString PackageName = FString(MCPAssetPathTestRoot) + TEXT("DT_PathProbe");
	const FString ObjectPath = PackageName + TEXT(".DT_PathProbe");

	UPackage* Package = CreatePackage(*PackageName);
	TestNotNull(TEXT("probe package created"), Package);
	if (!Package)
	{
		return false;
	}

	UDataTable* Probe = NewObject<UDataTable>(
		Package, FName(TEXT("DT_PathProbe")), RF_Public | RF_Standalone);
	TestNotNull(TEXT("probe asset created"), Probe);
	if (!Probe)
	{
		Package->SetDirtyFlag(false);
		return false;
	}
	const FGCRootScope KeepProbeAlive(Probe);

	// #957: this is the assertion that fails on a regression. The short form is
	// what asset(search) reports, and it must resolve to the same object the
	// object path does.
	TestTrue(TEXT("a bare package path resolves to the asset"),
		MCPLoadAssetObject(PackageName) == Probe);
	TestTrue(TEXT("an object path resolves to the same asset"),
		MCPLoadAssetObject(ObjectPath) == Probe);
	TestTrue(TEXT("an export text path resolves to the same asset"),
		MCPLoadAssetObject(FString::Printf(TEXT("DataTable'%s'"), *ObjectPath)) == Probe);
	TestTrue(TEXT("a path with surrounding whitespace resolves to the same asset"),
		MCPLoadAssetObject(FString::Printf(TEXT("  %s  "), *PackageName)) == Probe);

	// The resolver never hands back the package in place of the asset.
	TestTrue(TEXT("the resolver answers the asset, never its package"),
		MCPLoadAssetObject(PackageName) != static_cast<UObject*>(Package));

	// The typed loader rides on the same resolution, so a short path works for
	// the type-specific readers too (#913).
	TestTrue(TEXT("a typed load resolves a bare package path"),
		LoadAssetByPath<UDataTable>(PackageName) == Probe);
	TestNull(TEXT("a typed load of the wrong type resolves to nothing"),
		LoadAssetByPath<UTexture2D>(PackageName));

	// A path that resolved to the wrong type says which type it found, so it is
	// distinguishable from a path that resolved to nothing at all.
	{
		const TSharedPtr<FJsonValue> Wrong = MCPAssetLoadError(PackageName, TEXT("Texture2D"));
		TestFalse(TEXT("a wrong-type answer is an error"), ErrorSucceeded(Wrong));
		TestEqual(TEXT("a wrong-type answer names the class that was found"),
			ErrorField(Wrong, TEXT("foundClass")), FString(TEXT("DataTable")));
		TestEqual(TEXT("a wrong-type answer names the class that was expected"),
			ErrorField(Wrong, TEXT("expectedClass")), FString(TEXT("Texture2D")));
	}

	// A genuinely missing asset reports the forms that were tried and says the
	// package is not there, rather than a bare "Asset not found".
	{
		const FString MissingPath = FString(MCPAssetPathTestRoot) + TEXT("DT_NotThere");
		TestNull(TEXT("a missing path resolves to nothing"), MCPLoadAssetObject(MissingPath));

		const TSharedPtr<FJsonValue> Missing = MCPAssetNotFoundError(MissingPath);
		TestFalse(TEXT("a miss is an error"), ErrorSucceeded(Missing));
		TestEqual(TEXT("a miss echoes the caller's own path"),
			ErrorField(Missing, TEXT("assetPath")), MissingPath);
		TestEqual(TEXT("a miss names the normalized package path"),
			ErrorField(Missing, TEXT("packagePath")), MissingPath);
		TestEqual(TEXT("a miss names the object path form it tried"),
			ErrorField(Missing, TEXT("objectPath")), MissingPath + TEXT(".DT_NotThere"));
		TestEqual(TEXT("a miss with nothing on disk and nothing in the registry reads as missing"),
			ErrorField(Missing, TEXT("reason")), FString(TEXT("missing")));

		// #1065: every load failure says whether play was running, because
		// that changes how assets load and a caller cannot see it. The suite
		// does not run under PIE, so the flag is present and false here; what
		// is being pinned is that the field exists at all.
		const TSharedPtr<FJsonObject> MissingObj = Missing->AsObject();
		TestTrue(TEXT("a miss reports whether play-in-editor was running"),
			MissingObj.IsValid() && MissingObj->HasField(TEXT("playInEditorActive")));
		TestEqual(TEXT("the flag agrees with the editor's own state"),
			MissingObj.IsValid() && MissingObj->GetBoolField(TEXT("playInEditorActive")),
			MCPIsPlayInEditorActive());
		TestEqual(TEXT("the shared note is empty when play is not running"),
			MCPPlayInEditorLoadNote().IsEmpty(), !MCPIsPlayInEditorActive());
	}

	// A context label names what the path was supposed to be.
	{
		const TSharedPtr<FJsonValue> Missing = MCPAssetNotFoundError(
			FString(MCPAssetPathTestRoot) + TEXT("DT_NotThere"), TEXT("Source asset"));
		TestTrue(TEXT("a context label leads the message"),
			ErrorField(Missing, TEXT("error")).StartsWith(TEXT("Source asset not found:")));
	}

	// Leave nothing dirty behind: a later editor(save_dirty) would otherwise
	// find this package and try to flush it through an unmounted root.
	Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPAssetReloadCorpseTest,
	"UE.MCP.Asset.PathResolution.AReloadCorpseIsNeverHandedBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetReloadCorpseTest::RunTest(const FString& Parameters)
{
	// #1074. A package reload does not free the object it replaced: it renames
	// it aside, marks it RF_NewerVersionExists, and leaves it reachable in the
	// object hash while a freshly loaded object takes the real path. The
	// resolver asked the hash first and returned whatever came back, so after a
	// write that reloaded a package, reads answered off the corpse and writes
	// landed on it. Only an editor restart cleared it.
	//
	// The reload is reproduced here by its observable effect rather than by
	// running one: an object carrying the flag, and a live object at the path.
	const FScopedAssetPathTestMount Mount;

	const FString PackageName = FString(MCPAssetPathTestRoot) + TEXT("DT_ReloadProbe");

	UPackage* Package = CreatePackage(*PackageName);
	TestNotNull(TEXT("probe package created"), Package);
	if (!Package)
	{
		return false;
	}

	UDataTable* Corpse = NewObject<UDataTable>(
		Package, FName(TEXT("DT_ReloadProbe")), RF_Public | RF_Standalone);
	TestNotNull(TEXT("probe asset created"), Corpse);
	if (!Corpse)
	{
		Package->SetDirtyFlag(false);
		return false;
	}
	const FGCRootScope KeepCorpseAlive(Corpse);

	// Before the reload the object at the path is the one the resolver answers.
	TestTrue(TEXT("a live asset resolves normally"),
		MCPLoadAssetObject(PackageName) == Corpse);

	// The reload: the old object is flagged and renamed out of the way, and a
	// new object takes the name. This is what UPackage reload does to the
	// object it replaces, and it is why FindObject can still reach the old one.
	Corpse->SetFlags(RF_NewerVersionExists);
	Corpse->Rename(
		*FString::Printf(TEXT("DT_ReloadProbe_DEADCLASS_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)),
		Package,
		REN_DontCreateRedirectors | REN_NonTransactional);

	UDataTable* Live = NewObject<UDataTable>(
		Package, FName(TEXT("DT_ReloadProbe")), RF_Public | RF_Standalone);
	TestNotNull(TEXT("replacement asset created"), Live);
	if (!Live)
	{
		Package->SetDirtyFlag(false);
		return false;
	}
	const FGCRootScope KeepLiveAlive(Live);

	// The assertion that fails on a regression: the resolver walks past the
	// corpse and answers the object the editor actually consults.
	UObject* Resolved = MCPLoadAssetObject(PackageName);
	TestTrue(TEXT("the resolver never answers the reload corpse"), Resolved != Corpse);
	TestTrue(TEXT("the resolver answers the live replacement"), Resolved == Live);

	// The predicate itself, so a caller reading it directly agrees.
	TestFalse(TEXT("a flagged object is not live"), MCPIsLiveAssetObject(Corpse));
	TestTrue(TEXT("the replacement is live"), MCPIsLiveAssetObject(Live));
	TestFalse(TEXT("a package is not an asset"), MCPIsLiveAssetObject(Package));
	TestFalse(TEXT("null is not live"), MCPIsLiveAssetObject(nullptr));

	Package->SetDirtyFlag(false);
	return true;
}

#endif
