// pcg(export_level_to_asset) and pcg(update_level_assets): PCG Assemblies, a level exported to a UPCGDataAsset (#1244).
#include "PCGHandlers.h"
#include "HandlerUtils.h"
#include "Runtime/Launch/Resources/Version.h"

#define UE_MCP_HAS_PCG_LEVEL_TO_ASSET (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5))

#if UE_MCP_HAS_PCG_LEVEL_TO_ASSET
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "PCGAssetExporter.h"
#include "PCGAssetExporterUtils.h"
#include "PCGDataAsset.h"
#include "PCGLevelToAsset.h"
#include "UObject/Package.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
#include "Data/PCGBasePointData.h"
#else
#include "Data/PCGPointData.h"
#endif

namespace
{
	int32 MCPPCGPointCount(const UPCGData* Data)
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Data);
		return Points ? Points->GetNumPoints() : -1;
#else
		const UPCGPointData* Points = Cast<UPCGPointData>(Data);
		return Points ? Points->GetPoints().Num() : -1;
#endif
	}

	/** One entry per output pin of the asset, with the points it holds, so a caller can see what the export produced. */
	TArray<TSharedPtr<FJsonValue>> MCPDescribePCGAssetPins(const UPCGDataAsset* Asset)
	{
		TMap<FName, TPair<int32, int32>> ByPin; // pin -> (data count, point count)
		TArray<FName> Order;
		for (const FPCGTaggedData& Tagged : Asset->Data.TaggedData)
		{
			if (!ByPin.Contains(Tagged.Pin))
			{
				ByPin.Add(Tagged.Pin, TPair<int32, int32>(0, 0));
				Order.Add(Tagged.Pin);
			}
			TPair<int32, int32>& Counts = ByPin[Tagged.Pin];
			++Counts.Key;
			Counts.Value += FMath::Max(0, MCPPCGPointCount(Tagged.Data));
		}
		TArray<TSharedPtr<FJsonValue>> Pins;
		for (const FName& Pin : Order)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("pin"), Pin.ToString());
			Entry->SetNumberField(TEXT("dataCount"), ByPin[Pin].Key);
			Entry->SetNumberField(TEXT("pointCount"), ByPin[Pin].Value);
			Pins.Add(MakeShared<FJsonValueObject>(Entry));
		}
		return Pins;
	}
}
#endif

