// Procedural StaticMesh operations: simplify, remesh, mirror, hole fill,
// collision generation and plane-slice fracture.
//
// Before this file the bridge could reshape a StaticMesh in exactly two ways:
// asset(mesh_boolean) (CSG between two meshes) and the UV surface in
// AssetHandlers_UV.cpp. Everything else on this list was an execute_python
// escape hatch that reached into the unreal.GeometryScript_* modules by hand.
//
// All functions below are members of FAssetHandlers: a translation-unit
// partition the way AssetHandlers_UV.cpp and AssetHandlers_Mesh.cpp are, not a
// new class. Declarations go in AssetHandlers.h, registration in
// AssetHandlers.cpp::RegisterHandlers.
//
// GEOMETRY SCRIPT IS REACHED BY REFLECTION. GeometryScripting is an engine
// PLUGIN, so a Build.cs dependency on it would fail to link for every project
// that has it switched off. Instead the UFUNCTIONs are invoked through a
// heap-allocated parameter frame (FMCPGeoCall below), exactly the way
// AssetHandlers_MeshBoolean.cpp and AssetHandlers_UV.cpp do it, and a project
// without the plugin gets a typed "geometry_scripting_unavailable" answer that
// names what to enable.
//
// WHAT IS DELIBERATELY NOT HERE:
//
//   * UV projection. asset(unwrap_uvs) already covers planar, box and cylinder
//     projection plus the auto-unwrappers; a second entry point would be a
//     rename of an action that ships.
//   * Boolean CSG. asset(mesh_boolean) ships.
//   * Chaos / GeometryCollection fracture. Every entry point for it
//     (FFractureEngineFracturing::VoronoiFracture / PlaneCutter / SliceCutter /
//     BrickCutter / UniformFracture in the Fracture plugin, and every
//     FGeometryCollectionEngineConversion::Append* / Convert* in
//     GeometryCollectionEngine) is a plain C++ static with no UFUNCTION on it,
//     so ProcessEvent cannot reach any of them and reflection is not an option.
//     apply_mesh_fracture below is the reflection-reachable half of the same idea: it
//     slices the mesh with planes and writes the pieces out as separate
//     StaticMesh assets, which is what level geometry wants. It does NOT
//     produce a UGeometryCollection and says so in its own result.
//
// DESTRUCTIVE FORM. Every mutation here defaults to writing a SEPARATE output
// asset, whose inverse is a complete delete_asset rollback. inPlace=true
// overwrites the source and has no complete inverse, because the original
// triangles are gone; that case emits no rollback descriptor and says why,
// rather than shipping an undo it cannot honour.
//
// UNITY BUILD: every file-local symbol below is prefixed MCPGeo / FMCPGeo. The
// asset handlers share one unity blob, so a helper named the same as one in
// AssetHandlers_MeshBoolean.cpp or AssetHandlers_UV.cpp is a redefinition
// (C2084) on whichever machine happens to group them together. The reflected-
// call helper is duplicated rather than shared for the same reason the UV pass
// duplicated it: the shared home would be Public/HandlerUtils.h, and moving it
// there is a change to a file this work does not own.

#include "AssetHandlers.h"

#include "HandlerFunctionCall.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodySetup.h"

#include "Math/RandomStream.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{

// ─── Limits ──────────────────────────────────────────────────────────────────

/** Hard ceiling on the pieces one fracture call may write. A fracture that
 *  asked for 10,000 pieces would spend an hour creating assets nobody wanted
 *  and there would be no way to stop it partway. */
constexpr int32 MCPGeoMaxFracturePieces = 256;

/** Ceiling on the cut planes a single fracture may apply. Each plane is a
 *  full boolean pass over the whole mesh, so this is the runtime guard rather
 *  than an asset-count one. */
constexpr int32 MCPGeoMaxFracturePlanes = 64;

// ─── The Geometry Script surface this file drives ────────────────────────────

const TCHAR* const MCPGeoDynamicMeshClass    = TEXT("/Script/GeometryFramework.DynamicMesh");
const TCHAR* const MCPGeoDebugClass          = TEXT("/Script/GeometryScriptingCore.GeometryScriptDebug");
const TCHAR* const MCPGeoAssetFunctions      = TEXT("/Script/GeometryScriptingCore.GeometryScriptLibrary_StaticMeshFunctions");
const TCHAR* const MCPGeoQueryFunctions      = TEXT("/Script/GeometryScriptingCore.GeometryScriptLibrary_MeshQueryFunctions");
const TCHAR* const MCPGeoSimplifyFunctions   = TEXT("/Script/GeometryScriptingCore.GeometryScriptLibrary_MeshSimplifyFunctions");
const TCHAR* const MCPGeoRemeshFunctions     = TEXT("/Script/GeometryScriptingCore.GeometryScriptLibrary_RemeshingFunctions");
const TCHAR* const MCPGeoBooleanFunctions    = TEXT("/Script/GeometryScriptingCore.GeometryScriptLibrary_MeshBooleanFunctions");
const TCHAR* const MCPGeoRepairFunctions     = TEXT("/Script/GeometryScriptingCore.GeometryScriptLibrary_MeshRepairFunctions");
const TCHAR* const MCPGeoCollisionFunctions  = TEXT("/Script/GeometryScriptingCore.GeometryScriptLibrary_CollisionFunctions");
const TCHAR* const MCPGeoDecompFunctions     = TEXT("/Script/GeometryScriptingCore.GeometryScriptLibrary_MeshDecompositionFunctions");
const TCHAR* const MCPGeoCreateAssetFunctions= TEXT("/Script/GeometryScriptingEditor.GeometryScriptLibrary_CreateNewAssetFunctions");

const TCHAR* const MCPGeoLODTypeEnum         = TEXT("/Script/GeometryScriptingCore.EGeometryScriptLODType");
const TCHAR* const MCPGeoSimplifyMethodEnum  = TEXT("/Script/GeometryScriptingCore.EGeometryScriptRemoveMeshSimplificationType");
const TCHAR* const MCPGeoRemeshTargetEnum    = TEXT("/Script/GeometryScriptingCore.EGeometryScriptUniformRemeshTargetType");
const TCHAR* const MCPGeoRemeshSmoothingEnum = TEXT("/Script/GeometryScriptingCore.EGeometryScriptRemeshSmoothingType");
const TCHAR* const MCPGeoRemeshConstraintEnum= TEXT("/Script/GeometryScriptingCore.EGeometryScriptRemeshEdgeConstraintType");
const TCHAR* const MCPGeoFillHolesEnum       = TEXT("/Script/GeometryScriptingCore.EGeometryScriptFillHolesMethod");
const TCHAR* const MCPGeoCollisionMethodEnum = TEXT("/Script/GeometryScriptingCore.EGeometryScriptCollisionGenerationMethod");
const TCHAR* const MCPGeoSweptHullAxisEnum   = TEXT("/Script/GeometryScriptingCore.EGeometryScriptSweptHullAxis");

/** Ask the module system for the Geometry Script modules once. False when the
 *  plugin is absent or disabled for this project. */
bool MCPGeoEnsureGeometryScripting()
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
	return FindObject<UClass>(nullptr, MCPGeoDynamicMeshClass) != nullptr
		&& FindObject<UClass>(nullptr, MCPGeoAssetFunctions) != nullptr;
}

/** The typed refusal for a project without the plugin. `StillWorks` names the
 *  actions in this area that do not need it, so the answer is a route forward
 *  rather than a dead end. */
TSharedPtr<FJsonValue> MCPGeoUnavailable(const FString& Detail, const FString& StillWorks = FString())
{
	FString Message = FString::Printf(
		TEXT("GeometryScripting plugin not available: %s Enable the 'Geometry Script' plugin ")
			TEXT("(Edit > Plugins > Geometry Script) and restart the editor, then retry."),
		*Detail);
	if (!StillWorks.IsEmpty())
	{
		Message += TEXT(" ") + StillWorks;
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), Message);
	Obj->SetStringField(TEXT("reason"), TEXT("geometry_scripting_unavailable"));
	Obj->SetStringField(TEXT("requiredPlugin"), TEXT("GeometryScripting"));
	return MakeShared<FJsonValueObject>(Obj);
}

/** A value from a UEnum this module does not link, by enumerator name.
 *  INDEX_NONE when the enum or the enumerator is absent, so an engine that
 *  renames an enumerator reports that rather than silently picking ordinal 0. */
int64 MCPGeoEnumValue(const TCHAR* EnumPath, const FString& EnumeratorName)
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
struct FMCPGeoCall
{
	UObject* CDO = nullptr;
	UFunction* Function = nullptr;
	TArray<uint8> Frame;

	FMCPGeoCall() = default;
	FMCPGeoCall(const FMCPGeoCall&) = delete;
	FMCPGeoCall& operator=(const FMCPGeoCall&) = delete;
	~FMCPGeoCall() { Release(); }

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

	bool SetString(const TCHAR* Name, const FString& Value)
	{
		FStrProperty* Prop = CastField<FStrProperty>(Param(Name));
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

	bool SetTransform(const TCHAR* Name, const FTransform& Value)
	{
		FStructProperty* Prop = CastField<FStructProperty>(Param(Name));
		if (!Prop || Prop->Struct != TBaseStructure<FTransform>::Get()) return false;
		*Prop->ContainerPtrToValuePtr<FTransform>(Frame.GetData()) = Value;
		return true;
	}

	bool SetEnum(const TCHAR* Name, int64 Value)
	{
		if (Value == INDEX_NONE) return false;
		FProperty* Prop = Param(Name);
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(
				EnumProp->ContainerPtrToValuePtr<void>(Frame.GetData()), Value);
			return true;
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			ByteProp->SetPropertyValue_InContainer(Frame.GetData(), static_cast<uint8>(Value));
			return true;
		}
		return false;
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

	int32 GetInt(const TCHAR* Name) const
	{
		FNumericProperty* Prop = CastField<FNumericProperty>(Param(Name));
		if (!Prop) return 0;
		return static_cast<int32>(Prop->GetSignedIntPropertyValue(
			Prop->ContainerPtrToValuePtr<void>(const_cast<uint8*>(Frame.GetData()))));
	}

	bool GetBool(const TCHAR* Name) const
	{
		FBoolProperty* Prop = CastField<FBoolProperty>(Param(Name));
		if (!Prop) return false;
		return Prop->GetPropertyValue_InContainer(const_cast<uint8*>(Frame.GetData()));
	}

	/** An FBox return or out parameter, which is how the mesh bounds come back.
	 *  FBox is a noexport core struct: it is declared as USTRUCT in
	 *  NoExportTypes.h but the C++ type is UE::Math::TBox<double>, which has no
	 *  StaticStruct() and no TBaseStructure specialisation (Class.h only
	 *  specialises Box2D, not Box). The reflected type is
	 *  /Script/CoreUObject.Box, so its name is the identity check available. */
	bool GetBox(const TCHAR* Name, FBox& Out) const
	{
		FStructProperty* Prop = CastField<FStructProperty>(Param(Name));
		if (!Prop || !Prop->Struct) return false;
		if (Prop->Struct->GetFName() != FName(NAME_Box)) return false;
		Out = *Prop->ContainerPtrToValuePtr<FBox>(const_cast<uint8*>(Frame.GetData()));
		return true;
	}

	/** An array-of-objects out parameter, which is how SplitMeshByComponents
	 *  hands back the pieces it made. */
	bool GetObjectArray(const TCHAR* Name, TArray<UObject*>& Out) const
	{
		Out.Reset();
		FArrayProperty* ArrayProp = CastField<FArrayProperty>(Param(Name));
		if (!ArrayProp) return false;
		FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(ArrayProp->Inner);
		if (!Inner) return false;

		FScriptArrayHelper Helper(ArrayProp,
			ArrayProp->ContainerPtrToValuePtr<void>(const_cast<uint8*>(Frame.GetData())));
		Out.Reserve(Helper.Num());
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			if (UObject* Element = Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index)))
			{
				Out.Add(Element);
			}
		}
		return true;
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

	/** The return value, whatever it is named. */
	UObject* ReturnObject() const
	{
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			if (!(It->PropertyFlags & CPF_ReturnParm)) continue;
			if (FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(*It))
			{
				return Prop->GetObjectPropertyValue_InContainer(const_cast<uint8*>(Frame.GetData()));
			}
		}
		return nullptr;
	}
};

/** Read and clear the messages a UGeometryScriptDebug collected, so the reason
 *  an operation failed reaches the caller instead of only the output log. */
TArray<FString> MCPGeoDrainDebug(UObject* Debug)
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

void MCPGeoAttachMessages(const TSharedPtr<FJsonObject>& Out, const TArray<FString>& Messages)
{
	if (Messages.Num() == 0) return;
	Out->SetArrayField(TEXT("geometryScriptMessages"), MCPStringListToJson(Messages));
}

// ─── Mesh statistics ─────────────────────────────────────────────────────────

/** Triangle, vertex and island counts plus closedness for one DynamicMesh,
 *  through Geometry Script's own query library, which is the only thing that
 *  can see inside it from here. */
struct FMCPGeoMeshStats
{
	int32 Triangles = 0;
	int32 Vertices = 0;
	int32 Islands = 0;
	int32 OpenBorderEdges = 0;
	bool bClosed = false;

	TSharedPtr<FJsonObject> ToJson() const
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("triangles"), Triangles);
		Obj->SetNumberField(TEXT("vertices"), Vertices);
		Obj->SetNumberField(TEXT("islands"), Islands);
		Obj->SetNumberField(TEXT("openBorderEdges"), OpenBorderEdges);
		Obj->SetBoolField(TEXT("closed"), bClosed);
		return Obj;
	}
};

FMCPGeoMeshStats MCPGeoReadStats(UObject* Mesh)
{
	FMCPGeoMeshStats Stats;
	if (!Mesh) return Stats;

	FString Error;
	auto ReadInt = [&](const TCHAR* FunctionName) -> int32
	{
		FMCPGeoCall Call;
		if (!Call.Bind(MCPGeoQueryFunctions, FunctionName, Error)) return 0;
		Call.SetObject(TEXT("TargetMesh"), Mesh);
		Call.Invoke();
		return Call.GetInt(TEXT("ReturnValue"));
	};

	Stats.Triangles = ReadInt(TEXT("GetNumTriangleIDs"));
	Stats.Vertices = ReadInt(TEXT("GetNumVertexIDs"));
	Stats.Islands = ReadInt(TEXT("GetNumConnectedComponents"));
	Stats.OpenBorderEdges = ReadInt(TEXT("GetNumOpenBorderEdges"));

	FMCPGeoCall Closed;
	if (Closed.Bind(MCPGeoQueryFunctions, TEXT("GetIsClosedMesh"), Error))
	{
		Closed.SetObject(TEXT("TargetMesh"), Mesh);
		Closed.Invoke();
		Stats.bClosed = Closed.GetBool(TEXT("ReturnValue"));
	}
	return Stats;
}

