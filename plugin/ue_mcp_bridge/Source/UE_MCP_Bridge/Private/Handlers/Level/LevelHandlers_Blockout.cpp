// Level blockout.
//
// A blockout pass lays a level out in simple shapes, then swaps each shape for
// a real asset. The bridge could place existing meshes (spawn_grid,
// place_actors_batch) and convert BSP brushes, but it had no shapes of its own
// beyond the engine's basic ones, no way to find a blockout again, and no swap.
// Epic's add_cube adds a box component and needs that engine's toolset;
// Geometry Script needs its plugin. So the primitives here are built through
// FMeshDescription, which is engine core and the same on 5.4 to 5.9.
//
// Every primitive mesh is a 100 cm unit shape with its pivot at the bottom
// centre, cached as an asset under meshFolder (one per shape, and per step or
// side count), and a blockout actor is a StaticMeshActor scaled to the
// requested size and tagged with the blockout tag and "BlockoutShape:<shape>".
//
// A replacement carries the tag "BlockoutReplacement" and a record of the
// blockout it replaced ("BlockoutRecord=<json>", LevelGeneratorRecords.h), and
// restore_blockout puts the blockout back from that record.

#include "LevelHandlers_Blockout.h"
#include "LevelGeneratorRecords.h"
#include "LevelSpawnFinalize.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Algo/Reverse.h"

namespace
{
	const TCHAR* const MCPBlkShapeTagPrefix = TEXT("BlockoutShape:");
	const TCHAR* const MCPBlkRecordPrefix = TEXT("BlockoutRecord=");
	const TCHAR* const MCPBlkReplacementTag = TEXT("BlockoutReplacement");
	const TCHAR* const MCPBlkDefaultFolder = TEXT("/Game/Blockout/Meshes");
	const TCHAR* const MCPBlkGridMaterial = TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial");

	bool MCPBlkIsShape(const FString& Shape)
	{
		return Shape == TEXT("box") || Shape == TEXT("ramp") || Shape == TEXT("wedge") || Shape == TEXT("cylinder") || Shape == TEXT("stairs");
	}

	/** Appends one flat face to a mesh description, ordering its corners so
	 *  the engine's winding faces Outward, with planar UVs in metres. */
	struct FMCPBlkMeshBuilder
	{
		FMeshDescription& Desc;
		FStaticMeshAttributes& Attributes;
		FPolygonGroupID Group;

		void AddFace(TArray<FVector3f> Corners, const FVector3f& Outward)
		{
			if (Corners.Num() < 3) return;
			// The engine's triangle normal is (C - A) x (B - A).
			const FVector3f Normal = FVector3f::CrossProduct(Corners[2] - Corners[0], Corners[1] - Corners[0]);
			if (FVector3f::DotProduct(Normal, Outward) < 0.0f)
			{
				Algo::Reverse(Corners);
			}
			const FVector3f N = Outward.GetSafeNormal();
			const FVector3f Abs(FMath::Abs(N.X), FMath::Abs(N.Y), FMath::Abs(N.Z));
			FVector3f Tangent = Abs.Z >= Abs.X && Abs.Z >= Abs.Y ? FVector3f(1, 0, 0) : (Abs.X >= Abs.Y ? FVector3f(0, 1, 0) : FVector3f(1, 0, 0));
			Tangent = (Tangent - N * FVector3f::DotProduct(Tangent, N)).GetSafeNormal();
			TArray<FVertexInstanceID> Instances;
			for (const FVector3f& Corner : Corners)
			{
				const FVertexID Vertex = Desc.CreateVertex();
				Attributes.GetVertexPositions()[Vertex] = Corner;
				const FVertexInstanceID Instance = Desc.CreateVertexInstance(Vertex);
				Attributes.GetVertexInstanceNormals()[Instance] = N;
				Attributes.GetVertexInstanceTangents()[Instance] = Tangent;
				Attributes.GetVertexInstanceBinormalSigns()[Instance] = 1.0f;
				FVector2f UV;
				if (Abs.Z >= Abs.X && Abs.Z >= Abs.Y) UV = FVector2f(Corner.X, Corner.Y);
				else if (Abs.X >= Abs.Y) UV = FVector2f(Corner.Y, Corner.Z);
				else UV = FVector2f(Corner.X, Corner.Z);
				Attributes.GetVertexInstanceUVs().Set(Instance, 0, UV / 100.0f);
				Instances.Add(Instance);
			}
			Desc.CreatePolygon(Group, Instances);
		}

		void AddQuad(const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D, const FVector3f& Outward)
		{
			AddFace({ A, B, C, D }, Outward);
		}

		/** An axis-aligned box from Min to Max, all six faces. */
		void AddBox(const FVector3f& Min, const FVector3f& Max)
		{
			const float X0 = Min.X, Y0 = Min.Y, Z0 = Min.Z, X1 = Max.X, Y1 = Max.Y, Z1 = Max.Z;
			AddQuad({X0, Y0, Z0}, {X1, Y0, Z0}, {X1, Y1, Z0}, {X0, Y1, Z0}, {0, 0, -1});
			AddQuad({X0, Y0, Z1}, {X1, Y0, Z1}, {X1, Y1, Z1}, {X0, Y1, Z1}, {0, 0, 1});
			AddQuad({X0, Y0, Z0}, {X0, Y1, Z0}, {X0, Y1, Z1}, {X0, Y0, Z1}, {-1, 0, 0});
			AddQuad({X1, Y0, Z0}, {X1, Y1, Z0}, {X1, Y1, Z1}, {X1, Y0, Z1}, {1, 0, 0});
			AddQuad({X0, Y0, Z0}, {X1, Y0, Z0}, {X1, Y0, Z1}, {X0, Y0, Z1}, {0, -1, 0});
			AddQuad({X0, Y1, Z0}, {X1, Y1, Z0}, {X1, Y1, Z1}, {X0, Y1, Z1}, {0, 1, 0});
		}
	};

