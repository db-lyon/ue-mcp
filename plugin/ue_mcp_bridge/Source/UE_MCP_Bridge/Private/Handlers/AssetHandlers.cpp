#include "AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "HandlerAssetCreate.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "Exporters/Exporter.h"
#include "AssetExportTask.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "EditorFramework/AssetImportData.h"
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

// World rename redirector cleanup (UEditorLoadingAndSavingUtils ships in FileHelpers.h, already included)
#include "UObject/ObjectRedirector.h"

// Collision / BodySetup
#include "PhysicsEngine/BodySetup.h"
#include "AI/Navigation/NavCollisionBase.h"

// ─── Protected mount guardrail ──────────────────────────────────────────
// Engine-shipped content (/Engine/, /Script/, /Memory/, /Temp/) and Verse
// runtime classes must never be mutated through the bridge. UE's
// UEditorAssetLibrary::DeleteAsset will happily destroy files under
// <engineRoot>/Engine/Content/ if not stopped — verified the hard way.
// Apply this check to every handler that deletes, moves, or renames an
// asset. Plugin content roots (mounted under /<PluginName>/) are NOT
// protected here; per-project plugin content is expected to be writable.
namespace
{
	bool IsProtectedAssetPath(const FString& Path)
	{
		FString P = Path;
		P.TrimStartAndEndInline();
		if (P.IsEmpty()) return false;
		// Tolerate leading whitespace and the surface form (no leading slash).
		if (!P.StartsWith(TEXT("/"))) P = TEXT("/") + P;
		const FString L = P.ToLower();
		if (L.StartsWith(TEXT("/engine/"))) return true;
		if (L.StartsWith(TEXT("/script/"))) return true;
		if (L.StartsWith(TEXT("/memory/"))) return true;
		if (L.StartsWith(TEXT("/temp/"))) return true;
		// Verse runtime objects surface as /Script/CoreUObject.* etc.
		if (L.Contains(TEXT("/script/"))) return true;
		return false;
	}

	TSharedPtr<FJsonValue> MakeProtectedPathError(const FString& Path)
	{
		return MCPError(FString::Printf(
			TEXT("Refusing to mutate protected mount: %s. Engine, /Script/, /Memory/, /Temp/ are read-only via the bridge."),
			*Path));
	}

