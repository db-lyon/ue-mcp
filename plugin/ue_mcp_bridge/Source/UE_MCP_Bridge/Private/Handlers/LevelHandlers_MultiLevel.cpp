#include "LevelHandlers.h"

#include "HandlerUtils.h"

#include "Editor.h"
#include "Engine/Brush.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "LevelEditorSubsystem.h"
#include "Misc/PackageName.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace
{
	constexpr int32 MaxLevelRequests = 16;
	constexpr int32 MaxLabelsPerLevel = 256;

	struct FExactLabelLevelRequest
	{
		FString LevelPath;
		TArray<FString> ActorLabels;
		FString ExpectedClassPath;
		int32 Deleted = 0;
		bool bSaved = false;

		TArray<FString> MatchedLabels;
		TArray<FString> MissingLabels;
		TArray<FString> DuplicateLabels;
		TArray<FString> WrongClassLabels;
		TArray<FString> NonPersistentLabels;
		TArray<FString> ProtectedLabels;
	};

	TArray<TSharedPtr<FJsonValue>> StringListToJson(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		Result.Reserve(Values.Num());
		for (const FString& Value : Values)
		{
			Result.Add(MakeShared<FJsonValueString>(Value));
		}
		return Result;
	}

	void CollectDirtyEditorPackageNames(TArray<FString>& OutPackageNames)
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

	UClass* ResolveExpectedActorClass(const FString& ClassPath, FString& OutError)
	{
		if (ClassPath.IsEmpty())
		{
			return nullptr;
		}

		UClass* ActorClass = LoadObject<UClass>(nullptr, *ClassPath);
		if (!ActorClass)
		{
			OutError = FString::Printf(TEXT("Expected actor class was not found: %s"), *ClassPath);
			return nullptr;
		}
		if (!ActorClass->IsChildOf(AActor::StaticClass()))
		{
			OutError = FString::Printf(TEXT("Expected class is not an actor class: %s"), *ClassPath);
			return nullptr;
		}
		return ActorClass;
	}

	bool IsProtectedLevelActor(AActor* Actor, UWorld* World)
	{
		return Actor &&
			World &&
			(Actor == World->GetWorldSettings() ||
			 Actor == World->GetDefaultBrush() ||
			 Actor->IsA<ALevelScriptActor>());
	}

	void ResetInspection(FExactLabelLevelRequest& Request)
	{
		Request.MatchedLabels.Reset();
		Request.MissingLabels.Reset();
		Request.DuplicateLabels.Reset();
		Request.WrongClassLabels.Reset();
		Request.NonPersistentLabels.Reset();
		Request.ProtectedLabels.Reset();
	}

	bool InspectLoadedLevel(
		UWorld* World,
		FExactLabelLevelRequest& Request,
		FString& OutError)
	{
		ResetInspection(Request);

		if (!World || !World->PersistentLevel)
		{
			OutError = TEXT("The loaded editor world has no persistent level");
			return false;
		}
		if (World->IsPartitionedWorld())
		{
			OutError = FString::Printf(
				TEXT("World Partition levels are not supported by this handler: %s"),
				*Request.LevelPath);
			return false;
		}

		UClass* ExpectedClass = ResolveExpectedActorClass(Request.ExpectedClassPath, OutError);
		if (!Request.ExpectedClassPath.IsEmpty() && !ExpectedClass)
		{
			return false;
		}

		TSet<FString> RequestedLabels;
		RequestedLabels.Reserve(Request.ActorLabels.Num());
		for (const FString& Label : Request.ActorLabels)
		{
			RequestedLabels.Add(Label);
		}

		TMap<FString, TArray<AActor*>> ActorsByLabel;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			const FString Label = Actor->GetActorLabel();
			if (RequestedLabels.Contains(Label))
			{
				ActorsByLabel.FindOrAdd(Label).Add(Actor);
			}
		}

		for (const FString& Label : Request.ActorLabels)
		{
			const TArray<AActor*>* Matches = ActorsByLabel.Find(Label);
			if (!Matches || Matches->IsEmpty())
			{
				Request.MissingLabels.Add(Label);
				continue;
			}
			if (Matches->Num() != 1)
			{
				Request.DuplicateLabels.Add(Label);
				continue;
			}

			AActor* Actor = (*Matches)[0];
			if (Actor->GetLevel() != World->PersistentLevel)
			{
				Request.NonPersistentLabels.Add(Label);
				continue;
			}
			if (IsProtectedLevelActor(Actor, World))
			{
				Request.ProtectedLabels.Add(Label);
				continue;
			}
			if (ExpectedClass && !Actor->IsA(ExpectedClass))
			{
				Request.WrongClassLabels.Add(Label);
				continue;
			}

			Request.MatchedLabels.Add(Label);
		}

		return true;
	}

	TSharedPtr<FJsonObject> MakeLevelResult(const FExactLabelLevelRequest& Request)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("levelPath"), Request.LevelPath);
		Result->SetNumberField(TEXT("requested"), Request.ActorLabels.Num());
		Result->SetNumberField(TEXT("matched"), Request.MatchedLabels.Num());
		Result->SetNumberField(TEXT("deleted"), Request.Deleted);
		Result->SetBoolField(TEXT("saved"), Request.bSaved);
		Result->SetArrayField(TEXT("matchedLabels"), StringListToJson(Request.MatchedLabels));
		Result->SetArrayField(TEXT("missingLabels"), StringListToJson(Request.MissingLabels));
		Result->SetArrayField(TEXT("duplicateLabels"), StringListToJson(Request.DuplicateLabels));
		Result->SetArrayField(TEXT("wrongClassLabels"), StringListToJson(Request.WrongClassLabels));
		Result->SetArrayField(TEXT("nonPersistentLabels"), StringListToJson(Request.NonPersistentLabels));
		Result->SetArrayField(TEXT("protectedLabels"), StringListToJson(Request.ProtectedLabels));
		if (!Request.ExpectedClassPath.IsEmpty())
		{
			Result->SetStringField(TEXT("expectedClassPath"), Request.ExpectedClassPath);
		}
		return Result;
	}
}