	/** Fill Desc with the unit shape: 100 cm on every axis, X and Y centred,
	 *  Z from 0 to 100. A ramp rises toward +X; a wedge is a gable with its
	 *  ridge along Y; stairs climb toward +X. */
	void MCPBlkBuildShape(FMCPBlkMeshBuilder& B, const FString& Shape, int32 Steps, int32 Sides)
	{
		const float H = 50.0f, T = 100.0f;
		if (Shape == TEXT("box"))
		{
			B.AddBox({-H, -H, 0}, {H, H, T});
		}
		else if (Shape == TEXT("ramp"))
		{
			B.AddQuad({-H, -H, 0}, {H, -H, 0}, {H, H, 0}, {-H, H, 0}, {0, 0, -1});
			B.AddQuad({H, -H, 0}, {H, H, 0}, {H, H, T}, {H, -H, T}, {1, 0, 0});
			B.AddQuad({-H, -H, 0}, {-H, H, 0}, {H, H, T}, {H, -H, T}, {-1, 0, 1});
			B.AddFace({ {-H, -H, 0}, {H, -H, 0}, {H, -H, T} }, {0, -1, 0});
			B.AddFace({ {-H, H, 0}, {H, H, 0}, {H, H, T} }, {0, 1, 0});
		}
		else if (Shape == TEXT("wedge"))
		{
			B.AddQuad({-H, -H, 0}, {H, -H, 0}, {H, H, 0}, {-H, H, 0}, {0, 0, -1});
			B.AddQuad({-H, -H, 0}, {-H, H, 0}, {0, H, T}, {0, -H, T}, {-2, 0, 1});
			B.AddQuad({H, -H, 0}, {H, H, 0}, {0, H, T}, {0, -H, T}, {2, 0, 1});
			B.AddFace({ {-H, -H, 0}, {H, -H, 0}, {0, -H, T} }, {0, -1, 0});
			B.AddFace({ {-H, H, 0}, {H, H, 0}, {0, H, T} }, {0, 1, 0});
		}
		else if (Shape == TEXT("cylinder"))
		{
			TArray<FVector3f> Bottom, Top;
			for (int32 Index = 0; Index < Sides; ++Index)
			{
				const float Angle = 2.0f * PI * Index / Sides;
				Bottom.Add({ H * FMath::Cos(Angle), H * FMath::Sin(Angle), 0 });
				Top.Add({ H * FMath::Cos(Angle), H * FMath::Sin(Angle), T });
			}
			for (int32 Index = 0; Index < Sides; ++Index)
			{
				const int32 Next = (Index + 1) % Sides;
				const float Mid = 2.0f * PI * (Index + 0.5f) / Sides;
				B.AddQuad(Bottom[Index], Bottom[Next], Top[Next], Top[Index], { FMath::Cos(Mid), FMath::Sin(Mid), 0 });
			}
			B.AddFace(Bottom, {0, 0, -1});
			B.AddFace(Top, {0, 0, 1});
		}
		else // stairs
		{
			const float Run = T / Steps, Rise = T / Steps;
			B.AddQuad({-H, -H, 0}, {H, -H, 0}, {H, H, 0}, {-H, H, 0}, {0, 0, -1});
			B.AddQuad({H, -H, 0}, {H, H, 0}, {H, H, T}, {H, -H, T}, {1, 0, 0});
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				const float X0 = -H + Step * Run, X1 = X0 + Run, Z0 = Step * Rise, Z1 = Z0 + Rise;
				B.AddQuad({X0, -H, Z1}, {X1, -H, Z1}, {X1, H, Z1}, {X0, H, Z1}, {0, 0, 1});
				B.AddQuad({X0, -H, Z0}, {X0, H, Z0}, {X0, H, Z1}, {X0, -H, Z1}, {-1, 0, 0});
				B.AddQuad({X0, -H, 0}, {X1, -H, 0}, {X1, -H, Z1}, {X0, -H, Z1}, {0, -1, 0});
				B.AddQuad({X0, H, 0}, {X1, H, 0}, {X1, H, Z1}, {X0, H, Z1}, {0, 1, 0});
			}
		}
	}

	FString MCPBlkMeshName(const FString& Shape, int32 Steps, int32 Sides)
	{
		if (Shape == TEXT("stairs")) return FString::Printf(TEXT("SM_Blockout_Stairs_%d"), Steps);
		if (Shape == TEXT("cylinder")) return FString::Printf(TEXT("SM_Blockout_Cylinder_%d"), Sides);
		FString Title = Shape;
		Title[0] = FChar::ToUpper(Title[0]);
		return TEXT("SM_Blockout_") + Title;
	}

	/** The cached unit mesh for a shape, built and saved the first time. */
	UStaticMesh* MCPBlkGetShapeMesh(const FString& Shape, int32 Steps, int32 Sides, const FString& Folder, bool& bOutCreated, FString& OutError)
	{
		bOutCreated = false;
		const FString AssetName = MCPBlkMeshName(Shape, Steps, Sides);
		const FString PackageName = Folder / AssetName;
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;
		UStaticMesh* Existing = FindObject<UStaticMesh>(nullptr, *ObjectPath);
		if (!Existing && FPackageName::DoesPackageExist(PackageName))
		{
			Existing = LoadObject<UStaticMesh>(nullptr, *ObjectPath);
		}
		if (Existing) return Existing;
		if (MCPIsProtectedAssetPath(PackageName) || !FPackageName::IsValidLongPackageName(PackageName))
		{
			OutError = FString::Printf(TEXT("meshFolder '%s' is not a writable content folder"), *Folder);
			return nullptr;
		}
		UPackage* Package = CreatePackage(*PackageName);
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		Mesh->SetNumSourceModels(1);
		FStaticMeshSourceModel& Source = Mesh->GetSourceModel(0);
		Source.BuildSettings.bRecomputeNormals = false;
		Source.BuildSettings.bRecomputeTangents = true;
		Source.BuildSettings.bGenerateLightmapUVs = true;
		Source.BuildSettings.SrcLightmapIndex = 0;
		Source.BuildSettings.DstLightmapIndex = 1;
		Mesh->SetLightMapCoordinateIndex(1);
		Mesh->SetLightMapResolution(64);

		FMeshDescription* Desc = Mesh->CreateMeshDescription(0);
		FStaticMeshAttributes Attributes(*Desc);
		Attributes.Register();
		Attributes.GetVertexInstanceUVs().SetNumChannels(1);
		const FPolygonGroupID Group = Desc->CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Blockout");
		FMCPBlkMeshBuilder Builder{ *Desc, Attributes, Group };
		MCPBlkBuildShape(Builder, Shape, Steps, Sides);

		UMaterialInterface* Grid = LoadObject<UMaterialInterface>(nullptr, MCPBlkGridMaterial);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Grid, TEXT("Blockout"), TEXT("Blockout")));
		Mesh->CommitMeshDescription(0);
		Mesh->CreateBodySetup();
		if (UBodySetup* Body = Mesh->GetBodySetup())
		{
			Body->CollisionTraceFlag = CTF_UseComplexAsSimple;
		}
		Mesh->Build(true);
		Mesh->PostEditChange();
		Mesh->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Mesh);
		FString SaveReason;
		if (!SaveAssetPackageChecked(Mesh, SaveReason))
		{
			OutError = FString::Printf(TEXT("Built %s but could not save it: %s"), *PackageName, *SaveReason);
			return nullptr;
		}
		bOutCreated = true;
		return Mesh;
	}

	FString MCPBlkShapeOf(const AActor* Actor)
	{
		for (const FName& Tag : Actor->Tags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(MCPBlkShapeTagPrefix)) return Text.RightChop(FCString::Strlen(MCPBlkShapeTagPrefix));
		}
		return TEXT("custom");
	}

	UStaticMeshComponent* MCPBlkMeshComponent(const AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	}

	/** An actor's extent along its own axes, actor scale included, and the
	 *  world position of the bottom centre of that box. */
	bool MCPBlkLocalExtent(const AActor* Actor, FVector& OutSize, FVector& OutBottomCentre)
	{
		const FBox Local = Actor->CalculateComponentsBoundingBoxInLocalSpace(true);
		if (!Local.IsValid) return false;
		const FVector Scale = Actor->GetActorScale3D().GetAbs();
		OutSize = Local.GetSize() * Scale;
		const FVector Centre = Local.GetCenter();
		OutBottomCentre = Actor->GetActorTransform().TransformPosition(FVector(Centre.X, Centre.Y, Local.Min.Z));
		return true;
	}

	TSharedPtr<FJsonObject> MCPBlkDescribe(const AActor* Actor)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Row->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Row->SetStringField(TEXT("shape"), MCPBlkShapeOf(Actor));
		const UStaticMeshComponent* MeshComp = MCPBlkMeshComponent(Actor);
		Row->SetStringField(TEXT("mesh"), MeshComp && MeshComp->GetStaticMesh() ? MeshComp->GetStaticMesh()->GetPathName() : FString());
		Row->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Actor->GetActorLocation()));
		Row->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Actor->GetActorRotation()));
		Row->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Actor->GetActorScale3D()));
		FVector Size, Bottom;
		if (MCPBlkLocalExtent(Actor, Size, Bottom))
		{
			Row->SetObjectField(TEXT("size"), MCPVec3ToJsonObject(Size));
			Row->SetObjectField(TEXT("bottomCentre"), MCPVec3ToJsonObject(Bottom));
		}
		FVector Origin, Extent;
		Actor->GetActorBounds(false, Origin, Extent);
		TSharedPtr<FJsonObject> Bounds = MakeShared<FJsonObject>();
		Bounds->SetObjectField(TEXT("origin"), MCPVec3ToJsonObject(Origin));
		Bounds->SetObjectField(TEXT("extent"), MCPVec3ToJsonObject(Extent));
		Row->SetObjectField(TEXT("worldBounds"), Bounds);
		TArray<FString> Tags;
		for (const FName& Tag : Actor->Tags)
		{
			const FString Text = Tag.ToString();
			if (!Text.StartsWith(MCPBlkRecordPrefix)) Tags.Add(Text);
		}
		Row->SetArrayField(TEXT("tags"), MCPStringListToJson(Tags));
		return Row;
	}

	/** Actors carrying Tag, sorted by path, excluding replacements. */
	TArray<AActor*> MCPBlkCollectTagged(UWorld* World, FName Tag, bool bReplacements)
	{
		TArray<AActor*> Out;
		const FName Replacement(MCPBlkReplacementTag);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) continue;
			const bool bIsReplacement = Actor->ActorHasTag(Replacement);
			if (bReplacements ? bIsReplacement : (Actor->ActorHasTag(Tag) && !bIsReplacement)) Out.Add(Actor);
		}
		Out.Sort([](const AActor& A, const AActor& B) { return A.GetPathName() < B.GetPathName(); });
		return Out;
	}

	/**
	 * The blockout actors a call names: one by label or path, every one carrying
	 * tag, or a list of actorPaths. bTag and bPaths say which of those the
	 * calling action declares; one it does not declare is never read.
	 */
	TSharedPtr<FJsonValue> MCPBlkResolveTargets(UWorld* World, const TSharedPtr<FJsonObject>& Params, TArray<AActor*>& Out, bool bTag, bool bPaths)
	{
		const FString Tag = bTag ? OptionalString(Params, TEXT("tag")) : FString();
		if (!Tag.IsEmpty())
		{
			Out = MCPBlkCollectTagged(World, FName(*Tag), false);
			if (Out.Num() == 0) return MCPError(FString::Printf(TEXT("No blockout actors carry the tag '%s'"), *Tag));
			return nullptr;
		}
		const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
		if (bPaths && TryGetArrayParam(Params, TEXT("actorPaths"), Paths) && Paths)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Paths)
			{
				FString Path;
				if (!Value.IsValid() || !Value->TryGetString(Path)) continue;
				AActor* Actor = MCPFindActorByPath(World, Path);
				if (!Actor) return MCPError(FString::Printf(TEXT("No actor at '%s'"), *Path));
				Out.Add(Actor);
			}
			if (Out.Num() == 0) return MCPError(TEXT("actorPaths names no actors"));
			return nullptr;
		}
		TSharedPtr<FJsonValue> Err;
		AActor* Actor = MCPResolveActor(World, Params, Err);
		if (!Actor) return Err;
		Out.Add(Actor);
		return nullptr;
	}

	struct FMCPBlkCandidate
	{
		FString AssetPath;
		UStaticMesh* Mesh = nullptr;
		FVector Size = FVector::ZeroVector;
		double Score = 0.0, SizeScore = 0.0, ShapeScore = 0.0;
		double Yaw = 0.0;
		double UniformScale = 1.0;
		FVector Stretch = FVector::OneVector;
	};

	double MCPBlkLogError(const FVector& A, const FVector& B)
	{
		double Sum = 0.0;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Sum += FMath::Abs(FMath::Loge(FMath::Max(A[Axis], 1.0) / FMath::Max(B[Axis], 1.0)));
		}
		return Sum / 3.0;
	}

	/** Score Mesh against a blockout of size Target: size similarity, shape
	 *  (aspect) similarity, and the scale each fit would need. With rotation
	 *  allowed a quarter turn is tried as well and the better one is kept. */
	FMCPBlkCandidate MCPBlkScore(UStaticMesh* Mesh, const FVector& Target, bool bAllowRotation)
	{
		FMCPBlkCandidate Best;
		Best.Mesh = Mesh;
		Best.AssetPath = Mesh->GetPathName();
		Best.Size = Mesh->GetBoundingBox().GetSize();
		Best.Score = -1.0;
		for (int32 Turn = 0; Turn < (bAllowRotation ? 2 : 1); ++Turn)
		{
			const FVector Size = Turn == 0 ? Best.Size : FVector(Best.Size.Y, Best.Size.X, Best.Size.Z);
			const double SizeScore = FMath::Exp(-MCPBlkLogError(Size, Target));
			const double SizeMax = FMath::Max(Size.GetMax(), 1.0), TargetMax = FMath::Max(Target.GetMax(), 1.0);
			const double ShapeScore = FMath::Exp(-MCPBlkLogError(Size / SizeMax * 100.0, Target / TargetMax * 100.0));
			const double Score = 0.5 * SizeScore + 0.5 * ShapeScore;
			if (Score > Best.Score)
			{
				Best.Score = Score;
				Best.SizeScore = SizeScore;
				Best.ShapeScore = ShapeScore;
				Best.Yaw = Turn == 0 ? 0.0 : 90.0;
				double Uniform = TNumericLimits<double>::Max();
				FVector Stretch = FVector::OneVector;
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					if (Size[Axis] > 0.01)
					{
						Stretch[Axis] = Target[Axis] / Size[Axis];
						Uniform = FMath::Min(Uniform, Stretch[Axis]);
					}
				}
				Best.UniformScale = Uniform == TNumericLimits<double>::Max() ? 1.0 : Uniform;
				Best.Stretch = Stretch;
			}
		}
		return Best;
	}

	TSharedPtr<FJsonObject> MCPBlkDescribeCandidate(const FMCPBlkCandidate& C)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("assetPath"), C.AssetPath);
		Row->SetNumberField(TEXT("score"), C.Score);
		Row->SetNumberField(TEXT("sizeScore"), C.SizeScore);
		Row->SetNumberField(TEXT("shapeScore"), C.ShapeScore);
		Row->SetObjectField(TEXT("size"), MCPVec3ToJsonObject(C.Size));
		Row->SetNumberField(TEXT("yawOffset"), C.Yaw);
		Row->SetNumberField(TEXT("uniformScale"), C.UniformScale);
		Row->SetObjectField(TEXT("stretchScale"), MCPVec3ToJsonObject(C.Stretch));
		return Row;
	}

	/** Load every StaticMesh under Folder, up to Max. */
	TSharedPtr<FJsonValue> MCPBlkLoadCandidates(const FString& Folder, bool bRecursive, int32 Max, TArray<UStaticMesh*>& Out, bool& bOutTruncated)
	{
		bOutTruncated = false;
		if (!Folder.StartsWith(TEXT("/")))
		{
			return MCPError(FString::Printf(TEXT("candidateFolder must be a content path such as /Game/Props, not '%s'"), *Folder));
		}
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Folder));
		Filter.bRecursivePaths = bRecursive;
		Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.GetObjectPathString() < B.GetObjectPathString(); });
		for (const FAssetData& Asset : Assets)
		{
			if (Out.Num() >= Max) { bOutTruncated = true; break; }
			if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset())) Out.Add(Mesh);
		}
		if (Out.Num() == 0) return MCPError(FString::Printf(TEXT("No StaticMesh assets under '%s'"), *Folder));
		return nullptr;
	}

	/** Ranked candidates for one blockout, best first. */
	TArray<FMCPBlkCandidate> MCPBlkRank(const TArray<UStaticMesh*>& Candidates, const FVector& Target, bool bAllowRotation)
	{
		TArray<FMCPBlkCandidate> Ranked;
		for (UStaticMesh* Mesh : Candidates) Ranked.Add(MCPBlkScore(Mesh, Target, bAllowRotation));
		Ranked.Sort([](const FMCPBlkCandidate& A, const FMCPBlkCandidate& B)
		{
			return A.Score != B.Score ? A.Score > B.Score : A.AssetPath < B.AssetPath;
		});
		return Ranked;
	}

	/** What restore_blockout needs to put this actor back. */
	TSharedPtr<FJsonObject> MCPBlkRecordOf(const AActor* Actor, const FString& Fit, double Yaw)
	{
		TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
		Record->SetStringField(TEXT("label"), Actor->GetActorLabel());
		const UStaticMeshComponent* MeshComp = MCPBlkMeshComponent(Actor);
		Record->SetStringField(TEXT("mesh"), MeshComp && MeshComp->GetStaticMesh() ? MeshComp->GetStaticMesh()->GetPathName() : FString());
		// The slot-0 material when it is not the mesh's own, so restore puts the override back.
		const UStaticMesh* RecordedMesh = MeshComp ? MeshComp->GetStaticMesh() : nullptr;
		const UMaterialInterface* Shown = MeshComp ? MeshComp->GetMaterial(0) : nullptr;
		if (Shown && RecordedMesh && Shown != RecordedMesh->GetMaterial(0)) Record->SetStringField(TEXT("material"), Shown->GetPathName());
		Record->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Actor->GetActorLocation()));
		Record->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Actor->GetActorRotation()));
		Record->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Actor->GetActorScale3D()));
		Record->SetStringField(TEXT("folder"), MCPActorFolderPath(Actor));
		TArray<FString> Tags;
		for (const FName& Tag : Actor->Tags) Tags.Add(Tag.ToString());
		Record->SetArrayField(TEXT("tags"), MCPStringListToJson(Tags));
		Record->SetStringField(TEXT("fit"), Fit);
		Record->SetNumberField(TEXT("yawOffset"), Yaw);
		return Record;
	}

	FVector MCPBlkVec(const TSharedPtr<FJsonObject>& Record, const TCHAR* Key, const FVector& Default)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		FVector Out = Default;
		if (Record.IsValid() && Record->TryGetObjectField(Key, Obj) && Obj) ReadVec3Fields(*Obj, Out);
		return Out;
	}

	/** A label not yet used by any actor: Base, else Base_1, Base_2 and so on. */
	FString MCPBlkFreeLabel(UWorld* World, const FString& Base)
	{
		TArray<AActor*> Matches;
		MCPCollectActorsByToken(World, Base, EMCPActorMatch::Label, Matches);
		if (Matches.Num() == 0) return Base;
		for (int32 Suffix = 1; Suffix < 100000; ++Suffix)
		{
			const FString Candidate = FString::Printf(TEXT("%s_%d"), *Base, Suffix);
			Matches.Reset();
			MCPCollectActorsByToken(World, Candidate, EMCPActorMatch::Label, Matches);
			if (Matches.Num() == 0) return Candidate;
		}
		return Base;
	}

	AStaticMeshActor* MCPBlkSpawnMeshActor(UWorld* World, ULevel* Level, UStaticMesh* Mesh, const FTransform& Transform)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.OverrideLevel = Level;
		SpawnParams.ObjectFlags |= RF_Transactional;
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, SpawnParams);
		if (!Actor) return nullptr;
		Actor->SetActorScale3D(Transform.GetScale3D());
		Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		return Actor;
	}
}

void FLevelBlockoutHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("level"));
	using EType = EMCPParamType;

	const FMCPParamSpec SpecActorLabel = MCPParam::Optional(TEXT("actorLabel"), EType::String, TEXT("Blockout actor label"));
	const FMCPParamSpec SpecActorPath = MCPParam::Optional(TEXT("actorPath"), EType::String, TEXT("Blockout actor path; the unambiguous selector"));
	const FMCPParamSpec SpecTargetTag = MCPParam::Optional(TEXT("tag"), EType::String, TEXT("Act on every blockout actor carrying this tag"));
	const FMCPParamSpec SpecFolder = MCPParam::Optional(TEXT("candidateFolder"), EType::String, TEXT("Content folder of candidate StaticMeshes, e.g. /Game/Props"));
	const FMCPParamSpec SpecRecursive = MCPParam::Optional(TEXT("recursive"), EType::Boolean, TEXT("Include subfolders of candidateFolder (default true)"));
	const FMCPParamSpec SpecAllowRotation = MCPParam::Optional(TEXT("allowRotation"), EType::Boolean, TEXT("Also try each candidate turned a quarter about Z (default true)"));
	const FMCPParamSpec SpecMaxCandidates = MCPParam::Optional(TEXT("maxCandidates"), EType::Integer, TEXT("Load at most this many candidate meshes (default 500)"));

	Registry.RegisterHandler(TEXT("create_blockout"), &CreateBlockout, {
		MCPParam::Required(TEXT("shape"), EType::String, TEXT("box | ramp | wedge | cylinder | stairs")),
		MCPParam::Optional(TEXT("location"), EType::Vec3, TEXT("World position of the bottom centre (default origin)")),
		MCPParam::Optional(TEXT("rotation"), EType::Rotator, TEXT("World rotation; a ramp and stairs climb toward the actor's +X")),
		MCPParam::Optional(TEXT("size"), EType::Vec3, TEXT("Size in centimetres along the actor's X, Y and Z (default 100, 100, 100)")),
		MCPParam::Optional(TEXT("steps"), EType::Integer, TEXT("Stairs: number of steps, 1 to 64 (default 8)")),
		MCPParam::Optional(TEXT("sides"), EType::Integer, TEXT("Cylinder: number of sides, 3 to 64 (default 16)")),
		MCPParam::Optional(TEXT("label"), EType::String, TEXT("Actor label; an existing actor with this label is reported rather than duplicated (default a free Blockout_<Shape>)")),
		MCPParam::Optional(TEXT("tag"), EType::String, TEXT("Blockout tag the actor carries (default Blockout)")),
		MCPParam::Optional(TEXT("material"), EType::String, TEXT("Material override (default the grid material the mesh carries)")),
		MCPParam::Optional(TEXT("meshFolder"), EType::String, TEXT("Content folder for the generated shape meshes (default /Game/Blockout/Meshes)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("When label is taken: skip (default) | error")),
	});
	Registry.RegisterHandler(TEXT("list_blockout"), &ListBlockout, {
		MCPParam::Optional(TEXT("tag"), EType::String, TEXT("Blockout tag to list (default Blockout)")),
		MCPParam::Optional(TEXT("includeReplaced"), EType::Boolean, TEXT("Also list replacements that carry a restorable blockout record (default false)")),
	});
	Registry.RegisterHandler(TEXT("match_asset_to_blockout"), &MatchAssetToBlockout, {
		SpecActorLabel, SpecActorPath, SpecTargetTag,
		MCPParam::Required(TEXT("candidateFolder"), EType::String, TEXT("Content folder of candidate StaticMeshes, e.g. /Game/Props")),
		SpecRecursive, SpecAllowRotation, SpecMaxCandidates,
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Candidates returned per blockout (default 5)")),
	}, MCPSpec::ExactlyOne({ { TEXT("actorLabel") }, { TEXT("actorPath") }, { TEXT("tag") } }));
	Registry.RegisterHandler(TEXT("replace_blockout"), &ReplaceBlockout, {
		SpecActorLabel, SpecActorPath, SpecTargetTag,
		MCPParam::Optional(TEXT("actorPaths"), EType::Array, TEXT("Blockout actor paths")).Items(EType::String),
		MCPParam::Optional(TEXT("assetPath"), EType::String, TEXT("StaticMesh to put in place of every target; wins over candidateFolder")),
		SpecFolder, SpecRecursive, SpecAllowRotation, SpecMaxCandidates,
		MCPParam::Optional(TEXT("fit"), EType::String, TEXT("uniform (default, largest uniform scale that fits inside) | stretch (match every axis) | none (asset at scale 1)")),
		MCPParam::Optional(TEXT("yawOffset"), EType::Number, TEXT("Extra yaw in degrees on top of the blockout's rotation (default the best match's, else 0)")),
	}, MCPSpec::ExactlyOne({ { TEXT("actorLabel") }, { TEXT("actorPath") }, { TEXT("tag") }, { TEXT("actorPaths") } }));
	Registry.RegisterHandler(TEXT("restore_blockout"), &RestoreBlockout, {
		MCPParam::Optional(TEXT("actorLabel"), EType::String, TEXT("Replacement actor label")),
		MCPParam::Optional(TEXT("actorPath"), EType::String, TEXT("Replacement actor path")),
		MCPParam::Optional(TEXT("actorPaths"), EType::Array, TEXT("Replacement actor paths, as replace_blockout returns them")).Items(EType::String),
	}, MCPSpec::ExactlyOne({ { TEXT("actorLabel") }, { TEXT("actorPath") }, { TEXT("actorPaths") } }));
}

// level(create_blockout): a tagged primitive actor of a given size.
TSharedPtr<FJsonValue> FLevelBlockoutHandlers::CreateBlockout(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("shape"), TEXT("location"), TEXT("rotation"), TEXT("size"), TEXT("steps"), TEXT("sides"), TEXT("label"),
		TEXT("tag"), TEXT("material"), TEXT("meshFolder"), TEXT("onConflict"),
	});
	REQUIRE_EDITOR_WORLD(World);
	FString Shape;
	if (auto Err = RequireString(Params, TEXT("shape"), Shape)) return Err;
	Shape = Shape.ToLower();
	if (!MCPBlkIsShape(Shape)) return MCPError(FString::Printf(TEXT("shape must be box, ramp, wedge, cylinder or stairs, not '%s'"), *Shape));
	const int32 Steps = OptionalInt(Params, TEXT("steps"), 8);
	const int32 Sides = OptionalInt(Params, TEXT("sides"), 16);
	if (Shape == TEXT("stairs") && (Steps < 1 || Steps > 64)) return MCPError(TEXT("steps must be 1 to 64"));
	if (Shape == TEXT("cylinder") && (Sides < 3 || Sides > 64)) return MCPError(TEXT("sides must be 3 to 64"));
	const FVector Size = OptionalVec3(Params, TEXT("size"), FVector(100.0));
	if (Size.X <= 0.0 || Size.Y <= 0.0 || Size.Z <= 0.0 || Size.ContainsNaN()) return MCPError(TEXT("size must be above zero on every axis"));
	const FVector Location = OptionalVec3(Params, TEXT("location"), FVector::ZeroVector);
	const FRotator Rotation = OptionalRotator(Params, TEXT("rotation"), FRotator::ZeroRotator);
	const FString Tag = OptionalString(Params, TEXT("tag"), TEXT("Blockout"));
	const FString MeshFolder = OptionalString(Params, TEXT("meshFolder"), MCPBlkDefaultFolder);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	if (OnConflict != TEXT("skip") && OnConflict != TEXT("error")) return MCPError(TEXT("onConflict must be skip or error"));
	FString Label = OptionalString(Params, TEXT("label"));
	if (Tag.IsEmpty()) return MCPError(TEXT("tag cannot be empty"));

	UMaterialInterface* Material = nullptr;
	const FString MaterialPath = OptionalString(Params, TEXT("material"));
	if (!MaterialPath.IsEmpty())
	{
		Material = Cast<UMaterialInterface>(MCPLoadAssetObject(MaterialPath));
		if (!Material) return MCPError(FString::Printf(TEXT("No material at '%s'"), *MaterialPath));
	}
	if (!Label.IsEmpty())
	{
		if (auto Existing = MCPCheckActorLabelExists(World, Label, OnConflict, TEXT("Blockout"))) return Existing;
	}

	bool bMeshCreated = false;
	FString MeshError;
	UStaticMesh* Mesh = MCPBlkGetShapeMesh(Shape, Steps, Sides, MeshFolder, bMeshCreated, MeshError);
	if (!Mesh) return MCPError(MeshError);

	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "CreateBlockout", "Create Blockout"));
	AStaticMeshActor* Actor = MCPBlkSpawnMeshActor(World, World->GetCurrentLevel(), Mesh, FTransform(Rotation, Location, Size / 100.0));
	if (!Actor) return MCPError(TEXT("SpawnActor returned no actor"));
	if (Material) Actor->GetStaticMeshComponent()->SetMaterial(0, Material);
	if (Label.IsEmpty()) Label = MCPBlkFreeLabel(World, TEXT("Blockout_") + MCPBlkMeshName(Shape, Steps, Sides).RightChop(12));
	Actor->SetActorLabel(Label);
	Actor->Tags.AddUnique(FName(*Tag));
	Actor->Tags.AddUnique(FName(*(FString(MCPBlkShapeTagPrefix) + Shape)));
	MCPSpawnFinalize::FinishEditorSpawn(Actor);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetObjectField(TEXT("blockout"), MCPBlkDescribe(Actor));
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("mesh"), Mesh->GetPathName());
	Result->SetBoolField(TEXT("meshCreated"), bMeshCreated);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);
	if (bMeshCreated)
	{
		Result->SetStringField(TEXT("note"), TEXT("The shape mesh was generated and saved for reuse; the rollback deletes the actor and keeps the mesh."));
	}
	return MCPResult(Result);
}

