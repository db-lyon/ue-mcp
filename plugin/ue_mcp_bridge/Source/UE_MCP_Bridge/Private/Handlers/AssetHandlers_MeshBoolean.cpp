// StaticMesh boolean CSG (#916). See AssetHandlers_MeshBoolean.h for why this
// is its own translation unit and why GeometryScripting is reached by
// reflection rather than linked.
//
// Everything below drives four Geometry Script entry points that this module
// does not link against:
//
//   GeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMeshV2
//   GeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean
//   GeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh
//   GeometryScriptLibrary_CreateNewAssetFunctions::CreateNewStaticMeshAssetFromMesh
//
// They are UFUNCTIONs, so a reflected call reaches them without a Build.cs
// dependency on the plugin. FMCPReflectedCall (HandlerFunctionCall.h) is the whole of that
// machinery: allocate the function's parameter frame, initialise every
// parameter to its declared default (which is how the option structs get their
// engine defaults without this file knowing their layout), overwrite the ones
// this handler has an opinion about by NAME, ProcessEvent, read the outputs
// back, destroy the frame.
//
// Enum values are looked up through their UEnum by name rather than by
// hardcoded ordinal, so an engine that appends an enumerator cannot silently
// turn a Subtract into a TrimInside.

#include "AssetHandlers_MeshBoolean.h"

#include "HandlerFunctionCall.h"
#include "HandlerGeometryScript.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Engine/StaticMesh.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
// ── The Geometry Script surface this handler drives ──────────────────────────

const TCHAR* const GSBooleanOperationEnum   = TEXT("/Script/GeometryScriptingCore.EGeometryScriptBooleanOperation");

/** Count triangles and vertices on a DynamicMesh through Geometry Script's own
 *  query library, which is the only thing that can see inside it from here. */
bool ReadDynamicMeshCounts(UObject* Mesh, int32& OutTriangles, int32& OutVertices)
{
	OutTriangles = MCPGeometryScript::QueryMeshInt(Mesh, TEXT("GetNumTriangleIDs"));
	OutVertices = MCPGeometryScript::QueryMeshInt(Mesh, TEXT("GetNumVertexIDs"));
	return Mesh != nullptr;
}

/** Map the caller's operation word to the Geometry Script enumerator.
 *  Returns an empty string when the word is not one this action accepts. */
FString ResolveBooleanOperation(const FString& Requested)
{
	const FString Lower = Requested.ToLower();
	if (Lower == TEXT("union") || Lower == TEXT("add") || Lower == TEXT("merge")) return TEXT("Union");
	if (Lower == TEXT("subtract") || Lower == TEXT("difference") || Lower == TEXT("minus")) return TEXT("Subtract");
	if (Lower == TEXT("intersect") || Lower == TEXT("intersection")) return TEXT("Intersection");
	if (Lower == TEXT("triminside")) return TEXT("TrimInside");
	if (Lower == TEXT("trimoutside")) return TEXT("TrimOutside");
	if (Lower == TEXT("newpolygroupinside")) return TEXT("NewPolyGroupInside");
	if (Lower == TEXT("newpolygroupoutside")) return TEXT("NewPolyGroupOutside");
	return FString();
}

/** The default output path for a boolean whose caller did not name one:
 *  "/Game/Meshes/SM_Wall" plus "_Subtract". A separate asset is the default on
 *  purpose, because the destructive form of this operation cannot be undone
 *  from a result object. */
FString DeriveOutputPath(const FString& TargetPath, const FString& OperationName)
{
	const FMCPAssetPathForms Forms = MCPAssetPathForms(TargetPath);
	if (Forms.PackagePath.IsEmpty()) return FString();
	return Forms.PackagePath + TEXT("_") + OperationName;
}

}

void FAssetMeshBooleanHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("asset"));
	// A boolean over two dense meshes, plus the copy in and the rebuild out, is
	// minutes rather than milliseconds on a repair-sized asset. The default
	// handler timeout would report a hang while the editor was still working.
	using EType = EMCPParamType;
	Registry.RegisterHandlerWithTimeout(TEXT("mesh_boolean"), &MeshBoolean, 300.0f, {
		MCPParam::Required(TEXT("operation"), EType::String, TEXT("union | subtract | intersect | trimInside | trimOutside | newPolyGroupInside | newPolyGroupOutside")),
		MCPParam::Required(TEXT("targetPath"), EType::String, TEXT("StaticMesh being cut; the result inherits its transform, materials, collision and Nanite setting")),
		MCPParam::Required(TEXT("toolPath"), EType::String, TEXT("StaticMesh doing the cutting")),
		MCPParam::Optional(TEXT("outputPath"), EType::String, TEXT("Asset to write (default '<targetPath>_<Operation>')")),
		MCPParam::Optional(TEXT("inPlace"), EType::Boolean, TEXT("Overwrite targetPath instead (default false); not with outputPath, and the only destructive form")),
		MCPParam::Optional(TEXT("targetTransform"), EType::Object, TEXT("Where the target sits for the boolean (default identity)")).WithFields({
			MCPParam::OptionalField(TEXT("location"), EType::Vec3, TEXT("Translation")),
			MCPParam::OptionalField(TEXT("rotation"), EType::Rotator, TEXT("Rotation")),
			MCPParam::OptionalField(TEXT("scale"), EType::Vec3, TEXT("Scale (default 1)")),
		}),
		MCPParam::Optional(TEXT("toolTransform"), EType::Object, TEXT("Where the tool sits for the boolean (default identity)")).WithFields({
			MCPParam::OptionalField(TEXT("location"), EType::Vec3, TEXT("Translation")),
			MCPParam::OptionalField(TEXT("rotation"), EType::Rotator, TEXT("Rotation")),
			MCPParam::OptionalField(TEXT("scale"), EType::Vec3, TEXT("Scale (default 1)")),
		}),
		MCPParam::Optional(TEXT("lodType"), EType::String, TEXT("MaxAvailable (default, ignores lodIndex) | HiResSourceModel | SourceModel | RenderData")),
		MCPParam::Optional(TEXT("lodIndex"), EType::Integer, TEXT("LOD of each input to read when lodType names one (default 0)")),
		MCPParam::Optional(TEXT("fillHoles"), EType::Boolean, TEXT("Close the holes the cut opens (default true)")),
		MCPParam::Optional(TEXT("simplifyOutput"), EType::Boolean, TEXT("Collapse coplanar triangles the boolean introduced (default true)")),
		MCPParam::Optional(TEXT("simplifyPlanarTolerance"), EType::Number, TEXT("How far from coplanar still counts as coplanar when simplifying (default 0.01)")),
		MCPParam::Optional(TEXT("allowEmptyResult"), EType::Boolean, TEXT("Accept a result with no triangles (default false)")),
		MCPParam::Optional(TEXT("recomputeNormals"), EType::Boolean, TEXT("Recompute normals on the written mesh (default false)")),
		MCPParam::Optional(TEXT("recomputeTangents"), EType::Boolean, TEXT("Recompute tangents on the written mesh (default false)")),
		MCPParam::Optional(TEXT("removeDegenerates"), EType::Boolean, TEXT("Drop degenerate triangles when writing back into an existing mesh (default false)")),
		MCPParam::Optional(TEXT("copyCollisionFromTarget"), EType::Boolean, TEXT("Copy the target's simple collision onto the result (default true)")),
		MCPParam::Optional(TEXT("copyMaterialsFromTarget"), EType::Boolean, TEXT("Copy the target's material slots onto the result (default true)")),
		MCPParam::Optional(TEXT("nanite"), EType::String, TEXT("inherit (default, match the target) | enable | disable")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("When outputPath exists: error (default) | replace")),
		MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("Run the boolean and report the counts without writing (default false)")),
		MCPParam::Optional(TEXT("save"), EType::Boolean, TEXT("Save the written asset (default true)")),
	});
}

