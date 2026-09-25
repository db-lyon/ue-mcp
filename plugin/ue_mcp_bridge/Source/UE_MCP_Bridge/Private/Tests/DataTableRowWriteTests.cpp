// Coverage for the DataTable row write path and the JSON property setter it
// runs on, for the three data-loss bugs they carried (#928, #929, #935), and
// for read_datatable's outputPath form, which writes the rows to a file.
//
// run_automation_tests dispatches every EditorContext/EngineFilter test in the
// process when it is called without a filter, against whatever project the
// bridge is attached to, so nothing here may touch a real asset. The DataTable
// under test is built in the transient package and lives for the length of one
// test; the property cases run on stack structs and on temporary property
// buffers, and read only the reflection data of engine types.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "JsonSerializer.h"
#include "Handlers/AssetHandlers.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/AssetManagerTypes.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Math/IntVector.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "MCPEngineCompat.h"
#include "Tests/MCPDataTableTestTypes.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

namespace
{
// FPerPlatformInt is the row struct throughout: it is a plain engine USTRUCT
// with exactly the shape issue #929 is about. "Default" is an int32 with an
// explicit constructor default, and "PerPlatform" is a TMap with none, so a
// row rebuilt from struct defaults keeps the first and loses the second.
const TCHAR* const DataTableRowWriteScalarField = TEXT("Default");
const TCHAR* const DataTableRowWriteMapField = TEXT("PerPlatform");

/** A DataTable in the transient package, keyed on FPerPlatformInt rows. */
UDataTable* MakeTransientPerPlatformTable()
{
	const FName TableName(*FString::Printf(TEXT("DT_UEMCP_RowWrite_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), TableName);
	if (!Table) return nullptr;
	Table->RowStruct = FPerPlatformInt::StaticStruct();
	return Table;
}

/** Add one row, built by hand so the test never depends on the write path it
 *  is about to exercise. */
void AddPerPlatformRow(UDataTable* Table, const TCHAR* RowName, int32 Default, const TMap<FName, int32>& PerPlatform)
{
	FPerPlatformInt Row;
	Row.Default = Default;
#if WITH_EDITORONLY_DATA
	Row.PerPlatform = PerPlatform;
#else
	(void)PerPlatform;
#endif
	Table->AddRow(FName(RowName), *reinterpret_cast<FTableRowBase*>(&Row));
}

/** The row memory the table owns, read straight out of the row map so nothing
 *  in the read path can paper over a bad write. */
const FPerPlatformInt* FindPerPlatformRow(const UDataTable* Table, const TCHAR* RowName)
{
	uint8* const* Found = Table->GetRowMap().Find(FName(RowName));
	return (Found && *Found) ? reinterpret_cast<const FPerPlatformInt*>(*Found) : nullptr;
}

/** Params for asset(set_datatable_cell) against a transient table. */
TSharedPtr<FJsonObject> MakeCellWriteParams(
	const UDataTable* Table,
	const TCHAR* RowName,
	const TCHAR* FieldName,
	const TSharedPtr<FJsonValue>& Value)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("assetPath"), Table->GetPathName());
	Params->SetStringField(TEXT("rowName"), RowName);
	Params->SetStringField(TEXT("fieldName"), FieldName);
	Params->SetField(TEXT("value"), Value);
	return Params;
}

bool ResponseSucceeded(const TSharedPtr<FJsonValue>& Response, FString& OutError)
{
	if (!Response.IsValid() || Response->Type != EJson::Object)
	{
		OutError = TEXT("handler returned no JSON object");
		return false;
	}
	const TSharedPtr<FJsonObject> Obj = Response->AsObject();
	bool bSuccess = false;
	Obj->TryGetBoolField(TEXT("success"), bSuccess);
	if (!bSuccess)
	{
		Obj->TryGetStringField(TEXT("error"), OutError);
	}
	return bSuccess;
}

/** Params for asset(read_datatable) against a transient table. An empty
 *  filter or path leaves that param out, which is the inline, unfiltered read. */
TSharedPtr<FJsonObject> MakeDataTableReadParams(const UDataTable* Table, const FString& RowFilter, const FString& OutputPath)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("assetPath"), Table->GetPathName());
	if (!RowFilter.IsEmpty()) Params->SetStringField(TEXT("rowFilter"), RowFilter);
	if (!OutputPath.IsEmpty()) Params->SetStringField(TEXT("outputPath"), OutputPath);
	return Params;
}

/** One canonical text form of a row array, so rows returned inline and rows
 *  read back from a file compare as strings. */
FString CondenseDataTableRows(const TArray<TSharedPtr<FJsonValue>>& Rows)
{
	FString Text;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	FJsonSerializer::Serialize(Rows, Writer);
	return Text;
}
}

