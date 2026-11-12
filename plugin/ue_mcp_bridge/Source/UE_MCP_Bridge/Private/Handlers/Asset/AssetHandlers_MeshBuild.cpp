#include "AssetHandlers_MeshBuild.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPackageSave.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/EngineTypes.h"
#include "StaticMeshResources.h"
#include "StaticMeshOperations.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "UObject/UnrealType.h"

namespace
{
	/** The editable build-settings fields: bools, numbers and FVector, which is every field FMeshBuildSettings has. */
	bool MCPIsBuildSettingField(const FProperty* Property)
	{
		if (CastField<FBoolProperty>(Property) || CastField<FNumericProperty>(Property)) return true;
		const FStructProperty* Struct = CastField<FStructProperty>(Property);
		return Struct && Struct->Struct == TBaseStructure<FVector>::Get();
	}

	TSharedPtr<FJsonValue> MCPBuildSettingToJson(const FProperty* Property, const void* Container)
	{
		const void* Value = Property->ContainerPtrToValuePtr<void>(Container);
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			return MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(Value));
		}
		if (const FNumericProperty* Number = CastField<FNumericProperty>(Property))
		{
			return MakeShared<FJsonValueNumber>(Number->IsFloatingPoint()
				? Number->GetFloatingPointPropertyValue(Value)
				: static_cast<double>(Number->GetSignedIntPropertyValue(Value)));
		}
		const FVector& Vector = *static_cast<const FVector*>(Value);
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("x"), Vector.X);
		Object->SetNumberField(TEXT("y"), Vector.Y);
		Object->SetNumberField(TEXT("z"), Vector.Z);
		return MakeShared<FJsonValueObject>(Object);
	}

	/** Writes one JSON value into a field. Returns false with a reason when the value does not fit the field. */
	bool MCPJsonToBuildSetting(const FProperty* Property, void* Container, const TSharedPtr<FJsonValue>& Json, FString& OutReason)
	{
		void* Value = Property->ContainerPtrToValuePtr<void>(Container);
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			// Strict types: TryGetBool/TryGetNumber coerce across JSON types, so 3 would pass as true.
			if (!Json.IsValid() || Json->Type != EJson::Boolean) { OutReason = TEXT("expects a boolean"); return false; }
			Bool->SetPropertyValue(Value, Json->AsBool());
			return true;
		}
		if (const FNumericProperty* Number = CastField<FNumericProperty>(Property))
		{
			if (!Json.IsValid() || Json->Type != EJson::Number) { OutReason = TEXT("expects a number"); return false; }
			const double D = Json->AsNumber();
			if (Number->IsFloatingPoint()) Number->SetFloatingPointPropertyValue(Value, D);
			else Number->SetIntPropertyValue(Value, static_cast<int64>(D));
			return true;
		}
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Json.IsValid() || Json->Type != EJson::Object || !Json->TryGetObject(Object) || !Object || !Object->IsValid()) { OutReason = TEXT("expects {x, y, z}"); return false; }
		FVector& Vector = *static_cast<FVector*>(Value);
		(*Object)->TryGetNumberField(TEXT("x"), Vector.X);
		(*Object)->TryGetNumberField(TEXT("y"), Vector.Y);
		(*Object)->TryGetNumberField(TEXT("z"), Vector.Z);
		return true;
	}

	const FProperty* MCPFindBuildSettingField(const FString& Name)
	{
		for (TFieldIterator<FProperty> It(FMeshBuildSettings::StaticStruct()); It; ++It)
		{
			if (MCPIsBuildSettingField(*It) && It->GetName().Equals(Name, ESearchCase::IgnoreCase)) return *It;
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> MCPBuildSettingsToJson(const FMeshBuildSettings& Settings)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(FMeshBuildSettings::StaticStruct()); It; ++It)
		{
			if (MCPIsBuildSettingField(*It)) Out->SetField(It->GetName(), MCPBuildSettingToJson(*It, &Settings));
		}
		return Out;
	}

	/** Bounds in the shape get_mesh_bounds answers with, so the two compare field for field. */
	TSharedPtr<FJsonObject> MCPMeshBoxToJson(const FBox& Box)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetObjectField(TEXT("min"), MCPVec3ToJsonObject(Box.Min));
		Out->SetObjectField(TEXT("max"), MCPVec3ToJsonObject(Box.Max));
		Out->SetObjectField(TEXT("boxExtent"), MCPVec3ToJsonObject(Box.GetExtent()));
		Out->SetObjectField(TEXT("boxCenter"), MCPVec3ToJsonObject(Box.GetCenter()));
		return Out;
	}

	/** How much a unit vector along Axis grows under the per-axis Scale. */
	double MCPScaleAlong(const FVector& Scale, const FVector& Axis)
	{
		return (Scale * Axis).Size();
	}

	struct FMCPCollisionScaleCounts
	{
		int32 Scaled = 0;
		int32 Approximated = 0;
		int32 Unsupported = 0;
	};

	/**
	 * Applies p' = Scale * p + Offset to the simple collision. Convex hulls and unrotated boxes stay exact under any
	 * positive scale, and every shape is exact under a uniform one. A rotated box, sphere or capsule cannot hold a
	 * non-uniform scale, so it gets the nearest shape it can represent and is counted as approximated.
	 */
	FMCPCollisionScaleCounts MCPScaleAggGeom(FKAggregateGeom& Geom, const FVector& Scale, const FVector& Offset)
	{
		FMCPCollisionScaleCounts Counts;
		const bool bUniform = Scale.AllComponentsEqual(UE_KINDA_SMALL_NUMBER);
		const double MaxScale = Scale.GetMax();
		auto MovePoint = [&Scale, &Offset](const FVector& P) { return Scale * P + Offset; };

		for (FKSphereElem& Sphere : Geom.SphereElems)
		{
			Sphere.Center = MovePoint(Sphere.Center);
			Sphere.Radius = static_cast<float>(Sphere.Radius * MaxScale);
			++(bUniform ? Counts.Scaled : Counts.Approximated);
		}
		for (FKBoxElem& Box : Geom.BoxElems)
		{
			const FQuat Rotation = Box.Rotation.Quaternion();
			Box.Center = MovePoint(Box.Center);
			Box.X = static_cast<float>(Box.X * MCPScaleAlong(Scale, Rotation.GetAxisX()));
			Box.Y = static_cast<float>(Box.Y * MCPScaleAlong(Scale, Rotation.GetAxisY()));
			Box.Z = static_cast<float>(Box.Z * MCPScaleAlong(Scale, Rotation.GetAxisZ()));
			++((bUniform || Box.Rotation.IsNearlyZero()) ? Counts.Scaled : Counts.Approximated);
		}
		for (FKSphylElem& Capsule : Geom.SphylElems)
		{
			const FVector Axis = Capsule.Rotation.Quaternion().GetAxisZ();
			Capsule.Center = MovePoint(Capsule.Center);
			Capsule.Length = static_cast<float>(Capsule.Length * MCPScaleAlong(Scale, Axis));
			Capsule.Radius = static_cast<float>(Capsule.Radius * MaxScale);
			++(bUniform ? Counts.Scaled : Counts.Approximated);
		}
		for (FKTaperedCapsuleElem& Capsule : Geom.TaperedCapsuleElems)
		{
			const FVector Axis = Capsule.Rotation.Quaternion().GetAxisZ();
			Capsule.Center = MovePoint(Capsule.Center);
			Capsule.Length = static_cast<float>(Capsule.Length * MCPScaleAlong(Scale, Axis));
			Capsule.Radius0 = static_cast<float>(Capsule.Radius0 * MaxScale);
			Capsule.Radius1 = static_cast<float>(Capsule.Radius1 * MaxScale);
			++(bUniform ? Counts.Scaled : Counts.Approximated);
		}
		for (FKConvexElem& Convex : Geom.ConvexElems)
		{
			// The hull is baked into mesh space, so its element transform becomes identity.
			const FTransform ElemTransform = Convex.GetTransform();
			for (FVector& Vertex : Convex.VertexData)
			{
				Vertex = MovePoint(ElemTransform.TransformPosition(Vertex));
			}
			Convex.SetTransform(FTransform::Identity);
			Convex.UpdateElemBox();
			++Counts.Scaled;
		}
		Counts.Unsupported = Geom.GetElementCount() - Counts.Scaled - Counts.Approximated;
		return Counts;
	}
}

void FAssetMeshBuildHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("asset"));
	using EType = EMCPParamType;
	Registry.RegisterHandler(TEXT("get_static_mesh_build_settings"), &GetStaticMeshBuildSettings, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("StaticMesh asset path")).Alias(TEXT("path")),
		MCPParam::Optional(TEXT("lodIndex"), EType::Integer, TEXT("Source model (LOD) to read (default 0)")),
	});
	Registry.RegisterHandler(TEXT("set_static_mesh_build_settings"), &SetStaticMeshBuildSettings, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("StaticMesh asset path")).Alias(TEXT("path")),
		MCPParam::Required(TEXT("settings"), EType::Object, TEXT("FMeshBuildSettings fields to write by name, e.g. {bRecomputeNormals: true}; get_static_mesh_build_settings lists them")),
		MCPParam::Optional(TEXT("lodIndex"), EType::Integer, TEXT("Source model (LOD) to write (default 0)")),
		MCPParam::Optional(TEXT("save"), EType::Boolean, TEXT("Save the mesh after the rebuild (default true)")),
	});
	// A rebuild of a dense mesh takes longer than the default handler timeout.
	Registry.RegisterHandlerWithTimeout(TEXT("scale_mesh_geometry"), &ScaleMeshGeometry, 300.0f, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("StaticMesh to scale")).Alias(TEXT("path")),
		MCPParam::Required(TEXT("factor"), EType::Number, TEXT("Uniform factor, or {x, y, z} per axis; every component must be above 0")).Or(EType::Vec3),
		MCPParam::Optional(TEXT("pivot"), EType::Vec3, TEXT("Mesh-space point that stays fixed (default the mesh origin)")),
		MCPParam::Optional(TEXT("outputPath"), EType::String, TEXT("Scale a copy written here and leave assetPath untouched (default: scale assetPath in place)")),
		MCPParam::Optional(TEXT("scaleSockets"), EType::Boolean, TEXT("Move socket locations with the geometry (default true)")),
		MCPParam::Optional(TEXT("scaleCollision"), EType::Boolean, TEXT("Scale the simple collision with the geometry (default true)")),
		MCPParam::Optional(TEXT("save"), EType::Boolean, TEXT("Save the mesh after the rebuild (default true)")),
	});
}

