// Split from AssetHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FAssetHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in AssetHandlers.cpp::RegisterHandlers.

#include "AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "HandlerJsonProperty.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetImportTask.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/FbxAnimSequenceImportData.h"
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
		UObject* Asset = UEditorAssetLibrary::LoadAsset(OutAssetPath);
		OutTable = Cast<UCurveTable>(Asset);
		if (!OutTable)
		{
			return MCPError(FString::Printf(TEXT("Asset is not a CurveTable: %s"), *OutAssetPath));
		}
		return nullptr;
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

	UFbxFactory* FbxFactory = NewObject<UFbxFactory>();
	FGCRootScope FactoryRoot(FbxFactory);

	UFbxImportUI* ImportUI = NewObject<UFbxImportUI>();
	ImportUI->bImportMesh = true;
	ImportUI->bImportAnimations = false;
	ImportUI->bImportMaterials = true;
	ImportUI->bImportTextures = true;
	ImportUI->bIsObjImport = false;
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
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetArrayField(TEXT("importedAssets"), ImportedPaths);
	Result->SetNumberField(TEXT("importedCount"), ImportedPaths.Num());
	Result->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0);
	if (ImportedPaths.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("Import task completed but no assets were produced"));
	}

	// Rollback only when a single asset was produced (paired inverse: delete_asset).
	if (ImportedPaths.Num() == 1)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ImportedPaths[0]->AsString());
		MCPSetRollback(Result, TEXT("delete_asset"), Payload);
	}

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
		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
		if (Skeleton)
		{
			ImportUI->Skeleton = Skeleton;
		}
		else
		{
			auto Result = MCPSuccess();
			Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("Skeleton not found: %s, importing without skeleton target"), *SkeletonPath));
			// Continue with import — don't return here, just note the warning
		}
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
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetArrayField(TEXT("importedAssets"), ImportedPaths);
	Result->SetNumberField(TEXT("importedCount"), ImportedPaths.Num());
	Result->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0);
	if (ImportedPaths.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("Import task completed but no assets were produced"));
	}

	if (ImportedPaths.Num() == 1)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ImportedPaths[0]->AsString());
		MCPSetRollback(Result, TEXT("delete_asset"), Payload);
	}

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

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
	{
		return MCPError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));
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

	if (ImportedPaths.Num() == 1)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ImportedPaths[0]->AsString());
		MCPSetRollback(Result, TEXT("delete_asset"), Payload);
	}

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

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	UTexture2D* Texture = Cast<UTexture2D>(Asset);
	if (!Texture)
	{
		return MCPError(FString::Printf(TEXT("Asset is not a Texture2D: %s"), *AssetPath));
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

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	UTexture2D* Texture = Cast<UTexture2D>(Asset);
	if (!Texture)
	{
		return MCPError(FString::Printf(TEXT("Asset is not a Texture2D: %s"), *AssetPath));
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

	// Self-inverse rollback. We store enum values as numeric strings for the
	// inverse call; the handler accepts strings so we'd lose the mapping back
	// to string keys. For safety, emit rollback only when simple bool props
	// changed — compression/LOD group changes are not reversed here.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	bool bHaveReversibleField = false;
	for (const FString& P : ModifiedProperties)
	{
		if (P == TEXT("sRGB"))
		{
			Payload->SetBoolField(TEXT("sRGB"), PrevSRGB);
			bHaveReversibleField = true;
		}
		else if (P == TEXT("neverStream"))
		{
			Payload->SetBoolField(TEXT("neverStream"), PrevNeverStream);
			bHaveReversibleField = true;
		}
	}
	// Suppress unused-variable warnings on the enum captures when no
	// reversible fields matched.
	(void)PrevCompression; (void)PrevLODGroup;
	if (bHaveReversibleField)
	{
		MCPSetRollback(Result, TEXT("set_texture_properties"), Payload);
	}

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
	MCPSetCreated(Result);
	Result->SetNumberField(TEXT("requested"), Items->Num());
	Result->SetNumberField(TEXT("imported"), Imported);
	Result->SetNumberField(TEXT("failed"), Items->Num() - Imported);

	TArray<TSharedPtr<FJsonValue>> RecArray;
	for (const TSharedPtr<FJsonObject>& Rec : ItemRecords)
	{
		RecArray.Add(MakeShared<FJsonValueObject>(Rec));
	}
	Result->SetArrayField(TEXT("items"), RecArray);
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

	if (ImportedPaths.Num() == 1)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ImportedPaths[0]->AsString());
		MCPSetRollback(Result, TEXT("delete_asset"), Payload);
	}

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
				if (UCurveTable* Existing = LoadObject<UCurveTable>(nullptr, *ExistingAssetPath))
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
		return MCPError(FString::Printf(TEXT("Unknown interpMode '%s'. Use linear, constant, or cubic."), *InterpRaw));
	}

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
		return MCPResult(Result);
	}

	SaveCurveTableChange(Table);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("format"), Format);
	Result->SetStringField(TEXT("curveType"), CurveTableModeName(Table->GetCurveTableMode()));
	Result->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());
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
		return MCPError(FString::Printf(TEXT("Unknown interpMode '%s'. Use linear, constant, or cubic."), *InterpRaw));
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

	Table->RemoveRow(RowKey);
	SaveCurveTableChange(Table);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("rowName"), RowName);
	Result->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());
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
				if (!ParseCurveInterpMode(InterpRaw, Parsed)) return MCPError(FString::Printf(TEXT("Unknown interpMode '%s'."), *InterpRaw));
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
				return MCPError(FString::Printf(TEXT("Unknown interpMode '%s'."), *InterpRaw));
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
		return MCPError(FString::Printf(TEXT("Unknown interpMode '%s'. Use linear, constant, or cubic."), *InterpRaw));
	}
	const float Tolerance = (float)OptionalNumber(Params, TEXT("keyTimeTolerance"), UE_KINDA_SMALL_NUMBER);

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
	ScriptStruct = LoadObject<UScriptStruct>(nullptr, *RowStruct);
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
				if (UDataTable* Existing = LoadObject<UDataTable>(nullptr, *ExistingAssetPath))
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


