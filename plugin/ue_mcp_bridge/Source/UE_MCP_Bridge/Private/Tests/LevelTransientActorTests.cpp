// Coverage for the transient verification actors (#956) and the component
// material overrides (#946).
//
// spawn_transient_actor is safe to run for real: its whole contract is that it
// cannot persist anything, and asserting that contract is most of the point.
// It is still cleaned up here, because a test that leaves an actor behind
// while testing an action about not leaving actors behind would be absurd.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> MCPTransientTestRun(
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
	FLevelTransientActorContractTest,
	"UE.MCP.Level.TransientActor.CannotBeSavedIntoTheMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelTransientActorContractTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	for (const TCHAR* Method : {
		TEXT("spawn_transient_actor"),
		TEXT("destroy_transient_actor"),
		TEXT("list_transient_actors"),
		TEXT("set_component_materials") })
	{
		TestTrue(FString::Printf(TEXT("%s is registered"), Method), Registry.HasHandler(Method));
	}

	// An unresolvable class is named rather than producing a null actor.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorClass"), TEXT("NotARealActorClassAtAll"));
		const TSharedPtr<FJsonObject> Response =
			MCPTransientTestRun(Registry, TEXT("spawn_transient_actor"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unresolvable class fails"), Response->GetBoolField(TEXT("success")));
		}
	}

	// An unknown initialize level is rejected by name, because silently
	// falling back to 'none' would leave a caller reading an uninitialised
	// component and believing it had asked for more.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorClass"), TEXT("StaticMeshActor"));
		Request->SetStringField(TEXT("initialize"), TEXT("everything"));
		const TSharedPtr<FJsonObject> Response =
			MCPTransientTestRun(Registry, TEXT("spawn_transient_actor"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unknown initialize level fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and lists the ones that exist"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("beginPlay")));
		}
	}

	// destroy with no target does nothing rather than guessing.
	{
		const TSharedPtr<FJsonObject> Response =
			MCPTransientTestRun(Registry, TEXT("destroy_transient_actor"), MakeShared<FJsonObject>());
		if (Response.IsValid())
		{
			TestFalse(TEXT("destroy with no target fails"), Response->GetBoolField(TEXT("success")));
		}
	}

	// The real round trip, in whatever map is open.
	TSharedPtr<FJsonObject> SpawnRequest = MakeShared<FJsonObject>();
	SpawnRequest->SetStringField(TEXT("actorClass"), TEXT("StaticMeshActor"));
	SpawnRequest->SetStringField(TEXT("label"), TEXT("UEMCP_TransientContractTest"));
	const TSharedPtr<FJsonObject> SpawnResponse =
		MCPTransientTestRun(Registry, TEXT("spawn_transient_actor"), SpawnRequest);
	if (!SpawnResponse.IsValid() || !SpawnResponse->GetBoolField(TEXT("success")))
	{
		// No editor world in this context, and this test may not open a map.
		return true;
	}

	const TSharedPtr<FJsonObject> Actor = SpawnResponse->GetObjectField(TEXT("actor"));
	const FString ActorPath = Actor->GetStringField(TEXT("actorPath"));
	TestTrue(TEXT("the spawned actor really is RF_Transient"), Actor->GetBoolField(TEXT("transient")));

	// The #966-shaped guarantee: a verification spawn that dirties the map has
	// failed at its one job, label included, since SetActorLabel dirties by
	// default and this passes bMarkDirty=false.
	TestFalse(TEXT("spawning a verification actor dirties nothing"),
		SpawnResponse->GetBoolField(TEXT("dirtiedPackages")));

	// It shows up in the listing, so nothing can be left behind unnoticed.
	{
		const TSharedPtr<FJsonObject> ListResponse =
			MCPTransientTestRun(Registry, TEXT("list_transient_actors"), MakeShared<FJsonObject>());
		if (ListResponse.IsValid() && ListResponse->GetBoolField(TEXT("success")))
		{
			TestTrue(TEXT("the listing sees it"),
				static_cast<int32>(ListResponse->GetNumberField(TEXT("total"))) >= 1);
		}
	}

	// And cleanup destroys exactly it.
	TSharedPtr<FJsonObject> DestroyRequest = MakeShared<FJsonObject>();
	DestroyRequest->SetStringField(TEXT("actorPath"), ActorPath);
	const TSharedPtr<FJsonObject> DestroyResponse =
		MCPTransientTestRun(Registry, TEXT("destroy_transient_actor"), DestroyRequest);
	TestTrue(TEXT("destroy succeeds"), DestroyResponse.IsValid() && DestroyResponse->GetBoolField(TEXT("success")));
	if (DestroyResponse.IsValid())
	{
		TestEqual(TEXT("destroy removed exactly one actor"),
			static_cast<int32>(DestroyResponse->GetNumberField(TEXT("destroyed"))), 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelSetComponentMaterialsContractTest,
	"UE.MCP.Level.SetComponentMaterials.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelSetComponentMaterialsContractTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	// No selector would mean every actor in the level.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("material"), TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
		const TSharedPtr<FJsonObject> Response =
			MCPTransientTestRun(Registry, TEXT("set_component_materials"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("no selector fails"), Response->GetBoolField(TEXT("success")));
		}
	}

	// A selector but no instruction about what to write.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("labelPrefix"), TEXT("Anything"));
		const TSharedPtr<FJsonObject> Response =
			MCPTransientTestRun(Registry, TEXT("set_component_materials"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("no materials fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and lists the three modes"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("clearOverrides")));
		}
	}

	// Two modes at once would fight over the same slots.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("labelPrefix"), TEXT("Anything"));
		Request->SetStringField(TEXT("material"), TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
		Request->SetBoolField(TEXT("clearOverrides"), true);
		const TSharedPtr<FJsonObject> Response =
			MCPTransientTestRun(Registry, TEXT("set_component_materials"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("two modes fail"), Response->GetBoolField(TEXT("success")));
		}
	}

	// A material path that does not resolve fails the whole call rather than
	// half-applying across the matched actors.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("labelPrefix"), TEXT("Anything"));
		Request->SetStringField(TEXT("material"), TEXT("/Game/UEMCPNoSuchMaterialForTests"));
		const TSharedPtr<FJsonObject> Response =
			MCPTransientTestRun(Registry, TEXT("set_component_materials"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unresolvable material fails before any write"),
				Response->GetBoolField(TEXT("success")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
