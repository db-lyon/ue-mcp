// Coverage for the DataTable row write path and the JSON property setter it
// runs on, for the three data-loss bugs they carried (#928, #929, #935).
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
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/PerPlatformProperties.h"
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
	FProperty* Int64Prop = TBaseStructure<FInt64Vector2>::Get()->FindPropertyByName(TEXT("X"));
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

#endif // WITH_DEV_AUTOMATION_TESTS
