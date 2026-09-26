#pragma once

// Plumbing shared by the handlers that drive Geometry Script by reflection
// (mesh boolean, mesh repair/remesh/collision, UV unwrap). The plugin is not
// linked, so every entry point is a UFUNCTION reached through FMCPReflectedCall;
// this header holds the class paths, module loading, the typed "plugin not
// available" refusal and the debug-message plumbing those handlers share.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "HandlerFunctionCall.h"
#include "HandlerUtils.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/UnrealType.h"

namespace MCPGeometryScript
{
	inline const TCHAR* const DynamicMeshClass     = TEXT("/Script/GeometryFramework.DynamicMesh");
	inline const TCHAR* const DebugClass           = TEXT("/Script/GeometryScriptingCore.GeometryScriptDebug");
	inline const TCHAR* const StaticMeshFunctions  = TEXT("/Script/GeometryScriptingCore.GeometryScriptLibrary_StaticMeshFunctions");
	inline const TCHAR* const QueryFunctions       = TEXT("/Script/GeometryScriptingCore.GeometryScriptLibrary_MeshQueryFunctions");
	inline const TCHAR* const BooleanFunctions     = TEXT("/Script/GeometryScriptingCore.GeometryScriptLibrary_MeshBooleanFunctions");
	inline const TCHAR* const CreateAssetFunctions = TEXT("/Script/GeometryScriptingEditor.GeometryScriptLibrary_CreateNewAssetFunctions");
	inline const TCHAR* const LODTypeEnum          = TEXT("/Script/GeometryScriptingCore.EGeometryScriptLODType");

	/** Load the Geometry Script modules if the project has them, then answer
	 *  whether UDynamicMesh and every class in RequiredClasses is registered.
	 *  LoadModule, not LoadModuleChecked: a project without the plugin must get
	 *  an answer, not a crashed editor. */
	inline bool EnsureLoaded(std::initializer_list<const TCHAR*> RequiredClasses)
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
				FModuleManager::Get().LoadModule(ModuleName);
			}
		}
		if (!FindObject<UClass>(nullptr, DynamicMeshClass)) return false;
		for (const TCHAR* ClassPath : RequiredClasses)
		{
			if (!FindObject<UClass>(nullptr, ClassPath)) return false;
		}
		return true;
	}

	/** The typed refusal for a project without the plugin. StillWorks names the
	 *  actions in the same area that do not need it, so the answer is a route
	 *  forward rather than a dead end. */
	inline TSharedPtr<FJsonValue> UnavailableError(const FString& Detail, const FString& StillWorks = FString())
	{
		FString Message = FString::Printf(
			TEXT("GeometryScripting plugin not available: %s Enable the 'Geometry Script' plugin ")
				TEXT("(Edit > Plugins > Geometry Script) and restart the editor, then retry."),
			*Detail);
		if (!StillWorks.IsEmpty())
		{
			Message += TEXT(" ") + StillWorks;
		}
		TSharedPtr<FJsonObject> Obj = MCPErrorObject(Message);
		Obj->SetStringField(TEXT("reason"), TEXT("geometry_scripting_unavailable"));
		Obj->SetStringField(TEXT("requiredPlugin"), TEXT("GeometryScripting"));
		return MakeShared<FJsonValueObject>(Obj);
	}

	/** A value from a UEnum this module does not link, by enumerator name.
	 *  INDEX_NONE when the enum or the enumerator is absent, so a renamed
	 *  enumerator is reported rather than silently read as ordinal 0. */
	inline int64 EnumValue(const TCHAR* EnumPath, const FString& EnumeratorName)
	{
		UEnum* Enum = FindObject<UEnum>(nullptr, EnumPath);
		if (!Enum) return INDEX_NONE;
		return Enum->GetValueByNameString(EnumeratorName);
	}

	/** Read and clear the messages a UGeometryScriptDebug collected, so the
	 *  reason an operation failed reaches the caller, not only the output log. */
	inline TArray<FString> DrainDebug(UObject* Debug)
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

	inline void AttachMessages(const TSharedPtr<FJsonObject>& Out, const TArray<FString>& Messages)
	{
		if (Messages.Num() == 0) return;
		Out->SetArrayField(TEXT("geometryScriptMessages"), MCPStringListToJson(Messages));
	}

	/** One integer-returning query on a DynamicMesh (GetNumTriangleIDs and
	 *  friends). 0 when the query library or the function is missing. */
	inline int32 QueryMeshInt(UObject* Mesh, const TCHAR* FunctionName)
	{
		if (!Mesh) return 0;
		FString Error;
		FMCPReflectedCall Call;
		if (!Call.Bind(QueryFunctions, FunctionName, Error)) return 0;
		Call.SetObject(TEXT("TargetMesh"), Mesh);
		Call.Invoke();
		return Call.GetInt(TEXT("ReturnValue"));
	}

	/** The LOD selector word, defaulting to the highest-quality source
	 *  available. Empty return means the word was not recognised. */
	inline FString ResolveLODType(const FString& Requested)
	{
		if (Requested.IsEmpty()) return TEXT("MaxAvailable");
		const FString Lower = Requested.ToLower();
		if (Lower == TEXT("maxavailable")) return TEXT("MaxAvailable");
		if (Lower == TEXT("hiressourcemodel")) return TEXT("HiResSourceModel");
		if (Lower == TEXT("sourcemodel")) return TEXT("SourceModel");
		if (Lower == TEXT("renderdata")) return TEXT("RenderData");
		return FString();
	}

	/** Copy the simple collision shapes and trace flag from one StaticMesh to
	 *  another, leaving the cook to the engine's lazy path. A generated result
	 *  with no collision is a silent trap for anything that walks on it. */
	inline bool CopySimpleCollision(UStaticMesh* From, UStaticMesh* To)
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
		TargetSetup->InvalidatePhysicsData();
		return true;
	}

	/** The written asset's own numbers under Out[Field], so the caller can
	 *  verify rather than trust. */
	inline void WriteAssetStats(const TSharedPtr<FJsonObject>& Out, const TCHAR* Field, UStaticMesh* Mesh)
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
}
