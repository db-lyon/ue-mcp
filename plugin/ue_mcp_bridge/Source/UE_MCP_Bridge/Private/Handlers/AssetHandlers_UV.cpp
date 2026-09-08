// UV authoring for StaticMesh and SkeletalMesh assets.
//
// Before this file the bridge could see exactly one UV channel, read-only, off
// render data (asset(get_mesh_geometry)), and could not add a channel, move a
// UV, unwrap anything, or answer "will this mesh bake a clean lightmap". Every
// one of those was an execute_python escape hatch.
//
// All functions below are members of FAssetHandlers: this is a translation-unit
// partition the way AssetHandlers_Mesh.cpp is, not a new class. Declarations go
// in AssetHandlers.h and registration in AssetHandlers.cpp::RegisterHandlers.
//
// Two engine surfaces are used, deliberately split:
//
//   * FMeshDescription + FStaticMeshAttributes for everything that is a UV edit
//     and nothing else - channel count, channel copy, and the affine transform.
//     These are exact, they touch no other attribute, and their inverse is
//     itself, which is what makes a real rollback record possible.
//
//   * Geometry Script, by REFLECTION, for auto-unwrap and island packing, which
//     has no counterpart in MeshDescription. GeometryScripting is an engine
//     PLUGIN, so it is reached the way AssetHandlers_MeshBoolean.cpp reaches it:
//     UFUNCTIONs called through a heap-allocated parameter frame, no Build.cs
//     dependency, and a named "geometry_scripting_unavailable" degradation for a
//     project that does not have it enabled.
//
// Lightmap UVs are NOT a setter here. LightMapCoordinateIndex,
// FMeshBuildSettings::bGenerateLightmapUVs and DstLightmapIndex are plain
// UPROPERTYs that asset(set_property) can already write, and writing them does
// nothing observable until UStaticMesh::Build() runs. generate_lightmap_uvs
// applies them, rebuilds, and reads the resulting channel back, because the
// read-back is the only part a caller cannot do itself.
//
// UNITY BUILD: every file-local symbol below is prefixed MCPUv / FMCPUv. The
// asset handlers share one unity blob, so a helper named the same as one in
// AssetHandlers_Mesh.cpp or AssetHandlers_MeshBoolean.cpp is a redefinition
// (C2084) on whichever machine happens to group them together.

#include "AssetHandlers.h"

#include "HandlerFunctionCall.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMeshSourceData.h"
#include "Engine/EngineTypes.h"
#include "Components.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "MeshAttributeArray.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{

// ─── Constants ───────────────────────────────────────────────────────────────

/** UV coordinates equal to within this are the same UV. Chosen to be well
 *  below one texel of a 16k texture, so two coordinates this close cannot be
 *  distinguished by any texture the mesh will ever sample. */
constexpr float MCPUvWeldTolerance = 1.0e-5f;

/** A UV triangle with less area than this is degenerate: it samples a single
 *  point of the texture no matter how the mesh is shaded. */
constexpr double MCPUvDegenerateArea = 1.0e-10;

/** Default resolution of the square used to measure overlap and coverage. */
constexpr int32 MCPUvDefaultRasterSize = 512;
constexpr int32 MCPUvMinRasterSize = 32;
constexpr int32 MCPUvMaxRasterSize = 4096;

/** Default and maximum size of an exported layout PNG. */
constexpr int32 MCPUvDefaultImageSize = 1024;
constexpr int32 MCPUvMaxImageSize = 4096;

/** Hard ceiling on UV channels. MAX_STATIC_TEXCOORDS is the engine's own. */
constexpr int32 MCPUvMaxChannels = MAX_STATIC_TEXCOORDS;

// ─── The Geometry Script surface this file drives ────────────────────────────

const TCHAR* const MCPUvGSDynamicMeshClass    = TEXT("/Script/GeometryFramework.DynamicMesh");
const TCHAR* const MCPUvGSDebugClass          = TEXT("/Script/GeometryScriptingCore.GeometryScriptDebug");
const TCHAR* const MCPUvGSAssetFunctions      = TEXT("/Script/GeometryScriptingCore.GeometryScriptLibrary_StaticMeshFunctions");
const TCHAR* const MCPUvGSUVFunctions         = TEXT("/Script/GeometryScriptingCore.GeometryScriptLibrary_MeshUVFunctions");
const TCHAR* const MCPUvGSLODTypeEnum         = TEXT("/Script/GeometryScriptingCore.EGeometryScriptLODType");
const TCHAR* const MCPUvGSFlattenMethodEnum   = TEXT("/Script/GeometryScriptingCore.EGeometryScriptUVFlattenMethod");
const TCHAR* const MCPUvGSIslandSourceEnum    = TEXT("/Script/GeometryScriptingCore.EGeometryScriptUVIslandSource");
const TCHAR* const MCPUvGSLightmapOptionEnum  = TEXT("/Script/GeometryScriptingCore.EGeometryScriptGenerateLightmapUVOptions");

/** Ask the module system for the Geometry Script modules once. False when the
 *  plugin is absent or disabled for this project. */
bool MCPUvEnsureGeometryScripting()
{
	static const TCHAR* const Modules[] = {
		TEXT("GeometryFramework"),
		TEXT("GeometryScriptingCore"),
		TEXT("GeometryScriptingEditor")
	};
	for (const TCHAR* Name : Modules)
	{
		const FName ModuleName(Name);
		if (!FModuleManager::Get().IsModuleLoaded(ModuleName))
		{
			// LoadModule, not LoadModuleChecked: a project that simply does not
			// have the plugin must get an answer, not a crashed editor.
			FModuleManager::Get().LoadModule(ModuleName);
		}
	}
	return FindObject<UClass>(nullptr, MCPUvGSDynamicMeshClass) != nullptr
		&& FindObject<UClass>(nullptr, MCPUvGSAssetFunctions) != nullptr
		&& FindObject<UClass>(nullptr, MCPUvGSUVFunctions) != nullptr;
}

TSharedPtr<FJsonValue> MCPUvGeometryScriptUnavailable(const FString& Detail)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), FString::Printf(
		TEXT("GeometryScripting plugin not available: %s Enable the 'Geometry Script' plugin ")
			TEXT("(Edit > Plugins > Geometry Script) and restart the editor, then retry. ")
			TEXT("asset(set_uv_channel_count), asset(transform_uvs), asset(read_uv_channels), ")
			TEXT("asset(check_uvs) and asset(export_uv_layout) do not need it and still work."),
		*Detail));
	Obj->SetStringField(TEXT("reason"), TEXT("geometry_scripting_unavailable"));
	Obj->SetStringField(TEXT("requiredPlugin"), TEXT("GeometryScripting"));
	return MakeShared<FJsonValueObject>(Obj);
}

/** A value from a UEnum this module does not link, by enumerator name.
 *  INDEX_NONE when the enum or the enumerator is absent, so an engine that
 *  renames an enumerator reports that rather than silently picking ordinal 0. */
int64 MCPUvEnumValue(const TCHAR* EnumPath, const FString& EnumeratorName)
{
	UEnum* Enum = FindObject<UEnum>(nullptr, EnumPath);
	if (!Enum) return INDEX_NONE;
	return Enum->GetValueByNameString(EnumeratorName);
}

/**
 * One reflected call into a class this module does not link against.
 *
 * The frame is heap-allocated and every parameter is default-constructed in it,
 * which is how the Geometry Script option structs get their engine defaults
 * without this file knowing their C++ layout: only the fields this handler has
 * an opinion about are overwritten, and by name.
 */
struct FMCPUvScriptCall
{
	UObject* CDO = nullptr;
	UFunction* Function = nullptr;
	TArray<uint8> Frame;

	FMCPUvScriptCall() = default;
	FMCPUvScriptCall(const FMCPUvScriptCall&) = delete;
	FMCPUvScriptCall& operator=(const FMCPUvScriptCall&) = delete;
	~FMCPUvScriptCall() { Release(); }

	bool Bind(const TCHAR* ClassPath, const TCHAR* FunctionName, FString& OutError)
	{
		Release();
		UClass* Class = FindObject<UClass>(nullptr, ClassPath);
		if (!Class)
		{
			OutError = FString::Printf(TEXT("class '%s' is not loaded."), ClassPath);
			return false;
		}
		Function = Class->FindFunctionByName(FName(FunctionName));
		if (!Function)
		{
			OutError = FString::Printf(
				TEXT("'%s' has no reflected function named '%s' in this engine build."),
				ClassPath, FunctionName);
			return false;
		}
		CDO = Class->GetDefaultObject();
		if (!CDO)
		{
			OutError = FString::Printf(TEXT("'%s' has no default object to call through."), ClassPath);
			Function = nullptr;
			return false;
		}
		Frame.SetNumZeroed(FMath::Max<int32>(Function->ParmsSize, 1));
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Frame.GetData());
		}
		return true;
	}

	void Release()
	{
		if (Function && Frame.Num() > 0)
		{
			MCPFunctionCall::DestroyFrame(Function, Frame.GetData());
		}
		Frame.Reset();
		Function = nullptr;
		CDO = nullptr;
	}

	void Invoke() { if (CDO && Function) CDO->ProcessEvent(Function, Frame.GetData()); }

	FProperty* Param(const TCHAR* Name) const
	{
		return Function ? Function->FindPropertyByName(FName(Name)) : nullptr;
	}

	bool SetObject(const TCHAR* Name, UObject* Value)
	{
		FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(Param(Name));
		if (!Prop) return false;
		Prop->SetObjectPropertyValue_InContainer(Frame.GetData(), Value);
		return true;
	}

	bool SetBool(const TCHAR* Name, bool Value)
	{
		FBoolProperty* Prop = CastField<FBoolProperty>(Param(Name));
		if (!Prop) return false;
		Prop->SetPropertyValue_InContainer(Frame.GetData(), Value);
		return true;
	}

	bool SetNumber(const TCHAR* Name, double Value)
	{
		FNumericProperty* Prop = CastField<FNumericProperty>(Param(Name));
		if (!Prop) return false;
		void* Ptr = Prop->ContainerPtrToValuePtr<void>(Frame.GetData());
		if (Prop->IsFloatingPoint()) Prop->SetFloatingPointPropertyValue(Ptr, Value);
		else Prop->SetIntPropertyValue(Ptr, static_cast<int64>(Value));
		return true;
	}

	bool SetVector2D(const TCHAR* Name, const FVector2D& Value)
	{
		FStructProperty* Prop = CastField<FStructProperty>(Param(Name));
		if (!Prop || Prop->Struct != TBaseStructure<FVector2D>::Get()) return false;
		*Prop->ContainerPtrToValuePtr<FVector2D>(Frame.GetData()) = Value;
		return true;
	}

	bool SetTransform(const TCHAR* Name, const FTransform& Value)
	{
		FStructProperty* Prop = CastField<FStructProperty>(Param(Name));
		if (!Prop || Prop->Struct != TBaseStructure<FTransform>::Get()) return false;
		*Prop->ContainerPtrToValuePtr<FTransform>(Frame.GetData()) = Value;
		return true;
	}

	/** Address of one field inside a struct parameter, so an option struct can
	 *  be filled without this module knowing its layout. */
	void* StructField(const TCHAR* ParamName, const TCHAR* FieldName, FProperty*& OutField) const
	{
		OutField = nullptr;
		FStructProperty* Prop = CastField<FStructProperty>(Param(ParamName));
		if (!Prop || !Prop->Struct) return nullptr;
		OutField = Prop->Struct->FindPropertyByName(FName(FieldName));
		if (!OutField) return nullptr;
		void* StructPtr = Prop->ContainerPtrToValuePtr<void>(const_cast<uint8*>(Frame.GetData()));
		return OutField->ContainerPtrToValuePtr<void>(StructPtr);
	}

	bool SetStructBool(const TCHAR* ParamName, const TCHAR* FieldName, bool Value)
	{
		FProperty* Field = nullptr;
		void* Ptr = StructField(ParamName, FieldName, Field);
		FBoolProperty* BoolProp = CastField<FBoolProperty>(Field);
		if (!Ptr || !BoolProp) return false;
		BoolProp->SetPropertyValue(Ptr, Value);
		return true;
	}

	bool SetStructNumber(const TCHAR* ParamName, const TCHAR* FieldName, double Value)
	{
		FProperty* Field = nullptr;
		void* Ptr = StructField(ParamName, FieldName, Field);
		FNumericProperty* NumProp = CastField<FNumericProperty>(Field);
		if (!Ptr || !NumProp) return false;
		if (NumProp->IsFloatingPoint()) NumProp->SetFloatingPointPropertyValue(Ptr, Value);
		else NumProp->SetIntPropertyValue(Ptr, static_cast<int64>(Value));
		return true;
	}

	/** One level deeper: Options.PackingOptions.TargetImageWidth and friends.
	 *  Geometry Script nests its option structs, and a dotted name passed to
	 *  FindPropertyByName resolves to nothing at all rather than failing loudly. */
	bool SetNestedStructNumber(
		const TCHAR* ParamName, const TCHAR* OuterField, const TCHAR* InnerField, double Value)
	{
		FStructProperty* Prop = CastField<FStructProperty>(Param(ParamName));
		if (!Prop || !Prop->Struct) return false;
		FStructProperty* Outer = CastField<FStructProperty>(Prop->Struct->FindPropertyByName(FName(OuterField)));
		if (!Outer || !Outer->Struct) return false;
		FNumericProperty* Inner = CastField<FNumericProperty>(Outer->Struct->FindPropertyByName(FName(InnerField)));
		if (!Inner) return false;
		void* ParamPtr = Prop->ContainerPtrToValuePtr<void>(Frame.GetData());
		void* OuterPtr = Outer->ContainerPtrToValuePtr<void>(ParamPtr);
		void* InnerPtr = Inner->ContainerPtrToValuePtr<void>(OuterPtr);
		if (Inner->IsFloatingPoint()) Inner->SetFloatingPointPropertyValue(InnerPtr, Value);
		else Inner->SetIntPropertyValue(InnerPtr, static_cast<int64>(Value));
		return true;
	}

	bool SetStructEnum(const TCHAR* ParamName, const TCHAR* FieldName, int64 Value)
	{
		if (Value == INDEX_NONE) return false;
		FProperty* Field = nullptr;
		void* Ptr = StructField(ParamName, FieldName, Field);
		if (!Ptr) return false;
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Field))
		{
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(Ptr, Value);
			return true;
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Field))
		{
			ByteProp->SetPropertyValue(Ptr, static_cast<uint8>(Value));
			return true;
		}
		return false;
	}

	/** An enum output read as its enumerator NAME, so the Outcome pin does not
	 *  depend on Failure staying ordinal 0 forever. */
	FString GetEnumName(const TCHAR* Name) const
	{
		FProperty* Prop = Param(Name);
		void* Ptr = Prop ? Prop->ContainerPtrToValuePtr<void>(const_cast<uint8*>(Frame.GetData())) : nullptr;
		if (!Ptr) return FString();
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			const int64 Raw = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(Ptr);
			return EnumProp->GetEnum() ? EnumProp->GetEnum()->GetNameStringByValue(Raw) : FString();
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			const int64 Raw = ByteProp->GetPropertyValue(Ptr);
			return ByteProp->Enum ? ByteProp->Enum->GetNameStringByValue(Raw) : FString();
		}
		return FString();
	}
};

/** Read and clear the messages a UGeometryScriptDebug collected, so the reason
 *  an unwrap failed reaches the caller instead of only the output log. */
TArray<FString> MCPUvDrainDebug(UObject* Debug)
{
	TArray<FString> Out;
	if (!Debug) return Out;

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(
		Debug->GetClass()->FindPropertyByName(FName(TEXT("Messages"))));
	if (!ArrayProp) return Out;
	FStructProperty* ElementProp = CastField<FStructProperty>(ArrayProp->Inner);
	if (!ElementProp || !ElementProp->Struct) return Out;
	FTextProperty* MessageProp = CastField<FTextProperty>(
		ElementProp->Struct->FindPropertyByName(FName(TEXT("Message"))));
	if (!MessageProp) return Out;

	FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Debug));
	for (int32 Index = 0; Index < Helper.Num(); ++Index)
	{
		const FString Text = MessageProp->GetPropertyValue(
			MessageProp->ContainerPtrToValuePtr<void>(Helper.GetRawPtr(Index))).ToString();
		if (!Text.IsEmpty()) Out.Add(Text);
	}
	Helper.EmptyValues();
	return Out;
}

void MCPUvAttachMessages(const TSharedPtr<FJsonObject>& Out, const TArray<FString>& Messages)
{
	if (Messages.Num() == 0) return;
	Out->SetArrayField(TEXT("geometryScriptMessages"), MCPStringListToJson(Messages));
}

// ─── The mesh a UV action is aimed at ────────────────────────────────────────

/**
 * A resolved (asset, LOD, MeshDescription) triple.
 *
 * StaticMesh and SkeletalMesh are both real answers. A SkeletalMesh LOD that is
 * auto-generated from a lower LOD has no mesh description at all, and that is a
 * typed answer (reason = "lod_has_no_source_geometry") rather than a null deref
 * or a bare "failed to get mesh description".
 */
struct FMCPUvTarget
{
	UObject* Asset = nullptr;
	UStaticMesh* StaticMesh = nullptr;
	USkeletalMesh* SkeletalMesh = nullptr;
	int32 LodIndex = 0;
	int32 LodCount = 0;
	FMeshDescription* Desc = nullptr;
	FString AssetPath;

	const TCHAR* TypeName() const { return StaticMesh ? TEXT("StaticMesh") : TEXT("SkeletalMesh"); }
};

