// Split from AssetHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FAssetHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in AssetHandlers.cpp::RegisterHandlers.

#include "AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
// FSkeletalMaterial moved out of Engine/SkeletalMesh.h in later UE versions.
// Pull it explicitly via SkinnedAssetCommon when available.
#if __has_include("Engine/SkinnedAssetCommon.h")
#include "Engine/SkinnedAssetCommon.h"
#endif
#include "HandlerJsonProperty.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Animation/Skeleton.h"
#include "StaticMeshResources.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "AI/Navigation/NavCollisionBase.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

TSharedPtr<FJsonValue> FAssetHandlers::SetMeshMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString MaterialPath;
	if (auto Err = RequireString(Params, TEXT("materialPath"), MaterialPath)) return Err;

	int32 SlotIndex = OptionalInt(Params, TEXT("slotIndex"), 0);

	UStaticMesh* Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *AssetPath));
	if (!Mesh)
	{
		return MCPError(FString::Printf(TEXT("Failed to load static mesh at '%s'"), *AssetPath));
	}

	UMaterialInterface* Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MaterialPath));
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	if (SlotIndex < 0 || SlotIndex >= Mesh->GetStaticMaterials().Num())
	{
		return MCPError(FString::Printf(TEXT("Slot index %d out of range (mesh has %d slots)"), SlotIndex, Mesh->GetStaticMaterials().Num()));
	}

	// Capture previous material for self-inverse rollback.
	FString PreviousMaterialPath;
	if (UMaterialInterface* Prev = Mesh->GetMaterial(SlotIndex))
	{
		PreviousMaterialPath = Prev->GetPathName();
	}

	Mesh->SetMaterial(SlotIndex, Material);
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("materialPath"), MaterialPath);
	Result->SetNumberField(TEXT("slotIndex"), SlotIndex);
	Result->SetStringField(TEXT("previousMaterialPath"), PreviousMaterialPath);

	if (!PreviousMaterialPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("materialPath"), PreviousMaterialPath);
		Payload->SetNumberField(TEXT("slotIndex"), SlotIndex);
		MCPSetRollback(Result, TEXT("set_mesh_material"), Payload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::RecenterPivot(const TSharedPtr<FJsonObject>& Params)
{
	// Support single assetPath or array of assetPaths
	TArray<FString> AssetPaths;
	const TArray<TSharedPtr<FJsonValue>>* PathsArray = nullptr;
	FString SinglePath;

	if (Params->TryGetArrayField(TEXT("assetPaths"), PathsArray))
	{
		for (const auto& Val : *PathsArray)
		{
			FString P;
			if (Val->TryGetString(P) && !P.IsEmpty())
			{
				AssetPaths.Add(P);
			}
		}
	}
	else if (Params->TryGetStringField(TEXT("assetPath"), SinglePath) || Params->TryGetStringField(TEXT("path"), SinglePath))
	{
		if (!SinglePath.IsEmpty())
		{
			AssetPaths.Add(SinglePath);
		}
	}

	if (AssetPaths.Num() == 0)
	{
		return MCPError(TEXT("Missing 'assetPath' (string) or 'assetPaths' (array of strings)"));
	}

	// Load all meshes
	TArray<UStaticMesh*> Meshes;
	for (const FString& Path : AssetPaths)
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *Path));
		if (!Mesh)
		{
			return MCPError(FString::Printf(TEXT("Failed to load static mesh at '%s'"), *Path));
		}
		Meshes.Add(Mesh);
	}

	// Compute the center from the FIRST mesh (reference mesh)
	FMeshDescription* RefDesc = Meshes[0]->GetMeshDescription(0);
	if (!RefDesc)
	{
		return MCPError(TEXT("Failed to get mesh description for reference mesh LOD 0"));
	}

	FVertexArray& RefVerts = RefDesc->Vertices();
	TVertexAttributesRef<FVector3f> RefPositions = RefDesc->GetVertexPositions();

	FVector3f Center = FVector3f::ZeroVector;
	int32 RefVertCount = RefVerts.Num();
	if (RefVertCount == 0)
	{
		return MCPError(TEXT("Reference mesh has no vertices"));
	}

	for (FVertexID VertID : RefVerts.GetElementIDs())
	{
		Center += RefPositions[VertID];
	}
	Center /= (float)RefVertCount;

	// Apply the SAME offset to ALL meshes
	TArray<TSharedPtr<FJsonValue>> ResultArray;
	for (int32 i = 0; i < Meshes.Num(); i++)
	{
		FMeshDescription* MeshDesc = Meshes[i]->GetMeshDescription(0);
		if (!MeshDesc) continue;

		FVertexArray& Verts = MeshDesc->Vertices();
		TVertexAttributesRef<FVector3f> Positions = MeshDesc->GetVertexPositions();

		for (FVertexID VertID : Verts.GetElementIDs())
		{
			Positions[VertID] -= Center;
		}

		Meshes[i]->CommitMeshDescription(0);
		Meshes[i]->Build(false);
		Meshes[i]->PostEditChange();
		Meshes[i]->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(AssetPaths[i], false);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), AssetPaths[i]);
		Entry->SetNumberField(TEXT("vertexCount"), Verts.Num());
		ResultArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetArrayField(TEXT("meshes"), ResultArray);
	Result->SetStringField(TEXT("offsetApplied"), FString::Printf(TEXT("(%.2f, %.2f, %.2f)"), Center.X, Center.Y, Center.Z));
	Result->SetNumberField(TEXT("meshCount"), Meshes.Num());
	// No rollback: destructive/external — vertex offsets applied non-idempotently;
	// re-running shifts the pivot again. Not natural-key idempotent.

	return MCPResult(Result);
}


