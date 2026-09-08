#include "AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "HandlerJsonProperty.h"
#include "HandlerPropertyText.h"
#include "HandlerAssetCreate.h"
#include "JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/AssetManager.h"
#include "Engine/AssetManagerTypes.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "Exporters/Exporter.h"
#include "AssetExportTask.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "EditorFramework/AssetImportData.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/TopLevelAssetPath.h"
#include "ScopedTransaction.h"

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
// The rule itself is MCPIsProtectedAssetPath in HandlerUtils.h, and the
// refusal it produces is MCPProtectedPathError beside it, shared by every
// asset translation unit so the write paths cannot drift apart.
namespace
{
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
	Registry.RegisterHandler(TEXT("read_uv_channels"), &ReadUvChannels);
	Registry.RegisterHandler(TEXT("set_uv_channel_count"), &SetUvChannelCount);
	Registry.RegisterHandler(TEXT("unwrap_uvs"), &UnwrapUvs);
	Registry.RegisterHandler(TEXT("transform_uvs"), &TransformUvs);
	Registry.RegisterHandler(TEXT("generate_lightmap_uvs"), &GenerateLightmapUvs);
	Registry.RegisterHandler(TEXT("export_uv_layout"), &ExportUvLayout);
	Registry.RegisterHandler(TEXT("check_uvs"), &CheckUvs);

	// Procedural mesh operations (AssetHandlers_GeometryScript.cpp). Each one
	// converts the LOD to a DynamicMesh, runs a solver over it and writes a
	// StaticMesh back, which is minutes rather than milliseconds on a dense
	// mesh. The default handler timeout would report a hang while the editor
	// was still working.
	Registry.RegisterHandlerWithTimeout(TEXT("apply_mesh_simplify"), &SimplifyMesh, 300.0f);
	Registry.RegisterHandlerWithTimeout(TEXT("apply_mesh_remesh"), &RemeshMesh, 300.0f);
	Registry.RegisterHandlerWithTimeout(TEXT("apply_mesh_mirror"), &MirrorMesh, 300.0f);
	Registry.RegisterHandlerWithTimeout(TEXT("apply_mesh_hole_fill"), &FillMeshHoles, 300.0f);
	Registry.RegisterHandlerWithTimeout(TEXT("generate_mesh_collision"), &GenerateMeshCollision, 300.0f);
	Registry.RegisterHandlerWithTimeout(TEXT("apply_mesh_fracture"), &FractureMesh, 600.0f);

	// Asset hygiene (AssetHandlers_Hygiene.cpp). Both walk the asset registry
	// across a whole content directory, so both are bounded by their own
	// maxAssets rather than by the default timeout.
	Registry.RegisterHandlerWithTimeout(TEXT("audit_asset_hygiene"), &AuditAssetHygiene, 300.0f);
	Registry.RegisterHandlerWithTimeout(TEXT("fix_asset_hygiene"), &FixAssetHygiene, 300.0f);

	Registry.RegisterHandler(TEXT("search_assets"), &SearchAssets);
	Registry.RegisterHandler(TEXT("read_asset"), &ReadAsset);
	Registry.RegisterHandler(TEXT("read_asset_properties"), &ReadAssetProperties);
	Registry.RegisterHandler(TEXT("duplicate_asset"), &DuplicateAsset);
	Registry.RegisterHandler(TEXT("rename_asset"), &RenameAsset);
	Registry.RegisterHandler(TEXT("move_asset"), &MoveAsset);
	Registry.RegisterHandler(TEXT("delete_asset"), &DeleteAsset);
	Registry.RegisterHandler(TEXT("delete_asset_batch"), &DeleteAssetBatch);
	Registry.RegisterHandler(TEXT("bulk_rename_assets"), &BulkRename);
	// #908: bounded redirector clean-up after a move. Timeout raised because a
	// fix-up loads and resaves every referencing package.
	Registry.RegisterHandlerWithTimeout(TEXT("fixup_redirectors"), &FixupRedirectors, 300.0f);
	Registry.RegisterHandler(TEXT("create_data_asset"), &CreateDataAsset);
	// Bounded batch upsert plus its rollback inverse. A 500-item batch that
	// preflights every item on a transient copy before touching a package can
	// legitimately outrun the default game-thread budget, so both carry an
	// explicit timeout.
	Registry.RegisterHandlerWithTimeout(TEXT("bulk_upsert_data_assets"), &BulkUpsertDataAssets, 120.0f);
	Registry.RegisterHandlerWithTimeout(TEXT("bulk_restore_data_assets"), &BulkRestoreDataAssets, 120.0f);
	// #726: generic create-any-concrete-UObject-class asset (physical materials,
	// curves, settings objects) - not restricted to UDataAsset subclasses.
	Registry.RegisterHandler(TEXT("create_asset_by_class"), &CreateAssetByClass);
	// #975: a named subobject inside an existing asset package, which
	// create_asset_by_class cannot make because it always creates a package.
	Registry.RegisterHandler(TEXT("create_subobject"), &CreateSubobject);
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
	Registry.RegisterHandler(TEXT("create_render_target_2d"), &CreateRenderTarget2D);
	// #697: texture export + compare.
	Registry.RegisterHandler(TEXT("export_texture"), &ExportTexture);
	Registry.RegisterHandler(TEXT("compare_textures"), &CompareTextures);
	Registry.RegisterHandler(TEXT("get_texture_info"), &ListTextureProperties);
	Registry.RegisterHandler(TEXT("set_texture_settings"), &SetTextureProperties);

	// Mesh handlers
	Registry.RegisterHandler(TEXT("set_mesh_material"), &SetMeshMaterial);
	Registry.RegisterHandler(TEXT("set_mesh_materials_batch"), &SetMeshMaterialsBatch);
	Registry.RegisterHandler(TEXT("recenter_pivot"), &RecenterPivot);

	// Socket handlers
	Registry.RegisterHandler(TEXT("add_socket"), &AddSocket);
	Registry.RegisterHandler(TEXT("set_socket_transform"), &SetSocketTransform);
	Registry.RegisterHandler(TEXT("set_asset_property"), &SetAssetProperty);
	Registry.RegisterHandler(TEXT("append_asset_array_elements"), &AppendAssetArrayElements);
	Registry.RegisterHandler(TEXT("bulk_set_asset_properties"), &BulkSetAssetProperties);
	Registry.RegisterHandler(TEXT("set_texture_settings_by_type"), &SetTextureSettingsByType);
	Registry.RegisterHandler(TEXT("create_interchange_pipeline"), &CreateInterchangePipeline);
	Registry.RegisterHandler(TEXT("remove_socket"), &RemoveSocket);
	Registry.RegisterHandler(TEXT("list_sockets"), &ListSockets);
	Registry.RegisterHandler(TEXT("list_asset_sockets"), &ListSockets);
	Registry.RegisterHandler(TEXT("reload_package"), &ReloadPackage);
	// #279: detect/recover stuck-unloadable assets
	Registry.RegisterHandler(TEXT("asset_health_check"), &HealthCheck);
	Registry.RegisterHandler(TEXT("force_reload_asset"), &ForceReload);

	// Additional DataTable handlers
	Registry.RegisterHandler(TEXT("create_datatable"), &CreateDataTable);
	Registry.RegisterHandler(TEXT("read_datatable"), &ReadDataTable);
	Registry.RegisterHandler(TEXT("reimport_datatable"), &ReimportDataTable);
	// #437: single-row mutation. Append a new row or overwrite an existing one
	// without exporting and re-importing the whole table.
	Registry.RegisterHandler(TEXT("set_datatable_row"), &SetDataTableRow);
	Registry.RegisterHandler(TEXT("add_datatable_row"), &SetDataTableRow);
	Registry.RegisterHandler(TEXT("update_datatable_row"), &SetDataTableRow);
	Registry.RegisterHandler(TEXT("remove_datatable_row"), &RemoveDataTableRow);
	Registry.RegisterHandler(TEXT("delete_datatable_row"), &RemoveDataTableRow);
	// #535: single-row read, single-cell write, row rename, and bulk JSON fill.
	Registry.RegisterHandler(TEXT("get_datatable_row"), &GetDataTableRow);
	Registry.RegisterHandler(TEXT("set_datatable_cell"), &SetDataTableCell);
	Registry.RegisterHandler(TEXT("rename_datatable_row"), &RenameDataTableRow);
	Registry.RegisterHandler(TEXT("fill_datatable_from_json"), &FillDataTableFromJson);

	// CurveTable handlers
	Registry.RegisterHandler(TEXT("create_curvetable"), &CreateCurveTable);
	Registry.RegisterHandler(TEXT("read_curvetable"), &ReadCurveTable);
	Registry.RegisterHandler(TEXT("list_curvetable_rows"), &ReadCurveTable);
	Registry.RegisterHandler(TEXT("import_curvetable"), &ImportCurveTable);
	Registry.RegisterHandler(TEXT("add_curvetable_row"), &AddCurveTableRow);
	Registry.RegisterHandler(TEXT("remove_curvetable_row"), &RemoveCurveTableRow);
	Registry.RegisterHandler(TEXT("rename_curvetable_row"), &RenameCurveTableRow);
	Registry.RegisterHandler(TEXT("get_curvetable_keys"), &GetCurveTableKeys);
	Registry.RegisterHandler(TEXT("set_curvetable_keys"), &SetCurveTableKeys);
	Registry.RegisterHandler(TEXT("add_curvetable_key"), &AddCurveTableKey);

	// Generic reimport / export
	Registry.RegisterHandler(TEXT("reimport_asset"), &ReimportAsset);
	Registry.RegisterHandler(TEXT("export_asset"), &ExportAsset);

	// StringTable handlers
	Registry.RegisterHandler(TEXT("create_stringtable"), &CreateStringTable);
	Registry.RegisterHandler(TEXT("read_stringtable"), &ReadStringTable);
	Registry.RegisterHandler(TEXT("list_stringtable_keys"), &ListStringTableKeys);
	Registry.RegisterHandler(TEXT("get_stringtable_entry"), &GetStringTableEntry);
	Registry.RegisterHandler(TEXT("set_stringtable_entry"), &SetStringTableEntry);
	Registry.RegisterHandler(TEXT("remove_stringtable_entry"), &RemoveStringTableEntry);
	Registry.RegisterHandler(TEXT("import_stringtable"), &ImportStringTable);
	Registry.RegisterHandler(TEXT("import_stringtable_csv"), &ImportStringTableCsv);

	// v0.7.8 stubs - FTS5-backed asset search
	Registry.RegisterHandler(TEXT("search_assets_fts"), &SearchAssetsFTS);
	Registry.RegisterHandler(TEXT("reindex_assets_fts"), &ReindexAssetsFTS);

	// v0.7.19 #150 - AssetRegistry referencers
	Registry.RegisterHandler(TEXT("get_asset_referencers"), &GetReferencers);
	Registry.RegisterHandler(TEXT("get_asset_dependencies"), &GetDependencies);
	Registry.RegisterHandler(TEXT("list_skeleton_bones"), &ListSkeletonBones);
	// #595: Chaos cloth read/write.
	Registry.RegisterHandler(TEXT("read_cloth_data"), &ReadClothData);
	Registry.RegisterHandler(TEXT("set_cloth_config"), &SetClothConfig);
	Registry.RegisterHandler(TEXT("get_primary_asset_ids"), &GetPrimaryAssetIds);

	// v1.0.0-rc.2 - #155 (asset gaps)
	Registry.RegisterHandler(TEXT("set_sk_material_slots"), &SetSkeletalMeshMaterialSlots);
	Registry.RegisterHandler(TEXT("diagnose_registry"), &DiagnoseRegistry);

	// v1.0.0-rc.3 - #177, #192, #193
	Registry.RegisterHandler(TEXT("get_mesh_bounds"), &GetMeshBounds);
	Registry.RegisterHandler(TEXT("get_mesh_info"), &GetMeshInfo);
	Registry.RegisterHandler(TEXT("read_import_sources"), &ReadImportSources);
	Registry.RegisterHandler(TEXT("get_mesh_collision"), &GetMeshCollision);
	Registry.RegisterHandler(TEXT("set_mesh_nav"), &SetMeshNav);
	Registry.RegisterHandler(TEXT("move_folder"), &MoveFolder);
	Registry.RegisterHandler(TEXT("create_folder"), &CreateFolder);
	Registry.RegisterHandler(TEXT("delete_folder"), &DeleteFolder);
	Registry.RegisterHandler(TEXT("migrate"), &MigrateAssets);

	// #686 - UserDefinedEnum authoring
	Registry.RegisterHandler(TEXT("create_user_defined_enum"), &CreateUserDefinedEnum);
	Registry.RegisterHandler(TEXT("list_enum_values"), &ListEnumValues);
	Registry.RegisterHandler(TEXT("edit_user_defined_enum"), &EditUserDefinedEnum);

	// #735: UserDefinedStruct authoring
	Registry.RegisterHandler(TEXT("create_user_defined_struct"), &CreateUserDefinedStruct);
	Registry.RegisterHandler(TEXT("list_struct_fields"), &ListStructFields);
	Registry.RegisterHandler(TEXT("edit_user_defined_struct"), &EditUserDefinedStruct);
}