/** The refusal for an asset type that has no authorable mesh description.
 *  Typed on purpose: "unsupported asset type" and "this LOD is generated" are
 *  different problems with different fixes. */
TSharedPtr<FJsonValue> MCPUvUnsupported(
	const FString& AssetPath,
	const TCHAR* Reason,
	const FString& Message)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), Message);
	Obj->SetStringField(TEXT("reason"), Reason);
	Obj->SetStringField(TEXT("assetPath"), AssetPath);
	Obj->SetBoolField(TEXT("supported"), false);
	return MakeShared<FJsonValueObject>(Obj);
}

TSharedPtr<FJsonValue> MCPUvResolveTarget(const TSharedPtr<FJsonObject>& Params, FMCPUvTarget& Out)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	Out.AssetPath = AssetPath;

	UObject* Object = MCPLoadAssetObject(AssetPath);
	if (!Object) return MCPAssetNotFoundError(AssetPath, TEXT("Mesh asset"));
	Out.Asset = Object;

	Out.LodIndex = OptionalInt(Params, TEXT("lodIndex"), 0);

	if (UStaticMesh* SM = Cast<UStaticMesh>(Object))
	{
		Out.StaticMesh = SM;
		Out.LodCount = SM->GetNumSourceModels();
	}
	else if (USkeletalMesh* SK = Cast<USkeletalMesh>(Object))
	{
#if WITH_EDITORONLY_DATA && UE_MCP_HAS_5_4_API
		Out.SkeletalMesh = SK;
		Out.LodCount = SK->GetNumSourceModels();
#else
		return MCPUvUnsupported(AssetPath, TEXT("skeletal_uv_unsupported_engine"), FString::Printf(
			TEXT("'%s' is a SkeletalMesh and USkeletalMesh::GetMeshDescription is not available in this engine build, ")
				TEXT("so its UVs cannot be authored through the bridge. StaticMesh assets are supported."),
			*AssetPath));
#endif
	}
	else
	{
		return MCPUvUnsupported(AssetPath, TEXT("unsupported_asset_type"), FString::Printf(
			TEXT("'%s' is a %s. UV authoring works on StaticMesh and SkeletalMesh assets only."),
			*AssetPath, *Object->GetClass()->GetName()));
	}

	if (Out.LodIndex < 0 || Out.LodIndex >= FMath::Max(Out.LodCount, 1))
	{
		return MCPError(FString::Printf(
			TEXT("lodIndex %d is out of range: '%s' has %d LOD source model(s), so valid indices are 0..%d."),
			Out.LodIndex, *AssetPath, Out.LodCount, FMath::Max(Out.LodCount - 1, 0)));
	}

#if WITH_EDITORONLY_DATA
	if (Out.StaticMesh)
	{
		Out.Desc = Out.StaticMesh->GetMeshDescription(Out.LodIndex);
	}
	else if (Out.SkeletalMesh)
	{
		Out.Desc = Out.SkeletalMesh->GetMeshDescription(Out.LodIndex);
	}
#endif

	if (!Out.Desc)
	{
		return MCPUvUnsupported(AssetPath, TEXT("lod_has_no_source_geometry"), FString::Printf(
			TEXT("LOD %d of '%s' has no mesh description. That LOD is generated from a lower one (or its source ")
				TEXT("data was stripped), so there are no authorable UVs on it. Target LOD 0, or import source ")
				TEXT("geometry for this LOD first."),
			Out.LodIndex, *AssetPath));
	}
	return nullptr;
}

/** The UV attribute reference for a mesh description.
 *
 *  FStaticMeshAttributes is correct for a SkeletalMesh description too:
 *  FSkeletalMeshAttributes derives from it, and both store UVs under the same
 *  MeshAttribute::VertexInstance::TextureCoordinate name. Only the accessor is
 *  borrowed - Register() is never called, so no static-mesh-only attribute is
 *  added to a skeletal description. */
TVertexInstanceAttributesRef<FVector2f> MCPUvAttributes(FMeshDescription& Desc)
{
	return FStaticMeshAttributes(Desc).GetVertexInstanceUVs();
}

int32 MCPUvChannelCount(FMeshDescription& Desc)
{
	TVertexInstanceAttributesRef<FVector2f> UVs = MCPUvAttributes(Desc);
	return UVs.IsValid() ? UVs.GetNumChannels() : 0;
}

/** The refusal for a channel index that does not exist. Says how many there
 *  are, because "invalid channel" without the count is a guessing game. */
TSharedPtr<FJsonValue> MCPUvBadChannel(
	const FString& AssetPath,
	const TCHAR* ParamName,
	int32 Requested,
	int32 ChannelCount,
	int32 LodIndex)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), FString::Printf(
		TEXT("%s %d does not exist: LOD %d of '%s' has %d UV channel(s), so valid indices are 0..%d. ")
			TEXT("Add channels with asset(set_uv_channel_count)."),
		ParamName, Requested, LodIndex, *AssetPath, ChannelCount, FMath::Max(ChannelCount - 1, 0)));
	Obj->SetStringField(TEXT("reason"), TEXT("uv_channel_out_of_range"));
	Obj->SetStringField(TEXT("assetPath"), AssetPath);
	Obj->SetStringField(TEXT("parameter"), ParamName);
	Obj->SetNumberField(TEXT("requestedChannel"), Requested);
	Obj->SetNumberField(TEXT("channelCount"), ChannelCount);
	Obj->SetNumberField(TEXT("lodIndex"), LodIndex);
	return MakeShared<FJsonValueObject>(Obj);
}

/** Commit an edited mesh description back to the asset and rebuild it.
 *  Returns false with OutError set; never leaves a half-committed asset. */
bool MCPUvCommit(const FMCPUvTarget& Target, bool bRebuild, FString& OutError)
{
#if WITH_EDITOR
	if (Target.StaticMesh)
	{
		Target.StaticMesh->Modify();
		Target.StaticMesh->ModifyMeshDescription(Target.LodIndex);
		Target.StaticMesh->CommitMeshDescription(Target.LodIndex);
		if (bRebuild)
		{
			Target.StaticMesh->Build(false);
		}
		Target.StaticMesh->PostEditChange();
		Target.StaticMesh->MarkPackageDirty();
		return true;
	}
	if (Target.SkeletalMesh)
	{
		Target.SkeletalMesh->Modify();
		Target.SkeletalMesh->ModifyMeshDescription(Target.LodIndex);
		if (!Target.SkeletalMesh->CommitMeshDescription(Target.LodIndex))
		{
			OutError = FString::Printf(
				TEXT("USkeletalMesh::CommitMeshDescription returned false for LOD %d."), Target.LodIndex);
			return false;
		}
		if (bRebuild)
		{
			Target.SkeletalMesh->Build();
		}
		Target.SkeletalMesh->PostEditChange();
		Target.SkeletalMesh->MarkPackageDirty();
		return true;
	}
#endif
	OutError = TEXT("no editable mesh description on this asset");
	return false;
}

// ─── Channel analysis ────────────────────────────────────────────────────────

/** Everything read_uv_channels, check_uvs and export_uv_layout report about one
 *  channel. Computed once so the three actions cannot disagree. */
struct FMCPUvChannelStats
{
	int32 Channel = 0;
	int32 TriangleCount = 0;
	int32 TrianglesWithUvs = 0;

	double UMin = 0.0, UMax = 0.0, VMin = 0.0, VMax = 0.0;
	bool bHasBounds = false;

	/** Signed-area sum, absolute. 1.0 means the islands cover exactly the area
	 *  of the unit square (they may still be outside it, or overlapping). */
	double UvArea = 0.0;
	/** Fraction of the unit square that at least one triangle rasterises onto. */
	double Coverage = 0.0;
	/** Fraction of the unit square that two or more triangles rasterise onto. */
	double OverlapFraction = 0.0;
	int32 OverlappingTriangleCount = 0;

	int32 IslandCount = 0;
	int32 DegenerateIslandCount = 0;
	int32 SeamEdgeCount = 0;
	int32 DegenerateTriangleCount = 0;
	int32 OutOfUnitSquareTriangleCount = 0;
	int32 FlippedTriangleCount = 0;

	int32 RasterSize = 0;
	/** Island index per triangle, indexed by FTriangleID value. INDEX_NONE for
	 *  triangle slots that are not allocated. Only filled when islands were
	 *  requested, because it is the expensive half. */
	TArray<int32> TriangleIsland;
};

int32 MCPUvFindRoot(TArray<int32>& Parent, int32 Index)
{
	while (Parent[Index] != Index)
	{
		Parent[Index] = Parent[Parent[Index]];
		Index = Parent[Index];
	}
	return Index;
}

void MCPUvUnion(TArray<int32>& Parent, int32 A, int32 B)
{
	const int32 RootA = MCPUvFindRoot(Parent, A);
	const int32 RootB = MCPUvFindRoot(Parent, B);
	if (RootA != RootB) Parent[RootB] = RootA;
}

bool MCPUvNearlyEqual(const FVector2f& A, const FVector2f& B)
{
	return FMath::Abs(A.X - B.X) <= MCPUvWeldTolerance
		&& FMath::Abs(A.Y - B.Y) <= MCPUvWeldTolerance;
}

/** The UV of the corner of Triangle that sits on Vertex, or false if the
 *  triangle does not touch that vertex. */
bool MCPUvCornerUv(
	const FMeshDescription& Desc,
	TVertexInstanceAttributesRef<FVector2f> UVs,
	int32 Channel,
	FTriangleID Triangle,
	FVertexID Vertex,
	FVector2f& OutUv)
{
	for (const FVertexInstanceID Instance : Desc.GetTriangleVertexInstances(Triangle))
	{
		if (Desc.GetVertexInstanceVertex(Instance) == Vertex)
		{
			OutUv = UVs.Get(Instance, Channel);
			return true;
		}
	}
	return false;
}

/** Signed area of a UV triangle. Positive and negative both occur in a healthy
 *  mesh; a sign that disagrees with the majority is a flipped island. */
double MCPUvSignedArea(const FVector2f& A, const FVector2f& B, const FVector2f& C)
{
	return 0.5 * (double(B.X - A.X) * double(C.Y - A.Y) - double(C.X - A.X) * double(B.Y - A.Y));
}

/**
 * Island membership and seam count for one channel.
 *
 * Two triangles are in the same island when they share an edge AND both of
 * their corners on that edge carry the same UV. An edge whose triangles
 * disagree is a UV seam, which is the definition a texture artist uses and the
 * one that makes the seam count comparable to what the UV editor draws.
 */
void MCPUvBuildIslands(
	const FMeshDescription& Desc,
	TVertexInstanceAttributesRef<FVector2f> UVs,
	int32 Channel,
	FMCPUvChannelStats& Stats)
{
	const int32 TriangleArraySize = Desc.Triangles().GetArraySize();
	TArray<int32> Parent;
	Parent.SetNumUninitialized(FMath::Max(TriangleArraySize, 1));
	for (int32 Index = 0; Index < Parent.Num(); ++Index) Parent[Index] = Index;

	int32 SeamEdges = 0;
	for (const FEdgeID EdgeID : Desc.Edges().GetElementIDs())
	{
		const TArray<FTriangleID> Connected = Desc.GetEdgeConnectedTriangles(EdgeID);
		if (Connected.Num() < 2) continue;

		const FVertexID V0 = Desc.GetEdgeVertex(EdgeID, 0);
		const FVertexID V1 = Desc.GetEdgeVertex(EdgeID, 1);

		bool bEdgeIsSeam = false;
		for (int32 A = 0; A < Connected.Num(); ++A)
		{
			for (int32 B = A + 1; B < Connected.Num(); ++B)
			{
				FVector2f A0, A1, B0, B1;
				if (!MCPUvCornerUv(Desc, UVs, Channel, Connected[A], V0, A0)) continue;
				if (!MCPUvCornerUv(Desc, UVs, Channel, Connected[A], V1, A1)) continue;
				if (!MCPUvCornerUv(Desc, UVs, Channel, Connected[B], V0, B0)) continue;
				if (!MCPUvCornerUv(Desc, UVs, Channel, Connected[B], V1, B1)) continue;

				if (MCPUvNearlyEqual(A0, B0) && MCPUvNearlyEqual(A1, B1))
				{
					MCPUvUnion(Parent, Connected[A].GetValue(), Connected[B].GetValue());
				}
				else
				{
					bEdgeIsSeam = true;
				}
			}
		}
		if (bEdgeIsSeam) ++SeamEdges;
	}

	Stats.SeamEdgeCount = SeamEdges;
	Stats.TriangleIsland.Init(INDEX_NONE, FMath::Max(TriangleArraySize, 1));

	TMap<int32, int32> RootToIsland;
	// Islands are numbered in ascending triangle-ID order, so an island index is
	// stable across calls and across any UV transform (an affine map preserves
	// UV equality, so it cannot re-partition the islands). transform_uvs
	// selections and its rollback record both rely on that.
	TArray<double> IslandArea;
	for (const FTriangleID TriangleID : Desc.Triangles().GetElementIDs())
	{
		const int32 Root = MCPUvFindRoot(Parent, TriangleID.GetValue());
		int32* Existing = RootToIsland.Find(Root);
		int32 IslandIndex;
		if (Existing)
		{
			IslandIndex = *Existing;
		}
		else
		{
			IslandIndex = RootToIsland.Num();
			RootToIsland.Add(Root, IslandIndex);
			IslandArea.Add(0.0);
		}
		Stats.TriangleIsland[TriangleID.GetValue()] = IslandIndex;

		const TArrayView<const FVertexInstanceID> Corners = Desc.GetTriangleVertexInstances(TriangleID);
		if (Corners.Num() == 3)
		{
			IslandArea[IslandIndex] += FMath::Abs(MCPUvSignedArea(
				UVs.Get(Corners[0], Channel), UVs.Get(Corners[1], Channel), UVs.Get(Corners[2], Channel)));
		}
	}

	Stats.IslandCount = RootToIsland.Num();
	Stats.DegenerateIslandCount = 0;
	for (const double Area : IslandArea)
	{
		if (Area <= MCPUvDegenerateArea) ++Stats.DegenerateIslandCount;
	}
}

/**
 * Coverage and overlap, by rasterising the channel into a square.
 *
 * An exact pairwise triangle-triangle test is quadratic and unusable on a real
 * asset. Rasterising is linear in covered area, and it measures the thing that
 * actually matters for a lightmap: how much of the texture two triangles are
 * fighting over. It IS an approximation, and the result says so and reports the
 * resolution it used, so a caller can raise it rather than trust a number whose
 * precision it cannot see.
 *
 * Pixel centres are tested with a strict half-space test on a consistently
 * oriented triangle, so a shared edge does not count as an overlap.
 */
void MCPUvRasterise(
	const FMeshDescription& Desc,
	TVertexInstanceAttributesRef<FVector2f> UVs,
	int32 Channel,
	int32 RasterSize,
	FMCPUvChannelStats& Stats,
	TArray<int32>* OutFirstTriangleGrid,
	TBitArray<>* OutOverlapGrid)
{
	const int32 PixelCount = RasterSize * RasterSize;
	TArray<int32> Owner;
	Owner.Init(INDEX_NONE, PixelCount);
	TBitArray<> Overlapped(false, PixelCount);
	TBitArray<> Covered(false, PixelCount);
	TSet<int32> OverlappingTriangles;

	for (const FTriangleID TriangleID : Desc.Triangles().GetElementIDs())
	{
		const TArrayView<const FVertexInstanceID> Corners = Desc.GetTriangleVertexInstances(TriangleID);
		if (Corners.Num() != 3) continue;

		FVector2f P0 = UVs.Get(Corners[0], Channel);
		FVector2f P1 = UVs.Get(Corners[1], Channel);
		FVector2f P2 = UVs.Get(Corners[2], Channel);

		const double Signed = MCPUvSignedArea(P0, P1, P2);
		if (FMath::Abs(Signed) <= MCPUvDegenerateArea) continue;
		if (Signed < 0.0) Swap(P1, P2);

		const float MinU = FMath::Min3(P0.X, P1.X, P2.X);
		const float MaxU = FMath::Max3(P0.X, P1.X, P2.X);
		const float MinV = FMath::Min3(P0.Y, P1.Y, P2.Y);
		const float MaxV = FMath::Max3(P0.Y, P1.Y, P2.Y);

		const int32 X0 = FMath::Clamp(FMath::FloorToInt(MinU * RasterSize), 0, RasterSize - 1);
		const int32 X1 = FMath::Clamp(FMath::CeilToInt(MaxU * RasterSize), 0, RasterSize - 1);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt(MinV * RasterSize), 0, RasterSize - 1);
		const int32 Y1 = FMath::Clamp(FMath::CeilToInt(MaxV * RasterSize), 0, RasterSize - 1);

		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			const double PixelV = (double(Y) + 0.5) / double(RasterSize);
			for (int32 X = X0; X <= X1; ++X)
			{
				const double PixelU = (double(X) + 0.5) / double(RasterSize);
				const double E0 = (double(P1.X) - P0.X) * (PixelV - P0.Y) - (double(P1.Y) - P0.Y) * (PixelU - P0.X);
				const double E1 = (double(P2.X) - P1.X) * (PixelV - P1.Y) - (double(P2.Y) - P1.Y) * (PixelU - P1.X);
				const double E2 = (double(P0.X) - P2.X) * (PixelV - P2.Y) - (double(P0.Y) - P2.Y) * (PixelU - P2.X);
				if (!(E0 > 0.0 && E1 > 0.0 && E2 > 0.0)) continue;

				const int32 Pixel = Y * RasterSize + X;
				Covered[Pixel] = true;
				const int32 Previous = Owner[Pixel];
				if (Previous == INDEX_NONE)
				{
					Owner[Pixel] = TriangleID.GetValue();
				}
				else if (Previous != TriangleID.GetValue())
				{
					Overlapped[Pixel] = true;
					OverlappingTriangles.Add(Previous);
					OverlappingTriangles.Add(TriangleID.GetValue());
				}
			}
		}
	}

	int32 CoveredPixels = 0;
	int32 OverlapPixels = 0;
	for (int32 Pixel = 0; Pixel < PixelCount; ++Pixel)
	{
		if (Covered[Pixel]) ++CoveredPixels;
		if (Overlapped[Pixel]) ++OverlapPixels;
	}

	Stats.RasterSize = RasterSize;
	Stats.Coverage = PixelCount > 0 ? double(CoveredPixels) / double(PixelCount) : 0.0;
	Stats.OverlapFraction = PixelCount > 0 ? double(OverlapPixels) / double(PixelCount) : 0.0;
	Stats.OverlappingTriangleCount = OverlappingTriangles.Num();

	if (OutFirstTriangleGrid) *OutFirstTriangleGrid = MoveTemp(Owner);
	if (OutOverlapGrid) *OutOverlapGrid = MoveTemp(Overlapped);
}