	// Split "/Game/Foo/Bar.Bar" (or "/Game/Foo/Bar") into mount "/Game/" + rel "Foo/Bar".
	// Returns false if the path is malformed or has no mount segment.
	bool SplitMountAndRel(const FString& AssetOrPackagePath, FString& OutMountRoot, FString& OutRelPath, FString& OutPackageName, FString& OutAssetName)
	{
		FString Pkg = AssetOrPackagePath;
		Pkg.TrimStartAndEndInline();
		if (Pkg.IsEmpty() || !Pkg.StartsWith(TEXT("/"))) return false;
		if (Pkg.Contains(TEXT(".")))
		{
			FString Name;
			FString PkgOnly;
			Pkg.Split(TEXT("."), &PkgOnly, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			OutAssetName = Name;
			Pkg = PkgOnly;
		}
		else
		{
			OutAssetName = FPaths::GetBaseFilename(Pkg);
		}
		OutPackageName = Pkg;
		int32 SecondSlash = INDEX_NONE;
		if (!Pkg.RightChop(1).FindChar(TEXT('/'), SecondSlash)) return false;
		OutMountRoot = Pkg.Left(SecondSlash + 2);     // "/Game/"
		OutRelPath = Pkg.RightChop(SecondSlash + 2);  // "Foo/Bar"
		return !OutRelPath.IsEmpty();
	}

	// Look up an asset's class via the AssetRegistry without forcing a load.
	// Returns the short class name (e.g., "World") or NAME_None when not found.
	FName GetAssetClassName(const FString& AssetOrPackagePath)
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& Reg = ARM.Get();
		FString Mount, Rel, Pkg, Name;
		if (!SplitMountAndRel(AssetOrPackagePath, Mount, Rel, Pkg, Name)) return NAME_None;
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *Pkg, *Name);
		FAssetData Data = Reg.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
		if (!Data.IsValid())
		{
			TArray<FAssetData> Assets;
			Reg.GetAssetsByPackageName(FName(*Pkg), Assets);
			if (Assets.Num() > 0) Data = Assets[0];
		}
		if (!Data.IsValid()) return NAME_None;
		return Data.AssetClassPath.GetAssetName();
	}

	bool IsWorldAsset(const FString& AssetOrPackagePath)
	{
		return GetAssetClassName(AssetOrPackagePath) == FName(TEXT("World"));
	}
}

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
	Registry.RegisterHandler(TEXT("save_all_dirty"), &SaveAllDirty);
	Registry.RegisterHandler(TEXT("list_textures"), &ListTextures);

	// FBX import handlers
	Registry.RegisterHandler(TEXT("import_static_mesh"), &ImportStaticMesh);
	Registry.RegisterHandler(TEXT("import_skeletal_mesh"), &ImportSkeletalMesh);
	Registry.RegisterHandler(TEXT("import_animation"), &ImportAnimation);

	// Texture handlers
	Registry.RegisterHandler(TEXT("import_texture"), &ImportTexture);
	Registry.RegisterHandler(TEXT("import_texture_batch"), &ImportTextureBatch);
	Registry.RegisterHandler(TEXT("get_texture_info"), &ListTextureProperties);
	Registry.RegisterHandler(TEXT("set_texture_settings"), &SetTextureProperties);

	// Mesh handlers
	Registry.RegisterHandler(TEXT("set_mesh_material"), &SetMeshMaterial);
	Registry.RegisterHandler(TEXT("recenter_pivot"), &RecenterPivot);

	// Socket handlers
	Registry.RegisterHandler(TEXT("add_socket"), &AddSocket);
	Registry.RegisterHandler(TEXT("set_socket_transform"), &SetSocketTransform);
	Registry.RegisterHandler(TEXT("set_asset_property"), &SetAssetProperty);
	Registry.RegisterHandler(TEXT("set_texture_settings_by_type"), &SetTextureSettingsByType);
	Registry.RegisterHandler(TEXT("create_interchange_pipeline"), &CreateInterchangePipeline);
	Registry.RegisterHandler(TEXT("remove_socket"), &RemoveSocket);
	Registry.RegisterHandler(TEXT("list_sockets"), &ListSockets);
	Registry.RegisterHandler(TEXT("reload_package"), &ReloadPackage);
	// #279: detect/recover stuck-unloadable assets
	Registry.RegisterHandler(TEXT("asset_health_check"), &HealthCheck);
	Registry.RegisterHandler(TEXT("force_reload_asset"), &ForceReload);

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
	Registry.RegisterHandler(TEXT("read_import_sources"), &ReadImportSources);
	Registry.RegisterHandler(TEXT("get_mesh_collision"), &GetMeshCollision);
	Registry.RegisterHandler(TEXT("set_mesh_nav"), &SetMeshNav);
	Registry.RegisterHandler(TEXT("move_folder"), &MoveFolder);
	Registry.RegisterHandler(TEXT("create_folder"), &CreateFolder);
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
	const FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game"));
	const bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);
	const int32 MaxResults = OptionalInt(Params, TEXT("maxResults"), 2000);
	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"));

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FAssetData> Found;
	Registry.GetAssetsByPath(FName(*Directory), Found, bRecursive);

	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FAssetData& Data : Found)
	{
		if (Out.Num() >= MaxResults) break;
		const FString ClassName = Data.AssetClassPath.GetAssetName().ToString();
		if (!ClassFilter.IsEmpty() && !ClassName.Equals(ClassFilter, ESearchCase::IgnoreCase) && !ClassName.Contains(ClassFilter))
		{
			continue;
		}
		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), Data.PackageName.ToString());
		Item->SetStringField(TEXT("name"), Data.AssetName.ToString());
		Item->SetStringField(TEXT("className"), ClassName);
		Out.Add(MakeShared<FJsonValueObject>(Item));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("directory"), Directory);
	Result->SetBoolField(TEXT("recursive"), bRecursive);
	Result->SetNumberField(TEXT("assetCount"), Out.Num());
	Result->SetArrayField(TEXT("assets"), Out);
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

	// Unified path: always use IAssetRegistry::GetAssets (with PackagePaths) so
	// substring matches hit AssetName + ObjectPath consistently. The previous
	// default branch leaned on UEditorAssetLibrary::ListAssets which returned
	// false negatives for assets that were indexed but not yet visible to that
	// API path (#256).
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.bRecursivePaths = true;
	if (!bSearchAll)
	{
		Filter.PackagePaths.Add(FName(*Directory));
	}
	else if (bHasDirectory)
	{
		// searchAll + directory = scope to that directory across mounted roots.
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
			if (Query.Contains(TEXT("*")))
			{
				if (!AssetPath.MatchesWildcard(Query) && !AssetName.MatchesWildcard(Query))
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
	Result->SetStringField(TEXT("searchScope"), bSearchAll ? (bHasDirectory ? Directory : TEXT("all")) : Directory);
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

// ─── #409: WP-aware world rename ────────────────────────────────────
// UEditorAssetLibrary::RenameAsset on a World Partition .umap renames only
// the umap and silently orphans every actor in /Game/__ExternalActors__/<Level>/.
// Detect Worlds and route through IAssetTools::RenameAssets with the world +
// all external packages in a single atomic batch.
//
// Guards (in order):
//  1. Cross-mount renames refused (external-package GUIDs are mount-relative).
//  2. Active editor world: if it matches the source and isn't dirty, auto-swap
//     to a blank map (RenameAssets on the loaded world otherwise leaves the
//     editor pointing at a stale UWorld*); if dirty, refuse with a clear ask.
//  3. Dirty world or external packages: refuse - caller must save first.
//  4. Destination external folders already populated: refuse unless bForceMerge
//     (rollback path uses force to step over orphans left by the forward call).
//  5. Pre-load every external package and abort before any change if any fail
//     to load (no partial state at the engine level).
//
// On success the source-side redirectors are fixed up + deleted so the project
// doesn't accumulate 200+ stub redirectors per rename.
//
// Rollback is emitted unconditionally on a path that reached the mutation - if
// AssetTools.RenameAssets returns false mid-batch, the rollback descriptor
// still ships so the caller (or flow runner) can attempt recovery.
static TSharedPtr<FJsonValue> RenameWorldWithExternals(const FString& SourceAssetPath, const FString& DestAssetPath, bool bForceMerge)
{
	FString SrcMount, SrcRel, SrcPkg, SrcName;
	FString DstMount, DstRel, DstPkg, DstName;
	if (!SplitMountAndRel(SourceAssetPath, SrcMount, SrcRel, SrcPkg, SrcName))
		return MCPError(FString::Printf(TEXT("Invalid source package path: %s"), *SourceAssetPath));
	if (!SplitMountAndRel(DestAssetPath, DstMount, DstRel, DstPkg, DstName))
		return MCPError(FString::Printf(TEXT("Invalid destination package path: %s"), *DestAssetPath));

	if (SrcMount != DstMount)
	{
		return MCPError(FString::Printf(
			TEXT("Refusing to rename World across content mounts (%s -> %s). Cross-mount external-actor migration is unsafe via the bridge - move externals manually."),
			*SrcMount, *DstMount));
	}

	// Guard 2: active editor world. RenameAssets against the live UWorld leaves
	// the editor pointing at a stale pointer; the safe pattern is to swap to a
	// blank map first. We only do that automatically when the world isn't dirty
	// so we never silently lose unsaved actor edits.
	if (UWorld* EditorWorld = GetEditorWorld())
	{
		UPackage* EditorWorldPkg = EditorWorld->GetOutermost();
		if (EditorWorldPkg && EditorWorldPkg->GetName() == SrcPkg)
		{
			if (EditorWorldPkg->IsDirty())
			{
				return MCPError(FString::Printf(
					TEXT("Refusing to rename World currently open in editor with unsaved changes: %s. Save the level (asset.save) or discard, then re-run."),
					*SourceAssetPath));
			}
			UEditorLoadingAndSavingUtils::NewBlankMap(/*bSaveExistingMap*/ false);
		}
	}

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = ARM.Get();
	FAssetToolsModule& ATM = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = ATM.Get();

	const FString ExtActorsSrc  = SrcMount + TEXT("__ExternalActors__/")  + SrcRel;
	const FString ExtObjectsSrc = SrcMount + TEXT("__ExternalObjects__/") + SrcRel;
	const FString ExtActorsDst  = DstMount + TEXT("__ExternalActors__/")  + DstRel;
	const FString ExtObjectsDst = DstMount + TEXT("__ExternalObjects__/") + DstRel;

	// Skip redirector stubs left by prior renames - we don't want to drag them along.
	auto Gather = [&](const FString& Folder, TArray<FAssetData>& Out)
	{
		FARFilter F;
		F.PackagePaths.Add(FName(*Folder));
		F.bRecursivePaths = true;
		Registry.GetAssets(F, Out);
		Out.RemoveAll([](const FAssetData& D)
		{
			return D.AssetClassPath.GetAssetName() == FName(TEXT("ObjectRedirector"));
		});
	};

	TArray<FAssetData> DstActorsExisting, DstObjectsExisting;
	Gather(ExtActorsDst, DstActorsExisting);
	Gather(ExtObjectsDst, DstObjectsExisting);
	if (!bForceMerge && DstActorsExisting.Num() + DstObjectsExisting.Num() > 0)
	{
		return MCPError(FString::Printf(
			TEXT("Refusing to rename World: destination external folders already contain %d asset(s) at %s or %s. Pick a clean destination, or pass force=true to merge (used by rollback)."),
			DstActorsExisting.Num() + DstObjectsExisting.Num(), *ExtActorsDst, *ExtObjectsDst));
	}

	TArray<FAssetData> SrcActors, SrcObjects;
	Gather(ExtActorsSrc, SrcActors);
	Gather(ExtObjectsSrc, SrcObjects);

	UObject* World = UEditorAssetLibrary::LoadAsset(SourceAssetPath);
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("Failed to load World asset: %s. No changes made."), *SourceAssetPath));
	}

	// Guard 3: dirty world or externals. Silent loss vector if we let it through.
	TArray<FString> DirtyPackages;
	auto CheckDirty = [&](const FString& PkgName)
	{
		if (UPackage* P = FindPackage(nullptr, *PkgName))
		{
			if (P->IsDirty()) DirtyPackages.Add(PkgName);
		}
	};
	CheckDirty(SrcPkg);
	for (const FAssetData& D : SrcActors)  CheckDirty(D.PackageName.ToString());
	for (const FAssetData& D : SrcObjects) CheckDirty(D.PackageName.ToString());
	if (DirtyPackages.Num() > 0)
	{
		FString Examples;
		const int32 Show = FMath::Min(DirtyPackages.Num(), 5);
		for (int32 i = 0; i < Show; ++i)
		{
			if (i) Examples += TEXT(", ");
			Examples += DirtyPackages[i];
		}
		return MCPError(FString::Printf(
			TEXT("Refusing to rename World: %d package(s) have unsaved changes. Save with asset.save (or editor.save_dirty) first. Examples: %s"),
			DirtyPackages.Num(), *Examples));
	}

	TArray<FAssetRenameData> Batch;
	Batch.Reserve(1 + SrcActors.Num() + SrcObjects.Num());

	const FString NewWorldPkgPath = FPaths::GetPath(DstPkg);
	Batch.Emplace(TWeakObjectPtr<UObject>(World), NewWorldPkgPath, DstName);

	int32 ActorAdded = 0;
	int32 ObjectAdded = 0;
	TArray<FString> FailedLoad;

	auto AddExternals = [&](const TArray<FAssetData>& Assets, const FString& SrcFolder, const FString& DstFolder, int32& AddedCounter)
	{
		for (const FAssetData& Data : Assets)
		{
			const FString PkgName = Data.PackageName.ToString();
			if (!PkgName.StartsWith(SrcFolder + TEXT("/")))
			{
				FailedLoad.Add(PkgName + TEXT(" (path mismatch)"));
				continue;
			}
			const FString Suffix = PkgName.RightChop(SrcFolder.Len() + 1);
			const FString SubPath = FPaths::GetPath(Suffix);
			const FString NewPkgPath = SubPath.IsEmpty() ? DstFolder : (DstFolder + TEXT("/") + SubPath);
			const FString NewName = FPaths::GetBaseFilename(Suffix);

			UObject* Obj = Data.GetAsset();
			if (!Obj)
			{
				FailedLoad.Add(PkgName);
				continue;
			}
			Batch.Emplace(TWeakObjectPtr<UObject>(Obj), NewPkgPath, NewName);
			++AddedCounter;
		}
	};
	AddExternals(SrcActors,  ExtActorsSrc,  ExtActorsDst,  ActorAdded);
	AddExternals(SrcObjects, ExtObjectsSrc, ExtObjectsDst, ObjectAdded);

	if (FailedLoad.Num() > 0)
	{
		FString Examples;
		const int32 Show = FMath::Min(FailedLoad.Num(), 5);
		for (int32 i = 0; i < Show; ++i)
		{
			if (i) Examples += TEXT(", ");
			Examples += FailedLoad[i];
		}
		return MCPError(FString::Printf(
			TEXT("Failed to load %d external package(s) for World rename. Aborting before any change. Examples: %s"),
			FailedLoad.Num(), *Examples));
	}

	// Build response + rollback handle BEFORE the mutation. We've cached every
	// source path that's about to move, so the inverse is deterministic even if
	// the batch half-completes. Rollback passes force=true to step over orphans
	// left at the original destination by a partial forward run.
	auto R = MCPSuccess();
	R->SetStringField(TEXT("sourcePath"), SourceAssetPath);
	R->SetStringField(TEXT("destinationPath"), DestAssetPath);
	R->SetStringField(TEXT("kind"), TEXT("world"));
	R->SetNumberField(TEXT("externalActors"), ActorAdded);
	R->SetNumberField(TEXT("externalObjects"), ObjectAdded);
	R->SetNumberField(TEXT("totalRenamed"), Batch.Num());

	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("sourcePath"), DestAssetPath);
	RollbackPayload->SetStringField(TEXT("destinationPath"), SourceAssetPath);
	RollbackPayload->SetBoolField(TEXT("force"), true);
	MCPSetRollback(R, TEXT("rename_asset"), RollbackPayload);

	const bool bOk = AssetTools.RenameAssets(Batch);
	R->SetBoolField(TEXT("success"), bOk);

	if (!bOk)
	{
		// Build the error object by hand so the rollback descriptor survives -
		// MCPError() would drop it. The caller (flow runner) can invoke the
		// inverse rename to attempt recovery from a partial batch.
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetBoolField(TEXT("partial"), true);
		Err->SetStringField(TEXT("error"), FString::Printf(
			TEXT("IAssetTools::RenameAssets failed on World+externals batch (%d items: 1 world, %d actors, %d objects). Rollback descriptor is attached - invoke the inverse rename to recover."),
			Batch.Num(), ActorAdded, ObjectAdded));
		MCPSetRollback(Err, TEXT("rename_asset"), RollbackPayload);
		return MakeShared<FJsonValueObject>(Err);
	}

	// Source-side redirector cleanup: AssetTools.RenameAssets leaves a redirector
	// stub at every old path. With WP that's potentially hundreds of stubs - fix
	// up referencers and delete them so the project doesn't accumulate cruft.
	TArray<FAssetData> SourceRedirectors;
	auto GatherRedirectorsAt = [&](const FString& Folder)
	{
		FARFilter F;
		F.PackagePaths.Add(FName(*Folder));
		F.bRecursivePaths = true;
		F.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/CoreUObject"), TEXT("ObjectRedirector")));
		Registry.GetAssets(F, SourceRedirectors);
	};
	GatherRedirectorsAt(FPaths::GetPath(SrcPkg));
	GatherRedirectorsAt(ExtActorsSrc);
	GatherRedirectorsAt(ExtObjectsSrc);

	TArray<UObjectRedirector*> RedirectorObjects;
	RedirectorObjects.Reserve(SourceRedirectors.Num());
	for (const FAssetData& D : SourceRedirectors)
	{
		if (UObjectRedirector* Red = Cast<UObjectRedirector>(D.GetAsset()))
		{
			RedirectorObjects.Add(Red);
		}
	}
	int32 RedirectorsCleaned = RedirectorObjects.Num();
	if (RedirectorsCleaned > 0)
	{
		AssetTools.FixupReferencers(RedirectorObjects, /*bCheckoutDialogPrompt*/ false);
	}
	R->SetNumberField(TEXT("redirectorsCleaned"), RedirectorsCleaned);

	MCPSetUpdated(R);
	return MCPResult(R);
}