// ─── #155 asset(set_sk_material_slots) ──────────────────────────────
// Blueprint component property writes to SkeletalMeshComponent.OverrideMaterials
// are silently reverted by UE's ICH pipeline; the reliable path is to mutate
// USkeletalMesh.Materials directly. Accepts either slotName or slotIndex per
// entry. Missing slot names are reported, not skipped silently.
TSharedPtr<FJsonValue> FAssetHandlers::SetSkeletalMeshMaterialSlots(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* SlotsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("slots"), SlotsArr))
	{
		return MCPError(TEXT("Missing 'slots' array parameter"));
	}

	USkeletalMesh* Mesh = Cast<USkeletalMesh>(StaticLoadObject(USkeletalMesh::StaticClass(), nullptr, *AssetPath));
	if (!Mesh) return MCPError(FString::Printf(TEXT("SkeletalMesh not found: %s"), *AssetPath));

	Mesh->Modify();
	TArray<FSkeletalMaterial> Materials = Mesh->GetMaterials();

	TArray<TSharedPtr<FJsonValue>> Applied;
	TArray<FString> Errors;

	for (const TSharedPtr<FJsonValue>& SlotVal : *SlotsArr)
	{
		const TSharedPtr<FJsonObject>* SlotObjPtr = nullptr;
		if (!SlotVal.IsValid() || !SlotVal->TryGetObject(SlotObjPtr)) continue;
		const TSharedPtr<FJsonObject>& Slot = *SlotObjPtr;

		FString MaterialPath;
		if (!Slot->TryGetStringField(TEXT("materialPath"), MaterialPath))
		{
			Errors.Add(TEXT("slot entry missing 'materialPath'"));
			continue;
		}

		UMaterialInterface* Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MaterialPath));
		if (!Material)
		{
			Errors.Add(FString::Printf(TEXT("material not found: %s"), *MaterialPath));
			continue;
		}

		int32 Index = INDEX_NONE;
		double SlotIdxNum = 0;
		if (Slot->TryGetNumberField(TEXT("slotIndex"), SlotIdxNum))
		{
			Index = (int32)SlotIdxNum;
		}
		else
		{
			FString SlotName;
			if (Slot->TryGetStringField(TEXT("slotName"), SlotName))
			{
				const FName Target(*SlotName);
				for (int32 I = 0; I < Materials.Num(); ++I)
				{
					if (Materials[I].MaterialSlotName == Target)
					{
						Index = I; break;
					}
				}
				if (Index == INDEX_NONE)
				{
					Errors.Add(FString::Printf(TEXT("slotName '%s' not found on %s"), *SlotName, *AssetPath));
					continue;
				}
			}
		}

		if (Index < 0 || Index >= Materials.Num())
		{
			Errors.Add(FString::Printf(TEXT("slotIndex %d out of range (mesh has %d slots)"), Index, Materials.Num()));
			continue;
		}

		Materials[Index].MaterialInterface = Material;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("slotIndex"), Index);
		Entry->SetStringField(TEXT("slotName"), Materials[Index].MaterialSlotName.ToString());
		Entry->SetStringField(TEXT("materialPath"), MaterialPath);
		Applied.Add(MakeShared<FJsonValueObject>(Entry));
	}

	Mesh->SetMaterials(Materials);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Mesh, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("slotCount"), Materials.Num());
	Result->SetArrayField(TEXT("applied"), Applied);
	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrArr;
		for (const FString& E : Errors) ErrArr.Add(MakeShared<FJsonValueString>(E));
		Result->SetArrayField(TEXT("errors"), ErrArr);
	}
	return MCPResult(Result);
}

