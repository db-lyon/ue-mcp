// Split from AssetHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FAssetHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in AssetHandlers.cpp::RegisterHandlers.

#include "AssetHandlers.h"
#include "AssetHandlers_TextureSettings.h"
#include "../Material/MaterialHandlers.h"
#include "Materials/MaterialInstance.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "HandlerJsonProperty.h"
#include "JsonSerializer.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetImportTask.h"
#include "Factories/Factory.h"
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
#include "DataTableUtils.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/App.h"
#include "TextureCompiler.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
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
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"

namespace
{
	/** An FTextKey as an FString.
	 *
	 *  5.5 gave FTextKey a ToString(); 5.4 exposes only the character pointer
	 *  behind it. Same characters either way. */
	FString MCPTextKeyToString(const FTextKey& Key)
	{
#if UE_MCP_HAS_5_5_API
		return Key.ToString();
#else
		return FString(Key.GetChars());
#endif
	}

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

	bool SaveCurveTableChange(UCurveTable* Table, FString& OutReason)
	{
		if (!Table) return false;
		UCurveTable::InvalidateAllCachedCurves();
		Table->OnCurveTableChanged().Broadcast();
		Table->Modify(true);
		return SaveAssetPackageChecked(Table, OutReason);
	}

	/** Resolve Params.assetPath the way asset(read) resolves any asset and
	 *  require it to be a T. `path` is an alias the registry resolves to
	 *  assetPath before dispatch (#1057). */
	template <typename T>
	TSharedPtr<FJsonValue> MCPImportLoadTypedAsset(
		const TSharedPtr<FJsonObject>& Params, const TCHAR* TypeLabel, FString& OutAssetPath, T*& OutAsset)
	{
		if (auto Err = RequireString(Params, TEXT("assetPath"), OutAssetPath)) return Err;
		TSharedPtr<FJsonValue> LoadError;
		UObject* Asset = MCPRequireAssetObject(OutAssetPath, LoadError);
		if (!Asset) return LoadError;
		OutAsset = Cast<T>(Asset);
		if (!OutAsset) return MCPAssetWrongTypeError(OutAssetPath, Asset, TypeLabel);
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

	/** Write every field of Settings onto the first target that has a property
	 *  by that name. Keys are UPROPERTY names or dotted paths; a bool may drop
	 *  its "b" prefix (snapToClosestFrameBoundary). Any key that matches nothing,
	 *  or any value that does not convert, fails the whole call before import. */
	bool MCPApplyImportSettings(
		const TArray<UObject*>& Targets,
		const TSharedPtr<FJsonObject>& Settings,
		TArray<TSharedPtr<FJsonValue>>& OutApplied,
		FString& OutError)
	{
		if (!Settings.IsValid()) return true;
		for (const auto& Pair : Settings->Values)
		{
			// The key type is not FString on every supported engine.
			const FString Key(*Pair.Key);
			TArray<FString> Names;
			Names.Add(Key);
			if (!Key.Contains(TEXT(".")) && !Key.StartsWith(TEXT("b"), ESearchCase::CaseSensitive))
			{
				Names.Add(TEXT("b") + Key);
			}

			bool bApplied = false;
			for (UObject* Target : Targets)
			{
				if (!Target || bApplied) continue;
				for (const FString& Name : Names)
				{
					FProperty* Prop = nullptr;
					void* ValueAddr = nullptr;
					UObject* LeafOwner = nullptr;
					FString ResolveError;
					if (!MCPJsonProperty::ResolveDottedPath(Target, Name, Prop, ValueAddr, LeafOwner, ResolveError)) continue;
					// The prefixed spelling only stands in for a bool.
					if (Name != Key && !CastField<FBoolProperty>(Prop)) continue;

					FString SetError;
					if (!MCPJsonProperty::SetJsonOnProperty(Prop, ValueAddr, Pair.Value, SetError))
					{
						OutError = FString::Printf(TEXT("Setting '%s' on %s failed: %s"),
							*Key, *Target->GetClass()->GetName(), *SetError);
						return false;
					}
					TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("key"), Key);
					Entry->SetStringField(TEXT("property"), Name);
					Entry->SetStringField(TEXT("target"), Target->GetClass()->GetName());
					OutApplied.Add(MakeShared<FJsonValueObject>(Entry));
					bApplied = true;
					break;
				}
			}

			if (!bApplied)
			{
				TArray<FString> ClassNames;
				for (UObject* Target : Targets)
				{
					if (Target) ClassNames.Add(Target->GetClass()->GetName());
				}
				OutError = FString::Printf(
					TEXT("Unknown setting '%s': no property by that name on %s. Keys are UPROPERTY names or dotted paths (reflection(reflect_class) lists them). Nothing was imported."),
					*Key, *FString::Join(ClassNames, TEXT(" or ")));
				return false;
			}
		}
		return true;
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
	if (auto Err = RequireString(Params, TEXT("filePath"), FileName)) return Err;

	FString DestinationPath = OptionalString(Params, TEXT("packagePath"));
	if (DestinationPath.IsEmpty()) DestinationPath = TEXT("/Game/Meshes");

	// Every parameter is read before anything can fail (#1057).
	bool bImportMaterials = true;
	const bool bHasImportMaterials = TryGetBoolParam(Params, TEXT("importMaterials"), bImportMaterials);
	bool bImportTextures = true;
	const bool bHasImportTextures = TryGetBoolParam(Params, TEXT("importTextures"), bImportTextures);
	bool bCombineMeshes = false;
	const bool bHasCombineMeshes = TryGetBoolParam(Params, TEXT("combineMeshes"), bCombineMeshes);
	bool bGenerateLightmapUVs = true;
	const bool bHasGenerateLightmapUVs = TryGetBoolParam(Params, TEXT("generateLightmapUVs"), bGenerateLightmapUVs);
	const bool bHasUniformScale = HasParam(Params, TEXT("importUniformScale"));
	const double UniformScale = OptionalNumber(Params, TEXT("importUniformScale"), 1.0);
	const FString AssetName = OptionalString(Params, TEXT("name"));

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
		if (bHasImportMaterials)
		{
			ImportUI->bImportMaterials = bImportMaterials;
		}
		if (bHasImportTextures)
		{
			ImportUI->bImportTextures = bImportTextures;
		}
		if (bHasCombineMeshes)
		{
			ImportUI->StaticMeshImportData->bCombineMeshes = bCombineMeshes;
		}
		if (bHasGenerateLightmapUVs)
		{
			ImportUI->StaticMeshImportData->bGenerateLightmapUVs = bGenerateLightmapUVs;
		}
		// #687: metre-authored FBX unit conversion. Default 1.0 leaves cm FBX
		// untouched; pass 100 for metre FBX.
		if (bHasUniformScale && ImportUI->StaticMeshImportData)
		{
			ImportUI->StaticMeshImportData->ImportUniformScale = (float)UniformScale;
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
	if (auto Err = RequireString(Params, TEXT("filePath"), FileName)) return Err;
	FString DestinationPath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Meshes"));
	// The created-asset check below compares against DestinationPath + "/".
	DestinationPath.RemoveFromEnd(TEXT("/"));
	const FString AssetName = OptionalString(Params, TEXT("name"), FPaths::GetBaseFilename(FileName));
	const FString SkeletonPath = OptionalString(Params, TEXT("skeletonPath"));
	const bool bImportMaterials = OptionalBool(Params, TEXT("importMaterials"), true);
	const bool bImportTextures = OptionalBool(Params, TEXT("importTextures"), true);
	const bool bImportMorphTargets = OptionalBool(Params, TEXT("importMorphTargets"), true);
	const bool bCreatePhysicsAsset = OptionalBool(Params, TEXT("createPhysicsAsset"), false);
	const bool bReplaceExisting = OptionalBool(Params, TEXT("replaceExisting"), true);
	const bool bSave = OptionalBool(Params, TEXT("save"), false);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const double UniformScale = OptionalNumber(Params, TEXT("importUniformScale"), 1.0);
	FString Importer = OptionalString(Params, TEXT("importer"), TEXT("auto")).ToLower();
	if (Importer == TEXT("auto"))
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
		Importer = TEXT("interchange");
#else
		Importer = TEXT("legacy");
#endif
	}
	if (Importer != TEXT("interchange") && Importer != TEXT("legacy")) return MCPError(TEXT("importer must be auto, interchange or legacy"));
	if (!FMath::IsFinite(UniformScale) || UniformScale <= 0) return MCPError(TEXT("importUniformScale must be finite and greater than zero"));
	if (!FPaths::FileExists(FileName)) return MCPError(TEXT("File not found: ") + FileName);
	const FString Destination = DestinationPath / AssetName;
	if (AssetName.IsEmpty() || AssetName.Contains(TEXT("/")) || AssetName.Contains(TEXT(".")) || !FPackageName::IsValidLongPackageName(Destination)) return MCPError(TEXT("Invalid mesh destination: ") + Destination);
	if (MCPIsProtectedAssetPath(Destination)) return MCPError(TEXT("Cannot import to a protected mount"));
	if (UObject* Existing = MCPLoadAssetObject(Destination))
	{
		if (!bReplaceExisting) return MCPError(TEXT("Destination exists and replaceExisting is false: ") + Destination);
		if (!Cast<USkeletalMesh>(Existing)) return MCPAssetWrongTypeError(Destination, Existing, TEXT("SkeletalMesh"));
		if (auto Blocked = MCPAssetWriteBlockedError(Existing, Destination, TEXT("import skeletal mesh"))) return Blocked;
	}
	USkeleton* Skeleton = nullptr;
	if (!SkeletonPath.IsEmpty())
	{
		Skeleton = LoadAssetByPath<USkeleton>(SkeletonPath);
		if (!Skeleton) return MCPAssetLoadError(SkeletonPath, TEXT("Skeleton"));
	}

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	FGCRootScope TaskRoot(Task);
	Task->bAutomated = true;
	Task->bAsync = false;
	Task->bReplaceExisting = bReplaceExisting;
	Task->bReplaceExistingSettings = true;
	Task->bSave = false;
	Task->Filename = FileName;
	Task->DestinationPath = DestinationPath;
	Task->DestinationName = AssetName;
	TArray<TUniquePtr<FGCRootScope>> Roots;
	if (Importer == TEXT("interchange"))
	{
		UClass* PipelineClass = LoadClass<UObject>(nullptr, TEXT("/Script/InterchangePipelines.InterchangeGenericAssetsPipeline"));
		UClass* OverrideClass = LoadClass<UObject>(nullptr, TEXT("/Script/InterchangeEngine.InterchangePipelineStackOverride"));
		if (!PipelineClass || !OverrideClass) return MCPError(TEXT("Interchange import is unavailable; enable the Interchange Editor plugin"));
		UObject* Pipeline = NewObject<UObject>(GetTransientPackage(), PipelineClass);
		UObject* Stack = NewObject<UObject>(GetTransientPackage(), OverrideClass);
		Roots.Add(MakeUnique<FGCRootScope>(Pipeline));
		Roots.Add(MakeUnique<FGCRootScope>(Stack));
		auto Settings = MakeShared<FJsonObject>();
		Settings->SetStringField(TEXT("AssetName"), AssetName);
		Settings->SetBoolField(TEXT("bUseSourceNameForAsset"), false);
		Settings->SetStringField(TEXT("ReimportStrategy"), TEXT("ApplyPipelineProperties"));
		Settings->SetNumberField(TEXT("ImportOffsetUniformScale"), UniformScale);
		Settings->SetStringField(TEXT("CommonMeshesProperties.ForceAllMeshAsType"), TEXT("SkeletalMesh"));
		Settings->SetBoolField(TEXT("MeshPipeline.bImportSkeletalMeshes"), true);
		Settings->SetBoolField(TEXT("MeshPipeline.bImportStaticMeshes"), false);
		Settings->SetBoolField(TEXT("MeshPipeline.bImportMorphTargets"), bImportMorphTargets);
		Settings->SetBoolField(TEXT("MeshPipeline.bCreatePhysicsAsset"), bCreatePhysicsAsset);
		Settings->SetBoolField(TEXT("AnimationPipeline.bImportAnimations"), false);
		Settings->SetBoolField(TEXT("MaterialPipeline.bImportMaterials"), bImportMaterials);
		Settings->SetBoolField(TEXT("MaterialPipeline.TexturePipeline.bImportTextures"), bImportTextures);
		Settings->SetStringField(TEXT("CommonSkeletalMeshesAndAnimationsProperties.Skeleton"), Skeleton ? Skeleton->GetPathName() : TEXT("None"));
		// Older versions have no automatic skeleton selection setting.
		FProperty* AutoProperty = nullptr;
		void* AutoAddress = nullptr;
		UObject* AutoOwner = nullptr;
		FString Error;
		if (MCPJsonProperty::ResolveDottedPath(Pipeline, TEXT("CommonSkeletalMeshesAndAnimationsProperties.bTryAutoSelectSkeleton"), AutoProperty, AutoAddress, AutoOwner, Error))
			Settings->SetBoolField(TEXT("CommonSkeletalMeshesAndAnimationsProperties.bTryAutoSelectSkeleton"), false);
		TArray<TSharedPtr<FJsonValue>> Applied;
		if (!MCPApplyImportSettings({ Pipeline }, Settings, Applied, Error)) return MCPError(Error);
		auto StackSettings = MakeShared<FJsonObject>();
		StackSettings->SetArrayField(TEXT("OverridePipelines"), { MakeShared<FJsonValueString>(Pipeline->GetPathName()) });
		if (!MCPApplyImportSettings({ Stack }, StackSettings, Applied, Error)) return MCPError(Error);
		Task->Options = Stack;
	}
	else
	{
		UFbxFactory* Factory = NewObject<UFbxFactory>();
		Roots.Add(MakeUnique<FGCRootScope>(Factory));
		UFbxImportUI* UI = NewObject<UFbxImportUI>(Factory);
		UI->bAutomatedImportShouldDetectType = false;
		UI->bImportMesh = true;
		UI->bImportAsSkeletal = true;
		UI->bImportAnimations = false;
		UI->bImportMaterials = bImportMaterials;
		UI->bImportTextures = bImportTextures;
		UI->bCreatePhysicsAsset = bCreatePhysicsAsset;
		UI->MeshTypeToImport = FBXIT_SkeletalMesh;
		UI->Skeleton = Skeleton;
		UI->SkeletalMeshImportData->bImportMorphTargets = bImportMorphTargets;
		UI->SkeletalMeshImportData->ImportUniformScale = static_cast<float>(UniformScale);
		Factory->ImportUI = UI;
		Task->Factory = Factory;
		Task->Options = UI;
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("importer"), Importer);
	Result->SetStringField(TEXT("filename"), FileName);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetNumberField(TEXT("importUniformScale"), UniformScale);
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	if (bDryRun)
	{
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("saved"), false);
		return MCPResult(Result);
	}
	TArray<FAssetData> ExistingAssets;
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByPath(FName(*DestinationPath), ExistingAssets, true);
	TSet<FString> ExistingPaths;
	for (const auto& Asset : ExistingAssets) ExistingPaths.Add(Asset.GetObjectPathString());
	if (Skeleton) ExistingPaths.Add(Skeleton->GetPathName());
	FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetTasks({ Task });
	TArray<TSharedPtr<FJsonValue>> ImportedPaths, Meshes, NewPaths;
	bool bAllSaved = bSave;
	for (UObject* Object : Task->GetObjects())
	{
		if (!Object) continue;
		ImportedPaths.Add(MakeShared<FJsonValueString>(Object->GetPathName()));
		if (Object->GetOutermost()->GetName().StartsWith(DestinationPath + TEXT("/")) && !ExistingPaths.Contains(Object->GetPathName()))
			NewPaths.Add(MakeShared<FJsonValueString>(Object->GetPathName()));
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Object))
		{
			auto ReadParams = MakeShared<FJsonObject>();
			ReadParams->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
			Meshes.Add(GetMeshInfo(ReadParams));
			if (Meshes.Num() == 1)
			{
				Result->SetObjectField(TEXT("boxExtent"), MCPVec3ToJsonObject(Mesh->GetBounds().BoxExtent));
				Result->SetNumberField(TEXT("boundsRadius"), Mesh->GetBounds().SphereRadius);
				Result->SetNumberField(TEXT("numLODs"), Mesh->GetLODNum());
				Result->SetStringField(TEXT("skeleton"), Mesh->GetSkeleton() ? Mesh->GetSkeleton()->GetPathName() : FString());
				TArray<TSharedPtr<FJsonValue>> MorphNames;
				for (UMorphTarget* Morph : Mesh->GetMorphTargets()) if (Morph) MorphNames.Add(MakeShared<FJsonValueString>(Morph->GetName()));
				Result->SetArrayField(TEXT("morphTargets"), MorphNames);
				Result->SetNumberField(TEXT("morphTargetCount"), MorphNames.Num());
			}
		}
		if (bSave)
		{
			FString SaveError;
			const bool bSaved = SaveAssetPackageChecked(Object, SaveError);
			bAllSaved &= bSaved;
			MCPNoteSaveOutcome(Result, Object->GetPathName(), bSaved, SaveError);
		}
	}
	Result->SetArrayField(TEXT("importedAssets"), ImportedPaths);
	Result->SetArrayField(TEXT("meshes"), Meshes);
	Result->SetNumberField(TEXT("importedCount"), ImportedPaths.Num());
	Result->SetBoolField(TEXT("saved"), bAllSaved && Meshes.Num() > 0);
	Result->SetBoolField(TEXT("success"), Meshes.Num() > 0 && (!bSave || bAllSaved));
	if (Meshes.Num() == 0) Result->SetStringField(TEXT("error"), TEXT("Import produced no skeletal meshes"));
	if (NewPaths.Num() > 0)
	{
		MCPSetCreated(Result);
		auto Rollback = MakeShared<FJsonObject>();
		Rollback->SetArrayField(TEXT("assetPaths"), NewPaths);
		MCPSetRollback(Result, TEXT("delete_asset_batch"), Rollback);
	}
	else Result->SetBoolField(TEXT("rollbackPossible"), false);
	if (ImportedPaths.Num() > NewPaths.Num()) MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"), TEXT("Rollback deletes only newly imported assets, subject to referencer checks. Replaced meshes and edits to shared skeletons or companion assets require backup restoration."));
	return MCPResult(Result);
}