TSharedPtr<FJsonValue> FLevelHandlers::DeleteExactLabeledActorsInLevels(
	const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MCPError(TEXT("GEditor is not available"));
	}
	if (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor)
	{
		return MCPError(TEXT("Stop PIE or SIE before deleting actors across levels"));
	}

	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), true);
	const bool bRestoreOriginalLevel = OptionalBool(Params, TEXT("restoreOriginalLevel"), true);
	FString OnMissing = OptionalString(Params, TEXT("onMissing"), TEXT("error")).ToLower();
	if (OnMissing != TEXT("error") && OnMissing != TEXT("ignore"))
	{
		return MCPError(TEXT("'onMissing' must be either 'error' or 'ignore'"));
	}

	const TArray<TSharedPtr<FJsonValue>>* LevelValues = nullptr;
	if (!Params->TryGetArrayField(TEXT("levels"), LevelValues) ||
		!LevelValues ||
		LevelValues->IsEmpty())
	{
		return MCPError(TEXT("Missing required non-empty 'levels' array"));
	}
	if (LevelValues->Num() > MaxLevelRequests)
	{
		return MCPError(FString::Printf(
			TEXT("'levels' exceeds the maximum of %d entries"),
			MaxLevelRequests));
	}

	TArray<FExactLabelLevelRequest> Requests;
	Requests.Reserve(LevelValues->Num());
	TSet<FString> SeenLevelPaths;

	for (int32 LevelIndex = 0; LevelIndex < LevelValues->Num(); ++LevelIndex)
	{
		const TSharedPtr<FJsonValue>& LevelValue = (*LevelValues)[LevelIndex];
		if (!LevelValue.IsValid() || LevelValue->Type != EJson::Object)
		{
			return MCPError(FString::Printf(
				TEXT("'levels[%d]' must be an object"),
				LevelIndex));
		}

		const TSharedPtr<FJsonObject> LevelObject = LevelValue->AsObject();
		FExactLabelLevelRequest Request;
		if (!LevelObject->TryGetStringField(TEXT("levelPath"), Request.LevelPath) ||
			Request.LevelPath.IsEmpty())
		{
			return MCPError(FString::Printf(
				TEXT("Missing required string 'levels[%d].levelPath'"),
				LevelIndex));
		}
		if (!FPackageName::IsValidLongPackageName(Request.LevelPath))
		{
			return MCPError(FString::Printf(
				TEXT("Invalid long package name at 'levels[%d].levelPath': %s"),
				LevelIndex,
				*Request.LevelPath));
		}
		if (Request.LevelPath.StartsWith(TEXT("/Engine/")) ||
			Request.LevelPath.StartsWith(TEXT("/Script/")) ||
			Request.LevelPath.StartsWith(TEXT("/Temp/")))
		{
			return MCPError(FString::Printf(
				TEXT("Level path is outside writable project or plugin content: %s"),
				*Request.LevelPath));
		}
		if (SeenLevelPaths.Contains(Request.LevelPath))
		{
			return MCPError(FString::Printf(
				TEXT("Duplicate level path in request: %s"),
				*Request.LevelPath));
		}
		SeenLevelPaths.Add(Request.LevelPath);

		FString LevelFilename;
		if (!FPackageName::DoesPackageExist(Request.LevelPath, &LevelFilename) ||
			!LevelFilename.EndsWith(FPackageName::GetMapPackageExtension(), ESearchCase::IgnoreCase))
		{
			return MCPError(FString::Printf(
				TEXT("Level package was not found as a .umap: %s"),
				*Request.LevelPath));
		}
		if (!bDryRun && IFileManager::Get().IsReadOnly(*LevelFilename))
		{
			return MCPError(FString::Printf(
				TEXT("Level package is read-only: %s"),
				*Request.LevelPath));
		}

		const TArray<TSharedPtr<FJsonValue>>* LabelValues = nullptr;
		if (!LevelObject->TryGetArrayField(TEXT("actorLabels"), LabelValues) ||
			!LabelValues ||
			LabelValues->IsEmpty())
		{
			return MCPError(FString::Printf(
				TEXT("Missing required non-empty 'levels[%d].actorLabels' array"),
				LevelIndex));
		}
		if (LabelValues->Num() > MaxLabelsPerLevel)
		{
			return MCPError(FString::Printf(
				TEXT("'levels[%d].actorLabels' exceeds the maximum of %d entries"),
				LevelIndex,
				MaxLabelsPerLevel));
		}

		TSet<FString> SeenLabels;
		for (int32 LabelIndex = 0; LabelIndex < LabelValues->Num(); ++LabelIndex)
		{
			const TSharedPtr<FJsonValue>& LabelValue = (*LabelValues)[LabelIndex];
			FString Label;
			if (!LabelValue.IsValid() ||
				!LabelValue->TryGetString(Label) ||
				Label.TrimStartAndEnd().IsEmpty())
			{
				return MCPError(FString::Printf(
					TEXT("'levels[%d].actorLabels[%d]' must be a non-empty string"),
					LevelIndex,
					LabelIndex));
			}
			if (SeenLabels.Contains(Label))
			{
				return MCPError(FString::Printf(
					TEXT("Duplicate actor label in level request '%s': %s"),
					*Request.LevelPath,
					*Label));
			}
			SeenLabels.Add(Label);
			Request.ActorLabels.Add(Label);
		}

		if (LevelObject->HasField(TEXT("expectedClassPath")) &&
			(!LevelObject->TryGetStringField(TEXT("expectedClassPath"), Request.ExpectedClassPath) ||
			 Request.ExpectedClassPath.IsEmpty()))
		{
			return MCPError(FString::Printf(
				TEXT("'levels[%d].expectedClassPath' must be a non-empty string when provided"),
				LevelIndex));
		}
		if (!Request.ExpectedClassPath.IsEmpty())
		{
			FString ClassError;
			if (!ResolveExpectedActorClass(Request.ExpectedClassPath, ClassError))
			{
				return MCPError(ClassError);
			}
		}

		Requests.Add(MoveTemp(Request));
	}

	UWorld* OriginalWorld = GEditor->GetEditorWorldContext().World();
	if (!OriginalWorld)
	{
		return MCPError(TEXT("The editor world is not available"));
	}
	const FString OriginalLevelPath = OriginalWorld->GetOutermost()->GetName();
	if (bRestoreOriginalLevel)
	{
		FString OriginalFilename;
		if (!FPackageName::IsValidLongPackageName(OriginalLevelPath) ||
			!FPackageName::DoesPackageExist(OriginalLevelPath, &OriginalFilename))
		{
			return MCPError(FString::Printf(
				TEXT("The original level cannot be restored from package path '%s'"),
				*OriginalLevelPath));
		}
	}

	TArray<FString> InitialDirtyPackages;
	CollectDirtyEditorPackageNames(InitialDirtyPackages);
	if (!InitialDirtyPackages.IsEmpty())
	{
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(
			TEXT("error"),
			TEXT("Refusing to load levels while content or map packages are dirty"));
		Result->SetArrayField(TEXT("dirtyPackages"), StringListToJson(InitialDirtyPackages));
		return MCPResult(Result);
	}

	auto LoadEditorLevel = [](const FString& LevelPath, FString& OutError) -> bool
	{
		TSharedPtr<FJsonObject> LoadParams = MakeShared<FJsonObject>();
		LoadParams->SetStringField(TEXT("levelPath"), LevelPath);
		const TSharedPtr<FJsonValue> LoadResult = FLevelHandlers::LoadLevel(LoadParams);
		if (!LoadResult.IsValid() || LoadResult->Type != EJson::Object)
		{
			OutError = FString::Printf(TEXT("Level load returned an invalid result: %s"), *LevelPath);
			return false;
		}

		const TSharedPtr<FJsonObject> LoadObject = LoadResult->AsObject();
		bool bSuccess = false;
		LoadObject->TryGetBoolField(TEXT("success"), bSuccess);
		if (!bSuccess)
		{
			if (!LoadObject->TryGetStringField(TEXT("error"), OutError))
			{
				OutError = FString::Printf(TEXT("Failed to load level: %s"), *LevelPath);
			}
			return false;
		}
		return true;
	};

	auto BuildResult = [&Requests, &OriginalLevelPath, bDryRun, bRestoreOriginalLevel](
		bool bSuccess,
		const FString& Error,
		bool bPartial,
		bool bRestoredOriginalLevel,
		const TArray<FString>& DirtyPackages) -> TSharedPtr<FJsonValue>
	{
		TArray<TSharedPtr<FJsonValue>> LevelResults;
		LevelResults.Reserve(Requests.Num());

		int32 TotalRequested = 0;
		int32 TotalMatched = 0;
		int32 TotalDeleted = 0;
		bool bAnyAppliedChanges = false;
		for (const FExactLabelLevelRequest& Request : Requests)
		{
			LevelResults.Add(MakeShared<FJsonValueObject>(MakeLevelResult(Request)));
			TotalRequested += Request.ActorLabels.Num();
			TotalMatched += Request.MatchedLabels.Num();
			TotalDeleted += Request.Deleted;
			bAnyAppliedChanges |= Request.Deleted > 0 || Request.bSaved;
		}

		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("success"), bSuccess);
		if (!Error.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), Error);
		}
		Result->SetBoolField(TEXT("dryRun"), bDryRun);
		Result->SetBoolField(TEXT("partial"), bPartial && bAnyAppliedChanges);
		Result->SetNumberField(TEXT("requestedLevels"), Requests.Num());
		Result->SetNumberField(TEXT("requestedActors"), TotalRequested);
		Result->SetNumberField(TEXT("matchedActors"), TotalMatched);
		Result->SetNumberField(TEXT("deletedActors"), TotalDeleted);
		Result->SetStringField(TEXT("originalLevelPath"), OriginalLevelPath);
		Result->SetBoolField(TEXT("restoreOriginalLevelRequested"), bRestoreOriginalLevel);
		Result->SetBoolField(TEXT("restoredOriginalLevel"), bRestoredOriginalLevel);
		Result->SetArrayField(TEXT("levels"), LevelResults);
		if (!DirtyPackages.IsEmpty())
		{
			Result->SetArrayField(TEXT("dirtyPackages"), StringListToJson(DirtyPackages));
		}
		return MCPResult(Result);
	};

	auto RestoreOriginalLevel = [&LoadEditorLevel, &OriginalLevelPath, bRestoreOriginalLevel](
		FString& OutError) -> bool
	{
		if (!bRestoreOriginalLevel)
		{
			return false;
		}

		UWorld* CurrentWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (CurrentWorld && CurrentWorld->GetOutermost()->GetName() == OriginalLevelPath)
		{
			return true;
		}
		return LoadEditorLevel(OriginalLevelPath, OutError);
	};

	// First pass validates every level and exact match before any destructive
	// operation begins. This prevents a missing label in a later level from
	// partially committing earlier levels.
	for (FExactLabelLevelRequest& Request : Requests)
	{
		FString LoadError;
		if (!LoadEditorLevel(Request.LevelPath, LoadError))
		{
			FString RestoreError;
			const bool bRestored = RestoreOriginalLevel(RestoreError);
			if (!RestoreError.IsEmpty())
			{
				LoadError += FString::Printf(TEXT("; failed to restore original level: %s"), *RestoreError);
			}
			return BuildResult(false, LoadError, false, bRestored, {});
		}

		UWorld* World = GEditor->GetEditorWorldContext().World();
		FString InspectError;
		if (!InspectLoadedLevel(World, Request, InspectError))
		{
			FString RestoreError;
			const bool bRestored = RestoreOriginalLevel(RestoreError);
			if (!RestoreError.IsEmpty())
			{
				InspectError += FString::Printf(TEXT("; failed to restore original level: %s"), *RestoreError);
			}
			return BuildResult(false, InspectError, false, bRestored, {});
		}

		TArray<FString> DirtyAfterLoad;
		CollectDirtyEditorPackageNames(DirtyAfterLoad);
		if (!DirtyAfterLoad.IsEmpty())
		{
			return BuildResult(
				false,
				TEXT("Loading or inspecting a level dirtied packages; refusing to load another level"),
				false,
				false,
				DirtyAfterLoad);
		}

		const bool bHasUnsafeMatch =
			!Request.DuplicateLabels.IsEmpty() ||
			!Request.WrongClassLabels.IsEmpty() ||
			!Request.NonPersistentLabels.IsEmpty() ||
			!Request.ProtectedLabels.IsEmpty();
		const bool bHasMissingError =
			OnMissing == TEXT("error") &&
			!Request.MissingLabels.IsEmpty();
		if (bHasUnsafeMatch || bHasMissingError)
		{
			FString RestoreError;
			const bool bRestored = RestoreOriginalLevel(RestoreError);
			FString Error = FString::Printf(
				TEXT("Exact-label preflight failed for level: %s"),
				*Request.LevelPath);
			if (!RestoreError.IsEmpty())
			{
				Error += FString::Printf(TEXT("; failed to restore original level: %s"), *RestoreError);
			}
			return BuildResult(false, Error, false, bRestored, {});
		}
	}

	if (bDryRun)
	{
		FString RestoreError;
		const bool bRestored = RestoreOriginalLevel(RestoreError);
		if (!RestoreError.IsEmpty())
		{
			return BuildResult(false, RestoreError, false, false, {});
		}
		return BuildResult(true, FString(), false, bRestored, {});
	}

	ULevelEditorSubsystem* LevelEditorSubsystem =
		GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	UEditorActorSubsystem* EditorActorSubsystem =
		GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (!LevelEditorSubsystem || !EditorActorSubsystem)
	{
		return BuildResult(
			false,
			TEXT("Required editor subsystems are not available"),
			false,
			false,
			{});
	}

	// Second pass commits only after all requested levels passed preflight.
	for (FExactLabelLevelRequest& Request : Requests)
	{
		FString LoadError;
		if (!LoadEditorLevel(Request.LevelPath, LoadError))
		{
			return BuildResult(false, LoadError, true, false, {});
		}

		UWorld* World = GEditor->GetEditorWorldContext().World();
		FString InspectError;
		if (!InspectLoadedLevel(World, Request, InspectError))
		{
			return BuildResult(false, InspectError, true, false, {});
		}

		TArray<FString> DirtyAfterLoad;
		CollectDirtyEditorPackageNames(DirtyAfterLoad);
		if (!DirtyAfterLoad.IsEmpty())
		{
			return BuildResult(
				false,
				TEXT("Loading or inspecting a level dirtied packages; refusing to delete actors"),
				true,
				false,
				DirtyAfterLoad);
		}

		const bool bHasUnsafeMatch =
			!Request.DuplicateLabels.IsEmpty() ||
			!Request.WrongClassLabels.IsEmpty() ||
			!Request.NonPersistentLabels.IsEmpty() ||
			!Request.ProtectedLabels.IsEmpty();
		const bool bHasMissingError =
			OnMissing == TEXT("error") &&
			!Request.MissingLabels.IsEmpty();
		if (bHasUnsafeMatch || bHasMissingError)
		{
			return BuildResult(
				false,
				FString::Printf(
					TEXT("Exact-label state changed after preflight for level: %s"),
					*Request.LevelPath),
				true,
				false,
				{});
		}

		// An idempotent onMissing=ignore replay with no remaining matches
		// changes nothing and therefore must not dirty or save the level.
		if (Request.MatchedLabels.IsEmpty())
		{
			continue;
		}

		TMap<FString, AActor*> ActorsToDelete;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor &&
				Actor->GetLevel() == World->PersistentLevel &&
				Request.MatchedLabels.Contains(Actor->GetActorLabel()))
			{
				ActorsToDelete.Add(Actor->GetActorLabel(), Actor);
			}
		}

		for (const FString& Label : Request.MatchedLabels)
		{
			AActor* const* FoundActor = ActorsToDelete.Find(Label);
			if (!FoundActor || !*FoundActor)
			{
				return BuildResult(
					false,
					FString::Printf(
						TEXT("Actor disappeared before deletion in '%s': %s"),
						*Request.LevelPath,
						*Label),
					true,
					false,
					{});
			}

			AActor* Actor = *FoundActor;
			Actor->Modify();
			if (!EditorActorSubsystem->DestroyActor(Actor))
			{
				return BuildResult(
					false,
					FString::Printf(
						TEXT("Failed to delete actor in '%s': %s"),
						*Request.LevelPath,
						*Label),
					true,
					false,
					{});
			}
			++Request.Deleted;
		}

		World->MarkPackageDirty();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && Request.MatchedLabels.Contains(Actor->GetActorLabel()))
			{
				return BuildResult(
					false,
					FString::Printf(
						TEXT("Exact-label verification failed after deletion in level: %s"),
						*Request.LevelPath),
					true,
					false,
					{});
			}
		}

		if (!LevelEditorSubsystem->SaveCurrentLevel())
		{
			return BuildResult(
				false,
				FString::Printf(TEXT("Failed to save level: %s"), *Request.LevelPath),
				true,
				false,
				{});
		}
		Request.bSaved = true;

		TArray<FString> DirtyAfterSave;
		CollectDirtyEditorPackageNames(DirtyAfterSave);
		if (!DirtyAfterSave.IsEmpty())
		{
			return BuildResult(
				false,
				TEXT("Packages remain dirty after saving the changed level"),
				true,
				false,
				DirtyAfterSave);
		}
	}

	FString RestoreError;
	const bool bRestored = RestoreOriginalLevel(RestoreError);
	if (!RestoreError.IsEmpty())
	{
		return BuildResult(false, RestoreError, true, false, {});
	}
	return BuildResult(true, FString(), false, bRestored, {});
}
