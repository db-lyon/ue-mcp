#include "AssetHandlers_MeshBuild.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPackageSave.h"
#include "Engine/StaticMesh.h"
#include "Engine/EngineTypes.h"
#include "StaticMeshResources.h"
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
