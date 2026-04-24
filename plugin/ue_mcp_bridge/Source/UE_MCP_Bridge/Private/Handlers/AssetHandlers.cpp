#include "AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Exporters/Exporter.h"
#include "AssetExportTask.h"
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
#include "Factories/DataTableFactory.h"
#include "Kismet/DataTableFunctionLibrary.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

// Mesh sockets
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"

// Import tasks
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

// FBX
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/FbxAnimSequenceImportData.h"

// Texture
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"

// Reimport
#include "EditorReimportHandler.h"

// Collision / BodySetup
#include "PhysicsEngine/BodySetup.h"
#include "AI/Navigation/NavCollisionBase.h"

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
	Registry.RegisterHandler(TEXT("delete_asset_batch"), &DeleteAssetBatch);
	Registry.RegisterHandler(TEXT("bulk_rename_assets"), &BulkRename);
	Registry.RegisterHandler(TEXT("create_data_asset"), &CreateDataAsset);
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

	// Aliases for TS tool compatibility
	Registry.RegisterHandler(TEXT("get_texture_info"), &ListTextureProperties);
	Registry.RegisterHandler(TEXT("set_texture_settings"), &SetTextureProperties);

	// Mesh handlers
	Registry.RegisterHandler(TEXT("set_mesh_material"), &SetMeshMaterial);
	Registry.RegisterHandler(TEXT("recenter_pivot"), &RecenterPivot);

	// Socket handlers
	Registry.RegisterHandler(TEXT("add_socket"), &AddSocket);
	Registry.RegisterHandler(TEXT("remove_socket"), &RemoveSocket);
	Registry.RegisterHandler(TEXT("list_sockets"), &ListSockets);
	Registry.RegisterHandler(TEXT("reload_package"), &ReloadPackage);

	// Additional DataTable handlers
	Registry.RegisterHandler(TEXT("create_datatable"), &CreateDataTable);
	Registry.RegisterHandler(TEXT("read_datatable"), &ReadDataTable);
	Registry.RegisterHandler(TEXT("reimport_datatable"), &ReimportDataTable);

	// Generic reimport / export
	Registry.RegisterHandler(TEXT("reimport_asset"), &ReimportAsset);
	Registry.RegisterHandler(TEXT("export_asset"), &ExportAsset);

	// v0.7.8 stubs — FTS5-backed asset search
	Registry.RegisterHandler(TEXT("search_assets_fts"), &SearchAssetsFTS);
	Registry.RegisterHandler(TEXT("reindex_assets_fts"), &ReindexAssetsFTS);

	// v0.7.19 #150 — AssetRegistry referencers
	Registry.RegisterHandler(TEXT("get_asset_referencers"), &GetReferencers);

	// v1.0.0-rc.2 — #155 (asset gaps)
	Registry.RegisterHandler(TEXT("set_sk_material_slots"), &SetSkeletalMeshMaterialSlots);
	Registry.RegisterHandler(TEXT("diagnose_registry"), &DiagnoseRegistry);

	// v1.0.0-rc.3 — #177, #192, #193
	Registry.RegisterHandler(TEXT("get_mesh_bounds"), &GetMeshBounds);
	Registry.RegisterHandler(TEXT("get_mesh_collision"), &GetMeshCollision);
	Registry.RegisterHandler(TEXT("set_mesh_nav"), &SetMeshNav);
	Registry.RegisterHandler(TEXT("move_folder"), &MoveFolder);
}

// ---------------------------------------------------------------------------
// v0.7.8 STUBS — FTS5-backed asset index (Milestone A)
// Strategy:
//  - Index lives at <project>/Saved/MCP/asset_index.sqlite (SQLite with FTS5).
//  - Columns: name, path, class, tags, referencers (tokenized).
//  - Populate via AssetRegistry scan; refresh via OnAssetAdded/Renamed/Removed hooks.
//  - search_assets_fts: MATCH on name/tags/class with bm25 ranking, limit/offset paging.
// ---------------------------------------------------------------------------

// Tokenize on non-alnum boundaries, lowercase, drop empties.
static void TokenizeLower(const FString& In, TArray<FString>& Out)
{
	FString Buf;
	Buf.Reserve(In.Len());
	for (TCHAR C : In)
	{
		if (FChar::IsAlnum(C)) Buf.AppendChar(FChar::ToLower(C));
		else if (Buf.Len()) { Out.Add(Buf); Buf.Reset(); }
	}
	if (Buf.Len()) Out.Add(Buf);
}

// Score a document against query tokens. Exact whole-token hit = 10; prefix hit = 5; substring = 2.
// Name field scores x3, class x2, path x1 (weights bias toward asset name matches).
static int32 ScoreAsset(const TArray<FString>& QueryTokens, const TArray<FString>& NameToks, const TArray<FString>& ClassToks, const TArray<FString>& PathToks)
{
	int32 Score = 0;
	auto ScoreField = [&](const TArray<FString>& DocToks, int32 Weight)
	{
		for (const FString& Q : QueryTokens)
		{
			int32 Best = 0;
			for (const FString& D : DocToks)
			{
				if (D == Q)                    { Best = FMath::Max(Best, 10); }
				else if (D.StartsWith(Q))      { Best = FMath::Max(Best, 5); }
				else if (D.Contains(Q))        { Best = FMath::Max(Best, 2); }
			}
			Score += Best * Weight;
		}
	};
	ScoreField(NameToks, 3);
	ScoreField(ClassToks, 2);
	ScoreField(PathToks, 1);
	return Score;
}

