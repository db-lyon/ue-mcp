// Split from AssetHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FAssetHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in AssetHandlers.cpp::RegisterHandlers.

#include "AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "HandlerJsonProperty.h"
#include "JsonSerializer.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetImportTask.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Animation/MorphTarget.h"
#include "Factories/TextureFactory.h"
#include "Factories/ReimportTextureFactory.h"
#include "Factories/CSVImportFactory.h"
#include "EditorReimportHandler.h"
#include "Curves/RichCurve.h"
#include "Curves/SimpleCurve.h"
#include "Engine/CurveTable.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/TopLevelAssetPath.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Factories/DataTableFactory.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/TextKey.h"
#include "Exporters/Exporter.h"
#include "AssetExportTask.h"

namespace
{
	FString CurveTableModeName(ECurveTableMode Mode)
	{
		switch (Mode)
		{
		case ECurveTableMode::SimpleCurves: return TEXT("simple");
		case ECurveTableMode::RichCurves: return TEXT("rich");
		default: return TEXT("empty");
		}
	}

	FString CurveInterpModeName(ERichCurveInterpMode Mode)
	{
		switch (Mode)
		{
		case RCIM_Constant: return TEXT("constant");
		case RCIM_Cubic: return TEXT("cubic");
		case RCIM_None: return TEXT("none");
		default: return TEXT("linear");
		}
	}

	bool ParseCurveInterpMode(const FString& Raw, ERichCurveInterpMode& OutMode)
	{
		const FString Mode = Raw.ToLower();
		if (Mode.IsEmpty() || Mode == TEXT("linear")) { OutMode = RCIM_Linear; return true; }
		if (Mode == TEXT("constant")) { OutMode = RCIM_Constant; return true; }
		if (Mode == TEXT("cubic")) { OutMode = RCIM_Cubic; return true; }
		if (Mode == TEXT("none")) { OutMode = RCIM_None; return true; }
		return false;
	}

	TSharedPtr<FJsonObject> CurveKeyToJson(float Time, float Value, ERichCurveInterpMode InterpMode)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("time"), Time);
		Obj->SetNumberField(TEXT("value"), Value);
		Obj->SetStringField(TEXT("interpMode"), CurveInterpModeName(InterpMode));
		return Obj;
	}

	TArray<TSharedPtr<FJsonValue>> CurveKeysToJson(FRealCurve* Curve, ECurveTableMode Mode)
	{
		TArray<TSharedPtr<FJsonValue>> Keys;
		if (!Curve) return Keys;

		if (Mode == ECurveTableMode::RichCurves)
		{
			FRichCurve* Rich = static_cast<FRichCurve*>(Curve);
			for (const FRichCurveKey& Key : Rich->GetConstRefOfKeys())
			{
				TSharedPtr<FJsonObject> Obj = CurveKeyToJson(Key.Time, Key.Value, Key.InterpMode);
				Obj->SetNumberField(TEXT("arriveTangent"), Key.ArriveTangent);
				Obj->SetNumberField(TEXT("leaveTangent"), Key.LeaveTangent);
				Keys.Add(MakeShared<FJsonValueObject>(Obj));
			}
			return Keys;
		}

		if (Mode == ECurveTableMode::SimpleCurves)
		{
			FSimpleCurve* Simple = static_cast<FSimpleCurve*>(Curve);
			for (const FSimpleCurveKey& Key : Simple->GetConstRefOfKeys())
			{
				Keys.Add(MakeShared<FJsonValueObject>(CurveKeyToJson(Key.Time, Key.Value, Simple->GetKeyInterpMode())));
			}
		}
		return Keys;
	}

	FRealCurve* GetCurveTableRow(UCurveTable* Table, const FString& RowName)
	{
		if (!Table) return nullptr;
		FRealCurve* const* Found = Table->GetRowMap().Find(FName(*RowName));
		return Found ? *Found : nullptr;
	}

	void SaveCurveTableChange(UCurveTable* Table)
	{
		if (!Table) return;
		UCurveTable::InvalidateAllCachedCurves();
		Table->OnCurveTableChanged().Broadcast();
		Table->Modify(true);
		SaveAssetPackage(Table);
	}

	TSharedPtr<FJsonValue> LoadCurveTable(const TSharedPtr<FJsonObject>& Params, UCurveTable*& OutTable, FString& OutAssetPath)
	{
		if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), OutAssetPath)) return Err;
		TSharedPtr<FJsonValue> LoadError;
		UObject* Asset = MCPRequireAssetObject(OutAssetPath, LoadError);
		if (!Asset) return LoadError;
		OutTable = Cast<UCurveTable>(Asset);
		if (!OutTable)
		{
			return MCPAssetWrongTypeError(OutAssetPath, Asset, TEXT("CurveTable"));
		}
		return nullptr;
	}

	/** The inverse of an import that produced assets: delete exactly what it
	 *  produced.
	 *
	 *  One asset goes through delete_asset and several through
	 *  delete_asset_batch, because a rollback record is a single call and an
	 *  FBX import routinely yields more than one object: a skeletal mesh import
	 *  returns the mesh plus a generated Skeleton and PhysicsAsset, and a static
	 *  mesh import can return the mesh plus its materials. Branching only on
	 *  ==1 and ==0 left exactly those calls, the ones that created the most,
	 *  saying nothing at all about recoverability. */
	void EmitImportedAssetsRollback(
		TSharedPtr<FJsonObject> Result,
		const TArray<TSharedPtr<FJsonValue>>& ImportedPaths)
	{
		if (ImportedPaths.Num() == 0)
		{
			Result->SetBoolField(TEXT("rollbackPossible"), false);
			Result->SetStringField(TEXT("rollbackNote"), TEXT("No asset was produced, so there is nothing to undo."));
			return;
		}

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		if (ImportedPaths.Num() == 1)
		{
			Payload->SetStringField(TEXT("assetPath"), ImportedPaths[0]->AsString());
			MCPSetRollback(Result, TEXT("delete_asset"), Payload);
		}
		else
		{
			Payload->SetArrayField(TEXT("assetPaths"), ImportedPaths);
			MCPSetRollback(Result, TEXT("delete_asset_batch"), Payload);
		}
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The inverse deletes the %d asset(s) this import produced. Two limits. These importers replace an asset already ")
			TEXT("at the destination rather than refusing, so where one was there its previous content is gone and deleting the ")
			TEXT("asset does not bring it back. And the delete runs with the default force=false, so it refuses any of these that ")
			TEXT("something outside the rollback still references, and reports that rather than removing them."),
			ImportedPaths.Num()));
	}

	/** What a set_curvetable_keys replay does NOT carry back.
	 *
	 *  CurveKeysToJson emits time, value, interpMode and the two tangents.
	 *  FRichCurveKey also has TangentMode, TangentWeightMode, ArriveTangentWeight
	 *  and LeaveTangentWeight, and SetCurveTableKeys rebuilds each key with
	 *  FRichCurveKey(Time, Value), which defaults those to RCTM_Auto,
	 *  RCTWM_WeightedNone and zero. A simple curve has none of those fields, so
	 *  its replay is exact and is not flagged. */
	void EmitCurveKeyReplayCaveat(TSharedPtr<FJsonObject> Result, const bool bRichCurve, const int32 PriorKeyCount)
	{
		const bool bLossy = bRichCurve && PriorKeyCount > 0;
		Result->SetBoolField(TEXT("rollbackLossy"), bLossy);
		if (bLossy)
		{
			Result->SetStringField(TEXT("rollbackNote"),
				TEXT("The inverse restores every key's time, value, interpolation mode and both tangents. On a rich curve it does ")
				TEXT("not restore per-key tangent mode, tangent weight mode or the tangent weights: this action rebuilds keys with ")
				TEXT("the FRichCurveKey defaults for those, so a break or user tangent goes back to auto and any weight goes to zero."));
		}
	}

	/** The inverse of a whole-table CurveTable import, from the snapshot taken
	 *  before it ran. Shared by the success and the failure path: the importer
	 *  empties the table before it reports problems, so a failed import is the
	 *  outcome that needs this most. */
	void EmitPriorCurveTableRollback(
		TSharedPtr<FJsonObject> Result,
		const FString& AssetPath,
		const FString& PriorJson,
		const int32 PriorRowCount,
		const ECurveTableMode PriorMode)
	{
		if (PriorRowCount > 0)
		{
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("assetPath"), AssetPath);
			Payload->SetStringField(TEXT("jsonString"), PriorJson);
			Payload->SetStringField(TEXT("format"), TEXT("json"));
			// Cubic is what makes CreateTableFromJSONString build rich rows, so
			// the restored table comes back in the curve mode it had.
			Payload->SetStringField(TEXT("interpMode"),
				PriorMode == ECurveTableMode::RichCurves ? TEXT("cubic") : TEXT("linear"));
			MCPSetRollback(Result, TEXT("import_curvetable"), Payload);
			Result->SetBoolField(TEXT("rollbackLossy"), true);
			Result->SetStringField(TEXT("rollbackNote"),
				TEXT("The inverse replays the table's pre-call JSON export, which carries row names, key times and key values. ")
				TEXT("The CurveTable JSON form has no place for per-key interpolation modes or tangents, so the restored rows all ")
				TEXT("share one interpolation mode."));
		}
		else
		{
			Result->SetBoolField(TEXT("rollbackPossible"), false);
			Result->SetStringField(TEXT("rollbackNote"),
				TEXT("The table had no rows before this call. Restoring that would mean importing an empty row set, and whether ")
				TEXT("the engine's CurveTable importer accepts one is not established from the installed headers, so no inverse is ")
				TEXT("emitted rather than one that may report success without emptying the table. ")
				TEXT("Remove the imported rows with asset(remove_curvetable_row)."));
		}
	}
}


// ============================================================================
// FBX import handlers
// ============================================================================