TSharedPtr<FJsonValue> FAssetHandlers::ReadDataTable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	UDataTable* DataTable = Cast<UDataTable>(Asset);
	if (!DataTable)
	{
		return MCPError(FString::Printf(TEXT("Asset is not a DataTable: %s"), *AssetPath));
	}

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
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	UDataTable* DataTable = Cast<UDataTable>(Asset);
	if (!DataTable)
	{
		return MCPError(FString::Printf(TEXT("Asset is not a DataTable: %s"), *AssetPath));
	}

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
		return MCPResult(ErrResult);
	}

	DataTable->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("rowCount"), DataTable->GetRowMap().Num());
	Result->SetStringField(TEXT("message"), TEXT("DataTable reimported successfully from JSON"));
	// No rollback: destructive/external — reimport replaces table contents.

	return MCPResult(Result);
}

// #437: single-row mutation. Append a new row or overwrite the existing one
// without round-tripping the whole table through JSON.
// Params: assetPath, rowName, row (JSON object with row-struct fields).
TSharedPtr<FJsonValue> FAssetHandlers::SetDataTableRow(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
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

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UDataTable* DataTable = Cast<UDataTable>(Asset);
	if (!DataTable)
	{
		return MCPError(FString::Printf(TEXT("Asset is not a DataTable: %s"), *AssetPath));
	}
	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct)
	{
		return MCPError(TEXT("DataTable has no row struct"));
	}

	const FName RowKey(*RowName);
	const TMap<FName, uint8*>& RowMap = DataTable->GetRowMap();
	const bool bExisted = RowMap.Contains(RowKey);

	// Snapshot the prior row (if any) for rollback / idempotency.
	FString PrevExport;
	if (bExisted)
	{
		uint8* PrevPtr = *RowMap.Find(RowKey);
		RowStruct->ExportText(PrevExport, PrevPtr, PrevPtr, nullptr, PPF_None, nullptr);
	}

	// Allocate a row buffer and apply fields via MCPJsonProperty so dicts/
	// arrays/asset paths/gameplay tags all work.
	const int32 StructSize = RowStruct->GetStructureSize();
	const int32 MinAlign = RowStruct->GetMinAlignment();
	uint8* NewRow = (uint8*)FMemory::Malloc(StructSize, MinAlign);
	RowStruct->InitializeStruct(NewRow);

	// Seed from the prior row so partial JSON only updates the named fields.
	if (bExisted)
	{
		uint8* PrevPtr = *RowMap.Find(RowKey);
		RowStruct->CopyScriptStruct(NewRow, PrevPtr);
	}

	FString SetErr;
	bool bOk = true;
	for (const auto& Pair : (*RowObj)->Values)
	{
		FProperty* FieldProp = nullptr;
		for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
		{
			if (It->GetName() == Pair.Key || It->GetAuthoredName() == Pair.Key)
			{
				FieldProp = *It;
				break;
			}
		}
		if (!FieldProp)
		{
			SetErr = FString::Printf(TEXT("row struct '%s' has no field '%s'"), *RowStruct->GetName(), *Pair.Key);
			bOk = false;
			break;
		}
		void* FieldAddr = FieldProp->ContainerPtrToValuePtr<void>(NewRow);
		FString E;
		if (!MCPJsonProperty::SetJsonOnProperty(FieldProp, FieldAddr, Pair.Value, E))
		{
			SetErr = FString::Printf(TEXT("%s: %s"), *Pair.Key, *E);
			bOk = false;
			break;
		}
	}
	if (!bOk)
	{
		RowStruct->DestroyStruct(NewRow);
		FMemory::Free(NewRow);
		return MCPError(SetErr);
	}

	// AddRow takes the struct buffer ownership (copies, manages lifetime).
	DataTable->RemoveRow(RowKey);
	DataTable->AddRow(RowKey, *reinterpret_cast<FTableRowBase*>(NewRow));
	RowStruct->DestroyStruct(NewRow);
	FMemory::Free(NewRow);

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
		Payload->SetStringField(TEXT("rowExport"), PrevExport);
		MCPSetRollback(Result, TEXT("set_datatable_row_raw"), Payload);
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
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString RowName;
	if (auto Err = RequireString(Params, TEXT("rowName"), RowName)) return Err;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UDataTable* DataTable = Cast<UDataTable>(Asset);
	if (!DataTable)
	{
		return MCPError(FString::Printf(TEXT("Asset is not a DataTable: %s"), *AssetPath));
	}
	const FName RowKey(*RowName);
	if (!DataTable->GetRowMap().Contains(RowKey))
	{
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("rowName"), RowName);
		return MCPResult(Noop);
	}

	DataTable->RemoveRow(RowKey);
	DataTable->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(DataTable, /*bOnlyIfIsDirty*/ true);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("rowName"), RowName);
	Result->SetNumberField(TEXT("rowCount"), DataTable->GetRowMap().Num());
	return MCPResult(Result);
}