TSharedPtr<FJsonValue> FAssetHandlers::SearchAssetsFTS(const TSharedPtr<FJsonObject>& Params)
{
	FString Query;
	if (auto Err = RequireString(Params, TEXT("query"), Query)) return Err;
	const int32 MaxResults = OptionalInt(Params, TEXT("maxResults"), 50);
	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"), TEXT(""));

	TArray<FString> QueryToks;
	TokenizeLower(Query, QueryToks);
	if (QueryToks.Num() == 0)
	{
		return MCPError(TEXT("Query contained no searchable tokens"));
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = AssetRegistryModule.Get();

	TArray<FAssetData> AllAssets;
	Registry.GetAllAssets(AllAssets, /*bIncludeOnlyOnDiskAssets=*/true);

	struct FHit { int32 Score; const FAssetData* Data; };
	TArray<FHit> Hits;
	Hits.Reserve(1024);

	for (const FAssetData& Data : AllAssets)
	{
		const FString ClassStr = Data.AssetClassPath.GetAssetName().ToString();
		if (!ClassFilter.IsEmpty() && !ClassStr.Contains(ClassFilter)) continue;

		const FString NameStr = Data.AssetName.ToString();
		const FString PathStr = Data.PackageName.ToString();

		TArray<FString> NameToks, ClassToks, PathToks;
		TokenizeLower(NameStr, NameToks);
		TokenizeLower(ClassStr, ClassToks);
		TokenizeLower(PathStr, PathToks);

		const int32 S = ScoreAsset(QueryToks, NameToks, ClassToks, PathToks);
		if (S > 0) Hits.Add({ S, &Data });
	}

	Hits.Sort([](const FHit& A, const FHit& B) { return A.Score > B.Score; });
	const int32 Kept = FMath::Min(Hits.Num(), MaxResults);

	TArray<TSharedPtr<FJsonValue>> Arr;
	Arr.Reserve(Kept);
	for (int32 i = 0; i < Kept; ++i)
	{
		const FAssetData& D = *Hits[i].Data;
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("path"), D.PackageName.ToString());
		R->SetStringField(TEXT("name"), D.AssetName.ToString());
		R->SetStringField(TEXT("class"), D.AssetClassPath.GetAssetName().ToString());
		R->SetNumberField(TEXT("score"), Hits[i].Score);
		Arr.Add(MakeShared<FJsonValueObject>(R));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("query"), Query);
	Result->SetNumberField(TEXT("totalMatched"), Hits.Num());
	Result->SetNumberField(TEXT("resultCount"), Arr.Num());
	Result->SetArrayField(TEXT("results"), Arr);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ReindexAssetsFTS(const TSharedPtr<FJsonObject>& Params)
{
	// No persistent index yet — ranked search runs live against the asset registry,
	// which keeps itself current. This endpoint forces a registry rescan so newly
	// added assets on disk become searchable immediately.
	const FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game"));

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = AssetRegistryModule.Get();

	TArray<FString> ScanPaths = { Directory };
	Registry.ScanPathsSynchronous(ScanPaths, /*bForceRescan=*/true);

	TArray<FAssetData> Found;
	Registry.GetAssetsByPath(FName(*Directory), Found, /*bRecursive=*/true);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("directory"), Directory);
	Result->SetNumberField(TEXT("indexedCount"), Found.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ListAssets(const TSharedPtr<FJsonObject>& Params)
{
	FString Query = OptionalString(Params, TEXT("query"), TEXT("*"));

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAllAssets(AssetDataList);

	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	for (const FAssetData& AssetData : AssetDataList)
	{
		FString AssetPath = AssetData.GetObjectPathString();
		if (Query == TEXT("*") || AssetPath.Contains(Query))
		{
			TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
			AssetObj->SetStringField(TEXT("path"), AssetPath);
			AssetObj->SetStringField(TEXT("className"), AssetData.AssetClassPath.GetAssetName().ToString());
			AssetObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
			AssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("assets"), AssetsArray);
	Result->SetNumberField(TEXT("count"), AssetsArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::SearchAssets(const TSharedPtr<FJsonObject>& Params)
{
	FString Query = OptionalString(Params, TEXT("query"));
	FString Directory;
	bool bHasDirectory = Params->TryGetStringField(TEXT("directory"), Directory);
	if (!bHasDirectory)
	{
		Directory = TEXT("/Game/");
	}
	int32 MaxResults = OptionalInt(Params, TEXT("maxResults"), 50);
	bool bSearchAll = OptionalBool(Params, TEXT("searchAll"));

	// Use AssetRegistry for global search when searchAll is true or directory contains wildcards
	if (bSearchAll || (!bHasDirectory && !Query.IsEmpty() && Query.Contains(TEXT("*"))))
	{
		// Use the AssetRegistry for indexed search across all content roots
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		FARFilter Filter;
		Filter.bRecursivePaths = true;

		// If a directory is explicitly provided with searchAll, scope to that directory
		if (bHasDirectory)
		{
			Filter.PackagePaths.Add(FName(*Directory));
		}

		TArray<FAssetData> AllAssets;
		AssetRegistry.GetAssets(Filter, AllAssets);

		TArray<TSharedPtr<FJsonValue>> ResultsArray;
		FString QueryLower = Query.ToLower();
		for (const FAssetData& AssetData : AllAssets)
		{
			if (ResultsArray.Num() >= MaxResults) break;
			FString AssetPath = AssetData.GetObjectPathString();
			FString AssetName = AssetData.AssetName.ToString();
			if (!Query.IsEmpty())
			{
				// Support wildcard matching
				if (Query.Contains(TEXT("*")))
				{
					if (!AssetPath.MatchesWildcard(Query))
					{
						continue;
					}
				}
				else if (!AssetPath.ToLower().Contains(QueryLower) && !AssetName.ToLower().Contains(QueryLower))
				{
					continue;
				}
			}

			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("path"), AssetData.PackageName.ToString());
			Item->SetStringField(TEXT("name"), AssetName);
			Item->SetStringField(TEXT("className"), AssetData.AssetClassPath.GetAssetName().ToString());
			ResultsArray.Add(MakeShared<FJsonValueObject>(Item));
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("query"), Query);
		Result->SetStringField(TEXT("searchScope"), bHasDirectory ? Directory : TEXT("all"));
		Result->SetNumberField(TEXT("resultCount"), ResultsArray.Num());
		Result->SetArrayField(TEXT("results"), ResultsArray);
		return MCPResult(Result);
	}

	// Default: directory-based search (original behavior)
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

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("query"), Query);
	Result->SetStringField(TEXT("directory"), Directory);
	Result->SetNumberField(TEXT("resultCount"), ResultsArray.Num());
	Result->SetArrayField(TEXT("results"), ResultsArray);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ReadAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		// Fallback to LoadObject for full object paths
		Asset = LoadObject<UObject>(nullptr, *AssetPath);
	}
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("className"), Asset->GetClass()->GetName());
	Result->SetStringField(TEXT("objectName"), Asset->GetName());

	// Read properties via reflection
	TSharedPtr<FJsonObject> PropertiesObj = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(Asset->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;

		// Skip editor-only internal properties that aren't useful
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient)) continue;

		const FString PropName = Prop->GetName();
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Asset);

		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			PropertiesObj->SetBoolField(PropName, BoolProp->GetPropertyValue(ValuePtr));
		}
		else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			PropertiesObj->SetNumberField(PropName, IntProp->GetPropertyValue(ValuePtr));
		}
		else if (FInt64Property* Int64Prop = CastField<FInt64Property>(Prop))
		{
			PropertiesObj->SetNumberField(PropName, static_cast<double>(Int64Prop->GetPropertyValue(ValuePtr)));
		}
		else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			PropertiesObj->SetNumberField(PropName, FloatProp->GetPropertyValue(ValuePtr));
		}
		else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			PropertiesObj->SetNumberField(PropName, DoubleProp->GetPropertyValue(ValuePtr));
		}
		else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			PropertiesObj->SetStringField(PropName, StrProp->GetPropertyValue(ValuePtr));
		}
		else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			PropertiesObj->SetStringField(PropName, NameProp->GetPropertyValue(ValuePtr).ToString());
		}
		else if (FTextProperty* TextProp = CastField<FTextProperty>(Prop))
		{
			PropertiesObj->SetStringField(PropName, TextProp->GetPropertyValue(ValuePtr).ToString());
		}
		else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
			int64 EnumValue = UnderlyingProp->GetSignedIntPropertyValue(ValuePtr);
			if (UEnum* Enum = EnumProp->GetEnum())
			{
				FString EnumName = Enum->GetNameStringByValue(EnumValue);
				PropertiesObj->SetStringField(PropName, EnumName);
			}
			else
			{
				PropertiesObj->SetNumberField(PropName, static_cast<double>(EnumValue));
			}
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			if (ByteProp->Enum)
			{
				uint8 ByteVal = ByteProp->GetPropertyValue(ValuePtr);
				FString EnumName = ByteProp->Enum->GetNameStringByValue(ByteVal);
				PropertiesObj->SetStringField(PropName, EnumName);
			}
			else
			{
				PropertiesObj->SetNumberField(PropName, ByteProp->GetPropertyValue(ValuePtr));
			}
		}
		else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			UObject* RefObj = ObjProp->GetPropertyValue(ValuePtr);
			if (RefObj)
			{
				PropertiesObj->SetStringField(PropName, RefObj->GetPathName());
			}
			else
			{
				PropertiesObj->SetField(PropName, MakeShared<FJsonValueNull>());
			}
		}
		else if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop))
		{
			FSoftObjectPtr SoftPtr = SoftObjProp->GetPropertyValue(ValuePtr);
			PropertiesObj->SetStringField(PropName, SoftPtr.ToString());
		}
		else
		{
			// For complex types, export as string
			FString ValueStr;
			Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
			if (!ValueStr.IsEmpty())
			{
				PropertiesObj->SetStringField(PropName, ValueStr);
			}
			else
			{
				PropertiesObj->SetStringField(PropName, FString::Printf(TEXT("<%s>"), *Prop->GetCPPType()));
			}
		}
	}

	Result->SetObjectField(TEXT("properties"), PropertiesObj);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ReadAssetProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	// Helper lambda to export a property value as string (#48 — reads arrays, structs, sub-objects)
	auto ExportPropertyValue = [](FProperty* Prop, const void* Container, UObject* Outer) -> FString
	{
		FString ValueStr;
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);
		Prop->ExportText_Direct(ValueStr, ValuePtr, ValuePtr, Outer, PPF_None);
		return ValueStr;
	};

	FString PropertyName;
	if (Params->TryGetStringField(TEXT("propertyName"), PropertyName) && !PropertyName.IsEmpty())
	{
		FProperty* Prop = Asset->GetClass()->FindPropertyByName(*PropertyName);
		if (!Prop)
		{
			return MCPError(FString::Printf(TEXT("Property not found: %s"), *PropertyName));
		}
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("propertyName"), PropertyName);
		Result->SetStringField(TEXT("type"), Prop->GetCPPType());
		Result->SetStringField(TEXT("value"), ExportPropertyValue(Prop, Asset, Asset));
		return MCPResult(Result);
	}

	// Return all properties with their values
	bool bIncludeValues = OptionalBool(Params, TEXT("includeValues"));

	TArray<TSharedPtr<FJsonValue>> PropsArray;
	for (TFieldIterator<FProperty> It(Asset->GetClass()); It; ++It)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), (*It)->GetName());
		P->SetStringField(TEXT("type"), (*It)->GetCPPType());
		if (bIncludeValues)
		{
			P->SetStringField(TEXT("value"), ExportPropertyValue(*It, Asset, Asset));
		}
		PropsArray.Add(MakeShared<FJsonValueObject>(P));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("className"), Asset->GetClass()->GetName());
	Result->SetNumberField(TEXT("propertyCount"), PropsArray.Num());
	Result->SetArrayField(TEXT("properties"), PropsArray);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::DuplicateAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString SourcePath;
	if (auto Err = RequireString(Params, TEXT("sourcePath"), SourcePath)) return Err;
	FString DestPath;
	if (auto Err = RequireString(Params, TEXT("destinationPath"), DestPath)) return Err;

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (!UEditorAssetLibrary::DoesAssetExist(SourcePath))
	{
		return MCPError(FString::Printf(TEXT("Source asset not found: %s"), *SourcePath));
	}

	// Idempotency: if the destination already exists, short-circuit.
	if (UEditorAssetLibrary::DoesAssetExist(DestPath))
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("Destination asset already exists: %s"), *DestPath));
		}
		auto Existing = MCPSuccess();
		MCPSetExisted(Existing);
		Existing->SetStringField(TEXT("sourcePath"), SourcePath);
		Existing->SetStringField(TEXT("destinationPath"), DestPath);
		return MCPResult(Existing);
	}

	UObject* Dup = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestPath);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("sourcePath"), SourcePath);
	Result->SetStringField(TEXT("destinationPath"), DestPath);
	Result->SetBoolField(TEXT("success"), Dup != nullptr);

	if (Dup)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), DestPath);
		MCPSetRollback(Result, TEXT("delete_asset"), Payload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::RenameAsset(const TSharedPtr<FJsonObject>& Params)
{
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
		return MCPError(TEXT("Missing 'sourcePath'+'destinationPath' or 'assetPath'+'newName'"));
	}

	// Idempotency: if already at destination, no-op.
	if (SourcePath == DestPath)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("sourcePath"), SourcePath);
		Noop->SetStringField(TEXT("destinationPath"), DestPath);
		return MCPResult(Noop);
	}

	// Idempotency: if source is absent but destination exists, prior run succeeded.
	if (!UEditorAssetLibrary::DoesAssetExist(SourcePath))
	{
		if (UEditorAssetLibrary::DoesAssetExist(DestPath))
		{
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetStringField(TEXT("sourcePath"), SourcePath);
			Noop->SetStringField(TEXT("destinationPath"), DestPath);
			return MCPResult(Noop);
		}
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *SourcePath));
	}

	bool bOk = UEditorAssetLibrary::RenameAsset(SourcePath, DestPath);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("sourcePath"), SourcePath);
	Result->SetStringField(TEXT("destinationPath"), DestPath);
	Result->SetBoolField(TEXT("success"), bOk);

	if (bOk)
	{
		// Self-inverse: rename back.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("sourcePath"), DestPath);
		Payload->SetStringField(TEXT("destinationPath"), SourcePath);
		MCPSetRollback(Result, TEXT("rename_asset"), Payload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::MoveAsset(const TSharedPtr<FJsonObject>& Params)
{
	// Move is equivalent to Rename in UE
	return RenameAsset(Params);
}

TSharedPtr<FJsonValue> FAssetHandlers::DeleteAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	// Idempotent: if the asset doesn't exist, treat as already-deleted.
	if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Result);
	}

	bool bSuccess = UEditorAssetLibrary::DeleteAsset(AssetPath);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetBoolField(TEXT("deleted"), bSuccess);
	// Delete is non-reversible by default.

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::DeleteAssetBatch(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("assetPaths"), PathsArr) && !Params->TryGetArrayField(TEXT("paths"), PathsArr))
	{
		return MCPError(TEXT("Missing 'assetPaths' array parameter"));
	}

	TArray<TSharedPtr<FJsonValue>> PerPath;
	int32 Deleted = 0;
	int32 Absent = 0;
	int32 Failed = 0;

	for (const TSharedPtr<FJsonValue>& V : *PathsArr)
	{
		FString Path;
		if (!V.IsValid() || !V->TryGetString(Path) || Path.IsEmpty())
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Path);

		if (!UEditorAssetLibrary::DoesAssetExist(Path))
		{
			Entry->SetStringField(TEXT("status"), TEXT("absent"));
			Absent++;
		}
		else if (UEditorAssetLibrary::DeleteAsset(Path))
		{
			Entry->SetStringField(TEXT("status"), TEXT("deleted"));
			Deleted++;
		}
		else
		{
			Entry->SetStringField(TEXT("status"), TEXT("failed"));
			Failed++;
		}
		PerPath.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("results"), PerPath);
	Result->SetNumberField(TEXT("deleted"), Deleted);
	Result->SetNumberField(TEXT("absent"), Absent);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetNumberField(TEXT("total"), PerPath.Num());
	return MCPResult(Result);
}

