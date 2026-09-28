// F6 World: the one spawn shape, the label rerun, Abandon, the shared params,
// and the instance-component helper.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Components/PointLightComponent.h"
#include "Families/MCPWorldSpawn.h"
#include "Misc/AutomationTest.h"

namespace MCPWorldSpawnTests
{
	const TCHAR* const Label = TEXT("UEMCPFamilySpawnProbe");

	void RemoveProbes(UWorld* World)
	{
		TArray<AActor*> Doomed;
		MCPCollectActorsByToken(World, Label, EMCPActorMatch::Label, Doomed);
		for (AActor* Actor : Doomed) World->DestroyActor(Actor);
	}

	TSharedPtr<FJsonObject> ProbeParams()
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("label"), Label);
		Params->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(FVector(10, 20, 30)));
		Params->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(FVector(2, 2, 2)));
		return Params;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPWorldSpawnShapeTest, "UE.MCP.Families.World.SpawnShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPWorldSpawnShapeTest::RunTest(const FString& Parameters)
{
	using namespace MCPWorldSpawnTests;
	UWorld* World = GetEditorWorld();
	if (!TestNotNull(TEXT("editor world"), World)) return false;
	RemoveProbes(World);

	{
		FMCPSpawn Spawn(ProbeParams(), { TEXT("Probe") });
		TestNull(TEXT("opens"), Spawn.Open().Get());
		AActor* Actor = Spawn.Spawn<AStaticMeshActor>(AStaticMeshActor::StaticClass());
		if (!TestNotNull(TEXT("spawned"), Actor)) return false;
		const TSharedPtr<FJsonObject> Result = Spawn.Finish();
		TestTrue(TEXT("created"), Result->GetBoolField(TEXT("created")));
		TestEqual(TEXT("actorLabel"), Result->GetStringField(TEXT("actorLabel")), FString(Label));
		TestEqual(TEXT("actorPath"), Result->GetStringField(TEXT("actorPath")), Actor->GetPathName());
		TestEqual(TEXT("actorClass"), Result->GetStringField(TEXT("actorClass")), FString(TEXT("StaticMeshActor")));
		TestEqual(TEXT("location x"), Result->GetObjectField(TEXT("location"))->GetNumberField(TEXT("x")), 10.0);
		TestEqual(TEXT("scale applied"), Result->GetObjectField(TEXT("scale"))->GetNumberField(TEXT("x")), 2.0);
		TestEqual(TEXT("world"), Result->GetStringField(TEXT("world")), FString(TEXT("editor")));
		const TSharedPtr<FJsonObject> Rollback = Result->GetObjectField(TEXT("rollback"));
		TestEqual(TEXT("delete_actor rollback"), Rollback->GetStringField(TEXT("method")), FString(TEXT("delete_actor")));
		TestEqual(TEXT("rollback by path"), Rollback->GetObjectField(TEXT("payload"))->GetStringField(TEXT("actorPath")), Actor->GetPathName());
	}

	// A rerun answers existed; onConflict=error refuses.
	{
		FMCPSpawn Spawn(ProbeParams(), { TEXT("Probe") });
		const TSharedPtr<FJsonValue> Early = Spawn.Open();
		TestTrue(TEXT("a rerun answers existed"), Early.IsValid() && Early->AsObject()->GetBoolField(TEXT("existed")));
	}
	{
		TSharedPtr<FJsonObject> Params = ProbeParams();
		Params->SetStringField(TEXT("onConflict"), TEXT("error"));
		FMCPSpawn Spawn(Params, { TEXT("Probe") });
		const TSharedPtr<FJsonValue> Early = Spawn.Open();
		TestTrue(TEXT("onConflict=error refuses"), Early.IsValid() && !Early->AsObject()->GetBoolField(TEXT("success")));
	}
	RemoveProbes(World);

	// Abandon destroys the half-made actor.
	{
		FMCPSpawn Spawn(ProbeParams(), { TEXT("Probe") });
		Spawn.Open();
		Spawn.Spawn(AStaticMeshActor::StaticClass());
		Spawn.Abandon(MCPError(TEXT("refused")));
		TArray<AActor*> Left;
		MCPCollectActorsByToken(World, Label, EMCPActorMatch::Label, Left);
		TestEqual(TEXT("nothing left after Abandon"), Left.Num(), 0);
	}

	// An unknown pieInstance is refused by name.
	{
		TSharedPtr<FJsonObject> Params = ProbeParams();
		Params->SetStringField(TEXT("world"), TEXT("auto"));
		Params->SetNumberField(TEXT("pieInstance"), 97);
		FMCPSpawn Spawn(Params, { TEXT("Probe") });
		const TSharedPtr<FJsonValue> Early = Spawn.Open();
		FString Error;
		if (Early.IsValid()) Early->AsObject()->TryGetStringField(TEXT("error"), Error);
		TestTrue(TEXT("an unknown pieInstance is named"), Error.Contains(TEXT("pieInstance 97")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPWorldSpawnParamsTest, "UE.MCP.Families.World.SharedParams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPWorldSpawnParamsTest::RunTest(const FString& Parameters)
{
	const TArray<FMCPParamSpec> Specs = FMCPSpawn::ParamsWith({
		MCPParam::Optional(TEXT("scale"), EMCPParamType::Vec3, TEXT("Own wording")),
	});
	TArray<FString> Names;
	for (const FMCPParamSpec& Spec : Specs) Names.Add(Spec.Name);
	for (const TCHAR* Shared : { TEXT("label"), TEXT("onConflict"), TEXT("location"), TEXT("rotation"), TEXT("scale"), TEXT("folderPath"), TEXT("world"), TEXT("pieInstance") })
	{
		TestTrue(FString::Printf(TEXT("%s is declared"), Shared), Names.Contains(Shared));
	}
	TestEqual(TEXT("an own param is not duplicated"), Names.FilterByPredicate([](const FString& N) { return N == TEXT("scale"); }).Num(), 1);
	TestEqual(TEXT("the own wording wins"), Specs[0].Description, FString(TEXT("Own wording")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPWorldSpawnInstanceComponentTest, "UE.MCP.Families.World.InstanceComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPWorldSpawnInstanceComponentTest::RunTest(const FString& Parameters)
{
	UWorld* World = GetEditorWorld();
	if (!TestNotNull(TEXT("editor world"), World)) return false;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, MCPTransientSpawnParameters());
	if (!TestNotNull(TEXT("a bare actor"), Actor)) return false;

	UActorComponent* Root = MCPWorldSpawn::AddInstanceComponent(Actor, USceneComponent::StaticClass(), TEXT("DefaultSceneRoot"));
	TestTrue(TEXT("a rootless actor takes the scene component as root"), Actor->GetRootComponent() == Root);
	UActorComponent* Light = MCPWorldSpawn::AddInstanceComponent(Actor, UPointLightComponent::StaticClass(), TEXT("ProbeLight"));
	TestTrue(TEXT("a second one attaches under the root"), Cast<USceneComponent>(Light) && Cast<USceneComponent>(Light)->GetAttachParent() == Root);
	TestTrue(TEXT("it is an instance component"), Actor->GetInstanceComponents().Contains(Light));
	TestTrue(TEXT("the class resolves by short name"), MCPWorldSpawn::ResolveComponentClass(TEXT("PointLightComponent")) == UPointLightComponent::StaticClass());

	World->DestroyActor(Actor);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