TSharedPtr<FJsonValue> FAssetHandlers::ImportStaticMesh(const TSharedPtr<FJsonObject>& Params)
{
	FString FileName;
	if (auto Err = RequireStringAlt(Params, TEXT("filename"), TEXT("filePath"), FileName)) return Err;

	FString DestinationPath = OptionalString(Params, TEXT("destinationPath"), TEXT("/Game/Meshes"));
	if (DestinationPath == TEXT("/Game/Meshes"))
	{
		FString PkgPath = OptionalString(Params, TEXT("packagePath"));
		if (!PkgPath.IsEmpty()) DestinationPath = PkgPath;
	}

	if (!FPaths::FileExists(FileName))
	{
		return MCPError(FString::Printf(TEXT("File not found: %s"), *FileName));
	}

	// #549: pick the right import path by extension. FBX and OBJ go through
	// UFbxFactory (OBJ via bIsObjImport); GLB/glTF leave the factory null so
	// AssetTools/Interchange auto-selects the glTF translator.
	const FString Ext = FPaths::GetExtension(FileName).ToLower();
	const bool bIsObj = (Ext == TEXT("obj"));
	const bool bIsGltf = (Ext == TEXT("glb") || Ext == TEXT("gltf"));

	UFbxFactory* FbxFactory = nullptr;
	if (!bIsGltf)
	{
		FbxFactory = NewObject<UFbxFactory>();

		UFbxImportUI* ImportUI = NewObject<UFbxImportUI>();
		ImportUI->bImportMesh = true;
		ImportUI->bImportAnimations = false;
		ImportUI->bImportMaterials = true;
		ImportUI->bImportTextures = true;
		ImportUI->bIsObjImport = bIsObj;
		ImportUI->MeshTypeToImport = FBXIT_StaticMesh;

		// Apply optional settings
		bool bImportMaterials = true;
		if (Params->TryGetBoolField(TEXT("importMaterials"), bImportMaterials))
		{
			ImportUI->bImportMaterials = bImportMaterials;
		}
		bool bImportTextures = true;
		if (Params->TryGetBoolField(TEXT("importTextures"), bImportTextures))
		{
			ImportUI->bImportTextures = bImportTextures;
		}
		bool bCombineMeshes = false;
		if (Params->TryGetBoolField(TEXT("combineMeshes"), bCombineMeshes))
		{
			ImportUI->StaticMeshImportData->bCombineMeshes = bCombineMeshes;
		}
		bool bGenerateLightmapUVs = true;
		if (Params->TryGetBoolField(TEXT("generateLightmapUVs"), bGenerateLightmapUVs))
		{
			ImportUI->StaticMeshImportData->bGenerateLightmapUVs = bGenerateLightmapUVs;
		}
		// #687: metre-authored FBX unit conversion. Default 1.0 leaves cm FBX
		// untouched; pass 100 for metre FBX.
		if (Params->HasField(TEXT("importUniformScale")) && ImportUI->StaticMeshImportData)
		{
			ImportUI->StaticMeshImportData->ImportUniformScale = (float)OptionalNumber(Params, TEXT("importUniformScale"), 1.0);
		}

		FbxFactory->ImportUI = ImportUI;
	}
	FGCRootScope FactoryRoot(FbxFactory); // no-op when null (glTF/Interchange)

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	FGCRootScope TaskRoot(Task);
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bSave = false;
	Task->Filename = FileName;
	Task->DestinationPath = DestinationPath;
	Task->Factory = FbxFactory; // null for glTF -> Interchange auto-selects

	// Optional asset name
	FString AssetName;
	if (!Params->TryGetStringField(TEXT("assetName"), AssetName))
	{
		Params->TryGetStringField(TEXT("name"), AssetName);
	}
	if (!AssetName.IsEmpty())
	{
		Task->DestinationName = AssetName;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	TArray<TSharedPtr<FJsonValue>> ImportedPaths;
	if (Task->GetObjects().Num() > 0)
	{
		for (UObject* ImportedObj : Task->GetObjects())
		{
			if (ImportedObj)
			{
				FString ObjPath = ImportedObj->GetPathName();
				ImportedPaths.Add(MakeShared<FJsonValueString>(ObjPath));
			}
		}
	}

	auto Result = MCPSuccess();
	if (ImportedPaths.Num() > 0) { MCPSetCreated(Result); }
	Result->SetStringField(TEXT("filename"), FileName);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetArrayField(TEXT("importedAssets"), ImportedPaths);
	Result->SetNumberField(TEXT("importedCount"), ImportedPaths.Num());
	Result->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0);
	if (ImportedPaths.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("Import task completed but no assets were produced"));
	}

	// Rollback only when a single asset was produced (paired inverse: delete_asset).
	EmitImportedAssetsRollback(Result, ImportedPaths);

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAssetHandlers::ImportSkeletalMesh(const TSharedPtr<FJsonObject>& Params)
{
	FString FileName;
	if (auto Err = RequireStringAlt(Params, TEXT("filename"), TEXT("filePath"), FileName)) return Err;

	FString DestinationPath = OptionalString(Params, TEXT("destinationPath"), TEXT("/Game/Meshes"));
	if (DestinationPath == TEXT("/Game/Meshes"))
	{
		FString PkgPath = OptionalString(Params, TEXT("packagePath"));
		if (!PkgPath.IsEmpty()) DestinationPath = PkgPath;
	}

	if (!FPaths::FileExists(FileName))
	{
		return MCPError(FString::Printf(TEXT("File not found: %s"), *FileName));
	}

	UFbxFactory* FbxFactory = NewObject<UFbxFactory>();
	FGCRootScope FactoryRoot(FbxFactory);

	UFbxImportUI* ImportUI = NewObject<UFbxImportUI>();
	ImportUI->bImportMesh = true;
	ImportUI->bImportAnimations = false;
	ImportUI->bImportMaterials = true;
	ImportUI->bImportTextures = true;
	ImportUI->bIsObjImport = false;
	ImportUI->MeshTypeToImport = FBXIT_SkeletalMesh;

	// Apply optional settings
	bool bImportMaterials = true;
	if (Params->TryGetBoolField(TEXT("importMaterials"), bImportMaterials))
	{
		ImportUI->bImportMaterials = bImportMaterials;
	}
	bool bImportTextures = true;
	if (Params->TryGetBoolField(TEXT("importTextures"), bImportTextures))
	{
		ImportUI->bImportTextures = bImportTextures;
	}

	// Optionally set an existing skeleton
	FString SkeletonPath;
	if (Params->TryGetStringField(TEXT("skeletonPath"), SkeletonPath) && !SkeletonPath.IsEmpty())
	{
		USkeleton* Skeleton = LoadAssetByPath<USkeleton>(SkeletonPath);
		if (Skeleton)
		{
			ImportUI->Skeleton = Skeleton;
		}
		else
		{
			auto Result = MCPSuccess();
			Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("Skeleton not found: %s, importing without skeleton target"), *SkeletonPath));
			// Continue with import - don't return here, just note the warning
		}
	}

	// #678: morph targets + physics asset creation controls.
	const bool bImportMorphTargets = OptionalBool(Params, TEXT("importMorphTargets"), true);
	const bool bCreatePhysicsAsset = OptionalBool(Params, TEXT("createPhysicsAsset"), false);
	ImportUI->bCreatePhysicsAsset = bCreatePhysicsAsset;
	if (ImportUI->SkeletalMeshImportData)
	{
		ImportUI->SkeletalMeshImportData->bImportMorphTargets = bImportMorphTargets;
		// #687: metre-authored FBX (Blender FBX_SCALE_ALL) lands 100x too small
		// on a cm skeleton. Expose the uniform-scale knob; default 1.0 leaves
		// cm-authored FBX untouched. Pass 100 for metre FBX.
		const double UniformScale = OptionalNumber(Params, TEXT("importUniformScale"), 1.0);
		ImportUI->SkeletalMeshImportData->ImportUniformScale = (float)UniformScale;
	}

	FbxFactory->ImportUI = ImportUI;

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	FGCRootScope TaskRoot(Task);
	Task->bAutomated = true;
	// #678: replace_existing now a param (default true preserves prior behavior).
	Task->bReplaceExisting = OptionalBool(Params, TEXT("replaceExisting"), true);
	Task->bSave = false;
	Task->Filename = FileName;
	Task->DestinationPath = DestinationPath;
	Task->Factory = FbxFactory;

	// Optional asset name
	FString AssetName;
	if (!Params->TryGetStringField(TEXT("assetName"), AssetName))
	{
		Params->TryGetStringField(TEXT("name"), AssetName);
	}
	if (!AssetName.IsEmpty())
	{
		Task->DestinationName = AssetName;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	TArray<TSharedPtr<FJsonValue>> ImportedPaths;
	if (Task->GetObjects().Num() > 0)
	{
		for (UObject* ImportedObj : Task->GetObjects())
		{
			if (ImportedObj)
			{
				FString ObjPath = ImportedObj->GetPathName();
				ImportedPaths.Add(MakeShared<FJsonValueString>(ObjPath));
			}
		}
	}

	auto Result = MCPSuccess();
	if (ImportedPaths.Num() > 0) { MCPSetCreated(Result); }
	Result->SetStringField(TEXT("filename"), FileName);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetArrayField(TEXT("importedAssets"), ImportedPaths);
	Result->SetNumberField(TEXT("importedCount"), ImportedPaths.Num());
	Result->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0);
	Result->SetNumberField(TEXT("importUniformScale"), OptionalNumber(Params, TEXT("importUniformScale"), 1.0));

	// #678: post-import readback so a caller can confirm scale (box extent),
	// morph-target names, and LOD count without a second round-trip.
	for (UObject* Obj : Task->GetObjects())
	{
		if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Obj))
		{
			const FBoxSphereBounds Bounds = SkelMesh->GetBounds();
			Result->SetObjectField(TEXT("boxExtent"), MCPVec3ToJsonObject(Bounds.BoxExtent));
			Result->SetNumberField(TEXT("boundsRadius"), Bounds.SphereRadius);
			TArray<TSharedPtr<FJsonValue>> MorphNames;
			for (UMorphTarget* MT : SkelMesh->GetMorphTargets())
			{
				if (MT) MorphNames.Add(MakeShared<FJsonValueString>(MT->GetName()));
			}
			Result->SetArrayField(TEXT("morphTargets"), MorphNames);
			Result->SetNumberField(TEXT("morphTargetCount"), MorphNames.Num());
			Result->SetNumberField(TEXT("numLODs"), SkelMesh->GetLODNum());
			if (USkeleton* Sk = SkelMesh->GetSkeleton())
			{
				Result->SetStringField(TEXT("skeleton"), Sk->GetPathName());
			}
			break;
		}
	}
	if (ImportedPaths.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("Import task completed but no assets were produced"));
	}

	EmitImportedAssetsRollback(Result, ImportedPaths);

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAssetHandlers::ImportAnimation(const TSharedPtr<FJsonObject>& Params)
{
	FString FileName;
	if (auto Err = RequireStringAlt(Params, TEXT("filename"), TEXT("filePath"), FileName)) return Err;

	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;

	FString DestinationPath = OptionalString(Params, TEXT("destinationPath"), TEXT("/Game/Animations"));
	if (DestinationPath == TEXT("/Game/Animations"))
	{
		FString PkgPath = OptionalString(Params, TEXT("packagePath"));
		if (!PkgPath.IsEmpty()) DestinationPath = PkgPath;
	}

	if (!FPaths::FileExists(FileName))
	{
		return MCPError(FString::Printf(TEXT("File not found: %s"), *FileName));
	}

	USkeleton* Skeleton = LoadAssetByPath<USkeleton>(SkeletonPath);
	if (!Skeleton)
	{
		return MCPAssetLoadError(SkeletonPath, TEXT("Skeleton"));
	}

	UFbxFactory* FbxFactory = NewObject<UFbxFactory>();
	FGCRootScope FactoryRoot(FbxFactory);

	UFbxImportUI* ImportUI = NewObject<UFbxImportUI>();
	ImportUI->bImportMesh = false;
	ImportUI->bImportAnimations = true;
	ImportUI->bImportMaterials = false;
	ImportUI->bImportTextures = false;
	ImportUI->bIsObjImport = false;
	ImportUI->MeshTypeToImport = FBXIT_Animation;
	ImportUI->Skeleton = Skeleton;

	// Apply optional animation settings
	bool bImportCustomAttribute = true;
	if (Params->TryGetBoolField(TEXT("importCustomAttribute"), bImportCustomAttribute))
	{
		ImportUI->AnimSequenceImportData->bImportCustomAttribute = bImportCustomAttribute;
	}
	bool bRemoveRedundantKeys = true;
	if (Params->TryGetBoolField(TEXT("removeRedundantKeys"), bRemoveRedundantKeys))
	{
		ImportUI->AnimSequenceImportData->bRemoveRedundantKeys = bRemoveRedundantKeys;
	}

	FbxFactory->ImportUI = ImportUI;

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	FGCRootScope TaskRoot(Task);
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bSave = false;
	Task->Filename = FileName;
	Task->DestinationPath = DestinationPath;
	Task->Factory = FbxFactory;

	// Optional asset name
	FString AssetName;
	if (!Params->TryGetStringField(TEXT("assetName"), AssetName))
	{
		Params->TryGetStringField(TEXT("name"), AssetName);
	}
	if (!AssetName.IsEmpty())
	{
		Task->DestinationName = AssetName;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	TArray<TSharedPtr<FJsonValue>> ImportedPaths;
	if (Task->GetObjects().Num() > 0)
	{
		for (UObject* ImportedObj : Task->GetObjects())
		{
			if (ImportedObj)
			{
				FString ObjPath = ImportedObj->GetPathName();
				ImportedPaths.Add(MakeShared<FJsonValueString>(ObjPath));
			}
		}
	}

	auto Result = MCPSuccess();
	if (ImportedPaths.Num() > 0) { MCPSetCreated(Result); }
	Result->SetStringField(TEXT("filename"), FileName);
	Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetArrayField(TEXT("importedAssets"), ImportedPaths);
	Result->SetNumberField(TEXT("importedCount"), ImportedPaths.Num());
	Result->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0);
	if (ImportedPaths.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("Import task completed but no assets were produced"));
	}

	EmitImportedAssetsRollback(Result, ImportedPaths);

	return MCPResult(Result);
}

// ============================================================================
// Texture handlers
// ============================================================================


// ============================================================================
// Texture handlers
// ============================================================================

