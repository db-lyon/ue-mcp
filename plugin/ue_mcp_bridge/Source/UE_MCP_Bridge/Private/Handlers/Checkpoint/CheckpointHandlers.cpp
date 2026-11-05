#include "CheckpointHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerCheckpoint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	struct FCheckpointRecorder
	{
		bool bArmed = false;
		FString PendingDir;
		FString ProjectDir;
		/** Lowercased full paths already recorded in this generation. */
		TSet<FString> Seen;
		int32 Count = 0;
		FDelegateHandle PreSaveHandle;
		FDelegateHandle DeletedHandle;
	};

	FCheckpointRecorder& CheckpointRecorder()
	{
		static FCheckpointRecorder Recorder;
		return Recorder;
	}

	FString CheckpointFullPath(const FString& Filename)
	{
		FString Full = FPaths::ConvertRelativePathToFull(Filename);
		FPaths::NormalizeFilename(Full);
		return Full;
	}

	void CheckpointOnPreSave(UPackage* Package, FObjectPreSaveContext Context)
	{
		if (!Package || Context.IsCooking()) return;
		// The package's own file, not the save's target: a save writes to a temp file under Saved and moves it into place.
		const FString& Extension = Package->ContainsMap() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
		FString File;
		if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), File, Extension)) return;
		MCPCheckpointRecordBeforeWrite(File);
	}

	void CheckpointOnPackageDeleted(UPackage* Package)
	{
		if (Package) MCPCheckpointRecordPackage(Package->GetName());
	}

	void CheckpointBind()
	{
		FCheckpointRecorder& R = CheckpointRecorder();
		if (!R.PreSaveHandle.IsValid())
		{
			R.PreSaveHandle = UPackage::PreSavePackageWithContextEvent.AddStatic(&CheckpointOnPreSave);
		}
		if (!R.DeletedHandle.IsValid())
		{
			R.DeletedHandle = FEditorDelegates::OnPackageDeleted.AddStatic(&CheckpointOnPackageDeleted);
		}
	}

	void CheckpointUnbind()
	{
		FCheckpointRecorder& R = CheckpointRecorder();
		if (R.PreSaveHandle.IsValid())
		{
			UPackage::PreSavePackageWithContextEvent.Remove(R.PreSaveHandle);
			R.PreSaveHandle.Reset();
		}
		if (R.DeletedHandle.IsValid())
		{
			FEditorDelegates::OnPackageDeleted.Remove(R.DeletedHandle);
			R.DeletedHandle.Reset();
		}
	}

	/** Package name for a file under a mounted content root, or empty. */
	FString CheckpointPackageOf(const FString& File)
	{
		FString PackageName;
		return FPackageName::TryConvertFilenameToLongPackageName(File, PackageName) ? PackageName : FString();
	}

	TArray<UObject*> CheckpointAssetsIn(UPackage* Package)
	{
		TArray<UObject*> Out;
		if (UObject* Asset = Package->FindAssetInPackage()) Out.Add(Asset);
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> CheckpointStrings(const TArray<FString>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& S : In) Out.Add(MakeShared<FJsonValueString>(S));
		return Out;
	}

	bool CheckpointPlaying()
	{
		return GEditor && (GEditor->PlayWorld != nullptr || GEditor->IsPlaySessionInProgress());
	}
}