/** Bounds, areas, degenerate and out-of-range counts. Cheap, always run. */
void MCPUvMeasure(
	const FMeshDescription& Desc,
	TVertexInstanceAttributesRef<FVector2f> UVs,
	int32 Channel,
	FMCPUvChannelStats& Stats)
{
	double PositiveArea = 0.0;
	double NegativeArea = 0.0;
	int32 Positive = 0;
	int32 Negative = 0;

	for (const FTriangleID TriangleID : Desc.Triangles().GetElementIDs())
	{
		++Stats.TriangleCount;
		const TArrayView<const FVertexInstanceID> Corners = Desc.GetTriangleVertexInstances(TriangleID);
		if (Corners.Num() != 3) continue;

		const FVector2f P0 = UVs.Get(Corners[0], Channel);
		const FVector2f P1 = UVs.Get(Corners[1], Channel);
		const FVector2f P2 = UVs.Get(Corners[2], Channel);

		if (!Stats.bHasBounds)
		{
			Stats.UMin = Stats.UMax = P0.X;
			Stats.VMin = Stats.VMax = P0.Y;
			Stats.bHasBounds = true;
		}
		for (const FVector2f& P : { P0, P1, P2 })
		{
			Stats.UMin = FMath::Min<double>(Stats.UMin, P.X);
			Stats.UMax = FMath::Max<double>(Stats.UMax, P.X);
			Stats.VMin = FMath::Min<double>(Stats.VMin, P.Y);
			Stats.VMax = FMath::Max<double>(Stats.VMax, P.Y);
		}

		const bool bOutside =
			P0.X < -MCPUvWeldTolerance || P0.X > 1.0f + MCPUvWeldTolerance ||
			P0.Y < -MCPUvWeldTolerance || P0.Y > 1.0f + MCPUvWeldTolerance ||
			P1.X < -MCPUvWeldTolerance || P1.X > 1.0f + MCPUvWeldTolerance ||
			P1.Y < -MCPUvWeldTolerance || P1.Y > 1.0f + MCPUvWeldTolerance ||
			P2.X < -MCPUvWeldTolerance || P2.X > 1.0f + MCPUvWeldTolerance ||
			P2.Y < -MCPUvWeldTolerance || P2.Y > 1.0f + MCPUvWeldTolerance;
		if (bOutside) ++Stats.OutOfUnitSquareTriangleCount;

		const double Signed = MCPUvSignedArea(P0, P1, P2);
		if (FMath::Abs(Signed) <= MCPUvDegenerateArea)
		{
			++Stats.DegenerateTriangleCount;
			continue;
		}
		++Stats.TrianglesWithUvs;
		if (Signed > 0.0) { PositiveArea += Signed; ++Positive; }
		else { NegativeArea += -Signed; ++Negative; }
	}

	Stats.UvArea = PositiveArea + NegativeArea;
	// The minority winding is the flipped one. A mesh with no clear majority is
	// reported as it is rather than guessed at.
	Stats.FlippedTriangleCount = FMath::Min(Positive, Negative);
}

TSharedPtr<FJsonObject> MCPUvChannelToJson(const FMCPUvChannelStats& Stats, int32 LightmapChannel)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("channel"), Stats.Channel);
	Obj->SetBoolField(TEXT("isLightmapChannel"), Stats.Channel == LightmapChannel);
	Obj->SetNumberField(TEXT("triangleCount"), Stats.TriangleCount);
	Obj->SetNumberField(TEXT("nonDegenerateTriangleCount"), Stats.TrianglesWithUvs);

	TSharedPtr<FJsonObject> Bounds = MakeShared<FJsonObject>();
	Bounds->SetNumberField(TEXT("uMin"), Stats.UMin);
	Bounds->SetNumberField(TEXT("uMax"), Stats.UMax);
	Bounds->SetNumberField(TEXT("vMin"), Stats.VMin);
	Bounds->SetNumberField(TEXT("vMax"), Stats.VMax);
	Bounds->SetBoolField(TEXT("withinUnitSquare"), Stats.bHasBounds
		&& Stats.UMin >= -MCPUvWeldTolerance && Stats.UMax <= 1.0 + MCPUvWeldTolerance
		&& Stats.VMin >= -MCPUvWeldTolerance && Stats.VMax <= 1.0 + MCPUvWeldTolerance);
	Obj->SetObjectField(TEXT("bounds"), Bounds);

	Obj->SetNumberField(TEXT("uvArea"), Stats.UvArea);
	Obj->SetNumberField(TEXT("coverage"), Stats.Coverage);
	Obj->SetNumberField(TEXT("overlapFraction"), Stats.OverlapFraction);
	Obj->SetNumberField(TEXT("overlappingTriangleCount"), Stats.OverlappingTriangleCount);
	Obj->SetNumberField(TEXT("islandCount"), Stats.IslandCount);
	Obj->SetNumberField(TEXT("degenerateIslandCount"), Stats.DegenerateIslandCount);
	Obj->SetNumberField(TEXT("seamEdgeCount"), Stats.SeamEdgeCount);
	Obj->SetNumberField(TEXT("degenerateTriangleCount"), Stats.DegenerateTriangleCount);
	Obj->SetNumberField(TEXT("outOfUnitSquareTriangleCount"), Stats.OutOfUnitSquareTriangleCount);
	Obj->SetNumberField(TEXT("flippedTriangleCount"), Stats.FlippedTriangleCount);
	if (Stats.RasterSize > 0)
	{
		Obj->SetNumberField(TEXT("overlapRasterSize"), Stats.RasterSize);
		Obj->SetStringField(TEXT("overlapMethod"), TEXT("raster"));
	}
	return Obj;
}

/** The static-mesh lightmap wiring, or -1 for a skeletal mesh (which has no
 *  lightmap coordinate index of its own). */
int32 MCPUvLightmapChannel(const FMCPUvTarget& Target)
{
	return Target.StaticMesh ? Target.StaticMesh->GetLightMapCoordinateIndex() : INDEX_NONE;
}

void MCPUvWriteAssetHeader(const TSharedPtr<FJsonObject>& Out, const FMCPUvTarget& Target)
{
	Out->SetStringField(TEXT("assetPath"), Target.Asset->GetPathName());
	Out->SetStringField(TEXT("assetType"), Target.TypeName());
	Out->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
	Out->SetNumberField(TEXT("lodCount"), Target.LodCount);
	Out->SetNumberField(TEXT("channelCount"), MCPUvChannelCount(*Target.Desc));
	Out->SetNumberField(TEXT("triangleCount"), Target.Desc->Triangles().Num());
	Out->SetNumberField(TEXT("vertexInstanceCount"), Target.Desc->VertexInstances().Num());
	Out->SetNumberField(TEXT("polygonGroupCount"), Target.Desc->PolygonGroups().Num());

#if WITH_EDITORONLY_DATA
	if (Target.StaticMesh)
	{
		Out->SetNumberField(TEXT("lightmapCoordinateIndex"), Target.StaticMesh->GetLightMapCoordinateIndex());
		Out->SetNumberField(TEXT("lightmapResolution"), Target.StaticMesh->GetLightMapResolution());
		Out->SetNumberField(TEXT("renderDataChannelCount"), Target.StaticMesh->GetNumUVChannels(Target.LodIndex));

		if (Target.StaticMesh->GetNumSourceModels() > Target.LodIndex)
		{
			const FMeshBuildSettings& Build =
				Target.StaticMesh->GetSourceModel(Target.LodIndex).BuildSettings;
			TSharedPtr<FJsonObject> BuildObj = MakeShared<FJsonObject>();
			BuildObj->SetBoolField(TEXT("bGenerateLightmapUVs"), Build.bGenerateLightmapUVs != 0);
			BuildObj->SetNumberField(TEXT("srcLightmapIndex"), Build.SrcLightmapIndex);
			BuildObj->SetNumberField(TEXT("dstLightmapIndex"), Build.DstLightmapIndex);
			BuildObj->SetNumberField(TEXT("minLightmapResolution"), Build.MinLightmapResolution);
			BuildObj->SetBoolField(TEXT("bUseFullPrecisionUVs"), Build.bUseFullPrecisionUVs != 0);
			Out->SetObjectField(TEXT("buildSettings"), BuildObj);
		}
	}
	else
	{
		// Named rather than omitted: a caller that branches on the lightmap
		// channel must be able to tell "none" from "this reader did not look".
		Out->SetNumberField(TEXT("lightmapCoordinateIndex"), -1);
		Out->SetStringField(TEXT("lightmapNote"),
			TEXT("SkeletalMesh assets carry no LightMapCoordinateIndex; lightmap UVs are a StaticMesh concept."));
	}
#endif
}

/** Which channels a read should report, defaulting to all of them. */
TSharedPtr<FJsonValue> MCPUvSelectChannels(
	const TSharedPtr<FJsonObject>& Params,
	const FMCPUvTarget& Target,
	int32 ChannelCount,
	TArray<int32>& OutChannels)
{
	OutChannels.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Requested = nullptr;
	if (Params->TryGetArrayField(TEXT("channels"), Requested) && Requested)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *Requested)
		{
			if (!Entry.IsValid()) continue;
			const int32 Channel = static_cast<int32>(Entry->AsNumber());
			if (Channel < 0 || Channel >= ChannelCount)
			{
				return MCPUvBadChannel(Target.AssetPath, TEXT("channels entry"), Channel, ChannelCount, Target.LodIndex);
			}
			OutChannels.AddUnique(Channel);
		}
		if (OutChannels.Num() == 0)
		{
			return MCPError(TEXT("'channels' was empty. Omit it to report every channel, or list the indices you want."));
		}
		OutChannels.Sort();
		return nullptr;
	}
	for (int32 Channel = 0; Channel < ChannelCount; ++Channel) OutChannels.Add(Channel);
	return nullptr;
}

int32 MCPUvClampRasterSize(const TSharedPtr<FJsonObject>& Params)
{
	return FMath::Clamp(
		OptionalInt(Params, TEXT("rasterSize"), MCPUvDefaultRasterSize),
		MCPUvMinRasterSize, MCPUvMaxRasterSize);
}

// ─── Selection for transform_uvs ─────────────────────────────────────────────

/**
 * Which triangles a transform touches.
 *
 * One action, four filters, because the transform maths is identical in all
 * four cases and only the membership test varies. Four near-identical actions
 * would be four places for the maths to drift apart.
 */
struct FMCPUvSelection
{
	FString Mode = TEXT("all");
	TBitArray<> TriangleMask;
	int32 SelectedTriangles = 0;
	int32 SelectedIslands = 0;
	TSharedPtr<FJsonObject> Report;
};

TSharedPtr<FJsonValue> MCPUvParseSelection(
	const TSharedPtr<FJsonObject>& Params,
	const FMCPUvTarget& Target,
	FMeshDescription& Desc,
	int32 Channel,
	FMCPUvSelection& Out)
{
	const int32 TriangleArraySize = FMath::Max(Desc.Triangles().GetArraySize(), 1);
	Out.TriangleMask.Init(false, TriangleArraySize);
	Out.Report = MakeShared<FJsonObject>();

	const TSharedPtr<FJsonObject>* SelectionPtr = nullptr;
	if (!Params->TryGetObjectField(TEXT("selection"), SelectionPtr) || !SelectionPtr || !SelectionPtr->IsValid())
	{
		Out.Mode = TEXT("all");
		for (const FTriangleID TriangleID : Desc.Triangles().GetElementIDs())
		{
			Out.TriangleMask[TriangleID.GetValue()] = true;
			++Out.SelectedTriangles;
		}
		Out.Report->SetStringField(TEXT("mode"), Out.Mode);
		Out.Report->SetNumberField(TEXT("selectedTriangles"), Out.SelectedTriangles);
		return nullptr;
	}

	const TSharedPtr<FJsonObject>& Selection = *SelectionPtr;
	Out.Mode = OptionalString(Selection, TEXT("mode"), TEXT("all")).ToLower();

	if (Out.Mode == TEXT("all"))
	{
		for (const FTriangleID TriangleID : Desc.Triangles().GetElementIDs())
		{
			Out.TriangleMask[TriangleID.GetValue()] = true;
			++Out.SelectedTriangles;
		}
	}
	else if (Out.Mode == TEXT("island") || Out.Mode == TEXT("islands"))
	{
		Out.Mode = TEXT("island");
		TVertexInstanceAttributesRef<FVector2f> UVs = MCPUvAttributes(Desc);
		FMCPUvChannelStats Islands;
		Islands.Channel = Channel;
		MCPUvBuildIslands(Desc, UVs, Channel, Islands);

		TSet<int32> Wanted;
		const TArray<TSharedPtr<FJsonValue>>* Indices = nullptr;
		if (Selection->TryGetArrayField(TEXT("islandIndices"), Indices) && Indices)
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Indices)
			{
				if (Entry.IsValid()) Wanted.Add(static_cast<int32>(Entry->AsNumber()));
			}
		}
		if (Wanted.Num() == 0)
		{
			return MCPError(FString::Printf(
				TEXT("selection.mode='island' needs a non-empty 'islandIndices' array. LOD %d channel %d of '%s' has ")
					TEXT("%d island(s), so valid indices are 0..%d. asset(read_uv_channels) reports the island count, ")
					TEXT("and asset(export_uv_layout) draws them with their indices."),
				Target.LodIndex, Channel, *Target.AssetPath, Islands.IslandCount,
				FMath::Max(Islands.IslandCount - 1, 0)));
		}
		for (const int32 Index : Wanted)
		{
			if (Index < 0 || Index >= Islands.IslandCount)
			{
				return MCPError(FString::Printf(
					TEXT("islandIndices entry %d does not exist: LOD %d channel %d of '%s' has %d island(s), ")
						TEXT("so valid indices are 0..%d."),
					Index, Target.LodIndex, Channel, *Target.AssetPath, Islands.IslandCount,
					FMath::Max(Islands.IslandCount - 1, 0)));
			}
		}
		for (const FTriangleID TriangleID : Desc.Triangles().GetElementIDs())
		{
			const int32 Island = Islands.TriangleIsland[TriangleID.GetValue()];
			if (Wanted.Contains(Island))
			{
				Out.TriangleMask[TriangleID.GetValue()] = true;
				++Out.SelectedTriangles;
			}
		}
		Out.SelectedIslands = Wanted.Num();
		Out.Report->SetNumberField(TEXT("islandCount"), Islands.IslandCount);
		Out.Report->SetNumberField(TEXT("selectedIslands"), Out.SelectedIslands);
	}
	else if (Out.Mode == TEXT("normal"))
	{
		FVector Direction(0.0, 0.0, 1.0);
		const TSharedPtr<FJsonObject>* DirectionObj = nullptr;
		if (Selection->TryGetObjectField(TEXT("normalDirection"), DirectionObj) && DirectionObj)
		{
			ReadVec3Fields(*DirectionObj, Direction);
		}
		if (Direction.IsNearlyZero())
		{
			return MCPError(TEXT("selection.normalDirection is the zero vector, which names no direction. ")
				TEXT("Pass a non-zero {x,y,z} such as {\"z\":1} for up-facing triangles."));
		}
		Direction.Normalize();
		const double ToleranceDegrees = FMath::Clamp(
			OptionalNumber(Selection, TEXT("normalAngleTolerance"), 45.0), 0.0, 180.0);
		const double CosLimit = FMath::Cos(FMath::DegreesToRadians(ToleranceDegrees));

		auto Positions = Desc.GetVertexPositions();
		for (const FTriangleID TriangleID : Desc.Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexID> Vertices = Desc.GetTriangleVertices(TriangleID);
			if (Vertices.Num() != 3) continue;
			const FVector A(Positions[Vertices[0]]);
			const FVector B(Positions[Vertices[1]]);
			const FVector C(Positions[Vertices[2]]);
			FVector Normal = FVector::CrossProduct(B - A, C - A);
			if (Normal.IsNearlyZero()) continue;
			Normal.Normalize();
			if (FVector::DotProduct(Normal, Direction) >= CosLimit)
			{
				Out.TriangleMask[TriangleID.GetValue()] = true;
				++Out.SelectedTriangles;
			}
		}
		Out.Report->SetObjectField(TEXT("normalDirection"), MCPVec3ToJsonObject(Direction));
		Out.Report->SetNumberField(TEXT("normalAngleTolerance"), ToleranceDegrees);
	}
	else if (Out.Mode == TEXT("polygongroup") || Out.Mode == TEXT("material"))
	{
		Out.Mode = TEXT("polygonGroup");
		TPolygonGroupAttributesRef<FName> SlotNames =
			FStaticMeshAttributes(Desc).GetPolygonGroupMaterialSlotNames();

		TSet<int32> WantedGroups;
		const TArray<TSharedPtr<FJsonValue>>* Groups = nullptr;
		if (Selection->TryGetArrayField(TEXT("polygonGroups"), Groups) && Groups)
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Groups)
			{
				if (Entry.IsValid()) WantedGroups.Add(static_cast<int32>(Entry->AsNumber()));
			}
		}

		TArray<FString> WantedSlots;
		const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
		if (Selection->TryGetArrayField(TEXT("materialSlotNames"), Slots) && Slots)
		{
			WantedSlots = JsonArrayToStringList(Slots);
		}

		TArray<FString> KnownSlots;
		TArray<int32> KnownGroups;
		for (const FPolygonGroupID GroupID : Desc.PolygonGroups().GetElementIDs())
		{
			KnownGroups.Add(GroupID.GetValue());
			KnownSlots.Add(SlotNames.IsValid() ? SlotNames[GroupID].ToString() : FString());
			if (SlotNames.IsValid())
			{
				const FString Name = SlotNames[GroupID].ToString();
				for (const FString& Wanted : WantedSlots)
				{
					if (Name.Equals(Wanted, ESearchCase::IgnoreCase))
					{
						WantedGroups.Add(GroupID.GetValue());
					}
				}
			}
		}

		if (WantedGroups.Num() == 0)
		{
			return MCPError(FString::Printf(
				TEXT("selection.mode='polygonGroup' matched no group. Pass 'polygonGroups' (indices) or ")
					TEXT("'materialSlotNames'. LOD %d of '%s' has groups [%s] with slot names [%s]."),
				Target.LodIndex, *Target.AssetPath,
				*FString::JoinBy(KnownGroups, TEXT(", "), [](int32 G) { return FString::FromInt(G); }),
				*FString::Join(KnownSlots, TEXT(", "))));
		}
		for (const int32 Group : WantedGroups)
		{
			if (!KnownGroups.Contains(Group))
			{
				return MCPError(FString::Printf(
					TEXT("polygonGroups entry %d does not exist. LOD %d of '%s' has groups [%s]."),
					Group, Target.LodIndex, *Target.AssetPath,
					*FString::JoinBy(KnownGroups, TEXT(", "), [](int32 G) { return FString::FromInt(G); })));
			}
		}
		for (const FTriangleID TriangleID : Desc.Triangles().GetElementIDs())
		{
			if (WantedGroups.Contains(Desc.GetTrianglePolygonGroup(TriangleID).GetValue()))
			{
				Out.TriangleMask[TriangleID.GetValue()] = true;
				++Out.SelectedTriangles;
			}
		}
		Out.Report->SetArrayField(TEXT("materialSlotNames"), MCPStringListToJson(KnownSlots));
	}
	else
	{
		return MCPError(FString::Printf(
			TEXT("Unknown selection.mode '%s'. Use all (default), island, normal or polygonGroup."), *Out.Mode));
	}

	Out.Report->SetStringField(TEXT("mode"), Out.Mode);
	Out.Report->SetNumberField(TEXT("selectedTriangles"), Out.SelectedTriangles);
	return nullptr;
}