// ─── #409: orphan recovery ───────────────────────────────────────────
// Fast-path called when the source .umap is gone but the destination .umap
// exists. If externals are still at the source path (a previous rename
// orphaned them), migrate them into place. If they're already at the
// destination, return Existed.
static TSharedPtr<FJsonValue> ReconcileOrphanExternals(const FString& SourceAssetPath, const FString& DestAssetPath)
{
	FString SrcMount, SrcRel, SrcPkg, SrcName;
	FString DstMount, DstRel, DstPkg, DstName;
	if (!SplitMountAndRel(SourceAssetPath, SrcMount, SrcRel, SrcPkg, SrcName) ||
	    !SplitMountAndRel(DestAssetPath, DstMount, DstRel, DstPkg, DstName) ||
	    SrcMount != DstMount)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("sourcePath"), SourceAssetPath);
		Noop->SetStringField(TEXT("destinationPath"), DestAssetPath);
		return MCPResult(Noop);
	}

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = ARM.Get();

	const FString ExtActorsSrc  = SrcMount + TEXT("__ExternalActors__/")  + SrcRel;
	const FString ExtObjectsSrc = SrcMount + TEXT("__ExternalObjects__/") + SrcRel;
	const FString ExtActorsDst  = DstMount + TEXT("__ExternalActors__/")  + DstRel;
	const FString ExtObjectsDst = DstMount + TEXT("__ExternalObjects__/") + DstRel;

	auto Gather = [&](const FString& Folder, TArray<FAssetData>& Out)
	{
		FARFilter F;
		F.PackagePaths.Add(FName(*Folder));
		F.bRecursivePaths = true;
		Registry.GetAssets(F, Out);
		Out.RemoveAll([](const FAssetData& D)
		{
			return D.AssetClassPath.GetAssetName() == FName(TEXT("ObjectRedirector"));
		});
	};

	TArray<FAssetData> Orphans;
	Gather(ExtActorsSrc,  Orphans);
	const int32 OrphanActors = Orphans.Num();
	TArray<FAssetData> OrphanObjects;
	Gather(ExtObjectsSrc, OrphanObjects);
	Orphans.Append(OrphanObjects);

	if (Orphans.Num() == 0)
	{
		// True idempotent replay - everything already at destination.
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("sourcePath"), SourceAssetPath);
		Noop->SetStringField(TEXT("destinationPath"), DestAssetPath);
		return MCPResult(Noop);
	}

	FAssetToolsModule& ATM = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = ATM.Get();

	TArray<FAssetRenameData> Batch;
	Batch.Reserve(Orphans.Num());
	TArray<FString> FailedLoad;

	auto Remap = [&](const FString& PkgName, FString& OutPath, FString& OutName) -> bool
	{
		auto Try = [&](const FString& Src, const FString& Dst) -> bool
		{
			if (!PkgName.StartsWith(Src + TEXT("/"))) return false;
			const FString Suffix = PkgName.RightChop(Src.Len() + 1);
			const FString Sub = FPaths::GetPath(Suffix);
			OutPath = Sub.IsEmpty() ? Dst : (Dst + TEXT("/") + Sub);
			OutName = FPaths::GetBaseFilename(Suffix);
			return true;
		};
		return Try(ExtActorsSrc, ExtActorsDst) || Try(ExtObjectsSrc, ExtObjectsDst);
	};

	for (const FAssetData& Data : Orphans)
	{
		const FString PkgName = Data.PackageName.ToString();
		FString NewPkgPath, NewName;
		if (!Remap(PkgName, NewPkgPath, NewName))
		{
			FailedLoad.Add(PkgName + TEXT(" (path mismatch)"));
			continue;
		}
		UObject* Obj = Data.GetAsset();
		if (!Obj)
		{
			FailedLoad.Add(PkgName);
			continue;
		}
		Batch.Emplace(TWeakObjectPtr<UObject>(Obj), NewPkgPath, NewName);
	}

	if (FailedLoad.Num() > 0)
	{
		return MCPError(FString::Printf(
			TEXT("Found %d orphan external(s) at %s* but failed to load %d. Aborting reconcile."),
			Orphans.Num(), *(SrcMount + TEXT("__ExternalActors__/") + SrcRel), FailedLoad.Num()));
	}

	const bool bOk = AssetTools.RenameAssets(Batch);

	auto R = MCPSuccess();
	R->SetStringField(TEXT("sourcePath"), SourceAssetPath);
	R->SetStringField(TEXT("destinationPath"), DestAssetPath);
	R->SetStringField(TEXT("kind"), TEXT("orphan_reconcile"));
	R->SetBoolField(TEXT("success"), bOk);
	R->SetNumberField(TEXT("externalActors"), OrphanActors);
	R->SetNumberField(TEXT("externalObjects"), Orphans.Num() - OrphanActors);
	R->SetNumberField(TEXT("totalRenamed"), Batch.Num());

	if (!bOk)
	{
		return MCPError(FString::Printf(
			TEXT("Orphan reconcile failed: RenameAssets returned false on %d external(s). State may be partial."),
			Batch.Num()));
	}

	MCPSetUpdated(R);
	return MCPResult(R);
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
			// AssetPath may be either bare ("/Game/Foo/Bar") or object-path form
			// ("/Game/Foo/Bar.Bar"). When the dot is absent, Split returns false
			// and leaves both outputs empty - then GetPath of "" yields "" and
			// DestPath collapses to "/NewName.NewName", dropping the source
			// folder entirely (#425). Treat the whole input as the package name
			// in that case.
			if (!AssetPath.Split(TEXT("."), &PackageName, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
			{
				PackageName = AssetPath;
			}
			FString ParentDir = FPaths::GetPath(PackageName);
			if (ParentDir.IsEmpty()) ParentDir = PackageName;
			DestPath = FString::Printf(TEXT("%s/%s.%s"), *ParentDir, *NewName, *NewName);
		}
	}

	if (SourcePath.IsEmpty() || DestPath.IsEmpty())
	{
		return MCPError(TEXT("Missing 'sourcePath'+'destinationPath' or 'assetPath'+'newName'"));
	}

	if (IsProtectedAssetPath(SourcePath)) return MakeProtectedPathError(SourcePath);
	if (IsProtectedAssetPath(DestPath))   return MakeProtectedPathError(DestPath);

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
	// For Worlds, also reconcile any externals stranded at the source path - the
	// .umap moving without its externals is exactly the #409 failure mode, and
	// re-running rename_asset should fix it instead of silently no-opping.
	if (!UEditorAssetLibrary::DoesAssetExist(SourcePath))
	{
		if (UEditorAssetLibrary::DoesAssetExist(DestPath))
		{
			if (IsWorldAsset(DestPath))
			{
				return ReconcileOrphanExternals(SourcePath, DestPath);
			}
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetStringField(TEXT("sourcePath"), SourcePath);
			Noop->SetStringField(TEXT("destinationPath"), DestPath);
			return MCPResult(Noop);
		}
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *SourcePath));
	}

	// #409: Worlds carry external actor/object packages in /Game/__ExternalActors__/<LevelPath>/.
	// The plain UEditorAssetLibrary::RenameAsset path silently orphans those - route through
	// a world-aware batch rename instead. `force` lets a rollback call step over
	// orphans at the destination left by a partial forward rename.
	if (IsWorldAsset(SourcePath))
	{
		const bool bForce = OptionalBool(Params, TEXT("force"), false);
		return RenameWorldWithExternals(SourcePath, DestPath, bForce);
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

// ─── #278: structured delete diagnostics ────────────────────────────
// UEditorAssetLibrary::DeleteAsset returns a bare bool with no reason on
// failure, leaving callers to guess. Wrap it: detect open editors first
// (and close them when force=true), and on failure report referencers
// from the asset registry so the agent has something to act on.
namespace
{
	struct FDeleteDiagnostics
	{
		bool bOpenInEditor = false;
		TArray<FString> Referencers;
		FString Reason;     // open_in_editor | has_referencers | unknown
	};

	bool TryCloseAssetEditors(const FString& AssetPath, bool& bOutHadOpenEditor)
	{
		bOutHadOpenEditor = false;
		if (!GEditor) return false;
		UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!AES) return false;

		UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
		if (!Asset) return false;

		const TArray<IAssetEditorInstance*> Editors = AES->FindEditorsForAsset(Asset);
		bOutHadOpenEditor = Editors.Num() > 0;
		if (bOutHadOpenEditor)
		{
			AES->CloseAllEditorsForAsset(Asset);
		}
		return true;
	}

	FDeleteDiagnostics DiagnoseDeleteFailure(const FString& AssetPath)
	{
		FDeleteDiagnostics Diag;

		if (GEditor)
		{
			if (UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				if (UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath))
				{
					Diag.bOpenInEditor = AES->FindEditorsForAsset(Asset).Num() > 0;
				}
			}
		}

		// AssetRegistry referencers - filtered to non-self.
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		const FName PackageFName = *FPackageName::ObjectPathToPackageName(AssetPath);
		TArray<FName> Refs;
		ARM.Get().GetReferencers(PackageFName, Refs);
		for (const FName& R : Refs)
		{
			if (R != PackageFName)
			{
				Diag.Referencers.Add(R.ToString());
			}
		}

		if (Diag.bOpenInEditor)         Diag.Reason = TEXT("open_in_editor");
		else if (Diag.Referencers.Num()) Diag.Reason = TEXT("has_referencers");
		else                             Diag.Reason = TEXT("unknown");
		return Diag;
	}

	void ApplyDiagnosticsToJson(const TSharedPtr<FJsonObject>& Out, const FDeleteDiagnostics& Diag)
	{
		Out->SetStringField(TEXT("reason"), Diag.Reason);
		Out->SetBoolField(TEXT("openInEditor"), Diag.bOpenInEditor);
		TArray<TSharedPtr<FJsonValue>> RefsJson;
		for (const FString& R : Diag.Referencers)
		{
			RefsJson.Add(MakeShared<FJsonValueString>(R));
		}
		Out->SetArrayField(TEXT("referencers"), RefsJson);
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::DeleteAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	if (IsProtectedAssetPath(AssetPath)) return MakeProtectedPathError(AssetPath);

	const bool bForce = OptionalBool(Params, TEXT("force"), false);

	// Idempotent: if the asset doesn't exist, treat as already-deleted.
	if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Result);
	}

	bool bClosedEditor = false;
	if (bForce)
	{
		TryCloseAssetEditors(AssetPath, bClosedEditor);
	}

	const bool bSuccess = UEditorAssetLibrary::DeleteAsset(AssetPath);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetBoolField(TEXT("deleted"), bSuccess);
	if (bClosedEditor)
	{
		Result->SetBoolField(TEXT("closedOpenEditor"), true);
	}

	if (!bSuccess)
	{
		ApplyDiagnosticsToJson(Result, DiagnoseDeleteFailure(AssetPath));
	}

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

	const bool bForce = OptionalBool(Params, TEXT("force"), false);

	TArray<TSharedPtr<FJsonValue>> PerPath;
	int32 Deleted = 0;
	int32 Absent = 0;
	int32 Failed = 0;
	int32 ClosedEditors = 0;

	int32 Protected = 0;
	for (const TSharedPtr<FJsonValue>& V : *PathsArr)
	{
		FString Path;
		if (!V.IsValid() || !V->TryGetString(Path) || Path.IsEmpty())
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Path);

		if (IsProtectedAssetPath(Path))
		{
			Entry->SetStringField(TEXT("status"), TEXT("protected"));
			Entry->SetStringField(TEXT("reason"), TEXT("Engine/Script/Memory/Temp mounts are read-only via the bridge"));
			Protected++;
		}
		else if (!UEditorAssetLibrary::DoesAssetExist(Path))
		{
			Entry->SetStringField(TEXT("status"), TEXT("absent"));
			Absent++;
		}
		else
		{
			bool bClosed = false;
			if (bForce)
			{
				TryCloseAssetEditors(Path, bClosed);
				if (bClosed) ClosedEditors++;
			}
			if (UEditorAssetLibrary::DeleteAsset(Path))
			{
				Entry->SetStringField(TEXT("status"), TEXT("deleted"));
				if (bClosed) Entry->SetBoolField(TEXT("closedOpenEditor"), true);
				Deleted++;
			}
			else
			{
				Entry->SetStringField(TEXT("status"), TEXT("failed"));
				ApplyDiagnosticsToJson(Entry, DiagnoseDeleteFailure(Path));
				Failed++;
			}
		}
		PerPath.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("results"), PerPath);
	Result->SetNumberField(TEXT("deleted"), Deleted);
	Result->SetNumberField(TEXT("absent"), Absent);
	Result->SetNumberField(TEXT("failed"), Failed);
	if (Protected > 0) Result->SetNumberField(TEXT("protected"), Protected);
	Result->SetNumberField(TEXT("total"), PerPath.Num());
	if (ClosedEditors > 0) Result->SetNumberField(TEXT("closedEditors"), ClosedEditors);
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

		if (IsProtectedAssetPath(SourcePath) || IsProtectedAssetPath(NewPackagePath))
		{
			Record->SetStringField(TEXT("status"), TEXT("protected"));
			Record->SetStringField(TEXT("reason"), TEXT("Engine/Script/Memory/Temp mounts are read-only via the bridge"));
			PerItem.Add(MakeShared<FJsonValueObject>(Record));
			Skipped++;
			continue;
		}

		// #409: bulk_rename doesn't migrate World external actor/object packages.
		// Refuse Worlds here so callers route them through rename_asset, which
		// handles WP externals atomically.
		if (IsWorldAsset(SourcePath))
		{
			Record->SetStringField(TEXT("status"), TEXT("rejected_world"));
			Record->SetStringField(TEXT("reason"), TEXT("World assets must use rename_asset, which migrates __ExternalActors__/__ExternalObjects__ atomically. bulk_rename would orphan WP actors."));
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

	auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, TEXT("DataAsset"), DataClass, nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UObject* NewAsset = Created.Asset;

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

TSharedPtr<FJsonValue> FAssetHandlers::SaveAllDirty(const TSharedPtr<FJsonObject>& Params)
{
	const bool bSaveMapPackages = OptionalBool(Params, TEXT("saveMapPackages"), true);
	const bool bSaveContentPackages = OptionalBool(Params, TEXT("saveContentPackages"), true);

	const bool bOk = UEditorLoadingAndSavingUtils::SaveDirtyPackages(bSaveMapPackages, bSaveContentPackages);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("saveMapPackages"), bSaveMapPackages);
	Result->SetBoolField(TEXT("saveContentPackages"), bSaveContentPackages);
	Result->SetBoolField(TEXT("savedAll"), bOk);
	return MCPResult(Result);
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
TSharedPtr<FJsonValue> FAssetHandlers::MoveFolder(const TSharedPtr<FJsonObject>& Params)
{
	FString SourcePath;
	if (auto Err = RequireString(Params, TEXT("sourcePath"), SourcePath)) return Err;

	FString DestinationPath;
	if (auto Err = RequireString(Params, TEXT("destinationPath"), DestinationPath)) return Err;

	// Ensure paths don't have trailing slashes for consistent prefix replacement
	SourcePath.RemoveFromEnd(TEXT("/"));
	DestinationPath.RemoveFromEnd(TEXT("/"));

	if (IsProtectedAssetPath(SourcePath))      return MakeProtectedPathError(SourcePath);
	if (IsProtectedAssetPath(DestinationPath)) return MakeProtectedPathError(DestinationPath);

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

// ---------------------------------------------------------------------------
// #212 — create empty content browser folder under /Game (or any mount point).
// Accepts a single 'path' or a 'paths' array; returns per-path created/existed.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::CreateFolder(const TSharedPtr<FJsonObject>& Params)
{
	TArray<FString> Paths;
	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("paths"), PathsArr) && PathsArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *PathsArr)
		{
			FString S; if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) Paths.Add(S);
		}
	}
	FString SinglePath;
	if (Params->TryGetStringField(TEXT("path"), SinglePath) && !SinglePath.IsEmpty())
	{
		Paths.AddUnique(SinglePath);
	}
	if (Paths.Num() == 0)
	{
		return MCPError(TEXT("Provide either 'path' or 'paths' (array of /Game/... directories)."));
	}

	TArray<TSharedPtr<FJsonValue>> Created, Existed, Failed;
	for (const FString& P : Paths)
	{
		FString Norm = P;
		Norm.RemoveFromEnd(TEXT("/"));
		if (!Norm.StartsWith(TEXT("/")))
		{
			Failed.Add(MakeShared<FJsonValueString>(P));
			continue;
		}
		if (UEditorAssetLibrary::DoesDirectoryExist(Norm))
		{
			Existed.Add(MakeShared<FJsonValueString>(Norm));
			continue;
		}
		const bool bOk = UEditorAssetLibrary::MakeDirectory(Norm);
		if (bOk)
		{
			Created.Add(MakeShared<FJsonValueString>(Norm));
		}
		else
		{
			Failed.Add(MakeShared<FJsonValueString>(Norm));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("created"), Created);
	Result->SetArrayField(TEXT("existed"), Existed);
	Result->SetArrayField(TEXT("failed"), Failed);
	Result->SetNumberField(TEXT("createdCount"), Created.Num());
	Result->SetNumberField(TEXT("existedCount"), Existed.Num());
	Result->SetNumberField(TEXT("failedCount"), Failed.Num());
	Result->SetBoolField(TEXT("allSucceeded"), Failed.Num() == 0);
	return MCPResult(Result);
}