// ─── #128 item 6 — bulk_rename_assets ───────────────────────────────
// Scene-referenced assets are expensive to rename one-by-one because each
// individual rename forces a redirector-fixup / level-reference-update
// pass across the whole project. At batches of 10-15 this can crash the
// editor (observed on the user's Vale project).
//
// Content Browser drag-moves use IAssetTools::RenameAssets() with an
// array of FAssetRenameData — that collapses every rename into a single
// transaction with one redirector-fixup pass. This handler mirrors that
// pattern.
//
// Params:
//   renames: [{ sourcePath, destinationPath }, ...]
//     or    [{ assetPath, newName }, ...]      (same as rename_asset)
//     or    [{ sourcePath, newPackagePath, newName }, ...]
TSharedPtr<FJsonValue> FAssetHandlers::BulkRename(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Params->TryGetArrayField(TEXT("renames"), Items) &&
		!Params->TryGetArrayField(TEXT("items"), Items))
	{
		return MCPError(TEXT("Missing 'renames' array parameter"));
	}

	TArray<FAssetRenameData> BatchRenames;
	TArray<TSharedPtr<FJsonValue>> PerItem;
	int32 Skipped = 0;

	for (const TSharedPtr<FJsonValue>& V : *Items)
	{
		if (!V.IsValid()) continue;
		const TSharedPtr<FJsonObject>* EntryPtr = nullptr;
		if (!V->TryGetObject(EntryPtr) || !EntryPtr || !EntryPtr->IsValid())
		{
			continue;
		}
		const TSharedPtr<FJsonObject>& Entry = *EntryPtr;

		TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();

		FString SourcePath;
		FString NewPackagePath;
		FString NewName;

		if (Entry->TryGetStringField(TEXT("sourcePath"), SourcePath))
		{
			FString DestPath;
			if (Entry->TryGetStringField(TEXT("destinationPath"), DestPath))
			{
				// Split DestPath "/Game/Foo/Bar.Bar" → "/Game/Foo" + "Bar"
				FString Pkg, ObjName;
				DestPath.Split(TEXT("."), &Pkg, &ObjName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				if (ObjName.IsEmpty())
				{
					// Accept bare package form "/Game/Foo/Bar"
					Pkg = DestPath;
					ObjName = FPaths::GetBaseFilename(DestPath);
				}
				NewPackagePath = FPaths::GetPath(Pkg);
				NewName = ObjName;
			}
			else
			{
				Entry->TryGetStringField(TEXT("newPackagePath"), NewPackagePath);
				Entry->TryGetStringField(TEXT("newName"), NewName);
			}
		}
		else if (Entry->TryGetStringField(TEXT("assetPath"), SourcePath))
		{
			Entry->TryGetStringField(TEXT("newName"), NewName);
			NewPackagePath = FPaths::GetPath(SourcePath);
		}

		Record->SetStringField(TEXT("sourcePath"), SourcePath);

		if (SourcePath.IsEmpty() || NewName.IsEmpty() || NewPackagePath.IsEmpty())
		{
			Record->SetStringField(TEXT("status"), TEXT("invalid"));
			PerItem.Add(MakeShared<FJsonValueObject>(Record));
			Skipped++;
			continue;
		}

		UObject* Asset = UEditorAssetLibrary::LoadAsset(SourcePath);
		if (!Asset)
		{
			Record->SetStringField(TEXT("status"), TEXT("not_found"));
			PerItem.Add(MakeShared<FJsonValueObject>(Record));
			Skipped++;
			continue;
		}

		FAssetRenameData Data(Asset, NewPackagePath, NewName);
		BatchRenames.Add(Data);

		Record->SetStringField(TEXT("destinationPath"),
			FString::Printf(TEXT("%s/%s.%s"), *NewPackagePath, *NewName, *NewName));
		PerItem.Add(MakeShared<FJsonValueObject>(Record));
	}

	if (BatchRenames.Num() == 0)
	{
		return MCPError(TEXT("No valid renames to process"));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	// RenameAssets wraps all renames in a single transaction + one
	// redirector-fixup pass — the same op the Content Browser performs on
	// drag-and-drop. Returns true if every rename succeeded.
	bool bOk = AssetTools.RenameAssets(BatchRenames);

	// Mark each batched rename with its post-op status.
	int32 Succeeded = 0;
	int32 Failed = 0;
	int32 Idx = 0;
	for (int32 i = 0; i < PerItem.Num(); ++i)
	{
		TSharedPtr<FJsonObject> Rec = PerItem[i]->AsObject();
		if (!Rec.IsValid() || Rec->HasField(TEXT("status"))) continue;

		const FAssetRenameData& Data = BatchRenames[Idx++];
		// A rename is considered to have succeeded if the asset now lives
		// at the destination. When bOk==false, some entries may still have
		// landed — check per-item.
		const FString DestFullPath = FString::Printf(TEXT("%s/%s.%s"),
			*Data.NewPackagePath, *Data.NewName, *Data.NewName);
		if (UEditorAssetLibrary::DoesAssetExist(DestFullPath))
		{
			Rec->SetStringField(TEXT("status"), TEXT("renamed"));
			Succeeded++;
		}
		else
		{
			Rec->SetStringField(TEXT("status"), TEXT("failed"));
			Failed++;
		}
	}

	auto Result = MCPSuccess();
	if (Succeeded > 0) MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("success"), bOk);
	Result->SetNumberField(TEXT("renamed"), Succeeded);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetNumberField(TEXT("skipped"), Skipped);
	Result->SetNumberField(TEXT("total"), PerItem.Num());
	Result->SetArrayField(TEXT("results"), PerItem);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::CreateDataAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game"));
	FString ClassName;
	if (auto Err = RequireStringAlt(Params, TEXT("className"), TEXT("class"), ClassName)) return Err;

	// Resolve the DataAsset subclass by name or path
	UClass* DataClass = nullptr;
	if (ClassName.StartsWith(TEXT("/")))
	{
		DataClass = LoadClass<UObject>(nullptr, *ClassName);
		if (!DataClass) DataClass = LoadObject<UClass>(nullptr, *ClassName);
	}
	if (!DataClass)
	{
		FString Trimmed = ClassName;
		Trimmed.RemoveFromEnd(TEXT("_C"));
		// Attempt find in any package (fallback: scan all loaded UClass objects)
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName() == Trimmed || It->GetName() == ClassName)
			{
				DataClass = *It;
				break;
			}
		}
	}
	if (!DataClass)
	{
		return MCPError(FString::Printf(TEXT("Class not found: %s (pass full /Script/Module.ClassName or a loaded class name)"), *ClassName));
	}
	if (!DataClass->IsChildOf(UDataAsset::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("Class %s is not a UDataAsset subclass"), *ClassName));
	}

	const FString FullPath = FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *Name, *Name);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("DataAsset")))
	{
		return Existing;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();
	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, DataClass, nullptr);
	if (!NewAsset)
	{
		return MCPError(FString::Printf(TEXT("Failed to create DataAsset %s of class %s"), *Name, *DataClass->GetName()));
	}

	// Optional properties object — use recursive JSON-to-property setter so that
	// TArray<FStruct> with nested UObject refs, FGameplayTag, etc. all work (#196, #199).
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	int32 SetCount = 0;
	TArray<FString> PropErrors;
	if (Params->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && (*PropsObj).IsValid())
	{
		for (const auto& Pair : (*PropsObj)->Values)
		{
			FProperty* Prop = DataClass->FindPropertyByName(FName(*Pair.Key));
			if (!Prop)
			{
				PropErrors.Add(FString::Printf(TEXT("Property not found: %s"), *Pair.Key));
				continue;
			}
			void* Addr = Prop->ContainerPtrToValuePtr<void>(NewAsset);
			FString SetErr;
			if (MCPJsonProperty::SetJsonOnProperty(Prop, Addr, Pair.Value, SetErr))
			{
				SetCount++;
			}
			else
			{
				PropErrors.Add(FString::Printf(TEXT("Failed to set %s: %s"), *Pair.Key, *SetErr));
			}
		}
	}

	UEditorAssetLibrary::SaveAsset(FullPath);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), FullPath);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("className"), DataClass->GetName());
	Result->SetNumberField(TEXT("propertiesSet"), SetCount);
	if (PropErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Errs;
		for (const FString& E : PropErrors) Errs.Add(MakeShared<FJsonValueString>(E));
		Result->SetArrayField(TEXT("propertyErrors"), Errs);
	}

	// Rollback: delete the newly created asset
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), FullPath);
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::SaveAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if ((Params->TryGetStringField(TEXT("path"), AssetPath) || Params->TryGetStringField(TEXT("assetPath"), AssetPath)) && !AssetPath.IsEmpty() && AssetPath != TEXT("all"))
	{
		bool bSuccess = UEditorAssetLibrary::SaveAsset(AssetPath);
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetBoolField(TEXT("success"), bSuccess);
		return MCPResult(Result);
	}
	else
	{
		// Save all dirty assets
		UEditorAssetLibrary::SaveDirectory(TEXT("/Game"));
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("message"), TEXT("All modified assets saved"));
		return MCPResult(Result);
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::ListTextures(const TSharedPtr<FJsonObject>& Params)
{
	FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game/"));
	int32 MaxResults = OptionalInt(Params, TEXT("maxResults"), 50);

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

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("textures"), TexturesArray);
	Result->SetNumberField(TEXT("count"), TexturesArray.Num());
	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FAssetHandlers::SetMeshMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString MaterialPath;
	if (auto Err = RequireString(Params, TEXT("materialPath"), MaterialPath)) return Err;

	int32 SlotIndex = OptionalInt(Params, TEXT("slotIndex"), 0);

	UStaticMesh* Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *AssetPath));
	if (!Mesh)
	{
		return MCPError(FString::Printf(TEXT("Failed to load static mesh at '%s'"), *AssetPath));
	}

	UMaterialInterface* Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MaterialPath));
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	if (SlotIndex < 0 || SlotIndex >= Mesh->GetStaticMaterials().Num())
	{
		return MCPError(FString::Printf(TEXT("Slot index %d out of range (mesh has %d slots)"), SlotIndex, Mesh->GetStaticMaterials().Num()));
	}

	// Capture previous material for self-inverse rollback.
	FString PreviousMaterialPath;
	if (UMaterialInterface* Prev = Mesh->GetMaterial(SlotIndex))
	{
		PreviousMaterialPath = Prev->GetPathName();
	}

	Mesh->SetMaterial(SlotIndex, Material);
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("materialPath"), MaterialPath);
	Result->SetNumberField(TEXT("slotIndex"), SlotIndex);
	Result->SetStringField(TEXT("previousMaterialPath"), PreviousMaterialPath);

	if (!PreviousMaterialPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("materialPath"), PreviousMaterialPath);
		Payload->SetNumberField(TEXT("slotIndex"), SlotIndex);
		MCPSetRollback(Result, TEXT("set_mesh_material"), Payload);
	}

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FAssetHandlers::RecenterPivot(const TSharedPtr<FJsonObject>& Params)
{
	// Support single assetPath or array of assetPaths
	TArray<FString> AssetPaths;
	const TArray<TSharedPtr<FJsonValue>>* PathsArray = nullptr;
	FString SinglePath;

	if (Params->TryGetArrayField(TEXT("assetPaths"), PathsArray))
	{
		for (const auto& Val : *PathsArray)
		{
			FString P;
			if (Val->TryGetString(P) && !P.IsEmpty())
			{
				AssetPaths.Add(P);
			}
		}
	}
	else if (Params->TryGetStringField(TEXT("assetPath"), SinglePath) || Params->TryGetStringField(TEXT("path"), SinglePath))
	{
		if (!SinglePath.IsEmpty())
		{
			AssetPaths.Add(SinglePath);
		}
	}

	if (AssetPaths.Num() == 0)
	{
		return MCPError(TEXT("Missing 'assetPath' (string) or 'assetPaths' (array of strings)"));
	}

	// Load all meshes
	TArray<UStaticMesh*> Meshes;
	for (const FString& Path : AssetPaths)
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *Path));
		if (!Mesh)
		{
			return MCPError(FString::Printf(TEXT("Failed to load static mesh at '%s'"), *Path));
		}
		Meshes.Add(Mesh);
	}

	// Compute the center from the FIRST mesh (reference mesh)
	FMeshDescription* RefDesc = Meshes[0]->GetMeshDescription(0);
	if (!RefDesc)
	{
		return MCPError(TEXT("Failed to get mesh description for reference mesh LOD 0"));
	}

	FVertexArray& RefVerts = RefDesc->Vertices();
	TVertexAttributesRef<FVector3f> RefPositions = RefDesc->GetVertexPositions();

	FVector3f Center = FVector3f::ZeroVector;
	int32 RefVertCount = RefVerts.Num();
	if (RefVertCount == 0)
	{
		return MCPError(TEXT("Reference mesh has no vertices"));
	}

	for (FVertexID VertID : RefVerts.GetElementIDs())
	{
		Center += RefPositions[VertID];
	}
	Center /= (float)RefVertCount;

	// Apply the SAME offset to ALL meshes
	TArray<TSharedPtr<FJsonValue>> ResultArray;
	for (int32 i = 0; i < Meshes.Num(); i++)
	{
		FMeshDescription* MeshDesc = Meshes[i]->GetMeshDescription(0);
		if (!MeshDesc) continue;

		FVertexArray& Verts = MeshDesc->Vertices();
		TVertexAttributesRef<FVector3f> Positions = MeshDesc->GetVertexPositions();

		for (FVertexID VertID : Verts.GetElementIDs())
		{
			Positions[VertID] -= Center;
		}

		Meshes[i]->CommitMeshDescription(0);
		Meshes[i]->Build(false);
		Meshes[i]->PostEditChange();
		Meshes[i]->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(AssetPaths[i], false);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), AssetPaths[i]);
		Entry->SetNumberField(TEXT("vertexCount"), Verts.Num());
		ResultArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetArrayField(TEXT("meshes"), ResultArray);
	Result->SetStringField(TEXT("offsetApplied"), FString::Printf(TEXT("(%.2f, %.2f, %.2f)"), Center.X, Center.Y, Center.Z));
	Result->SetNumberField(TEXT("meshCount"), Meshes.Num());
	// No rollback: destructive/external — vertex offsets applied non-idempotently;
	// re-running shifts the pivot again. Not natural-key idempotent.

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FAssetHandlers::AddSocket(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString SocketName;
	if (auto Err = RequireString(Params, TEXT("socketName"), SocketName)) return Err;

	FVector RelLoc = FVector::ZeroVector;
	FRotator RelRot = FRotator::ZeroRotator;
	FVector RelScale = FVector::OneVector;

	if (const TSharedPtr<FJsonObject>* LocObj; Params->TryGetObjectField(TEXT("relativeLocation"), LocObj))
	{
		RelLoc.X = (*LocObj)->GetNumberField(TEXT("x"));
		RelLoc.Y = (*LocObj)->GetNumberField(TEXT("y"));
		RelLoc.Z = (*LocObj)->GetNumberField(TEXT("z"));
	}
	if (const TSharedPtr<FJsonObject>* RotObj; Params->TryGetObjectField(TEXT("relativeRotation"), RotObj))
	{
		RelRot.Pitch = (*RotObj)->GetNumberField(TEXT("pitch"));
		RelRot.Yaw   = (*RotObj)->GetNumberField(TEXT("yaw"));
		RelRot.Roll  = (*RotObj)->GetNumberField(TEXT("roll"));
	}
	if (const TSharedPtr<FJsonObject>* ScaleObj; Params->TryGetObjectField(TEXT("relativeScale"), ScaleObj))
	{
		RelScale.X = (*ScaleObj)->GetNumberField(TEXT("x"));
		RelScale.Y = (*ScaleObj)->GetNumberField(TEXT("y"));
		RelScale.Z = (*ScaleObj)->GetNumberField(TEXT("z"));
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Could not load asset '%s'"), *AssetPath));
	}

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	// Try StaticMesh first
	if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
	{
		for (UStaticMeshSocket* Existing : SM->Sockets)
		{
			if (Existing && Existing->SocketName == FName(*SocketName))
			{
				if (OnConflict == TEXT("error"))
				{
					return MCPError(FString::Printf(TEXT("Socket '%s' already exists"), *SocketName));
				}
				auto ExistingResult = MCPSuccess();
				MCPSetExisted(ExistingResult);
				ExistingResult->SetStringField(TEXT("socketName"), SocketName);
				ExistingResult->SetStringField(TEXT("meshType"), TEXT("StaticMesh"));
				return MCPResult(ExistingResult);
			}
		}

		UStaticMeshSocket* NewSocket = NewObject<UStaticMeshSocket>(SM);
		NewSocket->SocketName = FName(*SocketName);
		NewSocket->RelativeLocation = RelLoc;
		NewSocket->RelativeRotation = RelRot;
		NewSocket->RelativeScale = RelScale;
		SM->Modify();
		SM->Sockets.Add(NewSocket);
		SM->MarkPackageDirty();

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("socketName"), SocketName);
		Result->SetStringField(TEXT("meshType"), TEXT("StaticMesh"));
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("socketName"), SocketName);
		MCPSetRollback(Result, TEXT("remove_socket"), Payload);
		return MCPResult(Result);
	}

	// Try SkeletalMesh
	if (USkeletalMesh* SKM = Cast<USkeletalMesh>(Asset))
	{
		FString BoneName = OptionalString(Params, TEXT("boneName"), TEXT("root"));

		for (USkeletalMeshSocket* Existing : SKM->GetMeshOnlySocketList())
		{
			if (Existing && Existing->SocketName == FName(*SocketName))
			{
				if (OnConflict == TEXT("error"))
				{
					return MCPError(FString::Printf(TEXT("Socket '%s' already exists"), *SocketName));
				}
				auto ExistingResult = MCPSuccess();
				MCPSetExisted(ExistingResult);
				ExistingResult->SetStringField(TEXT("socketName"), SocketName);
				ExistingResult->SetStringField(TEXT("meshType"), TEXT("SkeletalMesh"));
				return MCPResult(ExistingResult);
			}
		}

		USkeletalMeshSocket* NewSocket = NewObject<USkeletalMeshSocket>(SKM);
		NewSocket->SocketName = FName(*SocketName);
		NewSocket->BoneName = FName(*BoneName);
		NewSocket->RelativeLocation = RelLoc;
		NewSocket->RelativeRotation = RelRot;
		NewSocket->RelativeScale = RelScale;
		SKM->GetMeshOnlySocketList().Add(NewSocket);
		SKM->MarkPackageDirty();
		SKM->PostEditChange();

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("socketName"), SocketName);
		Result->SetStringField(TEXT("boneName"), BoneName);
		Result->SetStringField(TEXT("meshType"), TEXT("SkeletalMesh"));
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("socketName"), SocketName);
		MCPSetRollback(Result, TEXT("remove_socket"), Payload);
		return MCPResult(Result);
	}

	return MCPError(FString::Printf(TEXT("'%s' is not a StaticMesh or SkeletalMesh"), *AssetPath));
}