TSharedPtr<FJsonValue> FAssetMeshBuildHandlers::GetStaticMeshBuildSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	// Parameters are read before the asset loads, so a call naming a missing asset still reads all of them.
	const int32 LodIndex = OptionalInt(Params, TEXT("lodIndex"), 0);
	REQUIRE_ASSET(UStaticMesh, Mesh, AssetPath);
	if (LodIndex < 0 || LodIndex >= Mesh->GetNumSourceModels())
	{
		return MCPError(FString::Printf(TEXT("lodIndex %d is out of range: the mesh has %d source models."), LodIndex, Mesh->GetNumSourceModels()));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	Result->SetNumberField(TEXT("lodIndex"), LodIndex);
	Result->SetNumberField(TEXT("sourceModelCount"), Mesh->GetNumSourceModels());
	Result->SetObjectField(TEXT("settings"), MCPBuildSettingsToJson(Mesh->GetSourceModel(LodIndex).BuildSettings));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetMeshBuildHandlers::SetStaticMeshBuildSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	// Parameters are read before anything can refuse, so a refused call still reads all of them.
	const TSharedPtr<FJsonObject>* SettingsJson = nullptr;
	const bool bHasSettings = TryGetObjectParam(Params, TEXT("settings"), SettingsJson);
	const int32 LodIndex = OptionalInt(Params, TEXT("lodIndex"), 0);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	if (!bHasSettings || !SettingsJson || !SettingsJson->IsValid() || (*SettingsJson)->Values.Num() == 0)
	{
		return MCPError(TEXT("Missing 'settings': an object of FMeshBuildSettings fields to write."));
	}
	REQUIRE_ASSET(UStaticMesh, Mesh, AssetPath);
	if (LodIndex < 0 || LodIndex >= Mesh->GetNumSourceModels())
	{
		return MCPError(FString::Printf(TEXT("lodIndex %d is out of range: the mesh has %d source models."), LodIndex, Mesh->GetNumSourceModels()));
	}

	// Every field is validated against a copy before anything touches the mesh.
	const FMeshBuildSettings Previous = Mesh->GetSourceModel(LodIndex).BuildSettings;
	FMeshBuildSettings Next = Previous;
	TArray<FString> Problems;
	TArray<const FProperty*> Written;
	for (const auto& Pair : (*SettingsJson)->Values)
	{
		const FString Key(*Pair.Key);
		const FProperty* Field = MCPFindBuildSettingField(Key);
		FString Reason;
		if (!Field)
		{
			Problems.Add(FString::Printf(TEXT("%s is not an FMeshBuildSettings field"), *Key));
		}
		else if (!MCPJsonToBuildSetting(Field, &Next, Pair.Value, Reason))
		{
			Problems.Add(FString::Printf(TEXT("%s %s"), *Field->GetName(), *Reason));
		}
		else
		{
			Written.Add(Field);
		}
	}
	if (Problems.Num() > 0)
	{
		return MCPError(FString::Printf(TEXT("Nothing was changed: %s. get_static_mesh_build_settings lists the fields."), *FString::Join(Problems, TEXT("; "))));
	}

	bool bChanged = false;
	TSharedPtr<FJsonObject> PreviousValues = MakeShared<FJsonObject>();
	for (const FProperty* Field : Written)
	{
		PreviousValues->SetField(Field->GetName(), MCPBuildSettingToJson(Field, &Previous));
		bChanged |= !Field->Identical_InContainer(&Previous, &Next);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	Result->SetNumberField(TEXT("lodIndex"), LodIndex);
	Result->SetObjectField(TEXT("previous"), PreviousValues);
	if (!bChanged)
	{
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rebuilt"), false);
		Result->SetObjectField(TEXT("settings"), MCPBuildSettingsToJson(Previous));
		Result->SetStringField(TEXT("note"), TEXT("The settings already held these values, so nothing was written and the mesh was not rebuilt."));
		return MCPResult(Result);
	}

	Mesh->Modify();
	Mesh->GetSourceModel(LodIndex).BuildSettings = Next;
	Mesh->Build(/*bSilent*/ true);
	Mesh->PostEditChange();

	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("rebuilt"), true);
	Result->SetObjectField(TEXT("settings"), MCPBuildSettingsToJson(Mesh->GetSourceModel(LodIndex).BuildSettings));
	// The editor takes mesh bounds from the source mesh description, which BuildScale3D never touches (#1652),
	// so a rebuild scales the render vertices and leaves the bounds at the source size. Report both.
	if (!Mesh->GetSourceModel(0).BuildSettings.BuildScale3D.Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER))
	{
		const FBox MeshBounds = Mesh->GetBoundingBox();
		Result->SetObjectField(TEXT("bounds"), MCPMeshBoxToJson(MeshBounds));
		if (const FStaticMeshRenderData* RenderData = Mesh->GetRenderData())
		{
			const FBox RenderBounds = RenderData->Bounds.GetBox();
			Result->SetObjectField(TEXT("renderBounds"), MCPMeshBoxToJson(RenderBounds));
			const double Tolerance = FMath::Max(0.01, RenderBounds.GetExtent().GetMax() * 0.001);
			const bool bMatch = RenderBounds.GetExtent().Equals(MeshBounds.GetExtent(), Tolerance);
			Result->SetBoolField(TEXT("boundsMatchRender"), bMatch);
			if (!bMatch)
			{
				TArray<FString> Warnings;
				Warnings.Add(TEXT("BuildScale3D scaled the render vertices, but the editor computes mesh bounds (get_mesh_bounds, actor bounds, culling) from the unscaled source geometry, so they keep the source size. Use scale_mesh_geometry to bake the scale into the geometry and leave BuildScale3D at 1."));
				Result->SetArrayField(TEXT("warnings"), MCPStringListToJson(Warnings));
			}
		}
	}
	if (bSave)
	{
		FString SaveError;
		const bool bSaved = SaveAssetPackageChecked(Mesh, SaveError);
		MCPNoteSaveOutcome(Result, Mesh->GetPathName(), bSaved, SaveError);
	}
	else
	{
		Result->SetBoolField(TEXT("saved"), false);
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	Payload->SetNumberField(TEXT("lodIndex"), LodIndex);
	Payload->SetObjectField(TEXT("settings"), PreviousValues);
	Payload->SetBoolField(TEXT("save"), bSave);
	MCPSetRollback(Result, TEXT("set_static_mesh_build_settings"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetMeshBuildHandlers::ScaleMeshGeometry(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	// Parameters are read before anything can refuse, so a refused call still reads all of them.
	const TSharedPtr<FJsonValue> ScaleJson = TryGetParam(Params, TEXT("factor"));
	const FVector Pivot = OptionalVec3(Params, TEXT("pivot"), FVector::ZeroVector);
	const FString OutputPath = OptionalString(Params, TEXT("outputPath"));
	const bool bScaleSockets = OptionalBool(Params, TEXT("scaleSockets"), true);
	const bool bScaleCollision = OptionalBool(Params, TEXT("scaleCollision"), true);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);

	FVector Scale = FVector::OneVector;
	if (ScaleJson.IsValid() && ScaleJson->Type == EJson::Number)
	{
		Scale = FVector(ScaleJson->AsNumber());
	}
	else if (ScaleJson.IsValid() && ScaleJson->Type == EJson::Object)
	{
		ReadVec3Fields(ScaleJson->AsObject(), Scale);
	}
	else
	{
		return MCPError(TEXT("Missing 'factor': a number for a uniform factor, or {x, y, z} per axis."));
	}
	if (Scale.ContainsNaN() || Scale.X <= 0.0 || Scale.Y <= 0.0 || Scale.Z <= 0.0)
	{
		return MCPError(FString::Printf(
			TEXT("Every factor component must be above 0, got (%g, %g, %g). Mirroring is apply_mesh_mirror."),
			Scale.X, Scale.Y, Scale.Z));
	}

	REQUIRE_ASSET(UStaticMesh, Source, AssetPath);
	const FString TargetPath = OutputPath.IsEmpty() ? Source->GetPathName() : OutputPath;
	if (MCPIsProtectedAssetPath(TargetPath)) return MCPProtectedPathError(TargetPath);

	UStaticMesh* Mesh = Source;
	bool bCreated = false;
	if (!OutputPath.IsEmpty())
	{
		if (MCPLoadAssetObject(OutputPath))
		{
			return MCPError(FString::Printf(TEXT("'%s' already exists. Name a different outputPath, or delete it first."), *OutputPath));
		}
		Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::DuplicateAsset(Source->GetPathName(), OutputPath));
		if (!Mesh)
		{
			return MCPError(FString::Printf(TEXT("Could not copy '%s' to '%s'."), *AssetPath, *OutputPath));
		}
		bCreated = true;
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	if (bCreated) Result->SetStringField(TEXT("sourcePath"), Source->GetPathName());
	Result->SetObjectField(TEXT("factor"), MCPVec3ToJsonObject(Scale));
	Result->SetObjectField(TEXT("pivot"), MCPVec3ToJsonObject(Pivot));
	const FBox BoundsBefore = Mesh->GetBoundingBox();
	Result->SetObjectField(TEXT("previousBounds"), MCPMeshBoxToJson(BoundsBefore));

	if (!bCreated && Scale.Equals(FVector::OneVector, UE_SMALL_NUMBER))
	{
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rebuilt"), false);
		Result->SetObjectField(TEXT("bounds"), MCPMeshBoxToJson(BoundsBefore));
		Result->SetStringField(TEXT("note"), TEXT("A factor of 1 changes nothing, so the mesh was not touched."));
		return MCPResult(Result);
	}

	// p' = Pivot + (p - Pivot) * Scale
	const FVector Offset = Pivot - Scale * Pivot;
	const FTransform Transform(FQuat::Identity, Offset, Scale);

	Mesh->Modify();
	TArray<TSharedPtr<FJsonValue>> LodResults;
	int32 ScaledLods = 0;
	for (int32 LodIndex = 0; LodIndex < Mesh->GetNumSourceModels(); ++LodIndex)
	{
		TSharedPtr<FJsonObject> Lod = MakeShared<FJsonObject>();
		Lod->SetNumberField(TEXT("lodIndex"), LodIndex);
		FMeshDescription* Description = Mesh->IsMeshDescriptionValid(LodIndex) ? Mesh->GetMeshDescription(LodIndex) : nullptr;
		if (Description)
		{
			FStaticMeshOperations::ApplyTransform(*Description, Transform, /*bApplyCorrectNormalTransform*/ true);
			Mesh->CommitMeshDescription(LodIndex);
			Lod->SetBoolField(TEXT("scaled"), true);
			Lod->SetNumberField(TEXT("vertexCount"), Description->Vertices().Num());
			++ScaledLods;
		}
		else
		{
			// A generated LOD has no geometry of its own; the rebuild reduces it from the scaled LOD 0.
			Lod->SetBoolField(TEXT("scaled"), false);
			Lod->SetStringField(TEXT("reason"), TEXT("no source geometry: generated from LOD 0 at build time"));
		}
		LodResults.Add(MakeShared<FJsonValueObject>(Lod));
	}
	if (ScaledLods == 0)
	{
		return MCPError(FString::Printf(TEXT("'%s' has no source geometry to scale: no LOD holds a mesh description."), *Mesh->GetPathName()));
	}
	bool bScaledHiRes = false;
	if (Mesh->IsHiResMeshDescriptionValid())
	{
		if (FMeshDescription* HiRes = Mesh->GetHiResMeshDescription())
		{
			FStaticMeshOperations::ApplyTransform(*HiRes, Transform, /*bApplyCorrectNormalTransform*/ true);
			Mesh->CommitHiResMeshDescription();
			bScaledHiRes = true;
		}
	}

	int32 ScaledSockets = 0;
	if (bScaleSockets)
	{
		for (UStaticMeshSocket* Socket : Mesh->Sockets)
		{
			if (!Socket) continue;
			Socket->Modify();
			Socket->RelativeLocation = Scale * Socket->RelativeLocation + Offset;
			++ScaledSockets;
		}
	}

	TSharedPtr<FJsonObject> CollisionJson = MakeShared<FJsonObject>();
	FMCPCollisionScaleCounts Counts;
	UBodySetup* BodySetup = Mesh->GetBodySetup();
	const bool bCollisionScaled = bScaleCollision && BodySetup && BodySetup->AggGeom.GetElementCount() > 0;
	if (bCollisionScaled)
	{
		BodySetup->Modify();
		Counts = MCPScaleAggGeom(BodySetup->AggGeom, Scale, Offset);
		BodySetup->InvalidatePhysicsData();
		BodySetup->CreatePhysicsMeshes();
	}
	CollisionJson->SetBoolField(TEXT("skipped"), !bCollisionScaled);
	CollisionJson->SetNumberField(TEXT("scaled"), Counts.Scaled);
	CollisionJson->SetNumberField(TEXT("approximated"), Counts.Approximated);
	CollisionJson->SetNumberField(TEXT("unsupported"), Counts.Unsupported);

	Mesh->Build(/*bSilent*/ true);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	if (bCreated) MCPSetCreated(Result);
	else MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("rebuilt"), true);
	Result->SetObjectField(TEXT("bounds"), MCPMeshBoxToJson(Mesh->GetBoundingBox()));
	Result->SetArrayField(TEXT("lods"), LodResults);
	Result->SetBoolField(TEXT("hiResScaled"), bScaledHiRes);
	Result->SetNumberField(TEXT("socketsScaled"), ScaledSockets);
	Result->SetObjectField(TEXT("collision"), CollisionJson);

	TArray<FString> Warnings;
	const FVector BuildScale = Mesh->GetSourceModel(0).BuildSettings.BuildScale3D;
	if (!BuildScale.Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER))
	{
		Warnings.Add(FString::Printf(
			TEXT("BuildScale3D is (%g, %g, %g), so the rendered mesh is scaled by that again on top of this. Set it back to 1 with set_static_mesh_build_settings."),
			BuildScale.X, BuildScale.Y, BuildScale.Z));
	}
	if (Counts.Approximated > 0)
	{
		Warnings.Add(FString::Printf(
			TEXT("%d collision shape(s) cannot hold this non-uniform scale exactly (a rotated box, a sphere or a capsule) and were fitted to the nearest shape they can represent."),
			Counts.Approximated));
	}
	if (Counts.Unsupported > 0)
	{
		Warnings.Add(FString::Printf(TEXT("%d collision shape(s) of a kind this action does not scale were left as they were."), Counts.Unsupported));
	}
	if (Warnings.Num() > 0) Result->SetArrayField(TEXT("warnings"), MCPStringListToJson(Warnings));

	if (bSave)
	{
		FString SaveError;
		const bool bSaved = SaveAssetPackageChecked(Mesh, SaveError);
		MCPNoteSaveOutcome(Result, Mesh->GetPathName(), bSaved, SaveError);
	}
	else
	{
		Result->SetBoolField(TEXT("saved"), false);
	}

	if (bCreated)
	{
		MCPSetDeleteAssetRollback(Result, Mesh->GetPathName());
	}
	else
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
		Payload->SetObjectField(TEXT("factor"), MCPVec3ToJsonObject(FVector(1.0 / Scale.X, 1.0 / Scale.Y, 1.0 / Scale.Z)));
		Payload->SetObjectField(TEXT("pivot"), MCPVec3ToJsonObject(Pivot));
		Payload->SetBoolField(TEXT("scaleSockets"), bScaleSockets);
		Payload->SetBoolField(TEXT("scaleCollision"), bScaleCollision);
		Payload->SetBoolField(TEXT("save"), bSave);
		MCPSetRollback(Result, TEXT("scale_mesh_geometry"), Payload);
	}
	return MCPResult(Result);
}
