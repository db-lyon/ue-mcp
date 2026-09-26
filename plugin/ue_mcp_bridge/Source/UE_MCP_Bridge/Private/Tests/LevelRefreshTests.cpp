// Coverage for the level refresh and inspect actions (#944, #915, #914).
//
// run_automation_tests dispatches every EditorContext/EngineFilter test in the
// process against whatever project the bridge is attached to. Two of these
// three actions rebuild engine state, so the assertions stay on the paths
// specified to change nothing: the refusals that keep them from running
// unbounded, and the dry run.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> MCPRefreshTestRun(
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
	FLevelRefreshRefusesUnboundedTest,
	"UE.MCP.Level.Refresh.RefusesAnUnboundedRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelRefreshRefusesUnboundedTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	for (const TCHAR* Method : {
		TEXT("rerun_construction_scripts"),
		TEXT("recreate_physics_state"),
		TEXT("test_component_overlap") })
	{
		TestTrue(FString::Printf(TEXT("%s is registered"), Method), Registry.HasHandler(Method));
	}

	// Rerunning every construction script in a map destroys and rebuilds every
	// generated component in it, so an empty request is refused rather than
	// treated as "all".
	{
		const TSharedPtr<FJsonObject> Response =
			MCPRefreshTestRun(Registry, TEXT("rerun_construction_scripts"), MakeShared<FJsonObject>());
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unbounded rerun fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and says which selectors it wants"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("actorLabels")));
		}
	}

	// Same for the physics rebuild, which is long and disruptive and whose bug
	// is always localised to the meshes that changed.
	{
		const TSharedPtr<FJsonObject> Response =
			MCPRefreshTestRun(Registry, TEXT("recreate_physics_state"), MakeShared<FJsonObject>());
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unbounded physics rebuild fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and says so explicitly"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("whole world")));
		}
	}

	// An unknown overlap method is named rather than silently treated as OBB.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabelA"), TEXT("A"));
		Request->SetStringField(TEXT("actorLabelB"), TEXT("B"));
		Request->SetStringField(TEXT("method"), TEXT("Sphere"));
		const TSharedPtr<FJsonObject> Response =
			MCPRefreshTestRun(Registry, TEXT("test_component_overlap"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unknown method fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and lists the ones that exist"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("AABB")));
		}
	}

	// Both labels are required, and a missing one is named.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabelA"), TEXT("A"));
		const TSharedPtr<FJsonObject> Response =
			MCPRefreshTestRun(Registry, TEXT("test_component_overlap"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("a one-sided overlap test fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and names the missing parameter"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("actorLabelB")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelRecreatePhysicsPreviewTest,
	"UE.MCP.Level.Refresh.PhysicsRebuildPreviewsByDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelRecreatePhysicsPreviewTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	// A bounded request with no dryRun given. The preflight is the default,
	// which is what #915 asked for: see what would be rebuilt, and what its
	// collision state is now, before rebuilding it.
	TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("componentClass"), TEXT("StaticMeshComponent"));

	const TSharedPtr<FJsonObject> Response =
		MCPRefreshTestRun(Registry, TEXT("recreate_physics_state"), Request);
	if (!Response.IsValid() || !Response->GetBoolField(TEXT("success")))
	{
		// No editor world in this context, and this test may not open a map.
		return true;
	}

	TestTrue(TEXT("dryRun defaults to true"), Response->GetBoolField(TEXT("dryRun")));
	TestTrue(TEXT("a preview reports what it would rebuild"), Response->HasField(TEXT("wouldRecreate")));
	TestFalse(TEXT("and does not claim to have rebuilt anything"), Response->HasField(TEXT("recreated")));
	TestTrue(TEXT("a preview still returns the current collision summary"),
		Response->HasField(TEXT("collisionSummary")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