bool MCPGeoReadBounds(UObject* Mesh, FBox& OutBox)
{
	OutBox = FBox(ForceInit);
	if (!Mesh) return false;
	FString Error;
	FMCPGeoCall Call;
	if (!Call.Bind(MCPGeoQueryFunctions, TEXT("GetMeshBoundingBox"), Error)) return false;
	Call.SetObject(TEXT("TargetMesh"), Mesh);
	Call.Invoke();
	return Call.GetBox(TEXT("ReturnValue"), OutBox) && OutBox.IsValid;
}

/** The written asset's own numbers, so the caller can verify rather than trust. */
void MCPGeoWriteAssetStats(const TSharedPtr<FJsonObject>& Out, const TCHAR* Field, UStaticMesh* Mesh)
{
	if (!Mesh) return;
	TSharedPtr<FJsonObject> Stats = MakeShared<FJsonObject>();
	Stats->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	Stats->SetNumberField(TEXT("triangles"), Mesh->GetNumTriangles(0));
	Stats->SetNumberField(TEXT("vertices"), Mesh->GetNumVertices(0));
	Stats->SetNumberField(TEXT("lodCount"), Mesh->GetNumLODs());
	Stats->SetNumberField(TEXT("materialSlots"), Mesh->GetStaticMaterials().Num());
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	Stats->SetObjectField(TEXT("boundsOrigin"), MCPVec3ToJsonObject(Bounds.Origin));
	Stats->SetObjectField(TEXT("boundsExtent"), MCPVec3ToJsonObject(Bounds.BoxExtent));
	Out->SetObjectField(Field, Stats);
}

// ─── Simple collision ────────────────────────────────────────────────────────

/** A structural signature of a body setup's simple collision: how many of each
 *  shape kind, and how many vertices each convex hull has.
 *
 *  Used as the idempotency test for generate_mesh_collision. Generation from
 *  the same mesh with the same options is deterministic, so an identical
 *  signature means a repeat would produce the collision that is already there.
 *  It is a SIGNATURE, not a byte comparison, and the result says so. */
FString MCPGeoCollisionSignature(const UBodySetup* Setup)
{
	if (!Setup) return TEXT("none");
	const FKAggregateGeom& Geom = Setup->AggGeom;
	FString Signature = FString::Printf(
		TEXT("box=%d sphere=%d sphyl=%d taperedCapsule=%d levelSet=%d skinnedLevelSet=%d convex=%d"),
		Geom.BoxElems.Num(),
		Geom.SphereElems.Num(),
		Geom.SphylElems.Num(),
		Geom.TaperedCapsuleElems.Num(),
		Geom.LevelSetElems.Num(),
		Geom.SkinnedLevelSetElems.Num(),
		Geom.ConvexElems.Num());
	for (const FKConvexElem& Convex : Geom.ConvexElems)
	{
		Signature += FString::Printf(TEXT(" hull:%d"), Convex.VertexData.Num());
	}
	Signature += FString::Printf(TEXT(" trace=%d"), static_cast<int32>(Setup->CollisionTraceFlag));
	return Signature;
}

int32 MCPGeoCollisionShapeCount(const UBodySetup* Setup)
{
	return Setup ? Setup->AggGeom.GetElementCount() : 0;
}

/** Copy the simple collision shapes and trace flag from one StaticMesh to
 *  another. A generated result with no collision at all is a silent trap for
 *  anything that walks on it. */
bool MCPGeoCopySimpleCollision(UStaticMesh* From, UStaticMesh* To)
{
	if (!From || !To || From == To) return false;
	UBodySetup* SourceSetup = From->GetBodySetup();
	if (!SourceSetup) return false;

	if (!To->GetBodySetup())
	{
		To->CreateBodySetup();
	}
	UBodySetup* TargetSetup = To->GetBodySetup();
	if (!TargetSetup) return false;

	TargetSetup->Modify();
	TargetSetup->AggGeom = SourceSetup->AggGeom;
	TargetSetup->CollisionTraceFlag = SourceSetup->CollisionTraceFlag;
	// Invalidate and leave the cook to the engine's own lazy path. Forcing
	// CreatePhysicsMeshes here would cook on the game thread for no benefit the
	// caller can observe from the result.
	TargetSetup->InvalidatePhysicsData();
	return true;
}

// ─── Shared parameter parsing ────────────────────────────────────────────────

/** The LOD selector word, defaulting to the highest-quality source available.
 *  Empty return means the word was not recognised. */
FString MCPGeoResolveLODType(const FString& Requested)
{
	if (Requested.IsEmpty()) return TEXT("MaxAvailable");
	const FString Lower = Requested.ToLower();
	if (Lower == TEXT("maxavailable")) return TEXT("MaxAvailable");
	if (Lower == TEXT("hiressourcemodel")) return TEXT("HiResSourceModel");
	if (Lower == TEXT("sourcemodel")) return TEXT("SourceModel");
	if (Lower == TEXT("renderdata")) return TEXT("RenderData");
	return FString();
}

/**
 * Everything the five mesh-rewriting actions share: which asset, which LOD,
 * where the result goes, and what to do about an existing destination.
 *
 * Parsed once here rather than five times, so "inPlace and outputPath disagree"
 * cannot be an error in one action and a silent precedence rule in another.
 */
struct FMCPGeoRequest
{
	FString AssetPath;
	UStaticMesh* SourceMesh = nullptr;

	FString OutputPath;
	UStaticMesh* ExistingOutput = nullptr;
	bool bInPlace = false;

	FString LODTypeName;
	int32 LODIndex = 0;
	int64 LODTypeValue = INDEX_NONE;

	FString BackupPath;
	bool bDryRun = false;
	bool bSave = true;
	bool bRecomputeNormals = false;
	bool bRecomputeTangents = false;
	bool bRemoveDegenerates = false;
	bool bCopyMaterials = true;
	bool bCopyCollision = true;
	FString NaniteMode = TEXT("inherit");
};

/** Parse and validate the shared parameters. Returns an error value on the
 *  first problem, having touched nothing. `OutputSuffix` names the default
 *  output asset when the caller did not: "SM_Wall" plus "_Simplified". */
TSharedPtr<FJsonValue> MCPGeoParseRequest(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* OutputSuffix,
	FMCPGeoRequest& Out)
{
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), Out.AssetPath)) return Err;

	Out.bInPlace = OptionalBool(Params, TEXT("inPlace"), false);
	Out.OutputPath = OptionalString(Params, TEXT("outputPath"));
	if (Out.bInPlace && !Out.OutputPath.IsEmpty())
	{
		return MCPError(TEXT("Pass 'outputPath' or 'inPlace', not both: they name two different destinations ")
			TEXT("and there is no safe answer when they disagree."));
	}
	if (Out.bInPlace)
	{
		Out.OutputPath = Out.AssetPath;
	}
	else if (Out.OutputPath.IsEmpty())
	{
		const FMCPAssetPathForms Forms = MCPAssetPathForms(Out.AssetPath);
		if (Forms.PackagePath.IsEmpty())
		{
			return MCPError(FString::Printf(
				TEXT("'%s' is not a usable asset path, so no default outputPath could be derived from it. ")
					TEXT("Pass outputPath explicitly, or inPlace=true to overwrite the source."),
				*Out.AssetPath));
		}
		Out.OutputPath = Forms.PackagePath + TEXT("_") + OutputSuffix;
	}

	const FString RequestedLODType = OptionalString(Params, TEXT("lodType"));
	Out.LODTypeName = MCPGeoResolveLODType(RequestedLODType);
	if (Out.LODTypeName.IsEmpty())
	{
		return MCPError(FString::Printf(
			TEXT("Unknown lodType '%s'. Use MaxAvailable, HiResSourceModel, SourceModel or RenderData."),
			*RequestedLODType));
	}
	Out.LODIndex = FMath::Max(0, OptionalInt(Params, TEXT("lodIndex"), 0));

	Out.BackupPath = OptionalString(Params, TEXT("backupPath"));
	Out.bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	Out.bSave = OptionalBool(Params, TEXT("save"), true);
	Out.bRecomputeNormals = OptionalBool(Params, TEXT("recomputeNormals"), false);
	Out.bRecomputeTangents = OptionalBool(Params, TEXT("recomputeTangents"), false);
	Out.bRemoveDegenerates = OptionalBool(Params, TEXT("removeDegenerates"), false);
	Out.bCopyMaterials = OptionalBool(Params, TEXT("copyMaterialsFromSource"), true);
	Out.bCopyCollision = OptionalBool(Params, TEXT("copyCollisionFromSource"), true);

	Out.NaniteMode = OptionalString(Params, TEXT("nanite"), TEXT("inherit")).ToLower();
	if (Out.NaniteMode != TEXT("inherit") && Out.NaniteMode != TEXT("enable") && Out.NaniteMode != TEXT("disable"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown nanite mode '%s'. Use inherit (copy the source's setting), enable or disable."),
			*Out.NaniteMode));
	}

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("error")).ToLower();
	if (OnConflict != TEXT("error") && OnConflict != TEXT("replace"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown onConflict '%s'. Use error (default) or replace."), *OnConflict));
	}

	if (MCPIsProtectedAssetPath(Out.OutputPath)) return MCPProtectedPathError(Out.OutputPath);
	if (!Out.BackupPath.IsEmpty() && MCPIsProtectedAssetPath(Out.BackupPath))
	{
		return MCPProtectedPathError(Out.BackupPath);
	}

	UObject* SourceObject = MCPLoadAssetObject(Out.AssetPath);
	Out.SourceMesh = Cast<UStaticMesh>(SourceObject);
	if (!Out.SourceMesh)
	{
		return SourceObject
			? MCPAssetWrongTypeError(Out.AssetPath, SourceObject, TEXT("StaticMesh"))
			: MCPAssetNotFoundError(Out.AssetPath, TEXT("Source mesh"));
	}

	UObject* OutputObject = MCPLoadAssetObject(Out.OutputPath);
	Out.ExistingOutput = Cast<UStaticMesh>(OutputObject);
	if (OutputObject && !Out.ExistingOutput)
	{
		return MCPAssetWrongTypeError(Out.OutputPath, OutputObject, TEXT("StaticMesh"));
	}
	if (Out.ExistingOutput && OnConflict == TEXT("error") && !Out.bInPlace)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' already exists. Pass onConflict='replace' to overwrite it, inPlace=true to write back ")
				TEXT("into the source, or name a different outputPath."),
			*Out.OutputPath));
	}

	return nullptr;
}

/** Echo the request on every result, so a response identifies the call that
 *  produced it without the caller correlating it themselves. */
void MCPGeoDescribeRequest(const TSharedPtr<FJsonObject>& Out, const FMCPGeoRequest& Request)
{
	Out->SetStringField(TEXT("assetPath"), Request.AssetPath);
	Out->SetStringField(TEXT("outputPath"), Request.OutputPath);
	Out->SetBoolField(TEXT("inPlace"), Request.bInPlace);
	Out->SetStringField(TEXT("lodType"), Request.LODTypeName);
	Out->SetNumberField(TEXT("lodIndex"), Request.LODIndex);
}

// ─── The copy-in / write-out pair ────────────────────────────────────────────

/** Read the source StaticMesh LOD into a DynamicMesh. */
bool MCPGeoCopyMeshIn(
	const FMCPGeoRequest& Request,
	UObject* ToDynamic,
	UObject* Debug,
	TArray<FString>& Messages,
	FString& OutFailure)
{
	FMCPGeoCall Call;
	FString Error;
	if (!Call.Bind(MCPGeoAssetFunctions, TEXT("CopyMeshFromStaticMeshV2"), Error))
	{
		OutFailure = Error;
		return false;
	}
	Call.SetObject(TEXT("FromStaticMeshAsset"), Request.SourceMesh);
	Call.SetObject(TEXT("ToDynamicMesh"), ToDynamic);
	Call.SetStructEnum(TEXT("RequestedLOD"), TEXT("LODType"), Request.LODTypeValue);
	Call.SetStructNumber(TEXT("RequestedLOD"), TEXT("LODIndex"), Request.LODIndex);
	Call.SetBool(TEXT("bUseSectionMaterials"), true);
	Call.SetObject(TEXT("Debug"), Debug);
	Call.Invoke();

	Messages.Append(MCPGeoDrainDebug(Debug));
	if (Call.GetEnumName(TEXT("Outcome")) != TEXT("Success"))
	{
		OutFailure = FString::Printf(
			TEXT("reading '%s' at LOD %s[%d] produced no mesh."),
			*Request.AssetPath, *Request.LODTypeName, Request.LODIndex);
		return false;
	}
	return true;
}

/**
 * Write a DynamicMesh out to a StaticMesh asset at `TargetPath`, creating it if
 * it is not there. Returns the written asset, or null with `OutFailure` and
 * `OutReason` set.
 */