// level(list_blockout): the blockout actors in the level.
TSharedPtr<FJsonValue> FLevelBlockoutHandlers::ListBlockout(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("tag"), TEXT("includeReplaced") });
	REQUIRE_EDITOR_WORLD(World);
	const FString Tag = OptionalString(Params, TEXT("tag"), TEXT("Blockout"));
	const bool bReplaced = OptionalBool(Params, TEXT("includeReplaced"), false);

	TArray<TSharedPtr<FJsonValue>> Rows;
	TMap<FString, int32> ByShape;
	for (AActor* Actor : MCPBlkCollectTagged(World, FName(*Tag), false))
	{
		Rows.Add(MakeShared<FJsonValueObject>(MCPBlkDescribe(Actor)));
		ByShape.FindOrAdd(MCPBlkShapeOf(Actor))++;
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("tag"), Tag);
	Result->SetNumberField(TEXT("count"), Rows.Num());
	Result->SetArrayField(TEXT("blockouts"), Rows);
	TSharedPtr<FJsonObject> Shapes = MakeShared<FJsonObject>();
	for (const TPair<FString, int32>& Pair : ByShape) Shapes->SetNumberField(Pair.Key, Pair.Value);
	Result->SetObjectField(TEXT("byShape"), Shapes);
	if (bReplaced)
	{
		TArray<TSharedPtr<FJsonValue>> Replaced;
		for (AActor* Actor : MCPBlkCollectTagged(World, NAME_None, true))
		{
			const TSharedPtr<FJsonObject> Record = MCPLevelRecords::Read(Actor, MCPBlkRecordPrefix);
			TSharedPtr<FJsonObject> Row = MCPBlkDescribe(Actor);
			if (Record.IsValid()) Row->SetObjectField(TEXT("record"), Record);
			Row->SetBoolField(TEXT("restorable"), Record.IsValid());
			Replaced.Add(MakeShared<FJsonValueObject>(Row));
		}
		Result->SetNumberField(TEXT("replacedCount"), Replaced.Num());
		Result->SetArrayField(TEXT("replaced"), Replaced);
	}
	MCPNoteLoadedOnlyEnumeration(World, Result);
	return MCPResult(Result);
}

