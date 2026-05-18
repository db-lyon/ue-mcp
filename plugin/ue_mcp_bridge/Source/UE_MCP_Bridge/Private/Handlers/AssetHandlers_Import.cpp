// Split from AssetHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FAssetHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in AssetHandlers.cpp::RegisterHandlers.

#include "AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
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
#include "Exporters/Exporter.h"
#include "AssetExportTask.h"


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
