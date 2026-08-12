#include "LevelHandlers.h"

#include "HandlerUtils.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

namespace
{
	constexpr int32 DefaultStaticMeshResults = 100;
	constexpr int32 MaxStaticMeshResults = 500;
	constexpr int32 HardMaxUniqueStaticMeshes = 8192;
	constexpr int32 DefaultStaticMeshOccurrences = 32;
	constexpr int32 MaxStaticMeshOccurrences = 256;

	struct FStaticMeshAggregate
	{
		FString MeshPath;
		FString LastActorPath;
		int64 ActorCount = 0;
		int64 ComponentCount = 0;
		int64 PlacementCount = 0;
	};

	int64 GetPlacementCount(const UStaticMeshComponent* Component)
	{
		if (const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component))
		{
			return FMath::Max(0, Instanced->GetInstanceCount());
		}

		return 1;
	}

	void GetSortedActors(UWorld* World, TArray<AActor*>& OutActors)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				OutActors.Add(*It);
			}
		}

		OutActors.Sort([](const AActor& Left, const AActor& Right)
		{
			return Left.GetPathName() < Right.GetPathName();
		});
	}

	void GetSortedStaticMeshComponents(AActor* Actor, TArray<UStaticMeshComponent*>& OutComponents)
	{
		Actor->GetComponents<UStaticMeshComponent>(OutComponents);
		OutComponents.RemoveAll([](const UStaticMeshComponent* Component)
		{
			return !IsValid(Component);
		});
		OutComponents.Sort([](const UStaticMeshComponent& Left, const UStaticMeshComponent& Right)
		{
			return Left.GetPathName() < Right.GetPathName();
		});
	}
}

