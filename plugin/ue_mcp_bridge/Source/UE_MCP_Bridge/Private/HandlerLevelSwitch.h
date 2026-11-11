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

namespace MCPLevelSwitch
{
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