UStaticMesh* MCPGeoWriteMeshOut(
	const FMCPGeoRequest& Request,
	UObject* FromDynamic,
	const FString& TargetPath,
	UStaticMesh* ExistingTarget,
	UObject* Debug,
	TArray<FString>& Messages,
	bool& bOutCreated,
	FString& OutFailure,
	FString& OutReason)
{
	bOutCreated = false;
	FString BindError;

	if (ExistingTarget)
	{
		FMCPGeoCall CopyOut;
		if (!CopyOut.Bind(MCPGeoAssetFunctions, TEXT("CopyMeshToStaticMesh"), BindError))
		{
			OutFailure = BindError;
			OutReason = TEXT("geometry_scripting_unavailable");
			return nullptr;
		}
		CopyOut.SetObject(TEXT("FromDynamicMesh"), FromDynamic);
		CopyOut.SetObject(TEXT("ToStaticMeshAsset"), ExistingTarget);
		CopyOut.SetStructBool(TEXT("Options"), TEXT("bEnableRecomputeNormals"), Request.bRecomputeNormals);
		CopyOut.SetStructBool(TEXT("Options"), TEXT("bEnableRecomputeTangents"), Request.bRecomputeTangents);
		CopyOut.SetStructBool(TEXT("Options"), TEXT("bEnableRemoveDegenerates"), Request.bRemoveDegenerates);
		CopyOut.SetStructNumber(TEXT("TargetLOD"), TEXT("LODIndex"), 0);
		CopyOut.SetBool(TEXT("bUseSectionMaterials"), true);
		CopyOut.SetObject(TEXT("Debug"), Debug);
		CopyOut.Invoke();
		Messages.Append(MCPGeoDrainDebug(Debug));

		if (CopyOut.GetEnumName(TEXT("Outcome")) != TEXT("Success"))
		{
			OutFailure = FString::Printf(TEXT("writing the result into '%s' failed."), *TargetPath);
			OutReason = TEXT("write_failed");
			return nullptr;
		}
		return ExistingTarget;
	}

	FMCPGeoCall Create;
	if (!Create.Bind(MCPGeoCreateAssetFunctions, TEXT("CreateNewStaticMeshAssetFromMesh"), BindError))
	{
		OutFailure = FString::Printf(
			TEXT("%s Creating a new StaticMesh asset needs the editor half of the plugin ")
				TEXT("(GeometryScriptingEditor)."),
			*BindError);
		OutReason = TEXT("geometry_scripting_unavailable");
		return nullptr;
	}
	Create.SetObject(TEXT("FromDynamicMesh"), FromDynamic);
	Create.SetString(TEXT("AssetPathAndName"), TargetPath);
	Create.SetStructBool(TEXT("Options"), TEXT("bEnableRecomputeNormals"), Request.bRecomputeNormals);
	Create.SetStructBool(TEXT("Options"), TEXT("bEnableRecomputeTangents"), Request.bRecomputeTangents);
	Create.SetStructBool(TEXT("Options"), TEXT("bEnableCollision"), Request.bCopyCollision);
	Create.SetStructBool(TEXT("Options"), TEXT("bEnableNanite"),
		Request.NaniteMode == TEXT("enable")
		|| (Request.NaniteMode == TEXT("inherit")
			&& Request.SourceMesh && Request.SourceMesh->GetNaniteSettings().bEnabled));
	Create.SetObject(TEXT("Debug"), Debug);
	Create.Invoke();
	Messages.Append(MCPGeoDrainDebug(Debug));

	UStaticMesh* Written = Cast<UStaticMesh>(Create.ReturnObject());
	if (Create.GetEnumName(TEXT("Outcome")) != TEXT("Success") || !Written)
	{
		OutFailure = FString::Printf(
			TEXT("the new StaticMesh asset at '%s' could not be created. The path may be invalid or ")
				TEXT("already taken by another asset type."),
			*TargetPath);
		OutReason = TEXT("create_failed");
		return nullptr;
	}
	bOutCreated = true;
	return Written;
}

/** Materials, collision and Nanite on a freshly written asset, plus the save.
 *  Every one of these is reported, because a result that says "written" while
 *  the package is still dirty in memory reads as a write that silently did not
 *  take. */
void MCPGeoFinishWrite(
	const TSharedPtr<FJsonObject>& Result,
	const FMCPGeoRequest& Request,
	UStaticMesh* Written,
	const FString& TargetPath,
	bool bCreated)
{
	if (!Written) return;

	bool bMaterialsCopied = false;
	if (Request.bCopyMaterials && Written != Request.SourceMesh)
	{
		// The dynamic mesh carried section indices as material IDs, so the
		// source's slot list maps straight back onto the result's sections.
		Written->Modify();
		Written->SetStaticMaterials(Request.SourceMesh->GetStaticMaterials());
		bMaterialsCopied = true;
	}

	bool bCollisionCopied = false;
	if (Request.bCopyCollision && Written != Request.SourceMesh)
	{
		bCollisionCopied = MCPGeoCopySimpleCollision(Request.SourceMesh, Written);
	}

	bool bNaniteChanged = false;
	{
		FMeshNaniteSettings Settings = Written->GetNaniteSettings();
		const bool bWant =
			Request.NaniteMode == TEXT("enable") ? true :
			Request.NaniteMode == TEXT("disable") ? false :
			Request.SourceMesh->GetNaniteSettings().bEnabled;
		if (Settings.bEnabled != bWant)
		{
			Settings.bEnabled = bWant;
			Written->Modify();
			Written->SetNaniteSettings(Settings);
			bNaniteChanged = true;
		}
	}

	if (bMaterialsCopied || bCollisionCopied || bNaniteChanged)
	{
		Written->PostEditChange();
	}

	Result->SetBoolField(TEXT("written"), true);
	Result->SetBoolField(TEXT("createdOutputAsset"), bCreated);
	Result->SetBoolField(TEXT("materialsCopiedFromSource"), bMaterialsCopied);
	Result->SetBoolField(TEXT("collisionCopiedFromSource"), bCollisionCopied);
	Result->SetStringField(TEXT("nanite"), Request.NaniteMode);
	Result->SetBoolField(TEXT("naniteChanged"), bNaniteChanged);
	MCPGeoWriteAssetStats(Result, TEXT("output"), Written);

	if (Request.bSave)
	{
		FString SaveReason;
		const bool bSaved = SaveAssetPackageChecked(Written, SaveReason);
		MCPNoteSaveOutcome(Result, TargetPath, bSaved, SaveReason);
	}
	else
	{
		// The caller asked for this, so it is not a failure. It is still said
		// out loud, because geometry that only exists in memory is gone at the
		// next editor start.
		Written->MarkPackageDirty();
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetStringField(TEXT("saveSkippedReason"),
			TEXT("save=false was requested, so the new geometry is dirty in memory only and is lost when the ")
				TEXT("editor closes. Call asset(save) for it, or repeat with save=true."));
	}
}

/**
 * The honest absence of a rollback.
 *
 * A newly created output asset has an exact inverse and each handler emits it
 * itself. An in-place rewrite does not, because the triangles that were there
 * are gone and no call puts them back. Saying nothing would leave a caller
 * assuming the usual rollback exists, so the absence is stated along with the
 * two ways to get a reversible form of the same edit.
 */
void MCPGeoExplainNoRollback(
	const TSharedPtr<FJsonObject>& Result,
	const FMCPGeoRequest& Request,
	const FString& TargetPath,
	const TCHAR* OperationName)
{
	Result->SetBoolField(TEXT("rollbackAvailable"), false);
	// Two ways to reach here, and they need different advice: an inPlace edit
	// overwrote the source, and an onConflict='replace' write overwrote an
	// asset the caller named. Telling the second one to "omit inPlace" would be
	// advice about a parameter they did not pass.
	FString Reason = Request.bInPlace
		? FString::Printf(
			TEXT("%s rewrote the geometry of '%s' in place and the original triangles are gone, so there is ")
				TEXT("no inverse call that restores them. Omit inPlace so the result lands in a separate ")
				TEXT("asset, which an asset(delete) rollback reverses completely."),
			OperationName, *TargetPath)
		: FString::Printf(
			TEXT("%s wrote over the existing asset at '%s' (onConflict='replace') and the geometry that was ")
				TEXT("there is gone, so there is no inverse call that restores it. Name an outputPath that ")
				TEXT("does not exist yet and the rollback is a complete asset(delete)."),
			OperationName, *TargetPath);
	if (!Request.BackupPath.IsEmpty())
	{
		Reason += FString::Printf(
			TEXT(" A copy of the pre-edit mesh was written to '%s': restore it with asset(delete, assetPath='%s') ")
				TEXT("followed by asset(duplicate, sourcePath='%s', destinationPath='%s')."),
			*Request.BackupPath, *TargetPath, *Request.BackupPath, *TargetPath);
	}
	else if (Request.bInPlace)
	{
		Reason += TEXT(" Pass backupPath to have the source copied aside before the next in-place edit.");
	}
	Result->SetStringField(TEXT("rollbackUnavailableReason"), Reason);
}

/** Copy the source aside before a destructive in-place edit. Best effort and
 *  reported either way: a backup that silently did not happen is worse than no
 *  backup at all, because the caller would rely on it. */
void MCPGeoWriteBackup(const TSharedPtr<FJsonObject>& Result, const FMCPGeoRequest& Request)
{
	if (Request.BackupPath.IsEmpty()) return;

	Result->SetStringField(TEXT("backupPath"), Request.BackupPath);
	if (MCPLoadAssetObject(Request.BackupPath) != nullptr)
	{
		Result->SetBoolField(TEXT("backupWritten"), false);
		Result->SetStringField(TEXT("backupSkippedReason"), FString::Printf(
			TEXT("'%s' already exists, so it was left alone rather than overwritten with the mesh as it is now. ")
				TEXT("Name an unused backupPath, or delete the existing one first."),
			*Request.BackupPath));
		return;
	}

	UObject* Copy = UEditorAssetLibrary::DuplicateAsset(Request.AssetPath, Request.BackupPath);
	Result->SetBoolField(TEXT("backupWritten"), Copy != nullptr);
	if (!Copy)
	{
		Result->SetStringField(TEXT("backupSkippedReason"), FString::Printf(
			TEXT("Copying '%s' to '%s' failed, so no backup exists. The edit below still ran."),
			*Request.AssetPath, *Request.BackupPath));
	}
	else if (UStaticMesh* CopyMesh = Cast<UStaticMesh>(Copy))
	{
		FString SaveReason;
		SaveAssetPackageChecked(CopyMesh, SaveReason);
	}
}

/**
 * Everything the five rewriting actions do around their one distinguishing
 * call: bind the plugin, build the dynamic mesh, read the before-stats, run
 * `Operate`, read the after-stats, and either write or report a dry run.
 *
 * `Operate` gets the dynamic mesh, the debug object and the message list, and
 * returns false with a filled `OutFailure` to abort before anything is written.
 * `Describe` adds the action's own fields to whichever result comes back, so a
 * dry run and a real write report the same shape.
 */
TSharedPtr<FJsonValue> MCPGeoRunMeshOperation(
	const FMCPGeoRequest& RequestIn,
	const TCHAR* OperationName,
	TFunctionRef<bool(UObject* /*Mesh*/, UObject* /*Debug*/, TArray<FString>& /*Messages*/, FString& /*OutFailure*/)> Operate,
	TFunctionRef<void(const TSharedPtr<FJsonObject>&)> Describe,
	TFunctionRef<bool(const FMCPGeoMeshStats& /*Before*/, const FMCPGeoMeshStats& /*After*/)> DidChange,
	TFunctionRef<void(const TSharedPtr<FJsonObject>& /*Out*/, bool /*bCreated*/, const FString& /*TargetPath*/)> Finish)
{
	FMCPGeoRequest Request = RequestIn;

	if (!MCPGeoEnsureGeometryScripting())
	{
		return MCPGeoUnavailable(TEXT("its runtime classes are not registered in this editor."));
	}
	Request.LODTypeValue = MCPGeoEnumValue(MCPGeoLODTypeEnum, Request.LODTypeName);

	UClass* DynamicMeshClass = FindObject<UClass>(nullptr, MCPGeoDynamicMeshClass);
	if (!DynamicMeshClass)
	{
		return MCPGeoUnavailable(TEXT("UDynamicMesh is not registered."));
	}
	UObject* Dynamic = NewObject<UObject>(GetTransientPackage(), DynamicMeshClass);
	if (!Dynamic)
	{
		return MCPGeoUnavailable(TEXT("a UDynamicMesh could not be constructed."));
	}
	const FGCRootScope KeepDynamic(Dynamic);

	UObject* Debug = nullptr;
	if (UClass* DebugClass = FindObject<UClass>(nullptr, MCPGeoDebugClass))
	{
		Debug = NewObject<UObject>(GetTransientPackage(), DebugClass);
	}
	const FGCRootScope KeepDebug(Debug);

	TArray<FString> Messages;
	FString Failure;

	if (!MCPGeoCopyMeshIn(Request, Dynamic, Debug, Messages, Failure))
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(TEXT("Source mesh could not be read: %s"), *Failure));
		Obj->SetStringField(TEXT("reason"), TEXT("source_read_failed"));
		MCPGeoDescribeRequest(Obj, Request);
		MCPGeoAttachMessages(Obj, Messages);
		return MakeShared<FJsonValueObject>(Obj);
	}

	const FMCPGeoMeshStats Before = MCPGeoReadStats(Dynamic);

	if (!Operate(Dynamic, Debug, Messages, Failure))
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%s failed on '%s': %s"), OperationName, *Request.AssetPath, *Failure));
		Obj->SetStringField(TEXT("reason"), TEXT("operation_failed"));
		MCPGeoDescribeRequest(Obj, Request);
		Obj->SetObjectField(TEXT("before"), Before.ToJson());
		Describe(Obj);
		MCPGeoAttachMessages(Obj, Messages);
		return MakeShared<FJsonValueObject>(Obj);
	}

	const FMCPGeoMeshStats After = MCPGeoReadStats(Dynamic);

	if (After.Triangles == 0)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%s left '%s' with no triangles at all, so nothing was written. The parameters removed the ")
				TEXT("whole mesh rather than reshaping it."),
			OperationName, *Request.AssetPath));
		Obj->SetStringField(TEXT("reason"), TEXT("empty_result"));
		MCPGeoDescribeRequest(Obj, Request);
		Obj->SetObjectField(TEXT("before"), Before.ToJson());
		Obj->SetObjectField(TEXT("after"), After.ToJson());
		Describe(Obj);
		MCPGeoAttachMessages(Obj, Messages);
		return MakeShared<FJsonValueObject>(Obj);
	}

	const bool bChanged = DidChange(Before, After);

	// Nothing moved. Report it and skip the write, which is what makes a repeat
	// of an already-applied operation cheap as well as safe.
	if (!bChanged && Request.bInPlace)
	{
		auto Result = MCPSuccess();
		MCPSetExisted(Result);
		MCPGeoDescribeRequest(Result, Request);
		Result->SetObjectField(TEXT("before"), Before.ToJson());
		Result->SetObjectField(TEXT("after"), After.ToJson());
		Result->SetBoolField(TEXT("changed"), false);
		Result->SetBoolField(TEXT("written"), false);
		Result->SetStringField(TEXT("unchangedReason"), FString::Printf(
			TEXT("%s produced the mesh that is already there, so '%s' was left untouched."),
			OperationName, *Request.AssetPath));
		Describe(Result);
		MCPGeoAttachMessages(Result, Messages);
		return MCPResult(Result);
	}

	if (Request.bDryRun)
	{
		auto Preview = MCPSuccess();
		MCPGeoDescribeRequest(Preview, Request);
		Preview->SetObjectField(TEXT("before"), Before.ToJson());
		Preview->SetObjectField(TEXT("after"), After.ToJson());
		Preview->SetBoolField(TEXT("changed"), bChanged);
		Preview->SetBoolField(TEXT("dryRun"), true);
		Preview->SetBoolField(TEXT("written"), false);
		Preview->SetStringField(TEXT("wouldWrite"), Request.OutputPath);
		Describe(Preview);
		MCPGeoAttachMessages(Preview, Messages);
		return MCPResult(Preview);
	}

	auto Result = MCPSuccess();
	MCPGeoDescribeRequest(Result, Request);
	Result->SetObjectField(TEXT("before"), Before.ToJson());
	Result->SetObjectField(TEXT("after"), After.ToJson());
	Result->SetBoolField(TEXT("changed"), bChanged);
	Describe(Result);

	if (Request.bInPlace)
	{
		MCPGeoWriteBackup(Result, Request);
	}

	bool bCreated = false;
	FString WriteFailure;
	FString WriteReason;
	UStaticMesh* Written = MCPGeoWriteMeshOut(
		Request, Dynamic, Request.OutputPath, Request.ExistingOutput, Debug, Messages,
		bCreated, WriteFailure, WriteReason);

	if (!Written)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%s produced %d triangles but %s"), OperationName, After.Triangles, *WriteFailure));
		Result->SetStringField(TEXT("reason"), WriteReason);
		Result->SetBoolField(TEXT("written"), false);
		MCPGeoAttachMessages(Result, Messages);
		return MakeShared<FJsonValueObject>(Result);
	}

	MCPGeoFinishWrite(Result, Request, Written, Request.OutputPath, bCreated);
	// The idempotency marker and the rollback record are the handler's own,
	// stated in its body rather than buried here: which call undoes a
	// simplify is a claim only the action that made the edit can make.
	Finish(Result, bCreated, Request.OutputPath);
	MCPGeoAttachMessages(Result, Messages);
	return MCPResult(Result);
}

