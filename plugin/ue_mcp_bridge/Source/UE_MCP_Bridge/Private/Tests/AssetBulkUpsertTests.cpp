#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Misc/AutomationTest.h"
#include "EditorAssetLibrary.h"

namespace
{
TSharedPtr<FJsonObject> MakeDryRunRequest(const FString& PackagePath)
{
	TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
	Item->SetStringField(TEXT("name"), TEXT("DA_UEMCP_BulkUpsertDryRun"));
	Item->SetStringField(TEXT("packagePath"), PackagePath);
	Item->SetStringField(TEXT("className"), TEXT("/Script/EnhancedInput.InputAction"));
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
	TestTrue(TEXT("canonical handler is registered"), Registry.HasHandler(TEXT("bulk_upsert_data_assets")));
	TestTrue(TEXT("dotted handler alias is registered"), Registry.HasHandler(TEXT("asset.bulk_upsert_data_assets")));
	TestTrue(TEXT("rollback handler is registered"), Registry.HasHandler(TEXT("bulk_restore_data_assets")));

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

	const FString DryRunAssetPath = TEXT("/Game/UEMCPTests/DA_UEMCP_BulkUpsertDryRun.DA_UEMCP_BulkUpsertDryRun");
	if (UEditorAssetLibrary::DoesAssetExist(DryRunAssetPath))
	{
		UEditorAssetLibrary::DeleteAsset(DryRunAssetPath);
	}
	TestFalse(TEXT("dry-run target is absent before preflight"), UEditorAssetLibrary::DoesAssetExist(DryRunAssetPath));
	const TSharedPtr<FJsonValue> DryRunResponse = Registry.ExecuteHandler(
		TEXT("bulk_upsert_data_assets"),
		MakeDryRunRequest(TEXT("/Game/UEMCPTests")));
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
			TestEqual(TEXT("dry run plans creation"), Items[0]->AsObject()->GetStringField(TEXT("status")), FString(TEXT("wouldCreate")));
		}
	}
	TestFalse(TEXT("dry-run target remains absent"), UEditorAssetLibrary::DoesAssetExist(DryRunAssetPath));

	TSharedPtr<FJsonObject> CreateRequest = MakeDryRunRequest(TEXT("/Game/UEMCPTests"));
	CreateRequest->SetBoolField(TEXT("dryRun"), false);
	CreateRequest->SetBoolField(TEXT("save"), false);
	const TSharedPtr<FJsonValue> CreateResponse = Registry.ExecuteHandler(
		TEXT("bulk_upsert_data_assets"),
		CreateRequest);
	TestTrue(TEXT("execution creates the requested asset"), UEditorAssetLibrary::DoesAssetExist(DryRunAssetPath));
	if (CreateResponse.IsValid() && CreateResponse->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Result = CreateResponse->AsObject();
		TestTrue(TEXT("create succeeds"), Result->GetBoolField(TEXT("success")));
		TestEqual(TEXT("one asset is created"), static_cast<int32>(Result->GetNumberField(TEXT("createdAssetCount"))), 1);
		const TArray<TSharedPtr<FJsonValue>>& Items = Result->GetArrayField(TEXT("items"));
		if (Items.Num() == 1)
		{
			TestEqual(TEXT("create status is reported"), Items[0]->AsObject()->GetStringField(TEXT("status")), FString(TEXT("created")));
		}
	}

	const TSharedPtr<FJsonValue> RepeatResponse = Registry.ExecuteHandler(
		TEXT("bulk_upsert_data_assets"),
		CreateRequest);
	if (RepeatResponse.IsValid() && RepeatResponse->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Result = RepeatResponse->AsObject();
		TestTrue(TEXT("repeat succeeds"), Result->GetBoolField(TEXT("success")));
		TestEqual(TEXT("repeat is idempotent"), static_cast<int32>(Result->GetNumberField(TEXT("unchangedAssetCount"))), 1);
		const TArray<TSharedPtr<FJsonValue>>& Items = Result->GetArrayField(TEXT("items"));
		if (Items.Num() == 1)
		{
			TestEqual(TEXT("unchanged status is reported"), Items[0]->AsObject()->GetStringField(TEXT("status")), FString(TEXT("unchanged")));
		}
	}
	if (UEditorAssetLibrary::DoesAssetExist(DryRunAssetPath))
	{
		TestTrue(TEXT("test asset cleanup succeeds"), UEditorAssetLibrary::DeleteAsset(DryRunAssetPath));
	}

	const TSharedPtr<FJsonValue> ProtectedResponse = Registry.ExecuteHandler(
		TEXT("bulk_upsert_data_assets"),
		MakeDryRunRequest(TEXT("/Engine/UEMCPTests")));
	TestTrue(TEXT("protected path returns an object"), ProtectedResponse.IsValid() && ProtectedResponse->Type == EJson::Object);
	if (ProtectedResponse.IsValid() && ProtectedResponse->Type == EJson::Object)
	{
		TestFalse(TEXT("protected path is unsuccessful"), ProtectedResponse->AsObject()->GetBoolField(TEXT("success")));
		TestTrue(TEXT("protected path is identified"), ProtectedResponse->AsObject()->GetStringField(TEXT("error")).Contains(TEXT("protected")));
	}
	return true;
}

#endif