TSharedPtr<FJsonValue> FAssetHandlers::ListTextureProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;

	UTexture2D* Texture = Cast<UTexture2D>(Asset);
	if (!Texture)
	{
		return MCPAssetWrongTypeError(AssetPath, Asset, TEXT("Texture2D"));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), Texture->GetName());

	// Dimensions
	Result->SetNumberField(TEXT("sizeX"), Texture->GetSizeX());
	Result->SetNumberField(TEXT("sizeY"), Texture->GetSizeY());

	// Compression settings
	FString CompressionStr;
	switch (Texture->CompressionSettings)
	{
		case TC_Default:         CompressionStr = TEXT("Default"); break;
		case TC_Normalmap:       CompressionStr = TEXT("Normalmap"); break;
		case TC_Grayscale:       CompressionStr = TEXT("Grayscale"); break;
		case TC_Displacementmap: CompressionStr = TEXT("Displacementmap"); break;
		case TC_VectorDisplacementmap: CompressionStr = TEXT("VectorDisplacementmap"); break;
		case TC_HDR:             CompressionStr = TEXT("HDR"); break;
		case TC_EditorIcon:      CompressionStr = TEXT("EditorIcon"); break;
		case TC_Alpha:           CompressionStr = TEXT("Alpha"); break;
		case TC_DistanceFieldFont: CompressionStr = TEXT("DistanceFieldFont"); break;
		case TC_HDR_Compressed:  CompressionStr = TEXT("HDR_Compressed"); break;
		case TC_BC7:             CompressionStr = TEXT("BC7"); break;
		default:                 CompressionStr = TEXT("Unknown"); break;
	}
	Result->SetStringField(TEXT("compressionSettings"), CompressionStr);

	// LOD group
	FString LODGroupStr;
	switch (Texture->LODGroup)
	{
		case TEXTUREGROUP_World:           LODGroupStr = TEXT("World"); break;
		case TEXTUREGROUP_WorldNormalMap:   LODGroupStr = TEXT("WorldNormalMap"); break;
		case TEXTUREGROUP_WorldSpecular:    LODGroupStr = TEXT("WorldSpecular"); break;
		case TEXTUREGROUP_Character:        LODGroupStr = TEXT("Character"); break;
		case TEXTUREGROUP_CharacterNormalMap: LODGroupStr = TEXT("CharacterNormalMap"); break;
		case TEXTUREGROUP_CharacterSpecular: LODGroupStr = TEXT("CharacterSpecular"); break;
		case TEXTUREGROUP_Weapon:           LODGroupStr = TEXT("Weapon"); break;
		case TEXTUREGROUP_WeaponNormalMap:   LODGroupStr = TEXT("WeaponNormalMap"); break;
		case TEXTUREGROUP_WeaponSpecular:    LODGroupStr = TEXT("WeaponSpecular"); break;
		case TEXTUREGROUP_Vehicle:           LODGroupStr = TEXT("Vehicle"); break;
		case TEXTUREGROUP_VehicleNormalMap:   LODGroupStr = TEXT("VehicleNormalMap"); break;
		case TEXTUREGROUP_VehicleSpecular:    LODGroupStr = TEXT("VehicleSpecular"); break;
		case TEXTUREGROUP_UI:               LODGroupStr = TEXT("UI"); break;
		case TEXTUREGROUP_Lightmap:          LODGroupStr = TEXT("Lightmap"); break;
		case TEXTUREGROUP_Shadowmap:         LODGroupStr = TEXT("Shadowmap"); break;
		case TEXTUREGROUP_Effects:           LODGroupStr = TEXT("Effects"); break;
		case TEXTUREGROUP_EffectsNotFiltered: LODGroupStr = TEXT("EffectsNotFiltered"); break;
		case TEXTUREGROUP_Skybox:            LODGroupStr = TEXT("Skybox"); break;
		case TEXTUREGROUP_Pixels2D:          LODGroupStr = TEXT("Pixels2D"); break;
		default:                             LODGroupStr = TEXT("Unknown"); break;
	}
	Result->SetStringField(TEXT("lodGroup"), LODGroupStr);

	// Other properties
	Result->SetBoolField(TEXT("sRGB"), Texture->SRGB);
	Result->SetBoolField(TEXT("neverStream"), Texture->NeverStream);

	// Num mips
	Result->SetNumberField(TEXT("numMips"), Texture->GetNumMips());

	// Pixel format
	Result->SetStringField(TEXT("pixelFormat"), GPixelFormats[Texture->GetPixelFormat()].Name);

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAssetHandlers::SetTextureProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;

	UTexture2D* Texture = Cast<UTexture2D>(Asset);
	if (!Texture)
	{
		return MCPAssetWrongTypeError(AssetPath, Asset, TEXT("Texture2D"));
	}

	// Capture previous values for self-inverse rollback. Use reflection paths
	// since the enum string mapping is long.
	const TextureCompressionSettings PrevCompression = Texture->CompressionSettings;
	const TextureGroup PrevLODGroup = Texture->LODGroup;
	const bool PrevSRGB = Texture->SRGB;
	const bool PrevNeverStream = Texture->NeverStream;

	TArray<FString> ModifiedProperties;

	// Compression settings
	FString CompressionStr;
	if (Params->TryGetStringField(TEXT("compressionSettings"), CompressionStr))
	{
		TextureCompressionSettings NewCompression = TC_Default;
		if (CompressionStr == TEXT("Default"))                    NewCompression = TC_Default;
		else if (CompressionStr == TEXT("Normalmap"))             NewCompression = TC_Normalmap;
		else if (CompressionStr == TEXT("Grayscale"))             NewCompression = TC_Grayscale;
		else if (CompressionStr == TEXT("Displacementmap"))       NewCompression = TC_Displacementmap;
		else if (CompressionStr == TEXT("VectorDisplacementmap")) NewCompression = TC_VectorDisplacementmap;
		else if (CompressionStr == TEXT("HDR"))                   NewCompression = TC_HDR;
		else if (CompressionStr == TEXT("EditorIcon"))            NewCompression = TC_EditorIcon;
		else if (CompressionStr == TEXT("Alpha"))                 NewCompression = TC_Alpha;
		else if (CompressionStr == TEXT("DistanceFieldFont"))     NewCompression = TC_DistanceFieldFont;
		else if (CompressionStr == TEXT("HDR_Compressed"))        NewCompression = TC_HDR_Compressed;
		else if (CompressionStr == TEXT("BC7"))                   NewCompression = TC_BC7;

		Texture->CompressionSettings = NewCompression;
		ModifiedProperties.Add(TEXT("compressionSettings"));
	}

	// LOD group
	FString LODGroupStr;
	if (Params->TryGetStringField(TEXT("lodGroup"), LODGroupStr))
	{
		TextureGroup NewGroup = TEXTUREGROUP_World;
		if (LODGroupStr == TEXT("World"))                    NewGroup = TEXTUREGROUP_World;
		else if (LODGroupStr == TEXT("WorldNormalMap"))       NewGroup = TEXTUREGROUP_WorldNormalMap;
		else if (LODGroupStr == TEXT("WorldSpecular"))        NewGroup = TEXTUREGROUP_WorldSpecular;
		else if (LODGroupStr == TEXT("Character"))            NewGroup = TEXTUREGROUP_Character;
		else if (LODGroupStr == TEXT("CharacterNormalMap"))   NewGroup = TEXTUREGROUP_CharacterNormalMap;
		else if (LODGroupStr == TEXT("CharacterSpecular"))    NewGroup = TEXTUREGROUP_CharacterSpecular;
		else if (LODGroupStr == TEXT("Weapon"))               NewGroup = TEXTUREGROUP_Weapon;
		else if (LODGroupStr == TEXT("WeaponNormalMap"))      NewGroup = TEXTUREGROUP_WeaponNormalMap;
		else if (LODGroupStr == TEXT("WeaponSpecular"))       NewGroup = TEXTUREGROUP_WeaponSpecular;
		else if (LODGroupStr == TEXT("Vehicle"))              NewGroup = TEXTUREGROUP_Vehicle;
		else if (LODGroupStr == TEXT("VehicleNormalMap"))     NewGroup = TEXTUREGROUP_VehicleNormalMap;
		else if (LODGroupStr == TEXT("VehicleSpecular"))      NewGroup = TEXTUREGROUP_VehicleSpecular;
		else if (LODGroupStr == TEXT("UI"))                   NewGroup = TEXTUREGROUP_UI;
		else if (LODGroupStr == TEXT("Lightmap"))             NewGroup = TEXTUREGROUP_Lightmap;
		else if (LODGroupStr == TEXT("Shadowmap"))            NewGroup = TEXTUREGROUP_Shadowmap;
		else if (LODGroupStr == TEXT("Effects"))              NewGroup = TEXTUREGROUP_Effects;
		else if (LODGroupStr == TEXT("EffectsNotFiltered"))   NewGroup = TEXTUREGROUP_EffectsNotFiltered;
		else if (LODGroupStr == TEXT("Skybox"))               NewGroup = TEXTUREGROUP_Skybox;
		else if (LODGroupStr == TEXT("Pixels2D"))             NewGroup = TEXTUREGROUP_Pixels2D;

		Texture->LODGroup = NewGroup;
		ModifiedProperties.Add(TEXT("lodGroup"));
	}

	// sRGB
	bool bSRGB;
	if (Params->TryGetBoolField(TEXT("sRGB"), bSRGB))
	{
		Texture->SRGB = bSRGB;
		ModifiedProperties.Add(TEXT("sRGB"));
	}

	// NeverStream
	bool bNeverStream;
	if (Params->TryGetBoolField(TEXT("neverStream"), bNeverStream))
	{
		Texture->NeverStream = bNeverStream;
		ModifiedProperties.Add(TEXT("neverStream"));
	}

	if (ModifiedProperties.Num() == 0)
	{
		return MCPError(TEXT("No valid properties specified to set"));
	}

	// Notify the engine of property changes and mark dirty
	Texture->PostEditChange();
	Texture->MarkPackageDirty();

	TArray<TSharedPtr<FJsonValue>> ModifiedArray;
	for (const FString& PropName : ModifiedProperties)
	{
		ModifiedArray.Add(MakeShared<FJsonValueString>(PropName));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetArrayField(TEXT("modifiedProperties"), ModifiedArray);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Modified %d texture properties"), ModifiedProperties.Num()));

	// Complete self-inverse, routed the way SetTextureSettingsByType routes its
	// own: bulk_set_asset_properties writes UPROPERTYs by name and resolves an
	// enum from its enumerator name, so all four of these come back exactly.
	//
	// This deliberately does NOT go back through set_texture_settings. That
	// action maps compressionSettings from an 11-entry list and lodGroup from a
	// 19-entry list, and both fall through to a default (TC_Default,
	// TEXTUREGROUP_World) on anything outside them, so a prior value from
	// elsewhere in either enum would be silently rewritten to the wrong one.
	// GetNameStringByValue has no such gap.
	TSharedPtr<FJsonObject> Prev = MakeShared<FJsonObject>();
	for (const FString& P : ModifiedProperties)
	{
		if (P == TEXT("compressionSettings"))
		{
			Prev->SetStringField(TEXT("CompressionSettings"),
				StaticEnum<TextureCompressionSettings>()->GetNameStringByValue((int64)PrevCompression));
		}
		else if (P == TEXT("lodGroup"))
		{
			Prev->SetStringField(TEXT("LODGroup"),
				StaticEnum<TextureGroup>()->GetNameStringByValue((int64)PrevLODGroup));
		}
		else if (P == TEXT("sRGB"))       Prev->SetBoolField(TEXT("SRGB"), PrevSRGB);
		else if (P == TEXT("neverStream")) Prev->SetBoolField(TEXT("NeverStream"), PrevNeverStream);
	}
	// ModifiedProperties is never empty here: the handler returns an error above
	// when nothing was named, so there is always at least one field to restore.
	TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
	Item->SetStringField(TEXT("assetPath"), AssetPath);
	Item->SetObjectField(TEXT("properties"), Prev);
	TArray<TSharedPtr<FJsonValue>> Items;
	Items.Add(MakeShared<FJsonValueObject>(Item));
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetArrayField(TEXT("items"), Items);
	MCPSetRollback(Result, TEXT("bulk_set_asset_properties"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);

	return MCPResult(Result);
}


// #430: single-call batch of texture imports. Wraps N AssetImportTasks in one
// ImportAssetTasks call so the loop stays inside the editor (no per-import
// bridge round-trip). Per-item result records mirror what import_texture
// would have returned for each individual call.
TSharedPtr<FJsonValue> FAssetHandlers::ImportTextureBatch(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Params->TryGetArrayField(TEXT("items"), Items) || !Items)
	{
		return MCPError(TEXT("Missing 'items' array. Each entry: { filePath, packagePath?, name?, replaceExisting? }"));
	}

	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	const bool bAutomated = OptionalBool(Params, TEXT("automated"), true);
	const FString DefaultPackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Textures"));

	TArray<UAssetImportTask*> Tasks;
	TArray<FGCRootScope*> Roots;
	TArray<TSharedPtr<FJsonObject>> ItemRecords;
	Tasks.Reserve(Items->Num());
	ItemRecords.Reserve(Items->Num());

	for (const TSharedPtr<FJsonValue>& Entry : *Items)
	{
		TSharedPtr<FJsonObject> Obj = Entry.IsValid() ? Entry->AsObject() : nullptr;
		TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
		ItemRecords.Add(Rec);
		if (!Obj.IsValid())
		{
			Rec->SetBoolField(TEXT("success"), false);
			Rec->SetStringField(TEXT("error"), TEXT("Entry is not an object"));
			continue;
		}
		FString FilePath;
		if (!Obj->TryGetStringField(TEXT("filePath"), FilePath) || FilePath.IsEmpty())
		{
			Rec->SetBoolField(TEXT("success"), false);
			Rec->SetStringField(TEXT("error"), TEXT("Missing 'filePath'"));
			continue;
		}
		if (!FPaths::FileExists(FilePath))
		{
			Rec->SetBoolField(TEXT("success"), false);
			Rec->SetStringField(TEXT("filePath"), FilePath);
			Rec->SetStringField(TEXT("error"), FString::Printf(TEXT("File not found: %s"), *FilePath));
			continue;
		}

		FString PkgPath = DefaultPackagePath;
		Obj->TryGetStringField(TEXT("packagePath"), PkgPath);
		FString AssetName;
		Obj->TryGetStringField(TEXT("name"), AssetName);
		bool bReplaceExisting = true;
		Obj->TryGetBoolField(TEXT("replaceExisting"), bReplaceExisting);

		UTextureFactory* Factory = NewObject<UTextureFactory>();
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Roots.Add(new FGCRootScope(Factory));
		Roots.Add(new FGCRootScope(Task));
		Task->bAutomated = bAutomated;
		Task->bReplaceExisting = bReplaceExisting;
		Task->bSave = bSave;
		Task->Filename = FilePath;
		Task->DestinationPath = PkgPath;
		if (!AssetName.IsEmpty()) Task->DestinationName = AssetName;
		Task->Factory = Factory;

		Rec->SetStringField(TEXT("filePath"), FilePath);
		Rec->SetStringField(TEXT("packagePath"), PkgPath);
		if (!AssetName.IsEmpty()) Rec->SetStringField(TEXT("name"), AssetName);
		Tasks.Add(Task);
	}

	if (Tasks.Num() > 0)
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		AssetToolsModule.Get().ImportAssetTasks(Tasks);
	}

	// Map task results back to records by index.
	int32 Imported = 0;
	int32 TaskIdx = 0;
	for (TSharedPtr<FJsonObject>& Rec : ItemRecords)
	{
		bool bAlreadyFailed = false;
		Rec->TryGetBoolField(TEXT("success"), bAlreadyFailed);
		if (bAlreadyFailed == false && Rec->HasField(TEXT("error"))) continue; // validation rejection
		if (TaskIdx >= Tasks.Num()) break;
		UAssetImportTask* Task = Tasks[TaskIdx++];
		TArray<TSharedPtr<FJsonValue>> ImportedPaths;
		for (UObject* Imported2 : Task->GetObjects())
		{
			if (Imported2) ImportedPaths.Add(MakeShared<FJsonValueString>(Imported2->GetPathName()));
		}
		Rec->SetArrayField(TEXT("importedAssets"), ImportedPaths);
		Rec->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0);
		if (ImportedPaths.Num() > 0) Imported++;
	}

	for (FGCRootScope* G : Roots) delete G;

	auto Result = MCPSuccess();
	// Guarded: a batch that produced nothing used to report created:true.
	if (Imported > 0) MCPSetCreated(Result);
	Result->SetBoolField(TEXT("unchanged"), Imported == 0);
	Result->SetNumberField(TEXT("requested"), Items->Num());
	Result->SetNumberField(TEXT("imported"), Imported);
	Result->SetNumberField(TEXT("failed"), Items->Num() - Imported);

	TArray<TSharedPtr<FJsonValue>> RecArray;
	for (const TSharedPtr<FJsonObject>& Rec : ItemRecords)
	{
		RecArray.Add(MakeShared<FJsonValueObject>(Rec));
	}
	Result->SetArrayField(TEXT("items"), RecArray);

	// Rollback: delete exactly the assets this call produced. Lossy, because
	// entries default to replaceExisting=true: where an import landed on a
	// texture that was already there, deleting it removes the asset instead of
	// restoring the version the import overwrote.
	TArray<TSharedPtr<FJsonValue>> ImportedForRollback;
	for (const TSharedPtr<FJsonObject>& Rec : ItemRecords)
	{
		const TArray<TSharedPtr<FJsonValue>>* Produced = nullptr;
		if (Rec->TryGetArrayField(TEXT("importedAssets"), Produced) && Produced)
		{
			for (const TSharedPtr<FJsonValue>& P : *Produced) ImportedForRollback.Add(P);
		}
	}
	if (ImportedForRollback.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("assetPaths"), ImportedForRollback);
		MCPSetRollback(Result, TEXT("delete_asset_batch"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The inverse deletes the assets this import produced. Two limits. An entry that landed on an existing texture ")
			TEXT("(replaceExisting defaults to true) had its previous content overwritten, and deleting the asset does not bring ")
			TEXT("that back. And the delete runs with the default force=false, so it refuses any imported texture that something ")
			TEXT("outside the rollback still references, and reports that rather than removing it."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), TEXT("No asset was produced, so there is nothing to undo."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ImportTexture(const TSharedPtr<FJsonObject>& Params)
{
	FString FileName;
	if (auto Err = RequireStringAlt(Params, TEXT("filename"), TEXT("filePath"), FileName)) return Err;

	FString DestinationPath = OptionalString(Params, TEXT("destinationPath"), TEXT("/Game/Textures"));
	if (DestinationPath == TEXT("/Game/Textures"))
	{
		FString PkgPath = OptionalString(Params, TEXT("packagePath"));
		if (!PkgPath.IsEmpty()) DestinationPath = PkgPath;
	}

	if (!FPaths::FileExists(FileName))
	{
		return MCPError(FString::Printf(TEXT("File not found: %s"), *FileName));
	}

	UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
	FGCRootScope FactoryRoot(TextureFactory);

	// Apply optional settings
	bool bNoCompression = false;
	if (Params->TryGetBoolField(TEXT("noCompression"), bNoCompression))
	{
		TextureFactory->NoCompression = bNoCompression;
	}
	bool bNoAlpha = false;
	if (Params->TryGetBoolField(TEXT("noAlpha"), bNoAlpha))
	{
		TextureFactory->NoAlpha = bNoAlpha;
	}

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	FGCRootScope TaskRoot(Task);
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bSave = false;
	Task->Filename = FileName;
	Task->DestinationPath = DestinationPath;
	Task->Factory = TextureFactory;

	// Optional asset name
	FString AssetName;
	if (!Params->TryGetStringField(TEXT("assetName"), AssetName))
	{
		Params->TryGetStringField(TEXT("name"), AssetName);
	}
	if (!AssetName.IsEmpty())
	{
		Task->DestinationName = AssetName;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	TArray<TSharedPtr<FJsonValue>> ImportedPaths;
	if (Task->GetObjects().Num() > 0)
	{
		for (UObject* ImportedObj : Task->GetObjects())
		{
			if (ImportedObj)
			{
				FString ObjPath = ImportedObj->GetPathName();
				ImportedPaths.Add(MakeShared<FJsonValueString>(ObjPath));
			}
		}
	}

	auto Result = MCPSuccess();
	if (ImportedPaths.Num() > 0) { MCPSetCreated(Result); }
	Result->SetStringField(TEXT("filename"), FileName);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetArrayField(TEXT("importedAssets"), ImportedPaths);
	Result->SetNumberField(TEXT("importedCount"), ImportedPaths.Num());
	Result->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0);
	if (ImportedPaths.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("Import task completed but no assets were produced"));
	}

	// #661: honor sRGB / compressionSettings / lodGroup at import time. The
	// UTextureFactory does not take these, so apply them to the imported
	// texture in the same call (folding in the set_texture_settings path) so
	// callers no longer need a second round-trip.
	if (ImportedPaths.Num() == 1 &&
		(Params->HasField(TEXT("sRGB")) || Params->HasField(TEXT("compressionSettings")) ||
		 Params->HasField(TEXT("lodGroup")) || Params->HasField(TEXT("neverStream"))))
	{
		TSharedPtr<FJsonObject> SettingsParams = MakeShared<FJsonObject>();
		SettingsParams->SetStringField(TEXT("assetPath"), ImportedPaths[0]->AsString());
		if (Params->HasField(TEXT("sRGB"))) SettingsParams->SetBoolField(TEXT("sRGB"), OptionalBool(Params, TEXT("sRGB"), true));
		if (Params->HasField(TEXT("compressionSettings"))) SettingsParams->SetStringField(TEXT("compressionSettings"), OptionalString(Params, TEXT("compressionSettings")));
		if (Params->HasField(TEXT("lodGroup"))) SettingsParams->SetStringField(TEXT("lodGroup"), OptionalString(Params, TEXT("lodGroup")));
		if (Params->HasField(TEXT("neverStream"))) SettingsParams->SetBoolField(TEXT("neverStream"), OptionalBool(Params, TEXT("neverStream"), false));
		TSharedPtr<FJsonValue> SettingsResult = SetTextureProperties(SettingsParams);
		if (SettingsResult.IsValid() && SettingsResult->AsObject().IsValid())
		{
			const TSharedPtr<FJsonObject> SO = SettingsResult->AsObject();
			bool bSettingsOk = false;
			SO->TryGetBoolField(TEXT("success"), bSettingsOk);
			Result->SetBoolField(TEXT("settingsApplied"), bSettingsOk);
			const TArray<TSharedPtr<FJsonValue>>* Mod = nullptr;
			if (SO->TryGetArrayField(TEXT("modifiedProperties"), Mod) && Mod)
			{
				Result->SetArrayField(TEXT("appliedSettings"), *Mod);
			}
		}
	}

	EmitImportedAssetsRollback(Result, ImportedPaths);

	return MCPResult(Result);
}


// ============================================================================
// CurveTable handlers
// ============================================================================

TSharedPtr<FJsonValue> FAssetHandlers::CreateCurveTable(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/CurveTables"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	auto Created = MCPCreateAssetIdempotentNewObject<UCurveTable>(Name, PackagePath, OnConflict, TEXT("CurveTable"));
	if (Created.EarlyReturn)
	{
		if (TSharedPtr<FJsonObject> ExistingObj = Created.EarlyReturn->AsObject())
		{
			bool bExisted = false;
			if (ExistingObj->TryGetBoolField(TEXT("existed"), bExisted) && bExisted)
			{
				FString ExistingAssetPath;
				ExistingObj->TryGetStringField(TEXT("path"), ExistingAssetPath);
				if (UCurveTable* Existing = LoadAssetByPath<UCurveTable>(ExistingAssetPath))
				{
					ExistingObj->SetStringField(TEXT("assetPath"), Existing->GetPathName());
					ExistingObj->SetStringField(TEXT("curveType"), CurveTableModeName(Existing->GetCurveTableMode()));
					ExistingObj->SetNumberField(TEXT("rowCount"), Existing->GetRowMap().Num());
				}
			}
		}
		return Created.EarlyReturn;
	}

	UCurveTable* Table = Created.Asset;
	SaveCurveTableChange(Table);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	Result->SetStringField(TEXT("assetPath"), Table->GetPathName());
	Result->SetStringField(TEXT("curveType"), CurveTableModeName(Table->GetCurveTableMode()));
	Result->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());
	MCPSetDeleteAssetRollback(Result, Table->GetPathName());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ReadCurveTable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = LoadCurveTable(Params, Table, AssetPath)) return Err;

	const FString RowFilter = OptionalString(Params, TEXT("rowFilter")).ToLower();
	const ECurveTableMode Mode = Table->GetCurveTableMode();

	TArray<TSharedPtr<FJsonValue>> Rows;
	TArray<TSharedPtr<FJsonValue>> RowNames;
	for (const TPair<FName, FRealCurve*>& Pair : Table->GetRowMap())
	{
		const FString Name = Pair.Key.ToString();
		RowNames.Add(MakeShared<FJsonValueString>(Name));
		if (!RowFilter.IsEmpty() && !Name.ToLower().Contains(RowFilter))
		{
			continue;
		}

		TArray<TSharedPtr<FJsonValue>> Keys = CurveKeysToJson(Pair.Value, Mode);
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), Name);
		Row->SetNumberField(TEXT("keyCount"), Keys.Num());
		Row->SetArrayField(TEXT("keys"), Keys);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("curveType"), CurveTableModeName(Mode));
	Result->SetArrayField(TEXT("rows"), Rows);
	Result->SetArrayField(TEXT("rowNames"), RowNames);
	Result->SetNumberField(TEXT("rowCount"), Rows.Num());
	Result->SetNumberField(TEXT("totalRowCount"), Table->GetRowMap().Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ImportCurveTable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = LoadCurveTable(Params, Table, AssetPath)) return Err;

	FString Data;
	FString Format = OptionalString(Params, TEXT("format")).ToLower();
	if (Params->TryGetStringField(TEXT("jsonString"), Data) && !Data.IsEmpty())
	{
		Format = TEXT("json");
	}
	else if (Params->TryGetStringField(TEXT("csvString"), Data) && !Data.IsEmpty())
	{
		Format = TEXT("csv");
	}
	else
	{
		FString FilePath;
		if (!Params->TryGetStringField(TEXT("filePath"), FilePath) &&
			!Params->TryGetStringField(TEXT("jsonPath"), FilePath) &&
			!Params->TryGetStringField(TEXT("csvPath"), FilePath))
		{
			return MCPError(TEXT("Missing jsonString, csvString, or filePath"));
		}
		if (!FPaths::FileExists(FilePath))
		{
			return MCPError(FString::Printf(TEXT("File not found: %s"), *FilePath));
		}
		if (!FFileHelper::LoadFileToString(Data, *FilePath))
		{
			return MCPError(FString::Printf(TEXT("Failed to read file: %s"), *FilePath));
		}
		if (Format.IsEmpty())
		{
			Format = FPaths::GetExtension(FilePath).ToLower() == TEXT("json") ? TEXT("json") : TEXT("csv");
		}
	}

	ERichCurveInterpMode InterpMode = RCIM_Linear;
	const FString InterpRaw = OptionalString(Params, TEXT("interpMode"), TEXT("linear"));
	if (!ParseCurveInterpMode(InterpRaw, InterpMode))
	{
		return MCPError(FString::Printf(TEXT("Unknown interpMode '%s'. Use linear, constant, cubic or none."), *InterpRaw));
	}

	// CreateTableFrom*String empties the table first, so the rows that are
	// there now are the only record of the pre-call state. UCurveTable's own
	// JSON writer is the reader's counterpart, so the snapshot replays through
	// this same action.
	const int32 PriorRowCount = Table->GetRowMap().Num();
	const ECurveTableMode PriorMode = Table->GetCurveTableMode();
	const FString PriorJson = PriorRowCount > 0 ? Table->GetTableAsJSON() : FString();

	TArray<FString> Problems;
	if (Format == TEXT("json"))
	{
		Problems = Table->CreateTableFromJSONString(Data, InterpMode);
	}
	else if (Format == TEXT("csv"))
	{
		Problems = Table->CreateTableFromCSVString(Data, InterpMode);
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Unknown format '%s'. Use json or csv."), *Format));
	}

	if (Problems.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Errors;
		for (const FString& Problem : Problems)
		{
			Errors.Add(MakeShared<FJsonValueString>(Problem));
		}
		auto Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetArrayField(TEXT("errors"), Errors);
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("CurveTable import completed with %d problem(s)"), Problems.Num()));
		// The importer emptied the table before it hit these problems, so this
		// failure is MORE destructive than the success path, not less. The
		// snapshot is the only way back and it is already in hand.
		Result->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());
		EmitPriorCurveTableRollback(Result, AssetPath, PriorJson, PriorRowCount, PriorMode);
		return MCPResult(Result);
	}

	SaveCurveTableChange(Table);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("format"), Format);
	Result->SetStringField(TEXT("curveType"), CurveTableModeName(Table->GetCurveTableMode()));
	Result->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());

	EmitPriorCurveTableRollback(Result, AssetPath, PriorJson, PriorRowCount, PriorMode);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::AddCurveTableRow(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = LoadCurveTable(Params, Table, AssetPath)) return Err;

	FString RowName;
	if (auto Err = RequireString(Params, TEXT("rowName"), RowName)) return Err;
	const FName RowKey(*RowName);
	if (Table->GetRowMap().Contains(RowKey))
	{
		auto Result = MCPSuccess();
		MCPSetExisted(Result);
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetStringField(TEXT("rowName"), RowName);
		return MCPResult(Result);
	}

	ERichCurveInterpMode InterpMode = RCIM_Linear;
	const FString InterpRaw = OptionalString(Params, TEXT("interpMode"), TEXT("linear"));
	if (!ParseCurveInterpMode(InterpRaw, InterpMode))
	{
		return MCPError(FString::Printf(TEXT("Unknown interpMode '%s'. Use linear, constant, cubic or none."), *InterpRaw));
	}

	FString CurveType = OptionalString(Params, TEXT("curveType"), OptionalString(Params, TEXT("mode")));
	CurveType = CurveType.ToLower();
	bool bRich = InterpMode == RCIM_Cubic;
	if (Table->GetCurveTableMode() == ECurveTableMode::RichCurves) bRich = true;
	if (Table->GetCurveTableMode() == ECurveTableMode::SimpleCurves) bRich = false;
	if (CurveType == TEXT("rich")) bRich = true;
	if (CurveType == TEXT("simple")) bRich = false;

	if (!bRich && InterpMode == RCIM_Cubic)
	{
		return MCPError(TEXT("Simple CurveTables cannot use cubic interpolation; use curveType='rich'."));
	}
	if (bRich && Table->GetCurveTableMode() == ECurveTableMode::SimpleCurves)
	{
		return MCPError(TEXT("Cannot add a rich row to a simple CurveTable."));
	}
	if (!bRich && Table->GetCurveTableMode() == ECurveTableMode::RichCurves)
	{
		return MCPError(TEXT("Cannot add a simple row to a rich CurveTable."));
	}

	// Adding the first row also moves the table off ECurveTableMode::Empty, and
	// RemoveRow does not move it back: only EmptyTable resets the mode, and no
	// action calls it. So the inverse is exact for every row but the first.
	const bool bTableWasEmptyMode = Table->GetCurveTableMode() == ECurveTableMode::Empty;

	if (bRich)
	{
		Table->AddRichCurve(RowKey);
	}
	else
	{
		FSimpleCurve& Curve = Table->AddSimpleCurve(RowKey);
		Curve.SetKeyInterpMode(InterpMode);
	}

	SaveCurveTableChange(Table);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("rowName"), RowName);
	Result->SetStringField(TEXT("curveType"), CurveTableModeName(Table->GetCurveTableMode()));
	Result->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());

	// The row is created empty, so removing it takes the table back to where it
	// was, with one exception handled below.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("rowName"), RowName);
	MCPSetRollback(Result, TEXT("remove_curvetable_row"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), bTableWasEmptyMode);
	if (bTableWasEmptyMode)
	{
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("This was the first row, so it also set the table's curve mode from empty to %s. The inverse removes the row, ")
			TEXT("but only EmptyTable resets that mode and no action calls it, so the table stays typed and will refuse a row of ")
			TEXT("the other curve type afterwards."),
			*CurveTableModeName(Table->GetCurveTableMode())));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::RemoveCurveTableRow(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = LoadCurveTable(Params, Table, AssetPath)) return Err;

	FString RowName;
	if (auto Err = RequireString(Params, TEXT("rowName"), RowName)) return Err;
	const FName RowKey(*RowName);
	if (!Table->GetRowMap().Contains(RowKey))
	{
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("alreadyDeleted"), true);
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetStringField(TEXT("rowName"), RowName);
		return MCPResult(Result);
	}

	// The keys go with the row, and add_curvetable_row only puts an empty row
	// back, so hand them to the caller as well as naming the loss below.
	const ECurveTableMode PriorMode = Table->GetCurveTableMode();
	FRealCurve* const PriorCurve = GetCurveTableRow(Table, RowName);
	const TArray<TSharedPtr<FJsonValue>> RemovedKeys = CurveKeysToJson(PriorCurve, PriorMode);
	// A simple curve's interpolation mode is a property of the row, not of its
	// keys, and add_curvetable_row takes it, so it does not have to be lost.
	FString PriorSimpleInterp;
	if (PriorMode == ECurveTableMode::SimpleCurves && PriorCurve)
	{
		PriorSimpleInterp = CurveInterpModeName(static_cast<FSimpleCurve*>(PriorCurve)->GetKeyInterpMode());
	}

	Table->RemoveRow(RowKey);
	SaveCurveTableChange(Table);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("rowName"), RowName);
	Result->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());
	Result->SetArrayField(TEXT("removedKeys"), RemovedKeys);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("rowName"), RowName);
	if (PriorMode == ECurveTableMode::RichCurves)       Payload->SetStringField(TEXT("curveType"), TEXT("rich"));
	else if (PriorMode == ECurveTableMode::SimpleCurves) Payload->SetStringField(TEXT("curveType"), TEXT("simple"));
	if (!PriorSimpleInterp.IsEmpty()) Payload->SetStringField(TEXT("interpMode"), PriorSimpleInterp);
	MCPSetRollback(Result, TEXT("add_curvetable_row"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), RemovedKeys.Num() > 0);
	if (RemovedKeys.Num() > 0)
	{
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The inverse puts the row back under the same name and curve type, but empty: add_curvetable_row does not take keys, ")
			TEXT("so the %d key(s) this call removed do not come back. They are in removedKeys, and asset(set_curvetable_keys) replays them."),
			RemovedKeys.Num()));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::RenameCurveTableRow(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = LoadCurveTable(Params, Table, AssetPath)) return Err;

	FString OldName;
	if (auto Err = RequireStringAlt(Params, TEXT("oldName"), TEXT("rowName"), OldName)) return Err;
	FString NewName;
	if (auto Err = RequireString(Params, TEXT("newName"), NewName)) return Err;

	FName OldKey(*OldName);
	FName NewKey(*NewName);
	if (OldKey == NewKey)
	{
		auto Result = MCPSuccess();
		MCPSetExisted(Result);
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetStringField(TEXT("rowName"), OldName);
		return MCPResult(Result);
	}
	if (!Table->GetRowMap().Contains(OldKey))
	{
		return MCPError(FString::Printf(TEXT("Row not found: %s"), *OldName));
	}
	if (Table->GetRowMap().Contains(NewKey))
	{
		return MCPError(FString::Printf(TEXT("Target row already exists: %s"), *NewName));
	}

	Table->RenameRow(OldKey, NewKey);
	SaveCurveTableChange(Table);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("oldName"), OldName);
	Result->SetStringField(TEXT("newName"), NewName);

	// Self-inverse: rename back. The row keeps its curve either way.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("oldName"), NewName);
	Payload->SetStringField(TEXT("newName"), OldName);
	MCPSetRollback(Result, TEXT("rename_curvetable_row"), Payload);
	// Declared lossy for the same row-position effect rename_datatable_row
	// declares, and for the conservative reason: UCurveTable::RenameRow is
	// ENGINE_API with no implementation in the installed tree, so whether it
	// keeps the row's place in the row map is not established here. Claiming
	// an exact inverse would be asserting the half that has not been checked.
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The inverse restores the row name and its curve. Whether the row returns to its original position in the row map ")
		TEXT("is not established: UCurveTable::RenameRow's implementation is not in the installed engine tree, and the DataTable ")
		TEXT("equivalent is known to move a renamed row to the end."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::GetCurveTableKeys(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = LoadCurveTable(Params, Table, AssetPath)) return Err;

	FString RowName;
	if (auto Err = RequireString(Params, TEXT("rowName"), RowName)) return Err;
	FRealCurve* Curve = GetCurveTableRow(Table, RowName);
	if (!Curve)
	{
		return MCPError(FString::Printf(TEXT("Row not found: %s"), *RowName));
	}

	TArray<TSharedPtr<FJsonValue>> Keys = CurveKeysToJson(Curve, Table->GetCurveTableMode());
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("rowName"), RowName);
	Result->SetStringField(TEXT("curveType"), CurveTableModeName(Table->GetCurveTableMode()));
	Result->SetArrayField(TEXT("keys"), Keys);
	Result->SetNumberField(TEXT("keyCount"), Keys.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::SetCurveTableKeys(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = LoadCurveTable(Params, Table, AssetPath)) return Err;

	FString RowName;
	if (auto Err = RequireString(Params, TEXT("rowName"), RowName)) return Err;
	FRealCurve* Curve = GetCurveTableRow(Table, RowName);
	if (!Curve)
	{
		return MCPError(FString::Printf(TEXT("Row not found: %s"), *RowName));
	}

	const TArray<TSharedPtr<FJsonValue>>* KeyValues = nullptr;
	if (!Params->TryGetArrayField(TEXT("keys"), KeyValues) || !KeyValues)
	{
		return MCPError(TEXT("Missing keys array. Each key is { time, value, interpMode? }."));
	}

	// This replaces the row's whole key list, so the list it is about to
	// replace is the inverse call's payload, in the { time, value, interpMode,
	// arriveTangent?, leaveTangent? } shape read back in below.
	//
	// That shape is everything a simple curve has, and less than a rich one
	// has: FRichCurveKey also carries TangentMode, TangentWeightMode and the
	// two tangent weights, and the rich branch below rebuilds each key with
	// FRichCurveKey(Time, Value), whose defaults are RCTM_Auto,
	// RCTWM_WeightedNone and zero weights. So a rich replay resets break and
	// user tangent modes and every weight. Flagged rather than silently lost.
	const bool bPriorRich = Table->GetCurveTableMode() == ECurveTableMode::RichCurves;
	const TArray<TSharedPtr<FJsonValue>> PriorKeys = CurveKeysToJson(Curve, Table->GetCurveTableMode());

	if (Table->GetCurveTableMode() == ECurveTableMode::SimpleCurves)
	{
		FSimpleCurve* Simple = static_cast<FSimpleCurve*>(Curve);
		TArray<FSimpleCurveKey> Keys;
		ERichCurveInterpMode SharedInterp = Simple->GetKeyInterpMode();
		bool bSawInterp = false;
		for (const TSharedPtr<FJsonValue>& KeyValue : *KeyValues)
		{
			const TSharedPtr<FJsonObject>* KeyObj = nullptr;
			if (!KeyValue.IsValid() || !KeyValue->TryGetObject(KeyObj) || !KeyObj || !(*KeyObj).IsValid())
			{
				return MCPError(TEXT("Each key must be an object: { time, value, interpMode? }"));
			}
			double Time = 0.0;
			double Value = 0.0;
			if (!(*KeyObj)->TryGetNumberField(TEXT("time"), Time) || !(*KeyObj)->TryGetNumberField(TEXT("value"), Value))
			{
				return MCPError(TEXT("Each key needs numeric time and value fields."));
			}
			FString InterpRaw;
			if ((*KeyObj)->TryGetStringField(TEXT("interpMode"), InterpRaw))
			{
				ERichCurveInterpMode Parsed = RCIM_Linear;
				if (!ParseCurveInterpMode(InterpRaw, Parsed)) return MCPError(FString::Printf(TEXT("Unknown interpMode '%s'. Use linear, constant, cubic or none."), *InterpRaw));
				if (Parsed == RCIM_Cubic) return MCPError(TEXT("Simple CurveTables cannot use cubic interpolation."));
				if (bSawInterp && Parsed != SharedInterp) return MCPError(TEXT("Simple CurveTables require one shared interpMode for all keys."));
				SharedInterp = Parsed;
				bSawInterp = true;
			}
			Keys.Add(FSimpleCurveKey((float)Time, (float)Value));
		}
		Keys.Sort([](const FSimpleCurveKey& A, const FSimpleCurveKey& B) { return A.Time < B.Time; });
		Simple->SetKeys(Keys);
		Simple->SetKeyInterpMode(SharedInterp);
	}
	else if (Table->GetCurveTableMode() == ECurveTableMode::RichCurves)
	{
		FRichCurve* Rich = static_cast<FRichCurve*>(Curve);
		TArray<FRichCurveKey> Keys;
		for (const TSharedPtr<FJsonValue>& KeyValue : *KeyValues)
		{
			const TSharedPtr<FJsonObject>* KeyObj = nullptr;
			if (!KeyValue.IsValid() || !KeyValue->TryGetObject(KeyObj) || !KeyObj || !(*KeyObj).IsValid())
			{
				return MCPError(TEXT("Each key must be an object: { time, value, interpMode? }."));
			}
			double Time = 0.0;
			double Value = 0.0;
			if (!(*KeyObj)->TryGetNumberField(TEXT("time"), Time) || !(*KeyObj)->TryGetNumberField(TEXT("value"), Value))
			{
				return MCPError(TEXT("Each key needs numeric time and value fields."));
			}
			ERichCurveInterpMode Interp = RCIM_Linear;
			FString InterpRaw;
			if ((*KeyObj)->TryGetStringField(TEXT("interpMode"), InterpRaw) && !ParseCurveInterpMode(InterpRaw, Interp))
			{
				return MCPError(FString::Printf(TEXT("Unknown interpMode '%s'. Use linear, constant, cubic or none."), *InterpRaw));
			}
			FRichCurveKey Key((float)Time, (float)Value);
			Key.InterpMode = Interp;
			double Tangent = 0.0;
			if ((*KeyObj)->TryGetNumberField(TEXT("arriveTangent"), Tangent)) Key.ArriveTangent = (float)Tangent;
			if ((*KeyObj)->TryGetNumberField(TEXT("leaveTangent"), Tangent)) Key.LeaveTangent = (float)Tangent;
			Keys.Add(Key);
		}
		Keys.Sort([](const FRichCurveKey& A, const FRichCurveKey& B) { return A.Time < B.Time; });
		Rich->SetKeys(Keys);
	}
	else
	{
		return MCPError(TEXT("CurveTable has no curve type yet; add a row first."));
	}

	SaveCurveTableChange(Table);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("rowName"), RowName);
	Result->SetStringField(TEXT("curveType"), CurveTableModeName(Table->GetCurveTableMode()));
	Result->SetNumberField(TEXT("keyCount"), KeyValues->Num());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("rowName"), RowName);
	Payload->SetArrayField(TEXT("keys"), PriorKeys);
	MCPSetRollback(Result, TEXT("set_curvetable_keys"), Payload);
	EmitCurveKeyReplayCaveat(Result, bPriorRich, PriorKeys.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::AddCurveTableKey(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = LoadCurveTable(Params, Table, AssetPath)) return Err;

	FString RowName;
	if (auto Err = RequireString(Params, TEXT("rowName"), RowName)) return Err;
	FRealCurve* Curve = GetCurveTableRow(Table, RowName);
	if (!Curve)
	{
		return MCPError(FString::Printf(TEXT("Row not found: %s"), *RowName));
	}

	double Time = 0.0;
	double Value = 0.0;
	if (!Params->TryGetNumberField(TEXT("time"), Time) || !Params->TryGetNumberField(TEXT("value"), Value))
	{
		return MCPError(TEXT("Missing numeric time and value fields."));
	}

	ERichCurveInterpMode InterpMode = RCIM_Linear;
	const FString InterpRaw = OptionalString(Params, TEXT("interpMode"), TEXT("linear"));
	if (!ParseCurveInterpMode(InterpRaw, InterpMode))
	{
		return MCPError(FString::Printf(TEXT("Unknown interpMode '%s'. Use linear, constant, cubic or none."), *InterpRaw));
	}
	const float Tolerance = (float)OptionalNumber(Params, TEXT("keyTimeTolerance"), UE_KINDA_SMALL_NUMBER);

	// UpdateOrAddKey either adds a key or overwrites the one already at that
	// time, so neither "remove the key" nor "write the old value" is the
	// inverse on its own. The row's whole prior key list is, and
	// set_curvetable_keys takes exactly that, with the rich-curve caveat
	// EmitCurveKeyReplayCaveat states.
	const bool bPriorRich = Table->GetCurveTableMode() == ECurveTableMode::RichCurves;
	const TArray<TSharedPtr<FJsonValue>> PriorKeys = CurveKeysToJson(Curve, Table->GetCurveTableMode());

	if (Table->GetCurveTableMode() == ECurveTableMode::SimpleCurves)
	{
		if (InterpMode == RCIM_Cubic) return MCPError(TEXT("Simple CurveTables cannot use cubic interpolation."));
		FSimpleCurve* Simple = static_cast<FSimpleCurve*>(Curve);
		const FKeyHandle Handle = Simple->UpdateOrAddKey((float)Time, (float)Value, false, Tolerance);
		Simple->SetKeyInterpMode(Handle, InterpMode);
	}
	else if (Table->GetCurveTableMode() == ECurveTableMode::RichCurves)
	{
		FRichCurve* Rich = static_cast<FRichCurve*>(Curve);
		const FKeyHandle Handle = Rich->UpdateOrAddKey((float)Time, (float)Value, false, Tolerance);
		Rich->SetKeyInterpMode(Handle, InterpMode);
	}
	else
	{
		return MCPError(TEXT("CurveTable has no curve type yet; add a row first."));
	}

	SaveCurveTableChange(Table);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("rowName"), RowName);
	Result->SetNumberField(TEXT("time"), Time);
	Result->SetNumberField(TEXT("value"), Value);
	Result->SetStringField(TEXT("interpMode"), CurveInterpModeName(InterpMode));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("rowName"), RowName);
	Payload->SetArrayField(TEXT("keys"), PriorKeys);
	MCPSetRollback(Result, TEXT("set_curvetable_keys"), Payload);
	EmitCurveKeyReplayCaveat(Result, bPriorRich, PriorKeys.Num());
	return MCPResult(Result);
}