// ─── Fracture plane generation ───────────────────────────────────────────────

/** One cut plane, as the frame Geometry Script wants: the plane is the frame's
 *  XY plane and its +Z axis is the plane normal. */
FTransform MCPGeoPlaneFrame(const FVector& Point, const FVector& Normal)
{
	const FVector Unit = Normal.GetSafeNormal(UE_KINDA_SMALL_NUMBER, FVector::UpVector);
	return FTransform(FQuat::FindBetweenNormals(FVector::UpVector, Unit), Point);
}

FVector MCPGeoAxisVector(const FString& Axis)
{
	if (Axis == TEXT("x")) return FVector::ForwardVector;
	if (Axis == TEXT("y")) return FVector::RightVector;
	return FVector::UpVector;
}

} // namespace

// ---------------------------------------------------------------------------
// asset(apply_mesh_simplify)
//
// Reduce triangle count while keeping the silhouette. Seven strategies, because
// "simplify" means different things: a target triangle or vertex count, a
// geometric tolerance, an edge length, or a structural collapse of planar
// regions or PolyGroup faces. The strategy is a parameter rather than seven
// actions because only the target term differs.
//
// asset(set_property) cannot do this: LOD reduction settings on a StaticMesh
// build a NEW LOD, they do not rewrite LOD 0's source geometry, and there is no
// UPROPERTY anywhere that means "collapse this mesh to 500 triangles".
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::SimplifyMesh(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPGeoRequest Request;
	if (auto Err = MCPGeoParseRequest(Params, TEXT("Simplified"), Request)) return Err;

	// The plugin has to be loaded BEFORE any enumerator is looked up: an unloaded
	// GeometryScriptingCore makes every FindObject<UEnum> miss, and a valid
	// method name would be rejected as unknown rather than reported as a missing
	// plugin.
	if (!MCPGeoEnsureGeometryScripting())
	{
		return MCPGeoUnavailable(TEXT("its runtime classes are not registered in this editor."));
	}

	const FString Mode = OptionalString(Params, TEXT("simplifyMode"), TEXT("triangleCount")).ToLower();
	const TCHAR* const KnownModes =
		TEXT("triangleCount, vertexCount, tolerance, edgeLength, clusterEdgeLength, planar, polygroup, ")
		TEXT("editorTriangleCount, editorVertexCount");
	static const TSet<FString> ValidModes = {
		TEXT("trianglecount"), TEXT("vertexcount"), TEXT("tolerance"), TEXT("edgelength"),
		TEXT("clusteredgelength"), TEXT("planar"), TEXT("polygroup"),
		TEXT("editortrianglecount"), TEXT("editorvertexcount")
	};
	if (!ValidModes.Contains(Mode))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown simplifyMode '%s'. Use one of: %s."), *Mode, KnownModes));
	}

	const int32 TriangleCount = OptionalInt(Params, TEXT("triangleCount"), 0);
	const int32 VertexCount = OptionalInt(Params, TEXT("vertexCount"), 0);
	const double Tolerance = OptionalNumber(Params, TEXT("tolerance"), 0.0);
	const double EdgeLength = OptionalNumber(Params, TEXT("edgeLength"), 0.0);
	const double AngleThreshold = OptionalNumber(Params, TEXT("angleThreshold"), 0.001);

	// Validate the target term BEFORE any mesh is read: a triangleCount of 0
	// would otherwise be "simplify until nothing is left" and be reported as an
	// empty result rather than as the parameter mistake it is.
	if (Mode == TEXT("trianglecount") || Mode == TEXT("editortrianglecount"))
	{
		if (TriangleCount <= 0)
		{
			return MCPError(FString::Printf(
				TEXT("simplifyMode='%s' needs a positive 'triangleCount'. Received %d."), *Mode, TriangleCount));
		}
	}
	else if (Mode == TEXT("vertexcount") || Mode == TEXT("editorvertexcount"))
	{
		if (VertexCount < 4)
		{
			return MCPError(FString::Printf(
				TEXT("simplifyMode='%s' needs a 'vertexCount' of at least 4, which is the fewest vertices a closed ")
					TEXT("surface can have. Received %d."),
				*Mode, VertexCount));
		}
	}
	else if (Mode == TEXT("tolerance"))
	{
		if (Tolerance <= 0.0)
		{
			return MCPError(FString::Printf(
				TEXT("simplifyMode='tolerance' needs a positive 'tolerance' in centimetres, the furthest the ")
					TEXT("simplified surface may drift from the original. Received %f."),
				Tolerance));
		}
	}
	else if (Mode == TEXT("edgelength") || Mode == TEXT("clusteredgelength"))
	{
		if (EdgeLength <= 0.0)
		{
			return MCPError(FString::Printf(
				TEXT("simplifyMode='%s' needs a positive 'edgeLength' in centimetres. Received %f."),
				*Mode, EdgeLength));
		}
	}

	const FString Method = OptionalString(Params, TEXT("method"), TEXT("AttributeAware"));
	const TCHAR* const KnownMethods = TEXT("StandardQEM, VolumePreserving, AttributeAware, AttributeAwareV2");
	const int64 MethodValue = MCPGeoEnumValue(MCPGeoSimplifyMethodEnum, Method);
	if (MethodValue == INDEX_NONE)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown simplify method '%s'. Use one of: %s."), *Method, KnownMethods));
	}

	const bool bAllowSeamCollapse = OptionalBool(Params, TEXT("allowSeamCollapse"), true);
	const bool bPreserveVertexPositions = OptionalBool(Params, TEXT("preserveVertexPositions"), false);
	const bool bAutoCompact = OptionalBool(Params, TEXT("autoCompact"), true);

	auto ApplyOptions = [&](FMCPGeoCall& Call)
	{
		Call.SetStructEnum(TEXT("Options"), TEXT("Method"), MethodValue);
		Call.SetStructBool(TEXT("Options"), TEXT("bAllowSeamCollapse"), bAllowSeamCollapse);
		Call.SetStructBool(TEXT("Options"), TEXT("bPreserveVertexPositions"), bPreserveVertexPositions);
		Call.SetStructBool(TEXT("Options"), TEXT("bAutoCompact"), bAutoCompact);
	};

	auto Operate = [&](UObject* Mesh, UObject* Debug, TArray<FString>& Messages, FString& OutFailure) -> bool
	{
		FString BindError;
		FMCPGeoCall Call;

		const TCHAR* FunctionName =
			Mode == TEXT("trianglecount")       ? TEXT("ApplySimplifyToTriangleCount") :
			Mode == TEXT("vertexcount")         ? TEXT("ApplySimplifyToVertexCount") :
			Mode == TEXT("tolerance")           ? TEXT("ApplySimplifyToTolerance") :
			Mode == TEXT("edgelength")          ? TEXT("ApplySimplifyToEdgeLength") :
			Mode == TEXT("clusteredgelength")   ? TEXT("ApplyClusterSimplifyToEdgeLength") :
			Mode == TEXT("planar")              ? TEXT("ApplySimplifyToPlanar") :
			Mode == TEXT("polygroup")           ? TEXT("ApplySimplifyToPolygroupTopology") :
			Mode == TEXT("editortrianglecount") ? TEXT("ApplyEditorSimplifyToTriangleCount") :
			                                      TEXT("ApplyEditorSimplifyToVertexCount");

		if (!Call.Bind(MCPGeoSimplifyFunctions, FunctionName, BindError))
		{
			OutFailure = BindError;
			return false;
		}
		Call.SetObject(TEXT("TargetMesh"), Mesh);

		if (Mode == TEXT("trianglecount") || Mode == TEXT("editortrianglecount"))
		{
			Call.SetNumber(TEXT("TriangleCount"), TriangleCount);
		}
		else if (Mode == TEXT("vertexcount") || Mode == TEXT("editorvertexcount"))
		{
			Call.SetNumber(TEXT("VertexCount"), VertexCount);
		}
		else if (Mode == TEXT("tolerance"))
		{
			Call.SetNumber(TEXT("Tolerance"), Tolerance);
		}
		else if (Mode == TEXT("edgelength") || Mode == TEXT("clusteredgelength"))
		{
			Call.SetNumber(TEXT("EdgeLength"), EdgeLength);
		}

		// The two structural modes take their own small option struct with an
		// angle threshold instead of the full simplifier options.
		if (Mode == TEXT("planar") || Mode == TEXT("polygroup"))
		{
			Call.SetStructNumber(TEXT("Options"), TEXT("AngleThreshold"), AngleThreshold);
			Call.SetStructBool(TEXT("Options"), TEXT("bAutoCompact"), bAutoCompact);
		}
		else if (Mode == TEXT("clusteredgelength"))
		{
			// Cluster simplification carries its own option struct with none of
			// the quadric fields in it, so its engine defaults are left alone
			// rather than half-written from options that do not apply.
		}
		else if (!Mode.StartsWith(TEXT("editor")))
		{
			ApplyOptions(Call);
		}

		Call.SetObject(TEXT("Debug"), Debug);
		Call.Invoke();
		Messages.Append(MCPGeoDrainDebug(Debug));
		return true;
	};

	auto Describe = [&](const TSharedPtr<FJsonObject>& Out)
	{
		Out->SetStringField(TEXT("simplifyMode"), Mode);
		if (!Mode.StartsWith(TEXT("editor")) && Mode != TEXT("planar") && Mode != TEXT("polygroup"))
		{
			Out->SetStringField(TEXT("method"), Method);
		}
		if (TriangleCount > 0) Out->SetNumberField(TEXT("triangleCount"), TriangleCount);
		if (VertexCount > 0) Out->SetNumberField(TEXT("vertexCount"), VertexCount);
		if (Tolerance > 0.0) Out->SetNumberField(TEXT("tolerance"), Tolerance);
		if (EdgeLength > 0.0) Out->SetNumberField(TEXT("edgeLength"), EdgeLength);
		Out->SetBoolField(TEXT("repeatIsIdempotent"), true);
		Out->SetStringField(TEXT("repeatNote"),
			TEXT("Simplifying an already-simplified mesh to the same target is a no-op: the second call finds ")
				TEXT("the counts already at or under the target and reports changed=false."));
	};

	auto DidChange = [](const FMCPGeoMeshStats& Before, const FMCPGeoMeshStats& After)
	{
		return Before.Triangles != After.Triangles || Before.Vertices != After.Vertices;
	};

	// A created output asset has an exact inverse: delete it. An in-place
	// rewrite has none, and says so rather than shipping an undo it cannot
	// honour.
	auto Finish = [&](const TSharedPtr<FJsonObject>& Out, bool bCreated, const FString& TargetPath)
	{
		if (bCreated)
		{
			MCPSetCreated(Out);
			Out->SetBoolField(TEXT("rollbackAvailable"), true);
			MCPSetDeleteAssetRollback(Out, TargetPath);
		}
		else
		{
			MCPSetUpdated(Out);
			MCPGeoExplainNoRollback(Out, Request, TargetPath, TEXT("apply_mesh_simplify"));
		}
	};

	return MCPGeoRunMeshOperation(Request, TEXT("apply_mesh_simplify"), Operate, Describe, DidChange, Finish);
}