// ─── Layout rendering ────────────────────────────────────────────────────────

FColor MCPUvIslandColour(int32 Island)
{
	// Golden-angle hue stepping: consecutive islands are always far apart in
	// hue, which is what makes two adjacent islands distinguishable at a glance.
	const float Hue = FMath::Fmod(Island * 137.507f, 360.0f);
	const FLinearColor Linear = FLinearColor(Hue, 0.65f, 0.95f).HSVToLinearRGB();
	return Linear.ToFColor(false);
}

void MCPUvBlendPixel(TArray<FColor>& Pixels, int32 Size, int32 X, int32 Y, const FColor& Colour, float Alpha)
{
	if (X < 0 || Y < 0 || X >= Size || Y >= Size) return;
	FColor& Target = Pixels[Y * Size + X];
	Target.R = static_cast<uint8>(FMath::Clamp(Target.R * (1.0f - Alpha) + Colour.R * Alpha, 0.0f, 255.0f));
	Target.G = static_cast<uint8>(FMath::Clamp(Target.G * (1.0f - Alpha) + Colour.G * Alpha, 0.0f, 255.0f));
	Target.B = static_cast<uint8>(FMath::Clamp(Target.B * (1.0f - Alpha) + Colour.B * Alpha, 0.0f, 255.0f));
	Target.A = 255;
}