// ============================================================================
// Additional DataTable handlers
// ============================================================================

TSharedPtr<FJsonValue> FAssetHandlers::CreateDataTable(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString RowStruct;
	if (auto Err = RequireString(Params, TEXT("rowStruct"), RowStruct)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/DataTables"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	// Find the row struct type
	UScriptStruct* ScriptStruct = nullptr;
	ScriptStruct = LoadAssetByPath<UScriptStruct>(RowStruct);
	if (!ScriptStruct)
	{
		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			if (It->GetName() == RowStruct)
			{
				ScriptStruct = *It;
				break;
			}
		}
	}
	if (!ScriptStruct)
	{
		return MCPError(FString::Printf(TEXT("Row struct not found: %s"), *RowStruct));
	}

	UDataTableFactory* Factory = NewObject<UDataTableFactory>();
	Factory->Struct = ScriptStruct;

	auto Created = MCPCreateAssetIdempotent<UDataTable>(Name, PackagePath, OnConflict, TEXT("DataTable"), Factory);
	if (Created.EarlyReturn)
	{
		// Augment the Existed payload with DataTable-specific fields if it was an idempotency hit.
		if (TSharedPtr<FJsonObject> ExistingObj = Created.EarlyReturn->AsObject())
		{
			bool bExisted = false;
			if (ExistingObj->TryGetBoolField(TEXT("existed"), bExisted) && bExisted)
			{
				FString ExistingAssetPath;
				ExistingObj->TryGetStringField(TEXT("path"), ExistingAssetPath);
				if (UDataTable* Existing = LoadAssetByPath<UDataTable>(ExistingAssetPath))
				{
					ExistingObj->SetStringField(TEXT("assetPath"), Existing->GetPathName());
					ExistingObj->SetStringField(TEXT("rowStruct"), Existing->RowStruct ? Existing->RowStruct->GetName() : TEXT(""));
					ExistingObj->SetNumberField(TEXT("rowCount"), Existing->GetRowMap().Num());
				}
			}
		}
		return Created.EarlyReturn;
	}
	UDataTable* DataTable = Created.Asset;
	const FString AssetPath = DataTable->GetPathName();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("rowStruct"), ScriptStruct->GetName());
	Result->SetNumberField(TEXT("rowCount"), DataTable ? DataTable->GetRowMap().Num() : 0);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);

	return MCPResult(Result);
}


