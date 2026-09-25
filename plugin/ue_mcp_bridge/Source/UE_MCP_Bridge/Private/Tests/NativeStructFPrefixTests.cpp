// Coverage for #1088: native USTRUCTs are registered without the leading F, so
// FTableRowBase and /Script/Engine.FTableRowBase must resolve the same object
// as TableRowBase. MCPResolveScriptStruct is the shared lookup; reflect_struct
// and create_datatable are the two handlers that call it.
//
// run_automation_tests dispatches every EditorContext/EngineFilter test against
// whatever project the bridge is attached to. The DataTable create goes onto a
// private mount in the system temp area and is deleted before the mount comes
// down, so nothing here can reach a user's content.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/AssetHandlers.h"
#include "Handlers/ReflectionHandlers.h"
#include "Tests/MCPScopedTestMount.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

namespace MCPNativeStructPrefixTests
{
	TSharedPtr<FJsonObject> StructPrefixResponseObject(const TSharedPtr<FJsonValue>& Response)
	{
		return (Response.IsValid() && Response->Type == EJson::Object)
			? Response->AsObject()
			: TSharedPtr<FJsonObject>();
	}

	FString StructPrefixResponseString(const TSharedPtr<FJsonValue>& Response, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject> Obj = StructPrefixResponseObject(Response);
		if (!Obj.IsValid()) return FString();
		FString Value;
		Obj->TryGetStringField(Field, Value);
		return Value;
	}