void MCPUvDrawLine(TArray<FColor>& Pixels, int32 Size, FVector2f A, FVector2f B, const FColor& Colour)
{
	// UV V grows upward, image Y grows downward, so V is flipped here and only
	// here: every other coordinate in this file stays in UV space.
	int32 X0 = FMath::RoundToInt(A.X * Size);
	int32 Y0 = FMath::RoundToInt((1.0f - A.Y) * Size);
	const int32 X1 = FMath::RoundToInt(B.X * Size);
	const int32 Y1 = FMath::RoundToInt((1.0f - B.Y) * Size);

	const int32 DeltaX = FMath::Abs(X1 - X0);
	const int32 DeltaY = -FMath::Abs(Y1 - Y0);
	const int32 StepX = X0 < X1 ? 1 : -1;
	const int32 StepY = Y0 < Y1 ? 1 : -1;
	int32 Error = DeltaX + DeltaY;

	// Bounded so a wildly out-of-range UV cannot spin here.
	for (int32 Guard = 0; Guard <= 4 * Size + 8; ++Guard)
	{
		MCPUvBlendPixel(Pixels, Size, X0, Y0, Colour, 1.0f);
		if (X0 == X1 && Y0 == Y1) break;
		const int32 Double = 2 * Error;
		if (Double >= DeltaY) { Error += DeltaY; X0 += StepX; }
		if (Double <= DeltaX) { Error += DeltaX; Y0 += StepY; }
	}
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// asset(read_uv_channels)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FAssetHandlers::ReadUvChannels(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPUvTarget Target;
	if (auto Err = MCPUvResolveTarget(Params, Target)) return Err;

	const int32 ChannelCount = MCPUvChannelCount(*Target.Desc);
	const bool bIncludeIslands = OptionalBool(Params, TEXT("includeIslands"), true);
	const bool bIncludeOverlap = OptionalBool(Params, TEXT("includeOverlap"), true);
	const int32 RasterSize = MCPUvClampRasterSize(Params);

	TArray<int32> Channels;
	if (auto Err = MCPUvSelectChannels(Params, Target, ChannelCount, Channels)) return Err;

	const int32 LightmapChannel = MCPUvLightmapChannel(Target);
	TVertexInstanceAttributesRef<FVector2f> UVs = MCPUvAttributes(*Target.Desc);

	TArray<TSharedPtr<FJsonValue>> ChannelRows;
	for (const int32 Channel : Channels)
	{
		FMCPUvChannelStats Stats;
		Stats.Channel = Channel;
		MCPUvMeasure(*Target.Desc, UVs, Channel, Stats);
		if (bIncludeIslands) MCPUvBuildIslands(*Target.Desc, UVs, Channel, Stats);
		if (bIncludeOverlap) MCPUvRasterise(*Target.Desc, UVs, Channel, RasterSize, Stats, nullptr, nullptr);
		ChannelRows.Add(MakeShared<FJsonValueObject>(MCPUvChannelToJson(Stats, LightmapChannel)));
	}

	auto Result = MCPSuccess();
	MCPUvWriteAssetHeader(Result, Target);
	Result->SetArrayField(TEXT("channels"), ChannelRows);
	Result->SetBoolField(TEXT("includedIslands"), bIncludeIslands);
	Result->SetBoolField(TEXT("includedOverlap"), bIncludeOverlap);
	if (ChannelCount == 0)
	{
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("LOD %d of '%s' has no UV channels at all. Add one with asset(set_uv_channel_count) and fill it ")
				TEXT("with asset(unwrap_uvs)."),
			Target.LodIndex, *Target.AssetPath));
	}
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// asset(set_uv_channel_count)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FAssetHandlers::SetUvChannelCount(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPUvTarget Target;
	if (auto Err = MCPUvResolveTarget(Params, Target)) return Err;
	if (MCPIsProtectedAssetPath(Target.AssetPath)) return MCPProtectedPathError(Target.AssetPath);
	if (auto Blocked = MCPAssetWriteBlockedError(Target.Asset, Target.AssetPath, TEXT("change the UV channel count")))
	{
		return Blocked;
	}

	const int32 PreviousCount = MCPUvChannelCount(*Target.Desc);
	const FString Op = OptionalString(Params, TEXT("op"), TEXT("set")).ToLower();
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);

	int32 TargetCount = PreviousCount;
	int32 FromChannel = INDEX_NONE;
	int32 ToChannel = INDEX_NONE;
	bool bDestinationExisted = false;

	if (Op == TEXT("set"))
	{
		if (!Params->HasField(TEXT("channelCount")))
		{
			return MCPError(FString::Printf(
				TEXT("op='set' needs 'channelCount'. LOD %d of '%s' currently has %d channel(s); pass a number ")
					TEXT("between 1 and %d."),
				Target.LodIndex, *Target.AssetPath, PreviousCount, MCPUvMaxChannels));
		}
		TargetCount = OptionalInt(Params, TEXT("channelCount"), PreviousCount);
	}
	else if (Op == TEXT("add"))
	{
		TargetCount = PreviousCount + FMath::Max(1, OptionalInt(Params, TEXT("count"), 1));
	}
	else if (Op == TEXT("remove"))
	{
		if (!Params->HasField(TEXT("channel")))
		{
			return MCPError(FString::Printf(
				TEXT("op='remove' needs 'channel', the index to remove. LOD %d of '%s' has %d channel(s)."),
				Target.LodIndex, *Target.AssetPath, PreviousCount));
		}
		const int32 Channel = OptionalInt(Params, TEXT("channel"), INDEX_NONE);
		if (Channel < 0 || Channel >= PreviousCount)
		{
			return MCPUvBadChannel(Target.AssetPath, TEXT("channel"), Channel, PreviousCount, Target.LodIndex);
		}
		if (PreviousCount <= 1)
		{
			return MCPError(FString::Printf(
				TEXT("Refusing to remove the last UV channel of '%s'. A mesh with zero UV channels cannot be ")
					TEXT("textured and several engine build steps assume channel 0 exists."),
				*Target.AssetPath));
		}
		FromChannel = Channel;
		TargetCount = PreviousCount - 1;
	}
	else if (Op == TEXT("copy"))
	{
		FromChannel = OptionalInt(Params, TEXT("fromChannel"), INDEX_NONE);
		ToChannel = OptionalInt(Params, TEXT("toChannel"), INDEX_NONE);
		if (FromChannel < 0 || FromChannel >= PreviousCount)
		{
			return MCPUvBadChannel(Target.AssetPath, TEXT("fromChannel"), FromChannel, PreviousCount, Target.LodIndex);
		}
		if (ToChannel < 0 || ToChannel >= MCPUvMaxChannels)
		{
			return MCPError(FString::Printf(
				TEXT("toChannel %d is not a usable channel index. Valid indices are 0..%d; a value at or above the ")
					TEXT("current count (%d) grows the channel list to fit."),
				ToChannel, MCPUvMaxChannels - 1, PreviousCount));
		}
		if (ToChannel == FromChannel)
		{
			auto NoOp = MCPSuccess();
			MCPUvWriteAssetHeader(NoOp, Target);
			NoOp->SetStringField(TEXT("op"), Op);
			NoOp->SetBoolField(TEXT("unchanged"), true);
			MCPSetExisted(NoOp);
			NoOp->SetStringField(TEXT("note"), FString::Printf(
				TEXT("fromChannel and toChannel are both %d, so there was nothing to copy."), FromChannel));
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("assetPath"), Target.AssetPath);
			Payload->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
			Payload->SetStringField(TEXT("op"), TEXT("set"));
			Payload->SetNumberField(TEXT("channelCount"), PreviousCount);
			MCPSetRollback(NoOp, TEXT("set_uv_channel_count"), Payload);
			NoOp->SetBoolField(TEXT("rollbackIsNoOp"), true);
			return MCPResult(NoOp);
		}
		bDestinationExisted = ToChannel < PreviousCount;
		TargetCount = FMath::Max(PreviousCount, ToChannel + 1);
	}
	else
	{
		return MCPError(FString::Printf(
			TEXT("Unknown op '%s'. Use set (channelCount), add (count), remove (channel) or copy ")
				TEXT("(fromChannel, toChannel)."),
			*Op));
	}

	if (TargetCount < 1 || TargetCount > MCPUvMaxChannels)
	{
		return MCPError(FString::Printf(
			TEXT("The requested channel count %d is out of range: a mesh must keep at least 1 UV channel and the ")
				TEXT("engine supports at most %d (MAX_STATIC_TEXCOORDS). '%s' currently has %d."),
			TargetCount, MCPUvMaxChannels, *Target.AssetPath, PreviousCount));
	}

	const bool bCountUnchanged = TargetCount == PreviousCount;
	const bool bWouldChangeData = Op == TEXT("copy") || Op == TEXT("remove");

	// Idempotent replay: asking for the count it already has, with no data op,
	// is a success that says nothing happened rather than a redundant rebuild.
	if (bCountUnchanged && !bWouldChangeData)
	{
		auto Existing = MCPSuccess();
		MCPUvWriteAssetHeader(Existing, Target);
		MCPSetExisted(Existing);
		Existing->SetStringField(TEXT("op"), Op);
		Existing->SetBoolField(TEXT("unchanged"), true);
		Existing->SetNumberField(TEXT("previousChannelCount"), PreviousCount);
		Existing->SetNumberField(TEXT("newChannelCount"), TargetCount);
		Existing->SetStringField(TEXT("note"), FString::Printf(
			TEXT("LOD %d of '%s' already has %d UV channel(s); nothing was written and the asset was not rebuilt."),
			Target.LodIndex, *Target.AssetPath, PreviousCount));
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Target.AssetPath);
		Payload->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
		Payload->SetStringField(TEXT("op"), TEXT("set"));
		Payload->SetNumberField(TEXT("channelCount"), PreviousCount);
		MCPSetRollback(Existing, TEXT("set_uv_channel_count"), Payload);
		Existing->SetBoolField(TEXT("rollbackIsNoOp"), true);
		return MCPResult(Existing);
	}

	if (bDryRun)
	{
		auto Preview = MCPSuccess();
		MCPUvWriteAssetHeader(Preview, Target);
		Preview->SetBoolField(TEXT("dryRun"), true);
		Preview->SetBoolField(TEXT("written"), false);
		Preview->SetStringField(TEXT("op"), Op);
		Preview->SetNumberField(TEXT("previousChannelCount"), PreviousCount);
		Preview->SetNumberField(TEXT("newChannelCount"), TargetCount);
		if (Op == TEXT("remove")) Preview->SetNumberField(TEXT("wouldRemoveChannel"), FromChannel);
		if (Op == TEXT("copy"))
		{
			Preview->SetNumberField(TEXT("wouldCopyFrom"), FromChannel);
			Preview->SetNumberField(TEXT("wouldCopyTo"), ToChannel);
			Preview->SetBoolField(TEXT("wouldOverwriteExistingChannel"), bDestinationExisted);
		}
		return MCPResult(Preview);
	}

	// ── Mutate ───────────────────────────────────────────────────────────────
	TVertexInstanceAttributesRef<FVector2f> UVs = MCPUvAttributes(*Target.Desc);
	if (!UVs.IsValid())
	{
		return MCPError(FString::Printf(
			TEXT("LOD %d of '%s' has no VertexInstance TextureCoordinate attribute, so its UV channel list cannot ")
				TEXT("be resized. The mesh description was imported without UV support."),
			Target.LodIndex, *Target.AssetPath));
	}

	if (Op == TEXT("remove"))
	{
		UVs.RemoveChannel(FromChannel);
	}
	else if (Op == TEXT("copy"))
	{
		if (TargetCount != PreviousCount) UVs.SetNumChannels(TargetCount);
		for (const FVertexInstanceID Instance : Target.Desc->VertexInstances().GetElementIDs())
		{
			UVs.Set(Instance, ToChannel, UVs.Get(Instance, FromChannel));
		}
	}
	else
	{
		UVs.SetNumChannels(TargetCount);
	}

	FString CommitError;
	if (!MCPUvCommit(Target, /*bRebuild*/ true, CommitError))
	{
		return MCPError(FString::Printf(
			TEXT("The UV channel edit could not be committed to '%s': %s Nothing was saved."),
			*Target.AssetPath, *CommitError));
	}

	const int32 ActualCount = MCPUvChannelCount(*Target.Desc);

	auto Result = MCPSuccess();
	MCPUvWriteAssetHeader(Result, Target);
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("op"), Op);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetNumberField(TEXT("previousChannelCount"), PreviousCount);
	Result->SetNumberField(TEXT("newChannelCount"), ActualCount);
	if (Op == TEXT("remove")) Result->SetNumberField(TEXT("removedChannel"), FromChannel);
	if (Op == TEXT("copy"))
	{
		Result->SetNumberField(TEXT("copiedFromChannel"), FromChannel);
		Result->SetNumberField(TEXT("copiedToChannel"), ToChannel);
		Result->SetBoolField(TEXT("overwroteExistingChannel"), bDestinationExisted);
	}

	// ── Rollback ─────────────────────────────────────────────────────────────
	//
	// Growing the channel list has an exact inverse: shrink it back, and the
	// channels that were added held nothing. Removing a channel, or copying over
	// one that already had data, destroys UVs that this record cannot bring
	// back, and the flags below say so rather than letting the caller read a
	// rollback descriptor as a promise it is not.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), Target.AssetPath);
	Payload->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
	Payload->SetStringField(TEXT("op"), TEXT("set"));
	Payload->SetNumberField(TEXT("channelCount"), PreviousCount);
	Payload->SetBoolField(TEXT("save"), bSave);
	MCPSetRollback(Result, TEXT("set_uv_channel_count"), Payload);

	const bool bLossy = (Op == TEXT("remove")) || (Op == TEXT("copy") && bDestinationExisted);
	Result->SetBoolField(TEXT("rollbackRestoresChannelCountOnly"), bLossy);
	if (bLossy)
	{
		Result->SetStringField(TEXT("rollbackNote"), Op == TEXT("remove")
			? FString::Printf(
				TEXT("The rollback record restores the channel COUNT (%d) but not the UVs that were in channel %d: ")
					TEXT("mesh edits are destructive and the removed coordinates are gone. Duplicate the asset ")
					TEXT("before a removal you may want to undo."),
				PreviousCount, FromChannel)
			: FString::Printf(
				TEXT("The rollback record restores the channel COUNT (%d) but not the UVs that channel %d held ")
					TEXT("before the copy: they were overwritten. Copy channel %d to a new channel first if you ")
					TEXT("need it back."),
				PreviousCount, ToChannel, ToChannel));
	}

	if (bSave)
	{
		FString SaveReason;
		const bool bSaved = SaveAssetPackageChecked(Target.Asset, SaveReason);
		MCPNoteSaveOutcome(Result, Target.AssetPath, bSaved, SaveReason);
	}
	else
	{
		Target.Asset->MarkPackageDirty();
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetStringField(TEXT("saveSkippedReason"),
			TEXT("save=false was requested, so the new channel layout is dirty in memory only and is lost when the ")
				TEXT("editor closes. Call asset(save) for it, or repeat with save=true."));
	}
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// asset(transform_uvs)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FAssetHandlers::TransformUvs(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPUvTarget Target;
	if (auto Err = MCPUvResolveTarget(Params, Target)) return Err;
	if (MCPIsProtectedAssetPath(Target.AssetPath)) return MCPProtectedPathError(Target.AssetPath);
	if (auto Blocked = MCPAssetWriteBlockedError(Target.Asset, Target.AssetPath, TEXT("transform UVs")))
	{
		return Blocked;
	}

	const int32 ChannelCount = MCPUvChannelCount(*Target.Desc);
	const int32 Channel = OptionalInt(Params, TEXT("channel"), 0);
	if (Channel < 0 || Channel >= ChannelCount)
	{
		return MCPUvBadChannel(Target.AssetPath, TEXT("channel"), Channel, ChannelCount, Target.LodIndex);
	}

	// ── The transform ────────────────────────────────────────────────────────
	FVector2D Translation(0.0, 0.0);
	const TSharedPtr<FJsonObject>* TranslateObj = nullptr;
	if (Params->TryGetObjectField(TEXT("translate"), TranslateObj) && TranslateObj)
	{
		Translation.X = OptionalNumber(*TranslateObj, TEXT("u"), 0.0);
		Translation.Y = OptionalNumber(*TranslateObj, TEXT("v"), 0.0);
	}

	FVector2D Scale(1.0, 1.0);
	const TSharedPtr<FJsonObject>* ScaleObj = nullptr;
	if (Params->TryGetObjectField(TEXT("scale"), ScaleObj) && ScaleObj)
	{
		Scale.X = OptionalNumber(*ScaleObj, TEXT("u"), 1.0);
		Scale.Y = OptionalNumber(*ScaleObj, TEXT("v"), 1.0);
	}
	if (FMath::IsNearlyZero(Scale.X) || FMath::IsNearlyZero(Scale.Y))
	{
		return MCPError(FString::Printf(
			TEXT("scale {u:%g, v:%g} collapses the UVs onto a line or a point, which destroys the channel and has ")
				TEXT("no inverse, so this call refuses rather than emitting a rollback record it cannot honour. ")
				TEXT("Use a non-zero scale on both axes."),
			Scale.X, Scale.Y));
	}

	const double RotationDegrees = OptionalNumber(Params, TEXT("rotate"), 0.0);

	FVector2D Origin(0.5, 0.5);
	const TSharedPtr<FJsonObject>* OriginObj = nullptr;
	if (Params->TryGetObjectField(TEXT("origin"), OriginObj) && OriginObj)
	{
		Origin.X = OptionalNumber(*OriginObj, TEXT("u"), 0.5);
		Origin.Y = OptionalNumber(*OriginObj, TEXT("v"), 0.5);
	}

	const bool bFlipU = OptionalBool(Params, TEXT("flipU"), false);
	const bool bFlipV = OptionalBool(Params, TEXT("flipV"), false);

	// Application order. The default is what a UV editor does; the inverse order
	// exists so the rollback record this call emits is exact rather than
	// approximately right.
	const FString Order = OptionalString(Params, TEXT("order"), TEXT("flipScaleRotateTranslate"));
	const bool bForward = Order.Equals(TEXT("flipScaleRotateTranslate"), ESearchCase::IgnoreCase);
	const bool bInverse = Order.Equals(TEXT("translateRotateScaleFlip"), ESearchCase::IgnoreCase);
	if (!bForward && !bInverse)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown order '%s'. Use flipScaleRotateTranslate (default) or translateRotateScaleFlip, which is ")
				TEXT("the order the rollback record for this action uses."),
			*Order));
	}

	const bool bIdentity = Translation.IsNearlyZero() && !bFlipU && !bFlipV
		&& FMath::IsNearlyEqual(Scale.X, 1.0) && FMath::IsNearlyEqual(Scale.Y, 1.0)
		&& FMath::IsNearlyZero(RotationDegrees);

	FMCPUvSelection Selection;
	if (auto Err = MCPUvParseSelection(Params, Target, *Target.Desc, Channel, Selection)) return Err;

	if (Selection.SelectedTriangles == 0)
	{
		return MCPError(FString::Printf(
			TEXT("The selection matched no triangles on LOD %d channel %d of '%s', so there was nothing to ")
				TEXT("transform. A transform that touches nothing is reported as an error rather than a success, ")
				TEXT("because a mistyped filter and a deliberate no-op look identical otherwise."),
			Target.LodIndex, Channel, *Target.AssetPath));
	}

	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);

	if (bIdentity)
	{
		auto NoOp = MCPSuccess();
		MCPUvWriteAssetHeader(NoOp, Target);
		MCPSetExisted(NoOp);
		NoOp->SetBoolField(TEXT("unchanged"), true);
		NoOp->SetNumberField(TEXT("channel"), Channel);
		NoOp->SetObjectField(TEXT("selection"), Selection.Report);
		NoOp->SetStringField(TEXT("note"),
			TEXT("Every component of the transform is the identity (no translation, unit scale, no rotation, no ")
				TEXT("flip), so no UV was written and the asset was not rebuilt."));
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Target.AssetPath);
		Payload->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
		Payload->SetNumberField(TEXT("channel"), Channel);
		MCPSetRollback(NoOp, TEXT("transform_uvs"), Payload);
		NoOp->SetBoolField(TEXT("rollbackIsNoOp"), true);
		return MCPResult(NoOp);
	}

	TVertexInstanceAttributesRef<FVector2f> UVs = MCPUvAttributes(*Target.Desc);
	if (!UVs.IsValid())
	{
		return MCPError(FString::Printf(
			TEXT("LOD %d of '%s' has no VertexInstance TextureCoordinate attribute to transform."),
			Target.LodIndex, *Target.AssetPath));
	}

	const double RotationRadians = FMath::DegreesToRadians(RotationDegrees);
	const double CosR = FMath::Cos(RotationRadians);
	const double SinR = FMath::Sin(RotationRadians);

	auto ApplyFlip = [&](FVector2D& Uv)
	{
		if (bFlipU) Uv.X = 2.0 * Origin.X - Uv.X;
		if (bFlipV) Uv.Y = 2.0 * Origin.Y - Uv.Y;
	};
	auto ApplyScale = [&](FVector2D& Uv)
	{
		Uv.X = (Uv.X - Origin.X) * Scale.X + Origin.X;
		Uv.Y = (Uv.Y - Origin.Y) * Scale.Y + Origin.Y;
	};
	auto ApplyRotate = [&](FVector2D& Uv)
	{
		const double X = Uv.X - Origin.X;
		const double Y = Uv.Y - Origin.Y;
		Uv.X = X * CosR - Y * SinR + Origin.X;
		Uv.Y = X * SinR + Y * CosR + Origin.Y;
	};
	auto ApplyTranslate = [&](FVector2D& Uv) { Uv += Translation; };

	// Collect the vertex instances once. A vertex instance shared by a selected
	// and an unselected triangle would otherwise be transformed twice, or once
	// and then read as unselected, depending on iteration order.
	TSet<int32> Instances;
	for (const FTriangleID TriangleID : Target.Desc->Triangles().GetElementIDs())
	{
		if (!Selection.TriangleMask[TriangleID.GetValue()]) continue;
		for (const FVertexInstanceID Instance : Target.Desc->GetTriangleVertexInstances(TriangleID))
		{
			Instances.Add(Instance.GetValue());
		}
	}

	if (bDryRun)
	{
		auto Preview = MCPSuccess();
		MCPUvWriteAssetHeader(Preview, Target);
		Preview->SetBoolField(TEXT("dryRun"), true);
		Preview->SetBoolField(TEXT("written"), false);
		Preview->SetNumberField(TEXT("channel"), Channel);
		Preview->SetObjectField(TEXT("selection"), Selection.Report);
		Preview->SetNumberField(TEXT("wouldTransformVertexInstances"), Instances.Num());
		return MCPResult(Preview);
	}

	for (const int32 RawInstance : Instances)
	{
		const FVertexInstanceID Instance(RawInstance);
		const FVector2f Existing = UVs.Get(Instance, Channel);
		FVector2D Uv(Existing.X, Existing.Y);
		if (bForward)
		{
			ApplyFlip(Uv); ApplyScale(Uv); ApplyRotate(Uv); ApplyTranslate(Uv);
		}
		else
		{
			ApplyTranslate(Uv); ApplyRotate(Uv); ApplyScale(Uv); ApplyFlip(Uv);
		}
		UVs.Set(Instance, Channel, FVector2f(static_cast<float>(Uv.X), static_cast<float>(Uv.Y)));
	}

	FString CommitError;
	if (!MCPUvCommit(Target, /*bRebuild*/ true, CommitError))
	{
		return MCPError(FString::Printf(
			TEXT("The UV transform could not be committed to '%s': %s"), *Target.AssetPath, *CommitError));
	}

	// Read the channel back so the caller can verify rather than trust.
	FMCPUvChannelStats After;
	After.Channel = Channel;
	MCPUvMeasure(*Target.Desc, UVs, Channel, After);
	MCPUvBuildIslands(*Target.Desc, UVs, Channel, After);

	auto Result = MCPSuccess();
	MCPUvWriteAssetHeader(Result, Target);
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetNumberField(TEXT("channel"), Channel);
	Result->SetNumberField(TEXT("transformedVertexInstances"), Instances.Num());
	Result->SetObjectField(TEXT("selection"), Selection.Report);
	Result->SetStringField(TEXT("order"), bForward ? TEXT("flipScaleRotateTranslate") : TEXT("translateRotateScaleFlip"));
	Result->SetObjectField(TEXT("channelAfter"), MCPUvChannelToJson(After, MCPUvLightmapChannel(Target)));

	// ── Rollback ─────────────────────────────────────────────────────────────
	//
	// An affine UV transform IS invertible, so this record is exact, not a
	// best effort: the same selection, the reciprocal scale, the negated
	// rotation and translation, the same flips about the same origin, applied in
	// the reverse order. Island indices are stable under an affine map because
	// the map preserves UV equality, so an island selection still names the same
	// triangles on the way back.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), Target.AssetPath);
	Payload->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
	Payload->SetNumberField(TEXT("channel"), Channel);
	TSharedPtr<FJsonObject> InverseTranslate = MakeShared<FJsonObject>();
	InverseTranslate->SetNumberField(TEXT("u"), -Translation.X);
	InverseTranslate->SetNumberField(TEXT("v"), -Translation.Y);
	Payload->SetObjectField(TEXT("translate"), InverseTranslate);
	TSharedPtr<FJsonObject> InverseScale = MakeShared<FJsonObject>();
	InverseScale->SetNumberField(TEXT("u"), 1.0 / Scale.X);
	InverseScale->SetNumberField(TEXT("v"), 1.0 / Scale.Y);
	Payload->SetObjectField(TEXT("scale"), InverseScale);
	Payload->SetNumberField(TEXT("rotate"), -RotationDegrees);
	TSharedPtr<FJsonObject> SameOrigin = MakeShared<FJsonObject>();
	SameOrigin->SetNumberField(TEXT("u"), Origin.X);
	SameOrigin->SetNumberField(TEXT("v"), Origin.Y);
	Payload->SetObjectField(TEXT("origin"), SameOrigin);
	Payload->SetBoolField(TEXT("flipU"), bFlipU);
	Payload->SetBoolField(TEXT("flipV"), bFlipV);
	Payload->SetStringField(TEXT("order"), bForward
		? TEXT("translateRotateScaleFlip") : TEXT("flipScaleRotateTranslate"));
	Payload->SetBoolField(TEXT("save"), bSave);
	if (const TSharedPtr<FJsonObject>* SelectionObj = nullptr;
		Params->TryGetObjectField(TEXT("selection"), SelectionObj) && SelectionObj)
	{
		Payload->SetObjectField(TEXT("selection"), *SelectionObj);
	}
	MCPSetRollback(Result, TEXT("transform_uvs"), Payload);
	Result->SetBoolField(TEXT("rollbackRestoresUvs"), true);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("This rollback is exact: the transform is affine and its inverse is replayed over the same selection. ")
			TEXT("Float rounding aside, the channel returns to the coordinates it held."));

	if (bSave)
	{
		FString SaveReason;
		const bool bSaved = SaveAssetPackageChecked(Target.Asset, SaveReason);
		MCPNoteSaveOutcome(Result, Target.AssetPath, bSaved, SaveReason);
	}
	else
	{
		Target.Asset->MarkPackageDirty();
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetStringField(TEXT("saveSkippedReason"),
			TEXT("save=false was requested, so the transformed UVs are dirty in memory only and are lost when the ")
				TEXT("editor closes. Call asset(save) for it, or repeat with save=true."));
	}
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// asset(unwrap_uvs)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FAssetHandlers::UnwrapUvs(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPUvTarget Target;
	if (auto Err = MCPUvResolveTarget(Params, Target)) return Err;
	if (MCPIsProtectedAssetPath(Target.AssetPath)) return MCPProtectedPathError(Target.AssetPath);
	if (auto Blocked = MCPAssetWriteBlockedError(Target.Asset, Target.AssetPath, TEXT("unwrap UVs")))
	{
		return Blocked;
	}

	const FString Method = OptionalString(Params, TEXT("method"), TEXT("xatlas")).ToLower();
	static const TCHAR* const KnownMethods =
		TEXT("xatlas, patchBuilder, expMap, conformal, spectralConformal, planar, box, cylinder");
	const bool bKnownMethod =
		Method == TEXT("xatlas") || Method == TEXT("patchbuilder") ||
		Method == TEXT("expmap") || Method == TEXT("conformal") || Method == TEXT("spectralconformal") ||
		Method == TEXT("planar") || Method == TEXT("box") || Method == TEXT("cylinder");
	if (!bKnownMethod)
	{
		return MCPError(FString::Printf(TEXT("Unknown unwrap method '%s'. Use one of: %s."), *Method, KnownMethods));
	}

	const int32 PreviousCount = MCPUvChannelCount(*Target.Desc);
	const int32 Channel = OptionalInt(Params, TEXT("channel"), 0);
	if (Channel < 0 || Channel >= MCPUvMaxChannels)
	{
		return MCPError(FString::Printf(
			TEXT("channel %d is not a usable UV channel index. Valid indices are 0..%d; a value at or above the ")
				TEXT("current count (%d) grows the channel list to fit."),
			Channel, MCPUvMaxChannels - 1, PreviousCount));
	}
	const bool bChannelExisted = Channel < PreviousCount;

	const bool bPack = OptionalBool(Params, TEXT("pack"), true);
	const int32 TextureResolution = FMath::Clamp(OptionalInt(Params, TEXT("textureResolution"), 1024), 16, 8192);
	const int32 MaxIterations = FMath::Clamp(OptionalInt(Params, TEXT("maxIterations"), 2), 1, 16);
	const int32 InitialPatchCount = FMath::Clamp(OptionalInt(Params, TEXT("initialPatchCount"), 100), 1, 10000);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bPreserveVertexOrder = OptionalBool(Params, TEXT("preserveVertexOrder"), true);
	const FTransform ProjectionTransform = OptionalTransform(Params, TEXT("projectionTransform"));
	const int32 BackupToChannel = OptionalInt(Params, TEXT("backupToChannel"), INDEX_NONE);

	if (BackupToChannel != INDEX_NONE
		&& (BackupToChannel < 0 || BackupToChannel >= MCPUvMaxChannels || BackupToChannel == Channel))
	{
		return MCPError(FString::Printf(
			TEXT("backupToChannel %d is not usable: it must be 0..%d and must differ from the channel being ")
				TEXT("unwrapped (%d)."),
			BackupToChannel, MCPUvMaxChannels - 1, Channel));
	}

	if (!MCPUvEnsureGeometryScripting())
	{
		return MCPUvGeometryScriptUnavailable(TEXT("its runtime classes are not registered in this editor."));
	}

	UClass* DynamicMeshClass = FindObject<UClass>(nullptr, MCPUvGSDynamicMeshClass);
	if (!DynamicMeshClass)
	{
		return MCPUvGeometryScriptUnavailable(TEXT("UDynamicMesh is not registered."));
	}

	const TCHAR* const CopyInName = Target.StaticMesh
		? TEXT("CopyMeshFromStaticMeshV2") : TEXT("CopyMeshFromSkeletalMesh");
	const TCHAR* const CopyOutName = Target.StaticMesh
		? TEXT("CopyMeshToStaticMesh") : TEXT("CopyMeshToSkeletalMesh");

	// Bind everything BEFORE anything is built, so an engine that is missing one
	// of these reports it without having already half-rewritten the asset.
	FString BindError;
	{
		FMCPUvScriptCall Probe;
		if (!Probe.Bind(MCPUvGSAssetFunctions, CopyInName, BindError))
		{
			return MCPUvGeometryScriptUnavailable(BindError);
		}
		if (!Probe.Bind(MCPUvGSAssetFunctions, CopyOutName, BindError))
		{
			return MCPUvGeometryScriptUnavailable(BindError);
		}
	}

	if (bDryRun)
	{
		auto Preview = MCPSuccess();
		MCPUvWriteAssetHeader(Preview, Target);
		Preview->SetBoolField(TEXT("dryRun"), true);
		Preview->SetBoolField(TEXT("written"), false);
		Preview->SetStringField(TEXT("method"), Method);
		Preview->SetNumberField(TEXT("channel"), Channel);
		Preview->SetBoolField(TEXT("channelExisted"), bChannelExisted);
		Preview->SetBoolField(TEXT("wouldPack"), bPack);
		Preview->SetStringField(TEXT("note"),
			TEXT("unwrap_uvs rewrites the whole LOD through Geometry Script, not only its UV channel. Vertex order ")
				TEXT("is preserved when preserveVertexOrder is true, but normals, tangents and the exact triangle ")
				TEXT("list come back through the conversion."));
		return MCPResult(Preview);
	}

	// ── Optional lossless backup of the destination channel ─────────────────
	bool bBackedUp = false;
	if (BackupToChannel != INDEX_NONE && bChannelExisted)
	{
		TVertexInstanceAttributesRef<FVector2f> UVs = MCPUvAttributes(*Target.Desc);
		if (UVs.IsValid())
		{
			if (BackupToChannel >= PreviousCount) UVs.SetNumChannels(BackupToChannel + 1);
			for (const FVertexInstanceID Instance : Target.Desc->VertexInstances().GetElementIDs())
			{
				UVs.Set(Instance, BackupToChannel, UVs.Get(Instance, Channel));
			}
			FString BackupCommitError;
			if (!MCPUvCommit(Target, /*bRebuild*/ false, BackupCommitError))
			{
				return MCPError(FString::Printf(
					TEXT("The pre-unwrap backup of channel %d into channel %d could not be committed to '%s': %s ")
						TEXT("Nothing was unwrapped."),
					Channel, BackupToChannel, *Target.AssetPath, *BackupCommitError));
			}
			bBackedUp = true;
		}
	}

	// ── Round trip through a DynamicMesh ────────────────────────────────────
	UObject* Dynamic = NewObject<UObject>(GetTransientPackage(), DynamicMeshClass);
	if (!Dynamic)
	{
		return MCPUvGeometryScriptUnavailable(TEXT("a UDynamicMesh could not be constructed."));
	}
	const FGCRootScope KeepDynamic(Dynamic);

	UObject* Debug = nullptr;
	if (UClass* DebugClass = FindObject<UClass>(nullptr, MCPUvGSDebugClass))
	{
		Debug = NewObject<UObject>(GetTransientPackage(), DebugClass);
	}
	const FGCRootScope KeepDebug(Debug);

	TArray<FString> Messages;
	const int64 LODTypeValue = MCPUvEnumValue(MCPUvGSLODTypeEnum, TEXT("SourceModel"));

	{
		FMCPUvScriptCall CopyIn;
		if (!CopyIn.Bind(MCPUvGSAssetFunctions, CopyInName, BindError))
		{
			return MCPUvGeometryScriptUnavailable(BindError);
		}
		CopyIn.SetObject(Target.StaticMesh ? TEXT("FromStaticMeshAsset") : TEXT("FromSkeletalMeshAsset"), Target.Asset);
		CopyIn.SetObject(TEXT("ToDynamicMesh"), Dynamic);
		CopyIn.SetStructEnum(TEXT("RequestedLOD"), TEXT("LODType"), LODTypeValue);
		CopyIn.SetStructNumber(TEXT("RequestedLOD"), TEXT("LODIndex"), Target.LodIndex);
		// Build settings are NOT applied on the way in: they would bake the
		// lightmap generation the caller is about to replace.
		CopyIn.SetStructBool(TEXT("AssetOptions"), TEXT("bApplyBuildSettings"), false);
		if (Target.StaticMesh) CopyIn.SetBool(TEXT("bUseSectionMaterials"), true);
		CopyIn.SetObject(TEXT("Debug"), Debug);
		CopyIn.Invoke();
		Messages.Append(MCPUvDrainDebug(Debug));

		if (CopyIn.GetEnumName(TEXT("Outcome")) != TEXT("Success"))
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetBoolField(TEXT("success"), false);
			Obj->SetStringField(TEXT("error"), FString::Printf(
				TEXT("LOD %d of '%s' could not be read into a DynamicMesh, so nothing was unwrapped and the asset ")
					TEXT("is untouched."),
				Target.LodIndex, *Target.AssetPath));
			Obj->SetStringField(TEXT("reason"), TEXT("source_read_failed"));
			MCPUvAttachMessages(Obj, Messages);
			return MakeShared<FJsonValueObject>(Obj);
		}
	}

	// Make sure the destination channel exists on the dynamic mesh.
	{
		FMCPUvScriptCall SetCount;
		if (SetCount.Bind(MCPUvGSUVFunctions, TEXT("SetNumUVSets"), BindError))
		{
			SetCount.SetObject(TEXT("TargetMesh"), Dynamic);
			SetCount.SetNumber(TEXT("NumUVSets"), FMath::Max(PreviousCount, Channel + 1));
			SetCount.SetObject(TEXT("Debug"), Debug);
			SetCount.Invoke();
			Messages.Append(MCPUvDrainDebug(Debug));
		}
	}

	// ── The unwrap itself ───────────────────────────────────────────────────
	auto UnwrapFailed = [&](const FString& Detail) -> TSharedPtr<FJsonValue>
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("The '%s' unwrap of LOD %d channel %d on '%s' failed: %s Nothing was written back."),
			*Method, Target.LodIndex, Channel, *Target.AssetPath, *Detail));
		Obj->SetStringField(TEXT("reason"), TEXT("unwrap_failed"));
		Obj->SetStringField(TEXT("method"), Method);
		MCPUvAttachMessages(Obj, Messages);
		return MakeShared<FJsonValueObject>(Obj);
	};

	{
		FMCPUvScriptCall Unwrap;
		if (Method == TEXT("xatlas"))
		{
			if (!Unwrap.Bind(MCPUvGSUVFunctions, TEXT("AutoGenerateXAtlasMeshUVs"), BindError))
			{
				return MCPUvGeometryScriptUnavailable(BindError);
			}
			Unwrap.SetObject(TEXT("TargetMesh"), Dynamic);
			Unwrap.SetNumber(TEXT("UVSetIndex"), Channel);
			Unwrap.SetStructNumber(TEXT("Options"), TEXT("MaxIterations"), MaxIterations);
		}
		else if (Method == TEXT("patchbuilder"))
		{
			if (!Unwrap.Bind(MCPUvGSUVFunctions, TEXT("AutoGeneratePatchBuilderMeshUVs"), BindError))
			{
				return MCPUvGeometryScriptUnavailable(BindError);
			}
			Unwrap.SetObject(TEXT("TargetMesh"), Dynamic);
			Unwrap.SetNumber(TEXT("UVSetIndex"), Channel);
			Unwrap.SetStructNumber(TEXT("Options"), TEXT("InitialPatchCount"), InitialPatchCount);
			Unwrap.SetStructBool(TEXT("Options"), TEXT("bAutoPack"), bPack);
			Unwrap.SetNestedStructNumber(TEXT("Options"), TEXT("PackingOptions"), TEXT("TargetImageWidth"), TextureResolution);
		}
		else if (Method == TEXT("expmap") || Method == TEXT("conformal") || Method == TEXT("spectralconformal"))
		{
			if (!Unwrap.Bind(MCPUvGSUVFunctions, TEXT("RecomputeMeshUVs"), BindError))
			{
				return MCPUvGeometryScriptUnavailable(BindError);
			}
			const FString Enumerator =
				Method == TEXT("expmap") ? TEXT("ExpMap") :
				Method == TEXT("conformal") ? TEXT("Conformal") : TEXT("SpectralConformal");
			const int64 MethodValue = MCPUvEnumValue(MCPUvGSFlattenMethodEnum, Enumerator);
			if (MethodValue == INDEX_NONE)
			{
				return MCPUvGeometryScriptUnavailable(FString::Printf(
					TEXT("EGeometryScriptUVFlattenMethod has no '%s' in this engine build."), *Enumerator));
			}
			const FString IslandSource = OptionalString(Params, TEXT("islandSource"), TEXT("UVIslands"));
			const int64 IslandValue = MCPUvEnumValue(MCPUvGSIslandSourceEnum,
				IslandSource.Equals(TEXT("PolyGroups"), ESearchCase::IgnoreCase) ? TEXT("PolyGroups") : TEXT("UVIslands"));
			Unwrap.SetObject(TEXT("TargetMesh"), Dynamic);
			Unwrap.SetNumber(TEXT("UVSetIndex"), Channel);
			Unwrap.SetStructEnum(TEXT("Options"), TEXT("Method"), MethodValue);
			Unwrap.SetStructEnum(TEXT("Options"), TEXT("IslandSource"), IslandValue);
		}
		else
		{
			const TCHAR* const Function =
				Method == TEXT("planar") ? TEXT("SetMeshUVsFromPlanarProjection") :
				Method == TEXT("box") ? TEXT("SetMeshUVsFromBoxProjection") :
				TEXT("SetMeshUVsFromCylinderProjection");
			if (!Unwrap.Bind(MCPUvGSUVFunctions, Function, BindError))
			{
				return MCPUvGeometryScriptUnavailable(BindError);
			}
			Unwrap.SetObject(TEXT("TargetMesh"), Dynamic);
			Unwrap.SetNumber(TEXT("UVSetIndex"), Channel);
			// One of PlaneTransform / BoxTransform / CylinderTransform is present
			// depending on the function; setting all three by name is how this
			// stays one branch instead of three.
			Unwrap.SetTransform(TEXT("PlaneTransform"), ProjectionTransform);
			Unwrap.SetTransform(TEXT("BoxTransform"), ProjectionTransform);
			Unwrap.SetTransform(TEXT("CylinderTransform"), ProjectionTransform);
		}
		Unwrap.SetObject(TEXT("Debug"), Debug);
		Unwrap.Invoke();
		Messages.Append(MCPUvDrainDebug(Debug));
	}

	// ── Pack ────────────────────────────────────────────────────────────────
	bool bPacked = false;
	if (bPack && Method != TEXT("patchbuilder"))
	{
		FMCPUvScriptCall Repack;
		if (Repack.Bind(MCPUvGSUVFunctions, TEXT("RepackMeshUVs"), BindError))
		{
			Repack.SetObject(TEXT("TargetMesh"), Dynamic);
			Repack.SetNumber(TEXT("UVSetIndex"), Channel);
			Repack.SetStructNumber(TEXT("RepackOptions"), TEXT("TargetImageWidth"), TextureResolution);
			Repack.SetStructBool(TEXT("RepackOptions"), TEXT("bOptimizeIslandRotation"), true);
			Repack.SetObject(TEXT("Debug"), Debug);
			Repack.Invoke();
			Messages.Append(MCPUvDrainDebug(Debug));
			bPacked = true;
		}
		else
		{
			Messages.Add(FString::Printf(
				TEXT("Island packing was requested but RepackMeshUVs could not be bound (%s). The UVs were ")
					TEXT("generated and written, unpacked."),
				*BindError));
		}
	}
	else if (bPack)
	{
		bPacked = true; // patchBuilder packs inside its own options.
	}

	// ── Write back ──────────────────────────────────────────────────────────
	{
		FMCPUvScriptCall CopyOut;
		if (!CopyOut.Bind(MCPUvGSAssetFunctions, CopyOutName, BindError))
		{
			return MCPUvGeometryScriptUnavailable(BindError);
		}
		CopyOut.SetObject(TEXT("FromDynamicMesh"), Dynamic);
		CopyOut.SetObject(Target.StaticMesh ? TEXT("ToStaticMeshAsset") : TEXT("ToSkeletalMeshAsset"), Target.Asset);
		CopyOut.SetStructBool(TEXT("Options"), TEXT("bEnableRecomputeNormals"), false);
		CopyOut.SetStructBool(TEXT("Options"), TEXT("bEnableRecomputeTangents"), false);
		CopyOut.SetStructBool(TEXT("Options"), TEXT("bEnableRemoveDegenerates"), false);
		CopyOut.SetStructBool(TEXT("Options"), TEXT("bUseOriginalVertexOrder"), bPreserveVertexOrder);
		// A rebuild that regenerates lightmap UVs into the channel just unwrapped
		// would silently undo this whole call, so it is turned off when the two
		// collide and left alone otherwise.
		const bool bCollidesWithLightmap = Target.StaticMesh
			&& Target.StaticMesh->GetNumSourceModels() > Target.LodIndex
			&& Target.StaticMesh->GetSourceModel(Target.LodIndex).BuildSettings.bGenerateLightmapUVs
			&& Target.StaticMesh->GetSourceModel(Target.LodIndex).BuildSettings.DstLightmapIndex == Channel;
		if (bCollidesWithLightmap)
		{
			const int64 NoLightmap = MCPUvEnumValue(MCPUvGSLightmapOptionEnum, TEXT("DoNotGenerateLightmapUVs"));
			CopyOut.SetStructEnum(TEXT("Options"), TEXT("GenerateLightmapUVs"), NoLightmap);
			Messages.Add(FString::Printf(
				TEXT("Lightmap UV generation writes to channel %d, the channel this unwrap targets, so it was ")
					TEXT("suppressed for this write. Re-run asset(generate_lightmap_uvs) with a different ")
					TEXT("destination channel if you want both."),
				Channel));
		}
		CopyOut.SetStructNumber(TEXT("TargetLOD"), TEXT("LODIndex"), Target.LodIndex);
		if (Target.StaticMesh) CopyOut.SetBool(TEXT("bUseSectionMaterials"), true);
		CopyOut.SetObject(TEXT("Debug"), Debug);
		CopyOut.Invoke();
		Messages.Append(MCPUvDrainDebug(Debug));

		if (CopyOut.GetEnumName(TEXT("Outcome")) != TEXT("Success"))
		{
			return UnwrapFailed(TEXT("the unwrapped mesh could not be written back to the asset."));
		}
	}

	// ── Read the result back ────────────────────────────────────────────────
	FMCPUvTarget After;
	{
		TSharedPtr<FJsonObject> Reread = MakeShared<FJsonObject>();
		Reread->SetStringField(TEXT("assetPath"), Target.AssetPath);
		Reread->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
		if (auto Err = MCPUvResolveTarget(Reread, After)) return Err;
	}

	FMCPUvChannelStats Stats;
	Stats.Channel = Channel;
	const int32 NewCount = MCPUvChannelCount(*After.Desc);
	if (Channel < NewCount)
	{
		TVertexInstanceAttributesRef<FVector2f> UVs = MCPUvAttributes(*After.Desc);
		MCPUvMeasure(*After.Desc, UVs, Channel, Stats);
		MCPUvBuildIslands(*After.Desc, UVs, Channel, Stats);
		MCPUvRasterise(*After.Desc, UVs, Channel, MCPUvClampRasterSize(Params), Stats, nullptr, nullptr);
	}

	auto Result = MCPSuccess();
	MCPUvWriteAssetHeader(Result, After);
	if (bChannelExisted) MCPSetUpdated(Result); else MCPSetCreated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetStringField(TEXT("method"), Method);
	Result->SetNumberField(TEXT("channel"), Channel);
	Result->SetBoolField(TEXT("channelExisted"), bChannelExisted);
	Result->SetNumberField(TEXT("previousChannelCount"), PreviousCount);
	Result->SetBoolField(TEXT("packed"), bPacked);
	Result->SetNumberField(TEXT("textureResolution"), TextureResolution);
	Result->SetBoolField(TEXT("geometryRewritten"), true);
	Result->SetStringField(TEXT("geometryNote"),
		TEXT("unwrap_uvs converts the LOD to a DynamicMesh and back, so the whole LOD is rewritten, not only its UV ")
			TEXT("channel. Vertex order is preserved by default; normals and tangents come back through the ")
			TEXT("conversion. Verify with the channel report below and asset(get_mesh_info)."));
	if (Channel < NewCount)
	{
		Result->SetObjectField(TEXT("channelAfter"), MCPUvChannelToJson(Stats, MCPUvLightmapChannel(After)));
	}
	MCPUvAttachMessages(Result, Messages);

	// ── Rollback ─────────────────────────────────────────────────────────────
	if (bBackedUp)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Target.AssetPath);
		Payload->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
		Payload->SetStringField(TEXT("op"), TEXT("copy"));
		Payload->SetNumberField(TEXT("fromChannel"), BackupToChannel);
		Payload->SetNumberField(TEXT("toChannel"), Channel);
		Payload->SetBoolField(TEXT("save"), bSave);
		MCPSetRollback(Result, TEXT("set_uv_channel_count"), Payload);
		Result->SetBoolField(TEXT("rollbackRestoresUvs"), true);
		Result->SetNumberField(TEXT("backupChannel"), BackupToChannel);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("Channel %d was copied to channel %d before the unwrap, so the rollback restores the previous UVs ")
				TEXT("exactly. The backup channel %d stays on the mesh; remove it with ")
				TEXT("asset(set_uv_channel_count) op='remove' when you no longer want it. The LOD geometry itself ")
				TEXT("was rewritten by the conversion and is NOT restored."),
			Channel, BackupToChannel, BackupToChannel));
	}
	else
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Target.AssetPath);
		Payload->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
		Payload->SetStringField(TEXT("op"), TEXT("set"));
		Payload->SetNumberField(TEXT("channelCount"), FMath::Max(PreviousCount, 1));
		Payload->SetBoolField(TEXT("save"), bSave);
		MCPSetRollback(Result, TEXT("set_uv_channel_count"), Payload);
		Result->SetBoolField(TEXT("rollbackRestoresUvs"), false);
		Result->SetBoolField(TEXT("rollbackRestoresChannelCountOnly"), true);
		Result->SetStringField(TEXT("rollbackNote"), bChannelExisted
			? FString::Printf(
				TEXT("The rollback record restores the channel COUNT (%d) and NOTHING ELSE. Channel %d already held ")
					TEXT("UVs and they were overwritten, and the LOD geometry was rewritten by the conversion; ")
					TEXT("neither can be recovered from this record. Pass backupToChannel next time for a rollback ")
					TEXT("that restores the UVs, or duplicate the asset first."),
				PreviousCount, Channel)
			: FString::Printf(
				TEXT("The rollback record restores the channel COUNT (%d), which removes the channel this call ")
					TEXT("added. The LOD geometry was rewritten by the DynamicMesh conversion and is NOT restored ")
					TEXT("by it."),
				PreviousCount));
	}

	if (bSave)
	{
		FString SaveReason;
		const bool bSaved = SaveAssetPackageChecked(After.Asset, SaveReason);
		MCPNoteSaveOutcome(Result, Target.AssetPath, bSaved, SaveReason);
	}
	else
	{
		After.Asset->MarkPackageDirty();
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetStringField(TEXT("saveSkippedReason"),
			TEXT("save=false was requested, so the unwrap is dirty in memory only and is lost when the editor ")
				TEXT("closes. Call asset(save) for it, or repeat with save=true."));
	}
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// asset(generate_lightmap_uvs)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FAssetHandlers::GenerateLightmapUvs(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPUvTarget Target;
	if (auto Err = MCPUvResolveTarget(Params, Target)) return Err;
	if (MCPIsProtectedAssetPath(Target.AssetPath)) return MCPProtectedPathError(Target.AssetPath);

	if (!Target.StaticMesh)
	{
		return MCPUvUnsupported(Target.AssetPath, TEXT("lightmap_uvs_static_mesh_only"), FString::Printf(
			TEXT("'%s' is a %s. Lightmap UV generation is a StaticMesh build step: it lives in ")
				TEXT("FMeshBuildSettings and UStaticMesh::LightMapCoordinateIndex, neither of which a SkeletalMesh ")
				TEXT("has. Skeletal meshes are lit dynamically and need no lightmap channel."),
			*Target.AssetPath, *Target.Asset->GetClass()->GetName()));
	}
	if (auto Blocked = MCPAssetWriteBlockedError(Target.Asset, Target.AssetPath, TEXT("generate lightmap UVs")))
	{
		return Blocked;
	}