namespace
{
	// #930: resolve the DataTable parameter the way asset(read) resolves any
	// asset, so the type-specific actions succeed on every path form the
	// generic reader already opens. Returns a ready-to-return error, or an
	// unset pointer on success with OutAssetPath and OutTable filled in.
	//
	// The "not a DataTable" message names the class that was found, because
	// the old wording was also what a caller saw when the path resolved to
	// nothing at all.
	TSharedPtr<FJsonValue> LoadDataTableParam(
		const TSharedPtr<FJsonObject>& Params,
		FString& OutAssetPath,
		UDataTable*& OutTable)
	{
		if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), OutAssetPath)) return Err;

		TSharedPtr<FJsonValue> LoadError;
		UObject* Asset = MCPRequireAssetObject(OutAssetPath, LoadError);
		if (!Asset) return LoadError;
		OutTable = Cast<UDataTable>(Asset);
		if (!OutTable)
		{
			return MCPAssetWrongTypeError(OutAssetPath, Asset, TEXT("DataTable"));
		}
		return nullptr;
	}

	/** The inverse of a whole-table DataTable import, from the snapshot taken
	 *  before it ran. Shared by the success and the failure path, because
	 *  CreateTableFromJSONString empties the table before it reports errors. */
	void EmitPriorDataTableRollback(
		TSharedPtr<FJsonObject> Result,
		const FString& AssetPath,
		const FString& PriorJson,
		const int32 PriorRowCount)
	{
		if (PriorRowCount > 0)
		{
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("assetPath"), AssetPath);
			Payload->SetStringField(TEXT("jsonString"), PriorJson);
			MCPSetRollback(Result, TEXT("reimport_datatable"), Payload);
			// Marked lossy for the same reason its CurveTable twin is: a whole
			// table replayed through a JSON export only carries what that form
			// can hold. FText is the known gap, and it is the one this module
			// already documents (HandlerJsonProperty.h: "FText identity is per
			// instance"), so a restored FText cell keeps its string and loses
			// the namespace and key it was authored under.
			Result->SetBoolField(TEXT("rollbackLossy"), true);
			Result->SetStringField(TEXT("rollbackNote"),
				TEXT("The inverse replays the table's pre-call JSON export, which carries every row and every field value. ")
				TEXT("An FText field comes back as its source string under a fresh localization namespace and key rather than ")
				TEXT("the ones it was authored with, because that identity is per instance and the JSON form does not carry it."));
		}
		else
		{
			Result->SetBoolField(TEXT("rollbackPossible"), false);
			Result->SetStringField(TEXT("rollbackNote"),
				TEXT("The table had no rows before this call. Restoring that would mean importing an empty row set, and whether ")
				TEXT("the engine's DataTable importer accepts one is not established from the installed headers, so no inverse is ")
				TEXT("emitted rather than one that may report success without emptying the table. ")
				TEXT("Remove the imported rows with asset(remove_datatable_row)."));
		}
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::ReadDataTable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = LoadDataTableParam(Params, AssetPath, DataTable)) return Err;

	FString RowFilter = OptionalString(Params, TEXT("rowFilter"));

	// Get the row struct for property iteration
	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct)
	{
		return MCPError(TEXT("DataTable has no row struct"));
	}

	// Export the table as JSON for reliable serialization, then parse it
	FString JsonString = DataTable->GetTableAsJSON(EDataTableExportFlags::UseJsonObjectsForStructs);

	auto Result = MCPSuccess();

	TArray<TSharedPtr<FJsonValue>> ParsedRows;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
	if (FJsonSerializer::Deserialize(JsonReader, ParsedRows))
	{
		// Apply row filter if specified
		if (!RowFilter.IsEmpty())
		{
			FString FilterLower = RowFilter.ToLower();
			TArray<TSharedPtr<FJsonValue>> FilteredRows;
			for (const TSharedPtr<FJsonValue>& RowValue : ParsedRows)
			{
				if (RowValue.IsValid() && RowValue->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject>& RowObj = RowValue->AsObject();
					FString RowName;
					if (RowObj->TryGetStringField(TEXT("Name"), RowName) && RowName.ToLower().Contains(FilterLower))
					{
						FilteredRows.Add(RowValue);
					}
				}
			}
			Result->SetArrayField(TEXT("rows"), FilteredRows);
			Result->SetNumberField(TEXT("filteredCount"), FilteredRows.Num());
		}
		else
		{
			Result->SetArrayField(TEXT("rows"), ParsedRows);
		}
	}
	else
	{
		// Fallback: return the raw JSON string
		Result->SetStringField(TEXT("rawJson"), JsonString);
	}

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("rowStruct"), RowStruct->GetName());
	Result->SetNumberField(TEXT("totalRowCount"), DataTable->GetRowMap().Num());

	// Also list the row names
	TArray<TSharedPtr<FJsonValue>> RowNames;
	for (const auto& Pair : DataTable->GetRowMap())
	{
		RowNames.Add(MakeShared<FJsonValueString>(Pair.Key.ToString()));
	}
	Result->SetArrayField(TEXT("rowNames"), RowNames);

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAssetHandlers::ReimportDataTable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = LoadDataTableParam(Params, AssetPath, DataTable)) return Err;

	// Get JSON string from either inline jsonString or from a file path
	FString JsonString;
	if (!Params->TryGetStringField(TEXT("jsonString"), JsonString) || JsonString.IsEmpty())
	{
		FString JsonPath;
		if (Params->TryGetStringField(TEXT("jsonPath"), JsonPath) && !JsonPath.IsEmpty())
		{
			if (!FPaths::FileExists(JsonPath))
			{
				return MCPError(FString::Printf(TEXT("JSON file not found: %s"), *JsonPath));
			}
			if (!FFileHelper::LoadFileToString(JsonString, *JsonPath))
			{
				return MCPError(FString::Printf(TEXT("Failed to read JSON file: %s"), *JsonPath));
			}
		}
		else
		{
			return MCPError(TEXT("Missing 'jsonString' or 'jsonPath' parameter"));
		}
	}

	// CreateTableFromJSONString replaces the whole table, so the rows standing
	// now are the only record of the pre-call state. GetTableAsJSON is the
	// writer that pairs with that reader, and asset(read_datatable) already
	// relies on the same export, so the snapshot replays through this action.
	const int32 PriorRowCount = DataTable->GetRowMap().Num();
	const FString PriorJson = PriorRowCount > 0
		? DataTable->GetTableAsJSON(EDataTableExportFlags::UseJsonObjectsForStructs)
		: FString();

	TArray<FString> Errors = DataTable->CreateTableFromJSONString(JsonString);

	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorsArray;
		for (const FString& Error : Errors)
		{
			ErrorsArray.Add(MakeShared<FJsonValueString>(Error));
		}
		TSharedPtr<FJsonObject> ErrResult = MakeShared<FJsonObject>();
		ErrResult->SetBoolField(TEXT("success"), false);
		ErrResult->SetArrayField(TEXT("errors"), ErrorsArray);
		ErrResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Reimport completed with %d error(s)"), Errors.Num()));
		// CreateTableFromJSONString emptied the table before it hit these
		// errors, so this failure is MORE destructive than the success path.
		// The snapshot is the only way back and it is already in hand.
		ErrResult->SetStringField(TEXT("assetPath"), AssetPath);
		ErrResult->SetNumberField(TEXT("rowCount"), DataTable->GetRowMap().Num());
		EmitPriorDataTableRollback(ErrResult, AssetPath, PriorJson, PriorRowCount);
		return MCPResult(ErrResult);
	}

	DataTable->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("rowCount"), DataTable->GetRowMap().Num());
	Result->SetStringField(TEXT("message"), TEXT("DataTable reimported successfully from JSON"));

	EmitPriorDataTableRollback(Result, AssetPath, PriorJson, PriorRowCount);

	return MCPResult(Result);
}

