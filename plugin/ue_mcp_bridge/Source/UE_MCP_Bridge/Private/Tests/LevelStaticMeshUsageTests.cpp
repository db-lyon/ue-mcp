#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/LevelHandlers.h"
#include "HandlerUtils.h"
#include "Misc/AutomationTest.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	constexpr const TCHAR* StaticMeshUsageMethod = TEXT("summarize_static_mesh_usage");

	class FScopedEditorTestWorld
	{
	public:
		FScopedEditorTestWorld()
		{
			if (!GEditor)
			{
				return;
			}

			OriginalWorld = GEditor->GetEditorWorldContext().World();
			const FName WorldName = MakeUniqueObjectName(
				GetTransientPackage(),
				UWorld::StaticClass(),
				FName(TEXT("UEMCP_StaticMeshUsageTestWorld")));
			const UWorld::InitializationValues InitializationValues = UWorld::InitializationValues()
				.InitializeScenes(false)
				.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(false)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(false)
				.SetTransactional(false)
				.CreateFXSystem(false)
				.CreateWorldPartition(false);

			TestWorld = UWorld::CreateWorld(
				EWorldType::Editor,
				false,
				WorldName,
				GetTransientPackage(),
				true,
				ERHIFeatureLevel::Num,
				&InitializationValues);
			if (TestWorld)
			{
				GEditor->GetEditorWorldContext().SetCurrentWorld(TestWorld);
			}
		}

		~FScopedEditorTestWorld()
		{
			if (GEditor && TestWorld)
			{
				GEditor->GetEditorWorldContext().SetCurrentWorld(OriginalWorld);
			}

			if (TestWorld)
			{
				TestWorld->DestroyWorld(false);
			}
		}

		UWorld* Get() const
		{
			return TestWorld;
		}

	private:
		UWorld* OriginalWorld = nullptr;
		UWorld* TestWorld = nullptr;
	};

	AActor* SpawnTransientActor(UWorld* World, const TCHAR* Name, const TCHAR* Label)
	{
		if (!World)
		{
			return nullptr;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = MakeUniqueObjectName(
			World->PersistentLevel,
			AActor::StaticClass(),
			FName(Name));
		SpawnParameters.ObjectFlags = RF_Transient;
		AActor* Actor = World->SpawnActor<AActor>(
			AActor::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
		if (Actor)
		{
			Actor->SetActorLabel(Label, false);
		}
		return Actor;
	}

	template <typename ComponentType>
	ComponentType* AddTransientMeshComponent(
		AActor* Actor,
		const TCHAR* Name,
		UStaticMesh* Mesh)
	{
		if (!Actor)
		{
			return nullptr;
		}

		ComponentType* Component = NewObject<ComponentType>(Actor, Name, RF_Transient);
		Actor->AddInstanceComponent(Component);
		Component->SetStaticMesh(Mesh);
		return Component;
	}

	TSharedPtr<FJsonObject> ExecuteStaticMeshUsage(
		FMCPHandlerRegistry& Registry,
		int32 MaxResults,
		bool bIncludeOccurrences,
		int32 MaxOccurrences)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("world"), TEXT("editor"));
		Params->SetNumberField(TEXT("maxResults"), MaxResults);
		Params->SetBoolField(TEXT("includeOccurrences"), bIncludeOccurrences);
		Params->SetNumberField(TEXT("maxOccurrences"), MaxOccurrences);

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(StaticMeshUsageMethod, Params);
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}

	TSharedPtr<FJsonObject> FindMeshResult(
		const TArray<TSharedPtr<FJsonValue>>& Meshes,
		const FString& MeshPath)
	{
		for (const TSharedPtr<FJsonValue>& MeshValue : Meshes)
		{
			if (!MeshValue.IsValid() || MeshValue->Type != EJson::Object)
			{
				continue;
			}

			const TSharedPtr<FJsonObject>& Mesh = MeshValue->AsObject();
			if (Mesh.IsValid() && Mesh->GetStringField(TEXT("meshPath")) == MeshPath)
			{
				return Mesh;
			}
		}
		return TSharedPtr<FJsonObject>();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelStaticMeshUsageRegistrationTest,
	"UE.MCP.Level.SummarizeStaticMeshUsage.RegistrationAndWorldValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelStaticMeshUsageRegistrationTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("static-mesh usage handler is registered"), Registry.HasHandler(StaticMeshUsageMethod));

	TSharedPtr<FJsonObject> InvalidWorldParams = MakeShared<FJsonObject>();
	InvalidWorldParams->SetStringField(TEXT("world"), TEXT("auto"));
	const TSharedPtr<FJsonValue> InvalidWorldResponse = Registry.ExecuteHandler(
		StaticMeshUsageMethod,
		InvalidWorldParams);
	TestTrue(
		TEXT("an invalid editor/PIE scope returns an object"),
		InvalidWorldResponse.IsValid() && InvalidWorldResponse->Type == EJson::Object);
	if (InvalidWorldResponse.IsValid() && InvalidWorldResponse->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Result = InvalidWorldResponse->AsObject();
		TestFalse(TEXT("an invalid world scope is unsuccessful"), Result->GetBoolField(TEXT("success")));
		TestTrue(
			TEXT("the scope error names both supported choices"),
			Result->GetStringField(TEXT("error")).Contains(TEXT("editor")) &&
			Result->GetStringField(TEXT("error")).Contains(TEXT("pie")));
	}

	// The handler must not silently substitute the editor world when PIE was
	// explicitly requested. Automation normally runs outside PIE; retain the
	// invalid-scope assertion above when a caller deliberately runs it in PIE.
	if (GetPIEWorld() == nullptr)
	{
		TSharedPtr<FJsonObject> PieParams = MakeShared<FJsonObject>();
		PieParams->SetStringField(TEXT("world"), TEXT("pie"));
		const TSharedPtr<FJsonValue> PieResponse = Registry.ExecuteHandler(StaticMeshUsageMethod, PieParams);
		TestTrue(
			TEXT("missing PIE returns an object"),
			PieResponse.IsValid() && PieResponse->Type == EJson::Object);
		if (PieResponse.IsValid() && PieResponse->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Result = PieResponse->AsObject();
			TestFalse(TEXT("missing PIE is unsuccessful"), Result->GetBoolField(TEXT("success")));
			TestTrue(
				TEXT("missing PIE is reported as the requested scope"),
				Result->GetStringField(TEXT("error")).Contains(TEXT("pie")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelStaticMeshUsageAggregationTest,
	"UE.MCP.Level.SummarizeStaticMeshUsage.AggregationOrderingAndBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelStaticMeshUsageAggregationTest::RunTest(const FString& Parameters)
{
	FScopedEditorTestWorld TestWorldScope;
	UWorld* World = TestWorldScope.Get();
	if (!TestNotNull(TEXT("a transient editor world was created"), World))
	{
		return false;
	}

	const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UStaticMesh* AlphaMesh = NewObject<UStaticMesh>(
		GetTransientPackage(),
		*(FString(TEXT("UEMCP_Alpha_")) + UniqueSuffix),
		RF_Transient);
	UStaticMesh* BetaMesh = NewObject<UStaticMesh>(
		GetTransientPackage(),
		*(FString(TEXT("UEMCP_Beta_")) + UniqueSuffix),
		RF_Transient);
	UStaticMesh* ZuluMesh = NewObject<UStaticMesh>(
		GetTransientPackage(),
		*(FString(TEXT("UEMCP_Zulu_")) + UniqueSuffix),
		RF_Transient);
	TestNotNull(TEXT("alpha mesh fixture exists"), AlphaMesh);
	TestNotNull(TEXT("beta mesh fixture exists"), BetaMesh);
	TestNotNull(TEXT("zulu mesh fixture exists"), ZuluMesh);
	if (!AlphaMesh || !BetaMesh || !ZuluMesh)
	{
		return false;
	}

	AActor* ActorA = SpawnTransientActor(World, TEXT("UEMCP_Actor_A"), TEXT("Usage Actor A"));
	AActor* ActorB = SpawnTransientActor(World, TEXT("UEMCP_Actor_B"), TEXT("Usage Actor B"));
	if (!TestNotNull(TEXT("first fixture actor exists"), ActorA) ||
		!TestNotNull(TEXT("second fixture actor exists"), ActorB))
	{
		return false;
	}

	UStaticMeshComponent* AlphaPlainA =
		AddTransientMeshComponent<UStaticMeshComponent>(ActorA, TEXT("CompAlphaAPlain"), AlphaMesh);
	UInstancedStaticMeshComponent* AlphaInstances =
		AddTransientMeshComponent<UInstancedStaticMeshComponent>(
			ActorA,
			TEXT("CompAlphaBInstanced"),
			AlphaMesh);
	UInstancedStaticMeshComponent* BetaInstances =
		AddTransientMeshComponent<UInstancedStaticMeshComponent>(
			ActorA,
			TEXT("CompBeta"),
			BetaMesh);
	if (!TestNotNull(TEXT("plain alpha component exists"), AlphaPlainA) ||
		!TestNotNull(TEXT("instanced alpha component exists"), AlphaInstances) ||
		!TestNotNull(TEXT("instanced beta component exists"), BetaInstances))
	{
		return false;
	}
	AlphaInstances->AddInstance(FTransform(FVector(10.0, 0.0, 0.0)));
	AlphaInstances->AddInstance(FTransform(FVector(20.0, 0.0, 0.0)));
	for (int32 Index = 0; Index < 5; ++Index)
	{
		BetaInstances->AddInstance(FTransform(FVector(0.0, Index * 10.0, 0.0)));
	}

	UInstancedStaticMeshComponent* ZuluZeroInstances =
		AddTransientMeshComponent<UInstancedStaticMeshComponent>(
		ActorA,
		TEXT("CompZuluZeroInstances"),
		ZuluMesh);
	UStaticMeshComponent* NullMeshComponent =
		AddTransientMeshComponent<UStaticMeshComponent>(ActorA, TEXT("CompNullMesh"), nullptr);
	UStaticMeshComponent* AlphaPlainB =
		AddTransientMeshComponent<UStaticMeshComponent>(ActorB, TEXT("CompAlphaCPlain"), AlphaMesh);
	UInstancedStaticMeshComponent* ZuluInstances =
		AddTransientMeshComponent<UInstancedStaticMeshComponent>(
			ActorB,
			TEXT("CompZuluFilled"),
			ZuluMesh);
	if (!TestNotNull(TEXT("zero-instance zulu component exists"), ZuluZeroInstances) ||
		!TestNotNull(TEXT("null-mesh component exists"), NullMeshComponent) ||
		!TestNotNull(TEXT("second plain alpha component exists"), AlphaPlainB) ||
		!TestNotNull(TEXT("filled zulu component exists"), ZuluInstances))
	{
		return false;
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		ZuluInstances->AddInstance(FTransform(FVector(0.0, 0.0, Index * 10.0)));
	}

	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);
	const TSharedPtr<FJsonObject> Result = ExecuteStaticMeshUsage(Registry, 3, true, 2);
	if (!TestTrue(TEXT("aggregation returns an object"), Result.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("aggregation succeeds"), Result->GetBoolField(TEXT("success")));
	TestEqual(TEXT("the transient editor scope is reported"), Result->GetStringField(TEXT("world")), FString(TEXT("editor")));
	TestTrue(TEXT("the scan explicitly reports loaded-only semantics"), Result->GetBoolField(TEXT("loadedOnly")));
	TestEqual(TEXT("the requested row cap is reported"), static_cast<int32>(Result->GetNumberField(TEXT("maxResults"))), 3);
	TestEqual(TEXT("the requested occurrence cap is reported"), static_cast<int32>(Result->GetNumberField(TEXT("maxOccurrences"))), 2);
	TestEqual(TEXT("all fixture static-mesh components are scanned"), static_cast<int32>(Result->GetNumberField(TEXT("scannedStaticMeshComponents"))), 7);
	TestEqual(TEXT("the null mesh component is diagnosed"), static_cast<int32>(Result->GetNumberField(TEXT("nullMeshComponents"))), 1);
	TestEqual(TEXT("assigned components are counted separately"), static_cast<int32>(Result->GetNumberField(TEXT("totalComponentCount"))), 6);
	TestEqual(TEXT("plain and instanced placements are counted exactly"), static_cast<int32>(Result->GetNumberField(TEXT("totalPlacementCount"))), 13);
	TestEqual(TEXT("three unique meshes are aggregated"), static_cast<int32>(Result->GetNumberField(TEXT("totalUniqueMeshes"))), 3);
	TestEqual(TEXT("all three mesh rows are returned"), static_cast<int32>(Result->GetNumberField(TEXT("returnedMeshCount"))), 3);
	TestFalse(TEXT("the complete result is not truncated"), Result->GetBoolField(TEXT("truncated")));
	TestEqual(TEXT("the global occurrence cap is honored"), static_cast<int32>(Result->GetNumberField(TEXT("returnedOccurrences"))), 2);
	TestEqual(TEXT("all returned-row components contribute to the occurrence total"), static_cast<int32>(Result->GetNumberField(TEXT("totalOccurrences"))), 6);
	TestTrue(TEXT("the occurrence sample reports truncation"), Result->GetBoolField(TEXT("occurrencesTruncated")));

	const TArray<TSharedPtr<FJsonValue>>& Meshes = Result->GetArrayField(TEXT("meshes"));
	TestEqual(TEXT("three mesh rows are present"), Meshes.Num(), 3);
	if (Meshes.Num() == 3)
	{
		TestEqual(TEXT("highest placement row sorts first"), Meshes[0]->AsObject()->GetStringField(TEXT("meshPath")), BetaMesh->GetPathName());
		TestEqual(TEXT("equal-placement rows tie-break by path"), Meshes[1]->AsObject()->GetStringField(TEXT("meshPath")), AlphaMesh->GetPathName());
		TestEqual(TEXT("the remaining equal-placement row sorts last"), Meshes[2]->AsObject()->GetStringField(TEXT("meshPath")), ZuluMesh->GetPathName());
	}

	const TSharedPtr<FJsonObject> AlphaResult = FindMeshResult(Meshes, AlphaMesh->GetPathName());
	const TSharedPtr<FJsonObject> BetaResult = FindMeshResult(Meshes, BetaMesh->GetPathName());
	const TSharedPtr<FJsonObject> ZuluResult = FindMeshResult(Meshes, ZuluMesh->GetPathName());
	if (TestTrue(TEXT("alpha aggregate exists"), AlphaResult.IsValid()))
	{
		TestEqual(TEXT("alpha actor count"), static_cast<int32>(AlphaResult->GetNumberField(TEXT("actorCount"))), 2);
		TestEqual(TEXT("alpha component count"), static_cast<int32>(AlphaResult->GetNumberField(TEXT("componentCount"))), 3);
		TestEqual(TEXT("alpha placement count"), static_cast<int32>(AlphaResult->GetNumberField(TEXT("placementCount"))), 4);
		const TArray<TSharedPtr<FJsonValue>>& Occurrences = AlphaResult->GetArrayField(TEXT("occurrences"));
		TestEqual(TEXT("alpha receives the two deterministic occurrence slots"), Occurrences.Num(), 2);
		if (Occurrences.Num() == 2)
		{
			TestEqual(TEXT("first occurrence is path-ordered"), Occurrences[0]->AsObject()->GetStringField(TEXT("componentName")), FString(TEXT("CompAlphaAPlain")));
			TestEqual(TEXT("second occurrence is path-ordered"), Occurrences[1]->AsObject()->GetStringField(TEXT("componentName")), FString(TEXT("CompAlphaBInstanced")));
		}
		TestTrue(TEXT("alpha reports its omitted occurrence"), AlphaResult->GetBoolField(TEXT("occurrencesTruncated")));
	}
	if (TestTrue(TEXT("beta aggregate exists"), BetaResult.IsValid()))
	{
		TestEqual(TEXT("beta actor count"), static_cast<int32>(BetaResult->GetNumberField(TEXT("actorCount"))), 1);
		TestEqual(TEXT("beta component count"), static_cast<int32>(BetaResult->GetNumberField(TEXT("componentCount"))), 1);
		TestEqual(TEXT("beta placement count"), static_cast<int32>(BetaResult->GetNumberField(TEXT("placementCount"))), 5);
	}
	if (TestTrue(TEXT("zulu aggregate exists"), ZuluResult.IsValid()))
	{
		TestEqual(TEXT("zero-instance components still count as components"), static_cast<int32>(ZuluResult->GetNumberField(TEXT("componentCount"))), 2);
		TestEqual(TEXT("zero-instance components add no placements"), static_cast<int32>(ZuluResult->GetNumberField(TEXT("placementCount"))), 4);
		TestEqual(TEXT("zulu actor count includes both referencing actors"), static_cast<int32>(ZuluResult->GetNumberField(TEXT("actorCount"))), 2);
	}

	const TSharedPtr<FJsonObject> BoundedResult = ExecuteStaticMeshUsage(Registry, 2, false, 256);
	if (TestTrue(TEXT("bounded aggregation returns an object"), BoundedResult.IsValid()))
	{
		TestTrue(TEXT("bounded aggregation succeeds"), BoundedResult->GetBoolField(TEXT("success")));
		TestEqual(TEXT("bounded aggregation reports the effective row cap"), static_cast<int32>(BoundedResult->GetNumberField(TEXT("maxResults"))), 2);
		TestEqual(TEXT("bounded aggregation returns two rows"), static_cast<int32>(BoundedResult->GetNumberField(TEXT("returnedMeshCount"))), 2);
		TestTrue(TEXT("bounded aggregation reports truncation"), BoundedResult->GetBoolField(TEXT("truncated")));
		const TArray<TSharedPtr<FJsonValue>>& BoundedMeshes = BoundedResult->GetArrayField(TEXT("meshes"));
		if (TestEqual(TEXT("bounded aggregation contains two rows"), BoundedMeshes.Num(), 2))
		{
			TestEqual(TEXT("top-N retains the highest placement row"), BoundedMeshes[0]->AsObject()->GetStringField(TEXT("meshPath")), BetaMesh->GetPathName());
			TestEqual(TEXT("top-N deterministically retains the first tied row"), BoundedMeshes[1]->AsObject()->GetStringField(TEXT("meshPath")), AlphaMesh->GetPathName());
		}
	}

	const TSharedPtr<FJsonObject> ClampedResult = ExecuteStaticMeshUsage(Registry, 0, false, 999);
	if (TestTrue(TEXT("out-of-range bounds return an object"), ClampedResult.IsValid()))
	{
		TestTrue(TEXT("clamped aggregation succeeds"), ClampedResult->GetBoolField(TEXT("success")));
		TestEqual(TEXT("the row cap clamps to one"), static_cast<int32>(ClampedResult->GetNumberField(TEXT("maxResults"))), 1);
		TestEqual(TEXT("the occurrence cap clamps to 256"), static_cast<int32>(ClampedResult->GetNumberField(TEXT("maxOccurrences"))), 256);
		TestEqual(TEXT("the clamped result returns one row"), static_cast<int32>(ClampedResult->GetNumberField(TEXT("returnedMeshCount"))), 1);
		TestTrue(TEXT("the clamped top-one result is truncated"), ClampedResult->GetBoolField(TEXT("truncated")));
		const TArray<TSharedPtr<FJsonValue>>& ClampedMeshes = ClampedResult->GetArrayField(TEXT("meshes"));
		if (TestEqual(TEXT("the clamped result contains one row"), ClampedMeshes.Num(), 1))
		{
			TestEqual(TEXT("the clamped top-one row is the highest-use mesh"), ClampedMeshes[0]->AsObject()->GetStringField(TEXT("meshPath")), BetaMesh->GetPathName());
		}
	}

	return true;
}

#endif
