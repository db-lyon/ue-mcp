#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/Fab/FabLibrary.h"
#include "Misc/AutomationTest.h"
#include "Misc/Base64.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFabLibraryProtocolTest, "UE.MCP.Fab.Library.Protocol",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFabLibraryProtocolTest::RunTest(const FString& Parameters)
{
	using namespace MCPFab;
	const FString Account = TEXT("0123456789abcdef0123456789abcdef");
	const FString Payload = FString::Printf(TEXT("{\"sub\":\"%s\"}"), *Account);
	TestEqual(TEXT("native JWT account discovery"), AccountFromToken(TEXT("eg1~header.") + FBase64::Encode(Payload) + TEXT(".signature")), Account);
	TestTrue(TEXT("opaque tokens do not guess an account"), AccountFromToken(TEXT("opaque")).IsEmpty());
	FSession Session{TEXT("https://www.fab.com"), Account, TEXT("")};
	TestEqual(TEXT("cursor is encoded without quotes"), LibraryUrl(Session, 20, TEXT("a+/= b")),
		TEXT("https://www.fab.com/e/accounts/") + Account + TEXT("/ue/library?count=20&cursor=a%2B%2F%3D%20b"));
	const FString Item = TEXT("{\"title\":\"Fixture\",\"assetId\":\"0123456789abcdef0123456789abcdef\",\"assetNamespace\":\"11111111111111111111111111111111\"}");
	FLibraryState State;
	State.Items.Add(MakeShared<FJsonValueString>(TEXT("previous snapshot")));
	State.AccountId = Account;
	State.Begin(Account);
	TestTrue(TEXT("first valid page"), State.Consume(200, TEXT("application/json; charset=utf-8"),
		TEXT("{\"results\":[") + Item + TEXT("],\"cursors\":{\"next\":\"cursor+1\"}}")));
	TestEqual(TEXT("previous snapshot remains while fetching"), State.Items.Num(), 1);
	TestEqual(TEXT("no premature success"), State.State, FString(TEXT("fetching")));
	TestFalse(TEXT("404 HTML is failure"), State.Consume(404, TEXT("text/html"), TEXT("private response")));
	TestEqual(TEXT("failure retains previous snapshot"), State.Items.Num(), 1);
	TestEqual(TEXT("partial results discarded"), State.Pending.Num(), 0);
	TestFalse(TEXT("response bodies never enter errors"), State.Error.Contains(TEXT("private response")));
	State.Begin(Account);
	const FString Repeated = TEXT("{\"results\":[],\"cursors\":{\"next\":\"same\"}}");
	TestTrue(TEXT("first cursor accepted"), State.Consume(200, TEXT("application/json"), Repeated));
	TestFalse(TEXT("repeating cursor terminates"), State.Consume(200, TEXT("application/json"), Repeated));
	State.Begin(Account);
	TestFalse(TEXT("malformed item cannot erase old index"), State.Consume(200, TEXT("application/json"), TEXT("{\"results\":[{}]}")));
	State.Begin(Account);
	TestFalse(TEXT("200 HTML is failure too"), State.Consume(200, TEXT("text/html"), TEXT("<html/>")));
	State.Begin(Account);
	TestTrue(TEXT("empty owned library is valid"), State.Consume(200, TEXT("application/json"), TEXT("{\"results\":[],\"cursors\":{\"next\":null}}")));
	TestEqual(TEXT("complete fetch still awaits publication"), State.State, FString(TEXT("publishing")));
	State.Begin(Account);
	const FString Legacy = Item.Replace(TEXT("11111111111111111111111111111111"), TEXT("ue"));
	TestTrue(TEXT("legacy Epic namespaces are opaque strings, not GUIDs"), State.Consume(200, TEXT("application/json"), TEXT("{\"results\":[") + Legacy + TEXT("]}")));
	State.Begin(TEXT("22222222222222222222222222222222"));
	TestEqual(TEXT("account switch drops private snapshot"), State.Items.Num(), 0);
	return true;
}

#endif
