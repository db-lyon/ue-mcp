#include "AssetHandlers.h"
#include "HandlerRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/TopLevelAssetPath.h"

// DataTable
#include "Engine/DataTable.h"
#include "Kismet/DataTableFunctionLibrary.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// Import tasks
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

// FBX
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"

// Texture
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"

void FAssetHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_assets"), &ListAssets);
	Registry.RegisterHandler(TEXT("search_assets"), &SearchAssets);
	Registry.RegisterHandler(TEXT("read_asset"), &ReadAsset);
	Registry.RegisterHandler(TEXT("read_asset_properties"), &ReadAssetProperties);
	Registry.RegisterHandler(TEXT("duplicate_asset"), &DuplicateAsset);
	Registry.RegisterHandler(TEXT("rename_asset"), &RenameAsset);
	Registry.RegisterHandler(TEXT("move_asset"), &MoveAsset);
	Registry.RegisterHandler(TEXT("delete_asset"), &DeleteAsset);
	Registry.RegisterHandler(TEXT("save_asset"), &SaveAsset);
	Registry.RegisterHandler(TEXT("list_textures"), &ListTextures);

	// DataTable handlers
	Registry.RegisterHandler(TEXT("import_datatable_json"), &ImportDataTableJson);
	Registry.RegisterHandler(TEXT("export_datatable_json"), &ExportDataTableJson);

	// FBX import handlers
	Registry.RegisterHandler(TEXT("import_static_mesh"), &ImportStaticMesh);
	Registry.RegisterHandler(TEXT("import_skeletal_mesh"), &ImportSkeletalMesh);
	Registry.RegisterHandler(TEXT("import_animation"), &ImportAnimation);

	// Texture handlers
	Registry.RegisterHandler(TEXT("list_texture_properties"), &ListTextureProperties);
	Registry.RegisterHandler(TEXT("set_texture_properties"), &SetTextureProperties);
	Registry.RegisterHandler(TEXT("import_texture"), &ImportTexture);
}