// ─── #279: health_check + force_reload ──────────────────────────────
// Agents hit a state where WidgetBlueprint / asset loads quietly return
// nullptr while the file exists on disk and AssetRegistry knows about it
// - only an editor restart unsticks it. health_check exposes the four
// flags an agent needs to detect the half-shutdown (onDisk, inRegistry,
// isLoaded, canLoad). force_reload bypasses the in-memory cache by
// resetting the package loader and forcing a fresh load.

TSharedPtr<FJsonValue> FAssetHandlers::HealthCheck(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const FString PackageName = FPackageName::ObjectPathToPackageName(AssetPath);

	// On disk?
	FString PackageFileName;
	const bool bOnDisk = FPackageName::DoesPackageExist(PackageName, &PackageFileName);

	// In AssetRegistry?
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> AssetsForPackage;
	ARM.Get().GetAssetsByPackageName(*PackageName, AssetsForPackage);
	const bool bInRegistry = AssetsForPackage.Num() > 0;

	// Already loaded?
	UPackage* ExistingPkg = FindPackage(nullptr, *PackageName);
	const bool bPackageLoaded = ExistingPkg != nullptr;
	UObject* InMemory = bPackageLoaded ? StaticFindObject(UObject::StaticClass(), ExistingPkg, *FPackageName::GetShortName(PackageName)) : nullptr;
	const bool bIsLoaded = InMemory != nullptr;

	// Can load? Try a non-destructive load attempt only if we don't already have it.
	bool bCanLoad = bIsLoaded;
	if (!bIsLoaded)
	{
		UObject* Probe = UEditorAssetLibrary::LoadAsset(AssetPath);
		bCanLoad = Probe != nullptr;
		if (Probe) InMemory = Probe;
	}

	const bool bIsStuck = bOnDisk && bInRegistry && !bCanLoad;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("packageName"), PackageName);
	Result->SetBoolField(TEXT("onDisk"), bOnDisk);
	Result->SetBoolField(TEXT("inRegistry"), bInRegistry);
	Result->SetBoolField(TEXT("isLoaded"), bIsLoaded);
	Result->SetBoolField(TEXT("canLoad"), bCanLoad);
	Result->SetBoolField(TEXT("isStuck"), bIsStuck);
	if (bOnDisk) Result->SetStringField(TEXT("packageFile"), PackageFileName);
	if (InMemory) Result->SetStringField(TEXT("class"), InMemory->GetClass()->GetName());
	if (bIsStuck)
	{
		Result->SetStringField(TEXT("hint"), TEXT("Asset on disk + in registry but cannot load. Try force_reload."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ForceReload(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const FString PackageName = FPackageName::ObjectPathToPackageName(AssetPath);
	FString PackageFileName;
	if (!FPackageName::DoesPackageExist(PackageName, &PackageFileName))
	{
		return MCPError(FString::Printf(TEXT("Package not found on disk: %s"), *PackageName));
	}

	// Close any open asset editors so they don't pin stale references.
	bool bClosedEditor = false;
	if (GEditor)
	{
		if (UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			if (UObject* Existing = StaticFindObject(UObject::StaticClass(), nullptr, *AssetPath))
			{
				if (AES->FindEditorsForAsset(Existing).Num() > 0)
				{
					AES->CloseAllEditorsForAsset(Existing);
					bClosedEditor = true;
				}
			}
		}
	}

	// Reset loaders on the existing package (if any) and force a GC pass so
	// the in-memory pointer is genuinely released before reload. Without
	// this, LoadObject hands back the same broken instance.
	if (UPackage* ExistingPkg = FindPackage(nullptr, *PackageName))
	{
		ResetLoaders(ExistingPkg);
		ExistingPkg->ClearFlags(RF_WasLoaded);
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	UObject* Reloaded = LoadObject<UObject>(nullptr, *AssetPath, nullptr, LOAD_None);
	const bool bSuccess = Reloaded != nullptr;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("packageName"), PackageName);
	Result->SetBoolField(TEXT("reloaded"), bSuccess);
	if (bClosedEditor) Result->SetBoolField(TEXT("closedOpenEditor"), true);
	if (Reloaded) Result->SetStringField(TEXT("class"), Reloaded->GetClass()->GetName());
	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), TEXT("LoadObject returned null after reset; the package file may be corrupt or contain a class the editor cannot resolve."));
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_asset_property -- Set a UPROPERTY on any loaded asset, walking dotted
// paths through nested structs and sub-objects (#420).
//
// Removes the read-modify-write Python pattern for things like
// `subsurface_profile.settings.mean_free_path_distance` - the handler does
// the struct-copy dance internally and writes back through SetJsonOnProperty.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::SetAssetProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;
	const TSharedPtr<FJsonValue>* ValueField = Params->Values.Find(TEXT("value"));
	if (!ValueField || !(*ValueField).IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Could not load asset '%s'"), *AssetPath));
	}

	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));
	if (PathParts.Num() == 0) return MCPError(TEXT("Empty propertyName"));

	UStruct* CurrentStruct = Asset->GetClass();
	void* CurrentContainer = Asset;
	FProperty* FinalProp = nullptr;
	for (int32 i = 0; i < PathParts.Num(); ++i)
	{
		FProperty* SegmentProp = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
		if (!SegmentProp)
		{
			return MCPError(FString::Printf(TEXT("Property '%s' not found at '%s'"), *PathParts[i], *PropertyName));
		}
		if (i < PathParts.Num() - 1)
		{
			if (FStructProperty* SP = CastField<FStructProperty>(SegmentProp))
			{
				CurrentContainer = SP->ContainerPtrToValuePtr<void>(CurrentContainer);
				CurrentStruct = SP->Struct;
			}
			else if (FObjectProperty* OP = CastField<FObjectProperty>(SegmentProp))
			{
				UObject* Sub = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(CurrentContainer));
				if (!Sub) return MCPError(FString::Printf(TEXT("Sub-object '%s' is null - cannot descend"), *PathParts[i]));
				Sub->Modify();
				CurrentContainer = Sub;
				CurrentStruct = Sub->GetClass();
			}
			else
			{
				return MCPError(FString::Printf(TEXT("'%s' is not a struct or sub-object - cannot descend"), *PathParts[i]));
			}
		}
		else
		{
			FinalProp = SegmentProp;
		}
	}

	void* ValuePtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);
	FString PrevValue;
	FinalProp->ExportText_Direct(PrevValue, ValuePtr, ValuePtr, nullptr, PPF_None);

	Asset->Modify();
	FString SetErr;
	if (!MCPJsonProperty::SetJsonOnProperty(FinalProp, ValuePtr, *ValueField, SetErr))
	{
		return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
	}

	Asset->PostEditChange();
	Asset->MarkPackageDirty();

	FString NewValue;
	FinalProp->ExportText_Direct(NewValue, ValuePtr, ValuePtr, nullptr, PPF_None);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("previousValue"), PrevValue);
	Result->SetStringField(TEXT("value"), NewValue);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetStringField(TEXT("value"), PrevValue);
	MCPSetRollback(Result, TEXT("set_asset_property"), Payload);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_texture_settings_by_type (#421)
//
// Apply the canonical compression / sRGB / LOD combo per type group:
//   normal      -> TC_Normalmap,    sRGB=false, TextureGroup::Character_NormalMap fallback
//   grayscale   -> TC_Grayscale,    sRGB=false, TextureGroup::World
//   baseColor   -> TC_Default,      sRGB=true,  TextureGroup::Character
//   hdr         -> TC_HDR,          sRGB=false, TextureGroup::HDR
//
// Params: groups: { normal?: [paths], grayscale?: [paths], baseColor?: [paths], hdr?: [paths] }
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::SetTextureSettingsByType(const TSharedPtr<FJsonObject>& Params)
{
	const TSharedPtr<FJsonObject>* GroupsObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("groups"), GroupsObj) || !GroupsObj || !(*GroupsObj).IsValid())
	{
		return MCPError(TEXT("Missing 'groups' object: { normal?: [paths], grayscale?: [paths], baseColor?: [paths], hdr?: [paths] }"));
	}

	struct FProfile
	{
		TextureCompressionSettings Compression;
		bool bSRGB;
		TextureGroup LodGroup;
	};
	static const TMap<FString, FProfile> Profiles = {
		{ TEXT("normal"),    { TC_Normalmap,  false, TEXTUREGROUP_CharacterNormalMap } },
		{ TEXT("grayscale"), { TC_Grayscale,  false, TEXTUREGROUP_World } },
		{ TEXT("baseColor"), { TC_Default,    true,  TEXTUREGROUP_Character } },
		{ TEXT("hdr"),       { TC_HDR,        false, TEXTUREGROUP_Skybox } },
	};

	TArray<TSharedPtr<FJsonValue>> Updated;
	TArray<TSharedPtr<FJsonValue>> Failed;

	for (const auto& Pair : (*GroupsObj)->Values)
	{
		const FString& Group = Pair.Key;
		const FProfile* Profile = Profiles.Find(Group);
		if (!Profile)
		{
			TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
			F->SetStringField(TEXT("group"), Group);
			F->SetStringField(TEXT("error"), TEXT("Unknown group; expected normal, grayscale, baseColor, hdr"));
			Failed.Add(MakeShared<FJsonValueObject>(F));
			continue;
		}
		const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
		if (!Pair.Value->TryGetArray(Paths) || !Paths) continue;

		for (const TSharedPtr<FJsonValue>& V : *Paths)
		{
			FString TexPath;
			if (!V->TryGetString(TexPath)) continue;
			UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *TexPath);
			if (!Tex)
			{
				TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
				F->SetStringField(TEXT("path"), TexPath);
				F->SetStringField(TEXT("error"), TEXT("Texture not found or not a Texture2D"));
				Failed.Add(MakeShared<FJsonValueObject>(F));
				continue;
			}
			Tex->Modify();
			Tex->PreEditChange(nullptr);
			Tex->CompressionSettings = Profile->Compression;
			Tex->SRGB = Profile->bSRGB;
			Tex->LODGroup = Profile->LodGroup;
			Tex->PostEditChange();
			Tex->MarkPackageDirty();
			TSharedPtr<FJsonObject> U = MakeShared<FJsonObject>();
			U->SetStringField(TEXT("path"), Tex->GetPathName());
			U->SetStringField(TEXT("group"), Group);
			Updated.Add(MakeShared<FJsonValueObject>(U));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("updated"), Updated);
	Result->SetNumberField(TEXT("updatedCount"), Updated.Num());
	if (Failed.Num() > 0)
	{
		Result->SetArrayField(TEXT("failed"), Failed);
		Result->SetNumberField(TEXT("failedCount"), Failed.Num());
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// create_interchange_pipeline (#421)
//
// Spawn a UInterchangeGenericAssetsPipeline asset with the most common
// mesh-import boilerplate already applied, replacing 15+ set_editor_property
// calls per project.
//
// Params: assetPath OR (name + packagePath), meshType ('skeletal' default,
// 'static'), options? (object of overrides on the resulting pipeline).
//
// Uses pure reflection so we don't depend on the InterchangeEditor module.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::CreateInterchangePipeline(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	Params->TryGetStringField(TEXT("assetPath"), AssetPath);
	FString Name, PackagePath;
	if (AssetPath.IsEmpty())
	{
		if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
		PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Import"));
	}
	else
	{
		FString Package, AssetName;
		AssetPath.Split(TEXT("."), &Package, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		Name = AssetName;
		PackagePath = FPaths::GetPath(Package);
	}
	const FString MeshType = OptionalString(Params, TEXT("meshType"), TEXT("skeletal")).ToLower();
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UClass* PipelineClass = FindObject<UClass>(nullptr, TEXT("/Script/InterchangePipelines.InterchangeGenericAssetsPipeline"));
	if (!PipelineClass)
	{
		return MCPError(TEXT("InterchangeGenericAssetsPipeline class not found. Enable the Interchange Editor plugin."));
	}

	auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, TEXT("InterchangePipeline"), PipelineClass, nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UObject* NewAsset = Created.Asset;

	// Default mesh-pipeline settings. We write through SetJsonOnProperty so
	// every field stays in sync with the asset's UPROPERTY layout.
	auto SetSubProp = [&](const FString& SubPath, const TCHAR* Field, const TSharedPtr<FJsonValue>& Val) -> bool
	{
		TArray<FString> Parts;
		SubPath.ParseIntoArray(Parts, TEXT("."));
		UStruct* Cur = NewAsset->GetClass();
		void* Container = NewAsset;
		for (const FString& Part : Parts)
		{
			FProperty* P = Cur->FindPropertyByName(FName(*Part));
			if (!P) return false;
			if (FObjectProperty* OP = CastField<FObjectProperty>(P))
			{
				UObject* Sub = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(Container));
				if (!Sub) return false;
				Sub->Modify();
				Container = Sub;
				Cur = Sub->GetClass();
			}
			else if (FStructProperty* SP = CastField<FStructProperty>(P))
			{
				Container = SP->ContainerPtrToValuePtr<void>(Container);
				Cur = SP->Struct;
			}
			else { return false; }
		}
		FProperty* Leaf = Cur->FindPropertyByName(FName(Field));
		if (!Leaf) return false;
		FString E;
		return MCPJsonProperty::SetJsonOnProperty(Leaf, Leaf->ContainerPtrToValuePtr<void>(Container), Val, E);
	};

	auto JBool = [](bool B) { return MakeShared<FJsonValueBoolean>(B); };
	auto JStr  = [](const TCHAR* S) { return MakeShared<FJsonValueString>(S); };

	// Common mesh pipeline defaults (matches the user's Python boilerplate).
	const bool bSkeletal = (MeshType == TEXT("skeletal"));
	SetSubProp(TEXT("CommonMeshesProperties"), TEXT("bRecomputeNormals"), JBool(false));
	SetSubProp(TEXT("CommonMeshesProperties"), TEXT("bRecomputeTangents"), JBool(false));
	SetSubProp(TEXT("CommonMeshesProperties"), TEXT("bUseMikkTSpace"), JBool(true));
	SetSubProp(TEXT("CommonMeshesProperties"), TEXT("bUseHighPrecisionTangentBasis"), JBool(true));
	SetSubProp(TEXT("CommonMeshesProperties"), TEXT("bRemoveDegenerates"), JBool(true));
	SetSubProp(TEXT("CommonMeshesProperties"), TEXT("ForceAllMeshAsType"),
		JStr(bSkeletal ? TEXT("SkeletalMesh") : TEXT("StaticMesh")));
	SetSubProp(TEXT("MeshPipeline"), TEXT("bBuildNanite"), JBool(false));
	SetSubProp(TEXT("MeshPipeline"), TEXT("bImportSkeletalMeshes"), JBool(bSkeletal));
	SetSubProp(TEXT("MeshPipeline"), TEXT("bImportStaticMeshes"), JBool(!bSkeletal));
	SetSubProp(TEXT("MeshPipeline"), TEXT("bCreatePhysicsAsset"), JBool(false));

	// Caller-supplied overrides.
	const TSharedPtr<FJsonObject>* OptionsObj = nullptr;
	int32 OverridesApplied = 0;
	TArray<TSharedPtr<FJsonValue>> OverrideFailures;
	if (Params->TryGetObjectField(TEXT("options"), OptionsObj) && OptionsObj && (*OptionsObj).IsValid())
	{
		for (const auto& Pair : (*OptionsObj)->Values)
		{
			// Caller key is a dotted path: "MeshPipeline.bImportSkeletalMeshes" etc.
			const FString& Key = Pair.Key;
			int32 Dot = INDEX_NONE;
			Key.FindLastChar('.', Dot);
			if (Dot == INDEX_NONE)
			{
				if (SetSubProp(TEXT(""), *Key, Pair.Value)) ++OverridesApplied;
				else OverrideFailures.Add(MakeShared<FJsonValueString>(Key));
				continue;
			}
			const FString SubPath = Key.Left(Dot);
			const FString Field = Key.RightChop(Dot + 1);
			if (SetSubProp(SubPath, *Field, Pair.Value)) ++OverridesApplied;
			else OverrideFailures.Add(MakeShared<FJsonValueString>(Key));
		}
	}

	NewAsset->PostEditChange();
	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("meshType"), MeshType);
	Result->SetNumberField(TEXT("overridesApplied"), OverridesApplied);
	if (OverrideFailures.Num() > 0) Result->SetArrayField(TEXT("overrideFailures"), OverrideFailures);
	MCPSetDeleteAssetRollback(Result, NewAsset->GetPathName());
	return MCPResult(Result);
}