// #535: read a single row's fields without dumping the whole table.
// Params: assetPath, rowName.
TSharedPtr<FJsonValue> FAssetHandlers::GetDataTableRow(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString RowName;
	if (auto Err = RequireString(Params, TEXT("rowName"), RowName)) return Err;

	UDataTable* DataTable = Cast<UDataTable>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!DataTable) return MCPError(FString::Printf(TEXT("Asset is not a DataTable: %s"), *AssetPath));
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
// upsert (SetDataTableRow), which seeds from the existing row so only the named
// cell changes. Params: assetPath, rowName, fieldName, value.
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
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString RowName;
	if (auto Err = RequireString(Params, TEXT("rowName"), RowName)) return Err;
	UDataTable* DataTable = Cast<UDataTable>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!DataTable) return MCPError(FString::Printf(TEXT("Asset is not a DataTable: %s"), *AssetPath));
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
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString OldName;
	if (auto Err = RequireStringAlt(Params, TEXT("oldName"), TEXT("rowName"), OldName)) return Err;
	FString NewName;
	if (auto Err = RequireString(Params, TEXT("newName"), NewName)) return Err;

	UDataTable* DataTable = Cast<UDataTable>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!DataTable) return MCPError(FString::Printf(TEXT("Asset is not a DataTable: %s"), *AssetPath));
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
	return MCPResult(Result);
}

