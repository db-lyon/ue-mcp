// get_runtime_values with a component classFilter answers for every matching component on an actor, not the
// first one found: two instanced static mesh components are two rows. Runs on a transient verification actor,
// which cannot be saved into the map and is destroyed at the end.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Editor/EditorHandlers.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> MCPRuntimeValuesTestRun(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Request);
		return Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorRuntimeValuesEveryComponentTest,
	"UE.MCP.Editor.RuntimeValues.EveryMatchingComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorRuntimeValuesEveryComponentTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);
	FEditorHandlers::RegisterHandlers(Registry);

	const FString ActorLabel = TEXT("UEMCP_RuntimeValuesTwoISMs");
	TSharedPtr<FJsonObject> SpawnRequest = MakeShared<FJsonObject>();
	SpawnRequest->SetStringField(TEXT("actorClass"), TEXT("StaticMeshActor"));
	SpawnRequest->SetStringField(TEXT("label"), ActorLabel);
	const TSharedPtr<FJsonObject> Spawned = MCPRuntimeValuesTestRun(Registry, TEXT("spawn_transient_actor"), SpawnRequest);
	if (!Spawned.IsValid() || !Spawned->GetBoolField(TEXT("success")))
	{
		// No editor world in this context, and this test may not open a map.
		return true;
	}
	const FString ActorPath = Spawned->GetObjectField(TEXT("actor"))->GetStringField(TEXT("actorPath"));

	for (const TCHAR* Name : { TEXT("UEMCP_ISM_A"), TEXT("UEMCP_ISM_B") })
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorPath"), ActorPath);
		Request->SetStringField(TEXT("componentClass"), TEXT("InstancedStaticMeshComponent"));
		Request->SetStringField(TEXT("componentName"), Name);
		const TSharedPtr<FJsonObject> Added = MCPRuntimeValuesTestRun(Registry, TEXT("add_component_to_actor"), Request);
		TestTrue(FString::Printf(TEXT("%s is added"), Name), Added.IsValid() && Added->GetBoolField(TEXT("success")));
	}

	TSharedPtr<FJsonObject> Read = MakeShared<FJsonObject>();
	Read->SetStringField(TEXT("classFilter"), TEXT("InstancedStaticMeshComponent"));
	Read->SetStringField(TEXT("world"), TEXT("editor"));
	Read->SetArrayField(TEXT("paths"), TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueString>(TEXT("GetInstanceCount")) });
	const TSharedPtr<FJsonObject> Values = MCPRuntimeValuesTestRun(Registry, TEXT("get_runtime_values"), Read);
	TArray<FString> Components;
	if (Values.IsValid())
	{
		for (const TSharedPtr<FJsonValue>& RowValue : Values->GetArrayField(TEXT("rows")))
		{
			const TSharedPtr<FJsonObject> Row = RowValue->AsObject();
			if (Row->GetStringField(TEXT("actorLabel")) == ActorLabel) Components.Add(Row->GetStringField(TEXT("componentName")));
		}
	}
	TestEqual(TEXT("both matching components are rows"), Components.Num(), 2);
	TestTrue(TEXT("each row names its own component"), Components.Contains(TEXT("UEMCP_ISM_A")) && Components.Contains(TEXT("UEMCP_ISM_B")));
	TestTrue(TEXT("matchedActors is reported"), Values.IsValid() && Values->HasField(TEXT("matchedActors")));

	TSharedPtr<FJsonObject> Destroy = MakeShared<FJsonObject>();
	Destroy->SetStringField(TEXT("actorPath"), ActorPath);
	const TSharedPtr<FJsonObject> Destroyed = MCPRuntimeValuesTestRun(Registry, TEXT("destroy_transient_actor"), Destroy);
	TestTrue(TEXT("the verification actor is destroyed"), Destroyed.IsValid() && Destroyed->GetBoolField(TEXT("success")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