// ---------------------------------------------------------------------------
// v0.7.8 STUBS - FTS5-backed asset index (Milestone A)
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
	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"), TEXT(""));

	// T3: paged. `maxResults` used to cut the ranked list and say nothing about
	// what it had cut, so a caller reading the top 50 could not tell a project
	// with 50 matches from one with 5000. The server now scores every match and
	// hands out pages of them, and `maxResults` is mapped onto `limit` on the
	// TypeScript side so the old spelling still sizes a page.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("search_assets_fts|query=%s|classFilter=%s"), *Query, *ClassFilter),
			/*DefaultLimit*/ 50, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

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

	// Score alone is not an order: TArray::Sort is not stable and equal scores
	// would come back in a different sequence between two calls, which is
	// exactly what a page anchor cannot survive. Ties break on the object path,
	// so the ranked list is one fixed sequence for a given query.
	Hits.Sort([](const FHit& A, const FHit& B)
	{
		if (A.Score != B.Score) return A.Score > B.Score;
		return A.Data->GetObjectPathString() < B.Data->GetObjectPathString();
	});

	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(Hits.Num());
	for (const FHit& Hit : Hits)
	{
		const FAssetData& D = *Hit.Data;
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("path"), D.PackageName.ToString());
		R->SetStringField(TEXT("name"), D.AssetName.ToString());
		R->SetStringField(TEXT("class"), D.AssetClassPath.GetAssetName().ToString());
		R->SetNumberField(TEXT("score"), Hit.Score);
		// The OBJECT path is the anchor, not the package name reported as
		// 'path': one package can hold more than one asset, and a page boundary
		// has to name exactly one row.
		Rows.Add({ D.GetObjectPathString(), MakeShared<FJsonValueObject>(R) });
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("query"), Query);
	Result->SetNumberField(TEXT("totalMatched"), Hits.Num());
	MCPPagination::EmitPage(Page, Rows, TEXT("results"), Result);
	Result->SetNumberField(TEXT("resultCount"), Result->GetIntegerField(TEXT("count")));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ReindexAssetsFTS(const TSharedPtr<FJsonObject>& Params)
{
	// No persistent index yet - ranked search runs live against the asset registry,
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
	Result->SetBoolField(TEXT("unchanged"), true);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("A rescan makes the in-memory asset registry agree with what is on disk. No asset, package or file changed, ")
		TEXT("so there is nothing to undo, and un-scanning a path is not an operation the registry offers."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ListAssets(const TSharedPtr<FJsonObject>& Params)
{
	const FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game"));
	const bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);
	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"));

	// #766/#790 established that one response for a big folder drops the
	// bridge, and answered it with a raw row offset. T3 replaces that offset
	// with the shared cursor: an offset re-read a moved collection at a row
	// number and could not tell that it had, while a cursor names the row it
	// resumes after and reports when that row moved or vanished.
	if (Params.IsValid() && Params->HasField(TEXT("offset")))
	{
		return MCPError(TEXT(
			"'offset' is no longer how list_assets pages, because a row number cannot tell you the "
			"folder changed underneath it. Pass the 'nextCursor' this action returned as 'cursor', "
			"and size the page with 'limit' (1 to 5000, default 500). Omit both for the first page."));
	}
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_assets|directory=%s|recursive=%d|classFilter=%s"),
				*Directory, bRecursive ? 1 : 0, *ClassFilter),
			/*DefaultLimit*/ 500, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FAssetData> Found;
	Registry.GetAssetsByPath(FName(*Directory), Found, bRecursive);

	// AssetRegistry order is not a stable contract, so paging over it could
	// overlap or skip entries between calls. Sort so the enumeration is one
	// fixed sequence, which is what a page anchor resumes into.
	// TArray::Sort is not stable, so tie on AssetName too: multi-asset packages
	// would otherwise order arbitrarily and paging could still overlap or skip.
	Found.Sort([](const FAssetData& A, const FAssetData& B)
	{
		if (A.PackageName != B.PackageName) return A.PackageName.LexicalLess(B.PackageName);
		return A.AssetName.LexicalLess(B.AssetName);
	});

	TArray<MCPPagination::FPageRow> Rows;
	for (const FAssetData& Data : Found)
	{
		const FString ClassName = Data.AssetClassPath.GetAssetName().ToString();
		if (!ClassFilter.IsEmpty() && !ClassName.Equals(ClassFilter, ESearchCase::IgnoreCase) && !ClassName.Contains(ClassFilter))
		{
			continue;
		}

		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), Data.PackageName.ToString());
		Item->SetStringField(TEXT("name"), Data.AssetName.ToString());
		Item->SetStringField(TEXT("className"), ClassName);
		// The OBJECT path is the anchor, not the package name reported as
		// 'path': a package holding two assets would otherwise give two rows
		// the same identity and a resume could not tell them apart.
		Rows.Add({ Data.GetObjectPathString(), MakeShared<FJsonValueObject>(Item) });
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("directory"), Directory);
	Result->SetBoolField(TEXT("recursive"), bRecursive);
	// Every match is enumerated before the page is cut, so totalMatched is the
	// real size of the result set rather than what fitted in this page.
	Result->SetNumberField(TEXT("totalMatched"), Rows.Num());
	MCPPagination::EmitPage(Page, Rows, TEXT("assets"), Result);
	Result->SetNumberField(TEXT("assetCount"), Result->GetIntegerField(TEXT("count")));
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
	bool bSearchAll = OptionalBool(Params, TEXT("searchAll"));

	// T3: paged. This used to stop at `maxResults` matches and report only how
	// many it had returned, so a caller searching a large root read the first
	// 50 hits as though they were the whole answer. `maxResults` is mapped onto
	// `limit` on the TypeScript side, so the old spelling still sizes a page.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("search_assets|query=%s|directory=%s|searchAll=%d"),
				*Query, *Directory, bSearchAll ? 1 : 0),
			/*DefaultLimit*/ 50, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

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

	TArray<MCPPagination::FPageRow> Rows;
	FString QueryLower = Query.ToLower();
	for (const FAssetData& AssetData : AllAssets)
	{
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
		// The object path is the anchor: it names one asset even when a package
		// holds several, and 'path' above reports the package for compatibility.
		Rows.Add({ AssetPath, MakeShared<FJsonValueObject>(Item) });
	}

	// The asset registry does not promise an enumeration order, so the matches
	// are sorted before paging. Without it the same page can come back in a
	// different order between two calls and the anchor would report a change
	// that is only the registry reshuffling. Sorted on the rows rather than on
	// the registry results, so the object path each comparison reads is the one
	// already built above.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("query"), Query);
	Result->SetStringField(TEXT("searchScope"), bSearchAll ? (bHasDirectory ? Directory : TEXT("all")) : Directory);
	MCPPagination::EmitPage(Page, Rows, TEXT("results"), Result);
	Result->SetNumberField(TEXT("resultCount"), Result->GetIntegerField(TEXT("count")));

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ReadAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	// The LoadAsset-then-LoadObject pair this action has always used now lives
	// in MCPLoadAssetObject, so the type-specific readers resolve a path the
	// same way this one does (#930).
	UObject* Asset = MCPLoadAssetObject(AssetPath);
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

// #568: loading a Blueprint asset path yields the UBlueprint wrapper, whose
// own properties (ParentClass, etc.) are rarely what a caller wants. Resolve to
// the generated-class CDO so asset property reads/writes hit the real defaults
// and can author Instanced sub-object arrays. Non-Blueprint assets pass through.
//
// OutBlueprint carries the wrapper back out, because a write to the CDO has
// bookkeeping the CDO alone cannot do: the package to save is the Blueprint's,
// and the compiler's own record of the variable's default has to be reconciled
// with the value just written (#931).
static UObject* MCPResolveAssetToCDO(UObject* Asset, UBlueprint** OutBlueprint = nullptr)
{
	if (OutBlueprint) *OutBlueprint = nullptr;
	if (UBlueprint* BP = Cast<UBlueprint>(Asset))
	{
		if (UClass* GenClass = BP->GeneratedClass)
		{
			if (UObject* CDO = GenClass->GetDefaultObject())
			{
				if (OutBlueprint) *OutBlueprint = BP;
				return CDO;
			}
		}
	}
	return Asset;
}

// #931: a property write marked the package dirty and stopped there, so it read
// back correctly, survived until the editor closed, and was gone on the next
// start. A GameplayAbility whose default reverted computed correct values and
// applied no effect, with nothing pointing at the cause. A write now either
// reaches the package on disk or says why it did not, and never reports plain
// success for a change that only exists in memory.
//
// Returns true when the package was written. On false, OutReason carries the
// sentence to hand back to the caller.
static bool MCPPersistAssetWrite(UObject* Asset, UBlueprint* OwningBlueprint, bool bSave, FString& OutReason)
{
	OutReason.Reset();
	UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
	if (!Package)
	{
		OutReason = TEXT("The asset has no package, so there is nothing to write.");
		return false;
	}

	const FString PackageName = Package->GetName();
	if (!bSave)
	{
		OutReason = FString::Printf(
			TEXT("save=false was requested, so '%s' is dirty in memory only and the change is lost when the editor closes. ")
			TEXT("Call asset(save) for it, or repeat the write with save=true."),
			*PackageName);
		return false;
	}
	// Protected mounts and read-only package files are both answered before the
	// engine is asked to write, by the one shared guard (#932): asking it to
	// open a file it cannot is what took the editor down with a fatal error.
	// For a Blueprint the package's asset is the UBlueprint; the CDO is one of
	// its exports and rides along, so the guard and the save see the same
	// object.
	UObject* WriteTarget = OwningBlueprint ? static_cast<UObject*>(OwningBlueprint) : Asset;
	return SaveAssetPackageChecked(WriteTarget, OutReason);
}

// A CDO write is a genuine package export and survives both the save and a
// later recompile, but two pieces of editor bookkeeping do not happen on their
// own, and both read as "my write did not take" (#931).
//
// UBlueprintGeneratedClass keeps a post-construct property list that newly
// spawned instances initialise from, and only MarkBlueprintAsModified rebuilds
// it. And FBPVariableDescription::DefaultValue, when it is not empty, is
// replayed onto the CDO by the next full compile AFTER the old CDO's values are
// copied forward, so a stale string default silently overwrites the value that
// was just written. Clearing it makes the CDO the single record of the default,
// which is what the Blueprint editor's own Class Defaults panel does.
static void MCPNoteBlueprintCDOWrite(UBlueprint* Blueprint, const FString& PropertyPath)
{
	if (!Blueprint) return;

	// The root segment is the variable the compiler knows by name. A dotted or
	// indexed path lands inside that variable's value, and the string default
	// it would replay covers the whole variable either way.
	FString RootName = PropertyPath;
	int32 Cut = INDEX_NONE;
	if (RootName.FindChar(TEXT('.'), Cut)) RootName = RootName.Left(Cut);
	if (RootName.FindChar(TEXT('['), Cut)) RootName = RootName.Left(Cut);
	const FName RootVarName(*RootName);

	FProperty* ChangedProperty = nullptr;
	if (UClass* GenClass = Blueprint->GeneratedClass)
	{
		ChangedProperty = GenClass->FindPropertyByName(RootVarName);
	}

	for (FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == RootVarName)
		{
			Variable.DefaultValue.Empty();
			break;
		}
	}

	FPropertyChangedEvent ChangeEvent(ChangedProperty, EPropertyChangeType::ValueSet);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint, ChangeEvent);
}

/** Report where a property write ended up: the package, whether it reached
 *  disk, and the reason when it did not. A write the caller asked to persist
 *  and that did not persist is a failure, not a success with a footnote. */