// Readback of one imported AnimSequence, the shape import_skeletal_mesh reports (#1228).
static TSharedPtr<FJsonObject> MCPDescribeImportedAnimation(UAnimSequence* Seq)
{
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("path"), Seq->GetPathName());
	Out->SetNumberField(TEXT("playLength"), Seq->GetPlayLength());
	Out->SetNumberField(TEXT("numberOfKeys"), Seq->GetNumberOfSampledKeys());
	Out->SetNumberField(TEXT("frameRate"), Seq->GetSamplingFrameRate().AsDecimal());
	Out->SetBoolField(TEXT("enableRootMotion"), Seq->bEnableRootMotion);
	const USkeleton* Skeleton = Seq->GetSkeleton();
	if (Skeleton) Out->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
	const IAnimationDataModel* Model = Seq->GetDataModel();
	if (!Model) return Out;
	Out->SetNumberField(TEXT("numberOfFrames"), Model->GetNumberOfFrames());
	TArray<FName> Tracks;
	Model->GetBoneTrackNames(Tracks);
	Out->SetNumberField(TEXT("boneTrackCount"), Tracks.Num());
	Out->SetNumberField(TEXT("curveCount"), Model->GetFloatCurves().Num());
	// Root motion came in when the root bone's own track travels.
	if (Skeleton && Skeleton->GetReferenceSkeleton().GetNum() > 0)
	{
		const FName Root = Skeleton->GetReferenceSkeleton().GetBoneName(0);
		TArray<FTransform> RootKeys;
		if (Model->IsValidBoneTrackName(Root)) Model->GetBoneTrackTransforms(Root, RootKeys);
		const double Travel = RootKeys.Num() > 1 ? FVector::Dist(RootKeys[0].GetLocation(), RootKeys.Last().GetLocation()) : 0.0;
		Out->SetStringField(TEXT("rootBone"), Root.ToString());
		Out->SetNumberField(TEXT("rootTravel"), Travel);
		Out->SetBoolField(TEXT("hasRootMotion"), Travel > UE_KINDA_SMALL_NUMBER);
	}
	return Out;
}

TSharedPtr<FJsonValue> FAssetHandlers::ImportAnimation(const TSharedPtr<FJsonObject>& Params)
{
	// Every parameter is read before anything can fail (#1057).
	FString SingleFile;
	const bool bHasFile = TryGetStringParam(Params, TEXT("filePath"), SingleFile) && !SingleFile.IsEmpty();
	const TArray<TSharedPtr<FJsonValue>>* FilesArr = nullptr;
	const bool bHasFiles = TryGetArrayParam(Params, TEXT("filePaths"), FilesArr) && FilesArr;
	FString SkeletonPath;
	TSharedPtr<FJsonValue> SkeletonErr = RequireString(Params, TEXT("skeletonPath"), SkeletonPath);
	FString DestinationPath = OptionalString(Params, TEXT("packagePath"));
	if (DestinationPath.IsEmpty()) DestinationPath = TEXT("/Game/Animations");
	bool bImportCustomAttribute = true;
	const bool bHasImportCustomAttribute = TryGetBoolParam(Params, TEXT("importCustomAttribute"), bImportCustomAttribute);
	bool bRemoveRedundantKeys = true;
	const bool bHasRemoveRedundantKeys = TryGetBoolParam(Params, TEXT("removeRedundantKeys"), bRemoveRedundantKeys);
	const TSharedPtr<FJsonObject>* ImportSettings = nullptr;
	const bool bHasImportSettings = TryGetObjectParam(Params, TEXT("importSettings"), ImportSettings) && ImportSettings;
	const FString AssetName = OptionalString(Params, TEXT("name"));
	if (SkeletonErr) return SkeletonErr;

	if (bHasFile == bHasFiles)
	{
		return MCPError(TEXT("Pass exactly one of filePath (one FBX) and filePaths (a batch into one folder)"));
	}
	TArray<FString> Files;
	if (bHasFile) Files.Add(SingleFile);
	else Files = JsonArrayToStringList(FilesArr);
	if (Files.Num() == 0) return MCPError(TEXT("filePaths is empty"));
	if (Files.Num() > 200) return MCPError(FString::Printf(TEXT("filePaths holds %d files; the limit is 200"), Files.Num()));
	if (bHasFiles && !AssetName.IsEmpty())
	{
		return MCPError(TEXT("name applies to a single filePath; a batch names each asset after its file"));
	}
	for (const FString& File : Files)
	{
		if (!FPaths::FileExists(File)) return MCPError(FString::Printf(TEXT("File not found: %s"), *File));
	}

	USkeleton* Skeleton = LoadAssetByPath<USkeleton>(SkeletonPath);
	if (!Skeleton)
	{
		return MCPAssetLoadError(SkeletonPath, TEXT("Skeleton"));
	}

	// One factory and one set of options for every file, validated before the first import.
	UFbxFactory* FbxFactory = NewObject<UFbxFactory>();
	FGCRootScope FactoryRoot(FbxFactory);

	UFbxImportUI* ImportUI = NewObject<UFbxImportUI>();
	ImportUI->bImportMesh = false;
	ImportUI->bImportAnimations = true;
	ImportUI->bImportMaterials = false;
	ImportUI->bImportTextures = false;
	ImportUI->bIsObjImport = false;
	ImportUI->MeshTypeToImport = FBXIT_Animation;
	// An automated import detects the type from the file by default, so an FBX that also carries a
	// mesh came in as a SkeletalMesh and no AnimSequence at all (#1228).
	ImportUI->bAutomatedImportShouldDetectType = false;
	ImportUI->Skeleton = Skeleton;
	if (bHasImportCustomAttribute)
	{
		ImportUI->AnimSequenceImportData->bImportCustomAttribute = bImportCustomAttribute;
	}
	if (bHasRemoveRedundantKeys)
	{
		ImportUI->AnimSequenceImportData->bRemoveRedundantKeys = bRemoveRedundantKeys;
	}

	// importSettings reaches every FbxAnimSequenceImportData field first, then
	// FbxImportUI itself (#1134).
	TArray<TSharedPtr<FJsonValue>> AppliedSettings;
	if (bHasImportSettings)
	{
		TArray<UObject*> Targets;
		Targets.Add(ImportUI->AnimSequenceImportData);
		Targets.Add(ImportUI);
		FString SettingsError;
		if (!MCPApplyImportSettings(Targets, *ImportSettings, AppliedSettings, SettingsError))
		{
			return MCPError(SettingsError);
		}
	}
	FbxFactory->ImportUI = ImportUI;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<TSharedPtr<FJsonValue>> ImportedPaths;
	TArray<TSharedPtr<FJsonValue>> Animations;
	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 FailedFiles = 0;
	for (const FString& File : Files)
	{
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		FGCRootScope TaskRoot(Task);
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = false;
		Task->Filename = File;
		Task->DestinationPath = DestinationPath;
		Task->Factory = FbxFactory;
		if (!AssetName.IsEmpty()) Task->DestinationName = AssetName;

		TArray<UAssetImportTask*> Tasks;
		Tasks.Add(Task);
		AssetToolsModule.Get().ImportAssetTasks(Tasks);

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("filePath"), File);
		TArray<TSharedPtr<FJsonValue>> FileAssets;
		TArray<FString> OtherClasses;
		int32 FileAnimations = 0;
		for (UObject* ImportedObj : Task->GetObjects())
		{
			if (!ImportedObj) continue;
			FileAssets.Add(MakeShared<FJsonValueString>(ImportedObj->GetPathName()));
			ImportedPaths.Add(MakeShared<FJsonValueString>(ImportedObj->GetPathName()));
			if (UAnimSequence* Seq = Cast<UAnimSequence>(ImportedObj))
			{
				Animations.Add(MakeShared<FJsonValueObject>(MCPDescribeImportedAnimation(Seq)));
				++FileAnimations;
			}
			else
			{
				OtherClasses.AddUnique(ImportedObj->GetClass()->GetName());
			}
		}
		Row->SetArrayField(TEXT("importedAssets"), FileAssets);
		// An animation import that produced no AnimSequence did not do its job, whatever else it made.
		Row->SetBoolField(TEXT("success"), FileAnimations > 0);
		if (FileAssets.Num() == 0)
		{
			Row->SetStringField(TEXT("error"), TEXT("The import produced no asset"));
			++FailedFiles;
		}
		else if (FileAnimations == 0)
		{
			Row->SetStringField(TEXT("error"), FString::Printf(
				TEXT("The import produced no AnimSequence, only %s"), *FString::Join(OtherClasses, TEXT(", "))));
			++FailedFiles;
		}
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	if (ImportedPaths.Num() > 0) { MCPSetCreated(Result); }
	Result->SetStringField(TEXT("filename"), Files[0]);
	Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	if (AppliedSettings.Num() > 0) Result->SetArrayField(TEXT("importSettingsApplied"), AppliedSettings);
	Result->SetArrayField(TEXT("importedAssets"), ImportedPaths);
	Result->SetNumberField(TEXT("importedCount"), ImportedPaths.Num());
	Result->SetArrayField(TEXT("animations"), Animations);
	if (bHasFiles)
	{
		Result->SetArrayField(TEXT("files"), Rows);
		Result->SetNumberField(TEXT("failedFiles"), FailedFiles);
	}
	Result->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0 && FailedFiles == 0);
	if (ImportedPaths.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("Import task completed but no assets were produced"));
	}
	else if (FailedFiles > 0)
	{
		Result->SetStringField(TEXT("error"), bHasFiles
			? FString::Printf(TEXT("%d of %d file(s) produced no AnimSequence; files lists each one"), FailedFiles, Files.Num())
			: FString::Printf(TEXT("The import produced no AnimSequence from %s; importedAssets lists what it made instead"), *Files[0]));
	}

	EmitImportedAssetsRollback(Result, ImportedPaths);

	return MCPResult(Result);
}