TSharedPtr<FJsonValue> FAssetHandlers::RemoveSocket(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString SocketName;
	if (auto Err = RequireString(Params, TEXT("socketName"), SocketName)) return Err;

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Could not load asset '%s'"), *AssetPath));
	}

	if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
	{
		for (int32 i = 0; i < SM->Sockets.Num(); ++i)
		{
			if (SM->Sockets[i] && SM->Sockets[i]->SocketName == FName(*SocketName))
			{
				SM->Modify();
				SM->Sockets.RemoveAt(i);
				SM->MarkPackageDirty();

				auto Result = MCPSuccess();
				Result->SetStringField(TEXT("removed"), SocketName);
				Result->SetBoolField(TEXT("deleted"), true);
				return MCPResult(Result);
			}
		}
		// Idempotent: socket already absent.
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("socketName"), SocketName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	if (USkeletalMesh* SKM = Cast<USkeletalMesh>(Asset))
	{
		auto& Sockets = SKM->GetMeshOnlySocketList();
		for (int32 i = 0; i < Sockets.Num(); ++i)
		{
			if (Sockets[i] && Sockets[i]->SocketName == FName(*SocketName))
			{
				Sockets.RemoveAt(i);
				SKM->MarkPackageDirty();
				SKM->PostEditChange();

				auto Result = MCPSuccess();
				Result->SetStringField(TEXT("removed"), SocketName);
				Result->SetBoolField(TEXT("deleted"), true);
				return MCPResult(Result);
			}
		}
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("socketName"), SocketName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	return MCPError(FString::Printf(TEXT("'%s' is not a StaticMesh or SkeletalMesh"), *AssetPath));
}

TSharedPtr<FJsonValue> FAssetHandlers::ListSockets(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Could not load asset '%s'"), *AssetPath));
	}

	auto Result = MCPSuccess();
	TArray<TSharedPtr<FJsonValue>> SocketArray;

	if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
	{
		for (UStaticMeshSocket* Socket : SM->Sockets)
		{
			if (!Socket) continue;
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("name"), Socket->SocketName.ToString());
			S->SetStringField(TEXT("tag"), Socket->Tag);

			TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
			Loc->SetNumberField(TEXT("x"), Socket->RelativeLocation.X);
			Loc->SetNumberField(TEXT("y"), Socket->RelativeLocation.Y);
			Loc->SetNumberField(TEXT("z"), Socket->RelativeLocation.Z);
			S->SetObjectField(TEXT("relativeLocation"), Loc);

			TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
			Rot->SetNumberField(TEXT("pitch"), Socket->RelativeRotation.Pitch);
			Rot->SetNumberField(TEXT("yaw"), Socket->RelativeRotation.Yaw);
			Rot->SetNumberField(TEXT("roll"), Socket->RelativeRotation.Roll);
			S->SetObjectField(TEXT("relativeRotation"), Rot);

			TSharedPtr<FJsonObject> Scale = MakeShared<FJsonObject>();
			Scale->SetNumberField(TEXT("x"), Socket->RelativeScale.X);
			Scale->SetNumberField(TEXT("y"), Socket->RelativeScale.Y);
			Scale->SetNumberField(TEXT("z"), Socket->RelativeScale.Z);
			S->SetObjectField(TEXT("relativeScale"), Scale);

			SocketArray.Add(MakeShared<FJsonValueObject>(S));
		}
		Result->SetStringField(TEXT("meshType"), TEXT("StaticMesh"));
	}
	else if (USkeletalMesh* SKM = Cast<USkeletalMesh>(Asset))
	{
		for (USkeletalMeshSocket* Socket : SKM->GetMeshOnlySocketList())
		{
			if (!Socket) continue;
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("name"), Socket->SocketName.ToString());
			S->SetStringField(TEXT("boneName"), Socket->BoneName.ToString());

			TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
			Loc->SetNumberField(TEXT("x"), Socket->RelativeLocation.X);
			Loc->SetNumberField(TEXT("y"), Socket->RelativeLocation.Y);
			Loc->SetNumberField(TEXT("z"), Socket->RelativeLocation.Z);
			S->SetObjectField(TEXT("relativeLocation"), Loc);

			TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
			Rot->SetNumberField(TEXT("pitch"), Socket->RelativeRotation.Pitch);
			Rot->SetNumberField(TEXT("yaw"), Socket->RelativeRotation.Yaw);
			Rot->SetNumberField(TEXT("roll"), Socket->RelativeRotation.Roll);
			S->SetObjectField(TEXT("relativeRotation"), Rot);

			TSharedPtr<FJsonObject> Scale = MakeShared<FJsonObject>();
			Scale->SetNumberField(TEXT("x"), Socket->RelativeScale.X);
			Scale->SetNumberField(TEXT("y"), Socket->RelativeScale.Y);
			Scale->SetNumberField(TEXT("z"), Socket->RelativeScale.Z);
			S->SetObjectField(TEXT("relativeScale"), Scale);

			SocketArray.Add(MakeShared<FJsonValueObject>(S));
		}
		Result->SetStringField(TEXT("meshType"), TEXT("SkeletalMesh"));
	}
	else
	{
		return MCPError(FString::Printf(TEXT("'%s' is not a StaticMesh or SkeletalMesh"), *AssetPath));
	}

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("socketCount"), SocketArray.Num());
	Result->SetArrayField(TEXT("sockets"), SocketArray);

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// reload_package -- Force reload an asset package from disk (#53)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::ReloadPackage(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	UPackage* Package = Asset->GetOutermost();
	if (!Package)
	{
		return MCPError(TEXT("Could not get asset package"));
	}

	// Unload and reload the package
	FString PackageName = Package->GetName();
	FString PackageFileName;
	bool bSuccess = false;
	if (FPackageName::DoesPackageExist(PackageName, &PackageFileName))
	{
		// Reset loaders so we can reload
		ResetLoaders(Package);

		// Force garbage collection to release old references
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

		// Reload
		UObject* Reloaded = UEditorAssetLibrary::LoadAsset(AssetPath);
		bSuccess = (Reloaded != nullptr);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("packageName"), Package->GetName());
	Result->SetBoolField(TEXT("success"), bSuccess);
	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), TEXT("Package reload failed"));
	}

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FAssetHandlers::GetReferencers(const TSharedPtr<FJsonObject>& Params)
{
	TArray<FString> Packages;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Params->TryGetArrayField(TEXT("packages"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString S; if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) Packages.Add(S);
		}
	}
	else
	{
		FString Single;
		if (Params->TryGetStringField(TEXT("packagePath"), Single)) Packages.Add(Single);
	}
	if (Packages.Num() == 0) return MCPError(TEXT("Supply 'packages' (array) or 'packagePath'"));

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TSharedPtr<FJsonObject> ByPkg = MakeShared<FJsonObject>();
	int32 TotalRefs = 0;
	for (const FString& Pkg : Packages)
	{
		TArray<FName> Refs;
		AR.GetReferencers(FName(*Pkg), Refs, UE::AssetRegistry::EDependencyCategory::Package);
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FName& R : Refs) Out.Add(MakeShared<FJsonValueString>(R.ToString()));
		ByPkg->SetArrayField(Pkg, Out);
		TotalRefs += Refs.Num();
	}

	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("referencersByPackage"), ByPkg);
	Result->SetNumberField(TEXT("totalReferencers"), TotalRefs);
	Result->SetNumberField(TEXT("queriedPackages"), Packages.Num());
	return MCPResult(Result);
}