void MCPCheckpointRecordBeforeWrite(const FString& Filename)
{
	FCheckpointRecorder& R = CheckpointRecorder();
	if (!R.bArmed || Filename.IsEmpty()) return;
	const FString Full = CheckpointFullPath(Filename);
	if (!FPaths::IsUnderDirectory(Full, R.ProjectDir)) return;
	const FString Key = Full.ToLower();
	if (R.Seen.Contains(Key)) return;

	IFileManager& FileManager = IFileManager::Get();
	const bool bExisted = FileManager.FileExists(*Full);
	FString Copy;
	if (bExisted)
	{
		Copy = FString::Printf(TEXT("%d.bin"), R.Count);
		if (FileManager.Copy(*FPaths::Combine(R.PendingDir, Copy), *Full) != COPY_OK)
		{
			// Recording the file as absent would make a restore delete it, so record nothing.
			UE_LOG(LogTemp, Warning, TEXT("UE-MCP checkpoints: could not copy %s before it was written"), *Full);
			return;
		}
	}
	R.Seen.Add(Key);
	++R.Count;

	TSharedPtr<FJsonObject> Line = MakeShared<FJsonObject>();
	Line->SetStringField(TEXT("path"), Full);
	Line->SetBoolField(TEXT("existed"), bExisted);
	if (bExisted) Line->SetStringField(TEXT("copy"), Copy);
	FString Text;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	FJsonSerializer::Serialize(Line.ToSharedRef(), Writer);
	Text += TEXT("\n");
	FFileHelper::SaveStringToFile(Text, *FPaths::Combine(R.PendingDir, TEXT("manifest.jsonl")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &FileManager, FILEWRITE_Append);
}

void MCPCheckpointRecordPackage(const FString& PackageName)
{
	if (!CheckpointRecorder().bArmed) return;
	FString Filename;
	if (FPackageName::DoesPackageExist(PackageName, &Filename))
	{
		MCPCheckpointRecordBeforeWrite(Filename);
	}
}

void FCheckpointHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Internal to editor(checkpoint) and editor(restore): no spec, no category action of their own.
	Registry.RegisterHandler(TEXT("checkpoint_arm"), &FCheckpointHandlers::Arm);
	Registry.RegisterHandler(TEXT("checkpoint_prepare_restore"), &FCheckpointHandlers::PrepareRestore);
	Registry.RegisterHandlerWithTimeout(TEXT("checkpoint_finish_restore"), &FCheckpointHandlers::FinishRestore, 300.f);
}

void FCheckpointHandlers::Shutdown()
{
	CheckpointUnbind();
	CheckpointRecorder().bArmed = false;
}

