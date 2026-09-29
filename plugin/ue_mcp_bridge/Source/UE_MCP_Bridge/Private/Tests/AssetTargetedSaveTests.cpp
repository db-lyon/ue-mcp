// asset(save, assetPaths=[...]) wrote every dirty package in the editor.
//
// assetPaths was not a parameter of save_asset, so the handler never read it,
// saw no assetPath and took its save-everything branch: seven material paths in,
// sixteen packages written, World Partition actor packages included. The fix
// reads assetPaths and saves exactly those packages, and every save-everything
// entry point refuses a key it does not take instead of widening the call.
//
// The probes live under a private mount point in the system temp area, because
// run_automation_tests dispatches every EditorContext test against whatever
// project the bridge is attached to.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Asset/AssetHandlers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "MCPEngineCompat.h"
#include "Tests/MCPScopedTestMount.h"

namespace
{
	const TCHAR* const MCPTargetedSaveTestRoot = TEXT("/UEMCPTargetedSaveTest/");

	UDataTable* MakeTargetedSaveProbe(const FString& PackageName)
	{
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package) return nullptr;
		UDataTable* Table = NewObject<UDataTable>(
			Package, FName(*FPackageName::GetShortName(PackageName)), RF_Public | RF_Standalone);
		if (!Table) return nullptr;
		Table->RowStruct = FPerPlatformInt::StaticStruct();
		return SaveAssetPackage(Table) ? Table : nullptr;
	}

	TSharedPtr<FJsonObject> TargetedSaveCall(FMCPHandlerRegistry& Registry, const TArray<FString>& Paths)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetArrayField(TEXT("assetPaths"), MCPStringListToJson(Paths));
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("save_asset"), Params);
		return Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : MakeShared<FJsonObject>();
	}

	FString TargetedSaveStatus(const TSharedPtr<FJsonObject>& Result, const FString& PackageName)
	{
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Result->TryGetArrayField(TEXT("results"), Rows)) return FString();
		for (const TSharedPtr<FJsonValue>& Row : *Rows)
		{
			const TSharedPtr<FJsonObject> Obj = Row->AsObject();
			if (Obj.IsValid() && Obj->GetStringField(TEXT("package")) == PackageName)
			{
				return Obj->GetStringField(TEXT("status"));
			}
		}
		return FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPTargetedSaveLeavesOthersDirtyTest,
	"UE.MCP.Asset.Save.AssetPathsSavesOnlyTheNamedPackages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPTargetedSaveLeavesOthersDirtyTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ FString(MCPTargetedSaveTestRoot), TEXT("UEMCPTargetedSaveTest") };

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);

	const FString NamedPackage = FString(MCPTargetedSaveTestRoot) + TEXT("DT_Named");
	const FString UnrelatedPackage = FString(MCPTargetedSaveTestRoot) + TEXT("DT_Unrelated");
	UDataTable* Named = MakeTargetedSaveProbe(NamedPackage);
	UDataTable* Unrelated = MakeTargetedSaveProbe(UnrelatedPackage);
	if (!TestNotNull(TEXT("named probe saved"), Named) || !TestNotNull(TEXT("unrelated probe saved"), Unrelated)) return false;
	const FGCRootScope KeepNamed(Named);
	const FGCRootScope KeepUnrelated(Unrelated);
	UPackage* NamedPkg = Named->GetOutermost();
	UPackage* UnrelatedPkg = Unrelated->GetOutermost();
	NamedPkg->SetDirtyFlag(true);
	UnrelatedPkg->SetDirtyFlag(true);

	// An object path in the list resolves to its package, and only that one is written.
	{
		const TSharedPtr<FJsonObject> Result = TargetedSaveCall(Registry, { Named->GetPathName() });
		FString Error;
		Result->TryGetStringField(TEXT("error"), Error);
		TestTrue(FString::Printf(TEXT("the targeted save succeeds (%s)"), *Error), Result->GetBoolField(TEXT("success")));
		TestEqual(TEXT("the named package is reported saved"), TargetedSaveStatus(Result, NamedPackage), FString(TEXT("saved")));
		TestFalse(TEXT("the named package is clean"), NamedPkg->IsDirty());
		TestTrue(TEXT("the unrelated dirty package is still dirty"), UnrelatedPkg->IsDirty());
		TestEqual(TEXT("the unrelated package is not in the report"), TargetedSaveStatus(Result, UnrelatedPackage), FString());
	}

	// A clean package named again is reported, not rewritten.
	{
		const TSharedPtr<FJsonObject> Result = TargetedSaveCall(Registry, { NamedPackage });
		TestEqual(TEXT("a clean named package is notDirty"), TargetedSaveStatus(Result, NamedPackage), FString(TEXT("notDirty")));
		TestTrue(TEXT("the unrelated package is still dirty"), UnrelatedPkg->IsDirty());
	}

	// One unknown entry refuses the whole list before anything is written.
	{
		NamedPkg->SetDirtyFlag(true);
		const TSharedPtr<FJsonObject> Result = TargetedSaveCall(Registry,
			{ NamedPackage, FString(MCPTargetedSaveTestRoot) + TEXT("DT_NoSuchProbe") });
		TestFalse(TEXT("a list with an unknown path is refused"), Result->GetBoolField(TEXT("success")));
		const TArray<TSharedPtr<FJsonValue>>* Refused = nullptr;
		TestTrue(TEXT("the refusal lists the bad entry"),
			Result->TryGetArrayField(TEXT("refused"), Refused) && Refused->Num() == 1);
		TestTrue(TEXT("the valid entry was not written either"), NamedPkg->IsDirty());
		TestTrue(TEXT("the unrelated package is still dirty"), UnrelatedPkg->IsDirty());
	}

	// The save-everything form refuses a key it does not take. Asserted on the
	// guard itself: driving the sweep here would write the attached project's
	// dirty packages if the guard regressed.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetArrayField(TEXT("assetPathz"), MCPStringListToJson({ NamedPackage }));
		Params->SetStringField(TEXT("timeoutMs"), TEXT("routing names are not refused"));
		const TSharedPtr<FJsonValue> Refusal = MCPRefuseSweepWithUnknownParams(Params, { TEXT("assetPath"), TEXT("force") }, TEXT("asset(save)"));
		TestTrue(TEXT("an unknown key refuses the sweep"), Refusal.IsValid());
		if (Refusal.IsValid())
		{
			FString Error;
			Refusal->AsObject()->TryGetStringField(TEXT("error"), Error);
			TestTrue(TEXT("the refusal names the key"), Error.Contains(TEXT("assetPathz")));
			TestFalse(TEXT("the refusal does not name a routing key"), Error.Contains(TEXT("timeoutMs")));
		}

		TSharedPtr<FJsonObject> Declared = MakeShared<FJsonObject>();
		Declared->SetStringField(TEXT("assetPath"), TEXT(""));
		TestFalse(TEXT("declared keys pass"),
			MCPRefuseSweepWithUnknownParams(Declared, { TEXT("assetPath"), TEXT("force") }, TEXT("asset(save)")).IsValid());
	}

	NamedPkg->SetDirtyFlag(false);
	UnrelatedPkg->SetDirtyFlag(false);
	return true;
}

#endif