// #1096: one import path for any factory, including ones a third-party plugin
// ships. The factory class is resolved at runtime, so no module dependency.
// factoryProperties is how a caller turns off a factory's own options dialog,
// which unattended mode would otherwise cancel along with the import.
TSharedPtr<FJsonValue> FAssetHandlers::ImportFile(const TSharedPtr<FJsonObject>& Params)
{
	FString FileName;
	if (auto Err = RequireString(Params, TEXT("filePath"), FileName)) return Err;

	FString DestinationPath;
	if (auto Err = RequireString(Params, TEXT("packagePath"), DestinationPath)) return Err;

	// Every parameter is read before anything can fail (#1057).
	const FString FactoryClassSpec = OptionalString(Params, TEXT("factoryClass"));
	const TSharedPtr<FJsonObject>* FactoryProperties = nullptr;
	const bool bHasFactoryProperties = TryGetObjectParam(Params, TEXT("factoryProperties"), FactoryProperties) && FactoryProperties;
	const bool bAutomated = OptionalBool(Params, TEXT("automated"), true);
	const bool bReplaceExisting = OptionalBool(Params, TEXT("replaceExisting"), true);
	// An import that is never written is lost on editor close, so it saves by default like every create (#1280).
	const bool bSaveImported = OptionalBool(Params, TEXT("save"), true);
	const FString AssetName = OptionalString(Params, TEXT("name"));

	DestinationPath.RemoveFromEnd(TEXT("/"));
	if (MCPIsProtectedAssetPath(DestinationPath)) return MCPProtectedPathError(DestinationPath);

	if (!FPaths::FileExists(FileName))
	{
		return MCPError(FString::Printf(TEXT("File not found: %s"), *FileName));
	}

	UFactory* Factory = nullptr;
	if (!FactoryClassSpec.IsEmpty())
	{
		UClass* FactoryClass = MCPResolveClassOfType(FactoryClassSpec, UFactory::StaticClass());
		if (!FactoryClass) return MCPClassNotFoundError(FactoryClassSpec, TEXT("factoryClass"));
		if (auto Err = MCPCheckClassUsable(FactoryClassSpec, FactoryClass, UFactory::StaticClass())) return Err;

		Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
		if (!Factory->bEditorImport)
		{
			return MCPError(FString::Printf(
				TEXT("%s is not an import factory (bEditorImport is false), so it cannot read a source file."),
				*FactoryClass->GetName()));
		}

		// Refused here rather than handed to AssetTools, which would fall back
		// to another factory and report that one's result as this one's.
		TArray<FString> Extensions;
		Factory->GetSupportedFileExtensions(Extensions);
		const FString Ext = FPaths::GetExtension(FileName);
		bool bExtensionSupported = Extensions.Num() == 0;
		for (const FString& Candidate : Extensions)
		{
			if (Candidate.Equals(Ext, ESearchCase::IgnoreCase)) { bExtensionSupported = true; break; }
		}
		if (!bExtensionSupported)
		{
			return MCPError(FString::Printf(TEXT("%s does not import .%s files. It accepts: %s"),
				*FactoryClass->GetName(), *Ext, *FString::Join(Extensions, TEXT(", "))));
		}
	}
	FGCRootScope FactoryRoot(Factory);

	TArray<TSharedPtr<FJsonValue>> AppliedProperties;
	if (bHasFactoryProperties)
	{
		if (!Factory)
		{
			return MCPError(TEXT("factoryProperties needs factoryClass: without a named factory there is no object to set them on."));
		}
		TArray<UObject*> Targets;
		Targets.Add(Factory);
		FString PropertiesError;
		if (!MCPApplyImportSettings(Targets, *FactoryProperties, AppliedProperties, PropertiesError))
		{
			return MCPError(PropertiesError);
		}
	}

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	FGCRootScope TaskRoot(Task);
	Task->bAutomated = bAutomated;
	Task->bReplaceExisting = bReplaceExisting;
	// Saved below per asset, so a failed write is reported on its row rather than only logged.
	Task->bSave = false;
	Task->Filename = FileName;
	Task->DestinationPath = DestinationPath;
	Task->Factory = Factory; // null lets AssetTools pick by extension

	if (!AssetName.IsEmpty())
	{
		Task->DestinationName = AssetName;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	TArray<TSharedPtr<FJsonValue>> ImportedPaths;
	TArray<TSharedPtr<FJsonValue>> ImportedRows;
	bool bAllSaved = true;
	for (UObject* ImportedObj : Task->GetObjects())
	{
		if (!ImportedObj) continue;
		ImportedPaths.Add(MakeShared<FJsonValueString>(ImportedObj->GetPathName()));
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("assetPath"), ImportedObj->GetPathName());
		Row->SetStringField(TEXT("class"), ImportedObj->GetClass()->GetName());
		bool bSaved = false;
		if (bSaveImported)
		{
			FString SaveError;
			bSaved = SaveAssetPackageChecked(ImportedObj, SaveError);
			if (!bSaved) Row->SetStringField(TEXT("saveError"), SaveError);
		}
		Row->SetBoolField(TEXT("saved"), bSaved);
		UPackage* Package = ImportedObj->GetOutermost();
		if (!bSaved && Package && Package->IsDirty()) Row->SetBoolField(TEXT("dirtyNotSaved"), true);
		bAllSaved &= bSaved;
		ImportedRows.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	if (ImportedPaths.Num() > 0) { MCPSetCreated(Result); }
	Result->SetStringField(TEXT("filename"), FileName);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetStringField(TEXT("factoryClass"), Factory ? Factory->GetClass()->GetPathName() : TEXT("auto"));
	if (AppliedProperties.Num() > 0) Result->SetArrayField(TEXT("factoryPropertiesApplied"), AppliedProperties);
	Result->SetArrayField(TEXT("importedAssets"), ImportedPaths);
	Result->SetArrayField(TEXT("imported"), ImportedRows);
	Result->SetNumberField(TEXT("importedCount"), ImportedPaths.Num());
	Result->SetBoolField(TEXT("saved"), ImportedPaths.Num() > 0 && bAllSaved);
	Result->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0);
	if (bSaveImported && ImportedPaths.Num() > 0 && !bAllSaved)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("The import produced assets, but at least one could not be saved; its row carries saveError."));
	}
	if (ImportedPaths.Num() == 0)
	{
		Result->SetStringField(TEXT("error"),
			TEXT("Import task completed but no assets were produced. A factory that raises its own options dialog is cancelled in ")
			TEXT("unattended mode: set the property that makes it use the supplied options through factoryProperties. ")
			TEXT("The output log carries the factory's reason."));
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
	const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
	const bool bBatch = TryGetArrayParam(Params, TEXT("assetPaths"), Paths);
	const FString AssetPath = OptionalString(Params, TEXT("assetPath"));
	if (bBatch)
	{
		if (!AssetPath.IsEmpty() || !Paths || Paths->Num() == 0 || Paths->Num() > 500) return MCPError(TEXT("Supply assetPath or 1-500 assetPaths"));
		auto Result = MCPSuccess();
		TArray<TSharedPtr<FJsonValue>> Items;
		bool bSuccess = true;
		for (const auto& Path : *Paths)
		{
			FString Text;
			if (!Path->TryGetString(Text) || Text.IsEmpty()) return MCPError(TEXT("assetPaths must contain nonempty strings"));
			auto Single = MakeShared<FJsonObject>();
			Single->SetStringField(TEXT("assetPath"), Text);
			auto Row = ListTextureProperties(Single);
			bSuccess &= Row->AsObject()->GetBoolField(TEXT("success"));
			Items.Add(Row);
		}
		Result->SetBoolField(TEXT("success"), bSuccess);
		Result->SetArrayField(TEXT("items"), Items);
		return MCPResult(Result);
	}
	if (AssetPath.IsEmpty()) return MCPError(TEXT("Supply assetPath or assetPaths"));
	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;
	UTexture2D* Texture = Cast<UTexture2D>(Asset);
	if (!Texture) return MCPAssetWrongTypeError(AssetPath, Asset, TEXT("Texture2D"));
	// A texture imported a moment ago may still be compiling, and reads 0 x 0 until it is done.
	FTextureCompilingManager::Get().FinishCompilation({ Texture });
	auto Result = AssetTextureSettings::Read(Texture);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), Texture->GetName());
	// With no RHI there is no platform data to size; the source is the same image.
	const bool bFromSource = Texture->GetSizeX() == 0 && Texture->Source.IsValid();
	Result->SetNumberField(TEXT("sizeX"), bFromSource ? Texture->Source.GetSizeX() : Texture->GetSizeX());
	Result->SetNumberField(TEXT("sizeY"), bFromSource ? Texture->Source.GetSizeY() : Texture->GetSizeY());
	Result->SetNumberField(TEXT("numMips"), Texture->GetNumMips());
	Result->SetStringField(TEXT("pixelFormat"), GPixelFormats[Texture->GetPixelFormat()].Name);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::SetTextureProperties(const TSharedPtr<FJsonObject>& Params)
{
	// Every declared parameter is read before anything can refuse (#1057); Parse
	// stops at the first bad setting, which left the rest unread.
	MCPReadParamsAhead(Params, { TEXT("preset"), TEXT("settings"), TEXT("compressionSettings"), TEXT("lodGroup"), TEXT("sRGB"),
		TEXT("neverStream"), TEXT("flipGreenChannel"), TEXT("maxTextureSize") });
	const FString AssetPath = OptionalString(Params, TEXT("assetPath"));
	const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
	const bool bBatch = TryGetArrayParam(Params, TEXT("assetPaths"), Paths);
	const bool bSave = OptionalBool(Params, TEXT("save"), false);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	TSharedPtr<FJsonObject> Settings;
	FString Error;
	if (!AssetTextureSettings::Parse(Params, Settings, Error)) return MCPError(Error);
	if (Settings->Values.Num() == 0) return MCPError(TEXT("Supply a preset or texture settings"));
	TArray<FString> AssetPaths;
	if (bBatch)
	{
		if (!AssetPath.IsEmpty() || !Paths || Paths->Num() == 0 || Paths->Num() > 500) return MCPError(TEXT("Supply assetPath or 1-500 assetPaths"));
		for (const auto& Path : *Paths)
		{
			FString Text;
			if (!Path->TryGetString(Text) || Text.IsEmpty()) return MCPError(TEXT("assetPaths must contain nonempty strings"));
			AssetPaths.AddUnique(Text);
		}
	}
	else if (!AssetPath.IsEmpty()) AssetPaths.Add(AssetPath);
	else return MCPError(TEXT("Supply assetPath or assetPaths"));

	TArray<UTexture2D*> Textures;
	for (const FString& Path : AssetPaths)
	{
		TSharedPtr<FJsonValue> LoadError;
		UObject* Asset = MCPRequireAssetObject(Path, LoadError);
		if (!Asset) return LoadError;
		UTexture2D* Texture = Cast<UTexture2D>(Asset);
		if (!Texture) return MCPAssetWrongTypeError(Path, Asset, TEXT("Texture2D"));
		if (auto Blocked = MCPAssetWriteBlockedError(Texture, Path, TEXT("set texture settings"))) return Blocked;
		Textures.AddUnique(Texture);
	}
	TArray<TSharedPtr<FJsonValue>> Rows;
	TArray<TSharedPtr<FJsonValue>> InverseItems;
	bool bAllSaved = bSave && !bDryRun;
	FScopedTransaction Transaction(NSLOCTEXT("AssetPipeline", "TextureSettings", "Set texture settings"), !bDryRun);
	for (UTexture2D* Texture : Textures)
	{
		auto Before = AssetTextureSettings::Read(Texture);
		auto Row = MCPSuccess();
		Row->SetStringField(TEXT("assetPath"), bBatch ? Texture->GetPathName() : AssetPath);
		Row->SetObjectField(TEXT("previousSettings"), Before);
		Row->SetObjectField(TEXT("requestedSettings"), Settings);
		Row->SetBoolField(TEXT("saved"), false);
		if (!bDryRun)
		{
			Texture->Modify();
			AssetTextureSettings::Apply(Texture, Settings);
			Texture->PostEditChange();
			Texture->MarkPackageDirty();
			FString SaveError;
			const bool bSaved = bSave && SaveAssetPackageChecked(Texture, SaveError);
			Row->SetBoolField(TEXT("saved"), bSaved);
			if (bSave) MCPNoteSaveOutcome(Row, Texture->GetPathName(), bSaved, SaveError);
			bAllSaved &= bSaved;
		}
		Row->SetObjectField(TEXT("readBack"), AssetTextureSettings::Read(Texture));
		Rows.Add(MakeShared<FJsonValueObject>(Row));
		auto Inverse = MakeShared<FJsonObject>();
		Inverse->SetStringField(TEXT("assetPath"), Texture->GetPathName());
		auto Properties = MakeShared<FJsonObject>();
		Properties->SetStringField(TEXT("CompressionSettings"), TEXT("TC_") + Before->GetStringField(TEXT("compressionSettings")));
		Properties->SetStringField(TEXT("LODGroup"), TEXT("TEXTUREGROUP_") + Before->GetStringField(TEXT("lodGroup")));
		Properties->SetBoolField(TEXT("SRGB"), Before->GetBoolField(TEXT("sRGB")));
		Properties->SetBoolField(TEXT("NeverStream"), Before->GetBoolField(TEXT("neverStream")));
		Properties->SetBoolField(TEXT("bFlipGreenChannel"), Before->GetBoolField(TEXT("flipGreenChannel")));
		Properties->SetNumberField(TEXT("MaxTextureSize"), Before->GetNumberField(TEXT("maxTextureSize")));
		// Restore only requested fields, preserving unrelated edits made later.
		for (const auto& Pair : Before->Values)
		{
			const FString Key(*Pair.Key);
			if (Settings->HasField(Key)) continue;
			const FString PropertyName = Key == TEXT("sRGB") ? TEXT("SRGB")
				: Key == TEXT("lodGroup") ? TEXT("LODGroup")
				: Key == TEXT("flipGreenChannel") ? TEXT("bFlipGreenChannel")
				: Key.Left(1).ToUpper() + Key.Mid(1);
			Properties->RemoveField(PropertyName);
		}
		Inverse->SetObjectField(TEXT("properties"), Properties);
		InverseItems.Add(MakeShared<FJsonValueObject>(Inverse));
	}
	auto Result = bBatch ? MCPSuccess() : Rows[0]->AsObject();
	if (bBatch) Result->SetArrayField(TEXT("items"), Rows);
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetBoolField(TEXT("saved"), bAllSaved);
	Result->SetBoolField(TEXT("success"), !bSave || bDryRun || bAllSaved);
	Result->SetNumberField(TEXT("textureCount"), Textures.Num());
	TArray<TSharedPtr<FJsonValue>> Modified;
	for (const auto& Pair : Settings->Values) Modified.Add(MakeShared<FJsonValueString>(FString(*Pair.Key)));
	Result->SetArrayField(TEXT("modifiedProperties"), Modified);
	if (!bBatch) Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Modified %d texture properties"), bDryRun ? 0 : Modified.Num()));
	if (!bDryRun)
	{
		MCPSetUpdated(Result);
		auto Rollback = MakeShared<FJsonObject>();
		Rollback->SetArrayField(TEXT("items"), InverseItems);
		Rollback->SetBoolField(TEXT("save"), bSave);
		MCPSetRollback(Result, TEXT("bulk_set_asset_properties"), Rollback);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
		Result->SetStringField(TEXT("rollbackNote"), TEXT("Restores only the requested settings on each texture, using the same save choice."));
	}
	else Result->SetBoolField(TEXT("unchanged"), true);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ImportTextureBatch(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	const bool bHasItems = TryGetArrayParam(Params, TEXT("items"), Items);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bAutomated = OptionalBool(Params, TEXT("automated"), true);
	const FString DefaultPackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Textures"));
	if (!bHasItems || !Items || Items->Num() == 0 || Items->Num() > 500) return MCPError(TEXT("Supply 1-500 items"));

	TArray<UAssetImportTask*> Tasks;
	TArray<TUniquePtr<FGCRootScope>> Roots;
	TArray<TSharedPtr<FJsonObject>> Settings;
	TArray<TSharedPtr<FJsonValue>> Rows;
	TSet<FString> Destinations;
	for (const auto& Entry : *Items)
	{
		const TSharedPtr<FJsonObject>* Item = nullptr;
		if (!Entry->TryGetObject(Item)) return MCPError(TEXT("Each item must be an object"));
		FString FilePath;
		if (auto Err = RequireString(*Item, TEXT("filePath"), FilePath)) return Err;
		if (!FPaths::FileExists(FilePath)) return MCPError(TEXT("File not found: ") + FilePath);
		TSharedPtr<FJsonObject> Parsed;
		FString Error;
		if (!AssetTextureSettings::Parse(*Item, Parsed, Error)) return MCPError(Error);
		for (const TCHAR* Key : { TEXT("packagePath"), TEXT("name") })
		{
			FString Value;
			if (HasParam(*Item, Key) && !TryGetStringParam(*Item, Key, Value)) return MCPError(FString(Key) + TEXT(" must be a string"));
		}
		bool bReplace = true;
		if (HasParam(*Item, TEXT("replaceExisting")) && !TryGetBoolParam(*Item, TEXT("replaceExisting"), bReplace))
			return MCPError(TEXT("replaceExisting must be boolean"));
		const FString Folder = OptionalString(*Item, TEXT("packagePath"), DefaultPackagePath);
		const FString Name = OptionalString(*Item, TEXT("name"), FPaths::GetBaseFilename(FilePath));
		const FString Destination = Folder / Name;
		if (Name.IsEmpty() || Name.Contains(TEXT("/")) || Name.Contains(TEXT(".")) || !FPackageName::IsValidLongPackageName(Destination)) return MCPError(TEXT("Invalid texture destination: ") + Destination);
		if (MCPIsProtectedAssetPath(Destination)) return MCPError(TEXT("Cannot import to a protected mount: ") + Destination);
		if (UObject* Existing = MCPLoadAssetObject(Destination))
		{
			if (auto Blocked = MCPAssetWriteBlockedError(Existing, Destination, TEXT("import texture"))) return Blocked;
		}
		if (Destinations.Contains(Destination.ToLower())) return MCPError(TEXT("Duplicate destination: ") + Destination);
		Destinations.Add(Destination.ToLower());
		const bool bExists = UEditorAssetLibrary::DoesAssetExist(Destination);
		if (bExists && !bReplace) return MCPError(TEXT("Destination exists and replaceExisting is false: ") + Destination);
		if (bExists && !LoadAssetByPath<UTexture2D>(Destination)) return MCPError(TEXT("Destination is not a Texture2D: ") + Destination);
		auto Row = MCPSuccess();
		Row->SetStringField(TEXT("filePath"), FilePath);
		Row->SetStringField(TEXT("packagePath"), Folder);
		Row->SetStringField(TEXT("name"), Name);
		Row->SetBoolField(TEXT("replacedExisting"), bExists);
		Row->SetObjectField(TEXT("requestedSettings"), Parsed);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
		Settings.Add(Parsed);
		if (bDryRun) continue;
		UTextureFactory* Factory = NewObject<UTextureFactory>();
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Roots.Add(MakeUnique<FGCRootScope>(Factory));
		Roots.Add(MakeUnique<FGCRootScope>(Task));
		Task->bAutomated = bAutomated;
		Task->bReplaceExisting = bReplace;
		Task->bSave = false;
		Task->Filename = FilePath;
		Task->DestinationPath = Folder;
		Task->DestinationName = Name;
		Task->Factory = Factory;
		Tasks.Add(Task);
	}
	if (!bDryRun) FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetTasks(Tasks);
	int32 Imported = 0;
	TArray<TSharedPtr<FJsonValue>> CreatedPaths;
	bool bReplaced = false;
	for (int32 Index = 0; Index < Tasks.Num(); ++Index)
	{
		auto Row = Rows[Index]->AsObject();
		TArray<TSharedPtr<FJsonValue>> ImportedPaths, ReadBack;
		bool bSaved = bSave;
		for (UObject* Object : Tasks[Index]->GetObjects())
		{
			UTexture2D* Texture = Cast<UTexture2D>(Object);
			if (!Texture) continue;
			// An entry with no settings keeps the import as it is, with no second build.
			if (Settings[Index]->Values.Num() > 0)
			{
				Texture->Modify();
				AssetTextureSettings::Apply(Texture, Settings[Index]);
				Texture->PostEditChange();
				Texture->MarkPackageDirty();
			}
			FString SaveError;
			const bool bTextureSaved = bSave && SaveAssetPackageChecked(Texture, SaveError);
			bSaved &= bTextureSaved;
			if (bSave) MCPNoteSaveOutcome(Row, Texture->GetPathName(), bTextureSaved, SaveError);
			ImportedPaths.Add(MakeShared<FJsonValueString>(Texture->GetPathName()));
			auto ReadParams = MakeShared<FJsonObject>();
			ReadParams->SetStringField(TEXT("assetPath"), Texture->GetPathName());
			ReadBack.Add(ListTextureProperties(ReadParams));
			if (!Row->GetBoolField(TEXT("replacedExisting"))) CreatedPaths.Add(MakeShared<FJsonValueString>(Texture->GetPathName()));
			else bReplaced = true;
		}
		Row->SetArrayField(TEXT("importedAssets"), ImportedPaths);
		Row->SetArrayField(TEXT("readBack"), ReadBack);
		Row->SetBoolField(TEXT("saved"), bSaved && ImportedPaths.Num() > 0);
		Row->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0 && (!bSave || bSaved));
		if (ImportedPaths.Num() > 0) ++Imported;
		else Row->SetStringField(TEXT("error"), TEXT("Import produced no textures"));
	}
	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("items"), Rows);
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetBoolField(TEXT("unchanged"), bDryRun || Imported == 0);
	Result->SetNumberField(TEXT("requested"), Items->Num());
	Result->SetNumberField(TEXT("imported"), Imported);
	Result->SetNumberField(TEXT("failed"), bDryRun ? 0 : Items->Num() - Imported);
	bool bSuccess = true;
	for (const auto& Row : Rows) bSuccess &= Row->AsObject()->GetBoolField(TEXT("success"));
	Result->SetBoolField(TEXT("success"), bSuccess);
	if (CreatedPaths.Num() > 0) MCPSetCreated(Result);
	if (bReplaced) MCPSetUpdated(Result);
	if (CreatedPaths.Num() > 0)
	{
		auto Rollback = MakeShared<FJsonObject>();
		Rollback->SetArrayField(TEXT("assetPaths"), CreatedPaths);
		MCPSetRollback(Result, TEXT("delete_asset_batch"), Rollback);
	}
	else Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetBoolField(TEXT("rollbackLossy"), bReplaced);
	Result->SetStringField(TEXT("rollbackNote"), TEXT("Rollback deletes only newly created textures, subject to referencer checks. Replaced source pixels cannot be restored; restore those assets from a backup."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ImportTexture(const TSharedPtr<FJsonObject>& Params)
{
	FString FileName;
	if (auto Err = RequireString(Params, TEXT("filePath"), FileName)) return Err;

	FString DestinationPath = OptionalString(Params, TEXT("packagePath"));
	if (DestinationPath.IsEmpty()) DestinationPath = TEXT("/Game/Textures");

	// Every parameter is read before anything can fail (#1057).
	bool bNoCompression = false;
	const bool bHasNoCompression = TryGetBoolParam(Params, TEXT("noCompression"), bNoCompression);
	bool bNoAlpha = false;
	const bool bHasNoAlpha = TryGetBoolParam(Params, TEXT("noAlpha"), bNoAlpha);
	const FString AssetName = OptionalString(Params, TEXT("name"));
	const bool bHasSRGB = HasParam(Params, TEXT("sRGB"));
	const bool bSRGB = OptionalBool(Params, TEXT("sRGB"), true);
	const bool bHasCompression = HasParam(Params, TEXT("compressionSettings"));
	const FString CompressionSettings = OptionalString(Params, TEXT("compressionSettings"));
	const bool bHasLodGroup = HasParam(Params, TEXT("lodGroup"));
	const FString LodGroup = OptionalString(Params, TEXT("lodGroup"));
	const bool bHasNeverStream = HasParam(Params, TEXT("neverStream"));
	const bool bNeverStream = OptionalBool(Params, TEXT("neverStream"), false);

	if (!FPaths::FileExists(FileName))
	{
		return MCPError(FString::Printf(TEXT("File not found: %s"), *FileName));
	}

	UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
	FGCRootScope FactoryRoot(TextureFactory);

	// Apply optional settings
	if (bHasNoCompression)
	{
		TextureFactory->NoCompression = bNoCompression;
	}
	if (bHasNoAlpha)
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
	if (ImportedPaths.Num() == 1 && (bHasSRGB || bHasCompression || bHasLodGroup || bHasNeverStream))
	{
		TSharedPtr<FJsonObject> SettingsParams = MakeShared<FJsonObject>();
		SettingsParams->SetStringField(TEXT("assetPath"), ImportedPaths[0]->AsString());
		if (bHasSRGB) SettingsParams->SetBoolField(TEXT("sRGB"), bSRGB);
		if (bHasCompression) SettingsParams->SetStringField(TEXT("compressionSettings"), CompressionSettings);
		if (bHasLodGroup) SettingsParams->SetStringField(TEXT("lodGroup"), LodGroup);
		if (bHasNeverStream) SettingsParams->SetBoolField(TEXT("neverStream"), bNeverStream);
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
	FString SaveReason;
	const bool bSaved = SaveCurveTableChange(Table, SaveReason);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	MCPNoteSaveOutcome(Result, Table->GetPathName(), bSaved, SaveReason);
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
	const FString RowFilter = OptionalString(Params, TEXT("rowFilter")).ToLower();

	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("CurveTable"), AssetPath, Table)) return Err;

	const ECurveTableMode Mode = Table->GetCurveTableMode();

	TArray<TSharedPtr<FJsonValue>> Rows;
	TArray<TSharedPtr<FJsonValue>> RowNames;
	for (const TPair<FName, FRealCurve*>& Pair : Table->GetRowMap())
	{
		const FString Name = Pair.Key.ToString();
		if (!RowFilter.IsEmpty() && !Name.ToLower().Contains(RowFilter))
		{
			continue;
		}
		RowNames.Add(MakeShared<FJsonValueString>(Name));

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
	// Every parameter is read before anything can fail (#1057). jsonPath and
	// csvPath are aliases of filePath, resolved by the registry.
	FString Format = OptionalString(Params, TEXT("format")).ToLower();
	FString JsonData;
	const bool bHasJsonData = TryGetStringParam(Params, TEXT("jsonString"), JsonData) && !JsonData.IsEmpty();
	FString CsvData;
	const bool bHasCsvData = TryGetStringParam(Params, TEXT("csvString"), CsvData) && !CsvData.IsEmpty();
	FString FilePath;
	const bool bHasFilePath = TryGetStringParam(Params, TEXT("filePath"), FilePath);
	const FString InterpRaw = OptionalString(Params, TEXT("interpMode"), TEXT("linear"));

	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("CurveTable"), AssetPath, Table)) return Err;

	FString Data;
	if (bHasJsonData)
	{
		Data = JsonData;
		Format = TEXT("json");
	}
	else if (bHasCsvData)
	{
		Data = CsvData;
		Format = TEXT("csv");
	}
	else
	{
		if (!bHasFilePath)
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

	FString SaveReason;
	const bool bSaved = SaveCurveTableChange(Table, SaveReason);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	MCPNoteSaveOutcome(Result, Table->GetPathName(), bSaved, SaveReason);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("format"), Format);
	Result->SetStringField(TEXT("curveType"), CurveTableModeName(Table->GetCurveTableMode()));
	Result->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());

	EmitPriorCurveTableRollback(Result, AssetPath, PriorJson, PriorRowCount, PriorMode);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::AddCurveTableRow(const TSharedPtr<FJsonObject>& Params)
{
	// Every parameter is read before anything can fail (#1057). `mode` is an
	// alias of curveType, resolved by the registry.
	FString RowName;
	const TSharedPtr<FJsonValue> MissingRowName = RequireString(Params, TEXT("rowName"), RowName);
	const FString InterpRaw = OptionalString(Params, TEXT("interpMode"), TEXT("linear"));
	FString CurveType = OptionalString(Params, TEXT("curveType"));
	CurveType = CurveType.ToLower();

	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("CurveTable"), AssetPath, Table)) return Err;
	if (MissingRowName) return MissingRowName;
	// The spec types curveType as a string, so an unknown value is refused here
	// rather than silently keeping the inferred curve type.
	if (!CurveType.IsEmpty() && CurveType != TEXT("simple") && CurveType != TEXT("rich"))
	{
		return MCPError(FString::Printf(TEXT("Unknown curveType '%s'. Use simple or rich."), *CurveType));
	}

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
	if (!ParseCurveInterpMode(InterpRaw, InterpMode))
	{
		return MCPError(FString::Printf(TEXT("Unknown interpMode '%s'. Use linear, constant, cubic or none."), *InterpRaw));
	}

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

	FString SaveReason;
	const bool bSaved = SaveCurveTableChange(Table, SaveReason);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	MCPNoteSaveOutcome(Result, Table->GetPathName(), bSaved, SaveReason);
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
	FString RowName;
	const TSharedPtr<FJsonValue> MissingRowName = RequireString(Params, TEXT("rowName"), RowName);
	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("CurveTable"), AssetPath, Table)) return Err;
	if (MissingRowName) return MissingRowName;

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
	FString SaveReason;
	const bool bSaved = SaveCurveTableChange(Table, SaveReason);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	MCPNoteSaveOutcome(Result, Table->GetPathName(), bSaved, SaveReason);
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
	// `rowName` is an alias of oldName, resolved by the registry (#1057).
	FString OldName;
	const TSharedPtr<FJsonValue> MissingOldName = RequireString(Params, TEXT("oldName"), OldName);
	FString NewName;
	const TSharedPtr<FJsonValue> MissingNewName = RequireString(Params, TEXT("newName"), NewName);
	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("CurveTable"), AssetPath, Table)) return Err;
	if (MissingOldName) return MissingOldName;
	if (MissingNewName) return MissingNewName;

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
	FString SaveReason;
	const bool bSaved = SaveCurveTableChange(Table, SaveReason);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	MCPNoteSaveOutcome(Result, Table->GetPathName(), bSaved, SaveReason);
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
	FString RowName;
	const TSharedPtr<FJsonValue> MissingRowName = RequireString(Params, TEXT("rowName"), RowName);
	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("CurveTable"), AssetPath, Table)) return Err;
	if (MissingRowName) return MissingRowName;

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
	// Every parameter is read before anything can fail (#1057).
	FString RowName;
	const TSharedPtr<FJsonValue> MissingRowName = RequireString(Params, TEXT("rowName"), RowName);
	const TArray<TSharedPtr<FJsonValue>>* KeyValues = nullptr;
	const bool bHasKeys = TryGetArrayParam(Params, TEXT("keys"), KeyValues);

	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("CurveTable"), AssetPath, Table)) return Err;
	if (MissingRowName) return MissingRowName;

	FRealCurve* Curve = GetCurveTableRow(Table, RowName);
	if (!Curve)
	{
		return MCPError(FString::Printf(TEXT("Row not found: %s"), *RowName));
	}

	if (!bHasKeys || !KeyValues)
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

	FString SaveReason;
	const bool bSaved = SaveCurveTableChange(Table, SaveReason);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	MCPNoteSaveOutcome(Result, Table->GetPathName(), bSaved, SaveReason);
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
	// Every parameter is read before anything can fail (#1057).
	FString RowName;
	const TSharedPtr<FJsonValue> MissingRowName = RequireString(Params, TEXT("rowName"), RowName);
	double Time = 0.0;
	double Value = 0.0;
	const bool bHasTime = TryGetNumberParam(Params, TEXT("time"), Time);
	const bool bHasValue = TryGetNumberParam(Params, TEXT("value"), Value);
	const FString InterpRaw = OptionalString(Params, TEXT("interpMode"), TEXT("linear"));
	const float Tolerance = (float)OptionalNumber(Params, TEXT("keyTimeTolerance"), UE_KINDA_SMALL_NUMBER);

	FString AssetPath;
	UCurveTable* Table = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("CurveTable"), AssetPath, Table)) return Err;
	if (MissingRowName) return MissingRowName;

	FRealCurve* Curve = GetCurveTableRow(Table, RowName);
	if (!Curve)
	{
		return MCPError(FString::Printf(TEXT("Row not found: %s"), *RowName));
	}

	if (!bHasTime || !bHasValue)
	{
		return MCPError(TEXT("Missing numeric time and value fields."));
	}

	ERichCurveInterpMode InterpMode = RCIM_Linear;
	if (!ParseCurveInterpMode(InterpRaw, InterpMode))
	{
		return MCPError(FString::Printf(TEXT("Unknown interpMode '%s'. Use linear, constant, cubic or none."), *InterpRaw));
	}

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

	FString SaveReason;
	const bool bSaved = SaveCurveTableChange(Table, SaveReason);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	MCPNoteSaveOutcome(Result, Table->GetPathName(), bSaved, SaveReason);
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

	// #1088: FTableRowBase and /Script/Engine.FTableRowBase are the spellings
	// callers copy from headers; the UScriptStruct is named TableRowBase.
	FString ResolveError;
	UScriptStruct* ScriptStruct = MCPResolveScriptStruct(RowStruct, &ResolveError);
	if (!ScriptStruct)
	{
		return MCPError(ResolveError.IsEmpty()
			? FString::Printf(TEXT("Row struct not found: %s"), *RowStruct)
			: ResolveError);
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
	// Every declared parameter is read before anything can refuse (#1057).
	MCPReadParamsAhead(Params, { TEXT("assetPath"), TEXT("rowFilter"), TEXT("rowNames"), TEXT("columns"), TEXT("outputPath") });
	FString RowFilter = OptionalString(Params, TEXT("rowFilter"));
	// A large table overflows the response inline, so outputPath writes the
	// rows to a file under the shared dump convention (HandlerUtils.h) and
	// the response carries only where they went and how much was written.
	const FString OutputPath = OptionalString(Params, TEXT("outputPath"));
	// One field across hundreds of rows is a common ask, and every row of a
	// wide table inline overflows the response even after rowFilter. columns
	// narrows each row to its Name and the named fields. Read here so every
	// parameter is read before anything can fail (#1057); the names are
	// checked against the row struct once the table has loaded.
	const bool bHasColumns = HasParam(Params, TEXT("columns"));
	const TArray<TSharedPtr<FJsonValue>>* ColumnsJson = nullptr;
	if (bHasColumns && !(TryGetArrayParam(Params, TEXT("columns"), ColumnsJson) && ColumnsJson))
	{
		return MCPError(TEXT("'columns' must be an array of row-struct field names"));
	}
	// rowNames reads a known set of rows, in the order asked for. rowFilter is
	// a substring, so "Axe" also returns "AxeHeavy"; an exact list could only
	// be had by filtering the whole table afterwards.
	const bool bHasRowNames = HasParam(Params, TEXT("rowNames"));
	TArray<FName> WantedRows;
	if (bHasRowNames)
	{
		const TArray<TSharedPtr<FJsonValue>>* RowNamesJson = nullptr;
		if (!TryGetArrayParam(Params, TEXT("rowNames"), RowNamesJson) || !RowNamesJson || RowNamesJson->Num() == 0)
		{
			return MCPError(TEXT("'rowNames' must be a non-empty array of row names"));
		}
		for (const TSharedPtr<FJsonValue>& Value : *RowNamesJson)
		{
			FString RowName;
			if (!Value.IsValid() || !Value->TryGetString(RowName) || RowName.TrimStartAndEnd().IsEmpty())
			{
				return MCPError(TEXT("'rowNames' must be an array of non-empty row names"));
			}
			RowName.TrimStartAndEndInline();
			// FName asserts on a name this long, and no row can carry one.
			if (RowName.Len() >= NAME_SIZE)
			{
				return MCPError(FString::Printf(
					TEXT("'rowNames' holds a name of %d characters; a row name is at most %d"), RowName.Len(), NAME_SIZE - 1));
			}
			WantedRows.AddUnique(FName(*RowName));
		}
	}

	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("DataTable"), AssetPath, DataTable)) return Err;

	// Get the row struct for property iteration
	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct)
	{
		return MCPError(TEXT("DataTable has no row struct"));
	}

	// A column is matched the way the table importer matches a header, by the
	// field's internal or display name, and the key kept is the one the JSON
	// export writes, so a user-defined struct's friendly names work as typed.
	TArray<FString> ColumnKeys;
	if (bHasColumns)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ColumnsJson)
		{
			FString Column;
			if (!Value.IsValid() || !Value->TryGetString(Column) || Column.IsEmpty())
			{
				return MCPError(TEXT("'columns' must be an array of non-empty field names"));
			}
			const FProperty* Match = nullptr;
			for (TFieldIterator<FProperty> It(RowStruct); It && !Match; ++It)
			{
				if (DataTableUtils::GetPropertyImportNames(*It).Contains(Column)) Match = *It;
			}
			if (!Match)
			{
				TArray<FString> ExportNames;
				for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
				{
					ExportNames.Add(DataTableUtils::GetPropertyExportName(*It));
				}
				return MCPError(FString::Printf(
					TEXT("Unknown column '%s' in 'columns'. Row struct %s has these fields: %s"),
					*Column, *RowStruct->GetName(), *FString::Join(ExportNames, TEXT(", "))));
			}
			ColumnKeys.AddUnique(DataTableUtils::GetPropertyExportName(Match));
		}
	}

	// Export the table as JSON for reliable serialization, then parse it
	FString JsonString = DataTable->GetTableAsJSON(EDataTableExportFlags::UseJsonObjectsForStructs);

	auto Result = MCPSuccess();

	TArray<TSharedPtr<FJsonValue>> ParsedRows;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
	const bool bParsed = FJsonSerializer::Deserialize(JsonReader, ParsedRows);
	if (!bParsed && (bHasColumns || bHasRowNames))
	{
		// The raw-export fallback below has no rows to narrow.
		return MCPError(
			TEXT("The DataTable's JSON export could not be parsed, so 'columns' and 'rowNames' cannot be applied. ")
			TEXT("Call read_datatable without them to get the raw export as rawJson."));
	}
	TArray<TSharedPtr<FJsonValue>> MissingRows;
	if (bParsed)
	{
		// Row names are FNames, so they match case-insensitively, as the
		// table's own FindRow does. A name the table lacks is reported rather
		// than refused, so one stale name does not cost the rest of the read.
		// Selected before rowFilter, so a name rowFilter drops is not reported
		// as missing from the table.
		if (bHasRowNames)
		{
			TMap<FName, TSharedPtr<FJsonValue>> ByName;
			for (const TSharedPtr<FJsonValue>& RowValue : ParsedRows)
			{
				FString RowName;
				if (RowValue.IsValid() && RowValue->Type == EJson::Object
					&& RowValue->AsObject()->TryGetStringField(TEXT("Name"), RowName))
				{
					ByName.Add(FName(*RowName), RowValue);
				}
			}
			TArray<TSharedPtr<FJsonValue>> Selected;
			for (const FName& Wanted : WantedRows)
			{
				if (const TSharedPtr<FJsonValue>* Found = ByName.Find(Wanted)) Selected.Add(*Found);
				else MissingRows.Add(MakeShared<FJsonValueString>(Wanted.ToString()));
			}
			ParsedRows = MoveTemp(Selected);
		}

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
			ParsedRows = MoveTemp(FilteredRows);
		}

		// Project after the filter, so only the rows that are going out are
		// rebuilt. Name stays first; the fields follow in the order asked for.
		if (bHasColumns)
		{
			for (TSharedPtr<FJsonValue>& RowValue : ParsedRows)
			{
				if (!RowValue.IsValid() || RowValue->Type != EJson::Object) continue;
				const TSharedPtr<FJsonObject>& RowObj = RowValue->AsObject();
				TSharedPtr<FJsonObject> Projected = MakeShared<FJsonObject>();
				if (TSharedPtr<FJsonValue> Name = RowObj->TryGetField(TEXT("Name")))
				{
					Projected->SetField(TEXT("Name"), Name);
				}
				for (const FString& Key : ColumnKeys)
				{
					if (TSharedPtr<FJsonValue> Cell = RowObj->TryGetField(Key))
					{
						Projected->SetField(Key, Cell);
					}
				}
				RowValue = MakeShared<FJsonValueObject>(Projected);
			}
		}
	}
	const bool bFiltered = bParsed && (!RowFilter.IsEmpty() || bHasRowNames);
	if (bHasRowNames) Result->SetArrayField(TEXT("missingRowNames"), MissingRows);

	if (!OutputPath.IsEmpty())
	{
		// The file holds the same row objects the inline form returns, so the
		// export cannot be written when it did not parse: there would be no
		// rows to filter, and a raw string is not the shape the caller asked for.
		if (!bParsed)
		{
			return MCPError(FString::Printf(
				TEXT("The DataTable's JSON export could not be parsed, so no rows were written to '%s'. ")
				TEXT("Call read_datatable without outputPath to get the raw export as rawJson."),
				*OutputPath));
		}

		FString RowsText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RowsText);
		if (!FJsonSerializer::Serialize(ParsedRows, Writer))
		{
			return MCPError(TEXT("Failed to serialize DataTable rows"));
		}

		const FString ResolvedPath = FPaths::ConvertRelativePathToFull(MCPResolveDumpPath(OutputPath));
		FString WriteError;
		if (!MCPWriteDumpFile(ResolvedPath, RowsText, TEXT("DataTable rows"), WriteError))
		{
			return MCPError(WriteError);
		}

		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetStringField(TEXT("rowStruct"), RowStruct->GetName());
		Result->SetNumberField(TEXT("totalRowCount"), DataTable->GetRowMap().Num());
		if (bFiltered)
		{
			Result->SetNumberField(TEXT("filteredCount"), ParsedRows.Num());
		}
		if (bHasColumns)
		{
			Result->SetArrayField(TEXT("columns"), MCPStringListToJson(ColumnKeys));
		}
		Result->SetStringField(TEXT("outputPath"), ResolvedPath);
		Result->SetNumberField(TEXT("rowCount"), ParsedRows.Num());
		Result->SetNumberField(TEXT("bytes"), static_cast<double>(IFileManager::Get().FileSize(*ResolvedPath)));
		// rowNames is left out as well: it grows with the table just as rows
		// does, and every row in the file carries its own Name.
		return MCPResult(Result);
	}

	if (bParsed)
	{
		Result->SetArrayField(TEXT("rows"), ParsedRows);
		if (bFiltered)
		{
			Result->SetNumberField(TEXT("filteredCount"), ParsedRows.Num());
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
	if (bHasColumns)
	{
		Result->SetArrayField(TEXT("columns"), MCPStringListToJson(ColumnKeys));
	}

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
	// Every parameter is read before anything can fail (#1057).
	FString JsonString;
	const bool bHasJsonString = TryGetStringParam(Params, TEXT("jsonString"), JsonString) && !JsonString.IsEmpty();
	FString JsonPath;
	const bool bHasJsonPath = TryGetStringParam(Params, TEXT("jsonPath"), JsonPath) && !JsonPath.IsEmpty();

	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("DataTable"), AssetPath, DataTable)) return Err;

	// Get JSON string from either inline jsonString or from a file path
	if (!bHasJsonString)
	{
		JsonString.Reset();
		if (bHasJsonPath)
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
	// Every parameter is read before anything can fail (#1057). `fields` and
	// `data` are aliases of row, resolved by the registry before dispatch.
	FString RowName;
	const TSharedPtr<FJsonValue> MissingRowName = RequireString(Params, TEXT("rowName"), RowName);
	const TSharedPtr<FJsonObject>* RowObj = nullptr;
	TryGetObjectParam(Params, TEXT("row"), RowObj);

	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("DataTable"), AssetPath, DataTable)) return Err;
	if (MissingRowName) return MissingRowName;

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
	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(DataTable, SaveReason);

	auto Result = MCPSuccess();
	if (bExisted) MCPSetUpdated(Result); else MCPSetCreated(Result);
	MCPNoteSaveOutcome(Result, DataTable->GetPathName(), bSaved, SaveReason);
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
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	if (HasParam(Params, TEXT("rowNames")) || !bSave) return RemoveDataTableRows(Params);
	FString RowName;
	const TSharedPtr<FJsonValue> MissingRowName = RequireString(Params, TEXT("rowName"), RowName);
	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("DataTable"), AssetPath, DataTable)) return Err;
	if (MissingRowName) return MissingRowName;

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
	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(DataTable, SaveReason);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	MCPNoteSaveOutcome(Result, DataTable->GetPathName(), bSaved, SaveReason);
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
	FString RowName;
	const TSharedPtr<FJsonValue> MissingRowName = RequireString(Params, TEXT("rowName"), RowName);
	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("DataTable"), AssetPath, DataTable)) return Err;
	if (MissingRowName) return MissingRowName;

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
	const TSharedPtr<FJsonValue> MissingFieldName = RequireString(Params, TEXT("fieldName"), FieldName);
	const TSharedPtr<FJsonValue> ValueField = TryGetParam(Params, TEXT("value"));
	FString RowName;
	const TSharedPtr<FJsonValue> MissingRowName = RequireString(Params, TEXT("rowName"), RowName);
	if (MissingFieldName) return MissingFieldName;
	if (!ValueField.IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	// Build a one-field row object and delegate to the row upsert, which
	// requires the row to exist for a cell edit (no accidental row creation).
	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("DataTable"), AssetPath, DataTable)) return Err;
	if (MissingRowName) return MissingRowName;
	if (!DataTable->GetRowMap().Contains(FName(*RowName)))
	{
		return MCPError(FString::Printf(TEXT("Row not found: %s (use set_datatable_row to create it)"), *RowName));
	}

	TSharedPtr<FJsonObject> RowObj = MakeShared<FJsonObject>();
	RowObj->SetField(FieldName, ValueField);
	TSharedPtr<FJsonObject> Delegated = MakeShared<FJsonObject>();
	Delegated->SetStringField(TEXT("assetPath"), AssetPath);
	Delegated->SetStringField(TEXT("rowName"), RowName);
	Delegated->SetObjectField(TEXT("row"), RowObj);
	return SetDataTableRow(Delegated);
}

