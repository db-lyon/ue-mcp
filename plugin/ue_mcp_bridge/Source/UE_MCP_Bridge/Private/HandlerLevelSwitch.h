#pragma once

// Switching the editor's open map when the map that is open has unsaved
// changes (#1851). Shared by level(load) and demo(go_home), so the rule for
// what a switch would lose, and how a discard is carried out, is written once.

#include "CoreMinimal.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "LevelEditorSubsystem.h"
#include "UObject/Package.h"

#include "HandlerEngineVersion.h"
#include "HandlerJsonConvert.h"
#include "HandlerUtils.h"

namespace MCPLevelSwitch
{
	/**
	 * Any play session is up: Play, Simulate, a Play session ejected with F8
	 * (which reads as simulating), or a start queued for the next tick (#1912).
	 * PlayWorld alone misses the queued start, and "PlayWorld and not
	 * simulating" misses an ejected session, which is how an agent read "no
	 * session" while the user was mid-play.
	 */
	inline bool IsPlaySessionActive()
	{
		return GEditor && (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor || GEditor->IsPlaySessionInProgress());
	}

	/** "play", "simulate", "queued" or "editor", for a response. An ejected Play session reads as "simulate". */
	inline FString PlaySessionMode()
	{
		if (!GEditor) return TEXT("editor");
		if (GEditor->bIsSimulatingInEditor) return TEXT("simulate");
		if (GEditor->PlayWorld != nullptr) return TEXT("play");
		if (GEditor->IsPlaySessionInProgress()) return TEXT("queued");
		return TEXT("editor");
	}

	/**
	 * The answer a world-replacing action gives while a play session is up and
	 * the caller did not pass endPIE=true. Replacing the editor world ends the
	 * session, so it is never done without being asked (#1912).
	 */
	inline TSharedPtr<FJsonObject> PlaySessionRefusal(const FString& What)
	{
		const FString Mode = PlaySessionMode();
		TSharedPtr<FJsonObject> Refusal = MCPErrorObject(FString::Printf(
			TEXT("A play session (%s) is running, and %s would end it. Pass endPIE=true to end the session and go ahead, or %s Nothing was changed."),
			*Mode, *What, *MCPStopPlayInEditorStep()));
		Refusal->SetStringField(TEXT("reason"), TEXT("play_session_active"));
		Refusal->SetBoolField(TEXT("playSessionActive"), true);
		Refusal->SetStringField(TEXT("playMode"), Mode);
		return Refusal;
	}

	/** End the play session, a queued start included. True when there was one to end. */
	inline bool EndPlaySession()
	{
		if (!IsPlaySessionActive()) return false;
		if (GEditor->IsPlaySessionRequestQueued()) GEditor->CancelRequestPlaySession();
		if (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor) GEditor->EndPlayMap();
		return true;
	}
	/**
	 * Every dirty package a switch away from EditorWorld would lose.
	 *
	 * FEditorFileUtils::GetDirtyWorldPackages alone is not that list: it skips
	 * worlds outside the writable folders, and a never-saved /Temp/Untitled map
	 * can be one of them. UE 5.9's ULevelEditorSubsystem::LoadLevel counts the
	 * open level's own packages as well (GetUnlistedDirtyOpenLevelPackages in
	 * LevelEditorSubsystem.cpp), so a list that missed them refused nothing
	 * here and then failed in the engine. Those are added the same way: the
	 * level package, its built data and its loaded external packages, leaving
	 * out never-saved external packages that are empty again.
	 */
	inline TArray<UPackage*> CollectDirtyPackages(UWorld* EditorWorld)
	{
		TArray<UPackage*> Packages;
		FEditorFileUtils::GetDirtyWorldPackages(Packages);
		Packages.RemoveAll([](UPackage* Package) { return !Package || !Package->IsDirty(); });

		const auto AddIfDirty = [&Packages](UPackage* Package)
		{
			if (!Package || !Package->IsDirty()) return;
			if (Package->HasAnyPackageFlags(PKG_NewlyCreated) && UPackage::IsEmptyPackage(Package)) return;
			Packages.AddUnique(Package);
		};
		if (EditorWorld)
		{
			for (ULevel* Level : EditorWorld->GetLevels())
			{
				if (!Level) continue;
				AddIfDirty(Level->GetPackage());
				if (Level->MapBuildData) AddIfDirty(Level->MapBuildData->GetPackage());
				for (UPackage* External : Level->GetLoadedExternalObjectPackages())
				{
					AddIfDirty(External);
				}
			}
		}
		return Packages;
	}