// ─────────────────────────────────────────────────────────────────────────────
// #929: one named field changes, everything else keeps the bytes it had.
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDataTableSingleFieldWritePreservesRowTest,
	"UE.MCP.Asset.DataTable.SingleFieldWritePreservesTheRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDataTableSingleFieldWritePreservesRowTest::RunTest(const FString& Parameters)
{
#if !WITH_EDITORONLY_DATA
	AddInfo(TEXT("FPerPlatformInt::PerPlatform is editor-only data; nothing to assert here."));
	return true;
#else
	// The table lives in the transient package, which is not a registered
	// asset, so the handler's save step declines and says so. That is the
	// expected cost of keeping the test off a real asset.
	AddExpectedError(TEXT("SaveLoadedAsset failed"), EAutomationExpectedErrorFlags::Contains, 0);

	UDataTable* Table = MakeTransientPerPlatformTable();
	if (!TestNotNull(TEXT("transient DataTable was created"), Table)) return false;
	FGCRootScope TableRoot(Table);

	TMap<FName, int32> FirstRowOverrides;
	FirstRowOverrides.Add(TEXT("Windows"), 41);
	FirstRowOverrides.Add(TEXT("Mac"), 42);
	AddPerPlatformRow(Table, TEXT("RowA"), 1, FirstRowOverrides);

	TMap<FName, int32> SecondRowOverrides;
	SecondRowOverrides.Add(TEXT("Windows"), 43);
	AddPerPlatformRow(Table, TEXT("RowB"), 2, SecondRowOverrides);

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("set_datatable_cell is registered"), Registry.HasHandler(TEXT("set_datatable_cell")));

	// Stated up front so a resolution failure reads as one, rather than as a
	// pile of downstream assertions about a row that was never reached.
	if (!TestTrue(
			TEXT("the handler resolves the transient table by path"),
			MCPLoadAssetObject(Table->GetPathName()) == Table))
	{
		return false;
	}

	FString Error;
	const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(
		TEXT("set_datatable_cell"),
		MakeCellWriteParams(Table, TEXT("RowA"), DataTableRowWriteScalarField, MakeShared<FJsonValueNumber>(7)));
	const bool bWritten = ResponseSucceeded(Response, Error);
	if (!TestTrue(FString::Printf(TEXT("cell write succeeded (%s)"), *Error), bWritten))
	{
		return false;
	}

	const FPerPlatformInt* RowA = FindPerPlatformRow(Table, TEXT("RowA"));
	if (!TestNotNull(TEXT("the edited row still exists"), RowA)) return false;

	TestEqual(TEXT("the named field took the new value"), RowA->Default, 7);

	// The assertion the issue is about. PerPlatform declares no default, so a
	// row rebuilt from the struct's defaults comes back empty.
	TestEqual(TEXT("the untouched TMap kept every entry"), RowA->PerPlatform.Num(), 2);
	if (const int32* Windows = RowA->PerPlatform.Find(TEXT("Windows")))
	{
		TestEqual(TEXT("the untouched TMap kept its first value"), *Windows, 41);
	}
	else
	{
		AddError(TEXT("the untouched TMap lost its 'Windows' entry"));
	}
	if (const int32* Mac = RowA->PerPlatform.Find(TEXT("Mac")))
	{
		TestEqual(TEXT("the untouched TMap kept its second value"), *Mac, 42);
	}
	else
	{
		AddError(TEXT("the untouched TMap lost its 'Mac' entry"));
	}

	// A write to one row must not reach any other row.
	const FPerPlatformInt* RowB = FindPerPlatformRow(Table, TEXT("RowB"));
	if (!TestNotNull(TEXT("the row that was not named still exists"), RowB)) return false;
	TestEqual(TEXT("the other row kept its scalar"), RowB->Default, 2);
	TestEqual(TEXT("the other row kept its TMap"), RowB->PerPlatform.Num(), 1);

	TestEqual(TEXT("no row was added or dropped"), Table->GetRowMap().Num(), 2);
	return true;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// #935: a rejected write leaves the row exactly as it was found.
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDataTableRejectedWriteLeavesRowIntactTest,
	"UE.MCP.Asset.DataTable.RejectedWriteLeavesRowIntact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDataTableRejectedWriteLeavesRowIntactTest::RunTest(const FString& Parameters)
{
#if !WITH_EDITORONLY_DATA
	AddInfo(TEXT("FPerPlatformInt::PerPlatform is editor-only data; nothing to assert here."));
	return true;
#else
	UDataTable* Table = MakeTransientPerPlatformTable();
	if (!TestNotNull(TEXT("transient DataTable was created"), Table)) return false;
	FGCRootScope TableRoot(Table);

	TMap<FName, int32> Overrides;
	Overrides.Add(TEXT("Windows"), 41);
	AddPerPlatformRow(Table, TEXT("RowA"), 1, Overrides);

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);

	if (!TestTrue(
			TEXT("the handler resolves the transient table by path"),
			MCPLoadAssetObject(Table->GetPathName()) == Table))
	{
		return false;
	}

	// A field the row struct does not have. Nothing may be written, and no
	// save may be attempted, so this test expects no log errors at all.
	FString Error;
	const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(
		TEXT("set_datatable_cell"),
		MakeCellWriteParams(Table, TEXT("RowA"), TEXT("NoSuchField"), MakeShared<FJsonValueNumber>(7)));
	TestFalse(TEXT("a write to an unknown field fails"), ResponseSucceeded(Response, Error));
	TestTrue(TEXT("the error names the field"), Error.Contains(TEXT("NoSuchField")));

	const FPerPlatformInt* RowA = FindPerPlatformRow(Table, TEXT("RowA"));
	if (!TestNotNull(TEXT("the row survived the rejected write"), RowA)) return false;
	TestEqual(TEXT("the scalar is untouched"), RowA->Default, 1);
	TestEqual(TEXT("the TMap is untouched"), RowA->PerPlatform.Num(), 1);
	return true;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// read_datatable with outputPath writes exactly the rows the inline read