// Batched cell writes with whole-table verification. The case that asked for
// it: relabelling one column on a few hundred rows of a recipe table and
// proving that no other field moved. set_datatable_cell is one round trip per
// cell, fill_datatable_from_json wants whole rows, and neither compares the
// table afterwards, so the work fell back to a JSON round trip through
// execute_python and a hand-written diff.
//
// Every cell is validated first, on a staged copy of its row: the row exists,
// the column is a top-level field of the row struct, the value parses. One
// failure and nothing is written; every failing cell is reported. The staged
// rows are then copied into the table inside one transaction with one
// Modify and a notification per changed row. Every row is re-exported and
// compared with the prior export: a touched row may differ only in the fields
// that were requested, each of which must hold the requested value, and every
// other row must export the same text. A mismatch restores every row from its
// backup, cancels the transaction and names the row and field.
// Params: assetPath, cells [{row, column, value}], dryRun?, save?.
TSharedPtr<FJsonValue> FAssetHandlers::SetDataTableCells(const TSharedPtr<FJsonObject>& Params)
{
	// Every parameter is read before anything can fail (#1057).
	const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
	const bool bHasCells = TryGetArrayParam(Params, TEXT("cells"), Cells);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bSave = OptionalBool(Params, TEXT("save"), false);

	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("DataTable"), AssetPath, DataTable)) return Err;
	if (!bHasCells || !Cells) return MCPError(TEXT("Missing 'cells' array of {row, column, value}"));
	if (Cells->Num() == 0) return MCPError(TEXT("'cells' must contain at least one entry"));

	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct) return MCPError(TEXT("DataTable has no row struct"));

	TArray<FProperty*> Props;
	for (TFieldIterator<FProperty> It(RowStruct); It; ++It) Props.Add(*It);

	// One field as text, with no defaults so every value is written out. With
	// the value passed as its own default, as the single-row read does, a
	// nested struct exports as "()" and every struct would compare equal.
	auto ExportField = [](const FProperty* Prop, const void* ContainerPtr) -> FString
	{
		FString Text;
		Prop->ExportText_Direct(Text, Prop->ContainerPtrToValuePtr<void>(ContainerPtr), nullptr, nullptr, PPF_None);
		return Text;
	};
	auto ExportRow = [&Props, &ExportField](const uint8* RowPtr) -> TArray<FString>
	{
		TArray<FString> Fields;
		Fields.Reserve(Props.Num());
		for (const FProperty* Prop : Props) Fields.Add(ExportField(Prop, RowPtr));
		return Fields;
	};

	struct FCell
	{
		FString Row;
		FString Column;
		FName RowKey;
		FProperty* Prop = nullptr;
		TSharedPtr<FJsonValue> Value;
		TSharedPtr<FJsonValue> Previous;
		bool bChanged = false;
	};
	struct FStagedRow
	{
		uint8* Live = nullptr;    // the table's own row memory
		uint8* Staged = nullptr;  // a copy carrying the requested writes
		TArray<int32> CellIndices;
		bool bChanged = false;
	};
	TArray<FCell> Parsed;
	Parsed.Reserve(Cells->Num());
	TArray<FStagedRow> Staged;
	TMap<FName, int32> StagedIndexByRow;
	TMap<FName, uint8*> Backups;

	const int32 StructSize = RowStruct->GetStructureSize();
	const int32 MinAlign = RowStruct->GetMinAlignment();
	auto AllocCopy = [RowStruct, StructSize, MinAlign](const uint8* From) -> uint8*
	{
		uint8* Buffer = (uint8*)FMemory::Malloc(StructSize, MinAlign);
		RowStruct->InitializeStruct(Buffer);
		RowStruct->CopyScriptStruct(Buffer, From);
		return Buffer;
	};
	auto FreeCopy = [RowStruct](uint8* Buffer)
	{
		if (!Buffer) return;
		RowStruct->DestroyStruct(Buffer);
		FMemory::Free(Buffer);
	};
	ON_SCOPE_EXIT
	{
		for (const FStagedRow& Row : Staged) FreeCopy(Row.Staged);
		for (const TPair<FName, uint8*>& Pair : Backups) FreeCopy(Pair.Value);
	};

	// ── Validate every cell before anything is written. Rejections are
	// collected rather than returned, so the caller sees all of them at once.
	TArray<TSharedPtr<FJsonValue>> Errors;
	auto Reject = [&Errors](int32 Index, const FString& Row, const FString& Column, const FString& Message)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("index"), Index);
		if (!Row.IsEmpty()) Entry->SetStringField(TEXT("row"), Row);
		if (!Column.IsEmpty()) Entry->SetStringField(TEXT("column"), Column);
		Entry->SetStringField(TEXT("error"), Message);
		Errors.Add(MakeShared<FJsonValueObject>(Entry));
	};
	TMap<FString, int32> FirstIndexOfCell;

	for (int32 Index = 0; Index < Cells->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* CellObj = nullptr;
		if (!(*Cells)[Index].IsValid() || !(*Cells)[Index]->TryGetObject(CellObj) || !CellObj || !CellObj->IsValid())
		{
			Reject(Index, FString(), FString(), FString::Printf(TEXT("cells[%d] must be an object {row, column, value}"), Index));
			continue;
		}
		FCell Cell;
		if (!(*CellObj)->TryGetStringField(TEXT("row"), Cell.Row) || Cell.Row.IsEmpty())
		{
			Reject(Index, FString(), FString(), FString::Printf(TEXT("cells[%d].row must be a non-empty string"), Index));
			continue;
		}
		if (!(*CellObj)->TryGetStringField(TEXT("column"), Cell.Column) || Cell.Column.IsEmpty())
		{
			Reject(Index, Cell.Row, FString(), FString::Printf(TEXT("cells[%d].column must be a non-empty string"), Index));
			continue;
		}
		Cell.Value = (*CellObj)->TryGetField(TEXT("value"));
		if (!Cell.Value.IsValid())
		{
			Reject(Index, Cell.Row, Cell.Column, FString::Printf(TEXT("cells[%d] has no 'value'"), Index));
			continue;
		}

		Cell.RowKey = FName(*Cell.Row);
		uint8* const* LiveRow = DataTable->GetRowMap().Find(Cell.RowKey);
		if (!LiveRow || !*LiveRow)
		{
			Reject(Index, Cell.Row, Cell.Column,
				FString::Printf(TEXT("Row not found: %s (use set_datatable_row to create it)"), *Cell.Row));
			continue;
		}
		for (FProperty* Prop : Props)
		{
			if (Prop->GetName() == Cell.Column || Prop->GetAuthoredName() == Cell.Column)
			{
				Cell.Prop = Prop;
				break;
			}
		}
		if (!Cell.Prop)
		{
			Reject(Index, Cell.Row, Cell.Column,
				FString::Printf(TEXT("row struct '%s' has no field '%s'"), *RowStruct->GetName(), *Cell.Column));
			continue;
		}
		// Two entries for one cell would make the write order decide the value.
		const FString CellKey = Cell.RowKey.ToString() + TEXT("\n") + Cell.Prop->GetName();
		if (const int32* First = FirstIndexOfCell.Find(CellKey))
		{
			Reject(Index, Cell.Row, Cell.Column, FString::Printf(
				TEXT("cells[%d] names the same cell as cells[%d]; keep one entry per cell"), Index, *First));
			continue;
		}
		FirstIndexOfCell.Add(CellKey, Index);

		int32 StagedIndex = INDEX_NONE;
		if (const int32* Existing = StagedIndexByRow.Find(Cell.RowKey))
		{
			StagedIndex = *Existing;
		}
		else
		{
			FStagedRow Row;
			Row.Live = *LiveRow;
			Row.Staged = AllocCopy(*LiveRow);
			StagedIndex = Staged.Add(MoveTemp(Row));
			StagedIndexByRow.Add(Cell.RowKey, StagedIndex);
		}
		FStagedRow& Row = Staged[StagedIndex];

		// The value parses onto the staged copy, so the table is untouched
		// however this turns out.
		FString SetError;
		if (!MCPJsonProperty::SetJsonOnProperty(
				Cell.Prop, Cell.Prop->ContainerPtrToValuePtr<void>(Row.Staged), Cell.Value, SetError))
		{
			Reject(Index, Cell.Row, Cell.Column,
				FString::Printf(TEXT("%s: %s"), *Cell.Prop->GetAuthoredName(), *SetError));
			continue;
		}
		Cell.Previous = FMCPJsonSerializer::SerializeValue(Cell.Prop->ContainerPtrToValuePtr<void>(Row.Live), Cell.Prop);
		Cell.bChanged = ExportField(Cell.Prop, Row.Staged) != ExportField(Cell.Prop, Row.Live);
		Row.bChanged |= Cell.bChanged;
		Row.CellIndices.Add(Parsed.Add(MoveTemp(Cell)));
	}

	if (Errors.Num() > 0)
	{
		FString FirstError;
		Errors[0]->AsObject()->TryGetStringField(TEXT("error"), FirstError);
		auto Rejected = MCPSuccess();
		Rejected->SetBoolField(TEXT("success"), false);
		Rejected->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d of %d cells failed validation; nothing was written. First failure: %s"),
			Errors.Num(), Cells->Num(), *FirstError));
		Rejected->SetStringField(TEXT("assetPath"), AssetPath);
		Rejected->SetBoolField(TEXT("dryRun"), bDryRun);
		Rejected->SetNumberField(TEXT("requestedCells"), Cells->Num());
		Rejected->SetNumberField(TEXT("failedCells"), Errors.Num());
		Rejected->SetArrayField(TEXT("errors"), Errors);
		return MCPResult(Rejected);
	}

	int32 ChangedCells = 0;
	TArray<TSharedPtr<FJsonValue>> Unchanged;
	for (const FCell& Cell : Parsed)
	{
		if (Cell.bChanged)
		{
			++ChangedCells;
			continue;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("row"), Cell.Row);
		Entry->SetStringField(TEXT("column"), Cell.Column);
		Unchanged.Add(MakeShared<FJsonValueObject>(Entry));
	}
	int32 RowsTouched = 0;
	for (const FStagedRow& Row : Staged) RowsTouched += Row.bChanged ? 1 : 0;

	auto Describe = [&](const TSharedPtr<FJsonObject>& Result)
	{
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetBoolField(TEXT("dryRun"), bDryRun);
		Result->SetNumberField(TEXT("requestedCells"), Parsed.Num());
		Result->SetNumberField(TEXT("changedCells"), ChangedCells);
		Result->SetNumberField(TEXT("unchangedCells"), Unchanged.Num());
		Result->SetNumberField(TEXT("rowsRequested"), Staged.Num());
		Result->SetNumberField(TEXT("rowsTouched"), RowsTouched);
		Result->SetNumberField(TEXT("rowCount"), DataTable->GetRowMap().Num());
		if (Unchanged.Num() > 0) Result->SetArrayField(TEXT("unchanged"), Unchanged);
	};

	if (bDryRun || ChangedCells == 0)
	{
		auto Result = MCPSuccess();
		Describe(Result);
		Result->SetBoolField(TEXT("updated"), false);
		if (!bDryRun)
		{
			// Every cell already held its value: an idempotent replay, with
			// nothing to verify against itself.
			Result->SetBoolField(TEXT("verified"), true);
		}
		return MCPResult(Result);
	}

	// ── Every row as text and as bytes before the write: the text is what the
	// comparison afterwards reads, the bytes are what a failed comparison
	// restores. ponytail: both are O(rows x fields) per call; a per-row hash
	// and touched-only backups are the upgrade once tables reach tens of
	// thousands of rows.
	TMap<FName, TArray<FString>> Before;
	Before.Reserve(DataTable->GetRowMap().Num());
	Backups.Reserve(DataTable->GetRowMap().Num());
	for (const TPair<FName, uint8*>& Pair : DataTable->GetRowMap())
	{
		if (!Pair.Value) continue;
		Before.Add(Pair.Key, ExportRow(Pair.Value));
		Backups.Add(Pair.Key, AllocCopy(Pair.Value));
	}

	// One transaction and one Modify. Copy all changed rows before running
	// their hooks, so each hook sees the complete batch (#929).
	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "SetDataTableCells", "Set DataTable cells"));
	DataTable->Modify();
	for (const FStagedRow& Row : Staged)
	{
		if (Row.bChanged) RowStruct->CopyScriptStruct(Row.Live, Row.Staged);
	}
	for (const TPair<FName, int32>& Pair : StagedIndexByRow)
	{
		if (Staged[Pair.Value].bChanged) DataTable->HandleDataTableChanged(Pair.Key);
	}
	DataTable->MarkPackageDirty();

	// ── Verify against the snapshot. A requested field must hold what was
	// asked for (#935); everything else must export the text it exported
	// before, on touched rows and untouched rows alike.
	FString Mismatch;
	if (DataTable->GetRowMap().Num() != Before.Num())
	{
		Mismatch = FString::Printf(TEXT("the table had %d rows before the write and %d after"),
			Before.Num(), DataTable->GetRowMap().Num());
	}
	for (const TPair<FName, uint8*>& Pair : DataTable->GetRowMap())
	{
		if (!Mismatch.IsEmpty()) break;
		const TArray<FString>* Prior = Before.Find(Pair.Key);
		if (!Prior || !Pair.Value)
		{
			Mismatch = FString::Printf(TEXT("row '%s' was not in the table before the write"), *Pair.Key.ToString());
			break;
		}
		const int32* StagedIndex = StagedIndexByRow.Find(Pair.Key);
		for (int32 PropIndex = 0; PropIndex < Props.Num() && Mismatch.IsEmpty(); ++PropIndex)
		{
			FProperty* Prop = Props[PropIndex];
			const FCell* Requested = nullptr;
			if (StagedIndex)
			{
				for (const int32 CellIndex : Staged[*StagedIndex].CellIndices)
				{
					if (Parsed[CellIndex].Prop == Prop)
					{
						Requested = &Parsed[CellIndex];
						break;
					}
				}
			}
			if (Requested)
			{
				FString Detail;
				if (!MCPJsonProperty::VerifyJsonOnProperty(
						Prop, Prop->ContainerPtrToValuePtr<void>(Pair.Value), Requested->Value, Detail))
				{
					Mismatch = FString::Printf(TEXT("field '%s' on row '%s' did not store the requested value: %s"),
						*Prop->GetAuthoredName(), *Requested->Row, *Detail);
				}
			}
			else if (ExportField(Prop, Pair.Value) != (*Prior)[PropIndex])
			{
				Mismatch = FString::Printf(TEXT("field '%s' on row '%s' changed although it was not requested (was %s, now %s)"),
					*Prop->GetAuthoredName(), *Pair.Key.ToString(), *(*Prior)[PropIndex], *ExportField(Prop, Pair.Value));
			}
		}
	}
	if (!Mismatch.IsEmpty())
	{
		for (const TPair<FName, uint8*>& Pair : Backups)
		{
			if (uint8* const* Live = DataTable->GetRowMap().Find(Pair.Key))
			{
				if (*Live) RowStruct->CopyScriptStruct(*Live, Pair.Value);
			}
		}
		for (const TPair<FName, int32>& Pair : StagedIndexByRow)
		{
			if (Staged[Pair.Value].bChanged) DataTable->HandleDataTableChanged(Pair.Key);
		}
		Transaction.Cancel();
		return MCPError(FString::Printf(
			TEXT("Verification failed after the write: %s. Every row was restored, the transaction was cancelled and nothing was saved."),
			*Mismatch));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Describe(Result);
	Result->SetBoolField(TEXT("verified"), true);
	if (bSave)
	{
		FString SaveReason;
		const bool bSaved = SaveAssetPackageChecked(DataTable, SaveReason);
		MCPNoteSaveOutcome(Result, DataTable->GetPathName(), bSaved, SaveReason);
	}
	else
	{
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetStringField(TEXT("saveNote"),
			TEXT("The package is dirty and the write is one undo step; pass save=true to write it to disk."));
	}

	// Self-inverse: the prior value of every cell that changed.
	TArray<TSharedPtr<FJsonValue>> Inverse;
	Inverse.Reserve(ChangedCells);
	for (const FCell& Cell : Parsed)
	{
		if (!Cell.bChanged) continue;
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("row"), Cell.Row);
		Entry->SetStringField(TEXT("column"), Cell.Column);
		Entry->SetField(TEXT("value"), Cell.Previous);
		Inverse.Add(MakeShared<FJsonValueObject>(Entry));
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetArrayField(TEXT("cells"), Inverse);
	Payload->SetBoolField(TEXT("save"), bSave);
	MCPSetRollback(Result, TEXT("set_datatable_cells"), Payload);
	return MCPResult(Result);
}