// ---------------------------------------------------------------------------
// asset(apply_mesh_remesh)
//
// Rebuild the triangulation at a uniform or adaptive density. Different from
// simplify: simplify only removes, remesh splits AND collapses AND flips to
// reach an even edge length, which is what a mesh needs before deformation,
// baking or a physics conversion.
//
// Deliberately NOT idempotent, and it says so. Remeshing an already-remeshed
// mesh keeps moving vertices, so a repeat is a second edit rather than a no-op;
// the default separate-output form is what makes that safe.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::RemeshMesh(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPGeoRequest Request;
	if (auto Err = MCPGeoParseRequest(Params, TEXT("Remeshed"), Request)) return Err;

	// Load before resolving enumerators, for the reason given in apply_mesh_simplify.
	if (!MCPGeoEnsureGeometryScripting())
	{
		return MCPGeoUnavailable(TEXT("its runtime classes are not registered in this editor."));
	}

	const FString Mode = OptionalString(Params, TEXT("remeshMode"), TEXT("uniform")).ToLower();
	if (Mode != TEXT("uniform") && Mode != TEXT("adaptive"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown remeshMode '%s'. Use uniform (even edge lengths everywhere) or adaptive ")
				TEXT("(denser where the surface curves)."),
			*Mode));
	}

	const FString TargetType = OptionalString(Params, TEXT("targetType"), TEXT("TriangleCount"));
	const int64 TargetTypeValue = MCPGeoEnumValue(MCPGeoRemeshTargetEnum, TargetType);
	if (TargetTypeValue == INDEX_NONE)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown remesh targetType '%s'. Use TriangleCount or TargetEdgeLength."), *TargetType));
	}

	const int32 TargetTriangleCount = OptionalInt(Params, TEXT("targetTriangleCount"), 1000);
	const double TargetEdgeLength = OptionalNumber(Params, TEXT("targetEdgeLength"), 1.0);
	if (TargetType.Equals(TEXT("TriangleCount"), ESearchCase::IgnoreCase) && TargetTriangleCount <= 0)
	{
		return MCPError(FString::Printf(
			TEXT("targetType='TriangleCount' needs a positive 'targetTriangleCount'. Received %d."),
			TargetTriangleCount));
	}
	if (TargetType.Equals(TEXT("TargetEdgeLength"), ESearchCase::IgnoreCase) && TargetEdgeLength <= 0.0)
	{
		return MCPError(FString::Printf(
			TEXT("targetType='TargetEdgeLength' needs a positive 'targetEdgeLength' in centimetres. Received %f."),
			TargetEdgeLength));
	}

	const FString Smoothing = OptionalString(Params, TEXT("smoothingType"), TEXT("Mixed"));
	const int64 SmoothingValue = MCPGeoEnumValue(MCPGeoRemeshSmoothingEnum, Smoothing);
	if (SmoothingValue == INDEX_NONE)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown smoothingType '%s'. Use Uniform (most regular triangles, UVs ignored), ")
				TEXT("UVPreserving, or Mixed."),
			*Smoothing));
	}

	const FString BoundaryConstraint = OptionalString(Params, TEXT("boundaryConstraint"), TEXT("Free"));
	const int64 BoundaryValue = MCPGeoEnumValue(MCPGeoRemeshConstraintEnum, BoundaryConstraint);
	if (BoundaryValue == INDEX_NONE)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown boundaryConstraint '%s'. Use Fixed, Refine, Free or Ignore."), *BoundaryConstraint));
	}

	const double SmoothingRate = FMath::Clamp(OptionalNumber(Params, TEXT("smoothingRate"), 0.25), 0.0, 1.0);
	const int32 Iterations = FMath::Max(0, OptionalInt(Params, TEXT("iterations"), 0));
	const bool bDiscardAttributes = OptionalBool(Params, TEXT("discardAttributes"), false);
	const bool bReproject = OptionalBool(Params, TEXT("reprojectToInputMesh"), true);
	const double RelativeDensity = OptionalNumber(Params, TEXT("relativeDensity"), 0.0);

	auto Operate = [&](UObject* Mesh, UObject* Debug, TArray<FString>& Messages, FString& OutFailure) -> bool
	{
		FString BindError;
		FMCPGeoCall Call;
		const TCHAR* FunctionName = Mode == TEXT("uniform")
			? TEXT("ApplyUniformRemesh") : TEXT("ApplyAdaptiveRemesh");
		if (!Call.Bind(MCPGeoRemeshFunctions, FunctionName, BindError))
		{
			OutFailure = BindError;
			return false;
		}
		Call.SetObject(TEXT("TargetMesh"), Mesh);

		Call.SetStructBool(TEXT("RemeshOptions"), TEXT("bDiscardAttributes"), bDiscardAttributes);
		Call.SetStructBool(TEXT("RemeshOptions"), TEXT("bReprojectToInputMesh"), bReproject);
		Call.SetStructEnum(TEXT("RemeshOptions"), TEXT("SmoothingType"), SmoothingValue);
		Call.SetStructNumber(TEXT("RemeshOptions"), TEXT("SmoothingRate"), SmoothingRate);
		Call.SetStructEnum(TEXT("RemeshOptions"), TEXT("MeshBoundaryConstraint"), BoundaryValue);
		if (Iterations > 0)
		{
			Call.SetStructNumber(TEXT("RemeshOptions"), TEXT("NumIterations"), Iterations);
		}

		const TCHAR* OptionsName = Mode == TEXT("uniform") ? TEXT("UniformOptions") : TEXT("AdaptiveOptions");
		Call.SetStructEnum(OptionsName, TEXT("TargetType"), TargetTypeValue);
		Call.SetStructNumber(OptionsName, TEXT("TargetTriangleCount"), TargetTriangleCount);
		Call.SetStructNumber(OptionsName, TEXT("TargetEdgeLength"), TargetEdgeLength);
		if (Mode == TEXT("adaptive"))
		{
			Call.SetStructNumber(OptionsName, TEXT("RelativeDensity"), RelativeDensity);
		}

		Call.SetObject(TEXT("Debug"), Debug);
		Call.Invoke();
		Messages.Append(MCPGeoDrainDebug(Debug));
		return true;
	};

	auto Describe = [&](const TSharedPtr<FJsonObject>& Out)
	{
		Out->SetStringField(TEXT("remeshMode"), Mode);
		Out->SetStringField(TEXT("targetType"), TargetType);
		Out->SetNumberField(TEXT("targetTriangleCount"), TargetTriangleCount);
		Out->SetNumberField(TEXT("targetEdgeLength"), TargetEdgeLength);
		Out->SetStringField(TEXT("smoothingType"), Smoothing);
		Out->SetStringField(TEXT("boundaryConstraint"), BoundaryConstraint);
		Out->SetBoolField(TEXT("repeatIsIdempotent"), false);
		Out->SetStringField(TEXT("repeatNote"),
			TEXT("Remeshing keeps moving vertices, so a second call on the same mesh is a second edit, not a ")
				TEXT("no-op. Leave inPlace off so each call writes its own asset, or check changed and the ")
				TEXT("before/after counts before repeating."));
	};

	auto DidChange = [](const FMCPGeoMeshStats& Before, const FMCPGeoMeshStats& After)
	{
		return Before.Triangles != After.Triangles || Before.Vertices != After.Vertices;
	};

	// A created output asset has an exact inverse: delete it. An in-place
	// rewrite has none, and says so rather than shipping an undo it cannot
	// honour.
	auto Finish = [&](const TSharedPtr<FJsonObject>& Out, bool bCreated, const FString& TargetPath)
	{
		if (bCreated)
		{
			MCPSetCreated(Out);
			Out->SetBoolField(TEXT("rollbackAvailable"), true);
			MCPSetDeleteAssetRollback(Out, TargetPath);
		}
		else
		{
			MCPSetUpdated(Out);
			MCPGeoExplainNoRollback(Out, Request, TargetPath, TEXT("apply_mesh_remesh"));
		}
	};

	return MCPGeoRunMeshOperation(Request, TEXT("apply_mesh_remesh"), Operate, Describe, DidChange, Finish);
}

// ---------------------------------------------------------------------------
// asset(apply_mesh_mirror)
//
// Reflect the mesh across a plane, optionally cutting away the far side first
// and welding the seam. This is the "model half of it and mirror" workflow, and
// it is a real geometry operation rather than a negative scale: a negative
// scale on a component inverts the winding and lights wrong, and there is no
// UPROPERTY on a StaticMesh that mirrors its source geometry.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::MirrorMesh(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPGeoRequest Request;
	if (auto Err = MCPGeoParseRequest(Params, TEXT("Mirrored"), Request)) return Err;

	if (!MCPGeoEnsureGeometryScripting())
	{
		return MCPGeoUnavailable(TEXT("its runtime classes are not registered in this editor."));
	}

	// The plane is named either by an axis word (the common case) or by an
	// explicit point and normal.
	const FString Axis = OptionalString(Params, TEXT("axis"), TEXT("x")).ToLower();
	if (Axis != TEXT("x") && Axis != TEXT("y") && Axis != TEXT("z") && Axis != TEXT("custom"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown mirror axis '%s'. Use x, y, z, or custom with planeOrigin and planeNormal."), *Axis));
	}

	const FVector PlaneOrigin = OptionalVec3(Params, TEXT("planeOrigin"), FVector::ZeroVector);
	FVector PlaneNormal = Axis == TEXT("custom")
		? OptionalVec3(Params, TEXT("planeNormal"), FVector::ForwardVector)
		: MCPGeoAxisVector(Axis);
	if (PlaneNormal.IsNearlyZero())
	{
		return MCPError(TEXT("'planeNormal' is a zero vector, which names no plane. Give a direction, or use ")
			TEXT("axis=x|y|z for the axis-aligned planes through planeOrigin."));
	}
	PlaneNormal = PlaneNormal.GetSafeNormal();

	const bool bApplyPlaneCut = OptionalBool(Params, TEXT("applyPlaneCut"), true);
	const bool bFlipCutSide = OptionalBool(Params, TEXT("flipCutSide"), false);
	const bool bWeldAlongPlane = OptionalBool(Params, TEXT("weldAlongPlane"), true);

	auto Operate = [&](UObject* Mesh, UObject* Debug, TArray<FString>& Messages, FString& OutFailure) -> bool
	{
		FString BindError;
		FMCPGeoCall Call;
		if (!Call.Bind(MCPGeoBooleanFunctions, TEXT("ApplyMeshMirror"), BindError))
		{
			OutFailure = BindError;
			return false;
		}
		Call.SetObject(TEXT("TargetMesh"), Mesh);
		Call.SetTransform(TEXT("MirrorFrame"), MCPGeoPlaneFrame(PlaneOrigin, PlaneNormal));
		Call.SetStructBool(TEXT("Options"), TEXT("bApplyPlaneCut"), bApplyPlaneCut);
		Call.SetStructBool(TEXT("Options"), TEXT("bFlipCutSide"), bFlipCutSide);
		Call.SetStructBool(TEXT("Options"), TEXT("bWeldAlongPlane"), bWeldAlongPlane);
		Call.SetObject(TEXT("Debug"), Debug);
		Call.Invoke();
		Messages.Append(MCPGeoDrainDebug(Debug));
		return true;
	};

	auto Describe = [&](const TSharedPtr<FJsonObject>& Out)
	{
		Out->SetStringField(TEXT("axis"), Axis);
		Out->SetObjectField(TEXT("planeOrigin"), MCPVec3ToJsonObject(PlaneOrigin));
		Out->SetObjectField(TEXT("planeNormal"), MCPVec3ToJsonObject(PlaneNormal));
		Out->SetBoolField(TEXT("applyPlaneCut"), bApplyPlaneCut);
		Out->SetBoolField(TEXT("weldAlongPlane"), bWeldAlongPlane);
		Out->SetBoolField(TEXT("repeatIsIdempotent"), false);
		Out->SetStringField(TEXT("repeatNote"),
			TEXT("Mirroring a mirrored mesh doubles it again rather than returning the original, so a repeat ")
				TEXT("is a second edit. Leave inPlace off so each call writes its own asset."));
	};

	auto DidChange = [](const FMCPGeoMeshStats&, const FMCPGeoMeshStats&)
	{
		// A mirror always rewrites the geometry, including the degenerate case
		// of a mesh entirely on the cut side, which the empty-result guard in
		// the shared runner catches instead.
		return true;
	};

	// A created output asset has an exact inverse: delete it. An in-place
	// rewrite has none, and says so rather than shipping an undo it cannot
	// honour.
	auto Finish = [&](const TSharedPtr<FJsonObject>& Out, bool bCreated, const FString& TargetPath)
	{
		if (bCreated)
		{
			MCPSetCreated(Out);
			Out->SetBoolField(TEXT("rollbackAvailable"), true);
			MCPSetDeleteAssetRollback(Out, TargetPath);
		}
		else
		{
			MCPSetUpdated(Out);
			MCPGeoExplainNoRollback(Out, Request, TargetPath, TEXT("apply_mesh_mirror"));
		}
	};

	return MCPGeoRunMeshOperation(Request, TEXT("apply_mesh_mirror"), Operate, Describe, DidChange, Finish);
}