// #437: single-row mutation. Append a new row or overwrite the existing one
// without round-tripping the whole table through JSON.
// Params: assetPath, rowName, row (JSON object with row-struct fields).
TSharedPtr<FJsonValue> FAssetHandlers::SetDataTableRow(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = LoadDataTableParam(Params, AssetPath, DataTable)) return Err;
	FString RowName;
	if (auto Err = RequireString(Params, TEXT("rowName"), RowName)) return Err;

	const TSharedPtr<FJsonObject>* RowObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("row"), RowObj))
	{
		// Also accept "fields" or "data" aliases.
		if (!Params->TryGetObjectField(TEXT("fields"), RowObj))
		{
			Params->TryGetObjectField(TEXT("data"), RowObj);
		}
	}
	if (!RowObj || !RowObj->IsValid())
	{
		return MCPError(TEXT("Missing 'row' (or 'fields'/'data') JSON object with the row struct fields"));
	}

	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct)
	{
		return MCPError(TEXT("DataTable has no row struct"));
	}

	const FName RowKey(*RowName);
	uint8* const* ExistingRow = DataTable->GetRowMap().Find(RowKey);
	const bool bExisted = ExistingRow != nullptr && *ExistingRow != nullptr;

	// Resolve every named field before a single byte is written. An unknown
	// field name used to be discovered half way through the loop, after
	// earlier fields had already been applied.
	TArray<TPair<FProperty*, TSharedPtr<FJsonValue>>> Writes;
	Writes.Reserve((*RowObj)->Values.Num());
	for (const auto& Pair : (*RowObj)->Values)
	{
		// FJsonObject::Values is not keyed by FString on UE 5.8, so the key has
		// to be materialised before it can be compared with a property name.
		// FString(*Pair.Key) is the idiom the rest of this module already uses.
		const FString FieldName(*Pair.Key);
		FProperty* FieldProp = nullptr;
		for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
		{
			if (It->GetName() == FieldName || It->GetAuthoredName() == FieldName)
			{
				FieldProp = *It;
				break;
			}
		}
		if (!FieldProp)
		{
			return MCPError(FString::Printf(
				TEXT("row struct '%s' has no field '%s'"), *RowStruct->GetName(), *Pair.Key));
		}
		Writes.Emplace(FieldProp, Pair.Value);
	}

	const int32 StructSize = RowStruct->GetStructureSize();
	const int32 MinAlign = RowStruct->GetMinAlignment();

	// Snapshot the prior row: it is both the rollback record handed back to
	// the caller and the copy this handler restores if a field write fails
	// part way through.
	//
	// The rollback record carries the prior values of exactly the fields
	// about to change, as a row payload for set_datatable_row, which is a
	// method that exists and now merges in place. It used to name
	// set_datatable_row_raw, which is registered nowhere, so a flow running
	// with rollback_on_failure could not restore a row at all; and it carried
	// whole-struct export text produced with the value as its own defaults,
	// which exports every field as unchanged and yields "()".
	TSharedPtr<FJsonObject> PrevFields = MakeShared<FJsonObject>();
	uint8* Backup = nullptr;
	if (bExisted)
	{
		Backup = (uint8*)FMemory::Malloc(StructSize, MinAlign);
		RowStruct->InitializeStruct(Backup);
		RowStruct->CopyScriptStruct(Backup, *ExistingRow);
		for (const TPair<FProperty*, TSharedPtr<FJsonValue>>& Write : Writes)
		{
			const void* FieldAddr = Write.Key->ContainerPtrToValuePtr<void>(Backup);
			PrevFields->SetField(
				Write.Key->GetAuthoredName(),
				FMCPJsonSerializer::SerializeValue(FieldAddr, Write.Key));
		}
	}

	// #929: an existing row is edited in the memory the table already owns.
	// Only the named fields are touched, so every other field keeps the exact
	// bytes it had, whether or not its UPROPERTY declares a default.
	//
	// The previous shape reallocated the row (RemoveRow followed by AddRow)
	// and relied on a CopyScriptStruct seed to carry the untouched fields
	// across. That seed is one line away from being lost, it moved the row to
	// the end of the row map on every edit, and UDataTable::RemoveRow closes
	// an FScopedDataTableChange whose destructor calls
	// HandleDataTableChanged(NAME_None), which runs
	// FTableRowBase::OnDataTableChanged against *every* row in the table
	// rather than the one being edited. A single-cell write should not reach
	// the other rows at all.
	uint8* RowData = bExisted ? *ExistingRow : (uint8*)FMemory::Malloc(StructSize, MinAlign);
	if (!bExisted)
	{
		RowStruct->InitializeStruct(RowData);
	}

	FString SetErr;
	bool bOk = true;
	for (const TPair<FProperty*, TSharedPtr<FJsonValue>>& Write : Writes)
	{
		void* FieldAddr = Write.Key->ContainerPtrToValuePtr<void>(RowData);
		FString E;
		if (!MCPJsonProperty::SetJsonOnProperty(Write.Key, FieldAddr, Write.Value, E))
		{
			SetErr = FString::Printf(TEXT("%s: %s"), *Write.Key->GetAuthoredName(), *E);
			bOk = false;
			break;
		}
	}
	if (!bOk)
	{
		if (bExisted)
		{
			// Put the row back exactly as it was found.
			RowStruct->CopyScriptStruct(RowData, Backup);
			RowStruct->DestroyStruct(Backup);
			FMemory::Free(Backup);
		}
		else
		{
			RowStruct->DestroyStruct(RowData);
			FMemory::Free(RowData);
		}
		return MCPError(SetErr);
	}

	if (!bExisted)
	{
		// AddRow copies the buffer and owns the copy from here on.
		DataTable->AddRow(RowKey, *reinterpret_cast<FTableRowBase*>(RowData));
		RowStruct->DestroyStruct(RowData);
		FMemory::Free(RowData);
		RowData = nullptr;
	}

	// #935: read the row back out of the table and check that every field the
	// caller named holds what was asked for, before anything is saved. This
	// reads the table's own memory, not the buffer that was written, so a
	// value lost in the copy into the table is caught here too.
	uint8* const* StoredRow = DataTable->GetRowMap().Find(RowKey);
	if (!StoredRow || !*StoredRow)
	{
		if (bExisted)
		{
			RowStruct->DestroyStruct(Backup);
			FMemory::Free(Backup);
		}
		return MCPError(FString::Printf(
			TEXT("Row '%s' is missing from '%s' after the write. Nothing was saved."),
			*RowName, *AssetPath));
	}
	for (const TPair<FProperty*, TSharedPtr<FJsonValue>>& Write : Writes)
	{
		const void* FieldAddr = Write.Key->ContainerPtrToValuePtr<void>(*StoredRow);
		FString Detail;
		if (MCPJsonProperty::VerifyJsonOnProperty(Write.Key, FieldAddr, Write.Value, Detail))
		{
			continue;
		}

		// Undo the whole call. A verified-bad write is not a partial success.
		if (bExisted)
		{
			RowStruct->CopyScriptStruct(*StoredRow, Backup);
			RowStruct->DestroyStruct(Backup);
			FMemory::Free(Backup);
			DataTable->HandleDataTableChanged(RowKey);
		}
		else
		{
			DataTable->RemoveRow(RowKey);
		}
		return MCPError(FString::Printf(
			TEXT("Field '%s' on row '%s' did not store the requested value: %s. The row was left unchanged."),
			*Write.Key->GetAuthoredName(), *RowName, *Detail));
	}

	if (bExisted)
	{
		RowStruct->DestroyStruct(Backup);
		FMemory::Free(Backup);
		Backup = nullptr;
		// The table's own row memory was edited, so name the row that changed
		// and only its hook runs.
		DataTable->HandleDataTableChanged(RowKey);
	}

	DataTable->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(DataTable, /*bOnlyIfIsDirty*/ true);

	auto Result = MCPSuccess();
	if (bExisted) MCPSetUpdated(Result); else MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("rowName"), RowName);
	Result->SetNumberField(TEXT("rowCount"), DataTable->GetRowMap().Num());

	// Rollback: restore the prior row (if any) or remove on create.
	if (bExisted)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("rowName"), RowName);
		Payload->SetObjectField(TEXT("row"), PrevFields);
		MCPSetRollback(Result, TEXT("set_datatable_row"), Payload);
	}
	else
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("rowName"), RowName);
		MCPSetRollback(Result, TEXT("remove_datatable_row"), Payload);
	}

	return MCPResult(Result);
}

// #437: remove a single row from a DataTable.
// Params: assetPath, rowName.
TSharedPtr<FJsonValue> FAssetHandlers::RemoveDataTableRow(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = LoadDataTableParam(Params, AssetPath, DataTable)) return Err;
	FString RowName;
	if (auto Err = RequireString(Params, TEXT("rowName"), RowName)) return Err;

	const FName RowKey(*RowName);
	if (!DataTable->GetRowMap().Contains(RowKey))
	{
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("rowName"), RowName);
		return MCPResult(Noop);
	}

	// Snapshot every field before the row goes, in the shape set_datatable_row
	// reads back as its `row` object. That is the same serializer that call
	// uses to build its own inverse payload, so the round trip is the one the
	// upsert path already relies on.
	TSharedPtr<FJsonObject> PrevFields = MakeShared<FJsonObject>();
	if (const UScriptStruct* RowStruct = DataTable->GetRowStruct())
	{
		if (uint8* const* RowPtr = DataTable->GetRowMap().Find(RowKey))
		{
			if (*RowPtr)
			{
				for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
				{
					FProperty* Prop = *It;
					PrevFields->SetField(
						Prop->GetAuthoredName(),
						FMCPJsonSerializer::SerializeValue(Prop->ContainerPtrToValuePtr<void>(*RowPtr), Prop));
				}
			}
		}
	}

	DataTable->RemoveRow(RowKey);
	DataTable->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(DataTable, /*bOnlyIfIsDirty*/ true);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("rowName"), RowName);
	Result->SetNumberField(TEXT("rowCount"), DataTable->GetRowMap().Num());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("rowName"), RowName);
	Payload->SetObjectField(TEXT("row"), PrevFields);
	MCPSetRollback(Result, TEXT("set_datatable_row"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The inverse recreates the row and writes back each field's value. Two things do not come back. The row is appended ")
		TEXT("to the end of the row map rather than returned to its original position, because the DataTable API has no way to ")
		TEXT("insert at an index. And an FText field comes back as its source string under a fresh localization namespace and key: ")
		TEXT("that identity is per instance and the JSON form the value travels in does not carry it."));
	return MCPResult(Result);
}

// #535: read a single row's fields without dumping the whole table.
// Params: assetPath, rowName.
TSharedPtr<FJsonValue> FAssetHandlers::GetDataTableRow(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = LoadDataTableParam(Params, AssetPath, DataTable)) return Err;
	FString RowName;
	if (auto Err = RequireString(Params, TEXT("rowName"), RowName)) return Err;

	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct) return MCPError(TEXT("DataTable has no row struct"));

	const FName RowKey(*RowName);
	uint8* const* RowPtr = DataTable->GetRowMap().Find(RowKey);
	if (!RowPtr || !*RowPtr) return MCPError(FString::Printf(TEXT("Row not found: %s"), *RowName));

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
	{
		FProperty* Prop = *It;
		FString ValueStr;
		const void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(*RowPtr);
		Prop->ExportText_Direct(ValueStr, ValueAddr, ValueAddr, nullptr, PPF_None);
		Fields->SetStringField(Prop->GetAuthoredName(), ValueStr);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("rowName"), RowName);
	Result->SetStringField(TEXT("rowStruct"), RowStruct->GetName());
	Result->SetObjectField(TEXT("fields"), Fields);
	return MCPResult(Result);
}

// #535: write a single field on a single row. Thin wrapper over the row-merge
// upsert (SetDataTableRow), which edits the existing row in place so only the
// named cell changes. Params: assetPath, rowName, fieldName, value.
TSharedPtr<FJsonValue> FAssetHandlers::SetDataTableCell(const TSharedPtr<FJsonObject>& Params)
{
	FString FieldName;
	if (auto Err = RequireString(Params, TEXT("fieldName"), FieldName)) return Err;
	const TSharedPtr<FJsonValue>* ValueField = Params->Values.Find(TEXT("value"));
	if (!ValueField || !(*ValueField).IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	// Build a one-field row object and delegate to the row upsert, which
	// requires the row to exist for a cell edit (no accidental row creation).
	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = LoadDataTableParam(Params, AssetPath, DataTable)) return Err;
	FString RowName;
	if (auto Err = RequireString(Params, TEXT("rowName"), RowName)) return Err;
	if (!DataTable->GetRowMap().Contains(FName(*RowName)))
	{
		return MCPError(FString::Printf(TEXT("Row not found: %s (use set_datatable_row to create it)"), *RowName));
	}

	TSharedPtr<FJsonObject> RowObj = MakeShared<FJsonObject>();
	RowObj->SetField(FieldName, *ValueField);
	TSharedPtr<FJsonObject> Delegated = MakeShared<FJsonObject>();
	Delegated->SetStringField(TEXT("assetPath"), AssetPath);
	Delegated->SetStringField(TEXT("rowName"), RowName);
	Delegated->SetObjectField(TEXT("row"), RowObj);
	return SetDataTableRow(Delegated);
}

// #535: rename a row key, preserving its field values. Params: assetPath,
// oldName, newName.
TSharedPtr<FJsonValue> FAssetHandlers::RenameDataTableRow(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = LoadDataTableParam(Params, AssetPath, DataTable)) return Err;
	FString OldName;
	if (auto Err = RequireStringAlt(Params, TEXT("oldName"), TEXT("rowName"), OldName)) return Err;
	FString NewName;
	if (auto Err = RequireString(Params, TEXT("newName"), NewName)) return Err;

	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct) return MCPError(TEXT("DataTable has no row struct"));

	const FName OldKey(*OldName);
	const FName NewKey(*NewName);
	if (OldKey == NewKey)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		return MCPResult(Noop);
	}
	uint8* const* OldPtr = DataTable->GetRowMap().Find(OldKey);
	if (!OldPtr || !*OldPtr) return MCPError(FString::Printf(TEXT("Row not found: %s"), *OldName));
	if (DataTable->GetRowMap().Contains(NewKey))
	{
		return MCPError(FString::Printf(TEXT("Target row already exists: %s"), *NewName));
	}

	// Copy the existing row struct into a fresh buffer under the new key.
	const int32 StructSize = RowStruct->GetStructureSize();
	const int32 MinAlign = RowStruct->GetMinAlignment();
	uint8* NewRow = (uint8*)FMemory::Malloc(StructSize, MinAlign);
	RowStruct->InitializeStruct(NewRow);
	RowStruct->CopyScriptStruct(NewRow, *OldPtr);

	DataTable->AddRow(NewKey, *reinterpret_cast<FTableRowBase*>(NewRow));
	DataTable->RemoveRow(OldKey);
	RowStruct->DestroyStruct(NewRow);
	FMemory::Free(NewRow);

	DataTable->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(DataTable, /*bOnlyIfIsDirty*/ true);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("oldName"), OldName);
	Result->SetStringField(TEXT("newName"), NewName);

	// Self-inverse: rename back. The field values travel with the row.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("oldName"), NewName);
	Payload->SetStringField(TEXT("newName"), OldName);
	MCPSetRollback(Result, TEXT("rename_datatable_row"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("A rename is an add followed by a remove, so this call already moved the row to the end of the row map. ")
		TEXT("The inverse restores the name and every field value; it cannot restore the original position."));
	return MCPResult(Result);
}

// #535: bulk-upsert rows from a JSON object ({rowName: {field: value, ...}})
// without touching unrelated rows (unlike reimport_datatable, which replaces
// the whole table). Params: assetPath, rows (object) or jsonString.
TSharedPtr<FJsonValue> FAssetHandlers::FillDataTableFromJson(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = LoadDataTableParam(Params, AssetPath, DataTable)) return Err;

	const TSharedPtr<FJsonObject>* RowsObj = nullptr;
	TSharedPtr<FJsonObject> ParsedRows;
	if (!Params->TryGetObjectField(TEXT("rows"), RowsObj))
	{
		// Accept a jsonString carrying the {rowName: {fields}} object.
		FString JsonStr;
		if (Params->TryGetStringField(TEXT("jsonString"), JsonStr) && !JsonStr.IsEmpty())
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
			if (FJsonSerializer::Deserialize(Reader, ParsedRows) && ParsedRows.IsValid())
			{
				RowsObj = &ParsedRows;
			}
		}
	}
	if (!RowsObj || !(*RowsObj).IsValid())
	{
		return MCPError(TEXT("Missing 'rows' object (or 'jsonString') mapping rowName -> {field: value}"));
	}

	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct) return MCPError(TEXT("DataTable has no row struct"));

	int32 Upserted = 0;
	for (const auto& RowPair : (*RowsObj)->Values)
	{
		const TSharedPtr<FJsonObject>* FieldsObj = nullptr;
		if (!RowPair.Value->TryGetObject(FieldsObj) || !FieldsObj || !(*FieldsObj).IsValid())
		{
			return MCPError(FString::Printf(TEXT("Row '%s' is not an object of fields"), *RowPair.Key));
		}
		TSharedPtr<FJsonObject> Delegated = MakeShared<FJsonObject>();
		Delegated->SetStringField(TEXT("assetPath"), AssetPath);
		Delegated->SetStringField(TEXT("rowName"), RowPair.Key);
		Delegated->SetObjectField(TEXT("row"), *FieldsObj);
		TSharedPtr<FJsonValue> RowResult = SetDataTableRow(Delegated);
		// SetDataTableRow returns an MCP error object on failure; surface it.
		const TSharedPtr<FJsonObject>* ResObj = nullptr;
		if (RowResult.IsValid() && RowResult->TryGetObject(ResObj) && ResObj)
		{
			bool bSuccess = false;
			(*ResObj)->TryGetBoolField(TEXT("success"), bSuccess);
			if (!bSuccess) return RowResult;
		}
		++Upserted;
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("rowsUpserted"), Upserted);
	Result->SetNumberField(TEXT("rowCount"), DataTable->GetRowMap().Num());
	return MCPResult(Result);
}

