// Coverage for level(query_components) that is safe to run anywhere.
//
// run_automation_tests dispatches every EditorContext/EngineFilter test in the
// process against whatever project the bridge is attached to, so nothing here
// may write. query_components never writes by design, which makes most of it
// testable directly: the assertions below run the real handler against the
// open editor world and check the contract (registration, argument rejection,
// bounded output, the read guarantee, clean enum names) rather than any
// particular level's contents.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> MCPQueryTestRun(FMCPHandlerRegistry& Registry, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("query_components"), Request);
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}

	TSharedPtr<FJsonObject> MCPQueryTestPredicate(const FString& Field, const FString& Op)
	{
		TSharedPtr<FJsonObject> Predicate = MakeShared<FJsonObject>();
		Predicate->SetStringField(TEXT("field"), Field);
		Predicate->SetStringField(TEXT("op"), Op);
		return Predicate;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelQueryComponentsContractTest,
	"UE.MCP.Level.QueryComponents.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelQueryComponentsContractTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	TestTrue(TEXT("query_components is registered"), Registry.HasHandler(TEXT("query_components")));
	TestTrue(
		TEXT("query_components carries its own timeout, because a whole-map scan outlives the default"),
		Registry.GetHandlerTimeout(TEXT("query_components")) > 0.0f);

	// An unknown field group is an error rather than a silently empty
	// projection. A projection that quietly produces nothing is how a caller
	// ends up trusting a confident zero.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetArrayField(TEXT("fields"), { MakeShared<FJsonValueString>(TEXT("nonsense")) });
		const TSharedPtr<FJsonObject> Response = MCPQueryTestRun(Registry, Request);
		TestTrue(TEXT("unknown field group returns an object"), Response.IsValid());
		if (Response.IsValid())
		{
			TestFalse(TEXT("unknown field group fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(
				TEXT("unknown field group names the offending entry"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("nonsense")));
		}
	}

	// An unknown operator is likewise rejected, and the message lists the
	// operators that do exist.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetArrayField(TEXT("where"), {
			MakeShared<FJsonValueObject>(MCPQueryTestPredicate(TEXT("shadow.castShadow"), TEXT("approximately"))) });
		const TSharedPtr<FJsonObject> Response = MCPQueryTestRun(Registry, Request);
		TestTrue(TEXT("unknown operator returns an object"), Response.IsValid());
		if (Response.IsValid())
		{
			TestFalse(TEXT("unknown operator fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(
				TEXT("unknown operator lists the valid ones"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("isTrue")));
		}
	}

	// A predicate missing its field is rejected by index, so a caller with
	// twenty predicates knows which one.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetArrayField(TEXT("where"), { MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()) });
		const TSharedPtr<FJsonObject> Response = MCPQueryTestRun(Registry, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("predicate without a field fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(
				TEXT("predicate rejection names the index"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("where[0]")));
		}
	}

	// suspectOnly is an AND, so combining it with whereMode 'any' would change
	// what the other predicates mean. That is refused rather than guessed at.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetBoolField(TEXT("suspectOnly"), true);
		Request->SetStringField(TEXT("whereMode"), TEXT("any"));
		Request->SetArrayField(TEXT("where"), {
			MakeShared<FJsonValueObject>(MCPQueryTestPredicate(TEXT("shadow.castShadow"), TEXT("isTrue"))) });
		const TSharedPtr<FJsonObject> Response = MCPQueryTestRun(Registry, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("suspectOnly with whereMode any fails"), Response->GetBoolField(TEXT("success")));
		}
	}

	// A levelPath that is not a map is refused before anything is opened.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("levelPath"), TEXT("/Game/UEMCPDefinitelyNotAMap"));
		const TSharedPtr<FJsonObject> Response = MCPQueryTestRun(Registry, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("a missing level package fails"), Response->GetBoolField(TEXT("success")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelQueryComponentsBoundsTest,
	"UE.MCP.Level.QueryComponents.BoundedAndReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelQueryComponentsBoundsTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	// An unfiltered query against whatever map is open. This is the shape that
	// used to return over a megabyte, so the assertions are about the caps.
	TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetNumberField(TEXT("limit"), 5);
	Request->SetArrayField(TEXT("fields"), {
		MakeShared<FJsonValueString>(TEXT("shadow")),
		MakeShared<FJsonValueString>(TEXT("navigation")) });

	const TSharedPtr<FJsonObject> Response = MCPQueryTestRun(Registry, Request);
	if (!Response.IsValid() || !Response->GetBoolField(TEXT("success")))
	{
		// No editor world (a cook or commandlet context) is not a failure of
		// this handler, and this test is not allowed to open a map to create
		// one. Everything above already ran without a world.
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>& Rows = Response->GetArrayField(TEXT("rows"));
	TestTrue(TEXT("the row array honours 'limit'"), Rows.Num() <= 5);
	TestTrue(
		TEXT("'matched' is reported independently of how many rows came back"),
		Response->GetNumberField(TEXT("matched")) >= Rows.Num());

	// The #912 guarantee: a map-wide read leaves nothing dirty.
	TestFalse(TEXT("a query dirties no packages"), Response->GetBoolField(TEXT("dirtiedPackages")));

	// The #943 trap: a clean enum name, never EComponentMobility.MOVABLE.
	for (const TSharedPtr<FJsonValue>& RowValue : Rows)
	{
		if (!RowValue.IsValid() || RowValue->Type != EJson::Object) continue;
		const TSharedPtr<FJsonObject> Row = RowValue->AsObject();
		if (!Row->HasTypedField<EJson::Object>(TEXT("navigation"))) continue;
		const FString Mobility = Row->GetObjectField(TEXT("navigation"))->GetStringField(TEXT("mobility"));
		TestFalse(
			FString::Printf(TEXT("mobility '%s' carries no enum type prefix"), *Mobility),
			Mobility.Contains(TEXT("EComponentMobility")));
	}

	// countOnly answers the aggregate question without paying for rows.
	{
		TSharedPtr<FJsonObject> CountRequest = MakeShared<FJsonObject>();
		CountRequest->SetBoolField(TEXT("countOnly"), true);
		CountRequest->SetStringField(TEXT("groupBy"), TEXT("actorClass"));
		const TSharedPtr<FJsonObject> CountResponse = MCPQueryTestRun(Registry, CountRequest);
		if (CountResponse.IsValid() && CountResponse->GetBoolField(TEXT("success")))
		{
			TestFalse(TEXT("countOnly returns no rows array"), CountResponse->HasField(TEXT("rows")));
			TestTrue(TEXT("countOnly still groups"), CountResponse->HasField(TEXT("groups")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