	/** Package names, sorted, for a response. */
	inline TArray<FString> PackageNames(const TArray<UPackage*>& Packages)
	{
		TArray<FString> Names;
		for (UPackage* Package : Packages)
		{
			if (Package) Names.AddUnique(Package->GetName());
		}
		Names.Sort();
		return Names;
	}

	/** True when a package is a map no path can save to: an untitled or /Temp world. */
	inline bool IsUnsavedMapPackage(const FString& PackageName)
	{
		return FPackageName::IsTempPackage(PackageName) || !FPackageName::IsValidLongPackageName(PackageName);
	}

	/**
	 * Drop the unsaved changes in Packages so the map switch that follows can
	 * proceed, the way editor(discard_dirty) does: the dirty flag is cleared and
	 * read back. Returns the names that were dirty again at once, which the
	 * caller must treat as a failed discard.
	 *
	 * Clearing the flag is also what lets the old world go. Map_Load keeps a
	 * world whose own package is dirty loaded through the switch, so a dirty
	 * untitled map would otherwise outlive the load it was discarded for.
	 */
	inline TArray<FString> DiscardChanges(const TArray<UPackage*>& Packages)
	{
		TArray<FString> StillDirty;
		for (UPackage* Package : Packages)
		{
			if (!Package) continue;
			Package->SetDirtyFlag(false);
			if (Package->IsDirty()) StillDirty.AddUnique(Package->GetName());
		}
		StillDirty.Sort();
		return StillDirty;
	}

	/** What Prepare did on the way to a switch, for the response. */
	struct FSwitchPreparation
	{
		bool bEndedPlaySession = false;
		TArray<FString> SavedPackages;
		TArray<FString> DiscardedPackages;
	};

