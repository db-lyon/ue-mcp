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
};
