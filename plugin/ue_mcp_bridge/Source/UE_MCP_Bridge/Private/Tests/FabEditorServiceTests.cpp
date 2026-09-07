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
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT(R"({"results":[{"assetId":"owned-1","title":"Forest Kit","seller":"Publisher","url":"https://www.fab.com/listings/owned-1?secret=never-return","projectVersions":[{"engineVersions":["5.8"]}]}],"cursors":{"next":null}})"))), Page);
	TArray<TSharedPtr<FJsonObject>> Items; FString Cursor, Error;
	TestTrue(TEXT("valid ownership page parses"), MCPFabEditor::ParseLibraryPage(Page, Items, Cursor, Error));
	TestEqual(TEXT("one item"), Items.Num(), 1);
	if (!Items.IsEmpty())
	{
		TestTrue(TEXT("ownership provenance is explicit"), Items[0]->GetBoolField(TEXT("owned")));
		TestEqual(TEXT("query strings are not exposed"), Items[0]->GetStringField(TEXT("url")), FString(TEXT("https://www.fab.com/listings/owned-1")));
		TestTrue(TEXT("engine-version metadata is retained"), Items[0]->HasField(TEXT("projectVersions")));
	}
	TestTrue(TEXT("terminal null cursor"), Cursor.IsEmpty());
	Page->RemoveField(TEXT("results"));
	TestFalse(TEXT("errors are not treated as empty library"), MCPFabEditor::ParseLibraryPage(Page, Items, Cursor, Error));
	TestTrue(TEXT("failed parser returns no ownership list"), Items.IsEmpty());
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT(R"({"results":[{"title":"Unidentified"}],"cursors":{"next":"next-page"}})"))), Page);
	TestFalse(TEXT("missing stable ID fails closed"), MCPFabEditor::ParseLibraryPage(Page, Items, Cursor, Error));
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT(R"({"results":[],"cursors":{"next":17}})"))), Page);
	TestFalse(TEXT("malformed cursor fails closed"), MCPFabEditor::ParseLibraryPage(Page, Items, Cursor, Error));
	TestTrue(TEXT("production Fab origin accepted"), MCPFabEditor::IsFabUrl(TEXT("https://fab.com/plugins/ue5")));
	TestFalse(TEXT("lookalike hostname rejected"), MCPFabEditor::IsFabUrl(TEXT("https://fab.com.evil.test/plugins/ue5")));
	TestFalse(TEXT("userinfo origin rejected"), MCPFabEditor::IsFabUrl(TEXT("https://fab.com@evil.test/")));
	TestFalse(TEXT("insecure origin rejected"), MCPFabEditor::IsFabUrl(TEXT("http://fab.com/")));
	FString Account;
	const FString Subject = TEXT("0123456789abcdef0123456789abcdef");
	const FString Payload = FBase64::Encode(TEXT("{\"sub\":\"") + Subject + TEXT("\"}"));
	TestTrue(TEXT("standard session envelope supported"), MCPFabEditor::AccountFromToken(TEXT("header.") + Payload + TEXT(".signature"), Account));
	TestEqual(TEXT("account subject decoded internally"), Account, Subject);
	TestTrue(TEXT("Epic token prefix supported"), MCPFabEditor::AccountFromToken(TEXT("eg1~header.") + Payload + TEXT(".signature"), Account));
	TestFalse(TEXT("opaque token has no invented account ID"), MCPFabEditor::AccountFromToken(TEXT("opaque-token"), Account));
	TestTrue(TEXT("failed decode clears identity"), Account.IsEmpty());
	auto Bad = MakeShared<FJsonObject>(); Bad->SetStringField(TEXT("operation"), TEXT("operation_status")); Bad->SetNumberField(TEXT("offset"), 1.5);
	TestFalse(TEXT("fractional integers rejected before any action"), MCPFabEditor::Execute(Bad)->AsObject()->GetBoolField(TEXT("success")));
	Bad->RemoveField(TEXT("offset")); Bad->SetStringField(TEXT("operationId"), TEXT("does-not-exist"));
	TestFalse(TEXT("unknown operation is not reported completed"), MCPFabEditor::Execute(Bad)->AsObject()->GetBoolField(TEXT("success")));
	return true;
}
#endif