// #535: rename a row key, preserving its field values. Params: assetPath,
// oldName, newName.
TSharedPtr<FJsonValue> FAssetHandlers::RenameDataTableRow(const TSharedPtr<FJsonObject>& Params)
{
	// `rowName` is an alias of oldName, resolved by the registry (#1057).
	FString OldName;
	const TSharedPtr<FJsonValue> MissingOldName = RequireString(Params, TEXT("oldName"), OldName);
	FString NewName;
	const TSharedPtr<FJsonValue> MissingNewName = RequireString(Params, TEXT("newName"), NewName);
	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("DataTable"), AssetPath, DataTable)) return Err;
	if (MissingOldName) return MissingOldName;
	if (MissingNewName) return MissingNewName;

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
	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(DataTable, SaveReason);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	MCPNoteSaveOutcome(Result, DataTable->GetPathName(), bSaved, SaveReason);
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
	// Every parameter is read before anything can fail (#1057).
	const TSharedPtr<FJsonObject>* RowsObj = nullptr;
	const bool bHasRows = TryGetObjectParam(Params, TEXT("rows"), RowsObj);
	FString JsonStr;
	const bool bHasJsonString = TryGetStringParam(Params, TEXT("jsonString"), JsonStr) && !JsonStr.IsEmpty();

	FString AssetPath;
	UDataTable* DataTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("DataTable"), AssetPath, DataTable)) return Err;

	TSharedPtr<FJsonObject> ParsedRows;
	if (!bHasRows)
	{
		// Accept a jsonString carrying the {rowName: {fields}} object.
		if (bHasJsonString)
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
				const FString KeyString = MCPTextKeyToString(Key);
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
#if UE_MCP_HAS_5_5_API
		StringTable->GetMutableStringTable()->SetNamespace(FTextKey(TableNamespace));
#else
		// 5.4's FStringTable::SetNamespace takes the string itself.
		StringTable->GetMutableStringTable()->SetNamespace(TableNamespace);
#endif
	}
	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(StringTable, SaveReason);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	MCPNoteSaveOutcome(Result, StringTable->GetPathName(), bSaved, SaveReason);
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
	const FString KeyFilter = OptionalString(Params, TEXT("keyFilter"));
	FString AssetPath;
	UStringTable* StringTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("StringTable"), AssetPath, StringTable)) return Err;

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
	const FString KeyFilter = OptionalString(Params, TEXT("keyFilter"));
	FString AssetPath;
	UStringTable* StringTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("StringTable"), AssetPath, StringTable)) return Err;

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
	FString Key;
	const TSharedPtr<FJsonValue> MissingKey = RequireString(Params, TEXT("key"), Key);
	FString AssetPath;
	UStringTable* StringTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("StringTable"), AssetPath, StringTable)) return Err;
	if (MissingKey) return MissingKey;

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
	// Every parameter is read before anything can fail (#1057). `value` is a
	// second spelling of sourceString, declared on its own because the
	// category's `value` key is not string-typed.
	FString Key;
	const TSharedPtr<FJsonValue> MissingKey = RequireString(Params, TEXT("key"), Key);
	FString SourceString;
	bool bHasSourceString = TryGetStringParam(Params, TEXT("sourceString"), SourceString);
	FString ValueString;
	const bool bHasValueString = TryGetStringParam(Params, TEXT("value"), ValueString);
	if (!bHasSourceString && bHasValueString)
	{
		SourceString = ValueString;
		bHasSourceString = true;
	}

	FString AssetPath;
	UStringTable* StringTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("StringTable"), AssetPath, StringTable)) return Err;
	if (MissingKey) return MissingKey;

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
#if WITH_EDITORONLY_DATA && UE_MCP_HAS_5_8_API
	StringTable->GetMutableStringTable()->SetSourceString(EntryKey, SourceString, FString());
