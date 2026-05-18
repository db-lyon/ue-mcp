#pragma once

// Idempotent asset-creation helper shared across create_X_asset handlers.
// Kept in its own header so HandlerUtils.h doesn't drag the AssetTools
// module into every translation unit.

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/Factory.h"
#include "UObject/UObjectGlobals.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/** Outcome of an idempotent asset-create attempt.
 *  If EarlyReturn is set, the caller should just `return EarlyReturn` -
 *  it carries either the Existed result (idempotency hit) or an Error.
 *  Otherwise Asset is non-null and the caller proceeds with post-create work
 *  (saving, configuring, building the success JSON). */
template <typename TAsset>
struct FMCPAssetCreate
{
	TAsset* Asset = nullptr;
	TSharedPtr<FJsonValue> EarlyReturn;
};

/** Probe-then-create using AssetTools. Honors onConflict ("skip" returns
 *  the Existed record, "error" returns an MCPError). On success returns
 *  the newly created asset cast to TAsset; the caller is responsible for
 *  SaveAssetPackage() and assembling the result JSON. */
template <typename TAsset>
inline FMCPAssetCreate<TAsset> MCPCreateAssetIdempotent(
	const FString& Name,
	const FString& PackagePath,
	const FString& OnConflict,
	const FString& AssetTypeLabel,
	UFactory* Factory)
{
	FMCPAssetCreate<TAsset> Out;

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, AssetTypeLabel))
	{
		Out.EarlyReturn = Existing;
		return Out;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, PackagePath, TAsset::StaticClass(), Factory);
	if (!NewAsset)
	{
		Out.EarlyReturn = MCPError(FString::Printf(TEXT("Failed to create %s asset"), *AssetTypeLabel));
		return Out;
	}
	Out.Asset = Cast<TAsset>(NewAsset);
	if (!Out.Asset)
	{
		Out.EarlyReturn = MCPError(FString::Printf(TEXT("Created asset is not a %s"), *AssetTypeLabel));
		return Out;
	}
	return Out;
}

// (Rollback emission lives in HandlerUtils.h as MCPSetDeleteAssetRollback;
// kept there because non-create handlers also need it.)
