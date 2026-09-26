#include "LevelHandlers.h"

#include "HandlerUtils.h"
#include "Engine/LevelStreamingDynamic.h"
#include "EditorLevelUtils.h"
#include "Handlers/HandlerEditorState.h"

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
		Result->SetArrayField(TEXT("matchedLabels"), MCPStringListToJson(Request.MatchedLabels));
		Result->SetArrayField(TEXT("missingLabels"), MCPStringListToJson(Request.MissingLabels));
		Result->SetArrayField(TEXT("duplicateLabels"), MCPStringListToJson(Request.DuplicateLabels));
		Result->SetArrayField(TEXT("wrongClassLabels"), MCPStringListToJson(Request.WrongClassLabels));
		Result->SetArrayField(TEXT("nonPersistentLabels"), MCPStringListToJson(Request.NonPersistentLabels));
		Result->SetArrayField(TEXT("protectedLabels"), MCPStringListToJson(Request.ProtectedLabels));
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
	MCPReadParamsAhead(Params, {
		TEXT("levels"), TEXT("dryRun"), TEXT("onMissing"), TEXT("restoreOriginalLevel"),
	});

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
	if (!TryGetArrayParam(Params, TEXT("levels"), LevelValues) ||
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

	UWorld* OriginalWorld = GetEditorWorld();
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
	if (auto Dirty = MCPEditorState::RefuseIfDirty(
		TEXT("Refusing to load levels while content or map packages are dirty"), InitialDirtyPackages))
	{
		return Dirty;
	}

	auto LoadEditorLevel = [](const FString& LevelPath, FString& OutError) -> bool
	{
		return MCPEditorState::LoadLevelViaHandler(&FLevelHandlers::LoadLevel, LevelPath, OutError);
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
		// Whether this call actually changed anything, rather than leaving the
		// caller to infer it from two counters. A dry run and an
		// onMissing=ignore replay whose labels are already gone both reach here
		// having deleted nothing and saved nothing.
		Result->SetBoolField(TEXT("unchanged"), !bAnyAppliedChanges);
		if (bAnyAppliedChanges)
		{
			// Stated only when something was really committed, because a dry
			// run has nothing to undo in the first place.
			MCPSetNoRollback(Result,
				TEXT("Actors were destroyed and every changed level was saved to disk, so the .umap files no longer hold them. ")
				TEXT("Undoing that would need a call that re-creates an actor with its full serialized state inside a named level, and the bridge has no such action."));
		}
		Result->SetStringField(TEXT("originalLevelPath"), OriginalLevelPath);
		Result->SetBoolField(TEXT("restoreOriginalLevelRequested"), bRestoreOriginalLevel);
		Result->SetBoolField(TEXT("restoredOriginalLevel"), bRestoredOriginalLevel);
		Result->SetArrayField(TEXT("levels"), LevelResults);
		if (!DirtyPackages.IsEmpty())
		{
			Result->SetArrayField(TEXT("dirtyPackages"), MCPStringListToJson(DirtyPackages));
		}
		return MCPResult(Result);
	};

	// Restored explicitly rather than on exit, because each result reports
	// whether the restore happened.
	MCPEditorState::FScopedLevelRestore OriginalLevel(&FLevelHandlers::LoadLevel, false);
	if (bRestoreOriginalLevel)
	{
		OriginalLevel.SetLevelPath(OriginalLevelPath);
	}
	auto RestoreOriginalLevel = [&OriginalLevel](FString& OutError) -> bool
	{
		return OriginalLevel.Restore(OutError);
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

		UWorld* World = GetEditorWorld();
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
		MCPEditorState::CollectDirtyEditorPackageNames(DirtyAfterLoad);
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

		UWorld* World = GetEditorWorld();
		FString InspectError;
		if (!InspectLoadedLevel(World, Request, InspectError))
		{
			return BuildResult(false, InspectError, true, false, {});
		}

		TArray<FString> DirtyAfterLoad;
		MCPEditorState::CollectDirtyEditorPackageNames(DirtyAfterLoad);
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
		MCPEditorState::CollectDirtyEditorPackageNames(DirtyAfterSave);
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

// #204: read the current edit-target sub-level. UE drops new actors into this
// level when multiple sub-levels are loaded; without a way to query/set it the
// caller can't reliably target a particular streaming sub-level for spawns.
TSharedPtr<FJsonValue> FLevelHandlers::GetCurrentEditLevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	ULevel* Cur = World->GetCurrentLevel();
	auto Result = MCPSuccess();
	if (Cur)
	{
		Result->SetStringField(TEXT("levelName"), Cur->GetOuter()->GetName());
		Result->SetStringField(TEXT("levelPath"), Cur->GetOuter()->GetPathName());
		Result->SetBoolField(TEXT("isPersistent"), Cur == World->PersistentLevel);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetCurrentEditLevel(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("levelName"),
	});

	REQUIRE_EDITOR_WORLD(World);
	// levelPath is a spec alias, renamed to levelName before this runs (#1057).
	FString LevelName;
	if (auto Err = RequireString(Params, TEXT("levelName"), LevelName)) return Err;

	ULevelEditorSubsystem* LES = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
	if (!LES) return MCPError(TEXT("LevelEditorSubsystem not available"));

	// The level that was current. Held as a POINTER as well as a name: two
	// sub-levels can share a name, so comparing names afterwards would report
	// a real switch between namesakes as "nothing happened".
	ULevel* PreviousLevel = World->GetCurrentLevel();
	FString PreviousLevelName;
	FString PreviousLevelPath;
	if (PreviousLevel)
	{
		PreviousLevelName = PreviousLevel->GetOuter()->GetName();
		PreviousLevelPath = PreviousLevel->GetOuter()->GetPathName();
	}

	// SetCurrentLevelByName is first-match-wins on a duplicated name, and the
	// payload can carry nothing but that name, so count the namesakes now and
	// let the response say whether the inverse is exact or a coin flip.
	int32 PreviousNameMatches = 0;
	if (!PreviousLevelName.IsEmpty())
	{
		for (ULevel* Candidate : World->GetLevels())
		{
			if (Candidate && Candidate->GetOuter() &&
				Candidate->GetOuter()->GetName() == PreviousLevelName)
			{
				++PreviousNameMatches;
			}
		}
	}

	const bool bOk = LES->SetCurrentLevelByName(FName(*LevelName));
	if (!bOk)
	{
		return MCPError(FString::Printf(TEXT("No loaded sub-level named '%s'"), *LevelName));
	}

	ULevel* Cur = World->GetCurrentLevel();
	auto Result = MCPSuccess();
	const FString NewLevelName = Cur ? Cur->GetOuter()->GetName() : FString();
	const bool bLevelChanged = Cur != PreviousLevel;
	if (bLevelChanged) MCPSetUpdated(Result); else MCPSetExisted(Result);
	// Present in both branches: MCPSetExisted does not write `updated`, and a
	// consumer branching on it must not read undefined.
	Result->SetBoolField(TEXT("updated"), bLevelChanged);
	Result->SetBoolField(TEXT("unchanged"), !bLevelChanged);
	if (Cur)
	{
		Result->SetStringField(TEXT("levelName"), NewLevelName);
		Result->SetStringField(TEXT("levelPath"), Cur->GetOuter()->GetPathName());
	}
	Result->SetStringField(TEXT("previousLevelName"), PreviousLevelName);
	Result->SetStringField(TEXT("previousLevelPath"), PreviousLevelPath);

	if (bLevelChanged && !PreviousLevelName.IsEmpty())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("levelName"), PreviousLevelName);
		MCPSetRollback(Result, TEXT("set_current_edit_level"), Payload);
		if (PreviousNameMatches > 1)
		{
			Result->SetBoolField(TEXT("rollbackLossy"), true);
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("%d loaded sub-levels answer to the name '%s', and SetCurrentLevelByName takes the first one it reaches. This action's only parameter is that name, so the inverse can set the current level to a DIFFERENT sub-level of the same name, and everything spawned afterwards would land in the wrong package. previousLevelPath names the one this call left; check the current level against it after replaying."),
				PreviousNameMatches, *PreviousLevelName));
		}
	}
	return MCPResult(Result);
}