TSharedPtr<FJsonValue> FPCGHandlers::ExportLevelToAsset(const TSharedPtr<FJsonObject>& Params)
{
#if UE_MCP_HAS_PCG_LEVEL_TO_ASSET
	FString LevelPath;
	if (auto Err = RequireString(Params, TEXT("levelPath"), LevelPath)) return Err;
	FString AssetFolder = OptionalString(Params, TEXT("assetPath"));
	FString AssetName = OptionalString(Params, TEXT("assetName"));
	const bool bSave = OptionalBool(Params, TEXT("save"), true);

	const FMCPAssetPathForms Forms = MCPAssetPathForms(LevelPath);
	FString LevelFilename;
	if (Forms.PackagePath.IsEmpty() || !FPackageName::DoesPackageExist(Forms.PackagePath, &LevelFilename)
		|| FPaths::GetExtension(LevelFilename, /*bIncludeDot*/ true) != FPackageName::GetMapPackageExtension())
	{
		return MCPError(FString::Printf(TEXT("'%s' is not a saved level (.umap). Export reads the level from disk, so save a new level first."), *LevelPath));
	}

	UWorld* World = TSoftObjectPtr<UWorld>(FSoftObjectPath(Forms.ObjectPath)).LoadSynchronous();
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("Could not load the level '%s'."), *Forms.ObjectPath));
	}
	// The engine's own guard, repeated because this path does not go through UPCGLevelToAsset::CreateOrUpdatePCGAsset.
	if (ULevelStreaming::FindStreamingLevel(World->PersistentLevel) != nullptr)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is loaded as a sub-level of another world. Unload it from that world first, then export."), *Forms.PackagePath));
	}

	// assetPath names the asset itself when it is an object path or an existing package; otherwise it is the folder (#1296).
	if (!AssetFolder.IsEmpty())
	{
		const FMCPAssetPathForms Target = MCPAssetPathForms(AssetFolder);
		const bool bObjectPath = AssetFolder.Contains(TEXT("."));
		if (bObjectPath || (!Target.PackagePath.IsEmpty() && FPackageName::DoesPackageExist(Target.PackagePath)))
		{
			const FString NamedAsset = FPackageName::GetShortName(Target.PackagePath);
			if (!AssetName.IsEmpty() && AssetName != NamedAsset)
			{
				return MCPError(FString::Printf(
					TEXT("assetPath '%s' names the asset '%s', but assetName is '%s'. Pass the asset's path alone, or a folder with assetName."),
					*AssetFolder, *NamedAsset, *AssetName));
			}
			if (FPackageName::DoesPackageExist(Target.PackagePath))
			{
				IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
				const FAssetData Existing = Registry.GetAssetByObjectPath(FSoftObjectPath(Target.ObjectPath));
				if (Existing.IsValid() && !Existing.IsInstanceOf(UPCGDataAsset::StaticClass()))
				{
					return MCPError(FString::Printf(TEXT("'%s' is a %s, not a PCG data asset; nothing was exported."),
						*Target.ObjectPath, *Existing.AssetClassPath.GetAssetName().ToString()));
				}
			}
			AssetName = NamedAsset;
			AssetFolder = FPackageName::GetLongPackagePath(Target.PackagePath);
		}
	}
	if (AssetName.IsEmpty())
	{
		AssetName = World->GetName() + TEXT("_PCG");
	}
	if (AssetFolder.IsEmpty())
	{
		AssetFolder = FPackageName::GetLongPackagePath(Forms.PackagePath);
	}
	const FString AssetPackage = AssetFolder / AssetName;
	const FString AssetObjectPath = AssetPackage + TEXT(".") + AssetName;
	if (MCPIsProtectedAssetPath(AssetPackage)) return MCPProtectedPathError(AssetPackage);
	const bool bExisted = FPackageName::DoesPackageExist(AssetPackage);

	UPCGLevelToAsset* Exporter = NewObject<UPCGLevelToAsset>(GetTransientPackage());
	Exporter->SetWorld(World);

	FPCGAssetExporterParameters ExportParams;
	ExportParams.bOpenSaveDialog = false;
	ExportParams.AssetName = AssetName;
	ExportParams.AssetPath = AssetFolder;
	// The exporter's own save goes through FEditorFileUtils::PromptForCheckoutAndSave,
	// and since 5.9 CreateAsset returns null whenever that does not report success,
	// which an unattended editor's prompt path does not. The export is kept in
	// memory and saved here, where a refusal comes back with its reason.
	ExportParams.bSaveOnExportEnded = false;

	UPackage* Package = UPCGAssetExporterUtils::CreateAsset(Exporter, ExportParams);
	if (!Package)
	{
		return MCPError(FString::Printf(TEXT("Exporting '%s' to '%s' failed; the editor log names the reason (LogPCG, LogPCGEditor)."), *Forms.PackagePath, *AssetObjectPath));
	}

	UPCGDataAsset* Asset = FindObject<UPCGDataAsset>(Package, *AssetName);
	if (bSave)
	{
		FString SaveError;
		if (!Asset || !SaveAssetPackageChecked(Asset, SaveError))
		{
			return MCPError(FString::Printf(TEXT("'%s' was exported but not saved: %s"), *AssetObjectPath,
				Asset ? *SaveError : TEXT("the exported package holds no PCG data asset of that name")));
		}
	}
	auto Result = MCPSuccess();
	if (bExisted)
	{
		MCPSetUpdated(Result);
	}
	else
	{
		MCPSetCreated(Result);
	}
	Result->SetStringField(TEXT("levelPath"), Forms.PackagePath);
	Result->SetStringField(TEXT("assetPath"), AssetObjectPath);
	Result->SetBoolField(TEXT("saved"), bSave);
	if (Asset)
	{
		Result->SetArrayField(TEXT("pins"), MCPDescribePCGAssetPins(Asset));
	}
	if (bExisted)
	{
		MCPSetNoRollback(Result, TEXT("The existing PCG data asset was overwritten with a fresh export of the level. No call restores its previous contents; source control holds them."));
	}
	else
	{
		MCPSetDeleteAssetRollback(Result, AssetObjectPath);
	}
	return MCPResult(Result);
