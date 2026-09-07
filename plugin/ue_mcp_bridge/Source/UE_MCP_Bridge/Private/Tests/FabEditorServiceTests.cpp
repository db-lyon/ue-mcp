#if WITH_DEV_AUTOMATION_TESTS
#include "Handlers/FabEditorService.h"
#include "Misc/AutomationTest.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPFabLibraryContractTest, "UE.MCP.FabEditor.LibraryContract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPFabLibraryContractTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Page;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT(R"({"results":[{"source":"acquired","uid":"library-entry","entitlement":{"licenses":[{"name":"Personal","slug":"personal"}]},"listing":{"uid":"11111111-1111-4111-8111-111111111111","title":"Example environment","listingType":"3d-model","publisher":{"sellerName":"Example publisher"},"assetFormats":[{"assetFormatType":{"code":"unreal-engine"},"technicalSpecs":{"unrealEngineDistributionMethod":"asset_pack","unrealEngineEngineVersions":["UE_5.8"],"unrealEngineTargetPlatforms":["Windows"]}}]}}],"cursors":{"next":null}})"))), Page);
	TArray<TSharedPtr<FJsonObject>> Items; FString Cursor, Error;
	TestTrue(TEXT("live frontend response shape parses"), MCPFabEditor::ParseLibraryPage(Page, Items, Cursor, Error));
	TestEqual(TEXT("one owned item"), Items.Num(), 1);
	if (Items.IsEmpty()) return false;
	TestEqual(TEXT("listing ID is used, not library-entry ID"), Items[0]->GetStringField(TEXT("assetId")), FString(TEXT("11111111-1111-4111-8111-111111111111")));
	TestEqual(TEXT("asset packs use Add to Project"), Items[0]->GetStringField(TEXT("deliveryMode")), FString(TEXT("add_to_project")));
	TestTrue(TEXT("ownership provenance is explicit"), Items[0]->GetBoolField(TEXT("owned")));
	TestTrue(TEXT("format and engine metadata retained"), Items[0]->HasField(TEXT("formats")) && Items[0]->HasField(TEXT("projectVersions")));
	TestTrue(TEXT("terminal null cursor"), Cursor.IsEmpty());
	const auto Entry = Page->GetArrayField(TEXT("results"))[0]->AsObject();
	const auto Listing = Entry->GetObjectField(TEXT("listing"));
	const auto Specs = Listing->GetArrayField(TEXT("assetFormats"))[0]->AsObject()->GetObjectField(TEXT("technicalSpecs"));
	Specs->SetStringField(TEXT("unrealEngineDistributionMethod"), TEXT("complete_project"));
	TestTrue(TEXT("complete project parses"), MCPFabEditor::ParseLibraryPage(Page, Items, Cursor, Error));
	TestEqual(TEXT("complete projects require Create Project"), Items[0]->GetStringField(TEXT("deliveryMode")), FString(TEXT("create_project")));
	Specs->SetStringField(TEXT("unrealEngineDistributionMethod"), TEXT("code_plugin"));
	MCPFabEditor::ParseLibraryPage(Page, Items, Cursor, Error);
	TestEqual(TEXT("code plugins remain separate"), Items[0]->GetStringField(TEXT("deliveryMode")), FString(TEXT("install_plugin")));
	Specs->SetField(TEXT("unrealEngineDistributionMethod"), MakeShared<FJsonValueNull>());
	MCPFabEditor::ParseLibraryPage(Page, Items, Cursor, Error);
	TestEqual(TEXT("missing distribution is not guessed"), Items[0]->GetStringField(TEXT("deliveryMode")), FString(TEXT("unknown")));
	TestFalse(TEXT("unknown delivery is explicitly unverified"), Items[0]->GetBoolField(TEXT("deliveryModeVerified")));
	Entry->SetStringField(TEXT("source"), TEXT("catalog"));
	TestFalse(TEXT("public catalog records are rejected"), MCPFabEditor::ParseLibraryPage(Page, Items, Cursor, Error));
	TestTrue(TEXT("failure returns no ownership list"), Items.IsEmpty());
	Entry->SetStringField(TEXT("source"), TEXT("acquired"));
	Listing->RemoveField(TEXT("uid"));
	TestFalse(TEXT("missing listing ID fails closed"), MCPFabEditor::ParseLibraryPage(Page, Items, Cursor, Error));
	Page->GetObjectField(TEXT("cursors"))->SetNumberField(TEXT("next"), 17);
	TestFalse(TEXT("malformed cursor fails closed"), MCPFabEditor::ParseLibraryPage(Page, Items, Cursor, Error));
	TestTrue(TEXT("production origin accepted"), MCPFabEditor::IsFabUrl(TEXT("https://www.fab.com/plugins/ue5/library")));
	TestFalse(TEXT("lookalike origin rejected"), MCPFabEditor::IsFabUrl(TEXT("https://fab.com.evil.test/")));
	TestFalse(TEXT("userinfo origin rejected"), MCPFabEditor::IsFabUrl(TEXT("https://fab.com@evil.test/")));
	FString Account;
	const FString Subject = TEXT("0123456789abcdef0123456789abcdef");
	const FString Payload = FBase64::Encode(TEXT("{\"sub\":\"") + Subject + TEXT("\"}"));
	TestTrue(TEXT("Epic session envelope supported"), MCPFabEditor::AccountFromToken(TEXT("eg1~header.") + Payload + TEXT(".signature"), Account));
	TestEqual(TEXT("native identity is retained for frontend comparison"), Account, Subject);
	TestFalse(TEXT("opaque token is not an invented identity"), MCPFabEditor::AccountFromToken(TEXT("opaque-token"), Account));
	auto Bad = MakeShared<FJsonObject>(); Bad->SetStringField(TEXT("operation"), TEXT("operation_status")); Bad->SetNumberField(TEXT("offset"), 1.5);
	TestFalse(TEXT("fractional integers rejected before any action"), MCPFabEditor::Execute(Bad)->AsObject()->GetBoolField(TEXT("success")));
	Bad->RemoveField(TEXT("offset")); Bad->SetStringField(TEXT("operationId"), TEXT("does-not-exist"));
	TestFalse(TEXT("unknown operation is not completed"), MCPFabEditor::Execute(Bad)->AsObject()->GetBoolField(TEXT("success")));
	return true;
}
#endif