static void MCPDescribePropertyWritePersistence(
	TSharedPtr<FJsonObject>& Result,
	UObject* Asset,
	const FString& PropertyName,
	bool bSaveRequested,
	bool bPersisted,
	const FString& PersistReason)
{
	if (UPackage* Package = Asset ? Asset->GetOutermost() : nullptr)
	{
		Result->SetStringField(TEXT("packageName"), Package->GetName());
		Result->SetBoolField(TEXT("packageDirty"), Package->IsDirty());
	}
	Result->SetBoolField(TEXT("persisted"), bPersisted);
	Result->SetBoolField(TEXT("saved"), bPersisted);
	if (bPersisted) return;

	Result->SetStringField(TEXT("persistError"), PersistReason);
	if (bSaveRequested)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Set '%s' in memory but could not persist it: %s"), *PropertyName, *PersistReason));
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::ReadAssetProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;
	Asset = MCPResolveAssetToCDO(Asset); // #568

	FString ValueFormat;
	Params->TryGetStringField(TEXT("valueFormat"), ValueFormat);
	const bool bJsonValues = ValueFormat.Equals(TEXT("json"), ESearchCase::IgnoreCase);

	// Helper lambda to export a property value as string (#48 - reads arrays, structs, sub-objects)
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
		// Resolve dotted/indexed paths into nested structs, array elements, and
		// instanced subobjects (#527), e.g. "Config.Traits[1].Params.Field".
		FProperty* Prop = nullptr;
		void* ValuePtr = nullptr;
		UObject* LeafOwner = nullptr;
		FString ResolveErr;
		if (!MCPJsonProperty::ResolveDottedPath(Asset, PropertyName, Prop, ValuePtr, LeafOwner, ResolveErr))
		{
			return MCPError(ResolveErr);
		}
		FString ValueStr;
		Prop->ExportText_Direct(ValueStr, ValuePtr, ValuePtr, LeafOwner, PPF_None);

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("propertyName"), PropertyName);
		Result->SetStringField(TEXT("type"), Prop->GetCPPType());
		if (bJsonValues)
		{
			Result->SetField(TEXT("value"), FMCPJsonSerializer::SerializeValue(ValuePtr, Prop));
		}
		else
		{
			Result->SetStringField(TEXT("value"), ValueStr);
		}

		// #820: export text cannot carry a struct-keyed TMap back into a setter,
		// so a text-format read of a map-bearing property also hands back the
		// structured form set_property accepts, and says whether the text form
		// may be reused at all.
		if (MCPPropertyText::ContainsMap(Prop))
		{
			Result->SetNumberField(TEXT("mapPairCount"), MCPPropertyText::CountMapPairs(Prop, ValuePtr));
			Result->SetBoolField(TEXT("valueTextRoundTrips"),
				MCPPropertyText::ExportedTextRoundTrips(Prop, ValuePtr, LeafOwner));
			if (!bJsonValues)
			{
				Result->SetField(TEXT("valueJson"), FMCPJsonSerializer::SerializeValue(ValuePtr, Prop));
			}
		}

		// When the path lands on an array of instanced subobjects, also
		// enumerate each element's index and concrete class so callers can
		// pick a trait to descend into next (#527).
		if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
		{
			if (FObjectProperty* InnerObj = CastField<FObjectProperty>(ArrProp->Inner))
			{
				FScriptArrayHelper H(ArrProp, ValuePtr);
				TArray<TSharedPtr<FJsonValue>> Elems;
				for (int32 i = 0; i < H.Num(); ++i)
				{
					UObject* Sub = InnerObj->GetObjectPropertyValue(H.GetRawPtr(i));
					TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
					E->SetNumberField(TEXT("index"), i);
					E->SetStringField(TEXT("class"), Sub ? Sub->GetClass()->GetName() : TEXT("None"));
					Elems.Add(MakeShared<FJsonValueObject>(E));
				}
				Result->SetArrayField(TEXT("elements"), Elems);
			}
		}
		return MCPResult(Result);
	}

	// Return all properties with their values
	bool bIncludeValues = OptionalBool(Params, TEXT("includeValues"));

	// #755: an object property returned only a reference path, so reading a
	// data asset's nested payload (a LearningNeuralNetworkData subobject, an
	// instanced config) meant a second call - or Python. expandDepth walks
	// into subobjects OWNED by this asset and inlines their properties.
	//
	// Owned-only by default: following an arbitrary asset reference would drag
	// half the content browser into one response. expandExternal lifts that
	// for callers who really do want to follow a reference to another asset.
	const int32 ExpandDepth = FMath::Clamp(OptionalInt(Params, TEXT("expandDepth"), 0), 0, 5);
	const bool bExpandExternal = OptionalBool(Params, TEXT("expandExternal"), false);
	// Hard cap so a deep or cyclic graph cannot produce a response big enough
	// to drop the bridge connection.
	const int32 MaxExpanded = FMath::Clamp(OptionalInt(Params, TEXT("maxExpandedObjects"), 64), 1, 512);

	int32 ExpandedCount = 0;
	bool bExpansionTruncated = false;
	TSet<const UObject*> Visited;
	Visited.Add(Asset);

	// Declared before use so the lambdas can recurse into each other.
	TFunction<void(UObject*, TSharedPtr<FJsonObject>&, int32)> ExpandObject;
	// Walks any container (a UObject or a struct instance) emitting its
	// properties, descending into struct members so subobject collections held
	// inside a struct - material expressions, for one - are still reachable.
	TFunction<void(UStruct*, const void*, TArray<TSharedPtr<FJsonValue>>&, int32)> WalkContainer;

	WalkContainer = [&](UStruct* Struct, const void* Container, TArray<TSharedPtr<FJsonValue>>& Props, int32 Depth)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Prop = *It;
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), Prop->GetName());
			P->SetStringField(TEXT("type"), Prop->GetCPPType());
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);
			if (bIncludeValues)
			{
				if (bJsonValues)
				{
					P->SetField(TEXT("value"), FMCPJsonSerializer::SerializeValue(ValuePtr, Prop));
				}
				else
				{
					FString ValueStr;
					Prop->ExportText_Direct(ValueStr, ValuePtr, ValuePtr, Asset, PPF_None);
					P->SetStringField(TEXT("value"), ValueStr);
					// #820: a TMap does not survive the text round trip, so the
					// structured form rides along for those properties only.
					if (MCPPropertyText::ContainsMap(Prop))
					{
						P->SetField(TEXT("valueJson"), FMCPJsonSerializer::SerializeValue(ValuePtr, Prop));
					}
				}
			}

			if (Depth > 0)
			{
				// Arrays of owned subobjects are as common as single ones
				// (material expressions, instanced configs, node graphs), and
				// leaving them out would make expandDepth work for one shape
				// of asset and silently not for the next.
				if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
				{
					if (FObjectPropertyBase* InnerObj = CastField<FObjectPropertyBase>(ArrProp->Inner))
					{
						FScriptArrayHelper Helper(ArrProp, ValuePtr);
						TArray<TSharedPtr<FJsonValue>> Elems;
						for (int32 ElemIdx = 0; ElemIdx < Helper.Num(); ++ElemIdx)
						{
							UObject* Sub = InnerObj->GetObjectPropertyValue(Helper.GetRawPtr(ElemIdx));
							if (!Sub || !IsValid(Sub)) continue;
							const bool bOwnedElem = Sub->IsIn(Asset);
							if ((!bOwnedElem && !bExpandExternal) || Visited.Contains(Sub)) continue;
							if (ExpandedCount >= MaxExpanded) { bExpansionTruncated = true; break; }
							Visited.Add(Sub);
							++ExpandedCount;
							TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
							E->SetNumberField(TEXT("index"), ElemIdx);
							E->SetStringField(TEXT("objectPath"), Sub->GetPathName());
							E->SetStringField(TEXT("className"), Sub->GetClass()->GetName());
							E->SetBoolField(TEXT("owned"), bOwnedElem);
							ExpandObject(Sub, E, Depth - 1);
							Elems.Add(MakeShared<FJsonValueObject>(E));
						}
						if (Elems.Num() > 0) P->SetArrayField(TEXT("objects"), Elems);
					}
				}
				else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
				{
					UObject* Sub = ObjProp->GetObjectPropertyValue(ValuePtr);
					// IsIn(Asset) is what makes this a SUBOBJECT rather than a
					// reference to a separate asset.
					const bool bOwned = Sub && Sub->IsIn(Asset);
					if (Sub && IsValid(Sub) && (bOwned || bExpandExternal) && !Visited.Contains(Sub))
					{
						if (ExpandedCount >= MaxExpanded)
						{
							bExpansionTruncated = true;
						}
						else
						{
							Visited.Add(Sub);
							++ExpandedCount;
							TSharedPtr<FJsonObject> Nested = MakeShared<FJsonObject>();
							Nested->SetStringField(TEXT("objectPath"), Sub->GetPathName());
							Nested->SetStringField(TEXT("className"), Sub->GetClass()->GetName());
							Nested->SetBoolField(TEXT("owned"), bOwned);
							ExpandObject(Sub, Nested, Depth - 1);
							P->SetObjectField(TEXT("object"), Nested);
						}
					}
					else if (Sub && !Visited.Contains(Sub) && !bOwned && !bExpandExternal)
					{
						// Say why it was not followed rather than leaving a bare
						// path that looks the same as an unexpandable value.
						P->SetBoolField(TEXT("expandable"), true);
					}
				}
				else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
				{
					// Not counted against MaxExpanded: a struct is part of this
					// object, not a separate one, so descending costs no extra
					// object budget - only the objects it leads to do.
					TArray<TSharedPtr<FJsonValue>> Inner;
					WalkContainer(StructProp->Struct, ValuePtr, Inner, Depth);
					if (Inner.Num() > 0)
					{
						TSharedPtr<FJsonObject> SObj = MakeShared<FJsonObject>();
						SObj->SetStringField(TEXT("structType"), StructProp->Struct->GetName());
						SObj->SetArrayField(TEXT("properties"), Inner);
						P->SetObjectField(TEXT("struct"), SObj);
					}
				}
			}
			Props.Add(MakeShared<FJsonValueObject>(P));
		}
	};

	ExpandObject = [&](UObject* Owner, TSharedPtr<FJsonObject>& Into, int32 Depth)
	{
		TArray<TSharedPtr<FJsonValue>> Props;
		WalkContainer(Owner->GetClass(), Owner, Props, Depth);
		Into->SetNumberField(TEXT("propertyCount"), Props.Num());
		Into->SetArrayField(TEXT("properties"), Props);
	};

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("className"), Asset->GetClass()->GetName());
	ExpandObject(Asset, Result, ExpandDepth);
	if (ExpandDepth > 0)
	{
		Result->SetNumberField(TEXT("expandedObjects"), ExpandedCount);
		if (bExpansionTruncated)
		{
			Result->SetBoolField(TEXT("expansionTruncated"), true);
			Result->SetStringField(TEXT("note"), FString::Printf(
				TEXT("Stopped after %d expanded objects. Raise maxExpandedObjects, lower expandDepth, or read a specific subobject with editor(get_object_properties)."),
				MaxExpanded));
		}
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::DuplicateAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString SourcePath;
	if (auto Err = RequireString(Params, TEXT("sourcePath"), SourcePath)) return Err;
	FString DestPath;
	if (auto Err = RequireString(Params, TEXT("destinationPath"), DestPath)) return Err;

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	// #441: DoesAssetExist returns false for some Blueprints in 5.7 even when
	// the registry/loader can resolve them. Confirm via the shared resolver
	// before erroring out so duplicate doesn't bounce off valid paths.
	TSharedPtr<FJsonValue> SourceLoadError;
	UObject* SourceObj = MCPRequireAssetObject(SourcePath, SourceLoadError, TEXT("Source asset"));
	if (!SourceObj) return SourceLoadError;

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
	if (!Dup)
	{
		// Fallback: drive AssetTools directly off the loaded UObject. Same path
		// the Python workaround in #441 used.
		FString DestPkg, DestName;
		if (DestPath.Split(TEXT("/"), &DestPkg, &DestName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			Dup = AssetTools.DuplicateAsset(DestName, DestPkg, SourceObj);
		}
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("sourcePath"), SourcePath);
	Result->SetStringField(TEXT("destinationPath"), DestPath);
	Result->SetBoolField(TEXT("success"), Dup != nullptr);

	// #589: duplicating a .umap that has a Level Blueprint leaves the copy's
	// persistent level pointing its level-script class reference at the SOURCE
	// world's generated class (a dangling cross-world ref), which crashes the
	// editor when the copy is later loaded. Recompile the duplicated world's
	// level-script blueprint so its class regenerates against the new package,
	// then resave, breaking the dangling reference.
	if (UWorld* DupWorld = Cast<UWorld>(Dup))
	{
		bool bRecompiledScript = false;
		if (ULevel* PersistentLevel = DupWorld->PersistentLevel)
		{
			if (UBlueprint* LSB = PersistentLevel->GetLevelScriptBlueprint(/*bDontCreate*/ true))
			{
				FKismetEditorUtilities::CompileBlueprint(LSB);
				bRecompiledScript = true;
			}
		}
		SaveAssetPackage(DupWorld);
		Result->SetBoolField(TEXT("isWorld"), true);
		Result->SetBoolField(TEXT("recompiledLevelScript"), bRecompiledScript);
	}

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

	UObject* World = MCPLoadAssetObject(SourceAssetPath);
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

	if (MCPIsProtectedAssetPath(SourcePath)) return MCPProtectedPathError(SourcePath);
	if (MCPIsProtectedAssetPath(DestPath))   return MCPProtectedPathError(DestPath);

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
		TArray<FString> Referencers;       // on-disk (AssetRegistry) referencers
		TArray<FString> InMemoryReferencers; // live UObject referencers (#601)
		bool bInMemoryReferenced = false;  // #601
		bool bPackageReadOnly = false;     // #601 - file read-only on disk
		bool bPackageDirty = false;        // #601 - unsaved changes block delete
		FString Reason;     // open_in_editor | has_referencers | in_memory_referenced | package_read_only | package_dirty | unknown
	};

	bool TryCloseAssetEditors(const FString& AssetPath, bool& bOutHadOpenEditor)
	{
		bOutHadOpenEditor = false;
		if (!GEditor) return false;
		UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!AES) return false;

		UObject* Asset = MCPLoadAssetObject(AssetPath);
		if (!Asset) return false;

		const TArray<IAssetEditorInstance*> Editors = AES->FindEditorsForAsset(Asset);
		bOutHadOpenEditor = Editors.Num() > 0;
		if (bOutHadOpenEditor)
		{
			AES->CloseAllEditorsForAsset(Asset);
		}
		return true;
	}

	// #976: UEditorAssetLibrary::DeleteAsset is the FORCE-delete entry point.
	// Its own header says so: "It doesn't check if the asset has references in
	// other Levels or by Actors." Both delete handlers called it whatever the
	// caller passed for `force`, so a referenced asset was destroyed and its
	// referencers were rewritten to point at nothing, with `force=false`
	// reading as a safety flag that did not exist.
	//
	// The Asset Registry holds the same reference graph the Content Browser's
	// reference viewer draws, so asking it first is what turns a force delete
	// back into a checked one. Self-references are dropped: a package always
	// lists itself.
	TArray<FString> CollectPackageReferencers(const FString& AssetPath)
	{
		TArray<FString> Out;

		const FMCPAssetPathForms Forms = MCPAssetPathForms(AssetPath);
		if (Forms.PackagePath.IsEmpty()) return Out;

		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		const FName PackageFName(*Forms.PackagePath);
		TArray<FName> Refs;
		ARM.Get().GetReferencers(PackageFName, Refs);
		for (const FName& R : Refs)
		{
			if (R != PackageFName)
			{
				Out.AddUnique(R.ToString());
			}
		}
		Out.Sort([](const FString& A, const FString& B) { return A < B; });
		return Out;
	}

	/** The refusal a checked delete hands back. Names the packages that would
	 *  have been rewritten, so the caller can decide rather than discover. */
	void ApplyReferencerRefusalToJson(
		const TSharedPtr<FJsonObject>& Out,
		const FString& AssetPath,
		const TArray<FString>& Referencers)
	{
		// Long lists are the interesting case and truncating them would hide
		// exactly the referencer a caller is looking for, so the count is
		// reported alongside a bounded sample rather than a silent slice.
		constexpr int32 MaxNamed = 25;
		const int32 Named = FMath::Min(Referencers.Num(), MaxNamed);

		TArray<FString> Sample;
		Sample.Append(Referencers.GetData(), Named);

		Out->SetStringField(TEXT("error"), FString::Printf(
			TEXT("'%s' is referenced by %d package(s) and force=false, so it was not deleted. ")
			TEXT("Deleting it would rewrite those referencers to point at nothing. ")
			TEXT("Repoint or delete the referencers first, or pass force=true to delete anyway. Referencers: %s%s"),
			*AssetPath,
			Referencers.Num(),
			*FString::Join(Sample, TEXT(", ")),
			Referencers.Num() > Named ? TEXT(", ...") : TEXT("")));
		Out->SetStringField(TEXT("path"), AssetPath);
		Out->SetBoolField(TEXT("deleted"), false);
		Out->SetStringField(TEXT("reason"), TEXT("has_referencers"));
		Out->SetNumberField(TEXT("referencerCount"), Referencers.Num());

		TArray<TSharedPtr<FJsonValue>> RefsJson;
		for (const FString& R : Referencers)
		{
			RefsJson.Add(MakeShared<FJsonValueString>(R));
		}
		Out->SetArrayField(TEXT("referencers"), RefsJson);
	}

	/** Run the checked-delete gate for one path. Returns true and fills Out
	 *  with the refusal when the asset has referencers; false when the delete
	 *  may proceed. Both delete handlers go through this so the single and
	 *  batch forms cannot drift into enforcing different rules. */
	bool CheckedDeleteRefusal(const FString& AssetPath, const TSharedPtr<FJsonObject>& Out)
	{
		const TArray<FString> Referencers = CollectPackageReferencers(AssetPath);
		if (Referencers.IsEmpty()) return false;
		Out->SetStringField(TEXT("status"), TEXT("refused"));
		ApplyReferencerRefusalToJson(Out, AssetPath, Referencers);
		return true;
	}

	FDeleteDiagnostics DiagnoseDeleteFailure(const FString& AssetPath)
	{
		FDeleteDiagnostics Diag;

		if (GEditor)
		{
			if (UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				if (UObject* Asset = MCPLoadAssetObject(AssetPath))
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

		// #601: when there are no editors/on-disk referencers the delete still
		// fails for non-obvious reasons. Gather the common culprits so callers
		// get something actionable instead of a bare "unknown".
		if (UObject* Asset = MCPLoadAssetObject(AssetPath))
		{
			// Live (in-memory) references beyond the asset's own package.
			FReferencerInformationList RefInfo;
			if (IsReferenced(Asset, RF_Public | RF_Standalone, EInternalObjectFlags::Garbage, /*bCheckSubObjects=*/true, &RefInfo))
			{
				for (const FReferencerInformation& Ext : RefInfo.ExternalReferences)
				{
					if (Ext.Referencer && Ext.Referencer->GetOutermost() != Asset->GetOutermost())
					{
						Diag.bInMemoryReferenced = true;
						if (Diag.InMemoryReferencers.Num() < 20)
						{
							Diag.InMemoryReferencers.Add(Ext.Referencer->GetPathName());
						}
					}
				}
			}

			if (UPackage* Pkg = Asset->GetOutermost())
			{
				Diag.bPackageDirty = Pkg->IsDirty();
				FString PkgFilename;
				if (FPackageName::DoesPackageExist(Pkg->GetName(), &PkgFilename))
				{
					Diag.bPackageReadOnly = IFileManager::Get().IsReadOnly(*PkgFilename);
				}
			}
		}

		if (Diag.bOpenInEditor)               Diag.Reason = TEXT("open_in_editor");
		else if (Diag.Referencers.Num())      Diag.Reason = TEXT("has_referencers");
		else if (Diag.bInMemoryReferenced)    Diag.Reason = TEXT("in_memory_referenced");
		else if (Diag.bPackageReadOnly)       Diag.Reason = TEXT("package_read_only");
		else if (Diag.bPackageDirty)          Diag.Reason = TEXT("package_dirty");
		else                                  Diag.Reason = TEXT("unknown");
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

		// #601 richer diagnostics for the formerly-"unknown" cases.
		Out->SetBoolField(TEXT("inMemoryReferenced"), Diag.bInMemoryReferenced);
		if (Diag.InMemoryReferencers.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> InMemJson;
			for (const FString& R : Diag.InMemoryReferencers)
			{
				InMemJson.Add(MakeShared<FJsonValueString>(R));
			}
			Out->SetArrayField(TEXT("inMemoryReferencers"), InMemJson);
		}
		Out->SetBoolField(TEXT("packageReadOnly"), Diag.bPackageReadOnly);
		Out->SetBoolField(TEXT("packageDirty"), Diag.bPackageDirty);
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::DeleteAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	if (MCPIsProtectedAssetPath(AssetPath)) return MCPProtectedPathError(AssetPath);

	const bool bForce = OptionalBool(Params, TEXT("force"), false);

	// Idempotent: if the asset doesn't exist, treat as already-deleted.
	if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Result);
	}

	// #976: the delete below is a force delete, so the reference check has to
	// happen here rather than being left to an API that does not do one.
	if (!bForce)
	{
		TSharedPtr<FJsonObject> Refused = MakeShared<FJsonObject>();
		Refused->SetBoolField(TEXT("success"), false);
		if (CheckedDeleteRefusal(AssetPath, Refused))
		{
			return MCPResult(Refused);
		}
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
	Result->SetBoolField(TEXT("forced"), bForce);
	if (bClosedEditor)
	{
		Result->SetBoolField(TEXT("closedOpenEditor"), true);
	}

	if (!bSuccess)
	{
		ApplyDiagnosticsToJson(Result, DiagnoseDeleteFailure(AssetPath));
	}

	// Scoped to what actually happened, the way the batch delete scopes its
	// note to the entries with status deleted. A delete the editor refused or
	// could not finish left the asset where it was, and saying the package is
	// gone would be describing a different call.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	if (bSuccess)
	{
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The package is gone from disk and the bridge holds no copy of it, so no call recreates the asset that was ")
			TEXT("deleted. There is no inverse action. Take the snapshot before the delete if the step has to be recoverable."));
	}
	else
	{
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Nothing was deleted: the asset is still at this path, so there is nothing to undo. The diagnostics on this ")
			TEXT("result say why the delete did not finish."));
	}
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
	int32 Refused = 0;
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

		if (MCPIsProtectedAssetPath(Path))
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
		else if (!bForce && CheckedDeleteRefusal(Path, Entry))
		{
			// #976: same rule as the single delete. Per-asset, because one
			// referenced entry in a batch is not a reason to refuse the rest,
			// and a bare "failed" count never said which entry was skipped or
			// why. Status is its own value so a caller can tell a refusal
			// apart from a delete the editor attempted and could not finish.
			Refused++;
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
	Result->SetNumberField(TEXT("refused"), Refused);
	Result->SetBoolField(TEXT("forced"), bForce);
	if (Protected > 0) Result->SetNumberField(TEXT("protected"), Protected);
	Result->SetNumberField(TEXT("total"), PerPath.Num());
	if (ClosedEditors > 0) Result->SetNumberField(TEXT("closedEditors"), ClosedEditors);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Every package with status deleted is gone from disk and the bridge holds no copy of it, so no call recreates ")
		TEXT("them. There is no inverse action. Take the snapshot before the delete if the step has to be recoverable."));
	return MCPResult(Result);
}

