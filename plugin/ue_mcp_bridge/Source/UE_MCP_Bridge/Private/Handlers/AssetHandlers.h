#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FAssetHandlers
{
public:
	// Register all asset handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Handler implementations
	static TSharedPtr<FJsonValue> ListAssets(const TSharedPtr<FJsonObject>& Params);

	// UV authoring and inspection, in AssetHandlers_UV.cpp. There was no UV
	// surface at all: one read-only peek at a single channel off render data.
	// Lightmap settings themselves stay with asset(set_property); what earns a
	// handler here is that writing them does nothing until the mesh rebuilds.
	static TSharedPtr<FJsonValue> ReadUvChannels(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetUvChannelCount(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> UnwrapUvs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> TransformUvs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GenerateLightmapUvs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ExportUvLayout(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CheckUvs(const TSharedPtr<FJsonObject>& Params);

	// Procedural mesh operations, in AssetHandlers_GeometryScript.cpp. Geometry
	// Script is an engine PLUGIN and is reached by reflection rather than linked,
	// so a project without it gets a named refusal instead of a broken build.
	// Each of these defaults to writing a SEPARATE output asset, whose inverse is
	// a complete delete rollback; inPlace=true is the destructive form and says
	// that its edit cannot be undone.
	static TSharedPtr<FJsonValue> SimplifyMesh(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemeshMesh(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MirrorMesh(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> FillMeshHoles(const TSharedPtr<FJsonObject>& Params);
	// Simple collision built from the mesh's own geometry, or cleared. Not a
	// property write: what would go into AggGeom is the output of a decomposition
	// solver, so there is no value a caller could supply to set_property.
	static TSharedPtr<FJsonValue> GenerateMeshCollision(const TSharedPtr<FJsonObject>& Params);
	// Plane-slice fracture into separate StaticMesh assets. NOT a
	// GeometryCollection: the engine's fracture entry points carry no UFUNCTION,
	// so reflection cannot reach them. The result says so.
	static TSharedPtr<FJsonValue> FractureMesh(const TSharedPtr<FJsonObject>& Params);

	// Project-wide asset hygiene, in AssetHandlers_Hygiene.cpp. The audit sweeps
	// for unreferenced assets, broken references, duplicates, naming violations
	// and redirector stubs; the fix applies the two of those a machine can act on
	// safely, with a preflight, a dryRun that defaults to TRUE, and a quarantine
	// move (which has an exact inverse) in place of a delete.
	static TSharedPtr<FJsonValue> AuditAssetHygiene(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> FixAssetHygiene(const TSharedPtr<FJsonObject>& Params);

	static TSharedPtr<FJsonValue> SearchAssets(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadAssetProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DuplicateAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RenameAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MoveAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteAssetBatch(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BulkRename(const TSharedPtr<FJsonObject>& Params);
	// Bounded redirector clean-up after a move (#908). Lives in
	// AssetHandlers_Redirectors.cpp.
	static TSharedPtr<FJsonValue> FixupRedirectors(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateDataAsset(const TSharedPtr<FJsonObject>& Params);
	// Bounded batch create-or-update of UDataAsset instances. Lives in
	// AssetHandlers_BulkUpsert.cpp.
	static TSharedPtr<FJsonValue> BulkUpsertDataAssets(const TSharedPtr<FJsonObject>& Params);
	// Inverse of BulkUpsertDataAssets, driven by the rollback descriptor that
	// call emits. Not a first-class action.
	static TSharedPtr<FJsonValue> BulkRestoreDataAssets(const TSharedPtr<FJsonObject>& Params);
	// #726: create an asset of any concrete UObject class via its registered
	// factory (or NewObject fallback), not just UDataAsset subclasses.
	static TSharedPtr<FJsonValue> CreateAssetByClass(const TSharedPtr<FJsonObject>& Params);
	// A named subobject inside an existing asset's package (#975). Lives in
	// AssetHandlers_Subobject.cpp.
	static TSharedPtr<FJsonValue> CreateSubobject(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SaveAsset(const TSharedPtr<FJsonObject>& Params);
	// #429: bulk save of every dirty package - one-shot end-of-workflow flush.
	static TSharedPtr<FJsonValue> SaveAllDirty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListTextures(const TSharedPtr<FJsonObject>& Params);

	// DataTable handlers
	static TSharedPtr<FJsonValue> CreateDataTable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadDataTable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReimportDataTable(const TSharedPtr<FJsonObject>& Params);
	// #437: single-row append/update without re-serializing the whole table.
	static TSharedPtr<FJsonValue> SetDataTableRow(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveDataTableRow(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetDataTableRow(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetDataTableCell(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RenameDataTableRow(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> FillDataTableFromJson(const TSharedPtr<FJsonObject>& Params);

	// CurveTable handlers
	static TSharedPtr<FJsonValue> CreateCurveTable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadCurveTable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportCurveTable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddCurveTableRow(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveCurveTableRow(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RenameCurveTableRow(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetCurveTableKeys(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetCurveTableKeys(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddCurveTableKey(const TSharedPtr<FJsonObject>& Params);

	// FBX import handlers
	static TSharedPtr<FJsonValue> ImportStaticMesh(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportSkeletalMesh(const TSharedPtr<FJsonObject>& Params);
	// #595: read/write Chaos cloth data on a skeletal mesh's clothing assets.
	static TSharedPtr<FJsonValue> ReadClothData(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetClothConfig(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportAnimation(const TSharedPtr<FJsonObject>& Params);

	// Mesh material handlers
	static TSharedPtr<FJsonValue> SetMeshMaterial(const TSharedPtr<FJsonObject>& Params);
	// #822: batch counterpart to SetMeshMaterial. Slots are addressable by name
	// as well as index, for static and skeletal meshes alike.
	static TSharedPtr<FJsonValue> SetMeshMaterialsBatch(const TSharedPtr<FJsonObject>& Params);

	// Mesh pivot handlers
	static TSharedPtr<FJsonValue> RecenterPivot(const TSharedPtr<FJsonObject>& Params);

	// Texture handlers
	static TSharedPtr<FJsonValue> ListTextureProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetTextureProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportTexture(const TSharedPtr<FJsonObject>& Params);
	// #697: export a texture to PNG on disk, and compare two textures.
	static TSharedPtr<FJsonValue> ExportTexture(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CompareTextures(const TSharedPtr<FJsonObject>& Params);
	// #430: one-call batch of texture imports - loops AssetImportTasks inside the editor.
	static TSharedPtr<FJsonValue> ImportTextureBatch(const TSharedPtr<FJsonObject>& Params);
	// Create and persist a TextureRenderTarget2D with explicit render settings.
	static TSharedPtr<FJsonValue> CreateRenderTarget2D(const TSharedPtr<FJsonObject>& Params);

	// StringTable handlers
	static TSharedPtr<FJsonValue> CreateStringTable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadStringTable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListStringTableKeys(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetStringTableEntry(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetStringTableEntry(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveStringTableEntry(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportStringTable(const TSharedPtr<FJsonObject>& Params);
	// CSV import with key-set validation and an explicit save result (#978).
	static TSharedPtr<FJsonValue> ImportStringTableCsv(const TSharedPtr<FJsonObject>& Params);

	// Export
	static TSharedPtr<FJsonValue> ExportAsset(const TSharedPtr<FJsonObject>& Params);

	// Reimport
	static TSharedPtr<FJsonValue> ReimportAsset(const TSharedPtr<FJsonObject>& Params);

	// Socket handlers
	static TSharedPtr<FJsonValue> AddSocket(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveSocket(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListSockets(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetSocketTransform(const TSharedPtr<FJsonObject>& Params);

	// #420: nested-path UPROPERTY setter for any asset (materials, datatables,
	// data assets, etc.) so callers don't read-modify-write struct copies.
	static TSharedPtr<FJsonValue> SetAssetProperty(const TSharedPtr<FJsonObject>& Params);
	// Append prevalidated JSON values to a reflected TArray without replacing
	// existing entries. Supports native and user-defined struct elements.
	static TSharedPtr<FJsonValue> AppendAssetArrayElements(const TSharedPtr<FJsonObject>& Params);
	// Batch counterpart to SetAssetProperty. Preflights every asset/property
	// before mutating any package and emits a replayable rollback payload.
	static TSharedPtr<FJsonValue> BulkSetAssetProperties(const TSharedPtr<FJsonObject>& Params);
	// #421: batch texture settings by canonical type (Normal/Grayscale/BaseColor/HDR).
	static TSharedPtr<FJsonValue> SetTextureSettingsByType(const TSharedPtr<FJsonObject>& Params);
	// #421: one-call factory for an Interchange pipeline asset with the
	// 15-property mesh-import boilerplate already configured.
	static TSharedPtr<FJsonValue> CreateInterchangePipeline(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReloadPackage(const TSharedPtr<FJsonObject>& Params);

	// v0.7.8 - FTS5-backed asset search (stubs)
	static TSharedPtr<FJsonValue> SearchAssetsFTS(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReindexAssetsFTS(const TSharedPtr<FJsonObject>& Params);

	// v0.7.19 issue #150 - AssetRegistry referencers for a set of packages
	static TSharedPtr<FJsonValue> GetReferencers(const TSharedPtr<FJsonObject>& Params);

	// issue #588 - AssetRegistry forward dependencies for a set of packages
	static TSharedPtr<FJsonValue> GetDependencies(const TSharedPtr<FJsonObject>& Params);

	// issue #579 - AssetManager primary-asset-id enumeration / verification
	static TSharedPtr<FJsonValue> GetPrimaryAssetIds(const TSharedPtr<FJsonObject>& Params);

	// v1.0.0-rc.2 - #155 (asset gaps)
	static TSharedPtr<FJsonValue> SetSkeletalMeshMaterialSlots(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DiagnoseRegistry(const TSharedPtr<FJsonObject>& Params);

	// v1.0.0-rc.3 - #177, #192, #193
	static TSharedPtr<FJsonValue> GetMeshBounds(const TSharedPtr<FJsonObject>& Params);
	// #431: one-call mesh QA - bounds + materials + LOD/vertex/skeleton.
	static TSharedPtr<FJsonValue> GetMeshInfo(const TSharedPtr<FJsonObject>& Params);
	// #593: list bones (names + rest-pose transforms) from a SkeletalMesh/Skeleton asset.
	static TSharedPtr<FJsonValue> ListSkeletonBones(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetMeshCollision(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MoveFolder(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetMeshNav(const TSharedPtr<FJsonObject>& Params);
	// #212: create empty content browser folders
	static TSharedPtr<FJsonValue> CreateFolder(const TSharedPtr<FJsonObject>& Params);
	// Delete content browser folder(s) - empty by default; force=true also removes contained assets.
	static TSharedPtr<FJsonValue> DeleteFolder(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MigrateAssets(const TSharedPtr<FJsonObject>& Params);
	// #270: read AssetImportData source filenames from imported assets
	static TSharedPtr<FJsonValue> ReadImportSources(const TSharedPtr<FJsonObject>& Params);

	// #279: detect stuck-unloadable assets and recover without editor restart
	static TSharedPtr<FJsonValue> HealthCheck(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ForceReload(const TSharedPtr<FJsonObject>& Params);

	// #686: UserDefinedEnum authoring (create, list values, add/rename/remove enumerator)
	static TSharedPtr<FJsonValue> CreateUserDefinedEnum(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListEnumValues(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> EditUserDefinedEnum(const TSharedPtr<FJsonObject>& Params);

	// #735: UserDefinedStruct authoring (create, list fields, add/rename/retype/remove member).
	// rename_field preserves the member GUID so Blueprint pins and DataTable rows survive.
	static TSharedPtr<FJsonValue> CreateUserDefinedStruct(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListStructFields(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> EditUserDefinedStruct(const TSharedPtr<FJsonObject>& Params);
};