// level(match_asset_to_blockout): rank candidate meshes by how well their bounds fit a blockout.
TSharedPtr<FJsonValue> FLevelBlockoutHandlers::MatchAssetToBlockout(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("tag"), TEXT("candidateFolder"), TEXT("recursive"), TEXT("allowRotation"),
		TEXT("maxCandidates"), TEXT("limit"),
	});
	REQUIRE_EDITOR_WORLD(World);
	TArray<AActor*> Targets;
	if (auto Err = MCPBlkResolveTargets(World, Params, Targets, true, false)) return Err;
	FString Folder;
	if (auto Err = RequireString(Params, TEXT("candidateFolder"), Folder)) return Err;
	const bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);
	const bool bAllowRotation = OptionalBool(Params, TEXT("allowRotation"), true);
	const int32 MaxCandidates = FMath::Max(1, OptionalInt(Params, TEXT("maxCandidates"), 500));
	const int32 Limit = FMath::Max(1, OptionalInt(Params, TEXT("limit"), 5));
	TArray<UStaticMesh*> Candidates;
	bool bTruncated = false;
	if (auto Err = MCPBlkLoadCandidates(Folder, bRecursive, MaxCandidates, Candidates, bTruncated)) return Err;

	TArray<TSharedPtr<FJsonValue>> Rows;
	for (AActor* Target : Targets)
	{
		TSharedPtr<FJsonObject> Row = MCPBlkDescribe(Target);
		FVector Size, Bottom;
		if (!MCPBlkLocalExtent(Target, Size, Bottom))
		{
			Row->SetStringField(TEXT("error"), TEXT("The actor has no registered primitive components to measure"));
			Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}
		const TArray<FMCPBlkCandidate> Ranked = MCPBlkRank(Candidates, Size, bAllowRotation);
		TArray<TSharedPtr<FJsonValue>> Top;
		for (int32 Index = 0; Index < Ranked.Num() && Index < Limit; ++Index)
		{
			Top.Add(MakeShared<FJsonValueObject>(MCPBlkDescribeCandidate(Ranked[Index])));
		}
		Row->SetArrayField(TEXT("candidates"), Top);
		if (Ranked.Num() > 0) Row->SetStringField(TEXT("bestMatch"), Ranked[0].AssetPath);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("candidateFolder"), Folder);
	Result->SetNumberField(TEXT("candidateCount"), Candidates.Num());
	Result->SetBoolField(TEXT("candidatesTruncated"), bTruncated);
	Result->SetNumberField(TEXT("count"), Rows.Num());
	Result->SetArrayField(TEXT("matches"), Rows);
	return MCPResult(Result);
}