// ─── #155 asset(set_sk_material_slots) ──────────────────────────────
// Blueprint component property writes to SkeletalMeshComponent.OverrideMaterials
// are silently reverted by UE's ICH pipeline; the reliable path is to mutate
// USkeletalMesh.Materials directly. Accepts either slotName or slotIndex per
// entry. Missing slot names are reported, not skipped silently.
TSharedPtr<FJsonValue> FAssetHandlers::SetSkeletalMeshMaterialSlots(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* SlotsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("slots"), SlotsArr))
	{
		return MCPError(TEXT("Missing 'slots' array parameter"));
	}

	USkeletalMesh* Mesh = Cast<USkeletalMesh>(StaticLoadObject(USkeletalMesh::StaticClass(), nullptr, *AssetPath));
	if (!Mesh) return MCPError(FString::Printf(TEXT("SkeletalMesh not found: %s"), *AssetPath));

	Mesh->Modify();
	TArray<FSkeletalMaterial> Materials = Mesh->GetMaterials();

	TArray<TSharedPtr<FJsonValue>> Applied;
	TArray<FString> Errors;

	for (const TSharedPtr<FJsonValue>& SlotVal : *SlotsArr)
	{
		const TSharedPtr<FJsonObject>* SlotObjPtr = nullptr;
		if (!SlotVal.IsValid() || !SlotVal->TryGetObject(SlotObjPtr)) continue;
		const TSharedPtr<FJsonObject>& Slot = *SlotObjPtr;

		FString MaterialPath;
		if (!Slot->TryGetStringField(TEXT("materialPath"), MaterialPath))
		{
			Errors.Add(TEXT("slot entry missing 'materialPath'"));
			continue;
		}

		UMaterialInterface* Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MaterialPath));
		if (!Material)
		{
			Errors.Add(FString::Printf(TEXT("material not found: %s"), *MaterialPath));
			continue;
		}

		int32 Index = INDEX_NONE;
		double SlotIdxNum = 0;
		if (Slot->TryGetNumberField(TEXT("slotIndex"), SlotIdxNum))
		{
			Index = (int32)SlotIdxNum;
		}
		else
		{
			FString SlotName;
			if (Slot->TryGetStringField(TEXT("slotName"), SlotName))
			{
				const FName Target(*SlotName);
				for (int32 I = 0; I < Materials.Num(); ++I)
				{
					if (Materials[I].MaterialSlotName == Target)
					{
						Index = I; break;
					}
				}
				if (Index == INDEX_NONE)
				{
					Errors.Add(FString::Printf(TEXT("slotName '%s' not found on %s"), *SlotName, *AssetPath));
					continue;
				}
			}
		}

		if (Index < 0 || Index >= Materials.Num())
		{
			Errors.Add(FString::Printf(TEXT("slotIndex %d out of range (mesh has %d slots)"), Index, Materials.Num()));
			continue;
		}

		Materials[Index].MaterialInterface = Material;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("slotIndex"), Index);
		Entry->SetStringField(TEXT("slotName"), Materials[Index].MaterialSlotName.ToString());
		Entry->SetStringField(TEXT("materialPath"), MaterialPath);
		Applied.Add(MakeShared<FJsonValueObject>(Entry));
	}

	Mesh->SetMaterials(Materials);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Mesh, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("slotCount"), Materials.Num());
	Result->SetArrayField(TEXT("applied"), Applied);
	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrArr;
		for (const FString& E : Errors) ErrArr.Add(MakeShared<FJsonValueString>(E));
		Result->SetArrayField(TEXT("errors"), ErrArr);
	}
	return MCPResult(Result);
}