#else
	StringTable->GetMutableStringTable()->SetSourceString(EntryKey, SourceString);
#endif
	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(StringTable, SaveReason);

	auto Result = MCPSuccess();
	if (bExisted) MCPSetUpdated(Result); else MCPSetCreated(Result);
	MCPNoteSaveOutcome(Result, StringTable->GetPathName(), bSaved, SaveReason);
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
	FString Key;
	const TSharedPtr<FJsonValue> MissingKey = RequireString(Params, TEXT("key"), Key);
	FString AssetPath;
	UStringTable* StringTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("StringTable"), AssetPath, StringTable)) return Err;
	if (MissingKey) return MissingKey;

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
	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(StringTable, SaveReason);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	MCPNoteSaveOutcome(Result, StringTable->GetPathName(), bSaved, SaveReason);
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
	// filename and csvPath are aliases of filePath, resolved by the registry (#1057).
	FString FilePath;
	TryGetStringParam(Params, TEXT("filePath"), FilePath);
	FString AssetPath;
	UStringTable* StringTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("StringTable"), AssetPath, StringTable)) return Err;

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
			BeforeStrings.Add(MCPTextKeyToString(Key), SourceString);
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
	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(StringTable, SaveReason);

	TArray<TSharedPtr<FJsonValue>> Entries;
	TArray<TSharedPtr<FJsonValue>> Keys;
	const int32 AfterCount = AppendStringTableEntries(StringTable, TEXT(""), Entries, Keys, false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	SetStringTableInfoFields(Result, StringTable);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("filePath"), FilePath);
	MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveReason);
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
			const FString KeyString = MCPTextKeyToString(Key);
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
				Out.Add(MCPTextKeyToString(Key), SourceString);
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

	// Every parameter is read before anything can fail (#1057). `filePath` is
	// an alias of csvPath, resolved by the registry.
	FString CsvPath;
	const TSharedPtr<FJsonValue> MissingCsvPath = RequireString(Params, TEXT("csvPath"), CsvPath);
	const bool bReplaceExisting = OptionalBool(Params, TEXT("replaceExisting"), false);
	const bool bRequireExactKeys = OptionalBool(Params, TEXT("requireExactKeys"), false);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	TArray<FString> ExpectedKeys;
	const TArray<TSharedPtr<FJsonValue>>* ExpectedField = nullptr;
	if (TryGetArrayParam(Params, TEXT("expectedKeys"), ExpectedField) && ExpectedField)
	{
		ExpectedKeys = JsonArrayToStringList(ExpectedField);
	}

	FString AssetPath;
	UStringTable* StringTable = nullptr;
	if (auto Err = MCPImportLoadTypedAsset(Params, TEXT("StringTable"), AssetPath, StringTable)) return Err;

	if (MCPIsProtectedAssetPath(AssetPath))
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is on a protected mount (/Engine/, /Script/, /Memory/, /Temp/), which the bridge never writes to."),
			*AssetPath));
	}

	if (MissingCsvPath) return MissingCsvPath;
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
			TSharedPtr<FJsonObject> Obj = MCPErrorObject(FString::Printf(
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

	// The engine importer resets the table before reading the CSV, so a merge
	// puts back every earlier entry the CSV does not carry.
	TArray<FString> RemovedKeys;
	for (const TPair<FString, FString>& Entry : Before)
	{
		if (Incoming.Contains(Entry.Key)) continue;
		if (bReplaceExisting)
		{
			StringTable->GetMutableStringTable()->RemoveSourceString(FTextKey(Entry.Key));
			RemovedKeys.Add(Entry.Key);
		}
		else
		{
#if WITH_EDITORONLY_DATA && UE_MCP_HAS_5_8_API
			StringTable->GetMutableStringTable()->SetSourceString(FTextKey(Entry.Key), Entry.Value, FString());
#else
			StringTable->GetMutableStringTable()->SetSourceString(FTextKey(Entry.Key), Entry.Value);
#endif
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
		FString SaveError;
		bPersisted = SaveAssetPackageChecked(StringTable, SaveError);
		if (!bPersisted)
		{
			PersistReason = FString::Printf(
				TEXT("'%s' was not written: %s The entries are in memory only."),
				Package ? *Package->GetName() : *AssetPath, *SaveError);
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

namespace
{
	/** The texture settings a reimport must not change: how the texture is
	 *  compressed, grouped, streamed, sized, mipped, filtered and addressed.
	 *  Names the class does not declare (AddressX on a cube) are skipped. */
	const TCHAR* const GMCPReimportTextureSettings[] = {
		TEXT("CompressionSettings"), TEXT("LODGroup"), TEXT("SRGB"), TEXT("NeverStream"),
		TEXT("MaxTextureSize"), TEXT("MipGenSettings"), TEXT("Filter"), TEXT("AddressX"), TEXT("AddressY"),
		TEXT("CompressionNoAlpha"), TEXT("CompressionQuality"), TEXT("LossyCompressionAmount"),
		TEXT("VirtualTextureStreaming"), TEXT("PowerOfTwoMode"), TEXT("bFlipGreenChannel"),
	};

	/** Each listed setting the texture's class declares, as export text. */
	TMap<FString, FString> MCPReimportSnapshotTextureSettings(UTexture* Texture)
	{
		TMap<FString, FString> Out;
		for (const TCHAR* Name : GMCPReimportTextureSettings)
		{
			const FProperty* Property = Texture->GetClass()->FindPropertyByName(Name);
			if (!Property) continue;
			FString Text;
			// ExportText_InContainer skips a value equal to zero (false, 0, the first
			// enum entry), which is exactly what a reimport tends to flip.
			Property->ExportTextItem_InContainer(Text, Texture, nullptr, Texture, PPF_None);
			Out.Add(Name, Text);
		}
		return Out;
	}

	TSharedPtr<FJsonObject> MCPReimportSettingsJson(const TMap<FString, FString>& Settings)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Pair : Settings) Obj->SetStringField(Pair.Key, Pair.Value);
		return Obj;
	}

	TSharedPtr<FJsonObject> MCPReimportSourceSize(UTexture* Texture)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), Texture->Source.GetSizeX());
		Obj->SetNumberField(TEXT("y"), Texture->Source.GetSizeY());
		return Obj;
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::ReimportAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	// Read before anything can fail (#1057). `filename` is an alias of filePath.
	FString NewSourcePath;
	const bool bHasNewSourcePath = TryGetStringParam(Params, TEXT("filePath"), NewSourcePath);
	const bool bPreserveSettings = OptionalBool(Params, TEXT("preserveSettings"), true);
	const bool bSave = OptionalBool(Params, TEXT("save"), false);

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;

	// An asset nothing can reimport is refused here rather than handed to the
	// manager. FReimportManager::Reimport on an asset with no registered handler
	// - a Blueprint, say - does not return promptly: it wedges the game thread,
	// so the bridge stops answering and the caller sees a timeout rather than an
	// answer about their asset. CanReimport is the question actually being asked.
	if (!FReimportManager::Instance()->CanReimport(Asset))
	{
		return MCPError(FString::Printf(
			TEXT("Nothing can reimport a %s: '%s' has no registered reimport handler. ")
			TEXT("Reimport rebuilds an asset from the file it was imported from, so it only applies to ")
			TEXT("imported assets - a Blueprint or any other asset authored in the editor has no such file."),
			*Asset->GetClass()->GetName(), *AssetPath));
	}

	// Optionally override the source file path
	bool bSourceFileUpdated = false;
	if (bHasNewSourcePath)
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

		if (!ImportData)
		{
			// #1008: a filePath that lands nowhere used to be dropped here, and the
			// reimport below then re-read the file the asset was ORIGINALLY imported
			// from and reported success. The caller was told their new source file
			// had been used when it had not been read at all.
			return MCPError(FString::Printf(
				TEXT("%s carries no AssetImportData, so '%s' cannot be recorded as its source file. ")
				TEXT("Reimporting anyway would have re-read the file this asset was originally imported from ")
				TEXT("and reported success. Import it as a new asset instead, or call reimport without filePath ")
				TEXT("to rebuild it from the source it already has."),
				*Asset->GetClass()->GetName(), *NewSourcePath));
		}
		ImportData->Update(NewSourcePath);
		bSourceFileUpdated = true;
	}

	// A texture's settings are the caller's, not the source file's. Whether a
	// reimport keeps them depends on the import path (the legacy factory copies
	// them across, an Interchange pipeline may apply its own), so they are read
	// before and put back after rather than trusted to survive.
	UTexture* Texture = Cast<UTexture>(Asset);
	TMap<FString, FString> SettingsBefore;
	TSharedPtr<FJsonObject> SizeBefore;
	if (Texture)
	{
		SettingsBefore = MCPReimportSnapshotTextureSettings(Texture);
		SizeBefore = MCPReimportSourceSize(Texture);
	}

	// Use FReimportManager to reimport
	bool bSuccess = FReimportManager::Instance()->Reimport(Asset, /*bAskForNewFileIfMissing=*/false, /*bShowNotification=*/false);

	auto Result = MCPSuccess();
	if (bSuccess) MCPSetUpdated(Result);
	if (Texture)
	{
		TArray<TSharedPtr<FJsonValue>> Restored;
		if (bSuccess && bPreserveSettings)
		{
			const TMap<FString, FString> SettingsReimported = MCPReimportSnapshotTextureSettings(Texture);
			bool bChanged = false;
			for (const TPair<FString, FString>& Pair : SettingsBefore)
			{
				const FString* Now = SettingsReimported.Find(Pair.Key);
				if (Now && *Now == Pair.Value) continue;
				FProperty* Property = Texture->GetClass()->FindPropertyByName(*Pair.Key);
				if (!Property) continue;
				if (!bChanged) Texture->PreEditChange(nullptr);
				bChanged = true;
				Property->ImportText_InContainer(*Pair.Value, Texture, Texture, PPF_None);
				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("name"), Pair.Key);
				Row->SetStringField(TEXT("reimportedValue"), Now ? *Now : FString());
				Row->SetStringField(TEXT("restoredValue"), Pair.Value);
				Restored.Add(MakeShared<FJsonValueObject>(Row));
			}
			if (bChanged)
			{
				// Rebuilds the platform data under the restored settings.
				Texture->PostEditChange();
				Texture->MarkPackageDirty();
			}
		}
		Result->SetBoolField(TEXT("preserveSettings"), bPreserveSettings);
		Result->SetObjectField(TEXT("settingsBefore"), MCPReimportSettingsJson(SettingsBefore));
		Result->SetObjectField(TEXT("settingsAfter"), MCPReimportSettingsJson(MCPReimportSnapshotTextureSettings(Texture)));
		Result->SetArrayField(TEXT("restoredSettings"), Restored);
		Result->SetObjectField(TEXT("sourceSizeBefore"), SizeBefore);
		Result->SetObjectField(TEXT("sourceSizeAfter"), MCPReimportSourceSize(Texture));
	}
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
	Result->SetBoolField(TEXT("success"), bSuccess);
	// Whether this call changed which file the asset reimports from, so a
	// caller can tell "rebuilt from the same source" from "repointed and
	// rebuilt" without inferring it from what it passed.
	Result->SetBoolField(TEXT("sourceFileUpdated"), bSourceFileUpdated);
	if (bSourceFileUpdated) Result->SetStringField(TEXT("sourceFile"), NewSourcePath);
	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), TEXT("Reimport failed -- check that the asset has a valid source file"));
	}
	else if (bSave)
	{
		// A reimport leaves the package dirty and unsaved, which is the default:
		// the caller looks before it keeps. save writes this one package only.
		FString SaveReason;
		const bool bSaved = SaveAssetPackageChecked(Asset, SaveReason);
		MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveReason);
	}
	else
	{
		Result->SetBoolField(TEXT("saved"), false);
	}
	if (UPackage* Package = Asset->GetOutermost()) Result->SetBoolField(TEXT("packageDirty"), Package->IsDirty());
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("A reimport rebuilds the asset from its source file in place, so the content it replaced is gone and the new ")
		TEXT("content does not carry it. Reimporting again re-reads the same file rather than restoring the previous build, ")
		TEXT("and deleting the asset would destroy it rather than undo the call. There is no inverse action."));

	return MCPResult(Result);
}