// #535: bulk-upsert rows from a JSON object ({rowName: {field: value, ...}})
// without touching unrelated rows (unlike reimport_datatable, which replaces
// the whole table). Params: assetPath, rows (object) or jsonString.
TSharedPtr<FJsonValue> FAssetHandlers::FillDataTableFromJson(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

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

	UDataTable* DataTable = Cast<UDataTable>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!DataTable) return MCPError(FString::Printf(TEXT("Asset is not a DataTable: %s"), *AssetPath));
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

		UObject* Asset = UEditorAssetLibrary::LoadAsset(OutAssetPath);
		if (!Asset)
		{
			return MCPError(FString::Printf(TEXT("Asset not found: %s"), *OutAssetPath));
		}

		OutStringTable = Cast<UStringTable>(Asset);
		if (!OutStringTable)
		{
			return MCPError(FString::Printf(TEXT("Asset is not a StringTable: %s"), *OutAssetPath));
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
				if (UStringTable* Existing = LoadObject<UStringTable>(nullptr, *ExistingAssetPath))
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
	StringTable->GetMutableStringTable()->SetSourceString(EntryKey, SourceString);
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

	StringTable->Modify(true);
	const bool bImported = StringTable->GetMutableStringTable()->ImportStrings(FilePath);
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
	return MCPResult(Result);
}

// --- Reimport ---------------------------------------------------------


// --- Reimport ---------------------------------------------------------

TSharedPtr<FJsonValue> FAssetHandlers::ReimportAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

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
	// No rollback: destructive/external — reimport pulls fresh from source file.

	return MCPResult(Result);
}

// --- Socket Handlers --------------------------------------------------


// ---------------------------------------------------------------------------
// export_asset — Export an asset to disk (e.g. Texture2D → PNG, StaticMesh → FBX)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::ExportAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString OutputPath;
	if (auto Err = RequireString(Params, TEXT("outputPath"), OutputPath)) return Err;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	// Create parent directory if needed
	FString OutputDir = FPaths::GetPath(OutputPath);
	if (!OutputDir.IsEmpty())
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*OutputDir);
	}

	// Use UE's AssetExportTask — same as unreal.AssetExportTask in Python
	UAssetExportTask* ExportTask = NewObject<UAssetExportTask>();
	ExportTask->Object = Asset;
	ExportTask->Filename = OutputPath;
	ExportTask->bAutomated = true;
	ExportTask->bPrompt = false;
	ExportTask->bReplaceIdentical = true;

	bool bSuccess = UExporter::RunAssetExportTask(ExportTask);

	if (!bSuccess)
	{
		return MCPError(FString::Printf(TEXT("Export failed for '%s' to '%s'. The asset type may not have a registered exporter."), *AssetPath, *OutputPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("outputPath"), OutputPath);
	Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
	return MCPResult(Result);
}

// ─── #150 asset(get_referencers) ────────────────────────────────────
// Reverse dependency lookup per package. Feeds the common "what uses this
// texture / material?" question without dropping into Python.