// ─── #155 asset(diagnose_registry) ──────────────────────────────────
// Explains the gap between disk state and the in-memory AssetRegistry.
// Returns on-disk vs registry-including-memory counts so callers can
// recognise pending-kill ghost entries after delete(). reconcile=true
// forces a synchronous rescan (matches the Python workaround).
TSharedPtr<FJsonValue> FAssetHandlers::DiagnoseRegistry(const TSharedPtr<FJsonObject>& Params)
{
	FString Path;
	if (auto Err = RequireString(Params, TEXT("path"), Path)) return Err;

	const bool bReconcile = OptionalBool(Params, TEXT("reconcile"), false);
	const bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	if (bReconcile)
	{
		AR.ScanPathsSynchronous({ Path }, /*bForceRescan=*/true, /*bIgnoreDenyListScanFilters=*/true);
	}

	FARFilter FilterDisk;
	FilterDisk.PackagePaths.Add(FName(*Path));
	FilterDisk.bRecursivePaths = bRecursive;
	FilterDisk.bIncludeOnlyOnDiskAssets = true;
	TArray<FAssetData> OnDisk;
	AR.GetAssets(FilterDisk, OnDisk);

	FARFilter FilterAll = FilterDisk;
	FilterAll.bIncludeOnlyOnDiskAssets = false;
	TArray<FAssetData> InMemoryIncluded;
	AR.GetAssets(FilterAll, InMemoryIncluded);

	TSet<FName> DiskSet;
	for (const FAssetData& D : OnDisk) DiskSet.Add(D.PackageName);

	TArray<TSharedPtr<FJsonValue>> GhostArr;
	for (const FAssetData& A : InMemoryIncluded)
	{
		if (!DiskSet.Contains(A.PackageName))
		{
			GhostArr.Add(MakeShared<FJsonValueString>(A.GetObjectPathString()));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), Path);
	Result->SetBoolField(TEXT("recursive"), bRecursive);
	Result->SetBoolField(TEXT("reconciled"), bReconcile);
	Result->SetNumberField(TEXT("onDiskCount"), OnDisk.Num());
	Result->SetNumberField(TEXT("inMemoryIncludedCount"), InMemoryIncluded.Num());
	Result->SetNumberField(TEXT("ghostCount"), GhostArr.Num());
	Result->SetArrayField(TEXT("ghostPaths"), GhostArr);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// v1.0.0-rc.3 — #193 get_mesh_bounds
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::GetMeshBounds(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	REQUIRE_ASSET(UStaticMesh, Mesh, AssetPath);

	FBox BoundingBox = Mesh->GetBoundingBox();

	FVector Min = BoundingBox.Min;
	FVector Max = BoundingBox.Max;
	FVector Extent = BoundingBox.GetExtent();
	FVector Center = BoundingBox.GetCenter();

	TSharedPtr<FJsonObject> MinObj = MakeShared<FJsonObject>();
	MinObj->SetNumberField(TEXT("x"), Min.X);
	MinObj->SetNumberField(TEXT("y"), Min.Y);
	MinObj->SetNumberField(TEXT("z"), Min.Z);

	TSharedPtr<FJsonObject> MaxObj = MakeShared<FJsonObject>();
	MaxObj->SetNumberField(TEXT("x"), Max.X);
	MaxObj->SetNumberField(TEXT("y"), Max.Y);
	MaxObj->SetNumberField(TEXT("z"), Max.Z);

	TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
	ExtentObj->SetNumberField(TEXT("x"), Extent.X);
	ExtentObj->SetNumberField(TEXT("y"), Extent.Y);
	ExtentObj->SetNumberField(TEXT("z"), Extent.Z);

	TSharedPtr<FJsonObject> CenterObj = MakeShared<FJsonObject>();
	CenterObj->SetNumberField(TEXT("x"), Center.X);
	CenterObj->SetNumberField(TEXT("y"), Center.Y);
	CenterObj->SetNumberField(TEXT("z"), Center.Z);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetObjectField(TEXT("min"), MinObj);
	Result->SetObjectField(TEXT("max"), MaxObj);
	Result->SetObjectField(TEXT("boxExtent"), ExtentObj);
	Result->SetObjectField(TEXT("boxCenter"), CenterObj);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// v1.0.0-rc.3 — #177 get_mesh_collision
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::GetMeshCollision(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	REQUIRE_ASSET(UStaticMesh, Mesh, AssetPath);

	UBodySetup* BodySetup = Mesh->GetBodySetup();
	if (!BodySetup)
	{
		return MCPError(FString::Printf(TEXT("No BodySetup found on mesh: %s"), *AssetPath));
	}

	// Collision trace flag as string
	FString TraceFlag;
	switch (BodySetup->CollisionTraceFlag)
	{
	case CTF_UseDefault:             TraceFlag = TEXT("CTF_UseDefault"); break;
	case CTF_UseSimpleAndComplex:    TraceFlag = TEXT("CTF_UseSimpleAndComplex"); break;
	case CTF_UseSimpleAsComplex:     TraceFlag = TEXT("CTF_UseSimpleAsComplex"); break;
	case CTF_UseComplexAsSimple:     TraceFlag = TEXT("CTF_UseComplexAsSimple"); break;
	default:                         TraceFlag = TEXT("Unknown"); break;
	}

	const FKAggregateGeom& AggGeom = BodySetup->AggGeom;

	int32 NumConvex  = AggGeom.ConvexElems.Num();
	int32 NumBox     = AggGeom.BoxElems.Num();
	int32 NumSphere  = AggGeom.SphereElems.Num();
	int32 NumSphyl   = AggGeom.SphylElems.Num();

	bool bHasSimple = (NumConvex + NumBox + NumSphere + NumSphyl) > 0;

	// Complex collision is available when the trace flag allows it
	bool bHasComplex = (BodySetup->CollisionTraceFlag == CTF_UseDefault
		|| BodySetup->CollisionTraceFlag == CTF_UseSimpleAndComplex
		|| BodySetup->CollisionTraceFlag == CTF_UseComplexAsSimple);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("collisionTraceFlag"), TraceFlag);
	Result->SetBoolField(TEXT("hasSimpleCollision"), bHasSimple);
	Result->SetBoolField(TEXT("hasComplexCollision"), bHasComplex);
	Result->SetNumberField(TEXT("numConvexElems"), NumConvex);
	Result->SetNumberField(TEXT("numBoxElems"), NumBox);
	Result->SetNumberField(TEXT("numSphereElems"), NumSphere);
	Result->SetNumberField(TEXT("numSphylElems"), NumSphyl);

	// NavCollision info (#167)
	Result->SetBoolField(TEXT("bCanEverAffectNavigation"), Mesh->bHasNavigationData);
	if (Mesh->GetNavCollision())
	{
		Result->SetBoolField(TEXT("hasNavCollision"), true);
		Result->SetBoolField(TEXT("bIsDynamicObstacle"), Mesh->GetNavCollision()->IsDynamicObstacle());
	}
	else
	{
		Result->SetBoolField(TEXT("hasNavCollision"), false);
	}

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// v1.0.0-rc.5 — #167 set_mesh_nav
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::SetMeshNav(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	REQUIRE_ASSET(UStaticMesh, Mesh, AssetPath);

	bool bChanged = false;

	bool bHasNavData = false;
	if (Params->TryGetBoolField(TEXT("bHasNavigationData"), bHasNavData))
	{
		Mesh->bHasNavigationData = bHasNavData;
		bChanged = true;
	}

	bool bClearNavCollision = false;
	if (Params->TryGetBoolField(TEXT("clearNavCollision"), bClearNavCollision) && bClearNavCollision)
	{
		Mesh->SetNavCollision(nullptr);
		bChanged = true;
	}

	if (!bChanged)
	{
		return MCPError(TEXT("No changes requested. Provide bHasNavigationData and/or clearNavCollision."));
	}

	Mesh->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetBoolField(TEXT("bHasNavigationData"), Mesh->bHasNavigationData);
	Result->SetBoolField(TEXT("hasNavCollision"), Mesh->GetNavCollision() != nullptr);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// v1.0.0-rc.3 — #192 move_folder
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::MoveFolder(const TSharedPtr<FJsonObject>& Params)
{
	FString SourcePath;
	if (auto Err = RequireString(Params, TEXT("sourcePath"), SourcePath)) return Err;

	FString DestinationPath;
	if (auto Err = RequireString(Params, TEXT("destinationPath"), DestinationPath)) return Err;

	// Ensure paths don't have trailing slashes for consistent prefix replacement
	SourcePath.RemoveFromEnd(TEXT("/"));
	DestinationPath.RemoveFromEnd(TEXT("/"));

	// Scan source path to discover all assets
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AR.ScanPathsSynchronous({ SourcePath }, /*bForceRescan=*/true);

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*SourcePath));
	Filter.bRecursivePaths = true;

	TArray<FAssetData> FoundAssets;
	AR.GetAssets(Filter, FoundAssets);

	if (FoundAssets.Num() == 0)
	{
		return MCPError(FString::Printf(TEXT("No assets found under '%s'"), *SourcePath));
	}

	// Build rename data: replace source prefix with destination prefix
	TArray<FAssetRenameData> BatchRenames;
	for (const FAssetData& AssetData : FoundAssets)
	{
		UObject* Asset = AssetData.GetAsset();
		if (!Asset) continue;

		FString OldPackagePath = FPaths::GetPath(AssetData.PackageName.ToString());
		FString NewPackagePath = OldPackagePath;
		// Replace source prefix with destination prefix
		if (NewPackagePath.StartsWith(SourcePath))
		{
			NewPackagePath = DestinationPath + NewPackagePath.Mid(SourcePath.Len());
		}

		FString AssetName = AssetData.AssetName.ToString();
		FAssetRenameData RenameData(Asset, NewPackagePath, AssetName);
		BatchRenames.Add(RenameData);
	}

	if (BatchRenames.Num() == 0)
	{
		return MCPError(TEXT("Failed to load any assets for renaming"));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	bool bOk = AssetTools.RenameAssets(BatchRenames);

	// Count how many actually landed at the destination
	int32 Succeeded = 0;
	for (const FAssetRenameData& Data : BatchRenames)
	{
		const FString DestFullPath = FString::Printf(TEXT("%s/%s.%s"),
			*Data.NewPackagePath, *Data.NewName, *Data.NewName);
		if (UEditorAssetLibrary::DoesAssetExist(DestFullPath))
		{
			Succeeded++;
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("sourcePath"), SourcePath);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetNumberField(TEXT("totalAssets"), FoundAssets.Num());
	Result->SetNumberField(TEXT("renamedCount"), Succeeded);
	Result->SetBoolField(TEXT("allSucceeded"), bOk && Succeeded == BatchRenames.Num());
	return MCPResult(Result);
}