#if WITH_EDITORONLY_DATA
	UStaticMesh* Mesh = Target.StaticMesh;
	if (Mesh->GetNumSourceModels() <= Target.LodIndex)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' has %d source model(s), so LOD %d has no build settings to apply."),
			*Target.AssetPath, Mesh->GetNumSourceModels(), Target.LodIndex));
	}

	const int32 ChannelCount = MCPUvChannelCount(*Target.Desc);
	const FMeshBuildSettings Previous = Mesh->GetSourceModel(Target.LodIndex).BuildSettings;
	const int32 PreviousCoordinateIndex = Mesh->GetLightMapCoordinateIndex();
	const int32 PreviousResolution = Mesh->GetLightMapResolution();

	const bool bEnable = OptionalBool(Params, TEXT("enable"), true);
	const int32 SourceChannel = OptionalInt(Params, TEXT("sourceChannel"), Previous.SrcLightmapIndex);
	const int32 DestinationChannel = OptionalInt(Params, TEXT("destinationChannel"),
		Previous.DstLightmapIndex > 0 ? Previous.DstLightmapIndex : FMath::Max(ChannelCount, 1));
	const int32 MinResolution = FMath::Clamp(
		OptionalInt(Params, TEXT("minLightmapResolution"), Previous.MinLightmapResolution > 0 ? Previous.MinLightmapResolution : 64),
		4, 4096);
	const int32 LightmapResolution = FMath::Clamp(
		OptionalInt(Params, TEXT("lightmapResolution"), PreviousResolution > 0 ? PreviousResolution : MinResolution),
		4, 4096);
	const bool bSetCoordinateIndex = OptionalBool(Params, TEXT("setLightmapCoordinateIndex"), true);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);

	if (SourceChannel < 0 || SourceChannel >= FMath::Max(ChannelCount, 1))
	{
		return MCPUvBadChannel(Target.AssetPath, TEXT("sourceChannel"), SourceChannel, ChannelCount, Target.LodIndex);
	}
	if (DestinationChannel < 0 || DestinationChannel >= MCPUvMaxChannels)
	{
		return MCPError(FString::Printf(
			TEXT("destinationChannel %d is not a usable UV channel index. Valid indices are 0..%d. The build ")
				TEXT("creates the channel if it does not exist yet; LOD %d currently has %d."),
			DestinationChannel, MCPUvMaxChannels - 1, Target.LodIndex, ChannelCount));
	}
	if (DestinationChannel == SourceChannel)
	{
		return MCPError(FString::Printf(
			TEXT("sourceChannel and destinationChannel are both %d. The lightmap build reads the source and writes ")
				TEXT("the destination, so pointing them at one channel destroys the input it is unwrapping from."),
			SourceChannel));
	}

	const bool bSettingsAlreadyMatch =
		(Previous.bGenerateLightmapUVs != 0) == bEnable
		&& Previous.SrcLightmapIndex == SourceChannel
		&& Previous.DstLightmapIndex == DestinationChannel
		&& Previous.MinLightmapResolution == MinResolution
		&& (!bSetCoordinateIndex || PreviousCoordinateIndex == DestinationChannel)
		&& PreviousResolution == LightmapResolution;
	const bool bChannelAlreadyPresent = DestinationChannel < ChannelCount;

	auto DescribeOutcome = [&](const TSharedPtr<FJsonObject>& Out, const FMCPUvTarget& State)
	{
		MCPUvWriteAssetHeader(Out, State);
		Out->SetBoolField(TEXT("enabled"), bEnable);
		Out->SetNumberField(TEXT("sourceChannel"), SourceChannel);
		Out->SetNumberField(TEXT("destinationChannel"), DestinationChannel);
		Out->SetNumberField(TEXT("minLightmapResolution"), MinResolution);
		Out->SetNumberField(TEXT("lightmapResolution"), LightmapResolution);
		Out->SetBoolField(TEXT("setLightmapCoordinateIndex"), bSetCoordinateIndex);
		Out->SetNumberField(TEXT("previousLightmapCoordinateIndex"), PreviousCoordinateIndex);
		Out->SetNumberField(TEXT("previousLightmapResolution"), PreviousResolution);
		Out->SetNumberField(TEXT("previousDestinationChannel"), Previous.DstLightmapIndex);
		Out->SetBoolField(TEXT("previouslyEnabled"), Previous.bGenerateLightmapUVs != 0);
	};

	if (bSettingsAlreadyMatch && bChannelAlreadyPresent && !bDryRun
		&& OptionalBool(Params, TEXT("force"), false) == false)
	{
		// Idempotent replay. The settings are the ones asked for AND the channel
		// the build would produce is already there, so a rebuild would burn
		// minutes to arrive where the asset already is.
		FMCPUvChannelStats Stats;
		Stats.Channel = DestinationChannel;
		TVertexInstanceAttributesRef<FVector2f> UVs = MCPUvAttributes(*Target.Desc);
		MCPUvMeasure(*Target.Desc, UVs, DestinationChannel, Stats);
		MCPUvBuildIslands(*Target.Desc, UVs, DestinationChannel, Stats);
		MCPUvRasterise(*Target.Desc, UVs, DestinationChannel, MCPUvClampRasterSize(Params), Stats, nullptr, nullptr);

		auto Existing = MCPSuccess();
		DescribeOutcome(Existing, Target);
		MCPSetExisted(Existing);
		Existing->SetBoolField(TEXT("unchanged"), true);
		Existing->SetBoolField(TEXT("rebuilt"), false);
		Existing->SetObjectField(TEXT("lightmapChannel"), MCPUvChannelToJson(Stats, DestinationChannel));
		Existing->SetStringField(TEXT("note"), FString::Printf(
			TEXT("'%s' already has exactly these lightmap settings and channel %d already exists, so no rebuild ")
				TEXT("was run. Pass force=true to rebuild anyway."),
			*Target.AssetPath, DestinationChannel));
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Target.AssetPath);
		Payload->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
		Payload->SetBoolField(TEXT("enable"), Previous.bGenerateLightmapUVs != 0);
		Payload->SetNumberField(TEXT("sourceChannel"), Previous.SrcLightmapIndex);
		Payload->SetNumberField(TEXT("destinationChannel"), Previous.DstLightmapIndex);
		Payload->SetNumberField(TEXT("minLightmapResolution"), Previous.MinLightmapResolution);
		Payload->SetNumberField(TEXT("lightmapResolution"), PreviousResolution);
		MCPSetRollback(Existing, TEXT("generate_lightmap_uvs"), Payload);
		Existing->SetBoolField(TEXT("rollbackIsNoOp"), true);
		return MCPResult(Existing);
	}

	if (bDryRun)
	{
		auto Preview = MCPSuccess();
		DescribeOutcome(Preview, Target);
		Preview->SetBoolField(TEXT("dryRun"), true);
		Preview->SetBoolField(TEXT("written"), false);
		Preview->SetBoolField(TEXT("rebuilt"), false);
		Preview->SetBoolField(TEXT("destinationChannelExists"), bChannelAlreadyPresent);
		Preview->SetStringField(TEXT("note"),
			TEXT("These settings are UPROPERTYs and writing them does nothing until UStaticMesh::Build runs. This ")
				TEXT("action applies them AND rebuilds; the dry run reports what it would apply."));
		return MCPResult(Preview);
	}

	// ── Apply ────────────────────────────────────────────────────────────────
	Mesh->Modify();
	FMeshBuildSettings& Build = Mesh->GetSourceModel(Target.LodIndex).BuildSettings;
	Build.bGenerateLightmapUVs = bEnable;
	Build.SrcLightmapIndex = SourceChannel;
	Build.DstLightmapIndex = DestinationChannel;
	Build.MinLightmapResolution = MinResolution;
	if (bSetCoordinateIndex) Mesh->SetLightMapCoordinateIndex(DestinationChannel);
	Mesh->SetLightMapResolution(LightmapResolution);

	// ── Rebuild. This is the half a setter cannot do. ───────────────────────
	TArray<FText> BuildErrors;
	Mesh->Build(/*bInSilent*/ true, &BuildErrors);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	// ── Read back ────────────────────────────────────────────────────────────
	FMCPUvTarget After;
	{
		TSharedPtr<FJsonObject> Reread = MakeShared<FJsonObject>();
		Reread->SetStringField(TEXT("assetPath"), Target.AssetPath);
		Reread->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
		if (auto Err = MCPUvResolveTarget(Reread, After)) return Err;
	}

	const int32 NewChannelCount = MCPUvChannelCount(*After.Desc);
	FMCPUvChannelStats Stats;
	Stats.Channel = DestinationChannel;
	bool bChannelProduced = DestinationChannel < NewChannelCount;
	if (bChannelProduced)
	{
		TVertexInstanceAttributesRef<FVector2f> UVs = MCPUvAttributes(*After.Desc);
		MCPUvMeasure(*After.Desc, UVs, DestinationChannel, Stats);
		MCPUvBuildIslands(*After.Desc, UVs, DestinationChannel, Stats);
		MCPUvRasterise(*After.Desc, UVs, DestinationChannel, MCPUvClampRasterSize(Params), Stats, nullptr, nullptr);
	}

	auto Result = MCPSuccess();
	DescribeOutcome(Result, After);
	if (bChannelAlreadyPresent) MCPSetUpdated(Result); else MCPSetCreated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetBoolField(TEXT("rebuilt"), true);
	Result->SetBoolField(TEXT("destinationChannelProduced"), bChannelProduced);
	Result->SetNumberField(TEXT("resultingLightmapCoordinateIndex"), After.StaticMesh->GetLightMapCoordinateIndex());
	Result->SetNumberField(TEXT("resultingLightmapResolution"), After.StaticMesh->GetLightMapResolution());
	if (bChannelProduced)
	{
		Result->SetObjectField(TEXT("lightmapChannel"),
			MCPUvChannelToJson(Stats, After.StaticMesh->GetLightMapCoordinateIndex()));
	}
	if (BuildErrors.Num() > 0)
	{
		TArray<FString> AsStrings;
		for (const FText& Error : BuildErrors) AsStrings.Add(Error.ToString());
		Result->SetArrayField(TEXT("buildMessages"), MCPStringListToJson(AsStrings));
	}
	if (bEnable && !bChannelProduced)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("The settings were applied and '%s' was rebuilt, but LOD %d still has only %d UV channel(s), so ")
				TEXT("channel %d was not produced. The source channel %d may be empty or degenerate; check it with ")
				TEXT("asset(read_uv_channels)."),
			*Target.AssetPath, Target.LodIndex, NewChannelCount, DestinationChannel, SourceChannel));
		Result->SetStringField(TEXT("reason"), TEXT("lightmap_channel_not_produced"));
	}

	// ── Rollback ─────────────────────────────────────────────────────────────
	//
	// The SETTINGS have an exact inverse and it is replayed here. The generated
	// coordinates do not: the rebuild that produced them cannot be un-run, and
	// if the destination overwrote an existing channel that channel is gone.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), Target.AssetPath);
	Payload->SetNumberField(TEXT("lodIndex"), Target.LodIndex);
	Payload->SetBoolField(TEXT("enable"), Previous.bGenerateLightmapUVs != 0);
	Payload->SetNumberField(TEXT("sourceChannel"), Previous.SrcLightmapIndex);
	Payload->SetNumberField(TEXT("destinationChannel"), Previous.DstLightmapIndex);
	Payload->SetNumberField(TEXT("minLightmapResolution"), Previous.MinLightmapResolution);
	Payload->SetNumberField(TEXT("lightmapResolution"), PreviousResolution);
	Payload->SetBoolField(TEXT("setLightmapCoordinateIndex"), true);
	Payload->SetBoolField(TEXT("force"), true);
	Payload->SetBoolField(TEXT("save"), bSave);
	MCPSetRollback(Result, TEXT("generate_lightmap_uvs"), Payload);
	Result->SetBoolField(TEXT("rollbackRestoresSettingsOnly"), true);
	Result->SetBoolField(TEXT("rollbackRestoresUvs"), false);
	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("The rollback record restores the build SETTINGS (bGenerateLightmapUVs=%s, src=%d, dst=%d, ")
			TEXT("minResolution=%d, LightMapCoordinateIndex=%d, LightMapResolution=%d) and rebuilds with them. It ")
			TEXT("does NOT restore the coordinates that channel %d held before this call: the build overwrote ")
			TEXT("them and a rebuild cannot recover them. Copy that channel elsewhere with ")
			TEXT("asset(set_uv_channel_count) op='copy' first if you need it back."),
		Previous.bGenerateLightmapUVs ? TEXT("true") : TEXT("false"),
		Previous.SrcLightmapIndex, Previous.DstLightmapIndex, Previous.MinLightmapResolution,
		PreviousCoordinateIndex, PreviousResolution, DestinationChannel));

	if (bSave)
	{
		FString SaveReason;
		const bool bSaved = SaveAssetPackageChecked(After.Asset, SaveReason);
		MCPNoteSaveOutcome(Result, Target.AssetPath, bSaved, SaveReason);
	}
	else
	{
		After.Asset->MarkPackageDirty();
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetStringField(TEXT("saveSkippedReason"),
			TEXT("save=false was requested, so the rebuilt lightmap UVs are dirty in memory only and are lost when ")
				TEXT("the editor closes. Call asset(save) for it, or repeat with save=true."));
	}
	return MCPResult(Result);