// ─── #155 asset(diagnose_registry) ──────────────────────────────────
// Explains the gap between disk state and the in-memory AssetRegistry.
// Returns on-disk vs registry-including-memory counts so callers can
// recognise pending-kill ghost entries after delete(). reconcile=true
// forces a synchronous rescan (matches the Python workaround).


// ---------------------------------------------------------------------------
// v1.0.0-rc.3 — #193 get_mesh_bounds
// ---------------------------------------------------------------------------
// #431: one-call asset QA - bounds + material slots + skeleton + LOD/vertex
// counts in one shot. Works for both UStaticMesh and USkeletalMesh.
TSharedPtr<FJsonValue> FAssetHandlers::GetMeshInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UStaticMesh* AsStaticMesh = LoadAssetByPath<UStaticMesh>(AssetPath);
	USkeletalMesh* AsSkeletalMesh = AsStaticMesh ? nullptr : LoadAssetByPath<USkeletalMesh>(AssetPath);
	if (!AsStaticMesh && !AsSkeletalMesh)
	{
		return MCPError(FString::Printf(TEXT("Mesh not found at '%s' (tried StaticMesh and SkeletalMesh)"), *AssetPath));
	}

	FBox BoundingBox(ForceInit);
	FString MeshKind;
	int32 LodCount = 0;
	int32 VertexCount = 0;
	FString SkeletonPath;
	TArray<TSharedPtr<FJsonValue>> SlotsJson;

	if (AsStaticMesh)
	{
		MeshKind = TEXT("StaticMesh");
		BoundingBox = AsStaticMesh->GetBoundingBox();
		LodCount = AsStaticMesh->GetNumLODs();
		if (LodCount > 0 && AsStaticMesh->GetRenderData() && AsStaticMesh->GetRenderData()->LODResources.Num() > 0)
		{
			VertexCount = AsStaticMesh->GetRenderData()->LODResources[0].GetNumVertices();
		}
		const TArray<FStaticMaterial>& Mats = AsStaticMesh->GetStaticMaterials();
		for (int32 i = 0; i < Mats.Num(); ++i)
		{
			const FStaticMaterial& M = Mats[i];
			TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
			SlotObj->SetNumberField(TEXT("index"), i);
			SlotObj->SetStringField(TEXT("slotName"), M.MaterialSlotName.ToString());
			SlotObj->SetStringField(TEXT("materialPath"), M.MaterialInterface ? M.MaterialInterface->GetPathName() : FString());
			SlotObj->SetBoolField(TEXT("isDefaultFallback"), M.MaterialInterface == nullptr);
			SlotsJson.Add(MakeShared<FJsonValueObject>(SlotObj));
		}
	}
	else
	{
		MeshKind = TEXT("SkeletalMesh");
		const FBoxSphereBounds Bounds = AsSkeletalMesh->GetBounds();
		BoundingBox = FBox(Bounds.Origin - Bounds.BoxExtent, Bounds.Origin + Bounds.BoxExtent);
		if (USkeleton* Skel = AsSkeletalMesh->GetSkeleton()) SkeletonPath = Skel->GetPathName();
		if (const FSkeletalMeshRenderData* RD = AsSkeletalMesh->GetResourceForRendering())
		{
			LodCount = RD->LODRenderData.Num();
			if (LodCount > 0) VertexCount = RD->LODRenderData[0].GetNumVertices();
		}
		const TArray<FSkeletalMaterial>& Mats = AsSkeletalMesh->GetMaterials();
		for (int32 i = 0; i < Mats.Num(); ++i)
		{
			const FSkeletalMaterial& M = Mats[i];
			TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
			SlotObj->SetNumberField(TEXT("index"), i);
			SlotObj->SetStringField(TEXT("slotName"), M.MaterialSlotName.ToString());
			SlotObj->SetStringField(TEXT("materialPath"), M.MaterialInterface ? M.MaterialInterface->GetPathName() : FString());
			SlotObj->SetBoolField(TEXT("isDefaultFallback"), M.MaterialInterface == nullptr);
			SlotsJson.Add(MakeShared<FJsonValueObject>(SlotObj));
		}
	}

	const FVector Extent = BoundingBox.GetExtent();
	const FVector Origin = BoundingBox.GetCenter();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("meshKind"), MeshKind);
	Result->SetObjectField(TEXT("boundsOrigin"), MCPVec3ToJsonObject(Origin));
	Result->SetObjectField(TEXT("boundsExtent"), MCPVec3ToJsonObject(Extent));
	Result->SetNumberField(TEXT("heightM"), (Extent.Z * 2.0) / 100.0);
	Result->SetNumberField(TEXT("lodCount"), LodCount);
	Result->SetNumberField(TEXT("vertexCount"), VertexCount);
	if (!SkeletonPath.IsEmpty()) Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Result->SetArrayField(TEXT("materialSlots"), SlotsJson);
	Result->SetNumberField(TEXT("materialCount"), SlotsJson.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::GetMeshBounds(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	// #351: accept SkeletalMesh too — get_mesh_bounds previously errored
	// on SkeletalMesh assets and callers had to fall back to Python
	// (load_asset + get_bounds). Probe StaticMesh first, then SkeletalMesh.
	FBox BoundingBox(ForceInit);
	FString MeshKind;
	if (UStaticMesh* AsStaticMesh = LoadAssetByPath<UStaticMesh>(AssetPath))
	{
		BoundingBox = AsStaticMesh->GetBoundingBox();
		MeshKind = TEXT("StaticMesh");
	}
	else if (USkeletalMesh* AsSkeletalMesh = LoadAssetByPath<USkeletalMesh>(AssetPath))
	{
		const FBoxSphereBounds Bounds = AsSkeletalMesh->GetBounds();
		BoundingBox = FBox(Bounds.Origin - Bounds.BoxExtent, Bounds.Origin + Bounds.BoxExtent);
		MeshKind = TEXT("SkeletalMesh");
	}
	else
	{
		return MCPError(FString::Printf(
			TEXT("Mesh not found at '%s' (tried StaticMesh and SkeletalMesh)"), *AssetPath));
	}

	FVector Min = BoundingBox.Min;
	FVector Max = BoundingBox.Max;
	FVector Extent = BoundingBox.GetExtent();
	FVector Center = BoundingBox.GetCenter();

	TSharedPtr<FJsonObject> MinObj = MakeShared<FJsonObject>();
	MinObj->SetNumberField(TEXT("x"), Min.X);
	MinObj->SetNumberField(TEXT("y"), Min.Y);
	MinObj->SetNumberField(TEXT("z"), Min.Z);

	TSharedPtr<FJsonObject> MaxObj = MakeShared<FJsonObject>();
	MaxObj->SetNumberField(TEXT("x"), Max.X);
	MaxObj->SetNumberField(TEXT("y"), Max.Y);
	MaxObj->SetNumberField(TEXT("z"), Max.Z);

	TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
	ExtentObj->SetNumberField(TEXT("x"), Extent.X);
	ExtentObj->SetNumberField(TEXT("y"), Extent.Y);
	ExtentObj->SetNumberField(TEXT("z"), Extent.Z);

	TSharedPtr<FJsonObject> CenterObj = MakeShared<FJsonObject>();
	CenterObj->SetNumberField(TEXT("x"), Center.X);
	CenterObj->SetNumberField(TEXT("y"), Center.Y);
	CenterObj->SetNumberField(TEXT("z"), Center.Z);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("meshKind"), MeshKind);
	Result->SetObjectField(TEXT("min"), MinObj);
	Result->SetObjectField(TEXT("max"), MaxObj);
	Result->SetObjectField(TEXT("boxExtent"), ExtentObj);
	Result->SetObjectField(TEXT("boxCenter"), CenterObj);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// #270: surface AssetImportData->SourceData filenames on imported assets so
// callers can validate legacy imports without dropping to Python. Works for
// any UObject that owns an AssetImportData (StaticMesh, SkeletalMesh, Texture,
// Animation*, etc.) - resolved via reflection on the asset class.
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// #270: surface AssetImportData->SourceData filenames on imported assets so
// callers can validate legacy imports without dropping to Python. Works for
// any UObject that owns an AssetImportData (StaticMesh, SkeletalMesh, Texture,
// Animation*, etc.) - resolved via reflection on the asset class.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::ReadImportSources(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* Asset = LoadAssetByPath<UObject>(AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	UAssetImportData* ImportData = nullptr;
	if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
	{
		ImportData = SM->GetAssetImportData();
	}
	else if (USkeletalMesh* SKM = Cast<USkeletalMesh>(Asset))
	{
		ImportData = SKM->GetAssetImportData();
	}
	else
	{
		// Most other importable assets expose an `AssetImportData` UPROPERTY.
		if (FObjectProperty* Prop = CastField<FObjectProperty>(Asset->GetClass()->FindPropertyByName(TEXT("AssetImportData"))))
		{
			ImportData = Cast<UAssetImportData>(Prop->GetObjectPropertyValue_InContainer(Asset));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
	Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());

	if (!ImportData)
	{
		Result->SetBoolField(TEXT("hasImportData"), false);
		TArray<TSharedPtr<FJsonValue>> Empty;
		Result->SetArrayField(TEXT("sources"), Empty);
		return MCPResult(Result);
	}

	Result->SetBoolField(TEXT("hasImportData"), true);
	TArray<TSharedPtr<FJsonValue>> Sources;
	for (const FAssetImportInfo::FSourceFile& SF : ImportData->SourceData.SourceFiles)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("relativeFilename"), SF.RelativeFilename);
		Entry->SetStringField(TEXT("timestamp"), SF.Timestamp.ToString());
		Entry->SetStringField(TEXT("fileHash"), LexToString(SF.FileHash));
		Entry->SetStringField(TEXT("displayLabelName"), SF.DisplayLabelName);
		// Resolve absolute path: SourceFilenames returns the resolved paths in
		// the same order as SourceData.SourceFiles. The internal Resolve method
		// is protected, so we lift the public ExtractFilenames helper instead.
		Sources.Add(MakeShared<FJsonValueObject>(Entry));
	}
	TArray<FString> AbsoluteFilenames;
	ImportData->ExtractFilenames(AbsoluteFilenames);
	for (int32 i = 0; i < Sources.Num() && i < AbsoluteFilenames.Num(); ++i)
	{
		Sources[i]->AsObject()->SetStringField(TEXT("absolutePath"), AbsoluteFilenames[i]);
	}
	Result->SetArrayField(TEXT("sources"), Sources);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// v1.0.0-rc.3 — #177 get_mesh_collision
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// v1.0.0-rc.3 — #177 get_mesh_collision
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::GetMeshCollision(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	REQUIRE_ASSET(UStaticMesh, Mesh, AssetPath);

	UBodySetup* BodySetup = Mesh->GetBodySetup();
	if (!BodySetup)
	{
		return MCPError(FString::Printf(TEXT("No BodySetup found on mesh: %s"), *AssetPath));
	}

	// Collision trace flag as string
	FString TraceFlag;
	switch (BodySetup->CollisionTraceFlag)
	{
	case CTF_UseDefault:             TraceFlag = TEXT("CTF_UseDefault"); break;
	case CTF_UseSimpleAndComplex:    TraceFlag = TEXT("CTF_UseSimpleAndComplex"); break;
	case CTF_UseSimpleAsComplex:     TraceFlag = TEXT("CTF_UseSimpleAsComplex"); break;
	case CTF_UseComplexAsSimple:     TraceFlag = TEXT("CTF_UseComplexAsSimple"); break;
	default:                         TraceFlag = TEXT("Unknown"); break;
	}

	const FKAggregateGeom& AggGeom = BodySetup->AggGeom;

	int32 NumConvex  = AggGeom.ConvexElems.Num();
	int32 NumBox     = AggGeom.BoxElems.Num();
	int32 NumSphere  = AggGeom.SphereElems.Num();
	int32 NumSphyl   = AggGeom.SphylElems.Num();

	bool bHasSimple = (NumConvex + NumBox + NumSphere + NumSphyl) > 0;

	// Complex collision is available when the trace flag allows it
	bool bHasComplex = (BodySetup->CollisionTraceFlag == CTF_UseDefault
		|| BodySetup->CollisionTraceFlag == CTF_UseSimpleAndComplex
		|| BodySetup->CollisionTraceFlag == CTF_UseComplexAsSimple);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("collisionTraceFlag"), TraceFlag);
	Result->SetBoolField(TEXT("hasSimpleCollision"), bHasSimple);
	Result->SetBoolField(TEXT("hasComplexCollision"), bHasComplex);
	Result->SetNumberField(TEXT("numConvexElems"), NumConvex);
	Result->SetNumberField(TEXT("numBoxElems"), NumBox);
	Result->SetNumberField(TEXT("numSphereElems"), NumSphere);
	Result->SetNumberField(TEXT("numSphylElems"), NumSphyl);

	// NavCollision info (#167)
	Result->SetBoolField(TEXT("bCanEverAffectNavigation"), Mesh->bHasNavigationData);
	if (Mesh->GetNavCollision())
	{
		Result->SetBoolField(TEXT("hasNavCollision"), true);
		Result->SetBoolField(TEXT("bIsDynamicObstacle"), Mesh->GetNavCollision()->IsDynamicObstacle());
	}
	else
	{
		Result->SetBoolField(TEXT("hasNavCollision"), false);
	}

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// v1.0.0-rc.5 — #167 set_mesh_nav
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// v1.0.0-rc.5 — #167 set_mesh_nav
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::SetMeshNav(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	REQUIRE_ASSET(UStaticMesh, Mesh, AssetPath);

	bool bChanged = false;

	bool bHasNavData = false;
	if (Params->TryGetBoolField(TEXT("bHasNavigationData"), bHasNavData))
	{
		Mesh->bHasNavigationData = bHasNavData;
		bChanged = true;
	}

	bool bClearNavCollision = false;
	if (Params->TryGetBoolField(TEXT("clearNavCollision"), bClearNavCollision) && bClearNavCollision)
	{
		Mesh->SetNavCollision(nullptr);
		bChanged = true;
	}

	if (!bChanged)
	{
		return MCPError(TEXT("No changes requested. Provide bHasNavigationData and/or clearNavCollision."));
	}

	Mesh->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetBoolField(TEXT("bHasNavigationData"), Mesh->bHasNavigationData);
	Result->SetBoolField(TEXT("hasNavCollision"), Mesh->GetNavCollision() != nullptr);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// v1.0.0-rc.3 — #192 move_folder
// ---------------------------------------------------------------------------