// ============================================================================
// StringTable handlers
// ============================================================================

namespace
{
	TSharedPtr<FJsonValue> LoadStringTableAsset(const TSharedPtr<FJsonObject>& Params, FString& OutAssetPath, UStringTable*& OutStringTable)
	{
		if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), OutAssetPath)) return Err;

		TSharedPtr<FJsonValue> LoadError;
		UObject* Asset = MCPRequireAssetObject(OutAssetPath, LoadError);
		if (!Asset) return LoadError;

		OutStringTable = Cast<UStringTable>(Asset);
		if (!OutStringTable)
		{
			return MCPAssetWrongTypeError(OutAssetPath, Asset, TEXT("StringTable"));
		}
		return nullptr;
	}

	int32 AppendStringTableEntries(
		const UStringTable* StringTable,
		const FString& KeyFilter,
		TArray<TSharedPtr<FJsonValue>>& OutEntries,
		TArray<TSharedPtr<FJsonValue>>& OutKeys,
		const bool bIncludeEntries = true)
	{
		if (!StringTable)
		{
			return 0;
		}

		const FString FilterLower = KeyFilter.ToLower();
		int32 TotalEntryCount = 0;
		StringTable->GetStringTable()->EnumerateKeysAndSourceStrings(
			[&](const FTextKey& Key, const FString& SourceString)
			{
				++TotalEntryCount;
				const FString KeyString = Key.ToString();
				if (!FilterLower.IsEmpty() && !KeyString.ToLower().Contains(FilterLower))
				{
					return true;
				}

				OutKeys.Add(MakeShared<FJsonValueString>(KeyString));

				if (bIncludeEntries)
				{
					TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("key"), KeyString);
					Entry->SetStringField(TEXT("sourceString"), SourceString);
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				}
				return true;
			});

		return TotalEntryCount;
	}

	void SetStringTableInfoFields(TSharedPtr<FJsonObject> Result, UStringTable* StringTable)
	{
		if (!Result.IsValid() || !StringTable)
		{
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Entries;
		TArray<TSharedPtr<FJsonValue>> Keys;
		const int32 EntryCount = AppendStringTableEntries(StringTable, TEXT(""), Entries, Keys, false);

		Result->SetStringField(TEXT("assetPath"), StringTable->GetPathName());
		Result->SetStringField(TEXT("tableId"), StringTable->GetStringTableId().ToString());
		Result->SetStringField(TEXT("namespace"), StringTable->GetStringTable()->GetNamespace());
		Result->SetNumberField(TEXT("entryCount"), EntryCount);
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::CreateStringTable(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/StringTables"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	const FString TableNamespace = OptionalString(Params, TEXT("namespace"));

	auto Created = MCPCreateAssetIdempotentNewObject<UStringTable>(Name, PackagePath, OnConflict, TEXT("StringTable"));
	if (Created.EarlyReturn)
	{
		if (TSharedPtr<FJsonObject> ExistingObj = Created.EarlyReturn->AsObject())
		{
			bool bExisted = false;
			if (ExistingObj->TryGetBoolField(TEXT("existed"), bExisted) && bExisted)
			{
				FString ExistingAssetPath;
				ExistingObj->TryGetStringField(TEXT("path"), ExistingAssetPath);
				if (UStringTable* Existing = LoadAssetByPath<UStringTable>(ExistingAssetPath))
				{
					SetStringTableInfoFields(ExistingObj, Existing);
				}
			}
		}
		return Created.EarlyReturn;
	}

	UStringTable* StringTable = Created.Asset;
	if (!StringTable)
	{
		return MCPError(TEXT("Failed to create StringTable"));
	}

	if (!TableNamespace.IsEmpty())
	{
		StringTable->Modify(true);
		StringTable->GetMutableStringTable()->SetNamespace(FTextKey(TableNamespace));
	}
	SaveAssetPackage(StringTable);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	SetStringTableInfoFields(Result, StringTable);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), StringTable->GetPathName());
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ReadStringTable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UStringTable* StringTable = nullptr;
	if (auto Err = LoadStringTableAsset(Params, AssetPath, StringTable)) return Err;

	const FString KeyFilter = OptionalString(Params, TEXT("keyFilter"));
	TArray<TSharedPtr<FJsonValue>> Entries;
	TArray<TSharedPtr<FJsonValue>> Keys;
	const int32 TotalEntryCount = AppendStringTableEntries(StringTable, KeyFilter, Entries, Keys);

	auto Result = MCPSuccess();
	SetStringTableInfoFields(Result, StringTable);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetArrayField(TEXT("entries"), Entries);
	Result->SetArrayField(TEXT("keys"), Keys);
	Result->SetNumberField(TEXT("totalEntryCount"), TotalEntryCount);
	Result->SetNumberField(TEXT("filteredCount"), Entries.Num());
	if (!KeyFilter.IsEmpty())
	{
		Result->SetStringField(TEXT("keyFilter"), KeyFilter);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ListStringTableKeys(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UStringTable* StringTable = nullptr;
	if (auto Err = LoadStringTableAsset(Params, AssetPath, StringTable)) return Err;

	const FString KeyFilter = OptionalString(Params, TEXT("keyFilter"));
	TArray<TSharedPtr<FJsonValue>> Entries;
	TArray<TSharedPtr<FJsonValue>> Keys;
	const int32 TotalEntryCount = AppendStringTableEntries(StringTable, KeyFilter, Entries, Keys, false);

	auto Result = MCPSuccess();
	SetStringTableInfoFields(Result, StringTable);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetArrayField(TEXT("keys"), Keys);
	Result->SetNumberField(TEXT("totalEntryCount"), TotalEntryCount);
	Result->SetNumberField(TEXT("filteredCount"), Keys.Num());
	if (!KeyFilter.IsEmpty())
	{
		Result->SetStringField(TEXT("keyFilter"), KeyFilter);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::GetStringTableEntry(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UStringTable* StringTable = nullptr;
	if (auto Err = LoadStringTableAsset(Params, AssetPath, StringTable)) return Err;

	FString Key;
	if (auto Err = RequireString(Params, TEXT("key"), Key)) return Err;

	FString SourceString;
	if (!StringTable->GetStringTable()->GetSourceString(FTextKey(Key), SourceString))
	{
		return MCPError(FString::Printf(TEXT("StringTable entry not found: %s"), *Key));
	}

	auto Result = MCPSuccess();
	SetStringTableInfoFields(Result, StringTable);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("key"), Key);
	Result->SetStringField(TEXT("sourceString"), SourceString);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::SetStringTableEntry(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UStringTable* StringTable = nullptr;
	if (auto Err = LoadStringTableAsset(Params, AssetPath, StringTable)) return Err;

	FString Key;
	if (auto Err = RequireString(Params, TEXT("key"), Key)) return Err;

	FString SourceString;
	bool bHasSourceString = Params->TryGetStringField(TEXT("sourceString"), SourceString);
	if (!bHasSourceString)
	{
		bHasSourceString = Params->TryGetStringField(TEXT("value"), SourceString);
	}
	if (!bHasSourceString)
	{
		return MCPError(TEXT("Missing 'sourceString' parameter"));
	}

	const FTextKey EntryKey(Key);
	FString PreviousSourceString;
	const bool bExisted = StringTable->GetStringTable()->GetSourceString(EntryKey, PreviousSourceString);

	StringTable->Modify(true);
	// The 3-arg SetSourceString (with a trailing metadata/namespace arg) is UE 5.8+.
	// 5.7 (and non-editor) take the 2-arg form.
#if WITH_EDITORONLY_DATA && (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))
	StringTable->GetMutableStringTable()->SetSourceString(EntryKey, SourceString, FString());
#else
	StringTable->GetMutableStringTable()->SetSourceString(EntryKey, SourceString);
#endif
	SaveAssetPackage(StringTable);

	auto Result = MCPSuccess();
	if (bExisted) MCPSetUpdated(Result); else MCPSetCreated(Result);
	SetStringTableInfoFields(Result, StringTable);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("key"), Key);
	Result->SetStringField(TEXT("sourceString"), SourceString);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("key"), Key);
	if (bExisted)
	{
		Payload->SetStringField(TEXT("sourceString"), PreviousSourceString);
		MCPSetRollback(Result, TEXT("set_stringtable_entry"), Payload);
	}
	else
	{
		MCPSetRollback(Result, TEXT("remove_stringtable_entry"), Payload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::RemoveStringTableEntry(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UStringTable* StringTable = nullptr;
	if (auto Err = LoadStringTableAsset(Params, AssetPath, StringTable)) return Err;

	FString Key;
	if (auto Err = RequireString(Params, TEXT("key"), Key)) return Err;

	const FTextKey EntryKey(Key);
	FString PreviousSourceString;
	if (!StringTable->GetStringTable()->GetSourceString(EntryKey, PreviousSourceString))
	{
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("key"), Key);
		return MCPResult(Noop);
	}

	StringTable->Modify(true);
	StringTable->GetMutableStringTable()->RemoveSourceString(EntryKey);
	SaveAssetPackage(StringTable);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	SetStringTableInfoFields(Result, StringTable);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("key"), Key);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("key"), Key);
	Payload->SetStringField(TEXT("sourceString"), PreviousSourceString);
	MCPSetRollback(Result, TEXT("set_stringtable_entry"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ImportStringTable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UStringTable* StringTable = nullptr;
	if (auto Err = LoadStringTableAsset(Params, AssetPath, StringTable)) return Err;

	FString FilePath;
	if (!Params->TryGetStringField(TEXT("filePath"), FilePath) || FilePath.IsEmpty())
	{
		if (!Params->TryGetStringField(TEXT("filename"), FilePath) || FilePath.IsEmpty())
		{
			Params->TryGetStringField(TEXT("csvPath"), FilePath);
		}
	}
	if (FilePath.IsEmpty())
	{
		return MCPError(TEXT("Missing 'filePath' parameter"));
	}
	if (!FPaths::FileExists(FilePath))
	{
		return MCPError(FString::Printf(TEXT("StringTable import file not found: %s"), *FilePath));
	}

	TArray<TSharedPtr<FJsonValue>> BeforeEntries;
	TArray<TSharedPtr<FJsonValue>> BeforeKeys;
	const int32 BeforeCount = AppendStringTableEntries(StringTable, TEXT(""), BeforeEntries, BeforeKeys, false);

	// The source string of every key, kept only long enough to work out which
	// ones this import actually overwrote or dropped. Serialising the whole
	// pre-call table into the response would put a localization table of
	// thousands of entries on the wire on every call.
	TMap<FString, FString> BeforeStrings;
	StringTable->GetStringTable()->EnumerateKeysAndSourceStrings(
		[&BeforeStrings](const FTextKey& Key, const FString& SourceString) -> bool
		{
			BeforeStrings.Add(Key.ToString(), SourceString);
			return true;
		});

	StringTable->Modify(true);
#if UE_MCP_HAS_5_8_API
	const bool bImported = StringTable->GetMutableStringTable()->ImportStringsFromCSVFile(FilePath);
#else
	const bool bImported = StringTable->GetMutableStringTable()->ImportStrings(FilePath);
#endif
	if (!bImported)
	{
		return MCPError(FString::Printf(TEXT("Failed to import StringTable from: %s"), *FilePath));
	}
	SaveAssetPackage(StringTable);

	TArray<TSharedPtr<FJsonValue>> Entries;
	TArray<TSharedPtr<FJsonValue>> Keys;
	const int32 AfterCount = AppendStringTableEntries(StringTable, TEXT(""), Entries, Keys, false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	SetStringTableInfoFields(Result, StringTable);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("filePath"), FilePath);
	Result->SetNumberField(TEXT("entryCountBefore"), BeforeCount);
	Result->SetNumberField(TEXT("entryCountAfter"), AfterCount);
	Result->SetArrayField(TEXT("keys"), Keys);

	// Only the keys this import changed, matching what import_stringtable_csv
	// reports. An untouched key needs nothing replayed, so carrying it here
	// would be payload for payload's sake.
	TSharedPtr<FJsonObject> PreviousEntries = MakeShared<FJsonObject>();
	StringTable->GetStringTable()->EnumerateKeysAndSourceStrings(
		[&BeforeStrings, &PreviousEntries](const FTextKey& Key, const FString& SourceString) -> bool
		{
			const FString KeyString = Key.ToString();
			if (const FString* Previous = BeforeStrings.Find(KeyString))
			{
				if (!Previous->Equals(SourceString, ESearchCase::CaseSensitive))
				{
					PreviousEntries->SetStringField(KeyString, *Previous);
				}
				BeforeStrings.Remove(KeyString);
			}
			return true;
		});
	// Whatever is left was in the table before and is not in it now.
	for (const TPair<FString, FString>& Dropped : BeforeStrings)
	{
		PreviousEntries->SetStringField(Dropped.Key, Dropped.Value);
	}
	Result->SetObjectField(TEXT("previousEntries"), PreviousEntries);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The importer merges the file into the table, overwriting the source string of every key it carries. ")
		TEXT("The bridge writes String Table entries one key at a time (set_stringtable_entry, remove_stringtable_entry), ")
		TEXT("so restoring N entries takes N calls and a rollback record is a single call. previousEntries carries the ")
		TEXT("pre-call sourceString of exactly the keys this import overwrote or dropped, which is what a caller replays by hand."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// import_stringtable_csv (#978)
//
// String Tables could be created and inspected but not refreshed from a CSV
// source with any confidence, so a user kept the CSV as the canonical copy and
// ran a custom editor script instead. This is the same engine importer
// FStringTable exposes, wrapped so the caller gets the three things the script
// was written to provide: the CSV is proved to parse and to carry the expected
// keys BEFORE the asset is touched, the result names exactly which keys were
// added, updated and removed, and the save is reported rather than assumed.
// ---------------------------------------------------------------------------

namespace
{
	/** Every key and source string in a table, as a plain map. */
	void MCPSnapshotStringTable(const UStringTable* Table, TMap<FString, FString>& Out)
	{
		Out.Reset();
		if (!Table) return;
		Table->GetStringTable()->EnumerateKeysAndSourceStrings(
			[&Out](const FTextKey& Key, const FString& SourceString) -> bool
			{
				Out.Add(Key.ToString(), SourceString);
				return true;
			});
	}

	/** Run the engine's CSV importer over Table, on whichever engine this is. */
	bool MCPImportStringTableCsvFile(UStringTable* Table, const FString& FilePath)
	{
		if (!Table) return false;
#if UE_MCP_HAS_5_8_API
		return Table->GetMutableStringTable()->ImportStringsFromCSVFile(FilePath);
#else
		return Table->GetMutableStringTable()->ImportStrings(FilePath);
#endif
	}

	TArray<TSharedPtr<FJsonValue>> MCPSortedKeysToJson(const TArray<FString>& Keys)
	{
		TArray<FString> Sorted = Keys;
		Sorted.Sort();
		TArray<TSharedPtr<FJsonValue>> Out;
		Out.Reserve(Sorted.Num());
		for (const FString& Key : Sorted) Out.Add(MakeShared<FJsonValueString>(Key));
		return Out;
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::ImportStringTableCsv(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString AssetPath;
	UStringTable* StringTable = nullptr;
	if (auto Err = LoadStringTableAsset(Params, AssetPath, StringTable)) return Err;

	if (MCPIsProtectedAssetPath(AssetPath))
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is on a protected mount (/Engine/, /Script/, /Memory/, /Temp/), which the bridge never writes to."),
			*AssetPath));
	}

	FString CsvPath;
	if (auto Err = RequireStringAlt(Params, TEXT("csvPath"), TEXT("filePath"), CsvPath)) return Err;
	// A relative path is read against the project directory, not the editor's
	// working directory, which is not something a caller can predict.
	if (FPaths::IsRelative(CsvPath))
	{
		CsvPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), CsvPath);
	}
	if (!FPaths::FileExists(CsvPath))
	{
		return MCPError(FString::Printf(TEXT("CSV file not found: %s"), *CsvPath));
	}

	const bool bReplaceExisting = OptionalBool(Params, TEXT("replaceExisting"), false);
	const bool bRequireExactKeys = OptionalBool(Params, TEXT("requireExactKeys"), false);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);

	TArray<FString> ExpectedKeys;
	const TArray<TSharedPtr<FJsonValue>>* ExpectedField = nullptr;
	if (Params->TryGetArrayField(TEXT("expectedKeys"), ExpectedField) && ExpectedField)
	{
		ExpectedKeys = JsonArrayToStringList(ExpectedField);
	}

	// Parse and validate against a throwaway table first, so a malformed CSV or
	// a key set that does not match expectations never reaches the asset.
	UStringTable* Staged = NewObject<UStringTable>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!Staged)
	{
		return MCPError(TEXT("Could not create the staging StringTable used to validate the CSV."));
	}
	const FGCRootScope KeepStagedAlive(Staged);
	if (!MCPImportStringTableCsvFile(Staged, CsvPath))
	{
		return MCPError(FString::Printf(
			TEXT("The engine's String Table importer rejected '%s'. Nothing was changed. ")
			TEXT("The file must be a CSV whose first column is the entry key; the editor log carries the parse error."),
			*CsvPath));
	}

	TMap<FString, FString> Incoming;
	MCPSnapshotStringTable(Staged, Incoming);
	if (Incoming.Num() == 0)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' parsed but carried no entries, so nothing was changed."), *CsvPath));
	}

	if (ExpectedKeys.Num() > 0)
	{
		TArray<FString> MissingKeys;
		for (const FString& Expected : ExpectedKeys)
		{
			if (!Incoming.Contains(Expected)) MissingKeys.Add(Expected);
		}
		TArray<FString> UnexpectedKeys;
		if (bRequireExactKeys)
		{
			for (const TPair<FString, FString>& Entry : Incoming)
			{
				if (!ExpectedKeys.Contains(Entry.Key)) UnexpectedKeys.Add(Entry.Key);
			}
		}
		if (MissingKeys.Num() > 0 || UnexpectedKeys.Num() > 0)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetBoolField(TEXT("success"), false);
			Obj->SetStringField(TEXT("error"), FString::Printf(
				TEXT("'%s' does not match the expected key set, so the String Table was not touched: %d expected key(s) missing%s."),
				*CsvPath, MissingKeys.Num(),
				bRequireExactKeys
					? *FString::Printf(TEXT(", %d unexpected"), UnexpectedKeys.Num())
					: TEXT("")));
			Obj->SetStringField(TEXT("reason"), TEXT("key_set_mismatch"));
			Obj->SetStringField(TEXT("assetPath"), AssetPath);
			Obj->SetStringField(TEXT("csvPath"), CsvPath);
			Obj->SetNumberField(TEXT("csvKeyCount"), Incoming.Num());
			Obj->SetArrayField(TEXT("missingKeys"), MCPSortedKeysToJson(MissingKeys));
			Obj->SetArrayField(TEXT("unexpectedKeys"), MCPSortedKeysToJson(UnexpectedKeys));
			return MakeShared<FJsonValueObject>(Obj);
		}
	}

	TMap<FString, FString> Before;
	MCPSnapshotStringTable(StringTable, Before);

	// The merge runs first and the pruning second, so a parse that somehow
	// fails on the second pass cannot leave the table emptied.
	StringTable->Modify(true);
	if (!MCPImportStringTableCsvFile(StringTable, CsvPath))
	{
		return MCPError(FString::Printf(
			TEXT("'%s' parsed into the staging table but not into '%s'. The table is unchanged apart from any partial merge the importer applied; re-read it before retrying."),
			*CsvPath, *AssetPath));
	}

	TArray<FString> RemovedKeys;
	if (bReplaceExisting)
	{
		for (const TPair<FString, FString>& Entry : Before)
		{
			if (!Incoming.Contains(Entry.Key))
			{
				StringTable->GetMutableStringTable()->RemoveSourceString(FTextKey(Entry.Key));
				RemovedKeys.Add(Entry.Key);
			}
		}
	}

	TMap<FString, FString> After;
	MCPSnapshotStringTable(StringTable, After);

	TArray<FString> AddedKeys;
	TArray<FString> UpdatedKeys;
	for (const TPair<FString, FString>& Entry : After)
	{
		if (const FString* Previous = Before.Find(Entry.Key))
		{
			if (!Previous->Equals(Entry.Value, ESearchCase::CaseSensitive))
			{
				UpdatedKeys.Add(Entry.Key);
			}
		}
		else
		{
			AddedKeys.Add(Entry.Key);
		}
	}

	UPackage* Package = StringTable->GetOutermost();
	bool bPersisted = false;
	FString PersistReason;
	if (bSave)
	{
		bPersisted = SaveAssetPackage(StringTable);
		if (!bPersisted)
		{
			PersistReason = FString::Printf(
				TEXT("The editor refused to write '%s'. The entries are in memory only."),
				Package ? *Package->GetName() : *AssetPath);
		}
	}
	else
	{
		PersistReason = TEXT("save=false was requested, so the imported entries are in memory only until the package is saved.");
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	SetStringTableInfoFields(Result, StringTable);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("csvPath"), CsvPath);
	Result->SetBoolField(TEXT("replaceExisting"), bReplaceExisting);
	Result->SetNumberField(TEXT("csvKeyCount"), Incoming.Num());
	Result->SetNumberField(TEXT("entryCountBefore"), Before.Num());
	Result->SetNumberField(TEXT("entryCountAfter"), After.Num());
	Result->SetArrayField(TEXT("addedKeys"), MCPSortedKeysToJson(AddedKeys));
	Result->SetArrayField(TEXT("updatedKeys"), MCPSortedKeysToJson(UpdatedKeys));
	Result->SetArrayField(TEXT("removedKeys"), MCPSortedKeysToJson(RemovedKeys));
	TArray<FString> AllKeys;
	After.GetKeys(AllKeys);
	Result->SetArrayField(TEXT("keys"), MCPSortedKeysToJson(AllKeys));
	if (ExpectedKeys.Num() > 0)
	{
		Result->SetNumberField(TEXT("expectedKeyCount"), ExpectedKeys.Num());
		Result->SetBoolField(TEXT("expectedKeysPresent"), true);
	}
	// The pre-call source string of every key this call overwrote or removed.
	// Without it, updatedKeys and removedKeys name the damage without carrying
	// what it would take to repair.
	TSharedPtr<FJsonObject> PreviousEntries = MakeShared<FJsonObject>();
	for (const FString& Key : UpdatedKeys)
	{
		if (const FString* Previous = Before.Find(Key)) PreviousEntries->SetStringField(Key, *Previous);
	}
	for (const FString& Key : RemovedKeys)
	{
		if (const FString* Previous = Before.Find(Key)) PreviousEntries->SetStringField(Key, *Previous);
	}
	Result->SetObjectField(TEXT("previousEntries"), PreviousEntries);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The bridge writes String Table entries one key at a time (set_stringtable_entry, remove_stringtable_entry), ")
		TEXT("so restoring the entries in updatedKeys and removedKeys and dropping the ones in addedKeys takes one call each, ")
		TEXT("and a rollback record is a single call. previousEntries carries the pre-call sourceString for every key in ")
		TEXT("updatedKeys and removedKeys, which is what a caller replays by hand."));
	Result->SetBoolField(TEXT("persisted"), bPersisted);
	Result->SetBoolField(TEXT("saved"), bPersisted);
	if (Package)
	{
		Result->SetStringField(TEXT("packageName"), Package->GetName());
		Result->SetBoolField(TEXT("packageDirty"), Package->IsDirty());
	}
	if (!bPersisted)
	{
		Result->SetStringField(TEXT("persistError"), PersistReason);
		if (bSave)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), PersistReason);
		}
	}
	return MCPResult(Result);
}