TSharedPtr<FJsonValue> FLevelHandlers::SummarizeStaticMeshUsage(const TSharedPtr<FJsonObject>& Params)
{
	check(IsInGameThread());

	const FString RequestedWorld = OptionalString(Params, TEXT("world"), TEXT("editor"));
	const bool bEditorWorld = RequestedWorld.Equals(TEXT("editor"), ESearchCase::IgnoreCase);
	const bool bPieWorld = RequestedWorld.Equals(TEXT("pie"), ESearchCase::IgnoreCase);
	if (!bEditorWorld && !bPieWorld)
	{
		return MCPError(TEXT("Invalid 'world': expected 'editor' or 'pie'"));
	}

	const FString WorldScope = bEditorWorld ? TEXT("editor") : TEXT("pie");
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));
	}

	const int32 MaxResults = FMath::Clamp(
		OptionalInt(Params, TEXT("maxResults"), DefaultStaticMeshResults),
		1,
		MaxStaticMeshResults);
	const bool bIncludeOccurrences = OptionalBool(Params, TEXT("includeOccurrences"), false);
	const int32 MaxOccurrences = FMath::Clamp(
		OptionalInt(Params, TEXT("maxOccurrences"), DefaultStaticMeshOccurrences),
		0,
		MaxStaticMeshOccurrences);

	// Actor/component iteration order is not an API guarantee. Exact object-path
	// ordering makes aggregation and the bounded occurrence sample reproducible.
	TArray<AActor*> Actors;
	GetSortedActors(World, Actors);

	TMap<FString, FStaticMeshAggregate> Aggregates;
	int64 StaticMeshActors = 0;
	int64 ScannedStaticMeshComponents = 0;
	int64 NullMeshComponents = 0;
	int64 TotalComponentCount = 0;
	int64 TotalPlacementCount = 0;

	for (AActor* Actor : Actors)
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		TArray<UStaticMeshComponent*> Components;
		GetSortedStaticMeshComponents(Actor, Components);
		if (Components.Num() > 0)
		{
			++StaticMeshActors;
		}

		const FString ActorPath = Actor->GetPathName();
		for (UStaticMeshComponent* Component : Components)
		{
			++ScannedStaticMeshComponents;
			UStaticMesh* Mesh = Component->GetStaticMesh();
			if (!IsValid(Mesh))
			{
				++NullMeshComponents;
				continue;
			}

			const FString MeshPath = Mesh->GetPathName();
			FStaticMeshAggregate* Aggregate = Aggregates.Find(MeshPath);
			if (!Aggregate)
			{
				if (Aggregates.Num() >= HardMaxUniqueStaticMeshes)
				{
					return MCPError(FString::Printf(
						TEXT("Static mesh usage exceeds the hard limit of %d unique loaded meshes"),
						HardMaxUniqueStaticMeshes));
				}

				FStaticMeshAggregate NewAggregate;
				NewAggregate.MeshPath = MeshPath;
				Aggregate = &Aggregates.Add(MeshPath, MoveTemp(NewAggregate));
			}

			const int64 PlacementCount = GetPlacementCount(Component);
			++TotalComponentCount;
			TotalPlacementCount += PlacementCount;
			++Aggregate->ComponentCount;
			Aggregate->PlacementCount += PlacementCount;
			if (Aggregate->LastActorPath != ActorPath)
			{
				Aggregate->LastActorPath = ActorPath;
				++Aggregate->ActorCount;
			}
		}
	}

	TArray<FString> SortedMeshPaths;
	Aggregates.GenerateKeyArray(SortedMeshPaths);
	SortedMeshPaths.Sort([&Aggregates](const FString& LeftPath, const FString& RightPath)
	{
		const FStaticMeshAggregate& Left = Aggregates.FindChecked(LeftPath);
		const FStaticMeshAggregate& Right = Aggregates.FindChecked(RightPath);
		if (Left.PlacementCount != Right.PlacementCount)
		{
			return Left.PlacementCount > Right.PlacementCount;
		}
		return LeftPath < RightPath;
	});

	const int32 ReturnedMeshCount = FMath::Min(MaxResults, SortedMeshPaths.Num());
	SortedMeshPaths.SetNum(ReturnedMeshCount);
	TSet<FString> ReturnedMeshPaths;
	ReturnedMeshPaths.Reserve(ReturnedMeshCount);
	for (const FString& MeshPath : SortedMeshPaths)
	{
		ReturnedMeshPaths.Add(MeshPath);
	}

	// Occurrences are examples, globally capped across only the returned meshes.
	// A second deterministic pass keeps omitted aggregates from consuming the cap.
	TMap<FString, TArray<TSharedPtr<FJsonValue>>> OccurrencesByMesh;
	int32 ReturnedOccurrences = 0;
	if (bIncludeOccurrences && MaxOccurrences > 0)
	{
		for (AActor* Actor : Actors)
		{
			if (!IsValid(Actor) || ReturnedOccurrences >= MaxOccurrences)
			{
				break;
			}

			TArray<UStaticMeshComponent*> Components;
			GetSortedStaticMeshComponents(Actor, Components);
			for (UStaticMeshComponent* Component : Components)
			{
				if (ReturnedOccurrences >= MaxOccurrences)
				{
					break;
				}

				UStaticMesh* Mesh = Component->GetStaticMesh();
				if (!IsValid(Mesh) || !ReturnedMeshPaths.Contains(Mesh->GetPathName()))
				{
					continue;
				}

				TSharedPtr<FJsonObject> Occurrence = MakeShared<FJsonObject>();
				Occurrence->SetStringField(TEXT("actorPath"), Actor->GetPathName());
				Occurrence->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
				Occurrence->SetStringField(TEXT("componentPath"), Component->GetPathName());
				Occurrence->SetStringField(TEXT("componentName"), Component->GetName());
				Occurrence->SetStringField(TEXT("componentClass"), Component->GetClass()->GetPathName());
				Occurrence->SetNumberField(TEXT("placementCount"), static_cast<double>(GetPlacementCount(Component)));
				OccurrencesByMesh.FindOrAdd(Mesh->GetPathName()).Add(MakeShared<FJsonValueObject>(Occurrence));
				++ReturnedOccurrences;
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> MeshesJson;
	MeshesJson.Reserve(ReturnedMeshCount);
	int64 ReturnedComponentCount = 0;
	for (const FString& MeshPath : SortedMeshPaths)
	{
		const FStaticMeshAggregate& Aggregate = Aggregates.FindChecked(MeshPath);
		ReturnedComponentCount += Aggregate.ComponentCount;

		TSharedPtr<FJsonObject> MeshJson = MakeShared<FJsonObject>();
		MeshJson->SetStringField(TEXT("meshPath"), Aggregate.MeshPath);
		MeshJson->SetNumberField(TEXT("actorCount"), static_cast<double>(Aggregate.ActorCount));
		MeshJson->SetNumberField(TEXT("componentCount"), static_cast<double>(Aggregate.ComponentCount));
		MeshJson->SetNumberField(TEXT("placementCount"), static_cast<double>(Aggregate.PlacementCount));

		if (bIncludeOccurrences)
		{
			const TArray<TSharedPtr<FJsonValue>>* Occurrences = OccurrencesByMesh.Find(MeshPath);
			MeshJson->SetArrayField(
				TEXT("occurrences"),
				Occurrences ? *Occurrences : TArray<TSharedPtr<FJsonValue>>());
			MeshJson->SetBoolField(
				TEXT("occurrencesTruncated"),
				!Occurrences || Occurrences->Num() < Aggregate.ComponentCount);
		}

		MeshesJson.Add(MakeShared<FJsonValueObject>(MeshJson));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("world"), WorldScope);
	Result->SetStringField(TEXT("worldName"), World->GetName());
	Result->SetBoolField(TEXT("loadedOnly"), true);
	Result->SetNumberField(TEXT("maxResults"), MaxResults);
	Result->SetBoolField(TEXT("includeOccurrences"), bIncludeOccurrences);
	Result->SetNumberField(TEXT("maxOccurrences"), MaxOccurrences);
	Result->SetNumberField(TEXT("hardUniqueMeshLimit"), HardMaxUniqueStaticMeshes);
	Result->SetNumberField(TEXT("actorsScanned"), Actors.Num());
	Result->SetNumberField(TEXT("staticMeshActors"), static_cast<double>(StaticMeshActors));
	Result->SetNumberField(TEXT("scannedStaticMeshComponents"), static_cast<double>(ScannedStaticMeshComponents));
	Result->SetNumberField(TEXT("nullMeshComponents"), static_cast<double>(NullMeshComponents));
	Result->SetNumberField(TEXT("totalComponentCount"), static_cast<double>(TotalComponentCount));
	Result->SetNumberField(TEXT("totalPlacementCount"), static_cast<double>(TotalPlacementCount));
	Result->SetNumberField(TEXT("totalUniqueMeshes"), Aggregates.Num());
	Result->SetNumberField(TEXT("returnedMeshCount"), ReturnedMeshCount);
	Result->SetBoolField(TEXT("truncated"), ReturnedMeshCount < Aggregates.Num());
	Result->SetNumberField(TEXT("totalOccurrences"), static_cast<double>(TotalComponentCount));
	Result->SetNumberField(TEXT("eligibleOccurrencesForReturnedMeshes"), static_cast<double>(ReturnedComponentCount));
	Result->SetNumberField(TEXT("returnedOccurrences"), ReturnedOccurrences);
	Result->SetBoolField(
		TEXT("occurrencesTruncated"),
		bIncludeOccurrences && ReturnedOccurrences < TotalComponentCount);
	Result->SetArrayField(TEXT("meshes"), MeshesJson);
	return MCPResult(Result);
}
