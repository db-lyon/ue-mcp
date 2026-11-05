// A spawned actor must render without a nudge (#1548). A stationary light
// needs a preview shadow map channel to light the scene, a static mesh set on
// a component needs the engine setter to get bounds and a render state, and a
// misspelt function argument must be refused rather than silently dropped.
// Runs on transient verification actors, destroyed at the end.

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "HandlerRegistry.h"
#include "Handlers/Editor/EditorHandlers.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> MCPSpawnRenderTestRun(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Request);
		return Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : nullptr;
	}

	AActor* MCPSpawnRenderTestSpawn(FMCPHandlerRegistry& Registry, const TCHAR* ActorClass, const TCHAR* Label)
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorClass"), ActorClass);
		Request->SetStringField(TEXT("label"), Label);
		const TSharedPtr<FJsonObject> Spawned = MCPSpawnRenderTestRun(Registry, TEXT("spawn_transient_actor"), Request);
		if (!Spawned.IsValid() || !Spawned->GetBoolField(TEXT("success"))) return nullptr;
		return FindObject<AActor>(nullptr, *Spawned->GetObjectField(TEXT("actor"))->GetStringField(TEXT("actorPath")));
	}

	void MCPSpawnRenderTestDestroy(FMCPHandlerRegistry& Registry, AActor* Actor)
	{
		if (!Actor) return;
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		MCPSpawnRenderTestRun(Registry, TEXT("destroy_transient_actor"), Request);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelSpawnedLightLightsTest,
	"UE.MCP.Level.SpawnRenderState.StationaryLightGetsAChannel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelSpawnedLightLightsTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	AActor* Light = MCPSpawnRenderTestSpawn(Registry, TEXT("DirectionalLight"), TEXT("UEMCP_SpawnRenderLight"));
	if (!Light)
	{
		// No editor world in this context, and this test may not open a map.
		return true;
	}

	ULightComponent* Component = Light->FindComponentByClass<ULightComponent>();
	TestNotNull(TEXT("the directional light has a light component"), Component);
	if (Component && Component->HasStaticShadowing() && !Component->HasStaticLighting() && Component->CastShadows && Component->CastStaticShadows)
	{
		// Every directional light overlaps every other, and only four stationary
		// lights can share a channel set, so the claim holds below that count.
		int32 OtherStationary = 0;
		for (TObjectIterator<ULightComponent> It; It; ++It)
		{
			if (*It != Component && It->GetOwner() && It->GetWorld() == Light->GetWorld()
				&& It->HasStaticShadowing() && !It->HasStaticLighting())
			{
				++OtherStationary;
			}
		}
		if (OtherStationary < 4)
		{
			TestNotEqual(TEXT("a spawned stationary light has a preview shadow map channel"),
				Component->PreviewShadowMapChannel, static_cast<int32>(INDEX_NONE));
		}
	}

	MCPSpawnRenderTestDestroy(Registry, Light);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelSpawnedStaticMeshDrawsTest,
	"UE.MCP.Level.SpawnRenderState.StaticMeshGoesThroughTheSetter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelSpawnedStaticMeshDrawsTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);
	FEditorHandlers::RegisterHandlers(Registry);

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Cube || !Sphere)
	{
		AddError(TEXT("The engine basic shapes did not load"));
		return false;
	}

	AActor* Actor = MCPSpawnRenderTestSpawn(Registry, TEXT("StaticMeshActor"), TEXT("UEMCP_SpawnRenderMesh"));
	if (!Actor)
	{
		return true;
	}
	UStaticMeshComponent* Component = Actor->FindComponentByClass<UStaticMeshComponent>();
	TestNotNull(TEXT("the static mesh actor has its component"), Component);
	if (!Component)
	{
		MCPSpawnRenderTestDestroy(Registry, Actor);
		return false;
	}

	// set_component_property routes StaticMesh through SetStaticMesh, so the
	// bounds are the cube's rather than the empty component's.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Request->SetStringField(TEXT("componentName"), Component->GetName());
		Request->SetStringField(TEXT("propertyName"), TEXT("StaticMesh"));
		Request->SetStringField(TEXT("value"), TEXT("/Engine/BasicShapes/Cube"));
		const TSharedPtr<FJsonObject> Response = MCPSpawnRenderTestRun(Registry, TEXT("set_component_property"), Request);
		TestTrue(TEXT("set_component_property StaticMesh succeeds"), Response.IsValid() && Response->GetBoolField(TEXT("success")));
		TestTrue(TEXT("the component holds the cube"), Component->GetStaticMesh() == Cube);
		TestTrue(TEXT("the bounds are the cube's"), Component->Bounds.SphereRadius > 50.0);
		if (Response.IsValid())
		{
			TestTrue(TEXT("the write reports a change"), Response->GetBoolField(TEXT("changed")));
		}
	}

	// A misspelt argument is refused by name instead of calling SetStaticMesh
	// with no mesh, which returned false and changed nothing.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("Mesh"), TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("objectPath"), Component->GetPathName());
		Request->SetStringField(TEXT("functionName"), TEXT("SetStaticMesh"));
		Request->SetObjectField(TEXT("args"), Args);
		const TSharedPtr<FJsonObject> Response = MCPSpawnRenderTestRun(Registry, TEXT("invoke_object_function"), Request);
		TestTrue(TEXT("an unknown argument name fails"), Response.IsValid() && !Response->GetBoolField(TEXT("success")));
		if (Response.IsValid())
		{
			const FString Error = Response->GetStringField(TEXT("error"));
			TestTrue(TEXT("and names the real parameter"), Error.Contains(TEXT("NewMesh")));
		}
		TestTrue(TEXT("the refused call changed nothing"), Component->GetStaticMesh() == Cube);
	}

	// The real name still works, and the call reports the setter's verdict.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("NewMesh"), TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("objectPath"), Component->GetPathName());
		Request->SetStringField(TEXT("functionName"), TEXT("SetStaticMesh"));
		Request->SetObjectField(TEXT("args"), Args);
		const TSharedPtr<FJsonObject> Response = MCPSpawnRenderTestRun(Registry, TEXT("invoke_object_function"), Request);
		TestTrue(TEXT("SetStaticMesh with NewMesh succeeds"), Response.IsValid() && Response->GetBoolField(TEXT("success")));
		TestTrue(TEXT("the component holds the sphere"), Component->GetStaticMesh() == Sphere);
	}

	MCPSpawnRenderTestDestroy(Registry, Actor);
	return true;
}

#endif