// --- Socket Handlers --------------------------------------------------


// ---------------------------------------------------------------------------
// export_asset - Export an asset to disk (DataTable → JSON/CSV, Texture2D → PNG, etc.)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::ExportAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FString OutputPath;
	if (auto Err = RequireString(Params, TEXT("outputPath"), OutputPath)) return Err;
	// Read before anything can fail (#1057); only a DataTable export uses it.
	const FString RequestedFormat = OptionalString(Params, TEXT("format")).ToLower();
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bReplaceExisting = OptionalBool(Params, TEXT("replaceExisting"), true);

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

	// CompositeDataTable derives from DataTable; its merged rows use the same
	// serializers. Neither table class needs a registered UExporter.
	UDataTable* DataTable = Cast<UDataTable>(Asset);
	FString TableFormat;
	if (DataTable)
	{
		const FString Extension = FPaths::GetExtension(AbsOutputPath).ToLower();
		if (Extension != TEXT("json") && Extension != TEXT("csv"))
		{
			return MCPError(TEXT("DataTable export outputPath must have a .json or .csv extension"));
		}
		if (!DataTable->GetRowStruct()) return MCPError(TEXT("DataTable has no row struct"));
		TableFormat = RequestedFormat;
		if (TableFormat.IsEmpty())
		{
			TableFormat = Extension;
		}
		if (TableFormat != TEXT("json") && TableFormat != TEXT("csv"))
		{
			return MCPError(TEXT("DataTable export format must be 'json' or 'csv'"));
		}
	}

	TSharedPtr<FJsonObject> InstanceData;
	// A .json outputPath (or format=json) writes the resolved parameters; any
	// other extension keeps the registered exporter (T3D, COPY).
	if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Asset))
	{
		const bool bJsonExtension = FPaths::GetExtension(AbsOutputPath).Equals(TEXT("json"), ESearchCase::IgnoreCase);
		if (RequestedFormat == TEXT("json") && !bJsonExtension)
			return MCPError(TEXT("MaterialInstance format=json requires a .json outputPath"));
		if (bJsonExtension) InstanceData = FMaterialHandlers::DescribeMaterialInstance(Instance);
	}

	// Whether the output file was already there decides whether this call
	// created one or overwrote one. bReplaceIdentical below is true, so
	// reporting "created" unconditionally would call every overwrite a create.
	const bool bOutputExisted = IFileManager::Get().FileExists(*AbsOutputPath);

	if (bOutputExisted && !bReplaceExisting) return MCPError(TEXT("Output file exists and replaceExisting is false: ") + AbsOutputPath);
	if (bDryRun)
	{
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("dryRun"), true);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetStringField(TEXT("outputPath"), AbsOutputPath);
		Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
		Result->SetBoolField(TEXT("wouldOverwrite"), bOutputExisted);
		return MCPResult(Result);
	}

	// Create parent directory if needed
	FString OutputDir = FPaths::GetPath(AbsOutputPath);
	if (!OutputDir.IsEmpty())
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*OutputDir);
	}

	if (InstanceData.IsValid())
	{
		FString Contents;
		const auto Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Contents);
		if (!FJsonSerializer::Serialize(InstanceData.ToSharedRef(), Writer) || !FFileHelper::SaveStringToFile(Contents, *AbsOutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			return MCPError(TEXT("Failed to write material parameter JSON: ") + AbsOutputPath);
	}
	else if (DataTable)
	{
		// Match DataTableFunctionLibrary's file exports, including structured
		// JSON for nested structs and UTF-8 without a BOM.
		const FString Contents = TableFormat == TEXT("csv")
			? DataTable->GetTableAsCSV()
			: DataTable->GetTableAsJSON(EDataTableExportFlags::UseJsonObjectsForStructs);
		if (!FFileHelper::SaveStringToFile(Contents, *AbsOutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			return MCPError(FString::Printf(TEXT("Failed to write DataTable export for '%s' to '%s'"), *AssetPath, *AbsOutputPath));
		}
	}
	else
	{
		// The skeletal mesh exporter reads the render-side mesh object, which a -nullrhi editor
		// never builds; the engine asserts on it instead of failing the export.
		if (!FApp::CanEverRender() && Asset->IsA<USkeletalMesh>())
		{
			return MCPError(FString::Printf(TEXT("Exporting skeletal mesh '%s' needs an editor that renders; this one runs with -nullrhi. Use -RenderOffScreen for a windowless editor that can export it."), *AssetPath));
		}

		// Use UE's AssetExportTask - same as unreal.AssetExportTask in Python
		UAssetExportTask* ExportTask = NewObject<UAssetExportTask>();
		FGCRootScope ExportTaskRoot(ExportTask);
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
	}

	auto Result = MCPSuccess();
	if (bOutputExisted) MCPSetUpdated(Result); else MCPSetCreated(Result);
	Result->SetBoolField(TEXT("overwroteExistingFile"), bOutputExisted);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("outputPath"), AbsOutputPath);
	Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
	if (DataTable)
	{
		Result->SetStringField(TEXT("format"), TableFormat);
		Result->SetNumberField(TEXT("rowCount"), DataTable->GetRowMap().Num());
	}
	Result->SetNumberField(TEXT("bytes"), IFileManager::Get().FileSize(*AbsOutputPath));
	if (InstanceData.IsValid())
	{
		Result->SetStringField(TEXT("format"), TEXT("json"));
		Result->SetObjectField(TEXT("readBack"), InstanceData);
	}
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
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString OutputPath;
	if (auto Err = RequireString(Params, TEXT("outputPath"), OutputPath)) return Err;

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
	if (auto Err = RequireString(Params, TEXT("assetPathA"), PathA)) return Err;
	if (auto Err = RequireString(Params, TEXT("assetPathB"), PathB)) return Err;

	UTexture2D* A = LoadAssetByPath<UTexture2D>(PathA);
	UTexture2D* B = LoadAssetByPath<UTexture2D>(PathB);
	if (!A) return MCPAssetLoadError(PathA, TEXT("Texture2D"));
	if (!B) return MCPAssetLoadError(PathB, TEXT("Texture2D"));

	const bool bSameDims = A->GetSizeX() == B->GetSizeX() && A->GetSizeY() == B->GetSizeY();
	const bool bSameFormat = A->GetPixelFormat() == B->GetPixelFormat();
#if WITH_EDITORONLY_DATA
#if UE_MCP_HAS_5_5_API
	const FString IdA = A->Source.GetIdString();
	const FString IdB = B->Source.GetIdString();
#else
	// 5.4 declares GetIdString() without exporting it, so it links nowhere
	// outside Engine. GetId() is exported and is the value that string is made
	// of: the source hash plus the source attributes.
	const FString IdA = A->Source.GetId().ToString();
	const FString IdB = B->Source.GetId().ToString();
#endif
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
