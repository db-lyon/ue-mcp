// An array element addressed by the value of one of its fields rather than by
// its index: `WeaponProfiles[WeaponId=SGK_Lighter].Presentation.<field>` on a
// data asset whose whole array is too large to read just to find the index.
//
// The selector goes through the shared path resolver, so read_properties,
// set_property, bulk_read_properties and editor(get_property) all take it.
// Each must land on the one matching element, report the numeric path it
// resolved to, refuse a selector matching no element or several, and hand
// back a rollback addressed by index, since a write to the key field itself
// leaves the Field=Value form naming nothing.
//
// Everything happens under a private mount, like the #931 persistence test,
// so no package of the attached project is touched.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/MCPNestedArrayPathTestTypes.h"

#include "HandlerRegistry.h"
#include "HandlerQuery.h"
#include "HandlerUtils.h"
#include "Handlers/Asset/AssetHandlers.h"
#include "Handlers/Asset/AssetHandlers_BulkRead.h"
#include "Handlers/Editor/EditorHandlers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

namespace
{
	const TCHAR* const MCPKeyedPathTestRoot = TEXT("/UEMCPKeyedArrayPathTest/");

	TSharedPtr<FJsonObject> AssetKeyedPathCall(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Params);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : MakeShared<FJsonObject>();
	}

	bool AssetKeyedPathBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool bDefault = false)
	{
		bool bValue = bDefault;
		Object->TryGetBoolField(Field, bValue);
		return bValue;
	}

	FString AssetKeyedPathString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		Object->TryGetStringField(Field, Value);
		return Value;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPAssetKeyedArrayPathTest,
	"UE.MCP.Asset.SetProperty.KeyedArraySelector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetKeyedArrayPathTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ FString(MCPKeyedPathTestRoot), TEXT("UEMCPKeyedArrayPathTest") };

	const FString AssetPath = FString(MCPKeyedPathTestRoot) + TEXT("DA_Profiles");
	UPackage* Package = CreatePackage(*AssetPath);
	if (!TestNotNull(TEXT("fixture package created"), Package)) return false;
	UUEMCPNestedArrayPathTestAsset* Asset = NewObject<UUEMCPNestedArrayPathTestAsset>(
		Package, FName(TEXT("DA_Profiles")), RF_Public | RF_Standalone | RF_Transactional);
	const FGCRootScope KeepAssetAlive(Asset);
	// B twice, so one selector is ambiguous.
	const TCHAR* const Ids[] = { TEXT("Alpha"), TEXT("Bravo"), TEXT("Lighter"), TEXT("Bravo") };
	for (const TCHAR* Id : Ids)
	{
		Asset->Profiles.AddDefaulted_GetRef().Id = FName(Id);
	}

	const FString ClipPath = FString(MCPKeyedPathTestRoot) + TEXT("DT_Clip");
	UPackage* ClipPackage = CreatePackage(*ClipPath);
	UDataTable* Clip = NewObject<UDataTable>(ClipPackage, FName(TEXT("DT_Clip")), RF_Public | RF_Standalone);
	// A table with no row struct logs an error when it is emptied, which fails whichever test is running when it is collected.
	Clip->RowStruct = FTableRowBase::StaticStruct();
	const FGCRootScope KeepClipAlive(Clip);

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	FAssetBulkReadHandlers::RegisterHandlers(Registry);
	FEditorHandlers::RegisterHandlers(Registry);

	auto SetUnsaved = [&](const FString& PropertyName, const TSharedPtr<FJsonValue>& Value)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), AssetPath);
		Params->SetStringField(TEXT("propertyName"), PropertyName);
		Params->SetField(TEXT("value"), Value);
		Params->SetBoolField(TEXT("save"), false);
		return AssetKeyedPathCall(Registry, TEXT("set_asset_property"), Params);
	};
	auto Read = [&](const FString& PropertyName)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), AssetPath);
		Params->SetStringField(TEXT("propertyName"), PropertyName);
		return AssetKeyedPathCall(Registry, TEXT("read_asset_properties"), Params);
	};

	// Write through the selector: element 2 and no other, cited by index.
	const TSharedPtr<FJsonObject> Write = SetUnsaved(
		TEXT("Profiles[Id=Lighter].Presentation.PrimaryUse"), MakeShared<FJsonValueString>(ClipPath));
	TestTrue(FString::Printf(TEXT("the keyed write succeeded (%s)"), *AssetKeyedPathString(Write, TEXT("error"))),
		AssetKeyedPathBool(Write, TEXT("success")));
	TestTrue(TEXT("the keyed write landed on element 2"), Asset->Profiles[2].Presentation.PrimaryUse == Clip);
	TestTrue(TEXT("element 3 is untouched"), Asset->Profiles[3].Presentation.PrimaryUse == nullptr);
	TestEqual(TEXT("the write reports the index it resolved to"),
		AssetKeyedPathString(Write, TEXT("resolvedPropertyName")), FString(TEXT("Profiles[2].Presentation.PrimaryUse")));
	TestEqual(TEXT("the upstream indexedPath field remains available"),
		AssetKeyedPathString(Write, TEXT("indexedPath")), FString(TEXT("Profiles[2].Presentation.PrimaryUse")));

	// Read through the selector.
	{
		const TSharedPtr<FJsonObject> Result = Read(TEXT("Profiles[Id=Lighter].Presentation.PrimaryUse"));
		TestTrue(TEXT("read_properties resolves the keyed path"), AssetKeyedPathBool(Result, TEXT("success")));
		TestTrue(TEXT("read_properties returns element 2's reference"), AssetKeyedPathString(Result, TEXT("value")).Contains(TEXT("DT_Clip")));
		TestEqual(TEXT("read_properties reports the resolved index"),
			AssetKeyedPathString(Result, TEXT("resolvedPropertyName")), FString(TEXT("Profiles[2].Presentation.PrimaryUse")));
	}

	// A key on a nested field whose value has dots in it: the path splitter
	// must not cut inside the brackets.
	{
		FString ClipText;
		FProperty* PrimaryUse = FUEMCPNestedArrayPathPresentation::StaticStruct()->FindPropertyByName(TEXT("PrimaryUse"));
		PrimaryUse->ExportText_Direct(ClipText, &Asset->Profiles[2].Presentation.PrimaryUse, &Asset->Profiles[2].Presentation.PrimaryUse, nullptr, PPF_None);
		const TSharedPtr<FJsonObject> Result = Read(FString::Printf(TEXT("Profiles[Presentation.PrimaryUse=%s].Id"), *ClipText));
		TestTrue(FString::Printf(TEXT("a nested key field resolves (%s)"), *AssetKeyedPathString(Result, TEXT("error"))),
			AssetKeyedPathBool(Result, TEXT("success")));
		TestEqual(TEXT("the nested key found element 2"), AssetKeyedPathString(Result, TEXT("value")), FString(TEXT("Lighter")));

		FString Type, ResolvedPath, Error;
		const TSharedPtr<FJsonValue> QueryValue = MCPQuery::ReadDottedProperty(Asset,
			FString::Printf(TEXT("Profiles[Presentation.PrimaryUse=%s].Id"), *ClipText), Type, &ResolvedPath, &Error);
		TestTrue(FString::Printf(TEXT("query reads share nested key matching (%s)"), *Error),
			QueryValue.IsValid() && QueryValue->AsString() == TEXT("Lighter"));
		TestEqual(TEXT("query reports the nested key's numeric path"), ResolvedPath, FString(TEXT("Profiles[2].Id")));
	}

	// Bulk query paths retain upstream's quoting rules, including a closing
	// bracket and a dot inside the key, and use the same numeric path.
	{
		Asset->Profiles[0].Id = FName(TEXT("a]b.c"));
		const FString Path = TEXT("Profiles[Id=\"a]b.c\"].Id");
		const TSharedPtr<FJsonObject> Result = Read(Path);
		TestTrue(TEXT("read_properties accepts a quoted key containing brackets"), AssetKeyedPathBool(Result, TEXT("success")));
		FString Type, ResolvedPath, Error;
		const TSharedPtr<FJsonValue> QueryValue = MCPQuery::ReadDottedProperty(Asset, Path, Type, &ResolvedPath, &Error);
		TestTrue(FString::Printf(TEXT("query reads use the quoted key parser (%s)"), *Error),
			QueryValue.IsValid() && QueryValue->AsString() == TEXT("a]b.c"));
		TestEqual(TEXT("query reports the quoted key's numeric path"), ResolvedPath, FString(TEXT("Profiles[0].Id")));
		Asset->Profiles[0].Id = FName(TEXT("Alpha"));
	}

	// A numeric index is unchanged, and resolves to itself.
	{
		const TSharedPtr<FJsonObject> Result = SetUnsaved(TEXT("Profiles[1].Presentation.PrimaryUse"), MakeShared<FJsonValueString>(ClipPath));
		TestTrue(TEXT("the numeric write succeeded"), AssetKeyedPathBool(Result, TEXT("success")));
		TestTrue(TEXT("the numeric write landed on element 1"), Asset->Profiles[1].Presentation.PrimaryUse == Clip);
		TestEqual(TEXT("a numeric path resolves to itself"),
			AssetKeyedPathString(Result, TEXT("resolvedPropertyName")), FString(TEXT("Profiles[1].Presentation.PrimaryUse")));
	}

	// Writing the key field: the rollback must address the element by index,
	// because Profiles[Id=Alpha] names nothing once Alpha is renamed.
	{
		const TSharedPtr<FJsonObject> Rename = SetUnsaved(TEXT("Profiles[Id=Alpha].Id"), MakeShared<FJsonValueString>(TEXT("Renamed")));
		TestTrue(TEXT("the key field write succeeded"), AssetKeyedPathBool(Rename, TEXT("success")));
		TestEqual(TEXT("element 0 was renamed"), Asset->Profiles[0].Id.ToString(), FString(TEXT("Renamed")));

		const TSharedPtr<FJsonObject>* Rollback = nullptr;
		const TSharedPtr<FJsonObject>* RollbackPayload = nullptr;
		if (TestTrue(TEXT("the write carries a rollback"), Rename->TryGetObjectField(TEXT("rollback"), Rollback))
			&& TestTrue(TEXT("the rollback has a payload"), (*Rollback)->TryGetObjectField(TEXT("payload"), RollbackPayload)))
		{
			TestEqual(TEXT("the rollback addresses the resolved index"),
				AssetKeyedPathString(*RollbackPayload, TEXT("propertyName")), FString(TEXT("Profiles[0].Id")));
			// Replay in memory; this test is about where it lands, not the save.
			(*RollbackPayload)->SetBoolField(TEXT("save"), false);
			const TSharedPtr<FJsonObject> Restored = AssetKeyedPathCall(Registry, TEXT("set_asset_property"), *RollbackPayload);
			TestTrue(FString::Printf(TEXT("the rollback replays (%s)"), *AssetKeyedPathString(Restored, TEXT("error"))),
				AssetKeyedPathBool(Restored, TEXT("success")));
			TestEqual(TEXT("the rollback restored the key"), Asset->Profiles[0].Id.ToString(), FString(TEXT("Alpha")));
		}
	}

	// Selectors that name no element, or several, or are malformed, are
	// refused and write nothing.
	struct FRefusal
	{
		const TCHAR* Path;
		const TCHAR* Reason;
	};
	const FRefusal Refusals[] = {
		{ TEXT("Profiles[Id=Nope].Presentation.PrimaryUse"), TEXT("no element of 'Profiles' (4 elements)") },
		{ TEXT("Profiles[Id=Nope].Presentation.PrimaryUse"), TEXT("Id values: [Alpha, Bravo, Lighter, Bravo]") },
		{ TEXT("Profiles[Id=lighter].Presentation.PrimaryUse"), TEXT("differs only in case") },
		{ TEXT("Profiles[Id=Bravo].Presentation.PrimaryUse"), TEXT("matches 2 elements of 'Profiles' (indices 1, 3)") },
		{ TEXT("Profiles[NoSuchField=Alpha].Presentation.PrimaryUse"), TEXT("has a field named 'NoSuchField'") },
		{ TEXT("Profiles[=Alpha].Presentation.PrimaryUse"), TEXT("no key field before '='") },
		{ TEXT("Profiles[x].Presentation.PrimaryUse"), TEXT("neither an index nor a key") },
		{ TEXT("Profiles[0][1].Presentation.PrimaryUse"), TEXT("One selector per segment") },
		{ TEXT("Profiles[4].Presentation.PrimaryUse"), TEXT("out of range") },
		{ TEXT("Profiles[0].Id[Id=Alpha]"), TEXT("not an array") },
	};
	for (const FRefusal& Refusal : Refusals)
	{
		const TSharedPtr<FJsonObject> Refused = SetUnsaved(Refusal.Path, MakeShared<FJsonValueString>(ClipPath));
		TestFalse(FString::Printf(TEXT("'%s' is refused"), Refusal.Path), AssetKeyedPathBool(Refused, TEXT("success"), true));
		TestTrue(FString::Printf(TEXT("'%s' says why (%s)"), Refusal.Path, *AssetKeyedPathString(Refused, TEXT("error"))),
			AssetKeyedPathString(Refused, TEXT("error")).Contains(Refusal.Reason));
	}
	TestTrue(TEXT("a refused selector wrote nothing to element 0"), Asset->Profiles[0].Presentation.PrimaryUse == nullptr);
	TestTrue(TEXT("a refused selector wrote nothing to element 3"), Asset->Profiles[3].Presentation.PrimaryUse == nullptr);

	// editor(get_property) shares the resolver.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("objectPath"), Asset->GetPathName());
		Params->SetStringField(TEXT("propertyName"), TEXT("Profiles[Id=Lighter].Id"));
		const TSharedPtr<FJsonObject> Result = AssetKeyedPathCall(Registry, TEXT("get_property"), Params);
		TestTrue(FString::Printf(TEXT("get_property resolves the keyed path (%s)"), *AssetKeyedPathString(Result, TEXT("error"))),
			AssetKeyedPathBool(Result, TEXT("success")));
		TestEqual(TEXT("get_property reports the resolved index"),
			AssetKeyedPathString(Result, TEXT("resolvedPropertyName")), FString(TEXT("Profiles[2].Id")));
	}

	// bulk_read_properties walks its own struct-only path, with the same selector.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetArrayField(TEXT("assetPaths"), { MakeShared<FJsonValueString>(AssetPath) });
		Params->SetArrayField(TEXT("propertyNames"), {
			MakeShared<FJsonValueString>(TEXT("Profiles[Id=Lighter].Id")),
			MakeShared<FJsonValueString>(TEXT("Profiles[Id=Bravo].Id")),
		});
		const TSharedPtr<FJsonObject> Result = AssetKeyedPathCall(Registry, TEXT("bulk_read_asset_properties"), Params);
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (TestTrue(FString::Printf(TEXT("bulk_read returns rows (%s)"), *AssetKeyedPathString(Result, TEXT("error"))),
				Result->TryGetArrayField(TEXT("rows"), Rows) && Rows->Num() == 1))
		{
			const TSharedPtr<FJsonObject> Row = (*Rows)[0]->AsObject();
			const TSharedPtr<FJsonObject>* Props = nullptr;
			const TSharedPtr<FJsonObject>* Resolved = nullptr;
			const TSharedPtr<FJsonObject>* Reasons = nullptr;
			TestTrue(TEXT("bulk_read reads the keyed element"),
				Row->TryGetObjectField(TEXT("props"), Props)
				&& AssetKeyedPathString(*Props, TEXT("Profiles[Id=Lighter].Id")) == TEXT("Lighter"));
			TestTrue(TEXT("bulk_read reports the resolved index"),
				Row->TryGetObjectField(TEXT("resolvedPropertyNames"), Resolved)
				&& AssetKeyedPathString(*Resolved, TEXT("Profiles[Id=Lighter].Id")) == TEXT("Profiles[2].Id"));
			TestTrue(TEXT("bulk_read says why an ambiguous selector is missing"),
				Row->TryGetObjectField(TEXT("missingReasons"), Reasons)
				&& AssetKeyedPathString(*Reasons, TEXT("Profiles[Id=Bravo].Id")).Contains(TEXT("matches 2 elements")));
		}
	}

	// Leave nothing dirty for a later save_dirty to flush through an unmounted root.
	Package->SetDirtyFlag(false);
	ClipPackage->SetDirtyFlag(false);
	return true;
}

#endif