#else
	return MCPUvUnsupported(Target.AssetPath, TEXT("editor_only"),
		TEXT("Lightmap UV generation needs editor-only mesh build data, which this build does not have."));
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// asset(export_uv_layout)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FAssetHandlers::ExportUvLayout(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPUvTarget Target;
	if (auto Err = MCPUvResolveTarget(Params, Target)) return Err;

	const int32 ChannelCount = MCPUvChannelCount(*Target.Desc);
	const int32 Channel = OptionalInt(Params, TEXT("channel"), 0);
	if (Channel < 0 || Channel >= ChannelCount)
	{
		return MCPUvBadChannel(Target.AssetPath, TEXT("channel"), Channel, ChannelCount, Target.LodIndex);
	}

	const int32 Size = FMath::Clamp(
		OptionalInt(Params, TEXT("imageSize"), MCPUvDefaultImageSize), 64, MCPUvMaxImageSize);
	const bool bShowIslands = OptionalBool(Params, TEXT("showIslands"), true);
	const bool bShowOverlaps = OptionalBool(Params, TEXT("showOverlaps"), true);
	const bool bShowGrid = OptionalBool(Params, TEXT("showGrid"), true);

	FString OutputPath = OptionalString(Params, TEXT("outputPath"));
	if (OutputPath.IsEmpty())
	{
		const FMCPAssetPathForms Forms = MCPAssetPathForms(Target.AssetPath);
		OutputPath = FPaths::Combine(
			FPaths::ProjectSavedDir(), TEXT("UVLayouts"),
			FString::Printf(TEXT("%s_LOD%d_UV%d.png"),
				Forms.AssetName.IsEmpty() ? TEXT("Mesh") : *Forms.AssetName, Target.LodIndex, Channel));
	}
	else if (FPaths::IsRelative(OutputPath))
	{
		OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UVLayouts"), OutputPath);
	}
	if (!OutputPath.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
	{
		OutputPath += TEXT(".png");
	}

	TVertexInstanceAttributesRef<FVector2f> UVs = MCPUvAttributes(*Target.Desc);
	if (!UVs.IsValid())
	{
		return MCPError(FString::Printf(
			TEXT("LOD %d of '%s' has no UV attribute to draw."), Target.LodIndex, *Target.AssetPath));
	}

	FMCPUvChannelStats Stats;
	Stats.Channel = Channel;
	MCPUvMeasure(*Target.Desc, UVs, Channel, Stats);
	MCPUvBuildIslands(*Target.Desc, UVs, Channel, Stats);

	TArray<int32> OwnerGrid;
	TBitArray<> OverlapGrid;
	MCPUvRasterise(*Target.Desc, UVs, Channel, Size, Stats, &OwnerGrid, &OverlapGrid);

	// ── Draw ─────────────────────────────────────────────────────────────────
	TArray<FColor> Pixels;
	Pixels.Init(FColor(24, 24, 28, 255), Size * Size);

	if (bShowGrid)
	{
		const FColor Grid(56, 56, 64, 255);
		for (int32 Step = 1; Step < 4; ++Step)
		{
			const int32 Line = (Size * Step) / 4;
			for (int32 Index = 0; Index < Size; ++Index)
			{
				MCPUvBlendPixel(Pixels, Size, Line, Index, Grid, 1.0f);
				MCPUvBlendPixel(Pixels, Size, Index, Line, Grid, 1.0f);
			}
		}
	}

	// Island fill, read straight off the rasteriser's ownership grid so the
	// picture and the overlap number in the report describe the same thing.
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const int32 Pixel = Y * Size + X;
			const int32 Owner = OwnerGrid[Pixel];
			if (Owner == INDEX_NONE) continue;
			// The rasteriser works in UV space (V up); the image is V down, so
			// every write below flips the row.
			if (bShowOverlaps && OverlapGrid[Pixel])
			{
				MCPUvBlendPixel(Pixels, Size, X, Size - 1 - Y, FColor(235, 64, 52, 255), 0.85f);
			}
			else if (bShowIslands && Stats.TriangleIsland.IsValidIndex(Owner) && Stats.TriangleIsland[Owner] >= 0)
			{
				MCPUvBlendPixel(Pixels, Size, X, Size - 1 - Y,
					MCPUvIslandColour(Stats.TriangleIsland[Owner]), 0.45f);
			}
			else
			{
				MCPUvBlendPixel(Pixels, Size, X, Size - 1 - Y, FColor(120, 140, 170, 255), 0.35f);
			}
		}
	}

	// Wireframe on top: it is the edges, not the fill, that show a bad unwrap.
	const FColor Wire(225, 230, 240, 255);
	for (const FTriangleID TriangleID : Target.Desc->Triangles().GetElementIDs())
	{
		const TArrayView<const FVertexInstanceID> Corners = Target.Desc->GetTriangleVertexInstances(TriangleID);
		if (Corners.Num() != 3) continue;
		const FVector2f P0 = UVs.Get(Corners[0], Channel);
		const FVector2f P1 = UVs.Get(Corners[1], Channel);
		const FVector2f P2 = UVs.Get(Corners[2], Channel);
		MCPUvDrawLine(Pixels, Size, P0, P1, Wire);
		MCPUvDrawLine(Pixels, Size, P1, P2, Wire);
		MCPUvDrawLine(Pixels, Size, P2, P0, Wire);
	}

	if (bShowGrid)
	{
		// The unit square border last, so it is never buried under a fill.
		const FColor Border(255, 214, 92, 255);
		for (int32 Index = 0; Index < Size; ++Index)
		{
			MCPUvBlendPixel(Pixels, Size, Index, 0, Border, 1.0f);
			MCPUvBlendPixel(Pixels, Size, Index, Size - 1, Border, 1.0f);
			MCPUvBlendPixel(Pixels, Size, 0, Index, Border, 1.0f);
			MCPUvBlendPixel(Pixels, Size, Size - 1, Index, Border, 1.0f);
		}
	}

	// ── Encode ───────────────────────────────────────────────────────────────
	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> PngWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!PngWrapper.IsValid()
		|| !PngWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Size, Size, ERGBFormat::BGRA, 8))
	{
		return MCPError(FString::Printf(
			TEXT("The %dx%d UV layout for '%s' could not be PNG-encoded."), Size, Size, *Target.AssetPath));
	}
	const TArray64<uint8> PngData = PngWrapper->GetCompressed(100);
	// Asked before the write, because afterwards every export looks like a
	// first one. A caller rerunning this over a directory it is collecting
	// pictures in wants to know which of them it just replaced.
	const bool bOverwroteExistingFile = FPaths::FileExists(OutputPath);
	if (!FFileHelper::SaveArrayToFile(PngData, *OutputPath))
	{
		return MCPError(FString::Printf(
			TEXT("The UV layout was rendered but could not be written to '%s'. The directory may be read-only or ")
				TEXT("the path invalid."),
			*OutputPath));
	}

	auto Result = MCPSuccess();
	MCPUvWriteAssetHeader(Result, Target);
	Result->SetStringField(TEXT("outputPath"), OutputPath);
	Result->SetNumberField(TEXT("channel"), Channel);
	Result->SetNumberField(TEXT("imageSize"), Size);
	Result->SetNumberField(TEXT("fileSizeBytes"), static_cast<double>(PngData.Num()));
	Result->SetBoolField(TEXT("showIslands"), bShowIslands);
	Result->SetBoolField(TEXT("showOverlaps"), bShowOverlaps);
	Result->SetObjectField(TEXT("channelStats"), MCPUvChannelToJson(Stats, MCPUvLightmapChannel(Target)));
	Result->SetStringField(TEXT("legend"),
		TEXT("White wire = UV triangle edges. Yellow border = the unit square. Grey grid = quarters. Coloured fill ")
			TEXT("= one hue per UV island. Red fill = two or more triangles on the same texel, which is what breaks ")
			TEXT("a lightmap bake."));

	// The render always runs and the file is always written, so the honest
	// answer to "did this change anything" is whether a picture was already
	// sitting at that path.
	if (bOverwroteExistingFile)
	{
		MCPSetExisted(Result);
		MCPSetUpdated(Result);
	}
	else
	{
		MCPSetCreated(Result);
	}

	// The mesh is untouched: this writes a preview PNG under Saved/ and nothing
	// else. An export produces an output artifact rather than project state,
	// and deleting a picture that regenerates on demand from the same call is
	// not a meaningful undo, so no inverse is named. The six export_* actions
	// that predate this one emit no rollback for the same reason.
	const FString OverwriteNote = bOverwroteExistingFile
		? FString(TEXT(" The picture that was already at that path is gone and no copy was kept."))
		: FString();
	MCPSetNoRollback(Result, FString::Printf(
		TEXT("Wrote the preview PNG '%s' and changed no project state. An export produces an output artifact, and ")
		TEXT("deleting a file that this same call regenerates on demand is not a meaningful undo, so no inverse is ")
		TEXT("named.%s"),
		*OutputPath, *OverwriteNote));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// asset(check_uvs)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FAssetHandlers::CheckUvs(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPUvTarget Target;
	if (auto Err = MCPUvResolveTarget(Params, Target)) return Err;

	const int32 ChannelCount = MCPUvChannelCount(*Target.Desc);
	const int32 RasterSize = MCPUvClampRasterSize(Params);
	const double MaxOverlapFraction = FMath::Clamp(
		OptionalNumber(Params, TEXT("maxOverlapFraction"), 0.001), 0.0, 1.0);
	const bool bRequireLightmap = OptionalBool(Params, TEXT("requireLightmapChannel"), Target.StaticMesh != nullptr);
	const int32 LightmapChannel = MCPUvLightmapChannel(Target);

	TArray<TSharedPtr<FJsonValue>> Issues;
	auto AddIssue = [&Issues](const TCHAR* Code, const TCHAR* Severity, int32 Channel, const FString& Message)
	{
		TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
		Issue->SetStringField(TEXT("code"), Code);
		Issue->SetStringField(TEXT("severity"), Severity);
		if (Channel >= 0) Issue->SetNumberField(TEXT("channel"), Channel);
		Issue->SetStringField(TEXT("message"), Message);
		Issues.Add(MakeShared<FJsonValueObject>(Issue));
	};

	if (ChannelCount == 0)
	{
		AddIssue(TEXT("no_uv_channels"), TEXT("error"), -1, FString::Printf(
			TEXT("LOD %d of '%s' has no UV channels, so it cannot be textured at all. Add one with ")
				TEXT("asset(set_uv_channel_count) and fill it with asset(unwrap_uvs)."),
			Target.LodIndex, *Target.AssetPath));
	}

	TVertexInstanceAttributesRef<FVector2f> UVs = MCPUvAttributes(*Target.Desc);
	TArray<TSharedPtr<FJsonValue>> ChannelRows;
	TArray<FMCPUvChannelStats> AllStats;
	AllStats.Reserve(ChannelCount);

	for (int32 Channel = 0; Channel < ChannelCount; ++Channel)
	{
		FMCPUvChannelStats Stats;
		Stats.Channel = Channel;
		MCPUvMeasure(*Target.Desc, UVs, Channel, Stats);
		MCPUvBuildIslands(*Target.Desc, UVs, Channel, Stats);
		MCPUvRasterise(*Target.Desc, UVs, Channel, RasterSize, Stats, nullptr, nullptr);
		ChannelRows.Add(MakeShared<FJsonValueObject>(MCPUvChannelToJson(Stats, LightmapChannel)));
		AllStats.Add(MoveTemp(Stats));
	}

	// ── Lightmap wiring ──────────────────────────────────────────────────────
	if (bRequireLightmap && Target.StaticMesh)
	{
		if (LightmapChannel < 0 || LightmapChannel >= ChannelCount)
		{
			AddIssue(TEXT("lightmap_channel_missing"), TEXT("error"), -1, FString::Printf(
				TEXT("LightMapCoordinateIndex is %d but LOD %d of '%s' has %d UV channel(s), so the lightmap ")
					TEXT("channel does not exist and every static light on this mesh bakes into channel 0's ")
					TEXT("layout. Run asset(generate_lightmap_uvs)."),
				LightmapChannel, Target.LodIndex, *Target.AssetPath, ChannelCount));
		}
		else
		{
			const FMCPUvChannelStats& Lightmap = AllStats[LightmapChannel];
			if (Lightmap.OverlapFraction > MaxOverlapFraction)
			{
				AddIssue(TEXT("lightmap_overlap"), TEXT("error"), LightmapChannel, FString::Printf(
					TEXT("%.3f%% of the unit square in lightmap channel %d has two or more triangles on it ")
						TEXT("(%d triangles involved, measured at %dx%d), over the %.3f%% budget. Overlapping ")
						TEXT("lightmap UVs bake light from one surface onto another. Re-run ")
						TEXT("asset(generate_lightmap_uvs), or asset(unwrap_uvs) with pack=true into that channel."),
					Lightmap.OverlapFraction * 100.0, LightmapChannel, Lightmap.OverlappingTriangleCount,
					Lightmap.RasterSize, Lightmap.RasterSize, MaxOverlapFraction * 100.0));
			}
			if (Lightmap.OutOfUnitSquareTriangleCount > 0)
			{
				AddIssue(TEXT("lightmap_out_of_range"), TEXT("error"), LightmapChannel, FString::Printf(
					TEXT("%d triangle(s) in lightmap channel %d fall outside the 0..1 square (bounds u %.4f..%.4f, ")
						TEXT("v %.4f..%.4f). A lightmap only samples the unit square, so anything outside it is ")
						TEXT("wrapped onto geometry it does not belong to."),
					Lightmap.OutOfUnitSquareTriangleCount, LightmapChannel,
					Lightmap.UMin, Lightmap.UMax, Lightmap.VMin, Lightmap.VMax));
			}
			if (Lightmap.DegenerateIslandCount > 0)
			{
				AddIssue(TEXT("lightmap_degenerate_islands"), TEXT("warning"), LightmapChannel, FString::Printf(
					TEXT("%d of the %d islands in lightmap channel %d have zero UV area, so the triangles in them ")
						TEXT("sample a single texel and will shade flat."),
					Lightmap.DegenerateIslandCount, Lightmap.IslandCount, LightmapChannel));
			}

			const FMeshBuildSettings& Build = Target.StaticMesh->GetSourceModel(Target.LodIndex).BuildSettings;
			if (Build.bGenerateLightmapUVs && Build.DstLightmapIndex != LightmapChannel)
			{
				AddIssue(TEXT("lightmap_index_mismatch"), TEXT("error"), -1, FString::Printf(
					TEXT("The build generates lightmap UVs into channel %d but LightMapCoordinateIndex is %d, so ")
						TEXT("the renderer samples a different channel from the one the build writes. Set both to ")
						TEXT("the same index with asset(generate_lightmap_uvs)."),
					Build.DstLightmapIndex, LightmapChannel));
			}
			if (!Build.bGenerateLightmapUVs && LightmapChannel > 0)
			{
				AddIssue(TEXT("lightmap_generation_disabled"), TEXT("warning"), -1, FString::Printf(
					TEXT("LightMapCoordinateIndex points at channel %d but bGenerateLightmapUVs is off, so that ")
						TEXT("channel is whatever the source asset shipped and is not maintained by the build."),
					LightmapChannel));
			}
		}
	}

	// ── Per-channel health ───────────────────────────────────────────────────
	for (const FMCPUvChannelStats& Stats : AllStats)
	{
		if (Stats.TrianglesWithUvs == 0 && Stats.TriangleCount > 0)
		{
			AddIssue(TEXT("channel_empty"), TEXT("error"), Stats.Channel, FString::Printf(
				TEXT("Every one of the %d triangles in channel %d has zero UV area, so the channel is effectively ")
					TEXT("empty. It was probably added but never filled."),
				Stats.TriangleCount, Stats.Channel));
			continue;
		}
		if (Stats.DegenerateTriangleCount > 0)
		{
			AddIssue(TEXT("degenerate_triangles"), TEXT("warning"), Stats.Channel, FString::Printf(
				TEXT("%d of %d triangles in channel %d have zero UV area."),
				Stats.DegenerateTriangleCount, Stats.TriangleCount, Stats.Channel));
		}
		if (Stats.Channel != LightmapChannel && Stats.OutOfUnitSquareTriangleCount > 0)
		{
			// Tiling is legitimate outside the lightmap channel, so this is
			// information rather than a fault.
			AddIssue(TEXT("out_of_unit_square"), TEXT("info"), Stats.Channel, FString::Printf(
				TEXT("%d triangle(s) in channel %d fall outside the 0..1 square (u %.4f..%.4f, v %.4f..%.4f). ")
					TEXT("That is normal for a tiling texture and a fault only for a lightmap or a bake target."),
				Stats.OutOfUnitSquareTriangleCount, Stats.Channel,
				Stats.UMin, Stats.UMax, Stats.VMin, Stats.VMax));
		}
		if (Stats.FlippedTriangleCount > 0)
		{
			AddIssue(TEXT("flipped_triangles"), TEXT("warning"), Stats.Channel, FString::Printf(
				TEXT("%d triangle(s) in channel %d wind the opposite way from the rest, so their texture is ")
					TEXT("mirrored."),
				Stats.FlippedTriangleCount, Stats.Channel));
		}
	}

	int32 Errors = 0, Warnings = 0;
	for (const TSharedPtr<FJsonValue>& Entry : Issues)
	{
		const FString Severity = Entry->AsObject()->GetStringField(TEXT("severity"));
		if (Severity == TEXT("error")) ++Errors;
		else if (Severity == TEXT("warning")) ++Warnings;
	}

	auto Result = MCPSuccess();
	MCPUvWriteAssetHeader(Result, Target);
	Result->SetArrayField(TEXT("channels"), ChannelRows);
	Result->SetArrayField(TEXT("issues"), Issues);
	Result->SetNumberField(TEXT("errorCount"), Errors);
	Result->SetNumberField(TEXT("warningCount"), Warnings);
	Result->SetBoolField(TEXT("healthy"), Errors == 0);
	Result->SetNumberField(TEXT("overlapRasterSize"), RasterSize);
	Result->SetNumberField(TEXT("maxOverlapFraction"), MaxOverlapFraction);
	Result->SetStringField(TEXT("overlapNote"), FString::Printf(
		TEXT("Overlap and coverage are measured by rasterising the channel into a %dx%d square, not by an exact ")
			TEXT("triangle-triangle test. Raise rasterSize for a finer answer on a dense mesh."),
		RasterSize, RasterSize));
	return MCPResult(Result);
}
