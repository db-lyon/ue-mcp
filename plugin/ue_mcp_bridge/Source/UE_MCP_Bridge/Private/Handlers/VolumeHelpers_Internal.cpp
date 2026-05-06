#include "VolumeHelpers_Internal.h"

#include "Engine/World.h"
#include "Engine/Brush.h"
#include "Engine/Polys.h"
#include "Components/BrushComponent.h"
#include "Builders/CubeBuilder.h"
#include "BSPOps.h"
#include "Model.h"
#include "GameFramework/Volume.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

namespace UEMCP
{
	void BuildVolumeAsCube(UWorld* World, AVolume* Volume, const FVector& HalfExtent)
	{
		if (!World || !Volume) return;

		Volume->PreEditChange(nullptr);

		if (!Volume->Brush)
		{
			const EObjectFlags ObjectFlags = Volume->GetFlags() & (RF_Transient | RF_Transactional);
			Volume->PolyFlags = 0;
			Volume->Brush = NewObject<UModel>(Volume, NAME_None, ObjectFlags);
			Volume->Brush->Initialize(nullptr, true);
			Volume->Brush->Polys = NewObject<UPolys>(Volume->Brush, NAME_None, ObjectFlags);
			if (UBrushComponent* BC = Volume->GetBrushComponent())
			{
				BC->Brush = Volume->Brush;
			}
		}

		UCubeBuilder* CubeBuilder = NewObject<UCubeBuilder>(GetTransientPackage(), UCubeBuilder::StaticClass());
		CubeBuilder->X = HalfExtent.X * 2.0;
		CubeBuilder->Y = HalfExtent.Y * 2.0;
		CubeBuilder->Z = HalfExtent.Z * 2.0;
		CubeBuilder->Build(World, Volume);

		Volume->BrushBuilder = CubeBuilder;
		Volume->SetActorScale3D(FVector::OneVector);

		FBSPOps::csgPrepMovingBrush(Volume);
	}
}