// returns, rowFilter applied, and reports the file instead of the rows.
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDataTableReadToFileTest,
	"UE.MCP.Asset.DataTable.ReadToFileRoundTrips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDataTableReadToFileTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = MakeTransientPerPlatformTable();
	if (!TestNotNull(TEXT("transient DataTable was created"), Table)) return false;
	FGCRootScope TableRoot(Table);

	TMap<FName, int32> Overrides;
	Overrides.Add(TEXT("Windows"), 41);
	AddPerPlatformRow(Table, TEXT("AlphaOne"), 1, Overrides);
	AddPerPlatformRow(Table, TEXT("AlphaTwo"), 2, TMap<FName, int32>());
	AddPerPlatformRow(Table, TEXT("Beta"), 3, TMap<FName, int32>());

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("read_datatable is registered"), Registry.HasHandler(TEXT("read_datatable")));

	if (!TestTrue(
			TEXT("the handler resolves the transient table by path"),
			MCPLoadAssetObject(Table->GetPathName()) == Table))
	{
		return false;
	}

	// Every file this test writes lands in one folder of its own under Saved/,
	// removed on the way out whatever the assertions found.
	const FString DumpFolder = FString::Printf(
		TEXT("UE_MCP/AutomationTests/ReadDataTable_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString DumpDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), DumpFolder));
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*DumpDirectory, false, true);
	};

	auto Read = [this, &Registry, Table](const TCHAR* Label, const FString& RowFilter, const FString& OutputPath)
		-> TSharedPtr<FJsonObject>
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(
			TEXT("read_datatable"), MakeDataTableReadParams(Table, RowFilter, OutputPath));
		FString Error;
		const bool bSucceeded = ResponseSucceeded(Response, Error);
		if (!TestTrue(FString::Printf(TEXT("%s succeeded (%s)"), Label, *Error), bSucceeded)) return nullptr;
		return Response->AsObject();
	};

	auto LoadRows = [this](const FString& Path, TArray<TSharedPtr<FJsonValue>>& OutRows) -> bool
	{
		FString Text;
		if (!TestTrue(FString::Printf(TEXT("the file can be read back (%s)"), *Path), FFileHelper::LoadFileToString(Text, *Path)))
		{
			return false;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		return TestTrue(TEXT("the file holds a JSON array"), FJsonSerializer::Deserialize(Reader, OutRows));
	};

	// ── The inline reads are the reference the files must match, and the
	// shape they return must not have moved.
	const TSharedPtr<FJsonObject> InlineAll = Read(TEXT("the inline read"), FString(), FString());
	const TSharedPtr<FJsonObject> InlineFiltered = Read(TEXT("the filtered inline read"), TEXT("alpha"), FString());
	if (!InlineAll.IsValid() || !InlineFiltered.IsValid()) return false;

	const TArray<TSharedPtr<FJsonValue>>* InlineAllRows = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* InlineFilteredRows = nullptr;
	if (!TestTrue(TEXT("the inline read returns rows"), InlineAll->TryGetArrayField(TEXT("rows"), InlineAllRows))) return false;
	if (!TestTrue(TEXT("the filtered inline read returns rows"), InlineFiltered->TryGetArrayField(TEXT("rows"), InlineFilteredRows))) return false;
	TestEqual(TEXT("the inline read returns every row"), InlineAllRows->Num(), 3);
	TestEqual(TEXT("the filter keeps the two matching rows"), InlineFilteredRows->Num(), 2);
	TestTrue(TEXT("the inline read still lists rowNames"), InlineAll->HasField(TEXT("rowNames")));
	TestFalse(TEXT("the inline read names no file"), InlineAll->HasField(TEXT("outputPath")));

	// ── A relative outputPath resolves under Saved/, creating the folder, and
	// the file holds the filtered rows and nothing else.
	{
		const FString Requested = DumpFolder / TEXT("filtered.json");
		const FString Expected = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), Requested));
		const TSharedPtr<FJsonObject> Written = Read(TEXT("the filtered read to a relative path"), TEXT("alpha"), Requested);
		if (!Written.IsValid()) return false;

		FString OutputPath;
		Written->TryGetStringField(TEXT("outputPath"), OutputPath);
		TestEqual(TEXT("a relative path resolves under Saved/ and comes back absolute"), OutputPath, Expected);
		TestFalse(TEXT("the rows do not come back inline"), Written->HasField(TEXT("rows")));
		TestFalse(TEXT("nor does the per-row name list"), Written->HasField(TEXT("rowNames")));

		int32 RowCount = -1;
		Written->TryGetNumberField(TEXT("rowCount"), RowCount);
		TestEqual(TEXT("rowCount counts the rows after the filter"), RowCount, 2);
		int32 TotalRowCount = -1;
		Written->TryGetNumberField(TEXT("totalRowCount"), TotalRowCount);
		TestEqual(TEXT("totalRowCount still counts the whole table"), TotalRowCount, 3);
		FString RowStruct;
		Written->TryGetStringField(TEXT("rowStruct"), RowStruct);
		TestEqual(TEXT("rowStruct names the row struct"), RowStruct, FString(TEXT("PerPlatformInt")));
		int64 Bytes = -1;
		Written->TryGetNumberField(TEXT("bytes"), Bytes);
		TestEqual(TEXT("bytes is the size of the file on disk"), Bytes, IFileManager::Get().FileSize(*Expected));
		TestTrue(TEXT("and the file is not empty"), Bytes > 0);

		TArray<TSharedPtr<FJsonValue>> FileRows;
		if (LoadRows(Expected, FileRows))
		{
			TestEqual(
				TEXT("the file holds the rows the filtered inline read returned"),
				CondenseDataTableRows(FileRows),
				CondenseDataTableRows(*InlineFilteredRows));
		}
	}

	// ── An absolute outputPath is used as given, a missing nested folder is
	// created, and without a filter every row is written.
	const FString AbsolutePath = DumpDirectory / TEXT("nested") / TEXT("all.json");
	{
		const TSharedPtr<FJsonObject> Written = Read(TEXT("the unfiltered read to an absolute path"), FString(), AbsolutePath);
		if (!Written.IsValid()) return false;

		FString OutputPath;
		Written->TryGetStringField(TEXT("outputPath"), OutputPath);
		TestEqual(TEXT("an absolute path is used as given"), OutputPath, AbsolutePath);
		TestFalse(TEXT("no filteredCount without a filter"), Written->HasField(TEXT("filteredCount")));
		int32 RowCount = -1;
		Written->TryGetNumberField(TEXT("rowCount"), RowCount);
		TestEqual(TEXT("rowCount counts every row"), RowCount, 3);

		TArray<TSharedPtr<FJsonValue>> FileRows;
		if (LoadRows(AbsolutePath, FileRows))
		{
			TestEqual(
				TEXT("the file holds the rows the inline read returned"),
				CondenseDataTableRows(FileRows),
				CondenseDataTableRows(*InlineAllRows));
		}
	}

	// ── Writing to the same path again replaces the file rather than
	// appending to it or refusing.
	{
		const TSharedPtr<FJsonObject> Written = Read(TEXT("the second read to the same path"), TEXT("beta"), AbsolutePath);
		if (!Written.IsValid()) return false;

		TArray<TSharedPtr<FJsonValue>> FileRows;
		if (LoadRows(AbsolutePath, FileRows))
		{
			TestEqual(TEXT("the file now holds only the second read's rows"), FileRows.Num(), 1);
		}
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// #935: reference kinds are classified most-derived first, so the branch that
// knows how to resolve a class is the one that runs for a class field.
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FJsonPropertyReferenceKindTest,
	"UE.MCP.Property.ReferenceKindClassification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJsonPropertyReferenceKindTest::RunTest(const FString& Parameters)
{
	using MCPJsonProperty::ERefKind;
	using MCPJsonProperty::ClassifyReference;

	UScriptStruct* TypeInfo = FPrimaryAssetTypeInfo::StaticStruct();

	FProperty* SoftClassProp = TypeInfo->FindPropertyByName(TEXT("AssetBaseClass"));
	FProperty* HardClassProp = TypeInfo->FindPropertyByName(TEXT("AssetBaseClassLoaded"));
	FProperty* NameProp = TypeInfo->FindPropertyByName(TEXT("PrimaryAssetType"));
	FProperty* ObjectProp = AActor::StaticClass()->FindPropertyByName(TEXT("RootComponent"));
	FProperty* SubclassOfProp = AGameModeBase::StaticClass()->FindPropertyByName(TEXT("DefaultPawnClass"));

	if (!TestNotNull(TEXT("FPrimaryAssetTypeInfo::AssetBaseClass exists"), SoftClassProp)) return false;
	if (!TestNotNull(TEXT("FPrimaryAssetTypeInfo::AssetBaseClassLoaded exists"), HardClassProp)) return false;
	if (!TestNotNull(TEXT("FPrimaryAssetTypeInfo::PrimaryAssetType exists"), NameProp)) return false;
	if (!TestNotNull(TEXT("AActor::RootComponent exists"), ObjectProp)) return false;
	if (!TestNotNull(TEXT("AGameModeBase::DefaultPawnClass exists"), SubclassOfProp)) return false;

	// The whole point: FClassProperty derives from FObjectProperty and
	// FSoftClassProperty from FSoftObjectProperty, so a chain that tested the
	// base first answered "Object" and "SoftObject" for these two.
	TestTrue(TEXT("a UClass* field classifies as a class"), ClassifyReference(HardClassProp) == ERefKind::Class);
	TestTrue(TEXT("a TSubclassOf<> field classifies as a class"), ClassifyReference(SubclassOfProp) == ERefKind::Class);
	TestTrue(TEXT("a TSoftClassPtr<> field classifies as a soft class"), ClassifyReference(SoftClassProp) == ERefKind::SoftClass);
	TestTrue(TEXT("an object pointer classifies as an object"), ClassifyReference(ObjectProp) == ERefKind::Object);
	TestTrue(TEXT("a name is not a reference"), ClassifyReference(NameProp) == ERefKind::NotAReference);
	TestTrue(TEXT("a null property is not a reference"), ClassifyReference(nullptr) == ERefKind::NotAReference);

	// A native class path resolves as written, and a class-typed field takes it.
	UClass* Resolved = MCPJsonProperty::ResolveClassPath(TEXT("/Script/Engine.DefaultPawn"));
	TestTrue(TEXT("a native class path resolves without a suffix"), Resolved == ADefaultPawn::StaticClass());

	FDefaultConstructedPropertyElement HardClassValue(SubclassOfProp);
	FString SetError;
	const bool bSet = MCPJsonProperty::SetJsonOnProperty(
		SubclassOfProp,
		HardClassValue.GetObjAddress(),
		MakeShared<FJsonValueString>(TEXT("/Script/Engine.DefaultPawn")),
		SetError);
	if (TestTrue(FString::Printf(TEXT("a native class path is accepted (%s)"), *SetError), bSet))
	{
		UObject* Stored = CastField<FClassProperty>(SubclassOfProp)->GetObjectPropertyValue(HardClassValue.GetObjAddress());
		TestTrue(TEXT("the class field stores the class that was named"), Stored == ADefaultPawn::StaticClass());
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// #928: a native class path is stored as written, with no Blueprint suffix.
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoftClassPathSuffixTest,
	"UE.MCP.Property.SoftClassPathKeepsNativePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSoftClassPathSuffixTest::RunTest(const FString& Parameters)
{
	FProperty* SoftClassProp = FPrimaryAssetTypeInfo::StaticStruct()->FindPropertyByName(TEXT("AssetBaseClass"));
	if (!TestNotNull(TEXT("FPrimaryAssetTypeInfo::AssetBaseClass exists"), SoftClassProp)) return false;

	FDefaultConstructedPropertyElement SoftClassValue(SoftClassProp);
	const TSharedPtr<FJsonValue> Requested = MakeShared<FJsonValueString>(TEXT("/Script/Engine.DefaultPawn"));

	FString SetError;
	const bool bSet = MCPJsonProperty::SetJsonOnProperty(
		SoftClassProp, SoftClassValue.GetObjAddress(), Requested, SetError);
	if (!TestTrue(FString::Printf(TEXT("the soft class path is accepted (%s)"), *SetError), bSet)) return false;

	const FString Stored =
		CastField<FSoftClassProperty>(SoftClassProp)->GetPropertyValue(SoftClassValue.GetObjAddress()).ToString();
	TestEqual(TEXT("the native path is stored verbatim"), Stored, FString(TEXT("/Script/Engine.DefaultPawn")));
	TestFalse(TEXT("no Blueprint suffix was appended"), Stored.EndsWith(TEXT("_C")));

	// Readback verification agrees, and notices when the stored value is not
	// the one that was asked for.
	FString Detail;
	TestTrue(
		TEXT("verification accepts the value that was stored"),
		MCPJsonProperty::VerifyJsonOnProperty(SoftClassProp, SoftClassValue.GetObjAddress(), Requested, Detail));

	CastField<FSoftClassProperty>(SoftClassProp)->SetPropertyValue(
		SoftClassValue.GetObjAddress(), FSoftObjectPtr(FSoftObjectPath(TEXT("/Script/Engine.DefaultPawn_C"))));
	Detail.Reset();
	TestFalse(
		TEXT("verification rejects a path that was rewritten"),
		MCPJsonProperty::VerifyJsonOnProperty(SoftClassProp, SoftClassValue.GetObjAddress(), Requested, Detail));
	TestTrue(TEXT("the mismatch reports what was stored"), Detail.Contains(TEXT("_C")));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// The path comparison the readback verification depends on.
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetPathCompareTest,
	"UE.MCP.Property.AssetPathComparison",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetPathCompareTest::RunTest(const FString& Parameters)
{
	using MCPJsonProperty::NormalizeAssetPathForCompare;

	// A Blueprint asset path, its long form, and its generated class all name
	// the same thing.
	const FString Expected(TEXT("/Game/Foo/BP_Thing"));
	TestEqual(TEXT("short form"), NormalizeAssetPathForCompare(TEXT("/Game/Foo/BP_Thing")), Expected);
	TestEqual(TEXT("long form"), NormalizeAssetPathForCompare(TEXT("/Game/Foo/BP_Thing.BP_Thing")), Expected);
	TestEqual(TEXT("generated class"), NormalizeAssetPathForCompare(TEXT("/Game/Foo/BP_Thing.BP_Thing_C")), Expected);

	// A native class path keeps its object name: the package leaf and the
	// object name are different, so there is nothing to collapse.
	TestEqual(
		TEXT("native class path"),
		NormalizeAssetPathForCompare(TEXT("/Script/Engine.DefaultPawn")),
		FString(TEXT("/Script/Engine.DefaultPawn")));

	// #928 in one line: the corrupted form of that path stays a different
	// path, so the readback check can see it.
	TestNotEqual(
		TEXT("a suffixed native class path is not the same path"),
		NormalizeAssetPathForCompare(TEXT("/Script/Engine.DefaultPawn_C")),
		NormalizeAssetPathForCompare(TEXT("/Script/Engine.DefaultPawn")));

	// An asset whose own name ends in _C still compares equal to itself.
	TestEqual(
		TEXT("an asset named with a _C suffix"),
		NormalizeAssetPathForCompare(TEXT("/Game/Foo_C.Foo_C_C")),
		NormalizeAssetPathForCompare(TEXT("/Game/Foo_C")));

	// Two different assets do not.
	TestNotEqual(
		TEXT("different assets stay different"),
		NormalizeAssetPathForCompare(TEXT("/Game/Foo/BP_Thing")),
		NormalizeAssetPathForCompare(TEXT("/Game/Foo/BP_Other")));

	// A subobject path is compared whole.
	TestEqual(
		TEXT("subobject path"),
		NormalizeAssetPathForCompare(TEXT("/Game/Foo/BP_Thing.BP_Thing_C:Comp")),
		FString(TEXT("/Game/Foo/BP_Thing.BP_Thing_C:Comp")));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// #935: readback verification notices a container that did not store what it
// was handed, which is the shape the DataTable writes report on.
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReadbackVerificationTest,
	"UE.MCP.Property.ReadbackVerification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReadbackVerificationTest::RunTest(const FString& Parameters)
{
#if !WITH_EDITORONLY_DATA
	AddInfo(TEXT("FPerPlatformInt::PerPlatform is editor-only data; nothing to assert here."));
	return true;
#else
	FProperty* MapProp = FPerPlatformInt::StaticStruct()->FindPropertyByName(DataTableRowWriteMapField);
	if (!TestNotNull(TEXT("FPerPlatformInt::PerPlatform exists"), MapProp)) return false;

	TSharedPtr<FJsonObject> MapJson = MakeShared<FJsonObject>();
	MapJson->SetNumberField(TEXT("Windows"), 41);
	MapJson->SetNumberField(TEXT("Mac"), 42);
	const TSharedPtr<FJsonValue> Requested = MakeShared<FJsonValueObject>(MapJson);

	FPerPlatformInt Value;
	void* MapAddr = MapProp->ContainerPtrToValuePtr<void>(&Value);

	FString SetError;
	if (!TestTrue(
			FString::Printf(TEXT("the map is written (%s)"), *SetError),
			MCPJsonProperty::SetJsonOnProperty(MapProp, MapAddr, Requested, SetError)))
	{
		return false;
	}

	FString Detail;
	TestTrue(
		TEXT("verification accepts a map that stored every pair"),
		MCPJsonProperty::VerifyJsonOnProperty(MapProp, MapAddr, Requested, Detail));

	// Drop an entry behind the setter's back: this is what a write that
	// reported success and stored less than it was handed looks like.
	Value.PerPlatform.Remove(TEXT("Mac"));
	Detail.Reset();
	TestFalse(
		TEXT("verification rejects a map that lost a pair"),
		MCPJsonProperty::VerifyJsonOnProperty(MapProp, MapAddr, Requested, Detail));
	TestTrue(TEXT("the mismatch says what was asked for"), Detail.Contains(TEXT("Windows")));
	return true;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// A number written to a numeric property arrives as a number.
//
// The setter used to render every scalar as text before importing it, and a
// JSON number renders through FJsonValueNumber::TryGetString, which is
// FString::SanitizeFloat, which is Printf("%f"): six fractional digits and no
// more. A double asked for 1e-9 stored 0 and one asked for 0.123456789 stored
// 0.123457, in the property itself and in every component of a struct written
// through the same recursion.
//
// Every comparison below is exact. A tolerance is what let this hide.
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FJsonPropertyNumericPrecisionTest,
	"UE.MCP.Property.NumericWritesKeepTheirPrecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJsonPropertyNumericPrecisionTest::RunTest(const FString& Parameters)
{
	// ── Doubles. FVector::X is FLargeWorldCoordinatesReal, which is a double.
	FProperty* DoubleProp = TBaseStructure<FVector>::Get()->FindPropertyByName(TEXT("X"));
	if (!TestNotNull(TEXT("FVector::X exists"), DoubleProp)) return false;
	if (!TestNotNull(TEXT("FVector::X is a double property"), CastField<FDoubleProperty>(DoubleProp))) return false;

	auto WriteDouble = [this, DoubleProp](double Requested) -> double
	{
		FDefaultConstructedPropertyElement Buffer(DoubleProp);
		FString Error;
		if (!MCPJsonProperty::SetJsonOnProperty(
				DoubleProp, Buffer.GetObjAddress(), MakeShared<FJsonValueNumber>(Requested), Error))
		{
			AddError(FString::Printf(TEXT("writing %.17g to a double failed: %s"), Requested, *Error));
			return 0.0;
		}
		return CastField<FDoubleProperty>(DoubleProp)->GetPropertyValue(Buffer.GetObjAddress());
	};

	TestTrue(TEXT("1e-9 lands as 1e-9 and not as zero"), WriteDouble(1e-9) == 1e-9);
	TestTrue(TEXT("0.123456789 keeps every digit"), WriteDouble(0.123456789) == 0.123456789);
	TestTrue(TEXT("a negative fraction keeps every digit"), WriteDouble(-2.0000000001) == -2.0000000001);
	TestTrue(TEXT("a whole number is unchanged"), WriteDouble(42.0) == 42.0);
	TestTrue(TEXT("zero is unchanged"), WriteDouble(0.0) == 0.0);

	// ── Floats. The double narrows to the property's own precision, which is
	// what the property's type means, and nothing else is lost on the way.
	FProperty* FloatProp = FPerPlatformFloat::StaticStruct()->FindPropertyByName(TEXT("Default"));
	if (!TestNotNull(TEXT("FPerPlatformFloat::Default exists"), FloatProp)) return false;
	if (!TestNotNull(TEXT("FPerPlatformFloat::Default is a float property"), CastField<FFloatProperty>(FloatProp))) return false;
	{
		FDefaultConstructedPropertyElement Buffer(FloatProp);
		FString Error;
		if (TestTrue(
				TEXT("a float takes a fractional number"),
				MCPJsonProperty::SetJsonOnProperty(
					FloatProp, Buffer.GetObjAddress(), MakeShared<FJsonValueNumber>(0.123456789), Error)))
		{
			const float Stored = CastField<FFloatProperty>(FloatProp)->GetPropertyValue(Buffer.GetObjAddress());
			TestTrue(TEXT("a float keeps all the precision a float has"), Stored == (float)0.123456789);
		}
	}

	// ── int32. A value that is not the integer the caller named is refused,
	// never rounded or truncated into a different one.
	FProperty* IntProp = FPerPlatformInt::StaticStruct()->FindPropertyByName(TEXT("Default"));
	if (!TestNotNull(TEXT("FPerPlatformInt::Default exists"), IntProp)) return false;
	if (!TestNotNull(TEXT("FPerPlatformInt::Default is an int32 property"), CastField<FIntProperty>(IntProp))) return false;
	{
		FDefaultConstructedPropertyElement Buffer(IntProp);
		FString Error;
		if (TestTrue(
				TEXT("an int32 takes a whole number"),
				MCPJsonProperty::SetJsonOnProperty(
					IntProp, Buffer.GetObjAddress(), MakeShared<FJsonValueNumber>(42), Error)))
		{
			TestEqual(TEXT("the int32 holds the number that was written"),
				CastField<FIntProperty>(IntProp)->GetPropertyValue(Buffer.GetObjAddress()), 42);
		}

		Error.Reset();
		TestFalse(
			TEXT("an int32 refuses a fractional number"),
			MCPJsonProperty::SetJsonOnProperty(
				IntProp, Buffer.GetObjAddress(), MakeShared<FJsonValueNumber>(2.5), Error));
		TestTrue(TEXT("and says the value is not whole"), Error.Contains(TEXT("whole number")));
		TestEqual(TEXT("the refused write left the previous value alone"),
			CastField<FIntProperty>(IntProp)->GetPropertyValue(Buffer.GetObjAddress()), 42);

		Error.Reset();
		TestFalse(
			TEXT("an int32 refuses a value it cannot hold"),
			MCPJsonProperty::SetJsonOnProperty(
				IntProp, Buffer.GetObjAddress(), MakeShared<FJsonValueNumber>(1e10), Error));
		TestTrue(TEXT("and says it is out of range"), Error.Contains(TEXT("out of range")));

		Error.Reset();
		TestFalse(
			TEXT("an int32 refuses a negative value it cannot hold"),
			MCPJsonProperty::SetJsonOnProperty(
				IntProp, Buffer.GetObjAddress(), MakeShared<FJsonValueNumber>(-1e10), Error));
		TestTrue(TEXT("and says it is out of range"), Error.Contains(TEXT("out of range")));
	}

	// ── int64. A JSON number is a double, so above 2^53 it no longer carries
	// every integer: the write is refused rather than storing a neighbour, and
	// the same number sent as a JSON string lands at full width.
#if UE_MCP_HAS_5_5_API
	FProperty* Int64Prop = TBaseStructure<FInt64Vector2>::Get()->FindPropertyByName(TEXT("X"));
#else
	// 5.4 has no TBaseStructure specialisation for FInt64Vector2; its script
	// struct is still registered, so it is found by name instead.
	UScriptStruct* Int64VectorStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/CoreUObject.Int64Vector2"));
	if (!TestNotNull(TEXT("FInt64Vector2 script struct exists"), Int64VectorStruct)) return false;
	FProperty* Int64Prop = Int64VectorStruct->FindPropertyByName(TEXT("X"));
#endif
	if (!TestNotNull(TEXT("FInt64Vector2::X exists"), Int64Prop)) return false;
	if (!TestNotNull(TEXT("FInt64Vector2::X is an int64 property"), CastField<FInt64Property>(Int64Prop))) return false;
	{
		FDefaultConstructedPropertyElement Buffer(Int64Prop);
		FString Error;
		const int64 ExactLimit = 9007199254740992LL; // 2^53
		if (TestTrue(
				TEXT("an int64 takes the largest integer a JSON number carries exactly"),
				MCPJsonProperty::SetJsonOnProperty(
					Int64Prop, Buffer.GetObjAddress(), MakeShared<FJsonValueNumber>((double)ExactLimit), Error)))
		{
			TestTrue(TEXT("and stores it exactly"),
				CastField<FInt64Property>(Int64Prop)->GetPropertyValue(Buffer.GetObjAddress()) == ExactLimit);
		}

		Error.Reset();
		TestFalse(
			TEXT("an int64 refuses a number past 2^53"),
			MCPJsonProperty::SetJsonOnProperty(
				Int64Prop, Buffer.GetObjAddress(), MakeShared<FJsonValueNumber>(9007199254740994.0), Error));
		TestTrue(TEXT("and names the escape that does work"), Error.Contains(TEXT("JSON string")));
		TestTrue(TEXT("the refused write left the previous value alone"),
			CastField<FInt64Property>(Int64Prop)->GetPropertyValue(Buffer.GetObjAddress()) == ExactLimit);

		// The escape: as text, every digit survives, which is the whole reason
		// the number form is allowed to refuse.
		Error.Reset();
		if (TestTrue(
				TEXT("an int64 takes a big integer written as a string"),
				MCPJsonProperty::SetJsonOnProperty(
					Int64Prop, Buffer.GetObjAddress(),
					MakeShared<FJsonValueString>(TEXT("9007199254740993")), Error)))
		{
			TestTrue(TEXT("and stores the odd integer a double could not have carried"),
				CastField<FInt64Property>(Int64Prop)->GetPropertyValue(Buffer.GetObjAddress()) == 9007199254740993LL);
		}
	}

	// ── An enum-valued byte is an enum first. It is an FNumericProperty, so it
	// is the one numeric kind the number branch has to step over by hand.
	FProperty* MobilityProp = USceneComponent::StaticClass()->FindPropertyByName(TEXT("Mobility"));
	if (!TestNotNull(TEXT("USceneComponent::Mobility exists"), MobilityProp)) return false;
	{
		FNumericProperty* AsNumeric = CastField<FNumericProperty>(MobilityProp);
		if (!TestNotNull(TEXT("a TEnumAsByte field is a numeric property"), AsNumeric)) return false;
		TestTrue(TEXT("and says it is an enum, which is what keeps it off the number branch"), AsNumeric->IsEnum());

		FDefaultConstructedPropertyElement Buffer(MobilityProp);
		FString Error;
		if (TestTrue(
				TEXT("the enum branch still resolves a full enumerator name"),
				MCPJsonProperty::SetJsonOnProperty(
					MobilityProp, Buffer.GetObjAddress(), MakeShared<FJsonValueString>(TEXT("Movable")), Error)))
		{
			TestTrue(TEXT("and stores that enumerator"),
				CastField<FByteProperty>(MobilityProp)->GetPropertyValue(Buffer.GetObjAddress())
					== (uint8)EComponentMobility::Movable);
		}

		Error.Reset();
		if (TestTrue(
				TEXT("the enum branch still resolves a friendly alias"),
				MCPJsonProperty::SetJsonOnProperty(
					MobilityProp, Buffer.GetObjAddress(), MakeShared<FJsonValueString>(TEXT("static")), Error)))
		{
			TestTrue(TEXT("and stores that enumerator"),
				CastField<FByteProperty>(MobilityProp)->GetPropertyValue(Buffer.GetObjAddress())
					== (uint8)EComponentMobility::Static);
		}
	}

	// ── An enum class is not an FNumericProperty at all, so no number branch
	// can reach it whatever the value looks like.
	FProperty* EnumProp = AActor::StaticClass()->FindPropertyByName(TEXT("SpawnCollisionHandlingMethod"));
	if (!TestNotNull(TEXT("AActor::SpawnCollisionHandlingMethod exists"), EnumProp)) return false;
	{
		if (!TestNotNull(TEXT("an enum class field is an enum property"), CastField<FEnumProperty>(EnumProp))) return false;
		TestNull(TEXT("an enum class field is not a numeric property"), CastField<FNumericProperty>(EnumProp));

		FDefaultConstructedPropertyElement Buffer(EnumProp);
		FString Error;
		if (TestTrue(
				TEXT("the enum branch still resolves an enum class name"),
				MCPJsonProperty::SetJsonOnProperty(
					EnumProp, Buffer.GetObjAddress(), MakeShared<FJsonValueString>(TEXT("AlwaysSpawn")), Error)))
		{
			TestTrue(TEXT("and stores that enumerator"),
				CastField<FEnumProperty>(EnumProp)->GetUnderlyingProperty()->GetSignedIntPropertyValue(Buffer.GetObjAddress())
					== (int64)ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		}
	}

	// ── A bool is not an FNumericProperty either.
	FProperty* BoolProp = FPerPlatformBool::StaticStruct()->FindPropertyByName(TEXT("Default"));
	if (!TestNotNull(TEXT("FPerPlatformBool::Default exists"), BoolProp)) return false;
	{
		if (!TestNotNull(TEXT("a bool field is a bool property"), CastField<FBoolProperty>(BoolProp))) return false;
		TestNull(TEXT("a bool field is not a numeric property"), CastField<FNumericProperty>(BoolProp));

		FDefaultConstructedPropertyElement Buffer(BoolProp);
		FString Error;
		if (TestTrue(
				TEXT("a bool takes true"),
				MCPJsonProperty::SetJsonOnProperty(
					BoolProp, Buffer.GetObjAddress(), MakeShared<FJsonValueBoolean>(true), Error)))
		{
			TestTrue(TEXT("and stores true"),
				CastField<FBoolProperty>(BoolProp)->GetPropertyValue(Buffer.GetObjAddress()));
		}
		Error.Reset();
		if (TestTrue(
				TEXT("a bool takes false"),
				MCPJsonProperty::SetJsonOnProperty(
					BoolProp, Buffer.GetObjAddress(), MakeShared<FJsonValueBoolean>(false), Error)))
		{
			TestFalse(TEXT("and stores false"),
				CastField<FBoolProperty>(BoolProp)->GetPropertyValue(Buffer.GetObjAddress()));
		}
	}

	// ── A struct written field by field recurses through the same setter, so
	// every component has to keep its digits too. This is the case that made a
	// location or a transform drift.
	FProperty* VectorProp = USceneComponent::StaticClass()->FindPropertyByName(TEXT("RelativeLocation"));
	if (!TestNotNull(TEXT("USceneComponent::RelativeLocation exists"), VectorProp)) return false;
	{
		FDefaultConstructedPropertyElement Buffer(VectorProp);
		TSharedPtr<FJsonObject> VectorJson = MakeShared<FJsonObject>();
		VectorJson->SetNumberField(TEXT("X"), 0.123456789);
		VectorJson->SetNumberField(TEXT("Y"), 1e-9);
		VectorJson->SetNumberField(TEXT("Z"), -3.0000000001);
		const TSharedPtr<FJsonValue> Requested = MakeShared<FJsonValueObject>(VectorJson);

		FString Error;
		if (TestTrue(
				FString::Printf(TEXT("the vector is written (%s)"), *Error),
				MCPJsonProperty::SetJsonOnProperty(VectorProp, Buffer.GetObjAddress(), Requested, Error)))
		{
			const FVector& Stored = *static_cast<const FVector*>(Buffer.GetObjAddress());
			TestTrue(TEXT("X keeps every digit"), Stored.X == 0.123456789);
			TestTrue(TEXT("Y keeps every digit"), Stored.Y == 1e-9);
			TestTrue(TEXT("Z keeps every digit"), Stored.Z == -3.0000000001);

			// The readback check compares numerics exactly, so it is the thing
			// that reported a lossy round trip while the text path was in use.
			FString Detail;
			TestTrue(
				FString::Printf(TEXT("the stored vector verifies against the request (%s)"), *Detail),
				MCPJsonProperty::VerifyJsonOnProperty(VectorProp, Buffer.GetObjAddress(), Requested, Detail));
		}
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// The read half. A numeric property comes back as a JSON number, not as a
// quoted string, for every width a double carries exactly.
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FJsonPropertyNumericReadTest,
	"UE.MCP.Property.NumericReadsComeBackAsNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJsonPropertyNumericReadTest::RunTest(const FString& Parameters)
{
	// An int32 and a double were already emitted as numbers; they are here so a
	// later edit cannot quietly turn them back into text.
	FProperty* IntProp = FPerPlatformInt::StaticStruct()->FindPropertyByName(TEXT("Default"));
	FProperty* DoubleProp = TBaseStructure<FVector>::Get()->FindPropertyByName(TEXT("X"));
	if (!TestNotNull(TEXT("FPerPlatformInt::Default exists"), IntProp)) return false;
	if (!TestNotNull(TEXT("FVector::X exists"), DoubleProp)) return false;

	{
		FDefaultConstructedPropertyElement Buffer(IntProp);
		CastField<FIntProperty>(IntProp)->SetPropertyValue(Buffer.GetObjAddress(), -7);
		const TSharedPtr<FJsonValue> Read = FMCPJsonSerializer::SerializeValue(Buffer.GetObjAddress(), IntProp);
		if (TestTrue(TEXT("an int32 reads back as a number"), Read.IsValid() && Read->Type == EJson::Number))
		{
			TestTrue(TEXT("and carries the value"), Read->AsNumber() == -7.0);
		}
	}
	{
		FDefaultConstructedPropertyElement Buffer(DoubleProp);
		CastField<FDoubleProperty>(DoubleProp)->SetPropertyValue(Buffer.GetObjAddress(), 0.123456789);
		const TSharedPtr<FJsonValue> Read = FMCPJsonSerializer::SerializeValue(Buffer.GetObjAddress(), DoubleProp);
		if (TestTrue(TEXT("a double reads back as a number"), Read.IsValid() && Read->Type == EJson::Number))
		{
			TestTrue(TEXT("and keeps every digit"), Read->AsNumber() == 0.123456789);
		}
	}

	// A uint32 is the width that used to fall through to exported text and come
	// back quoted. The struct is looked up by path because the core integer
	// vector variants have no StaticStruct() of their own.
	if (UScriptStruct* Uint32Vector = FindObject<UScriptStruct>(nullptr, TEXT("/Script/CoreUObject.Uint32Vector")))
	{
		FProperty* UintProp = Uint32Vector->FindPropertyByName(TEXT("X"));
		if (TestNotNull(TEXT("FUint32Vector::X exists"), UintProp))
		{
			FDefaultConstructedPropertyElement Buffer(UintProp);
			CastField<FNumericProperty>(UintProp)->SetIntPropertyValue(Buffer.GetObjAddress(), (uint64)4294967295u);
			const TSharedPtr<FJsonValue> Read = FMCPJsonSerializer::SerializeValue(Buffer.GetObjAddress(), UintProp);
			if (TestTrue(TEXT("a uint32 reads back as a number"), Read.IsValid() && Read->Type == EJson::Number))
			{
				TestTrue(TEXT("and carries the whole value"), Read->AsNumber() == 4294967295.0);
			}
		}
	}
	else
	{
		AddInfo(TEXT("FUint32Vector is not registered in this build; the uint32 read is not asserted here."));
	}

	// An enum still reads back as its name, which is the form the setter takes.
	FProperty* MobilityProp = USceneComponent::StaticClass()->FindPropertyByName(TEXT("Mobility"));
	if (TestNotNull(TEXT("USceneComponent::Mobility exists"), MobilityProp))
	{
		FDefaultConstructedPropertyElement Buffer(MobilityProp);
		CastField<FByteProperty>(MobilityProp)->SetPropertyValue(Buffer.GetObjAddress(), (uint8)EComponentMobility::Movable);
		const TSharedPtr<FJsonValue> Read = FMCPJsonSerializer::SerializeValue(Buffer.GetObjAddress(), MobilityProp);
		if (TestTrue(TEXT("an enum byte reads back as a string"), Read.IsValid() && Read->Type == EJson::String))
		{
			TestTrue(TEXT("and names the enumerator"), Read->AsString().Contains(TEXT("Movable")));
		}
	}

	// A bool still reads back as a bool.
	FProperty* BoolProp = FPerPlatformBool::StaticStruct()->FindPropertyByName(TEXT("Default"));
	if (TestNotNull(TEXT("FPerPlatformBool::Default exists"), BoolProp))
	{
		FDefaultConstructedPropertyElement Buffer(BoolProp);
		CastField<FBoolProperty>(BoolProp)->SetPropertyValue(Buffer.GetObjAddress(), true);
		const TSharedPtr<FJsonValue> Read = FMCPJsonSerializer::SerializeValue(Buffer.GetObjAddress(), BoolProp);
		if (TestTrue(TEXT("a bool reads back as a bool"), Read.IsValid() && Read->Type == EJson::Boolean))
		{
			TestTrue(TEXT("and carries the value"), Read->AsBool());
		}
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// "None" is what the engine's own DataTable JSON export writes for a reference
// that is not set, so a row exported, edited and written back carries it. It
// clears the reference exactly as "" does, and the readback check agrees.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
const TCHAR* const EmptyRefTexturePath = TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture");
const TCHAR* const EmptyRefSoftClassPath = TEXT("/Script/Engine.DefaultPawn");
const TCHAR* const EmptyRefRowName = TEXT("EmptyReferenceRow");

/** Roots a transient table for the length of one test and discards it on
 *  every way out, so a failed assertion does not leave it behind. */
struct FTransientDataTableScope
{
	UDataTable* Table = nullptr;

	explicit FTransientDataTableScope(UDataTable* InTable) : Table(InTable)
	{
		if (Table) Table->AddToRoot();
	}
	~FTransientDataTableScope()
	{
		if (!Table) return;
		Table->EmptyTable();
		Table->RemoveFromRoot();
		Table->MarkAsGarbage();
	}
	FTransientDataTableScope(const FTransientDataTableScope&) = delete;
	FTransientDataTableScope& operator=(const FTransientDataTableScope&) = delete;
};

FUEMCPDataTableReferenceRow* FindReferenceRow(const UDataTable* Table, const TCHAR* RowName)
{
	uint8* const* Found = Table->GetRowMap().Find(FName(RowName));
	return (Found && *Found) ? reinterpret_cast<FUEMCPDataTableReferenceRow*>(*Found) : nullptr;
}

/** Params for asset(fill_datatable_from_json) writing one field of one row. */
TSharedPtr<FJsonObject> MakeFillParams(
	const UDataTable* Table,
	const TCHAR* RowName,
	const FString& FieldName,
	const TSharedPtr<FJsonValue>& Value)
{
	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetField(FieldName, Value);
	TSharedPtr<FJsonObject> Rows = MakeShared<FJsonObject>();
	Rows->SetObjectField(RowName, Fields);
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("assetPath"), Table->GetPathName());
	Params->SetObjectField(TEXT("rows"), Rows);
	return Params;
}

/** Read straight off the typed row, so the answer does not come from the
 *  reflection code the verifier itself uses. */
bool ReferenceFieldIsEmpty(const FUEMCPDataTableReferenceRow& Row, const FString& FieldName)
{
	if (FieldName == TEXT("Icon")) return Row.Icon.IsNull();
	if (FieldName == TEXT("SoftClass")) return Row.SoftClass.IsNull();
	if (FieldName == TEXT("HardObject")) return Row.HardObject.Get() == nullptr;
	if (FieldName == TEXT("HardClass")) return Row.HardClass.Get() == nullptr;
	return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDataTableEmptyReferenceSpellingsTest,
	"UE.MCP.Asset.DataTable.EmptyReferenceSpellingsClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDataTableEmptyReferenceSpellingsTest::RunTest(const FString& Parameters)
{
	using MCPJsonProperty::IsEmptyReferenceText;
	using MCPJsonProperty::VerifyJsonOnProperty;

	// The spellings themselves. "none" is a path, as it is to FSoftObjectPath.
	TestTrue(TEXT("\"None\" is an empty reference"), IsEmptyReferenceText(TEXT("None")));
	TestTrue(TEXT("\"\" is an empty reference"), IsEmptyReferenceText(TEXT("")));
	TestFalse(TEXT("\"none\" is not"), IsEmptyReferenceText(TEXT("none")));
	TestFalse(TEXT("an asset path is not"), IsEmptyReferenceText(EmptyRefTexturePath));

	// Successful writes reach the save step, which declines for a transient
	// table. That is the cost of keeping the test off a real asset.
	AddExpectedError(TEXT("SaveLoadedAsset failed"), EAutomationExpectedErrorFlags::Contains, 0);

	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, EmptyRefTexturePath);
	if (!TestNotNull(TEXT("the engine's default texture loads"), Texture)) return false;

	const FName TableName(*FString::Printf(TEXT("DT_UEMCP_EmptyRef_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), TableName);
	if (!TestNotNull(TEXT("transient DataTable was created"), Table)) return false;
	const FTransientDataTableScope TableScope(Table);
	Table->RowStruct = FUEMCPDataTableReferenceRow::StaticStruct();

	{
		FUEMCPDataTableReferenceRow Seed;
		Seed.Count = 3;
		Table->AddRow(FName(EmptyRefRowName), Seed);
	}

	// Every reference field points at something real before each write, so
	// an empty result can only have come from the write.
	auto SeedReferences = [Table, Texture]()
	{
		if (FUEMCPDataTableReferenceRow* Row = FindReferenceRow(Table, EmptyRefRowName))
		{
			Row->Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(EmptyRefTexturePath));
			Row->SoftClass = TSoftClassPtr<UObject>(FSoftObjectPath(EmptyRefSoftClassPath));
			Row->HardObject = Texture;
			Row->HardClass = ADefaultPawn::StaticClass();
		}
	};

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("fill_datatable_from_json is registered"), Registry.HasHandler(TEXT("fill_datatable_from_json")));

	if (!TestTrue(
			TEXT("the handler resolves the transient table by path"),
			MCPLoadAssetObject(Table->GetPathName()) == Table))
	{
		return false;
	}

	const TArray<FString> ReferenceFields = { TEXT("Icon"), TEXT("SoftClass"), TEXT("HardObject"), TEXT("HardClass") };
	const TArray<FString> Spellings = { TEXT("None"), TEXT("") };

	for (const FString& Field : ReferenceFields)
	{
		for (const FString& Spelling : Spellings)
		{
			SeedReferences();

			FString Error;
			const bool bWritten = ResponseSucceeded(
				Registry.ExecuteHandler(
					TEXT("fill_datatable_from_json"),
					MakeFillParams(Table, EmptyRefRowName, Field, MakeShared<FJsonValueString>(Spelling))),
				Error);
			TestTrue(
				FString::Printf(TEXT("clearing %s with '%s' succeeds (%s)"), *Field, *Spelling, *Error),
				bWritten);

			const FUEMCPDataTableReferenceRow* Row = FindReferenceRow(Table, EmptyRefRowName);
			if (!TestNotNull(TEXT("the row still exists"), Row)) return false;

			TestTrue(
				FString::Printf(TEXT("%s reads back empty after '%s'"), *Field, *Spelling),
				ReferenceFieldIsEmpty(*Row, Field));
			for (const FString& Other : ReferenceFields)
			{
				if (Other == Field) continue;
				TestFalse(
					FString::Printf(TEXT("clearing %s left %s alone"), *Field, *Other),
					ReferenceFieldIsEmpty(*Row, Other));
			}
			TestEqual(TEXT("the plain value is untouched"), Row->Count, 3);
		}
	}

	// Neither spelling verifies against a reference that still points at an
	// asset. The acceptance is "stored empty", not "any empty spelling".
	SeedReferences();
	FUEMCPDataTableReferenceRow* Row = FindReferenceRow(Table, EmptyRefRowName);
	if (!TestNotNull(TEXT("the row still exists"), Row)) return false;
	const UScriptStruct* RowStruct = Table->GetRowStruct();
	for (const FString& Field : ReferenceFields)
	{
		FProperty* Prop = RowStruct->FindPropertyByName(FName(*Field));
		if (!TestNotNull(FString::Printf(TEXT("%s exists on the row struct"), *Field), Prop)) return false;
		for (const FString& Spelling : Spellings)
		{
			FString Detail;
			TestFalse(
				FString::Printf(TEXT("'%s' does not verify against a %s that is still set"), *Spelling, *Field),
				VerifyJsonOnProperty(Prop, Prop->ContainerPtrToValuePtr<void>(Row), MakeShared<FJsonValueString>(Spelling), Detail));
			TestTrue(TEXT("and the mismatch says what is stored"), Detail.Contains(TEXT("Default")));
		}
	}

	// A plain value is not loosened: a genuine mismatch is still one, and
	// "None" is not a number.
	FProperty* CountProp = RowStruct->FindPropertyByName(TEXT("Count"));
	if (!TestNotNull(TEXT("Count exists on the row struct"), CountProp)) return false;
	void* CountAddr = CountProp->ContainerPtrToValuePtr<void>(Row);
	{
		FString Detail;
		TestTrue(
			TEXT("the stored plain value verifies"),
			VerifyJsonOnProperty(CountProp, CountAddr, MakeShared<FJsonValueNumber>(3), Detail));
		Detail.Reset();
		TestFalse(
			TEXT("a different plain value does not verify"),
			VerifyJsonOnProperty(CountProp, CountAddr, MakeShared<FJsonValueNumber>(7), Detail));
		Detail.Reset();
		TestFalse(
			TEXT("\"None\" does not verify against a plain value"),
			VerifyJsonOnProperty(CountProp, CountAddr, MakeShared<FJsonValueString>(TEXT("None")), Detail));
	}
	{
		FString Error;
		TestFalse(
			TEXT("writing \"None\" to a plain value is refused"),
			ResponseSucceeded(
				Registry.ExecuteHandler(
					TEXT("fill_datatable_from_json"),
					MakeFillParams(Table, EmptyRefRowName, TEXT("Count"), MakeShared<FJsonValueString>(TEXT("None")))),
				Error));
		Row = FindReferenceRow(Table, EmptyRefRowName);
		if (!TestNotNull(TEXT("the row survived the refused write"), Row)) return false;
		TestEqual(TEXT("the refused write left the plain value alone"), Row->Count, 3);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
