#include "AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
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

// ============================================================================
// DataTable handlers
// ============================================================================

TSharedPtr<FJsonValue> FAssetHandlers::ImportDataTableJson(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString JsonString;
	if (auto Err = RequireString(Params, TEXT("jsonString"), JsonString)) return Err;

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
		ErrResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Import completed with %d error(s)"), Errors.Num()));
		return MCPResult(ErrResult);
	}

	DataTable->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("rowCount"), DataTable->GetRowMap().Num());
	Result->SetStringField(TEXT("message"), TEXT("DataTable imported successfully from JSON"));
	// No rollback: destructive — import replaces table contents.

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ExportDataTableJson(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

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

	FString JsonString = DataTable->GetTableAsJSON(EDataTableExportFlags::UseJsonObjectsForStructs);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("json"), JsonString);
	Result->SetNumberField(TEXT("rowCount"), DataTable->GetRowMap().Num());

	return MCPResult(Result);
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

	Task->RemoveFromRoot();
	FbxFactory->RemoveFromRoot();

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
			auto Result = MCPSuccess();
			Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("Skeleton not found: %s, importing without skeleton target"), *SkeletonPath));
			// Continue with import — don't return here, just note the warning
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

	Task->RemoveFromRoot();
	FbxFactory->RemoveFromRoot();

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

	Task->RemoveFromRoot();
	FbxFactory->RemoveFromRoot();

	return MCPResult(Result);
}

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

	Task->RemoveFromRoot();
	TextureFactory->RemoveFromRoot();

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

	// Idempotency: check if the DataTable already exists at the target path.
	const FString ProbePath = PackagePath + TEXT("/") + Name + TEXT(".") + Name;
	if (UDataTable* Existing = LoadObject<UDataTable>(nullptr, *ProbePath))
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("DataTable '%s' already exists"), *ProbePath));
		}
		auto ExistingResult = MCPSuccess();
		MCPSetExisted(ExistingResult);
		ExistingResult->SetStringField(TEXT("name"), Name);
		ExistingResult->SetStringField(TEXT("packagePath"), PackagePath);
		ExistingResult->SetStringField(TEXT("assetPath"), Existing->GetPathName());
		ExistingResult->SetStringField(TEXT("rowStruct"), Existing->RowStruct ? Existing->RowStruct->GetName() : TEXT(""));
		ExistingResult->SetNumberField(TEXT("rowCount"), Existing->GetRowMap().Num());
		return MCPResult(ExistingResult);
	}

	// Find the row struct type
	UScriptStruct* ScriptStruct = nullptr;

	// First try as a full path
	ScriptStruct = LoadObject<UScriptStruct>(nullptr, *RowStruct);

	// If not found, try finding by short name
	if (!ScriptStruct)
	{
		// Try common patterns: search for the struct by name in all packages
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

	// Create the DataTable asset
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UDataTableFactory* Factory = NewObject<UDataTableFactory>();
	Factory->Struct = ScriptStruct;

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UDataTable::StaticClass(), Factory);
	if (!NewAsset)
	{
		return MCPError(FString::Printf(TEXT("Failed to create DataTable: %s/%s"), *PackagePath, *Name));
	}

	UDataTable* DataTable = Cast<UDataTable>(NewAsset);
	const FString AssetPath = NewAsset->GetPathName();

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