#else
	return MCPError(TEXT("Exporting a level to a PCG data asset needs UE 5.5 or newer."));
#endif
}

TSharedPtr<FJsonValue> FPCGHandlers::UpdateLevelAssets(const TSharedPtr<FJsonObject>& Params)
{
#if UE_MCP_HAS_PCG_LEVEL_TO_ASSET
	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	const bool bHasPaths = TryGetArrayParam(Params, TEXT("assetPaths"), PathsArr);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	if (!bHasPaths || !PathsArr || PathsArr->Num() == 0)
	{
		return MCPError(TEXT("Missing 'assetPaths': the PCG data assets to re-export from their source levels."));
	}

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FAssetData> Assets;
	TArray<FString> Rejected;
	for (const TSharedPtr<FJsonValue>& Value : *PathsArr)
	{
		FString Path;
		if (!Value.IsValid() || !Value->TryGetString(Path) || Path.IsEmpty()) continue;
		const FAssetData Data = Registry.GetAssetByObjectPath(FSoftObjectPath(MCPAssetPathForms(Path).ObjectPath));
		if (!Data.IsValid() || !Data.IsInstanceOf(UPCGDataAsset::StaticClass()))
		{
			Rejected.Add(Path);
			continue;
		}
		Assets.Add(Data);
	}
	if (Rejected.Num() > 0)
	{
		return MCPError(FString::Printf(TEXT("Nothing was updated: not PCG data assets: %s"), *FString::Join(Rejected, TEXT(", "))));
	}

	FPCGAssetExporterParameters UpdateParams;
	UpdateParams.bOpenSaveDialog = false;
	// Saved below rather than by the exporter, for the reason ExportLevelToAsset gives.
	UpdateParams.bSaveOnExportEnded = false;
	UPCGAssetExporterUtils::UpdateAssets(Assets, UpdateParams);
	if (bSave)
	{
		TArray<FString> SaveErrors;
		for (const FAssetData& Data : Assets)
		{
			FString SaveError;
			UObject* Updated = Data.GetAsset();
			if (!Updated || !SaveAssetPackageChecked(Updated, SaveError))
			{
				SaveErrors.Add(FString::Printf(TEXT("%s: %s"), *Data.GetObjectPathString(), Updated ? *SaveError : TEXT("not loaded")));
			}
		}
		if (SaveErrors.Num() > 0)
		{
			return MCPError(FString::Printf(TEXT("Updated in memory but not saved: %s"), *FString::Join(SaveErrors, TEXT("; "))));
		}
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	TArray<TSharedPtr<FJsonValue>> Updated;
	for (const FAssetData& Data : Assets)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), Data.GetObjectPathString());
		if (const UPCGDataAsset* Asset = Cast<UPCGDataAsset>(Data.GetAsset()))
		{
			Entry->SetArrayField(TEXT("pins"), MCPDescribePCGAssetPins(Asset));
		}
		Updated.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("assets"), Updated);
	Result->SetBoolField(TEXT("saved"), bSave);
	MCPSetNoRollback(Result, TEXT("Each asset was overwritten with a fresh export of its source level. No call restores the previous contents; source control holds them."));
	return MCPResult(Result);
#else
	return MCPError(TEXT("Updating PCG data assets from their levels needs UE 5.5 or newer."));
#endif
}