TSharedPtr<FJsonValue> FAssetHandlers::ListAssets(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Query = TEXT("*");
	Params->TryGetStringField(TEXT("query"), Query);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAllAssets(AssetDataList);

	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	for (const FAssetData& AssetData : AssetDataList)
	{
		FString AssetPath = AssetData.ObjectPath.ToString();
		if (Query == TEXT("*") || AssetPath.Contains(Query))
		{
			TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
			AssetObj->SetStringField(TEXT("path"), AssetPath);
			AssetObj->SetStringField(TEXT("className"), AssetData.AssetClass.ToString());
			AssetObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
			AssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
		}
	}

	Result->SetArrayField(TEXT("assets"), AssetsArray);
	Result->SetNumberField(TEXT("count"), AssetsArray.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::SearchAssets(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Query;
	Params->TryGetStringField(TEXT("query"), Query);
	FString Directory = TEXT("/Game/");
	Params->TryGetStringField(TEXT("directory"), Directory);
	int32 MaxResults = 50;
	Params->TryGetNumberField(TEXT("maxResults"), MaxResults);

	TArray<FString> AssetPaths = UEditorAssetLibrary::ListAssets(Directory, true, false);

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	FString QueryLower = Query.ToLower();
	for (const FString& AssetPath : AssetPaths)
	{
		if (ResultsArray.Num() >= MaxResults) break;
		if (!Query.IsEmpty() && !AssetPath.ToLower().Contains(QueryLower)) continue;

		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), AssetPath);
		FString Name;
		int32 SlashIdx = 0;
		if (AssetPath.FindLastChar(TEXT('/'), SlashIdx))
		{
			Name = AssetPath.Mid(SlashIdx + 1);
			int32 DotIdx = 0;
			if (Name.FindChar(TEXT('.'), DotIdx))
			{
				Name = Name.Left(DotIdx);
			}
		}
		else
		{
			Name = AssetPath;
		}
		Item->SetStringField(TEXT("name"), Name);

		FAssetData AssetData = UEditorAssetLibrary::FindAssetData(AssetPath);
		if (AssetData.IsValid())
		{
			Item->SetStringField(TEXT("className"), AssetData.AssetClassPath.GetAssetName().ToString());
		}
		ResultsArray.Add(MakeShared<FJsonValueObject>(Item));
	}

	Result->SetStringField(TEXT("query"), Query);
	Result->SetStringField(TEXT("directory"), Directory);
	Result->SetNumberField(TEXT("resultCount"), ResultsArray.Num());
	Result->SetArrayField(TEXT("results"), ResultsArray);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ReadAsset(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("className"), Asset->GetClass()->GetName());
	Result->SetStringField(TEXT("objectName"), Asset->GetName());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ReadAssetProperties(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Params->TryGetStringField(TEXT("assetPath"), AssetPath);
	}
	if (AssetPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PropertyName;
	if (Params->TryGetStringField(TEXT("propertyName"), PropertyName) && !PropertyName.IsEmpty())
	{
		FProperty* Prop = Asset->GetClass()->FindPropertyByName(*PropertyName);
		if (!Prop)
		{
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Property not found: %s"), *PropertyName));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}
		// Basic serialization - extend as needed
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("propertyName"), PropertyName);
		Result->SetStringField(TEXT("type"), Prop->GetCPPType());
		Result->SetBoolField(TEXT("success"), true);
		return MakeShared<FJsonValueObject>(Result);
	}

	TArray<TSharedPtr<FJsonValue>> PropsArray;
	for (TFieldIterator<FProperty> It(Asset->GetClass()); It; ++It)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), (*It)->GetName());
		P->SetStringField(TEXT("type"), (*It)->GetCPPType());
		PropsArray.Add(MakeShared<FJsonValueObject>(P));
	}
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("className"), Asset->GetClass()->GetName());
	Result->SetNumberField(TEXT("propertyCount"), PropsArray.Num());
	Result->SetArrayField(TEXT("properties"), PropsArray);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::DuplicateAsset(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString SourcePath, DestPath;
	if (!Params->TryGetStringField(TEXT("sourcePath"), SourcePath) || !Params->TryGetStringField(TEXT("destinationPath"), DestPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'sourcePath' and 'destinationPath'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (!UEditorAssetLibrary::DoesAssetExist(SourcePath))
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Source asset not found: %s"), *SourcePath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* Dup = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestPath);
	Result->SetStringField(TEXT("sourcePath"), SourcePath);
	Result->SetStringField(TEXT("destinationPath"), DestPath);
	Result->SetBoolField(TEXT("success"), Dup != nullptr);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::RenameAsset(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString SourcePath, DestPath;
	if (Params->TryGetStringField(TEXT("sourcePath"), SourcePath) && Params->TryGetStringField(TEXT("destinationPath"), DestPath))
	{
		// Use sourcePath/destinationPath directly
	}
	else
	{
		FString AssetPath, NewName;
		if (Params->TryGetStringField(TEXT("assetPath"), AssetPath) && Params->TryGetStringField(TEXT("newName"), NewName))
		{
			SourcePath = AssetPath;
			FString PackageName, AssetName;
			AssetPath.Split(TEXT("."), &PackageName, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			FString ParentDir = FPaths::GetPath(PackageName);
			if (ParentDir.IsEmpty()) ParentDir = PackageName;
			DestPath = FString::Printf(TEXT("%s/%s.%s"), *ParentDir, *NewName, *NewName);
		}
	}

	if (SourcePath.IsEmpty() || DestPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'sourcePath'+'destinationPath' or 'assetPath'+'newName'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (!UEditorAssetLibrary::DoesAssetExist(SourcePath))
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *SourcePath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	bool bOk = UEditorAssetLibrary::RenameAsset(SourcePath, DestPath);
	Result->SetStringField(TEXT("sourcePath"), SourcePath);
	Result->SetStringField(TEXT("destinationPath"), DestPath);
	Result->SetBoolField(TEXT("success"), bOk);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::MoveAsset(const TSharedPtr<FJsonObject>& Params)
{
	// Move is equivalent to Rename in UE
	return RenameAsset(Params);
}

TSharedPtr<FJsonValue> FAssetHandlers::DeleteAsset(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	bool bSuccess = UEditorAssetLibrary::DeleteAsset(AssetPath);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetBoolField(TEXT("success"), bSuccess);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::SaveAsset(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (Params->TryGetStringField(TEXT("path"), AssetPath) && !AssetPath.IsEmpty() && AssetPath != TEXT("all"))
	{
		bool bSuccess = UEditorAssetLibrary::SaveAsset(AssetPath);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetBoolField(TEXT("success"), bSuccess);
	}
	else
	{
		// Save all dirty assets
		UEditorAssetLibrary::SaveDirectory(TEXT("/Game"));
		Result->SetStringField(TEXT("message"), TEXT("All modified assets saved"));
		Result->SetBoolField(TEXT("success"), true);
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ListTextures(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Directory = TEXT("/Game/");
	Params->TryGetStringField(TEXT("directory"), Directory);
	int32 MaxResults = 50;
	Params->TryGetNumberField(TEXT("maxResults"), MaxResults);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Texture2D")), AssetDataList, true);

	TArray<TSharedPtr<FJsonValue>> TexturesArray;
	for (const FAssetData& AssetData : AssetDataList)
	{
		if (TexturesArray.Num() >= MaxResults) break;
		FString AssetPath = AssetData.GetObjectPathString();
		if (!AssetPath.StartsWith(Directory)) continue;

		TSharedPtr<FJsonObject> TexObj = MakeShared<FJsonObject>();
		TexObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		TexObj->SetStringField(TEXT("path"), AssetPath);
		TexturesArray.Add(MakeShared<FJsonValueObject>(TexObj));
	}

	Result->SetArrayField(TEXT("textures"), TexturesArray);
	Result->SetNumberField(TEXT("count"), TexturesArray.Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

// ============================================================================
// DataTable handlers
// ============================================================================

TSharedPtr<FJsonValue> FAssetHandlers::ImportDataTableJson(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString JsonString;
	if (!Params->TryGetStringField(TEXT("jsonString"), JsonString))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'jsonString' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UDataTable* DataTable = Cast<UDataTable>(Asset);
	if (!DataTable)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset is not a DataTable: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	TArray<FString> Errors = DataTable->CreateTableFromJSONString(JsonString);

	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorsArray;
		for (const FString& Error : Errors)
		{
			ErrorsArray.Add(MakeShared<FJsonValueString>(Error));
		}
		Result->SetArrayField(TEXT("errors"), ErrorsArray);
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Import completed with %d error(s)"), Errors.Num()));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	DataTable->MarkPackageDirty();

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("rowCount"), DataTable->GetRowMap().Num());
	Result->SetStringField(TEXT("message"), TEXT("DataTable imported successfully from JSON"));
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ExportDataTableJson(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UDataTable* DataTable = Cast<UDataTable>(Asset);
	if (!DataTable)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset is not a DataTable: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString JsonString = DataTable->GetTableAsJSON(EDataTableExportFlags::UseJsonObjectsForStructs);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("json"), JsonString);
	Result->SetNumberField(TEXT("rowCount"), DataTable->GetRowMap().Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ============================================================================
// FBX import handlers
// ============================================================================

TSharedPtr<FJsonValue> FAssetHandlers::ImportStaticMesh(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString FileName;
	if (!Params->TryGetStringField(TEXT("filename"), FileName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'filename' parameter (path to FBX file)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString DestinationPath = TEXT("/Game/Meshes");
	Params->TryGetStringField(TEXT("destinationPath"), DestinationPath);

	if (!FPaths::FileExists(FileName))
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("File not found: %s"), *FileName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UFbxFactory* FbxFactory = NewObject<UFbxFactory>();
	FbxFactory->AddToRoot();

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
	Task->AddToRoot();
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bSave = false;
	Task->Filename = FileName;
	Task->DestinationPath = DestinationPath;
	Task->Factory = FbxFactory;

	// Optional asset name
	FString AssetName;
	if (Params->TryGetStringField(TEXT("assetName"), AssetName) && !AssetName.IsEmpty())
	{
		Task->DestinationName = AssetName;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	TArray<TSharedPtr<FJsonValue>> ImportedPaths;
	if (Task->Result.Num() > 0)
	{
		for (UObject* ImportedObj : Task->Result)
		{
			if (ImportedObj)
			{
				FString ObjPath = ImportedObj->GetPathName();
				ImportedPaths.Add(MakeShared<FJsonValueString>(ObjPath));
			}
		}
	}

	Result->SetStringField(TEXT("filename"), FileName);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetArrayField(TEXT("importedAssets"), ImportedPaths);
	Result->SetNumberField(TEXT("importedCount"), ImportedPaths.Num());
	Result->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0);
	if (ImportedPaths.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("Import task completed but no assets were produced"));
	}

	Task->RemoveFromRoot();
	FbxFactory->RemoveFromRoot();

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ImportSkeletalMesh(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString FileName;
	if (!Params->TryGetStringField(TEXT("filename"), FileName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'filename' parameter (path to FBX file)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString DestinationPath = TEXT("/Game/Meshes");
	Params->TryGetStringField(TEXT("destinationPath"), DestinationPath);

	if (!FPaths::FileExists(FileName))
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("File not found: %s"), *FileName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UFbxFactory* FbxFactory = NewObject<UFbxFactory>();
	FbxFactory->AddToRoot();

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
			Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("Skeleton not found: %s, importing without skeleton target"), *SkeletonPath));
		}
	}

	FbxFactory->ImportUI = ImportUI;

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->AddToRoot();
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bSave = false;
	Task->Filename = FileName;
	Task->DestinationPath = DestinationPath;
	Task->Factory = FbxFactory;

	// Optional asset name
	FString AssetName;
	if (Params->TryGetStringField(TEXT("assetName"), AssetName) && !AssetName.IsEmpty())
	{
		Task->DestinationName = AssetName;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	TArray<TSharedPtr<FJsonValue>> ImportedPaths;
	if (Task->Result.Num() > 0)
	{
		for (UObject* ImportedObj : Task->Result)
		{
			if (ImportedObj)
			{
				FString ObjPath = ImportedObj->GetPathName();
				ImportedPaths.Add(MakeShared<FJsonValueString>(ObjPath));
			}
		}
	}

	Result->SetStringField(TEXT("filename"), FileName);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetArrayField(TEXT("importedAssets"), ImportedPaths);
	Result->SetNumberField(TEXT("importedCount"), ImportedPaths.Num());
	Result->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0);
	if (ImportedPaths.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("Import task completed but no assets were produced"));
	}

	Task->RemoveFromRoot();
	FbxFactory->RemoveFromRoot();

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ImportAnimation(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString FileName;
	if (!Params->TryGetStringField(TEXT("filename"), FileName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'filename' parameter (path to FBX file)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString SkeletonPath;
	if (!Params->TryGetStringField(TEXT("skeletonPath"), SkeletonPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'skeletonPath' parameter (path to target Skeleton asset)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString DestinationPath = TEXT("/Game/Animations");
	Params->TryGetStringField(TEXT("destinationPath"), DestinationPath);

	if (!FPaths::FileExists(FileName))
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("File not found: %s"), *FileName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UFbxFactory* FbxFactory = NewObject<UFbxFactory>();
	FbxFactory->AddToRoot();

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
	Task->AddToRoot();
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bSave = false;
	Task->Filename = FileName;
	Task->DestinationPath = DestinationPath;
	Task->Factory = FbxFactory;

	// Optional asset name
	FString AssetName;
	if (Params->TryGetStringField(TEXT("assetName"), AssetName) && !AssetName.IsEmpty())
	{
		Task->DestinationName = AssetName;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	TArray<TSharedPtr<FJsonValue>> ImportedPaths;
	if (Task->Result.Num() > 0)
	{
		for (UObject* ImportedObj : Task->Result)
		{
			if (ImportedObj)
			{
				FString ObjPath = ImportedObj->GetPathName();
				ImportedPaths.Add(MakeShared<FJsonValueString>(ObjPath));
			}
		}
	}

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

	Task->RemoveFromRoot();
	FbxFactory->RemoveFromRoot();

	return MakeShared<FJsonValueObject>(Result);
}

// ============================================================================
// Texture handlers
// ============================================================================

TSharedPtr<FJsonValue> FAssetHandlers::ListTextureProperties(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		if (!Params->TryGetStringField(TEXT("path"), AssetPath))
		{
			Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' or 'path' parameter"));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}
	}

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UTexture2D* Texture = Cast<UTexture2D>(Asset);
	if (!Texture)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset is not a Texture2D: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::SetTextureProperties(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		if (!Params->TryGetStringField(TEXT("path"), AssetPath))
		{
			Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' or 'path' parameter"));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}
	}

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UTexture2D* Texture = Cast<UTexture2D>(Asset);
	if (!Texture)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset is not a Texture2D: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
		Result->SetStringField(TEXT("error"), TEXT("No valid properties specified to set"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Notify the engine of property changes and mark dirty
	Texture->PostEditChange();
	Texture->MarkPackageDirty();

	TArray<TSharedPtr<FJsonValue>> ModifiedArray;
	for (const FString& PropName : ModifiedProperties)
	{
		ModifiedArray.Add(MakeShared<FJsonValueString>(PropName));
	}

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetArrayField(TEXT("modifiedProperties"), ModifiedArray);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Modified %d texture properties"), ModifiedProperties.Num()));
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ImportTexture(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString FileName;
	if (!Params->TryGetStringField(TEXT("filename"), FileName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'filename' parameter (path to texture file)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString DestinationPath = TEXT("/Game/Textures");
	Params->TryGetStringField(TEXT("destinationPath"), DestinationPath);

	if (!FPaths::FileExists(FileName))
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("File not found: %s"), *FileName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
	TextureFactory->AddToRoot();

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
	Task->AddToRoot();
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bSave = false;
	Task->Filename = FileName;
	Task->DestinationPath = DestinationPath;
	Task->Factory = TextureFactory;

	// Optional asset name
	FString AssetName;
	if (Params->TryGetStringField(TEXT("assetName"), AssetName) && !AssetName.IsEmpty())
	{
		Task->DestinationName = AssetName;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	TArray<TSharedPtr<FJsonValue>> ImportedPaths;
	if (Task->Result.Num() > 0)
	{
		for (UObject* ImportedObj : Task->Result)
		{
			if (ImportedObj)
			{
				FString ObjPath = ImportedObj->GetPathName();
				ImportedPaths.Add(MakeShared<FJsonValueString>(ObjPath));
			}
		}
	}

	Result->SetStringField(TEXT("filename"), FileName);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetArrayField(TEXT("importedAssets"), ImportedPaths);
	Result->SetNumberField(TEXT("importedCount"), ImportedPaths.Num());
	Result->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0);
	if (ImportedPaths.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("Import task completed but no assets were produced"));
	}

	Task->RemoveFromRoot();
	TextureFactory->RemoveFromRoot();

	return MakeShared<FJsonValueObject>(Result);
}