TSharedPtr<FJsonValue> FAssetMeshBooleanHandlers::MeshBoolean(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	// ── Parameters ──────────────────────────────────────────────────────────
	// Every parameter is read before a check below can refuse (#1057).
	MCPReadParamsAhead(Params, {
		TEXT("operation"), TEXT("targetPath"), TEXT("toolPath"), TEXT("outputPath"), TEXT("inPlace"),
		TEXT("targetTransform"), TEXT("toolTransform"), TEXT("lodType"), TEXT("lodIndex"), TEXT("fillHoles"),
		TEXT("simplifyOutput"), TEXT("simplifyPlanarTolerance"), TEXT("allowEmptyResult"), TEXT("recomputeNormals"),
		TEXT("recomputeTangents"), TEXT("removeDegenerates"), TEXT("copyCollisionFromTarget"),
		TEXT("copyMaterialsFromTarget"), TEXT("nanite"), TEXT("onConflict"), TEXT("dryRun"), TEXT("save") });
	FString RequestedOperation;
	if (auto Err = RequireString(Params, TEXT("operation"), RequestedOperation)) return Err;
	const FString OperationName = ResolveBooleanOperation(RequestedOperation);
	if (OperationName.IsEmpty())
	{
		return MCPError(FString::Printf(
			TEXT("Unknown boolean operation '%s'. Use union, subtract, intersect, trimInside, trimOutside, ")
				TEXT("newPolyGroupInside or newPolyGroupOutside."),
			*RequestedOperation));
	}

	FString TargetPath;
	if (auto Err = RequireString(Params, TEXT("targetPath"), TargetPath)) return Err;
	FString ToolPath;
	if (auto Err = RequireString(Params, TEXT("toolPath"), ToolPath)) return Err;

	const bool bInPlace = OptionalBool(Params, TEXT("inPlace"), false);
	FString OutputPath = OptionalString(Params, TEXT("outputPath"));
	if (bInPlace && !OutputPath.IsEmpty())
	{
		return MCPError(TEXT("Pass 'outputPath' or 'inPlace', not both: they name two different destinations ")
			TEXT("and there is no safe answer when they disagree."));
	}
	if (bInPlace)
	{
		OutputPath = TargetPath;
	}
	else if (OutputPath.IsEmpty())
	{
		OutputPath = DeriveOutputPath(TargetPath, OperationName);
		if (OutputPath.IsEmpty())
		{
			return MCPError(FString::Printf(
				TEXT("'%s' is not a usable asset path, so no default output path could be derived from it. ")
					TEXT("Pass outputPath explicitly."),
				*TargetPath));
		}
	}

	const FString RequestedLODType = OptionalString(Params, TEXT("lodType"));
	const FString LODTypeName = MCPGeometryScript::ResolveLODType(RequestedLODType);
	if (LODTypeName.IsEmpty())
	{
		return MCPError(FString::Printf(
			TEXT("Unknown lodType '%s'. Use MaxAvailable, HiResSourceModel, SourceModel or RenderData."),
			*RequestedLODType));
	}
	const int32 LODIndex = FMath::Max(0, OptionalInt(Params, TEXT("lodIndex"), 0));

	const bool bFillHoles = OptionalBool(Params, TEXT("fillHoles"), true);
	const bool bSimplifyOutput = OptionalBool(Params, TEXT("simplifyOutput"), true);
	const double SimplifyPlanarTolerance = OptionalNumber(Params, TEXT("simplifyPlanarTolerance"), 0.01);
	const bool bAllowEmptyResult = OptionalBool(Params, TEXT("allowEmptyResult"), false);
	const bool bRecomputeNormals = OptionalBool(Params, TEXT("recomputeNormals"), false);
	const bool bRecomputeTangents = OptionalBool(Params, TEXT("recomputeTangents"), false);
	const bool bRemoveDegenerates = OptionalBool(Params, TEXT("removeDegenerates"), false);
	const bool bCopyCollision = OptionalBool(Params, TEXT("copyCollisionFromTarget"), true);
	const bool bCopyMaterials = OptionalBool(Params, TEXT("copyMaterialsFromTarget"), true);
	const FString NaniteMode = OptionalString(Params, TEXT("nanite"), TEXT("inherit")).ToLower();
	if (NaniteMode != TEXT("inherit") && NaniteMode != TEXT("enable") && NaniteMode != TEXT("disable"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown nanite mode '%s'. Use inherit (copy the target's setting), enable or disable."),
			*NaniteMode));
	}
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("error")).ToLower();
	if (OnConflict != TEXT("error") && OnConflict != TEXT("replace"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown onConflict '%s'. Use error (default) or replace."), *OnConflict));
	}

	const FTransform TargetTransform = OptionalTransform(Params, TEXT("targetTransform"));
	const FTransform ToolTransform = OptionalTransform(Params, TEXT("toolTransform"));

	// ── Guardrails ──────────────────────────────────────────────────────────
	if (MCPIsProtectedAssetPath(OutputPath)) return MCPProtectedPathError(OutputPath);

	if (!MCPGeometryScript::EnsureLoaded({ MCPGeometryScript::StaticMeshFunctions, MCPGeometryScript::BooleanFunctions }))
	{
		return MCPGeometryScript::UnavailableError(
			TEXT("its runtime classes are not registered in this editor."));
	}

	UObject* TargetObject = MCPLoadAssetObject(TargetPath);
	UStaticMesh* TargetMesh = Cast<UStaticMesh>(TargetObject);
	if (!TargetMesh)
	{
		return TargetObject
			? MCPAssetWrongTypeError(TargetPath, TargetObject, TEXT("StaticMesh"))
			: MCPAssetNotFoundError(TargetPath, TEXT("Target mesh"));
	}
	UObject* ToolObject = MCPLoadAssetObject(ToolPath);
	UStaticMesh* ToolMesh = Cast<UStaticMesh>(ToolObject);
	if (!ToolMesh)
	{
		return ToolObject
			? MCPAssetWrongTypeError(ToolPath, ToolObject, TEXT("StaticMesh"))
			: MCPAssetNotFoundError(ToolPath, TEXT("Tool mesh"));
	}

	UObject* OutputObject = MCPLoadAssetObject(OutputPath);
	UStaticMesh* ExistingOutput = Cast<UStaticMesh>(OutputObject);
	if (OutputObject && !ExistingOutput)
	{
		return MCPAssetWrongTypeError(OutputPath, OutputObject, TEXT("StaticMesh"));
	}
	if (ExistingOutput && OnConflict == TEXT("error") && !bInPlace)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' already exists. Pass onConflict='replace' to overwrite it, inPlace=true to write back ")
				TEXT("into the target, or name a different outputPath."),
			*OutputPath));
	}

	// ── Bind everything before anything is built ────────────────────────────
	UClass* DynamicMeshClass = FindObject<UClass>(nullptr, MCPGeometryScript::DynamicMeshClass);
	if (!DynamicMeshClass)
	{
		return MCPGeometryScript::UnavailableError(TEXT("UDynamicMesh is not registered."));
	}

	FString BindError;
	FMCPReflectedCall CopyIn;
	if (!CopyIn.Bind(MCPGeometryScript::StaticMeshFunctions, TEXT("CopyMeshFromStaticMeshV2"), BindError))
	{
		return MCPGeometryScript::UnavailableError(BindError);
	}
	FMCPReflectedCall Boolean;
	if (!Boolean.Bind(MCPGeometryScript::BooleanFunctions, TEXT("ApplyMeshBoolean"), BindError))
	{
		return MCPGeometryScript::UnavailableError(BindError);
	}

	const int64 OperationValue = MCPGeometryScript::EnumValue(GSBooleanOperationEnum, OperationName);
	if (OperationValue == INDEX_NONE)
	{
		return MCPGeometryScript::UnavailableError(FString::Printf(
			TEXT("EGeometryScriptBooleanOperation has no '%s' in this engine build."), *OperationName));
	}
	const int64 LODTypeValue = MCPGeometryScript::EnumValue(MCPGeometryScript::LODTypeEnum, LODTypeName);

	// ── Build the two dynamic meshes ────────────────────────────────────────
	UObject* TargetDynamic = NewObject<UObject>(GetTransientPackage(), DynamicMeshClass);
	UObject* ToolDynamic = NewObject<UObject>(GetTransientPackage(), DynamicMeshClass);
	if (!TargetDynamic || !ToolDynamic)
	{
		return MCPGeometryScript::UnavailableError(TEXT("a UDynamicMesh could not be constructed."));
	}
	const FGCRootScope KeepTargetDynamic(TargetDynamic);
	const FGCRootScope KeepToolDynamic(ToolDynamic);

	UObject* Debug = nullptr;
	if (UClass* DebugClass = FindObject<UClass>(nullptr, MCPGeometryScript::DebugClass))
	{
		Debug = NewObject<UObject>(GetTransientPackage(), DebugClass);
	}
	const FGCRootScope KeepDebug(Debug);

	TArray<FString> Messages;

	auto CopyMeshIn = [&](UStaticMesh* From, UObject* To, const FString& FromPath, FString& OutFailure) -> bool
	{
		FMCPReflectedCall Call;
		FString Error;
		if (!Call.Bind(MCPGeometryScript::StaticMeshFunctions, TEXT("CopyMeshFromStaticMeshV2"), Error))
		{
			OutFailure = Error;
			return false;
		}
		Call.SetObject(TEXT("FromStaticMeshAsset"), From);
		Call.SetObject(TEXT("ToDynamicMesh"), To);
		Call.SetStructEnum(TEXT("RequestedLOD"), TEXT("LODType"), LODTypeValue);
		Call.SetStructNumber(TEXT("RequestedLOD"), TEXT("LODIndex"), LODIndex);
		Call.SetBool(TEXT("bUseSectionMaterials"), true);
		Call.SetObject(TEXT("Debug"), Debug);
		Call.Invoke();

		Messages.Append(MCPGeometryScript::DrainDebug(Debug));
		if (Call.GetEnumName(TEXT("Outcome")) != TEXT("Success"))
		{
			OutFailure = FString::Printf(
				TEXT("reading '%s' at LOD %s[%d] produced no mesh."), *FromPath, *LODTypeName, LODIndex);
			return false;
		}
		return true;
	};

	FString Failure;
	if (!CopyMeshIn(TargetMesh, TargetDynamic, TargetPath, Failure))
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(TEXT("Target mesh could not be read: %s"), *Failure));
		Obj->SetStringField(TEXT("reason"), TEXT("target_read_failed"));
		MCPGeometryScript::AttachMessages(Obj, Messages);
		return MakeShared<FJsonValueObject>(Obj);
	}
	if (!CopyMeshIn(ToolMesh, ToolDynamic, ToolPath, Failure))
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(TEXT("Tool mesh could not be read: %s"), *Failure));
		Obj->SetStringField(TEXT("reason"), TEXT("tool_read_failed"));
		MCPGeometryScript::AttachMessages(Obj, Messages);
		return MakeShared<FJsonValueObject>(Obj);
	}

	int32 TargetTrianglesIn = 0, TargetVerticesIn = 0;
	int32 ToolTrianglesIn = 0, ToolVerticesIn = 0;
	ReadDynamicMeshCounts(TargetDynamic, TargetTrianglesIn, TargetVerticesIn);
	ReadDynamicMeshCounts(ToolDynamic, ToolTrianglesIn, ToolVerticesIn);

	// ── The boolean ─────────────────────────────────────────────────────────
	Boolean.SetObject(TEXT("TargetMesh"), TargetDynamic);
	Boolean.SetTransform(TEXT("TargetTransform"), TargetTransform);
	Boolean.SetObject(TEXT("ToolMesh"), ToolDynamic);
	Boolean.SetTransform(TEXT("ToolTransform"), ToolTransform);
	Boolean.SetEnum(TEXT("Operation"), OperationValue);
	Boolean.SetStructBool(TEXT("Options"), TEXT("bFillHoles"), bFillHoles);
	Boolean.SetStructBool(TEXT("Options"), TEXT("bSimplifyOutput"), bSimplifyOutput);
	Boolean.SetStructNumber(TEXT("Options"), TEXT("SimplifyPlanarTolerance"), SimplifyPlanarTolerance);
	Boolean.SetStructBool(TEXT("Options"), TEXT("bAllowEmptyResult"), bAllowEmptyResult);
	Boolean.SetObject(TEXT("Debug"), Debug);
	Boolean.Invoke();
	Messages.Append(MCPGeometryScript::DrainDebug(Debug));

	int32 ResultTriangles = 0, ResultVertices = 0;
	ReadDynamicMeshCounts(TargetDynamic, ResultTriangles, ResultVertices);

	// ── Validate before writing ─────────────────────────────────────────────
	if (ResultTriangles == 0 && !bAllowEmptyResult)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("The %s of '%s' and '%s' is empty, so nothing was written. The two meshes may not overlap ")
				TEXT("in the space the transforms put them in, or the operation may have removed everything. ")
				TEXT("Pass allowEmptyResult=true if an empty result is what you meant."),
			*OperationName, *TargetPath, *ToolPath));
		Obj->SetStringField(TEXT("reason"), TEXT("empty_result"));
		Obj->SetStringField(TEXT("operation"), OperationName);
		Obj->SetNumberField(TEXT("resultTriangles"), 0);
		MCPGeometryScript::AttachMessages(Obj, Messages);
		return MakeShared<FJsonValueObject>(Obj);
	}

	auto BuildResultBody = [&](const TSharedPtr<FJsonObject>& Out)
	{
		Out->SetStringField(TEXT("operation"), OperationName);
		Out->SetStringField(TEXT("targetPath"), TargetPath);
		Out->SetStringField(TEXT("toolPath"), ToolPath);
		Out->SetStringField(TEXT("outputPath"), OutputPath);
		Out->SetBoolField(TEXT("inPlace"), bInPlace);
		Out->SetStringField(TEXT("lodType"), LODTypeName);
		Out->SetNumberField(TEXT("lodIndex"), LODIndex);
		Out->SetNumberField(TEXT("targetTrianglesIn"), TargetTrianglesIn);
		Out->SetNumberField(TEXT("targetVerticesIn"), TargetVerticesIn);
		Out->SetNumberField(TEXT("toolTrianglesIn"), ToolTrianglesIn);
		Out->SetNumberField(TEXT("toolVerticesIn"), ToolVerticesIn);
		Out->SetNumberField(TEXT("resultTriangles"), ResultTriangles);
		Out->SetNumberField(TEXT("resultVertices"), ResultVertices);
		Out->SetBoolField(TEXT("resultIsEmpty"), ResultTriangles == 0);
		MCPGeometryScript::AttachMessages(Out, Messages);
	};

	if (bDryRun)
	{
		auto Preview = MCPSuccess();
		BuildResultBody(Preview);
		Preview->SetBoolField(TEXT("dryRun"), true);
		Preview->SetBoolField(TEXT("written"), false);
		Preview->SetStringField(TEXT("wouldWrite"), OutputPath);
		return MCPResult(Preview);
	}

	// ── Write ───────────────────────────────────────────────────────────────
	UStaticMesh* Written = nullptr;
	bool bCreatedAsset = false;

	if (ExistingOutput)
	{
		FMCPReflectedCall CopyOut;
		if (!CopyOut.Bind(MCPGeometryScript::StaticMeshFunctions, TEXT("CopyMeshToStaticMesh"), BindError))
		{
			return MCPGeometryScript::UnavailableError(BindError);
		}
		CopyOut.SetObject(TEXT("FromDynamicMesh"), TargetDynamic);
		CopyOut.SetObject(TEXT("ToStaticMeshAsset"), ExistingOutput);
		CopyOut.SetStructBool(TEXT("Options"), TEXT("bEnableRecomputeNormals"), bRecomputeNormals);
		CopyOut.SetStructBool(TEXT("Options"), TEXT("bEnableRecomputeTangents"), bRecomputeTangents);
		CopyOut.SetStructBool(TEXT("Options"), TEXT("bEnableRemoveDegenerates"), bRemoveDegenerates);
		CopyOut.SetStructNumber(TEXT("TargetLOD"), TEXT("LODIndex"), 0);
		CopyOut.SetBool(TEXT("bUseSectionMaterials"), true);
		CopyOut.SetObject(TEXT("Debug"), Debug);
		CopyOut.Invoke();
		Messages.Append(MCPGeometryScript::DrainDebug(Debug));

		if (CopyOut.GetEnumName(TEXT("Outcome")) != TEXT("Success"))
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetBoolField(TEXT("success"), false);
			Obj->SetStringField(TEXT("error"), FString::Printf(
				TEXT("The boolean produced %d triangles but writing them into '%s' failed."),
				ResultTriangles, *OutputPath));
			Obj->SetStringField(TEXT("reason"), TEXT("write_failed"));
			BuildResultBody(Obj);
			return MakeShared<FJsonValueObject>(Obj);
		}
		Written = ExistingOutput;
	}
	else
	{
		FMCPReflectedCall Create;
		if (!Create.Bind(MCPGeometryScript::CreateAssetFunctions, TEXT("CreateNewStaticMeshAssetFromMesh"), BindError))
		{
			return MCPGeometryScript::UnavailableError(FString::Printf(
				TEXT("%s Creating a new StaticMesh asset needs the editor half of the plugin ")
					TEXT("(GeometryScriptingEditor)."),
				*BindError));
		}
		Create.SetObject(TEXT("FromDynamicMesh"), TargetDynamic);
		Create.SetString(TEXT("AssetPathAndName"), OutputPath);
		Create.SetStructBool(TEXT("Options"), TEXT("bEnableRecomputeNormals"), bRecomputeNormals);
		Create.SetStructBool(TEXT("Options"), TEXT("bEnableRecomputeTangents"), bRecomputeTangents);
		Create.SetStructBool(TEXT("Options"), TEXT("bEnableCollision"), bCopyCollision);
		Create.SetStructBool(TEXT("Options"), TEXT("bEnableNanite"),
			NaniteMode == TEXT("enable")
			|| (NaniteMode == TEXT("inherit") && MCPGetNaniteSettings(TargetMesh).bEnabled));
		Create.SetObject(TEXT("Debug"), Debug);
		Create.Invoke();
		Messages.Append(MCPGeometryScript::DrainDebug(Debug));

		Written = Cast<UStaticMesh>(Create.ReturnObject());
		if (Create.GetEnumName(TEXT("Outcome")) != TEXT("Success") || !Written)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetBoolField(TEXT("success"), false);
			Obj->SetStringField(TEXT("error"), FString::Printf(
				TEXT("The boolean produced %d triangles but the new StaticMesh asset at '%s' could not be ")
					TEXT("created. The path may be invalid or already taken by another asset type."),
				ResultTriangles, *OutputPath));
			Obj->SetStringField(TEXT("reason"), TEXT("create_failed"));
			BuildResultBody(Obj);
			return MakeShared<FJsonValueObject>(Obj);
		}
		bCreatedAsset = true;
	}

	// ── Post-write fixups: materials, collision, Nanite ─────────────────────
	bool bMaterialsCopied = false;
	if (bCopyMaterials && Written != TargetMesh)
	{
		// The dynamic mesh carried section indices as material IDs, so the
		// target's slot list maps straight back onto the result's sections.
		Written->Modify();
		Written->SetStaticMaterials(TargetMesh->GetStaticMaterials());
		bMaterialsCopied = true;
	}

	bool bCollisionCopied = false;
	if (bCopyCollision && Written != TargetMesh)
	{
		bCollisionCopied = MCPGeometryScript::CopySimpleCollision(TargetMesh, Written);
	}

	bool bNaniteChanged = false;
	{
		FMeshNaniteSettings Settings = MCPGetNaniteSettings(Written);
		const bool bWant =
			NaniteMode == TEXT("enable") ? true :
			NaniteMode == TEXT("disable") ? false :
			MCPGetNaniteSettings(TargetMesh).bEnabled;
		if (Settings.bEnabled != bWant)
		{
			Settings.bEnabled = bWant;
			Written->Modify();
			MCPSetNaniteSettings(Written, Settings);
			bNaniteChanged = true;
		}
	}

	if (bMaterialsCopied || bCollisionCopied || bNaniteChanged)
	{
		Written->PostEditChange();
	}

	// ── Persist ─────────────────────────────────────────────────────────────
	auto Result = MCPSuccess();
	BuildResultBody(Result);
	if (bCreatedAsset)
	{
		MCPSetCreated(Result);
	}
	else
	{
		MCPSetUpdated(Result);
	}
	Result->SetBoolField(TEXT("written"), true);
	Result->SetBoolField(TEXT("createdOutputAsset"), bCreatedAsset);
	Result->SetBoolField(TEXT("materialsCopiedFromTarget"), bMaterialsCopied);
	Result->SetBoolField(TEXT("collisionCopiedFromTarget"), bCollisionCopied);
	Result->SetStringField(TEXT("nanite"), NaniteMode);
	Result->SetBoolField(TEXT("naniteChanged"), bNaniteChanged);
	MCPGeometryScript::WriteAssetStats(Result, TEXT("output"), Written);

	if (bSave)
	{
		FString SaveReason;
		const bool bSaved = SaveAssetPackageChecked(Written, SaveReason);
		MCPNoteSaveOutcome(Result, OutputPath, bSaved, SaveReason);
	}
	else
	{
		// The caller asked for this, so it is not a failure. It is still said
		// out loud, because geometry that only exists in memory is gone at the
		// next editor start and reads as a write that silently did not take.
		Written->MarkPackageDirty();
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetStringField(TEXT("saveSkippedReason"),
			TEXT("save=false was requested, so the new geometry is dirty in memory only and is lost when the ")
				TEXT("editor closes. Call asset(save) for it, or repeat with save=true."));
	}

	if (bCreatedAsset)
	{
		// The inverse of "this call created an asset" is deleting it. An
		// in-place or replacing write has no inverse and gets no descriptor,
		// because a rollback record that cannot restore the prior geometry
		// would be a promise this handler cannot keep.
		MCPSetDeleteAssetRollback(Result, OutputPath);
	}
	return MCPResult(Result);
}
