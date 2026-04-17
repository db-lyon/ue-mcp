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
	static TSharedPtr<FJsonValue> SearchAssets(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadAssetProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DuplicateAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RenameAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MoveAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteAssetBatch(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BulkRename(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateDataAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SaveAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListTextures(const TSharedPtr<FJsonObject>& Params);

	// DataTable handlers
	static TSharedPtr<FJsonValue> ImportDataTableJson(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ExportDataTableJson(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateDataTable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadDataTable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReimportDataTable(const TSharedPtr<FJsonObject>& Params);

	// FBX import handlers
	static TSharedPtr<FJsonValue> ImportStaticMesh(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportSkeletalMesh(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportAnimation(const TSharedPtr<FJsonObject>& Params);

	// Mesh material handlers
	static TSharedPtr<FJsonValue> SetMeshMaterial(const TSharedPtr<FJsonObject>& Params);

	// Mesh pivot handlers
	static TSharedPtr<FJsonValue> RecenterPivot(const TSharedPtr<FJsonObject>& Params);

	// Texture handlers
	static TSharedPtr<FJsonValue> ListTextureProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetTextureProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportTexture(const TSharedPtr<FJsonObject>& Params);

	// Export
	static TSharedPtr<FJsonValue> ExportAsset(const TSharedPtr<FJsonObject>& Params);

	// Reimport
	static TSharedPtr<FJsonValue> ReimportAsset(const TSharedPtr<FJsonObject>& Params);

	// Socket handlers
	static TSharedPtr<FJsonValue> AddSocket(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveSocket(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListSockets(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReloadPackage(const TSharedPtr<FJsonObject>& Params);

	// v0.7.8 — FTS5-backed asset search (stubs)
	static TSharedPtr<FJsonValue> SearchAssetsFTS(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReindexAssetsFTS(const TSharedPtr<FJsonObject>& Params);

	// v0.7.19 issue #150 — AssetRegistry referencers for a set of packages
	static TSharedPtr<FJsonValue> GetReferencers(const TSharedPtr<FJsonObject>& Params);
};
