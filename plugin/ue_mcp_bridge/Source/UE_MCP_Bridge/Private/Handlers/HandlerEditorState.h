#pragma once

// Editor-state helpers shared by more than one handler translation unit.
//
// The module is compiled as a unity build, so a file-local helper copied into a
// second .cpp becomes a redefinition the moment UBT groups the two files into
// one blob (error C2084), and the grouping is not stable across machines.
// Anything two handlers both need lives here, in a named namespace, as an
// inline function. It is a Private header rather than Public/HandlerUtils.h
// because it pulls in the editor file utilities, and HandlerUtils.h is also
// included by UE_MCP_BridgeStatus, which does not link UnrealEd.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "HandlerUtils.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace MCPEditorState
{
	/**
	 * Long package names of every dirty content or map package in the editor.
	 *
	 * Two handlers need this for the same reason: an operation that temporarily
	 * opens a different map has to refuse when the editor already has unsaved
	 * work, because loading another level either discards it or raises a modal
	 * save prompt the bridge cannot answer.
	 *
	 * `/Script/` packages are always dirty-ish bookkeeping and are excluded.
	 */
	inline void CollectDirtyEditorPackageNames(TArray<FString>& OutPackageNames)
	{
		OutPackageNames.Reset();
		TArray<UPackage*> DirtyPackages;
		FEditorFileUtils::GetDirtyContentPackages(DirtyPackages);
		FEditorFileUtils::GetDirtyWorldPackages(DirtyPackages);
		for (UPackage* Package : DirtyPackages)
		{
			if (!Package || !Package->IsDirty())
			{
				continue;
			}

			const FString PackageName = Package->GetName();
			if (PackageName.StartsWith(TEXT("/Script/")))
			{
				continue;
			}

			OutPackageNames.AddUnique(PackageName);
		}
		OutPackageNames.Sort();
	}

	/** Long package name of the map currently open in the editor, or empty. */
	inline FString CurrentEditorLevelPackageName()
	{
		if (!GEditor)
		{
			return FString();
		}
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World || !World->GetOutermost())
		{
			return FString();
		}
		return World->GetOutermost()->GetName();
	}

	/** Is this long package name an existing .umap on disk? */
	inline bool IsExistingMapPackage(const FString& LevelPath)
	{
		if (LevelPath.IsEmpty() || !FPackageName::IsValidLongPackageName(LevelPath))
		{
			return false;
		}
		FString Filename;
		return FPackageName::DoesPackageExist(LevelPath, &Filename) &&
			Filename.EndsWith(FPackageName::GetMapPackageExtension(), ESearchCase::IgnoreCase);
	}

	/**
	 * The refusal an action that opens another map returns while packages are
	 * dirty, or nullptr when nothing is. OutDirty receives the dirty list either
	 * way, so a clean call can reuse it as its dirtied-packages baseline.
	 */
	inline TSharedPtr<FJsonValue> RefuseIfDirty(const FString& Message, TArray<FString>& OutDirty)
	{
		CollectDirtyEditorPackageNames(OutDirty);
		if (OutDirty.IsEmpty())
		{
			return nullptr;
		}
		TSharedPtr<FJsonObject> Result = MCPErrorObject(Message);
		Result->SetArrayField(TEXT("dirtyPackages"), MCPStringListToJson(OutDirty));
		return MCPResult(Result);
	}

	/** The level(load_level) handler, passed in by the level handlers that own it. */
	using FLoadLevelHandler = TSharedPtr<FJsonValue> (*)(const TSharedPtr<FJsonObject>&);

	/** Open LevelPath through level(load_level). On failure OutError carries the
	 *  handler's own error, or a generic sentence when it gave none. */
	inline bool LoadLevelViaHandler(FLoadLevelHandler LoadLevel, const FString& LevelPath, FString& OutError)
	{
		TSharedPtr<FJsonObject> LoadParams = MakeShared<FJsonObject>();
		LoadParams->SetStringField(TEXT("levelPath"), LevelPath);
		const TSharedPtr<FJsonValue> LoadResult = LoadLevel(LoadParams);
		if (!LoadResult.IsValid() || LoadResult->Type != EJson::Object)
		{
			OutError = FString::Printf(TEXT("Level load returned an invalid result: %s"), *LevelPath);
			return false;
		}
		const TSharedPtr<FJsonObject> LoadObject = LoadResult->AsObject();
		bool bSuccess = false;
		LoadObject->TryGetBoolField(TEXT("success"), bSuccess);
		if (bSuccess)
		{
			return true;
		}
		if (!LoadObject->TryGetStringField(TEXT("error"), OutError))
		{
			OutError = FString::Printf(TEXT("Failed to load level: %s"), *LevelPath);
		}
		return false;
	}

	/**
	 * Puts the map that was open back after an action opened another one.
	 * Restore() reports the outcome; with bRestoreOnExit the destructor restores
	 * on any exit path that did not already call it.
	 */
	class FScopedLevelRestore
	{
	public:
		FScopedLevelRestore(FLoadLevelHandler InLoadLevel, bool bInRestoreOnExit)
			: LoadLevel(InLoadLevel), bRestoreOnExit(bInRestoreOnExit) {}
		~FScopedLevelRestore()
		{
			if (bRestoreOnExit && !bRestored && !LevelPath.IsEmpty())
			{
				FString Ignored;
				Restore(Ignored);
			}
		}
		FScopedLevelRestore(const FScopedLevelRestore&) = delete;
		FScopedLevelRestore& operator=(const FScopedLevelRestore&) = delete;

		/** The map to put back. Empty means there is nothing to restore. */
		void SetLevelPath(const FString& InLevelPath) { LevelPath = InLevelPath; }
		const FString& GetLevelPath() const { return LevelPath; }

		/** True when the original map is open again (or already was). */
		bool Restore(FString& OutError)
		{
			if (LevelPath.IsEmpty())
			{
				return false;
			}
			bRestored = CurrentEditorLevelPackageName() == LevelPath
				|| LoadLevelViaHandler(LoadLevel, LevelPath, OutError);
			return bRestored;
		}

	private:
		FLoadLevelHandler LoadLevel = nullptr;
		FString LevelPath;
		bool bRestoreOnExit = false;
		bool bRestored = false;
	};
}
