#pragma once

// Earlier versions of an asset loaded into /Temp packages for a diff, and the
// lifetime of those packages: they stay loaded while a diff window or asset
// editor shows them and are unloaded once it closes (#1483).

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class ISourceControlRevision;
class ISourceControlState;
class SWindow;
class UPackage;
class UObject;

namespace MCPDiffPackage
{
	/** A package file loaded at one source control revision. Error set and Package null on failure. */
	struct FRevisionPackage
	{
		UPackage* Package = nullptr;
		UObject* Asset = nullptr;
		/** The provider's revision id, its check-in id (changelist or commit) and date. */
		FString Revision;
		int32 CheckInIdentifier = 0;
		FDateTime Date;
		FString User;
		FString Description;
		/** The file the provider wrote the revision to. */
		FString TempFile;
		FString Error;
	};

	/**
	 * The history entry Revision names: the provider's revision id, a unique
	 * prefix of 7 or more characters of it (a git hash), its revision number,
	 * or head (or empty) for the newest. Null with OutError set when none or
	 * several match. blueprint(diff) and source_control(diff) share it.
	 */
	UE_MCP_BRIDGE_API TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> FindRevision(
		const ISourceControlState& State, const FString& Revision, FString& OutError);

	/**
	 * Fetch Revision of the long package PackageName from the provider and load
	 * it into a /Temp package with DiffUtils::LoadPackageForDiff. The caller
	 * keeps it with KeepUntilClosed, or unloads it with Unload.
	 */
	UE_MCP_BRIDGE_API FRevisionPackage LoadRevision(const TSharedRef<ISourceControlRevision, ESPMode::ThreadSafe>& Revision, const FString& PackageName);

	/** Unload diff packages now, except those an open window or editor still shows. */
	UE_MCP_BRIDGE_API void Unload(const TArray<UPackage*>& Packages);

	/**
	 * Keep Packages loaded until Window closes, then unload them on the next
	 * tick. A package shown by several windows is unloaded with the last one.
	 */
	UE_MCP_BRIDGE_API void KeepUntilClosed(const TSharedRef<SWindow>& Window, const TArray<UPackage*>& Packages);

	/** Keep Asset's package loaded until its asset editor closes, then unload it. */
	UE_MCP_BRIDGE_API void KeepUntilEditorCloses(UObject* Asset);

	/** The packages' names as a JSON array, for a result to report what it keeps loaded. */
	UE_MCP_BRIDGE_API TArray<TSharedPtr<FJsonValue>> PackageNames(const TArray<UPackage*>& Packages);

	/** How many diff packages are kept for an open window or editor, for tests. */
	UE_MCP_BRIDGE_API int32 KeptCount();
}