// --- Reimport ---------------------------------------------------------


// --- Reimport ---------------------------------------------------------

TSharedPtr<FJsonValue> FAssetHandlers::ReimportAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;

	// Optionally override the source file path
	FString NewSourcePath;
	if (Params->TryGetStringField(TEXT("filePath"), NewSourcePath) || Params->TryGetStringField(TEXT("filename"), NewSourcePath))
	{
		if (!FPaths::FileExists(NewSourcePath))
		{
			return MCPError(FString::Printf(TEXT("File not found: %s"), *NewSourcePath));
		}

		// Update the stored source file path on the asset import data
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
			// Generic: try finding AssetImportData property via reflection
			FObjectProperty* Prop = CastField<FObjectProperty>(Asset->GetClass()->FindPropertyByName(TEXT("AssetImportData")));
			if (Prop)
			{
				ImportData = Cast<UAssetImportData>(Prop->GetObjectPropertyValue_InContainer(Asset));
			}
		}

		if (ImportData)
		{
			ImportData->Update(NewSourcePath);
		}
	}

	// Use FReimportManager to reimport
	bool bSuccess = FReimportManager::Instance()->Reimport(Asset, /*bAskForNewFileIfMissing=*/false, /*bShowNotification=*/false);

	auto Result = MCPSuccess();
	if (bSuccess) MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
	Result->SetBoolField(TEXT("success"), bSuccess);
	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), TEXT("Reimport failed -- check that the asset has a valid source file"));
	}
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("A reimport rebuilds the asset from its source file in place, so the content it replaced is gone and the new ")
		TEXT("content does not carry it. Reimporting again re-reads the same file rather than restoring the previous build, ")
		TEXT("and deleting the asset would destroy it rather than undo the call. There is no inverse action."));

	return MCPResult(Result);
}

// --- Socket Handlers --------------------------------------------------


// ---------------------------------------------------------------------------
// export_asset - Export an asset to disk (e.g. Texture2D → PNG, StaticMesh → FBX)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::ExportAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString OutputPath;
	if (auto Err = RequireString(Params, TEXT("outputPath"), OutputPath)) return Err;

	// #930: same resolution as asset(read), which opened assets this action
	// reported as missing.
	UObject* Asset = MCPLoadAssetObject(AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	// Resolve a relative outputPath against the project directory, the way
	// export_texture already does. Left raw it resolves against the process
	// working directory, which is the engine's binaries folder: not somewhere a
	// caller can predict, and it would put the existence probe below and the
	// file the exporter actually writes in two different places.
	FString AbsOutputPath = OutputPath;
	if (FPaths::IsRelative(AbsOutputPath))
	{
		AbsOutputPath = FPaths::Combine(FPaths::ProjectDir(), AbsOutputPath);
	}

	// Whether the output file was already there decides whether this call
	// created one or overwrote one. bReplaceIdentical below is true, so
	// reporting "created" unconditionally would call every overwrite a create.
	const bool bOutputExisted = IFileManager::Get().FileExists(*AbsOutputPath);

	// Create parent directory if needed
	FString OutputDir = FPaths::GetPath(AbsOutputPath);
	if (!OutputDir.IsEmpty())
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*OutputDir);
	}

	// Use UE's AssetExportTask - same as unreal.AssetExportTask in Python
	UAssetExportTask* ExportTask = NewObject<UAssetExportTask>();
	ExportTask->Object = Asset;
	ExportTask->Filename = AbsOutputPath;
	ExportTask->bAutomated = true;
	ExportTask->bPrompt = false;
	ExportTask->bReplaceIdentical = true;

	bool bSuccess = UExporter::RunAssetExportTask(ExportTask);

	if (!bSuccess)
	{
		return MCPError(FString::Printf(TEXT("Export failed for '%s' to '%s'. The asset type may not have a registered exporter."), *AssetPath, *AbsOutputPath));
	}

	auto Result = MCPSuccess();
	if (bOutputExisted) MCPSetUpdated(Result); else MCPSetCreated(Result);
	Result->SetBoolField(TEXT("overwroteExistingFile"), bOutputExisted);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("outputPath"), AbsOutputPath);
	Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("An export writes an output artifact rather than changing project state. Nothing in the project moved, and ")
		TEXT("deleting a file that regenerates on demand is not an undo. There is no inverse action."));
	return MCPResult(Result);
}

// ─── #150 asset(get_referencers) ────────────────────────────────────
// Reverse dependency lookup per package. Feeds the common "what uses this
// texture / material?" question without dropping into Python.

// #697: export a Texture2D to a PNG on disk (UTextureExporterPNG, auto-found
// from the .png extension). Lets a look-dev workflow pull a texture out for
// inspection or external diffing.
TSharedPtr<FJsonValue> FAssetHandlers::ExportTexture(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString OutputPath;
	if (auto Err = RequireStringAlt(Params, TEXT("outputPath"), TEXT("filePath"), OutputPath)) return Err;

	UTexture2D* Texture = LoadAssetByPath<UTexture2D>(AssetPath);
	if (!Texture) return MCPAssetLoadError(AssetPath, TEXT("Texture2D"));

	FString AbsPath = OutputPath;
	if (FPaths::IsRelative(AbsPath)) AbsPath = FPaths::Combine(FPaths::ProjectDir(), AbsPath);
	if (!AbsPath.EndsWith(TEXT(".png"))) AbsPath += TEXT(".png");
	// Same reason as export_asset: bReplaceIdentical is true below, so this
	// distinguishes writing a new PNG from overwriting one that was there.
	const bool bOutputExisted = IFileManager::Get().FileExists(*AbsPath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsPath), /*Tree*/ true);

	UAssetExportTask* Task = NewObject<UAssetExportTask>();
	FGCRootScope TaskRoot(Task);
	Task->Object = Texture;
	Task->Filename = AbsPath;
	Task->bAutomated = true;
	Task->bPrompt = false;
	Task->bReplaceIdentical = true;
	const bool bOk = UExporter::RunAssetExportTask(Task);
	const int64 Size = IFileManager::Get().FileSize(*AbsPath);
	if (!bOk || Size < 0) return MCPError(FString::Printf(TEXT("Texture export failed for %s"), *AssetPath));

	auto Result = MCPSuccess();
	if (bOutputExisted) MCPSetUpdated(Result); else MCPSetCreated(Result);
	Result->SetBoolField(TEXT("overwroteExistingFile"), bOutputExisted);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("path"), AbsPath);
	Result->SetNumberField(TEXT("width"), Texture->GetSizeX());
	Result->SetNumberField(TEXT("height"), Texture->GetSizeY());
	Result->SetNumberField(TEXT("sizeBytes"), (double)Size);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("An export writes an output artifact rather than changing project state. The texture is untouched, and ")
		TEXT("deleting a PNG that regenerates on demand is not an undo. There is no inverse action."));
	return MCPResult(Result);
}

// #697: compare two textures by dimensions, pixel format, and source content
// identity (FTextureSource::GetIdString) so a look-dev workflow can tell
// whether an authored texture actually changed without pixel-diffing offline.
TSharedPtr<FJsonValue> FAssetHandlers::CompareTextures(const TSharedPtr<FJsonObject>& Params)
{
	FString PathA, PathB;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPathA"), TEXT("a"), PathA)) return Err;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPathB"), TEXT("b"), PathB)) return Err;

	UTexture2D* A = LoadAssetByPath<UTexture2D>(PathA);
	UTexture2D* B = LoadAssetByPath<UTexture2D>(PathB);
	if (!A) return MCPAssetLoadError(PathA, TEXT("Texture2D"));
	if (!B) return MCPAssetLoadError(PathB, TEXT("Texture2D"));

	const bool bSameDims = A->GetSizeX() == B->GetSizeX() && A->GetSizeY() == B->GetSizeY();
	const bool bSameFormat = A->GetPixelFormat() == B->GetPixelFormat();
#if WITH_EDITORONLY_DATA
	const FString IdA = A->Source.GetIdString();
	const FString IdB = B->Source.GetIdString();
	const bool bSameSource = (IdA == IdB);
#else
	const FString IdA, IdB; const bool bSameSource = false;
#endif
	const bool bIdentical = bSameDims && bSameFormat && bSameSource;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPathA"), PathA);
	Result->SetStringField(TEXT("assetPathB"), PathB);
	Result->SetBoolField(TEXT("identical"), bIdentical);
	Result->SetBoolField(TEXT("sameDimensions"), bSameDims);
	Result->SetBoolField(TEXT("sameFormat"), bSameFormat);
	Result->SetBoolField(TEXT("sameSourceContent"), bSameSource);
	Result->SetNumberField(TEXT("widthA"), A->GetSizeX());
	Result->SetNumberField(TEXT("heightA"), A->GetSizeY());
	Result->SetNumberField(TEXT("widthB"), B->GetSizeX());
	Result->SetNumberField(TEXT("heightB"), B->GetSizeY());
	return MCPResult(Result);
}
