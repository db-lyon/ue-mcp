// Coverage for bulk_upsert_data_assets that is safe to run anywhere.
//
// run_automation_tests dispatches every EditorContext/EngineFilter test in the
// process when it is called without a filter, against whatever project the
// bridge is attached to. A test that creates and deletes real packages would
// therefore mutate a user's live project as a side effect of asking for a test
// run. Everything below is confined to argument validation, preflight
// rejection, and the dry-run path, all of which are specified to write nothing;
// the create/update/replay behaviour is exercised by the smoke suite against
// the dedicated test project instead.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/AssetHandlers.h"
#include "Misc/AutomationTest.h"
#include "EditorAssetLibrary.h"

namespace
{
TSharedPtr<FJsonObject> MakeUpsertRequest(const FString& PackagePath, const FString& ClassName)
{
	TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
	Item->SetStringField(TEXT("name"), TEXT("DA_UEMCP_BulkUpsertDryRun"));
	Item->SetStringField(TEXT("packagePath"), PackagePath);
	Item->SetStringField(TEXT("className"), ClassName);
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetBoolField(TEXT("bConsumeInput"), false);
	Item->SetObjectField(TEXT("properties"), Properties);

	TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetArrayField(TEXT("items"), { MakeShared<FJsonValueObject>(Item) });
	Request->SetBoolField(TEXT("dryRun"), true);
	return Request;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetBulkUpsertRegistrationTest,
	"UE.MCP.Asset.BulkUpsert.RegistrationAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetBulkUpsertRegistrationTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("upsert handler is registered"), Registry.HasHandler(TEXT("bulk_upsert_data_assets")));
	TestTrue(TEXT("rollback handler is registered"), Registry.HasHandler(TEXT("bulk_restore_data_assets")));
	TestTrue(TEXT("upsert handler carries its own timeout"), Registry.GetHandlerTimeout(TEXT("bulk_upsert_data_assets")) > 0.0f);

	const TSharedPtr<FJsonValue> MissingItemsResponse = Registry.ExecuteHandler(
		TEXT("bulk_upsert_data_assets"),
		MakeShared<FJsonObject>());
	TestTrue(TEXT("missing items returns an object"), MissingItemsResponse.IsValid() && MissingItemsResponse->Type == EJson::Object);
	if (MissingItemsResponse.IsValid() && MissingItemsResponse->Type == EJson::Object)
	{
		TestFalse(TEXT("missing items is unsuccessful"), MissingItemsResponse->AsObject()->GetBoolField(TEXT("success")));
		TestTrue(TEXT("missing items identifies the field"), MissingItemsResponse->AsObject()->GetStringField(TEXT("error")).Contains(TEXT("items")));
	}

	TSharedPtr<FJsonObject> OversizedRequest = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> OversizedItems;
	for (int32 Index = 0; Index < 501; ++Index)
	{
		OversizedItems.Add(MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()));
	}
	OversizedRequest->SetArrayField(TEXT("items"), OversizedItems);
	const TSharedPtr<FJsonValue> OversizedResponse = Registry.ExecuteHandler(
		TEXT("bulk_upsert_data_assets"),
		OversizedRequest);
	TestTrue(TEXT("oversized batch returns an object"), OversizedResponse.IsValid() && OversizedResponse->Type == EJson::Object);
	if (OversizedResponse.IsValid() && OversizedResponse->Type == EJson::Object)
	{
		TestFalse(TEXT("oversized batch is unsuccessful"), OversizedResponse->AsObject()->GetBoolField(TEXT("success")));
		TestTrue(TEXT("oversized batch reports the bound"), OversizedResponse->AsObject()->GetStringField(TEXT("error")).Contains(TEXT("500")));
	}

	TSharedPtr<FJsonObject> BadConflictRequest = MakeUpsertRequest(
		TEXT("/Game/UEMCPTests"),
		TEXT("/Script/EnhancedInput.InputAction"));
	BadConflictRequest->SetStringField(TEXT("onConflict"), TEXT("merge"));
	const TSharedPtr<FJsonValue> BadConflictResponse = Registry.ExecuteHandler(
		TEXT("bulk_upsert_data_assets"),
		BadConflictRequest);
	if (BadConflictResponse.IsValid() && BadConflictResponse->Type == EJson::Object)
	{
		TestFalse(TEXT("unknown conflict policy is rejected"), BadConflictResponse->AsObject()->GetBoolField(TEXT("success")));
	}

