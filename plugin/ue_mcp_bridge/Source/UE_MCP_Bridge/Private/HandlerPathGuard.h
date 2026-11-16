#pragma once

// Keeping a caller-chosen file path inside the folders an action may touch.
// Every handler that writes, moves or deletes at a path a caller supplied
// resolves it through MCPResolvePathInside before touching the disk.

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "HandlerResult.h"

/** True for a path built from FPaths::ProjectDir() while that is relative to the engine binaries folder. */
inline bool MCPIsEngineRelative(const FString& Path)
{
	const FString ProjectDir = FPaths::ProjectDir();
	return FPaths::IsRelative(ProjectDir) && FPaths::IsRelative(Path)
		&& Path.Replace(TEXT("\\"), TEXT("/")).StartsWith(ProjectDir, ESearchCase::IgnoreCase);
}

/**
 * Full, normalised form of Path. A relative Path is taken against BaseDir, itself made full first,
 * except one built from an FPaths directory, which is relative to the engine binaries folder.
 */
inline FString MCPFullPath(const FString& Path, const FString& BaseDir)
{
	FString Full = FPaths::IsRelative(Path) && !MCPIsEngineRelative(Path)
		? FPaths::ConvertRelativePathToFull(FPaths::ConvertRelativePathToFull(BaseDir), Path)
		: FPaths::ConvertRelativePathToFull(Path);
	FPaths::NormalizeFilename(Full);
	FPaths::CollapseRelativeDirectories(Full);
	FPaths::RemoveDuplicateSlashes(Full);
	while (Full.Len() > 1 && Full.EndsWith(TEXT("/")) && !Full.EndsWith(TEXT(":/"))) Full.LeftChopInline(1);
	return Full;
}

/** The folders a caller-chosen output path may land in: the project, its Saved folder and the user temp folder. */
inline TArray<FString> MCPDefaultWritableRoots()
{
	return { FPaths::ProjectDir(), FPaths::ProjectSavedDir(), FString(FPlatformProcess::UserTempDir()) };
}

/**
 * Resolve InPath and require it to sit inside one of AllowedRoots.
 * A relative InPath is taken against BaseDir, or the first root when BaseDir is
 * empty. The comparison follows FPaths::IsUnderDirectory, which ignores case on
 * Windows. On failure OutError names the resolved path and every allowed root.
 */
inline bool MCPResolvePathInside(
	const FString& InPath,
	const TArray<FString>& AllowedRoots,
	FString& OutFull,
	FString& OutError,
	const FString& BaseDir = FString())
{
	if (InPath.IsEmpty())
	{
		OutError = TEXT("The path is empty.");
		return false;
	}
	const FString Base = !BaseDir.IsEmpty() ? BaseDir
		: AllowedRoots.Num() > 0 ? AllowedRoots[0]
		: FPaths::ProjectDir();
	const FString Full = MCPFullPath(InPath, MCPFullPath(Base, FPaths::ProjectDir()));
	// A '..' that could not be collapsed climbed past the drive root.
	const bool bUnresolved = Full == TEXT("..") || Full.StartsWith(TEXT("../"))
		|| Full.Contains(TEXT("/../")) || Full.EndsWith(TEXT("/.."));

	TArray<FString> Roots;
	for (const FString& Root : AllowedRoots)
	{
		if (Root.IsEmpty()) continue;
		const FString RootFull = MCPFullPath(Root, FPaths::ProjectDir());
		Roots.AddUnique(RootFull);
		if (!bUnresolved && FPaths::IsUnderDirectory(Full, RootFull))
		{
			OutFull = Full;
			return true;
		}
	}
	OutError = FString::Printf(
		TEXT("'%s' resolves to '%s', which is outside the folders this action may use: %s"),
		*InPath, *Full, *FString::Join(Roots, TEXT(", ")));
	return false;
}

/** MCPResolvePathInside as a handler early return: nullptr when the path is allowed, an MCPError otherwise. */
inline TSharedPtr<FJsonValue> MCPRequirePathInside(
	const FString& InPath,
	const TArray<FString>& AllowedRoots,
	FString& OutFull,
	const FString& BaseDir = FString())
{
	FString Error;
	if (MCPResolvePathInside(InPath, AllowedRoots, OutFull, Error, BaseDir)) return nullptr;
	return MCPError(Error);
}

/** A caller-chosen output path checked against MCPDefaultWritableRoots, relative to the project. */
inline TSharedPtr<FJsonValue> MCPRequireWritablePath(const FString& InPath, FString& OutFull, const FString& BaseDir = FString())
{
	return MCPRequirePathInside(InPath, MCPDefaultWritableRoots(), OutFull, BaseDir);
}