// ─── #128 item 6 - bulk_rename_assets ───────────────────────────────
// Scene-referenced assets are expensive to rename one-by-one because each
// individual rename forces a redirector-fixup / level-reference-update
// pass across the whole project. At batches of 10-15 this can crash the
// editor (observed on the user's Vale project).
//
// Content Browser drag-moves use IAssetTools::RenameAssets() with an
// array of FAssetRenameData - that collapses every rename into a single
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

		if (MCPIsProtectedAssetPath(SourcePath) || MCPIsProtectedAssetPath(NewPackagePath))
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

		UObject* Asset = MCPLoadAssetObject(SourcePath);
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
	// redirector-fixup pass - the same op the Content Browser performs on
	// drag-and-drop. Returns true if every rename succeeded.
	bool bOk = AssetTools.RenameAssets(BatchRenames);

	// Mark each batched rename with its post-op status.
	// Every entry that landed also produces the entry that puts it back, so the
	// inverse covers exactly the renames that happened rather than the ones
	// that were asked for.
	int32 Succeeded = 0;
	int32 Failed = 0;
	int32 Idx = 0;
	TArray<TSharedPtr<FJsonValue>> ReverseRenames;
	// Two entries naming the same source collapse onto one asset, so a reverse
	// list built without this would carry a second entry whose source no
	// longer exists and which comes back not_found.
	TSet<FString> ReverseSeen;
	for (int32 i = 0; i < PerItem.Num(); ++i)
	{
		TSharedPtr<FJsonObject> Rec = PerItem[i]->AsObject();
		if (!Rec.IsValid() || Rec->HasField(TEXT("status"))) continue;

		const FAssetRenameData& Data = BatchRenames[Idx++];
		// A rename is considered to have succeeded if the asset now lives
		// at the destination. When bOk==false, some entries may still have
		// landed - check per-item.
		const FString DestFullPath = FString::Printf(TEXT("%s/%s.%s"),
			*Data.NewPackagePath, *Data.NewName, *Data.NewName);
		if (UEditorAssetLibrary::DoesAssetExist(DestFullPath))
		{
			Rec->SetStringField(TEXT("status"), TEXT("renamed"));
			Succeeded++;

			if (!ReverseSeen.Contains(DestFullPath))
			{
				ReverseSeen.Add(DestFullPath);
				FString SourcePath;
				Rec->TryGetStringField(TEXT("sourcePath"), SourcePath);
				const FMCPAssetPathForms SourceForms = MCPAssetPathForms(SourcePath);
				TSharedPtr<FJsonObject> Reverse = MakeShared<FJsonObject>();
				Reverse->SetStringField(TEXT("sourcePath"), DestFullPath);
				Reverse->SetStringField(TEXT("newPackagePath"), FPaths::GetPath(SourceForms.PackagePath));
				Reverse->SetStringField(TEXT("newName"), SourceForms.AssetName);
				ReverseRenames.Add(MakeShared<FJsonValueObject>(Reverse));
			}
		}
		else
		{
			Rec->SetStringField(TEXT("status"), TEXT("failed"));
			Failed++;
		}
	}

	// #908: RenameAssets runs one redirector fix-up pass, and a referencer that
	// is not loaded is invisible to it, so a stub is left at the old path with
	// nothing in the response saying so. 29 renames left 19 of them, and the
	// first sign of it was a later search finding redirectors nobody expected.
	IAssetRegistry& RenameRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	int32 RedirectorsLeft = 0;
	TArray<TSharedPtr<FJsonValue>> RedirectorPackages;
	for (const TSharedPtr<FJsonValue>& Value : PerItem)
	{
		TSharedPtr<FJsonObject> Rec = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Rec.IsValid()) continue;
		FString Status;
		Rec->TryGetStringField(TEXT("status"), Status);
		if (Status != TEXT("renamed")) continue;

		FString SourcePath;
		Rec->TryGetStringField(TEXT("sourcePath"), SourcePath);
		const FMCPAssetPathForms SourceForms = MCPAssetPathForms(SourcePath);

		bool bRedirectorLeft = false;
		TArray<FAssetData> AtOldPath;
		RenameRegistry.GetAssetsByPackageName(FName(*SourceForms.PackagePath), AtOldPath);
		for (const FAssetData& Data : AtOldPath)
		{
			if (Data.IsRedirector()) { bRedirectorLeft = true; break; }
		}
		// The registry can lag a rename that just happened, so an in-memory
		// redirector counts too rather than reading as a clean result.
		if (!bRedirectorLeft && FindObject<UObjectRedirector>(nullptr, *SourceForms.ObjectPath))
		{
			bRedirectorLeft = true;
		}

		Rec->SetBoolField(TEXT("redirectorLeft"), bRedirectorLeft);
		if (bRedirectorLeft)
		{
			++RedirectorsLeft;
			RedirectorPackages.Add(MakeShared<FJsonValueString>(SourceForms.PackagePath));
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
	Result->SetNumberField(TEXT("redirectorsLeft"), RedirectorsLeft);
	Result->SetNumberField(TEXT("redirectorsRemoved"), FMath::Max(0, Succeeded - RedirectorsLeft));
	Result->SetArrayField(TEXT("redirectorPackages"), RedirectorPackages);
	if (RedirectorsLeft > 0)
	{
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("%d of %d renamed assets left an ObjectRedirector at the old path, still pointed at by packages this rename did not load. ")
			TEXT("Pass redirectorPackages to asset(fixup_redirectors) to load exactly those referencers, rewrite them, and delete the stubs that come out unreferenced."),
			RedirectorsLeft, Succeeded));
	}

	if (ReverseRenames.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("renames"), ReverseRenames);
		MCPSetRollback(Result, TEXT("bulk_rename_assets"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetBoolField(TEXT("rollbackVerified"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("UNVERIFIED. The inverse renames every asset that landed back to the package path and name it came from, and ")
			TEXT("redirectorsLeft on this result says how many of those old paths this call left an ObjectRedirector standing at. ")
			TEXT("Whether IAssetTools::RenameAssets will rename over a destination package occupied by such a redirector is not ")
			TEXT("stated in the shipped IAssetTools.h and FAssetRenameManager is not distributed, so it has not been established ")
			TEXT("here. If it refuses, this rollback fails for the entries whose old path still carries a stub. Redirectors are ")
			TEXT("not undone in any case: the stubs at the old paths stay, and the reverse rename leaves its own at the new paths. ")
			TEXT("Run asset(fixup_redirectors) before a rollback to clear the old stubs, and again after it."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), TEXT("No rename landed, so there is nothing to undo."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::CreateDataAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game"));
	FString ClassName;
	if (auto Err = RequireStringAlt(Params, TEXT("className"), TEXT("class"), ClassName)) return Err;

	// #823: shared resolution, so the prefixed C++ spelling ("UMyConfig",
	// "/Script/MyGame.UMyConfig") resolves to the class UE registered as
	// "MyConfig" instead of reporting the class missing.
	UClass* DataClass = MCPResolveClass(ClassName);
	if (auto Err = MCPCheckClassUsable(ClassName, DataClass, UDataAsset::StaticClass())) return Err;

	const FString FullPath = FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *Name, *Name);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, TEXT("DataAsset"), DataClass, nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UObject* NewAsset = Created.Asset;

	// Optional properties object - use recursive JSON-to-property setter so that
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

// #726: create an asset of any concrete UObject class. create_data_asset only
// accepts UDataAsset subclasses, so "settings-object" classes (UPhysicalMaterial
// subclasses, curves, etc.) had no native route and required execute_python with
// a hand-picked UFactory. This resolves the class, creates the asset (IAssetTools
// CreateAsset with a null factory NewObjects the exact class), applies optional
// properties, and saves.
TSharedPtr<FJsonValue> FAssetHandlers::CreateAssetByClass(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game"));
	FString ClassName;
	if (auto Err = RequireStringAlt(Params, TEXT("className"), TEXT("class"), ClassName)) return Err;

	// #823: shared resolution - same as create_data_asset, prefix tolerant.
	UClass* AssetClass = MCPResolveClass(ClassName);
	// Guard classes that cannot be standalone assets or need a specialized flow.
	if (auto Err = MCPCheckClassUsable(ClassName, AssetClass)) return Err;
	if (AssetClass->IsChildOf(AActor::StaticClass()) || AssetClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return MCPClassUnusableError(ClassName, AssetClass, TEXT("not_an_asset"),
			TEXT("it is an actor or component class, not an asset. Use level(place_actor) or level(add_component_to_actor)."));
	}

	const FString FullPath = FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *Name, *Name);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, AssetClass->GetName(), AssetClass, nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UObject* NewAsset = Created.Asset;

	// Optional properties (recursive JSON-to-property setter), mirroring create_data_asset.
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	int32 SetCount = 0;
	TArray<FString> PropErrors;
	if (Params->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && (*PropsObj).IsValid())
	{
		for (const auto& Pair : (*PropsObj)->Values)
		{
			FProperty* Prop = AssetClass->FindPropertyByName(FName(*Pair.Key));
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
	Result->SetStringField(TEXT("className"), AssetClass->GetName());
	Result->SetNumberField(TEXT("propertiesSet"), SetCount);
	if (PropErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Errs;
		for (const FString& E : PropErrors) Errs.Add(MakeShared<FJsonValueString>(E));
		Result->SetArrayField(TEXT("propertyErrors"), Errs);
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), FullPath);
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);

	return MCPResult(Result);
}

namespace
{
	// #768: on-disk evidence for a save. A boolean return is not enough - the
	// reported failure was save actions returning success while the .uasset was
	// never touched, only surfacing after an editor restart. Reporting the file
	// path, size and mtime lets a caller verify instead of trusting the flag.
	void DescribeOnDisk(const TSharedPtr<FJsonObject>& Out, const FString& PackageName)
	{
		FString Filename;
		if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename))
		{
			return;
		}
		// A package is either an asset or a map; try both extensions.
		const FString AssetFile = Filename + FPackageName::GetAssetPackageExtension();
		const FString MapFile = Filename + FPackageName::GetMapPackageExtension();
		IFileManager& FM = IFileManager::Get();
		const FString Resolved = FM.FileExists(*AssetFile) ? AssetFile
			: (FM.FileExists(*MapFile) ? MapFile : FString());
		Out->SetBoolField(TEXT("existsOnDisk"), !Resolved.IsEmpty());
		if (!Resolved.IsEmpty())
		{
			Out->SetStringField(TEXT("file"), Resolved);
			Out->SetNumberField(TEXT("fileSize"), (double)FM.FileSize(*Resolved));
			Out->SetStringField(TEXT("modifiedUtc"), FM.GetTimeStamp(*Resolved).ToIso8601());
		}
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::SaveAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if ((Params->TryGetStringField(TEXT("path"), AssetPath) || Params->TryGetStringField(TEXT("assetPath"), AssetPath)) && !AssetPath.IsEmpty() && AssetPath != TEXT("all"))
	{
		// #768: force=true saves regardless of the dirty flag. Several reports
		// hit edits that never marked their package dirty (OFPA level actors,
		// property writes through some subsystems), so a dirty-only save
		// skipped them and still returned success.
		const bool bForce = OptionalBool(Params, TEXT("force"), false);
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetBoolField(TEXT("force"), bForce);

		// The dirty flag is the only honest answer to "did this call change
		// anything", and it has to be read before the save, because afterwards
		// every package is clean and a save that wrote nothing is
		// indistinguishable from one that flushed an edit.
		UObject* PreSaveAsset = MCPLoadAssetObject(AssetPath);
		UPackage* PreSavePackage = PreSaveAsset ? PreSaveAsset->GetOutermost() : nullptr;
		const bool bWasDirty = PreSavePackage && PreSavePackage->IsDirty();
		Result->SetBoolField(TEXT("wasDirty"), bWasDirty);

		bool bSuccess = false;
		if (bForce)
		{
			UObject* Asset = LoadAssetByPath<UObject>(AssetPath);
			if (!Asset)
			{
				return MCPAssetNotFoundError(AssetPath);
			}
			UPackage* Package = Asset->GetOutermost();
			if (!Package)
			{
				return MCPError(FString::Printf(TEXT("Asset has no package: %s"), *AssetPath));
			}
			TArray<UPackage*> Packages{ Package };
			bSuccess = UEditorLoadingAndSavingUtils::SavePackages(Packages, /*bOnlyDirty=*/false);
			Result->SetStringField(TEXT("package"), Package->GetName());
			DescribeOnDisk(Result, Package->GetName());
		}
		else
		{
			bSuccess = UEditorAssetLibrary::SaveAsset(AssetPath);
			if (UObject* Asset = MCPLoadAssetObject(AssetPath))
			{
				if (UPackage* Package = Asset->GetOutermost())
				{
					Result->SetStringField(TEXT("package"), Package->GetName());
					Result->SetBoolField(TEXT("stillDirty"), Package->IsDirty());
					DescribeOnDisk(Result, Package->GetName());
				}
			}
		}
		Result->SetBoolField(TEXT("success"), bSuccess);

		// A clean package had nothing to flush. Without force the save was a
		// no-op and says so; with force the file was rewritten anyway, which is
		// a write to disk even though no edit was pending, so it does not claim
		// to have changed nothing.
		if (bWasDirty)
		{
			MCPSetUpdated(Result);
		}
		else if (bForce && bSuccess)
		{
			MCPSetUpdated(Result);
			Result->SetStringField(TEXT("note"),
				TEXT("The package was not dirty. force=true rewrote the file on disk anyway; without it this call would have written nothing."));
		}
		else
		{
			Result->SetBoolField(TEXT("updated"), false);
			Result->SetBoolField(TEXT("unchanged"), true);
			if (bSuccess)
			{
				// Only when the save itself reported success. A failed save
				// wrote nothing either, and telling that caller the package was
				// simply clean would hide the failure behind a no-op.
				Result->SetStringField(TEXT("note"),
					TEXT("The package was not dirty, so nothing was written. Pass force=true to write it regardless."));
			}
		}

		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("A save overwrites the file on disk with what is in memory. The previous file contents are gone and the ")
			TEXT("bridge holds no copy, so there is no un-save. There is no inverse action."));
		return MCPResult(Result);
	}
	else
	{
		// No assetPath: this is the save-everything branch. 'force' has no
		// meaning here (SaveDirectory is dirty-only), so say so rather than
		// accepting a flag that silently does nothing.
		if (OptionalBool(Params, TEXT("force"), false))
		{
			return MCPError(TEXT("'force' requires an assetPath - it forces one package to disk. To flush everything, use asset(save_all_dirty), which reports exactly which packages were written."));
		}
		// Counted before the sweep for the same reason as the single-asset
		// branch: SaveDirectory is dirty-only, so with nothing dirty it writes
		// nothing, and afterwards there is no way to tell that from a sweep
		// that flushed a hundred packages.
		TArray<UPackage*> DirtyBefore;
		FEditorFileUtils::GetDirtyContentPackages(DirtyBefore);
		FEditorFileUtils::GetDirtyWorldPackages(DirtyBefore);
		int32 DirtyUnderGame = 0;
		for (UPackage* Package : DirtyBefore)
		{
			if (Package && Package->IsDirty() && Package->GetName().StartsWith(TEXT("/Game/")))
			{
				++DirtyUnderGame;
			}
		}

		UEditorAssetLibrary::SaveDirectory(TEXT("/Game"));
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("message"), TEXT("All modified assets under /Game saved (dirty only). Use save_all_dirty for a per-package report."));
		Result->SetNumberField(TEXT("dirtyPackagesBefore"), DirtyUnderGame);
		if (DirtyUnderGame > 0)
		{
			MCPSetUpdated(Result);
		}
		else
		{
			Result->SetBoolField(TEXT("updated"), false);
			Result->SetBoolField(TEXT("unchanged"), true);
		}
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("A save overwrites the files on disk with what is in memory. The previous file contents are gone and the ")
			TEXT("bridge holds no copy, so there is no un-save. There is no inverse action."));
		return MCPResult(Result);
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::SaveAllDirty(const TSharedPtr<FJsonObject>& Params)
{
	const bool bSaveMapPackages = OptionalBool(Params, TEXT("saveMapPackages"), true);
	const bool bSaveContentPackages = OptionalBool(Params, TEXT("saveContentPackages"), true);

	// #768: capture what was dirty BEFORE saving, so the response can name the
	// packages this call was responsible for and report whether each one
	// actually reached disk. 'savedAll' on its own was reported as true while
	// packages were silently never written.
	TArray<UPackage*> DirtyBefore;
	if (bSaveContentPackages) FEditorFileUtils::GetDirtyContentPackages(DirtyBefore);
	if (bSaveMapPackages) FEditorFileUtils::GetDirtyWorldPackages(DirtyBefore);
	TArray<FString> TargetNames;
	for (UPackage* Package : DirtyBefore)
	{
		if (Package && Package->IsDirty()) TargetNames.AddUnique(Package->GetName());
	}

	const bool bOk = UEditorLoadingAndSavingUtils::SaveDirtyPackages(bSaveMapPackages, bSaveContentPackages);

	TArray<TSharedPtr<FJsonValue>> Written;
	TArray<TSharedPtr<FJsonValue>> StillDirty;
	for (const FString& Name : TargetNames)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("package"), Name);
		UPackage* Package = FindPackage(nullptr, *Name);
		const bool bDirty = Package && Package->IsDirty();
		Entry->SetBoolField(TEXT("stillDirty"), bDirty);
		DescribeOnDisk(Entry, Name);
		if (bDirty) StillDirty.Add(MakeShared<FJsonValueObject>(Entry));
		else        Written.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("saveMapPackages"), bSaveMapPackages);
	Result->SetBoolField(TEXT("saveContentPackages"), bSaveContentPackages);
	Result->SetBoolField(TEXT("savedAll"), bOk && StillDirty.Num() == 0);
	Result->SetNumberField(TEXT("attempted"), TargetNames.Num());
	Result->SetArrayField(TEXT("saved"), Written);
	Result->SetArrayField(TEXT("stillDirty"), StillDirty);
	// Keyed off what reached disk, not off what was dirty going in: a run where
	// every package failed to write attempted work and changed nothing.
	Result->SetBoolField(TEXT("unchanged"), Written.Num() == 0);
	if (StillDirty.Num() > 0)
	{
		Result->SetStringField(TEXT("note"), TEXT("Some packages are still dirty after the save. Retry those with asset(save, path=..., force=true)."));
	}
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("A save overwrites the files on disk with what is in memory. The previous file contents are gone and the ")
		TEXT("bridge holds no copy, so there is no un-save. There is no inverse action."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::ListTextures(const TSharedPtr<FJsonObject>& Params)
{
	FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game/"));

	// T3: paged. This stopped at 50 textures and reported a count of 50, so a
	// project with 4000 of them looked identical to one with 50. `maxResults`
	// is mapped onto `limit` on the TypeScript side, so the old spelling still
	// sizes a page.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_textures|directory=%s"), *Directory),
			/*DefaultLimit*/ 50, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Texture2D")), AssetDataList, true);

	TArray<MCPPagination::FPageRow> Rows;
	for (const FAssetData& AssetData : AssetDataList)
	{
		FString AssetPath = AssetData.GetObjectPathString();
		if (!AssetPath.StartsWith(Directory)) continue;

		TSharedPtr<FJsonObject> TexObj = MakeShared<FJsonObject>();
		TexObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		TexObj->SetStringField(TEXT("path"), AssetPath);
		// The object path is the anchor: unique across the project and the same
		// string on the next enumeration.
		Rows.Add({ AssetPath, MakeShared<FJsonValueObject>(TexObj) });
	}

	// The asset registry does not promise an enumeration order, so the list is
	// sorted before paging. A cursor over an unordered enumeration is not
	// resumable: the anchor would move every call.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("directory"), Directory);
	MCPPagination::EmitPage(Page, Rows, TEXT("textures"), Result);
	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FAssetHandlers::ReloadPackage(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;

	UPackage* Package = Asset->GetOutermost();
	if (!Package)
	{
		return MCPError(TEXT("Could not get asset package"));
	}

	// Unload and reload the package
	FString PackageName = Package->GetName();
	FString PackageFileName;
	bool bSuccess = false;
	// Whether the reload machinery ran at all. Everything below sits inside
	// this branch, so when the package is not on disk nothing is reset,
	// collected or re-read and the call genuinely changed nothing.
	bool bReloadRan = false;
	if (FPackageName::DoesPackageExist(PackageName, &PackageFileName))
	{
		bReloadRan = true;

		// Reset loaders so we can reload
		ResetLoaders(Package);

		// Force garbage collection to release old references
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

		// Reload
		UObject* Reloaded = MCPLoadAssetObject(AssetPath);
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
	// Keyed off whether the reload block executed, not off whether it worked.
	// Where it ran, ResetLoaders and a collection have already changed editor
	// state, and a failed re-read left the package WORSE than it found it
	// rather than unchanged. Where the package is not on disk that block is
	// skipped whole and nothing happened, so an unconditional false would have
	// been an inaccurate marker on exactly that path.
	Result->SetBoolField(TEXT("unchanged"), !bReloadRan);
	if (bReloadRan)
	{
		Result->SetStringField(TEXT("idempotencyNote"),
			TEXT("A reload that runs is never a no-op: ResetLoaders and a garbage collection precede the re-read, so replaying ")
			TEXT("this call discards and rebuilds the in-memory package again rather than recognising it has nothing to do."));
	}
	else
	{
		Result->SetStringField(TEXT("idempotencyNote"),
			TEXT("The package is not on disk, so nothing was reset, collected or re-read and this call changed nothing."));
	}
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("A reload throws the in-memory package away and re-reads the file. Whatever was in memory and not on disk is ")
		TEXT("gone, and reloading again re-reads the same file rather than restoring it. There is no inverse action."));

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

// ─── #588 asset(get_dependencies) ───────────────────────────────────
// Forward dependency lookup per package: "what packages does this asset
// reference?" Mirrors GetReferencers (#150) but walks the other direction.
// Optional 'hard'/'soft' flags filter by dependency link type; both default
// on, matching GetDependencies' default (all package dependencies).
TSharedPtr<FJsonValue> FAssetHandlers::GetDependencies(const TSharedPtr<FJsonObject>& Params)
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

	const bool bHard = OptionalBool(Params, TEXT("hard"), true);
	const bool bSoft = OptionalBool(Params, TEXT("soft"), true);

	using namespace UE::AssetRegistry;
	EDependencyQuery QueryFlags = EDependencyQuery::NoRequirements;
	if (bHard && !bSoft) QueryFlags = EDependencyQuery::Hard;
	else if (bSoft && !bHard) QueryFlags = EDependencyQuery::Soft;
	const FDependencyQuery Query(QueryFlags);

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TSharedPtr<FJsonObject> ByPkg = MakeShared<FJsonObject>();
	int32 TotalDeps = 0;
	for (const FString& Pkg : Packages)
	{
		TArray<FName> Deps;
		AR.GetDependencies(FName(*Pkg), Deps, EDependencyCategory::Package, Query);
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FName& D : Deps) Out.Add(MakeShared<FJsonValueString>(D.ToString()));
		ByPkg->SetArrayField(Pkg, Out);
		TotalDeps += Deps.Num();
	}

	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("dependenciesByPackage"), ByPkg);
	Result->SetNumberField(TEXT("totalDependencies"), TotalDeps);
	Result->SetNumberField(TEXT("queriedPackages"), Packages.Num());
	return MCPResult(Result);
}
// ─── #579 asset(get_primary_asset_ids) ──────────────────────────────
// Enumerate AssetManager-registered FPrimaryAssetIds, optionally filtered to a
// single type, so callers can verify a primary-asset registration without
// Python. Each entry carries the id, type, name, and resolved asset path.
TSharedPtr<FJsonValue> FAssetHandlers::GetPrimaryAssetIds(const TSharedPtr<FJsonObject>& Params)
{
	UAssetManager* AM = UAssetManager::GetIfInitialized();
	if (!AM) return MCPError(TEXT("AssetManager is not initialized for this project"));

	const FString TypeFilter = OptionalString(Params, TEXT("type"));
	const int32 MaxResults = OptionalInt(Params, TEXT("maxResults"), 1000);

	TArray<FPrimaryAssetType> Types;
	if (!TypeFilter.IsEmpty())
	{
		Types.Add(FPrimaryAssetType(*TypeFilter));
	}
	else
	{
		TArray<FPrimaryAssetTypeInfo> TypeInfos;
		AM->GetPrimaryAssetTypeInfoList(TypeInfos);
		for (const FPrimaryAssetTypeInfo& Info : TypeInfos)
		{
			Types.Add(FPrimaryAssetType(Info.PrimaryAssetType));
		}
	}

	TArray<TSharedPtr<FJsonValue>> Out;
	int32 Total = 0;
	for (const FPrimaryAssetType& Type : Types)
	{
		TArray<FPrimaryAssetId> Ids;
		AM->GetPrimaryAssetIdList(Type, Ids);
		for (const FPrimaryAssetId& Id : Ids)
		{
			++Total;
			if (Out.Num() >= MaxResults) continue;
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("primaryAssetId"), Id.ToString());
			E->SetStringField(TEXT("type"), Id.PrimaryAssetType.ToString());
			E->SetStringField(TEXT("name"), Id.PrimaryAssetName.ToString());
			E->SetStringField(TEXT("assetPath"), AM->GetPrimaryAssetPath(Id).ToString());
			Out.Add(MakeShared<FJsonValueObject>(E));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("primaryAssetIds"), Out);
	Result->SetNumberField(TEXT("count"), Out.Num());
	Result->SetNumberField(TEXT("total"), Total);
	Result->SetNumberField(TEXT("typeCount"), Types.Num());
	if (Total > Out.Num()) Result->SetBoolField(TEXT("truncated"), true);
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

	if (MCPIsProtectedAssetPath(SourcePath))      return MCPProtectedPathError(SourcePath);
	if (MCPIsProtectedAssetPath(DestinationPath)) return MCPProtectedPathError(DestinationPath);

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

	// Count how many actually landed at the destination, and record the entry
	// that puts each one back, so the inverse covers exactly the moves that
	// happened rather than the ones that were asked for.
	int32 Succeeded = 0;
	TArray<TSharedPtr<FJsonValue>> ReverseRenames;
	for (const FAssetRenameData& Data : BatchRenames)
	{
		const FString DestFullPath = FString::Printf(TEXT("%s/%s.%s"),
			*Data.NewPackagePath, *Data.NewName, *Data.NewName);
		if (UEditorAssetLibrary::DoesAssetExist(DestFullPath))
		{
			Succeeded++;

			FString OriginalPackagePath = Data.NewPackagePath;
			if (OriginalPackagePath.StartsWith(DestinationPath))
			{
				OriginalPackagePath = SourcePath + OriginalPackagePath.Mid(DestinationPath.Len());
			}
			TSharedPtr<FJsonObject> Reverse = MakeShared<FJsonObject>();
			Reverse->SetStringField(TEXT("sourcePath"), DestFullPath);
			Reverse->SetStringField(TEXT("newPackagePath"), OriginalPackagePath);
			Reverse->SetStringField(TEXT("newName"), Data.NewName);
			ReverseRenames.Add(MakeShared<FJsonValueObject>(Reverse));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("sourcePath"), SourcePath);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetNumberField(TEXT("totalAssets"), FoundAssets.Num());
	Result->SetNumberField(TEXT("renamedCount"), Succeeded);
	Result->SetBoolField(TEXT("allSucceeded"), bOk && Succeeded == BatchRenames.Num());
	Result->SetBoolField(TEXT("unchanged"), Succeeded == 0);

	// The inverse is per asset, not the mirrored move_folder call: reversing
	// the folders would also drag anything that was already sitting under
	// destinationPath before this ran, which this call never touched.
	if (ReverseRenames.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("renames"), ReverseRenames);
		MCPSetRollback(Result, TEXT("bulk_rename_assets"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetBoolField(TEXT("rollbackVerified"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("UNVERIFIED. The inverse moves each asset that landed back to the package path it came from, and this move left ")
			TEXT("an ObjectRedirector at each of those old paths. Whether IAssetTools::RenameAssets will rename over a destination ")
			TEXT("package occupied by such a redirector is not stated in the shipped IAssetTools.h and FAssetRenameManager is not ")
			TEXT("distributed, so it has not been established here. If it refuses, this rollback fails. Two things do not come back ")
			TEXT("in any case: bulk_rename_assets refuses World assets, so a level this move carried has to be returned with ")
			TEXT("asset(rename); and the redirector stubs stay, as do the ones the reverse move leaves at the new paths. ")
			TEXT("Run asset(fixup_redirectors) before a rollback to clear the old stubs, and again after it."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), TEXT("No asset landed at the destination, so there is nothing to undo."));
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// #212 - create empty content browser folder under /Game (or any mount point).
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
	// Every directory this call brought into being, leaves and intermediates
	// alike. `Created` stays the leaf-only list the response has always
	// reported; this is what the rollback has to remove.
	TArray<FString> MadeDirectories;
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
		// MakeDirectory builds the whole tree, so /Game/A/B/C can create A and B
		// as well as C. Recording only the leaf left the intermediates behind
		// on rollback. Walk up first and collect every segment that is missing.
		TArray<FString> MissingChain;
		{
			// The mount root is the floor. Contains("/") is true of "/Game"
			// because of the leading slash, so it never stopped the walk on its
			// own: it only held because DoesDirectoryExist happens to answer
			// true for a mount root. Ask the package system where the floor is
			// instead of relying on that.
			const FName MountPoint = FPackageName::GetPackageMountPoint(Norm);
			const FString MountRoot = MountPoint.IsNone()
				? FString()
				: FString(TEXT("/")) + MountPoint.ToString();

			FString Walk = Norm;
			while (!Walk.IsEmpty() && Walk != TEXT("/"))
			{
				if (!MountRoot.IsEmpty() && Walk == MountRoot) break;
				if (UEditorAssetLibrary::DoesDirectoryExist(Walk)) break;
				MissingChain.Add(Walk);
				const FString Parent = FPaths::GetPath(Walk);
				if (Parent.IsEmpty() || Parent == Walk) break;
				Walk = Parent;
			}
		}

		const bool bOk = UEditorAssetLibrary::MakeDirectory(Norm);
		if (bOk)
		{
			Created.Add(MakeShared<FJsonValueString>(Norm));
			for (const FString& Made : MissingChain) MadeDirectories.AddUnique(Made);
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
	Result->SetBoolField(TEXT("unchanged"), Created.Num() == 0);

	// Only the directories this call made. A path that already existed is not
	// this call's to delete. Deepest first, so delete_folder takes a child out
	// before its parent: a parent still holding a child is not empty, and the
	// default force=false leaves a non-empty directory alone.
	if (MadeDirectories.Num() > 0)
	{
		MadeDirectories.Sort([](const FString& A, const FString& B) { return A.Len() > B.Len(); });
		TArray<TSharedPtr<FJsonValue>> RollbackPaths;
		for (const FString& Made : MadeDirectories) RollbackPaths.Add(MakeShared<FJsonValueString>(Made));

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("paths"), RollbackPaths);
		MCPSetRollback(Result, TEXT("delete_folder"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The inverse removes every directory this call brought into being, intermediates included, deepest first. It runs ")
			TEXT("with the default force=false, so a directory that has since had assets put into it is reported not_empty and left ")
			TEXT("standing rather than deleted out from under them."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), TEXT("No directory was created, so there is nothing to undo."));
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// Delete content browser folder(s). Counterpart to create_folder + delete_asset
// - the bare delete_asset leaves the parent directory entry behind, producing
// orphan dirs in the content browser. Default is safe (empty-folder only);
// pass force=true for the Content Browser "Delete folder" behaviour that
// removes any assets still inside it.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::DeleteFolder(const TSharedPtr<FJsonObject>& Params)
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

	const bool bForce = OptionalBool(Params, TEXT("force"), false);

	TArray<TSharedPtr<FJsonValue>> Entries;
	int32 Deleted = 0, Absent = 0, Failed = 0;
	// The directories this call actually removed, and whether any of them took
	// assets with them. Both feed the rollback record below.
	TArray<TSharedPtr<FJsonValue>> DeletedPaths;
	int32 AssetsDeleted = 0;
	// DeleteDirectory takes the whole subtree, so a folder with subdirectories
	// loses more than the path that was named. create_folder can only put back
	// the paths it is handed, and the empty subdirectories are not enumerable
	// from the asset list, so this is counted and disclosed rather than claimed.
	int32 SubdirectoriesDeleted = 0;

	for (const FString& P : Paths)
	{
		FString Norm = P;
		Norm.TrimStartAndEndInline();
		Norm.RemoveFromEnd(TEXT("/"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Norm);

		if (!Norm.StartsWith(TEXT("/")))
		{
			Entry->SetStringField(TEXT("status"), TEXT("failed"));
			Entry->SetStringField(TEXT("reason"), TEXT("invalid_path"));
			Failed++;
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}

		if (MCPIsProtectedAssetPath(Norm))
		{
			Entry->SetStringField(TEXT("status"), TEXT("failed"));
			Entry->SetStringField(TEXT("reason"), TEXT("protected_path"));
			Failed++;
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}

		if (!UEditorAssetLibrary::DoesDirectoryExist(Norm))
		{
			Entry->SetStringField(TEXT("status"), TEXT("absent"));
			Absent++;
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}

		TArray<FString> Contained = UEditorAssetLibrary::ListAssets(Norm, /*Recursive=*/true);
		Entry->SetNumberField(TEXT("assetCount"), Contained.Num());

		if (Contained.Num() > 0 && !bForce)
		{
			Entry->SetStringField(TEXT("status"), TEXT("failed"));
			Entry->SetStringField(TEXT("reason"), TEXT("not_empty"));
			TArray<TSharedPtr<FJsonValue>> Sample;
			const int32 N = FMath::Min(Contained.Num(), 25);
			for (int32 i = 0; i < N; ++i) Sample.Add(MakeShared<FJsonValueString>(Contained[i]));
			Entry->SetArrayField(TEXT("assets"), Sample);
			Failed++;
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}

		const bool bOk = UEditorAssetLibrary::DeleteDirectory(Norm);
		if (bOk)
		{
			Entry->SetStringField(TEXT("status"), TEXT("deleted"));
			if (Contained.Num() > 0) Entry->SetNumberField(TEXT("assetsDeleted"), Contained.Num());
			DeletedPaths.Add(MakeShared<FJsonValueString>(Norm));
			AssetsDeleted += Contained.Num();
			// Any contained asset sitting below Norm rather than directly in it
			// proves there was a subdirectory that went with the delete.
			{
				TSet<FString> NestedDirs;
				for (const FString& Held : Contained)
				{
					const FString HeldDir = FPaths::GetPath(Held);
					if (HeldDir.Len() > Norm.Len() && HeldDir.StartsWith(Norm)) NestedDirs.Add(HeldDir);
				}
				SubdirectoriesDeleted += NestedDirs.Num();
			}
			Deleted++;
		}
		else
		{
			Entry->SetStringField(TEXT("status"), TEXT("failed"));
			Entry->SetStringField(TEXT("reason"), TEXT("delete_failed"));
			Failed++;
		}
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("entries"), Entries);
	Result->SetNumberField(TEXT("deletedCount"), Deleted);
	Result->SetNumberField(TEXT("absentCount"), Absent);
	Result->SetNumberField(TEXT("failedCount"), Failed);
	Result->SetBoolField(TEXT("allSucceeded"), Failed == 0);
	Result->SetBoolField(TEXT("unchanged"), Deleted == 0);

	if (DeletedPaths.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("paths"), DeletedPaths);
		MCPSetRollback(Result, TEXT("create_folder"), Payload);
		Result->SetNumberField(TEXT("subdirectoriesDeleted"), SubdirectoriesDeleted);
		const bool bLossy = AssetsDeleted > 0 || SubdirectoriesDeleted > 0;
		Result->SetBoolField(TEXT("rollbackLossy"), bLossy);
		if (bLossy)
		{
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("The inverse recreates only the %d directory path(s) that were named, empty. It does not restore the tree ")
				TEXT("beneath them: this delete also took %d subdirectory(ies), and force=true took %d asset(s) inside them, which ")
				TEXT("no call brings back. Only a delete of empty, leaf directories is fully undone by this rollback."),
				DeletedPaths.Num(), SubdirectoriesDeleted, AssetsDeleted));
		}
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), TEXT("No directory was deleted, so there is nothing to undo."));
	}
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
		UObject* Probe = MCPLoadAssetObject(AssetPath);
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

	const FMCPAssetPathForms Forms = MCPAssetPathForms(AssetPath);
	const FString PackageName = Forms.PackagePath;
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
			if (UObject* Existing = StaticFindObject(UObject::StaticClass(), nullptr, *Forms.ObjectPath))
			{
				if (AES->FindEditorsForAsset(Existing).Num() > 0)
				{
					AES->CloseAllEditorsForAsset(Existing);
					bClosedEditor = true;
				}
			}
		}
	}

	// #820: a Blueprint's generated class and CDO survive ResetLoaders, so the
	// old path handed back the same CDO and container properties (a TMap above
	// all) still read their pre-reload contents while scalars looked restored.
	// UPackageTools::ReloadPackages is the editor's own re-read: it rebuilds the
	// package, recompiles the Blueprint and reinstances against the new class.
	// A package the editor refuses to release is reported, not papered over.
	UPackage* ExistingPkg = FindPackage(nullptr, *PackageName);
	UObject* PreviousObject = StaticFindObject(UObject::StaticClass(), nullptr, *Forms.ObjectPath);
	const TWeakObjectPtr<UObject> PreviousWeak(PreviousObject);
	const bool bWasDirty = ExistingPkg != nullptr && ExistingPkg->IsDirty();
	const bool bDiscardUnsaved = OptionalBool(Params, TEXT("discardUnsaved"), false);
	if (bWasDirty && !bDiscardUnsaved)
	{
		return MCPError(FString::Printf(
			TEXT("Package '%s' has unsaved changes; reloading would discard them. Save it first, or pass discardUnsaved=true."),
			*PackageName));
	}

	FString ReloadMethod = TEXT("load");
	FString ReloadError;
	if (ExistingPkg)
	{
		FText PackageToolsError;
		TArray<UPackage*> ToReload;
		ToReload.Add(ExistingPkg);
		const bool bChanged = UPackageTools::ReloadPackages(
			ToReload, PackageToolsError, EReloadPackagesInteractionMode::AssumePositive);
		ReloadMethod = TEXT("reloadPackages");
		if (!PackageToolsError.IsEmpty())
		{
			ReloadError = PackageToolsError.ToString();
		}

		if (!bChanged)
		{
			// Fall back to the loader reset, which recovers the half-shutdown
			// state ReloadPackages will not touch (#279).
			if (UPackage* StillThere = FindPackage(nullptr, *PackageName))
			{
				ResetLoaders(StillThere);
				StillThere->ClearFlags(RF_WasLoaded);
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			ReloadMethod = TEXT("resetLoaders");
		}
	}
	else
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	UObject* Reloaded = LoadObject<UObject>(nullptr, *Forms.ObjectPath, nullptr, LOAD_None);
	const bool bSuccess = Reloaded != nullptr;
	// A package that was already in memory must come back as a new object for
	// the read to be trustworthy. Same pointer means the editor kept its copy.
	const bool bReplaced = PreviousObject == nullptr || !PreviousWeak.IsValid() || Reloaded != PreviousObject;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("packageName"), PackageName);
	Result->SetBoolField(TEXT("reloaded"), bSuccess && bReplaced);
	Result->SetStringField(TEXT("method"), ReloadMethod);
	Result->SetBoolField(TEXT("wasLoaded"), PreviousObject != nullptr);
	Result->SetBoolField(TEXT("objectReplaced"), bReplaced);
	// Never true, and answered the same way asset(reload_package) answers it.
	// Every path that reaches here has already run ReloadPackages, or its
	// fallback of ResetLoaders plus ClearFlags plus a collection, or at minimum
	// a bare CollectGarbage, so editor state moved before this line.
	//
	// !bReplaced does NOT mean nothing happened. It means the editor refused to
	// release the package, which is the same state the branch below reports as
	// success:false with an error saying the object still holds its pre-reload
	// contents. Keeping it in this term produced success:false, reloaded:false
	// and unchanged:true on one result, and a dirty package whose unsaved edits
	// had just been discarded could read unchanged:true as well.
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetStringField(TEXT("idempotencyNote"),
		TEXT("A forced reload is never a no-op: it releases and re-reads the package every time it is called rather than ")
		TEXT("recognising it has nothing to do. wasLoaded, objectReplaced, closedOpenEditor and discardedUnsavedChanges on this ")
		TEXT("result say what it actually moved."));
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("A forced reload throws the in-memory package away and re-reads the file. With discardUnsaved=true anything that ")
		TEXT("was in memory and not on disk is gone, and reloading again re-reads the same file rather than restoring it. ")
		TEXT("There is no inverse action."));
	if (bWasDirty) Result->SetBoolField(TEXT("discardedUnsavedChanges"), true);
	if (bClosedEditor) Result->SetBoolField(TEXT("closedOpenEditor"), true);
	if (Reloaded) Result->SetStringField(TEXT("class"), Reloaded->GetClass()->GetName());
	if (!bSuccess)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), ReloadError.IsEmpty()
			? TEXT("LoadObject returned null after reset; the package file may be corrupt or contain a class the editor cannot resolve.")
			: ReloadError);
	}
	else if (!bReplaced)
	{
		// Loud rather than misleading: the caller would otherwise read stale
		// container values and conclude the file on disk was wrong (#820).
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Package '%s' could not be released, so the object still holds its pre-reload state. Values read now may be stale. Close anything referencing it, or restart the editor.%s"),
			*PackageName, ReloadError.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" Editor reported: %s"), *ReloadError)));
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

	// #931: default to persisting. A write that only marked the package dirty
	// was the whole defect, so opting out has to be deliberate.
	const bool bSave = OptionalBool(Params, TEXT("save"), true);

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;
	UBlueprint* OwningBlueprint = nullptr;
	Asset = MCPResolveAssetToCDO(Asset, &OwningBlueprint); // #568 - author the generated-class CDO for Blueprint paths

	// Resolve the (possibly indexed, possibly subobject-descending) path.
	// Supports "Config.Traits[1].Params.RepresentationActorManagementClass"
	// style writes into instanced subobjects held in arrays (#527).
	FProperty* FinalProp = nullptr;
	void* ValuePtr = nullptr;
	UObject* LeafOwner = nullptr;
	FString ResolveErr;
	if (!MCPJsonProperty::ResolveDottedPath(Asset, PropertyName, FinalProp, ValuePtr, LeafOwner, ResolveErr))
	{
		return MCPError(ResolveErr);
	}

	FString PrevValue;
	FinalProp->ExportText_Direct(PrevValue, ValuePtr, ValuePtr, nullptr, PPF_None);

	// #820: a rollback built from export text replays as an empty map when the
	// key is a struct. Keep the structured previous value for those properties.
	const bool bMapBearing = MCPPropertyText::ContainsMap(FinalProp);
	TSharedPtr<FJsonValue> PrevStructured;
	if (bMapBearing)
	{
		PrevStructured = FMCPJsonSerializer::SerializeValue(ValuePtr, FinalProp);
	}

	Asset->Modify();
	if (LeafOwner && LeafOwner != Asset) LeafOwner->Modify();
	FString SetErr;
	if (!MCPJsonProperty::SetJsonOnProperty(FinalProp, ValuePtr, *ValueField, SetErr))
	{
		return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
	}

	if (LeafOwner) LeafOwner->PostEditChange();
	Asset->PostEditChange();
	Asset->MarkPackageDirty();
	MCPNoteBlueprintCDOWrite(OwningBlueprint, PropertyName);

	FString NewValue;
	FinalProp->ExportText_Direct(NewValue, ValuePtr, ValuePtr, nullptr, PPF_None);

	FString PersistReason;
	const bool bPersisted = MCPPersistAssetWrite(Asset, OwningBlueprint, bSave, PersistReason);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("previousValue"), PrevValue);
	Result->SetStringField(TEXT("value"), NewValue);
	MCPDescribePropertyWritePersistence(Result, Asset, PropertyName, bSave, bPersisted, PersistReason);
	if (bMapBearing)
	{
		Result->SetNumberField(TEXT("mapPairCount"), MCPPropertyText::CountMapPairs(FinalProp, ValuePtr));
		Result->SetField(TEXT("valueJson"), FMCPJsonSerializer::SerializeValue(ValuePtr, FinalProp));
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	if (PrevStructured.IsValid())
	{
		Payload->SetField(TEXT("value"), PrevStructured);
	}
	else
	{
		Payload->SetStringField(TEXT("value"), PrevValue);
	}
	MCPSetRollback(Result, TEXT("set_asset_property"), Payload);
	return MCPResult(Result);
}

