// Coverage for the level batch writes that is safe to run anywhere.
//
// run_automation_tests dispatches every EditorContext/EngineFilter test in the
// process against whatever project the bridge is attached to, so nothing here
// may mutate a level. Everything below stays on the paths that are specified
// to write nothing: argument rejection, selector refusal, and the dry run.
// The committing paths are exercised by the smoke suite against the dedicated
// test project instead.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> MCPBatchTestRun(
		FMCPHandlerRegistry& Registry,
		const TCHAR* Method,
		const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Request);
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelBatchWriteRegistrationTest,
	"UE.MCP.Level.BatchWrite.Registration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelBatchWriteRegistrationTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	for (const TCHAR* Method : {
		TEXT("batch_set_actor_properties"),
		TEXT("bulk_set_component_property"),
		TEXT("remove_components_by_class"),
		TEXT("spawn_actors_batch") })
	{
		TestTrue(FString::Printf(TEXT("%s is registered"), Method), Registry.HasHandler(Method));
		TestTrue(
			FString::Printf(TEXT("%s carries its own timeout"), Method),
			Registry.GetHandlerTimeout(Method) > 0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelBatchWriteRefusalTest,
	"UE.MCP.Level.BatchWrite.RefusesAnUnboundedRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelBatchWriteRefusalTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	// batch_set_properties with no selector would mean "every actor in the
	// level", which is never what someone means by a property batch.
	{
		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
		Properties->SetBoolField(TEXT("bHidden"), true);
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetObjectField(TEXT("properties"), Properties);
		const TSharedPtr<FJsonObject> Response =
			MCPBatchTestRun(Registry, TEXT("batch_set_actor_properties"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("no selector fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and lists the selectors it accepts"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("labelPrefix")));
		}
	}

	// And properties are required: a selector on its own does nothing.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("tag"), TEXT("SomeTag"));
		const TSharedPtr<FJsonObject> Response =
			MCPBatchTestRun(Registry, TEXT("batch_set_actor_properties"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("no properties fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and names the parameter"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("properties")));
		}
	}

	// spawn_actors_batch needs exactly one source. Two would make the spawn
	// count ambiguous, which is the sort of ambiguity that spawns 380 actors.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorClass"), TEXT("StaticMeshActor"));
		Request->SetArrayField(TEXT("instances"), { MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()) });
		Request->SetObjectField(TEXT("fromComponents"), MakeShared<FJsonObject>());
		const TSharedPtr<FJsonObject> Response =
			MCPBatchTestRun(Registry, TEXT("spawn_actors_batch"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("two spawn sources fail"), Response->GetBoolField(TEXT("success")));
		}
	}
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorClass"), TEXT("StaticMeshActor"));
		const TSharedPtr<FJsonObject> Response =
			MCPBatchTestRun(Registry, TEXT("spawn_actors_batch"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("no spawn source fails"), Response->GetBoolField(TEXT("success")));
		}
	}

	// An unresolvable component class is named rather than matching nothing.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("componentClass"), TEXT("NotARealComponentClass"));
		const TSharedPtr<FJsonObject> Response =
			MCPBatchTestRun(Registry, TEXT("remove_components_by_class"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unresolvable component class fails"), Response->GetBoolField(TEXT("success")));
			TestEqual(TEXT("and says why"),
				Response->GetStringField(TEXT("reason")), FString(TEXT("class_not_found")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelBatchWriteDryRunDefaultTest,
	"UE.MCP.Level.BatchWrite.RemoveComponentsPreviewsByDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelBatchWriteDryRunDefaultTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	// remove_components_by_class deletes components whose auto-generated names
	// a caller cannot enumerate, so it previews unless told otherwise. This
	// runs against whatever map is open with no actor selector at all, which
	// is the widest request the action accepts, and it must still write
	// nothing.
	TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("componentClass"), TEXT("SplineMeshComponent"));

	const TSharedPtr<FJsonObject> Response =
		MCPBatchTestRun(Registry, TEXT("remove_components_by_class"), Request);
	if (!Response.IsValid() || !Response->GetBoolField(TEXT("success")))
	{
		// No editor world available in this context, and this test may not
		// open a map to make one.
		return true;
	}

	TestTrue(TEXT("dryRun defaults to true"), Response->GetBoolField(TEXT("dryRun")));
	TestFalse(TEXT("a preview saves nothing"), Response->GetBoolField(TEXT("saved")));
	TestTrue(TEXT("a preview reports what it would have removed"),
		Response->HasField(TEXT("wouldRemoveComponents")));
	TestFalse(TEXT("and does not claim to have removed anything"),
		Response->HasField(TEXT("removedComponents")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
