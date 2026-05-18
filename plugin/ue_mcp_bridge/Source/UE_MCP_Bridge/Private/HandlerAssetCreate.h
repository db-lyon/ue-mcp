#pragma once

// Idempotent asset-creation helper shared across create_X_asset handlers.
// Kept in its own header so HandlerUtils.h doesn't drag the AssetTools
// module into every translation unit.

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/Factory.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
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
	UClass* AssetClass,
	UFactory* Factory)
{
	FMCPAssetCreate<TAsset> Out;

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, AssetTypeLabel))
	{
		Out.EarlyReturn = Existing;
		return Out;
	}

	if (!AssetClass)
	{
		Out.EarlyReturn = MCPError(FString::Printf(TEXT("%s class is unavailable (plugin not loaded?)"), *AssetTypeLabel));
		return Out;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, PackagePath, AssetClass, Factory);
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

/** Overload for the common case where TAsset's class is statically known. */
template <typename TAsset>
inline FMCPAssetCreate<TAsset> MCPCreateAssetIdempotent(
	const FString& Name,
	const FString& PackagePath,
	const FString& OnConflict,
	const FString& AssetTypeLabel,
	UFactory* Factory)
{
	return MCPCreateAssetIdempotent<TAsset>(Name, PackagePath, OnConflict, AssetTypeLabel, TAsset::StaticClass(), Factory);
}

/** Probe-then-create using direct NewObject<> on a fresh UPackage. Used by
 *  asset types whose proper factory either doesn't exist or whose Initialize
 *  / configuration must happen on the constructed object before
 *  AssetTools-style finalization (LevelSequence, AnimSequence, AnimComposite,
 *  PoseSearchDatabase, NiagaraSystem-from-spec).
 *
 *  Calls FAssetRegistryModule::AssetCreated and marks the package dirty.
 *  Caller is responsible for any post-create configuration plus
 *  UEditorAssetLibrary::SaveLoadedAsset / SaveAssetPackage and the success
 *  JSON. */
template <typename TAsset>
inline FMCPAssetCreate<TAsset> MCPCreateAssetIdempotentNewObject(
	const FString& Name,
	const FString& PackagePath,
	const FString& OnConflict,
	const FString& AssetTypeLabel)
{
	FMCPAssetCreate<TAsset> Out;

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, AssetTypeLabel))
	{
		Out.EarlyReturn = Existing;
		return Out;
	}

	const FString PkgName = PackagePath + TEXT("/") + Name;
	UPackage* Package = CreatePackage(*PkgName);
	if (!Package)
	{
		Out.EarlyReturn = MCPError(FString::Printf(TEXT("Failed to create package for %s '%s'"), *AssetTypeLabel, *PkgName));
		return Out;
	}
	TAsset* NewAsset = NewObject<TAsset>(Package, TAsset::StaticClass(), *Name, RF_Public | RF_Standalone);
	if (!NewAsset)
	{
		Out.EarlyReturn = MCPError(FString::Printf(TEXT("Failed to construct %s '%s'"), *AssetTypeLabel, *Name));
		return Out;
	}
	FAssetRegistryModule::AssetCreated(NewAsset);
	NewAsset->MarkPackageDirty();
	Package->SetDirtyFlag(true);
	Out.Asset = NewAsset;
	return Out;
}

// (Rollback emission lives in HandlerUtils.h as MCPSetDeleteAssetRollback;
// kept there because non-create handlers also need it.)