// #206: streaming sub-level CRUD
namespace
{
	static ULevelStreaming* FindStreamingByName(UWorld* World, const FString& NameOrPath)
	{
		if (!World) return nullptr;
		for (ULevelStreaming* SL : World->GetStreamingLevels())
		{
			if (!SL) continue;
			const FString PkgName = SL->GetWorldAssetPackageName();
			if (PkgName == NameOrPath) return SL;
			if (FPaths::GetBaseFilename(PkgName) == NameOrPath) return SL;
			if (SL->GetName() == NameOrPath) return SL;
		}
		return nullptr;
	}
}

TSharedPtr<FJsonValue> FLevelHandlers::ListStreamingSublevels(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> Out;
	for (ULevelStreaming* SL : World->GetStreamingLevels())
	{
		if (!SL) continue;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		const FString PkgName = SL->GetWorldAssetPackageName();
		O->SetStringField(TEXT("levelName"), FPaths::GetBaseFilename(PkgName));
		O->SetStringField(TEXT("packageName"), PkgName);
		O->SetStringField(TEXT("streamingClass"), SL->GetClass()->GetName());
		O->SetBoolField(TEXT("initiallyLoaded"), SL->ShouldBeLoaded());
		O->SetBoolField(TEXT("initiallyVisible"), SL->GetShouldBeVisibleFlag());
		O->SetBoolField(TEXT("loaded"), SL->IsLevelLoaded());
		O->SetBoolField(TEXT("visible"), SL->GetShouldBeVisibleFlag());
		const FTransform T = SL->LevelTransform;
		TSharedPtr<FJsonObject> Loc = MCPVec3ToJsonObject(T.GetLocation());
		O->SetObjectField(TEXT("location"), Loc);
		Out.Add(MakeShared<FJsonValueObject>(O));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("sublevels"), Out);
	Result->SetNumberField(TEXT("count"), Out.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::AddStreamingSublevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString LevelPath; if (auto E = RequireString(Params, TEXT("levelPath"), LevelPath)) return E;

	const FString StreamingClassName = OptionalString(Params, TEXT("streamingClass"), TEXT("LevelStreamingDynamic"));
	UClass* StreamingClass = ULevelStreamingDynamic::StaticClass();
	if (StreamingClassName.Equals(TEXT("LevelStreamingAlwaysLoaded"), ESearchCase::IgnoreCase))
	{
		StreamingClass = LoadClass<ULevelStreaming>(nullptr, TEXT("/Script/Engine.LevelStreamingAlwaysLoaded"));
		if (!StreamingClass) StreamingClass = ULevelStreamingDynamic::StaticClass();
	}

	ULevelStreaming* SL = UEditorLevelUtils::AddLevelToWorld(World, *LevelPath, StreamingClass);
	if (!SL)
	{
		return MCPError(FString::Printf(TEXT("Failed to add sub-level '%s'"), *LevelPath));
	}

	if (HasParam(Params, TEXT("initiallyLoaded"))) SL->SetShouldBeLoaded(OptionalBool(Params, TEXT("initiallyLoaded"), true));
	if (HasParam(Params, TEXT("initiallyVisible"))) SL->SetShouldBeVisible(OptionalBool(Params, TEXT("initiallyVisible"), true));

	if (HasParam(Params, TEXT("location")))
	{
		FTransform T = SL->LevelTransform;
		T.SetLocation(OptionalVec3(Params, TEXT("location")));
		SL->LevelTransform = T;
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("levelPath"), LevelPath);
	Result->SetStringField(TEXT("levelName"), FPaths::GetBaseFilename(LevelPath));

	// remove_streaming_sublevel resolves by package name, which is what
	// AddLevelToWorld recorded on the streaming level it returned.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("levelName"), SL->GetWorldAssetPackageName());
	MCPSetRollback(Result, TEXT("remove_streaming_sublevel"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::RemoveStreamingSublevel(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("levelName"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString Name;
	// levelPath is a spec alias, renamed to levelName before this runs (#1057).
	if (auto Err = RequireString(Params, TEXT("levelName"), Name)) return Err;

	ULevelStreaming* SL = FindStreamingByName(World, Name);
	if (!SL)
	{
		// Idempotent: a sub-level that is not in the world is the state this
		// call asks for, so a replayed rollback is a no-op rather than a fail.
		//
		// #963's rule applies though: a destructive action that matched nothing
		// must not answer with a bare success. A typo and a completed removal
		// produce the same alreadyRemoved=true, so the response names what IS
		// in the world and says which of the two this might be.
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("levelName"), Name);
		Noop->SetBoolField(TEXT("removed"), false);
		Noop->SetBoolField(TEXT("alreadyRemoved"), true);
		Noop->SetStringField(TEXT("zeroMatchNote"),
			TEXT("No streaming sub-level answers to that name. This is idempotent, not a statement that it was ever there: an already-removed sub-level and a misspelt name look identical here. Compare against candidates[] before concluding the removal happened."));
		TArray<TSharedPtr<FJsonValue>> Candidates;
		for (ULevelStreaming* Other : World->GetStreamingLevels())
		{
			if (!Other) continue;
			Candidates.Add(MakeShared<FJsonValueString>(Other->GetWorldAssetPackageName()));
		}
		Noop->SetArrayField(TEXT("candidates"), Candidates);
		return MCPResult(Noop);
	}

	// Everything add_streaming_sublevel can put back, read before the removal.
	const FString PackageName = SL->GetWorldAssetPackageName();
	const bool bWasLoaded = SL->ShouldBeLoaded();
	const bool bWasVisible = SL->GetShouldBeVisibleFlag();
	const FVector PreviousLocation = SL->LevelTransform.GetLocation();
	const bool bWasAlwaysLoaded = SL->GetClass()->GetName().Contains(TEXT("AlwaysLoaded"));

	ULevel* Loaded = SL->GetLoadedLevel();
	if (Loaded)
	{
		UEditorLevelUtils::RemoveLevelFromWorld(Loaded);
	}
	World->RemoveStreamingLevels({ SL });

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("levelName"), Name);
	Result->SetStringField(TEXT("packageName"), PackageName);
	Result->SetBoolField(TEXT("removed"), true);
	Result->SetBoolField(TEXT("alreadyRemoved"), false);

	if (!PackageName.IsEmpty())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("levelPath"), PackageName);
		Payload->SetStringField(TEXT("streamingClass"),
			bWasAlwaysLoaded ? TEXT("LevelStreamingAlwaysLoaded") : TEXT("LevelStreamingDynamic"));
		Payload->SetBoolField(TEXT("initiallyLoaded"), bWasLoaded);
		Payload->SetBoolField(TEXT("initiallyVisible"), bWasVisible);
		Payload->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(PreviousLocation));
		MCPSetRollback(Result, TEXT("add_streaming_sublevel"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The biggest loss first: removing a sub-level discards the loaded ULevel, and adding it back LOADS IT FROM DISK, so every unsaved edit to every actor inside it is gone and does not come back. Beyond that, the sub-level returns with its package, its loaded and visible flags and its transform location, but add_streaming_sublevel only builds a LevelStreamingDynamic or a LevelStreamingAlwaysLoaded, so any other streaming class becomes the nearest of those two, and the streaming level's remaining properties, the transform rotation and its position in the world's streaming list are not restored."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetStreamingSublevelProperties(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("levelName"), TEXT("initiallyLoaded"), TEXT("initiallyVisible"), TEXT("location"), TEXT("editorVisible"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString Name;
	// levelPath is a spec alias, renamed to levelName before this runs (#1057).
	if (auto Err = RequireString(Params, TEXT("levelName"), Name)) return Err;

	ULevelStreaming* SL = FindStreamingByName(World, Name);
	if (!SL) return MCPError(FString::Printf(TEXT("Streaming sub-level not found: %s"), *Name));

	// Every field this action can write, as it stands, so the inverse is this
	// same call with the values that were there.
	const bool bPreviousLoaded = SL->ShouldBeLoaded();
	const bool bPreviousVisible = SL->GetShouldBeVisibleFlag();
	const FVector PreviousLocation = SL->LevelTransform.GetLocation();
	// Read the field the write actually drives. UEditorLevelUtils::SetLevelVisibility
	// sets ULevelStreaming::bShouldBeVisibleInEditor; ULevel::bIsVisible is the
	// transient "is it associated with the world right now" state, which is a
	// different question and can disagree mid-transition. Restoring from the
	// wrong one would put back a value this call never changed.
	const bool bPreviousEditorVisible = SL->GetShouldBeVisibleInEditor();

	bool bChanged = false;
	if (HasParam(Params, TEXT("initiallyLoaded"))) { SL->SetShouldBeLoaded(OptionalBool(Params, TEXT("initiallyLoaded"), true)); bChanged = true; }
	if (HasParam(Params, TEXT("initiallyVisible"))) { SL->SetShouldBeVisible(OptionalBool(Params, TEXT("initiallyVisible"), true)); bChanged = true; }

	const TSharedPtr<FJsonObject>* LocObj = nullptr;
	if (TryGetObjectParam(Params, TEXT("location"), LocObj) && LocObj && (*LocObj).IsValid())
	{
		double X = 0, Y = 0, Z = 0;
		(*LocObj)->TryGetNumberField(TEXT("x"), X);
		(*LocObj)->TryGetNumberField(TEXT("y"), Y);
		(*LocObj)->TryGetNumberField(TEXT("z"), Z);
		FTransform T = SL->LevelTransform;
		T.SetLocation(FVector(X, Y, Z));
		SL->LevelTransform = T;
		bChanged = true;
	}

	bool bEditorVisibleSet = false;
	bool bEditorVisibleSkipped = false;
	const bool bEditorVisible = OptionalBool(Params, TEXT("editorVisible"), true);
	if (HasParam(Params, TEXT("editorVisible")))
	{
		ULevel* Loaded = SL->GetLoadedLevel();
		if (Loaded)
		{
			UEditorLevelUtils::SetLevelVisibility(Loaded, bEditorVisible, false);
			bEditorVisibleSet = true;
		}
		else
		{
			// The sub-level is not loaded, so there is nothing to show or hide
			// and no write happened. Flagging it as set anyway made the
			// response claim a write and emit a rollback record for it.
			bEditorVisibleSkipped = true;
		}
	}

	auto Result = MCPSuccess();
	if (bChanged) MCPSetUpdated(Result); else MCPSetExisted(Result);
	Result->SetStringField(TEXT("levelName"), Name);
	Result->SetBoolField(TEXT("initiallyLoaded"), SL->ShouldBeLoaded());
	Result->SetBoolField(TEXT("initiallyVisible"), SL->GetShouldBeVisibleFlag());
	if (bEditorVisibleSet) Result->SetBoolField(TEXT("editorVisible"), bEditorVisible);
	if (bEditorVisibleSkipped)
	{
		Result->SetStringField(TEXT("editorVisibleNote"),
			TEXT("editorVisible was passed but the sub-level is not loaded, so there was nothing to show or hide and no write was made. Load it first if you meant to change its editor visibility."));
	}

	if (bChanged || bEditorVisibleSet)
	{
		// Restate only the fields this call actually wrote. Passing the others
		// would write values the caller never asked to change.
		//
		// Addressed by the resolved PACKAGE NAME, never by the caller's token.
		// FindStreamingByName also matches on the base filename, and
		// /Game/A/Sub and /Game/B/Sub both answer to "Sub", so replaying the
		// token could write this sub-level's old flags onto a different one.
		// The package name is the pass that resolver checks first and the only
		// one that is unique.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("levelName"), SL->GetWorldAssetPackageName());
		if (HasParam(Params, TEXT("initiallyLoaded"))) Payload->SetBoolField(TEXT("initiallyLoaded"), bPreviousLoaded);
		if (HasParam(Params, TEXT("initiallyVisible"))) Payload->SetBoolField(TEXT("initiallyVisible"), bPreviousVisible);
		if (HasParam(Params, TEXT("location"))) Payload->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(PreviousLocation));
		if (bEditorVisibleSet) Payload->SetBoolField(TEXT("editorVisible"), bPreviousEditorVisible);
		MCPSetRollback(Result, TEXT("set_streaming_sublevel_properties"), Payload);
	}
	return MCPResult(Result);
}