// ---------------------------------------------------------------------------
// asset(apply_mesh_hole_fill)
//
// Close every open boundary loop, so the mesh becomes watertight. This is what
// a mesh needs before a boolean, a voxel operation, a convex decomposition or a
// physics conversion, and it is the single most common reason those fail.
//
// Welds first by default: a great many "holes" are not holes at all but
// duplicated vertices along a seam that no fill can close, and running the fill
// alone on such a mesh reports zero holes filled while the mesh stays open.
// Genuinely idempotent: a mesh with nothing left to fill reports existed=true
// and is not rewritten.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::FillMeshHoles(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPGeoRequest Request;
	if (auto Err = MCPGeoParseRequest(Params, TEXT("Filled"), Request)) return Err;

	// Load before resolving enumerators, for the reason given in apply_mesh_simplify.
	if (!MCPGeoEnsureGeometryScripting())
	{
		return MCPGeoUnavailable(TEXT("its runtime classes are not registered in this editor."));
	}

	const FString Method = OptionalString(Params, TEXT("fillMethod"), TEXT("Automatic"));
	const int64 MethodValue = MCPGeoEnumValue(MCPGeoFillHolesEnum, Method);
	if (MethodValue == INDEX_NONE)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown fillMethod '%s'. Use Automatic, MinimalFill, PolygonTriangulation, TriangleFan ")
				TEXT("or PlanarProjection."),
			*Method));
	}

	const bool bDeleteIsolatedTriangles = OptionalBool(Params, TEXT("deleteIsolatedTriangles"), true);
	const bool bWeldFirst = OptionalBool(Params, TEXT("weldFirst"), true);
	const double WeldTolerance = OptionalNumber(Params, TEXT("weldTolerance"), 1.0e-6);
	const bool bRemoveDegenerateFirst = OptionalBool(Params, TEXT("removeDegenerateFirst"), false);

	int32 FilledHoles = 0;
	int32 FailedHoleFills = 0;
	int32 WeldedOpenEdges = 0;

	auto Operate = [&](UObject* Mesh, UObject* Debug, TArray<FString>& Messages, FString& OutFailure) -> bool
	{
		FString BindError;

		if (bRemoveDegenerateFirst)
		{
			FMCPGeoCall Repair;
			if (!Repair.Bind(MCPGeoRepairFunctions, TEXT("RepairMeshDegenerateGeometry"), BindError))
			{
				OutFailure = BindError;
				return false;
			}
			Repair.SetObject(TEXT("TargetMesh"), Mesh);
			Repair.SetObject(TEXT("Debug"), Debug);
			Repair.Invoke();
			Messages.Append(MCPGeoDrainDebug(Debug));
		}

		if (bWeldFirst)
		{
			const FMCPGeoMeshStats BeforeWeld = MCPGeoReadStats(Mesh);

			FMCPGeoCall Weld;
			if (!Weld.Bind(MCPGeoRepairFunctions, TEXT("WeldMeshEdges"), BindError))
			{
				OutFailure = BindError;
				return false;
			}
			Weld.SetObject(TEXT("TargetMesh"), Mesh);
			Weld.SetStructNumber(TEXT("WeldOptions"), TEXT("Tolerance"), WeldTolerance);
			Weld.SetObject(TEXT("Debug"), Debug);
			Weld.Invoke();
			Messages.Append(MCPGeoDrainDebug(Debug));

			const FMCPGeoMeshStats AfterWeld = MCPGeoReadStats(Mesh);
			WeldedOpenEdges = FMath::Max(0, BeforeWeld.OpenBorderEdges - AfterWeld.OpenBorderEdges);
		}

		FMCPGeoCall Fill;
		if (!Fill.Bind(MCPGeoRepairFunctions, TEXT("FillAllMeshHoles"), BindError))
		{
			OutFailure = BindError;
			return false;
		}
		Fill.SetObject(TEXT("TargetMesh"), Mesh);
		Fill.SetStructEnum(TEXT("FillOptions"), TEXT("FillMethod"), MethodValue);
		Fill.SetStructBool(TEXT("FillOptions"), TEXT("bDeleteIsolatedTriangles"), bDeleteIsolatedTriangles);
		Fill.SetObject(TEXT("Debug"), Debug);
		Fill.Invoke();
		Messages.Append(MCPGeoDrainDebug(Debug));

		FilledHoles = Fill.GetInt(TEXT("NumFilledHoles"));
		FailedHoleFills = Fill.GetInt(TEXT("NumFailedHoleFills"));
		return true;
	};

	auto Describe = [&](const TSharedPtr<FJsonObject>& Out)
	{
		Out->SetStringField(TEXT("fillMethod"), Method);
		Out->SetNumberField(TEXT("filledHoles"), FilledHoles);
		Out->SetNumberField(TEXT("failedHoleFills"), FailedHoleFills);
		Out->SetBoolField(TEXT("weldFirst"), bWeldFirst);
		Out->SetNumberField(TEXT("weldedOpenEdges"), WeldedOpenEdges);
		Out->SetBoolField(TEXT("repeatIsIdempotent"), true);
		Out->SetStringField(TEXT("repeatNote"),
			TEXT("A mesh with nothing left to fill reports existed=true and is not rewritten, so repeating ")
				TEXT("this call is safe and cheap."));
		if (FailedHoleFills > 0)
		{
			Out->SetStringField(TEXT("note"), FString::Printf(
				TEXT("%d boundary loop(s) could not be filled by the '%s' method. Try fillMethod='MinimalFill' ")
					TEXT("for a complex boundary, or 'PlanarProjection' for a flat one, and check ")
					TEXT("after.openBorderEdges to see how much boundary is left."),
				FailedHoleFills, *Method));
		}
	};

	auto DidChange = [&](const FMCPGeoMeshStats& Before, const FMCPGeoMeshStats& After)
	{
		return FilledHoles > 0
			|| WeldedOpenEdges > 0
			|| Before.Triangles != After.Triangles
			|| Before.Vertices != After.Vertices;
	};

	// A created output asset has an exact inverse: delete it. An in-place
	// rewrite has none, and says so rather than shipping an undo it cannot
	// honour.
	auto Finish = [&](const TSharedPtr<FJsonObject>& Out, bool bCreated, const FString& TargetPath)
	{
		if (bCreated)
		{
			MCPSetCreated(Out);
			Out->SetBoolField(TEXT("rollbackAvailable"), true);
			MCPSetDeleteAssetRollback(Out, TargetPath);
		}
		else
		{
			MCPSetUpdated(Out);
			MCPGeoExplainNoRollback(Out, Request, TargetPath, TEXT("apply_mesh_hole_fill"));
		}
	};

	return MCPGeoRunMeshOperation(Request, TEXT("apply_mesh_hole_fill"), Operate, Describe, DidChange, Finish);
}

// ---------------------------------------------------------------------------
// asset(generate_mesh_collision)
//
// Build simple collision shapes for a StaticMesh from its own geometry, or
// clear them. Eight generation methods, from axis-aligned boxes to a full
// convex decomposition.
//
// asset(set_property) cannot do this. UBodySetup::AggGeom is a UPROPERTY, but
// what would have to be written into it is the OUTPUT of a decomposition solver
// running over the mesh; there is no value a caller could supply. The read half
// already ships as asset(get_mesh_collision), and the shape count and trace
// flag stay ordinary property writes.
//
// op='clear' is the remove half, and it needs no plugin at all: emptying
// AggGeom is a direct edit of the body setup.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::GenerateMeshCollision(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	if (MCPIsProtectedAssetPath(AssetPath)) return MCPProtectedPathError(AssetPath);

	const FString Op = OptionalString(Params, TEXT("op"), TEXT("generate")).ToLower();
	if (Op != TEXT("generate") && Op != TEXT("clear"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown collision op '%s'. Use generate (build shapes from the mesh) or clear (remove every ")
				TEXT("simple collision shape)."),
			*Op));
	}

	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);

	UObject* AssetObject = MCPLoadAssetObject(AssetPath);
	UStaticMesh* Mesh = Cast<UStaticMesh>(AssetObject);
	if (!Mesh)
	{
		return AssetObject
			? MCPAssetWrongTypeError(AssetPath, AssetObject, TEXT("StaticMesh"))
			: MCPAssetNotFoundError(AssetPath, TEXT("Mesh"));
	}

	UBodySetup* Setup = Mesh->GetBodySetup();
	const int32 PreviousShapeCount = MCPGeoCollisionShapeCount(Setup);
	const FString PreviousSignature = MCPGeoCollisionSignature(Setup);

	// ── Clear ───────────────────────────────────────────────────────────────
	if (Op == TEXT("clear"))
	{
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetStringField(TEXT("op"), Op);
		Result->SetNumberField(TEXT("previousShapeCount"), PreviousShapeCount);
		Result->SetNumberField(TEXT("shapeCount"), 0);
		Result->SetBoolField(TEXT("repeatIsIdempotent"), true);

		if (PreviousShapeCount == 0)
		{
			MCPSetExisted(Result);
			Result->SetBoolField(TEXT("changed"), false);
			Result->SetStringField(TEXT("unchangedReason"), FString::Printf(
				TEXT("'%s' already has no simple collision shapes."), *AssetPath));
			return MCPResult(Result);
		}

		if (bDryRun)
		{
			Result->SetBoolField(TEXT("dryRun"), true);
			Result->SetBoolField(TEXT("changed"), false);
			Result->SetBoolField(TEXT("wouldRemoveShapes"), true);
			Result->SetStringField(TEXT("previousCollisionSignature"), PreviousSignature);
			return MCPResult(Result);
		}

		Setup->Modify();
		Setup->AggGeom.EmptyElements();
		Setup->InvalidatePhysicsData();
		Mesh->Modify();
		Mesh->PostEditChange();
		Mesh->MarkPackageDirty();

		MCPSetUpdated(Result);
		Result->SetBoolField(TEXT("changed"), true);
		Result->SetStringField(TEXT("previousCollisionSignature"), PreviousSignature);
		Result->SetBoolField(TEXT("rollbackAvailable"), false);
		Result->SetStringField(TEXT("rollbackUnavailableReason"), FString::Printf(
			TEXT("Clearing discarded %d collision shape(s) and no call puts those exact shapes back. ")
				TEXT("asset(generate_mesh_collision, op='generate') rebuilds collision from the mesh, which is ")
				TEXT("a new answer rather than the old one."),
			PreviousShapeCount));

		if (bSave)
		{
			FString SaveReason;
			const bool bSaved = SaveAssetPackageChecked(Mesh, SaveReason);
			MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveReason);
		}
		else
		{
			Result->SetBoolField(TEXT("saved"), false);
		}
		return MCPResult(Result);
	}

	// ── Generate ────────────────────────────────────────────────────────────
	const FString Method = OptionalString(Params, TEXT("method"), TEXT("ConvexHulls"));
	const TCHAR* const KnownMethods =
		TEXT("AlignedBoxes, OrientedBoxes, MinimalSpheres, Capsules, ConvexHulls, SweptHulls, ")
		TEXT("MinVolumeShapes, LevelSets");

	if (!MCPGeoEnsureGeometryScripting())
	{
		return MCPGeoUnavailable(
			TEXT("its runtime classes are not registered in this editor."),
			TEXT("asset(generate_mesh_collision, op='clear') and asset(get_mesh_collision) do not need it and ")
				TEXT("still work."));
	}

	const int64 MethodValue = MCPGeoEnumValue(MCPGeoCollisionMethodEnum, Method);
	if (MethodValue == INDEX_NONE)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown collision method '%s'. Use one of: %s."), *Method, KnownMethods));
	}

	const FString SweptHullAxis = OptionalString(Params, TEXT("sweptHullAxis"), TEXT("Z"));
	const int64 SweptHullAxisValue = MCPGeoEnumValue(MCPGeoSweptHullAxisEnum, SweptHullAxis);
	if (SweptHullAxisValue == INDEX_NONE)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown sweptHullAxis '%s'. Use X, Y, Z, SmallestBoxDimension or SmallestVolume."),
			*SweptHullAxis));
	}

	const int32 MaxHulls = FMath::Max(1, OptionalInt(Params, TEXT("maxConvexHulls"), 1));
	const int32 HullTargetFaceCount = FMath::Max(4, OptionalInt(Params, TEXT("hullTargetFaceCount"), 25));
	const int32 MaxShapeCount = FMath::Max(0, OptionalInt(Params, TEXT("maxShapeCount"), 0));
	const double MinThickness = OptionalNumber(Params, TEXT("minThickness"), 1.0);
	const bool bAutoDetectSpheres = OptionalBool(Params, TEXT("autoDetectSpheres"), true);
	const bool bAutoDetectBoxes = OptionalBool(Params, TEXT("autoDetectBoxes"), true);
	const bool bAutoDetectCapsules = OptionalBool(Params, TEXT("autoDetectCapsules"), true);
	const bool bSimplifyHulls = OptionalBool(Params, TEXT("simplifyHulls"), true);
	const bool bRemoveContained = OptionalBool(Params, TEXT("removeFullyContainedShapes"), true);
	const double DecompositionErrorTolerance = OptionalNumber(Params, TEXT("decompositionErrorTolerance"), 0.0);
	const double DecompositionSearchFactor = OptionalNumber(Params, TEXT("decompositionSearchFactor"), 0.5);
	const bool bMarkAsCustomized = OptionalBool(Params, TEXT("markAsCustomized"), true);

	FString RequestedLODType = OptionalString(Params, TEXT("lodType"));
	const FString LODTypeName = MCPGeoResolveLODType(RequestedLODType);
	if (LODTypeName.IsEmpty())
	{
		return MCPError(FString::Printf(
			TEXT("Unknown lodType '%s'. Use MaxAvailable, HiResSourceModel, SourceModel or RenderData."),
			*RequestedLODType));
	}

	FMCPGeoRequest ReadRequest;
	ReadRequest.AssetPath = AssetPath;
	ReadRequest.SourceMesh = Mesh;
	ReadRequest.LODTypeName = LODTypeName;
	ReadRequest.LODIndex = FMath::Max(0, OptionalInt(Params, TEXT("lodIndex"), 0));
	ReadRequest.LODTypeValue = MCPGeoEnumValue(MCPGeoLODTypeEnum, LODTypeName);

	UClass* DynamicMeshClass = FindObject<UClass>(nullptr, MCPGeoDynamicMeshClass);
	if (!DynamicMeshClass) return MCPGeoUnavailable(TEXT("UDynamicMesh is not registered."));
	UObject* Dynamic = NewObject<UObject>(GetTransientPackage(), DynamicMeshClass);
	if (!Dynamic) return MCPGeoUnavailable(TEXT("a UDynamicMesh could not be constructed."));
	const FGCRootScope KeepDynamic(Dynamic);

	UObject* Debug = nullptr;
	if (UClass* DebugClass = FindObject<UClass>(nullptr, MCPGeoDebugClass))
	{
		Debug = NewObject<UObject>(GetTransientPackage(), DebugClass);
	}
	const FGCRootScope KeepDebug(Debug);

	TArray<FString> Messages;
	FString Failure;
	if (!MCPGeoCopyMeshIn(ReadRequest, Dynamic, Debug, Messages, Failure))
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(TEXT("Mesh could not be read: %s"), *Failure));
		Obj->SetStringField(TEXT("reason"), TEXT("source_read_failed"));
		MCPGeoAttachMessages(Obj, Messages);
		return MakeShared<FJsonValueObject>(Obj);
	}

	const FMCPGeoMeshStats SourceStats = MCPGeoReadStats(Dynamic);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("op"), Op);
	Result->SetStringField(TEXT("method"), Method);
	Result->SetStringField(TEXT("lodType"), LODTypeName);
	Result->SetNumberField(TEXT("lodIndex"), ReadRequest.LODIndex);
	Result->SetNumberField(TEXT("previousShapeCount"), PreviousShapeCount);
	Result->SetStringField(TEXT("previousCollisionSignature"), PreviousSignature);
	Result->SetObjectField(TEXT("sourceMesh"), SourceStats.ToJson());
	Result->SetBoolField(TEXT("repeatIsIdempotent"), true);
	Result->SetStringField(TEXT("repeatNote"),
		TEXT("Generation from the same mesh with the same options is deterministic, so a repeat produces the ")
			TEXT("collision that is already there and reports changed=false without rewriting the asset. The ")
			TEXT("comparison is a structural signature (shape counts per kind, hull vertex counts, trace flag), ")
			TEXT("not a byte comparison."));

	if (bDryRun)
	{
		Result->SetBoolField(TEXT("dryRun"), true);
		Result->SetBoolField(TEXT("changed"), false);
		Result->SetStringField(TEXT("wouldWrite"), AssetPath);
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("Would replace the %d existing simple collision shape(s) on '%s' with a '%s' generation over ")
				TEXT("%d triangles. The shape count is only known once the solver has run, so it is not ")
				TEXT("predicted here."),
			PreviousShapeCount, *AssetPath, *Method, SourceStats.Triangles));
		MCPGeoAttachMessages(Result, Messages);
		return MCPResult(Result);
	}

	FString BindError;
	FMCPGeoCall Generate;
	if (!Generate.Bind(MCPGeoCollisionFunctions, TEXT("SetStaticMeshCollisionFromMesh"), BindError))
	{
		return MCPGeoUnavailable(BindError);
	}
	Generate.SetObject(TEXT("FromDynamicMesh"), Dynamic);
	Generate.SetObject(TEXT("ToStaticMeshAsset"), Mesh);
	Generate.SetStructEnum(TEXT("Options"), TEXT("Method"), MethodValue);
	Generate.SetStructBool(TEXT("Options"), TEXT("bEmitTransaction"), true);
	Generate.SetStructBool(TEXT("Options"), TEXT("bAutoDetectSpheres"), bAutoDetectSpheres);
	Generate.SetStructBool(TEXT("Options"), TEXT("bAutoDetectBoxes"), bAutoDetectBoxes);
	Generate.SetStructBool(TEXT("Options"), TEXT("bAutoDetectCapsules"), bAutoDetectCapsules);
	Generate.SetStructNumber(TEXT("Options"), TEXT("MinThickness"), MinThickness);
	Generate.SetStructBool(TEXT("Options"), TEXT("bSimplifyHulls"), bSimplifyHulls);
	Generate.SetStructNumber(TEXT("Options"), TEXT("ConvexHullTargetFaceCount"), HullTargetFaceCount);
	Generate.SetStructNumber(TEXT("Options"), TEXT("MaxConvexHullsPerMesh"), MaxHulls);
	Generate.SetStructNumber(TEXT("Options"), TEXT("ConvexDecompositionSearchFactor"), DecompositionSearchFactor);
	Generate.SetStructNumber(TEXT("Options"), TEXT("ConvexDecompositionErrorTolerance"), DecompositionErrorTolerance);
	Generate.SetStructEnum(TEXT("Options"), TEXT("SweptHullAxis"), SweptHullAxisValue);
	Generate.SetStructBool(TEXT("Options"), TEXT("bRemoveFullyContainedShapes"), bRemoveContained);
	Generate.SetStructNumber(TEXT("Options"), TEXT("MaxShapeCount"), MaxShapeCount);
	Generate.SetStructBool(TEXT("StaticMeshCollisionOptions"), TEXT("bMarkAsCustomized"), bMarkAsCustomized);
	Generate.SetObject(TEXT("Debug"), Debug);
	Generate.Invoke();
	Messages.Append(MCPGeoDrainDebug(Debug));

	UBodySetup* NewSetup = Mesh->GetBodySetup();
	const int32 ShapeCount = MCPGeoCollisionShapeCount(NewSetup);
	const FString Signature = MCPGeoCollisionSignature(NewSetup);
	const bool bChanged = Signature != PreviousSignature;

	Result->SetNumberField(TEXT("shapeCount"), ShapeCount);
	Result->SetStringField(TEXT("collisionSignature"), Signature);
	Result->SetBoolField(TEXT("changed"), bChanged);

	if (ShapeCount == 0)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("'%s' generation over '%s' produced no collision shapes at all, so the mesh now has none. ")
				TEXT("A mesh with open boundaries or zero-thickness geometry defeats most methods; run ")
				TEXT("asset(apply_mesh_hole_fill) first, or try method='ConvexHulls' which tolerates an open surface."),
			*Method, *AssetPath));
		Result->SetStringField(TEXT("reason"), TEXT("no_shapes_generated"));
		MCPGeoAttachMessages(Result, Messages);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (!bChanged)
	{
		MCPSetExisted(Result);
		Result->SetStringField(TEXT("unchangedReason"), FString::Printf(
			TEXT("'%s' already carried the collision this generation produces (%s)."),
			*AssetPath, *Signature));
	}
	else
	{
		MCPSetUpdated(Result);
		Mesh->MarkPackageDirty();

		if (PreviousShapeCount == 0)
		{
			// The mesh had no simple collision before this call, so clearing
			// what was just generated restores it exactly. That is a complete
			// inverse, not an approximation of one.
			Result->SetBoolField(TEXT("rollbackAvailable"), true);
			TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
			RollbackPayload->SetStringField(TEXT("assetPath"), AssetPath);
			RollbackPayload->SetStringField(TEXT("op"), TEXT("clear"));
			RollbackPayload->SetBoolField(TEXT("save"), bSave);
			MCPSetRollback(Result, TEXT("generate_mesh_collision"), RollbackPayload);
		}
		else
		{
			Result->SetBoolField(TEXT("rollbackAvailable"), false);
			Result->SetStringField(TEXT("rollbackUnavailableReason"), FString::Printf(
				TEXT("Generation replaced the %d shape(s) that were on '%s' (%s) and no call restores those ")
					TEXT("exact shapes. asset(generate_mesh_collision, op='clear') removes what is there now, ")
					TEXT("and a re-run with the previous options rebuilds an equivalent set, but neither is ")
					TEXT("the old one. Generating onto a mesh that had no collision IS reversible, and that ")
					TEXT("case emits a rollback."),
				PreviousShapeCount, *AssetPath, *PreviousSignature));
		}
	}

	if (bSave && bChanged)
	{
		FString SaveReason;
		const bool bSaved = SaveAssetPackageChecked(Mesh, SaveReason);
		MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveReason);
	}
	else
	{
		Result->SetBoolField(TEXT("saved"), false);
	}

	MCPGeoAttachMessages(Result, Messages);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// asset(apply_mesh_fracture)