TSharedPtr<FJsonValue> FCheckpointHandlers::Arm(const TSharedPtr<FJsonObject>& Params)
{
	const bool bEnabled = OptionalBool(Params, TEXT("enabled"), false);
	FString PendingDir;
	TryGetStringParam(Params, TEXT("pendingDir"), PendingDir);
	MCPReadParamsAhead(Params, { TEXT("projectDir") });

	FCheckpointRecorder& R = CheckpointRecorder();
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (!bEnabled)
	{
		R.bArmed = false;
		R.Seen.Empty();
		CheckpointUnbind();
		Result->SetBoolField(TEXT("armed"), false);
		return MCPResult(Result);
	}
	if (PendingDir.IsEmpty()) return MCPError(TEXT("pendingDir is required when enabled is true"));

	const FString Previous = R.bArmed ? R.PendingDir : FString();
	R.PendingDir = CheckpointFullPath(PendingDir);
	R.ProjectDir = CheckpointFullPath(FPaths::ProjectDir());
	R.Seen.Empty();
	R.Count = 0;
	IFileManager::Get().MakeDirectory(*R.PendingDir, /*Tree*/ true);
	R.bArmed = true;
	CheckpointBind();

	TArray<UPackage*> Dirty;
	FEditorFileUtils::GetDirtyWorldPackages(Dirty);
	FEditorFileUtils::GetDirtyContentPackages(Dirty);
	TArray<FString> DirtyNames;
	for (const UPackage* Package : Dirty)
	{
		if (Package) DirtyNames.AddUnique(Package->GetName());
	}
	DirtyNames.Sort();

	Result->SetBoolField(TEXT("armed"), true);
	Result->SetStringField(TEXT("pendingDir"), R.PendingDir);
	if (!Previous.IsEmpty()) Result->SetStringField(TEXT("previousDir"), Previous);
	Result->SetArrayField(TEXT("dirtyPackages"), CheckpointStrings(DirtyNames));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FCheckpointHandlers::PrepareRestore(const TSharedPtr<FJsonObject>& Params)
{
	const bool bCheckOnly = OptionalBool(Params, TEXT("checkOnly"), false);
	const TSharedPtr<FJsonValue> FilesValue = TryGetParam(Params, TEXT("files"));
	if (CheckpointPlaying())
	{
		return MCPErrorWithCode(TEXT("pie_running"), TEXT("Restore is refused while Play In Editor runs: stop it first."));
	}
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (bCheckOnly) return MCPResult(Result);

	FlushAsyncLoading();
	UAssetEditorSubsystem* Editors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	TArray<FString> Closed;
	TArray<FString> Detached;
	if (FilesValue.IsValid() && FilesValue->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& V : FilesValue->AsArray())
		{
			const FString PackageName = CheckpointPackageOf(V->AsString());
			if (PackageName.IsEmpty()) continue;
			UPackage* Package = FindPackage(nullptr, *PackageName);
			if (!Package) continue;
			for (UObject* Asset : CheckpointAssetsIn(Package))
			{
				if (Editors && Editors->FindEditorsForAsset(Asset).Num() > 0)
				{
					Closed.Add(Asset->GetPathName());
					Editors->CloseAllEditorsForAsset(Asset);
				}
			}
			// Loads what the linker still reads lazily and lets go of the file, so it can be rewritten.
			ResetLoaders(Package);
			Detached.Add(PackageName);
		}
	}
	Result->SetArrayField(TEXT("closedEditors"), CheckpointStrings(Closed));
	Result->SetArrayField(TEXT("detached"), CheckpointStrings(Detached));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FCheckpointHandlers::FinishRestore(const TSharedPtr<FJsonObject>& Params)
{
	const TSharedPtr<FJsonValue> FilesValue = TryGetParam(Params, TEXT("files"));
	const TSharedPtr<FJsonValue> ReopenValue = TryGetParam(Params, TEXT("reopen"));

	TArray<UPackage*> ToReload;
	TArray<UPackage*> ToUnload;
	TArray<FString> Reloaded, Unloaded, ConfigsReloaded, Reopened, Scanned;
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	if (FilesValue.IsValid() && FilesValue->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& V : FilesValue->AsArray())
		{
			const TSharedPtr<FJsonObject> Entry = V.IsValid() && V->Type == EJson::Object ? V->AsObject() : nullptr;
			if (!Entry.IsValid()) continue;
			FString File, Kind;
			bool bExists = false;
			Entry->TryGetStringField(TEXT("file"), File);
			Entry->TryGetStringField(TEXT("kind"), Kind);
			Entry->TryGetBoolField(TEXT("exists"), bExists);
			File = CheckpointFullPath(File);

			if (Kind == TEXT("package"))
			{
				const FString PackageName = CheckpointPackageOf(File);
				if (PackageName.IsEmpty()) continue;
				Scanned.Add(File);
				UPackage* Package = FindPackage(nullptr, *PackageName);
				if (!Package) continue;
				if (bExists)
				{
					ToReload.Add(Package);
					Reloaded.Add(PackageName);
				}
				else
				{
					for (UObject* Asset : CheckpointAssetsIn(Package)) FAssetRegistryModule::AssetDeleted(Asset);
					ToUnload.Add(Package);
					Unloaded.Add(PackageName);
				}
			}
			else if (Kind == TEXT("config"))
			{
				// Config/DefaultGame.ini is the Game branch of the hierarchy.
				const FString Base = FPaths::GetBaseFilename(File);
				if (!Base.StartsWith(TEXT("Default"))) continue;
				const FString Branch = Base.RightChop(7);
				FString FinalIni;
				if (!Branch.IsEmpty() && FConfigCacheIni::LoadGlobalIniFile(FinalIni, *Branch, nullptr, /*bForceReload*/ true))
				{
					ConfigsReloaded.Add(Branch);
				}
			}
		}
	}

	FText Error;
	if (ToReload.Num() > 0)
	{
		UPackageTools::ReloadPackages(ToReload, Error, UPackageTools::EReloadPackagesInteractionMode::AssumePositive);
	}
	if (ToUnload.Num() > 0)
	{
		UPackageTools::UnloadPackages(ToUnload, Error, /*bUnloadDirtyPackages*/ true);
	}
	if (Scanned.Num() > 0)
	{
		AssetRegistry.ScanModifiedAssetFiles(Scanned);
	}

	UAssetEditorSubsystem* Editors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (Editors && ReopenValue.IsValid() && ReopenValue->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& V : ReopenValue->AsArray())
		{
			const FString AssetPath = V->AsString();
			if (UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath))
			{
				if (Editors->OpenEditorForAsset(Asset)) Reopened.Add(AssetPath);
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetArrayField(TEXT("reloaded"), CheckpointStrings(Reloaded));
	Result->SetArrayField(TEXT("unloaded"), CheckpointStrings(Unloaded));
	Result->SetArrayField(TEXT("configsReloaded"), CheckpointStrings(ConfigsReloaded));
	Result->SetArrayField(TEXT("reopened"), CheckpointStrings(Reopened));
	if (!Error.IsEmpty()) Result->SetStringField(TEXT("warning"), Error.ToString());
	return MCPResult(Result);
}