// Append reflected TArray values without replacing existing entries. Values
// are converted in temporary property storage first so a bad later element
// cannot leave a partial append.
TSharedPtr<FJsonValue> FAssetHandlers::AppendAssetArrayElements(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
	if (!Params->TryGetArrayField(TEXT("elements"), Elements) || !Elements)
	{
		return MCPError(TEXT("Missing 'elements' array parameter"));
	}
	if (Elements->IsEmpty())
	{
		return MCPError(TEXT("'elements' must contain at least one value"));
	}

	const bool bSave = OptionalBool(Params, TEXT("save"), true);

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;
	UBlueprint* OwningBlueprint = nullptr;
	Asset = MCPResolveAssetToCDO(Asset, &OwningBlueprint);

	FProperty* FinalProp = nullptr;
	void* ValuePtr = nullptr;
	UObject* LeafOwner = nullptr;
	FString ResolveErr;
	if (!MCPJsonProperty::ResolveDottedPath(Asset, PropertyName, FinalProp, ValuePtr, LeafOwner, ResolveErr))
	{
		return MCPError(ResolveErr);
	}

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(FinalProp);
	if (!ArrayProp)
	{
		return MCPError(FString::Printf(
			TEXT("Property '%s' is '%s', not a TArray"),
			*PropertyName,
			FinalProp ? *FinalProp->GetCPPType() : TEXT("unknown")));
	}

	TArray<void*> StagedElements;
	StagedElements.Reserve(Elements->Num());
	auto DestroyStagedElements = [&]()
	{
		for (void* StagedValue : StagedElements)
		{
			ArrayProp->Inner->DestroyAndFreeValue(StagedValue);
		}
		StagedElements.Empty();
	};

	for (int32 Index = 0; Index < Elements->Num(); ++Index)
	{
		void* StagedValue = ArrayProp->Inner->AllocateAndInitializeValue();
		StagedElements.Add(StagedValue);

		FString ElementError;
		if (!MCPJsonProperty::SetJsonOnProperty(ArrayProp->Inner, StagedValue, (*Elements)[Index], ElementError))
		{
			DestroyStagedElements();
			return MCPError(FString::Printf(
				TEXT("Failed to convert elements[%d] for '%s': %s"),
				Index,
				*PropertyName,
				*ElementError));
		}
	}

	const TSharedPtr<FJsonValue> PreviousValue = FMCPJsonSerializer::SerializeValue(ValuePtr, ArrayProp);
	if (!PreviousValue.IsValid())
	{
		DestroyStagedElements();
		return MCPError(FString::Printf(TEXT("Failed to serialize the previous value of '%s'"), *PropertyName));
	}

	FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);
	const int32 PreviousNum = ArrayHelper.Num();
	const int32 AppendedCount = StagedElements.Num();

	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "AppendAssetArrayElements", "Append asset array elements"));
	Asset->Modify();
	if (LeafOwner && LeafOwner != Asset) LeafOwner->Modify();

	ArrayHelper.AddValues(AppendedCount);
	for (int32 Index = 0; Index < AppendedCount; ++Index)
	{
		ArrayProp->Inner->CopyCompleteValue(ArrayHelper.GetRawPtr(PreviousNum + Index), StagedElements[Index]);
	}
	DestroyStagedElements();

	if (LeafOwner && LeafOwner != Asset) LeafOwner->PostEditChange();
	Asset->PostEditChange();
	Asset->MarkPackageDirty();
	MCPNoteBlueprintCDOWrite(OwningBlueprint, PropertyName);

	FString PersistReason;
	const bool bPersisted = MCPPersistAssetWrite(Asset, OwningBlueprint, bSave, PersistReason);

	TArray<TSharedPtr<FJsonValue>> AppendedIndices;
	AppendedIndices.Reserve(AppendedCount);
	for (int32 Index = 0; Index < AppendedCount; ++Index)
	{
		AppendedIndices.Add(MakeShared<FJsonValueNumber>(PreviousNum + Index));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("elementType"), ArrayProp->Inner->GetCPPType());
	Result->SetNumberField(TEXT("previousNum"), PreviousNum);
	Result->SetNumberField(TEXT("appendedCount"), AppendedCount);
	Result->SetNumberField(TEXT("newNum"), PreviousNum + AppendedCount);
	Result->SetArrayField(TEXT("appendedIndices"), AppendedIndices);
	Result->SetField(TEXT("previousValue"), PreviousValue);
	MCPDescribePropertyWritePersistence(Result, Asset, PropertyName, bSave, bPersisted, PersistReason);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetField(TEXT("value"), PreviousValue);
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
	// One rollback item per texture, carrying the three properties this action
	// writes, as they stood before the first write to that texture. Keyed by
	// path because a caller may list the same texture under two groups, and
	// bulk_set_asset_properties rejects a duplicate assetPath.
	TArray<TSharedPtr<FJsonValue>> RollbackItems;
	TSet<FString> RollbackSeen;

	for (const auto& Pair : (*GroupsObj)->Values)
	{
		const FString Group(*Pair.Key);
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
			UTexture2D* Tex = LoadAssetByPath<UTexture2D>(TexPath);
			if (!Tex)
			{
				TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
				F->SetStringField(TEXT("path"), TexPath);
				F->SetStringField(TEXT("error"), TEXT("Texture not found or not a Texture2D"));
				Failed.Add(MakeShared<FJsonValueObject>(F));
				continue;
			}
			const FString TexObjectPath = Tex->GetPathName();
			if (!RollbackSeen.Contains(TexObjectPath))
			{
				RollbackSeen.Add(TexObjectPath);
				TSharedPtr<FJsonObject> Prev = MakeShared<FJsonObject>();
				// UPROPERTY names, and the enumerator spellings the bulk
				// property writer resolves by name.
				Prev->SetStringField(TEXT("CompressionSettings"),
					StaticEnum<TextureCompressionSettings>()->GetNameStringByValue((int64)Tex->CompressionSettings.GetValue()));
				Prev->SetStringField(TEXT("LODGroup"),
					StaticEnum<TextureGroup>()->GetNameStringByValue((int64)Tex->LODGroup.GetValue()));
				Prev->SetBoolField(TEXT("SRGB"), Tex->SRGB != 0);
				TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("assetPath"), TexObjectPath);
				Item->SetObjectField(TEXT("properties"), Prev);
				RollbackItems.Add(MakeShared<FJsonValueObject>(Item));
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
	// Not MCPSetUpdated: it writes a bool named "updated", which is the name
	// this result already uses for the per-texture array.
	Result->SetBoolField(TEXT("unchanged"), Updated.Num() == 0);
	Result->SetArrayField(TEXT("updated"), Updated);
	Result->SetNumberField(TEXT("updatedCount"), Updated.Num());
	if (Failed.Num() > 0)
	{
		Result->SetArrayField(TEXT("failed"), Failed);
		Result->SetNumberField(TEXT("failedCount"), Failed.Num());
	}

	// bulk_set_asset_properties rejects a batch over this size outright, so past
	// it the inverse is a call that cannot execute. Kept in step with
	// MaxBulkPropertyAssets in AssetHandlers_BulkProperties.cpp; that constant
	// is file-local to its own translation unit and cannot be shared without
	// moving it to a header, so the number is asserted here rather than copied
	// silently.
	const int32 MaxBulkRollbackItems = 500;
	if (RollbackItems.Num() > 0 && RollbackItems.Num() <= MaxBulkRollbackItems)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("items"), RollbackItems);
		// Without this, bulk_set_asset_properties stops at the first item it
		// cannot apply and restores nothing. A rollback should put back
		// everything it still can and report the rest.
		Payload->SetBoolField(TEXT("continueOnError"), true);
		MCPSetRollback(Result, TEXT("bulk_set_asset_properties"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else if (RollbackItems.Num() > MaxBulkRollbackItems)
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("This call wrote %d textures. The inverse is one bulk_set_asset_properties call and that action refuses a batch ")
			TEXT("larger than %d, so emitting one here would hand the flow engine a rollback that cannot run. Split the call into ")
			TEXT("batches of %d or fewer to get a working inverse for each."),
			RollbackItems.Num(), MaxBulkRollbackItems, MaxBulkRollbackItems));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), TEXT("No texture was written, so there is nothing to undo."));
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
			const FString Key(*Pair.Key);
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


