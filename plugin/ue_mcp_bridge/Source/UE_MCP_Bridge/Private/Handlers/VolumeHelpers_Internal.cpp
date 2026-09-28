#include "VolumeHelpers_Internal.h"

#include "Engine/World.h"
#include "Engine/Brush.h"
#include "Engine/Polys.h"
#include "Components/BrushComponent.h"
#include "Builders/CubeBuilder.h"
#include "BSPOps.h"
#include "Model.h"
#include "GameFramework/Volume.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "HandlerUtils.h"
#include "Families/MCPWorldSpawn.h"

namespace UEMCP
{
	void BuildVolumeAsCube(UWorld* World, AVolume* Volume, const FVector& HalfExtent)
	{
		if (!World || !Volume) return;

		Volume->PreEditChange(nullptr);

		const EObjectFlags ObjectFlags = Volume->GetFlags() & (RF_Transient | RF_Transactional);
		if (!Volume->Brush)
		{
			Volume->PolyFlags = 0;
			Volume->Brush = NewObject<UModel>(Volume, NAME_None, ObjectFlags);
			Volume->Brush->Initialize(nullptr, true);
		}
		if (!Volume->Brush->Polys)
		{
			Volume->Brush->Polys = NewObject<UPolys>(Volume->Brush, NAME_None, ObjectFlags);
		}
		if (UBrushComponent* BC = Volume->GetBrushComponent())
		{
			BC->Brush = Volume->Brush;
		}

		// The builder is saved with the volume, so it must live inside it:
		// a transient-package builder is lost on save and reload.
		UCubeBuilder* CubeBuilder = Cast<UCubeBuilder>(Volume->BrushBuilder);
		if (!CubeBuilder || CubeBuilder->GetOuter() != Volume)
		{
			CubeBuilder = NewObject<UCubeBuilder>(Volume, NAME_None, ObjectFlags);
		}
		CubeBuilder->X = HalfExtent.X * 2.0;
		CubeBuilder->Y = HalfExtent.Y * 2.0;
		CubeBuilder->Z = HalfExtent.Z * 2.0;
		Volume->BrushBuilder = CubeBuilder;
		CubeBuilder->Build(World, Volume);

		Volume->SetActorScale3D(FVector::OneVector);

		FBSPOps::csgPrepMovingBrush(Volume);

		Volume->PostEditChange();
		NotifyVolumeBoundsChanged(Volume);
	}

	void NotifyVolumeBoundsChanged(AVolume* Volume)
	{
		ANavMeshBoundsVolume* NavVolume = Cast<ANavMeshBoundsVolume>(Volume);
		if (!NavVolume) return;
		if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavVolume->GetWorld()))
		{
			NavSys->OnNavigationBoundsUpdated(NavVolume);
		}
	}

	TSharedPtr<FJsonValue> SpawnVolumeActor(
		const TSharedPtr<FJsonObject>& Params,
		UClass* VolumeClass,
		const FVector& DefaultExtent,
		const TCHAR* TypeLabel,
		TFunctionRef<TSharedPtr<FJsonValue>(AActor*)> Configure)
	{
		FMCPSpawn Spawn(Params, { TypeLabel });
		const FVector Extent = OptionalVec3(Params, TEXT("extent"), DefaultExtent);
		const FString GraphPath = OptionalString(Params, TEXT("graphPath"));
		if (Extent.X <= 0.0 || Extent.Y <= 0.0 || Extent.Z <= 0.0)
		{
			return MCPError(TEXT("extent must be positive on every axis (it is a half-size, not a corner)."));
		}
		UPCGGraph* Graph = nullptr;
		if (!GraphPath.IsEmpty())
		{
			Graph = LoadAssetByPath<UPCGGraph>(GraphPath);
			if (!Graph) return MCPAssetLoadError(GraphPath, TEXT("PCGGraph"));
		}

		if (auto Early = Spawn.Open()) return Early;
		AActor* NewActor = Spawn.SpawnAt<AActor>(VolumeClass, FTransform(Spawn.RequestedRotation(), Spawn.RequestedLocation()));
		if (!NewActor) return Spawn.Failed();

		// #238: a spawned AVolume has no brush and so no bounds; build the cube.
		// Its builder resets scale, so the caller's scale goes on afterwards.
		if (AVolume* Volume = Cast<AVolume>(NewActor))
		{
			BuildVolumeAsCube(Spawn.World(), Volume, Extent);
			Spawn.ApplyScale();
			NotifyVolumeBoundsChanged(Volume);
		}
		else
		{
			NewActor->SetActorScale3D(Extent / 100.0);
		}

		if (Graph)
		{
			UPCGComponent* PCGComp = NewActor->FindComponentByClass<UPCGComponent>();
			if (!PCGComp)
			{
				return Spawn.Abandon(MCPError(FString::Printf(
					TEXT("graphPath was given, but a %s has no PCG component to bind it to."), *NewActor->GetClass()->GetName())));
			}
			PCGComp->SetGraph(Graph);
		}
		if (auto Err = Configure(NewActor)) return Spawn.Abandon(Err);

		TSharedPtr<FJsonObject> Result = Spawn.Finish();
		// The built extent, read back rather than echoed: a zero here is the failure #238 fixed.
		FVector BoundsOrigin, BoundsExtent;
		NewActor->GetActorBounds(/*bOnlyCollidingComponents=*/false, BoundsOrigin, BoundsExtent);
		Result->SetObjectField(TEXT("extent"), MCPVec3ToJsonObject(BoundsExtent));
		if (Graph)
		{
			Result->SetStringField(TEXT("graphPath"), Graph->GetPathName());
			Result->SetStringField(TEXT("graphName"), Graph->GetName());
		}
		return MCPResult(Result);
	}
}