// level(replace_blockout): swap blockout actors for assets fitted to their bounds.
TSharedPtr<FJsonValue> FLevelBlockoutHandlers::ReplaceBlockout(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("tag"), TEXT("actorPaths"), TEXT("assetPath"), TEXT("candidateFolder"),
		TEXT("recursive"), TEXT("allowRotation"), TEXT("maxCandidates"), TEXT("fit"), TEXT("yawOffset"),
	});
	REQUIRE_EDITOR_WORLD(World);
	TArray<AActor*> Targets;
	if (auto Err = MCPBlkResolveTargets(World, Params, Targets, true, true)) return Err;
	const FString Fit = OptionalString(Params, TEXT("fit"), TEXT("uniform"));
	if (Fit != TEXT("uniform") && Fit != TEXT("stretch") && Fit != TEXT("none")) return MCPError(TEXT("fit must be uniform, stretch or none"));
	const FString AssetPath = OptionalString(Params, TEXT("assetPath"));
	const FString Folder = OptionalString(Params, TEXT("candidateFolder"));
	double YawParam = 0.0;
	const bool bYawGiven = TryGetNumberParam(Params, TEXT("yawOffset"), YawParam);

	UStaticMesh* Explicit = nullptr;
	TArray<UStaticMesh*> Candidates;
	if (!AssetPath.IsEmpty())
	{
		Explicit = Cast<UStaticMesh>(MCPLoadAssetObject(AssetPath));
		if (!Explicit) return MCPError(FString::Printf(TEXT("No StaticMesh at '%s'"), *AssetPath));
	}
	else if (!Folder.IsEmpty())
	{
		bool bTruncated = false;
		if (auto Err = MCPBlkLoadCandidates(Folder, OptionalBool(Params, TEXT("recursive"), true),
			FMath::Max(1, OptionalInt(Params, TEXT("maxCandidates"), 500)), Candidates, bTruncated)) return Err;
	}
	else
	{
		return MCPError(TEXT("Pass assetPath, or candidateFolder to pick the best match for each blockout"));
	}
	const bool bAllowRotation = OptionalBool(Params, TEXT("allowRotation"), true);

	// Plan every swap, and check its record fits, before anything is destroyed.
	struct FPlan { AActor* Blockout; UStaticMesh* Mesh; FTransform Transform; TSharedPtr<FJsonObject> Record; double Yaw; double Score; };
	TArray<FPlan> Plans;
	TArray<TSharedPtr<FJsonValue>> Skipped;
	for (AActor* Blockout : Targets)
	{
		if (Blockout->ActorHasTag(FName(MCPBlkReplacementTag)))
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("actorPath"), Blockout->GetPathName());
			Row->SetStringField(TEXT("reason"), TEXT("already a replacement"));
			Skipped.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}
		FVector Size, Bottom;
		if (!MCPBlkLocalExtent(Blockout, Size, Bottom))
		{
			return MCPError(FString::Printf(TEXT("'%s' has no registered primitive components to measure"), *Blockout->GetActorLabel()));
		}
		FMCPBlkCandidate Choice;
		if (Explicit) Choice = MCPBlkScore(Explicit, Size, false);
		else
		{
			const TArray<FMCPBlkCandidate> Ranked = MCPBlkRank(Candidates, Size, bAllowRotation);
			Choice = Ranked[0];
		}
		const double Yaw = bYawGiven ? YawParam : Choice.Yaw;
		// Size in the asset's own axes once turned by Yaw: a quarter turn swaps X and Y.
		const bool bSwap = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Yaw))) > 0.7071;
		const FVector Target = bSwap ? FVector(Size.Y, Size.X, Size.Z) : Size;
		const FBox AssetBox = Choice.Mesh->GetBoundingBox();
		const FVector AssetSize = AssetBox.GetSize();
		FVector Scale = FVector::OneVector;
		if (Fit == TEXT("stretch"))
		{
			for (int32 Axis = 0; Axis < 3; ++Axis) Scale[Axis] = AssetSize[Axis] > 0.01 ? Target[Axis] / AssetSize[Axis] : 1.0;
		}
		else if (Fit == TEXT("uniform"))
		{
			double Uniform = TNumericLimits<double>::Max();
			for (int32 Axis = 0; Axis < 3; ++Axis) if (AssetSize[Axis] > 0.01) Uniform = FMath::Min(Uniform, Target[Axis] / AssetSize[Axis]);
			Scale = FVector(Uniform == TNumericLimits<double>::Max() ? 1.0 : Uniform);
		}
		const FQuat Rotation = Blockout->GetActorQuat() * FRotator(0.0, Yaw, 0.0).Quaternion();
		const FVector AssetBottom(AssetBox.GetCenter().X, AssetBox.GetCenter().Y, AssetBox.Min.Z);
		const FVector Location = Bottom - Rotation.RotateVector(Scale * AssetBottom);
		TSharedPtr<FJsonObject> Record = MCPBlkRecordOf(Blockout, Fit, Yaw);
		const FString RecordText = FString(MCPBlkRecordPrefix) + MCPLevelRecords::ToCompactJson(Record);
		if (RecordText.Len() > MCPLevelRecords::MaxTagLength)
		{
			// Keep the swap restorable: drop the extra tags before refusing.
			Record->SetArrayField(TEXT("tags"), TArray<TSharedPtr<FJsonValue>>());
			if ((FString(MCPBlkRecordPrefix) + MCPLevelRecords::ToCompactJson(Record)).Len() > MCPLevelRecords::MaxTagLength)
			{
				return MCPError(FString::Printf(TEXT("'%s': its restore record is too long for an actor tag, so it is left in place"), *Blockout->GetActorLabel()));
			}
		}
		Plans.Add({ Blockout, Choice.Mesh, FTransform(Rotation, Location, Scale), Record, Yaw, Choice.Score });
	}

	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "ReplaceBlockout", "Replace Blockout"));
	TArray<TSharedPtr<FJsonValue>> Rows;
	TArray<FString> ReplacementPaths;
	for (const FPlan& Plan : Plans)
	{
		const FString Label = Plan.Blockout->GetActorLabel();
		const FName OutlinerFolder = Plan.Blockout->GetFolderPath();
		AStaticMeshActor* Replacement = MCPBlkSpawnMeshActor(World, Plan.Blockout->GetLevel(), Plan.Mesh, Plan.Transform);
		if (!Replacement) continue;
		FString RecordError;
		Replacement->Tags.Add(FName(MCPBlkReplacementTag));
		if (!MCPLevelRecords::Write(Replacement, MCPBlkRecordPrefix, Plan.Record, RecordError))
		{
			World->DestroyActor(Replacement);
			Transaction.Cancel();
			return MCPError(FString::Printf(TEXT("'%s': %s"), *Label, *RecordError));
		}
		const FString BlockoutPath = Plan.Blockout->GetPathName();
		World->DestroyActor(Plan.Blockout);
		Replacement->SetActorLabel(Label);
		Replacement->SetFolderPath(OutlinerFolder);
		MCPSpawnFinalize::FinishEditorSpawn(Replacement);
		ReplacementPaths.Add(Replacement->GetPathName());

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("blockoutPath"), BlockoutPath);
		Row->SetStringField(TEXT("actorLabel"), Replacement->GetActorLabel());
		Row->SetStringField(TEXT("actorPath"), Replacement->GetPathName());
		Row->SetStringField(TEXT("assetPath"), Plan.Mesh->GetPathName());
		Row->SetNumberField(TEXT("score"), Plan.Score);
		Row->SetNumberField(TEXT("yawOffset"), Plan.Yaw);
		Row->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Plan.Transform.GetLocation()));
		Row->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Plan.Transform.Rotator()));
		Row->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Plan.Transform.GetScale3D()));
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("fit"), Fit);
	Result->SetNumberField(TEXT("replacedCount"), Rows.Num());
	Result->SetArrayField(TEXT("replaced"), Rows);
	Result->SetArrayField(TEXT("skipped"), Skipped);
	Result->SetArrayField(TEXT("actorPaths"), MCPStringListToJson(ReplacementPaths));
	Result->SetBoolField(TEXT("updated"), Rows.Num() > 0);
	Result->SetBoolField(TEXT("unchanged"), Rows.Num() == 0);
	if (ReplacementPaths.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("actorPaths"), MCPStringListToJson(ReplacementPaths));
		MCPSetRollback(Result, TEXT("restore_blockout"), Payload);
	}
	return MCPResult(Result);
}