//
// Cut a StaticMesh with planes and write each resulting piece out as its own
// StaticMesh asset, ready to be placed, simulated or destroyed individually.
//
// WHAT THIS IS NOT. It is not Chaos destruction: it does not produce a
// UGeometryCollection and there is no cluster hierarchy in the result. Every
// entry point for that (FFractureEngineFracturing::VoronoiFracture and its
// siblings in the Fracture plugin, FGeometryCollectionEngineConversion in
// GeometryCollectionEngine) is a plain C++ static with no UFUNCTION on it, so
// reflection cannot reach any of them and the only route would be a Build.cs
// dependency on two Experimental plugin modules, which would fail to link
// wherever those plugins are off. The result of this action says so in
// `producesGeometryCollection: false` rather than leaving it to be discovered.
//
// What it IS: the plane-slice fracture, which is what level geometry actually
// wants. A wall broken into six pieces, a floor split on a grid, a rock
// shattered along random planes.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::FractureMesh(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const FString Pattern = OptionalString(Params, TEXT("pattern"), TEXT("slice")).ToLower();
	if (Pattern != TEXT("slice") && Pattern != TEXT("grid") && Pattern != TEXT("random"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown fracture pattern '%s'. Use slice (parallel cuts along one axis), grid (cuts along ")
				TEXT("all three axes) or random (planes through the bounds, seeded)."),
			*Pattern));
	}

	const FString Axis = OptionalString(Params, TEXT("axis"), TEXT("z")).ToLower();
	if (Pattern == TEXT("slice") && Axis != TEXT("x") && Axis != TEXT("y") && Axis != TEXT("z"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown fracture axis '%s'. Use x, y or z."), *Axis));
	}

	const int32 Pieces = OptionalInt(Params, TEXT("pieces"), 4);
	const int32 GridX = FMath::Max(1, OptionalInt(Params, TEXT("gridX"), 2));
	const int32 GridY = FMath::Max(1, OptionalInt(Params, TEXT("gridY"), 2));
	const int32 GridZ = FMath::Max(1, OptionalInt(Params, TEXT("gridZ"), 1));
	const int32 PlaneCount = OptionalInt(Params, TEXT("planeCount"), 3);
	const int32 Seed = OptionalInt(Params, TEXT("seed"), 0);
	const double Jitter = FMath::Clamp(OptionalNumber(Params, TEXT("jitter"), 0.0), 0.0, 0.45);
	const double GapWidth = FMath::Max(0.0, OptionalNumber(Params, TEXT("gapWidth"), 0.01));
	const bool bFillHoles = OptionalBool(Params, TEXT("fillHoles"), true);
	const int32 MinPieceTriangles = FMath::Max(0, OptionalInt(Params, TEXT("minPieceTriangles"), 4));
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("error")).ToLower();
	if (OnConflict != TEXT("error") && OnConflict != TEXT("replace"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown onConflict '%s'. Use error (default) or replace."), *OnConflict));
	}

	if (Pattern == TEXT("slice") && Pieces < 2)
	{
		return MCPError(FString::Printf(
			TEXT("pattern='slice' needs 'pieces' of at least 2. Received %d."), Pieces));
	}
	if (Pattern == TEXT("random") && PlaneCount < 1)
	{
		return MCPError(FString::Printf(
			TEXT("pattern='random' needs 'planeCount' of at least 1. Received %d."), PlaneCount));
	}
	if (Pattern == TEXT("grid") && GridX * GridY * GridZ < 2)
	{
		return MCPError(FString::Printf(
			TEXT("pattern='grid' with gridX=%d, gridY=%d, gridZ=%d asks for a single piece, which is not a ")
				TEXT("fracture. At least one axis needs a count above 1."),
			GridX, GridY, GridZ));
	}

	const int32 PlannedPlanes =
		Pattern == TEXT("slice")  ? Pieces - 1 :
		Pattern == TEXT("grid")   ? (GridX - 1) + (GridY - 1) + (GridZ - 1) :
		                            PlaneCount;
	if (PlannedPlanes > MCPGeoMaxFracturePlanes)
	{
		return MCPError(FString::Printf(
			TEXT("This fracture would apply %d cut planes and the ceiling is %d. Each plane is a full pass ")
				TEXT("over the whole mesh, so a larger number would take the editor out of service for ")
				TEXT("minutes with no way to stop it. Fracture in stages, or lower the counts."),
			PlannedPlanes, MCPGeoMaxFracturePlanes));
	}

	const int32 PlannedPieces =
		Pattern == TEXT("grid") ? GridX * GridY * GridZ :
		Pattern == TEXT("slice") ? Pieces :
		PlaneCount + 1;
	if (PlannedPieces > MCPGeoMaxFracturePieces)
	{
		return MCPError(FString::Printf(
			TEXT("This fracture could write up to %d assets and the ceiling is %d. Lower the counts, or ")
				TEXT("fracture a piece at a time."),
			PlannedPieces, MCPGeoMaxFracturePieces));
	}

	// Where the pieces go: "<outputBasePath>_00", "_01", ... Derived from the
	// source when the caller does not name one.
	FString OutputBasePath = OptionalString(Params, TEXT("outputBasePath"));
	if (OutputBasePath.IsEmpty())
	{
		const FMCPAssetPathForms Forms = MCPAssetPathForms(AssetPath);
		if (Forms.PackagePath.IsEmpty())
		{
			return MCPError(FString::Printf(
				TEXT("'%s' is not a usable asset path, so no default outputBasePath could be derived from it. ")
					TEXT("Pass outputBasePath explicitly."),
				*AssetPath));
		}
		OutputBasePath = Forms.PackagePath + TEXT("_Piece");
	}
	if (MCPIsProtectedAssetPath(OutputBasePath)) return MCPProtectedPathError(OutputBasePath);

	FMCPGeoRequest Request;
	Request.AssetPath = AssetPath;
	Request.bSave = bSave;
	Request.bRecomputeNormals = OptionalBool(Params, TEXT("recomputeNormals"), false);
	Request.bRecomputeTangents = OptionalBool(Params, TEXT("recomputeTangents"), false);
	Request.bCopyMaterials = OptionalBool(Params, TEXT("copyMaterialsFromSource"), true);
	Request.bCopyCollision = false;
	Request.NaniteMode = OptionalString(Params, TEXT("nanite"), TEXT("inherit")).ToLower();
	if (Request.NaniteMode != TEXT("inherit") && Request.NaniteMode != TEXT("enable")
		&& Request.NaniteMode != TEXT("disable"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown nanite mode '%s'. Use inherit (copy the source's setting), enable or disable."),
			*Request.NaniteMode));
	}

	FString RequestedLODType = OptionalString(Params, TEXT("lodType"));
	Request.LODTypeName = MCPGeoResolveLODType(RequestedLODType);
	if (Request.LODTypeName.IsEmpty())
	{
		return MCPError(FString::Printf(
			TEXT("Unknown lodType '%s'. Use MaxAvailable, HiResSourceModel, SourceModel or RenderData."),
			*RequestedLODType));
	}
	Request.LODIndex = FMath::Max(0, OptionalInt(Params, TEXT("lodIndex"), 0));

	UObject* SourceObject = MCPLoadAssetObject(AssetPath);
	Request.SourceMesh = Cast<UStaticMesh>(SourceObject);
	if (!Request.SourceMesh)
	{
		return SourceObject
			? MCPAssetWrongTypeError(AssetPath, SourceObject, TEXT("StaticMesh"))
			: MCPAssetNotFoundError(AssetPath, TEXT("Source mesh"));
	}

	if (!MCPGeoEnsureGeometryScripting())
	{
		return MCPGeoUnavailable(TEXT("its runtime classes are not registered in this editor."));
	}
	Request.LODTypeValue = MCPGeoEnumValue(MCPGeoLODTypeEnum, Request.LODTypeName);

	UClass* DynamicMeshClass = FindObject<UClass>(nullptr, MCPGeoDynamicMeshClass);
	if (!DynamicMeshClass) return MCPGeoUnavailable(TEXT("UDynamicMesh is not registered."));
	UObject* Dynamic = NewObject<UObject>(GetTransientPackage(), DynamicMeshClass);
	if (!Dynamic) return MCPGeoUnavailable(TEXT("a UDynamicMesh could not be constructed."));
	const FGCRootScope KeepDynamic(Dynamic);

	UObject* Debug = nullptr;
	if (UClass* DebugClass = FindObject<UClass>(nullptr, MCPGeoDebugClass))
	{
		Debug = NewObject<UObject>(GetTransientPackage(), DebugClass);
	}
	const FGCRootScope KeepDebug(Debug);

	TArray<FString> Messages;
	FString Failure;
	if (!MCPGeoCopyMeshIn(Request, Dynamic, Debug, Messages, Failure))
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(TEXT("Source mesh could not be read: %s"), *Failure));
		Obj->SetStringField(TEXT("reason"), TEXT("source_read_failed"));
		MCPGeoAttachMessages(Obj, Messages);
		return MakeShared<FJsonValueObject>(Obj);
	}

	const FMCPGeoMeshStats Before = MCPGeoReadStats(Dynamic);

	FBox Bounds;
	if (!MCPGeoReadBounds(Dynamic, Bounds))
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("'%s' has no valid bounding box, so no cut planes could be placed inside it."), *AssetPath));
		Obj->SetStringField(TEXT("reason"), TEXT("no_bounds"));
		MCPGeoAttachMessages(Obj, Messages);
		return MakeShared<FJsonValueObject>(Obj);
	}

	// ── Place the cut planes, all of them, before anything is cut ───────────
	struct FMCPGeoPlane { FVector Point; FVector Normal; };
	TArray<FMCPGeoPlane> Planes;
	FRandomStream Random(Seed);
	const FVector Centre = Bounds.GetCenter();
	const FVector Extent = Bounds.GetExtent();

	auto AddAxisPlanes = [&](const FVector& Normal, double HalfSpan, int32 Count)
	{
		// Count pieces along this axis means Count-1 cuts, evenly spaced across
		// the span with the ends left alone.
		for (int32 Index = 1; Index < Count; ++Index)
		{
			double Fraction = static_cast<double>(Index) / static_cast<double>(Count);
			if (Jitter > 0.0)
			{
				Fraction += Random.FRandRange(-Jitter, Jitter) / static_cast<double>(Count);
				Fraction = FMath::Clamp(Fraction, 0.05, 0.95);
			}
			const double Offset = (Fraction * 2.0 - 1.0) * HalfSpan;
			Planes.Add({ Centre + Normal * Offset, Normal });
		}
	};

	if (Pattern == TEXT("slice"))
	{
		const FVector Normal = MCPGeoAxisVector(Axis);
		AddAxisPlanes(Normal, FVector::DotProduct(Extent.GetAbs(), Normal.GetAbs()), Pieces);
	}
	else if (Pattern == TEXT("grid"))
	{
		AddAxisPlanes(FVector::ForwardVector, Extent.X, GridX);
		AddAxisPlanes(FVector::RightVector, Extent.Y, GridY);
		AddAxisPlanes(FVector::UpVector, Extent.Z, GridZ);
	}
	else
	{
		for (int32 Index = 0; Index < PlaneCount; ++Index)
		{
			const FVector Normal = Random.GetUnitVector();
			const FVector Point = Centre + FVector(
				Random.FRandRange(-Extent.X * 0.6, Extent.X * 0.6),
				Random.FRandRange(-Extent.Y * 0.6, Extent.Y * 0.6),
				Random.FRandRange(-Extent.Z * 0.6, Extent.Z * 0.6));
			Planes.Add({ Point, Normal });
		}
	}

	if (Planes.Num() == 0)
	{
		return MCPError(TEXT("These parameters produce no cut planes at all, so there is nothing to fracture. ")
			TEXT("Raise 'pieces', one of the grid counts, or 'planeCount'."));
	}

	auto DescribePlanes = [&](const TSharedPtr<FJsonObject>& Out)
	{
		TArray<TSharedPtr<FJsonValue>> Json;
		for (const FMCPGeoPlane& Plane : Planes)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetObjectField(TEXT("point"), MCPVec3ToJsonObject(Plane.Point));
			Entry->SetObjectField(TEXT("normal"), MCPVec3ToJsonObject(Plane.Normal));
			Json.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Out->SetArrayField(TEXT("cutPlanes"), Json);
	};

	auto DescribeRequest = [&](const TSharedPtr<FJsonObject>& Out)
	{
		Out->SetStringField(TEXT("assetPath"), AssetPath);
		Out->SetStringField(TEXT("outputBasePath"), OutputBasePath);
		Out->SetStringField(TEXT("pattern"), Pattern);
		if (Pattern == TEXT("slice")) Out->SetStringField(TEXT("axis"), Axis);
		Out->SetNumberField(TEXT("cutPlaneCount"), Planes.Num());
		Out->SetNumberField(TEXT("seed"), Seed);
		Out->SetNumberField(TEXT("gapWidth"), GapWidth);
		Out->SetObjectField(TEXT("source"), Before.ToJson());
		Out->SetBoolField(TEXT("producesGeometryCollection"), false);
		Out->SetStringField(TEXT("fractureKind"), TEXT("plane_slice_static_meshes"));
		Out->SetStringField(TEXT("fractureNote"),
			TEXT("Each piece is written as its own StaticMesh asset. This is not Chaos destruction and no ")
				TEXT("UGeometryCollection is produced: the engine's fracture entry points are plain C++ ")
				TEXT("statics with no UFUNCTION on them, so the bridge cannot reach them by reflection and ")
				TEXT("linking against them would take the plugin down wherever the Fracture and ")
				TEXT("GeometryCollection plugins are disabled."));
		Out->SetBoolField(TEXT("repeatIsIdempotent"), true);
		Out->SetStringField(TEXT("repeatNote"),
			TEXT("The plane placement is seeded, so the same parameters produce the same pieces. A repeat with ")
				TEXT("onConflict='error' (the default) refuses rather than writing over the pieces already ")
				TEXT("there; onConflict='replace' overwrites them with the identical geometry."));
	};

	// ── Refuse a collision with existing pieces before cutting anything ─────
	if (OnConflict == TEXT("error"))
	{
		TArray<FString> Occupied;
		for (int32 Index = 0; Index < PlannedPieces; ++Index)
		{
			const FString PiecePath = FString::Printf(TEXT("%s_%02d"), *OutputBasePath, Index);
			if (MCPLoadAssetObject(PiecePath) != nullptr) Occupied.Add(PiecePath);
		}
		if (Occupied.Num() > 0)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetBoolField(TEXT("success"), false);
			Obj->SetStringField(TEXT("error"), FString::Printf(
				TEXT("%d asset(s) already occupy the piece paths this fracture would write, starting at '%s'. ")
					TEXT("Pass onConflict='replace' to overwrite them, or name a different outputBasePath."),
				Occupied.Num(), *Occupied[0]));
			Obj->SetStringField(TEXT("reason"), TEXT("output_exists"));
			Obj->SetArrayField(TEXT("occupiedPaths"), MCPStringListToJson(Occupied));
			DescribeRequest(Obj);
			return MakeShared<FJsonValueObject>(Obj);
		}
	}

	if (bDryRun)
	{
		auto Preview = MCPSuccess();
		DescribeRequest(Preview);
		DescribePlanes(Preview);
		Preview->SetBoolField(TEXT("dryRun"), true);
		Preview->SetBoolField(TEXT("written"), false);
		Preview->SetNumberField(TEXT("plannedPieceCount"), PlannedPieces);
		TArray<FString> WouldWrite;
		for (int32 Index = 0; Index < PlannedPieces; ++Index)
		{
			WouldWrite.Add(FString::Printf(TEXT("%s_%02d"), *OutputBasePath, Index));
		}
		Preview->SetArrayField(TEXT("wouldWrite"), MCPStringListToJson(WouldWrite));
		Preview->SetStringField(TEXT("plannedPieceCountNote"),
			TEXT("plannedPieceCount is what the plane layout implies. The cut itself decides the real count: a ")
				TEXT("plane that misses the geometry, or a concave shape that a single plane separates into ")
				TEXT("three parts, both change it. Run without dryRun to get the true count."));
		MCPGeoAttachMessages(Preview, Messages);
		return MCPResult(Preview);
	}

	// ── Cut ─────────────────────────────────────────────────────────────────
	FString BindError;
	for (const FMCPGeoPlane& Plane : Planes)
	{
		FMCPGeoCall Slice;
		if (!Slice.Bind(MCPGeoBooleanFunctions, TEXT("ApplyMeshPlaneSlice"), BindError))
		{
			return MCPGeoUnavailable(BindError);
		}
		Slice.SetObject(TEXT("TargetMesh"), Dynamic);
		Slice.SetTransform(TEXT("CutFrame"), MCPGeoPlaneFrame(Plane.Point, Plane.Normal));
		Slice.SetStructBool(TEXT("Options"), TEXT("bFillHoles"), bFillHoles);
		Slice.SetStructBool(TEXT("Options"), TEXT("bFillSpans"), true);
		Slice.SetStructNumber(TEXT("Options"), TEXT("GapWidth"), GapWidth);
		Slice.SetObject(TEXT("Debug"), Debug);
		Slice.Invoke();
		Messages.Append(MCPGeoDrainDebug(Debug));
	}

	// ── Split into pieces ───────────────────────────────────────────────────
	FMCPGeoCall Split;
	if (!Split.Bind(MCPGeoDecompFunctions, TEXT("SplitMeshByComponents"), BindError))
	{
		return MCPGeoUnavailable(BindError);
	}
	Split.SetObject(TEXT("TargetMesh"), Dynamic);
	Split.SetObject(TEXT("MeshPool"), nullptr);
	Split.SetObject(TEXT("Debug"), Debug);
	Split.Invoke();
	Messages.Append(MCPGeoDrainDebug(Debug));

	TArray<UObject*> PieceMeshes;
	Split.GetObjectArray(TEXT("ComponentMeshes"), PieceMeshes);

	// The split allocated fresh UDynamicMesh objects into a raw parameter frame,
	// which the garbage collector cannot see. Hold them for the rest of this
	// handler, because writing each piece out allocates and can collect.
	TArray<TStrongObjectPtr<UObject>> KeepPieces;
	KeepPieces.Reserve(PieceMeshes.Num());
	for (UObject* Piece : PieceMeshes) KeepPieces.Emplace(Piece);

	auto Result = MCPSuccess();
	DescribeRequest(Result);
	DescribePlanes(Result);
	Result->SetNumberField(TEXT("plannedPieceCount"), PlannedPieces);
	Result->SetNumberField(TEXT("producedPieceCount"), PieceMeshes.Num());

	if (PieceMeshes.Num() <= 1)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("The cuts left '%s' in %d connected piece(s), so there was nothing to write out. The planes ")
				TEXT("may all miss the geometry, or gapWidth may be too small to separate the halves; raise ")
				TEXT("gapWidth, or check cutPlanes against the source bounds."),
			*AssetPath, PieceMeshes.Num()));
		Result->SetStringField(TEXT("reason"), TEXT("no_separation"));
		MCPGeoAttachMessages(Result, Messages);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (PieceMeshes.Num() > MCPGeoMaxFracturePieces)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("The cuts separated '%s' into %d pieces, over the ceiling of %d, so nothing was written. ")
				TEXT("A mesh with many disconnected parts multiplies with every plane; lower the plane count, ")
				TEXT("or raise minPieceTriangles to discard the fragments."),
			*AssetPath, PieceMeshes.Num(), MCPGeoMaxFracturePieces));
		Result->SetStringField(TEXT("reason"), TEXT("too_many_pieces"));
		MCPGeoAttachMessages(Result, Messages);
		return MakeShared<FJsonValueObject>(Result);
	}

	// ── Write each piece ────────────────────────────────────────────────────
	TArray<TSharedPtr<FJsonValue>> PieceResults;
	TArray<FString> WrittenPaths;
	int32 SkippedTiny = 0;
	int32 FailedWrites = 0;
	int32 WrittenIndex = 0;

	for (UObject* Piece : PieceMeshes)
	{
		const FMCPGeoMeshStats PieceStats = MCPGeoReadStats(Piece);
		if (PieceStats.Triangles < MinPieceTriangles)
		{
			++SkippedTiny;
			continue;
		}

		const FString PiecePath = FString::Printf(TEXT("%s_%02d"), *OutputBasePath, WrittenIndex);
		UStaticMesh* Existing = Cast<UStaticMesh>(MCPLoadAssetObject(PiecePath));

		bool bCreated = false;
		FString WriteFailure;
		FString WriteReason;
		UStaticMesh* Written = MCPGeoWriteMeshOut(
			Request, Piece, PiecePath, Existing, Debug, Messages, bCreated, WriteFailure, WriteReason);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("index"), WrittenIndex);
		Entry->SetStringField(TEXT("assetPath"), PiecePath);
		Entry->SetObjectField(TEXT("mesh"), PieceStats.ToJson());

		if (!Written)
		{
			++FailedWrites;
			Entry->SetBoolField(TEXT("ok"), false);
			Entry->SetStringField(TEXT("status"), WriteReason);
			Entry->SetStringField(TEXT("error"), WriteFailure);
			PieceResults.Add(MakeShared<FJsonValueObject>(Entry));
			++WrittenIndex;
			continue;
		}

		TSharedPtr<FJsonObject> PieceWrite = MakeShared<FJsonObject>();
		MCPGeoFinishWrite(PieceWrite, Request, Written, PiecePath, bCreated);
		Entry->SetBoolField(TEXT("ok"), true);
		Entry->SetStringField(TEXT("status"), bCreated ? TEXT("created") : TEXT("updated"));
		bool bPieceSaved = false;
		PieceWrite->TryGetBoolField(TEXT("saved"), bPieceSaved);
		Entry->SetBoolField(TEXT("saved"), bPieceSaved);
		PieceResults.Add(MakeShared<FJsonValueObject>(Entry));
		WrittenPaths.Add(PiecePath);
		++WrittenIndex;
	}

	Result->SetArrayField(TEXT("pieces"), PieceResults);
	Result->SetNumberField(TEXT("writtenPieceCount"), WrittenPaths.Num());
	Result->SetNumberField(TEXT("skippedTinyPieceCount"), SkippedTiny);
	Result->SetNumberField(TEXT("failedPieceCount"), FailedWrites);
	Result->SetBoolField(TEXT("written"), WrittenPaths.Num() > 0);
	Result->SetBoolField(TEXT("changed"), WrittenPaths.Num() > 0);
	Result->SetBoolField(TEXT("sourceModified"), false);
	Result->SetStringField(TEXT("sourceNote"), FString::Printf(
		TEXT("'%s' was not touched. The pieces are separate assets; delete the source yourself if the ")
			TEXT("fracture replaces it."),
		*AssetPath));

	if (WrittenPaths.Num() > 0) MCPSetCreated(Result);
	if (FailedWrites > 0)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d of %d pieces could not be written; see pieces[] for each one. The %d that did land are ")
				TEXT("listed in writtenPaths and are safe to delete."),
			FailedWrites, PieceResults.Num(), WrittenPaths.Num()));
	}

	Result->SetArrayField(TEXT("writtenPaths"), MCPStringListToJson(WrittenPaths));
	if (WrittenPaths.Num() > 0)
	{
		// The inverse of "this call created N assets" is deleting exactly those
		// N, which is complete: the source was never modified.
		Result->SetBoolField(TEXT("rollbackAvailable"), true);
		TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
		RollbackPayload->SetArrayField(TEXT("assetPaths"), MCPStringListToJson(WrittenPaths));
		RollbackPayload->SetBoolField(TEXT("force"), true);
		MCPSetRollback(Result, TEXT("delete_asset_batch"), RollbackPayload);
	}

	MCPGeoAttachMessages(Result, Messages);
	return MCPResult(Result);
}
