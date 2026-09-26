// Coverage for per-ISM instance counts in get_component_tree (#986).
//
// The tree reported an instanced mesh component's class, its mesh and its
// materials, and not how many instances it held. That is the one number that
// decides whether to touch it: a component with three instances and one with
// three hundred thousand were the same row. Getting it meant dumping every
// instance transform through get_instance_transforms just to count them.
//
// The round trip runs on a transient verification actor, which cannot be saved
// into the map and is destroyed at the end.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> MCPInstanceCountTestRun(
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

	/** One transform entry for add_hismc_instances. */
	TSharedPtr<FJsonValue> MCPInstanceCountTestTransform(double X)
	{
		TSharedPtr<FJsonObject> Location = MakeShared<FJsonObject>();
		Location->SetNumberField(TEXT("x"), X);
		Location->SetNumberField(TEXT("y"), 0.0);
		Location->SetNumberField(TEXT("z"), 0.0);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetObjectField(TEXT("location"), Location);
		return MakeShared<FJsonValueObject>(Entry);
	}

	/** The component row for a named component in a get_component_tree response. */
	TSharedPtr<FJsonObject> MCPInstanceCountTestFindComponent(
		const TSharedPtr<FJsonObject>& Response,
		const FString& NameContains)
	{
		if (!Response.IsValid()) return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Response->TryGetArrayField(TEXT("components"), Rows) || !Rows) return nullptr;
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Row.IsValid()) continue;
			if (Row->GetStringField(TEXT("name")).Contains(NameContains, ESearchCase::IgnoreCase)) return Row;
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelComponentTreeInstanceCountTest,
	"UE.MCP.Level.ComponentTree.ReportsInstanceCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelComponentTreeInstanceCountTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("get_component_tree is registered"), Registry.HasHandler(TEXT("get_component_tree")));

	TSharedPtr<FJsonObject> SpawnRequest = MakeShared<FJsonObject>();
	SpawnRequest->SetStringField(TEXT("actorClass"), TEXT("StaticMeshActor"));
	SpawnRequest->SetStringField(TEXT("label"), TEXT("UEMCP_InstanceCountTest"));
	const TSharedPtr<FJsonObject> SpawnResponse =
		MCPInstanceCountTestRun(Registry, TEXT("spawn_transient_actor"), SpawnRequest);
	if (!SpawnResponse.IsValid() || !SpawnResponse->GetBoolField(TEXT("success")))
	{
		// No editor world in this context, and this test may not open a map.
		return true;
	}
	const TSharedPtr<FJsonObject> SpawnedActor = SpawnResponse->GetObjectField(TEXT("actor"));
	const FString ActorPath = SpawnedActor.IsValid() ? SpawnedActor->GetStringField(TEXT("actorPath")) : FString();
	const FString ActorLabel = TEXT("UEMCP_InstanceCountTest");
	const FString ComponentName = TEXT("UEMCP_TestHISM");

	bool bAddedComponent = false;
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), ActorLabel);
		Request->SetStringField(TEXT("componentClass"), TEXT("HierarchicalInstancedStaticMeshComponent"));
		Request->SetStringField(TEXT("componentName"), ComponentName);
		const TSharedPtr<FJsonObject> Response =
			MCPInstanceCountTestRun(Registry, TEXT("add_component_to_actor"), Request);
		bAddedComponent = Response.IsValid() && Response->GetBoolField(TEXT("success"));
	}

	if (bAddedComponent)
	{
		// An empty instanced component still has to report a count. Zero is an
		// answer; a missing field is not, because a caller cannot tell it apart
		// from a field that was trimmed.
		{
			TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
			Request->SetStringField(TEXT("actorLabel"), ActorLabel);
			const TSharedPtr<FJsonObject> Response =
				MCPInstanceCountTestRun(Registry, TEXT("get_component_tree"), Request);
			const TSharedPtr<FJsonObject> Row = MCPInstanceCountTestFindComponent(Response, ComponentName);
			TestNotNull(TEXT("the tree lists the instanced component"), Row.Get());
			if (Row.IsValid())
			{
				double Count = -1.0;
				TestTrue(TEXT("an empty ISM still reports instanceCount"),
					Row->TryGetNumberField(TEXT("instanceCount"), Count));
				TestEqual(TEXT("which is zero"), static_cast<int32>(Count), 0);
				const TSharedPtr<FJsonObject>* Instanced = nullptr;
				if (Row->TryGetObjectField(TEXT("instanced"), Instanced) && Instanced)
				{
					TestTrue(TEXT("and a HISM is reported as hierarchical"),
						(*Instanced)->GetBoolField(TEXT("hierarchical")));
				}
			}
		}

		// Add instances, then read the count back through the tree rather than
		// through the writer's own report.
		{
			TArray<TSharedPtr<FJsonValue>> Transforms;
			Transforms.Add(MCPInstanceCountTestTransform(0.0));
			Transforms.Add(MCPInstanceCountTestTransform(100.0));
			Transforms.Add(MCPInstanceCountTestTransform(200.0));
			TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
			Request->SetStringField(TEXT("actorLabel"), ActorLabel);
			Request->SetStringField(TEXT("componentName"), ComponentName);
			Request->SetArrayField(TEXT("transforms"), Transforms);
			const TSharedPtr<FJsonObject> AddResponse =
				MCPInstanceCountTestRun(Registry, TEXT("add_hismc_instances"), Request);
			if (AddResponse.IsValid() && AddResponse->GetBoolField(TEXT("success")))
			{
				TSharedPtr<FJsonObject> TreeRequest = MakeShared<FJsonObject>();
				TreeRequest->SetStringField(TEXT("actorLabel"), ActorLabel);
				const TSharedPtr<FJsonObject> TreeResponse =
					MCPInstanceCountTestRun(Registry, TEXT("get_component_tree"), TreeRequest);
				const TSharedPtr<FJsonObject> Row =
					MCPInstanceCountTestFindComponent(TreeResponse, ComponentName);
				if (Row.IsValid())
				{
					double Count = -1.0;
					Row->TryGetNumberField(TEXT("instanceCount"), Count);
					TestEqual(TEXT("the tree reports the instances that were added"),
						static_cast<int32>(Count), 3);
				}
			}
		}
	}

	// A component that holds no instances at all must NOT grow the field, so a
	// caller can use its presence to mean "this is an instanced component".
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), ActorLabel);
		const TSharedPtr<FJsonObject> Response =
			MCPInstanceCountTestRun(Registry, TEXT("get_component_tree"), Request);
		const TSharedPtr<FJsonObject> Root =
			MCPInstanceCountTestFindComponent(Response, TEXT("StaticMeshComponent"));
		if (Root.IsValid())
		{
			double Count = -1.0;
			TestFalse(TEXT("a plain StaticMeshComponent has no instanceCount"),
				Root->TryGetNumberField(TEXT("instanceCount"), Count));
		}
	}

	if (!ActorPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> DestroyRequest = MakeShared<FJsonObject>();
		DestroyRequest->SetStringField(TEXT("actorPath"), ActorPath);
		const TSharedPtr<FJsonObject> DestroyResponse =
			MCPInstanceCountTestRun(Registry, TEXT("destroy_transient_actor"), DestroyRequest);
		TestTrue(TEXT("the verification actor is destroyed"),
			DestroyResponse.IsValid() && DestroyResponse->GetBoolField(TEXT("success")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
