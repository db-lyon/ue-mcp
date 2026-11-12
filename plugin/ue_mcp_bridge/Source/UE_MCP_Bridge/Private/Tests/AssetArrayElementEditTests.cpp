// One entry added to or taken out of a TArray<FString> on a data asset,
// leaving every other element where it was (#1659).
//
// append_asset_array_elements with unique=true must skip a value already
// present and write nothing when every value is; remove_asset_array_elements
// must take out elements by value or by index, refuse an index that does not
// exist without moving anything, and hand back a rollback that restores the
// array without saving. Everything is unsaved and under a private mount.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/MCPNestedArrayPathTestTypes.h"

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Asset/AssetHandlers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

namespace
{
	const TCHAR* const MCPArrayEditTestRoot = TEXT("/UEMCPArrayElementEditTest/");

	TSharedPtr<FJsonObject> ArrayEditCall(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Params);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : MakeShared<FJsonObject>();
	}

	bool ArrayEditBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool bDefault = false)
	{
		bool bValue = bDefault;
		Object->TryGetBoolField(Field, bValue);
		return bValue;
	}

	FString ArrayEditString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		Object->TryGetStringField(Field, Value);
		return Value;
	}

	int32 ArrayEditInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		double Value = -1;
		Object->TryGetNumberField(Field, Value);
		return static_cast<int32>(Value);
	}

	TArray<TSharedPtr<FJsonValue>> ArrayEditStrings(std::initializer_list<const TCHAR*> Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TCHAR* Value : Values)
		{
			Out.Add(MakeShared<FJsonValueString>(Value));
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPAssetArrayElementEditTest,
	"UE.MCP.Asset.ArrayElements.UniqueAppendAndRemove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetArrayElementEditTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ FString(MCPArrayEditTestRoot), TEXT("UEMCPArrayElementEditTest") };

	const FString AssetPath = FString(MCPArrayEditTestRoot) + TEXT("DA_BuildList");
	UPackage* Package = CreatePackage(*AssetPath);
	if (!TestNotNull(TEXT("fixture package created"), Package)) return false;
	UUEMCPNestedArrayPathTestAsset* Asset = NewObject<UUEMCPNestedArrayPathTestAsset>(
		Package, FName(TEXT("DA_BuildList")), RF_Public | RF_Standalone | RF_Transactional);
	Asset->OnlyInMaps = { TEXT("/Game/Maps/Dev_AllCraftingTest"), TEXT("/Game/Maps/openworldz") };
	const FGCRootScope KeepAssetAlive(Asset);

	FString PackageFileName;
	FPackageName::TryConvertLongPackageNameToFilename(AssetPath, PackageFileName, FPackageName::GetAssetPackageExtension());

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);

	auto MakeParams = [&]()
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), AssetPath);
		Params->SetStringField(TEXT("propertyName"), TEXT("OnlyInMaps"));
		Params->SetBoolField(TEXT("save"), false);
		return Params;
	};

	// A unique append adds the new map once and skips the one already listed.
	{
		TSharedPtr<FJsonObject> Params = MakeParams();
		Params->SetArrayField(TEXT("elements"), ArrayEditStrings({
			TEXT("/Game/Maps/SurvivalArena"), TEXT("/Game/Maps/openworldz"), TEXT("/Game/Maps/SurvivalArena") }));
		Params->SetBoolField(TEXT("unique"), true);
		const TSharedPtr<FJsonObject> Appended = ArrayEditCall(Registry, TEXT("append_asset_array_elements"), Params);
		TestTrue(FString::Printf(TEXT("unique append succeeds (%s)"), *ArrayEditString(Appended, TEXT("error"))),
			ArrayEditBool(Appended, TEXT("success")));
		if (!TestEqual(TEXT("only the new value was appended"), Asset->OnlyInMaps.Num(), 3)) return false;
		TestEqual(TEXT("existing element 0 is untouched"), Asset->OnlyInMaps[0], FString(TEXT("/Game/Maps/Dev_AllCraftingTest")));
		TestEqual(TEXT("existing element 1 is untouched"), Asset->OnlyInMaps[1], FString(TEXT("/Game/Maps/openworldz")));
		TestEqual(TEXT("the new value is last"), Asset->OnlyInMaps[2], FString(TEXT("/Game/Maps/SurvivalArena")));
		TestEqual(TEXT("appendedCount is 1"), ArrayEditInt(Appended, TEXT("appendedCount")), 1);
		const TArray<TSharedPtr<FJsonValue>>* Skipped = nullptr;
		if (TestTrue(TEXT("skippedElements is reported"), Appended->TryGetArrayField(TEXT("skippedElements"), Skipped)))
		{
			TestEqual(TEXT("two values were skipped"), Skipped->Num(), 2);
		}
		const TArray<TSharedPtr<FJsonValue>>* ReadBack = nullptr;
		if (TestTrue(TEXT("the array is read back"), Appended->TryGetArrayField(TEXT("value"), ReadBack)))
		{
			TestEqual(TEXT("the read back has three entries"), ReadBack->Num(), 3);
		}
		TestFalse(TEXT("an unsaved append writes no package"), IFileManager::Get().FileExists(*PackageFileName));
	}

	// Repeating it changes nothing and carries no rollback.
	{
		Package->SetDirtyFlag(false);
		TSharedPtr<FJsonObject> Params = MakeParams();
		Params->SetArrayField(TEXT("elements"), ArrayEditStrings({ TEXT("/Game/Maps/SurvivalArena") }));
		Params->SetBoolField(TEXT("unique"), true);
		const TSharedPtr<FJsonObject> Again = ArrayEditCall(Registry, TEXT("append_asset_array_elements"), Params);
		TestTrue(TEXT("a no-op unique append succeeds"), ArrayEditBool(Again, TEXT("success")));
		TestEqual(TEXT("the array keeps three entries"), Asset->OnlyInMaps.Num(), 3);
		TestEqual(TEXT("appendedCount is 0"), ArrayEditInt(Again, TEXT("appendedCount")), 0);
		TestFalse(TEXT("a no-op append has no rollback"), Again->HasField(TEXT("rollback")));
		TestFalse(TEXT("a no-op append leaves the package clean"), Package->IsDirty());
	}

	// Remove by value takes out the one entry and keeps the order of the rest.
	{
		TSharedPtr<FJsonObject> Params = MakeParams();
		Params->SetArrayField(TEXT("elements"), ArrayEditStrings({ TEXT("/Game/Maps/openworldz") }));
		const TSharedPtr<FJsonObject> Removed = ArrayEditCall(Registry, TEXT("remove_asset_array_elements"), Params);
		TestTrue(FString::Printf(TEXT("remove by value succeeds (%s)"), *ArrayEditString(Removed, TEXT("error"))),
			ArrayEditBool(Removed, TEXT("success")));
		if (!TestEqual(TEXT("one entry removed"), Asset->OnlyInMaps.Num(), 2)) return false;
		TestEqual(TEXT("element 0 stays first"), Asset->OnlyInMaps[0], FString(TEXT("/Game/Maps/Dev_AllCraftingTest")));
		TestEqual(TEXT("the appended map moves up"), Asset->OnlyInMaps[1], FString(TEXT("/Game/Maps/SurvivalArena")));
		TestEqual(TEXT("removedCount is 1"), ArrayEditInt(Removed, TEXT("removedCount")), 1);
		TestTrue(TEXT("the package is dirty"), Package->IsDirty());
		TestFalse(TEXT("an unsaved remove writes no package"), IFileManager::Get().FileExists(*PackageFileName));

		const TSharedPtr<FJsonObject>* Rollback = nullptr;
		const TSharedPtr<FJsonObject>* Payload = nullptr;
		if (TestTrue(TEXT("remove has rollback"), Removed->TryGetObjectField(TEXT("rollback"), Rollback))
			&& TestTrue(TEXT("remove rollback has payload"), (*Rollback)->TryGetObjectField(TEXT("payload"), Payload)))
		{
			TestFalse(TEXT("remove inverse preserves save=false"), ArrayEditBool(*Payload, TEXT("save"), true));
			const TSharedPtr<FJsonObject> Restored = ArrayEditCall(Registry, TEXT("set_asset_property"), *Payload);
			TestTrue(TEXT("remove inverse replays"), ArrayEditBool(Restored, TEXT("success")));
			if (TestEqual(TEXT("remove inverse restores three entries"), Asset->OnlyInMaps.Num(), 3))
			{
				TestEqual(TEXT("remove inverse restores the removed entry in place"), Asset->OnlyInMaps[1], FString(TEXT("/Game/Maps/openworldz")));
			}
		}
	}

	// Remove by index, undone by the editor's undo.
	{
		TSharedPtr<FJsonObject> Params = MakeParams();
		Params->SetArrayField(TEXT("indices"), { MakeShared<FJsonValueNumber>(0), MakeShared<FJsonValueNumber>(2) });
		const TSharedPtr<FJsonObject> Removed = ArrayEditCall(Registry, TEXT("remove_asset_array_elements"), Params);
		TestTrue(TEXT("remove by index succeeds"), ArrayEditBool(Removed, TEXT("success")));
		if (TestEqual(TEXT("two entries removed"), Asset->OnlyInMaps.Num(), 1))
		{
			TestEqual(TEXT("the middle entry remains"), Asset->OnlyInMaps[0], FString(TEXT("/Game/Maps/openworldz")));
		}
		if (TestNotNull(TEXT("the editor is available for undo"), GEditor))
		{
			GEditor->UndoTransaction();
			TestEqual(TEXT("undo restores three entries"), Asset->OnlyInMaps.Num(), 3);
		}
	}

	// An index that does not exist is refused and nothing moves.
	{
		TSharedPtr<FJsonObject> Params = MakeParams();
		Params->SetArrayField(TEXT("indices"), { MakeShared<FJsonValueNumber>(1), MakeShared<FJsonValueNumber>(7) });
		const TSharedPtr<FJsonObject> Refused = ArrayEditCall(Registry, TEXT("remove_asset_array_elements"), Params);
		TestFalse(TEXT("an out of range index is refused"), ArrayEditBool(Refused, TEXT("success"), true));
		TestTrue(FString::Printf(TEXT("the refusal says why (%s)"), *ArrayEditString(Refused, TEXT("error"))),
			ArrayEditString(Refused, TEXT("error")).Contains(TEXT("out of range")));
		TestEqual(TEXT("a refused remove leaves every entry"), Asset->OnlyInMaps.Num(), 3);
	}

	// A value the array does not hold removes nothing and has no rollback.
	{
		TSharedPtr<FJsonObject> Params = MakeParams();
		Params->SetArrayField(TEXT("elements"), ArrayEditStrings({ TEXT("/Game/Maps/NotListed") }));
		const TSharedPtr<FJsonObject> Missed = ArrayEditCall(Registry, TEXT("remove_asset_array_elements"), Params);
		TestTrue(TEXT("a remove that matches nothing succeeds"), ArrayEditBool(Missed, TEXT("success")));
		TestEqual(TEXT("removedCount is 0"), ArrayEditInt(Missed, TEXT("removedCount")), 0);
		TestFalse(TEXT("a no-op remove has no rollback"), Missed->HasField(TEXT("rollback")));
		TestEqual(TEXT("the array keeps three entries"), Asset->OnlyInMaps.Num(), 3);
	}

	// Leave nothing dirty for a later save_dirty to flush through an unmounted root.
	Package->SetDirtyFlag(false);
	return true;
}

#endif