// #760: copy assets and their dependencies into ANOTHER project's Content
// directory - the scripted form of the content browser's Migrate.
//
// This is the half of cross-project work the bridge can actually do. The
// bridge attaches to one editor, so it cannot drive two projects at once; what
// it can do is push assets out of the project it is attached to. Combined with
// project(set_project) + editor(restart_editor), that makes the full round trip
// reachable: attach to the reference project, batch_retarget_animations there,
// migrate the results into the game project, switch back.
//
// Params:
//   assetPaths (string[]) OR assetPath, destinationContentDir (the TARGET
//   project's Content folder), includeDependencies? (default true),
//   onConflict? ("skip" default | "overwrite"), dryRun? (default false)
TSharedPtr<FJsonValue> FAssetHandlers::MigrateAssets(const TSharedPtr<FJsonObject>& Params)
{
	TArray<FString> AssetPaths;
	const TArray<TSharedPtr<FJsonValue>>* PathArray = nullptr;
	if (Params->TryGetArrayField(TEXT("assetPaths"), PathArray) && PathArray)
	{
		for (const TSharedPtr<FJsonValue>& V : *PathArray)
		{
			FString P;
			if (V.IsValid() && V->TryGetString(P) && !P.IsEmpty()) AssetPaths.Add(P);
		}
	}
	FString Single;
	if (Params->TryGetStringField(TEXT("assetPath"), Single) && !Single.IsEmpty())
	{
		AssetPaths.AddUnique(Single);
	}
	if (AssetPaths.Num() == 0)
	{
		return MCPError(TEXT("Provide assetPaths (string[]) or assetPath."));
	}

	FString DestContentDir;
	if (auto Err = RequireString(Params, TEXT("destinationContentDir"), DestContentDir)) return Err;
	FPaths::NormalizeDirectoryName(DestContentDir);
	if (!FPaths::DirectoryExists(DestContentDir))
	{
		return MCPError(FString::Printf(
			TEXT("destinationContentDir does not exist: %s. Point it at the TARGET project's Content folder (e.g. D:/Games/MyGame/Content)."),
			*DestContentDir));
	}
	// Migrating into our own Content folder would be a no-op that still reports
	// success, and is almost always a wrong path rather than an intent.
	FString OwnContent = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	FString DestFull = FPaths::ConvertRelativePathToFull(DestContentDir);
	// Normalise both sides: the caller's path arrives with forward slashes and
	// no trailing separator, ProjectContentDir has the platform form and a
	// trailing one, so a raw compare let the no-op case through.
	FPaths::NormalizeDirectoryName(OwnContent);
	FPaths::NormalizeDirectoryName(DestFull);
	if (DestFull.Equals(OwnContent, ESearchCase::IgnoreCase))
	{
		return MCPError(TEXT("destinationContentDir is this project's own Content folder; migrate targets a DIFFERENT project."));
	}

	// Resolve every asset before migrating any: a half-done migration with an
	// error at the end is worse than a clean refusal.
	const bool bAllowDirty = OptionalBool(Params, TEXT("allowDirty"), false);
	TArray<FName> PackageNames;
	TArray<TSharedPtr<FJsonValue>> Resolved;
	for (const FString& Path : AssetPaths)
	{
		TSharedPtr<FJsonValue> LoadError;
		UObject* Asset = MCPRequireAssetObject(Path, LoadError);
		if (!Asset) return LoadError;
		UPackage* Package = Asset->GetOutermost();
		const FName PackageName(*Package->GetName());

		// MigratePackages copies files off disk. A package that has never been
		// saved, or has unsaved edits, migrates its LAST SAVED state or nothing
		// at all - and reports neither. Refuse instead.
		FString PackageFile;
		const bool bOnDisk = FPackageName::DoesPackageExist(PackageName.ToString(), &PackageFile);
		if (!bOnDisk)
		{
			return MCPError(FString::Printf(
				TEXT("'%s' has never been saved to disk, so there is no file to migrate. Save it first (asset(save)) - migrate copies files, it does not serialise in-memory state."),
				*Path));
		}
		if (Package->IsDirty() && !bAllowDirty)
		{
			return MCPError(FString::Printf(
				TEXT("'%s' has unsaved changes. Migrating now would copy the last SAVED state and silently omit your edits. Save it first (asset(save)), or pass allowDirty=true to migrate the on-disk version deliberately."),
				*Path));
		}

		PackageNames.AddUnique(PackageName);
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("assetPath"), Asset->GetPathName());
		R->SetStringField(TEXT("package"), PackageName.ToString());
		Resolved.Add(MakeShared<FJsonValueObject>(R));
	}

	const bool bIncludeDeps = OptionalBool(Params, TEXT("includeDependencies"), true);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	if (!OnConflict.Equals(TEXT("skip"), ESearchCase::IgnoreCase) &&
		!OnConflict.Equals(TEXT("overwrite"), ESearchCase::IgnoreCase))
	{
		return MCPError(TEXT("onConflict must be 'skip' or 'overwrite'."));
	}
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("assets"), Resolved);
	Result->SetNumberField(TEXT("packageCount"), PackageNames.Num());
	Result->SetStringField(TEXT("destinationContentDir"), DestContentDir);
	Result->SetBoolField(TEXT("includeDependencies"), bIncludeDeps);
	Result->SetStringField(TEXT("onConflict"), OnConflict);

	if (bDryRun)
	{
		Result->SetBoolField(TEXT("dryRun"), true);
		Result->SetStringField(TEXT("note"), TEXT("Dry run - nothing was copied. Re-run without dryRun to migrate."));
		return MCPResult(Result);
	}

	FMigrationOptions Options;
	// bPrompt is documented as always false through scripting; setting it would
	// block the bridge on a modal dialog with nobody to answer it.
	Options.bPrompt = false;
	Options.bIgnoreDependencies = !bIncludeDeps;
	Options.AssetConflict = OnConflict.Equals(TEXT("overwrite"), ESearchCase::IgnoreCase)
		? EAssetMigrationConflict::Overwrite
		: EAssetMigrationConflict::Skip;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	AssetToolsModule.Get().MigratePackages(PackageNames, DestContentDir, Options);

	// MigratePackages returns void and reports through the message log, so the
	// only honest confirmation is to look for the files afterwards rather than
	// claim success on the call returning.
	int32 Present = 0;
	TArray<TSharedPtr<FJsonValue>> Missing;
	for (const FName& Pkg : PackageNames)
	{
		FString Rel = Pkg.ToString();
		if (Rel.StartsWith(TEXT("/Game/"))) Rel = Rel.RightChop(6);
		const FString Candidate = DestContentDir / Rel + TEXT(".uasset");
		if (FPaths::FileExists(Candidate)) ++Present;
		else Missing.Add(MakeShared<FJsonValueString>(Pkg.ToString()));
	}
	Result->SetNumberField(TEXT("packagesPresentAtDestination"), Present);
	if (Missing.Num() > 0)
	{
		Result->SetArrayField(TEXT("packagesNotFoundAtDestination"), Missing);
		Result->SetStringField(TEXT("note"), TEXT("Some packages were not found at the destination afterwards. With onConflict=skip that is expected when they already existed; otherwise check the Message Log (editor(get_message_log, logName='AssetTools')) for what the migration reported."));
	}
	return MCPResult(Result);
}