// level(restore_blockout): put back the blockout a replacement's record describes.
TSharedPtr<FJsonValue> FLevelBlockoutHandlers::RestoreBlockout(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("actorLabel"), TEXT("actorPath"), TEXT("actorPaths") });
	REQUIRE_EDITOR_WORLD(World);
	TArray<AActor*> Targets;
	if (auto Err = MCPBlkResolveTargets(World, Params, Targets, false, true)) return Err;

	struct FPlan { AActor* Replacement; UStaticMesh* Mesh; TSharedPtr<FJsonObject> Record; };
	TArray<FPlan> Plans;
	for (AActor* Replacement : Targets)
	{
		const TSharedPtr<FJsonObject> Record = MCPLevelRecords::Read(Replacement, MCPBlkRecordPrefix);
		if (!Record.IsValid())
		{
			return MCPError(FString::Printf(TEXT("'%s' carries no blockout record; only actors replace_blockout made can be restored"), *Replacement->GetActorLabel()));
		}
		FString MeshPath;
		Record->TryGetStringField(TEXT("mesh"), MeshPath);
		UStaticMesh* Mesh = Cast<UStaticMesh>(MCPLoadAssetObject(MeshPath));
		if (!Mesh) return MCPError(FString::Printf(TEXT("'%s': the blockout mesh '%s' no longer loads"), *Replacement->GetActorLabel(), *MeshPath));
		Plans.Add({ Replacement, Mesh, Record });
	}

	// One replace_blockout call can undo this only when every swap used the same asset, fit and yaw.
	FString CommonAsset, CommonFit;
	double CommonYaw = 0.0;
	bool bUniform = true;
	for (int32 Index = 0; Index < Plans.Num(); ++Index)
	{
		const UStaticMeshComponent* MeshComp = MCPBlkMeshComponent(Plans[Index].Replacement);
		const FString Asset = MeshComp && MeshComp->GetStaticMesh() ? MeshComp->GetStaticMesh()->GetPathName() : FString();
		FString Fit;
		double Yaw = 0.0;
		Plans[Index].Record->TryGetStringField(TEXT("fit"), Fit);
		Plans[Index].Record->TryGetNumberField(TEXT("yawOffset"), Yaw);
		if (Index == 0) { CommonAsset = Asset; CommonFit = Fit; CommonYaw = Yaw; }
		else if (Asset != CommonAsset || Fit != CommonFit || !FMath::IsNearlyEqual(Yaw, CommonYaw)) bUniform = false;
	}

	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "RestoreBlockout", "Restore Blockout"));
	TArray<TSharedPtr<FJsonValue>> Rows;
	TArray<FString> RestoredPaths;
	for (const FPlan& Plan : Plans)
	{
		const FVector Location = MCPBlkVec(Plan.Record, TEXT("location"), Plan.Replacement->GetActorLocation());
		const FVector Scale = MCPBlkVec(Plan.Record, TEXT("scale"), FVector::OneVector);
		FRotator Rotation = FRotator::ZeroRotator;
		const TSharedPtr<FJsonObject>* RotationObj = nullptr;
		if (Plan.Record->TryGetObjectField(TEXT("rotation"), RotationObj) && RotationObj) ReadRotatorFields(*RotationObj, Rotation);
		AStaticMeshActor* Restored = MCPBlkSpawnMeshActor(World, Plan.Replacement->GetLevel(), Plan.Mesh, FTransform(Rotation, Location, Scale));
		if (!Restored) continue;
		FString MaterialPath;
		if (Plan.Record->TryGetStringField(TEXT("material"), MaterialPath) && !MaterialPath.IsEmpty())
		{
			if (UMaterialInterface* Material = Cast<UMaterialInterface>(MCPLoadAssetObject(MaterialPath)))
			{
				Restored->GetStaticMeshComponent()->SetMaterial(0, Material);
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
		if (Plan.Record->TryGetArrayField(TEXT("tags"), Tags) && Tags)
		{
			for (const TSharedPtr<FJsonValue>& Tag : *Tags)
			{
				FString Text;
				if (Tag.IsValid() && Tag->TryGetString(Text) && !Text.IsEmpty()) Restored->Tags.AddUnique(FName(*Text));
			}
		}
		FString Label, Folder;
		Plan.Record->TryGetStringField(TEXT("label"), Label);
		Plan.Record->TryGetStringField(TEXT("folder"), Folder);
		const FString ReplacementPath = Plan.Replacement->GetPathName();
		World->DestroyActor(Plan.Replacement);
		if (!Label.IsEmpty()) Restored->SetActorLabel(Label);
		if (!Folder.IsEmpty()) Restored->SetFolderPath(FName(*Folder));
		MCPSpawnFinalize::FinishEditorSpawn(Restored);
		RestoredPaths.Add(Restored->GetPathName());

		TSharedPtr<FJsonObject> Row = MCPBlkDescribe(Restored);
		Row->SetStringField(TEXT("replacementPath"), ReplacementPath);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("restoredCount"), Rows.Num());
	Result->SetArrayField(TEXT("restored"), Rows);
	Result->SetArrayField(TEXT("actorPaths"), MCPStringListToJson(RestoredPaths));
	Result->SetBoolField(TEXT("updated"), Rows.Num() > 0);
	Result->SetBoolField(TEXT("unchanged"), Rows.Num() == 0);
	if (bUniform && RestoredPaths.Num() > 0 && !CommonAsset.IsEmpty())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("actorPaths"), MCPStringListToJson(RestoredPaths));
		Payload->SetStringField(TEXT("assetPath"), CommonAsset);
		Payload->SetStringField(TEXT("fit"), CommonFit.IsEmpty() ? TEXT("uniform") : CommonFit);
		Payload->SetNumberField(TEXT("yawOffset"), CommonYaw);
		MCPSetRollback(Result, TEXT("replace_blockout"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("The restored blockouts were swapped for different assets, fits or yaws, which one replace_blockout call cannot repeat; replace them again one at a time."));
	}
	return MCPResult(Result);
}