	/**
	 * Answer for everything replacing the editor world would lose, before it is
	 * replaced: a running play session (endPIE) and the open map's unsaved
	 * packages (saveFirst or discardUnsaved), read from Params (#1912, #1259).
	 * Shared by level(load) and level(create), so a switch is refused, saved or
	 * discarded the same way whichever action makes it.
	 *
	 * Returns a refusal to hand back, with nothing changed, or nullptr when the
	 * switch may go ahead. By then the session is ended and the packages are
	 * saved or discarded as asked, and Out says which.
	 */
	inline TSharedPtr<FJsonObject> Prepare(const TSharedPtr<FJsonObject>& Params, const FString& What, FSwitchPreparation& Out)
	{
		const bool bEndPIE = OptionalBool(Params, TEXT("endPIE"), false);
		const bool bSaveFirst = OptionalBool(Params, TEXT("saveFirst"), false);
		const bool bDiscardUnsaved = OptionalBool(Params, TEXT("discardUnsaved"), false);
		if (bSaveFirst && bDiscardUnsaved)
		{
			return MCPErrorObject(TEXT("Pass saveFirst or discardUnsaved, not both."));
		}

		// Every refusal comes before anything is changed.
		if (IsPlaySessionActive() && !bEndPIE)
		{
			return PlaySessionRefusal(What);
		}
		const TArray<FString> DirtyBefore = PackageNames(CollectDirtyPackages(GetEditorWorld()));
		if (DirtyBefore.Num() > 0 && !bDiscardUnsaved)
		{
			if (!bSaveFirst)
			{
				TSharedPtr<FJsonObject> Refusal = MCPErrorObject(FString::Printf(
					TEXT("The open map has %d unsaved package(s) that %s would discard. Pass saveFirst=true to save them first, or discardUnsaved=true to drop them. Nothing was unloaded."),
					DirtyBefore.Num(), *What));
				Refusal->SetArrayField(TEXT("dirtyPackages"), MCPStringListToJson(DirtyBefore));
				Refusal->SetStringField(TEXT("reason"), TEXT("unsaved_changes"));
				return Refusal;
			}
			// An untitled map has no path to save to, and saving it would raise a modal Save As dialog.
			for (const FString& Name : DirtyBefore)
			{
				if (IsUnsavedMapPackage(Name))
				{
					TSharedPtr<FJsonObject> Refusal = MCPErrorObject(FString::Printf(
						TEXT("'%s' is an untitled map with no file to save to. Save it under a path first with level(save_as, levelPath=\"/Game/...\"), or pass discardUnsaved=true. Nothing was unloaded."), *Name));
					Refusal->SetArrayField(TEXT("dirtyPackages"), MCPStringListToJson(DirtyBefore));
					Refusal->SetStringField(TEXT("reason"), TEXT("untitled_map"));
					return Refusal;
				}
			}
		}

		// The play world goes first: a map cannot be saved while it is up, and
		// a switch right after a session asserts on the old world unless the
		// session is over (#590).
		Out.bEndedPlaySession = EndPlaySession();

		if (DirtyBefore.Num() > 0 && bSaveFirst)
		{
			UEditorLoadingAndSavingUtils::SaveDirtyPackages(/*bSaveMapPackages*/ true, /*bSaveContentPackages*/ false);
			const TArray<FString> StillDirty = PackageNames(CollectDirtyPackages(GetEditorWorld()));
			if (StillDirty.Num() > 0)
			{
				TSharedPtr<FJsonObject> Refusal = MCPErrorObject(FString::Printf(
					TEXT("saveFirst could not save %d package(s); the output log carries the reason. Nothing was unloaded."), StillDirty.Num()));
				Refusal->SetArrayField(TEXT("stillDirty"), MCPStringListToJson(StillDirty));
				Refusal->SetBoolField(TEXT("endedPlaySession"), Out.bEndedPlaySession);
				return Refusal;
			}
			Out.SavedPackages = DirtyBefore;
		}

		// discardUnsaved is carried out here rather than left to the engine. Its
		// LoadLevel refuses on any dirty level package from UE 5.9 unless told to
		// discard, and on every version a world whose package is still dirty is
		// kept loaded through the switch. Clearing the flags first is what
		// editor(discard_dirty) does, and is the recovery that already worked.
		if (DirtyBefore.Num() > 0 && bDiscardUnsaved)
		{
			const TArray<FString> StillDirty = DiscardChanges(CollectDirtyPackages(GetEditorWorld()));
			if (StillDirty.Num() > 0)
			{
				TSharedPtr<FJsonObject> Refusal = MCPErrorObject(FString::Printf(
					TEXT("discardUnsaved could not discard %d package(s): each was dirty again as soon as its flag was cleared. Nothing was unloaded."), StillDirty.Num()));
				Refusal->SetArrayField(TEXT("stillDirty"), MCPStringListToJson(StillDirty));
				Refusal->SetBoolField(TEXT("endedPlaySession"), Out.bEndedPlaySession);
				return Refusal;
			}
			Out.DiscardedPackages = DirtyBefore;
		}
		return nullptr;
	}

	/** Write what Prepare did into a success response. */
	inline void ReportPreparation(const TSharedPtr<FJsonObject>& Result, const FSwitchPreparation& Prepared)
	{
		Result->SetBoolField(TEXT("endedPlaySession"), Prepared.bEndedPlaySession);
		if (Prepared.SavedPackages.Num() > 0) Result->SetArrayField(TEXT("savedPackages"), MCPStringListToJson(Prepared.SavedPackages));
		if (Prepared.DiscardedPackages.Num() > 0) Result->SetArrayField(TEXT("discardedPackages"), MCPStringListToJson(Prepared.DiscardedPackages));
	}

	/**
	 * ULevelEditorSubsystem::LoadLevel, told that unsaved changes were already
	 * answered for. From UE 5.9 it takes a policy and refuses on any dirty
	 * level package by default; before 5.9 it takes none and does not check.
	 */
	inline bool LoadLevel(ULevelEditorSubsystem& Subsystem, const FString& LevelPath, bool bDiscardUnsaved)
	{
#if UE_MCP_HAS_5_9_API
		return Subsystem.LoadLevel(LevelPath, bDiscardUnsaved
			? ELevelEditorUnsavedChangesPolicy::Discard
			: ELevelEditorUnsavedChangesPolicy::Fail);
#else
		return Subsystem.LoadLevel(LevelPath);
#endif
	}

	/** The long package name of the map the editor has open, or empty. */
	inline FString OpenMapPackageName()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		return World && World->GetOutermost() ? World->GetOutermost()->GetName() : FString();
	}
}