	// A class that is not a UDataAsset subclass must be rejected in preflight
	// rather than produce an asset the caller cannot use.
	const TSharedPtr<FJsonValue> WrongClassResponse = Registry.ExecuteHandler(
		TEXT("bulk_upsert_data_assets"),
		MakeUpsertRequest(TEXT("/Game/UEMCPTests"), TEXT("/Script/Engine.StaticMesh")));
	if (WrongClassResponse.IsValid() && WrongClassResponse->Type == EJson::Object)
	{
		TestFalse(TEXT("non-DataAsset class is rejected"), WrongClassResponse->AsObject()->GetBoolField(TEXT("success")));
		TestTrue(TEXT("non-DataAsset rejection names the requirement"), WrongClassResponse->AsObject()->GetStringField(TEXT("error")).Contains(TEXT("UDataAsset")));
	}

	const FString DryRunAssetPath = TEXT("/Game/UEMCPTests/DA_UEMCP_BulkUpsertDryRun.DA_UEMCP_BulkUpsertDryRun");
	const bool bTargetExisted = UEditorAssetLibrary::DoesAssetExist(DryRunAssetPath);
	const TSharedPtr<FJsonValue> DryRunResponse = Registry.ExecuteHandler(
		TEXT("bulk_upsert_data_assets"),
		MakeUpsertRequest(TEXT("/Game/UEMCPTests"), TEXT("/Script/EnhancedInput.InputAction")));
	TestTrue(TEXT("dry run returns an object"), DryRunResponse.IsValid() && DryRunResponse->Type == EJson::Object);
	if (DryRunResponse.IsValid() && DryRunResponse->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Result = DryRunResponse->AsObject();
		TestTrue(TEXT("dry run succeeds"), Result->GetBoolField(TEXT("success")));
		TestTrue(TEXT("dry run passes preflight"), Result->GetBoolField(TEXT("preflightPassed")));
		TestFalse(TEXT("dry run performs no mutation"), Result->GetBoolField(TEXT("mutationPerformed")));
		const TArray<TSharedPtr<FJsonValue>>& Items = Result->GetArrayField(TEXT("items"));
		TestEqual(TEXT("dry run returns one item"), Items.Num(), 1);
		if (Items.Num() == 1)
		{
			const TSharedPtr<FJsonObject> Item = Items[0]->AsObject();
			TestTrue(TEXT("dry run item reports success"), Item->GetBoolField(TEXT("success")));
			TestEqual(
				TEXT("dry run plans the right outcome"),
				Item->GetStringField(TEXT("status")),
				bTargetExisted ? FString(TEXT("wouldUpdate")) : FString(TEXT("wouldCreate")));
		}
	}
	TestTrue(
		TEXT("dry run leaves the target package untouched"),
		UEditorAssetLibrary::DoesAssetExist(DryRunAssetPath) == bTargetExisted);

	const TSharedPtr<FJsonValue> ProtectedResponse = Registry.ExecuteHandler(
		TEXT("bulk_upsert_data_assets"),
		MakeUpsertRequest(TEXT("/Engine/UEMCPTests"), TEXT("/Script/EnhancedInput.InputAction")));
	TestTrue(TEXT("protected path returns an object"), ProtectedResponse.IsValid() && ProtectedResponse->Type == EJson::Object);
	if (ProtectedResponse.IsValid() && ProtectedResponse->Type == EJson::Object)
	{
		TestFalse(TEXT("protected path is unsuccessful"), ProtectedResponse->AsObject()->GetBoolField(TEXT("success")));
		TestTrue(TEXT("protected path is identified"), ProtectedResponse->AsObject()->GetStringField(TEXT("error")).Contains(TEXT("protected")));
	}
	return true;
}

#endif