	bool StructPrefixResponseSucceeded(const TSharedPtr<FJsonValue>& Response)
	{
		const TSharedPtr<FJsonObject> Obj = StructPrefixResponseObject(Response);
		bool bSuccess = false;
		return Obj.IsValid() && Obj->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPNativeStructFPrefixTest,
	"UE.MCP.Reflection.Struct.NativeFPrefix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNativeStructFPrefixTest::RunTest(const FString& Parameters)
{
	using namespace MCPNativeStructPrefixTests;
	UScriptStruct* const TableRow = FTableRowBase::StaticStruct();
	UScriptStruct* const VectorStruct = TBaseStructure<FVector>::Get();
	if (!TestNotNull(TEXT("FTableRowBase::StaticStruct is available"), TableRow)) return false;
	if (!TestNotNull(TEXT("FVector StaticStruct is available"), VectorStruct)) return false;

	TestEqual(TEXT("the native row struct is registered without the F"),
		TableRow->GetName(), FString(TEXT("TableRowBase")));

	const FString QualifiedNative = TableRow->GetPathName();
	const FString QualifiedFPrefixed = [&QualifiedNative]()
	{
		FString PackagePart, ObjectPart;
		if (QualifiedNative.Split(TEXT("."), &PackagePart, &ObjectPart, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			return PackagePart + TEXT(".F") + ObjectPart;
		}
		return FString(TEXT("/Script/Engine.FTableRowBase"));
	}();

	// Shared resolver: short names, qualified paths, and literal precedence.
	TestTrue(TEXT("TableRowBase resolves by exact short name"),
		MCPResolveScriptStruct(TEXT("TableRowBase")) == TableRow);
	TestTrue(TEXT("FTableRowBase strips one leading F"),
		MCPResolveScriptStruct(TEXT("FTableRowBase")) == TableRow);
	TestTrue(TEXT("the native /Script path resolves as written"),
		MCPResolveScriptStruct(QualifiedNative) == TableRow);
	TestTrue(TEXT("an F-prefixed /Script leaf keeps the module qualification"),
		MCPResolveScriptStruct(QualifiedFPrefixed) == TableRow);
	TestNull(TEXT("a qualified miss does not fall back to another module"),
		MCPResolveScriptStruct(TEXT("/Script/NoSuchModule1088.FTableRowBase")));
	// Probe structs are rooted so the GC run by delete_asset cannot free them
	// mid-test, then unrooted and marked as garbage on every exit path.
	const FString ProbeId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TArray<UObject*> Probes;
	ON_SCOPE_EXIT
	{
		for (UObject* Probe : Probes)
		{
			if (!IsValid(Probe)) continue;
			Probe->RemoveFromRoot();
			Probe->MarkAsGarbage();
		}
	};
	auto MakeProbe = [&Probes](UObject* Outer, const FString& Name) -> UScriptStruct*
	{
		UScriptStruct* Probe = NewObject<UScriptStruct>(Outer, FName(*Name), RF_Transient);
		Probe->AddToRoot();
		Probes.Add(Probe);
		return Probe;
	};

	const FString LiteralName = TEXT("FPrefixProbe_") + ProbeId;
	UScriptStruct* Literal = MakeProbe(GetTransientPackage(), LiteralName);
	MakeProbe(GetTransientPackage(), LiteralName.RightChop(1));
	TestTrue(TEXT("an existing F-prefixed name wins over its stripped counterpart"),
		MCPResolveScriptStruct(LiteralName) == Literal);

	// One short name, two outers: the resolver must refuse to pick.
	const FString AmbiguousName = TEXT("AmbiguousProbe_") + ProbeId;
	UPackage* OtherOuter = CreatePackage(*(TEXT("/Temp/UEMCPStructProbe_") + ProbeId));
	if (!TestNotNull(TEXT("second probe outer is created"), OtherOuter)) return false;
	OtherOuter->SetFlags(RF_Transient);
	OtherOuter->AddToRoot();
	Probes.Add(OtherOuter);
	UScriptStruct* AmbiguousA = MakeProbe(GetTransientPackage(), AmbiguousName);
	UScriptStruct* AmbiguousB = MakeProbe(OtherOuter, AmbiguousName);
	{
		FString Error;
		TestNull(TEXT("an ambiguous short name does not resolve"), MCPResolveScriptStruct(AmbiguousName, &Error));
		TestTrue(TEXT("the ambiguity error says so"), Error.Contains(TEXT("ambiguous")));
		TestTrue(TEXT("and lists the first candidate"), Error.Contains(AmbiguousA->GetPathName()));
		TestTrue(TEXT("and lists the second candidate"), Error.Contains(AmbiguousB->GetPathName()));

		FString StrippedError;
		TestNull(TEXT("an ambiguous F-stripped name does not resolve"),
			MCPResolveScriptStruct(TEXT("F") + AmbiguousName, &StrippedError));
		TestTrue(TEXT("and reports the ambiguity"), StrippedError.Contains(AmbiguousB->GetPathName()));
	}

	// Adding an F is reflect_struct's fallback only; create_datatable never had it.
	const FString AddedFName = TEXT("AddedFProbe_") + ProbeId;
	UScriptStruct* AddedF = MakeProbe(GetTransientPackage(), TEXT("F") + AddedFName);
	TestNull(TEXT("an F is not added by default"), MCPResolveScriptStruct(AddedFName));
	TestTrue(TEXT("an F is added when the caller opts in"),
		MCPResolveScriptStruct(AddedFName, nullptr, /*bTryAddedF=*/true) == AddedF);

	TestTrue(TEXT("Vector wins by exact name before any F is added"),
		MCPResolveScriptStruct(TEXT("Vector")) == VectorStruct);
	TestTrue(TEXT("FVector strips to Vector"),
		MCPResolveScriptStruct(TEXT("FVector")) == VectorStruct);

	TestTrue(TEXT("a double F prefix is not stripped twice"),
		MCPResolveScriptStruct(TEXT("FFTableRowBase")) == nullptr);
	TestTrue(TEXT("an unknown native name stays a miss"),
		MCPResolveScriptStruct(TEXT("FNoSuchNativeStruct1088")) == nullptr);

	FMCPHandlerRegistry ReflectionRegistry;
	FReflectionHandlers::RegisterHandlers(ReflectionRegistry);
	if (!TestTrue(TEXT("reflect_struct is registered"), ReflectionRegistry.HasHandler(TEXT("reflect_struct"))))
	{
		return false;
	}

	auto Reflect = [&](const TCHAR* StructName) -> TSharedPtr<FJsonValue>
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("structName"), StructName);
		return ReflectionRegistry.ExecuteHandler(TEXT("reflect_struct"), Params);
	};

	const TSharedPtr<FJsonValue> ReflectF = Reflect(TEXT("FTableRowBase"));
	TestTrue(TEXT("reflect_struct accepts FTableRowBase"), StructPrefixResponseSucceeded(ReflectF));
	TestEqual(TEXT("and reports the registered name"),
		StructPrefixResponseString(ReflectF, TEXT("structName")), FString(TEXT("TableRowBase")));

	const TSharedPtr<FJsonValue> ReflectQualified = Reflect(*QualifiedFPrefixed);
	TestTrue(TEXT("reflect_struct accepts /Script/Engine.FTableRowBase"), StructPrefixResponseSucceeded(ReflectQualified));
	TestEqual(TEXT("and still reports TableRowBase"),
		StructPrefixResponseString(ReflectQualified, TEXT("structName")), FString(TEXT("TableRowBase")));

	const TSharedPtr<FJsonValue> ReflectExact = Reflect(TEXT("TableRowBase"));
	TestTrue(TEXT("reflect_struct still accepts the unprefixed name"), StructPrefixResponseSucceeded(ReflectExact));

	const TSharedPtr<FJsonValue> ReflectMiss = Reflect(TEXT("FNoSuchNativeStruct1088"));
	TestFalse(TEXT("reflect_struct still misses an unknown name"), StructPrefixResponseSucceeded(ReflectMiss));
	TestTrue(TEXT("and the error names the requested struct"),
		StructPrefixResponseString(ReflectMiss, TEXT("error")).Contains(TEXT("FNoSuchNativeStruct1088")));

	const TSharedPtr<FJsonValue> ReflectAmbiguous = Reflect(*AmbiguousName);
	TestFalse(TEXT("reflect_struct refuses an ambiguous short name"), StructPrefixResponseSucceeded(ReflectAmbiguous));
	TestTrue(TEXT("and returns the qualified candidates"),
		StructPrefixResponseString(ReflectAmbiguous, TEXT("error")).Contains(AmbiguousB->GetPathName()));

	const FMCPScopedTestMount Mount{
		TEXT("/UEMCPNativeStructPrefix_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"),
		TEXT("UEMCPNativeStructPrefixTest") };
	FMCPHandlerRegistry AssetRegistry;
	FAssetHandlers::RegisterHandlers(AssetRegistry);
	if (!TestTrue(TEXT("create_datatable is registered"), AssetRegistry.HasHandler(TEXT("create_datatable"))))
	{
		return false;
	}

	if (!TestTrue(TEXT("delete_asset is registered"), AssetRegistry.HasHandler(TEXT("delete_asset"))))
	{
		return false;
	}

	auto CreateTable = [&](const FString& RowName) -> TSharedPtr<FJsonValue>
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("name"), FString::Printf(TEXT("DT_UEMCP_FPrefix_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		Params->SetStringField(TEXT("rowStruct"), RowName);
		Params->SetStringField(TEXT("packagePath"), Mount.RootPath.LeftChop(1));
		Params->SetStringField(TEXT("onConflict"), TEXT("error"));
		return AssetRegistry.ExecuteHandler(TEXT("create_datatable"), Params);
	};

	for (const FString& RowName : { FString(TEXT("FTableRowBase")), QualifiedFPrefixed })
	{
		const TSharedPtr<FJsonValue> Created = CreateTable(RowName);
		if (!TestTrue(
				FString::Printf(TEXT("create_datatable accepts %s (%s)"), *RowName, *StructPrefixResponseString(Created, TEXT("error"))),
				StructPrefixResponseSucceeded(Created)))
		{
			return false;
		}
		TestEqual(TEXT("and stores the registered row struct name"),
			StructPrefixResponseString(Created, TEXT("rowStruct")), FString(TEXT("TableRowBase")));

		// The asset must be removed before the mount comes down.
		const FString CreatedPath = StructPrefixResponseString(Created, TEXT("assetPath"));
		if (!TestFalse(TEXT("create_datatable reports assetPath"), CreatedPath.IsEmpty())) return false;
		TSharedPtr<FJsonObject> DeleteParams = MakeShared<FJsonObject>();
		DeleteParams->SetStringField(TEXT("assetPath"), CreatedPath);
		DeleteParams->SetBoolField(TEXT("force"), true);
		const TSharedPtr<FJsonValue> Deleted = AssetRegistry.ExecuteHandler(TEXT("delete_asset"), DeleteParams);
		if (!TestTrue(
				FString::Printf(TEXT("created DataTable is deleted (%s)"), *StructPrefixResponseString(Deleted, TEXT("error"))),
				StructPrefixResponseSucceeded(Deleted)))
		{
			return false;
		}
	}

	const TSharedPtr<FJsonValue> CreatedAmbiguous = CreateTable(AmbiguousName);
	TestFalse(TEXT("create_datatable refuses an ambiguous row struct"), StructPrefixResponseSucceeded(CreatedAmbiguous));
	TestTrue(TEXT("and returns the qualified candidates"),
		StructPrefixResponseString(CreatedAmbiguous, TEXT("error")).Contains(AmbiguousA->GetPathName()));

	const TSharedPtr<FJsonValue> CreatedAddedF = CreateTable(AddedFName);
	TestFalse(TEXT("create_datatable does not add an F to the row struct name"), StructPrefixResponseSucceeded(CreatedAddedF));

	AmbiguousB->RemoveFromRoot();
	AmbiguousB->MarkAsGarbage();
	TestTrue(TEXT("a garbage struct no longer counts toward ambiguity"),
		MCPResolveScriptStruct(AmbiguousName) == AmbiguousA);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
